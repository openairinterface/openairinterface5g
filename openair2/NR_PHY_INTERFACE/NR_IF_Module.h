/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief data structures for PHY/MAC interface modules
 */

#ifndef __NR_IF_MODULE__H__
#define __NR_IF_MODULE__H__

#include <pthread.h>
#include <stdint.h>
#include "assertions.h"
#include "nfapi_nr_interface_scf.h"
#include "common/platform_constants.h"
#include "common/platform_types.h"
#include "common/openairinterface5g_limits.h"

#define MAX_NUM_DL_PDU 100
#define MAX_NUM_UL_PDU 100
#define MAX_NUM_HI_DCI0_PDU 100
#define MAX_NUM_TX_REQUEST_PDU 100

#define MAX_NUM_NR_RX_RACH_PDUS 4
#define MAX_UL_PDUS_PER_SLOT 8
#define MAX_NUM_NR_SRS_PDUS 8
#define MAX_NUM_NR_UCI_PDUS MAX_MOBILES_PER_GNB
typedef struct {
  /// Module ID
  module_id_t module_id;
  /// PHY ID
  int phy_id;
  /// frame
  frame_t frame;
  /// slot
  slot_t slot;

  /// crc indication list
  nfapi_nr_crc_indication_t crc_ind;
  /// RX indication
  nfapi_nr_rx_data_indication_t rx_ind;
  /// RACH indication list
  nfapi_nr_rach_indication_t rach_ind;

  /// SRS indication list
  nfapi_nr_srs_indication_t srs_ind;

  /// SRS ToA vendor extension
  nfapi_nr_srs_toa_vendor_ext_indication_t srs_toa_vendor_ext_ind;

  /// UCI indication
  nfapi_nr_uci_indication_t uci_ind;
  nfapi_nr_crc_t crc_pdu_list[MAX_UL_PDUS_PER_SLOT];
  nfapi_nr_rx_data_pdu_t rx_pdu_list[MAX_UL_PDUS_PER_SLOT];
  nfapi_nr_srs_indication_pdu_t srs_pdu_list[MAX_NUM_NR_SRS_PDUS];
  nfapi_nr_srs_toa_vendor_ext_pdu_t srs_toa_vendor_ext_pdu_list[MAX_NUM_NR_SRS_PDUS];
  nfapi_nr_uci_t uci_pdu_list[MAX_NUM_NR_UCI_PDUS];
  /// NFAPI PRACH information
  nfapi_nr_prach_indication_pdu_t prach_pdu_indication_list[MAX_NUM_NR_RX_RACH_PDUS];

} NR_UL_IND_t;

// Downlink slot P7


typedef struct NR_Sched_Rsp {
  /// Module ID
  module_id_t module_id;
  /// PHY ID
  uint8_t phy_id;
  /// frame
  frame_t frame;
  /// slot
  slot_t slot;
  /// nFAPI DL Config Request
  nfapi_nr_dl_tti_request_t DL_req;
  /// nFAPI UL Config Request
  nfapi_nr_ul_tti_request_t UL_tti_req;
  /// nFAPI UL_DCI Request
  nfapi_nr_ul_dci_request_t UL_dci_req;
  /// Pointers to DL SDUs
  nfapi_nr_tx_data_request_t TX_req;
} NR_Sched_Rsp_t;
void reset_sched_response(NR_Sched_Rsp_t *sched_response, int frame, int slot, int module_id, int phy_id);

typedef struct {
  int phy_id;
  nfapi_nr_config_request_scf_t *cfg;
} NR_PHY_Config_t;

typedef struct NR_IF_Module_s {
  //define the function pointer
  void (*NR_UL_indication)(NR_UL_IND_t *UL_INFO);
  void (*NR_slot_indication)(const nfapi_nr_slot_indication_scf_t *ind, NR_Sched_Rsp_t *rsp);
  void (*NR_PHY_config_req)(NR_PHY_Config_t *config_INFO);
  uint32_t CC_mask;
  uint16_t current_frame;
  uint8_t current_slot;
  pthread_mutex_t if_mutex;
  int sl_ahead;
} NR_IF_Module_t;

/*Initial */
NR_IF_Module_t *NR_IF_Module_init(int Mod_id);

void NR_IF_Module_kill(int Mod_id);

#endif /*_NFAPI_INTERFACE_NR_H_*/
