/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_PHY_COMMON__H__
#define __NR_PHY_COMMON__H__

#include "common/platform_types.h"
#include "common/utils/nr/nr_common.h"

typedef struct {
  /// bit mask of PT-RS ofdm symbol indicies
  uint16_t ptrs_symbols;
  int n_ptrs;
} nr_ptrs_info_t;

void init_byte2m128i(void);
void freq2time(uint16_t ofdm_symbol_size, int16_t *freq_signal, int16_t *time_signal);
int nr_idft_prescale_shift(const c16_t *x, int n);
void nr_est_delay(int ofdm_symbol_size, const c16_t *ls_est, c16_t *ch_estimates_time, delay_t *delay);
unsigned int nr_get_tx_amp(int power_dBm, int power_max_dBm, int total_nb_rb, int nb_rb);
void nr_fo_compensation(double fo_Hz, int samples_per_ms, int sample_offset, const c16_t *rxdata_in, c16_t *rxdata_out, int size);
void nr_channel_level(const int symbol,
                      const int size_est,
                      const c16_t ch_estimates_ext[][size_est],
                      const int nb_rx,
                      int32_t avg[nb_rx],
                      const uint32_t len);
void nr_scale_channel(int size, int ch_estimates_ext[][size], int symb, uint32_t len, int nrOfLayers, int nb_rx, int shift_ch_ext);
int nr_get_ssb_start_sc(int scs,
                        int ssb_offset_point_a,
                        int ssb_sco,
                        frequency_range_t freq_range);

void get_s1_s2(int *s1, int *s2, int rbsize, int nr_symbols, int start_symb, uint16_t dmrs_symb_pos, uint16_t ptrs_symb, int n_ptrs);
double get_beta_offset_harq_ack(uint8_t beta_offset_index);
double get_beta_offset_csi(const uint8_t beta_offset_idx);
double get_alpha_scaling_value(uint8_t alpha_scaling);
uint32_t get_Qd(const uint32_t ouci,
                double beta,
                double alpha,
                const uint32_t eff_bits,
                const uint32_t s1,
                const uint32_t s2,
                const uint32_t sub);
#endif
