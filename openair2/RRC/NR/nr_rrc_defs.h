/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NR RRC struct definitions and function prototypes
 */

#ifndef __OPENAIR_RRC_DEFS_NR_H__
#define __OPENAIR_RRC_DEFS_NR_H__


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "collection/tree.h"
#include "collection/linear_alloc.h"
#include "common/utils/ds/seq_arr.h"
#include "nr_rrc_common.h"
#include "ds/byte_array.h"
#include "common/platform_constants.h"
#include "common/platform_types.h"
#include "mac_rrc_dl.h"
#include "cucp_cuup_if.h"
#include "NR_BCCH-BCH-Message.h"
#include "NR_BCCH-DL-SCH-Message.h"
#include "NR_CellGroupConfig.h"
#include "NR_MeasurementReport.h"
#include "NR_MeasurementTimingConfiguration.h"
#include "NR_RRCReconfiguration.h"
#include "NR_UE-CapabilityRAT-ContainerList.h"
#include "NR_UL-CCCH-Message.h"
#include "NR_UE-MRDC-Capability.h"
#include "NR_UE-NR-Capability.h"
#include "intertask_interface.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_configuration.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_configuration.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_configuration.h"
#include "openair2/RRC/NR/nrdc_configuration.h"

typedef enum {
  NR_RRC_OK=0,
  NR_RRC_ConnSetup_failed,
  NR_RRC_PHY_RESYNCH,
  NR_RRC_Handover_failed,
  NR_RRC_HO_STARTED
} NR_RRC_status_t;

#define UNDEF_SECURITY_MODE                           0xff
#define NO_SECURITY_MODE                              0x20

/* TS 36.331: RRC-TransactionIdentifier ::= INTEGER (0..3) */
#define NR_RRC_TRANSACTION_IDENTIFIER_NUMBER 4

typedef struct UE_S_TMSI_NR_s {
  bool                                                presence;
  uint16_t                                            amf_set_id;
  uint8_t                                             amf_pointer;
  uint32_t                                            fiveg_tmsi;
} __attribute__ ((__packed__)) NR_UE_S_TMSI;

typedef struct nr_e_rab_param_s {
  e_rab_t param;
  uint8_t status;
  uint8_t xid; // transaction_id
} __attribute__ ((__packed__)) nr_e_rab_param_t;

typedef enum pdu_session_satus_e {
  PDU_SESSION_STATUS_NEW,
  PDU_SESSION_STATUS_ESTABLISHED,
  PDU_SESSION_STATUS_TOMODIFY, // ENDC NSA
  PDU_SESSION_STATUS_FAILED,
  PDU_SESSION_STATUS_TORELEASE, // PDU Session Release
  PDU_SESSION_STATUS_NOTIFYONRELEASE, // N3 GTP-U Error Indication triggers Resource Notify
} pdu_session_status_t;

typedef struct pdusession_s {
  /* Unique pdusession_id for the UE. */
  int pdusession_id;
  byte_array_t nas_pdu;
  /* Quality of service for this pdusession */
  seq_arr_t qos;
  /* The transport layer address for the IP packets */
  pdu_session_type_t pdu_session_type;
  // NG-RAN endpoint of the NG-U (N3) transport bearer
  gtpu_tunnel_t n3_outgoing;
  // UPF endpoint of the NG-U (N3) transport bearer
  gtpu_tunnel_t n3_incoming;
  nssai_t nssai;
  // PDU Session specific SDAP configuration
  nr_sdap_configuration_t sdap_config;
} pdusession_t;

typedef struct pdu_session_param_s {
  pdusession_t param;
  pdu_session_status_t status;
  uint8_t xid; // transaction_id
  ngap_cause_t cause;
} rrc_pdu_session_param_t;

typedef struct drb_s {
  int status;
  int drb_id;
  int pdusession_id;
  // F1-U Downlink Tunnel Config (on DU side)
  gtpu_tunnel_t du_tunnel_config;
  // F1-U Uplink Tunnel Config (on CU-UP side)
  gtpu_tunnel_t cuup_tunnel_config;
  // DRB-specific PDCP configuration
  nr_pdcp_configuration_t pdcp_config;
} drb_t;

typedef enum {
  RRC_ACTION_NONE, /* no transaction ongoing */
  RRC_SETUP,
  RRC_SETUP_FOR_REESTABLISHMENT,
  RRC_REESTABLISH,
  RRC_REESTABLISH_COMPLETE,
  RRC_DEDICATED_RECONF,
  RRC_PDUSESSION_ESTABLISH,
  RRC_PDUSESSION_MODIFY,
  RRC_PDUSESSION_RELEASE,
  RRC_PDUSESSION_RESOURCE_NOTIFY,
  RRC_UECAPABILITY_ENQUIRY,
  RRC_F1_NRDC_IN_PROGRESS,
} rrc_action_t;

typedef struct nr_rrc_config {
  uint32_t tac;
  plmn_id_t plmn[NR_MAX_NB_PLMNS];
  uint8_t num_plmn;

  bool um_on_default_drb;
  bool enable_sdap;
  int drbs;
} nr_rrc_config_t;

/* Small state for delaying NG-triggered actions (setup/release) */
typedef struct {
  int max_delays;
  bool ongoing_transaction;
} delayed_action_state_t;

typedef struct nr_redcap_ue_cap {
  bool support_of_redcap_r17;
  bool support_of_16drb_redcap_r17;
  bool pdcp_drb_long_sn_redcap_r17;
  bool rlc_am_drb_long_sn_redcap_r17;
} nr_redcap_ue_cap_t;

typedef struct {
  int drb_id;
  pdusession_level_qos_parameter_t qos;
  /** Indicate if the QoS flow is pending for NGAP modify response. */
  bool ngap_pending;
} nr_rrc_qos_t;

typedef struct {
  uint64_t dl_br;
  uint64_t ul_br;
} nr_rrc_ambr_t;

/** @brief UE serving cell information
 * @note ServCellIndex is a short identity used to uniquely identify a serving cell
 *       (PCell, PSCell, or SCell) across cell groups (TS 38.331).
 *       Value 0 applies for the PCell, while the SCellIndex that has previously
 *       been assigned applies for SCells.
 * @note Range: 0..maxNrofServingCells-1 where maxNrofServingCells = 32 */
typedef struct {
  /* NR Cell Identity (cell_id) */
  uint64_t nci;
  /* ServCellIndex (TS 38.331): 0 = PCell, 1-31 = SCell */
  uint8_t serving_cell_id;
  /* SCTP association ID of the DU that owns this cell (for fast lookup) */
  sctp_assoc_t assoc_id;
} ue_serving_cell_t;

/* forward declaration */
typedef struct nr_handover_context_s nr_handover_context_t;

typedef struct gNB_RRC_UE_s {
  time_t last_seen; // last time this UE has been accessed

  NR_SRB_INFO_TABLE_ENTRY Srb[NR_NUM_SRB];
  NR_MeasConfig_t                   *measConfig;
  nr_handover_context_t *ho_context;
  NR_MeasResults_t                  *measResults;

  bool as_security_active;
  bool f1_ue_context_active;

  byte_array_t ue_cap_buffer;
  NR_UE_NR_Capability_t*             UE_Capability_nr;
  int                                UE_Capability_size;
  NR_UE_MRDC_Capability_t*           UE_Capability_MRDC;
  int                                UE_MRDC_Capability_size;

  // Transparent forwarding of CellGroupConfig
  byte_array_t mcg;
  NR_RadioBearerConfig_t             *rb_config;

  /* KgNB as derived from KASME received from EPC */
  uint8_t kgnb[32];
  uint8_t nh[32];
  int8_t  nh_ncc;

  /* Used integrity/ciphering algorithms */
  NR_CipheringAlgorithm_t            ciphering_algorithm;
  e_NR_IntegrityProtAlgorithm        integrity_algorithm;

  rnti_t                             rnti;
  uint64_t                           random_ue_identity;

  /* Information from UE RRC Setup Request */
  NR_UE_S_TMSI                       Initialue_identity_5g_s_TMSI;
  uint64_t                           ng_5G_S_TMSI_Part1;
  NR_EstablishmentCause_t            establishment_cause;

  /* Dynamic array of UE serving cells */
  seq_arr_t serving_cells; /* ue_serving_cell_t */

  uint32_t                           rrc_ue_id;
  uint64_t amf_ue_ngap_id;
  // Globally Unique AMF Identifier
  nr_guami_t ue_guami;
  // Serving PLMN of the UE
  plmn_id_t serving_plmn;

  ngap_security_capabilities_t       security_capabilities;
  //NSA block
  sctp_assoc_t x2_target_assoc;
  int MeNB_ue_x2_id;
  int                                nb_of_e_rabs;
  nr_e_rab_param_t                   e_rab[NB_RB_MAX];//[S1AP_MAX_E_RAB];
  uint32_t                           nsa_gtp_teid[S1AP_MAX_E_RAB];
  transport_layer_addr_t             nsa_gtp_addrs[S1AP_MAX_E_RAB];
  rb_id_t                            nsa_gtp_ebi[S1AP_MAX_E_RAB];
  rb_id_t                            nsa_gtp_psi[S1AP_MAX_E_RAB];

  //SA block
  seq_arr_t pduSessions;
  // Established DRBs
  seq_arr_t drbs;

  rrc_action_t xids[NR_RRC_TRANSACTION_IDENTIFIER_NUMBER];
  uint8_t e_rab_release_command_flag;
  uint32_t ue_rrc_inactivity_timer;
  uint32_t                           ue_reestablishment_counter;
  uint32_t                           ue_reconfiguration_counter;
  bool ongoing_reconfiguration;
  bool an_release; // flag if core requested UE release

  /* NGUEContextSetup might come with PDU sessions, but setup needs to be
   * delayed after security (and capability); PDU sessions are stored here */
  int n_initial_pdu;
  pdusession_t *initial_pdus;

  // Aggregate Maximum Bit Rate
  nr_rrc_ambr_t ambr;

  /* Nas Pdu */
  byte_array_t nas_pdu;

  /* hack, see rrc_gNB_process_NGAP_PDUSESSION_SETUP_REQ() for more info */
  delayed_action_state_t delayed_action;

  nr_redcap_ue_cap_t *redcap_cap;

  /* measurement IDs */
  uint64_t measurement_object_ids;       /* bitfield, 64 possible values */
  uint64_t measurement_ids;              /* bitfield, 64 possible values */
  uint64_t report_config_ids;            /* bitfield, 64 possible values */

  /* opaque pointer to store NR-DC state (NULL when no NR-DC) */
  void *nrdc;
} gNB_RRC_UE_t;

typedef struct rrc_gNB_ue_context_s {
  /* Tree related data */
  RB_ENTRY(rrc_gNB_ue_context_s) entries;
  /* UE id for initial connection to NGAP */
  struct gNB_RRC_UE_s   ue_context;
} rrc_gNB_ue_context_t;

typedef struct {
  /* nea0 = 0, nea1 = 1, ... */
  int ciphering_algorithms[4];
  int ciphering_algorithms_count;

  /* nia0 = 0, nia1 = 1, ... */
  int integrity_algorithms[4];
  int integrity_algorithms_count;

  /* flags to enable/disable ciphering and integrity for DRBs */
  int do_drb_ciphering;
  int do_drb_integrity;
} nr_security_configuration_t;

typedef struct {
  long maxReportCells;
  bool includeBeamMeasurements;
} nr_per_event_t;

typedef struct {
  long threshold_RSRP;
  long timeToTrigger;
} nr_a2_event_t;

typedef struct {
  int pci;
  long a3_offset;
  long hysteresis;
  long timeToTrigger;
} nr_a3_event_t;

typedef struct {
  nr_per_event_t *per_event;
  nr_a2_event_t *a2_event;
  seq_arr_t *a3_event_list;
  bool is_default_a3_configuration_exists;
} nr_measurement_configuration_t;

/** @brief Per-neighbor cell-specific offsets (TS 38.331), shared by SIB3 and SIB4
 * Maps to ASN.1 IntraFreqNeighCellInfo and InterFreqNeighCellInfo
 * (SIB3.IntraFreqNeighCellList, SIB4.InterFreqCarrierFreqInfo.neighCellList) */
typedef struct {
  // q-OffsetCell: (Q-OffsetRange values -24,-22,-20,-18,-16,-14,-12,-10,-8,-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6,8,10,12,14,16,18,20,22,24 dB)
  // per-neighbor cell ranking
  int q_OffsetCell;
  // q-RxLevMinOffsetCell: Q-OffsetCellSmall (1..8): Per-neighbor RSRP minimum offset
  int q_RxLevMinOffsetCell;
  // q-QualMinOffsetCell: Q-OffsetCellSmall (1..8): Per-neighbor RSRQ minimum offset
  int q_QualMinOffsetCell;
} nr_neighbour_cell_neighbor_offset_t;

/** @brief SIB3 (intra-frequency) neighbor list parameters (TS 38.331)
 * Per-neighbor fields for SIB3.IntraFreqNeighCellList.IntraFreqNeighCellInfo.
 * SIB3 carries intra-freq neighbor offsets for ranking. */
typedef struct {
  nr_neighbour_cell_neighbor_offset_t offset;
} nr_neighbour_cell_sib3_t;

/** @brief SIB4 inter-frequency carrier parameters (TS 38.331)
 * Per-frequency fields for SIB4.InterFreqCarrierFreqList.InterFreqCarrierFreqInfo.
 * Stored once per ARFCN; all neighbors on that frequency share this config. */
typedef struct {
  // cellReselectionPriority (0..7): Absolute priority of this inter-freq carrier
  int cellReselectionPriority;
  // threshX-HighP (0..31): RSRP threshold for reselection to a higher-priority inter-freq
  int threshX_HighP;
  // threshX-LowP (0..31): RSRP threshold for reselection to a lower-priority inter-freq
  int threshX_LowP;
  // threshX-HighQ (0..31): RSRQ threshold for higher-priority inter-freq reselection
  int threshX_HighQ;
  // threshX-LowQ (0..31): RSRQ threshold for lower-priority inter-freq reselection
  int threshX_LowQ;
  // q-OffsetFreq (Q-OffsetRange values: -24,-22,-20,-18,-16,-14,-12,-10,-8,-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6,8,10,12,14,16,18,20,22,24 dB):
  // Frequency-specific offset in inter-freq cell ranking formula
  int q_OffsetFreq;
} nr_neighbour_cell_sib4_freq_t;

/** @brief SIB4 (inter-frequency) parameters per neighbor (TS 38.331)
 * Combines per-frequency carrier info (InterFreqCarrierFreqInfo) and per-neighbor
 * offsets (InterFreqNeighCellInfo). */
typedef struct {
  // Per-frequency carrier (InterFreqCarrierFreqInfo)
  nr_neighbour_cell_sib4_freq_t sib4_freq;
  // Per-neighbor offsets (InterFreqNeighCellInfo)
  nr_neighbour_cell_neighbor_offset_t offset;
} nr_neighbour_cell_sib4_t;

/** @brief Per-frequency SIB4 configuration (TS 38.331), keyed by ARFCN
 * One entry per inter-freq carrier (absoluteFrequencySSB + subcarrierSpacing).
 * freq_cfg holds InterFreqCarrierFreqInfo parameters. Analogous to one “carrier” slice of SIB4
 * interFreqCarrierFreqList. */
typedef struct {
  // absoluteFrequencySSB (ARFCN)
  int arfcn;
  // ssbSubcarrierSpacing
  int scs;
  // q-RxLevMin (-70..-22 dBm)
  int q_RxLevMin;
  // t-ReselectionNR (0..7)
  int t_ReselectionNR;
  // Inter-frequency carrier parameters (InterFreqCarrierFreqInfo)
  nr_neighbour_cell_sib4_freq_t freq_cfg;
} nr_inter_freq_cfg_t;

typedef struct {
  // Number of F1 and N2 handovers successfully completed to this neighbour cell
  uint16_t ho_success_count;
  // Shall be incremented with each neighbour information change
  uint16_t version;
} nr_neighbour_cell_stats_t;

/** @brief Neighbor cell configuration structure
 * Single source of truth for neighbor cell information, used across multiple protocols/scopes:
 * - Handover (NGAP/XnAP): for target gNB (ID, plmn, tac, nrcell_id, physicalCellId)
 * - Measurement (MeasConfig): physicalCellId, absoluteFrequencySSB, band
 * - DU validation: all fields validated against actual cell information
 * - SIB3/SIB4 generation: physicalCellId, absoluteFrequencySSB, subcarrierSpacing, band
 * References:
 * - 3GPP TS 38.331 (RRC) for SIB3/SIB4 and MeasConfig
 * - 3GPP TS 38.413 (NGAP) / TS 38.423 (XnAP) for target cell identification */
typedef struct {
  // gNB identifier (for target node in NGAP/XnAP handover)
  uint32_t gNB_ID;
  // NR Cell Global Identifier (for NGAP/XnAP handover, DU validation)
  uint64_t nrcell_id;
  // Physical Cell ID (PCI) (used in HandoverPreparationInformation and MeasObjectNR)
  int physicalCellId;
  // SSB absolute frequency (ARFCN) (for MeasObjectNR, intra/inter-frequency determination)
  int absoluteFrequencySSB;
  // SSB subcarrier spacing (for MeasObjectNR/SIB4)
  int subcarrierSpacing;
  // Frequency band indicator (for MeasObjectNR/SIB4)
  int band;
  // PLMN identity (for target node in NGAP/XnAP handover)
  plmn_id_t plmn;
  // Tracking Area Code (for target node in NGAP/XnAP handover)
  uint32_t tac;
  // SIB3 (intra-frequency neighbor cell-specific offsets)
  nr_neighbour_cell_sib3_t sib3;
  // SIB4 (inter-frequency neighbor cell-specific parameters)
  nr_neighbour_cell_sib4_t sib4;
  nr_neighbour_cell_stats_t stats;
} nr_neighbour_cell_t;

typedef struct neighbour_cell_configuration_s {
  uint64_t nr_cell_id;
  seq_arr_t neighbour_cells;
} neighbour_cell_configuration_t;

typedef struct nr_mac_rrc_dl_if_s {
  f1_reset_cu_initiated_func_t f1_reset;
  f1_reset_acknowledge_du_initiated_func_t f1_reset_acknowledge;
  f1_setup_response_func_t f1_setup_response;
  f1_setup_failure_func_t f1_setup_failure;
  gnb_du_configuration_update_ack_func_t gnb_du_configuration_update_acknowledge;
  ue_context_setup_request_func_t ue_context_setup_request;
  ue_context_modification_request_func_t ue_context_modification_request;
  ue_context_modification_confirm_func_t ue_context_modification_confirm;
  ue_context_modification_refuse_func_t ue_context_modification_refuse;
  ue_context_release_command_func_t ue_context_release_command;
  dl_rrc_message_transfer_func_t dl_rrc_message_transfer;
  f1_paging_transfer_func_t paging_transfer;
  trp_information_request_func_t trp_information_request;
  positioning_information_request_func_t positioning_information_request;
  positioning_activation_request_func_t positioning_activation_request;
  positioning_measurement_request_func_t positioning_measurement_request;
} nr_mac_rrc_dl_if_t;

typedef struct cucp_cuup_if_s {
  cucp_cuup_bearer_context_setup_func_t bearer_context_setup;
  cucp_cuup_bearer_context_mod_func_t bearer_context_mod;
  cucp_cuup_bearer_context_mod_confirm_func_t bearer_context_mod_confirm;
  cucp_cuup_bearer_context_release_func_t bearer_context_release;
} cucp_cuup_if_t;

typedef struct {
  int band;
  uint32_t arfcn;
  uint8_t scs;
  uint16_t nrb;
} nr_rrc_freq_info_t;

typedef struct {
  nr_rrc_freq_info_t dlul;
} nr_rrc_tdd_info_t;

typedef struct {
  nr_rrc_freq_info_t dl;
  nr_rrc_freq_info_t ul;
} nr_rrc_fdd_info_t;

typedef struct {
  /* operating mode: TDD or FDD */
  enum { NR_MODE_TDD = 0, NR_MODE_FDD = 1 } mode;
  uint64_t cell_id;
  uint16_t pci;
  union {
    nr_rrc_tdd_info_t tdd;
    nr_rrc_fdd_info_t fdd;
  };
  plmn_id_t plmn;
  uint16_t tac;
} nr_rrc_cell_info_t;

typedef struct nr_rrc_cell_container_t {
  /* Tree-related data */
  RB_ENTRY(nr_rrc_cell_container_t) entries;
  /* transport association */
  sctp_assoc_t assoc_id;
  /* Cell-only RRC-local info */
  nr_rrc_cell_info_t info;
  /* MIB message (6.2.2 TS 38.331) */
  NR_MIB_t *mib;
  /* SIB1 message (6.2.2 TS 38.331) */
  NR_SIB1_t *sib1;
  /* MeasurementTimingConfiguration inter-node (TS 38.331) */
  NR_MeasurementTimingConfiguration_t *mtc;
} nr_rrc_cell_container_t;

typedef struct nr_rrc_du_container_t {
  /* Tree-related data */
  RB_ENTRY(nr_rrc_du_container_t) entries;
  /* DU-only information */
  /* Transport association identifier for this DU */
  sctp_assoc_t assoc_id;
  /* DU identity */
  uint64_t gNB_DU_id;
  /* DU name */
  char *gNB_DU_name;
  /* RRC version */
  uint8_t rrc_ver[3];
  /* Cells, indexed by cell_id */
  seq_arr_t cells; /* nr_rrc_cell_container_t* */
} nr_rrc_du_container_t;

typedef struct nr_rrc_cuup_container_t {
  /* Tree-related data */
  RB_ENTRY(nr_rrc_cuup_container_t) entries;

  e1ap_setup_req_t *setup_req;
  sctp_assoc_t assoc_id;
} nr_rrc_cuup_container_t;

/**Timers for SIB2 MobilityStateParameters (TS 38.331)
 * Value set shared by T-Evaluation and T-HystNormal: (s30, s60, s120, s180, s240). */
typedef enum sib2_mobility_state_timer_e {
  SIB2_MOBILITY_STATE_TIMER_S30,
  SIB2_MOBILITY_STATE_TIMER_S60,
  SIB2_MOBILITY_STATE_TIMER_S120,
  SIB2_MOBILITY_STATE_TIMER_S180,
  SIB2_MOBILITY_STATE_TIMER_S240,
} sib2_mobility_state_timer_t;

/** Q-HystSF scaling factors for Q_hyst (TS 38.331)
 * Speed-dependent offsets (-6, -4, -2, 0 dB) applied to the base Q_hyst
 * when the UE is in medium or high mobility state. */
typedef enum sib2_q_hystsf_e {
  SIB2_Q_HYSTSF_DB_6,
  SIB2_Q_HYSTSF_DB_4,
  SIB2_Q_HYSTSF_DB_2,
  SIB2_Q_HYSTSF_DB0,
} sib2_q_hystsf_t;

/** SIB2 speedStateReselectionPars configuration
 * Mirrors TS 38.331 MobilityStateParameters and Q-hystSF. */
typedef struct sib2_speed_state_reselection_pars_s {
  /* t-Evaluation: duration for evaluating allowed reselections
   * to enter mobility states (s30, s60, s120, s180, s240) */
  sib2_mobility_state_timer_t t_Evaluation;
  /* t-HystNormal: additional time period to evaluate reselection criteria
   * before returning to normal mobility (s30, s60, s120, s180, s240) */
  sib2_mobility_state_timer_t t_HystNormal;
  /* n-CellChangeMedium: max number of cell reselections to enter medium mobility state (1..16) */
  int n_CellChangeMedium;
  /* n-CellChangeHigh: max number of cell reselections to enter high mobility state (1..16) */
  int n_CellChangeHigh;
  /* sf-Medium: speed-dependent scaling factor for Qhyst in medium state (-6, -4, -2, 0 dB) */
  sib2_q_hystsf_t sf_Medium;
  /* sf-High: speed-dependent scaling factor for Qhyst in high state (-6, -4, -2, 0 dB) */
  sib2_q_hystsf_t sf_High;
} sib2_speed_state_reselection_pars_t;

/** SIB2 cellReselectionServingFreqInfo (TS 38.331) */
typedef struct {
  /* s-NonIntraSearchP: Srxlev threshold to trigger NR inter-freq / inter-RAT measurements (0..31)*2 dB.
   * Set to -1 to omit (optional ASN.1 field). */
  int s_NonIntraSearchP;
  /* s-NonIntraSearchQ: Squal threshold to trigger NR inter-freq / inter-RAT measurements (0..31)*2 dB */
  int s_NonIntraSearchQ;
  /* threshServingLowP: Srxlev threshold (dB) used by the UE on the serving cell
   * when reselecting towards a lower-priority RAT/frequency (0..31)*2 dB */
  int threshServingLowP;
  /* threshServingLowQ: Squal threshold (dB) used by the UE on the serving cell
   * when reselecting towards a lower-priority RAT/frequency (0..31)*2 dB.
   * Set to -1 to omit (optional ASN.1 field). */
  int threshServingLowQ;
  /* cellReselectionPriority: absolute priority of serving NR frequency (0..7) */
  int cellReselectionPriority;
} cell_reselection_serving_freq_info_t;

/** SIB2 cellReselectionInfoCommon (TS 38.331) */
typedef struct {
  /* q-Hyst: hysteresis added to serving-cell ranking R_s (0..24 dB) */
  int q_Hyst;
  /* speedStateReselectionPars: MobilityStateParameters + q-HystSF */
  sib2_speed_state_reselection_pars_t *speedStateReselectionPars;
} cell_reselection_info_common_t;

/** SIB2 intraFreqCellReselectionInfo (TS 38.331) */
typedef struct {
  /* q-RxLevMin: minimum required RX level in the cell (dBm) */
  int q_RxLevMin;
  /* q-QualMin: minimum required quality level in the cell (dB); */
  int q_QualMin;
  /* s-IntraSearchP: Srxlev threshold for intra-freq meas (0..31) */
  int s_IntraSearchP;
  /* s-IntraSearchQ: Squal threshold for intra-freq meas (0..31) */
  int s_IntraSearchQ;
  /* t-ReselectionNR: NR cell reselection timer (0..7) */
  int t_ReselectionNR;
} intra_freq_cell_reselection_info_t;

/** SIB2 configuration for idle/inactive cell reselection
 *  (maps to SIB2 reselection parameters in TS 38.304/38.331, per cell).
 * @note Squal = Cell selection quality value (dB)
 *       Srxlev = Cell selection RX level value (dB) */
typedef struct sib2_config_s {
  // cellReselectionInfoCommon
  cell_reselection_info_common_t cell_reselection_info_common;
  // cellReselectionServingFreqInfo
  cell_reselection_serving_freq_info_t cell_reselection_serving_freq_info;
  // intraFreqCellReselectionInfo
  intra_freq_cell_reselection_info_t intra_freq_cell_reselection_info;
  /* deriveSSB_IndexFromCell: SIB2 deriveSSB-IndexFromCell - whether UE may
   * assume SFN/SSB alignment across cells on serving freq (per TS 38.304/38.133) */
  bool deriveSSB_IndexFromCell;
} sib2_config_t;

/* MR.NRScSSSINR histogram dimension (3GPP TS 28.552 §5.1.1.32). */
#define NR_KPM_SS_SINR_NB_LEVELS 128 /* SS-SINR report level: 0..127, TS 38.133 Table 10.1.16.1-1 */

/** Peer gNB with an active Xn interface, used during measurement-report
 *  processing to decide between Xn-HO and N2-HO. */
typedef struct rrc_xn_candidate_s {
  RB_ENTRY(rrc_xn_candidate_s) entry;
  /* remote gNB identity (tree key) */
  uint32_t gnb_id;
  /* SCTP association to that gNB */
  sctp_assoc_t assoc_id;
} rrc_xn_candidate_t;

int rrc_xn_candidate_cmp(struct rrc_xn_candidate_s *a, struct rrc_xn_candidate_s *b);

//---NR---(completely change)---------------------
typedef struct gNB_RRC_INST_s {

  ngran_node_t                                        node_type;
  uint32_t                                            node_id;
  char                                               *node_name;
  int                                                 module_id;
  uid_allocator_t                                     uid_allocator;
  RB_HEAD(rrc_nr_ue_tree_s, rrc_gNB_ue_context_s) rrc_ue_head; // ue_context tree key search by rnti

  // RRC configuration
  nr_rrc_config_t configuration;
  seq_arr_t *SIBs;
  // SIB2 configuration for cell reselection parameters
  sib2_config_t sib2_config;

  // gNB N3 GTPU instance
  instance_t e1_inst;

  char *uecap_file;

  // security configuration (preferred algorithms)
  nr_security_configuration_t security;

  nr_mac_rrc_dl_if_t mac_rrc;
  cucp_cuup_if_t cucp_cuup;
  // Per-frequency SIB4 configurations, indexed by ARFCN
  seq_arr_t inter_freqs; /* array of nr_inter_freq_cfg_t */
  // Per-cell neighbour configurations, indexed by cell_id
  seq_arr_t *neighbour_cell_configuration;
  nr_measurement_configuration_t measurementConfiguration;

  RB_HEAD(rrc_du_tree, nr_rrc_du_container_t) dus; // DUs, indexed by assoc_id
  size_t num_dus;

  /* Global cell tree, indexed by cell_id */
  RB_HEAD(rrc_cell_tree, nr_rrc_cell_container_t) cells;
  size_t num_cells;

  RB_HEAD(rrc_cuup_tree, nr_rrc_cuup_container_t) cuups; // CU-UPs, indexed by assoc_id
  size_t num_cuups;

  /* Peer gNBs reachable over an active Xn interface, indexed by gnb_id.
   * Populated by XNAP_SETUP_IND; consulted during measurement-report
   * processing to prefer Xn-HO over N2-HO when a Xn link is available. */
  RB_HEAD(rrc_xn_cand_tree, rrc_xn_candidate_s) xn_candidates;

  // PDCP configuration parameters loaded during startup
  nr_pdcp_configuration_t pdcp_config;
  nr_rlc_configuration_t rlc_config;
  nrdc_configuration_t nrdc_config;

  /// cell-wide NR serving-cell SS-SINR distribution, see 28.552 5.1.1.32
  /// 0-127 SS-SINR report level (TS 38.133)
  uint32_t ss_sinr_cell_dist[NR_KPM_SS_SINR_NB_LEVELS];

  uint64_t rrc_conn_count_sum;
  uint64_t rrc_conn_count_samples;
} gNB_RRC_INST;

/** Forward declaration for UE log macros */
const ue_serving_cell_t *ue_get_pcell_entry(const gNB_RRC_UE_t *ue);

#define UE_LOG_FMT "(cellID %lx, UE ID %d RNTI %04x)"
#define UE_LOG_ARGS(ue_context) \
  (ue_get_pcell_entry(ue_context) ? ue_get_pcell_entry(ue_context)->nci : 0), (ue_context)->rrc_ue_id, (ue_context)->rnti

#define LOG_UE_DL_EVENT(ue_context, fmt, ...) LOG_A(NR_RRC, "[DL] " UE_LOG_FMT " " fmt, UE_LOG_ARGS(ue_context) __VA_OPT__(,) __VA_ARGS__)
#define LOG_UE_EVENT(ue_context, fmt, ...)    LOG_A(NR_RRC, "[--] " UE_LOG_FMT " " fmt, UE_LOG_ARGS(ue_context) __VA_OPT__(,) __VA_ARGS__)
#define LOG_UE_UL_EVENT(ue_context, fmt, ...) LOG_A(NR_RRC, "[UL] " UE_LOG_FMT " " fmt, UE_LOG_ARGS(ue_context) __VA_OPT__(,) __VA_ARGS__)

#include "nr_rrc_proto.h" //should be put here otherwise compilation error

#endif
