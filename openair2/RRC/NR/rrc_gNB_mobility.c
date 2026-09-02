/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>

#include "assertions.h"

#include "nr_rrc_proto.h"
#include "rrc_gNB_du.h"
#include "rrc_cell_management.h"
#include "rrc_gNB_radio_bearers.h"
#include "rrc_gNB_UE_context.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/lib/f1ap_ue_context.h"
#include "MESSAGES/asn1_msg.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "openair3/SECU/key_nas_deriver.h"
#include "openair2/RRC/NR/rrc_gNB_NGAP.h"
#include "NR_DL-DCCH-MessageType.h"
#include "rrc_cell_management.h"
#include "openair2/RRC/NR/rrc_gNB_mobility.h"

#ifdef E2_AGENT
#include "openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_rc_extern.h"
#endif

/* ------------------------------ Xn candidate tree ----------------------------------------*/
int rrc_xn_candidate_cmp(struct rrc_xn_candidate_s *a, struct rrc_xn_candidate_s *b)
{
  if (a->gnb_id < b->gnb_id) return -1;
  if (a->gnb_id > b->gnb_id) return  1;
  return 0;
}

RB_GENERATE(rrc_xn_cand_tree, rrc_xn_candidate_s, entry, rrc_xn_candidate_cmp);

static rrc_xn_candidate_t *rrc_xn_candidate_alloc(uint32_t gnb_id, sctp_assoc_t assoc_id)
{
  rrc_xn_candidate_t *cand = calloc_or_fail(1, sizeof(*cand));
  cand->gnb_id = gnb_id;
  cand->assoc_id = assoc_id;
  return cand;
}

static void rrc_xn_candidate_free(rrc_xn_candidate_t *cand)
{
  free(cand);
}

void rrc_add_xn_candidate(gNB_RRC_INST *rrc, uint32_t gnb_id, sctp_assoc_t assoc_id)
{
  /* Update assoc_id if the gNB_id is already registered (reconnect). */
  rrc_xn_candidate_t key = {.gnb_id = gnb_id};
  rrc_xn_candidate_t *existing = RB_FIND(rrc_xn_cand_tree, &rrc->xn_candidates, &key);
  if (existing) {
    existing->assoc_id = assoc_id;
    LOG_I(NR_RRC, "Xn candidate gNB_id 0x%x updated (assoc_id %d)\n", gnb_id, assoc_id);
    return;
  }

  rrc_xn_candidate_t *cand = rrc_xn_candidate_alloc(gnb_id, assoc_id);
  rrc_xn_candidate_t *dup = RB_INSERT(rrc_xn_cand_tree, &rrc->xn_candidates, cand);
  AssertFatal(dup == NULL, "duplicate Xn candidate gNB_id 0x%x despite RB_FIND miss above\n", gnb_id);
  LOG_I(NR_RRC, "Xn candidate gNB_id 0x%x registered (assoc_id %d)\n", gnb_id, assoc_id);
}

void rrc_remove_xn_candidate(gNB_RRC_INST *rrc, uint32_t gnb_id)
{
  rrc_xn_candidate_t key = {.gnb_id = gnb_id};
  rrc_xn_candidate_t *cand = RB_FIND(rrc_xn_cand_tree, &rrc->xn_candidates, &key);
  if (!cand) {
    LOG_W(NR_RRC, "Xn candidate gNB_id 0x%x not found for removal\n", gnb_id);
    return;
  }
  RB_REMOVE(rrc_xn_cand_tree, &rrc->xn_candidates, cand);
  rrc_xn_candidate_free(cand);
  LOG_I(NR_RRC, "Xn candidate gNB_id 0x%x removed (peer disconnected)\n", gnb_id);
}
/* ----------------------------------------------------------------------------------------- */

nr_handover_context_t *alloc_ho_ctx(ho_ctx_type_t type)
{
  nr_handover_context_t *ho_ctx = calloc_or_fail(1, sizeof(*ho_ctx));
  if (type == HO_CTX_SOURCE || type == HO_CTX_BOTH) {
    ho_ctx->source = calloc_or_fail(1, sizeof(*ho_ctx->source));
  }
  if (type == HO_CTX_TARGET || type == HO_CTX_BOTH) {
    ho_ctx->target = calloc_or_fail(1, sizeof(*ho_ctx->target));
  }
  return ho_ctx;
}

static void free_ho_ctx(nr_handover_context_t *ho_ctx)
{
  free(ho_ctx->source);
  if (ho_ctx->target)
    FREE_AND_ZERO_BYTE_ARRAY(ho_ctx->target->ue_ho_prep_info);
  free(ho_ctx->target);
  free(ho_ctx);
}

/** @brief Apply target handover context: update F1AP associations and RNTI */
void nr_rrc_apply_target_context(gNB_RRC_UE_t *UE)
{
  DevAssert(UE->ho_context);
  DevAssert(UE->ho_context->target);
  nr_ho_target_cu_t *target_ctx = UE->ho_context->target;
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  LOG_I(NR_RRC,
        "UE %d: update CU F1AP Context DU UE ID %u => %u and RNTI %04x => %04x\n",
        UE->rrc_ue_id,
        ue_data.secondary_ue,
        target_ctx->du_ue_id,
        UE->rnti,
        target_ctx->new_rnti);

  /* update F1 data: secondary UE association and DU association */
  ue_data.secondary_ue = target_ctx->du_ue_id;
  ue_data.du_assoc_id = target_ctx->cell->assoc_id;
  bool success = cu_update_f1_ue_data(UE->rrc_ue_id, &ue_data);
  DevAssert(success);

  /* update UE RNTI */
  UE->rnti = target_ctx->new_rnti;
}

/* \brief Initiate a handover of UE to a specific target cell handled by this
 * CU.
 * \param ue a UE context for which the handover should be triggered. The UE
 * context must be non-null (if the HO request comes from "outside" (N2, Xn),
 * the UE contex must be created first.
 * \param target_du the DU towards which to handover. Note: currently, the CU
 * is limited to one cell per DU, so DU and cell are equivalent here.
 * \param ho_ctxt contextual data for the type of handover (F1, N2, Xn) */
static void nr_initiate_handover(const gNB_RRC_INST *rrc,
                                 gNB_RRC_UE_t *ue,
                                 const nr_rrc_cell_container_t *source_cell,
                                 byte_array_t *ho_prep_info,
                                 ho_req_ack_t ack,
                                 ho_success_t success,
                                 ho_cancel_t cancel,
                                 ho_failure_t failure)
{
  DevAssert(rrc != NULL);
  DevAssert(ue != NULL);
  // source_cell might be NULL -> inter-CU handover
  DevAssert(ho_prep_info->buf != NULL && ho_prep_info->len > 0);
  DevAssert(ue->ho_context);
  DevAssert(ue->ho_context->target != NULL);

  // if any reconfiguration is ongoing, abort handover request
  for (int i = 0; i < NR_RRC_TRANSACTION_IDENTIFIER_NUMBER; ++i) {
    if (ue->xids[i] != RRC_ACTION_NONE) {
      LOG_E(NR_RRC, "UE %d: ongoig transaction %d (action %d), cannot trigger handover\n", ue->rrc_ue_id, i, ue->xids[i]);
      return;
    }
  }

  nr_handover_context_t *ho_ctx = ue->ho_context;
  const nr_rrc_cell_container_t *target_cell = ho_ctx->target->cell;
  DevAssert(target_cell != NULL);

  // we will know target->{du_ue_id,new_rnti} once we have UE ctxt setup
  // response
  ho_ctx->target->ho_req_ack = ack;
  ho_ctx->target->ho_success = success;
  ho_ctx->target->ho_failure = failure;

  const f1_ue_data_t ue_data = cu_get_f1_ue_data(ue->rrc_ue_id);
  if (source_cell != NULL) {
    DevAssert(source_cell->assoc_id == ue_data.du_assoc_id);
    ho_ctx->source->ho_cancel = cancel;

    ho_ctx->source->cell = source_cell;
    ho_ctx->source->du_ue_id = ue_data.secondary_ue;
    ho_ctx->source->old_rnti = ue->rnti;
    DevAssert(source_cell->assoc_id == ue_data.du_assoc_id);

    // Save the GTP-U tunnel info for source DU before process UE context setup request/response
    // since tunnel info will be updated by calling store_du_f1u_tunnel() in rrc_CU_process_ue_context_setup_response()
    FOR_EACH_SEQ_ARR(drb_t *, drb, &ue->drbs) {
      ho_ctx->source->old_du_tunnel_config = drb->du_tunnel_config;
    }

    // Store the old CellGroupConfig for reestablishment
    ho_ctx->source->old_cgc = copy_byte_array(ue->mcg);
  }

  LOG_A(NR_RRC,
        "Handover triggered for UE %u/RNTI %04x towards cell %ld/assoc_id %d/PCI %d\n",
        ue->rrc_ue_id,
        ue->rnti,
        target_cell->info.cell_id,
        target_cell->assoc_id,
        target_cell->info.pci);

  rrc_f1_ue_context_setup_for_target_du(rrc, ue, target_cell, ho_prep_info);
}

typedef struct {
  gNB_RRC_INST *rrc;
  f1ap_ue_context_mod_req_t *modification_req;
  sctp_assoc_t assoc_id;
} deliver_ue_ctxt_modification_data_t;

static void rrc_deliver_ue_ctxt_modif_req(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  UNUSED(ue_id);
  UNUSED(srb_id);
  UNUSED(sdu_id);
  DevAssert(deliver_pdu_data != NULL);
  deliver_ue_ctxt_modification_data_t *data = deliver_pdu_data;
  byte_array_t ba = {.buf = (uint8_t *) buf, .len = size};
  data->modification_req->rrc_container = &ba;
  data->rrc->mac_rrc.ue_context_modification_request(data->assoc_id, data->modification_req);
}

void rrc_gNB_trigger_reconfiguration_for_handover(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, uint8_t *rrc_reconf, int rrc_reconf_len)
{
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue->rrc_ue_id);

  TransmActionInd_t transmission_action_indicator = TransmActionInd_STOP;
  RETURN_IF_INVALID_ASSOC_ID(ue_data.du_assoc_id);
  f1ap_ue_context_mod_req_t ue_context_modif_req = {
      .gNB_CU_ue_id = ue->rrc_ue_id,
      .gNB_DU_ue_id = ue_data.secondary_ue,
      .transm_action_ind = &transmission_action_indicator,
  };
  deliver_ue_ctxt_modification_data_t data = {.rrc = rrc,
                                              .modification_req = &ue_context_modif_req,
                                              .assoc_id = ue_data.du_assoc_id};
  int srb_id = 1;
  nr_pdcp_data_req_srb(ue->rrc_ue_id,
                       srb_id,
                       rrc_gNB_mui++,
                       rrc_reconf_len,
                       (unsigned char *const)rrc_reconf,
                       rrc_deliver_ue_ctxt_modif_req,
                       &data);
#ifdef E2_AGENT
  uint32_t message_id = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;
  byte_array_t buffer_ba = {.len = rrc_reconf_len};
  buffer_ba.buf = rrc_reconf;
  signal_rrc_msg(DL_DCCH_NR_RRC_CLASS, message_id, buffer_ba);
#endif
}

static void nr_rrc_f1_ho_acknowledge(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  /* 3GPP TS 38.331 specifies that PDCP shall be re-established whenever the security key
   * used for the radio bearer changes, with some expections for SRB1 (i.e. when resuming
   * an RRC connection, or at the first reconfiguration after RRC connection reestablishment
   * in NR, do not re-establish PDCP */
  nr_rrc_reconfig_param_t params = get_RRCReconfiguration_params(rrc, UE, (1 << SRB2), true);
  UE->xids[params.transaction_id] = RRC_DEDICATED_RECONF;
  byte_array_t buffer = rrc_gNB_encode_RRCReconfiguration(UE, params);
  free_RRCReconfiguration_params(params);
  if (!buffer.len) {
    LOG_E(NR_RRC, "UE %d: Failed to generate RRCReconfiguration\n", UE->rrc_ue_id);
    return;
  }

  // F1 HO: handling of "source CU" information
  DevAssert(UE->ho_context->source != NULL);
  rrc_gNB_trigger_reconfiguration_for_handover(rrc, UE, buffer.buf, buffer.len);
  LOG_A(NR_RRC, "HO acknowledged: Send reconfiguration for UE %u/RNTI %04x...\n", UE->rrc_ue_id, UE->rnti);
  free_byte_array(buffer);

  /* Re-establish SRB2 according to clause 5.3.5.6.3 of 3GPP TS 38.331
   * (SRB1 is re-established with RRCReestablishment message)
   */
  int srb_id = 2;
  if (UE->Srb[srb_id].Active) {
    nr_pdcp_entity_security_keys_and_algos_t security_parameters;
    /* Derive the keys from kgnb */
    nr_derive_key(RRC_ENC_ALG, UE->ciphering_algorithm, UE->kgnb, security_parameters.ciphering_key);
    nr_derive_key(RRC_INT_ALG, UE->integrity_algorithm, UE->kgnb, security_parameters.integrity_key);
    security_parameters.integrity_algorithm = UE->integrity_algorithm;
    security_parameters.ciphering_algorithm = UE->ciphering_algorithm;
    nr_pdcp_reestablishment(UE->rrc_ue_id, srb_id, true, &security_parameters);
  }
}

static void nr_rrc_f1_ho_complete(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  DevAssert(UE->ho_context != NULL);
  nr_ho_source_cu_t *source_ctx = UE->ho_context->source;
  DevAssert(source_ctx != NULL);
  RETURN_IF_INVALID_ASSOC_ID(source_ctx->cell->assoc_id);
  f1ap_ue_context_rel_cmd_t cmd = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = source_ctx->du_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK,
      .cause_value = 5, // 5 = F1AP_CauseRadioNetwork_interaction_with_other_procedure
  };
  rrc->mac_rrc.ue_context_release_command(source_ctx->cell->assoc_id, &cmd);
  LOG_I(NR_RRC,
        "UE %d Handover: trigger release on cell PCI %d/assoc_id %d\n",
        UE->rrc_ue_id,
        source_ctx->cell->info.pci,
        source_ctx->cell->assoc_id);
}

static void nr_rrc_cancel_f1_ho(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  DevAssert(UE->ho_context != NULL);
  nr_ho_target_cu_t *target_ctx = UE->ho_context->target;
  DevAssert(target_ctx != NULL);
  f1ap_ue_context_rel_cmd_t cmd = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = target_ctx->du_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK, // better
      .cause_value = 5, // 5 = F1AP_CauseRadioNetwork_interaction_with_other_procedure
  };
  rrc->mac_rrc.ue_context_release_command(target_ctx->cell->assoc_id, &cmd);
}

/** @brief Generate the HandoverPreparationInformation to be carried
 * in the RRC Container (9.3.1.29 of 3GPP TS 38.413) of the Source
 * NG-RAN Node to Target NG-RAN Node Transparent Container IE */
static byte_array_t rrc_gNB_generate_HandoverPreparationInformation(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue)
{
  nr_rrc_reconfig_param_t params = get_RRCReconfiguration_params(rrc, ue, 0, false);
  params.ue_cap = ue->ue_cap_buffer;

  byte_array_t hoPrepInfo = get_HandoverPreparationInformation(&params);
  free_RRCReconfiguration_params(params);

  if (hoPrepInfo.len < 0) {
    LOG_E(NR_RRC, "HandoverPreparationInformation generation failed for UE %d\n", ue->rrc_ue_id);
    return hoPrepInfo;
  }

  LOG_D(NR_RRC, "HO LOG: Handover Preparation for UE %lu Encoded (%zd bytes)\n", ue->amf_ue_ngap_id, hoPrepInfo.len);

  return hoPrepInfo;
}

void nr_rrc_trigger_f1_ho(gNB_RRC_INST *rrc,
                          gNB_RRC_UE_t *ue,
                          const nr_rrc_cell_container_t *source_cell,
                          const nr_rrc_cell_container_t *target_cell)
{
  DevAssert(rrc != NULL);
  DevAssert(ue != NULL);

  /* 3GPP TS 38.473, clause 8.3.1.2: The gNB-CU shall only initiate UeContextSetupRequest
   * for handover when at least one DRB is setup for the UE. */
  if (!seq_arr_size(&ue->drbs)) {
    LOG_W(NR_RRC, "UE %u: no DRB configured, cannot trigger handover\n", ue->rrc_ue_id);
    return;
  }

  // Allocate handover context (both CU and DU)
  if (ue->ho_context != NULL) {
    LOG_E(NR_RRC, "Ongoing handover for UE %d, cannot trigger new\n", ue->rrc_ue_id);
    return;
  }

  byte_array_t hpi = rrc_gNB_generate_HandoverPreparationInformation(rrc, ue);
  if (hpi.len <= 0) {
    free_byte_array(hpi);
    LOG_E(NR_RRC, "UE %u: failed to generate HandoverPreparationInformation for F1 handover\n", ue->rrc_ue_id);
    return;
  }

  ue->ho_context = alloc_ho_ctx(HO_CTX_BOTH);
  ue->ho_context->target->cell = target_cell;

  // corresponds to a "handover request", 38.300 Sec 9.3.2.3
  // see also 38.413 Sec 9.3.1.29 for information on source-CU to target-CU
  // information (Source NG-RAN Node to Target NG-RAN Node Transparent Container)
  // here: target Cell is preselected, target CU has access to UE information
  // and therefore also the PDU sessions.
  ho_req_ack_t ack = nr_rrc_f1_ho_acknowledge;
  ho_success_t success = nr_rrc_f1_ho_complete;
  ho_cancel_t cancel = nr_rrc_cancel_f1_ho;
  nr_initiate_handover(rrc, ue, source_cell, &hpi, ack, success, cancel, NULL);
  free_byte_array(hpi);
}

void nr_rrc_finalize_ho(gNB_RRC_UE_t *ue)
{
  if (ue->ho_context->source)
    free_byte_array(ue->ho_context->source->old_cgc);
  free_ho_ctx(ue->ho_context);
  ue->ho_context = NULL;
}

static nr_rrc_cell_container_t *rrc_find_cell_at_index(gNB_RRC_INST *rrc, int target_idx)
{
  nr_rrc_du_container_t *du;
  RB_FOREACH(du, rrc_du_tree, &rrc->dus)
  {
    if (target_idx < seq_arr_size(&du->cells)) {
      nr_rrc_cell_container_t **cp = seq_arr_at(&du->cells, target_idx);
      return *cp;
    }
    target_idx -= (int)seq_arr_size(&du->cells);
  }
  return NULL;
}

void nr_HO_F1_trigger_telnet(gNB_RRC_INST *rrc, uint32_t rrc_ue_id)
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, rrc_ue_id);
  if (ue_context_p == NULL) {
    LOG_E(NR_RRC, "cannot find UE context for UE ID %d\n", rrc_ue_id);
    return;
  }
  gNB_RRC_UE_t *ue = &ue_context_p->ue_context;
  nr_rrc_cell_container_t *source_cell = rrc_get_pcell_for_ue(rrc, ue);
  if (source_cell == NULL) {
    LOG_E(NR_RRC, "cannot get source cell for UE %u\n", ue->rrc_ue_id);
    return;
  }

  /* Collect all cells across all DUs in a consistent order (DUs by assoc_id,
   * cells by seq_arr index within each DU), find the source cell's global
   * index, then hand over to (index + 1) % total. */
  int source_idx = -1;
  int total = 0;
  nr_rrc_du_container_t *du;
  RB_FOREACH(du, rrc_du_tree, &rrc->dus) {
    size_t n = seq_arr_size(&du->cells);
    for (size_t i = 0; i < n; i++) {
      nr_rrc_cell_container_t **cp = seq_arr_at(&du->cells, i);
      if (*cp == source_cell)
        source_idx = total;
      total++;
    }
  }

  if (total < 2) {
    LOG_E(NR_RRC, "UE %u: only one cell configured, cannot handover\n", ue->rrc_ue_id);
    return;
  }
  AssertFatal(source_idx >= 0, "source cell not found among connected DUs for UE %u\n", ue->rrc_ue_id);

  nr_rrc_cell_container_t *target_cell = rrc_find_cell_at_index(rrc, (source_idx + 1) % total);
  AssertFatal(target_cell != NULL, "target cell at index %d not found\n", (source_idx + 1) % total);

  LOG_I(NR_RRC,
        "UE %u: round-robin HO trigger → target PCI %d (source PCI %d)\n",
        ue->rrc_ue_id,
        target_cell->info.pci,
        source_cell->info.pci);

  nr_rrc_trigger_f1_ho(rrc, ue, source_cell, target_cell);
}

static byte_array_t rrc_gNB_encode_HandoverCommand(gNB_RRC_UE_t *UE, gNB_RRC_INST *rrc)
{
  DevAssert(UE->ho_context);
  DevAssert(UE->ho_context->target);

  NR_SecurityConfig_t *sec = calloc_or_fail(1, sizeof(*sec));
  sec->keyToUse = calloc_or_fail(1, sizeof(*sec->keyToUse));
  *sec->keyToUse = NR_SecurityConfig__keyToUse_master;
  sec->securityAlgorithmConfig = calloc_or_fail(1, sizeof(*sec->securityAlgorithmConfig));
  struct NR_SecurityAlgorithmConfig *alg = sec->securityAlgorithmConfig;
  alg->cipheringAlgorithm = UE->ciphering_algorithm;
  alg->integrityProtAlgorithm = calloc_or_fail(1, sizeof(*alg->integrityProtAlgorithm));
  *alg->integrityProtAlgorithm = UE->integrity_algorithm;

  // Mark source gNB's measurement configuration for removal in UE->measConfig
  if (UE->ho_context->target->ue_ho_prep_info.len) {
    fill_removal_lists_from_source_measConfig(UE->measConfig, UE->ho_context->target->ue_ho_prep_info);
  }

  /* 3GPP TS 38.331 RadioBearerConfig: re-establish PDCP whenever
  the security key used for this radio bearer changes, e.g. for SRB2
  when receiving reconfiguration with sync [...]. Similarly, for DRBs
  PDCP re-establishment is necessary with derivation of new UP keys */
  nr_rrc_reconfig_param_t params = get_RRCReconfiguration_params(rrc, UE, (1 << 1) | (1 << 2), true);
  UE->xids[params.transaction_id] = RRC_DEDICATED_RECONF;

  params.security_config = sec;
  params.masterKeyUpdate = true;
  params.nextHopChainingCount = UE->nh_ncc;
  byte_array_t out = get_HandoverCommandMessage(&params);

  // Free remove lists in UE->measConfig
  ASN_STRUCT_FREE(asn_DEF_NR_MeasIdToRemoveList, UE->measConfig->measIdToRemoveList);
  ASN_STRUCT_FREE(asn_DEF_NR_ReportConfigToRemoveList, UE->measConfig->reportConfigToRemoveList);
  ASN_STRUCT_FREE(asn_DEF_NR_MeasObjectToRemoveList, UE->measConfig->measObjectToRemoveList);

  free_RRCReconfiguration_params(params);
  return out;
}

/** @brief Update cell association after handover: set PCell on target DU
 * @param[in] rrc RRC instance
 * @param[in] UE UE context
 * @return true on success, false on failure
 *
 * Generic for all handover types (F1, N2, Xn). Behaviour depends on whether
 * ho_context->source is set on this CU:
 * - Source present (intra-CU): F1 HO, remove all serving cells belonging to
 *   the source DU, then add target cell as PCell.
 * - Source absent (inter-CU): N2 or Xn handover (target CU created new UE
 *   on Handover Request). This UE context has no serving cells yet; add
 *   target cell as first PCell. */
bool nr_rrc_update_cell_assoc_after_ho(gNB_RRC_UE_t *UE)
{
  DevAssert(UE->ho_context);
  DevAssert(UE->ho_context->target);
  nr_ho_target_cu_t *target_ctx = UE->ho_context->target;

  /* Pre-check: ensure F1 UE data matches target cell before mutating state. */
  if (!cu_exists_f1_ue_data(UE->rrc_ue_id)) {
    LOG_E(NR_RRC, "UE %d handover: no F1 UE data, cannot add PCell\n", UE->rrc_ue_id);
    return false;
  }
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  if (ue_data.du_assoc_id != target_ctx->cell->assoc_id) {
    LOG_E(NR_RRC,
          "UE %d handover: target cell assoc_id %d does not match F1 du_assoc_id %d\n",
          UE->rrc_ue_id,
          target_ctx->cell->assoc_id,
          ue_data.du_assoc_id);
    return false;
  }

  if (!UE->ho_context->source) {
    /* Inter-CU (N2/Xn target): this UE context has no serving cells yet */
    const ue_serving_cell_t *existing_pcell = ue_get_pcell_entry(UE);
    AssertFatal(existing_pcell == NULL, "UE context should not have PCell yet (inter-CU target)\n");
  }

  /* Update PCell: removes all serving cells from existing PCell's DU (if any)
   * and adds the target cell as the new PCell. For F1 HO (intra-CU), this
   * migrates serving cells from source DU to target DU. */
  ue_serving_cell_t *added = rrc_update_ue_pcell(UE, target_ctx->cell);
  if (added == NULL) {
    LOG_E(NR_RRC,
          "UE %d handover: failed to add PCell (cell %ld) to serving cells\n",
          UE->rrc_ue_id,
          target_ctx->cell->info.cell_id);
    return false;
  }

  return true;
}

/** @brief This callback is used by the target gNB
 *         to trigger the Handover Request Acknowledge towards the AMF */
static void nr_rrc_n2_ho_acknowledge(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  AssertFatal(cu_exists_f1_ue_data(UE->rrc_ue_id), "No CU found for rrc_ue_id %d\n", UE->rrc_ue_id);

  f1_ue_data_t previous_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  /* this is the callback for N2 handover after F1 UE context setup response in
   * the handover case. Check that there was no associated DU, then set it */
  AssertFatal(previous_data.secondary_ue == -1, "there was already a DU present\n");
  nr_rrc_apply_target_context(UE);

  /* Update cell association after handover */
  nr_ho_target_cu_t *target = UE->ho_context->target;
  if (!nr_rrc_update_cell_assoc_after_ho(UE)) {
    ngap_handover_failure_t fail = {.amf_ue_ngap_id = UE->amf_ue_ngap_id,
                                    .cause.type = NGAP_CAUSE_RADIO_NETWORK,
                                    .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM};
    target->ho_failure(rrc, UE->rrc_ue_id, &fail);
    return;
  }

  byte_array_t hoCommand = rrc_gNB_encode_HandoverCommand(UE, rrc);
  if (hoCommand.len < 0) {
    LOG_E(NR_RRC, "ASN1 message encoding failed: failed to generate Handover Command Message\n");
    ngap_handover_failure_t fail = {.amf_ue_ngap_id = UE->amf_ue_ngap_id,
                                    .cause.type = NGAP_CAUSE_RADIO_NETWORK,
                                    .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM};
    target->ho_failure(rrc, UE->rrc_ue_id, &fail);
    return;
  } else {
    LOG_D(NR_RRC, "HO LOG: Handover Command for UE %u Encoded (%ld bytes)\n", UE->rrc_ue_id, hoCommand.len);
  }

  rrc_gNB_send_NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(rrc, UE, hoCommand);
  free_byte_array(hoCommand);
}

/** @brief This callback is used by the target gNB
 *         to trigger the Handover Notify towards the AMF */
static void nr_rrc_n2_ho_complete(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  rrc_gNB_send_NGAP_HANDOVER_NOTIFY(rrc, UE);
}

/** @brief This callback is used by the source gNB to inform the AMF
 *         about the cancellation of an ongoing handover */
static void nr_rrc_n2_ho_cancel(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  DevAssert(UE->ho_context);
  DevAssert(UE->ho_context->source);
  ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_HANDOVER_CANCELLED};
  rrc_gNB_send_NGAP_HANDOVER_CANCEL(rrc->module_id, UE, cause);
}

/** @brief Callback function to trigger NG Handover Failure on the target gNB, to inform the AMF
 * that the preparation of resources has failed (e.g. unsatisfied criteria, gNB is already loaded).
 * This message represents an Unsuccessful Outcome of the Handover Resource Allocation */
void nr_rrc_n2_ho_failure(gNB_RRC_INST *rrc, uint32_t gnb_ue_id, ngap_handover_failure_t *msg)
{
  LOG_I(NR_RRC, "Triggering N2 Handover Failure\n");
  rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, msg);
  LOG_I(NR_RRC, "Send UE Context Release for gnb_ue_id %d\n", gnb_ue_id);
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, gnb_ue_id);
  rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(rrc->module_id, ue_context_p, msg->cause);
  return;
}

/** @brief Trigger N2 Handover on the target gNB:
 *         1) set up callbacks
 *         2) initiate N2 handover on target NG-RAN, which triggers the set up
 *            of HO context and UE Context Setup procedures */
void nr_rrc_trigger_n2_ho_target(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue)
{
  ho_req_ack_t ack = nr_rrc_n2_ho_acknowledge;
  ho_success_t success = nr_rrc_n2_ho_complete;
  ho_failure_t failure = nr_rrc_n2_ho_failure;

  nr_initiate_handover(rrc, ue, NULL, &ue->ho_context->target->ue_ho_prep_info, ack, success, NULL, failure);
  FREE_AND_ZERO_BYTE_ARRAY(ue->ho_context->target->ue_ho_prep_info);

  NR_UE_NR_Capability_t *ue_cap = get_ue_nr_capability(ue->rnti, ue->ue_cap_buffer.buf, ue->ue_cap_buffer.len);
  ue->UE_Capability_nr = ue_cap;
  ue->UE_Capability_size = ue->ue_cap_buffer.len;

}

/** @brief Trigger N2 handover on source gNB:
 *         1) Prepare RRC Container with HandoverPreparationInformation message
 *         2) send NGAP Handover Required message */
void nr_rrc_trigger_n2_ho(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, const nr_neighbour_cell_t *neighbour_config)
{
  bool has_active_pdu_session = false;
  FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, pduSession, &ue->pduSessions) {
    if (pduSession->status == PDU_SESSION_STATUS_ESTABLISHED) {
      has_active_pdu_session = true;
      break;
    }
  }
  /* If there are no active PDU sessions, we can't send the Handover Preparation information
   * message to the source gNB (3GPP TS 38.413 §9.2.3.1): Handover Required must carry a PDU
   * Session Resource List with at least one PDU Session Resource Item */
  if (!has_active_pdu_session) {
    LOG_W(NR_RRC, "N2 HO not triggered for UE %u: no active PDU sessions\n", ue->rrc_ue_id);
    return;
  }

  byte_array_t hoPrepInfo = rrc_gNB_generate_HandoverPreparationInformation(rrc, ue);
  if (hoPrepInfo.len < 0) {
    free_byte_array(hoPrepInfo);
    LOG_E(NR_RRC, "Failed to trigger N2 handover on source gNB for UE %x\n", ue->rrc_ue_id);
    return;
  }

  // allocate context for source
  if (ue->ho_context != NULL) {
    LOG_E(NR_RRC, "Ongoing handover for UE %d, cannot trigger new\n", ue->rrc_ue_id);
    return;
  }

  ue->ho_context = alloc_ho_ctx(HO_CTX_SOURCE);
  nr_rrc_cell_container_t *source_cell = rrc_get_pcell_for_ue(rrc, ue);
  if (source_cell == NULL) {
    LOG_E(NR_RRC, "No source cell found for UE %d\n", ue->rrc_ue_id);
    free_byte_array(hoPrepInfo);
    return;
  }
  ue->ho_context->source->cell = source_cell;
  ue->ho_context->source->target_nci = neighbour_config->nrcell_id;
  ue->ho_context->source->ho_status_transfer = rrc_gNB_send_NGAP_ul_ran_status_transfer;
  ue->ho_context->source->ho_cancel = nr_rrc_n2_ho_cancel;

  rrc_gNB_send_NGAP_HANDOVER_REQUIRED(rrc, ue, neighbour_config, hoPrepInfo);
  free_byte_array(hoPrepInfo);
}

void nr_HO_N2_trigger_telnet(gNB_RRC_INST *rrc, uint32_t neighbour_pci, uint32_t rrc_ue_id)
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, rrc_ue_id);
  if (ue_context_p == NULL) {
    LOG_E(NR_RRC, "N2 HO trigger failed for UE %d: cannot find UE context\n", rrc_ue_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  nr_rrc_du_container_t *du = get_du_for_ue(rrc, UE->rrc_ue_id);
  if (du == NULL) {
    LOG_E(NR_RRC, "N2 HO trigger failed for UE %d: Unknown DU\n", rrc_ue_id);
    return;
  }
  nr_rrc_cell_container_t *pcell = rrc_get_pcell_for_ue(rrc, UE);
  if (pcell == NULL) {
    LOG_E(NR_RRC, "N2 HO trigger failed for UE %d: Unknown cell\n", rrc_ue_id);
    return;
  }
  uint16_t scell_pci = pcell->info.pci;

  // Simulate handover on the same cell (testing purposes)
  if (neighbour_pci == scell_pci) {
    LOG_I(NR_RRC, "UE %d: trigger handover on the same cell PCI=%d\n", rrc_ue_id, neighbour_pci);
    nr_neighbour_cell_t neighbourConfig = {
        .gNB_ID = du->gNB_DU_id,
        .nrcell_id = pcell->info.cell_id,
        .physicalCellId = pcell->info.pci,
        .plmn = pcell->info.plmn,
        .subcarrierSpacing = pcell->info.mode == NR_MODE_TDD ? pcell->info.tdd.dlul.scs : pcell->info.fdd.dl.scs,
    };
    nr_rrc_trigger_n2_ho(rrc, UE, &neighbourConfig);
    return;
  }

  LOG_I(NR_RRC, "UE %d: triggered N2 HO, source PCI=%d to target PCI=%d\n", rrc_ue_id, scell_pci, neighbour_pci);

  const neighbour_cell_configuration_t *cell = get_neighbour_cell_config(rrc, pcell->info.cell_id);
  if (cell == NULL) {
    LOG_E(NR_RRC,
          "N2 HO trigger failed for UE %d: could not find neighbour cell with Cell ID=%ld\n",
          rrc_ue_id,
          pcell->info.cell_id);
    return;
  }

  const nr_neighbour_cell_t *neighbour = get_neighbour_cell_by_pci(cell, neighbour_pci);
  if (neighbour == NULL) {
    LOG_E(NR_RRC, "N2 HO trigger failed for UE %d: could not find neighbour cell with PCI=%d\n", rrc_ue_id, neighbour_pci);
    return;
  }

  nr_rrc_trigger_n2_ho(rrc, UE, neighbour);
}

// This function detects if there are at least two different ssbFrequency values, and if so, returns meas_timing_config;
// otherwise, it returns NULL. When you return NULL, the measurement gaps will not be configured.
byte_array_t *get_meas_timing_config(const NR_MeasurementTimingConfiguration_t *mtc, const NR_MeasConfig_t *measConfig)
{
  if (!mtc || !measConfig || !measConfig->measObjectToAddModList)
    return NULL;

  byte_array_t *meas_timing_config = NULL;
  NR_MeasObjectToAddModList_t *mo_list = measConfig->measObjectToAddModList;
  NR_ARFCN_ValueNR_t ssbFrequency0 = 0;
  for (int i = 0; i < mo_list->list.count; i++) {
    NR_MeasObjectToAddMod_t *mo = mo_list->list.array[i];
    if (mo->measObject.present != NR_MeasObjectToAddMod__measObject_PR_measObjectNR)
      continue;
    NR_MeasObjectNR_t *monr = mo->measObject.choice.measObjectNR;
    if (i == 0) {
      ssbFrequency0 = *monr->ssbFrequency;
    } else if (ssbFrequency0 != *monr->ssbFrequency) {
      meas_timing_config = calloc_or_fail(1, sizeof(*meas_timing_config));
      meas_timing_config->buf = calloc_or_fail(1, NR_RRC_BUF_SIZE);
      meas_timing_config->len = do_NR_MeasurementTimingConfiguration(mtc, meas_timing_config->buf, NR_RRC_BUF_SIZE);
      break;
    }
  }

  return meas_timing_config;
}
