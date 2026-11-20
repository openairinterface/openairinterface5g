/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_PHY_INIT_H_
#define NR_PHY_INIT_H_

#include "PHY/defs_gNB.h"
#include "PHY/defs_nr_UE.h"
#include "nr_parms.h"

int nr_get_ssb_start_symbol(const NR_DL_FRAME_PARMS *fp, uint8_t i_ssb);
int init_nr_ue_signal(PHY_VARS_NR_UE *ue,int nb_connected_eNB);
void term_nr_ue_signal(PHY_VARS_NR_UE *ue);
void init_nr_ue_transport(PHY_VARS_NR_UE *ue);
void term_nr_ue_transport(PHY_VARS_NR_UE *ue);
void init_phy_nr_measurements(PHY_VARS_NR_UE *ue);
void init_nr_ue_sl_transport(PHY_VARS_NR_UE *ue);
void term_nr_ue_sl_transport(PHY_VARS_NR_UE *ue);
void phy_init_nr_gNB(PHY_VARS_gNB *gNB);
int init_codebook_gNB(PHY_VARS_gNB *gNB);
void nr_phy_config_request(NR_PHY_Config_t *gNB);
void nr_phy_config_request_sim(PHY_VARS_gNB *gNB,int N_RB_DL,int N_RB_UL,int mu,int Nid_cell,uint64_t position_in_burst);
void phy_free_nr_gNB(PHY_VARS_gNB *gNB);
int l1_north_init_gNB(void);
void init_nr_transport(PHY_VARS_gNB *gNB);
void reset_nr_transport(PHY_VARS_gNB *gNB);

void RCconfig_nrUE_prs(void *cfg);
void init_nr_prs_ue_vars(PHY_VARS_NR_UE *ue);
void nr_init_dl_harq_processes(NR_DL_UE_HARQ_t harq_list[2][NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb);
void nr_init_ul_harq_processes(NR_UL_UE_HARQ_t harq_list[NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb, int num_ant_tx);
void free_nr_ue_dl_harq(NR_DL_UE_HARQ_t harq_list[2][NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb);
void free_nr_ue_ul_harq(NR_UL_UE_HARQ_t harq_list[NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb, int num_ant_tx);
void nr_init_pdsch_buffers(pdsch_scratch_t *buffers, int num_actors, const NR_DL_FRAME_PARMS *fp, bool do_ml);
void free_nr_ue_pdsch_buffers(pdsch_scratch_t *buffers, int num_actors);

void phy_init_nr_top(PHY_VARS_NR_UE *ue);
void phy_term_nr_top(void);
#ifndef __cplusplus
void init_delay_table(uint16_t ofdm_symbol_size,
                      int max_delay_comp,
                      int max_ofdm_symbol_size,
                      c16_t delay_table[][max_ofdm_symbol_size]);
#endif
void sl_ue_phy_init(PHY_VARS_NR_UE *UE);
#endif
