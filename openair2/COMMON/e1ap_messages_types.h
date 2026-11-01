/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef E1AP_MESSAGES_TYPES_H
#define E1AP_MESSAGES_TYPES_H

#include <netinet/in.h>
#include <netinet/sctp.h>
#include "common/ngran_types.h"
#include "common/platform_types.h"
#include "common/5g_platform_types.h"
#include "common/platform_constants.h"

/* Definitions according to 3GPP TS 38.463 */
#define E1AP_MAX_NUM_TRANSAC_IDS 4
#define E1AP_MAX_NUM_PLMNS 4
#define E1AP_MAX_NUM_CELL_GROUPS 4
#define E1AP_MAX_NUM_QOS_FLOWS 64
#define E1AP_MAX_NUM_DRBS 32
#define E1AP_MAX_NUM_UP_PARAM 4
#define E1AP_SECURITY_KEY_SIZE 16 // keys have 128 bits length
#define E1AP_MAX_TL_ADDRESSES 16
#define E1AP_MAX_GTP_TL_ADDRESSES 16
#define E1AP_MAX_NUM_ERRORS 256

#define E1AP_REGISTER_REQ(mSGpTR)                         (mSGpTR)->ittiMsg.e1ap_register_req
#define E1AP_SETUP_REQ(mSGpTR)                            (mSGpTR)->ittiMsg.e1ap_setup_req
#define E1AP_SETUP_RESP(mSGpTR)                           (mSGpTR)->ittiMsg.e1ap_setup_resp
#define E1AP_SETUP_FAIL(mSGpTR)                           (mSGpTR)->ittiMsg.e1ap_setup_fail
#define E1AP_BEARER_CONTEXT_SETUP_REQ(mSGpTR)             (mSGpTR)->ittiMsg.e1ap_bearer_setup_req
#define E1AP_BEARER_CONTEXT_SETUP_RESP(mSGpTR)            (mSGpTR)->ittiMsg.e1ap_bearer_setup_resp
#define E1AP_BEARER_CONTEXT_SETUP_FAILURE(mSGpTR)         (mSGpTR)->ittiMsg.e1ap_bearer_setup_fail
#define E1AP_BEARER_CONTEXT_MODIFICATION_REQ(mSGpTR)      (mSGpTR)->ittiMsg.e1ap_bearer_mod_req
#define E1AP_BEARER_CONTEXT_MODIFICATION_RESP(mSGpTR)     (mSGpTR)->ittiMsg.e1ap_bearer_modif_resp
#define E1AP_BEARER_CONTEXT_MODIFICATION_FAIL(mSGpTR)     (mSGpTR)->ittiMsg.e1ap_bearer_modif_fail
#define E1AP_BEARER_CONTEXT_MODIFICATION_REQUIRED(mSGpTR) (mSGpTR)->ittiMsg.e1ap_bearer_mod_required
#define E1AP_BEARER_CONTEXT_MODIFICATION_CONFIRM(mSGpTR)  (mSGpTR)->ittiMsg.e1ap_bearer_mod_confirm
#define E1AP_BEARER_CONTEXT_RELEASE_CMD(mSGpTR)           (mSGpTR)->ittiMsg.e1ap_bearer_release_cmd
#define E1AP_BEARER_CONTEXT_RELEASE_CPLT(mSGpTR)          (mSGpTR)->ittiMsg.e1ap_bearer_release_cplt
#define E1AP_LOST_CONNECTION(mSGpTR)                      (mSGpTR)->ittiMsg.e1ap_lost_connection

typedef net_ip_address_t e1ap_net_ip_address_t;

typedef enum {
    E1AP_RADIO_CAUSE_UNSPECIFIED = 0,
    E1AP_RADIO_CAUSE_UNKNOWN_ALREADY_ALLOCATED_GNB_CU_CP_UE_E1AP_ID,
    E1AP_RADIO_CAUSE_UNKNOWN_ALREADY_ALLOCATED_GNB_CU_UP_UE_E1AP_ID,
    E1AP_RADIO_CAUSE_UNKNOWN_INCONSISTENT_PAIR_UE_E1AP_ID,
    E1AP_RADIO_CAUSE_INTERACTION_WITH_OTHER_PROCEDURE,
    E1AP_RADIO_CAUSE_PDCP_COUNT_WRAP_AROUND,
    E1AP_RADIO_CAUSE_UNSUPPORTED_QCI_VALUE,
    E1AP_RADIO_CAUSE_UNSUPPORTED_5QI_VALUE,
    E1AP_RADIO_CAUSE_ENCRYPTION_ALGORITHMS_NOT_SUPPORTED,
    E1AP_RADIO_CAUSE_INTEGRITY_PROTECTION_ALGORITHMS_NOT_SUPPORTED,
    E1AP_RADIO_CAUSE_UP_INTEGRITY_PROTECTION_NOT_POSSIBLE,
    E1AP_RADIO_CAUSE_UP_CONFIDENTIALITY_PROTECTION_NOT_POSSIBLE,
    E1AP_RADIO_CAUSE_MULTIPLE_PDU_SESSION_ID_INSTANCES,
    E1AP_RADIO_CAUSE_UNKNOWN_PDU_SESSION_ID,
    E1AP_RADIO_CAUSE_MULTIPLE_QOS_FLOW_ID_INSTANCES,
    E1AP_RADIO_CAUSE_UNKNOWN_QOS_FLOW_ID,
    E1AP_RADIO_CAUSE_MULTIPLE_DRB_ID_INSTANCES,
    E1AP_RADIO_CAUSE_UNKNOWN_DRB_ID,
    E1AP_RADIO_CAUSE_INVALID_QOS_COMBINATION,
    E1AP_RADIO_CAUSE_PROCEDURE_CANCELLED,
    E1AP_RADIO_CAUSE_NORMAL_RELEASE,
    E1AP_RADIO_CAUSE_NO_RADIO_RESOURCES_AVAILABLE,
    E1AP_RADIO_CAUSE_ACTION_DESIRABLE_FOR_RADIO_REASONS,
    E1AP_RADIO_CAUSE_RESOURCES_NOT_AVAILABLE_FOR_SLICE,
    E1AP_RADIO_CAUSE_PDCP_CONFIG_NOT_SUPPORTED,
    E1AP_RADIO_CAUSE_UE_DL_MAX_INTEGRITY_PROTECTED_DATA_RATE_REASON,
    E1AP_RADIO_CAUSE_UP_INTEGRITY_PROTECTION_FAILURE,
    E1AP_RADIO_CAUSE_RELEASE_DUE_TO_PREEMPTION,
    E1AP_RADIO_CAUSE_RSN_NOT_AVAILABLE_FOR_UP,
    E1AP_RADIO_CAUSE_NPN_NOT_SUPPORTED,
    E1AP_RADIO_CAUSE_OTHER
} e1ap_cause_radio_t;

typedef enum {
    E1AP_TRANSPORT_CAUSE_UNSPECIFIED = 0,
    E1AP_TRANSPORT_CAUSE_RESOURCE_UNAVAILABLE,
    E1AP_TRANSPORT_CAUSE_UNKNOWN_TNL_ADDRESS_FOR_IAB,
    E1AP_TRANSPORT_CAUSE_OTHER
} e1ap_cause_transport_t;

typedef enum {
    E1AP_PROTOCOL_CAUSE_TRANSFER_SYNTAX_ERROR = 0,
    E1AP_PROTOCOL_CAUSE_ABSTRACT_SYNTAX_ERROR_REJECT,
    E1AP_PROTOCOL_CAUSE_ABSTRACT_SYNTAX_ERROR_IGNORE_NOTIFY,
    E1AP_PROTOCOL_CAUSE_MESSAGE_NOT_COMPATIBLE_WITH_RECEIVER_STATE,
    E1AP_PROTOCOL_CAUSE_SEMANTIC_ERROR,
    E1AP_PROTOCOL_CAUSE_ABSTRACT_SYNTAX_ERROR_FALSELY_CONSTRUCTED_MESSAGE,
    E1AP_PROTOCOL_CAUSE_UNSPECIFIED,
    E1AP_PROTOCOL_CAUSE_OTHER
} e1ap_cause_protocol_t;

typedef enum {
    E1AP_MISC_CAUSE_CONTROL_PROCESSING_OVERLOAD,
    E1AP_MISC_CAUSE_NOT_ENOUGH_USER_PLANE_PROCESSING_RESOURCES,
    E1AP_MISC_CAUSE_HARDWARE_FAILURE,
    E1AP_MISC_CAUSE_OM_INTERVENTION,
    E1AP_MISC_CAUSE_UNSPECIFIED,
    E1AP_MISC_CAUSE_OTHER
} e1ap_cause_misc_t;

typedef enum e1ap_cause_group_e {
  E1AP_CAUSE_NOTHING,
  E1AP_CAUSE_RADIO_NETWORK,
  E1AP_CAUSE_TRANSPORT,
  E1AP_CAUSE_PROTOCOL,
  E1AP_CAUSE_MISC
} e1ap_cause_group_t;

typedef struct e1ap_cause_s {
  e1ap_cause_group_t type;
  uint8_t value;
} e1ap_cause_t;

typedef enum BEARER_CONTEXT_STATUS_e {
  BEARER_ACTIVE = 0,
  BEARER_SUSPEND,
  BEARER_RESUME,
} BEARER_CONTEXT_STATUS_t;

typedef enum activity_notification_level_e {
  activity_notification_level_drb = 0,
  activity_notification_level_pdu_session = 1,
  activity_notification_level_ue = 2,
} activity_notification_level_t;

typedef enum cell_group_id_e {
  MCG = 0,
  SCG,
} cell_group_id_t;

typedef enum CN_Support_e {
  cn_support_EPC = 0,
  cn_support_5GC,
} cn_Support_t;

typedef struct PLMN_ID_s {
  int mcc;
  int mnc;
  int mnc_digit_length;
} PLMN_ID_t;

typedef enum { CRITICALITY_REJECT = 0, CRITICALITY_IGNORE, CRITICALITY_NOTIFY } criticality_t;

typedef enum {
  ERROR_TYPE_NOT_UNDERSTOOD = 0,
  ERROR_TYPE_MISSING,
} error_type_t;

typedef enum {
  TRIGGERING_MSG_INITIATING = 0,
  TRIGGERING_MSG_SUCCESSFUL_OUTCOME,
  TRIGGERING_MSG_UNSUCCESSFUL_OUTCOME
} triggering_msg_t;

typedef struct criticality_diagnostics_ie_s {
  criticality_t criticality;
  int ie_id;
  error_type_t error_type;
} criticality_diagnostics_ie_t;

typedef struct criticality_diagnostics_s {
  int *procedure_code;
  triggering_msg_t *triggering_msg;
  criticality_t *procedure_criticality;
  int num_errors;
  criticality_diagnostics_ie_t errors[E1AP_MAX_NUM_ERRORS];
} criticality_diagnostics_t;

typedef struct {
  in_addr_t ipsec_tl_address;
  uint8_t num_gtp_tl_addresses;
  in_addr_t gtp_tl_addresses[E1AP_MAX_GTP_TL_ADDRESSES];
} tnl_address_info_item_t;

typedef struct {
  // Transport UP LayerAddresses Info to Add List
  tnl_address_info_item_t addresses_to_add[E1AP_MAX_TL_ADDRESSES];
  uint8_t num_addresses_to_add;
  // Transport UP Layer Addresses Info to Remove List
  tnl_address_info_item_t addresses_to_remove[E1AP_MAX_TL_ADDRESSES];
  uint8_t num_addresses_to_remove;
} tnl_address_info_t;

typedef nssai_t e1ap_nssai_t;

typedef struct {
  net_ip_address_t CUUP_e1_ip_address;
  net_ip_address_t CUCP_e1_ip_address;
  uint16_t remotePortF1U;
  char* localAddressF1U;
  uint16_t localPortF1U;
  char* localAddressN3;
  uint16_t localPortN3;
  uint16_t remotePortN3;
} e1ap_net_config_t;

/* GNB-CU-UP E1 Setup Request */
typedef struct e1ap_setup_req_s {
  uint64_t              gNB_cu_up_id;
  char *                gNB_cu_up_name;
  uint64_t              transac_id;
  int                   supported_plmns;
  // CN Support
  cn_Support_t cn_support;
  struct {
    PLMN_ID_t id;
    int supported_slices;
    e1ap_nssai_t *slice;
  } plmn[E1AP_MAX_NUM_PLMNS];
} e1ap_setup_req_t;

typedef struct e1ap_cucp_setup_req_s {
  char* gNB_cu_cp_name;
  uint64_t transac_id;
} e1ap_cucp_setup_req_t;

typedef struct {
  e1ap_setup_req_t setup_req;
  e1ap_net_config_t net_config;
  uint32_t gnb_id; // unused in CU-UP, but might be necessary for some functionality, e.g., E2 agent
} e1ap_register_req_t;

typedef struct e1ap_setup_resp_s {
  // Transaction ID
  long transac_id;
  // gNB-CU-CP Name
  char* gNB_cu_cp_name;
  // Transport Network Layer Address Info
  tnl_address_info_t* tnla_info;
} e1ap_setup_resp_t;

/* E1AP Setup Failure */
typedef struct e1ap_setup_fail_s {
  long transac_id;
  e1ap_cause_t cause;
  long *time_to_wait;
  criticality_diagnostics_t *crit_diag;
} e1ap_setup_fail_t;

typedef struct UP_TL_information_s {
  in_addr_t tlAddress;
  int32_t teId;
} UP_TL_information_t;

typedef struct up_params_s {
  // Transport layer info
  UP_TL_information_t tl_info;
  // Cell Group ID (M)
  cell_group_id_t cell_group_id;
} up_params_t;

/* IE SDAP Configuration (clause 9.3.1.39 of 3GPP TS 38.463) */
typedef struct bearer_context_sdap_config_s {
  long defaultDRB;
  bool sDAP_Header_UL;
  bool sDAP_Header_DL;
} bearer_context_sdap_config_t;

/* IE PDCP Configuration (clause 9.3.1.38 of 3GPP TS 38.463) */
typedef struct bearer_context_pdcp_config_s {
  long pDCP_SN_Size_UL;
  long pDCP_SN_Size_DL;
  long rLC_Mode;
  long reorderingTimer;
  long discardTimer;
  bool pDCP_Reestablishment;
} bearer_context_pdcp_config_t;

/* 9.3.1.35 3GPP TS 38.463 */
typedef struct {
  // PDCP SN
  uint32_t sn;
  // HFN
  uint32_t hfn;
} e1_pdcp_count_t;

/* 9.3.1.58 3GPP TS 38.463 */
typedef struct {
  // UL Count Value
  e1_pdcp_count_t ul_count;
  // DL Count Value
  e1_pdcp_count_t dl_count;
} e1_pdcp_status_info_t;

typedef struct drb_to_setup_s {
  long drbId;
  bearer_context_pdcp_config_t pdcp_config;
  long qci;
  long qosPriorityLevel;
  long pre_emptionCapability;
  long pre_emptionVulnerability;
  in_addr_t tlAddress;
  long teId;
  int numCellGroups;
  cell_group_id_t cellGroupList[E1AP_MAX_NUM_CELL_GROUPS];
} drb_to_setup_t;

typedef struct qos_characteristics_s {
  union {
    struct {
      uint16_t fiveqi;
      uint8_t qos_priority_level;
    } non_dynamic;
    struct {
      // Range 5QI [0 - 255]
      uint16_t fiveqi;
      /* Range [0 - 15]
       15 = "no priority," 1-14 = decreasing priority (1 highest), 0 = logical error if received */
      uint8_t qos_priority_level;
      // Range [0, 1023]: Upper bound for packet delay in 0.5ms units
      uint16_t packet_delay_budget;
      struct {
        // PER = Scalar x 10^-k (k: 0-9)
        uint8_t per_scalar;
        uint8_t per_exponent;
      } packet_error_rate;
    } dynamic;
  };
  fiveQI_t qos_type;
} qos_characteristics_t;

typedef struct ngran_allocation_retention_priority_s {
  uint16_t priority_level;
  // Pre-emption capability on other QoS flows
  uint8_t preemption_capability;
  // Vulnerability of the QoS flow to pre-emption of other QoS flows
  uint8_t preemption_vulnerability;
} ngran_allocation_retention_priority_t;

typedef struct qos_flow_level_qos_parameters_s {
  qos_characteristics_t qos_characteristics;
  ngran_allocation_retention_priority_t alloc_reten_priority; // additional members should be added!!
} qos_flow_level_qos_parameters_t;

// QoS Flow QoS Parameters List 9.3.1.25
typedef struct qos_flow_setup_e {
  long qfi; // qos flow identifier
  qos_flow_level_qos_parameters_t qos_params;
} qos_flow_to_setup_t;

// 9.3.1.60 QoS Flow Mapping Indication
typedef enum qos_flow_mapping_indication_e {
  QF_UL = 0,
  QF_DL,
  QF_BOTH
} qos_flow_mapping_indication_t;

// 9.3.1.12 QoS Flow List
typedef struct qos_flow_list_s {
  long qfi; // qos flow identifier
  qos_flow_mapping_indication_t *indication;
} qos_flow_list_t;

/**
 * DRB To Setup List according to 3GPP TS 38.463
 */
typedef struct DRB_nGRAN_to_setup_s {
  // DRB ID (M) (clause 9.3.1.16)
  long id;
  // SDAP Configuration (M) (clause 9.3.1.39)
  bearer_context_sdap_config_t sdap_config;
  // PDCP Configuration (M) (clause 9.3.1.38)
  bearer_context_pdcp_config_t pdcp_config;
  // DRB Inactivity Timer (O)
  int *drb_inactivity_timer;
  // Cell Group Information (M) (clause 9.3.1.11)
  int numCellGroups;
  cell_group_id_t cellGroupList[E1AP_MAX_NUM_CELL_GROUPS];
  // QoS Flows Information To Be Setup (M) (clause 9.3.1.25, 9.3.1.26)
  int numQosFlow2Setup;
  qos_flow_to_setup_t qosFlows[E1AP_MAX_NUM_QOS_FLOWS];
} DRB_nGRAN_to_setup_t;

/**
 * DRB To Modify List according to 3GPP TS 38.463
 */
typedef struct DRB_nGRAN_to_modify_s {
  // DRB ID (M) (clause 9.3.1.16)
  long id;
  // SDAP Configuration (O) (clause 9.3.1.39)
  bearer_context_sdap_config_t *sdap_config;
  // PDCP Configuration (O) (clause 9.3.1.38)
  bearer_context_pdcp_config_t *pdcp_config;
  // PDCP SN Status Request (O)
  bool pdcp_sn_status_requested;
  // PDCP SN Status Information
  e1_pdcp_status_info_t *pdcp_status;
  // DL UP Transport Layer Information (O) (clause 9.3.1.13, 9.3.2.1)
  int numDlUpParam;
  up_params_t DlUpParamList[E1AP_MAX_NUM_UP_PARAM];
  // Flow Mapping Information (O) (clause 9.3.1.25, 9.3.1.26)
  int numQosFlowsMod;
  qos_flow_to_setup_t qosFlows[E1AP_MAX_NUM_QOS_FLOWS];
} DRB_nGRAN_to_mod_t;

/* DRB To Remove Item (NG-RAN) clause 9.3.1.11 */
typedef struct {
  // DRB ID (M)
  long id;
} drb_to_remove_t;

typedef enum e1ap_indication_e {
  SECURITY_REQUIRED = 0,
  SECURITY_PREFERRED,
  SECURITY_NOT_NEEDED
} e1ap_indication_t;

typedef struct security_indication_s {
  e1ap_indication_t integrityProtectionIndication;
  e1ap_indication_t confidentialityProtectionIndication;
  // Maximum Integrity Protected Data Rate
  long maxIPrate;
} security_indication_t;

// Security Information (9.3.1.10)
typedef struct security_information_s {
  uint64_t cipheringAlgorithm;
  uint64_t integrityProtectionAlgorithm;
  char encryptionKey[E1AP_SECURITY_KEY_SIZE];
  char integrityProtectionKey[E1AP_SECURITY_KEY_SIZE];
} security_information_t;

/**
 * PDU Session Resource To Setup List (clause 9.3.3.10)
 */
typedef struct pdu_session_to_setup_s {
  // PDU Session ID (M)
  long sessionId;
  // PDU Session Type (M)
  long sessionType;
  // S-NSSAI (M)
  e1ap_nssai_t nssai;
  // Security Indication (M)
  security_indication_t securityIndication;
  // PDU Session Resource DL Aggregate Maximum Bit Rate (O)
  long *dlAggregateMaxBitRate;
  // NG UL UP Transport Layer Information (M)
  UP_TL_information_t UP_TL_information;
  // PDU Session Inactivity Timer (O)
  int *inactivityTimer;
  // DRB To Setup List (M)
  int numDRB2Setup;
  // DRB To Setup Item (1..<E1AP_MAX_NUM_DRBS>)
  DRB_nGRAN_to_setup_t DRBnGRanList[E1AP_MAX_NUM_DRBS];
} pdu_session_to_setup_t;

/**
 * PDU Session Resource To Modify List (clause 9.3.3.11)
 */
typedef struct pdu_session_to_mod_s {
  // PDU Session ID (M)
  long sessionId;
  // Security Indication (O)
  security_indication_t *securityIndication;
  // NG UL UP Transport Layer Information (O)
  UP_TL_information_t *UP_TL_information;
  // DRB To Setup List (0..1)
  int numDRB2Setup;
  // DRB To Setup Item (1..<E1AP_MAX_NUM_DRBS>)
  DRB_nGRAN_to_setup_t DRBnGRanList[E1AP_MAX_NUM_DRBS];
  // DRB To Modify List (0..1)
  long numDRB2Modify;
  // DRB To Modify Item (1..<E1AP_MAX_NUM_DRBS>)
  DRB_nGRAN_to_mod_t DRBnGRanModList[E1AP_MAX_NUM_DRBS];
  // DRB To Remove List (0..maxnoofDRBs)
  int n_drb_to_remove;
  drb_to_remove_t drbs_to_remove[E1AP_MAX_NUM_DRBS];
} pdu_session_to_mod_t;

/** PDU Session Resource To Remove List (3GPP TS 38.463 clause 9.3.3.12) */
typedef struct {
  // PDU Session ID (M)
  long sessionId;
  // Cause (O)
  e1ap_cause_t cause;
} pdu_session_to_remove_t;

/** DRB Required To Modify Item (NG-RAN) */
typedef struct {
  // DRB ID (M)
  long id;
  // Cause (O)
  e1ap_cause_t cause;
} DRB_nGRAN_required_to_mod_t;

/** PDU Session Resource Required To Modify List */
typedef struct {
  // PDU Session ID (M)
  long sessionId;
  // DRB Required To Modify List (O)
  int numDRB2Modify;
  DRB_nGRAN_required_to_mod_t DRBnGRanModList[E1AP_MAX_NUM_DRBS];
} pdu_session_required_to_mod_t;

/**
 * Bearer Context Setup Request message, clause 9.2.2.1 of 3GPP TS 38.463
 */
typedef struct e1ap_bearer_setup_req_s {
  // gNB-CU-CP UE E1AP ID (M)
  uint32_t gNB_cu_cp_ue_id;
  // Security Information (M)
  security_information_t secInfo;
  // UE DL Aggregate Maximum Bit Rate (M)
  long ueDlAggMaxBitRate;
  // UE DL Maximum Integrity Protected Data Rate (O)
  long *ueDlMaxIPBitRate;
  // Serving PLMN (M)
  PLMN_ID_t servingPLMNid;
  // Activity Notification Level (M)
  activity_notification_level_t anl;
  // Bearer Context Status Change (O)
  BEARER_CONTEXT_STATUS_t bearerContextStatus;
  // UE Inactivity Timer (O)
  int *inactivityTimerUE;
  // NG-RAN PDU Session Resource To Setup List (M)
  int numPDUSessions;
  pdu_session_to_setup_t *pduSession;
} e1ap_bearer_setup_req_t;

/** Bearer Context Setup Failure (9.2.2.3 3GPP TS 38.463) */
typedef struct e1ap_bearer_context_setup_failure_s {
  // gNB-CU-CP UE E1AP ID (M)
  uint32_t gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (O)
  uint32_t *gNB_cu_up_ue_id;
  // Cause (M)
  e1ap_cause_t cause;
} e1ap_bearer_context_setup_failure_t;

/**
 * Bearer Context Modification Request, clause 9.2.2.4 of 3GPP TS 38.463
 */
typedef struct e1ap_bearer_mod_req_s {
  // gNB-CU-CP UE E1AP ID (M)
  uint32_t gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  uint32_t gNB_cu_up_ue_id;
  // Security Information (O)
  security_information_t *secInfo;
  // UE DL Aggregate Maximum Bit Rate (O)
  uint64_t *ueDlAggMaxBitRate;
  // UE DL Maximum Integrity Protected Data Rate (O)
  uint64_t *ueDlMaxIPBitRate;
  // Bearer Context Status Change (O)
  BEARER_CONTEXT_STATUS_t *bearerContextStatus;
  // UE Inactivity Timer (O)
  int *inactivityTimer;
  // Data Discard Required (O)
  bool dataDiscardRequired;
  // NG-RAN PDU Session Resource To Setup List (O)
  int numPDUSessions;
  pdu_session_to_setup_t *pduSession;
  // NG-RAN PDU Session Resource To Modify List (O)
  int numPDUSessionsMod;
  pdu_session_to_mod_t *pduSessionMod;
  // NG-RAN PDU Session Resource To Remove List (O)
  int numPDUSessionsRem;
  pdu_session_to_remove_t *pduSessionRem;
} e1ap_bearer_mod_req_t;

typedef struct e1ap_bearer_release_cmd_s {
  uint32_t gNB_cu_cp_ue_id;
  uint32_t gNB_cu_up_ue_id;
  e1ap_cause_t cause;
} e1ap_bearer_release_cmd_t;

typedef struct e1ap_bearer_release_cplt_s {
  uint32_t gNB_cu_cp_ue_id;
  uint32_t gNB_cu_up_ue_id;
} e1ap_bearer_release_cplt_t;

// 9.3.1.45 Flow Failed List
typedef struct qos_flow_failed_s {
  long qfi;
  e1ap_cause_t cause;
} qos_flow_failed_t;

typedef struct DRB_nGRAN_setup_s {
  // DRB ID (M)
  long id;
  int numUpParam;
  up_params_t UpParamList[E1AP_MAX_NUM_UP_PARAM];
  // Flow Setup List (M)
  int numQosFlowSetup;
  qos_flow_list_t qosFlows[E1AP_MAX_NUM_QOS_FLOWS];
  // Flow Failed List (O)
  int numQosFlowFailed;
  qos_flow_failed_t qosFlowsFailed[E1AP_MAX_NUM_QOS_FLOWS];
} DRB_nGRAN_setup_t;

/* DRB Modified Item */
typedef struct DRB_nGRAN_modified_s {
  // DRB ID (M)
  long id;
  // UL UP Parameters (O)
  int numUpParam;
  up_params_t ul_UP_Params[E1AP_MAX_NUM_UP_PARAM];
  // Flow Setup List (O)
  int numQosFlowSetup;
  qos_flow_list_t qosFlows[E1AP_MAX_NUM_QOS_FLOWS];
  // Flow Failed List (O)
  int numQosFlowFailed;
  qos_flow_failed_t qosFlowsFailed[E1AP_MAX_NUM_QOS_FLOWS];
  // PDCP SN Status Information (0)
  e1_pdcp_status_info_t *pdcp_status;
} DRB_nGRAN_modified_t;

typedef struct DRB_nGRAN_failed_s {
  long id;
  e1ap_cause_t cause;
} DRB_nGRAN_failed_t;

typedef struct pdu_session_setup_s {
  long id;
  // Transport layer info
  UP_TL_information_t tl_info;
  int numDRBSetup;
  DRB_nGRAN_setup_t DRBnGRanList[E1AP_MAX_NUM_DRBS];
  int numDRBFailed;
  DRB_nGRAN_failed_t DRBnGRanFailedList[E1AP_MAX_NUM_DRBS];
} pdu_session_setup_t;

/* PDU Session Resource Modified List (9.3.3.17) */
typedef struct pdu_session_modif_s {
  // PDU Session ID (M)
  long id;
  // Security Result (O)
  e1ap_indication_t *integrityProtectionIndication;
  e1ap_indication_t *confidentialityProtectionIndication;
  // NG DL UP Transport Layer Information (O)
  UP_TL_information_t *ng_DL_UP_TL_info;
  // DRB Modified List (O)
  int numDRBModified;
  DRB_nGRAN_modified_t DRBnGRanModList[E1AP_MAX_NUM_DRBS];
  // DRB Failed to Modify List (O)
  int numDRBFailedToMod;
  DRB_nGRAN_failed_t DRBnGRanFailedModList[E1AP_MAX_NUM_DRBS];
  // DRB Setup List (O)
  int numDRBSetup;
  DRB_nGRAN_setup_t DRBnGRanSetupList[E1AP_MAX_NUM_DRBS];
  // DRB Failed List (O)
  int numDRBFailed;
  DRB_nGRAN_failed_t DRBnGRanFailedList[E1AP_MAX_NUM_DRBS];
} pdu_session_modif_t;

typedef struct e1ap_bearer_setup_resp_s {
  uint32_t gNB_cu_cp_ue_id;
  uint32_t gNB_cu_up_ue_id;
  int numPDUSessions;
  pdu_session_setup_t *pduSession;
} e1ap_bearer_setup_resp_t;

typedef struct e1ap_bearer_modif_resp_s {
  // gNB-CU-CP UE E1AP ID (M)
  uint32_t gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  uint32_t gNB_cu_up_ue_id;
  // NG-RAN PDU Session Resource Modified List (O)
  int numPDUSessionsMod;
  pdu_session_modif_t *pduSessionMod;
} e1ap_bearer_modif_resp_t;

/* E1AP Connection Loss indication */
typedef struct {
  int dummy;
} e1ap_lost_connection_t;

/// @brief 9.2.2.6 BEARER CONTEXT MODIFICATION FAILURE
typedef struct e1ap_bearer_context_mod_failure_s {
  // gNB-CU-CP UE E1AP ID (M)
  uint32_t gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  uint32_t gNB_cu_up_ue_id;
  // NG-RAN PDU Session Resource Modified List (O)
  e1ap_cause_t cause;
} e1ap_bearer_context_mod_failure_t;

/**
 * Bearer Context Modification Required (9.2.2.7 3GPP TS 38.463)
 * (gNB-CU-UP -> gNB-CU-CP)
 */
typedef struct e1ap_bearer_mod_required_s {
  uint32_t gNB_cu_cp_ue_id;
  uint32_t gNB_cu_up_ue_id;
  // PDU Session Resource Required To Modify List (O)
  int numPDUSessionsMod;
  pdu_session_required_to_mod_t *pduSessionMod;
  // PDU Session Resource To Remove List (O)
  int numPDUSessionsRem;
  pdu_session_to_remove_t *pduSessionRem;
} e1ap_bearer_mod_required_t;

/** Bearer Context Modification Confirm (9.2.2.8 3GPP TS 38.463) */
typedef struct e1ap_bearer_mod_confirm_s {
  uint32_t gNB_cu_cp_ue_id;
  uint32_t gNB_cu_up_ue_id;
} e1ap_bearer_mod_confirm_t;

#endif /* E1AP_MESSAGES_TYPES_H */
