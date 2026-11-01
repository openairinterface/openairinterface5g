/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief RRC structures/types definition
 */

#ifndef __OPENAIR_NR_RRC_DEFS_H__
#define __OPENAIR_NR_RRC_DEFS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/platform_types.h"
#include "commonDef.h"
#include "common/platform_constants.h"
#include "common/utils/LOG/log.h"

#include "NR_asn_constant.h"
#include "NR_MeasConfig.h"
#include "NR_CellGroupConfig.h"
#include "NR_RadioBearerConfig.h"
#include "NR_RLC-BearerConfig.h"
#include "NR_TAG.h"
#include "NR_asn_constant.h"
#include "NR_MIB.h"
#include "NR_SIB1.h"
#include "NR_BCCH-BCH-Message.h"
#include "NR_DL-DCCH-Message.h"
#include "NR_SystemInformation.h"
#include "NR_UE-NR-Capability.h"
#include "NR_SL-PreconfigurationNR-r16.h"
#include "NR_MasterInformationBlockSidelink.h"
#include "NR_ReestablishmentCause.h"
#include "NR_MeasurementReport.h"
#include "NR_VarMeasReport.h"

#include "RRC/NR/nr_rrc_common.h"
#include "as_message.h"
#include "common/utils/nr/nr_common.h"
#include "notified_fifo.h"
#include "RRC/NR_UE/nr_mac_rrc_types.h"

#define NB_CNX_UE 2//MAX_MANAGED_RG_PER_MOBILE
#define NR_MAX_MEAS_OBJ 64
#define NR_MAX_MEAS_CONFIG 64
#define NR_MAX_MEAS_ID 64
#define MAX_QUANTITY_CONFIG 2
#define NUMBER_OF_NEIGHBORING_CELLS_MAX 8

typedef enum {
  nr_SecondaryCellGroupConfig_r15=0,
  nr_RadioBearerConfigX_r15=1
} nsa_message_t;

#define MAX_UE_NR_CAPABILITY_SIZE 2048
typedef struct OAI_NR_UECapability_s {
  uint8_t sdu[MAX_UE_NR_CAPABILITY_SIZE];
  uint16_t sdu_size;
  NR_UE_NR_Capability_t *UE_NR_Capability;
} OAI_NR_UECapability_t;

typedef enum Rrc_State_NR_e {
  RRC_STATE_IDLE_NR = 0,
  RRC_STATE_INACTIVE_NR,
  RRC_STATE_CONNECTED_NR,
  RRC_STATE_DETACH_NR,
} Rrc_State_NR_t;

// 3GPP TS 38.300 Section 9.2.6
typedef enum RA_trigger_e {
  RA_NOT_RUNNING,
  RRC_CONNECTION_SETUP,
  RRC_CONNECTION_REESTABLISHMENT,
  RRC_RESUME_REQUEST,
  DURING_HANDOVER,
  NON_SYNCHRONISED,
  TRANSITION_FROM_RRC_INACTIVE,
  TO_ESTABLISH_TA,
  REQUEST_FOR_OTHER_SI,
  BEAM_FAILURE_RECOVERY,
} RA_trigger_t;

typedef struct UE_RRC_SI_INFO_NR_r17_s {
  bool sib15_validity;
  NR_timer_t sib15_timer;
  bool sib16_validity;
  NR_timer_t sib16_timer;
  bool sib17_validity;
  NR_timer_t sib17_timer;
  bool sib18_validity;
  NR_timer_t sib18_timer;
  bool sib19_validity;
  NR_timer_t sib19_timer;
  bool sib20_validity;
  NR_timer_t sib20_timer;
  bool sib21_validity;
  NR_timer_t sib21_timer;
} NR_UE_RRC_SI_INFO_r17;

typedef struct UE_RRC_SI_INFO_NR_s {
  bool sib_pending;
  uint32_t default_otherSI_map[MAX_SI_GROUPS];
  bool sib1_validity;
  NR_timer_t sib1_timer;
  /// stored SIB1, to revert back to the source PCell upon T304 expiry
  NR_SIB1_t *sib1;
  bool sib2_validity;
  NR_timer_t sib2_timer;
  bool sib3_validity;
  NR_timer_t sib3_timer;
  bool sib4_validity;
  NR_timer_t sib4_timer;
  bool sib5_validity;
  NR_timer_t sib5_timer;
  bool sib6_validity;
  NR_timer_t sib6_timer;
  bool sib7_validity;
  NR_timer_t sib7_timer;
  bool sib8_validity;
  NR_timer_t sib8_timer;
  bool sib9_validity;
  NR_timer_t sib9_timer;
  bool sib10_validity;
  NR_timer_t sib10_timer;
  bool sib11_validity;
  NR_timer_t sib11_timer;
  bool sib12_validity;
  NR_timer_t sib12_timer;
  bool sib13_validity;
  NR_timer_t sib13_timer;
  bool sib14_validity;
  NR_timer_t sib14_timer;
  NR_UE_RRC_SI_INFO_r17 SInfo_r17;
  // Extracted from SIB1
  int scs;
  int sib19_periodicity;
  int sib19_windowposition;
  int si_windowlength;
} NR_UE_RRC_SI_INFO;

typedef struct NR_UE_Timers_Constants_s {
  // timers
  NR_timer_t T300;
  NR_timer_t T301;
  NR_timer_t T302;
  NR_timer_t T304;
  NR_timer_t T310;
  NR_timer_t T311;
  NR_timer_t T319;
  NR_timer_t T320;
  NR_timer_t T321;
  NR_timer_t T325;
  NR_timer_t T380;
  NR_timer_t T390;
  // NTN timer T430 which guards UL SYNC
  NR_timer_t T430;
  // counters
  uint32_t N310_cnt;
  uint32_t N311_cnt;
  // constants (limits configured by the network)
  uint32_t N310_k;
  uint32_t N311_k;
  NR_UE_TimersAndConstants_t *sib1_TimersAndConstants;
} NR_UE_Timers_Constants_t;

typedef enum {
  OUT_OF_SYNC = 0,
  IN_SYNC = 1
} nr_sync_msg_t;

typedef enum { RB_NOT_PRESENT, RB_ESTABLISHED, RB_SUSPENDED } NR_RB_status_t;

typedef struct meas_report_params_s {
  long trigger_quantity;
  long rs_type;
  int reports_sent;
  int max_reports;
  int max_report_cells;
  long report_interval_ms;
  bool neighbor_cell_valid;
  bool report_rsrp;
  NR_timer_t TA2;
  NR_timer_t TA3;
  NR_timer_t periodic_report_timer;
} meas_report_params_t;

typedef struct l3_measurements_s {
  float ssb_filter_coeff_rsrp;
  float csi_RS_filter_coeff_rsrp;
  meas_t serving_cell;
  meas_t neighboring_cell[NUMBER_OF_NEIGHBORING_CELLS_MAX];
  meas_report_params_t meas_report[NR_MAX_MEAS_ID];
} l3_measurements_t;

typedef struct rrcPerNB {
  NR_MeasObjectToAddMod_t *MeasObj[NR_MAX_MEAS_OBJ];
  NR_ReportConfigToAddMod_t *ReportConfig[NR_MAX_MEAS_CONFIG];
  NR_QuantityConfigNR_t *QuantityConfig[MAX_QUANTITY_CONFIG];
  NR_MeasIdToAddMod_t *MeasId[NR_MAX_MEAS_ID];
  NR_VarMeasReport_t *MeasReport[NR_MAX_MEAS_ID];
  NR_MeasGapConfig_t *measGapConfig;
  NR_UE_RRC_SI_INFO SInfo;
  NR_RSRP_Range_t s_measure;
  l3_measurements_t l3_measurements;
} rrcPerNB_t;

typedef struct NR_UE_RRC_INST_s {
  instance_t ue_id;
  rrcPerNB_t perNB[NB_CNX_UE];
  bool access_barred;
  rnti_t rnti;
  uint32_t phyCellID;
  long arfcn_ssb;
  uint64_t cell_identity;

  OAI_NR_UECapability_t UECap;
  NR_UE_Timers_Constants_t timers_and_constants;

  RA_trigger_t ra_trigger;
  NR_ReestablishmentCause_t reestablishment_cause;
  uint32_t reestab_source_pci;
  rnti_t reestab_source_crnti;
  plmn_t plmnID;

  NR_BWP_Id_t dl_bwp_id;
  NR_BWP_Id_t ul_bwp_id;

  NR_RB_status_t Srb[NR_NUM_SRB];
  NR_RB_status_t status_DRBs[MAX_DRBS_PER_UE];
  bool active_RLC_entity[NR_MAX_NUM_LCID];

  /* KgNB as computed from parameters within USIM card */
  uint8_t kgnb[32];
  /* Used integrity/ciphering algorithms */
  //RRC_LIST_TYPE(NR_SecurityAlgorithmConfig_t, NR_SecurityAlgorithmConfig) SecurityAlgorithmConfig_list;
  NR_CipheringAlgorithm_t  cipheringAlgorithm;
  e_NR_IntegrityProtAlgorithm  integrityProtAlgorithm;
  long keyToUse;
  bool as_security_activated;
  /// Next Hop Chaining Count
  uint8_t nh[32];
  uint64_t nhcc;

  bool detach_after_release;
  NR_timer_t release_timer;
  NR_RRCRelease_t *RRCRelease;
  long selected_plmn_identity;
  Rrc_State_NR_t nrRrcState;
  // flag to identify 1st reconfiguration after reestablishment
  bool reconfig_after_reestab;
  // 5G-S-TMSI
  uint64_t fiveG_S_TMSI;
  // Frame timing received from MAC
  int current_hfn;
  int current_frame;
  bool sched_reconfsync_sib1;
  //Sidelink params
  NR_SL_PreconfigurationNR_r16_t *sl_preconfig;
  // NTN params
  bool is_NTN_UE;
  NR_NTN_Config_r17_t *target_ntncfg;
  bool process_target_ntncfg;
  notifiedFIFO_t *mac_input_nf;
  /* NAS PDU deferred until UL-DCCH exists: consumed in RRCSetupComplete/RRCResumeComplete dedicatedNAS-Message */
  as_nas_info_t pending_initial_nas;
  /// source PCell snapshot, to revert back to the source PCell upon T304 expiry (TS 38.331 5.3.5.8.3)
  struct {
    bool valid;
    uint32_t phyCellID;
    rnti_t rnti;
    uint8_t kgnb[32];
    uint8_t nh[32];
    uint64_t nhcc;
    NR_CipheringAlgorithm_t cipheringAlgorithm;
    e_NR_IntegrityProtAlgorithm integrityProtAlgorithm;
    long keyToUse;
    NR_RB_status_t Srb[NR_NUM_SRB];
    NR_RB_status_t status_DRBs[MAX_DRBS_PER_UE];
    NR_SIB1_t *sib1;
    bool meas_config;
  } ho_source;
} NR_UE_RRC_INST_t;

#define RRCLOG_D(f, ...) LOG_D(NR_RRC, "[UE %ld] RNTI 0x%04x " f, rrc->ue_id, rrc->rnti __VA_OPT__(, ) __VA_ARGS__)
#define RRCLOG_I(f, ...) LOG_I(NR_RRC, "[UE %ld] RNTI 0x%04x " f, rrc->ue_id, rrc->rnti __VA_OPT__(, ) __VA_ARGS__)
#define RRCLOG_W(f, ...) LOG_W(NR_RRC, "[UE %ld] RNTI 0x%04x " f, rrc->ue_id, rrc->rnti __VA_OPT__(, ) __VA_ARGS__)
#define RRCLOG_E(f, ...) LOG_E(NR_RRC, "[UE %ld] RNTI 0x%04x " f, rrc->ue_id, rrc->rnti __VA_OPT__(, ) __VA_ARGS__)
#define RRCLOG_A(f, ...) LOG_A(NR_RRC, "[UE %ld] RNTI 0x%04x " f, rrc->ue_id, rrc->rnti __VA_OPT__(, ) __VA_ARGS__)
#endif
