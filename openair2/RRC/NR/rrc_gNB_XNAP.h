/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef RRC_GNB_XNAP_H_
#define RRC_GNB_XNAP_H_

#include "nr_rrc_defs.h"

bool rrc_gNB_send_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc,
                                        gNB_RRC_UE_t *UE,
                                        const nr_neighbour_cell_t *neighbour,
                                        const byte_array_t hoPrepInfo);

int rrc_gNB_process_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc, xnap_handover_req_t *req);

void rrc_gNB_send_XNAP_HANDOVER_REQ_ACK(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, byte_array_t ho_command);

void rrc_gNB_process_XNAP_HANDOVER_REQ_ACK(gNB_RRC_INST *rrc, const xnap_handover_req_ack_t *msg);

void rrc_gNB_send_XNAP_HANDOVER_PREP_FAILURE(gNB_RRC_INST *rrc,
                                             uint32_t rrc_ue_id,
                                             uint32_t t_ng_node_ue_xnap_id,
                                             xnap_cause_t cause);

int rrc_gNB_process_XNAP_HANDOVER_PREP_FAILURE(gNB_RRC_INST *rrc, const xnap_handover_preparation_failure_t *msg);

void rrc_gNB_xn_ho_target_abort(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const char *why);

void rrc_gNB_send_XNAP_HANDOVER_CANCEL(gNB_RRC_INST *rrc, uint32_t rrc_ue_id, xnap_cause_t cause);
void rrc_gNB_process_XNAP_HANDOVER_CANCEL(gNB_RRC_INST *rrc, const xnap_handover_cancel_t *msg);

int rrc_gNB_send_XNAP_SN_STATUS_TRANSFER(gNB_RRC_INST *rrc,
                                          gNB_RRC_UE_t *UE,
                                          const int n_to_mod,
                                          const int *drb_ids,
                                          const e1_pdcp_status_info_t *pdcp_status);

int rrc_gNB_process_XNAP_SN_STATUS_TRANSFER(gNB_RRC_INST *rrc, instance_t instance, const xnap_sn_status_transfer_t *msg);

void rrc_gNB_send_XNAP_UE_CONTEXT_RELEASE(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

int rrc_gNB_process_XNAP_UE_CONTEXT_RELEASE(gNB_RRC_INST *rrc, instance_t instance, xnap_ue_context_release_t *msg);

#endif /* RRC_GNB_XNAP_H_ */
