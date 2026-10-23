/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include "PHY/TOOLS/tools_defs.h"
#include "sim.h"

#if defined(__AVX512F__)
#include <simde/x86/avx512/add.h>
#include <simde/x86/avx512/fmadd.h>
#include <simde/x86/avx512/fnmadd.h>
#include <simde/x86/avx512/mul.h>
#include <simde/x86/avx512/permutex2var.h>
#include <simde/x86/avx512/permutexvar.h>
#include <simde/x86/avx512/setzero.h>
#include <simde/x86/avx512/xor.h>
#include <simde/x86/avx512/extract.h>
#include <simde/x86/avx512/cast.h>
#endif
#if defined(__AVX2__)
#include <simde/x86/avx2.h>
#include <simde/x86/fma.h>
#endif

/* SIMD loops: AVX512 on blocks of its width, then AVX2 on what remains, then scalar code on the tail.
 * VEC_ALIGN: largest block (in output samples) of the vector loops of the convolutions, the scalar head
 * stops on a multiple of it so that the vector loops can use "i < (n & ~(width - 1))" */
#if defined(__AVX512F__)
#define VEC_ALIGN 16
#elif defined(__AVX2__)
#define VEC_ALIGN 8
#else
#define VEC_ALIGN 1
#endif

//#define DEBUG_CH
//#define DOPPLER_DEBUG

/* The convolutions are vectorized over the output samples: a block of consecutive samples is computed
 * for all the taps. The first samples (at least channel_length - 1: taps before the first TX sample, up to
 * a multiple of VEC_ALIGN) and the samples after the last full block are computed by the scalar code. */

uint8_t multipath_channel_nosigconv(channel_desc_t *desc)
{
  random_channel(desc, 0);
  return (1);
}

void interleave_channel_output(float **rx_sig_re, float **rx_sig_im, float **output_interleaved, int nb_rx, int num_samples)
{
  for (int i = 0; i < nb_rx; i++) {
    const float *re = rx_sig_re[i], *im = rx_sig_im[i];
    float *out = output_interleaved[i];
    int j = 0;
#if defined(__AVX512F__)
    const simde__m512i lo = simde_mm512_set_epi32(23, 7, 22, 6, 21, 5, 20, 4, 19, 3, 18, 2, 17, 1, 16, 0);
    const simde__m512i hi = simde_mm512_set_epi32(31, 15, 30, 14, 29, 13, 28, 12, 27, 11, 26, 10, 25, 9, 24, 8);
    for (; j < (num_samples & ~15); j += 16) {
      const simde__m512 r = simde_mm512_loadu_ps(re + j), m = simde_mm512_loadu_ps(im + j);
      simde_mm512_storeu_ps(out + 2 * j, simde_mm512_permutex2var_ps(r, lo, m));
      simde_mm512_storeu_ps(out + 2 * j + 16, simde_mm512_permutex2var_ps(r, hi, m));
    }
#endif
#if defined(__AVX2__)
    for (; j < (num_samples & ~7); j += 8) {
      const simde__m256 r = simde_mm256_loadu_ps(re + j), m = simde_mm256_loadu_ps(im + j);
      const simde__m256 l = simde_mm256_unpacklo_ps(r, m); // r0 i0 r1 i1 | r4 i4 r5 i5
      const simde__m256 h = simde_mm256_unpackhi_ps(r, m); // r2 i2 r3 i3 | r6 i6 r7 i7
      simde_mm256_storeu_ps(out + 2 * j, simde_mm256_permute2f128_ps(l, h, 0x20));
      simde_mm256_storeu_ps(out + 2 * j + 8, simde_mm256_permute2f128_ps(l, h, 0x31));
    }
#endif
    for (; j < num_samples; j++) {
      out[2 * j] = re[j];
      out[2 * j + 1] = im[j];
    }
  }
}

/* one output sample i of the double precision convolution, only the taps l <= i (tx[i - l]) */
static inline struct complexd mp_sample_d(struct complexd *const *chans, int nb_tx, int len, double **tx_re, double **tx_im, int i)
{
  struct complexd rx_tmp = {0};
  const int end = min(len, i + 1);
  for (int j = 0; j < nb_tx; j++) {
    const struct complexd *chan = chans[j];
    for (int l = 0; l < end; l++) {
      const struct complexd tx = {tx_re[j][i - l], tx_im[j][i - l]};
      rx_tmp.r += tx.r * chan[l].r - tx.i * chan[l].i;
      rx_tmp.i += tx.i * chan[l].r + tx.r * chan[l].i;
    }
  }
  return rx_tmp;
}

void __attribute__((no_sanitize_address)) multipath_channel(channel_desc_t *desc,
                                                            double **tx_sig_re,
                                                            double **tx_sig_im,
                                                            double **rx_sig_re,
                                                            double **rx_sig_im,
                                                            uint32_t length,
                                                            uint8_t keep_channel,
                                                            int log_channel)
{
  double path_gain = pow(10, -desc->path_loss_dB / 20);
  uint64_t dd = desc->channel_offset;

#ifdef DEBUG_CH
  printf("[CHANNEL] keep = %d : path_gain = %g (path_loss_dB %f), nb_rx %d, nb_tx %d, dd %lu, len %d \n",
         keep_channel,
         path_gain,
         desc->path_loss_dB,
         desc->nb_rx,
         desc->nb_tx,
         dd,
         desc->channel_length);
#endif

  if (keep_channel) {
    // do nothing - keep channel
  } else {
    random_channel(desc, 0);
  }

  struct complexd cexp_doppler[length];
  if (desc->max_Doppler != 0.0) {
    get_cexp_doppler(cexp_doppler, desc, length);
  }
  if (dd >= length)
    return; // no output sample
  const int n_out = (int)(length - dd);
  const int len = desc->channel_length;
  const int nb_tx = desc->nb_tx;
  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  for (int ii = 0; ii < desc->nb_rx; ii++) {
    struct complexd *chans[nb_tx];
    for (int j = 0; j < nb_tx; j++)
      chans[j] = ch[j][ii];
    double *out_re = rx_sig_re[ii] + dd, *out_im = rx_sig_im[ii] + dd;
    int i = 0;
    // samples with taps before the first TX sample
    for (; i < min((len - 1 + VEC_ALIGN - 1) & ~(VEC_ALIGN - 1), n_out); i++) {
      const struct complexd rx_tmp = mp_sample_d(chans, nb_tx, len, tx_sig_re, tx_sig_im, i);
      out_re[i] = rx_tmp.r * path_gain;
      out_im[i] = rx_tmp.i * path_gain;
    }
#if defined(__AVX512F__)
    const simde__m512d gain = simde_mm512_set1_pd(path_gain);
    for (; i < (n_out & ~7); i += 8) {
      simde__m512d acc_re = simde_mm512_setzero_pd(), acc_im = simde_mm512_setzero_pd();
      for (int j = 0; j < nb_tx; j++) {
        const double *tr = tx_sig_re[j] + i, *ti = tx_sig_im[j] + i;
        for (int l = 0; l < len; l++) {
          const simde__m512d xr = simde_mm512_loadu_pd(tr - l), xi = simde_mm512_loadu_pd(ti - l);
          const simde__m512d cr = simde_mm512_set1_pd(chans[j][l].r), ci = simde_mm512_set1_pd(chans[j][l].i);
          acc_re = simde_mm512_fnmadd_pd(xi, ci, simde_mm512_fmadd_pd(xr, cr, acc_re));
          acc_im = simde_mm512_fmadd_pd(xr, ci, simde_mm512_fmadd_pd(xi, cr, acc_im));
        }
      }
      simde_mm512_storeu_pd(out_re + i, simde_mm512_mul_pd(acc_re, gain));
      simde_mm512_storeu_pd(out_im + i, simde_mm512_mul_pd(acc_im, gain));
    }
#endif
#if defined(__AVX2__)
    const simde__m256d gain2 = simde_mm256_set1_pd(path_gain);
    for (; i < (n_out & ~3); i += 4) {
      simde__m256d acc_re = simde_mm256_setzero_pd(), acc_im = simde_mm256_setzero_pd();
      for (int j = 0; j < nb_tx; j++) {
        const double *tr = tx_sig_re[j] + i, *ti = tx_sig_im[j] + i;
        for (int l = 0; l < len; l++) {
          const simde__m256d xr = simde_mm256_loadu_pd(tr - l), xi = simde_mm256_loadu_pd(ti - l);
          const simde__m256d cr = simde_mm256_set1_pd(chans[j][l].r), ci = simde_mm256_set1_pd(chans[j][l].i);
          acc_re = simde_mm256_fnmadd_pd(xi, ci, simde_mm256_fmadd_pd(xr, cr, acc_re));
          acc_im = simde_mm256_fmadd_pd(xr, ci, simde_mm256_fmadd_pd(xi, cr, acc_im));
        }
      }
      simde_mm256_storeu_pd(out_re + i, simde_mm256_mul_pd(acc_re, gain2));
      simde_mm256_storeu_pd(out_im + i, simde_mm256_mul_pd(acc_im, gain2));
    }
#endif
    for (; i < n_out; i++) {
      const struct complexd rx_tmp = mp_sample_d(chans, nb_tx, len, tx_sig_re, tx_sig_im, i);
#ifdef DOPPLER_DEBUG
      printf("[k %2i] cexp_doppler = (%7.4f, %7.4f), abs(cexp_doppler) = %.4f\n",
             i,
             cexp_doppler[i].r,
             cexp_doppler[i].i,
             sqrt(cexp_doppler[i].r * cexp_doppler[i].r + cexp_doppler[i].i * cexp_doppler[i].i));
#endif
      out_re[i] = rx_tmp.r * path_gain;
      out_im[i] = rx_tmp.i * path_gain;
#ifdef DEBUG_CH
      if ((i % 32) == 0) {
        printf("rx aa %d: %f, %f  =>  %e, %e\n", ii, rx_tmp.r, rx_tmp.i, out_re[i], out_im[i]);
      }
#endif
    }
  } // ii
}

/* one output sample i of the single precision convolution on interleaved TX samples, only the taps l <= i */
static inline struct complexf mp_sample_f(const struct complexf *const *chans, int nb_tx, int len, float **tx, int i)
{
  struct complexf rx_tmp = {0.0f, 0.0f};
  const int end = min(len, i + 1);
  for (int j = 0; j < nb_tx; j++) {
    const struct complexf *chan = chans[j];
    for (int l = 0; l < end; l++) {
      const struct complexf x = {tx[j][2 * (i - l)], tx[j][2 * (i - l) + 1]};
      rx_tmp.r += (x.r * chan[l].r) - (x.i * chan[l].i);
      rx_tmp.i += (x.i * chan[l].r) + (x.r * chan[l].i);
    }
  }
  return rx_tmp;
}

void __attribute__((no_sanitize_address)) multipath_channel_float(channel_desc_t *desc,
                                                                  float **tx_sig_interleaved,
                                                                  float **rx_sig_re,
                                                                  float **rx_sig_im,
                                                                  uint32_t length,
                                                                  uint8_t keep_channel,
                                                                  int log_channel)
{
  uint64_t dd = desc->channel_offset;

  if (keep_channel == 0) {
    random_channel(desc, 0);
  }

  struct complexd cexp_doppler[length];
  if (desc->max_Doppler != 0.0) {
    get_cexp_doppler(cexp_doppler, desc, length);
  }

  if (dd >= length) {
    return;
  }

  const int n_out = (int)(length - dd);
  const int len = desc->channel_length;
  const int nb_tx = desc->nb_tx;
  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  // single precision copy of the taps of one RX antenna
  struct complexf taps[nb_tx][len];
  for (int ii = 0; ii < desc->nb_rx; ii++) {
    const struct complexf *chans[nb_tx];
    for (int j = 0; j < nb_tx; j++) {
      for (int l = 0; l < len; l++)
        taps[j][l] = (struct complexf){(float)ch[j][ii][l].r, (float)ch[j][ii][l].i};
      chans[j] = taps[j];
    }
    float *out_re = rx_sig_re[ii] + dd, *out_im = rx_sig_im[ii] + dd;
    int i = 0;
    for (; i < min((len - 1 + VEC_ALIGN - 1) & ~(VEC_ALIGN - 1), n_out); i++) {
      const struct complexf rx_tmp = mp_sample_f(chans, nb_tx, len, tx_sig_interleaved, i);
      out_re[i] = rx_tmp.r;
      out_im[i] = rx_tmp.i;
    }
    /* Accumulate on interleaved samples {re, im, re, im...}: x * c = x * c.r + swap(x) * {-c.i, c.i}.
     * Two accumulators of consecutive samples, deinterleaved once per block. */
#if defined(__AVX512F__)
    const simde__m512 neg_even = simde_mm512_castsi512_ps(simde_mm512_set_epi32(0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN,
                                                                                0,
                                                                                INT32_MIN));
    const simde__m512i deint = simde_mm512_set_epi32(15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0);
    for (; i < (n_out & ~15); i += 16) {
      simde__m512 acc0 = simde_mm512_setzero_ps(), acc1 = simde_mm512_setzero_ps();
      for (int j = 0; j < nb_tx; j++) {
        const float *x = tx_sig_interleaved[j] + 2 * i;
        for (int l = 0; l < len; l++) {
          const simde__m512 cr = simde_mm512_set1_ps(chans[j][l].r);
          const simde__m512 ci = simde_mm512_xor_ps(simde_mm512_set1_ps(chans[j][l].i), neg_even);
          const simde__m512 x0 = simde_mm512_loadu_ps(x - 2 * l), x1 = simde_mm512_loadu_ps(x - 2 * l + 16);
          acc0 = simde_mm512_fmadd_ps(simde_mm512_shuffle_ps(x0, x0, 0xB1), ci, simde_mm512_fmadd_ps(x0, cr, acc0));
          acc1 = simde_mm512_fmadd_ps(simde_mm512_shuffle_ps(x1, x1, 0xB1), ci, simde_mm512_fmadd_ps(x1, cr, acc1));
        }
      }
      // {r0 i0 ... r7 i7} -> {r0..r7 | i0..i7}
      const simde__m512 d0 = simde_mm512_permutexvar_ps(deint, acc0), d1 = simde_mm512_permutexvar_ps(deint, acc1);
      simde_mm256_storeu_ps(out_re + i, simde_mm512_castps512_ps256(d0));
      simde_mm256_storeu_ps(out_im + i, simde_mm512_extractf32x8_ps(d0, 1));
      simde_mm256_storeu_ps(out_re + i + 8, simde_mm512_castps512_ps256(d1));
      simde_mm256_storeu_ps(out_im + i + 8, simde_mm512_extractf32x8_ps(d1, 1));
    }
#endif
#if defined(__AVX2__)
    const simde__m256 neg_even2 =
        simde_mm256_castsi256_ps(simde_mm256_set_epi32(0, INT32_MIN, 0, INT32_MIN, 0, INT32_MIN, 0, INT32_MIN));
    const simde__m256i deint2 = simde_mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);
    for (; i < (n_out & ~7); i += 8) {
      simde__m256 acc0 = simde_mm256_setzero_ps(), acc1 = simde_mm256_setzero_ps();
      for (int j = 0; j < nb_tx; j++) {
        const float *x = tx_sig_interleaved[j] + 2 * i;
        for (int l = 0; l < len; l++) {
          const simde__m256 cr = simde_mm256_set1_ps(chans[j][l].r);
          const simde__m256 ci = simde_mm256_xor_ps(simde_mm256_set1_ps(chans[j][l].i), neg_even2);
          const simde__m256 x0 = simde_mm256_loadu_ps(x - 2 * l), x1 = simde_mm256_loadu_ps(x - 2 * l + 8);
          acc0 = simde_mm256_fmadd_ps(simde_mm256_shuffle_ps(x0, x0, 0xB1), ci, simde_mm256_fmadd_ps(x0, cr, acc0));
          acc1 = simde_mm256_fmadd_ps(simde_mm256_shuffle_ps(x1, x1, 0xB1), ci, simde_mm256_fmadd_ps(x1, cr, acc1));
        }
      }
      // {r0 i0 ... r3 i3} -> {r0..r3 | i0..i3}
      const simde__m256 d0 = simde_mm256_permutevar8x32_ps(acc0, deint2), d1 = simde_mm256_permutevar8x32_ps(acc1, deint2);
      simde_mm_storeu_ps(out_re + i, simde_mm256_castps256_ps128(d0));
      simde_mm_storeu_ps(out_im + i, simde_mm256_extractf128_ps(d0, 1));
      simde_mm_storeu_ps(out_re + i + 4, simde_mm256_castps256_ps128(d1));
      simde_mm_storeu_ps(out_im + i + 4, simde_mm256_extractf128_ps(d1, 1));
    }
#endif
    for (; i < n_out; i++) {
      const struct complexf rx_tmp = mp_sample_f(chans, nb_tx, len, tx_sig_interleaved, i);
      out_re[i] = rx_tmp.r;
      out_im[i] = rx_tmp.i;
    }
  } // ii (nb_rx)
}

// \f$\mathbf{y} = y + \mathbf{x}\f$
void add_rx_signals(float **y_re, float **y_im, float **x_re, float **x_im, int n_rx, int length)
{
  for (int aarx = 0; aarx < n_rx; aarx++) {
    int i = 0;
#if defined(__AVX512F__)
    for (; i < (length & ~15); i += 16) {
      simde_mm512_storeu_ps(&y_re[aarx][i],
                            simde_mm512_add_ps(simde_mm512_loadu_ps(&y_re[aarx][i]), simde_mm512_loadu_ps(&x_re[aarx][i])));
      simde_mm512_storeu_ps(&y_im[aarx][i],
                            simde_mm512_add_ps(simde_mm512_loadu_ps(&y_im[aarx][i]), simde_mm512_loadu_ps(&x_im[aarx][i])));
    }
#endif
#if defined(__AVX2__)
    for (; i < (length & ~7); i += 8) {
      simde_mm256_storeu_ps(&y_re[aarx][i],
                            simde_mm256_add_ps(simde_mm256_loadu_ps(&y_re[aarx][i]), simde_mm256_loadu_ps(&x_re[aarx][i])));
      simde_mm256_storeu_ps(&y_im[aarx][i],
                            simde_mm256_add_ps(simde_mm256_loadu_ps(&y_im[aarx][i]), simde_mm256_loadu_ps(&x_im[aarx][i])));
    }
#endif
    for (; i < length; i++) {
      y_re[aarx][i] += x_re[aarx][i];
      y_im[aarx][i] += x_im[aarx][i];
    }
  }
}
