/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* XnAP task: SCTP associations to the Xn peers, Xn Setup, and sending of the messages from RRC */

#include <string.h>
#include <stdlib.h>
#include "common/platform_types.h"
#include "common/utils/LOG/log.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "assertions.h"
#include "openair2/COMMON/sctp_messages_types.h"
#include "openair2/COMMON/xnap_messages_types.h"
#include "xnap_default_values.h"
#include "xnap_common.h"
#include "xnap_gNB.h"
#include "lib/xnap_gNB_interface_management.h"
#include "lib/xnap_gNB_mobility_management.h"
#include "xnap_ids.h"
#include "xnap_gNB_itti_messaging.h"
#include "xnap_gNB_handlers.h"
#include "xnap_gNB_encoder.h"

static void xnap_gNB_generate_xn_setup_request(instance_t instance, xnap_gnb_inst_t *inst, xnap_peer_t *peer)
{
  const xnap_setup_req_t *req = &inst->setup_info;

  XNAP_XnAP_PDU_t *pdu = encode_xn_setup_request(req);
  AssertFatal(pdu != NULL, "[gNB %ld] encode_xn_setup_request() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  int rc = xnap_gNB_encode_pdu(pdu, &buffer, &length);
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
  if (rc < 0) {
    /* without Xn Setup the association is of no use: release it */
    LOG_E(XNAP, "[gNB %ld] Failed to encode XnSetupRequest, closing assoc_id %d\n", instance, peer->assoc_id);
    xnap_gNB_itti_send_sctp_close_association(instance, peer->assoc_id);
    xnap_remove_peer(inst, peer);
    return;
  }

  LOG_I(XNAP, "[gNB %ld] Sending XnSetupRequest to peer assoc_id %d (%u bytes)\n",
        instance, peer->assoc_id, length);

  /* XnSetup is non-UE-associated signalling — always stream 0 */
  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
}

/* Source gNB: the HandoverRequest was not sent: the source RRC ends the handover
 * as for a Handover Preparation Failure, the UE stays on the source */
static void xnap_gNB_handover_request_not_sent(instance_t instance, uint32_t rrc_ue_id, xnap_cause_t cause)
{
  MessageDef *fail = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_PREP_FAILURE);
  XNAP_HANDOVER_PREP_FAILURE(fail) = (xnap_handover_preparation_failure_t){.cause = cause, .rrc_ue_id = rrc_ue_id};
  itti_send_msg_to_task(TASK_RRC_GNB, instance, fail);
}

/* Source gNB: RRC triggers Xn HO — allocate and store XnAP UE ID, send HandoverRequest */
static void xnap_gNB_generate_handover_request(instance_t instance, xnap_handover_req_t *req)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  const xnap_cause_t no_peer = {.type = XNAP_CAUSE_TRANSPORT, .value = XNAP_CAUSE_TRANSPORT_LAYER_TRANSPORT_RESOURCE_UNAVAILABLE};
  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, req->target_assoc_id);
  if (peer == NULL) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequest: no peer for assoc_id %d\n",
          instance, req->target_assoc_id);
    xnap_gNB_handover_request_not_sent(instance, req->rrc_ue_id, no_peer);
    return;
  }
  if (peer->state != XNAP_PEER_STATE_CONNECTED) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequest: peer assoc_id %d not connected\n",
          instance, req->target_assoc_id);
    xnap_gNB_handover_request_not_sent(instance, req->rrc_ue_id, no_peer);
    return;
  }

  /* Allocate the source XnAP UE ID, kept with the rrc_ue_id and target association so
   * that later messages of this handover are routed by rrc_ue_id */
  req->s_ng_node_ue_xnap_id = xn_add_ue(XN_ROLE_SOURCE, req->rrc_ue_id, 0, req->target_assoc_id);

  XNAP_XnAP_PDU_t *pdu = encode_xnap_handover_request(req);
  AssertFatal(pdu != NULL, "[gNB %ld] encode_xnap_handover_request() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  if (xnap_gNB_encode_pdu(pdu, &buffer, &length) < 0) {
    LOG_E(XNAP, "[gNB %ld] Failed to encode HandoverRequest (rrc_ue_id %u)\n", instance, req->rrc_ue_id);
    ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
    xn_remove_ue_by_xnap_id(req->s_ng_node_ue_xnap_id);
    const xnap_cause_t cause = {.type = XNAP_CAUSE_PROTOCOL, .value = XNAP_CAUSE_PROTOCOL_UNSPECIFIED};
    xnap_gNB_handover_request_not_sent(instance, req->rrc_ue_id, cause);
    return;
  }
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);

  LOG_I(XNAP, "[gNB %ld] Sending HandoverRequest to peer assoc_id %d xnap_ue_id %u rrc_ue_id %u (%u bytes)\n",
        instance, req->target_assoc_id, req->s_ng_node_ue_xnap_id, req->rrc_ue_id, length);

  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
}

/* Target gNB: RRC rejects a HandoverRequest — encode and send HandoverPreparationFailure to source.
 * The sender resolved the XnAP UE IDs and removed them: the handover preparation is over */
static void xnap_gNB_generate_handover_prep_failure(instance_t instance, xnap_handover_preparation_failure_t *msg)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, msg->assoc_id);
  if (peer == NULL) {
    LOG_E(XNAP, "[gNB %ld] HandoverPreparationFailure: no peer for assoc_id %d\n",
          instance, msg->assoc_id);
    return;
  }
  if (peer->state != XNAP_PEER_STATE_CONNECTED) {
    LOG_E(XNAP, "[gNB %ld] HandoverPreparationFailure: peer assoc_id %d not connected\n",
          instance, msg->assoc_id);
    return;
  }

  XNAP_XnAP_PDU_t *pdu = encode_xnap_handover_preparation_failure(msg);
  AssertFatal(pdu != NULL, "[gNB %ld] encode_xnap_handover_preparation_failure() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  int rc = xnap_gNB_encode_pdu(pdu, &buffer, &length);
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
  if (rc < 0) {
    LOG_E(XNAP, "[gNB %ld] Failed to encode HandoverPreparationFailure (rrc_ue_id %u)\n", instance, msg->rrc_ue_id);
    return;
  }

  LOG_I(XNAP, "[gNB %ld] Sending HandoverPreparationFailure to source assoc_id %d s_xnap_ue_id %u "
        "cause group %d value %d (%u bytes)\n",
        instance, msg->assoc_id, msg->s_ng_node_ue_xnap_id, msg->cause.type, msg->cause.value, length);

  xnap_gNB_itti_send_sctp_data(instance, msg->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
}

/* Target gNB: the HandoverRequestAck was not sent: if the peer is up, send a
 * HandoverPreparationFailure so the source ends the handover, and let the target
 * RRC abort the prepared UE */
static void xnap_gNB_handover_request_ack_not_sent(instance_t instance, const xn_ue_data_t *ue, bool peer_up)
{
  const xnap_cause_t cause = {.type = XNAP_CAUSE_RADIO_NETWORK, .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_UNSPECIFIED};
  xn_remove_ue_by_xnap_id(ue->t_ng_node_ue_xnap_id);
  if (peer_up) {
    xnap_handover_preparation_failure_t fail = {.s_ng_node_ue_xnap_id = ue->s_ng_node_ue_xnap_id,
                                                .cause = cause,
                                                .rrc_ue_id = ue->rrc_ue_id,
                                                .assoc_id = ue->assoc_id};
    xnap_gNB_generate_handover_prep_failure(instance, &fail);
  }
  MessageDef *cancel = itti_alloc_new_message(TASK_XNAP, instance, XNAP_HANDOVER_CANCEL);
  XNAP_HANDOVER_CANCEL(cancel) = (xnap_handover_cancel_t){
      .s_ng_node_ue_xnap_id = ue->s_ng_node_ue_xnap_id,
      .cause = cause,
      .rrc_ue_id = ue->rrc_ue_id,
  };
  itti_send_msg_to_task(TASK_RRC_GNB, instance, cancel);
}

/* Target gNB: target RRC sends ACK — fill the XnAP UE IDs of the handover, send HandoverRequestAck */
static void xnap_gNB_generate_handover_request_acknowledge(instance_t instance, xnap_handover_req_ack_t *ack)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  xn_ue_data_t ue_data;
  if (!xn_get_ue(XN_ROLE_TARGET, ack->rrc_ue_id, &ue_data)) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequestAck: no Xn handover for rrc_ue_id %u\n", instance, ack->rrc_ue_id);
    return;
  }
  ack->s_ng_node_ue_xnap_id = ue_data.s_ng_node_ue_xnap_id;
  ack->t_ng_node_ue_xnap_id = ue_data.t_ng_node_ue_xnap_id;

  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, ue_data.assoc_id);
  if (peer == NULL) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequestAck: no peer for source_assoc_id %d\n",
          instance, ue_data.assoc_id);
    xnap_gNB_handover_request_ack_not_sent(instance, &ue_data, false);
    return;
  }
  if (peer->state != XNAP_PEER_STATE_CONNECTED) {
    LOG_E(XNAP, "[gNB %ld] HandoverRequestAck: peer assoc_id %d not connected\n",
          instance, ue_data.assoc_id);
    xnap_gNB_handover_request_ack_not_sent(instance, &ue_data, false);
    return;
  }

  XNAP_XnAP_PDU_t *pdu = encode_xnap_handover_request_acknowledge(ack);
  AssertFatal(pdu != NULL, "[gNB %ld] encode_xnap_handover_request_acknowledge() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  if (xnap_gNB_encode_pdu(pdu, &buffer, &length) < 0) {
    LOG_E(XNAP, "[gNB %ld] Failed to encode HandoverRequestAck (rrc_ue_id %u)\n", instance, ack->rrc_ue_id);
    ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
    xnap_gNB_handover_request_ack_not_sent(instance, &ue_data, true);
    return;
  }
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);

  LOG_I(XNAP,
        "[gNB %ld] Sending HandoverRequestAck to source assoc_id %d "
        "s_xnap_ue_id %u t_xnap_ue_id %u rrc_ue_id %u (%u bytes)\n",
        instance,
        ue_data.assoc_id,
        ack->s_ng_node_ue_xnap_id,
        ack->t_ng_node_ue_xnap_id,
        ack->rrc_ue_id,
        length);

  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
}

/* Source gNB: RRC sends SN Status Transfer — encode and send to target */
static void xnap_gNB_generate_sn_status_transfer(instance_t instance, xnap_sn_status_transfer_t *msg)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  xn_ue_data_t ue_data;
  if (!xn_get_ue(XN_ROLE_SOURCE, msg->rrc_ue_id, &ue_data)) {
    LOG_E(XNAP, "[gNB %ld] SN Status Transfer: no Xn handover for rrc_ue_id %u\n", instance, msg->rrc_ue_id);
    return;
  }
  msg->s_ng_node_ue_xnap_id = ue_data.s_ng_node_ue_xnap_id;
  msg->t_ng_node_ue_xnap_id = ue_data.t_ng_node_ue_xnap_id;
  sctp_assoc_t assoc_id = ue_data.assoc_id;

  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, assoc_id);
  if (peer == NULL) {
    LOG_E(XNAP, "[gNB %ld] SN Status Transfer: no peer for assoc_id %d\n", instance, assoc_id);
    return;
  }
  if (peer->state != XNAP_PEER_STATE_CONNECTED) {
    LOG_E(XNAP, "[gNB %ld] SN Status Transfer: peer assoc_id %d not connected\n",
          instance, assoc_id);
    return;
  }

  XNAP_XnAP_PDU_t *pdu = encode_xnap_sn_status_transfer(msg);
  AssertFatal(pdu != NULL, "[gNB %ld] encode_xnap_sn_status_transfer() failed\n", instance);

  uint8_t *buffer = NULL;
  uint32_t length = 0;
  int rc = xnap_gNB_encode_pdu(pdu, &buffer, &length);
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
  if (rc < 0) {
    LOG_E(XNAP, "[gNB %ld] Failed to encode SN Status Transfer (rrc_ue_id %u)\n", instance, msg->rrc_ue_id);
    return;
  }

  LOG_I(XNAP,
        "[gNB %ld] Sending SN Status Transfer to peer assoc_id %d s_xnap_ue_id %u t_xnap_ue_id %u (%u bytes)\n",
        instance,
        assoc_id,
        msg->s_ng_node_ue_xnap_id,
        msg->t_ng_node_ue_xnap_id,
        length);

  xnap_gNB_itti_send_sctp_data(instance, peer->assoc_id, buffer, length, XNAP_NON_UE_STREAM_ID);
}

/* Create the Xn instance, bind a local SCTP listener (for incoming Xn
 * connections), and dial every configured candidate gNB */
static void xnap_gNB_handle_register_gnb(instance_t instance, xnap_register_gnb_req_t *req)
{
  xnap_create_inst(instance, &req->ng_setup_info, &req->net_config);
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);

  const char *local_ip = inst->net_config.gnb_xn_interface_ip_address;
  size_t addr_len = strlen(local_ip) + 1;

  MessageDef *listen_msg = itti_alloc_new_message_sized(TASK_XNAP, instance, SCTP_INIT_MSG, sizeof(sctp_init_t) + addr_len);
  sctp_init_t *init = &SCTP_INIT_MSG(listen_msg);
  init->port = XNAP_PORT_NUMBER;
  init->ppid = XNAP_SCTP_PPID;
  char *addr_buf = (char *)(init + 1);
  init->bind_address = addr_buf;
  memcpy(addr_buf, local_ip, addr_len);
  itti_send_msg_to_task(TASK_SCTP, instance, listen_msg);

  /* Candidates are not yet peers (no SCTP association exists): they are
   * addressed directly by their index in net_config, which doubles as
   * ulp_cnx_id so the eventual SCTP_NEW_ASSOCIATION_RESP can be matched back
   * to the candidate that was dialled. */
  const uint8_t nb_candidates = inst->net_config.nb_of_candidate_gNBs;
  LOG_I(XNAP, "[gNB %ld] Xn registered — listening on %s port %u, connecting to %u candidate(s)\n",
        instance, local_ip, XNAP_PORT_NUMBER, nb_candidates);

  for (uint16_t candidate_id = 0; candidate_id < nb_candidates; candidate_id++) {
    const char *remote_ip = inst->net_config.candidate_gnb_address_for_xnc[candidate_id];

    MessageDef *msg = itti_alloc_new_message(TASK_XNAP, instance, SCTP_NEW_ASSOCIATION_REQ);
    sctp_new_association_req_t *assoc_req = &msg->ittiMsg.sctp_new_association_req;

    assoc_req->ulp_cnx_id = candidate_id;
    assoc_req->port = XNAP_PORT_NUMBER;
    assoc_req->ppid = XNAP_SCTP_PPID;
    assoc_req->in_streams = inst->net_config.sctp_streams.sctp_in_streams;
    assoc_req->out_streams = inst->net_config.sctp_streams.sctp_out_streams;

    assoc_req->local_address.ipv4 = 1;
    strncpy(assoc_req->local_address.ipv4_address, local_ip, sizeof(assoc_req->local_address.ipv4_address) - 1);

    assoc_req->remote_address.ipv4 = 1;
    strncpy(assoc_req->remote_address.ipv4_address, remote_ip, sizeof(assoc_req->remote_address.ipv4_address) - 1);

    LOG_I(XNAP, "[gNB %ld] Initiating SCTP connection to candidate %u at %s port %u\n", instance, candidate_id, remote_ip, XNAP_PORT_NUMBER);

    itti_send_msg_to_task(TASK_SCTP, instance, msg);
  }
}

/* To handle SCTP association response received from the candidates to which the SCTP connection request was sent */
static void xnap_gNB_handle_sctp_association_resp(instance_t instance, const sctp_new_association_resp_t *resp)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  /* Case 1: peer already has a live association, keyed by assoc_id.
   * Remote opened the SCTP connection to us first — i.e. a simultaneous connect.
   * assoc_id == -1 means SCTP (UNREACHABLE); */
  xnap_peer_t *peer = NULL;
  if (resp->assoc_id != -1)
    peer = xnap_get_peer_by_assoc(inst, resp->assoc_id);
  if (peer != NULL) {
    if (resp->sctp_state != SCTP_STATE_ESTABLISHED) {
      LOG_W(XNAP, "[gNB %ld] SCTP_NEW_ASSOCIATION_RESP: peer assoc_id %d %s\n", instance, resp->assoc_id,
            resp->sctp_state == SCTP_STATE_SHUTDOWN ? "shut down" : "unexpected state");
      xnap_handle_xn_setup_message(instance, peer, 1 /* shutdown */);
      xnap_remove_peer(inst, peer);
      return;
    }
    /* Remote opened sctp first via IND, already keyed by assoc_id.
     * Update streams and return — peer is the initiator and will send XnSetupRequest. */
    LOG_I(XNAP, "[gNB %ld] SCTP_NEW_ASSOCIATION_RESP: peer assoc_id %d already registered "
          "via IND (simultaneous connect), updating streams\n", instance, resp->assoc_id);
    peer->in_streams  = resp->in_streams;
    peer->out_streams = resp->out_streams;
    return;
  }

  /* Case 2: no peer exists yet — this is the result of our own outgoing
   * connect attempt for candidate resp->ulp_cnx_id. */
  if (resp->sctp_state != SCTP_STATE_ESTABLISHED) {
    LOG_W(XNAP, "[gNB %ld] SCTP association failed for candidate %u (%s)\n",
          instance, resp->ulp_cnx_id,
          resp->sctp_state == SCTP_STATE_SHUTDOWN ? "shutdown" : "unreachable");
    return;
  }

  peer = xnap_add_peer(instance, inst, resp->assoc_id, resp->in_streams, resp->out_streams);

  LOG_I(XNAP, "[gNB %ld] SCTP association established with candidate %u assoc_id %d "
        "(in_streams %u out_streams %u) — sending XnSetupRequest\n",
        instance, resp->ulp_cnx_id, resp->assoc_id, resp->in_streams, resp->out_streams);

  xnap_gNB_generate_xn_setup_request(instance, inst, peer);
}

/* Peer dialled in first (before sending XnSetupRequest) — register it now */
static void xnap_gNB_handle_sctp_association_ind(instance_t instance, const sctp_new_association_ind_t *ind)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  AssertFatal(inst != NULL, "Xn instance %ld not found\n", instance);

  if (xnap_get_peer_by_assoc(inst, ind->assoc_id) != NULL) {
    LOG_W(XNAP, "[gNB %ld] SCTP_NEW_ASSOCIATION_IND: assoc_id %d already registered\n", instance, ind->assoc_id);
    return;
  }

  xnap_add_peer(instance, inst, ind->assoc_id, ind->in_streams, ind->out_streams);

  LOG_I(XNAP, "[gNB %ld] Incoming Xn connection: assoc_id %d — waiting for XnSetupRequest\n", instance, ind->assoc_id);
}

static void xnap_gNB_handle_sctp_close_association(instance_t instance, const sctp_close_association_t *close)
{
  xnap_gnb_inst_t *inst = xnap_get_inst(instance);
  if (inst == NULL) {
    LOG_W(XNAP, "[gNB %ld] SCTP_CLOSE_ASSOCIATION: instance not found\n", instance);
    return;
  }

  xnap_peer_t *peer = xnap_get_peer_by_assoc(inst, close->assoc_id);
  if (peer == NULL) {
    LOG_W(XNAP, "[gNB %ld] SCTP_CLOSE_ASSOCIATION: no peer for assoc_id %d\n",
          instance, close->assoc_id);
    return;
  }

  xnap_handle_xn_setup_message(instance, peer, 1);
  xnap_remove_peer(inst, peer);
}

/* Incoming SCTP data on an Xn association — decode and dispatch via the message callback table */
static void xnap_gNB_handle_sctp_data_ind(instance_t instance, sctp_data_ind_t *ind)
{
  int result;
  DevAssert(ind != NULL);
  xnap_gNB_handle_message(instance, ind->assoc_id, ind->stream, ind->buffer, ind->buffer_length);
  result = itti_free(TASK_UNKNOWN, ind->buffer);
  AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
}

void *xnap_task(void *args)
{
  UNUSED(args);
  LOG_I(XNAP, "Starting XnAP task\n");
  xn_init_ue_data();
  itti_mark_task_ready(TASK_XNAP);

  while (1) {
    MessageDef *msg = NULL;
    itti_receive_msg(TASK_XNAP, &msg);
    const instance_t instance = ITTI_MSG_DESTINATION_INSTANCE(msg);
    const int msgType = ITTI_MSG_ID(msg);
    LOG_D(XNAP, "XnAP received %s for instance %ld\n", ITTI_MSG_NAME(msg), instance);

    switch (msgType) {
      case XNAP_REGISTER_GNB_REQ:
        xnap_gNB_handle_register_gnb(instance, &XNAP_REGISTER_GNB_REQ(msg));
        break;

      case SCTP_NEW_ASSOCIATION_RESP:
        xnap_gNB_handle_sctp_association_resp(instance, &SCTP_NEW_ASSOCIATION_RESP(msg));
        break;

      case SCTP_NEW_ASSOCIATION_IND:
        xnap_gNB_handle_sctp_association_ind(instance, &SCTP_NEW_ASSOCIATION_IND(msg));
        break;

      case SCTP_CLOSE_ASSOCIATION:
        xnap_gNB_handle_sctp_close_association(instance, &SCTP_CLOSE_ASSOCIATION(msg));
        break;

      case SCTP_DATA_IND:
        xnap_gNB_handle_sctp_data_ind(instance, &SCTP_DATA_IND(msg));
        break;

      case XNAP_HANDOVER_REQ:
        xnap_gNB_generate_handover_request(instance, &XNAP_HANDOVER_REQ(msg));
        free_xnap_handover_request(&XNAP_HANDOVER_REQ(msg));
        break;

      case XNAP_HANDOVER_REQ_ACK:
        xnap_gNB_generate_handover_request_acknowledge(instance, &XNAP_HANDOVER_REQ_ACK(msg));
        free_xnap_handover_request_acknowledge(&XNAP_HANDOVER_REQ_ACK(msg));
        break;

      case XNAP_HANDOVER_PREP_FAILURE:
        xnap_gNB_generate_handover_prep_failure(instance, &XNAP_HANDOVER_PREP_FAILURE(msg));
        break;

      case XNAP_SN_STATUS_TRANSFER:
        xnap_gNB_generate_sn_status_transfer(instance, &XNAP_SN_STATUS_TRANSFER(msg));
        break;

      default:
        LOG_E(XNAP, "Unknown message type %d (%s)\n", msgType, ITTI_MSG_NAME(msg));
        break;
    }

    int result = itti_free(ITTI_MSG_ORIGIN_ID(msg), msg);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free ITTI message (%d)\n", result);
    msg = NULL;
  }
}
