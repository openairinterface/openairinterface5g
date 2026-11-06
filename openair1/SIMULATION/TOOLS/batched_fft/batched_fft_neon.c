/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// NEON batched_fft kernels: 4 transforms per call, one per lane. Same algorithm as
// batched_fft_avx2.c.

#include <stddef.h>
#include <arm_neon.h>
#include "batched_fft.h"

#define LANES 4
_Static_assert(LANES == BATCHED_FFT_BATCH_SIZE, "batch size");

// o = x * w and o = x * conj(w)
static inline void cmul(float32x4_t xr, float32x4_t xi, float32x4_t wr, float32x4_t wi, float32x4_t *o_re, float32x4_t *o_im)
{
  const float32x4_t t = vmulq_f32(xr, wr);
  *o_im = vfmaq_f32(vmulq_f32(xi, wr), xr, wi);
  *o_re = vfmsq_f32(t, xi, wi);
}

static inline void cmul_conj(float32x4_t xr, float32x4_t xi, float32x4_t wr, float32x4_t wi, float32x4_t *o_re, float32x4_t *o_im)
{
  const float32x4_t t = vmulq_f32(xr, wr);
  *o_im = vfmsq_f32(vmulq_f32(xi, wr), xr, wi);
  *o_re = vfmaq_f32(t, xi, wi);
}

static void radix2_pass(float *re, float *im, int n)
{
  for (int s = 0; s < n; s += 2) {
    float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
    const float32x4_t ar = vld1q_f32(pr), ai = vld1q_f32(pi);
    const float32x4_t br = vld1q_f32(pr + LANES), bi = vld1q_f32(pi + LANES);
    vst1q_f32(pr, vaddq_f32(ar, br));
    vst1q_f32(pi, vaddq_f32(ai, bi));
    vst1q_f32(pr + LANES, vsubq_f32(ar, br));
    vst1q_f32(pi + LANES, vsubq_f32(ai, bi));
  }
}

static void dif_pass(float *re, float *im, int n, int len, int N, const float *tw)
{
  const float *tw_im = tw + N / 2;
  const int tstride = N / len;
  const int q = len >> 2;
  const size_t d = (size_t)q * LANES;
  for (int j = 0; j < q; j++) {
    const float32x4_t w1r = vdupq_n_f32(tw[j * tstride]), w1i = vdupq_n_f32(tw_im[j * tstride]);
    const float32x4_t w2r = vdupq_n_f32(tw[(j + q) * tstride]), w2i = vdupq_n_f32(tw_im[(j + q) * tstride]);
    const float32x4_t w3r = vdupq_n_f32(tw[2 * j * tstride]), w3i = vdupq_n_f32(tw_im[2 * j * tstride]);
    for (int s = j; s < n; s += len) {
      float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
      const float32x4_t ar = vld1q_f32(pr), ai = vld1q_f32(pi);
      const float32x4_t br = vld1q_f32(pr + d), bi = vld1q_f32(pi + d);
      const float32x4_t cr = vld1q_f32(pr + 2 * d), ci = vld1q_f32(pi + 2 * d);
      const float32x4_t dr = vld1q_f32(pr + 3 * d), di = vld1q_f32(pi + 3 * d);
      const float32x4_t a1r = vaddq_f32(ar, cr), a1i = vaddq_f32(ai, ci);
      const float32x4_t b1r = vaddq_f32(br, dr), b1i = vaddq_f32(bi, di);
      float32x4_t c1r, c1i, d1r, d1i, tr, ti;
      cmul(vsubq_f32(ar, cr), vsubq_f32(ai, ci), w1r, w1i, &c1r, &c1i);
      cmul(vsubq_f32(br, dr), vsubq_f32(bi, di), w2r, w2i, &d1r, &d1i);
      vst1q_f32(pr, vaddq_f32(a1r, b1r));
      vst1q_f32(pi, vaddq_f32(a1i, b1i));
      vst1q_f32(pr + 2 * d, vaddq_f32(c1r, d1r));
      vst1q_f32(pi + 2 * d, vaddq_f32(c1i, d1i));
      cmul(vsubq_f32(a1r, b1r), vsubq_f32(a1i, b1i), w3r, w3i, &tr, &ti);
      vst1q_f32(pr + d, tr);
      vst1q_f32(pi + d, ti);
      cmul(vsubq_f32(c1r, d1r), vsubq_f32(c1i, d1i), w3r, w3i, &tr, &ti);
      vst1q_f32(pr + 3 * d, tr);
      vst1q_f32(pi + 3 * d, ti);
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
    const float32x4_t w1r = vdupq_n_f32(tw[j * ts1]), w1i = vdupq_n_f32(tw_im[j * ts1]);
    const float32x4_t w2r = vdupq_n_f32(tw[j * ts2]), w2i = vdupq_n_f32(tw_im[j * ts2]);
    const float32x4_t w3r = vdupq_n_f32(tw[(j + h) * ts2]), w3i = vdupq_n_f32(tw_im[(j + h) * ts2]);
    for (int s = j; s < n; s += 2 * len) {
      float *pr = re + (size_t)s * LANES, *pi = im + (size_t)s * LANES;
      const float32x4_t ar = vld1q_f32(pr), ai = vld1q_f32(pi);
      const float32x4_t br = vld1q_f32(pr + d), bi = vld1q_f32(pi + d);
      const float32x4_t cr = vld1q_f32(pr + 2 * d), ci = vld1q_f32(pi + 2 * d);
      const float32x4_t dr = vld1q_f32(pr + 3 * d), di = vld1q_f32(pi + 3 * d);
      float32x4_t tr, ti;
      cmul_conj(br, bi, w1r, w1i, &tr, &ti);
      const float32x4_t a1r = vaddq_f32(ar, tr), a1i = vaddq_f32(ai, ti);
      const float32x4_t b1r = vsubq_f32(ar, tr), b1i = vsubq_f32(ai, ti);
      cmul_conj(dr, di, w1r, w1i, &tr, &ti);
      const float32x4_t c1r = vaddq_f32(cr, tr), c1i = vaddq_f32(ci, ti);
      const float32x4_t d1r = vsubq_f32(cr, tr), d1i = vsubq_f32(ci, ti);
      cmul_conj(c1r, c1i, w2r, w2i, &tr, &ti);
      vst1q_f32(pr, vaddq_f32(a1r, tr));
      vst1q_f32(pi, vaddq_f32(a1i, ti));
      vst1q_f32(pr + 2 * d, vsubq_f32(a1r, tr));
      vst1q_f32(pi + 2 * d, vsubq_f32(a1i, ti));
      cmul_conj(d1r, d1i, w3r, w3i, &tr, &ti);
      vst1q_f32(pr + d, vaddq_f32(b1r, tr));
      vst1q_f32(pi + d, vaddq_f32(b1i, ti));
      vst1q_f32(pr + 3 * d, vsubq_f32(b1r, tr));
      vst1q_f32(pi + 3 * d, vsubq_f32(b1i, ti));
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
