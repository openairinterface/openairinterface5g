/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/defs_gNB.h"
#include "PHY/impl_defs_nr.h"
#include "phy_ofdm_mod.h"
#include "common/utils/LOG/log.h"
//#define DEBUG_OFDM_MOD

void nr_normal_prefix_mod(c16_t *txdataF,
                          c16_t *txdata,
                          uint8_t nsymb,
                          const NR_DL_FRAME_PARMS *frame_parms,
                          uint32_t slot,
                          bool was_symbol_used[NR_SYMBOLS_PER_SLOT])
{
  // This function works only slot wise. For more generic symbol generation refer nr_feptx0()
  if (frame_parms->numerology_index != 0) { // case where numerology != 0
    if (!(slot%(frame_parms->slots_per_subframe/2))) {
      if (was_symbol_used[0]) {
        PHY_ofdm_mod((int *)txdataF,
                    (int *)txdata,
                    frame_parms->ofdm_symbol_size,
                    1,
                    frame_parms->nb_prefix_samples0,
                    CYCLIC_PREFIX);
      } else {
        memset(txdata, 0, (frame_parms->nb_prefix_samples0 +  frame_parms->ofdm_symbol_size) * sizeof(c16_t));
      }
      for (int i = 1; i < nsymb; i++) {
        c16_t* tx_data_ptr = txdata + (i - 1) * (frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples) +
                            frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples0;
        if (was_symbol_used[i]) {
          PHY_ofdm_mod((int *)txdataF + frame_parms->ofdm_symbol_size * i,
                      (int *)tx_data_ptr,
                      frame_parms->ofdm_symbol_size,
                      1,
                      frame_parms->nb_prefix_samples,
                      CYCLIC_PREFIX);
        } else {
          memset(tx_data_ptr, 0, (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size) * sizeof(c16_t));
        }
      }
    }
    else {
      for (int i = 0; i < nsymb; i++) {
        c16_t* tx_data_ptr = txdata + i * (frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples);
        if (was_symbol_used[i]) {
          PHY_ofdm_mod((int *)txdataF + frame_parms->ofdm_symbol_size * i,
                      (int *)tx_data_ptr,
                      frame_parms->ofdm_symbol_size,
                      1,
                      frame_parms->nb_prefix_samples,
                      CYCLIC_PREFIX);
        } else {
          memset(tx_data_ptr, 0, (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size) * sizeof(c16_t));
        }
      }
    }
  } else { // numerology = 0, longer CP for symbols 0 and 7
    c16_t *tx_data_ptr = txdata;
    for (int i = 0; i < nsymb; i++) {
      const int cp = (i % 7) == 0 ? frame_parms->nb_prefix_samples0 : frame_parms->nb_prefix_samples;
      if (was_symbol_used[i])
        PHY_ofdm_mod((int *)txdataF + frame_parms->ofdm_symbol_size * i,
                     (int *)tx_data_ptr,
                     frame_parms->ofdm_symbol_size,
                     1,
                     cp,
                     CYCLIC_PREFIX);
      else
        memset(tx_data_ptr, 0, (cp + frame_parms->ofdm_symbol_size) * sizeof(c16_t));
      tx_data_ptr += cp + frame_parms->ofdm_symbol_size;
    }
  }
}

/* txdataF is FFT shifted, i.e. the REs of the carrier are contiguous starting at index 0. */
void apply_nr_rotation_TX(const NR_DL_FRAME_PARMS *fp,
                          c16_t *txdataF,
                          const c16_t *symbol_rotation,
                          int slot,
                          int nb_rb,
                          int first_symbol,
                          int nsymb)
{
  int symb_offset = (slot % fp->slots_per_subframe) * fp->symbols_per_slot;

  symbol_rotation += symb_offset;

  for (int sidx = first_symbol; sidx < first_symbol + nsymb; sidx++) {
    const c16_t this_rotation = symbol_rotation[sidx];
    c16_t *this_symbol = txdataF + sidx * fp->ofdm_symbol_size;

    LOG_D(PHY,"Rotating symbol %d, slot %d, symbol_subframe_index %d (%d,%d)\n",
      sidx,
      slot,
      sidx + symb_offset,
      this_rotation.r,
      this_rotation.i);

    rotate_cpx_vector(this_symbol, this_rotation, this_symbol, nb_rb * NR_NB_SC_PER_RB, 15);
  }
}
                       
void fftshift(const c16_t *in, c16_t *out, int nbins, int fft_size)
{
  const int half = nbins / 2; // half bw
  // negative-freq half -> front
  memcpy(out, in + fft_size - half, half * sizeof(c16_t));
  // dc + positive-freq half -> next
  memcpy(out + half, in, half * sizeof(c16_t));
}

void fftshift_inplace(c16_t *in, int nbins, int fft_size)
{
  const int half = nbins / 2; // half bw

  c16_t tmp[half]; // holds the negative half

  // save negative bins (they sit at the tail: indices N-half .. N-1)
  memcpy(tmp, in + fft_size - half, half * sizeof(c16_t));
  // slide DC + positive (and trailing zeros) to the right by half
  memmove(in + half, in, (fft_size - half) * sizeof(c16_t));
  // place negative bins at the front
  memcpy(in, tmp, half * sizeof(c16_t));
}

void fftshift_inverse(const c16_t *in, c16_t *out, int nbins, int fft_size)
{
  const int half = nbins / 2; // half bw
  // negative-freq half -> back
  memcpy(out + fft_size - half, in, half * sizeof(c16_t));
  // dc + positive-freq half -> front
  memcpy(out, in + half, half * sizeof(c16_t));
}

void fftshift_inverse_inplace(c16_t *in, int nbins, int fft_size)
{
  const int half = nbins / 2; // # negative-freq bins

  c16_t tmp[half]; // holds the negative half

  // save negative bins (they sit at the tail: indices N-half .. N-1)
  memcpy(tmp, in, half * sizeof(c16_t));
  // slide DC + positive (and trailing zeros) to the left by half
  memmove(in, in + half, (fft_size - half) * sizeof(c16_t));
  // place negative bins at the back
  memcpy(in + fft_size - half, tmp, half * sizeof(c16_t));
}
