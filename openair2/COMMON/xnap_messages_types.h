/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef XNAP_MESSAGES_TYPES_H_
#define XNAP_MESSAGES_TYPES_H_

#include "common/5g_platform_types.h"
#include "common/utils/ds/byte_array.h"
#include "common/platform_types.h"
#include "common/platform_constants.h"
#include "openair2/COMMON/sctp_messages_types.h"

#define XNAP_MAX_NB_CANDIDATES 8

#define XNAP_REGISTER_GNB_REQ(mSGpTR)     (mSGpTR)->ittiMsg.xnap_register_gnb_req
#define XNAP_SETUP_IND(mSGpTR)            (mSGpTR)->ittiMsg.xnap_setup_ind
#define XNAP_PEER_SHUTDOWN_IND(mSGpTR)    (mSGpTR)->ittiMsg.xnap_peer_shutdown_ind

typedef struct {
  // PLMN Identity (M)
  plmn_id_t plmn;
  // Number of supported S-NSSAIs
  uint16_t num_nssai;
  // List of supported S-NSSAIs (M)
  nssai_t *nssai;
} xnap_plmn_support_t;

/* TAI Support Item */
typedef struct {
  // Tracking Area Code (M)
  uint32_t tac;
  // Number of supported PLMNs
  uint8_t num_plmn;
  // List of supported PLMNs (M)
  xnap_plmn_support_t *plmn_support;
} xnap_tai_support_t;

/* 3GPP TS 38.423 9.2.3.83 AMF Region Information */
typedef struct {
  // PLMN Identity (M)
  plmn_id_t plmn;
  // AMF Region Identifier (M)
  uint8_t amf_region_id;
} xnap_amf_region_info_t;

/* 3GPP TS 38.423 9.1.3.1 – Xn Setup Request */
typedef struct {
  // Global NG-RAN Node ID (gNB_id+plmn) (M)
  uint32_t gNB_id;
  plmn_id_t plmn;
  // TAI Support List (M)
  uint16_t num_tai;
  xnap_tai_support_t *tai_support;
  // AMF Region Information (M)
  uint8_t num_amf_regions;
  xnap_amf_region_info_t *amf_region_info;
} xnap_setup_req_t;

/* 3GPP TS 38.423 9.1.3.2 – Xn Setup Response */
typedef struct {
  // Global NG-RAN Node ID (gNB_id+plmn) (M)
  uint32_t gNB_id;
  plmn_id_t plmn;
  // TAI Support List (M)
  uint16_t num_tai;
  xnap_tai_support_t *tai_support;
} xnap_setup_resp_t;

typedef struct xnap_sctp_s {
  uint16_t sctp_in_streams;
  uint16_t sctp_out_streams;
} xnap_sctp_t;

typedef struct xnap_net_config_t {
  char *gnb_xn_interface_ip_address;
  uint8_t nb_of_candidate_gNBs;
  char *candidate_gnb_address_for_xnc[XNAP_MAX_NB_CANDIDATES];
  xnap_sctp_t sctp_streams;
} xnap_net_config_t;

typedef struct xnap_register_gnb_req_s {
  xnap_setup_req_t ng_setup_info;
  xnap_net_config_t net_config;
} xnap_register_gnb_req_t;

typedef struct xnap_setup_ind_s {
  uint32_t gnb_id;
  sctp_assoc_t assoc_id;
} xnap_setup_ind_t;

typedef struct xnap_peer_shutdown_ind_s {
  uint32_t gnb_id;
} xnap_peer_shutdown_ind_t;

typedef enum xnap_cause_radio_network_e {
    XNAP_CAUSE_RADIO_NETWORK_LAYER_CELL_NOT_AVAILABLE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_HANDOVER_DESIRABLE_FOR_RADIO_REASONS,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_HANDOVER_TARGET_NOT_ALLOWED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_INVALID_AMF_SET_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_NO_RADIO_RESOURCES_AVAILABLE_IN_TARGET_CELL,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_PARTIAL_HANDOVER,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_REDUCE_LOAD_IN_SERVING_CELL,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RESOURCE_OPTIMISATION_HANDOVER,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TIME_CRITICAL_HANDOVER,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TXN_RELOCOVERALL_EXPIRY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TXN_RELOCPREP_EXPIRY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_GUAMI_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_LOCAL_NG_RAN_NODE_UE_XNAP_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_INCONSISTENT_REMOTE_NG_RAN_NODE_UE_XNAP_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_ENCRYPTION_AND_OR_INTEGRITY_PROTECTION_ALGORITHMS_NOT_SUPPORTED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_PROTECTION_ALGORITHMS_NOT_SUPPORTED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_MULTIPLE_PDU_SESSION_ID_INSTANCES,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_PDU_SESSION_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_QOS_FLOW_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_MULTIPLE_QOS_FLOW_ID_INSTANCES,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_SWITCH_OFF_ONGOING,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_NOT_SUPPORTED_5QI_VALUE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TXN_DCOVERALL_EXPIRY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TXN_DCPREP_EXPIRY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_ACTION_DESIRABLE_FOR_RADIO_REASONS,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_REDUCE_LOAD,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RESOURCE_OPTIMISATION,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TIME_CRITICAL_ACTION,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_TARGET_NOT_ALLOWED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_NO_RADIO_RESOURCES_AVAILABLE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_INVALID_QOS_COMBINATION,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_ENCRYPTION_ALGORITHMS_NOT_SUPPORTED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_PROCEDURE_CANCELLED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RRM_PURPOSE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_IMPROVE_USER_BIT_RATE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_USER_INACTIVITY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RADIO_CONNECTION_WITH_UE_LOST,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_FAILURE_IN_THE_RADIO_INTERFACE_PROCEDURE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_BEARER_OPTION_NOT_SUPPORTED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UP_INTEGRITY_PROTECTION_NOT_POSSIBLE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UP_CONFIDENTIALITY_PROTECTION_NOT_POSSIBLE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RESOURCES_NOT_AVAILABLE_FOR_THE_SLICE_S,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UE_MAX_IP_DATA_RATE_REASON,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_CP_INTEGRITY_PROTECTION_FAILURE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UP_INTEGRITY_PROTECTION_FAILURE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_SLICE_NOT_SUPPORTED_BY_NG_RAN,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_MN_MOBILITY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_SN_MOBILITY,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_COUNT_REACHES_MAX_VALUE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_OLD_NG_RAN_NODE_UE_XNAP_ID,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_PDCP_OVERLOAD,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_DRB_ID_NOT_AVAILABLE,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UNSPECIFIED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_UE_CONTEXT_ID_NOT_KNOWN,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_NON_RELOCATION_OF_CONTEXT,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_CHO_CPC_RESOURCES_TOBECHANGED,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_RSN_NOT_AVAILABLE_FOR_THE_UP,
    XNAP_CAUSE_RADIO_NETWORK_LAYER_NPN_ACCESS_DENIED
} xnap_cause_radio_network_t;

typedef enum xnap_cause_transport_layer_e {
  XNAP_CAUSE_TRANSPORT_LAYER_TRANSPORT_RESOURCE_UNAVAILABLE,
  XNAP_CAUSE_TRANSPORT_LAYER_UNSPECIFIED
} xnap_cause_transport_layer_t;

typedef enum xnap_cause_protocol_e {
  XNAP_CAUSE_PROTOCOL_TRANSFER_SYNTAX_ERROR,
  XNAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_REJECT,
  XNAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_IGNORE_AND_NOTIFY,
  XNAP_CAUSE_PROTOCOL_MESSAGE_NOT_COMPATIBLE_WITH_RECEIVER_STATE,
  XNAP_CAUSE_PROTOCOL_SEMANTIC_ERROR,
  XNAP_CAUSE_PROTOCOL_ABSTRACT_SYNTAX_ERROR_FALSELY_CONSTRUCTED,
  XNAP_CAUSE_PROTOCOL_UNSPECIFIED
} xnap_cause_protocol_t;

typedef enum xnap_cause_misc_e {
  XNAP_CAUSE_MISC_CONTROL_PROCESSING_OVERLOAD,
  XNAP_CAUSE_MISC_HARDWARE_FAILURE,
  XNAP_CAUSE_MISC_O_AND_M_INTERVENTION,
  XNAP_CAUSE_MISC_NOT_ENOUGH_USER_PLANE_PROCESSING_RESOURCES,
  XNAP_CAUSE_MISC_UNSPECIFIED
} xnap_cause_misc_t;

typedef enum xnap_cause_group_e {
  XNAP_CAUSE_NOTHING,
  XNAP_CAUSE_RADIO_NETWORK,
  XNAP_CAUSE_TRANSPORT,
  XNAP_CAUSE_PROTOCOL,
  XNAP_CAUSE_MISC,
} xnap_cause_group_t;

typedef struct xnap_cause_s {
  /* Cause group (e.g. radioNetwork, transport, protocol, misc) */
  xnap_cause_group_t type;
  /* Specific cause value within the selected cause group */
  uint8_t value;
} xnap_cause_t;

/* 3GPP TS 38.423 9.1.3.3 – Xn Setup Failure */
typedef struct {
  // Cause (M)
  xnap_cause_t cause;
} xnap_setup_failure_t;

/* 3GPP TS 38.423 9.2.2.7 – NR CGI */
typedef struct {
  // PLMN Identity (M)
  plmn_id_t plmn_id;
  // NR Cell Identity (M)
  uint64_t nrcell_id;
} xnap_ngran_cgi_t;

/*  3GPP TS 38.423 9.2.3.4 – Bit Rate */
typedef uint64_t bitrate_t;

/* 3GPP TS 38.423 9.2.3.17 – UE Aggregate Maximum Bit Rate */
typedef struct {
  bitrate_t br_ul;
  bitrate_t br_dl;
} xnap_ambr_t;

/* 3GPP TS 38.423 9.2.3.49 – UE Security Capabilities */
typedef struct {
  uint16_t nRencryption_algorithms;
  uint16_t nRintegrity_algorithms;
  uint16_t eUTRAencryption_algorithms;
  uint16_t eUTRAintegrity_algorithms;
} xnap_security_capabilities_t;

/* 3GPP TS 38.423 9.2.3.13 Packet Error Rate */
typedef struct xnap_per_s {
  // Scalar (M)
  uint8_t scalar;
  // Exponent (M)
  uint8_t exponent;
} xnap_per_t;

/* 3GPP TS 38.423 9.2.3.8 Non-Dynamic 5QI Descriptor */
typedef struct xnap_nondynamic_5qi_s {
  // 5QI (M)
  int fiveQI;
} xnap_nondynamic_5qi_t;

/* 3GPP TS 38.423 9.2.3.9 Dynamic 5QI Descriptor */
typedef struct xnap_dynamic_5qi_s {
  // QoS Priority Level (M)
  int prio;
  // Packet Delay Budget (M)
  int pdb;
  // Packet Error Rate (M)
  xnap_per_t per;
} xnap_dynamic_5qi_t;

/* 3GPP TS 38.423 9.2.3.5 QoS Flow Level QoS Parameters */
typedef struct xnap_qos_flow_param_s {
  fiveQI_t qos_type;
  union {
    // Non dynamic 5QI Descriptor (M)
    xnap_nondynamic_5qi_t nondyn;
    // Dynamic 5QI Descriptor (M)
    xnap_dynamic_5qi_t dyn;
  };
  // Allocation and Retention Priority (M)
  qos_arp_t arp;
} xnap_qos_flow_param_t;

typedef struct {
  // QoS Flow Identifier (M)
  uint8_t qfi;
  // QoS Flow Level QoS Parameters (M)
  xnap_qos_flow_param_t qos_params;
} xnap_qos_flow_tobe_setup_item_t;

/* 3GPP TS 38.423 9.2.1.1 PDU Session Resources To Be Setup Item */
typedef struct {
  // PDU Session ID (M)
  uint8_t pdusession_id;
  // S-NSSAI (M)
  nssai_t *nssai;
  // UL NG-U UP TNL Information at UPF (M)
  gtpu_tunnel_t n3_incoming;
  // PDU Session Type (M)
  pdu_session_type_t pdu_session_type;
  // QoS Flows To Be Setup List (M)
  uint8_t num_qos;
  xnap_qos_flow_tobe_setup_item_t *qos_list;
} xnap_pdusession_resources_tobe_setup_item_t;

typedef struct {
  // NG-C UE associated Signalling reference (M)
  uint64_t ngc_ue_sig_ref;
  // Signalling TNL association address at source NG-C side (M)
  transport_layer_addr_t cp_tnl_ip_source;
  // UE Security Capabilities (M)
  xnap_security_capabilities_t security_capabilities;
  // AS Security Information (M)
  uint8_t as_security_key_ranstar[32];
  long as_security_ncc;
  // UE Aggregate Maximum Bit Rate (M)
  xnap_ambr_t ue_ambr;
  // RRC Context (M)(3GPP TS 38.331 11.2.2 HandoverPreparationInformation message)
  byte_array_t rrc_context;
  // PDU Session Resources To Be Setup List (M)
  uint16_t num_pdu;
  xnap_pdusession_resources_tobe_setup_item_t *pdusession_resources_tobe_setup_list;
} xnap_ue_context_info_t;

/* Last Visited Cell Information */
typedef struct {
  // Last Visited Cell Type
  uint8_t xnap_cell_type;
  // 3GPP TS 38.413 9.3.1.97 Last Visited NG-RAN Cell Information
  byte_array_t last_visited_cell_info;
} ue_history_info_t;

/* 3GPP TS 38.423 9.1.1.1 – Handover Request */
typedef struct {
  // Source NG-RAN node UE XnAP ID reference (M)
  uint32_t s_ng_node_ue_xnap_id;
  // Cause (M)
  xnap_cause_t cause;
  // Target Cell Global ID (M)
  xnap_ngran_cgi_t target_cgi;
  // GUAMI (M)
  nr_guami_t guami;
  // UE Context Information (M)
  xnap_ue_context_info_t ue_context;
  // UE History Information (M)
  uint8_t num_last_visited_cells;
  ue_history_info_t *ue_history_info;
} xnap_handover_req_t;

/* QoS Flows Admitted Item */
typedef struct {
  // QoS Flow Identifier
  uint8_t qfi;
} xnap_qos_admitted_item_t;

/* 3GPP TS 38.423 9.2.1.2 – PDU Session Resources Admitted Item */
typedef struct {
  // PDU Session ID
  uint8_t pdusession_id;
  // QoS Flows Admitted List
  uint8_t num_qos;
  xnap_qos_admitted_item_t *qos_list;
} xnap_pdusession_admitted_item_t;

/* 3GPP TS 38.423 9.1.1.2 – Handover Request Acknowledge */
typedef struct {
  // Source NG-RAN node UE XnAP ID (M)
  uint32_t s_ng_node_ue_xnap_id;
  // Target NG-RAN node UE XnAP ID (M)
  uint32_t t_ng_node_ue_xnap_id;
  // PDU Session Resources Admitted List (M)
  uint8_t num_pdu_admitted;
  xnap_pdusession_admitted_item_t *pdusession_admitted_list;
  // Target NG-RAN node To Source NG-RAN node Transparent Container (M)
  // (3GPP TS 38.331 11.2.2 HandoverCommand message )
  byte_array_t target2source;
} xnap_handover_req_ack_t;

/* 3GPP TS 38.423 9.1.1.3 – Handover Preparation Failure */
typedef struct {
  // Source NG-RAN node UE XnAP ID (M) //
  uint32_t s_ng_node_ue_xnap_id;
  // Cause (M)
  xnap_cause_t cause;
} xnap_handover_preparation_failure_t;

/** 3GPP TS 38.423 – 9.1.1.4 SN Status Transfer 
 * COUNT value used for both UL and DL PDCP SN + HFN (12-bit or 18-bit SN) */

// Indicates PDCP SN length
typedef enum { XNAP_SN_LENGTH_12 = 0, XNAP_SN_LENGTH_18 = 1 } xnap_sn_length_t;

/* 3GPP TS 38.423 9.2.3.37 – COUNT Value for PDCP */
typedef struct {
  // PDCP Sequence Number (M)
  uint32_t pdcp_sn;
  // Hyper Frame Number (M)
  uint32_t hfn;
  // SN length
  xnap_sn_length_t sn_len;
} xnap_drb_count_value_t;

/* 3GPP TS 38.423 9.2.1.14 – DRBs Subject To Status Transfer Item */
typedef struct {
  // DRB ID (M)
  uint8_t drb_id;
  // UL COUNT value (M)
  xnap_drb_count_value_t ul_count;
  // DL COUNT value (M)
  xnap_drb_count_value_t dl_count;
} xnap_drb_status_t;

/* DRBs Subject To Status Transfer List */
typedef struct {
  // Number of DRBs in the list
  uint8_t nb_drb;
  // DRB Status List
  xnap_drb_status_t drb_status_list[MAX_DRBS_PER_UE];
} xnap_ran_status_container_t;

/* 3GPP TS 38.423 9.1.1.4 – SN Status Transfer */
typedef struct {
  // Source NG-RAN node UE XnAP ID (M)
  uint32_t s_ng_node_ue_xnap_id;
  // Target NG-RAN node UE XnAP ID (M)
  uint32_t t_ng_node_ue_xnap_id;
  // DRBs Subject To Status Transfer List (M)
  xnap_ran_status_container_t ran_status;
} xnap_sn_status_transfer_t;

/* 3GPP TS 38.423 9.1.1.5 – UE CONTEXT RELEASE */
typedef struct {
  /* Source NG-RAN node UE XnAP ID (M) */
  uint32_t s_ng_node_ue_xnap_id;
  /* Target NG-RAN node UE XnAP ID (M) */
  uint32_t t_ng_node_ue_xnap_id;
} xnap_ue_context_release_t;

/* 3GPP TS 38.423 9.1.1.6 – Handover Cancel */
typedef struct {
  /* Source NG-RAN node UE XnAP ID (M) */
  uint32_t s_ng_node_ue_xnap_id;
  /* Cause (M) */
  xnap_cause_t cause;
} xnap_handover_cancel_t;

/* 3GPP TS 38.423 9.1.1.12 – Handover Success */
typedef struct {
  /* Source NG-RAN node UE XnAP ID (M) */
  uint32_t s_ng_node_ue_xnap_id;
  /* Target NG-RAN node UE XnAP ID (M) */
  uint32_t t_ng_node_ue_xnap_id;
  /* Requested Target Cell Global ID (M) */
  xnap_ngran_cgi_t target_cgi;
} xnap_handover_success_t;

/* 3GPP TS 38.423 9.2.3.66 – Paging DRX */
typedef enum {
  XNAP_PAGING_DRX_32 = 0,
  XNAP_PAGING_DRX_64,
  XNAP_PAGING_DRX_128,
  XNAP_PAGING_DRX_256,
  XNAP_PAGING_DRX_512,
  XNAP_PAGING_DRX_1024,
} xnap_paging_drx_t;

typedef enum {
  XNAP_RAN_PAGING_AREA_CELL_LIST = 0,
  XNAP_RAN_PAGING_AREA_RAN_AREA_ID,
} xnap_ran_paging_area_choice_t;

/* RAN Area ID entry: TAC (M) + optional RANAC */
typedef struct {
  uint32_t tac;        /* 24-bit Tracking Area Code (M) */
  bool ranac_present;
  uint8_t ranac;       /* RAN Area Code 0..255 (O) */
} xnap_ran_area_id_t;

/* 3GPP TS 38.423 9.2.3.38 – RAN Paging Area */
typedef struct {
  plmn_id_t plmn;
  xnap_ran_paging_area_choice_t choice;
  union {
    struct {
      uint8_t num_cells;
      uint64_t *cell_ids; /* NR-Cell-Identity, 36-bit values */
    };
    struct {
      uint8_t num_ran_area_ids;
      xnap_ran_area_id_t *ran_area_ids;
    };
  };
} xnap_ran_paging_area_t;

/* 3GPP TS 38.423 9.2.3.44 – Paging Priority; lower value codepoint indicates higher priority */
typedef enum {
  XNAP_PAGING_PRIO_LEVEL1 = 0,
  XNAP_PAGING_PRIO_LEVEL2,
  XNAP_PAGING_PRIO_LEVEL3,
  XNAP_PAGING_PRIO_LEVEL4,
  XNAP_PAGING_PRIO_LEVEL5,
  XNAP_PAGING_PRIO_LEVEL6,
  XNAP_PAGING_PRIO_LEVEL7,
  XNAP_PAGING_PRIO_LEVEL8,
} xnap_paging_priority_t;

/* 3GPP TS 38.423 9.2.3.42 – RAN Paging Attempt Information */
typedef struct {
  /* Paging Attempt Count, 1..16 (M) */
  uint8_t paging_attempt_count;
  /* Intended Number of Paging Attempts, 1..16 (M) */
  uint8_t intended_num_paging_attempts;
  /* Next Paging Area Scope (O) */
  bool next_paging_area_scope_changed;
} xnap_ran_paging_attempt_info_t;

/* 3GPP TS 38.423 9.1.1.7 – RAN Paging */
typedef struct {
  /* UE Identity Index Value – 10-bit index (M) */
  uint16_t ue_identity_index_value;
  /* UE RAN Paging Identity – I-RNTI, 40-bit (M) */
  uint64_t ue_ran_paging_identity;
  /* Paging DRX (M) */
  xnap_paging_drx_t paging_drx;
  /* RAN Paging Area (M) */
  xnap_ran_paging_area_t ran_paging_area;
  /* Paging Priority (O) */
  xnap_paging_priority_t *paging_priority;
  /* Assistance Data for RAN Paging (O) 9.2.3.41 – RAN Paging Attempt Information (O) */
  xnap_ran_paging_attempt_info_t *ran_paging_attempt_info;
  /* UE Radio Capability for Paging (O) 9.2.3.91 – of NR (O): RRC UERadioPagingInformation, TS 38.331 */
  byte_array_t ue_radio_cap_for_paging;
} xnap_ran_paging_t;

/* 3GPP TS 38.423 9.2.3.40 – UE Context ID */
typedef enum {
  XNAP_UE_CONTEXT_ID_NOTHING = 0,
  XNAP_UE_CONTEXT_ID_RRC_RESUME,
  XNAP_UE_CONTEXT_ID_RRC_REESTABLISHMENT,
} xnap_ue_context_id_choice_t;

/* I-RNTI variant (3GPP TS 38.423 9.2.3.46): full (40-bit) or short (24-bit) */
typedef enum {
  XNAP_I_RNTI_FULL = 0,
  XNAP_I_RNTI_SHORT,
} xnap_i_rnti_type_t;

/* UE Context ID for RRC Resume */
typedef struct {
  /* I-RNTI type – full or short (M) */
  xnap_i_rnti_type_t i_rnti_type;
  /* I-RNTI value – 40-bit if full, 24-bit if short (M) */
  uint64_t i_rnti;
  /* Allocated C-RNTI – 16-bit (M) */
  uint16_t allocated_c_rnti;
  /* Access PCI – NR PCI, 0..1007 (M) */
  uint16_t access_pci;
} xnap_ue_context_id_rrc_resume_t;

/* UE Context ID for RRC Reestablishment */
typedef struct {
  /* C-RNTI – 16-bit (M) */
  uint16_t c_rnti;
  /* Failure Cell PCI – NR PCI, 0..1007 (M) */
  uint16_t failure_cell_pci;
} xnap_ue_context_id_rrc_reest_t;

typedef struct {
  xnap_ue_context_id_choice_t choice;
  union {
    xnap_ue_context_id_rrc_resume_t rrc_resume;
    xnap_ue_context_id_rrc_reest_t rrc_reest;
  };
} xnap_ue_context_id_t;

/* 3GPP TS 38.423 9.1.1.8 – Retrieve UE Context Request */
typedef struct {
  /* New NG-RAN node UE XnAP ID (M) */
  uint32_t new_ng_node_ue_xnap_id;
  /* UE Context ID (M) */
  xnap_ue_context_id_t ue_context_id;
  /* MAC-I (M) – 16-bit integrity code */
  uint16_t integrity_protection;
  /* New NG-RAN Cell Identity (M) – 36-bit NR cell */
  uint64_t new_cell_id;
} xnap_retrieve_ue_context_request_t;

/* 3GPP TS 38.423 9.1.1.9 – Retrieve UE Context Response */
typedef struct {
  /* New NG-RAN node UE XnAP ID (M) */
  uint32_t new_ng_node_ue_xnap_id;
  /* Old NG-RAN node UE XnAP ID (M) */
  uint32_t old_ng_node_ue_xnap_id;
  /* GUAMI (M) */
  nr_guami_t guami;
  /* UE Context Information – Retrieve UE Context Response (M)
   * Reuses the Handover Request UE Context Information container (the
   * mandatory sub-IEs are identical). */
  xnap_ue_context_info_t ue_context;
} xnap_retrieve_ue_context_response_t;

/* 3GPP TS 38.423 9.1.1.10 – Retrieve UE Context Failure */
typedef struct {
  /* New NG-RAN node UE XnAP ID (M) */
  uint32_t new_ng_node_ue_xnap_id;
  /* Old NG-RAN node To New NG-RAN node Resume Container (O): RRCRelease (TS 38.331) or
   * RRCConnectionRelease (TS 36.331) encapsulated in a PDCP-C PDU */
  byte_array_t old_to_new_resume_container;
  /* Cause (M) */
  xnap_cause_t cause;
} xnap_retrieve_ue_context_failure_t;

#endif /* XNAP_MESSAGES_TYPES_H_ */
