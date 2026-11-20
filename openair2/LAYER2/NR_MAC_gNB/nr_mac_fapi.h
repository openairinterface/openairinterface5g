/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_MAC_FAPI_H_
#define NR_MAC_FAPI_H_

#include "nfapi/open-nFAPI/nfapi/public_inc/nfapi_nr_interface_scf.h"

struct NR_Sched_Rsp;
int nr_mac_handle_slot_indication(const nfapi_nr_slot_indication_scf_t *ind, struct NR_Sched_Rsp *rsp);
int nr_mac_handle_rach_indication(const nfapi_nr_rach_indication_t *rach_ind);
int nr_mac_handle_uci_indication(const nfapi_nr_uci_indication_t *uci_ind);
int nr_mac_handle_crc_indication(const nfapi_nr_crc_indication_t *crc_ind);
int nr_mac_handle_rx_data_indication(const nfapi_nr_rx_data_indication_t *rx_ind);
int nr_mac_handle_srs_indication(const nfapi_nr_srs_indication_t *srs_ind);
int nr_mac_handle_srs_toa_vendor_ext(const nfapi_nr_srs_toa_vendor_ext_indication_t *srs_ind);

#endif /* NR_MAC_FAPI_H_ */
