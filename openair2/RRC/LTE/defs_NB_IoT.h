/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NB-IoT RRC struct definitions and function prototypes
 */

#ifndef __OPENAIR_RRC_DEFS_NB_IOT_H__
#define __OPENAIR_RRC_DEFS_NB_IOT_H__


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "COMMON/s1ap_messages_types.h"
#include "COMMON/rrc_messages_types.h"

#include "collection/tree.h"
#include "rrc_types_NB_IoT.h"
#include "common/platform_constants.h"
#include "common/platform_types.h"
#include "common/openairinterface5g_limits.h"

#include "COMMON/mac_rrc_primitives.h"

//-----NB-IoT #include files-------

//#include "SystemInformationBlockType1-NB.h"
//#include "SystemInformation-NB.h"
#include "LTE_RRCConnectionReconfiguration-NB.h"
#include "LTE_RRCConnectionReconfigurationComplete-NB.h"
#include "LTE_RRCConnectionSetup-NB.h"
#include "LTE_RRCConnectionSetupComplete-NB.h"
#include "LTE_RRCConnectionRequest-NB.h"
#include "LTE_RRCConnectionReestablishmentRequest-NB.h"
#include "LTE_BCCH-DL-SCH-Message-NB.h"
#include "LTE_BCCH-BCH-Message-NB.h"
#include "LTE_AS-Config-NB.h"
#include "LTE_AS-Context-NB.h"
#include "LTE_UE-Capability-NB-r13.h" //equivalent of UE-EUTRA-Capability.h
//-------------------

# include "intertask_interface.h"
# include "commonDef.h"

/*I will change the name of the structure for compile purposes--> hope not to undo this process*/

typedef unsigned int uid_NB_IoT_t;
#define UID_LINEAR_ALLOCATOR_BITMAP_SIZE_NB_IoT (((MAX_MOBILES_PER_ENB_NB_IoT/8)/sizeof(unsigned int)) + 1)

typedef struct uid_linear_allocator_NB_IoT_s {
  unsigned int   bitmap[UID_LINEAR_ALLOCATOR_BITMAP_SIZE_NB_IoT];
} uid_allocator_NB_IoT_t;


#define PROTOCOL_RRC_CTXT_UE_FMT           PROTOCOL_CTXT_FMT
#define PROTOCOL_RRC_CTXT_UE_ARGS(CTXT_Pp) PROTOCOL_CTXT_ARGS(CTXT_Pp)

#define PROTOCOL_RRC_CTXT_FMT           PROTOCOL_CTXT_FMT
#define PROTOCOL_RRC_CTXT_ARGS(CTXT_Pp) PROTOCOL_CTXT_ARGS(CTXT_Pp)


//left as they are --> used in LAYER2/epenair2_proc.c and UE side
typedef enum UE_STATE_NB_IoT_e {
  RRC_INACTIVE_NB_IoT=0,
  RRC_IDLE_NB_IoT,
  RRC_SI_RECEIVED_NB_IoT,
  RRC_CONNECTED_NB_IoT,
  RRC_RECONFIGURED_NB_IoT,
  RRC_HO_EXECUTION_NB_IoT //maybe not needed?
} UE_STATE_NB_IoT_t;


/** @defgroup _rrc RRC
 * @ingroup _oai2
 * @{
 */
typedef struct UE_RRC_INFO_NB_IoT_s {
  UE_STATE_NB_IoT_t State;
  uint8_t SIB1systemInfoValueTag;
  uint32_t SIStatus;
  uint32_t SIcnt;
  uint8_t MCCHStatus[8]; // MAX_MBSFN_AREA
  uint8_t SIwindowsize; //!< Corresponds to the SIB1 si-WindowLength parameter. The unit is ms. Possible values are (final): 1,2,5,10,15,20,40
  uint8_t handoverTarget;
  //HO_STATE_t ho_state;
  uint16_t SIperiod; //!< Corresponds to the SIB1 si-Periodicity parameter (multiplied by 10). Possible values are (final): 80,160,320,640,1280,2560,5120
  unsigned short UE_index;
  uint32_t T300_active;
  uint32_t T300_cnt;
  uint32_t T304_active;
  uint32_t T304_cnt;
  uint32_t T310_active;
  uint32_t T310_cnt;
  uint32_t N310_cnt;
  uint32_t N311_cnt;
  rnti_t   rnti;
} __attribute__ ((__packed__)) UE_RRC_INFO_NB_IoT;

//#define NUM_PRECONFIGURED_LCHAN (NB_CH_CX*2)  //BCCH, CCCH

#define UE_MODULE_INVALID ((module_id_t) ~0) // FIXME attention! depends on type uint8_t!!!
#define UE_INDEX_INVALID  ((module_id_t) ~0) // FIXME attention! depends on type uint8_t!!! used to be -1



// HO_STATE is not supported by NB-IoT

//#define MAX_MOBILES_PER_ENB MAX_MOBILES_PER_RG
#define RRM_FREE(p)       if ( (p) != NULL) { free(p) ; p=NULL ; }
#define RRM_MALLOC(t,n)   (t *) malloc16( sizeof(t) * n )
#define RRM_CALLOC(t,n)   (t *) malloc16( sizeof(t) * n)
#define RRM_CALLOC2(t,s)  (t *) malloc16( s )

//Measurement Report not supported in NB-IoT

#define PAYLOAD_SIZE_MAX 1024
#define RRC_BUF_SIZE 1024
#define UNDEF_SECURITY_MODE 0xff
#define NO_SECURITY_MODE 0x20

/* TS 36.331: RRC-TransactionIdentifier ::= INTEGER (0..3) */
#define RRC_TRANSACTION_IDENTIFIER_NUMBER  3

typedef struct UE_S_TMSI_NB_IoT_s {
  bool       presence;
  mme_code_t mme_code;
  m_tmsi_t   m_tmsi;
} __attribute__ ((__packed__)) UE_S_TMSI_NB_IoT;


typedef enum e_rab_satus_NB_IoT_e {
  E_RAB_STATUS_NEW_NB_IoT,
  E_RAB_STATUS_DONE_NB_IoT, // from the eNB perspective
  E_RAB_STATUS_ESTABLISHED_NB_IoT, // get the reconfigurationcomplete form UE
  E_RAB_STATUS_FAILED_NB_IoT,
} e_rab_status_NB_IoT_t;

typedef struct e_rab_param_NB_IoT_s {
  e_rab_t param;
  uint8_t status;
  uint8_t xid; // transaction_id
} __attribute__ ((__packed__)) e_rab_param_NB_IoT_t;


//HANDOVER_INFO not implemented in NB-IoT delete


#define RRC_HEADER_SIZE_MAX 64
#define RRC_BUFFER_SIZE_MAX 1024

typedef struct {
  char Payload[RRC_BUFFER_SIZE_MAX];
  char Header[RRC_HEADER_SIZE_MAX];
  int payload_size;
} RRC_BUFFER_NB_IoT;

#define RRC_BUFFER_SIZE_NB_IoT sizeof(RRC_BUFFER_NB_IoT)


typedef struct RB_INFO_NB_IoT_s {
  uint16_t Rb_id;  //=Lchan_id
  //LCHAN_DESC Lchan_desc[2]; no more used
  //MAC_MEAS_REQ_ENTRY *Meas_entry; //may not needed for NB-IoT
} RB_INFO_NB_IoT;

typedef struct SRB_INFO_NB_IoT_s {
  uint16_t Srb_id;  //=Lchan_id---> useful for distinguish between SRB1 and SRB1bis?
  RRC_BUFFER_NB_IoT Rx_buffer;
  RRC_BUFFER_NB_IoT Tx_buffer;
  //LCHAN_DESC Lchan_desc[2]; no more used
  unsigned int Trans_id;
  uint8_t Active;
} SRB_INFO_NB_IoT;


typedef struct RB_INFO_TABLE_ENTRY_NB_IoT_s {
  RB_INFO_NB_IoT Rb_info;
  uint8_t Active;
  uint32_t Next_check_frame;
  uint8_t status;
} RB_INFO_TABLE_ENTRY_NB_IoT;

typedef struct SRB_INFO_TABLE_ENTRY_NB_IoT_s {
  SRB_INFO_NB_IoT Srb_info;
  uint8_t Active;
  uint8_t status;
  uint32_t Next_check_frame;
} SRB_INFO_TABLE_ENTRY_NB_IoT;

//MEAS_REPORT_LIST_s not implemented in NB-IoT but is used at UE side
//HANDOVER_INFO_UE not implemented in NB-IoT
typedef struct HANDOVER_INFO_UE_NB_IoT_s {
  LTE_PhysCellId_t targetCellId;
  uint8_t measFlag;
} HANDOVER_INFO_UE_NB_IoT;

//NB-IoT eNB_RRC_UE_NB_IoT_s--(used as a context in eNB --> ue_context in rrc_eNB_ue_context)------
typedef struct eNB_RRC_UE_NB_IoT_s {

  LTE_EstablishmentCause_t           establishment_cause;
  uint8_t                            primaryCC_id;
  //in NB-IoT only SRB0, SRB1 and SRB1bis (until AS security activation) exist

  /*MP: Concept behind List and List2
   *
   * SRB_configList --> is used for the actual list of SRBs that is managed/that should be send over the RRC message
   * SRB_configList2--> refers to all the SRBs configured for that specific transaction identifier
   *          this because in a single transaction one or more SRBs could be established
   *          and you want to keep memory on what happen for every transaction
   * Transaction ID (xid): is used to associate the proper RRC....Complete message received by the UE to the corresponding
   *             message previously sent by the eNB (e.g. RRCConnectionSetup -- RRCConnectionSetupComplete)
   *             this because it could happen that more messages are transmitted at the same time
   */
  LTE_SRB_ToAddModList_NB_r13_t            *SRB_configList;//for SRB1 and SRB1bis
  LTE_SRB_ToAddModList_NB_r13_t            *SRB_configList2[RRC_TRANSACTION_IDENTIFIER_NUMBER];
  LTE_DRB_ToAddModList_NB_r13_t            *DRB_configList; //for all the DRBs
  LTE_DRB_ToAddModList_NB_r13_t            *DRB_configList2[RRC_TRANSACTION_IDENTIFIER_NUMBER]; //for the configured DRBs of a xid
  uint8_t                               DRB_active[2];//in LTE was 8 --> at most 2 for NB-IoT

  struct LTE_PhysicalConfigDedicated_NB_r13    *physicalConfigDedicated_NB_IoT;
  LTE_MAC_MainConfig_NB_r13_t           *mac_MainConfig_NB_IoT;

  //No SPS(semi-persistent scheduling) in NB-IoT
  //No Measurement report in NB-IoT

  SRB_INFO_NB_IoT                           SI;
  SRB_INFO_NB_IoT                           Srb0;
  SRB_INFO_TABLE_ENTRY_NB_IoT               Srb1;
  SRB_INFO_TABLE_ENTRY_NB_IoT               Srb1bis;

  /* KeNB as derived from KASME received from EPC */
  uint8_t kenb[32];

  /* Used integrity/ciphering algorithms--> maintained the same for NB-IoT */
  e_LTE_CipheringAlgorithm_r12     ciphering_algorithm; //Specs. TS 36.331 V14.1.0 pag 432 Change position of chipering enumerative w.r.t previous version
  e_LTE_SecurityAlgorithmConfig__integrityProtAlgorithm integrity_algorithm;

  uint8_t                            status;
  rnti_t                             rnti;
  uint64_t                           random_ue_identity;



  /* Information from UE RRC ConnectionRequest-NB-r13_IE--> NB-IoT */
  UE_S_TMSI_NB_IoT                          Initialue_identity_s_TMSI;
  LTE_EstablishmentCause_NB_r13_t               establishment_cause_NB_IoT; //different set for NB-IoT

  /* Information from UE RRC ConnectionReestablishmentRequest-NB--> NB-IoT */
  LTE_ReestablishmentCause_NB_r13_t             reestablishment_cause_NB_IoT; //different set for NB_IoT

  /* UE id for initial connection to S1AP */
  uint16_t                           ue_initial_id;

  /* Information from S1AP initial_context_setup_req */
  uint32_t                           eNB_ue_s1ap_id :24;

  security_capabilities_t            security_capabilities;

  /* Total number of e_rab already setup in the list */ //NAS list?
  int                               setup_e_rabs;
  /* Number of e_rab to be setup in the list */ //NAS list?
  int                                nb_of_e_rabs;
  /* list of e_rab to be setup by RRC layers */
  e_rab_param_NB_IoT_t                      e_rab[NB_RB_MAX_NB_IOT];//[S1AP_MAX_E_RAB];

  // LG: For GTPV1 TUNNELS
  uint32_t                           enb_gtp_teid[S1AP_MAX_E_RAB];
  transport_layer_addr_t             enb_gtp_addrs[S1AP_MAX_E_RAB];
  rb_id_t                            enb_gtp_ebi[S1AP_MAX_E_RAB];

  //Which timers are referring to?
  uint32_t                           ul_failure_timer;
  uint32_t                           ue_release_timer;
  //threshold of the release timer--> set in RRCConnectionRelease
  uint32_t                           ue_release_timer_thres;
} eNB_RRC_UE_NB_IoT_t;
//--------------------------------------------------------------------------------

typedef uid_NB_IoT_t ue_uid_t;


//generally variable called: ue_context_pP
typedef struct rrc_eNB_ue_context_NB_IoT_s {

  /* Tree related data */
  RB_ENTRY(rrc_eNB_ue_context_NB_IoT_s) entries;

  /* Uniquely identifies the UE between MME and eNB within the eNB.
   * This id is encoded on 24bits.
   */
  rnti_t         ue_id_rnti;

  // another key for protocol layers but should not be used as a key for RB tree
  ue_uid_t       local_uid;

  /* UE id for initial connection to S1AP */
  struct eNB_RRC_UE_NB_IoT_s   ue_context; //context of ue in the e-nB

} rrc_eNB_ue_context_NB_IoT_t;



//---NB-IoT (completely changed)-------------------------------
//called "carrier"--> data from PHY layer
typedef struct {

  // buffer that contains the encoded messages
  uint8_t             *MIB_NB_IoT;
  int                 sizeof_MIB_NB_IoT;

  uint8_t                           *SIB1_NB_IoT;
  int                               sizeof_SIB1_NB_IoT;
  uint8_t                           *SIB23_NB_IoT;
  int                               sizeof_SIB23_NB_IoT;


  //not actually implemented in OAI
  uint8_t                           *SIB4_NB_IoT;
  int                               sizeof_SIB4_NB_IoT;
  uint8_t                           *SIB5_NB_IoT;
  int                               sizeof_SIB5_NB_IoT;
  uint8_t                           *SIB14_NB_IoT;
  int                               sizeof_SIB14_NB_IoT;
  uint8_t                           *SIB16_NB_IoT;
  int                               sizeof_SIB16_NB_IoT;

  //TS 36.331 V14.2.1
  //  uint8_t                           *SIB15_NB;
  //  int                               sizeof_SIB15_NB;
  //  uint8_t                           *SIB20_NB;
  //  int                               sizeof_SIB20_NB;
  //  uint8_t                           *SIB22_NB;
  //  int                               sizeof_SIB22_NB;

  //implicit parameters needed
  int                               Ncp; //cyclic prefix for DL
  int               Ncp_UL; //cyclic prefix for UL
  int                               p_eNB; //number of tx antenna port
  int               p_rx_eNB; //number of receiving antenna ports
  uint32_t                          dl_CarrierFreq; //detected by the UE
  uint32_t                          ul_CarrierFreq; //detected by the UE
  uint16_t                          physCellId; //not stored in the MIB-NB but is getting through NPSS/NSSS

  //are the only static one (memory has been already allocated)
  LTE_BCCH_BCH_Message_NB_t                mib_NB_IoT;
  LTE_BCCH_DL_SCH_Message_NB_t             siblock1_NB_IoT; //SIB1-NB
  LTE_BCCH_DL_SCH_Message_NB_t             systemInformation_NB_IoT; //SI

  LTE_SystemInformationBlockType1_NB_t        *sib1_NB_IoT;
  LTE_SystemInformationBlockType2_NB_r13_t    *sib2_NB_IoT;
  LTE_SystemInformationBlockType3_NB_r13_t    *sib3_NB_IoT;
  //not implemented yet
  LTE_SystemInformationBlockType4_NB_r13_t      *sib4_NB_IoT;
  LTE_SystemInformationBlockType5_NB_r13_t      *sib5_NB_IoT;
  LTE_SystemInformationBlockType14_NB_r13_t     *sib14_NB_IoT;
  LTE_SystemInformationBlockType16_NB_r13_t     *sib16_NB_IoT;


  SRB_INFO_NB_IoT                          SI;
  SRB_INFO_NB_IoT                          Srb0;

  uint8_t                           **MCCH_MESSAGE; //  probably not needed , but added to remove errors
  int                               sizeof_MCCH_MESSAGE[8];// but added to remove errors
  SRB_INFO_NB_IoT                          MCCH_MESS[8];// MAX_MBSFN_AREA
  /*future implementation TS 36.331 V14.2.1
  SystemInformationBlockType15_NB_r14_t     *sib15;
  SystemInformationBlockType20_NB_r14_t     *sib20;
  SystemInformationBlockType22_NB_r14_t     *sib22;

  uint8_t             SCPTM_flag;
  int                 sizeof_SC_MCHH_MESS[];
  SC_MCCH_Message_NB_t        scptm;*/


} rrc_eNB_carrier_data_NB_IoT_t;
//---------------------------------------------------



//---NB-IoT---(completely change)---------------------
typedef struct eNB_RRC_INST_NB_IoT_s {

  rrc_eNB_carrier_data_NB_IoT_t          carrier[MAX_NUM_CCs];

  uid_allocator_NB_IoT_t                    uid_allocator; // for rrc_ue_head
  RB_HEAD(rrc_ue_tree_NB_IoT_s, rrc_eNB_ue_context_NB_IoT_s)     rrc_ue_head; // ue_context tree key search by rnti

  uint8_t                           Nb_ue;

  hash_table_t                      initial_id2_s1ap_ids; // key is    content is rrc_ue_s1ap_ids_t
  hash_table_t                      s1ap_id2_s1ap_ids   ; // key is    content is rrc_ue_s1ap_ids_t

  //RRC configuration
  RrcConfigurationReq configuration; //rrc_messages_types.h

  // other PLMN parameters
  /// Mobile country code
  int mcc;
  /// Mobile network code
  int mnc;
  /// number of mnc digits
  int mnc_digit_length;

  // other RAN parameters //FIXME: to be checked--> depends on APP layer
  int srb1_timer_poll_retransmit;
  int srb1_max_retx_threshold;
  int srb1_timer_reordering;
  int srb1_timer_status_prohibit;
  int srs_enable[MAX_NUM_CCs];


} eNB_RRC_INST_NB_IoT;

#define MAX_UE_CAPABILITY_SIZE_NB_IoT 255

//not needed for the moment
typedef struct OAI_UECapability_NB_IoT_s {
  uint8_t sdu[MAX_UE_CAPABILITY_SIZE_NB_IoT];
  int     sdu_size;
  ////NB-IoT------
  LTE_UE_Capability_NB_r13_t  UE_Capability_NB_IoT; //replace the UE_EUTRA_Capability of LTE
} OAI_UECapability_NB_IoT_t;

typedef struct UE_RRC_INST_NB_IoT_s {
  Rrc_State_NB_IoT_t     RrcState;
  Rrc_Sub_State_NB_IoT_t RrcSubState;
  plmn_t          plmnID;
  uint8_t rat;
  as_nas_info_t   initialNasMsg;
  OAI_UECapability_NB_IoT_t *UECap;
  uint8_t *UECapability;
  int     UECapability_size;

  UE_RRC_INFO_NB_IoT Info[NB_SIG_CNX_UE];

  SRB_INFO_NB_IoT                 Srb0[NB_SIG_CNX_UE];
  SRB_INFO_TABLE_ENTRY_NB_IoT     Srb1[NB_CNX_UE];
  SRB_INFO_TABLE_ENTRY_NB_IoT     Srb2[NB_CNX_UE];
  HANDOVER_INFO_UE_NB_IoT         HandoverInfoUe;
  LTE_SystemInformationBlockType2_t *sib2[NB_CNX_UE];

  struct QuantityConfig           *QuantityConfig[NB_CNX_UE];

} UE_RRC_INST_NB_IoT;


#include "proto_NB_IoT.h" //should be put here otherwise compilation error

#endif
/** @} */
