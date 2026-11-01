/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

//#include "defs.h"
#include "mac_proto.h"
#include "radio/COMMON/common_lib.h"
#include "assertions.h"
#include "executables/nr-uesoftmodem.h"
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "RRC/NR_UE/rrc_proto.h"
#include <pthread.h>
static NR_UE_MAC_INST_t *nr_ue_mac_inst[MAX_NUM_NR_UE_INST] = {0};

void send_srb0_rrc(int ue_id, const uint8_t *sdu, sdu_size_t sdu_len, void *data)
{
  AssertFatal(sdu_len > 0 && sdu_len < CCCH_SDU_SIZE, "invalid CCCH SDU size %d\n", sdu_len);
  UNUSED(data);
  MessageDef *message_p = itti_alloc_new_message(TASK_MAC_UE, 0, NR_RRC_MAC_CCCH_DATA_IND);
  memset(NR_RRC_MAC_CCCH_DATA_IND(message_p).sdu, 0, sdu_len);
  memcpy(NR_RRC_MAC_CCCH_DATA_IND(message_p).sdu, sdu, sdu_len);
  NR_RRC_MAC_CCCH_DATA_IND(message_p).sdu_size = sdu_len;
  itti_send_msg_to_task(TASK_RRC_NRUE, ue_id, message_p);
}

void send_pcch_rrc(int ue_id, const uint8_t *sdu, sdu_size_t sdu_len, void *data)
{
  AssertFatal(sdu_len > 0 && sdu_len <= PCCH_SDU_SIZE, "invalid PCCH SDU size %d\n", sdu_len);
  MessageDef *message_p = itti_alloc_new_message(TASK_MAC_UE, 0, NR_RRC_MAC_PCCH_DATA_IND);
  memset(NR_RRC_MAC_PCCH_DATA_IND(message_p).sdu, 0, sdu_len);
  memcpy(NR_RRC_MAC_PCCH_DATA_IND(message_p).sdu, sdu, sdu_len);
  NR_RRC_MAC_PCCH_DATA_IND(message_p).sdu_size = sdu_len;
  itti_send_msg_to_task(TASK_RRC_NRUE, ue_id, message_p);
}

void nr_ue_init_mac(NR_UE_MAC_INST_t *mac)
{
  LOG_I(NR_MAC, "[UE%d] Initializing MAC\n", mac->ue_id);
  nr_ue_reset_sync_state(mac, false);
  mac->get_sib1 = false;
  for (int i = 0; i < MAX_SI_GROUPS; i++)
    mac->get_otherSI[i] = false;
  memset(&mac->phy_config, 0, sizeof(mac->phy_config));
  mac->si_SchedInfo.si_window_start = -1;
  mac->servCellIndex = 0;
  mac->harq_ACK_SpatialBundlingPUCCH = false;
  mac->harq_ACK_SpatialBundlingPUSCH = false;
  mac->uecap_maxMIMO_PDSCH_layers = 0;
  mac->uecap_maxMIMO_PUSCH_layers_cb = 0;
  mac->uecap_maxMIMO_PUSCH_layers_nocb = 0;
  mac->p_Max = INT_MIN;
  mac->p_Max_alt = INT_MIN;
  mac->msg3_C_RNTI = false;
  mac->sr_fallback_ra_triggered = false;
  mac->phy_config.config_req.ntn_config.params_changed = false;
  initNotifiedFIFO(&mac->input_nf);
  reset_mac_inst(mac);

  // need to inizialize because might not been setup (optional timer)
  nr_timer_stop(&mac->scheduling_info.sr_DelayTimer);

  memset(&mac->ssb_measurements, 0, sizeof(mac->ssb_measurements));
  for (int i = 0; i < MAX_NB_SSB; i++) {
    mac->ssb_measurements[i].ssb_rsrp_dBm = INT_MIN;
    mac->ssb_measurements[i].ssb_sinr_dB = INT_MIN;
  }

  memset(&mac->ul_time_alignment, 0, sizeof(mac->ul_time_alignment));
  memset(&mac->ssb_list, 0, sizeof(mac->ssb_list));

  for (int i = 0; i < NR_MAX_SR_ID; i++)
    memset(&mac->scheduling_info.sr_info[i], 0, sizeof(mac->scheduling_info.sr_info[i]));

  mac->pucch_power_control_initialized = false;
  mac->pusch_power_control_initialized = false;
}

void nr_ue_mac_default_configs(NR_UE_MAC_INST_t *mac)
{
  // default values as defined in 38.331 sec 9.2.2

  // sf80 default for retxBSR_Timer sf10 for periodicBSR_Timer
  int mu = mac->current_UL_BWP ? mac->current_UL_BWP->scs : mac->numerology;
  int subframes_per_slot = get_slots_per_frame_from_scs(mu) / 10;
  nr_timer_setup(&mac->scheduling_info.retxBSR_Timer, 80 * subframes_per_slot, 1); // 1 slot update rate
  nr_timer_setup(&mac->scheduling_info.periodicBSR_Timer, 10 * subframes_per_slot, 1); // 1 slot update rate

  mac->scheduling_info.phr_info.is_configured = true;
  mac->scheduling_info.phr_info.PathlossChange_db = 1;
  nr_timer_setup(&mac->scheduling_info.phr_info.periodicPHR_Timer, 10 * subframes_per_slot, 1);
  nr_timer_setup(&mac->scheduling_info.phr_info.prohibitPHR_Timer, 10 * subframes_per_slot, 1);
}

void nr_ue_send_synch_request(NR_UE_MAC_INST_t *mac, module_id_t module_id, int cc_id, const fapi_nr_synch_request_t *sync_req)
{
  // Sending to PHY a request to resync
  mac->synch_request.Mod_id = module_id;
  mac->synch_request.CC_id = cc_id;
  mac->synch_request.synch_req = *sync_req;
  mac->if_module->synch_request(&mac->synch_request);
}

void nr_ue_reset_sync_state(NR_UE_MAC_INST_t *mac, bool reconf)
{
  // reset synchornization status
  mac->state = reconf ? UE_NOT_SYNC_RECONF : UE_NOT_SYNC;
  mac->ra.ra_state = nrRA_UE_IDLE;
}

NR_UE_L2_STATE_t nr_ue_get_sync_state(module_id_t mod_id)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(mod_id);
  return mac->state;
}

NR_UE_MAC_INST_t *nr_l2_init_ue(int instance_id, int numerology, int rx2tx_capability)
{
  AssertFatal(instance_id < MAX_NUM_NR_UE_INST, "instance_id %d is out of range\n", instance_id);
  AssertFatal(nr_ue_mac_inst[instance_id] == NULL, "MAC instance %d already initialized\n", instance_id);
  nr_ue_mac_inst[instance_id] = calloc_or_fail(1, sizeof(NR_UE_MAC_INST_t));

  NR_UE_MAC_INST_t *mac = nr_ue_mac_inst[instance_id];
  mac->ue_id = instance_id;
  mac->numerology = numerology;
  mac->rx2tx_capability = rx2tx_capability;
  nr_ue_init_mac(mac);
  int ret = pthread_mutex_init(&mac->if_mutex, NULL);
  AssertFatal(ret == 0, "Mutex init failed\n");
  nr_ue_mac_default_configs(mac);
  if (IS_SA_MODE(get_softmodem_params()))
    ue_init_config_request(mac, get_slots_per_frame_from_scs(numerology));

  static bool initialized = false;
  if (!initialized) {
    int rc = nr_rlc_module_init(NR_RLC_OP_MODE_UE);
    AssertFatal(rc == 0, "Could not initialize RLC layer\n");
    initialized = true;
  }

  nr_rlc_activate_srb0(instance_id, NULL, send_srb0_rrc);

  return nr_ue_mac_inst[instance_id];
}

NR_UE_MAC_INST_t *get_mac_inst(module_id_t module_id)
{
  AssertFatal(module_id < MAX_NUM_NR_UE_INST, "module_id %d is out of range\n", module_id);
  NR_UE_MAC_INST_t *mac = nr_ue_mac_inst[module_id];
  if (mac == NULL) return NULL;
  AssertFatal(mac->ue_id == module_id, "MAC ID %d doesn't match with input %d\n", mac->ue_id, module_id);
  return mac;
}

void reset_mac_inst(NR_UE_MAC_INST_t *nr_mac)
{
  // MAC reset according to 38.321 Section 5.12

  // initialize Bj for each logical channel to zero
  // TODO reset also other status variables of LC, is this ok?
  for (int i = 0; i < NR_MAX_NUM_LCID; i++) {
    LOG_D(NR_MAC, "Applying default logical channel config for LCID %d\n", i);
    nr_mac->scheduling_info.lc_sched_info[i].Bj = 0;
    nr_mac->scheduling_info.lc_sched_info[i].LCID_buffer_remain = 0;
  }

  // TODO stop all running timers
  for (int i = 0; i < NR_MAX_NUM_LCID; i++) {
    nr_mac->scheduling_info.lc_sched_info[i].Bj = 0;
    nr_timer_stop(&nr_mac->scheduling_info.lc_sched_info[i].Bj_timer);
  }
  if (nr_mac->data_inactivity_timer)
    nr_timer_stop(nr_mac->data_inactivity_timer);
  nr_timer_stop(&nr_mac->time_alignment_timer);
  nr_timer_stop(&nr_mac->scheduling_info.sr_DelayTimer);
  nr_timer_stop(&nr_mac->scheduling_info.retxBSR_Timer);
  nr_timer_stop(&nr_mac->ra.response_window_timer);
  nr_timer_stop(&nr_mac->ra.RA_backoff_timer);
  nr_timer_stop(&nr_mac->ra.contention_resolution_timer);
  for (int i = 0; i < NR_MAX_SR_ID; i++)
    nr_timer_stop(&nr_mac->scheduling_info.sr_info[i].prohibitTimer);

  // consider all timeAlignmentTimers as expired and perform the corresponding actions in clause 5.2
  handle_time_alignment_timer_expired(nr_mac);

  // set the NDIs for all uplink HARQ processes to the value 0
  for (int k = 0; k < NR_MAX_HARQ_PROCESSES; k++)
    nr_mac->ul_harq_info[k].last_ndi = -1; // initialize to invalid value

  // stop any ongoing RACH procedure
  if (nr_mac->ra.RA_active) {
    nr_mac->msg3_C_RNTI = false;
    nr_mac->sr_fallback_ra_triggered = false;
    nr_mac->ra.ra_state = nrRA_UE_IDLE;
    nr_mac->ra.RA_active = false;
  }

  // discard explicitly signalled contention-free Random Access Resources
  // TODO not sure what needs to be done here

  // flush Msg3 buffer
  free_and_zero(nr_mac->ra.Msg3_buffer);

  // cancel any triggered Scheduling Request procedure
  for (int i = 0; i < NR_MAX_SR_ID; i++) {
    nr_mac->scheduling_info.sr_info[i].pending = false;
    nr_mac->scheduling_info.sr_info[i].counter = 0;
  }

  // cancel any triggered Buffer Status Reporting procedure
  nr_mac->scheduling_info.BSR_reporting_active = NR_BSR_TRIGGER_NONE;

  // cancel any triggered Power Headroom Reporting procedure
  nr_mac->scheduling_info.phr_info.phr_reporting = 0;
  nr_mac->scheduling_info.phr_info.was_mac_reset = true;

  for (int c = 0; c < 2; c++) {
    // flush the soft buffers for all DL HARQ processes
    // for each DL HARQ process, consider the next received transmission for a TB as the very first transmission
    for (int k = 0; k < NR_MAX_HARQ_PROCESSES; k++) {
      memset(&nr_mac->dl_harq_info[k][c], 0, sizeof(NR_UE_DL_HARQ_STATUS_t));
      nr_mac->dl_harq_info[k][c].last_ndi = -1; // initialize to invalid value
    }
  }

  // release, if any, Temporary C-RNTI
  nr_mac->ra.t_crnti = 0;

  // reset BFI_COUNTER
  // TODO beam failure procedure not implemented
}

static void release_dedicated_bwp0_config(NR_UE_MAC_INST_t *mac)
{
  if (mac->dl_BWPs.count > 0) {
    NR_UE_DL_BWP_t *bwp = mac->dl_BWPs.array[0];
    NR_BWP_PDCCH_t *pdcch = &mac->config_BWP_PDCCH[0];
    asn_sequence_empty(&pdcch->list_Coreset);
    asn_sequence_empty(&pdcch->list_SS);
    asn1cFreeStruc(asn_DEF_NR_PDSCH_Config, bwp->pdsch_Config);
    mac->current_DL_BWP = bwp;
    mac->sc_info.initial_dl_BWPSize = bwp->BWPSize;
    mac->sc_info.initial_dl_BWPStart = bwp->BWPStart;
  }

  if (mac->ul_BWPs.count > 0) {
    NR_UE_UL_BWP_t *ubwp = mac->ul_BWPs.array[0];
    asn1cFreeStruc(asn_DEF_NR_PUCCH_Config, ubwp->pucch_Config);
    asn1cFreeStruc(asn_DEF_NR_SRS_Config, ubwp->srs_Config);
    asn1cFreeStruc(asn_DEF_NR_PUSCH_Config, ubwp->pusch_Config);
    mac->current_UL_BWP = ubwp;
    mac->sc_info.initial_ul_BWPSize = ubwp->BWPSize;
    mac->sc_info.initial_ul_BWPStart = ubwp->BWPStart;
  }
}

void release_mac_configuration(NR_UE_MAC_INST_t *mac, NR_UE_MAC_reset_cause_t cause)
{
  NR_UE_ServingCell_Info_t *sc = &mac->sc_info;
  /* Partial release for normal no-redirection RRCRelease: RRC keeps the current cell selected for idle camping
   * (TS 38.304 §5.2.5), so keep SIB1/common BWP0/paging PDCCH and drop only connected-mode MAC config. */
  if (cause == GO_TO_IDLE_KEEP_CAMPED) {
    for (int i = mac->lc_ordered_list.count; i > 0; i--)
      asn_sequence_del(&mac->lc_ordered_list, i - 1, 1);
    for (int i = mac->dl_BWPs.count - 1; i >= 1; i--)
      release_dl_BWP(mac, i);
    for (int i = mac->ul_BWPs.count - 1; i >= 1; i--)
      release_ul_BWP(mac, i);
    release_dedicated_bwp0_config(mac);
    return;
  }

  if (cause == GO_TO_IDLE) {
    /* Full idle/cell-selection path: release stored common cell context before selecting/syncing again
     * (TS 38.331 §5.3.11, TS 38.304 §5.2.6). */
    asn1cFreeStruc(asn_DEF_NR_MIB, mac->mib);
    asn1cFreeStruc(asn_DEF_NR_SearchSpace, mac->search_space_zero);
    asn1cFreeStruc(asn_DEF_NR_ControlResourceSet, mac->coreset0);
    asn_sequence_empty(&mac->si_SchedInfo.si_SchedInfo_list);
    asn1cFreeStruc(asn_DEF_NR_TDD_UL_DL_ConfigCommon, mac->tdd_UL_DL_ConfigurationCommon);
    for (int i = mac->lc_ordered_list.count; i > 0 ; i--)
      asn_sequence_del(&mac->lc_ordered_list, i - 1, 1);
  }

  asn1cFreeStruc(asn_DEF_NR_CrossCarrierSchedulingConfig, sc->crossCarrierSchedulingConfig);
  asn1cFreeStruc(asn_DEF_NR_SRS_CarrierSwitching, sc->carrierSwitching);
  asn1cFreeStruc(asn_DEF_NR_UplinkConfig, sc->supplementaryUplink);
  asn1cFreeStruc(asn_DEF_NR_PDSCH_CodeBlockGroupTransmission, sc->pdsch_CGB_Transmission);
  asn1cFreeStruc(asn_DEF_NR_PUSCH_CodeBlockGroupTransmission, sc->pusch_CGB_Transmission);
  asn1cFreeStruc(asn_DEF_NR_CSI_MeasConfig, sc->csi_MeasConfig);
  asn1cFreeStruc(asn_DEF_NR_CSI_AperiodicTriggerStateList, sc->aperiodicTriggerStateList);
  asn1cFreeStruc(asn_DEF_NR_NTN_Config_r17, sc->ntn_Config_r17);
  asn1cFreeStruc(asn_DEF_NR_DownlinkHARQ_FeedbackDisabled_r17, sc->downlinkHARQ_FeedbackDisabled_r17);
  free(sc->xOverhead_PDSCH);
  free(sc->nrofHARQ_ProcessesForPDSCH);
  free(sc->nrofHARQ_ProcessesForPDSCH_v1700);
  free(sc->nrofHARQ_ProcessesForPUSCH_r17);
  free(sc->rateMatching_PUSCH);
  free(sc->xOverhead_PUSCH);
  free(sc->maxMIMO_Layers_PDSCH);
  free(sc->maxMIMO_Layers_PUSCH);
  memset(&mac->sc_info, 0, sizeof(mac->sc_info));

  mac->current_DL_BWP = NULL;
  mac->current_UL_BWP = NULL;

  // in case of re-establishment we don't need to release initial BWP config common
  int first_bwp_rel = 0; // first BWP to release
  if (cause == RE_ESTABLISHMENT || cause == RRC_SETUP_REESTAB_RESUME) {
    first_bwp_rel = 1;
    release_dedicated_bwp0_config(mac);
  }

  for (int i = mac->dl_BWPs.count - 1; i >= first_bwp_rel; i--)
    release_dl_BWP(mac, i);
  for (int i = mac->ul_BWPs.count - 1; i >= first_bwp_rel; i--)
    release_ul_BWP(mac, i);

  memset(&mac->ssb_measurements, 0, sizeof(mac->ssb_measurements));
  memset(&mac->csirs_measurements, 0, sizeof(mac->csirs_measurements));
  memset(&mac->ul_time_alignment, 0, sizeof(mac->ul_time_alignment));
  for (int i = mac->TAG_list.count; i > 0 ; i--)
    asn_sequence_del(&mac->TAG_list, i - 1, 1);
}
