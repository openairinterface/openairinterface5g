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
