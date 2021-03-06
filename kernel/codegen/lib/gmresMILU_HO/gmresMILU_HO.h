#ifndef GMRESMILU_HO_H
#define GMRESMILU_HO_H
#include <stddef.h>
#include <stdlib.h>
#include "rtwtypes.h"
#include "gmresMILU_HO_types.h"

extern void gmresMILU_HO(const struct0_T *A, const emxArray_real_T *b, const
  emxArray_struct1_T *M, int restart, double rtol, int maxit, const
  emxArray_real_T *x0, int verbose, int nthreads, emxArray_real_T *x, int *flag,
  int *iter, emxArray_real_T *resids);
extern void gmresMILU_HO_initialize(void);
extern void gmresMILU_HO_terminate(void);

#endif
