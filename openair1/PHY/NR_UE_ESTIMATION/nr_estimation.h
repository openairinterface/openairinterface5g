/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_ESTIMATION_DEFS__H__
#define __NR_ESTIMATION_DEFS__H__

#include "common/utils/bits.h"
#include "PHY/defs_nr_UE.h"

/** @addtogroup _PHY_PARAMETER_ESTIMATION_BLOCKS_
 * @{
 */

/* A function to perform the channel estimation of DL PRS signal */
int nr_prs_channel_estimation(uint8_t gNB_id,
                              uint8_t rsc_id,
                              int slot_prs,
                              const fapi_nr_dl_config_prs_pdu_rel16_t *prs_pdu,
                              PHY_VARS_NR_UE *ue,
                              const UE_nr_rxtx_proc_t *proc,
                              c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP]);

/* Generic function to find the peak of channel estimation buffer */
void peak_estimator(c16_t *buffer, int32_t buf_len, int32_t *peak_idx, int32_t *peak_val, int32_t mean_val);

/*!
\brief This function performs channel estimation including frequency and temporal interpolation
*/
void nr_pdcch_channel_estimation(const NR_DL_FRAME_PARMS *frame_parms,
                                 int nb_rb,
                                 int rb_offset,
                                 int dmrs_ref,
                                 uint16_t BWPStart,
                                 int32_t pdcch_est_size,
                                 c16_t pdcch_dl_ch_estimates[][pdcch_est_size],
                                 c16_t rxdataF[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size],
                                 c16_t *pilot);

c32_t nr_pbch_dmrs_correlation(const NR_DL_FRAME_PARMS *frame_parms,
                               const int symbol,
                               const int dmrss,
                               const int Nid_cell,
                               const int ssb_start_subcarrier,
                               const uint32_t nr_gold_pbch[NR_PBCH_DMRS_LENGTH_DWORD],
                               const c16_t rxdataF[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size]);

int nr_pbch_channel_estimation(const NR_DL_FRAME_PARMS *frame_parms,
                               const sl_nr_ue_phy_params_t *sl_phy_params,
                               c16_t dl_ch_estimates[frame_parms->ofdm_symbol_size],
                               const UE_nr_rxtx_proc_t *proc,
                               int dmrss,
                               uint ssb_index,
                               uint n_hf,
                               int ssb_start_subcarrier,
                               const c16_t rxdataF[frame_parms->ofdm_symbol_size],
                               bool sidelink,
                               uint Nid);

void nr_pdsch_channel_estimation(PHY_VARS_NR_UE *ue,
                                 const UE_nr_rxtx_proc_t *proc,
                                 const fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch,
                                 const freq_alloc_bitmap_t *freq_alloc,
                                 int nl,
                                 unsigned short p,
                                 unsigned char symbol,
                                 uint32_t pdsch_est_size,
                                 int32_t dl_ch_estimates[][pdsch_est_size],
                                 int rxdataFsize,
                                 c16_t rxdataF[][rxdataFsize],
                                 uint32_t *nvar);

int nr_adjust_synch_ue(const NR_DL_FRAME_PARMS *frame_parms,
                       PHY_VARS_NR_UE *ue,
                       const c16_t dl_ch_estimates_time[][frame_parms->ofdm_symbol_size],
                       uint8_t frame,
                       uint8_t slot,
                       short coef);

void nr_ue_measurements(PHY_VARS_NR_UE *ue,
                        const UE_nr_rxtx_proc_t *proc,
                        int number_rbs,
                        uint16_t l,
                        uint32_t pdsch_est_size,
                        int32_t dl_ch_estimates[][pdsch_est_size]);

uint32_t nr_ue_calculate_ssb_rsrp(const NR_DL_FRAME_PARMS *fp,
                                  const c16_t rxdataF[][fp->ofdm_symbol_size],
                                  int ssb_start_subcarrier);

void nr_ue_ssb_rsrp_measurements(PHY_VARS_NR_UE *ue,
                                 int ssb_index,
                                 const UE_nr_rxtx_proc_t *proc,
                                 const c16_t rxdataF[ue->frame_parms.nb_antennas_rx][ue->frame_parms.ofdm_symbol_size]);

// Structure to pass data to neighboring cell measurement task
typedef struct {
  UE_nr_rxtx_proc_t proc;
  PHY_VARS_NR_UE *ue;
  int nb_ant;
  uint32_t rxdata_size;
  c16_t rxdata_ant[];
} nr_meas_task_args_t;

void nr_ue_meas_neighboring_cell(void *arg);
void nr_ue_search_new_neighboring_cell(void *arg);

void nr_ue_rrc_measurements(PHY_VARS_NR_UE *ue,
                            const UE_nr_rxtx_proc_t *proc,
                            const c16_t rxdataF[ue->frame_parms.nb_antennas_rx][ue->frame_parms.ofdm_symbol_size]);

void phy_adjust_gain_nr(PHY_VARS_NR_UE *ue,
                        uint32_t rx_power_fil_dB,
                        uint8_t gNB_id);

void nr_pdsch_ptrs_processing(int nbRx,
                              c16_t ptrs_phase_per_slot[][14],
                              int32_t ptrs_re_per_slot[][14],
                              uint32_t pdsch_buf_size_max,
                              int nl,
                              int max_layers,
                              c16_t rxdataF_comp[][max_layers][pdsch_buf_size_max],
                              NR_DL_FRAME_PARMS *frame_parms,
                              fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config,
                              uint8_t nr_slot_rx,
                              unsigned char symbol,
                              int nb_rb,
                              uint16_t rnti,
                              uint16_t *ptrsSymbPos,
                              uint8_t *ptrsSymbIdx);

int nr_sl_psbch_rsrp_measurements(PHY_VARS_NR_UE *ue,
                                  sl_nr_ue_phy_params_t *sl_phy_params,
                                  const NR_DL_FRAME_PARMS *fp,
                                  const int symbol,
                                  const c16_t rxdataF[][fp->ofdm_symbol_size],
                                  bool use_SSS);
/** @}*/
#endif
