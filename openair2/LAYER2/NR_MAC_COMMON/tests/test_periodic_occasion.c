/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "executables/softmodem-common.h"

softmodem_params_t *get_softmodem_params(void)
{
  return NULL;
}

int main(void)
{
  // Test that period <= 0 returns false without divide-by-zero (SIGFPE)
  assert(!is_periodic_occasion(0, 0, 10, 0, 0));
  assert(!is_periodic_occasion(0, 5, 20, 0, 0));
  assert(!is_periodic_occasion(1, 0, 10, -1, 0));
  assert(!is_periodic_occasion(5, 5, 20, -5, 2));

  // Test valid periodic occasions
  assert(is_periodic_occasion(0, 0, 10, 10, 0));
  assert(is_periodic_occasion(1, 0, 10, 10, 0));
  assert(is_periodic_occasion(0, 5, 10, 10, 5));
  assert(!is_periodic_occasion(0, 4, 10, 10, 5));

  // Test csi_period_offset with NULL configurations
  int period = -1, offset = -1;
  bool res = csi_period_offset(NULL, NULL, &period, &offset);
  assert(!res);
  assert(period == 0);
  assert(offset == 0);

  printf("All periodic occasion unit tests passed.\n");
  return 0;
}
