CFLAGS = -O -DFOUR -m64 -Wno-return-mismatch -Wno-implicit-int -Wno-implicit-function-declaration

SHELL = /bin/sh
RM = /bin/rm
BIN = ../../bin

SPLIB = sparse/sparse.a

OBJS = induct.o gmres.o savemat_mod.o readGeom.o joelself.o writefastcap.o \
       SetupMulti.o calcp.o SetupComputePsi.o mulSetup.o BreakupSeg.o \
       Precond.o addgroundplane.o findpaths.o fillM.o cx_ludecomp.o \
       parse_command_line.o default_opts.o Prec_cost.o hole.o \
       dist_betw_fils.o mutual.o newPrecond.o deg_mutual.o \
       barnoldi.o regurgitate.o

MOBJS = mulGlobal.o mulDo.o mulMulti.o mulLocal.o mulMats.o direct.o \
        uglieralloc.o capsolve.o 

NONUNIOBJS = find_nonuni_path.o read_tree.o contact.o

HEADER = induct.h cmplx.h resusage.h
MULHEAD = mulStruct.h mulGlobal.h patran.h resusage.h
NONUNIHEAD = gp.h

fasthenry:	$(OBJS) $(MOBJS) $(NONUNIOBJS) $(SPLIB)
	$(CC) -o fasthenry $(CFLAGS) $(OBJS) $(MOBJS) $(NONUNIOBJS) $(SPLIB) -lm
	mv fasthenry $(BIN)/fasthenry

$(SPLIB):
	cd sparse; make

clean:
	$(RM) -f *.o
	cd sparse; make clean

$(OBJS):	$(HEADER) $(MULHEAD)

$(MOBJS):	$(MULHEAD)

$(NONUNIOBJS):  $(HEADER) $(MULHEAD) $(NONUNIHEAD)
