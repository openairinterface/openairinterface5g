/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * ngap_messages_def.h
 */

/* Messages for NGAP logging */
MESSAGE_DEF(NGAP_UPLINK_NAS_LOG            , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_uplink_nas_log)
MESSAGE_DEF(NGAP_UE_CAPABILITY_IND_LOG     , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_ue_capability_ind_log)
MESSAGE_DEF(NGAP_INITIAL_CONTEXT_SETUP_LOG , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_initial_context_setup_log)
MESSAGE_DEF(NGAP_NAS_NON_DELIVERY_IND_LOG  , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_nas_non_delivery_ind_log)
MESSAGE_DEF(NGAP_DOWNLINK_NAS_LOG          , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_downlink_nas_log)
MESSAGE_DEF(NGAP_S1_SETUP_LOG              , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_s1_setup_log)
MESSAGE_DEF(NGAP_INITIAL_UE_MESSAGE_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_initial_ue_message_log)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_REQ_LOG, MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_ue_context_release_req_log)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_COMMAND_LOG, MESSAGE_PRIORITY_MED, IttiMsgText                  , ngap_ue_context_release_command_log)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_COMPLETE_LOG, MESSAGE_PRIORITY_MED, IttiMsgText                 , ngap_ue_context_release_complete_log)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_ue_context_release_log)
MESSAGE_DEF(NGAP_PDUSESSION_SETUP_REQUEST_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_setup_request_log)
MESSAGE_DEF(NGAP_PDUSESSION_SETUP_RESPONSE_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_setup_response_log)
MESSAGE_DEF(NGAP_PDUSESSION_MODIFY_REQUEST_LOG     , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_modify_request_log)
MESSAGE_DEF(NGAP_PDUSESSION_MODIFY_RESPONSE_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_modify_response_log)
MESSAGE_DEF(NGAP_PAGING_LOG    , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_paging_log)
MESSAGE_DEF(NGAP_PDUSESSION_RELEASE_REQUEST_LOG   , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_release_request_log)
MESSAGE_DEF(NGAP_PDUSESSION_RELEASE_RESPONSE_LOG  , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_pdusession_release_response_log)
MESSAGE_DEF(NGAP_ERROR_INDICATION_LOG        , MESSAGE_PRIORITY_MED, IttiMsgText                      , ngap_error_indication_log)

/* gNB application layer -> NGAP messages */
MESSAGE_DEF(NGAP_REGISTER_GNB_REQ          , MESSAGE_PRIORITY_MED, ngap_register_gnb_req_t          , ngap_register_gnb_req)

/* NGAP -> gNB application layer messages */
MESSAGE_DEF(NGAP_REGISTER_GNB_CNF          , MESSAGE_PRIORITY_MED, ngap_register_gnb_cnf_t          , ngap_register_gnb_cnf)
MESSAGE_DEF(NGAP_DEREGISTERED_GNB_IND      , MESSAGE_PRIORITY_MED, ngap_deregistered_gnb_ind_t      , ngap_deregistered_gnb_ind)

/* RRC -> NGAP messages */
MESSAGE_DEF(NGAP_NAS_FIRST_REQ             , MESSAGE_PRIORITY_MED, ngap_nas_first_req_t             , ngap_nas_first_req)
MESSAGE_DEF(NGAP_UPLINK_NAS                , MESSAGE_PRIORITY_MED, ngap_uplink_nas_t                , ngap_uplink_nas)
MESSAGE_DEF(NGAP_UE_CAPABILITIES_IND       , MESSAGE_PRIORITY_MED, ngap_ue_cap_info_ind_t           , ngap_ue_cap_info_ind)
MESSAGE_DEF(NGAP_INITIAL_CONTEXT_SETUP_RESP, MESSAGE_PRIORITY_MED, ngap_initial_context_setup_resp_t, ngap_initial_context_setup_resp)
MESSAGE_DEF(NGAP_INITIAL_CONTEXT_SETUP_FAIL, MESSAGE_PRIORITY_MED, ngap_initial_context_setup_fail_t, ngap_initial_context_setup_fail)
MESSAGE_DEF(NGAP_NAS_NON_DELIVERY_IND      , MESSAGE_PRIORITY_MED, ngap_nas_non_delivery_ind_t      , ngap_nas_non_delivery_ind)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_COMPLETE, MESSAGE_PRIORITY_MED, ngap_ue_release_complete_t      , ngap_ue_release_complete)
MESSAGE_DEF(NGAP_UE_CONTEXT_LOCAL_RELEASE  , MESSAGE_PRIORITY_MED, ngap_ue_local_release_t         , ngap_ue_local_release)
MESSAGE_DEF(NGAP_PDUSESSION_SETUP_RESP          , MESSAGE_PRIORITY_MED, ngap_pdusession_setup_resp_t          , ngap_pdusession_setup_resp)
MESSAGE_DEF(NGAP_PDUSESSION_MODIFY_RESP          , MESSAGE_PRIORITY_MED, ngap_pdusession_modify_resp_t          , ngap_pdusession_modify_resp)
MESSAGE_DEF(NGAP_PDUSESSION_RELEASE_RESPONSE    , MESSAGE_PRIORITY_MED, ngap_pdusession_release_resp_t        , ngap_pdusession_release_resp)
MESSAGE_DEF(NGAP_PDUSESSION_RESOURCE_NOTIFY     , MESSAGE_PRIORITY_MED, ngap_pdusession_resource_notify_t    , ngap_pdusession_resource_notify)
MESSAGE_DEF(NGAP_HANDOVER_REQUIRED, MESSAGE_PRIORITY_MED, ngap_handover_required_t, ngap_handover_required)
MESSAGE_DEF(NGAP_HANDOVER_FAILURE, MESSAGE_PRIORITY_MED, ngap_handover_failure_t, ngap_handover_failure)
MESSAGE_DEF(NGAP_HANDOVER_REQUEST_ACKNOWLEDGE, MESSAGE_PRIORITY_MED, ngap_handover_request_ack_t, ngap_handover_request_ack)
MESSAGE_DEF(NGAP_HANDOVER_NOTIFY, MESSAGE_PRIORITY_MED, ngap_handover_notify_t, ngap_handover_notify)
MESSAGE_DEF(NGAP_HANDOVER_CANCEL, MESSAGE_PRIORITY_MED, ngap_handover_cancel_t, ngap_handover_cancel)
MESSAGE_DEF(NGAP_UL_RAN_STATUS_TRANSFER, MESSAGE_PRIORITY_MED, ngap_ran_status_transfer_t, ngap_ul_ran_status_transfer)
MESSAGE_DEF(NGAP_DL_RAN_STATUS_TRANSFER, MESSAGE_PRIORITY_MED, ngap_ran_status_transfer_t, ngap_dl_ran_status_transfer)
MESSAGE_DEF(NGAP_RECONNECT_TIMER, MESSAGE_PRIORITY_MED, IttiMsgText, ngap_gNB_amf_data)
MESSAGE_DEF(NGAP_PATH_SWITCH_REQ, MESSAGE_PRIORITY_MED, ngap_path_switch_req_t, ngap_path_switch_req)
MESSAGE_DEF(NGAP_PATH_SWITCH_REQ_ACK, MESSAGE_PRIORITY_MED, ngap_path_switch_req_ack_t, ngap_path_switch_req_ack)

/* NGAP -> RRC messages */
MESSAGE_DEF(NGAP_DOWNLINK_NAS              , MESSAGE_PRIORITY_MED, ngap_downlink_nas_t              , ngap_downlink_nas )
MESSAGE_DEF(NGAP_INITIAL_CONTEXT_SETUP_REQ , MESSAGE_PRIORITY_MED, ngap_initial_context_setup_req_t , ngap_initial_context_setup_req )
MESSAGE_DEF(NGAP_PAGING_IND                , MESSAGE_PRIORITY_MED, ngap_paging_ind_t                , ngap_paging_ind )
MESSAGE_DEF(NGAP_PDUSESSION_SETUP_REQ            , MESSAGE_PRIORITY_MED, ngap_pdusession_setup_req_t        , ngap_pdusession_setup_req )
MESSAGE_DEF(NGAP_PDUSESSION_MODIFY_REQ           , MESSAGE_PRIORITY_MED, ngap_pdusession_modify_req_t        , ngap_pdusession_modify_req )
MESSAGE_DEF(NGAP_PDUSESSION_RELEASE_COMMAND     , MESSAGE_PRIORITY_MED, ngap_pdusession_release_command_t     , ngap_pdusession_release_command)
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_COMMAND, MESSAGE_PRIORITY_MED, ngap_ue_release_command_t        , ngap_ue_release_command)
MESSAGE_DEF(NGAP_HANDOVER_REQUEST, MESSAGE_PRIORITY_MED, ngap_handover_request_t, ngap_handover_request)
MESSAGE_DEF(NGAP_HANDOVER_COMMAND, MESSAGE_PRIORITY_MED, ngap_handover_command_t, ngap_handover_command)
MESSAGE_DEF(NGAP_HANDOVER_CANCEL_ACK, MESSAGE_PRIORITY_MED, ngap_handover_cancel_ack_t, ngap_handover_cancel_ack)

/* NGAP <-> RRC messages (can be initiated either by MME or gNB) */
MESSAGE_DEF(NGAP_UE_CONTEXT_RELEASE_REQ    , MESSAGE_PRIORITY_MED, ngap_ue_release_req_t            , ngap_ue_release_req)

/* NGAP -> NRPPA messages*/
MESSAGE_DEF(NGAP_DOWNLINKUEASSOCIATEDNRPPA,
            MESSAGE_PRIORITY_MED,
            ngap_downlink_ue_associated_nrppa_t,
            ngap_downlink_ue_associated_nrppa)
MESSAGE_DEF(NGAP_DOWNLINKNONUEASSOCIATEDNRPPA,
            MESSAGE_PRIORITY_MED,
            ngap_downlink_non_ue_associated_nrppa_t,
            ngap_downlink_non_ue_associated_nrppa)
MESSAGE_DEF(NGAP_UPLINKUEASSOCIATEDNRPPA, MESSAGE_PRIORITY_MED, ngap_uplink_ue_associated_nrppa_t, ngap_uplink_ue_associated_nrppa)
MESSAGE_DEF(NGAP_UPLINKNONUEASSOCIATEDNRPPA,
            MESSAGE_PRIORITY_MED,
            ngap_uplink_non_ue_associated_nrppa_t,
            ngap_uplink_non_ue_associated_nrppa)
