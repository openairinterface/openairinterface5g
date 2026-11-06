/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// AVX2 + FMA batched_fft kernels: 8 transforms per call, one per lane. Written with simde, so this
// is also the fallback on targets other than x86 and aarch64 (correct, but slow).

#include <stddef.h>
#include <simde/x86/avx2.h>
#include <simde/x86/fma.h>
#include "batched_fft.h"

#define LANES 8
_Static_assert(LANES == BATCHED_FFT_BATCH_SIZE, "batch size");

// o = x * w and o = x * conj(w)
static inline void cmul(simde__m256 xr, simde__m256 xi, simde__m256 wr, simde__m256 wi, simde__m256 *o_re, simde__m256 *o_im)
{
  const simde__m256 t = simde_mm256_mul_ps(xr, wr);
  *o_im = simde_mm256_fmadd_ps(xr, wi, simde_mm256_mul_ps(xi, wr));
  *o_re = simde_mm256_fnmadd_ps(xi, wi, t);
}

static inline void cmul_conj(simde__m256 xr, simde__m256 xi, simde__m256 wr, simde__m256 wi, simde__m256 *o_re, simde__m256 *o_im)
{
  const simde__m256 t = simde_mm256_mul_ps(xr, wr);
  *o_im = simde_mm256_fnmadd_ps(xr, wi, simde_mm256_mul_ps(xi, wr));
  *o_re = simde_mm256_fmadd_ps(xi, wi, t);
}

static void radix2_pass(float *re, float *im, int n)
{
  for (int s = 0; s < n; s += 2) {
    float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
    const simde__m256 ar = simde_mm256_loadu_ps(pr), ai = simde_mm256_loadu_ps(pi);
    const simde__m256 br = simde_mm256_loadu_ps(pr + LANES), bi = simde_mm256_loadu_ps(pi + LANES);
    simde_mm256_storeu_ps(pr, simde_mm256_add_ps(ar, br));
    simde_mm256_storeu_ps(pi, simde_mm256_add_ps(ai, bi));
    simde_mm256_storeu_ps(pr + LANES, simde_mm256_sub_ps(ar, br));
    simde_mm256_storeu_ps(pi + LANES, simde_mm256_sub_ps(ai, bi));
  }
}

static void dif_pass(float *re, float *im, int n, int len, int N, const float *tw)
{
  const float *tw_im = tw + N / 2;
  const int tstride = N / len;
  const int q = len >> 2;
  const size_t d = (size_t)q * LANES;
  for (int j = 0; j < q; j++) {
    const simde__m256 w1r = simde_mm256_set1_ps(tw[j * tstride]), w1i = simde_mm256_set1_ps(tw_im[j * tstride]);
    const simde__m256 w2r = simde_mm256_set1_ps(tw[(j + q) * tstride]), w2i = simde_mm256_set1_ps(tw_im[(j + q) * tstride]);
    const simde__m256 w3r = simde_mm256_set1_ps(tw[2 * j * tstride]), w3i = simde_mm256_set1_ps(tw_im[2 * j * tstride]);
    for (int s = j; s < n; s += len) {
      float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
      const simde__m256 ar = simde_mm256_loadu_ps(pr), ai = simde_mm256_loadu_ps(pi);
      const simde__m256 br = simde_mm256_loadu_ps(pr + d), bi = simde_mm256_loadu_ps(pi + d);
      const simde__m256 cr = simde_mm256_loadu_ps(pr + 2 * d), ci = simde_mm256_loadu_ps(pi + 2 * d);
      const simde__m256 dr = simde_mm256_loadu_ps(pr + 3 * d), di = simde_mm256_loadu_ps(pi + 3 * d);
      const simde__m256 a1r = simde_mm256_add_ps(ar, cr), a1i = simde_mm256_add_ps(ai, ci);
      const simde__m256 b1r = simde_mm256_add_ps(br, dr), b1i = simde_mm256_add_ps(bi, di);
      simde__m256 c1r, c1i, d1r, d1i, tr, ti;
      cmul(simde_mm256_sub_ps(ar, cr), simde_mm256_sub_ps(ai, ci), w1r, w1i, &c1r, &c1i);
      cmul(simde_mm256_sub_ps(br, dr), simde_mm256_sub_ps(bi, di), w2r, w2i, &d1r, &d1i);
      simde_mm256_storeu_ps(pr, simde_mm256_add_ps(a1r, b1r));
      simde_mm256_storeu_ps(pi, simde_mm256_add_ps(a1i, b1i));
      simde_mm256_storeu_ps(pr + 2 * d, simde_mm256_add_ps(c1r, d1r));
      simde_mm256_storeu_ps(pi + 2 * d, simde_mm256_add_ps(c1i, d1i));
      cmul(simde_mm256_sub_ps(a1r, b1r), simde_mm256_sub_ps(a1i, b1i), w3r, w3i, &tr, &ti);
      simde_mm256_storeu_ps(pr + d, tr);
      simde_mm256_storeu_ps(pi + d, ti);
      cmul(simde_mm256_sub_ps(c1r, d1r), simde_mm256_sub_ps(c1i, d1i), w3r, w3i, &tr, &ti);
      simde_mm256_storeu_ps(pr + 3 * d, tr);
      simde_mm256_storeu_ps(pi + 3 * d, ti);
    }
  }
}

static void dit_pass(float *re, float *im, int n, int len, int N, const float *tw)
{
  const float *tw_im = tw + N / 2;
  const int h = len >> 1;
  const size_t d = (size_t)h * LANES;
  const int ts1 = N / len, ts2 = N / (2 * len);
  for (int j = 0; j < h; j++) {
    const simde__m256 w1r = simde_mm256_set1_ps(tw[j * ts1]), w1i = simde_mm256_set1_ps(tw_im[j * ts1]);
    const simde__m256 w2r = simde_mm256_set1_ps(tw[j * ts2]), w2i = simde_mm256_set1_ps(tw_im[j * ts2]);
    const simde__m256 w3r = simde_mm256_set1_ps(tw[(j + h) * ts2]), w3i = simde_mm256_set1_ps(tw_im[(j + h) * ts2]);
    for (int s = j; s < n; s += 2 * len) {
      float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
      const simde__m256 ar = simde_mm256_loadu_ps(pr), ai = simde_mm256_loadu_ps(pi);
      const simde__m256 br = simde_mm256_loadu_ps(pr + d), bi = simde_mm256_loadu_ps(pi + d);
      const simde__m256 cr = simde_mm256_loadu_ps(pr + 2 * d), ci = simde_mm256_loadu_ps(pi + 2 * d);
      const simde__m256 dr = simde_mm256_loadu_ps(pr + 3 * d), di = simde_mm256_loadu_ps(pi + 3 * d);
      simde__m256 tr, ti;
      cmul_conj(br, bi, w1r, w1i, &tr, &ti);
      const simde__m256 a1r = simde_mm256_add_ps(ar, tr), a1i = simde_mm256_add_ps(ai, ti);
      const simde__m256 b1r = simde_mm256_sub_ps(ar, tr), b1i = simde_mm256_sub_ps(ai, ti);
      cmul_conj(dr, di, w1r, w1i, &tr, &ti);
      const simde__m256 c1r = simde_mm256_add_ps(cr, tr), c1i = simde_mm256_add_ps(ci, ti);
      const simde__m256 d1r = simde_mm256_sub_ps(cr, tr), d1i = simde_mm256_sub_ps(ci, ti);
      cmul_conj(c1r, c1i, w2r, w2i, &tr, &ti);
      simde_mm256_storeu_ps(pr, simde_mm256_add_ps(a1r, tr));
      simde_mm256_storeu_ps(pi, simde_mm256_add_ps(a1i, ti));
      simde_mm256_storeu_ps(pr + 2 * d, simde_mm256_sub_ps(a1r, tr));
      simde_mm256_storeu_ps(pi + 2 * d, simde_mm256_sub_ps(a1i, ti));
      cmul_conj(d1r, d1i, w3r, w3i, &tr, &ti);
      simde_mm256_storeu_ps(pr + d, simde_mm256_add_ps(b1r, tr));
      simde_mm256_storeu_ps(pi + d, simde_mm256_add_ps(b1i, ti));
      simde_mm256_storeu_ps(pr + 3 * d, simde_mm256_sub_ps(b1r, tr));
      simde_mm256_storeu_ps(pi + 3 * d, simde_mm256_sub_ps(b1i, ti));
    }
  }
}

static void dif_recursive(float *re, float *im, int n, int N, const float *tw)
{
  if ((size_t)n * LANES > BATCHED_FFT_LEAF_FLOATS && n >= 16) {
    dif_pass(re, im, n, n, N, tw);
    const size_t quarter = (size_t)(n / 4) * LANES;
    for (int k = 0; k < 4; k++)
      dif_recursive(re + k * quarter, im + k * quarter, n / 4, N, tw);
    return;
  }
  int len = n;
  for (; len >= 4; len >>= 2)
    dif_pass(re, im, n, len, N, tw);
  if (len == 2)
    radix2_pass(re, im, n);
}

static void dit_recursive(float *re, float *im, int n, int N, const float *tw)
{
  if ((size_t)n * LANES > BATCHED_FFT_LEAF_FLOATS && n >= 16) {
    const size_t quarter = (size_t)(n / 4) * LANES;
    for (int k = 0; k < 4; k++)
      dit_recursive(re + k * quarter, im + k * quarter, n / 4, N, tw);
    dit_pass(re, im, n, n / 2, N, tw);
    return;
  }
  int len = 2;
  if (__builtin_ctz(n) & 1) {
    radix2_pass(re, im, n);
    len = 4;
  }
  for (; 2 * len <= n; len <<= 2)
    dit_pass(re, im, n, len, N, tw);
}

void batched_fft_forward(float *re, float *im, int N, const float *tw)
{
  dif_recursive(re, im, N, N, tw);
}

void batched_fft_inverse(float *re, float *im, int N, const float *tw)
{
  dit_recursive(re, im, N, N, tw);
}
