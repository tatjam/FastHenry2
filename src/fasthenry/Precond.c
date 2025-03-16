/* This is FastHenry's overlapped preconditioner. It is based on much of the
 code from olmulPrcond() from FastCap.  It still contains remnants of the
 actual FastCap code which could be removed. */

/* Also added is the code for the sparse preconditioner.  This preconditioner
is the default.  Most of this code is if'd out for this precond */

#include "induct.h"

// Enrico, for spGetElement()
#include "sparse/spMatrix.h"

#define PARTMESH OFF    /* this should always be OFF */

/* turn on positive definite preconditioner */
/* #define POSDEF ON */

/* This near picks up only the hamming distance one cubes. */
#define HNEAR(nbr, nj, nk, nl) \
((ABS((nbr)->j - (nj)) + ABS((nbr)->k - (nk)) + ABS((nbr)->l - (nl))) <= 1)

/* This near picks up all 27 neighboring cubes. */
#define NEAR(nbr, nj, nk, nl) \
((ABS((nbr)->j - (nj)) <= 1) && \
 (ABS((nbr)->k - (nk)) <= 1) && \
 (ABS((nbr)->l - (nl)) <= 1))

/* This near picks only the diagonal, for testing. */
#define DNEAR(nbr, nj, nk, nl) \
(((nbr)->j == (nj)) && \
 ((nbr)->k == (nk)) && \
 ((nbr)->l == (nl)) )

extern FILE *fp;
static char outfname[80];

indPrecond(sys, indsys, w)
ssystem *sys;
SYS *indsys;
double w;
{
  cube *nc, *nnbr, *nnnbr;
  static double **mat = NULL, **nmat;
  int i, j, k, l, m;
  int maxsize, nsize, nnsize, nnnsize, *reorder;
  int nj, nk, nl, offset, noffset;
  int dindex; /*, *nc_dummy, *nnbr_dummy, *nnnbr_dummy;*/
  /*static int *is_dummy;*/	/* local dummy flag vector, stays around */
  static int big_mat_size = 0;	/* size of previous mat */
  charge **nnnbr_pc, **nnbr_pc, **nc_pc, **mpc, *dp;
  surface *surf;
  double factor;
  double shiftval;

  /* FastHenry stuff */
  static CX **meshmat = NULL;
  static int meshmax = 0;       /* size of previous meshmat */
  static int *filcount = NULL;  /* number of fils per mesh for this cube and nbrs */
  static int *filcount2 = NULL; /* number of fils per mesh for this cube only*/
  static int *maxfilcount = NULL; /* max fils per mesh for any cube and nbrs */
  static int *indx = NULL;      /* index of real mesh number in meshmat */
  static int *meshnum = NULL;   /* mesh number corresponding to a row in meshmat */
         /* meshnum and indx should be inverses of each other (sort of). */
         /* i.e.  indx[meshnum[i]] == i. */
         /* meshnum[indx[i]] == i if i is one of the meshes in this cube */
  static int *fillist;      /* list of the filament numbers to which rows */
                            /* and cols of mat correspond */
  static int *findx;        /* For every filament, -1 if not in fillist, row */
                            /* number in mat if in fillist.  */
                            /* fillist and findx are inverses of each other */

  MELEMENT *getnext();

  int num_mesh = indsys->num_mesh;
  int num_fils = indsys->num_fils;
  int filnum, count, the_size;
  MELEMENT *mtran, *mtranj, *mtrani, *melem;
  MELEMENT **Mtrans = indsys->Mtrans;
  MELEMENT **Mlist = indsys->Mlist;
  PRE_ELEMENT **Precond = indsys->Precond;
  PRE_ELEMENT *pre, *prelast;
  double *R = indsys->R;
  int meshsize, realmrow, realmcol;
  int counter, mrow, mcol, posdef, usefilcount;
  static int *is_in_nc;
  static DUPS *is_dup;
  static int *is_partial;
  int debug = 0;
  int isdirect;

  int xi, yi, zi;
  CX tempsum, *elem;
  charge *filchg;
  double length = sys->length;
  double minx = sys->minx, miny = sys->miny, minz = sys->minz;
  double PrecondCost = 0;
  int totalcubes = 0;
  char *Matrix = indsys->sparMatrix;

  if (filcount == NULL) {
    CALLOC(filcount, num_mesh, int, ON, IND);
    CALLOC(filcount2, num_mesh, int, ON, IND);
    CALLOC(is_partial, num_mesh, int, ON, IND);
    CALLOC(maxfilcount, num_mesh, int, ON, IND);
    CALLOC(indx, num_mesh, int, ON, IND);
    CALLOC(findx, num_fils, int, ON, IND);
    CALLOC(is_in_nc, num_mesh, int, ON, IND);

  }
  for (i = 0; i < num_mesh; i++)
    maxfilcount[i] = 0;

  /* clear old precond */
  for(i = 0; i < num_mesh; i++)
    Precond[i] = NULL;

  /* open file for dumping Ls? */
  if (indsys->precond_type == SPARSE && (indsys->opts->dumpMats & DUMP_Ls)) {
    concat4(outfname,"Ls",indsys->opts->suffix,".mat");
    if ( (fp = fopen(outfname,"w")) == NULL) {
      printf("Couldn't open file\n");
      exit(1);
    }
  }

  isdirect = !(indsys->opts->mat_vect_prod == MULTIPOLE);

  if (mat == NULL) {
    /* Figure out the max number of elements in any set of near cubes. */
    for(maxsize=0, nc=sys->directlist; nc != NULL; nc = nc->dnext) {
      nsize = nc->upnumeles[0];
      /* nsize = nc->directnumeles[0];*/
      if (indsys->opts->mat_vect_prod == MULTIPOLE)
        ASSERT(nc->upnumeles[0] == nc->directnumeles[0]);

      nj = nc->j;
      nk = nc->k;
      nl = nc->l;
      for(i=0; i < nc->numnbrs; i++) {
        nnbr = nc->nbrs[i];
        if(NEAR(nnbr, nj, nk, nl)) nsize += nnbr->upnumeles[0];
        /* if(NEAR(nnbr, nj, nk, nl)) nsize += nnbr->directnumeles[0];*/
        if (indsys->opts->mat_vect_prod == MULTIPOLE)
          ASSERT(nnbr->upnumeles[0] == nnbr->directnumeles[0]);
      }
      maxsize = MAX(nsize, maxsize);
    }

    /* CALLOC(is_dummy, maxsize, int, ON, AMSC); */
    MALLOC(mat, maxsize, double*, ON, AMSC);
    MALLOC(fillist, maxsize, int, ON, IND);   /* filament numbers  IND stuff */
    for(i=0; i < maxsize; i++) {
      MALLOC(mat[i], maxsize, double, ON, AMSC);
    }
  }

  /* Now go fill-in a matrix. */
  /* For each cube, gather all the meshes and invert that subproblem */
  for(nc=sys->directlist; nc != NULL; nc = nc->dnext) {

    for(i = 0; i < num_mesh; i++) {
      filcount[i] = 0;
      filcount2[i] = 0;
      is_in_nc[i] = 0;
      is_partial[i] = 0;
    }
    for(i = 0; i < num_fils; i++)
      findx[i] = -1;

    /*nsize = nc->directnumeles[0];*/
    nsize = nc->upnumeles[0];
    if (indsys->opts->mat_vect_prod == MULTIPOLE)
      ASSERT(nc->upnumeles[0] == nc->directnumeles[0]);

    /* nc_dummy = nc->nbr_is_dummy[0]; */
    nc_pc = nc->chgs;

    nj = nc->j;
    nk = nc->k;
    nl = nc->l;
    for(i = nsize - 1; i >= 0; i--) {
      /* if(nc_dummy[i]) continue;*//* dummy rows copied only in divided diff */

      filnum = fillist[i] = nc_pc[i]->fil->filnumber;   /* IND stuff. 8/92 */
      findx[nc_pc[i]->fil->filnumber] = i;
      /* find all the meshes that this filament is contained within */
      for(mtran = indsys->Mtrans[filnum]; mtran != NULL; mtran=mtran->mnext) {
	filcount[mtran->filindex]++;
	filcount2[mtran->filindex]++;
	is_in_nc[mtran->filindex] = 1;
      }

      for(j = nsize - 1; j >= 0; j--) {
	if (isdirect)
	  mat[i][j] = indsys->Z[filnum][nc_pc[j]->fil->filnumber];
	else
	  mat[i][j] = nc->directmats[0][i][j];

        if (indsys->precond_subtype == SHELLS) {
          shiftval = shift_mutual(nc_pc[i]->fil, nc_pc[j]->fil, sys);
          if (fabs(shiftval) < fabs(mat[i][j]))
            mat[i][j] -= shiftval;
          else
            mat[i][j] = 0.0;
        }
      }

    }

    /* bring in nearest neighbor terms.  Shouldn't get called if SPARSE */
    offset = nsize;
    for(k=0;
        k < nc->numnbrs && (indsys->precond_type == LOC
                            || indsys->precond_subtype == SHELLS);
        k++) {     /* loop on neighbors of nc */
      nnbr = nc->nbrs[k];
      if(NEAR(nnbr, nj, nk, nl)) {
	nnsize = nc->directnumeles[k+1];
	nmat = nc->directmats[k+1];
	ASSERT(nc->directnumeles[k+1] == nnbr->directnumeles[0]);
	/* nnbr_dummy = nnbr->nbr_is_dummy[0]; */
	nnbr_pc = nnbr->chgs;

	/* IND stuff */
	for(i = 0; i < nnsize; i++) {
	  filnum = fillist[offset + i] = nnbr_pc[i]->fil->filnumber;
	  findx[nnbr_pc[i]->fil->filnumber] = offset + i;
	  for(mtran = indsys->Mtrans[filnum]; mtran!=NULL; mtran=mtran->mnext)
	    filcount[mtran->filindex]++;
	}

	for(i = nsize - 1; i >= 0; i--) {
	  /* if(nc_dummy[i]) continue; */
	  for(j = nnsize - 1; j >= 0; j--) {
	    mat[i][offset + j] = nmat[i][j];

            if (indsys->precond_subtype == SHELLS) {
              shiftval = shift_mutual(nc_pc[i]->fil, nnbr_pc[j]->fil,
                                      sys);
              if (fabs(shiftval) < fabs(mat[i][j + offset]))
                mat[i][j + offset] -= shiftval;
              else
                mat[i][j + offset] = 0.0;
            }
	  }
	}
	/* Get the row of the big matrix associated with this nnbr. */
	for(noffset = 0, l = -1; l < nc->numnbrs; l++) { /* lp on nc's nbrs */
	  if(l < 0) nnnbr = nc;
	  else nnnbr = nc->nbrs[l];
	  if(NEAR(nnnbr, nj, nk, nl)) {  /* Note, near to nc!! */
	    if(nnbr == nnnbr) m = -1;
	    else { /* Find this nnnbr's position in nnbr's list */
	      for(m=0; m < nnbr->numnbrs; m++) {
		if(nnbr->nbrs[m] == nnnbr) break;
	      }
	      ASSERT(m < nnbr->numnbrs);
	    }
	    nnnsize = nnbr->directnumeles[m+1];
	    nmat = nnbr->directmats[m+1];
	    ASSERT(nnbr->directnumeles[m+1] == nnnbr->directnumeles[0]);
	    nnnbr_pc = nnnbr->chgs; /* panels in nnnbr */
	    /* nnnbr_dummy = nnnbr->nbr_is_dummy[0]; */
#if CHKDUM == ON
	    chkDummyList(nnnbr_pc, nnnbr_dummy, nnnsize);
#endif
	    for(i = nnsize - 1; i >= 0; i--) { /* loop on panels in nnbr */
	      /* if(nnbr_dummy[i]) continue;*/

	      for(j = nnnsize - 1; j >= 0; j--) {
		mat[offset + i][noffset+j] = nmat[i][j];

                if (indsys->precond_subtype == SHELLS) {
                  /* oops, we don't really want to be in this loop over nnbr,
                     but I don't want this to be a non-square matrix.
                     it should end up as a nsize x offset matrix */
                  mat[offset + i][noffset+j] = 0.0;
                }
	      }
	    }
	    noffset += nnnsize;
	  }
	}
	offset += nnsize;
      }
    }

    /* FastHenry stuff */

#if PARTMESH == OFF
    /* check to see if a mesh is only partly in the cube + neighbors */
/*    if (indsys->precond_type == LOC && ) {*/
    if (indsys->precond_subtype == OVERLAP) {
      for(i = 0; i < num_mesh; i++)
	if (filcount[i] > 0) {
	  count = 0;
	  j = 0;
	  for(melem = Mlist[i]; melem != NULL; melem = melem->mnext) {
	    count++;
	    j += melem->sign;
	  }
	  if (count != filcount[i]) {
	    if (count <= 2 || 1==1) {
	      filcount[i] = -1;   /* this is a partial mesh */
	      /*	  fprintf(stdout,"removed partial mesh #%d\n",i); */
	    }
	    else
	      is_partial[i] = TRUE;
	  }
	}
    }
    else if (indsys->precond_subtype == POSDEF_LOC) {
      /* remove partial meshes and meshes composed of more elements outside
	 of nc or used before*/
      for(i = 0; i < num_mesh; i++)
	if (filcount2[i] > 0) {
	  count = 0;
	  j = 0;
	  for(melem = Mlist[i]; melem != NULL; melem = melem->mnext) {
	    count++;
	    j += melem->sign;
	  }
	  if (count != filcount[i] || (double)filcount2[i]/count < 0.5
	      || maxfilcount[i] != 0)
	    filcount2[i] = -1;   /* this is a partial mesh */
	}
    }
    else if (indsys->precond_type != SPARSE) {
      fprintf(stderr,"What kind of precondtioner?\n");
      exit(1);
    }

#endif

    usefilcount = indsys->precond_type == SPARSE
		   || (indsys->precond_subtype == OVERLAP);

    /* count total number of meshes */
    meshsize = 0;
    for(i = 0; i < num_mesh; i++) {
      if (usefilcount) {
	if (filcount[i] > 0)
	  meshsize++;
      }
      else if (!usefilcount) {
	if (filcount2[i] > 0)
	  meshsize++;
      }
    }

    PrecondCost += CUBE(meshsize);
    totalcubes++;

    if (meshsize > meshmax) {
      CALLOC(meshnum, meshsize + 10, int, ON, IND);
      CALLOC(meshmat, meshsize + 10, CX*, ON, IND);
      for(i = 0; i < meshsize + 10; i++)
	CALLOC(meshmat[i], meshsize + 10, CX, ON, IND);
      CALLOC(is_dup, meshsize + 10, DUPS, ON, IND);
      meshmax = meshsize + 10;
    }

    /* fill indx and meshnum vectors */
    counter = 0;
    for(i = 0; i < num_mesh; i++) {
      if ((usefilcount && filcount[i] > 0)
	  || (!usefilcount && filcount2[i] > 0)) {
	indx[i] = counter;
	meshnum[counter++] = i;
      }
      else {
	indx[i] = -1;
      }
    }
    if (counter != meshsize) {
      fprintf(stderr, "Hey, counter should equal meshsize\n");
      exit(1);
    }

    for(i = 0; i < meshsize; i++)
      for(j = 0; j < meshsize; j++)
	meshmat[i][j] = CXZERO;

    /* for each element in mat, determine it's contribution to */
    /* meshmat = M*mat*Mtrans */
    /* there may be a more efficient way to do this with some more */
    /* temporary storage. Like a temp matrix for mat*Mtran */

    posdef = indsys->precond_subtype == POSDEF_LOC;

    if (indsys->precond_subtype == SHELLS)
      the_size = nsize;    /* only care about this cube's rows */
    else
      the_size = offset;   /* == nsize for SPARSE, non shells */

    for(i = 0; i < the_size; i++)
      for(j = 0; j < offset; j++)
       if (mat[i][j] != 0.0) {
	for(mtranj=Mtrans[fillist[j]]; mtranj != NULL; mtranj = mtranj->mnext)
	  {
	    if (filcount[mtranj->filindex] > 0 && !posdef
		|| filcount2[mtranj->filindex] > 0 && posdef) {
	      mcol = indx[mtranj->filindex];
	      for(mtrani=Mtrans[fillist[i]]; mtrani!=NULL;mtrani=mtrani->mnext)
		{
		  if (filcount[mtrani->filindex] > 0 && !posdef
		      || filcount2[mtrani->filindex] > 0 && posdef) {
		    mrow = indx[mtrani->filindex];
		    meshmat[mrow][mcol].imag
		      += w*mtrani->sign*mat[i][j]*mtranj->sign;
		    if (i == j)
		      meshmat[mrow][mcol].real
			+= mtrani->sign*R[fillist[i]]*mtranj->sign;
		  }
		}
	    }
	  }
       }
       else if (i == j)
         fprintf(stderr,"Possible Bug:  self term in preconditioner == 0!\n");

    if (indsys->precond_type == SPARSE) {
      for(i = 0; i < meshsize; i++) {
	realmrow = meshnum[i];
	maxfilcount[realmrow] = filcount[realmrow];
	for(j = 0; j < meshsize; j++) {
          if ( (meshmat[i][j].real != 0.0 || meshmat[i][j].imag != 0.0) ) {
            realmcol = meshnum[j];
            (elem = (CX *)spGetElement(Matrix,realmrow+1,realmcol+1))->real
                             += meshmat[i][j].real;
            elem->imag += meshmat[i][j].imag;
          }
	}
      }
      if (indsys->opts->dumpMats & DUMP_Ls) {
        if (indsys->precond_subtype == SHELLS)
          the_size = offset;
        else
          the_size = nsize;

	for(i = 0; i < the_size; i++)
	  for(j = 0; j < the_size; j++)
            if (mat[i][j] != 0.0)
              fprintf(fp, "%d\t%d\t%20.13lg\n",fillist[i]+1,fillist[j]+1,
                      mat[i][j]);
      }
    }
    else {
      /* check if duplicate partial meshes (fills is_dup) */
      mark_dup_mesh(Mlist, meshnum, meshsize, is_dup, findx);

      if (debug == 1) {
	fp = fopen("chkinv.mat","w");
	if (fp == NULL) {printf("no open\n"); exit(1); }
	savecmplx(fp, "before", meshmat, meshsize, meshsize);
      }

      if (indsys->opts->debug == ON)
	fprintf(stdout, "Inverting a %d x %d matrix\n",meshsize,meshsize);

      /* for experiment */
      /*
	for(i=0; i<meshsize;i++)
	for(j=0; j<meshsize;j++)
	meshmat[i][j].imag = 0.0;
	*/

      /* now invert meshmat and skip duplicate rows and cols */
      cx_invert_dup(meshmat, meshsize, is_dup);

      if (debug == 1) {
	savecmplx(fp, "after", meshmat, meshsize, meshsize);
	fclose(fp);
      }

      /* add the rows to the preconditioner */
      /* this uses the allocated PRE_ELEMENTs that are there. */
      /* It is based on the fact that there are going to be more */
      for(i = 0; i < meshsize; i++) {
	if (is_in_nc[meshnum[i]] != 1 || is_dup[i].sign != 0
	    || is_partial[meshnum[i]] == TRUE) {
	  /* this mesh is in one of the neighbors or it's a duplicate */
	  continue;
	}
	realmrow = meshnum[i];
	if (filcount[ realmrow ] > maxfilcount[ realmrow ]) {
	  maxfilcount[realmrow] = filcount[realmrow];
	  if (Precond[realmrow] == NULL) {
	    CALLOC(Precond[realmrow], 1, PRE_ELEMENT, ON, IND);
	    Precond[realmrow]->next = NULL;
	  }
	  prelast = NULL;
	  for(j = 0, pre = Precond[realmrow]; j < meshsize; j++) {
	    if (pre == NULL) {
	      CALLOC(pre, 1, PRE_ELEMENT, ON, IND);
	      pre->next = NULL;
	      if (prelast == NULL) {
		fprintf(stderr, "Hey, prelast is null!\n");
		exit(1);
	      }
	      prelast->next = pre;
	    }

	    pre->meshcol = meshnum[j];
	    if (is_dup[j].sign == 0)
	      pre->value = meshmat[i][j];
	    else
	      /* it's a duplicate, so use the duplicates inverse value. */
	      /* this effectively 'adds' the mesh currents of all duplicates */
	      cx_scalar_mult(pre->value,
			     is_dup[j].sign, meshmat[i][is_dup[j].dup]);
	    prelast = pre;
	    pre = pre->next;
	  }
	}
      }

    } /* end if local-inv precond */
  }


/* precondition the big meshes */
/*  bigmeshPre(sys, indsys, w);  */

  /* make sure all meshes get preconditioned */

  if (indsys->precond_type == LOC) {
    for(i = 0; i < num_mesh; i++)
      if (Precond[i] == NULL) {
	/* set to PARTMESH = BLAH if calling bigmeshPre() */
#if PARTMESH == OFF
	/* make the identity */
	if (indsys->opts->debug == ON)
	  printf("mesh %d partial everywhere\n",i);
	CALLOC(Precond[i], 1, PRE_ELEMENT, ON, IND);
	Precond[i]->next = NULL;
	Precond[i]->meshcol = i;
	Precond[i]->value = CXONE;

	/* let's do better than the identity */
	/* Try adding up self terms in mesh and inverting */

	tempsum = CXZERO;
	for(melem = Mlist[i]; melem != NULL; melem = melem->mnext) {
	  filchg = melem->fil->pchg;
	  xi =  (filchg->x - minx) / length;
	  yi =  (filchg->y - miny) / length;
	  zi =  (filchg->z - minz) / length;
	  nc = sys->cubes[sys->depth][xi][yi][zi];
	  if (nc == NULL) {
	    fprintf(stderr, "Hey, why isn't there a cube for this charge?\n");
	    exit(1);
	  }
	  nc_pc = nc->chgs;
	  j = 0;
	  while(j < nc->directnumeles[0] && filchg != nc_pc[j])
	    j++;
	  if (j == nc->directnumeles[0]) {
	    fprintf(stderr,"Hey, why isn't the charge in the cube?\n");
	    exit(1);
	  }
	  tempsum.real += R[melem->filindex];
	  tempsum.imag += w*nc->directmats[0][j][j];
	}

	/* for experiment */
	/*    tempsum.imag = 0; */

	cx_div(Precond[i]->value, CXONE, tempsum);
	if (indsys->opts->debug == ON)
	  fprintf(stdout, "Sum of self terms: %lg +i*%lg\n",tempsum.real, tempsum.imag);

#else
	fprintf(stderr, "Hey, mesh %d is never included in any cube??\n",i);
	exit(1);
#endif
      }
  }
  else {
    count = 0;
    for(i = 0; i < num_mesh; i++)
      if (maxfilcount[i] == 0) {
	fprintf(stderr, "Internal Err: mesh %d not used in Preconditioner\n"
		,i);
	count++;
      }
    if (count != 0)
      exit(1);
  }

  if (indsys->precond_type != SPARSE
      && (indsys->opts->dumpMats & PRE))
    dumpPrecond(Precond, num_mesh, indsys->opts->suffix);

  if (indsys->precond_type == SPARSE && (indsys->opts->dumpMats & DUMP_Ls))
    fclose(fp);

  if (indsys->opts->debug == ON)
    fprintf(stdout, "Actual PrecCost: %lg, aver_mat:%lg\n",
	    PrecondCost, pow(PrecondCost/totalcubes,1.0/3.0));

}

/* multiplies x times the preconditioner and returns in result */

multPrecond(Precond, x, result, size)
PRE_ELEMENT **Precond;
CX *x, *result;
int size;
{

  PRE_ELEMENT *pre;
  int i;
  CX temp;

  for(i = 0; i < size; i++) {
    result[i] = CXZERO;
    for(pre = Precond[i]; pre != NULL; pre = pre->next) {
      cx_mul(temp, x[pre->meshcol], pre->value);
      cx_add(result[i], result[i], temp);
    }
  }
}

/* if mel->filindex is not in the cube and nearest nbrs (findx == -1), skip */
/* to the next element which is */
MELEMENT *getnext(mel, findx)
 MELEMENT *mel;
 int *findx;
{
  while(mel != NULL && findx[mel->filindex] == -1)
    mel = mel->mnext;

  return mel;
}
/*
  In-place inverts a matrix using guass-jordan.
  Skips rows and columns with is_dup[i].sign != 1.
*/
cx_invert_dup(mat, size, is_dup)
CX **mat;
int size;
DUPS *is_dup;
{
  int i, j, k;
  CX normal, multiplier, tmp;

  for(i=0; i < size; i++) {
    if (is_dup[i].sign != 0) continue;
    /* First i^{th} column of A. */
    cx_div(normal, CXONE, mat[i][i]);
    for(j=0; j < size; j++) {
      if (is_dup[j].sign != 0) continue;
      cx_mul(tmp, mat[j][i], normal);
      mat[j][i] = tmp;
    }
    mat[i][i] = normal;

    /* Fix the backward columns. */
    for(j=0; j < size; j++) {
      if (is_dup[j].sign != 0) continue;
      if(j != i) {
	cx_mul(multiplier, CXMONE, mat[i][j]);
	for(k=0; k < size; k++) {
	  if (is_dup[k].sign != 0) continue;
	  cx_mul(tmp, mat[k][i], multiplier);
	  if(k != i) cx_add(mat[k][j], mat[k][j], tmp);
	  else mat[k][j] = tmp;
	}
      }
    }
  }
}

/* If the meshes which only have part of themselves in the cube
   and it's nearest neighbors happen to be identical to other
   partial meshes, then the meshmat will have two identical rows/columns
   for each identical pair.  This marks one of the duplicates so it
   will be skipped by the inversion routine.
   In essence, two identical meshes carry two different currents and
   we wish their sum to be the actual current which gets preconditioned,
   so when we form the preconditioner later, the duplicate mesh will
   get the same entry as the original */

mark_dup_mesh(Mlist, meshnum, meshsize, is_dup, findx)
MELEMENT **Mlist;
int *meshnum, meshsize, *findx;
DUPS *is_dup;

{
  int i,j;
  MELEMENT *meli, *melj;
  int different, sign;

    for(i = 0; i < meshsize; i++)
      is_dup[i].sign = 0;

    for(i = 0; i < meshsize; i++) {  /* compare mesh i with all the others */
      if (is_dup[i].sign == 0) {
	for(j = i + 1; j < meshsize; j++) {
	  if (is_dup[j].sign == 0) {
	    meli = getnext(Mlist[meshnum[i]], findx);
	    melj = getnext(Mlist[meshnum[j]], findx);
	    different = FALSE;
	    while(meli != NULL && melj != NULL && different == FALSE) {
	      if (meli->filindex != melj->filindex)
		different = TRUE;
	      else {
		sign = meli->sign*melj->sign;
		meli = getnext(meli->mnext, findx);
		melj = getnext(melj->mnext, findx);
	      }
	    }
	    if (different == FALSE) /* they match up to shorter list length */
	      if (meli == NULL && melj == NULL) { /* same length */
		/* mark the duplicate */
		is_dup[j].sign = sign;
		is_dup[j].dup = i;
		fprintf(stderr,"Duplicate mesh marked\n");
	      }
	  } /* endif */
	} /*for j*/
      } /* endif */
    } /* for i*/
}

dumpPrecond(Precond, size, suffix)
PRE_ELEMENT **Precond;
int size;
char *suffix;
{
  FILE *fp;
  int i,j;
  int machine;
  double *temprow;
  int rows, cols;
  PRE_ELEMENT *pre;

  rows = cols = size;

  printf("Dumping Preconditioner...\n");
  CALLOC(temprow, cols, double, ON, IND);

  concat4(outfname,"Pre",suffix,".mat");
  fp = fopen(outfname,"w");
  if (fp == NULL) {
    fprintf(stderr,"Couldn't open Pre\n");
    exit(1);
  }

  machine = 1000;
#ifdef DEC
  machine = 2000;
#endif

  /* this only saves the real part */
  for(i = 0; i < rows; i++) {
    for(j = 0; j < cols; j++)
      temprow[j] = 0;
    for(pre = Precond[i]; pre != NULL; pre = pre->next)
      temprow[pre->meshcol] = pre->value.real;
    savemat_mod(fp, machine+100, "Pre", rows, cols, 1, temprow,
		  (double *)NULL, i, cols);
  }

  /* do imaginary part */
  for(i = 0; i < rows; i++) {
    for(j = 0; j < cols; j++)
      temprow[j] = 0;
    for(pre = Precond[i]; pre != NULL; pre = pre->next)
      temprow[pre->meshcol] = pre->value.imag;
    savemat_mod(fp, machine+100, "Pre", rows, cols, 1, temprow,
		  (double *)NULL, 1, cols);
  }

  fclose(fp);
  printf("Done\n");
}



indPrecond_direct(sys, indsys, w)
ssystem *sys;
SYS *indsys;
double w;
{
  cube *nc, *nnbr, *nnnbr;
  int nsize, nnsize;
  charge **nc_pc, **nnbr_pc;
  int meshmax = 0, *meshnum;
  CX **meshmat = NULL;
  int *filcount = NULL, *is_in_nc, *maxfilcount, *indx;
  int num_mesh = indsys->num_mesh;
  int filnum, i, j, k, nj, nk, nl;
  MELEMENT *mtran;
  MELEMENT **Mtrans = indsys->Mtrans;
  MELEMENT **Mlist = indsys->Mlist;
  PRE_ELEMENT **Precond = indsys->Precond;
  PRE_ELEMENT *pre, *prelast;
  int meshsize, realmrow;
  int counter;
  int debug = 0;

  if (filcount == NULL) {
    CALLOC(filcount, num_mesh, int, ON, IND);
    CALLOC(is_in_nc, num_mesh, int, ON, IND);
    CALLOC(maxfilcount, num_mesh, int, ON, IND);
    CALLOC(indx, num_mesh, int, ON, IND);

    for (i = 0; i < num_mesh; i++)
      maxfilcount[i] = 0;
  }

  /* clear old precond */
  for(i = 0; i < num_mesh; i++)
    Precond[i] = NULL;

/* Now go fill-in a matrix. */
  for(nc=sys->directlist; nc != NULL; nc = nc->dnext) {

    for(i = 0; i < num_mesh; i++) {
      filcount[i] = 0;
      is_in_nc[i] = 0;
    }

    nsize = nc->upnumeles[0];
    nc_pc = nc->chgs;
    nj = nc->j;
    nk = nc->k;
    nl = nc->l;
    for(i = nsize - 1; i >= 0; i--) {
      filnum = nc_pc[i]->fil->filnumber;   /* IND stuff. 8/92 */
      /* find all the meshes that this filament is contained within */
      for(mtran = indsys->Mtrans[filnum]; mtran != NULL; mtran=mtran->mnext) {
	filcount[mtran->filindex]++;
	is_in_nc[mtran->filindex] = 1;
      }
    }
    for(k=0; k < nc->numnbrs; k++) { /* loop on neighbors of nc */
      nnbr = nc->nbrs[k];
      if(NEAR(nnbr, nj, nk, nl)) {
	nnsize = nnbr->upnumeles[0];
	nnbr_pc = nnbr->chgs;

	/* IND stuff */
	for(i = 0; i < nnsize; i++) {
	  filnum = nnbr_pc[i]->fil->filnumber;
	  for(mtran = indsys->Mtrans[filnum]; mtran!=NULL; mtran=mtran->mnext)
	    filcount[mtran->filindex]++;
	}

      }
    }

    /* count total number of meshes */
    meshsize = 0;
    for(i = 0; i < num_mesh; i++)
      if (filcount[i] > 0) meshsize++;

    if (meshsize > meshmax) {
      CALLOC(meshnum, meshsize + 10, int, ON, IND);
      CALLOC(meshmat, meshsize + 10, CX*, ON, IND);
      for(i = 0; i < meshsize + 10; i++)
	CALLOC(meshmat[i], meshsize + 10, CX, ON, IND);
      meshmax = meshsize + 10;
    }

    /* fill indx and meshnum vectors */
    counter = 0;
    for(i = 0; i < num_mesh; i++) {
      if (filcount[i] > 0) {
	indx[i] = counter;
	meshnum[counter++] = i;
      }
      else {
	indx[i] = -1;
      }
    }
    if (counter != meshsize) {
      fprintf(stderr, "Hey, counter should equal meshsize\n");
      exit(1);
    }

    for(i = 0; i < meshsize; i++)
      for(j = 0; j < meshsize; j++)
	meshmat[i][j] = indsys->MtZM[meshnum[i]][meshnum[j]];

    if (debug == 1) {
      fp = fopen("chkinv.mat","w");
      if (fp == NULL) {printf("no open\n"); exit(1); }
      savecmplx(fp, "before", meshmat, meshsize, meshsize);
    }

    if (indsys->opts->debug == ON)
      fprintf(stdout, "Inverting a %d x %d matrix\n",meshsize,meshsize);

    /* now invert meshmat and skip duplicate rows and cols */
    cx_invert(meshmat, meshsize);

    if (debug == 1) {
      savecmplx(fp, "after", meshmat, meshsize, meshsize);
      fclose(fp);
    }

    /* add the rows to the preconditioner */
    for(i = 0; i < meshsize; i++) {
      if (is_in_nc[meshnum[i]] != 1) {
	/* this mesh is in one of the neighbors or it's a duplicate */
	continue;
      }
      realmrow = meshnum[i];
      if (filcount[ realmrow ] > maxfilcount[ realmrow ]) {
	maxfilcount[realmrow] = filcount[realmrow];
	if (Precond[realmrow] == NULL) {
	  CALLOC(Precond[realmrow], 1, PRE_ELEMENT, ON, IND);
	  Precond[realmrow]->next = NULL;
	}
	prelast = NULL;
	for(j = 0, pre = Precond[realmrow]; j < meshsize; j++) {
	  if (pre == NULL) {
	    CALLOC(pre, 1, PRE_ELEMENT, ON, IND);
	    pre->next = NULL;
	    if (prelast == NULL) {
	      fprintf(stderr, "Hey, prelast is null!\n");
	      exit(1);
	    }
	    prelast->next = pre;
	  }

	  pre->meshcol = meshnum[j];
	  pre->value = meshmat[i][j];
	  prelast = pre;
	  pre = pre->next;
	}
      }

#if 1==0   /* a stupid way */
      for(j = 0, pre = Precond[realmrow]; j < meshsize; j++) {
	if (is_in_Precond(Precond[realmrow], meshnum[j], &prelast) == 0) {
	  CALLOC(pre, 1, PRE_ELEMENT, ON, IND);
	  pre->meshcol = meshnum[j];
	  pre->value = meshmat[i][j];
	  if (prelast == NULL) {
	    pre->next = Precond[realmrow];
	    Precond[realmrow] = pre;
	  }
	  else {
	    pre->next = prelast->next;
	    prelast->next = pre;
	  }
	}
      }
#endif


    }

  }

/*  bigmesh_direct(sys, indsys, w); */

  /* make sure all meshes get preconditioned */
  for(i = 0; i < num_mesh; i++)
    if (Precond[i] == NULL) {
/*
      CALLOC(Precond[i], 1, PRE_ELEMENT, ON, IND);
      Precond[i]->next = NULL;
      Precond[i]->meshcol = i;
      cx_div(Precond[i]->value, CXONE, indsys->MtZM[i][i]);
      fprintf(stdout, "self term: %lg +i*%lg\n",indsys->MtZM[i][i].real,
	      indsys->MtZM[i][i].imag);
*/
      fprintf(stderr, "Hey, mesh %d is never included in any cube??\n",i);
      exit(1);
    }
  if (indsys->opts->dumpMats & PRE)
    dumpPrecond(Precond, num_mesh, indsys->opts->suffix);

}
