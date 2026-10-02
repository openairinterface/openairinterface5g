/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include "PHY/TOOLS/tools_defs.h"
#include "sim.h"

//#define DEBUG_CH
//#define DOPPLER_DEBUG

uint8_t multipath_channel_nosigconv(channel_desc_t *desc)
{
  random_channel(desc, 0);
  return (1);
}

void interleave_channel_output(float **rx_sig_re, float **rx_sig_im, float **output_interleaved, int nb_rx, int num_samples)
{
  for (int i = 0; i < nb_rx; i++) {
    for (int j = 0; j < num_samples; j++) {
      output_interleaved[i][2 * j] = rx_sig_re[i][j];
      output_interleaved[i][2 * j + 1] = rx_sig_im[i][j];
    }
  }
}

//#define CHANNEL_SSE

#ifdef CHANNEL_SSE
void __attribute__((no_sanitize_address)) multipath_channel(channel_desc_t *desc,
                                                            double **tx_sig_re,
                                                            double **tx_sig_im,
                                                            double **rx_sig_re,
                                                            double **rx_sig_im,
                                                            uint32_t length,
                                                            uint8_t keep_channel,
                                                            int log_channel)
{
  int i, ii, j, l;
  // int simd_length;
  // int simd_length, tail_start;
  simde__m128d rx_tmp128_re, rx_tmp128_im, tx128_re, tx128_im, ch128_r, ch128_i, path_gain128;

  double path_gain = pow(10, -desc->path_loss_dB / 20);
  uint64_t dd = desc->channel_offset;

  path_gain128 = simde_mm_set1_pd(path_gain);

  if (keep_channel == 0) {
    random_channel(desc, 0);
  }

  // simd_length = (length - dd) / 2;
  // tail_start = simd_length * 2;
  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);

  for (i = 0; i < (int)(length - dd); i += 2) {
    for (ii = 0; ii < desc->nb_rx; ii++) {
      rx_tmp128_re = simde_mm_setzero_pd();
      rx_tmp128_im = simde_mm_setzero_pd();

      for (i = 0; i < (int)(length - dd); i += 2) {
        for (ii = 0; ii < desc->nb_rx; ii++) {
          rx_tmp128_re = simde_mm_setzero_pd();
          rx_tmp128_im = simde_mm_setzero_pd();
          for (j = 0; j < desc->nb_tx; j++) {
            for (l = 0; l < (int)desc->channel_length; l++) {
              double tx_re[2] = {0.0, 0.0}, tx_im[2] = {0.0, 0.0};
              if ((i - l) >= 0)
                tx_re[0] = tx_sig_re[j][i - l];
              if ((i + 1 - l) >= 0)
                tx_re[1] = tx_sig_re[j][i + 1 - l];
              if ((i - l) >= 0)
                tx_im[0] = tx_sig_im[j][i - l];
              if ((i + 1 - l) >= 0)
                tx_im[1] = tx_sig_im[j][i + 1 - l];
              tx128_re = simde_mm_loadu_pd(tx_re);
              tx128_im = simde_mm_loadu_pd(tx_im);
              ch128_r = simde_mm_set1_pd(ch[j][ii][l].r);
              ch128_i = simde_mm_set1_pd(ch[j][ii][l].i);
              rx_tmp128_re =
                  simde_mm_add_pd(rx_tmp128_re,
                                  simde_mm_sub_pd(simde_mm_mul_pd(tx128_re, ch128_r), simde_mm_mul_pd(tx128_im, ch128_i)));
              rx_tmp128_im =
                  simde_mm_add_pd(rx_tmp128_im,
                                  simde_mm_add_pd(simde_mm_mul_pd(tx128_re, ch128_i), simde_mm_mul_pd(tx128_im, ch128_r)));
            }
          }
          simde_mm_storeu_pd(&rx_sig_re[ii][i + dd], simde_mm_mul_pd(rx_tmp128_re, path_gain128));
          simde_mm_storeu_pd(&rx_sig_im[ii][i + dd], simde_mm_mul_pd(rx_tmp128_im, path_gain128));
        }
      }
    } // ii
  } // i

  // Handle the final sample if the length is odd
  if ((length - dd) % 2) {
    int i_tail = length - dd - 1;
    for (ii = 0; ii < desc->nb_rx; ii++) {
      struct complexd rx_tmp = {0};
      for (j = 0; j < desc->nb_tx; j++) {
        struct complexd *chan = ch[j][ii];
        for (l = 0; l < (int)desc->channel_length; l++) {
          if ((i_tail - l) >= 0) {
            struct complexd tx;
            tx.r = tx_sig_re[j][i_tail - l];
            tx.i = tx_sig_im[j][i_tail - l];
            rx_tmp.r += (tx.r * chan[l].r) - (tx.i * chan[l].i);
            rx_tmp.i += (tx.i * chan[l].r) + (tx.r * chan[l].i);
          }
        }
      }
      rx_sig_re[ii][i_tail + dd] = rx_tmp.r * path_gain;
      rx_sig_im[ii][i_tail + dd] = rx_tmp.i * path_gain;
    }
  }
}

#else

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
  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  for (int i = 0; i < ((int)length - dd); i++) {
    for (int ii = 0; ii < desc->nb_rx; ii++) {
      struct complexd rx_tmp = {0};
      for (int j = 0; j < desc->nb_tx; j++) {
        struct complexd *chan = ch[j][ii];
        int end = min(desc->channel_length, i + 1); // taps l <= i only (tx[i - l])
        for (int l = 0; l < end; l++) {
          struct complexd tx = (struct complexd){tx_sig_re[j][i - l], tx_sig_im[j][i - l]};
          rx_tmp.r += tx.r * chan[l].r - tx.i * chan[l].i;
          rx_tmp.i += tx.i * chan[l].r + tx.r * chan[l].i;
#if 0
          if (i==0 && log_channel == 1) {
            printf("channel[%d][%d][%d] = %f dB \t(%e, %e)\n",
                   ii, j, l, 10 * log10(pow(chan[l].r, 2.0) + pow(chan[l].i, 2.0)), chan[l].r, chan[l].i);
	        }
#endif
        } // l
      } // j
#if 0
      if (desc->max_Doppler != 0.0)
        rx_tmp = cdMul(rx_tmp, cexp_doppler[i]);
#endif

#ifdef DOPPLER_DEBUG
      printf("[k %2i] cexp_doppler = (%7.4f, %7.4f), abs(cexp_doppler) = %.4f\n",
             i,
             cexp_doppler[i].r,
             cexp_doppler[i].i,
             sqrt(cexp_doppler[i].r * cexp_doppler[i].r + cexp_doppler[i].i * cexp_doppler[i].i));
#endif

      rx_sig_re[ii][i + dd] = rx_tmp.r * path_gain;
      rx_sig_im[ii][i + dd] = rx_tmp.i * path_gain;
#ifdef DEBUG_CH
      if ((i % 32) == 0) {
        printf("rx aa %d: %f, %f  =>  %e, %e\n", ii, rx_tmp.r, rx_tmp.i, rx_sig_re[ii][i - dd], rx_sig_im[ii][i - dd]);
      }
#endif
      // rx_sig_re[ii][i] = sqrt(.5)*(tx_sig_re[0][i] + tx_sig_re[1][i]);
      // rx_sig_im[ii][i] = sqrt(.5)*(tx_sig_im[0][i] + tx_sig_im[1][i]);

    } // ii
  } // i
}
#endif

#ifdef CHANNEL_SSE
void __attribute__((no_sanitize_address)) multipath_channel_float(channel_desc_t *desc,
                                                                  float **tx_sig_interleaved,
                                                                  float **rx_sig_re,
                                                                  float **rx_sig_im,
                                                                  uint32_t length,
                                                                  uint8_t keep_channel,
                                                                  int log_channel)
{
  int i, ii, j, l;
  simde__m128 rx_tmp128_re, rx_tmp128_im, tx128_re, tx128_im, ch128_r, ch128_i, path_gain128;
  uint64_t dd = desc->channel_offset;

  if (keep_channel == 0) {
    random_channel(desc, 0);
  }

  struct complexd cexp_doppler[length];
  if (desc->max_Doppler != 0.0) {
    get_cexp_doppler(cexp_doppler, desc, length);
  }

  if (dd >= length) {
    return; // No samples to process
  }
  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  for (i = 0; i <= (int)(length - dd) - 4; i += 4) {
    for (ii = 0; ii < desc->nb_rx; ii++) {
      rx_tmp128_re = simde_mm_setzero_ps();
      rx_tmp128_im = simde_mm_setzero_ps();

      for (j = 0; j < desc->nb_tx; j++) {
        for (l = 0; l < (int)desc->channel_length; l++) {
          int idx = i - l;
          if (idx >= 0) {
            // Load 2 complex numbers (4 floats) => {I0, Q0, I1, Q1}
            simde__m128 vec0 = simde_mm_loadu_ps(&tx_sig_interleaved[j][2 * idx]);
            simde__m128 vec1 = simde_mm_loadu_ps(&tx_sig_interleaved[j][2 * (idx + 2)]);
            tx128_re = simde_mm_shuffle_ps(vec0, vec1, SIMDE_MM_SHUFFLE(2, 0, 2, 0)); // {I0,I1,I2,I3}
            tx128_im = simde_mm_shuffle_ps(vec0, vec1, SIMDE_MM_SHUFFLE(3, 1, 3, 1)); // {Q0,Q1,Q2,Q3}
          } else {
            tx128_re = simde_mm_setzero_ps();
            tx128_im = simde_mm_setzero_ps();
          }

          ch128_r = simde_mm_set1_ps((float)ch[j][ii][l].r);
          ch128_i = simde_mm_set1_ps((float)ch[j][ii][l].i);

          // re = (tx_re * ch_r) - (tx_im * ch_i)
          rx_tmp128_re = simde_mm_add_ps(rx_tmp128_re,
                                         simde_mm_sub_ps(simde_mm_mul_ps(tx128_re, ch128_r), simde_mm_mul_ps(tx128_im, ch128_i)));
          // im = (tx_re * ch_i) + (tx_im * ch_r)
          rx_tmp128_im = simde_mm_add_ps(rx_tmp128_im,
                                         simde_mm_add_ps(simde_mm_mul_ps(tx128_re, ch128_i), simde_mm_mul_ps(tx128_im, ch128_r)));
        }
      }

#if 0
            if (desc->max_Doppler != 0.0) {
                float doppler_re[4] = {(float)cexp_doppler[i].r, (float)cexp_doppler[i+1].r, (float)cexp_doppler[i+2].r, (float)cexp_doppler[i+3].r};
                float doppler_im[4] = {(float)cexp_doppler[i].i, (float)cexp_doppler[i+1].i, (float)cexp_doppler[i+2].i, (float)cexp_doppler[i+3].i};
                
                simde__m128 doppler128_r = simde_mm_loadu_ps(doppler_re);
                simde__m128 doppler128_i = simde_mm_loadu_ps(doppler_im);

                simde__m128 temp_re = rx_tmp128_re;
                simde__m128 temp_im = rx_tmp128_im;

                rx_tmp128_re = simde_mm_sub_ps(simde_mm_mul_ps(temp_re, doppler128_r), simde_mm_mul_ps(temp_im, doppler128_i));
                rx_tmp128_im = simde_mm_add_ps(simde_mm_mul_ps(temp_im, doppler128_r), simde_mm_mul_ps(temp_re, doppler128_i));
            }
#endif
      simde_mm_storeu_ps(&rx_sig_re[ii][i + dd], rx_tmp128_re);
      simde_mm_storeu_ps(&rx_sig_im[ii][i + dd], rx_tmp128_im);
    }
  }

  // Handle remaining samples (tail loop) that are not a multiple of 4
  for (; i < (int)(length - dd); i++) {
    for (ii = 0; ii < desc->nb_rx; ii++) {
      struct complexf rx_tmp = {0.0f, 0.0f};
      for (j = 0; j < desc->nb_tx; j++) {
        struct complexd *chan = ch[j][ii];
        for (l = 0; l < (int)desc->channel_length; l++) {
          if ((i - l) >= 0) {
            struct complexf tx;
            tx.r = tx_sig_interleaved[j][2 * (i - l)];
            tx.i = tx_sig_interleaved[j][2 * (i - l) + 1];
            rx_tmp.r += (tx.r * (float)chan[l].r) - (tx.i * (float)chan[l].i);
            rx_tmp.i += (tx.i * (float)chan[l].r) + (tx.r * (float)chan[l].i);
          }
        }
      }
#if 0
            if (desc->max_Doppler != 0.0) {
                struct complexf doppler_factor = {(float)cexp_doppler[i].r, (float)cexp_doppler[i].i};
                struct complexf temp = rx_tmp;
                rx_tmp.r = (temp.r * doppler_factor.r) - (temp.i * doppler_factor.i);
                rx_tmp.i = (temp.i * doppler_factor.r) + (temp.r * doppler_factor.i);
            }
#endif
      rx_sig_re[ii][i + dd] = rx_tmp.r;
      rx_sig_im[ii][i + dd] = rx_tmp.i;
    }
  }
}

#else
void multipath_channel_float(channel_desc_t *desc,
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

  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  for (int i = 0; i < ((int)length - dd); i++) {
    for (int ii = 0; ii < desc->nb_rx; ii++) {
      struct complexf rx_tmp = {0.0f, 0.0f};

      for (int j = 0; j < desc->nb_tx; j++) {
        struct complexd *chan = ch[j][ii];

        for (int l = 0; l < (int)desc->channel_length; l++) {
          if ((i - l) >= 0) {
            struct complexf tx;
            tx.r = tx_sig_interleaved[j][2 * (i - l)];
            tx.i = tx_sig_interleaved[j][2 * (i - l) + 1];
            rx_tmp.r += (tx.r * (float)chan[l].r) - (tx.i * (float)chan[l].i);
            rx_tmp.i += (tx.i * (float)chan[l].r) + (tx.r * (float)chan[l].i);
          }
        } // l (channel_length)
      } // j (nb_tx)

#if 0
            if (desc->max_Doppler != 0.0) {
                // Perform complex multiplication: rx_tmp = rx_tmp * cexp_doppler[i]
                struct complexf doppler_factor = {(float)cexp_doppler[i].r, (float)cexp_doppler[i].i};
                struct complexf temp = rx_tmp;
                rx_tmp.r = (temp.r * doppler_factor.r) - (temp.i * doppler_factor.i);
                rx_tmp.i = (temp.i * doppler_factor.r) + (temp.r * doppler_factor.i);
            }
#endif
      rx_sig_re[ii][i + dd] = rx_tmp.r;
      rx_sig_im[ii][i + dd] = rx_tmp.i;

    } // ii (nb_rx)
  } // i (length)
}

#endif

// \f$\mathbf{y} = y + \mathbf{x}\f$
#ifdef CHANNEL_SSE
void __attribute__((no_sanitize_address)) add_rx_signals(float **y_re,
                                                         float **y_im,
                                                         float **x_re,
                                                         float **x_im,
                                                         int n_rx,
                                                         int length)
{
  for (int aarx = 0; aarx < n_rx; aarx++) {
    int i = 0;
    for (; i <= length - 4; i += 4) {
      // Add real values
      simde__m128 vec_re_x = simde_mm_loadu_ps(&x_re[aarx][i]);
      simde__m128 vec_re_y = simde_mm_loadu_ps(&y_re[aarx][i]);
      simde_mm_storeu_ps(&y_re[aarx][i], simde_mm_add_ps(vec_re_y, vec_re_x));

      // Add imaginary values
      simde__m128 vec_im_x = simde_mm_loadu_ps(&x_im[aarx][i]);
      simde__m128 vec_im_y = simde_mm_loadu_ps(&y_im[aarx][i]);
      simde_mm_storeu_ps(&y_im[aarx][i], simde_mm_add_ps(vec_im_y, vec_im_x));
    }

    // Add remaining samples
    for (; i < length; i++) {
      y_re[aarx][i] += x_re[aarx][i];
      y_im[aarx][i] += x_im[aarx][i];
    }
  }
}
#else
void add_rx_signals(float **y_re, float **y_im, float **x_re, float **x_im, int n_rx, int length)
{
  for (int aarx = 0; aarx < n_rx; aarx++) {
    for (int i = 0; i < length; i++) {
      y_re[aarx][i] += x_re[aarx][i];
      y_im[aarx][i] += x_im[aarx][i];
    }
  }
}
#endif
