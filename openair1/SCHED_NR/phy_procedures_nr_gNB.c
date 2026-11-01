/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "common/utils/fsn.h"
#include "PHY/defs_gNB.h"
#include "sched_nr.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_dlsch.h"
#include "PHY/NR_TRANSPORT/nr_ulsch.h"
#include "PHY/NR_TRANSPORT/nr_dci.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/nr_phy_common/inc/nr_phy_meas.h"
#include "nfapi/open-nFAPI/nfapi/public_inc/nfapi_interface.h"
#include "common/utils/LOG/log.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common_csi_rs.h"
#include "PHY/nr_phy_common/inc/nr_phy_common_srs.h"
#include "PHY/NR_REFSIG/ss_pbch_nr.h"
#include "T.h"
#include "T_messages_creator.h"
#include "executables/nr-softmodem.h"
#include "executables/softmodem-common.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "assertions.h"
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <openair1/PHY/TOOLS/phy_scope_interface.h>

#ifdef E3_AGENT
#include "openair2/E3AP/service_models/l1_kpm_sm/e3_ran_buffers.h"
#include "openair2/E3AP/service_models/l1_kpm_sm/l1_kpm_sm.h"
#endif // E3_AGENT

//#define DEBUG_RXDATA
//#define SRS_IND_DEBUG

typedef struct {
  int jobs[MAX_UL_PDUS_PER_SLOT][MAX_UL_PDUS_PER_SLOT];
  uint8_t size[MAX_UL_PDUS_PER_SLOT];
  int16_t active_groups[MAX_UL_PDUS_PER_SLOT];
  int n_active_groups;
} NR_MU_MIMO_job_groups_t;

static void nr_fill_indication(const PHY_VARS_gNB *gNB,
                               int frame,
                               int slot_rx,
                               const NR_gNB_PUSCH *pusch,
                               const nfapi_nr_pusch_pdu_t *pusch_pdu,
                               NR_gNB_PHY_STATS_t *stats,
                               uint8_t *payload,
                               int dtx_flag,
                               nfapi_nr_crc_t *crc,
                               nfapi_nr_rx_data_pdu_t *pdu);

static inline bool prb_is_rotated(const uint64_t *prb_mask, int prb)
{
  return (prb_mask[prb >> 6] >> (prb & 63)) & 0x1;
}

static inline void mark_prb(uint64_t *prb_mask, int prb_mask_words, int symbol, int prb)
{
  uint64_t *symbol_mask = prb_mask + symbol * prb_mask_words;
  symbol_mask[prb >> 6] |= UINT64_C(1) << (prb & 63);
}

static inline void mark_prb_range(uint64_t *prb_mask, int prb_mask_words, int symbol, int first_prb, int nb_prb)
{
  for (int prb = first_prb; prb < first_prb + nb_prb; prb++)
    mark_prb(prb_mask, prb_mask_words, symbol, prb);
}

static inline void apply_nr_rotation_TX_segment(const NR_DL_FRAME_PARMS *fp,
                                                c16_t *txdataF,
                                                const c16_t *symbol_rotation,
                                                int slot,
                                                int symbol,
                                                int first_prb,
                                                int nb_prb)
{
  const int symb_offset = (slot % fp->slots_per_subframe) * fp->symbols_per_slot;
  const c16_t rot = symbol_rotation[symb_offset + symbol];
  c16_t *this_symbol = txdataF + symbol * fp->ofdm_symbol_size + first_prb * NR_NB_SC_PER_RB;
  rotate_cpx_vector(this_symbol, rot, this_symbol, nb_prb * NR_NB_SC_PER_RB, 15);
}

static void apply_nr_rotation_TX_masked(const NR_DL_FRAME_PARMS *fp,
                                        c16_t **txdataF,
                                        int num_tx_buffers,
                                        const c16_t *symbol_rotation,
                                        int slot,
                                        const uint64_t *local_prb_mask,
                                        int prb_mask_words)
{
  for (int aa = 0; aa < num_tx_buffers; aa++) {
    if (!txdataF[aa])
      continue;
    for (int symbol = 0; symbol < fp->symbols_per_slot; symbol++) {
      const uint64_t *local_symbol_mask = local_prb_mask + symbol * prb_mask_words;
      int prb = 0;
      while (prb < fp->N_RB_DL) {
        while (prb < fp->N_RB_DL && !prb_is_rotated(local_symbol_mask, prb))
          prb++;
        const int start_prb = prb;
        while (prb < fp->N_RB_DL && prb_is_rotated(local_symbol_mask, prb))
          prb++;
        if (prb > start_prb) {
          apply_nr_rotation_TX_segment(fp, txdataF[aa], symbol_rotation, slot, symbol, start_prb, prb - start_prb);
        }
      }
    }
  }
}

void beam_index_allocation(uint16_t fapi_beam_index,
                           int ant,
                           int num_ports,
                           int symbols_per_slot,
                           int slot,
                           uint16_t bitmap_symbols,
                           int num_ant_max,
                           uint16_t **ant_beam_id_list)
{
  if (!ant_beam_id_list)
    return;

  AssertFatal(IS_BIT_SET(fapi_beam_index, 15), "Can't handle preconfigured DBM yet\n");
  uint16_t ru_beam_idx = fapi_beam_index & 0x7fff;
  for (int j = 0; j < symbols_per_slot; j++) {
    if (((bitmap_symbols >> j) & 0x01))
      for (uint_fast8_t p = 0; p < num_ports; p++) {
        DevAssert(ant + p < num_ant_max);
        ant_beam_id_list[slot * symbols_per_slot + j][ant + p] = ru_beam_idx;
      }
  }
}

// Temporary workaround to get antenna ports for DAS. After L1 digital
// beamforming is implemented, DAS case can be handled with a special DBT in
// config file and this function can be removed.
uint16_t get_first_ant_idx(bool das, uint16_t num_ports_beams, uint16_t beam_id, uint16_t fapi_start_port)
{
  return ((das) ? (beam_id & 0x7fff) * num_ports_beams : fapi_start_port);
}

void nr_common_signal_procedures(PHY_VARS_gNB *gNB, int frame, int slot, const nfapi_nr_dl_tti_ssb_pdu *ssb_pdu, int prb_mask_words)
{
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  const nfapi_nr_dl_tti_ssb_pdu_rel15_t *pdu = &ssb_pdu->ssb_pdu_rel15;
  uint8_t ssb_index = pdu->SsbBlockIndex;
  LOG_D(PHY,"common_signal_procedures: frame %d, slot %d ssb index %d\n", frame, slot, ssb_index);

  int ssb_start_symbol_abs = nr_get_ssb_start_symbol(fp, ssb_index); // computing the starting symbol for current ssb
  uint16_t ssb_start_symbol = ssb_start_symbol_abs % fp->symbols_per_slot;  // start symbol wrt slot

  // Setting the first subcarrier
  // 3GPP TS 38.211 sections 7.4.3.1 and 4.4.4.2
  // for FR1 offsetToPointA and k_SSB are expressed in terms of 15 kHz SCS
  // for FR2 offsetToPointA is expressed in terms of 60 kHz SCS and k_SSB expressed in terms of the subcarrier spacing provided
  // by the higher-layer parameter subCarrierSpacingCommon
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  const int scs = cfg->ssb_config.scs_common.value;
  fp->ssb_start_subcarrier = nr_get_ssb_start_sc(scs,
                                                 pdu->ssbOffsetPointA,
                                                 pdu->SsbSubcarrierOffset,
                                                 fp->freq_range);

  if (fp->print_ue_help_cmdline_log && IS_SA_MODE(get_softmodem_params())) {
    fp->print_ue_help_cmdline_log = false;
    if (fp->dl_CarrierFreq != fp->ul_CarrierFreq)
      LOG_A(PHY,
            "Command line parameters for OAI UE: -C %lu --CO %ld -r %d --numerology %d --ssb %d %s\n",
            fp->dl_CarrierFreq,
            fp->ul_CarrierFreq - fp->dl_CarrierFreq,
            fp->N_RB_DL,
            scs,
            fp->ssb_start_subcarrier,
            fp->threequarter_fs ? "-E" : "");
    else
      LOG_A(PHY,
            "Command line parameters for OAI UE: -C %lu -r %d --numerology %d --ssb %d %s\n",
            fp->dl_CarrierFreq,
            fp->N_RB_DL,
            scs,
            fp->ssb_start_subcarrier,
            fp->threequarter_fs ? "-E" : "");
  }
  LOG_D(PHY,"SS TX: frame %d, slot %d, start_symbol %d\n", frame, slot, ssb_start_symbol);
  const nfapi_nr_tx_precoding_and_beamforming_t *pb = &pdu->precoding_and_beamforming;
  c16_t **txdataF = gNB->common_vars.txdataF;
  // beam number in a scenario with multiple concurrent beams
  uint16_t sym_bitmap = SL_to_bitmap(ssb_start_symbol, 4); // 4 ssb symbols
  uint16_t beam_id = pb->prgs_list[0].dig_bf_interface_list[0].beam_idx;
  uint16_t ant_port = get_first_ant_idx(gNB->enable_analog_das,
                                        fp->nb_antennas_tx / gNB->common_vars.num_beams_period,
                                        beam_id,
                                        (pdu->param_v4.spatialStreamIndexPresent ? pdu->param_v4.spatialStreamIndex : 0));
  beam_index_allocation(beam_id, ant_port, 1, fp->symbols_per_slot, slot, sym_bitmap, fp->nb_antennas_tx, gNB->common_vars.beam_id);

  nr_generate_pss(txdataF[ant_port], gNB->TX_AMP, ssb_start_symbol, cfg, fp);
  nr_generate_sss(txdataF[ant_port], gNB->TX_AMP, ssb_start_symbol, cfg->cell_config.phy_cell_id.value, fp);

  uint16_t slots_per_hf = (fp->slots_per_frame) >> 1;
  int n_hf = slot < slots_per_hf ? 0 : 1;

  int hf = fp->Lmax == 4 ? n_hf : 0;
  nr_generate_pbch_dmrs(nr_gold_pbch(fp->Lmax, gNB->gNB_config.cell_config.phy_cell_id.value, hf, ssb_index & 7),
                        txdataF[ant_port],
                        gNB->TX_AMP,
                        ssb_start_symbol,
                        cfg,
                        fp);

#if T_TRACER
  if (T_ACTIVE(T_GNB_PHY_MIB)) {
    unsigned char bch[3];
    bch[0] = pdu->bchPayload & 0xff;
    bch[1] = (pdu->bchPayload >> 8) & 0xff;
    bch[2] = (pdu->bchPayload >> 16) & 0xff;
    T(T_GNB_PHY_MIB, T_INT(0) /* module ID */, T_INT(frame), T_INT(slot), T_BUFFER(bch, 3));
  }
#endif

  nr_generate_pbch(gNB, ssb_pdu, txdataF[ant_port], ssb_start_symbol, n_hf, frame, cfg, fp);

  if (!gNB->phase_comp)
    return;

  uint64_t local_phase_comp_prb_mask[fp->symbols_per_slot][prb_mask_words];
  memset(local_phase_comp_prb_mask, 0, sizeof(local_phase_comp_prb_mask));
  const int ssb_start_prb = fp->ssb_start_subcarrier / NR_NB_SC_PER_RB;
  const int ssb_nb_prb = (fp->ssb_start_subcarrier % NR_NB_SC_PER_RB + 240 + NR_NB_SC_PER_RB - 1) / NR_NB_SC_PER_RB;
  for (int symbol = ssb_start_symbol; symbol < ssb_start_symbol + NR_N_SYMBOLS_SSB; symbol++)
    mark_prb_range(&local_phase_comp_prb_mask[0][0], prb_mask_words, symbol, ssb_start_prb, ssb_nb_prb);

  /* the SSB was written to txdataF[ant_port] above, so rotate that port -- not port 0 */
  c16_t *ssb_txdataF[] = {txdataF[ant_port]};
  apply_nr_rotation_TX_masked(fp, ssb_txdataF, 1, fp->symbol_rotation[0], slot, &local_phase_comp_prb_mask[0][0], prb_mask_words);
}

// clearing beam information to be provided to RU for all slots (DL and UL)
void clear_slot_beamid(PHY_VARS_gNB *gNB, int slot)
{
  LOG_D(PHY, "Clearing beam_id structure for slot %d\n", slot);
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  int slot_sz = fp->symbols_per_slot;
  if (gNB->common_vars.beam_id)
    for (int i = 0; i < slot_sz; i++) {
      memset(gNB->common_vars.beam_id[slot * slot_sz + i], 0, fp->nb_antennas_tx * sizeof(**gNB->common_vars.beam_id));
    }
}

static void nr_generate_csi_rs_gNB(PHY_VARS_gNB *gNB, int slot, const nfapi_nr_dl_tti_csi_rs_pdu *csi_rs_pdu, int prb_mask_words)
{
  const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *csi_params = &csi_rs_pdu->csi_rs_pdu_rel15;
  if (csi_params->csi_type == 2) // ZP-CSI
    return;

  csi_mapping_parms_t mapping_parms =
      get_csi_mapping_parms(csi_params->row, csi_params->freq_domain, csi_params->symb_l0, csi_params->symb_l1);
  const nfapi_nr_tx_precoding_and_beamforming_t *pb = &csi_params->precodingAndBeamforming;
  uint16_t csi_bitmap = 0;
  int lprime_num = mapping_parms.lprime + 1;
  for (int j = 0; j < mapping_parms.size; j++)
    csi_bitmap |= ((1 << lprime_num) - 1) << mapping_parms.loverline[j];
  // Get first antenna port index for CSI assigned by L2
  AssertFatal(
      csi_params->param_v4.numSpatialStreamIndices <= 1,
      "In current implementation, CSI spatial stream indexing is used to specify the first antenna port. So it cannot be > 1: %d\n",
      csi_params->param_v4.numSpatialStreamIndices);
  const uint16_t beam_id = pb->prgs_list[0].dig_bf_interface_list[0].beam_idx;
  const uint16_t ant_port_offset =
      get_first_ant_idx(gNB->enable_analog_das,
                        gNB->frame_parms.nb_antennas_tx / gNB->common_vars.num_beams_period,
                        beam_id,
                        csi_params->param_v4.numSpatialStreamIndices > 0 ? csi_params->param_v4.spatialStreamIndices[0] : 0);
  const int group_sz = get_cdm_group_size(csi_params->cdm_type);
  const uint16_t start_port = mapping_parms.j[0] * group_sz; // Start port of this CSI config
  const uint16_t num_ports = mapping_parms.size * group_sz;
  beam_index_allocation(beam_id,
                        ant_port_offset + start_port,
                        num_ports,
                        gNB->frame_parms.symbols_per_slot,
                        slot,
                        csi_bitmap,
                        gNB->frame_parms.nb_antennas_tx,
                        gNB->common_vars.beam_id);

  nr_generate_csi_rs(&gNB->frame_parms,
                     &mapping_parms,
                     gNB->TX_AMP,
                     slot,
                     csi_params->freq_density,
                     csi_params->start_rb,
                     csi_params->nr_of_rbs,
                     csi_params->symb_l0,
                     csi_params->symb_l1,
                     csi_params->row,
                     csi_params->scramb_id,
                     csi_params->power_control_offset_ss,
                     csi_params->cdm_type,
                     gNB->common_vars.txdataF + ant_port_offset);

  if (!gNB->phase_comp)
    return;

  uint64_t local_phase_comp_prb_mask[gNB->frame_parms.symbols_per_slot][prb_mask_words];
  memset(local_phase_comp_prb_mask, 0, sizeof(local_phase_comp_prb_mask));
  for (int rb = csi_params->start_rb; rb < csi_params->start_rb + csi_params->nr_of_rbs; rb++) {
    if ((csi_params->freq_density <= 1) && (csi_params->freq_density != (rb % 2)))
      continue;
    for (int j = 0; j < mapping_parms.size; j++) {
      for (int lp = 0; lp <= mapping_parms.lprime; lp++) {
        const int symbol = mapping_parms.loverline[j] + lp;
        mark_prb(&local_phase_comp_prb_mask[0][0], prb_mask_words, symbol, rb);
      }
    }
  }

  apply_nr_rotation_TX_masked(&gNB->frame_parms,
                              gNB->common_vars.txdataF,
                              min(mapping_parms.ports, gNB->gNB_config.carrier_config.num_tx_ant.value),
                              gNB->frame_parms.symbol_rotation[0],
                              slot,
                              &local_phase_comp_prb_mask[0][0],
                              prb_mask_words);
}

static void nr_generate_prs_gNB(PHY_VARS_gNB *gNB, int slot, int slot_prs, prs_config_t *prs_config, int prb_mask_words)
{
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  nr_generate_prs(slot_prs, gNB->common_vars.txdataF[0], AMP, prs_config, fp);

  if (!gNB->phase_comp)
    return;

  uint64_t local_phase_comp_prb_mask[fp->symbols_per_slot][prb_mask_words];
  memset(local_phase_comp_prb_mask, 0, sizeof(local_phase_comp_prb_mask));
  for (int symbol = prs_config->SymbolStart; symbol < prs_config->SymbolStart + prs_config->NumPRSSymbols; symbol++)
    mark_prb_range(&local_phase_comp_prb_mask[0][0], prb_mask_words, symbol, prs_config->RBOffset, prs_config->NumRB);

  c16_t *prs_txdataF[] = {gNB->common_vars.txdataF[0]};
  apply_nr_rotation_TX_masked(fp, prs_txdataF, 1, fp->symbol_rotation[0], slot, &local_phase_comp_prb_mask[0][0], prb_mask_words);
}

void phy_procedures_gNB_TX(PHY_VARS_gNB *gNB,
                           const nfapi_nr_dl_tti_request_t *DL_req,
                           const nfapi_nr_tx_data_request_t *TX_req,
                           const nfapi_nr_ul_dci_request_t *UL_dci_req,
                           int frame,
                           int slot)
{
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;

  if ((cfg->cell_config.frame_duplex_type.value == TDD) && (nr_slot_select(cfg,frame,slot) == NR_UPLINK_SLOT))
    return;

  const int prb_mask_words = (fp->N_RB_DL + 63) >> 6;

  // clear the transmit data array and beam index for the current slot
  for (int aa = 0; aa < fp->nb_antennas_tx; aa++) {
    memset(gNB->common_vars.txdataF[aa], 0, fp->samples_per_slot_wCP * sizeof(**gNB->common_vars.txdataF));
  }

  // Check for PRS slot - section 7.4.1.7.4 in 3GPP rel16 38.211
  for(int rsc_id = 0; rsc_id < gNB->prs_vars.NumPRSResources; rsc_id++)
  {
    prs_config_t *prs_config = &gNB->prs_vars.prs_cfg[rsc_id];
    for (int i = 0; i < prs_config->PRSResourceRepetition; i++)
    {
      if( (((frame*fp->slots_per_frame + slot) - (prs_config->PRSResourceSetPeriod[1] + prs_config->PRSResourceOffset)+prs_config->PRSResourceSetPeriod[0])%prs_config->PRSResourceSetPeriod[0]) == i*prs_config->PRSResourceTimeGap )
      {
        int slot_prs = (slot - i * prs_config->PRSResourceTimeGap + fp->slots_per_frame) % fp->slots_per_frame;
        LOG_D(PHY,"gNB_TX: frame %d, slot %d, slot_prs %d, PRS Resource ID %d\n",frame, slot, slot_prs, rsc_id);
        nr_generate_prs_gNB(gNB, slot, slot_prs, prs_config, prb_mask_words);
      }
    }
  }

  for (int i = 0; i < UL_dci_req->numPdus; ++i)
    nr_generate_dci(gNB, &UL_dci_req->ul_dci_pdu_list[i].pdcch_pdu.pdcch_pdu_rel15, &gNB->frame_parms, slot, prb_mask_words);

  int num_pdsch = 0;
  for (int i = 0; i < DL_req->dl_tti_request_body.nPDUs; ++i) {
    const nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdu = &DL_req->dl_tti_request_body.dl_tti_pdu_list[i];
    switch (dl_tti_pdu->PDUType) {
      case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
        nr_common_signal_procedures(gNB, frame, slot, &dl_tti_pdu->ssb_pdu, prb_mask_words);
        break;
      case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
        nr_generate_dci(gNB, &dl_tti_pdu->pdcch_pdu.pdcch_pdu_rel15, &gNB->frame_parms, slot, prb_mask_words);
        break;
      case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
        nr_generate_csi_rs_gNB(gNB, slot, &dl_tti_pdu->csi_rs_pdu, prb_mask_words);
        break;
      case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE: {
        int tx_data_idx = dl_tti_pdu->pdsch_pdu.pdsch_pdu_rel15.pduIndex;
        if (tx_data_idx < TX_req->Number_of_PDUs && TX_req->pdu_list[tx_data_idx].PDU_index == tx_data_idx) {
          // reuse dlsch variables, as there are multiple very large memory
          // buffers
          gNB->dlsch[num_pdsch].pdsch_pdu = &dl_tti_pdu->pdsch_pdu;
          const nfapi_nr_tx_data_request_tlv_t *tlv = &TX_req->pdu_list[tx_data_idx].TLVs[0];
          gNB->dlsch[num_pdsch].pdu = tlv->tag == 0 ? (uint8_t *)tlv->value.direct : (uint8_t *)tlv->value.ptr;
          DevAssert(num_pdsch < gNB->max_nb_pdsch);
          num_pdsch++;
        } else {
          LOG_E(NR_PHY,
                "%4d.%2d no corresponding tx_data.request for dl_tti.request index %d (out of %d)\n",
                frame,
                slot,
                tx_data_idx,
                TX_req->Number_of_PDUs);
        }
        } break;
    }
  }
 
  if (num_pdsch > 0) {
    LOG_D(PHY, "PDSCH generation started (%d) in frame %d.%d\n", num_pdsch, frame, slot);
    nr_generate_pdsch(gNB, num_pdsch, gNB->dlsch, frame, slot);
  }

  for (int aa = 0; aa < fp->nb_antennas_tx; aa++) {
    T(T_GNB_PHY_DL_OUTPUT_SIGNAL,
      T_INT(0),
      T_INT(frame),
      T_INT(slot),
      T_INT(aa),
      T_BUFFER(gNB->common_vars.txdataF[aa], fp->samples_per_slot_wCP * sizeof(int32_t)));
  }
}

static int nr_ulsch_procedures(PHY_VARS_gNB *gNB, int frame_rx, int slot_rx, int *ulsch_to_decode, int nb_pusch, NR_UL_IND_t *UL_INFO)
{
  DevAssert(nb_pusch > 0);
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;

  nr_uci_decoding(gNB, UL_INFO, nb_pusch, ulsch_to_decode, frame_rx, slot_rx);

  //----------------------------------------------------------
  //--------------------- ULSCH decoding ---------------------
  //----------------------------------------------------------

  int ret_nr_ulsch_decoding = nr_ulsch_decoding(gNB, frame_parms, frame_rx, slot_rx, ulsch_to_decode, nb_pusch);

  // CRC check per uplink shared channel
  for (int pusch_id = 0; pusch_id < nb_pusch; pusch_id++) {
    uint8_t ULSCH_id = ulsch_to_decode[pusch_id];
    NR_gNB_ULSCH_t *ulsch = &gNB->ulsch[ULSCH_id];
    NR_gNB_PUSCH *pusch = &gNB->pusch_vars[ULSCH_id];
    NR_UL_gNB_HARQ_t *ulsch_harq = ulsch->harq_process;
    NR_gNB_PHY_STATS_t *stats = get_phy_stats(gNB, ulsch->rnti);
    const nfapi_nr_pusch_pdu_t *pusch_pdu = &ulsch_harq->ulsch_pdu;
    if (!(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA))
      continue;

    bool crc_valid = false;
    // if all segments are done
    if (ulsch_harq->processedSegments == ulsch_harq->C) {
      if (ulsch_harq->C > 1) {
        int tbs = pusch_pdu->pusch_data.tb_size;
        crc_valid = check_crc(ulsch_harq->b, lenWithCrc(1, tbs << 3), crcType(1, tbs << 3));
      } else {
        // When the number of code blocks is 1 (C = 1) and ulsch_harq->processedSegments = 1, we can assume a good TB because of the
        // CRC check made by the LDPC for early termination, so, no need to perform CRC check twice for a single code block
        crc_valid = true;
      }
    }
#if T_TRACER
    {
      // capture Rx Payload via T-Tracer for both CRC valid and invalid cases
      NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
      int dmrs_port = get_dmrs_port(0, pusch_pdu->dmrs_ports);
      int number_dmrs_symbols = 0;
      for (int l = pusch_pdu->start_symbol_index; l < pusch_pdu->start_symbol_index + pusch_pdu->nr_of_symbols; l++)
        number_dmrs_symbols += ((pusch_pdu->ul_dmrs_symb_pos) >> l) & 0x01;

      log_ul_payload_rx_bits(ulsch->frame, ulsch->slot, frame_parms, pusch_pdu,
                             number_dmrs_symbols, dmrs_port,
                             (const uint8_t *)ulsch_harq->b,
                             pusch_pdu->pusch_data.tb_size);
    }
#endif

    nfapi_nr_crc_t *crc = &UL_INFO->crc_ind.crc_list[UL_INFO->crc_ind.number_crcs++];
    nfapi_nr_rx_data_pdu_t *pdu = &UL_INFO->rx_ind.pdu_list[UL_INFO->rx_ind.number_of_pdus++];
    if (crc_valid && !check_abort(&ulsch_harq->abort_decode) && !pusch->DTX) {
      LOG_D(NR_PHY,
            "[gNB %d] ULSCH %d: Setting ACK for SFN/SF %d.%d (rnti %x, pid %d, ndi %d, status %d, round %d, TBS %d, Max interation "
            "(all seg) %d)\n",
            gNB->Mod_id,
            ULSCH_id,
            ulsch->frame,
            ulsch->slot,
            ulsch->rnti,
            ulsch->harq_pid,
            pusch_pdu->pusch_data.new_data_indicator,
            ulsch->active,
            ulsch_harq->round,
            pusch_pdu->pusch_data.tb_size,
            ulsch->max_ldpc_iterations);
      nr_fill_indication(gNB, ulsch->frame, ulsch->slot, pusch, pusch_pdu, stats, ulsch->harq_process->b, 0, crc, pdu);
      LOG_D(PHY, "ULSCH received ok \n");
      ulsch->active = false;
      ulsch_harq->round = 0;
      ulsch->last_iteration_cnt = ulsch->max_ldpc_iterations - 1; // Setting to max_ldpc_iterations - 1 is sufficient given that this variable is only used for checking for failure
    } else {
      LOG_D(PHY,
            "[gNB %d] ULSCH: Setting NAK for SFN/SF %d/%d (pid %d, ndi %d, status %d, round %d, RV %d, prb_start %d, prb_size %d, "
            "TBS %d)\n",
            gNB->Mod_id,
            ulsch->frame,
            ulsch->slot,
            ulsch->harq_pid,
            pusch_pdu->pusch_data.new_data_indicator,
            ulsch->active,
            ulsch_harq->round,
            ulsch_harq->ulsch_pdu.pusch_data.rv_index,
            ulsch_harq->ulsch_pdu.rb_start,
            ulsch_harq->ulsch_pdu.rb_size,
            pusch_pdu->pusch_data.tb_size);
      nr_fill_indication(gNB, ulsch->frame, ulsch->slot, pusch, pusch_pdu, stats, NULL, 0, crc, pdu);
      gNBdumpScopeData(gNB, ulsch->slot, ulsch->frame, "ULSCH_NACK");
      LOG_D(PHY, "ULSCH %d in error\n",ULSCH_id);
      ulsch->last_iteration_cnt = ulsch->max_ldpc_iterations; // Setting to max_ldpc_iterations is sufficient given that this variable is only used for checking for failure
    }
  }

  return ret_nr_ulsch_decoding;
}

static void nr_fill_indication(const PHY_VARS_gNB *gNB,
                               int frame,
                               int slot_rx,
                               const NR_gNB_PUSCH *pusch,
                               const nfapi_nr_pusch_pdu_t *pusch_pdu,
                               NR_gNB_PHY_STATS_t *stats,
                               uint8_t *payload,
                               int dtx_flag,
                               nfapi_nr_crc_t *crc,
                               nfapi_nr_rx_data_pdu_t *pdu)
{
  crc->handle = pusch_pdu->handle;
  crc->rnti = pusch_pdu->rnti;
  crc->harq_id = pusch_pdu->pusch_data.harq_process_id;
  crc->tb_crc_status = payload == NULL; // 222.10.02/04: pass 0, fail 1
  crc->num_cb = pusch_pdu->pusch_data.num_cb;
  crc->ul_cqi = get_pusch_cqi(pusch, frame, slot_rx);
  crc->timing_advance = get_pusch_ta(gNB, stats, pusch, frame, slot_rx);
  // in terms of dBFS range -128 to 0 with 0.1 step
  crc->rssi = (dtx_flag == 0) ? get_pusch_rssi(pusch, pusch_pdu->param_v4.numSpatialStreamIndices) : 0;

  pdu->handle = pusch_pdu->handle;
  pdu->rnti = pusch_pdu->rnti;
  pdu->harq_id = pusch_pdu->pusch_data.harq_process_id;
  pdu->ul_cqi = crc->ul_cqi;
  pdu->timing_advance = crc->timing_advance;
  pdu->rssi = crc->rssi;
  pdu->pdu_length = payload != NULL ? pusch_pdu->pusch_data.tb_size : 0;
  pdu->pdu = payload;
}

// Function to fill UL RB mask to be used for N0 measurements
static void fill_ul_rb_mask(PHY_VARS_gNB *gNB,
                            uint32_t rb_mask_ul[14][MAX_BWP_SIZE],
                            const NR_gNB_PUCCH_job_t *pucch,
                            int n_pucch,
                            const NR_gNB_PUSCH_job_t *pusch,
                            int n_pusch,
                            const NR_gNB_SRS_job_t *srs,
                            int n_srs)
{
  for (int symbol = 0; symbol < 14; symbol++) {
    for (int prb = 0; prb < MAX_BWP_SIZE; prb++) {
      rb_mask_ul[symbol][prb] = gNB->ulprbbl[prb] > 0;
    }
  }

  for (int i = 0; i < n_pucch; ++i) {
    const nfapi_nr_pucch_pdu_t *pucch_pdu = &pucch[i].pucch_pdu;
    const int start = pucch_pdu->start_symbol_index;
    for (int symbol = start; symbol < start + pucch_pdu->nr_of_symbols; symbol++) {
      bool first_hop = symbol < start + (pucch_pdu->nr_of_symbols >> 1);
      int prb_first_second = first_hop || pucch_pdu->freq_hop_flag == 0 ? pucch_pdu->prb_start : pucch_pdu->second_hop_prb;
      for (int rb = 0; rb < pucch_pdu->prb_size; rb++) {
        int prb = rb + pucch_pdu->bwp_start + prb_first_second;
        rb_mask_ul[symbol][prb] = 1;
      }
    }
  }

  for (int i = 0; i < n_pusch; i++) {
    const nfapi_nr_pusch_pdu_t *pusch_pdu = &pusch[i].pusch_pdu;
    uint8_t symbol_start = pusch_pdu->start_symbol_index;
    uint8_t symbol_end = symbol_start + pusch_pdu->nr_of_symbols;
    for (int symbol = symbol_start; symbol < symbol_end; symbol++) {
      for (int rb = 0; rb < pusch_pdu->rb_size; rb++) {
        int prb = rb + pusch_pdu->rb_start + pusch_pdu->bwp_start;
        rb_mask_ul[symbol][prb] = 1;
      }
    }
  }

  for (int i = 0; i < n_srs; i++) {
    const nfapi_nr_srs_pdu_t *srs_pdu = &srs[i].srs_pdu;
    const uint8_t l0 = srs_pdu->time_start_position; // L2 already sends the absolute symbol index
    for (int symbol = 0; symbol < (1 << srs_pdu->num_symbols); symbol++) {
      for (int rb = srs_pdu->bwp_start; rb < (srs_pdu->bwp_start + srs_pdu->bwp_size); rb++) {
        rb_mask_ul[l0 + symbol][rb] = 1;
      }
    }
  }
}

static int fill_srs_reported_symbol(nfapi_nr_srs_reported_symbol_t *reported_symbol,
                                    const nfapi_nr_srs_pdu_t *srs_pdu,
                                    const int16_t *snr_per_rb,
                                    const int srs_est)
{
  reported_symbol->num_prgs = srs_pdu->beamforming.num_prgs;
  for (int prg_idx = 0; prg_idx < reported_symbol->num_prgs; prg_idx++) {
    uint8_t *snr = &reported_symbol->prg_list[prg_idx].rb_snr;
    if (srs_est < 0) {
      *snr = 0xFF;
    } else if (snr_per_rb[prg_idx] < -64) {
      *snr = 0;
    } else if (snr_per_rb[prg_idx] > 63) {
      *snr = 0xFE;
    } else {
      *snr = (snr_per_rb[prg_idx] + 64) << 1;
    }
  }

  return 0;
}

static int fill_srs_channel_matrix(nfapi_nr_srs_normalized_channel_iq_matrix_t *nr_srs_channel_iq_matrix,
                                   const nfapi_nr_srs_pdu_t *srs_pdu,
                                   const nr_srs_info_t *nr_srs_info,
                                   const NR_DL_FRAME_PARMS *frame_parms,
                                   const c16_t srs_estimated_channel_freq[][1 << srs_pdu->num_ant_ports][frame_parms->ofdm_symbol_size * (1 << srs_pdu->num_symbols)])
{
  const uint16_t num_gnb_antenna_elements = srs_pdu->srs_parameters_v4.num_ul_spatial_streams_ports;
  const uint16_t num_ue_srs_ports = srs_pdu->srs_parameters_v4.num_total_ue_antennas;
  const uint8_t normalized_iq_representation = srs_pdu->srs_parameters_v4.iq_representation;
  const uint16_t prg_size = srs_pdu->srs_parameters_v4.prg_size;
  const uint16_t num_prgs = srs_pdu->srs_parameters_v4.srs_bandwidth_size / srs_pdu->srs_parameters_v4.prg_size;
  const uint64_t first_subcarrier =
      (frame_parms->first_carrier_offset - (frame_parms->ofdm_symbol_size >> 1)) + srs_pdu->bwp_start * NR_NB_SC_PER_RB;
  const uint16_t step = prg_size * NR_NB_SC_PER_RB;

  nr_srs_channel_iq_matrix->normalized_iq_representation = normalized_iq_representation;
  nr_srs_channel_iq_matrix->num_gnb_antenna_elements = num_gnb_antenna_elements;
  nr_srs_channel_iq_matrix->num_ue_srs_ports = num_ue_srs_ports;
  nr_srs_channel_iq_matrix->prg_size = prg_size;
  nr_srs_channel_iq_matrix->num_prgs = num_prgs;

  uint8_t *channel_matrix = nr_srs_channel_iq_matrix->channel_matrix;
  c16_t *channel_matrix16 = (c16_t *)channel_matrix;
  c8_t *channel_matrix8 = (c8_t *)channel_matrix;

  for (int uI = 0; uI < num_ue_srs_ports; uI++) {
    for (int gI = 0; gI < num_gnb_antenna_elements; gI++) {

      // channel_matrix8 or channel_matrix16 will only be populated for the symbol 0
      uint16_t subcarrier_abs = first_subcarrier + nr_srs_info->k_0_p[uI][0];

      for (int pI = 0; pI < num_prgs; pI++) {
        const c16_t *srs_estimated_channel16 = srs_estimated_channel_freq[gI][uI] + subcarrier_abs;
        uint16_t index = uI * num_gnb_antenna_elements * num_prgs + gI * num_prgs + pI;

        if (normalized_iq_representation == 0) {
          channel_matrix8[index].r = (int8_t)(srs_estimated_channel16->r >> 8);
          channel_matrix8[index].i = (int8_t)(srs_estimated_channel16->i >> 8);
        } else {
          channel_matrix16[index].r = srs_estimated_channel16->r;
          channel_matrix16[index].i = srs_estimated_channel16->i;
        }
        // Subcarrier increment
        subcarrier_abs += step;
      }
    }
  }

  return 0;
}

static void copy_srs_info(const nfapi_nr_srs_pdu_t *srs_config_pdu, nr_srs_info_t *nr_srs_info)
{
  nr_srs_info->B_SRS = srs_config_pdu->bandwidth_index;
  nr_srs_info->C_SRS = srs_config_pdu->config_index;
  nr_srs_info->b_hop = srs_config_pdu->frequency_hopping;
  nr_srs_info->comb_size = srs_config_pdu->comb_size;
  nr_srs_info->K_TC_overbar = srs_config_pdu->comb_offset;
  nr_srs_info->n_SRS_cs = srs_config_pdu->cyclic_shift;
  nr_srs_info->n_ID_SRS = srs_config_pdu->sequence_id;
  // It adjusts the SRS allocation to align with the common resource block grid in multiples of four
  nr_srs_info->n_shift = srs_config_pdu->frequency_position;
  nr_srs_info->n_RRC = srs_config_pdu->frequency_shift;
  nr_srs_info->groupOrSequenceHopping = srs_config_pdu->group_or_sequence_hopping;
  nr_srs_info->l_offset = srs_config_pdu->time_start_position;
  nr_srs_info->T_SRS = srs_config_pdu->t_srs;
  nr_srs_info->T_offset = srs_config_pdu->t_offset;
  nr_srs_info->R = 1 << srs_config_pdu->num_repetitions;
  nr_srs_info->N_symb_SRS = 1 << srs_config_pdu->num_symbols; // Number of consecutive OFDM symbols
  nr_srs_info->n_srs_ports = 1 << srs_config_pdu->num_ant_ports; // Number of antenna port for transmission
  nr_srs_info->resource_type = srs_config_pdu->resource_type;
}

nr_srs_info_t nr_srs_rx_procedures(PHY_VARS_gNB *gNB,
                          int frame_rx,
                          int slot_rx,
                          uint8_t nb_antennas_rx,
                          uint8_t N_ap,
                          uint8_t N_symb_SRS,
                          uint16_t ofdm_symbol_size,
                          const NR_gNB_SRS_job_t *srs,
                          int *srs_est,
                          int8_t *snr,
                          c16_t srs_estimated_channel_freq[][N_ap][ofdm_symbol_size * N_symb_SRS],
                          int16_t *snr_per_rb,
                          uint16_t *timing_advance_offset,
                          int16_t *timing_advance_offset_nsec)
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const nfapi_nr_srs_pdu_t *srs_pdu = &srs->srs_pdu;

  c16_t srs_estimated_channel_time[nb_antennas_rx][N_ap][NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size]
        __attribute__((aligned(32)));
  c16_t srs_received_signal[nb_antennas_rx][ofdm_symbol_size * N_symb_SRS];
  c16_t srs_received_noise[nb_antennas_rx][ofdm_symbol_size * N_symb_SRS];
  c16_t srs_estimated_channel_time_shifted[nb_antennas_rx][N_ap][NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size];

  start_meas(&gNB->generate_srs_stats);

  nr_srs_info_t nr_srs_info = {0};
  copy_srs_info(srs_pdu, &nr_srs_info);

  // TODO permanently allocate?
  c16_t srs_generated_signal_tmp[N_ap][ofdm_symbol_size * MAX_NUM_NR_SRS_SYMBOLS];
  c16_t *srs_generated_signal[N_ap];
  for (int i = 0; i < N_ap; ++i)
    srs_generated_signal[i] = srs_generated_signal_tmp[i];
  if (true) {
    generate_srs_nr(frame_parms,
                    srs_generated_signal,
                    0,
                    srs_pdu->bwp_start,
                    &nr_srs_info,
                    AMP,
                    frame_rx,
                    slot_rx,
                    nb_antennas_rx);
  }

  stop_meas(&gNB->generate_srs_stats);
  const nfapi_v4_srs_parameters_t *p = &srs_pdu->srs_parameters_v4;
  const uint16_t ant_port_start = get_first_ant_idx(gNB->enable_analog_das,
                                                    frame_parms->nb_antennas_tx / gNB->common_vars.num_beams_period,
                                                    srs_pdu->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx,
                                                    p->num_ul_spatial_streams_ports > 0 ? p->Ul_spatial_stream_ports[0] : 0);
  c16_t **rxdataF = gNB->common_vars.rxdataF + ant_port_start;
  start_meas(&gNB->get_srs_signal_stats);
  *srs_est = nr_get_srs_signal(gNB, rxdataF, slot_rx, srs_pdu, &nr_srs_info, srs_received_signal, srs_received_noise);
  stop_meas(&gNB->get_srs_signal_stats);

  uint32_t signal_power_avg = 0;
  c16_t srs_ls_estimated_channel[nb_antennas_rx][N_ap][ofdm_symbol_size * N_symb_SRS];

  if (*srs_est >= 0) {
    start_meas(&gNB->srs_channel_estimation_stats);

    delay_t delay = {0};
    for (int ant_rx_ind = 0; ant_rx_ind < nb_antennas_rx; ant_rx_ind++) {
      for (int p_ind = 0; p_ind < N_ap; p_ind++) {
        delay_t delay_aux = {0};
        nr_srs_ls_channel_estimation(ant_rx_ind,
                                     p_ind,
                                     ofdm_symbol_size,
                                     N_symb_SRS,
                                     srs_pdu,
                                     &nr_srs_info,
                                     srs_generated_signal[p_ind],
                                     srs_received_signal[ant_rx_ind],
                                     srs_ls_estimated_channel[ant_rx_ind][p_ind],
                                     &delay_aux);
        if (delay_aux.delay_max_val > delay.delay_max_val)
          delay = delay_aux;
      }
    }

    for (int ant_rx_ind = 0; ant_rx_ind < nb_antennas_rx; ant_rx_ind++) {
      for (int p_ind = 0; p_ind < N_ap; p_ind++) {
        uint32_t signal_power = 0;
        nr_srs_channel_interpolation(p_ind,
                                     ofdm_symbol_size,
                                     frame_parms->first_carrier_offset,
                                     N_symb_SRS,
                                     srs_pdu,
                                     &nr_srs_info,
                                     srs_ls_estimated_channel[ant_rx_ind][p_ind],
                                     delay.est_delay,
                                     srs_received_noise[ant_rx_ind],
                                     srs_estimated_channel_freq[ant_rx_ind][p_ind],
                                     srs_estimated_channel_time[ant_rx_ind][p_ind],
                                     srs_estimated_channel_time_shifted[ant_rx_ind][p_ind],
                                     &signal_power,
                                     frame_parms->delay_table);

        signal_power_avg += signal_power;

        T(T_GNB_PHY_UL_FREQ_CHANNEL_ESTIMATE,
          T_INT(gNB->Mod_id),
          T_INT(srs_pdu->rnti),
          T_INT(frame_rx),
          T_INT(0),
          T_INT(ant_rx_ind),
          T_INT(p_ind),
          T_BUFFER(srs_estimated_channel_freq[ant_rx_ind][p_ind], N_symb_SRS * ofdm_symbol_size * sizeof(c16_t)));

        T(T_GNB_PHY_SRS_LS_ESTIMATE,
          T_INT(gNB->Mod_id),
          T_INT(srs_pdu->rnti),
          T_INT(frame_rx),
          T_INT(slot_rx),
          T_INT(ant_rx_ind),
          T_INT(p_ind),
          T_BUFFER(srs_ls_estimated_channel[ant_rx_ind][p_ind], N_symb_SRS * ofdm_symbol_size * sizeof(c16_t)));

        T(T_GNB_PHY_UL_TIME_CHANNEL_ESTIMATE,
          T_INT(gNB->Mod_id),
          T_INT(srs_pdu->rnti),
          T_INT(frame_rx),
          T_INT(0),
          T_INT(ant_rx_ind),
          T_INT(p_ind),
          T_BUFFER(srs_estimated_channel_time_shifted[ant_rx_ind][p_ind],
                   NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size * sizeof(c16_t)));
      }
    }

    signal_power_avg /= (nb_antennas_rx * N_ap);
    signal_power_avg = max(signal_power_avg, 1);

    uint32_t noise_power_avg = 0;
    int16_t noise_power_per_rb[srs_pdu->bwp_size];
    memset(noise_power_per_rb, 0, srs_pdu->bwp_size * sizeof(int16_t));
    for (int ant_rx_ind = 0; ant_rx_ind < nb_antennas_rx; ant_rx_ind++) {
      uint32_t noise_power_per_ant = 0;
      nr_srs_noise_power_estimation(ofdm_symbol_size,
                                    N_symb_SRS,
                                    srs_pdu,
                                    &nr_srs_info,
                                    signal_power_avg,
                                    srs_received_noise[ant_rx_ind],
                                    &noise_power_per_ant,
                                    noise_power_per_rb);
      noise_power_avg += noise_power_per_ant;
    }

    noise_power_avg /= nb_antennas_rx;
    *snr = dB_fixed(signal_power_avg) - dB_fixed(max(noise_power_avg, 1));

    const uint16_t m_SRS_b = get_m_srs(srs_pdu->config_index, srs_pdu->bandwidth_index);
    for (int rb = 0; rb < m_SRS_b; rb++) {
      snr_per_rb[rb] = dB_fixed(signal_power_avg) - dB_fixed(max(noise_power_per_rb[rb] / nb_antennas_rx, 1));
    }
    stop_meas(&gNB->srs_channel_estimation_stats);

    start_meas(&gNB->srs_timing_advance_stats);
    for (int ant_rx_ind = 0; ant_rx_ind < nb_antennas_rx; ant_rx_ind++) {
      nr_est_srs_timing_advance_offset(ofdm_symbol_size,
                                       srs_estimated_channel_time[ant_rx_ind],
                                       ant_rx_ind,
                                       N_ap,
                                       frame_parms->samples_per_frame,
                                       timing_advance_offset,
                                       &timing_advance_offset_nsec[ant_rx_ind]);
    }
    stop_meas(&gNB->srs_timing_advance_stats);

    T(T_GNB_PHY_UL_SNR_ESTIMATE,
      T_INT(0),
      T_INT(srs_pdu->rnti),
      T_INT(frame_rx),
      T_INT(0),
      T_INT(0),
      T_BUFFER(snr_per_rb, srs_pdu->bwp_size * sizeof(int16_t)));

    T(T_GNB_PHY_UL_SRS_TOA_NS,
      T_INT(gNB->Mod_id),
      T_INT(srs_pdu->rnti),
      T_INT(frame_rx),
      T_INT(slot_rx),
      T_BUFFER(timing_advance_offset_nsec, nb_antennas_rx * sizeof(int16_t)));
  }
  return nr_srs_info;
}

static void handle_srs(fsn_t now,
                       PHY_VARS_gNB *gNB,
                       const NR_gNB_SRS_job_t *srs,
                       nfapi_nr_srs_indication_pdu_t *srs_indication,
                       nfapi_nr_srs_toa_vendor_ext_indication_t *srs_toa_v_ext)
{
  const NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const nfapi_nr_srs_pdu_t *srs_pdu = &srs->srs_pdu;
  const uint8_t nb_antennas_rx = srs_pdu->srs_parameters_v4.num_ul_spatial_streams_ports;
  const uint16_t ofdm_symbol_size = frame_parms->ofdm_symbol_size;

  uint8_t N_symb_SRS = 1 << srs_pdu->num_symbols;
  uint8_t N_ap = 1 << srs_pdu->num_ant_ports;
  int16_t snr_per_rb[srs_pdu->bwp_size];
  uint16_t timing_advance_offset;
  int16_t timing_advance_offset_nsec[nb_antennas_rx];
  int srs_est;

  c16_t srs_estimated_channel_freq[nb_antennas_rx][N_ap][ofdm_symbol_size * N_symb_SRS] __attribute__((aligned(32)));

  int8_t snr;
  nr_srs_info_t srs_info = nr_srs_rx_procedures(gNB,
                                                now.f,
                                                now.s,
                                                nb_antennas_rx,
                                                N_ap,
                                                N_symb_SRS,
                                                ofdm_symbol_size,
                                                srs,
                                                &srs_est,
                                                &snr,
                                                srs_estimated_channel_freq,
                                                snr_per_rb,
                                                &timing_advance_offset,
                                                timing_advance_offset_nsec);

  // Measurement outputs are valid on raw success (including 0), before SNR thresholding.
  const bool estimate_valid = srs_est >= 0;
  if (estimate_valid && (snr * 10) < gNB->srs_thres) {
    srs_est = -1;
  }

  srs_indication->handle = srs_pdu->handle;
  srs_indication->rnti = srs_pdu->rnti;
  srs_indication->timing_advance_offset = srs_est >= 0 ? timing_advance_offset : 0xFFFF;
  // TODO: currently we fill timing_advance_offset_nsec for antenna 0. Need to extend it for other antennas
  srs_indication->timing_advance_offset_nsec = srs_est >= 0 ? timing_advance_offset_nsec[0] : 0x8000;
  srs_indication->srs_usage = NFAPI_NR_SRS_BEAMMANAGEMENT;
  switch (srs_pdu->srs_parameters_v4.usage) {
    case 0:
      srs_indication->report_type = 0;
      srs_indication->report_tlv.tag = 0;
      srs_indication->report_tlv.length = 0;
      LOG_W(NR_PHY, "SRS report was not requested by MAC\n");
      return;
    case 1 << NFAPI_NR_SRS_BEAMMANAGEMENT:
      srs_indication->srs_usage = NFAPI_NR_SRS_BEAMMANAGEMENT;
      break;
    case 1 << NFAPI_NR_SRS_CODEBOOK:
      srs_indication->srs_usage = NFAPI_NR_SRS_CODEBOOK;
      break;
    case 1 << NFAPI_NR_SRS_NONCODEBOOK:
      srs_indication->srs_usage = NFAPI_NR_SRS_NONCODEBOOK;
      break;
    case 1 << NFAPI_NR_SRS_ANTENNASWITCH:
      srs_indication->srs_usage = NFAPI_NR_SRS_ANTENNASWITCH;
      break;
    case 1 << NFAPI_NR_SRS_POSITIONING:
      srs_indication->srs_usage = NFAPI_NR_SRS_POSITIONING;
      break;
    default:
      LOG_E(NR_PHY, "Invalid srs_pdu->srs_parameters_v4.usage %i\n", srs_pdu->srs_parameters_v4.usage);
  }
  srs_indication->report_type = srs_pdu->srs_parameters_v4.report_type[0];

  nfapi_srs_report_tlv_t *report_tlv = &srs_indication->report_tlv;
  report_tlv->tag = 0;
  report_tlv->length = 0;

  if (!estimate_valid) {
    srs_indication->report_type = 0;
    if (srs_indication->srs_usage == NFAPI_NR_SRS_POSITIONING)
      srs_toa_v_ext->num_ta = 0;
    return;
  }

  start_meas(&gNB->srs_report_tlv_stats);
  switch (srs_indication->srs_usage) {
    case NFAPI_NR_SRS_BEAMMANAGEMENT: {
      start_meas(&gNB->srs_beam_report_stats);
      nfapi_nr_srs_beamforming_report_t nr_srs_bf_report;
      nr_srs_bf_report.prg_size = srs_pdu->beamforming.prg_size;
      nr_srs_bf_report.num_symbols = N_symb_SRS;
      nr_srs_bf_report.wide_band_snr = srs_est >= 0 ? (snr + 64) << 1 : 0xFF; // 0xFF will be set if this field is invalid
      nr_srs_bf_report.num_reported_symbols = N_symb_SRS;
      AssertFatal(nr_srs_bf_report.num_reported_symbols == 1,
                  "nr_srs_bf_report.num_reported_symbols %i not handled yet!\n",
                  nr_srs_bf_report.num_reported_symbols);
      fill_srs_reported_symbol(&nr_srs_bf_report.reported_symbol_list[0], srs_pdu, snr_per_rb, srs_est);

      report_tlv->length = pack_nr_srs_beamforming_report(&nr_srs_bf_report, report_tlv->value, sizeof(report_tlv->value));
      stop_meas(&gNB->srs_beam_report_stats);
      break;
    }

    case NFAPI_NR_SRS_CODEBOOK: {
      start_meas(&gNB->srs_iq_matrix_stats);
      nfapi_nr_srs_normalized_channel_iq_matrix_t nr_srs_channel_iq_matrix;
      fill_srs_channel_matrix(&nr_srs_channel_iq_matrix,
                              srs_pdu,
                              &srs_info,
                              frame_parms,
                              srs_estimated_channel_freq);

      report_tlv->length =
          pack_nr_srs_normalized_channel_iq_matrix(&nr_srs_channel_iq_matrix, report_tlv->value, sizeof(report_tlv->value));
      stop_meas(&gNB->srs_iq_matrix_stats);
      break;
    }

    case NFAPI_NR_SRS_NONCODEBOOK:
    case NFAPI_NR_SRS_ANTENNASWITCH:
      LOG_W(NR_PHY, "PHY procedures for this SRS usage are not implemented yet!\n");
      break;

    case NFAPI_NR_SRS_POSITIONING: {
      nfapi_nr_srs_toa_vendor_ext_indication_t *srs_toa_vendor_ext_ind = srs_toa_v_ext;
      srs_toa_vendor_ext_ind->sfn = now.f;
      srs_toa_vendor_ext_ind->slot = now.s;
      srs_toa_vendor_ext_ind->rnti = srs_pdu->rnti;
      srs_toa_vendor_ext_ind->num_ta = nb_antennas_rx;
      for (int ta_idx = 0; ta_idx < nb_antennas_rx; ta_idx++) {
        srs_toa_vendor_ext_ind->ta_offset_nsec[ta_idx] = timing_advance_offset_nsec[ta_idx];
      }
      break;
    }

    default:
      AssertFatal(1 == 0, "Invalid SRS usage\n");
  }
  stop_meas(&gNB->srs_report_tlv_stats);
}

static void handle_pucch(PHY_VARS_gNB *gNB, c16_t **rxdataF, const NR_gNB_PUCCH_job_t *pucch, nfapi_nr_uci_t *uci)
{
  const nfapi_nr_pucch_pdu_t *pucch_pdu = &pucch->pucch_pdu;

  switch (pucch_pdu->format_type) {
    case 0:
      uci->pdu_type = NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE;
      uci->pdu_size = sizeof(nfapi_nr_uci_pucch_pdu_format_0_1_t);
      nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_pdu_format0 = &uci->pucch_pdu_format_0_1;
      uci_pdu_format0->handle = 0;
      start_meas(&gNB->pucch01_proc_rx);
      nr_decode_pucch0(gNB, rxdataF, pucch->frame, pucch->slot, uci_pdu_format0, pucch_pdu);
      stop_meas(&gNB->pucch01_proc_rx);
      break;
    case 2:
      uci->pdu_type = NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE;
      uci->pdu_size = sizeof(nfapi_nr_uci_pucch_pdu_format_2_3_4_t);
      nfapi_nr_uci_pucch_pdu_format_2_3_4_t *uci_pdu_format2 = &uci->pucch_pdu_format_2_3_4;
      uci_pdu_format2->csi_part2.csi_part2_crc = 0;
      uci_pdu_format2->csi_part2.csi_part2_bit_len = 0;
      uci_pdu_format2->csi_part2.csi_part2_payload = NULL;
      start_meas(&gNB->pucch23_proc_rx);
      nr_decode_pucch2(gNB, rxdataF, pucch->frame, pucch->slot, uci_pdu_format2, pucch_pdu);
      stop_meas(&gNB->pucch23_proc_rx);
      break;
    default:
      AssertFatal(1 == 0, "Only PUCCH formats 0 and 2 are currently supported\n");
  }
}

#ifdef DEBUG_RXDATA
/* writes the RX samples of the slot of the first 20 PUSCH, then exits. Per PUSCH, as int32 words: rnti, rb_size,
   rb_start, nr_of_symbols, start_symbol_index, mcs_index, rv_index, harq_process_id as 8 int16, the samples of the
   slot, 1000 zeros */
static void dump_pusch_rx_data(PHY_VARS_gNB *gNB, const nfapi_nr_pusch_pdu_t *pdu, uint8_t slot, int ue_idx)
{
  static FILE *f = NULL;
  static int nb_dumped = 0;
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  RU_t *ru = gNB->RU_list[0];
  int slot_offset = get_samples_slot_timestamp(frame_parms, slot);
  slot_offset -= ru->N_TA_offset;
  if (!f) {
    char fname[64];
    snprintf(fname, sizeof(fname), "rxdata_buff_ue%d.raw", ue_idx);
    f = fopen(fname, "w");
    AssertFatal(f, "cannot open %s\n", fname);
  }
  const int16_t header[8] = {pdu->rnti,
                             pdu->rb_size,
                             pdu->rb_start,
                             pdu->nr_of_symbols,
                             pdu->start_symbol_index,
                             pdu->mcs_index,
                             pdu->pusch_data.rv_index,
                             pdu->pusch_data.harq_process_id};
  static const int32_t zeros[1000] = {0};
  fwrite(header, sizeof(header), 1, f);
  fwrite(&ru->common.rxdata[0][slot_offset], sizeof(int32_t), get_samples_per_slot(slot, frame_parms), f);
  fwrite(zeros, sizeof(zeros), 1, f);
  if (++nb_dumped == 20) {
    fclose(f);
    exit(-1);
  }
}
#endif

static void handle_pusch_rx_group_trigger(PHY_VARS_gNB *gNB,
                                          NR_gNB_PUSCH **pusch_vars_group,
                                          const nfapi_nr_pusch_pdu_t **ulsch_pdu_group,
                                          uint32_t **ret_unav_res_group,
                                          int group_size,
                                          uint32_t frame,
                                          uint8_t slot)
{
#ifdef DEBUG_RXDATA
  // Dumping only ue index 0 for now
  int ue_idx = 0;
  const nfapi_nr_pusch_pdu_t *pdu = ulsch_pdu_group[ue_idx];
  dump_pusch_rx_data(gNB, pdu, slot, ue_idx);
#endif

  start_meas(&gNB->rx_pusch_stats);
  nr_rx_pusch_group_tp(gNB, pusch_vars_group, ulsch_pdu_group, ret_unav_res_group, group_size, frame, slot);
  stop_meas(&gNB->rx_pusch_stats);
}

static bool pusch_signal_detected(PHY_VARS_gNB *gNB, NR_gNB_PUSCH *pusch_vars, NR_gNB_ULSCH_t *ulsch)
{
  const bool detected =
      dB_fixed_x10(pusch_vars->ulsch_power_tot) >= dB_fixed_x10(pusch_vars->ulsch_noise_power_tot) + gNB->pusch_thres;

  LOG_D(NR_PHY,
        "PUSCH %s in %d.%d (%d,%d,%d)\n",
        detected ? "detected" : "not detected",
        ulsch->frame,
        ulsch->slot,
        dB_fixed_x10(pusch_vars->ulsch_power_tot),
        dB_fixed_x10(pusch_vars->ulsch_noise_power_tot),
        gNB->pusch_thres);

  if (detected) {
    pusch_vars->DTX = 0;
  } else {
    NR_gNB_PHY_STATS_t *stats = get_phy_stats(gNB, ulsch->rnti);
    pusch_vars->ulsch_power_tot = pusch_vars->ulsch_noise_power_tot;
    pusch_vars->DTX = 1;
    if (stats)
      stats->ulsch_stats.DTX++;
  }

  return detected;
}

static bool drop_old_pucch(const void *data, void *user)
{
  const NR_gNB_PUCCH_job_t *pucch = data;
  const fsn_t *now = user;
  const fsn_t t = {pucch->frame, pucch->slot, now->mu};
  bool drop = fsn_in_the_past(t, *now);
  if (drop)
    LOG_E(NR_PHY, "%4d.%2d PUCCH job for UE %04x is in the past (%4d.%2d)\n", now->f, now->s, pucch->pucch_pdu.rnti, t.f, t.s);
  return drop;
}

static bool get_current_pucch(const void *data, void *user)
{
  const NR_gNB_PUCCH_job_t *pucch = data;
  const fsn_t *now = user;
  const fsn_t t = {pucch->frame, pucch->slot, now->mu};
  return fsn_equal(t, *now);
}

static bool drop_old_pusch(const void *data, void *user)
{
  const NR_gNB_PUSCH_job_t *pusch = data;
  const fsn_t *now = user;
  const fsn_t t = {pusch->frame, pusch->slot, now->mu};
  bool drop = fsn_in_the_past(t, *now);
  if (drop)
    LOG_E(NR_PHY, "%4d.%2d PUSCH job for UE %04x is in the past (%4d.%2d)\n", now->f, now->s, pusch->pusch_pdu.rnti, t.f, t.s);
  return drop;
}

static bool get_current_pusch(const void *data, void *user)
{
  const NR_gNB_PUSCH_job_t *pusch = data;
  const fsn_t *now = user;
  const fsn_t t = {pusch->frame, pusch->slot, now->mu};
  return fsn_equal(t, *now);
}

static bool drop_old_srs(const void *data, void *user)
{
  const NR_gNB_SRS_job_t *srs = data;
  const fsn_t *now = user;
  const fsn_t t = {srs->frame, srs->slot, now->mu};
  bool drop = fsn_in_the_past(t, *now);
  if (drop)
    LOG_E(NR_PHY, "%4d.%2d SRS job for UE %04x is in the past (%4d.%2d)\n", now->f, now->s, srs->srs_pdu.rnti, t.f, t.s);
  return drop;
}

static bool get_current_srs(const void *data, void *user)
{
  const NR_gNB_SRS_job_t *srs = data;
  const fsn_t *now = user;
  const fsn_t t = {srs->frame, srs->slot, now->mu};
  return fsn_equal(t, *now);
}

static int find_nr_ulsch_idx(PHY_VARS_gNB *gNB, uint16_t rnti, int pid)
{
  AssertFatal(gNB != NULL, "gNB is null\n");

  int16_t first_free_index = -1;
  for (int i = 0; i < gNB->max_nb_pusch; i++) {
    const NR_gNB_ULSCH_t *ulsch = &gNB->ulsch[i];
    AssertFatal(ulsch, "gNB->ulsch[%d] is null\n", i);
    if (!ulsch->active) {
      if (first_free_index == -1)
        first_free_index = i;
    } else {
      // if there is already an active ULSCH for this RNTI and HARQ_PID
      if ((ulsch->harq_pid == pid) && (ulsch->rnti == rnti))
        return i;
    }
  }
  return first_free_index;
}
static int handle_pusch_job_trigger(PHY_VARS_gNB *gNB, const NR_gNB_PUSCH_job_t *job)
{
  const nfapi_nr_pusch_pdu_t *pdu = &job->pusch_pdu;
  int pid = pdu->pusch_data.harq_process_id;
  int ULSCH_id = find_nr_ulsch_idx(gNB, pdu->rnti, pid);
  if (ULSCH_id < 0) {
    LOG_E(NR_PHY, "%4d.%2d cannot handle PUSCH job of RNTI %04x: no space\n", job->frame, job->slot, pdu->rnti);
    return -1;
  }
  /* (re-)initialize this PUSCH context from job data */
  NR_gNB_ULSCH_t *ulsch = &gNB->ulsch[ULSCH_id];
  ulsch->active = true;
  ulsch->frame = job->frame;
  ulsch->slot = job->slot;
  ulsch->rnti = pdu->rnti;
  ulsch->harq_pid = pid;
  ulsch->harq_process->ulsch_pdu = job->pusch_pdu;
  if (pdu->pusch_data.new_data_indicator) {
    ulsch->harq_process->harq_to_be_cleared = true;
    ulsch->harq_process->round = 0;
  } else {
    ulsch->harq_process->round++;
  }
  return ULSCH_id;
}

static NR_MU_MIMO_job_groups_t group_pusch_jobs(PHY_VARS_gNB *gNB, NR_gNB_PUSCH_job_t *pusch, int n_pusch_jobs)
{
  NR_MU_MIMO_job_groups_t groups = {0};
  for (int i = 0; i < n_pusch_jobs; ++i) {
    int ULSCH_id = handle_pusch_job_trigger(gNB, &pusch[i]);
    if (ULSCH_id < 0) {
      continue;
    }
    int g = pusch[i].mu_group_idx;
    AssertFatal(g >= 0, "Ungrouped ULSCH_id %d\n", ULSCH_id);
    if (groups.size[g] == 0) {
      groups.active_groups[groups.n_active_groups++] = g;
    }
    groups.jobs[g][groups.size[g]++] = ULSCH_id;
  }
  return groups;
}

int phy_procedures_gNB_uespec_RX(PHY_VARS_gNB *gNB, int frame_rx, int slot_rx, NR_UL_IND_t *UL_INFO)
{
  /* those variables to log T_GNB_PHY_PUCCH_PUSCH_IQ only when we try to decode */
  int pusch_DTX = 0;

  const NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  const uint16_t ofdm_symbol_size = frame_parms->ofdm_symbol_size;

  fsn_t now = {frame_rx, slot_rx, frame_parms->numerology_index};

  spsc_q_drop_while(&gNB->pucch_queue, drop_old_pucch, &now);
  NR_gNB_PUCCH_job_t pucch[MAX_NUM_NR_UCI_PDUS];
  int n_pucch = spsc_q_get_while(&gNB->pucch_queue, get_current_pucch, &now, pucch, sizeof(*pucch), MAX_NUM_NR_UCI_PDUS);

  spsc_q_drop_while(&gNB->pusch_queue, drop_old_pusch, &now);
  NR_gNB_PUSCH_job_t pusch[MAX_UL_PDUS_PER_SLOT];
  int n_pusch_jobs = spsc_q_get_while(&gNB->pusch_queue, get_current_pusch, &now, pusch, sizeof(*pusch), MAX_UL_PDUS_PER_SLOT);

  spsc_q_drop_while(&gNB->srs_queue, drop_old_srs, &now);
  NR_gNB_SRS_job_t srs[MAX_NUM_NR_SRS_PDUS];
  int n_srs = spsc_q_get_while(&gNB->srs_queue, get_current_srs, &now, srs, sizeof(*srs), MAX_NUM_NR_SRS_PDUS);

  LOG_D(PHY,"phy_procedures_gNB_uespec_RX frame %d, slot %d\n",frame_rx,slot_rx);
  {
    // Mask of occupied RBs, per symbol and PRB
    uint32_t rb_mask_ul[14][MAX_BWP_SIZE] = {0};
    fill_ul_rb_mask(gNB, rb_mask_ul, pucch, n_pucch, pusch, n_pusch_jobs, srs, n_srs);

    int first_symb = 0, num_symb = 0;
    if (frame_parms->frame_type == TDD) {
      const nfapi_nr_max_num_of_symbol_per_slot_t *slot_conf = gNB->gNB_config.tdd_table.max_tdd_periodicity_list[slot_rx].max_num_of_symbol_per_slot_list;
      for (int symbol_count = 0; symbol_count < frame_parms->symbols_per_slot; symbol_count++) {
        if (slot_conf[symbol_count].slot_config.value == 1) {
          if (num_symb == 0)
            first_symb = symbol_count;
          num_symb++;
        }
      }
    } else {
      num_symb = frame_parms->symbols_per_slot;
    }
    gNB_I0_measurements(gNB, slot_rx, first_symb, num_symb, rb_mask_ul);
  }

  int slot_type = nr_slot_select(&gNB->gNB_config, frame_rx, slot_rx);
  START_MEAS_FULL_SLOT(&gNB->phy_proc_rx, slot_type, NR_UPLINK_SLOT);
  UL_INFO->uci_ind.header.transmit_timestamp = 0;
  UL_INFO->uci_ind.uci_list = UL_INFO->uci_pdu_list;
  UL_INFO->uci_ind.sfn = frame_rx;
  UL_INFO->uci_ind.slot = slot_rx;
  UL_INFO->uci_ind.num_ucis = n_pucch;
  nfapi_nr_uci_t *uci = UL_INFO->uci_ind.uci_list;
  for (int i = 0; i < n_pucch; ++i) {
    const nfapi_nr_spatial_stream_index_t *p = &pucch[i].pucch_pdu.param_v4;
    const uint16_t ant_port = get_first_ant_idx(gNB->enable_analog_das,
                                                frame_parms->nb_antennas_tx / gNB->common_vars.num_beams_period,
                                                pucch[i].pucch_pdu.beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx,
                                                p->numSpatialStreamIndices > 0 ? p->spatialStreamIndices[0] : 0);
    c16_t **rxdataF = gNB->common_vars.rxdataF + ant_port;
    handle_pucch(gNB, rxdataF, &pucch[i], uci++);
  }


  UL_INFO->crc_ind.header.transmit_timestamp = 0;
  UL_INFO->crc_ind.sfn = frame_rx;
  UL_INFO->crc_ind.slot = slot_rx;
  UL_INFO->crc_ind.crc_list = UL_INFO->crc_pdu_list;
  UL_INFO->crc_ind.number_crcs = 0;
  UL_INFO->rx_ind.header.transmit_timestamp = 0;
  UL_INFO->rx_ind.sfn = frame_rx;
  UL_INFO->rx_ind.slot = slot_rx;
  UL_INFO->rx_ind.pdu_list = UL_INFO->rx_pdu_list;
  UL_INFO->rx_ind.number_of_pdus = 0;
  int ulsch_idx_to_decode[MAX_UL_PDUS_PER_SLOT];
  int num_pusch = 0;

  // Group the jobs using group index
  NR_MU_MIMO_job_groups_t pusch_groups = group_pusch_jobs(gNB, pusch, n_pusch_jobs);

  for (int i = 0; i < pusch_groups.n_active_groups; i++) {
    int g = pusch_groups.active_groups[i];
    int gsz = pusch_groups.size[g];
    AssertFatal(gsz <= NR_MAX_NB_LAYERS, "Cannot handle group size > %d\n", NR_MAX_NB_LAYERS);
    NR_gNB_PUSCH *pusch_vars_group[gsz];
    const nfapi_nr_pusch_pdu_t *ulsch_pdu_group[gsz];
    uint32_t *unav_res_group[gsz];
    // store pusch vars and ulsch pdu for group based processing
    for (int u = 0; u < gsz; u++) {
      int ulsch_id = pusch_groups.jobs[g][u];
      pusch_vars_group[u] = &gNB->pusch_vars[ulsch_id];
      ulsch_pdu_group[u] = &gNB->ulsch[ulsch_id].harq_process->ulsch_pdu;
      unav_res_group[u] = &gNB->ulsch[ulsch_id].unav_res;
    }

    handle_pusch_rx_group_trigger(gNB, pusch_vars_group, ulsch_pdu_group, unav_res_group, gsz, frame_rx, slot_rx);

    for (int u = 0; u < gsz; u++) {
      int ULSCH_id = pusch_groups.jobs[g][u];
      NR_gNB_PUSCH *pusch_vars = &gNB->pusch_vars[ULSCH_id];
      NR_gNB_ULSCH_t *ulsch = &gNB->ulsch[ULSCH_id];

      if (pusch_signal_detected(gNB, pusch_vars, ulsch) || get_softmodem_params()->phy_test) {
        ulsch_idx_to_decode[num_pusch++] = ULSCH_id;
      } else {
        AssertFatal(ulsch->harq_process != NULL, "harq_pid %d is not allocated\n", ulsch->harq_pid);
        const nfapi_nr_pusch_pdu_t *pdu = &ulsch->harq_process->ulsch_pdu;
        NR_gNB_PHY_STATS_t *stats = get_phy_stats(gNB, ulsch->rnti);
        nfapi_nr_crc_t *crc = &UL_INFO->crc_ind.crc_list[UL_INFO->crc_ind.number_crcs++];
        nfapi_nr_rx_data_pdu_t *rx = &UL_INFO->rx_ind.pdu_list[UL_INFO->rx_ind.number_of_pdus++];
        nr_fill_indication(gNB, ulsch->frame, ulsch->slot, pusch_vars, pdu, stats, NULL, 1, crc, rx);
        pusch_DTX++;
        gNBdumpScopeData(gNB, ulsch->slot, ulsch->frame, "ULSCH_DTX");
      }
    }
  }

  if (num_pusch > 0) {
    START_MEAS_FULL_SLOT(&gNB->ulsch_decoding_stats, slot_type, NR_UPLINK_SLOT);
    int ret_nr_ulsch_procedures = nr_ulsch_procedures(gNB, frame_rx, slot_rx, ulsch_idx_to_decode, num_pusch, UL_INFO);
    if (ret_nr_ulsch_procedures != 0)
      LOG_E(NR_PHY, "Error in nr_ulsch_procedures, returned %d\n", ret_nr_ulsch_procedures);
    STOP_MEAS_FULL_SLOT(&gNB->ulsch_decoding_stats, slot_type, NR_UPLINK_SLOT);
  }

  UL_INFO->srs_ind.header.transmit_timestamp = 0;
  UL_INFO->srs_ind.sfn = frame_rx;
  UL_INFO->srs_ind.slot = slot_rx;
  UL_INFO->srs_ind.pdu_list = UL_INFO->srs_pdu_list;
  UL_INFO->srs_ind.number_of_pdus = n_srs;
  UL_INFO->srs_ind.control_length = 0;
  UL_INFO->srs_toa_vendor_ext_ind.header.transmit_timestamp = 0;
  UL_INFO->srs_toa_vendor_ext_ind.sfn = frame_rx;
  UL_INFO->srs_toa_vendor_ext_ind.slot = slot_rx;
  UL_INFO->srs_toa_vendor_ext_ind.rnti = 0;
  UL_INFO->srs_toa_vendor_ext_ind.num_ta = 0;
  for (int i = 0; i < n_srs; ++i) {
    start_meas(&gNB->rx_srs_stats);
    handle_srs(now, gNB, &srs[i], &UL_INFO->srs_ind.pdu_list[i], &UL_INFO->srs_toa_vendor_ext_ind);
    stop_meas(&gNB->rx_srs_stats);
  }

  STOP_MEAS_FULL_SLOT(&gNB->phy_proc_rx, slot_type, NR_UPLINK_SLOT);

  if (n_pucch > 0 || num_pusch > 0) {
    UNUSED(ofdm_symbol_size); // only used if T activated
    T(T_GNB_PHY_PUCCH_PUSCH_IQ,
      T_INT(frame_rx),
      T_INT(slot_rx),
      T_BUFFER(&gNB->common_vars.rxdataF[0][0], frame_parms->symbols_per_slot * ofdm_symbol_size * 4));
  }

#ifdef E3_AGENT
  /* Hand this slot's IQ to the L1-KPM SM (RF=2) via /e3_ran_buffers. rx_func()
   * only reaches here on UL and mixed slots, so the subscription is the only
   * thing left to check. */
  if (l1_kpm_sm_has_subscribers()) {
    e3_ran_buffers_push_rxdataF(gNB, frame_rx, slot_rx);
  }
#endif // E3_AGENT

  return pusch_DTX;
}

void nr_save_ul_tti_req(PHY_VARS_gNB *gNB, nfapi_nr_ul_tti_request_t *UL_tti_req)
{
  DevAssert(gNB != NULL);
  DevAssert(UL_tti_req != NULL);

  int frame = UL_tti_req->SFN;
  int slot = UL_tti_req->Slot;

  int16_t pdu_group_idx[MAX_UL_PDUS_PER_SLOT] = {[0 ... MAX_UL_PDUS_PER_SLOT - 1] = -1};
  uint8_t pdu_group_size[MAX_UL_PDUS_PER_SLOT] = {0};
  for (int g = 0; g < UL_tti_req->n_group; g++) {
    const nfapi_nr_ul_tti_request_number_of_groups_t *grp = &UL_tti_req->groups_list[g];
    for (int u = 0; u < grp->n_ue; u++) {
      int pdu_idx = grp->ue_list[u].pdu_idx;
      if (pdu_idx >= 0 && pdu_idx < UL_tti_req->n_pdus) {
        pdu_group_idx[pdu_idx] = g;
        pdu_group_size[pdu_idx] = grp->n_ue;
      }
    }
  }

  for (int i = 0; i < UL_tti_req->n_pdus; i++) {
    int type = UL_tti_req->pdus_list[i].pdu_type;
    LOG_D(NR_PHY,
          "frame %d, slot %d got %s for %d.%d\n",
          frame,
          slot,
          txt_nfapi_nr_ul_config_pdu_type[type],
          UL_tti_req->SFN,
          UL_tti_req->Slot);
    switch (type) {
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        nr_fill_ulsch(gNB,
                      UL_tti_req->SFN,
                      UL_tti_req->Slot,
                      &UL_tti_req->pdus_list[i].pusch_pdu,
                      pdu_group_idx[i],
                      pdu_group_size[i]);
        break;
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
        nr_fill_pucch(gNB, UL_tti_req->SFN, UL_tti_req->Slot, &UL_tti_req->pdus_list[i].pucch_pdu);
        break;
      case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE: {
        nfapi_nr_prach_pdu_t *prach_pdu = &UL_tti_req->pdus_list[i].prach_pdu;
        nr_schedule_rx_prach(gNB, UL_tti_req->SFN, UL_tti_req->Slot, prach_pdu);
      } break;
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
        nr_fill_srs(gNB, UL_tti_req->SFN, UL_tti_req->Slot, &UL_tti_req->pdus_list[i].srs_pdu);
        break;
    }
  }
}
