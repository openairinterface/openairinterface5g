/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <random>
#include <vector>
#include "batched_fft.h"
#include "openair1/PHY/TOOLS/phy_test_tools.hpp"

using cd = std::complex<double>;

// Reference: recursive radix-2 FFT in double, sign -1 forward, +1 inverse (unscaled).
static void ref_fft(std::vector<cd> &x, int sign)
{
  const size_t n = x.size();
  if (n == 1)
    return;
  std::vector<cd> even(n / 2), odd(n / 2);
  for (size_t i = 0; i < n / 2; i++) {
    even[i] = x[2 * i];
    odd[i] = x[2 * i + 1];
  }
  ref_fft(even, sign);
  ref_fft(odd, sign);
  for (size_t k = 0; k < n / 2; k++) {
    cd t = std::polar(1.0, sign * 2.0 * M_PI * k / n) * odd[k];
    x[k] = even[k] + t;
    x[k + n / 2] = even[k] - t;
  }
}

static int bitrev(int v, int log2n)
{
  int r = 0;
  for (int i = 0; i < log2n; i++)
    r |= ((v >> i) & 1) << (log2n - 1 - i);
  return r;
}

TEST(BatchedFftReference, MatchesNaiveDft)
{
  std::mt19937 rng(1);
  std::uniform_real_distribution<double> u(-1, 1);
  for (int n = 2; n <= 64; n *= 2) {
    std::vector<cd> x(n);
    for (auto &v : x)
      v = cd(u(rng), u(rng));
    std::vector<cd> X = x;
    ref_fft(X, -1);
    for (int k = 0; k < n; k++) {
      cd s = 0;
      for (int i = 0; i < n; i++)
        s += x[i] * std::polar(1.0, -2.0 * M_PI * i * k / n);
      EXPECT_LT(std::abs(s - X[k]), 1e-9) << "n=" << n << " k=" << k;
    }
  }
}

TEST(BatchedFftTwiddles, RejectsInvalidSizes)
{
  EXPECT_EQ(batched_fft_twiddles(0), nullptr);
  EXPECT_EQ(batched_fft_twiddles(1), nullptr);
  EXPECT_EQ(batched_fft_twiddles(96), nullptr);
  EXPECT_EQ(batched_fft_twiddles(2 << BATCHED_FFT_MAX_LOG2), nullptr);
  EXPECT_NE(batched_fft_twiddles(2), nullptr);
  EXPECT_NE(batched_fft_twiddles(1 << BATCHED_FFT_MAX_LOG2), nullptr);
}

class BatchedFftTest : public ::testing::TestWithParam<int> {
 protected:
  void SetUp() override
  {
    log2n = GetParam();
    N = 1 << log2n;
    batch = BATCHED_FFT_BATCH_SIZE;
    tw = batched_fft_twiddles(N);
    ASSERT_NE(tw, nullptr);
    re.resize((size_t)N * batch);
    im.resize((size_t)N * batch);
    ref.assign(batch, std::vector<cd>(N));
    std::mt19937 rng(log2n);
    std::uniform_real_distribution<float> u(-1000, 1000);
    for (int b = 0; b < batch; b++) {
      for (int n = 0; n < N; n++) {
        re[(size_t)n * batch + b] = u(rng);
        im[(size_t)n * batch + b] = u(rng);
        ref[b][n] = cd(re[(size_t)n * batch + b], im[(size_t)n * batch + b]);
      }
    }
  }

  // Largest error relative to the RMS of the expected values, over all batch. at(b, p) is the
  // expected value at position p of sequence b.
  template <typename F>
  double rel_error(F at) const
  {
    double err = 0, pow = 0;
    for (int b = 0; b < batch; b++) {
      for (int p = 0; p < N; p++) {
        cd e = at(b, p);
        cd g(re[(size_t)p * batch + b], im[(size_t)p * batch + b]);
        err = std::max(err, std::abs(g - e));
        pow += std::norm(e);
      }
    }
    return err / std::sqrt(pow / (N * batch));
  }

  // Float round-off grows ~log2(N); ~1e-5 relative is well above it and far below
  // anything a wrong twiddle, stage or index would produce.
  double tolerance() const
  {
    return 1e-6 * (log2n + 2);
  }

  int log2n, N, batch;
  const float *tw;
  AlignedVector512<float> re, im;
  std::vector<std::vector<cd>> ref;
};

TEST_P(BatchedFftTest, ForwardIsBitReversedDft)
{
  batched_fft_forward(re.data(), im.data(), N, tw);
  auto X = ref;
  for (auto &x : X)
    ref_fft(x, -1);
  EXPECT_LT(rel_error([&](int b, int p) { return X[b][bitrev(p, log2n)]; }), tolerance());
}

TEST_P(BatchedFftTest, InverseOfBitReversedInputIsIdft)
{
  // Treat the input as a spectrum stored bit-reversed: position p holds Y[bitrev(p)].
  std::vector<std::vector<cd>> Y(batch, std::vector<cd>(N));
  for (int b = 0; b < batch; b++)
    for (int p = 0; p < N; p++)
      Y[b][bitrev(p, log2n)] = ref[b][p];
  batched_fft_inverse(re.data(), im.data(), N, tw);
  for (auto &y : Y)
    ref_fft(y, +1);
  EXPECT_LT(rel_error([&](int b, int p) { return Y[b][p]; }), tolerance());
}

TEST_P(BatchedFftTest, RoundTripScalesByN)
{
  batched_fft_forward(re.data(), im.data(), N, tw);
  batched_fft_inverse(re.data(), im.data(), N, tw);
  EXPECT_LT(rel_error([&](int b, int p) { return ref[b][p] * (double)N; }), tolerance());
}

INSTANTIATE_TEST_SUITE_P(BatchedFft,
                         BatchedFftTest,
                         ::testing::Range(1, BATCHED_FFT_MAX_LOG2 + 1),
                         [](const ::testing::TestParamInfo<int> &info) { return "N" + std::to_string(1 << info.param); });

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
