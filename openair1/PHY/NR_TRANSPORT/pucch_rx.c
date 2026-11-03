/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level routines for decoding the PUCCH physical channel
 */
#include "PHY/defs_RU.h"
#include "nr_common.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_gNB.h"
#include "PHY/sse_intrin.h"
#include "PHY/CODING/nrSmallBlock/nr_small_block_defs.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "common/utils/LOG/log.h"
#include "nr_sequences_tables.h"
#include "SCHED_NR/sched_nr.h"
#include "bits.h"

#include "nr_phy_common.h"

//#define DEBUG_NR_PUCCH_RX 1
void nr_fill_pucch(PHY_VARS_gNB *gNB, int frame, int slot, nfapi_nr_pucch_pdu_t *pucch_pdu)
{
  LOG_D(PHY,
        "%4d.%2d UE %04x Programming PUCCH format %d, nb_harq %d, nb_sr %d, nb_csi %d\n",
        frame,
        slot,
        pucch_pdu->rnti,
        pucch_pdu->format_type,
        pucch_pdu->bit_len_harq,
        pucch_pdu->sr_flag,
        pucch_pdu->bit_len_csi_part1);
  NR_gNB_PUCCH_job_t pucch = {.frame = frame, .slot = slot, .pucch_pdu = *pucch_pdu};
  if (gNB->common_vars.beam_id) {
    int fapi_beam_idx = pucch_pdu->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx;
    int bitmap = SL_to_bitmap(pucch_pdu->start_symbol_index, pucch_pdu->nr_of_symbols);
    const nfapi_nr_spatial_stream_index_t *p = &pucch_pdu->param_v4;
    const uint16_t ant_port = p->numSpatialStreamIndices > 0 ? p->spatialStreamIndices[0] : 0;
    beam_index_allocation(fapi_beam_idx, ant_port, 1, NR_SYMBOLS_PER_SLOT, slot, bitmap, gNB->frame_parms.nb_antennas_rx, gNB->common_vars.beam_id);
  }
  bool found = spsc_q_put(&gNB->pucch_queue, &pucch, sizeof(pucch));
  if (!found)
    LOG_W(NR_PHY, "PUCCH list is full: dropping PUCCH UE %04x\n", pucch_pdu->rnti);
}

int get_pucch0_cs_lut_index(PHY_VARS_gNB *gNB, const nfapi_nr_pucch_pdu_t *pucch_pdu)
{
#ifdef DEBUG_NR_PUCCH_RX
  printf("getting index for LUT with %d entries, Nid %d\n", gNB->pucch0_lut.nb_id, pucch_pdu->hopping_id);
#endif

  int i;
  for (i = 0; i < gNB->pucch0_lut.nb_id; i++) {
    if (gNB->pucch0_lut.Nid[i] == pucch_pdu->hopping_id)
      break;
  }

  if (i < gNB->pucch0_lut.nb_id) {
#ifdef DEBUG_NR_PUCCH_RX
  printf("found index %d\n", i);
#endif
  return (i);
  }

#ifdef DEBUG_NR_PUCCH_RX
  printf("Initializing PUCCH0 LUT index %i with Nid %d\n", i, pucch_pdu->hopping_id);
#endif
  // initialize
  gNB->pucch0_lut.Nid[gNB->pucch0_lut.nb_id] = pucch_pdu->hopping_id;
  for (int slot = 0; slot < 10 << pucch_pdu->subcarrier_spacing; slot++)
    for (int symbol = 0; symbol < 14; symbol++)
      gNB->pucch0_lut.lut[gNB->pucch0_lut.nb_id][slot][symbol] =
          (int)floor(nr_cyclic_shift_hopping(pucch_pdu->hopping_id, 0, 0, symbol, 0, slot) / 0.5235987756);
  gNB->pucch0_lut.nb_id++;
  return (gNB->pucch0_lut.nb_id - 1);
}

static const int16_t idft12_re[12][12] = {
    {23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170},
    {23170, 20066, 11585, 0, -11585, -20066, -23170, -20066, -11585, 0, 11585, 20066},
    {23170, 11585, -11585, -23170, -11585, 11585, 23170, 11585, -11585, -23170, -11585, 11585},
    {23170, 0, -23170, 0, 23170, 0, -23170, 0, 23170, 0, -23170, 0},
    {23170, -11585, -11585, 23170, -11585, -11585, 23170, -11585, -11585, 23170, -11585, -11585},
    {23170, -20066, 11585, 0, -11585, 20066, -23170, 20066, -11585, 0, 11585, -20066},
    {23170, -23170, 23170, -23170, 23170, -23170, 23170, -23170, 23170, -23170, 23170, -23170},
    {23170, -20066, 11585, 0, -11585, 20066, -23170, 20066, -11585, 0, 11585, -20066},
    {23170, -11585, -11585, 23170, -11585, -11585, 23170, -11585, -11585, 23170, -11585, -11585},
    {23170, 0, -23170, 0, 23170, 0, -23170, 0, 23170, 0, -23170, 0},
    {23170, 11585, -11585, -23170, -11585, 11585, 23170, 11585, -11585, -23170, -11585, 11585},
    {23170, 20066, 11585, 0, -11585, -20066, -23170, -20066, -11585, 0, 11585, 20066}};

static const int16_t idft12_im[12][12] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                          {0, 11585, 20066, 23170, 20066, 11585, 0, -11585, -20066, -23170, -20066, -11585},
                                          {0, 20066, 20066, 0, -20066, -20066, 0, 20066, 20066, 0, -20066, -20066},
                                          {0, 23170, 0, -23170, 0, 23170, 0, -23170, 0, 23170, 0, -23170},
                                          {0, 20066, -20066, 0, 20066, -20066, 0, 20066, -20066, 0, 20066, -20066},
                                          {0, 11585, -20066, 23170, -20066, 11585, 0, -11585, 20066, -23170, 20066, -11585},
                                          {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                          {0, -11585, 20066, -23170, 20066, -11585, 0, 11585, -20066, 23170, -20066, 11585},
                                          {0, -20066, 20066, 0, -20066, 20066, 0, -20066, 20066, 0, -20066, 20066},
                                          {0, -23170, 0, 23170, 0, -23170, 0, 23170, 0, -23170, 0, 23170},
                                          {0, -20066, -20066, 0, 20066, 20066, 0, -20066, -20066, 0, 20066, 20066},
                                          {0, -11585, -20066, -23170, -20066, -11585, 0, 11585, 20066, 23170, 20066, 11585}};
//************************************************************************//
void nr_decode_pucch0(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_pdu,
                      const nfapi_nr_pucch_pdu_t *pucch_pdu)
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const int nb_symbols = pucch_pdu->nr_of_symbols;
  const int symb_sz = frame_parms->ofdm_symbol_size;
  int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * symb_sz;

  AssertFatal(pucch_pdu->bit_len_harq > 0 || pucch_pdu->sr_flag > 0,
              "Either bit_len_harq (%d) or sr_flag (%d) must be > 0\n",
              pucch_pdu->bit_len_harq,
              pucch_pdu->sr_flag);

  /* it might be that the stats list is full: In this case, we will simply
   * write to some memory on the stack instead of the UE's UCI stats */
  NR_gNB_UCI_STATS_t stack_uci_stats = {0};
  NR_gNB_UCI_STATS_t *uci_stats = &stack_uci_stats;
  NR_gNB_PHY_STATS_t *phy_stats = get_phy_stats(gNB, pucch_pdu->rnti);
  if (phy_stats != NULL) {
    phy_stats->frame = frame;
    uci_stats = &phy_stats->uci_stats;
  }

  int nr_sequences;
  const uint8_t *mcs;
  if (pucch_pdu->bit_len_harq == 0) {
    mcs = table1_mcs;
    nr_sequences = 1;
  } else if (pucch_pdu->bit_len_harq == 1) {
    mcs = table1_mcs;
    nr_sequences = 4 >> (1 - pucch_pdu->sr_flag);
  } else {
    mcs = table2_mcs;
    nr_sequences = 8 >> (1 - pucch_pdu->sr_flag);
  }

  LOG_D(PHY,
        "pucch0: nr_symbols %d, start_symbol %d, prb_start %d, second_hop_prb %d,  group_hop_flag %d, sequence_hop_flag %d, O_ACK "
        "%d, O_SR %d, mcs %d initial_cyclic_shift %d\n",
        nb_symbols,
        pucch_pdu->start_symbol_index,
        pucch_pdu->prb_start,
        pucch_pdu->second_hop_prb,
        pucch_pdu->group_hop_flag,
        pucch_pdu->sequence_hop_flag,
        pucch_pdu->bit_len_harq,
        pucch_pdu->sr_flag,
        mcs[0],
        pucch_pdu->initial_cyclic_shift);

  int cs_ind = get_pucch0_cs_lut_index(gNB, pucch_pdu);
  /*
   * Implement TS 38.211 Subclause 6.3.2.3.1 Sequence generation
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  int prb_offset[2] = {pucch_pdu->bwp_start + pucch_pdu->prb_start, pucch_pdu->bwp_start + pucch_pdu->prb_start};

  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag << 1);
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according
  // to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u[2] = {0}, v[2] = {0};
  nr_group_sequence_hopping(pucch_GroupHopping, pucch_pdu->hopping_id, 0, slot, u,
                            v); // calculating u and v value first hop
  LOG_D(PHY, "pucch0: u %d, v %d\n", u[0], v[0]);

  if (pucch_pdu->freq_hop_flag == 1) {
    nr_group_sequence_hopping(pucch_GroupHopping,
                              pucch_pdu->hopping_id,
                              1,
                              slot,
                              &u[1],
                              &v[1]); // calculating u and v value second hop
    LOG_D(PHY, "pucch0 second hop: u %d, v %d\n", u[1], v[1]);
    prb_offset[1] = pucch_pdu->bwp_start + pucch_pdu->second_hop_prb;
  }

  AssertFatal(nb_symbols < 3, "nr_of_symbols %d not allowed\n", nb_symbols);
  uint32_t re_offset[2] = {0};

  const int16_t *x_re[2], *x_im[2];
  x_re[0] = table_5_2_2_2_2_Re[u[0]];
  x_im[0] = table_5_2_2_2_2_Im[u[0]];
  // TS 38.211, 6.3.2.2.1: both non-hopping symbols use frequency-hop index 0.
  const unsigned second_group = u[pucch_pdu->freq_hop_flag ? 1 : 0];
  x_re[1] = table_5_2_2_2_2_Re[second_group];
  x_im[1] = table_5_2_2_2_2_Im[second_group];

  const uint8_t num_sp_streams = pucch_pdu->param_v4.numSpatialStreamIndices;

  int64_t xrtmag = 0, xrtmag_next = 0;
  uint8_t maxpos = 0;
  uint8_t index = 0;

  const int nb_re_pucch = NR_NB_SC_PER_RB * pucch_pdu->prb_size; // prb size is 1
  int64_t signal_energy = 0, signal_energy_ant0 = 0;

  c64_t xr[num_sp_streams][nb_symbols][nb_re_pucch] __attribute__((aligned(32)));
  for (int l = 0; l < nb_symbols; l++) {
    uint8_t l2 = l + pucch_pdu->start_symbol_index;

    re_offset[l] = NR_NB_SC_PER_RB * prb_offset[l];

    for (int aa = 0; aa < num_sp_streams; aa++) {
      c16_t *tmp_rp = &rxdataF[aa][soffset + l2 * frame_parms->ofdm_symbol_size];
      c16_t *rp = tmp_rp + re_offset[l];

      for (int n = 0; n < nb_re_pucch; n++) {
        xr[aa][l][n].r = (int32_t)x_re[l][n] * rp[n].r + (int32_t)x_im[l][n] * rp[n].i;
        xr[aa][l][n].i = (int32_t)x_re[l][n] * rp[n].i - (int32_t)x_im[l][n] * rp[n].r;
#ifdef DEBUG_NR_PUCCH_RX
        printf("x (%d,%d), xr (%ld,%ld)\n", x_re[l][n], x_im[l][n], xr[aa][l][n].r, xr[aa][l][n].i);
#endif
      }
      int energ = signal_energy_nodc(rp, nb_re_pucch);
      signal_energy += energ;
      if (aa == 0)
        signal_energy_ant0 += energ;
    }
  }
  signal_energy /= nb_symbols * num_sp_streams;
  signal_energy_ant0 /= nb_symbols;
  int pucch_power_dBtimes10 = 10 * dB_fixed(signal_energy);

  int seq_index = 0;

  for (int i = 0; i < nr_sequences; i++) {
    c64_t corr[num_sp_streams][2];
    for (int aa = 0; aa < num_sp_streams; aa++) {
      for (int l = 0; l < nb_symbols; l++) {
        seq_index =
            (pucch_pdu->initial_cyclic_shift + mcs[i] + gNB->pucch0_lut.lut[cs_ind][slot][l + pucch_pdu->start_symbol_index])
            % NR_NB_SC_PER_RB;
#ifdef DEBUG_NR_PUCCH_RX
        printf("PUCCH symbol %d seq %d, seq_index %d, mcs %d\n", l, i, seq_index, mcs[i]);
#endif
        corr[aa][l] = (c64_t){0};
        for (int n = 0; n < NR_NB_SC_PER_RB; n++) {
          corr[aa][l].r += xr[aa][l][n].r * idft12_re[seq_index][n] + xr[aa][l][n].i * idft12_im[seq_index][n];
          corr[aa][l].i += xr[aa][l][n].r * idft12_im[seq_index][n] - xr[aa][l][n].i * idft12_re[seq_index][n];
        }
        corr[aa][l].r >>= 31;
        corr[aa][l].i >>= 31;
      }
    }
    LOG_D(PHY,
          "PUCCH IDFT[%d/%d] = (%ld,%ld)=>%f\n",
          mcs[i],
          seq_index,
          corr[0][0].r,
          corr[0][0].i,
          10 * log10((double)squaredMod(corr[0][0])));
    if (nb_symbols == 2)
      LOG_D(PHY,
            "PUCCH 2nd symbol IDFT[%d/%d] = (%ld,%ld)=>%f\n",
            mcs[i],
            seq_index,
            corr[0][1].r,
            corr[0][1].i,
            10 * log10((double)squaredMod(corr[0][1])));
    int64_t temp = 0;
    if (pucch_pdu->freq_hop_flag == 0) {
      if (nb_symbols == 1) { // non-coherent correlation
        for (int aa = 0; aa < num_sp_streams; aa++)
          temp += squaredMod(corr[aa][0]);
      } else {
        for (int aa = 0; aa < num_sp_streams; aa++) {
          c64_t corr2;
          csum(corr2, corr[aa][0], corr[aa][1]);
          // coherent combining of 2 symbols and then complex modulus for
          // single-frequency case
          temp += squaredMod(corr2);
        }
      }
    } else {
      // full non-coherent combining of 2 symbols for frequency-hopping case
      for (int aa = 0; aa < num_sp_streams; aa++)
        temp += squaredMod(corr[aa][0]) + squaredMod(corr[aa][1]);
    }

    if (temp > xrtmag) {
      xrtmag_next = xrtmag;
      xrtmag = temp;
      LOG_D(PHY, "Sequence %d xrtmag %ld xrtmag_next %ld\n", i, xrtmag, xrtmag_next);
      maxpos = i;
      uci_stats->current_pucch0_stat0 = 0;
      int64_t temp2 = 0, temp3 = 0;
      for (int aa = 0; aa < num_sp_streams; aa++) {
        temp2 += squaredMod(corr[aa][0]);
        if (nb_symbols == 2)
          temp3 += squaredMod(corr[aa][1]);
      }
      uci_stats->current_pucch0_stat0 = dB_fixed64(temp2);
      if (nb_symbols == 2)
        uci_stats->current_pucch0_stat1 = dB_fixed64(temp3);
    } else if (temp > xrtmag_next)
      xrtmag_next = temp;
  }

  int xrtmag_dBtimes10 = 10 * (int)dB_fixed64(xrtmag / (12 * nb_symbols));
  int xrtmag_next_dBtimes10 = 10 * (int)dB_fixed64(xrtmag_next / (12 * nb_symbols));
#ifdef DEBUG_NR_PUCCH_RX
  printf("PUCCH 0 : maxpos %d\n", maxpos);
#endif

  index = maxpos;
  int pucch0_n00 = gNB->measurements.n0_subband_power_tot_dB[prb_offset[0]];
  int pucch0_n01 = gNB->measurements.n0_subband_power_tot_dB[prb_offset[1]];
  LOG_D(PHY, "n00[%d] = %d, n01[%d] = %d\n", prb_offset[0], pucch0_n00, prb_offset[1], pucch0_n01);

  uci_stats->pucch0_n00 = pucch0_n00;
  uci_stats->pucch0_n01 = pucch0_n01;
  uci_stats->pucch0_thres = gNB->pucch0_thres;

  // estimate CQI for MAC (from antenna port 0 only)
  int max_n0 =
      max(gNB->measurements.n0_subband_power_tot_dB[prb_offset[0]], gNB->measurements.n0_subband_power_tot_dB[prb_offset[1]]);
  const int SNRtimes10 = pucch_power_dBtimes10 - (10 * max_n0);
  int cqi;
  if (SNRtimes10 < -640)
    cqi = 0;
  else if (SNRtimes10 > 635)
    cqi = 255;
  else
    cqi = (640 + SNRtimes10) / 5;

  bool no_conf = false;
  if (nr_sequences > 1) {
    if (/*xrtmag_dBtimes10 < (30+xrtmag_next_dBtimes10) ||*/ SNRtimes10 < gNB->pucch0_thres) {
      no_conf = true;
      LOG_D(PHY,
            "%d.%d PUCCH bad confidence: %d threshold, %d, %d, %d\n",
            frame,
            slot,
            gNB->pucch0_thres,
            SNRtimes10,
            xrtmag_dBtimes10,
            xrtmag_next_dBtimes10);
    }
  }
  gNB->bad_pucch += no_conf;
  // first bit of bitmap for sr presence and second bit for acknack presence
  uci_pdu->pduBitmap = pucch_pdu->sr_flag | ((pucch_pdu->bit_len_harq > 0) << 1);
  uci_pdu->pucch_format = 0; // format 0
  uci_pdu->rnti = pucch_pdu->rnti;
  uci_pdu->ul_cqi = cqi;
  uci_pdu->timing_advance = 0xffff; // currently not valid
  // dB_fixed_times10() gives -900 for zero energy and saturates at 900. rssi value is never > 1280
  int16_t rssi_raw = 1280 - (10 * dB_fixed(INT16_MAX * INT16_MAX) - dB_fixed_times10(signal_energy));
  uci_pdu->rssi = (uint16_t)max(0, rssi_raw);

  if (pucch_pdu->bit_len_harq == 0) {
    uci_pdu->sr.sr_confidence_level = SNRtimes10 < gNB->pucch0_thres;
    uci_stats->pucch0_sr_trials++;
    if (xrtmag_dBtimes10 > (10 * max_n0 + 100)) {
      uci_pdu->sr.sr_indication = 1;
      uci_stats->pucch0_positive_SR++;
      LOG_D(PHY, "PUCCH0 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch0_positive_SR);
    } else {
      uci_pdu->sr.sr_indication = 0;
    }
  } else if (pucch_pdu->bit_len_harq == 1) {
    uci_pdu->harq.num_harq = 1;
    uci_pdu->harq.harq_confidence_level = no_conf;
    uci_pdu->harq.harq_list[0].harq_value = !(index & 0x01);
    LOG_D(PHY,
          "[DLSCH/PDSCH/PUCCH] %d.%d HARQ %s with confidence level %s xrt_mag "
          "%d xrt_mag_next %d pucch_power_dBtimes10 %d n0 %d "
          "(%d,%d) pucch0_thres %d, "
          "cqi %d, SNRtimes10 %d, energy %f\n",
          frame,
          slot,
          uci_pdu->harq.harq_list[0].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_confidence_level == 0 ? "good" : "bad",
          xrtmag_dBtimes10,
          xrtmag_next_dBtimes10,
          pucch_power_dBtimes10,
          max_n0,
          pucch0_n00,
          pucch0_n01,
          gNB->pucch0_thres,
          cqi,
          SNRtimes10,
          10 * log10((double)signal_energy_ant0));

    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr.sr_indication = (index > 1);
      uci_pdu->sr.sr_confidence_level = no_conf;
      if (uci_pdu->sr.sr_indication == 1 && uci_pdu->sr.sr_confidence_level == 0) {
        uci_stats->pucch0_positive_SR++;
        LOG_D(PHY, "PUCCH0 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch0_positive_SR);
      }
    }
    uci_stats->pucch01_trials++;
  } else {
    uci_pdu->harq.num_harq = 2;
    uci_pdu->harq.harq_confidence_level = no_conf;

    uci_pdu->harq.harq_list[1].harq_value = !(index & 0x01);
    uci_pdu->harq.harq_list[0].harq_value = !((index >> 1) & 0x01);
    LOG_D(PHY,
          "[DLSCH/PDSCH/PUCCH] %d.%d HARQ values (%s, %s) with confidence level %s, xrt_mag %d xrt_mag_next %d "
          "pucch_power_dBtimes10 %d n0 %d (%d,%d) "
          "pucch0_thres %d, cqi %d, SNRtimes10 %d\n",
          frame,
          slot,
          uci_pdu->harq.harq_list[1].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_list[0].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_confidence_level == 0 ? "good" : "bad",
          xrtmag_dBtimes10,
          xrtmag_next_dBtimes10,
          pucch_power_dBtimes10,
          max_n0,
          pucch0_n00,
          pucch0_n01,
          gNB->pucch0_thres,
          cqi,
          SNRtimes10);
    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr.sr_indication = (index > 3) ? 1 : 0;
      uci_pdu->sr.sr_confidence_level = no_conf;
      if (uci_pdu->sr.sr_indication == 1 && uci_pdu->sr.sr_confidence_level == 0) {
        uci_stats->pucch0_positive_SR++;
        LOG_D(PHY, "PUCCH0 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch0_positive_SR);
      }
    }
  }
}

static void addIt(const c16_t *in1, cd_t *out1, const c16_t *in2, cd_t *out2)
{
  cd_t tmp1 = {0}, tmp2 = {0};
  for (int n = 0; n < NR_NB_SC_PER_RB; n++) {
    csum(tmp1, tmp1, in1[n]);
    csum(tmp2, tmp2, in2[n]);
  }
  out1->r += tmp1.r / NR_NB_SC_PER_RB;
  out1->i += tmp1.i / NR_NB_SC_PER_RB;
  out2->r += tmp2.r / NR_NB_SC_PER_RB;
  out2->i += tmp2.i / NR_NB_SC_PER_RB;
}

//*****************************************************************//
void nr_decode_pucch1(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_pdu,
                      nfapi_nr_pucch_pdu_t *pucch_pdu)
{
#ifdef DEBUG_NR_PUCCH_RX
  printf(
      "\t [nr_decode_pucch1] start function at slot(nr_tti_tx)=%d "
      "payload=%lux m0=%d nrofSymbols=%d startingSymbolIndex=%d "
      "startingPRB=%d startingPRB_intraSlotHopping=%d timeDomainOCC=%d "
      "nr_bit=%d\n",
      nr_tti_tx,
      *payload,
      m0,
      nrofSymbols,
      startingSymbolIndex,
      startingPRB,
      startingPRB_intraSlotHopping,
      timeDomainOCC,
      pucch_pdu->bit_len_harq);
#endif
  /*
   * Implement TS 38.211 Subclause 6.3.2.4.1 Sequence modulation
   *
   */
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const uint n_rx = pucch_pdu->param_v4.numSpatialStreamIndices;
  const int nb_symbols = pucch_pdu->nr_of_symbols;
  const int symb_sz = frame_parms->ofdm_symbol_size;

  const int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * symb_sz;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the
  // slot given by [5, TS 38.213]
  const int lprime = pucch_pdu->start_symbol_index;
  // mcs = 0 except for PUCCH format 0
  const uint mcs = 0;
  // r_u_v_alpha_delta_re and r_u_v_alpha_delta_im tables containing the sequence y(n) for the PUCCH, when they are multiplied by
  // d(0) r_u_v_alpha_delta_dmrs_re and r_u_v_alpha_delta_dmrs_im tables containing the sequence for the DM-RS.
  c16_t r_u_v_alpha_delta[12], r_u_v_alpha_delta_dmrs[12];
  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  /*
   * the complex-valued symbol d_0 shall be multiplied with a sequence r_u_v_alpha_delta(n): y(n) = d_0 * r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u = 0, v = 0; //,delta=0;

  // Intra-slot frequency hopping shall be assumed when the higher-layer parameter intraSlotFrequencyHopping is provided,
  // regardless of whether the frequency-hop distance is zero or not,
  // otherwise no intra-slot frequency hopping shall be assumed
  // uint8_t PUCCH_Frequency_Hopping = 0 ; // from higher layers
  const bool intraSlotFrequencyHopping = pucch_pdu->prb_start != pucch_pdu->second_hop_prb;
  const float inv_sqrt2 = 0.70710678118f; // 1 / sqrt(2)
  int64_t signal_energy = 0, signal_energy_ant0 = 0;
  uint8_t nb_re_pucch = pucch_pdu->prb_size * NR_NB_SC_PER_RB;
  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag << 1);
  const int16_t amp = INT16_MAX;
  int xrtmag_dBtimes10 = 0;

  NR_gNB_UCI_STATS_t stack_uci_stats = {0};
  NR_gNB_UCI_STATS_t *uci_stats = &stack_uci_stats;
  NR_gNB_PHY_STATS_t *phy_stats = get_phy_stats(gNB, pucch_pdu->rnti);
  if (phy_stats != NULL) {
    phy_stats->frame = frame;
    uci_stats = &phy_stats->uci_stats;
  }

#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_decode_pucch1] intraSlotFrequencyHopping = %d \n", intraSlotFrequencyHopping);
  printf("\t [nr_decode_pucch1] soffset= %d\n", soffset);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.4.2 Mapping to physical resources
   */
// This value has to be calculated from mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n
#define MAX_SIZE_Z 168 
  c16_t z_rx[16][MAX_SIZE_Z] = {0};
  c16_t z_dmrs_rx[16][MAX_SIZE_Z] = {0};
  c16_t z[16][12] = {0};
  for (int l = 0; l < nb_symbols; l++) { // extracting data and dmrs from rxdataF
    if (intraSlotFrequencyHopping && (l >= nb_symbols / 2)) { // intra-slot hopping enabled, we need
      // to calculate new offset PRB
      pucch_pdu->prb_start = pucch_pdu->bwp_start + pucch_pdu->second_hop_prb;
    }

    int re_offset = (l + pucch_pdu->start_symbol_index) * symb_sz + NR_NB_SC_PER_RB * pucch_pdu->prb_start;

    for (int n = 0; n < NR_NB_SC_PER_RB; n++) {
      const int current_subcarrier = (l / 2) * NR_NB_SC_PER_RB + n;

      if (l % 2 == 1) // mapping PUCCH or DM-RS according to TS38.211 subclause 6.4.1.3.1
        for (int r = 0; r < n_rx; r++) {
          z_rx[r][current_subcarrier] = rxdataF[r][soffset + re_offset];
          z[r][n] = z_rx[r][current_subcarrier];
        }
      else
        for (int r = 0; r < n_rx; r++) {
          z_dmrs_rx[r][current_subcarrier] = rxdataF[r][soffset + re_offset];
          z[r][n] = z_dmrs_rx[r][current_subcarrier];
        }

#ifdef DEBUG_NR_PUCCH_RX
      printf(
          "\t [nr_decode_pucch1] mapping %s to RE \t amp=%d "
          "\tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d "
          "\tz_pucch[%d]=rxptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
          l % 2 ? "PUCCH" : "DM-RS",
          amp,
          symb_sz,
          frame_parms->N_RB_DL,
          frame_parms->first_carrier_offset,
          current_subcarrier,
          soffset + re_offset,
          l,
          n,
          rxdataF[0][soffset + re_offset].r,
          rxdataF[0][soffset + re_offset].i);
#endif
      re_offset++;
    } // end sc loop

    // compute signal energy

    for (int r = 0; r < n_rx; r++) {
      int energ = signal_energy_nodc(z[r], nb_re_pucch);
      signal_energy += energ;
      if (!r)
        signal_energy_ant0 += energ;
    }

  } // end symbols loop

  signal_energy /= (nb_symbols * n_rx);
  signal_energy_ant0 /= nb_symbols;
  int pucch_power_dBtimes10 = 10 * dB_fixed(signal_energy);
  int max_n0 = max(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->bwp_start + pucch_pdu->prb_start],
                   gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->bwp_start + pucch_pdu->second_hop_prb]);
  const int SNRtimes10 = pucch_power_dBtimes10 - (10 * max_n0);

  LOG_D(PHY,
        "signal_energy %lld signal_energy_ant0 %lld pucch_power_dBtimes10 %d max_n0 %d SNRtimes10 %d\n",
        (long long)signal_energy,
        (long long)signal_energy_ant0,
        pucch_power_dBtimes10,
        max_n0,
        SNRtimes10);
  int cqi;
  if (SNRtimes10 < -640)
    cqi = 0;
  else if (SNRtimes10 > 635)
    cqi = 255;
  else
    cqi = (640 + SNRtimes10) / 5;

  // generating transmitted sequence and dmrs
  for (int l = 0; l < nb_symbols; l++) {
#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_decode_pucch1] for symbol l=%d, lprime=%d\n", l, lprime);
#endif
    // y_n contains the complex value d multiplied by the sequence r_u_v
    // if frequency hopping is disabled, intraSlotFrequencyHopping is not
    // provided
    //              n_hop = 0
    // if frequency hopping is enabled,  intraSlotFrequencyHopping is provided
    //              n_hop = 0 for first hop
    //              n_hop = 1 for second hop
    const int n_hop = intraSlotFrequencyHopping && l >= nb_symbols / 2 ? 1 : 0;

#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_decode_pucch1] entering function nr_group_sequence_hopping with n_hop=%d, nr_tti_tx=%d\n", n_hop, slot);
#endif
    const int symb_idx = nb_symbols - 1;
    nr_group_sequence_hopping(pucch_GroupHopping, pucch_pdu->hopping_id, n_hop, slot, &u, &v); // calculating u and v value
    // Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
    double alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id, pucch_pdu->initial_cyclic_shift, mcs, l, lprime, slot);
    for (int n = 0; n < 12; n++) { // generating low papr sequences
      const c16_t angle = {lround(32767 * cos(alpha * n)), lround(32767 * sin(alpha * n))};
      const c16_t table = {table_5_2_2_2_2_Re[u][n], table_5_2_2_2_2_Im[u][n]};
      if (l % 2 == 1)
        r_u_v_alpha_delta[n] = c16mulShift(angle, table, 15);
      else
        r_u_v_alpha_delta_dmrs[n] = c16mulRealShift(c16mulShift(angle, table, 15), amp, 15);
    }
    /*
     * The block of complex-valued symbols y(n) shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     * The block of complex-valued symbols r_u_v_alpha_dmrs_delta(n) for DM-RS shall be block-wise spread with the orthogonal
     * sequence wi(m) (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     */
    // the orthogonal sequence index for wi(m) defined in TS 38.213 Subclause 9.2.1
    // the index of the orthogonal cover code is from a set determined as described in [4, TS 38.211]
    // and is indicated by higher layer parameter PUCCH-F1-time-domain-OCC
    // In the PUCCH_Config IE, the PUCCH-format1, timeDomainOCC field
    const int w_index = pucch_pdu->time_domain_occ_idx;
    if (intraSlotFrequencyHopping == false) { // intra-slot hopping disabled
#ifdef DEBUG_NR_PUCCH_RX
      printf(
          "\t [nr_decode_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, "
          "intra-slot hopping disabled\n",
          intraSlotFrequencyHopping);
#endif
      // mprime is 0 in this not hopping case
      // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1
      // (depending on number of PUCCH symbols nrofSymbols, mprime and
      // intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains
      // N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH
      // symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
      // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1
      // (depending on number of PUCCH symbols nrofSymbols, mprime=0 and
      // intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains
      // N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH
      // symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
      // mprime is 0 if no intra-slot hopping / mprime is {0,1} if intra-slot
      // hopping

      // only if intra-slot hopping not enabled (PUCCH)
      int N_SF_mprime_PUCCH_1 = table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[symb_idx];
      // only if intra-slot hopping not enabled (DM-RS)
      int N_SF_mprime_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[symb_idx];
      if (l % 2 == 1) {
        for (int m = 0; m < N_SF_mprime_PUCCH_1; m++) {
          c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],
                         table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m]};
          if (l / 2 == m) {
            for (int r = 0; r < n_rx; r++) {
              for (int n = 0; n < 12; n++) {
                c16_t *zPtr = z_rx[r] + m * 12 + n;
                *zPtr = c16MulConjShift(table, *zPtr, 15);
                // multiplying with conjugate of low papr sequence
                *zPtr = c16MulConjShift(r_u_v_alpha_delta[n], *zPtr, 16);
              }
            }
          }
        }
      } else {
        for (int m = 0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
          const c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m],
                               table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m]};
          if (l / 2 == m) {
            for (int r = 0; r < n_rx; r++) {
              for (int n = 0; n < 12; n++) {
                c16_t *zDmrsPtr = z_dmrs_rx[r] + m * 12 + n;
                *zDmrsPtr = c16MulConjShift(table, *zDmrsPtr, 15);
                // finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and
                // z_dmrs_im_rx arrays
                *zDmrsPtr = c16MulConjShift(r_u_v_alpha_delta_dmrs[n], *zDmrsPtr, 16);
              }
            }
          }
        }
      }
    }

    if (intraSlotFrequencyHopping == true) { // intra-slot hopping enabled
#ifdef DEBUG_NR_PUCCH_RX
      printf(
          "\t [nr_decode_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, "
          "intra-slot hopping enabled\n",
          intraSlotFrequencyHopping);
#endif
      // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1
      // (depending on number of PUCCH symbols nrofSymbols, mprime and
      // intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains
      // N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH
      // symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
      // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1
      // (depending on number of PUCCH symbols nrofSymbols, mprime=0 and
      // intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains
      // N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH
      // symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
      // mprime is 0 if no intra-slot hopping / mprime is {0,1} if intra-slot
      // hopping

      // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      int N_SF_mprime_PUCCH_1 = table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[symb_idx];
      // only if intra-slot hopping enabled mprime = 0 (DM-RS)
      int N_SF_mprime_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[symb_idx];
      // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      int N_SF_mprime0_PUCCH_1 = table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[symb_idx];
      // only if intra-slot hopping enabled mprime = 0 (DM-RS)
      int N_SF_mprime0_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[symb_idx];
#ifdef DEBUG_NR_PUCCH_RX
      printf(
          "\t [nr_decode_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, "
          "N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
          w_index,
          N_SF_mprime_PUCCH_1,
          N_SF_mprime_PUCCH_DMRS_1,
          N_SF_mprime0_PUCCH_1,
          N_SF_mprime0_PUCCH_DMRS_1);
#endif

      for (int mprime = 0; mprime < 2; mprime++) { // mprime can get values {0,1}
        if (l % 2 == 1) {
          for (int m = 0; m < N_SF_mprime_PUCCH_1; m++) {
            c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],
                           table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m]};
            if (floor(l / 2) * 12 == (mprime * 12 * N_SF_mprime0_PUCCH_1) + (m * 12)) {
              for (int r = 0; r < n_rx; r++) {
                for (int n = 0; n < 12; n++) {
                  c16_t *zPtr = z_rx[r] + (mprime * 12 * N_SF_mprime0_PUCCH_1) + (m * 12) + n;
                  *zPtr = c16MulConjShift(table, *zPtr, 15);
                  *zPtr = c16MulConjShift(r_u_v_alpha_delta[n], *zPtr, 15);
                }
              }
            }
          }
        }

        else {
          for (int m = 0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
            c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],
                           table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m]};
            if (floor(l / 2) * 12 == (mprime * 12 * N_SF_mprime0_PUCCH_DMRS_1) + (m * 12)) {
              for (int r = 0; r < n_rx; r++) {
                for (int n = 0; n < 12; n++) {
                  c16_t *zDmrsPtr = z_dmrs_rx[r] + (mprime * 12 * N_SF_mprime0_PUCCH_DMRS_1) + (m * 12) + n;
                  *zDmrsPtr = c16MulConjShift(table, *zDmrsPtr, 15);
                  // finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and
                  // z_dmrs_im_rx arrays
                  *zDmrsPtr = c16MulConjShift(r_u_v_alpha_delta_dmrs[n], *zDmrsPtr, 15);
                }
              }
            }
          }
        }

        N_SF_mprime_PUCCH_1 =
            table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m1Hop[symb_idx]; // only if intra-slot hopping enabled mprime = 1 (PUCCH)
        N_SF_mprime_PUCCH_DMRS_1 =
            table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m1Hop[symb_idx]; // only if intra-slot hopping enabled mprime = 1 (DM-RS)
      }
    }
  } // end of symbols loop

  cd_t H[16] = {0}, H1[16] = {0};
  cd_t y[16] = {0}, y1[16] = {0};
  const int half_nb_symbols = nb_symbols / 2;
  for (int r = 0; r < n_rx; r++) {
    for (int l = 0; l <= half_nb_symbols; l++) {
      const c16_t *dmrs = z_dmrs_rx[r] + l * NR_NB_SC_PER_RB;
      const c16_t *z = z_rx[r] + l * NR_NB_SC_PER_RB;
      if (intraSlotFrequencyHopping) { // with Frequency-hopping
        if (l < nb_symbols / 4)
          addIt(dmrs, H + r, z, y + r);
        else
          addIt(dmrs, H1 + r, z, y1 + r);
      } else {
        addIt(dmrs, H + r, z, y + r);
      }
    }
    H[r].r /= half_nb_symbols;
    H[r].i /= half_nb_symbols;
    H1[r].r /= half_nb_symbols;
    H1[r].i /= half_nb_symbols;
    y[r].r /= half_nb_symbols;
    y[r].i /= half_nb_symbols;
    y1[r].r /= half_nb_symbols;
    y1[r].i /= half_nb_symbols;
  }
  // mrc combining to obtain z_re and z_im
  // complex-valued symbol d_re, d_im containing complex-valued symbol d(0):
  if (pucch_pdu->bit_len_harq == 1) { // BPSK
    double dp1mag = {0}, dm1mag = {0};
    for (int r = 0; r < n_rx; r++) {
      double yr = y[r].r, yi = y[r].i;
      cd_t dp1 = (cd_t){H[r].r + inv_sqrt2 * (yr + yi), H[r].i + inv_sqrt2 * (yi - yr)};
      dp1mag += squaredMod(dp1);
      cd_t dm1 = (cd_t){H[r].r + inv_sqrt2 * (-yr - yi), H[r].i + inv_sqrt2 * (yr - yi)};
      dm1mag += squaredMod(dm1);

      if (intraSlotFrequencyHopping) {
        double y1r = y1[r].r, y1i = y1[r].i;
        cd_t dp1 = (cd_t){H1[r].r + inv_sqrt2 * (y1r + y1i), H1[r].i + inv_sqrt2 * (y1i - y1r)};
        dp1mag += squaredMod(dp1);
        cd_t dm1 = (cd_t){H1[r].r + inv_sqrt2 * (-y1r - y1i), H1[r].i + inv_sqrt2 * (y1r - y1i)};
        dm1mag += squaredMod(dm1);
      }
    }
    if (dp1mag > dm1mag) {
      uci_pdu->harq.harq_list[0].harq_value = 1;
      xrtmag_dBtimes10 = 10 * (int)dB_fixed64(dp1mag / (NR_NB_SC_PER_RB * nb_symbols));
    } else {
      //*payload = 1;
      uci_pdu->harq.harq_list[0].harq_value = 0;
      xrtmag_dBtimes10 = 10 * (int)dB_fixed64(dm1mag / (NR_NB_SC_PER_RB * nb_symbols));
    }
  } else if (pucch_pdu->bit_len_harq == 2) { // QPSK
    double d0mag = {0}, d1mag = {0}, d2mag = {0}, d3mag = {0};
    for (int r = 0; r < n_rx; r++) {
      double yr = y[r].r, yi = y[r].i;
      // d0 = H + (1 - j)*y
      cd_t d0 = (cd_t){H[r].r + inv_sqrt2 * (yr + yi), H[r].i + inv_sqrt2 * (yi - yr)};
      d0mag += squaredMod(d0);

      // d1 = H + (-1 - j)*y
      cd_t d1 = (cd_t){H[r].r + inv_sqrt2 * (-yr + yi), H[r].i + inv_sqrt2 * (-yr - yi)};
      d1mag += squaredMod(d1);

      // d2 = H + (1 + j)*y
      cd_t d2 = (cd_t){H[r].r + inv_sqrt2 * (yr - yi), H[r].i + inv_sqrt2 * (yi + yr)};
      d2mag += squaredMod(d2);

      // d3 = H + (-1 + j)*y
      cd_t d3 = (cd_t){H[r].r + inv_sqrt2 * (-yr - yi), H[r].i + inv_sqrt2 * (yr - yi)};
      d3mag += squaredMod(d3);

      // with frequency hopping
      if (intraSlotFrequencyHopping == true) {
        double y1r = y1[r].r, y1i = y1[r].i;
        cd_t d0 = (cd_t){H1[r].r + inv_sqrt2 * (y1r + y1i), H1[r].i + inv_sqrt2 * (y1i - y1r)};
        d0mag += squaredMod(d0);

        cd_t d1 = (cd_t){H1[r].r + inv_sqrt2 * (-y1r + y1i), H1[r].i + inv_sqrt2 * (-y1r - y1i)};
        d1mag += squaredMod(d1);

        cd_t d2 = (cd_t){H1[r].r + inv_sqrt2 * (y1r - y1i), H1[r].i + inv_sqrt2 * (y1i + y1r)};
        d2mag += squaredMod(d2);

        cd_t d3 = (cd_t){H1[r].r + inv_sqrt2 * (-y1r - y1i), H1[r].i + inv_sqrt2 * (y1r - y1i)};
        d3mag += squaredMod(d3);
      }

      if (d0mag >= d1mag && d0mag >= d2mag && d0mag >= d3mag) {
        uci_pdu->harq.harq_list[0].harq_value = 1;
        uci_pdu->harq.harq_list[1].harq_value = 1;
        xrtmag_dBtimes10 = 10 * (int)dB_fixed64(d0mag / (NR_NB_SC_PER_RB * nb_symbols));
      } else if (d1mag >= d0mag && d1mag >= d2mag && d1mag >= d3mag) {
        uci_pdu->harq.harq_list[0].harq_value = 1;
        uci_pdu->harq.harq_list[1].harq_value = 0;
        xrtmag_dBtimes10 = 10 * (int)dB_fixed64(d1mag / (NR_NB_SC_PER_RB * nb_symbols));
      } else if (d2mag >= d0mag && d2mag >= d1mag && d2mag >= d3mag) {
        uci_pdu->harq.harq_list[0].harq_value = 0;
        uci_pdu->harq.harq_list[1].harq_value = 1;
        xrtmag_dBtimes10 = 10 * (int)dB_fixed64(d2mag / (NR_NB_SC_PER_RB * nb_symbols));
      } else {
        uci_pdu->harq.harq_list[0].harq_value = 0;
        uci_pdu->harq.harq_list[1].harq_value = 0;
        xrtmag_dBtimes10 = 10 * (int)dB_fixed64(d3mag / (NR_NB_SC_PER_RB * nb_symbols));
      }
    }
  }

  bool no_conf = false;
  if (pucch_pdu->bit_len_harq > 0 || pucch_pdu->sr_flag > 0) {
    if (/*xrtmag_dBtimes10 < (30+xrtmag_next_dBtimes10) ||*/ SNRtimes10 <= gNB->pucch0_thres) {
      no_conf = true;
      LOG_D(PHY, "%d.%d PUCCH F1 bad confidence: %d threshold, %d,\n", frame, slot, gNB->pucch0_thres, SNRtimes10);
    }
  }
  gNB->bad_pucch += no_conf;
  // first bit of bitmap for sr presence and second bit for acknack presence
  uci_pdu->pduBitmap = pucch_pdu->sr_flag | ((pucch_pdu->bit_len_harq > 0) << 1);
  uci_pdu->pucch_format = 1; // format 1
  uci_pdu->rnti = pucch_pdu->rnti;
  uci_pdu->ul_cqi = cqi;
  uci_pdu->timing_advance = 0xffff; // currently not valid
  // dB_fixed_times10() gives -900 for zero energy and saturates at 900. rssi value is never > 1280
  int16_t rssi_raw = 1280 - (10 * dB_fixed(INT16_MAX * INT16_MAX) - dB_fixed_times10(signal_energy_ant0));
  uci_pdu->rssi = (uint16_t)max(0, rssi_raw);

  if (pucch_pdu->bit_len_harq == 0) {
    uci_pdu->sr.sr_confidence_level = SNRtimes10 < gNB->pucch0_thres;
    uci_stats->pucch1_sr_trials++;
    if (xrtmag_dBtimes10 >= (10 * max_n0 /*+100*/)) {
      uci_pdu->sr.sr_indication = 1;
      uci_stats->pucch1_positive_SR++;
      LOG_D(PHY, "PUCCH1 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch1_positive_SR);
    } else {
      uci_pdu->sr.sr_indication = 0;
    }
  } else if (pucch_pdu->bit_len_harq == 1) {
    uci_pdu->harq.num_harq = 1;
    uci_pdu->harq.harq_confidence_level = no_conf;
    LOG_D(PHY,
          "[PUCCH F1] %d.%d HARQ %s with confidence level %s"
          " xrtmag_dBtimes10 %d pucch_power_dBtimes10 %d n0 %d "
          "pucch0_thres %d, "
          "cqi %d, SNRtimes10 %d, energy %f\n",
          frame,
          slot,
          uci_pdu->harq.harq_list[0].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_confidence_level == 0 ? "good" : "bad",
          xrtmag_dBtimes10,
          pucch_power_dBtimes10,
          max_n0,
          gNB->pucch0_thres, // using same pucch threshold for both pucch 0 and pucch 1.
          cqi,
          SNRtimes10,
          10 * log10((double)signal_energy_ant0));

    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr.sr_indication = 0; // Upper layers determine if SR when the PUCCH resource is an SR resource or not
      uci_pdu->sr.sr_confidence_level = no_conf;
      if (uci_pdu->sr.sr_indication == 1 && uci_pdu->sr.sr_confidence_level == 0) {
        uci_stats->pucch1_positive_SR++;
        LOG_D(PHY, "PUCCH F1 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch1_positive_SR);
      }
    }
    uci_stats->pucch11_trials++;
  } else {
    uci_pdu->harq.num_harq = 2;
    uci_pdu->harq.harq_confidence_level = no_conf;
    LOG_D(PHY,
          "[PUCCH F1] %d.%d HARQ values (%s, %s) with confidence level %s, xrtmag_dBtimes10 %d pucch_power_dBtimes10 %d n0 %d "
          "pucch0_thres %d, cqi %d, SNRtimes10 %d\n",
          frame,
          slot,
          uci_pdu->harq.harq_list[1].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_list[0].harq_value == 0 ? "ACK" : "NACK",
          uci_pdu->harq.harq_confidence_level == 0 ? "good" : "bad",
          xrtmag_dBtimes10,
          pucch_power_dBtimes10,
          max_n0,
          gNB->pucch0_thres, // using same pucch threshold for both pucch 0 and pucch 1.
          cqi,
          SNRtimes10);
    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr.sr_indication = 0; // Upper layers determine if SR when the PUCCH resource is an SR resource or not
      uci_pdu->sr.sr_confidence_level = no_conf;
      if (uci_pdu->sr.sr_indication == 1 && uci_pdu->sr.sr_confidence_level == 0) {
        uci_stats->pucch1_positive_SR++;
        LOG_D(PHY, "PUCCH F1 got positive SR. Cumulative number of positive SR %d\n", uci_stats->pucch1_positive_SR);
      }
    }
  }
}

typedef struct {
  c16_t cw[16];
} cw_t;
static cw_t pucch2_3bit[8] __attribute__((aligned(32)));
static cw_t pucch2_4bit[16] __attribute__((aligned(32)));
static cw_t pucch2_5bit[32] __attribute__((aligned(32)));
static cw_t pucch2_6bit[64] __attribute__((aligned(32)));
static cw_t pucch2_7bit[128] __attribute__((aligned(32)));
static cw_t pucch2_8bit[256] __attribute__((aligned(32)));
static cw_t pucch2_9bit[512] __attribute__((aligned(32)));
static cw_t pucch2_10bit[1024] __attribute__((aligned(32)));
static cw_t pucch2_11bit[2048] __attribute__((aligned(32)));

static cw_t *pucch2_lut[9] =
    {pucch2_3bit, pucch2_4bit, pucch2_5bit, pucch2_6bit, pucch2_7bit, pucch2_8bit, pucch2_9bit, pucch2_10bit, pucch2_11bit};

typedef struct {
  int16_t cw[4];
} cw4bit_t;

static simde__m128i pucch2_polar_llr_num_lut[256];
static simde__m128i pucch2_polar_factor_lut[256] __attribute__((aligned(16)));

void init_pucch2_luts()
{
  for (int b = 3; b < 12; b++) {
    for (int cw = 0; cw < (1 << b); cw++) {
      uint32_t out = encodeSmallBlock(cw, b, 0); // Qm = 0 because it's unused for len >= 3
      uint16_t *tmp = (uint16_t *)pucch2_lut[b - 3][cw].cw;
      for (int j = 0; j < 32; j++)
        *tmp++ = (out & (1U << j)) > 0 ? -1 : 1;
    }
  }
  cw4bit_t pucch2_polar_4bit[16] __attribute__((aligned(32)));
  for (int i = 0; i < 16; i++) {
    int16_t *lut_i = pucch2_polar_4bit[i].cw;
    *lut_i++ = (i & 0x1) <= 0;
    *lut_i++ = (i & 0x2) <= 0;
    *lut_i++ = (i & 0x4) <= 0;
    *lut_i++ = (i & 0x8) <= 0;
  }
  for (int cw = 0; cw < 256; cw++) {
    simde__m128i part1 = simde_mm_set_epi64x(0ULL, *(int64_t *)&pucch2_polar_4bit[cw & 15].cw);
    simde__m128i part2 = simde_mm_set_epi64x(0ULL, *(int64_t *)&pucch2_polar_4bit[cw >> 4].cw);
    pucch2_polar_factor_lut[cw] = simde_mm_unpacklo_epi16(part1, part2);
  }
  for (int cw = 0; cw < 256; cw++) {
    int16_t *lut_num_i = (int16_t *)&pucch2_polar_llr_num_lut[cw];
    *lut_num_i++ = (cw & 0x1) <= 0;
    *lut_num_i++ = (cw & 0x10) <= 0;
    *lut_num_i++ = (cw & 0x2) <= 0;
    *lut_num_i++ = (cw & 0x20) <= 0;
    *lut_num_i++ = (cw & 0x4) <= 0;
    *lut_num_i++ = (cw & 0x40) <= 0;
    *lut_num_i++ = (cw & 0x8) <= 0;
    *lut_num_i++ = (cw & 0x80) <= 0;
#ifdef DEBUG_NR_PUCCH_RX
    log_dump(PHY, pucch2_polar_llr_num_lut, 8, LOG_DUMP_C16, "lut_num %d:", i);
#endif
  }
}

void nr_decode_pucch2(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_2_3_4_t *uci_pdu,
                      const nfapi_nr_pucch_pdu_t *pucch_pdu)
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  // pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);
  const int nb_symbols = pucch_pdu->nr_of_symbols;
  const int symb_sz = frame_parms->ofdm_symbol_size;

  AssertFatal(nb_symbols == 1 || nb_symbols == 2, "Illegal number of symbols  for PUCCH 2 %d\n", nb_symbols);

  AssertFatal((pucch_pdu->prb_start - ((pucch_pdu->prb_start >> 2) << 2)) == 0,
              "Current pucch2 receiver implementation requires a PRB offset multiple of 4. The one selected is %d",
              pucch_pdu->prb_start);

  // extract pucch and dmrs first

  int l2 = pucch_pdu->start_symbol_index;
  int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * symb_sz;
  uint16_t starting_prb = pucch_pdu->prb_start + pucch_pdu->bwp_start;
  int re_offset[nb_symbols];
  re_offset[0] = NR_NB_SC_PER_RB * starting_prb;
  if (nb_symbols == 2) {
    if (pucch_pdu->freq_hop_flag)
      re_offset[1] = NR_NB_SC_PER_RB * (pucch_pdu->second_hop_prb + pucch_pdu->bwp_start);
    else
      re_offset[1] = re_offset[0];
  }
  AssertFatal(pucch_pdu->prb_size * nb_symbols > 1, "number of PRB*SYMB (%d,%d)< 2", pucch_pdu->prb_size, nb_symbols);

  int Prx = pucch_pdu->param_v4.numSpatialStreamIndices;
  //  AssertFatal((pucch_pdu->prb_size&1) == 0,"prb_size %d is not a multiple of2\n",pucch_pdu->prb_size);
  // use 2 for Nb antennas in case of single antenna to allow the following allocations
  const int nb_re_pucch = NR_NB_SC_PER_RB * pucch_pdu->prb_size;
  c16_t rp[Prx][nb_symbols][nb_re_pucch];
  memset(rp, 0, sizeof(rp));

  int64_t pucch2_lev = 0;
  for (int aa = 0; aa < Prx; aa++) {
    for (int symb = 0; symb < nb_symbols; symb++) {
      c16_t *tmp_rp = &rxdataF[aa][soffset + (l2 + symb) * symb_sz];
      memcpy(rp[aa][symb], &tmp_rp[re_offset[symb]], nb_re_pucch * sizeof(c16_t));
      pucch2_lev += signal_energy_nodc(rp[aa][symb], nb_re_pucch);
    }
  }

  pucch2_lev /= Prx * nb_symbols;
  int pucch2_levdB = dB_fixed(pucch2_lev);
  int scaling = max((log2_approx64(pucch2_lev) >> 1) - 8, 0);
  LOG_D(NR_PHY,
        "%d.%d Decoding pucch2 for %d symbols, %d PRB, nb_harq %d, nb_sr %d, nb_csi %d/%d, pucch2_lev %d dB (scaling %d)\n",
        frame,
        slot,
        nb_symbols,
        pucch_pdu->prb_size,
        pucch_pdu->bit_len_harq,
        pucch_pdu->sr_flag,
        pucch_pdu->bit_len_csi_part1,
        pucch_pdu->bit_len_csi_part2,
        pucch2_levdB,
        scaling);

  int prb_size_ext = pucch_pdu->prb_size + (pucch_pdu->prb_size & 1);
  int nc_group_size = 1; // 2 PRB
  int ngroup = prb_size_ext / nc_group_size / 2;
  c32_t corr32[nb_symbols][ngroup][Prx];
  memset(corr32, 0, sizeof(corr32));
  const int nb_re_data = 8 * prb_size_ext;
  const int nb_re_dmrs = 4 * prb_size_ext;
  c16_t r_ext[Prx][nb_symbols][nb_re_data] __attribute__((aligned(32)));
  c16_t r_ext2[Prx][nb_symbols][nb_re_data] __attribute__((aligned(32)));
  const simde__m256i swap = simde_mm256_set_epi8(29,
                                                 28,
                                                 31,
                                                 30,
                                                 25,
                                                 24,
                                                 27,
                                                 26,
                                                 21,
                                                 20,
                                                 23,
                                                 22,
                                                 17,
                                                 16,
                                                 19,
                                                 18,
                                                 13,
                                                 12,
                                                 15,
                                                 14,
                                                 9,
                                                 8,
                                                 11,
                                                 10,
                                                 5,
                                                 4,
                                                 7,
                                                 6,
                                                 1,
                                                 0,
                                                 3,
                                                 2);
  // prepare scrambling sequence for data
  uint32_t x2 = ((pucch_pdu->rnti) << 15) + pucch_pdu->data_scrambling_id;
#ifdef DEBUG_NR_PUCCH_RX
  printf("x2 %x\n", x2);
#endif
  c16_t scramb_data[nb_re_data] __attribute__((aligned(32)));

  uint32_t *sGold = gold_cache(x2, nb_symbols * nb_re_data / 2);
  uint8_t *sGold8 = (uint8_t *)sGold;
  for (int i = 0; i < nb_re_data; i += 4)
    *(simde__m128i *)(scramb_data + i) = byte2m128i[*sGold8++];

  for (int symb = 0; symb < nb_symbols; symb++) {
    c16_t rdmrs_ext[Prx][nb_re_dmrs] __attribute__((aligned(32)));

    // extract DMRS
    for (int aa = 0; aa < Prx; aa++) {
      c16_t *rdmrs_ext_p = rdmrs_ext[aa];
      c16_t *rp_base = rp[aa][symb];
      for (int prb = 0; prb < pucch_pdu->prb_size; prb++) {
        for (int idx = 0; idx < 4; idx++) {
          rp_base++;
          *rdmrs_ext_p++ = *rp_base++;
          rp_base++;
        }
      }
      if (pucch_pdu->prb_size != prb_size_ext)
        // if the number of PRBs is odd
        // we fill the unsed part of the arrays
        memset(rdmrs_ext[aa] + pucch_pdu->prb_size * 4, 0, 4 * sizeof(c16_t));
    }

#ifdef DEBUG_NR_PUCCH_RX
    for (int aa = 0; aa < Prx; aa++)
      log_dump(PHY, rdmrs_ext[aa], nb_re_dmrs, LOG_DUMP_C16, "Ant %d dmrs:\n", aa);
#endif

    // first compute DMRS component
    const int scramble = pucch_pdu->dmrs_scrambling_id * 2;
    uint32_t x2 =
        ((1ULL << 17) * ((frame_parms->symbols_per_slot * slot + pucch_pdu->start_symbol_index + symb + 1) * (scramble + 1))
         + scramble)
        % (1U << 31); // c_init calculation according to TS38.211 subclause
#ifdef DEBUG_NR_PUCCH_RX
    printf("slot %d, start_symbol_index %d, symbol %d, dmrs_scrambling_id %d\n",
           slot,
           pucch_pdu->start_symbol_index,
           symb,
           pucch_pdu->dmrs_scrambling_id);
#endif
    uint32_t *sGold = gold_cache(x2, starting_prb / 4 + (ngroup + 1) / 2);
    // Compute pilot conjugate
    c16_t pil_dmrs[nb_re_dmrs] __attribute__((aligned(32)));
    uint8_t *sGold8 = (uint8_t *)(sGold + starting_prb / 4);
    for (int group = 0; group < nb_re_dmrs; group += 4)
      *(simde__m128i *)(pil_dmrs + group) = oai_mm_conj(byte2m128i[*sGold8++]);

    // Compute delay
    c16_t ch_ls[128] __attribute__((aligned(32))) = {0};
    {
      c16_t rdmrs_gold[nb_re_dmrs] __attribute__((aligned(32)));
      for (int aa = 0; aa < Prx; aa++) {
        mult_complex_vectors(rdmrs_ext[aa], pil_dmrs, rdmrs_gold, nb_re_dmrs, 0);
        c16_t *ch_ls_ptr = ch_ls;
        c16_t *end = ch_ls_ptr + 128;
        for (int i = 0; i < nb_re_dmrs; i++)
          for (int k = 0; k < 3 && ch_ls_ptr < end; k++)
            *ch_ls_ptr++ = rdmrs_gold[i];
      }
    }
    c16_t ch_temp[128] __attribute__((aligned(32)));
    delay_t delay = {0};
    nr_est_delay(128, ch_ls, ch_temp, &delay);

    // Apply delay compensation on the input
    if (delay.est_delay != 0) {
      int delay_idx = get_delay_idx(delay.est_delay, MAX_DELAY_COMP);
      c16_t *delay_table = frame_parms->delay_table128[delay_idx];
      for (int aa = 0; aa < Prx; aa++)
        mult_complex_vectors(rp[aa][symb], delay_table, rp[aa][symb], nb_re_pucch, 8);
    }
    
    // extract again DMRS, and signal, after delay compensation
    for (int aa = 0; aa < Prx; aa++) {
      c16_t *r_ext_p = r_ext[aa][symb];
      c16_t *rdmrs_ext_p = rdmrs_ext[aa];
      c16_t *rp_base = rp[aa][symb];
      for (int prb = 0; prb < pucch_pdu->prb_size; prb++) {
        for (int idx = 0; idx < 4; idx++) {
          *r_ext_p++ = *rp_base++;
          *rdmrs_ext_p++ = *rp_base++;
          *r_ext_p++ = *rp_base++;
        }
      }
      if (pucch_pdu->prb_size != prb_size_ext) {
        // if the number of PRBs is odd
        // we fill the unsed part of the arrays
        memset(rdmrs_ext[aa] + pucch_pdu->prb_size * 4, 0, 4 * sizeof(c16_t));
        memset(r_ext[aa][symb] + pucch_pdu->prb_size * 8, 0, 8 * sizeof(c16_t));
      }
    }
#ifdef DEBUG_NR_PUCCH_RX
    for (int aa = 0; aa < Prx; aa++) {
      log_dump(PHY, rdmrs_ext[aa], nb_re_dmrs, LOG_DUMP_C16, "after delay compensation ant %d dmrs:\n", aa);
      log_dump(PHY, r_ext[aa], nb_re_data, LOG_DUMP_C16, "after delay compensation ant %d data:\n", aa);
    }
#endif
    c16_t rdmrs_gold[Prx][nb_re_dmrs] __attribute__((aligned(32)));
    for (int aa = 0; aa < Prx; aa++)
      mult_complex_vectors(rdmrs_ext[aa], pil_dmrs, rdmrs_gold[aa], nb_re_dmrs, 0);
    for (int aa = 0; aa < Prx; aa++) {
      c16_t *pil_ptr = pil_dmrs;
      for (int group = 0; group < ngroup; group++) {
        // each group has 8*nc_group_size elements, compute 1 complex correlation with DMRS per group
        // non-coherent combining across groups
        c16_t *rdmrs_p = &rdmrs_ext[aa][8 * group];
        for (int z = 0; z < 8; z++) {
          c16_t tmp = c16mulShift(*rdmrs_p++, *pil_ptr++, scaling);
          corr32[symb][group][aa].r += tmp.r;
          corr32[symb][group][aa].i += tmp.i;
        }
      }
    }
#ifdef DEBUG_NR_PUCCH_RX
    log_dump(PHY, corr32[symb][0], 8, LOG_DUMP_C32, "corr32:");
#endif

    // apply gold sequence on data symbols
    for (int aa = 0; aa < Prx; aa++) {
      simde__m256i *pil_ptr = (simde__m256i *)scramb_data;
      simde__m256i *end = (simde__m256i *)(scramb_data + nb_re_data);
      for (simde__m256i *ptr = (simde__m256i *)r_ext[aa][symb], *ptr2 = (simde__m256i *)r_ext2[aa][symb]; pil_ptr < end;
           ptr++, pil_ptr++, ptr2++) {
        simde__m256i tmp = simde_mm256_srai_epi16(*ptr, scaling);
        *ptr2 = oai_mm256_conj(simde_mm256_sign_epi16(simde_mm256_shuffle_epi8(tmp, swap), *pil_ptr));
        *ptr = simde_mm256_sign_epi16(tmp, *pil_ptr);
      }
    }
  }

  int nb_bit = pucch_pdu->bit_len_harq + pucch_pdu->sr_flag + pucch_pdu->bit_len_csi_part1 + pucch_pdu->bit_len_csi_part2;
  AssertFatal(nb_bit > 2 && nb_bit < 65,
              "illegal length (%d : %d,%d,%d,%d)\n",
              nb_bit,
              pucch_pdu->bit_len_harq,
              pucch_pdu->sr_flag,
              pucch_pdu->bit_len_csi_part1,
              pucch_pdu->bit_len_csi_part2);

  uint64_t decodedPayload[nb_symbols];
  memset(decodedPayload, 0, sizeof(decodedPayload));
  uint8_t corr_dB;
  int decoderState = 2;
  int max_n0 = gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->bwp_start + pucch_pdu->prb_start];
  for (int p = 1; p < pucch_pdu->prb_size; p++)
    max_n0 = max(max_n0, gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->bwp_start + pucch_pdu->prb_start + p]);
  if (pucch2_levdB < max_n0 + (gNB->pucch0_thres / 10))
    decoderState = 1; // assuming missed detection, only attempt to decode for polar case (with CRC)
  LOG_D(NR_PHY,
        "n0+thres %d decoderState %d\n",
        gNB->measurements.n0_subband_power_avg_dB + (gNB->pucch0_thres / 10),
        decoderState);

  if (nb_bit < 12 && decoderState == 2) { // short blocklength case
    uint64_t corr = 0;
    int cw_ML = 0;
    // Bit 0 of the message toggles nrSmallBlockBasis[0] (the all-ones basis vector), which negates
    // every output bit of the codeword. In the correlation domain this means the data-correlation term
    // for codeword (cw|1) is the exact negation of that for codeword (cw&~1); only the cw-independent
    // DMRS/pilot reference term (corr32) stays the same. So we only run the expensive correlation once
    // per even/odd pair and derive both metrics from it -- halving the number of madd/hadd evaluations.
    for (int cw = 0; cw < 1 << nb_bit; cw += 2) {
#if defined(__AVX2__)
      const simde__m256i *coeff = (simde__m256i *)&pucch2_lut[nb_bit - 3][cw].cw;
#else
      // No native 256-bit ISA (e.g. aarch64): use 128-bit NEON directly rather than
      // SIMDe's 256-bit-on-NEON emulation, which is inefficient for the cross-lane
      // hadd/permute ops this loop relies on.
      const simde__m128i *coeff = (simde__m128i *)&pucch2_lut[nb_bit - 3][cw].cw;
#endif
      uint64_t corr_tmp_even = 0;
      uint64_t corr_tmp_odd = 0;
      for (int symb = 0; symb < nb_symbols; symb++) {
        for (int group = 0; group < ngroup; group++) {
          // do complex correlation
          for (int aa = 0; aa < Prx; aa++) {
            c64_t d;
#if defined(__AVX2__)
            const simde__m256i *rext = (simde__m256i *)r_ext[aa][symb];
            const simde__m256i *rext2 = (simde__m256i *)r_ext2[aa][symb];
            simde__m256i re = simde_mm256_madd_epi16(coeff[0], rext[group]);
            simde__m256i im = simde_mm256_madd_epi16(coeff[0], rext2[group]);
            simde__m256i re2 = simde_mm256_madd_epi16(coeff[1], rext[group + 1]);
            simde__m256i im2 = simde_mm256_madd_epi16(coeff[1], rext2[group + 1]);
            re = simde_mm256_add_epi32(re, re2);
            im = simde_mm256_add_epi32(im, im2);
            simde__m256i ri = simde_mm256_hadd_epi32(re, im);
            ri = simde_mm256_hadd_epi32(ri, ri);
            int32_t *v = (int32_t *)&ri;
            d = (c64_t){v[0] + v[4], v[1] + v[5]};
#else
            // Each AVX2 256-bit "group"/"group+1" chunk is, byte-for-byte, two
            // consecutive 128-bit chunks here: coeff[0]/coeff[1] correspond to
            // the low/high halves of AVX2's coeff[0], coeff[2]/coeff[3] to
            // AVX2's coeff[1]. Same mapping for rext/rext2. All reductions
            // below stay within a single 128-bit register -- no cross-lane
            // combine except a final plain scalar add.
            const simde__m128i *rext = (simde__m128i *)r_ext[aa][symb];
            const simde__m128i *rext2 = (simde__m128i *)r_ext2[aa][symb];
            int g0 = group * 2, g1 = group * 2 + 1;
            int g2 = (group + 1) * 2, g3 = (group + 1) * 2 + 1;
            simde__m128i re_lo = simde_mm_madd_epi16(coeff[0], rext[g0]);
            simde__m128i im_lo = simde_mm_madd_epi16(coeff[0], rext2[g0]);
            simde__m128i re_hi = simde_mm_madd_epi16(coeff[1], rext[g1]);
            simde__m128i im_hi = simde_mm_madd_epi16(coeff[1], rext2[g1]);
            simde__m128i re2_lo = simde_mm_madd_epi16(coeff[2], rext[g2]);
            simde__m128i im2_lo = simde_mm_madd_epi16(coeff[2], rext2[g2]);
            simde__m128i re2_hi = simde_mm_madd_epi16(coeff[3], rext[g3]);
            simde__m128i im2_hi = simde_mm_madd_epi16(coeff[3], rext2[g3]);
            re_lo = simde_mm_add_epi32(re_lo, re2_lo);
            im_lo = simde_mm_add_epi32(im_lo, im2_lo);
            re_hi = simde_mm_add_epi32(re_hi, re2_hi);
            im_hi = simde_mm_add_epi32(im_hi, im2_hi);
            simde__m128i lo_ri = simde_mm_hadd_epi32(re_lo, im_lo);
            lo_ri = simde_mm_hadd_epi32(lo_ri, lo_ri);
            simde__m128i hi_ri = simde_mm_hadd_epi32(re_hi, im_hi);
            hi_ri = simde_mm_hadd_epi32(hi_ri, hi_ri);
            int32_t *vlo = (int32_t *)&lo_ri;
            int32_t *vhi = (int32_t *)&hi_ri;
            d = (c64_t){vlo[0] + vhi[0], vlo[1] + vhi[1]};
#endif
            c32_t c = corr32[symb][group][aa];
            c64_t prod_even = {d.r + c.r, d.i + c.i};
            c64_t prod_odd  = {-d.r + c.r, -d.i + c.i};
            corr_tmp_even += squaredMod(prod_even);
            corr_tmp_odd  += squaredMod(prod_odd);
#ifdef DEBUG_NR_PUCCH_RX
            printf("pucch2 cw %d/%d group %d aa %d: c=(%d,%d) d=(%ld,%ld) even=(%ld,%ld) odd=(%ld,%ld)\n",
                   cw,
                   cw+1,
                   group,
                   aa,
                   c.r,
                   c.i,
                   d.r,
                   d.i,
                   prod_even.r,
                   prod_even.i,
                   prod_odd.r,
                   prod_odd.i);
#endif
          }
        } // group loop
      } // symb loop
      if (corr_tmp_even > corr) {
        corr = corr_tmp_even;
        cw_ML = cw;
#ifdef DEBUG_NR_PUCCH_RX
        printf("slot %d PUCCH2 cw_ML %d, corr %lu\n", slot, cw_ML, corr);
#endif
      }
      if (corr_tmp_odd > corr) {
        corr = corr_tmp_odd;
        cw_ML = cw + 1;
#ifdef DEBUG_NR_PUCCH_RX
        printf("slot %d PUCCH2 cw_ML %d, corr %lu\n", slot, cw_ML, corr);
#endif
      }
    } // cw loop
    corr_dB = dB_fixed64(corr);
#ifdef DEBUG_NR_PUCCH_RX
    printf("slot %d PUCCH2 cw_ML %d, metric %d \n", slot, cw_ML, corr_dB);
#endif
    decodedPayload[0] = (uint64_t)cw_ML;
  } else if (nb_bit >= 12) { // polar coded case
    simde__m128i llrs[pucch_pdu->prb_size * 2 * nb_symbols];
    // non-coherent LLR computation on groups of 4 REs (half-PRBs)
    uint64_t corr = 0;
    const simde__m128i ones = simde_mm_set1_epi16(1);
    for (int symb = 0; symb < nb_symbols; symb++) {
      for (int half_prb = 0; half_prb < (2 * pucch_pdu->prb_size); half_prb++) {
        simde__m128i llr_num = simde_mm_set1_epi16(0);
        simde__m128i llr_den = simde_mm_set1_epi16(0);
        for (int cw = 0; cw < 256; cw++) {
          int32_t corr_tmp = 0;
          simde__m128i factor = pucch2_polar_factor_lut[cw];
          for (int aa = 0; aa < Prx; aa++) {
            simde__m128i re = *(simde__m128i *)&r_ext[aa][symb][half_prb * 4];
            simde__m128i im = *(simde__m128i *)&r_ext2[aa][symb][half_prb * 4];
            simde__m128i prod_re = simde_mm_madd_epi16(re, factor);
            simde__m128i prod_im = simde_mm_madd_epi16(im, factor);
            // combine re/im into one register so both reductions share the same hadd chain
            simde__m128i ri = simde_mm_hadd_epi32(prod_re, prod_im);
            ri = simde_mm_hadd_epi32(ri, ri);
            simde__m128i prod = simde_mm_srai_epi32(ri, 5);
            c64_t corr64 = (c64_t){corr32[symb][half_prb >> 2][aa].r / (2 * nc_group_size * 4 / 2),
                                   corr32[symb][half_prb >> 2][aa].i / (2 * nc_group_size * 4 / 2)};
            //  _mm_srai_epi64 is missing in SIMDE package, we need to update it
            c64_t prod2 = {simde_mm_extract_epi32(prod, 0), simde_mm_extract_epi32(prod, 1)};
            csum(prod2, prod2, corr64);
            corr_tmp += squaredMod(prod2) >> (Prx / 2);
            // this is for UL CQI measurement
            if (cw == 0)
              corr += squaredMod(corr32[symb][half_prb >> 2][aa]);
          }
          simde__m128i corr16 = simde_mm_set1_epi16((int16_t)(corr_tmp >> 8));
          simde__m128i den = simde_mm_xor_si128(pucch2_polar_llr_num_lut[cw], ones);
          llr_num = simde_mm_max_epi16(simde_mm_mullo_epi16(corr16, pucch2_polar_llr_num_lut[cw]), llr_num);
          llr_den = simde_mm_max_epi16(simde_mm_mullo_epi16(corr16, den), llr_den);
        }
        // compute llrs
        llrs[half_prb + symb * 2 * pucch_pdu->prb_size] = simde_mm_subs_epi16(llr_num, llr_den);
        LOG_DDUMP(PHY, llrs + half_prb + symb * 2 * pucch_pdu->prb_size, 8, LOG_DUMP_I16, "llrs:");
      } // half_prb
    } // symb

    // run polar decoder on llrs
    decoderState =
        polar_decoder_int16((int16_t *)llrs, decodedPayload, 0, NR_POLAR_UCI_PUCCH_MESSAGE_TYPE, nb_bit, pucch_pdu->prb_size);

    // Decoder reversal
    decodedPayload[0] = reverse_bits(decodedPayload[0], nb_bit);

    if (decoderState > 0)
      decoderState = 1;
    corr_dB = dB_fixed64(corr);
    LOG_D(PHY, "metric %d dB\n", corr_dB);
  } else
    LOG_D(PHY, "PUCCH not processed: nb_bit %d decoderState %d\n", nb_bit, decoderState);

  LOG_D(PHY, "UCI decoderState %d, payload[0] %llu\n", decoderState, (unsigned long long)decodedPayload[0]);

  int SNRtimes10 = dB_fixed_times10(pucch2_lev) - (10 * max_n0);
  int cqi;
  if (SNRtimes10 < -640)
    cqi = 0;
  else if (SNRtimes10 > 635)
    cqi = 255;
  else
    cqi = (640 + SNRtimes10) / 5;

  uci_pdu->harq.harq_bit_len = pucch_pdu->bit_len_harq;
  uci_pdu->pduBitmap = 0;
  uci_pdu->rnti = pucch_pdu->rnti;
  uci_pdu->handle = pucch_pdu->handle;
  uci_pdu->pucch_format = 0;
  uci_pdu->ul_cqi = cqi;
  uci_pdu->timing_advance = 0xffff; // currently not valid
  uint e = signal_energy_nodc(rp[0][0], nb_re_pucch);
  // dB_fixed_times10() gives -900 for zero energy and saturates at 900. rssi value is never > 1280
  int16_t rssi_raw = 1280 - (10 * dB_fixed(INT16_MAX * INT16_MAX) - dB_fixed_times10(e));
  uci_pdu->rssi = (uint16_t)max(0, rssi_raw);
  if (pucch_pdu->bit_len_harq > 0) {
    uci_pdu->pduBitmap |= 2;
    uci_pdu->harq.harq_crc = decoderState;
    int harq_bytes = (pucch_pdu->bit_len_harq + 7) / 8;
    uci_pdu->harq.harq_payload = calloc_or_fail(harq_bytes, sizeof(uint8_t));
    LOG_D(PHY, "[DLSCH/PDSCH/PUCCH2] %d.%d HARQ bytes (%d) Decoder state %d\n", frame, slot, harq_bytes, decoderState);
    set_uci_payload(&decodedPayload[0], pucch_pdu->bit_len_harq, harq_bytes, uci_pdu->harq.harq_payload, frame, slot);
  }

  if (pucch_pdu->sr_flag == 1) {
    uci_pdu->pduBitmap |= 1;
    uci_pdu->sr.sr_bit_len = 1;
    uci_pdu->sr.sr_payload = malloc(1);
    uci_pdu->sr.sr_payload[0] = decodedPayload[0] & 1;
    decodedPayload[0] = decodedPayload[0] >> 1;
  }
  // csi
  if (pucch_pdu->bit_len_csi_part1 > 0) {
    uci_pdu->pduBitmap |= 4;
    uci_pdu->csi_part1.csi_part1_bit_len = pucch_pdu->bit_len_csi_part1;
    uci_pdu->csi_part1.csi_part1_crc = decoderState;
    int csi_bytes = (pucch_pdu->bit_len_csi_part1 + 7) / 8;
    uci_pdu->csi_part1.csi_part1_payload = calloc_or_fail(csi_bytes, sizeof(uint8_t));
    set_uci_payload(&decodedPayload[0], pucch_pdu->bit_len_csi_part1, csi_bytes, uci_pdu->csi_part1.csi_part1_payload, frame, slot);
  }

  if (pucch_pdu->bit_len_csi_part2 > 0) {
    uci_pdu->pduBitmap |= 8;
  }
}
