/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_UL_ESTIMATION_DEFS__H__
#define __NR_UL_ESTIMATION_DEFS__H__


#include "PHY/defs_gNB.h"
/** @addtogroup _PHY_PARAMETER_ESTIMATION_BLOCKS_
 * @{
 */

/*!
\brief This function performs channel estimation including frequency interpolation
\param gNB Pointer to gNB PHY variables
\param Ns slot number (0..19)
\param p
\param symbol symbol within slot
\param pusch_vars lower-phy PUSCH info
\param bwp_start_subcarrier first allocated subcarrier
\param pusch_pdu
\param max_ch maximum value of estimated channel
\param nvar
*/

int nr_pusch_channel_estimation(PHY_VARS_gNB *gNB,
                                unsigned char Ns,
                                int nl,
                                unsigned short p,
                                uint8_t lp,
                                unsigned char symbol,
                                NR_gNB_PUSCH *pusch_vars,
                                uint16_t ant_port_start,
                                unsigned short bwp_start_subcarrier,
                                const nfapi_nr_pusch_pdu_t *pusch_pdu,
                                int *max_ch,
                                uint32_t *nvar,
                                c16_t *pusch_dmrs_slot_mem,
                                c16_t *pusch_ch_est_dmrs_pos_slot_mem);

void dump_nr_I0_stats(FILE *fd,PHY_VARS_gNB *gNB);

void gNB_I0_measurements(PHY_VARS_gNB *gNB, int slot, int first_symb, int num_symb, uint32_t rb_mask_ul[14][MAX_BWP_SIZE]);

void nr_est_srs_timing_advance_offset(uint16_t ofdm_symbol_size,
                                      const c16_t srs_estimated_channel_time[][NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                      uint8_t ant,
                                      uint8_t N_ap,
                                      uint32_t samples_per_frame,
                                      uint16_t *timing_advance_offset,
                                      int16_t *timing_advance_offset_nsec);

void nr_pusch_ptrs_processing(PHY_VARS_gNB *gNB,
                              NR_DL_FRAME_PARMS *frame_parms,
                              const nfapi_nr_pusch_pdu_t *rel15_ul,
                              NR_gNB_PUSCH *pusch_vars,
                              uint8_t nr_tti_rx,
                              unsigned char symbol,
                              int nb_rx_ant,
                              uint32_t nb_re_pusch);

int nr_srs_ls_channel_estimation(int ant,
                                 int p_index,
                                 uint16_t ofdm_symbol_size,
                                 uint8_t N_symb_SRS,
                                 const nfapi_nr_srs_pdu_t *srs_pdu,
                                 const nr_srs_info_t *nr_srs_info,
                                 const c16_t *srs_generated_signal,
                                 c16_t srs_received_signal[ofdm_symbol_size * N_symb_SRS],
                                 c16_t srs_ls_estimated_channel[ofdm_symbol_size * N_symb_SRS],
                                 delay_t *delay);

int nr_srs_channel_interpolation(int p_index,
                                 uint16_t ofdm_symbol_size,
                                 uint16_t first_carrier_offset,
                                 uint8_t N_symb_SRS,
                                 const nfapi_nr_srs_pdu_t *srs_pdu,
                                 const nr_srs_info_t *nr_srs_info,
                                 const c16_t srs_ls_estimated_channel[ofdm_symbol_size * N_symb_SRS],
                                 int est_delay,
                                 c16_t srs_estimated_channel_freq[ofdm_symbol_size * N_symb_SRS],
                                 c16_t srs_estimated_channel_time[NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                 c16_t srs_estimated_channel_time_shifted[NR_SRS_IDFT_OVERSAMP_FACTOR * ofdm_symbol_size],
                                 uint32_t *signal_power,
                                 c16_t delay_table[2 * MAX_DELAY_COMP + 1][NR_MAX_OFDM_SYMBOL_SIZE]);

void nr_freq_equalization(NR_DL_FRAME_PARMS *frame_parms,
                          c16_t *rxdataF_comp,
                          c16_t *ul_ch_mag,
                          c16_t *ul_ch_mag_b,
                          unsigned char symbol,
                          unsigned short Msc_RS,
                          unsigned char Qm);

void nr_init_fde(void);

#endif
/** @}*/
