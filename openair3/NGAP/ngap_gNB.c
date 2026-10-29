/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NGAP gNB task
 * @ingroup _ngap
 */

#include <openair3/NGAP/ngap_gNB.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "BIT_STRING.h"
#include "ngap_msg_includes.h"
#include "OCTET_STRING.h"
#include "T.h"
#include "asn_internal.h"
#include "assertions.h"
#include "common/utils/T/T.h"
#include "conversions.h"
#include "ds/byte_array.h"
#include "intertask_interface.h"
#include "ngap_common.h"
#include "ngap_gNB.h"
#include "ngap_gNB_context_management_procedures.h"
#include "ngap_gNB_default_values.h"
#include "ngap_gNB_defs.h"
#include "ngap_gNB_encoder.h"
#include "ngap_gNB_handlers.h"
#include "ngap_gNB_itti_messaging.h"
#include "ngap_gNB_management_procedures.h"
#include "ngap_gNB_nas_procedures.h"
#include "ngap_messages_types.h"
#include "ngap_gNB_mobility_management.h"
#include "ngap_gNB_ue_context.h"
#include "ngap_gNB_pdu_session_management.h"
#include "ngap_gNB_NRPPa_transport_procedures.h"
#include "oai_asn1.h"
#include "openair3/SECU/kdf.h"
#include "queue.h"
#include "s1ap_messages_types.h"
#include "sctp_messages_types.h"
#include "tree.h"

#ifdef E2_AGENT
#include "openair2/E2AP/RAN_FUNCTION/setup_msg_store.h"
#endif

_Static_assert(NR_MAX_NB_PLMNS == NGAP_maxnoofBPLMNs, "NGAP broadcast PLMN limit must match ASN.1");

static int ngap_gNB_generate_ng_setup_request(
  ngap_gNB_instance_t *instance_p, ngap_gNB_amf_data_t *ngap_amf_data_p);

void ngap_gNB_handle_register_gNB(instance_t instance, ngap_register_gnb_req_t *ngap_register_gNB);

void ngap_gNB_handle_sctp_association_resp(instance_t instance, sctp_new_association_resp_t *sctp_new_association_resp);

uint32_t ngap_generate_gNB_id(void)
{
  /* Retrieve the host name */
  char hostname[32] = {0};
  int const ret = gethostname(hostname, sizeof(hostname));
  DevAssert(ret == 0);

  uint8_t key[32] = {"eurecom"};
  byte_array_t data = {.len = 32, .buf = (uint8_t *)hostname};

  uint8_t out[32] = {0};
  kdf(key, data, 32, out);

  uint32_t const gNB_id = ((out[0] << 24) | (out[1] << 16) | (out[2] << 8) | out[3]);
  return gNB_id;
}

static void ngap_gNB_redial_amf(ngap_gNB_amf_data_t *amf_desc_p)
{
  NGAP_INFO("AMF cnx_id = %u\n", amf_desc_p->cnx_id);
  NGAP_INFO("AMF state  = %d\n", amf_desc_p->state);
  /*reconnect with the amf */
  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, SCTP_NEW_ASSOCIATION_REQ);
  sctp_new_association_req_t *req = &message_p->ittiMsg.sctp_new_association_req;

  /*amf descriptor */
  req->port = NGAP_PORT_NUMBER;
  req->ppid = NGAP_SCTP_PPID;
  req->in_streams = amf_desc_p->in_streams;
  req->out_streams = amf_desc_p->out_streams;
  req->ulp_cnx_id = amf_desc_p->cnx_id;

  ngap_gNB_instance_t *inst = amf_desc_p->ngap_gNB_instance;

  memcpy(&req->remote_address, &amf_desc_p->amf_s1_ip, sizeof(req->remote_address));
  memcpy(&req->local_address, &inst->gNB_ng_ip, sizeof(req->local_address));

  NGAP_INFO("[gNB %ld] Re-dial AMF cnx_id=%u (streams in=%u out=%u)\n",
            inst->instance,
            amf_desc_p->cnx_id,
            req->in_streams,
            req->out_streams);

  inst->ngap_amf_nb++;
  inst->ngap_amf_pending_nb++;

  itti_send_msg_to_task(TASK_SCTP, inst->instance, message_p);
}

static void ngap_gNB_register_amf(ngap_gNB_instance_t *instance_p,
                                  net_ip_address_t *amf_ip_address,
                                  net_ip_address_t *local_ip_addr,
                                  uint16_t in_streams,
                                  uint16_t out_streams,
                                  uint8_t broadcast_plmn_num,
                                  uint8_t broadcast_plmn_index[NR_MAX_NB_PLMNS])
{
  MessageDef                 *message_p                   = NULL;
  sctp_new_association_req_t *sctp_new_association_req_p  = NULL;
  ngap_gNB_amf_data_t        *ngap_amf_data_p             = NULL;

  DevAssert(instance_p != NULL);
  DevAssert(amf_ip_address != NULL);
  DevAssert(broadcast_plmn_num > 0 && broadcast_plmn_num <= NR_MAX_NB_PLMNS);
  message_p = itti_alloc_new_message(TASK_NGAP, 0, SCTP_NEW_ASSOCIATION_REQ);
  sctp_new_association_req_p = &message_p->ittiMsg.sctp_new_association_req;
  sctp_new_association_req_p->port = NGAP_PORT_NUMBER;
  sctp_new_association_req_p->ppid = NGAP_SCTP_PPID;
  sctp_new_association_req_p->in_streams  = in_streams;
  sctp_new_association_req_p->out_streams = out_streams;
  memcpy(&sctp_new_association_req_p->remote_address,
         amf_ip_address,
         sizeof(*amf_ip_address));
  memcpy(&sctp_new_association_req_p->local_address,
         local_ip_addr,
         sizeof(*local_ip_addr));
  NGAP_INFO("[gNB %ld] check the amf registration state\n",instance_p->instance);

  /* Create new AMF descriptor */
  ngap_amf_data_p = calloc(1, sizeof(*ngap_amf_data_p));
  DevAssert(ngap_amf_data_p != NULL);
  ngap_amf_data_p->cnx_id                = ngap_gNB_fetch_add_global_cnx_id();
  sctp_new_association_req_p->ulp_cnx_id = ngap_amf_data_p->cnx_id;
  ngap_amf_data_p->assoc_id          = -1;
  ngap_amf_data_p->broadcast_plmn_num = broadcast_plmn_num;
  memcpy(&ngap_amf_data_p->amf_s1_ip,
        amf_ip_address,
        sizeof(*amf_ip_address));
  for (int i = 0; i < broadcast_plmn_num; ++i) {
    DevAssert(broadcast_plmn_index[i] < instance_p->num_plmn);
    ngap_amf_data_p->broadcast_plmn_index[i] = broadcast_plmn_index[i];
  }

  ngap_amf_data_p->ngap_gNB_instance = instance_p;
  STAILQ_INIT(&ngap_amf_data_p->served_guami);
  /* Insert the new descriptor in list of known AMF
   * but not yet associated.
   */
  RB_INSERT(ngap_amf_map, &instance_p->ngap_amf_head, ngap_amf_data_p);
  ngap_amf_data_p->state = NGAP_GNB_STATE_DISCONNECTED;
  instance_p->ngap_amf_nb ++;
  instance_p->ngap_amf_pending_nb ++;

  itti_send_msg_to_task(TASK_SCTP, instance_p->instance, message_p);
}

void ngap_gNB_handle_register_gNB(instance_t instance, ngap_register_gnb_req_t *ngap_register_gNB) {
  ngap_gNB_instance_t *new_instance;
  uint8_t index;
  DevAssert(ngap_register_gNB != NULL);
  DevAssert(ngap_register_gNB->num_plmn > 0 && ngap_register_gNB->num_plmn <= NR_MAX_NB_PLMNS);
  /* Look if the provided instance already exists */
  new_instance = ngap_gNB_get_instance(instance);

  if (new_instance != NULL) {
    /* Checks if it is a retry on the same gNB */
    DevCheck(new_instance->gNB_id == ngap_register_gNB->gNB_id, new_instance->gNB_id, ngap_register_gNB->gNB_id, 0);
    DevCheck(new_instance->cell_type == ngap_register_gNB->cell_type, new_instance->cell_type, ngap_register_gNB->cell_type, 0);
    DevCheck(new_instance->num_plmn == ngap_register_gNB->num_plmn, new_instance->num_plmn, ngap_register_gNB->num_plmn, 0);
    DevCheck(new_instance->tac == ngap_register_gNB->tac, new_instance->tac, ngap_register_gNB->tac, 0);

    for (int i = 0; i < new_instance->num_plmn; i++) {
      plmn_id_t *exist_plmn = &new_instance->plmn[i].plmn;
      plmn_id_t *new_plmn = &ngap_register_gNB->plmn[i].plmn;
      DevCheck(exist_plmn->mcc == new_plmn->mcc, exist_plmn->mcc, new_plmn->mcc, 0);
      DevCheck(exist_plmn->mnc == new_plmn->mnc, exist_plmn->mnc, new_plmn->mnc, 0);
      DevCheck(exist_plmn->mnc_digit_length == new_plmn->mnc_digit_length,
               exist_plmn->mnc_digit_length,
               new_plmn->mnc_digit_length,
               0);
    }

    DevCheck(new_instance->default_drx == ngap_register_gNB->default_drx, new_instance->default_drx, ngap_register_gNB->default_drx, 0);
  } else {
    new_instance = calloc(1, sizeof(ngap_gNB_instance_t));
    DevAssert(new_instance != NULL);
    RB_INIT(&new_instance->ngap_amf_head);
    /* Copy usefull parameters */
    new_instance->instance         = instance;
    new_instance->gNB_name         = ngap_register_gNB->gNB_name;
    new_instance->gNB_id           = ngap_register_gNB->gNB_id;
    new_instance->cell_type        = ngap_register_gNB->cell_type;
    new_instance->tac              = ngap_register_gNB->tac;
    
    memcpy(&new_instance->gNB_ng_ip,
       &ngap_register_gNB->gnb_ip_address,
       sizeof(ngap_register_gNB->gnb_ip_address));

    for (int i = 0; i < ngap_register_gNB->num_plmn; i++) {
      new_instance->plmn[i].plmn.mcc = ngap_register_gNB->plmn[i].plmn.mcc;
      new_instance->plmn[i].plmn.mnc = ngap_register_gNB->plmn[i].plmn.mnc;
      new_instance->plmn[i].plmn.mnc_digit_length = ngap_register_gNB->plmn[i].plmn.mnc_digit_length;

      new_instance->plmn[i].num_nssai = ngap_register_gNB->plmn[i].num_nssai;
      memcpy(&new_instance->plmn[i].s_nssai, &ngap_register_gNB->plmn[i].s_nssai, sizeof(ngap_register_gNB->plmn[i].s_nssai));
    }

    new_instance->num_plmn         = ngap_register_gNB->num_plmn;
    new_instance->default_drx      = ngap_register_gNB->default_drx;

    /* Add the new instance to the list of gNB (meaningfull in virtual mode) */
    ngap_gNB_insert_new_instance(new_instance);
    NGAP_INFO("Registered new gNB[%ld] and %s gNB id %u\n",
              instance,
              ngap_register_gNB->cell_type == CELL_MACRO_GNB ? "macro" : "home",
              ngap_register_gNB->gNB_id);
  }

  DevCheck(ngap_register_gNB->nb_amf <= NGAP_MAX_NB_AMF_IP_ADDRESS,
           NGAP_MAX_NB_AMF_IP_ADDRESS, ngap_register_gNB->nb_amf, 0);

  /* Trying to connect to provided list of AMF ip address */
  for (index = 0; index < ngap_register_gNB->nb_amf; index++) {
    net_ip_address_t *amf_ip = &ngap_register_gNB->amf_ip_address[index];
    struct ngap_gNB_amf_data_s *amf = NULL;
    RB_FOREACH(amf, ngap_amf_map, &new_instance->ngap_amf_head) {
      /* Compare whether IPv4 and IPv6 information is already present, in which
       * wase we do not register again */
      if (amf->amf_s1_ip.ipv4 == amf_ip->ipv4 && (!amf_ip->ipv4
              || strncmp(amf->amf_s1_ip.ipv4_address, amf_ip->ipv4_address, 16) == 0)
          && amf->amf_s1_ip.ipv6 == amf_ip->ipv6 && (!amf_ip->ipv6
              || strncmp(amf->amf_s1_ip.ipv6_address, amf_ip->ipv6_address, 46) == 0))
        break;
    }
    if (amf)
      continue;
    ngap_gNB_register_amf(new_instance,
                          amf_ip,
                          &ngap_register_gNB->gnb_ip_address,
                          ngap_register_gNB->sctp_in_streams,
                          ngap_register_gNB->sctp_out_streams,
                          ngap_register_gNB->broadcast_plmn_num[index],
                          ngap_register_gNB->broadcast_plmn_index[index]);
  }
}

void ngap_gNB_handle_sctp_association_resp(instance_t instance, sctp_new_association_resp_t *sctp_new_association_resp) {
  ngap_gNB_instance_t *instance_p;
  ngap_gNB_amf_data_t *ngap_amf_data_p;
  DevAssert(sctp_new_association_resp != NULL);
  instance_p = ngap_gNB_get_instance(instance);
  DevAssert(instance_p != NULL);
  ngap_amf_data_p = ngap_gNB_get_AMF(instance_p, -1,
                                     sctp_new_association_resp->ulp_cnx_id);
  DevAssert(ngap_amf_data_p != NULL);

  if (sctp_new_association_resp->sctp_state != SCTP_STATE_ESTABLISHED) {
    NGAP_WARN("Received unsuccessful result for SCTP association (%u), instance %ld, cnx_id %u\n",
              sctp_new_association_resp->sctp_state,
              instance,
              sctp_new_association_resp->ulp_cnx_id);
    ngap_handle_ng_setup_message(ngap_amf_data_p, sctp_new_association_resp->sctp_state == SCTP_STATE_SHUTDOWN);
    return;
  }

  /* Update parameters */
  ngap_amf_data_p->assoc_id    = sctp_new_association_resp->assoc_id;
  ngap_amf_data_p->in_streams  = sctp_new_association_resp->in_streams;
  ngap_amf_data_p->out_streams = sctp_new_association_resp->out_streams;
  /* Prepare new NG Setup Request */
  LOG_A(NGAP, "Send NGSetupRequest to AMF\n");
  ngap_gNB_generate_ng_setup_request(instance_p, ngap_amf_data_p);
}

static
void ngap_gNB_handle_sctp_data_ind(sctp_data_ind_t *sctp_data_ind) {
  int result;
  DevAssert(sctp_data_ind != NULL);
  ngap_gNB_handle_message(sctp_data_ind->assoc_id, sctp_data_ind->stream,
                          sctp_data_ind->buffer, sctp_data_ind->buffer_length);
  result = itti_free(TASK_UNKNOWN, sctp_data_ind->buffer);
  AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
}

static int ngap_gNB_path_switch_request(instance_t instance, ngap_path_switch_req_t *msg)
{
  DevAssert(msg != NULL);
  NGAP_DEBUG("Triggered Pathswitch Request\n");

  /*Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  NGAP_NGAP_PDU_t *pdu = encode_ng_path_switch_request(msg);
  DevAssert(pdu != NULL);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NGAP_NGAP_PDU, pdu);

  byte_array_t out = { .buf = NULL, .len = 0 };
  if (ngap_gNB_encode_pdu(pdu, &out.buf, (uint32_t *)&out.len) < 0) {
    NGAP_ERROR("Failed to encode Path Switch Request message\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  ngap_gNB_amf_data_t *amf_ref = ngap_gNB_get_AMF_from_instance(ngap_gNB_instance_p);
  if (!amf_ref) {
    NGAP_ERROR("Failed to fetch AMF for current NGAP instance\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  // Create and then store the NGAP UE context
  ngap_gNB_ue_context_t ue_context_p = {
    .amf_ref = amf_ref,
    .gNB_ue_ngap_id = msg->gNB_ue_ngap_id,
    .amf_ue_ngap_id = msg->amf_ue_ngap_id,
    .gNB_instance = ngap_gNB_instance_p,
    .ue_state = NGAP_UE_CONNECTED,
  };
  ngap_store_ue_context(&ue_context_p);

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p.amf_ref->assoc_id,
                                   out.buf,
                                   out.len,
                                   ue_context_p.tx_stream);
  NGAP_INFO("Sent Path Switch Request to AMF\n");
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
  return 0;
}

 /** @brief UE Mobility Management: callback for Handover Required */
int ngap_handover_required(instance_t instance, ngap_handover_required_t *msg)
{
  DevAssert(msg != NULL);
  NGAP_DEBUG("Triggered Handover Required\n");
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  ngap_gNB_ue_context_t *ue_context_p = NULL;
  if ((ue_context_p = ngap_get_ue_context(msg->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: 0x%08x\n", msg->gNB_ue_ngap_id);
    return -1;
  }

  DevAssert(ue_context_p->gNB_ue_ngap_id == msg->gNB_ue_ngap_id);

  NGAP_NGAP_PDU_t *pdu = encode_ng_handover_required(msg);
  DevAssert(pdu != NULL);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NGAP_NGAP_PDU, pdu);

  byte_array_t out = { .buf = NULL, .len = 0 };
  if (ngap_gNB_encode_pdu(pdu, &out.buf, (uint32_t *)&out.len) < 0) {
    NGAP_ERROR("Failed to encode Handover Required message\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id,
                                   out.buf,
                                   out.len,
                                   ue_context_p->tx_stream);

  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
  return 0;
}

void ngap_gNB_init(void) {
  NGAP_DEBUG("Starting NGAP layer\n");
  ngap_gNB_prepare_internal_data();
  itti_mark_task_ready(TASK_NGAP);
}

static int ngap_gNB_handover_failure(instance_t instance, const ngap_handover_failure_t *msg)
{
  ngap_gNB_ue_context_t *ue_context_p = NULL;
  uint8_t *buffer = NULL;
  uint32_t length;

  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(msg != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  NGAP_NGAP_PDU_t *pdu = encode_ng_handover_failure(msg);
  DevAssert(pdu != NULL);

  if (ngap_gNB_encode_pdu(pdu, &buffer, &length) < 0) {
    NGAP_ERROR("Failed to encode HANDOVER FAILURE MESSAGE\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  ngap_gNB_amf_data_t *amf = ngap_gNB_get_AMF_from_instance(ngap_gNB_instance_p);
  if ((ue_context_p = ngap_get_ue_context_from_amf_ue_ngap_id(msg->amf_ue_ngap_id)) == NULL) {
    //The context for this gNB ue ngap id doesn't exist in the map of gNB UEs
    NGAP_WARN("Failed to find UE context associated with amf_ue_ngap_id=%ld\n", msg->amf_ue_ngap_id);
    /*In this case there is no ue context since HO REQ might be failed at preprocessing stage itself.
    * so sctp message send to AMF based on its assoc_id  */
    ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance, amf->assoc_id, buffer, length, 0);
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance, amf->assoc_id, buffer, length, amf->nextstream);
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);

  return 0;
}

static int ngap_gNB_handover_request_acknowledge(instance_t instance, ngap_handover_request_ack_t *msg)
{
  byte_array_t ba = { .buf = NULL, .len = 0 };

  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_t *ngap = ngap_gNB_get_instance(instance);
  DevAssert(msg != NULL);
  DevAssert(ngap != NULL);

  NGAP_NGAP_PDU_t *pdu = encode_ng_handover_request_ack(msg);
  DevAssert(pdu != NULL);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NGAP_NGAP_PDU, pdu);

  if (ngap_gNB_encode_pdu(pdu, &ba.buf, (uint32_t *)&ba.len) < 0) {
    NGAP_ERROR("Failed to encode Handover Request Acknowledge message\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  // Create and store NGAP UE context
  ngap_gNB_ue_context_t ue_context_p = {
    .amf_ref = ngap_gNB_get_AMF_from_instance(ngap),
    .gNB_ue_ngap_id = msg->gNB_ue_ngap_id,
    .amf_ue_ngap_id = msg->amf_ue_ngap_id,
    .gNB_instance = ngap,
    .ue_state = NGAP_UE_CONNECTED,
  };
  if (!ue_context_p.amf_ref) {
    NGAP_ERROR("Failed to fetch AMF for current NGAP instance\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }
  ngap_store_ue_context(&ue_context_p);

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap->instance, ue_context_p.amf_ref->assoc_id, ba.buf, ba.len, ue_context_p.tx_stream);
  NGAP_INFO("Sent Handover Request Acknowledge to AMF\n");
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
  return 0;
}

static int ngap_gNB_handover_notify(instance_t instance, const ngap_handover_notify_t *msg)
{
  LOG_D(NGAP, "Encode NGAP Handover Notify\n");
  ngap_gNB_ue_context_t *ue_context_p = NULL;
  byte_array_t ba = { .buf = NULL, .len = 0};

  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(msg != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(msg->gNB_ue_ngap_id)) == NULL) {
    NGAP_ERROR("Failed to find UE context for Handover Notify: gNB_ue_ngap_id=%d\n", msg->gNB_ue_ngap_id);
    return -1;
  }
  DevAssert(ue_context_p->gNB_ue_ngap_id == msg->gNB_ue_ngap_id);

  if (ue_context_p->amf_ue_ngap_id != msg->amf_ue_ngap_id) {
    NGAP_ERROR("Failed to encode Handover Notify: unknown amf_ue_ngap_id=%ld\n", msg->amf_ue_ngap_id);
    return -1;
  }

  NGAP_NGAP_PDU_t *pdu = encode_ng_handover_notify(msg);
  if (!pdu) {
    NGAP_ERROR("Failed to encode NG Handover Notify\n");
    return -1;
  }

  if (ngap_gNB_encode_pdu(pdu, &ba.buf, (uint32_t *)&ba.len) < 0) {
    NGAP_ERROR("Failed to encode NG Handover Notify message\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id,
                                   ba.buf,
                                   ba.len,
                                   ue_context_p->tx_stream);
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);

  return 0;
}

/** @brief NGAP: Handover Cancel callback */
static int ngap_gNB_handover_cancel(instance_t instance, const ngap_handover_cancel_t *msg)
{
  DevAssert(msg != NULL);
  byte_array_t ba = {.buf = NULL, .len = 0};

  // Retrieve NGAP gNB instance
  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  // we must have the UE context by RAN UE NGAP ID
  ngap_gNB_ue_context_t *ue_context_p = ngap_get_ue_context(msg->gNB_ue_ngap_id);
  if (ue_context_p == NULL) {
    NGAP_ERROR("Failed to encode Handover Cancel: no UE context for gNB_ue_ngap_id=%d\n", msg->gNB_ue_ngap_id);
    return -1;
  }
  DevAssert(ue_context_p->gNB_ue_ngap_id == msg->gNB_ue_ngap_id);

  if (ue_context_p->amf_ue_ngap_id != msg->amf_ue_ngap_id) {
    NGAP_ERROR("Failed to encode Handover Cancel: mismatched amf_ue_ngap_id=%ld\n", msg->amf_ue_ngap_id);
    return -1;
  }

  /* Encode NGAP PDU */
  NGAP_NGAP_PDU_t *pdu = encode_ng_handover_cancel(msg);
  DevAssert(pdu != NULL);

  if (ngap_gNB_encode_pdu(pdu, &ba.buf, (uint32_t *)&ba.len) < 0) {
    NGAP_ERROR("Failed to encode NG Handover Cancel message PDU\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  /* UE-associated signalling -> use the UE's allocated SCTP stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id,
                                   ba.buf,
                                   ba.len,
                                   ue_context_p->tx_stream);

  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
  return 0;
}

int ngap_gNB_handle_ul_ran_status_transfer(instance_t instance, const ngap_ran_status_transfer_t *msg)
{
  NGAP_DEBUG("Triggered UL RAN Status Transfer\n");

  ngap_gNB_instance_t *ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  ngap_gNB_ue_context_t *ue_context_p = ngap_get_ue_context(msg->gnb_ue_ngap_id);
  if (!ue_context_p) {
    NGAP_ERROR("Failed to find UE context for gNB_ue_ngap_id %d\n", msg->gnb_ue_ngap_id);
    return -1;
  }

  DevAssert(ue_context_p->gNB_ue_ngap_id == msg->gnb_ue_ngap_id);

  NGAP_NGAP_PDU_t *pdu = encode_ng_ul_ran_status_transfer(msg);
  DevAssert(pdu != NULL);

  byte_array_t out = {.buf = NULL, .len = 0};
  if (ngap_gNB_encode_pdu(pdu, &out.buf, (uint32_t *)&out.len) < 0) {
    NGAP_ERROR("UL RAN Status Transfer encoding failure\n");
    ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
    return -1;
  }

  ngap_gNB_itti_send_sctp_data_req(instance, ue_context_p->amf_ref->assoc_id, out.buf, out.len, ue_context_p->tx_stream);
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
  return 0;
}

void *ngap_gNB_process_itti_msg(void *notUsed)
{
  UNUSED(notUsed);
  MessageDef *received_msg = NULL;
  int         result;
  itti_receive_msg(TASK_NGAP, &received_msg);
  if (received_msg) {
    instance_t instance = ITTI_MSG_DESTINATION_INSTANCE(received_msg);
    LOG_D(RRC, "Received message %s\n", ITTI_MSG_NAME(received_msg));
    switch (ITTI_MSG_ID(received_msg)) {
      case TERMINATE_MESSAGE:
        NGAP_WARN(" *** Exiting NGAP thread\n");
        itti_exit_task();
        break;

      case NGAP_REGISTER_GNB_REQ:
        /* Register a new gNB.
         * in Virtual mode gNBs will be distinguished using the mod_id/
         * Each gNB has to send an NGAP_REGISTER_GNB message with its
         * own parameters.
         */
        ngap_gNB_handle_register_gNB(instance, &NGAP_REGISTER_GNB_REQ(received_msg));
        break;

      case SCTP_NEW_ASSOCIATION_RESP:
        ngap_gNB_handle_sctp_association_resp(instance, &received_msg->ittiMsg.sctp_new_association_resp);
        break;

      case TIMER_HAS_EXPIRED: {
        timer_has_expired_t *th = &received_msg->ittiMsg.timer_has_expired;
        ngap_gNB_amf_data_t *amf_desc_p = (ngap_gNB_amf_data_t *)th->arg;
        if (amf_desc_p) {
          ngap_gNB_redial_amf(amf_desc_p);
        }
        break;
      }

      case SCTP_DATA_IND:
        ngap_gNB_handle_sctp_data_ind(&received_msg->ittiMsg.sctp_data_ind);
        break;

      case NGAP_NAS_FIRST_REQ:
        ngap_gNB_handle_nas_first_req(instance, &NGAP_NAS_FIRST_REQ(received_msg));
        free_byte_array(NGAP_NAS_FIRST_REQ(received_msg).nas_pdu);
        break;

      case NGAP_UPLINK_NAS:
        ngap_gNB_nas_uplink(instance, &NGAP_UPLINK_NAS(received_msg));
        break;

      case NGAP_UE_CAPABILITIES_IND:
        ngap_gNB_ue_capabilities(instance, &NGAP_UE_CAPABILITIES_IND(received_msg));
        break;

      case NGAP_INITIAL_CONTEXT_SETUP_RESP:
        ngap_gNB_initial_ctxt_resp(instance, &NGAP_INITIAL_CONTEXT_SETUP_RESP(received_msg));
        break;

      case NGAP_INITIAL_CONTEXT_SETUP_FAIL:
        ngap_gNB_initial_ctxt_fail(instance, &NGAP_INITIAL_CONTEXT_SETUP_FAIL(received_msg));
        break;

      case NGAP_PDUSESSION_SETUP_RESP:
        ngap_gNB_pdusession_setup_resp(instance, &NGAP_PDUSESSION_SETUP_RESP(received_msg));
        break;

      case NGAP_PDUSESSION_MODIFY_RESP:
        ngap_gNB_pdusession_modify_resp(instance, &NGAP_PDUSESSION_MODIFY_RESP(received_msg));
        break;

      case NGAP_NAS_NON_DELIVERY_IND:
        ngap_gNB_nas_non_delivery_ind(instance, &NGAP_NAS_NON_DELIVERY_IND(received_msg));
        break;

      case NGAP_UE_CONTEXT_RELEASE_COMPLETE:
        ngap_ue_context_release_complete(instance, &NGAP_UE_CONTEXT_RELEASE_COMPLETE(received_msg));
        break;

      case NGAP_UE_CONTEXT_RELEASE_REQ:
        ngap_ue_context_release_req(instance, &NGAP_UE_CONTEXT_RELEASE_REQ(received_msg));
        break;

      case NGAP_PDUSESSION_RELEASE_RESPONSE:
        ngap_gNB_pdusession_release_resp(instance, &NGAP_PDUSESSION_RELEASE_RESPONSE(received_msg));
        break;

      case NGAP_PDUSESSION_RESOURCE_NOTIFY:
        ngap_gNB_pdusession_resource_notify(instance, &NGAP_PDUSESSION_RESOURCE_NOTIFY(received_msg));
        break;

      case NGAP_PATH_SWITCH_REQ:
        ngap_gNB_path_switch_request(instance, &NGAP_PATH_SWITCH_REQ(received_msg));
        break;

      case NGAP_HANDOVER_REQUIRED:
        if (ngap_handover_required(instance, &NGAP_HANDOVER_REQUIRED(received_msg)) < 0) {
          NGAP_ERROR("Handover Required failure: indication to RRC is not sent!\n");
        }
        break;

      case NGAP_HANDOVER_FAILURE:
        ngap_gNB_handover_failure(instance, &NGAP_HANDOVER_FAILURE(received_msg));
        break;

      case NGAP_HANDOVER_REQUEST_ACKNOWLEDGE:
        ngap_gNB_handover_request_acknowledge(instance, &NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(received_msg));
        free_ng_handover_req_ack(&NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(received_msg));
        break;

      case NGAP_HANDOVER_NOTIFY:
        ngap_gNB_handover_notify(instance, &NGAP_HANDOVER_NOTIFY(received_msg));
        break;

      case NGAP_HANDOVER_CANCEL:
        ngap_gNB_handover_cancel(instance, &NGAP_HANDOVER_CANCEL(received_msg));
        break;

      case NGAP_UL_RAN_STATUS_TRANSFER:
        ngap_gNB_handle_ul_ran_status_transfer(instance, &NGAP_UL_RAN_STATUS_TRANSFER(received_msg));
        break;

      case NGAP_UPLINKUEASSOCIATEDNRPPA:
        ngap_gNB_uplink_ue_associated_nrppa_transport(instance, &NGAP_UPLINKUEASSOCIATEDNRPPA(received_msg));
        break;

      case NGAP_UPLINKNONUEASSOCIATEDNRPPA:
        ngap_gNB_uplink_non_ue_associated_nrppa_transport(instance, &NGAP_UPLINKNONUEASSOCIATEDNRPPA(received_msg));
        break;

      default:
        NGAP_ERROR("Received unhandled message: %d:%s\n", ITTI_MSG_ID(received_msg), ITTI_MSG_NAME(received_msg));
        break;
    }

    result = itti_free(ITTI_MSG_ORIGIN_ID(received_msg), received_msg);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  }
  return NULL;
}

void *ngap_gNB_task(void *arg)
{
  UNUSED(arg);
  ngap_gNB_init();

  while (1) {
    ngap_gNB_process_itti_msg(NULL);
  }

  return NULL;
}

//-----------------------------------------------------------------------------
/*
* gNB generate a NG setup request towards AMF
*/
static int ngap_gNB_generate_ng_setup_request(
  ngap_gNB_instance_t *instance_p,
  ngap_gNB_amf_data_t *ngap_amf_data_p)
//-----------------------------------------------------------------------------
{
  NGAP_NGAP_PDU_t            pdu;
  NGAP_NGSetupRequest_t      *out = NULL;
  NGAP_NGSetupRequestIEs_t   *ie = NULL;
  NGAP_SupportedTAItem_t     *ta = NULL;
  NGAP_BroadcastPLMNItem_t   *plmn = NULL;
  NGAP_SliceSupportItem_t    *ssi = NULL;
  uint8_t  *buffer = NULL;
  uint32_t  len = 0;
  DevAssert(instance_p != NULL);
  DevAssert(ngap_amf_data_p != NULL);
  ngap_amf_data_p->state = NGAP_GNB_STATE_WAITING;
  /* Prepare the NGAP message to encode */
  memset(&pdu, 0, sizeof(pdu));
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  pdu.choice.initiatingMessage = CALLOC(1, sizeof(struct NGAP_InitiatingMessage));
  pdu.choice.initiatingMessage->procedureCode = NGAP_ProcedureCode_id_NGSetup;
  pdu.choice.initiatingMessage->criticality = NGAP_Criticality_reject;
  pdu.choice.initiatingMessage->value.present = NGAP_InitiatingMessage__value_PR_NGSetupRequest;
  out = &pdu.choice.initiatingMessage->value.choice.NGSetupRequest;
  /* mandatory */
  ie = calloc_or_fail(1, sizeof(NGAP_NGSetupRequestIEs_t));
  ie->id = NGAP_ProtocolIE_ID_id_GlobalRANNodeID;
  ie->criticality = NGAP_Criticality_reject;
  ie->value.present = NGAP_NGSetupRequestIEs__value_PR_GlobalRANNodeID;
  ie->value.choice.GlobalRANNodeID.present = NGAP_GlobalRANNodeID_PR_globalGNB_ID;
  ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID = CALLOC(1, sizeof(struct NGAP_GlobalGNB_ID));
  plmn_id_t *plmn_id = &instance_p->plmn[ngap_amf_data_p->broadcast_plmn_index[0]].plmn;
  MCC_MNC_TO_PLMNID(plmn_id->mcc,
                    plmn_id->mnc,
                    plmn_id->mnc_digit_length,
                    &(ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->pLMNIdentity));
  ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.present = NGAP_GNB_ID_PR_gNB_ID;
  MACRO_GNB_ID_TO_BIT_STRING(instance_p->gNB_id,
                             &ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.choice.gNB_ID);
  NGAP_INFO("%u -> %02x%02x%02x%02x\n", instance_p->gNB_id,
            ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.choice.gNB_ID.buf[0],
            ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.choice.gNB_ID.buf[1],
            ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.choice.gNB_ID.buf[2],
            ie->value.choice.GlobalRANNodeID.choice.globalGNB_ID->gNB_ID.choice.gNB_ID.buf[3]);
  asn1cSeqAdd(&out->protocolIEs.list, ie);

  /* optional */
  if (instance_p->gNB_name) {
    ie = calloc_or_fail(1, sizeof(NGAP_NGSetupRequestIEs_t));
    ie->id = NGAP_ProtocolIE_ID_id_RANNodeName;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_NGSetupRequestIEs__value_PR_RANNodeName;
    OCTET_STRING_fromBuf(&ie->value.choice.RANNodeName, instance_p->gNB_name,
                         strlen(instance_p->gNB_name));
    asn1cSeqAdd(&out->protocolIEs.list, ie);
  }

  /* mandatory */
  ie = calloc_or_fail(1, sizeof(NGAP_NGSetupRequestIEs_t));
  ie->id = NGAP_ProtocolIE_ID_id_SupportedTAList;
  ie->criticality = NGAP_Criticality_reject;
  ie->value.present = NGAP_NGSetupRequestIEs__value_PR_SupportedTAList;
  {
    ta = (NGAP_SupportedTAItem_t *)calloc_or_fail(1, sizeof(NGAP_SupportedTAItem_t));
    INT24_TO_OCTET_STRING(instance_p->tac, &ta->tAC);
    {
      for (int i = 0; i < ngap_amf_data_p->broadcast_plmn_num; ++i) {
        plmn = (NGAP_BroadcastPLMNItem_t *)calloc_or_fail(1, sizeof(NGAP_BroadcastPLMNItem_t));
        ngap_plmn_t *plmn_req = &instance_p->plmn[ngap_amf_data_p->broadcast_plmn_index[i]];
        plmn_id_t *plmn_id = &instance_p->plmn[ngap_amf_data_p->broadcast_plmn_index[i]].plmn;
        MCC_MNC_TO_TBCD(plmn_id->mcc, plmn_id->mnc, plmn_id->mnc_digit_length, &plmn->pLMNIdentity);

        for (int si = 0; si < plmn_req->num_nssai; si++) {
          ssi = (NGAP_SliceSupportItem_t *)calloc_or_fail(1, sizeof(NGAP_SliceSupportItem_t));
          INT8_TO_OCTET_STRING(plmn_req->s_nssai[si].sst, &ssi->s_NSSAI.sST);

          const uint32_t sd = plmn_req->s_nssai[si].sd;
          if (sd != 0xffffff) {
            ssi->s_NSSAI.sD = calloc_or_fail(1, sizeof(NGAP_SD_t));
            ssi->s_NSSAI.sD->buf = calloc_or_fail(3, sizeof(uint8_t));
            ssi->s_NSSAI.sD->size = 3;
            ssi->s_NSSAI.sD->buf[0] = (sd & 0xff0000) >> 16;
            ssi->s_NSSAI.sD->buf[1] = (sd & 0x00ff00) >> 8;
            ssi->s_NSSAI.sD->buf[2] = (sd & 0x0000ff);
          }

          asn1cSeqAdd(&plmn->tAISliceSupportList.list, ssi);
        }

        asn1cSeqAdd(&ta->broadcastPLMNList.list, plmn);
      }
    }
    asn1cSeqAdd(&ie->value.choice.SupportedTAList.list, ta);
  }
  asn1cSeqAdd(&out->protocolIEs.list, ie);
  
  /* mandatory */
  ie = calloc_or_fail(1, sizeof(NGAP_NGSetupRequestIEs_t));
  ie->id = NGAP_ProtocolIE_ID_id_DefaultPagingDRX;
  ie->criticality = NGAP_Criticality_ignore;
  ie->value.present = NGAP_NGSetupRequestIEs__value_PR_PagingDRX;
  ie->value.choice.PagingDRX = instance_p->default_drx;
  asn1cSeqAdd(&out->protocolIEs.list, ie);


  if (ngap_gNB_encode_pdu(&pdu, &buffer, &len) < 0) {
    NGAP_ERROR("Failed to encode NG setup request\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

#ifdef E2_AGENT
  e2ap_store_setup_req(E2AP_SETUP_MSG_NGAP, buffer, len);
#endif
  /* Non UE-Associated signalling -> stream = 0 */
  ngap_gNB_itti_send_sctp_data_req(instance_p->instance, ngap_amf_data_p->assoc_id, buffer, len, 0);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}




