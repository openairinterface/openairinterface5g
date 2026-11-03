/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// AVX2 + FMA channel pipeline kernels, 8 lanes. Written with simde, so this is also the fallback on
// targets other than x86 and aarch64 (correct, but slow). See channel_pipeline_simd.h.

#include <stddef.h>
#include <simde/x86/avx2.h>
#include <simde/x86/fma.h>
#include "channel_pipeline_simd.h"

#define LANES 8
_Static_assert(LANES == CHANNEL_PIPELINE_LANES, "lanes");

// One c16_t is one 32-bit lane: sign-extend the low half for re, arithmetic-shift for im.
static inline void unpack_c16(simde__m256i v, simde__m256 *re, simde__m256 *im)
{
  *re = simde_mm256_cvtepi32_ps(simde_mm256_srai_epi32(simde_mm256_slli_epi32(v, 16), 16));
  *im = simde_mm256_cvtepi32_ps(simde_mm256_srai_epi32(v, 16));
}

// Clamp in float, round to nearest (MXCSR default), then pack re into the low and im into the high
// half of each lane.
static inline simde__m256i pack_c16(simde__m256 re, simde__m256 im)
{
  const simde__m256 lo = simde_mm256_set1_ps(-32768.0f), hi = simde_mm256_set1_ps(32767.0f);
  simde__m256i r = simde_mm256_cvtps_epi32(simde_mm256_min_ps(simde_mm256_max_ps(re, lo), hi));
  simde__m256i i = simde_mm256_cvtps_epi32(simde_mm256_min_ps(simde_mm256_max_ps(im, lo), hi));
  return simde_mm256_or_si256(simde_mm256_and_si256(r, simde_mm256_set1_epi32(0xffff)), simde_mm256_slli_epi32(i, 16));
}

void channel_pipeline_c16_to_split(float *re, float *im, const c16_t *src, int n)
{
  int i = 0;
  for (; i + LANES <= n; i += LANES) {
    simde__m256 r, m;
    unpack_c16(simde_mm256_loadu_si256((const simde__m256i *)(src + i)), &r, &m);
    simde_mm256_storeu_ps(re + i, r);
    simde_mm256_storeu_ps(im + i, m);
  }
  for (; i < n; i++) {
    re[i] = src[i].r;
    im[i] = src[i].i;
  }
}

void channel_pipeline_split_to_c16(c16_t *dst, const float *re, const float *im, int n)
{
  int i = 0;
  for (; i + LANES <= n; i += LANES)
    simde_mm256_storeu_si256((simde__m256i *)(dst + i), pack_c16(simde_mm256_loadu_ps(re + i), simde_mm256_loadu_ps(im + i)));
  for (; i < n; i++) {
    dst[i].r = channel_pipeline_round_s16(re[i]);
    dst[i].i = channel_pipeline_round_s16(im[i]);
  }
}

void channel_pipeline_direct_mac(float *acc_re,
                                 float *acc_im,
                                 const float *in_re,
                                 const float *in_im,
                                 const float *x_re,
                                 const float *x_im,
                                 const float *g_re,
                                 const float *g_im,
                                 int n,
                                 int L)
{
  for (int i = 0; i < n; i += 4 * LANES) {
    simde__m256 r0, r1, r2, r3, i0, i1, i2, i3;
    if (in_re) {
      const float *sr = in_re + i, *si = in_im + i;
      r0 = simde_mm256_loadu_ps(sr), r1 = simde_mm256_loadu_ps(sr + LANES);
      r2 = simde_mm256_loadu_ps(sr + 2 * LANES), r3 = simde_mm256_loadu_ps(sr + 3 * LANES);
      i0 = simde_mm256_loadu_ps(si), i1 = simde_mm256_loadu_ps(si + LANES);
      i2 = simde_mm256_loadu_ps(si + 2 * LANES), i3 = simde_mm256_loadu_ps(si + 3 * LANES);
    } else {
      r0 = r1 = r2 = r3 = i0 = i1 = i2 = i3 = simde_mm256_setzero_ps();
    }
    const float *xr = x_re + i;
    const float *xi = x_im + i;
    for (int k = 0; k < L; k++) {
      const simde__m256 hr = simde_mm256_set1_ps(g_re[k]);
      const simde__m256 hi = simde_mm256_set1_ps(g_im[k]);
      simde__m256 a, b;
      a = simde_mm256_loadu_ps(xr + k);
      b = simde_mm256_loadu_ps(xi + k);
      r0 = simde_mm256_fnmadd_ps(b, hi, simde_mm256_fmadd_ps(a, hr, r0));
      i0 = simde_mm256_fmadd_ps(b, hr, simde_mm256_fmadd_ps(a, hi, i0));
      a = simde_mm256_loadu_ps(xr + k + LANES);
      b = simde_mm256_loadu_ps(xi + k + LANES);
      r1 = simde_mm256_fnmadd_ps(b, hi, simde_mm256_fmadd_ps(a, hr, r1));
      i1 = simde_mm256_fmadd_ps(b, hr, simde_mm256_fmadd_ps(a, hi, i1));
      a = simde_mm256_loadu_ps(xr + k + 2 * LANES);
      b = simde_mm256_loadu_ps(xi + k + 2 * LANES);
      r2 = simde_mm256_fnmadd_ps(b, hi, simde_mm256_fmadd_ps(a, hr, r2));
      i2 = simde_mm256_fmadd_ps(b, hr, simde_mm256_fmadd_ps(a, hi, i2));
      a = simde_mm256_loadu_ps(xr + k + 3 * LANES);
      b = simde_mm256_loadu_ps(xi + k + 3 * LANES);
      r3 = simde_mm256_fnmadd_ps(b, hi, simde_mm256_fmadd_ps(a, hr, r3));
      i3 = simde_mm256_fmadd_ps(b, hr, simde_mm256_fmadd_ps(a, hi, i3));
    }
    float *ar = acc_re + i;
    float *ai = acc_im + i;
    simde_mm256_storeu_ps(ar, r0);
    simde_mm256_storeu_ps(ar + LANES, r1);
    simde_mm256_storeu_ps(ar + 2 * LANES, r2);
    simde_mm256_storeu_ps(ar + 3 * LANES, r3);
    simde_mm256_storeu_ps(ai, i0);
    simde_mm256_storeu_ps(ai + LANES, i1);
    simde_mm256_storeu_ps(ai + 2 * LANES, i2);
    simde_mm256_storeu_ps(ai + 3 * LANES, i3);
  }
}
