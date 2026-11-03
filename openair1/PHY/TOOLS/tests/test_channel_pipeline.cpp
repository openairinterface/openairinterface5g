/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <gtest/gtest.h>
#include <vector>
#include <tuple>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <utility>
#include "oai_cuda.h"
#include "test_channel_pipeline_tools.h"
#include "channel_pipeline.h"

#define MAX_SAMPLE_LENGTH (65536)

extern "C" {
#include "openair1/SIMULATION/TOOLS/sim.h"
}
configmodule_interface_t *uniqCfg = NULL;

extern "C" void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  fprintf(stderr, "FATAL: %s at %s:%s:%d\n", s, file, function, line);
  exit(EXIT_FAILURE);
}

// Taps scaled as K/sqrt(nb_tx * channel_length) so the output std stays ~2800 regardless of the
// antenna/tap count (see accuracy_test_gpu_optimized_pipeline.cpp): far from int16 saturation, so
// the comparison checks the convolution rather than clipping.
static void generate_scaled_taps(cf_t *sig, int channel_length, int nb_tx)
{
  const float max_tap_mag = 6.0f / std::sqrt((float)nb_tx * (float)channel_length);
  for (int i = 0; i < channel_length; i++) {
    sig[i].r = (((rand() % 2000) - 1000) / 1000.0f) * max_tap_mag;
    sig[i].i = (((rand() % 2000) - 1000) / 1000.0f) * max_tap_mag;
  }
}

#ifdef CHANNEL_SIM_CUDA
class ChannelConvolutionTest : public ::testing::TestWithParam<std::tuple<int, int, int>> {
 protected:
  void SetUp() override
  {
    const int nb_rx = std::get<0>(GetParam());
    const int nb_tx = std::get<1>(GetParam());
    const int channel_length = 16;
    gpu_context = cuda_channel_pipeline_init(MAX_SAMPLE_LENGTH, nb_tx, nb_rx, channel_length);
    tpool = init_tpool(8);
    pipeline = channel_pipeline_init(0.0f);
  }

  void TearDown() override
  {
    cuda_channel_pipeline_shutdown(gpu_context);
    destroy_tpool(tpool);
    channel_pipeline_shutdown(pipeline);
  }

  void *gpu_context = nullptr;
  void *tpool = nullptr;
  channel_pipeline_t *pipeline = nullptr;
};

TEST_P(ChannelConvolutionTest, CompareCpuGpu)
{
  int nb_rx = std::get<0>(GetParam());
  int nb_tx = std::get<1>(GetParam());
  int num_samples = std::get<2>(GetParam());
  int channel_length = 16;

  // nb_tx=64 and nb_rx>4 are outside the currently supported GPU pipeline configuration;
  // Support all lesser link level configs and skip unsupported configs
  if (nb_tx * nb_rx > (64 * 4)) {
    GTEST_SKIP() << "nb_tx=64 with nb_rx > 4  is not currently supported";
  }
  // Skip any configs where RX is larger than TX as this is not a realistic case
  if (nb_tx < nb_rx) {
    GTEST_SKIP() << "No configs where RX antenna count is > TX antenna count";
  }

  // The input buffer must be padded at the beginning to handle the convolution history.
  size_t num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
    generate_random_signal(input[i], num_input_samples);
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
    generate_scaled_taps(channel[i], channel_length, nb_tx);
  }

  std::vector<c16_t *> output_cpu(nb_rx);
  std::vector<c16_t *> output_gpu(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output_cpu[i] = new c16_t[num_samples];
    output_gpu[i] = new c16_t[num_samples];
    memset(output_cpu[i], 0, num_samples * sizeof(c16_t));
    memset(output_gpu[i], 0, num_samples * sizeof(c16_t));
  }

  // Run CPU implementation
  channel_convolution_cpu((const cf_t **)channel.data(),
                          (const c16_t **)input.data(),
                          nullptr,
                          num_input_samples,
                          output_cpu.data(),
                          nullptr,
                          num_samples,
                          num_samples,
                          channel_length,
                          nb_tx,
                          nb_rx);

  // Run GPU implementation
  cuda_channel_pipeline(gpu_context,
                        (const cf_t **)channel.data(),
                        (const c16_t **)input.data(),
                        nullptr,
                        num_input_samples,
                        output_gpu.data(),
                        nullptr,
                        num_samples,
                        num_samples,
                        channel_length,
                        nb_tx,
                        nb_rx,
                        0.0f);

  // Compare results - increase LSbit limit to 3 as GPU does frequency processing and CPU does time processing
  for (int r = 0; r < nb_rx; ++r) {
    for (int i = 0; i < num_samples; ++i) {
      EXPECT_LE(std::abs(output_cpu[r][i].r - output_gpu[r][i].r), 7) << "Real part mismatch at rx=" << r << " sample=" << i;
      EXPECT_LE(std::abs(output_cpu[r][i].i - output_gpu[r][i].i), 7) << "Imag part mismatch at rx=" << r << " sample=" << i;
    }
  }

  // Cleanup
  for (int i = 0; i < nb_tx; ++i)
    delete[] input[i];
  for (int i = 0; i < nb_rx * nb_tx; ++i)
    delete[] channel[i];
  for (int i = 0; i < nb_rx; ++i) {
    delete[] output_cpu[i];
    delete[] output_gpu[i];
  }
}

INSTANTIATE_TEST_SUITE_P(ChannelConvolutionTests,
                         ChannelConvolutionTest,
                         ::testing::Combine(::testing::Values(1, 2, 4),
                                            ::testing::Values(1, 2, 4),
                                            ::testing::Values(100, 1024, 6000, MAX_SAMPLE_LENGTH)),
                         [](const ::testing::TestParamInfo<ChannelConvolutionTest::ParamType> &info) {
                           int rx = std::get<0>(info.param);
                           int tx = std::get<1>(info.param);
                           int samples = std::get<2>(info.param);
                           std::ostringstream name;
                           name << "Rx" << rx << "_Tx" << tx << "_Samples" << samples;
                           return name.str();
                         });
#endif // CHANNEL_SIM_CUDA

// Input/output of one pipeline call, with the input split into sig0|sig1 at in_split and the
// output split into sig0|sig1 at out_split (the same way vrtsim passes history + new samples).
struct PipelineIo {
  int nb_tx, nb_rx, num_samples, L, in_split, out_split;
  std::vector<std::vector<c16_t>> in0, in1, out0, out1, ref0, ref1;
  std::vector<std::vector<cf_t>> channel;

  PipelineIo(int nb_tx_, int nb_rx_, int num_samples_, int L_, int in_split_, int out_split_)
      : nb_tx(nb_tx_), nb_rx(nb_rx_), num_samples(num_samples_), L(L_), in_split(in_split_), out_split(out_split_)
  {
    const int total = num_samples + L - 1;
    in0.resize(nb_tx);
    in1.resize(nb_tx);
    for (int t = 0; t < nb_tx; t++) {
      in0[t].resize(in_split);
      in1[t].resize(std::max(1, total - in_split));
      generate_random_signal(in0[t].data(), in_split);
      generate_random_signal(in1[t].data(), total - in_split);
    }
    for (auto *v : {&out0, &out1, &ref0, &ref1})
      v->resize(nb_rx);
    for (int r = 0; r < nb_rx; r++) {
      out0[r].assign(out_split, {0, 0});
      ref0[r].assign(out_split, {0, 0});
      out1[r].assign(std::max(1, num_samples - out_split), {0, 0});
      ref1[r].assign(std::max(1, num_samples - out_split), {0, 0});
    }
    channel.resize(nb_rx * nb_tx);
    for (auto &c : channel) {
      c.resize(L);
      generate_scaled_taps(c.data(), L, nb_tx);
    }
  }

  template <typename T>
  static std::vector<T *> ptrs(std::vector<std::vector<T>> &v)
  {
    std::vector<T *> p;
    for (auto &x : v)
      p.push_back(x.data());
    return p;
  }

  // Set the taps on pc and run on it.
  void run(channel_pipeline_t *pipeline, void *tpool, channel_pipeline_channel_t *pc, float noise_power = 0.0f)
  {
    auto ch = ptrs(channel);
    channel_pipeline_channel_set(pc, (const cf_t **)ch.data(), nb_tx, nb_rx, L);
    auto i0 = ptrs(in0), i1 = ptrs(in1);
    auto o0 = ptrs(out0), o1 = ptrs(out1);
    channel_pipeline(pipeline,
                     tpool,
                     pc,
                     (const c16_t **)i0.data(),
                     (const c16_t **)i1.data(),
                     in_split,
                     o0.data(),
                     o1.data(),
                     out_split,
                     num_samples,
                     noise_power);
  }

  void run(channel_pipeline_t *pipeline, void *tpool, float noise_power = 0.0f)
  {
    channel_pipeline_channel_t *pc = channel_pipeline_channel_init();
    run(pipeline, tpool, pc, noise_power);
    channel_pipeline_channel_free(pc);
  }

  void reference()
  {
    auto ch = ptrs(channel);
    auto i0 = ptrs(in0), i1 = ptrs(in1);
    auto r0 = ptrs(ref0), r1 = ptrs(ref1);
    channel_convolution_cpu((const cf_t **)ch.data(),
                            (const c16_t **)i0.data(),
                            (const c16_t **)i1.data(),
                            in_split,
                            r0.data(),
                            r1.data(),
                            out_split,
                            num_samples,
                            L,
                            nb_tx,
                            nb_rx);
  }

  // Max absolute per-component difference to the reference, and error power.
  std::pair<int, double> compare() const
  {
    int max_err = 0;
    double err_pow = 0;
    for (int r = 0; r < nb_rx; r++) {
      for (int i = 0; i < num_samples; i++) {
        const c16_t a = i < out_split ? out0[r][i] : out1[r][i - out_split];
        const c16_t b = i < out_split ? ref0[r][i] : ref1[r][i - out_split];
        int dr = std::abs(a.r - b.r), di = std::abs(a.i - b.i);
        max_err = std::max(max_err, std::max(dr, di));
        err_pow += (double)dr * dr + (double)di * di;
      }
    }
    return {max_err, err_pow / (2.0 * nb_rx * num_samples)};
  }
};

using CpuParam = std::tuple<channel_pipeline_method_t, int, int, int, int>;

class ChannelPipelineCpuTest : public ::testing::TestWithParam<CpuParam> {
 protected:
  void SetUp() override
  {
    pipeline = channel_pipeline_init(0.0f);
    channel_pipeline_set_method(pipeline, std::get<0>(GetParam()));
    tpool = init_tpool(8);
  }

  void TearDown() override
  {
    if (tpool)
      destroy_tpool(tpool);
    channel_pipeline_shutdown(pipeline);
  }

  void *tpool = nullptr;
  channel_pipeline_t *pipeline = nullptr;
};

TEST_P(ChannelPipelineCpuTest, MatchesReference)
{
  const int nb_rx = std::get<1>(GetParam());
  const int nb_tx = std::get<2>(GetParam());
  const int num_samples = std::get<3>(GetParam());
  const int L = std::get<4>(GetParam());
  const int total = num_samples + L - 1;
  // Input split: all in sig0; vrtsim layout (only the L - 1 history in sig0); an odd split.
  // Output split: all in sig0, or an odd split.
  const int in_splits[] = {total, L - 1, total / 2 + 3};
  const int out_splits[] = {num_samples, num_samples / 2 + 1};
  for (int in_split : in_splits) {
    for (int out_split : out_splits) {
      PipelineIo io(nb_tx, nb_rx, num_samples, L, in_split, out_split);
      io.reference();
      io.run(pipeline, tpool);
      auto [max_err, err_pow] = io.compare();
      EXPECT_LE(max_err, 1) << "in_split=" << in_split << " out_split=" << out_split;
    }
  }
}

// Setting new taps on a channel replaces everything built from the old ones, including the
// frequency response of an FFT size already used.
TEST_P(ChannelPipelineCpuTest, ChannelSetReplacesTaps)
{
  const int nb_rx = std::get<1>(GetParam());
  const int nb_tx = std::get<2>(GetParam());
  const int num_samples = std::get<3>(GetParam());
  const int L = std::get<4>(GetParam());
  PipelineIo a(nb_tx, nb_rx, num_samples, L, L - 1, num_samples);
  PipelineIo b(nb_tx, nb_rx, num_samples, L, L - 1, num_samples);
  // A different shape, so set also has to resize.
  PipelineIo c(nb_rx, nb_tx, num_samples, L + 3, L + 2, num_samples);
  channel_pipeline_channel_t *pc = channel_pipeline_channel_init();
  for (int round = 0; round < 3; round++) {
    for (PipelineIo *io : {&a, &b, &c}) {
      io->reference();
      io->run(pipeline, tpool, pc);
      EXPECT_LE(io->compare().first, 1) << "round " << round;
    }
    a.channel[0][L / 2].r += 0.5f;
    a.channel[nb_rx * nb_tx - 1][0].i -= 0.25f;
  }
  channel_pipeline_channel_free(pc);
}

static std::string cpu_param_name(const ::testing::TestParamInfo<CpuParam> &info)
{
  std::ostringstream name;
  name << (std::get<0>(info.param) == CHANNEL_PIPELINE_METHOD_FFT ? "fft" : "direct") << "_Rx" << std::get<1>(info.param) << "_Tx"
       << std::get<2>(info.param) << "_Samples" << std::get<3>(info.param) << "_ChanLen" << std::get<4>(info.param);
  return name.str();
}

static const auto cpu_methods = ::testing::Values(CHANNEL_PIPELINE_METHOD_DIRECT, CHANNEL_PIPELINE_METHOD_FFT);
static const auto cpu_samples = ::testing::Values(1, 100, 1031, 30720);
static const auto cpu_lengths = ::testing::Values(1, 7, 16, 64, 255);

INSTANTIATE_TEST_SUITE_P(ChannelPipelineCpu,
                         ChannelPipelineCpuTest,
                         ::testing::Combine(cpu_methods,
                                            ::testing::Values(1, 2, 4), // nb_rx
                                            ::testing::Values(1, 3, 4), // nb_tx
                                            cpu_samples,
                                            cpu_lengths),
                         cpu_param_name);

// Asymmetric antenna configurations: 8 gNB antennas against a 2-antenna UE, in both directions.
INSTANTIATE_TEST_SUITE_P(ChannelPipelineCpu8x2,
                         ChannelPipelineCpuTest,
                         ::testing::Combine(cpu_methods, ::testing::Values(8), ::testing::Values(2), cpu_samples, cpu_lengths),
                         cpu_param_name);

INSTANTIATE_TEST_SUITE_P(ChannelPipelineCpu2x8,
                         ChannelPipelineCpuTest,
                         ::testing::Combine(cpu_methods, ::testing::Values(2), ::testing::Values(8), cpu_samples, cpu_lengths),
                         cpu_param_name);

// Noise is added on top of the convolution with the configured standard deviation.
TEST(ChannelPipelineCpuNoise, AddsNoiseOfConfiguredPower)
{
  const float sigma = 10.0f;
  void *tpool = init_tpool(8);
  channel_pipeline_t *pipeline = channel_pipeline_init(sigma);
  for (auto method : {CHANNEL_PIPELINE_METHOD_DIRECT, CHANNEL_PIPELINE_METHOD_FFT}) {
    channel_pipeline_set_method(pipeline, method);
    PipelineIo io(2, 2, 30720, 16, 15, 30720);
    io.reference();
    io.run(pipeline, tpool, sigma);
    double rms = std::sqrt(io.compare().second);
    EXPECT_GT(rms, 0.8 * sigma);
    EXPECT_LT(rms, 1.2 * sigma);
  }
  destroy_tpool(tpool);
  channel_pipeline_shutdown(pipeline);
}

int main(int argc, char **argv)
{
  logInit();
  randominit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
