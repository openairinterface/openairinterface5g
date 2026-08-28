/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

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
      sig_interleaved[i][2 * j] = (float)((rand() % 2000) - 1000); // Real part (I)
      sig_interleaved[i][2 * j + 1] = (float)((rand() % 2000) - 1000); // Imaginary part (Q)
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

  int num_channels = 1;
  int nb_tx = 4;
  int nb_rx = 4;
  int num_samples = 122880;
  int channel_length = 32;
  int num_trials = 50;
  int sum_outputs = 0;
  char mode_str[10] = "batch";

  struct option long_options[] = {{"num-channels", required_argument, 0, 'c'},
                                  {"nb-tx", required_argument, 0, 't'},
                                  {"nb-rx", required_argument, 0, 'r'},
                                  {"num-samples", required_argument, 0, 's'},
                                  {"ch-len", required_argument, 0, 'l'},
                                  {"trials", required_argument, 0, 'n'},
                                  {"sum-outputs", no_argument, 0, 'S'},
                                  {"mode", required_argument, 0, 'm'},
                                  {"help", no_argument, 0, 'h'},
                                  {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "c:t:r:s:l:n:Sm:h", long_options, NULL)) != -1) {
    switch (opt) {
      case 'c':
        num_channels = atoi(optarg);
        break;
      case 't':
        nb_tx = atoi(optarg);
        break;
      case 'r':
        nb_rx = atoi(optarg);
        break;
      case 's':
        num_samples = atoi(optarg);
        break;
      case 'l':
        channel_length = atoi(optarg);
        break;
      case 'n':
        num_trials = atoi(optarg);
        break;
      case 'S':
        sum_outputs = 1;
        break;
      case 'm':
        strncpy(mode_str, optarg, sizeof(mode_str) - 1);
        break;
      case 'h':
        printf("Usage: %s [options]\n", argv[0]);
        printf("  -c, --num-channels <N>   Number of parallel channels to simulate (Default: 1)\n");
        printf("  -t, --nb-tx <N>          Number of transmit antennas (Default: 4)\n");
        printf("  -r, --nb-rx <N>          Number of receive antennas (Default: 4)\n");
        printf("  -s, --num-samples <N>    Number of samples (Default: 30720)\n");
        printf("  -l, --ch-len <N>         Channel length (Default: 32)\n");
        printf("  -n, --trials <N>         Number of trials for averaging (Default: 50)\n");
        printf("  -S, --sum-outputs        Enable summation of outputs for interference simulation.\n");
        printf("  -m, --mode <serial|stream|batch>  GPU execution mode (Default: batch)\n");
        printf("  -h, --help               Show this help message\n");
        return 0;
      default:
        exit(1);
    }
  }

  // --- MEMORY ALLOCATION ---
  // HOST MEMORY
  int num_tx_signals = sum_outputs ? num_channels : 1;
  float *tx_sig_data = malloc(num_tx_signals * nb_tx * num_samples * 2 * sizeof(float));
  float ***tx_sig_interleaved = malloc(num_tx_signals * sizeof(float **));
  for (int i = 0; i < num_tx_signals; i++) {
    tx_sig_interleaved[i] = malloc(nb_tx * sizeof(float *));
    for (int j = 0; j < nb_tx; j++) {
      tx_sig_interleaved[i][j] = tx_sig_data + (i * nb_tx + j) * num_samples * 2;
    }
  }

  float *rx_multipath_data = malloc(nb_rx * num_samples * 2 * sizeof(float));
  float **rx_multipath_re_cpu = malloc(nb_rx * sizeof(float *));
  float **rx_multipath_im_cpu = malloc(nb_rx * sizeof(float *));
  for (int i = 0; i < nb_rx; i++) {
    rx_multipath_re_cpu[i] = rx_multipath_data + i * num_samples;
    rx_multipath_im_cpu[i] = rx_multipath_data + (nb_rx + i) * num_samples;
  }

  c16_t *output_cpu_data = malloc(num_channels * nb_rx * num_samples * sizeof(c16_t));
  c16_t ***output_cpu = malloc(num_channels * sizeof(c16_t **));
  for (int c = 0; c < num_channels; c++) {
    output_cpu[c] = malloc(nb_rx * sizeof(c16_t *));
    for (int i = 0; i < nb_rx; i++) {
      output_cpu[c][i] = output_cpu_data + (c * nb_rx + i) * num_samples;
    }
  }

  channel_desc_t **channels = malloc(num_channels * sizeof(channel_desc_t *));

  // Define some realistic default values for the channel model
  // double sampling_rate = 122.88e6;
  // double channel_bandwidth = 100e6;
  // uint64_t center_freq = 3.5e9;
  // double ue_speed_kmh = 3.0;
  // double max_doppler = (ue_speed_kmh * 1000.0 / 3600.0) * center_freq / 3e8;

  for (int c = 0; c < num_channels; c++) {
    // Use the full channel descriptor initialization with all 13 arguments
    // channels[c] = new_channel_desc_scm(nb_tx,
    //                                    nb_rx,
    //                                    TDL_A,                 // channel_model
    //                                    sampling_rate,
    //                                    center_freq,
    //                                    channel_bandwidth,
    //                                    1e-7,                  // DS_TDL (Delay Spread)
    //                                    max_doppler,           // maxDoppler
    //                                    CORR_LEVEL_LOW,        // corr_level
    //                                    0.0,                   // forgetting_factor
    //                                    0,                     // channel_offset
    //                                    0.0,                   // path_loss_dB
    //                                    -100.0);               // noise_power_dB

    channels[c] = create_manual_channel_desc(nb_tx, nb_rx, channel_length);
    if (!channels[c]) {
      fprintf(stderr, "Error creating channel descriptor %d\n", c);
      exit(1);
    }
  }

  // DEVICE MEMORY
  void *d_tx_sig = NULL, *d_rx_sig = NULL, *d_curand_states = NULL, *h_tx_sig_pinned = NULL, *h_output_sig_pinned = NULL,
       *d_channel_coeffs_gpu = NULL, **d_individual_gpu_outputs = NULL, *d_summed_gpu_output = NULL;
  c16_t **output_gpu = NULL;

  void *d_tx_sig_batch = NULL, *d_intermediate_sig_batch = NULL, *d_final_output_batch = NULL, *d_channel_coeffs_batch = NULL;
  float2 *h_channel_coeffs_batch = NULL;
  float *h_channel_coeffs = NULL;
  float2 *h_tx_sig_batch_interleaved = NULL;

  const int max_taps = 256;
  const int padding_len = max_taps - 1;
  const int padded_num_samples = num_samples + padding_len;

  // Sizes for batch mode
  size_t tx_batch_bytes = num_channels * nb_tx * padded_num_samples * sizeof(float2);
  size_t intermediate_batch_bytes = num_channels * nb_rx * num_samples * sizeof(float2);
  size_t final_batch_bytes = num_channels * nb_rx * num_samples * sizeof(short2);
  size_t channel_batch_bytes = num_channels * nb_tx * nb_rx * max_taps * sizeof(float2);

  // Sizes for serial/stream mode
  size_t tx_bytes = nb_tx * padded_num_samples * 2 * sizeof(float);
  size_t rx_bytes = nb_rx * num_samples * sizeof(float2);
  size_t output_bytes = nb_rx * num_samples * sizeof(short2);
  size_t channel_bytes = nb_tx * nb_rx * max_taps * sizeof(float2);

  if (strcmp(mode_str, "batch") == 0) {
    h_channel_coeffs_batch = malloc(channel_batch_bytes);
#if defined(USE_UNIFIED_MEMORY)
    printf("Memory Mode: Unified Memory\n");
    cudaMallocManaged(&d_tx_sig_batch, tx_batch_bytes, cudaMemAttachGlobal);
    cudaMallocManaged(&d_intermediate_sig_batch, intermediate_batch_bytes, cudaMemAttachGlobal);
    cudaMallocManaged(&d_final_output_batch, final_batch_bytes, cudaMemAttachGlobal);
    cudaMallocManaged(&d_channel_coeffs_batch, channel_batch_bytes, cudaMemAttachGlobal);
#elif defined(USE_ATS_MEMORY)
    printf("Memory Mode: ATS\n");
    d_tx_sig_batch = malloc(tx_batch_bytes);
    cudaMalloc(&d_intermediate_sig_batch, intermediate_batch_bytes);
    cudaMalloc(&d_final_output_batch, final_batch_bytes);
    cudaMalloc(&d_channel_coeffs_batch, channel_batch_bytes);
#else
    printf("Memory Mode: Explicit Copy\n");
    cudaMalloc(&d_tx_sig_batch, tx_batch_bytes);
    cudaMalloc(&d_intermediate_sig_batch, intermediate_batch_bytes);
    cudaMalloc(&d_final_output_batch, final_batch_bytes);
    cudaMalloc(&d_channel_coeffs_batch, channel_batch_bytes);
    h_tx_sig_batch_interleaved = (float2 *)malloc(tx_batch_bytes);
#endif

    if (sum_outputs) {
      cudaMalloc(&d_summed_gpu_output, final_batch_bytes);
    }

  } else {
    // Serial & Stream
    h_channel_coeffs = malloc(channel_bytes);
    d_individual_gpu_outputs = malloc(num_channels * sizeof(void *));

#if defined(USE_UNIFIED_MEMORY)
    printf("Memory Mode: Unified Memory\n");
    cudaMallocManaged(&d_channel_coeffs_gpu, channel_bytes, cudaMemAttachGlobal);
    cudaMallocManaged(&d_tx_sig, tx_bytes, cudaMemAttachGlobal);
    cudaMallocManaged(&d_rx_sig, rx_bytes, cudaMemAttachGlobal);
    for (int c = 0; c < num_channels; c++) {
      cudaMallocManaged(&d_individual_gpu_outputs[c], output_bytes, cudaMemAttachGlobal);
    }
    if (sum_outputs) {
      cudaMallocManaged(&d_summed_gpu_output, output_bytes, cudaMemAttachGlobal);
    }
    h_tx_sig_pinned = d_tx_sig;
#elif defined(USE_ATS_MEMORY)
    printf("Memory Mode: ATS\n");
    cudaMalloc(&d_channel_coeffs_gpu, channel_bytes);
    cudaMalloc(&d_rx_sig, rx_bytes);
    h_tx_sig_pinned = malloc(tx_bytes);
    d_tx_sig = NULL;
    for (int c = 0; c < num_channels; c++) {
      cudaMalloc(&d_individual_gpu_outputs[c], output_bytes);
    }
    if (sum_outputs) {
      cudaMalloc(&d_summed_gpu_output, output_bytes);
    }
    if (strcmp(mode_str, "serial") == 0) {
      h_output_sig_pinned = malloc(output_bytes);
    }
#else
    printf("Memory Mode: Explicit Copy\n");
    cudaMalloc(&d_channel_coeffs_gpu, channel_bytes);
    cudaMalloc(&d_tx_sig, tx_bytes);
    cudaMalloc(&d_rx_sig, rx_bytes);
    cudaMallocHost(&h_tx_sig_pinned, tx_bytes);
    for (int c = 0; c < num_channels; c++) {
      cudaMalloc(&d_individual_gpu_outputs[c], output_bytes);
    }
    if (sum_outputs) {
      cudaMalloc(&d_summed_gpu_output, output_bytes);
    }
    if (strcmp(mode_str, "serial") == 0) {
      cudaMallocHost(&h_output_sig_pinned, output_bytes);
    }
#endif

    if (strcmp(mode_str, "serial") == 0) {
      output_gpu = malloc(nb_rx * sizeof(c16_t *));
      output_gpu[0] = malloc(nb_rx * num_samples * sizeof(c16_t));
      for (int i = 1; i < nb_rx; i++)
        output_gpu[i] = output_gpu[0] + i * num_samples;
    }
  }

  d_curand_states = create_and_init_curand_states_cuda(nb_rx * num_samples, time(NULL));

  double total_cpu_ns = 0;
  double total_gpu_ns = 0;

  for (int t = 0; t < num_trials; t++) {
    if (sum_outputs) {
      d_individual_gpu_outputs = malloc(num_channels * sizeof(void *));
    }
    for (int i = 0; i < num_tx_signals; i++) {
      generate_random_signal_interleaved(tx_sig_interleaved[i], nb_tx, num_samples);
    }

    struct timespec start, end;

    // --- CPU RUN ---
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int c = 0; c < num_channels; c++) {
      float **current_tx = sum_outputs ? tx_sig_interleaved[c] : tx_sig_interleaved[0];
      multipath_channel_float(channels[c], current_tx, rx_multipath_re_cpu, rx_multipath_im_cpu, num_samples, 1, 0);
      add_noise_float(output_cpu[c],
                      (const float **)rx_multipath_re_cpu,
                      (const float **)rx_multipath_im_cpu,
                      0.1,
                      num_samples,
                      0,
                      0,
                      0,
                      false,
                      nb_rx);
    }
    if (sum_outputs) {
      c16_t *final_sum_cpu = calloc(nb_rx * num_samples, sizeof(c16_t));
      for (int c = 0; c < num_channels; c++) {
        for (int i = 0; i < nb_rx * num_samples; i++) {
          final_sum_cpu[i].r += output_cpu[c][0][i].r;
          final_sum_cpu[i].i += output_cpu[c][0][i].i;
        }
      }
      free(final_sum_cpu);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_cpu_ns += (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    // --- GPU RUN ---
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (strcmp(mode_str, "batch") == 0) {
      for (int c = 0; c < num_channels; c++) {
        cast3Darray(ch_array, struct complexd, channels[c]->ch);
        for (int tx = 0; tx < nb_tx; tx++) {
          for (int rx = 0; rx < nb_rx; rx++) {
            int link = tx * nb_rx + rx;
            for (int l = 0; l < channel_length; l++) {
              int batch_idx = (c * nb_tx * nb_rx * max_taps) + (link * max_taps) + l;
              h_channel_coeffs_batch[batch_idx].x = (float)ch_array[tx][rx][l].r;
              h_channel_coeffs_batch[batch_idx].y = (float)ch_array[tx][rx][l].i;
            }
          }
        }
      }
    }

    if (strcmp(mode_str, "batch") == 0) {
#if defined(USE_UNIFIED_MEMORY) || defined(USE_ATS_MEMORY)
      float2 *tx_batch_ptr = (float2 *)d_tx_sig_batch;
      memset(tx_batch_ptr, 0, tx_batch_bytes);
      for (int c = 0; c < num_channels; c++) {
        float **current_tx = sum_outputs ? tx_sig_interleaved[c] : tx_sig_interleaved[0];
        for (int j = 0; j < nb_tx; j++) {
          float2 *data_start_ptr = tx_batch_ptr + (c * nb_tx + j) * padded_num_samples + padding_len;
          for (int i = 0; i < num_samples; i++) {
            data_start_ptr[i] = make_float2(current_tx[j][2 * i], current_tx[j][2 * i + 1]);
          }
        }
      }
#else // EXPLICIT COPY
      memset(h_tx_sig_batch_interleaved, 0, tx_batch_bytes);
      for (int c = 0; c < num_channels; c++) {
        float **current_tx = sum_outputs ? tx_sig_interleaved[c] : tx_sig_interleaved[0];
        for (int j = 0; j < nb_tx; j++) {
          float2 *data_start_ptr = h_tx_sig_batch_interleaved + (c * nb_tx + j) * padded_num_samples + padding_len;
          for (int i = 0; i < num_samples; i++) {
            data_start_ptr[i] = make_float2(current_tx[j][2 * i], current_tx[j][2 * i + 1]);
          }
        }
      }
      cudaMemcpy(d_tx_sig_batch, h_tx_sig_batch_interleaved, tx_batch_bytes, cudaMemcpyHostToDevice);
#endif

      cudaMemcpy(d_channel_coeffs_batch, h_channel_coeffs_batch, channel_batch_bytes, cudaMemcpyHostToDevice);

      run_channel_pipeline_cuda_batched(num_channels,
                                        nb_tx,
                                        nb_rx,
                                        channel_length,
                                        num_samples,
                                        d_channel_coeffs_batch,
                                        0.1,
                                        0,
                                        0xFFFF,
                                        0xFFFF,
                                        d_tx_sig_batch,
                                        d_intermediate_sig_batch,
                                        d_final_output_batch,
                                        d_curand_states);
      cudaDeviceSynchronize();

      if (sum_outputs) {
        for (int c = 0; c < num_channels; c++) {
          d_individual_gpu_outputs[c] = d_final_output_batch + c * nb_rx * num_samples * sizeof(short2);
        }
        sum_channel_outputs_cuda(d_individual_gpu_outputs, d_summed_gpu_output, num_channels, nb_rx, num_samples);
      }

    } else {
      if (strcmp(mode_str, "stream") == 0) {
        cudaStream_t streams[num_channels];
        for (int c = 0; c < num_channels; c++)
          cudaStreamCreateWithFlags(&streams[c], cudaStreamNonBlocking);

        for (int c = 0; c < num_channels; c++) {
          cast3Darray(ch_array, struct complexd, channels[c]->ch);
          for (int tx = 0; tx < nb_tx; tx++) {
            for (int rx = 0; rx < nb_rx; rx++) {
              int link = tx * nb_rx + rx;
              for (int l = 0; l < channels[c]->channel_length; l++) {
                int idx = link * max_taps + l;
                ((float2 *)h_channel_coeffs)[idx].x = (float)ch_array[tx][rx][l].r;
                ((float2 *)h_channel_coeffs)[idx].y = (float)ch_array[tx][rx][l].i;
              }
            }
          }

          float *h_tx_ptr = (float *)h_tx_sig_pinned;
          float **current_tx = sum_outputs ? tx_sig_interleaved[c] : tx_sig_interleaved[0];
          memset(h_tx_ptr, 0, tx_bytes);
          for (int j = 0; j < nb_tx; j++) {
            float *data_start_ptr = h_tx_ptr + (j * padded_num_samples + padding_len) * 2;
            memcpy(data_start_ptr, current_tx[j], num_samples * 2 * sizeof(float));
          }

          run_channel_pipeline_cuda_streamed(nb_tx,
                                             nb_rx,
                                             channels[c]->channel_length,
                                             num_samples,
                                             h_channel_coeffs,
                                             0.1,
                                             0,
                                             0xFFFF,
                                             0xFFFF,
                                             d_tx_sig,
                                             d_rx_sig,
                                             d_individual_gpu_outputs[c],
                                             d_curand_states,
                                             h_tx_sig_pinned,
                                             d_channel_coeffs_gpu,
                                             (void *)streams[c]);
        }
        if (sum_outputs) {
          sum_channel_outputs_cuda(d_individual_gpu_outputs, d_summed_gpu_output, num_channels, nb_rx, num_samples);
        }
        cudaDeviceSynchronize();
        for (int c = 0; c < num_channels; c++)
          cudaStreamDestroy(streams[c]);

      } else if (strcmp(mode_str, "serial") == 0) {
        for (int c = 0; c < num_channels; c++) {
          float *h_tx_ptr = (float *)h_tx_sig_pinned;
          float **current_tx = sum_outputs ? tx_sig_interleaved[c] : tx_sig_interleaved[0];

          memset(h_tx_ptr, 0, tx_bytes);
          for (int j = 0; j < nb_tx; j++) {
            float *data_start_ptr = h_tx_ptr + (j * padded_num_samples + padding_len) * 2;
            memcpy(data_start_ptr, current_tx[j], num_samples * 2 * sizeof(float));
          }

          cast3Darray(ch_array, struct complexd, channels[c]->ch);
          for (int tx = 0; tx < nb_tx; tx++) {
            for (int rx = 0; rx < nb_rx; rx++) {
              int link = tx * nb_rx + rx;
              for (int l = 0; l < channels[c]->channel_length; l++) {
                int idx = link * max_taps + l;
                ((float2 *)h_channel_coeffs)[idx].x = (float)ch_array[tx][rx][l].r;
                ((float2 *)h_channel_coeffs)[idx].y = (float)ch_array[tx][rx][l].i;
              }
            }
          }

          void *host_output_ptr_for_pipeline = h_output_sig_pinned;
#if defined(USE_UNIFIED_MEMORY)
          host_output_ptr_for_pipeline = d_individual_gpu_outputs[c];
#endif

          run_channel_pipeline_cuda(output_gpu,
                                    nb_tx,
                                    nb_rx,
                                    channels[c]->channel_length,
                                    num_samples,
                                    h_channel_coeffs,
                                    0.1,
                                    0,
                                    0xFFFF,
                                    0xFFFF,
                                    0,
                                    0,
                                    d_tx_sig,
                                    d_rx_sig,
                                    d_individual_gpu_outputs[c],
                                    d_curand_states,
                                    h_tx_sig_pinned,
                                    host_output_ptr_for_pipeline,
                                    d_channel_coeffs_gpu);
        }
      }
    }
    cudaDeviceSynchronize();
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_gpu_ns += (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  }

  double avg_cpu_us = (total_cpu_ns / num_trials) / 1000.0;
  double avg_gpu_us = (total_gpu_ns / num_trials) / 1000.0;
  double speedup = (avg_cpu_us > 0 && avg_gpu_us > 0) ? (avg_cpu_us / avg_gpu_us) : 0;
  double avg_cpu_per_channel_us = avg_cpu_us / num_channels;
  double avg_gpu_per_channel_us = avg_gpu_us / num_channels;
  double total_samples_processed = (double)num_channels * nb_rx * num_samples;
  double gpu_throughput_gsps = total_samples_processed / (avg_gpu_us * 1000.0);
  char val_str[30];

  printf("\n--- Final Benchmark Results ---\n\n");
  printf("+----------------------------------+--------------------------+\n");
  printf("| %-32s | %-24s |\n", "Configuration", "Value");
  printf("+----------------------------------+--------------------------+\n");
  printf("| %-32s | %-24d |\n", "Parallel Channels", num_channels);
  snprintf(val_str, sizeof(val_str), "%d x %d", nb_tx, nb_rx);
  printf("| %-32s | %-24s |\n", "MIMO Configuration (Tx x Rx)", val_str);
  printf("| %-32s | %-24d |\n", "Signal Length (Samples)", num_samples);
  printf("| %-32s | %-24d |\n", "Trials per configuration", num_trials);
  printf("+----------------------------------+--------------------------+\n");
  printf("| %-32s | %-24s |\n", "Performance Metric", "Value");
  printf("+----------------------------------+--------------------------+\n");
  printf("| %-32s   %-19.2f %-4s |\n", "Total CPU Time", avg_cpu_us, "[us]");
  printf("| %-32s   %-19.2f %-4s |\n", "Total GPU Time", avg_gpu_us, "[us]");
  printf("| %-32s   %-19.2f %-4s |\n", "Avg Time per Channel - CPU", avg_cpu_per_channel_us, "[us]");
  printf("| %-32s   %-19.2f %-4s |\n", "Avg Time per Channel - GPU", avg_gpu_per_channel_us, "[us]");
  snprintf(val_str, sizeof(val_str), "%.2fx", speedup);
  printf("| %-32s   %-24s |\n", "Speedup (CPU/GPU)", val_str);
  printf("| %-32s   %-24.3f |\n", "GPU Throughput (GSPS)", gpu_throughput_gsps);
  printf("+----------------------------------+--------------------------+\n");

  free(tx_sig_data);
  for (int i = 0; i < num_tx_signals; i++) {
    free(tx_sig_interleaved[i]);
  }
  free(tx_sig_interleaved);

  free(rx_multipath_data);
  free(rx_multipath_re_cpu);
  free(rx_multipath_im_cpu);

  free(output_cpu_data);
  for (int c = 0; c < num_channels; c++) {
    free(output_cpu[c]);
    free_manual_channel_desc(channels[c]);
  }
  free(output_cpu);
  free(channels);

  if (strcmp(mode_str, "batch") == 0) {
    free(h_channel_coeffs_batch);
    if (sum_outputs) {
      cudaFree(d_summed_gpu_output);
    }
#if defined(USE_ATS_MEMORY)
    free(d_tx_sig_batch);
#else
    cudaFree(d_tx_sig_batch);
    if (h_tx_sig_batch_interleaved)
      free(h_tx_sig_batch_interleaved);
#endif
    cudaFree(d_intermediate_sig_batch);
    cudaFree(d_final_output_batch);
    cudaFree(d_channel_coeffs_batch);
  } else { // Serial & Stream Cleanup
    free(h_channel_coeffs);
    cudaFree(d_channel_coeffs_gpu);

    for (int c = 0; c < num_channels; c++) {
      cudaFree(d_individual_gpu_outputs[c]);
    }
    free(d_individual_gpu_outputs);

    if (sum_outputs) {
      cudaFree(d_summed_gpu_output);
    }

#if defined(USE_UNIFIED_MEMORY)
    cudaFree(d_tx_sig);
    cudaFree(d_rx_sig);
#elif defined(USE_ATS_MEMORY)
    free(h_tx_sig_pinned);
    cudaFree(d_rx_sig);
#else // EXPLICIT COPY
    cudaFreeHost(h_tx_sig_pinned);
    cudaFree(d_tx_sig);
    cudaFree(d_rx_sig);
#endif

    if (strcmp(mode_str, "serial") == 0) {
      if (h_output_sig_pinned) {
#if defined(USE_ATS_MEMORY)
        free(h_output_sig_pinned);
#elif defined(USE_EXPLICIT_COPY)
        cudaFreeHost(h_output_sig_pinned);
#endif
      }
      if (output_gpu) {
        free(output_gpu[0]);
        free(output_gpu);
      }
    }
  }
  destroy_curand_states_cuda(d_curand_states);

  printf("Benchmark finished.\n");
  return 0;
}
