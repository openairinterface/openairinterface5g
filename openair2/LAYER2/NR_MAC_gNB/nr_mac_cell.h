/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __LAYER2_NR_MAC_CELL_H__
#define __LAYER2_NR_MAC_CELL_H__

#include <stdint.h>
#include "common/5g_platform_types.h"

struct gNB_MAC_INST_s;
typedef struct gNB_MAC_INST_s gNB_MAC_INST;
struct nr_cell_sched_s;
typedef struct nr_cell_sched_s nr_cell_sched_t;

int nr_mac_get_cell_idx(gNB_MAC_INST *mac, nr_cell_sched_t *cell);
int nr_mac_get_cell_phy_id(gNB_MAC_INST *mac, nr_cell_sched_t *cell);
nr_cell_sched_t *nr_mac_get_cell_by_idx(gNB_MAC_INST *mac, uint16_t idx);
nr_cell_sched_t *nr_mac_get_cell_by_phy_id(gNB_MAC_INST *mac, uint16_t phy_id);
nr_cell_sched_t *nr_mac_get_cell_by_cgi(gNB_MAC_INST *mac, plmn_id_t plmn, uint64_t nr_cellid);

#endif /* __LAYER2_NR_MAC_CELL_H__ */
