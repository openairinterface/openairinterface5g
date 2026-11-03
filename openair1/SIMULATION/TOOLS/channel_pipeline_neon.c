/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// NEON channel pipeline kernels, 4 lanes. See channel_pipeline_simd.h.

#include <stddef.h>
#include <arm_neon.h>
#include "channel_pipeline_simd.h"

#define LANES 4
_Static_assert(LANES == CHANNEL_PIPELINE_LANES, "lanes");

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
