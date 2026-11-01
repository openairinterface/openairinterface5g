/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include "utils.h"
#include "channel_pipeline.h"
#include "task_ans.h"
#include "noise_device.h"
#include "thread-pool.h"

static noise_device_t *noise_device;

typedef struct {
  const c16_t **tx_sig0;
  const c16_t **tx_sig1;
  int num_samples_tx_sig0;
  int nb_tx;
  int num_samples_rx_sig0;
  int num_samples;
  int channel_length;
  int num_jobs;
  task_ans_t *task_ans;
  float noise_power;
} job_common_args_t;

typedef struct {
  job_common_args_t *common;
  const cf_t **channel;
  c16_t *rx_sig0;
  c16_t *rx_sig1;
  int job_index;
} job_args_t;

void do_convolution_and_noise(int nb_tx,
                              int channel_length,
                              const cf_t **channel,
                              const c16_t **tx_sig0,
                              const c16_t **tx_sig1,
                              int num_samples_tx_sig0,
                              c16_t *rx_sig0,
                              c16_t *rx_sig1,
                              int num_samples_rx_sig0,
                              int num_samples,
                              int job_index,
                              int num_jobs,
                              float noise_power)
{
  const int batch_size = 4000;
  cf_t work_buffer[batch_size] __attribute__((aligned(64)));
  for (int batch_start = job_index * batch_size; batch_start < num_samples; batch_start += batch_size * num_jobs) {
    int batch_end = min(batch_start + batch_size, num_samples);
    if (noise_power > 0.0f) {
      get_noise_vector(noise_device, (float *)work_buffer, batch_size * 2);
    }
    for (int i = batch_start; i < batch_end; i++) {
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
          cf_t ch = channel[tx_ant][l];

          rx_r += tx_sample.r * ch.r - tx_sample.i * ch.i;
          rx_i += tx_sample.r * ch.i + tx_sample.i * ch.r;
        }
      }

      if (noise_power > 0.0f) {
        work_buffer[i - batch_start].r += rx_r;
        work_buffer[i - batch_start].i += rx_i;
      } else {
        work_buffer[i - batch_start].r = rx_r;
        work_buffer[i - batch_start].i = rx_i;
      }
    }
    for (int i = batch_start; i < batch_end; i++) {
      if (i < num_samples_rx_sig0) {
        rx_sig0[i].r = work_buffer[i - batch_start].r;
        rx_sig0[i].i = work_buffer[i - batch_start].i;
      } else {
        rx_sig1[i - num_samples_rx_sig0].r = work_buffer[i - batch_start].r;
        rx_sig1[i - num_samples_rx_sig0].i = work_buffer[i - batch_start].i;
      }
    }
  }
}

void channel_job(void *args)
{
  job_args_t *job_args = (job_args_t *)args;
  job_common_args_t *common = job_args->common;
  do_convolution_and_noise(common->nb_tx,
                           common->channel_length,
                           job_args->channel,
                           common->tx_sig0,
                           common->tx_sig1,
                           common->num_samples_tx_sig0,
                           job_args->rx_sig0,
                           job_args->rx_sig1,
                           common->num_samples_rx_sig0,
                           common->num_samples,
                           job_args->job_index,
                           common->num_jobs,
                           common->noise_power);
  completed_task_ans(common->task_ans);
}

void channel_pipeline(void *tpool,
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
                      float noise_power)
{
  AssertFatal(nb_tx > 0, "nb_tx must be positive (%d)\n", nb_tx);
  AssertFatal(nb_rx > 0, "nb_rx must be positive (%d)\n", nb_rx);
  AssertFatal(nb_tx <= 64, "Number of TX antennas is too large (%d)\n", nb_tx);
  AssertFatal(nb_rx <= 64, "Number of RX antennas is too large (%d)\n", nb_rx);
  tpool_t *thread_pool = (tpool_t *)tpool;
  size_t num_tpool_threads = thread_pool->len_thr;
  job_common_args_t common_args;
  common_args.tx_sig0 = tx_sig0;
  if (tx_sig1) {
    common_args.tx_sig1 = tx_sig1;
  } else {
    common_args.tx_sig1 = NULL;
  }

  // At least 1 job per RX antenna. Attempt to saturate the threadpool
  size_t num_jobs_per_rx_antenna = (num_tpool_threads + nb_rx - 1) / nb_rx;

  common_args.num_samples_tx_sig0 = num_samples_tx_sig0;
  common_args.num_samples_rx_sig0 = num_samples_rx_sig0;
  common_args.num_samples = num_samples;
  common_args.channel_length = channel_length;
  common_args.nb_tx = nb_tx;
  common_args.num_jobs = num_jobs_per_rx_antenna;
  common_args.noise_power = noise_power;

  task_ans_t task_ans;
  init_task_ans(&task_ans, nb_rx * num_jobs_per_rx_antenna);
  common_args.task_ans = &task_ans;
  job_args_t job_args_array[nb_rx][num_jobs_per_rx_antenna];
  for (int aarx = 0; aarx < nb_rx; aarx++) {
    for (int job_idx = 0; job_idx < num_jobs_per_rx_antenna; job_idx++) {
      job_args_t *job_args = &job_args_array[aarx][job_idx];
      job_args->common = &common_args;
      job_args->channel = &channel[nb_tx * aarx];
      job_args->rx_sig0 = rx_sig0[aarx];
      if (rx_sig1) {
        job_args->rx_sig1 = rx_sig1[aarx];
      } else {
        job_args->rx_sig1 = NULL;
      }
      job_args->job_index = job_idx;
      task_t task;
      task.args = job_args;
      task.func = channel_job;
      pushTpool(tpool, task);
    }
  }
  join_task_ans(&task_ans);
}

void channel_pipeline_init(float noise_power) {
  noise_device = init_noise_device(noise_power);
}

void channel_pipeline_shutdown(void) {
  free_noise_device(noise_device);
  noise_device = NULL;
}
