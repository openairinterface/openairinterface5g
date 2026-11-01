/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
 
/*!
 * \brief ngap itti messages handlers for gNB
 */

#ifndef NGAP_GNB_ITTI_MESSAGING_H_
#define NGAP_GNB_ITTI_MESSAGING_H_

#include <assertions.h>
#include <netinet/sctp.h>
#include <stdint.h>

void ngap_gNB_itti_send_sctp_data_req(instance_t instance,
                                      sctp_assoc_t assoc_id,
                                      uint8_t *buffer,
                                      uint32_t buffer_length,
                                      uint16_t stream);

void ngap_gNB_itti_send_nas_downlink_ind(instance_t instance,
                                         uint32_t gNB_ue_ngap_id,
                                         uint64_t amf_ue_ngap_id,
                                         uint8_t *nas_pdu,
                                         uint32_t nas_pdu_length,
                                         const ngap_ambr_t *ue_ambr);

void ngap_gNB_itti_send_sctp_close_association(instance_t instance, sctp_assoc_t assoc_id);

#endif /* NGAP_GNB_ITTI_MESSAGING_H_ */
