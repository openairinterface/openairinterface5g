/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* RRC part of the Xn handover: messages to and from XNAP, at the source and the target */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rrc_gNB_XNAP.h"
#include "rrc_gNB_mobility.h"
#include "rrc_cell_management.h"
#include "rrc_gNB_UE_context.h"
#include "rrc_gNB_NGAP.h"
#include "rrc_gNB_security.h"
#include "rrc_gNB_radio_bearers.h"
#include "nr_rrc_proto.h"
#include "openair2/COMMON/xnap_messages_types.h"
#include "openair3/NGAP/ngap_common.h"
#include "openair3/SECU/key_nas_deriver.h"
#include "NGAP_LastVisitedNGRANCellInformation.h"
#include "NGAP_CellSize.h"
#include "common/utils/LOG/log.h"
#include "common/utils/ds/byte_array.h"
#include "assertions.h"
#include "intertask_interface.h"
#include "aper_encoder.h"
#include "openair2/XNAP/xnap_ids.h"

/** @brief APER-encode the serving cell as an NGAP LastVisitedNGRANCellInformation OCTET STRING.
 *  As per 3GPP TS 38.423 §9.2.3.65, the XnAP UE history entry for NR is the NGAP encoding of
 *  NGAP LastVisitedNGRANCellInformation carried inside an OCTET STRING. */
static byte_array_t build_last_visited_nr_cell_info(const plmn_id_t *plmn, uint64_t cell_id, time_t last_seen)
{
  NGAP_LastVisitedNGRANCellInformation_t info = {0};

  /* Global Cell ID */
  info.globalCellID.present = NGAP_NGRAN_CGI_PR_nR_CGI;
  asn1cCalloc(info.globalCellID.choice.nR_CGI, nrcgi);
  encode_ngap_nr_cgi(nrcgi, plmn, (uint32_t)cell_id);

  /* Cell type */
  info.cellType.cellSize = NGAP_CellSize_small;

  /* Time UE stayed in cell: seconds since last_seen, clamped to 4095 (field max) */
  info.timeUEStayedInCell = min(time(NULL) - last_seen, 4095);

  byte_array_t ba = {0};
  ssize_t len = aper_encode_to_new_buffer(&asn_DEF_NGAP_LastVisitedNGRANCellInformation, NULL, &info, (void **)&ba.buf);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_LastVisitedNGRANCellInformation, &info);
  if (len < 0) {
    LOG_E(NR_RRC, "Failed to encode LastVisitedNGRANCellInformation\n");
    return (byte_array_t){0};
  }
  ba.len = len;
  return ba;
}

bool rrc_gNB_send_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc,
                                        gNB_RRC_UE_t *UE,
                                        const nr_neighbour_cell_t *neighbour,
                                        const byte_array_t hoPrepInfo)
{
  const rrc_xn_candidate_t *xn_cand = rrc_find_xn_candidate(rrc, neighbour->gNB_ID);
  if (!xn_cand) {
    LOG_E(NR_RRC, "UE %d: no Xn connection to gNB_ID 0x%x\n", UE->rrc_ue_id, neighbour->gNB_ID);
    return false;
  }

  /* the UE History Information starts with the serving cell (mandatory, at least one cell) */
  const nr_rrc_cell_container_t *source_cell = rrc_get_pcell_for_ue(rrc, UE);
  if (!source_cell) {
    LOG_E(NR_RRC, "UE %d: serving cell not found, cannot trigger Xn HO\n", UE->rrc_ue_id);
    return false;
  }

  /* Build target CGI */
  xnap_ngran_cgi_t target_cgi = {
    .plmn_id = neighbour->plmn,
    .nrcell_id = neighbour->nrcell_id,
  };

  /* Security capabilities */
  xnap_security_capabilities_t sec_cap = {
    .nRencryption_algorithms = UE->security_capabilities.nRencryption_algorithms,
    .nRintegrity_algorithms = UE->security_capabilities.nRintegrity_algorithms,
    .eUTRAencryption_algorithms = UE->security_capabilities.eUTRAencryption_algorithms,
    .eUTRAintegrity_algorithms = UE->security_capabilities.eUTRAintegrity_algorithms,
  };

  /* Count established PDU sessions: XnAP requires at least one, each with at least one QoS flow */
  int num_pdu = 0;
  FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, p, &UE->pduSessions) {
    if (p->status != PDU_SESSION_STATUS_ESTABLISHED)
      continue;
    if (seq_arr_size(&p->param.qos) == 0) {
      LOG_E(NR_RRC, "UE %d: PDU session %d has no QoS flow, cannot trigger Xn HO\n", UE->rrc_ue_id, p->param.pdusession_id);
      return false;
    }
    num_pdu++;
  }
  if (num_pdu == 0) {
    LOG_E(NR_RRC, "UE %d: no established PDU session, cannot trigger Xn HO\n", UE->rrc_ue_id);
    return false;
  }

  /* Build PDU session resource list */
  xnap_pdusession_resources_tobe_setup_item_t *pdu_list = calloc_or_fail(num_pdu, sizeof(*pdu_list));

  int idx = 0;
  FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, p, &UE->pduSessions) {
    if (p->status != PDU_SESSION_STATUS_ESTABLISHED)
      continue;
    const pdusession_t *ps = &p->param;
    xnap_pdusession_resources_tobe_setup_item_t *item = &pdu_list[idx++];

    item->pdusession_id = ps->pdusession_id;
    item->pdu_session_type = ps->pdu_session_type;
    item->n3_incoming = ps->n3_incoming;

    nssai_t *nssai_copy = malloc_or_fail(sizeof(*nssai_copy));
    *nssai_copy = ps->nssai;
    item->nssai = nssai_copy;

    int num_qos = seq_arr_size(&ps->qos);
    item->num_qos = num_qos;
    if (num_qos > 0) {
      item->qos_list = calloc_or_fail(num_qos, sizeof(*item->qos_list));
    }

    xnap_qos_flow_tobe_setup_item_t *qi = item->qos_list;
    FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, qitem, &ps->qos) {
      const pdusession_level_qos_parameter_t *qp = &qitem->qos;
      qi->qfi = qp->qfi;
      qi->qos_params.qos_type = qp->fiveQI_type;
      if (qp->fiveQI_type == NON_DYNAMIC) {
        qi->qos_params.nondyn.fiveQI = qp->qos_characteristics.non_dynamic.fiveQI;
      } else {
        qi->qos_params.dyn.prio = qp->qos_characteristics.dynamic.qos_priority;
        qi->qos_params.dyn.pdb = qp->qos_characteristics.dynamic.packet_delay_budget;
        qi->qos_params.dyn.per.scalar = qp->qos_characteristics.dynamic.per.scalar;
        qi->qos_params.dyn.per.exponent = qp->qos_characteristics.dynamic.per.exponent;
      }
      qi->qos_params.arp = qp->arp;
      qi++;
    }
  }

  /* Derive NG-RAN* key: use NH when NCC > 0, otherwise fall back to KgNB */
  uint8_t as_key_ranstar[32];
  nr_derive_key_ng_ran_star(neighbour->physicalCellId,
                            neighbour->absoluteFrequencySSB,
                            UE->nh_ncc > 0 ? UE->nh : UE->kgnb,
                            as_key_ranstar);

  /* UE context information */
  xnap_ue_context_info_t ue_context = {
    .ngc_ue_sig_ref = UE->amf_ue_ngap_id,
    .cp_tnl_ip_source = UE->amf_ng_ip,
    .security_capabilities = sec_cap,
    .as_security_ncc = UE->nh_ncc,
    .ue_ambr = {.br_ul = UE->ambr.ul_br, .br_dl = UE->ambr.dl_br},
    .rrc_context = copy_byte_array(hoPrepInfo),
    .num_pdu = num_pdu,
    .pdusession_resources_tobe_setup_list = pdu_list,
  };
  memcpy(ue_context.as_security_key_ranstar, as_key_ranstar, 32);

  /* UE history: the source (serving) cell */
  ue_history_info_t *history = calloc_or_fail(1, sizeof(*history));
  history->xnap_cell_type = XNAP_LAST_VISITED_CELL_NR;
  history->last_visited_cell_info = build_last_visited_nr_cell_info(&source_cell->info.plmn,
                                                                    source_cell->info.cell_id,
                                                                    UE->last_seen);

  /* Build and send the ITTI message */
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, XNAP_HANDOVER_REQ);
  xnap_handover_req_t *req = &XNAP_HANDOVER_REQ(msg_p);
  *req = (xnap_handover_req_t){
    .cause = {
      .type = XNAP_CAUSE_RADIO_NETWORK,
      .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_HANDOVER_DESIRABLE_FOR_RADIO_REASONS,
    },
    .target_cgi = target_cgi,
    .guami = UE->ue_guami,
    .ue_context = ue_context,
    .num_last_visited_cells = 1,
    .ue_history_info = history,
    .rrc_ue_id = UE->rrc_ue_id,
    .target_assoc_id = xn_cand->assoc_id,
  };

  LOG_I(NR_RRC,
        "Xn HO source: UE %d: sending Handover Request to gNB_ID 0x%x (assoc_id %d)\n",
        UE->rrc_ue_id,
        neighbour->gNB_ID,
        xn_cand->assoc_id);
  LOG_D(NR_RRC,
        "Xn HO source: UE %d: Handover Request to NR Cell ID 0x%lx PCI %d: AMF UE NGAP ID %lu, NCC %d, %d PDU session(s), "
        "HandoverPreparationInformation %zu bytes\n",
        UE->rrc_ue_id,
        target_cgi.nrcell_id,
        neighbour->physicalCellId,
        UE->amf_ue_ngap_id,
        UE->nh_ncc,
        num_pdu,
        hoPrepInfo.len);
  for (int i = 0; i < num_pdu; i++)
    LOG_D(NR_RRC,
          "Xn HO source: UE %d:   PDU session %d, %d QoS flow(s)\n",
          UE->rrc_ue_id,
          pdu_list[i].pdusession_id,
          pdu_list[i].num_qos);
  itti_send_msg_to_task(TASK_XNAP, rrc->module_id, msg_p);
  return true;
}

/* Select security algorithms from Xn capabilities and configure the UE context.
 * xnap_security_capabilities_t has identical bitmask fields to ngap_security_capabilities_t. */
static void set_UE_security_algos_xn(const gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const xnap_security_capabilities_t *cap)
{
  UE->security_capabilities.nRencryption_algorithms = cap->nRencryption_algorithms;
  UE->security_capabilities.nRintegrity_algorithms = cap->nRintegrity_algorithms;
  UE->security_capabilities.eUTRAencryption_algorithms = cap->eUTRAencryption_algorithms;
  UE->security_capabilities.eUTRAintegrity_algorithms = cap->eUTRAintegrity_algorithms;

  UE->ciphering_algorithm = rrc_gNB_select_ciphering(rrc, cap->nRencryption_algorithms);
  UE->integrity_algorithm = rrc_gNB_select_integrity(rrc, cap->nRintegrity_algorithms);

  LOG_UE_EVENT(UE, "Xn HO: selected ciphering %lx integrity %x\n", UE->ciphering_algorithm, UE->integrity_algorithm);
}

int rrc_gNB_process_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc, xnap_handover_req_t *req)
{
  nr_rrc_cell_container_t *cell = get_cell_by_cell_id(&rrc->cells, req->target_cgi.nrcell_id);
  if (!cell) {
    LOG_E(NR_RRC, "Xn HandoverRequest: no cell with NR Cell ID 0x%lx\n", req->target_cgi.nrcell_id);
    xnap_cause_t cause = {.type = XNAP_CAUSE_RADIO_NETWORK,
                          .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_CELL_NOT_AVAILABLE};
    rrc_gNB_send_XNAP_HANDOVER_PREP_FAILURE(rrc, 0, req->t_ng_node_ue_xnap_id, cause);
    return -1;
  }

  nr_rrc_du_container_t *du = get_du_by_assoc_id(rrc, cell->assoc_id);
  if (!du) {
    LOG_E(NR_RRC, "Xn HandoverRequest: no DU for assoc_id %d\n", cell->assoc_id);
    xnap_cause_t cause = {.type = XNAP_CAUSE_RADIO_NETWORK, .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_CELL_NOT_AVAILABLE};
    rrc_gNB_send_XNAP_HANDOVER_PREP_FAILURE(rrc, 0, req->t_ng_node_ue_xnap_id, cause);
    return -1;
  }

  if (!is_cuup_associated(rrc)) {
    LOG_E(NR_RRC, "Xn HandoverRequest: no CU-UP associated — rejecting\n");
    xnap_cause_t cause = {.type = XNAP_CAUSE_MISC,
                          .value = XNAP_CAUSE_MISC_NOT_ENOUGH_USER_PLANE_PROCESSING_RESOURCES};
    rrc_gNB_send_XNAP_HANDOVER_PREP_FAILURE(rrc, 0, req->t_ng_node_ue_xnap_id, cause);
    return -1;
  }

  rrc_gNB_ue_context_t *ue_ctx = rrc_gNB_create_ue_context(du->assoc_id, UINT16_MAX, rrc, UINT64_MAX, UINT32_MAX);
  gNB_RRC_UE_t *UE = &ue_ctx->ue_context;

  /* map the XnAP UE IDs of this handover to the new UE in XNAP */
  if (!xn_bind_ue(req->t_ng_node_ue_xnap_id, UE->rrc_ue_id)) {
    /* XNAP dropped the handover meanwhile, e.g. the source peer is lost */
    LOG_W(NR_RRC, "Xn HO target: UE %d: t_xnap_ue_id %u is gone, dropping the UE\n", UE->rrc_ue_id, req->t_ng_node_ue_xnap_id);
    rrc_remove_ue(rrc, ue_ctx);
    return -1;
  }
  LOG_I(NR_RRC,
        "Xn HO target: UE %d: Handover Request received (s_xnap_ue_id %u t_xnap_ue_id %u), UE created\n",
        UE->rrc_ue_id,
        req->s_ng_node_ue_xnap_id,
        req->t_ng_node_ue_xnap_id);

  UE->ho_context = alloc_ho_ctx(HO_CTX_TARGET);
  nr_ho_target_cu_t *target = UE->ho_context->target;
  target->cell = cell;
  target->ho_trigger = nr_rrc_trigger_xn_ho_target;
  target->ue_ho_prep_info = copy_byte_array(req->ue_context.rrc_context);

  /* Carry UE capabilities forward from the source: unlike N2 HO (where the AMF sends
   * them as a separate NGAP IE), Xn HO only carries them embedded in rrc_context. Without
   * this, ue_cap_buffer stays empty and a later handover of this same UE (now as source)
   * fails to re-encode its own HandoverPreparationInformation. */
  UE->ue_cap_buffer = extract_ue_cap_from_HandoverPreparationInformation(req->ue_context.rrc_context);

  UE->amf_ue_ngap_id = req->ue_context.ngc_ue_sig_ref;
  UE->amf_ng_ip = req->ue_context.cp_tnl_ip_source;
  UE->ue_guami = req->guami;
  UE->serving_plmn = cell->info.plmn;

  /* Xn HO: source delivers KgNB* (as_security_key_ranstar) directly —
   * copy to kgnb; no derivation step unlike N2 HO which starts from NH. */
  set_UE_security_algos_xn(rrc, UE, &req->ue_context.security_capabilities);
  UE->nh_ncc = req->ue_context.as_security_ncc;
  memcpy(UE->kgnb, req->ue_context.as_security_key_ranstar, SECURITY_KEY_LENGTH);
  memset(UE->nh, 0, SECURITY_KEY_LENGTH);
  UE->as_security_active = true;

  activate_srb(UE, SRB1);
  activate_srb(UE, SRB2);
  nr_rrc_pdcp_config_security(UE, true);

  UE->ambr.dl_br = req->ue_context.ue_ambr.br_dl;
  UE->ambr.ul_br = req->ue_context.ue_ambr.br_ul;
  LOG_D(NR_RRC,
        "Xn HO target: UE %d: Handover Request: AMF UE NGAP ID %lu, NCC %d, ciphering %ld integrity %d, UE-AMBR DL %lu UL %lu, "
        "%d PDU session(s)\n",
        UE->rrc_ue_id,
        UE->amf_ue_ngap_id,
        UE->nh_ncc,
        UE->ciphering_algorithm,
        UE->integrity_algorithm,
        UE->ambr.dl_br,
        UE->ambr.ul_br,
        req->ue_context.num_pdu);

  /* Convert Xn PDU session resources to pdusession_t for bearer setup */
  int n_pdu = req->ue_context.num_pdu;
  pdusession_t *sessions = calloc_or_fail(n_pdu, sizeof(*sessions));

  for (int i = 0; i < n_pdu; i++) {
    xnap_pdusession_resources_tobe_setup_item_t *xpdu = &req->ue_context.pdusession_resources_tobe_setup_list[i];
    pdusession_t *pdu = &sessions[i];

    pdu->pdusession_id = xpdu->pdusession_id;
    pdu->pdu_session_type = xpdu->pdu_session_type;
    pdu->n3_incoming = xpdu->n3_incoming;
    if (xpdu->nssai)
      pdu->nssai = *xpdu->nssai;

    seq_arr_init(&pdu->qos, sizeof(nr_rrc_qos_t));
    for (int j = 0; j < xpdu->num_qos; j++) {
      xnap_qos_flow_tobe_setup_item_t *xqos = &xpdu->qos_list[j];
      pdusession_level_qos_parameter_t qp = {
        .qfi = xqos->qfi,
        .fiveQI_type = xqos->qos_params.qos_type,
        .arp = xqos->qos_params.arp,
      };
      if (xqos->qos_params.qos_type == NON_DYNAMIC) {
        qp.qos_characteristics.non_dynamic.fiveQI = xqos->qos_params.nondyn.fiveQI;
      } else {
        qp.qos_characteristics.dynamic.qos_priority = xqos->qos_params.dyn.prio;
        qp.qos_characteristics.dynamic.packet_delay_budget = xqos->qos_params.dyn.pdb;
        qp.qos_characteristics.dynamic.per.scalar = xqos->qos_params.dyn.per.scalar;
        qp.qos_characteristics.dynamic.per.exponent = xqos->qos_params.dyn.per.exponent;
      }
      add_qos(&pdu->qos, &qp);
    }
    LOG_D(NR_RRC, "Xn HO target: UE %d:   PDU session %d, %d QoS flow(s)\n", UE->rrc_ue_id, pdu->pdusession_id, xpdu->num_qos);
  }

  nr_rrc_add_bearers(rrc, UE, n_pdu, sessions);
  free(sessions);
  trigger_bearer_setup(rrc, UE, UE->ambr.dl_br);
  return 0;
}

void rrc_gNB_send_XNAP_HANDOVER_REQ_ACK(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, byte_array_t ho_command)
{
  int num_admitted = seq_arr_size(&UE->pduSessions);
  xnap_pdusession_admitted_item_t *admitted = NULL;
  if (num_admitted > 0) {
    admitted = calloc_or_fail(num_admitted, sizeof(*admitted));
    int idx = 0;
    FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, p, &UE->pduSessions) {
      p->status = PDU_SESSION_STATUS_ESTABLISHED;
      admitted[idx].pdusession_id = p->param.pdusession_id;
      int num_qos = seq_arr_size(&p->param.qos);
      admitted[idx].num_qos = num_qos;
      if (num_qos > 0) {
        admitted[idx].qos_list = calloc_or_fail(num_qos, sizeof(*admitted[idx].qos_list));
        int j = 0;
        FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, qos, &p->param.qos)
          admitted[idx].qos_list[j++].qfi = qos->qos.qfi;
      }
      idx++;
    }
  }

  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, XNAP_HANDOVER_REQ_ACK);
  xnap_handover_req_ack_t *ack = &XNAP_HANDOVER_REQ_ACK(msg);
  *ack = (xnap_handover_req_ack_t){
    .num_pdu_admitted = num_admitted,
    .pdusession_admitted_list = admitted,
    .target2source = copy_byte_array(ho_command),
    .rrc_ue_id = UE->rrc_ue_id,
  };

  LOG_I(NR_RRC, "Xn HO target: UE %d: sending Handover Request Acknowledge\n", UE->rrc_ue_id);
  LOG_D(NR_RRC,
        "Xn HO target: UE %d: Handover Request Acknowledge: %d PDU session(s) admitted, HandoverCommand %zu bytes\n",
        UE->rrc_ue_id,
        num_admitted,
        ho_command.len);

  itti_send_msg_to_task(TASK_XNAP, rrc->module_id, msg);
}

/* @brief Source gNB processes Handover Request Acknowledge from the target gNB:
 *  sends the HandoverCommand to the UE. */
void rrc_gNB_process_XNAP_HANDOVER_REQ_ACK(gNB_RRC_INST *rrc, const xnap_handover_req_ack_t *msg)
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, msg->rrc_ue_id);
  if (!ue_context_p) {
    LOG_W(NR_RRC, "Xn HandoverRequestAck: unknown rrc_ue_id %u\n", msg->rrc_ue_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  if (!UE->ho_context || !UE->ho_context->source) {
    LOG_W(NR_RRC, "UE %u: Xn HandoverRequestAck but no source handover context — dropping\n", UE->rrc_ue_id);
    return;
  }

  byte_array_t buffer = doRRCReconfiguration_from_HandoverCommand(msg->target2source);
  if (!buffer.buf || buffer.len == 0) {
    LOG_E(NR_RRC, "UE %d: failed to decode HandoverCommand from Xn HO Request Ack\n", UE->rrc_ue_id);
    free_byte_array(buffer);
    nr_rrc_finalize_ho(UE);
    return;
  }

  rrc_gNB_trigger_reconfiguration_for_handover(rrc, UE, buffer.buf, buffer.len);
  LOG_A(NR_RRC,
        "Xn HO source: UE %u/RNTI %04x: Handover Request Acknowledge (s_xnap_ue_id %u t_xnap_ue_id %u), "
        "sent RRCReconfiguration (HO Command)\n",
        UE->rrc_ue_id,
        UE->rnti,
        msg->s_ng_node_ue_xnap_id,
        msg->t_ng_node_ue_xnap_id);
  free_byte_array(buffer);
}

/** @brief Send Handover Preparation Failure (TS 38.423 §9.1.1.3) from target to source via Xn.
 *  The handover is found by rrc_ue_id, or by t_ng_node_ue_xnap_id if no UE was created (rrc_ue_id 0).
 *  Its XnAP UE IDs are removed here, as RRC may remove the UE before XNAP sends the message */
void rrc_gNB_send_XNAP_HANDOVER_PREP_FAILURE(gNB_RRC_INST *rrc,
                                             uint32_t rrc_ue_id,
                                             uint32_t t_ng_node_ue_xnap_id,
                                             xnap_cause_t cause)
{
  xn_ue_data_t xn_ue;
  bool found = rrc_ue_id != 0 ? xn_get_ue(XN_ROLE_TARGET, rrc_ue_id, &xn_ue)
                              : xn_get_ue_by_xnap_id(XN_ROLE_TARGET, t_ng_node_ue_xnap_id, &xn_ue);
  /* the remove fails if XNAP removed it meanwhile (Handover Cancel, peer lost) */
  if (!found || !xn_remove_ue_by_xnap_id(xn_ue.t_ng_node_ue_xnap_id)) {
    LOG_W(NR_RRC, "UE %u: no Xn handover (t_xnap_ue_id %u), Handover Preparation Failure not sent\n",
          rrc_ue_id, t_ng_node_ue_xnap_id);
    return;
  }
  LOG_I(NR_RRC,
        "Xn HO target: UE %u: sending Handover Preparation Failure (s_xnap_ue_id %u t_xnap_ue_id %u, cause %d/%d)\n",
        rrc_ue_id,
        xn_ue.s_ng_node_ue_xnap_id,
        xn_ue.t_ng_node_ue_xnap_id,
        cause.type,
        cause.value);

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, XNAP_HANDOVER_PREP_FAILURE);
  XNAP_HANDOVER_PREP_FAILURE(msg_p) = (xnap_handover_preparation_failure_t){
    .s_ng_node_ue_xnap_id = xn_ue.s_ng_node_ue_xnap_id,
    .cause = cause,
    .rrc_ue_id = rrc_ue_id,
    .assoc_id = xn_ue.assoc_id,
  };
  itti_send_msg_to_task(TASK_XNAP, rrc->module_id, msg_p);
}

/* @brief Source gNB processes Handover Preparation Failure from the target gNB:
 * the HO preparation is over, the UE stays on the source cell. */
int rrc_gNB_process_XNAP_HANDOVER_PREP_FAILURE(gNB_RRC_INST *rrc, const xnap_handover_preparation_failure_t *msg)
{
  rrc_gNB_ue_context_t *ue_ctx = rrc_gNB_get_ue_context(rrc, msg->rrc_ue_id);
  if (ue_ctx == NULL) {
    LOG_W(NR_RRC, "Xn Handover Preparation Failure: unknown rrc_ue_id %u\n", msg->rrc_ue_id);
    return -1;
  }
  gNB_RRC_UE_t *UE = &ue_ctx->ue_context;

  if (!UE->ho_context || !UE->ho_context->source) {
    LOG_W(NR_RRC, "UE %u: Xn Handover Preparation Failure but no source handover context — dropping\n",
          UE->rrc_ue_id);
    return -1;
  }

  LOG_E(NR_RRC,
        "Xn HO source: UE %u: Handover Preparation Failure from target (s_xnap_ue_id %u, cause %d/%d), "
        "handover aborted, UE stays on source\n",
        UE->rrc_ue_id,
        msg->s_ng_node_ue_xnap_id,
        msg->cause.type,
        msg->cause.value);

  nr_rrc_finalize_ho(UE);
  return 0;
}

/** @brief Abort an ongoing Xn handover at the target gNB: release the resources prepared
 * for the incoming UE (CU-UP bearers, target DU context) and remove the UE context. */
void rrc_gNB_xn_ho_target_abort(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const char *why)
{
  AssertFatal(UE->ho_context != NULL && UE->ho_context->target != NULL,
              "UE %u: Xn HO target abort without target handover context\n", UE->rrc_ue_id);

  LOG_E(NR_RRC, "Xn HO target: UE %u: aborting the handover: %s\n", UE->rrc_ue_id, why);

  if (ue_associated_to_cuup(UE)) {
    sctp_assoc_t assoc_id = get_existing_cuup_for_ue(UE);
    e1ap_cause_t cause = {.type = E1AP_CAUSE_RADIO_NETWORK, .value = E1AP_RADIO_CAUSE_NORMAL_RELEASE};
    e1ap_bearer_release_cmd_t cmd = {
      .gNB_cu_cp_ue_id = UE->rrc_ue_id,
      .gNB_cu_up_ue_id = UE->rrc_ue_id,
      .cause = cause,
    };
    rrc->cucp_cuup.bearer_context_release(assoc_id, &cmd);
  }

  nr_ho_target_cu_t *target = UE->ho_context->target;
  if (target->du_ue_id != 0 && target->cell != NULL) {
    f1ap_ue_context_rel_cmd_t cmd = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = target->du_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK,
      .cause_value = 5, // 5 = F1AP_CauseRadioNetwork_interaction_with_other_procedure
    };
    rrc->mac_rrc.ue_context_release_command(target->cell->assoc_id, &cmd);
  }

  rrc_gNB_ue_context_t *ue_ctx = rrc_gNB_get_ue_context(rrc, UE->rrc_ue_id);
  nr_rrc_finalize_ho(UE);
  rrc_remove_ue(rrc, ue_ctx);
}
