/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief RRC functions prototypes for gNB
 */
/** \addtogroup _rrc
 *  @{
 */

#ifndef __NR_RRC_PROTO_H__
#define __NR_RRC_PROTO_H__

#include "RRC/NR/nr_rrc_defs.h"
#include "NR_CG-Config.h"
#include "NR_CG-ConfigInfo.h"
#include "NR_RRCReconfiguration.h"
#include "RRC/NR/MESSAGES/asn1_msg.h"
#include "f1ap_messages_types.h"

#define SRB1 1
#define SRB2 2

void rrc_add_nsa_user(gNB_RRC_INST *rrc, x2ap_ENDC_sgnb_addition_req_t *m, sctp_assoc_t assoc_id);
void rrc_add_nsa_user_resp(gNB_RRC_UE_t *UE, const f1ap_ue_context_setup_resp_t *resp);
void rrc_release_nsa_user(gNB_RRC_INST *rrc, rrc_gNB_ue_context_t *ue_context);
void rrc_remove_nsa_user_context(gNB_RRC_INST *rrc, rrc_gNB_ue_context_t *ue_context);

void rrc_remove_ue(gNB_RRC_INST *rrc, rrc_gNB_ue_context_t *ue_context_p);

int parse_CG_ConfigInfo(gNB_RRC_INST *rrc, NR_CG_ConfigInfo_t *CG_ConfigInfo, x2ap_ENDC_sgnb_addition_req_t *m);

void rrc_gNB_generate_SecurityModeCommand(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue_p);

void rrc_forward_ue_nas_message(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

unsigned int rrc_gNB_get_next_transaction_identifier(module_id_t gnb_mod_idP);

void rrc_gNB_generate_RRCRelease(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE);

/**\brief RRC eNB task.
   \param args_p Pointer on arguments to start the task. */
void *rrc_gnb_task(void *args_p);

bool ue_associated_to_cuup(const gNB_RRC_UE_t *ue);
sctp_assoc_t get_existing_cuup_for_ue(const gNB_RRC_UE_t *ue);
sctp_assoc_t get_new_cuup_for_ue(const gNB_RRC_INST *rrc, const gNB_RRC_UE_t *ue, int sst, int sd);
int rrc_gNB_process_e1_setup_req(sctp_assoc_t assoc_id, const e1ap_setup_req_t *req);
bool is_cuup_associated(gNB_RRC_INST *rrc);

/* Process indication of E1 connection loss on CU-CP */
void rrc_gNB_process_e1_lost_connection(gNB_RRC_INST *rrc, sctp_assoc_t assoc_id);

void bearer_context_setup_direct(e1ap_bearer_setup_req_t *req,
                                 instance_t instance);

void bearer_context_setup_e1ap(e1ap_bearer_setup_req_t *req,
                                 instance_t instance);

void ue_cxt_mod_send_e1ap(MessageDef *msg,
                          instance_t instance);

void ue_cxt_mod_direct(MessageDef *msg,
                       instance_t instance);

void prepare_and_send_ue_context_modification_f1(rrc_gNB_ue_context_t *ue_context_p,
                                                 e1ap_bearer_setup_resp_t *e1ap_resp);
void trigger_bearer_setup(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint64_t ueAggMaxBitRateDownlink);

/** @}*/

/* UE Management Procedures */

void rrc_f1_ue_context_setup_from_e1_response(const gNB_RRC_INST *rrc,
                                              rrc_gNB_ue_context_t *const ue_context_pP,
                                              const e1ap_bearer_setup_resp_t *resp);

void free_RRCReconfiguration_params(nr_rrc_reconfig_param_t params);

byte_array_t rrc_gNB_encode_RRCReconfiguration(gNB_RRC_UE_t *UE, nr_rrc_reconfig_param_t params);

nr_rrc_reconfig_param_t get_RRCReconfiguration_params(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t srb_reest_bitmap, bool drb_reestablish);

f1ap_qos_flow_param_t nr_rrc_get_f1_qos_flow_param(const pdusession_level_qos_parameter_t *qos_param);
void openair_rrc_gNB_configuration(gNB_RRC_INST *rrc, nr_rrc_config_t *configuration);
NR_SRB_ToAddModList_t *createSRBlist(gNB_RRC_UE_t *ue, uint8_t reestablish);
NR_DRB_ToAddModList_t *createDRBlist(gNB_RRC_UE_t *ue, bool reestablish, bool do_integrity, bool do_ciphering);
void activate_srb(gNB_RRC_UE_t *UE, int srb_id);
void e1_notify_pdcp_status(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const rrc_drb_pdcp_status_t *drb_status);
void init_delayed_action(delayed_action_state_t *delayed_action);

/** @brief Prepare and send F1AP UE Context Setup Request for a target DU
 * @param rrc RRC instance
 * @param ue UE context
 * @param cell Target cell container containing cell information
 * @param ho_prep_info Optional handover preparation information (NULL if not provided) */
void rrc_f1_ue_context_setup_for_target_du(const gNB_RRC_INST *rrc,
                                           gNB_RRC_UE_t *ue,
                                           const nr_rrc_cell_container_t *cell,
                                           const byte_array_t *ho_prep_info);
const neighbour_cell_configuration_t *get_neighbour_cell_config(const gNB_RRC_INST *rrc, int cell_id);

/** @brief Record that a UE has successfully completed an F1 or N2 handover from
 *         from_cell_id to to_cell_id, incrementing that neighbour relation's
 *         ho_success_count count. No-op if from_cell_id has no configured
 *         neighbour to_cell_id. */
void nr_rrc_mark_ho_completed(const gNB_RRC_INST *rrc, const uint64_t from_cell_id, const uint64_t to_cell_id);

void nr_rrc_transfer_protected_rrc_message(const gNB_RRC_INST *rrc,
                                           const gNB_RRC_UE_t *ue_p,
                                           uint8_t srb_id,
                                           const uint32_t message_id,
                                           const uint8_t *buffer,
                                           int size);

void store_du_f1u_tunnel(const f1ap_drb_setup_t *drbs, int n, gNB_RRC_UE_t *ue);
void e1_send_bearer_updates(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, int n, f1ap_drb_setup_t *drbs);

#endif
