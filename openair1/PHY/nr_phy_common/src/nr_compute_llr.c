/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_compute_llr.h"
#include "PHY/impl_defs_top.h"
#include "PHY/TOOLS/tools_defs.h"
#ifdef __aarch64__
#define USE_128BIT
#endif

void nr_compute_llr(c16_t *rxdataF_comp,
                    c16_t *ch_mag,
                    c16_t *ch_magb,
                    c16_t *ch_magc,
                    int16_t *llr,
                    uint32_t nb_re,
                    uint8_t symbol,
                    uint8_t mod_order)
{
  switch (mod_order) {
    case 2:
      nr_qpsk_llr(rxdataF_comp, llr, nb_re);
      break;
    case 4:
      nr_16qam_llr(rxdataF_comp, ch_mag, llr, nb_re);
      break;
    case 6:
      nr_64qam_llr(rxdataF_comp, ch_mag, ch_magb, llr, nb_re);
      break;
    case 8:
      nr_256qam_llr(rxdataF_comp, ch_mag, ch_magb, ch_magc, llr, nb_re);
      break;
    default:
      AssertFatal(false, "nr_compute_llr: invalid mod_order, symbol=%d, Qm=%d\n", symbol, mod_order);
      break;
  }
}

/*
 * This function computes the LLRs of stream 0 (s_0) in presence of the interfering stream 1 (s_1) assuming that both symbols are
 * QPSK. It can be used for both MU-MIMO interference-aware receiver or for SU-MIMO receivers.
 *
 * Input:
 *   stream0_in:  MF filter output for 1st stream, i.e., y0' = h0'*y0
 *   stream1_in:  MF filter output for 2nd stream, i.e., y1' = h1'*y0
 *   rho01:       Channel cross correlation, i.e., rho01 = h0'*h1
 *   length:      Number of resource elements
 *
 * Output:
 *   stream0_out: Output LLRs for 1st stream
 */
void nr_qpsk_llr_2layer(c16_t *stream0_in, c16_t *stream1_in, int16_t *stream0_out, c16_t *rho01, uint32_t length)
{
#ifdef USE_128BIT
  simde__m128i *rho01_128i = (simde__m128i *)rho01;
  simde__m128i *stream0_128i_in = (simde__m128i *)stream0_in;
  simde__m128i *stream1_128i_in = (simde__m128i *)stream1_in;
  simde__m128i *stream0_128i_out = (simde__m128i *)stream0_out;
  simde__m128i ONE_OVER_2_SQRT_2 = simde_mm_set1_epi16(23170); // round(2 ^ 16 / (2 * sqrt(2)))

  // In each iteration, we take 8 complex symbols
  for (int i = 0; i < (length + 3) >> 2; i += 2) {
    /// Compute real and imaginary parts of MF output for stream 0 (desired stream)
    simde__m128i y0r, y0i;
    oai_mm_separate_real_imag_parts(&y0r, &y0i, stream0_128i_in[i], stream0_128i_in[i + 1]);
    simde__m128i y0r_over2 = simde_mm_mulhi_epi16(y0r, ONE_OVER_2_SQRT_2);
    y0r_over2 = simde_mm_slli_epi16(y0r_over2, 1); // y0r_over2 = Re(y0) / sqrt(2)
    simde__m128i y0i_over2 = simde_mm_mulhi_epi16(y0i, ONE_OVER_2_SQRT_2);
    y0i_over2 = simde_mm_slli_epi16(y0i_over2, 1); // y0i_over2 = Im(y0) / sqrt(2)

    /// Compute real and imaginary parts of MF output for stream 1 (interference stream)
    simde__m128i y1r_over2, y1i_over2;
    oai_mm_separate_real_imag_parts(&y1r_over2, &y1i_over2, stream1_128i_in[i], stream1_128i_in[i + 1]);
    y1r_over2 = simde_mm_srai_epi16(y1r_over2, 1); // y1r_over2 = Re(y1) / 2
    y1i_over2 = simde_mm_srai_epi16(y1i_over2, 1); // y1i_over2 = Im(y1) / 2

    /// Get real and imaginary parts of rho
    simde__m128i rhor, rhoi;
    oai_mm_separate_real_imag_parts(&rhor, &rhoi, rho01_128i[i], rho01_128i[i + 1]);

    /// Compute |psi_r| and |psi_i|

    // psi_r = rhor * xR + rhoi * xI
    // psi_i = rhor * xI - rhoi * xR

    // Put (rho_r + rho_i)/(2*sqrt(2)) in rho_p
    // rhor * xR + rhoi * xI  --> xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // rhor * xI - rhoi * xR  --> xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i rho_p = simde_mm_adds_epi16(rhor, rhoi); // rho_p = Re(rho) + Im(rho)
    rho_p = simde_mm_mulhi_epi16(rho_p, ONE_OVER_2_SQRT_2); // rho_p = rho_p / (2*sqrt(2))

    // Put (rho_r - rho_i)/(2*sqrt(2)) in rho_m
    // rhor * xR + rhoi * xI  --> xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // rhor * xI - rhoi * xR  --> xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i rho_m = simde_mm_subs_epi16(rhor, rhoi); // rho_m = Re(rho) - Im(rho)
    rho_m = simde_mm_mulhi_epi16(rho_m, ONE_OVER_2_SQRT_2); // rho_m = rho_m / (2*sqrt(2))

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i abs_psi_rpm = simde_mm_subs_epi16(rho_p, y1r_over2); // psi_rpm = rho_p - y1r/2
    abs_psi_rpm = simde_mm_abs_epi16(abs_psi_rpm); // abs_psi_rpm = |psi_rpm|

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i abs_psi_imm = simde_mm_subs_epi16(rho_m, y1i_over2); // psi_imm = rho_m - y1i/2
    abs_psi_imm = simde_mm_abs_epi16(abs_psi_imm); // abs_psi_imm = |psi_imm|

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    simde__m128i abs_psi_rmm = simde_mm_subs_epi16(rho_m, y1r_over2); // psi_rmm = rho_m - y1r/2
    abs_psi_rmm = simde_mm_abs_epi16(abs_psi_rmm); // abs_psi_rmm = |psi_rmm|

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i abs_psi_ipm = simde_mm_subs_epi16(rho_p, y1i_over2); // psi_ipm = rho_p - y1i/2
    abs_psi_ipm = simde_mm_abs_epi16(abs_psi_ipm); // abs_psi_ipm = |psi_ipm|

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    simde__m128i abs_psi_rpp = simde_mm_adds_epi16(rho_p, y1r_over2); // psi_rpp = rho_p + y1r/2
    abs_psi_rpp = simde_mm_abs_epi16(abs_psi_rpp); // abs_psi_rpp = |psi_rpp|

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    simde__m128i abs_psi_imp = simde_mm_adds_epi16(rho_m, y1i_over2); // psi_imp = rho_m + y1i/2
    abs_psi_imp = simde_mm_abs_epi16(abs_psi_imp); // abs_psi_imp = |psi_imp|

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m128i abs_psi_rmp = simde_mm_adds_epi16(rho_m, y1r_over2); // psi_rmp = rho_m + y1r/2
    abs_psi_rmp = simde_mm_abs_epi16(abs_psi_rmp); // abs_psi_rmp = |psi_rmp|

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    simde__m128i abs_psi_ipp = simde_mm_adds_epi16(rho_p, y1i_over2); // psi_ipm = rho_p + y1i/2
    abs_psi_ipp = simde_mm_abs_epi16(abs_psi_ipp); // abs_psi_ipp = |psi_ipm|

    /// Compute bit metrics (lambda)

    // lambda = max { |psi_r - y1r| * |x2R| + |psi_i - y1i| * |x2I| + y0r * xR + y0i * xI}

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_re_p = abs_psi_rpm + abs_psi_imm + y0r/sqrt(2) + y0i/sqrt(2)
    simde__m128i bit_met_num_re_p = simde_mm_adds_epi16(abs_psi_rpm, abs_psi_imm);
    bit_met_num_re_p = simde_mm_adds_epi16(bit_met_num_re_p, y0r_over2);
    bit_met_num_re_p = simde_mm_adds_epi16(bit_met_num_re_p, y0i_over2);

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // For numerator: bit_met_num_re_m = abs_psi_rmm + abs_psi_ipp + y0r/sqrt(2) - y0i/sqrt(2)
    simde__m128i bit_met_num_re_m = simde_mm_adds_epi16(abs_psi_rmm, abs_psi_ipp);
    bit_met_num_re_m = simde_mm_adds_epi16(bit_met_num_re_m, y0r_over2);
    bit_met_num_re_m = simde_mm_subs_epi16(bit_met_num_re_m, y0i_over2);

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    // For denominator: bit_met_den_re_p = abs_psi_rmp + abs_psi_ipm - y0r/sqrt(2) + y0i/sqrt(2)
    simde__m128i bit_met_den_re_p = simde_mm_adds_epi16(abs_psi_rmp, abs_psi_ipm);
    bit_met_den_re_p = simde_mm_subs_epi16(bit_met_den_re_p, y0r_over2);
    bit_met_den_re_p = simde_mm_adds_epi16(bit_met_den_re_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_re_m = abs_psi_rpp + abs_psi_imp - y0r/sqrt(2) - y0i/sqrt(2)
    simde__m128i bit_met_den_re_m = simde_mm_adds_epi16(abs_psi_rpp, abs_psi_imp);
    bit_met_den_re_m = simde_mm_subs_epi16(bit_met_den_re_m, y0r_over2);
    bit_met_den_re_m = simde_mm_subs_epi16(bit_met_den_re_m, y0i_over2);

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_im_p = abs_psi_rpm + abs_psi_imm + y0r/sqrt(2) + y0i/sqrt(2)
    simde__m128i bit_met_num_im_p = simde_mm_adds_epi16(abs_psi_rpm, abs_psi_imm);
    bit_met_num_im_p = simde_mm_adds_epi16(bit_met_num_im_p, y0r_over2);
    bit_met_num_im_p = simde_mm_adds_epi16(bit_met_num_im_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_im_m = abs_psi_rmp + abs_psi_ipm - y0r/sqrt(2) + y0i/sqrt(2)
    simde__m128i bit_met_num_im_m = simde_mm_adds_epi16(abs_psi_rmp, abs_psi_ipm);
    bit_met_num_im_m = simde_mm_subs_epi16(bit_met_num_im_m, y0r_over2);
    bit_met_num_im_m = simde_mm_adds_epi16(bit_met_num_im_m, y0i_over2);

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_im_p = abs_psi_rmm + abs_psi_ipp + y0r/sqrt(2) - y0i/sqrt(2)
    simde__m128i bit_met_den_im_p = simde_mm_adds_epi16(abs_psi_rmm, abs_psi_ipp);
    bit_met_den_im_p = simde_mm_adds_epi16(bit_met_den_im_p, y0r_over2);
    bit_met_den_im_p = simde_mm_subs_epi16(bit_met_den_im_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_im_m = abs_psi_rpp + abs_psi_imp - y0r/sqrt(2)- y0i/sqrt(2)
    simde__m128i bit_met_den_im_m = simde_mm_adds_epi16(abs_psi_rpp, abs_psi_imp);
    bit_met_den_im_m = simde_mm_subs_epi16(bit_met_den_im_m, y0r_over2);
    bit_met_den_im_m = simde_mm_subs_epi16(bit_met_den_im_m, y0i_over2);

    /// Compute the LLRs

    // LLR = lambda(c==1) - lambda(c==0)

    simde__m128i logmax_num_re0 = simde_mm_max_epi16(bit_met_num_re_p, bit_met_num_re_m); // LLR of the first bit: Bit = 1
    simde__m128i logmax_den_re0 = simde_mm_max_epi16(bit_met_den_re_p, bit_met_den_re_m); // LLR of the first bit: Bit = 0
    simde__m128i logmax_num_im0 = simde_mm_max_epi16(bit_met_num_im_p, bit_met_num_im_m); // LLR of the second bit: Bit = 1
    simde__m128i logmax_den_im0 = simde_mm_max_epi16(bit_met_den_im_p, bit_met_den_im_m); // LLR of the second bit: Bit = 0

    y0r = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0); // LLR of first bit [L1(1), L1(2), L1(3), L1(4)]
    y0i = simde_mm_subs_epi16(logmax_num_im0, logmax_den_im0); // LLR of second bit [L2(1), L2(2), L2(3), L2(4)]

    // [L1(1), L2(1), L1(2), L2(2)]
    simde_mm_storeu_si128(&stream0_128i_out[i], simde_mm_unpacklo_epi16(y0r, y0i));

    // false if only 2 REs remain
    if (i < ((length >> 1) - 1)) {
      simde_mm_storeu_si128(&stream0_128i_out[i + 1], simde_mm_unpackhi_epi16(y0r, y0i));
    }
  }
#else

  simde__m256i *rho01_256i = (simde__m256i *)rho01;
  simde__m256i *stream0_256i_in = (simde__m256i *)stream0_in;
  simde__m256i *stream1_256i_in = (simde__m256i *)stream1_in;
  simde__m256i *stream0_256i_out = (simde__m256i *)stream0_out;
  simde__m256i ONE_OVER_2_SQRT_2 = simde_mm256_set1_epi16(23170); // round(2 ^ 16 / (2 * sqrt(2)))

  // In each iteration, we take 16 complex symbols
  for (int i = 0; i < (length + 7) >> 3; i += 2) {
    /// Compute real and imaginary parts of MF output for stream 0 (desired stream)
    simde__m256i y0r, y0i;
    oai_mm256_separate_real_imag_parts(&y0r, &y0i, stream0_256i_in[i], stream0_256i_in[i + 1]);
    simde__m256i y0r_over2 = simde_mm256_mulhi_epi16(y0r, ONE_OVER_2_SQRT_2);
    y0r_over2 = simde_mm256_slli_epi16(y0r_over2, 1); // y0r_over2 = Re(y0) / sqrt(2)
    simde__m256i y0i_over2 = simde_mm256_mulhi_epi16(y0i, ONE_OVER_2_SQRT_2);
    y0i_over2 = simde_mm256_slli_epi16(y0i_over2, 1); // y0i_over2 = Im(y0) / sqrt(2)

    /// Compute real and imaginary parts of MF output for stream 1 (interference stream)
    simde__m256i y1r_over2, y1i_over2;
    oai_mm256_separate_real_imag_parts(&y1r_over2, &y1i_over2, stream1_256i_in[i], stream1_256i_in[i + 1]);
    y1r_over2 = simde_mm256_srai_epi16(y1r_over2, 1); // y1r_over2 = Re(y1) / 2
    y1i_over2 = simde_mm256_srai_epi16(y1i_over2, 1); // y1i_over2 = Im(y1) / 2

    /// Get real and imaginary parts of rho
    simde__m256i rhor, rhoi;
    oai_mm256_separate_real_imag_parts(&rhor, &rhoi, rho01_256i[i], rho01_256i[i + 1]);

    /// Compute |psi_r| and |psi_i|

    // psi_r = rhor * xR + rhoi * xI
    // psi_i = rhor * xI - rhoi * xR

    // Put (rho_r + rho_i)/(2*sqrt(2)) in rho_p
    // rhor * xR + rhoi * xI  --> xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // rhor * xI - rhoi * xR  --> xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i rho_p = simde_mm256_adds_epi16(rhor, rhoi); // rho_p = Re(rho) + Im(rho)
    rho_p = simde_mm256_mulhi_epi16(rho_p, ONE_OVER_2_SQRT_2); // rho_p = rho_p / (2*sqrt(2))

    // Put (rho_r - rho_i)/(2*sqrt(2)) in rho_m
    // rhor * xR + rhoi * xI  --> xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // rhor * xI - rhoi * xR  --> xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i rho_m = simde_mm256_subs_epi16(rhor, rhoi); // rho_m = Re(rho) - Im(rho)
    rho_m = simde_mm256_mulhi_epi16(rho_m, ONE_OVER_2_SQRT_2); // rho_m = rho_m / (2*sqrt(2))

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i abs_psi_rpm = simde_mm256_subs_epi16(rho_p, y1r_over2); // psi_rpm = rho_p - y1r/2
    abs_psi_rpm = simde_mm256_abs_epi16(abs_psi_rpm); // abs_psi_rpm = |psi_rpm|

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i abs_psi_imm = simde_mm256_subs_epi16(rho_m, y1i_over2); // psi_imm = rho_m - y1i/2
    abs_psi_imm = simde_mm256_abs_epi16(abs_psi_imm); // abs_psi_imm = |psi_imm|

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    simde__m256i abs_psi_rmm = simde_mm256_subs_epi16(rho_m, y1r_over2); // psi_rmm = rho_m - y1r/2
    abs_psi_rmm = simde_mm256_abs_epi16(abs_psi_rmm); // abs_psi_rmm = |psi_rmm|

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i abs_psi_ipm = simde_mm256_subs_epi16(rho_p, y1i_over2); // psi_ipm = rho_p - y1i/2
    abs_psi_ipm = simde_mm256_abs_epi16(abs_psi_ipm); // abs_psi_ipm = |psi_ipm|

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    simde__m256i abs_psi_rpp = simde_mm256_adds_epi16(rho_p, y1r_over2); // psi_rpp = rho_p + y1r/2
    abs_psi_rpp = simde_mm256_abs_epi16(abs_psi_rpp); // abs_psi_rpp = |psi_rpp|

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    simde__m256i abs_psi_imp = simde_mm256_adds_epi16(rho_m, y1i_over2); // psi_imp = rho_m + y1i/2
    abs_psi_imp = simde_mm256_abs_epi16(abs_psi_imp); // abs_psi_imp = |psi_imp|

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    simde__m256i abs_psi_rmp = simde_mm256_adds_epi16(rho_m, y1r_over2); // psi_rmp = rho_m + y1r/2
    abs_psi_rmp = simde_mm256_abs_epi16(abs_psi_rmp); // abs_psi_rmp = |psi_rmp|

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    simde__m256i abs_psi_ipp = simde_mm256_adds_epi16(rho_p, y1i_over2); // psi_ipm = rho_p + y1i/2
    abs_psi_ipp = simde_mm256_abs_epi16(abs_psi_ipp); // abs_psi_ipp = |psi_ipm|

    /// Compute bit metrics (lambda)

    // lambda = max { |psi_r - y1r| * |x2R| + |psi_i - y1i| * |x2I| + y0r * xR + y0i * xI}

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_re_p = abs_psi_rpm + abs_psi_imm + y0r/sqrt(2) + y0i/sqrt(2)
    simde__m256i bit_met_num_re_p = simde_mm256_adds_epi16(abs_psi_rpm, abs_psi_imm);
    bit_met_num_re_p = simde_mm256_adds_epi16(bit_met_num_re_p, y0r_over2);
    bit_met_num_re_p = simde_mm256_adds_epi16(bit_met_num_re_p, y0i_over2);

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // For numerator: bit_met_num_re_m = abs_psi_rmm + abs_psi_ipp + y0r/sqrt(2) - y0i/sqrt(2)
    simde__m256i bit_met_num_re_m = simde_mm256_adds_epi16(abs_psi_rmm, abs_psi_ipp);
    bit_met_num_re_m = simde_mm256_adds_epi16(bit_met_num_re_m, y0r_over2);
    bit_met_num_re_m = simde_mm256_subs_epi16(bit_met_num_re_m, y0i_over2);

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    // For denominator: bit_met_den_re_p = abs_psi_rmp + abs_psi_ipm - y0r/sqrt(2) + y0i/sqrt(2)
    simde__m256i bit_met_den_re_p = simde_mm256_adds_epi16(abs_psi_rmp, abs_psi_ipm);
    bit_met_den_re_p = simde_mm256_subs_epi16(bit_met_den_re_p, y0r_over2);
    bit_met_den_re_p = simde_mm256_adds_epi16(bit_met_den_re_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_re_m = abs_psi_rpp + abs_psi_imp - y0r/sqrt(2) - y0i/sqrt(2)
    simde__m256i bit_met_den_re_m = simde_mm256_adds_epi16(abs_psi_rpp, abs_psi_imp);
    bit_met_den_re_m = simde_mm256_subs_epi16(bit_met_den_re_m, y0r_over2);
    bit_met_den_re_m = simde_mm256_subs_epi16(bit_met_den_re_m, y0i_over2);

    // xR = 1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_im_p = abs_psi_rpm + abs_psi_imm + y0r/sqrt(2) + y0i/sqrt(2)
    simde__m256i bit_met_num_im_p = simde_mm256_adds_epi16(abs_psi_rpm, abs_psi_imm);
    bit_met_num_im_p = simde_mm256_adds_epi16(bit_met_num_im_p, y0r_over2);
    bit_met_num_im_p = simde_mm256_adds_epi16(bit_met_num_im_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = 1/sqrt(2)
    // For numerator: bit_met_num_im_m = abs_psi_rmp + abs_psi_ipm - y0r/sqrt(2) + y0i/sqrt(2)
    simde__m256i bit_met_num_im_m = simde_mm256_adds_epi16(abs_psi_rmp, abs_psi_ipm);
    bit_met_num_im_m = simde_mm256_subs_epi16(bit_met_num_im_m, y0r_over2);
    bit_met_num_im_m = simde_mm256_adds_epi16(bit_met_num_im_m, y0i_over2);

    // xR = 1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_im_p = abs_psi_rmm + abs_psi_ipp + y0r/sqrt(2) - y0i/sqrt(2)
    simde__m256i bit_met_den_im_p = simde_mm256_adds_epi16(abs_psi_rmm, abs_psi_ipp);
    bit_met_den_im_p = simde_mm256_adds_epi16(bit_met_den_im_p, y0r_over2);
    bit_met_den_im_p = simde_mm256_subs_epi16(bit_met_den_im_p, y0i_over2);

    // xR = -1/sqrt(2) and xI = -1/sqrt(2)
    // For denominator: bit_met_den_im_m = abs_psi_rpp + abs_psi_imp - y0r/sqrt(2)- y0i/sqrt(2)
    simde__m256i bit_met_den_im_m = simde_mm256_adds_epi16(abs_psi_rpp, abs_psi_imp);
    bit_met_den_im_m = simde_mm256_subs_epi16(bit_met_den_im_m, y0r_over2);
    bit_met_den_im_m = simde_mm256_subs_epi16(bit_met_den_im_m, y0i_over2);

    /// Compute the LLRs

    // LLR = lambda(c==1) - lambda(c==0)

    simde__m256i logmax_num_re0 = simde_mm256_max_epi16(bit_met_num_re_p, bit_met_num_re_m); // LLR of the first bit: Bit = 1
    simde__m256i logmax_den_re0 = simde_mm256_max_epi16(bit_met_den_re_p, bit_met_den_re_m); // LLR of the first bit: Bit = 0
    simde__m256i logmax_num_im0 = simde_mm256_max_epi16(bit_met_num_im_p, bit_met_num_im_m); // LLR of the second bit: Bit = 1
    simde__m256i logmax_den_im0 = simde_mm256_max_epi16(bit_met_den_im_p, bit_met_den_im_m); // LLR of the second bit: Bit = 0

    y0r = simde_mm256_subs_epi16(logmax_num_re0,
                                 logmax_den_re0); // LLR of first bit [L1(1), L1(2), L1(3), L1(4), L1(5), L1(6), L1(7), L1(8)]
    y0i = simde_mm256_subs_epi16(logmax_num_im0,
                                 logmax_den_im0); // LLR of second bit [L2(1), L2(2), L2(3), L2(4), L2(5), L2(6), L2(7), L2(8)]

    // [L1(1), L2(1), L1(2), L2(2) ...]
    simde__m128i *stream0_128i_out = (simde__m128i *)&stream0_256i_out[i];
    simde__m128i *y0r_128 = (simde__m128i *)&y0r;
    simde__m128i *y0i_128 = (simde__m128i *)&y0i;
    simde_mm_storeu_si128(&stream0_128i_out[0], simde_mm_unpacklo_epi16(y0r_128[0], y0i_128[0]));
    simde_mm_storeu_si128(&stream0_128i_out[1], simde_mm_unpackhi_epi16(y0r_128[0], y0i_128[0]));

    // false if only 4 REs remain
    if (i < ((length >> 2) - 1)) {
      simde__m128i *stream0_128i_out = (simde__m128i *)&stream0_256i_out[i + 1];
      simde_mm_storeu_si128(&stream0_128i_out[0], simde_mm_unpacklo_epi16(y0r_128[1], y0i_128[1]));
      simde_mm_storeu_si128(&stream0_128i_out[1], simde_mm_unpackhi_epi16(y0r_128[1], y0i_128[1]));
    }
  }
#endif
}

#ifdef USE_128BIT
// calculate interference magnitude
// tmp_result = ones in shorts corr. to interval 2<=x<=4, tmp_result2 interval < 2, tmp_result3 interval 4<x<6 and tmp_result4
// interval x>6
static inline simde__m128i interference_abs_64qam_epi16(simde__m128i psi,
                                                        simde__m128i int_ch_mag,
                                                        simde__m128i int_two_ch_mag,
                                                        simde__m128i int_three_ch_mag,
                                                        simde__m128i c1,
                                                        simde__m128i c3,
                                                        simde__m128i c5,
                                                        simde__m128i c7)
{
  simde__m128i tmp_result = simde_mm_cmpgt_epi16(int_two_ch_mag, psi);
  simde__m128i tmp_result3 = simde_mm_xor_si128(tmp_result, allones128());
  simde__m128i tmp_result2 = simde_mm_cmpgt_epi16(int_ch_mag, psi);
  tmp_result = simde_mm_xor_si128(tmp_result, tmp_result2);
  simde__m128i tmp_result4 = simde_mm_cmpgt_epi16(psi, int_three_ch_mag);
  tmp_result3 = simde_mm_xor_si128(tmp_result3, tmp_result4);
  tmp_result = simde_mm_and_si128(tmp_result, c3);
  tmp_result2 = simde_mm_and_si128(tmp_result2, c1);
  tmp_result3 = simde_mm_and_si128(tmp_result3, c5);
  tmp_result4 = simde_mm_and_si128(tmp_result4, c7);
  tmp_result = simde_mm_or_si128(tmp_result, tmp_result2);
  tmp_result3 = simde_mm_or_si128(tmp_result3, tmp_result4);
  return simde_mm_or_si128(tmp_result, tmp_result3);
}

// Calculates psi_a = psi_r * a_r + psi_i * a_i
static inline simde__m128i prodsum_psi_a_epi16(simde__m128i psi_r, simde__m128i a_r, simde__m128i psi_i, simde__m128i a_i)
{
  simde__m128i tmp_result = simde_mm_mulhi_epi16(psi_r, a_r);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  simde__m128i tmp_result2 = simde_mm_mulhi_epi16(psi_i, a_i);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  return simde_mm_adds_epi16(tmp_result, tmp_result2);
}

// Calculate interference magnitude
static inline simde__m128i interference_abs_epi16(simde__m128i psi, simde__m128i int_ch_mag, simde__m128i c1, simde__m128i c2)
{
  simde__m128i tmp_result = simde_mm_cmplt_epi16(psi, int_ch_mag);
  simde__m128i tmp_result2 = simde_mm_xor_si128(tmp_result, allones128());
  tmp_result = simde_mm_and_si128(tmp_result, c1);
  tmp_result2 = simde_mm_and_si128(tmp_result2, c2);
  return simde_mm_or_si128(tmp_result, tmp_result2);
}

// Calculates a_sq = int_ch_mag * (a_r^2 + a_i^2) * scale_factor
static inline simde__m128i square_a_epi16(simde__m128i a_r, simde__m128i a_i, simde__m128i int_ch_mag, simde__m128i scale_factor)
{
  simde__m128i tmp_result = simde_mm_mulhi_epi16(a_r, a_r);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm_mulhi_epi16(tmp_result, scale_factor);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm_mulhi_epi16(tmp_result, int_ch_mag);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  simde__m128i tmp_result2 = simde_mm_mulhi_epi16(a_i, a_i);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm_mulhi_epi16(tmp_result2, scale_factor);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm_mulhi_epi16(tmp_result2, int_ch_mag);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  return simde_mm_adds_epi16(tmp_result, tmp_result2);
}

// calculates a_sq = int_ch_mag*(a_r^2 + a_i^2)*scale_factor for 64-QAM
static inline simde__m128i square_a_64qam_epi16(simde__m128i a_r,
                                                simde__m128i a_i,
                                                simde__m128i int_ch_mag,
                                                simde__m128i scale_factor)
{
  simde__m128i tmp_result = simde_mm_mulhi_epi16(a_r, a_r);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm_mulhi_epi16(tmp_result, scale_factor);
  tmp_result = simde_mm_slli_epi16(tmp_result, 3);
  tmp_result = simde_mm_mulhi_epi16(tmp_result, int_ch_mag);
  tmp_result = simde_mm_slli_epi16(tmp_result, 1);
  simde__m128i tmp_result2 = simde_mm_mulhi_epi16(a_i, a_i);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm_mulhi_epi16(tmp_result2, scale_factor);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 3);
  tmp_result2 = simde_mm_mulhi_epi16(tmp_result2, int_ch_mag);
  tmp_result2 = simde_mm_slli_epi16(tmp_result2, 1);
  return simde_mm_adds_epi16(tmp_result, tmp_result2);
}

static inline simde__m128i max_epi16(simde__m128i m0,
                                     simde__m128i m1,
                                     simde__m128i m2,
                                     simde__m128i m3,
                                     simde__m128i m4,
                                     simde__m128i m5,
                                     simde__m128i m6,
                                     simde__m128i m7)
{
  simde__m128i a0 = simde_mm_max_epi16(m0, m1);
  simde__m128i a1 = simde_mm_max_epi16(m2, m3);
  simde__m128i a2 = simde_mm_max_epi16(m4, m5);
  simde__m128i a3 = simde_mm_max_epi16(m6, m7);
  simde__m128i b0 = simde_mm_max_epi16(a0, a1);
  simde__m128i b1 = simde_mm_max_epi16(a2, a3);
  return simde_mm_max_epi16(b0, b1);
}

#else

// calculate interference magnitude
// tmp_result = ones in shorts corr. to interval 2<=x<=4, tmp_result2 interval < 2, tmp_result3 interval 4<x<6 and tmp_result4
// interval x>6
static inline simde__m256i interference_abs_64qam_epi16_256(simde__m256i psi,
                                                            simde__m256i int_ch_mag,
                                                            simde__m256i int_two_ch_mag,
                                                            simde__m256i int_three_ch_mag,
                                                            simde__m256i c1,
                                                            simde__m256i c3,
                                                            simde__m256i c5,
                                                            simde__m256i c7)
{
  simde__m256i tmp_result = simde_mm256_cmpgt_epi16(int_two_ch_mag, psi);
  simde__m256i tmp_result3 = simde_mm256_xor_si256(tmp_result, allones256());
  simde__m256i tmp_result2 = simde_mm256_cmpgt_epi16(int_ch_mag, psi);
  tmp_result = simde_mm256_xor_si256(tmp_result, tmp_result2);
  simde__m256i tmp_result4 = simde_mm256_cmpgt_epi16(psi, int_three_ch_mag);
  tmp_result3 = simde_mm256_xor_si256(tmp_result3, tmp_result4);
  tmp_result = simde_mm256_and_si256(tmp_result, c3);
  tmp_result2 = simde_mm256_and_si256(tmp_result2, c1);
  tmp_result3 = simde_mm256_and_si256(tmp_result3, c5);
  tmp_result4 = simde_mm256_and_si256(tmp_result4, c7);
  tmp_result = simde_mm256_or_si256(tmp_result, tmp_result2);
  tmp_result3 = simde_mm256_or_si256(tmp_result3, tmp_result4);
  return simde_mm256_or_si256(tmp_result, tmp_result3);
}

// calculates psi_a = psi_r*a_r + psi_i*a_i
static inline simde__m256i prodsum_psi_a_epi16_256(simde__m256i psi_r, simde__m256i a_r, simde__m256i psi_i, simde__m256i a_i)
{
  simde__m256i tmp_result = simde_mm256_mulhi_epi16(psi_r, a_r);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  simde__m256i tmp_result2 = simde_mm256_mulhi_epi16(psi_i, a_i);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  return simde_mm256_adds_epi16(tmp_result, tmp_result2);
}

// Calculate interference magnitude
static inline simde__m256i interference_abs_epi16_256(simde__m256i psi, simde__m256i int_ch_mag, simde__m256i c1, simde__m256i c2)
{
  simde__m256i tmp_result = simde_mm256_cmpgt_epi16(int_ch_mag, psi);
  simde__m256i tmp_result2 = simde_mm256_xor_si256(tmp_result, allones256());
  tmp_result = simde_mm256_and_si256(tmp_result, c1);
  tmp_result2 = simde_mm256_and_si256(tmp_result2, c2);
  return simde_mm256_or_si256(tmp_result, tmp_result2);
}

// Calculates a_sq = int_ch_mag * (a_r^2 + a_i^2) * scale_factor
static inline simde__m256i square_a_epi16_256(simde__m256i a_r,
                                              simde__m256i a_i,
                                              simde__m256i int_ch_mag,
                                              simde__m256i scale_factor)
{
  simde__m256i tmp_result = simde_mm256_mulhi_epi16(a_r, a_r);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm256_mulhi_epi16(tmp_result, scale_factor);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm256_mulhi_epi16(tmp_result, int_ch_mag);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  simde__m256i tmp_result2 = simde_mm256_mulhi_epi16(a_i, a_i);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm256_mulhi_epi16(tmp_result2, scale_factor);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm256_mulhi_epi16(tmp_result2, int_ch_mag);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  return simde_mm256_adds_epi16(tmp_result, tmp_result2);
}

// calculates a_sq = int_ch_mag*(a_r^2 + a_i^2)*scale_factor for 64-QAM
static inline simde__m256i square_a_64qam_epi16_256(simde__m256i a_r,
                                                    simde__m256i a_i,
                                                    simde__m256i int_ch_mag,
                                                    simde__m256i scale_factor)
{
  simde__m256i tmp_result = simde_mm256_mulhi_epi16(a_r, a_r);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  tmp_result = simde_mm256_mulhi_epi16(tmp_result, scale_factor);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 3);
  tmp_result = simde_mm256_mulhi_epi16(tmp_result, int_ch_mag);
  tmp_result = simde_mm256_slli_epi16(tmp_result, 1);
  simde__m256i tmp_result2 = simde_mm256_mulhi_epi16(a_i, a_i);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  tmp_result2 = simde_mm256_mulhi_epi16(tmp_result2, scale_factor);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 3);
  tmp_result2 = simde_mm256_mulhi_epi16(tmp_result2, int_ch_mag);
  tmp_result2 = simde_mm256_slli_epi16(tmp_result2, 1);
  return simde_mm256_adds_epi16(tmp_result, tmp_result2);
}

static inline simde__m256i max_epi16_256(simde__m256i m0,
                                         simde__m256i m1,
                                         simde__m256i m2,
                                         simde__m256i m3,
                                         simde__m256i m4,
                                         simde__m256i m5,
                                         simde__m256i m6,
                                         simde__m256i m7)
{
  simde__m256i a0 = simde_mm256_max_epi16(m0, m1);
  simde__m256i a1 = simde_mm256_max_epi16(m2, m3);
  simde__m256i a2 = simde_mm256_max_epi16(m4, m5);
  simde__m256i a3 = simde_mm256_max_epi16(m6, m7);
  simde__m256i b0 = simde_mm256_max_epi16(a0, a1);
  simde__m256i b1 = simde_mm256_max_epi16(a2, a3);
  return simde_mm256_max_epi16(b0, b1);
}

#endif

/*
 * This function computes the LLRs of stream 0 (s_0) in presence of the interfering stream 1 (s_1) assuming that both symbols are
 * 16QAM. It can be used for both MU-MIMO interference-aware receiver or for SU-MIMO receivers.
 *
 * Input:
 *   stream0_in:  MF filter output for 1st stream, i.e., y0' = h0'*y0
 *   stream1_in:  MF filter output for 2nd stream, i.e., y1' = h1'*y0
 *   ch_mag:      2*h0/sqrt(10), [Re0 Im0 Re1 Im1] s.t. Im0=Re0, Im1=Re1, etc
 *   ch_mag_i:    2*h1/sqrt(10), [Re0 Im0 Re1 Im1] s.t. Im0=Re0, Im1=Re1, etc
 *   rho01:       Channel cross correlation, i.e., rho01 = h0'*h1
 *   length:      Number of resource elements
 *
 * Output:
 *   stream0_out: Output LLRs for 1st stream
 */
void nr_qam16_llr_2layer(c16_t *stream0_in,
                         c16_t *stream1_in,
                         c16_t *ch_mag,
                         c16_t *ch_mag_i,
                         int16_t *stream0_out,
                         c16_t *rho01,
                         uint32_t length)
{
#ifdef USE_128BIT
  simde__m128i *rho01_128i = (simde__m128i *)rho01;
  simde__m128i *stream0_128i_in = (simde__m128i *)stream0_in;
  simde__m128i *stream1_128i_in = (simde__m128i *)stream1_in;
  simde__m128i *stream0_128i_out = (simde__m128i *)stream0_out;
  simde__m128i *ch_mag_128i = (simde__m128i *)ch_mag;
  simde__m128i *ch_mag_128i_i = (simde__m128i *)ch_mag_i;

  simde__m128i ONE_OVER_SQRT_10 = simde_mm_set1_epi16(20724); // round(1/sqrt(10)*2^16)
  simde__m128i ONE_OVER_SQRT_10_Q15 = simde_mm_set1_epi16(10362); // round(1/sqrt(10)*2^15)
  simde__m128i THREE_OVER_SQRT_10 = simde_mm_set1_epi16(31086); // round(3/sqrt(10)*2^15)
  simde__m128i SQRT_10_OVER_FOUR = simde_mm_set1_epi16(25905); // round(sqrt(10)/4*2^15)
  simde__m128i ONE_OVER_TWO_SQRT_10 = simde_mm_set1_epi16(10362); // round(1/2/sqrt(10)*2^16)
  simde__m128i NINE_OVER_TWO_SQRT_10 = simde_mm_set1_epi16(23315); // round(9/2/sqrt(10)*2^14)
  simde__m128i ch_mag_des, ch_mag_int;
  simde__m128i y0r_over_sqrt10;
  simde__m128i y0i_over_sqrt10;
  simde__m128i y0r_three_over_sqrt10;
  simde__m128i y0i_three_over_sqrt10;
  simde__m128i ch_mag_over_10;
  simde__m128i ch_mag_over_2;
  simde__m128i ch_mag_9_over_10;

  simde__m128i xmm0;
  simde__m128i xmm1;
  simde__m128i xmm2;
  simde__m128i xmm3;
  simde__m128i xmm4;
  simde__m128i xmm5;
  simde__m128i xmm6;
  simde__m128i xmm7;

  simde__m128i rho_rpi;
  simde__m128i rho_rmi;
  simde__m128i rho_rs[8];
  simde__m128i psi_rs[16];
  simde__m128i psi_is[16];
  simde__m128i a_rs[16];
  simde__m128i a_is[16];
  simde__m128i psi_as[16];
  simde__m128i a_sqs[16];
  simde__m128i y0_s[8];

  simde__m128i y0r;
  simde__m128i y0i;
  simde__m128i y1r;
  simde__m128i y1i;

  // In one iteration, we deal with 8 REs
  for (int i = 0; i < (length + 3) >> 2; i += 2) {
    // Get rho
    oai_mm_separate_real_imag_parts(&xmm2, &xmm3, rho01_128i[i], rho01_128i[i + 1]);
    rho_rpi = simde_mm_adds_epi16(xmm2, xmm3); // rho = Re(rho) + Im(rho)
    rho_rmi = simde_mm_subs_epi16(xmm2, xmm3); // rho* = Re(rho) - Im(rho)

    // Compute the different rhos
    rho_rs[0] = simde_mm_mulhi_epi16(rho_rpi, ONE_OVER_SQRT_10);
    rho_rs[4] = simde_mm_mulhi_epi16(rho_rmi, ONE_OVER_SQRT_10);
    rho_rs[3] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(rho_rpi, THREE_OVER_SQRT_10), 1);
    rho_rs[7] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(rho_rmi, THREE_OVER_SQRT_10), 1);

    xmm4 = simde_mm_mulhi_epi16(xmm2, ONE_OVER_SQRT_10); // Re(rho)
    xmm5 = simde_mm_mulhi_epi16(xmm3, THREE_OVER_SQRT_10); // Im(rho)
    xmm5 = simde_mm_slli_epi16(xmm5, 1);

    rho_rs[1] = simde_mm_adds_epi16(xmm4, xmm5);
    rho_rs[5] = simde_mm_subs_epi16(xmm4, xmm5);

    xmm6 = simde_mm_mulhi_epi16(xmm2, THREE_OVER_SQRT_10); // Re(rho)
    xmm7 = simde_mm_mulhi_epi16(xmm3, ONE_OVER_SQRT_10); // Im(rho)
    xmm6 = simde_mm_slli_epi16(xmm6, 1);

    rho_rs[2] = simde_mm_adds_epi16(xmm6, xmm7);
    rho_rs[6] = simde_mm_subs_epi16(xmm6, xmm7);

    // Rearrange interfering MF output
    oai_mm_separate_real_imag_parts(&y1r, &y1i, stream1_128i_in[i], stream1_128i_in[i + 1]);

    // |  [Re(rho)+ Im(rho)]/sqrt(10) - y1r  |
    for (int j = 0; j < 8; j++) { // psi_rs[0~7], rho_rs[0~7]
      psi_rs[j] = simde_mm_abs_epi16(simde_mm_subs_epi16(rho_rs[j], y1r));
    }
    for (int j = 8; j < 16; j++) { // psi_rs[8~16], rho_rs[4,5,6,7,0,1,2,3]
      psi_rs[j] = simde_mm_abs_epi16(simde_mm_adds_epi16(rho_rs[(j - 4) & 7], y1r));
    }
    const uint8_t rho_rs_indexes[16] = {4, 6, 5, 7, 0, 2, 1, 3, 0, 2, 1, 3, 4, 6, 5, 7};
    for (int k = 0; k < 16;
         k += 8) { // psi_is[0~15], sub(rho_rs[4,6,5,7]), add(rho_rs[0,2,1,3]), sub(rho_rs[0,2,1,3]), add(rho_rs[4,6,5,7])
      for (int j = k; j < k + 4; j++) {
        psi_is[j] = simde_mm_abs_epi16(simde_mm_subs_epi16(rho_rs[rho_rs_indexes[j]], y1i));
        psi_is[j + 4] = simde_mm_abs_epi16(simde_mm_adds_epi16(rho_rs[rho_rs_indexes[j + 4]], y1i));
      }
    }

    // Rearrange desired MF output
    oai_mm_separate_real_imag_parts(&y0r, &y0i, stream0_128i_in[i], stream0_128i_in[i + 1]);

    // Rearrange desired channel magnitudes
    // [|h|^2(1),|h|^2(2),|h|^2(3),|h|^2(4)]*(2/sqrt(10))
    oai_mm_separate_real_imag_parts(&ch_mag_des, &xmm2, ch_mag_128i[i], ch_mag_128i[i + 1]);

    // Rearrange interfering channel magnitudes
    oai_mm_separate_real_imag_parts(&ch_mag_int, &xmm2, ch_mag_128i_i[i], ch_mag_128i_i[i + 1]);

    // Scale MF output of desired signal
    y0r_over_sqrt10 = simde_mm_mulhi_epi16(y0r, ONE_OVER_SQRT_10);
    y0i_over_sqrt10 = simde_mm_mulhi_epi16(y0i, ONE_OVER_SQRT_10);
    y0r_three_over_sqrt10 = simde_mm_slli_epi16(simde_mm_mulhi_epi16(y0r, THREE_OVER_SQRT_10), 1);
    y0i_three_over_sqrt10 = simde_mm_slli_epi16(simde_mm_mulhi_epi16(y0i, THREE_OVER_SQRT_10), 1);

    // Compute necessary combination of required terms
    y0_s[0] = simde_mm_adds_epi16(y0r_over_sqrt10, y0i_over_sqrt10);
    y0_s[4] = simde_mm_subs_epi16(y0r_over_sqrt10, y0i_over_sqrt10);

    y0_s[1] = simde_mm_adds_epi16(y0r_over_sqrt10, y0i_three_over_sqrt10);
    y0_s[5] = simde_mm_subs_epi16(y0r_over_sqrt10, y0i_three_over_sqrt10);

    y0_s[2] = simde_mm_adds_epi16(y0r_three_over_sqrt10, y0i_over_sqrt10);
    y0_s[6] = simde_mm_subs_epi16(y0r_three_over_sqrt10, y0i_over_sqrt10);

    y0_s[3] = simde_mm_adds_epi16(y0r_three_over_sqrt10, y0i_three_over_sqrt10);
    y0_s[7] = simde_mm_subs_epi16(y0r_three_over_sqrt10, y0i_three_over_sqrt10);

    for (int j = 0; j < 16; j++) {
      // Compute optimal interfering symbol magnitude
      a_rs[j] = interference_abs_epi16(psi_rs[j], ch_mag_int, ONE_OVER_SQRT_10_Q15, THREE_OVER_SQRT_10);
      a_is[j] = interference_abs_epi16(psi_is[j], ch_mag_int, ONE_OVER_SQRT_10_Q15, THREE_OVER_SQRT_10);

      // Calculation of groups of two terms in the bit metric involving product of psi and interference magnitude
      psi_as[j] = prodsum_psi_a_epi16(psi_rs[j], a_rs[j], psi_is[j], a_is[j]);

      // squared interference magnitude times int. ch. power
      a_sqs[j] = square_a_epi16(a_rs[j], a_is[j], ch_mag_int, SQRT_10_OVER_FOUR);
    }

    // Computing different multiples of channel norms
    ch_mag_over_10 = simde_mm_mulhi_epi16(ch_mag_des, ONE_OVER_TWO_SQRT_10);
    ch_mag_over_2 = simde_mm_mulhi_epi16(ch_mag_des, SQRT_10_OVER_FOUR);
    ch_mag_over_2 = simde_mm_slli_epi16(ch_mag_over_2, 1);
    ch_mag_9_over_10 = simde_mm_mulhi_epi16(ch_mag_des, NINE_OVER_TWO_SQRT_10);
    ch_mag_9_over_10 = simde_mm_slli_epi16(ch_mag_9_over_10, 2);

    /// Compute bit metrics (lambda)
    simde__m128i bit_mets[16];
    for (int j = 0; j < 8; j += 4) {
      bit_mets[j + 0] = simde_mm_subs_epi16(psi_as[j + 0], a_sqs[j + 0]);
      bit_mets[j + 0] = simde_mm_adds_epi16(bit_mets[j + 0], y0_s[j + 0]);
      bit_mets[j + 0] = simde_mm_subs_epi16(bit_mets[j + 0], ch_mag_over_10);

      bit_mets[j + 1] = simde_mm_subs_epi16(psi_as[j + 1], a_sqs[j + 1]);
      bit_mets[j + 1] = simde_mm_adds_epi16(bit_mets[j + 1], y0_s[j + 1]);
      bit_mets[j + 1] = simde_mm_subs_epi16(bit_mets[j + 1], ch_mag_over_2);
      bit_mets[j + 2] = simde_mm_subs_epi16(psi_as[j + 2], a_sqs[j + 2]);
      bit_mets[j + 2] = simde_mm_adds_epi16(bit_mets[j + 2], y0_s[j + 2]);
      bit_mets[j + 2] = simde_mm_subs_epi16(bit_mets[j + 2], ch_mag_over_2);

      bit_mets[j + 3] = simde_mm_subs_epi16(psi_as[j + 3], a_sqs[j + 3]);
      bit_mets[j + 3] = simde_mm_adds_epi16(bit_mets[j + 3], y0_s[j + 3]);
      bit_mets[j + 3] = simde_mm_subs_epi16(bit_mets[j + 3], ch_mag_9_over_10);
    }
    for (int j = 8; j < 16; j += 4) {
      bit_mets[j + 0] = simde_mm_subs_epi16(psi_as[j + 0], a_sqs[j + 0]);
      bit_mets[j + 0] = simde_mm_subs_epi16(bit_mets[j + 0], y0_s[(j - 4) & 0x07]);
      bit_mets[j + 0] = simde_mm_subs_epi16(bit_mets[j + 0], ch_mag_over_10);

      bit_mets[j + 1] = simde_mm_subs_epi16(psi_as[j + 1], a_sqs[j + 1]);
      bit_mets[j + 1] = simde_mm_subs_epi16(bit_mets[j + 1], y0_s[(j - 3) & 0x07]);
      bit_mets[j + 1] = simde_mm_subs_epi16(bit_mets[j + 1], ch_mag_over_2);
      bit_mets[j + 2] = simde_mm_subs_epi16(psi_as[j + 2], a_sqs[j + 2]);
      bit_mets[j + 2] = simde_mm_subs_epi16(bit_mets[j + 2], y0_s[(j - 2) & 0x07]);
      bit_mets[j + 2] = simde_mm_subs_epi16(bit_mets[j + 2], ch_mag_over_2);

      bit_mets[j + 3] = simde_mm_subs_epi16(psi_as[j + 3], a_sqs[j + 3]);
      bit_mets[j + 3] = simde_mm_subs_epi16(bit_mets[j + 3], y0_s[(j - 1) & 0x07]);
      bit_mets[j + 3] = simde_mm_subs_epi16(bit_mets[j + 3], ch_mag_9_over_10);
    }
    /// Compute the LLRs

    // LLR = lambda(c==1) - lambda(c==0)

    // LLR of the first bit: Bit = 1
    simde__m128i logmax_num_re0 =
        max_epi16(bit_mets[8], bit_mets[9], bit_mets[10], bit_mets[11], bit_mets[12], bit_mets[13], bit_mets[14], bit_mets[15]);
    // LLR of the first bit: Bit = 0
    simde__m128i logmax_den_re0 =
        max_epi16(bit_mets[0], bit_mets[1], bit_mets[2], bit_mets[3], bit_mets[4], bit_mets[5], bit_mets[6], bit_mets[7]);
    // LLR of the second bit: Bit = 1
    simde__m128i logmax_num_re1 =
        max_epi16(bit_mets[4], bit_mets[5], bit_mets[6], bit_mets[7], bit_mets[12], bit_mets[13], bit_mets[14], bit_mets[15]);
    // LLR of the second bit: Bit = 0
    simde__m128i logmax_den_re1 =
        max_epi16(bit_mets[0], bit_mets[1], bit_mets[3], bit_mets[2], bit_mets[8], bit_mets[9], bit_mets[10], bit_mets[11]);
    // LLR of the third bit: Bit = 1
    simde__m128i logmax_num_im0 =
        max_epi16(bit_mets[2], bit_mets[3], bit_mets[6], bit_mets[7], bit_mets[10], bit_mets[11], bit_mets[14], bit_mets[15]);
    // LLR of the third bit: Bit = 0
    simde__m128i logmax_den_im0 =
        max_epi16(bit_mets[0], bit_mets[1], bit_mets[4], bit_mets[5], bit_mets[8], bit_mets[9], bit_mets[12], bit_mets[13]);
    // LLR of the fourth bit: Bit = 1
    simde__m128i logmax_num_im1 =
        max_epi16(bit_mets[1], bit_mets[3], bit_mets[5], bit_mets[7], bit_mets[9], bit_mets[11], bit_mets[13], bit_mets[15]);
    // LLR of the fourth bit: Bit = 0
    simde__m128i logmax_den_im1 =
        max_epi16(bit_mets[0], bit_mets[2], bit_mets[4], bit_mets[6], bit_mets[8], bit_mets[10], bit_mets[12], bit_mets[14]);

    y0r = simde_mm_subs_epi16(logmax_den_re0,
                              logmax_num_re0); // LLR of first bit  [L1(1), L1(2), L1(3), L1(4), L1(5), L1(6), L1(7), L1(8)]
    y1r = simde_mm_subs_epi16(logmax_den_re1,
                              logmax_num_re1); // LLR of second bit [L2(1), L2(2), L2(3), L2(4), L2(5), L2(6), L2(7), L2(8)]
    y0i = simde_mm_subs_epi16(logmax_den_im0,
                              logmax_num_im0); // LLR of third bit  [L3(1), L3(2), L3(3), L3(4), L3(5), L3(6), L3(7), L3(8)]
    y1i = simde_mm_subs_epi16(logmax_den_im1,
                              logmax_num_im1); // LLR of fourth bit [L4(1), L4(2), L4(3), L4(4), L4(5), L4(6), L4(7), L4(8)]

    // Pack LLRs in output
    xmm0 = simde_mm_unpacklo_epi16(y0r, y1r); // [L1(1), L2(1), L1(2), L2(2), L1(3), L2(3), L1(4), L2(4)]
    xmm1 = simde_mm_unpackhi_epi16(y0r, y1r); // [L1(5), L2(5), L1(6), L2(6), L1(7), L2(7), L1(8), L2(8)]
    xmm2 = simde_mm_unpacklo_epi16(y0i, y1i); // [L3(1), L4(1), L3(2), L4(2), L3(3), L4(3), L3(4), L4(4)]
    xmm3 = simde_mm_unpackhi_epi16(y0i, y1i); // [L3(5), L4(5), L3(6), L4(6), L3(7), L4(7), L3(8), L4(8)]
    stream0_128i_out[2 * i + 0] = simde_mm_unpacklo_epi32(xmm0, xmm2); // 8 LLRs, 2 REs
    stream0_128i_out[2 * i + 1] = simde_mm_unpackhi_epi32(xmm0, xmm2); // 8 LLRs, 2 REs
    stream0_128i_out[2 * i + 2] = simde_mm_unpacklo_epi32(xmm1, xmm3); // 8 LLRs, 2 REs
    stream0_128i_out[2 * i + 3] = simde_mm_unpackhi_epi32(xmm1, xmm3); // 8 LLRs, 2 REs
  }
#else
  simde__m256i *rho01_256i = (simde__m256i *)rho01;
  simde__m256i *stream0_256i_in = (simde__m256i *)stream0_in;
  simde__m256i *stream1_256i_in = (simde__m256i *)stream1_in;
  simde__m256i *stream0_256i_out = (simde__m256i *)stream0_out;
  simde__m256i *ch_mag_256i = (simde__m256i *)ch_mag;
  simde__m256i *ch_mag_256i_i = (simde__m256i *)ch_mag_i;

  simde__m256i ONE_OVER_SQRT_10 = simde_mm256_set1_epi16(20724); // round(1/sqrt(10)*2^16)
  simde__m256i ONE_OVER_SQRT_10_Q15 = simde_mm256_set1_epi16(10362); // round(1/sqrt(10)*2^15)
  simde__m256i THREE_OVER_SQRT_10 = simde_mm256_set1_epi16(31086); // round(3/sqrt(10)*2^15)
  simde__m256i SQRT_10_OVER_FOUR = simde_mm256_set1_epi16(25905); // round(sqrt(10)/4*2^15)
  simde__m256i ONE_OVER_TWO_SQRT_10 = simde_mm256_set1_epi16(10362); // round(1/2/sqrt(10)*2^16)
  simde__m256i NINE_OVER_TWO_SQRT_10 = simde_mm256_set1_epi16(23315); // round(9/2/sqrt(10)*2^14)
  simde__m256i ch_mag_des, ch_mag_int;
  simde__m256i y0r_over_sqrt10;
  simde__m256i y0i_over_sqrt10;
  simde__m256i y0r_three_over_sqrt10;
  simde__m256i y0i_three_over_sqrt10;
  simde__m256i ch_mag_over_10;
  simde__m256i ch_mag_over_2;
  simde__m256i ch_mag_9_over_10;

  simde__m256i xmm2;
  simde__m256i xmm3;
  simde__m256i xmm4;
  simde__m256i xmm5;
  simde__m256i xmm6;
  simde__m256i xmm7;

  simde__m256i rho_rpi;
  simde__m256i rho_rmi;
  simde__m256i rho_rs[8];
  simde__m256i psi_rs[16];
  simde__m256i psi_is[16];
  simde__m256i a_rs[16];
  simde__m256i a_is[16];
  simde__m256i psi_as[16];
  simde__m256i a_sqs[16];
  simde__m256i y0_s[8];

  simde__m256i y0r;
  simde__m256i y0i;
  simde__m256i y1r;
  simde__m256i y1i;

  // In one iteration, we deal with 8 REs
  for (int i = 0; i < (length + 7) >> 3; i += 2) {
    // Get rho
    oai_mm256_separate_real_imag_parts(&xmm2, &xmm3, rho01_256i[i], rho01_256i[i + 1]);
    rho_rpi = simde_mm256_adds_epi16(xmm2, xmm3); // rho = Re(rho) + Im(rho)
    rho_rmi = simde_mm256_subs_epi16(xmm2, xmm3); // rho* = Re(rho) - Im(rho)

    // Compute the different rhos
    rho_rs[0] = simde_mm256_mulhi_epi16(rho_rpi, ONE_OVER_SQRT_10);
    rho_rs[4] = simde_mm256_mulhi_epi16(rho_rmi, ONE_OVER_SQRT_10);
    rho_rs[3] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(rho_rpi, THREE_OVER_SQRT_10), 1);
    rho_rs[7] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(rho_rmi, THREE_OVER_SQRT_10), 1);

    xmm4 = simde_mm256_mulhi_epi16(xmm2, ONE_OVER_SQRT_10); // Re(rho)
    xmm5 = simde_mm256_mulhi_epi16(xmm3, THREE_OVER_SQRT_10); // Im(rho)
    xmm5 = simde_mm256_slli_epi16(xmm5, 1);

    rho_rs[1] = simde_mm256_adds_epi16(xmm4, xmm5);
    rho_rs[5] = simde_mm256_subs_epi16(xmm4, xmm5);

    xmm6 = simde_mm256_mulhi_epi16(xmm2, THREE_OVER_SQRT_10); // Re(rho)
    xmm7 = simde_mm256_mulhi_epi16(xmm3, ONE_OVER_SQRT_10); // Im(rho)
    xmm6 = simde_mm256_slli_epi16(xmm6, 1);

    rho_rs[2] = simde_mm256_adds_epi16(xmm6, xmm7);
    rho_rs[6] = simde_mm256_subs_epi16(xmm6, xmm7);

    // Rearrange interfering MF output
    oai_mm256_separate_real_imag_parts(&y1r, &y1i, stream1_256i_in[i], stream1_256i_in[i + 1]);

    // |  [Re(rho)+ Im(rho)]/sqrt(10) - y1r  |
    for (int j = 0; j < 8; j++) { // psi_rs[0~7], rho_rs[0~7]
      psi_rs[j] = simde_mm256_abs_epi16(simde_mm256_subs_epi16(rho_rs[j], y1r));
    }
    for (int j = 8; j < 16; j++) { // psi_rs[8~16], rho_rs[4,5,6,7,0,1,2,3]
      psi_rs[j] = simde_mm256_abs_epi16(simde_mm256_adds_epi16(rho_rs[(j - 4) & 7], y1r));
    }
    const uint8_t rho_rs_indexes[16] = {4, 6, 5, 7, 0, 2, 1, 3, 0, 2, 1, 3, 4, 6, 5, 7};
    for (int k = 0; k < 16;
         k += 8) { // psi_is[0~15], sub(rho_rs[4,6,5,7]), add(rho_rs[0,2,1,3]), sub(rho_rs[0,2,1,3]), add(rho_rs[4,6,5,7])
      for (int j = k; j < k + 4; j++) {
        psi_is[j] = simde_mm256_abs_epi16(simde_mm256_subs_epi16(rho_rs[rho_rs_indexes[j]], y1i));
        psi_is[j + 4] = simde_mm256_abs_epi16(simde_mm256_adds_epi16(rho_rs[rho_rs_indexes[j + 4]], y1i));
      }
    }

    // Rearrange desired MF output
    oai_mm256_separate_real_imag_parts(&y0r, &y0i, stream0_256i_in[i], stream0_256i_in[i + 1]);

    // Rearrange desired channel magnitudes
    // [|h|^2(1),|h|^2(2),|h|^2(3),|h|^2(4)]*(2/sqrt(10))
    oai_mm256_separate_real_imag_parts(&ch_mag_des, &xmm2, ch_mag_256i[i], ch_mag_256i[i + 1]);

    // Rearrange interfering channel magnitudes
    oai_mm256_separate_real_imag_parts(&ch_mag_int, &xmm2, ch_mag_256i_i[i], ch_mag_256i_i[i + 1]);

    // Scale MF output of desired signal
    y0r_over_sqrt10 = simde_mm256_mulhi_epi16(y0r, ONE_OVER_SQRT_10);
    y0i_over_sqrt10 = simde_mm256_mulhi_epi16(y0i, ONE_OVER_SQRT_10);
    y0r_three_over_sqrt10 = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(y0r, THREE_OVER_SQRT_10), 1);
    y0i_three_over_sqrt10 = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(y0i, THREE_OVER_SQRT_10), 1);

    // Compute necessary combination of required terms
    y0_s[0] = simde_mm256_adds_epi16(y0r_over_sqrt10, y0i_over_sqrt10);
    y0_s[4] = simde_mm256_subs_epi16(y0r_over_sqrt10, y0i_over_sqrt10);

    y0_s[1] = simde_mm256_adds_epi16(y0r_over_sqrt10, y0i_three_over_sqrt10);
    y0_s[5] = simde_mm256_subs_epi16(y0r_over_sqrt10, y0i_three_over_sqrt10);

    y0_s[2] = simde_mm256_adds_epi16(y0r_three_over_sqrt10, y0i_over_sqrt10);
    y0_s[6] = simde_mm256_subs_epi16(y0r_three_over_sqrt10, y0i_over_sqrt10);

    y0_s[3] = simde_mm256_adds_epi16(y0r_three_over_sqrt10, y0i_three_over_sqrt10);
    y0_s[7] = simde_mm256_subs_epi16(y0r_three_over_sqrt10, y0i_three_over_sqrt10);

    for (int j = 0; j < 16; j++) {
      // Compute optimal interfering symbol magnitude
      a_rs[j] = interference_abs_epi16_256(psi_rs[j], ch_mag_int, ONE_OVER_SQRT_10_Q15, THREE_OVER_SQRT_10);
      a_is[j] = interference_abs_epi16_256(psi_is[j], ch_mag_int, ONE_OVER_SQRT_10_Q15, THREE_OVER_SQRT_10);

      // Calculation of groups of two terms in the bit metric involving product of psi and interference magnitude
      psi_as[j] = prodsum_psi_a_epi16_256(psi_rs[j], a_rs[j], psi_is[j], a_is[j]);

      // squared interference magnitude times int. ch. power
      a_sqs[j] = square_a_epi16_256(a_rs[j], a_is[j], ch_mag_int, SQRT_10_OVER_FOUR);
    }

    // Computing different multiples of channel norms
    ch_mag_over_10 = simde_mm256_mulhi_epi16(ch_mag_des, ONE_OVER_TWO_SQRT_10);
    ch_mag_over_2 = simde_mm256_mulhi_epi16(ch_mag_des, SQRT_10_OVER_FOUR);
    ch_mag_over_2 = simde_mm256_slli_epi16(ch_mag_over_2, 1);
    ch_mag_9_over_10 = simde_mm256_mulhi_epi16(ch_mag_des, NINE_OVER_TWO_SQRT_10);
    ch_mag_9_over_10 = simde_mm256_slli_epi16(ch_mag_9_over_10, 2);

    /// Compute bit metrics (lambda)

    simde__m256i bit_mets[16];
    for (int j = 0; j < 8; j += 4) {
      bit_mets[j + 0] = simde_mm256_subs_epi16(psi_as[j + 0], a_sqs[j + 0]);
      bit_mets[j + 0] = simde_mm256_adds_epi16(bit_mets[j + 0], y0_s[j + 0]);
      bit_mets[j + 0] = simde_mm256_subs_epi16(bit_mets[j + 0], ch_mag_over_10);

      bit_mets[j + 1] = simde_mm256_subs_epi16(psi_as[j + 1], a_sqs[j + 1]);
      bit_mets[j + 1] = simde_mm256_adds_epi16(bit_mets[j + 1], y0_s[j + 1]);
      bit_mets[j + 1] = simde_mm256_subs_epi16(bit_mets[j + 1], ch_mag_over_2);
      bit_mets[j + 2] = simde_mm256_subs_epi16(psi_as[j + 2], a_sqs[j + 2]);
      bit_mets[j + 2] = simde_mm256_adds_epi16(bit_mets[j + 2], y0_s[j + 2]);
      bit_mets[j + 2] = simde_mm256_subs_epi16(bit_mets[j + 2], ch_mag_over_2);

      bit_mets[j + 3] = simde_mm256_subs_epi16(psi_as[j + 3], a_sqs[j + 3]);
      bit_mets[j + 3] = simde_mm256_adds_epi16(bit_mets[j + 3], y0_s[j + 3]);
      bit_mets[j + 3] = simde_mm256_subs_epi16(bit_mets[j + 3], ch_mag_9_over_10);
    }
    for (int j = 8; j < 16; j += 4) {
      bit_mets[j + 0] = simde_mm256_subs_epi16(psi_as[j + 0], a_sqs[j + 0]);
      bit_mets[j + 0] = simde_mm256_subs_epi16(bit_mets[j + 0], y0_s[(j - 4) & 0x07]);
      bit_mets[j + 0] = simde_mm256_subs_epi16(bit_mets[j + 0], ch_mag_over_10);

      bit_mets[j + 1] = simde_mm256_subs_epi16(psi_as[j + 1], a_sqs[j + 1]);
      bit_mets[j + 1] = simde_mm256_subs_epi16(bit_mets[j + 1], y0_s[(j - 3) & 0x07]);
      bit_mets[j + 1] = simde_mm256_subs_epi16(bit_mets[j + 1], ch_mag_over_2);
      bit_mets[j + 2] = simde_mm256_subs_epi16(psi_as[j + 2], a_sqs[j + 2]);
      bit_mets[j + 2] = simde_mm256_subs_epi16(bit_mets[j + 2], y0_s[(j - 2) & 0x07]);
      bit_mets[j + 2] = simde_mm256_subs_epi16(bit_mets[j + 2], ch_mag_over_2);

      bit_mets[j + 3] = simde_mm256_subs_epi16(psi_as[j + 3], a_sqs[j + 3]);
      bit_mets[j + 3] = simde_mm256_subs_epi16(bit_mets[j + 3], y0_s[(j - 1) & 0x07]);
      bit_mets[j + 3] = simde_mm256_subs_epi16(bit_mets[j + 3], ch_mag_9_over_10);
    }

    /// Compute the LLRs

    // LLR = lambda(c==1) - lambda(c==0)

    // LLR of the first bit: Bit = 1
    simde__m256i logmax_num_re0 =
        max_epi16_256(bit_mets[8], bit_mets[9], bit_mets[10], bit_mets[11], bit_mets[12], bit_mets[13], bit_mets[14], bit_mets[15]);
    // LLR of the first bit: Bit = 0
    simde__m256i logmax_den_re0 =
        max_epi16_256(bit_mets[0], bit_mets[1], bit_mets[2], bit_mets[3], bit_mets[4], bit_mets[5], bit_mets[6], bit_mets[7]);
    // LLR of the second bit: Bit = 1
    simde__m256i logmax_num_re1 =
        max_epi16_256(bit_mets[4], bit_mets[5], bit_mets[6], bit_mets[7], bit_mets[12], bit_mets[13], bit_mets[14], bit_mets[15]);
    // LLR of the second bit: Bit = 0
    simde__m256i logmax_den_re1 =
        max_epi16_256(bit_mets[0], bit_mets[1], bit_mets[3], bit_mets[2], bit_mets[8], bit_mets[9], bit_mets[10], bit_mets[11]);
    // LLR of the third bit: Bit = 1
    simde__m256i logmax_num_im0 =
        max_epi16_256(bit_mets[2], bit_mets[3], bit_mets[6], bit_mets[7], bit_mets[10], bit_mets[11], bit_mets[14], bit_mets[15]);
    // LLR of the third bit: Bit = 0
    simde__m256i logmax_den_im0 =
        max_epi16_256(bit_mets[0], bit_mets[1], bit_mets[4], bit_mets[5], bit_mets[8], bit_mets[9], bit_mets[12], bit_mets[13]);
    // LLR of the fourth bit: Bit = 1
    simde__m256i logmax_num_im1 =
        max_epi16_256(bit_mets[1], bit_mets[3], bit_mets[5], bit_mets[7], bit_mets[9], bit_mets[11], bit_mets[13], bit_mets[15]);
    // LLR of the fourth bit: Bit = 0
    simde__m256i logmax_den_im1 =
        max_epi16_256(bit_mets[0], bit_mets[2], bit_mets[4], bit_mets[6], bit_mets[8], bit_mets[10], bit_mets[12], bit_mets[14]);

    y0r = simde_mm256_subs_epi16(logmax_den_re0,
                                 logmax_num_re0); // LLR of first bit  [L1(1), L1(2), L1(3), L1(4), L1(5), L1(6), L1(7), L1(8)...]
    y1r = simde_mm256_subs_epi16(logmax_den_re1,
                                 logmax_num_re1); // LLR of second bit [L2(1), L2(2), L2(3), L2(4), L2(5), L2(6), L2(7), L2(8)...]
    y0i = simde_mm256_subs_epi16(logmax_den_im0,
                                 logmax_num_im0); // LLR of third bit  [L3(1), L3(2), L3(3), L3(4), L3(5), L3(6), L3(7), L3(8)...]
    y1i = simde_mm256_subs_epi16(logmax_den_im1,
                                 logmax_num_im1); // LLR of fourth bit [L4(1), L4(2), L4(3), L4(4), L4(5), L4(6), L4(7), L4(8)...]

    // Pack LLRs in output
    simde__m128i *y0r_128 = (simde__m128i *)&y0r;
    simde__m128i *y1r_128 = (simde__m128i *)&y1r;
    simde__m128i *y0i_128 = (simde__m128i *)&y0i;
    simde__m128i *y1i_128 = (simde__m128i *)&y1i;
    simde__m128i xmm0_128, xmm1_128, xmm2_128, xmm3_128;
    xmm0_128 = simde_mm_unpacklo_epi16(y0r_128[0], y1r_128[0]); // [L1(1), L2(1), L1(2), L2(2), L1(3), L2(3), L1(4), L2(4)]
    xmm1_128 = simde_mm_unpackhi_epi16(y0r_128[0], y1r_128[0]); // [L1(5), L2(5), L1(6), L2(6), L1(7), L2(7), L1(8), L2(8)]
    xmm2_128 = simde_mm_unpacklo_epi16(y0i_128[0], y1i_128[0]); // [L3(1), L4(1), L3(2), L4(2), L3(3), L4(3), L3(4), L4(4)]
    xmm3_128 = simde_mm_unpackhi_epi16(y0i_128[0], y1i_128[0]); // [L3(5), L4(5), L3(6), L4(6), L3(7), L4(7), L3(8), L4(8)]
    simde__m128i *stream0_128i_out = (simde__m128i *)&stream0_256i_out[2 * i + 0];
    stream0_128i_out[0] = simde_mm_unpacklo_epi32(xmm0_128, xmm2_128); // 8 LLRs, 2 REs
    stream0_128i_out[1] = simde_mm_unpackhi_epi32(xmm0_128, xmm2_128); // 8 LLRs, 2 REs
    stream0_128i_out[2] = simde_mm_unpacklo_epi32(xmm1_128, xmm3_128); // 8 LLRs, 2 REs
    stream0_128i_out[3] = simde_mm_unpackhi_epi32(xmm1_128, xmm3_128); // 8 LLRs, 2 REs
    xmm0_128 = simde_mm_unpacklo_epi16(y0r_128[1], y1r_128[1]); // [L1(9), L2(9), L1(10), L2(10), L1(11), L2(11), L1(12), L2(12)]
    xmm1_128 = simde_mm_unpackhi_epi16(y0r_128[1], y1r_128[1]); // [L1(13), L2(13), L1(14), L2(14), L1(15), L2(15), L1(16), L2(16)]
    xmm2_128 = simde_mm_unpacklo_epi16(y0i_128[1], y1i_128[1]); // [L3(9), L4(9), L3(10), L4(10), L3(11), L4(11), L3(12), L4(12)]
    xmm3_128 = simde_mm_unpackhi_epi16(y0i_128[1], y1i_128[1]); // [L3(13), L4(13), L3(14), L4(14), L3(15), L4(15), L3(16), L4(16)]
    stream0_128i_out = (simde__m128i *)&stream0_256i_out[2 * i + 2];
    stream0_128i_out[0] = simde_mm_unpacklo_epi32(xmm0_128, xmm2_128); // 8 LLRs, 2 REs
    stream0_128i_out[1] = simde_mm_unpackhi_epi32(xmm0_128, xmm2_128); // 8 LLRs, 2 REs
    stream0_128i_out[2] = simde_mm_unpacklo_epi32(xmm1_128, xmm3_128); // 8 LLRs, 2 REs
    stream0_128i_out[3] = simde_mm_unpackhi_epi32(xmm1_128, xmm3_128); // 8 LLRs, 2 REs
  }
#endif
}

/*
 * This function computes the LLRs of stream 0 (s_0) in presence of the interfering stream 1 (s_1) assuming that both symbols are
 * 64QAM. It can be used for both MU-MIMO interference-aware receiver or for SU-MIMO receivers.
 *
 * Input:
 *   stream0_in:  MF filter output for 1st stream, i.e., y0' = h0'*y0
 *   stream1_in:  MF filter output for 2nd stream, i.e., y1' = h1'*y0
 *   ch_mag:      4*h0/sqrt(42), [Re0 Im0 Re1 Im1] s.t. Im0=Re0, Im1=Re1, etc
 *   ch_mag_i:    4*h0/sqrt(42), [Re0 Im0 Re1 Im1] s.t. Im0=Re0, Im1=Re1, etc
 *   rho01:       Channel cross correlation, i.e., rho01 = h0'*h1
 *   length:      Number of resource elements
 *
 * Output:
 *   stream0_out: Output LLRs for 1st stream
 */
void nr_qam64_llr_2layer(c16_t *stream0_in,
                         c16_t *stream1_in,
                         c16_t *ch_mag,
                         c16_t *ch_mag_i,
                         int16_t *stream0_out,
                         c16_t *rho01,
                         uint32_t length)
{
#ifdef USE_128BIT
  simde__m128i *rho01_128i = (simde__m128i *)rho01;
  simde__m128i *stream0_128i_in = (simde__m128i *)stream0_in;
  simde__m128i *stream1_128i_in = (simde__m128i *)stream1_in;
  simde__m128i *ch_mag_128i = (simde__m128i *)ch_mag;
  simde__m128i *ch_mag_128i_i = (simde__m128i *)ch_mag_i;

  simde__m128i ONE_OVER_SQRT_42 = simde_mm_set1_epi16(10112); // round(1/sqrt(42)*2^16)
  simde__m128i THREE_OVER_SQRT_42 = simde_mm_set1_epi16(30337); // round(3/sqrt(42)*2^16)
  simde__m128i FIVE_OVER_SQRT_42 = simde_mm_set1_epi16(25281); // round(5/sqrt(42)*2^15)
  simde__m128i SEVEN_OVER_SQRT_42 = simde_mm_set1_epi16(17697); // round(7/sqrt(42)*2^14) Q2.14
  simde__m128i ONE_OVER_SQRT_2 = simde_mm_set1_epi16(23170); // round(1/sqrt(2)*2^15)
  simde__m128i ONE_OVER_SQRT_2_42 = simde_mm_set1_epi16(3575); // round(1/sqrt(2*42)*2^15)
  simde__m128i THREE_OVER_SQRT_2_42 = simde_mm_set1_epi16(10726); // round(3/sqrt(2*42)*2^15)
  simde__m128i FIVE_OVER_SQRT_2_42 = simde_mm_set1_epi16(17876); // round(5/sqrt(2*42)*2^15)
  simde__m128i SEVEN_OVER_SQRT_2_42 = simde_mm_set1_epi16(25027); // round(7/sqrt(2*42)*2^15)
  simde__m128i FORTYNINE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(30969); // round(49/(4*sqrt(42))*2^14), Q2.14
  simde__m128i THIRTYSEVEN_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(23385); // round(37/(4*sqrt(42))*2^14), Q2.14
  simde__m128i TWENTYFIVE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(31601); // round(25/(4*sqrt(42))*2^15)
  simde__m128i TWENTYNINE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(18329); // round(29/(4*sqrt(42))*2^15), Q2.14
  simde__m128i SEVENTEEN_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(21489); // round(17/(4*sqrt(42))*2^15)
  simde__m128i NINE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(11376); // round(9/(4*sqrt(42))*2^15)
  simde__m128i THIRTEEN_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(16433); // round(13/(4*sqrt(42))*2^15)
  simde__m128i FIVE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(6320); // round(5/(4*sqrt(42))*2^15)
  simde__m128i ONE_OVER_FOUR_SQRT_42 = simde_mm_set1_epi16(1264); // round(1/(4*sqrt(42))*2^15)
  simde__m128i SQRT_42_OVER_FOUR = simde_mm_set1_epi16(13272); // round(sqrt(42)/4*2^13), Q3.12

  simde__m128i ch_mag_des;
  simde__m128i ch_mag_int;
  simde__m128i y0r_one_over_sqrt_21;
  simde__m128i y0r_three_over_sqrt_21;
  simde__m128i y0r_five_over_sqrt_21;
  simde__m128i y0r_seven_over_sqrt_21;
  simde__m128i y0i_one_over_sqrt_21;
  simde__m128i y0i_three_over_sqrt_21;
  simde__m128i y0i_five_over_sqrt_21;
  simde__m128i y0i_seven_over_sqrt_21;
  simde__m128i ch_mag_int_with_sigma2;
  simde__m128i two_ch_mag_int_with_sigma2;
  simde__m128i three_ch_mag_int_with_sigma2;

  for (int i = 0; i < (length + 3) >> 2; i += 2) {
    // Get rho
    simde__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8;
    oai_mm_separate_real_imag_parts(&xmm2, &xmm3, rho01_128i[i], rho01_128i[i + 1]);

    simde__m128i rho_rpi = simde_mm_adds_epi16(xmm2, xmm3); // rho = Re(rho) + Im(rho)
    simde__m128i rho_rmi = simde_mm_subs_epi16(xmm2, xmm3); // rho* = Re(rho) - Im(rho)

    // Compute the different rhos
    simde__m128i rho_rs[32];
    rho_rs[27] = simde_mm_mulhi_epi16(rho_rpi, ONE_OVER_SQRT_42);
    rho_rs[28] = simde_mm_mulhi_epi16(rho_rmi, ONE_OVER_SQRT_42);
    rho_rs[18] = simde_mm_mulhi_epi16(rho_rpi, THREE_OVER_SQRT_42);
    rho_rs[21] = simde_mm_mulhi_epi16(rho_rmi, THREE_OVER_SQRT_42);
    rho_rs[9] = simde_mm_mulhi_epi16(rho_rpi, FIVE_OVER_SQRT_42);
    rho_rs[14] = simde_mm_mulhi_epi16(rho_rmi, FIVE_OVER_SQRT_42);
    rho_rs[0] = simde_mm_mulhi_epi16(rho_rpi, SEVEN_OVER_SQRT_42);
    rho_rs[7] = simde_mm_mulhi_epi16(rho_rmi, SEVEN_OVER_SQRT_42);

    rho_rs[9] = simde_mm_slli_epi16(rho_rs[9], 1);
    rho_rs[14] = simde_mm_slli_epi16(rho_rs[14], 1);
    rho_rs[0] = simde_mm_slli_epi16(rho_rs[0], 2);
    rho_rs[7] = simde_mm_slli_epi16(rho_rs[7], 2);

    xmm4 = simde_mm_mulhi_epi16(xmm2, ONE_OVER_SQRT_42);
    xmm5 = simde_mm_mulhi_epi16(xmm3, ONE_OVER_SQRT_42);
    xmm6 = simde_mm_mulhi_epi16(xmm3, THREE_OVER_SQRT_42);
    xmm7 = simde_mm_mulhi_epi16(xmm3, FIVE_OVER_SQRT_42);
    xmm8 = simde_mm_mulhi_epi16(xmm3, SEVEN_OVER_SQRT_42);
    xmm7 = simde_mm_slli_epi16(xmm7, 1);
    xmm8 = simde_mm_slli_epi16(xmm8, 2);

    rho_rs[26] = simde_mm_adds_epi16(xmm4, xmm6);
    rho_rs[29] = simde_mm_subs_epi16(xmm4, xmm6);
    rho_rs[25] = simde_mm_adds_epi16(xmm4, xmm7);
    rho_rs[30] = simde_mm_subs_epi16(xmm4, xmm7);
    rho_rs[24] = simde_mm_adds_epi16(xmm4, xmm8);
    rho_rs[31] = simde_mm_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm_mulhi_epi16(xmm2, THREE_OVER_SQRT_42);
    rho_rs[19] = simde_mm_adds_epi16(xmm4, xmm5);
    rho_rs[20] = simde_mm_subs_epi16(xmm4, xmm5);
    rho_rs[17] = simde_mm_adds_epi16(xmm4, xmm7);
    rho_rs[22] = simde_mm_subs_epi16(xmm4, xmm7);
    rho_rs[16] = simde_mm_adds_epi16(xmm4, xmm8);
    rho_rs[23] = simde_mm_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm_mulhi_epi16(xmm2, FIVE_OVER_SQRT_42);
    xmm4 = simde_mm_slli_epi16(xmm4, 1);
    rho_rs[11] = simde_mm_adds_epi16(xmm4, xmm5);
    rho_rs[12] = simde_mm_subs_epi16(xmm4, xmm5);
    rho_rs[10] = simde_mm_adds_epi16(xmm4, xmm6);
    rho_rs[13] = simde_mm_subs_epi16(xmm4, xmm6);
    rho_rs[8] = simde_mm_adds_epi16(xmm4, xmm8);
    rho_rs[15] = simde_mm_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm_mulhi_epi16(xmm2, SEVEN_OVER_SQRT_42);
    xmm4 = simde_mm_slli_epi16(xmm4, 2);
    rho_rs[3] = simde_mm_adds_epi16(xmm4, xmm5);
    rho_rs[4] = simde_mm_subs_epi16(xmm4, xmm5);
    rho_rs[2] = simde_mm_adds_epi16(xmm4, xmm6);
    rho_rs[5] = simde_mm_subs_epi16(xmm4, xmm6);
    rho_rs[1] = simde_mm_adds_epi16(xmm4, xmm7);
    rho_rs[6] = simde_mm_subs_epi16(xmm4, xmm7);

    // Rearrange interfering MF output
    simde__m128i y1r, y1i;
    oai_mm_separate_real_imag_parts(&y1r, &y1i, stream1_128i_in[i], stream1_128i_in[i + 1]);

    // Psi_r calculation from rho_rpi or rho_rmi
    xmm0 = simde_mm_set1_epi16(0); // ZERO for abs_pi16
    xmm2 = simde_mm_subs_epi16(rho_rs[0], y1r);

    simde__m128i psi_r_s[64];
    for (int j = 0; j < 32; j++) // psi_r_s[0~31], rho_rs[0~31]
      psi_r_s[j] = simde_mm_abs_epi16(simde_mm_subs_epi16(rho_rs[j], y1r));
    for (int j = 32; j < 64; j++) // psi_r_s[32~64], rho_rs[31~0]
      psi_r_s[j] = simde_mm_abs_epi16(simde_mm_adds_epi16(rho_rs[63 - j], y1r));

    // simde__m128i psi_i calculation from rho_rpi or rho_rmi
    simde__m128i psi_i_s[64];
    const uint8_t rho_rs_index[32] = {7, 15, 23, 31, 24, 16, 8,  0, 6, 14, 22, 30, 25, 17, 9,  1,
                                      5, 13, 21, 29, 26, 18, 10, 2, 4, 12, 20, 28, 27, 19, 11, 3};
    for (int k = 0; k < 32; k += 8) { // psi_i_s[0~31]
      for (int j = k; j < k + 4; j++)
        psi_i_s[j] = simde_mm_abs_epi16(simde_mm_subs_epi16(rho_rs[rho_rs_index[j]], y1i));
      for (int j = k + 4; j < k + 8; j++)
        psi_i_s[j] = simde_mm_abs_epi16(simde_mm_adds_epi16(rho_rs[rho_rs_index[j]], y1i));
    }
    for (int k = 32; k < 64; k += 8) { // psi_i_s[32~64]
      for (int j = k; j < k + 4; j++)
        psi_i_s[j] = simde_mm_abs_epi16(simde_mm_subs_epi16(rho_rs[rho_rs_index[63 - j]], y1i));
      for (int j = k + 4; j < k + 8; j++)
        psi_i_s[j] = simde_mm_abs_epi16(simde_mm_adds_epi16(rho_rs[rho_rs_index[63 - j]], y1i));
    }

    // Rearrange desired MF output
    simde__m128i y0r, y0i;
    oai_mm_separate_real_imag_parts(&y0r, &y0i, stream0_128i_in[i], stream0_128i_in[i + 1]);

    // Rearrange desired channel magnitudes
    // [|h|^2(1),|h|^2(1),|h|^2(2),|h|^2(2),...,,|h|^2(7),|h|^2(7)]*(2/sqrt(10))
    // xmm2 is dummy variable that contains the same values as ch_mag_des
    oai_mm_separate_real_imag_parts(&ch_mag_des, &xmm2, ch_mag_128i[i], ch_mag_128i[i + 1]);

    // Rearrange interfering channel magnitudes
    oai_mm_separate_real_imag_parts(&ch_mag_int, &xmm2, ch_mag_128i_i[i], ch_mag_128i_i[i + 1]);

    y0r_one_over_sqrt_21 = simde_mm_mulhi_epi16(y0r, ONE_OVER_SQRT_42);
    y0r_three_over_sqrt_21 = simde_mm_mulhi_epi16(y0r, THREE_OVER_SQRT_42);
    y0r_five_over_sqrt_21 = simde_mm_mulhi_epi16(y0r, FIVE_OVER_SQRT_42);
    y0r_five_over_sqrt_21 = simde_mm_slli_epi16(y0r_five_over_sqrt_21, 1);
    y0r_seven_over_sqrt_21 = simde_mm_mulhi_epi16(y0r, SEVEN_OVER_SQRT_42);
    y0r_seven_over_sqrt_21 = simde_mm_slli_epi16(y0r_seven_over_sqrt_21, 2); // Q2.14

    y0i_one_over_sqrt_21 = simde_mm_mulhi_epi16(y0i, ONE_OVER_SQRT_42);
    y0i_three_over_sqrt_21 = simde_mm_mulhi_epi16(y0i, THREE_OVER_SQRT_42);
    y0i_five_over_sqrt_21 = simde_mm_mulhi_epi16(y0i, FIVE_OVER_SQRT_42);
    y0i_five_over_sqrt_21 = simde_mm_slli_epi16(y0i_five_over_sqrt_21, 1);
    y0i_seven_over_sqrt_21 = simde_mm_mulhi_epi16(y0i, SEVEN_OVER_SQRT_42);
    y0i_seven_over_sqrt_21 = simde_mm_slli_epi16(y0i_seven_over_sqrt_21, 2); // Q2.14

    simde__m128i y0_s[64];
    const simde__m128i y0r_over_s[8] = {y0r_seven_over_sqrt_21,
                                        y0r_five_over_sqrt_21,
                                        y0r_three_over_sqrt_21,
                                        y0r_one_over_sqrt_21};
    for (int j = 0; j < 32; j += 8) {
      y0_s[j + 0] = simde_mm_adds_epi16(y0r_over_s[j >> 3], y0i_seven_over_sqrt_21);
      y0_s[j + 1] = simde_mm_adds_epi16(y0r_over_s[j >> 3], y0i_five_over_sqrt_21);
      y0_s[j + 2] = simde_mm_adds_epi16(y0r_over_s[j >> 3], y0i_three_over_sqrt_21);
      y0_s[j + 3] = simde_mm_adds_epi16(y0r_over_s[j >> 3], y0i_one_over_sqrt_21);
      y0_s[j + 4] = simde_mm_subs_epi16(y0r_over_s[j >> 3], y0i_one_over_sqrt_21);
      y0_s[j + 5] = simde_mm_subs_epi16(y0r_over_s[j >> 3], y0i_three_over_sqrt_21);
      y0_s[j + 6] = simde_mm_subs_epi16(y0r_over_s[j >> 3], y0i_five_over_sqrt_21);
      y0_s[j + 7] = simde_mm_subs_epi16(y0r_over_s[j >> 3], y0i_seven_over_sqrt_21);
    }

    ch_mag_int_with_sigma2 = simde_mm_srai_epi16(ch_mag_int, 1); // *2
    two_ch_mag_int_with_sigma2 = ch_mag_int; // *4
    three_ch_mag_int_with_sigma2 = simde_mm_adds_epi16(ch_mag_int_with_sigma2, two_ch_mag_int_with_sigma2); // *6
    simde__m128i a_r_s[64];
    simde__m128i a_i_s[64];
    simde__m128i psi_a_s[64];
    simde__m128i a_sq_s[64];
    for (int j = 0; j < 64; j++) {
      // Detection of interference term
      a_r_s[j] = interference_abs_64qam_epi16(psi_r_s[j],
                                              ch_mag_int_with_sigma2,
                                              two_ch_mag_int_with_sigma2,
                                              three_ch_mag_int_with_sigma2,
                                              ONE_OVER_SQRT_2_42,
                                              THREE_OVER_SQRT_2_42,
                                              FIVE_OVER_SQRT_2_42,
                                              SEVEN_OVER_SQRT_2_42);
      a_i_s[j] = interference_abs_64qam_epi16(psi_i_s[j],
                                              ch_mag_int_with_sigma2,
                                              two_ch_mag_int_with_sigma2,
                                              three_ch_mag_int_with_sigma2,
                                              ONE_OVER_SQRT_2_42,
                                              THREE_OVER_SQRT_2_42,
                                              FIVE_OVER_SQRT_2_42,
                                              SEVEN_OVER_SQRT_2_42);

      // Calculation of a group of two terms in the bit metric involving product of psi and interference
      psi_a_s[j] = prodsum_psi_a_epi16(psi_r_s[j], a_r_s[j], psi_i_s[j], a_i_s[j]);

      // Multiply by sqrt(2)
      psi_a_s[j] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(psi_a_s[j], ONE_OVER_SQRT_2), 2);

      // Calculation of a group of two terms in the bit metric involving squares of interference
      a_sq_s[j] = square_a_64qam_epi16(a_r_s[j], a_i_s[j], ch_mag_int, SQRT_42_OVER_FOUR);
    }

    // Computing different multiples of ||h0||^2
    simde__m128i ch_mag_with_sigma2[10];
    enum ch_mag_over_42with_sigma2_vals { mag2 = 0, mag10, mag26, mag18, mag34, mag58, mag50, mag74, mag98 };
    // x=1, y=1
    ch_mag_with_sigma2[mag2] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, ONE_OVER_FOUR_SQRT_42), 1);
    // x=1, y=3
    ch_mag_with_sigma2[mag10] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, FIVE_OVER_FOUR_SQRT_42), 1);
    // x=1, x=5
    ch_mag_with_sigma2[mag26] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, THIRTEEN_OVER_FOUR_SQRT_42), 1);
    // x=1, y=7
    ch_mag_with_sigma2[mag50] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, TWENTYFIVE_OVER_FOUR_SQRT_42), 1);
    // x=3, y=3
    ch_mag_with_sigma2[mag18] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, NINE_OVER_FOUR_SQRT_42), 1);
    // x=3, y=5
    ch_mag_with_sigma2[mag34] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, SEVENTEEN_OVER_FOUR_SQRT_42), 1);
    // x=3, y=7
    ch_mag_with_sigma2[mag58] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, TWENTYNINE_OVER_FOUR_SQRT_42), 2);
    // x=5, y=5
    ch_mag_with_sigma2[mag50] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, TWENTYFIVE_OVER_FOUR_SQRT_42), 1);
    // x=5, y=7
    ch_mag_with_sigma2[mag74] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, THIRTYSEVEN_OVER_FOUR_SQRT_42), 2);
    // x=7, y=7
    ch_mag_with_sigma2[mag98] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(ch_mag_des, FORTYNINE_OVER_FOUR_SQRT_42), 2);

    // Computing Metrics
    simde__m128i bit_met_s[64];
    const enum ch_mag_over_42with_sigma2_vals table[] = {
        mag98, mag74, mag58, mag50, mag50, mag58, mag74, mag98, mag74, mag50, mag34, mag26, mag26, mag34, mag50, mag74,
        mag58, mag34, mag18, mag10, mag10, mag18, mag34, mag58, mag50, mag26, mag10, mag2,  mag2,  mag10, mag26, mag50};

    for (int i = 0; i < 32; i++) {
      const simde__m128i x = simde_mm_adds_epi16(simde_mm_subs_epi16(psi_a_s[i], a_sq_s[i]), y0_s[i]);
      bit_met_s[i] = simde_mm_subs_epi16(x, ch_mag_with_sigma2[table[i]]);
    }
    for (int i = 0; i < 32; i++) {
      const simde__m128i x = simde_mm_subs_epi16(simde_mm_subs_epi16(psi_a_s[32 + i], a_sq_s[32 + i]), y0_s[31 - i]);
      bit_met_s[32 + i] = simde_mm_subs_epi16(x, ch_mag_with_sigma2[table[31 - i]]);
    }

    // Detection for bits
    simde__m128i logmax_den_re0;
    simde__m128i logmax_num_re0;
    // Detection for 1st bit
    // bit = 1
    xmm0 = max_epi16(bit_met_s[56],
                     bit_met_s[57],
                     bit_met_s[58],
                     bit_met_s[59],
                     bit_met_s[60],
                     bit_met_s[61],
                     bit_met_s[62],
                     bit_met_s[63]);
    xmm1 = max_epi16(bit_met_s[48],
                     bit_met_s[49],
                     bit_met_s[50],
                     bit_met_s[51],
                     bit_met_s[52],
                     bit_met_s[53],
                     bit_met_s[54],
                     bit_met_s[55]);
    xmm2 = max_epi16(bit_met_s[40],
                     bit_met_s[41],
                     bit_met_s[42],
                     bit_met_s[43],
                     bit_met_s[44],
                     bit_met_s[45],
                     bit_met_s[46],
                     bit_met_s[47]);
    xmm3 = max_epi16(bit_met_s[32],
                     bit_met_s[33],
                     bit_met_s[34],
                     bit_met_s[35],
                     bit_met_s[36],
                     bit_met_s[37],
                     bit_met_s[38],
                     bit_met_s[39]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    // bit = 0
    xmm0 =
        max_epi16(bit_met_s[0], bit_met_s[1], bit_met_s[2], bit_met_s[3], bit_met_s[4], bit_met_s[5], bit_met_s[6], bit_met_s[7]);
    xmm1 = max_epi16(bit_met_s[8],
                     bit_met_s[9],
                     bit_met_s[10],
                     bit_met_s[11],
                     bit_met_s[12],
                     bit_met_s[13],
                     bit_met_s[14],
                     bit_met_s[15]);
    xmm2 = max_epi16(bit_met_s[16],
                     bit_met_s[17],
                     bit_met_s[18],
                     bit_met_s[19],
                     bit_met_s[20],
                     bit_met_s[21],
                     bit_met_s[22],
                     bit_met_s[23]);
    xmm3 = max_epi16(bit_met_s[24],
                     bit_met_s[25],
                     bit_met_s[26],
                     bit_met_s[27],
                     bit_met_s[28],
                     bit_met_s[29],
                     bit_met_s[30],
                     bit_met_s[31]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    y0r = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 2nd bit
    // bit = 1
    xmm0 = max_epi16(bit_met_s[4],
                     bit_met_s[12],
                     bit_met_s[20],
                     bit_met_s[28],
                     bit_met_s[36],
                     bit_met_s[44],
                     bit_met_s[52],
                     bit_met_s[60]);
    xmm1 = max_epi16(bit_met_s[5],
                     bit_met_s[13],
                     bit_met_s[21],
                     bit_met_s[29],
                     bit_met_s[37],
                     bit_met_s[45],
                     bit_met_s[53],
                     bit_met_s[61]);
    xmm2 = max_epi16(bit_met_s[6],
                     bit_met_s[14],
                     bit_met_s[22],
                     bit_met_s[30],
                     bit_met_s[38],
                     bit_met_s[46],
                     bit_met_s[54],
                     bit_met_s[62]);
    xmm3 = max_epi16(bit_met_s[7],
                     bit_met_s[15],
                     bit_met_s[23],
                     bit_met_s[31],
                     bit_met_s[39],
                     bit_met_s[47],
                     bit_met_s[55],
                     bit_met_s[63]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    // bit = 0
    xmm0 = max_epi16(bit_met_s[3],
                     bit_met_s[11],
                     bit_met_s[19],
                     bit_met_s[27],
                     bit_met_s[35],
                     bit_met_s[43],
                     bit_met_s[51],
                     bit_met_s[59]);
    xmm1 = max_epi16(bit_met_s[2],
                     bit_met_s[10],
                     bit_met_s[18],
                     bit_met_s[26],
                     bit_met_s[34],
                     bit_met_s[42],
                     bit_met_s[50],
                     bit_met_s[58]);
    xmm2 = max_epi16(bit_met_s[1],
                     bit_met_s[9],
                     bit_met_s[17],
                     bit_met_s[25],
                     bit_met_s[33],
                     bit_met_s[41],
                     bit_met_s[49],
                     bit_met_s[57]);
    xmm3 = max_epi16(bit_met_s[0],
                     bit_met_s[8],
                     bit_met_s[16],
                     bit_met_s[24],
                     bit_met_s[32],
                     bit_met_s[40],
                     bit_met_s[48],
                     bit_met_s[56]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    y1r = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 3rd bit
    xmm0 = max_epi16(bit_met_s[63],
                     bit_met_s[62],
                     bit_met_s[61],
                     bit_met_s[60],
                     bit_met_s[59],
                     bit_met_s[58],
                     bit_met_s[57],
                     bit_met_s[56]);
    xmm1 = max_epi16(bit_met_s[55],
                     bit_met_s[54],
                     bit_met_s[53],
                     bit_met_s[52],
                     bit_met_s[51],
                     bit_met_s[50],
                     bit_met_s[49],
                     bit_met_s[48]);
    xmm2 = max_epi16(bit_met_s[15],
                     bit_met_s[14],
                     bit_met_s[13],
                     bit_met_s[12],
                     bit_met_s[11],
                     bit_met_s[10],
                     bit_met_s[9],
                     bit_met_s[8]);
    xmm3 =
        max_epi16(bit_met_s[7], bit_met_s[6], bit_met_s[5], bit_met_s[4], bit_met_s[3], bit_met_s[2], bit_met_s[1], bit_met_s[0]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16(bit_met_s[47],
                     bit_met_s[46],
                     bit_met_s[45],
                     bit_met_s[44],
                     bit_met_s[43],
                     bit_met_s[42],
                     bit_met_s[41],
                     bit_met_s[40]);
    xmm1 = max_epi16(bit_met_s[39],
                     bit_met_s[38],
                     bit_met_s[37],
                     bit_met_s[36],
                     bit_met_s[35],
                     bit_met_s[34],
                     bit_met_s[33],
                     bit_met_s[32]);
    xmm2 = max_epi16(bit_met_s[31],
                     bit_met_s[30],
                     bit_met_s[29],
                     bit_met_s[28],
                     bit_met_s[27],
                     bit_met_s[26],
                     bit_met_s[25],
                     bit_met_s[24]);
    xmm3 = max_epi16(bit_met_s[23],
                     bit_met_s[22],
                     bit_met_s[21],
                     bit_met_s[20],
                     bit_met_s[19],
                     bit_met_s[18],
                     bit_met_s[17],
                     bit_met_s[16]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    simde__m128i y2r = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 4th bit
    xmm0 = max_epi16(bit_met_s[0],
                     bit_met_s[8],
                     bit_met_s[16],
                     bit_met_s[24],
                     bit_met_s[32],
                     bit_met_s[40],
                     bit_met_s[48],
                     bit_met_s[56]);
    xmm1 = max_epi16(bit_met_s[1],
                     bit_met_s[9],
                     bit_met_s[17],
                     bit_met_s[25],
                     bit_met_s[33],
                     bit_met_s[41],
                     bit_met_s[49],
                     bit_met_s[57]);
    xmm2 = max_epi16(bit_met_s[6],
                     bit_met_s[14],
                     bit_met_s[22],
                     bit_met_s[30],
                     bit_met_s[38],
                     bit_met_s[46],
                     bit_met_s[54],
                     bit_met_s[62]);
    xmm3 = max_epi16(bit_met_s[7],
                     bit_met_s[15],
                     bit_met_s[23],
                     bit_met_s[31],
                     bit_met_s[39],
                     bit_met_s[47],
                     bit_met_s[55],
                     bit_met_s[63]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16(bit_met_s[4],
                     bit_met_s[12],
                     bit_met_s[20],
                     bit_met_s[28],
                     bit_met_s[36],
                     bit_met_s[44],
                     bit_met_s[52],
                     bit_met_s[60]);
    xmm1 = max_epi16(bit_met_s[5],
                     bit_met_s[13],
                     bit_met_s[21],
                     bit_met_s[29],
                     bit_met_s[37],
                     bit_met_s[45],
                     bit_met_s[53],
                     bit_met_s[61]);
    xmm2 = max_epi16(bit_met_s[3],
                     bit_met_s[11],
                     bit_met_s[19],
                     bit_met_s[27],
                     bit_met_s[35],
                     bit_met_s[43],
                     bit_met_s[51],
                     bit_met_s[59]);
    xmm3 = max_epi16(bit_met_s[2],
                     bit_met_s[10],
                     bit_met_s[18],
                     bit_met_s[26],
                     bit_met_s[34],
                     bit_met_s[42],
                     bit_met_s[50],
                     bit_met_s[58]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    y0i = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 5th bit
    xmm0 = max_epi16(bit_met_s[63],
                     bit_met_s[62],
                     bit_met_s[61],
                     bit_met_s[60],
                     bit_met_s[59],
                     bit_met_s[58],
                     bit_met_s[57],
                     bit_met_s[56]);
    xmm1 = max_epi16(bit_met_s[39],
                     bit_met_s[38],
                     bit_met_s[37],
                     bit_met_s[36],
                     bit_met_s[35],
                     bit_met_s[34],
                     bit_met_s[33],
                     bit_met_s[32]);
    xmm2 = max_epi16(bit_met_s[31],
                     bit_met_s[30],
                     bit_met_s[29],
                     bit_met_s[28],
                     bit_met_s[27],
                     bit_met_s[26],
                     bit_met_s[25],
                     bit_met_s[24]);
    xmm3 =
        max_epi16(bit_met_s[7], bit_met_s[6], bit_met_s[5], bit_met_s[4], bit_met_s[3], bit_met_s[2], bit_met_s[1], bit_met_s[0]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16(bit_met_s[55],
                     bit_met_s[54],
                     bit_met_s[53],
                     bit_met_s[52],
                     bit_met_s[51],
                     bit_met_s[50],
                     bit_met_s[49],
                     bit_met_s[48]);
    xmm1 = max_epi16(bit_met_s[47],
                     bit_met_s[46],
                     bit_met_s[45],
                     bit_met_s[44],
                     bit_met_s[43],
                     bit_met_s[42],
                     bit_met_s[41],
                     bit_met_s[40]);
    xmm2 = max_epi16(bit_met_s[23],
                     bit_met_s[22],
                     bit_met_s[21],
                     bit_met_s[20],
                     bit_met_s[19],
                     bit_met_s[18],
                     bit_met_s[17],
                     bit_met_s[16]);
    xmm3 = max_epi16(bit_met_s[15],
                     bit_met_s[14],
                     bit_met_s[13],
                     bit_met_s[12],
                     bit_met_s[11],
                     bit_met_s[10],
                     bit_met_s[9],
                     bit_met_s[8]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    y1i = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 6th bit
    xmm0 = max_epi16(bit_met_s[0],
                     bit_met_s[8],
                     bit_met_s[16],
                     bit_met_s[24],
                     bit_met_s[32],
                     bit_met_s[40],
                     bit_met_s[48],
                     bit_met_s[56]);
    xmm1 = max_epi16(bit_met_s[3],
                     bit_met_s[11],
                     bit_met_s[19],
                     bit_met_s[27],
                     bit_met_s[35],
                     bit_met_s[43],
                     bit_met_s[51],
                     bit_met_s[59]);
    xmm2 = max_epi16(bit_met_s[4],
                     bit_met_s[12],
                     bit_met_s[20],
                     bit_met_s[28],
                     bit_met_s[36],
                     bit_met_s[44],
                     bit_met_s[52],
                     bit_met_s[60]);
    xmm3 = max_epi16(bit_met_s[7],
                     bit_met_s[15],
                     bit_met_s[23],
                     bit_met_s[31],
                     bit_met_s[39],
                     bit_met_s[47],
                     bit_met_s[55],
                     bit_met_s[63]);
    logmax_den_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16(bit_met_s[6],
                     bit_met_s[14],
                     bit_met_s[22],
                     bit_met_s[30],
                     bit_met_s[38],
                     bit_met_s[46],
                     bit_met_s[54],
                     bit_met_s[62]);
    xmm1 = max_epi16(bit_met_s[5],
                     bit_met_s[13],
                     bit_met_s[21],
                     bit_met_s[29],
                     bit_met_s[37],
                     bit_met_s[45],
                     bit_met_s[53],
                     bit_met_s[61]);
    xmm2 = max_epi16(bit_met_s[2],
                     bit_met_s[10],
                     bit_met_s[18],
                     bit_met_s[26],
                     bit_met_s[34],
                     bit_met_s[42],
                     bit_met_s[50],
                     bit_met_s[58]);
    xmm3 = max_epi16(bit_met_s[1],
                     bit_met_s[9],
                     bit_met_s[17],
                     bit_met_s[25],
                     bit_met_s[33],
                     bit_met_s[41],
                     bit_met_s[49],
                     bit_met_s[57]);
    logmax_num_re0 = simde_mm_max_epi16(simde_mm_max_epi16(xmm0, xmm1), simde_mm_max_epi16(xmm2, xmm3));

    simde__m128i y2i = simde_mm_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Map to output stream, difficult to do in SIMD since we have 6 16bit LLRs
    for (int re = 0; re < 8; re++) {
      *stream0_out++ = ((short *)&y0r)[re];
      *stream0_out++ = ((short *)&y1r)[re];
      *stream0_out++ = ((short *)&y2r)[re];
      *stream0_out++ = ((short *)&y0i)[re];
      *stream0_out++ = ((short *)&y1i)[re];
      *stream0_out++ = ((short *)&y2i)[re];
    }
  }
#else
  simde__m256i *rho01_256i = (simde__m256i *)rho01;
  simde__m256i *stream0_256i_in = (simde__m256i *)stream0_in;
  simde__m256i *stream1_256i_in = (simde__m256i *)stream1_in;
  simde__m256i *ch_mag_256i = (simde__m256i *)ch_mag;
  simde__m256i *ch_mag_256i_i = (simde__m256i *)ch_mag_i;

  simde__m256i ONE_OVER_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(10112)); // round(1/sqrt(42)*2^16)
  simde__m256i THREE_OVER_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(30337)); // round(3/sqrt(42)*2^16)
  simde__m256i FIVE_OVER_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(25281)); // round(5/sqrt(42)*2^15)
  simde__m256i SEVEN_OVER_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(17697)); // round(7/sqrt(42)*2^14) Q2.14
  simde__m256i ONE_OVER_SQRT_2 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(23170)); // round(1/sqrt(2)*2^15)
  simde__m256i ONE_OVER_SQRT_2_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(3575)); // round(1/sqrt(2*42)*2^15)
  simde__m256i THREE_OVER_SQRT_2_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(10726)); // round(3/sqrt(2*42)*2^15)
  simde__m256i FIVE_OVER_SQRT_2_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(17876)); // round(5/sqrt(2*42)*2^15)
  simde__m256i SEVEN_OVER_SQRT_2_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(25027)); // round(7/sqrt(2*42)*2^15)
  simde__m256i FORTYNINE_OVER_FOUR_SQRT_42 =
      simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(30969)); // round(49/(4*sqrt(42))*2^14), Q2.14
  simde__m256i THIRTYSEVEN_OVER_FOUR_SQRT_42 =
      simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(23385)); // round(37/(4*sqrt(42))*2^14), Q2.14
  simde__m256i TWENTYFIVE_OVER_FOUR_SQRT_42 =
      simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(31601)); // round(25/(4*sqrt(42))*2^15)
  simde__m256i TWENTYNINE_OVER_FOUR_SQRT_42 =
      simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(18329)); // round(29/(4*sqrt(42))*2^15), Q2.14
  simde__m256i SEVENTEEN_OVER_FOUR_SQRT_42 =
      simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(21489)); // round(17/(4*sqrt(42))*2^15)
  simde__m256i NINE_OVER_FOUR_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(11376)); // round(9/(4*sqrt(42))*2^15)
  simde__m256i THIRTEEN_OVER_FOUR_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(16433)); // round(13/(4*sqrt(42))*2^15)
  simde__m256i FIVE_OVER_FOUR_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(6320)); // round(5/(4*sqrt(42))*2^15)
  simde__m256i ONE_OVER_FOUR_SQRT_42 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(1264)); // round(1/(4*sqrt(42))*2^15)
  simde__m256i SQRT_42_OVER_FOUR = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(13272)); // round(sqrt(42)/4*2^13), Q3.12

  simde__m256i ch_mag_des;
  simde__m256i ch_mag_int;
  simde__m256i y0r_one_over_sqrt_21;
  simde__m256i y0r_three_over_sqrt_21;
  simde__m256i y0r_five_over_sqrt_21;
  simde__m256i y0r_seven_over_sqrt_21;
  simde__m256i y0i_one_over_sqrt_21;
  simde__m256i y0i_three_over_sqrt_21;
  simde__m256i y0i_five_over_sqrt_21;
  simde__m256i y0i_seven_over_sqrt_21;
  simde__m256i ch_mag_int_with_sigma2;
  simde__m256i two_ch_mag_int_with_sigma2;
  simde__m256i three_ch_mag_int_with_sigma2;

  uint32_t len256 = (length + 7) >> 3;

  for (int i = 0; i < len256; i += 2) {
    // Get rho
    simde__m256i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8;
    oai_mm256_separate_real_imag_parts(&xmm2, &xmm3, rho01_256i[i], rho01_256i[i + 1]);

    simde__m256i rho_rpi = simde_mm256_adds_epi16(xmm2, xmm3); // rho = Re(rho) + Im(rho)
    simde__m256i rho_rmi = simde_mm256_subs_epi16(xmm2, xmm3); // rho* = Re(rho) - Im(rho)

    // Compute the different rhos
    simde__m256i rho_rs[32];
    rho_rs[27] = simde_mm256_mulhi_epi16(rho_rpi, ONE_OVER_SQRT_42);
    rho_rs[28] = simde_mm256_mulhi_epi16(rho_rmi, ONE_OVER_SQRT_42);
    rho_rs[18] = simde_mm256_mulhi_epi16(rho_rpi, THREE_OVER_SQRT_42);
    rho_rs[21] = simde_mm256_mulhi_epi16(rho_rmi, THREE_OVER_SQRT_42);
    rho_rs[9] = simde_mm256_mulhi_epi16(rho_rpi, FIVE_OVER_SQRT_42);
    rho_rs[14] = simde_mm256_mulhi_epi16(rho_rmi, FIVE_OVER_SQRT_42);
    rho_rs[0] = simde_mm256_mulhi_epi16(rho_rpi, SEVEN_OVER_SQRT_42);
    rho_rs[7] = simde_mm256_mulhi_epi16(rho_rmi, SEVEN_OVER_SQRT_42);

    rho_rs[9] = simde_mm256_slli_epi16(rho_rs[9], 1);
    rho_rs[14] = simde_mm256_slli_epi16(rho_rs[14], 1);
    rho_rs[0] = simde_mm256_slli_epi16(rho_rs[0], 2);
    rho_rs[7] = simde_mm256_slli_epi16(rho_rs[7], 2);

    xmm4 = simde_mm256_mulhi_epi16(xmm2, ONE_OVER_SQRT_42);
    xmm5 = simde_mm256_mulhi_epi16(xmm3, ONE_OVER_SQRT_42);
    xmm6 = simde_mm256_mulhi_epi16(xmm3, THREE_OVER_SQRT_42);
    xmm7 = simde_mm256_mulhi_epi16(xmm3, FIVE_OVER_SQRT_42);
    xmm8 = simde_mm256_mulhi_epi16(xmm3, SEVEN_OVER_SQRT_42);
    xmm7 = simde_mm256_slli_epi16(xmm7, 1);
    xmm8 = simde_mm256_slli_epi16(xmm8, 2);

    rho_rs[26] = simde_mm256_adds_epi16(xmm4, xmm6);
    rho_rs[29] = simde_mm256_subs_epi16(xmm4, xmm6);
    rho_rs[25] = simde_mm256_adds_epi16(xmm4, xmm7);
    rho_rs[30] = simde_mm256_subs_epi16(xmm4, xmm7);
    rho_rs[24] = simde_mm256_adds_epi16(xmm4, xmm8);
    rho_rs[31] = simde_mm256_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm256_mulhi_epi16(xmm2, THREE_OVER_SQRT_42);
    rho_rs[19] = simde_mm256_adds_epi16(xmm4, xmm5);
    rho_rs[20] = simde_mm256_subs_epi16(xmm4, xmm5);
    rho_rs[17] = simde_mm256_adds_epi16(xmm4, xmm7);
    rho_rs[22] = simde_mm256_subs_epi16(xmm4, xmm7);
    rho_rs[16] = simde_mm256_adds_epi16(xmm4, xmm8);
    rho_rs[23] = simde_mm256_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm256_mulhi_epi16(xmm2, FIVE_OVER_SQRT_42);
    xmm4 = simde_mm256_slli_epi16(xmm4, 1);
    rho_rs[11] = simde_mm256_adds_epi16(xmm4, xmm5);
    rho_rs[12] = simde_mm256_subs_epi16(xmm4, xmm5);
    rho_rs[10] = simde_mm256_adds_epi16(xmm4, xmm6);
    rho_rs[13] = simde_mm256_subs_epi16(xmm4, xmm6);
    rho_rs[8] = simde_mm256_adds_epi16(xmm4, xmm8);
    rho_rs[15] = simde_mm256_subs_epi16(xmm4, xmm8);

    xmm4 = simde_mm256_mulhi_epi16(xmm2, SEVEN_OVER_SQRT_42);
    xmm4 = simde_mm256_slli_epi16(xmm4, 2);
    rho_rs[3] = simde_mm256_adds_epi16(xmm4, xmm5);
    rho_rs[4] = simde_mm256_subs_epi16(xmm4, xmm5);
    rho_rs[2] = simde_mm256_adds_epi16(xmm4, xmm6);
    rho_rs[5] = simde_mm256_subs_epi16(xmm4, xmm6);
    rho_rs[1] = simde_mm256_adds_epi16(xmm4, xmm7);
    rho_rs[6] = simde_mm256_subs_epi16(xmm4, xmm7);

    // Rearrange interfering MF output
    simde__m256i y1r, y1i;
    oai_mm256_separate_real_imag_parts(&y1r, &y1i, stream1_256i_in[i], stream1_256i_in[i + 1]);

    // Psi_r calculation from rho_rpi or rho_rmi
    xmm0 = simde_mm256_broadcastw_epi16(simde_mm_set1_epi16(0)); // ZERO for abs_pi16
    xmm2 = simde_mm256_subs_epi16(rho_rs[0], y1r);

    simde__m256i psi_r_s[64];
    for (int j = 0; j < 32; j++) // psi_r_s[0~31], rho_rs[0~31]
      psi_r_s[j] = simde_mm256_abs_epi16(simde_mm256_subs_epi16(rho_rs[j], y1r));
    for (int j = 32; j < 64; j++) // psi_r_s[32~64], rho_rs[31~0]
      psi_r_s[j] = simde_mm256_abs_epi16(simde_mm256_adds_epi16(rho_rs[63 - j], y1r));

    // simde__m256i psi_i calculation from rho_rpi or rho_rmi
    simde__m256i psi_i_s[64];
    const uint8_t rho_rs_index[32] = {7, 15, 23, 31, 24, 16, 8,  0, 6, 14, 22, 30, 25, 17, 9,  1,
                                      5, 13, 21, 29, 26, 18, 10, 2, 4, 12, 20, 28, 27, 19, 11, 3};
    for (int k = 0; k < 32; k += 8) { // psi_i_s[0~31]
      for (int j = k; j < k + 4; j++)
        psi_i_s[j] = simde_mm256_abs_epi16(simde_mm256_subs_epi16(rho_rs[rho_rs_index[j]], y1i));
      for (int j = k + 4; j < k + 8; j++)
        psi_i_s[j] = simde_mm256_abs_epi16(simde_mm256_adds_epi16(rho_rs[rho_rs_index[j]], y1i));
    }
    for (int k = 32; k < 64; k += 8) { // psi_i_s[32~64]
      for (int j = k; j < k + 4; j++)
        psi_i_s[j] = simde_mm256_abs_epi16(simde_mm256_subs_epi16(rho_rs[rho_rs_index[63 - j]], y1i));
      for (int j = k + 4; j < k + 8; j++)
        psi_i_s[j] = simde_mm256_abs_epi16(simde_mm256_adds_epi16(rho_rs[rho_rs_index[63 - j]], y1i));
    }

    // Rearrange desired MF output
    simde__m256i y0r, y0i;
    oai_mm256_separate_real_imag_parts(&y0r, &y0i, stream0_256i_in[i], stream0_256i_in[i + 1]);

    // Rearrange desired channel magnitudes
    // [|h|^2(1),|h|^2(1),|h|^2(2),|h|^2(2),...,,|h|^2(7),|h|^2(7)]*(2/sqrt(10))
    // xmm2 is dummy variable that contains the same values as ch_mag_des
    oai_mm256_separate_real_imag_parts(&ch_mag_des, &xmm2, ch_mag_256i[i], ch_mag_256i[i + 1]);

    // Rearrange interfering channel magnitudes
    oai_mm256_separate_real_imag_parts(&ch_mag_int, &xmm2, ch_mag_256i_i[i], ch_mag_256i_i[i + 1]);

    y0r_one_over_sqrt_21 = simde_mm256_mulhi_epi16(y0r, ONE_OVER_SQRT_42);
    y0r_three_over_sqrt_21 = simde_mm256_mulhi_epi16(y0r, THREE_OVER_SQRT_42);
    y0r_five_over_sqrt_21 = simde_mm256_mulhi_epi16(y0r, FIVE_OVER_SQRT_42);
    y0r_five_over_sqrt_21 = simde_mm256_slli_epi16(y0r_five_over_sqrt_21, 1);
    y0r_seven_over_sqrt_21 = simde_mm256_mulhi_epi16(y0r, SEVEN_OVER_SQRT_42);
    y0r_seven_over_sqrt_21 = simde_mm256_slli_epi16(y0r_seven_over_sqrt_21, 2); // Q2.14

    y0i_one_over_sqrt_21 = simde_mm256_mulhi_epi16(y0i, ONE_OVER_SQRT_42);
    y0i_three_over_sqrt_21 = simde_mm256_mulhi_epi16(y0i, THREE_OVER_SQRT_42);
    y0i_five_over_sqrt_21 = simde_mm256_mulhi_epi16(y0i, FIVE_OVER_SQRT_42);
    y0i_five_over_sqrt_21 = simde_mm256_slli_epi16(y0i_five_over_sqrt_21, 1);
    y0i_seven_over_sqrt_21 = simde_mm256_mulhi_epi16(y0i, SEVEN_OVER_SQRT_42);
    y0i_seven_over_sqrt_21 = simde_mm256_slli_epi16(y0i_seven_over_sqrt_21, 2); // Q2.14

    simde__m256i y0_s[64];
    const simde__m256i y0r_over_s[8] = {y0r_seven_over_sqrt_21,
                                        y0r_five_over_sqrt_21,
                                        y0r_three_over_sqrt_21,
                                        y0r_one_over_sqrt_21};
    for (int j = 0; j < 32; j += 8) {
      y0_s[j + 0] = simde_mm256_adds_epi16(y0r_over_s[j >> 3], y0i_seven_over_sqrt_21);
      y0_s[j + 1] = simde_mm256_adds_epi16(y0r_over_s[j >> 3], y0i_five_over_sqrt_21);
      y0_s[j + 2] = simde_mm256_adds_epi16(y0r_over_s[j >> 3], y0i_three_over_sqrt_21);
      y0_s[j + 3] = simde_mm256_adds_epi16(y0r_over_s[j >> 3], y0i_one_over_sqrt_21);
      y0_s[j + 4] = simde_mm256_subs_epi16(y0r_over_s[j >> 3], y0i_one_over_sqrt_21);
      y0_s[j + 5] = simde_mm256_subs_epi16(y0r_over_s[j >> 3], y0i_three_over_sqrt_21);
      y0_s[j + 6] = simde_mm256_subs_epi16(y0r_over_s[j >> 3], y0i_five_over_sqrt_21);
      y0_s[j + 7] = simde_mm256_subs_epi16(y0r_over_s[j >> 3], y0i_seven_over_sqrt_21);
    }

    ch_mag_int_with_sigma2 = simde_mm256_srai_epi16(ch_mag_int, 1); // *2
    two_ch_mag_int_with_sigma2 = ch_mag_int; // *4
    three_ch_mag_int_with_sigma2 = simde_mm256_adds_epi16(ch_mag_int_with_sigma2, two_ch_mag_int_with_sigma2); // *6
    simde__m256i a_r_s[64];
    simde__m256i a_i_s[64];
    simde__m256i psi_a_s[64];
    simde__m256i a_sq_s[64];
    for (int j = 0; j < 64; j++) {
      // Detection of interference term
      a_r_s[j] = interference_abs_64qam_epi16_256(psi_r_s[j],
                                                  ch_mag_int_with_sigma2,
                                                  two_ch_mag_int_with_sigma2,
                                                  three_ch_mag_int_with_sigma2,
                                                  ONE_OVER_SQRT_2_42,
                                                  THREE_OVER_SQRT_2_42,
                                                  FIVE_OVER_SQRT_2_42,
                                                  SEVEN_OVER_SQRT_2_42);
      a_i_s[j] = interference_abs_64qam_epi16_256(psi_i_s[j],
                                                  ch_mag_int_with_sigma2,
                                                  two_ch_mag_int_with_sigma2,
                                                  three_ch_mag_int_with_sigma2,
                                                  ONE_OVER_SQRT_2_42,
                                                  THREE_OVER_SQRT_2_42,
                                                  FIVE_OVER_SQRT_2_42,
                                                  SEVEN_OVER_SQRT_2_42);

      // Calculation of a group of two terms in the bit metric involving product of psi and interference
      psi_a_s[j] = prodsum_psi_a_epi16_256(psi_r_s[j], a_r_s[j], psi_i_s[j], a_i_s[j]);

      // Multiply by sqrt(2)
      psi_a_s[j] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(psi_a_s[j], ONE_OVER_SQRT_2), 2);

      // Calculation of a group of two terms in the bit metric involving squares of interference
      a_sq_s[j] = square_a_64qam_epi16_256(a_r_s[j], a_i_s[j], ch_mag_int, SQRT_42_OVER_FOUR);
    }

    // Computing different multiples of ||h0||^2
    simde__m256i ch_mag_with_sigma2[10];
    enum ch_mag_over_42with_sigma2_vals { mag2 = 0, mag10, mag26, mag18, mag34, mag58, mag50, mag74, mag98 };
    // x=1, y=1
    ch_mag_with_sigma2[mag2] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, ONE_OVER_FOUR_SQRT_42), 1);
    // x=1, y=3
    ch_mag_with_sigma2[mag10] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, FIVE_OVER_FOUR_SQRT_42), 1);
    // x=1, x=5
    ch_mag_with_sigma2[mag26] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, THIRTEEN_OVER_FOUR_SQRT_42), 1);
    // x=1, y=7
    ch_mag_with_sigma2[mag50] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, TWENTYFIVE_OVER_FOUR_SQRT_42), 1);
    // x=3, y=3
    ch_mag_with_sigma2[mag18] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, NINE_OVER_FOUR_SQRT_42), 1);
    // x=3, y=5
    ch_mag_with_sigma2[mag34] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, SEVENTEEN_OVER_FOUR_SQRT_42), 1);
    // x=3, y=7
    ch_mag_with_sigma2[mag58] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, TWENTYNINE_OVER_FOUR_SQRT_42), 2);
    // x=5, y=5
    ch_mag_with_sigma2[mag50] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, TWENTYFIVE_OVER_FOUR_SQRT_42), 1);
    // x=5, y=7
    ch_mag_with_sigma2[mag74] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, THIRTYSEVEN_OVER_FOUR_SQRT_42), 2);
    // x=7, y=7
    ch_mag_with_sigma2[mag98] = simde_mm256_slli_epi16(simde_mm256_mulhi_epi16(ch_mag_des, FORTYNINE_OVER_FOUR_SQRT_42), 2);

    // Computing Metrics
    simde__m256i bit_met_s[64];
    const enum ch_mag_over_42with_sigma2_vals table[] = {
        mag98, mag74, mag58, mag50, mag50, mag58, mag74, mag98, mag74, mag50, mag34, mag26, mag26, mag34, mag50, mag74,
        mag58, mag34, mag18, mag10, mag10, mag18, mag34, mag58, mag50, mag26, mag10, mag2,  mag2,  mag10, mag26, mag50};

    for (int i = 0; i < 32; i++) {
      const simde__m256i x = simde_mm256_adds_epi16(simde_mm256_subs_epi16(psi_a_s[i], a_sq_s[i]), y0_s[i]);
      bit_met_s[i] = simde_mm256_subs_epi16(x, ch_mag_with_sigma2[table[i]]);
    }
    for (int i = 0; i < 32; i++) {
      const simde__m256i x = simde_mm256_subs_epi16(simde_mm256_subs_epi16(psi_a_s[32 + i], a_sq_s[32 + i]), y0_s[31 - i]);
      bit_met_s[32 + i] = simde_mm256_subs_epi16(x, ch_mag_with_sigma2[table[31 - i]]);
    }

    // Detection for bits
    simde__m256i logmax_den_re0;
    simde__m256i logmax_num_re0;
    // Detection for 1st bit
    // bit = 1
    xmm0 = max_epi16_256(bit_met_s[56],
                         bit_met_s[57],
                         bit_met_s[58],
                         bit_met_s[59],
                         bit_met_s[60],
                         bit_met_s[61],
                         bit_met_s[62],
                         bit_met_s[63]);
    xmm1 = max_epi16_256(bit_met_s[48],
                         bit_met_s[49],
                         bit_met_s[50],
                         bit_met_s[51],
                         bit_met_s[52],
                         bit_met_s[53],
                         bit_met_s[54],
                         bit_met_s[55]);
    xmm2 = max_epi16_256(bit_met_s[40],
                         bit_met_s[41],
                         bit_met_s[42],
                         bit_met_s[43],
                         bit_met_s[44],
                         bit_met_s[45],
                         bit_met_s[46],
                         bit_met_s[47]);
    xmm3 = max_epi16_256(bit_met_s[32],
                         bit_met_s[33],
                         bit_met_s[34],
                         bit_met_s[35],
                         bit_met_s[36],
                         bit_met_s[37],
                         bit_met_s[38],
                         bit_met_s[39]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    // bit = 0
    xmm0 = max_epi16_256(bit_met_s[0],
                         bit_met_s[1],
                         bit_met_s[2],
                         bit_met_s[3],
                         bit_met_s[4],
                         bit_met_s[5],
                         bit_met_s[6],
                         bit_met_s[7]);
    xmm1 = max_epi16_256(bit_met_s[8],
                         bit_met_s[9],
                         bit_met_s[10],
                         bit_met_s[11],
                         bit_met_s[12],
                         bit_met_s[13],
                         bit_met_s[14],
                         bit_met_s[15]);
    xmm2 = max_epi16_256(bit_met_s[16],
                         bit_met_s[17],
                         bit_met_s[18],
                         bit_met_s[19],
                         bit_met_s[20],
                         bit_met_s[21],
                         bit_met_s[22],
                         bit_met_s[23]);
    xmm3 = max_epi16_256(bit_met_s[24],
                         bit_met_s[25],
                         bit_met_s[26],
                         bit_met_s[27],
                         bit_met_s[28],
                         bit_met_s[29],
                         bit_met_s[30],
                         bit_met_s[31]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    y0r = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 2nd bit
    // bit = 1
    xmm0 = max_epi16_256(bit_met_s[4],
                         bit_met_s[12],
                         bit_met_s[20],
                         bit_met_s[28],
                         bit_met_s[36],
                         bit_met_s[44],
                         bit_met_s[52],
                         bit_met_s[60]);
    xmm1 = max_epi16_256(bit_met_s[5],
                         bit_met_s[13],
                         bit_met_s[21],
                         bit_met_s[29],
                         bit_met_s[37],
                         bit_met_s[45],
                         bit_met_s[53],
                         bit_met_s[61]);
    xmm2 = max_epi16_256(bit_met_s[6],
                         bit_met_s[14],
                         bit_met_s[22],
                         bit_met_s[30],
                         bit_met_s[38],
                         bit_met_s[46],
                         bit_met_s[54],
                         bit_met_s[62]);
    xmm3 = max_epi16_256(bit_met_s[7],
                         bit_met_s[15],
                         bit_met_s[23],
                         bit_met_s[31],
                         bit_met_s[39],
                         bit_met_s[47],
                         bit_met_s[55],
                         bit_met_s[63]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    // bit = 0
    xmm0 = max_epi16_256(bit_met_s[3],
                         bit_met_s[11],
                         bit_met_s[19],
                         bit_met_s[27],
                         bit_met_s[35],
                         bit_met_s[43],
                         bit_met_s[51],
                         bit_met_s[59]);
    xmm1 = max_epi16_256(bit_met_s[2],
                         bit_met_s[10],
                         bit_met_s[18],
                         bit_met_s[26],
                         bit_met_s[34],
                         bit_met_s[42],
                         bit_met_s[50],
                         bit_met_s[58]);
    xmm2 = max_epi16_256(bit_met_s[1],
                         bit_met_s[9],
                         bit_met_s[17],
                         bit_met_s[25],
                         bit_met_s[33],
                         bit_met_s[41],
                         bit_met_s[49],
                         bit_met_s[57]);
    xmm3 = max_epi16_256(bit_met_s[0],
                         bit_met_s[8],
                         bit_met_s[16],
                         bit_met_s[24],
                         bit_met_s[32],
                         bit_met_s[40],
                         bit_met_s[48],
                         bit_met_s[56]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    y1r = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 3rd bit
    xmm0 = max_epi16_256(bit_met_s[63],
                         bit_met_s[62],
                         bit_met_s[61],
                         bit_met_s[60],
                         bit_met_s[59],
                         bit_met_s[58],
                         bit_met_s[57],
                         bit_met_s[56]);
    xmm1 = max_epi16_256(bit_met_s[55],
                         bit_met_s[54],
                         bit_met_s[53],
                         bit_met_s[52],
                         bit_met_s[51],
                         bit_met_s[50],
                         bit_met_s[49],
                         bit_met_s[48]);
    xmm2 = max_epi16_256(bit_met_s[15],
                         bit_met_s[14],
                         bit_met_s[13],
                         bit_met_s[12],
                         bit_met_s[11],
                         bit_met_s[10],
                         bit_met_s[9],
                         bit_met_s[8]);
    xmm3 = max_epi16_256(bit_met_s[7],
                         bit_met_s[6],
                         bit_met_s[5],
                         bit_met_s[4],
                         bit_met_s[3],
                         bit_met_s[2],
                         bit_met_s[1],
                         bit_met_s[0]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16_256(bit_met_s[47],
                         bit_met_s[46],
                         bit_met_s[45],
                         bit_met_s[44],
                         bit_met_s[43],
                         bit_met_s[42],
                         bit_met_s[41],
                         bit_met_s[40]);
    xmm1 = max_epi16_256(bit_met_s[39],
                         bit_met_s[38],
                         bit_met_s[37],
                         bit_met_s[36],
                         bit_met_s[35],
                         bit_met_s[34],
                         bit_met_s[33],
                         bit_met_s[32]);
    xmm2 = max_epi16_256(bit_met_s[31],
                         bit_met_s[30],
                         bit_met_s[29],
                         bit_met_s[28],
                         bit_met_s[27],
                         bit_met_s[26],
                         bit_met_s[25],
                         bit_met_s[24]);
    xmm3 = max_epi16_256(bit_met_s[23],
                         bit_met_s[22],
                         bit_met_s[21],
                         bit_met_s[20],
                         bit_met_s[19],
                         bit_met_s[18],
                         bit_met_s[17],
                         bit_met_s[16]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    simde__m256i y2r = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 4th bit
    xmm0 = max_epi16_256(bit_met_s[0],
                         bit_met_s[8],
                         bit_met_s[16],
                         bit_met_s[24],
                         bit_met_s[32],
                         bit_met_s[40],
                         bit_met_s[48],
                         bit_met_s[56]);
    xmm1 = max_epi16_256(bit_met_s[1],
                         bit_met_s[9],
                         bit_met_s[17],
                         bit_met_s[25],
                         bit_met_s[33],
                         bit_met_s[41],
                         bit_met_s[49],
                         bit_met_s[57]);
    xmm2 = max_epi16_256(bit_met_s[6],
                         bit_met_s[14],
                         bit_met_s[22],
                         bit_met_s[30],
                         bit_met_s[38],
                         bit_met_s[46],
                         bit_met_s[54],
                         bit_met_s[62]);
    xmm3 = max_epi16_256(bit_met_s[7],
                         bit_met_s[15],
                         bit_met_s[23],
                         bit_met_s[31],
                         bit_met_s[39],
                         bit_met_s[47],
                         bit_met_s[55],
                         bit_met_s[63]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16_256(bit_met_s[4],
                         bit_met_s[12],
                         bit_met_s[20],
                         bit_met_s[28],
                         bit_met_s[36],
                         bit_met_s[44],
                         bit_met_s[52],
                         bit_met_s[60]);
    xmm1 = max_epi16_256(bit_met_s[5],
                         bit_met_s[13],
                         bit_met_s[21],
                         bit_met_s[29],
                         bit_met_s[37],
                         bit_met_s[45],
                         bit_met_s[53],
                         bit_met_s[61]);
    xmm2 = max_epi16_256(bit_met_s[3],
                         bit_met_s[11],
                         bit_met_s[19],
                         bit_met_s[27],
                         bit_met_s[35],
                         bit_met_s[43],
                         bit_met_s[51],
                         bit_met_s[59]);
    xmm3 = max_epi16_256(bit_met_s[2],
                         bit_met_s[10],
                         bit_met_s[18],
                         bit_met_s[26],
                         bit_met_s[34],
                         bit_met_s[42],
                         bit_met_s[50],
                         bit_met_s[58]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    y0i = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 5th bit
    xmm0 = max_epi16_256(bit_met_s[63],
                         bit_met_s[62],
                         bit_met_s[61],
                         bit_met_s[60],
                         bit_met_s[59],
                         bit_met_s[58],
                         bit_met_s[57],
                         bit_met_s[56]);
    xmm1 = max_epi16_256(bit_met_s[39],
                         bit_met_s[38],
                         bit_met_s[37],
                         bit_met_s[36],
                         bit_met_s[35],
                         bit_met_s[34],
                         bit_met_s[33],
                         bit_met_s[32]);
    xmm2 = max_epi16_256(bit_met_s[31],
                         bit_met_s[30],
                         bit_met_s[29],
                         bit_met_s[28],
                         bit_met_s[27],
                         bit_met_s[26],
                         bit_met_s[25],
                         bit_met_s[24]);
    xmm3 = max_epi16_256(bit_met_s[7],
                         bit_met_s[6],
                         bit_met_s[5],
                         bit_met_s[4],
                         bit_met_s[3],
                         bit_met_s[2],
                         bit_met_s[1],
                         bit_met_s[0]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16_256(bit_met_s[55],
                         bit_met_s[54],
                         bit_met_s[53],
                         bit_met_s[52],
                         bit_met_s[51],
                         bit_met_s[50],
                         bit_met_s[49],
                         bit_met_s[48]);
    xmm1 = max_epi16_256(bit_met_s[47],
                         bit_met_s[46],
                         bit_met_s[45],
                         bit_met_s[44],
                         bit_met_s[43],
                         bit_met_s[42],
                         bit_met_s[41],
                         bit_met_s[40]);
    xmm2 = max_epi16_256(bit_met_s[23],
                         bit_met_s[22],
                         bit_met_s[21],
                         bit_met_s[20],
                         bit_met_s[19],
                         bit_met_s[18],
                         bit_met_s[17],
                         bit_met_s[16]);
    xmm3 = max_epi16_256(bit_met_s[15],
                         bit_met_s[14],
                         bit_met_s[13],
                         bit_met_s[12],
                         bit_met_s[11],
                         bit_met_s[10],
                         bit_met_s[9],
                         bit_met_s[8]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    y1i = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Detection for 6th bit
    xmm0 = max_epi16_256(bit_met_s[0],
                         bit_met_s[8],
                         bit_met_s[16],
                         bit_met_s[24],
                         bit_met_s[32],
                         bit_met_s[40],
                         bit_met_s[48],
                         bit_met_s[56]);
    xmm1 = max_epi16_256(bit_met_s[3],
                         bit_met_s[11],
                         bit_met_s[19],
                         bit_met_s[27],
                         bit_met_s[35],
                         bit_met_s[43],
                         bit_met_s[51],
                         bit_met_s[59]);
    xmm2 = max_epi16_256(bit_met_s[4],
                         bit_met_s[12],
                         bit_met_s[20],
                         bit_met_s[28],
                         bit_met_s[36],
                         bit_met_s[44],
                         bit_met_s[52],
                         bit_met_s[60]);
    xmm3 = max_epi16_256(bit_met_s[7],
                         bit_met_s[15],
                         bit_met_s[23],
                         bit_met_s[31],
                         bit_met_s[39],
                         bit_met_s[47],
                         bit_met_s[55],
                         bit_met_s[63]);
    logmax_den_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    xmm0 = max_epi16_256(bit_met_s[6],
                         bit_met_s[14],
                         bit_met_s[22],
                         bit_met_s[30],
                         bit_met_s[38],
                         bit_met_s[46],
                         bit_met_s[54],
                         bit_met_s[62]);
    xmm1 = max_epi16_256(bit_met_s[5],
                         bit_met_s[13],
                         bit_met_s[21],
                         bit_met_s[29],
                         bit_met_s[37],
                         bit_met_s[45],
                         bit_met_s[53],
                         bit_met_s[61]);
    xmm2 = max_epi16_256(bit_met_s[2],
                         bit_met_s[10],
                         bit_met_s[18],
                         bit_met_s[26],
                         bit_met_s[34],
                         bit_met_s[42],
                         bit_met_s[50],
                         bit_met_s[58]);
    xmm3 = max_epi16_256(bit_met_s[1],
                         bit_met_s[9],
                         bit_met_s[17],
                         bit_met_s[25],
                         bit_met_s[33],
                         bit_met_s[41],
                         bit_met_s[49],
                         bit_met_s[57]);
    logmax_num_re0 = simde_mm256_max_epi16(simde_mm256_max_epi16(xmm0, xmm1), simde_mm256_max_epi16(xmm2, xmm3));

    simde__m256i y2i = simde_mm256_subs_epi16(logmax_num_re0, logmax_den_re0);

    // Map to output stream, difficult to do in SIMD since we have 6 16bit LLRs
    for (int re = 0; re < 16; re++) {
      *stream0_out++ = ((short *)&y0r)[re];
      *stream0_out++ = ((short *)&y1r)[re];
      *stream0_out++ = ((short *)&y2r)[re];
      *stream0_out++ = ((short *)&y0i)[re];
      *stream0_out++ = ((short *)&y1i)[re];
      *stream0_out++ = ((short *)&y2i)[re];
    }
  }
#endif
}

static void nr_ml_llr_shift(int16_t *llr_layer0, int16_t *llr_layer1, uint32_t nb_re, int shift)
{
  const uint32_t n = 2 * nb_re; // QPSK: 2 LLRs per RE
  uint32_t i = 0;
  for (; i + 8 <= n; i += 8) {
    simde__m128i *l0 = (simde__m128i *)&llr_layer0[i];
    simde__m128i *l1 = (simde__m128i *)&llr_layer1[i];
    simde_mm_storeu_si128(l0, simde_mm_srai_epi16(simde_mm_loadu_si128(l0), shift));
    simde_mm_storeu_si128(l1, simde_mm_srai_epi16(simde_mm_loadu_si128(l1), shift));
  }
  for (; i < n; i++) {
    llr_layer0[i] >>= shift;
    llr_layer1[i] >>= shift;
  }
}

void nr_compute_ML_llr(c16_t *rxdataF_comp0,
                       c16_t *rxdataF_comp1,
                       c16_t *ch_mag0,
                       c16_t *ch_mag1,
                       int16_t *llr_layers0,
                       int16_t *llr_layers1,
                       c16_t *rho0,
                       c16_t *rho1,
                       uint32_t nb_re,
                       uint8_t mod_order)
{
  switch (mod_order) {
    case 2:
      nr_qpsk_llr_2layer(rxdataF_comp0, rxdataF_comp1, llr_layers0, rho0, nb_re);
      nr_qpsk_llr_2layer(rxdataF_comp1, rxdataF_comp0, llr_layers1, rho1, nb_re);
      nr_ml_llr_shift((int16_t *)llr_layers0, (int16_t *)llr_layers1, nb_re, 4);
      break;
    case 4:
      nr_qam16_llr_2layer(rxdataF_comp0, rxdataF_comp1, ch_mag0, ch_mag1, llr_layers0, rho0, nb_re);
      nr_qam16_llr_2layer(rxdataF_comp1, rxdataF_comp0, ch_mag1, ch_mag0, llr_layers1, rho1, nb_re);
      break;
    case 6:
      nr_qam64_llr_2layer(rxdataF_comp0, rxdataF_comp1, ch_mag0, ch_mag1, llr_layers0, rho0, nb_re);
      nr_qam64_llr_2layer(rxdataF_comp1, rxdataF_comp0, ch_mag1, ch_mag0, llr_layers1, rho1, nb_re);
      break;
    default:
      AssertFatal(1 == 0, "nr_compute_ML_llr: invalid mod_order, Qm = %d\n", mod_order);
  }
}
// Zero Forcing Rx function: nr_det_HhH()
static void nr_det_HhH(c16_t *after_mf_00, // a
                       c16_t *after_mf_01, // b
                       c16_t *after_mf_10, // c
                       c16_t *after_mf_11, // d
                       uint32_t *det_fin, // 1/ad-bc
                       unsigned short nb_rb)
{
  simde__m128i *after_mf_00_128, *after_mf_01_128, *after_mf_10_128, *after_mf_11_128, ad_re_128, bc_re_128; // ad_im_128,
                                                                                                             // bc_im_128;
  simde__m128i *det_fin_128, det_re_128; // det_im_128, tmp_det0, tmp_det1;

  after_mf_00_128 = (simde__m128i *)after_mf_00;
  after_mf_01_128 = (simde__m128i *)after_mf_01;
  after_mf_10_128 = (simde__m128i *)after_mf_10;
  after_mf_11_128 = (simde__m128i *)after_mf_11;

  det_fin_128 = (simde__m128i *)det_fin;

  for (unsigned short rb = 0; rb < 3 * nb_rb; rb++) {
    // complex multiplication (I_a+jQ_a)(I_d+jQ_d) = (I_aI_d - Q_aQ_d) + j(Q_aI_d + I_aQ_d)
    // The imag part is often zero, we compute only the real part
    ad_re_128 = simde_mm_madd_epi16(oai_mm_conj(after_mf_00_128[0]), after_mf_11_128[0]); // Re: I_a0*I_d0 - Q_a1*Q_d1
    // ad_im_128 = simde_mm_madd_epi16(oai_mm_swap(after_mf_00_128[0]),after_mf_11_128[0]);//Im: (Q_aI_d + I_aQ_d)

    // complex multiplication (I_b+jQ_b)(I_c+jQ_c) = (I_bI_c - Q_bQ_c) + j(Q_bI_c + I_bQ_c)
    // The imag part is often zero, we compute only the real part
    bc_re_128 = simde_mm_madd_epi16(oai_mm_conj(after_mf_01_128[0]), after_mf_10_128[0]); // Re: I_b0*I_c0 - Q_b1*Q_c1
    // bc_im_128 = simde_mm_madd_epi16(oai_mm_swap(after_mf_01_128[0]),after_mf_10_128[0]);//Im: (Q_bI_c + I_bQ_c)

    det_re_128 = simde_mm_sub_epi32(ad_re_128, bc_re_128);
    // det_im_128 = simde_mm_sub_epi32(ad_im_128, bc_im_128);

    // det in Q30 format
    det_fin_128[0] = simde_mm_abs_epi32(det_re_128);

#ifdef DEBUG_DLSCH_DEMOD
    printf("\n Computing det_HhH_inv \n");
    // print_ints("det_re_128:",(int32_t*)&det_re_128);
    // print_ints("det_im_128:",(int32_t*)&det_im_128);
    print_ints("det_fin_128:", (int32_t *)&det_fin_128[0]);
#endif
    det_fin_128 += 1;
    after_mf_00_128 += 1;
    after_mf_01_128 += 1;
    after_mf_10_128 += 1;
    after_mf_11_128 += 1;
  }
}

/* Zero Forcing Rx function: nr_conjch0_mult_ch1()
 *
 *
 * */
// TODO: This function is just a wrapper, can be removed.
static void nr_conjch0_mult_ch1(c16_t *ch0, c16_t *ch1, c16_t *ch0conj_ch1, unsigned short nb_rb, unsigned char output_shift0)
{
  // This function is used to compute multiplications in H_hermitian * H matrix
  mult_cpx_conj_vector(ch0, ch1, ch0conj_ch1, 12 * nb_rb, output_shift0);
}

static simde__m128i nr_comp_muli_sum(simde__m128i input_x,
                                     simde__m128i input_y,
                                     simde__m128i input_w,
                                     simde__m128i input_z,
                                     simde__m128i det)
{
  // complex multiplication (x_re + jx_im)*(y_re + jy_im) = (x_re*y_re - x_im*y_im) + j(x_im*y_re + x_re*y_im)
  // complex multiplication (w_re + jw_im)*(z_re + jz_im) = (w_re*z_re - w_im*z_im) + j(w_im*z_re + w_re*z_im)
  // the real part
  simde__m128i xy_re_128 = simde_mm_madd_epi16(oai_mm_conj(input_x), input_y); // Re: (x_re*y_re - x_im*y_im)
  simde__m128i wz_re_128 = simde_mm_madd_epi16(oai_mm_conj(input_w), input_z); // Re: (w_re*z_re - w_im*z_im)
  xy_re_128 = simde_mm_sub_epi32(xy_re_128, wz_re_128);

  // the imag part
  simde__m128i xy_im_128 = simde_mm_madd_epi16(oai_mm_swap(input_x), input_y); // Im: (x_im*y_re + x_re*y_im)
  simde__m128i wz_im_128 = simde_mm_madd_epi16(oai_mm_swap(input_w), input_z); // Im: (w_im*z_re + w_re*z_im)
  xy_im_128 = simde_mm_sub_epi32(xy_im_128, wz_im_128);

  // print_ints("rx_re:",(int32_t*)&xy_re_128[0]);
  // print_ints("rx_Img:",(int32_t*)&xy_im_128[0]);
  // divide by matrix det and convert back to Q15 before packing
  uint64_t sum_det = 0;
  for (int k = 0; k < 4; k++) {
    sum_det += (((uint32_t *)&det)[k]);
  }
  // Add bias to reduce rounding error
  sum_det = (sum_det + 2) >> 2;

  int b = log2_approx(sum_det) - 8;
  if (b > 0) {
    xy_re_128 = simde_mm_srai_epi32(xy_re_128, b);
    xy_im_128 = simde_mm_srai_epi32(xy_im_128, b);
  } else {
    // shift count in a register: on Arm, slli maps to vshlq_n, which clang only accepts with a constant count
    const simde__m128i shift = simde_mm_cvtsi32_si128(-b);
    xy_re_128 = simde_mm_sll_epi32(xy_re_128, shift);
    xy_im_128 = simde_mm_sll_epi32(xy_im_128, shift);
  }

  simde__m128i output = oai_mm_pack(xy_re_128, xy_im_128);

  return (output);
}

/* Zero Forcing Rx function: nr_construct_HhH_elements()
 *
 *
 * */
static void nr_construct_HhH_elements(c16_t *conjch00_ch00,
                                      c16_t *conjch01_ch01,
                                      c16_t *conjch11_ch11,
                                      c16_t *conjch10_ch10, //
                                      c16_t *conjch20_ch20,
                                      c16_t *conjch21_ch21,
                                      c16_t *conjch30_ch30,
                                      c16_t *conjch31_ch31,
                                      c16_t *conjch00_ch01, // 00_01
                                      c16_t *conjch01_ch00, // 01_00
                                      c16_t *conjch10_ch11, // 10_11
                                      c16_t *conjch11_ch10, // 11_10
                                      c16_t *conjch20_ch21,
                                      c16_t *conjch21_ch20,
                                      c16_t *conjch30_ch31,
                                      c16_t *conjch31_ch30,
                                      c16_t *after_mf_00,
                                      c16_t *after_mf_01,
                                      c16_t *after_mf_10,
                                      c16_t *after_mf_11,
                                      unsigned short nb_rb)
{
  // This function is used to construct the (H_hermitian * H matrix) matrix elements
  simde__m128i *conjch00_ch00_128 = (simde__m128i *)conjch00_ch00;
  simde__m128i *conjch01_ch01_128 = (simde__m128i *)conjch01_ch01;
  simde__m128i *conjch11_ch11_128 = (simde__m128i *)conjch11_ch11;
  simde__m128i *conjch10_ch10_128 = (simde__m128i *)conjch10_ch10;

  simde__m128i *conjch20_ch20_128 = (simde__m128i *)conjch20_ch20;
  simde__m128i *conjch21_ch21_128 = (simde__m128i *)conjch21_ch21;
  simde__m128i *conjch30_ch30_128 = (simde__m128i *)conjch30_ch30;
  simde__m128i *conjch31_ch31_128 = (simde__m128i *)conjch31_ch31;

  simde__m128i *conjch00_ch01_128 = (simde__m128i *)conjch00_ch01;
  simde__m128i *conjch01_ch00_128 = (simde__m128i *)conjch01_ch00;
  simde__m128i *conjch10_ch11_128 = (simde__m128i *)conjch10_ch11;
  simde__m128i *conjch11_ch10_128 = (simde__m128i *)conjch11_ch10;

  simde__m128i *conjch20_ch21_128 = (simde__m128i *)conjch20_ch21;
  simde__m128i *conjch21_ch20_128 = (simde__m128i *)conjch21_ch20;
  simde__m128i *conjch30_ch31_128 = (simde__m128i *)conjch30_ch31;
  simde__m128i *conjch31_ch30_128 = (simde__m128i *)conjch31_ch30;

  simde__m128i *after_mf_00_128 = (simde__m128i *)after_mf_00;
  simde__m128i *after_mf_01_128 = (simde__m128i *)after_mf_01;
  simde__m128i *after_mf_10_128 = (simde__m128i *)after_mf_10;
  simde__m128i *after_mf_11_128 = (simde__m128i *)after_mf_11;

  for (unsigned short rb = 0; rb < 3 * nb_rb; rb++) {
    after_mf_00_128[0] = simde_mm_adds_epi16(conjch00_ch00_128[0], conjch10_ch10_128[0]); // 00_00 + 10_10
    if (conjch20_ch20 != NULL)
      after_mf_00_128[0] = simde_mm_adds_epi16(after_mf_00_128[0], conjch20_ch20_128[0]);
    if (conjch30_ch30 != NULL)
      after_mf_00_128[0] = simde_mm_adds_epi16(after_mf_00_128[0], conjch30_ch30_128[0]);

    after_mf_11_128[0] = simde_mm_adds_epi16(conjch01_ch01_128[0], conjch11_ch11_128[0]); // 01_01 + 11_11
    if (conjch21_ch21 != NULL)
      after_mf_11_128[0] = simde_mm_adds_epi16(after_mf_11_128[0], conjch21_ch21_128[0]);
    if (conjch31_ch31 != NULL)
      after_mf_11_128[0] = simde_mm_adds_epi16(after_mf_11_128[0], conjch31_ch31_128[0]);

    after_mf_01_128[0] = simde_mm_adds_epi16(conjch00_ch01_128[0], conjch10_ch11_128[0]); // 00_01 + 10_11
    if (conjch20_ch21 != NULL)
      after_mf_01_128[0] = simde_mm_adds_epi16(after_mf_01_128[0], conjch20_ch21_128[0]);
    if (conjch30_ch31 != NULL)
      after_mf_01_128[0] = simde_mm_adds_epi16(after_mf_01_128[0], conjch30_ch31_128[0]);

    after_mf_10_128[0] = simde_mm_adds_epi16(conjch01_ch00_128[0], conjch11_ch10_128[0]); // 01_00 + 11_10
    if (conjch21_ch20 != NULL)
      after_mf_10_128[0] = simde_mm_adds_epi16(after_mf_10_128[0], conjch21_ch20_128[0]);
    if (conjch31_ch30 != NULL)
      after_mf_10_128[0] = simde_mm_adds_epi16(after_mf_10_128[0], conjch31_ch30_128[0]);

#ifdef DEBUG_DLSCH_DEMOD
    if ((rb <= 30)) {
      printf(" \n construct_HhH_elements \n");
      print_shorts("after_mf_00_128:", (int16_t *)&after_mf_00_128[0]);
      print_shorts("after_mf_01_128:", (int16_t *)&after_mf_01_128[0]);
      print_shorts("after_mf_10_128:", (int16_t *)&after_mf_10_128[0]);
      print_shorts("after_mf_11_128:", (int16_t *)&after_mf_11_128[0]);
    }
#endif
    conjch00_ch00_128 += 1;
    conjch10_ch10_128 += 1;
    conjch01_ch01_128 += 1;
    conjch11_ch11_128 += 1;

    if (conjch20_ch20 != NULL)
      conjch20_ch20_128 += 1;
    if (conjch21_ch21 != NULL)
      conjch21_ch21_128 += 1;
    if (conjch30_ch30 != NULL)
      conjch30_ch30_128 += 1;
    if (conjch31_ch31 != NULL)
      conjch31_ch31_128 += 1;

    conjch00_ch01_128 += 1;
    conjch01_ch00_128 += 1;
    conjch10_ch11_128 += 1;
    conjch11_ch10_128 += 1;

    if (conjch20_ch21 != NULL)
      conjch20_ch21_128 += 1;
    if (conjch21_ch20 != NULL)
      conjch21_ch20_128 += 1;
    if (conjch30_ch31 != NULL)
      conjch30_ch31_128 += 1;
    if (conjch31_ch30 != NULL)
      conjch31_ch30_128 += 1;

    after_mf_00_128 += 1;
    after_mf_01_128 += 1;
    after_mf_10_128 += 1;
    after_mf_11_128 += 1;
  }
}

// MMSE Rx function: nr_mmse_2layers()
uint8_t nr_mmse_2layers(c16_t **rxdataF_comp,
                        uint32_t buffer_length,
                        uint32_t pdsch_buf_size_max,
                        int nb_rx_ant,
                        int nb_layers,
                        c16_t ch_mag[nb_layers][pdsch_buf_size_max],
                        c16_t ch_magb[nb_layers][pdsch_buf_size_max],
                        c16_t ch_magc[nb_layers][pdsch_buf_size_max],
                        c16_t ch_estimates_ext[][nb_rx_ant][buffer_length],
                        unsigned short nb_rb,
                        unsigned char mod_order,
                        int shift,
                        unsigned char symbol,
                        int length,
                        uint32_t noise_var)
{
  uint32_t nb_rb_0 = length / 12 + ((length % 12) ? 1 : 0);

  /* we need at least alignment to 16 bytes, let's put 32 to be sure
   * (maybe not necessary but doesn't hurt)
   */
  c16_t conjch00_ch01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch01_ch00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch10_ch11[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch11_ch10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch00_ch00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch01_ch01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch10_ch10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch11_ch11[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch20_ch20[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch21_ch21[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch30_ch30[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch31_ch31[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch20_ch21[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch30_ch31[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch21_ch20[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch31_ch30[12 * nb_rb] __attribute__((aligned(32)));

  c16_t af_mf_00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_11[12 * nb_rb] __attribute__((aligned(32)));
  uint32_t determ_fin[12 * nb_rb] __attribute__((aligned(32)));

  c16_t *ch00, *ch01, *ch10, *ch11;
  c16_t *ch20, *ch30, *ch21, *ch31;
  switch (nb_rx_ant) {
    case 2: //
      ch00 = ch_estimates_ext[0][0];
      ch01 = ch_estimates_ext[1][0];
      ch10 = ch_estimates_ext[0][1];
      ch11 = ch_estimates_ext[1][1];
      ch20 = NULL;
      ch21 = NULL;
      ch30 = NULL;
      ch31 = NULL;
      break;

    case 4: //
      ch00 = ch_estimates_ext[0][0];
      ch01 = ch_estimates_ext[1][0];
      ch10 = ch_estimates_ext[0][1];
      ch11 = ch_estimates_ext[1][1];
      ch20 = ch_estimates_ext[0][2];
      ch21 = ch_estimates_ext[1][2];
      ch30 = ch_estimates_ext[0][3];
      ch31 = ch_estimates_ext[1][3];
      break;

    default:
      return -1;
      break;
  }

  /* 1- Compute the rx channel matrix after compensation: (1/2^log2_max)x(H_herm x H)
   * for n_rx = 2
   * |conj_H_00       conj_H_10|    | H_00         H_01|   |(conj_H_00xH_00+conj_H_10xH_10)   (conj_H_00xH_01+conj_H_10xH_11)|
   * |                         |  x |                  | = |                                                                 |
   * |conj_H_01       conj_H_11|    | H_10         H_11|   |(conj_H_01xH_00+conj_H_11xH_10)   (conj_H_01xH_01+conj_H_11xH_11)|
   *
   */

  if (nb_rx_ant >= 2) {
    // (1/2^log2_maxh)*conj_H_00xH_00: (1/(64*2))conjH_00*H_00*2^15
    nr_conjch0_mult_ch1(ch00, ch00, conjch00_ch00, nb_rb_0, shift);
    // (1/2^log2_maxh)*conj_H_10xH_10: (1/(64*2))conjH_10*H_10*2^15
    nr_conjch0_mult_ch1(ch10, ch10, conjch10_ch10, nb_rb_0, shift);
    // conj_H_00xH_01
    nr_conjch0_mult_ch1(ch00, ch01, conjch00_ch01, nb_rb_0,
                        shift); // this shift is equal to the channel level log2_maxh
    // conj_H_10xH_11
    nr_conjch0_mult_ch1(ch10, ch11, conjch10_ch11, nb_rb_0, shift);
    // conj_H_01xH_01
    nr_conjch0_mult_ch1(ch01, ch01, conjch01_ch01, nb_rb_0, shift);
    // conj_H_11xH_11
    nr_conjch0_mult_ch1(ch11, ch11, conjch11_ch11, nb_rb_0, shift);
    // conj_H_01xH_00
    nr_conjch0_mult_ch1(ch01, ch00, conjch01_ch00, nb_rb_0, shift);
    // conj_H_11xH_10
    nr_conjch0_mult_ch1(ch11, ch10, conjch11_ch10, nb_rb_0, shift);
  }
  if (nb_rx_ant == 4) {
    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2*16))conjH_20*H_20*2^15
    nr_conjch0_mult_ch1(ch20, ch20, conjch20_ch20, nb_rb_0, shift);

    // (1/2^log2_maxh)*conj_H_30xH_30: (1/(64*2*4))conjH_30*H_30*2^15
    nr_conjch0_mult_ch1(ch30, ch30, conjch30_ch30, nb_rb_0, shift);

    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2))conjH_20*H_20*2^15
    nr_conjch0_mult_ch1(ch20, ch21, conjch20_ch21, nb_rb_0, shift);

    nr_conjch0_mult_ch1(ch30, ch31, conjch30_ch31, nb_rb_0, shift);

    nr_conjch0_mult_ch1(ch21, ch21, conjch21_ch21, nb_rb_0, shift);

    nr_conjch0_mult_ch1(ch31, ch31, conjch31_ch31, nb_rb_0, shift);

    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2))conjH_20*H_20*2^15
    nr_conjch0_mult_ch1(ch21, ch20, conjch21_ch20, nb_rb_0, shift);

    nr_conjch0_mult_ch1(ch31, ch30, conjch31_ch30, nb_rb_0, shift);

    nr_construct_HhH_elements(conjch00_ch00,
                              conjch01_ch01,
                              conjch11_ch11,
                              conjch10_ch10, //
                              conjch20_ch20,
                              conjch21_ch21,
                              conjch30_ch30,
                              conjch31_ch31,
                              conjch00_ch01,
                              conjch01_ch00,
                              conjch10_ch11,
                              conjch11_ch10, //
                              conjch20_ch21,
                              conjch21_ch20,
                              conjch30_ch31,
                              conjch31_ch30,
                              af_mf_00,
                              af_mf_01,
                              af_mf_10,
                              af_mf_11,
                              nb_rb_0);
  }
  if (nb_rx_ant == 2) {
    nr_construct_HhH_elements(conjch00_ch00,
                              conjch01_ch01,
                              conjch11_ch11,
                              conjch10_ch10, //
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              conjch00_ch01,
                              conjch01_ch00,
                              conjch10_ch11,
                              conjch11_ch10, //
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              af_mf_00,
                              af_mf_01,
                              af_mf_10,
                              af_mf_11,
                              nb_rb_0);
  }

  // Add noise_var such that: H^h * H + noise_var * I
  if (noise_var != 0) {
    simde__m128i nvar_128i = simde_mm_set1_epi32(noise_var >> shift);
    simde__m128i *af_mf_00_128i = (simde__m128i *)af_mf_00;
    simde__m128i *af_mf_11_128i = (simde__m128i *)af_mf_11;
    for (int k = 0; k < 3 * nb_rb_0; k++) {
      af_mf_00_128i[0] = simde_mm_add_epi32(af_mf_00_128i[0], nvar_128i);
      af_mf_11_128i[0] = simde_mm_add_epi32(af_mf_11_128i[0], nvar_128i);
      af_mf_00_128i++;
      af_mf_11_128i++;
    }
  }

  // det_HhH = ad -bc
  nr_det_HhH(af_mf_00, // a
             af_mf_01, // b
             af_mf_10, // c
             af_mf_11, // d
             determ_fin,
             nb_rb_0);
  /* 2- Compute the channel matrix inversion **********************************
   *
   *    |(conj_H_00xH_00+conj_H_10xH_10)   (conj_H_00xH_01+conj_H_10xH_11)|
   * A= |                                                                 |
   *    |(conj_H_01xH_00+conj_H_11xH_10)   (conj_H_01xH_01+conj_H_11xH_11)|
   *
   *
   *
   *inv(A) =(1/det)*[d  -b
   *                 -c  a]
   *
   *
   **************************************************************************/
  simde__m128i *ch_mag128_0 = NULL, *ch_mag128b_0 = NULL, *ch_mag128c_0 = NULL; // Layer 0
  simde__m128i *ch_mag128_1 = NULL, *ch_mag128b_1 = NULL, *ch_mag128c_1 = NULL; // Layer 1
  simde__m128i mmtmpD0, mmtmpD1, mmtmpD2, mmtmpD3;
  simde__m128i QAM_amp128 = {0}, QAM_amp128b = {0}, QAM_amp128c = {0};

  simde__m128i *determ_fin_128 = (simde__m128i *)&determ_fin[0];

  simde__m128i *after_mf_a_128 = (simde__m128i *)af_mf_00;
  simde__m128i *after_mf_b_128 = (simde__m128i *)af_mf_01;
  simde__m128i *after_mf_c_128 = (simde__m128i *)af_mf_10;
  simde__m128i *after_mf_d_128 = (simde__m128i *)af_mf_11;

  simde__m128i *rxdataF_comp128_0 = (simde__m128i *)&rxdataF_comp[0][symbol * buffer_length];
  simde__m128i *rxdataF_comp128_1 = (simde__m128i *)&rxdataF_comp[1][symbol * buffer_length];

  if (mod_order > 2) {
    if (mod_order == 4) {
      QAM_amp128 = simde_mm_set1_epi16(QAM16_n1); // 2/sqrt(10)
      QAM_amp128b = simde_mm_setzero_si128();
      QAM_amp128c = simde_mm_setzero_si128();
    } else if (mod_order == 6) {
      QAM_amp128 = simde_mm_set1_epi16(QAM64_n1); // 4/sqrt{42}
      QAM_amp128b = simde_mm_set1_epi16(QAM64_n2); // 2/sqrt{42}
      QAM_amp128c = simde_mm_setzero_si128();
    } else if (mod_order == 8) {
      QAM_amp128 = simde_mm_set1_epi16(QAM256_n1);
      QAM_amp128b = simde_mm_set1_epi16(QAM256_n2);
      QAM_amp128c = simde_mm_set1_epi16(QAM256_n3);
    }
    ch_mag128_0 = (simde__m128i *)ch_mag[0];
    ch_mag128b_0 = (simde__m128i *)ch_magb[0];
    ch_mag128c_0 = (simde__m128i *)ch_magc[0];
    ch_mag128_1 = (simde__m128i *)ch_mag[1];
    ch_mag128b_1 = (simde__m128i *)ch_magb[1];
    ch_mag128c_1 = (simde__m128i *)ch_magc[1];
  }

  for (int rb = 0; rb < 3 * nb_rb_0; rb++) {
    // Magnitude computation
    if (mod_order > 2) {
      uint64_t sum_det = 0;
      for (int k = 0; k < 4; k++) {
        sum_det += (((uint32_t *)&determ_fin_128[0])[k]);
      }
      // Add bias to reduce rounding error
      sum_det = (sum_det + 2) >> 2;

      int b = log2_approx(sum_det) - 8;
      if (b > 0) {
        mmtmpD2 = simde_mm_srai_epi32(determ_fin_128[0], b);
      } else {
        mmtmpD2 = simde_mm_sll_epi32(determ_fin_128[0], simde_mm_cvtsi32_si128(-b));
      }
      mmtmpD3 = simde_mm_unpacklo_epi32(mmtmpD2, mmtmpD2);
      mmtmpD2 = simde_mm_unpackhi_epi32(mmtmpD2, mmtmpD2);
      mmtmpD2 = simde_mm_packs_epi32(mmtmpD3, mmtmpD2);

      // Layer 0
      ch_mag128_0[0] = mmtmpD2;
      ch_mag128b_0[0] = mmtmpD2;
      ch_mag128c_0[0] = mmtmpD2;
      ch_mag128_0[0] = simde_mm_mulhi_epi16(ch_mag128_0[0], QAM_amp128);
      ch_mag128_0[0] = simde_mm_slli_epi16(ch_mag128_0[0], 1);
      ch_mag128b_0[0] = simde_mm_mulhi_epi16(ch_mag128b_0[0], QAM_amp128b);
      ch_mag128b_0[0] = simde_mm_slli_epi16(ch_mag128b_0[0], 1);
      ch_mag128c_0[0] = simde_mm_mulhi_epi16(ch_mag128c_0[0], QAM_amp128c);
      ch_mag128c_0[0] = simde_mm_slli_epi16(ch_mag128c_0[0], 1);

      // Layer 1
      ch_mag128_1[0] = mmtmpD2;
      ch_mag128b_1[0] = mmtmpD2;
      ch_mag128c_1[0] = mmtmpD2;
      ch_mag128_1[0] = simde_mm_mulhi_epi16(ch_mag128_1[0], QAM_amp128);
      ch_mag128_1[0] = simde_mm_slli_epi16(ch_mag128_1[0], 1);
      ch_mag128b_1[0] = simde_mm_mulhi_epi16(ch_mag128b_1[0], QAM_amp128b);
      ch_mag128b_1[0] = simde_mm_slli_epi16(ch_mag128b_1[0], 1);
      ch_mag128c_1[0] = simde_mm_mulhi_epi16(ch_mag128c_1[0], QAM_amp128c);
      ch_mag128c_1[0] = simde_mm_slli_epi16(ch_mag128c_1[0], 1);
    }

    // multiply by channel Inv
    // rxdataF_zf128_0 = rxdataF_comp128_0*d - b*rxdataF_comp128_1
    // rxdataF_zf128_1 = rxdataF_comp128_1*a - c*rxdataF_comp128_0
    // printf("layer_1 \n");
    mmtmpD0 = nr_comp_muli_sum(rxdataF_comp128_0[0], after_mf_d_128[0], rxdataF_comp128_1[0], after_mf_b_128[0], determ_fin_128[0]);

    // printf("layer_2 \n");
    mmtmpD1 = nr_comp_muli_sum(rxdataF_comp128_1[0], after_mf_a_128[0], rxdataF_comp128_0[0], after_mf_c_128[0], determ_fin_128[0]);

    rxdataF_comp128_0[0] = mmtmpD0;
    rxdataF_comp128_1[0] = mmtmpD1;

#ifdef DEBUG_DLSCH_DEMOD
    printf("\n Rx signal after ZF l%d rb%d\n", symbol, rb);
    print_shorts(" Rx layer 1:", (int16_t *)&rxdataF_comp128_0[0]);
    print_shorts(" Rx layer 2:", (int16_t *)&rxdataF_comp128_1[0]);
#endif
    determ_fin_128 += 1;
    ch_mag128_0 += 1;
    ch_mag128_1 += 1;
    ch_mag128b_0 += 1;
    ch_mag128b_1 += 1;
    ch_mag128c_0 += 1;
    ch_mag128c_1 += 1;
    rxdataF_comp128_0 += 1;
    rxdataF_comp128_1 += 1;
    after_mf_a_128 += 1;
    after_mf_b_128 += 1;
    after_mf_c_128 += 1;
    after_mf_d_128 += 1;
  }
  return (0);
}

//----------------------------------------------------------------------------------------------
// Simple LLR functions (QPSK / 16QAM / 64QAM / 256QAM) - modulation-order-specific demapping
//----------------------------------------------------------------------------------------------

static inline c16_t saturating_sub(c16_t a, c16_t b)
{
  c32_t tmp = {a.r - abs(b.r), a.i - abs(b.i)};
  tmp = (c32_t){min(tmp.r, INT16_MAX), min(tmp.i, INT16_MAX)};
  c16_t tmp2 = (c16_t){max(tmp.r, -INT16_MAX), max(tmp.i, -INT16_MAX)};
  return tmp2;
}

void nr_qpsk_llr(const c16_t *rxdataF_comp, int16_t *llr, uint32_t nb_re)
{
  const c16_t *rxF = rxdataF_comp;
  c16_t *llr32 = (c16_t *)llr;
  for (int i = 0; i < nb_re; i++) {
    llr32[i].r = rxF[i].r >> 4;
    llr32[i].i = rxF[i].i >> 4;
  }
}

void nr_16qam_llr(const c16_t *rxdataF_comp, const c16_t *ch_mag_in, int16_t *llr, uint32_t nb_re)
{
  simde__m256i *rxF_256 = (simde__m256i *)rxdataF_comp;
  simde__m256i *ch_mag256 = (simde__m256i *)ch_mag_in;
  int64_t *llr_64 = (int64_t *)llr;

#ifndef USE_128BIT
  for (int i = 0; i < (nb_re >> 3); i++) {
    // registers of even index in xmm0-> |y_R|, registers of odd index in xmm0-> |y_I|
    simde__m256i xmm0 = protected_abs256(*rxF_256);
    // registers of even index in xmm0-> |y_R|-|h|^2, registers of odd index in xmm0-> |y_I|-|h|^2
    xmm0 = simde_mm256_subs_epi16(*ch_mag256, xmm0);

    simde__m256i xmm1 = simde_mm256_unpacklo_epi32(*rxF_256, xmm0);
    simde__m256i xmm2 = simde_mm256_unpackhi_epi32(*rxF_256, xmm0);

    // xmm1 |1st 2ed 3rd 4th  9th 10th 13rd 14th|
    // xmm2 |5th 6th 7th 8th 11st 12ed 15th 16th|

    *llr_64++ = simde_mm256_extract_epi64(xmm1, 0);
    *llr_64++ = simde_mm256_extract_epi64(xmm1, 1);
    *llr_64++ = simde_mm256_extract_epi64(xmm2, 0);
    *llr_64++ = simde_mm256_extract_epi64(xmm2, 1);
    *llr_64++ = simde_mm256_extract_epi64(xmm1, 2);
    *llr_64++ = simde_mm256_extract_epi64(xmm1, 3);
    *llr_64++ = simde_mm256_extract_epi64(xmm2, 2);
    *llr_64++ = simde_mm256_extract_epi64(xmm2, 3);
    rxF_256++;
    ch_mag256++;
  }

  nb_re &= 0x7;
#endif

  simde__m128i *rxF_128 = (simde__m128i *)rxF_256;
  simde__m128i *ch_mag_128 = (simde__m128i *)ch_mag256;
  simde__m128i *llr_128 = (simde__m128i *)llr_64;

  // Each iteration does 4 RE (gives 16 16bit-llrs)
  for (int i = 0; i < (nb_re >> 2); i++) {
    // registers of even index in xmm0-> |y_R|, registers of odd index in xmm0-> |y_I|
    simde__m128i xmm0 = protected_abs128(*rxF_128);
    // registers of even index in xmm0-> |y_R|-|h|^2, registers of odd index in xmm0-> |y_I|-|h|^2
    xmm0 = simde_mm_subs_epi16(*ch_mag_128, xmm0);

    simde_mm_storeu_si128(llr_128++, simde_mm_unpacklo_epi32(*rxF_128, xmm0)); // RE: 0,1,4,5
    simde_mm_storeu_si128(llr_128++, simde_mm_unpackhi_epi32(*rxF_128, xmm0)); // RE: 2,3,6,7
    rxF_128++;
    ch_mag_128++;
  }

  nb_re &= 0x3;
  c16_t *rxDataF = (c16_t *)rxF_128;
  c16_t *ch_mag = (c16_t *)ch_mag_128;
  c16_t *llr_tail = (c16_t *)llr_128;
  for (uint32_t i = 0U; i < nb_re; i++) {
    c16_t tmp = *rxDataF++;
    *llr_tail++ = tmp;
    *llr_tail++ = saturating_sub(*ch_mag++, tmp);
  }
}

void nr_64qam_llr(const c16_t *rxdataF_comp, const c16_t *ch_mag, const c16_t *ch_mag2, int16_t *llr, uint32_t nb_re)
{
  simde__m256i *rxF = (simde__m256i *)rxdataF_comp;

  simde__m256i *ch_maga = (simde__m256i *)ch_mag;
  simde__m256i *ch_magb = (simde__m256i *)ch_mag2;

  int32_t *llr_32 = (int32_t *)llr;
#ifndef USE_128BIT
  for (int i = 0; i < (nb_re >> 3); i++) {
    simde__m256i xmm0 = simde_mm256_loadu_si256(rxF);
    // registers of even index in xmm0-> |y_R|, registers of odd index in xmm0-> |y_I|
    simde__m256i xmm1 = protected_abs256(xmm0);
    // registers of even index in xmm0-> |y_R|-|h|^2, registers of odd index in xmm0-> |y_I|-|h|^2
    xmm1 = simde_mm256_subs_epi16(*ch_maga, xmm1);
    simde__m256i xmm2 = protected_abs256(xmm1);
    xmm2 = simde_mm256_subs_epi16(*ch_magb, xmm2);
    // xmm0 |1st 4th 7th 10th 13th 16th 19th 22ed|
    // xmm1 |2ed 5th 8th 11th 14th 17th 20th 23rd|
    // xmm2 |3rd 6th 9th 12th 15th 18th 21st 24th|

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 0);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 0);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 0);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 1);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 1);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 1);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 2);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 2);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 2);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 3);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 3);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 3);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 4);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 4);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 4);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 5);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 5);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 5);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 6);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 6);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 6);

    *llr_32++ = simde_mm256_extract_epi32(xmm0, 7);
    *llr_32++ = simde_mm256_extract_epi32(xmm1, 7);
    *llr_32++ = simde_mm256_extract_epi32(xmm2, 7);
    rxF++;
    ch_maga++;
    ch_magb++;
  }

  nb_re &= 0x7;
#endif

  simde__m128i *rxF_128 = (simde__m128i *)rxF;
  simde__m128i *ch_mag_128 = (simde__m128i *)ch_maga;
  simde__m128i *ch_magb_128 = (simde__m128i *)ch_magb;
  // Each iteration does 4 RE (gives 24 16bit-llrs)
  for (int i = 0; i < (nb_re >> 2); i++) {
    simde__m128i xmm0, xmm1, xmm2;
    xmm0 = *rxF_128;
    xmm1 = protected_abs128(xmm0);
    xmm1 = simde_mm_subs_epi16(*ch_mag_128, xmm1);
    xmm2 = protected_abs128(xmm1);
    xmm2 = simde_mm_subs_epi16(*ch_magb_128, xmm2);

    *llr_32++ = simde_mm_extract_epi32(xmm0, 0);
    *llr_32++ = simde_mm_extract_epi32(xmm1, 0);
    *llr_32++ = simde_mm_extract_epi32(xmm2, 0);
    *llr_32++ = simde_mm_extract_epi32(xmm0, 1);
    *llr_32++ = simde_mm_extract_epi32(xmm1, 1);
    *llr_32++ = simde_mm_extract_epi32(xmm2, 1);
    *llr_32++ = simde_mm_extract_epi32(xmm0, 2);
    *llr_32++ = simde_mm_extract_epi32(xmm1, 2);
    *llr_32++ = simde_mm_extract_epi32(xmm2, 2);
    *llr_32++ = simde_mm_extract_epi32(xmm0, 3);
    *llr_32++ = simde_mm_extract_epi32(xmm1, 3);
    *llr_32++ = simde_mm_extract_epi32(xmm2, 3);
    rxF_128++;
    ch_mag_128++;
    ch_magb_128++;
  }

  nb_re &= 0x3;

  c16_t *rxDataF = (c16_t *)rxF_128;
  c16_t *ch_mag_tail = (c16_t *)ch_mag_128;
  c16_t *ch_magb_tail = (c16_t *)ch_magb_128;
  c16_t *llr_tail = (c16_t *)llr_32;
  for (int i = 0; i < nb_re; i++) {
    *llr_tail++ = *rxDataF;
    c16_t tmp = saturating_sub(*ch_mag_tail++, *rxDataF++);
    *llr_tail++ = tmp;
    *llr_tail++ = saturating_sub(*ch_magb_tail++, tmp);
  }
}

void nr_256qam_llr(const c16_t *rxdataF_comp,
                   const c16_t *ch_mag,
                   const c16_t *ch_mag2,
                   const c16_t *ch_mag3,
                   int16_t *llr,
                   uint32_t nb_re)
{
  simde__m256i *rxF_256 = (simde__m256i *)rxdataF_comp;
  simde__m256i *llr256 = (simde__m256i *)llr;

  simde__m256i *ch_maga = (simde__m256i *)ch_mag;
  simde__m256i *ch_magb = (simde__m256i *)ch_mag2;
  simde__m256i *ch_magc = (simde__m256i *)ch_mag3;
#ifndef USE_128BIT
  for (int i = 0; i < (nb_re >> 3); i++) {
    // registers of even index in xmm0-> |y_R|, registers of odd index in xmm0-> |y_I|
    simde__m256i xmm0 = protected_abs256(*rxF_256);
    // registers of even index in xmm0-> |y_R|-|h|^2, registers of odd index in xmm0-> |y_I|-|h|^2
    xmm0 = simde_mm256_subs_epi16(*ch_maga, xmm0);
    //  xmmtmpD2 contains 16 LLRs
    simde__m256i xmm1 = protected_abs256(xmm0);
    xmm1 = simde_mm256_subs_epi16(*ch_magb, xmm1); // contains 16 LLRs
    simde__m256i xmm2 = protected_abs256(xmm1);
    xmm2 = simde_mm256_subs_epi16(*ch_magc, xmm2); // contains 16 LLRs
    // rxF[i] A0 A1 A2 A3 A4 A5 A6 A7 bits 7,6
    // xmm0   B0 B1 B2 B3 B4 B5 B6 B7 bits 5,4
    // xmm1   C0 C1 C2 C3 C4 C5 C6 C7 bits 3,2
    // xmm2   D0 D1 D2 D3 D4 D5 D6 D7 bits 1,0
    simde__m256i xmm3 = simde_mm256_unpacklo_epi32(*rxF_256, xmm0); // A0 B0 A1 B1 A4 B4 A5 B5
    simde__m256i xmm4 = simde_mm256_unpackhi_epi32(*rxF_256, xmm0); // A2 B2 A3 B3 A6 B6 A7 B7
    simde__m256i xmm5 = simde_mm256_unpacklo_epi32(xmm1, xmm2); // C0 D0 C1 D1 C4 D4 C5 D5
    simde__m256i xmm6 = simde_mm256_unpackhi_epi32(xmm1, xmm2); // C2 D2 C3 D3 C6 D6 C7 D7

    xmm0 = simde_mm256_unpacklo_epi64(xmm3, xmm5); // A0 B0 C0 D0 A4 B4 C4 D4
    xmm1 = simde_mm256_unpackhi_epi64(xmm3, xmm5); // A1 B1 C1 D1 A5 B5 C5 D5
    xmm2 = simde_mm256_unpacklo_epi64(xmm4, xmm6); // A2 B2 C2 D2 A6 B6 C6 D6
    xmm3 = simde_mm256_unpackhi_epi64(xmm4, xmm6); // A3 B3 C3 D3 A7 B7 C7 D7
    simde_mm256_storeu_si256(llr256++, simde_mm256_permute2x128_si256(xmm0, xmm1, 0x20)); // A0 B0 C0 D0 A1 B1 C1 D1
    simde_mm256_storeu_si256(llr256++, simde_mm256_permute2x128_si256(xmm2, xmm3, 0x20)); // A2 B2 C2 D2 A3 B3 C3 D3
    simde_mm256_storeu_si256(llr256++, simde_mm256_permute2x128_si256(xmm0, xmm1, 0x31)); // A4 B4 C4 D4 A5 B5 C5 D5
    simde_mm256_storeu_si256(llr256++, simde_mm256_permute2x128_si256(xmm2, xmm3, 0x31)); // A6 B6 C6 D6 A7 B7 C7 D7
    ch_magc++;
    ch_magb++;
    ch_maga++;
    rxF_256++;
  }

  nb_re &= 0x7;
#endif

  simde__m128i *rxF_128 = (simde__m128i *)rxF_256;
  simde__m128i *llr_128 = (simde__m128i *)llr256;

  simde__m128i *ch_maga_128 = (simde__m128i *)ch_maga;
  simde__m128i *ch_magb_128 = (simde__m128i *)ch_magb;
  simde__m128i *ch_magc_128 = (simde__m128i *)ch_magc;
  for (int i = 0; i < (nb_re >> 2); i++) {
    simde__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6;
    // registers of even index in xmm0-> |y_R|, registers of odd index in xmm0-> |y_I|
    xmm0 = protected_abs128(*rxF_128);
    // registers of even index in xmm0-> |y_R|-|h|^2, registers of odd index in xmm0-> |y_I|-|h|^2
    xmm0 = simde_mm_subs_epi16(*ch_maga_128, xmm0);
    xmm1 = protected_abs128(xmm0);
    xmm1 = simde_mm_subs_epi16(*ch_magb_128, xmm1); // contains 8 LLRs
    xmm2 = protected_abs128(xmm1);
    xmm2 = simde_mm_subs_epi16(*ch_magc_128, xmm2); // contains 8 LLRs
    // rxF[i] A0 A1 A2 A3
    // xmm0   B0 B1 B2 B3
    // xmm1   C0 C1 C2 C3
    // xmm2   D0 D1 D2 D3
    xmm3 = simde_mm_unpacklo_epi32(*rxF_128, xmm0); // A0 B0 A1 B1
    xmm4 = simde_mm_unpackhi_epi32(*rxF_128, xmm0); // A2 B2 A3 B3
    xmm5 = simde_mm_unpacklo_epi32(xmm1, xmm2); // C0 D0 C1 D1
    xmm6 = simde_mm_unpackhi_epi32(xmm1, xmm2); // C2 D2 C3 D3

    simde_mm_storeu_si128(llr_128++, simde_mm_unpacklo_epi64(xmm3, xmm5)); // A0 B0 C0 D0
    simde_mm_storeu_si128(llr_128++, simde_mm_unpackhi_epi64(xmm3, xmm5)); // A1 B1 C1 D1
    simde_mm_storeu_si128(llr_128++, simde_mm_unpacklo_epi64(xmm4, xmm6)); // A2 B2 C2 D2
    simde_mm_storeu_si128(llr_128++, simde_mm_unpackhi_epi64(xmm4, xmm6)); // A3 B3 C3 D3

    rxF_128++;
    ch_maga_128++;
    ch_magb_128++;
    ch_magc_128++;
  }

  nb_re &= 0x3;
  c16_t *rxDataF = (c16_t *)rxF_128;
  c16_t *ch_mag_tail = (c16_t *)ch_maga_128;
  c16_t *ch_magb_tail = (c16_t *)ch_magb_128;
  c16_t *ch_magc_tail = (c16_t *)ch_magc_128;
  c16_t *llr_tail = (c16_t *)llr_128;
  for (int i = 0; i < nb_re; i++) {
    c16_t tmp = *rxDataF++;
    *llr_tail++ = tmp;
    c16_t tmp1 = saturating_sub(*ch_mag_tail++, tmp);
    *llr_tail++ = tmp1;
    c16_t tmp2 = saturating_sub(*ch_magb_tail++, tmp1);
    *llr_tail++ = tmp2;
    *llr_tail++ = saturating_sub(*ch_magc_tail++, tmp2);
  }
}
