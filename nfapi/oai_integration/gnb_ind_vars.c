/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "gnb_ind_vars.h"

queue_t gnb_rach_ind_queue[NR_MAX_CELLS];
queue_t gnb_rx_ind_queue[NR_MAX_CELLS];
queue_t gnb_crc_ind_queue[NR_MAX_CELLS];
queue_t gnb_uci_ind_queue[NR_MAX_CELLS];
