/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __LAYER2_NR_MAC_H__
#define __LAYER2_NR_MAC_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h> // ntohs()
#include "common/utils/nr/nr_common.h"
#include "common/utils/LOG/log.h"
#include "common/utils/collection/linear_alloc.h"

#include "NR_CellGroupConfig.h"

#define MAX_FRAME_NUMBER 0x400
#define NR_SHORT_BSR_TABLE_SIZE 32
#define NR_LONG_BSR_TABLE_SIZE 256


// Definitions for MAC control and data
#define NR_BCCH_DL_SCH 3 // SI
#define NR_BCCH_BCH 5    // MIB
#define NR_CCCH_PAYLOAD_SIZE_MAX 512
#define RAR_PAYLOAD_SIZE_MAX  128
#define MAX_CSI_REPORTCONFIG  48

//  For both DL/UL-SCH
//  Except:
//   - UL/DL-SCH: fixed-size MAC CE(known by LCID)
//   - UL/DL-SCH: padding
//   - UL-SCH:    MSG3 48-bits
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |       L       |

//  For both DL/UL-SCH
//  For:
//   - UL/DL-SCH: fixed-size MAC CE(known by LCID)
//   - UL/DL-SCH: padding, for single/multiple 1-oct padding CE(s)
//   - UL-SCH:    MSG3 48-bits
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|R|   LCID    |
//  LCID: The Logical Channel ID field identifies the logical channel instance of the corresponding MAC SDU or the type of the corresponding MAC CE or padding as described in Tables 6.2.1-1 and 6.2.1-2 for the DL-SCH and UL-SCH respectively. There is one LCID field per MAC subheader. The LCID field size is 6 bits;
//  L: The Length field indicates the length of the corresponding MAC SDU or variable-sized MAC CE in bytes. There is one L field per MAC subheader except for subheaders corresponding to fixed-sized MAC CEs and padding. The size of the L field is indicated by the F field;
//  F: lenght of L is 0:8 or 1:16 bits wide
//  R: Reserved bit, set to zero.

typedef struct {
  uint8_t LCID: 6;    // octet 1 [5:0]
  uint8_t F: 1;       // octet 1 [6]
  uint8_t R: 1;       // octet 1 [7]
  uint8_t L: 8;       // octet 2 [7:0]
} __attribute__ ((__packed__)) NR_MAC_SUBHEADER_SHORT;

typedef struct {
  uint8_t LCID: 6;
  uint8_t F: 1;
  uint8_t R: 1;
  uint16_t L: 16;
} __attribute__ ((__packed__)) NR_MAC_SUBHEADER_LONG;

typedef struct {
  uint8_t LCID: 6;    // octet 1 [5:0]
  uint8_t R: 2;       // octet 1 [7:6]
} __attribute__ ((__packed__)) NR_MAC_SUBHEADER_FIXED;

static inline int get_mac_len(uint8_t *pdu, uint32_t pdu_len, uint16_t *mac_ce_len, uint16_t *mac_subheader_len)
{
  if (pdu_len < sizeof(NR_MAC_SUBHEADER_SHORT))
    return false;
  NR_MAC_SUBHEADER_SHORT *s = (NR_MAC_SUBHEADER_SHORT *)pdu;
  NR_MAC_SUBHEADER_LONG *l = (NR_MAC_SUBHEADER_LONG *)pdu;
  if (s->F && pdu_len < sizeof(NR_MAC_SUBHEADER_LONG))
    return false;
  if (s->F) {
    *mac_subheader_len = sizeof(*l);
    *mac_ce_len = ntohs(l->L);
  } else {
    *mac_subheader_len = sizeof(*s);
    *mac_ce_len = s->L;
  }
  if (*mac_ce_len > pdu_len) {
    LOG_E(NR_MAC, "MAC sdu len impossible (%d)\n", *mac_ce_len);
    return false;
  }

  return true;
}

// BSR MAC CEs
// TS 38.321 ch. 6.1.3.1
// Short BSR for a specific logical channel group ID
typedef struct {
  uint8_t Buffer_size: 5;  // octet 1 LSB
  uint8_t LcgID: 3;        // octet 1 MSB
} __attribute__ ((__packed__)) NR_BSR_SHORT;

// Long BSR for all logical channel group ID
typedef struct {
  uint8_t LcgID0: 1;        // octet 1 [0]
  uint8_t LcgID1: 1;        // octet 1 [1]
  uint8_t LcgID2: 1;        // octet 1 [2]
  uint8_t LcgID3: 1;        // octet 1 [3]
  uint8_t LcgID4: 1;        // octet 1 [4]
  uint8_t LcgID5: 1;        // octet 1 [5]
  uint8_t LcgID6: 1;        // octet 1 [6]
  uint8_t LcgID7: 1; // octet 1 [7]
} __attribute__ ((__packed__)) NR_BSR_LONG;

// 38.321 ch. 6.1.3.4
typedef struct {
  uint8_t TA_COMMAND: 6;  // octet 1 [5:0]
  uint8_t TAGID: 2;       // octet 1 [7:6]
} __attribute__ ((__packed__)) NR_MAC_CE_TA;

// single Entry PHR MAC CE
// TS 38.321 ch. 6.1.3.8
typedef struct {
  uint8_t PH: 6;    // octet 1 [5:0]
  uint8_t R1: 2;    // octet 1 [7:6]
  uint8_t PCMAX: 6; // octet 2 [5:0]
  uint8_t R2: 2;    // octet 2 [7:6]
} __attribute__ ((__packed__)) NR_SINGLE_ENTRY_PHR_MAC_CE;


// SP ZP CSI-RS Resource Set Activation/Deactivation MAC CE
// 38.321 ch. 6.1.3.19
typedef struct {
  uint8_t BWPID: 2;       // octet 1 [1:0]
  uint8_t CELLID: 5;      // octet 1 [6:2]
  uint8_t A_D: 1;         // octet 1 [7]
  uint8_t CSIRS_RSC_ID: 4; // octet 2 [3:0]
  uint8_t R: 4;            // octet 2 [7:4]
} __attribute__ ((__packed__)) NR_MAC_CE_SP_ZP_CSI_RS_RES_SET;

//TS 38.321 Sec 6.1.3.15, TCI State indicaton for UE-Specific PDCCH MAC CE
typedef struct {
  uint8_t CoresetId1: 3;   //Octect 1 [2:0]
  uint8_t ServingCellId: 5; //Octect 1 [7:3]
  uint8_t TciStateId: 7;   //Octect 2 [6:0]
  uint8_t CoresetId2: 1;   //Octect 2 [7]
} __attribute__ ((__packed__)) NR_TCI_PDCCH;

//TS 38.321 Sec 6.1.3.14, TCI State activation/deactivation for UE Specific PDSCH MAC CE
typedef struct {
  uint8_t BWP_Id: 2;       //Octect 1 [1:0]
  uint8_t ServingCellId: 5; //Octect 1 [6:2]
  uint8_t R: 1;            //Octect 1 [7]
  uint8_t T[];             //Octects 2 to MAX TCI States/8
} __attribute__ ((__packed__)) NR_TCI_PDSCH_APERIODIC_CSI;

//TS 6.1.3.16, SP CSI reporting on PUCCH Activation/Deactivation MAC CE
typedef struct {
  uint8_t BWP_Id: 2;      //Octect 1 [1:0]
  uint8_t ServingCellId: 5; //Octect 1 [6:2]
  uint8_t R1: 1;        //Octect 1 [7]
  uint8_t S0: 1;        //Octect 2 [0]
  uint8_t S1: 1;        //Octect 2 [1]
  uint8_t S2: 1;        //Octect 2 [2]
  uint8_t S3: 1;        //Octect 2 [3]
  uint8_t R2: 4;        //Octect 2 [7:4]
} __attribute__ ((__packed__)) NR_PUCCH_CSI_REPORTING;


//TS 38.321 sec 6.1.3.12
//SP CSI-RS / CSI-IM Resource Set Activation/Deactivation MAC CE
typedef struct {
  uint8_t BWP_ID: 2;
  uint8_t SCID: 5;
  uint8_t A_D: 1;
  uint8_t SP_CSI_RSID: 6;
  uint8_t IM: 1;
  uint8_t R1: 1;
  uint8_t SP_CSI_IMID: 6;
  uint8_t R2: 2;
  struct TCI_S {
    uint8_t TCI_STATE_ID: 6;
    uint8_t R: 2;
  } __attribute__ ((__packed__)) TCI_STATE;
} __attribute__ ((__packed__)) CSI_RS_CSI_IM_ACT_DEACT_MAC_CE;


//* RAR MAC subheader // TS 38.321 ch. 6.1.5, 6.2.2 *//
// - E: The Extension field is a flag indicating if the MAC subPDU including this MAC subheader is the last MAC subPDU or not in the MAC PDU
// - T: The Type field is a flag indicating whether the MAC subheader contains a Random Access Preamble ID or a Backoff Indicator (0, BI) (1, RAPID)
// - R: Reserved bit, set to "0"
// - BI: The Backoff Indicator field identifies the overload condition in the cell.
// - RAPID: The Random Access Preamble IDentifier field identifies the transmitted Random Access Preamble

/*!\brief RAR MAC subheader with RAPID */
typedef struct {
  uint8_t RAPID: 6;
  uint8_t T: 1;
  uint8_t E: 1;
} __attribute__ ((__packed__)) NR_RA_HEADER_RAPID;

typedef struct {
  uint8_t RAPID: 6;
  uint8_t T1: 1;
  uint8_t E: 1;
} __attribute__((__packed__)) NR_RA_HEADER_RAPID_MSGB;

/*!\brief RAR MAC subheader with Backoff Indicator */
typedef struct {
  uint8_t BI: 4;
  uint8_t R: 2;
  uint8_t T: 1;
  uint8_t E: 1;
} __attribute__ ((__packed__)) NR_RA_HEADER_BI;

typedef struct {
  uint8_t BI: 4;
  uint8_t R: 1;
  uint8_t T2: 1;
  uint8_t T1: 1;
  uint8_t E: 1;
} __attribute__((__packed__)) NR_RA_HEADER_BI_MSGB;

typedef struct {
  uint8_t R: 4;
  uint8_t S: 1;
  uint8_t T2: 1;
  uint8_t T1: 1;
  uint8_t E: 1;
} __attribute__((__packed__)) NR_RA_HEADER_SUCCESS_RAR_MSGB;

// TS 38.321 Sec. 6.2.3
typedef struct {
  uint8_t TA1: 7;         // octet 1 [6:0]
  uint8_t R: 1;           // octet 1 [7]
  uint8_t UL_GRANT_1: 3;  // octet 2 [2:0]
  uint8_t TA2: 5;         // octet 2 [7:3]
  uint8_t UL_GRANT_2: 8;  // octet 3 [7:0]
  uint8_t UL_GRANT_3: 8;  // octet 4 [7:0]
  uint8_t UL_GRANT_4: 8;  // octet 5 [7:0]
  uint8_t TCRNTI_1: 8;    // octet 6 [7:0]
  uint8_t TCRNTI_2: 8;    // octet 7 [7:0]
} __attribute__ ((__packed__)) NR_MAC_RAR;

// TS 38.321 Sec. 6.2.3
typedef struct {
  uint8_t TA1: 7; // octet 1 [6:0]
  uint8_t R: 1; // octet 1 [7]
  uint8_t UL_GRANT_1: 3; // octet 2 [2:0]
  uint8_t TA2: 5; // octet 2 [7:3]
  uint8_t UL_GRANT_2: 8; // octet 3 [7:0]
  uint8_t UL_GRANT_3: 8; // octet 4 [7:0]
  uint8_t UL_GRANT_4: 8; // octet 5 [7:0]
  uint8_t TCRNTI_1: 8; // octet 6 [7:0]
  uint8_t TCRNTI_2: 8; // octet 7 [7:0]
} __attribute__((__packed__)) NR_MAC_RAR_MSGB;

// TS 38.321 Sec. 6.2.3
typedef struct {
  uint8_t CONT_RES_1: 8; // octet 1 [7:0]
  uint8_t CONT_RES_2: 8; // octet 2 [7:0]
  uint8_t CONT_RES_3: 8; // octet 3 [7:0]
  uint8_t CONT_RES_4: 8; // octet 4 [7:0]
  uint8_t CONT_RES_5: 8; // octet 5 [7:0]
  uint8_t CONT_RES_6: 8; // octet 6 [7:0]
  uint8_t HARQ_FTI: 3; // octet 7 [2:0]
  uint8_t TPC: 2; // octet 7 [4:3]
  uint8_t CH_ACESS_CPEXT: 2; // octet 7 [6:5]
  uint8_t R: 1; // octet 7 [7]
  uint8_t TA1: 4; // octet 8 [3:0]
  uint8_t PUCCH_RI: 4; // octet 8 [7:4]
  uint8_t TA2: 8; // octet 9 [7:0]
  uint8_t CRNTI_1: 8; // octet 10 [7:0]
  uint8_t CRNTI_2: 8; // octet 11 [7:0]
} __attribute__((__packed__)) NR_MAC_SUCCESS_RAR;

/* P-RNTI DCI 1_0 Short Messages Indicator values (TS 38.212 Table 7.3.1.2.1-1). */
#define NR_DCI_PRNTI_SMI_RESERVED 0 /* 00: reserved */
#define NR_DCI_PRNTI_SMI_PAGING_SCHED_ONLY 1 /* 01: PDSCH carries paging, no Short Message */
#define NR_DCI_PRNTI_SMI_SHORT_MSG_ONLY 2 /* 10: Short Message only */
#define NR_DCI_PRNTI_SMI_PAGING_AND_SHORT 3 /* 11: PDSCH paging and Short Message */

// DCI pdu structures. Used by both gNB and UE.
typedef struct {
  uint32_t val;
  uint8_t nbits;
} dci_field_t;

typedef struct {

  uint8_t     format_indicator; //1 bit
  uint8_t     ra_preamble_index; //6 bits
  uint8_t     ss_pbch_index; //6 bits
  uint8_t     prach_mask_index; //4 bits
  uint8_t     mcs; //5 bits
  uint8_t     ndi; //1 bit
  uint8_t     rv; //2 bits
  dci_field_t harq_pid; // 4/5 bits
  uint8_t     tpc; //2 bits
  uint8_t     short_messages_indicator; //2 bits
  uint8_t     short_messages; //8 bits
  uint8_t     tb_scaling; //2 bits
  uint8_t     pucch_resource_indicator; //3 bits
  uint8_t     system_info_indicator; //1 bit
  uint8_t     ulsch_indicator;
  uint8_t     slot_format_indicator_count;
  uint8_t     *slot_format_indicators;
 
  uint8_t     pre_emption_indication_count;
  uint16_t    *pre_emption_indications; //14 bit each
 
  uint8_t     block_number_count;
  uint8_t     *block_numbers;
  uint8_t     padding;

  dci_field_t mcs2; //variable
  dci_field_t ndi2; //variable
  dci_field_t rv2; //variable
  dci_field_t frequency_domain_assignment; //variable
  dci_field_t time_domain_assignment; //variable
  dci_field_t frequency_hopping_flag; //variable
  dci_field_t vrb_to_prb_mapping; //variable
  dci_field_t dai[2]; //variable
  dci_field_t pdsch_to_harq_feedback_timing_indicator; //variable
  dci_field_t carrier_indicator; //variable
  dci_field_t bwp_indicator; //variable
  dci_field_t prb_bundling_size_indicator; //variable
  dci_field_t rate_matching_indicator; //variable
  dci_field_t zp_csi_rs_trigger; //variable
  dci_field_t transmission_configuration_indication; //variable
  dci_field_t srs_request; //variable
  dci_field_t cbgti; //variable
  dci_field_t cbgfi; //variable
  dci_field_t srs_resource_indicator; //variable
  dci_field_t precoding_information; //variable
  dci_field_t csi_request; //variable
  dci_field_t ptrs_dmrs_association; //variable
  dci_field_t beta_offset_indicator; //variable
  dci_field_t cloded_loop_indicator; //variable
  dci_field_t ul_sul_indicator; //variable
  dci_field_t antenna_ports; //variable
  dci_field_t dmrs_sequence_initialization;
  dci_field_t reserved; //1_0/C-RNTI:10 bits, 1_0/P-RNTI: 6 bits, 1_0/SI-&RA-RNTI: 16 bits

} dci_pdu_rel15_t;

//  38.321 ch6.2.1, 38.331
#define DL_SCH_LCID_CCCH                           0x00
#define DL_SCH_LCID_DCCH                           0x01
#define DL_SCH_LCID_DCCH1                          0x02
#define DL_SCH_LCID_DTCH                           0x04
#define DL_SCH_LCID_RECOMMENDED_BITRATE            0x2F
#define DL_SCH_LCID_SP_ZP_CSI_RS_RES_SET_ACT       0x30
#define DL_SCH_LCID_PUCCH_SPATIAL_RELATION_ACT     0x31
#define DL_SCH_LCID_SP_SRS_ACTIVATION              0x32
#define DL_SCH_LCID_SP_CSI_REP_PUCCH_ACT           0x33
#define DL_SCH_LCID_TCI_STATE_IND_UE_SPEC_PDCCH    0x34
#define DL_SCH_LCID_TCI_STATE_ACT_UE_SPEC_PDSCH    0x35
#define DL_SCH_LCID_APERIODIC_CSI_TRI_STATE_SUBSEL 0x36
#define DL_SCH_LCID_SP_CSI_RS_CSI_IM_RES_SET_ACT   0X37
#define DL_SCH_LCID_DUPLICATION_ACT                0X38
#define DL_SCH_LCID_SCell_ACT_4_OCT                0X39
#define DL_SCH_LCID_SCell_ACT_1_OCT                0X3A
#define DL_SCH_LCID_L_DRX                          0x3B
#define DL_SCH_LCID_DRX                            0x3C
#define DL_SCH_LCID_TA_COMMAND                     0x3D
#define DL_SCH_LCID_CON_RES_ID                     0x3E
#define DL_SCH_LCID_PADDING                        0x3F

#define UL_SCH_LCID_CCCH_64_BITS                   0x00
#define UL_SCH_LCID_SRB1                           0x01
#define UL_SCH_LCID_SRB2                           0x02
#define UL_SCH_LCID_SRB3                           0x03
#define UL_SCH_LCID_DTCH                           0x04
#define UL_SCH_LCID_EXTENDED_LCID_2_OCT            0x21
#define UL_SCH_LCID_EXTENDED_LCID_1_OCT            0x22
#define UL_SCH_LCID_CCCH_48_BITS_REDCAP            0x23
#define UL_SCH_LCID_CCCH_64_BITS_REDCAP            0x24
#define UL_SCH_LCID_TRUNCATED_ENHANCED_BFR         0x2B
#define UL_SCH_LCID_TIMING_ADVANCE_REPORT          0x2C
#define UL_SCH_LCID_TRUNCATED_SIDELINK_BSR         0x2D
#define UL_SCH_LCID_SIDELINK_BSR                   0x2E
#define UL_SCH_LCID_LBT_FAILURE_4_OCT              0x30
#define UL_SCH_LCID_LBT_FAILURE_1_OCT              0x31
#define UL_SCH_LCID_BFR                            0x32
#define UL_SCH_LCID_TRUNCATED_BFR                  0x33
#define UL_SCH_LCID_CCCH_48_BITS                   0x34
#define UL_SCH_LCID_RECOMMENDED_BITRATE_QUERY      0x35
#define UL_SCH_LCID_MULTI_ENTRY_PHR_4_OCT          0x36
#define UL_SCH_LCID_CONFIGURED_GRANT_CONFIRMATION  0x37
#define UL_SCH_LCID_MULTI_ENTRY_PHR_1_OCT          0x38
#define UL_SCH_LCID_SINGLE_ENTRY_PHR               0x39
#define UL_SCH_LCID_C_RNTI                         0x3A
#define UL_SCH_LCID_S_TRUNCATED_BSR                0x3B
#define UL_SCH_LCID_L_TRUNCATED_BSR                0x3C
#define UL_SCH_LCID_S_BSR                          0x3D
#define UL_SCH_LCID_L_BSR                          0x3E
#define UL_SCH_LCID_PADDING                        0x3F

#define NR_MAX_NUM_LCGID              8

//=========
// DCI defs
//=========

typedef enum {
  NR_DL_DCI_FORMAT_1_0 = 0,
  NR_DL_DCI_FORMAT_1_1,
  NR_DL_DCI_FORMAT_2_0,
  NR_DL_DCI_FORMAT_2_1,
  NR_DL_DCI_FORMAT_2_2,
  NR_DL_DCI_FORMAT_2_3,
  NR_UL_DCI_FORMAT_0_0,
  NR_UL_DCI_FORMAT_0_1,
  NR_DCI_NONE
} nr_dci_format_t;

typedef enum channel_bandwidth_e {
  bw_5MHz   = 0x1,
  bw_10MHz  = 0x2,
  bw_20MHz  = 0x4,
  bw_40MHz  = 0x8,
  bw_80MHz  = 0x16,
  bw_100MHz = 0x32
} channel_bandwidth_t;

typedef enum nr_ssb_and_cset_mux_pattern_type_e {
  NR_SSB_AND_CSET_MUX_PATTERN_TYPE1=1,
  NR_SSB_AND_CSET_MUX_PATTERN_TYPE2,
  NR_SSB_AND_CSET_MUX_PATTERN_TYPE3
} nr_ssb_and_cset_mux_pattern_type_t;

typedef struct Type0_PDCCH_CSS_config_s {
  int32_t num_rbs;
  int32_t num_symbols;
  int32_t rb_offset; // Offset from SSB RB0
  uint32_t type0_pdcch_ss_mux_pattern;
  uint16_t frame;
  int sfn_c;
  uint32_t slot;
  uint32_t first_symbol_index;
  uint32_t search_space_duration;
  uint32_t search_space_frame_period;  // in slots
  uint32_t ssb_index;
  int32_t cset_start_rb;
  NR_SubcarrierSpacing_t scs_pdcch;
  bool active;
} NR_Type0_PDCCH_CSS_config_t;

typedef struct {
  uint8_t nb_ssbri_cri;
  uint8_t cri_ssbri_bitlen;
  uint8_t rsrp_bitlen;
  uint8_t diff_rsrp_bitlen;
  uint8_t sinr_bitlen;
  uint8_t diff_sinr_bitlen;
} L1_Meas_bitlen_t;

typedef struct{
  uint8_t ri_restriction;
  uint8_t cri_bitlen;
  uint8_t ri_bitlen;
  uint8_t li_bitlen[8];
  uint8_t pmi_x1_bitlen[8];
  uint8_t pmi_x2_bitlen[8];
  uint8_t pmi_i11_bitlen[8];
  uint8_t pmi_i12_bitlen[8];
  uint8_t pmi_i13_bitlen[8];
  uint8_t cqi_bitlen[8];
} CSI_Meas_bitlen_t;

typedef struct nr_csi_report {
  NR_CSI_ReportConfigId_t reportConfigId;
  NR_CSI_ReportConfig__reportQuantity_PR reportQuantity_type;
  NR_CSI_ReportConfig__ext2__reportQuantity_r16_PR reportQuantity_type_r16;
  long periodicity;
  uint16_t offset;
  /// number of entries in SSB_Index_list or CSI_Index_list
  int nb_resources;
  long **SSB_Index_list;
  long **CSI_Index_list;
//  uint8_t nb_of_nzp_csi_report;
  uint8_t nb_of_csi_ssb_report;
  L1_Meas_bitlen_t CSI_report_bitlen;
  CSI_Meas_bitlen_t csi_meas_bitlen;
  int codebook_mode;
  int N1;
  int N2;
} nr_csi_report_t;

typedef enum {
  NR_SRS_SRI_0 = 0,
  NR_SRS_SRI_1,
  NR_SRS_SRI_2,
  NR_SRS_SRI_3,
  NR_SRS_SRI_0_1,
  NR_SRS_SRI_0_2,
  NR_SRS_SRI_0_3,
  NR_SRS_SRI_1_2,
  NR_SRS_SRI_1_3,
  NR_SRS_SRI_2_3,
  NR_SRS_SRI_0_1_2,
  NR_SRS_SRI_0_1_3,
  NR_SRS_SRI_0_2_3,
  NR_SRS_SRI_1_2_3,
  NR_SRS_SRI_0_1_2_3
} nr_srs_sri_t;

typedef struct nr_srs_feedback {
  uint8_t sri;
  uint8_t ul_ri;
  uint8_t tpmi;
} nr_srs_feedback_t;

typedef struct NR_UE_DL_BWP {
  NR_BWP_Id_t bwp_id;
  int scs;
  long *cyclicprefix;
  uint16_t BWPSize;
  uint16_t BWPStart;
  NR_PDSCH_TimeDomainResourceAllocationList_t *tdaList_Common;
  NR_PDSCH_Config_t *pdsch_Config;
  uint8_t mcsTableIdx;
  nr_dci_format_t dci_format;
} NR_UE_DL_BWP_t;

typedef struct NR_UE_UL_BWP {
  NR_BWP_Id_t bwp_id;
  int scs;
  long *cyclicprefix;
  uint16_t BWPSize;
  uint16_t BWPStart;
  NR_RACH_ConfigCommon_t *rach_ConfigCommon;
  NR_MsgA_ConfigCommon_r16_t *msgA_ConfigCommon_r16;
  NR_PUSCH_TimeDomainResourceAllocationList_t *tdaList_Common;
  NR_ConfiguredGrantConfig_t *configuredGrantConfig;
  NR_PUSCH_Config_t *pusch_Config;
  NR_UCI_OnPUSCH_t *uci_onPusch;
  NR_PUCCH_Config_t *pucch_Config;
  NR_PUCCH_ConfigCommon_t *pucch_ConfigCommon;
  NR_SRS_Config_t *srs_Config;
  long *msg3_DeltaPreamble;
  long transform_precoding;
  uint8_t mcs_table;
  nr_dci_format_t dci_format;
  int max_fb_time;
  long *p0_NominalWithGrant;
  // UE Channel bandwidth according to 38.101 5.3.2
  int channel_bandwidth;
  // Minimum transmission power according to 38.101 6.3.1
  float P_CMIN;
  // SRS power control adjustment state
  int h_b_f_c;
  bool srs_power_control_initialized;
} NR_UE_UL_BWP_t;

// non-BWP serving cell configuration
typedef struct {
  NR_CrossCarrierSchedulingConfig_t *crossCarrierSchedulingConfig;
  NR_SRS_CarrierSwitching_t *carrierSwitching;
  NR_UplinkConfig_t *supplementaryUplink;
  NR_PDSCH_CodeBlockGroupTransmission_t *pdsch_CGB_Transmission;
  long *xOverhead_PDSCH;
  long *nrofHARQ_ProcessesForPDSCH;
  long *maxMIMO_Layers_PDSCH;
  NR_PUSCH_CodeBlockGroupTransmission_t *pusch_CGB_Transmission;
  long *rateMatching_PUSCH;
  long *xOverhead_PUSCH;
  long *maxMIMO_Layers_PUSCH;
  NR_CSI_MeasConfig_t *csi_MeasConfig;
  NR_CSI_AperiodicTriggerStateList_t *aperiodicTriggerStateList;
  uint16_t initial_dl_BWPSize;
  uint16_t initial_dl_BWPStart;
  uint16_t initial_ul_BWPSize;
  uint16_t initial_ul_BWPStart;
  int n_dl_bwp;
  int n_ul_bwp;
  int dl_bw_tbslbrm;
  int ul_bw_tbslbrm;
  NR_NTN_Config_r17_t *ntn_Config_r17;
  NR_DownlinkHARQ_FeedbackDisabled_r17_t *downlinkHARQ_FeedbackDisabled_r17;
  long *nrofHARQ_ProcessesForPDSCH_v1700;
  long *nrofHARQ_ProcessesForPUSCH_r17;
} NR_UE_ServingCell_Info_t;

typedef enum {
  defaultA = 0,
  defaultB = 1,
  defaultC = 2
} default_table_type_t;

typedef enum {
  typeA = 0,
  typeB = 1
} mappingType_t;

typedef struct NR_tda_info {
  mappingType_t mapping_type;
  int startSymbolIndex;
  int nrOfSymbols;
  long k2;
  bool valid_tda;
} NR_tda_info_t;

typedef enum {
  RA_4_STEP = 0,
  RA_2_STEP = 1,
} nr_ra_type_t;

#endif /*__LAYER2_MAC_H__ */

