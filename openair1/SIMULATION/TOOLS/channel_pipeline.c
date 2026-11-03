/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// CPU channel pipeline: rx[a] = sum_t h[a][t] (*) tx[t] + noise, for every rx antenna a.
//
// Output-stationary FMA convolution (channel_pipeline_direct_mac() in channel_pipeline_<isa>.c) on
// split (planar) float data produced by one deinterleave of the int16 input.
//
// The channel side (tap reordering/deinterleave and time reversal) depends only on the taps, so it
// lives in a caller-owned channel_pipeline_channel_t, rebuilt when the caller sets new taps.
//
// Kernels are NEON on aarch64 and AVX2 + FMA (simde) elsewhere, chosen at build time.
//
// All other state (scratch memory, noise device) lives in the instance.

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "common/utils/LOG/log.h"
#include "channel_pipeline.h"
#include "channel_pipeline_simd.h"
#include "task_ans.h"
#include "noise_device.h"
#include "thread-pool.h"

// Below this many samples x tx x rx antennas, waking the thread pool costs more than it saves and
// the call runs on the calling thread.
#define POOL_MIN_WORK 32768
#define SCRATCH_ALIGN 64

// ---------------------------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------------------------
// Grow-only scratch memory of one job index.
typedef struct {
  float *buf;
  size_t len;
} scratch_t;

struct channel_pipeline_channel_s {
  int nb_tx, nb_rx, L;
  cf_t *raw; // [nb_rx * nb_tx][L], caller order channel[aarx * nb_tx + aatx]
  float *g; // link l = aarx * nb_tx + aatx, time-reversed re at g + l * 2 * L, im at + L
};

struct channel_pipeline_s {
  noise_device_t *noise;
  // One per job index, so jobs of a call never share one.
  scratch_t *scratch;
  int num_scratch;
  // Set during a call: calls on one instance must not overlap.
  _Atomic bool busy;
};

float *channel_pipeline_scratch(const channel_pipeline_call_t *c, int job, size_t nfloats)
{
  scratch_t *s = &c->pipeline->scratch[job];
  if (s->len < nfloats) {
    free(s->buf);
    s->len = nfloats;
    s->buf = aligned_alloc(SCRATCH_ALIGN, ((nfloats * sizeof(float) + SCRATCH_ALIGN - 1) / SCRATCH_ALIGN) * SCRATCH_ALIGN);
    AssertFatal(s->buf, "out of memory (%zu floats)\n", nfloats);
  }
  return s->buf;
}

// Called by the caller thread before jobs start.
static void scratch_reserve(channel_pipeline_t *p, int num_jobs)
{
  if (p->num_scratch >= num_jobs)
    return;
  p->scratch = realloc(p->scratch, sizeof(*p->scratch) * num_jobs);
  AssertFatal(p->scratch, "out of memory\n");
  memset(p->scratch + p->num_scratch, 0, sizeof(*p->scratch) * (num_jobs - p->num_scratch));
  p->num_scratch = num_jobs;
}

// ---------------------------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------------------------
static void channel_release(channel_pipeline_channel_t *c)
{
  free(c->raw);
  free(c->g);
  memset(c, 0, sizeof(*c));
}

channel_pipeline_channel_t *channel_pipeline_channel_init(void)
{
  return calloc_or_fail(1, sizeof(channel_pipeline_channel_t));
}

void channel_pipeline_channel_free(channel_pipeline_channel_t *c)
{
  if (!c)
    return;
  channel_release(c);
  free(c);
}

void channel_pipeline_channel_set(channel_pipeline_channel_t *c, const cf_t **channel, int nb_tx, int nb_rx, int L)
{
  AssertFatal(nb_tx > 0 && nb_tx <= 64, "Invalid number of TX antennas (%d)\n", nb_tx);
  AssertFatal(nb_rx > 0 && nb_rx <= 64, "Invalid number of RX antennas (%d)\n", nb_rx);
  AssertFatal(L > 0, "channel_length must be positive (%d)\n", L);
  channel_release(c);
  const int links = nb_tx * nb_rx;
  c->nb_tx = nb_tx;
  c->nb_rx = nb_rx;
  c->L = L;
  c->raw = malloc_or_fail(sizeof(cf_t) * links * L);
  c->g = malloc_or_fail(sizeof(float) * links * 2 * L);
  for (int l = 0; l < links; l++) {
    const cf_t *taps = c->raw + (size_t)l * L;
    memcpy(c->raw + (size_t)l * L, channel[l], sizeof(cf_t) * L);
    float *gr = c->g + (size_t)l * 2 * L;
    for (int k = 0; k < L; k++) {
      gr[k] = taps[L - 1 - k].r;
      gr[L + k] = taps[L - 1 - k].i;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Jobs (the per-unit work lives in channel_pipeline_simd.c)
// ---------------------------------------------------------------------------------------------
typedef struct {
  const channel_pipeline_call_t *c;
  int index;
} pipeline_job_t;

static inline int input_len(const channel_pipeline_call_t *c)
{
  return c->num_samples + c->L - 1;
}

const c16_t *channel_pipeline_input_view(const channel_pipeline_call_t *c, int t, int start, int n, c16_t *tmp)
{
  const int total = input_len(c);
  const int n0 = min(c->num_samples_tx_sig0, total);
  if (start + n <= n0)
    return c->tx_sig0[t] + start;
  if (start >= n0 && start + n <= total)
    return c->tx_sig1[t] + (start - n0);
  int pos = start, done = 0;
  if (pos < n0) {
    int cnt = min(n, n0 - pos);
    memcpy(tmp, c->tx_sig0[t] + pos, sizeof(c16_t) * cnt);
    pos += cnt;
    done += cnt;
  }
  if (pos < total && done < n) {
    int cnt = min(n - done, total - pos);
    memcpy(tmp + done, c->tx_sig1[t] + (pos - n0), sizeof(c16_t) * cnt);
    done += cnt;
  }
  if (done < n)
    memset(tmp + done, 0, sizeof(c16_t) * (n - done));
  return tmp;
}

c16_t *channel_pipeline_output_view(const channel_pipeline_call_t *c, int a, int start, int n)
{
  const int n0 = c->num_samples_rx_sig0;
  if (start + n <= n0)
    return c->rx_sig0[a] + start;
  if (start >= n0)
    return c->rx_sig1[a] + (start - n0);
  return NULL;
}

void channel_pipeline_copy_output(const channel_pipeline_call_t *c, int a, int start, int n, const c16_t *src)
{
  const int n0 = c->num_samples_rx_sig0;
  int cnt0 = max(0, min(n, n0 - start));
  if (cnt0 > 0)
    memcpy(c->rx_sig0[a] + start, src, sizeof(c16_t) * cnt0);
  if (n > cnt0)
    memcpy(c->rx_sig1[a] + (start + cnt0 - n0), src + cnt0, sizeof(c16_t) * (n - cnt0));
}

static void pipeline_job(void *arg)
{
  pipeline_job_t *job = arg;
  channel_pipeline_run_units(job->c, job->index);
  completed_task_ans(job->c->ans);
}

// ---------------------------------------------------------------------------------------------
// Planning: fixed rules, from sweeps on Zen 5 with 1..8 tx/rx antennas and 8..128 taps.
// ---------------------------------------------------------------------------------------------
typedef struct {
  int block;
  int num_units;
  int num_jobs;
} plan_t;

static plan_t plan_call(int num_samples, int nb_tx, int nb_rx, int threads)
{
  plan_t p = {0};
  const bool pool = threads > 1 && (size_t)num_samples * nb_tx * nb_rx >= POOL_MIN_WORK;
  // About two units per thread so stragglers balance out, but not so small that the fixed per-unit
  // work dominates.
  const int batches = max(1, (2 * threads + nb_rx - 1) / nb_rx);
  p.block = max(256, min(2048, (int)channel_pipeline_round_up((num_samples + batches - 1) / batches, 64)));
  p.num_units = nb_rx * ((num_samples + p.block - 1) / p.block);
  p.num_jobs = pool ? min(threads, p.num_units) : 1;
  return p;
}

void channel_pipeline(channel_pipeline_t *p,
                      void *tpool,
                      channel_pipeline_channel_t *ch,
                      const c16_t **tx_sig0,
                      const c16_t **tx_sig1,
                      int num_samples_tx_sig0,
                      c16_t **rx_sig0,
                      c16_t **rx_sig1,
                      int num_samples_rx_sig0,
                      int num_samples,
                      float noise_power)
{
  AssertFatal(ch->raw, "channel_pipeline() on a channel without taps\n");
  const int nb_tx = ch->nb_tx;
  const int nb_rx = ch->nb_rx;
  const int channel_length = ch->L;
  AssertFatal(num_samples_tx_sig0 >= num_samples + channel_length - 1 || tx_sig1,
              "tx_sig1 required: tx_sig0 has %d samples, need %d\n",
              num_samples_tx_sig0,
              num_samples + channel_length - 1);
  AssertFatal(num_samples_rx_sig0 >= num_samples || rx_sig1,
              "rx_sig1 required: rx_sig0 has %d samples, need %d\n",
              num_samples_rx_sig0,
              num_samples);
  if (num_samples <= 0)
    return;
  AssertFatal(!atomic_exchange(&p->busy, true), "concurrent channel_pipeline() calls on one instance\n");

  tpool_t *thread_pool = (tpool_t *)tpool;
  const int threads = max(1, (int)thread_pool->len_thr);

  plan_t plan = plan_call(num_samples, nb_tx, nb_rx, threads);
  scratch_reserve(p, plan.num_jobs);
  channel_pipeline_call_t c = {
      .pipeline = p,
      .g = ch->g,
      .tx_sig0 = tx_sig0,
      .tx_sig1 = tx_sig1,
      .num_samples_tx_sig0 = num_samples_tx_sig0,
      .rx_sig0 = rx_sig0,
      .rx_sig1 = rx_sig1,
      .num_samples_rx_sig0 = num_samples_rx_sig0,
      .num_samples = num_samples,
      .nb_tx = nb_tx,
      .nb_rx = nb_rx,
      .L = channel_length,
      .noise = noise_power > 0.0f ? p->noise : NULL,
      .block = plan.block,
      .num_units = plan.num_units,
      .num_jobs = plan.num_jobs,
  };

  if (c.num_jobs <= 1) {
    c.num_jobs = 1;
    channel_pipeline_run_units(&c, 0);
  } else {
    task_ans_t ans;
    init_task_ans(&ans, c.num_jobs);
    c.ans = &ans;
    pipeline_job_t jobs[c.num_jobs];
    for (int j = 0; j < c.num_jobs; j++) {
      jobs[j] = (pipeline_job_t){.c = &c, .index = j};
      task_t task = {.func = pipeline_job, .args = &jobs[j]};
      pushTpool(thread_pool, task);
    }
    join_task_ans(&ans);
  }
  p->busy = false;
}

channel_pipeline_t *channel_pipeline_init(float noise_power)
{
  channel_pipeline_t *p = calloc_or_fail(1, sizeof(*p));
  p->noise = init_noise_device(noise_power);
  return p;
}

void channel_pipeline_shutdown(channel_pipeline_t *p)
{
  AssertFatal(!p->busy, "channel_pipeline_shutdown() during channel_pipeline()\n");
  for (int j = 0; j < p->num_scratch; j++)
    free(p->scratch[j].buf);
  free(p->scratch);
  free_noise_device(p->noise);
  free(p);
}
