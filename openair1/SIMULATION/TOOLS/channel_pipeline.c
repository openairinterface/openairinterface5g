/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// CPU channel pipeline: rx[a] = sum_t h[a][t] (*) tx[t] + noise, for every rx antenna a.
//
// Two methods, both on split (planar) float data produced by one deinterleave of the int16 input:
//  - direct: output-stationary FMA convolution (channel_pipeline_direct_mac() in channel_pipeline_<isa>.c)
//  - fft:    overlap-save with an N-point FFT, the same decomposition as the GPU pipeline. Each SIMD
//            lane carries its own block, so butterflies are purely vertical.
//
// The channel side (tap reordering/deinterleave, time reversal for the direct method and the
// frequency response for the FFT method) depends only on the taps, so it lives in a caller-owned
// channel_pipeline_channel_t, rebuilt when the caller sets new taps.
//
// Kernels are NEON on aarch64 and AVX2 + FMA (simde) elsewhere, chosen at build time.
//
// All other state (method, scratch memory, noise device) lives in the instance.

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "common/utils/LOG/log.h"
#include "channel_pipeline.h"
#include "channel_pipeline_simd.h"
#include "batched_fft.h"
#include "task_ans.h"
#include "noise_device.h"
#include "thread-pool.h"

#define FFT_MIN_N 64
// The FFT method is used from this many taps, or from FFT_MIN_TAPS_MIMO taps with at least
// FFT_MIN_LINKS tx x rx links; below, the direct method is faster.
#define FFT_MIN_TAPS 16
#define FFT_MIN_TAPS_MIMO 8
#define FFT_MIN_LINKS 8
// Below this many samples x tx x rx antennas, waking the thread pool costs more than it saves and
// the call runs on the calling thread.
#define POOL_MIN_WORK 32768
#define FFT_MAX_LOG2 BATCHED_FFT_MAX_LOG2
#define FFT_MAX_SCRATCH_FLOATS (1 << 20)
#define FFT_MAX_RESPONSE_FLOATS (1 << 21)
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
  float *g; // direct: link l = aarx * nb_tx + aatx, time-reversed re at g + l * 2 * L, im at + L
  // fft, built on first use of each size N: link l, bit-reversed frequency response / N, re at
  // H[log2 N] + l * 2 * N, im at + N
  float *H[FFT_MAX_LOG2 + 1];
};

struct channel_pipeline_s {
  channel_pipeline_method_t method;
  noise_device_t *noise;
  // One per job index, so jobs of a call never share one.
  scratch_t *scratch;
  int num_scratch;
  // Set during a call: calls on one instance must not overlap.
  _Atomic bool busy;
};

void channel_pipeline_set_method(channel_pipeline_t *p, channel_pipeline_method_t method)
{
  p->method = method;
}

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
  for (int i = 0; i <= FFT_MAX_LOG2; i++)
    free(c->H[i]);
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

static const float *channel_fft(channel_pipeline_channel_t *c, int N)
{
  const int log2n = __builtin_ctz(N);
  if (c->H[log2n])
    return c->H[log2n];
  const int links = c->nb_tx * c->nb_rx;
  const int L = c->L;
  const float *tw = batched_fft_twiddles(N);
  float *H = malloc_or_fail(sizeof(float) * links * 2 * N);
  // Up to `lanes` links per transform call, lane-major.
  const int vw = CHANNEL_PIPELINE_LANES;
  const size_t plane = (size_t)N * vw;
  float *re = aligned_alloc(SCRATCH_ALIGN, sizeof(float) * 2 * plane);
  AssertFatal(re, "out of memory\n");
  float *im = re + plane;
  for (int l0 = 0; l0 < links; l0 += vw) {
    memset(re, 0, sizeof(float) * 2 * plane);
    for (int b = 0; b < vw && l0 + b < links; b++) {
      const cf_t *taps = c->raw + (size_t)(l0 + b) * L;
      for (int k = 0; k < L; k++) {
        re[(size_t)k * vw + b] = taps[k].r / N;
        im[(size_t)k * vw + b] = taps[k].i / N;
      }
    }
    batched_fft_forward(re, im, N, tw);
    for (int b = 0; b < vw && l0 + b < links; b++) {
      float *hr = H + (size_t)(l0 + b) * 2 * N;
      float *hi = hr + N;
      for (int n = 0; n < N; n++) {
        hr[n] = re[(size_t)n * vw + b];
        hi[n] = im[(size_t)n * vw + b];
      }
    }
  }
  free(re);
  c->H[log2n] = H;
  return H;
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
  int fft_n; // 0: direct
  int block;
  int num_units;
  int num_jobs;
} plan_t;

static int pow2_ceil(int x)
{
  int p = 1;
  while (p < x)
    p <<= 1;
  return p;
}

// FFT size: 4 L (at least 64) is within a few percent of the best size for long calls, but no larger
// than needed for `units` groups of one block per lane to cover the call. 0 if the channel is too
// long for the FFT.
static int fft_size(int num_samples, int nb_tx, int nb_rx, int L, int vw, int units)
{
  const int n_min = pow2_ceil(2 * L); // B = N - L + 1 >= L
  const int per_block = (num_samples + vw * units - 1) / (vw * units);
  int N = max(FFT_MIN_N, pow2_ceil(4 * L));
  N = min(N, max(n_min, pow2_ceil(per_block + L - 1)));
  // Stay within a memory budget: per-thread scratch (all tx transforms) and the stored frequency
  // response of all links.
  while (N > n_min
         && ((size_t)nb_tx * 2 * N * vw > FFT_MAX_SCRATCH_FLOATS || (size_t)nb_tx * nb_rx * 2 * N > FFT_MAX_RESPONSE_FLOATS))
    N >>= 1;
  return N <= (1 << FFT_MAX_LOG2) ? N : 0;
}

static plan_t plan_call(channel_pipeline_method_t method, int num_samples, int nb_tx, int nb_rx, int L, int vw, int threads)
{
  plan_t p = {0};
  const bool pool = threads > 1 && (size_t)num_samples * nb_tx * nb_rx >= POOL_MIN_WORK;
  const bool fft_faster = L >= FFT_MIN_TAPS || (L >= FFT_MIN_TAPS_MIMO && nb_tx * nb_rx >= FFT_MIN_LINKS);
  const bool fft = method == CHANNEL_PIPELINE_METHOD_FFT || (method == CHANNEL_PIPELINE_METHOD_AUTO && fft_faster);
  p.fft_n = fft ? fft_size(num_samples, nb_tx, nb_rx, L, vw, pool ? threads : 1) : 0;
  if (p.fft_n) {
    p.block = p.fft_n - L + 1;
    p.num_units = ((num_samples + p.block - 1) / p.block + vw - 1) / vw;
  } else {
    // About two units per thread so stragglers balance out, but not so small that the fixed per-unit
    // work dominates.
    const int batches = max(1, (2 * threads + nb_rx - 1) / nb_rx);
    p.block = max(256, min(2048, (int)channel_pipeline_round_up((num_samples + batches - 1) / batches, 64)));
    p.num_units = nb_rx * ((num_samples + p.block - 1) / p.block);
  }
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
  const int vw = CHANNEL_PIPELINE_LANES;

  plan_t plan = plan_call(p->method, num_samples, nb_tx, nb_rx, channel_length, vw, threads);
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
      .fft_n = plan.fft_n,
      .block = plan.block,
      .num_units = plan.num_units,
      .num_jobs = plan.num_jobs,
  };
  if (plan.fft_n) {
    c.tw = batched_fft_twiddles(plan.fft_n);
    c.H = channel_fft(ch, plan.fft_n);
  }

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
  p->method = CHANNEL_PIPELINE_METHOD_AUTO;
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
