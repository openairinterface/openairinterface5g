/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <time.h>
#include <getopt.h>
#include "oai_cuda.h"
#include "common/config/config_userapi.h"
#include <memory>
#include "benchmark/benchmark.h"
#include "test_channel_pipeline_tools.h"
#include "channel_pipeline.h"
extern "C" {
#include "openair1/SIMULATION/TOOLS/sim.h"
}
configmodule_interface_t *uniqCfg = NULL;

extern "C" void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  fprintf(stderr, "FATAL: %s at %s:%s:%d\n", s, file, function, line);
  exit(EXIT_FAILURE);
}

#define MAX_SAMPLE_LENGTH (65536)

#ifdef CHANNEL_SIM_CUDA
static void BM_channel_convolution_gpu(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = state.range(3);

  // nb_tx=64 and nb_rx>4 are outside the currently supported GPU pipeline configuration;
  // Support all lesser link level configs and skip unsupported configs
  if (nb_tx * nb_rx > (64 * 4)) {
    state.SkipWithMessage("nb_tx=64 with nb_rx > 4  is not currently supported");
    return;
  }
  // Skip any configs where RX is larger than TX as this is not a realistic case
  if (nb_tx < nb_rx) {
    state.SkipWithMessage("No configs where RX antenna count is > TX antenna count");
    return;
  }

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }

  void *gpu_context = cuda_channel_pipeline_init(MAX_SAMPLE_LENGTH, nb_tx, nb_rx, channel_length);

  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    cuda_channel_pipeline(gpu_context,
                          (const cf_t **)channel.data(),
                          (const c16_t **)input.data(),
                          nullptr,
                          num_input_samples,
                          output.data(),
                          nullptr,
                          num_samples,
                          num_samples,
                          channel_length,
                          nb_tx,
                          nb_rx,
                          0.0f);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  cuda_channel_pipeline_shutdown(gpu_context);
}

#endif

static void BM_channel_convolution_cpu(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = state.range(3);

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    channel_convolution_cpu((const cf_t **)channel.data(),
                            (const c16_t **)input.data(),
                            nullptr,
                            num_input_samples,
                            output.data(),
                            nullptr,
                            num_samples,
                            num_samples,
                            channel_length,
                            nb_tx,
                            nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.0f, benchmark::Counter::kIsRate);

  for (int i = 0; i < nb_tx; ++i)
    delete[] input[i];
  for (int i = 0; i < nb_rx * nb_tx; ++i)
    delete[] channel[i];
  for (int i = 0; i < nb_rx; ++i) {
    delete[] output[i];
  }
}

static void BM_channel_convolution_tpool(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = state.range(3);

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  void *tpool = init_tpool(16);
  channel_pipeline_t *pipeline = channel_pipeline_init(0.0f);
  channel_pipeline_channel_t *pc = channel_pipeline_channel_init();
  channel_pipeline_channel_set(pc, (const cf_t **)channel.data(), nb_tx, nb_rx, channel_length);

  size_t total_samples = 0;
  for (auto _ : state) {
    channel_pipeline(pipeline,
                     tpool,
                     pc,
                     (const c16_t **)input.data(),
                     nullptr,
                     num_input_samples,
                     output.data(),
                     nullptr,
                     num_samples,
                     num_samples,
                     0.0f);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  channel_pipeline_channel_free(pc);
  channel_pipeline_shutdown(pipeline);

  destroy_tpool(tpool);

  for (int i = 0; i < nb_tx; ++i)
    delete[] input[i];
  for (int i = 0; i < nb_rx * nb_tx; ++i)
    delete[] channel[i];
  for (int i = 0; i < nb_rx; ++i) {
    delete[] output[i];
  }
}

#ifdef CHANNEL_SIM_CUDA
BENCHMARK(BM_channel_convolution_gpu)
    ->ArgsProduct({
        {1, 2, 4, 8, 16, 64}, // nb_rx
        {1, 2, 4, 8, 16, 64}, // nb_tx
        {1024, 2048, 30720}, // num_samples
        {8, 16, 32, 64}, // channel_length
    })
    ->Iterations(50);
#endif

BENCHMARK(BM_channel_convolution_cpu)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {1024, 2048, 30720}, // num_samples
        {8, 16, 32, 64}, // channel_length
    })
    ->Iterations(10);

BENCHMARK(BM_channel_convolution_tpool)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {1024, 2048, 30720}, // num_samples
        {8, 16, 32, 64}, // channel_length
    })
    ->Iterations(10);

int main(int argc, char **argv)
{
  logInit();
  randominit();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
