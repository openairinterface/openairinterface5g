/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * PUSCH delay and TA estimation at high input level.
 *
 * The LS estimate of a channel without delay is real and positive on the occupied bins, so the exact IDFT has its peak
 * at bin 0: nr_est_delay() must return delay 0 and get_pusch_ta() the neutral TA 31.
 */
#include <stdio.h>
#include <string.h>
#include "PHY/defs_gNB.h"
#include "PHY/NR_TRANSPORT/nr_ulsch.h"
#include "nr_phy_common.h"

// get_pusch_ta() shares its object file with nr_fill_ulsch(), which needs this scheduler function; not called here
void beam_index_allocation(uint16_t fapi_beam_index,
                           int ant,
                           int num_ports,
                           int symbols_per_slot,
                           int slot,
                           uint16_t bitmap_symbols,
                           int num_ant_max,
                           uint16_t **ant_beam_id_list)
{
  abort();
}

static int check(PHY_VARS_gNB *gNB, int n, int used, int hi, int lo, const char *name)
{
  c16_t *x = malloc16_clear(n * sizeof(*x));
  c16_t *y = malloc16_clear(n * sizeof(*y));
  for (int k = 0; k < used; k++)
    x[k].r = (k & 1) ? lo : hi;

  NR_gNB_PUSCH pusch;
  memset(&pusch, 0, sizeof(pusch));
  nr_est_delay(n, x, y, &pusch.delay);
  gNB->frame_parms.ofdm_symbol_size = n;
  const int ta = get_pusch_ta(gNB, NULL, &pusch, 0, 0);
  const int ok = pusch.delay.valid && pusch.delay.est_delay == 0 && ta == 31;
  printf("size %4d %-14s hi %5d lo %5d: shift %d delay %5d valid %d TA %2d %s\n",
         n,
         name,
         hi,
         lo,
         nr_idft_prescale_shift(x, n),
         pusch.delay.est_delay,
         pusch.delay.valid,
         ta,
         ok ? "ok" : "FAIL");
  free(x);
  free(y);
  return !ok;
}

// at normal level the prescale must not change anything: same IDFT output as the direct call
static int check_identity(int n, int used, int16_t a)
{
  c16_t *x = malloc16_clear(n * sizeof(*x));
  c16_t *y = malloc16_clear(n * sizeof(*y));
  c16_t *ref = malloc16_clear(n * sizeof(*ref));
  for (int k = 0; k < used; k++)
    x[k] = (c16_t){(k & 3) ? a : -a, (k & 1) ? a / 2 : -a / 3};
  delay_t delay = {0};
  nr_est_delay(n, x, y, &delay);
  idft(get_idft(n), (int16_t *)x, (int16_t *)ref, 1);
  const int shift = nr_idft_prescale_shift(x, n);
  const int ok = shift == 0 && memcmp(y, ref, n * sizeof(*y)) == 0;
  printf("size %4d identity a %5d: shift %d %s\n", n, a, shift, ok ? "ok" : "FAIL");
  free(x);
  free(y);
  free(ref);
  return !ok;
}

// at high level the shift is not 0: delay_max_val must still order two antennas by peak power (it is compared across
// antennas to pick the strongest) and keep the running maximum over consecutive calls
static int check_strongest(int n, int used)
{
  c16_t *x = malloc16_clear(n * sizeof(*x));
  c16_t *y = malloc16_clear(n * sizeof(*y));
  delay_t weak = {0}, strong = {0}, carry = {0};
  for (int k = 0; k < used; k++)
    x[k].r = 12000;
  nr_est_delay(n, x, y, &weak);
  nr_est_delay(n, x, y, &carry);
  for (int k = 0; k < used; k++)
    x[k].r = 24000;
  nr_est_delay(n, x, y, &strong);
  nr_est_delay(n, x, y, &carry);
  const int ok = strong.delay_max_val > weak.delay_max_val && carry.delay_max_val == strong.delay_max_val;
  printf("size %4d strongest antenna: weak %lld strong %lld carried %lld %s\n",
         n,
         (long long)weak.delay_max_val,
         (long long)strong.delay_max_val,
         (long long)carry.delay_max_val,
         ok ? "ok" : "FAIL");
  free(x);
  free(y);
  return !ok;
}

int main(void)
{
  load_dftslib();
  PHY_VARS_gNB *gNB = calloc(1, sizeof(*gNB));
  int ret = 0;

  // 1272 occupied bins of 2048
  ret |= check(gNB, 2048, 1272, 3000, 3000, "flat");
  ret |= check(gNB, 2048, 1272, 2100, 200, "alternating");

  // same shapes from normal level up to full scale, on the usual PUSCH FFT sizes
  const int sizes[] = {512, 1024, 1536, 2048, 3072, 4096};
  for (int s = 0; s < sizeofArray(sizes); s++) {
    const int n = sizes[s];
    const int used = n * 1272 / 2048;
    for (int a = 250; a <= 32000; a *= 2) {
      ret |= check(gNB, n, used, a, a, "flat");
      ret |= check(gNB, n, used, a, a * 2 / 21, "alternating");
    }
    ret |= check(gNB, n, used, 32767, 32767, "flat");
    ret |= check_identity(n, used, 200);
  }
  ret |= check_strongest(2048, 1272);
  free(gNB);
  return ret;
}
