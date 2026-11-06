/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include <stdlib.h>
#include "test_channel_pipeline_tools.h"
#include "channel_pipeline.h"
#include "thread-pool.h"

void *init_tpool(int num_threads)
{
  tpool_t *tpool = malloc(sizeof(*tpool));
  initFloatingCoresTpool(num_threads, tpool, false, NULL);
  return tpool;
}

void destroy_tpool(void *tpool)
{
  abortTpool((tpool_t *)tpool);
  free(tpool);
}

// Round to nearest and saturate to int16, the output conversion of the pipeline.
static int16_t sat_round_s16(float v)
{
  if (v >= 32767.0f)
    return 32767;
  if (v <= -32768.0f)
    return -32768;
  return (int16_t)lrintf(v);
}

void channel_convolution_cpu(const cf_t **channel,
                             const c16_t **tx_sig0,
                             const c16_t **tx_sig1,
                             int num_samples_tx_sig0,
                             c16_t **rx_sig0,
                             c16_t **rx_sig1,
                             int num_samples_rx_sig0,
                             int num_samples,
                             int channel_length,
                             int nb_tx,
                             int nb_rx)
{
  for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++) {
    for (int i = 0; i < num_samples; i++) {
      float rx_r = 0.0f;
      float rx_i = 0.0f;

      for (int tx_ant = 0; tx_ant < nb_tx; tx_ant++) {
        for (int l = 0; l < channel_length; l++) {
          int idx = i + (channel_length - 1) - l;
          c16_t in_sample;
          if (idx < num_samples_tx_sig0) {
            in_sample = tx_sig0[tx_ant][idx];
          } else {
            in_sample = tx_sig1[tx_ant][idx - num_samples_tx_sig0];
          }
          cf_t tx_sample = {(float)in_sample.r, (float)in_sample.i};

          int chan_idx = tx_ant + nb_tx * rx_ant;
          cf_t ch = channel[chan_idx][l];

          rx_r += tx_sample.r * ch.r - tx_sample.i * ch.i;
          rx_i += tx_sample.r * ch.i + tx_sample.i * ch.r;
        }
      }

      c16_t out = {sat_round_s16(rx_r), sat_round_s16(rx_i)};
      if (i < num_samples_rx_sig0) {
        rx_sig0[rx_ant][i] = out;
      } else {
        rx_sig1[rx_ant][i - num_samples_rx_sig0] = out;
      }
    }
  }
}

void generate_random_signal(c16_t *sig, int num_samples)
{
  for (int i = 0; i < num_samples; i++) {
    sig[i].r = (rand() % 2000) - 1000;
    sig[i].i = (rand() % 2000) - 1000;
  }
}

void generate_random_signal_float(cf_t *sig, int num_samples)
{
  for (int i = 0; i < num_samples; i++) {
    sig[i].r = (rand() % 2000) - 1000;
    sig[i].i = (rand() % 2000) - 1000;
  }
}
