/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "mac_rrc_dl_handler.h"

#include "mac_proto.h"
#include "nr_radio_config.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "F1AP_CauseRadioNetwork.h"
#include "NR_HandoverPreparationInformation.h"
#include "NR_CG-ConfigInfo.h"
#include "NR_RRCReconfiguration.h"
#include "openair3/ocp-gtpu/gtp_itf.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "lib/f1ap_rrc_message_transfer.h"
#include "lib/f1ap_interface_management.h"
#include "lib/f1ap_ue_context.h"
#include "nrdc_mac_rrc_dl_handler.h"

#include "executables/softmodem-common.h"

#include "uper_decoder.h"
#include "uper_encoder.h"
#include "openair3/NRPPA/nrppa_gNB_config.h"
#include "openair2/F1AP/lib/f1ap_positioning.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// Standarized 5QI values and Default Priority levels as mentioned in 3GPP TS 23.501 Table 5.7.4-1
const uint64_t qos_fiveqi[26] = {1, 2, 3, 4, 65, 66, 67, 71, 72, 73, 74, 76, 5, 6, 7, 8, 9, 69, 70, 79, 80, 82, 83, 84, 85, 86};
const uint64_t qos_priority[26] = {20, 40, 30, 50, 7, 20, 15, 56, 56, 56, 56, 56, 10,
                                   60, 70, 80, 90, 5, 55, 65, 68, 19, 22, 24, 21, 18};

static instance_t get_f1_gtp_instance(void)
{
  const f1ap_cudu_inst_t *inst = getCxt(0);
  if (!inst)
    return -1; // means no F1
  return inst->gtpInst;
}

bool DURecvCb(protocol_ctxt_t *ctxt_pP,
              const srb_flag_t srb_flagP,
              const rb_id_t rb_idP,
              const mui_t muiP,
              const confirm_t confirmP,
              const sdu_size_t sdu_buffer_sizeP,
              unsigned char *const sdu_buffer_pP,
              const pdcp_transmission_mode_t modeP,
              const uint32_t *sourceL2Id,
              const uint32_t *destinationL2Id)
{
  UNUSED(confirmP);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  // The buffer comes from the stack in gtp-u thread, we have a make a separate buffer to enqueue in a inter-thread message queue
  uint8_t *sdu = malloc16(sdu_buffer_sizeP);
  memcpy(sdu, sdu_buffer_pP, sdu_buffer_sizeP);
  nr_rlc_data_req(ctxt_pP, srb_flagP, rb_idP, muiP, sdu_buffer_sizeP, sdu);
  return true;
}

/** @brief Fill and send request to create GTP-U tunnel on F1 */
static f1ap_up_tnl_t f1_drb_gtpu_create(const gtpv1u_gnb_create_tunnel_req_t *req)
{
  f1ap_up_tnl_t out = {0};

  LOG_I(GTPU, "Incoming DRB %d / PDU Session %d - UL TEID %d\n", req->incoming_rb_id, req->pdusession_id, req->outgoing_teid);

  instance_t f1inst = get_f1_gtp_instance();
  DevAssert(f1inst >= 0);
  gtpv1u_gnb_create_tunnel_resp_t resp = {0};
  int ret = gtpv1u_create_ngu_tunnel(f1inst, req, &resp, DURecvCb, NULL, NULL);
  AssertFatal(ret >= 0, "Unable to create GTP Tunnel for F1-U\n");
  AssertFatal(resp.gnb_addr.length == sizeof(in_addr_t),
              "GTP tunnel response address length %d does not match IPv4 size %zu\n",
              resp.gnb_addr.length,
              sizeof(in_addr_t));
  memcpy(&out.tl_address, &resp.gnb_addr.buffer, resp.gnb_addr.length);
  out.teid = resp.gnb_NGu_teid;

  return out;
}

static bool check_plmn_identity(const plmn_id_t *check_plmn, const plmn_id_t *plmn)
{
  return plmn->mcc == check_plmn->mcc && plmn->mnc_digit_length == check_plmn->mnc_digit_length && plmn->mnc == check_plmn->mnc;
}

/* not static, so we can call it from the outside (in telnet) */
void du_clear_all_ue_states()
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);

  instance_t f1inst = get_f1_gtp_instance();

  rnti_t to_release[NR_NB_RA_PROC_MAX + MAX_MOBILES_PER_GNB];
  int num_release = 0;
  FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
    rnti_t rnti = UE->rnti;
    // free all F1 contexts
    if (du_exists_f1_ue_data(rnti))
      du_remove_f1_ue_data(rnti);
    newGtpuDeleteAllTunnels(f1inst, rnti);
    to_release[num_release++] = rnti;
  }

  FOR_EACH_RA_UE(&mac->UE_info, UE) {
    rnti_t rnti = UE->rnti;
    // free all F1 contexts
    if (du_exists_f1_ue_data(UE->rnti))
      du_remove_f1_ue_data(UE->rnti);
    newGtpuDeleteAllTunnels(f1inst, UE->rnti);
    to_release[num_release++] = rnti;
  }
  for (int i = 0; i < num_release; ++i)
    nr_mac_release_ue(mac, to_release[i]);

  NR_SCHED_UNLOCK(&mac->sched_lock);
}

void f1_reset_cu_initiated(const f1ap_reset_t *reset)
{
  LOG_I(MAC, "F1 Reset initiated by CU\n");

  f1ap_reset_ack_t ack = {.transaction_id = reset->transaction_id};
  if(reset->reset_type == F1AP_RESET_ALL) {
    du_clear_all_ue_states();
  } else {
    // reset->reset_type == F1AP_RESET_PART_OF_F1_INTERFACE
    AssertFatal(1==0, "Not implemented yet\n");
  }

  gNB_MAC_INST *mac = RC.nrmac[0];
  mac->mac_rrc.f1_reset_acknowledge(&ack);
}

void f1_reset_acknowledge_du_initiated(const f1ap_reset_ack_t *ack)
{
  (void) ack;
  AssertFatal(false, "%s() not implemented yet\n", __func__);
}

void f1_setup_response(const f1ap_setup_resp_t *resp)
{
  LOG_I(MAC, "received F1 Setup Response from CU %s\n", resp->gNB_CU_name);
  LOG_I(MAC, "CU uses RRC version %d.%d.%d\n", resp->rrc_ver[0], resp->rrc_ver[1], resp->rrc_ver[2]);

  if (resp->num_cells_to_activate == 0) {
    LOG_W(NR_MAC, "no cell to activate: cell remains blocked\n");
    return;
  }

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  const f1ap_setup_req_t *setup_req = mac->f1_config.setup_req;
  const f1ap_served_cell_info_t *du_cell = &setup_req->cell[0].info;

  AssertFatal(resp->num_cells_to_activate == 1, "can only handle one cell, but %d activated\n", resp->num_cells_to_activate);
  const served_cells_to_activate_t *cu_cell = &resp->cells_to_activate[0];

  AssertFatal(du_cell->nr_cellid  == cu_cell->nr_cellid, "CellID mismatch: DU %ld vs CU %ld\n", du_cell->nr_cellid, cu_cell->nr_cellid);
  AssertFatal(check_plmn_identity(&du_cell->plmn, &cu_cell->plmn), "PLMN mismatch\n");
  AssertFatal(du_cell->nr_pci == cu_cell->nrpci, "PCI mismatch: DU %d vs CU %d\n", du_cell->nr_pci, cu_cell->nrpci);
  // TODO: Add proper error handling
  nr_cell_sched_t *cell = nr_mac_get_cell_by_cgi(mac, cu_cell->plmn, cu_cell->nr_cellid);
  AssertFatal(cell != NULL, "cell with CGI (cellid=%lu) not found\n", cu_cell->nr_cellid);
  // we can configure other SIB only after having received the ones generated by CU
  bool update = nr_mac_configure_other_sib(cell, cu_cell->num_SI, cu_cell->SI_msg);
  /* only if we have to update SIB1, set mib and sib1 to non-NULL to indicate
   * in du_config_update that it has changed (otherwise, gNB-DU config update
   * indicates only cell status */
  NR_BCCH_BCH_Message_t *mib = NULL;
  NR_BCCH_DL_SCH_Message_t *sib1 = NULL;
  if (update) {
    NR_COMMON_channels_t *cc = &cell->common_channels;
    mib = cc->mib;
    sib1 = cc->sib1;
  }

  mac->f1_config.setup_resp = malloc(sizeof(*mac->f1_config.setup_resp));
  AssertFatal(mac->f1_config.setup_resp != NULL, "out of memory\n");
  // Copy F1AP message
  *mac->f1_config.setup_resp = cp_f1ap_setup_response(resp);

  f1ap_setup_req_t *sr = mac->f1_config.setup_req;
  DevAssert(sr->num_cells_available == 1);
  prepare_du_configuration_update(mac, &sr->cell[0].info, mib, sib1);
  NR_SCHED_UNLOCK(&mac->sched_lock);

  // NOTE: Before accepting any UEs, we should initialize the UE states.
  // This is to handle cases when DU loses the existing SCTP connection,
  // and reestablishes a new connection to either a new CU or the same CU.
  // This triggers a new F1 Setup Request from DU to CU as per the specs.
  // Reinitializing the UE states is necessary to avoid any inconsistent states
  // between DU and CU.
  // NOTE2: do not reset in phy_test, because there is a pre-configured UE in
  // this case. Once NSA/phy-test use F1, this might be lifted, because
  // creation of a UE will be requested from higher layers.

  // TS38.473 [Sec 8.2.3.1]: "This procedure also re-initialises the F1AP UE-related
  // contexts (if any) and erases all related signalling connections
  // in the two nodes like a Reset procedure would do."
  if (!get_softmodem_params()->phy_test) {
    LOG_I(MAC, "Clearing the DU's UE states before, if any.\n");
    du_clear_all_ue_states();
  }
}

void f1_setup_failure(const f1ap_setup_failure_t *failure)
{
  UNUSED(failure);
  LOG_E(MAC, "the CU reported F1AP Setup Failure, is there a configuration mismatch?\n");
  exit(1);
}

void gnb_du_configuration_update_acknowledge(const f1ap_gnb_du_configuration_update_acknowledge_t *ack)
{
  (void)ack;
  LOG_I(MAC, "received gNB-DU configuration update acknowledge\n");
}

static NR_RLC_BearerConfig_t *get_bearerconfig_from_srb(const f1ap_srb_to_setup_t *srb,
                                                        const nr_rlc_configuration_t *rlc_config)
{
  long priority = srb->id == 2 ? 3 : 1; // see 38.331 sec 9.2.1
  e_NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration bucket =
      NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms5;
  return get_SRB_RLC_BearerConfig(get_lcid_from_srbid(srb->id), priority, bucket, rlc_config);
}

static int handle_ue_context_srbs_setup(NR_UE_info_t *UE,
                                        int srbs_len,
                                        const f1ap_srb_to_setup_t *req_srbs,
                                        f1ap_srb_setup_t **resp_srbs,
                                        NR_CellGroupConfig_t *cellGroupConfig,
                                        const nr_rlc_configuration_t *rlc_config)
{
  DevAssert(req_srbs != NULL && resp_srbs != NULL && cellGroupConfig != NULL);

  *resp_srbs = calloc(srbs_len, sizeof(**resp_srbs));
  AssertFatal(*resp_srbs != NULL, "out of memory\n");
  for (int i = 0; i < srbs_len; i++) {
    const f1ap_srb_to_setup_t *srb = &req_srbs[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_srb(srb, rlc_config);
    nr_rlc_add_srb(UE->rnti, srb->id, rlc_BearerConfig);

    int priority = rlc_BearerConfig->mac_LogicalChannelConfig->ul_SpecificParameters->priority;
    nr_lc_config_t c = {.lcid = rlc_BearerConfig->logicalChannelIdentity, .priority = priority};
    nr_mac_add_lcid(&UE->UE_sched_ctrl, &c);

    (*resp_srbs)[i].id = srb->id;
    (*resp_srbs)[i].lcid = c.lcid;

    if (rlc_BearerConfig->logicalChannelIdentity == 1) {
      // CU asks to add SRB1: when creating a cellGroupConfig, we always add it
      // (see get_initial_cellGroupConfig())
      const struct NR_CellGroupConfig__rlc_BearerToAddModList *addmod = cellGroupConfig->rlc_BearerToAddModList;
      DevAssert(addmod->list.count >= 1 && addmod->list.array[0]->logicalChannelIdentity == 1);
      ASN_STRUCT_FREE(asn_DEF_NR_RLC_BearerConfig, rlc_BearerConfig);
    } else {
      int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
      DevAssert(ret == 0);
    }
  }
  return srbs_len;
}

static NR_RLC_BearerConfig_t *get_bearerconfig_from_drb(const f1ap_drb_to_setup_t *drb,
                                                        const nr_rlc_configuration_t *rlc_config)
{
  const NR_RLC_Config_PR rlc_conf = drb->rlc_mode == F1AP_RLC_MODE_AM ? NR_RLC_Config_PR_am : NR_RLC_Config_PR_um_Bi_Directional;
  long priority = 13; // hardcoded for the moment
  return get_DRB_RLC_BearerConfig(get_lcid_from_drbid(drb->id), drb->id, rlc_conf, priority, rlc_config);
}

static int get_non_dynamic_priority(int fiveqi)
{
  for (int i = 0; i < sizeofArray(qos_fiveqi); ++i)
    if (qos_fiveqi[i] == fiveqi)
      return qos_priority[i];
  LOG_W(NR_MAC, "unsupported non-dynamic 5QI %d\n", fiveqi);
  return -1;
}

static NR_QoS_config_t get_qos_config(const f1ap_qos_flow_param_t *qos)
{
  NR_QoS_config_t qos_c = {0};
  switch (qos->qos_type) {
    case DYNAMIC:
      qos_c.priority = qos->dyn.prio;
      qos_c.fiveQI = 0; // does not exist for non-dynamic
      break;
    case NON_DYNAMIC:
      qos_c.fiveQI = qos->nondyn.fiveQI;
      qos_c.priority = get_non_dynamic_priority(qos_c.fiveQI);
      break;
    default:
      AssertFatal(false, "illegal QoS type %d\n", qos->qos_type);
      break;
  }
  return qos_c;
}

int handle_ue_context_drbs_setup(NR_UE_info_t *UE,
                                 int drbs_len,
                                 const f1ap_drb_to_setup_t *req_drbs,
                                 f1ap_drb_setup_t **resp_drbs,
                                 NR_CellGroupConfig_t *cellGroupConfig,
                                 const nr_rlc_configuration_t *rlc_config)
{
  DevAssert(req_drbs != NULL && resp_drbs != NULL && cellGroupConfig != NULL);
  instance_t f1inst = get_f1_gtp_instance();

  /* Note: the actual GTP tunnels are created in the F1AP breanch of
   * ue_context_*_response() */
  *resp_drbs = calloc(drbs_len, sizeof(**resp_drbs));
  AssertFatal(*resp_drbs != NULL, "out of memory\n");
  for (int i = 0; i < drbs_len; i++) {
    const f1ap_drb_to_setup_t *drb = &req_drbs[i];
    AssertFatal(drb->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS supported\n");
    f1ap_drb_setup_t *resp_drb = &(*resp_drbs)[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_drb(drb, rlc_config);
    if (UE->capability && UE->capability->rlc_Parameters && UE->capability->rlc_Parameters->ext2
        && UE->capability->rlc_Parameters->ext2->am_WithLongSN_RedCap_r17 == NULL) {
      *rlc_BearerConfig->rlc_Config->choice.am->dl_AM_RLC.sn_FieldLength = NR_SN_FieldLengthAM_size12;
      *rlc_BearerConfig->rlc_Config->choice.am->ul_AM_RLC.sn_FieldLength = NR_SN_FieldLengthAM_size12;
    }
    AssertFatal(rlc_BearerConfig->rlc_Config, "We expect rlc-Config to be always present when we configure a DRB\n");
    nr_rlc_add_drb(UE->rnti, drb->id, rlc_BearerConfig);

    nr_lc_config_t c = {.lcid = rlc_BearerConfig->logicalChannelIdentity, .nssai = drb->nr.nssai};
    int prio = 100;
    for (int q = 0; q < drb->nr.flows_len; ++q) {
      c.qos_config[q] = get_qos_config(&drb->nr.flows[q].param);
      if (c.qos_config[q].priority < 0)
        continue;
      prio = min(prio, c.qos_config[q].priority);
    }
    c.priority = prio;
    nr_mac_add_lcid(&UE->UE_sched_ctrl, &c);

    resp_drb->id = drb->id;
    resp_drb->lcid = malloc_or_fail(sizeof(*resp_drb->lcid));
    *resp_drb->lcid = c.lcid;
    // just put same number of tunnels in DL as in UL
    DevAssert(drb->up_ul_tnl_len == 1);
    resp_drb->up_dl_tnl_len = drb->up_ul_tnl_len;
    if (f1inst >= 0) { // we actually use F1-U
      // F1-U tunnel setup: 1 GTP-U tunnel per DRB
      gtpv1u_gnb_create_tunnel_req_t req = {.ue_id = UE->rnti,
                                            .outgoing_teid = drb->up_ul_tnl[0].teid,
                                            .pdusession_id = drb->id,
                                            .incoming_rb_id = drb->id,
                                            .dst_addr.length = 32};
      memcpy(&req.dst_addr.buffer, &drb->up_ul_tnl[0].tl_address, sizeof(uint8_t) * 4); // only IPv4 now
      resp_drb->up_dl_tnl[0] = f1_drb_gtpu_create(&req);
    }

    if (!cellGroupConfig->rlc_BearerToAddModList)
      cellGroupConfig->rlc_BearerToAddModList = calloc_or_fail(1, sizeof(*cellGroupConfig->rlc_BearerToAddModList));
    int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
    DevAssert(ret == 0);
  }
  return drbs_len;
}

static int handle_ue_context_drbs_release(NR_UE_info_t *UE,
                                          int drbs_len,
                                          const f1ap_drb_to_release_t *req_drbs,
                                          NR_CellGroupConfig_t *cellGroupConfig)
{
  DevAssert(req_drbs != NULL && cellGroupConfig != NULL);
  instance_t f1inst = get_f1_gtp_instance();
  cellGroupConfig->rlc_BearerToReleaseList = calloc(1, sizeof(*cellGroupConfig->rlc_BearerToReleaseList));
  AssertFatal(cellGroupConfig->rlc_BearerToReleaseList != NULL, "out of memory\n");

  for (int i = 0; i < drbs_len; i++) {
    const f1ap_drb_to_release_t *drb = &req_drbs[i];

    long lcid = get_lcid_from_drbid(drb->id);
    int idx = 0;
    while (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
      const NR_RLC_BearerConfig_t *bc = cellGroupConfig->rlc_BearerToAddModList->list.array[idx];
      if (bc->logicalChannelIdentity == lcid)
        break;
      ++idx;
    }
    if (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
      nr_mac_remove_lcid(&UE->UE_sched_ctrl, lcid);
      nr_rlc_release_entity(UE->rnti, lcid);
      if (f1inst >= 0) /* Delete F1 tunnel */
        newGtpuDeleteOneTunnel(f1inst, UE->rnti, drb->id);
      asn_sequence_del(&cellGroupConfig->rlc_BearerToAddModList->list, idx, 1);
      long *plcid = malloc(sizeof(*plcid));
      AssertFatal(plcid, "out of memory\n");
      *plcid = lcid;
      int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToReleaseList->list, plcid);
      DevAssert(ret == 0);
    }
  }
  return drbs_len;
}

static NR_UE_NR_Capability_t *get_nr_cap(const NR_UE_CapabilityRAT_ContainerList_t *clist)
{
  for (int i = 0; i < clist->list.count; i++) {
    const NR_UE_CapabilityRAT_Container_t *c = clist->list.array[i];
    if (c->rat_Type != NR_RAT_Type_nr) {
      LOG_W(NR_MAC, "ignoring capability of type %ld\n", c->rat_Type);
      continue;
    }

    NR_UE_NR_Capability_t *cap = NULL;
    asn_dec_rval_t dec_rval = uper_decode(NULL,
                                          &asn_DEF_NR_UE_NR_Capability,
                                          (void **)&cap,
                                          c->ue_CapabilityRAT_Container.buf,
                                          c->ue_CapabilityRAT_Container.size,
                                          0,
                                          0);
    if (dec_rval.code != RC_OK) {
      LOG_W(NR_MAC, "cannot decode NR UE capability, ignoring\n");
      ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, cap);
      continue;
    }
    return cap;
  }
  return NULL;
}

static NR_UE_NR_Capability_t *get_ue_nr_cap(int rnti, uint8_t *buf, uint32_t len)
{
  if (buf == NULL || len == 0)
    return NULL;

  NR_UE_CapabilityRAT_ContainerList_t *clist = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_UE_CapabilityRAT_ContainerList, (void **)&clist, buf, len, 0, 0);
  if (dec_rval.code != RC_OK) {
    LOG_W(NR_MAC, "cannot decode UE capability container list of UE RNTI %04x, ignoring capabilities\n", rnti);
    return NULL;
  }

  NR_UE_NR_Capability_t *cap = get_nr_cap(clist);
  ASN_STRUCT_FREE(asn_DEF_NR_UE_CapabilityRAT_ContainerList, clist);
  return cap;
}

static NR_CellGroupConfig_t *get_source_cellgroup(const NR_AS_Config_t *src)
{
  NR_RRCReconfiguration_t *reconf = NULL;
  NR_CellGroupConfig_t *src_cg = NULL;
  asn_dec_rval_t rv = uper_decode_complete(NULL,
                                           &asn_DEF_NR_RRCReconfiguration,
                                           (void **)&reconf,
                                           src->rrcReconfiguration.buf,
                                           src->rrcReconfiguration.size);
  if (rv.code == RC_OK && reconf
      && reconf->criticalExtensions.present == NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration
      && reconf->criticalExtensions.choice.rrcReconfiguration
      && reconf->criticalExtensions.choice.rrcReconfiguration->nonCriticalExtension
      && reconf->criticalExtensions.choice.rrcReconfiguration->nonCriticalExtension->masterCellGroup) {
    const OCTET_STRING_t *mcg = reconf->criticalExtensions.choice.rrcReconfiguration->nonCriticalExtension->masterCellGroup;
    rv = uper_decode_complete(NULL, &asn_DEF_NR_CellGroupConfig, (void **)&src_cg, mcg->buf, mcg->size);
    if (rv.code != RC_OK || !src_cg || !src_cg->spCellConfig || !src_cg->spCellConfig->spCellConfigDedicated) {
      ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, src_cg);
      src_cg = NULL;
    }
  }
  if (!src_cg)
    LOG_W(NR_MAC, "HO: HandoverPreparationInformation carries no usable source spCellConfigDedicated\n");
  ASN_STRUCT_FREE(asn_DEF_NR_RRCReconfiguration, reconf);
  return src_cg;
}

static void get_ue_cap_and_source_cg_from_ho_prep_info(uint8_t *buf,
                                                       uint32_t len,
                                                       NR_UE_NR_Capability_t **ue_cap,
                                                       NR_CellGroupConfig_t **src_cg)
{
  *ue_cap = NULL;
  *src_cg = NULL;
  if (buf == NULL || len == 0)
    return;
  NR_HandoverPreparationInformation_t *hpi = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_HandoverPreparationInformation, (void **)&hpi, buf, len);
  if (dec_rval.code != RC_OK) {
    LOG_W(NR_MAC, "cannot decode HandoverPreparationInformation, ignoring capabilities and source configuration\n");
    return;
  }
  if (hpi->criticalExtensions.present == NR_HandoverPreparationInformation__criticalExtensions_PR_c1
      && hpi->criticalExtensions.choice.c1 != NULL
      && hpi->criticalExtensions.choice.c1->present
             == NR_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation
      && hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation != NULL) {
    const NR_HandoverPreparationInformation_IEs_t *hpi_ie = hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation;
    *ue_cap = get_nr_cap(&hpi_ie->ue_CapabilityRAT_List);
    if (hpi_ie->sourceConfig)
      *src_cg = get_source_cellgroup(hpi_ie->sourceConfig);
  }
  ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
}

NR_CG_ConfigInfo_t *get_cg_config_info(uint8_t *buf, uint32_t len)
{
  struct NR_CG_ConfigInfo *cg_configinfo = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_CG_ConfigInfo, (void **)&cg_configinfo, buf, len);
  if (dec_rval.code != RC_OK) {
    LOG_W(NR_MAC, "cannot decode CG-ConfigInfo, ignoring it\n");
    return NULL;
  }
  //xer_fprint(stdout, &asn_DEF_NR_CG_ConfigInfo, cg_configinfo);
  return cg_configinfo;
}

NR_UE_NR_Capability_t *get_ue_nr_cap_from_cg_config_info(const NR_CG_ConfigInfo_t *cgci)
{
  /* INTO DU handler */
  if (cgci->criticalExtensions.present != NR_CG_ConfigInfo__criticalExtensions_PR_c1)
    return NULL;
  if (!cgci->criticalExtensions.choice.c1
      || cgci->criticalExtensions.choice.c1->present != NR_CG_ConfigInfo__criticalExtensions__c1_PR_cg_ConfigInfo)
    return NULL;

  const NR_CG_ConfigInfo_IEs_t *cgci_ie = cgci->criticalExtensions.choice.c1->choice.cg_ConfigInfo;
  if (!cgci_ie->ue_CapabilityInfo)
    return NULL;

  // Decode UE-CapabilityRAT-ContainerList
  const OCTET_STRING_t *cap_buf = cgci_ie->ue_CapabilityInfo;
  NR_UE_CapabilityRAT_ContainerList_t *clist = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                        &asn_DEF_NR_UE_CapabilityRAT_ContainerList,
                                        (void **)&clist,
                                        cap_buf->buf,
                                        cap_buf->size);

  if (dec_rval.code != RC_OK) {
    LOG_W(NR_MAC,
          "Failed to decode NR_UE_CapabilityRAT_ContainerList (%zu bits), size of OCTET_STRING %lu\n",
          dec_rval.consumed,
          cap_buf->size);
    return NULL;
  }
  NR_UE_NR_Capability_t *cap = get_nr_cap(clist);
  ASN_STRUCT_FREE(asn_DEF_NR_UE_CapabilityRAT_ContainerList, clist);
  return cap;
}

NR_CellGroupConfig_t *clone_CellGroupConfig(const NR_CellGroupConfig_t *orig)
{
  uint8_t buf[16636];
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig, NULL, orig, buf, sizeof(buf));
  AssertFatal(enc_rval.encoded > 0, "could not clone CellGroupConfig: problem while encoding\n");
  NR_CellGroupConfig_t *cloned = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_CellGroupConfig, (void **)&cloned, buf, enc_rval.encoded, 0, 0);
  AssertFatal(dec_rval.code == RC_OK && dec_rval.consumed == enc_rval.encoded,
              "could not clone CellGroupConfig: problem while decodung\n");
  return cloned;
}

static NR_UE_info_t *create_new_UE(gNB_MAC_INST *mac,
                                   nr_cell_sched_t *cell,
                                   uint32_t cu_id,
                                   const NR_CG_ConfigInfo_t *cgci,
                                   const NR_ServingCellConfig_t *source_dedicated)
{
  const bool is_SA = IS_SA_MODE(get_softmodem_params());
  rnti_t rnti;
  if (get_softmodem_params()->phy_test) {
    AssertFatal(get_num_connected_ue(&mac->UE_info) == 0, "phytest: UE already present\n");
    rnti = 0x1234;
  } else {
    bool found = nr_mac_get_new_rnti(&mac->UE_info, &rnti);
    if (!found)
      return NULL;
  }

  f1_ue_data_t new_ue_data = {.secondary_ue = cu_id};
  bool success = du_add_f1_ue_data(rnti, &new_ue_data);
  DevAssert(success);
  NR_UE_info_t *UE = get_new_nr_ue_inst(&mac->UE_info.uid_allocator, rnti, NULL, cell);
  AssertFatal(UE->uid < MAX_MOBILES_PER_GNB, "cannot create UE context, UE context setup failure not implemented\n");

  NR_CellGroupConfig_t *cellGroupConfig = NULL;
  NR_COMMON_channels_t *cc = &cell->common_channels;
  const NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
  if (is_SA) {
    cellGroupConfig = get_initial_cellGroupConfig(UE->uid, UE->is_redcap, scc, cell, &mac->rlc_config, ssb_index);
    cellGroupConfig->spCellConfig->reconfigurationWithSync = get_reconfiguration_with_sync(UE->rnti, UE->uid, scc, mac->frame);
    if (source_dedicated)
      release_stale_ho_source_bwps(cellGroupConfig->spCellConfig->spCellConfigDedicated, source_dedicated);
    UE->local_bwp_id = cell->radio_config.first_active_bwp;
  } else {
    NR_UE_NR_Capability_t *cap = get_ue_nr_cap_from_cg_config_info(cgci);
    NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
    UE->uecap_fs_ids = feature_set_ids_handler(cap, frequencyInfoDL, EN_DC);
    cellGroupConfig = get_default_secondaryCellGroup(scc, cap, &UE->uecap_fs_ids, 1, 1, &cell->radio_config, cell, UE->uid, ssb_index);
    cellGroupConfig->spCellConfig->reconfigurationWithSync = get_reconfiguration_with_sync(UE->rnti, UE->uid, scc, mac->frame);
    // TODO: in NSA we assign capabilities here, otherwise outside => not logic
    UE->capability = cap;
    UE->local_bwp_id = 1; // get_default_secondaryCellGroup sets 1st active BWP as 1
  }
  // note: we don't pass the cellGroupConfig to add_new_nr_ue() because we need
  // the uid to create the CellGroupConfig (which is in the UE context created
  // by add_new_nr_ue(); it's a kind of chicken-and-egg problem), so below we
  // complete the UE context with the information that add_new_nr_ue() would
  // have added
  AssertFatal(cellGroupConfig != NULL, "out of memory\n");
  UE->CellGroup = cellGroupConfig;

  if (get_softmodem_params()->phy_test) {
    // phytest mode: we don't set up RA, etc
    free_and_zero(UE->ra); // test-mode: UE will not do RA
    bool res = add_connected_nr_ue(mac, UE);
    DevAssert(res);
  } else {
    if (!add_new_ra_ue(&mac->UE_info, UE)) {
      release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
      LOG_E(NR_MAC, "UE list full while creating new UE\n");
      return NULL;
    }
    nr_mac_prepare_ra_ue(mac, cell, UE);

    if (is_SA) {
      /* SRB1 is added to RLC and MAC in the handler later */
      nr_rlc_activate_srb0(UE->rnti, UE, NULL);
    }
  }
  return UE;
}

/** @brief Encode CellGroupConfig to byte array for transparent forwarding in F1AP messages.
 * The encoded bytes are intended for transparent forwarding to the UE without
 * decode/re-encode cycles per TS 38.473 transparency requirements.
 * @param cellGroup CellGroupConfig to encode
 * @return Encoded byte array */
byte_array_t encode_cellgroup_config(const NR_CellGroupConfig_t *cellGroup)
{
  byte_array_t cgc = {0};
  ssize_t encoded = uper_encode_to_new_buffer(&asn_DEF_NR_CellGroupConfig, NULL, cellGroup, (void **)&cgc.buf);
  AssertFatal(encoded > 0, "Could not encode CellGroup\n");
  cgc.len = encoded;
  return cgc;
}

/** @brief Handle RRC container from UE Context Setup/Modification request.
 * Per 3GPP TS 38.473, the RRCContainer IE contains an RRC message (e.g., DL-DCCH-Message
 * as defined in TS 38.331) that is encapsulated in a PDCP PDU. The DU forwards this
 * container transparently to the UE via SRB1 without decode/re-encode cycles.
 * @param rrc_container RRC container
 * @param rnti RNTI of the UE */
static void handle_ue_context_rrc_container(const byte_array_t *rrc_container, const rnti_t rnti)
{
  DevAssert(rrc_container);
  logical_chan_id_t id = 1;
  nr_rlc_srb_recv_sdu(rnti, id, rrc_container->buf, rrc_container->len);
}

/** @brief Get and clone CellGroupConfig for UE Context Setup/Modification response.
 * The source depends on re-establishment state:
 * - CellGroup: Currently active/runtime CellGroupConfig used by MAC for scheduling.
 *   During re-establishment, spCellConfig is removed from it (per TS 38.331 §5.3.7.2).
 * - reconfigCellGroup: Staging area that holds a complete CellGroupConfig saved before
 *   modifications. During re-establishment, it still contains spCellConfig (saved before
 *   removal). It becomes the new CellGroup when reconfiguration completes.
 * During re-establishment, we must clone from reconfigCellGroup to get a complete
 * CellGroupConfig with spCellConfig for transparent forwarding to the UE.
 * @param UE UE context
 * @return Cloned CellGroupConfig */
static NR_CellGroupConfig_t *get_cellgroup_config(NR_UE_info_t *UE)
{
  if (UE->reestablish_rlc && UE->reconfigCellGroup != NULL) {
    return clone_CellGroupConfig(UE->reconfigCellGroup);
  } else {
    return clone_CellGroupConfig(UE->CellGroup);
  }
}

/** @brief Update CellGroupConfig for RRC re-establishment procedure.
 * This function prepares a complete CellGroupConfig for gNB-DU Configuration Query response
 * during RRC re-establishment.
 * The function sets reestablishRLC flags for all RLC bearers except SRB1.
 * SRB1 is removed from the bearer list since it is already re-established during
 * RRCReestablishment and should not be included. */
static void update_cellgroup_for_reestablishment(NR_UE_info_t *UE, NR_CellGroupConfig_t *new_CellGroup)
{
  DevAssert(new_CellGroup);
  DevAssert(UE->reestablish_rlc);
  if (!new_CellGroup->spCellConfig) {
    LOG_E(NR_MAC, "UE %04x: CellGroupConfig has no spCellConfig during reestablishment "
          "(possible double reestablishment race), skipping reestablishRLC update\n", UE->rnti);
    return;
  }
  LOG_I(NR_MAC, "UE %04x: Re-establishment detected, setting reestablishRLC flags\n", UE->rnti);
  struct NR_CellGroupConfig__rlc_BearerToAddModList *addmod = new_CellGroup->rlc_BearerToAddModList;
  if (addmod && addmod->list.count > 0) {
    LOG_I(NR_MAC, "UE %04x: CellGroupConfig has %d bearers:\n", UE->rnti, addmod->list.count);
    for (int i = 0; i < addmod->list.count; ++i) {
      NR_RLC_BearerConfig_t *bearer = addmod->list.array[i];
      int rb_type = bearer->servedRadioBearer->present;
      int rb_id = (rb_type == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity)
                      ? bearer->servedRadioBearer->choice.srb_Identity
                      : bearer->servedRadioBearer->choice.drb_Identity;
      if (rb_type == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity && rb_id == 1) {
        asn_sequence_del(&addmod->list, i, 1);
        --i;
        continue;
      }
      asn1cCallocOne(addmod->list.array[i]->reestablishRLC, NR_RLC_BearerConfig__reestablishRLC_true);
    }
  }
}

void ue_context_setup_request(const f1ap_ue_context_setup_req_t *req)
{
  const bool is_SA = IS_SA_MODE(get_softmodem_params());
  gNB_MAC_INST *mac = RC.nrmac[0];
  // TODO: Add proper error handling
  nr_cell_sched_t *cell = nr_mac_get_cell_by_cgi(mac, req->plmn, req->nr_cellid);
  AssertFatal(cell != NULL, "cell with CGI (cellid=%lu) not found\n", req->nr_cellid);

  f1ap_ue_context_setup_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
  };

  bool ue_id_provided = req->gNB_DU_ue_id != NULL;

  const f1ap_cu_to_du_rrc_info_t *cu2du = &req->cu_to_du_rrc_info;
  NR_CG_ConfigInfo_t *cg_configinfo = NULL;
  if (cu2du->cg_configinfo != NULL)
    cg_configinfo = get_cg_config_info(cu2du->cg_configinfo->buf, cu2du->cg_configinfo->len);
  NR_UE_NR_Capability_t *ue_cap = NULL;
  NR_CellGroupConfig_t *source_cg = NULL;
  if (cu2du->ho_prep_info != NULL) {
    get_ue_cap_and_source_cg_from_ho_prep_info(cu2du->ho_prep_info->buf, cu2du->ho_prep_info->len, &ue_cap, &source_cg);
  } else if (cu2du->ue_cap != NULL) {
    ue_cap = get_ue_nr_cap(*req->gNB_DU_ue_id, cu2du->ue_cap->buf, cu2du->ue_cap->len);
  }
  NR_MeasurementTimingConfiguration_t *mtc = NULL;
  if (cu2du->meas_timing_config != NULL)
    mtc = get_nr_mtc(cu2du->meas_timing_config->buf, cu2du->meas_timing_config->len);

  const NR_ServingCellConfig_t *source_dedicated = source_cg ? source_cg->spCellConfig->spCellConfigDedicated : NULL;

  /* inclusion of CG-ConfigInfo in SA mode is interpreted
   * as NR-DC activation in the DU
   */
  if (is_SA && cg_configinfo) {
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, source_cg);
    return nrdc_ue_context_setup_request(req);
  }

  AssertFatal(is_SA || (cg_configinfo != NULL), "CG-ConfigInfo needed for NSA/phy-test/do-ra\n");

  NR_SCHED_LOCK(&mac->sched_lock);

  NR_UE_info_t *UE = NULL;
  if (!ue_id_provided) {
    UE = create_new_UE(mac, cell, req->gNB_CU_ue_id, cg_configinfo, source_dedicated);
  } else {
    DevAssert(is_SA);
    UE = find_connected_nr_ue(&mac->UE_info, *req->gNB_DU_ue_id);
  }
  /* create_new_UE() returns NULL if no RNTI is available or the RA list is full
   * (e.g. many simultaneous handovers into this cell), find_connected_nr_ue() if the CU
   * sent an unknown gNB_DU_ue_id: check here, before any dereference below */
  AssertFatal(UE, "no UE found or could not be created, but UE Context Setup Failed not implemented\n");
  resp.gNB_DU_ue_id = UE->rnti;
  if (!ue_id_provided) {
    /* new UE: report the C-RNTI we allocated back to the CU */
    resp.crnti = malloc_or_fail(sizeof(*resp.crnti));
    *resp.crnti = UE->rnti;
  }

  NR_CellGroupConfig_t *new_CellGroup = get_cellgroup_config(UE);

  // Needed for DRB Setup (e.g., RLC might reduce SN size)
  UE->capability = ue_cap;
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  UE->uecap_fs_ids = feature_set_ids_handler(ue_cap, scc->downlinkConfigCommon->frequencyInfoDL, is_SA ? NR_SA : EN_DC);

  if (req->srbs_len > 0) {
    resp.srbs_len = handle_ue_context_srbs_setup(UE, req->srbs_len, req->srbs, &resp.srbs, new_CellGroup, &mac->rlc_config);
  }

  if (req->drbs_len > 0) {
    resp.drbs_len =
        handle_ue_context_drbs_setup(UE, req->drbs_len, req->drbs, &resp.drbs, new_CellGroup, &mac->rlc_config);
  }

  if (req->rrc_container != NULL) {
    handle_ue_context_rrc_container(req->rrc_container, UE->rnti);
  }

  if (ue_cap != NULL && cg_configinfo == NULL) {
    // store the new UE capabilities, and update the cellGroupConfig
    // only to be done if we did not already update through the cg_configinfo
    update_cellGroupConfig(new_CellGroup, UE->uid, UE->capability, &UE->uecap_fs_ids, cell, scc);
  }
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, source_cg);

  /* During re-establishment, prepare CellGroupConfig for UE Context Setup response.
   * Per TS 38.401 §8.7: when a UE re-establishes on a different DU, the CU triggers
   * UE Context Setup on the new DU. The DU must respond with a CellGroupConfig that has
   * reestablishRLC flags set for all RLC bearers except SRB1. This prepares the CellGroupConfig
   * for transparent forwarding to the UE per TS 38.473 transparency requirements. */
  if (UE->reestablish_rlc) {
    update_cellgroup_for_reestablishment(UE, new_CellGroup);
  }

  if (!ue_id_provided && cg_configinfo == NULL) {
    /* new UE: tell the UE to reestablish RLC */
    struct NR_CellGroupConfig__rlc_BearerToAddModList *addmod = new_CellGroup->rlc_BearerToAddModList;
    for (int i = 0; i < addmod->list.count; ++i) {
      NR_RLC_BearerConfig_t *bc = addmod->list.array[i];
      asn1cCallocOne(bc->reestablishRLC, NR_RLC_BearerConfig__reestablishRLC_true);
      nr_rlc_reestablish_entity(UE->rnti, bc->logicalChannelIdentity);
    }
  }

  resp.du_to_cu_rrc_info.cell_group_config = encode_cellgroup_config(new_CellGroup);

  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->reconfigCellGroup);
  UE->reconfigCellGroup = new_CellGroup;
  int ss_type = cg_configinfo ? NR_SearchSpace__searchSpaceType_PR_ue_Specific: NR_SearchSpace__searchSpaceType_PR_common;
  configure_UE_BWP(cell, scc, UE, false, ss_type, -1, -1);

  if (mtc) {
    /* creates a suitable measGap config to be used in the gNB */
    UE->measgap_config = create_measgap_config(mtc, UE->current_DL_BWP.scs, cell->radio_config.minRXTXTIME);
    /* encodes the measGapConfig created, if useful (or not!) */
    byte_array_t *mgc = calloc_or_fail(1, sizeof(*mgc));
    mgc->buf = calloc_or_fail(1, 1024);
    mgc->len = encode_measgap_config(&UE->measgap_config, mgc->buf);
    resp.du_to_cu_rrc_info.meas_gap_config = mgc;
  }

  NR_SCHED_UNLOCK(&mac->sched_lock);

  mac->mac_rrc.ue_context_setup_response(&resp);

  /* free the memory we allocated above */
  free_ue_context_setup_resp(&resp);
  ASN_STRUCT_FREE(asn_DEF_NR_CG_ConfigInfo, cg_configinfo);
  ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, mtc);
}

void ue_context_modification_request(const f1ap_ue_context_mod_req_t *req)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  f1ap_ue_context_mod_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  /* special handling for NR-DC UEs */
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_connected_nr_ue(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);
  if (UE && UE->nrdc_mode) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return nrdc_ue_context_modification_request(req);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  NR_UE_NR_Capability_t *ue_cap = NULL;
  if (req->cu_to_du_rrc_info != NULL) {
    AssertFatal(req->cu_to_du_rrc_info->cg_configinfo == NULL, "CG-ConfigInfo not handled\n");
    if (req->cu_to_du_rrc_info->ue_cap) {
      byte_array_t *b = req->cu_to_du_rrc_info->ue_cap;
      ue_cap = get_ue_nr_cap(req->gNB_DU_ue_id, b->buf, b->len);
    }
  }

  NR_SCHED_LOCK(&mac->sched_lock);
  UE = find_connected_nr_ue(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);
  if (!UE) {
    LOG_E(NR_MAC, "could not find UE with RNTI %04x\n", req->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  // SpCell CGI is optional in UE Context Modification (only present on handover).
  // TODO: Add proper error handling
  nr_cell_sched_t *cell = (req->plmn && req->nr_cellid)
                              ? nr_mac_get_cell_by_cgi(mac, *req->plmn, *req->nr_cellid)
                              : UE->pcell;
  AssertFatal(cell != NULL, "cell not found (no UE pcell or NRCGI provided)\n");

  NR_CellGroupConfig_t *new_CellGroup = get_cellgroup_config(UE);

  if (req->srbs_len > 0) {
    resp.srbs_len = handle_ue_context_srbs_setup(UE, req->srbs_len, req->srbs, &resp.srbs, new_CellGroup, &mac->rlc_config);
  }

  if (req->drbs_len > 0) {
    resp.drbs_len = handle_ue_context_drbs_setup(UE, req->drbs_len, req->drbs, &resp.drbs, new_CellGroup, &mac->rlc_config);
  }

  if (req->drbs_rel_len > 0) {
    handle_ue_context_drbs_release(UE, req->drbs_rel_len, req->drbs_rel, new_CellGroup);
  }

  if (req->rrc_container != NULL) {
    handle_ue_context_rrc_container(req->rrc_container, req->gNB_DU_ue_id);
  }

  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  if (req->reconfig_compl && *req->reconfig_compl != RRCreconf_success) {
    LOG_E(NR_MAC,
          "RRC reconfiguration outcome unsuccessful, but no rollback mechanism implemented to come back to old configuration\n");
  } else if (req->reconfig_compl) {
    LOG_I(NR_MAC, "DU received confirmation of successful RRC Reconfiguration\n");
    if (UE->reconfigCellGroup) {
      /** During handover, target DU never sends RRC Reconfiguration (source DU does),
       * so ack_reconfig is never called on target DU - this warning is expected.
       * If RRC Reconfiguration was sent via PDSCH, ack_reconfig should have been called when UE ACKed it.
       * If not, this warning would indicate a bug. */
      LOG_W(NR_MAC, "reconfigCellGroup still present, did we miss ACK for RRCReconfiguration?\n");
      ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->CellGroup);
      UE->CellGroup = UE->reconfigCellGroup;
      UE->reconfigCellGroup = NULL;
    }
    if (UE->reestablish_rlc) {
      for (int i = 1; i < seq_arr_size(&UE->UE_sched_ctrl.lc_config); ++i) {
        nr_lc_config_t *c = seq_arr_at(&UE->UE_sched_ctrl.lc_config, i);
        c->suspended = false;
        LOG_I(NR_MAC, "UE %04x: Re-establishing RLC for LCID %d\n", UE->rnti, c->lcid);
        nr_rlc_reestablish_entity(req->gNB_DU_ue_id, c->lcid);
      }
      UE->reestablish_rlc = false;
    }
    // we re-configure the BWP to apply the CellGroup and to use UE specific Search Space with DCIX1
    configure_UE_BWP(cell, scc, UE, false, NR_SearchSpace__searchSpaceType_PR_ue_Specific, -1, -1);
    nr_mac_clean_cellgroup(UE->CellGroup);

    // Restore dedicated BWP if configure_UE_BWP() bumped the UE to BWP0 for RA
    if (UE->pending_bwp_restore) {
      UE->pending_bwp_restore = false;
      int bwp_id = cell->radio_config.first_active_bwp;
      LOG_A(NR_MAC, "UE %04x: Restoring dedicated BWP %d after RA parking on BWP0 by RRCReconfiguration(nr_mac_trigger_reconfiguration)\n", UE->rnti, bwp_id);
      // BWP switch (beam arg = -1); UE->local_bwp_id is updated on UE Context Modification Confirm
      nr_mac_trigger_reconfiguration(mac, cell, UE, bwp_id, -1);
    }
  }

  if (ue_cap != NULL) {
    // store the new UE capabilities, and update the cellGroupConfig
    ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->capability);
    UE->capability = ue_cap;
    NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
    UE->uecap_fs_ids = feature_set_ids_handler(ue_cap, frequencyInfoDL, IS_SA_MODE(get_softmodem_params()) ? NR_SA : EN_DC);
    LOG_I(NR_MAC, "UE %04x: received capabilities, updating CellGroupConfig\n", UE->rnti);
    update_cellGroupConfig(new_CellGroup, UE->uid, UE->capability, &UE->uecap_fs_ids, cell, scc);
  }

  /* 3GPP TS 38.473 Clause 8.3.4: If gNB-DU Configuration Query is present, include CellGroupConfig
   * (CU requested CellGroupConfig for transparent forwarding), or when SRB/DRB/cap change needs an
   * RLC update. */
  const bool query = req->gNB_DU_Configuration_Query != NULL && *req->gNB_DU_Configuration_Query;
  const bool nrdc_mcg_drb_rel_only = req->drbs_rel_len == 1 && req->srbs_len == 0 && req->drbs_len == 0
                                     && req->cu_to_du_rrc_info == NULL && req->rrc_container == NULL;
  const bool has_bearer_or_cap = req->srbs_len > 0 || req->drbs_len > 0 || req->drbs_rel_len > 0 || ue_cap != NULL;
  const bool include_cgc = query || (has_bearer_or_cap && !nrdc_mcg_drb_rel_only);

  if (include_cgc) {
    LOG_I(NR_MAC, "UE %04x: include CellGroupConfig in DU to CU transfer IE (query %d)\n", UE->rnti, query);

    if (UE->reestablish_rlc) {
      update_cellgroup_for_reestablishment(UE, new_CellGroup);
    }

    /* Encode CellGroupConfig for transparent forwarding in the DU to CU trasnfer IE
     * CU will forward these encoded bytes directly to UE without decode/re-encode cycles */
    resp.du_to_cu_rrc_info = calloc_or_fail(1, sizeof(du_to_cu_rrc_information_t));
    resp.du_to_cu_rrc_info->cell_group_config = encode_cellgroup_config(new_CellGroup);

    // Replace reconfigCellGroup with new_CellGroup (now contains complete config with spCellConfig restored)
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->reconfigCellGroup);
    UE->reconfigCellGroup = new_CellGroup;
    configure_UE_BWP(cell, scc, UE, false, NR_SearchSpace__searchSpaceType_PR_common, -1, -1);
  } else {
    /* hack: for NR-DC, remove the RLC bearer from the MCG group config
     * of the UE. This bearer is transfered to the SCG.
     * We check if the modification request asks to remove a bearer and adds
     * no bearer. If yes, we are doing the NR-DC scenario (hopefully).
     * This hack has to be removed in the future. (No other solution
     * was found.)
     */
    if (nrdc_mcg_drb_rel_only) {
      /* remove the RLC from UE->CellGroup->rlc_BearerToAddModList */
      long lcid = get_lcid_from_drbid(req->drbs_rel[0].id);
      LOG_I(NR_MAC, "NR-DC hack: remove RLC lcid %ld from UE cell group config\n", lcid);
      NR_CellGroupConfig_t *cellGroupConfig = UE->CellGroup;
      int idx = 0;
      while (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
        const NR_RLC_BearerConfig_t *bc = cellGroupConfig->rlc_BearerToAddModList->list.array[idx];
        if (bc->logicalChannelIdentity == lcid)
          break;
        idx++;
      }
      DevAssert(idx < cellGroupConfig->rlc_BearerToAddModList->list.count);
      asn_sequence_del(&cellGroupConfig->rlc_BearerToAddModList->list, idx, 1);
    }
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, new_CellGroup); // we actually don't need it
  }

  if (req->transm_action_ind != NULL) {
    AssertFatal(*req->transm_action_ind == TransmActionInd_STOP, "Transmission Action Indicator restart not handled yet\n");
    nr_transmission_action_indicator_stop(mac, UE);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  mac->mac_rrc.ue_context_modification_response(&resp);

  /* free the memory we allocated above */
  free_ue_context_mod_resp(&resp);
}

void ue_context_modification_confirm(const f1ap_ue_context_modif_confirm_t *confirm)
{
  LOG_I(NR_MAC, "Received UE Context Modification Confirm for UE %04x\n", confirm->gNB_DU_ue_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_connected_nr_ue(&mac->UE_info, confirm->gNB_DU_ue_id);
  if (UE == NULL) {
    LOG_E(NR_MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Confirm\n", confirm->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }
  if (UE->cm_info.trigger_info == BEAM_SWITCH) {
    LOG_I(NR_MAC, "[UE %x] Switching to beam with ID %d (from %d)\n", UE->rnti, UE->cm_info.new_state, UE->UE_beam_index);
    UE->UE_beam_index = UE->cm_info.new_state;
  } else if (UE->cm_info.trigger_info == BWP_SWITCH)
    UE->local_bwp_id = UE->cm_info.new_state;
  UE->cm_info.trigger_info = NO_TRIGGER;
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (confirm->rrc_container_length > 0) {
    logical_chan_id_t id = 1;
    nr_rlc_srb_recv_sdu(confirm->gNB_DU_ue_id, id, confirm->rrc_container, confirm->rrc_container_length);
  }
}

void ue_context_modification_refuse(const f1ap_ue_context_modif_refuse_t *refuse)
{
  LOG_W(NR_MAC, "Received UE Context Modification Refuse for %04x\n", refuse->gNB_DU_ue_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_connected_nr_ue(&RC.nrmac[0]->UE_info, refuse->gNB_DU_ue_id);
  if (UE == NULL) {
    LOG_E(NR_MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Refuse\n", refuse->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  /* if the UE Context Modification Required procedure was initiated
   * for a RRC reconfigurtion after Msg.3 with C-RNTI MAC CE, if the CU
   * refuses, it cannot do this reconfiguration, leaving the UE in an
   * unconfigured state. Therefore, we just free all RA-related info, and
   * request the release of the UE.  */
  bool release = UE->cm_info.trigger_info == MSG3_CRNTI;
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->reconfigCellGroup);
  UE->reconfigCellGroup = NULL;
  UE->cm_info.trigger_info = NO_TRIGGER;
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (release) {
    LOG_W(NR_MAC, "Context Modification Required after MSG3 with C-RNTI, requesting release\n");
    f1ap_ue_context_rel_req_t request = {
      .gNB_CU_ue_id = refuse->gNB_CU_ue_id,
      .gNB_DU_ue_id = refuse->gNB_DU_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK,
      .cause_value = F1AP_CauseRadioNetwork_procedure_cancelled,
    };
    mac->mac_rrc.ue_context_release_request(&request);
  }
}

void ue_context_release_command(const f1ap_ue_context_rel_cmd_t *cmd)
{
  /* mark UE as to be deleted after PUSCH failure */
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_connected_nr_ue(&mac->UE_info, cmd->gNB_DU_ue_id);
  UE = UE ? UE : find_ra_ue(&mac->UE_info, cmd->gNB_DU_ue_id);
  if (UE == NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    LOG_W(NR_MAC, "UE Context Release Command for unknown RNTI %04x/CU UE ID %d\n", cmd->gNB_DU_ue_id, cmd->gNB_CU_ue_id);
    f1ap_ue_context_rel_cplt_t complete = {
        .gNB_CU_ue_id = cmd->gNB_CU_ue_id,
        .gNB_DU_ue_id = cmd->gNB_DU_ue_id,
    };
    mac->mac_rrc.ue_context_release_complete(&complete);
    return;
  }

  instance_t f1inst = get_f1_gtp_instance();
  if (f1inst >= 0)
    newGtpuDeleteAllTunnels(f1inst, cmd->gNB_DU_ue_id);

  if (UE->UE_sched_ctrl.ul_failure || !cmd->rrc_container) {
    /* The UE is already not connected anymore or we have nothing to forward*/
    nr_mac_release_ue(mac, cmd->gNB_DU_ue_id);
    nr_mac_trigger_release_complete(mac, cmd->gNB_DU_ue_id);
  } else if (cmd->rrc_container && cmd->srb_id){
    /* UE is in sync: forward release message and mark to be deleted
     * after UL failure */
    byte_array_t *rrc_cont = cmd->rrc_container;
    nr_rlc_srb_recv_sdu(cmd->gNB_DU_ue_id, *cmd->srb_id, rrc_cont->buf, rrc_cont->len);
    nr_mac_trigger_release_timer(&UE->UE_sched_ctrl, UE->current_UL_BWP.scs);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
}

static void process_reestablishment(gNB_MAC_INST *mac, uint32_t new_dl_rrc_id, uint32_t old_dl_rrc_id)
{
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_ra_ue(&mac->UE_info, new_dl_rrc_id);
  if (!UE) {
    LOG_E(MAC, "ERROR: couldn't find UE with RNTI %04x, ignoring DL RRC Message Transfer\n", new_dl_rrc_id);
    return;
  }

  NR_UE_info_t *oldUE = find_connected_nr_ue(&mac->UE_info, old_dl_rrc_id);
  if (!oldUE) {
    /* No matching UE-associated logical F1-connection for the old gNB-DU UE F1AP ID.
     * Per TS 38.473, if there's no matching connection, there's nothing to release. */
    // Cross-DU re-establishment: hold off reconfiguration on Msg3 C-RNTI
    LOG_W(NR_MAC,
          "DL RRC Message Transfer: old gNB-DU UE F1AP ID %04x has no matching UE-associated F1-connection, nothing to relese\n",
          old_dl_rrc_id);
    UE->reestablish_rlc = true;
    /* Clean up any F1 UE data associated with the old gNB-DU UE F1AP ID */
    if (du_exists_f1_ue_data(old_dl_rrc_id))
      du_remove_f1_ue_data(old_dl_rrc_id);
    return;
  }

  // Per TS 38.401: "Find UE context based on old gNB-DU UE F1AP ID, replace old C-RNTI/PCI with new C-RNTI/PCI"
  // to do that, we have to remove the old UE from connected list first, as the
  // data structure in the connected list uses the RNTI for lookup
  oldUE = remove_connected_nr_ue(&mac->UE_info, oldUE->rnti);

  rnti_t new_rnti = UE->rnti;
  // assigning the old RNTI to the new UE so that mac_remove_nr_ue prints correct RNTI when removing
  UE->rnti = oldUE->rnti;
  oldUE->rnti = new_rnti;
  for (int i = 1; i < seq_arr_size(&oldUE->UE_sched_ctrl.lc_config); ++i) {
    const nr_lc_config_t *c = seq_arr_at(&oldUE->UE_sched_ctrl.lc_config, i);
    nr_lc_config_t new = *c;
    new.suspended = true;
    nr_mac_add_lcid(&UE->UE_sched_ctrl, &new);
  }
  // need to move the oldUE to RA list because it still needs to transmit MSG4
  NR_RA_t *temp_ra = UE->ra;
  oldUE->ra = temp_ra;
  UE->ra = NULL;
  NR_UE_sched_ctrl_t temp_sc;
  memcpy(&temp_sc, &UE->UE_sched_ctrl, sizeof(NR_UE_sched_ctrl_t));
  memcpy(&UE->UE_sched_ctrl, &oldUE->UE_sched_ctrl, sizeof(NR_UE_sched_ctrl_t));
  memcpy(&oldUE->UE_sched_ctrl, &temp_sc, sizeof(NR_UE_sched_ctrl_t));
  mac_remove_nr_ue(mac, UE->rnti);
  bool success = add_new_ra_ue(&mac->UE_info, oldUE);
  AssertFatal(success, "can not add UE: F1 UE ctxt fail to be sent\n");
  nr_rlc_remove_ue(new_dl_rrc_id);
  nr_rlc_update_id(old_dl_rrc_id, new_dl_rrc_id);
  instance_t f1inst = get_f1_gtp_instance();
  if (f1inst >= 0) // we actually use F1-U
    gtpv1u_update_ue_id(f1inst, old_dl_rrc_id, new_dl_rrc_id);

  /* Per TS 38.331 5.3.7.2: the UE releases the spCellConfig, so we drop it
   * from the current configuration. It will be reapplied when the
   * reconfiguration has succeeded (indicated by the CU).
   * Guard against double reestablishment: if reestablish_rlc is already set,
   * reconfigCellGroup was saved by the first reestablishment and
   * CellGroup.spCellConfig is already NULL — don't overwrite. */
  if (!oldUE->reestablish_rlc) {
    asn_copy(&asn_DEF_NR_CellGroupConfig, (void **)&oldUE->reconfigCellGroup, oldUE->CellGroup);
    ASN_STRUCT_FREE(asn_DEF_NR_SpCellConfig, oldUE->CellGroup->spCellConfig);
    oldUE->CellGroup->spCellConfig = NULL;
    reset_sc_info(&oldUE->sc_info);
    configure_UE_BWP(oldUE->pcell,
                     oldUE->pcell->common_channels.ServingCellConfigCommon,
                     oldUE,
                     true,
                     NR_SearchSpace__searchSpaceType_PR_common,
                     -1,
                     -1);
  } else {
    LOG_W(NR_MAC,
          "UE %04x: reestablishment while other reestablishment still pending keeping saved reconfigCellGroup with spCellConfig\n",
          oldUE->rnti);
  }
  oldUE->reestablish_rlc = true;
  /* Per TS 38.331 clause 5.3.7.4: apply gNB RLC configuration for SRB1 to match the UE RLC configuration defined in 9.2.1.
   * Use configuration file values for timers t_poll_retransmit, t_reassembly and t_status_prohibit */
  nr_rlc_configuration_t rlc_configuration = mac->rlc_config;
  rlc_configuration.srb.poll_pdu = -1;
  rlc_configuration.srb.poll_byte = -1;
  rlc_configuration.srb.max_retx_threshold = 8;
  rlc_configuration.srb.sn_field_length = 12;
  NR_RLC_Config_t *rlc_Config = nr_srb_config(&rlc_configuration);
  nr_rlc_reconfigure_entity(new_dl_rrc_id, 1, rlc_Config);
  ASN_STRUCT_FREE(asn_DEF_NR_RLC_Config, rlc_Config);
}

/** @brief Process a DL RRC MESSAGE TRANSFER. Handles delivery of an RRC message to a UE
 * via the F1AP DL RRC MESSAGE TRANSFER procedure, as specified in TS 38.473. This procedure
 * is also responsible for re-establishing UE context when required (e.g., during RRC connection
 * reestablishment).
 * @param dl_rrc Pointer to the DL RRC MESSAGE TRANSFER data structure. */
void dl_rrc_message_transfer(const f1ap_dl_rrc_message_t *dl_rrc)
{
  LOG_D(NR_MAC,
        "DL RRC Message Transfer with %d bytes for RNTI %04x SRB %d\n",
        dl_rrc->rrc_container_length,
        dl_rrc->gNB_DU_ue_id,
        dl_rrc->srb_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  if (!du_exists_f1_ue_data(dl_rrc->gNB_DU_ue_id)) {
    LOG_D(NR_MAC, "No CU UE ID stored for UE RNTI %04x, adding CU UE ID %d\n", dl_rrc->gNB_DU_ue_id, dl_rrc->gNB_CU_ue_id);
    f1_ue_data_t new_ue_data = {.secondary_ue = dl_rrc->gNB_CU_ue_id};
    bool success = du_add_f1_ue_data(dl_rrc->gNB_DU_ue_id, &new_ue_data);
    DevAssert(success);
  }

  /* Per TS 38.473: "The DL RRC MESSAGE TRANSFER message shall include, if available,
   * the old gNB-DU UE F1AP ID IE so that the gNB-DU can retrieve the existing UE context
   * in RRC connection reestablishment procedure, as defined in TS 38.401"
   *
   * "If the gNB-DU identifies the UE-associated logical F1-connection by the gNB-DU UE F1AP ID
   * IE in the DL RRC MESSAGE TRANSFER message and the old gNB-DU UE F1AP ID IE is included,
   * it shall release the old gNB-DU UE F1AP ID and the related configurations associated
   * with the old gNB-DU UE F1AP ID." */
  if (dl_rrc->old_gNB_DU_ue_id != NULL) {
    if (*dl_rrc->old_gNB_DU_ue_id != dl_rrc->gNB_DU_ue_id) {
      NR_SCHED_LOCK(&mac->sched_lock);
      process_reestablishment(mac, dl_rrc->gNB_DU_ue_id, *dl_rrc->old_gNB_DU_ue_id);
      NR_SCHED_UNLOCK(&mac->sched_lock);
    } else
      LOG_E(NR_MAC, "Current and old gNB DU UE ID are the same (%04x), cannot do reestablishment\n", dl_rrc->gNB_DU_ue_id);
  }
  /* the DU ue id is the RNTI */
  nr_rlc_srb_recv_sdu(dl_rrc->gNB_DU_ue_id, dl_rrc->srb_id, dl_rrc->rrc_container, dl_rrc->rrc_container_length);
}

/** @brief For CN-initiated Paging, enqueue one MAC record per F1AP/NGAP Paging.
 * TS 38.413 §8.5.1.2: each NGAP PAGING shall result in one radio page per cell.
 * One received indication is mapped to one DU queue entry. */
void f1_paging(const f1ap_paging_t *paging)
{
  DevAssert(paging);
  if (paging->identity_type != F1AP_PAGING_IDENTITY_CN_UE) {
    LOG_W(MAC, "RAN UE paging identity not supported\n");
    return;
  }

  const uint64_t fiveg_s_tmsi = paging->identity.cn_ue_paging_identity;
  const uint16_t ue_id = paging->ue_identity_index_value % 1024;
  gNB_MAC_INST *mac = RC.nrmac[0];
  LOG_I(MAC, "Paging transfer: ue_identity_index=%u, 5G-S-TMSI=0x%012lu\n", paging->ue_identity_index_value, fiveg_s_tmsi);
  for (int i = 0; i < paging->n_cells; i++) {
    // TODO: Add proper error handling
    nr_cell_sched_t *cell = nr_mac_get_cell_by_cgi(mac, paging->cells[i].plmn, paging->cells[i].nr_cellid);
    if (!cell) {
      LOG_W(MAC,
            "Cell with cellid %lu and plmn %d %d doesn't exist, ignoring\n",
            paging->cells[i].nr_cellid,
            paging->cells[i].plmn.mcc,
            paging->cells[i].plmn.mnc);
      continue;
    }
    nr_mac_pcch_enqueue(mac, cell, fiveg_s_tmsi, ue_id);
  }
}

void trp_information_request(const f1ap_trp_information_req_t *req)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  positioning_config_t *positioning_config = mac->positioning_config;
  if (positioning_config == NULL) {
    LOG_E(NR_PHY, "No TRPs configured for positioning in the configuration file\n");
    f1ap_trp_information_failure_t fail = {.transaction_id = req->transaction_id};
    fail.cause = F1AP_CAUSE_RADIO_NETWORK;
    mac->mac_rrc.trp_information_failure(&fail);
    return;
  }
  uint8_t NumTRPs = positioning_config->num_trp;
  f1ap_trp_information_resp_t resp = {0};

  resp.transaction_id = req->transaction_id;
  // Check if the TRP_ID matches with the list sent in the trp information request
  if (req->has_trp_list) {
    uint8_t trp_resp_len = 0;
    uint32_t trp_list_length = req->trp_list.trp_list_length;
    DevAssert(trp_list_length > 0);
    resp.trp_information_list.trp_information_item =
        calloc_or_fail(trp_list_length, sizeof(*resp.trp_information_list.trp_information_item));
    for (int i = 0; i < trp_list_length; i++) {
      for (int j = 0; j < NumTRPs; j++) {
        if (positioning_config->trps[j].id == req->trp_list.trp_list_item[i].trp_id) {
          resp.trp_information_list.trp_information_item[trp_resp_len].trp_id = req->trp_list.trp_list_item[i].trp_id;
          trp_resp_len++;
        }
      }
    }
    resp.trp_information_list.trp_information_item_length = trp_resp_len;
  } else {
    resp.trp_information_list.trp_information_item =
        calloc_or_fail(NumTRPs, sizeof(*resp.trp_information_list.trp_information_item));
    for (int i = 0; i < NumTRPs; i++) {
      f1ap_trp_information_t *trp_info_item = &resp.trp_information_list.trp_information_item[i];
      trp_info_item->trp_id = positioning_config->trps[i].id;
      create_trp_info_item(req, trp_info_item, positioning_config, i);
    }
    resp.trp_information_list.trp_information_item_length = NumTRPs;
  }
  mac->mac_rrc.trp_information_response(&resp);
  free_trp_information_resp(&resp);
}

void positioning_information_request(const f1ap_positioning_information_req_t *req)
{
  f1ap_positioning_information_resp_t resp = {.gNB_CU_ue_id = req->gNB_CU_ue_id, .gNB_DU_ue_id = req->gNB_DU_ue_id};
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_UE_info_t *UE = find_connected_nr_ue(&mac->UE_info, req->gNB_DU_ue_id);
  NR_UE_UL_BWP_t *current_UL_BWP = &UE->current_UL_BWP;
  NR_ServingCellConfigCommon_t *scc = UE->pcell->common_channels.ServingCellConfigCommon;
  if (current_UL_BWP->srs_Config) {
    resp.srs_configuration = calloc_or_fail(1, sizeof(*resp.srs_configuration));
    *resp.srs_configuration = cp_rrc_to_f1ap_srs_configuration(current_UL_BWP, scc);
  }
  mac->mac_rrc.positioning_information_response(&resp);
  free_positioning_information_resp(&resp);
}

void positioning_activation_request(const f1ap_positioning_activation_req_t *req)
{
  f1ap_positioning_activation_resp_t resp = {.gNB_CU_ue_id = req->gNB_CU_ue_id, .gNB_DU_ue_id = req->gNB_DU_ue_id};
  gNB_MAC_INST *mac = RC.nrmac[0];
  // Currently in OAI-LMF its hardcoded to aperiodic SRS
  // Ignoring the SRS type and considering periodic SRS
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_connected_nr_ue(&mac->UE_info, req->gNB_DU_ue_id);
  add_pos_act_ue_context(mac, UE->rnti);
  NR_SCHED_UNLOCK(&mac->sched_lock);
  mac->mac_rrc.positioning_activation_response(&resp);
}

void positioning_measurement_request(const f1ap_positioning_measurement_req_t *req)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  positioning_measurement_info_t *pos_meas_info = &mac->pos_meas_info;
  pos_meas_info->meas_req = cp_positioning_measurement_req(req);
  pos_meas_info->active = true;
  NR_SCHED_UNLOCK(&mac->sched_lock);
}
