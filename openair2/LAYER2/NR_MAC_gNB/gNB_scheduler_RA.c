/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief     primitives used for random access
 */

#include "common/platform_types.h"

/* MAC */
#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "openair2/COMMON/mac_messages_types.h"

/* Utils */
#include "common/utils/LOG/log.h"
#include "common/utils/bits.h"
#include "common/utils/alg/find.h"
#include "UTIL/OPT/opt.h"

/* rlc */
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"

#include <executables/softmodem-common.h>

typedef struct {
  bool checked; // Validator covers this RA case.
  bool valid; // RAPID accepted, or case unchecked.
  uint16_t num_valid_rapids; // Number of accepted consecutive RAPIDs.
  const char *reason; // Static unchecked-case reason.
} nr_rapid_check_t;

static nr_rapid_check_t check_rapid_for_prach_occasion(nr_cell_sched_t *cell, uint16_t preamble_index)
{
  nr_rapid_check_t check = {
      .checked = false,
      .valid = true,
      .num_valid_rapids = 0,
      .reason = "unchecked",
  };

  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_BWP_UplinkCommon_t *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;

  if (initialUplinkBWP->ext1 && initialUplinkBWP->ext1->msgA_ConfigCommon_r16) {
    check.reason = "msgA_not_checked";
    return check;
  }

  NR_RACH_ConfigCommon_t *rach_ConfigCommon = initialUplinkBWP->rach_ConfigCommon->choice.setup;
  ssb_ro_preambles_t ssb_ro = get_ssb_ro_preambles_4step(rach_ConfigCommon->ssb_perRACH_OccasionAndCB_PreamblesPerSSB);

  /*
   * First implementation validates the non-shared RO case. For shared ROs,
   * preserve existing behavior until SSB/RO-specific RAPID partitioning is added.
   */
  if (ssb_ro.ssb_per_ro > 1) {
    check.reason = "shared_ro_not_checked";
    return check;
  }

  if (ssb_ro.preambles_per_ssb <= 0) {
    check.reason = "invalid_cb_preambles";
    return check;
  }

  const uint16_t total_ra_preambles =
      rach_ConfigCommon->totalNumberOfRA_Preambles ? *rach_ConfigCommon->totalNumberOfRA_Preambles : MAX_NUM_NR_PRACH_PREAMBLES;
  const uint16_t num_valid_rapids = min(ssb_ro.preambles_per_ssb, (int)total_ra_preambles);

  check.checked = true;
  check.num_valid_rapids = num_valid_rapids;
  check.valid = preamble_index < num_valid_rapids;
  return check;
}

static int16_t ssb_index_from_prach(nr_cell_sched_t *cell,
                                    frame_t frameP,
                                    slot_t slotP,
                                    uint16_t preamble_index,
                                    uint8_t freq_index,
                                    uint8_t symbol)
{
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  nfapi_nr_config_request_scf_t *cfg = &cell->config;
  NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
  uint8_t config_index = 0;
  uint8_t fdm = cfg->prach_config.num_prach_fd_occasions.value;
  
  uint8_t total_RApreambles = MAX_NUM_NR_PRACH_PREAMBLES;
  if (rach_ConfigCommon->totalNumberOfRA_Preambles != NULL)
    total_RApreambles = *rach_ConfigCommon->totalNumberOfRA_Preambles;

  ssb_ro_preambles_t ssb_ro;
  uint16_t start_symbol_index = 0;
  uint8_t temp_start_symbol = 0;
  uint16_t RA_sfn_index = -1;
  uint16_t prach_occasion_id = -1;
  uint8_t num_active_ssb = cc->num_active_ssb;
  NR_MsgA_ConfigCommon_r16_t *msgacc = NULL;
  if (scc->uplinkConfigCommon->initialUplinkBWP->ext1 && scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16)
    msgacc = scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  get_config_index_and_ssb_ro(msgacc, rach_ConfigCommon, &config_index, &ssb_ro);
  float num_ssb_per_RO = ssb_ro.ssb_per_ro;
  const int ul_mu = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
  const int mu = nr_get_prach_or_ul_mu(msgacc, rach_ConfigCommon, ul_mu);
  frequency_range_t freq_range = get_freq_range_from_arfcn(scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA);
  get_nr_prach_sched_from_info(cc->prach_info, config_index, frameP, slotP, mu, freq_range, &RA_sfn_index, cc->frame_type);

  uint8_t index = 0, slot_index = 0;
  for (slot_index = 0; slot_index < cc->prach_info.N_RA_slot; slot_index++) {
    if (cc->prach_info.N_RA_slot <= 1) { // 1 PRACH slot in a subframe
       if((mu == 1) || (mu == 3))
         slot_index = 1; // For scs = 30khz and 120khz
    }
    for (int i = 0; i < cc->prach_info.N_t_slot; i++) {
      temp_start_symbol = (cc->prach_info.start_symbol + i * cc->prach_info.N_dur + 14 * slot_index) % 14;
      if(symbol == temp_start_symbol) {
        start_symbol_index = i;
        break;
      }
    }
  }
  if (cc->prach_info.N_RA_slot <= 1) { // 1 PRACH slot in a subframe
    if((mu == 1) || (mu == 3))
      slot_index = 0; // For scs = 30khz and 120khz
  }
  int config_period = cc->prach_info.x;
  //  prach_occasion_id = subframe_index * N_t_slot * N_RA_slot * fdm + N_RA_slot_index * N_t_slot * fdm + freq_index + fdm * start_symbol_index;
  prach_occasion_id =
      (((frameP % (cc->association_period * config_period)) / config_period) * cc->total_prach_occasions_per_config_period)
      + (RA_sfn_index + slot_index) * cc->prach_info.N_t_slot * fdm + start_symbol_index * fdm + freq_index;

  //one SSB have more than one continuous RO
  if(num_ssb_per_RO <= 1)
    index = (int) (prach_occasion_id / (int)(1 / num_ssb_per_RO)) % num_active_ssb;
  //one RO is shared by one or more SSB
  else if (num_ssb_per_RO > 1) {
    index = (prach_occasion_id * (int)num_ssb_per_RO) % num_active_ssb;
    for(int j = 0; j < num_ssb_per_RO; j++) {
      if(preamble_index <  ((j + 1) * cc->cb_preambles_per_ssb))
        index = index + j;
    }
  }

  LOG_D(NR_MAC, "Frame %d, Slot %d: Prach Occasion id = %d ssb per RO = %f number of active SSB %u index = %d fdm %u symbol index %u freq_index %u total_RApreambles %u\n",
        frameP, slotP, prach_occasion_id, num_ssb_per_RO, num_active_ssb, index, fdm, start_symbol_index, freq_index, total_RApreambles);

  return index;
}

//Compute Total active SSBs and RO available
void find_SSB_and_RO_available(nr_cell_sched_t *cell)
{
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  nfapi_nr_config_request_scf_t *cfg = &cell->config;
  NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
  uint8_t config_index = 0;
  uint16_t unused_RA_occasion, repetition = 0;
  uint8_t num_active_ssb = 0;
  ssb_ro_preambles_t ssb_ro;
  NR_MsgA_ConfigCommon_r16_t *msgacc = NULL;
  if (scc->uplinkConfigCommon->initialUplinkBWP->ext1 && scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16)
    msgacc = scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  get_config_index_and_ssb_ro(msgacc, rach_ConfigCommon, &config_index, &ssb_ro);
  cc->cb_preambles_per_ssb = ssb_ro.preambles_per_ssb;

  // prach is scheduled according to configuration index and tables 6.3.3.2.2 to 6.3.3.2.4
  frequency_range_t freq_range = get_freq_range_from_arfcn(scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA);
  nr_prach_info_t prach_info =  get_nr_prach_occasion_info_from_index(config_index, freq_range, cc->frame_type);

  float num_ssb_per_RO = ssb_ro.ssb_per_ro;
  uint8_t fdm = cfg->prach_config.num_prach_fd_occasions.value;
  uint64_t L_ssb = (((uint64_t) cfg->ssb_table.ssb_mask_list[0].ssb_mask.value) << 32) | cfg->ssb_table.ssb_mask_list[1].ssb_mask.value;
  cc->total_prach_occasions_per_config_period = prach_info.N_RA_sfn * prach_info.N_t_slot * prach_info.N_RA_slot * fdm;

  for(int i = 0; i < 64; i++) {
    if ((L_ssb >> (63 - i)) & 0x01) { // only if the bit of L_ssb at current ssb index is 1
      cc->ssb_index[num_active_ssb] = i;
      num_active_ssb++;
    }
  }

  // An association period, starting from frame 0, for mapping SS/PBCH block indexes to PRACH occasions is the smallest
  // value in the set determined by the PRACH configuration period according Table 8.1-1 of 38.213
  // such that all tx SSB indexes are mapped at least once to the PRACH occasions within the association period
  // An association pattern period includes one or more association periods and is determined
  // so that a pattern between PRACH occasions and SS/PBCH block indexes repeats at most every 160 msec
  int total_RA_occasions = 0;
  for (int i = 1; (1 << (i - 1)) <= prach_info.max_association_period; i++) {
    cc->association_period = (1 << (i - 1));
    int temp_RA_occasions = cc->total_prach_occasions_per_config_period * cc->association_period;
    if(temp_RA_occasions >= (int) (num_active_ssb / num_ssb_per_RO)) {
      total_RA_occasions = temp_RA_occasions;
      repetition = (uint16_t)((total_RA_occasions * num_ssb_per_RO) / num_active_ssb);
      break;
    }
  }
  AssertFatal(total_RA_occasions > 0, "Couldn't find a valid association period for PRACH occasions\n");

  unused_RA_occasion = total_RA_occasions - (int)((num_active_ssb * repetition) / num_ssb_per_RO);
  cc->total_prach_occasions = total_RA_occasions - unused_RA_occasion;
  cc->num_active_ssb = num_active_ssb;
  cc->prach_info = prach_info;

  LOG_D(NR_MAC,
        "Total available RO %d, num of active SSB %d: unused RO = %d association_period %u N_RA_sfn %u "
        "total_prach_occasions_per_config_period %u\n",
        cc->total_prach_occasions,
        cc->num_active_ssb,
        unused_RA_occasion,
        cc->association_period,
        prach_info.N_RA_sfn,
        cc->total_prach_occasions_per_config_period);
}

static void schedule_nr_MsgA_pusch(NR_UplinkConfigCommon_t *uplinkConfigCommon,
                                   gNB_MAC_INST *nr_mac,
                                   nr_cell_sched_t *cell,
                                   frame_t frameP,
                                   slot_t slotP,
                                   nfapi_nr_prach_pdu_t *prach_pdu,
                                   uint16_t dmrs_TypeA_Position,
                                   NR_PhysCellId_t physCellId)
{

  NR_MsgA_ConfigCommon_r16_t *msgacc = uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource = msgacc->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;

  const int n_slots_frame = cell->frame_structure.numb_slots_frame;
  slot_t msgA_pusch_slot = (slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) % n_slots_frame;
  frame_t msgA_pusch_frame = (frameP + ((slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) / n_slots_frame)) % 1024;

  int index = ul_buffer_index((int)msgA_pusch_frame, (int)msgA_pusch_slot, n_slots_frame, cell->UL_tti_req_ahead_size);
  nfapi_nr_ul_tti_request_t *UL_tti_req = &cell->UL_tti_req_ahead[index];

  UL_tti_req->SFN = msgA_pusch_frame;
  UL_tti_req->Slot = msgA_pusch_slot;
  AssertFatal(is_ul_slot(msgA_pusch_slot, &cell->frame_structure),
              "Slot %d is not an Uplink slot, invalid msgA_PUSCH_TimeDomainOffset_r16 %ld\n",
              msgA_pusch_slot,
              msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16);

  UL_tti_req->pdus_list[UL_tti_req->n_pdus].pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
  UL_tti_req->pdus_list[UL_tti_req->n_pdus].pdu_size = sizeof(nfapi_nr_pusch_pdu_t);
  nfapi_nr_pusch_pdu_t *pusch_pdu = &UL_tti_req->pdus_list[UL_tti_req->n_pdus].pusch_pdu;
  memset(pusch_pdu, 0, sizeof(nfapi_nr_pusch_pdu_t));

  rnti_t ra_rnti = nr_get_ra_rnti(prach_pdu->prach_start_symbol, slotP, prach_pdu->num_ra, 0);

  // Fill PUSCH PDU
  pusch_pdu->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
  pusch_pdu->rnti = ra_rnti;
  pusch_pdu->handle = 0;
  pusch_pdu->rb_size = msgA_PUSCH_Resource->nrofPRBs_PerMsgA_PO_r16;
  pusch_pdu->mcs_table = 0;
  pusch_pdu->frequency_hopping =
      msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 ? *msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 : 0;
  pusch_pdu->dmrs_ports = 1; // 6.2.2 in 38.214 only port 0 to be used
  AssertFatal(msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16,
              "Only SLIV based on startSymbolAndLengthMsgA_PO_r16 implemented\n");
  int S = 0;
  int L = 0;
  SLIV2SL(*msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16, &S, &L);
  pusch_pdu->start_symbol_index = S;
  pusch_pdu->nr_of_symbols = L;
  pusch_pdu->pusch_data.new_data_indicator = 1;
  pusch_pdu->nrOfLayers = 1;
  pusch_pdu->num_dmrs_cdm_grps_no_data = L <= 2 ? 1 : 2; // no data in dmrs symbols as in 6.2.2 in 38.214
  pusch_pdu->ul_dmrs_symb_pos =
      get_l_prime(L, *msgA_PUSCH_Resource->mappingTypeMsgA_PUSCH_r16, pusch_dmrs_pos2, pusch_len1, S, dmrs_TypeA_Position);
  pusch_pdu->transform_precoding = *msgacc->msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16;
  pusch_pdu->rb_bitmap[0] = 0;
  pusch_pdu->rb_start = msgA_PUSCH_Resource->frequencyStartMsgA_PUSCH_r16; // rb_start depends on the RO
  int locationAndBandwidth = uplinkConfigCommon->initialUplinkBWP->genericParameters.locationAndBandwidth;
  pusch_pdu->bwp_size = NRRIV2BW(locationAndBandwidth, MAX_BWP_SIZE);
  pusch_pdu->bwp_start = NRRIV2PRBOFFSET(locationAndBandwidth, MAX_BWP_SIZE);
  pusch_pdu->subcarrier_spacing = uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
  pusch_pdu->cyclic_prefix = 0;
  pusch_pdu->uplink_frequency_shift_7p5khz = 0;
  pusch_pdu->vrb_to_prb_mapping = 0;
  pusch_pdu->dmrs_config_type = 0;
  NR_MsgA_PUSCH_Config_r16_t *msgA_pusch_cfg = msgacc->msgA_PUSCH_Config_r16;
  pusch_pdu->data_scrambling_id =
      msgA_pusch_cfg->msgA_DataScramblingIndex_r16 ? *msgA_pusch_cfg->msgA_DataScramblingIndex_r16 : physCellId;
  pusch_pdu->ul_dmrs_scrambling_id = msgA_PUSCH_Resource->msgA_DMRS_Config_r16.msgA_ScramblingID0_r16
                                         ? *msgA_PUSCH_Resource->msgA_DMRS_Config_r16.msgA_ScramblingID0_r16
                                         : physCellId;
  pusch_pdu->scid = 0; // n_SCID from DMRS resource mapping (TS 38.213 8.1A); only resource 0 scheduled (nrofDMRS-Sequences = 1)
  pusch_pdu->pusch_identity = 0;
  pusch_pdu->resource_alloc = 1; // type 1
  pusch_pdu->tx_direct_current_location = 0;
  pusch_pdu->mcs_index = msgA_PUSCH_Resource->msgA_MCS_r16;
  pusch_pdu->qam_mod_order = nr_get_Qm_dl(pusch_pdu->mcs_index, pusch_pdu->mcs_table);

  int num_dmrs_symb = count_bits64_with_mask(pusch_pdu->ul_dmrs_symb_pos, S, L);
  AssertFatal(pusch_pdu->mcs_index <= 28, "Exceeding MCS limit for MsgA PUSCH\n");
  int R = nr_get_code_rate_ul(pusch_pdu->mcs_index, pusch_pdu->mcs_table);
  pusch_pdu->target_code_rate = R;
  int TBS = nr_compute_tbs(pusch_pdu->qam_mod_order,
                           R,
                           pusch_pdu->rb_size,
                           pusch_pdu->nr_of_symbols,
                           num_dmrs_symb * 12, // nb dmrs set for no data in dmrs symbol
                           0, // nb_rb_oh
                           0, // to verify tb scaling
                           pusch_pdu->nrOfLayers)
            >> 3;

  pusch_pdu->pusch_data.tb_size = TBS;
  pusch_pdu->maintenance_parms_v3.ldpcBaseGraph = get_BG(TBS << 3, R);

  pusch_pdu->param_v4.numSpatialStreamIndices = cell->radio_config.pusch_AntennaPorts;

  LOG_D(NR_MAC, "Scheduling MsgA PUSCH in %d.%d\n", msgA_pusch_frame, msgA_pusch_slot);

  LOG_D(NR_MAC,
        "pdu_bit_map %d rnti %d handle %d rb_size %d mcs_table %d frequency_hopping %d dmrs_ports %d start_symbol_index %d "
        "nr_of_symbols %d new_data_indicator %d num_dmrs_cdm_grps_no_data %d ul_dmrs_symb_pos %d transform_precoding %d rb_start "
        "%d bwp_size %d bwp_start %d subcarrier_spacing %d data_scrambling_id %d ul_dmrs_scrambling_id %d mcs_index %d "
        "qam_mod_order %d target_code_rate %d tb_size %d ldpcBaseGraph %d numSpatialStreamIndices %d\n",
        pusch_pdu->pdu_bit_map,
        pusch_pdu->rnti,
        pusch_pdu->handle,
        pusch_pdu->rb_size,
        pusch_pdu->mcs_table,
        pusch_pdu->frequency_hopping,
        pusch_pdu->dmrs_ports,
        pusch_pdu->start_symbol_index,
        pusch_pdu->nr_of_symbols,
        pusch_pdu->pusch_data.new_data_indicator,
        pusch_pdu->num_dmrs_cdm_grps_no_data,
        pusch_pdu->ul_dmrs_symb_pos,
        pusch_pdu->transform_precoding,
        pusch_pdu->rb_start,
        pusch_pdu->bwp_size,
        pusch_pdu->bwp_start,
        pusch_pdu->subcarrier_spacing,
        pusch_pdu->data_scrambling_id,
        pusch_pdu->ul_dmrs_scrambling_id,
        pusch_pdu->mcs_index,
        pusch_pdu->qam_mod_order,
        pusch_pdu->target_code_rate,
        pusch_pdu->pusch_data.tb_size,
        pusch_pdu->maintenance_parms_v3.ldpcBaseGraph,
        pusch_pdu->param_v4.numSpatialStreamIndices);

  UL_tti_req->n_pdus += 1;
}

static void fill_vrb(const frame_t frame,
                     const slot_t slot,
                     int nb_rb,
                     int beam_idx,
                     int vrb_size,
                     int slots_frame,
                     int rb_start,
                     int start_symb,
                     int num_symb,
                     NR_COMMON_channels_t *cc)
{
  const int index = ul_buffer_index(frame, slot, slots_frame, vrb_size);
  uint16_t *vrb_map_UL = &cc->vrb_map_UL[beam_idx][index * MAX_BWP_SIZE];
  for (int i = 0; i < nb_rb; ++i) {
    AssertFatal(
        !(vrb_map_UL[rb_start + i] & SL_to_bitmap(start_symb, num_symb)),
        "PRACH resources are already occupied!\n");
    vrb_map_UL[rb_start + i] |= SL_to_bitmap(start_symb, num_symb);
  }
}

void schedule_nr_prach(gNB_MAC_INST *gNB, nr_cell_sched_t *cell, frame_t frameP, slot_t slotP)
{
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_BWP_UplinkCommon_t *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;
  NR_RACH_ConfigCommon_t *rach_ConfigCommon = initialUplinkBWP->rach_ConfigCommon->choice.setup;
  NR_MsgA_ConfigCommon_r16_t *msgacc = NULL;
  if (initialUplinkBWP->ext1 && initialUplinkBWP->ext1->msgA_ConfigCommon_r16)
    msgacc = initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  int slots_frame = cell->frame_structure.numb_slots_frame;
  int index = ul_buffer_index(frameP, slotP, slots_frame, cell->UL_tti_req_ahead_size);
  nfapi_nr_ul_tti_request_t *UL_tti_req = &cell->UL_tti_req_ahead[index];
  nfapi_nr_config_request_scf_t *cfg = &cell->config;

  if (is_ul_slot(slotP, &cell->frame_structure)) {
    const NR_RACH_ConfigGeneric_t *rach_ConfigGeneric = &rach_ConfigCommon->rach_ConfigGeneric;
    uint8_t config_index = 0;
    ssb_ro_preambles_t ssb_ro;
    float num_ssb_per_RO = 0;
    int slot_index = 0;
    uint16_t prach_occasion_id = -1;
    get_config_index_and_ssb_ro(msgacc, rach_ConfigCommon, &config_index, &ssb_ro);
    num_ssb_per_RO = ssb_ro.ssb_per_ro;

    int bwp_start = NRRIV2PRBOFFSET(initialUplinkBWP->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);

    uint8_t fdm = cfg->prach_config.num_prach_fd_occasions.value;
    const int ul_mu = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
    const int mu = nr_get_prach_or_ul_mu(msgacc, rach_ConfigCommon, ul_mu);
    // prach is scheduled according to configuration index and tables 6.3.3.2.2 to 6.3.3.2.4
    uint16_t RA_sfn_index = -1;
    frequency_range_t freq_range = get_freq_range_from_arfcn(scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA);
    const int mu_pusch = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
    const int16_t n_ra_rb = get_N_RA_RB(cfg->prach_config.prach_sub_c_spacing.value, mu_pusch);
    if (get_nr_prach_sched_from_info(cc->prach_info, config_index, frameP, slotP, mu, freq_range, &RA_sfn_index, cc->frame_type)) {
      uint16_t format0 = cc->prach_info.format & 0xff; // first column of format from table
      uint16_t format1 = (cc->prach_info.format >> 8) & 0xff; // second column of format from table

      if (cc->prach_info.N_RA_slot > 1) { // more than 1 PRACH slot in a subframe
        if (slotP % 2 == 1)
          slot_index = 1;
        else
          slot_index = 0;
      } else if (cc->prach_info.N_RA_slot <= 1) { // 1 PRACH slot in a subframe
        slot_index = 0;
      }

      UL_tti_req->SFN = frameP;
      UL_tti_req->Slot = slotP;
      UL_tti_req->rach_present = 1;
      NR_beam_alloc_t beam = {0};
      uint32_t N_t_slot = cc->prach_info.N_t_slot;
      uint32_t start_symb = cc->prach_info.start_symbol;
      for (int fdm_index = 0; fdm_index < fdm; fdm_index++) { // one structure per frequency domain occasion
        AssertFatal(UL_tti_req->n_pdus < sizeofArray(UL_tti_req->pdus_list), "Invalid UL_tti_req->n_pdus %d\n", UL_tti_req->n_pdus);
        nfapi_nr_ul_tti_request_number_of_pdus_t *newpdu = UL_tti_req->pdus_list + UL_tti_req->n_pdus;
        *newpdu = (nfapi_nr_ul_tti_request_number_of_pdus_t){
            .pdu_type = NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE,
            .pdu_size = sizeof(nfapi_nr_prach_pdu_t),
        };
        UL_tti_req->n_pdus += 1;
        int num_td_occ = 0;
        nfapi_nr_prach_pdu_t *prach_pdu = &newpdu->prach_pdu;
        for (int td_index = 0; td_index < N_t_slot; td_index++) {
          uint32_t config_period = cc->prach_info.x;
          prach_occasion_id = (((frameP % (cc->association_period * config_period))/config_period) * cc->total_prach_occasions_per_config_period) +
                              (RA_sfn_index + slot_index) * N_t_slot * fdm + td_index * fdm + fdm_index;

          if (prach_occasion_id >= cc->total_prach_occasions) // to be confirmed: unused occasion?
            continue;

          num_td_occ++;
          int beam_index = 0;
          if(num_ssb_per_RO <= 1) {
            // ordered ssb number
            int n_ssb = (int) (prach_occasion_id / (int)(1 / num_ssb_per_RO)) % cc->num_active_ssb;
            // fapi beam index
            beam_index = get_beam_from_ssbidx(cell, cc->ssb_index[n_ssb]);
            // multi-beam allocation structure
            beam = beam_allocation_procedure(&cell->beam_info, frameP, slotP, beam_index, slots_frame);
            AssertFatal(beam.idx >= 0, "Cannot allocate PRACH corresponding to %d SSB transmitted in any available beam\n", n_ssb + 1);
          } else {
            int first_ssb_index = (prach_occasion_id * (int)num_ssb_per_RO) % cc->num_active_ssb;
            for(int j = first_ssb_index; j < first_ssb_index + num_ssb_per_RO; j++) {
              // fapi beam index
              beam_index = get_beam_from_ssbidx(cell, cc->ssb_index[j]);
              // multi-beam allocation structure
              beam = beam_allocation_procedure(&cell->beam_info, frameP, slotP, beam_index, slots_frame);
              AssertFatal(beam.idx >= 0, "Cannot allocate PRACH corresponding to SSB %d in any available beam\n", j);
            }
          }
          if(num_td_occ == 1) {
            // filling the prach fapi structure
            prach_pdu->phys_cell_id = *scc->physCellId;
            prach_pdu->prach_start_symbol = start_symb;
            prach_pdu->num_ra = fdm_index;
            prach_pdu->num_cs = get_NCS(rach_ConfigGeneric->zeroCorrelationZoneConfig,
                                        format0,
                                        rach_ConfigCommon->restrictedSetConfig);

            // SCF PRACH PDU format field does not consider A1/B1 etc. possibilities
            // We added 9 = A1/B1 10 = A2/B2 11 A3/B3
            if (format1!=0xff) {
              switch(format0) {
                case 0xa1:
                  prach_pdu->prach_format = 11;
                  break;
                case 0xa2:
                  prach_pdu->prach_format = 12;
                  break;
                case 0xa3:
                  prach_pdu->prach_format = 13;
                  break;
              default:
                AssertFatal(1==0,"Only formats A1/B1 A2/B2 A3/B3 are valid for dual format");
              }
            }
            else{
              switch(format0) {
                case 0:
                  prach_pdu->prach_format = 0;
                  break;
                case 1:
                  prach_pdu->prach_format = 1;
                  break;
                case 2:
                  prach_pdu->prach_format = 2;
                  break;
                case 3:
                  prach_pdu->prach_format = 3;
                  break;
                case 0xa1:
                  prach_pdu->prach_format = 4;
                  break;
                case 0xa2:
                  prach_pdu->prach_format = 5;
                  break;
                case 0xa3:
                  prach_pdu->prach_format = 6;
                  break;
                case 0xb1:
                  prach_pdu->prach_format = 7;
                  break;
                case 0xb4:
                  prach_pdu->prach_format = 8;
                  break;
                case 0xc0:
                  prach_pdu->prach_format = 9;
                  break;
                case 0xc2:
                  prach_pdu->prach_format = 10;
                  break;
              default:
                AssertFatal(1==0,"Invalid PRACH format");
              }
            }
            if (initialUplinkBWP->ext1 && initialUplinkBWP->ext1->msgA_ConfigCommon_r16) {
              schedule_nr_MsgA_pusch(scc->uplinkConfigCommon,
                                     gNB,
                                     cell,
                                     frameP,
                                     slotP,
                                     prach_pdu,
                                     scc->dmrs_TypeA_Position,
                                     *scc->physCellId);
            }
          }
          prach_pdu->num_prach_ocas = num_td_occ;
          prach_pdu->beamforming.num_prgs = 1;
          prach_pdu->beamforming.prg_size = n_ra_rb;
          // Spatial stream indices
          const uint16_t num_log_ports = cell->radio_config.pusch_AntennaPorts;
          const uint8_t num_beams = beam.idx + 1;
          prach_pdu->param_v4.numSpatialStreamIndices = num_beams * num_log_ports;
          const int start_stream_idx = beam.idx * num_log_ports;
          for (int s = 0; s < num_log_ports; s++)
            prach_pdu->param_v4.spatialStreamIndices[start_stream_idx + s] =
                cell->radio_config.spatial_stream_index[start_stream_idx + s];
          // Beam id
          const uint16_t fapi_beam = convert_to_fapi_beam(beam_index, &cell->beam_info);
          prach_pdu->beamforming.dig_bf_interface = num_beams;
          prach_pdu->beamforming.prgs_list[0].dig_bf_interface_list[beam.idx].beam_idx = fapi_beam;

          LOG_D(NR_MAC,
                "Frame %d, Slot %d: Prach Occasion id = %u  fdm index = %u start symbol = %u slot index = %u subframe index = %u \n",
                frameP,
                slotP,
                prach_occasion_id,
                prach_pdu->num_ra,
                prach_pdu->prach_start_symbol,
                slot_index,
                RA_sfn_index);
        }
        if (num_td_occ == 0) // no TD PRACH occasion -> no PRACH PDU
          UL_tti_req->n_pdus -= 1;
        else
          cell->num_scheduled_prach_rx += num_td_occ;
      }

      // block resources in vrb_map_UL
      // mark PRBs as occupied for current and future slots if prach extends beyond current slot
      int total_prach_slots;
      uint32_t N_dur = cc->prach_info.N_dur;
      if (format0 < 4) {
        N_dur = 14; // number of PRACH symbols in PRACH slot
        total_prach_slots = get_long_prach_dur(format0, mu_pusch);
        AssertFatal(slotP + total_prach_slots - 1 < slots_frame, "PRACH cannot extend across frames\n");
      } else {
        total_prach_slots = 1;
      }
      // reserve PRBs occupied by PRACH in all PRACH slot.
      for (int i = 0; i < total_prach_slots; i++) {
        fill_vrb(frameP,
                 slotP + i,
                 n_ra_rb * fdm,
                 beam.idx,
                 cell->vrb_map_UL_size,
                 slots_frame,
                 bwp_start + rach_ConfigGeneric->msg1_FrequencyStart,
                 start_symb,
                 N_t_slot * N_dur,
                 cc);
      }
    }
  }
}

int nr_fill_successrar(const NR_UE_sched_ctrl_t *ue_sched_ctl,
                       rnti_t crnti,
                       const unsigned char *ue_cont_res_id,
                       uint8_t resource_indicator,
                       uint8_t timing_indicator,
                       unsigned char *mac_pdu,
                       int mac_pdu_length)
{
  LOG_D(NR_MAC, "mac_pdu_length = %d\n", mac_pdu_length);
  int timing_advance_cmd = ue_sched_ctl->ta_update;
  // TS 38.321 - Figure 6.1.5a-1: BI MAC subheader
  NR_RA_HEADER_BI_MSGB *bi = (NR_RA_HEADER_BI_MSGB *)&mac_pdu[mac_pdu_length];
  mac_pdu_length += sizeof(NR_RA_HEADER_BI_MSGB);

  bi->E = 1;
  bi->T1 = 0;
  bi->T2 = 0;
  bi->R = 0;
  bi->BI = 0; // BI = 0, 5ms

  // TS 38.321 - Figure 6.1.5a-3: SuccessRAR MAC subheader
  NR_RA_HEADER_SUCCESS_RAR_MSGB *SUCESS_RAR_header = (NR_RA_HEADER_SUCCESS_RAR_MSGB *)&mac_pdu[mac_pdu_length];
  mac_pdu_length += sizeof(NR_RA_HEADER_SUCCESS_RAR_MSGB);

  SUCESS_RAR_header->E = 0;
  SUCESS_RAR_header->T1 = 0;
  SUCESS_RAR_header->T2 = 1;
  SUCESS_RAR_header->S = 0;
  SUCESS_RAR_header->R = 0;

  // TS 38.321 - Figure 6.2.3a-2: successRAR
  NR_MAC_SUCCESS_RAR *successRAR = (NR_MAC_SUCCESS_RAR *)&mac_pdu[mac_pdu_length];
  mac_pdu_length += sizeof(NR_MAC_SUCCESS_RAR);

  successRAR->CONT_RES_1 = ue_cont_res_id[0];
  successRAR->CONT_RES_2 = ue_cont_res_id[1];
  successRAR->CONT_RES_3 = ue_cont_res_id[2];
  successRAR->CONT_RES_4 = ue_cont_res_id[3];
  successRAR->CONT_RES_5 = ue_cont_res_id[4];
  successRAR->CONT_RES_6 = ue_cont_res_id[5];
  successRAR->R = 0;
  successRAR->CH_ACESS_CPEXT = 1;
  successRAR->TPC = 1; // 0dB change, don't know how to determine this.
  successRAR->HARQ_FTI = timing_indicator;
  successRAR->PUCCH_RI = resource_indicator;
  successRAR->TA1 = (uint8_t)(timing_advance_cmd >> 8); // 4 MSBs of timing advance;
  successRAR->TA2 = (uint8_t)(timing_advance_cmd & 0xff); // 8 LSBs of timing advance;
  successRAR->CRNTI_1 = (uint8_t)(crnti >> 8); // 8 MSBs of rnti
  successRAR->CRNTI_2 = (uint8_t)(crnti & 0xff); // 8 LSBs of rnti

  LOG_D(NR_MAC,
        "successRAR: Contention Resolution ID 0x%02x%02x%02x%02x%02x%02x R 0x%01x CH_ACESS_CPEXT 0x%02x TPC 0x%02x HARQ_FTI 0x%03x "
        "PUCCH_RI 0x%04x TA 0x%012x CRNTI 0x%04x\n",
        successRAR->CONT_RES_1,
        successRAR->CONT_RES_2,
        successRAR->CONT_RES_3,
        successRAR->CONT_RES_4,
        successRAR->CONT_RES_5,
        successRAR->CONT_RES_6,
        successRAR->R,
        successRAR->CH_ACESS_CPEXT,
        successRAR->TPC,
        successRAR->HARQ_FTI,
        successRAR->PUCCH_RI,
        timing_advance_cmd,
        crnti);
  LOG_D(NR_MAC, "mac_pdu_length = %d\n", mac_pdu_length);
  return mac_pdu_length;
}

/** @brief find UE with RA process for given preamble */
static NR_UE_info_t *get_existing_ra(gNB_MAC_INST *nr_mac, const nr_cell_sched_t* cell, uint16_t preamble_index)
{
  FOR_EACH_RA_UE(&nr_mac->UE_info, UE) {
    if (UE->pcell != cell) {
      continue;
    }
    NR_RA_t *ra = UE->ra;
    for (int i = 0; i < ra->preambles.num_preambles; ++i) {
      if (ra->preambles.preamble_list[i] == preamble_index)
        return UE;
    }
  }
  return NULL;
}

static bool find_rnti(const void *search, const void *current)
{
  rnti_t s = *(const rnti_t *) search;
  const NR_UE_info_t *c = *(const NR_UE_info_t **) current;
  // note: not all UEs necessarily have c->ra when being removed from RA list
  // TODO why does 2-step RA removal (before this commit) not check c->ra->RA_rnti?
  return (c->ra && c->ra->ra_type == RA_2_STEP && c->ra->RA_rnti == s) || s == c->rnti;
}

/** @brief add UE to list of UEs doing RA.
 *
 * Remove with remove_ra_ue(). */
bool add_new_ra_ue(NR_UEs_t *UEs, NR_UE_info_t *UE)
{
  DevAssert(UE->ra); // a UE in the acess_ue_list needs to have an RA process
  if (seq_arr_size(&UEs->access_ue_list) >= NR_NB_RA_PROC_MAX)
    return false;
  /* store pointer to UE! */
  seq_arr_push_back(&UEs->access_ue_list, &UE, sizeof(UE));
  return true;
}

/** @brief release a UE from list of UEs doing RA.
 *
 * The corresponding function to add is add_new_ra_ue(). */
NR_UE_info_t *remove_ra_ue(NR_UEs_t *UEs, rnti_t rnti)
{
  elm_arr_t el = find_if(&UEs->access_ue_list, &rnti, find_rnti);
  if (el.found) {
    NR_UE_info_t *found = *(NR_UE_info_t **)el.it;
    seq_arr_erase(&UEs->access_ue_list, el.it);
    return found;
  }
  return NULL;
}

NR_UE_info_t *find_ra_ue(NR_UEs_t *UEs, rnti_t rnti)
{
  elm_arr_t el = find_if(&UEs->access_ue_list, &rnti, find_rnti);
  if (el.found)
    return *(NR_UE_info_t **)el.it;
  return NULL;
}

static uint8_t nr_get_msg3_tpc(uint32_t preamble_power)
{
  // TODO not sure how to implement TPC for MSG3 to be sent in RAR
  //      maybe using preambleReceivedTargetPower as a term of comparison
  //      in any case OAI L1 sets this as invalid
  //      and Aerial report doesn't seem to be reliable (not matching preambleReceivedTargetPower)
  //      so for now we feedback 0dB TPC
  UNUSED(preamble_power);
  return 3; // it means 0dB
}

static unsigned int get_slot_RA(const NR_ServingCellConfigCommon_t *scc,
                                const NR_RACH_ConfigCommon_t *rach_ConfigCommon,
                                const NR_MsgA_ConfigCommon_r16_t *msgacc,
                                frame_type_t frame_type,
                                int slot)
{
  uint8_t index = msgacc ? *msgacc->rach_ConfigCommonTwoStepRA_r16.rach_ConfigGenericTwoStepRA_r16.msgA_PRACH_ConfigurationIndex_r16
                         : rach_ConfigCommon->rach_ConfigGeneric.prach_ConfigurationIndex;
  uint16_t prach_format =
    get_nr_prach_format_from_index(index, scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA, frame_type);
  unsigned int slot_RA;
  if ((prach_format & 0xff) < 4) {
    const int ul_mu = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
    const int mu = nr_get_prach_or_ul_mu(msgacc, rach_ConfigCommon, ul_mu);
    unsigned int slots_per_sf = (1 << mu);
    slot_RA = slot / slots_per_sf;
  } else {
    slot_RA = slot;
  }
  return slot_RA;
}

void nr_initiate_ra_proc(module_id_t module_idP,
                         int phy_id,
                         frame_t frame,
                         int slot,
                         uint16_t preamble_index,
                         uint8_t freq_index,
                         uint8_t symbol,
                         int16_t timing_offset,
                         uint32_t preamble_power)
{
  gNB_MAC_INST *nr_mac = RC.nrmac[module_idP];
  nr_cell_sched_t* cell = nr_mac_get_cell_by_phy_id(nr_mac, phy_id);
  NR_SCHED_LOCK(&nr_mac->sched_lock);
  /* check if preamble exists (NSA, HO cases) */
  NR_UE_info_t *UE = get_existing_ra(nr_mac, cell, preamble_index);
  if ((get_softmodem_params()->nsa || get_softmodem_params()->do_ra) && !UE) {
    /* we are in NSA, but no UE has been configured => we ignore it */
    LOG_W(NR_MAC, "random access with preamble %d: no pre-configured RA process found\n", preamble_index);
    NR_SCHED_UNLOCK(&nr_mac->sched_lock);
    return;
  }

  /*
   * For unknown-UE four-step CBRA, reject RAPIDs outside the configured
   * contention-based preamble set before allocating a TC-RNTI.
   */
  if (!UE) {
    nr_rapid_check_t rapid_check = check_rapid_for_prach_occasion(cell, preamble_index);
    if (!rapid_check.checked) {
      LOG_D(NR_MAC,
            "[RAPROC] %d.%d RAPID %u symbol %u freq-index %u not checked: %s\n",
            frame,
            slot,
            preamble_index,
            symbol,
            freq_index,
            rapid_check.reason);
    } else if (!rapid_check.valid) {
      LOG_W(NR_MAC,
            "[RAPROC] %d.%d ignoring RAPID %u at symbol %u freq-index %u: outside valid CBRA range [0,%u)\n",
            frame,
            slot,
            preamble_index,
            symbol,
            freq_index,
            rapid_check.num_valid_rapids);
      NR_SCHED_UNLOCK(&nr_mac->sched_lock);
      return;
    } else {
      LOG_D(NR_MAC,
            "[RAPROC] %d.%d RAPID %u symbol %u freq-index %u accepted in CBRA range [0,%u)\n",
            frame,
            slot,
            preamble_index,
            symbol,
            freq_index,
            rapid_check.num_valid_rapids);
    }
  }

  if (!UE) {
    /* in CBRA: we don't know this UE yet. There might be CFRA (e.g., HO IN SA)
     * where we know the UE */
    rnti_t rnti;
    bool rnti_found = nr_mac_get_new_rnti(&nr_mac->UE_info, &rnti);
    if (!rnti_found) {
      LOG_E(NR_MAC, "initialisation random access: no more available RNTIs for new UE\n");
      NR_SCHED_UNLOCK(&nr_mac->sched_lock);
      return;
    }

    UE = get_new_nr_ue_inst(&nr_mac->UE_info.uid_allocator, rnti, NULL, cell);
    if (!add_new_ra_ue(&nr_mac->UE_info, UE)) {
      LOG_E(NR_MAC, "FAILURE: %4d.%2d initiating RA procedure for preamble index %d: no free RA process\n", frame, slot, preamble_index);
      release_nr_ue_inst(&nr_mac->UE_info.uid_allocator, UE);
      NR_SCHED_UNLOCK(&nr_mac->sched_lock);
      return;
    }
  }

  NR_RA_t *ra = UE->ra;
  ra->preamble_frame = frame;
  ra->preamble_slot = slot;
  ra->preamble_index = preamble_index;
  ra->timing_offset = timing_offset;
  ra->msg3_TPC = nr_get_msg3_tpc(preamble_power);

  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  // 3GPP TS 38.321 Section 5.1.3(a) says t_id for RA-RNTI depends on mu as specified in clause 5.3.2 in TS 38.211
  // so mu = 0 for prach format < 4.
  NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
  NR_MsgA_ConfigCommon_r16_t *msgacc = NULL;
  if (scc->uplinkConfigCommon->initialUplinkBWP->ext1 && scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16)
    msgacc = scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  uint8_t ul_carrier_id = 0; // 0 for NUL 1 for SUL
  uint32_t slot_RA = get_slot_RA(scc, rach_ConfigCommon, msgacc, cc->frame_type, slot);
  ra->RA_rnti = nr_get_ra_rnti(symbol, slot_RA, freq_index, ul_carrier_id);
  if (msgacc) {
    ra->ra_type = RA_2_STEP;
    ra->ra_state = nrRA_WAIT_MsgA_PUSCH;
    ra->MsgB_rnti = nr_get_MsgB_rnti(symbol, slot_RA, freq_index, ul_carrier_id);
  } else {
    ra->ra_type = RA_4_STEP;
    ra->ra_state = nrRA_Msg2;
  }

  LOG_A(NR_MAC,
        "%d.%d UE RA-RNTI %04x TC-RNTI %04x: initiating RA procedure for RAPID %u with TA %d units\n",
        frame,
        slot,
        ra->RA_rnti,
        UE->rnti,
        preamble_index,
        timing_offset);

  // return current SSB order in the list of tranmitted SSBs
  int n_ssb = ssb_index_from_prach(cell, frame, slot, preamble_index, freq_index, symbol);
  UE->UE_beam_index = get_beam_from_ssbidx(cell, cc->ssb_index[n_ssb]);
  LOG_I(NR_MAC, "UE %04x: Sync beam index %d\n", UE->rnti, UE->UE_beam_index);

  // Configure RA BWP
  configure_UE_BWP(cell, scc, UE, true, NR_SearchSpace__searchSpaceType_PR_common, -1, -1);
 
  NR_SCHED_UNLOCK(&nr_mac->sched_lock);
}

static void start_ra_contention_resolution_timer(NR_RA_t *ra, const long ra_ContentionResolutionTimer, const int K2, const int scs)
{
  // 3GPP TS 38.331 Section 6.3.2 Radio resource control information elements
  // ra-ContentionResolutionTimer ENUMERATED {sf8, sf16, sf24, sf32, sf40, sf48, sf56, sf64}
  // The initial value for the contention resolution timer.
  // Value sf8 corresponds to 8 subframes, value sf16 corresponds to 16 subframes, and so on.
  // We add 2 * K2 because the timer runs from Msg2 transmission till Msg4 ACK reception
  ra->contention_resolution_timer = ((((int)ra_ContentionResolutionTimer + 1) * 8) << scs) + 2 * K2;
  LOG_D(NR_MAC,
        "Starting RA Contention Resolution timer with %d ms + 2 * %d K2 (%d slots) duration\n",
        ((int)ra_ContentionResolutionTimer + 1) * 8,
        K2,
        ra->contention_resolution_timer);
}

static void nr_generate_Msg3_retransmission(nr_cell_sched_t *cell,
                                            frame_t frame,
                                            slot_t slot,
                                            NR_UE_info_t *UE,
                                            nfapi_nr_ul_dci_request_t *ul_dci_req)
{
  NR_RA_t *ra = UE->ra;
  DevAssert(!ra->cfra);
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &UE->sc_info;
  NR_SearchSpace_t *ss = UE->UE_sched_ctrl.search_space;
  NR_ControlResourceSet_t *coreset = UE->UE_sched_ctrl.coreset;
  AssertFatal(coreset, "Coreset cannot be null for RA-Msg3 retransmission\n");
  const int coresetid = coreset->controlResourceSetId;
  NR_tda_info_t tda_info = get_ul_tda_info(ul_bwp,
                                           coresetid,
                                           NR_SearchSpace__searchSpaceType_PR_common,
                                           TYPE_TC_RNTI_,
                                           ra->Msg3_tda_id);

  int slots_frame = cell->frame_structure.numb_slots_frame;
  int NTN_gNB_Koffset = get_NTN_Koffset(scc);
  const fsn_t sched = get_fb_frame_slot(frame, slot, tda_info.k2, slots_frame, NTN_gNB_Koffset);
  uint16_t slot_bitmap = get_ul_bitmap(&cell->frame_structure, sched.s);
  uint16_t msg3_mask = SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);

  if (!is_dl_slot(slot, &cell->frame_structure)
      || !is_ul_slot(sched.s, &cell->frame_structure)
      || !((msg3_mask & slot_bitmap) == msg3_mask))
    return;

  NR_beam_alloc_t beam_ul = beam_allocation_procedure(&cell->beam_info, sched.f, sched.s, UE->UE_beam_index, slots_frame);
  if (beam_ul.idx < 0)
    return;
  NR_beam_alloc_t beam_dci = beam_allocation_procedure(&cell->beam_info, frame, slot, UE->UE_beam_index, slots_frame);
  if (beam_dci.idx < 0) {
    reset_beam_status(&cell->beam_info, sched.f, sched.s, UE->UE_beam_index, slots_frame, beam_ul.new_beam);
    return;
  }
  int buffer_index = ul_buffer_index(sched.f, sched.s, slots_frame, cell->vrb_map_UL_size);
  uint16_t *vrb_map_UL = &cell->common_channels.vrb_map_UL[beam_ul.idx][buffer_index * MAX_BWP_SIZE];

  NR_pusch_dmrs_t dmrs_info = get_ul_dmrs_params(scc, ul_bwp, &tda_info, 1, 0, 0);
  int num_dmrs_symb = count_bits64_with_mask(dmrs_info.ul_dmrs_symb_pos, tda_info.startSymbolIndex, tda_info.nrOfSymbols);
  int TBS = 0, mcsindex = 0, R = 0, Qm = 0;
  while(TBS < 7) {  // TBS for msg3 is 7 bytes (except for RRCResumeRequest1 currently not implemented)
    mcsindex++;
    AssertFatal(mcsindex <= 28, "Exceeding MCS limit for Msg3\n");
    R = nr_get_code_rate_ul(mcsindex, ul_bwp->mcs_table);
    Qm = nr_get_Qm_ul(mcsindex, ul_bwp->mcs_table);
    TBS = nr_compute_tbs(Qm,
                         R,
                         ra->msg3_nb_rb,
                         tda_info.nrOfSymbols,
                         num_dmrs_symb * 12, // nb dmrs set for no data in dmrs symbol
                         0, //nb_rb_oh
                         0, // to verify tb scaling
                         1) >> 3;
  }

  NR_sched_pusch_t sched_pusch = {
    .bwp_info = get_pusch_bwp_start_size(UE),
    .tb_size = TBS,
    .rbSize = ra->msg3_nb_rb,
    .R = R,
    .Qm = Qm,
    .mcs = mcsindex,
    .nrOfLayers = 1,
    .tda_info = tda_info,
    .dmrs_info = dmrs_info,
  };

  // Map antenna ports for this UE
  sched_pusch.ant_port_idx.numSpatialStreamIndices = cell->radio_config.pusch_AntennaPorts;
  const int start_stream_idx = beam_ul.idx * cell->radio_config.pusch_AntennaPorts;
  for (int i = 0; i < sched_pusch.ant_port_idx.numSpatialStreamIndices; i++)
    sched_pusch.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[start_stream_idx + i];

  const int bwpStart = sched_pusch.bwp_info.bwpStart;
  const int bwpSize = sched_pusch.bwp_info.bwpSize;
  int rbStart = 0;
  int rbSize = 0;
  if (!get_rb_alloc(ra->msg3_nb_rb, ra->msg3_nb_rb, bwpStart, bwpSize, vrb_map_UL, msg3_mask, &rbStart, &rbSize)) {
    // cannot find free vrb_map for msg3 retransmission in this slot
    reset_beam_status(&cell->beam_info, sched.f, sched.s, UE->UE_beam_index, slots_frame, beam_ul.new_beam);
    reset_beam_status(&cell->beam_info, frame, slot, UE->UE_beam_index, slots_frame, beam_dci.new_beam);
    return;
  }

  sched_pusch.rbStart = rbStart;
  LOG_I(NR_MAC,
        "%4d%2d: RA RNTI %04x Cell ID %lu Scheduling retransmission of Msg3 in (%d,%d)\n",
        frame,
        slot,
        UE->rnti,
        cell->nr_cellid,
        sched.f,
        sched.s);

  buffer_index = ul_buffer_index(sched.f, sched.s, slots_frame, cell->UL_tti_req_ahead_size);
  nfapi_nr_ul_tti_request_t *future_ul_tti_req = &cell->UL_tti_req_ahead[buffer_index];
  AssertFatal(future_ul_tti_req->SFN == sched.f
              && future_ul_tti_req->Slot == sched.s,
              "future UL_tti_req's frame.slot %d.%d does not match PUSCH %d.%d\n",
              future_ul_tti_req->SFN,
              future_ul_tti_req->Slot,
              sched.f,
              sched.s);
  AssertFatal(future_ul_tti_req->n_pdus <
              sizeof(future_ul_tti_req->pdus_list) / sizeof(future_ul_tti_req->pdus_list[0]),
              "Invalid future_ul_tti_req->n_pdus %d\n", future_ul_tti_req->n_pdus);
  future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
  future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_size = sizeof(nfapi_nr_pusch_pdu_t);
  nfapi_nr_pusch_pdu_t *pusch_pdu = prepare_pusch_pdu(future_ul_tti_req,
                                                      UE,
                                                      scc,
                                                      &sched_pusch,
                                                      get_transformPrecoding(ul_bwp, NR_UL_DCI_FORMAT_0_0, 0),
                                                      0,
                                                      ra->msg3_round,
                                                      ul_bwp->pusch_Config && ul_bwp->pusch_Config->frequencyHopping,
                                                      UE->rnti,
                                                      &cell->beam_info);
  future_ul_tti_req->n_pdus += 1;

  // generation of DCI 0_0 to schedule msg3 retransmission
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_rel15 = cell->pdcch_pdu_idx[coresetid];
  if (!pdcch_pdu_rel15) {
    nfapi_nr_ul_dci_request_pdus_t *ul_dci_request_pdu = &ul_dci_req->ul_dci_pdu_list[ul_dci_req->numPdus];
    memset(ul_dci_request_pdu, 0, sizeof(nfapi_nr_ul_dci_request_pdus_t));
    ul_dci_request_pdu->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    ul_dci_request_pdu->PDUSize = (uint8_t)(2+sizeof(nfapi_nr_dl_tti_pdcch_pdu));
    pdcch_pdu_rel15 = &ul_dci_request_pdu->pdcch_pdu.pdcch_pdu_rel15;
    ul_dci_req->numPdus += 1;
    nr_configure_pdcch(pdcch_pdu_rel15, coreset, &UE->UE_sched_ctrl.sched_pdcch);
    cell->pdcch_pdu_idx[coresetid] = pdcch_pdu_rel15;
  }

  int aggregation_level;
  int CCEIndex = get_cce_index(cell, slot, 0,
                               &aggregation_level,
                               beam_dci.idx,
                               ss,
                               coreset,
                               &UE->UE_sched_ctrl.sched_pdcch,
                               0);
  if (CCEIndex < 0) {
    LOG_E(NR_MAC, "UE %04x cannot find free CCE!\n", UE->rnti);
    reset_beam_status(&cell->beam_info, sched.f, sched.s, UE->UE_beam_index, slots_frame, beam_ul.new_beam);
    reset_beam_status(&cell->beam_info, frame, slot, UE->UE_beam_index, slots_frame, beam_dci.new_beam);
    return;
  }

  // Fill PDCCH DL DCI PDU
  const uint16_t fapi_beam = convert_to_fapi_beam(UE->UE_beam_index, &cell->beam_info);
  nfapi_nr_dl_dci_pdu_t *dci_pdu = prepare_dci_pdu(pdcch_pdu_rel15,
                                                   scc,
                                                   ss,
                                                   coreset,
                                                   sched_pusch.ant_port_idx.spatialStreamIndices,
                                                   aggregation_level,
                                                   CCEIndex,
                                                   fapi_beam,
                                                   UE->rnti);
  pdcch_pdu_rel15->numDlDci++;

  dci_pdu_rel15_t uldci_payload = {0};
  config_uldci(sc_info,
               pusch_pdu,
               &uldci_payload,
               NULL,
               NULL,
               ra->Msg3_tda_id,
               ra->msg3_TPC,
               1, // Not toggling NDI in msg3 retransmissions
               ul_bwp,
               ss->searchSpaceType->present);

  // Reset TPC to 0 dB to not request new gain multiple times before computing new value for SNR
  ra->msg3_TPC = 1;

  fill_dci_pdu_rel15(sc_info,
                     &UE->current_DL_BWP,
                     ul_bwp,
                     dci_pdu,
                     &uldci_payload,
                     NR_UL_DCI_FORMAT_0_0,
                     TYPE_TC_RNTI_,
                     0,
                     ss,
                     coreset,
                     0, // parameter not needed for DCI 0_0
                     cell->cset0_bwp_size);

  // Mark the corresponding RBs as used
  fill_pdcch_vrb_map(cell,
                     &UE->UE_sched_ctrl.sched_pdcch,
                     CCEIndex,
                     aggregation_level,
                     beam_dci.idx);

  for (int rb = 0; rb < ra->msg3_nb_rb; rb++) {
    vrb_map_UL[rbStart + sched_pusch.bwp_info.bwpStart + rb] |= msg3_mask;
  }

  // Restart RA contention resolution timer in Msg3 retransmission slot (current slot + K2)
  // 3GPP TS 38.321 Section 5.1.5 Contention Resolution
  start_ra_contention_resolution_timer(
      ra,
      scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer,
      NTN_gNB_Koffset + tda_info.k2,
      ul_bwp->scs);

  // reset state to wait msg3
  ra->ra_state = nrRA_WAIT_Msg3;
  ra->Msg3_frame = sched.f;
  ra->Msg3_slot = sched.s;
}

static bool get_feasible_msg3_tda(const NR_ServingCellConfigCommon_t *scc,
                                  int mu_delta,
                                  const NR_PUSCH_TimeDomainResourceAllocationList_t *tda_list,
                                  int frame,
                                  int slot,
                                  NR_RA_t *ra,
                                  NR_beam_info_t *beam_info,
                                  int ue_beam_idx,
                                  const frame_structure_t *fs)
{
  DevAssert(tda_list != NULL);

  const int NTN_gNB_Koffset = get_NTN_Koffset(scc);

  int slots_per_frame = fs->numb_slots_frame;
  for (int i = 0; i < tda_list->list.count; i++) {
    // check if it is UL
     // msg3 slot according to 8.3 in 38.213
    fsn_t temp = get_fb_frame_slot(frame, slot, *tda_list->list.array[i]->k2 + mu_delta, slots_per_frame, NTN_gNB_Koffset);
    if (fs->frame_type == TDD && !is_ul_slot(temp.s, fs))
      continue;

    int s = get_slot_idx_in_period(temp.s, fs);
    const tdd_bitmap_t *bm = &fs->period_cfg.tdd_slot_bitmap[s];
    bool is_mixed = is_mixed_slot(s, fs);
    uint16_t slot_mask = is_mixed ? SL_to_bitmap(NR_SYMBOLS_PER_SLOT - bm->num_ul_symbols, bm->num_ul_symbols) : 0x3fff;
    long startSymbolAndLength = tda_list->list.array[i]->startSymbolAndLength;
    int start, nr;
    SLIV2SL(startSymbolAndLength, &start, &nr);
    uint16_t msg3_mask = SL_to_bitmap(start, nr);
    LOG_D(NR_MAC, "Check Msg3 TDA %d for slot %d: k2 %ld, S %d L %d\n", i, temp.s, *tda_list->list.array[i]->k2, start, nr);
    /* if this start and length of this TDA cannot be fulfilled, skip */
    if ((slot_mask & msg3_mask) != msg3_mask)
      continue;

    // check if it is possible to allocate MSG3 in a beam in this slot
    NR_beam_alloc_t beam = beam_allocation_procedure(beam_info, temp.f, temp.s, ue_beam_idx, slots_per_frame);
    if (beam.idx < 0)
      continue;
      
    // is in mixed slot with more or equal than 3 symbols, or UL slot
    ra->Msg3_frame = temp.f;
    ra->Msg3_slot = temp.s;
    ra->Msg3_tda_id = i;
    ra->Msg3_beam = beam;
    return true;
  }

  return false;
}

/////////////////////////////////////
//    Random Access Response PDU   //
//         TS 38.213 ch 8.2        //
//        TS 38.321 ch 6.2.3       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//| E | T |       R A P I D       |//
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |//
//| R |           T A             |//
//|       T A         |  UL grant |//
//|            UL grant           |//
//|            UL grant           |//
//|            UL grant           |//
//|         T C - R N T I         |//
//|         T C - R N T I         |//
/////////////////////////////////////
//       UL grant  (27 bits)       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//|-------------------|FHF|F_alloc|//
//|        Freq allocation        |//
//|    F_alloc    |Time allocation|//
//|      MCS      |     TPC   |CSI|//
/////////////////////////////////////
// WIP
// todo:
// - handle MAC RAR BI subheader
// - sending only 1 RAR subPDU
// - UL Grant: hardcoded CSI, TPC, time alloc
// - padding
static void nr_fill_rar(NR_UE_info_t *UE, uint8_t *dlsch_buffer, nfapi_nr_pusch_pdu_t *pusch_pdu)
{
  NR_RA_t *ra = UE->ra;
  LOG_D(NR_MAC,
        "[gNB] Generate RAR MAC PDU frame %d slot %d preamble index %u TA command %d \n",
        ra->Msg2_frame,
        ra->Msg2_slot,
        ra->preamble_index,
        ra->timing_offset);
  NR_RA_HEADER_BI *rarbi = (NR_RA_HEADER_BI *) dlsch_buffer;
  NR_RA_HEADER_RAPID *rarh = (NR_RA_HEADER_RAPID *) (dlsch_buffer + 1);
  NR_MAC_RAR *rar = (NR_MAC_RAR *) (dlsch_buffer + 2);
  unsigned char csi_req = 0;

  /// E/T/R/R/BI subheader ///
  // E = 1, MAC PDU includes another MAC sub-PDU (RAPID)
  // T = 0, Back-off indicator subheader
  // R = 2, Reserved
  // BI = 0, 5ms
  rarbi->E = 1;
  rarbi->T = 0;
  rarbi->R = 0;
  rarbi->BI = 0;

  /// E/T/RAPID subheader ///
  // E = 0, one only RAR, first and last
  // T = 1, RAPID
  rarh->E = 0;
  rarh->T = 1;
  rarh->RAPID = ra->preamble_index;

  /// RAR MAC payload ///
  rar->R = 0;

  // TA command
  rar->TA1 = (uint8_t) (ra->timing_offset >> 5);    // 7 MSBs of timing advance
  rar->TA2 = (uint8_t) (ra->timing_offset & 0x1f);  // 5 LSBs of timing advance
  const int rar_ta = rar->TA2 + (rar->TA1 << 5);

  // TC-RNTI
  rar->TCRNTI_1 = (uint8_t) (UE->rnti >> 8);        // 8 MSBs of rnti
  rar->TCRNTI_2 = (uint8_t) (UE->rnti & 0xff);      // 8 LSBs of rnti

  // UL grant

  if (pusch_pdu->frequency_hopping)
    AssertFatal(1==0,"PUSCH with frequency hopping currently not supported");

  int bwp_size = pusch_pdu->bwp_size;
  int prb_alloc = PRBalloc_to_locationandbandwidth0(ra->msg3_nb_rb, ra->msg3_first_rb, bwp_size);
  int valid_bits = 14;
  int f_alloc = prb_alloc & ((1 << valid_bits) - 1);

  uint32_t ul_grant = csi_req | (ra->msg3_TPC << 1) | (pusch_pdu->mcs_index << 4) | (ra->Msg3_tda_id << 8) | (f_alloc << 12) | (pusch_pdu->frequency_hopping << 26);

  rar->UL_GRANT_1 = (uint8_t) (ul_grant >> 24) & 0x07;
  rar->UL_GRANT_2 = (uint8_t) (ul_grant >> 16) & 0xff;
  rar->UL_GRANT_3 = (uint8_t) (ul_grant >> 8) & 0xff;
  rar->UL_GRANT_4 = (uint8_t) ul_grant & 0xff;

#ifdef DEBUG_RAR
  LOG_I(NR_MAC, "rarh->E = 0x%x\n", rarh->E);
  LOG_I(NR_MAC, "rarh->T = 0x%x\n", rarh->T);
  LOG_I(NR_MAC, "rarh->RAPID = 0x%x (%i)\n", rarh->RAPID, rarh->RAPID);
  LOG_I(NR_MAC, "rar->R = 0x%x\n", rar->R);
  LOG_I(NR_MAC, "rar->TA1 = 0x%x\n", rar->TA1);
  LOG_I(NR_MAC, "rar->TA2 = 0x%x\n", rar->TA2);
  LOG_I(NR_MAC, "rar->UL_GRANT_1 = 0x%x\n", rar->UL_GRANT_1);
  LOG_I(NR_MAC, "rar->UL_GRANT_2 = 0x%x\n", rar->UL_GRANT_2);
  LOG_I(NR_MAC, "rar->UL_GRANT_3 = 0x%x\n", rar->UL_GRANT_3);
  LOG_I(NR_MAC, "rar->UL_GRANT_4 = 0x%x\n", rar->UL_GRANT_4);
  LOG_I(NR_MAC, "rar->TCRNTI_1 = 0x%x\n", rar->TCRNTI_1);
  LOG_I(NR_MAC, "rar->TCRNTI_2 = 0x%x\n", rar->TCRNTI_2);
#endif
  LOG_D(NR_MAC,
        "In %s: %d.%d transmitted RAR for RAPID %u with t_alloc %d f_alloc %d ta_command %d mcs %d freq_hopping %d "
        "tpc_command %d csi_req %d t_crnti %x\n",
        __FUNCTION__,
        ra->Msg2_frame,
        ra->Msg2_slot,
        ra->preamble_index,
        rar->UL_GRANT_3 & 0x0f,
        (rar->UL_GRANT_3 >> 4) | (rar->UL_GRANT_2 << 4) | ((rar->UL_GRANT_1 & 0x03) << 12),
        rar_ta,
        rar->UL_GRANT_4 >> 4,
        rar->UL_GRANT_1 >> 2,
        ra->msg3_TPC,
        csi_req,
        rar->TCRNTI_2 + (rar->TCRNTI_1 << 8));

  // resetting msg3 TPC to 0dB for possible retransmissions
  ra->msg3_TPC = 1;
}

static bool nr_get_Msg3alloc(nr_cell_sched_t *cell, int current_slot, frame_t current_frame, NR_UE_info_t *UE)
{
  NR_RA_t *ra = UE->ra;
  DevAssert(ra->Msg3_tda_id >= 0 && ra->Msg3_tda_id < 16);

  uint16_t msg3_nb_rb = max(8, cell->min_grant_prb); // sdu has 6 or 8 bytes

  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &UE->sc_info;

  const NR_PUSCH_TimeDomainResourceAllocationList_t *pusch_TimeDomainAllocationList = ul_bwp->tdaList_Common;

  int startSymbolAndLength = pusch_TimeDomainAllocationList->list.array[ra->Msg3_tda_id]->startSymbolAndLength;
  SLIV2SL(startSymbolAndLength, &ra->msg3_startsymb, &ra->msg3_nbSymb);
  const uint16_t msg3_mask = SL_to_bitmap(ra->msg3_startsymb, ra->msg3_nbSymb);

  const int buffer_index = ul_buffer_index(ra->Msg3_frame,
                                           ra->Msg3_slot,
                                           cell->frame_structure.numb_slots_frame,
                                           cell->vrb_map_UL_size);
  uint16_t *vrb_map_UL = &cell->common_channels.vrb_map_UL[ra->Msg3_beam.idx][buffer_index * MAX_BWP_SIZE];

  int bwpSize = sc_info->initial_ul_BWPSize;
  int bwpStart = sc_info->initial_ul_BWPStart;
  if (bwpSize != ul_bwp->BWPSize || bwpStart != ul_bwp->BWPStart) {
    int act_bwp_start = ul_bwp->BWPStart;
    int act_bwp_size  = ul_bwp->BWPSize;
    if (!((bwpStart >= act_bwp_start) && ((bwpStart+bwpSize) <= (act_bwp_start+act_bwp_size))))
      bwpStart = act_bwp_start;
  }

  /* search msg3_nb_rb free RBs */
  int rbSize = 0;
  int rbStart = 0;
  if (!get_rb_alloc(msg3_nb_rb, msg3_nb_rb, bwpStart, bwpSize, vrb_map_UL, msg3_mask, &rbStart, &rbSize)) {
    LOG_D(NR_MAC, "No space to allocate Msg 3\n");
    return false;
  }
  ra->msg3_nb_rb = msg3_nb_rb;
  ra->msg3_first_rb = rbStart;
  ra->msg3_bwp_start = bwpStart;
  LOG_I(NR_MAC,
        "UE %04x: Msg3 scheduled at %d.%d (%d.%d TDA %u) start %d RBs %d\n",
        UE->rnti,
        ra->Msg3_frame,
        ra->Msg3_slot,
        current_frame,
        current_slot,
        ra->Msg3_tda_id,
        ra->msg3_first_rb,
        ra->msg3_nb_rb);
  return true;
}

static void
nr_add_msg3(nr_cell_sched_t *cell, frame_t frameP, slot_t slotP, NR_UE_info_t *UE, uint8_t *RAR_pdu, int beam_number)
{
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &UE->sc_info;
  NR_RA_t *ra = UE->ra;
  if (ra->ra_state == nrRA_gNB_IDLE) {
    LOG_W(NR_MAC,"RA is not active for RA %X. skipping msg3 scheduling\n", UE->rnti);
    return;
  }

  const uint16_t mask = SL_to_bitmap(ra->msg3_startsymb, ra->msg3_nbSymb);
  int slots_frame = cell->frame_structure.numb_slots_frame;
  int buffer_index = ul_buffer_index(ra->Msg3_frame, ra->Msg3_slot, slots_frame, cell->vrb_map_UL_size);
  uint16_t *vrb_map_UL = &cc->vrb_map_UL[ra->Msg3_beam.idx][buffer_index * MAX_BWP_SIZE];
  for (int i = 0; i < ra->msg3_nb_rb; ++i) {
    AssertFatal(!(vrb_map_UL[i + ra->msg3_first_rb + ra->msg3_bwp_start] & mask),
                "RB %d in %4d.%2d is already taken, cannot allocate Msg3!\n",
                i + ra->msg3_first_rb,
                ra->Msg3_frame,
                ra->Msg3_slot);
    vrb_map_UL[i + ra->msg3_first_rb + ra->msg3_bwp_start] |= mask;
  }

  LOG_D(NR_MAC, "UE %04x: %d.%d RA is active, Msg3 in (%d,%d)\n", UE->rnti, frameP, slotP, ra->Msg3_frame, ra->Msg3_slot);
  buffer_index = ul_buffer_index(ra->Msg3_frame, ra->Msg3_slot, slots_frame, cell->UL_tti_req_ahead_size);
  nfapi_nr_ul_tti_request_t *future_ul_tti_req = &cell->UL_tti_req_ahead[buffer_index];
  AssertFatal(future_ul_tti_req->SFN == ra->Msg3_frame
              && future_ul_tti_req->Slot == ra->Msg3_slot,
              "future UL_tti_req's frame.slot %d.%d does not match PUSCH %d.%d\n",
              future_ul_tti_req->SFN,
              future_ul_tti_req->Slot,
              ra->Msg3_frame,
              ra->Msg3_slot);
  future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
  future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pdu_size = sizeof(nfapi_nr_pusch_pdu_t);

  NR_tda_info_t tda_info = get_ul_tda_info(ul_bwp, 0, NR_SearchSpace__searchSpaceType_PR_common, TYPE_TC_RNTI_, ra->Msg3_tda_id);
  NR_pusch_dmrs_t dmrs_info = get_ul_dmrs_params(scc, ul_bwp, &tda_info, 1, 0, 0);
  int num_dmrs_symb = count_bits64_with_mask(dmrs_info.ul_dmrs_symb_pos, tda_info.startSymbolIndex, tda_info.nrOfSymbols);
  int TBS = 0, mcsindex = 0, R = 0, Qm = 0;
  while(TBS < 7) {  // TBS for msg3 is 7 bytes (except for RRCResumeRequest1 currently not implemented)
    mcsindex++;
    AssertFatal(mcsindex <= 28, "Exceeding MCS limit for Msg3\n");
    R = nr_get_code_rate_ul(mcsindex, ul_bwp->mcs_table);
    Qm = nr_get_Qm_ul(mcsindex, ul_bwp->mcs_table);
    TBS = nr_compute_tbs(Qm,
                         R,
                         ra->msg3_nb_rb,
                         tda_info.nrOfSymbols,
                         num_dmrs_symb * 12, // nb dmrs set for no data in dmrs symbol
                         0, //nb_rb_oh
                         0, // to verify tb scaling
                         1) >> 3;
  }

  NR_sched_pusch_t sched_pusch = {.bwp_info.bwpSize = sc_info->initial_ul_BWPSize,
                                  .bwp_info.bwpStart = ra->msg3_bwp_start,
                                  .rbSize = ra->msg3_nb_rb,
                                  .rbStart = ra->msg3_first_rb,
                                  .tb_size = TBS,
                                  .R = R,
                                  .Qm = Qm,
                                  .mcs = mcsindex,
                                  .nrOfLayers = 1,
                                  .tda_info = tda_info,
                                  .dmrs_info = dmrs_info,
                                  .ant_port_idx = {.numSpatialStreamIndices = cell->radio_config.pusch_AntennaPorts}};
  const int start_stream_idx = beam_number * cell->radio_config.pusch_AntennaPorts;
  for (int i = 0; i < sched_pusch.ant_port_idx.numSpatialStreamIndices; i++)
    sched_pusch.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[start_stream_idx + i];

  LOG_D(NR_MAC,
        "UE %04x: %d.%d Adding Msg3 UL Config Request for (%d,%d) : (%d,%d,%d)\n",
        UE->rnti,
        frameP,
        slotP,
        ra->Msg3_frame,
        ra->Msg3_slot,
        ra->msg3_nb_rb,
        ra->msg3_first_rb,
        ra->msg3_round);

  nfapi_nr_pusch_pdu_t *pusch_pdu = prepare_pusch_pdu(future_ul_tti_req,
                                                      UE,
                                                      scc,
                                                      &sched_pusch,
                                                      get_transformPrecoding(ul_bwp, NR_UL_DCI_FORMAT_0_0, 0),
                                                      0,
                                                      0,
                                                      ul_bwp->pusch_Config && ul_bwp->pusch_Config->frequencyHopping,
                                                      UE->rnti,
                                                      &cell->beam_info);
  future_ul_tti_req->n_pdus += 1;

  // calling function to fill rar message
  nr_fill_rar(UE, RAR_pdu, pusch_pdu);
}

static bool check_msg2_monitoring(const NR_SearchSpace_t *ss, int slots_per_frame, int current_frame, int current_slot)
{
  // check if the slot is not among the PDCCH monitored ones (38.213 10.1)
  int monitoring_slot_period, monitoring_offset;
  get_monitoring_period_offset(ss, &monitoring_slot_period, &monitoring_offset);
  if (!is_periodic_occasion(current_frame, current_slot, slots_per_frame, monitoring_slot_period, monitoring_offset))
    return false;
  return true;
}

static int get_response_window(e_NR_RACH_ConfigGeneric__ra_ResponseWindow response_window)
{
  int slots;
  switch (response_window) {
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl1:
      slots = 1;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl2:
      slots = 2;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl4:
      slots = 4;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl8:
      slots = 8;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl10:
      slots = 10;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl20:
      slots = 20;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl40:
      slots = 40;
      break;
    case NR_RACH_ConfigGeneric__ra_ResponseWindow_sl80:
      slots = 80;
      break;
    default:
      AssertFatal(false, "Invalid response window value %d\n", response_window);
  }
  return slots;
}

static bool msg2_in_response_window(int rach_frame,
                                    int rach_slot,
                                    int n_slots_frame,
                                    long rrc_ra_ResponseWindow,
                                    int current_frame,
                                    int current_slot)
{
  int window_slots = get_response_window(rrc_ra_ResponseWindow);
  if (window_slots > n_slots_frame)
    LOG_E(NR_MAC, "RA-ResponseWindow need to be configured to a value lower than or equal to 10 ms\n");
  int abs_rach = n_slots_frame * rach_frame + rach_slot;
  int abs_now = n_slots_frame * current_frame + current_slot;
  int diff = (n_slots_frame * 1024 + abs_now - abs_rach) % (n_slots_frame * 1024);

  bool in_window = diff <= window_slots;
  if (!in_window) {
    LOG_W(NR_MAC,
          "exceeded RA window: preamble at %d.%2d now %d.%d (diff %d), ra_ResponseWindow %ld/%d slots\n",
          rach_frame,
          rach_slot,
          current_frame,
          current_slot,
          diff,
          rrc_ra_ResponseWindow,
          window_slots);
  }
  return in_window;
}

static void prepare_dl_pdus(nr_cell_sched_t *cell,
                            NR_UE_info_t *UE,
                            NR_sched_pdsch_t *sched_pdsch,
                            nfapi_nr_dl_tti_request_body_t *dl_req,
                            NR_sched_pucch_t *pucch,
                            nr_rnti_type_t rnti_type,
                            int aggregation_level,
                            int CCEIndex,
                            int current_harq_pid,
                            int rnti,
                            int round,
                            int tb_scaling,
                            int pduindex)
{
  // look up the PDCCH PDU for this CC, BWP, and CORESET. If it does not exist, create it. This is especially
  // important if we have multiple RAs, and the DLSCH has to reuse them, so we need to mark them
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_ControlResourceSet_t *coreset = sched_ctrl->coreset;
  const int coresetid = coreset->controlResourceSetId;
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_rel15 = cell->pdcch_pdu_idx[coresetid];
  if (!pdcch_pdu_rel15) {
    nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdcch_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
    memset(dl_tti_pdcch_pdu, 0, sizeof(nfapi_nr_dl_tti_request_pdu_t));
    dl_tti_pdcch_pdu->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    dl_tti_pdcch_pdu->PDUSize = (uint8_t)(2 + sizeof(nfapi_nr_dl_tti_pdcch_pdu));
    dl_req->nPDUs += 1;
    pdcch_pdu_rel15 = &dl_tti_pdcch_pdu->pdcch_pdu.pdcch_pdu_rel15;
    nr_configure_pdcch(pdcch_pdu_rel15, coreset, &sched_ctrl->sched_pdcch);
    cell->pdcch_pdu_idx[coresetid] = pdcch_pdu_rel15;
  }
  AssertFatal(
      pdcch_pdu_rel15->StartSymbolIndex + pdcch_pdu_rel15->DurationSymbols <= sched_pdsch->tda_info.startSymbolIndex,
      "'initialDLBWPcontrolResourceSetZero' value in configuration file is not supported for the current Bandwidth! PDSCH is "
      "overlapping CORESET! Please check 3GPP TS 38.213 Section 13 and choose an index with less number of symbols for CORESET\n");

  nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdsch_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
  memset((void *)dl_tti_pdsch_pdu, 0, sizeof(nfapi_nr_dl_tti_request_pdu_t));
  dl_tti_pdsch_pdu->PDUType = NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE;
  dl_tti_pdsch_pdu->PDUSize = (uint8_t)(2 + sizeof(nfapi_nr_dl_tti_pdsch_pdu));
  dl_req->nPDUs += 1;

  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;
  const uint16_t fapi_beam = convert_to_fapi_beam(UE->UE_beam_index, &cell->beam_info);
  nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdsch_pdu_rel15 = prepare_pdsch_pdu(dl_tti_pdsch_pdu,
                                                                         cell,
                                                                         UE,
                                                                         sched_pdsch,
                                                                         dl_bwp->pdsch_Config,
                                                                         false,
                                                                         round,
                                                                         rnti,
                                                                         fapi_beam,
                                                                         1,
                                                                         pduindex);

  /* Fill PDCCH DL DCI PDU */
  nfapi_nr_dl_dci_pdu_t *dci_pdu = prepare_dci_pdu(pdcch_pdu_rel15,
                                                   scc,
                                                   sched_ctrl->search_space,
                                                   coreset,
                                                   sched_pdsch->ant_port_idx.spatialStreamIndices,
                                                   aggregation_level,
                                                   CCEIndex,
                                                   fapi_beam,
                                                   rnti);
  pdcch_pdu_rel15->numDlDci++;

  int tpc = 1; // 0dB change, don't know how to determine this.
  dci_pdu_rel15_t dci_payload = prepare_dci_dl_payload(cell,
                                                       UE,
                                                       rnti_type,
                                                       sched_ctrl->search_space->searchSpaceType->present,
                                                       pdsch_pdu_rel15,
                                                       sched_pdsch,
                                                       pucch,
                                                       tpc,
                                                       current_harq_pid,
                                                       tb_scaling,
                                                       false);

  LOG_D(NR_MAC,
        "DCI 1_0 payload: freq_alloc %d (%d,%d,%d), time_alloc %d, vrb to prb %d, mcs %d tb_scaling %d pucchres %d harqtiming %d\n",
        dci_payload.frequency_domain_assignment.val,
        pdsch_pdu_rel15->rbStart,
        pdsch_pdu_rel15->rbSize,
        pdsch_pdu_rel15->BWPSize,
        dci_payload.time_domain_assignment.val,
        dci_payload.vrb_to_prb_mapping.val,
        dci_payload.mcs,
        dci_payload.tb_scaling,
        dci_payload.pucch_resource_indicator,
        dci_payload.pdsch_to_harq_feedback_timing_indicator.val);

  LOG_D(NR_MAC,
        "DCI params: rnti 0x%x, rnti_type %d, dci_format %d coreset params: FreqDomainResource %llx, start_symbol %d  "
        "n_symb %d, BWPsize %d\n",
        dci_pdu->RNTI,
        rnti_type,
        NR_DL_DCI_FORMAT_1_0,
        (unsigned long long)pdcch_pdu_rel15->FreqDomainResource,
        pdcch_pdu_rel15->StartSymbolIndex,
        pdcch_pdu_rel15->DurationSymbols,
        pdsch_pdu_rel15->BWPSize);

  fill_dci_pdu_rel15(&UE->sc_info,
                     dl_bwp,
                     &UE->current_UL_BWP,
                     dci_pdu,
                     &dci_payload,
                     NR_DL_DCI_FORMAT_1_0,
                     rnti_type,
                     0,
                     sched_ctrl->search_space,
                     coreset,
                     0, // parameter not needed for DCI 1_0
                     cell->cset0_bwp_size);

  LOG_D(NR_MAC, "BWPSize: %i\n", pdcch_pdu_rel15->BWPSize);
  LOG_D(NR_MAC, "BWPStart: %i\n", pdcch_pdu_rel15->BWPStart);
  LOG_D(NR_MAC, "SubcarrierSpacing: %i\n", pdcch_pdu_rel15->SubcarrierSpacing);
  LOG_D(NR_MAC, "CyclicPrefix: %i\n", pdcch_pdu_rel15->CyclicPrefix);
  LOG_D(NR_MAC, "StartSymbolIndex: %i\n", pdcch_pdu_rel15->StartSymbolIndex);
  LOG_D(NR_MAC, "DurationSymbols: %i\n", pdcch_pdu_rel15->DurationSymbols);
  for (int n = 0; n < 6; n++)
    LOG_D(NR_MAC, "FreqDomainResource[%i]: %x\n", n, pdcch_pdu_rel15->FreqDomainResource[n]);
  LOG_D(NR_MAC, "CceRegMappingType: %i\n", pdcch_pdu_rel15->CceRegMappingType);
  LOG_D(NR_MAC, "RegBundleSize: %i\n", pdcch_pdu_rel15->RegBundleSize);
  LOG_D(NR_MAC, "InterleaverSize: %i\n", pdcch_pdu_rel15->InterleaverSize);
  LOG_D(NR_MAC, "CoreSetType: %i\n", pdcch_pdu_rel15->CoreSetType);
  LOG_D(NR_MAC, "ShiftIndex: %i\n", pdcch_pdu_rel15->ShiftIndex);
  LOG_D(NR_MAC, "precoderGranularity: %i\n", pdcch_pdu_rel15->precoderGranularity);
  LOG_D(NR_MAC, "numDlDci: %i\n", pdcch_pdu_rel15->numDlDci);
}

// returns true if the UE must be released by the caller
static bool nr_generate_Msg2(gNB_MAC_INST *nr_mac,
                             nr_cell_sched_t *cell,
                             frame_t frameP,
                             slot_t slotP,
                             NR_UE_info_t *UE,
                             nfapi_nr_dl_tti_request_t *DL_req,
                             nfapi_nr_tx_data_request_t *TX_req)
{
  // no DL -> cannot send Msg2
  if (!is_dl_slot(slotP, &cell->frame_structure)) {
    return false;
  }

  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_RA_t *ra = UE->ra;
  long rrc_ra_ResponseWindow =
      scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.ra_ResponseWindow;
  const int n_slots_frame = cell->frame_structure.numb_slots_frame;
  if (!msg2_in_response_window(ra->preamble_frame, ra->preamble_slot, n_slots_frame, rrc_ra_ResponseWindow, frameP, slotP)) {
    LOG_E(NR_MAC,
          "sfn: %d.%d UE RA-RNTI %04x TC-RNTI %04x: exceeded RA window, cannot schedule Msg2\n",
          frameP,
          slotP,
          ra->RA_rnti,
          UE->rnti);
    return true;
  }

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_SearchSpace_t *ss = sched_ctrl->search_space;
  if (!check_msg2_monitoring(ss, n_slots_frame, frameP, slotP)) {
    LOG_E(NR_MAC, "UE RA-RNTI %04x TC-RNTI %04x: Msg2 not monitored by UE\n", ra->RA_rnti, UE->rnti);
    return false;
  }
  NR_beam_alloc_t beam = beam_allocation_procedure(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame);
  if (beam.idx < 0)
    return false;

  const NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  // check the feasibility of Msg3, the actual Msg3 allocation
  // is further below. In the case of CFRA, we don't need Msg3, but 38.321
  // §5.1.4 does not clearly exclude Msg3, and UL TA might still be useful.
  // Before the change in this commit, we used CFRA but required Msg3, which
  // COTS UE would often (but not always) send.
  bool ret = get_feasible_msg3_tda(scc,
                                   get_delta_for_k2(ul_bwp->scs),
                                   ul_bwp->tdaList_Common,
                                   frameP,
                                   slotP,
                                   ra,
                                   &cell->beam_info,
                                   UE->UE_beam_index,
                                   &cell->frame_structure);
  if (!ret || ra->Msg3_tda_id > 15) {
    LOG_D(NR_MAC, "UE RNTI %04x %d.%d: infeasible Msg3 TDA\n", UE->rnti, frameP, slotP);
    reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
    return false;
  }

  NR_ControlResourceSet_t *coreset = sched_ctrl->coreset;
  AssertFatal(coreset, "Coreset cannot be null for RA-Msg2\n");
  const int coresetid = coreset->controlResourceSetId;
  // Calculate number of symbols
  int time_domain_assignment = get_dl_tda(nr_mac, cell, slotP);
  int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
  NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config = &cell->type0_PDCCH_CSS_config[ssb_index];
  int mux_pattern = type0_PDCCH_CSS_config ? type0_PDCCH_CSS_config->type0_pdcch_ss_mux_pattern : 1;
  NR_tda_info_t tda_info = get_dl_tda_info(dl_bwp,
                                           ss->searchSpaceType->present,
                                           time_domain_assignment,
                                           scc->dmrs_TypeA_Position,
                                           mux_pattern,
                                           TYPE_RA_RNTI_,
                                           coresetid,
                                           false);
  if (!tda_info.valid_tda)
    return false;

  int rbStart = 0;
  int rbSize = 0;
  uint16_t msg2_nb_rb = max(8, cell->min_grant_prb); // RAR TBS is 9 bytes
  const uint16_t msg2_mask = SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);
  bwp_info_t bwp_info = get_pdsch_bwp_start_size(cell, UE);
  uint16_t *vrb_map = cc->vrb_map[beam.idx];
  if (!get_rb_alloc(msg2_nb_rb, msg2_nb_rb, bwp_info.bwpStart, bwp_info.bwpSize, vrb_map, msg2_mask, &rbStart, &rbSize)) {
    LOG_W(NR_MAC, "Cannot find free vrb_map for RA RNTI %04x!\n", ra->RA_rnti);
    reset_beam_status(&cell->beam_info, ra->Msg3_frame, ra->Msg3_slot, UE->UE_beam_index, n_slots_frame, ra->Msg3_beam.new_beam);
    reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
    return false;
  }

  // Checking if the DCI allocation is feasible in current subframe
  nfapi_nr_dl_tti_request_body_t *dl_req = &DL_req->dl_tti_request_body;
  if (dl_req->nPDUs > NFAPI_NR_MAX_DL_TTI_PDUS - 2) {
    LOG_W(NR_MAC, "UE %04x: %d.%d FAPI DL structure is full\n", UE->rnti, frameP, slotP);
    reset_beam_status(&cell->beam_info, ra->Msg3_frame, ra->Msg3_slot, UE->UE_beam_index, n_slots_frame, ra->Msg3_beam.new_beam);
    reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
    return false;
  }

  int aggregation_level;
  int CCEIndex = get_cce_index(cell, slotP, 0, &aggregation_level, beam.idx, ss, coreset, &sched_ctrl->sched_pdcch, 0);

  if (CCEIndex < 0) {
    LOG_W(NR_MAC, "UE %04x: %d.%d cannot find free CCE for Msg2!\n", UE->rnti, frameP, slotP);
    reset_beam_status(&cell->beam_info, ra->Msg3_frame, ra->Msg3_slot, UE->UE_beam_index, n_slots_frame, ra->Msg3_beam.new_beam);
    reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
    return false;
  }

  // get an actual Msg3 allocation in CBRA
  bool msg3_ret = nr_get_Msg3alloc(cell, slotP, frameP, UE);
  if (!msg3_ret) {
    reset_beam_status(&cell->beam_info, ra->Msg3_frame, ra->Msg3_slot, UE->UE_beam_index, n_slots_frame, ra->Msg3_beam.new_beam);
    reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
    return false;
  }

  LOG_D(NR_MAC, "Msg2 startSymbolIndex.nrOfSymbols %d.%d\n", tda_info.startSymbolIndex, tda_info.nrOfSymbols);

  // Distance calculation according to SCF222.10.02 RACH.indication (table 3-74) and 38.213 4.2/38.211 4.3.1
  // T_c according to 38.211 4.1
  float T_c_ns = 0.509;
  int numerology = ul_bwp->scs;
  float ta_unit_ns = T_c_ns * 16 * 64 / (1 << numerology);
  float rtt_ns = ta_unit_ns * ra->timing_offset;
  float distance_in_meters = (float) SPEED_OF_LIGHT * rtt_ns / 1000 / 1000 / 1000 / 2;
  LOG_A(NR_MAC,
        "UE %04x: %d.%d Generating RA-Msg2 DCI, RA RNTI 0x%x, state %d, preamble_index(RAPID) %d, "
        "timing_offset = %d (estimated distance %.1f [m])\n",
        UE->rnti,
        frameP,
        slotP,
        ra->RA_rnti,
        ra->ra_state,
        ra->preamble_index,
        ra->timing_offset,
        distance_in_meters);

  // SCF222: PDU index incremented for each PDSCH PDU sent in TX control message. This is used to associate control
  // information to data and is reset every slot.
  const int pduindex = cell->pdu_index++;
  uint8_t mcsTableIdx = dl_bwp->mcsTableIdx;

  NR_pdsch_dmrs_t dmrs_parms = get_dl_dmrs_params(scc, dl_bwp, &tda_info, 1);

  int mcsIndex = -1; // initialization value
  uint8_t tb_scaling = 0;
  int R, Qm;
  uint32_t TBS = 0;
  while (TBS < 9) { // min TBS for RAR is 9 bytes
    mcsIndex++;
    R = nr_get_code_rate_dl(mcsIndex, mcsTableIdx);
    Qm = nr_get_Qm_dl(mcsIndex, mcsTableIdx);
    TBS = nr_compute_tbs(Qm,
                         R,
                         rbSize,
                         tda_info.nrOfSymbols,
                         dmrs_parms.N_PRB_DMRS * dmrs_parms.N_DMRS_SLOT,
                         0, // overhead
                         tb_scaling, // tb scaling
                         1)
          >> 3; // layers
  }

  const nr_pdsch_AntennaPorts_t *p = &cell->radio_config.pdsch_AntennaPorts;
  const uint16_t num_log_ports_beam = p->N1 * p->N2 * p->XP;
  NR_sched_pdsch_t sched_pdsch = {
      .R = R,
      .Qm = Qm,
      .mcs = mcsIndex,
      .time_domain_allocation = time_domain_assignment,
      .nrOfLayers = 1,
      .dmrs_parms = dmrs_parms,
      .tda_info = tda_info,
      .pm_index = 0,
      .tb_size = TBS,
      .bwp_info = bwp_info,
      .alloc_type = PDSCH_TYPE1,
      .rbStart = rbStart,
      .rbSize = rbSize,
      // no of layers 1 and pmi 0 gives only one log antenna port
      .ant_port_idx = {.numSpatialStreamIndices = 1,
                       .spatialStreamIndices[0] = cell->radio_config.spatial_stream_index[beam.idx * num_log_ports_beam]}};

  prepare_dl_pdus(cell,
                  UE,
                  &sched_pdsch,
                  dl_req,
                  NULL,
                  TYPE_RA_RNTI_,
                  aggregation_level,
                  CCEIndex,
                  0,
                  ra->RA_rnti,
                  0,
                  tb_scaling,
                  pduindex);

  // DL TX request
  nfapi_nr_pdu_t *tx_req = &TX_req->pdu_list[TX_req->Number_of_PDUs];

  // If CFRA: 38.321 §5.1.4 does not clearly say (to me) if UL grant should be
  // dropped or not, and COTS UE would often send Msg3 if we configured CFRA
  // but required Msg3. Also, "if RAR includes a MAC subPDU with RAPID: [...]
  // indicate the reception of an acknowledgement for SI request to upper
  // layers." which is not the case here.
  nr_add_msg3(cell, frameP, slotP, UE, (uint8_t *)&tx_req->TLVs[0].value.direct[0], beam.idx);

  if (!ra->cfra) {
    LOG_D(NR_MAC,
          "UE %04x: %d.%d: Setting RA-Msg3 reception for SFN.Slot %d.%d\n",
          UE->rnti,
          frameP,
          slotP,
          ra->Msg3_frame,
          ra->Msg3_slot);

    // Start RA contention resolution timer in Msg3 transmission slot (current slot + K2)
    // 3GPP TS 38.321 Section 5.1.5 Contention Resolution
    start_ra_contention_resolution_timer(
        ra,
        scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer,
        *ul_bwp->tdaList_Common->list.array[ra->Msg3_tda_id]->k2 + get_NTN_Koffset(scc),
        ul_bwp->scs);

  }

  tx_req->PDU_index = pduindex;
  tx_req->num_TLV = 1;
  tx_req->TLVs[0].tag = 0;
  tx_req->TLVs[0].length = TBS;
  tx_req->PDU_length = compute_PDU_length(tx_req->num_TLV, TBS);
  TX_req->SFN = frameP;
  TX_req->Number_of_PDUs++;
  TX_req->Slot = slotP;

  T(T_GNB_MAC_DL_RAR_PDU_WITH_DATA,
    T_INT(nr_mac->Mod_id),
    T_INT(cell->nr_cellid),
    T_INT(ra->RA_rnti),
    T_INT(frameP),
    T_INT(slotP),
    T_INT(0),
    T_BUFFER(&tx_req->TLVs[0].value.direct[0], tx_req->TLVs[0].length));

  // Mark the corresponding symbols RBs as used
  fill_pdcch_vrb_map(cell, &sched_ctrl->sched_pdcch, CCEIndex, aggregation_level, beam.idx);
  for (int rb = 0; rb < rbSize; rb++) {
    vrb_map[bwp_info.bwpStart + rb + rbStart] |= msg2_mask;
  }

  // In CFRA: in Msg3 handling, will unconditionally mark succeeded
  ra->ra_state = nrRA_WAIT_Msg3;
  return false;
}

static void nr_generate_Msg4_MsgB(gNB_MAC_INST *nr_mac,
                                  nr_cell_sched_t *cell,
                                  frame_t frameP,
                                  slot_t slotP,
                                  NR_UE_info_t *UE,
                                  nfapi_nr_dl_tti_request_t *DL_req,
                                  nfapi_nr_tx_data_request_t *TX_req)
{
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;

  // if it is a DL slot, if the RA is in MSG4 state
  if (is_dl_slot(slotP, &cell->frame_structure)) {
    NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    NR_SearchSpace_t *ss = sched_ctrl->search_space;
    NR_RA_t *ra = UE->ra;
    DevAssert(!ra->cfra);
    const char *ra_type_str = ra->ra_type == RA_2_STEP ? "MsgB" : "Msg4";
    NR_ControlResourceSet_t *coreset = sched_ctrl->coreset;
    AssertFatal(coreset != NULL, "Coreset cannot be null for RA %s\n", ra_type_str);

    uint16_t mac_sdu_length = 0;
    /* get the PID of a HARQ process awaiting retrnasmission, or -1 otherwise */
    int current_harq_pid = sched_ctrl->retrans_dl_harq.head;

    logical_chan_id_t lcid = 0;
    if (current_harq_pid < 0) {
      const logical_chan_id_t lcid_l[2] = {DL_SCH_LCID_CCCH, DL_SCH_LCID_DCCH};
      mac_rlc_status_resp_t stat[2];
      nr_mac_rlc_status_ind(UE->rnti, frameP, 2, lcid_l, stat);

      if (stat[0].bytes_in_buffer > 0) {
        // some data in SRB0 (likely RRCSetup)
        mac_sdu_length = stat[0].bytes_in_buffer;
        lcid = lcid_l[0];
      } else if (stat[1].bytes_in_buffer > 0) {
        // some data in SRB1 (likely RRCReestablishmnt/RRCReconfiguration)
        mac_sdu_length = stat[1].bytes_in_buffer;
        lcid = lcid_l[1];
      } else {
        return; // no data to forward
      }
    }

    const int n_slots_frame = cell->frame_structure.numb_slots_frame;
    NR_beam_alloc_t beam = beam_allocation_procedure(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame);
    if (beam.idx < 0)
      return;

    // get CCEindex, needed also for PUCCH and then later for PDCCH
    int aggregation_level;
    int CCEIndex = get_cce_index(cell, slotP, 0,
                                 &aggregation_level,
                                 beam.idx,
                                 ss,
                                 coreset,
                                 &sched_ctrl->sched_pdcch,
                                 0);

    if (CCEIndex < 0) {
      LOG_E(NR_MAC, "Cannot find free CCE for RA RNTI 0x%04x!\n", UE->rnti);
      reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
      return;
    }

    // Checking if the DCI allocation is feasible in current subframe
    nfapi_nr_dl_tti_request_body_t *dl_req = &DL_req->dl_tti_request_body;
    if (dl_req->nPDUs > NFAPI_NR_MAX_DL_TTI_PDUS - 2) {
      LOG_I(NR_MAC, "UE %04x: %d.%d FAPI DL structure is full\n", UE->rnti, frameP, slotP);
      reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
      return;
    }
    int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
    uint8_t time_domain_assignment = get_dl_tda(nr_mac, cell, slotP);
    NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config = &cell->type0_PDCCH_CSS_config[ssb_index];
    int mux_pattern = type0_PDCCH_CSS_config ? type0_PDCCH_CSS_config->type0_pdcch_ss_mux_pattern : 1;
    NR_tda_info_t msg4_tda = get_dl_tda_info(dl_bwp,
                                             ss->searchSpaceType->present,
                                             time_domain_assignment,
                                             scc->dmrs_TypeA_Position,
                                             mux_pattern,
                                             TYPE_TC_RNTI_,
                                             coreset->controlResourceSetId,
                                             false);
    if (!msg4_tda.valid_tda) {
      reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
      return;
    }

    uint16_t pdu_length;
    if(current_harq_pid >= 0) { // in case of retransmission
      NR_UE_harq_t *harq = &sched_ctrl->harq_processes[current_harq_pid];
      DevAssert(!harq->is_waiting);
      pdu_length = harq->tb_size;
    } else {
      uint8_t subheader_len = (mac_sdu_length < 256) ? sizeof(NR_MAC_SUBHEADER_SHORT) : sizeof(NR_MAC_SUBHEADER_LONG);
      pdu_length = mac_sdu_length + subheader_len + 13; // 13 is contention resolution length of msgB. It is divided into 3 parts:
      // BI MAC subheader (1 Oct) + SuccessRAR MAC subheader (1 Oct) + SuccessRAR (11 Oct)
    }

    NR_pdsch_dmrs_t dmrs_info = get_dl_dmrs_params(scc, dl_bwp, &msg4_tda, 1);
    bwp_info_t bwp_info = get_pdsch_bwp_start_size(cell, UE);
    uint8_t mcsTableIdx = dl_bwp->mcsTableIdx;
    uint8_t mcsIndex = 0;
    uint8_t tb_scaling = 0;
    uint32_t tb_size = 0;
    int msg4_nr_rb = 0;
    // increase PRBs until we get to BWPSize or TBS is bigger than MAC PDU size
    do {
      if(msg4_nr_rb < bwp_info.bwpSize)
        msg4_nr_rb++;
      else
        mcsIndex++;
      LOG_D(NR_MAC,"Calling nr_compute_tbs with N_PRB_DMRS %d, N_DMRS_SLOT %d\n",dmrs_info.N_PRB_DMRS,dmrs_info.N_DMRS_SLOT);
      tb_size = nr_compute_tbs(nr_get_Qm_dl(mcsIndex, mcsTableIdx),
                               nr_get_code_rate_dl(mcsIndex, mcsTableIdx),
                               msg4_nr_rb,
                               msg4_tda.nrOfSymbols,
                               dmrs_info.N_PRB_DMRS * dmrs_info.N_DMRS_SLOT,
                               0,
                               tb_scaling,1) >> 3;
    } while (tb_size < pdu_length && mcsIndex<=28);

    AssertFatal(tb_size >= pdu_length, "Cannot allocate %s\n", ra_type_str);

    int rbStart = 0;
    int rbSize = 0;
    const uint16_t msg4_mask = SL_to_bitmap(msg4_tda.startSymbolIndex, msg4_tda.nrOfSymbols);
    uint16_t *vrb_map = cc->vrb_map[beam.idx];
    if (!get_rb_alloc(msg4_nr_rb, msg4_nr_rb, bwp_info.bwpStart, bwp_info.bwpSize, vrb_map, msg4_mask, &rbStart, &rbSize)) {
      LOG_E(NR_MAC, "Cannot find free vrb_map for RNTI %04x!\n", UE->rnti);
      reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
      return;
    }

    int r_pucch = nr_get_pucch_resource(coreset, UE->current_UL_BWP.pucch_Config, CCEIndex);
    LOG_D(NR_MAC, "Msg4 r_pucch %d CCEIndex %d\n", r_pucch, CCEIndex);
    int alloc = nr_acknack_scheduling(cell, UE, frameP, slotP, UE->UE_beam_index, r_pucch, 1);
    if (alloc < 0) {
      LOG_D(NR_MAC,"Couldn't find a pucch allocation for ack nack (msg4) in frame %d slot %d\n", frameP, slotP);
      reset_beam_status(&cell->beam_info, frameP, slotP, UE->UE_beam_index, n_slots_frame, beam.new_beam);
      return;
    }

    // HARQ management
    if (current_harq_pid < 0) {
      AssertFatal(sched_ctrl->available_dl_harq.head >= 0,
                  "UE context not initialized: no HARQ processes found\n");
      current_harq_pid = sched_ctrl->available_dl_harq.head;
      remove_front_nr_list(&sched_ctrl->available_dl_harq);
    }

    NR_UE_harq_t *harq = &sched_ctrl->harq_processes[current_harq_pid];
    NR_sched_pucch_t *pucch = &sched_ctrl->sched_pucch[alloc];
    DevAssert(!harq->is_waiting);
    add_tail_nr_list(&sched_ctrl->feedback_dl_harq, current_harq_pid);
    harq->feedback_slot = pucch->ul_slot;
    harq->feedback_frame = pucch->frame;
    harq->is_waiting = true;
    ra->harq_pid = current_harq_pid;
    UE->mac_stats.dl.rounds[harq->round]++;
    harq->tb_size = tb_size;
    uint8_t *buf = allocate_transportBlock_buffer(&harq->transportBlock, tb_size);
    // Bytes to be transmitted
    if (harq->round == 0) {
      uint16_t mac_pdu_length = 0;
      if (ra->ra_type == RA_4_STEP) {
        // UE Contention Resolution Identity MAC CE
        mac_pdu_length = nr_write_ce_dlsch_pdu(nr_mac, sched_ctrl, buf, 255, ra->cont_res_id);
      } else if (ra->ra_type == RA_2_STEP) {
        mac_pdu_length = nr_fill_successrar(sched_ctrl,
                                            UE->rnti,
                                            ra->cont_res_id,
                                            pucch->resource_indicator,
                                            pucch->timing_indicator,
                                            buf,
                                            mac_pdu_length);
      } else {
        AssertFatal(false, "RA type %d not implemented!\n", ra->ra_type);
      }

      uint8_t buffer[CCCH_SDU_SIZE];
      uint8_t mac_subheader_len = sizeof(NR_MAC_SUBHEADER_SHORT);
      // Get RLC data on the SRB (RRCSetup, RRCReestablishment)
      mac_sdu_length = nr_mac_rlc_data_req(nr_mac->Mod_id, UE->rnti, true, lcid, CCCH_SDU_SIZE, (char *)buffer);

      if (mac_sdu_length < 256) {
        ((NR_MAC_SUBHEADER_SHORT *)&buf[mac_pdu_length])->R = 0;
        ((NR_MAC_SUBHEADER_SHORT *)&buf[mac_pdu_length])->F = 0;
        ((NR_MAC_SUBHEADER_SHORT *)&buf[mac_pdu_length])->LCID = lcid;
        ((NR_MAC_SUBHEADER_SHORT *)&buf[mac_pdu_length])->L = mac_sdu_length;
        ra->mac_pdu_length = mac_pdu_length + mac_sdu_length + sizeof(NR_MAC_SUBHEADER_SHORT);
      } else {
        mac_subheader_len = sizeof(NR_MAC_SUBHEADER_LONG);
        ((NR_MAC_SUBHEADER_LONG *)&buf[mac_pdu_length])->R = 0;
        ((NR_MAC_SUBHEADER_LONG *)&buf[mac_pdu_length])->F = 1;
        ((NR_MAC_SUBHEADER_LONG *)&buf[mac_pdu_length])->LCID = lcid;
        ((NR_MAC_SUBHEADER_LONG *)&buf[mac_pdu_length])->L = htons(mac_sdu_length);
        ra->mac_pdu_length = mac_pdu_length + mac_sdu_length + sizeof(NR_MAC_SUBHEADER_LONG);
      }
      memcpy(&buf[mac_pdu_length + mac_subheader_len], buffer, mac_sdu_length);
    }

    rnti_t rnti = ra->ra_type == RA_4_STEP ? UE->rnti : ra->MsgB_rnti;
    const nr_pdsch_AntennaPorts_t *p = &cell->radio_config.pdsch_AntennaPorts;
    const int num_log_ports = p->N1 * p->N2 * p->XP;
    NR_sched_pdsch_t sched_pdsch = {
        .R = nr_get_code_rate_dl(mcsIndex, mcsTableIdx),
        .Qm = nr_get_Qm_dl(mcsIndex, mcsTableIdx),
        .mcs = mcsIndex,
        .time_domain_allocation = time_domain_assignment,
        .nrOfLayers = 1,
        .dmrs_parms = dmrs_info,
        .tda_info = msg4_tda,
        .pm_index = 0,
        .tb_size = tb_size,
        .bwp_info = bwp_info,
        .rbStart = rbStart,
        .rbSize = rbSize,
        .alloc_type = PDSCH_TYPE1,
        .ant_port_idx = {.numSpatialStreamIndices = 1,
                         .spatialStreamIndices[0] = cell->radio_config.spatial_stream_index[beam.idx * num_log_ports]}};

    const int pduindex = cell->pdu_index++;
    prepare_dl_pdus(cell,
                    UE,
                    &sched_pdsch,
                    dl_req,
                    pucch,
                    TYPE_TC_RNTI_,
                    aggregation_level,
                    CCEIndex,
                    current_harq_pid,
                    rnti,
                    harq->round,
                    tb_scaling,
                    pduindex);

    // Add padding header and zero rest out if there is space left
    if (ra->mac_pdu_length < harq->tb_size) {
      NR_MAC_SUBHEADER_FIXED *padding = (NR_MAC_SUBHEADER_FIXED *) &buf[ra->mac_pdu_length];
      padding->R = 0;
      padding->LCID = DL_SCH_LCID_PADDING;
      for(int k = ra->mac_pdu_length+1; k<harq->tb_size; k++) {
        buf[k] = 0;
      }
    }

    T(T_GNB_MAC_DL_PDU_WITH_DATA, T_INT(nr_mac->Mod_id), T_INT(cell->nr_cellid), T_INT(UE->rnti),
      T_INT(frameP), T_INT(slotP), T_INT(current_harq_pid), T_BUFFER(harq->transportBlock.buf, harq->tb_size));

    // DL TX request
    nfapi_nr_pdu_t *tx_req = &TX_req->pdu_list[TX_req->Number_of_PDUs];
    tx_req->TLVs[0].tag = 0;
    memcpy(tx_req->TLVs[0].value.direct, harq->transportBlock.buf, sizeof(uint8_t) * harq->tb_size);
    tx_req->PDU_index = pduindex;
    tx_req->num_TLV = 1;
    tx_req->TLVs[0].length =  harq->tb_size;
    tx_req->PDU_length = compute_PDU_length(tx_req->num_TLV, tx_req->TLVs[0].length);
    TX_req->SFN = frameP;
    TX_req->Number_of_PDUs++;
    TX_req->Slot = slotP;

    // Mark the corresponding symbols and RBs as used
    fill_pdcch_vrb_map(cell,
                       &sched_ctrl->sched_pdcch,
                       CCEIndex,
                       aggregation_level,
                       beam.idx);
    for (int rb = 0; rb < rbSize; rb++) {
      vrb_map[bwp_info.bwpStart + rb + rbStart] |= msg4_mask;
    }

    ra->ra_state = nrRA_WAIT_Msg4_MsgB_ACK;
    LOG_I(NR_MAC,
          "UE %04x Generate %s: feedback at %4d.%2d, payload %d bytes, next state nrRA_WAIT_Msg4_MsgB_ACK\n",
          UE->rnti,
          ra_type_str,
          pucch->frame,
          pucch->ul_slot,
          harq->tb_size);
  }
}

bool nr_check_Msg4_MsgB_Ack(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, NR_UE_info_t *UE, bool success)
{
  NR_RA_t *ra = UE->ra;
  const char *ra_type_str = ra->ra_type == RA_2_STEP ? "MsgB" : "Msg4";
  const int current_harq_pid = ra->harq_pid;

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_harq_t *harq = &sched_ctrl->harq_processes[current_harq_pid];

  LOG_D(NR_MAC, "ue rnti 0x%04x, round %d, frame %d %d, harq id %d\n", UE->rnti, harq->round, frame, slot, current_harq_pid);

  if (harq->round == 0) {
    if (success) {
      NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
      LOG_A(NR_MAC,
            "%4d.%2d UE %04x: Received Ack of %s. CBRA procedure succeeded (%s)\n",
            frame,
            slot,
            UE->rnti,
            ra_type_str,
            UE->CellGroup ? "UE Connected" : "UE Rejected");
      if (UE->CellGroup) {
        // we configure the UE using common search space with DCIX0 while waiting for a reconfiguration
        configure_UE_BWP(cell, scc, UE, false, NR_SearchSpace__searchSpaceType_PR_common, -1, -1);
        if (!transition_ra_connected_nr_ue(nr_mac, UE))
          release_nr_ue_inst(&nr_mac->UE_info.uid_allocator, UE);
      } else {  // in case of  UE reject
        NR_UE_info_t *ue = remove_ra_ue(&nr_mac->UE_info, UE->rnti);
        if (ue)
          release_nr_ue_inst(&nr_mac->UE_info.uid_allocator, ue);
        return true;
      }
    } else {
      LOG_I(NR_MAC, "%4d.%2d UE %04x: RA Procedure failed at %s!\n", frame, slot, UE->rnti, ra_type_str);
      nr_mac_trigger_ul_failure(sched_ctrl, UE->current_DL_BWP.scs);
    }

    if (sched_ctrl->retrans_dl_harq.head >= 0) {
      remove_nr_list(&sched_ctrl->retrans_dl_harq, current_harq_pid);
    }
  } else {
    LOG_I(NR_MAC, "(UE %04x) Received Nack in %s, preparing retransmission!\n", UE->rnti, ra_type_str);
    ra->ra_state = ra->ra_type == RA_4_STEP ? nrRA_Msg4 : nrRA_MsgB;
  }
  return false;
}

void nr_schedule_RA(gNB_MAC_INST *mac,
                    nr_cell_sched_t *cell,
                    frame_t frameP,
                    slot_t slotP,
                    nfapi_nr_ul_dci_request_t *ul_dci_req,
                    nfapi_nr_dl_tti_request_t *DL_req,
                    nfapi_nr_tx_data_request_t *TX_req)
{
  start_meas(&cell->schedule_ra);
  // releasing a UE modifies access_ue_list: collect them and release after the loop
  rnti_t to_release[NR_NB_RA_PROC_MAX];
  int num_release = 0;
  FOR_EACH_RA_UE(&mac->UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    NR_RA_t *ra = UE->ra;
    if (ra->ra_state != nrRA_gNB_IDLE)
      LOG_D(NR_MAC, "UE %04x frame.slot %d.%d RA state: %d\n", UE->rnti, frameP, slotP, ra->ra_state);

    // Check RA Contention Resolution timer (TODO check this procedure)
    if (ra->ra_type == RA_4_STEP && ra->ra_state > nrRA_WAIT_Msg3) {
      ra->contention_resolution_timer--;
      if (ra->contention_resolution_timer == 0) {
        LOG_W(NR_MAC, "(%d.%d) RA Contention Resolution timer expired for UE 0x%04x, RA procedure failed...\n", frameP, slotP, UE->rnti);
        bool requested = false;
        if (UE->CellGroup) // request if we did not reject the UE
          requested = nr_mac_request_release_ue(mac, UE->rnti);
        if (!requested)
          to_release[num_release++] = UE->rnti;
        continue;
      }
    }

    switch (ra->ra_state) {
      case nrRA_Msg2:
        if (nr_generate_Msg2(mac, cell, frameP, slotP, UE, DL_req, TX_req))
          to_release[num_release++] = UE->rnti;
        break;
      case nrRA_Msg3_retransmission:
        nr_generate_Msg3_retransmission(cell, frameP, slotP, UE, ul_dci_req);
        break;
      case nrRA_Msg4:
      case nrRA_MsgB:
        nr_generate_Msg4_MsgB(mac, cell, frameP, slotP, UE, DL_req, TX_req);
        break;
      default:
        break;
    }
  }
  for (int i = 0; i < num_release; i++) {
    NR_UE_info_t *UE = remove_ra_ue(&mac->UE_info, to_release[i]);
    DevAssert(UE); // must exist, otherwise would not be handled here
    release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
  }
  stop_meas(&cell->schedule_ra);
}
