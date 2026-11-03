/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// Work units of the CPU channel pipeline. The kernels are in channel_pipeline_<isa>.c, the FFT is
// batched_fft.

#include <stddef.h>
#include <string.h>
#include "channel_pipeline_simd.h"
#include "batched_fft.h"

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

// FFT unit u = one group of `lanes` consecutive overlap-save blocks, all tx and rx antennas.
// Block b of the group reads input window [start + b * B, start + b * B + N) and produces outputs
// [start + b * B, start + (b + 1) * B) from circular outputs L - 1 .. N - 1.
static void fft_unit(const channel_pipeline_call_t *c, int job, int u)
{
  const int vw = CHANNEL_PIPELINE_LANES;
  const int N = c->fft_n;
  const int B = c->block;
  const int L = c->L;
  const int start = u * vw * B;
  const int n_out = vw * B < c->num_samples - start ? vw * B : c->num_samples - start;
  const size_t plane = (size_t)N * vw;
  const size_t group = (size_t)vw * B;
  const int in_len = group + L - 1;
  // X (nb_tx transforms), Y, c16 staging for input (in_len) and output (group)
  float *X = channel_pipeline_scratch(c, job, (size_t)c->nb_tx * 2 * plane + 2 * plane + in_len + group);
  float *y_re = X + (size_t)c->nb_tx * 2 * plane;
  float *y_im = y_re + plane;
  c16_t *in_tmp = (c16_t *)(y_im + plane);
  c16_t *out_tmp = in_tmp + in_len;

  for (int t = 0; t < c->nb_tx; t++) {
    float *xr = X + t * 2 * plane;
    float *xi = xr + plane;
    channel_pipeline_gather_c16(xr, xi, channel_pipeline_input_view(c, t, start, in_len, in_tmp), B, N);
    batched_fft_forward(xr, xi, N, c->tw);
  }
  for (int a = 0; a < c->nb_rx; a++) {
    channel_pipeline_freq_mac(y_re, y_im, X, c->H + (size_t)a * c->nb_tx * 2 * N, N, c->nb_tx);
    batched_fft_inverse(y_re, y_im, N, c->tw);
    // Overlap-save: the first L - 1 circular outputs of each block are aliased, skip them. The valid
    // part is contiguous (points L - 1 .. N - 1 of every lane), so noise goes straight onto it.
    float *vr = y_re + (size_t)(L - 1) * vw;
    float *vi = y_im + (size_t)(L - 1) * vw;
    if (c->noise) {
      add_noise_vector(c->noise, vr, group);
      add_noise_vector(c->noise, vi, group);
    }
    c16_t *dst = n_out == (int)group ? channel_pipeline_output_view(c, a, start, n_out) : NULL;
    if (dst) {
      channel_pipeline_scatter_c16(dst, B, vr, vi, B);
    } else {
      channel_pipeline_scatter_c16(out_tmp, B, vr, vi, B);
      channel_pipeline_copy_output(c, a, start, n_out, out_tmp);
    }
  }
}

void channel_pipeline_run_units(const channel_pipeline_call_t *c, int index)
{
  for (int u = index; u < c->num_units; u += c->num_jobs) {
    if (c->fft_n)
      fft_unit(c, index, u);
    else
      direct_unit(c, index, u);
  }
}
