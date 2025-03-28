# Makefile for
#  zbuf - capacitance or inductance visualization picture

# assumed places for unaliased executables - must be same in all Makefile.*
SHELL = /bin/sh
AWK = awk
RM = /bin/rm

MAKETYPE = other
# normally overidden by master Makefile
TYPE = other

INCL = .
IO = .
DIR = .
MUL = .
ALC = .
COM = .
GEN = .
BIN = ../../bin
TOP = ..

OBJS = $(MUL)/mulGlobal.o $(DIR)/extras.o \
$(MUL)/zbuf.o \
$(ALC)/uglieralloc.o \
$(IO)/patran.o $(IO)/quickif.o \
$(IO)/input.o \
$(IO)/zbufInOut.o $(IO)/zbuf2fastcap.o $(IO)/zbufProj.o $(IO)/zbufSort.o \
$(IO)/dump_struct.o $(IO)/savemat_mod.o

SRCS = $(MUL)/mulGlobal.c $(DIR)/extras.c \
$(MUL)/zbuf.c \
$(ALC)/uglieralloc.c \
$(IO)/patran.c $(IO)/quickif.c \
$(IO)/input.c \
$(IO)/zbufInOut.c $(IO)/zbuf2fastcap.c $(IO)/zbufProj.c $(IO)/zbufSort.c \
$(IO)/dump_struct.c $(IO)/savemat_mod.c

LIBS = -lm

# These compile flags are needed to account for differences in the
#  resource usage system call used to get CPU time and memory use data
#  - also used to select the correct CC macro definition for AIX machines
#
# for x86 64bit generic machine use (timers not supported)
CFLAGS = -O -DFOUR -m64 -Wno-return-mismatch -Wno-implicit-int -Wno-implicit-function-declaration
# for most machines running 4.2/3 use (only necessary if timers are needed)
#CFLAGS = -O -DFOUR
# for most machines running System V use (only necessary if timers are needed)
#CFLAGS = -O -DFIVE
# for IBM AIX systems use
#CC = xlc

zbuf: $(OBJS)
	$(CC) -o zbuf $(CFLAGS) $(OBJS) $(LIBS)
	mv zbuf $(BIN)/zbuf

clean:
	$(RM) -f *.o

$(OBJS) : $(INCL)/mulStruct.h $(INCL)/mulGlobal.h $(INCL)/patran.h \
$(INCL)/resusage.h Makefile
$(SRCS) : $(INCL)/mulStruct.h $(INCL)/mulGlobal.h $(INCL)/patran.h \
$(INCL)/resusage.h Makefile
$(IO)/quickif.c $(IO)/quickif.o : $(IO)/quickif.h Makefile
