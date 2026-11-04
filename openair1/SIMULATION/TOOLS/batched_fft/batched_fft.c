/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// Twiddle tables, shared by the per-ISA kernels (batched_fft_<isa>.c).

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include "batched_fft.h"

// All tables are built once (sum over N of N floats: 32 KB for BATCHED_FFT_MAX_LOG2 = 12).
static float *batched_fft_tw[BATCHED_FFT_MAX_LOG2 + 1];
static pthread_once_t batched_fft_tw_once = PTHREAD_ONCE_INIT;

static void batched_fft_tw_init(void)
{
  for (int log2n = 1; log2n <= BATCHED_FFT_MAX_LOG2; log2n++) {
    const int N = 1 << log2n;
    float *tw = malloc(sizeof(float) * N);
    if (!tw)
      abort();
    for (int k = 0; k < N / 2; k++) {
      tw[k] = (float)cos(-2.0 * M_PI * k / N);
      tw[N / 2 + k] = (float)sin(-2.0 * M_PI * k / N);
    }
    batched_fft_tw[log2n] = tw;
  }
}

const float *batched_fft_twiddles(int N)
{
  if (N < 2 || (N & (N - 1)) || N > (1 << BATCHED_FFT_MAX_LOG2))
    return NULL;
  pthread_once(&batched_fft_tw_once, batched_fft_tw_init);
  return batched_fft_tw[__builtin_ctz(N)];
}
