/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief gNB measurement routines
 */

#include "PHY/defs_gNB.h"
#include "nr_ul_estimation.h"

#define I0_SKIP_DC 1

void nr_est_srs_timing_advance_offset(uint16_t ofdm_symbol_size,
                                      const c16_t srs_estimated_channel_time[][NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                      uint8_t ant,
                                      uint8_t N_ap,
                                      uint32_t samples_per_frame,
                                      uint16_t *ta_offset,
                                      int16_t *ta_offset_nsec)
{
  int64_t mean_val = 0;
  int64_t max_val = 0;
  int32_t max_idx = 0;
  int16_t srs_toa = 0;
  int16_t ofdm_os_size = NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size;
  for (int k = 0; k < ofdm_os_size; k++) {
    int64_t abs_val = 0;
    for (int p_index = 0; p_index < N_ap; p_index++) {
      abs_val += squaredMod(srs_estimated_channel_time[p_index][k]);
    }
    mean_val += abs_val;
    if (abs_val > max_val) {
      max_val = abs_val;
      max_idx = k;
    }
  }
  max_val = max_val / N_ap;
  mean_val = mean_val / (N_ap * ofdm_os_size);

  if (max_idx > ofdm_os_size >> 1)
    max_idx = max_idx - ofdm_os_size;

  // Check for detection threshold
  if ((mean_val != 0) && (max_val / mean_val > NR_SRS_DETECTION_THRESHOLD)) {
    srs_toa = max_idx / NR_SRS_IDFT_OVERSAMP_FACTOR;

    // restrict the computation of ta_offset to antenna 0
    if (ant == 0) {
      // Scale the 16 factor in N_TA calculation in 38.213 section 4.2 according to the used FFT size
      const uint16_t bw_scaling = ofdm_symbol_size >> 7;

      // do some integer rounding to improve TA accuracy
      int sync_pos_rounded;
      if (srs_toa > 0) {
        sync_pos_rounded = srs_toa + (bw_scaling >> 1) - 1;
      } else {
        sync_pos_rounded = srs_toa - (bw_scaling >> 1) + 1;
      }

      *ta_offset = sync_pos_rounded / bw_scaling;

      // put timing advance command in 0..63 range
      *ta_offset = BOUNDED_EVAL(0, *ta_offset + 31, 63);
    }
    *ta_offset_nsec = (max_idx * 1e9) / (NR_SRS_IDFT_OVERSAMP_FACTOR * samples_per_frame * 100);
  } else {
    *ta_offset = 0xFFFF;
    *ta_offset_nsec = 0x8000;
  }

  LOG_D(NR_PHY,
        "SRS estimatd ToA %d [RX ant %d]: TA offset %d, TA offset ns %d (max_val %ld, mean_val %ld, max_idx %d)\n",
        srs_toa,
        ant,
        *ta_offset,
        *ta_offset_nsec,
        max_val,
        mean_val,
        max_idx);
}

void dump_nr_I0_stats(FILE *fd, PHY_VARS_gNB *gNB)
{
  int min_I0 = 1000, max_I0 = 0;
  int amin = 0, amax = 0;
  fprintf(fd, "Blacklisted PRBs %d/%d\n", gNB->num_ulprbbl, gNB->frame_parms.N_RB_UL);
  for (int i = 0; i < gNB->frame_parms.N_RB_UL; i++) {
    if (gNB->ulprbbl[i] > 0)
      continue;
    if (gNB->measurements.n0_subband_power_tot_dB[i] < min_I0) {
      min_I0 = gNB->measurements.n0_subband_power_tot_dB[i];
      amin = i;
    }
    if (gNB->measurements.n0_subband_power_tot_dB[i] > max_I0) {
      max_I0 = gNB->measurements.n0_subband_power_tot_dB[i];
      amax = i;
    }
  }

  for (int i = 0; i < gNB->frame_parms.N_RB_UL; i++) {
    if (gNB->ulprbbl[i] == 0)
      fprintf(fd, "%2d.", gNB->measurements.n0_subband_power_tot_dB[i] - gNB->measurements.n0_subband_power_avg_dB);
    else
      fprintf(fd, " X.");
    if (i % 25 == 24)
      fprintf(fd, "\n");
  }
  fprintf(fd, "\n");
  fprintf(fd,
          "max_IO = %d (%d), min_I0 = %d (%d), avg_I0 = %d dB",
          max_I0,
          amax,
          min_I0,
          amin,
          gNB->measurements.n0_subband_power_avg_dB);
  if (gNB->frame_parms.nb_antennas_rx > 1) {
    fprintf(fd, "(");
    for (int aarx = 0; aarx < gNB->frame_parms.nb_antennas_rx; aarx++)
      fprintf(fd, "%d.", gNB->measurements.n0_subband_power_avg_perANT_dB[aarx]);
    fprintf(fd, ")");
  }
  fprintf(fd, "\nPRACH I0 = %.1f dB\n", gNB->measurements.prach_I0 / 10.0);
}

void gNB_I0_measurements(PHY_VARS_gNB *gNB, int slot, int first_symb, int num_symb, uint32_t rb_mask_ul[14][MAX_BWP_SIZE])
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  NR_gNB_COMMON *common_vars = &gNB->common_vars;
  PHY_MEASUREMENTS_gNB *measurements = &gNB->measurements;
  int nb_symb[MAX_BWP_SIZE] = {0};

  unsigned int tmp_n0_subband[frame_parms->nb_antennas_rx][frame_parms->N_RB_UL];
  memset(tmp_n0_subband, 0, sizeof(tmp_n0_subband));

  /* Noise measurements is done on all spatial streams here. Later these
  measurements are used in estimating SNR of individual signal with their
  corresponding streams.
  */
  for (int s = first_symb; s < first_symb + num_symb; s++) {
    int offset0 = ((slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot + s) * frame_parms->ofdm_symbol_size;
    for (int rb = 0; rb < frame_parms->N_RB_UL; rb++) {
      if (rb_mask_ul[s][rb] == 0 && // check that rb was not used in this subframe
          !(I0_SKIP_DC && rb == frame_parms->N_RB_UL >> 1)) { // skip middle PRB because of artificial noise possibly created by FFT
        int offset = offset0 + rb * NR_NB_SC_PER_RB;
        nb_symb[rb]++;
        for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
          c16_t *ul_ch = &common_vars->rxdataF[aarx][offset];
          int32_t signal_energy = signal_energy_nodc(ul_ch, 12);
          // noise power per antenna/RB, symbols summed up
          tmp_n0_subband[aarx][rb] += signal_energy;
          LOG_D(NR_PHY,"slot %d symbol %d RB %d aarx %d n0_subband_power %d\n", slot, s, rb, aarx, signal_energy);
        } //antenna
      }
    } //rb
  } // symb
  int nb_rb=0;
  int32_t n0_subband_tot_perANT[frame_parms->nb_antennas_rx];
  memset(n0_subband_tot_perANT, 0, sizeof(n0_subband_tot_perANT));

  bool init_meas = measurements->n0_subband_power == NULL;
  allocCast2D(n0_subband_power,
              unsigned int,
              measurements->n0_subband_power,
              frame_parms->nb_antennas_rx,
              frame_parms->N_RB_UL,
              false);

  for (int rb = 0 ; rb<frame_parms->N_RB_UL;rb++) {
    int32_t n0_subband_tot_perPRB=0;
    if (nb_symb[rb] > 0) {
      for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
        tmp_n0_subband[aarx][rb] /= nb_symb[rb];
        // Initialize n0_subband_power from the first measurement before EMA
        if (init_meas)
          n0_subband_power[aarx][rb] = tmp_n0_subband[aarx][rb];
        // apply exponential moving average to smooth noise measurements
        n0_subband_power[aarx][rb] = 0.9 * n0_subband_power[aarx][rb] + 0.1 * tmp_n0_subband[aarx][rb];
        n0_subband_tot_perPRB += n0_subband_power[aarx][rb];
        n0_subband_tot_perANT[aarx] += n0_subband_power[aarx][rb];
      }
      n0_subband_tot_perPRB /= frame_parms->nb_antennas_rx;
      measurements->n0_subband_power_tot_dB[rb] = dB_fixed(n0_subband_tot_perPRB);
      LOG_D(NR_PHY,"n0_subband_power_tot_dB[%d] => %d, over %d symbols\n",rb,measurements->n0_subband_power_tot_dB[rb],nb_symb[rb]);
      nb_rb++;
    }
  }
  if (nb_rb>0) {
    int64_t n0_subband_tot = 0;
    for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
      measurements->n0_subband_power_avg_perANT_dB[aarx] = dB_fixed(n0_subband_tot_perANT[aarx] / nb_rb);
      n0_subband_tot += n0_subband_tot_perANT[aarx];
    }
    measurements->n0_subband_power_avg_dB = dB_fixed(n0_subband_tot / nb_rb);
  }
}
