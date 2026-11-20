/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_UE_PHY_MEAS__H__
#define __NR_UE_PHY_MEAS__H__
#include "time_meas.h"
#include "utils.h"

#define NOOP(a) a
#define FOREACH_NR_PHY_CPU_MEAS(FN)                                                                                                \
  FN(RX_PDSCH_STATS),\
  FN(DLSCH_RX_PDCCH_STATS),\
  FN(RX_FO_COMPENSATION_STATS),\
  FN(RX_DFT_STATS),\
  FN(DLSCH_CHANNEL_ESTIMATION_STATS),\
  FN(DLSCH_DECODING_STATS),\
  FN(DLSCH_LDPC_DECODING_STATS),\
  FN(DLSCH_EXTRACT_RBS_STATS),\
  FN(DLSCH_CHANNEL_SCALE_STATS),\
  FN(DLSCH_CHANNEL_LEVEL_STATS),\
  FN(DLSCH_MRC_MMSE_STATS),\
  FN(DLSCH_UNSCRAMBLING_STATS),\
  FN(DLSCH_CHANNEL_COMPENSATION_STATS),\
  FN(DLSCH_LLR_STATS),\
  FN(DLSCH_LAYER_DEMAPPING),\
  FN(DLSCH_LLR_LAYER_DEMAP_STATS),\
  FN(PHY_RX_PDCCH_STATS),\
  FN(DLSCH_PROCEDURES_STATS),\
  FN(PHY_PROC_TX),\
  FN(PUSCH_PROC_STATS),\
  FN(UCI_ON_PUSCH_MAPPING),\
  FN(ULSCH_LDPC_ENCODING_STATS),\
  FN(ULSCH_ENCODING_STATS),\
  FN(OFDM_MOD_STATS),\
  FN(PRACH_GEN_STATS),\
  FN(TRS_PROCESSING)

typedef enum {
  FOREACH_NR_PHY_CPU_MEAS(NOOP),
  MAX_CPU_STAT_TYPE
} nr_ue_phy_cpu_stat_type_t;

typedef struct nr_ue_phy_cpu_stat_t {
  time_stats_t cpu_time_stats[MAX_CPU_STAT_TYPE];
} nr_ue_phy_cpu_stat_t;

void init_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats);
void reset_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats);
void free_nr_ue_phy_cpu_stats(nr_ue_phy_cpu_stat_t *ue_phy_cpu_stats);

#endif
