/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* ITTI messages of XNAP */

/* gNB application layer -> XNAP messages */
MESSAGE_DEF(XNAP_REGISTER_GNB_REQ, MESSAGE_PRIORITY_MED, xnap_register_gnb_req_t, xnap_register_gnb_req)

/* XNAP -> RRC messages */
MESSAGE_DEF(XNAP_SETUP_IND,          MESSAGE_PRIORITY_MED, xnap_setup_ind_t,          xnap_setup_ind)
MESSAGE_DEF(XNAP_PEER_SHUTDOWN_IND,  MESSAGE_PRIORITY_MED, xnap_peer_shutdown_ind_t,  xnap_peer_shutdown_ind)

/* Handover Preparation: RRC -> XNAP (source sends) / XNAP -> RRC (target receives) */
MESSAGE_DEF(XNAP_HANDOVER_REQ,       MESSAGE_PRIORITY_MED, xnap_handover_req_t,       xnap_handover_req)
MESSAGE_DEF(XNAP_HANDOVER_REQ_ACK,   MESSAGE_PRIORITY_MED, xnap_handover_req_ack_t,   xnap_handover_req_ack)

/* Handover Preparation Failure: RRC -> XNAP (target sends) / XNAP -> RRC (source receives) */
MESSAGE_DEF(XNAP_HANDOVER_PREP_FAILURE, MESSAGE_PRIORITY_MED, xnap_handover_preparation_failure_t, xnap_handover_prep_failure)

/* SN Status Transfer: RRC -> XNAP (source sends) / XNAP -> RRC (target receives) */
MESSAGE_DEF(XNAP_SN_STATUS_TRANSFER,   MESSAGE_PRIORITY_MED, xnap_sn_status_transfer_t,   xnap_sn_status_transfer)

/* UE Context Release: RRC -> XNAP (target sends) / XNAP -> RRC (source receives) */
MESSAGE_DEF(XNAP_UE_CONTEXT_RELEASE,   MESSAGE_PRIORITY_MED, xnap_ue_context_release_t,   xnap_ue_context_release)

/* Handover Cancel: XNAP -> RRC (target receives; also sent by XNAP when the source peer is lost) */
MESSAGE_DEF(XNAP_HANDOVER_CANCEL,      MESSAGE_PRIORITY_MED, xnap_handover_cancel_t,      xnap_handover_cancel)
