/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include "nr_ue_phy_meas.h"
#include <stdbool.h>

void init_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats) {
  reset_nr_ue_phy_cpu_stats(ue_phy_cpu_stats);
  const char* cpu_stats_enum_to_string_table[] = {
    FOREACH_NR_PHY_CPU_MEAS(TO_STRING)
  };
  for (int i = 0; i < MAX_CPU_STAT_TYPE; i++) {
    ue_phy_cpu_stats->cpu_time_stats[i].meas_name = strdup(cpu_stats_enum_to_string_table[i]);
  }
}

void reset_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats) {
  for (int i = 0; i < MAX_CPU_STAT_TYPE; i++) {
    reset_meas(&ue_phy_cpu_stats->cpu_time_stats[i]);
  }
}

void free_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats)
{
  for (int i = 0; i < MAX_CPU_STAT_TYPE; i++)
    free_and_zero(ue_phy_cpu_stats->cpu_time_stats[i].meas_name);
}
