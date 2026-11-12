/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

static int check_power(const char *name, const c16_t pattern[4], int bwp_start)
{
  enum { carrier_n_rb = 36, bwp_size = 32, n_rb = 24, fft_size = 512, samples_per_slot = 14 * fft_size };
  PHY_VARS_NR_UE *ue = calloc(1, sizeof(*ue));
  if (ue == NULL) {
    fprintf(stderr, "%s: cannot allocate UE\n", name);
    return 1;
  }

  nr_csi_info_t csi_info = {0};
  ue->nr_csi_info = &csi_info;
  ue->frame_parms.nb_antennas_rx = 1;
  ue->frame_parms.N_RB_DL = carrier_n_rb;
  ue->frame_parms.ofdm_symbol_size = fft_size;
  ue->frame_parms.samples_per_slot_wCP = samples_per_slot;

  // Pattern 1 (TS 38.331 section 6.3.2, CSI-IM-Resource): four subcarriers in one symbol.
  // Offsets 4..7 avoid the DC exclusion, giving exactly 96 measured REs.
  const fapi_nr_dl_config_csiim_pdu_rel15_t pdu = {
      .bwp_size = bwp_size,
      .bwp_start = bwp_start,
      .start_rb = bwp_start,
      .nr_of_rbs = n_rb,
      .k_csiim = {4, 5, 6, 7},
      .l_csiim = {0, 0, 0, 0},
  };
  c16_t rxdataF[1][samples_per_slot] = {0};
  for (int sc = 0; sc < fft_size; sc++)
    rxdataF[0][sc] = (c16_t){1234, -1234};
  c16_t samples[4 * n_rb];
  for (int rb = 0; rb < n_rb; rb++) {
    for (int k = 0; k < 4; k++) {
      c16_t sample = pattern[k];
      // Vary energy by RB so incorrect resource bounds cannot preserve the average.
      if (bwp_start != 0) {
        sample.r += 2 * rb;
        sample.i -= 2 * rb;
      }
      samples[4 * rb + k] = sample;
      // startingRB is relative to CRB0 (TS 38.331 section 6.3.2, CSI-FrequencyOccupation).
      rxdataF[0][(pdu.start_rb + rb) * NR_NB_SC_PER_RB + pdu.k_csiim[k]] = sample;
    }
  }
  const uint32_t expected = reference_power(samples, 4 * n_rb);

  nr_ue_csi_im_procedures(ue, rxdataF, &pdu);
  const int failed = !csi_info.csi_im_meas_computed || csi_info.interference_plus_noise_power != expected;
  printf("%s: %s, expected=%" PRIu32 ", actual=%" PRIu32 ", measured=%d\n",
         failed ? "FAIL" : "PASS",
         name,
         expected,
         csi_info.interference_plus_noise_power,
         csi_info.csi_im_meas_computed);
  free(ue);
  return failed;
}

int main(void)
{
  int failures = 0;
  failures += check_power("zero", (c16_t[]){{0, 0}, {0, 0}, {0, 0}, {0, 0}}, 0);
  failures += check_power("low amplitude", (c16_t[]){{100, 50}, {-100, 50}, {100, -50}, {-100, -50}}, 0);
  failures += check_power("nonzero mean", (c16_t[]){{100, -50}, {200, -50}, {100, 50}, {200, 50}}, 0);

  // Each component's sum of squares is 96 * 5000^2 = 2,400,000,000 > INT32_MAX.
  // The expected variances remain representable in the uint32_t output.
  failures += check_power("real accumulation", (c16_t[]){{5000, 0}, {-5000, 0}, {5000, 0}, {-5000, 0}}, 0);
  failures += check_power("imaginary accumulation", (c16_t[]){{0, 5000}, {0, 5000}, {0, -5000}, {0, -5000}}, 0);
  failures += check_power("complex accumulation", (c16_t[]){{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 0);
  failures += check_power("constant signal", (c16_t[]){{5000, 5000}, {5000, 5000}, {5000, 5000}, {5000, 5000}}, 0);
  failures += check_power("large nonzero mean", (c16_t[]){{6000, -50}, {-4000, -50}, {6000, 50}, {-4000, 50}}, 0);
  failures +=
      check_power("full-scale complex signal", (c16_t[]){{32767, 32767}, {-32767, 32767}, {32767, -32767}, {-32767, -32767}}, 0);
  failures +=
      check_power("minimum int16 constant", (c16_t[]){{-32768, -32768}, {-32768, -32768}, {-32768, -32768}, {-32768, -32768}}, 0);
  failures += check_power("nonzero BWP start", (c16_t[]){{5000, 5000}, {-5000, 5000}, {5000, -5000}, {-5000, -5000}}, 4);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
