/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// Initial unit test for nr_layer_precoder_simd_[cm, simd] functions
// Issue: gNB: SIMD Precoding Bug in nr_layer_precoder_simd for PMI = 4
// https://gitlab.eurecom.fr/oai/openairinterface5g/-/issues/955

#include "gtest/gtest.h"
#include <cstdint>
#include <cmath>
#include <vector>
#include <simde/x86/avx512.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "openair1/PHY/TOOLS/tools_defs.h" // c16_t
#include "nfapi/open-nFAPI/nfapi/public_inc/fapi_nr_ue_interface.h" // fapi_nr_dl_config_dci_dl_pdu_rel15_t

//#include "openair1/PHY/MODULATION/nr_modulation.h" // nr_layer_precoder_simd_[cm, simd]

// Forward declare the function but hide the Variable-Length Array (VLA) feature from C not available in C++
c16_t nr_layer_precoder_cm(int n_layers,
                           int symbol_size,
                           c16_t (*dataF_in)[/*symbol_size*/], // c16_t dataF_in[n_layers][symbol_size]
                           int ant,
                           c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                           int offset);

// Forward declare the function but hide the Variable-Length Array (VLA) feature from C not available in C++
void nr_layer_precoder_simd(int n_layers,
                            int symbol_size,
                            const c16_t (*dataF_in)[/*symbol_size*/], // c16_t dataF_in[n_layers][symbol_size]
                            int ant,
                            c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                            int offset,
                            int re_cnt,
                            c16_t *dataF_out);

// 2-port / 2-layer fast path: fills both antenna ports in one pass
void nr_layer_precoder_2x2_simd(int symbol_size,
                                const c16_t (*dataF_in)[/*symbol_size*/], // c16_t dataF_in[2][symbol_size]
                                c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                                int offset,
                                int re_cnt,
                                c16_t *dataF_out_ant0,
                                c16_t *dataF_out_ant1);

// Cross-polar fast paths: fill both ports of the pair (p, p+2) in one pass, sharing the
// per-layer complex multiplies between the two polarisations. The rank-2 specialisation is
// dispatched on ARMv8.0 only; the general one is built on every target.
#if defined(__aarch64__) && !defined(__ARM_FEATURE_QRDMX)
void nr_layer_precoder_2x4_simd(int symbol_size,
                                const c16_t (*dataF_in)[/*symbol_size*/], // c16_t dataF_in[2][symbol_size]
                                c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                                int p,
                                bool phi_swap,
                                bool phi_neg,
                                int offset,
                                int re_cnt,
                                c16_t *dataF_out_lo,
                                c16_t *dataF_out_hi);
#endif

void nr_layer_precoder_Nx4_simd(int n_layers,
                                int symbol_size,
                                const c16_t (*dataF_in)[/*symbol_size*/], // c16_t dataF_in[n_layers][symbol_size]
                                c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                                int p,
                                const bool phi_swap[NR_MAX_NB_LAYERS],
                                const bool phi_neg[NR_MAX_NB_LAYERS],
                                int offset,
                                int re_cnt,
                                c16_t *dataF_out_lo,
                                c16_t *dataF_out_hi);

#ifdef __cplusplus
}
#endif

// The SIMD precoders apply their per-layer Q15 scaling with different rounding
// per ISA: x86 (madd + arithmetic shift) truncates, while aarch64/NEON
// (vqdmulh/vqrdmlah) rounds. Each layer's contribution can therefore differ by
// up to 1 LSB, and the difference accumulates over the layers, so a comparison
// against a reference computed with different rounding must tolerate up to
// n_layers LSB. Exact-equality checks previously broke CI on aarch64.
#define EXPECT_C16_NEAR(got, want, tol) EXPECT_LE(abs((int)(got) - (int)(want)), (tol))

TEST(NrLayerPrecoderTest, Basic)
{
  constexpr int n_layers = 2;
  constexpr int symbol_size = 24;
  constexpr int n_ants = 2;
  constexpr int ant = 1;
  constexpr int re_cnt = 24;

  // Initialize the 2D input data buffer
  std::vector<c16_t> buffer_in(n_layers * symbol_size);
  std::vector<c16_t> buffer_out(n_layers * symbol_size);

  for (int i = 0; i < n_layers * symbol_size; ++i) {
    buffer_in[i] = {static_cast<int16_t>(i + 1), static_cast<int16_t>(-i - 1)};
  }
  for (int i = 0; i < n_ants * symbol_size; ++i) {
    buffer_out[i] = {static_cast<int16_t>(0), static_cast<int16_t>(0)};
  }

  // Cast flat buffer to the required 2D Variable-Length Array (VLA) style pointer
  c16_t(*dataF_in)[] = reinterpret_cast<c16_t(*)[]>(buffer_in.data());
  c16_t(*dataF_out)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(buffer_out.data());

  // Create and populate the weights
  c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS];
  for (int layer = 0; layer < n_layers; ++layer) {
    weights[layer][ant] = (c16_t){-SHRT_MAX, 0};
  }

  // Call the C function
  for (int symbol = 0; symbol < re_cnt; symbol++)
    dataF_out[ant][symbol] = nr_layer_precoder_cm(n_layers, symbol_size, dataF_in, ant, weights, symbol);

  // You can calculate the expected value manually or test rough correctness
  for (int symbol = 0; symbol < re_cnt; symbol++) {
    int result_real = -26 - (symbol<<1);
    int result_imag = -24 - (symbol<<1);

    EXPECT_EQ(dataF_out[ant][symbol].r, result_real)
        << " at [" << ant << "][" << symbol << "] got real part: " << dataF_out[ant][symbol].r;
    EXPECT_EQ(dataF_out[ant][symbol].i, -result_imag)
        << " at [" << ant << "][" << symbol << "] got imag part: " << dataF_out[ant][symbol].i;
  }
}

TEST(NrLayerPrecoderTest, SIMD)
{
  constexpr int n_layers = 2;
  constexpr int symbol_size = 24;
  constexpr int n_ants = 2;
  constexpr int ant = 1;
  constexpr int re_cnt = 24;

  // Initialize the 2D input data buffer
  std::vector<c16_t> buffer_in(n_layers * symbol_size);
  std::vector<c16_t> buffer_out(n_layers * symbol_size);

  for (int i = 0; i < n_layers * symbol_size; ++i) {
    buffer_in[i] = {static_cast<int16_t>(i + 1), static_cast<int16_t>(-i - 1)};
  }
  for (int i = 0; i < n_ants * symbol_size; ++i) {
    buffer_out[i] = {static_cast<int16_t>(0), static_cast<int16_t>(0)};
  }

  // Cast flat buffer to the required 2D Variable-Length Array (VLA) style pointer
  c16_t(*dataF_in)[] = reinterpret_cast<c16_t(*)[]>(buffer_in.data());
  c16_t(*dataF_out)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(buffer_out.data());

  // Create and populate the weights
  c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS];
  for (int layer = 0; layer < n_layers; ++layer) {
    weights[layer][ant] = (c16_t){-SHRT_MAX, 0};
  }

  // Call the C function
  nr_layer_precoder_simd(n_layers, symbol_size, dataF_in, ant, weights, 0, re_cnt, dataF_out[ant]);

  // You can calculate the expected value manually or test rough correctness
  for (int symbol = 0; symbol < re_cnt; symbol++) {
    int result_real = -26 - (symbol<<1);
    int result_imag = -24 - (symbol<<1);

    // n_layers contributions, each up to 1 LSB apart between truncating (x86)
    // and rounding (NEON) Q15 scaling
    EXPECT_C16_NEAR(dataF_out[ant][symbol].r, result_real, n_layers)
        << " at [" << ant << "][" << symbol << "] got real part: " << dataF_out[ant][symbol].r;
    EXPECT_C16_NEAR(dataF_out[ant][symbol].i, -result_imag, n_layers)
        << " at [" << ant << "][" << symbol << "] got imag part: " << dataF_out[ant][symbol].i;
  }
}

TEST(NrLayerPrecoderTest, Compare_CM_SIMD)
{
  constexpr int n_layers = 1;
  constexpr int symbol_size = 24;
  constexpr int n_ants = 2;
  constexpr int re_cnt = 24;

  // Initialize the 2D input data buffer
  std::vector<c16_t> buffer_in(n_layers * symbol_size);
  std::vector<c16_t> buffer_out_cm(n_ants * symbol_size);
  std::vector<c16_t> buffer_out_simd(n_ants * symbol_size);

  for (int i = 0; i < n_layers * symbol_size; ++i) {
	buffer_in[i] = {static_cast<int16_t>((rand() % (2 * SHRT_MAX + 1)) - SHRT_MAX), static_cast<int16_t>((rand() % (2 * SHRT_MAX + 1)) - SHRT_MAX)};
  }
  for (int i = 0; i < n_ants * symbol_size; ++i) {
    buffer_out_cm[i] = {static_cast<int16_t>(0), static_cast<int16_t>(0)};
    buffer_out_simd[i] = {static_cast<int16_t>(0), static_cast<int16_t>(0)};
  }

  // Cast flat buffer to the required 2D Variable-Length Array (VLA) style pointer
  c16_t(*dataF_in)[] = reinterpret_cast<c16_t(*)[]>(buffer_in.data());
  c16_t(*dataF_out_cm)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(buffer_out_cm.data());
  c16_t(*dataF_out_simd)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(buffer_out_simd.data());

  // Create and populate the weights
  c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS];
  for (int layer = 0; layer < n_layers; ++layer) {
    // Could not use convert_precoder_weight() as complex.h could not be used in googletest
    // Use the logic in convert_precoder_weight()
    // precoder [−1,−j]
    weights[layer][0] = (c16_t){-SHRT_MAX, 0};
    weights[layer][1] = (c16_t){0, -SHRT_MAX};
  }

  // Get the results for all the antenna 
  for (int ant = 0; ant < n_ants; ant++) {

    // Call the C function
    for (int symbol = 0; symbol < re_cnt; symbol++)
      dataF_out_cm[ant][symbol] = nr_layer_precoder_cm(n_layers, symbol_size, dataF_in, ant, weights, symbol);

    // Call the C function
    nr_layer_precoder_simd(n_layers, symbol_size, dataF_in, ant, weights, 0, re_cnt, dataF_out_simd[ant]);

    // Compare the result from both C function
    for (int symbol = 0; symbol < re_cnt; symbol++) {
      EXPECT_C16_NEAR(dataF_out_cm[ant][symbol].r, dataF_out_simd[ant][symbol].r, 1)
          << " at [" << ant << "][" << symbol << "] got real part: " << dataF_out_cm[ant][symbol].r << " result " << dataF_out_simd[ant][symbol].r;
      EXPECT_C16_NEAR(dataF_out_cm[ant][symbol].i, dataF_out_simd[ant][symbol].i, 1)
          << " at [" << ant << "][" << symbol << "] got imag part: " << dataF_out_cm[ant][symbol].i << " result " << dataF_out_simd[ant][symbol].i;
    }
  }
}

// Compare the 2-port/2-layer butterfly fast path against the generic
// per-antenna SIMD kernel for all four 2x2 codebook weights. They must agree to
// <= n_layers LSB (truncating vs rounding of the per-layer Q15 scale, summed
// over 2 layers). Inputs are kept below the range where the combined output
// (x0 +/- x1)/sqrt(2) overflows int16: there the generic path's overflow
// handling differs by ISA (x86 saturates, NEON wraps) whereas the fast path
// always saturates, so that region is not a meaningful equivalence check.
TEST(NrLayerPrecoderTest, Compare_2x2_SIMD)
{
  constexpr int n_layers = 2;
  constexpr int symbol_size = 24;
  constexpr int re_cnt = 24;
  constexpr int16_t C = 23170; // round(SHRT_MAX / sqrt(2))
  constexpr int in_max = 16000; // |x0|+|x1| after scaling stays within int16

  std::vector<c16_t> buffer_in(n_layers * symbol_size);
  for (int i = 0; i < n_layers * symbol_size; ++i)
    buffer_in[i] = {static_cast<int16_t>((rand() % (2 * in_max + 1)) - in_max),
                    static_cast<int16_t>((rand() % (2 * in_max + 1)) - in_max)};
  c16_t(*dataF_in)[] = reinterpret_cast<c16_t(*)[]>(buffer_in.data());

  // The two 2-port/2-layer PMIs: theta = 1 and theta = j ([layer][port])
  const c16_t pmis[2][2][2] = {
      {{{C, 0}, {C, 0}}, {{C, 0}, {static_cast<int16_t>(-C), 0}}}, // theta = 1
      {{{C, 0}, {0, C}}, {{C, 0}, {0, static_cast<int16_t>(-C)}}}, // theta = j
  };

  for (int p = 0; p < 2; ++p) {
    c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS];
    for (int layer = 0; layer < n_layers; ++layer)
      for (int port = 0; port < 2; ++port)
        weights[layer][port] = pmis[p][layer][port];

    std::vector<c16_t> out_ref(2 * symbol_size, {0, 0});
    std::vector<c16_t> out_2x2(2 * symbol_size, {0, 0});
    c16_t(*ref)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(out_ref.data());
    c16_t(*bf)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(out_2x2.data());

    for (int ant = 0; ant < 2; ++ant)
      nr_layer_precoder_simd(n_layers, symbol_size, dataF_in, ant, weights, 0, re_cnt, ref[ant]);

    nr_layer_precoder_2x2_simd(symbol_size, dataF_in, weights, 0, re_cnt, bf[0], bf[1]);

    for (int ant = 0; ant < 2; ++ant) {
      for (int sym = 0; sym < re_cnt; ++sym) {
        EXPECT_C16_NEAR(ref[ant][sym].r, bf[ant][sym].r, n_layers)
            << " PMI " << p << " ant " << ant << " sym " << sym << " real " << ref[ant][sym].r << " vs " << bf[ant][sym].r;
        EXPECT_C16_NEAR(ref[ant][sym].i, bf[ant][sym].i, n_layers)
            << " PMI " << p << " ant " << ant << " sym " << sym << " imag " << ref[ant][sym].i << " vs " << bf[ant][sym].i;
      }
    }
  }
}

// Build a 4-port, rank-r codebook entry of the cross-polar form the fast paths assume:
// ports {p, p+2} are the two polarisations of one co-polar element and differ, per layer,
// by a co-phasing phi_l in {+1,-1,+j,-j}. W[l][p+2] is computed from W[l][p], as the real
// codebook does, so the two are rounded independently -- which is why the comparison below
// tolerates a few LSB rather than requiring bit-exactness.
static void make_xpol_weights(int n_layers,
                              int16_t amp,
                              const int *phi, // phi_l: 0 = +1, 1 = -1, 2 = +j, 3 = -j
                              c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS],
                              bool phi_swap[NR_MAX_NB_LAYERS],
                              bool phi_neg[NR_MAX_NB_LAYERS])
{
  for (int l = 0; l < n_layers; ++l) {
    for (int p = 0; p < 2; ++p) {
      // an arbitrary but non-degenerate beam weight for the first polarisation
      const c16_t w = (l + p) % 2 ? (c16_t){0, amp} : (c16_t){amp, 0};
      weights[l][p] = w;
      switch (phi[l]) {
        case 0:
          weights[l][p + 2] = w;
          break;
        case 1:
          weights[l][p + 2] = (c16_t){(int16_t)-w.r, (int16_t)-w.i};
          break;
        case 2:
          weights[l][p + 2] = (c16_t){(int16_t)-w.i, w.r};
          break;
        default:
          weights[l][p + 2] = (c16_t){w.i, (int16_t)-w.r};
          break;
      }
    }
    phi_swap[l] = phi[l] >= 2;
    phi_neg[l] = (phi[l] & 1) != 0;
  }
}

// Compare both cross-polar kernels against the generic per-antenna kernel over every
// co-phasing pattern, at each supported rank.
TEST(NrLayerPrecoderTest, Compare_Xpol_4port_SIMD)
{
  constexpr int symbol_size = 24;
  constexpr int in_max = 20000; // r terms of amp/sqrt(2r) each stay inside int16

  // Real allocations are a multiple of NR_NB_SC_PER_RB = 12. 24 REs exercise the widest
  // vector tier, 12 leave a remainder for the narrower ones to mop up, so both the main
  // loop and the tier cascade are covered.
  for (int re_cnt : {12, 24})
    for (int n_layers = 2; n_layers <= 4; ++n_layers) {
      const int16_t amp = (int16_t)(SHRT_MAX / sqrt(2.0 * n_layers));

      std::vector<c16_t> buffer_in(n_layers * symbol_size);
      for (int i = 0; i < n_layers * symbol_size; ++i)
        buffer_in[i] = {static_cast<int16_t>((rand() % (2 * in_max + 1)) - in_max),
                        static_cast<int16_t>((rand() % (2 * in_max + 1)) - in_max)};
      c16_t(*dataF_in)[] = reinterpret_cast<c16_t(*)[]>(buffer_in.data());

      // every co-phasing pattern: 4^n_layers is small enough to enumerate exhaustively
      int n_patterns = 1;
      for (int l = 0; l < n_layers; ++l)
        n_patterns *= 4;

      for (int pat = 0; pat < n_patterns; ++pat) {
        int phi[NR_MAX_NB_LAYERS] = {0};
        for (int l = 0, q = pat; l < n_layers; ++l, q /= 4)
          phi[l] = q % 4;

        c16_t weights[NR_MAX_NB_LAYERS][NR_MAX_CSI_PORTS];
        bool phi_swap[NR_MAX_NB_LAYERS], phi_neg[NR_MAX_NB_LAYERS];
        make_xpol_weights(n_layers, amp, phi, weights, phi_swap, phi_neg);

        std::vector<c16_t> out_ref(4 * symbol_size, {0, 0});
        std::vector<c16_t> out_fast(4 * symbol_size, {0, 0});
        c16_t(*ref)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(out_ref.data());
        c16_t(*fast)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(out_fast.data());

        for (int ant = 0; ant < 4; ++ant)
          nr_layer_precoder_simd(n_layers, symbol_size, dataF_in, ant, weights, 0, re_cnt, ref[ant]);

        for (int p = 0; p < 2; ++p)
          nr_layer_precoder_Nx4_simd(n_layers,
                                     symbol_size,
                                     dataF_in,
                                     weights,
                                     p,
                                     phi_swap,
                                     phi_neg,
                                     0,
                                     re_cnt,
                                     fast[p],
                                     fast[p + 2]);

        for (int ant = 0; ant < 4; ++ant) {
          for (int sym = 0; sym < re_cnt; ++sym) {
            EXPECT_C16_NEAR(ref[ant][sym].r, fast[ant][sym].r, n_layers)
                << " Nx4 layers " << n_layers << " pattern " << pat << " ant " << ant << " sym " << sym;
            EXPECT_C16_NEAR(ref[ant][sym].i, fast[ant][sym].i, n_layers)
                << " Nx4 layers " << n_layers << " pattern " << pat << " ant " << ant << " sym " << sym;
          }
        }

      // The rank-2 specialisation covers the subset of the codebook where layer 1 takes
      // -phi against layer 0's +phi, and phi is restricted to {+1, +j}. Dispatched on
      // ARMv8.0 only, so that is where it is checked; ARMv8.1+ and x86 serve rank 2 from
      // the general kernel checked above.
#if defined(__aarch64__) && !defined(__ARM_FEATURE_QRDMX)
        if (n_layers == 2 && !phi_neg[0] && phi_swap[1] == phi_swap[0] && phi_neg[1]) {
          std::vector<c16_t> out_2x4(4 * symbol_size, {0, 0});
          c16_t(*bf)[symbol_size] = reinterpret_cast<c16_t(*)[symbol_size]>(out_2x4.data());
          for (int p = 0; p < 2; ++p)
            nr_layer_precoder_2x4_simd(symbol_size, dataF_in, weights, p, phi_swap[0], phi_neg[0], 0, re_cnt, bf[p], bf[p + 2]);
          for (int ant = 0; ant < 4; ++ant) {
            for (int sym = 0; sym < re_cnt; ++sym) {
              EXPECT_C16_NEAR(ref[ant][sym].r, bf[ant][sym].r, n_layers)
                  << " 2x4 pattern " << pat << " ant " << ant << " sym " << sym;
              EXPECT_C16_NEAR(ref[ant][sym].i, bf[ant][sym].i, n_layers)
                  << " 2x4 pattern " << pat << " ant " << ant << " sym " << sym;
            }
          }
        }
#endif // __aarch64__ && !__ARM_FEATURE_QRDMX
      }
    }
}

int main(int argc, char **argv)
{
  // Initialize random seed
  srand(time(0));

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
