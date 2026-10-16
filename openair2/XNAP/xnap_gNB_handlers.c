/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* Received XnAP PDUs: decoding, per-procedure handlers, and cleanup of a lost Xn peer */

#include "xnap_gNB_handlers.h"
#include "xnap_gNB.h"
#include "xnap_common.h"
#include "common/utils/LOG/log.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "assertions.h"
#include "xnap_gNB_encoder.h"
#include "lib/xnap_gNB_interface_management.h"
#include "lib/xnap_gNB_mobility_management.h"
#include "aper_decoder.h"
#include "xnap_gNB_itti_messaging.h"
#include "xnap_ids.h"

/* Notify RRC of a peer's Xn Setup completion or SCTP-level shutdown. */
void xnap_handle_xn_setup_message(instance_t instance, xnap_peer_t *peer, int sctp_shutdown)
{
  if (sctp_shutdown) {
    if (peer->state == XNAP_PEER_STATE_CONNECTED || peer->state == XNAP_PEER_STATE_WAITING) {
      LOG_W(XNAP, "[gNB %ld] Xn peer assoc_id %d (gNB_id 0x%x) disconnected\n", instance, peer->assoc_id, peer->remote_gnb_id);

      /* Notify RRC only if XnSetup had completed because RRC has an entry,
       * WAITING peers never reached RRC so there is nothing to remove. */
      if (peer->state == XNAP_PEER_STATE_CONNECTED && peer->remote_gnb_id != 0) {
        MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_PEER_SHUTDOWN_IND);
        XNAP_PEER_SHUTDOWN_IND(msg).gnb_id = peer->remote_gnb_id;
        itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);
      }

      const xnap_cause_t cause = {.type = XNAP_CAUSE_TRANSPORT, .value = XNAP_CAUSE_TRANSPORT_LAYER_TRANSPORT_RESOURCE_UNAVAILABLE};
      xn_ue_data_t ues[64];
      int n;
      while ((n = xn_remove_ues_by_assoc(peer->assoc_id, ues, sizeofArray(ues))) > 0) {
        for (int i = 0; i < n; i++) {
          const xn_ue_data_t *ue = &ues[i];
          if (ue->role == XN_ROLE_SOURCE) {
            /* fail the handover prepared towards this peer: the source RRC finalizes it
             * as for a Handover Preparation Failure, so the UE can be handed over again */
            LOG_W(XNAP,
                  "[gNB %ld] Xn peer lost: failing handover of rrc_ue_id %u (s_xnap_ue_id %u)\n",
                  instance,
                  ue->rrc_ue_id,
                  ue->s_ng_node_ue_xnap_id);
            MessageDef *fail = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_PREP_FAILURE);
            XNAP_HANDOVER_PREP_FAILURE(fail) = (xnap_handover_preparation_failure_t){
                .s_ng_node_ue_xnap_id = ue->s_ng_node_ue_xnap_id,
                .cause = cause,
                .rrc_ue_id = ue->rrc_ue_id,
            };
            itti_send_msg_to_task(TASK_RRC_GNB, instance, fail);
            continue;
          }
          /* cancel the handover from this peer: the target RRC releases the UE if not arrived yet */
          LOG_W(XNAP,
                "[gNB %ld] Xn peer lost: cancelling handover of rrc_ue_id %u (t_xnap_ue_id %u)\n",
                instance,
                ue->rrc_ue_id,
                ue->t_ng_node_ue_xnap_id);
          /* no UE was created by RRC for this handover yet */
          if (ue->rrc_ue_id == 0)
            continue;
          MessageDef *cancel = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_CANCEL);
          XNAP_HANDOVER_CANCEL(cancel) = (xnap_handover_cancel_t){
              .s_ng_node_ue_xnap_id = ue->s_ng_node_ue_xnap_id,
              .cause = cause,
              .rrc_ue_id = ue->rrc_ue_id,
          };
          itti_send_msg_to_task(TASK_RRC_GNB, instance, cancel);
        }
      }
    }
  } else {
    peer->state = XNAP_PEER_STATE_CONNECTED;
    LOG_I(XNAP, "[gNB %ld] Xn peer assoc_id %d (gNB_id 0x%x) setup complete — notifying RRC\n",
          instance, peer->assoc_id, peer->remote_gnb_id);

    /* Notify RRC so it can register this peer as an Xn-capable candidate */
    MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_SETUP_IND);
    XNAP_SETUP_IND(msg).gnb_id   = peer->remote_gnb_id;
    XNAP_SETUP_IND(msg).assoc_id = peer->assoc_id;
    itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);
  }
}

typedef int (*xnap_message_decoded_callback)(instance_t instance,
                                              sctp_assoc_t assoc_id,
                                              uint32_t stream,
                                              xnap_gnb_inst_t *inst,
                                              xnap_peer_t *peer,
                                              XNAP_XnAP_PDU_t *pdu);

static int send_xn_setup_failure(instance_t instance, xnap_peer_t *peer, xnap_cause_group_t type, uint8_t value)
{
  const xnap_setup_failure_t fail = {.cause = {.type = type, .value = value}};
  XNAP_XnAP_PDU_t *fail_pdu = encode_xn_setup_failure(&fail);
  AssertFatal(fail_pdu != NULL, "[gNB %ld] encode_xn_setup_failure() failed\n", instance);
  uint8_t *buffer = NULL;
  uint32_t length = 0;
  int rc = xnap_gNB_encode_pdu(fail_pdu, &buffer, &length);
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, fail_pdu);
  if (rc < 0) {
    /* the peer is rejected anyway: release the association without the XnSetupFailure */
    LOG_E(XNAP, "[gNB %ld] Failed to encode XnSetupFailure, closing assoc_id %d\n", instance, peer->assoc_id);
    xnap_gNB_itti_send_sctp_close_association(instance, peer->assoc_id);
    xnap_remove_peer(xnap_get_inst(instance), peer);
    return -1;
  }
  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
  return -1;
}

static int xnap_gNB_handle_xn_setup_request(instance_t instance,
                                             sctp_assoc_t assoc_id,
                                             uint32_t stream,
                                             xnap_gnb_inst_t *inst,
                                             xnap_peer_t *peer,
                                             XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(assoc_id);
  UNUSED(stream);

  xnap_setup_req_t req = {0};
  if (!decode_xn_setup_request(&req, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode XnSetupRequest from assoc_id %d\n",
          instance, peer->assoc_id);
    return -1;
  }

  peer->remote_gnb_id = req.gNB_id;

  /* Another association to this same remote gNB is already up (e.g. both
   * sides dialled each other simultaneously): keep the existing one, close
   * this redundant association, and don't notify RRC about this one. */
  xnap_peer_t *dup = xnap_get_connected_peer_by_remote_gnb_id(inst, peer->remote_gnb_id, peer->assoc_id);
  if (dup != NULL) {
    LOG_W(XNAP, "[gNB %ld] Duplicate Xn association to gNB_id 0x%x: assoc_id %d already connected, closing redundant assoc_id %d\n",
          instance, peer->remote_gnb_id, dup->assoc_id, peer->assoc_id);
    free_xnap_setup_request(&req);
    xnap_gNB_itti_send_sctp_close_association(instance, peer->assoc_id);
    xnap_remove_peer(inst, peer);
    return -1;
  }

  /* Reject if the peer belongs to a different PLMN — most common failure
   * in misconfigured or cross-operator deployments */
  const plmn_id_t *peer_plmn  = &req.plmn;
  const plmn_id_t *local_plmn = &inst->setup_info.plmn;
  if (peer_plmn->mcc != local_plmn->mcc || peer_plmn->mnc != local_plmn->mnc) {
    LOG_W(XNAP, "[gNB %ld] XnSetupRequest from assoc_id %d (gNB_id 0x%x): "
          "PLMN mismatch (peer MCC/MNC %u/%u, local %u/%u) — rejecting\n",
          instance, peer->assoc_id, peer->remote_gnb_id,
          peer_plmn->mcc, peer_plmn->mnc, local_plmn->mcc, local_plmn->mnc);
    free_xnap_setup_request(&req);
    return send_xn_setup_failure(instance, peer, XNAP_CAUSE_MISC, XNAP_CAUSE_MISC_O_AND_M_INTERVENTION);
  }

  free_xnap_setup_request(&req);

  LOG_I(XNAP, "[gNB %ld] XnSetupRequest from peer assoc_id %d (gNB_id 0x%x) — sending XnSetupResponse\n",
        instance, peer->assoc_id, peer->remote_gnb_id);

  xnap_setup_resp_t resp = {
    .gNB_id = inst->setup_info.gNB_id,
    .plmn = inst->setup_info.plmn,
    .num_tai = inst->setup_info.num_tai,
    .tai_support = inst->setup_info.tai_support,
  };

  XNAP_XnAP_PDU_t *resp_pdu = encode_xn_setup_response(&resp);
  AssertFatal(resp_pdu != NULL, "[gNB %ld] encode_xn_setup_response() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  int rc = xnap_gNB_encode_pdu(resp_pdu, &buffer, &length);
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, resp_pdu);
  if (rc < 0) {
    /* the peer would wait for the response: release the association, RRC is not notified */
    LOG_E(XNAP, "[gNB %ld] Failed to encode XnSetupResponse, closing assoc_id %d\n", instance, peer->assoc_id);
    xnap_gNB_itti_send_sctp_close_association(instance, peer->assoc_id);
    xnap_remove_peer(inst, peer);
    return -1;
  }

  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);

  /* Xn interface is now active from our side — notify RRC */
  xnap_handle_xn_setup_message(instance, peer, 0);
  return 0;
}

static int xnap_gNB_handle_xn_setup_response(instance_t instance,
                                              sctp_assoc_t assoc_id,
                                              uint32_t stream,
                                              xnap_gnb_inst_t *inst,
                                              xnap_peer_t *peer,
                                              XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(assoc_id);
  UNUSED(stream);

  xnap_setup_resp_t resp = {0};
  if (!decode_xn_setup_response(&resp, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode XnSetupResponse from assoc_id %d\n",
          instance, peer->assoc_id);
    return -1;
  }

  peer->remote_gnb_id = resp.gNB_id;
  free_xnap_setup_response(&resp);

  /* Another association to this same remote gNB is already up (e.g. both
   * sides dialled each other simultaneously): keep the existing one, close
   * this redundant association, and don't notify RRC about this one. */
  xnap_peer_t *dup = xnap_get_connected_peer_by_remote_gnb_id(inst, peer->remote_gnb_id, peer->assoc_id);
  if (dup != NULL) {
    LOG_W(XNAP, "[gNB %ld] Duplicate Xn association to gNB_id 0x%x: assoc_id %d already connected, closing redundant assoc_id %d\n",
          instance, peer->remote_gnb_id, dup->assoc_id, peer->assoc_id);
    xnap_gNB_itti_send_sctp_close_association(instance, peer->assoc_id);
    xnap_remove_peer(inst, peer);
    return -1;
  }

  xnap_handle_xn_setup_message(instance, peer, 0);
  return 0;
}

static const char *xnap_cause_group2str(xnap_cause_group_t t)
{
  switch (t) {
    case XNAP_CAUSE_RADIO_NETWORK: return "radioNetwork";
    case XNAP_CAUSE_TRANSPORT:     return "transport";
    case XNAP_CAUSE_PROTOCOL:      return "protocol";
    case XNAP_CAUSE_MISC:          return "misc";
    default:                       return "unknown";
  }
}

static int xnap_gNB_handle_xn_setup_failure(instance_t instance,
                                             sctp_assoc_t assoc_id,
                                             uint32_t stream,
                                             xnap_gnb_inst_t *inst,
                                             xnap_peer_t *peer,
                                             XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(assoc_id);
  UNUSED(stream);

  xnap_setup_failure_t fail = {0};
  if (!decode_xn_setup_failure(&fail, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode XnSetupFailure from assoc_id %d\n",
          instance, peer->assoc_id);
    return -1;
  }

  LOG_W(XNAP, "[gNB %ld] XnSetupFailure from peer assoc_id %d (gNB_id 0x%x): "
        "cause %s value=%d — Xn interface not established\n",
        instance, peer->assoc_id, peer->remote_gnb_id,
        xnap_cause_group2str(fail.cause.type), fail.cause.value);

  xnap_remove_peer(inst, peer);

  free_xnap_setup_failure(&fail);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Handover Preparation handlers                                      */
/* ------------------------------------------------------------------ */

/* Target gNB: receives HandoverRequest from source — decode and forward to RRC */
static int xnap_gNB_handle_handover_request(instance_t instance,
                                            sctp_assoc_t assoc_id,
                                            uint32_t stream,
                                            xnap_gnb_inst_t *inst,
                                            xnap_peer_t *peer,
                                            XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received HandoverRequest from assoc_id %d\n", instance, assoc_id);

  MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_REQ);
  xnap_handover_req_t *req = &XNAP_HANDOVER_REQ(msg);

  if (!decode_xnap_handover_request(req, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode HandoverRequest from assoc_id %d\n",
          instance, assoc_id);
    free_xnap_handover_request(req);
    itti_free(TASK_XNAP, msg);
    return -1;
  }

  /* Allocate the target XnAP UE ID; RRC binds it to the UE it creates */
  req->t_ng_node_ue_xnap_id = xn_add_ue(XN_ROLE_TARGET, 0, req->s_ng_node_ue_xnap_id, assoc_id);
  if (req->t_ng_node_ue_xnap_id == 0) {
    LOG_W(XNAP, "[gNB %ld] HandoverRequest: s_xnap_ue_id %u from assoc_id %d already received, dropping\n",
          instance, req->s_ng_node_ue_xnap_id, assoc_id);
    free_xnap_handover_request(req);
    itti_free(TASK_XNAP, msg);
    return -1;
  }
  LOG_D(XNAP,
        "[gNB %ld] HandoverRequest s_xnap_ue_id %u -> t_xnap_ue_id %u: target NR Cell ID 0x%lx, cause %d/%d, "
        "NCC %ld, %d PDU session(s), RRC context %zu bytes\n",
        instance,
        req->s_ng_node_ue_xnap_id,
        req->t_ng_node_ue_xnap_id,
        req->target_cgi.nrcell_id,
        req->cause.type,
        req->cause.value,
        req->ue_context.as_security_ncc,
        req->ue_context.num_pdu,
        req->ue_context.rrc_context.len);

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);
  return 0;
}

/* A UE-associated message is valid only from the peer of its handover */
static bool xn_ue_from_peer(instance_t instance, const char *msg_name, const xn_ue_data_t *ue, sctp_assoc_t assoc_id)
{
  if (ue->assoc_id == assoc_id)
    return true;
  LOG_W(XNAP,
        "[gNB %ld] %s from assoc_id %d, handover is with assoc_id %d: dropping\n",
        instance,
        msg_name,
        assoc_id,
        ue->assoc_id);
  return false;
}

/* Source gNB: receives HandoverRequestAck from target — decode, look up rrc_ue_id, forward to RRC */
static int xnap_gNB_handle_handover_request_acknowledge(instance_t instance,
                                                         sctp_assoc_t assoc_id,
                                                         uint32_t stream,
                                                         xnap_gnb_inst_t *inst,
                                                         xnap_peer_t *peer,
                                                         XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received HandoverRequestAck from assoc_id %d\n", instance, assoc_id);

  xnap_handover_req_ack_t ack = {0};
  if (!decode_xnap_handover_request_acknowledge(&ack, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode HandoverRequestAck from assoc_id %d\n",
          instance, assoc_id);
    free_xnap_handover_request_acknowledge(&ack);
    return -1;
  }

  /* Look up source UE data using s_ng_node_ue_xnap_id, keep the target XnAP UE ID */
  xn_ue_data_t ue_data;
  if (!xn_get_ue_by_xnap_id(XN_ROLE_SOURCE, ack.s_ng_node_ue_xnap_id, &ue_data)) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequestAck: unknown s_xnap_ue_id %u\n", instance, ack.s_ng_node_ue_xnap_id);
    free_xnap_handover_request_acknowledge(&ack);
    return -1;
  }
  if (!xn_ue_from_peer(instance, "HandoverRequestAck", &ue_data, assoc_id)) {
    free_xnap_handover_request_acknowledge(&ack);
    return -1;
  }
  if (ue_data.t_ng_node_ue_xnap_id != 0) {
    LOG_W(XNAP,
          "[gNB %ld] HandoverRequestAck: s_xnap_ue_id %u already acknowledged, dropping\n",
          instance,
          ack.s_ng_node_ue_xnap_id);
    free_xnap_handover_request_acknowledge(&ack);
    return -1;
  }
  xn_set_ue_peer_id(ack.s_ng_node_ue_xnap_id, ack.t_ng_node_ue_xnap_id);

  /* Populate routing fields for RRC */
  ack.rrc_ue_id = ue_data.rrc_ue_id;

  MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_REQ_ACK);
  XNAP_HANDOVER_REQ_ACK(msg) = ack;

  LOG_I(XNAP,
        "[gNB %ld] HandoverRequestAck: rrc_ue_id %u s_xnap_ue_id %u t_xnap_ue_id %u — notifying RRC\n",
        instance,
        ue_data.rrc_ue_id,
        ack.s_ng_node_ue_xnap_id,
        ack.t_ng_node_ue_xnap_id);

  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);
  return 0;
}

/* Source gNB: receives HandoverPreparationFailure from target — decode, look up rrc_ue_id,
 * drop the UE mapping (the HO preparation is over) and forward to RRC */
static int xnap_gNB_handle_handover_prep_failure(instance_t instance,
                                                  sctp_assoc_t assoc_id,
                                                  uint32_t stream,
                                                  xnap_gnb_inst_t *inst,
                                                  xnap_peer_t *peer,
                                                  XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received HandoverPreparationFailure from assoc_id %d\n", instance, assoc_id);

  xnap_handover_preparation_failure_t fail = {0};
  if (!decode_xnap_handover_preparation_failure(&fail, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode HandoverPreparationFailure from assoc_id %d\n",
          instance, assoc_id);
    return -1;
  }

  xn_ue_data_t ue_data;
  if (!xn_get_ue_by_xnap_id(XN_ROLE_SOURCE, fail.s_ng_node_ue_xnap_id, &ue_data)) {
    LOG_E(XNAP, "[gNB %ld] HandoverPreparationFailure: unknown s_xnap_ue_id %u\n",
          instance, fail.s_ng_node_ue_xnap_id);
    return -1;
  }
  if (!xn_ue_from_peer(instance, "HandoverPreparationFailure", &ue_data, assoc_id))
    return -1;
  if (ue_data.t_ng_node_ue_xnap_id != 0) {
    LOG_W(XNAP,
          "[gNB %ld] HandoverPreparationFailure: s_xnap_ue_id %u already acknowledged, dropping\n",
          instance,
          fail.s_ng_node_ue_xnap_id);
    return -1;
  }
  xn_remove_ue_by_xnap_id(fail.s_ng_node_ue_xnap_id);

  fail.rrc_ue_id = ue_data.rrc_ue_id;

  LOG_W(XNAP, "[gNB %ld] HandoverPreparationFailure: rrc_ue_id %u s_xnap_ue_id %u cause group %d value %d — notifying RRC\n",
        instance, fail.rrc_ue_id, fail.s_ng_node_ue_xnap_id, fail.cause.type, fail.cause.value);

  MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_PREP_FAILURE);
  XNAP_HANDOVER_PREP_FAILURE(msg) = fail;
  itti_send_msg_to_task(TASK_RRC_GNB, instance, msg);
  return 0;
}

/* Target gNB: receives SN Status Transfer from source — decode, look up rrc_ue_id, forward to RRC */
static int xnap_gNB_handle_sn_status_transfer(instance_t instance,
                                               sctp_assoc_t assoc_id,
                                               uint32_t stream,
                                               xnap_gnb_inst_t *inst,
                                               xnap_peer_t *peer,
                                               XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received SN Status Transfer from assoc_id %d\n", instance, assoc_id);

  xnap_sn_status_transfer_t msg = {0};
  if (!decode_xnap_sn_status_transfer(&msg, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode SN Status Transfer from assoc_id %d\n",
          instance, assoc_id);
    return -1;
  }

  xn_ue_data_t ue_data;
  if (!xn_get_ue_by_xnap_id(XN_ROLE_TARGET, msg.t_ng_node_ue_xnap_id, &ue_data) || ue_data.rrc_ue_id == 0) {
    LOG_E(XNAP, "[gNB %ld] SN Status Transfer: unknown t_xnap_ue_id %u\n", instance, msg.t_ng_node_ue_xnap_id);
    return -1;
  }
  if (!xn_ue_from_peer(instance, "SN Status Transfer", &ue_data, assoc_id))
    return -1;
  if (msg.s_ng_node_ue_xnap_id != ue_data.s_ng_node_ue_xnap_id) {
    LOG_W(XNAP,
          "[gNB %ld] SN Status Transfer: t_xnap_ue_id %u with s_xnap_ue_id %u, expected %u: dropping\n",
          instance,
          msg.t_ng_node_ue_xnap_id,
          msg.s_ng_node_ue_xnap_id,
          ue_data.s_ng_node_ue_xnap_id);
    return -1;
  }
  msg.rrc_ue_id = ue_data.rrc_ue_id;

  MessageDef *itti_msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_SN_STATUS_TRANSFER);
  XNAP_SN_STATUS_TRANSFER(itti_msg) = msg;
  itti_send_msg_to_task(TASK_RRC_GNB, instance, itti_msg);
  return 0;
}

/* Source gNB: receives UE Context Release from target — decode, look up rrc_ue_id,
 * drop the UE mapping (the handover is over) and forward to RRC */
static int xnap_gNB_handle_ue_context_release(instance_t instance,
                                               sctp_assoc_t assoc_id,
                                               uint32_t stream,
                                               xnap_gnb_inst_t *inst,
                                               xnap_peer_t *peer,
                                               XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received UE Context Release from assoc_id %d\n", instance, assoc_id);

  xnap_ue_context_release_t msg = {0};
  if (!decode_xnap_ue_context_release(&msg, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode UE Context Release from assoc_id %d\n",
          instance, assoc_id);
    return -1;
  }

  /* the handover is over at the source */
  xn_ue_data_t ue_data;
  if (!xn_get_ue_by_xnap_id(XN_ROLE_SOURCE, msg.s_ng_node_ue_xnap_id, &ue_data)) {
    LOG_W(XNAP, "[gNB %ld] UE Context Release: unknown s_xnap_ue_id %u\n", instance, msg.s_ng_node_ue_xnap_id);
    return -1;
  }
  if (!xn_ue_from_peer(instance, "UE Context Release", &ue_data, assoc_id))
    return -1;
  if (msg.t_ng_node_ue_xnap_id != ue_data.t_ng_node_ue_xnap_id) {
    LOG_W(XNAP,
          "[gNB %ld] UE Context Release: s_xnap_ue_id %u with t_xnap_ue_id %u, expected %u: dropping\n",
          instance,
          msg.s_ng_node_ue_xnap_id,
          msg.t_ng_node_ue_xnap_id,
          ue_data.t_ng_node_ue_xnap_id);
    return -1;
  }
  xn_remove_ue_by_xnap_id(msg.s_ng_node_ue_xnap_id);
  msg.rrc_ue_id = ue_data.rrc_ue_id;

  MessageDef *itti_msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_UE_CONTEXT_RELEASE);
  XNAP_UE_CONTEXT_RELEASE(itti_msg) = msg;
  itti_send_msg_to_task(TASK_RRC_GNB, instance, itti_msg);
  return 0;
}

/* Target gNB: receives Handover Cancel from source — decode, remove the target XnAP UE ID
 * and forward to RRC to release the UE prepared for the handover */
static int xnap_gNB_handle_handover_cancel(instance_t instance,
                                           sctp_assoc_t assoc_id,
                                           uint32_t stream,
                                           xnap_gnb_inst_t *inst,
                                           xnap_peer_t *peer,
                                           XNAP_XnAP_PDU_t *pdu)
{
  UNUSED(stream);
  UNUSED(peer);
  UNUSED(inst);
  LOG_I(XNAP, "[gNB %ld] Received Handover Cancel from assoc_id %d\n", instance, assoc_id);

  xnap_handover_cancel_t msg = {0};
  if (!decode_xnap_handover_cancel(&msg, pdu)) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode Handover Cancel from assoc_id %d\n", instance, assoc_id);
    return -1;
  }

  /* the Handover Cancel carries the source XnAP UE ID only */
  xn_ue_data_t ue_data;
  if (!xn_remove_ue_by_peer_id(XN_ROLE_TARGET, assoc_id, msg.s_ng_node_ue_xnap_id, &ue_data)) {
    LOG_W(XNAP, "[gNB %ld] Handover Cancel: unknown s_xnap_ue_id %u\n", instance, msg.s_ng_node_ue_xnap_id);
    return -1;
  }
  /* no UE at RRC yet: binding the XnAP UE ID fails, RRC drops the handover */
  if (ue_data.rrc_ue_id == 0)
    return 0;
  msg.rrc_ue_id = ue_data.rrc_ue_id;

  MessageDef *itti_msg = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_CANCEL);
  XNAP_HANDOVER_CANCEL(itti_msg) = msg;
  itti_send_msg_to_task(TASK_RRC_GNB, instance, itti_msg);
  return 0;
}

/* Callback table
 * Indexed by [procedureCode][direction-1]
 * where direction is:  0 = initiatingMessage, 1 = successfulOutcome, 2 = unsuccessfulOutcome
 * Procedure codes taken from XNAP_ProcedureCode.h (TS 38.423 v16.2.0)
 */
static const xnap_message_decoded_callback xnap_messages_callback[][3] = {
  {xnap_gNB_handle_handover_request, xnap_gNB_handle_handover_request_acknowledge, xnap_gNB_handle_handover_prep_failure}, /*  0 handoverPreparation */
  {xnap_gNB_handle_sn_status_transfer, 0, 0}, /*  1 sNStatusTransfer */
  {xnap_gNB_handle_handover_cancel, 0, 0}, /*  2 handoverCancel */
  {0, 0, 0}, /*  3 retrieveUEContext */
  {0, 0, 0}, /*  4 rANPaging */
  {0, 0, 0}, /*  5 xnUAddressIndication */
  {xnap_gNB_handle_ue_context_release, 0, 0}, /*  6 uEContextRelease */
  {0, 0, 0}, /*  7 sNGRANnodeAdditionPreparation */
  {0, 0, 0}, /*  8 sNGRANnodeReconfigurationCompletion */
  {0, 0, 0}, /*  9 mNGRANnodeinitiatedSNGRANnodeModificationPrep */
  {0, 0, 0}, /* 10 sNGRANnodeinitiatedSNGRANnodeModificationPrep */
  {0, 0, 0}, /* 11 mNGRANnodeinitiatedSNGRANnodeRelease */
  {0, 0, 0}, /* 12 sNGRANnodeinitiatedSNGRANnodeRelease */
  {0, 0, 0}, /* 13 sNGRANnodeCounterCheck */
  {0, 0, 0}, /* 14 sNGRANnodeChange */
  {0, 0, 0}, /* 15 rRCTransfer */
  {0, 0, 0}, /* 16 xnRemoval */
  {xnap_gNB_handle_xn_setup_request, xnap_gNB_handle_xn_setup_response, xnap_gNB_handle_xn_setup_failure}, /* 17 xnSetup */
  {0, 0, 0}, /* 18 nGRANnodeConfigurationUpdate */
  {0, 0, 0}, /* 19 cellActivation */
  {0, 0, 0}, /* 20 reset */
  {0, 0, 0}, /* 21 errorIndication */
  {0, 0, 0}, /* 22 privateMessage */
  {0, 0, 0}, /* 23 notificationControl */
  {0, 0, 0}, /* 24 activityNotification */
  {0, 0, 0}, /* 25 e_UTRA_NR_CellResourceCoordination */
  {0, 0, 0}, /* 26 secondaryRATDataUsageReport */
  {0, 0, 0}, /* 27 deactivateTrace */
  {0, 0, 0}, /* 28 traceStart */
  {0, 0, 0}, /* 29 handoverSuccess */
  {0, 0, 0}, /* 30 conditionalHandoverCancel */
  {0, 0, 0}, /* 31 earlyStatusTransfer */
  {0, 0, 0}, /* 32 failureIndication */
  {0, 0, 0}, /* 33 handoverReport */
  {0, 0, 0}, /* 34 resourceStatusReportingInitiation */
  {0, 0, 0}, /* 35 resourceStatusReporting */
  {0, 0, 0}, /* 36 mobilitySettingsChange */
  {0, 0, 0}, /* 37 accessAndMobilityIndication */
};

static const char *xnap_direction2str(int dir)
{
  static const char *const s[] = {"", "initiatingMessage", "successfulOutcome", "unsuccessfulOutcome"};
  return (dir >= 0 && dir <= 3) ? s[dir] : "unknown";
}

static XNAP_XnAP_PDU_t *xnap_gNB_decode_pdu(const uint8_t *const buf, uint32_t len)
{
  DevAssert(buf != NULL);
  asn_codec_ctx_t st = {.max_stack_size = 100 * 1000};
  XNAP_XnAP_PDU_t *pdu = NULL;
  asn_dec_rval_t dec = aper_decode(&st, &asn_DEF_XNAP_XnAP_PDU, (void **)&pdu, buf, len, 0, 0);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    LOG_E(XNAP, "----------------- ASN1 DECODER PRINT START----------------- \n");
    xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, pdu);
    LOG_E(XNAP, "----------------- ASN1 DECODER PRINT END ----------------- \n");
  }

  return dec.code == RC_OK ? pdu : NULL;
}

int xnap_gNB_handle_message(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, const uint8_t *buf, uint32_t len)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, assoc_id);
  if (peer == NULL) {
    LOG_E(XNAP, "[gNB %ld] xnap_gNB_handle_message: no peer for assoc_id %d\n", instance, assoc_id);
    return -1;
  }

  XNAP_XnAP_PDU_t *pdu = xnap_gNB_decode_pdu(buf, len);
  if (pdu == NULL) {
    LOG_E(XNAP, "[gNB %ld] Failed to decode XnAP PDU from assoc_id %d\n", instance, assoc_id);
    return -1;
  }

  /* Checking procedure Code and direction of message */
  if (pdu->choice.initiatingMessage->procedureCode >= (long)(sizeof(xnap_messages_callback) / (3 * sizeof(xnap_message_decoded_callback)))
      || pdu->present > XNAP_XnAP_PDU_PR_unsuccessfulOutcome) {
    LOG_E(XNAP,
          "[gNB %ld] [SCTP %d] Either procedureCode %ld or direction %d exceed expected\n",
          instance,
          assoc_id,
          pdu->choice.initiatingMessage->procedureCode,
          pdu->present);
    ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
    return -1;
  }

  int ret;
  if (xnap_messages_callback[pdu->choice.initiatingMessage->procedureCode][pdu->present - 1] == NULL) {
    // No handler present. This can mean not implemented or no procedure for this direction.
    LOG_W(XNAP,
          "[gNB %ld] [SCTP %d] No handler for procedureCode %ld in %s\n",
          instance,
          assoc_id,
          pdu->choice.initiatingMessage->procedureCode,
          xnap_direction2str(pdu->present));
    ret = 0;
  } else {
    /* Calling the right handler */
    LOG_D(XNAP, "Calling handler with instance %ld\n", instance);
    ret = (*xnap_messages_callback[pdu->choice.initiatingMessage->procedureCode][pdu->present - 1])(instance, assoc_id, stream, inst, peer, pdu);
  }

  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
  return ret;
}
