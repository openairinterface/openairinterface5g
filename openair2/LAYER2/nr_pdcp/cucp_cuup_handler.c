/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "cucp_cuup_handler.h"
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "NR_DRB-ToAddMod.h"
#include "NR_DRB-ToAddModList.h"
#include "NR_PDCP-Config.h"
#include "NR_QFI.h"
#include "NR_SDAP-Config.h"
#include "RRC/NR/nr_rrc_common.h"
#include "SDAP/nr_sdap/nr_sdap_entity.h"
#include "asn_internal.h"
#include "assertions.h"
#include "common/utils/T/T.h"
#include "common/utils/oai_asn1.h"
#include "constr_TYPE.h"
#include "cuup_cucp_if.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "openair2/COMMON/e1ap_messages_types.h"
#include "openair2/E1AP/e1ap_common.h"
#include "openair2/E1AP/lib/e1ap_bearer_context_management.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/SDAP/nr_sdap/nr_sdap.h"
#include "openair3/ocp-gtpu/gtp_itf.h"

static NR_DRB_ToAddMod_t *get_rrc_drb_to_addmod(const DRB_nGRAN_to_setup_t *drb,
                                                const long sessionId,
                                                const security_indication_t *sec)
{
  DevAssert(drb);
  NR_DRB_ToAddMod_t *ie = calloc_or_fail(1, sizeof(*ie));
  ie->drb_Identity = drb->id;
  ie->cnAssociation = calloc_or_fail(1, sizeof(*ie->cnAssociation));
  ie->cnAssociation->present = NR_DRB_ToAddMod__cnAssociation_PR_sdap_Config;

  // sdap_Config
  asn1cCalloc(ie->cnAssociation->choice.sdap_Config, sdap_config);
  sdap_config->pdu_Session = sessionId;
  /* SDAP: convert from internal representation to ASN.1 enum:
   * Internal: false=absent (default), true=present
   * ASN.1: 0=present, 1=absent */
  const bearer_context_sdap_config_t *sdap = &drb->sdap_config;
  sdap_config->sdap_HeaderDL = sdap->sDAP_Header_DL ? NR_SDAP_Config__sdap_HeaderDL_present : NR_SDAP_Config__sdap_HeaderDL_absent;
  sdap_config->sdap_HeaderUL = sdap->sDAP_Header_UL ? NR_SDAP_Config__sdap_HeaderUL_present : NR_SDAP_Config__sdap_HeaderUL_absent;
  sdap_config->defaultDRB = sdap->defaultDRB;
  asn1cCalloc(sdap_config->mappedQoS_FlowsToAdd, FlowsToAdd);
  for (int j = 0; j < drb->numQosFlow2Setup; j++) {
    asn1cSequenceAdd(FlowsToAdd->list, NR_QFI_t, qfi);
    *qfi = drb->qosFlows[j].qfi;
  }
  sdap_config->mappedQoS_FlowsToRelease = NULL;

  // pdcp_Config
  ie->reestablishPDCP = NULL;
  ie->recoverPDCP = NULL;
  asn1cCalloc(ie->pdcp_Config, pdcp_config);
  asn1cCalloc(pdcp_config->drb, drbCfg);
  asn1cCallocOne(drbCfg->discardTimer, drb->pdcp_config.discardTimer);
  asn1cCallocOne(drbCfg->pdcp_SN_SizeUL, drb->pdcp_config.pDCP_SN_Size_UL);
  asn1cCallocOne(drbCfg->pdcp_SN_SizeDL, drb->pdcp_config.pDCP_SN_Size_DL);
  drbCfg->headerCompression.present = NR_PDCP_Config__drb__headerCompression_PR_notUsed;
  drbCfg->headerCompression.choice.notUsed = 0;
  drbCfg->integrityProtection = NULL;
  drbCfg->statusReportRequired = NULL;
  drbCfg->outOfOrderDelivery = NULL;
  pdcp_config->moreThanOneRLC = NULL;
  pdcp_config->t_Reordering = calloc(1, sizeof(*pdcp_config->t_Reordering));
  *pdcp_config->t_Reordering = drb->pdcp_config.reorderingTimer;
  pdcp_config->ext1 = NULL;
  if (sec) {
    if (sec->integrityProtectionIndication == SECURITY_REQUIRED || sec->integrityProtectionIndication == SECURITY_PREFERRED) {
      asn1cCallocOne(drbCfg->integrityProtection, NR_PDCP_Config__drb__integrityProtection_enabled);
    }
    if (sec->confidentialityProtectionIndication == SECURITY_NOT_NEEDED) {
      asn1cCalloc(pdcp_config->ext1, ext1);
      asn1cCallocOne(ext1->cipheringDisabled, NR_PDCP_Config__ext1__cipheringDisabled_true);
    }
  }

  return ie;
}

static void cuup_release_pdu_session_up(uint32_t ue_id, int pdusession_id);

/** @brief N3 GTP-U Error Indication callback (TS 29.281 clause 7.3.1)
 * TS 23.527 clause 5.3.3.1: 5G-AN shall release PDU session resources immediately
 * and initiate NGAP PDU Session Resource Notify. Release N3/F1-U/SDAP here, then
 * signal CU-CP via Bearer Context Modification Required (TS 38.463 clause 8.3.3)
 * through cuup_cucp_if (direct ITTI or E1AP). */
static void n3_error_indication(const gtpv1u_error_indication_ind_t *ind)
{
  if (N3GTPUInst == NULL || ind->gtp_instance != *N3GTPUInst)
    return; // Error Indication is for N3 only

  cuup_release_pdu_session_up(ind->ue_id, ind->pdusession_id);

  e1ap_bearer_mod_required_t req = {0};
  req.gNB_cu_cp_ue_id = ind->ue_id;
  req.gNB_cu_up_ue_id = ind->ue_id;
  req.numPDUSessionsRem = 1;
  req.pduSessionRem = calloc_or_fail(1, sizeof(*req.pduSessionRem));
  req.pduSessionRem[0].sessionId = ind->pdusession_id;
  req.pduSessionRem[0].cause.type = E1AP_CAUSE_TRANSPORT;
  req.pduSessionRem[0].cause.value = E1AP_TRANSPORT_CAUSE_RESOURCE_UNAVAILABLE;
  get_e1_if()->bearer_mod_required(&req);
  free_e1ap_context_mod_required(&req);
}

/** @brief Fill and send request to create GTP-U tunnel (F1-U) */
static UP_TL_information_t f1_drb_gtpu_create(const instance_t f1inst,
                                              const gtpv1u_gnb_create_tunnel_req_t *req)
{
  UP_TL_information_t out = {0};

  LOG_I(GTPU, "Incoming DRB %d / PDU Session %d - UL TEID %d\n", req->incoming_rb_id, req->pdusession_id, req->outgoing_teid);

  gtpv1u_gnb_create_tunnel_resp_t resp = {0};
  int ret = gtpv1u_create_ngu_tunnel(f1inst, req, &resp, cu_f1u_data_req, NULL, NULL);
  AssertFatal(ret >= 0, "Unable to create GTP-U tunnel for F1-U\n");
  AssertFatal(resp.gnb_addr.length == sizeof(in_addr_t),
              "GTP tunnel response address length %d does not match IPv4 size %zu\n",
              resp.gnb_addr.length,
              sizeof(in_addr_t));
  memcpy(&out.tlAddress, resp.gnb_addr.buffer, resp.gnb_addr.length);
  out.teId = resp.gnb_NGu_teid;

  return out;
}

static instance_t get_n3_gtp_instance(void)
{
  const e1ap_upcp_inst_t *inst = getCxtE1(0);
  AssertFatal(inst, "need to have E1 instance\n");
  return inst->gtpInstN3;
}

/** @brief Fill and send request to create GTP-U tunnel (N3). One tunnel per PDU session. */
static UP_TL_information_t n3_gtpu_create(const gtpv1u_gnb_create_tunnel_req_t *req)
{
  instance_t n3inst = get_n3_gtp_instance();
  UP_TL_information_t out = {0};

  LOG_I(GTPU, "N3 GTP-U tunnel: PDUSession=%d/UL TEID=0x%08x\n", req->pdusession_id, req->outgoing_teid);

  gtpv1u_gnb_create_tunnel_resp_t resp = {0};
  int ret = gtpv1u_create_ngu_tunnel(n3inst, req, &resp, nr_pdcp_data_req_drb, sdap_data_req, n3_error_indication);
  AssertFatal(ret >= 0, "Unable to create GTP-U tunnel for N3\n");
  AssertFatal(resp.gnb_addr.length == sizeof(in_addr_t),
              "GTP tunnel response address length %d does not match IPv4 size %zu\n",
              resp.gnb_addr.length,
              sizeof(in_addr_t));
  memcpy(&out.tlAddress, resp.gnb_addr.buffer, resp.gnb_addr.length);
  out.teId = resp.gnb_NGu_teid;

  return out;
}

static instance_t get_f1_gtp_instance(void)
{
  const f1ap_cudu_inst_t *inst = getCxt(0);
  if (!inst)
    return -1; // means no F1
  return inst->gtpInst;
}

/** @brief Add bearer in PDCP/SDAP for 5GC association (SA) */
static void e1_add_bearers(const int ue_id, const NR_DRB_ToAddModList_t *addMod, const nr_pdcp_entity_security_keys_and_algos_t *sp)
{
  for (int i = 0; i < addMod->list.count; i++) {
    NR_DRB_ToAddMod_t *drb = addMod->list.array[i];
    DevAssert(drb->cnAssociation);
    DevAssert(drb->cnAssociation->present != NR_DRB_ToAddMod__cnAssociation_PR_NOTHING);
    DevAssert(drb->cnAssociation->present == NR_DRB_ToAddMod__cnAssociation_PR_sdap_Config);
    DevAssert(drb->cnAssociation->choice.sdap_Config);
    // get SDAP config
    sdap_config_t sdap = nr_sdap_get_config(GNB_FLAG_YES, drb->cnAssociation->choice.sdap_Config, drb->drb_Identity);
    // add SDAP entity
    nr_sdap_addmod_entity(GNB_FLAG_YES, ue_id, &sdap);
    // add PDCP entity
    nr_pdcp_add_drb(GNB_FLAG_YES, ue_id, drb->pdcp_Config, &sdap, sp);
  }
}

/** @brief Process QoS flows from E1AP request and populate DRB setup response
 * @param resp_drb DRB setup response structure to populate
 * @param req_drb DRB to setup request structure
 * Processes each QoS flow from E1AP request per TS 38.463.
 * Multiple QoS flows can be mapped to a single DRB per TS 38.331. */
static void fill_e1_qos_flows_setup(DRB_nGRAN_setup_t *resp_drb, const DRB_nGRAN_to_setup_t *req_drb)
{
  DevAssert(resp_drb);
  DevAssert(req_drb);
  for (int k = 0; k < req_drb->numQosFlow2Setup; k++) {
    const qos_flow_to_setup_t *qosflow2Setup = &req_drb->qosFlows[k];
    // Populate E1AP response structure with QFI (mandatory IE per TS 38.463 §9.3.3.1)
    qos_flow_list_t *qosflowSetup = &resp_drb->qosFlows[resp_drb->numQosFlowSetup++];
    qosflowSetup->qfi = qosflow2Setup->qfi;
    LOG_D(E1AP, "Setup DRB %ld with QFI %ld\n", resp_drb->id, qosflow2Setup->qfi);
  }
}

/** @brief Fill DRB setup response structure (both for bearer setup and modification)
 * @param req_drb DRB to setup request structure
 * @param secInfo Security information (can be NULL)
 * @return DRB setup response structure */
static DRB_nGRAN_setup_t fill_e1_drb_setup(const DRB_nGRAN_to_setup_t *req_drb)
{
  DevAssert(req_drb);
  DRB_nGRAN_setup_t resp_drb = {0};
  resp_drb.id = req_drb->id;
  // Fill QoS flows from E1AP request and populate the E1 response
  fill_e1_qos_flows_setup(&resp_drb, req_drb);
  return resp_drb;
}

void e1_bearer_context_setup(const e1ap_bearer_setup_req_t *req)
{
  bool need_ue_id_mgmt = e1_used();

  /* mirror the CU-CP UE ID for CU-UP */
  uint32_t cu_up_ue_id = req->gNB_cu_cp_ue_id;
  f1_ue_data_t ued = {.secondary_ue = req->gNB_cu_cp_ue_id};
  if (need_ue_id_mgmt && !cu_exists_f1_ue_data(cu_up_ue_id)) {
    bool success = cu_add_f1_ue_data(cu_up_ue_id, &ued);
    DevAssert(success);
    LOG_I(E1AP, "adding UE with CU-CP UE ID %d and CU-UP UE ID %d\n", req->gNB_cu_cp_ue_id, cu_up_ue_id);
  }

  instance_t f1inst = get_f1_gtp_instance();

  e1ap_bearer_setup_resp_t resp = {
      .gNB_cu_cp_ue_id = req->gNB_cu_cp_ue_id,
      .gNB_cu_up_ue_id = cu_up_ue_id,
      .numPDUSessions = req->numPDUSessions,
  };
  resp.pduSession = calloc_or_fail(req->numPDUSessions, sizeof(*resp.pduSession));

  /** Loop over the number of PDU Sessions to setup
   * for each PDU session, a GTP-U N3 Tunnel Create Request
   * and fill the relevant item in E1 Bearer Setup Response */
  for (int i = 0; i < resp.numPDUSessions; ++i) {
    const pdu_session_to_setup_t *req_pdu = req->pduSession + i;
    LOG_I(E1AP, "UE %d: set up PDU session ID %ld (%d bearers)\n", cu_up_ue_id, req_pdu->sessionId, req_pdu->numDRB2Setup);

    pdu_session_setup_t *resp_pdu = resp.pduSession + i;
    resp_pdu->id = req_pdu->sessionId;
    resp_pdu->numDRBSetup = req_pdu->numDRB2Setup;

    /* Loop though the number of DRB to setup
     * if required, for each DRB a F1-U GTP-U Tunnel Create Request
     * and fill the relevant item in E1 Bearer Setup Response */
    NR_DRB_ToAddModList_t DRB_configList = {0};
    for (int d = 0; d < resp_pdu->numDRBSetup; d++) {
      const DRB_nGRAN_to_setup_t *req_drb = &req_pdu->DRBnGRanList[d];
      DRB_nGRAN_setup_t *resp_drb = &resp_pdu->DRBnGRanList[d];

      /* Fill DRB item in E1 Bearer Setup Response */
      *resp_drb = fill_e1_drb_setup(req_drb);

      /* Add DRB to addmod list for CUUP bearer setup */
      NR_DRB_ToAddMod_t *drb_to_add = get_rrc_drb_to_addmod(req_drb, req_pdu->sessionId, &req_pdu->securityIndication);
      ASN_SEQUENCE_ADD(&DRB_configList.list, drb_to_add);

      /* F1-U tunnel setup */
      if (f1inst >= 0) { /* we have F1(-U) */
        teid_t dummy_teid = 0xffff; // we will update later with answer from DU
        in_addr_t dummy_address = {0}; // IPv4, updated later with answer from DU
        gtpv1u_gnb_create_tunnel_req_t f1_tunnel_req = {.incoming_rb_id = req_drb->id,
                                                        .outgoing_teid = dummy_teid,
                                                        .pdusession_id = req_drb->id,
                                                        .ue_id = cu_up_ue_id,
                                                        .dst_addr.length = 32};
        memcpy(&f1_tunnel_req.dst_addr.buffer, &dummy_address, sizeof(uint8_t) * 4);
        up_params_t *up = &resp_drb->UpParamList[resp_drb->numUpParam++];
        up->tl_info = f1_drb_gtpu_create(f1inst, &f1_tunnel_req);
        LOG_D(E1AP,
              "Created F1-U tunnel for DRB %ld (TEID %u tlAddress %x)\n",
              req_drb->id,
              up->tl_info.teId,
              up->tl_info.tlAddress);
      }
    }

    /* Create PDCP/SDAP entities for each DRB */
    nr_pdcp_entity_security_keys_and_algos_t security_parameters = {0};
    security_parameters.ciphering_algorithm = req->secInfo.cipheringAlgorithm;
    security_parameters.integrity_algorithm = req->secInfo.integrityProtectionAlgorithm;
    memcpy(security_parameters.ciphering_key, req->secInfo.encryptionKey, NR_K_KEY_SIZE);
    memcpy(security_parameters.integrity_key, req->secInfo.integrityProtectionKey, NR_K_KEY_SIZE);
    e1_add_bearers(cu_up_ue_id, &DRB_configList, &security_parameters);
    ASN_STRUCT_RESET(asn_DEF_NR_DRB_ToAddModList, &DRB_configList.list);

    /** GTP tunnel for N3/to core: one GTP-U tunnel per PDU session. */
    LOG_D(E1AP, "In %s: GTP tunnel for N3/to core for PDU Session %ld\n", __func__, req_pdu->sessionId);
    // N3 GTP-U Tunnel Request for PDU Session
    gtpv1u_gnb_create_tunnel_req_t n3_tunnel_req = {.ue_id = cu_up_ue_id,
                                                    .outgoing_teid = req_pdu->UP_TL_information.teId,
                                                    .pdusession_id = req_pdu->sessionId,
                                                    .incoming_rb_id = req_pdu->sessionId,
                                                    .dst_addr.length = 32};
    memcpy(&n3_tunnel_req.dst_addr.buffer, &req_pdu->UP_TL_information.tlAddress, sizeof(uint8_t) * 4); // only IPv4 now
    resp_pdu->tl_info = n3_gtpu_create(&n3_tunnel_req);

    // We assume all DRBs to setup have been setup successfully, so we always
    // send successful outcome in response and no failed DRBs
    resp_pdu->numDRBFailed = 0;
  }

  get_e1_if()->bearer_setup_response(&resp);
  free_e1ap_context_setup_response(&resp);
}

static void release_gtpu_tunnel(uint32_t ue_id, int pdusession_id)
{
  instance_t n3inst = get_n3_gtp_instance();

  // Handle N3 tunnel (1 tunnel per PDU session)
  if (n3inst >= 0) {
    LOG_I(E1AP, "UE %u: Deleting N3 tunnel for PDU session %d\n", ue_id, pdusession_id);
    int result = newGtpuDeleteOneTunnel(n3inst, ue_id, pdusession_id);
    if (result == GTPNOK) {
      LOG_E(E1AP, "UE %u: Failed to delete N3 tunnel for PDU session %d\n", ue_id, pdusession_id);
    }
  }
}

/** @brief Release DRB resources (F1-U tunnel, PDCP, update SDAP mapping) */
static void release_drb_resources(uint32_t ue_id, int drb_id)
{
  instance_t f1inst = get_f1_gtp_instance();
  if (f1inst >= 0) {
    int result = newGtpuDeleteOneTunnel(f1inst, ue_id, drb_id);
    if (result == GTPNOK)
      LOG_E(E1AP, "UE %u: Failed to delete F1 tunnel for DRB %d\n", ue_id, drb_id);
  }
  nr_pdcp_release_drb(ue_id, drb_id);
}

static void release_f1_drbs(uint32_t ue_id, int pdusession_id)
{
  // Handle F1 tunnels (1 tunnel per DRB, fetch PDCP entities and get DRB IDs)
  int drb_ids[MAX_DRBS_PER_UE];
  int n_drbs = nr_pdcp_get_drb_ids_for_pdusession(ue_id, pdusession_id, drb_ids);
  if (n_drbs > 0) {
    LOG_I(E1AP, "UE %u: Deleting %d F1 tunnels for PDU session %d\n", ue_id, n_drbs, pdusession_id);
    // Delete each F1 tunnel using the DRB IDs
    for (int i = 0; i < n_drbs; i++) {
      release_drb_resources(ue_id, drb_ids[i]);
    }
  } else {
    LOG_W(E1AP, "UE %u: No DRBs found for PDU session %d\n", ue_id, pdusession_id);
  }
}

/** @brief Release UP resources for one PDU session (N3, F1-U, SDAP) */
static void cuup_release_pdu_session_up(uint32_t ue_id, int pdusession_id)
{
  release_gtpu_tunnel(ue_id, pdusession_id);
  release_f1_drbs(ue_id, pdusession_id);
  nr_sdap_delete_entity(ue_id, pdusession_id);
}

/**
 * @brief Fill Bearer Context Modification Response and send to callback
 */
void e1_bearer_context_modif(const e1ap_bearer_mod_req_t *req)
{
  AssertFatal(req->numPDUSessionsMod > 0 || req->numPDUSessionsRem > 0,
              "need at least one PDU session to modify or release (mod=%u, rel=%u)\n",
              req->numPDUSessionsMod,
              req->numPDUSessionsRem);

  e1ap_bearer_modif_resp_t modif = {
      .gNB_cu_cp_ue_id = req->gNB_cu_cp_ue_id,
      .gNB_cu_up_ue_id = req->gNB_cu_up_ue_id,
  };
  modif.pduSessionMod = calloc_or_fail(req->numPDUSessionsMod + req->numPDUSessionsRem,
                                      sizeof(*modif.pduSessionMod));

  instance_t f1inst = get_f1_gtp_instance();

  /* PDU Session Resource To Modify List (see 9.3.3.11 of TS 38.463) */
  for (int i = 0; i < req->numPDUSessionsMod; i++) {
    DevAssert(req->pduSessionMod[i].sessionId > 0);
    const pdu_session_to_mod_t *req_pdu_mod = &req->pduSessionMod[i];
    pdu_session_modif_t *resp_pdu_mod = &modif.pduSessionMod[i];
    LOG_I(E1AP,
          "UE %d: updating PDU session ID %ld (%d setup, %ld modify bearers)\n",
          req->gNB_cu_up_ue_id,
          req_pdu_mod->sessionId,
          req_pdu_mod->numDRB2Setup,
          req_pdu_mod->numDRB2Modify);
    resp_pdu_mod->id = req_pdu_mod->sessionId;
    resp_pdu_mod->numDRBModified = req_pdu_mod->numDRB2Modify;
    resp_pdu_mod->numDRBSetup = req_pdu_mod->numDRB2Setup;
    modif.numPDUSessionsMod++;

    /* DRBs to setup */
    NR_DRB_ToAddModList_t DRB_configList = {0};
    for (int d = 0; d < req_pdu_mod->numDRB2Setup; d++) {
      const DRB_nGRAN_to_setup_t *req_drb = &req_pdu_mod->DRBnGRanList[d];
      DRB_nGRAN_setup_t *resp_drb = &resp_pdu_mod->DRBnGRanSetupList[d];
      *resp_drb = fill_e1_drb_setup(req_drb);
      LOG_I(E1AP, "DRB %ld setup completed: numUpParam=%d\n", req_drb->id, resp_pdu_mod->DRBnGRanSetupList[d].numUpParam);
      /* Create PDCP/SDAP entities for DRB */
      NR_DRB_ToAddMod_t *drb_to_add = get_rrc_drb_to_addmod(req_drb, req_pdu_mod->sessionId, req_pdu_mod->securityIndication);
      ASN_SEQUENCE_ADD(&DRB_configList.list, drb_to_add);

      /* F1-U tunnel setup */
      if (f1inst >= 0) { /* we have F1(-U) */
        teid_t dummy_teid = 0xffff; // we will update later with answer from DU
        in_addr_t dummy_address = {0}; // IPv4, updated later with answer from DU
        gtpv1u_gnb_create_tunnel_req_t f1_tunnel_req = {.incoming_rb_id = req_drb->id,
                                                        .outgoing_teid = dummy_teid,
                                                        .pdusession_id = req_drb->id,
                                                        .ue_id = req->gNB_cu_up_ue_id,
                                                        .dst_addr.length = 32};
        memcpy(&f1_tunnel_req.dst_addr.buffer, &dummy_address, sizeof(uint8_t) * 4);
        up_params_t *up = &resp_drb->UpParamList[resp_drb->numUpParam++];
        up->tl_info = f1_drb_gtpu_create(f1inst, &f1_tunnel_req);
        LOG_D(E1AP,
              "Created F1-U tunnel for DRB %ld (TEID %u tlAddress %x)\n",
              req_drb->id,
              up->tl_info.teId,
              up->tl_info.tlAddress);
      }
    }

    nr_pdcp_entity_security_keys_and_algos_t security_parameters = {0};
    if (req->secInfo) {
      security_parameters.ciphering_algorithm = req->secInfo->cipheringAlgorithm;
      security_parameters.integrity_algorithm = req->secInfo->integrityProtectionAlgorithm;
      memcpy(security_parameters.ciphering_key, req->secInfo->encryptionKey, NR_K_KEY_SIZE);
      memcpy(security_parameters.integrity_key, req->secInfo->integrityProtectionKey, NR_K_KEY_SIZE);
    } else {
      /* don't change security settings if not present in the Bearer Context Modification Request */
      security_parameters.ciphering_algorithm = -1;
      security_parameters.integrity_algorithm = -1;
    }

    /* Create PDCP/SDAP entities for each DRB to add */
    e1_add_bearers(req->gNB_cu_up_ue_id, &DRB_configList, &security_parameters);
    ASN_STRUCT_RESET(asn_DEF_NR_DRB_ToAddModList, &DRB_configList.list);

    /* DRBs to modify */
    for (int j = 0; j < req_pdu_mod->numDRB2Modify; j++) {
      const DRB_nGRAN_to_mod_t *to_modif = &req_pdu_mod->DRBnGRanModList[j];
      DRB_nGRAN_modified_t *modified = &resp_pdu_mod->DRBnGRanModList[j];
      modified->id = to_modif->id;

      /* Collect new QFIs to add and update SDAP */
      if (to_modif->numQosFlowsMod > 0) {
        /* Reflect QoS flow add/modify in E1 response so CU-CP can detect QoS changes */
        DevAssert(to_modif->numQosFlowsMod <= E1AP_MAX_NUM_QOS_FLOWS);
        modified->numQosFlowSetup = to_modif->numQosFlowsMod;

        sdap_config_t sdap = {
            .pdusession_id = req_pdu_mod->sessionId,
            .drb_id = to_modif->id,
            .mappedQFIs2AddCount = to_modif->numQosFlowsMod,
        };

        for (int q = 0; q < to_modif->numQosFlowsMod; q++) {
          sdap.mappedQFIs2Add[q] = to_modif->qosFlows[q].qfi;
          modified->qosFlows[q].qfi = to_modif->qosFlows[q].qfi;
        }

        LOG_D(NR_RRC,
              "QoS flows mapping override: ue=%d pdu_session=%ld drb=%ld num_qfis=%d\n",
              req->gNB_cu_up_ue_id,
              req_pdu_mod->sessionId,
              to_modif->id,
              to_modif->numQosFlowsMod);
        nr_sdap_entity_update_qos_flows(req->gNB_cu_up_ue_id, &sdap);
      }

      if (to_modif->pdcp_config && to_modif->pdcp_config->pDCP_Reestablishment) {
        nr_pdcp_reestablishment(req->gNB_cu_up_ue_id,
                                to_modif->id,
                                false,
                                &security_parameters);
      }

      // PDCP Status Information requested
      if (to_modif->pdcp_sn_status_requested) {
        modified->pdcp_status = malloc_or_fail(sizeof(*modified->pdcp_status));
        nr_pdcp_count_t dl_count = {0};
        nr_pdcp_count_t ul_count = {0};
        nr_pdcp_get_drb_count_values(req->gNB_cu_cp_ue_id, to_modif->id, &ul_count, &dl_count);
        modified->pdcp_status->dl_count.hfn = dl_count.hfn;
        modified->pdcp_status->dl_count.sn = dl_count.sn;
        modified->pdcp_status->ul_count.hfn = ul_count.hfn;
        modified->pdcp_status->ul_count.sn = ul_count.sn;
      }

      // PDCP Status received (from source CU-UP)
      if (to_modif->pdcp_status) {
        DevAssert(to_modif->pdcp_config);
        DevAssert(to_modif->pdcp_config->pDCP_SN_Size_DL == to_modif->pdcp_config->pDCP_SN_Size_UL);
        bool is_sn_len_18 = to_modif->pdcp_config->pDCP_SN_Size_DL == NR_PDCP_Config__drb__pdcp_SN_SizeDL_len18bits;
        nr_pdcp_count_t dl_count = { .hfn = to_modif->pdcp_status->dl_count.hfn, .sn = to_modif->pdcp_status->dl_count.sn};
        nr_pdcp_count_t ul_count = { .hfn = to_modif->pdcp_status->ul_count.hfn, .sn = to_modif->pdcp_status->ul_count.sn};
        nr_pdcp_count_update(req->gNB_cu_cp_ue_id,
                             to_modif->id,
                             dl_count,
                             ul_count,
                             is_sn_len_18 ? LONG_SN_SIZE : SHORT_SN_SIZE);
      }

      if (f1inst < 0) // no F1-U?
        continue; // nothing to do

      /* Loop through DL UP Transport Layer params list
       * and update GTP tunnel outgoing addr and TEID */
      for (int k = 0; k < to_modif->numDlUpParam; k++) {
        in_addr_t addr = to_modif->DlUpParamList[k].tl_info.tlAddress;
        GtpuUpdateTunnelOutgoingAddressAndTeid(f1inst, req->gNB_cu_cp_ue_id, to_modif->id, addr, to_modif->DlUpParamList[k].tl_info.teId);
      }
    }

    /* DRBs to remove: delete F1 tunnel and release PDCP, update SDAP mapping */
    for (int j = 0; j < req_pdu_mod->n_drb_to_remove; j++) {
      const int drb_id = req_pdu_mod->drbs_to_remove[j].id;
      release_drb_resources(req->gNB_cu_up_ue_id, drb_id);
    }
  }

  /* PDU Session Resource To Remove List (see 9.3.3.12 of TS 38.463) */
  for (int i = 0; i < req->numPDUSessionsRem; i++) {
    modif.pduSessionMod[req->numPDUSessionsMod + i].id = req->pduSessionRem[i].sessionId;
    modif.numPDUSessionsMod++;
    cuup_release_pdu_session_up(req->gNB_cu_up_ue_id, req->pduSessionRem[i].sessionId);
  }

  get_e1_if()->bearer_modif_response(&modif);
  free_e1ap_context_mod_response(&modif);
}

static void remove_ue_e1(const uint32_t ue_id)
{
  bool need_ue_id_mgmt = e1_used();

  instance_t n3inst = get_n3_gtp_instance();
  instance_t f1inst = get_f1_gtp_instance();

  newGtpuDeleteAllTunnels(n3inst, ue_id);
  if (f1inst >= 0)  // is there F1-U?
    newGtpuDeleteAllTunnels(f1inst, ue_id);
  if (need_ue_id_mgmt) {
    // see issue #706: in monolithic, gNB will free PDCP of UE
    nr_pdcp_remove_UE(ue_id);
    cu_remove_f1_ue_data(ue_id);
  }
  nr_sdap_delete_ue_entities(ue_id);
}

void e1_bearer_release_cmd(const e1ap_bearer_release_cmd_t *cmd)
{
  LOG_I(E1AP, "releasing UE %d\n", cmd->gNB_cu_up_ue_id);
  remove_ue_e1(cmd->gNB_cu_up_ue_id);

  e1ap_bearer_release_cplt_t cplt = {
    .gNB_cu_cp_ue_id = cmd->gNB_cu_cp_ue_id,
    .gNB_cu_up_ue_id = cmd->gNB_cu_up_ue_id,
  };

  get_e1_if()->bearer_release_complete(&cplt);
}

/** @brief Bearer Context Modification Confirm (TS 38.463 clause 8.3.3).
 * UP teardown already done on Error Indication RX, Confirm is acknowledgement only. */
void e1_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *conf)
{
  DevAssert(conf);
  LOG_I(E1AP, "Received Bearer Context Modification Confirm for CU-CP UE E1AP ID %u\n", conf->gNB_cu_cp_ue_id);
}

void e1_reset(void)
{
  /* we get the list of all UEs from the PDCP, which maintains a list */
  ue_id_t ue_ids[MAX_MOBILES_PER_GNB];
  int num = nr_pdcp_get_num_ues(ue_ids, MAX_MOBILES_PER_GNB);
  for (uint32_t i = 0; i < num; ++i) {
    ue_id_t ue_id = ue_ids[i];
    LOG_W(E1AP, "releasing UE %ld\n", ue_id);
    remove_ue_e1(ue_id);
  }
}
