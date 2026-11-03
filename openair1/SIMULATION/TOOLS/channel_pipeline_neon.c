/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// NEON channel pipeline kernels, 4 lanes. See channel_pipeline_simd.h.

#include <stddef.h>
#include <arm_neon.h>
#include "channel_pipeline_simd.h"

#define LANES 4
_Static_assert(LANES == CHANNEL_PIPELINE_LANES, "lanes");

// vcvtnq_s32_f32 rounds to nearest and saturates, vqmovn saturates: no explicit clamp needed.
static inline int32x4_t pack_c16(float32x4_t re, float32x4_t im)
{
  int16x4x2_t z = vzip_s16(vqmovn_s32(vcvtnq_s32_f32(re)), vqmovn_s32(vcvtnq_s32_f32(im)));
  return vreinterpretq_s32_s16(vcombine_s16(z.val[0], z.val[1]));
}

// One c16_t per 32-bit lane: sign-extend the low half for re, arithmetic-shift for im.
static inline float32x4_t c16_re(int32x4_t v)
{
  return vcvtq_f32_s32(vshrq_n_s32(vshlq_n_s32(v, 16), 16));
}

static inline float32x4_t c16_im(int32x4_t v)
{
  return vcvtq_f32_s32(vshrq_n_s32(v, 16));
}

// 4x4 transpose of 32-bit elements (one c16_t each).
static inline void transpose4(int32x4_t m[4])
{
  int32x4x2_t a = vtrnq_s32(m[0], m[1]);
  int32x4x2_t b = vtrnq_s32(m[2], m[3]);
  m[0] = vcombine_s32(vget_low_s32(a.val[0]), vget_low_s32(b.val[0]));
  m[1] = vcombine_s32(vget_low_s32(a.val[1]), vget_low_s32(b.val[1]));
  m[2] = vcombine_s32(vget_high_s32(a.val[0]), vget_high_s32(b.val[0]));
  m[3] = vcombine_s32(vget_high_s32(a.val[1]), vget_high_s32(b.val[1]));
}

// vld2/vst2 (de)interleave re/im in the load/store itself.
void channel_pipeline_c16_to_split(float *re, float *im, const c16_t *src, int n)
{
  int i = 0;
  for (; i + LANES <= n; i += LANES) {
    int16x4x2_t v = vld2_s16((const int16_t *)(src + i));
    vst1q_f32(re + i, vcvtq_f32_s32(vmovl_s16(v.val[0])));
    vst1q_f32(im + i, vcvtq_f32_s32(vmovl_s16(v.val[1])));
  }
  for (; i < n; i++) {
    re[i] = src[i].r;
    im[i] = src[i].i;
  }
}

void channel_pipeline_split_to_c16(c16_t *dst, const float *re, const float *im, int n)
{
  int i = 0;
  for (; i + LANES <= n; i += LANES) {
    int16x4x2_t o;
    o.val[0] = vqmovn_s32(vcvtnq_s32_f32(vld1q_f32(re + i)));
    o.val[1] = vqmovn_s32(vcvtnq_s32_f32(vld1q_f32(im + i)));
    vst2_s16((int16_t *)(dst + i), o);
  }
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
    float32x4_t r0, r1, r2, r3, i0, i1, i2, i3;
    if (in_re) {
      const float *sr = in_re + i, *si = in_im + i;
      r0 = vld1q_f32(sr), r1 = vld1q_f32(sr + LANES);
      r2 = vld1q_f32(sr + 2 * LANES), r3 = vld1q_f32(sr + 3 * LANES);
      i0 = vld1q_f32(si), i1 = vld1q_f32(si + LANES);
      i2 = vld1q_f32(si + 2 * LANES), i3 = vld1q_f32(si + 3 * LANES);
    } else {
      r0 = r1 = r2 = r3 = i0 = i1 = i2 = i3 = vdupq_n_f32(0.0f);
    }
    const float *xr = x_re + i;
    const float *xi = x_im + i;
    for (int k = 0; k < L; k++) {
      const float32x4_t hr = vdupq_n_f32(g_re[k]);
      const float32x4_t hi = vdupq_n_f32(g_im[k]);
      float32x4_t a, b;
      a = vld1q_f32(xr + k);
      b = vld1q_f32(xi + k);
      r0 = vfmsq_f32(vfmaq_f32(r0, a, hr), b, hi);
      i0 = vfmaq_f32(vfmaq_f32(i0, a, hi), b, hr);
      a = vld1q_f32(xr + k + LANES);
      b = vld1q_f32(xi + k + LANES);
      r1 = vfmsq_f32(vfmaq_f32(r1, a, hr), b, hi);
      i1 = vfmaq_f32(vfmaq_f32(i1, a, hi), b, hr);
      a = vld1q_f32(xr + k + 2 * LANES);
      b = vld1q_f32(xi + k + 2 * LANES);
      r2 = vfmsq_f32(vfmaq_f32(r2, a, hr), b, hi);
      i2 = vfmaq_f32(vfmaq_f32(i2, a, hi), b, hr);
      a = vld1q_f32(xr + k + 3 * LANES);
      b = vld1q_f32(xi + k + 3 * LANES);
      r3 = vfmsq_f32(vfmaq_f32(r3, a, hr), b, hi);
      i3 = vfmaq_f32(vfmaq_f32(i3, a, hi), b, hr);
    }
    float *ar = acc_re + i;
    float *ai = acc_im + i;
    vst1q_f32(ar, r0);
    vst1q_f32(ar + LANES, r1);
    vst1q_f32(ar + 2 * LANES, r2);
    vst1q_f32(ar + 3 * LANES, r3);
    vst1q_f32(ai, i0);
    vst1q_f32(ai + LANES, i1);
    vst1q_f32(ai + 2 * LANES, i2);
    vst1q_f32(ai + 3 * LANES, i3);
  }
}

void channel_pipeline_freq_mac(float *y_re, float *y_im, const float *X, const float *H, int N, int nb_tx)
{
  const size_t xstride = (size_t)2 * N * LANES;
  for (int n = 0; n < N; n++) {
    float32x4_t yr = vdupq_n_f32(0.0f), yi = vdupq_n_f32(0.0f);
    for (int t = 0; t < nb_tx; t++) {
      const float *xr = X + t * xstride + (size_t)n * LANES;
      const float *xi = xr + (size_t)N * LANES;
      const float32x4_t ar = vld1q_f32(xr), ai = vld1q_f32(xi);
      const float32x4_t hr = vdupq_n_f32(H[(size_t)t * 2 * N + n]);
      const float32x4_t hi = vdupq_n_f32(H[(size_t)t * 2 * N + N + n]);
      yr = vfmsq_f32(vfmaq_f32(yr, ar, hr), ai, hi);
      yi = vfmaq_f32(vfmaq_f32(yi, ar, hi), ai, hr);
    }
    vst1q_f32(y_re + (size_t)n * LANES, yr);
    vst1q_f32(y_im + (size_t)n * LANES, yi);
  }
}

void channel_pipeline_gather_c16(float *re, float *im, const c16_t *src, int stride, int npts)
{
  int n = 0;
  for (; n + LANES <= npts; n += LANES) {
    int32x4_t m[4];
    for (int b = 0; b < 4; b++)
      m[b] = vld1q_s32((const int32_t *)(src + n + (size_t)b * stride));
    transpose4(m);
    for (int j = 0; j < 4; j++) {
      vst1q_f32(re + (size_t)(n + j) * LANES, c16_re(m[j]));
      vst1q_f32(im + (size_t)(n + j) * LANES, c16_im(m[j]));
    }
  }
  // No gather in NEON: four lane loads of one 32-bit c16_t each.
  for (; n < npts; n++) {
    const int32_t *s = (const int32_t *)(src + n);
    int32x4_t v = vdupq_n_s32(0);
    v = vld1q_lane_s32(s, v, 0);
    v = vld1q_lane_s32(s + stride, v, 1);
    v = vld1q_lane_s32(s + 2 * stride, v, 2);
    v = vld1q_lane_s32(s + 3 * stride, v, 3);
    vst1q_f32(re + (size_t)n * LANES, c16_re(v));
    vst1q_f32(im + (size_t)n * LANES, c16_im(v));
  }
}

void channel_pipeline_scatter_c16(c16_t *dst, int stride, const float *re, const float *im, int npts)
{
  int n = 0;
  for (; n + LANES <= npts; n += LANES) {
    int32x4_t m[4];
    for (int j = 0; j < 4; j++)
      m[j] = pack_c16(vld1q_f32(re + (size_t)(n + j) * LANES), vld1q_f32(im + (size_t)(n + j) * LANES));
    transpose4(m);
    for (int b = 0; b < 4; b++)
      vst1q_s32((int32_t *)(dst + n + (size_t)b * stride), m[b]);
  }
  for (; n < npts; n++) {
    int32x4_t o = pack_c16(vld1q_f32(re + (size_t)n * LANES), vld1q_f32(im + (size_t)n * LANES));
    int32_t *d = (int32_t *)(dst + n);
    vst1q_lane_s32(d, o, 0);
    vst1q_lane_s32(d + stride, o, 1);
    vst1q_lane_s32(d + 2 * stride, o, 2);
    vst1q_lane_s32(d + 3 * stride, o, 3);
  }
}
