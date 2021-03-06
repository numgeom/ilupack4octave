/* $Id: DGNLselbinv.c 808 2015-12-02 08:21:38Z bolle $ */
/* ========================================================================== */
/* === DGNLselbinv mexFunction ============================================== */
/* ========================================================================== */

/*
    Usage:

    Given a block-structured approximate factorization

          P^T Deltal A Deltar P ~ BL BD BUT^T

    with a block diagonal matrix BD, block lower triangular factor BL, BUT,
    return the properly reordered and rescaled diagonal part D ~ diag(A^{-1})
    as well as a block-structured approximate selective inverse

       (P^T Deltal A Deltar P)^{-1} ~ BUTinv+BDinv+BLinv^T

    where BDinv is again block diagonal and BUTinv, BLinv are block lower
    triangular


    Example:

    [D, BLinv,BDinv,BUTinv]=DGNLselbinv(BL,BD,BUT,perm, Deltal,Deltar)


    Authors:

        Matthias Bollhoefer, TU Braunschweig

    Date:

        November 22, 2015. ILUPACK V2.5.

    Notice:

        Copyright (c) 2015 by TU Braunschweig.  All Rights Reserved.

        THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY
        EXPRESSED OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.

    Availability:

        This file is located at

        http://ilupack.tu-bs.de/
*/

/* ========================================================================== */
/* === Include files and prototypes ========================================= */
/* ========================================================================== */

#include "matrix.h"
#include "mex.h"
#include <blas.h>
#include <ilupack.h>
#include <ilupackmacros.h>
#include <lapack.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 100
#define MAX(A, B) (((A) >= (B)) ? (A) : (B))
#define MIN(A, B) (((A) >= (B)) ? (B) : (A))
#define ELBOW MAX(4.0, 2.0)
/* #define PRINT_CHECK */
/* #define PRINT_INFO  */

/* ========================================================================== */
/* === mexFunction ========================================================== */
/* ========================================================================== */

void mexFunction(
    /* === Parameters ======================================================= */

    int nlhs,             /* number of left-hand sides */
    mxArray *plhs[],      /* left-hand side matrices */
    int nrhs,             /* number of right--hand sides */
    const mxArray *prhs[] /* right-hand side matrices */
    ) {
  mwSize dims[1];
  const char *BLnames[] = {"J", "I", "L", "D"};
  mxArray *BL, *BL_block, *BL_blockJ, *BL_blockI, *BL_blockL, *BL_blockD, *BD,
      *BD_block, *BD_blockD, *BUT, *BUT_block, *BUT_blockJ, *BUT_blockI,
      *BUT_blockL, *BUT_blockD, *BLinv, *BLinv_block, *BLinv_blockJ,
      *BLinv_blockI, *BLinv_blockL, *BLinv_blockD, *BLinv_blocki,
      *BLinv_blockJi, *BLinv_blockIi, *BLinv_blockLi, *BLinv_blockDi, *BDinv,
      *BDinv_block, *BDinv_blockJ, *BDinv_blockI, *BDinv_blockL, *BDinv_blockD,
      *BDinv_blocki, *BDinv_blockJi, *BDinv_blockIi, *BDinv_blockLi,
      *BDinv_blockDi, *BUTinv, *BUTinv_block, *BUTinv_blockJ, *BUTinv_blockI,
      *BUTinv_blockL, *BUTinv_blockD, *BUTinv_blocki, *BUTinv_blockJi,
      *BUTinv_blockIi, *BUTinv_blockLi, *BUTinv_blockDi, *perm, *Deltal,
      *Deltar;

  integer i, j, k, l, m, n, p, q, r, s, t, tt, ii, jj, flag, *ipiv,
      size_gemm_buff, sumn, level3_BLAS, copy_cnt, *block, nblocks, n_size,
      ml_size, mut_size, ni_size, mi_size, i_first, j_first, k_first, jit_first,
      kt_first, Ji_cont, Ik_cont, Ii_cont, Jit_cont, Ikt_cont;
  double val, alpha, beta, *Dbuff, *work, *gemm_buff, *pr, *pr2, *pr3, *pr4,
      *prBDinvJ, *prBDinvD, *prBDinvDi, *prBLJ, *prBLI, *prBLL, *prBLD, *prBDD,
      *prBUTJ, *prBUTI, *prBUTL, *prBUTD, *prBLinvJ, *prBLinvI, *prBLinvL,
      *prBLinvD, *prBLinvJi, *prBLinvIi, *prBLinvLi, *prBLinvDi, *prBUTinvJ,
      *prBUTinvI, *prBUTinvL, *prBUTinvD, *prBUTinvJi, *prBUTinvIi, *prBUTinvLi,
      *prBUTinvDi;
  mwIndex *ja, *ia;

  if (nrhs != 6)
    mexErrMsgTxt("Six input arguments required.");
  else if (nlhs != 4)
    mexErrMsgTxt("wrong number of output arguments.");

  /* The first input must be a cell array.*/
  BL = (mxArray *)prhs[0];
  /* get size of input cell array BL */
  nblocks = MAX(mxGetM(BL), mxGetN(BL));
#ifdef PRINT_CHECK
  if (mxGetM(BL) != 1 && mxGetN(BL) != 1) {
    mexPrintf("!!!BL must be a 1-dim. cell array!!!\n");
    fflush(stdout);
  }
#endif
  if (!mxIsCell(BL)) {
    mexErrMsgTxt("First input matrix must be a cell array.");
  }
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter BL imported\n");
  fflush(stdout);
#endif

  /* The second input must be a cell array as well.*/
  BD = (mxArray *)prhs[1];
  if (!mxIsCell(BD)) {
    mexErrMsgTxt("Second input matrix must be a cell array.");
  }
  /* get size of input matrix BD */
  if (MAX(mxGetM(BD), mxGetN(BD)) != nblocks) {
    mexErrMsgTxt(
        "Second input must be a cell array of same size as the first input.");
  }
#ifdef PRINT_CHECK
  if (mxGetM(BD) != 1 && mxGetN(BD) != 1) {
    mexPrintf("!!!BD must be a 1-dim. cell array!!!\n");
    fflush(stdout);
  }
#endif
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter BD imported\n");
  fflush(stdout);
#endif

  /* The third input must be a cell array.*/
  BUT = (mxArray *)prhs[2];
  /* get size of input matrix BUT */
  if (MAX(mxGetM(BUT), mxGetN(BUT)) != nblocks) {
    mexErrMsgTxt(
        "Third input must be a cell array of same size as the first input.");
  }
#ifdef PRINT_CHECK
  if (mxGetM(BUT) != 1 && mxGetN(BUT) != 1) {
    mexPrintf("!!!BUT must be a 1-dim. cell array!!!\n");
    fflush(stdout);
  }
#endif
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter BUT imported\n");
  fflush(stdout);
#endif

  /* The fourth input must be an "integer" vector */
  perm = (mxArray *)prhs[3];
  if (!mxIsNumeric(perm)) {
    mexErrMsgTxt("Fourth input vector must be in dense format.");
  }
  /* get size of input vector */
  n = mxGetM(perm) * mxGetN(perm);
#ifdef PRINT_CHECK
  if (mxGetM(perm) != 1 && mxGetN(perm) != 1) {
    mexPrintf("!!!perm must be a 1-dim. array!!!\n");
    fflush(stdout);
  }
#endif
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter perm imported\n");
  fflush(stdout);
#endif

  /* The fifth input must be dense a vector */
  Deltal = (mxArray *)prhs[4];
  if (!mxIsNumeric(Deltal)) {
    mexErrMsgTxt("Fifth input vector must be in dense format.");
  }
  /* get size of input matrix Delta */
  if (MAX(mxGetM(Deltal), mxGetN(Deltal)) != n) {
    mexErrMsgTxt(
        "Fifth argument must be a vector of same size as the third one.");
  }
#ifdef PRINT_CHECK
  if (mxGetM(Deltal) != 1 && mxGetN(Deltal) != 1) {
    mexPrintf("!!Deltal must be a 1-dim. array!!!\n");
    fflush(stdout);
  }
#endif
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter Deltal imported\n");
  fflush(stdout);
#endif

  /* The sixth input must be dense a vector */
  Deltar = (mxArray *)prhs[5];
  if (!mxIsNumeric(Deltar)) {
    mexErrMsgTxt("Sixth input vector must be in dense format.");
  }
  /* get size of input matrix Delta */
  if (MAX(mxGetM(Deltar), mxGetN(Deltar)) != n) {
    mexErrMsgTxt(
        "Sixth argument must be a vector of same size as the third one.");
  }
#ifdef PRINT_CHECK
  if (mxGetM(Deltar) != 1 && mxGetN(Deltar) != 1) {
    mexPrintf("!!!Deltar must be a 1-dim. array!!!\n");
    fflush(stdout);
  }
#endif
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameter Deltar imported\n");
  fflush(stdout);
#endif

#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: input parameters imported\n");
  fflush(stdout);
#endif

  /* create output cell array BLinv, BDinv, BUTinv of length "nblocks" */
  dims[0] = nblocks;
  plhs[1] = mxCreateCellArray((mwSize)1, dims);
  BLinv = plhs[1];
  plhs[2] = mxCreateCellArray((mwSize)1, dims);
  BDinv = plhs[2];
  plhs[3] = mxCreateCellArray((mwSize)1, dims);
  BUTinv = plhs[3];

  /* auxiliary arrays for inverting diagonal blocks using dsytri_ */
  ipiv = (integer *)MAlloc((size_t)n * sizeof(integer), "DGNLselbinv:ipiv");
  work = (doubleprecision *)MAlloc((size_t)n * sizeof(doubleprecision),
                                   "DGNLselbinv:work");
  /* auxiliary buff for output D */
  Dbuff = (doubleprecision *)MAlloc((size_t)n * sizeof(doubleprecision),
                                    "DGNLselbinv:Dbuff");
  /* auxiliary buffer for level 3 BLAS */
  size_gemm_buff = n;
  gemm_buff = (doubleprecision *)MAlloc((size_t)size_gemm_buff *
                                            sizeof(doubleprecision),
                                        "DGNLselbinv:gemm_buff");

  /* inverse mapping index -> block number */
  block = (integer *)CAlloc((size_t)n, sizeof(integer), "DGNLselbinv:block");
  for (i = 0; i < nblocks; i++) {
    BL_block = mxGetCell(BL, i);
    if (!mxIsStruct(BL_block))
      mexErrMsgTxt("Field BL{i} must be a structure.");
    BL_blockJ = mxGetField(BL_block, 0, "J");
    if (BL_blockJ == NULL)
      mexErrMsgTxt("Field BL{i}.J does not exist.");
    if (!mxIsNumeric(BL_blockJ))
      mexErrMsgTxt("Field BL{i}.J must be numerical.");
    n_size = mxGetN(BL_blockJ) * mxGetM(BL_blockJ);
#ifdef PRINT_CHECK
    if (mxGetM(BL_blockJ) != 1 && mxGetN(BL_blockJ) != 1) {
      mexPrintf("!!!BL{%d}.J must be a 1-dim. array!!!\n", i + 1);
      fflush(stdout);
    }
#endif
    prBLJ = (double *)mxGetPr(BL_blockJ);
    for (k = 0; k < n_size; k++) {
      j = *prBLJ++;
/* remember that the structure stores indices from 1,...,n */
#ifdef PRINT_CHECK
      if (j < 1 || j > n) {
        mexPrintf("!!!index %d=BL{%d}.J(%d) out of range!!!\n", j, i + 1,
                  k + 1);
        fflush(stdout);
      }
      if (block[j - 1] != 0) {
        mexPrintf("!!!block[%d]=%d nonzero!!!\n", j, block[j - 1] + 1);
        fflush(stdout);
      }
#endif
      block[j - 1] = i;
    } /* end for k */

    BUT_block = mxGetCell(BUT, i);
    if (!mxIsStruct(BUT_block))
      mexErrMsgTxt("Field BUT{i} must be a structure.");
    BUT_blockJ = mxGetField(BUT_block, 0, "J");
    if (BUT_blockJ == NULL)
      mexErrMsgTxt("Field BUT{i}.J does not exist.");
    if (!mxIsNumeric(BUT_blockJ))
      mexErrMsgTxt("Field BUT{i}.J must be numerical.");
    n_size = mxGetN(BUT_blockJ) * mxGetM(BUT_blockJ);
#ifdef PRINT_CHECK
    if (mxGetM(BUT_blockJ) != 1 && mxGetN(BUT_blockJ) != 1) {
      mexPrintf("!!!BUT{%d}.J must be a 1-dim. array!!!\n", i + 1);
      fflush(stdout);
    }
#endif
#ifdef PRINT_CHECK
    prBUTJ = (double *)mxGetPr(BUT_blockJ);
    for (k = 0; k < n_size; k++) {
      j = *prBUTJ++;
      /* remember that the structure stores indices from 1,...,n */
      if (j < 1 || j > n) {
        mexPrintf("!!!index %d=BUT{%d}.J(%d) out of range!!!\n", j, i + 1,
                  k + 1);
        fflush(stdout);
      }
    } /* end for k */
#endif

  } /* end for i */
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: inverse mapping index -> block number computed\n");
  fflush(stdout);
  for (i = 0; i < n; i++)
    mexPrintf("%4d", block[i] + 1);
  mexPrintf("\n");
  fflush(stdout);
#endif

  /* start selective block inversion from the back */
  k = nblocks - 1;

  /* extract BL{k} */
  BL_block = mxGetCell(BL, k);

  /* extract source BL{k}.J */
  BL_blockJ = mxGetField(BL_block, 0, "J");
  n_size = mxGetN(BL_blockJ) * mxGetM(BL_blockJ);
  prBLJ = (double *)mxGetPr(BL_blockJ);

  /* BL{k}.I and BL{k}.L should be empty */

  /* extract source BL{k}.D */
  BL_blockD = mxGetField(BL_block, 0, "D");
  if (BL_blockD == NULL)
    mexErrMsgTxt("Field BL{k}.D does not exist.");
  if (mxGetN(BL_blockD) != n_size || mxGetM(BL_blockD) != n_size ||
      !mxIsNumeric(BL_blockD))
    mexErrMsgTxt(
        "Field BL{k}.D must be square dense matrix of same size as BL{k}.J.");
  /* numerical values of BL{k}.D */
  prBLD = (double *)mxGetPr(BL_blockD);

  /* extract BD{k} */
  BD_block = mxGetCell(BD, k);
  if (!mxIsStruct(BD_block))
    mexErrMsgTxt("Field BD{k} must be a structure.");

  /* extract source BD{k}.D */
  BD_blockD = mxGetField(BD_block, 0, "D");
  if (BD_blockD == NULL)
    mexErrMsgTxt("Field BD{k}.D does not exist.");
  if (mxGetN(BD_blockD) != n_size || mxGetM(BD_blockD) != n_size ||
      !mxIsSparse(BD_blockD))
    mexErrMsgTxt(
        "Field BD{k}.D must be square sparse matrix of same size as BL{k}.J.");
  /* sparse representation of BD{k}.D */
  ia = (mwIndex *)mxGetJc(BD_blockD);
  ja = (mwIndex *)mxGetIr(BD_blockD);
  prBDD = (double *)mxGetPr(BD_blockD);

  /* extract BUT{k} */
  BUT_block = mxGetCell(BUT, k);

  /* extract source BUT{k}.J */
  BUT_blockJ = mxGetField(BUT_block, 0, "J");
  n_size = mxGetN(BUT_blockJ) * mxGetM(BUT_blockJ);
  prBUTJ = (double *)mxGetPr(BUT_blockJ);

  /* BUT{k}.I and BUT{k}.L should be empty */

  /* extract source BUT{k}.D */
  BUT_blockD = mxGetField(BUT_block, 0, "D");
  if (BUT_blockD == NULL)
    mexErrMsgTxt("Field BUT{k}.D does not exist.");
  if (mxGetN(BUT_blockD) != n_size || mxGetM(BUT_blockD) != n_size ||
      !mxIsNumeric(BUT_blockD))
    mexErrMsgTxt(
        "Field BUT{k}.D must be square dense matrix of same size as BUT{k}.J.");
  /* numerical values of BUT{k}.D */
  prBUTD = (double *)mxGetPr(BUT_blockD);

  /* set up new block column for BLinv{k} with four elements J, I, L, D */
  BLinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

  /* structure element 0:  J */
  /* create BLinv{k}.J */
  BLinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
  /* copy data */
  prBLinvJ = (double *)mxGetPr(BLinv_blockJ);
  memcpy(prBLinvJ, prBLJ, (size_t)n_size * sizeof(double));
  /* set each field in BLinv_block structure */
  mxSetFieldByNumber(BLinv_block, (mwIndex)0, 0, BLinv_blockJ);

  /* structure element 1:  I */
  /* create empty BLinv{k}.I */
  ml_size = 0;
  BLinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)ml_size, mxREAL);
  /* set each field in BLinv_block structure */
  mxSetFieldByNumber(BLinv_block, (mwIndex)0, 1, BLinv_blockI);

  /* structure element 2:  L */
  /* create empty BLinv{k}.L */
  BLinv_blockL = mxCreateDoubleMatrix((mwSize)ml_size, (mwSize)n_size, mxREAL);
  /* set each field in BLinv_block structure */
  mxSetFieldByNumber(BLinv_block, (mwIndex)0, 2, BLinv_blockL);

  /* structure element 3:  D */
  /* create empty sparse n_size x n_size matrix BLinv{k}.D */
  BLinv_blockD =
      mxCreateSparse((mwSize)n_size, (mwSize)n_size, (mwSize)0, mxREAL);
  /* set each field in BLinv_block structure */
  mxSetFieldByNumber(BLinv_block, (mwIndex)0, 3, BLinv_blockD);

  /* set up new block column for BUTinv{k} with four elements J, I, L, D */
  BUTinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

  /* structure element 0:  J */
  /* create BUTinv{k}.J */
  BUTinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
  /* copy data */
  prBUTinvJ = (double *)mxGetPr(BUTinv_blockJ);
  memcpy(prBUTinvJ, prBUTJ, (size_t)n_size * sizeof(double));
  /* set each field in BUTinv_block structure */
  mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 0, BUTinv_blockJ);

  /* structure element 1:  I */
  /* create empty BUTinv{k}.I */
  mut_size = 0;
  BUTinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)mut_size, mxREAL);
  /* set each field in BUTinv_block structure */
  mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 1, BUTinv_blockI);

  /* structure element 2:  L */
  /* create empty BUTinv{k}.L */
  BUTinv_blockL =
      mxCreateDoubleMatrix((mwSize)mut_size, (mwSize)n_size, mxREAL);
  /* set each field in BUTinv_block structure */
  mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 2, BUTinv_blockL);

  /* structure element 3:  D */
  /* create empty sparse n_size x n_size matrix BUTinv{k}.D */
  BUTinv_blockD =
      mxCreateSparse((mwSize)n_size, (mwSize)n_size, (mwSize)0, mxREAL);
  /* set each field in BUTinv_block structure */
  mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 3, BUTinv_blockD);

  /* set up new block column for BDinv{k} with four elements J, I, L, D */
  BDinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

  /* structure element 0:  J */
  /* create BDinv{k}.J */
  BDinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
  /* copy data from BL{k} */
  prBDinvJ = (double *)mxGetPr(BDinv_blockJ);
  memcpy(prBDinvJ, prBLJ, (size_t)n_size * sizeof(double));
  /* set each field in BDinv_block structure */
  mxSetFieldByNumber(BDinv_block, (mwIndex)0, 0, BDinv_blockJ);

  /* structure element 1:  I */
  /* create empty BDinv{k}.I */
  BDinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)0, mxREAL);
  /* set each field in BDinv_block structure */
  mxSetFieldByNumber(BDinv_block, (mwIndex)0, 1, BDinv_blockI);

  /* structure element 2:  L */
  /* create empty BDinv{k}.L */
  BDinv_blockL = mxCreateDoubleMatrix((mwSize)0, (mwSize)n_size, mxREAL);
  /* set each field in BDinv_block structure */
  mxSetFieldByNumber(BDinv_block, (mwIndex)0, 2, BDinv_blockL);

  /* structure element 3:  D */
  /* create dense n_size x n_size matrix BDinv{k}.D */
  BDinv_blockD = mxCreateDoubleMatrix((mwSize)n_size, (mwSize)n_size, mxREAL);
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  /* copy strict upper triangular part from BUT{k}.D column to row + diagonal
   * part from BD{k}.D */
  for (j = 0; j < n_size; j++) {
    /* no pivoting */
    ipiv[j] = j + 1;

    /* advance BDinv{k}.D to its diagonal part */
    pr = prBDinvD + j * n_size + j;
    /* copy diagonal part from BD{k}.D and advance pointer */
    *pr = *prBDD;
    pr += n_size;

    /* advance source BUT{k}.D to its strict lower triangular part of column j
     */
    prBUTD += j + 1;
    /* copy strict lower triangular part from BUT{k}.D, multiplied by diagonal
     * entry */
    for (i = j + 1; i < n_size; i++, pr += n_size)
      *pr = (*prBDD) * *prBUTD++;

    /* now advance pointer of diagonal part from BD{k}.D */
    prBDD++;
  } /* end for j */

  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  /* copy lower triangular part from BL{k}.D column by column */
  for (j = 0; j < n_size; j++) {
    /* advance BDinv{k}.D, BL{k}.D to their strict lower triangular part of
     * column j */
    prBDinvD += j + 1;
    prBLD += j + 1;
    /* copy strict lower triangular part from BL{k}.D */
    for (i = j + 1; i < n_size; i++)
      *prBDinvD++ = *prBLD++;
  } /* end for j */
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: final triangular factorization copied\n");
  fflush(stdout);
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  mexPrintf("        ");
  for (j = 0; j < n_size; j++)
    mexPrintf("%8d", ipiv[j]);
  mexPrintf("\n");
  fflush(stdout);
  mexPrintf("        ");
  for (j = 0; j < n_size; j++)
    mexPrintf("%8d", (integer)prBLinvJ[j]);
  mexPrintf("\n");
  fflush(stdout);
  for (i = 0; i < n_size; i++) {
    mexPrintf("%8d", (integer)prBLinvJ[i]);
    for (j = 0; j < n_size; j++)
      mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
    mexPrintf("\n");
    fflush(stdout);
  }
#endif

  /* use LAPACK's dgetri_ for matrix inversion given the LDU decompositon */
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  j = 0;
  dgetri_(&n_size, prBDinvD, &n_size, ipiv, work, &n, &j);
  if (j < 0) {
    mexPrintf("the %d-th argument had an illegal value\n", -j);
    mexErrMsgTxt("dgetri_ failed\n");
  }
  if (j > 0) {
    mexPrintf("D(%d,%d) = 0; the matrix is singular and its inverse could not "
              "be computed\n",
              j, j);
    mexErrMsgTxt("dgetri_ failed\n");
  }
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: final inverse diagonal block computed\n");
  fflush(stdout);
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  mexPrintf("        ");
  for (j = 0; j < n_size; j++)
    mexPrintf("%8d", (integer)prBLinvJ[j]);
  mexPrintf("\n");
  fflush(stdout);
  for (i = 0; i < n_size; i++) {
    mexPrintf("%8d", (integer)prBLinvJ[i]);
    for (j = 0; j < n_size; j++)
      mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
    mexPrintf("\n");
    fflush(stdout);
  }
#endif

  /* successively downdate "n" by the size "n_size" of the diagonal block */
  sumn = n - n_size;
  /* extract diagonal entries  */
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  for (j = 0; j < n_size; j++) {
    Dbuff[sumn + j] = *prBDinvD;
    /* advance to the diagonal part of column j+1 */
    prBDinvD += n_size + 1;
  } /* end for j */
#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: inverse diagonal entries extracted\n");
  fflush(stdout);
  for (j = 0; j < n_size; j++)
    mexPrintf("%8.1le", Dbuff[sumn + j]);
  mexPrintf("\n");
  fflush(stdout);
  mexPrintf("DGNLselbinv: final inverse diagonal block computed\n");
  fflush(stdout);
  prBDinvD = (double *)mxGetPr(BDinv_blockD);
  mexPrintf("        ");
  for (j = 0; j < n_size; j++)
    mexPrintf("%8d", (integer)prBLinvJ[j]);
  mexPrintf("\n");
  fflush(stdout);
  for (i = 0; i < n_size; i++) {
    mexPrintf("%8d", (integer)prBLinvJ[i]);
    for (j = 0; j < n_size; j++)
      mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
    mexPrintf("\n");
    fflush(stdout);
  }
#endif

  /* set each field in BDinv_block structure */
  mxSetFieldByNumber(BDinv_block, (mwIndex)0, 3, BDinv_blockD);

  /* finally set output BLinv{k}, BDinv{k}, BUTinv{k} */
  mxSetCell(BLinv, (mwIndex)k, BLinv_block);
  mxSetCell(BDinv, (mwIndex)k, BDinv_block);
  mxSetCell(BUTinv, (mwIndex)k, BUTinv_block);

  /* advance backwards toward the top */
  k--;

  /* main loop */
  while (k >= 0) {

    /* extract BL{k} */
    BL_block = mxGetCell(BL, k);

    /* 1. BL{k}.J */
    BL_blockJ = mxGetField(BL_block, 0, "J");
    n_size = mxGetN(BL_blockJ) * mxGetM(BL_blockJ);
    prBLJ = (double *)mxGetPr(BL_blockJ);

    /* 2. BL{k}.I */
    BL_blockI = mxGetField(BL_block, 0, "I");
    if (BL_blockI == NULL)
      mexErrMsgTxt("Field BL{k}.I does not exist.");
    ml_size = mxGetN(BL_blockI) * mxGetM(BL_blockI);
#ifdef PRINT_CHECK
    if (mxGetM(BL_blockI) != 1 && mxGetN(BL_blockI) != 1) {
      mexPrintf("!!!BL{%d}.I must be a 1-dim. array!!!\n", k + 1);
      fflush(stdout);
    }
#endif
    prBLI = (double *)mxGetPr(BL_blockI);

    /* 3. BL{k}.L, dense rectangular matrix ass. with I,J */
    BL_blockL = mxGetField(BL_block, 0, "L");
    if (BL_blockL == NULL)
      mexErrMsgTxt("Field BL{k}.L does not exist.");
    /* numerical values of BL{k}.L */
    prBLL = (double *)mxGetPr(BL_blockL);

    /* 4. BL{k}.D, lower unit triangular matrix */
    BL_blockD = mxGetField(BL_block, 0, "D");
    if (BL_blockD == NULL)
      mexErrMsgTxt("Field BL{k}.D does not exist.");
    if (mxGetN(BL_blockD) != n_size || mxGetM(BL_blockD) != n_size ||
        !mxIsNumeric(BL_blockD))
      mexErrMsgTxt(
          "Field BL{k}.D must be square dense matrix of same size as BL{k}.J.");
    /* numerical values of BL{k}.D */
    prBLD = (double *)mxGetPr(BL_blockD);

    /* extract BD{k} */
    BD_block = mxGetCell(BD, k);
    if (!mxIsStruct(BD_block))
      mexErrMsgTxt("Field BD{k} must be a structure.");

    /* extract source BD{k}.D, sparse diagonal matrix */
    BD_blockD = mxGetField(BD_block, 0, "D");
    if (BD_blockD == NULL)
      mexErrMsgTxt("Field BD{k}.D does not exist.");
    if (mxGetN(BD_blockD) != n_size || mxGetM(BD_blockD) != n_size ||
        !mxIsSparse(BD_blockD))
      mexErrMsgTxt("Field BD{k}.D must be square sparse matrix of same size as "
                   "BL{k}.J.");
    /* sparse diagonal representation of BD{k}.D */
    ia = (mwIndex *)mxGetJc(BD_blockD);
    ja = (mwIndex *)mxGetIr(BD_blockD);
    prBDD = (double *)mxGetPr(BD_blockD);

    /* extract BUT{k} */
    BUT_block = mxGetCell(BUT, k);

    /* 1. BUT{k}.J, this MUST be identical to BL{k}.J */
    BUT_blockJ = mxGetField(BUT_block, 0, "J");
    n_size = mxGetN(BUT_blockJ) * mxGetM(BUT_blockJ);
    prBUTJ = (double *)mxGetPr(BUT_blockJ);

    /* 2. BUT{k}.I, this could be completely different from BL{k}.I */
    BUT_blockI = mxGetField(BUT_block, 0, "I");
    if (BUT_blockI == NULL)
      mexErrMsgTxt("Field BUT{k}.I does not exist.");
    mut_size = mxGetN(BUT_blockI) * mxGetM(BUT_blockI);
#ifdef PRINT_CHECK
    if (mxGetM(BUT_blockI) != 1 && mxGetN(BUT_blockI) != 1) {
      mexPrintf("!!!BUT{%d}.I must be a 1-dim. array!!!\n", k + 1);
      fflush(stdout);
    }
#endif
    prBUTI = (double *)mxGetPr(BUT_blockI);

    /* 3. BUT{k}.L, dense rectangular matrix ass. with I,J */
    BUT_blockL = mxGetField(BUT_block, 0, "L");
    if (BUT_blockL == NULL)
      mexErrMsgTxt("Field BUT{k}.L does not exist.");
    /* numerical values of BUT{k}.L */
    prBUTL = (double *)mxGetPr(BUT_blockL);

    /* 4. BUT{k}.D, lower unit triangular matrix */
    BUT_blockD = mxGetField(BUT_block, 0, "D");
    if (BUT_blockD == NULL)
      mexErrMsgTxt("Field BUT{k}.D does not exist.");
    if (mxGetN(BUT_blockD) != n_size || mxGetM(BUT_blockD) != n_size ||
        !mxIsNumeric(BUT_blockD))
      mexErrMsgTxt("Field BUT{k}.D must be square dense matrix of same size as "
                   "BUT{k}.J.");
    /* numerical values of BUT{k}.D */
    prBUTD = (double *)mxGetPr(BUT_blockD);

    /* set up new block column for BLinv{k} with four elements J, I, L, D */
    BLinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

    /* structure element 0:  J */
    /* create BLinv{k}.J */
    BLinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
    /* copy data from BL{k}.J */
    prBLinvJ = (double *)mxGetPr(BLinv_blockJ);
    memcpy(prBLinvJ, prBLJ, (size_t)n_size * sizeof(double));
    /* set each field in BLinv_block structure */
    mxSetFieldByNumber(BLinv_block, (mwIndex)0, 0, BLinv_blockJ);

    /* structure element 1:  I */
    /* create  BLinv{k}.I from BL{k}.I */
    BLinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)ml_size, mxREAL);
    /* copy data from BL{k].I */
    prBLinvI = (double *)mxGetPr(BLinv_blockI);
    memcpy(prBLinvI, prBLI, (size_t)ml_size * sizeof(double));
    /* set each field in BLinv_block structure */
    mxSetFieldByNumber(BLinv_block, (mwIndex)0, 1, BLinv_blockI);

    /* structure element 3:  D, practically not used */
    /* create empty sparse n_size x n_size matrix BLinv{k}.D */
    BLinv_blockD =
        mxCreateSparse((mwSize)n_size, (mwSize)n_size, (mwSize)0, mxREAL);
    /* set each field in BLinv_block structure */
    mxSetFieldByNumber(BLinv_block, (mwIndex)0, 3, BLinv_blockD);

    /* structure element 2:  L */
    /* create BLinv{k}.L */
    BLinv_blockL =
        mxCreateDoubleMatrix((mwSize)ml_size, (mwSize)n_size, mxREAL);
    prBLinvL = (double *)mxGetPr(BLinv_blockL);
    /* init with zeros */
    for (j = 0; j < ml_size * n_size; j++)
      *prBLinvL++ = 0.0;
    prBLinvL = (double *)mxGetPr(BLinv_blockL);
    /* field "L" in BLinv_block structure not yet set!!! */

    /* set up new block column for BUTinv{k} with four elements J, I, L, D */
    BUTinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

    /* structure element 0:  J */
    /* create BUTinv{k}.J */
    BUTinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
    /* copy data from BUT{k}.J */
    prBUTinvJ = (double *)mxGetPr(BUTinv_blockJ);
    memcpy(prBUTinvJ, prBUTJ, (size_t)n_size * sizeof(double));
    /* set each field in BUTinv_block structure */
    mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 0, BUTinv_blockJ);

    /* structure element 1:  I */
    /* create empty BUTinv{k}.I */
    BUTinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)mut_size, mxREAL);
    /* copy data from BUT{k}.I */
    prBUTinvI = (double *)mxGetPr(BUTinv_blockI);
    memcpy(prBUTinvI, prBUTI, (size_t)mut_size * sizeof(double));
    /* set each field in BUTinv_block structure */
    mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 1, BUTinv_blockI);

    /* structure element 3:  D, practically not used */
    /* create empty sparse n_size x n_size matrix BUTinv{k}.D */
    BUTinv_blockD =
        mxCreateSparse((mwSize)n_size, (mwSize)n_size, (mwSize)0, mxREAL);
    /* set each field in BUTinv_block structure */
    mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 3, BUTinv_blockD);

    /* structure element 2:  L */
    /* create BUTinv{k}.L */
    BUTinv_blockL =
        mxCreateDoubleMatrix((mwSize)mut_size, (mwSize)n_size, mxREAL);
    prBUTinvL = (double *)mxGetPr(BUTinv_blockL);
    /* init with zeros */
    for (j = 0; j < mut_size * n_size; j++)
      *prBUTinvL++ = 0.0;
    prBUTinvL = (double *)mxGetPr(BUTinv_blockL);
    /* field "L" in BUTinv_block structure not yet set!!! */

    /* set up new block column for BDinv{k} with four elements J, I, L, D */
    BDinv_block = mxCreateStructMatrix((mwSize)1, (mwSize)1, 4, BLnames);

    /* structure element 0:  J */
    /* create BDinv{k}.J */
    BDinv_blockJ = mxCreateDoubleMatrix((mwSize)1, (mwSize)n_size, mxREAL);
    /* copy data from BL{k}.J */
    prBDinvJ = (double *)mxGetPr(BDinv_blockJ);
    memcpy(prBDinvJ, prBLJ, (size_t)n_size * sizeof(double));
    /* set each field in BDinv_block structure */
    mxSetFieldByNumber(BDinv_block, (mwIndex)0, 0, BDinv_blockJ);

    /* structure element 1:  I, practically not used */
    /* create empty BDinv{k}.I */
    BDinv_blockI = mxCreateDoubleMatrix((mwSize)1, (mwSize)0, mxREAL);
    /* set each field in BDinv_block structure */
    mxSetFieldByNumber(BDinv_block, (mwIndex)0, 1, BDinv_blockI);

    /* structure element 2:  L, practically not used */
    /* create empty BDinv{k}.L */
    BDinv_blockL = mxCreateSparse((mwSize)0, (mwSize)n_size, (mwSize)0, mxREAL);
    /* set each field in BDinv_block structure */
    mxSetFieldByNumber(BDinv_block, (mwIndex)0, 2, BDinv_blockL);

    /* structure element 3:  D */
    /* create dense n_size x n_size matrix BDinv{k}.D */
    BDinv_blockD = mxCreateDoubleMatrix((mwSize)n_size, (mwSize)n_size, mxREAL);
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    /* field "D" in BDinv_block structure not yet set!!! */

    /* --------------------------------------------------------------------------
     */
    /* update part I. update BUTinv{k}.L
       1 (a)
       update BUTinv{k}.L(Ik,:)  -= \sum_i [ BUTinv{i}.L(Ii,Ji)  * BL{k}(l:j,:)
       ]
       1 (b)
       update BUTinv{k}.L(Ikt,:) -= \sum_i [ BDinv{i}.D(Jit,Ji)  * BL{k}(l:j,:)
       ]
       2
       update BUTinv{k}.L(l:j,:) -= \sum_i [ BLinv{i}.L(Ii,Ji)^T * BL{k}(Ik,:) ]
    */
    /* --------------------------------------------------------------------------
     */

    /* 1 (a), 1 (b): scan the indices of BL{k}.I to find out which block columns
       i of
       BUTinv{i}, BDinv{i} are required to update BUTinv{k} */
    l = 0;
    while (l < ml_size) {
      /* associated index I[l] converted to C-style */
      ii = (integer)prBLI[l] - 1;
      i = block[ii];

      /* find out how many indices of I are associated with block column i */
      j = l + 1;
      flag = -1;
      while (flag) {
        if (j >= ml_size) {
          j = ml_size - 1;
          flag = 0;
        } else {
          /* associated index I[j] converted to C-style */
          ii = (integer)prBLI[j] - 1;
          if (block[ii] > i) {
            j--;
            flag = 0;
          } else
            j++;
        } /* end if-else j>=ml_size */
      }   /* end while flag */
          /* now BL{k}.I(l:j) are associated with block column
             BDinv{i}, BUTinv{i} */

      /* extract already computed BUTinv{i}, i>k */
      BUTinv_blocki = mxGetCell(BUTinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BUTinv_blocki == NULL) {
        mexPrintf("!!!BUTinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BUTinv_blocki)) {
        mexPrintf("!!!BUTinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif

      /* BUTinv{i}.J */
      BUTinv_blockJi = mxGetField(BUTinv_blocki, 0, "J");
#ifdef PRINT_CHECK
      if (BUTinv_blockJi == NULL) {
        mexPrintf("!!!BUTinv{%d}.J does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BUTinv_blockJi) != 1 && mxGetN(BUTinv_blockJi) != 1) {
        mexPrintf("!!!BUTinv{%d}.J must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      ni_size = mxGetN(BUTinv_blockJi) * mxGetM(BUTinv_blockJi);
      prBUTinvJi = (double *)mxGetPr(BUTinv_blockJi);

      /* BUTinv{i}.I */
      BUTinv_blockIi = mxGetField(BUTinv_blocki, 0, "I");
#ifdef PRINT_CHECK
      if (BUTinv_blockIi == NULL) {
        mexPrintf("!!!BUTinv{%d}.I does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BUTinv_blockIi) != 1 && mxGetN(BUTinv_blockIi) != 1) {
        mexPrintf("!!!BUTinv{%d}.I must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      mi_size = mxGetN(BUTinv_blockIi) * mxGetM(BUTinv_blockIi);
      prBUTinvIi = (double *)mxGetPr(BUTinv_blockIi);

      /* BUTinv{i}.L */
      BUTinv_blockLi = mxGetField(BUTinv_blocki, 0, "L");
#ifdef PRINT_CHECK
      if (BUTinv_blockLi == NULL) {
        mexPrintf("!!!BUTinv{%d}.L does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBUTinvLi = (double *)mxGetPr(BUTinv_blockLi);

      /* extract already computed BDinv{i}, i>k */
      BDinv_blocki = mxGetCell(BDinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BDinv_blocki == NULL) {
        mexPrintf("!!!BDinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BDinv_blocki)) {
        mexPrintf("!!!BDinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      /* BDinv{i}.D */
      BDinv_blockDi = mxGetField(BDinv_blocki, 0, "D");
#ifdef PRINT_CHECK
      if (BDinv_blockDi == NULL) {
        mexPrintf("!!!BDinv{%d}.D does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBDinvDi = (double *)mxGetPr(BDinv_blockDi);

      /* l:j refers to continuously chosen indices of BL{k}.I(l:j) !!! */
      /* Ji, Ik and Ii may exclude some entries !!! */

      /* check if I(l:j)==I(l):I(j) (contiguous sequence of indices) */
      /* flag for contiguous index set */
      Ji_cont = -1;
/* BUTinv{i}.L(Ii,Ji), BDinv{i}.D(Jit,Ji) will physically start at position
   j_first, where Ji refers to the sequence of positions in BUTinv{i}.L,
   BDinv{i}.D associated with I(l:j)
*/
#ifdef PRINT_INFO
      mexPrintf("BL{%d}.I(%d:%d)\n", k + 1, l + 1, j + 1);
      for (jj = l; jj <= j; jj++)
        mexPrintf("%4d", (integer)prBLI[jj]);
      mexPrintf("\n");
      mexPrintf("BUTinv{%d}.J=%d:%d\n", i + 1, (integer)prBUTinvJi[0],
                (integer)prBUTinvJi[ni_size - 1]);
      fflush(stdout);
#endif
      j_first = ((integer)prBLI[l]) - ((integer)prBUTinvJi[0]);
      for (jj = l; jj <= j; jj++) {
        /* index ii=I[jj] in MATLAB-style 1,...,n */
        ii = (integer)prBLI[jj];
        /* non-contiguous index found, break! */
        if (ii > (integer)prBLI[l] + jj - l) {
          Ji_cont = 0;
          jj = j + 1;
        }
      } /* end for jj */
#ifdef PRINT_INFO
      if (Ji_cont)
        mexPrintf(
            "BL{%d}.I(%d:%d) is a contiguous subsequence of BUTinv{%d}.J\n",
            k + 1, l + 1, j + 1, i + 1);
      else
        mexPrintf("BL{%d}.I(%d:%d) does not refer to a contiguous subsequence "
                  "of BUTinv{%d}.J\n",
                  k + 1, l + 1, j + 1, i + 1);
      fflush(stdout);
#endif

      /* check if the intersection of Ik=BUTinv{k}.I and Ii=BUTinv{i}.I
         consists of contiguous indices */
      Ik_cont = -1;
      Ii_cont = -1;
      p = 0;
      q = 0;
      t = 0;
      k_first = 0;
      i_first = 0;
      while (p < mut_size && q < mi_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBUTinvI[p];
        jj = (integer)prBUTinvIi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BUTinv{k}.I[p]<BUTinv{i}.I[q]
             refers to a gap in the intersection w.r.t. BUTinv{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ik_cont == -1) {
            /* BUTinv{k}.L(Ik,:) will physically start at position
               k_first, where Ik refers to the sequence of positions
               in BUTinv{k}.L associated with the intersection of
               BUTinv{k}.I and BUTinv{i}.I
            */
            k_first = p;
            /* BUTinv{i}.L(Ii,:) will physically start at position
               i_first, where Ii refers to the sequence of positions
               in BUTinv{i}.L associated with the intersection of
               BUTinv{k}.I and BUTinv{i}.I
            */
            i_first = q;
            /* store positions of the next indices to stay contiguous */
            Ik_cont = p + 1;
            Ii_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ik_cont)
              /* store position of the next index to stay contiguous */
              Ik_cont = p + 1;
            else
              Ik_cont = 0;
            if (q == Ii_cont)
              /* store position of the next index to stay contiguous */
              Ii_cont = q + 1;
            else
              Ii_cont = 0;
          }
          p++;
          q++;
          t++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BUTinv{%d}.I\n", k + 1);
      for (p = 0; p < mut_size; p++)
        mexPrintf("%4d", (integer)prBUTinvI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BUTinv{%d}.I\n", i + 1);
      for (q = 0; q < mi_size; q++)
        mexPrintf("%4d", (integer)prBUTinvIi[q]);
      mexPrintf("\n");
      fflush(stdout);
      if (Ik_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUTinv{%d}.I of length %d\n",
                  k + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BUTinv{%d}.I\n",
                  k + 1);
      if (Ii_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUTinv{%d}.I  of length %d\n",
                  i + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BUTinv{%d}.I\n",
                  i + 1);
      fflush(stdout);
#endif

      /* check if the intersection Ikt=BUTinv{k}.I and
         Jit=BUTinv{i}.J=BDinv{i}.J
         refer to contiguous indices */
      Ikt_cont = -1;
      Jit_cont = -1;
      p = 0;
      q = 0;
      tt = 0;
      kt_first = 0;
      jit_first = 0;
      while (p < mut_size && q < ni_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBUTinvI[p];
        jj = (integer)prBUTinvJi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BUTinv{k}.I[p]<BUTinv{i}.J[q]
             refers to a gap in the intersection w.r.t. BUTinv{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ikt_cont == -1) {
            /* BUTinv{k}.L(Ikt,:) will physically start at position
               kt_first, where Ikt refers to the sequence of positions
               in BUTinv{k}.L associated with the intersection of
               BUTinv{k}.I and BUTinv{i}.J
            */
            kt_first = p;
            /* BDinv{i}.D(Jit,:) will physically start at position
               jit_first, where Jit refers to the sequence of positions
               in BDinv{i}.D associated with the intersection of
               BUTinv{k}.I and BUTinv{i}.J
            */
            jit_first = q;
            /* store positions of the next indices to stay contiguous */
            Ikt_cont = p + 1;
            Jit_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ikt_cont)
              /* store position of the next index to stay contiguous */
              Ikt_cont = p + 1;
            else
              Ikt_cont = 0;
            if (q == Jit_cont)
              /* store position of the next index to stay contiguous */
              Jit_cont = q + 1;
            else
              Jit_cont = 0;
          }
          p++;
          q++;
          tt++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BUTinv{%d}.I\n", k + 1);
      for (p = 0; p < mut_size; p++)
        mexPrintf("%4d", (integer)prBUTinvI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BDinv{%d}.J=%d:%d\n", i + 1, (integer)prBUTinvJi[0],
                (integer)prBUTinvJi[ni_size - 1]);
      fflush(stdout);
      if (Ikt_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUTinv{%d}.I of length %d\n",
                  k + 1, tt);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BUTinv{%d}.I\n",
                  k + 1);
      if (Jit_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BDinv{%d}.J  of length %d\n",
                  i + 1, tt);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BDinv{%d}.J\n",
                  i + 1);
      fflush(stdout);
#endif

      /********************************************************************/
      /********************************************************************/
      /***** 1 (a) contribution from the strict lower triangular part *****/
      /* BUTinv{k}.L(Ik,:)  = - BUTinv{i}.L(Ii,Ji) *BL{k}.L(l:j,:)  +
       * BUTinv{k}.L(Ik,:) */

      /* optimal case, all index sets refer to successively stored rows and
         columns. We can easily use Level-3 BLAS
      */
      if (Ii_cont && Ik_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif
        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (t && ii)
          dgemm_("N", "N", &t, &n_size, &ii, &alpha,
                 prBUTinvLi + i_first + mi_size * j_first, &mi_size, prBLL + l,
                 &ml_size, &beta, prBUTinvL + k_first, &mut_size, 1, 1);
#ifdef PRINT_INFO
        mexPrintf("Ik=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < mi_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ii=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < mi_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji=[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ik,:) = - BUTinv{%d}.L(Ii,Ji)  "
                  "*BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ik,:)\n",
                  k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
        r = 0;
        s = 0;
        while (r < mut_size && s < mi_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
            s++;
          else {
            for (jj = 0; jj < n_size; jj++)
              mexPrintf("%8.1le", prBUTinvL[r + mut_size * jj]);
            mexPrintf("\n");
            fflush(stdout);
            r++;
            s++;
          } /* end if-elseif-else */
        }   /* end while r&s */
#endif
      }      /* end if Ii_cont & Ik_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stick with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

/* determine amount of auxiliary memory */
#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Ii_cont)
          mexPrintf("Ii not contiguous\n");
        if (!Ik_cont)
          mexPrintf("Ik not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Ii| x |Ji| buffer rather than
         * BUTinv{i}.L(Ii,Ji) */
        if (!Ii_cont || !Ji_cont)
          copy_cnt += t * (j - l + 1);
        /* level-3 BLAS have to use |Ik| x n_size buffer rather than
         * BUTinv{k}.L(Ik,:) */
        if (!Ik_cont)
          copy_cnt += t * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < t * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && t) {
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict lower triangular part, still "
                    "use level-3 BLAS\n");
          fflush(stdout);
#endif
          /* copy block to buffer if necessary */
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Ii_cont || !Ji_cont) {
            /* copy BUTinv{i}.L(Ii,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row BUTinv{i}.L(q,:)
                   of BUTinv{i}.L(Ii,Ji) associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBUTinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* does column BL{k}.I(r) match some BUTinv{i}.J(s)?
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    *pr3 = *pr2;
                    pr3 += t;
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ik=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ii=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji=[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ii,Ji) cached\n", i + 1);
            fflush(stdout);
            mexPrintf("        ");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", (integer)prBUTinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\n");
            fflush(stdout);
            p = 0;
            q = 0;
            pr = gemm_buff;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */
                mexPrintf("%8d", ii);

                r = l;
                s = 0;
                pr2 = pr;
                while (s < ni_size) {
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    mexPrintf("%8.1le", *pr2);
                    pr2 += t;
                    r++;
                  }
                  s++;
                }
                pr++;
                mexPrintf("\n");
                fflush(stdout);

                p++;
                q++;
              }
            }
#endif

            pr = gemm_buff;
            p = t;
          } else {
            /* pointer to BUTinv{i}.L(Ii,Ji) and LDA */
            pr = prBUTinvLi + i_first + mi_size * j_first;
            p = mi_size;
          } /* end if-else */

          if (!Ik_cont) {
            /* init buffer with zeros */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (q = 0; q < t * n_size; q++)
              *pr2++ = 0.0;
            /* pointer and LDC */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = t;
            /* since we initialized everything with zero, beta is
               almost arbitrary, we indicate this changing beta to 0.0,
               beta=1.0 would also be ok
            */
            alpha = 1.0;
            beta = 0.0;
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached zeros instead of  BUTinv{%d}.L(Ik,:)\n",
                k + 1);
            fflush(stdout);
#endif
          } else {
            /* pointer to BUTinv{k}.L(Ik,:) and LDC */
            pr2 = prBUTinvL + k_first;
            q = mut_size;
            alpha = -1.0;
            beta = 1.0;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          ii = j - l + 1;
          if (t && ii)
            dgemm_("N", "N", &t, &n_size, &ii, &alpha, pr, &p, prBLL + l,
                   &ml_size, &beta, pr2, &q, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          if (Ik_cont)
            mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ik,:) = - BUTinv{%d}.L(Ii,Ji) "
                      " *BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ik,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          else
            mexPrintf("DGNLselbinv: cached                 BUTinv{%d}.L(Ii,Ji) "
                      " *BL{%d}.L(%d:%d,:)\n",
                      i + 1, k + 1, l + 1, j + 1);

          for (r = 0; r < t; r++) {
            for (s = 0; s < n_size; s++)
              mexPrintf("%8.1le", pr2[r + q * s]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
          /* copy buffer back if necessary */
          if (!Ik_cont) {
            /* init buffer with zeros */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy current row of pr2 to BUTinv{k}.L(Ik,:) */
                pr = (double *)mxGetPr(BUTinv_blockL) + p;
                pr3 = pr2;
                for (r = 0; r < n_size; r++, pr += mut_size, pr3 += t)
                  *pr -= *pr3;
                pr2++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ik=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ii=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji=[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ik,:) = - BUTinv{%d}.L(Ii,Ji) "
                      " *BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ik,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);

            p = 0;
            q = 0;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                for (s = 0; s < n_size; s++)
                  mexPrintf("%8.1le", prBUTinvL[p + mut_size * s]);
                mexPrintf("\n");
                fflush(stdout);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#endif

          }           /* if !Ik_cont */
        }             /* end if level3_BLAS */
        else if (t) { /* it might not pay off, therefore we use a simple
                         hand-coded loop */
/* BUTinv{k}.L(Ik,:)  -=  BUTinv{i}.L(Ii,Ji) * BL{k}.L(l:j,:) */
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict lower triangular part, use "
                    "hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < mut_size && q < mi_size) {
            ii = (integer)prBUTinvI[p];
            jj = (integer)prBUTinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BUTinv{k}.I[p]=BUTinv{i}.I[q] match */
              pr = prBUTinvL + p;
              pr3 = prBLL + l;
              /* BUTinv{k}.L(p,:)  -=  BUTinv{i}.L(q,Ji) * BL{k}.L(l:j,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += mut_size, pr3 += ml_size - (j - l + 1)) {
                /* BUTinv{k}.L(p,ii)  -=  BUTinv{i}.L(q,Ji) * BL{k}.L(l:j,ii) */
                pr2 = prBUTinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    /* BUTinv{k}.L(p,ii)  -=  BUTinv{i}.L(q,s) * BL{k}.L(r,ii)
                     */
                    *pr -= (*pr2) * (*pr3++);
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ji=[");
          r = l;
          s = 0;
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ik,:) = - BUTinv{%d}.L(Ii,Ji)  "
                    "*BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ik,:)\n",
                    k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          p = 0;
          q = 0;
          while (p < mut_size && q < mi_size) {
            ii = (integer)prBUTinvI[p];
            jj = (integer)prBUTinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", prBUTinvL[p + mut_size * s]);
              mexPrintf("\n");
              fflush(stdout);
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the strict lower triangular part empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Ii_cont & Ik_cont & Ji_cont */
        /* end contribution from the strict lower triangular part */
        /**********************************************************/
        /**********************************************************/

      /****************************************************************/
      /****************************************************************/
      /**********  1 (b) contribution from the diagonal block *********/
      /* BUTinv{k}.L(Ikt,:) = - BDinv{i}.D(Jit,Ji)  *BL{k}.L(l:j,:) +
       * BUTinv{k}.L(Ikt,:) */

      /* optimal case, all index sets refer to successively stored rows and
         columns. We can easily use Level-3 BLAS
      */
      if (Jit_cont && Ikt_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif

        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (tt && ii)
          dgemm_("N", "N", &tt, &n_size, &ii, &alpha,
                 prBDinvDi + jit_first + ni_size * j_first, &ni_size, prBLL + l,
                 &ml_size, &beta, prBUTinvL + kt_first, &mut_size, 1, 1);
#ifdef PRINT_INFO
        mexPrintf("Ikt=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < ni_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Jit=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < ni_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji =[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ikt,:) = - BDinv{%d}.D(Jit,Ji)  "
                  "*BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ikt,:)\n",
                  k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
        r = 0;
        s = 0;
        while (r < mut_size && s < ni_size) {
          if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
            r++;
          else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
            s++;
          else {
            for (jj = 0; jj < n_size; jj++)
              mexPrintf("%8.1le", prBUTinvL[r + mut_size * jj]);
            mexPrintf("\n");
            fflush(stdout);
            r++;
            s++;
          } /* end if-elseif-else */
        }   /* end while r&s */
#endif
      }      /* end if Jit_cont & Ikt_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stick with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

/* determine amount of auxiliary memory */
#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Jit_cont)
          mexPrintf("Jit not contiguous\n");
        if (!Ikt_cont)
          mexPrintf("Ikt not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Jit| x |Ji| buffer rather than
         * BDinv{i}.D(Jit,Ji) */
        if (!Jit_cont || !Ji_cont)
          copy_cnt += tt * (j - l + 1);
        /* level-3 BLAS have to use |Ikt| x n_size buffer rather than
         * BUTinv{k}.L(Ikt,:) */
        if (!Ikt_cont)
          copy_cnt += tt * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < tt * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && tt) {
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the diagonal block, still use level-3 BLAS\n");
          fflush(stdout);
#endif
          /* copy block to buffer if necessary */
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Jit_cont || !Ji_cont) {
            /* copy BDinv{i}.D(Jit,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row BDinv{i}.D(q,:)
                   of BDinv{i}.D(Jit,Ji) associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBDinvDi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* does column BL{k}.I(r) match some BDinv{i}.J(s)?
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    *pr3 = *pr2;
                    pr3 += tt;
                    r++;
                  }
                  s++;
                  pr2 += ni_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ikt=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Jit=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji =[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BDinv{%d}.D(Jit,Ji) cached\n", i + 1);
            fflush(stdout);
            mexPrintf("        ");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", (integer)prBUTinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\n");
            fflush(stdout);
            p = 0;
            q = 0;
            pr = gemm_buff;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */
                mexPrintf("%8d", ii);

                r = l;
                s = 0;
                pr2 = pr;
                while (s < ni_size) {
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    mexPrintf("%8.1le", *pr2);
                    pr2 += tt;
                    r++;
                  }
                  s++;
                }
                pr++;
                mexPrintf("\n");
                fflush(stdout);

                p++;
                q++;
              }
            }
#endif

            pr = gemm_buff;
            p = tt;
          } else {
            /* pointer to BDinv{i}.D(Jit,Ji) and LDA */
            pr = prBDinvDi + jit_first + ni_size * j_first;
            p = ni_size;
          } /* end if-else */

          if (!Ikt_cont) {
            /* init buffer with zeros */
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (q = 0; q < tt * n_size; q++)
              *pr2++ = 0.0;
            /* pointer and LDC */
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = tt;
            /* since we initialized everything with zero, beta is
               almost arbitrary, we indicate this changing beta to 0.0,
               beta=1.0 would also be ok
            */
            alpha = 1.0;
            beta = 0.0;
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached zeros instead of  BUTinv{%d}.L(Ikt,:)\n",
                k + 1);
            fflush(stdout);
#endif
          } else {
            /* pointer to BUTinv{k}.L(Ikt,:) and LDC */
            pr2 = prBUTinvL + kt_first;
            q = mut_size;
            alpha = -1.0;
            beta = 1.0;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          ii = j - l + 1;
          if (tt && ii)
            dgemm_("N", "N", &tt, &n_size, &ii, &alpha, pr, &p, prBLL + l,
                   &ml_size, &beta, pr2, &q, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ikt=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Jit=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji =[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          if (Ikt_cont)
            mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ikt,:) = - "
                      "BDinv{%d}.D(Jit,Ji)  *BL{%d}.L(%d:%d,:)  + "
                      "BUTinv{%d}.L(Ikt,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          else
            mexPrintf("DGNLselbinv: cached                 BDinv{%d}.D(Jit,Ji) "
                      " *BL{%d}.L(%d:%d,:)\n",
                      i + 1, k + 1, l + 1, j + 1);

          for (r = 0; r < tt; r++) {
            for (s = 0; s < n_size; s++)
              mexPrintf("%8.1le", pr2[r + q * s]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
          /* copy buffer back if necessary */
          if (!Ikt_cont) {
            /* init buffer with zeros */
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy current row of pr2 to BUTinv{k}.L(Ik,:) */
                pr = (double *)mxGetPr(BUTinv_blockL) + p;
                pr3 = pr2;
                for (r = 0; r < n_size; r++, pr += mut_size, pr3 += tt)
                  *pr -= *pr3;
                pr2++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ikt=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Jit=[");
            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
                r++;
              else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji =[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ikt,:) = - "
                      "BDinv{%d}.D(Jit,Ji)  *BL{%d}.L(%d:%d,:)  + "
                      "BUTinv{%d}.L(Ikt,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);

            p = 0;
            q = 0;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTinvI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                for (s = 0; s < n_size; s++)
                  mexPrintf("%8.1le", prBUTinvL[p + mut_size * s]);
                mexPrintf("\n");
                fflush(stdout);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#endif

          }            /* if !Ikt_cont */
        }              /* end if level3_BLAS */
        else if (tt) { /* it might not pay off, therefore we use a simple
                          hand-coded loop */
/* BUTinv{k}.L(Ikt,:)  -=  BDinv{i}.D(Jit,Ji) * BL{k}.L(l:j,:) */
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the diagonal block, use hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < mut_size && q < ni_size) {
            ii = (integer)prBUTinvI[p];
            jj = (integer)prBUTinvJi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BUTinv{k}.I[p]=BDinv{i}.J[q] match */
              pr = prBUTinvL + p;
              pr3 = prBLL + l;
              /* BUTinv{k}.L(p,:)  -=  BDinv{i}.D(q,Ji) * BL{k}.L(l:j,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += mut_size, pr3 += ml_size - (j - l + 1)) {
                /* BUTinv{k}.L(p,ii)  -=  BDinv{i}.D(q,Ji) * BL{k}.L(l:j,ii) */
                pr2 = prBDinvDi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
                    /* BUTinv{k}.L(p,ii)  -=  BDinv{i}.D(q,s) * BL{k}.L(r,ii) */
                    *pr -= (*pr2) * (*pr3++);
                    r++;
                  }
                  s++;
                  pr2 += ni_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ikt=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Jit=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTinvI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTinvI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ji =[");
          r = l;
          s = 0;
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BUTinv{%d}.L(Ikt,:) = - BDinv{%d}.D(Jit,Ji)  "
                    "*BL{%d}.L(%d:%d,:)  + BUTinv{%d}.L(Ikt,:)\n",
                    k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          p = 0;
          q = 0;
          while (p < mut_size && q < ni_size) {
            ii = (integer)prBUTinvI[p];
            jj = (integer)prBUTinvJi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", prBUTinvL[p + mut_size * s]);
              mexPrintf("\n");
              fflush(stdout);
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf("contribution from the diagonal block empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Jit_cont & Ikt_cont & Ji_cont */
      /* end contribution from the diagonal block */
      /**********************************************************/
      /**********************************************************/

      /* advance to the next block column */
      l = j + 1;
    } /* end while l<ml_size */
      /* part I: end 1 (a), 1 (b) */

    /* part I: 2
       update BUTinv{k}.L(l:j,:)-= \sum_i [ BLinv{i}.L(Ii,Ji)^T * BL{k}(Ik,:) ]
    */
    /* scan the indices of BUTinv{k}.I to find out which block columns of
       BLinv are required to update BUTinv{k} */
    l = 0;
    while (l < mut_size) {
      /* associated index I[l] converted to C-style */
      ii = (integer)prBUTinvI[l] - 1;
      i = block[ii];

      /* find out how many indices of I are associated with block column i */
      j = l + 1;
      flag = -1;
      while (flag) {
        if (j >= mut_size) {
          j = mut_size - 1;
          flag = 0;
        } else {
          /* associated index I[j] converted to C-style */
          ii = (integer)prBUTinvI[j] - 1;
          if (block[ii] > i) {
            j--;
            flag = 0;
          } else
            j++;
        } /* end if-else j>=mut_size */
      }   /* end while flag */
      /* now BUTinv{k}.I(l:j) are associated with block column BLinv{i} */

      /* extract already computed BLinv{i}, i>k */
      BLinv_blocki = mxGetCell(BLinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BLinv_blocki == NULL) {
        mexPrintf("!!!BLinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BLinv_blocki)) {
        mexPrintf("!!!BLinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif

      /* BLinv{i}.J */
      BLinv_blockJi = mxGetField(BLinv_blocki, 0, "J");
#ifdef PRINT_CHECK
      if (BLinv_blockJi == NULL) {
        mexPrintf("!!!BLinv{%d}.J does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BLinv_blockJi) != 1 && mxGetN(BLinv_blockJi) != 1) {
        mexPrintf("!!!BLinv{%d}.J must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      ni_size = mxGetN(BLinv_blockJi) * mxGetM(BLinv_blockJi);
      prBLinvJi = (double *)mxGetPr(BLinv_blockJi);

      /* BLinv{i}.I */
      BLinv_blockIi = mxGetField(BLinv_blocki, 0, "I");
#ifdef PRINT_CHECK
      if (BLinv_blockIi == NULL) {
        mexPrintf("!!!BLinv{%d}.I does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BLinv_blockIi) != 1 && mxGetN(BLinv_blockIi) != 1) {
        mexPrintf("!!!BLinv{%d}.I must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      mi_size = mxGetN(BLinv_blockIi) * mxGetM(BLinv_blockIi);
      prBLinvIi = (double *)mxGetPr(BLinv_blockIi);

      /* BLinv{i}.L */
      BLinv_blockLi = mxGetField(BLinv_blocki, 0, "L");
#ifdef PRINT_CHECK
      if (BLinv_blockLi == NULL) {
        mexPrintf("!!!BLinv{%d}.L does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBLinvLi = (double *)mxGetPr(BLinv_blockLi);

      /* l:j refers to continuously chosen indices of BUTinv{k}.I(l:j) !!! */
      /* Ji, Ik and Ii may exclude some entries !!! */

      /* check if I(l:j)==I(l):I(j) (continuous sequence of indices) */
      /* flag for contiguous index set */
      Ji_cont = -1;
/* BLinv{i}.L(Ii,Ji) will physically start at position j_first,
   where Ji refers to the sequence of positions in BLinv{i}.L
   associated with I(l:j)
*/
#ifdef PRINT_INFO
      mexPrintf("BUTinv{%d}.I(%d:%d)\n", k + 1, l + 1, j + 1);
      for (jj = l; jj <= j; jj++)
        mexPrintf("%4d", (integer)prBUTinvI[jj]);
      mexPrintf("\n");
      mexPrintf("BLinv{%d}.J=%d:%d\n", i + 1, (integer)prBLinvJi[0],
                (integer)prBLinvJi[ni_size - 1]);
      fflush(stdout);
#endif
      j_first = ((integer)prBUTinvI[l]) - ((integer)prBLinvJi[0]);
      for (jj = l; jj <= j; jj++) {
        /* index I[jj] in MATLAB-style 1,...,n */
        ii = (integer)prBUTinvI[jj];
        /* non-contiguous index found, break! */
        if (ii > (integer)prBUTinvI[l] + jj - l) {
          Ji_cont = 0;
          jj = j + 1;
        }
      } /* end for jj */
#ifdef PRINT_INFO
      if (Ji_cont)
        mexPrintf(
            "BUTinv{%d}.I(%d:%d) is a contiguous subsequence of BLinv{%d}.J\n",
            k + 1, l + 1, j + 1, i + 1);
      else
        mexPrintf("BUTinv{%d}.I(%d:%d) does not refer to a contiguous "
                  "subsequence of BLinv{%d}.J\n",
                  k + 1, l + 1, j + 1, i + 1);
      fflush(stdout);
#endif

      /* check if the intersection of BL{k}.I and BLinv{i}.I
         consists of contiguous indices */
      Ik_cont = -1;
      Ii_cont = -1;
      p = 0;
      q = 0;
      t = 0;
      k_first = 0;
      i_first = 0;
      while (p < ml_size && q < mi_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBLI[p];
        jj = (integer)prBLinvIi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BL{k}.I[p]<BLinv{i}.I[q] refers
             to a gap in the intersection w.r.t. BL{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ik_cont == -1) {
            /* BL{k}.L(Ik,:) will physically start at position
               k_first, where Ik refers to the sequence of positions
               in BL{k}.L associated with the intersection of
               BL{k}.I and BLinv{i}.I
            */
            k_first = p;
            /* BLinv{i}.L(Ii,:) will physically start at position
               i_first, where Ii refers to the sequence of positions
               in BLinv{i}.L associated with the intersection of
               BL{k}.I and BLinv{i}.I
            */
            i_first = q;
            /* store positions of the next indices to stay contiguous */
            Ik_cont = p + 1;
            Ii_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ik_cont)
              /* store position of the next index to stay contiguous */
              Ik_cont = p + 1;
            else
              Ik_cont = 0;
            if (q == Ii_cont)
              /* store position of the next index to stay contiguous */
              Ii_cont = q + 1;
            else
              Ii_cont = 0;
          }
          p++;
          q++;
          t++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BL{%d}.I\n", k + 1);
      for (p = 0; p < ml_size; p++)
        mexPrintf("%4d", (integer)prBLI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BLinv{%d}.I\n", i + 1);
      for (q = 0; q < mi_size; q++)
        mexPrintf("%4d", (integer)prBLinvIi[q]);
      mexPrintf("\n");
      fflush(stdout);
      if (Ik_cont)
        mexPrintf("intersection leads to a contiguous sequence inside BL{%d}.I "
                  "of length %d\n",
                  k + 1, t);
      else
        mexPrintf(
            "intersection does not yield a contiguous sequence of BL{%d}.I\n",
            k + 1);
      if (Ii_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BLinv{%d}.I  of length %d\n",
                  i + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BLinv{%d}.I\n",
                  i + 1);
      fflush(stdout);
#endif

      /*************************************************************/
      /*************************************************************/
      /*** 2  contribution from the strict upper triangular part ***/
      /* BUTinv{k}.L(l:j,:) = - BLinv{i}.L(Ii,Ji)^T*BL{k}.L(Ik,:)  +
       * BUTinv{k}.L(l:j,:)  */

      /* optimal case, all index sets refer to successively stored rows and
         columns.
         We can easily use Level-3 BLAS
      */
      if (Ii_cont && Ik_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif

        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (ii && t)
          dgemm_("T", "N", &ii, &n_size, &t, &alpha,
                 prBLinvLi + i_first + mi_size * j_first, &mi_size,
                 prBLL + k_first, &ml_size, &beta, prBUTinvL + l, &mut_size, 1,
                 1);
#ifdef PRINT_INFO
        mexPrintf("Ik=[");
        r = 0;
        s = 0;
        while (r < ml_size && s < mi_size) {
          if ((integer)prBLI[r] < (integer)prBLinvIi[s])
            r++;
          else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ii=[");
        r = 0;
        s = 0;
        while (r < ml_size && s < mi_size) {
          if ((integer)prBLI[r] < (integer)prBLinvIi[s])
            r++;
          else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji=[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BUTinv{%d}.L(%d:%d,:) = - BLinv{%d}.L(Ii,Ji).' "
                  "*BL{%d}.L(Ik,:)  + BUTinv{%d}.L(%d:%d,:)\n",
                  k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
        for (jj = l; jj <= j; jj++) {
          for (q = 0; q < n_size; q++)
            mexPrintf("%8.1le", prBUTinvL[jj + mut_size * q]);
          mexPrintf("\n");
          fflush(stdout);
        }
#endif
      }      /* end if Ii_cont & Ik_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stik with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

/* determine amount of auxiliary memory */
#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Ii_cont)
          mexPrintf("Ii not contiguous\n");
        if (!Ik_cont)
          mexPrintf("Ik not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Ii| x |Ji| buffer rather than
         * BLinv{i}.L(Ii,Ji) */
        if (!Ii_cont || !Ji_cont)
          copy_cnt += t * (j - l + 1);
        /* level-3 BLAS have to use |Ik| x n_size buffer rather than
         * BL{k}.L(Ik,:) */
        if (!Ik_cont)
          copy_cnt += t * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < t * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && t) {
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, still "
                    "use level-3 BLAS\n");
          fflush(stdout);
#endif
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Ii_cont || !Ji_cont) {
            /* copy BLinv{i}.L(Ii,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row of BLinv{i}.L(Ii,:)
                   associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBLinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
                    *pr3 = *pr2;
                    pr3 += t;
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("DGNLselbinv: cached copy of BLinv{%d}.L(Ii,Ji)\nIndex "
                      "set Ji:\n",
                      i + 1);
            fflush(stdout);
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
                mexPrintf("%8d", (integer)prBLinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\nIndex set Ii:\n");
            fflush(stdout);
            p = 0;
            q = 0;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                mexPrintf("%8d", ii);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            for (p = 0; p < t; p++) {
              for (q = 0; q < j - l + 1; q++)
                mexPrintf("%8.1le", gemm_buff[p + q * t]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            pr = gemm_buff;
            p = t;
          } else {
            /* pointer to BLinv{i}.L(Ii,Ji) and LDA */
            pr = prBLinvLi + i_first + mi_size * j_first;
            p = mi_size;
          } /* end if-else */

          if (!Ik_cont) {
            /* copy BL{k}.L(Ik,:) to buffer */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;

            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              ii = (integer)prBLI[r];
              jj = (integer)prBLinvIi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else { /* indices match */

                /* copy BL{k}.L(r,:) to buffer */
                pr3 = pr2;
                pr4 = prBLL + r;
                for (ii = 0; ii < n_size; ii++, pr3 += t, pr4 += ml_size)
                  *pr3 = *pr4;
                pr2++;

                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached copy of BL{%d}.L(Ik,:)\nIndex set J:\n",
                k + 1);
            fflush(stdout);
            for (q = 0; q < n_size; q++)
              mexPrintf("%8d", prBLJ[q]);
            mexPrintf("\nIndex set Ik:\n");
            fflush(stdout);
            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              ii = (integer)prBLI[r];
              jj = (integer)prBLinvIi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else {
                mexPrintf("%8d", ii);
                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (r = 0; r < t; r++) {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", pr2[r + s * t]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            /* pointer and LDC */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = t;
          } else {
            /* pointer to BL{k}.L(Ik,:) and LDC */
            pr2 = prBLL + k_first;
            q = ml_size;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          alpha = -1;
          beta = 1.0;
          ii = j - l + 1;
          if (ii && t)
            dgemm_("T", "N", &ii, &n_size, &t, &alpha, pr, &p, pr2, &q, &beta,
                   prBUTinvL + l, &mut_size, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BUTinv{%d}.L(%d:%d,:) = - "
                    "BLinv{%d}.L(Ii,Ji).'*BL{%d}.L(Ik,:)  + "
                    "BUTinv{%d}.L(%d:%d,:)\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (jj = l; jj <= j; jj++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBUTinvL[jj + mut_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif

        }             /* end if level3_BLAS */
        else if (t) { /* it might not pay off, therefore we use a simple
                         hand-coded loop */
/* BUTinv{k}.L(l:j,:) -=  BLinv{i}.L(Ii,Ji)^T * BL{k}.L(Ik,:) */
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, use "
                    "hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < ml_size && q < mi_size) {
            ii = (integer)prBLI[p];
            jj = (integer)prBLinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BL{k}.I[p]=BLinv{i}.I[q] match */
              pr = prBLL + p;
              pr3 = prBUTinvL + l;
              /* BUTinv{k}.L(l:j,:) -=  BLinv{i}.L(q,Ji)^T * BL{k}.L(p,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += ml_size, pr3 += mut_size - (j - l + 1)) {
                /* BUTinv{k}.L(l:j,ii) -=  BLinv{i}.L(q,Ji)^T * BL{k}.L(p,ii) */
                pr2 = prBLinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
                    /* BUTinv{k}.L(r,ii)  -=  BLinv{i}.L(q,s)^T * BL{k}.L(p,ii)
                     */
                    *pr3++ -= (*pr2) * (*pr);
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBLinvJi[s] == (integer)prBUTinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BUTinv{%d}.L(%d:%d,:) = - "
                    "BLinv{%d}.L(Ii,Ji).'*BL{%d}.L(Ik,:)  + "
                    "BUTinv{%d}.L(%d:%d,:)\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (p = l; p <= j; p++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBUTinvL[p + mut_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the strict upper triangular part empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Ii_cont & Ik_cont & Ji_cont */
      /* end contribution from the strict upper triangular part */
      /**********************************************************/
      /**********************************************************/

      /* advance to the next block column */
      l = j + 1;
    } /* end while l<mut_size */

    /* --------------------------------------------------------------------------
     */
    /* update part II. update BLinv{k}.L
       1
       update BLinv{k}.L(Ik,: ) -= \sum_i [ BLinv{i}.L(Ii,Ji)    * BUT{k}(l:j,:)
       ]
       2 (a)
       update BLinv{k}.L(l:j,:) -= \sum_i [ BUTinv{i}.L(Ii,Ji)^T * BUT{k}(Ik,:)
       ]
       2 (b)
       update BLinv{k}.L(l:j,:) -= \sum_i [ BDinv{i}.D(Jit,Ji)^T * BUT{k}(Ikt,:)
       ]
    */
    /* --------------------------------------------------------------------------
     */

    /* 1: scan the indices of BUT{k}.I to find out which block columns i of
       BLinv{i} are required to update BLinv{k} */
    l = 0;
    while (l < mut_size) {
      /* associated index I[l] converted to C-style */
      ii = (integer)prBUTI[l] - 1;
      i = block[ii];

      /* find out how many indices of I are associated with block column i */
      j = l + 1;
      flag = -1;
      while (flag) {
        if (j >= mut_size) {
          j = mut_size - 1;
          flag = 0;
        } else {
          /* associated index I[j] converted to C-style */
          ii = (integer)prBUTI[j] - 1;
          if (block[ii] > i) {
            j--;
            flag = 0;
          } else
            j++;
        } /* end if-else j>=mut_size */
      }   /* end while flag */
      /* now BUT{k}.I(l:j) are associated with block column BLinv{i} */

      /* extract already computed BLinv{i}, i>k */
      BLinv_blocki = mxGetCell(BLinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BLinv_blocki == NULL) {
        mexPrintf("!!!BLinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BLinv_blocki)) {
        mexPrintf("!!!BLinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif

      /* BLinv{i}.J */
      BLinv_blockJi = mxGetField(BLinv_blocki, 0, "J");
#ifdef PRINT_CHECK
      if (BLinv_blockJi == NULL) {
        mexPrintf("!!!BLinv{%d}.J does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BLinv_blockJi) != 1 && mxGetN(BLinv_blockJi) != 1) {
        mexPrintf("!!!BLinv{%d}.J must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      ni_size = mxGetN(BLinv_blockJi) * mxGetM(BLinv_blockJi);
      prBLinvJi = (double *)mxGetPr(BLinv_blockJi);

      /* BLinv{i}.I */
      BLinv_blockIi = mxGetField(BLinv_blocki, 0, "I");
#ifdef PRINT_CHECK
      if (BLinv_blockIi == NULL) {
        mexPrintf("!!!BLinv{%d}.I does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BLinv_blockIi) != 1 && mxGetN(BLinv_blockIi) != 1) {
        mexPrintf("!!!BLinv{%d}.I must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      mi_size = mxGetN(BLinv_blockIi) * mxGetM(BLinv_blockIi);
      prBLinvIi = (double *)mxGetPr(BLinv_blockIi);

      /* BLinv{i}.L */
      BLinv_blockLi = mxGetField(BLinv_blocki, 0, "L");
#ifdef PRINT_CHECK
      if (BLinv_blockLi == NULL) {
        mexPrintf("!!!BLinv{%d}.L does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBLinvLi = (double *)mxGetPr(BLinv_blockLi);

      /* l:j refers to continuously chosen indices of BUT{k}.I(l:j) !!! */
      /* Ji, Ik and Ii may exclude some entries !!! */

      /* check if I(l:j)==I(l):I(j) (continuous sequence of indices) */
      /* flag for contiguous index set */
      Ji_cont = -1;
/* BLinv{i}.L(Ii,Ji) will physically start at position j_first,
   where Ji refers to the sequence of positions in BLinv{i}.L
   associated with I(l:j)
*/
#ifdef PRINT_INFO
      mexPrintf("BUT{%d}.I(%d:%d)\n", k + 1, l + 1, j + 1);
      for (jj = l; jj <= j; jj++)
        mexPrintf("%4d", (integer)prBUTI[jj]);
      mexPrintf("\n");
      mexPrintf("BLinv{%d}.J=%d:%d\n", i + 1, (integer)prBLinvJi[0],
                (integer)prBLinvJi[ni_size - 1]);
      fflush(stdout);
#endif
      j_first = ((integer)prBUTI[l]) - ((integer)prBLinvJi[0]);
      for (jj = l; jj <= j; jj++) {
        /* index ii=I[jj] in MATLAB-style 1,...,n */
        ii = (integer)prBUTI[jj];
        /* non-contiguous index found, break! */
        if (ii > (integer)prBUTI[l] + jj - l) {
          Ji_cont = 0;
          jj = j + 1;
        }
      } /* end for jj */
#ifdef PRINT_INFO
      if (Ji_cont)
        mexPrintf(
            "BUT{%d}.I(%d:%d) is a contiguous subsequence of BLinv{%d}.J\n",
            k + 1, l + 1, j + 1, i + 1);
      else
        mexPrintf("BUT{%d}.I(%d:%d) does not refer to a contiguous subsequence "
                  "of BLinv{%d}.J\n",
                  k + 1, l + 1, j + 1, i + 1);
      fflush(stdout);
#endif

      /* check if the intersection of Ik=BLinv{k}.I and Ii=BLinv{i}.I
         consists of contiguous indices */
      Ik_cont = -1;
      Ii_cont = -1;
      p = 0;
      q = 0;
      t = 0;
      k_first = 0;
      i_first = 0;
      while (p < ml_size && q < mi_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBLinvI[p];
        jj = (integer)prBLinvIi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BLinv{k}.I[p]<BLinv{i}.I[q]
             refers to a gap in the intersection w.r.t. BLinv{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ik_cont == -1) {
            /* BLinv{k}.L(Ik,:) will physically start at position
               k_first, where Ik refers to the sequence of positions
               in BLinv{k}.L associated with the intersection of
               BLinv{k}.I and BLinv{i}.I
            */
            k_first = p;
            /* BLinv{i}.L(Ii,:) will physically start at position
               i_first, where Ii refers to the sequence of positions
               in BLinv{i}.L associated with the intersection of
               BLinv{k}.I and BLinv{i}.I
            */
            i_first = q;
            /* store positions of the next indices to stay contiguous */
            Ik_cont = p + 1;
            Ii_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ik_cont)
              /* store position of the next index to stay contiguous */
              Ik_cont = p + 1;
            else
              Ik_cont = 0;
            if (q == Ii_cont)
              /* store position of the next index to stay contiguous */
              Ii_cont = q + 1;
            else
              Ii_cont = 0;
          }
          p++;
          q++;
          t++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BLinv{%d}.I\n", k + 1);
      for (p = 0; p < ml_size; p++)
        mexPrintf("%4d", (integer)prBLinvI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BLinv{%d}.I\n", i + 1);
      for (q = 0; q < mi_size; q++)
        mexPrintf("%4d", (integer)prBLinvIi[q]);
      mexPrintf("\n");
      fflush(stdout);
      if (Ik_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BLinv{%d}.I of length %d\n",
                  k + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BLinv{%d}.I\n",
                  k + 1);
      if (Ii_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BLinv{%d}.I  of length %d\n",
                  i + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BLinv{%d}.I\n",
                  i + 1);
      fflush(stdout);
#endif

      /******************************************************************/
      /*****************************************************************/
      /***** 1  contribution from the strict lower triangular part *****/
      /* BLinv{k}.L(Ik,:)  = - BLinv{i}.L(Ii,Ji) *BUT{k}.L(l:j,:)  +
       * BLinv{k}.L(Ik,:) */
      /* optimal case, all index sets refer to successively stored rows and
         columns. We can easily use Level-3 BLAS
      */
      if (Ii_cont && Ik_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif
        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (t && ii)
          dgemm_("N", "N", &t, &n_size, &ii, &alpha,
                 prBLinvLi + i_first + mi_size * j_first, &mi_size, prBUTL + l,
                 &mut_size, &beta, prBLinvL + k_first, &ml_size, 1, 1);
#ifdef PRINT_INFO
        mexPrintf("Ik=[");
        r = 0;
        s = 0;
        while (r < ml_size && s < mi_size) {
          if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
            r++;
          else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ii=[");
        r = 0;
        s = 0;
        while (r < ml_size && s < mi_size) {
          if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
            r++;
          else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji=[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BLinv{%d}.L(Ik,:) = - BLinv{%d}.L(Ii,Ji)  "
                  "*BUT{%d}.L(%d:%d,:)  + BLinv{%d}.L(Ik,:)\n",
                  k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
        r = 0;
        s = 0;
        while (r < ml_size && s < mi_size) {
          if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
            r++;
          else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
            s++;
          else {
            for (jj = 0; jj < n_size; jj++)
              mexPrintf("%8.1le", prBLinvL[r + ml_size * jj]);
            mexPrintf("\n");
            fflush(stdout);
            r++;
            s++;
          } /* end if-elseif-else */
        }   /* end while r&s */
#endif
      }      /* end if Ii_cont & Ik_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stick with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Ii_cont)
          mexPrintf("Ii not contiguous\n");
        if (!Ik_cont)
          mexPrintf("Ik not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Ii| x |Ji| buffer rather than
         * BLinv{i}.L(Ii,Ji) */
        if (!Ii_cont || !Ji_cont)
          copy_cnt += t * (j - l + 1);
        /* level-3 BLAS have to use |Ik| x n_size buffer rather than
         * BLinv{k}.L(Ik,:) */
        if (!Ik_cont)
          copy_cnt += t * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < t * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && t) {
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict lower triangular part, still "
                    "use level 3 BLAS\n");
          fflush(stdout);
#endif
          /* copy block to buffer if necessary */
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Ii_cont || !Ji_cont) {
            /* copy BLinv{i}.L(Ii,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLinvI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row BLinv{i}.L(q,:)
                   of BLinv{i}.L(Ii,Ji) associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBLinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* does column BUT{k}.I(r) match some BLinv{i}.J(s)?
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                    *pr3 = *pr2;
                    pr3 += t;
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ik=[");
            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
                r++;
              else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ii=[");
            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
                r++;
              else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji=[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BLinv{%d}.L(Ii,Ji) cached\n", i + 1);
            fflush(stdout);
            mexPrintf("        ");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                mexPrintf("%8d", (integer)prBLinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\n");
            fflush(stdout);
            p = 0;
            q = 0;
            pr = gemm_buff;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLinvI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */
                mexPrintf("%8d", ii);

                r = l;
                s = 0;
                pr2 = pr;
                while (s < ni_size) {
                  if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                    mexPrintf("%8.1le", *pr2);
                    pr2 += t;
                    r++;
                  }
                  s++;
                }
                pr++;
                mexPrintf("\n");
                fflush(stdout);

                p++;
                q++;
              }
            }
#endif

            pr = gemm_buff;
            p = t;
          } else {
            /* pointer to BLinv{i}.L(Ii,Ji) and LDA */
            pr = prBLinvLi + i_first + mi_size * j_first;
            p = mi_size;
          } /* end if-else */

          if (!Ik_cont) {
            /* init buffer with zeros */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (q = 0; q < t * n_size; q++)
              *pr2++ = 0.0;
            /* pointer and LDC */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = t;
            /* since we initialized everything with zero, beta is
               almost arbitrary, we indicate this changing beta to 0.0,
               beta=1.0 would also be ok
            */
            alpha = 1.0;
            beta = 0.0;
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached zeros instead of  BLinv{%d}.L(Ik,:)\n",
                k + 1);
            fflush(stdout);
#endif
          } else {
            /* pointer to BLinv{k}.L(Ik,:) and LDC */
            pr2 = prBLinvL + k_first;
            q = ml_size;
            alpha = -1.0;
            beta = 1.0;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          ii = j - l + 1;
          if (t && ii)
            dgemm_("N", "N", &t, &n_size, &ii, &alpha, pr, &p, prBUTL + l,
                   &mut_size, &beta, pr2, &q, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          if (Ik_cont)
            mexPrintf("DGNLselbinv: BLinv{%d}.L(Ik,:) = - BLinv{%d}.L(Ii,Ji)  "
                      "*BUT{%d}.L(%d:%d,:)  + BLinv{%d}.L(Ik,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          else
            mexPrintf("DGNLselbinv: cached                BLinv{%d}.L(Ii,Ji)  "
                      "*BUT{%d}.L(%d:%d,:)\n",
                      i + 1, k + 1, l + 1, j + 1);

          for (r = 0; r < t; r++) {
            for (s = 0; s < n_size; s++)
              mexPrintf("%8.1le", pr2[r + q * s]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
          /* copy buffer back if necessary */
          if (!Ik_cont) {
            /* init buffer with zeros */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            p = 0;
            q = 0;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLinvI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy current row of pr2 to BLinv{k}.L(Ik,:) */
                pr = (double *)mxGetPr(BLinv_blockL) + p;
                pr3 = pr2;
                for (r = 0; r < n_size; r++, pr += ml_size, pr3 += t)
                  *pr -= *pr3;
                pr2++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("Ik=[");
            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
                r++;
              else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
                s++;
              else {
                mexPrintf("%8d", r + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ii=[");
            r = 0;
            s = 0;
            while (r < ml_size && s < mi_size) {
              if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
                r++;
              else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
                s++;
              else {
                mexPrintf("%8d", s + 1);
                r++;
                s++;
              }
            }
            mexPrintf("];\n");
            mexPrintf("Ji=[");
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                mexPrintf("%8d", s + 1);
                r++;
              }
              s++;
            }
            mexPrintf("];\n");
            mexPrintf("DGNLselbinv: BLinv{%d}.L(Ik,:) = - BLinv{%d}.L(Ii,Ji)  "
                      "*BUT{%d}.L(%d:%d,:)  + BLinv{%d}.L(Ik,:)\n",
                      k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);

            p = 0;
            q = 0;
            while (p < ml_size && q < mi_size) {
              ii = (integer)prBLinvI[p];
              jj = (integer)prBLinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                for (s = 0; s < n_size; s++)
                  mexPrintf("%8.1le", prBLinvL[p + ml_size * s]);
                mexPrintf("\n");
                fflush(stdout);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#endif

          }           /* if !Ik_cont */
        }             /* end if level3_BLAS */
        else if (t) { /* it might not pay off, therefore we use a simple
                         hand-coded loop */
/* BLinv{k}.L(Ik,:)  -=  BLinv{i}.L(Ii,Ji) * BUT{k}.L(l:j,:) */
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict lower triangular part, use "
                    "hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < ml_size && q < mi_size) {
            ii = (integer)prBLinvI[p];
            jj = (integer)prBLinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BLinv{k}.I[p]=BLinv{i}.I[q] match */
              pr = prBLinvL + p;
              pr3 = prBUTL + l;
              /* BLinv{k}.L(p,:)  -=  BLinv{i}.L(q,Ji) * BUT{k}.L(l:j,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += ml_size, pr3 += mut_size - (j - l + 1)) {
                /* BLinv{k}.L(p,ii)  -=  BLinv{i}.L(q,Ji) * BUT{k}.L(l:j,ii) */
                pr2 = prBLinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
                    /* BLinv{k}.L(p,ii)  -=  BLinv{i}.L(q,s) * BUT{k}.L(r,ii) */
                    *pr -= (*pr2) * (*pr3++);
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < ml_size && s < mi_size) {
            if ((integer)prBLinvI[r] < (integer)prBLinvIi[s])
              r++;
            else if ((integer)prBLinvI[r] > (integer)prBLinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ji=[");
          r = l;
          s = 0;
          while (s < ni_size) {
            if ((integer)prBLinvJi[s] == (integer)prBUTI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BLinv{%d}.L(Ik,:) = - BLinv{%d}.L(Ii,Ji)  "
                    "*BUT{%d}.L(%d:%d,:)  + BLinv{%d}.L(Ik,:)\n",
                    k + 1, i + 1, k + 1, l + 1, j + 1, k + 1);
          p = 0;
          q = 0;
          while (p < ml_size && q < mi_size) {
            ii = (integer)prBLinvI[p];
            jj = (integer)prBLinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", prBLinvL[p + ml_size * s]);
              mexPrintf("\n");
              fflush(stdout);
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the strict lower triangular part empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Ii_cont & Ik_cont & Ji_cont */
      /* end contribution from the strict lower triangular part */
      /**********************************************************/
      /**********************************************************/

      /* advance to the next block column */
      l = j + 1;
    } /* end while l<mut_size */
      /* part II: end 1 */

    /* part II:
       2(a)
       update BLinv{k}.L(l:j,:)-= \sum_i [ BUTinv{i}.L(Ii,Ji)^T * BUT{k}(Ik,:) ]
       2(b)
       update BLinv{k}.L(l:j,:)-= \sum_i [ BDinv{i}.D(Jit,Ji)^T * BUT{k}(Ik,:) ]
    */
    /* scan the indices of BLinv{k}.I to find out which block columns of
       BUTinv are required to update BLinv{k} */
    l = 0;
    while (l < ml_size) {
      /* associated index I[l] converted to C-style */
      ii = (integer)prBLinvI[l] - 1;
      i = block[ii];

      /* find out how many indices of I are associated with block column i */
      j = l + 1;
      flag = -1;
      while (flag) {
        if (j >= ml_size) {
          j = ml_size - 1;
          flag = 0;
        } else {
          /* associated index I[j] converted to C-style */
          ii = (integer)prBLinvI[j] - 1;
          if (block[ii] > i) {
            j--;
            flag = 0;
          } else
            j++;
        } /* end if-else j>=ml_size */
      }   /* end while flag */
      /* now BLinv{k}.I(l:j) are associated with block column BUTinv{i} */

      /* extract already computed BUTinv{i}, i>k */
      BUTinv_blocki = mxGetCell(BUTinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BUTinv_blocki == NULL) {
        mexPrintf("!!!BUTinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BUTinv_blocki)) {
        mexPrintf("!!!BUTinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif

      /* BUTinv{i}.J */
      BUTinv_blockJi = mxGetField(BUTinv_blocki, 0, "J");
#ifdef PRINT_CHECK
      if (BUTinv_blockJi == NULL) {
        mexPrintf("!!!BUTinv{%d}.J does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BUTinv_blockJi) != 1 && mxGetN(BUTinv_blockJi) != 1) {
        mexPrintf("!!!BUTinv{%d}.J must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      ni_size = mxGetN(BUTinv_blockJi) * mxGetM(BUTinv_blockJi);
      prBUTinvJi = (double *)mxGetPr(BUTinv_blockJi);

      /* BUTinv{i}.I */
      BUTinv_blockIi = mxGetField(BUTinv_blocki, 0, "I");
#ifdef PRINT_CHECK
      if (BUTinv_blockIi == NULL) {
        mexPrintf("!!!BUTinv{%d}.I does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (mxGetM(BUTinv_blockIi) != 1 && mxGetN(BUTinv_blockIi) != 1) {
        mexPrintf("!!!BUTinv{%d}.I must be a 1-dim. array!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      mi_size = mxGetN(BUTinv_blockIi) * mxGetM(BUTinv_blockIi);
      prBUTinvIi = (double *)mxGetPr(BUTinv_blockIi);

      /* BUTinv{i}.L */
      BUTinv_blockLi = mxGetField(BUTinv_blocki, 0, "L");
#ifdef PRINT_CHECK
      if (BUTinv_blockLi == NULL) {
        mexPrintf("!!!BUTinv{%d}.L does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBUTinvLi = (double *)mxGetPr(BUTinv_blockLi);

      /* extract already computed BDinv{i}, i>k */
      BDinv_blocki = mxGetCell(BDinv, (mwIndex)i);
#ifdef PRINT_CHECK
      if (BDinv_blocki == NULL) {
        mexPrintf("!!!BDinv{%d} does not exist!!!\n", i + 1);
        fflush(stdout);
      } else if (!mxIsStruct(BDinv_blocki)) {
        mexPrintf("!!!BDinv{%d} must be structure!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      /* BDinv{i}.D */
      BDinv_blockDi = mxGetField(BDinv_blocki, 0, "D");
#ifdef PRINT_CHECK
      if (BDinv_blockDi == NULL) {
        mexPrintf("!!!BDinv{%d}.D does not exist!!!\n", i + 1);
        fflush(stdout);
      }
#endif
      prBDinvDi = (double *)mxGetPr(BDinv_blockDi);

      /* l:j refers to continuously chosen indices of BLinv{k}.I(l:j) !!! */
      /* Ji, Ik and Ii may exclude some entries !!! */

      /* check if I(l:j)==I(l):I(j) (continuous sequence of indices) */
      /* flag for contiguous index set */
      Ji_cont = -1;
/* BUTinv{i}(Ii,Ji), BDinv{i}.D(Jit,Ji) will physically start at position
   j_first,
   where Ji refers to the sequence of positions in BDinv{i}.D, BUTinv{i}.L
   associated with I(l:j)
*/
#ifdef PRINT_INFO
      mexPrintf("BLinv{%d}.I(%d:%d)\n", k + 1, l + 1, j + 1);
      for (jj = l; jj <= j; jj++)
        mexPrintf("%4d", (integer)prBLinvI[jj]);
      mexPrintf("\n");
      mexPrintf("BUTinv{%d}.J=%d:%d\n", i + 1, (integer)prBUTinvJi[0],
                (integer)prBUTinvJi[ni_size - 1]);
      fflush(stdout);
#endif
      j_first = ((integer)prBLinvI[l]) - ((integer)prBUTinvJi[0]);
      for (jj = l; jj <= j; jj++) {
        /* index I[jj] in MATLAB-style 1,...,n */
        ii = (integer)prBLinvI[jj];
        /* non-contiguous index found, break! */
        if (ii > (integer)prBLinvI[l] + jj - l) {
          Ji_cont = 0;
          jj = j + 1;
        }
      } /* end for jj */
#ifdef PRINT_INFO
      if (Ji_cont)
        mexPrintf(
            "BLinv{%d}.I(%d:%d) is a contiguous subsequence of BUTinv{%d}.J\n",
            k + 1, l + 1, j + 1, i + 1);
      else
        mexPrintf("BLinv{%d}.I(%d:%d) does not refer to a contiguous "
                  "subsequence of BUTinv{%d}.J\n",
                  k + 1, l + 1, j + 1, i + 1);
      fflush(stdout);
#endif

      /* check if the intersection of BUT{k}.I and BUTinv{i}.I
         consists of contiguous indices */
      Ik_cont = -1;
      Ii_cont = -1;
      p = 0;
      q = 0;
      t = 0;
      k_first = 0;
      i_first = 0;
      while (p < mut_size && q < mi_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBUTI[p];
        jj = (integer)prBUTinvIi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BUT{k}.I[p]<BUTinv{i}.I[q]
             refers
             to a gap in the intersection w.r.t. BUT{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ik_cont == -1) {
            /* BUT{k}.L(Ik,:) will physically start at position
               k_first, where Ik refers to the sequence of positions
               in BUT{k}.L associated with the intersection of
               BUT{k}.I and BUTinv{i}.I
            */
            k_first = p;
            /* BUTinv{i}.L(Ii,:) will physically start at position
               i_first, where Ii refers to the sequence of positions
               in BUTinv{i}.L associated with the intersection of
               BUT{k}.I and BUTinv{i}.I
            */
            i_first = q;
            /* store positions of the next indices to stay contiguous */
            Ik_cont = p + 1;
            Ii_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ik_cont)
              /* store position of the next index to stay contiguous */
              Ik_cont = p + 1;
            else
              Ik_cont = 0;
            if (q == Ii_cont)
              /* store position of the next index to stay contiguous */
              Ii_cont = q + 1;
            else
              Ii_cont = 0;
          }
          p++;
          q++;
          t++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BUT{%d}.I\n", k + 1);
      for (p = 0; p < mut_size; p++)
        mexPrintf("%4d", (integer)prBUTI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BUTinv{%d}.I\n", i + 1);
      for (q = 0; q < mi_size; q++)
        mexPrintf("%4d", (integer)prBUTinvIi[q]);
      mexPrintf("\n");
      fflush(stdout);
      if (Ik_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUT{%d}.I of length %d\n",
                  k + 1, t);
      else
        mexPrintf(
            "intersection does not yield a contiguous sequence of BUT{%d}.I\n",
            k + 1);
      if (Ii_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUTinv{%d}.I  of length %d\n",
                  i + 1, t);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BUTinv{%d}.I\n",
                  i + 1);
      fflush(stdout);
#endif

      /* check if the intersection Ikt=BUT{k}.I and Jit=BUTinv{i}.J=BDinv{i}.J
         refer to contiguous indices */
      Ikt_cont = -1;
      Jit_cont = -1;
      p = 0;
      q = 0;
      tt = 0;
      kt_first = 0;
      jit_first = 0;
      while (p < mut_size && q < ni_size) {
        /* indices in MATLAB-style */
        ii = (integer)prBUTI[p];
        jj = (integer)prBUTinvJi[q];
        if (ii < jj) {
          p++;
          /* If we already have common indices, BUT{k}.I[p]<BUTinv{i}.J[q]
             refers to a gap in the intersection w.r.t. BUT{k}.I
          */
        } else if (ii > jj) {
          q++;
        } else { /* indices match */
          /* store number of the first common index */
          if (Ikt_cont == -1) {
            /* BUT{k}.L(Ikt,:) will physically start at position
               kt_first, where Ikt refers to the sequence of positions
               in BUT{k}.L associated with the intersection of
               BUT{k}.I and BUTinv{i}.J
            */
            kt_first = p;
            /* BDinv{i}.D(Jit,:) will physically start at position
               jit_first, where Jit refers to the sequence of positions
               in BDinv{i}.D associated with the intersection of
               BUT{k}.I and BUTinv{i}.J
            */
            jit_first = q;
            /* store positions of the next indices to stay contiguous */
            Ikt_cont = p + 1;
            Jit_cont = q + 1;
          } else {
            /* there exists at least one common index */
            /* check if the current index position is the
               successor of the previous position */
            if (p == Ikt_cont)
              /* store position of the next index to stay contiguous */
              Ikt_cont = p + 1;
            else
              Ikt_cont = 0;
            if (q == Jit_cont)
              /* store position of the next index to stay contiguous */
              Jit_cont = q + 1;
            else
              Jit_cont = 0;
          }
          p++;
          q++;
          tt++;
        } /* end if-elseif-else */
      }   /* end while p&q */
#ifdef PRINT_INFO
      mexPrintf("BUT{%d}.I\n", k + 1);
      for (p = 0; p < mut_size; p++)
        mexPrintf("%4d", (integer)prBUTI[p]);
      mexPrintf("\n");
      fflush(stdout);
      mexPrintf("BDinv{%d}.J=%d:%d\n", i + 1, (integer)prBUTinvJi[0],
                (integer)prBUTinvJi[ni_size - 1]);
      fflush(stdout);
      if (Ikt_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BUT{%d}.I of length %d\n",
                  k + 1, tt);
      else
        mexPrintf(
            "intersection does not yield a contiguous sequence of BUT{%d}.I\n",
            k + 1);
      if (Jit_cont)
        mexPrintf("intersection leads to a contiguous sequence inside "
                  "BDinv{%d}.J  of length %d\n",
                  i + 1, tt);
      else
        mexPrintf("intersection does not yield a contiguous sequence of "
                  "BDinv{%d}.J\n",
                  i + 1);
      fflush(stdout);
#endif

      /********************************************************************/
      /********************************************************************/
      /***** 2 (a) contribution from the strict upper triangular part *****/
      /* BLinv{k}.L(l:j,:) = - BUTinv{i}.L(Ii,Ji)^T*BUT{k}.L(Ik,:)  +
       * BLinv{k}.L(l:j,:) */

      /* optimal case, all index sets refer to successively stored rows and
         columns.
         We can easily use Level-3 BLAS
      */
      if (Ii_cont && Ik_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif

        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (ii && t)
          dgemm_("T", "N", &ii, &n_size, &t, &alpha,
                 prBUTinvLi + i_first + mi_size * j_first, &mi_size,
                 prBUTL + k_first, &mut_size, &beta, prBLinvL + l, &ml_size, 1,
                 1);
#ifdef PRINT_INFO
        mexPrintf("Ik=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < mi_size) {
          if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
            r++;
          else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ii=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < mi_size) {
          if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
            r++;
          else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji=[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - BUTinv{%d}.L(Ii,Ji).' "
                  "*BUT{%d}.L(Ik,:)  + BLinv{%d}.L(%d:%d,:)\n",
                  k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
        for (jj = l; jj <= j; jj++) {
          for (q = 0; q < n_size; q++)
            mexPrintf("%8.1le", prBLinvL[jj + ml_size * q]);
          mexPrintf("\n");
          fflush(stdout);
        }
#endif

      }      /* end if Ii_cont & Ik_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stik with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

/* determine amount of auxiliary memory */
#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Ii_cont)
          mexPrintf("Ii not contiguous\n");
        if (!Ik_cont)
          mexPrintf("Ik not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Ii| x |Ji| buffer rather than
         * BUTinv{i}.L(Ii,Ji) */
        if (!Ii_cont || !Ji_cont)
          copy_cnt += t * (j - l + 1);
        /* level-3 BLAS have to use |Ik| x n_size buffer rather than
         * BUT{k}.L(Ik,:) */
        if (!Ik_cont)
          copy_cnt += t * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < t * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && t) {
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, still "
                    "use level-3 BLAS\n");
          fflush(stdout);
#endif
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Ii_cont || !Ji_cont) {
            /* copy BUTinv{i}.L(Ii,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row of BUTinv{i}.L(Ii,:)
                   associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBUTinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                    *pr3 = *pr2;
                    pr3 += t;
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("DGNLselbinv: cached copy of BUTinv{%d}.L(Ii,Ji)\nIndex "
                      "set Ji:\n",
                      i + 1);
            fflush(stdout);
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                mexPrintf("%8d", (integer)prBUTinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\nIndex set Ii:\n");
            fflush(stdout);
            p = 0;
            q = 0;
            while (p < mut_size && q < mi_size) {
              ii = (integer)prBUTI[p];
              jj = (integer)prBUTinvIi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                mexPrintf("%8d", ii);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            for (p = 0; p < t; p++) {
              for (q = 0; q < j - l + 1; q++)
                mexPrintf("%8.1le", gemm_buff[p + q * t]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            pr = gemm_buff;
            p = t;
          } else {
            /* pointer to BUTinv{i}.L(Ii,Ji) and LDA */
            pr = prBUTinvLi + i_first + mi_size * j_first;
            p = mi_size;
          } /* end if-else */

          if (!Ik_cont) {
            /* copy BUT{k}.L(Ik,:) to buffer */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;

            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              ii = (integer)prBUTI[r];
              jj = (integer)prBUTinvIi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else { /* indices match */

                /* copy BUT{k}.L(r,:) to buffer */
                pr3 = pr2;
                pr4 = prBUTL + r;
                for (ii = 0; ii < n_size; ii++, pr3 += t, pr4 += mut_size)
                  *pr3 = *pr4;
                pr2++;

                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached copy of BUT{%d}.L(Ik,:)\nIndex set J:\n",
                k + 1);
            fflush(stdout);
            for (q = 0; q < n_size; q++)
              mexPrintf("%8d", prBUTJ[q]);
            mexPrintf("\nIndex set Ik:\n");
            fflush(stdout);
            r = 0;
            s = 0;
            while (r < mut_size && s < mi_size) {
              ii = (integer)prBUTI[r];
              jj = (integer)prBUTinvIi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else {
                mexPrintf("%8d", ii);
                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (r = 0; r < t; r++) {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", pr2[r + s * t]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            /* pointer and LDC */
            if (!Ii_cont || !Ji_cont)
              pr2 = gemm_buff + t * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = t;
          } else {
            /* pointer to BUT{k}.L(Ik,:) and LDC */
            pr2 = prBUTL + k_first;
            q = mut_size;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          alpha = -1;
          beta = 1.0;
          ii = j - l + 1;
          if (ii && t)
            dgemm_("T", "N", &ii, &n_size, &t, &alpha, pr, &p, pr2, &q, &beta,
                   prBLinvL + l, &ml_size, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - "
                    "BUTinv{%d}.L(Ii,Ji).'*BUT{%d}.L(Ik,:)  + "
                    "BLinv{%d}.L(%d:%d,:)\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (jj = l; jj <= j; jj++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBLinvL[jj + ml_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif

        }             /* end if level3_BLAS */
        else if (t) { /* it might not pay off, therefore we use a simple
                         hand-coded loop */
/* BLinv{k}.L(l:j,:) -=  BUTinv{i}.L(Ii,Ji)^T * BUT{k}.L(Ik,:) */
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, use "
                    "hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < mut_size && q < mi_size) {
            ii = (integer)prBUTI[p];
            jj = (integer)prBUTinvIi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BUT{k}.I[p]=BUTinv{i}.I[q] match */
              pr = prBUTL + p;
              pr3 = prBLinvL + l;
              /* BLinv{k}.L(l:j,:) -=  BUTinv{i}.L(q,Ji)^T * BUT{k}.L(p,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += mut_size, pr3 += ml_size - (j - l + 1)) {
                /* BLinv{k}.L(l:j,ii) -=  BUTinv{i}.L(q,Ji)^T * BUT{k}.L(p,ii)
                 */
                pr2 = prBUTinvLi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                    /* BLinv{k}.L(r,ii)  -=  BUTinv{i}.L(q,s)^T * BUT{k}.L(p,ii)
                     */
                    *pr3++ -= (*pr2) * (*pr);
                    r++;
                  }
                  s++;
                  pr2 += mi_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ik=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Ii=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < mi_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvIi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvIi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji=[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - "
                    "BUTinv{%d}.L(Ii,Ji).'*BUT{%d}.L(Ik,:)  + "
                    "BLinv{%d}.L(%d:%d,:)\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (p = l; p <= j; p++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBLinvL[p + ml_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf(
              "contribution from the strict upper triangular part empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Ii_cont & Ik_cont & Ji_cont */
        /* end contribution from the strict upper triangular part */
        /**********************************************************/
        /**********************************************************/

      /*********************************************************/
      /*********************************************************/
      /****** 2 (b)  contribution from the diagonal block ******/
      /* BLinv{k}.L(l:j,:) = - BDinv{i}.D(Jit,Ji)^T*BUT{k}.L(Ikt,:)  +
       * BLinv{k}.L(l:j,:) */

      /* optimal case, all index sets refer to successively stored rows and
         columns.
         We can easily use Level-3 BLAS
      */
      if (Jit_cont && Ikt_cont && Ji_cont) {
#ifdef PRINT_INFO
        mexPrintf("ideal case, use level-3 BLAS directly!\n");
        fflush(stdout);
#endif

        alpha = -1.0;
        beta = 1.0;
        ii = j - l + 1;
        if (ii && tt)
          dgemm_("T", "N", &ii, &n_size, &tt, &alpha,
                 prBDinvDi + jit_first + ni_size * j_first, &ni_size,
                 prBUTL + kt_first, &mut_size, &beta, prBLinvL + l, &ml_size, 1,
                 1);
#ifdef PRINT_INFO
        mexPrintf("Ikt=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < ni_size) {
          if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
            r++;
          else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
            s++;
          else {
            mexPrintf("%8d", r + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Jit=[");
        r = 0;
        s = 0;
        while (r < mut_size && s < ni_size) {
          if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
            r++;
          else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
            s++;
          else {
            mexPrintf("%8d", s + 1);
            r++;
            s++;
          }
        }
        mexPrintf("];\n");
        mexPrintf("Ji =[");
        r = l;
        s = 0;
        while (s < ni_size) {
          if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
            mexPrintf("%8d", s + 1);
            r++;
          }
          s++;
        }
        mexPrintf("];\n");
        mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - BDinv{%d}.D(Jit,Ji).' "
                  "*BUT{%d}.L(Ikt,:)  + BLinv{%d}.L(%d:%d,:)\n",
                  k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
        for (jj = l; jj <= j; jj++) {
          for (q = 0; q < n_size; q++)
            mexPrintf("%8.1le", prBLinvL[jj + ml_size * q]);
          mexPrintf("\n");
          fflush(stdout);
        }
#endif

      }      /* end if Jit_cont & Ikt_cont & Ji_cont */
      else { /* now at least one block is not contiguous. The decision
                whether to stik with level-3 BLAS or not will be made on
                the cost for copying part of the data versus the
                computational cost. This is definitely not optimal
             */

/* determine amount of auxiliary memory */
#ifdef PRINT_INFO
        if (!Ji_cont)
          mexPrintf("Ji not contiguous\n");
        if (!Jit_cont)
          mexPrintf("Jit not contiguous\n");
        if (!Ikt_cont)
          mexPrintf("Ikt not contiguous\n");
        fflush(stdout);
#endif
        copy_cnt = 0;
        /* level-3 BLAS have to use |Jit| x |Ji| buffer rather than
         * BDinv{i}.D(Jit,Ji) */
        if (!Jit_cont || !Ji_cont)
          copy_cnt += tt * (j - l + 1);
        /* level-3 BLAS have to use |Ikt| x n_size buffer rather than
         * BUT{k}.L(Ikt,:) */
        if (!Ikt_cont)
          copy_cnt += tt * n_size;

        /* efficiency decision:  data to copy   versus   matrix-matrix
         * multiplication */
        if (copy_cnt < tt * (j - l + 1) * n_size)
          level3_BLAS = -1;
        else
          level3_BLAS = 0;

        /* it could pay off to copy the data into one or two auxiliary buffers
         */
        if (level3_BLAS && tt) {
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, still "
                    "use level-3 BLAS\n");
          fflush(stdout);
#endif
          size_gemm_buff = MAX(size_gemm_buff, copy_cnt);
          gemm_buff = (doubleprecision *)ReAlloc(
              gemm_buff, (size_t)size_gemm_buff * sizeof(doubleprecision),
              "DGNLselbinv:gemm_buff");
          if (!Jit_cont || !Ji_cont) {
            /* copy BDinv{i}.D(Jit,Ji) to buffer */
            pr = gemm_buff;
            p = 0;
            q = 0;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else { /* indices match */

                /* copy parts of the current row of BDinv{i}.D(Jit,:)
                   associated with Ji to gemm_buff */
                pr3 = pr;
                pr2 = prBDinvDi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                    *pr3 = *pr2;
                    pr3 += tt;
                    r++;
                  }
                  s++;
                  pr2 += ni_size;
                } /* end while s */
                pr++;

                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf("DGNLselbinv: cached copy of BDinv{%d}.D(Jit,Ji)\nIndex "
                      "set Ji:\n",
                      i + 1);
            fflush(stdout);
            r = l;
            s = 0;
            while (s < ni_size) {
              if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                mexPrintf("%8d", (integer)prBUTinvJi[s]);
                r++;
              }
              s++;
            } /* end while s */
            mexPrintf("\nIndex set Jit:\n");
            fflush(stdout);
            p = 0;
            q = 0;
            while (p < mut_size && q < ni_size) {
              ii = (integer)prBUTI[p];
              jj = (integer)prBUTinvJi[q];
              if (ii < jj)
                p++;
              else if (ii > jj)
                q++;
              else {
                mexPrintf("%8d", ii);
                p++;
                q++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            for (p = 0; p < tt; p++) {
              for (q = 0; q < j - l + 1; q++)
                mexPrintf("%8.1le", gemm_buff[p + q * tt]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            pr = gemm_buff;
            p = tt;
          } else {
            /* pointer to BDinv{i}.D(Jit,Ji) and LDA */
            pr = prBDinvDi + jit_first + ni_size * j_first;
            p = ni_size;
          } /* end if-else */

          if (!Ikt_cont) {
            /* copy BUT{k}.L(Ikt,:) to buffer */
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;

            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              ii = (integer)prBUTI[r];
              jj = (integer)prBUTinvJi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else { /* indices match */

                /* copy BUT{k}.L(r,:) to buffer */
                pr3 = pr2;
                pr4 = prBUTL + r;
                for (ii = 0; ii < n_size; ii++, pr3 += tt, pr4 += mut_size)
                  *pr3 = *pr4;
                pr2++;

                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
#ifdef PRINT_INFO
            mexPrintf(
                "DGNLselbinv: cached copy of BUT{%d}.L(Ikt,:)\nIndex set J:\n",
                k + 1);
            fflush(stdout);
            for (q = 0; q < n_size; q++)
              mexPrintf("%8d", prBUTJ[q]);
            mexPrintf("\nIndex set Ikt:\n");
            fflush(stdout);
            r = 0;
            s = 0;
            while (r < mut_size && s < ni_size) {
              ii = (integer)prBUTI[r];
              jj = (integer)prBUTinvJi[s];
              if (ii < jj)
                r++;
              else if (ii > jj)
                s++;
              else {
                mexPrintf("%8d", ii);
                r++;
                s++;
              } /* end if-elseif-else */
            }   /* end while p&q */
            mexPrintf("\n");
            fflush(stdout);
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;
            for (r = 0; r < tt; r++) {
              for (s = 0; s < n_size; s++)
                mexPrintf("%8.1le", pr2[r + s * tt]);
              mexPrintf("\n");
              fflush(stdout);
            }
#endif

            /* pointer and LDC */
            if (!Jit_cont || !Ji_cont)
              pr2 = gemm_buff + tt * (j - l + 1);
            else
              pr2 = gemm_buff;
            q = tt;
          } else {
            /* pointer to BUT{k}.L(Ikt,:) and LDC */
            pr2 = prBUTL + kt_first;
            q = mut_size;
          } /* end if-else */

/* call level-3 BLAS */
#ifdef PRINT_INFO
          mexPrintf("use level-3 BLAS after caching\n");
          fflush(stdout);
#endif
          alpha = -1;
          beta = 1.0;
          ii = j - l + 1;
          if (ii && tt)
            dgemm_("T", "N", &ii, &n_size, &tt, &alpha, pr, &p, pr2, &q, &beta,
                   prBLinvL + l, &ml_size, 1, 1);
#ifdef PRINT_INFO
          mexPrintf("Ikt=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Jit=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji =[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - "
                    "BDinv{%d}.D(Jit,Ji).'*BUT{%d}.L(Ikt,:)  + "
                    "BLinv{%d}.L(%d:%d,:);\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (jj = l; jj <= j; jj++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBLinvL[jj + ml_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif

        }              /* end if level3_BLAS */
        else if (tt) { /* it might not pay off, therefore we use a simple
                          hand-coded loop */
/* BLinv{k}.L(l:j,:) -=  BDinv{i}.D(Jit,Ji)^T * BUT{k}.L(Ikt,:) */
#ifdef PRINT_INFO
          mexPrintf("contribution from the strict upper triangular part, use "
                    "hand-coded loops\n");
          fflush(stdout);
#endif
          p = 0;
          q = 0;
          while (p < mut_size && q < ni_size) {
            ii = (integer)prBUTI[p];
            jj = (integer)prBUTinvJi[q];
            if (ii < jj)
              p++;
            else if (ii > jj)
              q++;
            else { /* row indices BUT{k}.I[p]=BUTinv{i}.J[q](=BDinv{i}.J[q])
                      match */
              pr = prBUTL + p;
              pr3 = prBLinvL + l;
              /* BLinv{k}.L(l:j,:) -=  BDinv{i}.D(q,Ji)^T * BUT{k}.L(p,:) */
              for (ii = 0; ii < n_size;
                   ii++, pr += mut_size, pr3 += ml_size - (j - l + 1)) {
                /* BLinv{k}.L(l:j,ii) -=  BDinv{i}.D(q,Ji)^T * BUT{k}.L(p,ii) */
                pr2 = prBDinvDi + q;
                r = l;
                s = 0;
                while (s < ni_size) {
                  /* column Ji[s] occurs within I(l:j).
                     Recall that I(l:j) is a subset of Ji
                  */
                  if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
                    /* BLinv{k}.L(r,ii)  -=  BDinv{i}.D(q,s)^T * BUT{k}.L(p,ii)
                     */
                    *pr3++ -= (*pr2) * (*pr);
                    r++;
                  }
                  s++;
                  pr2 += ni_size;
                } /* end while s */
              }   /* end for ii */
              p++;
              q++;
            } /* end if-elseif-else */
          }   /* end while p&q */
#ifdef PRINT_INFO
          mexPrintf("Ikt=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", r + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          mexPrintf("Jit=[");
          r = 0;
          s = 0;
          while (r < mut_size && s < ni_size) {
            if ((integer)prBUTI[r] < (integer)prBUTinvJi[s])
              r++;
            else if ((integer)prBUTI[r] > (integer)prBUTinvJi[s])
              s++;
            else {
              mexPrintf("%8d", s + 1);
              r++;
              s++;
            }
          }
          mexPrintf("];\n");
          r = l;
          s = 0;
          mexPrintf("Ji =[");
          while (s < ni_size) {
            if ((integer)prBUTinvJi[s] == (integer)prBLinvI[r]) {
              mexPrintf("%8d", s + 1);
              r++;
            }
            s++;
          }
          mexPrintf("];\n");
          mexPrintf("DGNLselbinv: BLinv{%d}.L(%d:%d,:) = - "
                    "BDinv{%d}.D(Jit,Ji).'*BUT{%d}.L(Ikt,:)  + "
                    "BLinv{%d}.L(%d:%d,:);\n",
                    k + 1, l + 1, j + 1, i + 1, k + 1, k + 1, l + 1, j + 1);
          for (p = l; p <= j; p++) {
            for (q = 0; q < n_size; q++)
              mexPrintf("%8.1le", prBLinvL[p + ml_size * q]);
            mexPrintf("\n");
            fflush(stdout);
          }
#endif
        } /* end if-else level3_BLAS */
        else {
#ifdef PRINT_INFO
          mexPrintf("contribution from block diagonal part empty\n");
          fflush(stdout);
#endif
        }
      } /* end if-else Jit_cont & Ikt_cont & Ji_cont */
      /*****   end contribution from diagonal block   *****/
      /****************************************************/
      /****************************************************/

      /* advance to the next block column */
      l = j + 1;
    } /* end while l<ml_size */

#ifdef PRINT_INFO
    mexPrintf("DGNLselbinv: %d-th inverse sub-diagonal blocks computed\n",
              k + 1);
    fflush(stdout);
    mexPrintf("BLinv{%d}.L\n", k + 1);
    mexPrintf("        ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", (integer)prBLinvJ[j]);
    mexPrintf("\n");
    fflush(stdout);
    ml_size = mxGetN(BL_blockI) * mxGetM(BL_blockI);
    for (i = 0; i < ml_size; i++) {
      mexPrintf("%8d", (integer)prBLinvI[i]);
      for (j = 0; j < n_size; j++)
        mexPrintf("%8.1le", prBLinvL[i + j * ml_size]);
      mexPrintf("\n");
      fflush(stdout);
    }
    mexPrintf("BUTinv{%d}.L\n", k + 1);
    mexPrintf("        ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", (integer)prBUTinvJ[j]);
    mexPrintf("\n");
    fflush(stdout);
    mut_size = mxGetN(BUT_blockI) * mxGetM(BUT_blockI);
    for (i = 0; i < mut_size; i++) {
      mexPrintf("%8d", (integer)prBUTinvI[i]);
      for (j = 0; j < n_size; j++)
        mexPrintf("%8.1le", prBUTinvL[i + j * mut_size]);
      mexPrintf("\n");
      fflush(stdout);
    }
#endif

    /* now finally set each field in BLinv_block structure */
    mxSetFieldByNumber(BLinv_block, (mwIndex)0, 2, BLinv_blockL);
    /* now finally set each field in BUTinv_block structure */
    mxSetFieldByNumber(BUTinv_block, (mwIndex)0, 2, BUTinv_blockL);

    /* structure element 3:  BDinv{k}.D */
    /* create dense n_size x n_size BDinv{k}.D */
    BDinv_blockD = mxCreateDoubleMatrix((mwSize)n_size, (mwSize)n_size, mxREAL);
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    /* copy strict upper triangular part from BUT{k}.D column to row + diagonal
     * part from BD{k}.D */
    for (j = 0; j < n_size; j++) {
      /* no pivoting */
      ipiv[j] = j + 1;

      /* advance BDinv{k}.D to its diagonal part */
      pr = prBDinvD + j * n_size + j;
      /* copy diagonal part from BD{k}.D and advance pointer */
      *pr = *prBDD;
      pr += n_size;

      /* advance source BUT{k}.D to its strict lower triangular part of column j
       */
      prBUTD += j + 1;
      /* copy strict lower triangular part from BUT{k}.D, multiplied by diagonal
       * entry */
      for (i = j + 1; i < n_size; i++, pr += n_size)
        *pr = (*prBDD) * *prBUTD++;

      /* now advance pointer of diagonal part from BD{k}.D */
      prBDD++;
    } /* end for j */

    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    /* copy lower triangular part from BL.D column by column */
    for (j = 0; j < n_size; j++) {
      /* advance BDinv{k}.D, BL{k}.D to their strict lower triangular part of
       * column j */
      prBDinvD += j + 1;
      prBLD += j + 1;
      /* copy strict lower triangular part from BL{k}.D */
      for (i = j + 1; i < n_size; i++)
        *prBDinvD++ = *prBLD++;
    } /* end for j */
#ifdef PRINT_INFO
    mexPrintf("DGNLselbinv: final lower triangular part copied\n");
    fflush(stdout);
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    mexPrintf("perm:   ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", ipiv[j]);
    mexPrintf("\n");
    fflush(stdout);
    mexPrintf("        ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", (integer)prBLinvJ[j]);
    mexPrintf("\n");
    fflush(stdout);
    for (i = 0; i < n_size; i++) {
      mexPrintf("%8d", (integer)prBLinvJ[i]);
      for (j = 0; j < n_size; j++)
        mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
      mexPrintf("\n");
      fflush(stdout);
    }
#endif

    /* use LAPACK's dgetri_ for matrix inversion given the LDU decompositon */
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    j = 0;
    dgetri_(&n_size, prBDinvD, &n_size, ipiv, work, &n, &j);
    if (j < 0) {
      mexPrintf("the %d-th argument had an illegal value\n", -j);
      mexErrMsgTxt("dgetri_ failed\n");
    }
    if (j > 0) {
      mexPrintf("D(%d,%d) = 0; the matrix is singular and its inverse could "
                "not be computed\n",
                j, j);
      mexErrMsgTxt("dgetri_ failed\n");
    }
#ifdef PRINT_INFO
    mexPrintf("DGNLselbinv: inverse lower triangular part computed\n");
    fflush(stdout);
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    mexPrintf("        ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", (integer)prBLinvJ[j]);
    mexPrintf("\n");
    fflush(stdout);
    for (i = 0; i < n_size; i++) {
      mexPrintf("%8d", (integer)prBLinvJ[i]);
      for (j = 0; j < n_size; j++)
        mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
      mexPrintf("\n");
      fflush(stdout);
    }
#endif

/* BDinv{k}.D = - BUT{k}.L^T  *BUTinv{k}.L + BDinv{k}.D */
/* alternatively we may use instead
   BDinv{k}.D = - BLinv{k}.L^T*BL{k}.L     + BDinv{k}.D */
/* call level-3 BLAS */
#ifdef PRINT_INFO
    mexPrintf("use level-3 BLAS anyway\n");
    mexPrintf(
        "DNGLselbinv: BDinv{%d}.D = -BUT{%d}.L.'*BUTinv{%d}.L + BDinv{%d}.D\n",
        k + 1, k + 1, k + 1, k + 1);
    fflush(stdout);
#endif
    alpha = -1;
    beta = 1.0;
    mut_size = mxGetN(BUT_blockI) * mxGetM(BUT_blockI);
    if (mut_size)
      dgemm_("T", "N", &n_size, &n_size, &mut_size, &alpha, prBUTL, &mut_size,
             prBUTinvL, &mut_size, &beta, prBDinvD, &n_size, 1, 1);

    /* successively downdate "n" by the size "n_size" of the diagonal block */
    sumn -= n_size;
    /* extract diagonal entries  */
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    for (j = 0; j < n_size; j++) {
      Dbuff[sumn + j] = *prBDinvD;
      /* advance to the diagonal part of column j+1 */
      prBDinvD += n_size + 1;
    } /* end for j */
#ifdef PRINT_INFO
    mexPrintf("DGNLselbinv: inverse diagonal entries extracted\n");
    fflush(stdout);
    for (j = 0; j < n_size; j++)
      mexPrintf("%8.1le", Dbuff[sumn + j]);
    mexPrintf("\n");
    fflush(stdout);
    mexPrintf("DGNLselbinv: inverse diagonal block computed\n");
    fflush(stdout);
    prBDinvD = (double *)mxGetPr(BDinv_blockD);
    mexPrintf("        ");
    for (j = 0; j < n_size; j++)
      mexPrintf("%8d", (integer)prBLinvJ[j]);
    mexPrintf("\n");
    fflush(stdout);
    for (i = 0; i < n_size; i++) {
      mexPrintf("%8d", (integer)prBLinvJ[i]);
      for (j = 0; j < n_size; j++)
        mexPrintf("%8.1le", prBDinvD[i + j * n_size]);
      mexPrintf("\n");
      fflush(stdout);
    }
#endif

    /* now finally set each field in BDinv_block structure */
    mxSetFieldByNumber(BDinv_block, (mwIndex)0, 3, BDinv_blockD);

    /* finally set output BLinv{k}, BDinv{k}, BUTinv{k} */
    mxSetCell(BLinv, (mwIndex)k, BLinv_block);
    mxSetCell(BDinv, (mwIndex)k, BDinv_block);
    mxSetCell(BUTinv, (mwIndex)k, BUTinv_block);

    k--;
  } /* end while k>=0 */

  /* Compute D=Deltar*(D(invperm)*Deltal) */
  /* 1. compute inverse permutation */
  pr = (double *)mxGetPr(perm);
  for (i = 0; i < n; i++) {
    j = *pr++;
    ipiv[j - 1] = i;
  } /* end for i */
  /* 2. create memory for output vector D */
  plhs[0] = mxCreateDoubleMatrix((mwSize)n, (mwSize)1, mxREAL);
  pr = mxGetPr(plhs[0]);
  /* 3. reorder and rescale */
  pr2 = (double *)mxGetPr(Deltal);
  pr3 = (double *)mxGetPr(Deltar);
  for (i = 0; i < n; i++, pr2++, pr3++) {
    *pr++ = (*pr3) * Dbuff[ipiv[i]] * (*pr2);
  } /* end for i */

  /* finally release auxiliary memory */
  free(ipiv);
  free(work);
  free(Dbuff);
  free(gemm_buff);
  free(block);

#ifdef PRINT_INFO
  mexPrintf("DGNLselbinv: memory released\n");
  fflush(stdout);
#endif

  return;
}
