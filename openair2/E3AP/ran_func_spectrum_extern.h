/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 *
 * The seam between the gNB MAC and the Spectrum RAN function: the only symbols
 * the scheduler is allowed to call into. Same role as the *_extern.h headers of
 * the E2 RAN functions (e.g. ran_func_rc_extern.h). Everything else about
 * sensing -- configuration, state, the scan itself -- is private to
 * ran_func_spectrum.c.
 */
#ifndef RAN_FUNC_SPECTRUM_EXTERN_H
#define RAN_FUNC_SPECTRUM_EXTERN_H

#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "openair2/E3AP/ran_func_spectrum_types.h"

/* Bind sensing to a cell once its frame structure is known (end of
 * nr_mac_cell_config_scc()). Reads the sensing keys of the E3Configuration section
 * and enables sensing for the cell iff the operator reserved slots. A no-op
 * when nothing is configured. */
void e3_spectrum_mac_attach_cell(nr_cell_sched_t *cell);

/* Release what e3_spectrum_mac_attach_cell() allocated. Idempotent. */
void e3_spectrum_mac_detach_cell(nr_cell_sched_t *cell);

/* Is this (absolute) slot hard-reserved for sensing for this cell? */
bool nr_mac_ul_slot_is_sensing_reserved(const nr_cell_sched_t *cell, int slot);

/* Per-slot hooks, called from gNB_dlsch_ulsch_scheduler(). reserve/restore
 * bracket the UE allocators (block the slot, then free it for the scan);
 * scan_and_publish runs the scan, the Aerial capture PUSCH and the E3 publish.
 * All three are no-ops for a cell sensing is not attached to. */
void nr_mac_sensing_reserve_ul_slot(nr_cell_sched_t *cell, int prev_slot, frame_t frame);
void nr_mac_sensing_restore_ul_slot(nr_cell_sched_t *cell, frame_t frame, slot_t slot);
void nr_mac_sensing_scan_and_publish(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot);

#endif /* RAN_FUNC_SPECTRUM_EXTERN_H */
