/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
 
/*!
 * \brief ngap itti messages handlers for gNB
 */

#include <stdlib.h>
#include <string.h>
#include "intertask_interface.h"
#include "ngap_messages_types.h"
#include "sctp_messages_types.h"

void ngap_gNB_itti_send_sctp_data_req(instance_t instance,
                                      sctp_assoc_t assoc_id,
                                      uint8_t *buffer,
                                      uint32_t buffer_length,
                                      uint16_t stream)
{
  MessageDef      *message_p;
  sctp_data_req_t *sctp_data_req;

  message_p = itti_alloc_new_message(TASK_NGAP, 0, SCTP_DATA_REQ);

  sctp_data_req = &message_p->ittiMsg.sctp_data_req;

  sctp_data_req->assoc_id      = assoc_id;
  sctp_data_req->buffer        = buffer;
  sctp_data_req->buffer_length = buffer_length;
  sctp_data_req->stream = stream;

  itti_send_msg_to_task(TASK_SCTP, instance, message_p);
}

void ngap_gNB_itti_send_nas_downlink_ind(instance_t instance,
                                         uint32_t gNB_ue_ngap_id,
                                         uint64_t amf_ue_ngap_id,
                                         uint8_t *nas_pdu,
                                         uint32_t nas_pdu_length,
                                         const ngap_ambr_t *ue_ambr)
{
  MessageDef          *message_p;
  ngap_downlink_nas_t *ngap_downlink_nas;

  message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_DOWNLINK_NAS);

  ngap_downlink_nas = &message_p->ittiMsg.ngap_downlink_nas;

  ngap_downlink_nas->gNB_ue_ngap_id = gNB_ue_ngap_id;
  ngap_downlink_nas->amf_ue_ngap_id = amf_ue_ngap_id;
  ngap_downlink_nas->nas_pdu = create_byte_array(nas_pdu_length, nas_pdu);
  if (ue_ambr) {
    ngap_downlink_nas->has_ue_ambr = true;
    ngap_downlink_nas->ue_ambr = *ue_ambr;
  }

  itti_send_msg_to_task(TASK_RRC_GNB, instance, message_p);
}

void ngap_gNB_itti_send_sctp_close_association(instance_t instance, sctp_assoc_t assoc_id)
{
  MessageDef               *message_p = NULL;
  sctp_close_association_t *sctp_close_association_p = NULL;

  message_p = itti_alloc_new_message(TASK_NGAP, 0, SCTP_CLOSE_ASSOCIATION);
  sctp_close_association_p = &message_p->ittiMsg.sctp_close_association;
  sctp_close_association_p->assoc_id      = assoc_id;

  itti_send_msg_to_task(TASK_SCTP, instance, message_p);
}

