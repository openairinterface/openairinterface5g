/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef _CHANNEL_CONVOLUTION_H_
#define _CHANNEL_CONVOLUTION_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "common/platform_types.h"

// Convolution method of the CPU pipeline. AUTO (the default) picks per call.
typedef enum {
  CHANNEL_PIPELINE_METHOD_AUTO,
  CHANNEL_PIPELINE_METHOD_DIRECT,
  CHANNEL_PIPELINE_METHOD_FFT,
} channel_pipeline_method_t;

// CPU pipeline instance: owns its scratch memory and noise device. Calls on one instance must not
// overlap; use one instance per calling thread.
typedef struct channel_pipeline_s channel_pipeline_t;

// Channel of the CPU pipeline: a copy of the taps plus the forms the convolution methods need,
// built when the taps are set and, for the FFT method, on first use of each FFT size. The caller
// keeps one per link (e.g. per UE) and sets it again only when the taps change. Used by one
// channel_pipeline() call at a time.
typedef struct channel_pipeline_channel_s channel_pipeline_channel_t;

channel_pipeline_channel_t *channel_pipeline_channel_init(void);
/// Set the taps: channel[aarx * nb_tx + aatx] holds L taps of the link from aatx to aarx.
void channel_pipeline_channel_set(channel_pipeline_channel_t *ch, const cf_t **channel, int nb_tx, int nb_rx, int L);
void channel_pipeline_channel_free(channel_pipeline_channel_t *ch);

/// Force a method; for tests and benchmarks only.
void channel_pipeline_set_method(channel_pipeline_t *p, channel_pipeline_method_t method);

#ifdef CHANNEL_SIM_CUDA
void *cuda_channel_pipeline_init(int max_samples, int num_tx_antenna, int num_rx_antenna, int max_channel_length);
void cuda_channel_pipeline_shutdown(void *context_handle);
void cuda_channel_pipeline(void *context_handle,
                           const cf_t **channel,
                           const c16_t **tx_sig0,
                           const c16_t **tx_sig1,
                           int num_samples_tx_sig0,
                           c16_t **rx_sig0,
                           c16_t **rx_sig1,
                           int num_samples_rx_sig0,
                           int num_samples,
                           int channel_length,
                           int nb_tx,
                           int nb_rx,
                           float noise_power);
#endif
channel_pipeline_t *channel_pipeline_init(float noise_power);
void channel_pipeline_shutdown(channel_pipeline_t *p);
/// rx[aarx] = sum over aatx of channel (*) tx[aatx], plus noise. Each tx signal holds L - 1 samples
/// of history followed by num_samples new ones, split over sig0 and sig1 (likewise for rx).
void channel_pipeline(channel_pipeline_t *p,
                      void *tpool,
                      channel_pipeline_channel_t *channel,
                      const c16_t **tx_sig0,
                      const c16_t **tx_sig1,
                      int num_samples_tx_sig0,
                      c16_t **rx_sig0,
                      c16_t **rx_sig1,
                      int num_samples_rx_sig0,
                      int num_samples,
                      float noise_power);
#ifdef __cplusplus
}
#endif

#endif
