/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief MAC data structures, constant, and function prototype
 */

/*@}*/

#ifndef __LAYER2_NR_MAC_GNB_H__
#define __LAYER2_NR_MAC_GNB_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <pthread.h>
#include "fsn.h"
#include "common/utils/ds/seq_arr.h"
#include "common/utils/nr/nr_common.h"
#include "common/utils/ds/byte_array.h"
#include "common/utils/ds/spsc_q.h"
#include "common/utils/ds/hashtable.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_configuration.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_pos_ue_context.h"

#define NR_SCHED_LOCK(lock)                                        \
  do {                                                             \
    int rc = pthread_mutex_lock(lock);                             \
    AssertFatal(rc == 0, "error while locking scheduler mutex, pthread_mutex_lock() returned %d\n", rc); \
  } while (0)

#define NR_SCHED_UNLOCK(lock)                                      \
  do {                                                             \
    int rc = pthread_mutex_unlock(lock);                           \
    AssertFatal(rc == 0, "error while locking scheduler mutex, pthread_mutex_unlock() returned %d\n", rc); \
  } while (0)

/* Commmon */
#include "COMMON/f1ap_messages_types.h"
#include "common/platform_constants.h"
#include "common/ran_context.h"
#include "collection/linear_alloc.h"

/* RRC */
#include "NR_BCCH-BCH-Message.h"
#include "NR_CellGroupConfig.h"
#include "NR_BCCH-DL-SCH-Message.h"
#include "nr_radio_config.h"

/* PHY */
#include "time_meas.h"

/* Interface */
#include "nfapi_nr_interface_scf.h"
#include "nfapi_nr_interface.h"
#include "NR_PHY_INTERFACE/NR_IF_Module.h"
#include "mac_rrc_ul.h"

/* MAC */
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "LAYER2/NR_MAC_gNB/mac_config.h"
#include "NR_TAG.h"

/* Defs */
#define MAX_NUM_BWP 5
#define MAX_NUM_CORESET 12
/*!\brief Maximum number of random access process */
#define NR_NB_RA_PROC_MAX 16 // set to 16 for multiple CFRA at the same time
#define MAX_NUM_OF_SSB 64
#define MAX_NUM_NR_PRACH_PREAMBLES 64
#define NR_MAX_SIB_LENGTH 2976 // 3GPP TS 38.331 section 5.2.1

uint8_t nr_get_rv(int rel_round);

/*! \brief NR_list_t is a "list" (of users, HARQ processes, slices, ...).
 * Especially useful in the scheduler and to keep "classes" of users. */
typedef struct {
  int head;
  int *next;
  int tail;
  int len;
} NR_list_t;

typedef enum {
  nrRA_gNB_IDLE,
  nrRA_Msg2,
  nrRA_WAIT_MsgA_PUSCH,
  nrRA_WAIT_Msg3,
  nrRA_Msg3_retransmission,
  nrRA_Msg4,
  nrRA_MsgB,
  nrRA_WAIT_Msg4_MsgB_ACK,
} RA_gNB_state_t;

static const char *const nrra_text[] =
    {"IDLE", "Msg2", "WAIT_MsgA_PUSCH", "WAIT_Msg3", "Msg3_retransmission", "Msg4", "MsgB", "WAIT_Msg4_MsgB_ACK"};

typedef enum nr_periodic_channel_e {
  CSI_RS,
  CSI_MEASUREMENTS,
  SRS
} nr_periodic_channel_t;

typedef struct {
  int idx;
  bool new_beam;
} NR_beam_alloc_t;

/** Pending CN paging record */
typedef struct {
  /// UE_ID for PF/PO computation (TS 38.304 §7.1)
  uint16_t ue_id;
  /// ng-5G-S-TMSI (48 bits)
  uint64_t fiveg_s_tmsi;
} nr_mac_pcch_record_t;

typedef struct nr_pdsch_AntennaPorts_t {
  int N1;
  int N2;
  int XP;
} nr_pdsch_AntennaPorts_t;

typedef struct nr_mac_timers {
  int sr_ProhibitTimer;
  int sr_TransMax;
  int sr_ProhibitTimer_v1700;
  int t300;
  int t301;
  int t310;
  int n310;
  int t311;
  int n311;
  int t319;
} nr_mac_timers_t;

typedef struct nr_redcap_config {
  int8_t cellBarredRedCap1Rx_r17;
  int8_t cellBarredRedCap2Rx_r17;
  uint8_t intraFreqReselectionRedCap_r17;
} nr_redcap_config_t;

typedef struct {
int dl_FreqDensity0_0;
int dl_FreqDensity1_0;
int dl_TimeDensity0_0;
int dl_TimeDensity1_0;
int dl_TimeDensity2_0;
int dl_EpreRatio_0;
int dl_ReOffset_0;
int ul_FreqDensity0_0;
int ul_FreqDensity1_0;
int ul_TimeDensity0_0;
int ul_TimeDensity1_0;
int ul_TimeDensity2_0;
int ul_ReOffset_0;
int ul_MaxPorts_0;
int ul_Power_0;
} nr_ptrs_config_t;

typedef struct {
  int id;
  int scs;
  int location_and_bw;
} nr_bwp_config_t;

typedef enum {
  SSB_RSRP,
  CRI_RSRP,
  SSB_SINR,
} nr_config_report_type_t;

typedef struct nr_beam_table {
  int num_weights_per_beam;
  int num_beams;
  uint16_t *beam_ids;
  double complex **beam_weights;
} nr_beam_table_t;

/// Physical antenna counts advertised to an Aerial L1 in CONFIG.request
/// (numTxAnt/numRxAnt), decoupled from the logical antenna ports carried in
/// numTxPort/numRxPort; 0 = derive from the logical ports as before
typedef struct nr_aerial_config {
  int num_tx_ant;
  int num_rx_ant;
} nr_aerial_config_t;

typedef struct nr_power_config {
  /// target SNR
  int target_snrx10;
  /// RSSI threshold for power control. Limits power control commands when RSSI reaches threshold.
  int rssi_threshold;
  /// Failure threshold (compared to consecutive PUSCH DTX)
  int failure_thres;
} nr_power_config_t;

typedef enum nr_srs_type_e {
  NO_SRS,
  PERIODIC_SRS,
  APERIODIC_SRS,
} nr_srs_type_t;

typedef enum {
  /// no beamforming, each logical antenna port maps straight onto a physical one
  BF_METHOD_STRAIGHT_WIRE = 0,
  /// each beam is one or more logical antenna ports
  /// OAI-specific, to be removed once the digital beam table is fully supported
  BF_METHOD_DAS,
  /// L2 signals beam IDs to L1, resolved through the digital beam table if there is one
  BF_METHOD_PREDEFINED,
  /// SRS-based precoding, not implemented yet
  BF_METHOD_DYNAMIC,
} nr_bf_method_e;

typedef struct {
  int beam_duration; // in slots
  int beams_per_period;
  nr_bf_method_e bf_method;
  /// beam IDs are consumed by the RU or the fronthaul rather than resolved by L1
  bool beam_id_to_ru;
} NR_beam_config_t;

/// Max number of physical RU antenna ports the gNB can be configured with.
/// Independent of MAX_NUM_SPATIAL_STREAMS (the NFAPI per-PDU wire limit).
#define NR_MAC_MAX_RU_ANTENNA_PORTS 64

typedef struct nr_mac_config_s {
  nr_pdsch_AntennaPorts_t pdsch_AntennaPorts;
  int pusch_AntennaPorts;
  int minRXTXTIME;
  int do_CSIRS;
  nr_srs_type_t do_SRS;
  int do_TCI;
  int max_num_rsrp;
  bool force_256qam_off;
  bool force_UL256qam_off;
  bool use_deltaMCS;
  int maxMIMO_layers;
  bool disable_harq;
  nr_power_config_t pusch;
  /// SNR threshold needed to put or not a PRB in the black list
  int ul_prbblack_SNR_threshold;
  nr_power_config_t pucch;
  nr_aerial_config_t aerial;
  nr_mac_timers_t timer_config;
  int num_dlharq;
  int num_ulharq;
  // BWP information
  int num_additional_bwps;
  int first_active_bwp;
  nr_bwp_config_t bwp_config[4];
  /// beam IDs statically allocated to SSB/PRACH, one per transmitted SSB
  int num_ssb_beams;
  int32_t *ssb_beams;
  int num_agg_level_candidates[NUM_PDCCH_AGG_LEVELS];
  nr_redcap_config_t *redcap;
  nr_ptrs_config_t *ptrs;
  nr_config_report_type_t report_type;
  NR_beam_config_t beam_config;
  nr_beam_table_t bt;
  /// Spatial stream indexing for mapping onto RU ports. Needed for MU-MIMO.
  /// Sized to the max number of physical RU antenna ports, NOT to be confused
  /// with MAX_NUM_SPATIAL_STREAMS, which bounds the per-PDU NFAPI wire field.
  uint16_t spatial_stream_index[NR_MAC_MAX_RU_ANTENNA_PORTS];
} nr_mac_config_t;

typedef struct NR_preamble_ue {
  uint8_t num_preambles;
  uint8_t preamble_list[MAX_NUM_NR_PRACH_PREAMBLES];
} NR_preamble_ue_t;

typedef struct NR_sched_pdcch {
  uint16_t BWPSize;
  uint16_t BWPStart;
  uint8_t CyclicPrefix;
  uint8_t SubcarrierSpacing;
  uint8_t StartSymbolIndex;
  uint8_t CceRegMappingType;
  uint8_t RegBundleSize;
  uint8_t InterleaverSize;
  uint16_t ShiftIndex;
  uint8_t DurationSymbols;
  uint16_t n_rb;
  uint16_t rb_start;
} NR_sched_pdcch_t;

/*! \brief gNB template for the Random access information */
typedef struct {
  /// Flag to indicate this process is active
  RA_gNB_state_t ra_state;
  /// CORESET0 configured flag
  int coreset0_configured;
  /// Frame where preamble was received
  int preamble_frame;
  /// Slot where preamble was received
  uint8_t preamble_slot;
  /// Received preamble_index
  uint8_t preamble_index;
  /// Timing offset indicated by PHY
  int16_t timing_offset;
  /// Subframe where Msg2 is to be sent
  uint8_t Msg2_slot;
  /// Frame where Msg2 is to be sent
  frame_t Msg2_frame;
  /// Subframe where Msg3 is to be sent
  slot_t Msg3_slot;
  /// Frame where Msg3 is to be sent
  frame_t Msg3_frame;
  /// Msg3 time domain allocation index
  int Msg3_tda_id;
  /// Msg3 beam matrix index
  NR_beam_alloc_t Msg3_beam;
  /// harq_pid used for Msg4 transmission
  uint8_t harq_pid;
  /// RA RNTI allocated from received PRACH
  uint16_t RA_rnti;
  /// MsgB RNTI allocated from received MsgA
  uint16_t MsgB_rnti;
  /// Received UE Contention Resolution Identifier
  uint8_t cont_res_id[6];
  /// Msg3 first RB
  int msg3_first_rb;
  /// Msg3 number of RB
  int msg3_nb_rb;
  /// Msg3 BWP start
  int msg3_bwp_start;
  /// Msg3 TPC command
  uint8_t msg3_TPC;
  /// Round of Msg3 HARQ
  uint8_t msg3_round;
  int msg3_startsymb;
  int msg3_nbSymb;
  /// MAC PDU length for Msg4
  int mac_pdu_length;
  /// Preambles for contention-free access
  NR_preamble_ue_t preambles;
  int contention_resolution_timer;
  nr_ra_type_t ra_type;
  /// CFRA flag
  bool cfra;
} NR_RA_t;

/*! \brief gNB common channels */
typedef struct {
  frame_type_t frame_type;
  NR_BCCH_BCH_Message_t *mib;
  NR_BCCH_DL_SCH_Message_t *sib1;
  seq_arr_t *du_SIBs;
  NR_ServingCellConfigCommon_t *ServingCellConfigCommon;
  /// Outgoing MIB PDU for PHY
  uint8_t MIB_pdu[3];
  /// Outgoing BCCH pdu for PHY
  uint8_t sib1_bcch_pdu[NR_MAX_SIB_LENGTH / 8];
  int sib1_bcch_length;
  /// used for otherSIB data
  uint8_t other_sib_bcch_pdu[2][NR_MAX_SIB_LENGTH / 8];
  int other_sib_bcch_length[2];
  /// VRB map for common channels
  uint16_t vrb_map[MAX_NUM_BEAM_PERIODS][275];
  /// VRB map for common channels and PUSCH, dynamically allocated because
  /// length depends on number of slots and RBs
  uint16_t *vrb_map_UL[MAX_NUM_BEAM_PERIODS];
  ///Number of active SSBs
  int num_active_ssb;
  //Total available prach occasions per configuration period
  int total_prach_occasions_per_config_period;
  //Total available prach occasions
  int total_prach_occasions;
  //Max Association period
  int association_period;
  //SSB index
  uint8_t ssb_index[MAX_NUM_OF_SSB];
  //CB preambles for each SSB
  int cb_preambles_per_ssb;
  /// Max prach length in slots
  int prach_len;
  nr_prach_info_t prach_info;
  /// PCCH SDU queue (one spsc_q per common-channel context)
  spsc_q_t pcch_queue;
} NR_COMMON_channels_t;

// SP ZP CSI-RS Resource Set Activation/Deactivation MAC CE
typedef struct sp_zp_csirs {
  bool is_scheduled;     //ZP CSI-RS ACT/Deact MAC CE is scheduled
  bool act_deact;        //Activation/Deactivation indication
  uint8_t serv_cell_id;  //Identity of Serving cell for which MAC CE applies
  uint8_t bwpid;         //Downlink BWP id
  uint8_t rsc_id;        //SP ZP CSI-RS resource set
} sp_zp_csirs_t;

//SP CSI-RS / CSI-IM Resource Set Activation/Deactivation MAC CE
#define MAX_CSI_RESOURCE_SET 64
typedef struct csi_rs_im {
  bool is_scheduled;
  bool act_deact;
  uint8_t serv_cellid;
  uint8_t bwp_id;
  bool im;
  uint8_t csi_im_rsc_id;
  uint8_t nzp_csi_rsc_id;
  uint8_t nb_tci_resource_set_id;
  uint8_t tci_state_id [ MAX_CSI_RESOURCE_SET ];
} csi_rs_im_t;

typedef struct tciStateInd {
  bool is_scheduled;
  uint32_t coresetId;
  uint32_t tciStateId;
} tciStateInd_t;

typedef struct pucchSpatialRelation {
  bool is_scheduled;
  uint8_t servingCellId;
  uint8_t bwpId;
  uint8_t pucchResourceId;
  bool s0tos7_actDeact[8];
} pucchSpatialRelation_t;

typedef struct SPCSIReportingpucch {
  bool is_scheduled;
  uint8_t servingCellId;
  uint8_t bwpId;
  bool s0tos3_actDeact[4];
} SPCSIReportingpucch_t;

#define MAX_APERIODIC_TRIGGER_STATES 128 //38.331                               
typedef struct aperiodicCSI_triggerStateSelection {
  bool is_scheduled;
  uint8_t servingCellId;
  uint8_t bwpId;
  uint8_t highestTriggerStateSelected;
  bool triggerStateSelection[MAX_APERIODIC_TRIGGER_STATES];
} aperiodicCSI_triggerStateSelection_t;

#define MAX_TCI_STATES 128 //38.331                                             
typedef struct pdschTciStatesActDeact {
  bool is_scheduled;
  uint8_t servingCellId;
  uint8_t bwpId;
  uint8_t highestTciStateActivated;
  bool tciStateActDeact[MAX_TCI_STATES];
  uint8_t codepoint[8];
} pdschTciStatesActDeact_t;

typedef struct UE_info {
  sp_zp_csirs_t sp_zp_csi_rs;
  csi_rs_im_t csi_im;
  tciStateInd_t tci_state_ind;
  pucchSpatialRelation_t pucch_spatial_relation;
  SPCSIReportingpucch_t SP_CSI_reporting_pucch;
  aperiodicCSI_triggerStateSelection_t aperi_CSI_trigger;
  pdschTciStatesActDeact_t pdsch_TCI_States_ActDeact;
} NR_UE_mac_ce_ctrl_t;

typedef struct NR_sched_pucch {
  bool active;
  int frame;
  int ul_slot;
  bool sr_flag;
  int csi_bits;
  bool simultaneous_harqcsi;
  uint8_t dai_c;
  uint8_t timing_indicator;
  uint8_t resource_indicator;
  int r_pucch;
  int prb_start;
  int second_hop_prb;
  int nr_of_symb;
  int start_symb;
  int beam_idx;
} NR_sched_pucch_t;

typedef struct NR_pusch_dmrs {
  uint8_t N_PRB_DMRS;
  uint8_t num_dmrs_symb;
  uint16_t ul_dmrs_symb_pos;
  uint8_t num_dmrs_cdm_grps_no_data;
  uint16_t dmrs_ports;
  nfapi_nr_dmrs_type_e dmrs_config_type;
  int dmrs_scrambling_id;
  int pusch_identity;
  int scid;
  int low_papr_sequence_number;
  NR_PTRS_UplinkConfig_t *ptrsConfig;
} NR_pusch_dmrs_t;

typedef struct NR_sched_pusch {
  int frame;
  int slot;

  /// RB allocation within active uBWP
  uint16_t rbSize;
  uint16_t rbStart;

  /// MCS
  uint8_t mcs;

  /// TBS-related info
  uint16_t R;
  uint8_t Qm;
  uint32_t tb_size;

  /// UL HARQ PID to use for this UE, or -1 for "any new"
  int8_t ul_harq_pid;
  uint8_t nrOfLayers;
  int tpmi;

  // time_domain_allocation is the index of a list of tda
  int time_domain_allocation;
  NR_tda_info_t tda_info;
  NR_pusch_dmrs_t dmrs_info;
  bwp_info_t bwp_info;
  int phr_txpower_calc;

  /// TPC command for this PUSCH
  int tpc_pusch;
  // Antenna ports to use
  nfapi_nr_spatial_stream_index_t ant_port_idx;
  // Antenna port for PUSCH DCI
  uint16_t dci_ant_idx;
} NR_sched_pusch_t;

typedef struct NR_pdsch_dmrs {
  uint8_t dmrs_ports_id;
  uint8_t N_PRB_DMRS;
  uint8_t N_DMRS_SLOT;
  uint16_t dl_dmrs_symb_pos;
  uint8_t numDmrsCdmGrpsNoData;
  uint32_t scrambling_id;
  int n_scid;
  nfapi_nr_dmrs_type_e dmrsConfigType;
  NR_PTRS_DownlinkConfig_t *phaseTrackingRS;
} NR_pdsch_dmrs_t;

struct NR_UE_info;
struct nr_cell_sched_s;
typedef void (*feedback_action_t)(struct nr_cell_sched_s *cell, struct NR_UE_info *ue);

typedef enum {
  PDSCH_TYPE0 = 0,
  PDSCH_TYPE1 = 1
} nr_pdsch_allocation_type_t;

typedef struct NR_sched_pdsch {
  /// RB allocation within active BWP
  nr_pdsch_allocation_type_t alloc_type;
  uint16_t rbSize;
  uint16_t rbStart;
  uint8_t rbBitmap[36];

  /// MCS-related infos
  uint8_t mcs;

  /// TBS-related info
  uint16_t R;
  uint8_t Qm;
  uint32_t tb_size;

  /// DL HARQ PID to use for this UE, or -1 for "any new"
  int8_t dl_harq_pid;

  // pucch format allocation
  int16_t pucch_allocation;

  uint16_t pm_index;
  uint8_t nrOfLayers;
  bwp_info_t bwp_info;
  NR_pdsch_dmrs_t dmrs_parms;
  // time_domain_allocation is the index of a list of tda
  int time_domain_allocation;
  NR_tda_info_t tda_info;
  feedback_action_t action;
  // Baseband ports to use
  nfapi_nr_spatial_stream_index_t ant_port_idx;
} NR_sched_pdsch_t;

typedef struct NR_UE_harq {
  bool is_waiting;
  uint8_t ndi;
  uint8_t round;
  uint16_t feedback_frame;
  uint16_t feedback_slot;

  /* Transport block to be sent using this HARQ process */
  byte_array_t transportBlock;
  uint32_t tb_size;  // size of currently stored TB
  bool start_tci_timer;
  /// sched_pdsch keeps information on MCS etc used for the initial transmission
  NR_sched_pdsch_t sched_pdsch;
} NR_UE_harq_t;

//! fixme : need to enhace for the multiple TB CQI report

typedef struct NR_bler_stats {
  frame_t last_frame;
  float bler;
  uint8_t mcs;
  uint64_t rounds[8];
  int last_num_sched; // scheduling count at last BLER update (for activity guard)
} NR_bler_stats_t;

//
/*! As per spec 38.214 section 5.2.1.4.2
 * - if the UE is configured with the higher layer parameter groupBasedBeamReporting set to 'disabled', the UE shall report in
  a single report nrofReportedRS (higher layer configured) different CRI or SSBRI for each report setting.
 * - if the UE is configured with the higher layer parameter groupBasedBeamReporting set to 'enabled', the UE shall report in a
  single reporting instance two different CRI or SSBRI for each report setting, where CSI-RS and/or SSB
  resources can be received simultaneously by the UE either with a single spatial domain receive filter, or with
  multiple simultaneous spatial domain receive filter
*/
#define MAX_NR_OF_REPORTED_RS 4

struct CRI_RI_LI_PMI_CQI {
  uint8_t cri;
  uint8_t ri;
  uint8_t li;
  uint8_t pmi_x1;
  uint8_t pmi_x2;
  uint8_t wb_cqi_1tb;
  uint8_t wb_cqi_2tb;
  uint8_t cqi_table;
  uint8_t csi_report_id;
  bool print_report;
};

typedef struct {
  uint8_t resource_id;
  int RSRP;
  int SINRx10;
} RSRP_report_t;

typedef struct {
  int nb;
  RSRP_report_t r[MAX_NR_OF_REPORTED_RS];
} RSRP_report_list_t;

typedef struct CSI_Report {
  struct CRI_RI_LI_PMI_CQI cri_ri_li_pmi_cqi_report;
  RSRP_report_list_t ssb_rsrp_report;
  RSRP_report_list_t csirs_rsrp_report;
} CSI_report_t;

typedef enum {
  INACTIVE = 0,
  ACTIVE_NOT_SCHED,
  ACTIVE_SCHED
} NR_UL_harq_states_t;

typedef struct NR_UE_ul_harq {
  bool is_waiting;
  uint8_t ndi;
  uint8_t round;
  uint16_t feedback_slot;

  /// sched_pusch keeps information on MCS etc used for the initial transmission
  NR_sched_pusch_t sched_pusch;
} NR_UE_ul_harq_t;

typedef struct NR_QoS_config_s {
  int fiveQI;
  int priority;
} NR_QoS_config_t;

typedef struct nr_lc_config {
  uint8_t lcid;
  /// flag if corresponding RB is suspended
  bool suspended;
  /// priority as specified in 38.321
  int priority;
  /// associated NSSAI for DRB
  nssai_t nssai;
  /// QoS config for DRB
  NR_QoS_config_t qos_config[NR_MAX_NUM_QFI];
} nr_lc_config_t;

typedef struct nr_power_control {
  float avg_snr; /// average SNR (in dB)
  int target_snrx10; /// UE-specific target SNR x10
  float avg_rssi; /// average RSSI
  int rssi_threshold; /// UE-specific RSSI threshld in 0.1dBm/dBFS, range -1280 to 0
  float tpc_in_flight; /// TPCs applied by UE but not yet in average SNR
} nr_power_control_t;

typedef struct {
  long *aperiodic_slotOffset;
  long aperiodic_ResourceTrigger;
  NR_timer_t aperiodic_srs_timer;
} NR_sched_aperiodic_srs_t;

typedef struct {
  int periodic_offset;
} NR_sched_periodic_srs_t;

typedef struct {
  long usage;
  NR_SRS_Resource_t *srs_resource;
  union {
    NR_sched_aperiodic_srs_t aperiodic_sched;
    NR_sched_periodic_srs_t periodic_sched;
  };
} NR_sched_srs_t;

typedef struct {
  int candidate_beam_index; // beam currently being considered for switching (-1 = none)
  int consecutive_count; // how many consecutive reports have selected candidate_beam_index
} beam_hysteresis_t;

/*! \brief scheduling control information set through an API */
typedef struct {
  /// CCE index and aggregation, should be coherent with cce_list
  NR_SearchSpace_t *search_space;
  NR_ControlResourceSet_t *coreset;
  NR_sched_pdcch_t sched_pdcch;

  /// CCE index and Aggr. Level are shared for PUSCH/PDSCH allocation decisions
  /// corresponding to the sched_pusch/sched_pdsch structures below
  int cce_index;
  int aggregation_level;
  uint32_t dl_cce_fail, ul_cce_fail;

  /// Array of PUCCH scheduling information
  /// Its size depends on TDD configuration and max feedback time
  /// There will be a structure for each UL slot in the active period determined by the size
  NR_sched_pucch_t *sched_pucch;
  int sched_pucch_size;

  /// uplink bytes that are currently scheduled
  int sched_ul_bytes;
  /// estimation of the UL buffer size
  int estimated_ul_buffer;

  /// PHR info: power headroom level (dB)
  int ph;

  /// PHR info: nominal UE transmit power levels (dBm)
  int pcmax;

  /// UE-estimated maximum MCS (from CSI-RS)
  uint8_t dl_max_mcs;

  /// For UL synchronization: store last UL scheduling grant
  frame_t last_ul_frame;
  slot_t last_ul_slot;

  /// total amount of data awaiting for this UE
  uint32_t num_total_bytes;
  /// per-LC status data
  mac_rlc_status_resp_t rlc_status[NR_MAX_NUM_LCID];

  /// Estimation of HARQ from BLER
  NR_bler_stats_t dl_bler_stats;
  NR_bler_stats_t ul_bler_stats;

  uint16_t ta_frame;
  int16_t ta_update;
  bool ta_apply;
  int pusch_consecutive_dtx_cnt;
  int pucch_consecutive_dtx_cnt;
  bool ul_failure;
  int ul_failure_timer;
  int release_timer;
  CSI_report_t CSI_report;
  /// number of SR received for this UE since the last UL grant
  uint8_t sr_cnt;
  beam_hysteresis_t beam_hysteresis;
  /// information about every HARQ process
  NR_UE_harq_t harq_processes[NR_MAX_HARQ_PROCESSES];
  /// HARQ processes that are free
  NR_list_t available_dl_harq;
  /// HARQ processes that await feedback
  NR_list_t feedback_dl_harq;
  /// HARQ processes that await retransmission
  NR_list_t retrans_dl_harq;
  /// information about every UL HARQ process
  NR_UE_ul_harq_t ul_harq_processes[NR_MAX_HARQ_PROCESSES];
  /// UL HARQ processes that are free
  NR_list_t available_ul_harq;
  /// UL HARQ processes that await feedback
  NR_list_t feedback_ul_harq;
  /// UL HARQ processes that await retransmission
  NR_list_t retrans_ul_harq;
  NR_UE_mac_ce_ctrl_t UE_mac_ce_ctrl; // MAC CE related information

  /// Timer for RRC processing procedures and transmission activity
  NR_timer_t transm_interrupt;
  NR_timer_t tci_beam_switch;

  /// Timer for timeout before UE is set to UL failure (e.g.,
  /// "TransmissionActionIndicator" handling
  NR_timer_t transm_timeout;

  /// sri, ul_ri and tpmi based on SRS
  nr_srs_feedback_t srs_feedback;

  /// per-LC configuration
  seq_arr_t lc_config;

  // pdcch closed loop adjust for PDCCH aggregation level, range <0, 1>
  // 0 - good channel, 1 - bad channel
  float pdcch_cl_adjust;

  nr_power_control_t pusch_pc;
  nr_power_control_t pucch_pc;
} NR_UE_sched_ctrl_t;

typedef struct NR_mac_dir_stats {
  uint64_t lc_bytes[64];
  uint64_t rounds[8];
  uint64_t errors;
  uint64_t total_bytes;
  uint32_t current_bytes;
  uint64_t total_sdu_bytes;
  uint32_t total_rbs;
  uint32_t total_rbs_retx;
  uint32_t num_mac_sdu;
  uint32_t current_rbs;
  uint64_t prev_sdu_bytes;
  frame_t last_goodput_frame;
} NR_mac_dir_stats_t;

typedef struct NR_mac_stats {
  NR_mac_dir_stats_t dl;
  NR_mac_dir_stats_t ul;
  uint32_t ulsch_DTX;
  uint64_t ulsch_total_bytes_scheduled;
  uint32_t pucch0_DTX;
  int cumul_rsrp;
  uint32_t num_rsrp_meas;
  int cumul_sinrx10;
  uint32_t num_sinr_meas;
  char srs_stats[50]; // Statistics may differ depending on SRS usage
  int deltaMCS;
  int NPRB;
} NR_mac_stats_t;

typedef struct NR_bler_options {
  double upper;
  double lower;
  uint8_t min_mcs;
  uint8_t max_mcs;
  uint8_t harq_round_max;
} NR_bler_options_t;

typedef struct nr_mac_rrc_ul_if_s {
  f1_reset_du_initiated_func_t f1_reset;
  f1_reset_acknowledge_cu_initiated_func_t f1_reset_acknowledge;
  f1_setup_request_func_t f1_setup_request;
  gnb_du_configuration_update_t gnb_du_configuration_update;
  ue_context_setup_response_func_t ue_context_setup_response;
  ue_context_modification_response_func_t ue_context_modification_response;
  ue_context_modification_required_func_t ue_context_modification_required;
  ue_context_release_request_func_t ue_context_release_request;
  ue_context_release_complete_func_t ue_context_release_complete;
  initial_ul_rrc_message_transfer_func_t initial_ul_rrc_message_transfer;
  trp_information_response_func_t trp_information_response;
  trp_information_failure_func_t trp_information_failure;
  positioning_information_response_func_t positioning_information_response;
  positioning_activation_response_func_t positioning_activation_response;
  positioning_measurement_response_func_t positioning_measurement_response;
  positioning_measurement_failure_func_t positioning_measurement_failure;
} nr_mac_rrc_ul_if_t;

typedef struct measgap_config {
  bool enable;
  int mgrp_ms;
  long mgrp;
  long gapOffset;
  long mgta;
  int n_slots_mgta;
  int n_slots_advance;
  float mgl_ms;
  long mgl;
  int mgl_slots;
} measgap_config_t;

typedef enum {
  NO_TRIGGER,
  MSG3_CRNTI,
  BWP_SWITCH,
  BEAM_SWITCH
} reconfig_trigger_state_t;

typedef struct {
  int new_state;
  reconfig_trigger_state_t trigger_info;
} context_modification_info_t;

/*! \brief UE list used by gNB to order UEs/CC for scheduling*/
typedef struct NR_UE_info {
  rnti_t rnti;
  uid_t uid; // unique ID of this UE
  /// scheduling control info
  nr_csi_report_t csi_report_template[MAX_CSI_REPORTCONFIG];
  NR_UE_sched_ctrl_t UE_sched_ctrl;
  NR_sched_srs_t sched_srs;
  NR_UE_DL_BWP_t current_DL_BWP;
  NR_UE_UL_BWP_t current_UL_BWP;
  NR_UE_ServingCell_Info_t sc_info;
  NR_mac_stats_t mac_stats;
  /// currently active CellGroupConfig
  NR_CellGroupConfig_t *CellGroup;
  /// in case of reconfiguration, new CellConfig to apply
  NR_CellGroupConfig_t *reconfigCellGroup;
  NR_UE_NR_Capability_t *capability;
  NR_feature_set_ids_t uecap_fs_ids;
  measgap_config_t measgap_config;
  // UE selected beam index
  uint16_t UE_beam_index;
  /// Per-SSB L1-RSRP table, indexed by SSB index (resource_id from CSI report).
  /// INT16_MIN means no measurement received yet for that beam.
  int16_t beam_rsrp[MAX_NUM_OF_SSB];
  /// Per-SSB L1-SINR×10 table, same indexing. INT16_MIN means no data.
  int16_t beam_sinr[MAX_NUM_OF_SSB];
  float ul_thr_ue;
  float dl_thr_ue;
  float ul_thr_ue_display; ///< slow EWMA for stable display (alpha=0.001)
  float dl_thr_ue_display; ///< slow EWMA for stable display (alpha=0.001)
  long pdsch_HARQ_ACK_Codebook;
  bool is_redcap;
  bool reestablish_rlc;
  /// set when UE is moved to BWP0 for RA, switch back to first_active_bwp after next RRCReconfigurationComplete
  bool pending_bwp_restore;
  NR_RA_t *ra;
  // 3GPP mandates that BWPs are enumerated consecutively, but we only send one (dedicated)
  // BWP to the UE (and modify that BWP on reconfiguration); consequently, the BWP ID for a
  // dedicated BWP is always 1 from the UE's point of view, even if the gNB has multiple BWPs.
  // The below ID is the "true" (non-consecutive) BWP ID from the gNB's point of view
  NR_BWP_Id_t local_bwp_id;
  context_modification_info_t cm_info;
  struct nr_cell_sched_s *pcell; ///< primary serving cell for this UE; used to filter candidates per cell
  /* if nrdc_mode is true then the DU is an SCG DU */
  bool nrdc_mode;
} NR_UE_info_t;

typedef struct {
  /// scheduling control info
  seq_arr_t connected_ues_list; // has pointers to NR_UE_info_t
  hash_table_t connected_ues; // has pointers to NR_UE_info_t
  seq_arr_t access_ue_list; // has pointers to NR_UE_info_t
  // bitmap of CSI-RS already scheduled in current slot
  int sched_csirs;
  uid_allocator_t uid_allocator;
} NR_UEs_t;

typedef struct {
  /// list of allocated beams per period
  int16_t **beam_allocation;
  int beam_allocation_size;

  NR_beam_config_t config;
} NR_beam_info_t;

// We store pointers to NR_UE_info_t in seq_arr, to which the iterator
// is a pointer (pointer to pointer), so reimplement loop to get the
// "normal" pointer
#define FOR_EACH_LIST_INDIR(lIsT, VaR)                            \
  for (NR_UE_info_t **VaR##pptr = seq_arr_front(lIsT), *VaR; \
       VaR##pptr != seq_arr_end(lIsT) && (VaR = *VaR##pptr); \
       VaR##pptr = seq_arr_next(lIsT, VaR##pptr))

#define FOR_EACH_RA_UE(Ue_InFo, VaR) FOR_EACH_LIST_INDIR(&(Ue_InFo)->access_ue_list, VaR)

#define FOR_EACH_CONNECTED_UE(Ue_InFo, VaR) FOR_EACH_LIST_INDIR(&(Ue_InFo)->connected_ues_list, VaR)

#define FOR_EACH_CANDIDATE(VaR, ArR, N) for (__typeof__(*(ArR)) *VaR = (ArR); VaR < (ArR) + (N); VaR++)

typedef struct {
  /// current frame
  frame_t frame;
  /// current slot
  slot_t slot;
  /// FAPI DL req in which allocations are made
  nfapi_nr_dl_tti_request_body_t *dl_req;
  /// TX_data request holds the actual data
  nfapi_nr_tx_data_request_t *TX_req;
} post_process_pdsch_t;

typedef struct {
  /// current frame for DCI
  frame_t frame;
  /// current slot for DCI
  slot_t slot;
  /// FAPI UL_DCI.request in which allocations are to be made
  nfapi_nr_ul_dci_request_t *ul_dci_req;
  /// group PDCCH PDU per CORESET
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_coreset[MAX_NUM_CORESET];
} post_process_pusch_t;

/* forward declarations for scheduling types */
struct gNB_MAC_INST_s;
typedef struct nr_dl_candidate nr_dl_candidate_t;

/// Scheduling context passed to the RB allocation policy.
/// Contains per-beam VRB maps so the policy can handle beam partitioning
/// internally (e.g. for MU-MIMO cross-beam scheduling).
typedef struct nr_dl_sched_params nr_dl_sched_params_t;
struct nr_dl_sched_params {
  struct gNB_MAC_INST_s *mac; ///< MAC instance (for CCE/PUCCH validation)
  struct nr_cell_sched_s *cell;
  frame_t frame;
  slot_t slot;
  int num_beams; ///< number of beams
  int max_num_ue; ///< max UEs to schedule
  uint16_t *vrb_map[MAX_NUM_BEAM_PERIODS]; ///< per-beam VRB maps [275], mutable
  int n_rb_avail[MAX_NUM_BEAM_PERIODS]; ///< available RBs per beam
  int min_mcs; ///< minimum MCS from BLER config
  float bler_lower; ///< BLER lower threshold (increase MCS if below)
  float bler_upper; ///< BLER upper threshold (decrease MCS if above)
};

/// Per-UE scheduling candidate — read-only inputs for the policy function.
struct nr_dl_candidate {
  /* ── UE identity / scheduling state (set by collect, never modified after) ── */
  NR_UE_info_t *UE;
  uint16_t rnti; ///< UE RNTI (convenience, avoids UE pointer dereference)
  bool is_retx; ///< true = HARQ retransmission pending
  int8_t retx_harq_pid; ///< HARQ PID for retx, -1 if none
  int retx_rbSize; ///< RBs needed for retx, 0 for new tx
  uint32_t pending_bytes; ///< total bytes waiting in RLC buffers
  uint32_t pending_bytes_per_lcid[NR_MAX_NUM_LCID]; ///< per-LCID bytes waiting in RLC buffers
  float avg_throughput; ///< EWMA goodput in bps (dl_thr_ue)
  float bler; ///< current BLER estimate
  int current_mcs; ///< current MCS state (retx: from HARQ, new tx: from BLER tracker)
  int max_mcs; ///< max allowed MCS (config + UE capability)
  int last_num_sched; ///< scheduled occasions in last BLER window
  bool bler_updated; ///< true if BLER was refreshed this frame
  int mcs_table; ///< MCS table index (from BWP config)
  int bwp_start; ///< UE's BWP start
  int bwp_size; ///< UE's BWP size
  uint64_t fiveQI; ///< 5QI from first DRB's QoS config (0 if none)
  int priority; ///< LC priority from first DRB (lower = higher priority, 0 if none)
  nssai_t nssai; ///< slice/service type/differentiator from first DRB

  bool skipped; ///< true if dropped by TDA/beam select (skip in downstream stages)
  bool scheduled; ///< true if accepted by the RB-allocation policy

  /* ── UE CSI observations (set by collect, read-only after) ─────────────── */
  uint16_t cqi; ///< UE-reported wideband CQI
  uint8_t csi_ri; ///< UE-reported rank indicator (0 = rank-1); forced to 0 for DCI 1_0
  int csi_pm_index; ///< PM index derived from UE-reported PMI + antenna config
  const int16_t *beam_rsrp; ///< per-SSB L1-RSRP; points into NR_UE_info_t::beam_rsrp. INT16_MIN = no data
  const int16_t *beam_sinr; ///< per-SSB L1-SINR×10; same indexing. INT16_MIN = no data

  /* ── gNB decisions (written by the named pipeline stage) ───────────────── */
  /* Use NR_sched_pdsch_t for fields shared with HARQ/dispatch (mcs, rbStart,
   * rbSize, nrOfLayers, pm_index, tda, tda_info, pucch_allocation).*/
  NR_sched_pdsch_t sched_pdsch;
  uint16_t alloc_slbitmap; ///< symbol bitmap derived from sched_pdsch.tda_info
  /* dl_beam_select: beam selection */
  int alloc_beam_dir; ///< beam direction index (initialised from UE->UE_beam_index; may be overridden)
  int alloc_beam_idx; ///< hardware beam structure index, set by beam_allocation_procedure
  bool alloc_new_beam; ///< true if beam alloc claimed a fresh slot (release via reset_beam_status if unscheduled)
  /* commit_alloc: CCE/PUCCH validation */
  int alloc_cce_index; ///< CCE index for PDCCH
  int alloc_aggregation_level; ///< PDCCH aggregation level
  NR_sched_pdcch_t alloc_sched_pdcch; ///< PDCCH scheduling info
};

/* forward declaration to use in nr_pp_impl_dl */
struct gNB_MAC_INST_s;
typedef struct gNB_MAC_INST_s gNB_MAC_INST;
typedef struct nr_cell_sched_s nr_cell_sched_t;

typedef void (*nr_pp_impl_dl)(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pdsch_t *pp_pdsch);
typedef void (*nr_pp_impl_ul)(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pusch_t *pp_pusch);

/// RI/PMI selection: sets nrOfLayers and pm_index per candidate from CSI feedback.
/// For retransmissions, nrOfLayers must match the original transmission.
/// Custom implementations may use SRS reciprocity to override the UE's reported RI/PMI.
typedef void (*nr_dl_ri_pmi_select_fn)(const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates);

/// MCS adaptation: sets sched_pdsch.mcs from BLER state for every candidate.
/// Called for all candidates (including those that won't get scheduled) so
/// BLER-based MCS ramps even for UEs that fail CCE. Also persists the
/// decision to dl_bler_stats.mcs for continuity across slots.
typedef void (*nr_dl_mcs_select_fn)(const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates);

/// Beam allocation: assigns beam structure index to each candidate.
/// beam_index_list maps SSB id -> beam index (same as mac->beam_index_list);
/// custom implementations can use it with cand->beam_rsrp[] to pick the best beam.
typedef int (*nr_dl_beam_select_fn)(NR_beam_info_t *beam_info,
                                    const int16_t *beam_index_list,
                                    nr_dl_candidate_t *candidates,
                                    int n_candidates,
                                    frame_t frame,
                                    slot_t slot,
                                    int slots_per_frame);

/// TDA selection: assigns tda/tda_info/slbitmap per candidate.
/// Returns the number of candidates with a valid TDA (compacts invalids out).
typedef int (
    *nr_dl_tda_select_fn)(const gNB_MAC_INST *mac, const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates, frame_t frame, slot_t slot);

/// Scheduling policy: decides PRB + MCS for candidates across all beams.
/// The beam loop is inside the policy so it can do cross-beam scheduling (e.g. MU-MIMO).
/// Writes sched_pdsch.rbStart/rbSize/mcs and cce/pucch fields on scheduled candidates.
/// Sets candidate->scheduled = true for each accepted UE; returns the count.
typedef int (*nr_dl_rb_alloc_fn)(const nr_dl_sched_params_t *params, nr_dl_candidate_t *candidates, int n_candidates);

/// Per-LCID byte allocation: decides how many bytes each LCID gets within
/// the available TBS for an initial transmission.  Called during MAC PDU
/// generation.  Writes lcid_alloc[lcid] = max data bytes for that LCID.
/// The execution loop caps actual RLC requests to these budgets.
typedef void (*nr_dl_lcid_alloc_fn)(const gNB_MAC_INST *mac,
                                    const nr_dl_candidate_t *candidate,
                                    int tbs_available,
                                    int lcid_alloc[NR_MAX_NUM_LCID]);

/* UL scheduling refactored data structures */
struct nr_ul_sched_params;
typedef struct nr_ul_candidate nr_ul_candidate_t;

typedef struct nr_ul_sched_params {
  struct gNB_MAC_INST_s *mac;
  struct nr_cell_sched_s *cell;
  frame_t dci_frame; ///< DCI slot frame (current DL slot)
  slot_t dci_slot; ///< DCI slot (current DL slot)
  frame_t frame; ///< scheduled PUSCH frame (future UL slot)
  slot_t slot; ///< scheduled PUSCH slot (future UL slot)
  int num_beams;
  int max_num_ue;
  uint16_t *vrb_map_UL[MAX_NUM_BEAM_PERIODS]; ///< per-beam VRB maps, mutable
  int n_rb_avail[MAX_NUM_BEAM_PERIODS]; ///< available RBs per beam
  int min_rb;
  int min_mcs;
  float bler_lower;
  float bler_upper;
  const NR_ServingCellConfigCommon_t *scc;
  const NR_bler_options_t *bler_opts; ///< UL BLER options (for adapt_ul_mcs)
} nr_ul_sched_params_t;

struct nr_ul_candidate {
  /* ── UE identity / scheduling state (set by collect, never modified after) ── */
  NR_UE_info_t *UE;
  uint16_t rnti; ///< UE RNTI (convenience)
  bool is_retx;
  int8_t retx_harq_pid;
  int retx_rbSize;
  bool sched_long_inactivity;
  int sched_srs;
  uint32_t pending_bytes;
  float avg_throughput;
  float bler;
  int current_mcs;
  int max_mcs;
  int last_num_sched; ///< scheduling count at last BLER update (activity guard)
  bool bler_updated; ///< true if BLER was refreshed this frame
  int mcs_table;
  int bwp_start;
  int bwp_size;
  uint64_t fiveQI; ///< 5QI from first DRB's QoS config (0 if none)
  int priority; ///< LC priority from first DRB (0 if none)
  nssai_t nssai; ///< slice/service type/differentiator from first DRB
  int beam_index;

  /* ── Power control (set by collect, read-only after) ─────────────────────── */
  int ph; ///< power headroom
  int pcmax; ///< configured max TX power
  int snrx10; ///< PUSCH SINR × 10 (for SINR-based MCS in harq_round_max==1)

  bool skipped; ///< true if dropped by TDA/beam select (skip in downstream stages)
  bool scheduled; ///< true if accepted by the RB-allocation policy

  /* ── UE CSI observations (set by collect, read-only after) ─────────────── */
  uint16_t cqi; ///< wideband CQI (from CSI report)
  const int16_t *beam_rsrp; ///< per-SSB L1-RSRP; points into NR_UE_info_t::beam_rsrp. INT16_MIN = no data.
  const int16_t *beam_sinr; ///< per-SSB L1-SINR×10; same indexing. INT16_MIN = no data.

  /* ── gNB decisions (written by the named pipeline stage) ───────────────── */
  /* Use NR_sched_pusch_t for fields shared with HARQ/dispatch (mcs, rbStart,
   * rbSize, nrOfLayers, tpmi, tda, tda_info).
   * Remaining dispatch-only fields (R, Qm, tb_size, dmrs_info, bwp_info) are
   * filled at dispatch time.  Mirrors DL sched_pdsch pattern. */
  NR_sched_pusch_t sched_pusch;
  uint16_t alloc_slbitmap; ///< symbol bitmap derived from sched_pusch.tda_info
  /* ul_beam_select: beam assignment (DCI slot + PUSCH slot) */
  int alloc_dci_beam_idx; ///< beam idx for the DCI slot; -1 = failed
  int alloc_beam_idx; ///< beam idx for the scheduled PUSCH slot; -1 = failed
  bool alloc_dci_beam_new; ///< true if dci_beam was newly allocated
  bool alloc_sched_beam_new; ///< true if sched_beam was newly allocated
  /* commit_ul_alloc: CCE validation */
  int alloc_cce_index;
  int alloc_aggregation_level;
  NR_sched_pdcch_t alloc_sched_pdcch;
  /// number of SR received since the last UL grant, used as scheduling priority
  uint8_t sr_cnt;
};

typedef struct {
  uint16_t rbSize;
  uint8_t mcs;
  bool valid;
} nr_ul_phr_suggestion_t;

typedef struct {
  nr_ul_phr_suggestion_t max_mcs_min_rb;
  nr_ul_phr_suggestion_t same_rb_min_mcs;
} nr_ul_phr_advice_t;

typedef int (*nr_ul_beam_select_fn)(NR_beam_info_t *beam_info,
                                    const int16_t *beam_index_list,
                                    nr_ul_candidate_t *candidates,
                                    int n_candidates,
                                    frame_t frame,
                                    slot_t slot,
                                    frame_t sched_frame,
                                    slot_t sched_slot,
                                    int slots_per_frame);

/// UL RI/TPMI selection: sets sched_pusch.nrOfLayers and tpmi per candidate from SRS feedback.
/// Default: reads from srs_feedback (current OAI behavior).
/// Custom: joint rank/TPMI search from H matrix, ML-based, etc.
typedef void (*nr_ul_ri_tpmi_select_fn)(gNB_MAC_INST *mac, nr_ul_candidate_t *cands, int n_cand);

/// UL TDA selection: picks TDA per candidate (default: same TDA for all), validates retx
/// feasibility, drops infeasible cands (compact). Returns surviving cand count.
/// Each surviving cand has sched_pusch.time_domain_allocation/tda_info and alloc_slbitmap populated. Retx cands have retx_rbSize
/// set.
typedef int (
    *nr_ul_tda_select_fn)(gNB_MAC_INST *mac, nr_cell_sched_t *cell, nr_ul_candidate_t *cands, int n_cand, frame_t sched_frame, slot_t sched_slot, int k2);

/// MCS selection: sets sched_pusch.mcs from BLER/SINR state for every candidate.
/// Runs after beam_select so sched_pusch.nrOfLayers (for SINR lookup) and beam info are available.
/// Also persists the decision to ul_bler_stats.mcs for continuity across slots.
typedef void (*nr_ul_mcs_select_fn)(const nr_cell_sched_t *cell, nr_ul_candidate_t *candidates, int n_candidates);

/// UL scheduling policy: beam loop is inside the policy for cross-beam scheduling.
/// Sets candidate->scheduled = true for each accepted UE; returns the count.
typedef int (*nr_ul_rb_alloc_fn)(const nr_ul_sched_params_t *params, nr_ul_candidate_t *candidates, int n_candidates);

typedef struct f1_config_t {
  f1ap_setup_req_t *setup_req;
  f1ap_setup_resp_t *setup_resp;
  uint32_t gnb_id; // associated gNB's ID, not used in DU itself
} f1_config_t;

typedef struct {
  char *nvipc_shm_prefix;
  int8_t nvipc_poll_core;
  uint8_t num_phys;
} nvipc_params_t;

typedef struct {
  uint64_t total_prb_aggregate;
  uint64_t used_prb_aggregate;
} mac_stats_t;

typedef struct dlul_mac_stats {
  mac_stats_t dl;
  mac_stats_t ul;
} dlul_mac_stats_t;

typedef struct {
  NR_SearchSpace_t search_space[MAX_NUM_OF_SSB];
  NR_ControlResourceSet_t coreset;
} NR_sched_ctrl_sib1_t;

/* Dimensions of the per-cell MCS distribution histograms (3GPP TS 28.552 §5.1.1.12). */
#define NR_KPM_MAX_LAYERS       8  /* RI: 1..8                  */
#define NR_KPM_NB_MCS_TABLE_DL  3  /* PDSCH MCS tables: 1..3    */
#define NR_KPM_NB_MCS_TABLE_UL  4  /* PUSCH MCS tables: 0, 1, 3  */
#define NR_KPM_NB_MCS           32 /* MCS index: 0..31          */

/* Dimensions of the L1M.SS-RSRP histogram (3GPP TS 28.552 §5.1.1.22.1). */
#define NR_KPM_NB_SSB            64  /* SS/PBCH block index: 0..63              */
#define NR_KPM_SS_RSRP_NB_LEVELS 128 /* SS-RSRP report level: 0..127, TS 38.133 */

typedef struct NR_du_stats {
  /// cell-wide wide-band CQI distribution, see 28.552 5.1.1.11.1;
  /// 0-15 CQI, 1-8 RI, 1-3 CQI table
  uint32_t wb_cqi_dist[16][8][3];

  /// cell-wide MCS distribution in PDSCH, see 28.552 5.1.1.12.1
  /// 1-8 RI, 1-3 MCS table, 0-31 MCS value
  uint32_t pdsch_mcs_dist[NR_KPM_MAX_LAYERS][NR_KPM_NB_MCS_TABLE_DL][NR_KPM_NB_MCS];

  /// cell-wide MCS distribution in PUSCH, see 28.552 5.1.1.12.1
  /// 1-8 RI, MCS table (0, 1, 3), 0-31 MCS value
  uint32_t pusch_mcs_dist[NR_KPM_MAX_LAYERS][NR_KPM_NB_MCS_TABLE_UL][NR_KPM_NB_MCS];

  /// cell-wide L1 SS-RSRP distribution per SSB beam, see 28.552 5.1.1.22.1
  /// 0-63 SSB index, 0-127 SS-RSRP report level (TS 38.133)
  uint32_t ss_rsrp_ssb_dist[NR_KPM_NB_SSB][NR_KPM_SS_RSRP_NB_LEVELS];
} NR_du_stats_t;

typedef struct {
  NR_UE_info_t **list;
  int max_period; // common multiple of periodicities sharing the table
  int max_ue_per_slot; // UE capacity per slot instance
} periodic_ue_sched_t;

typedef struct {
  bool active;
  f1ap_positioning_measurement_req_t meas_req;
} positioning_measurement_info_t;

/// Maximum number of cells (component carriers) per gNB MAC instance.
#define NR_MAX_CELLS 4

/*! \brief Per-cell scheduling context.
 *
 * One instance lives inside gNB_MAC_INST::cells[] for each active cell.
 * All fields here are logically owned by a single cell; shared state such as
 * the UE list and scheduling-policy function pointers lives on gNB_MAC_INST.
 */
typedef struct nr_cell_sched_s {

  /// Cell identity: Index in MAC->cell array matches FAPI header->phy_id; CGI used for F1 lookups
  /// TS 38.473 §9.3.1.12 NR CGI defined by PLMN Identity and NR Cell Identity
  uint64_t nr_cellid;
  plmn_id_t plmn;

  /// SSB placement offsets (derived from NFAPI config at init)
  int ssb_SubcarrierOffset;
  int ssb_OffsetPointA;

  /// Physical cell description: ServingCellConfigCommon, VRB maps, MIB/SIB1, PRACH
  NR_COMMON_channels_t common_channels;

  /// Running PDU index for BCH/DLSCH building within one slot
  uint16_t pdu_index;
  /// UL PRB blacklist (SNR-based)
  uint16_t ulprbbl[MAX_BWP_SIZE];
  /// NFAPI SCF config request sent to the DU/L1
  nfapi_nr_config_request_scf_t config;
  /// Pointers into the current DL_req PDCCH PDU, indexed by CORESET
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_idx[MAX_NUM_CORESET];
  /// Ring buffer of future UL TTI requests (dynamically allocated; size = UL_tti_req_ahead_size)
  nfapi_nr_ul_tti_request_t *UL_tti_req_ahead;
  int UL_tti_req_ahead_size;
  /// Length of the UL VRB map in slots (derived from frame_structure at init)
  int vrb_map_UL_size;

  /// Beam management state for this cell
  NR_beam_info_t beam_info;
  /// SSB index → beam index mapping
  int16_t beam_index_list[MAX_NUM_OF_SSB];

  /// TDD/FDD frame structure (slot patterns, numerology)
  frame_structure_t frame_structure;

  /// Slots of UL inactivity before an autonomous UL grant is issued to a UE
  uint32_t ulsch_max_frame_inactivity;

  /// Cell-level radio config (antennas, BWPs, timing, SRS/CSI-RS, precoding tables)
  nr_mac_config_t radio_config;

  /// BLER control thresholds for the default MCS-adaptation policy
  NR_bler_options_t dl_bler;
  NR_bler_options_t ul_bler;

  /// Minimum grant size in PRBs
  uint16_t min_grant_prb;
  /// Force identity precoding matrix (single-port or debug mode)
  bool identity_pm;
  /// Precoding matrix column counts per layer
  int precoding_matrix_size[NR_MAX_NB_LAYERS];

  /// SIB1 and initial PDCCH scheduling state
  NR_sched_ctrl_sib1_t *sched_ctrlSIB1;
  NR_sched_pdcch_t *sched_pdcch_otherSI;
  uint16_t cset0_bwp_start;
  uint16_t cset0_bwp_size;
  NR_Type0_PDCCH_CSS_config_t type0_PDCCH_CSS_config[MAX_NUM_OF_SSB];
  bool first_MIB;
  NR_sched_pdsch_t sib1_pdsch[MAX_NUM_OF_SSB];
  periodic_ue_sched_t periodic_srs_config;

  /// Dedicated UL TDA list, built from frame_structure at config time
  seq_arr_t ul_tda;
  /// Next UL slot targeted by the UL preprocessor look-ahead
  fsn_t ul_next;

  /// Optional opaque state for stateful custom scheduling policies
  void *sched_stateful_data;

  /// Per-cell KPI statistics
  NR_du_stats_t du_stats;
  uint64_t num_scheduled_prach_rx;

  // Per-cell MAC function execution performance profiler
  /// processing time of gNB scheduler
  time_stats_t gNB_scheduler;
  /// processing time of gNB scheduler for Random access
  time_stats_t schedule_ra;
  /// processing time of gNB ULSCH scheduler, including preprocessor
  time_stats_t schedule_ulsch;
  /// processing time of gNB DLSCH scheduler, including rlc_data_req + MAC header + preprocessor
  time_stats_t schedule_dlsch;
  /// processing time of CSI-RS CSI reporting SR and periodic SRS
  time_stats_t schedule_periodic;
  /// processing time of rlc_data_req
  time_stats_t rlc_data_req;
  /// processing time of nr_srs_ri_computation
  time_stats_t nr_srs_ri_computation_timer;
  /// processing time of nr_srs_tpmi_estimation
  time_stats_t nr_srs_tpmi_computation_timer;
  /// processing time of gNB ULSCH reception, including rlc_data_ind
  time_stats_t rx_ulsch_sdu;
} nr_cell_sched_t;

/*! \brief top level gNB MAC structure */
typedef struct gNB_MAC_INST_s {
  /// F1-C/U network configuration (addresses and ports)
  f1ap_net_config_t net_config;
  /// Nvipc parameters for FAPI interface with Aerial
  nvipc_params_t nvipc_params_s;
  /// Module
  module_id_t                     Mod_id;
  /// timing advance group
  NR_TAG_t                        *tag;
  /// Pointer to IF module instance for PHY
  NR_IF_Module_t                  *if_inst;
  pthread_t                       stats_thread;

  /// Per-cell scheduling contexts
  nr_cell_sched_t cells[NR_MAX_CELLS];

  NR_UEs_t UE_info;

  /// DL preprocessor for differentiated scheduling
  nr_pp_impl_dl pre_processor_dl;
  /// UL preprocessor for differentiated scheduling
  nr_pp_impl_ul pre_processor_ul;

  /// DL scheduling pipeline function pointers
  nr_dl_ri_pmi_select_fn dl_ri_pmi_select;
  nr_dl_tda_select_fn dl_tda_select;
  nr_dl_beam_select_fn dl_beam_select;
  nr_dl_mcs_select_fn dl_mcs_select;
  nr_dl_rb_alloc_fn dl_rb_alloc;
  nr_dl_lcid_alloc_fn dl_lcid_alloc;

  /// UL RI/TPMI + TDA selection + beam selection + MCS selection + RB allocation
  nr_ul_ri_tpmi_select_fn ul_ri_tpmi_select;
  nr_ul_tda_select_fn ul_tda_select;
  nr_ul_beam_select_fn ul_beam_select;
  nr_ul_mcs_select_fn ul_mcs_select;
  nr_ul_rb_alloc_fn ul_rb_alloc;

  nr_rlc_configuration_t rlc_config;

  nr_mac_rrc_ul_if_t mac_rrc;
  f1_config_t f1_config;
  int16_t frame;

  /// number of UEs to exceed to disable stats
  int stats_max_ue;
  /// if stats are currently enabled
  bool print_ue_stats;

  pthread_mutex_t sched_lock;

  dlul_mac_stats_t mac_stats;

  seq_arr_t pos_act_ue_arr;
  positioning_measurement_info_t pos_meas_info;
  positioning_config_t *positioning_config;
} gNB_MAC_INST;

#endif /*__LAYER2_NR_MAC_GNB_H__ */
/** @}*/
