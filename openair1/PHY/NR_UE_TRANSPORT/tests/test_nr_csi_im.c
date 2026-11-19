/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SCHED_NR_UE/defs.h"
#include "radio/COMMON/common_lib.h"

// CSI-RS dependencies share the production object but are not exercised by CSI-IM.
openair0_config_t openair0_cfg_g[MAX_CARDS];

void nr_fill_rx_indication(fapi_nr_rx_indication_t *rx_ind,
                           uint8_t pdu_type,
                           PHY_VARS_NR_UE *ue,
                           int cw_idx,
                           int harq_pid,
                           NR_UE_DLSCH_t *dlsch,
                           const UE_nr_rxtx_proc_t *proc,
                           void *typeSpecific)
{
  AssertFatal(false, "Unexpected RX indication in CSI-IM test\n");
}

void trs_freq_correction(PHY_VARS_NR_UE *ue, int cfo)
{
  AssertFatal(false, "Unexpected frequency correction in CSI-IM test\n");
}

static uint32_t reference_power(const c16_t *samples, int count)
{
  double mean_re = 0;
  double mean_im = 0;
  for (int i = 0; i < count; i++) {
    mean_re += samples[i].r;
    mean_im += samples[i].i;
  }
  mean_re /= count;
  mean_im /= count;
  // Integral fixture means avoid changing the estimator's existing integer-rounding semantics.
  DevAssert(mean_re == trunc(mean_re) && mean_im == trunc(mean_im));

  double power_re = 0;
  double power_im = 0;
  for (int i = 0; i < count; i++) {
    const double re = samples[i].r - mean_re;
    const double im = samples[i].i - mean_im;
    power_re += re * re;
    power_im += im * im;
  }
  return (uint32_t)(power_re / count) + (uint32_t)(power_im / count);
}

typedef struct {
  const char *name;
  c16_t pattern[4];
  uint16_t bwp_start;
  uint16_t bwp_size;
  uint16_t start_rb;
  uint16_t nr_of_rbs;
  int expected_start_rb;
  int expected_nr_of_rbs;
} csi_im_test_case_t;

static int check_power(const csi_im_test_case_t *test)
{
  enum { carrier_n_rb = 64, fft_size = 1024, samples_per_slot = 14 * fft_size };
  PHY_VARS_NR_UE *ue = calloc(1, sizeof(*ue));
  if (ue == NULL) {
    fprintf(stderr, "%s: cannot allocate UE\n", test->name);
    return 1;
  }

  const bool expected_measured = test->expected_nr_of_rbs > 0;
  // Empty resources must clear a previous measurement's validity without replacing its power.
  nr_csi_info_t csi_info = {.csi_im_meas_computed = !expected_measured, .interference_plus_noise_power = UINT32_MAX};
  ue->nr_csi_info = &csi_info;
  ue->frame_parms.nb_antennas_rx = 1;
  ue->frame_parms.N_RB_DL = carrier_n_rb;
  ue->frame_parms.ofdm_symbol_size = fft_size;
  ue->frame_parms.samples_per_slot_wCP = samples_per_slot;

  // Pattern 1 (TS 38.331 section 6.3.2, CSI-IM-Resource): four subcarriers in one symbol.
  // Offsets 4..7 avoid the DC exclusion, giving four measured REs per RB.
  const fapi_nr_dl_config_csiim_pdu_rel15_t pdu = {
      .bwp_size = test->bwp_size,
      .bwp_start = test->bwp_start,
      .start_rb = test->start_rb,
      .nr_of_rbs = test->nr_of_rbs,
      .k_csiim = {4, 5, 6, 7},
      .l_csiim = {0, 0, 0, 0},
  };
  c16_t rxdataF[1][samples_per_slot] = {0};
  for (int sc = 0; sc < fft_size; sc++)
    rxdataF[0][sc] = (c16_t){1234, -1234};
  c16_t samples[4 * carrier_n_rb];
  DevAssert(test->expected_start_rb + test->expected_nr_of_rbs <= carrier_n_rb);
  for (int rb = 0; rb < test->expected_nr_of_rbs; rb++) {
    for (int k = 0; k < 4; k++) {
      c16_t sample = test->pattern[k];
      // Vary energy by RB so incorrect resource bounds cannot preserve the average.
      if (test->bwp_start != 0) {
        sample.r += 2 * rb;
        sample.i -= 2 * rb;
      }
      samples[4 * rb + k] = sample;
      // startingRB is relative to CRB0 (TS 38.331 section 6.3.2, CSI-FrequencyOccupation).
      rxdataF[0][(test->expected_start_rb + rb) * NR_NB_SC_PER_RB + pdu.k_csiim[k]] = sample;
    }
  }
  const uint32_t expected = expected_measured ? reference_power(samples, 4 * test->expected_nr_of_rbs) : UINT32_MAX;

  nr_ue_csi_im_procedures(ue, rxdataF, &pdu);
  const int failed = csi_info.csi_im_meas_computed != expected_measured || csi_info.interference_plus_noise_power != expected;
  printf("%s: %s, expected=%" PRIu32 ", actual=%" PRIu32 ", measured=%d\n",
         failed ? "FAIL" : "PASS",
         test->name,
         expected,
         csi_info.interference_plus_noise_power,
         csi_info.csi_im_meas_computed);
  free(ue);
  return failed;
}

int main(int argc, char **argv)
{
  const csi_im_test_case_t tests[] = {
      {"zero", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}, 0, 32, 0, 24, 0, 24},
      {"low amplitude", {{100, 50}, {-100, 50}, {100, -50}, {-100, -50}}, 0, 32, 0, 24, 0, 24},
      {"nonzero mean", {{100, -50}, {200, -50}, {100, 50}, {200, 50}}, 0, 32, 0, 24, 0, 24},
      // Each component's sum of squares is 96 * 5000^2 = 2,400,000,000 > INT32_MAX.
      {"real accumulation", {{5000, 0}, {-5000, 0}, {5000, 0}, {-5000, 0}}, 0, 32, 0, 24, 0, 24},
      {"imaginary accumulation", {{0, 5000}, {0, 5000}, {0, -5000}, {0, -5000}}, 0, 32, 0, 24, 0, 24},
      {"complex accumulation", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 0, 32, 0, 24, 0, 24},
      {"constant signal", {{5000, 5000}, {5000, 5000}, {5000, 5000}, {5000, 5000}}, 0, 32, 0, 24, 0, 24},
      {"large nonzero mean", {{6000, -50}, {-4000, -50}, {6000, 50}, {-4000, 50}}, 0, 32, 0, 24, 0, 24},
      {"full-scale complex signal", {{32767, 32767}, {-32767, 32767}, {32767, -32767}, {-32767, -32767}}, 0, 32, 0, 24, 0, 24},
      {"minimum int16 constant", {{-32768, -32768}, {-32768, -32768}, {-32768, -32768}, {-32768, -32768}}, 0, 32, 0, 24, 0, 24},
      {"nonzero BWP start", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 4, 32, 4, 24, 4, 24},
      {"BWP upper boundary", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 4, 24, 4, 24, 4, 24},
      {"BWP start equals size", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 24, 24, 24, 24, 24, 24},
      {"BWP start above size", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 32, 24, 32, 24, 32, 24},
      {"resource crosses BWP end", {{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 4, 24, 20, 24, 20, 8},
      {"resource at BWP end", {{0}}, 4, 24, 28, 24, 28, 0},
      {"resource beyond BWP end", {{0}}, 4, 24, 32, 24, 32, 0},
      {"zero-length resource", {{0}}, 4, 24, 4, 0, 4, 0},
      {"zero-size BWP", {{0}}, 4, 0, 4, 24, 4, 0},
  };
  if (argc > 2) {
    fprintf(stderr, "Usage: %s [test name]\n", argv[0]);
    return EXIT_FAILURE;
  }

  int failures = 0;
  int selected = 0;
  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    if (argc == 2 && strcmp(argv[1], tests[i].name) != 0)
      continue;
    selected++;
    failures += check_power(&tests[i]);
  }
  if (selected == 0) {
    fprintf(stderr, "Unknown test: %s\n", argv[1]);
    return EXIT_FAILURE;
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
