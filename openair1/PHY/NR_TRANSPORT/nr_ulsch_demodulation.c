/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/defs_gNB.h"
#include "PHY/phy_extern.h"
#include "nfapi_nr_interface_scf.h"
#include "nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/defs_nr_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "nr_channel_compensation.h"
#include "nr_compute_llr.h"
#include "nr_layer_demapping.h"
#include "common/utils/nr/nr_common.h"
#include "platform_types.h"
#include "utils.h"
#include <openair1/PHY/TOOLS/phy_scope_interface.h>
#include "PHY/sse_intrin.h"
#include "T.h"
#include "T_messages_creator.h"
#include <sys/time.h>
#include "openair1/SCHED_NR/sched_nr.h"

#define NR_MAX_PUSCH_SCRAMBLING_STACK_BYTES (2 * 1024 * 1024) // 2MB

#if T_TRACER
static void copy_c16_data_to_slot_memory(c16_t *src, c16_t *dst_slot, int nb_re_pusch, int symbol)
{
  memcpy(&dst_slot[nb_re_pusch * symbol], src, nb_re_pusch * sizeof(c16_t));
}
#endif

void nr_idft(int32_t *z, uint32_t Msc_PUSCH)
{
  const dft_size_idx_t dftsize = get_dft(Msc_PUSCH);

  c16_t idft_input[Msc_PUSCH] __attribute__((aligned(64)));

  c16_t idft_output[Msc_PUSCH] __attribute__((aligned(64)));

  const size_t bytes = (size_t)Msc_PUSCH * sizeof(c16_t);

  memcpy(idft_input, z, bytes);

  idft(dftsize, (int16_t *)idft_input, (int16_t *)idft_output, 1);

  memcpy(z, idft_output, bytes);
}

static void nr_ulsch_extract_rbs(c16_t *const rxF,
                                 c16_t *const chF,
                                 c16_t *rxFext,
                                 c16_t *chFext,
                                 int choffset,
                                 int is_dmrs_symbol,
                                 const nfapi_nr_pusch_pdu_t *pusch_pdu,
                                 NR_DL_FRAME_PARMS *frame_parms,
                                 uint16_t rnti,
                                 bool is_ptrs)
{
  uint8_t delta = 0;
  if (is_dmrs_symbol) {
    uint8_t max_cdm = (pusch_pdu->dmrs_config_type == pusch_dmrs_type1 ? 2 : 3);
    AssertFatal(pusch_pdu->num_dmrs_cdm_grps_no_data <= max_cdm,
                "cdm group no data %d cannot be greater than %d\n",
                pusch_pdu->num_dmrs_cdm_grps_no_data,
                max_cdm);
    int first_port = get_dmrs_port(0, pusch_pdu->dmrs_ports);
    delta = get_delta(first_port, pusch_pdu->dmrs_config_type);
  }
  int start_re = (pusch_pdu->rb_start + pusch_pdu->bwp_start) * NR_NB_SC_PER_RB;
  int nb_re_pusch = NR_NB_SC_PER_RB * pusch_pdu->rb_size;
  c16_t *rxF_ext = &rxFext[0];
  c16_t *ul_ch0 = &chF[choffset];
  c16_t *ul_ch0_ext = &chFext[0];

  if (is_ptrs) {
    const uint k_ptrs = pusch_pdu->pusch_ptrs.ptrs_freq_density;
    const uint k_rb_ref = get_ptrs_k_RB(pusch_pdu->rb_size, k_ptrs, rnti);
    const uint k_re_ref = pusch_pdu->pusch_ptrs.ptrs_ports_list[0].ptrs_re_offset;
    uint k = start_re;
    uint ch_idx = 0;
    for (uint rb = 0; rb < pusch_pdu->rb_size; rb++) {
      // RB doesn't have PTRS.
      if ((rb - k_rb_ref) % k_ptrs) {
        memcpy(rxF_ext, rxF + k, sizeof(c16_t) * NR_NB_SC_PER_RB);
        rxF_ext += NR_NB_SC_PER_RB;
        memcpy(ul_ch0_ext, ul_ch0 + ch_idx, sizeof(c16_t) * NR_NB_SC_PER_RB);
        ul_ch0_ext += NR_NB_SC_PER_RB;
        // RB has PTRS.
      } else {
        // before PTRS RE
        const uint num_pre_ptrs = k_re_ref;
        const size_t pre_sz = sizeof(c16_t) * num_pre_ptrs;
        memcpy(rxF_ext, rxF + k, pre_sz);
        memcpy(ul_ch0_ext, ul_ch0 + ch_idx, pre_sz);
        // after PTRS RE
        const uint num_post_ptrs = NR_NB_SC_PER_RB - k_re_ref - 1;
        const size_t post_sz = sizeof(c16_t) * num_post_ptrs;
        memcpy(rxF_ext + num_pre_ptrs, rxF + k + k_re_ref + 1, post_sz);
        memcpy(ul_ch0_ext + num_pre_ptrs, ul_ch0 + ch_idx + k_re_ref + 1, post_sz);
        rxF_ext += NR_NB_SC_PER_RB - 1;
        ul_ch0_ext += NR_NB_SC_PER_RB - 1;
      }
      ch_idx += NR_NB_SC_PER_RB;
      k += NR_NB_SC_PER_RB;
    }
  } else if (is_dmrs_symbol == 0) {
    memcpy(rxF_ext, &rxF[start_re], nb_re_pusch * sizeof(c16_t));
    memcpy(ul_ch0_ext, ul_ch0, nb_re_pusch * sizeof(c16_t));
  } else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type1) { // 6 REs / PRB
    AssertFatal(delta == 0 || delta == 1, "Illegal delta %d\n",delta);
    c16_t *rxF32 = &rxF[start_re];
    for (int idx = 1 - delta; idx < nb_re_pusch; idx += 2) {
      *rxF_ext++ = rxF32[idx];
      *ul_ch0_ext++ = ul_ch0[idx];
    }
  } else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type2) { // 8 REs / PRB
    AssertFatal(delta==0||delta==2||delta==4,"Illegal delta %d\n",delta);
    c16_t *rxF32 = &rxF[start_re];
    for (int idx = 0; idx < nb_re_pusch; idx++) {
      if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
        continue;
      *rxF_ext++ = rxF32[idx];
      *ul_ch0_ext++ = ul_ch0[idx];
    }
  }
}

static int get_nb_re_pusch(NR_DL_FRAME_PARMS *frame_parms,
                           const nfapi_nr_pusch_pdu_t *rel15_ul,
                           int symbol,
                           const nr_ptrs_info_t *ptrs_info)
{
  int re_pusch = rel15_ul->rb_size * NR_NB_SC_PER_RB;
  if ((rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01) {
    if (rel15_ul->dmrs_config_type == 0) {
      // if no data in dmrs cdm group is 1 only even REs have no data
      // if no data in dmrs cdm group is 2 both odd and even REs have no data
      re_pusch -= rel15_ul->rb_size * rel15_ul->num_dmrs_cdm_grps_no_data * 6;
    } else
      re_pusch -= rel15_ul->rb_size * rel15_ul->num_dmrs_cdm_grps_no_data * 4;
  }
  if (IS_BIT_SET(ptrs_info->ptrs_symbols, symbol))
    re_pusch -= ptrs_info->n_ptrs;
  return re_pusch;
}

static void inner_rx(PHY_VARS_gNB *gNB,
                     int slot,
                     NR_DL_FRAME_PARMS *frame_parms,
                     NR_gNB_PUSCH *pusch_vars,
                     const nfapi_nr_pusch_pdu_t *rel15_ul,
                     c16_t **rxF,
                     int16_t **llr,
                     int soffset,
                     int symbol,
                     int output_shift,
                     uint32_t nvar,
                     uint16_t ptrs_symb_pos,
                     c16_t cpe,
                     c16_t *rxFext_slot,
                     c16_t *chFext_slot)
{
  int nb_layer = rel15_ul->nrOfLayers;
  int nb_rx_ant = rel15_ul->param_v4.numSpatialStreamIndices;
  int dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  int buffer_length = ceil_mod(rel15_ul->rb_size * NR_NB_SC_PER_RB, 16);
  c16_t rxFext[nb_rx_ant][buffer_length] __attribute__((aligned(64)));
  c16_t chFext[nb_layer][nb_rx_ant][buffer_length] __attribute__((aligned(64)));

  memset(rxFext, 0, sizeof(rxFext));
  memset(chFext, 0, sizeof(chFext));
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = dmrs_symbol_flag ? symbol : get_valid_dmrs_idx_for_channel_est(rel15_ul->ul_dmrs_symb_pos, symbol);
  else { // average of channel estimates stored in first symbol
    int end_symbol = rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols;
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, end_symbol);
  }

  for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
    for (int aatx = 0; aatx < nb_layer; aatx++) {
      nr_ulsch_extract_rbs(rxF[aarx] + soffset + symbol * frame_parms->ofdm_symbol_size,
                           (c16_t *)pusch_vars->ul_ch_estimates[aatx * nb_rx_ant + aarx],
                           rxFext[aarx],
                           chFext[aatx][aarx],
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           dmrs_symbol_flag,
                           rel15_ul,
                           frame_parms,
                           rel15_ul->rnti,
                           IS_BIT_SET(ptrs_symb_pos, symbol));
#if T_TRACER
      // Data Recording application supports only 1 layer and 1 Tx antenna, so only record the first layer and first Tx antenna
      if (aatx == 0 && aarx == 0) {
        int nb_re_pusch = NR_NB_SC_PER_RB * rel15_ul->rb_size;
        // Assume assume Tx and Rx = 1
        if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ)) {
          copy_c16_data_to_slot_memory(rxFext[aarx], rxFext_slot, nb_re_pusch, symbol);
        }
        if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL)) {
          copy_c16_data_to_slot_memory(chFext[aatx][aarx], chFext_slot, nb_re_pusch, symbol);
        }
      }
#endif
    }
  }
  c16_t rho[nb_layer][nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_maga[nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_magb[nb_layer][buffer_length] __attribute__((aligned(64)));
  c16_t rxF_ch_magc[nb_layer][buffer_length] __attribute__((aligned(64)));

  memset(rho, 0, sizeof(rho));
  for (int i = 0; i < nb_layer; i++)
    memset(&pusch_vars->rxdataF_comp[i][symbol * buffer_length], 0, sizeof(int32_t) * buffer_length);

  nr_channel_compensation(buffer_length,
                          buffer_length,
                          nb_rx_ant,
                          nb_layer,
                          rxFext,
                          chFext,
                          rxF_ch_maga,
                          rxF_ch_magb,
                          rxF_ch_magc,
                          pusch_vars->rxdataF_comp,
                          (nb_layer > 1) ? rho : NULL,
                          cpe,
                          rel15_ul->qam_mod_order,
                          symbol,
                          output_shift);

  if (nb_layer == 1 && rel15_ul->transform_precoding == transformPrecoder_enabled && rel15_ul->qam_mod_order <= 6) {
    if (rel15_ul->qam_mod_order > 2)
      nr_freq_equalization(frame_parms,
                           &pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                           rxF_ch_maga[0],
                           rxF_ch_magb[0],
                           symbol,
                           pusch_vars->ul_valid_re_per_slot[symbol],
                           rel15_ul->qam_mod_order);
    nr_idft((int32_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length], pusch_vars->ul_valid_re_per_slot[symbol]);
  }
  if (nb_layer == 2) {
    if (rel15_ul->qam_mod_order <= 6) {
      nr_compute_ML_llr((c16_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                        (c16_t *)&pusch_vars->rxdataF_comp[1][symbol * buffer_length],
                        rxF_ch_maga[0],
                        rxF_ch_maga[1],
                        llr[0],
                        llr[1],
                        rho[0][1],
                        rho[1][0],
                        pusch_vars->ul_valid_re_per_slot[symbol],
                        rel15_ul->qam_mod_order);
    }
    else {
      nr_mmse_2layers(pusch_vars->rxdataF_comp,
                      buffer_length,
                      buffer_length,
                      nb_rx_ant,
                      nb_layer,
                      rxF_ch_maga,
                      rxF_ch_magb,
                      rxF_ch_magc,
                      chFext,
                      rel15_ul->rb_size,
                      rel15_ul->qam_mod_order,
                      pusch_vars->log2_maxh,
                      symbol,
                      pusch_vars->ul_valid_re_per_slot[symbol],
                      nvar);
    }
  }
  if (nb_layer != 2 || rel15_ul->qam_mod_order > 6)
    for (int aatx = 0; aatx < nb_layer; aatx++)
           nr_compute_llr(&pusch_vars->rxdataF_comp[aatx][symbol * buffer_length],
                     rxF_ch_maga[aatx],
                     rxF_ch_magb[aatx],
                     rxF_ch_magc[aatx],
                     llr[aatx],
                     pusch_vars->ul_valid_re_per_slot[symbol],
                     symbol,
                     rel15_ul->qam_mod_order);
}

typedef struct {
  // The "Density" (used to find UCI REs within a symbol)
  int d_ack[NR_SYMBOLS_PER_SLOT];
  int d_ack_rvd[NR_SYMBOLS_PER_SLOT];
  int d_csi1[NR_SYMBOLS_PER_SLOT];
  int d_csi2[NR_SYMBOLS_PER_SLOT];
  // The "Starting Gates" (used for thread-safe parallel writes)
  int ack_offset[NR_SYMBOLS_PER_SLOT];
  int csi1_offset[NR_SYMBOLS_PER_SLOT];
  int csi2_offset[NR_SYMBOLS_PER_SLOT];
  int ulsch_offset[NR_SYMBOLS_PER_SLOT];
  // Number of resources per symbol
  uint32_t q_ack[NR_SYMBOLS_PER_SLOT];
  uint32_t q_ack_rvd[NR_SYMBOLS_PER_SLOT];
  uint32_t q_csi1[NR_SYMBOLS_PER_SLOT];
  uint32_t q_csi2[NR_SYMBOLS_PER_SLOT];
} nr_uci_mapping_t;

/*
 * Builds, per OFDM symbol, the mapping information the gNB needs to demultiplex
 * UCI (HARQ-ACK, CSI part 1, CSI part 2) from UL-SCH data on PUSCH.
 * It mirrors the UE-side multiplexing procedure of TS 38.212 Section 6.2.7.
 * Mapping rules implemented:
 *  - ACK: starts at the first non-DMRS symbol after the first DMRS symbol(s).
 *         If O_ack <= 2, REs are only *reserved* (they stay counted in the data REs, since
 *         ULSCH/CSI2 can be later punctured by the ACK).
 *  - CSI1: starts at the first non-DMRS symbol of the PUSCH. It cannot use reserved ACK REs.
 *  - CSI2: mapped after CSI1 in the REs left over. It can overlap reserved ACK REs (puncturing).
 *  - Within a symbol, if the remaining REs to place are fewer than the available ones, they
 *    are spread with spacing d = floor(available / remaining); otherwise all available REs
 *    are used (d = 1).
 */
nr_uci_mapping_t init_nr_uci_pusch_demux(const nfapi_nr_pusch_pdu_t *pusch_pdu,
                                         rate_match_info_uci_t *uci_info,
                                         NR_DL_FRAME_PARMS *frame_parms,
                                         NR_gNB_PUSCH *pusch_vars)
{
  nr_uci_mapping_t map = {0};
  int first_non_dmrs_sym = 0;
  int after_dmrs_symb = 0;
  uint32_t bits_per_re = pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order;
  get_dmrs_uci_symbol_info(pusch_pdu->start_symbol_index,
                           pusch_pdu->nr_of_symbols,
                           pusch_pdu->ul_dmrs_symb_pos,
                           &first_non_dmrs_sym,
                           &after_dmrs_symb);
  // get_dmrs_uci_symbol_info computes values relative to start
  first_non_dmrs_sym += pusch_pdu->start_symbol_index;
  after_dmrs_symb += pusch_pdu->start_symbol_index;
  // Track how many REs we have successfully "assigned" across symbols
  uint32_t re_assigned_ack = 0;
  uint32_t re_actual_ack = 0;
  uint32_t M_ul[14] = {0};
  uint32_t curr_ack_offset = 0;
  map.ulsch_offset[0] = 0;
  if (!(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI)) {
    for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
      if (s < frame_parms->symbols_per_slot - 1)
        map.ulsch_offset[s + 1] = map.ulsch_offset[s] + (pusch_vars->ul_valid_re_per_slot[s] * bits_per_re);
    }
    return map;
  }

  int Q_ack = uci_info->E_uci_ACK / bits_per_re; // includes reserved resources if O_ack <= 2
  for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
    // resources available for transmission of data in PUSCH symbol
    M_ul[s] = pusch_vars->ul_valid_re_per_slot[s];
    map.d_ack[s] = 0;
    map.d_ack_rvd[s] = 0;
    map.ack_offset[s] = curr_ack_offset;
    // symbols after the first set of consecutive symbol(s) carrying DMRS
    bool is_ack_sym = (s >= after_dmrs_symb) && !IS_BIT_SET(pusch_pdu->ul_dmrs_symb_pos, s);
    // if the symbol is valid for ACK and there are still ACK resources to be placed
    if (is_ack_sym && re_assigned_ack < Q_ack) {
      uint32_t re_remaining = Q_ack - re_assigned_ack; // includes reserved resources if O_ack <= 2
      uint32_t re_remaining_actual = uci_info->Q_dash_ACK - re_actual_ack;
      // if the remaining ACK resources to be placed are less than the ones available in the symbol
      if (re_remaining < pusch_vars->ul_valid_re_per_slot[s]) {
        if (uci_info->O_ack <= 2)
          map.q_ack_rvd[s] += re_remaining;
        // For O_ack <= 2 the actual ACK REs are a subset of the reserved ones: if fewer actual REs than
        // reserved REs remain in this symbol, the actual ACK is spread further over the reserved grid.
        // d_scaling = spacing between actual ACK REs, expressed in reserved-RE units
        // (0 means there is no actual ACK left to place in this symbol)
        uint32_t d_scaling = 1;
        if (uci_info->O_ack <= 2 && re_remaining_actual < map.q_ack_rvd[s])
          d_scaling = re_remaining_actual ? map.q_ack_rvd[s] / re_remaining_actual : 0;
        else
          re_remaining_actual = map.q_ack_rvd[s];
        map.d_ack[s] = M_ul[s] / re_remaining * d_scaling;
        if (uci_info->O_ack <= 2)
          map.d_ack_rvd[s] = M_ul[s] / re_remaining;
        else
          M_ul[s] -= re_remaining; // O_ack > 2: ACK REs are removed from available resources
        // actual ACK REs transmitted on this symbol (reserved REs are only a superset when O_ack <= 2)
        uint32_t n_actual = (uci_info->O_ack <= 2) ? re_remaining_actual : re_remaining;
        map.q_ack[s] = n_actual;
        // advance the ACK bit offset by the number of ACK REs that are actually transmitted in this symbol
        curr_ack_offset += bits_per_re * n_actual;
        re_assigned_ack += re_remaining;
        re_actual_ack += n_actual;
      } else { // if the remaining ACK resources to be placed are equal or more than the ones available in the symbol
        // ACK uses every valid RE of the symbol, d = 1, and continues in the next symbol
        if (uci_info->O_ack > 2)
          M_ul[s] = 0;
        else
          map.q_ack_rvd[s] += pusch_vars->ul_valid_re_per_slot[s];
        uint32_t d_scaling = 1;
        if (uci_info->O_ack <= 2 && re_remaining_actual < map.q_ack_rvd[s])
          d_scaling = re_remaining_actual ? map.q_ack_rvd[s] / re_remaining_actual : 0;
        else
          re_remaining_actual = map.q_ack_rvd[s];
        map.d_ack[s] = d_scaling;
        if (uci_info->O_ack <= 2)
          map.d_ack_rvd[s] = 1;
        uint32_t n_actual = (uci_info->O_ack <= 2) ? re_remaining_actual : pusch_vars->ul_valid_re_per_slot[s];
        map.q_ack[s] = n_actual;
        curr_ack_offset += bits_per_re * n_actual;
        re_assigned_ack += pusch_vars->ul_valid_re_per_slot[s];
        re_actual_ack += n_actual;
      }
    } else
      map.d_ack[s] = 0;
  }
  // if there are no CSI resources to be assigned we conclude the procedure
  if (uci_info->Q_dash_CSI1 == 0) {
    // UL-SCH offsets: cumulative data bits per symbol, given the REs left after ACK
    for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
      if (s < frame_parms->symbols_per_slot - 1)
        map.ulsch_offset[s + 1] = map.ulsch_offset[s] + (M_ul[s] * bits_per_re);
    }
    return map;
  }

  uint32_t re_assigned_csi1 = 0;
  uint32_t re_assigned_csi2 = 0;
  for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
    // CSI starts from the first non-DMRS symbol
    bool is_csi_sym = (s >= first_non_dmrs_sym) && !IS_BIT_SET(pusch_pdu->ul_dmrs_symb_pos, s);
    // CSI1 cannot be mapped on reserved ACK REs
    uint32_t re_avail_for_csi1 = M_ul[s] - map.q_ack_rvd[s];
    map.csi1_offset[s] = re_assigned_csi1 * bits_per_re;
    map.csi2_offset[s] = re_assigned_csi2 * bits_per_re;
    if (is_csi_sym && re_assigned_csi1 < uci_info->Q_dash_CSI1 && re_avail_for_csi1 > 0) {
      uint32_t re_rem_csi1 = uci_info->Q_dash_CSI1 - re_assigned_csi1;
      // if the remaining CSI1 resources to be placed are less than the ones available in the symbol
      if (re_rem_csi1 < re_avail_for_csi1) {
        map.d_csi1[s] = re_avail_for_csi1 / re_rem_csi1;
        map.q_csi1[s] = re_rem_csi1;
        M_ul[s] -= re_rem_csi1;
        re_assigned_csi1 += re_rem_csi1;
      } else { // if the remaining CSI1 resources to be placed are equal or more than the ones available in the symbol
        map.d_csi1[s] = 1;
        map.q_csi1[s] = re_avail_for_csi1;
        M_ul[s] -= re_avail_for_csi1;
        re_assigned_csi1 += re_avail_for_csi1;
      }
    } else {
      map.d_csi1[s] = 0;
      map.q_csi1[s] = 0;
    }
    // CSI2 uses whatever is left in the symbol after ACK (O_ack > 2) and CSI1. For O_ack <= 2 this
    // still includes the reserved ACK REs, which CSI2 may overlap (and which ACK later punctures).
    uint32_t re_avail_for_csi2 = M_ul[s];
    if (is_csi_sym && re_assigned_csi2 < uci_info->Q_dash_CSI2 && re_avail_for_csi2 > 0) {
      uint32_t re_rem_csi2 = uci_info->Q_dash_CSI2 - re_assigned_csi2;
      // if the remaining CSI2 resources to be placed are less than the ones available in the symbol
      if (re_rem_csi2 < re_avail_for_csi2) {
        map.d_csi2[s] = re_avail_for_csi2 / re_rem_csi2;
        map.q_csi2[s] = re_rem_csi2;
        M_ul[s] -= re_rem_csi2;
        re_assigned_csi2 += re_rem_csi2;
      } else { // if the remaining CSI2 resources to be placed are equal or more than the ones available in the symbol
        map.d_csi2[s] = 1;
        map.q_csi2[s] = re_avail_for_csi2;
        M_ul[s] = 0;
        re_assigned_csi2 += re_avail_for_csi2;
      }
    } else {
      map.d_csi2[s] = 0;
      map.q_csi2[s] = 0;
    }
    // UL-SCH gets the REs left in M_ul[s]; accumulate the offset for the next symbol
    if (s < frame_parms->symbols_per_slot - 1)
      map.ulsch_offset[s + 1] = map.ulsch_offset[s] + (M_ul[s] * bits_per_re);
  }
  return map;
}

typedef struct puschSymbolProc_s {
  PHY_VARS_gNB *gNB;
  NR_DL_FRAME_PARMS *frame_parms;
  const nfapi_nr_pusch_pdu_t *rel15_ul;
  NR_gNB_PUSCH *pusch_vars;
  nr_uci_mapping_t *map_uci;
  int slot;
  int startSymbol;
  int numSymbols;
  uint32_t nvar;
  uint16_t ptrs_symb_pos;
  c16_t *ptrs_cpe;
  int beam_nb;
  // TODO: Remove assumption of contiguous ports after DAS is properly handled in beamforming
  uint16_t ant_port_start;
  task_ans_t *ans;
  c16_t *pusch_ch_est_dmrs_interpl_slot_mem;
  c16_t *rxFext_slot_mem;
  uint8_t group_size;
  const nfapi_nr_pusch_pdu_t **rel15_ul_group;
  NR_gNB_PUSCH **pusch_vars_group;
  int16_t **scrambling_sequences;
  int *layer_offsets;
  int layers_attenuation;
} puschSymbolProc_t;

static inline void unscramble_helper(const int16_t *llr_in, const int16_t *seq, int16_t *llr_out, int num_llr)
{
  int i = 0;
  for (; (i + 16) <= num_llr; i += 16) {
    simde__m256i v_llr = simde_mm256_loadu_si256((const simde__m256i *)&llr_in[i]);
    simde__m256i v_s = simde_mm256_loadu_si256((const simde__m256i *)&seq[i]);
    simde_mm256_storeu_si256((simde__m256i *)&llr_out[i], simde_mm256_mullo_epi16(v_llr, v_s));
  }
  // scalar tail (fewer than 16 elements left)
  for (; i < num_llr; i++)
    llr_out[i] = llr_in[i] * seq[i];
}

static void symbol_unscrambling_demux(puschSymbolProc_t *rdata, int ue_idx, int s, int size, int16_t llr_in[size])
{
  const nfapi_nr_pusch_pdu_t *rel15_ul = rdata->rel15_ul_group[ue_idx];
  const nr_uci_mapping_t *map_uci = &rdata->map_uci[ue_idx];
  NR_gNB_PUSCH *joint_pusch_vars = rdata->pusch_vars;
  NR_gNB_PUSCH *ue_pusch_vars = rdata->pusch_vars_group[ue_idx];
  const rate_match_info_uci_t *uci_info = &ue_pusch_vars->uci_info;
  int16_t *s_seq = rdata->scrambling_sequences[ue_idx] + (joint_pusch_vars->llr_offset[s] * rel15_ul->nrOfLayers);
  uint32_t bits_per_re = rel15_ul->nrOfLayers * rel15_ul->qam_mod_order;
  uint32_t a_idx = map_uci->ack_offset[s];
  uint32_t c1_idx = map_uci->csi1_offset[s];
  uint32_t c2_idx = map_uci->csi2_offset[s];
  uint32_t u_idx = map_uci->ulsch_offset[s];

  // Fast path: uncrambling only no UCI multiplexed on this symbol
  bool no_uci = (map_uci->d_ack[s] == 0 && map_uci->d_csi1[s] == 0 && map_uci->d_csi2[s] == 0);
  if (no_uci) {
    const int end = joint_pusch_vars->ul_valid_re_per_slot[s] * bits_per_re;
    int16_t *llr = &ue_pusch_vars->ulsch_llrs[u_idx];
    unscramble_helper(llr_in, s_seq, llr, end);
    return;
  }

  // Per-symbol remaining counts: private to this task
  uint32_t rem_ack = map_uci->q_ack[s];
  uint32_t rem_rvd = map_uci->q_ack_rvd[s];
  uint32_t rem_csi1 = map_uci->q_csi1[s];
  uint32_t rem_csi2 = map_uci->q_csi2[s];
  int num_rvd_ack = 0;
  int num_ack = 0;
  int num_csi = 0;
  // unscrambling and UCI demultiplexing
  for (int re = 0; re < joint_pusch_vars->ul_valid_re_per_slot[s]; re++) {
    bool is_ack = rem_ack > 0 && map_uci->d_ack[s] > 0 && (re % map_uci->d_ack[s] == 0);
    bool is_cnt_ack = uci_info->O_ack <= 2
                      && rem_rvd > 0
                      && map_uci->d_ack_rvd[s] > 0
                      && rem_csi1 > 0
                      && (re % map_uci->d_ack_rvd[s] == 0);
    bool is_csi1 = rem_csi1 > 0 && map_uci->d_csi1[s] > 0 && ((re - num_ack - num_rvd_ack) % map_uci->d_csi1[s] == 0);
    bool is_csi2 = rem_csi2 > 0 && map_uci->d_csi2[s] > 0 && ((re - num_ack - num_csi) % map_uci->d_csi2[s] == 0);
    int16_t *curr_re_llr = &llr_in[re * bits_per_re];
    int16_t *curr_re_s = &s_seq[re * bits_per_re];

    if (is_cnt_ack) {
      num_rvd_ack++;
      rem_rvd--;
    }
    if (is_ack) {
      rem_ack--;
      if (uci_info->O_ack <= 2) {
        for (int b = 0; b < bits_per_re; b++) {
          int bit_in_mod_symbol = b % rel15_ul->qam_mod_order;
          if (uci_info->O_ack == 1) {
            // Table 5.3.3.1-1 of 38.212: Only the first bit (d0) is c0
            // Subsequent bits d1...dN-1 are placeholders (y, x).
            if (bit_in_mod_symbol == 0)
              ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b] * curr_re_s[b]; // unscrambling for info bits
            else if (bit_in_mod_symbol == 1)
              ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b] * curr_re_s[b - 1]; // y repeats the scrambled c0 (38.211 6.3.1.1)
            else
              ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b]; // not unscrambling for placeholder x
          } else {
            // Table 5.3.3.1-2 of 38.212
            // Subsequent bits d1...dN-1 are placeholders (y, x).
            if (bit_in_mod_symbol == 0 || bit_in_mod_symbol == 1)
              ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b] * curr_re_s[b]; // unscrambling for info bits
            else
              ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b]; // not unscrambling for placeholders x
          }
        }
      } else {
        // Large ACK (>2 bits): Standard extraction and unscrambling
        for (int b = 0; b < bits_per_re; b++)
          ue_pusch_vars->ack_llrs[a_idx++] = curr_re_llr[b] * curr_re_s[b];
        num_ack++;
        continue;
      }
    }
    if (is_csi1 && !is_cnt_ack) {
      for (int b = 0; b < bits_per_re; b++)
        ue_pusch_vars->csi1_llrs[c1_idx++] = curr_re_llr[b] * curr_re_s[b];
      rem_csi1--;
      num_csi++;
      continue;
    }
    if (is_csi2) {
      for (int b = 0; b < bits_per_re; b++)
        ue_pusch_vars->csi2_llrs[c2_idx++] = curr_re_llr[b] * curr_re_s[b];
      rem_csi2--;
      continue;
    }
    unscramble_helper(curr_re_llr, curr_re_s, &ue_pusch_vars->ulsch_llrs[u_idx], bits_per_re);
    u_idx += bits_per_re;
  }
}

static void nr_pusch_symbol_processing(void *arg)
{
  puschSymbolProc_t *rdata=(puschSymbolProc_t*)arg;
  PHY_VARS_gNB *gNB = rdata->gNB;
  NR_DL_FRAME_PARMS *frame_parms = rdata->frame_parms;
  const nfapi_nr_pusch_pdu_t *rel15_ul = rdata->rel15_ul;
  int slot = rdata->slot;
  NR_gNB_PUSCH *pusch_vars = rdata->pusch_vars;
  for (int symbol = rdata->startSymbol; symbol < rdata->startSymbol + rdata->numSymbols; symbol++) {
    if (pusch_vars->ul_valid_re_per_slot[symbol] == 0)
      continue;
    int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;
    int buffer_length = ceil_mod(pusch_vars->ul_valid_re_per_slot[symbol] * NR_NB_SC_PER_RB, 16);
    int16_t llrs[rel15_ul->nrOfLayers][ceil_mod(buffer_length * rel15_ul->qam_mod_order, 64)] __attribute__((aligned(32)));
    int16_t *llrss[rel15_ul->nrOfLayers];
    for (int l = 0; l < rel15_ul->nrOfLayers; l++)
      llrss[l] = llrs[l];

    inner_rx(gNB,
             slot,
             frame_parms,
             pusch_vars,
             rel15_ul,
             gNB->common_vars.rxdataF + rdata->ant_port_start,
             llrss,
             soffset,
             symbol,
             pusch_vars->log2_maxh + rdata->layers_attenuation,
             rdata->nvar,
             rdata->ptrs_symb_pos,
             rdata->ptrs_cpe[symbol],
             rdata->rxFext_slot_mem,
             rdata->pusch_ch_est_dmrs_interpl_slot_mem);

    int nb_re_pusch = pusch_vars->ul_valid_re_per_slot[symbol];
    for (int u = 0; u < rdata->group_size; u++) {
      // layer de-mapping
      const int ue_layers = rdata->rel15_ul_group[u]->nrOfLayers;
      int size = ue_layers * buffer_length;
      int16_t *llr_ptr;
      int16_t llr_buf[size];  // only needed for multi-layer
      const int layer_off = rdata->layer_offsets[u];
      if (ue_layers == 1) {
        llr_ptr = llrss[layer_off];  // zero-copy
      } else {
        llr_ptr = llr_buf;
        const int qam = rdata->rel15_ul_group[u]->qam_mod_order;
        nr_layer_demapping(ue_layers, qam, nb_re_pusch, &llrss[layer_off], llr_ptr);
      }
      symbol_unscrambling_demux(rdata, u, symbol, size, llr_ptr);
    }
  }
  // Task running in // completed
  completed_task_ans(rdata->ans);
}

static uint32_t average_u32(const uint32_t *x, uint16_t size)
{
  AssertFatal(size > 0 && x != NULL, "x is NULL or size is 0\n");

  uint64_t sum_x = 0;
  simde__m256i vec_sum = simde_mm256_setzero_si256();

  int i = 0;
  for (; i + 8 <= size; i += 8) {
    simde__m256i vec_data = simde_mm256_loadu_si256((simde__m256i *)&x[i]);
    vec_sum = simde_mm256_add_epi32(vec_sum, vec_data);
  }
  uint32_t *vec_sum32 = (uint32_t *)&vec_sum;
  for (int k = 0; k < 8; k++) {
    sum_x += vec_sum32[k];
  }
  for (; i < size; i++) {
    sum_x += x[i];
  }

  return (uint32_t)(sum_x / size);
}

static rate_match_info_uci_t get_uci_on_pusch_info(const nfapi_nr_pusch_pdu_t *pusch_pdu, const nr_ptrs_info_t *ptrs_info, int G)
{
  rate_match_info_uci_t uci_info = {0};
  if (!(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI)) {
    uci_info.G_ulsch = G;
    return uci_info;
  }

  AssertFatal(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA, "Scenario with no ULSCH data not supported yet\n");

  const nfapi_nr_pusch_uci_t *pusch_uci = &pusch_pdu->pusch_uci;
  int s1 = 0;
  int s2 = 0;
  get_s1_s2(&s1,
            &s2,
            pusch_pdu->rb_size,
            pusch_pdu->nr_of_symbols,
            pusch_pdu->start_symbol_index,
            pusch_pdu->ul_dmrs_symb_pos,
            ptrs_info->ptrs_symbols,
            ptrs_info->n_ptrs);

  // if the number of HARQ-ACK information bits to be transmitted on PUSCH is 0, 1 or 2 bits
  // the number of reserved resource elements for potential HARQ-ACK transmission is calculated using oack = 2
  // according to TS 38.212 section 6.2.7, step 1
  int rev_ack = (pusch_uci->harq_ack_bit_length <= 2) ? 2 : pusch_uci->harq_ack_bit_length;
  // As per 6.3.2.1.1 of 38.212
  // If UCI is transmitted on PUSCH without UL-SCH and the UCI includes CSI part 1 without CSI part 2
  // We need to generate a sequence of bits with A = 2 even if number of HARQ bits is < 2
  uci_info.O_ack = pusch_uci->harq_ack_bit_length;
  if (!(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA)
      && pusch_uci->harq_ack_bit_length < 2
      && pusch_uci->csi_part1_bit_length > 0
      && pusch_uci->csi_part2_bit_length == 0)
    uci_info.O_ack = 2;
  double alpha = get_alpha_scaling_value(pusch_uci->alpha_scaling);
  // Calculate sumKr (total bits in all code blocks)
  int kcb = pusch_pdu->maintenance_parms_v3.ldpcBaseGraph == 1 ? 8448 : 3840;
  int B = lenWithCrc(1, pusch_pdu->pusch_data.tb_size << 3);
  int C = get_C(B, kcb);
  int Bprime = B <= kcb ? B : B + (C * 24);
  int Kprime = Bprime / C;
  int Zout = get_Zout(get_Kb(pusch_pdu->maintenance_parms_v3.ldpcBaseGraph, B), Kprime);
  uint32_t sumKr = get_K(Zout, pusch_pdu->maintenance_parms_v3.ldpcBaseGraph) * C;

  // get the number of coded HARQ-ACK symbols and bits, TS 38.212 section 6.3.2.4.1.1
  double beta = get_beta_offset_harq_ack(pusch_uci->beta_offset_harq_ack);
  uci_info.Q_dash_ACK = get_Qd(uci_info.O_ack, beta, alpha, sumKr, s1, s2, 0);
  int Q_ack_rev = get_Qd(rev_ack, beta, alpha, sumKr, s1, s2, 0);
  uci_info.E_uci_ACK_actual = uci_info.Q_dash_ACK * pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order;
  uci_info.E_uci_ACK = Q_ack_rev * pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order;

  // get the number of coded CSI part 1 symbols and bits, TS 38.212 section 6.3.2.4.1.2
  const double beta_csi1 = get_beta_offset_csi(pusch_uci->beta_offset_csi1);
  int sub = uci_info.O_ack > 2 ? uci_info.Q_dash_ACK : Q_ack_rev;
  uci_info.Q_dash_CSI1 = get_Qd(pusch_uci->csi_part1_bit_length, beta_csi1, alpha, sumKr, s1, s1, sub);
  uci_info.E_uci_CSI1 = uci_info.Q_dash_CSI1 * pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order;

  // get the number of coded CSI part 2 symbols and bits, TS 38.212 section 6.3.2.4.1.3
  const double beta_csi2 = get_beta_offset_csi(pusch_uci->beta_offset_csi2);
  sub = uci_info.Q_dash_CSI1 + (uci_info.O_ack > 2 ? uci_info.Q_dash_ACK : 0);
  uci_info.Q_dash_CSI2 = get_Qd(pusch_uci->csi_part2_bit_length, beta_csi2, alpha, sumKr, s1, s1, sub);
  uci_info.E_uci_CSI2 = uci_info.Q_dash_CSI2 * pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order;

  uci_info.G_ulsch = G - uci_info.E_uci_CSI1 - uci_info.E_uci_CSI2 - (uci_info.O_ack > 2 ? uci_info.E_uci_ACK : 0);
  return uci_info;
}

int nr_rx_pusch_group_tp(PHY_VARS_gNB *gNB,
                         NR_gNB_PUSCH **pusch_vars_group,
                         const nfapi_nr_pusch_pdu_t **rel15_ul_group,
                         uint32_t **ret_unav_res_group,
                         uint8_t group_size,
                         uint32_t frame,
                         uint8_t slot)
{
  // This is a reference pdu since all the UEs in the group have same resource related parameters.
  const nfapi_nr_pusch_pdu_t *rel15_ul_ref = rel15_ul_group[0];
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const nfapi_nr_spatial_stream_index_t *p = &rel15_ul_ref->param_v4;
  uint16_t ant_port_start = get_first_ant_idx(gNB->enable_analog_das,
                                              frame_parms->nb_antennas_tx / gNB->common_vars.num_beams_period,
                                              rel15_ul_ref->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx,
                                              p->numSpatialStreamIndices > 0 ? p->spatialStreamIndices[0] : 0);

  uint32_t bwp_start_subcarrier = (rel15_ul_ref->rb_start + rel15_ul_ref->bwp_start) * NR_NB_SC_PER_RB;
  LOG_D(PHY,
        "pusch %d.%d : bwp_start_subcarrier %d, rb_start %d\n",
        frame,
        slot,
        bwp_start_subcarrier,
        rel15_ul_ref->rb_start);
  LOG_D(PHY, "pusch %d.%d : ul_dmrs_symb_pos %x\n", frame, slot, rel15_ul_ref->ul_dmrs_symb_pos);

  // Softscope dumps the whole slot grid; clear unused symbols so they do not keep
  // stale constellation points. scopeData is set only when nrscope is loaded (--doscope).
  if (gNB->scopeData) {
    const int rxdataF_comp_symbol_size = ceil_mod(frame_parms->N_RB_UL * NR_NB_SC_PER_RB, 16);
    const int rxdataF_comp_slot_size = rxdataF_comp_symbol_size * frame_parms->symbols_per_slot;
    for (int ue = 0; ue < group_size; ue++) {
      NR_gNB_PUSCH *pusch_vars = pusch_vars_group[ue];
      const int n_buf = rel15_ul_group[ue]->nrOfLayers;
      for (int i = 0; i < n_buf; i++)
        memset(pusch_vars->rxdataF_comp[i], 0, sizeof(*pusch_vars->rxdataF_comp[i]) * rxdataF_comp_slot_size);
      memset(pusch_vars->ul_valid_re_per_slot, 0, sizeof(*pusch_vars->ul_valid_re_per_slot) * frame_parms->symbols_per_slot);
    }
  }

  // Memories to store data for data recording
  int buffer_length_slot = rel15_ul_ref->rb_size * NR_NB_SC_PER_RB * NR_SYMBOLS_PER_SLOT;
  // data recording application supports only a single layer.
  // nb_rx_ant (= frame_parms->nb_antennas_rx) is limited to 1 for data recording application.
  // int nb_layer (= rel15_ul->nrOfLayers) is limited to 1 for data recording application.

  // Initialize memory for DMRS signals
  c16_t pusch_dmrs_slot_mem[1 * buffer_length_slot] __attribute__((aligned(64)));
  // Initialize memory for channel estimates based on DMRS positions
  c16_t pusch_ch_est_dmrs_pos_slot_mem[buffer_length_slot * 1 * 1] __attribute__((aligned(64)));
  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  c16_t pusch_ch_est_dmrs_interpl_slot_mem[buffer_length_slot * 1 * 1] __attribute__((aligned(64)));
  // memory to store extracted data including PUSCH + DMRS
  c16_t rxFext_slot_mem[1 * buffer_length_slot] __attribute__((aligned(64)));

#if T_TRACER
  // Initialize memory for DMRS signals
  if (T_ACTIVE(T_GNB_PHY_UL_FD_DMRS))
    memset(pusch_dmrs_slot_mem, 0, sizeof(c16_t) * 1 * buffer_length_slot);

  // Initialize memory for channel estimates based on DMRS positions
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS))
    memset(pusch_ch_est_dmrs_pos_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);

  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL))
    memset(pusch_ch_est_dmrs_interpl_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);

  // memory to store extracted data including PUSCH + DMRS
  if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ))
    memset(rxFext_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * 1 * 1);
#endif

  // Create a virtual multi layer pdu by accumulating the layers over UEs in the group and storing dmrs ports for joint processing
  uint32_t combined_dmrs_ports = 0;
  int total_layers = 0;
  int layer_offset[group_size];
  for (int u = 0; u < group_size; u++) {
    const nfapi_nr_pusch_pdu_t *p = rel15_ul_group[u];
    combined_dmrs_ports |= p->dmrs_ports;
    layer_offset[u] = total_layers;
    total_layers += rel15_ul_group[u]->nrOfLayers;
  }
  AssertFatal(total_layers <= NR_MAX_NB_LAYERS,
              "MU-MIMO group total_layers=%d > NR_MAX_NB_LAYERS=%d\n",
              total_layers,
              NR_MAX_NB_LAYERS);

  nfapi_nr_pusch_pdu_t joint_pdu = *rel15_ul_ref;
  joint_pdu.nrOfLayers = total_layers;
  joint_pdu.dmrs_ports = combined_dmrs_ports;

  NR_gNB_PUSCH *joint_pv = pusch_vars_group[0];
  LOG_D(PHY,
        "%4u.%u MU-MIMO joint RX: %d UEs, %d total layers, rb_start=%u rb_size=%u qam=%u\n",
        frame,
        slot,
        group_size,
        total_layers,
        rel15_ul_ref->rb_start,
        rel15_ul_ref->rb_size,
        rel15_ul_ref->qam_mod_order);

  //----------------------------------------------------------
  //------------------- Channel estimation -------------------
  //----------------------------------------------------------
  start_meas(&gNB->ulsch_channel_estimation_stats);
  int max_ch = 0;
  uint32_t nvar = 0;
  int end_symbol = rel15_ul_ref->start_symbol_index + rel15_ul_ref->nr_of_symbols;
  uint8_t dmrs_symb_idx = 0;
  for (uint8_t symbol = rel15_ul_ref->start_symbol_index; symbol < end_symbol; symbol++) {
    uint8_t dmrs_symbol_flag = (rel15_ul_ref->ul_dmrs_symb_pos >> symbol) & 0x01;
    LOG_D(PHY, "symbol %d, dmrs_symbol_flag :%d\n", symbol, dmrs_symbol_flag);
    if (dmrs_symbol_flag == 1) {
      for (int u = 0; u < group_size; u++) {
        const nfapi_nr_pusch_pdu_t *p = rel15_ul_group[u];
        for (int nl = 0; nl < p->nrOfLayers; nl++) {
          int global_layer = layer_offset[u] + nl;
          uint32_t nvar_tmp = 0;
          nr_pusch_channel_estimation(gNB,
                                      slot,
                                      global_layer,
                                      get_dmrs_port(nl, p->dmrs_ports),
                                      dmrs_symb_idx,
                                      symbol,
                                      joint_pv,
                                      ant_port_start,
                                      bwp_start_subcarrier,
                                      &joint_pdu,
                                      &max_ch,
                                      &nvar_tmp,
                                      pusch_dmrs_slot_mem,
                                      pusch_ch_est_dmrs_pos_slot_mem);
          nvar += nvar_tmp;
        }
      }
      dmrs_symb_idx++;
    }
  }

  // PTRS processing.
  const bool is_ptrs = rel15_ul_ref->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS;
  c16_t cpe[NR_SYMBOLS_PER_SLOT];
  for (uint s = 0; s < NR_SYMBOLS_PER_SLOT; s++)
    cpe[s] = (c16_t){.r = INT16_MAX}; // zero phase error.
  nr_ptrs_info_t ptrs_info = {0};
  if (is_ptrs) {
    if (rel15_ul_ref->pusch_ptrs.num_ptrs_ports != 1)
      LOG_W(NR_PHY, "Multi-port PTRS not supported, skipping PTRS processing\n");
    else {
      const NR_DL_FRAME_PARMS *fp = frame_parms;
      ptrs_proc_t p = {.k_ptrs = rel15_ul_ref->pusch_ptrs.ptrs_freq_density,
                       .k_re_ref = rel15_ul_ref->pusch_ptrs.ptrs_ports_list[0].ptrs_re_offset,
                       .symbols_per_slot = fp->symbols_per_slot,
                       .start_rb = rel15_ul_ref->rb_start,
                       .num_rb = rel15_ul_ref->rb_size,
                       .N_RB = fp->N_RB_UL,
                       .start_symb = rel15_ul_ref->start_symbol_index,
                       .num_symb = rel15_ul_ref->nr_of_symbols,
                       .dmrs_symb_pos = rel15_ul_ref->ul_dmrs_symb_pos,
                       .nid = fp->Nid_cell,
                       .nscid = rel15_ul_ref->scid,
                       .ofdm_symbol_size = fp->ofdm_symbol_size,
                       .slot = slot,
                       .rnti = rel15_ul_ref->rnti};
      const int slot_offset = (p.slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * p.ofdm_symbol_size;
      c16_t *rxdataF = (c16_t *)&gNB->common_vars.rxdataF[ant_port_start][slot_offset];
      ptrs_info.n_ptrs =
          nr_ptrs_run(&p, rel15_ul_ref->pusch_ptrs.ptrs_time_density, rxdataF, (const c16_t *)joint_pv->ul_ch_estimates[0], cpe);
      ptrs_info.ptrs_symbols = p.ptrs_symb_pos;
    }
  }

  if (dmrs_symb_idx > 0)
    nvar /= (dmrs_symb_idx * total_layers);

  // averaging time domain channel estimates
  // Change to joint processing
  const uint8_t num_sp_streams = rel15_ul_ref->param_v4.numSpatialStreamIndices;
  if (gNB->chest_time == 1) {
    AssertFatal(!is_ptrs, "Time domain averaging of DMRS estimates not allowed with PTRS\n");
    nr_chest_time_domain_avg(frame_parms,
                             joint_pv->ul_ch_estimates,
                             rel15_ul_ref->nr_of_symbols,
                             rel15_ul_ref->start_symbol_index,
                             rel15_ul_ref->ul_dmrs_symb_pos, // change needed ?
                             rel15_ul_ref->rb_size,
                             total_layers,
                             num_sp_streams);
  }

  // ULSCH signal and noise power measurements
  // This is same for all the UEs in the group
  allocCast2D(n0_subband_power,
              unsigned int,
              gNB->measurements.n0_subband_power,
              frame_parms->nb_antennas_rx,
              frame_parms->N_RB_UL,
              false);

  int start_sc = (rel15_ul_ref->bwp_start + rel15_ul_ref->rb_start) * NR_NB_SC_PER_RB;
  for (int aa_pusch = 0; aa_pusch < num_sp_streams; aa_pusch++) {
    const int aarx = ant_port_start + aa_pusch;
    DevAssert(aarx < sizeofArray(joint_pv->ulsch_power));
    joint_pv->ulsch_power[aa_pusch] = 0;
    joint_pv->ulsch_noise_power[aa_pusch] = 0;
    int64_t symb_energy = 0;

    for (uint8_t symbol = rel15_ul_ref->start_symbol_index; symbol < end_symbol; symbol++) {
      int offset0 = ((slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot + symbol) * frame_parms->ofdm_symbol_size;
      int offset = offset0 + start_sc;
      c16_t *ul_ch = &gNB->common_vars.rxdataF[aarx][offset];
      symb_energy += signal_energy_nodc(ul_ch, rel15_ul_ref->rb_size * NR_NB_SC_PER_RB);
    }
    joint_pv->ulsch_power[aa_pusch] += (symb_energy / rel15_ul_ref->nr_of_symbols);

    joint_pv->ulsch_noise_power[aa_pusch] +=
        average_u32(&n0_subband_power[aarx][rel15_ul_ref->bwp_start + rel15_ul_ref->rb_start], rel15_ul_ref->rb_size);

    LOG_D(NR_PHY,
          "aa %d, bwp_start%d, rb_start %d, rb_size %d: ulsch_power %d, ulsch_noise_power %d\n",
          aarx,
          rel15_ul_ref->bwp_start,
          rel15_ul_ref->rb_start,
          rel15_ul_ref->rb_size,
          joint_pv->ulsch_power[aa_pusch],
          joint_pv->ulsch_noise_power[aa_pusch]);
  }
  stop_meas(&gNB->ulsch_channel_estimation_stats);

  start_meas(&gNB->rx_pusch_init_stats);

  // Calculate number of unavailable resources due to PTRS
  // This is assumed to be same for all the UEs (same PTRS configuration for all UEs)
  uint32_t unav_res = 0;
  if (rel15_ul_ref->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ptrs_info.ptrs_symbols, rel15_ul_ref->start_symbol_index, rel15_ul_ref->nr_of_symbols);
    unav_res = ptrs_info.n_ptrs * ptrsSymbPerSlot;
  }

  // Scrambling initialization
  int number_dmrs_symbols =
      count_bits64_with_mask(rel15_ul_ref->ul_dmrs_symb_pos, rel15_ul_ref->start_symbol_index, rel15_ul_ref->nr_of_symbols);
  int factor = rel15_ul_ref->dmrs_config_type == pusch_dmrs_type1 ? 6 : 4;
  int nb_re_dmrs = factor * rel15_ul_ref->num_dmrs_cdm_grps_no_data;

  int max_G = 0;
  int G[group_size];
  for (int u = 0; u < group_size; u++) {
    const nfapi_nr_pusch_pdu_t *p = rel15_ul_group[u];
    G[u] = nr_get_G(p->rb_size, p->nr_of_symbols, nb_re_dmrs, number_dmrs_symbols, unav_res, p->qam_mod_order, p->nrOfLayers);
    if (G[u] > max_G)
      max_G = G[u];
  }

  const uint64_t num_scrambling_bytes = group_size * (max_G + 96) * sizeof(int16_t);
  AssertFatal(num_scrambling_bytes <= NR_MAX_PUSCH_SCRAMBLING_STACK_BYTES,
              "scrambling_sequences stack buffer %" PRIu64 " bytes exceeds %d MB limit : group_size %d, max_G %d\n",
              num_scrambling_bytes,
              NR_MAX_PUSCH_SCRAMBLING_STACK_BYTES >> 20,
              group_size,
              max_G);

  int16_t scrambling_sequences[group_size][max_G + 96] __attribute__((aligned(32)));
  int16_t *scrambling_sequences_arr[group_size];

  for (int u = 0; u < group_size; u++) {
    scrambling_sequences_arr[u] = scrambling_sequences[u];
    const nfapi_nr_pusch_pdu_t *p = rel15_ul_group[u];
    nr_codeword_unscrambling_init(scrambling_sequences_arr[u], G[u], 0, p->data_scrambling_id, p->rnti);
  }

  int meas_symbol = -1;
  for (int sym = 0; sym < frame_parms->symbols_per_slot; sym++) {
    if (sym >= rel15_ul_ref->start_symbol_index && sym < rel15_ul_ref->start_symbol_index + rel15_ul_ref->nr_of_symbols) {
      joint_pv->ul_valid_re_per_slot[sym] = get_nb_re_pusch(frame_parms, &joint_pdu, sym, &ptrs_info);
      if (meas_symbol == -1 && joint_pv->ul_valid_re_per_slot[sym] != 0)
        meas_symbol = sym;
    } else
      joint_pv->ul_valid_re_per_slot[sym] = 0;
  }

  int nb_re_pusch = joint_pv->ul_valid_re_per_slot[meas_symbol];
  AssertFatal(nb_re_pusch > 0 && meas_symbol >= 0,
              "nb_re_pusch %d cannot be 0 or meas_symbol %d cannot be negative here\n",
              nb_re_pusch,
              meas_symbol);

  // extract the first dmrs for the channel level computation
  // extract the data in the OFDM frame, to the start of the array
  int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;

  nb_re_pusch = ceil_mod(nb_re_pusch, 16);
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = get_valid_dmrs_idx_for_channel_est(rel15_ul_ref->ul_dmrs_symb_pos, meas_symbol);
  else // average of channel estimates stored in first symbol
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul_ref->ul_dmrs_symb_pos, rel15_ul_ref->start_symbol_index, end_symbol);
  int size_est = ceil_mod(nb_re_pusch * frame_parms->symbols_per_slot, 16);
  __attribute__((aligned(64))) c16_t ul_ch_estimates_ext[total_layers][num_sp_streams][size_est];
  memset(ul_ch_estimates_ext, 0, sizeof(ul_ch_estimates_ext));
  int buffer_length = rel15_ul_ref->rb_size * NR_NB_SC_PER_RB;
  c16_t temp_rxFext[num_sp_streams][buffer_length] __attribute__((aligned(32)));
  for (int aarx = 0; aarx < num_sp_streams; aarx++)
    for (int nl = 0; nl < total_layers; nl++) {
      start_meas(&gNB->pusch_extraction_stats);
      nr_ulsch_extract_rbs(gNB->common_vars.rxdataF[ant_port_start + aarx] + soffset + meas_symbol * frame_parms->ofdm_symbol_size,
                           (c16_t *)joint_pv->ul_ch_estimates[nl * num_sp_streams + aarx],
                           temp_rxFext[aarx],
                           &ul_ch_estimates_ext[nl][aarx][meas_symbol * nb_re_pusch],
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           (rel15_ul_ref->ul_dmrs_symb_pos >> meas_symbol) & 0x01,
                           &joint_pdu,
                           frame_parms,
                           rel15_ul_ref->rnti,
                           IS_BIT_SET(ptrs_info.ptrs_symbols, meas_symbol));
      stop_meas(&gNB->pusch_extraction_stats);
    }

  //----------------------------------------------------------
  //--------------------- Channel Scaling --------------------
  //----------------------------------------------------------

  int avg[total_layers][num_sp_streams];
  for (int i = 0; i < total_layers; i++)
    nr_channel_level(meas_symbol, size_est, ul_ch_estimates_ext[i], num_sp_streams, avg[i], nb_re_pusch);

  int avgs = 0;
  for (int nl = 0; nl < total_layers; nl++)
    for (int aarx = 0; aarx < num_sp_streams; aarx++)
      avgs = cmax(avgs, avg[nl][aarx]);

  if (total_layers == 2 && rel15_ul_ref->qam_mod_order > 6)
    joint_pv->log2_maxh = (log2_approx(avgs) >> 1) - 3; // for MMSE
  else if (total_layers == 2)
    joint_pv->log2_maxh = (log2_approx(avgs) >> 1) - 2 + log2_approx(num_sp_streams >> 1);
  else
    joint_pv->log2_maxh = (log2_approx(avgs) >> 1) + 1 + log2_approx(num_sp_streams >> 1);

  if (joint_pv->log2_maxh < 1)
    joint_pv->log2_maxh = 1;
  else if (joint_pv->log2_maxh > 14)
    joint_pv->log2_maxh = 14;

  nr_uci_mapping_t map_uci[group_size];
  for (int u = 0; u < group_size; u++) {
    pusch_vars_group[u]->uci_info = get_uci_on_pusch_info(rel15_ul_group[u], &ptrs_info, G[u]);
    map_uci[u] = init_nr_uci_pusch_demux(rel15_ul_group[u], &pusch_vars_group[u]->uci_info, frame_parms, joint_pv);
  }
  stop_meas(&gNB->rx_pusch_init_stats);

  start_meas(&gNB->rx_pusch_symbol_processing_stats);
  int numSymbols = gNB->num_pusch_symbols_per_thread;
  int total_res = 0;
  int const loop_iter = CEILIDIV(rel15_ul_ref->nr_of_symbols, numSymbols);
  puschSymbolProc_t arr[loop_iter];
  task_ans_t ans;
  init_task_ans(&ans, loop_iter);

  int sz_arr = 0;
  for (uint8_t task_index = 0; task_index < loop_iter; task_index++) {
    int symbol = task_index * numSymbols + rel15_ul_ref->start_symbol_index;
    int res_per_task = 0;
    for (int s = 0; s < numSymbols && s + symbol < end_symbol; s++) {
      int curr_sym = symbol + s;
      if (curr_sym == rel15_ul_ref->start_symbol_index) {
        joint_pv->llr_offset[curr_sym] = 0;
      } else {
        int prev_sym = curr_sym - 1;
        int prev_offset = joint_pv->llr_offset[prev_sym];
        int prev_re = joint_pv->ul_valid_re_per_slot[prev_sym];
        int mod_order = rel15_ul_ref->qam_mod_order;
        joint_pv->llr_offset[curr_sym] = prev_offset + (prev_re * mod_order);
      }
      res_per_task += joint_pv->ul_valid_re_per_slot[curr_sym];
    }
    total_res += res_per_task;
    if (res_per_task > 0) {
      puschSymbolProc_t *rdata = &arr[sz_arr];
      rdata->ans = &ans;
      ++sz_arr;

      rdata->gNB = gNB;
      rdata->frame_parms = frame_parms;
      rdata->rel15_ul = &joint_pdu;
      rdata->slot = slot;
      rdata->startSymbol = symbol;
      // Last task processes remainder symbols
      rdata->numSymbols = task_index == loop_iter - 1 ? rel15_ul_ref->nr_of_symbols - (loop_iter - 1) * numSymbols : numSymbols;
      rdata->pusch_vars = joint_pv;
      rdata->ptrs_symb_pos = ptrs_info.ptrs_symbols;
      rdata->ptrs_cpe = cpe;
      rdata->nvar = nvar;
      rdata->ant_port_start = ant_port_start;
      rdata->rxFext_slot_mem = rxFext_slot_mem;
      rdata->pusch_ch_est_dmrs_interpl_slot_mem = pusch_ch_est_dmrs_interpl_slot_mem;
      rdata->group_size = group_size;
      rdata->rel15_ul_group = rel15_ul_group;
      rdata->pusch_vars_group = pusch_vars_group;
      rdata->scrambling_sequences = scrambling_sequences_arr;
      rdata->layer_offsets = layer_offset;
      rdata->layers_attenuation = total_layers ? log2_approx(max_ch >> 11) : 0;
      rdata->map_uci = map_uci;

      task_t t = {.func = &nr_pusch_symbol_processing, .args = rdata};
      pushTpool(&gNB->threadPool, t);

      LOG_D(PHY, "%d.%d Added symbol %d to process, in pipe\n", frame, slot, symbol);
    } else {
      completed_task_ans(&ans);
    }
  } // symbol loop

#if T_TRACER
  int dmrs_port = get_dmrs_port(0, rel15_ul_ref->dmrs_ports);

  log_ul_fd_dmrs(frame,
                 slot,
                 frame_parms,
                 rel15_ul_ref,
                 number_dmrs_symbols,
                 dmrs_port,
                 (const c16_t *)(&(pusch_dmrs_slot_mem[0])),
                 rel15_ul_ref->rb_size * NR_NB_SC_PER_RB * rel15_ul_ref->nr_of_symbols * 4);

  log_ul_fd_chan_est_dmrs_pos(frame,
                              slot,
                              frame_parms,
                              rel15_ul_ref,
                              number_dmrs_symbols,
                              dmrs_port,
                              (const c16_t *)(&(pusch_ch_est_dmrs_pos_slot_mem[0])),
                              rel15_ul_ref->rb_size * NR_NB_SC_PER_RB * rel15_ul_ref->nr_of_symbols * 4);

  log_ul_fd_pusch_iq(frame,
                     slot,
                     frame_parms,
                     rel15_ul_ref,
                     number_dmrs_symbols,
                     dmrs_port,
                     (const c16_t *)(&(rxFext_slot_mem[0])),
                     rel15_ul_ref->rb_size * NR_NB_SC_PER_RB * rel15_ul_ref->nr_of_symbols * num_sp_streams * 4);

  log_ul_fd_chan_est_dmrs_interpl(
      frame,
      slot,
      frame_parms,
      rel15_ul_ref,
      number_dmrs_symbols,
      dmrs_port,
      (const c16_t *)pusch_ch_est_dmrs_interpl_slot_mem,
      rel15_ul_ref->rb_size * NR_NB_SC_PER_RB * rel15_ul_ref->nr_of_symbols * num_sp_streams * total_layers * 4);
#endif

  join_task_ans(&ans);
  for (int u = 0; u < group_size; u++) {
    NR_gNB_PUSCH *pv = pusch_vars_group[u];
    // Copy unavailable resources per UE
    *ret_unav_res_group[u] = unav_res;
    // Copy power measurements per UE
    pv->ulsch_power_tot = 0;
    pv->ulsch_noise_power_tot = 0;
    for (int aarx = 0; aarx < num_sp_streams; aarx++) {
      pv->ulsch_power[aarx] = joint_pv->ulsch_power[aarx];
      pv->ulsch_noise_power[aarx] = joint_pv->ulsch_noise_power[aarx];
      pv->ulsch_power_tot += pv->ulsch_power[aarx];
      pv->ulsch_noise_power_tot += pv->ulsch_noise_power[aarx];
    }
  }
  stop_meas(&gNB->rx_pusch_symbol_processing_stats);

  // Copy the data to the scope. This cannot be performed in one call to gNBscopeCopy because the data is not contiguous in the
  // buffer due to reference symbol extraction and padding. The gNBscopeCopy call is broken up into steps: trylock, copy, unlock.
  metadata mt = {.slot = slot, .frame = frame};
  if (gNBTryLockScopeData(gNB, gNBPuschRxIq, sizeof(c16_t), 1, total_res, &mt)) {
    int buffer_length = ceil_mod(rel15_ul_ref->rb_size * NR_NB_SC_PER_RB, 16);
    size_t offset = 0;
    for (uint8_t symbol = rel15_ul_ref->start_symbol_index;
         symbol < (rel15_ul_ref->start_symbol_index + rel15_ul_ref->nr_of_symbols);
         symbol++) {
      gNBscopeCopyUnsafe(gNB,
                         gNBPuschRxIq,
                         &pusch_vars_group[0]->rxdataF_comp[0][symbol * buffer_length],
                         sizeof(c16_t) * pusch_vars_group[0]->ul_valid_re_per_slot[symbol],
                         offset,
                         symbol - rel15_ul_ref->start_symbol_index);
      offset += sizeof(c16_t) * pusch_vars_group[0]->ul_valid_re_per_slot[symbol];
    }
    gNBunlockScopeData(gNB, gNBPuschRxIq)
  }
  uint32_t total_llrs = total_res * rel15_ul_ref->qam_mod_order * rel15_ul_ref->nrOfLayers;
  gNBscopeCopyWithMetadata(gNB, gNBPuschLlr, pusch_vars_group[0]->ulsch_llrs, sizeof(c16_t), 1, total_llrs, 0, &mt);
  return 0;
}
