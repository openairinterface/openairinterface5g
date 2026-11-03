/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// Work units of the CPU channel pipeline. The kernels are in channel_pipeline_<isa>.c.

#include <stddef.h>
#include <string.h>
#include "channel_pipeline_simd.h"

// Direct unit u = (batch u / nb_rx, rx antenna u % nb_rx).
static void direct_unit(const channel_pipeline_call_t *c, int job, int u)
{
  const int vw = CHANNEL_PIPELINE_LANES;
  const int L = c->L;
  const int a = u % c->nb_rx;
  const int start = (u / c->nb_rx) * c->block;
  const int n = c->block < c->num_samples - start ? c->block : c->num_samples - start;
  const int npad = channel_pipeline_round_up(n, 4 * vw);
  const int xlen = channel_pipeline_round_up(npad + L - 1, vw);
  // acc re/im (npad each), x re/im (xlen each), c16 staging (max(xlen, npad) entries)
  float *acc_re = channel_pipeline_scratch(c, job, 2 * (size_t)npad + 3 * (size_t)xlen);
  float *acc_im = acc_re + npad;
  float *x_re = acc_im + npad;
  float *x_im = x_re + xlen;
  c16_t *tmp = (c16_t *)(x_im + xlen);
  // Noise snapshot as the starting value of the accumulators (the noise device keeps rewriting its
  // table, so it is copied out rather than referenced); without noise they start from zero.
  const float *in_re = NULL, *in_im = NULL;
  if (c->noise) {
    get_noise_vector(c->noise, acc_re, npad);
    get_noise_vector(c->noise, acc_im, npad);
    in_re = acc_re;
    in_im = acc_im;
  }
  for (int t = 0; t < c->nb_tx; t++) {
    channel_pipeline_c16_to_split(x_re, x_im, channel_pipeline_input_view(c, t, start, xlen, tmp), xlen);
    const float *g = c->g + (size_t)(a * c->nb_tx + t) * 2 * L;
    channel_pipeline_direct_mac(acc_re, acc_im, in_re, in_im, x_re, x_im, g, g + L, npad, L);
    in_re = acc_re;
    in_im = acc_im;
  }
  c16_t *dst = channel_pipeline_output_view(c, a, start, n);
  if (dst) {
    channel_pipeline_split_to_c16(dst, acc_re, acc_im, n);
  } else {
    channel_pipeline_split_to_c16(tmp, acc_re, acc_im, n);
    channel_pipeline_copy_output(c, a, start, n, tmp);
  }
}

void channel_pipeline_run_units(const channel_pipeline_call_t *c, int index)
{
  for (int u = index; u < c->num_units; u += c->num_jobs)
    direct_unit(c, index, u);
}
