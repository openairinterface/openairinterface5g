/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "assertions.h"
#include "nr_common.h"
#include "platform_types.h"
#include <stdint.h>
#include <string.h>
#include "nr_estimation.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_REFSIG/nr_mod_table.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "nr_phy_common.h"
#include "filt16a_32.h"
#include "T.h"
#include <openair1/PHY/TOOLS/phy_scope_interface.h>
#include "nfapi/open-nFAPI/nfapi/public_inc/nfapi_nr_interface.h"
#include "instrumentation.h"
// #define DEBUG_PDSCH
// #define DEBUG_PDCCH
// #define DEBUG_PBCH(a...) printf(a)
#define DEBUG_PBCH(a...)
//#define DEBUG_PRS_CHEST   // To enable PRS Matlab dumps
//#define DEBUG_PRS_PRINTS  // To enable PRS channel estimation debug logs

#define CH_INTERP 0
#define NO_INTERP 1

/* Generic function to find the peak of channel estimation buffer */
void peak_estimator(c16_t *buffer, int32_t buf_len, int32_t *peak_idx, int32_t *peak_val, int32_t mean_val)
{
  int32_t max_val = 0, max_idx = 0, abs_val = 0;
  for(int k = 0; k < buf_len; k++)
  {
    abs_val = squaredMod(buffer[k]);
    if(abs_val > max_val)
    {
      max_val = abs_val;
      max_idx = k;
    }
  }

  // Check for detection threshold
  LOG_D(PHY, "PRS ToA estimator: max_val %d, mean_val %d, max_idx %d\n", max_val, mean_val, max_idx);
  if ((mean_val != 0) && (max_val / mean_val > 10)) {
    *peak_val = max_val;
    *peak_idx = max_idx;
  } else {
    *peak_val = 0;
    *peak_idx = 0;
  }
}

void set_prs_dl_toa(prs_meas_t *prs_meas, float dl_toa)
{
  int rc = pthread_mutex_lock(&prs_meas->dl_toa_mtx);
  AssertFatal(rc == 0, "pthread_mutex_lock() failed: errno %d, %s\n", errno, strerror(errno));
  *prs_meas->next_dl_toa++ = dl_toa;
  if (prs_meas->next_dl_toa >= prs_meas->dl_toa + sizeofArray(prs_meas->dl_toa))
    prs_meas->next_dl_toa = prs_meas->dl_toa;
  LOG_D(NR_PHY, "next_dl_toa %p\n", prs_meas->next_dl_toa);
  rc = pthread_mutex_unlock(&prs_meas->dl_toa_mtx);
  AssertFatal(rc == 0, "pthread_mutex_unlock() failed: errno %d, %s\n", errno, strerror(errno));
}

float get_prs_max_dl_toa(prs_meas_t *prs_meas)
{
  float max = 0.0f;
  int rc = pthread_mutex_lock(&prs_meas->dl_toa_mtx);
  AssertFatal(rc == 0, "pthread_mutex_lock() failed: errno %d, %s\n", errno, strerror(errno));
  for (int i = 0; i < sizeofArray(prs_meas->dl_toa); ++i) {
    max = cmax(max, prs_meas->dl_toa[i]);
    LOG_I(NR_PHY, "i %d dl_toa %.3f max %.3f\n", i, prs_meas->dl_toa[i], max);
  }
  rc = pthread_mutex_unlock(&prs_meas->dl_toa_mtx);
  AssertFatal(rc == 0, "pthread_mutex_unlock() failed: errno %d, %s\n", errno, strerror(errno));
  return max;
}

int nr_prs_channel_estimation(uint8_t gNB_id,
                              uint8_t rsc_id,
                              uint8_t rep_num,
                              PHY_VARS_NR_UE *ue,
                              const UE_nr_rxtx_proc_t *proc,
                              c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP])
{
  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  const int symb_sz = frame_parms->ofdm_symbol_size;
  prs_config_t *prs_cfg  = &ue->prs_vars[gNB_id]->prs_resource[rsc_id].prs_cfg;
  const int CombSize = prs_cfg->CombSize;
  prs_meas_t **prs_meas  = ue->prs_vars[gNB_id]->prs_resource[rsc_id].prs_meas;
  c16_t ch_tmp_buf[symb_sz] __attribute__((aligned(32)));
  memset(ch_tmp_buf, 0, sizeof(ch_tmp_buf));

  int slot_prs =
      (proc->nr_slot_rx - rep_num * prs_cfg->PRSResourceTimeGap + frame_parms->slots_per_frame) % frame_parms->slots_per_frame;

  const int16_t *fl, *fm, *fmm, *fml, *fmr, *fr;
#ifdef DEBUG_PRS_CHEST
  char filename[64] = {0}, varname[64] = {0};
#endif
  const int scale_factor = (1.0f / (float)(prs_cfg->NumPRSSymbols)) * (1 << 15);
  const int num_pilots = (NR_NB_SC_PER_RB / CombSize) * prs_cfg->NumRB;

  for (int l = prs_cfg->SymbolStart; l < prs_cfg->SymbolStart + prs_cfg->NumPRSSymbols; l++) {
    c16_t *ch_tmp = ch_tmp_buf;
    uint32_t *gold_prs = nr_gold_prs(ue->prs_vars[gNB_id]->prs_resource[rsc_id].prs_cfg.NPRSID, slot_prs, l);
    int symInd = l - prs_cfg->SymbolStart;
    int16_t k_prime = 0;
    switch (prs_cfg->CombSize) {
      case 2:
        k_prime = k_prime_table[0][symInd];
        break;
      case 4:
        k_prime = k_prime_table[1][symInd];
        break;
      case 6:
        k_prime = k_prime_table[2][symInd];
        break;
      case 12:
        k_prime = k_prime_table[3][symInd];
        break;
      default:
        AssertFatal(false, "prs_cfg->CombSize = %d\n", CombSize);
    }

#ifdef DEBUG_PRS_PRINTS
    printf(
        "[gNB %d][rsc %d] PRS config l %d k_prime %d:\nprs_cfg->SymbolStart %d\nprs_cfg->NumPRSSymbols %d\nprs_cfg->NumRB "
        "%d\nprs_cfg->CombSize %d\n",
        gNB_id,
        rsc_id,
        l,
        k_prime,
        prs_cfg->SymbolStart,
        prs_cfg->NumPRSSymbols,
        prs_cfg->NumRB,
        CombSize);
#endif
    // Pilots generation and modulation

    AssertFatal(num_pilots > 0, "num_pilots needs to be gt 0 or mod_prs[0] UB");
    c16_t mod_prs[NR_MAX_PRS_LENGTH];
    for (int m = 0; m < num_pilots; m++) {
      int idx = (((gold_prs[(m << 1) >> 5]) >> ((m << 1) & 0x1f)) & 3);
      mod_prs[m] = nr_qpsk_mod_table[idx];
    }

    for (int rxAnt = 0; rxAnt < frame_parms->nb_antennas_rx; rxAnt++) {
      // we sum all antennas
      ch_tmp = (c16_t *)ch_tmp_buf;
      int snr = 0;
      int rsrp = 0;

      // calculate RE offset
      int k = (prs_cfg->REOffset + k_prime) % CombSize + prs_cfg->RBOffset * NR_NB_SC_PER_RB;

      // Channel estimation and interpolation
      c16_t *pil = (c16_t *)mod_prs;
      c16_t *rxF = rxdataF[rxAnt] + l * symb_sz;

      if(prs_cfg->CombSize == 2)
      {
        // Choose the interpolation filters
        switch (k_prime) {
          case 0:
            fl  = filt8_l0;
            fml = filt8_m0;
            fmm = filt8_mm0;
            fmr = filt8_mr0;
            fm  = filt8_m0;
            fr  = filt8_r0;
            break;

          case 1:
            fl  = filt8_l1;
            fmm = filt8_mm1;
            fml = filt8_ml1;
            fmr = fmm;
            fm  = filt8_m1;
            fr  = filt8_r1;
            break;

          default:
            LOG_I(PHY, "%s: ERROR!! Invalid k_prime=%d for PRS comb_size %d, symbol %d\n", __FUNCTION__, k_prime, CombSize, l);
            return(-1);
            break;
        }

        //Start pilot
        c16_t ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fl, ch, ch_tmp, 8);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        c16_t noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
#ifdef DEBUG_PRS_PRINTS
        printf("[Rx %d] pilot %3d, SNR %+2d dB: rxF - > (%+3d, %+3d) addr %p  ch -> (%+3d, %+3d), pil -> (%+d, %+d) \n", rxAnt, 0, snr, rxF[0],rxF[1],&rxF[0],ch[0],ch[1],pil[0],pil[1]);
#endif
        pil++;
        k += CombSize;
        //Middle pilots
        for (int pIdx = 1; pIdx < num_pilots - 1; pIdx += 2) {
          c16_t ch = c16MulConjShift(*pil, rxF[k], 15);
          multadd_real_vector_complex_scalar(pIdx == 1 ? fml : fm, ch, ch_tmp, 8);

          // SNR & RSRP estimation
          rsrp += squaredMod(rxF[k]);
          c16_t noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
          snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
          pil++;
          k += CombSize;
          ch = c16MulConjShift(*pil, rxF[k], 15);
          multadd_real_vector_complex_scalar(pIdx == (num_pilots - 3) ? fmr : fmm, ch, ch_tmp, 8);

          // SNR & RSRP estimation
          rsrp += squaredMod(rxF[k]);
          noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
          snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
          pil++;
          k += CombSize;
          ch_tmp += 4;
        }

        //End pilot
        ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fr, ch, ch_tmp, 8);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
      }
      else if(prs_cfg->CombSize == 4)
      {
        // Choose the interpolation filters
        switch (k_prime) {
          case 0:
            fl = filt16a_l0;
            fml = filt16a_mm0;
            fmm = filt16a_mm0;
            fmr = filt16a_m0;
            fm = filt16a_m0;
            fr = filt16a_r0;
            break;

          case 1:
            fl = filt16a_l1;
            fml = filt16a_ml1;
            fmm = filt16a_mm1;
            fmr = filt16a_mr1;
            fm = filt16a_m1;
            fr = filt16a_r1;
            break;

          case 2:
            fl = filt16a_l2;
            fml = filt16a_ml2;
            fmm = filt16a_mm2;
            fmr = filt16a_mr2;
            fm = filt16a_m2;
            fr = filt16a_r2;
            break;

          case 3:
            fl = filt16a_l3;
            fml = filt16a_ml3;
            fmm = filt16a_mm3;
            fmr = filt16a_mm3;
            fm = filt16a_m3;
            fr = filt16a_r3;
            break;

          default:
            LOG_I(PHY, "%s: ERROR!! Invalid k_prime=%d for PRS comb_size %d, symbol %d\n", __FUNCTION__, k_prime, CombSize, l);
            return(-1);
            break;
        }

        //Start pilot
        c16_t ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fl, ch, ch_tmp, 16);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        c16_t noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
        pil++;
        k += CombSize;
        ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fml, ch, ch_tmp, 16);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
        pil++;
        k += CombSize;
        ch_tmp += 4;

        //Middle pilots
        for (int pIdx = 2; pIdx < num_pilots - 2; pIdx++) {
          c16_t ch = c16MulConjShift(*pil, rxF[k], 15);
          multadd_real_vector_complex_scalar(fmm, ch, ch_tmp, 16);

          // SNR & RSRP estimation
          rsrp += squaredMod(rxF[k]);
          c16_t noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
          snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
          pil++;
          k += CombSize;
          ch_tmp += 4;
        }

        //End pilot
        ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fmr, ch, ch_tmp, 16);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
        pil++;
        k += CombSize;
        ch = c16MulConjShift(*pil, rxF[k], 15);
        multadd_real_vector_complex_scalar(fr, ch, ch_tmp, 16);

        // SNR & RSRP estimation
        rsrp += squaredMod(rxF[k]);
        noiseFig = c16sub(rxF[k], c16mulShift(ch, *pil, 15));
        snr += 10 * log10(squaredMod(rxF[k]) - squaredMod(noiseFig)) - 10 * log10(squaredMod(noiseFig));
      }
      else
      {
        AssertFatal((prs_cfg->CombSize == 2)||(prs_cfg->CombSize == 4), "[%s] DL PRS CombSize other than 2 and 4 are NOT supported currently. Exiting!!!", __FUNCTION__);
      }

      // average out the SNR and RSRP computed
      prs_meas[rxAnt]->snr = snr / (float)num_pilots;
      prs_meas[rxAnt]->rsrp = rsrp / (float)num_pilots;
    } // for rxAnt
  } // for l

  for (int rxAnt = 0; rxAnt < frame_parms->nb_antennas_rx; rxAnt++) {
    // scale by averaging factor 1/NumPrsSymbols
    mult_complex_vector_real_scalar(ch_tmp_buf, scale_factor, ch_tmp_buf, symb_sz);

#ifdef DEBUG_PRS_PRINTS
    for (int rb = 0; rb < prs_cfg->NumRB; rb++)
    {
      printf("================================================================\n");
      printf("\t\t\t[gNB %d][Rx %d][RB %d]\n", gNB_id, rxAnt, rb);
      printf("================================================================\n");
      idx = (12*rb)<<1;
      printf("%4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d\n", ch_tmp[idx], ch_tmp[idx+1], ch_tmp[idx+2], ch_tmp[idx+3], ch_tmp[idx+4], ch_tmp[idx+5], ch_tmp[idx+6], ch_tmp[idx+7], ch_tmp[idx+8], ch_tmp[idx+9], ch_tmp[idx+10], ch_tmp[idx+11]);
      printf("%4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d  %4d %4d\n", ch_tmp[idx+12], ch_tmp[idx+13], ch_tmp[idx+14], ch_tmp[idx+15], ch_tmp[idx+16], ch_tmp[idx+17], ch_tmp[idx+18], ch_tmp[idx+19], ch_tmp[idx+20], ch_tmp[idx+21], ch_tmp[idx+22], ch_tmp[idx+23]);
      printf("\n");
    }
#endif

    const int dft_sz = NR_PRS_IDFT_OVERSAMP_FACTOR * symb_sz;
    c16_t chT_interpol[dft_sz] __attribute__((aligned(32)));
    {
      c16_t chF_interpol[dft_sz] __attribute__((aligned(32)));
      memset(chF_interpol, 0, sizeof(chF_interpol));
      // Place PRS channel estimates in FFT shifted format
      const int first_half = symb_sz - frame_parms->first_carrier_offset;
      const int second_half = prs_cfg->NumRB * NR_NB_SC_PER_RB - first_half;
      const int start_offset = NR_PRS_IDFT_OVERSAMP_FACTOR * symb_sz - first_half;
      LOG_D(PHY, "start_offset %d, first_half %d, second_half %d\n", start_offset, first_half, second_half);
      if (first_half > 0)
        memcpy(&chF_interpol[start_offset], ch_tmp_buf, first_half * sizeof(c16_t));
      if (second_half > 0)
        memcpy(chF_interpol, &ch_tmp_buf[first_half], second_half * sizeof(c16_t));

      // Convert to time domain
      freq2time(NR_PRS_IDFT_OVERSAMP_FACTOR * symb_sz, (int16_t *)chF_interpol, (int16_t *)chT_interpol);
    }
    // peak estimator
    int mean_val = squaredMod(ch_tmp_buf[(prs_cfg->NumRB * NR_NB_SC_PER_RB) >> 1]);
    int prs_toa, ch_pwr;
    peak_estimator(chT_interpol, NR_PRS_IDFT_OVERSAMP_FACTOR * symb_sz, &prs_toa, &ch_pwr, mean_val);
    openair0_config_t *cfg = &openair0_cfg_g[ue->rf_map.card];
    // adjusting the rx_gains for channel peak power
    double ch_pwr_dbm = 10 * log10(ch_pwr) + 30 - SQ15_SQUARED_NORM_FACTOR_DB - ((int)cfg->rx_gain[0] - (int)cfg->rx_gain_offset[0])
                        - dB_fixed(symb_sz);

    prs_meas[rxAnt]->rsrp_dBm = 10 * log10(prs_meas[rxAnt]->rsrp) + 30 - SQ15_SQUARED_NORM_FACTOR_DB
                                - ((int)cfg->rx_gain[0] - (int)cfg->rx_gain_offset[0]) - dB_fixed(symb_sz);

    // prs measurements
    prs_meas[rxAnt]->gNB_id     = gNB_id;
    prs_meas[rxAnt]->sfn        = proc->frame_rx;
    prs_meas[rxAnt]->slot       = proc->nr_slot_rx;
    prs_meas[rxAnt]->rxAnt_idx  = rxAnt;
    prs_meas[rxAnt]->dl_aoa     = rsc_id;
    float dl_toa = prs_toa / (float)NR_PRS_IDFT_OVERSAMP_FACTOR;
    if ((symb_sz - dl_toa) < symb_sz / 2)
      dl_toa -= (symb_sz);
    LOG_I(PHY,
          "[gNB %d][rsc %d][Rx %d][sfn %d][slot %d] DL PRS ToA ==> %.1f / %d samples, peak channel power %.1f dBm, SNR %+.1f dB, "
          "rsrp %+.1f dBm\n",
          gNB_id,
          rsc_id,
          rxAnt,
          proc->frame_rx,
          proc->nr_slot_rx,
          dl_toa,
          symb_sz,
          ch_pwr_dbm,
          prs_meas[rxAnt]->snr,
          prs_meas[rxAnt]->rsrp_dBm);

    set_prs_dl_toa(prs_meas[rxAnt], dl_toa);

#ifdef DEBUG_PRS_CHEST
    sprintf(filename, "%s%i%s", "PRSpilot_", rxAnt, ".m");
    LOG_M(filename, "prs_loc", mod_prs, num_pilots, 1, 1);
    sprintf(filename, "%s%i%s", "rxSigF_", rxAnt, ".m");
    sprintf(varname, "%s%i", "rxF_", rxAnt);
    LOG_M(filename, varname, &rxdataF[rxAnt][0], prs_cfg->NumPRSSymbols * symb_sz, 1, 1);
    sprintf(filename, "%s%i%s", "prsChestF_", rxAnt, ".m");
    sprintf(varname, "%s%i", "prsChF_", rxAnt);
    LOG_M(filename, varname, chF_interpol, symb_sz, 1, 1);
    sprintf(filename, "%s%i%s", "prsChestT_", rxAnt, ".m");
    sprintf(varname, "%s%i", "prsChT_", rxAnt);
    LOG_M(filename, varname, chT_interpol, symb_sz, 1, 1);
#endif

    // T tracer dump
    T(T_UE_PHY_INPUT_SIGNAL,
      T_INT(gNB_id),
      T_INT(proc->frame_rx),
      T_INT(proc->nr_slot_rx),
      T_INT(rxAnt),
      T_BUFFER(rxdataF[rxAnt], frame_parms->samples_per_slot_wCP * sizeof(c16_t)));

    T(T_UE_PHY_DL_CHANNEL_ESTIMATE,
      T_INT(gNB_id),
      T_INT(rsc_id),
      T_INT(proc->frame_rx),
      T_INT(proc->nr_slot_rx),
      T_INT(rxAnt),
      T_BUFFER(chT_interpol, dft_sz * sizeof(c16_t)));
  }

  return(0);
}

c32_t nr_pbch_dmrs_correlation(const NR_DL_FRAME_PARMS *frame_parms,
                               const int symbol,
                               const int dmrss,
                               const int Nid_cell,
                               const int ssb_start_subcarrier,
                               const uint32_t nr_gold_pbch[NR_PBCH_DMRS_LENGTH_DWORD],
                               const c16_t rxdataF[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size])
{
  AssertFatal(dmrss >= 0 && dmrss < 3, "symbol %d is illegal for PBCH DM-RS \n", dmrss);
  unsigned int k = Nid_cell % 4;

  DEBUG_PBCH("PBCH DMRS Correlation : OFDM size %d, Ncp=%d, k=%u symbol %d\n",
             frame_parms->ofdm_symbol_size,
             frame_parms->Ncp,
             k,
             symbol);

  // generate pilot
  // Note: pilot returned by the following function is already the complex conjugate of the transmitted DMRS
  c16_t pilot[200] __attribute__((aligned(16)));
  nr_pbch_dmrs_rx(dmrss, (uint32_t *)nr_gold_pbch, pilot, false);
  c32_t computed_val = {0};
  for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
    int re_offset = ssb_start_subcarrier + k;
    c16_t *pil = pilot;
    const c16_t *rxF = rxdataF[aarx];

    DEBUG_PBCH("pbch ch est pilot RB_DL %d\n", frame_parms->N_RB_DL);
    DEBUG_PBCH("k %u\n", k);

    // Treat first 2 pilots specially (left edge)
    computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
    DEBUG_PBCH("ch 0 %d\n", pil->r * rxF[re_offset].r - pil->i * rxF[re_offset].i);
    DEBUG_PBCH("pilot 0 : rxF - > (%d,%d)  pil -> (%d,%d) \n", rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

    pil++;
    re_offset += 4;
    computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
    DEBUG_PBCH("pilot 1 : rxF - > (%d,%d)  pil -> (%d,%d) \n", rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

    pil++;
    re_offset += 4;
    computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
    DEBUG_PBCH("pilot 2 : rxF - > (%d,%d), pil -> (%d,%d) \n", rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

    pil++;
    re_offset += 4;

    for (int pilot_cnt = 3; pilot_cnt < (3 * 20); pilot_cnt += 3) {
      // in 2nd symbol, skip middle  REs (48 with DMRS,  144 for SSS, and another 48 with DMRS)
      if (dmrss == 1 && pilot_cnt == 12) {
	pilot_cnt=48;
        re_offset += 144;
      }
      computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
      DEBUG_PBCH("pilot %u : rxF= (%d,%d) pil= (%d,%d) \n", pilot_cnt, rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

      pil++;
      re_offset += 4;
      computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
      DEBUG_PBCH("pilot %u : rxF= (%d,%d) pil= (%d,%d) \n", pilot_cnt + 1, rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

      pil++;
      re_offset += 4;
      computed_val = c32x16maddShift(*pil, rxF[re_offset], computed_val, 15);
      DEBUG_PBCH("pilot %u : rxF= (%d,%d)  pil= (%d,%d) \n", pilot_cnt + 2, rxF[re_offset].r, rxF[re_offset].i, pil->r, pil->i);

      pil++;
      re_offset += 4;
    }
  }
  return computed_val;
}

int nr_pbch_channel_estimation(const NR_DL_FRAME_PARMS *frame_parms,
                               const sl_nr_ue_phy_params_t *sl_phy_params,
                               c16_t dl_ch_estimates[frame_parms->ofdm_symbol_size],
                               const UE_nr_rxtx_proc_t *proc,
                               int dmrss,
                               uint ssb_index,
                               uint n_hf,
                               int ssb_start_subcarrier,
                               const c16_t rxdataF[frame_parms->ofdm_symbol_size],
                               bool sidelink,
                               uint Nid)
{
  TracyCZone(ctx, true);
  const int symb_sz = frame_parms->ofdm_symbol_size;

  c16_t pilot[200] __attribute__((aligned(16)));
  uint nushift, num_rbs;
  const uint *gold_seq;
  if (sidelink) {
    nushift = 0;
    AssertFatal(dmrss == 0 || (dmrss >= 5 && dmrss <= 12), "symbol %d is illegal for PSBCH DM-RS \n", dmrss);
    LOG_D(PHY, "PSBCH Channel Estimation SLSSID:%d\n", Nid);
    gold_seq = sl_phy_params->init_params.psbch_dmrs_gold_sequences[Nid];
    num_rbs = SL_NR_NUM_PSBCH_RBS_IN_ONE_SYMBOL;
  } else {
    nushift = Nid % 4;
    AssertFatal(dmrss >= 0 && dmrss < 3, "symbol %d is illegal for PBCH DM-RS \n", dmrss);
    gold_seq = nr_gold_pbch(frame_parms->Lmax, Nid, n_hf, ssb_index);
    num_rbs = 20;
  }

  const int k = nushift;

  DEBUG_PBCH("PBCH Channel Estimation : gNB_id %d, OFDM size %d, Ncp=%d, Ns=%d, k=%d\n",
             proc->gNB_id,
             symb_sz,
             frame_parms->Ncp,
             proc->nr_slot_rx,
             k);

  const int16_t *fl, *fm, *fr;
  switch (k) {
  case 0:
    fl = filt16a_l0;
    fm = filt16a_m0;
    fr = filt16a_r0;
    break;

  case 1:
    fl = filt16a_l1;
    fm = filt16a_m1;
    fr = filt16a_r1;
    break;

  case 2:
    fl = filt16a_l2;
    fm = filt16a_m2;
    fr = filt16a_r2;
    break;

  case 3:
    fl = filt16a_l3;
    fm = filt16a_m3;
    fr = filt16a_r3;
    break;

  default:
    msg("pbch_channel_estimation: k=%d -> ERROR\n",k);
    return(-1);
    break;
  }

  // generate pilot
  // Note: pilot returned by the following function is already the complex conjugate of the transmitted DMRS
  nr_pbch_dmrs_rx(dmrss, gold_seq, pilot, sidelink);

  int re_offset = ssb_start_subcarrier + k;
  const c16_t *pil = pilot;
  const c16_t *rxF = rxdataF;
  c16_t *dl_ch = dl_ch_estimates;
  memset(dl_ch, 0, sizeof(c16_t) * symb_sz);

  DEBUG_PBCH("pbch ch est pilot RB_DL %d\n", frame_parms->N_RB_DL);
  DEBUG_PBCH("k %d\n", k);

  // Treat first 2 pilots specially (left edge)
  c16_t ch;
  ch = c16mulShift(*pil, rxF[re_offset], 15);
  DEBUG_PBCH("pilot 0: rxF= (%d,%d), ch= (%d,%d), pil=(%d,%d)\n", rxF[re_offset].r, rxF[re_offset].i, ch.r, ch.i, pil->r, pil->i);

  multadd_real_vector_complex_scalar(fl, ch, dl_ch, 16);
  pil++;
  re_offset += 4;
  ch = c16mulShift(*pil, rxF[re_offset], 15);
  DEBUG_PBCH("pilot 1: rxF= (%d,%d), ch= (%d,%d), pil=(%d,%d)\n", rxF[re_offset].r, rxF[re_offset].i, ch.r, ch.i, pil->r, pil->i);

  multadd_real_vector_complex_scalar(fm, ch, dl_ch, 16);
  pil++;
  re_offset += 4;
  ch = c16mulShift(*pil, rxF[re_offset], 15);
  DEBUG_PBCH("pilot 2: rxF= (%d,%d), ch= (%d,%d), pil=(%d,%d)\n", rxF[re_offset].r, rxF[re_offset].i, ch.r, ch.i, pil->r, pil->i);

  multadd_real_vector_complex_scalar(fr, ch, dl_ch, 16);
  pil++;
  re_offset += 4;
  dl_ch += 12;

  for (int pilot_cnt = 3; pilot_cnt < (3 * num_rbs); pilot_cnt += 3) {
    // in 2nd symbol, skip middle  REs (48 with DMRS,  144 for SSS, and another 48 with DMRS)
    if (dmrss == 1 && pilot_cnt == 12) {
      pilot_cnt = 48;
      re_offset += 144;
      dl_ch += 144;
    }
    ch = c16mulShift(*pil, rxF[re_offset], 15);
    DEBUG_PBCH("pilot %u: rxF=(%d,%d) ch=(%d,%d) pil=(%d,%d)\n",
               pilot_cnt,
               rxF[re_offset].r,
               rxF[re_offset].i,
               ch.r,
               ch.i,
               pil->r,
               pil->i);

    multadd_real_vector_complex_scalar(fl, ch, dl_ch, 16);
    pil++;
    re_offset += 4;
    ch = c16mulShift(*pil, rxF[re_offset], 15);
    DEBUG_PBCH("pilot %u: rxF=(%d,%d) ch=(%d,%d) pil=(%d,%d)\n",
               pilot_cnt + 1,
               rxF[re_offset].r,
               rxF[re_offset].i,
               ch.r,
               ch.i,
               pil->r,
               pil->i);

    multadd_real_vector_complex_scalar(fm, ch, dl_ch, 16);
    pil++;
    re_offset += 4;
    ch = c16mulShift(*pil, rxF[re_offset], 15);
    DEBUG_PBCH("pilot %u: rxF=(%d,%d) ch=(%d,%d) pil=(%d,%d)\n",
               pilot_cnt + 2,
               rxF[re_offset].r,
               rxF[re_offset].i,
               ch.r,
               ch.i,
               pil->r,
               pil->i);

    multadd_real_vector_complex_scalar(fr, ch, dl_ch, 16);
    pil++;
    re_offset += 4;
    dl_ch += 12;
  }

  TracyCZoneEnd(ctx);
  return(0);
}

void nr_pdcch_channel_estimation(const NR_DL_FRAME_PARMS *frame_parms,
                                 int nb_rb_coreset,
                                 int coreset_start_rb,
                                 int dmrs_ref,
                                 uint16_t BWPStart,
                                 int32_t pdcch_est_size,
                                 c16_t pdcch_dl_ch_estimates[][pdcch_est_size],
                                 c16_t rxdataF[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size],
                                 c16_t *pilot)
{
  const int symb_sz = frame_parms->ofdm_symbol_size;

#ifdef DEBUG_PDCCH
  printf("pdcch_channel_estimation: BWPStart %d, coreset_start_rb %d, coreset_nb_rb %d\n",
         BWPStart, coreset_start_rb, nb_rb_coreset);
#endif

  const unsigned short coreset_start_subcarrier = (BWPStart + coreset_start_rb) * NR_NB_SC_PER_RB;

#if CH_INTERP
  int16_t *fl = filt16a_l1;
  int16_t *fm = filt16a_m1;
  int16_t *fr = filt16a_r1;
#endif

  for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
    int k = coreset_start_subcarrier + 1;
    c16_t *pil = &pilot[(dmrs_ref + coreset_start_rb) * 3];
    c16_t *rxF = rxdataF[aarx];
    c16_t *dl_ch = pdcch_dl_ch_estimates[aarx];

    memset(dl_ch, 0, sizeof(c16_t) * symb_sz);

#ifdef DEBUG_PDCCH
    printf("pdcch ch est pilot addr %p RB_DL %d\n", &pilot[dmrs_ref * 3], frame_parms->N_RB_DL);
    printf("k %d\n", k);
    printf("rxF addr %p\n", rxF);

    printf("dl_ch addr %p\n",dl_ch);
#endif
  #if CH_INTERP
    //    if ((frame_parms->N_RB_DL&1)==0) {
    // Treat first 2 pilots specially (left edge)
    multadd_real_vector_complex_scalar(fl, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
    k += 2;

    multadd_real_vector_complex_scalar(fm, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
    k += 4;

    multadd_real_vector_complex_scalar(fr, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
    dl_ch += 12;
    k += 4;

    for (int pilot_cnt = 3; pilot_cnt < (3 * nb_rb_coreset); pilot_cnt += 3) {
      multadd_real_vector_complex_scalar(fl, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
      k += 4;

      multadd_real_vector_complex_scalar(fm, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
      k += 4;

      multadd_real_vector_complex_scalar(fr, c16mulShift(*pil++, rxF[k], 15), dl_ch, 16);
      dl_ch += 12;
      k += 4;
    }
#else //ELSE CH_INTERP
    c32_t ch_sum = {0, 0};

    for (int pilot_cnt = 0; pilot_cnt < 3 * nb_rb_coreset; pilot_cnt++) {
      c16_t ch = c16mulShift(*pil++, rxF[k], 15);
      ch_sum.r += ch.r;
      ch_sum.i += ch.i;
      k += 4;

      if (pilot_cnt % 3 == 2) {
        ch.r = ch_sum.r / 3;
        ch.i = ch_sum.i / 3;
        // Replicate the RB average exactly, without the unity filter's fixed-point truncation.
        for (int re = 0; re < NR_NB_SC_PER_RB; re++)
          dl_ch[re] = ch;
        dl_ch += 12;
        ch_sum = (c32_t){0};
      }
    }
#endif // END CH_INTERP
  }
}

static void revert_delay(c16_t *dl_ch, uint num_dl_ch, const delay_t *delay, const c16_t delay_table[][NR_MAX_OFDM_SYMBOL_SIZE])
{
  int inv_delay_idx = get_delay_idx(-delay->est_delay, MAX_DELAY_COMP);
  const c16_t *dl_inv_delay_table = delay_table[inv_delay_idx];
  mult_complex_vectors(dl_ch, dl_inv_delay_table, dl_ch, num_dl_ch, 8);
}

static inline uint32_t estimate_noise_var(const c16_t *dl_ls_est, const c16_t *dl_ch, uint num_dl_ch)
{
  if (num_dl_ch == 0) {
    LOG_E(NR_PHY, "Couldn't estimate PDSCH N_0\n");
    return 0;
  }

  uint64_t noise_amp2 = 0;
  for (uint k = 0; k < num_dl_ch; k++)
    noise_amp2 += c16amp2(c16sub(dl_ls_est[k], dl_ch[k]));

  return (uint32_t)(noise_amp2 / num_dl_ch);
}

static void NFAPI_NR_DMRS_TYPE1_linear_interp(const NR_DL_FRAME_PARMS *frame_parms,
                                              c16_t *rxF,
                                              int delta,
                                              c16_t *pil,
                                              c16_t *dl_ch0,
                                              unsigned short bwp_start_subcarrier,
                                              const freq_alloc_bitmap_t *freq_alloc,
                                              int bwpsize,
                                              uint32_t *nvar)
{
  const int symb_sz = frame_parms->ofdm_symbol_size;
  int re_offset = delta + bwp_start_subcarrier;
  c16_t dl_ls_est[symb_sz] __attribute__((aligned(32)));
  memset(dl_ls_est, 0, sizeof(dl_ls_est));
  int idx = 0;
  int pos = freq_alloc->first_rb;
  int last_processed_rb = freq_alloc->first_rb;
  int block_start, block_end;
  while (find_next_rb_block(freq_alloc->bitmap, bwpsize, &pos, &block_start, &block_end)) {
    int skipped_rbs = block_start - last_processed_rb;
    pil += skipped_rbs * 6;
    re_offset += skipped_rbs * NR_NB_SC_PER_RB;
    for (int rb = block_start; rb <= block_end; rb++) {
      for (int pilot_cnt = 0; pilot_cnt < 6; pilot_cnt += 2) {
        c16_t ch_l = c16mulShift(*pil, rxF[re_offset], 15);
#ifdef DEBUG_PDSCH
        printf("pilot %3d: pil -> (%6d,%6d), rxF -> (%4d,%4d), ch -> (%4d,%4d) \n", pilot_cnt, pil->r, pil->i, rxF[re_offset].r, rxF[re_offset].i, ch_l.r, ch_l.i);
#endif
        pil++;
        re_offset += 2;
        c16_t ch_r = c16mulShift(*pil, rxF[re_offset], 15);
#ifdef DEBUG_PDSCH
        printf("pilot %3d: pil -> (%6d,%6d), rxF -> (%4d,%4d), ch -> (%4d,%4d) \n", pilot_cnt + 1, pil->r, pil->i, rxF[re_offset].r, rxF[re_offset].i, ch_r.r, ch_r.i);
#endif
        c16_t ch = c16addShift(ch_l, ch_r, 1);
        pil++;
        re_offset += 2;
        for (int k = 0; k < 4; k++) {
          dl_ls_est[idx] = ch;
          idx++;
        }
      }
    }
    last_processed_rb = block_end + 1;
  }
  c16_t ch_estimates_time[symb_sz] __attribute__((aligned(32)));
  delay_t delay = {0};
  nr_est_delay(symb_sz, dl_ls_est, ch_estimates_time, &delay);
  int delay_idx = get_delay_idx(delay.est_delay, MAX_DELAY_COMP);
  const c16_t *dl_delay_table = frame_parms->delay_table[delay_idx];
  c16_t *dl_ch = dl_ch0;
  for (int pilot_cnt = 0; pilot_cnt < idx >> 1; pilot_cnt++) {
    int k = pilot_cnt << 1;
    c16_t ch = c16mulShift(dl_ls_est[k], dl_delay_table[k], 8);
    if (pilot_cnt == 0) { // Treat first pilot
      c16multaddVectRealComplex(filt16_ul_p0, &ch, dl_ch, 16);
    } else if (pilot_cnt == 1 || pilot_cnt == 2) {
      c16multaddVectRealComplex(filt16_ul_p1p2, &ch, dl_ch, 16);
    } else if (pilot_cnt == (idx >> 1) - 1) { // Treat last pilot
      c16multaddVectRealComplex(filt16_ul_last, &ch, dl_ch, 16);
    } else { // Treat middle pilots
      c16multaddVectRealComplex(filt16_ul_middle, &ch, dl_ch, 16);
      if (pilot_cnt % 2 == 0) {
        dl_ch += 4;
      }
    }
  }

  // Revert delay
  revert_delay(dl_ch0, idx, &delay, frame_parms->delay_table);

  // Estimate noise var
  if (nvar)
    *nvar += estimate_noise_var(dl_ls_est, dl_ch0, idx);
}

static void NFAPI_NR_DMRS_TYPE1_average_prb(const int symb_sz,
                                            c16_t *rxF,
                                            int delta,
                                            c16_t *pil,
                                            c16_t *dl_ch,
                                            unsigned short bwp_start_subcarrier,
                                            unsigned short nb_rb_pdsch)
{
  int re_offset = delta + bwp_start_subcarrier;
  const int P_average = 6;

  c32_t ch32 = {0};
  for (int p_av = 0; p_av < P_average; p_av++) {
    ch32 = c32x16maddShift(*pil, rxF[re_offset], ch32, 15);
    pil++;
    re_offset += 2;
  }
  c16_t ch = c16x32div(ch32, P_average);

#if NO_INTERP
  for (int i = 0; i < 2 * P_average; i++) {
    dl_ch[i] = ch;
  }
  dl_ch += 2 * P_average;
#else
  c16multaddVectRealComplex(filt8_avlip0, &ch, dl_ch, 8);
  dl_ch += 16;
  c16multaddVectRealComplex(filt8_avlip1, &ch, dl_ch, 8);
  dl_ch += 16;
  c16multaddVectRealComplex(filt8_avlip2, &ch, dl_ch, 8);
  dl_ch -= 24;
#endif

  for (int pilot_cnt = P_average; pilot_cnt < 6 * (nb_rb_pdsch - 1); pilot_cnt += P_average) {
    c32_t val = {};
    for (int p_av = 0; p_av < P_average; p_av++) {
      val = c32x16maddShift(*pil, rxF[re_offset], val, 15);
      pil++;
      re_offset += 2;
    }
    ch = c16x32div(val, P_average);

#if NO_INTERP
    for (int i = 0; i < 2 * P_average; i++) {
      dl_ch[i] = ch;
    }
    dl_ch += 2 * P_average;
#else
    dl_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
    dl_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384
    dl_ch += 4;
    c16multaddVectRealComplex(filt8_avlip3, &ch, dl_ch, 8);
    dl_ch += 8;
    c16multaddVectRealComplex(filt8_avlip4, &ch, dl_ch, 8);
    dl_ch += 8;
    c16multaddVectRealComplex(filt8_avlip5, &ch, dl_ch, 8);
    dl_ch -= 8;
#endif
  }

  c32_t tmp = {0};
  for (int p_av = 0; p_av < P_average; p_av++) {
    tmp = c32x16maddShift(*pil, rxF[re_offset], tmp, 15);
    pil++;
    re_offset += 2;
  }
  ch = c16x32div(tmp, P_average);

#if NO_INTERP
  for (int i = 0; i < 2 * P_average; i++) {
    dl_ch[i] = ch;
  }
#else
  dl_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
  dl_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384
  dl_ch += 4;
  c16multaddVectRealComplex(filt8_avlip3, &ch, dl_ch, 8);
  dl_ch += 8;
  c16multaddVectRealComplex(filt8_avlip6, &ch, dl_ch, 8);
#endif
}

static void NFAPI_NR_DMRS_TYPE2_linear_interp(const NR_DL_FRAME_PARMS *frame_parms,
                                              c16_t *rxF,
                                              int delta,
                                              c16_t *pil,
                                              c16_t *dl_ch0,
                                              unsigned short bwp_start_subcarrier,
                                              const freq_alloc_bitmap_t *freq_alloc,
                                              int bwpsize,
                                              uint32_t *nvar)
{
  const int symb_sz = frame_parms->ofdm_symbol_size;
  int re_offset = delta + bwp_start_subcarrier;
  c16_t dl_ls_est[symb_sz] __attribute__((aligned(32)));
  memset(dl_ls_est, 0, sizeof(dl_ls_est));
  int idx = 0;
  int pos = freq_alloc->first_rb;
  int last_processed_rb = freq_alloc->first_rb;
  int block_start, block_end;
  while (find_next_rb_block(freq_alloc->bitmap, bwpsize, &pos, &block_start, &block_end)) {
    int skipped_rbs = block_start - last_processed_rb;
    pil += skipped_rbs * 4;
    re_offset += skipped_rbs * NR_NB_SC_PER_RB;
    for (int rb = block_start; rb <= block_end; rb++) {
      for (int pilot_cnt = 0; pilot_cnt < 4; pilot_cnt += 2) {
        c16_t ch_l = c16mulShift(*pil, rxF[re_offset], 15);
#ifdef DEBUG_PDSCH
        printf("pilot %3d: pil -> (%6d,%6d), rxF -> (%4d,%4d), ch -> (%4d,%4d) \n", pilot_cnt, pil->r, pil->i, rxF[re_offset].r, rxF[re_offset].i, ch_l.r, ch_l.i);
#endif
        pil++;
        re_offset++;
        c16_t ch_r = c16mulShift(*pil, rxF[re_offset], 15);
#ifdef DEBUG_PDSCH
        printf("pilot %3d: pil -> (%6d,%6d), rxF -> (%4d,%4d), ch -> (%4d,%4d) \n", pilot_cnt + 1, pil->r, pil->i, rxF[re_offset].r, rxF[re_offset].i, ch_r.r, ch_r.i);
#endif
        c16_t ch = c16addShift(ch_l, ch_r, 1);
        pil++;
        re_offset += 5;
        for (int k = 0; k < 6; k++) {
          dl_ls_est[idx] = ch;
          idx++;
        }
      }
    }
    last_processed_rb = block_end + 1;
  }

  c16_t ch_estimates_time[symb_sz] __attribute__((aligned(32)));
  delay_t delay = {0};
  nr_est_delay(symb_sz, dl_ls_est, ch_estimates_time, &delay);
  int delay_idx = get_delay_idx(delay.est_delay, MAX_DELAY_COMP);
  const c16_t *dl_delay_table = frame_parms->delay_table[delay_idx];
  // kept the same as before but unclear why  would it be nb_rb_pdsch / 2 and not / 3
  c16_t *dl_ch = dl_ch0;
  for (int pilot_cnt = 0; pilot_cnt < idx >> 1; pilot_cnt++) {
    int k = pilot_cnt << 1;
    c16_t ch = c16mulShift(dl_ls_est[k], dl_delay_table[k], 8);
    if (pilot_cnt == 0) { // Treat first pilot
      c16multaddVectRealComplex(filt16_ul_p0, &ch, dl_ch, 16);
    } else if (pilot_cnt == 1 || pilot_cnt == 2) {
      c16multaddVectRealComplex(filt16_ul_p1p2, &ch, dl_ch, 16);
    } else if (pilot_cnt == (idx >> 1) - 1) { // Treat last pilot
      c16multaddVectRealComplex(filt16_ul_last, &ch, dl_ch, 16);
    } else { // Treat middle pilots
      c16multaddVectRealComplex(filt16_ul_middle, &ch, dl_ch, 16);
      if (pilot_cnt % 2 == 0) {
        dl_ch += 4;
      }
    }
  }

  // Revert delay
  revert_delay(dl_ch0, idx, &delay, frame_parms->delay_table);

  // Estimate noise var
  if (nvar)
    *nvar += estimate_noise_var(dl_ls_est, dl_ch0, idx);
}

static void NFAPI_NR_DMRS_TYPE2_average_prb(const int symb_sz,
                                            c16_t *rxF,
                                            int delta,
                                            c16_t *pil,
                                            c16_t *dl_ch,
                                            unsigned short bwp_start_subcarrier,
                                            unsigned short nb_rb_pdsch)
{
  int re_offset = delta + bwp_start_subcarrier;
  const int P_average = 4;

  c32_t ch32 = {0};
  for (int p_av = 0; p_av < P_average; p_av++) {
    ch32 = c32x16maddShift(*pil, rxF[re_offset], ch32, 15);
    pil++;
    re_offset++;
  }
  c16_t ch = c16x32div(ch32, P_average);

#if NO_INTERP
  for (int i = 0; i < 3 * P_average; i++) {
    dl_ch[i] = ch;
  }
  dl_ch += 3 * P_average;
#else
  c16multaddVectRealComplex(filt8_avlip0, &ch, dl_ch, 8);
  dl_ch += 8;
  c16multaddVectRealComplex(filt8_avlip1, &ch, dl_ch, 8);
  dl_ch += 8;
  c16multaddVectRealComplex(filt8_avlip2, &ch, dl_ch, 8);
  dl_ch -= 12;
#endif

  for (int pilot_cnt = P_average; pilot_cnt < 4 * (nb_rb_pdsch - 1); pilot_cnt += P_average) {
    c32_t val = {};
    for (int p_av = 0; p_av < P_average; p_av++) {
      val = c32x16maddShift(*pil, rxF[re_offset], val, 15);
      pil++;
      re_offset += 5;
    }
    ch = c16x32div(val, P_average);

#if NO_INTERP
    for (int i = 0; i < 3 * P_average; i++) {
      dl_ch[i] = ch;
    }
    dl_ch += 3 * P_average;
#else
    dl_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
    dl_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384
    dl_ch += 4;
    c16multaddVectRealComplex(filt8_avlip3, &ch, dl_ch, 8);
    dl_ch += 8;
    c16multaddVectRealComplex(filt8_avlip4, &ch, dl_ch, 8);
    dl_ch += 8;
    c16multaddVectRealComplex(filt8_avlip5, &ch, dl_ch, 8);
    dl_ch -= 8;
#endif
  }

  c32_t tmp = {};
  for (int p_av = 0; p_av < P_average; p_av++) {
    tmp = c32x16maddShift(*pil, rxF[re_offset], tmp, 15);
    pil++;
    re_offset += 5;
  }
  ch = c16x32div(tmp, P_average);

#if NO_INTERP
  for (int i = 0; i < 3 * P_average; i++) {
    dl_ch[i] = ch;
  }
#else
  dl_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
  dl_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384
  dl_ch += 4;
  c16multaddVectRealComplex(filt8_avlip3, &ch, dl_ch, 8);
  dl_ch += 8;
  c16multaddVectRealComplex(filt8_avlip6, &ch, dl_ch, 8);
#endif
}

void nr_pdsch_channel_estimation(PHY_VARS_NR_UE *ue,
                                 const UE_nr_rxtx_proc_t *proc,
                                 const fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch,
                                 const freq_alloc_bitmap_t *freq_alloc,
                                 int nl,
                                 unsigned short p,
                                 unsigned char symbol,
                                 uint32_t pdsch_est_size,
                                 int32_t dl_ch_estimates[][pdsch_est_size],
                                 int rxdataFsize,
                                 c16_t rxdataF[][rxdataFsize],
                                 uint32_t *nvar)
{
  const int symb_sz = ue->frame_parms.ofdm_symbol_size;
  const int slot = proc->nr_slot_rx;
  const NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  const int ch_offset = symb_sz * symbol;
  const int symbol_offset = symb_sz * symbol;
  const int bwp_start_subcarrier = (dlsch->BWPStart + freq_alloc->first_rb) * NR_NB_SC_PER_RB;

#ifdef DEBUG_PDSCH
  printf(
      "PDSCH Channel Estimation : ch_offset %d, symbol_offset %d OFDM size %d, Ncp=%d, Ns=%d, bwp_start_subcarrier=%d "
      "symbol %d\n",
      ch_offset,
      symbol_offset,
      symb_sz,
      frame_parms->Ncp,
      slot,
      bwp_start_subcarrier,
      symbol);
#endif

  // generate pilot for gNB port number 1000+p
  int config_type = dlsch->dmrsConfigType;
  int rb_offset = freq_alloc->first_rb + (dlsch->refPoint ? 0 : dlsch->BWPStart);
  int nb_rb_pdsch = freq_alloc->last_rb - freq_alloc->first_rb + 1;
  int8_t delta = get_delta(p, config_type);
  c16_t pilot[3280] __attribute__((aligned(16)));
  // Note: pilot returned by the following function is already the complex conjugate of the transmitted DMRS
  float beta_dmrs_pdsch = get_beta_dmrs(dlsch->n_dmrs_cdm_groups, config_type == NFAPI_NR_DMRS_TYPE2);
  int16_t dmrs_scaling = (int16_t)((1 / beta_dmrs_pdsch) * (1 << 14));
  const uint32_t *gold =
      nr_gold_pdsch(frame_parms->N_RB_DL, frame_parms->symbols_per_slot, dlsch->dlDmrsScramblingId, dlsch->nscid, slot, symbol);
  nr_pdsch_dmrs_rx(frame_parms->Ncp, gold, pilot, 1000 + p, 0, nb_rb_pdsch + rb_offset, config_type, dmrs_scaling);

  for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
#ifdef DEBUG_PDSCH
    printf("\n============================================\n");
    printf("==== Tx port %i, Rx antenna %i, Symbol %i ====\n", p, aarx, symbol);
    printf("============================================\n");
#endif

    c16_t *rxF = rxdataF[aarx] + symbol_offset;
    c16_t *dl_ch = (c16_t *)&dl_ch_estimates[nl * frame_parms->nb_antennas_rx + aarx][ch_offset];
    memset(dl_ch, 0, sizeof(*dl_ch) * symb_sz);

    if (config_type == NFAPI_NR_DMRS_TYPE1 && ue->chest_freq == 0) {
      NFAPI_NR_DMRS_TYPE1_linear_interp(frame_parms,
                                        rxF,
                                        delta,
                                        &pilot[6 * rb_offset],
                                        dl_ch,
                                        bwp_start_subcarrier,
                                        freq_alloc,
                                        dlsch->BWPSize,
                                        nvar);

    } else if (config_type == NFAPI_NR_DMRS_TYPE2 && ue->chest_freq == 0) {
      NFAPI_NR_DMRS_TYPE2_linear_interp(frame_parms,
                                        rxF,
                                        delta,
                                        &pilot[4 * rb_offset],
                                        dl_ch,
                                        bwp_start_subcarrier,
                                        freq_alloc,
                                        dlsch->BWPSize,
                                        nvar);

    } else if (config_type == NFAPI_NR_DMRS_TYPE1) {
      AssertFatal(dlsch->resource_alloc == 1, "PRB average in channel estimation not supported for type0 DLSCH\n");
      NFAPI_NR_DMRS_TYPE1_average_prb(symb_sz, rxF, delta, &pilot[6 * rb_offset], dl_ch, bwp_start_subcarrier, nb_rb_pdsch);

    } else {
      AssertFatal(dlsch->resource_alloc == 1, "PRB average in channel estimation not supported for type0 DLSCH\n");
      NFAPI_NR_DMRS_TYPE2_average_prb(symb_sz, rxF, delta, &pilot[4 * rb_offset], dl_ch, bwp_start_subcarrier, nb_rb_pdsch);
    }

#ifdef DEBUG_PDSCH
    dl_ch = &dl_ch_estimates[nl * frame_parms->nb_antennas_rx + aarx][ch_offset];
    for (uint16_t idxP = 0; idxP < ceil((float)nb_rb_pdsch * 12 / 8); idxP++) {
      for (uint8_t idxI = 0; idxI < 8; idxI++) {
        printf("%4d\t%4d\t", dl_ch[idxP * 8 + idxI].r, dl_ch[idxP * 8 + idxI].i);
      }
      printf("%2d\n", idxP);
    }
#endif
  }
}
