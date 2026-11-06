/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#pragma once

#include "common/utils/ds/queue_t.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

extern queue_t gnb_rach_ind_queue[NR_MAX_CELLS];
extern queue_t gnb_rx_ind_queue[NR_MAX_CELLS];
extern queue_t gnb_crc_ind_queue[NR_MAX_CELLS];
extern queue_t gnb_uci_ind_queue[NR_MAX_CELLS];
