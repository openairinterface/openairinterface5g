/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */


#ifndef __PLATFORM_CONSTANTS_H__
#define __PLATFORM_CONSTANTS_H__

#define NL_MAX_PAYLOAD 18000 /* this should cover the max mtu size*/

#ifdef LARGE_SCALE
#define NB_MODULES_MAX 128
#else
#define NB_MODULES_MAX 32
#endif

#define MAX_IP_PACKET_SIZE 10000 // 9000

#define MAX_MODULES NB_MODULES_MAX

#ifndef UE_EXPANSION
#ifdef LARGE_SCALE
#define MAX_MOBILES_PER_ENB 128
#define MAX_MOBILES_PER_ENB_NB_IoT 128
#define MAX_eNB 2
#else
#define MAX_MOBILES_PER_ENB 40
#define MAX_MOBILES_PER_ENB_NB_IoT 40
#define MAX_eNB 2
#endif
#else
#define MAX_MOBILES_PER_ENB 256
#define MAX_MOBILES_PER_ENB_NB_IoT 256
#define MAX_eNB 2
#endif


/// NB-IOT
#define NB_RB_MAX_NB_IOT (2 + 3) /* 2 from LTE_maxDRB_NB_r13 in LTE_asn_constant.h + 3 SRBs */

#define DEFAULT_RAB_ID 1

#define NB_RB_MAX (11 + 3) /* LTE_maxDRB from LTE_asn_constant.h + 3 SRBs */
#define NR_NB_RB_MAX (29 + 3) /* NR_maxDRB from NR_asn_constant.hm + 3 SRBs */

#define NR_MAX_NB_PDU_SESSIONS (256)

#define NR_MAX_NB_ALLOWED_SNSSAI (8) /* Maximum number of allowed S-NSSAI in TS 38.413 */

#define MAX_DRBS_PER_UE         (32)  /* Maximum number of Data Radio Bearers per UE
                                       * defined for NGAP in TS 38.413 - maxnoofDRBs */
#define MAX_PDUS_PER_UE         (8)   /* Maximum number of PDU Sessions per UE */

/** Maximum value of nrofPDCCH-MonitoringOccasionPerSSB-InPO-r16 (TS 38.331 PCCH-Config) */
#define NR_PCCH_MAX_MO_PER_SSB_IN_PO 4

/** Maximum number of Paging Occasions per Paging Frame (TS 38.331 PCCH-Config) */
#define NR_PCCH_MAX_PO 4

#define NR_PHYS_CELL_ID_MAX 1007 /* Maximum Physical Cell ID (0..1007) */

#define NB_RB_MBMS_MAX (29 * 16) /* 29 = LTE_maxSessionPerPMCH + 16 = LTE_maxServiceCount from LTE_asn_constant.h */

#define NB_RAB_MAX 11 /* from LTE_maxDRB in LTE_asn_constant.h */
#define RAB_OFFSET 0x000F

#define MAX_NUM_SLICES 1024

#define NR_MAX_BIT_RATE UINT64_C(4000000000000) /* number of bits delivered by NG-RAN in UL/DL */

// RLC Entity
#define RLC_TX_MAXSIZE       10000000
#define RLC_RX_MAXSIZE       10000000
#define MAX_ANT 8
// CBA constant
#define NUM_MAX_CBA_GROUP 4

#define RETURNok (0)
#define RETURNerror (-1)
#define DEFAULT_NAS_PATH "PWD"
#define NUM_PRACH_RX_FOR_NOISE_ESTIMATE 100

// SDAP
#define MAX_QOS_FLOWS 64

/** Maximum number of PagingRecords in one PCCH Paging message (TS 38.331) */
#define NR_PCCH_MAX_PAGING_RECORDS 32

// SDAP/5G NAS NOS1
#define DEFAULT_NOS1_PDU_ID 10

#define SPEED_OF_LIGHT 299792458 // in m/s
#endif /* __PLATFORM_CONSTANTS_H__ */
