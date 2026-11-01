/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_UE_RU_H
#define NR_UE_RU_H

#include "PHY/defs_nr_UE.h"
#include "radio/COMMON/common_lib.h"

int nrue_get_cell_count(void);
const nrUE_cell_params_t *nrue_get_cell(int cell_id);
NR_DL_FRAME_PARMS *nrue_get_cell_fp(int cell_id);
void nrue_set_cell(int cell_id, const nrUE_cell_params_t *cell);
void nrue_set_cell_params(configmodule_interface_t *cfg);
int nrue_get_band(const PHY_VARS_NR_UE *UE);
int nrue_get_ru_count(void);
const nrUE_RU_params_t *nrue_get_ru(int ru_id);
void nrue_set_ru_cell_id(int ru_id, int cell_id);
void nrue_set_ru_params(configmodule_interface_t *cfg);

void nrue_init_openair0(void);

void nrue_ru_start(void);
void nrue_ru_stop(void);
void nrue_ru_end(void);
void nrue_ru_set_freq(PHY_VARS_NR_UE *UE, uint64_t ul_carrier, uint64_t dl_carrier, int freq_offset);
void nrue_ru_get_freq(const PHY_VARS_NR_UE *UE, uint64_t *ul_carrier, uint64_t *dl_carrier, int channel);
int nrue_ru_adjust_rx_gain(PHY_VARS_NR_UE *UE, int gain_change);
int nrue_ru_read(PHY_VARS_NR_UE *UE, openair0_timestamp_t *ptimestamp, void **buff, int nsamps, int num_antennas);
int nrue_ru_write(PHY_VARS_NR_UE *UE, openair0_timestamp_t timestamp, void **buff, int nsamps, int num_antennas, int flags);
int nrue_ru_write_reorder(PHY_VARS_NR_UE *UE, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags);
void nrue_ru_write_reorder_clear_context(PHY_VARS_NR_UE *UE);

#endif
