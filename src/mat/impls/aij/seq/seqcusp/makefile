#requirespackage 'PETSC_HAVE_CUSP'
ALL: lib

CFLAGS    =
FFLAGS    =
SOURCECU  = aijcusp.cu aijAssemble.cu
SOURCEF   =
SOURCEH   = cuspmatimpl.h
LIBBASE   = libpetscmat
DIRS      =
MANSEC    = Mat
LOCDIR    = src/mat/impls/aij/seq/seqcusp/

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
