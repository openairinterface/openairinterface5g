/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief       Routines for UE scheduling
 */

#include <stdio.h>
#include <math.h>
#include <pthread.h>

/* exe */
#include <common/utils/nr/nr_common.h>

/* MAC */
#include "LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"

/* utils */
#include "assertions.h"
#include "oai_asn1.h"
#include "SIMULATION/TOOLS/sim.h" // for taus
#include "utils.h"
#include "bits.h"
#include <executables/softmodem-common.h>
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "RRC/NR_UE/L2_interface_ue.h"

//#define SRS_DEBUG
#define verifyMutex(a)                                                \
  {                                                                   \
    int ret = a;                                                      \
    AssertFatal(ret == 0, "Failure in mutex management ret=%d\n", a); \
  }

static void clear_ul_config_request(NR_UE_MAC_INST_t *mac)
{
  int slots = mac->frame_structure.numb_slots_frame;
  for (int i = 0; i < slots ; i++) {
    fapi_nr_ul_config_request_t *ul_config = mac->ul_config_request + i;
    verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
    ul_config->number_pdus = 0;
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
  }
}

fapi_nr_ul_config_request_pdu_t *lockGet_ul_config(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx, uint8_t pdu_type)
{
  // Check if requested on the right slot
  if (!is_ul_slot(slot_tx, &mac->frame_structure)) {
    LOG_E(NR_MAC, "Lock of UL config for PDU type %d called for slot %d which is not UL\n", pdu_type, slot_tx);
    return NULL;
  }

  AssertFatal(mac->ul_config_request != NULL, "mac->ul_config_request not initialized, logic bug\n");
  fapi_nr_ul_config_request_t *ul_config = mac->ul_config_request + slot_tx;

  verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
  if (ul_config->number_pdus != 0 && (ul_config->frame != frame_tx || ul_config->slot != slot_tx)) {
    LOG_E(NR_MAC, "Error in ul config consistency, clearing slot %d\n", slot_tx);
    ul_config->number_pdus = 0;
  }
  ul_config->frame = frame_tx;
  ul_config->slot = slot_tx;
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config for slot %d, no memory\n", slot_tx);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  fapi_nr_ul_config_request_pdu_t *pdu = ul_config->ul_config_list + ul_config->number_pdus++;
  pdu->pdu_type = pdu_type;
  pdu->lock = &ul_config->mutex_ul_config;
  pdu->privateNBpdus = &ul_config->number_pdus;
  LOG_D(NR_MAC, "Added ul pdu for %d.%d, type %d\n", frame_tx, slot_tx, pdu_type);
  return pdu;
}

static nr_lcordered_info_t *get_lc_info_from_lcid(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid)
{
  nr_lcordered_info_t *lc_info = NULL;
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    if (mac->lc_ordered_list.array[i]->lcid == lcid) {
      lc_info = mac->lc_ordered_list.array[i];
      break;
    }
  }
  return lc_info;
}

static int lcid_buffer_index(NR_LogicalChannelIdentity_t lcid)
{
  AssertFatal(lcid > 0 && lcid <= NR_MAX_NUM_LCID, "Invalid LCID %ld\n", lcid);
  return lcid - 1;
}

NR_LC_SCHEDULING_INFO *get_scheduling_info_from_lcid(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid)
{
  int idx = lcid_buffer_index(lcid);
  return &mac->scheduling_info.lc_sched_info[idx];
}

static void trigger_regular_bsr(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid, bool sr_DelayTimerApplied)
{
  // Regular BSR trigger
  mac->scheduling_info.BSR_reporting_active |= NR_BSR_TRIGGER_REGULAR;
  mac->scheduling_info.regularBSR_trigger_lcid = lcid;
  LOG_D(NR_MAC, "Triggering regular BSR for LCID %ld\n", lcid);
  // if the BSR is triggered for a logical channel for which logicalChannelSR-DelayTimerApplied with value true
  // start or restart the logicalChannelSR-DelayTimer
  // else stop the logicalChannelSR-DelayTimer
  if (sr_DelayTimerApplied)
    nr_timer_start(&mac->scheduling_info.sr_DelayTimer);
  else
    nr_timer_stop(&mac->scheduling_info.sr_DelayTimer);
}

static void flush_harq_buffers(NR_UE_MAC_INST_t *mac)
{
  for (int k = 0; k < NR_MAX_HARQ_PROCESSES; k++) {
    for (int c = 0; c < 2; c++) {
      memset(&mac->dl_harq_info[k][c], 0, sizeof(NR_UE_DL_HARQ_STATUS_t));
      mac->dl_harq_info[k][c].last_ndi = -1; // initialize to invalid value
    }
    memset(&mac->ul_harq_info[k], 0, sizeof(*mac->ul_harq_info));
    mac->ul_harq_info[k].last_ndi = -1; // initialize to invalid value
  }
}

void handle_time_alignment_timer_expired(NR_UE_MAC_INST_t *mac)
{
  // flush all HARQ buffers for all Serving Cells
  flush_harq_buffers(mac);
  // release PUCCH for all Serving Cells;
  // release SRS for all Serving Cells;
  release_PUCCH_SRS(mac);
  // clear any configured downlink assignments and uplink grants;
  if (mac->dl_config_request)
    memset(mac->dl_config_request, 0, sizeof(*mac->dl_config_request));
  if (mac->ul_config_request)
    clear_ul_config_request(mac);
  // clear any PUSCH resources for semi-persistent CSI reporting
  // TODO we don't have semi-persistent CSI reporting
  // maintain N_TA
  // TODO not sure what to do here
}

void handle_ulsync_loss(NR_UE_MAC_INST_t *mac)
{
  // flush all HARQ buffers for all Serving Cells
  flush_harq_buffers(mac);
  // clear any configured downlink assignments and uplink grants;
  if (mac->dl_config_request)
    memset(mac->dl_config_request, 0, sizeof(*mac->dl_config_request));
  if (mac->ul_config_request)
    clear_ul_config_request(mac);

  // Need to receive SIB19 again after loosing UL SYNC
  mac->state = UE_RECEIVING_SIB;
  // gNB needs to send PDCCH ORDER triggering RA after detecting ULSYNC LOSS
  LOG_W(NR_MAC, "Wait for PDCCH ORDER, RACH needs to performed to obtain ULSYNC.\n");
}

void update_mac_dl_timers(NR_UE_MAC_INST_t *mac)
{
  bool ra_window_expired = nr_timer_tick(&mac->ra.response_window_timer);
  if (ra_window_expired) // consider the Random Access Response reception not successful
    nr_rar_not_successful(mac);
  bool alignment_timer_expired = nr_timer_tick(&mac->time_alignment_timer);
  if (alignment_timer_expired)
    handle_time_alignment_timer_expired(mac);
}

void update_mac_ul_timers(NR_UE_MAC_INST_t *mac)
{
  if (mac->data_inactivity_timer) {
    bool inactivity_timer_expired = nr_timer_tick(mac->data_inactivity_timer);
    if (inactivity_timer_expired)
      nr_mac_rrc_inactivity_timer_ind(mac->ue_id);
  }
  bool contention_resolution_expired = nr_timer_tick(&mac->ra.contention_resolution_timer);
  if (contention_resolution_expired)
    nr_ra_contention_resolution_failed(mac);
  for (int j = 0; j < NR_MAX_SR_ID; j++)
    nr_timer_tick(&mac->scheduling_info.sr_info[j].prohibitTimer);
  nr_timer_tick(&mac->scheduling_info.sr_DelayTimer);
  bool retxBSR_expired = nr_timer_tick(&mac->scheduling_info.retxBSR_Timer);
  if (retxBSR_expired) {
    LOG_D(NR_MAC, "retxBSR timer expired\n");
    for (int i = 0; i < mac->lc_ordered_list.count; i++) {
      nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
      NR_LogicalChannelIdentity_t lcid = lc_info->lcid;
      // if at least one of the logical channels which belong to an LCG contains UL data
      NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
      if (lc_sched_info->LCGID != NR_INVALID_LCGID && lc_sched_info->LCID_buffer_remain > 0) {
        LOG_D(NR_MAC, "Triggering regular BSR after retxBSR timer expired\n");
        trigger_regular_bsr(mac, lcid, lc_info->sr_DelayTimerApplied);
        break;
      }
    }
  }
  bool periodicBSR_expired = nr_timer_tick(&mac->scheduling_info.periodicBSR_Timer);
  if (periodicBSR_expired) {
    // periodicBSR-Timer expires, trigger BSR
    mac->scheduling_info.BSR_reporting_active |= NR_BSR_TRIGGER_PERIODIC;
    LOG_D(NR_MAC, "[UE %d] MAC BSR Triggered PeriodicBSR Timer expiry\n", mac->ue_id);
  }
  for (int i = 0; i < NR_MAX_NUM_LCID; i++)
    AssertFatal(!nr_timer_tick(&mac->scheduling_info.lc_sched_info[i].Bj_timer),
                "Bj timer for LCID %d expired! That should never happen\n",
                i);

  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  if (phr_info->is_configured) {
    bool prohibit_expired = nr_timer_tick(&phr_info->prohibitPHR_Timer);
    if (prohibit_expired) {
      int16_t pathloss = compute_nr_SSB_PL(mac);
      if (abs(pathloss - phr_info->PathlossLastValue) > phr_info->PathlossChange_db) {
        phr_info->phr_reporting |= (1 << phr_cause_prohibit_timer);
      }
    }
    bool periodic_expired = nr_timer_tick(&phr_info->periodicPHR_Timer);
    if (periodic_expired) {
      phr_info->phr_reporting |= (1 << phr_cause_periodic_timer);
    }
  }
  bool ra_backoff_expired = nr_timer_tick(&mac->ra.RA_backoff_timer);
  if (ra_backoff_expired) {
    // perform the Random Access Resource selection procedure after the backoff time
    mac->ra.ra_state = nrRA_GENERATE_PREAMBLE;
    ra_resource_selection(mac);
  } else {
    if (nr_timer_is_active(&mac->ra.RA_backoff_timer)) {
      // if the criteria (as defined in clause 5.1.2) to select contention-free Random Access Resources
      // is met during the backoff time
      // TODO verify what does this mean
      if (mac->ra.cfra) {
        // perform the Random Access Resource selection procedure
        nr_timer_stop(&mac->ra.RA_backoff_timer);
        mac->ra.ra_state = nrRA_GENERATE_PREAMBLE;
        ra_resource_selection(mac);
      }
    }
  }
}

void remove_ul_config_last_item(fapi_nr_ul_config_request_pdu_t *pdu)
{
  (*pdu->privateNBpdus)--;
}

void release_ul_config(fapi_nr_ul_config_request_pdu_t *configPerSlot, bool clearIt)
{
  if (clearIt)
    *configPerSlot->privateNBpdus = 0;
  verifyMutex(pthread_mutex_unlock(configPerSlot->lock));
}

fapi_nr_ul_config_request_pdu_t *fapiLockIterator(fapi_nr_ul_config_request_t *ul_config, frame_t frame_tx, int slot_tx)
{
  verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config in slot %d no memory\n", ul_config->slot);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  if (ul_config->number_pdus != 0 && (ul_config->frame != frame_tx || ul_config->slot != slot_tx)) {
    LOG_E(NR_MAC, "Error in ul config consistency, clearing it slot %d\n", slot_tx);
    ul_config->number_pdus = 0;
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config for slot %d, no memory\n", slot_tx);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  fapi_nr_ul_config_request_pdu_t *pdu = ul_config->ul_config_list + ul_config->number_pdus;
  pdu->pdu_type = FAPI_NR_END;
  pdu->lock = &ul_config->mutex_ul_config;
  pdu->privateNBpdus = &ul_config->number_pdus;
  return ul_config->ul_config_list;
}

fapi_nr_ul_config_request_pdu_t *lockGet_ul_iterator(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx)
{
  //Check if requested on the right slot
  if (!is_ul_slot(slot_tx, &mac->frame_structure)) {
    LOG_E(NR_MAC, "UL lock iterator called for slot %d which is not UL\n", slot_tx);
    return NULL;
  }
  AssertFatal(mac->ul_config_request != NULL, "mac->ul_config_request not initialized, logic bug\n");
  return fapiLockIterator(mac->ul_config_request + slot_tx, frame_tx, slot_tx);
}

/*
 * This function returns the DL config corresponding to a given DL slot
 * from MAC instance .
 */
fapi_nr_dl_config_request_t *get_dl_config_request(NR_UE_MAC_INST_t *mac, int slot)
{
  AssertFatal(mac->dl_config_request != NULL, "mac->dl_config_request not initialized, logic bug\n");
  return &mac->dl_config_request[slot];
}

static void ul_layers_config(NR_UE_MAC_INST_t *mac, nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu, dci_pdu_rel15_t *dci)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP->srs_Config;
  NR_PUSCH_Config_t *pusch_Config = current_UL_BWP->pusch_Config;
  long transformPrecoder = pusch_config_pdu->transform_precoding;

  /* PRECOD_NBR_LAYERS */
  // 0 bits if the higher layer parameter txConfig = nonCodeBook
  if (*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook){
    // The UE shall transmit PUSCH using the same antenna port(s) as the SRS port(s) in the SRS resource indicated by the DCI format 0_1
    // 38.214  Section 6.1.1
    uint8_t n = get_pusch_nb_antenna_ports(pusch_Config, srs_config, dci->srs_resource_indicator);
    set_precoding_information_parameters(pusch_config_pdu, n, transformPrecoder, dci->precoding_information.val, pusch_Config);
  }
}

static void fill_pusch_uci_struct(const NR_PUSCH_Config_t *pusch_Config,
                                  const nfapi_nr_ue_csi_payload_t *csi_payload,
                                  nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu)
{
  pusch_config_pdu->pusch_uci.csi_payload = *csi_payload;
  AssertFatal(pusch_Config && pusch_Config->uci_OnPUSCH, "UCI on PUSCH need to be configured\n");
  NR_UCI_OnPUSCH_t *onPusch = pusch_Config->uci_OnPUSCH->choice.setup;
  AssertFatal(onPusch && onPusch->betaOffsets && onPusch->betaOffsets->present == NR_UCI_OnPUSCH__betaOffsets_PR_semiStatic,
              "Only semistatic beta offset is supported\n");
  NR_BetaOffsets_t *beta_offsets = onPusch->betaOffsets->choice.semiStatic;

  pusch_config_pdu->pusch_uci.beta_offset_csi1 = pusch_config_pdu->pusch_uci.csi_payload.p1_bits < 12
                                                     ? *beta_offsets->betaOffsetCSI_Part1_Index1
                                                     : *beta_offsets->betaOffsetCSI_Part1_Index2;
  pusch_config_pdu->pusch_uci.beta_offset_csi2 = pusch_config_pdu->pusch_uci.csi_payload.p2_bits < 12
                                                     ? *beta_offsets->betaOffsetCSI_Part2_Index1
                                                     : *beta_offsets->betaOffsetCSI_Part2_Index2;
  pusch_config_pdu->pusch_uci.alpha_scaling = onPusch->scaling;
}

// Configuration of Msg3 PDU according to clauses:
// - 8.3 of 3GPP TS 38.213 version 16.3.0 Release 16
// - 6.1.2.2 of TS 38.214
// - 6.1.3 of TS 38.214
// - 6.2.2 of TS 38.214
// - 6.1.4.2 of TS 38.214
// - 6.4.1.1.1 of TS 38.211
// - 6.3.1.7 of 38.211
int nr_config_pusch_pdu(NR_UE_MAC_INST_t *mac,
                        NR_tda_info_t *tda_info,
                        nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                        dci_pdu_rel15_t *dci,
                        nfapi_nr_ue_csi_payload_t *csi_report,
                        RAR_grant_t *rar_grant,
                        rnti_t rnti,
                        int ss_type,
                        const nr_dci_format_t dci_format)
{
  uint16_t l_prime_mask = 0;
  int N_PRB_oh  = 0;

  int rnti_type = get_rnti_type(mac, rnti);
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  int scs = current_UL_BWP->scs;

  // Common configuration
  pusch_config_pdu->dmrs_config_type = pusch_dmrs_type1;
  pusch_config_pdu->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
  pusch_config_pdu->nrOfLayers = 1;
  pusch_config_pdu->Tpmi = 0;
  pusch_config_pdu->rnti = rnti;
  pusch_config_pdu->dmrs_ports = 1;
  pusch_dmrs_AdditionalPosition_t add_pos = pusch_dmrs_pos2;
  int dmrslength = 1;
  NR_PUSCH_Config_t *pusch_Config = current_UL_BWP->pusch_Config;

  AssertFatal(!((mac->ra.ra_state < nrRA_SUCCEEDED && mac->ra.ra_type == RA_2_STEP) && rar_grant),
              "logic error: Is not possible to have both msgA_pusch_resource and rar_grant\n");

  if (mac->ra.ra_state < nrRA_SUCCEEDED && mac->ra.ra_type == RA_2_STEP) {
    NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource =
        current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
    int mapping_type = *msgA_PUSCH_Resource->mappingTypeMsgA_PUSCH_r16;
    int S = 0;
    int L = 0;
    SLIV2SL(*msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16, &S, &L);
    tda_info->k2 = msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16;
    l_prime_mask = get_l_prime(L, mapping_type, add_pos, dmrslength, S, 0);
    LOG_I(NR_MAC, "MSGA PUSCH start_sym:%d NR Symb:%d mappingtype:%d, DMRS_MASK:%x\n", S, L, mapping_type, l_prime_mask);

    LOG_D(NR_MAC,
          "sc_info->initial_ul_BWPStart = %d  sc_info->initial_ul_BWPSize = %d\n",
          sc_info->initial_ul_BWPStart,
          sc_info->initial_ul_BWPSize);
    pusch_config_pdu->handle = 0;
    pusch_config_pdu->rb_size = msgA_PUSCH_Resource->nrofPRBs_PerMsgA_PO_r16;
    pusch_config_pdu->mcs_table = 0;
    pusch_config_pdu->frequency_hopping =
        msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 ? *msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 : 0;
    pusch_config_pdu->pusch_data.new_data_indicator = 1; // new data
    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    pusch_config_pdu->transform_precoding =
        *mac->current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16;
    pusch_config_pdu->rb_bitmap[0] = 0;
    pusch_config_pdu->rb_start = msgA_PUSCH_Resource->frequencyStartMsgA_PUSCH_r16; // rb_start depends on the RO
    pusch_config_pdu->bwp_size = sc_info->initial_ul_BWPSize;
    pusch_config_pdu->bwp_start = sc_info->initial_ul_BWPStart;
    pusch_config_pdu->subcarrier_spacing = scs;
    pusch_config_pdu->cyclic_prefix = 0;
    pusch_config_pdu->uplink_frequency_shift_7p5khz = 0;
    pusch_config_pdu->vrb_to_prb_mapping = 0;
    pusch_config_pdu->dmrs_config_type = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    NR_MsgA_PUSCH_Config_r16_t *msgA_pusch_cfg = current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16;
    pusch_config_pdu->data_scrambling_id =
        msgA_pusch_cfg->msgA_DataScramblingIndex_r16 ? *msgA_pusch_cfg->msgA_DataScramblingIndex_r16 : mac->physCellId;
    pusch_config_pdu->ul_dmrs_scrambling_id = msgA_PUSCH_Resource->msgA_DMRS_Config_r16.msgA_ScramblingID0_r16
                                                  ? *msgA_PUSCH_Resource->msgA_DMRS_Config_r16.msgA_ScramblingID0_r16
                                                  : mac->physCellId;
    pusch_config_pdu->scid =
        0; // n_SCID from DMRS resource mapping (TS 38.213 8.1A); only resource 0 scheduled (nrofDMRS-Sequences = 1)
    pusch_config_pdu->resource_alloc = 1;
    pusch_config_pdu->tx_direct_current_location = 0;
    pusch_config_pdu->mcs_index = msgA_PUSCH_Resource->msgA_MCS_r16;
    pusch_config_pdu->start_symbol_index = S;
    pusch_config_pdu->nr_of_symbols = L;
    pusch_config_pdu->pusch_data.rv_index = 0; // 8.3 in 38.213
    pusch_config_pdu->pusch_data.harq_process_id = 0;
    pusch_config_pdu->pusch_data.num_cb = 0;
    pusch_config_pdu->tbslbrm = 0;

    LOG_D(NR_MAC,
          "pdu_bit_map %d rnti %d handle %d rb_size %d mcs_table %d frequency_hopping %d dmrs_ports %d start_symbol_index %d "
          "nr_of_symbols %d new_data_indicator %d num_dmrs_cdm_grps_no_data %d ul_dmrs_symb_pos %d transform_precoding %d rb_start "
          "%d bwp_size %d bwp_start %d subcarrier_spacing %d data_scrambling_id %d ul_dmrs_scrambling_id %d mcs_index %d "
          "qam_mod_order %d target_code_rate %d tb_size %d\n",
          pusch_config_pdu->pdu_bit_map,
          pusch_config_pdu->rnti,
          pusch_config_pdu->handle,
          pusch_config_pdu->rb_size,
          pusch_config_pdu->mcs_table,
          pusch_config_pdu->frequency_hopping,
          pusch_config_pdu->dmrs_ports,
          pusch_config_pdu->start_symbol_index,
          pusch_config_pdu->nr_of_symbols,
          pusch_config_pdu->pusch_data.new_data_indicator,
          pusch_config_pdu->num_dmrs_cdm_grps_no_data,
          pusch_config_pdu->ul_dmrs_symb_pos,
          pusch_config_pdu->transform_precoding,
          pusch_config_pdu->rb_start,
          pusch_config_pdu->bwp_size,
          pusch_config_pdu->bwp_start,
          pusch_config_pdu->subcarrier_spacing,
          pusch_config_pdu->data_scrambling_id,
          pusch_config_pdu->ul_dmrs_scrambling_id,
          pusch_config_pdu->mcs_index,
          pusch_config_pdu->qam_mod_order,
          pusch_config_pdu->target_code_rate,
          pusch_config_pdu->pusch_data.tb_size);

  } else if (rar_grant) {
    // Note: for Msg3 or MsgA PUSCH transmission the N_PRB_oh is always set to 0
    int ibwp_start = sc_info->initial_ul_BWPStart;
    int ibwp_size = sc_info->initial_ul_BWPSize;
    int abwp_start = current_UL_BWP->BWPStart;
    int abwp_size = current_UL_BWP->BWPSize;
    int scs = current_UL_BWP->scs;

    // BWP start selection according to 8.3 of TS 38.213
    if ((ibwp_start < abwp_start) || (ibwp_size > abwp_size)) {
      pusch_config_pdu->bwp_start = abwp_start;
      pusch_config_pdu->bwp_size = abwp_size;
    } else {
      pusch_config_pdu->bwp_start = ibwp_start;
      pusch_config_pdu->bwp_size = ibwp_size;
    }

    //// Resource assignment from RAR
    // Frequency domain allocation according to 8.3 of TS 38.213
    int mask;
    if (ibwp_size < 180)
      mask = (1 << (ceil_log2_u32((ibwp_size * (ibwp_size + 1)) >>1 ))) - 1;
    else
      mask = (1 << (28 - (ceil_log2_u32((ibwp_size * (ibwp_size + 1)) >>1)))) - 1;

    dci_field_t f_alloc;
    f_alloc.val = rar_grant->Msg3_f_alloc & mask;
    if (nr_ue_process_dci_freq_dom_resource_assignment(pusch_config_pdu,
                                                       NULL,
                                                       NULL,
                                                       ibwp_size,
                                                       0,
                                                       0,
                                                       f_alloc) < 0) {
      LOG_E(NR_MAC, "can't nr_ue_process_dci_freq_dom_resource_assignment()\n");
      mac->stats.bad_dci++;
      return -1;
    }

    // virtual resource block to physical resource mapping for Msg3 PUSCH (6.3.1.7 in 38.211)
    //pusch_config_pdu->rb_start += ibwp_start - abwp_start;

    // Time domain allocation
    pusch_config_pdu->start_symbol_index = tda_info->startSymbolIndex;
    pusch_config_pdu->nr_of_symbols = tda_info->nrOfSymbols;
    l_prime_mask = get_l_prime(tda_info->nrOfSymbols,
                               tda_info->mapping_type,
                               add_pos,
                               dmrslength,
                               tda_info->startSymbolIndex,
                               mac->dmrs_TypeA_Position);
    LOG_D(NR_MAC, "MSG3 start_sym:%d NR Symb:%d mappingtype:%d, DMRS_MASK:%x\n", pusch_config_pdu->start_symbol_index, pusch_config_pdu->nr_of_symbols, tda_info->mapping_type, l_prime_mask);

#ifdef DEBUG_MSG3
    LOG_D(NR_MAC, "In %s BWP assignment (BWP (start %d, size %d) \n", __FUNCTION__, pusch_config_pdu->bwp_start, pusch_config_pdu->bwp_size);
#endif

    // MCS
    pusch_config_pdu->mcs_index = rar_grant->mcs;
    // Frequency hopping
    pusch_config_pdu->frequency_hopping = rar_grant->freq_hopping;

    // DM-RS configuration according to 6.2.2 UE DM-RS transmission procedure in 38.214
    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;

    // DMRS sequence initialization [TS 38.211, sec 6.4.1.1.1].
    // Should match what is sent in DCI 0_1, otherwise set to 0.
    pusch_config_pdu->scid = 0;

    // Transform precoding according to 6.1.3 UE procedure for applying transform precoding on PUSCH in 38.214
    pusch_config_pdu->transform_precoding = get_transformPrecoding(current_UL_BWP, NR_UL_DCI_FORMAT_0_0, 0); // as if it was DCI 0_0

    // Resource allocation in frequency domain according to 6.1.2.2 in TS 38.214
    pusch_config_pdu->resource_alloc = 1;

    //// Completing PUSCH PDU
    pusch_config_pdu->mcs_table = 0;
    pusch_config_pdu->cyclic_prefix = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    pusch_config_pdu->ul_dmrs_scrambling_id = mac->physCellId;
    pusch_config_pdu->subcarrier_spacing = scs;
    pusch_config_pdu->vrb_to_prb_mapping = 0;
    pusch_config_pdu->uplink_frequency_shift_7p5khz = 0;
    //Optional Data only included if indicated in pduBitmap
    pusch_config_pdu->pusch_data.rv_index = 0;  // 8.3 in 38.213
    pusch_config_pdu->pusch_data.harq_process_id = 0;
    pusch_config_pdu->pusch_data.new_data_indicator = true; // new data
    pusch_config_pdu->pusch_data.num_cb = 0;
    pusch_config_pdu->tbslbrm = 0;

  } else if (dci) {
    pusch_config_pdu->subcarrier_spacing = scs;
    pusch_config_pdu->ulsch_indicator = dci->ulsch_indicator;
    if (dci->csi_request.nbits > 0 && dci->csi_request.val > 0) {
      AssertFatal(csi_report, "CSI report needs to be present in case of CSI request\n");
      fill_pusch_uci_struct(pusch_Config, csi_report, pusch_config_pdu);
    } else {
      pusch_config_pdu->pusch_uci.csi_payload.p1_bits = 0;
      pusch_config_pdu->pusch_uci.csi_payload.p2_bits = 0;
    }

    pusch_config_pdu->pusch_uci.harq_ack_bit_length = 0;

    pusch_config_pdu->bwp_start = current_UL_BWP->BWPStart;
    if (dci_format == NR_UL_DCI_FORMAT_0_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common) {
      pusch_config_pdu->bwp_size = sc_info->initial_ul_BWPSize;
    } else {
      pusch_config_pdu->bwp_size = current_UL_BWP->BWPSize;
    }

    pusch_config_pdu->start_symbol_index = tda_info->startSymbolIndex;
    pusch_config_pdu->nr_of_symbols = tda_info->nrOfSymbols;

    /* Transform precoding */
    pusch_config_pdu->transform_precoding = get_transformPrecoding(current_UL_BWP, dci_format, 0);
    bool tp_enabled = pusch_config_pdu->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled;

    /*DCI format-related configuration*/
    if (dci_format == NR_UL_DCI_FORMAT_0_0) {
      if (!tp_enabled && pusch_config_pdu->nr_of_symbols < 3)
        pusch_config_pdu->num_dmrs_cdm_grps_no_data = 1;
      else
        pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    } else if (dci_format == NR_UL_DCI_FORMAT_0_1) {
      ul_layers_config(mac, pusch_config_pdu, dci);
      ul_ports_config(mac, &dmrslength, pusch_config_pdu, dci);
    } else {
      LOG_E(NR_MAC, "UL grant from DCI format %d is not handled...\n", dci_format);
      mac->stats.bad_dci++;
      return -1;
    }

    if (pusch_config_pdu->nrOfLayers < 1) {
      LOG_E(NR_MAC, "Invalid UL number of layers %d from DCI\n", pusch_config_pdu->nrOfLayers);
      mac->stats.bad_dci++;
      return -1;
    }

    int mappingtype = tda_info->mapping_type;
    NR_DMRS_UplinkConfig_t *NR_DMRS_ulconfig = NULL;
    if (pusch_Config) {
      if (mappingtype == NR_PUSCH_TimeDomainResourceAllocation__mappingType_typeA) {
        if (!pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA) {
          LOG_E(MAC, "Invalid PUSCH DMRS configuration, expected typeA but not configured\n");
          return -1;
        } else
          NR_DMRS_ulconfig = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup;
      } else { // typeB
        if (!pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB) {
          LOG_E(MAC, "Invalid PDSCH DMRS configuration, expected typeB but not configured\n");
          return -1;
        } else
          NR_DMRS_ulconfig = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup;
      }
    }

    pusch_config_pdu->scid = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    if (pusch_Config
        && pusch_Config->dataScramblingIdentityPUSCH
        && rnti_type == TYPE_C_RNTI_
        && !(dci_format == NR_UL_DCI_FORMAT_0_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common))
      pusch_config_pdu->data_scrambling_id = *pusch_Config->dataScramblingIdentityPUSCH;

    pusch_config_pdu->ul_dmrs_scrambling_id = mac->physCellId;
    if (dci_format == NR_UL_DCI_FORMAT_0_1)
      pusch_config_pdu->scid = dci->dmrs_sequence_initialization.val;

    /* TRANSFORM PRECODING ------------------------------------------------------------------------------------------*/
    if (tp_enabled) {

      uint32_t n_RS_Id = 0;
      if (NR_DMRS_ulconfig->transformPrecodingEnabled &&
          NR_DMRS_ulconfig->transformPrecodingEnabled->nPUSCH_Identity)
        n_RS_Id = *NR_DMRS_ulconfig->transformPrecodingEnabled->nPUSCH_Identity;
      else
        n_RS_Id = mac->physCellId;

      // U as specified in section 6.4.1.1.1.2 in 38.211, if sequence hopping and group hopping are disabled
      pusch_config_pdu->dfts_ofdm.low_papr_group_number = n_RS_Id % 30;

      // V as specified in section 6.4.1.1.1.2 in 38.211 V = 0 if sequence hopping and group hopping are disabled
      if (!NR_DMRS_ulconfig || !NR_DMRS_ulconfig->transformPrecodingEnabled ||
          (!NR_DMRS_ulconfig->transformPrecodingEnabled->sequenceGroupHopping && !NR_DMRS_ulconfig->transformPrecodingEnabled->sequenceHopping))
        pusch_config_pdu->dfts_ofdm.low_papr_sequence_number = 0;
      else
        AssertFatal(1==0,"SequenceGroupHopping or sequenceHopping are NOT Supported\n");

      LOG_D(NR_MAC,
            "TRANSFORM PRECODING IS ENABLED. CDM groups: %d, U: %d \n",
            pusch_config_pdu->num_dmrs_cdm_grps_no_data,
            pusch_config_pdu->dfts_ofdm.low_papr_group_number);
    }
    else {
      if (pusch_config_pdu->scid == 0 && NR_DMRS_ulconfig && NR_DMRS_ulconfig->transformPrecodingDisabled &&
          NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID0)
        pusch_config_pdu->ul_dmrs_scrambling_id = *NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID0;
      if (pusch_config_pdu->scid == 1 && NR_DMRS_ulconfig && NR_DMRS_ulconfig->transformPrecodingDisabled
          && NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID1)
        pusch_config_pdu->ul_dmrs_scrambling_id = *NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID1;
    }

    /* TRANSFORM PRECODING --------------------------------------------------------------------------------------------------------*/

    /* IDENTIFIER_DCI_FORMATS */
    /* FREQ_DOM_RESOURCE_ASSIGNMENT_UL */
    if (nr_ue_process_dci_freq_dom_resource_assignment(pusch_config_pdu,
                                                       NULL,
                                                       NULL,
                                                       pusch_config_pdu->bwp_size,
                                                       0,
                                                       0,
                                                       dci->frequency_domain_assignment) < 0) {
      LOG_E(NR_MAC, "can't nr_ue_process_dci_freq_dom_resource_assignment()\n");
      mac->stats.bad_dci++;
      return -1;
    }

    /* FREQ_HOPPING_FLAG */
    if (pusch_Config
        && pusch_Config->frequencyHopping
        && (pusch_Config->resourceAllocation != NR_PUSCH_Config__resourceAllocation_resourceAllocationType0)) {
      pusch_config_pdu->frequency_hopping = dci->frequency_hopping_flag.val;
    }

    /* MCS */
    pusch_config_pdu->mcs_index = dci->mcs;

    /* MCS TABLE */
    long *mcs_table_config = pusch_Config ? (tp_enabled ? pusch_Config->mcs_TableTransformPrecoder : pusch_Config->mcs_Table) : NULL;
    pusch_config_pdu->mcs_table = get_pusch_mcs_table(mcs_table_config, tp_enabled, dci_format, rnti_type, ss_type, false);

    /* NDI */
    const int pid = dci->harq_pid.val;
    NR_UE_UL_HARQ_INFO_t *harq = &mac->ul_harq_info[pid];
    pusch_config_pdu->pusch_data.new_data_indicator = false;
    if (dci->ndi != harq->last_ndi) {
      pusch_config_pdu->pusch_data.new_data_indicator = true;
      // if new data reset harq structure
      memset(harq, 0, sizeof(*harq));
    } else
      harq->round++;
    harq->last_ndi = dci->ndi;
    /* RV */
    pusch_config_pdu->pusch_data.rv_index = dci->rv;
    /* HARQ_PROCESS_NUMBER */
    pusch_config_pdu->pusch_data.harq_process_id = pid;

    if (dci_format == NR_UL_DCI_FORMAT_0_0)
      add_pos = pusch_config_pdu->frequency_hopping ? pusch_dmrs_pos1 : pusch_dmrs_pos2;
    else if (NR_DMRS_ulconfig != NULL)
      add_pos = get_pusch_dmrs_pos(NR_DMRS_ulconfig->dmrs_AdditionalPosition);

    /* DMRS */
    l_prime_mask = get_l_prime(pusch_config_pdu->nr_of_symbols,
                               mappingtype, add_pos, dmrslength,
                               pusch_config_pdu->start_symbol_index,
                               mac->dmrs_TypeA_Position);

    if (sc_info->xOverhead_PUSCH)
      N_PRB_oh = 6 * (1 + *sc_info->xOverhead_PUSCH);
    else
      N_PRB_oh = 0;

    if (sc_info->rateMatching_PUSCH) {
      long maxMIMO_Layers = 0;
      if (sc_info->maxMIMO_Layers_PUSCH)
        maxMIMO_Layers = *sc_info->maxMIMO_Layers_PUSCH;
      else if (pusch_Config && pusch_Config->maxRank)
        maxMIMO_Layers = *pusch_Config->maxRank;
      else {
        if (pusch_Config && pusch_Config->txConfig) {
          if (*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook)
            maxMIMO_Layers = mac->uecap_maxMIMO_PUSCH_layers_cb;
          else
            maxMIMO_Layers = mac->uecap_maxMIMO_PUSCH_layers_nocb;
        } else
          maxMIMO_Layers = 1; // single antenna port
      }
      AssertFatal (maxMIMO_Layers > 0, "Invalid number of max MIMO layers for PUSCH\n");
      pusch_config_pdu->tbslbrm = nr_compute_tbslbrm(pusch_config_pdu->mcs_table, sc_info->ul_bw_tbslbrm, maxMIMO_Layers);
    } else
      pusch_config_pdu->tbslbrm = 0;

    /* PTRS */
    if (pusch_Config && pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB &&
        pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->phaseTrackingRS) {
      if (!tp_enabled) {
        nfapi_nr_ue_ptrs_ports_t ptrs_ports_list;
        pusch_config_pdu->pusch_ptrs.ptrs_ports_list = &ptrs_ports_list;
        bool valid_ptrs_setup = set_ul_ptrs_values(pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->phaseTrackingRS->choice.setup,
                                                   pusch_config_pdu->rb_size,
                                                   pusch_config_pdu->mcs_index,
                                                   pusch_config_pdu->mcs_table,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_freq_density,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_time_density,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_ports_list->ptrs_re_offset,
                                                   &pusch_config_pdu->pusch_ptrs.num_ptrs_ports,
                                                   &pusch_config_pdu->pusch_ptrs.ul_ptrs_power,
                                                   pusch_config_pdu->nr_of_symbols);
        if(valid_ptrs_setup == true) {
          pusch_config_pdu->pdu_bit_map |= PUSCH_PDU_BITMAP_PUSCH_PTRS;
        }
        LOG_D(NR_MAC, "UL PTRS values: PTRS time den: %d, PTRS freq den: %d\n",
              pusch_config_pdu->pusch_ptrs.ptrs_time_density, pusch_config_pdu->pusch_ptrs.ptrs_freq_density);
      }
    }
  }

  LOG_D(NR_MAC,
        "Received UL grant (rb_start %d, rb_size %d, start_symbol_index %d, nr_of_symbols %d) for RNTI type %s \n",
        pusch_config_pdu->rb_start,
        pusch_config_pdu->rb_size,
        pusch_config_pdu->start_symbol_index,
        pusch_config_pdu->nr_of_symbols,
        rnti_types(rnti_type));

  pusch_config_pdu->ul_dmrs_symb_pos = l_prime_mask;
  pusch_config_pdu->qam_mod_order = nr_get_Qm_ul(pusch_config_pdu->mcs_index, pusch_config_pdu->mcs_table);
  if (pusch_config_pdu->qam_mod_order == 0) {
    LOG_W(NR_MAC, "Invalid Mod order, likely due to unexpected UL DCI. Ignoring DCI! \n");
    mac->stats.bad_dci++;
    return -1;
  }

  int start_symbol = pusch_config_pdu->start_symbol_index;
  int number_of_symbols = pusch_config_pdu->nr_of_symbols;
  int number_dmrs_symbols = count_bits64_with_mask(pusch_config_pdu->ul_dmrs_symb_pos, start_symbol, number_of_symbols);
  int nb_dmrs_re_per_rb =
      ((pusch_config_pdu->dmrs_config_type == pusch_dmrs_type1) ? 6 : 4) * pusch_config_pdu->num_dmrs_cdm_grps_no_data;

  // Compute TBS
  uint16_t R = nr_get_code_rate_ul(pusch_config_pdu->mcs_index, pusch_config_pdu->mcs_table);
  const int pid = pusch_config_pdu->pusch_data.harq_process_id;
  if (R > 0) {
    pusch_config_pdu->target_code_rate = R;
    pusch_config_pdu->pusch_data.tb_size = nr_compute_tbs(pusch_config_pdu->qam_mod_order,
                                                          R,
                                                          pusch_config_pdu->rb_size,
                                                          pusch_config_pdu->nr_of_symbols,
                                                          nb_dmrs_re_per_rb * number_dmrs_symbols,
                                                          N_PRB_oh,
                                                          0, // TBR to verify tb scaling
                                                          pusch_config_pdu->nrOfLayers) >> 3;
    mac->ul_harq_info[pid].TBS = pusch_config_pdu->pusch_data.tb_size;
    mac->ul_harq_info[pid].R = R;
  } else {
    pusch_config_pdu->target_code_rate = mac->ul_harq_info[pid].R;
    pusch_config_pdu->pusch_data.tb_size = mac->ul_harq_info[pid].TBS;
  }

  /* TPC_PUSCH */
  int delta_pusch = 0;
  if (dci) {
    bool stateless_pusch_power_control = mac->current_UL_BWP->pusch_Config != NULL
                                        && mac->current_UL_BWP->pusch_Config->pusch_PowerControl != NULL
                                        && mac->current_UL_BWP->pusch_Config->pusch_PowerControl->tpc_Accumulation != NULL;
    int table_38_213_7_1_1_1[2][4] = {{-1, 0, 1, 3}, {-4, -1, 1, 4}};
    if (stateless_pusch_power_control) {
      delta_pusch = table_38_213_7_1_1_1[1][dci->tpc];
    } else {
      // TODO: This is not entirely correct. In case there is a prevously scheduled PUSCH for a future slot
      // we should apply its TPC now.
      delta_pusch = table_38_213_7_1_1_1[0][dci->tpc];
    }
  }
  delta_pusch = 0; // set to 0 as a workaround for PHY not applying PUSCH tx power

  bool is_rar_tx_retx = rnti_type == TYPE_TC_RNTI_;
  bool tp_enabled = pusch_config_pdu->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled;
  pusch_config_pdu->tx_power = get_pusch_tx_power_ue(mac,
                                                     pusch_config_pdu->rb_size,
                                                     pusch_config_pdu->rb_start,
                                                     pusch_config_pdu->nr_of_symbols,
                                                     nb_dmrs_re_per_rb * number_dmrs_symbols,
                                                     0, // TODO: count PTRS per RB
                                                     pusch_config_pdu->qam_mod_order,
                                                     pusch_config_pdu->target_code_rate,
                                                     pusch_config_pdu->pusch_uci.beta_offset_csi1,
                                                     pusch_config_pdu->pusch_data.tb_size << 3,
                                                     delta_pusch,
                                                     is_rar_tx_retx,
                                                     tp_enabled);

  pusch_config_pdu->ldpcBaseGraph = get_BG(pusch_config_pdu->pusch_data.tb_size << 3, pusch_config_pdu->target_code_rate);

  // The MAC entity shall restart retxBSR-Timer upon reception of a grant for transmission of new data on any UL-SCH
  if (pusch_config_pdu->pusch_data.new_data_indicator && dci && dci->ulsch_indicator)
    nr_timer_start(&mac->scheduling_info.retxBSR_Timer);

  if (pusch_config_pdu->pusch_data.tb_size == 0) {
    LOG_E(MAC, "Invalid TBS = 0. Probably caused by missed detection of DCI\n");
    mac->stats.bad_dci++;
    return -1;
  }

  // 38.321 5.4.6
  //  if it is the first UL resource allocated for a new transmission since the last MAC reset:
  //  2> start phr-PeriodicTimer;
  if (mac->scheduling_info.phr_info.is_configured) {
    if (mac->scheduling_info.phr_info.was_mac_reset && pusch_config_pdu->pusch_data.new_data_indicator) {
      mac->scheduling_info.phr_info.was_mac_reset = false;
      nr_timer_start(&mac->scheduling_info.phr_info.periodicPHR_Timer);
    }
  }
  if (mac->ul_harq_info[pid].round < sizeofArray(mac->stats.ul.rounds))
    mac->stats.ul.rounds[mac->ul_harq_info[pid].round]++;
  int bits = pusch_config_pdu->pusch_data.tb_size;
  mac->stats.ul.total_bits += bits;
  mac->stats.ul.target_code_rate += pusch_config_pdu->target_code_rate * bits;
  mac->stats.ul.total_symbols += bits / pusch_config_pdu->qam_mod_order;
  mac->stats.ul.rb_size += pusch_config_pdu->rb_size;
  mac->stats.ul.nr_of_symbols += pusch_config_pdu->nr_of_symbols;

  return 0;
}

nfapi_nr_ue_csi_payload_t nr_ue_aperiodic_csi_reporting(NR_UE_MAC_INST_t *mac, dci_field_t csi_request, int tda, long *k2)
{
  NR_CSI_AperiodicTriggerStateList_t *aperiodicTriggerStateList = mac->sc_info.aperiodicTriggerStateList;
  AssertFatal(aperiodicTriggerStateList, "Received CSI request via DCI but aperiodicTriggerStateList is not present\n");
  int n_states = aperiodicTriggerStateList->list.count;
  int n_ts = csi_request.nbits;
  nfapi_nr_ue_csi_payload_t csi = {0};
  AssertFatal(n_states <= ((1 << n_ts) - 1), "Case of subselection indication of trigger states not supported yet\n");
  int num_trig = 0;
  for (int i = 0; i < n_ts; i++) {
    // A non-zero codepoint of the CSI request field in the DCI is mapped to a CSI triggering state
    // according to the order of the associated positions of the up to (2^n_ts -1) trigger states
    // in CSI-AperiodicTriggerStateList with codepoint 1 mapped to the triggering state in the first position
    if (csi_request.val & (1 << i)) {
      AssertFatal(num_trig == 0, "Multiplexing more than 1 CSI report is not supported\n");
      NR_CSI_AperiodicTriggerState_t *trigger_state = aperiodicTriggerStateList->list.array[i];
      AssertFatal(trigger_state->associatedReportConfigInfoList.list.count == 1,
                  "Cannot handle more than 1 report configuration per state\n");
      NR_CSI_AssociatedReportConfigInfo_t *reportconfig = trigger_state->associatedReportConfigInfoList.list.array[0];
      NR_CSI_ReportConfigId_t id = reportconfig->reportConfigId;
      NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;
      int found = -1;
      for (int c = 0; c < csi_measconfig->csi_ReportConfigToAddModList->list.count; c++) {
        NR_CSI_ReportConfig_t *report_config = csi_measconfig->csi_ReportConfigToAddModList->list.array[c];
        if (report_config->reportConfigId == id) {
          struct NR_CSI_ReportConfig__reportConfigType__aperiodic__reportSlotOffsetList *offset_list = &report_config->reportConfigType.choice.aperiodic->reportSlotOffsetList;
          AssertFatal(tda < offset_list->list.count, "TDA index from DCI %d exceeds slot offset list %d\n", tda, offset_list->list.count);
          if (*k2 < *offset_list->list.array[tda])
            *k2 = *offset_list->list.array[tda];
          found = c;
          break;
        }
      }
      AssertFatal(found >= 0, "Couldn't find csi-ReportConfig with ID %ld\n", id);
      num_trig++;
      csi = nr_get_csi_payload(mac, found, ON_PUSCH, csi_measconfig);
    }
  }
  return csi;
}

int configure_srs_pdu(NR_UE_MAC_INST_t *mac,
                      NR_SRS_Resource_t *srs_resource,
                      fapi_nr_ul_config_srs_pdu *srs_config_pdu,
                      int period,
                      int offset,
                      NR_SRS_ResourceSet_t *srs_resource_set)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  srs_config_pdu->rnti = mac->crnti;
  srs_config_pdu->handle = 0;
  srs_config_pdu->bwp_size = current_UL_BWP->BWPSize;
  srs_config_pdu->bwp_start = current_UL_BWP->BWPStart;
  srs_config_pdu->subcarrier_spacing = current_UL_BWP->scs;
  srs_config_pdu->cyclic_prefix = 0;
  srs_config_pdu->num_ant_ports = srs_resource->nrofSRS_Ports;
  srs_config_pdu->num_symbols = srs_resource->resourceMapping.nrofSymbols;
  srs_config_pdu->num_repetitions = srs_resource->resourceMapping.repetitionFactor;
  srs_config_pdu->time_start_position = NR_SYMBOLS_PER_SLOT - 1 - srs_resource->resourceMapping.startPosition;
  srs_config_pdu->config_index = srs_resource->freqHopping.c_SRS;
  srs_config_pdu->sequence_id = srs_resource->sequenceId;
  srs_config_pdu->bandwidth_index = srs_resource->freqHopping.b_SRS;
  srs_config_pdu->comb_size = srs_resource->transmissionComb.present - 1;

  switch(srs_resource->transmissionComb.present) {
    case NR_SRS_Resource__transmissionComb_PR_n2:
      srs_config_pdu->comb_offset = srs_resource->transmissionComb.choice.n2->combOffset_n2;
      srs_config_pdu->cyclic_shift = srs_resource->transmissionComb.choice.n2->cyclicShift_n2;
      break;
    case NR_SRS_Resource__transmissionComb_PR_n4:
      srs_config_pdu->comb_offset = srs_resource->transmissionComb.choice.n4->combOffset_n4;
      srs_config_pdu->cyclic_shift = srs_resource->transmissionComb.choice.n4->cyclicShift_n4;
      break;
    default:
      LOG_W(NR_MAC, "Invalid or not implemented comb_size!\n");
  }

  srs_config_pdu->frequency_position = srs_resource->freqDomainPosition;
  srs_config_pdu->frequency_shift = srs_resource->freqDomainShift;
  srs_config_pdu->frequency_hopping = srs_resource->freqHopping.b_hop;
  srs_config_pdu->group_or_sequence_hopping = srs_resource->groupOrSequenceHopping;
  srs_config_pdu->resource_type = srs_resource->resourceType.present - 1;
  if(srs_config_pdu->resource_type > 0) { // not aperiodic
    srs_config_pdu->t_srs = period;
    srs_config_pdu->t_offset = offset;
  }

  bool is_configured_for_pusch_on_current_bwp = true;
  int delta_srs = 0; // DCI format 2_3 not implemented
  srs_config_pdu->tx_power =
      get_srs_tx_power_ue(mac, srs_resource, srs_resource_set, delta_srs, is_configured_for_pusch_on_current_bwp);

#ifdef SRS_DEBUG
  LOG_I(NR_MAC,"Frame = %i, slot = %i\n", frame, slot);
  LOG_I(NR_MAC,"srs_config_pdu->rnti = 0x%04x\n", srs_config_pdu->rnti);
  LOG_I(NR_MAC,"srs_config_pdu->handle = %u\n", srs_config_pdu->handle);
  LOG_I(NR_MAC,"srs_config_pdu->bwp_size = %u\n", srs_config_pdu->bwp_size);
  LOG_I(NR_MAC,"srs_config_pdu->bwp_start = %u\n", srs_config_pdu->bwp_start);
  LOG_I(NR_MAC,"srs_config_pdu->subcarrier_spacing = %u\n", srs_config_pdu->subcarrier_spacing);
  LOG_I(NR_MAC,"srs_config_pdu->cyclic_prefix = %u (0: Normal; 1: Extended)\n", srs_config_pdu->cyclic_prefix);
  LOG_I(NR_MAC,"srs_config_pdu->num_ant_ports = %u (0 = 1 port, 1 = 2 ports, 2 = 4 ports)\n", srs_config_pdu->num_ant_ports);
  LOG_I(NR_MAC,"srs_config_pdu->num_symbols = %u (0 = 1 symbol, 1 = 2 symbols, 2 = 4 symbols)\n", srs_config_pdu->num_symbols);
  LOG_I(NR_MAC,"srs_config_pdu->num_repetitions = %u (0 = 1, 1 = 2, 2 = 4)\n", srs_config_pdu->num_repetitions);
  LOG_I(NR_MAC,"srs_config_pdu->time_start_position = %u\n", srs_config_pdu->time_start_position);
  LOG_I(NR_MAC,"srs_config_pdu->config_index = %u\n", srs_config_pdu->config_index);
  LOG_I(NR_MAC,"srs_config_pdu->sequence_id = %u\n", srs_config_pdu->sequence_id);
  LOG_I(NR_MAC,"srs_config_pdu->bandwidth_index = %u\n", srs_config_pdu->bandwidth_index);
  LOG_I(NR_MAC,"srs_config_pdu->comb_size = %u (0 = comb size 2, 1 = comb size 4, 2 = comb size 8)\n", srs_config_pdu->comb_size);
  LOG_I(NR_MAC,"srs_config_pdu->comb_offset = %u\n", srs_config_pdu->comb_offset);
  LOG_I(NR_MAC,"srs_config_pdu->cyclic_shift = %u\n", srs_config_pdu->cyclic_shift);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_position = %u\n", srs_config_pdu->frequency_position);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_shift = %u\n", srs_config_pdu->frequency_shift);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_hopping = %u\n", srs_config_pdu->frequency_hopping);
  LOG_I(NR_MAC,"srs_config_pdu->group_or_sequence_hopping = %u (0 = No hopping, 1 = Group hopping groupOrSequenceHopping, 2 = Sequence hopping)\n", srs_config_pdu->group_or_sequence_hopping);
  LOG_I(NR_MAC,"srs_config_pdu->resource_type = %u (0: aperiodic, 1: semi-persistent, 2: periodic)\n", srs_config_pdu->resource_type);
  LOG_I(NR_MAC,"srs_config_pdu->t_srs = %u\n", srs_config_pdu->t_srs);
  LOG_I(NR_MAC,"srs_config_pdu->t_offset = %u\n", srs_config_pdu->t_offset);
#endif
  return 0;
}

// Aperiodic SRS scheduling
void nr_ue_aperiodic_srs_scheduling(NR_UE_MAC_INST_t *mac, long resource_trigger, int frame, int slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP->srs_Config;

  if (!srs_config || !srs_config->srs_ResourceSetToAddModList) {
    LOG_E(NR_MAC, "DCI is triggering aperiodic SRS but there is no SRS configuration\n");
    return;
  }

  int slot_offset = 0;
  NR_SRS_Resource_t *srs_resource = NULL;
  NR_SRS_ResourceSet_t *srs_resource_set = NULL;
  for(int rs = 0; rs < srs_config->srs_ResourceSetToAddModList->list.count; rs++) {

    // Find aperiodic resource set
    srs_resource_set = srs_config->srs_ResourceSetToAddModList->list.array[rs];
    if(srs_resource_set->resourceType.present != NR_SRS_ResourceSet__resourceType_PR_aperiodic)
      continue;
    // the resource trigger need to match the DCI one
    if(srs_resource_set->resourceType.choice.aperiodic->aperiodicSRS_ResourceTrigger != resource_trigger)
      continue;
    // if slotOffset is null -> offset = 0
    if(srs_resource_set->resourceType.choice.aperiodic->slotOffset)
      slot_offset = *srs_resource_set->resourceType.choice.aperiodic->slotOffset;

    // Find the corresponding srs resource
    for(int r1 = 0; r1 < srs_resource_set->srs_ResourceIdList->list.count; r1++) {
      for (int r2 = 0; r2 < srs_config->srs_ResourceToAddModList->list.count; r2++) {
        if ((*srs_resource_set->srs_ResourceIdList->list.array[r1] == srs_config->srs_ResourceToAddModList->list.array[r2]->srs_ResourceId) &&
            (srs_config->srs_ResourceToAddModList->list.array[r2]->resourceType.present == NR_SRS_Resource__resourceType_PR_aperiodic)) {
          srs_resource = srs_config->srs_ResourceToAddModList->list.array[r2];
          break;
        }
      }
    }
  }

  if(srs_resource == NULL) {
    LOG_E(NR_MAC, "Couldn't find SRS aperiodic resource with trigger %ld\n", resource_trigger);
    return;
  }

  int scs = mac->current_UL_BWP->scs;
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, scs);
  const int effective_slot_offset = slot_offset + ntn_ue_koffset;
  AssertFatal(effective_slot_offset > GET_DURATION_RX_TO_TX(mac, scs),
              "Slot offset between DCI and aperiodic SRS (%d) needs to be higher than DURATION_RX_TO_TX (%ld)\n",
              effective_slot_offset,
              GET_DURATION_RX_TO_TX(mac, scs));
  int n_slots_frame = mac->frame_structure.numb_slots_frame;
  int sched_slot = (slot + effective_slot_offset) % n_slots_frame;
  if (!is_ul_slot(sched_slot, &mac->frame_structure)) {
    LOG_E(NR_MAC, "Slot for scheduling aperiodic SRS %d is not an UL slot\n", sched_slot);
    return;
  }
  int add_frame = (slot + effective_slot_offset) / n_slots_frame;
  int sched_frame = (frame + add_frame) % MAX_FRAME_NUMBER;
  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, sched_frame, sched_slot, FAPI_NR_UL_CONFIG_TYPE_SRS);
  if (!pdu)
    return;
  LOG_D(NR_MAC, "Scheduling transmission of aperiodic SRS in %d.%d\n", sched_frame, sched_slot);
  int ret = configure_srs_pdu(mac, srs_resource, &pdu->srs_config_pdu, 0, 0, srs_resource_set);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
}


// Periodic SRS scheduling
static bool nr_ue_periodic_srs_scheduling(NR_UE_MAC_INST_t *mac, frame_t frame, slot_t slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP ? current_UL_BWP->srs_Config : NULL;

  if (!srs_config || !srs_config->srs_ResourceSetToAddModList) {
    return false;
  }

  bool srs_scheduled = false;
  NR_SRS_ResourceSet_t *srs_resource_set = NULL;
  for(int rs = 0; rs < srs_config->srs_ResourceSetToAddModList->list.count; rs++) {
    // Find periodic resource set
    srs_resource_set = srs_config->srs_ResourceSetToAddModList->list.array[rs];
    if(srs_resource_set->resourceType.present != NR_SRS_ResourceSet__resourceType_PR_periodic) {
      continue;
    }

    // Find the corresponding srs resource
    NR_SRS_Resource_t *srs_resource = NULL;
    for(int r1 = 0; r1 < srs_resource_set->srs_ResourceIdList->list.count; r1++) {
      for (int r2 = 0; r2 < srs_config->srs_ResourceToAddModList->list.count; r2++) {
        if ((*srs_resource_set->srs_ResourceIdList->list.array[r1] == srs_config->srs_ResourceToAddModList->list.array[r2]->srs_ResourceId) &&
            (srs_config->srs_ResourceToAddModList->list.array[r2]->resourceType.present == NR_SRS_Resource__resourceType_PR_periodic)) {
          srs_resource = srs_config->srs_ResourceToAddModList->list.array[r2];
          break;
        }
      }
    }

    if(srs_resource == NULL) {
      continue;
    }

    uint16_t period = srs_period[srs_resource->resourceType.choice.periodic->periodicityAndOffset_p.present];
    int offset = get_nr_srs_offset(srs_resource->resourceType.choice.periodic->periodicityAndOffset_p);
    if (offset < 0)
      continue;
    int n_slots_frame = mac->frame_structure.numb_slots_frame;

    // Check if UE should transmit the SRS
    if (is_periodic_occasion(frame, slot, n_slots_frame, period, offset)) {
      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame, slot, FAPI_NR_UL_CONFIG_TYPE_SRS);
      if (!pdu)
        return false;
      int ret = configure_srs_pdu(mac, srs_resource, &pdu->srs_config_pdu, period, offset, srs_resource_set);
      if (ret != 0)
        remove_ul_config_last_item(pdu);
      else
        srs_scheduled = true;
      release_ul_config(pdu, false);
    }
  }
  return srs_scheduled;
}

static void schedule_ta_command(fapi_nr_dl_config_request_t *dl_config, NR_UE_MAC_INST_t *mac)
{
  NR_UL_TIME_ALIGNMENT_t *ul_time_alignment = &mac->ul_time_alignment;
  fapi_nr_ta_command_pdu *ta = &dl_config->dl_config_list[dl_config->number_pdus].ta_command_pdu;
  ta->ta_frame = ul_time_alignment->frame;
  ta->ta_slot = ul_time_alignment->slot;
  ta->is_rar = ul_time_alignment->ta_apply == rar_ta;
  ta->ta_command = ul_time_alignment->ta_command;
  dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_CONFIG_TA_COMMAND;
  dl_config->number_pdus += 1;
  ul_time_alignment->ta_apply = no_ta;
}

static NR_CSI_ResourceConfigId_t find_CSI_resourceconfig(NR_CSI_MeasConfig_t *csi_measconfig,
                                                         NR_BWP_Id_t dl_bwp_id,
                                                         NR_NZP_CSI_RS_ResourceId_t csi_id,
                                                         bool *is_last_res)
{
  bool found = false;
  for (int csi_list = 0; csi_list < csi_measconfig->csi_ResourceConfigToAddModList->list.count; csi_list++) {
    NR_CSI_ResourceConfig_t *csires = csi_measconfig->csi_ResourceConfigToAddModList->list.array[csi_list];
    if(csires->bwp_Id != dl_bwp_id)
      continue;
    struct NR_CSI_ResourceConfig__csi_RS_ResourceSetList *resset = &csires->csi_RS_ResourceSetList;
    if(resset->present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB)
      continue;
    if(!resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList)
      continue;
    for(int i = 0; i < resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.count; i++) {
      NR_NZP_CSI_RS_ResourceSetId_t *res_id = resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[i];
      AssertFatal(res_id, "NR_NZP_CSI_RS_ResourceSetId shouldn't be NULL\n");
      struct NR_CSI_MeasConfig__nzp_CSI_RS_ResourceSetToAddModList *res_list = csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList;
      AssertFatal(res_list, "nzp_CSI_RS_ResourceSetToAddModList shouldn't be NULL\n");
      for (int j = 0; j < res_list->list.count; j++) {
        NR_NZP_CSI_RS_ResourceSet_t *csi_res = res_list->list.array[j];
        if (*res_id != csi_res->nzp_CSI_ResourceSetId)
          continue;
        for (int k = 0; k < csi_res->nzp_CSI_RS_Resources.list.count; k++) {
          AssertFatal(csi_res->nzp_CSI_RS_Resources.list.array[k], "NZP_CSI_RS_ResourceId shoulan't be NULL\n");
          if (csi_id == *csi_res->nzp_CSI_RS_Resources.list.array[k]) {
            found = true;
            *is_last_res = (k == (csi_res->nzp_CSI_RS_Resources.list.count - 1));
            break;
          }
        }
        if (found && csi_res->trs_Info)
          /* CRI-RS for Tracking. In this case there is no associated CSI report
           * therefore to signal this we return a value higher than
           * maxNrofCSI-ResourceConfigurations. */
          return NR_maxNrofCSI_ResourceConfigurations + 1;
        else if (found)
          return csires->csi_ResourceConfigId;
      }
    }
  }
  return -1; // not found any CSI-resource in current DL BWP associated with this CSI-RS ID
}

static uint8_t set_csirs_measurement_bitmap(NR_CSI_MeasConfig_t *csi_measconfig, NR_CSI_ResourceConfigId_t csi_res_id)
{
  uint8_t meas_bitmap = 0;
  if (csi_res_id > NR_maxNrofCSI_ResourceConfigurations)
    return meas_bitmap; // CSI-RS for tracking
  for(int i = 0; i < csi_measconfig->csi_ReportConfigToAddModList->list.count; i++) {
    NR_CSI_ReportConfig_t *report_config = csi_measconfig->csi_ReportConfigToAddModList->list.array[i];
    if(report_config->resourcesForChannelMeasurement != csi_res_id)
      continue;
    // bit 0 RSRP bit 1 RI bit 2 LI bit 3 PMI bit 4 CQI bit 5 i1
    switch (report_config->reportQuantity.present) {
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_PMI_CQI:
        meas_bitmap += (1 << 1) + (1 << 3) + (1 << 4);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1:
        meas_bitmap += (1 << 1) + (1 << 5);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1_CQI:
        meas_bitmap += (1 << 1) + (1 << 4) + (1 << 5);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_CQI:
        meas_bitmap += (1 << 1) + (1 << 4);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP:
        meas_bitmap += 1;
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_LI_PMI_CQI:
        meas_bitmap += (1 << 1) + (1 << 2) + (1 << 3) + (1 << 4);
        break;
      default:
        AssertFatal(false, "Unexpected measurement report type %d\n", report_config->reportQuantity.present);
    }
  }
  return meas_bitmap;
}

static void nr_schedule_csirs_reception(NR_UE_MAC_INST_t *mac, int frame, int slot)
{
  if (!mac->sc_info.csi_MeasConfig)
    return;

  NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

  if (csi_measconfig->nzp_CSI_RS_ResourceToAddModList == NULL)
    return;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_BWP_Id_t dl_bwp_id = current_DL_BWP->bwp_id;

  int mu = current_DL_BWP->scs;
  uint16_t bwp_size = current_DL_BWP->BWPSize;
  uint16_t bwp_start = current_DL_BWP->BWPStart;

  for (int id = 0; id < csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.count; id++) {
    NR_NZP_CSI_RS_Resource_t *nzpcsi = csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.array[id];
    int period = 0, offset = 0;
    if (!csi_period_offset(NULL, nzpcsi->periodicityAndOffset, &period, &offset) || period <= 0)
      continue;
    if (!is_periodic_occasion(frame, slot, mac->frame_structure.numb_slots_frame, period, offset))
      continue;
    bool is_last_res = false;
    NR_CSI_ResourceConfigId_t csi_res_id = find_CSI_resourceconfig(csi_measconfig, dl_bwp_id, nzpcsi->nzp_CSI_RS_ResourceId, &is_last_res);
    // do not schedule reseption of this CSI-RS if not associated with current BWP
    if(csi_res_id < 0)
      continue;
    LOG_D(MAC,"Scheduling reception of CSI-RS in frame %d slot %d\n", frame, slot);
    fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu = &dl_config->dl_config_list[dl_config->number_pdus].csirs_config_pdu.csirs_config_rel15;
    csirs_config_pdu->measurement_bitmap = set_csirs_measurement_bitmap(csi_measconfig, csi_res_id);
    csirs_config_pdu->subcarrier_spacing = mu;
    csirs_config_pdu->cyclic_prefix = current_DL_BWP->cyclicprefix ? *current_DL_BWP->cyclicprefix : 0;

    if (csi_res_id > NR_maxNrofCSI_ResourceConfigurations) {
      /* According to 38.214 5.1.6.1.1, the number of resources indicate if one
       * or two consequtive slots for TRS is used. We indicate to phy the last slot. */
      csirs_config_pdu->last_trs_slot = is_last_res;
      csirs_config_pdu->csi_type = 0; // TRS
    } else
      csirs_config_pdu->csi_type = 1; // NZP-CSI-RS

    csirs_config_pdu->scramb_id = nzpcsi->scramblingID;
    csirs_config_pdu->power_control_offset = nzpcsi->powerControlOffset + 8;
    if (nzpcsi->powerControlOffsetSS)
      csirs_config_pdu->power_control_offset_ss = *nzpcsi->powerControlOffsetSS;
    else
      csirs_config_pdu->power_control_offset_ss = 1; // 0 dB

    configure_csi_resource_mapping(csirs_config_pdu, &nzpcsi->resourceMapping, bwp_size, bwp_start);

    dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_DL_CONFIG_TYPE_CSI_RS;
    dl_config->number_pdus += 1;
  }
}

static void nr_schedule_csi_for_im(NR_UE_MAC_INST_t *mac, int frame, int slot)
{
  if (!mac->sc_info.csi_MeasConfig)
    return;

  NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

  if (csi_measconfig->csi_IM_ResourceToAddModList == NULL)
    return;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  NR_CSI_IM_Resource_t *imcsi;
  int period, offset;

  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  int mu = current_DL_BWP->scs;
  uint16_t bwp_size = current_DL_BWP->BWPSize;
  uint16_t bwp_start = current_DL_BWP->BWPStart;

  for (int id = 0; id < csi_measconfig->csi_IM_ResourceToAddModList->list.count; id++){
    imcsi = csi_measconfig->csi_IM_ResourceToAddModList->list.array[id];
    csi_period_offset(NULL,imcsi->periodicityAndOffset,&period,&offset);
    if (!is_periodic_occasion(frame, slot, mac->frame_structure.numb_slots_frame, period, offset))
      continue;
    fapi_nr_dl_config_csiim_pdu_rel15_t *csiim_config_pdu = &dl_config->dl_config_list[dl_config->number_pdus].csiim_config_pdu.csiim_config_rel15;
    csiim_config_pdu->bwp_size = bwp_size;
    csiim_config_pdu->bwp_start = bwp_start;
    csiim_config_pdu->subcarrier_spacing = mu;
    csiim_config_pdu->start_rb = imcsi->freqBand->startingRB;
    csiim_config_pdu->nr_of_rbs = imcsi->freqBand->nrofRBs;
    // As specified in 5.2.2.4 of 38.214
    switch (imcsi->csi_IM_ResourceElementPattern->present) {
      case NR_CSI_IM_Resource__csi_IM_ResourceElementPattern_PR_pattern0:
        for (int i = 0; i < 4; i++) {
          csiim_config_pdu->k_csiim[i] =
              (imcsi->csi_IM_ResourceElementPattern->choice.pattern0->subcarrierLocation_p0 << 1) + (i >> 1);
          csiim_config_pdu->l_csiim[i] = imcsi->csi_IM_ResourceElementPattern->choice.pattern0->symbolLocation_p0 + (i % 2);
        }
        break;
      case NR_CSI_IM_Resource__csi_IM_ResourceElementPattern_PR_pattern1:
        for (int i = 0; i < 4; i++) {
          csiim_config_pdu->k_csiim[i] = (imcsi->csi_IM_ResourceElementPattern->choice.pattern1->subcarrierLocation_p1 << 2) + i;
          csiim_config_pdu->l_csiim[i] = imcsi->csi_IM_ResourceElementPattern->choice.pattern1->symbolLocation_p1;
        }
        break;
      default:
        AssertFatal(1 == 0, "Invalid CSI-IM pattern\n");
    }
    dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_DL_CONFIG_TYPE_CSI_IM;
    dl_config->number_pdus += 1;
  }
}

// Performs :
// 1. TODO: Call RRC for link status return to PHY
// 2. TODO: Perform SR/BSR procedures for scheduling feedback
void nr_ue_dl_scheduler(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info)
{
  frame_t rx_frame = dl_info->frame;
  slot_t rx_slot = dl_info->slot;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, rx_slot);
  dl_config->sfn  = rx_frame;
  dl_config->slot = rx_slot;
  dl_config->number_pdus = 0;

  if (mac->state == UE_NOT_SYNC || mac->state == UE_NOT_SYNC_RECONF || mac->state == UE_DETACHING)
    return;

  if (mac->state == UE_CONNECTED) {
    nr_schedule_csirs_reception(mac, rx_frame, rx_slot);
    nr_schedule_csi_for_im(mac, rx_frame, rx_slot);
  }

  ue_dci_configuration(mac, dl_config, rx_frame, rx_slot);

  if (mac->ul_time_alignment.ta_apply != no_ta)
    schedule_ta_command(dl_config, mac);

  nr_scheduled_response_t scheduled_response = {.dl_config = dl_config,
                                                .module_id = mac->ue_id,
                                                .phy_data = dl_info->phy_data,
                                                .mac = mac};
  if (mac->if_module != NULL && mac->if_module->scheduled_response != NULL)
    mac->if_module->scheduled_response(&scheduled_response);
  else
    LOG_E(NR_MAC, "Internal error, no scheduled_response function\n");
}

static bool check_pucchres_for_pending_SR(NR_PUCCH_Config_t *pucch_Config, int target_sr_id)
{
  if (!pucch_Config || !pucch_Config->schedulingRequestResourceToAddModList)
    return false;
  for (int id = 0; id < pucch_Config->schedulingRequestResourceToAddModList->list.count; id++) {
    NR_SchedulingRequestResourceConfig_t *sr_Config = pucch_Config->schedulingRequestResourceToAddModList->list.array[id];
    if (sr_Config->schedulingRequestID == target_sr_id)  {
      if (sr_Config->resource) {
        return true;
      }
    }
  }
  return false;
}

void schedule_RA_after_SR_failure(NR_UE_MAC_INST_t *mac)
{
  if (get_softmodem_params()->phy_test)
    return; // cannot trigger RA in phytest mode
  trigger_MAC_UE_RA(mac, NULL);
  // release PUCCH for all Serving Cells;
  // release SRS for all Serving Cells;
  release_PUCCH_SRS(mac);
  // clear any configured downlink assignments and uplink grants;
  if (mac->dl_config_request)
    memset(mac->dl_config_request, 0, sizeof(*mac->dl_config_request));
  if (mac->ul_config_request)
    clear_ul_config_request(mac);
  // clear any PUSCH resources for semi-persistent CSI reporting
  // TODO we don't have semi-persistent CSI reporting
}

static void nr_update_sr(NR_UE_MAC_INST_t *mac, bool BSRsent)
{
  NR_UE_SCHEDULING_INFO *sched_info = &mac->scheduling_info;

  // if no pending data available for transmission
  // All pending SR(s) shall be cancelled and each respective sr-ProhibitTimer shall be stopped
  // in TS 38.321:
  // The SR configuration of the logical channel that triggered the BSR
  // (subclause 5.4.5) (if such a configuration exists) is considered as corresponding SR configuration for the triggered SR
  // in the hereafter code, we compute actual state at the time we compute the SR
  // So, it might be more accurate than the logical channel that triggered the BSR some slots ago if new data came in the UE for
  // ULSCH

  nr_lcordered_info_t *lc_info = NULL;
  for (int idx = 0; idx < mac->lc_ordered_list.count && lc_info == NULL; idx++) {
    nr_lcordered_info_t *lc_info_loop = mac->lc_ordered_list.array[idx];
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lc_info_loop->lcid);
    if (lc_sched_info->LCID_buffer_remain)
      lc_info = lc_info_loop;
  }

  if (lc_info == NULL) { // all queues are empty
    for (int i = 0; i < NR_MAX_SR_ID; i++) {
      nr_sr_info_t *sr = &sched_info->sr_info[i];
      if (sr->active_SR_ID) {
        LOG_D(NR_MAC, "No pending data available -> Canceling pending SRs\n");
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
      }
    }
    return;
  }

  // if a Regular BSR has been triggered and logicalChannelSR-DelayTimer is not running
  if (BSRsent || nr_timer_is_active(&sched_info->sr_DelayTimer))
    return;

  // if there is no UL-SCH resource available for a new transmission (ie we are at this point)
  // if the MAC entity is configured with configured uplink grant(s) and the Regular BSR was triggered for a
  // logical channel for which logicalChannelSR-Mask is set to false or
  if (mac->current_UL_BWP->configuredGrantConfig && lc_info->lc_SRMask)
    return;

  // if the UL-SCH resources available for a new transmission do not meet the LCP mapping restrictions
  // TODO not implemented

  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;

  if (lc_info->sr_id < 0 || lc_info->sr_id >= NR_MAX_SR_ID)
    LOG_E(NR_MAC, "No SR corresponding to this LCID\n"); // TODO not sure what to do here
  else {
    nr_sr_info_t *sr = &sched_info->sr_info[lc_info->sr_id];
    if (!sr->pending) {
      if (check_pucchres_for_pending_SR(pucch_Config, lc_info->sr_id)) {
        // trigger SR
        LOG_D(NR_MAC, "Triggering SR for ID %d\n", lc_info->sr_id);
        mac->sr_fallback_ra_triggered = false;
        sr->pending = true;
        sr->counter = 0;
      } else if (!mac->sr_fallback_ra_triggered && !mac->ra.ra_pucch) {
        // initiate a Random Access procedure on the SpCell and cancel the pending SR
        // if the MAC entity has no valid PUCCH resource configured for the pending SR
        // Wait until any pending Msg4/MsgB HARQ feedback PUCCH has been transmitted
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
        mac->sr_fallback_ra_triggered = true;
        schedule_RA_after_SR_failure(mac);
      }
    }
  }
}

static void nr_update_rlc_buffers_status(NR_UE_MAC_INST_t *mac, frame_t frameP, slot_t slotP)
{
  logical_chan_id_t ch[NR_MAX_NUM_LCID] = {0};
  int n = 0;
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
    if (lc_info->rb_suspended)
      continue;
    ch[n++] = lc_info->lcid;
  }

  mac_rlc_status_resp_t ret[NR_MAX_NUM_LCID] = {0};
  nr_mac_rlc_status_ind(mac->ue_id, frameP, n, ch, ret);
  for (int i = 0; i < n; ++i) {
    const logical_chan_id_t lcid = ch[i];
    const rlc_buffer_occupancy_t b = ret[i].bytes_in_buffer;
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
    if (b > 0)
      LOG_D(NR_MAC, "[UE %d] LCID %d has %d bytes to transmit at sfn %d.%d\n", mac->ue_id, lcid, b, frameP, slotP);
    lc_sched_info->LCID_buffer_remain = b;
  }
}

/*TS 38.321
A BSR shall be triggered if any of the following events occur:
- UL data, for a logical channel which belongs to an LCG, becomes available to the MAC entity; and either
   => here we don't implement exactly the same, there is no direct relation with new data came in the UE since last BSR

- this UL data belongs to a logical channel with higher priority than the priority of any logical channel
  containing available UL data which belong to any LCG; or
  => same, we don't know the last BSR content

- none of the logical channels which belong to an LCG contains any available UL data.
  in which case the BSR is referred below to as 'Regular BSR';

- UL resources are allocated and number of padding bits is equal to or larger than the size of the Buffer Status
Report MAC CE plus its subheader, in which case the BSR is referred below to as 'Padding BSR';

- retxBSR-Timer expires, and at least one of the logical channels which belong to an LCG contains UL data, in
which case the BSR is referred below to as 'Regular BSR';

- periodicBSR-Timer expires, in which case the BSR is referred below to as 'Periodic BSR'.

*/

static void nr_update_bsr(NR_UE_MAC_INST_t *mac, uint32_t *LCG_bytes)
{
  bool bsr_regular_triggered = mac->scheduling_info.BSR_reporting_active & NR_BSR_TRIGGER_REGULAR;
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
    if (lc_info->rb_suspended)
      continue;
    int lcid = lc_info->lcid;
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
    int lcgid = lc_sched_info->LCGID;
    // check if UL data for a logical channel which belongs to a LCG becomes available for transmission
    if (lcgid != NR_INVALID_LCGID) {
      // Update waiting bytes for this LCG
      LCG_bytes[lcgid] += lc_sched_info->LCID_buffer_remain;
      if (!bsr_regular_triggered) {
        bsr_regular_triggered = true;
        trigger_regular_bsr(mac, lcid, lc_info->sr_DelayTimerApplied);
        LOG_D(NR_MAC, "[UE %d] MAC BSR Triggered\n", mac->ue_id);
      }
    }
  }
}

static bool is_prach_frame(frame_t frame, prach_occasion_info_t *prach_occasion_info, int association_periods)
{
  int config_period = prach_occasion_info->frame_info[0];
  int frame_period = config_period * association_periods;
  int frame_in_period = frame % frame_period;
  if (association_periods > 1) {
    int current_assoc_period = frame_in_period / config_period;
    if (current_assoc_period != prach_occasion_info->association_period_idx)
      return false;
    frame_in_period %= config_period;
  }
  if (frame_in_period == prach_occasion_info->frame_info[1]) // SFN % x == y (see Tables in Table 6.3.3.2 of 38.211)
    return true;
  else
    return false;
}

// This function schedules the PRACH according to prach_ConfigurationIndex and TS 38.211, tables 6.3.3.2.x
// PRACH formats 9, 10, 11 are corresponding to dual PRACH format configurations A1/B1, A2/B2, A3/B3.
// - todo:
// - Partial configuration is actually already stored in (fapi_nr_prach_config_t) &mac->phy_config.config_req->prach_config
static void nr_ue_prach_scheduler(NR_UE_MAC_INST_t *mac, frame_t frameP, slot_t slotP)
{
  RA_config_t *ra = &mac->ra;

  // Get any valid PRACH occasion in the current slot for the selected SSB index
  prach_occasion_info_t *prach_occasion_info = &ra->sched_ro_info;

  if (!is_prach_frame(frameP, prach_occasion_info, ra->association_periods))
    return;

  if (is_ul_slot(slotP, &mac->frame_structure)) {
    if (slotP == prach_occasion_info->slot) {
      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frameP, slotP, FAPI_NR_UL_CONFIG_TYPE_PRACH);
      if (!pdu) {
        LOG_E(NR_MAC, "Error in PRACH allocation\n");
        return;
      }

      int format = prach_occasion_info->format;
      fapi_nr_prach_config_t *prach_config = &mac->phy_config.config_req.prach_config;
      pdu->prach_config_pdu = (fapi_nr_ul_config_prach_pdu){
          .phys_cell_id = mac->physCellId,
          .num_prach_ocas = 1,
          .prach_slot = prach_occasion_info->slot,
          .prach_start_symbol = prach_occasion_info->start_symbol,
          .num_ra = prach_occasion_info->fdm,
          .num_cs = get_NCS(ra->zeroCorrelationZoneConfig, format, ra->restricted_set_config),
          .root_seq_id = prach_config->num_prach_fd_occasions_list[prach_occasion_info->fdm].prach_root_sequence_index,
          .restricted_set = prach_config->restricted_set_config,
          .freq_msg1 = prach_config->num_prach_fd_occasions_list[prach_occasion_info->fdm].k1};

      LOG_I(NR_MAC,
            "PRACH scheduler: Selected RO Frame %u, Slot %u, Symbol %u, Fdm %u\n",
            frameP,
            pdu->prach_config_pdu.prach_slot,
            pdu->prach_config_pdu.prach_start_symbol,
            pdu->prach_config_pdu.num_ra);

      switch (format) { // single PRACH format
        case 0:
          pdu->prach_config_pdu.prach_format = 0;
          break;
        case 1:
          pdu->prach_config_pdu.prach_format = 1;
          break;
        case 2:
          pdu->prach_config_pdu.prach_format = 2;
          break;
        case 3:
          pdu->prach_config_pdu.prach_format = 3;
          break;
        case 0xa1:
          pdu->prach_config_pdu.prach_format = 4;
          break;
        case 0xa2:
          pdu->prach_config_pdu.prach_format = 5;
          break;
        case 0xa3:
          pdu->prach_config_pdu.prach_format = 6;
          break;
        case 0xb1:
          pdu->prach_config_pdu.prach_format = 7;
          break;
        case 0xb4:
          pdu->prach_config_pdu.prach_format = 8;
          break;
        case 0xc0:
          pdu->prach_config_pdu.prach_format = 9;
          break;
        case 0xc2:
          pdu->prach_config_pdu.prach_format = 10;
          break;
        default:
          AssertFatal(false, "Invalid PRACH format");
      }

      pdu->prach_config_pdu.ra_PreambleIndex = ra->ra_PreambleIndex;
      pdu->prach_config_pdu.prach_tx_power = get_prach_tx_power(mac);
      release_ul_config(pdu, false);
      nr_scheduled_response_t scheduled_response = {.ul_config = mac->ul_config_request + slotP,
                                                    .mac = mac,
                                                    .module_id = mac->ue_id};
      if(mac->if_module != NULL && mac->if_module->scheduled_response != NULL)
        mac->if_module->scheduled_response(&scheduled_response);

      T(T_UE_PHY_INITIATE_RA_PROCEDURE, T_INT(frameP), T_INT(pdu->prach_config_pdu.prach_slot),
        T_INT(pdu->prach_config_pdu.ra_PreambleIndex), T_INT(pdu->prach_config_pdu.prach_tx_power));

      const int n_slots_frame = mac->frame_structure.numb_slots_frame;
      if (ra->ra_type == RA_4_STEP) {
        ra->ra_state = nrRA_WAIT_RAR;
        // we start to monitor DCI for RAR in the first valid occasion after transmitting RACH
        // the response window timer should be started at that time
        // but for processing reasons it is better to start it here and to add the slot difference
        // that also takes into account the rx to tx slot offset
        int next_slot = (slotP + 1) % n_slots_frame;
        int next_frame = (frameP + (next_slot < slotP)) % MAX_FRAME_NUMBER;
        int add_slots = 1;
        NR_BWP_PDCCH_t *pdcch_config = &mac->config_BWP_PDCCH[mac->current_DL_BWP->bwp_id];
        const NR_SearchSpace_t *ra_SS = get_common_search_space(mac, pdcch_config->ra_SS_id);
        while (!is_dl_slot(next_slot, &mac->frame_structure)
               || !is_ss_monitor_occasion(next_frame, next_slot, n_slots_frame, ra_SS)) {
          int temp_slot = (next_slot + 1) % n_slots_frame;
          next_frame = (next_frame + (temp_slot < next_slot)) % MAX_FRAME_NUMBER;
          next_slot = temp_slot;
          add_slots++;
        }
        add_slots += GET_DURATION_RX_TO_TX(mac, mac->current_DL_BWP->scs);
        nr_timer_setup(&ra->response_window_timer, ra->response_window_setup_time + add_slots, 1);
        nr_timer_start(&ra->response_window_timer);
      } else if (ra->ra_type == RA_2_STEP) {
        NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource =
            mac->current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
        slot_t msgA_pusch_slot = (slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) % n_slots_frame;
        frame_t msgA_pusch_frame =
            (frameP + ((slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) / n_slots_frame)) % 1024;

        fapi_nr_ul_config_request_pdu_t *pdu =
            lockGet_ul_config(mac, msgA_pusch_frame, msgA_pusch_slot, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
        if (!pdu)
          return;

        // Config Msg3/MsgA-PUSCH PDU
        NR_tda_info_t tda_info = {0};
        AssertFatal(msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16,
                    "Only SLIV based on startSymbolAndLengthMsgA_PO_r16 implemented\n");

        int ret = nr_config_pusch_pdu(mac,
                                      &tda_info,
                                      &pdu->pusch_config_pdu,
                                      NULL,
                                      NULL,
                                      NULL,
                                      mac->ra.ra_rnti,
                                      NR_SearchSpace__searchSpaceType_PR_common,
                                      NR_DCI_NONE);

        if (ret != 0)
          remove_ul_config_last_item(pdu);
        release_ul_config(pdu, false);

        LOG_D(NR_MAC, "ra->ra_state %s\n", nrra_ue_text[ra->ra_state]);
        ra->ra_state = nrRA_WAIT_MSGB;
        ra->t_crnti = 0;
        mac->crnti = 0;
        ra->ra_rnti = 0;

        // start MsgB response window timer
        int next_slot = (slotP + 1) % n_slots_frame;
        int next_frame = (frameP + (next_slot < slotP)) % MAX_FRAME_NUMBER;
        int add_slots = 1;
        NR_BWP_PDCCH_t *pdcch_config = &mac->config_BWP_PDCCH[mac->current_DL_BWP->bwp_id];
        const NR_SearchSpace_t *ra_SS = get_common_search_space(mac, pdcch_config->ra_SS_id);
        int max_slots = n_slots_frame * MAX_FRAME_NUMBER;
        int i = 0;
        for (i = 0; i < max_slots
                    && (!is_dl_slot(next_slot, &mac->frame_structure)
                        || !is_ss_monitor_occasion(next_frame, next_slot, n_slots_frame, ra_SS));
             i++) {
          int temp_slot = (next_slot + 1) % n_slots_frame;
          next_frame = (next_frame + (temp_slot < next_slot)) % MAX_FRAME_NUMBER;
          next_slot = temp_slot;
          add_slots++;
        }
        AssertFatal(i < max_slots,
                    "No DL slot matches RA search space %ld monitoring occasion (check TDD / ra-SearchSpace config)\n",
                    ra_SS->searchSpaceId);

        add_slots += GET_DURATION_RX_TO_TX(mac, mac->current_DL_BWP->scs);
        nr_timer_setup(&ra->response_window_timer, ra->response_window_setup_time + add_slots, 1);
        nr_timer_start(&ra->response_window_timer);
      } else {
        AssertFatal(false, "RA type %d not implemented!\n", ra->ra_type);
      }
    } // is_nr_prach_slot
  } // if is_nr_UL_slot
}

/** @brief Schedule UCI from an overlapping PUCCH onto PUSCH (TS 38.213 clause 9.2.5.3) */
static bool schedule_uci_on_pusch(NR_UE_MAC_INST_t *mac,
                                  frame_t frame_tx,
                                  int slot_tx,
                                  const PUCCH_sched_t *pucch,
                                  NR_UE_UL_BWP_t *current_UL_BWP)
{
  fapi_nr_ul_config_request_pdu_t *ulcfg_pdu = lockGet_ul_iterator(mac, frame_tx, slot_tx);
  if (!ulcfg_pdu)
    return NULL;

  nfapi_nr_ue_pusch_pdu_t *pusch_pdu = NULL;
  while (ulcfg_pdu->pdu_type != FAPI_NR_END) {
    if (ulcfg_pdu->pdu_type == FAPI_NR_UL_CONFIG_TYPE_PUSCH) {
      int start_pusch = ulcfg_pdu->pusch_config_pdu.start_symbol_index;
      int nsymb_pusch = ulcfg_pdu->pusch_config_pdu.nr_of_symbols;
      int end_pusch = start_pusch + nsymb_pusch;
      NR_PUCCH_Resource_t *pucchres = pucch->pucch_resource;
      int nr_of_symbols = 0;
      int start_symbol_index = 0;
      if (pucch->initial_pucch_id > -1 && pucch->pucch_resource == NULL) {
        // Common HARQ: symbols from Table 9.2.1-1 row on the occasion (frozen at DCI)
        const initial_pucch_resource_t pucch_resourcecommon = get_initial_pucch_resource(pucch->pucch_ResourceCommon);
        start_symbol_index = pucch_resourcecommon.startingSymbolIndex;
        nr_of_symbols = pucch_resourcecommon.nrofSymbols;
      }
      else {
        switch(pucchres->format.present) {
          case NR_PUCCH_Resource__format_PR_format0 :
            nr_of_symbols = pucchres->format.choice.format0->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format0->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format1 :
            nr_of_symbols = pucchres->format.choice.format1->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format1->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format2 :
            nr_of_symbols = pucchres->format.choice.format2->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format2->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format3 :
            nr_of_symbols = pucchres->format.choice.format3->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format3->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format4 :
            nr_of_symbols = pucchres->format.choice.format4->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format4->startingSymbolIndex;
            break;
          default :
            AssertFatal(false, "Undefined PUCCH format \n");
        }
      }
      int final_symbol = nr_of_symbols + start_symbol_index;
      // PUCCH overlapping in time with PUSCH
      if (start_symbol_index < end_pusch && final_symbol > start_pusch) {
        pusch_pdu = &ulcfg_pdu->pusch_config_pdu;
        break;
      }
    }
    ulcfg_pdu++;
  }

  if (!pusch_pdu) {
    release_ul_config(ulcfg_pdu, false);
    return false;
  }

  // If a UE would transmit on a serving cell a PUSCH without UL-SCH that overlaps with a PUCCH transmission
  // on a serving cell that includes positive SR information, the UE does not transmit the PUSCH.
  if (pusch_pdu && pusch_pdu->ulsch_indicator == 0 && pucch->sr_payload == 1) {
    release_ul_config(ulcfg_pdu, false);
    return false;
  }

  // Section 9 of 38.213 states:
  // - UE multiplexes only HARQ-ACK information, if any, from the UCI in the PUSCH transmission
  // and does not transmit the PUCCH if the UE multiplexes aperiodic or semi-persistent CSI reports in the PUSCH
  // - UE multiplexes only HARQ-ACK information and CSI reports, if any, from the UCI in the PUSCH transmission
  // and does not transmit the PUCCH if the UE does not multiplex aperiodic or semi-persistent CSI reports in the PUSCH

  // HARQ is then multiplexed on PUSCH in both scenarios
  bool mux_done = false;
  if (pucch->n_harq > 0) {
    NR_PUSCH_Config_t *pusch_Config = mac->current_UL_BWP->pusch_Config;
    if (pusch_Config && pusch_Config->uci_OnPUSCH) {
      NR_UCI_OnPUSCH_t *onPusch = pusch_Config->uci_OnPUSCH->choice.setup;
      AssertFatal(onPusch &&
                  onPusch->betaOffsets &&
                  onPusch->betaOffsets->present == NR_UCI_OnPUSCH__betaOffsets_PR_semiStatic,
                  "Only semistatic beta offset is supported\n");
      NR_BetaOffsets_t *beta_offsets = onPusch->betaOffsets->choice.semiStatic;
      pusch_pdu->pusch_uci.harq_ack_bit_length = pucch->n_harq;
      pusch_pdu->pusch_uci.harq_payload = pucch->ack_payload;
      pusch_pdu->pusch_uci.beta_offset_harq_ack = pucch->n_harq > 2 ?
                                                  (pucch->n_harq < 12 ? *beta_offsets->betaOffsetACK_Index2 :
                                                  *beta_offsets->betaOffsetACK_Index3) :
                                                  *beta_offsets->betaOffsetACK_Index1;
      pusch_pdu->pusch_uci.alpha_scaling = onPusch->scaling;
      mux_done = true;
    } else {
      LOG_E(NR_MAC, "UCI on PUSCH need to be configured to schedule UCI on PUSCH\n");
    }
  }

  // CSI, if present on PUCCH, is transmitted only if there is no aperiodic/semi-persistent CSI report already
  bool csi_present = (pusch_pdu->pusch_uci.csi_payload.p1_bits > 0) || (pusch_pdu->pusch_uci.csi_payload.p2_bits > 0);
  if (pucch->csi_payload.p1_bits > 0 && !csi_present) {
    nfapi_nr_ue_csi_payload_t csi_payload = {0};
    NR_PUSCH_Config_t *pusch_Config = mac->current_UL_BWP->pusch_Config;
    NR_PUCCH_Resource_t *csi_pucch = NULL;
    nr_get_csi_measurements(mac, frame_tx, slot_tx, &csi_payload, &csi_pucch);
    fill_pusch_uci_struct(pusch_Config, &csi_payload, pusch_pdu);
    mux_done = true;
  }

  release_ul_config(ulcfg_pdu, false);
  // only use PUSCH if any mux is done otherwise send PUCCH
  return mux_done;
}

static void nr_ue_pucch_scheduler(NR_UE_MAC_INST_t *mac, frame_t frame, int slot)
{
  PUCCH_sched_t pucch[3] = {0}; // TODO the size might change in the future in case of multiple SR or multiple CSI in a slot
  int num_res = 0;

  if (mac->ra.ra_pucch) {
    // scheduling PUCCH prepared in advance for MSG4
    RA_PUCCH_SCHED_t *ra_pucch = mac->ra.ra_pucch;
    if (ra_pucch->sched_frame == frame && ra_pucch->sched_slot == slot) {
      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame, slot, FAPI_NR_UL_CONFIG_TYPE_PUCCH);
      if (!pdu) {
        LOG_E(NR_MAC, "Error in pucch allocation\n");
        return;
      }
      pdu->pucch_config_pdu = ra_pucch->pucch_pdu;
      free_and_zero(mac->ra.ra_pucch);
      release_ul_config(pdu, false);
      return;
    }
  }

  // SR
  if (mac->state == UE_CONNECTED && trigger_periodic_scheduling_request(mac, &pucch[0], frame, slot)) {
    num_res++;
    // TODO check if the PUCCH resource for the SR transmission occasion overlap with a UL-SCH resource
  }

  // CSI
  int csi_res = 0;
  if (mac->state == UE_CONNECTED)
    csi_res = nr_get_csi_measurements(mac, frame, slot, &pucch[num_res].csi_payload, &pucch[num_res].pucch_resource);
  if (csi_res > 0) {
    num_res += csi_res;
  }

  // ACKNACK
  bool any_harq = get_downlink_ack(mac, frame, slot, &pucch[num_res]);
  if (any_harq)
    num_res++;

  if (num_res == 0)
    return;
  // do no transmit pucch if only SR scheduled and it is negative
  if (num_res == 1 && pucch[0].n_sr > 0 && pucch[0].sr_payload == 0)
    return;

  // Common HARQ (TS 38.213 clause 9.2.1, Table 9.2.1-1): keep only ACK in this slot, drop dedicated resources
  int common_harq = -1;
  for (int i = 0; i < num_res; i++) {
    if (pucch[i].pucch_resource == NULL && pucch[i].n_harq > 0) {
      common_harq = i;
      break;
    }
  }
  if (common_harq >= 0) {
    const PUCCH_sched_t harq = pucch[common_harq];
    memset(pucch, 0, sizeof(pucch));
    pucch[0] = harq;
    num_res = 1;
  } else if (num_res > 1) {
    // multiplex dedicated PUCCH-Resource (TS 38.213 clause 9.2.5.1 / 9.2.5.2)
    multiplex_pucch_resource(mac, pucch, num_res);
  }

  for (int j = 0; j < num_res; j++) {
    if (pucch[j].n_harq + pucch[j].n_sr + pucch[j].csi_payload.p1_bits != 0) {
      LOG_D(NR_MAC,
            "%d.%d configure pucch, O_ACK %d, O_SR %d, O_CSI %d\n",
            frame,
            slot,
            pucch[j].n_harq,
            pucch[j].n_sr,
            pucch[j].csi_payload.p1_bits);

      // checking if we need to schedule pucch[j] on PUSCH
      if (schedule_uci_on_pusch(mac, frame, slot, &pucch[j], mac->current_UL_BWP))
        continue;

      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame, slot, FAPI_NR_UL_CONFIG_TYPE_PUCCH);
      if (!pdu) {
        LOG_E(NR_MAC, "Error in pucch allocation\n");
        return;
      }
      int ret = nr_ue_configure_pucch(mac,
                                      slot,
                                      frame,
                                      mac->crnti, // FIXME not sure this is valid for all pucch instances
                                      &pucch[j],
                                      &pdu->pucch_config_pdu);
      if (ret != 0)
        remove_ul_config_last_item(pdu);
      release_ul_config(pdu, false);
    }
  }
}

static void nr_ue_fill_phr(NR_UE_MAC_INST_t *mac,
                           NR_SINGLE_ENTRY_PHR_MAC_CE *phr,
                           float P_CMAX,
                           float tx_power,
                           frame_t frame,
                           slot_t slot)
{
  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  // Value mapping according to 38.133 10.1.18.1
  const int PC_MAX_00 = -29;
  *phr = (NR_SINGLE_ENTRY_PHR_MAC_CE){.PCMAX = max(0, (int)P_CMAX - PC_MAX_00) & 0x3f};
  // Value mapping according to 38.133 10.1.17.1.1
  const int POWER_HEADROOM_55 = 22;
  int headroom = P_CMAX - tx_power;
  if (headroom < POWER_HEADROOM_55) {
    const int POWER_HEADROOM_0 = -32;
    phr->PH = max(0, headroom - POWER_HEADROOM_0);
  } else {
    phr->PH = min(0x3f, 55 + (headroom - POWER_HEADROOM_55) / 2);
  }

  LOG_D(NR_MAC,
        "PHR Reporting sfn.slot %d.%d reason = %d, P_CMAX = %d (%5.2f dBm), headrom = %d (%d dB) tx_power = %5.2f dBm\n",
        frame,
        slot,
        phr_info->phr_reporting,
        phr->PCMAX,
        P_CMAX,
        phr->PH,
        headroom,
        tx_power);

  phr_info->PathlossLastValue = compute_nr_SSB_PL(mac);
  // Restart both timers according to 38.321
  nr_timer_start(&phr_info->periodicPHR_Timer);
  nr_timer_start(&phr_info->prohibitPHR_Timer);
  phr_info->phr_reporting = 0;
}

typedef struct {
  uint8_t phr_len;
  uint sdu_length_total;
  NR_SINGLE_ENTRY_PHR_MAC_CE phr;
  type_bsr_t bsr;
  uint8_t *pdu_end;
  uint8_t *end_for_tailer;
  uint8_t *cur_ptr;
  uint num_sdus;
  // int highest_priority = 16;
  //  variable used to store the lcid data status during lcp
  bool lcids_data_status[NR_MAX_NUM_LCID];
  uint32_t lcp_allocation_counter;
} NR_UE_MAC_CE_INFO;

static uint8_t nr_locate_BsrIndexByBufferSize(int size, int value)
{
  if (value == 0) {
    return 0;   //elseif (value > 150000) return 63;
  }

  uint32_t (*get_bsr_value)(int);
  if (size == NR_SHORT_BSR_TABLE_SIZE)
    get_bsr_value = &get_short_bsr_value;
  else
    get_bsr_value = &get_long_bsr_value;

  int jl = 0; // lower bound
  int ju = size - 1; // upper bound
  // determine the order of the the table:  1 if ascending order of table, 0 otherwise
  bool ascend = (*get_bsr_value)(ju) >= (*get_bsr_value)(jl) ? 1 : 0;
  while (ju - jl > 1) { //If we are not yet done,
    int jm = (ju + jl) / 2; // compute a midpoint,
    if ((value >= (*get_bsr_value)(jm)) == ascend) {
      jl = jm;    // replace the lower limit
    } else {
      ju = jm;    //replace the upper limit
    }

    LOG_T(NR_MAC,
          "[UE] searching BSR index %d for (BSR TABLE %d < value %d)\n",
          jm,
          (*get_bsr_value)(jm),
          value);
  }

  if (value == (*get_bsr_value)(jl)) {
    return jl;
  } else {
    return jl + 1;    //equally  ju
  }
}

static void nr_ue_get_sdu_mac_ce_pre(NR_UE_MAC_INST_t *mac,
                                     frame_t frame,
                                     slot_t slot,
                                     uint8_t *ulsch_buffer,
                                     uint32_t buflen,
                                     uint32_t *LCG_bytes,
                                     NR_UE_MAC_CE_INFO *mac_ce_p,
                                     int tx_power,
                                     int P_CMAX)
{
  // Preparing the MAC CEs sub-PDUs and get the total size
  *mac_ce_p = (NR_UE_MAC_CE_INFO){.pdu_end = ulsch_buffer + buflen, .cur_ptr = ulsch_buffer};
  memset(mac_ce_p->lcids_data_status, 1, NR_MAX_NUM_LCID);

  // Compute BSR Length if Regular or Periodic BSR is triggered
  // If we decide here to send BSR long, we may decide later to use BSR short
  // if there remains less than 1 LCGROUP with data after Tx
  int bsr_len = 0;
  if (mac->scheduling_info.BSR_reporting_active) {
    // count the active channels
    int num_lcg_id_with_data = 0;
    for (int i = 0; i < NR_MAX_NUM_LCGID; i++) {
      if (LCG_bytes[i])
        num_lcg_id_with_data++;
    }
    // A Regular or Periodic BSR can only be sent if TBS is sufficient
    // as transmitting only a BSR is not allowed if UE has data to transmit
    int size_for_long = num_lcg_id_with_data + sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT);
    int size_for_short = sizeof(NR_BSR_SHORT) + sizeof(NR_MAC_SUBHEADER_FIXED);
    if (num_lcg_id_with_data > 1 && buflen >= size_for_long)
      bsr_len = size_for_long;
    else if ((num_lcg_id_with_data > 0 && buflen >= size_for_short)
             || mac->scheduling_info.BSR_reporting_active & NR_BSR_TRIGGER_PERIODIC)
      bsr_len = size_for_short;
  }
  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  if (phr_info->is_configured && phr_info->phr_reporting > 0) {
    int needed = sizeof(NR_MAC_SUBHEADER_FIXED) + sizeof(NR_SINGLE_ENTRY_PHR_MAC_CE);
    if (buflen >= bsr_len + needed) {
      if (mac->scheduling_info.phr_info.phr_reporting) {
        mac_ce_p->phr_len = needed;
        nr_ue_fill_phr(mac, &mac_ce_p->phr, P_CMAX, tx_power, frame, slot);
      }
    }
  }
  mac_ce_p->end_for_tailer = mac_ce_p->pdu_end - bsr_len - mac_ce_p->phr_len;
}

/*
  nr_ue_get_sdu_mac_ce_post recalculates length and prepares the mac_ce field
  Need the following from mac_ce_p:
  bsr_ce_len
  bsr_len
  Update the following in mac_ce_p:
  bsr_ce_len
  bsr_header_len
  bsr_len
  tot_mac_ce_len
  bsr_s
  bsr_l
  bsr_t
*/
static void nr_ue_get_sdu_mac_ce_post(NR_UE_MAC_INST_t *mac,
                                      frame_t frame,
                                      slot_t slot,
                                      NR_UE_MAC_CE_INFO *mac_ce_p,
                                      uint32_t *LCG_bytes,
                                      bool *BSRsent)
{
  // Compute BSR Values and update Nb LCGID with data after multiplexing
  int lcg_id_bsr_max = 0;
  NR_UE_SCHEDULING_INFO *sched_info = &mac->scheduling_info;
  // Search the LCGID within waiting data for the highest priority LC
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    int lcid = mac->lc_ordered_list.array[i]->lcid;
    NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
    if (sched_info->LCID_buffer_remain) {
      // this is the highest priority LC with data to transmit
      lcg_id_bsr_max = sched_info->LCGID;
      break;
    }
  }

  // TS 38.321 Section 5.4.5
  // Check BSR padding: it is done after PHR according to Logical Channel Prioritization order
  // Check for max padding size, ie MAC Hdr for last RLC PDU = 1
  /* For Padding BSR:
     -  if the number of padding bits is equal to or larger than the size of the Short BSR plus its subheader but smaller than the
     size of the Long BSR plus its subheader:
     -  if more than one LCG has data available for transmission in the TTI where the BSR is transmitted: report Truncated BSR of
     the LCG with the highest priority logical channel with data available for transmission;
     -  else report Short BSR.
     -  else if the number of padding bits is equal to or larger than the size of the Long BSR plus its subheader, report Long BSR.
  */
  uint32_t padding_len = mac_ce_p->pdu_end - mac_ce_p->cur_ptr - mac_ce_p->phr_len;
  AssertFatal(padding_len >= 0, "Error in buffer size compute");
  mac_ce_p->bsr.type_bsr = b_none;
  int short_bsr_sz = sizeof(NR_BSR_SHORT) + sizeof(NR_MAC_SUBHEADER_FIXED);
  int num_lcg_id_with_data = 0;
  for (int lcg_id = 0; lcg_id < NR_MAX_NUM_LCGID; lcg_id++)
    num_lcg_id_with_data += LCG_bytes[lcg_id] > 0;
  int long_bsr_sz = num_lcg_id_with_data + sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT);
  if (num_lcg_id_with_data < 2 && padding_len >= short_bsr_sz) {
    mac_ce_p->bsr.type_bsr = b_short;
    mac_ce_p->bsr.bsr.s.LcgID = lcg_id_bsr_max;
    mac_ce_p->bsr.bsr.s.Buffer_size =
        nr_locate_BsrIndexByBufferSize(NR_SHORT_BSR_TABLE_SIZE, LCG_bytes[lcg_id_bsr_max]);
    LOG_D(NR_MAC,
          "[UE %d] sfn %d.%d BSR Trigger=0x%x report SHORT BSR with level %d for LCGID %d\n",
          mac->ue_id,
          frame,
          slot,
          sched_info->BSR_reporting_active,
          mac_ce_p->bsr.bsr.s.Buffer_size,
          lcg_id_bsr_max);
  } else if (padding_len >= long_bsr_sz) {
    /* if the number of padding bits is equal to or larger than the size of the Long BSR plus its subheader,
       report Long BSR whatever periodic or regular BSR*/
    mac_ce_p->bsr.type_bsr = b_long;
    uint8_t *tmp = mac_ce_p->bsr.bsr.lcg_bsr;
    for (int lcg_id = 0; lcg_id < 8; lcg_id++) {
      tmp[lcg_id] = nr_locate_BsrIndexByBufferSize(NR_LONG_BSR_TABLE_SIZE, LCG_bytes[lcg_id]);
    }
    LOG_D(NR_MAC,
          "[UE %d] sfn %d.%d BSR Trig=0x%x report LONG BSR (level LCGID %d %d %d %d %d %d %d %d)\n",
          mac->ue_id,
          frame,
          slot,
          sched_info->BSR_reporting_active,
          tmp[0],
          tmp[1],
          tmp[2],
          tmp[3],
          tmp[4],
          tmp[5],
          tmp[6],
          tmp[7]);
  } else if (padding_len == short_bsr_sz) {
    mac_ce_p->bsr.type_bsr = b_short_trunc;
    mac_ce_p->bsr.bsr.s.LcgID = lcg_id_bsr_max;
    mac_ce_p->bsr.bsr.s.Buffer_size =
        nr_locate_BsrIndexByBufferSize(NR_SHORT_BSR_TABLE_SIZE, LCG_bytes[lcg_id_bsr_max]);
  } else if (padding_len >= sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT)) {
    mac_ce_p->bsr.type_bsr = b_long_trunc;
    //  Fixme: this should be sorted by (TS 38.321, 5.4.5)
    // the logical channels having data available for
    // transmission following a decreasing order of the highest priority logical channel (with or without data
    // available for transmission) in each of these LCG(s), and in case of equal priority, in increasing order of LCGID
    uint8_t *tmp = mac_ce_p->bsr.bsr.lcg_bsr;
    for (int lcg_id = 0; lcg_id < 8; lcg_id++) {
      tmp[lcg_id] = nr_locate_BsrIndexByBufferSize(NR_LONG_BSR_TABLE_SIZE, LCG_bytes[lcg_id]);
    }
  } else
    LOG_D(NR_MAC, "Can't add any BSR, not enough padding\n");

  /* Actions when a BSR is sent */
  if (mac_ce_p->bsr.type_bsr != b_none) {
    LOG_D(NR_MAC,
          "[UE %d] MAC BSR Sent! buff_len %ld triggering LCID %ld\n",
          mac->ue_id,
          mac_ce_p->pdu_end - mac_ce_p->cur_ptr,
          sched_info->regularBSR_trigger_lcid);
    *BSRsent = true;
    // Reset ReTx BSR Timer
    nr_timer_start(&sched_info->retxBSR_Timer);
    // Reset Periodic Timer except when BSR is truncated
    if (mac_ce_p->bsr.type_bsr != b_long_trunc && mac_ce_p->bsr.type_bsr != b_short_trunc) {
      nr_timer_start(&sched_info->periodicBSR_Timer);
      LOG_D(NR_MAC, "[UE %d] MAC Periodic BSR Timer Reset\n", mac->ue_id);
    }

    if (sched_info->regularBSR_trigger_lcid > 0) {
      nr_lcordered_info_t *lc_info = get_lc_info_from_lcid(mac, sched_info->regularBSR_trigger_lcid);
      AssertFatal(lc_info, "Couldn't find logical channel with LCID %ld\n", sched_info->regularBSR_trigger_lcid);
      if (lc_info->sr_id >= 0 && lc_info->sr_id < NR_MAX_SR_ID) {
        LOG_D(NR_MAC, "[UE %d][SR] Gave SDU to PHY, clearing scheduling request with ID %d\n", mac->ue_id, lc_info->sr_id);
        nr_sr_info_t *sr = &sched_info->sr_info[lc_info->sr_id];
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
      }
    }
  }

  int size = nr_write_ce_ulsch_pdu(mac_ce_p->cur_ptr, mac_ce_p->phr_len ? &mac_ce_p->phr : NULL, &mac_ce_p->bsr, mac_ce_p->pdu_end);
  LOG_D(NR_MAC, "Added %d bytes of BSR\n", size);
  mac_ce_p->cur_ptr += size;
}

static uint32_t get_count_lcids_same_priority(uint8_t start, uint8_t total_active_lcids, nr_lcordered_info_t *lcid_ordered_array[])
{
  // count number of logical channels with same priority as curr_lcid
  uint8_t same_priority_count = 0;
  uint8_t curr_lcid = lcid_ordered_array[start]->lcid;
  for (uint8_t index = start; index < total_active_lcids; index++) {
    if (lcid_ordered_array[start]->priority == lcid_ordered_array[index]->priority) {
      same_priority_count++;
    }
  }
  LOG_D(NR_MAC, "Number of lcids with same priority as that of lcid %d is %d\n", curr_lcid, same_priority_count);
  return same_priority_count;
}

static long get_num_bytes_to_reqlc(NR_UE_MAC_INST_t *mac,
                                   uint8_t same_priority_count,
                                   uint8_t lc_num,
                                   uint32_t buflen_remain_ep,
                                   int32_t buflen_remain,
                                   uint8_t round_id,
                                   uint32_t *bytes_read_fromlc,
                                   long *target)
{
  /* Calculates the number of bytes the logical channel should request from the correcponding RLC buffer*/
  nr_lcordered_info_t *lc_info = get_lc_info_from_lcid(mac, lc_num);
  AssertFatal(lc_info, "Couldn't find logical channel with LCID %d\n", lc_num);
  const uint32_t pbr = lc_info->pbr;
  NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lc_num);
  int32_t lcid_remain_buffer = sched_info->LCID_buffer_remain;
  *target = (same_priority_count > 1) ? min(buflen_remain_ep, pbr) : pbr;

  long num_remaining_bytes = 0;
  long num_bytes_requested = 0;
  if (round_id == 0) { // initial round
    uint32_t pdu_remain = (same_priority_count > 1) ? buflen_remain_ep : buflen_remain;
    num_bytes_requested = (pdu_remain < pbr) ? min(pdu_remain, lcid_remain_buffer) : min(pbr, lcid_remain_buffer);
    num_remaining_bytes = *target - bytes_read_fromlc[lc_num - 1];
    num_bytes_requested = min(num_bytes_requested, num_remaining_bytes);
  } else { // from first round
    if (same_priority_count > 1) {
      num_bytes_requested = min(buflen_remain_ep, lcid_remain_buffer);
      num_remaining_bytes = buflen_remain_ep - bytes_read_fromlc[lc_num - 1];
      num_bytes_requested = min(num_bytes_requested, num_remaining_bytes);
    } else {
      num_bytes_requested = min(buflen_remain, lcid_remain_buffer);
    }
  }
  AssertFatal(num_remaining_bytes >= 0 && num_bytes_requested <= buflen_remain,
              "the total number of bytes allocated until target length is greater than expected: num_bytes_requested %ld, "
              "buflen_remain %d\n",
              num_bytes_requested,
              buflen_remain);
  LOG_D(NR_MAC, "number of bytes requested for lcid %d is %li\n", lc_num, num_bytes_requested);

  return num_bytes_requested;
}

static bool get_dataavailability_buffers(uint8_t total_active_lcids,
                                         nr_lcordered_info_t *lcid_ordered_array[],
                                         bool *data_status_lcbuffers)
{
  // check whether there is any data in the rlc buffer corresponding to active lcs
  for (uint8_t id = 0; id < total_active_lcids; id++) {
    int lcid = lcid_ordered_array[id]->lcid;
    if (data_status_lcbuffers[lcid_buffer_index(lcid)]) {
      return true;
    }
  }
  return false;
}

static uint select_logical_channels(NR_UE_MAC_INST_t *mac, nr_lcordered_info_t *active_lcids[])
{
  // (TODO: selection of logical channels for logical channel prioritization procedure as per 5.4.3.1.2 Selection of logical
  // channels, TS38.321)
  int nb = 0;
  // selection of logical channels with Bj > 0
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
    if (lc_info->rb_suspended)
      continue;
    int lcid = lc_info->lcid;
    NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
    if (sched_info->Bj > 0) {
      active_lcids[nb++] = lc_info;
      LOG_D(NR_MAC, "The available lcid is %d with total active channels count = %d\n", lcid, nb);
    }
  }
  return nb;
}

static bool fill_mac_sdu(NR_UE_MAC_INST_t *mac,
                         frame_t frame,
                         slot_t slot,
                         int lcid,
                         uint count_same_priority_lcids,
                         uint32_t buflen_ep,
                         uint32_t *lcids_bytes_tot,
                         NR_UE_MAC_CE_INFO *mac_ce_p,
                         uint *num_lcids_same_priority,
                         uint32_t *LCG_bytes)
{
  /* prepare the MAC sdu */
  NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
  int32_t lcid_remain_buffer = sched_info->LCID_buffer_remain;
  uint usable = mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr;
  LOG_D(NR_MAC,
        "[UE %d] [%d.%d] lcp round = %d, remaining mac pdu length before BSR = %d, lcid buffer remaining = %d, lcid = %d \n",
        mac->ue_id,
        frame,
        slot,
        mac_ce_p->lcp_allocation_counter,
        usable,
        lcid_remain_buffer,
        lcid);

  if (is_lcid_suspended(mac, lcid))
    return false;
  if (mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr < sizeof(NR_MAC_SUBHEADER_LONG))
    // We can't add one byte after the header
    return false;

  // Pointer used to build the MAC sub-PDU headers in the ULSCH buffer for each SDU
  NR_MAC_SUBHEADER_LONG *header = (NR_MAC_SUBHEADER_LONG *)mac_ce_p->cur_ptr;

  // number of bytes requested from RLC for each LCID
  long target = 0;
  // NR_MAC_SUBHEADER_SHORT can handle up to 256 bytes message
  int header_sz = usable < 256 ? sizeof(NR_MAC_SUBHEADER_SHORT) : sizeof(NR_MAC_SUBHEADER_LONG);
  long bytes_requested = get_num_bytes_to_reqlc(mac,
                                                count_same_priority_lcids,
                                                lcid,
                                                buflen_ep,
                                                usable - header_sz,
                                                mac_ce_p->lcp_allocation_counter,
                                                lcids_bytes_tot,
                                                &target);
  header_sz = bytes_requested < 256 ? sizeof(NR_MAC_SUBHEADER_SHORT) : sizeof(NR_MAC_SUBHEADER_LONG);

  uint16_t sdu_length = nr_mac_rlc_data_req(mac->ue_id,
                                            mac->ue_id,
                                            false,
                                            lcid,
                                            bytes_requested,
                                            (char *)mac_ce_p->cur_ptr + header_sz);

  AssertFatal(bytes_requested >= sdu_length,
              "LCID = 0x%02x RLC has segmented %d bytes but MAC has max %li remaining bytes\n",
              lcid,
              sdu_length,
              bytes_requested);

  // Decrement Bj by the total size of MAC SDUs(RLC PDU) served to logical channel
  // currently the Bj is drecremented by size of MAC SDus everytime it is served to logical channel, so by this approach there
  // will be more chance for lower priority logical channels to be served in the next TTI
  // second approach can also be followed where Bj is decremented only in the first round but not in the subsequent rounds
  sched_info->Bj -= sdu_length; // TODO avoid Bj to go below 0
  LOG_D(NR_MAC,
        "decrement Bj of the lcid %d by size of sdu length = %d and new Bj for lcid %d is %d\n",
        lcid,
        sdu_length,
        lcid,
        sched_info->Bj);

  int lc_idx = lcid_buffer_index(lcid);
  if (sdu_length > 0) {
    mac_ce_p->num_sdus++;
    LOG_D(NR_MAC,
          "[UE %d] [%d.%d] UL-DXCH -> ULSCH, Generating UL MAC sub-PDU for SDU %d, length %d bytes, RB with LCID "
          "0x%02x (buflen (TBS) %ld bytes)\n",
          mac->ue_id,
          frame,
          slot,
          mac_ce_p->num_sdus,
          sdu_length,
          lcid,
          mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr);
    if (header_sz == sizeof(NR_MAC_SUBHEADER_SHORT)) {
      *(NR_MAC_SUBHEADER_SHORT *)header = (NR_MAC_SUBHEADER_SHORT){.R = 0, .F = 0, .LCID = lcid, .L = sdu_length};
    } else {
      *header = (NR_MAC_SUBHEADER_LONG){.R = 0, .F = 1, .LCID = lcid, .L = htons(sdu_length)};
#ifdef ENABLE_MAC_PAYLOAD_DEBUG
      LOG_I(NR_MAC, "dumping MAC SDU with length %d: \n", sdu_length);
      log_dump(NR_MAC, header, sdu_length, LOG_DUMP_CHAR, "\n");
#endif
    }
    mac_ce_p->cur_ptr += header_sz + sdu_length;
  } else {
    mac_ce_p->lcids_data_status[lc_idx] = false;
    (*num_lcids_same_priority)--;
    LOG_D(NR_MAC, "No data to transmit for RB with LCID 0x%02x\n and hence set to false", lcid);
    return false;
  }

  // Update Buffer remain and BSR bytes after transmission
  sched_info->LCID_buffer_remain -= sdu_length;
  LCG_bytes[sched_info->LCGID] -= sdu_length;
  LOG_D(NR_MAC,
        "[UE %d] Update BSR [%d.%d] LCG_bytes for LCG%ld = %d\n",
        mac->ue_id,
        frame,
        slot,
        sched_info->LCGID,
        LCG_bytes[sched_info->LCGID]);

  // update number of bytes served from the current lcid
  lcids_bytes_tot[lc_idx] += (sdu_length + (count_same_priority_lcids > 1 ? 1 : 0) * sizeof(NR_MAC_SUBHEADER_LONG));

  if ((mac_ce_p->lcp_allocation_counter == 0 && lcids_bytes_tot[lc_idx] >= target)
      || (count_same_priority_lcids > 1
          && lcids_bytes_tot[lc_idx] >= buflen_ep)) { // only prioritized bit rate should be taken from logical channel in
    // the first lcp run except when infinity
    LOG_D(NR_MAC,
          "Total number bytes read from rlc buffer for lcid %d are %d\n",
          lcid,
          lcids_bytes_tot[lc_idx]);
    (*num_lcids_same_priority)--;
    return false;
  }
  return true;
}

/**
 * Function:      to fetch data to be transmitted from RLC, place it in the ULSCH PDU buffer
 to generate the complete MAC PDU with sub-headers and MAC CEs according to ULSCH MAC PDU generation (6.1.2 TS 38.321)
 the selected sub-header for the payload sub-PDUs is NR_MAC_SUBHEADER_LONG
 * @module_idP    Module ID
 * @frame         current UL frame
 * @slot          current UL slot
 * @ulsch_buffer  Pointer to ULSCH PDU
 * @buflen        TBS
 */
static bool nr_ue_get_sdu(NR_UE_MAC_INST_t *mac,
                          frame_t frame,
                          slot_t slot,
                          uint8_t *ulsch_buffer,
                          const uint32_t buflen,
                          int tx_power,
                          int P_CMAX,
                          bool *BSRsent)
{
  NR_UE_MAC_CE_INFO mac_ce_info;

  // in the first run all the lc are allocated as per bj and prioritized bit rate but in subsequent runs, no need to consider
  // bj and prioritized bit rate but just consider priority
  uint32_t buflen_ep = 0; // this variable holds the length in bytes in mac pdu when multiple equal priority channels are present
  // because as per standard(TS38.321), all equal priority channels should be served equally

  // Call BSR procedure as described in Section 5.4.5 in 38.321
  // Check whether BSR is triggered before scheduling ULSCH
  uint32_t LCG_bytes[NR_MAX_NUM_LCGID] = {0};
  nr_update_bsr(mac, LCG_bytes);

  nr_ue_get_sdu_mac_ce_pre(mac, frame, slot, ulsch_buffer, buflen, LCG_bytes, &mac_ce_info, tx_power, P_CMAX);

  LOG_D(NR_MAC,
        "[UE %d] [%d.%d] process UL transport block with size TBS = %d bytes, number of existing LCids %d \n",
        mac->ue_id,
        frame,
        slot,
        buflen,
        mac->lc_ordered_list.count);

  // variable used to build the lcids with positive Bj
  if (!mac->lc_ordered_list.count) {
    LOG_E(NR_MAC, "Failed to init lcids_bj_pos: mac->lc_ordered_list.count = 0\n");
    return false;
  }
  nr_lcordered_info_t *lcids_bj_pos[mac->lc_ordered_list.count];
  int avail_lcids_count = select_logical_channels(mac, lcids_bj_pos);

  // multiplex in the order of highest priority
  do {
    /*
      go until there is space availabile in the MAC PDU and there is data available in RLC buffers of active logical channels
    */
    uint num_lcids_same_priority = 0;
    uint count_same_priority_lcids = 0;

    // variable used to store the total bytes read from rlc for each lcid
    uint32_t lcids_bytes_tot[NR_MAX_NUM_LCID] = {0};

    for (uint id = 0; id < avail_lcids_count; id++) {
      /*
  loop over all logical channels in the order of priority. As stated in TS138.321 Section 5.4.3.1, in the first run, only
  prioritized number of bytes are taken out from the corresponding RLC buffers of all active logical channels and if there is
  still space availble in the MAC PDU, then from the next run all the remaining data from the higher priority logical channel
  is placed in the MAC PDU before going on to next high priority logical channel
      */
      int lcid = lcids_bj_pos[id]->lcid;
      int idx = lcid_buffer_index(lcid);
      // skip the logical channel if no data in the buffer initially or the data in the buffer was zero because it was written in to
      // MAC PDU
      if (!mac_ce_info.lcids_data_status[idx]) {
        continue;
      }

      // count number of lc with same priority as lcid
      if (!num_lcids_same_priority) {
        num_lcids_same_priority = count_same_priority_lcids = get_count_lcids_same_priority(id, avail_lcids_count, lcids_bj_pos);
      }

      int remain = mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr;
      LOG_D(NR_MAC,
            "[UE %d] [%d.%d] UL-DXCH -> ULSCH, RLC with LCID 0x%02x buflen_remain %d bytes)\n",
            mac->ue_id,
            frame,
            slot,
            lcid,
            remain);

      if (num_lcids_same_priority == count_same_priority_lcids) {
        buflen_ep = remain < count_same_priority_lcids * sizeof(NR_MAC_SUBHEADER_LONG)
                        ? 0
                        : (remain - (count_same_priority_lcids * sizeof(NR_MAC_SUBHEADER_LONG))) / count_same_priority_lcids;
      }

      while (mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr > 0) {
        /*
          loops until the requested number of bytes from MAC to RLC are placed in the MAC PDU. The number of requested bytes
          depends on whether it is the first run or otherwise because in the first run only prioritited number of bytes of all
          active logical channels in the order of priority are placed in the MAC PDU. The 'get_num_bytes_to_reqlc' calculates
          the target number of bytes to request from RLC via 'mac_rlc_data_req'
        */
        if (!fill_mac_sdu(mac,
                          frame,
                          slot,
                          lcid,
                          count_same_priority_lcids,
                          buflen_ep,
                          lcids_bytes_tot,
                          &mac_ce_info,
                          &num_lcids_same_priority,
                          LCG_bytes)) {
          break;
        }
      }
    }

    mac_ce_info.lcp_allocation_counter++;
  } while (mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr > sizeof(NR_MAC_SUBHEADER_LONG)
           && get_dataavailability_buffers(avail_lcids_count, lcids_bj_pos, mac_ce_info.lcids_data_status));

  nr_ue_get_sdu_mac_ce_post(mac, frame, slot, &mac_ce_info, LCG_bytes, BSRsent);

  // Compute final offset for padding and fill remainder of ULSCH with 0
  int remain = mac_ce_info.pdu_end - mac_ce_info.cur_ptr;
  if (remain) {
    LOG_D(NR_MAC, "Filling remainder %d bytes to the UL PDU \n", remain);
    *(NR_MAC_SUBHEADER_FIXED *)mac_ce_info.cur_ptr = (NR_MAC_SUBHEADER_FIXED){.R = 0, .LCID = UL_SCH_LCID_PADDING};
    mac_ce_info.cur_ptr++;
    if (get_softmodem_params()->phy_test || get_softmodem_params()->do_ra) {
      uint8_t *buf = mac_ce_info.cur_ptr;
      uint8_t *end = mac_ce_info.pdu_end;
      for (; buf < end && ((intptr_t)buf) % 4; buf++)
        *buf = lrand48() & 0xff;
      for (; buf < end - 3; buf += 4) {
        uint32_t *buf32 = (uint32_t *)buf;
        *buf32 = lrand48();
      }
      for (; buf < end; buf++)
        *buf = lrand48() & 0xff;
    } else {
      memset(mac_ce_info.cur_ptr, 0, mac_ce_info.pdu_end - mac_ce_info.cur_ptr);
    }
  }
#ifdef ENABLE_MAC_PAYLOAD_DEBUG
  LOG_I(NR_MAC, "MAC PDU %ld bytes\n", mac_ce_info.cur_ptr - ulsch_buffer);
  log_dump(NR_MAC, ulsch_buffer, buflen, LOG_DUMP_CHAR, "\n");
#endif

  return true; // success if we got at least one sdu
}

void nr_ue_ul_scheduler(NR_UE_MAC_INST_t *mac, nr_uplink_indication_t *ul_info)
{
  int cc_id = ul_info->cc_id;
  frame_t frame_tx = ul_info->frame;
  slot_t slot_tx = ul_info->slot;
  RA_config_t *ra = &mac->ra;

  if (mac->state == UE_PERFORMING_RA && ra->ra_state == nrRA_UE_IDLE) {
    init_RA(mac);
    // perform the Random Access Resource selection procedure (see clause 5.1.2 and .2a)
    ra_resource_selection(mac);
  }

  if (mac->state == UE_PERFORMING_RA && ra->ra_state == nrRA_GENERATE_PREAMBLE)
    nr_ue_prach_scheduler(mac, frame_tx, slot_tx);

  bool BSRsent = false;
  if (mac->state == UE_CONNECTED) {
    nr_ue_periodic_srs_scheduling(mac, frame_tx, slot_tx);
    nr_update_rlc_buffers_status(mac, frame_tx, slot_tx);
  }

  // Schedule ULSCH only if the current frame and slot match those in ul_config_req
  // AND if a UL grant (UL DCI or Msg3) has been received (as indicated by num_pdus)
  uint8_t ulsch_input_buffer_array[FAPI_NR_UL_CONFIG_LIST_NUM][MAX_NUM_NR_ULSCH_SEGMENTS * 1056];
  int number_of_pdus = 0;

  /* Stale ul_config (frame mismatch on a slot-indexed buffer) must not abort the
   * whole UL scheduler: PUCCH HARQ-ACK/CSI/SR still needs to go out. lockGet_ul_iterator
   * already cleared the leftover PUSCH grant and released the mutex on that path. */
  fapi_nr_ul_config_request_pdu_t *ulcfg_pdu = lockGet_ul_iterator(mac, frame_tx, slot_tx);
  if (ulcfg_pdu) {
    LOG_D(NR_MAC, "number of UL PDUs: %d with UL transmission in sfn [%d.%d]\n", *ulcfg_pdu->privateNBpdus, frame_tx, slot_tx);

    while (ulcfg_pdu->pdu_type != FAPI_NR_END) {
      uint8_t *ulsch_input_buffer = ulsch_input_buffer_array[number_of_pdus];
      if (ulcfg_pdu->pdu_type == FAPI_NR_UL_CONFIG_TYPE_PUSCH) {
        nfapi_nr_ue_pusch_pdu_t *pdu = &ulcfg_pdu->pusch_config_pdu;
        uint32_t TBS_bytes = pdu->pusch_data.tb_size;
        LOG_D(NR_MAC,
              "harq_id %d, new_data_indicator %d, TBS_bytes %d (ra_state %d)\n",
              pdu->pusch_data.harq_process_id,
              pdu->pusch_data.new_data_indicator,
              TBS_bytes,
              ra->ra_state);
        pdu->tx_request_body.fapiTxPdu = NULL;
        if ((ra->ra_state == nrRA_WAIT_RAR || ra->ra_state == nrRA_WAIT_MSGB) && !ra->cfra) {
          nr_get_Msg3_MsgA_PUSCH_payload(mac, ulsch_input_buffer, TBS_bytes);
          for (int k = 0; k < TBS_bytes; k++) {
            LOG_D(NR_MAC, "(%i): 0x%x\n", k, ulsch_input_buffer[k]);
          }
          pdu->tx_request_body.fapiTxPdu = ulsch_input_buffer;
          pdu->tx_request_body.pdu_length = TBS_bytes;
          number_of_pdus++;
          T(T_NRUE_MAC_UL_PDU_WITH_DATA,
            T_INT(mac->crnti),
            T_INT(frame_tx),
            T_INT(slot_tx),
            T_INT(ulcfg_pdu->pusch_config_pdu.pusch_data.harq_process_id),
            T_BUFFER(ulsch_input_buffer, TBS_bytes));
        } else {
          if (ulcfg_pdu->pusch_config_pdu.pusch_data.new_data_indicator
              && (mac->state == UE_CONNECTED || (ra->ra_state == nrRA_WAIT_RAR && ra->cfra))) {
            if (!nr_timer_is_active(&mac->time_alignment_timer) && mac->state == UE_CONNECTED
                && !get_softmodem_params()->phy_test) {
              // UL data arrival during RRC_CONNECTED when UL synchronisation status is "non-synchronised"
              release_ul_config(ulcfg_pdu, false);
              trigger_MAC_UE_RA(mac, NULL);
              return;
            }
            // Getting IP traffic to be transmitted
            int tx_power = pdu->tx_power;
            bool tp_enabled = pdu->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled;
            int P_CMAX = nr_get_Pcmax(mac->p_Max,
                                      mac->nr_band,
                                      mac->frame_structure.frame_type,
                                      mac->frequency_range,
                                      mac->current_UL_BWP->channel_bandwidth,
                                      pdu->qam_mod_order,
                                      false,
                                      mac->current_UL_BWP->scs,
                                      mac->current_UL_BWP->BWPSize,
                                      tp_enabled,
                                      pdu->rb_size,
                                      pdu->rb_start);
            if (nr_ue_get_sdu(mac, frame_tx, slot_tx, ulsch_input_buffer, TBS_bytes, tx_power, P_CMAX, &BSRsent)) {
              pdu->tx_request_body.fapiTxPdu = ulsch_input_buffer;
              pdu->tx_request_body.pdu_length = TBS_bytes;
              number_of_pdus++;
              T(T_NRUE_MAC_UL_PDU_WITH_DATA,
                T_INT(mac->crnti),
                T_INT(frame_tx),
                T_INT(slot_tx),
                T_INT(ulcfg_pdu->pusch_config_pdu.pusch_data.harq_process_id),
                T_BUFFER(ulsch_input_buffer, TBS_bytes));
            } else
              LOG_E(MAC, "nr_ue_get_sdu() failed\n");
            // start or restart dataInactivityTimer  if any MAC entity transmits a MAC SDU for DTCH logical channel,
            // or DCCH logical channel
            if (mac->data_inactivity_timer)
              nr_timer_start(mac->data_inactivity_timer);
          }
        }

        if (ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION && !ra->cfra) {
          LOG_I(NR_MAC, "[RAPROC][%d.%d] RA-Msg3 retransmitted\n", frame_tx, slot_tx);
          // 38.321 restart the ra-ContentionResolutionTimer at each HARQ retransmission in the first symbol after the end of the
          // Msg3 transmission
          nr_Msg3_transmitted(mac);
        }
        if (ra->ra_state == nrRA_WAIT_RAR && !ra->cfra) {
          LOG_A(NR_MAC, "[RAPROC][%d.%d] RA-Msg3 transmitted\n", frame_tx, slot_tx);
          nr_Msg3_transmitted(mac);
        }
        if (ra->ra_state == nrRA_WAIT_MSGB && !ra->cfra) {
          LOG_A(NR_MAC, "[RAPROC][%d.%d] RA-MsgA-PUSCH transmitted\n", frame_tx, slot_tx);
        }
      }
      ulcfg_pdu++;
    }
    release_ul_config(ulcfg_pdu, false);
  }

  if(mac->state >= UE_PERFORMING_RA && mac->state < UE_DETACHING)
    nr_ue_pucch_scheduler(mac, frame_tx, slot_tx);

  if (mac->if_module != NULL && mac->if_module->scheduled_response != NULL) {
    LOG_D(NR_MAC, "3# scheduled_response transmitted,%d, %d\n", frame_tx, slot_tx);
    nr_scheduled_response_t scheduled_response = {.ul_config = mac->ul_config_request + slot_tx,
                                                  .mac = mac,
                                                  .module_id = mac->ue_id,
                                                  .CC_id = cc_id,
                                                  .phy_data = ul_info->phy_data};
    mac->if_module->scheduled_response(&scheduled_response);
  }

  if(mac->state == UE_CONNECTED)
    nr_update_sr(mac, BSRsent);
  // Global variables implicit logic
  // far away, BSR_reporting_active is set
  mac->scheduling_info.BSR_reporting_active = NR_BSR_TRIGGER_NONE;

  // update Bj for all active lcids before LCP procedure
  if (mac->current_UL_BWP) {
    LOG_D(NR_MAC, "%4d.%2d Logical Channel Prioritization\n", frame_tx, slot_tx);
    for (int i = 0; i < mac->lc_ordered_list.count; i++) {
      nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
      int lcid = lc_info->lcid;
      // max amount of data that can be buffered/accumulated in a logical channel buffer
      uint32_t bucketSize_max = lc_info->bucket_size;
      //  measure Bj increment the value of Bj by product PBR  * T
      NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
      int32_t bj = sched_info->Bj;
      if (lc_info->pbr < UINT_MAX) {
        uint32_t slots_elapsed = nr_timer_elapsed_time(&sched_info->Bj_timer); // slots elapsed since Bj was last incremented
        // it is safe to divide by 1k since pbr in lc_info is computed multiplying by 1000 the RRC value to convert kB/s to B/s
        uint32_t pbr_ms = lc_info->pbr / 1000;
        bj += ((pbr_ms * slots_elapsed) >> mac->current_UL_BWP->scs); // each slot length is 1/scs ms
      }
      else
        bj = INT_MAX;
      // bj > max bucket size, set bj to max bucket size, as in ts38.321 5.4.3.1 Logical Channel Prioritization
      sched_info->Bj = min(bj, bucketSize_max);
      // reset bj timer
      nr_timer_start(&sched_info->Bj_timer);
    }
  }
}

static void get_pusch_frame_slot(const int current_frame,
                                 const int current_slot,
                                 const int k2,
                                 const int delta,
                                 const int slots_per_frame,
                                 frame_t *frame_tx,
                                 int *slot_tx)
{
  *slot_tx = (current_slot + k2 + delta) % slots_per_frame;
  *frame_tx = (current_frame + (current_slot + k2 + delta) / slots_per_frame) % MAX_FRAME_NUMBER;
}

// PUSCH scheduler:
// - Calculate the slot in which ULSCH should be scheduled. This is current slot + K2,
// - where K2 is the offset between the slot in which UL DCI is received and the slot
// - in which ULSCH should be scheduled. K2 is configured in RRC configuration.
// PUSCH Msg3 scheduler:
// - scheduled by RAR UL grant according to 8.3 of TS 38.213
// Note: Msg3 tx in the uplink symbols of mixed slot
int nr_ue_pusch_scheduler(const NR_UE_MAC_INST_t *mac,
                          const uint8_t is_Msg3,
                          const frame_t current_frame,
                          const int current_slot,
                          frame_t *frame_tx,
                          int *slot_tx,
                          const long k2)
{
  int delta = 0;
  const NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  // Get the numerology to calculate the Tx frame and slot
  int mu = current_UL_BWP->scs;
  int slots_per_frame = mac->frame_structure.numb_slots_frame;

  // k2 as per 3GPP TS 38.214 version 15.9.0 Release 15 ch 6.1.2.1.1
  // PUSCH time domain resource allocation is higher layer configured from uschTimeDomainAllocationList in either pusch-ConfigCommon

  if (is_Msg3) {

    delta = get_delta_for_k2(mu);

    AssertFatal((k2 + delta) > GET_DURATION_RX_TO_TX(mac, mu),
                "Slot offset (%ld) for Msg3 needs to be higher than DURATION_RX_TO_TX (%ld). Please set min_rxtxtime at least to "
                "%ld in gNB config file or gNBs.[0].min_rxtxtime=%ld via command line.\n",
                k2,
                GET_DURATION_RX_TO_TX(mac, mu),
                GET_DURATION_RX_TO_TX(mac, mu),
                GET_DURATION_RX_TO_TX(mac, mu));


    get_pusch_frame_slot(current_frame, current_slot, k2, delta, slots_per_frame, frame_tx, slot_tx);
  } else {
    AssertFatal(k2 >= GET_DURATION_RX_TO_TX(mac, mu),
                "Slot offset K2 (%ld) needs to be higher than DURATION_RX_TO_TX (%ld). Please set min_rxtxtime at least to %ld in "
                "gNB config file or gNBs.[0].min_rxtxtime=%ld via command line.\n",
                k2,
                GET_DURATION_RX_TO_TX(mac, mu),
                GET_DURATION_RX_TO_TX(mac, mu),
                GET_DURATION_RX_TO_TX(mac, mu));


    if (k2 < 0) { // This can happen when a false DCI is received
      LOG_W(PHY, "%d.%d. Received k2 %ld\n", current_frame, current_slot, k2);
      return -1;
    }

    // Calculate TX slot and frame
    get_pusch_frame_slot(current_frame, current_slot, k2, 0, slots_per_frame, frame_tx, slot_tx);
  }

  LOG_D(NR_MAC, "[%04d.%02d] UL transmission in [%04d.%02d] (k2 %ld delta %d)\n", current_frame, current_slot, *frame_tx, *slot_tx, k2, delta);
  return 0;
}

void configure_csi_resource_mapping(fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                    const NR_CSI_RS_ResourceMapping_t *resourceMapping,
                                    uint32_t bwp_size,
                                    uint32_t bwp_start)
{
  // According to last paragraph of TS 38.214 5.2.2.3.1
  if (resourceMapping->freqBand.startingRB < bwp_start)
    csirs_config_pdu->start_rb = bwp_start;
  else
    csirs_config_pdu->start_rb = resourceMapping->freqBand.startingRB;
  if (resourceMapping->freqBand.nrofRBs > (bwp_start + bwp_size - csirs_config_pdu->start_rb))
    csirs_config_pdu->nr_of_rbs = bwp_start + bwp_size - csirs_config_pdu->start_rb;
  else
    csirs_config_pdu->nr_of_rbs = resourceMapping->freqBand.nrofRBs;
  AssertFatal(csirs_config_pdu->nr_of_rbs >= 24, "CSI-RS has %d RBs, but the minimum is 24\n", csirs_config_pdu->nr_of_rbs);

  csirs_config_pdu->symb_l0 = resourceMapping->firstOFDMSymbolInTimeDomain;
  if (resourceMapping->firstOFDMSymbolInTimeDomain2)
    csirs_config_pdu->symb_l1 = *resourceMapping->firstOFDMSymbolInTimeDomain2;
  csirs_config_pdu->cdm_type = resourceMapping->cdm_Type;

  csirs_config_pdu->freq_density = resourceMapping->density.present;
  if ((resourceMapping->density.present == NR_CSI_RS_ResourceMapping__density_PR_dot5)
      && (resourceMapping->density.choice.dot5 == NR_CSI_RS_ResourceMapping__density__dot5_evenPRBs))
    csirs_config_pdu->freq_density--;

  switch(resourceMapping->frequencyDomainAllocation.present){
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row1:
      csirs_config_pdu->row = 1;
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.row1.buf[0]) >> 4) & 0x0f;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row2:
      csirs_config_pdu->row = 2;
      csirs_config_pdu->freq_domain = (((resourceMapping->frequencyDomainAllocation.choice.row2.buf[1] >> 4) & 0x0f)
                                       | ((resourceMapping->frequencyDomainAllocation.choice.row2.buf[0] << 4) & 0xff0));
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row4:
      csirs_config_pdu->row = 4;
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.row4.buf[0]) >> 5) & 0x07;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_other:
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.other.buf[0]) >> 2) & 0x3f;
      // determining the row of table 7.4.1.5.3-1 in 38.211
      switch (resourceMapping->nrofPorts) {
        case NR_CSI_RS_ResourceMapping__nrofPorts_p1:
          AssertFatal(1 == 0, "Resource with 1 CSI port shouldn't be within other rows\n");
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p2:
          csirs_config_pdu->row = 3;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p4:
          csirs_config_pdu->row = 5;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p8:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 8;
          else {
            int num_k = 0;
            for (int k = 0; k < 6; k++)
              num_k += (((csirs_config_pdu->freq_domain) >> k) & 0x01);
            if (num_k == 4)
              csirs_config_pdu->row = 6;
            else
              csirs_config_pdu->row = 7;
          }
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p12:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 10;
          else
            csirs_config_pdu->row = 9;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p16:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 12;
          else
            csirs_config_pdu->row = 11;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p24:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 14;
          else {
            if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4)
              csirs_config_pdu->row = 15;
            else
              csirs_config_pdu->row = 13;
          }
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p32:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 17;
          else {
            if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4)
              csirs_config_pdu->row = 18;
            else
              csirs_config_pdu->row = 16;
          }
          break;
        default:
          AssertFatal(false, "Invalid number of ports in CSI-RS resource\n");
      }
      break;
    default:
      AssertFatal(false, "Invalid freqency domain allocation in CSI-RS resource\n");
  }
}
