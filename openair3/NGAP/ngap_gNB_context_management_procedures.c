/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NGAP context management procedures
 * @ingroup _ngap
 */

#include "ngap_gNB_context_management_procedures.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "INTEGER.h"
#include "T.h"
#include "assertions.h"
#include "common/utils/T/T.h"
#include "intertask_interface.h"
#include "ngap_msg_includes.h"
#include "ngap_common.h"
#include "ngap_gNB_defs.h"
#include "ngap_gNB_encoder.h"
#include "ngap_gNB_itti_messaging.h"
#include "ngap_gNB_management_procedures.h"
#include "ngap_gNB_ue_context.h"
#include "oai_asn1.h"

int ngap_ue_context_release_complete(instance_t instance,
                                     ngap_ue_release_complete_t *ue_release_complete_p)
{

  ngap_gNB_instance_t                 *ngap_gNB_instance_p = NULL;
  struct ngap_gNB_ue_context_s        *ue_context_p        = NULL;
  NGAP_NGAP_PDU_t pdu = {0};
  uint8_t  *buffer;
  uint32_t length;

  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);

  DevAssert(ue_release_complete_p != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(ue_release_complete_p->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_ERROR("Failed to find ue context associated with gNB ue ngap id: %u\n", ue_release_complete_p->gNB_ue_ngap_id);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  pdu.present = NGAP_NGAP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu.choice.successfulOutcome, head);
  head->procedureCode = NGAP_ProcedureCode_id_UEContextRelease;
  head->criticality = NGAP_Criticality_reject;
  head->value.present = NGAP_SuccessfulOutcome__value_PR_UEContextReleaseComplete;
  NGAP_UEContextReleaseComplete_t *out = &head->value.choice.UEContextReleaseComplete;

  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseComplete_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_UEContextReleaseComplete_IEs__value_PR_AMF_UE_NGAP_ID;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }

  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseComplete_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_UEContextReleaseComplete_IEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = ue_release_complete_p->gNB_ue_ngap_id;
  }

  /* number of PDU sessions */
  if (ue_release_complete_p->num_pdu_sessions > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseComplete_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_PDUSessionResourceListCxtRelCpl;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UEContextReleaseComplete_IEs__value_PR_PDUSessionResourceListCxtRelCpl;
    NGAP_PDUSessionResourceListCxtRelCpl_t *l = &ie->value.choice.PDUSessionResourceListCxtRelCpl;
    DevAssert(ue_release_complete_p->num_pdu_sessions <= 256);
    for (int i = 0; i < ue_release_complete_p->num_pdu_sessions; ++i) {
      asn1cSequenceAdd(l->list, NGAP_PDUSessionResourceItemCxtRelCpl_t, pdu);
      pdu->pDUSessionID = ue_release_complete_p->pdu_session_id[i];
    }
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    /* Encode procedure has failed... */
    NGAP_ERROR("Failed to encode UE context release complete\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id, buffer,
                                   length, ue_context_p->tx_stream);

  //LG ngap_gNB_itti_send_sctp_close_association(ngap_gNB_instance_p->instance,
  //                                             ue_context_p->amf_ref->assoc_id);
  // release UE context
  ngap_gNB_ue_context_t *tmp = ngap_detach_ue_context(ue_release_complete_p->gNB_ue_ngap_id);
  if (tmp)
    free(tmp);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}

void ngap_ue_context_local_release(const ngap_ue_local_release_t *rel)
{
  ngap_gNB_ue_context_t *ue = ngap_detach_ue_context(rel->gNB_ue_ngap_id);
  if (ue == NULL)
    return;
  LOG_I(NGAP, "UE %u: NGAP UE context released locally (AMF UE NGAP ID %lu)\n", ue->gNB_ue_ngap_id, ue->amf_ue_ngap_id);
  free(ue);
}

int ngap_ue_context_release_req(instance_t instance,
                                ngap_ue_release_req_t *ue_release_req_p)
{
  ngap_gNB_instance_t                *ngap_gNB_instance_p           = NULL;
  struct ngap_gNB_ue_context_s       *ue_context_p                  = NULL;
  NGAP_NGAP_PDU_t pdu = {0};
  uint8_t                            *buffer                        = NULL;
  uint32_t                            length;
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);

  DevAssert(ue_release_req_p != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(ue_release_req_p->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: %u\n",
              ue_release_req_p->gNB_ue_ngap_id);
    /* send response to free the UE: we don't know it, but it should be
     * released since RRC seems to know it (e.g., there is no AMF) */
    MessageDef *msg = itti_alloc_new_message(TASK_NGAP, 0, NGAP_UE_CONTEXT_RELEASE_COMMAND);
    NGAP_UE_CONTEXT_RELEASE_COMMAND(msg).gNB_ue_ngap_id = ue_release_req_p->gNB_ue_ngap_id;
    itti_send_msg_to_task(TASK_RRC_GNB, ngap_gNB_instance_p->instance, msg);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_UEContextReleaseRequest;
  head->criticality = NGAP_Criticality_ignore;
  head->value.present = NGAP_InitiatingMessage__value_PR_UEContextReleaseRequest;
  NGAP_UEContextReleaseRequest_t *out = &head->value.choice.UEContextReleaseRequest;

  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseRequest_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UEContextReleaseRequest_IEs__value_PR_AMF_UE_NGAP_ID;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }

  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseRequest_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UEContextReleaseRequest_IEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = ue_release_req_p->gNB_ue_ngap_id;
  }

  /* optional */
  if (ue_release_req_p->nb_of_pdusessions > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseRequest_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_PDUSessionResourceListCxtRelReq;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UEContextReleaseRequest_IEs__value_PR_PDUSessionResourceListCxtRelReq;
    for (int i = 0; i < ue_release_req_p->nb_of_pdusessions; i++) {
      NGAP_PDUSessionResourceItemCxtRelReq_t     *item;
      item = (NGAP_PDUSessionResourceItemCxtRelReq_t *)calloc(1,sizeof(NGAP_PDUSessionResourceItemCxtRelReq_t));
      item->pDUSessionID = ue_release_req_p->pdusession_ids[i];
      asn1cSeqAdd(&ie->value.choice.PDUSessionResourceListCxtRelReq.list, item);
    }
  }

  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UEContextReleaseRequest_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_Cause;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_UEContextReleaseRequest_IEs__value_PR_Cause;
    DevAssert(ue_release_req_p->cause.type <= NGAP_CAUSE_MISC);
    encode_ngap_cause(&ie->value.choice.Cause, &ue_release_req_p->cause);
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    /* Encode procedure has failed... */
    NGAP_ERROR("Failed to encode UE context release complete\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id, buffer,
                                   length, ue_context_p->tx_stream);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}

