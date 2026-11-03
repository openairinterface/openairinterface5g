/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "PHY/TOOLS/tools_defs.h"
#include "SIMULATION/TOOLS/sim.h"
#include "SIMULATION/TOOLS/oai_cuda.h"
#include "common/utils/LOG/log.h"
#include "common/utils/utils.h"
#include <cuda_runtime.h>

configmodule_interface_t *uniqCfg = NULL;

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert_not_exit)
{
  fprintf(stderr, "Exit function called from %s:%d in %s(). Message: %s\n", file, line, function, s);
  exit(1);
}

void generate_random_signal_interleaved(float **sig_interleaved, int nb_ant, int num_samples)
{
  for (int i = 0; i < nb_ant; i++) {
    for (int j = 0; j < num_samples; j++) {
      sig_interleaved[i][2 * j] = (float)((rand() % 2000) - 1000);
      sig_interleaved[i][2 * j + 1] = (float)((rand() % 2000) - 1000);
    }
  }
}

channel_desc_t *create_manual_channel_desc(int nb_tx, int nb_rx, int channel_length)
{
  channel_desc_t *desc = (channel_desc_t *)calloc(1, sizeof(channel_desc_t));
  desc->nb_tx = nb_tx;
  desc->nb_rx = nb_rx;
  desc->channel_length = channel_length;
  desc->path_loss_dB = 0.0;
  desc->channel_offset = 0;
  float path_gain = (float)pow(10, -desc->path_loss_dB / 20.0);
  desc->ch = allocateFourDimArray(sizeof(struct complexd), nb_tx, nb_rx, channel_length, 0);
  cast3Darray(ch_array, struct complexd, desc->ch);
  for (int tx = 0; tx < nb_tx; tx++) {
    for (int rx = 0; rx < nb_rx; rx++) {
      for (int l = 0; l < channel_length; l++) {
        ch_array[tx][rx][l].r = ((double)rand() / (double)RAND_MAX * 0.1) * path_gain;
        ch_array[tx][rx][l].i = ((double)rand() / (double)RAND_MAX * 0.1) * path_gain;
      }
    }
  }
  return desc;
}

void free_manual_channel_desc(channel_desc_t *desc)
{
  if (!desc)
    return;
  if (desc->ch)
    free(desc->ch);
  free(desc);
}

int main(int argc, char **argv)
{
  logInit();
  randominit(0);

  int nb_tx_configs[] = {1, 2, 4, 8};
  int nb_rx_configs[] = {1, 2, 4, 8};
  int num_samples_configs[] = {30720, 61440, 122880};
  int channel_length_configs[] = {16, 32};
  char *channel_type_names[] = {"Short Channel", "Long Channel"};
  int num_trials = 50;
  float snr_db = 15.0f;

  printf("Starting Full Channel Pipeline Benchmark (Multipath + Noise)\n");
  printf("Averaging each test case over %d trials.\n", num_trials);
  printf(
      "----------------------------------------------------------------------------------------------------------------------\n");
  printf("%-15s | %-15s | %-15s | %-20s | %-20s | %-15s\n",
         "Channel Type",
         "MIMO Config",
         "Signal Length",
         "CPU Pipeline (us)",
         "GPU Pipeline (us)",
         "Overall Speedup");
  printf(
      "----------------------------------------------------------------------------------------------------------------------\n");

  for (int c = 0; c < sizeof(channel_length_configs) / sizeof(int); c++) {
    for (int s = 0; s < sizeof(num_samples_configs) / sizeof(int); s++) {
      for (int m = 0; m < sizeof(nb_tx_configs) / sizeof(int); m++) {
        int nb_tx = nb_tx_configs[m];
        int nb_rx = nb_rx_configs[m];
        int num_samples = num_samples_configs[s];
        int channel_length = channel_length_configs[c];

        char mimo_str[16];
        sprintf(mimo_str, "%dx%d", nb_tx, nb_rx);

        channel_desc_t *chan_desc = create_manual_channel_desc(nb_tx, nb_rx, channel_length);

        float **tx_sig_interleaved = malloc(nb_tx * sizeof(float *));
        float **rx_multipath_re_cpu = malloc(nb_rx * sizeof(float *));
        float **rx_multipath_im_cpu = malloc(nb_rx * sizeof(float *));
        c16_t **output_cpu = malloc(nb_rx * sizeof(c16_t *));
        c16_t **output_gpu = malloc(nb_rx * sizeof(c16_t *));

        for (int i = 0; i < nb_tx; i++) {
          tx_sig_interleaved[i] = malloc(num_samples * 2 * sizeof(float));
        }
        for (int i = 0; i < nb_rx; i++) {
          rx_multipath_re_cpu[i] = malloc(num_samples * sizeof(float));
          rx_multipath_im_cpu[i] = malloc(num_samples * sizeof(float));
        }
        output_cpu[0] = malloc(nb_rx * num_samples * sizeof(c16_t));
        output_gpu[0] = malloc(nb_rx * num_samples * sizeof(c16_t));
        for (int i = 1; i < nb_rx; i++) {
          output_cpu[i] = output_cpu[0] + i * num_samples;
          output_gpu[i] = output_gpu[0] + i * num_samples;
        }

        void *d_tx_sig = NULL, *d_rx_sig = NULL, *d_output_noise = NULL, *d_curand_states = NULL;
        void *h_tx_sig_pinned = NULL, *h_output_sig_pinned = NULL, *d_channel_coeffs_gpu = NULL;

        const int padding_len = channel_length - 1;
        const size_t padded_tx_buffer_bytes = nb_tx * (num_samples + padding_len) * 2 * sizeof(float);
        const size_t rx_buffer_bytes = nb_rx * num_samples * sizeof(float2); // Intermediate buffer
        const size_t output_buffer_bytes = nb_rx * num_samples * sizeof(short2);
        const size_t channel_buffer_size = nb_tx * nb_rx * channel_length * sizeof(float2);

#if defined(USE_UNIFIED_MEMORY)
        cudaMallocManaged(&d_tx_sig, padded_tx_buffer_bytes, cudaMemAttachGlobal);
        cudaMallocManaged(&d_rx_sig, rx_buffer_bytes, cudaMemAttachGlobal);
        cudaMallocManaged(&d_output_noise, output_buffer_bytes, cudaMemAttachGlobal);
        cudaMallocManaged(&d_channel_coeffs_gpu, channel_buffer_size, cudaMemAttachGlobal);
        h_tx_sig_pinned = d_tx_sig;
        h_output_sig_pinned = d_output_noise;
#elif defined(USE_ATS_MEMORY)
        h_tx_sig_pinned = malloc(padded_tx_buffer_bytes);
        d_tx_sig = NULL;
        cudaMalloc(&d_rx_sig, rx_buffer_bytes);
        cudaMalloc(&d_output_noise, output_buffer_bytes);
        cudaMalloc(&d_channel_coeffs_gpu, channel_buffer_size);
        h_output_sig_pinned = malloc(output_buffer_bytes);
#else
        cudaMalloc(&d_tx_sig, padded_tx_buffer_bytes);
        cudaMalloc(&d_rx_sig, rx_buffer_bytes);
        cudaMalloc(&d_output_noise, output_buffer_bytes);
        cudaMalloc(&d_channel_coeffs_gpu, channel_buffer_size);
        cudaMallocHost(&h_tx_sig_pinned, padded_tx_buffer_bytes);
        cudaMallocHost(&h_output_sig_pinned, output_buffer_bytes);
#endif

        d_curand_states = create_and_init_curand_states_cuda(nb_rx * num_samples, time(NULL));

        double ts = 1.0 / 30.72e6;
        float sigma2 = 1.0f / powf(10.0f, snr_db / 10.0f);
        int num_links = chan_desc->nb_tx * chan_desc->nb_rx;
        float *h_channel_coeffs = (float *)malloc(num_links * channel_length * sizeof(float2));
        cast3Darray(ch_array, struct complexd, chan_desc->ch);
        for (int tx = 0; tx < nb_tx; tx++) {
          for (int rx = 0; rx < nb_rx; rx++) {
            int link = tx * nb_rx + rx;
            for (int l = 0; l < channel_length; l++) {
              int idx = link * channel_length + l;
              ((float2 *)h_channel_coeffs)[idx].x = (float)ch_array[tx][rx][l].r;
              ((float2 *)h_channel_coeffs)[idx].y = (float)ch_array[tx][rx][l].i;
            }
          }
        }

        double total_cpu_ns = 0;
        double total_gpu_ns = 0;

        for (int t = 0; t < num_trials; t++) {
          generate_random_signal_interleaved(tx_sig_interleaved, nb_tx, num_samples);
          struct timespec start, end;

          const int padding_len = channel_length - 1;
          const int padded_num_samples = num_samples + padding_len;
          float *h_tx_ptr = (float *)h_tx_sig_pinned;
          memset(h_tx_ptr, 0, padded_tx_buffer_bytes);
          for (int j = 0; j < nb_tx; j++) {
            float *data_start_ptr = h_tx_ptr + (j * padded_num_samples + padding_len) * 2;
            memcpy(data_start_ptr, tx_sig_interleaved[j], num_samples * 2 * sizeof(float));
          }

          clock_gettime(CLOCK_MONOTONIC, &start);
          float **tx_sig_for_cpu = malloc(nb_tx * sizeof(float *));
          for (int j = 0; j < nb_tx; j++) {
            tx_sig_for_cpu[j] = h_tx_ptr + (j * padded_num_samples + padding_len) * 2;
          }
          multipath_channel_float(chan_desc, tx_sig_for_cpu, rx_multipath_re_cpu, rx_multipath_im_cpu, num_samples, 1, 0);
          free(tx_sig_for_cpu);
          add_noise_float(output_cpu,
                          (const float **)rx_multipath_re_cpu,
                          (const float **)rx_multipath_im_cpu,
                          sigma2,
                          num_samples,
                          0,
                          ts,
                          0,
                          false,
                          nb_rx);
          clock_gettime(CLOCK_MONOTONIC, &end);
          total_cpu_ns += (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

          clock_gettime(CLOCK_MONOTONIC, &start);
          run_channel_pipeline_cuda(output_gpu,
                                    nb_tx,
                                    nb_rx,
                                    channel_length,
                                    num_samples,
                                    h_channel_coeffs,
                                    sigma2,
                                    ts,
                                    0,
                                    0,
                                    0,
                                    0,
                                    d_tx_sig,
                                    d_rx_sig,
                                    d_output_noise,
                                    d_curand_states,
                                    h_tx_sig_pinned,
                                    h_output_sig_pinned,
                                    d_channel_coeffs_gpu);
          cudaDeviceSynchronize();
          clock_gettime(CLOCK_MONOTONIC, &end);
          total_gpu_ns += (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
        }

        double avg_cpu_us = (total_cpu_ns / num_trials) / 1000.0;
        double avg_gpu_us = (total_gpu_ns / num_trials) / 1000.0;
        double speedup = (avg_gpu_us > 0) ? (avg_cpu_us / avg_gpu_us) : 0;

        printf("%-15s | %-15s | %-15d | %-20.2f | %-20.2f | %-15.2fx\n",
               channel_type_names[c],
               mimo_str,
               num_samples,
               avg_cpu_us,
               avg_gpu_us,
               speedup);

        free(h_channel_coeffs);
        for (int i = 0; i < nb_tx; i++) {
          free(tx_sig_interleaved[i]);
        }
        for (int i = 0; i < nb_rx; i++) {
          free(rx_multipath_re_cpu[i]);
          free(rx_multipath_im_cpu[i]);
        }
        free(tx_sig_interleaved);
        free(rx_multipath_re_cpu);
        free(rx_multipath_im_cpu);
        free(output_cpu[0]);
        free(output_gpu[0]);
        free(output_cpu);
        free(output_gpu);

#if defined(USE_UNIFIED_MEMORY)
        cudaFree(d_tx_sig); // In UM, h_tx_sig_pinned is an alias for d_tx_sig
        cudaFree(d_rx_sig);
        cudaFree(d_output_noise);
#elif defined(USE_ATS_MEMORY)
        free(h_tx_sig_pinned);
        cudaFree(d_rx_sig);
        cudaFree(d_output_noise);
        free(h_output_sig_pinned);
#else // EXPLICIT COPY
        cudaFree(d_tx_sig);
        cudaFree(d_rx_sig);
        cudaFree(d_output_noise);
        cudaFreeHost(h_tx_sig_pinned);
        cudaFreeHost(h_output_sig_pinned);
#endif

        cudaFree(d_channel_coeffs_gpu);
        destroy_curand_states_cuda(d_curand_states);
        free_manual_channel_desc(chan_desc);
      }
    }
  }

  printf(
      "----------------------------------------------------------------------------------------------------------------------\n");
  printf("Pipeline benchmark finished.\n");

  return 0;
}
