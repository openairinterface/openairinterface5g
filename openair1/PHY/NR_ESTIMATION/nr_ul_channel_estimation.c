/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_common.h"
#include "platform_types.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nr_ul_estimation.h"
#include "PHY/sse_intrin.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_UE_ESTIMATION/filt16a_32.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include "executables/softmodem-common.h"
#include "nr_phy_common.h"
#include "openair1/PHY/TOOLS/phy_scope_interface.h"
#include "T.h"

//#define DEBUG_CH
//#define DEBUG_PUSCH
//#define SRS_DEBUG

#define NO_INTERP 1
#define dBc(x, y) (dB_fixed(((int32_t)(x)) * (x) + ((int32_t)(y)) * (y)))

typedef struct puschAntennaProc_s {
  unsigned char Ns;
  int nl;
  unsigned short p;
  unsigned char symbol;
  unsigned short bwp_start_subcarrier;
  int aarx;
  uint16_t ant_port_start;
  int numAntennas;
  const nfapi_nr_pusch_pdu_t *pusch_pdu;
  int *max_ch;
  c16_t *pilot;
  int *nest_count;
  uint64_t *noise_amp2;
  delay_t *delay;
  int chest_freq;
  NR_gNB_PUSCH *pusch_vars;
  NR_DL_FRAME_PARMS *frame_parms;
  c16_t **rxdataF;
  task_ans_t *ans;
  scopeData_t *scope;
  c16_t *pusch_ch_est_dmrs_pos_slot_mem;
  int dmrs_symbol_start_idx;
} puschAntennaProc_t;

__attribute__((always_inline)) inline c16_t c32x16cumulVectVectWithSteps(c16_t *in1,
                                                                         int *offset1,
                                                                         const int step1,
                                                                         c16_t *in2,
                                                                         int *offset2,
                                                                         const int step2,
                                                                         const int modulo2,
                                                                         const int N)
{
  int localOffset1 = *offset1;
  int localOffset2 = *offset2;
  c32_t cumul = {0};
  for (int i = 0; i < N; i++) {
    cumul = c32x16maddShift(in1[localOffset1], in2[localOffset2], cumul, 15);
    localOffset1 += step1;
    localOffset2 = (localOffset2 + step2) % modulo2;
  }
  *offset1 = localOffset1;
  *offset2 = localOffset2;
  return c16x32div(cumul, N);
}

static void nr_pusch_antenna_processing(void *arg)
{
  puschAntennaProc_t *rdata = (puschAntennaProc_t *)arg;
  unsigned char Ns = rdata->Ns;
  int nl = rdata->nl;
  unsigned short p = rdata->p;
  unsigned char symbol = rdata->symbol;
  int aarx = rdata->aarx;
  int numAntennas = rdata->numAntennas;
  unsigned short bwp_start_subcarrier = rdata->bwp_start_subcarrier;
  const nfapi_nr_pusch_pdu_t *pusch_pdu = rdata->pusch_pdu;
  int *max_ch = rdata->max_ch;
  c16_t *pilot = rdata->pilot;
  uint64_t noise_amp2 = *(rdata->noise_amp2);
  int nest_count = *(rdata->nest_count);
  delay_t *delay = rdata->delay;

  const int chest_freq = rdata->chest_freq;
  NR_gNB_PUSCH *pusch_vars = rdata->pusch_vars;
  c16_t **ul_ch_estimates = (c16_t **)pusch_vars->ul_ch_estimates;
  NR_DL_FRAME_PARMS *frame_parms = rdata->frame_parms;
  const int symbolSize = frame_parms->ofdm_symbol_size;
  const int slot_offset = (Ns % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * symbolSize;
  const int delta = get_delta(p, pusch_pdu->dmrs_config_type);
  const int symbol_offset = symbolSize * symbol;
  const int k0 = bwp_start_subcarrier;
  const int nb_rb_pusch = pusch_pdu->rb_size;
  const int aa_start = rdata->ant_port_start;
  const uint8_t num_sp_streams = rdata->pusch_pdu->param_v4.numSpatialStreamIndices;
  for (int antenna = aarx; antenna < aarx + numAntennas; antenna++) {
    c16_t ul_ls_est[symbolSize] __attribute__((aligned(32)));
    memset(ul_ls_est, 0, sizeof(c16_t) * symbolSize);
    c16_t *rxdataF = (c16_t *)&rdata->rxdataF[aa_start + antenna][symbol_offset + slot_offset];
    c16_t *ul_ch = &ul_ch_estimates[nl * num_sp_streams + antenna][symbol_offset];
    memset(ul_ch, 0, sizeof(*ul_ch) * symbolSize);

    LOG_D(PHY,
          "symbol_offset %d, slot_offset %d, OFDM size %d, Ns = %d, k0 = %d, symbol %d\n",
          symbol_offset,
          slot_offset,
          symbolSize,
          Ns,
          k0,
          symbol);

#ifdef DEBUG_PUSCH
    LOG_I(PHY, "symbol_offset %d, delta %d\n", symbol_offset, delta);
    LOG_I(PHY, "ch est pilot, N_RB_UL %d\n", frame_parms->N_RB_UL);
    LOG_I(PHY,
          "bwp_start_subcarrier %d, k0 %d, first_carrier %d, nb_rb_pusch %d\n",
          bwp_start_subcarrier,
          k0,
          frame_parms->first_carrier_offset,
          nb_rb_pusch);
    LOG_I(PHY, "ul_ch addr %p \n", ul_ch);
#endif

    if (pusch_pdu->dmrs_config_type == pusch_dmrs_type1 && chest_freq == 0) {
      c16_t *pil = pilot;
      int re_offset = k0;
      LOG_D(PHY, "PUSCH estimation DMRS type 1, Freq-domain interpolation");
      int pilot_cnt = 0;
#if T_TRACER
      int ch_est_cnt = 0; // To trace channel coefficients
#endif

      for (int n = 0; n < 3 * nb_rb_pusch; n++) {
        // LS estimation
        c32_t ch = {0};

        for (int k_line = 0; k_line <= 1; k_line++) {
          re_offset = (k0 + (n << 2) + (k_line << 1) + delta);
          ch = c32x16maddShift(*pil, rxdataF[re_offset], ch, 16);
          pil++;
        }

        c16_t ch16 = {.r = (int16_t)ch.r, .i = (int16_t)ch.i};
        *max_ch = max(*max_ch, max(abs(ch.r), abs(ch.i)));
        for (int k = pilot_cnt << 1; k < (pilot_cnt << 1) + 4; k++) {
          ul_ls_est[k] = ch16;
        }
//------------------Write channel parameters to Memory  for data recording ------------------//
#if T_TRACER
        if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS) && nl == 0) {
          // Trace channel coefficients
          c16_t *pusch_ch_est_dmrs_pos_slot_mem = rdata->pusch_ch_est_dmrs_pos_slot_mem;
          int dmrs_symbol_start_idx = rdata->dmrs_symbol_start_idx;
          pusch_ch_est_dmrs_pos_slot_mem[dmrs_symbol_start_idx + delta + ch_est_cnt] = ch16; // 0, 2, 4, 6, 8, location of REs
          pusch_ch_est_dmrs_pos_slot_mem[dmrs_symbol_start_idx + delta + ch_est_cnt + 2] = ch16; // 0, 2, 4, 6, 8, location of REs
        }
        ch_est_cnt += 4;
#endif
        pilot_cnt += 2;
      }
      c16_t ch_estimates_time[frame_parms->ofdm_symbol_size] __attribute__((aligned(32)));
      nr_est_delay(frame_parms->ofdm_symbol_size, ul_ls_est, ch_estimates_time, delay);
      if (rdata->scope && antenna == 0) {
        metadata mt = {.slot = -1, .frame = -1};
        scopeData_t *tmp = rdata->scope;
        tmp->copyData(tmp, gNBulDelay, ch_estimates_time, sizeof(c16_t), 1, frame_parms->ofdm_symbol_size, 0, &mt);
      }
      int delay_idx = get_delay_idx(delay->est_delay, MAX_DELAY_COMP);
      c16_t *ul_delay_table = frame_parms->delay_table[delay_idx];

#ifdef DEBUG_PUSCH
      printf("Estimated delay = %i\n", delay->est_delay >> 1);
#endif

      pilot_cnt = 0;
      for (int n = 0; n < 3 * nb_rb_pusch; n++) {
        // Channel interpolation
        for (int k_line = 0; k_line <= 1; k_line++) {
          // Apply delay
          int k = pilot_cnt << 1;
          c16_t ch16 = c16mulShift(ul_ls_est[k], ul_delay_table[k], 8);

#ifdef DEBUG_PUSCH
          re_offset = (k0 + (n << 2) + (k_line << 1)) % symbolSize;
          c16_t *rxF = &rxdataF[re_offset];
          printf("pilot %4d: pil -> (%6d,%6d), rxF -> (%4d,%4d), ch -> (%4d,%4d)\n",
                 pilot_cnt,
                 pil->r,
                 pil->i,
                 rxF->r,
                 rxF->i,
                 ch.r,
                 ch.i);
#endif

          if (pilot_cnt == 0) {
            c16multaddVectRealComplex(filt16_ul_p0, &ch16, ul_ch, 16);
          } else if (pilot_cnt == 1 || pilot_cnt == 2) {
            c16multaddVectRealComplex(filt16_ul_p1p2, &ch16, ul_ch, 16);
          } else if (pilot_cnt == (6 * nb_rb_pusch - 1)) {
            c16multaddVectRealComplex(filt16_ul_last, &ch16, ul_ch, 16);
          } else {
            c16multaddVectRealComplex(filt16_ul_middle, &ch16, ul_ch, 16);
            if (pilot_cnt % 2 == 0) {
              ul_ch += 4;
            }
          }

          pilot_cnt++;
        }
      }

      // Revert delay
      pilot_cnt = 0;
      ul_ch = &ul_ch_estimates[nl * num_sp_streams + antenna][symbol_offset];
      int inv_delay_idx = get_delay_idx(-delay->est_delay, MAX_DELAY_COMP);
      c16_t *ul_inv_delay_table = frame_parms->delay_table[inv_delay_idx];
      for (int n = 0; n < 3 * nb_rb_pusch; n++) {
        for (int k_line = 0; k_line <= 1; k_line++) {
          int k = pilot_cnt << 1;
          ul_ch[k] = c16mulShift(ul_ch[k], ul_inv_delay_table[k], 8);
          ul_ch[k + 1] = c16mulShift(ul_ch[k + 1], ul_inv_delay_table[k + 1], 8);
          noise_amp2 += c16amp2(c16sub(ul_ls_est[k], ul_ch[k]));
          noise_amp2 += c16amp2(c16sub(ul_ls_est[k + 1], ul_ch[k + 1]));

#ifdef DEBUG_PUSCH
          re_offset = (k0 + (n << 2) + (k_line << 1)) % symbolSize;
          printf("ch -> (%4d,%4d), ch_inter -> (%4d,%4d)\n", ul_ls_est[k].r, ul_ls_est[k].i, ul_ch[k].r, ul_ch[k].i);
#endif
          pilot_cnt++;
          nest_count += 2;
        }
      }

      // Align the channel estimates for the delta shift
      if (delta != 0) {
        c16_t *ul_ch_base = &ul_ch_estimates[nl * num_sp_streams + antenna][symbol_offset];
        memmove(&ul_ch_base[delta], ul_ch_base, (nb_rb_pusch * 12 - delta) * sizeof(c16_t));
        for (int d = 0; d < delta; d++)
          ul_ch_base[d] = ul_ch_base[delta];
      }

    } else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type2
               && chest_freq == 0) { // pusch_dmrs_type2  |p_r,p_l,d,d,d,d,p_r,p_l,d,d,d,d|
      LOG_D(PHY, "PUSCH estimation DMRS type 2, Freq-domain interpolation\n");
      c16_t *pil = pilot;
      c16_t *rx = &rxdataF[delta];
      for (int n = 0; n < nb_rb_pusch * NR_NB_SC_PER_RB; n += 6) {
        c16_t ch0 = c16mulShift(*pil, rx[(k0 + n) % symbolSize], 15);
        pil++;
        c16_t ch1 = c16mulShift(*pil, rx[(k0 + n + 1) % symbolSize], 15);
        pil++;
        c16_t ch = c16addShift(ch0, ch1, 1);
        *max_ch = max(*max_ch, max(abs(ch.r), abs(ch.i)));
        multadd_real_four_symbols_vector_complex_scalar(filt8_rep4, ch, &ul_ls_est[n]);
        ul_ls_est[n + 4] = ch;
        ul_ls_est[n + 5] = ch;
        noise_amp2 += c16amp2(c16sub(ch0, ch));
        nest_count += 1;
      }

      // Delay compensation
      c16_t ch_estimates_time[frame_parms->ofdm_symbol_size] __attribute__((aligned(32)));
      nr_est_delay(frame_parms->ofdm_symbol_size, ul_ls_est, ch_estimates_time, delay);
      if (rdata->scope && antenna == 0) {
        metadata mt = {.slot = -1, .frame = -1};
        scopeData_t *tmp = rdata->scope;
        tmp->copyData(tmp, gNBulDelay, ch_estimates_time, sizeof(c16_t), 1, frame_parms->ofdm_symbol_size, 0, &mt);
      }
      int delay_idx = get_delay_idx(-delay->est_delay, MAX_DELAY_COMP);
      c16_t *ul_delay_table = frame_parms->delay_table[delay_idx];
      for (int n = 0; n < nb_rb_pusch * NR_NB_SC_PER_RB; n++) {
        ul_ch[n] = c16mulShift(ul_ls_est[n], ul_delay_table[n % 6], 8);
      }

    }
    // this is case without frequency-domain linear interpolation, just take average of LS channel estimates of 6 DMRS REs and use a
    // common value for the whole PRB
    else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type1) {
      LOG_D(PHY, "PUSCH estimation DMRS type 1, no Freq-domain interpolation\n");
      c16_t *rxF = &rxdataF[delta];
      int pil_offset = 0;
      int re_offset = k0;
      c16_t ch;

      // First PRB
      ch = c32x16cumulVectVectWithSteps(pilot, &pil_offset, 1, rxF, &re_offset, 2, symbolSize, 6);

#if NO_INTERP
      for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
        *ul_ch = ch;
#else
      c16multaddVectRealComplex(filt8_avlip0, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip1, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip2, &ch, ul_ch, 8);
      ul_ch -= 12;
#endif

      for (int pilot_cnt = 6; pilot_cnt < 6 * (nb_rb_pusch - 1); pilot_cnt += 6) {
        ch = c32x16cumulVectVectWithSteps(pilot, &pil_offset, 1, rxF, &re_offset, 2, symbolSize, 6);
        *max_ch = max(*max_ch, max(abs(ch.r), abs(ch.i)));

#if NO_INTERP
        for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
          *ul_ch = ch;
#else
        ul_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
        ul_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384

        ul_ch += 4;
        c16multaddVectRealComplex(filt8_avlip3, &ch, ul_ch, 8);
        ul_ch += 8;
        c16multaddVectRealComplex(filt8_avlip4, &ch, ul_ch, 8);
        ul_ch += 8;
        c16multaddVectRealComplex(filt8_avlip5, &ch, ul_ch, 8);
        ul_ch -= 8;
#endif
      }
      // Last PRB
      ch = c32x16cumulVectVectWithSteps(pilot, &pil_offset, 1, rxF, &re_offset, 2, symbolSize, 6);

#if NO_INTERP
      for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
        *ul_ch = ch;
#else
      ul_ch[3].r += (ch.r * 1365) >> 15; // 1/12*16384
      ul_ch[3].i += (ch.i * 1365) >> 15; // 1/12*16384

      ul_ch += 4;
      c16multaddVectRealComplex(filt8_avlip3, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip6, &ch, ul_ch, 8);
#endif
    } else { // this is case without frequency-domain linear interpolation, just take average of LS channel estimates of 4 DMRS REs
             // and use a common value for the whole PRB
      LOG_D(PHY, "PUSCH estimation DMRS type 2, no Freq-domain interpolation");
      c16_t *pil = pilot;
      int re_offset = (k0 + delta) % symbolSize;
      c32_t ch0 = {0};
      // First PRB
      ch0 = c32x16mulShift(*pil, rxdataF[re_offset], 15);
      pil++;
      re_offset = (re_offset + 1) % symbolSize;
      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      pil++;
      re_offset = (re_offset + 5) % symbolSize;
      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      re_offset = (re_offset + 1) % symbolSize;
      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      pil++;
      re_offset = (re_offset + 5) % symbolSize;

      c16_t ch = c16x32div(ch0, 4);
#if NO_INTERP
      for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
        *ul_ch = ch;
#else
      c16multaddVectRealComplex(filt8_avlip0, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip1, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip2, &ch, ul_ch, 8);
      ul_ch -= 12;
#endif

      for (int pilot_cnt = 4; pilot_cnt < 4 * (nb_rb_pusch - 1); pilot_cnt += 4) {
        c32_t ch0;
        ch0 = c32x16mulShift(*pil, rxdataF[re_offset], 15);
        pil++;
        re_offset = (re_offset + 1) % symbolSize;

        ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
        pil++;
        re_offset = (re_offset + 5) % symbolSize;

        ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
        pil++;
        re_offset = (re_offset + 1) % symbolSize;

        ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
        pil++;
        re_offset = (re_offset + 5) % symbolSize;

        ch = c16x32div(ch0, 4);
        *max_ch = max(*max_ch, max(abs(ch.r), abs(ch.i)));

#if NO_INTERP
        for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
          *ul_ch = ch;
#else
        ul_ch[3] = c16maddShift(ch, (c16_t){1365, 1365}, (c16_t){0, 0}, 15); // 1365 = 1/12*16384 (full range is +/- 32768)
        ul_ch += 4;
        c16multaddVectRealComplex(filt8_avlip3, &ch, ul_ch, 8);
        ul_ch += 8;
        c16multaddVectRealComplex(filt8_avlip4, &ch, ul_ch, 8);
        ul_ch += 8;
        c16multaddVectRealComplex(filt8_avlip5, &ch, ul_ch, 8);
        ul_ch -= 8;
#endif
      }

      // Last PRB
      ch0 = c32x16mulShift(*pil, rxdataF[re_offset], 15);
      pil++;
      re_offset = (re_offset + 1) % symbolSize;

      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      pil++;
      re_offset = (re_offset + 5) % symbolSize;

      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      pil++;
      re_offset = (re_offset + 1) % symbolSize;

      ch0 = c32x16maddShift(*pil, rxdataF[re_offset], ch0, 15);
      pil++;
      re_offset = (re_offset + 5) % symbolSize;

      ch = c16x32div(ch0, 4);
#if NO_INTERP
      for (c16_t *end = ul_ch + 12; ul_ch < end; ul_ch++)
        *ul_ch = ch;
#else
      ul_ch[3] = c16maddShift(ch, (c16_t){1365, 1365}, (c16_t){0, 0}, 15); // 1365 = 1/12*16384 (full range is +/- 32768)
      ul_ch += 4;
      c16multaddVectRealComplex(filt8_avlip3, &ch, ul_ch, 8);
      ul_ch += 8;
      c16multaddVectRealComplex(filt8_avlip6, &ch, ul_ch, 8);
#endif
    }

#ifdef DEBUG_PUSCH
    ul_ch = &ul_ch_estimates[nl * num_sp_streams + aarx][symbol_offset];
    for (int idxP = 0; idxP < ceil((float)nb_rb_pusch * 12 / 8); idxP++) {
      for (int idxI = 0; idxI < 8; idxI++) {
        printf("%d\t%d\t", ul_ch[idxP * 8 + idxI].r, ul_ch[idxP * 8 + idxI].i);
      }
      printf("%d\n", idxP);
    }
#endif
    // update the values inside the arrays
    *(rdata->noise_amp2) = noise_amp2;
    *(rdata->nest_count) = nest_count;
  }
  completed_task_ans(rdata->ans);
}

int nr_pusch_channel_estimation(PHY_VARS_gNB *gNB,
                                unsigned char Ns,
                                int nl,
                                unsigned short p,
                                uint8_t lp,
                                unsigned char symbol,
                                NR_gNB_PUSCH *pusch_vars,
                                uint16_t ant_port_start,
                                unsigned short bwp_start_subcarrier,
                                const nfapi_nr_pusch_pdu_t *pusch_pdu,
                                int *max_ch,
                                uint32_t *nvar,
                                c16_t *pusch_dmrs_slot_mem,
                                c16_t *pusch_ch_est_dmrs_pos_slot_mem)
{
  c16_t pilot[3280] __attribute__((aligned(32)));

#ifdef DEBUG_CH
  FILE *debug_ch_est;
  debug_ch_est = fopen("debug_ch_est.txt", "w");
#endif

  const int nb_rb_pusch = pusch_pdu->rb_size;

  //------------------generate DMRS------------------//
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  if (pusch_pdu->transform_precoding == transformPrecoder_disabled) {
    // Note: pilot returned by the following function is already the complex conjugate of the transmitted DMRS
    const uint32_t *gold = nr_gold_pusch(fp->N_RB_UL,
                                         fp->symbols_per_slot,
                                         gNB->gNB_config.cell_config.phy_cell_id.value,
                                         pusch_pdu->scid,
                                         Ns,
                                         symbol);
    float beta_dmrs_pusch = get_beta_dmrs(pusch_pdu->num_dmrs_cdm_grps_no_data, pusch_pdu->dmrs_config_type == pusch_dmrs_type2);
    int16_t dmrs_scaling = (1 / beta_dmrs_pusch) * (1 << 14);
    nr_pusch_dmrs_rx(fp->Ncp,
                     gold,
                     pilot,
                     (1000 + p),
                     lp % 2,
                     nb_rb_pusch,
                     (pusch_pdu->bwp_start + pusch_pdu->rb_start) * NR_NB_SC_PER_RB,
                     pusch_pdu->dmrs_config_type,
                     dmrs_scaling);
  } else { // if transform precoding or SC-FDMA is enabled in Uplink
    // NR_SC_FDMA supports type1 DMRS so only 6 DMRS REs per RB possible
    const int index = get_index_for_dmrs_lowpapr_seq(nb_rb_pusch * (NR_NB_SC_PER_RB / 2));
    const uint8_t u = pusch_pdu->dfts_ofdm.low_papr_group_number;
    const uint8_t v = pusch_pdu->dfts_ofdm.low_papr_sequence_number;
    c16_t *dmrs_seq = gNB_dmrs_lowpaprtype1_sequence[u][v][index];
    LOG_D(PHY, "Transform Precoding params. u: %d, v: %d, index for dmrsseq: %d\n", u, v, index);
    AssertFatal(index >= 0,
                "Num RBs not configured according to 3GPP 38.211 section 6.3.1.4. For PUSCH with transform precoding, num RBs "
                "cannot be multiple of any other primenumber other than 2,3,5\n");
    AssertFatal(dmrs_seq != NULL, "DMRS low PAPR seq not found, check if DMRS sequences are generated");
    nr_pusch_lowpaprtype1_dmrs_rx(fp->Ncp, dmrs_seq, pilot, 1000, 0, nb_rb_pusch, 0, pusch_pdu->dmrs_config_type);
#ifdef DEBUG_PUSCH
    printf("NR_UL_CHANNEL_EST: index %d, u %d,v %d\n", index, u, v);
    LOG_M("gNb_DMRS_SEQ.m", "gNb_DMRS_SEQ", dmrs_seq, 6 * nb_rb_pusch, 1, 1);
#endif
  }
  //------------------------------------------------//

#ifdef DEBUG_PUSCH

  for (int i = 0; i < (6 * nb_rb_pusch); i++) {
    LOG_I(PHY, "In %s: %d + j*(%d)\n", __FUNCTION__, pilot[i].r, pilot[i].i);
  }

#endif

  //------------------Write DMRS to Memory for Data Recording ------------------//
  int dmrs_symbol_start_idx = symbol * pusch_pdu->nrOfLayers * nb_rb_pusch * NR_NB_SC_PER_RB + nl * nb_rb_pusch * NR_NB_SC_PER_RB;
#if T_TRACER
  if (T_ACTIVE(T_GNB_PHY_UL_FD_DMRS) && nl == 0) {
    // used by T-Tracer to trace DMRS slot grid
    int dmrs_delta = 0; // intialize it to zero currently, derive it later from above functions
    for (int i = 0; i < (6 * nb_rb_pusch); i++) {
      // the generated DMRs is a complex conjugate of mod table, so flip the sign of imag. part
      pusch_dmrs_slot_mem[dmrs_symbol_start_idx + dmrs_delta + i * 2].r = pilot[i].r; // 0, 2, 4, 6, 8, location of REs
      pusch_dmrs_slot_mem[dmrs_symbol_start_idx + dmrs_delta + i * 2].i = -pilot[i].i; // 0, 2, 4, 6, 8, location of REs
    }
  }
#endif

  int nest_count = 0;
  uint64_t noise_amp2 = 0;
  delay_t *delay = &pusch_vars->delay;
  memset(delay, 0, sizeof(*delay));

  int nb_antennas_rx = pusch_pdu->param_v4.numSpatialStreamIndices;
  delay_t delay_arr[nb_antennas_rx];
  uint64_t noise_amp2_arr[nb_antennas_rx];
  int max_ch_arr[nb_antennas_rx];
  int nest_count_arr[nb_antennas_rx];

  for (int i = 0; i < nb_antennas_rx; ++i) {
    max_ch_arr[i] = *max_ch;
    nest_count_arr[i] = nest_count;
    noise_amp2_arr[i] = noise_amp2;
    delay_arr[i] = *delay;
  }

  notifiedFIFO_t respPuschAarx;
  initNotifiedFIFO(&respPuschAarx);
  start_meas(&gNB->pusch_channel_estimation_antenna_processing_stats);
  int numAntennas = gNB->dmrs_num_antennas_per_thread;
  int num_jobs = CEILIDIV(nb_antennas_rx, numAntennas);
  puschAntennaProc_t rdatas[num_jobs];
  memset(rdatas, 0, sizeof(rdatas));
  task_ans_t ans;
  init_task_ans(&ans, num_jobs);
  for (int job_id = 0; job_id < num_jobs; job_id++) {
    puschAntennaProc_t *rdata = &rdatas[job_id];
    task_t task = {.func = nr_pusch_antenna_processing, .args = rdata};

    // Local init in the current loop
    rdata->Ns = Ns;
    rdata->nl = nl;
    rdata->p = p;
    rdata->symbol = symbol;
    rdata->aarx = job_id * numAntennas;
    rdata->numAntennas = numAntennas;
    rdata->bwp_start_subcarrier = bwp_start_subcarrier;
    rdata->pusch_pdu = pusch_pdu;
    rdata->max_ch = &max_ch_arr[rdata->aarx];
    rdata->pilot = pilot;
    rdata->nest_count = &nest_count_arr[rdata->aarx];
    rdata->noise_amp2 = &noise_amp2_arr[rdata->aarx];
    rdata->delay = &delay_arr[rdata->aarx];
    rdata->ant_port_start = ant_port_start;
    rdata->frame_parms = fp;
    rdata->pusch_vars = pusch_vars;
    rdata->chest_freq = gNB->chest_freq;
    rdata->rxdataF = gNB->common_vars.rxdataF;
    rdata->scope = gNB->scopeData;
    rdata->ans = &ans;
    rdata->pusch_ch_est_dmrs_pos_slot_mem = pusch_ch_est_dmrs_pos_slot_mem;
    rdata->dmrs_symbol_start_idx = dmrs_symbol_start_idx;
    // Call the nr_pusch_antenna_processing function
    if (job_id == num_jobs - 1) {
      // Run the last job inline
      nr_pusch_antenna_processing(rdata);
    } else {
      pushTpool(&gNB->threadPool, task);
    }
  } // Antenna Loop

  join_task_ans(&ans);

  stop_meas(&gNB->pusch_channel_estimation_antenna_processing_stats);
  for (int aarx = 0; aarx < nb_antennas_rx; aarx++) {
    *max_ch = max(*max_ch, max_ch_arr[aarx]);
    noise_amp2 += noise_amp2_arr[aarx];
    nest_count += nest_count_arr[aarx];
  }
  // get the maximum delay
  for (int aarx = 0; aarx < nb_antennas_rx; aarx++) {
    if (delay_arr[aarx].valid && delay_arr[aarx].delay_max_val > delay->delay_max_val) {
      *delay = delay_arr[aarx];
    }
  }

#ifdef DEBUG_CH
  fclose(debug_ch_est);
#endif

  if (nvar && nest_count > 0) {
    *nvar = (uint32_t)(noise_amp2 / nest_count);
  }

  return 0;
}

uint8_t get_srs_fd_cdm(const uint8_t N_ap, const uint8_t K_TC, uint8_t cyclic_shift)
{
  uint8_t fd_cdm = N_ap;
  switch (N_ap) {
    case 4:
      if ((K_TC == 2 && cyclic_shift >= 4) || (K_TC == 4 && cyclic_shift >= 6) || (K_TC == 8))
        fd_cdm = 2;
      break;
    case 8:
      if ((K_TC == 2 && cyclic_shift >= 4) || (K_TC == 4)) {
        fd_cdm = 4;
      } else if (K_TC == 8) {
        fd_cdm = 2;
      }
      break;
    default:
      break;
  }
  return fd_cdm;
}

// Hann window in Q15, from the cosine recurrence so there is one libm call per window.
static void srs_noise_window(int n, int16_t *w)
{
  const double c1 = cos(2.0 * M_PI / (n - 1));
  double a = 1.0;
  double b = c1;
  for (int i = 0; i < n; i++) {
    w[i] = (int16_t)lroundf((float)(16383.5 * (1.0 - a)));
    const double next = 2.0 * c1 * b - a;
    a = b;
    b = next;
  }
}

static int srs_noise_fft_size(int num_ls)
{
  int n = 64;
  while (n < num_ls && n < NR_SRS_NOISE_MAX_FFT)
    n <<= 1;
  return n;
}

// k-th smallest of a[0..n), reorders a so that a[0..k) <= a[k] <= a(k..n)
// Does not sort the whole array like qsort(), it only keeps splitting the part that contains the k-th value
static double quickselect_double(double *a, int n, int k)
{
  int lo = 0;
  int hi = n - 1;
  while (lo < hi) {
    const double pivot = a[(lo + hi) / 2];
    int l = lo;
    int h = hi;
    while (l <= h) {
      while (a[l] < pivot)
        l++;
      while (a[h] > pivot)
        h--;
      if (l <= h) {
        const double tmp = a[l];
        a[l++] = a[h];
        a[h--] = tmp;
      }
    }
    if (k <= h)
      hi = h;
    else if (k >= l)
      lo = l;
    else
      break;
  }
  return a[k];
}

// Compute noise from the delay domain.
// The channel sits in a few delay bins while the noise is spread over all of them, so the weakest bins hold noise only.
void nr_srs_noise_accumulate(int p_index,
                             const nfapi_nr_srs_pdu_t *srs_pdu,
                             const nr_srs_info_t *nr_srs_info,
                             const c16_t *srs_ls_estimated_channel,
                             nr_srs_noise_acc_t *acc,
                             nr_srs_prg_pow_t *pow_per_prg)
{
  const uint64_t subcarrier_offset = srs_pdu->bwp_start * NR_NB_SC_PER_RB;
  const uint8_t N_ap = 1 << srs_pdu->num_ant_ports;
  const uint8_t K_TC = 2 << srs_pdu->comb_size;
  const uint16_t m_SRS_b = get_m_srs(srs_pdu->config_index, srs_pdu->bandwidth_index);
  const uint16_t M_sc_b_SRS = m_SRS_b * NR_NB_SC_PER_RB / K_TC;
  const uint8_t fd_cdm = get_srs_fd_cdm(N_ap, K_TC, srs_pdu->cyclic_shift);
  const uint16_t prg_size = max((int)srs_pdu->beamforming.prg_size, 1);
  const uint16_t num_prgs = max((int)srs_pdu->beamforming.num_prgs, 1);

  const uint16_t first_sc = subcarrier_offset + nr_srs_info->k_0_p[p_index][0];
  const uint16_t step = K_TC * fd_cdm;

  // The per PRG signal power wants every estimate, the noise floor only needs enough delay bins
  const int all_ls = M_sc_b_SRS / fd_cdm;
  const int num_ls = min(all_ls, NR_SRS_NOISE_MAX_FFT);
  if (num_ls < NR_SRS_NOISE_MIN_ESTIMATES) {
    // every pair of the slot has the same num_ls, so no pair reads a floor and nr_srs_snr_per_prg returns early
    LOG_W(NR_PHY, "SRS: %d LS estimates, cannot read a noise floor\n", num_ls);
    return;
  }

  const int nfft = srs_noise_fft_size(num_ls);
  c16_t freq[NR_SRS_NOISE_MAX_FFT] __attribute__((aligned(32))) = {0};
  c16_t time[NR_SRS_NOISE_MAX_FFT] __attribute__((aligned(32))) = {0};
  if (acc->win_len != num_ls) {
    srs_noise_window(num_ls, acc->win);
    acc->win_sq = 0;
    for (int i = 0; i < num_ls; i++)
      acc->win_sq += (uint64_t)acc->win[i] * acc->win[i];
    acc->win_len = num_ls;
  }

  const int16_t *win = acc->win;
  const uint32_t prg_len = NR_NB_SC_PER_RB * prg_size;
  uint32_t offset = 0; // subcarrier distance from the first LS estimate
  int prg = 0;
  uint64_t pair_sum = 0; // LS power of this rx antenna and port pair, over the whole band
  uint64_t energy = 0; // sum of |ls*w|^2, the windowed power in LS units
  c16_t prev = {0, 0};
  for (int i = 0; i < all_ls; i++) {
    const c16_t ls = srs_ls_estimated_channel[first_sc + offset];

    // Neighbouring LS estimates sit K_TC*fd_cdm subcarriers apart, which is exactly the span the
    // despreading sums over, so their correlation measures how much of that sum is coherent.
    if (i) {
      acc->corr_re += (int64_t)ls.r * prev.r + (int64_t)ls.i * prev.i;
      acc->corr_im += (int64_t)ls.i * prev.r - (int64_t)ls.r * prev.i;
      acc->corr_cnt++;
    }
    prev = ls;
    while (prg < num_prgs - 1 && offset >= (prg + 1) * prg_len)
      prg++;
    const uint64_t p = c16amp2(ls);
    pow_per_prg[prg].pow += p;
    pow_per_prg[prg].cnt++;
    pair_sum += p;

    if (i < num_ls) {
      freq[i] = c16xmulConstShift(ls, win[i], 15);
      energy += c16amp2(freq[i]);
    }
    offset += step;
  }

  if (energy == 0 || acc->win_sq == 0)
    return;
  const uint64_t wsq = acc->win_sq;

  idft(get_idft(nfft), (int16_t *)freq, (int16_t *)time, 1);

  uint32_t power[NR_SRS_NOISE_MAX_FFT];
  uint64_t total = 0;
  for (int t = 0; t < nfft; t++) {
    power[t] = c16amp2(time[t]);
    total += power[t];
  }
  if (total == 0)
    return;

  // power[t]/total is the share of this bin, scaling it by the windowed energy puts it back in LS units,
  // so the bins of every antenna and port land on the same scale and simply add up.
  const double unit = (double)nfft * (double)energy * 1073741824.0 / (double)wsq / (double)total;
  for (int t = 0; t < nfft; t++)
    acc->spectrum[t] += power[t] * unit;

  if (acc->cnt < NR_SRS_NOISE_MAX_PAIRS && all_ls > 0)
    acc->pair_pow[acc->cnt] = (double)pair_sum / all_ls;

  acc->nfft = nfft;
  acc->num_ls = num_ls;
  acc->cnt++;
}

// A noise bin averaged over cnt antenna/port pairs follows Gamma(cnt)/cnt, so the percentile the floor
// is read at sits below its mean by a factor that depends only on cnt: Wilson-Hilferty in closed form.
static double srs_noise_quantile_scale(uint32_t cnt, int num_ls, double z)
{
  const double a = 1.0 / (9.0 * cnt);
  const double wh = 1.0 - a - z * sqrt(a);
  return wh * wh * wh * (1.0 + 5.0 / (num_ls * sqrt((double)cnt)));
}

// Fills the per PRG SNR and returns the wideband SNR
uint32_t nr_srs_snr_per_prg(const nfapi_nr_srs_pdu_t *srs_pdu,
                            uint32_t signal_power_avg,
                            nr_srs_noise_acc_t *acc,
                            const nr_srs_prg_pow_t *pow_per_prg,
                            int16_t *snr_per_prg)
{
  const uint8_t N_ap = 1 << srs_pdu->num_ant_ports;
  const uint8_t K_TC = 2 << srs_pdu->comb_size;
  const uint8_t fd_cdm = get_srs_fd_cdm(N_ap, K_TC, srs_pdu->cyclic_shift);
  const uint16_t num_prgs = max((int)srs_pdu->beamforming.num_prgs, 1);

  if (acc->cnt == 0 || acc->nfft == 0)
    return 1;

  // Only two percentiles are read. Dividing by cnt keeps the order, so only the two selected bins are divided
  double *bins = acc->spectrum;
  const int idx1 = (acc->nfft * NR_SRS_NOISE_QUANTILE_NUM) / NR_SRS_NOISE_QUANTILE_DEN;
  const int idx2 = (acc->nfft * NR_SRS_NOISE_QUANTILE2_NUM) / NR_SRS_NOISE_QUANTILE_DEN;
  // Two ranks are enough, no full sort: the first select leaves the 20th percentile bin at idx2 with all
  // smaller bins before it, the second finds the 10th percentile bin (the noise floor) among those
  const double q2_bin = quickselect_double(bins, acc->nfft, idx2) / acc->cnt;
  const double floor_bin = quickselect_double(bins, idx2, idx1) / acc->cnt;
  const double scale_q1 = srs_noise_quantile_scale(acc->cnt, acc->num_ls, NR_SRS_NOISE_QUANTILE_Z);

  // The percentile is noise plus whatever the channel leaves under that bin, so it is an upper
  // bound. A second percentile gives a second equation: the pedestal cancels in the difference,
  // leaving the noise times the difference of the two scales. That slope under-reads rather than
  // over-reads, so it is taken only when it lands below the bound, and never under a quarter of it.
  double noise = floor_bin / scale_q1;
  const double scale_q2 = srs_noise_quantile_scale(acc->cnt, acc->num_ls, NR_SRS_NOISE_QUANTILE2_Z);
  if (scale_q2 > scale_q1) {
    const double slope = (q2_bin - floor_bin) / (scale_q2 - scale_q1);
    if (slope < noise && slope > noise * NR_SRS_NOISE_SLOPE_MIN_RATIO)
      noise = slope;
  }
  const int noise_dB_x10 = dB_fixed_x10(max((uint32_t)noise, 1u));

  // The despreading only lifts the signal by fd_cdm where the channel is flat across the
  // K_TC*fd_cdm subcarriers it sums. Taking that on faith costs 10*log10(fd_cdm) over a channel
  // that decorrelates inside the span, so the gain is measured from the correlation between
  // neighbouring estimates: fd_cdm when the channel is flat, 1 when it is gone by the end.
  uint64_t band_pow = 0;
  uint32_t band_cnt = 0;
  for (int prg = 0; prg < num_prgs; prg++) {
    band_pow += pow_per_prg[prg].pow;
    band_cnt += pow_per_prg[prg].cnt;
  }
  double despread = fd_cdm;
  if (fd_cdm > 1 && acc->corr_cnt && band_cnt) {
    const double mean = (double)band_pow / band_cnt;
    const double r1 = sqrt((double)acc->corr_re * acc->corr_re + (double)acc->corr_im * acc->corr_im) / acc->corr_cnt;
    double rho = (mean > noise) ? r1 / (mean - noise) : 0.0;
    if (rho > 1.0)
      rho = 1.0;
    if (!(rho > 0.0))
      rho = 0.0;
    // rho is measured across the whole span, so one step inside it carries the fd_cdm-th root
    const double step_rho = pow(rho, 1.0 / fd_cdm);
    double sum = fd_cdm;
    for (int d = 1; d < fd_cdm; d++)
      sum += 2.0 * (fd_cdm - d) * pow(step_rho, d);
    despread = sum / fd_cdm;
    if (despread < 1.0)
      despread = 1.0;
    if (despread > fd_cdm)
      despread = fd_cdm;
  }

  // The LS power is averaged over every rx antenna and port pair, but a pair only carries signal
  // when its port reaches its antenna, so the mean is diluted by the ones that do not. Counting
  // the carriers against a threshold does not survive a timing offset: the delay breaks the port
  // orthogonality over the despreading span, and that leakage is a fixed fraction of the signal,
  // so it crosses any noise-referenced threshold once the SNR is high enough. The participation
  // ratio weighs each pair by its own power instead: C for C equal pairs, 1 when one holds
  // everything, and no threshold anywhere.
  double excess_sum = 0.0;
  double excess_sq = 0.0;
  if (acc->cnt <= NR_SRS_NOISE_MAX_PAIRS) {
    for (uint32_t i = 0; i < acc->cnt; i++) {
      const double s = acc->pair_pow[i] - noise;
      if (s > 0.0) {
        excess_sum += s;
        excess_sq += s * s;
      }
    }
  }
  double carrying = excess_sq > 0.0 ? excess_sum * excess_sum / excess_sq : (double)acc->cnt;
  if (!(carrying >= 1.0))
    carrying = 1.0;
  if (carrying > (double)acc->cnt)
    carrying = (double)acc->cnt;
  const double dilution = (double)acc->cnt / carrying;

  for (int prg = 0; prg < num_prgs; prg++) {
    nr_srs_prg_pow_t w = pow_per_prg[prg];
    int span = 0;

    while (w.cnt < NR_SRS_NOISE_PRG_MIN_ESTIMATES && span < num_prgs) {
      span++;
      for (int s = -1; s <= 1; s += 2) {
        const int j = prg + s * span;
        if (j >= 0 && j < num_prgs) {
          w.pow += pow_per_prg[j].pow;
          w.cnt += pow_per_prg[j].cnt;
        }
      }
    }

    // The LS estimates carry signal plus noise, so the noise comes off before the ratio. Around and
    // below 0 dB the two are within their own estimation error, so the difference can land at or
    // under zero, and dB_fixed_x10 takes a uint32_t. The floor is a ratio rather than an absolute
    // power, which also bounds what a single faded PRG can report.
    const double floor_signal = noise * NR_SRS_SNR_MIN_RATIO;
    const double raw = w.cnt ? ((double)w.pow / w.cnt - noise) * dilution / despread : 0.0;
    const double signal = max(raw, floor_signal);
    snr_per_prg[prg] = (dB_fixed_x10(signal) - noise_dB_x10) / 10.0;

    LOG_D(NR_PHY,
          "SRS-SNR-PRG: rnti %04x, prg %3d, noise %u (%d dB), snr %d\n",
          srs_pdu->rnti,
          prg,
          (uint32_t)noise,
          noise_dB_x10 / 10,
          snr_per_prg[prg]);
  }

  // The wideband SNR is the same estimator over the whole band: same mean, same noise, same scaling
  const double signal = max(((double)signal_power_avg - noise) * dilution / despread, noise * NR_SRS_SNR_MIN_RATIO);
  return (dB_fixed_x10(signal) - noise_dB_x10) / 10;
}

int nr_srs_ls_channel_estimation(int ant,
                                 int p_index,
                                 uint16_t ofdm_symbol_size,
                                 uint8_t N_symb_SRS,
                                 const nfapi_nr_srs_pdu_t *srs_pdu,
                                 const nr_srs_info_t *nr_srs_info,
                                 const c16_t *srs_generated_signal,
                                 c16_t srs_received_signal[ofdm_symbol_size * N_symb_SRS],
                                 c16_t srs_ls_estimated_channel[ofdm_symbol_size * N_symb_SRS],
                                 int64_t *ls_peak,
                                 delay_t *delay)
{
#ifdef SRS_DEBUG
  LOG_I(NR_PHY, "Calling %s function\n", __FUNCTION__);
#endif

  const uint64_t subcarrier_offset = srs_pdu->bwp_start * NR_NB_SC_PER_RB;

  const uint8_t N_ap = 1 << srs_pdu->num_ant_ports;
  const uint8_t K_TC = 2 << srs_pdu->comb_size;
  const uint16_t m_SRS_b = get_m_srs(srs_pdu->config_index, srs_pdu->bandwidth_index);
  const uint16_t M_sc_b_SRS = m_SRS_b * NR_NB_SC_PER_RB / K_TC;
  uint8_t fd_cdm = get_srs_fd_cdm(N_ap, K_TC, srs_pdu->cyclic_shift);

  int16_t scale = (N_ap > fd_cdm) ? N_ap / fd_cdm : 1;

  memset(srs_ls_estimated_channel, 0, ofdm_symbol_size * N_symb_SRS * sizeof(c16_t));

  for (int srs_symb = 0; srs_symb < N_symb_SRS; srs_symb++) {
    uint16_t srs_symbol_offset = srs_symb * ofdm_symbol_size;

#ifdef SRS_DEBUG
    LOG_I(NR_PHY, "====================== UE port %d --> gNB Rx antenna %i ======================\n", p_index, ant);
    LOG_I(NR_PHY, "============================== SRS symbol index %d ===========================\n", srs_symb);
#else
    UNUSED(ant);
#endif

    uint16_t subcarrier = subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb];

    c16_t ls_estimated = {0};
    for (int k = 0; k < M_sc_b_SRS; k++) {
      if (k % fd_cdm == 0) {
        ls_estimated = (c16_t){0, 0};
        uint16_t subcarrier_cdm = subcarrier;

        // The despreading sums fd_cdm products and the result is then scaled by N_ap/fd_cdm, so the
        // value on the way out can be several bits wider than one product. Accumulating in 64 bits
        // and narrowing once at the end keeps every intermediate exact, and the narrowing clamps
        // rather than wraps: a wrap would send the estimate to the opposite rail and that impulse
        // spreads over every delay bin, which is what the noise floor is read from.
        int64_t acc_re = 0;
        int64_t acc_im = 0;

        for (int cdm_idx = 0; cdm_idx < fd_cdm; cdm_idx++) {
          c16_t generated_srs = srs_generated_signal[srs_symbol_offset + subcarrier_cdm];
          c16_t received_srs = srs_received_signal[srs_symbol_offset + subcarrier_cdm];
          acc_re += (int64_t)generated_srs.r * received_srs.r + (int64_t)generated_srs.i * received_srs.i;
          acc_im += (int64_t)generated_srs.r * received_srs.i - (int64_t)generated_srs.i * received_srs.r;

          // Subcarrier increment
          subcarrier_cdm += K_TC;
        }

        // We know that nr_srs_info->srs_generated_signal_bits bits are enough to represent the real and imaginary parts of
        // generated_srs, so that shift takes the products back to the scale of the received signal. The scaling for
        // N_ap > fd_cdm : N_ap/fd_cdm then follows, before the one narrowing to 16 bits.
        acc_re = (acc_re >> nr_srs_info->srs_generated_signal_bits) * scale;
        acc_im = (acc_im >> nr_srs_info->srs_generated_signal_bits) * scale;

        // The clamp keeps a wrap from turning one estimate into an impulse across every delay
        // bin; the peak says how close the received level came to needing it.
        const int64_t mag = max(llabs(acc_re), llabs(acc_im));
        if (mag > *ls_peak)
          *ls_peak = mag;
        ls_estimated = (c16_t){saturate_int16(acc_re), saturate_int16(acc_im)};
      }

      for (int ktc = 0; ktc < K_TC && srs_symbol_offset + subcarrier + ktc < ofdm_symbol_size * N_symb_SRS; ktc++) {
        srs_ls_estimated_channel[srs_symbol_offset + subcarrier + ktc] = ls_estimated;
      }

#ifdef SRS_DEBUG
      int subcarrier_log = subcarrier - subcarrier_offset;
      if (subcarrier_log % 12 == 0) {
        LOG_I(NR_PHY, "------------------------------------ %d ------------------------------------\n", subcarrier_log / 12);
        LOG_I(NR_PHY, "\t  __genRe________genIm__|____rxRe_________rxIm__|____lsRe________lsIm_\n");
      }
      LOG_I(NR_PHY,
            "(%4i) %6i\t%6i  |  %6i\t%6i  |  %6i\t%6i\n",
            subcarrier_log,
            srs_generated_signal[srs_symbol_offset + subcarrier].r,
            srs_generated_signal[srs_symbol_offset + subcarrier].i,
            srs_received_signal[srs_symbol_offset + subcarrier].r,
            srs_received_signal[srs_symbol_offset + subcarrier].i,
            ls_estimated.r,
            ls_estimated.i);
#endif

      // Subcarrier increment
      subcarrier += K_TC;
    } // for (int k = 0; k < M_sc_b_SRS; k++)

    // Delay estimation
    if (srs_symb == 0) {
      c16_t ch_estimates_time[ofdm_symbol_size] __attribute__((aligned(32)));
      nr_est_delay(ofdm_symbol_size, srs_ls_estimated_channel, ch_estimates_time, delay);
    }
  } // for (int srs_symb = 0; srs_symb < N_symb_SRS; srs_symb++)

  return 0;
}

int nr_srs_channel_interpolation(int p_index,
                                 uint16_t ofdm_symbol_size,
                                 uint16_t first_carrier_offset,
                                 uint8_t N_symb_SRS,
                                 const nfapi_nr_srs_pdu_t *srs_pdu,
                                 const nr_srs_info_t *nr_srs_info,
                                 const c16_t srs_ls_estimated_channel[ofdm_symbol_size * N_symb_SRS],
                                 int est_delay,
                                 c16_t srs_estimated_channel_freq[ofdm_symbol_size * N_symb_SRS],
                                 c16_t srs_estimated_channel_time[NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                 c16_t srs_estimated_channel_time_shifted[NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                 uint32_t *signal_power,
                                 c16_t delay_table[2 * MAX_DELAY_COMP + 1][NR_MAX_OFDM_SYMBOL_SIZE])
{
#ifdef SRS_DEBUG
  LOG_I(NR_PHY, "Calling %s function\n", __FUNCTION__);
#endif

  const uint64_t subcarrier_offset = srs_pdu->bwp_start * NR_NB_SC_PER_RB;

  const uint8_t K_TC = 2 << srs_pdu->comb_size;
  const uint16_t m_SRS_b = get_m_srs(srs_pdu->config_index, srs_pdu->bandwidth_index);
  const uint16_t M_sc_b_SRS = m_SRS_b * NR_NB_SC_PER_RB / K_TC;

  c16_t srs_estimated_channel_freq_avg[ofdm_symbol_size];
  memset(srs_estimated_channel_freq_avg, 0, ofdm_symbol_size * sizeof(c16_t));

  for (int srs_symb = 0; srs_symb < N_symb_SRS; srs_symb++) {
    uint16_t srs_symbol_offset = srs_symb * ofdm_symbol_size;

#ifdef SRS_DEBUG
    LOG_I(NR_PHY, "============================== UE port %d ====================================\n", p_index);
    LOG_I(NR_PHY, "============================== SRS symbol index %d ===========================\n", srs_symb);
#endif

    c16_t srs_est[ofdm_symbol_size] __attribute__((aligned(32)));
    memset(srs_est, 0, (ofdm_symbol_size) * sizeof(c16_t));

    // Start of buffer is 32 byte aligned.
    uint16_t subcarrier_abs = 0;
    c16_t *srs_estimated_channel16 = &srs_est[subcarrier_abs];

    uint16_t subcarrier = subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb];

    int delay_idx = get_delay_idx(est_delay, MAX_DELAY_COMP);
    const c16_t *srs_delay_table = delay_table[delay_idx];

    // Delay table might be FFT shift sensitive. Not sure.
    uint16_t subcarrier_delay =
        CIRCULAR_INC(first_carrier_offset, subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb], ofdm_symbol_size);
    for (int k = 0; k < M_sc_b_SRS; k++) {
      // Apply delay
      c16_t ls_estimated =
          c16mulShift(srs_ls_estimated_channel[srs_symbol_offset + subcarrier], srs_delay_table[subcarrier_delay], 8);

      // Channel interpolation
      if (srs_pdu->comb_size == 0) {
        if (k == 0) { // First subcarrier case
          // filt8_start is {12288,8192,4096,0,0,0,0,0}
          c16multaddVectRealComplex(filt8_start, &ls_estimated, srs_estimated_channel16, 8);
        } else if (k == (M_sc_b_SRS - 1)) { // End of OFDM symbol or last subcarrier cases
          // filt8_end is {4096,8192,12288,16384,0,0,0,0}
          c16multaddVectRealComplex(filt8_end, &ls_estimated, srs_estimated_channel16, 8);
        } else if (k % 2 == 1) { // 1st middle case
          // filt8_middle2 is {4096,8192,8192,8192,4096,0,0,0}
          c16multaddVectRealComplex(filt8_middle2, &ls_estimated, srs_estimated_channel16, 8);
        } else if (k % 2 == 0) { // 2nd middle case
          // filt8_middle4 is {0,0,4096,8192,8192,8192,4096,0}
          c16multaddVectRealComplex(filt8_middle4, &ls_estimated, srs_estimated_channel16, 8);
          srs_estimated_channel16 = &srs_est[subcarrier_abs];
        }
      } else {
        if (k == 0) { // First subcarrier case
          // filt16_start is {12288,8192,8192,8192,4096,0,0,0,0,0,0,0,0,0,0,0}
          c16multaddVectRealComplex(filt16_start, &ls_estimated, srs_estimated_channel16, 16);
        } else if (k == (M_sc_b_SRS - 1)) { // End of OFDM symbol or last subcarrier cases
          // filt16_end is {4096,8192,8192,8192,12288,16384,16384,16384,0,0,0,0,0,0,0,0}
          c16multaddVectRealComplex(filt16_end, &ls_estimated, srs_estimated_channel16, 16);
        } else { // Middle case
          // filt16_middle4 is {4096,8192,8192,8192,8192,8192,8192,8192,4096,0,0,0,0,0,0,0}
          c16multaddVectRealComplex(filt16_middle4, &ls_estimated, srs_estimated_channel16, 16);
          srs_estimated_channel16 = &srs_est[subcarrier_abs];
        }
      }

      // Subcarrier increment
      subcarrier += K_TC;
      subcarrier_delay = CIRCULAR_INC(subcarrier_delay, K_TC, ofdm_symbol_size);
      subcarrier_abs += K_TC;
    } // for (int k = 0; k < M_sc_b_SRS; k++)

    // Revert delay
    int inv_delay_idx = get_delay_idx(-est_delay, MAX_DELAY_COMP);
    const c16_t *srs_inv_delay_table = delay_table[inv_delay_idx];
    subcarrier_abs = 0;
    subcarrier_delay =
        CIRCULAR_INC(first_carrier_offset, subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb], ofdm_symbol_size);

    for (int k = 0; k < K_TC * M_sc_b_SRS; k++) {
      srs_est[subcarrier_abs] = c16mulShift(srs_est[subcarrier_abs], srs_inv_delay_table[subcarrier_delay], 8);
      // Subcarrier increment
      subcarrier_delay = CIRCULAR_INC(subcarrier_delay, 1, ofdm_symbol_size);
      subcarrier_abs++;
    }

    // Copy as DC in center.
    const uint half_bw = ofdm_symbol_size - first_carrier_offset;
    const uint neg_start = (ofdm_symbol_size >> 1) - half_bw + subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb];
    memset(&srs_estimated_channel_freq[srs_symbol_offset], 0, sizeof(c16_t) * neg_start);
    memcpy(&srs_estimated_channel_freq[srs_symbol_offset + neg_start],
           srs_est,
           (ofdm_symbol_size - neg_start) * sizeof(c16_t));

    // Average srs channel estimates over multiple symbols
    int16_t scale_factor = (1 << 15) / N_symb_SRS;
    multadd_complex_vector_real_scalar(&srs_estimated_channel_freq[srs_symbol_offset],
                                       scale_factor,
                                       srs_estimated_channel_freq_avg,
                                       ofdm_symbol_size);

#ifdef SRS_DEBUG
    subcarrier = subcarrier_offset + nr_srs_info->k_0_p[p_index][srs_symb];
    subcarrier_abs = neg_start;

    for (int k = 0; k < K_TC * M_sc_b_SRS; k++) {
      int subcarrier_log = subcarrier - subcarrier_offset;

      if (k == 0 || subcarrier_log % NR_NB_SC_PER_RB == 0) {
        LOG_I(NR_PHY,
              "---------------------------- PRB %d, symbol %d -------------------------------\n",
              subcarrier_log / NR_NB_SC_PER_RB,
              srs_symb);
        LOG_I(NR_PHY, "\t  __lsRe__________lsIm__|____intRe_______intIm__\n");
      }

      LOG_I(NR_PHY,
            "(%4i) %6i\t%6i  |  %6i\t%6i\n",
            subcarrier_log,
            srs_ls_estimated_channel[srs_symbol_offset + subcarrier].r,
            srs_ls_estimated_channel[srs_symbol_offset + subcarrier].i,
            srs_estimated_channel_freq[srs_symbol_offset + subcarrier_abs].r,
            srs_estimated_channel_freq[srs_symbol_offset + subcarrier_abs].i);

      // Subcarrier increment
      subcarrier++;
      subcarrier_abs++;
    }
#endif
  }

  // Convert to time domain
  int16_t ofdm_symbol_size_half = ofdm_symbol_size >> 1;
  int16_t ofdm_os_size = NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size;
  int16_t ofdm_os_size_half = ofdm_os_size >> 1;
  int16_t start_offset = ofdm_os_size - ofdm_symbol_size_half;

  c16_t chF_interpol[ofdm_os_size] __attribute__((aligned(32)));
  memset(chF_interpol, 0, sizeof(chF_interpol));

  // Place SRS channel estimates in FFT shifted format for oversampling
  memcpy(&chF_interpol[0], &srs_estimated_channel_freq_avg[ofdm_symbol_size_half], ofdm_symbol_size_half * sizeof(c16_t));
  memcpy(&chF_interpol[start_offset], &srs_estimated_channel_freq_avg[0], ofdm_symbol_size_half * sizeof(c16_t));

  // Convert to time domain oversampled
  freq2time(ofdm_os_size, (int16_t *)chF_interpol, (int16_t *)srs_estimated_channel_time);

  // Do FFT shift
  memcpy(srs_estimated_channel_time_shifted, &srs_estimated_channel_time[ofdm_os_size_half], ofdm_os_size_half * sizeof(c16_t));

  memcpy(&srs_estimated_channel_time_shifted[ofdm_os_size_half], srs_estimated_channel_time, ofdm_os_size_half * sizeof(c16_t));

  // Compute wideband SNR on the symbol 0
  int tot_subcarriers = m_SRS_b * NR_NB_SC_PER_RB;
  uint16_t subcarrier_abs = subcarrier_offset + nr_srs_info->k_0_p[p_index][0];
  *signal_power = signal_energy_nodc(&srs_ls_estimated_channel[subcarrier_abs], tot_subcarriers);

  if (*signal_power == 0) {
    LOG_W(NR_PHY, "Received SRS signal power is 0\n");
    return -1;
  }

  return 0;
}
