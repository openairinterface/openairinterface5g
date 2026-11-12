/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_phy_common.h"
#include <complex.h>
#include "common/utils/bits.h"
#include "PHY/impl_defs_top.h"
#include "common/utils/LOG/log.h"
#include "PHY/TOOLS/tools_defs.h"
#ifdef __aarch64__
#define USE_128BIT
#endif

#define PEAK_DETECT_THRESHOLD 15
simde__m128i byte2m128i[256];
void init_byte2m128i(void)
{
  for (int s = 0; s < 256; s++) {
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * (s & 1)), 0);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 1) & 1)), 1);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 2) & 1)), 2);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 3) & 1)), 3);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 4) & 1)), 4);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 5) & 1)), 5);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 6) & 1)), 6);
    byte2m128i[s] = simde_mm_insert_epi16(byte2m128i[s], (1 - 2 * ((s >> 7) & 1)), 7);
  }
}

void init_delay_table(uint16_t ofdm_symbol_size,
                      int max_delay_comp,
                      int max_ofdm_symbol_size,
                      c16_t delay_table[][max_ofdm_symbol_size])
{
  for (int delay = -max_delay_comp; delay <= max_delay_comp; delay++) {
    for (int k = 0; k < ofdm_symbol_size; k++) {
      double complex delay_cexp = cexp(I * (2.0 * M_PI * k * delay / ofdm_symbol_size));
      delay_table[max_delay_comp + delay][k].r = (int16_t)round(256 * creal(delay_cexp));
      delay_table[max_delay_comp + delay][k].i = (int16_t)round(256 * cimag(delay_cexp));
    }
  }
}

/* L1 norm of n int16 values */
static int64_t l1_norm_i16(const int16_t *x, int n)
{
  simde__m256i acc = simde_mm256_setzero_si256();
  int i = 0;
  for (; i + 16 <= n; i += 16) {
    const simde__m256i v = simde_mm256_loadu_si256((const simde__m256i *)(x + i));
    acc = simde_mm256_add_epi32(acc, simde_mm256_abs_epi32(simde_mm256_cvtepi16_epi32(simde_mm256_castsi256_si128(v))));
    acc = simde_mm256_add_epi32(acc, simde_mm256_abs_epi32(simde_mm256_cvtepi16_epi32(simde_mm256_extracti128_si256(v, 1))));
  }
  int32_t lane[8];
  simde_mm256_storeu_si256((simde__m256i *)lane, acc);
  int64_t l1 = 0;
  for (int k = 0; k < 8; k++)
    l1 += lane[k];
  for (; i < n; i++)
    l1 += abs(x[i]);
  return l1;
}

/* smallest right shift of the input for which the n-point IDFT output fits int16 with one bit of margin
 * (2n bounds the rounding of the shift). The bound is on the output: it holds for contiguous inputs such as the LS
 * estimates, a sparse comb input (stride >= 4) can still saturate inside the IDFT stages */
int nr_idft_prescale_shift(const c16_t *x, int n)
{
  // sum of |re| + |im|, which is at least the sum of |x|: the bound is conservative by at most a factor sqrt(2)
  const int64_t l1 = l1_norm_i16((const int16_t *)x, 2 * n);
  int shift = 0;
  while (shift < 16) {
    const int64_t bound = (l1 >> shift) + 2 * (int64_t)n;
    if (bound * bound <= 16384LL * 16384 * n)
      break;
    shift++;
  }
  return shift;
}

void freq2time(uint16_t ofdm_symbol_size, int16_t *freq_signal, int16_t *time_signal)
{
  const idft_size_idx_t idft_size = get_idft(ofdm_symbol_size);
  idft(idft_size, freq_signal, time_signal, 1);
}

void nr_est_delay(int ofdm_symbol_size, const c16_t *ls_est, c16_t *ch_estimates_time, delay_t *delay)
{
  // prescale the input so that the IDFT output fits int16; delay_max_val is returned unscaled
  const int shift = nr_idft_prescale_shift(ls_est, ofdm_symbol_size);
  const c16_t *in = ls_est;
  c16_t scaled[shift ? ofdm_symbol_size : 1] __attribute__((aligned(32)));
  if (shift) {
    int i = 0;
    for (; i + 8 <= ofdm_symbol_size; i += 8)
      simde_mm256_storeu_si256((simde__m256i *)(scaled + i),
                               simde_mm256_srai_epi16(simde_mm256_loadu_si256((const simde__m256i *)(ls_est + i)), shift));
    for (; i < ofdm_symbol_size; i++)
      scaled[i] = (c16_t){ls_est[i].r >> shift, ls_est[i].i >> shift};
    in = scaled;
  }
  idft(get_idft(ofdm_symbol_size), (int16_t *)in, (int16_t *)ch_estimates_time, 1);

  int max_pos = delay->delay_max_pos;
  int64_t max_val = delay->delay_max_val >> (2 * shift);
  const int sync_pos = 0;

  uint64_t mean_val = 0;
  for (int i = 0; i < ofdm_symbol_size; i++) {
    int temp = c16amp2(ch_estimates_time[i]) >> 1;
    mean_val += temp;
    if (temp > max_val) {
      max_pos = i;
      max_val = temp;
    }
  }
  mean_val /= ofdm_symbol_size;

  if (max_pos > ofdm_symbol_size / 2)
    max_pos = max_pos - ofdm_symbol_size;

  delay->delay_max_pos = max_pos;
  delay->delay_max_val = max_val << (2 * shift);

  // The peak in general is quite clear. It only gives a small peak when the noise is high, generally obtaining an incorrect
  // estimated delay, and causing the delay compensation to worsen the result instead of improving it. After analyzing several
  // peaks, and doing many tests, a PEAK_DETECT_THRESHOLD = 15 is an adequate value, to apply delay compensation only when there is
  // clearly a peak
  delay->valid = mean_val > 0 && max_val / mean_val > PEAK_DETECT_THRESHOLD;
  delay->est_delay = delay->valid ? max_pos - sync_pos : 0;
}

unsigned int nr_get_tx_amp(int power_dBm, int power_max_dBm, int total_nb_rb, int nb_rb)
{
  // assume power at AMP is 20dBm
  // if gain = 20 (power == 40)
  int gain_dB = power_dBm - power_max_dBm;
  double gain_lin;

  gain_lin = pow(10, .1 * gain_dB);
  if ((nb_rb > 0) && (nb_rb <= total_nb_rb)) {
    return ((int)(AMP * sqrt(gain_lin * total_nb_rb / (double)nb_rb)));
  } else {
    LOG_E(PHY, "Illegal nb_rb/N_RB_UL combination (%d/%d)\n", nb_rb, total_nb_rb);
    // mac_xface->macphy_exit("");
  }
  return (0);
}

// compute average channel_level on each antenna
void nr_channel_level(const int symbol,
                      const int size_est,
                      const c16_t ch_estimates_ext[][size_est],
                      const int nb_rx,
                      int32_t avg[nb_rx],
                      const uint32_t len)
{
  for (int aarx = 0; aarx < nb_rx; aarx++) {
    // compute average squared module
    avg[aarx] = signal_energy_nodc(ch_estimates_ext[aarx] + symbol * len, len);
    LOG_D(PHY, "Channel level: %d\n", avg[aarx]);
  }
}

void nr_fo_compensation(double fo_Hz, int samples_per_ms, int sample_offset, const c16_t *rxdata_in, c16_t *rxdata_out, int size)
{
  const double phase_inc = -fo_Hz / (samples_per_ms * 1000);
  double phase = sample_offset * phase_inc;
  phase -= (int)phase;
#if 1
  // The bottleneck is the calculation of the complex rotation values using get_sin_cos().
  // This code path does not compute these values for the complete OFDM symbol, but only for a smaller CHUNK size.
  // After applying the rotation to a CHUNK size of the output, these rotation values are efficiently rotated further by `rot_vec`.
  // Unfortunately, this propagates small errors from one chunk to the next.
  // Therefore, there is a tradeoff between speed (better with small CHUNK sizes) and accuracy (better with large CHUNK sizes).
#define CHUNK 128
  c16_t rot[CHUNK] __attribute__((aligned(32)));
  for (int i = 0; i < CHUNK; i++) {
    rot[i] = get_sin_cos(phase);
    phase += phase_inc;
  }
  const double chunk_phase = 2 * M_PI * CHUNK * phase_inc;
  const c16_t rot_vec = {round(cos(chunk_phase) * (1 << 14)), round(sin(chunk_phase) * (1 << 14))};
  while (size > CHUNK) {
    mult_complex_vectors(rxdata_in, rot, rxdata_out, CHUNK, 14);
    rotate_cpx_vector(rot, rot_vec, rot, CHUNK, 14);
    rxdata_in += CHUNK;
    rxdata_out += CHUNK;
    size -= CHUNK;
  }
  mult_complex_vectors(rxdata_in, rot, rxdata_out, size, 14);
#else
  // This code path computes the complex rotation values for the complete OFDM symbol using get_sin_cos().
  // This is more accurate, but also slower than the code path above.
  c16_t rot[size] __attribute__((aligned(32)));
  for (int i = 0; i < size; i++) {
    rot[i] = get_sin_cos(phase);
    phase += phase_inc;
  }
  mult_complex_vectors(rxdata_in, rot, rxdata_out, size, 14);
#endif
}

/*!
* Setting the first subcarrier
* 3GPP TS 38.211 sections 7.4.3.1 and 4.4.4.2
* for FR1 offsetToPointA and k_SSB are expressed in terms of 15 kHz SCS
* for FR2 offsetToPointA is expressed in terms of 60 kHz SCS and k_SSB expressed in terms of the SCS provided
* by the higher-layer parameter subCarrierSpacingCommon
*/
int nr_get_ssb_start_sc(int scs, int ssb_offset_point_a, int ssb_sco, frequency_range_t freq_range)
{
  const int prb_offset =
      (freq_range == FR1) ? ssb_offset_point_a >> scs : ssb_offset_point_a >> (scs - 2);
  const int sc_offset =
      (freq_range == FR1) ? ssb_sco >> scs : ssb_sco;

  int ssb_start_subcarrier = (12 * prb_offset + sc_offset);

  LOG_D(NR_PHY, "prb_offset:%d, ssb_subcarrier_offset:%d,scs :%d, Fr:%d, ssb_start_subcarrier:%d\n",
                        prb_offset, ssb_sco, scs, freq_range, ssb_start_subcarrier);

  return ssb_start_subcarrier;
}

void get_s1_s2(int *s1, int *s2, int rbsize, int nr_symbols, int start_symb, uint16_t dmrs_symb_pos, uint16_t ptrs_symb, int n_ptrs)
{
  // Calculate s1: total number of non-DMRS REs in allocation
  *s1 = rbsize * NR_NB_SC_PER_RB * (nr_symbols - get_num_dmrs(dmrs_symb_pos));

  // Calculate s2: number of non-DMRS REs after first DMRS symbol
  // __builtin_ctz returns the index of the first set bit
  int first_dmrs_symbol = __builtin_ctz(dmrs_symb_pos);
  // mask with everything from (first_dmrs_symbol + 1) to the end
  uint32_t range_mask = ((1U << nr_symbols) - 1) << start_symb;
  uint32_t post_dmrs_mask = range_mask & ~((1U << (first_dmrs_symbol + 1)) - 1);
  // number of non-DMRS REs bits in that post-DMRS range
  uint32_t non_dmrs_bits = post_dmrs_mask & ~dmrs_symb_pos;
  int num_non_dmrs_symbols = count_bits(&non_dmrs_bits, 1);
  *s2 = num_non_dmrs_symbols * rbsize * NR_NB_SC_PER_RB;

  if (ptrs_symb) {
    // for any OFDM symbol that does not carry DMRS of the PUSCH, M_UCI = M_PUSCH − M_PTRS
    uint32_t non_dmrs_ptrs_mask = ptrs_symb & ~dmrs_symb_pos;
    int ptrs_symb_in_alloc = count_bits(&non_dmrs_ptrs_mask, 1);
    *s1 -= (ptrs_symb_in_alloc * n_ptrs);
    uint32_t ptrs_in_post_window = ptrs_symb & post_dmrs_mask;
    int num_ptrs_symbols_s2 = count_bits(&ptrs_in_post_window, 1);
    *s2 -= (num_ptrs_symbols_s2 * n_ptrs);
  }
}

/*
 * This function gets the CRC size of UCI according to 6.3.1.2.1 of 38.212
 */
static int get_crc_uci(const uint32_t ouci)
{
  int L = 0;
  if (ouci > 19) {
    L = 11;
  } else if (ouci > 11) {
    L = 6;
  } else {
    L = 0;
  }
  return L;
}

uint32_t get_Qd(const uint32_t ouci,
                double beta,
                double alpha,
                const uint32_t eff_bits,
                const uint32_t s1,
                const uint32_t s2,
                const uint32_t sub)
{
  // as described in section 6.3.2.4.1 of 38.212
  if (ouci == 0)
    return 0;
  uint32_t first_term = ceil(((double)ouci + get_crc_uci(ouci)) * (double)beta * s1 / eff_bits);
  uint32_t second_term = ceil(alpha * s2) - sub;
  return (first_term < second_term) ? first_term : second_term;
}

double get_alpha_scaling_value(uint8_t alpha_scaling)
{
  switch (alpha_scaling) {
    case 0:
      return 0.5;
    case 1:
      return 0.65;
    case 2:
      return 0.8;
    case 3:
      return 1.0;
    default:
      AssertFatal(false, "Invalid alpha_scaling value %d, valid range is 0-3", alpha_scaling);
      return 1.0;
  }
}

// Function to lookup beta offset value from Table 9.3-1 in TS 38.213
double get_beta_offset_harq_ack(uint8_t beta_offset_index)
{
  static const double beta_offset_values[21] = {
      1.000, // Index 0
      2.000, // Index 1
      2.500, // Index 2
      3.125, // Index 3
      4.000, // Index 4
      5.000, // Index 5
      6.250, // Index 6
      8.000, // Index 7
      10.000, // Index 8
      12.625, // Index 9
      15.875, // Index 10
      20.000, // Index 11
      31.000, // Index 12
      50.000, // Index 13
      80.000, // Index 14
      126.000, // Index 15
      0.6, // Index 16
      0.4, // Index 17
      0.2, // Index 18
      0.1, // Index 19
      0.05, // Index 20
  };
  if (beta_offset_index > 20) {
    LOG_E(PHY, "Invalid beta_offset_index %d, using default value\n", beta_offset_index);
    return 20.000; // Default value using index 11
  }
  return beta_offset_values[beta_offset_index];
}

// Function to lookup beta offset value from Table 9.3-2 in TS 38.213
double get_beta_offset_csi(const uint8_t beta_offset_idx)
{
  static const double beta_offset_values[19] = {1.125,
                                                1.250,
                                                1.375,
                                                1.625,
                                                1.750,
                                                2.000,
                                                2.250,
                                                2.500,
                                                2.875,
                                                3.125,
                                                3.500,
                                                4.000,
                                                5.000,
                                                6.250,
                                                8.000,
                                                10.000,
                                                12.625,
                                                15.875,
                                                20.000};

  if (beta_offset_idx >= sizeofArray(beta_offset_values)) {
    LOG_E(PHY, "Invalid beta_offset_index %d, using default value\n", beta_offset_idx);
    return beta_offset_values[9];
  }

  return beta_offset_values[beta_offset_idx];
}
