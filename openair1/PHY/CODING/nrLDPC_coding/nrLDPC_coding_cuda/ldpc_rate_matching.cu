/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include "PHY/gpu_compat.h"
#include "PHY/gpu_simd_intrin_compat.h"
#include "openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_segment/nr_rate_matching.h"
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPCdecoder_defs.h"

__device__ __forceinline__ int clamp_i16_to_i8(int x)
{
  // x is int (promoted)
  x = (x < -128) ? -128 : x;
  x = (x > 127) ? 127 : x;
  return x;
}

__device__ __forceinline__ uint32_t packs_4x16_to_4x8(uint32_t a, uint32_t b)
{
  int a0 = (int)(int16_t)(a & 0xFFFFu);
  int a1 = (int)(int16_t)(a >> 16);
  int b0 = (int)(int16_t)(b & 0xFFFFu);
  int b1 = (int)(int16_t)(b >> 16);

  uint32_t o0 = (uint8_t)(int8_t)clamp_i16_to_i8(a0);
  uint32_t o1 = (uint8_t)(int8_t)clamp_i16_to_i8(a1);
  uint32_t o2 = (uint8_t)(int8_t)clamp_i16_to_i8(b0);
  uint32_t o3 = (uint8_t)(int8_t)clamp_i16_to_i8(b1);

  return (o0) | (o1 << 8) | (o2 << 16) | (o3 << 24);
}

__device__ __forceinline__ uint16_t packs_2x16_to_2x8(uint32_t a)
{
  int a0 = (int)(int16_t)(a & 0xFFFFu);
  int a1 = (int)(int16_t)(a >> 16);

  uint16_t o0 = (uint8_t)(int8_t)clamp_i16_to_i8(a0);
  uint16_t o1 = (uint8_t)(int8_t)clamp_i16_to_i8(a1);

  return (o0) | (o1 << 8);
}

__global__ void rm(int Ncb_4,
                   int ind0_4,
                   int E1,
                   int E2,
                   int r_firstE2,
                   int Foffset_4,
                   int F_4,
                   int clear,
                   int seglen_4,
                   int K_4,
                   int Z_4,
                   uint32_t *d,
                   uint32_t *e,
                   uint32_t *llr_buffer)
{
  int ind_4 = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int r = (int)blockIdx.y;
  uint32_t *er = e + ((r < r_firstE2) ? r * (E1 >> 1) : ((r_firstE2 * E1) + (r - r_firstE2) * E2) >> 1);
  uint32_t *dr = d + (2 * r * seglen_4);
  uint32_t *llr_bufferr = llr_buffer + (r * seglen_4);
#ifdef DEBUG_RM
  if (blockIdx.x == 0 && threadIdx.x == 0)
    printf("r %d, seglen %d\n", r, seglen_4);
#endif
  if (ind_4 >= Foffset_4 && ind_4 < Foffset_4 + F_4) {
    llr_bufferr[ind_4 - Foffset_4 + K_4 - F_4] = 0x7f7f7f7f;
#ifdef DEBUG_RM
    if (r == 0)
      printf("writing 0x7f7f7f7f to position %d (ind %d, K %d, Foffset %d, F %d)\n",
             seglen_4 * r + ind_4 - Foffset_4 + K_4 - F_4,
             ind_4,
             K_4,
             Foffset_4,
             F_4);
#endif
    return;
  }

  if (ind_4 >= Ncb_4)
    return;
  if (clear == 1) {
    dr[2 * ind_4] = 0;
    dr[(2 * ind_4) + 1] = 0;
  }

  int E_4;
  if (r < r_firstE2)
    E_4 = (E1 / 4);
  else
    E_4 = (E2 / 4);

  int ind1 = ind0_4, ind2;
  int k = 0;

#ifdef DEBUG_RM
  if (r == 0 && threadIdx.x == 0)
    printf("check 1a: ind %d ind1 %d Foffset %d\n", ind, ind1, Foffset);
#endif
  if (ind1 < Foffset_4) {
    int ind2 = ind1 + min(Foffset_4 - ind1, E_4);

#ifdef DEBUG_RM
    if (r == 0 && threadIdx.x == 0)
      printf("check 1b: ind %d ind1 %d ind2 %d\n", ind, ind1, ind2);
#endif
    if (ind_4 >= ind1 && ind_4 < ind2) {
      dr[2 * ind_4] = gpu_vaddss2(dr[2 * ind_4], er[2 * (ind_4 - ind1)]);
      dr[(2 * ind_4) + 1] = gpu_vaddss2(dr[(2 * ind_4) + 1], er[2 * (ind_4 - ind1) + 1]);
    }
#ifdef DEBUG_RM
    if (r <= 1 && threadIdx.x == 0 && ind_4 >= ind1 && ind_4 < ind2)
      printf("write 1. r %d : ind %d, ind1 %d, ind2 %d,k %d/E %d, pos %d, er %d %d\n",
             r,
             ind_4,
             ind1,
             ind2,
             ind_4 - ind1,
             E_4,
             4 * (ind_4 - ind1),
             (int16_t)(er[2 * (ind_4 - ind1)] & 0xffff),
             (int16_t)(er[2 * (ind_4 - ind1)] >> 16));
#endif
    k = ind2 - ind1;
    ind1 = ind2;
  }

#ifdef DEBUG_RM
  if (r == 0 && threadIdx.x == 0)
    printf("check 2a: ind %d ind1 %d Foffset %d Foffset+F %d\n", ind, ind1, Foffset, Foffset + F);
#endif
  if (ind1 >= Foffset_4 && ind1 < Foffset_4 + F_4)
    ind1 = Foffset_4 + F_4;
  ind2 = ind1 + min(Ncb_4 - ind1, E_4 - k);

#ifdef DEBUG_RM
  if (r == 0 && threadIdx.x == 0)
    printf("check 2b: ind %d ind1 %d ind2 %d\n", ind_4, ind1, ind2);
#endif
  if (ind_4 >= ind1 && ind_4 < ind2) {
    dr[2 * ind_4] = gpu_vaddss2(dr[2 * ind_4], er[2 * (k + (ind_4 - ind1))]);
    dr[(2 * ind_4) + 1] = gpu_vaddss2(dr[(2 * ind_4) + 1], er[2 * (k + (ind_4 - ind1)) + 1]);
  }
#ifdef DEBUG_RM
  if (r == 0 && threadIdx.x == 0 && ind >= ind1 && ind < ind2)
    printf("write 2. ind %d, ind1 %d, ind2 %d, k %d/E %d\n", ind, ind1, ind2, k + ind - ind1, E);
#endif
  k += (ind2 - ind1);

#ifdef DEBUG_RM
  if (r == 0 && threadIdx.x == 0)
    printf("check k %d E %d\n", k, E);
#endif
  while (k < E_4) {
    ind2 = min(Foffset_4, E_4 - k);
    if (ind_4 < ind2) {
      dr[2 * ind_4] = gpu_vaddss2(dr[2 * ind_4], er[2 * (k + ind_4)]);
      dr[(2 * ind_4) + 1] = gpu_vaddss2(dr[(2 * ind_4) + 1], er[2 * (k + ind_4) + 1]);
    }
#ifdef DEBUG_RM
    if (r == 0 && threadIdx.x == 0 && ind < ind2 && ind >= ind1)
      printf("3. ind %d, ind2 %d, k %d/E %d\n", ind, ind2, k + ind, E);
#endif
    k += ind2;

    ind1 = Foffset_4 + F_4;
    ind2 = ind1 + min(Ncb_4 - ind1, E_4 - k);
    if (ind_4 >= ind1 && ind_4 < ind2 && k < E_4) {
      dr[2 * ind_4] = gpu_vaddss2(dr[2 * ind_4], er[2 * (k + ind_4 - ind1)]);
      dr[(2 * ind_4) + 1] = gpu_vaddss2(dr[(2 * ind_4) + 1], er[2 * (k + ind_4 - ind1) + 1]);
    }
#ifdef DEBUG_RM
    if (r == 0 && threadIdx.x == 0 && ind < ind && ind >= ind1)
      printf("4. ind %d, ind1 %d, ind2 %d, k %d/E %d\n", ind, ind1, ind2, k + ind - ind1, E);
#endif
    k += (ind2 - ind1);
  }
  // note the offset here is such that when ind < Foffset = Kprime - 2Z, the output is put in position r*seglen + (2Z ... Kprime)
  // and when ind > Foffset+F, it is in position r*seglen + (Kprime+F = K .. 2Z+(66*Z)=seglen
  llr_bufferr[2 * Z_4 + ind_4] = packs_4x16_to_4x8(dr[(2 * ind_4)], dr[(2 * ind_4) + 1]);
#ifdef DEBUG_RM
  if (r <= 1 && threadIdx.x == 0)
    printf("RM segment %d: writing %x to position %d (ind %d)\n",
           r,
           llr_bufferr[2 * Z_4 + ind_4],
           r * seglen_4 + 2 * Z_4 + ind_4,
           ind_4);
#endif
}

__global__ void rm2(int Ncb_2,
                    int ind0_2,
                    int E1,
                    int E2,
                    int r_firstE2,
                    int Foffset_2,
                    int F_2,
                    int clear,
                    int seglen_2,
                    int K_2,
                    int Z_2,
                    uint32_t *d,
                    uint32_t *e,
                    uint16_t *llr_buffer)
{
  int ind_2 = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  int r = (int)blockIdx.y;
  uint32_t *er = e + ((r < r_firstE2) ? r * (E1 >> 1) : ((r_firstE2 * E1) + (r - r_firstE2) * E2) >> 1);
  uint32_t *dr = d + (r * seglen_2);
  uint16_t *llr_bufferr = llr_buffer + (r * seglen_2);

  if (ind_2 >= Ncb_2)
    return;
#ifdef RM_DEBUG
  if (r == 0)
    printf("Looking for ind %d...%d, Ncb %d\n", ind_2 * 2, 1 + ind_2 * 2, Ncb_2 * 2);
#endif
  if (ind_2 >= Foffset_2 && ind_2 < Foffset_2 + F_2) {
    llr_bufferr[ind_2 - Foffset_2 + K_2 - F_2] = 0x7f7f;
#ifdef RM_DEBUG
    if (r == 0)
      printf("writing 0x7f7f7f7f to position %d (ind %d, K %d, Foffset %d, F %d)\n",
             (seglen_2 * r + ind_2 - Foffset_2 + K_2 - F_2) * 2,
             ind_2 * 2,
             K_2 * 2,
             Foffset_2 * 2,
             F_2 * 2);
#endif
    return;
  }

  if (clear == 1)
    dr[ind_2] = 0;

  int E_2;
  if (r < r_firstE2)
    E_2 = E1 / 2;
  else
    E_2 = E2 / 2;

  int ind1 = ind0_2, ind2;
  int k = 0;

  //     if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check 1a: ind %d ind1 %d Foffset %d\n",ind,ind1,Foffset);
  if (ind1 < Foffset_2) {
    int ind2 = ind1 + min(Foffset_2 - ind1, E_2);

    //       if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check RM1B: ind %d ind1 %d ind2 %d k %d/E
    //       %d\n",4*ind,4*ind1,4*ind2,4*k,4*E);
    if (ind_2 >= ind1 && ind_2 < ind2) {
      dr[ind_2] = gpu_vaddss2(dr[ind_2], er[ind_2 - ind1]);
    }
#ifdef RM_DEBUG
    if (r == 0 /*&& threadIdx.x == 0 && blockIdx.x == 0*/ && ind_2 >= ind1 && ind_2 < ind2)
      printf("RM1A: ind_2 %d, ind1 %d, ind2 %d,k %d/E %d\n", 2 * ind_2, 2 * ind1, 2 * ind2, 2 * (ind_2 - ind1), 2 * E_2);
#endif
    k = ind2 - ind1;
    ind1 = ind2;
  }

  //     if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check 2a: ind %d ind1 %d Foffset %d Foffset+F
  //     %d\n",ind,ind1,Foffset,Foffset+F);
  if (ind1 >= Foffset_2 && ind1 < Foffset_2 + F_2)
    ind1 = Foffset_2 + F_2;
  ind2 = ind1 + min(Ncb_2 - ind1, E_2 - k);

  //     if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check RM2B: ind %d ind1 %d ind2 %d k%d/E
  //     %d\n",4*ind,4*ind1,4*ind2,4*k,4*E);
  if (ind_2 >= ind1 && ind_2 < ind2) {
    dr[ind_2] = gpu_vaddss2(dr[ind_2], er[(k + (ind_2 - ind1))]);
  }
#ifdef RM_DEBUG
  if (r == 0 /*&& threadIdx.x == 0 && blockIdx.x == 0*/ && ind_2 >= ind1 && ind_2 < ind2)
    printf("RM2A: ind %d, ind1 %d, ind2 %d, k %d/E %d\n", 2 * ind_2, 2 * ind1, 2 * ind2, 2 * (k + (ind - ind1)), 2 * E_2);
#endif
  k += (ind2 - ind1);

  //     if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check k %d E %d\n",k,E);
  while (k < E_2) {
    ind2 = min(Foffset_2, E_2 - k);
    //        if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check RM3B: ind %d ind1 %d ind2 %d k %d/E
    //        %d\n",4*ind,0,4*ind2,4*k,4*E);
    if (ind_2 < ind2) {
      dr[ind_2] = gpu_vaddss2(dr[ind_2], er[(k + ind_2)]);
    }
#ifdef RM_DEBUG
    if (r == 0 /*&& threadIdx.x == 0 && blockIdx.x == 0*/ && ind_2 < ind2)
      printf("RM3A: ind %d, ind2 %d, k %d/E %d\n", 2 * ind_2, 2 * ind2, 2 * (k + ind_2), 2 * E_2);
#endif
    k += ind2;

    ind1 = Foffset_2 + F_2;
    ind2 = ind1 + min(Ncb_2 - ind1, E_2 - k);
    //        if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("check RM4B: ind %d ind1 %d ind2 %d k %d/E
    //        %d\n",4*ind,0,4*ind2,4*k,4*E);
    if (ind_2 >= ind1 && ind_2 < ind2 && k < E_2) {
      dr[ind_2] = gpu_vaddss2(dr[ind_2], er[(k + ind_2 - ind1)]);
    }
#ifdef RM_DEBUG
    if (r == 0 /*&& threadIdx.x == 0 && blockIdx.x == 0*/ && ind_2 < ind2 && ind_2 >= ind1)
      printf("RM4A: ind %d, ind1 %d, ind2 %d, k %d/E %d\n", 2 * ind_2, 2 * ind1, 2 * ind2_2, 2 * (k + ind_2 - ind1), 2 * E_2);
#endif
    k += (ind2 - ind1);
  }
  // note the offset here is such that when ind < Foffset = Kprime - 2Z, the output is put in position r*seglen + (2Z ... Kprime)
  // and when ind > Foffset+F, it is in potiion r*seglent + (Kprime+F = K .. 2Z+(66*Z)=seglen
  llr_bufferr[2 * Z_2 + ind_2] = packs_2x16_to_2x8(dr[ind_2]);
  //     if (r==0 && threadIdx.x == 0 && blockIdx.x == 0) printf("writing %x to position %d (ind %d)\n",llr_bufferr[2*Z +
  //     ind_2],r*seglen + 2*Z + ind_2,ind_2);
}

static const uint8_t index_k0[2][4] = {{0, 17, 33, 56}, {0, 13, 25, 43}};
extern "C" int nr_rate_matching_ldpc_rx_cuda(uint32_t Tbslbrm,
                                             uint8_t BG,
                                             uint16_t Z,
                                             int16_t *d,
                                             int16_t *soft_input,
                                             int8_t *llr_buffer,
                                             uint32_t K,
                                             uint8_t C,
                                             uint8_t rvidx,
                                             uint8_t clear,
                                             uint32_t E1,
                                             uint32_t E2,
                                             uint32_t r_firstE2,
                                             uint32_t F,
                                             uint32_t Foffset,
                                             gpuStream_t *s,
                                             int8_t sidx)
{
  if (C == 0 || C > NR_LDPC_MAX_NUM_CB) {
    printf("nr_rate_matching: invalid parameter C %d\n", C);
    exit(-1);
  }

  // Bit selection
  uint32_t N = (BG == 1) ? (66 * Z) : (50 * Z);
  uint32_t Ncb;
  if (Tbslbrm == 0)
    Ncb = N;
  else {
    uint32_t Nref = (3 * Tbslbrm / (2 * C)); // R_LBRM = 2/3
    Ncb = min(N, Nref);
  }

  uint32_t ind = (index_k0[BG - 1][rvidx] * Ncb / N) * Z;

  int nthreads = Z;
#ifdef RM_DEBUG
  printf("\nrm (%d,%d): Ncb %d, ind %d, rvidx %d, E1 %d, E2 %d, Foffset %d, F %d, K %d, Z %d, clear %d\n",
         nblocks.x,
         nthreads,
         Ncb,
         ind,
         rvidx,
         E1,
         E2,
         Foffset,
         F,
         K,
         Z,
         clear);
#endif
  if ((E1 & 3) == 0 && (E2 & 3) == 0) {
    dim3 nblocks(((Ncb >> 2) + nthreads - 1) / nthreads, C);
    rm<<<nblocks, nthreads, 0, s[sidx]>>>(Ncb / 4,
                                          ind / 4,
                                          E1,
                                          E2,
                                          r_firstE2,
                                          Foffset / 4,
                                          F / 4,
                                          clear,
                                          68 * 384 / 4,
                                          K / 4,
                                          Z / 4,
                                          (uint32_t *)d,
                                          (uint32_t *)soft_input,
                                          (uint32_t *)llr_buffer);
  } else {
    dim3 nblocks2(((Ncb >> 1) + nthreads - 1) / nthreads, C);
    rm2<<<nblocks2, nthreads, 0, s[sidx]>>>(Ncb / 2,
                                            ind / 2,
                                            E1,
                                            E2,
                                            r_firstE2,
                                            Foffset / 2,
                                            F / 2,
                                            clear,
                                            68 * 384 / 2,
                                            K / 2,
                                            Z / 2,
                                            (uint32_t *)d,
                                            (uint32_t *)soft_input,
                                            (uint16_t *)llr_buffer);
  }
  gpuError_t err = gpuPeekAtLastError();

  if (err != gpuSuccess) {
    printf(
        "cuda error (nr_rate_matching_ldpc_rx_cuda): %s  Ncb %d, ind %d, rvidx %d, E1 %d, E2 %d, Foffset %d, F %d, K %d, Z %d, "
        "clear %d\n",
        gpuGetErrorString(err),
        Ncb,
        ind,
        rvidx,
        E1,
        E2,
        Foffset,
        F,
        K,
        Z,
        clear);
    exit(-1);
  }
  return (0);
}
