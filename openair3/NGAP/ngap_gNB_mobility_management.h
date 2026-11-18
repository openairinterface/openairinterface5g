/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NGAP_GNB_MOBILITY_MANAGEMENT_H_
#define NGAP_GNB_MOBILITY_MANAGEMENT_H_

#include <stdint.h>
#include "common/platform_types.h"
#include "ngap_messages_types.h"

typedef struct NGAP_NGAP_PDU NGAP_NGAP_PDU_t;

NGAP_NGAP_PDU_t *encode_ng_handover_required(const ngap_handover_required_t *msg);
NGAP_NGAP_PDU_t *encode_ng_handover_failure(const ngap_handover_failure_t *msg);
int decode_ng_handover_request(ngap_handover_request_t *out, const NGAP_NGAP_PDU_t *pdu);
NGAP_NGAP_PDU_t *encode_ng_handover_request_ack(ngap_handover_request_ack_t *msg);
void free_ng_handover_req_ack(ngap_handover_request_ack_t *msg);
int decode_ng_handover_command(ngap_handover_command_t *msg, NGAP_NGAP_PDU_t *pdu);
void free_ng_handover_command(ngap_handover_command_t *msg);
NGAP_NGAP_PDU_t *encode_ng_handover_notify(const ngap_handover_notify_t *msg);
NGAP_NGAP_PDU_t *encode_ng_ul_ran_status_transfer(const ngap_ran_status_transfer_t *msg);
NGAP_NGAP_PDU_t *encode_ng_handover_cancel(const ngap_handover_cancel_t *msg);
int decode_ng_handover_cancel_ack(ngap_handover_cancel_ack_t *out, const NGAP_NGAP_PDU_t *pdu);
NGAP_NGAP_PDU_t *encode_ng_path_switch_request(const ngap_path_switch_req_t *msg);
int decode_ng_path_switch_request_acknowledge(ngap_path_switch_req_ack_t *msg, NGAP_NGAP_PDU_t *pdu);
void free_ng_path_switch_req_ack(ngap_path_switch_req_ack_t *msg);

#endif /* NGAP_GNB_MOBILITY_MANAGEMENT_H_ */
