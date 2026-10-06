/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief rrc NGAP procedures for gNB
 */

#ifndef RRC_GNB_NGAP_H_
#define RRC_GNB_NGAP_H_

#include <assertions.h>
#include <stdint.h>
#include "NR_RRCSetupComplete-IEs.h"
#include "NR_UECapabilityInformation.h"
#include "NR_UL-DCCH-Message.h"
#include "gtpv1_u_messages_types.h"
#include "intertask_interface.h"
#include "ngap_messages_types.h"
#include "nr_rrc_defs.h"

extern mui_t rrc_gNB_mui;

void rrc_gNB_send_NGAP_NAS_FIRST_REQ(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, NR_RRCSetupComplete_IEs_t *rrcSetupComplete);

int rrc_gNB_process_NGAP_INITIAL_CONTEXT_SETUP_REQ(MessageDef *msg_p, instance_t instance);

void rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

void rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_FAIL(uint32_t gnb, const ngap_cause_t causeP);

int rrc_gNB_process_NGAP_DOWNLINK_NAS(MessageDef *msg_p, instance_t instance);

void rrc_gNB_send_NGAP_UPLINK_NAS(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const NR_UL_DCCH_Message_t *const ul_dcch_msg);

void rrc_gNB_send_NGAP_PDUSESSION_SETUP_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

void rrc_gNB_process_NGAP_PDUSESSION_SETUP_REQ(MessageDef *msg_p, instance_t instance);

int rrc_gNB_process_NGAP_PDUSESSION_MODIFY_REQ(const ngap_pdusession_modify_req_t *req, instance_t instance);

int rrc_gNB_send_NGAP_PDUSESSION_MODIFY_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid);

void rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(const module_id_t gnb_mod_idP,
                                              const rrc_gNB_ue_context_t *const ue_context_pP,
                                              const ngap_cause_t causeP);

int rrc_gNB_process_NGAP_UE_CONTEXT_RELEASE_REQ(MessageDef *msg_p, instance_t instance);

int rrc_gNB_process_NGAP_UE_CONTEXT_RELEASE_COMMAND(MessageDef *msg_p, instance_t instance);

void rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_COMPLETE(instance_t instance, uint32_t gNB_ue_ngap_id, seq_arr_t *pduSessions);

void rrc_gNB_send_NGAP_UE_CAPABILITIES_IND(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const NR_UECapabilityInformation_t *const ue_cap_info);

int rrc_gNB_process_NGAP_PDUSESSION_RELEASE_COMMAND(ngap_pdusession_release_command_t *cmd, gNB_RRC_INST *rrc);

void rrc_gNB_send_NGAP_PDUSESSION_RELEASE_RESPONSE(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid);

void rrc_gNB_send_NGAP_PDUSESSION_RESOURCE_NOTIFY(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid);

void nr_rrc_pdcp_config_security(gNB_RRC_UE_t *UE, bool enable_ciphering);

int rrc_gNB_process_PAGING_IND(gNB_RRC_INST *rrc, const instance_t instance, const ngap_paging_ind_t *msg);

void rrc_gNB_send_NGAP_HANDOVER_REQUIRED(gNB_RRC_INST *rrc,
                                         gNB_RRC_UE_t *UE,
                                         const nr_neighbour_cell_t *neighbourCellConfiguration,
                                         const byte_array_t hoPrepInfo);

void rrc_gNB_send_NGAP_HANDOVER_FAILURE(gNB_RRC_INST *rrc, ngap_handover_failure_t *msg);

int rrc_gNB_process_Handover_Request(gNB_RRC_INST *rrc, ngap_handover_request_t *msg);
void rrc_gNB_free_Handover_Request(ngap_handover_request_t *msg);

void rrc_gNB_send_NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, byte_array_t ho_command);

void rrc_gNB_process_HandoverCommand(gNB_RRC_INST *rrc, const ngap_handover_command_t *msg);
void rrc_gNB_free_Handover_Command(ngap_handover_command_t *msg);

void rrc_gNB_send_NGAP_HANDOVER_NOTIFY(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);
void rrc_gNB_send_NGAP_HANDOVER_CANCEL(int module_id, gNB_RRC_UE_t *UE, ngap_cause_t cause);

int rrc_gNB_send_NGAP_ul_ran_status_transfer(gNB_RRC_INST *rrc,
                                             gNB_RRC_UE_t *UE,
                                             const int n_to_mod,
                                             const int *drb_ids,
                                             const e1_pdcp_status_info_t *pdcp_status);

int rrc_gNB_process_NGAP_DL_RAN_STATUS_TRANSFER(MessageDef *msg_p, instance_t instance);

void rrc_gNB_send_NGAP_PATH_SWITCH_REQUEST(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

int rrc_gNB_process_NGAP_PATH_SWITCH_REQUEST_ACKNOWLEDGE(gNB_RRC_INST *rrc, instance_t instance, const ngap_path_switch_req_ack_t *msg);

#endif
