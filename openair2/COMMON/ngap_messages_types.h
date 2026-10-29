/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * ngap_messages_types.h
 */

#ifndef NGAP_MESSAGES_TYPES_H_
#define NGAP_MESSAGES_TYPES_H_
#include "common/5g_platform_types.h"
#include "common/platform_constants.h"
#include "common/platform_types.h"
#include "s1ap_messages_types.h"
#include "ds/byte_array.h"
#include "utils.h"

// Defines to access message fields.
#define NGAP_REGISTER_GNB_REQ(mSGpTR)           (mSGpTR)->ittiMsg.ngap_register_gnb_req

#define NGAP_REGISTER_GNB_CNF(mSGpTR)           (mSGpTR)->ittiMsg.ngap_register_gnb_cnf
#define NGAP_DEREGISTERED_GNB_IND(mSGpTR)       (mSGpTR)->ittiMsg.ngap_deregistered_gnb_ind

#define NGAP_NAS_FIRST_REQ(mSGpTR)              (mSGpTR)->ittiMsg.ngap_nas_first_req
#define NGAP_UPLINK_NAS(mSGpTR)                 (mSGpTR)->ittiMsg.ngap_uplink_nas
#define NGAP_UE_CAPABILITIES_IND(mSGpTR)        (mSGpTR)->ittiMsg.ngap_ue_cap_info_ind
#define NGAP_INITIAL_CONTEXT_SETUP_RESP(mSGpTR) (mSGpTR)->ittiMsg.ngap_initial_context_setup_resp
#define NGAP_INITIAL_CONTEXT_SETUP_FAIL(mSGpTR) (mSGpTR)->ittiMsg.ngap_initial_context_setup_fail
#define NGAP_NAS_NON_DELIVERY_IND(mSGpTR)       (mSGpTR)->ittiMsg.ngap_nas_non_delivery_ind
#define NGAP_UE_CTXT_MODIFICATION_RESP(mSGpTR)  (mSGpTR)->ittiMsg.ngap_ue_ctxt_modification_resp
#define NGAP_UE_CTXT_MODIFICATION_FAIL(mSGpTR)  (mSGpTR)->ittiMsg.ngap_ue_ctxt_modification_fail
#define NGAP_PDUSESSION_SETUP_RESP(mSGpTR)           (mSGpTR)->ittiMsg.ngap_pdusession_setup_resp
#define NGAP_PDUSESSION_MODIFY_RESP(mSGpTR)           (mSGpTR)->ittiMsg.ngap_pdusession_modify_resp

#define NGAP_DOWNLINK_NAS(mSGpTR)               (mSGpTR)->ittiMsg.ngap_downlink_nas
#define NGAP_INITIAL_CONTEXT_SETUP_REQ(mSGpTR)  (mSGpTR)->ittiMsg.ngap_initial_context_setup_req
#define NGAP_UE_CONTEXT_RELEASE_COMMAND(mSGpTR) (mSGpTR)->ittiMsg.ngap_ue_release_command
#define NGAP_UE_CONTEXT_RELEASE_COMPLETE(mSGpTR) (mSGpTR)->ittiMsg.ngap_ue_release_complete
#define NGAP_PDUSESSION_SETUP_REQ(mSGpTR)              (mSGpTR)->ittiMsg.ngap_pdusession_setup_req
#define NGAP_PDUSESSION_MODIFY_REQ(mSGpTR)              (mSGpTR)->ittiMsg.ngap_pdusession_modify_req
#define NGAP_PAGING_IND(mSGpTR)                 (mSGpTR)->ittiMsg.ngap_paging_ind
#define NGAP_HANDOVER_REQUIRED(mSGpTR)           (mSGpTR)->ittiMsg.ngap_handover_required
#define NGAP_HANDOVER_FAILURE(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_failure
#define NGAP_HANDOVER_REQUEST(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_request
#define NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_request_ack
#define NGAP_HANDOVER_COMMAND(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_command
#define NGAP_HANDOVER_NOTIFY(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_notify
#define NGAP_HANDOVER_CANCEL(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_cancel
#define NGAP_HANDOVER_CANCEL_ACK(mSGpTR) (mSGpTR)->ittiMsg.ngap_handover_cancel_ack
#define NGAP_PATH_SWITCH_REQ(mSGpTR) (mSGpTR)->ittiMsg.ngap_path_switch_req
#define NGAP_PATH_SWITCH_REQ_ACK(mSGpTR) (mSGpTR)->ittiMsg.ngap_path_switch_req_ack

#define NGAP_UE_CONTEXT_RELEASE_REQ(mSGpTR)     (mSGpTR)->ittiMsg.ngap_ue_release_req
#define NGAP_PDUSESSION_RELEASE_COMMAND(mSGpTR)      (mSGpTR)->ittiMsg.ngap_pdusession_release_command
#define NGAP_PDUSESSION_RELEASE_RESPONSE(mSGpTR)     (mSGpTR)->ittiMsg.ngap_pdusession_release_resp
#define NGAP_PDUSESSION_RESOURCE_NOTIFY(mSGpTR)      (mSGpTR)->ittiMsg.ngap_pdusession_resource_notify

#define NGAP_UL_RAN_STATUS_TRANSFER(mSGpTR) (mSGpTR)->ittiMsg.ngap_ul_ran_status_transfer
#define NGAP_DL_RAN_STATUS_TRANSFER(mSGpTR) (mSGpTR)->ittiMsg.ngap_dl_ran_status_transfer

#define NGAP_DOWNLINKUEASSOCIATEDNRPPA(mSGpTR) (mSGpTR)->ittiMsg.ngap_downlink_ue_associated_nrppa
#define NGAP_DOWNLINKNONUEASSOCIATEDNRPPA(mSGpTR) (mSGpTR)->ittiMsg.ngap_downlink_non_ue_associated_nrppa

#define NGAP_UPLINKUEASSOCIATEDNRPPA(mSGpTR) (mSGpTR)->ittiMsg.ngap_uplink_ue_associated_nrppa
#define NGAP_UPLINKNONUEASSOCIATEDNRPPA(mSGpTR) (mSGpTR)->ittiMsg.ngap_uplink_non_ue_associated_nrppa
//-------------------------------------------------------------------------------------------//

/* Length of the transport layer address string
 * 160 bits / 8 bits by char.
 */
#define NGAP_TRANSPORT_LAYER_ADDRESS_SIZE (160 / 8)

#define NGAP_MAX_NB_AMF_IP_ADDRESS 10

/* Security key length used within gNB
 * Even if only 16 bytes will be effectively used,
 * the key length is 32 bytes (256 bits)
 */
#define SECURITY_KEY_LENGTH 32

#define NGAP_MAX_NO_TAI_PAGING 16 // 9.2.4.1 3GPP TS 38.413

/* Paging DRX values (3GPP TS 38.413) */
#define FOREACH_PAGING_DRX(DRX_DEF) \
  DRX_DEF(NGAP_PAGING_DRX_32, 0x0)  \
  DRX_DEF(NGAP_PAGING_DRX_64, 0x1)  \
  DRX_DEF(NGAP_PAGING_DRX_128, 0x2) \
  DRX_DEF(NGAP_PAGING_DRX_256, 0x3)

/* Lower value codepoint indicates higher priority (3GPP TS 38.413) */
#define FOREACH_PAGING_PRIORITY(PRIO_DEF) \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL1, 0)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL2, 1)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL3, 2)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL4, 3)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL5, 4)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL6, 5)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL7, 6)    \
  PRIO_DEF(NGAP_PAGING_PRIO_LEVEL8, 7)

/* Paging Origin (9.3.3.22 of 3GPP TS 38.413) */
#define FOREACH_PAGING_ORIGIN(ORIGIN_DEF) ORIGIN_DEF(NGAP_PAGING_ORIGIN_NON_3GPP, 0)

static const text_info_t paging_drx_text[] = {FOREACH_PAGING_DRX(TO_TEXT)};

static const text_info_t paging_prio_text[] = {FOREACH_PAGING_PRIORITY(TO_TEXT)};

static const text_info_t paging_origin_text[] = {FOREACH_PAGING_ORIGIN(TO_TEXT)};

typedef enum { FOREACH_PAGING_DRX(TO_ENUM) } ngap_paging_drx_t;

typedef enum { FOREACH_PAGING_PRIORITY(TO_ENUM) } ngap_paging_priority_t;

typedef enum { FOREACH_PAGING_ORIGIN(TO_ENUM) } ngap_paging_origin_t;

typedef struct ngap_net_ip_address_s {
  unsigned ipv4:1;
  unsigned ipv6:1;
  char ipv4_address[16];
  char ipv6_address[46];
} ngap_net_ip_address_t;

typedef uint64_t bitrate_t;

typedef struct ngap_ambr_s {
  bitrate_t br_ul;
  bitrate_t br_dl;
} ngap_ambr_t;

typedef struct ngap_security_capabilities_s {
  uint16_t nRencryption_algorithms;
  uint16_t nRintegrity_algorithms;
  uint16_t eUTRAencryption_algorithms;
  uint16_t eUTRAintegrity_algorithms;
} ngap_security_capabilities_t;

/** @brief RRC Establishment Cause (9.3.1.111 of 3GPP TS 38.413)
 * Indicates the reason for RRC Connection Establishment/Resume as received from the UE.
 * The notAvailable value is used when re-establishing/resuming an RRC connection and the
 * cause value from the UE does not map to any other value. */
#define FOREACH_RRC_ESTABLISHMENT_CAUSE(CAUSE_DEF) \
  CAUSE_DEF(NGAP_RRC_CAUSE_EMERGENCY, 0)           \
  CAUSE_DEF(NGAP_RRC_CAUSE_HIGH_PRIO_ACCESS, 1)    \
  CAUSE_DEF(NGAP_RRC_CAUSE_MT_ACCESS, 2)           \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_SIGNALLING, 3)       \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_DATA, 4)             \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_VOICECALL, 5)        \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_VIDEOCALL, 6)        \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_SMS, 7)              \
  CAUSE_DEF(NGAP_RRC_CAUSE_MPS_PRIORITY_ACCESS, 8) \
  CAUSE_DEF(NGAP_RRC_CAUSE_MCS_PRIORITY_ACCESS, 9) \
  CAUSE_DEF(NGAP_RRC_CAUSE_NOTAVAILABLE, 10)       \
  CAUSE_DEF(NGAP_RRC_CAUSE_MO_EXCEPTION_DATA, 11)  \
  CAUSE_DEF(NGAP_RRC_CAUSE_LAST, 12)

typedef enum { FOREACH_RRC_ESTABLISHMENT_CAUSE(TO_ENUM) } ngap_rrc_establishment_cause_t;

typedef struct fiveg_s_tmsi_s {
  uint16_t amf_set_id;
  uint8_t  amf_pointer;
  uint32_t m_tmsi;
} fiveg_s_tmsi_t;

typedef enum ngap_ue_identities_presenceMask_e {
  NGAP_UE_IDENTITIES_FiveG_s_tmsi  = 1 << 1,
  NGAP_UE_IDENTITIES_guami         = 1 << 2,
} ngap_ue_identities_presenceMask_t;

typedef struct ngap_ue_identity_s {
  ngap_ue_identities_presenceMask_t presenceMask;
  fiveg_s_tmsi_t  s_tmsi;
  nr_guami_t guami;
} ngap_ue_identity_t;

typedef struct ngap_mobility_restriction_s{
  plmn_id_t serving_plmn;
}ngap_mobility_restriction_t;

/* PDU Session Resource Setup Request Transfer (9.3.4.1 3GPP TS 38.413) */
typedef struct {
  uint8_t nb_qos;
  pdusession_level_qos_parameter_t qos[MAX_QOS_FLOWS];
  pdu_session_type_t pdu_session_type;
  // UPF endpoint of the NG-U (N3) transport bearer
  gtpu_tunnel_t n3_incoming;
} pdusession_transfer_t;

typedef enum pdusession_qosflow_mapping_ind_e{
  QOSFLOW_MAPPING_INDICATION_UL = 0,
  QOSFLOW_MAPPING_INDICATION_DL = 1,
  QOSFLOW_MAPPING_INDICATION_NON = 0xFF
}pdusession_qosflow_mapping_ind_t;

typedef struct pdusession_associate_qosflow_s{
  uint8_t                           qfi;
  pdusession_qosflow_mapping_ind_t  qos_flow_mapping_ind;
} pdusession_associate_qosflow_t;

typedef struct pdusession_setup_s {
  /* Unique pdusession_id for the UE. */
  uint8_t pdusession_id;

  /* The transport layer address for the IP packets */
  uint8_t pdu_session_type;

  // NG-U (N3) Tunnel Endpoint on the RAN side
  gtpu_tunnel_t n3_outgoing;

  /* qos flow list number */
  uint8_t  nb_of_qos_flow;
  
  /* qos flow list(1 ~ 64) */
  pdusession_associate_qosflow_t associated_qos_flows[MAX_QOS_FLOWS];
} pdusession_setup_t;

/* QoS Flow Add or Modify Response Item (3GPP TS 38.413 9.2.1.6) */
typedef struct qos_flow_tobe_modified_s {
  // QoS Flow Identifier
  uint8_t qfi;
} qos_flow_addmod_response_item_t;

/* PDU Session Resource Modify Response Item (3GPP TS 38.413 9.2.1.6) */
typedef struct pdusession_modify_s {
  // PDU Session ID
  uint8_t pdusession_id;
  /* PDU Session Resource Modify Response Transfer */
  // QoS Flow Add or Modify Response List
  uint8_t nb_of_qos_flow;
  // qos_flow_add_or_modify
  qos_flow_addmod_response_item_t qos[MAX_QOS_FLOWS];
} pdusession_modify_t;

/* Cause Group (9.3.1.2 of 3GPP TS 38.413) */
#define FOREACH_CAUSE_GROUP(CAUSE_DEF)   \
  CAUSE_DEF(NGAP_CAUSE_NOTHING, 0)       \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK, 1) \
  CAUSE_DEF(NGAP_CAUSE_TRANSPORT, 2)     \
  CAUSE_DEF(NGAP_CAUSE_NAS, 3)           \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL, 4)      \
  CAUSE_DEF(NGAP_CAUSE_MISC, 5)

typedef enum { FOREACH_CAUSE_GROUP(TO_ENUM) } ngap_cause_group_t;

/* Cause (9.3.1.2 of 3GPP TS 38.413) */
typedef struct ngap_cause_s {
  ngap_cause_group_t type;
  uint8_t value;
} ngap_cause_t;

/* Radio Network Cause (9.3.1.2 of 3GPP TS 38.413) */
#define FOREACH_CAUSE_RADIO_NETWORK(CAUSE_DEF)                                                            \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UNSPECIFIED, 0)                                                      \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_TXNRELOCOVERALL_EXPIRY, 1)                                           \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_SUCCESSFUL_HANDOVER, 2)                                              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RELEASE_DUE_TO_NGRAN_GENERATED_REASON, 3)                            \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RELEASE_DUE_TO_5GC_GENERATED_REASON, 4)                              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_HANDOVER_CANCELLED, 5)                                               \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_PARTIAL_HANDOVER, 6)                                                 \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM, 7)             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_HO_TARGET_NOT_ALLOWED, 8)                                            \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_TNGRELOCOVERALL_EXPIRY, 9)                                           \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_TNGRELOCPREP_EXPIRY, 10)                                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_CELL_NOT_AVAILABLE, 11)                                              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_TARGETID, 12)                                                \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_NO_RADIO_RESOURCES_AVAILABLE_IN_TARGET_CELL, 13)                     \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_LOCAL_UE_NGAP_ID, 14)                                        \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_INCONSISTENT_REMOTE_UE_NGAP_ID, 15)                                  \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_HANDOVER_DESIRABLE_FOR_RADIO_REASON, 16)                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_TIME_CRITICAL_HANDOVER, 17)                                          \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RESOURCE_OPTIMISATION_HANDOVER, 18)                                  \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_REDUCE_LOAD_IN_SERVING_CELL, 19)                                     \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_USER_INACTIVITY, 20)                                                 \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RADIO_CONNECTION_WITH_UE_LOST, 21)                                   \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RADIO_RESOURCES_NOT_AVAILABLE, 22)                                   \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_INVALID_QOS_COMBINATION, 23)                                         \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_FAILURE_IN_RADIO_INTERFACE_PROCEDURE, 24)                            \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_INTERACTION_WITH_OTHER_PROCEDURE, 25)                                \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_PDU_SESSION_ID, 26)                                          \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_QOS_FLOW_ID, 27)                                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_MULTIPLE_PDU_SESSION_ID_INSTANCES, 28)                               \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_MULTIPLE_QOS_FLOW_ID_INSTANCES, 29)                                  \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_ENCRYPTION_AND_OR_INTEGRITY_PROTECTION_ALGORITHMS_NOT_SUPPORTED, 30) \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_NG_INTRA_SYSTEM_HANDOVER_TRIGGERED, 31)                              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_NG_INTER_SYSTEM_HANDOVER_TRIGGERED, 32)                              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_XN_HANDOVER_TRIGGERED, 33)                                           \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_NOT_SUPPORTED_5QI_VALUE, 34)                                         \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UE_CONTEXT_TRANSFER, 35)                                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_IMS_VOICE_EPS_FALLBACK_OR_RAT_FALLBACK_TRIGGERED, 36)                \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UP_INTEGRITY_PROTECTION_NOT_POSSIBLE, 37)                            \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UP_CONFIDENTIALITY_PROTECTION_NOT_POSSIBLE, 38)                      \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_SLICE_NOT_SUPPORTED, 39)                                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UE_IN_RRC_INACTIVE_STATE_NOT_REACHABLE, 40)                          \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_REDIRECTION, 41)                                                     \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RESOURCES_NOT_AVAILABLE_FOR_THE_SLICE, 42)                           \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_UE_MAX_INTEGRITY_PROTECTED_DATA_RATE_REASON, 43)                     \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RELEASE_DUE_TO_CN_DETECTED_MOBILITY, 44)                             \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_N26_INTERFACE_NOT_AVAILABLE, 45)                                     \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RELEASE_DUE_TO_PRE_EMPTION, 46)                                      \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_MULTIPLE_LOCATION_REPORTING_REFERENCE_ID_INSTANCES, 47)              \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_RSN_NOT_AVAILABLE_FOR_THE_UP, 48)                                    \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_NPN_ACCESS_DENIED, 49)                                               \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_CAG_ONLY_ACCESS_DENIED, 50)                                          \
  CAUSE_DEF(NGAP_CAUSE_RADIO_NETWORK_INSUFFICIENT_UE_CAPABILITIES, 51)

typedef enum { FOREACH_CAUSE_RADIO_NETWORK(TO_ENUM) } ngap_cause_radio_network_t;

/* Transport Cause (9.3.1.2 of 3GPP TS 38.413) */
typedef enum {
  NGAP_CAUSE_TRANSPORT_RESOURCE_UNAVAILABLE = 0,
  NGAP_CAUSE_TRANSPORT_UNSPECIFIED = 1,
} ngap_cause_transport_t;

/** NGAP protocol cause values (9.3.1.2 of 3GPP TS 38.413) */
#define FOREACH_CAUSE_PROTOCOL(CAUSE_DEF)                                  \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_TRANSFER_SYNTAX_ERROR, 0)                  \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_REJECT, 1)           \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_IGNORE, 2)           \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_MSG_NOT_COMPATIBLE_WITH_RECEIVER_STATE, 3) \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_SEMANTIC_ERROR, 4)                         \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_FCM, 5)              \
  CAUSE_DEF(NGAP_CAUSE_PROTOCOL_UNSPECIFIED, 6)

typedef enum { FOREACH_CAUSE_PROTOCOL(TO_ENUM) } ngap_cause_protocol_t;

typedef struct pdusession_failed_s {
  /* Unique pdusession_id for the UE. */
  uint8_t pdusession_id;
  /* Cause of the failure */
  ngap_cause_t cause;
} pdusession_failed_t;

typedef enum ngap_ue_ctxt_modification_present_s {
  NGAP_UE_CONTEXT_MODIFICATION_SECURITY_KEY = (1 << 0),
  NGAP_UE_CONTEXT_MODIFICATION_UE_AMBR      = (1 << 1),
  NGAP_UE_CONTEXT_MODIFICATION_UE_SECU_CAP  = (1 << 2),
} ngap_ue_ctxt_modification_present_t;

typedef enum ngap_paging_ind_present_s {
  NGAP_PAGING_IND_PAGING_DRX      = (1 << 0),
  NGAP_PAGING_IND_PAGING_PRIORITY = (1 << 1),
} ngap_paging_ind_present_t;

typedef struct {
  plmn_id_t plmn;
  uint16_t num_nssai;
  nssai_t s_nssai[8];
} ngap_plmn_t;

//-------------------------------------------------------------------------------------------//

/** @brief NG SETUP REQUEST message (9.2.6.1 of 3GPP TS 38.413)
 * This message is sent by the NG-RAN node to transfer application layer information
 * for an NG-C interface instance.
 * Direction: NG-RAN node -> AMF */
typedef struct ngap_register_gnb_req_s {
  /* Unique gNB_id to identify the gNB within EPC.
   * For macro gNB ids this field should be 20 bits long.
   * For home gNB ids this field should be 28 bits long.
   */
  uint32_t gNB_id;
  /* The type of the cell */
  enum cell_type_e cell_type;

  /* Optional name for the cell
   * NOTE: the name can be NULL (i.e no name) and will be cropped to 150
   * characters.
   */
  char *gNB_name;

  /* Tracking area code */
  uint32_t tac;

  /* Mobile Country Code
   * Mobile Network Code
   */
  uint8_t  num_plmn;
  ngap_plmn_t plmn[NR_MAX_NB_PLMNS];

  /* Default Paging DRX of the gNB as defined in TS 38.304 */
  ngap_paging_drx_t default_drx;

  /* The gNB IP address to bind */
  net_ip_address_t gnb_ip_address;

  /* Nb of AMF to connect to */
  uint8_t          nb_amf;
  /* List of AMF to connect to */
  net_ip_address_t amf_ip_address[NGAP_MAX_NB_AMF_IP_ADDRESS];
  uint8_t          broadcast_plmn_num[NGAP_MAX_NB_AMF_IP_ADDRESS];
  uint8_t broadcast_plmn_index[NGAP_MAX_NB_AMF_IP_ADDRESS][NR_MAX_NB_PLMNS];

  /* Number of SCTP streams used for a amf association */
  uint16_t sctp_in_streams;
  uint16_t sctp_out_streams;
} ngap_register_gnb_req_t;

//-------------------------------------------------------------------------------------------//
// NGAP -> gNB application layer messages

typedef struct ngap_amf_region_info_s {
  plmn_id_t plmn;
  uint8_t amf_region_id;
} ngap_amf_region_info_t;

#define NGAP_MAX_NB_AMF_REGIONS 16

typedef struct ngap_register_gnb_cnf_s {
  /* Nb of AMF connected */
  uint8_t          nb_amf;
  uint32_t gNB_id;
  uint32_t tac;
  uint8_t num_plmn;
  ngap_plmn_t plmn[NR_MAX_NB_PLMNS];
  uint8_t num_amf_regions;
  ngap_amf_region_info_t amf_region_info[NGAP_MAX_NB_AMF_REGIONS];
} ngap_register_gnb_cnf_t;

typedef struct ngap_deregistered_gnb_ind_s {
  /* Nb of AMF connected */
  uint8_t          nb_amf;
} ngap_deregistered_gnb_ind_t;

//-------------------------------------------------------------------------------------------//
// RRC -> NGAP messages

/* The NAS First Req is the first message exchanged between RRC and NGAP
 * for an UE.
 * The rnti uniquely identifies an UE within a cell. Later the gnb_ue_ngap_id
 * will be the unique identifier used between RRC and NGAP.
 */
typedef struct ngap_nas_first_req_s {
  // RAN UE NGAP ID (mandatory)
  uint32_t gNB_ue_ngap_id;
  /* PLMN: Selected PLMN Identity (optional)
   * User Location Information (mandatory) */
  plmn_id_t plmn;
  // NR Cell ID for NR CGI (mandatory)
  uint32_t nr_cell_id;
  // RRC Establishment Cause (mandatory)
  ngap_rrc_establishment_cause_t establishment_cause;
  // NAS-PDU (mandatory)
  byte_array_t nas_pdu;
  // UE identity: 5G-S-TMSI, GUAMI
  ngap_ue_identity_t ue_identity;
} ngap_nas_first_req_t;

typedef struct ngap_uplink_nas_s {
  /* Unique UE identifier within an gNB */
  uint32_t gNB_ue_ngap_id;
  /* NAS pdu */
  byte_array_t nas_pdu;
  /* UserLocationInformation (mandatory) */
  plmn_id_t plmn; // CGI and TAI
  uint32_t nr_cell_id; // CGI
  uint32_t tac; // TAI
} ngap_uplink_nas_t;

typedef struct target_cell_id_s {
  plmn_id_t plmn_identity;
  uint32_t nrCellIdentity;
} cell_id_t;

typedef struct target_ran_node_id_s {
  uint32_t targetgNBId;
  plmn_id_t plmn_identity;
  uint32_t tac;
} target_ran_node_id_t;

/* 3GPP TS 38.413 9.3.1.29 */
typedef struct {
  // QoS Flow Identifier
  uint8_t qfi;
} qosflow_info_t;

/* 3GPP TS 38.413 9.3.1.29 */
typedef struct {
  // PDU Session ID
  uint8_t pdusession_id;
  // QoS Flow Information List
  uint8_t nb_of_qos_flow;
  qosflow_info_t qos_flow_info[MAX_QOS_FLOWS];
} pdusession_resource_info_t;

/* 3GPP TS 38.413 9.3.1.97 */
typedef struct {
  // Global Cell ID
  cell_id_t id;
  // Cell type
  uint8_t type;
  // Time UE Stayed in Cell (seconds)
  uint16_t time_in_cell;
  // Cause
  ngap_cause_t *cause;
} last_visited_ngran_cell_info_t;

/* 3GPP TS 38.413 9.3.1.29 */
typedef struct {
  // RRC Container: HandoverPreparationInformation message
  byte_array_t handoverInfo;
  // Target Cell ID
  cell_id_t targetCellId;
  // PDU Session Resource Information List
  uint16_t nb_pdu_session_resource;
  pdusession_resource_info_t pdu_session_resource[NR_MAX_NB_PDU_SESSIONS];
  // UE History Information
  last_visited_ngran_cell_info_t ue_history_info;
} source_to_target_transparent_container_t;

typedef enum {
  HANDOVER_TYPE_INTRA5GS, // Intra5GS: NG-RAN node to NG-RAN node
  HANDOVER_TYPE_5GSTOEPS, // 5GStoEPS: NG-RAN node to eNB
  HANDOVER_TYPE_EPSTO5GS, // EPSto5GS: eNB to NG-RAN node
  HANDOVER_TYPE_5GSTOUTRA, // 5GStoUTRA: NG-RAN node to UTRA
} ho_type_t;

/* 3GPP TS 38.413 9.2.3.1 */
typedef struct {
  // PDU Session ID
  uint8_t pdusession_id;
} pdusession_resource_t;

/* 3GPP TS 38.413 9.2.3.1 */
typedef struct {
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // Handover Type
  ho_type_t handoverType;
  // Cause
  ngap_cause_t cause;
  // Target ID
  target_ran_node_id_t target_gnb_id;
  // PDU Session Resource List
  uint16_t nb_of_pdusessions;
  pdusession_resource_t pdusessions[NR_MAX_NB_PDU_SESSIONS];
  // Source to Target Transparent Container
  source_to_target_transparent_container_t *source2target;
} ngap_handover_required_t;

/* 3GPP TS 38.413 9.2.3.6 */
typedef struct {
  // AMF UE NGAP ID (M)
  uint64_t amf_ue_ngap_id;
  // Cause (M)
  ngap_cause_t cause;
} ngap_handover_failure_t;

typedef struct {
  // Next-Hop NH
  uint8_t next_hop[SECURITY_KEY_LENGTH];
  // Next Hop Chaining Count
  uint8_t next_hop_chain_count;
} ngap_security_context_t;

/* Handover Request (3GPP TS 38.413 9.2.3.4)
  PDU Session Resource Setup Item */
typedef struct {
  // PDU Session ID
  uint8_t pdusession_id;
  pdu_session_type_t pdu_session_type;
  // S-NSSAI
  nssai_t nssai;
  // Handover Required Transfer
  pdusession_transfer_t pdusessionTransfer;
} ho_request_pdusession_t;

/* 3GPP TS 38.413 9.2.3.4 */
typedef struct {
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // Handover Type
  ho_type_t ho_type;
  // Cause
  ngap_cause_t cause;
  // UE Aggregate Maximum Bit Rate
  ngap_ambr_t ue_ambr;
  // UE Security Capabilities
  ngap_security_capabilities_t security_capabilities;
  // Security Context
  ngap_security_context_t security_context;
  // PDU Session Resource Setup List
  uint16_t nb_of_pdusessions;
  ho_request_pdusession_t pduSessionResourceSetupList[NR_MAX_NB_PDU_SESSIONS];
  // Allowed NSSAI
  uint8_t nb_allowed_nssais;
  nssai_t allowed_nssai[8];
  // Source to Target Transparent Container contents
  uint64_t nr_cell_id;
  byte_array_t ue_ho_prep_info;
  byte_array_t ue_cap;
  // Mobility Restriction List
  ngap_mobility_restriction_t *mobility_restriction;
  // GUAMI
  nr_guami_t guami;
} ngap_handover_request_t;

/* 9.3.4.11 3GPP TS 38.413 */
typedef struct {
  // QoS Flow Setup Response List
  uint8_t nb_of_qos_flow;
  pdusession_associate_qosflow_t qos_setup_list[MAX_QOS_FLOWS];
  // DL NG-U UP TNL Information
  uint32_t gtp_teid;
  transport_layer_addr_t gNB_addr;
} ho_request_ack_transfer_t;

/* 9.2.3.5 3GPP TS 38.413 */
typedef struct {
  // PDU Session ID (M)
  uint8_t pdu_session_id;
  // Handover Request Acknowledge Transfer (M)
  ho_request_ack_transfer_t ack_transfer;
} pdu_session_resource_admitted_t;

typedef struct {
  // AMF UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // RAN UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // PDU Session Resource Admitted List
  pdu_session_resource_admitted_t pdusessions[NR_MAX_NB_PDU_SESSIONS];
  uint16_t nb_of_pdusessions;
  // Target to Source Transparent Container
  byte_array_t target2source;
} ngap_handover_request_ack_t;

/* 9.3.4.10 3GPP TS 38.413 */
typedef struct {
  // QoS Flow to be Forwarded List
  uint8_t nb_of_qos_flow;
  pdusession_associate_qosflow_t qos_setup_list[MAX_QOS_FLOWS];
  // UL Forwarding UP TNL Information
  uint32_t gtp_teid;
  transport_layer_addr_t gNB_addr;
} ho_command_transfer_t;

typedef struct {
  // PDU Session ID (M)
  uint8_t pdusession_id;
  // Handover Command Transfer (M)
  ho_command_transfer_t ho_command_transfer;
} pdusession_resource_handover_t;

typedef struct {
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // Handover Type
  ho_type_t handoverType;
  // PDU Session Resource Handover List
  uint16_t nb_of_pdusessions;
  pdusession_resource_handover_t pdu_sessions[NR_MAX_NB_PDU_SESSIONS];
  // Target to Source Transparent Container
  byte_array_t handoverCommand;
} ngap_handover_command_t;

/* 9.3.1.16 3GPP TS 38.413 */
typedef struct {
  // NR user location information
  target_ran_node_id_t target_ng_ran;
  uint32_t nrCellIdentity;
} user_location_information_t;

/* 9.2.3.7 3GPP TS 38.413 */
typedef struct {
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // User Location Information
  user_location_information_t user_info;
} ngap_handover_notify_t;

/* 9.2.3.11 3GPP TS 38.413 */
typedef struct {
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // Cause
  ngap_cause_t cause;
} ngap_handover_cancel_t;

/* 9.2.3.12 3GPP TS 38.413 */
typedef struct {
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
} ngap_handover_cancel_ack_t;

/* Path Switch Request 9.2.3.8 3GPP TS 38.413 */
typedef struct ngap_path_switch_req_s {
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // Source AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // User Location Information
  user_location_information_t user_info;
  // UE Security Capabilities
  ngap_security_capabilities_t security_capabilities;
  // Number of pdusession to be switched in the downlink list
  uint16_t nb_of_pdusessions;
  // List of PDU Session Resource to be Switched in Downlink
  pdusession_setup_t pdusessions_tobeswitched[NR_MAX_NB_PDU_SESSIONS];
} ngap_path_switch_req_t;

typedef enum ngap_security_ind_s {
  NGAP_SECURITY_REQUIRED = 0,
  NGAP_SECURITY_PREFERRED = 1,
  NGAP_SECURITY_NOT_NEEDED = 2,
} ngap_security_ind_t;

/* 9.3.1.27 3GPP TS 38.413 */
typedef struct security_ind_s {
  ngap_security_ind_t integrity_protection_ind;
  ngap_security_ind_t confidentiality_protection_ind;
} security_ind_t;

/* 9.3.4.9 3GPP TS 38.413 */
typedef struct path_switch_request_ack_transfer_s {
  // UL NG-U UP TNL Information (O)
  gtpu_tunnel_t *n3_incoming;
  // Security Indication (O)
  security_ind_t *security_ind;
} path_switch_request_ack_transfer_t;

/* Path Switch Request Acknowledge 9.2.3.9 3GPP TS 38.413
 * PDU Session Resource Switched Item */
typedef struct path_switch_request_ack_pdusession_s {
  // PDU Session ID (M)
  int pdusession_id;
  // Path Switch Request Acknowledge Transfer (M)
  path_switch_request_ack_transfer_t pathSwitchReqAckTransfer;
} path_switch_request_ack_pdusession_t;

/* Path Switch Request Acknowledge 9.2.3.9 3GPP TS 38.413 */
typedef struct ngap_path_switch_req_ack_s {
  // AMF UE NGAP ID (M)
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID (M)
  uint32_t gNB_ue_ngap_id;
  // Security Context - Next-Hop Chaining Count (M)
  uint8_t nh_ncc;
  // Security Context - Next-Hop NH (M)
  uint8_t next_security_key[SECURITY_KEY_LENGTH];
  // List of PDU Session Resource Switched (M)
  uint16_t nb_of_pdusessions;
  path_switch_request_ack_pdusession_t pdusessions_switched[NR_MAX_NB_PDU_SESSIONS];
  // Allowed NSSAI (M)
  uint8_t nb_allowed_nssais;
  nssai_t allowed_nssai[NR_MAX_NB_ALLOWED_SNSSAI];
} ngap_path_switch_req_ack_t;

typedef struct ngap_ue_cap_info_ind_s {
  uint32_t  gNB_ue_ngap_id;
  byte_array_t ue_radio_cap;
} ngap_ue_cap_info_ind_t;

typedef struct ngap_initial_context_setup_resp_s {
  uint32_t  gNB_ue_ngap_id;

  /* Number of pdusession setup-ed in the list */
  uint16_t nb_of_pdusessions;
  /* list of pdusession setup-ed by RRC layers */
  pdusession_setup_t pdusessions[NR_MAX_NB_PDU_SESSIONS];

  /* Number of pdusession failed to be setup in list */
  uint16_t nb_of_pdusessions_failed;
  /* list of pdusessions that failed to be setup */
  pdusession_failed_t pdusessions_failed[NR_MAX_NB_PDU_SESSIONS];
} ngap_initial_context_setup_resp_t;

typedef struct ngap_initial_context_setup_fail_s {
  uint32_t gNB_ue_ngap_id;

  uint64_t amf_ue_ngap_id;

  ngap_cause_t cause;
} ngap_initial_context_setup_fail_t, ngap_ue_ctxt_modification_fail_t;

typedef struct ngap_nas_non_delivery_ind_s {
  uint32_t     gNB_ue_ngap_id;
  byte_array_t nas_pdu;
  /* TODO: add cause */
} ngap_nas_non_delivery_ind_t;

typedef struct ngap_ue_ctxt_modification_req_s {
  uint32_t  gNB_ue_ngap_id;

  /* Bit-mask of possible present parameters */
  ngap_ue_ctxt_modification_present_t present;

  /* Following fields are optionnaly present */

  /* Security key */
  uint8_t security_key[SECURITY_KEY_LENGTH];

  /* UE aggregate maximum bitrate */
  ngap_ambr_t ue_ambr;

  /* NR Security capabilities */
  ngap_security_capabilities_t security_capabilities;
} ngap_ue_ctxt_modification_req_t;

typedef struct ngap_ue_ctxt_modification_resp_s {
  uint32_t  gNB_ue_ngap_id;
} ngap_ue_ctxt_modification_resp_t;

//-------------------------------------------------------------------------------------------//
// NGAP -> RRC messages
typedef struct ngap_downlink_nas_s {
  /* UE id for initial connection to NGAP */
  uint32_t gNB_ue_ngap_id;
  /* UE id at AMF */
  uint64_t amf_ue_ngap_id;
  /* NAS pdu */
  byte_array_t nas_pdu;
} ngap_downlink_nas_t;

/* PDU Session Resource Setup/Modify Request Item */
typedef struct {
  int pdusession_id;
  byte_array_t nas_pdu;
  nssai_t nssai;
  pdusession_transfer_t pdusessionTransfer;
} pdusession_resource_item_t;

typedef struct ngap_initial_context_setup_req_s {
  /* UE id for initial connection to NGAP */
  uint32_t gNB_ue_ngap_id;

  uint64_t amf_ue_ngap_id;

  /* UE aggregate maximum bitrate */
  bool has_ue_ambr;
  ngap_ambr_t ue_ambr;

  /* guami */
  nr_guami_t guami;

  /* allowed nssai */
  uint8_t nb_allowed_nssais;
  nssai_t allowed_nssai[8];

  /* Security algorithms */
  ngap_security_capabilities_t security_capabilities;

  /* Security key */
  uint8_t security_key[SECURITY_KEY_LENGTH];

  /* Number of pdusession to be setup in the list */
  uint16_t nb_of_pdusessions;
  // PDU Session Resource Setup Request List
  pdusession_resource_item_t pdusession[NR_MAX_NB_PDU_SESSIONS];

  /* Mobility Restriction List */
  uint8_t                        mobility_restriction_flag;
  ngap_mobility_restriction_t    mobility_restriction;

  /* Nas Pdu */
  uint8_t                        nas_pdu_flag;
  byte_array_t nas_pdu;
} ngap_initial_context_setup_req_t;

typedef struct ngap_pdusession_setup_req_s {
  /* UE id for initial connection to NGAP */
  uint32_t gNB_ue_ngap_id;

  /* AMF UE id  */
  uint64_t amf_ue_ngap_id;

  /* S-NSSAI */
  // Fixme: illogical, nssai is part of each pdu session
  nssai_t allowed_nssai[8];

  /* Number of pdusession to be setup in the list */
  uint16_t nb_pdusessions_tosetup;

  // PDU Session Resource Setup Request List
  pdusession_resource_item_t pdusession[NR_MAX_NB_PDU_SESSIONS];

  /* UE Aggregated Max Bitrates */
  bool has_ue_ambr;
  ngap_ambr_t ueAggMaxBitRate;

} ngap_pdusession_setup_req_t;

typedef struct ngap_pdusession_setup_resp_s {
  uint32_t gNB_ue_ngap_id;
  /* Number of pdusession setup-ed in the list */
  uint16_t nb_of_pdusessions;
  /* list of pdusession setup-ed by RRC layers */
  pdusession_setup_t pdusessions[NR_MAX_NB_PDU_SESSIONS];

  /* Number of pdusession failed to be setup in list */
  uint16_t nb_of_pdusessions_failed;
  /* list of pdusessions that failed to be setup */
  pdusession_failed_t pdusessions_failed[NR_MAX_NB_PDU_SESSIONS];
} ngap_pdusession_setup_resp_t;

// NGAP --> RRC messages
typedef struct ngap_ue_release_command_s {

  uint32_t  gNB_ue_ngap_id;

} ngap_ue_release_command_t;


//-------------------------------------------------------------------------------------------//
// NGAP <-- RRC messages
typedef struct ngap_ue_release_req_s {
  // RAN UE NGAP ID (mandatory)
  uint32_t gNB_ue_ngap_id;
  // PDU Session Resource List (optional)
  uint16_t nb_of_pdusessions;
  uint8_t pdusession_ids[NR_MAX_NB_PDU_SESSIONS];
  // Cause (mandatory)
  ngap_cause_t cause;
} ngap_ue_release_req_t;

typedef struct {
  // RAN UE NGAP ID (mandatory)
  uint32_t gNB_ue_ngap_id;
  // PDU Session Resource List (optional)
  uint16_t num_pdu_sessions;
  // PDU Session ID (mandatory)
  uint8_t pdu_session_id[NR_MAX_NB_PDU_SESSIONS];
} ngap_ue_release_complete_t;

/* QoS Flow to Release Item (9.3.1.13 3GPP TS 38.413) */
typedef struct qos_flow_to_release_s {
  uint8_t qfi;
  ngap_cause_t cause;
} qos_flow_to_release_t;

/* PDU Session Resource Modify Request Transfer (9.3.4.3 3GPP TS 38.413) */
typedef struct {
  // QoS Flow Add or Modify Request List (Mandatory)
  uint8_t nb_qos_to_add_modify;
  pdusession_level_qos_parameter_t qos_to_add_modify[MAX_QOS_FLOWS];
  // QoS Flow to Release List (Optional)
  uint8_t nb_qos_to_release;
  qos_flow_to_release_t qos_to_release[MAX_QOS_FLOWS];
} pdusession_mod_req_transfer_t;

/* PDU Session Resource Setup/Modify Request Item */
typedef struct {
  // PDU Session ID (Mandatory)
  int pdusession_id;
  // NAS PDU (Optional)
  byte_array_t nas_pdu;
  // S-NSSAI (Optional)
  nssai_t nssai;
  // PDU Session Resource Modify Request Transfer (Mandatory)
  pdusession_mod_req_transfer_t pdusessionTransfer;
} pdusession_resource_mod_item_t;

typedef struct ngap_pdusession_modify_req_s {
  /* AMF UE NGAP ID (Mandatory) */
  uint64_t amf_ue_ngap_id;
  /* RAN UE NGAP ID (Mandatory) */
  uint32_t  gNB_ue_ngap_id;
  /* PDU Session Resource Modify Request List (Mandatory) */
  uint16_t nb_pdusessions_tomodify;
  pdusession_resource_mod_item_t pdusession[NR_MAX_NB_PDU_SESSIONS];
} ngap_pdusession_modify_req_t;

/* 9.2.1.6 of 3GPP TS 38.413 */
typedef struct ngap_pdusession_modify_resp_s {
  // RAN UE NGAP ID
  uint32_t  gNB_ue_ngap_id;
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // PDU Session Resource Modify Response List (0..256)
  uint16_t nb_of_pdusessions;
  pdusession_modify_t pdusessions[NR_MAX_NB_PDU_SESSIONS];
  // PDU Session Resource Failed to Modify List (0..256)
  uint16_t nb_of_pdusessions_failed;
  pdusession_failed_t pdusessions_failed[NR_MAX_NB_PDU_SESSIONS];
} ngap_pdusession_modify_resp_t;

typedef struct ngap_pdusession_release_command_s {
  /* AMF UE id  */
  uint64_t amf_ue_ngap_id;

  /* gNB ue ngap id as initialized by NGAP layer */
  uint32_t                       gNB_ue_ngap_id;

  /* The NAS PDU should be forwarded by the RRC layer to the NAS layer */
  byte_array_t nas_pdu;

  // PDU Session Resource to Release List (mandatory)
  uint16_t nb_pdusessions_torelease;
  uint16_t pdusession_ids[NR_MAX_NB_PDU_SESSIONS];

} ngap_pdusession_release_command_t;

typedef struct pdusession_release_s {
  // PDU Session ID (mandatory)
  uint8_t pdusession_id;
  // PDU Session Resource Release Response Transfer (mandatory)
  byte_array_t pdusession_release_response_transfer;
} pdusession_release_t;

typedef struct ngap_pdusession_release_resp_s {
  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID
  uint32_t gNB_ue_ngap_id;
  // PDU Session Resource Released List
  uint16_t nb_of_pdusessions_released;
  pdusession_release_t pdusession_release[NR_MAX_NB_PDU_SESSIONS];
} ngap_pdusession_release_resp_t;

typedef struct ngap_pdusession_notify_item_s {
  uint8_t pdu_session_id;
  ngap_cause_t cause;
} ngap_pdusession_notify_item_t;

/** NGAP PDU Session Resource Notify (8.2.4 of 3GPP TS 38.413) */
typedef struct ngap_pdusession_resource_notify_s {
  uint32_t gNB_ue_ngap_id;
  int nb_pdu_sessions_released;
  ngap_pdusession_notify_item_t pdu_sessions[NR_MAX_NB_PDU_SESSIONS];
} ngap_pdusession_resource_notify_t;

/** NG PAGING PROCEDURES (9.2.4. of 3GPP TS 38.413) */

typedef struct {
  plmn_id_t plmn;
  uint16_t tac;
} nr_tai_t;

typedef struct ngap_ue_paging_identity_s {
  fiveg_s_tmsi_t s_tmsi;
} ngap_ue_paging_identity_t;

/** 9.3.1.72 Paging Attempt Information (3GPP TS 38.413) */
typedef struct {
  /* Paging Attempt Count */
  uint8_t paging_attempt_count;
  /* Intended Number of Paging Attempts */
  uint8_t intended_paging_attempts;
} ngap_paging_attempt_info_t;

/** 9.2.4.1 3GPP TS 38.413 */
typedef struct {
  /* UE paging identity */
  ngap_ue_paging_identity_t ue_paging_identity;

  /* TAI List for Paging */
  nr_tai_t tai_list[NGAP_MAX_NO_TAI_PAGING];
  int16_t n_tai;

  /* Optional fields */
  ngap_paging_drx_t *paging_drx;
  ngap_paging_priority_t *paging_priority;
  /* UE Radio Capability for Paging (optional) */
  byte_array_t *ue_radio_capability;
  /* Paging Origin (optional) */
  ngap_paging_origin_t *origin;
  /* Assistance Data for Paging (optional) */
  ngap_paging_attempt_info_t *paging_attempt_info;
} ngap_paging_ind_t;

/** 9.2.3.14 Uplink RAN Status Transfer (3GPP TS 38.413)
 * COUNT value used for both UL and DL PDCP SN + HFN (12-bit or 18-bit SN) */

// Indicates PDCP SN length
typedef enum { NGAP_SN_LENGTH_12 = 0, NGAP_SN_LENGTH_18 = 1 } ngap_sn_length_t;

typedef struct {
  // PDCP Sequence Number
  uint32_t pdcp_sn;
  // Hyper Frame Number
  uint32_t hfn;
  // SN length
  ngap_sn_length_t sn_len;
} ngap_drb_count_value_t;

// DRBs Subject to Status Transfer Item
typedef struct {
  // DRB ID
  uint8_t drb_id;
  // UL COUNT value
  ngap_drb_count_value_t ul_count;
  // DL COUNT value
  ngap_drb_count_value_t dl_count;
} ngap_drb_status_t;

// RAN Status Transfer Transparent Container (9.3.1.108)
typedef struct {
  // Number of DRBs in the list
  uint8_t nb_drb;
  // DRB Status List
  ngap_drb_status_t drb_status_list[MAX_DRBS_PER_UE];
} ngap_ran_status_container_t;

// Uplink RAN Status Transfer message (9.2.3.14)
typedef struct {
  // AMF UE NGAP ID (Mandatory)
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID (Mandatory)
  uint32_t gnb_ue_ngap_id;
  // RAN Status Transfer Transparent Container (Mandatory)
  ngap_ran_status_container_t ran_status;
} ngap_ran_status_transfer_t;

// Uplink UE Associated NRPPA Transport message (9.2.9.2 3GPP 38.413 v16.0.0)
typedef struct {
  // AMF UE NGAP ID (Mandatory)
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID (Mandatory)
  uint32_t gNB_ue_ngap_id;
  // Routing ID (Mandatory)
  byte_array_t routing_id;
  // NRPPa pdu (Mandatory)
  byte_array_t nrppa_pdu;
} ngap_uplink_ue_associated_nrppa_t;

// Uplink NON UE Associated NRPPA Transport message (9.2.9.4 3GPP 38.413 v16.0.0)
typedef struct {
  // Routing ID (Mandatory)
  byte_array_t routing_id;
  // NRPPa pdu (Mandatory)
  byte_array_t nrppa_pdu;
} ngap_uplink_non_ue_associated_nrppa_t;

// Downlink UE Associated NRPPA Transport message (9.2.9.1 3GPP 38.413 v16.0.0)
typedef struct {
  // AMF UE NGAP ID (Mandatory)
  uint64_t amf_ue_ngap_id;
  // RAN UE NGAP ID (Mandatory)
  uint32_t gNB_ue_ngap_id;
  // Routing ID (Mandatory)
  byte_array_t routing_id;
  // NRPPa pdu (Mandatory)
  byte_array_t nrppa_pdu;
} ngap_downlink_ue_associated_nrppa_t;

// Downlink NON UE Associated NRPPA Transport message (9.2.9.3 3GPP 38.413 v16.0.0)
typedef struct {
  // Routing ID (Mandatory)
  byte_array_t routing_id;
  // NRPPa pdu (Mandatory)
  byte_array_t nrppa_pdu;
} ngap_downlink_non_ue_associated_nrppa_t;

#endif /* NGAP_MESSAGES_TYPES_H_ */
