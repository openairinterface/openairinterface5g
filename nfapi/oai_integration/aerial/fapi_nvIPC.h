/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
* \brief Header file for fapi_nvIPC.c
 */
#ifndef OPENAIRINTERFACE_FAPI_NVIPC_H
#define OPENAIRINTERFACE_FAPI_NVIPC_H

#include "nv_ipc.h"
#include "nv_ipc_utils.h"
#include "nvlog.h"
#include "vnf_nr.h"
#include "debug.h"

#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

/* Maximum number of PHY instances supported by this layer.
 * Aliases NR_MAX_CELLS; using NFAPI_PHY_MAX here makes
 * clear that this is a per-PHY (not per-carrier-component)
 * limit at the cuBB transport boundary. */
#define NFAPI_PHY_MAX NR_MAX_CELLS

int get_cpu_msg_buf_size();
int get_cpu_data_buf_size();
bool allocate_msg(nv_ipc_msg_t* send_msg);
void release_msg(nv_ipc_msg_t* send_msg);
bool send_nvipc_msg(nv_ipc_msg_t* send_msg);
bool aerial_nr_send_p5_message(vnf_nr_t *vnf, uint16_t p5_idx, nfapi_nr_p4_p5_message_header_t *msg, uint32_t msg_len);
int nvIPC_Init(nvipc_params_t nvipc_params_s);
int oai_fapi_send_end_request(uint32_t frame, uint32_t slot, uint8_t PHY_id);
int oai_fapi_ul_tti_req(nfapi_nr_ul_tti_request_t *ul_tti_req);
int oai_fapi_ul_dci_req(nfapi_nr_ul_dci_request_t *ul_dci_req);
int oai_fapi_tx_data_req(nfapi_nr_tx_data_request_t *tx_data_req);
int oai_fapi_dl_tti_req(nfapi_nr_dl_tti_request_t *dl_config_req);
void nvIPC_Stop();
void nvIPC_send_stop_request();
#endif // OPENAIRINTERFACE_FAPI_NVIPC_H
