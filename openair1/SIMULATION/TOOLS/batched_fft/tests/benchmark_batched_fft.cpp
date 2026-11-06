/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// Single-threaded batched_fft throughput. The counter is the time per point of one transform (a call
// transforms `batch` sequences of N points), comparable with OAI's dft().

#include "benchmark/benchmark.h"
#include "openair1/PHY/TOOLS/phy_test_tools.hpp"
#include "batched_fft.h"

static void BM_batched_fft(benchmark::State &state)
{
  const int N = 1 << state.range(0);
  const bool inverse = state.range(1);
  const int batch = BATCHED_FFT_BATCH_SIZE;
  const float *tw = batched_fft_twiddles(N);
  // 64-byte aligned like real callers: std::vector's 16-byte alignment splits cache lines.
  AlignedVector512<float> re((size_t)N * batch), im((size_t)N * batch);
  for (size_t i = 0; i < re.size(); i++) {
    re[i] = (float)(i % 7);
    im[i] = (float)(i % 5);
  }
  for (auto _ : state) {
    if (inverse)
      batched_fft_inverse(re.data(), im.data(), N, tw);
    else
      batched_fft_forward(re.data(), im.data(), N, tw);
    benchmark::DoNotOptimize(re.data());
    benchmark::ClobberMemory();
  }
  state.counters["time_per_point"] =
      benchmark::Counter((double)N * batch * state.iterations(), benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

BENCHMARK(BM_batched_fft)
    ->ArgsProduct({
        {5, 6, 7, 8, 9, 10, 11, 12}, // log2(N)
        {0, 1}, // forward, inverse
    });

BENCHMARK_MAIN();
