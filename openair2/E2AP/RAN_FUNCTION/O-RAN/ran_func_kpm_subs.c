/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "ran_func_kpm_subs.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

#include <search.h>

#include "openair2/RRC/NR/rrc_gNB_UE_context.h"

e2_node_level_stats_t cp_node_level_stats(const e2_node_level_stats_t *src)
{
  e2_node_level_stats_t dst = {
    .mac_stats.dl.total_prb_aggregate = src->mac_stats.dl.total_prb_aggregate,
    .mac_stats.dl.used_prb_aggregate = src->mac_stats.dl.used_prb_aggregate,
    .mac_stats.ul.total_prb_aggregate = src->mac_stats.ul.total_prb_aggregate,
    .mac_stats.ul.used_prb_aggregate = src->mac_stats.ul.used_prb_aggregate,
    .rrc_conn_count_sum = src->rrc_conn_count_sum,
    .rrc_conn_count_samples = src->rrc_conn_count_samples,
  };

  return dst;
}

/* measurements that need to store values from previous reporting period have a limitation
   when it comes to multiple subscriptions to the same UEs; ric_req_id is unique per subscription */
typedef struct uldlcounter {
  uint32_t dl;
  uint32_t ul;
} uldlcounter_t;

static uldlcounter_t last_pdcp_sdu_total_bytes[MAX_MOBILES_PER_GNB] = {0};

static nr_pdcp_statistics_t get_pdcp_stats_per_drb(const uint32_t rrc_ue_id, const int rb_id)
{
  nr_pdcp_statistics_t pdcp = {0};
  const int srb_flag = 0;

  // Get PDCP stats for specific DRB
  const bool rc = nr_pdcp_get_statistics(rrc_ue_id, srb_flag, rb_id, &pdcp);
  assert(rc == true && "Cannot get PDCP stats\n");

  return pdcp;
}

/* 3GPP TS 28.522 - section 5.1.2.1.1.1
  note: this measurement is calculated as per spec */
static meas_record_lst_t fill_DRB_PdcpSduVolumeDL(__attribute__((unused)) const label_info_lst_t label,
                                                  __attribute__((unused)) uint32_t gran_period_ms,
                                                  cudu_ue_info_pair_t ue_info,
                                                  const size_t ue_idx,
                                                  __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  // Get PDCP stats per DRB
  const int rb_id = 1;  // at the moment, only 1 DRB is supported
  nr_pdcp_statistics_t pdcp = get_pdcp_stats_per_drb(ue_info.rrc_ue_id, rb_id);

  meas_record.value = INTEGER_MEAS_VALUE;

  // Get DL data volume delivered to PDCP layer
  meas_record.int_val = (pdcp.rxsdu_bytes - last_pdcp_sdu_total_bytes[ue_idx].dl)*8/1000000;   // [Mb]
  last_pdcp_sdu_total_bytes[ue_idx].dl = pdcp.rxsdu_bytes;

  return meas_record;
}

/* 3GPP TS 28.522 - section 5.1.2.1.2.1
  note: this measurement is calculated as per spec */
static meas_record_lst_t fill_DRB_PdcpSduVolumeUL(__attribute__((unused)) const label_info_lst_t label,
                                                  __attribute__((unused)) uint32_t gran_period_ms,
                                                  cudu_ue_info_pair_t ue_info,
                                                  const size_t ue_idx,
                                                  __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  // Get PDCP stats per DRB
  const int rb_id = 1;  // at the moment, only 1 DRB is supported
  nr_pdcp_statistics_t pdcp = get_pdcp_stats_per_drb(ue_info.rrc_ue_id, rb_id);

  meas_record.value = INTEGER_MEAS_VALUE;

  // Get UL data volume delivered from PDCP layer
  meas_record.int_val = (pdcp.txsdu_bytes - last_pdcp_sdu_total_bytes[ue_idx].ul)*8/1000000;   // [Mb]
  last_pdcp_sdu_total_bytes[ue_idx].ul = pdcp.txsdu_bytes;

  return meas_record;
}

#if defined(NGRAN_GNB_DU)
/* L1M.SS-RSRP — 3GPP TS 28.552 - section 5.1.1.22.1 */
static meas_record_lst_t fill_L1M_SS_RSRP(const label_info_lst_t label,
                                          __attribute__((unused)) uint32_t gran_period_ms,
                                          __attribute__((unused)) cudu_ue_info_pair_t ue_info,
                                          __attribute__((unused)) const size_t ue_idx,
                                          __attribute__((unused)) e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  if (label.distBinX == NULL) {
    printf("[E2 AGENT][E2SM-KPM] L1M.SS-RSRP requested without distBinX label; "
           "TS 28.552 5.1.1.22.1 is a distribution with no aggregate value, reporting NO_VALUE\n");
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  if (*label.distBinX > NR_KPM_SS_RSRP_NB_LEVELS - 1) {
    printf("[E2 AGENT][E2SM-KPM] L1M.SS-RSRP: distBinX %u out of range [0..127], reporting NO_VALUE\n", *label.distBinX);
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }
  const uint32_t bin = *label.distBinX;

  const NR_du_stats_t* du_stats = &nr_mac_get_cell_by_idx(RC.nrmac[0], 0)->du_stats;

  uint64_t count = 0;
  if (label.ssbIndex != NULL) {
    const uint32_t ssb_wire = *label.ssbIndex;
    if (ssb_wire > NR_KPM_NB_SSB - 1) {
      printf("[E2 AGENT][E2SM-KPM] L1M.SS-RSRP: ssbIndex %u out of range [0..63], reporting NO_VALUE\n", ssb_wire);
      meas_record.value = NO_VALUE_MEAS_VALUE;
      return meas_record;
    }
    const uint32_t ssb = ssb_wire;
    count = du_stats->ss_rsrp_ssb_dist[ssb][bin];
  } else {
    for (int s = 0; s < NR_KPM_NB_SSB; s++)
      count += du_stats->ss_rsrp_ssb_dist[s][bin];
  }

  meas_record.value = INTEGER_MEAS_VALUE;
  meas_record.int_val = (int64_t)count;
  return meas_record;
}
#endif /* NGRAN_GNB_DU */

/* MR.NRScSSSINR — 3GPP TS 28.552 - section 5.1.1.32 */
static meas_record_lst_t fill_MR_NRScSSSINR(const label_info_lst_t label,
                                            __attribute__((unused)) uint32_t gran_period_ms,
                                            __attribute__((unused)) cudu_ue_info_pair_t ue_info,
                                            __attribute__((unused)) const size_t ue_idx,
                                            __attribute__((unused)) e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  if (label.distBinX == NULL) {
    printf("[E2 AGENT][E2SM-KPM] MR.NRScSSSINR requested without distBinX label; "
           "TS 28.552 5.1.1.32 is a distribution with no aggregate value, reporting NO_VALUE\n");
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  if (*label.distBinX > NR_KPM_SS_SINR_NB_LEVELS - 1) {
    printf("[E2 AGENT][E2SM-KPM] MR.NRScSSSINR: distBinX %u out of range [0..127], reporting NO_VALUE\n", *label.distBinX);
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }
  const uint32_t bin = *label.distBinX;

  meas_record.value = INTEGER_MEAS_VALUE;
  meas_record.int_val = RC.nrrrc[0]->ss_sinr_cell_dist[bin];
  return meas_record;
}

/* RRC.ConnMean — 3GPP TS 28.552 - section 5.1.1.4.1 */
static meas_record_lst_t fill_RRC_ConnMean(__attribute__((unused)) const label_info_lst_t label,
                                           __attribute__((unused)) uint32_t gran_period_ms,
                                           __attribute__((unused)) cudu_ue_info_pair_t ue_info,
                                           __attribute__((unused)) const size_t ue_idx,
                                           e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {.value = INTEGER_MEAS_VALUE};

  node_stats[1].rrc_conn_count_sum     = RC.nrrrc[0]->rrc_conn_count_sum;
  node_stats[1].rrc_conn_count_samples = RC.nrrrc[0]->rrc_conn_count_samples;

  const uint64_t d_sum     = node_stats[1].rrc_conn_count_sum     - node_stats[0].rrc_conn_count_sum;
  const uint64_t d_samples = node_stats[1].rrc_conn_count_samples - node_stats[0].rrc_conn_count_samples;

  if (d_samples > 0) {
    meas_record.int_val = (long)((d_sum + d_samples / 2) / d_samples);
    node_stats[0].rrc_conn_count_sum     = node_stats[1].rrc_conn_count_sum;
    node_stats[0].rrc_conn_count_samples = node_stats[1].rrc_conn_count_samples;
  } else if (node_stats[1].rrc_conn_count_samples > 0) {
    meas_record.int_val = (long)((node_stats[1].rrc_conn_count_sum + node_stats[1].rrc_conn_count_samples / 2)
                                 / node_stats[1].rrc_conn_count_samples);
  } else {
    meas_record.int_val = 0;
  }

  return meas_record;
}

#if defined (NGRAN_GNB_DU)
static uldlcounter_t last_rlc_pdu_total_bytes[MAX_MOBILES_PER_GNB] = {0};
static uldlcounter_t last_total_prbs[MAX_MOBILES_PER_GNB] = {0};

static nr_rlc_statistics_t get_rlc_stats_per_drb(const rnti_t rnti, const int rb_id)
{
  nr_rlc_statistics_t rlc = {0};
  const int srb_flag = 0;

  // Get RLC stats for specific DRB
  const bool rc = nr_rlc_get_statistics(rnti, srb_flag, rb_id, &rlc);
  assert(rc == true && "Cannot get RLC stats\n");

  // Activate average sojourn time at the RLC buffer for specific DRB
  nr_rlc_activate_avg_time_to_tx(rnti, rb_id+3, 1);
  
  return rlc;  
}

/* 3GPP TS 28.522 - section 5.1.3.3.3
  note: by default this measurement is calculated for previous 100ms (openair2/LAYER2/nr_rlc/nr_rlc_entity.c:118, 173, 213); please, update according to your needs */
static meas_record_lst_t fill_DRB_RlcSduDelayDl(__attribute__((unused)) const label_info_lst_t label,
                                                __attribute__((unused)) uint32_t gran_period_ms,
                                                cudu_ue_info_pair_t ue_info,
                                                __attribute__((unused))const size_t ue_idx,
                                                __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};
  
  // Get RLC stats per DRB
  const int rb_id = 1;  // at the moment, only 1 DRB is supported
  nr_rlc_statistics_t rlc = get_rlc_stats_per_drb(ue_info.ue->rnti, rb_id);

  meas_record.value = REAL_MEAS_VALUE;

  // Get the value of sojourn time at the RLC buffer
  meas_record.real_val = rlc.txsdu_avg_time_to_tx;  // [μs]

  return meas_record;
}

/* 3GPP TS 28.522 - section 5.1.1.3.1
  note: per spec, average UE throughput in DL (taken into consideration values from all UEs, and averaged)
        here calculated as: UE specific throughput in DL */
static meas_record_lst_t fill_DRB_UEThpDl(__attribute__((unused)) const label_info_lst_t label,
                                          uint32_t gran_period_ms,
                                          cudu_ue_info_pair_t ue_info,
                                          const size_t ue_idx,
                                          __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};
  
  const int rb_id = 1;  // at the moment, only 1 DRB is supported
  nr_rlc_statistics_t rlc = get_rlc_stats_per_drb(ue_info.ue->rnti, rb_id);
  meas_record.value = REAL_MEAS_VALUE;

  // Calculate DL Thp
  meas_record.real_val = (double)(rlc.txpdu_bytes - last_rlc_pdu_total_bytes[ue_idx].dl)*8/gran_period_ms;  // [kbps]
  last_rlc_pdu_total_bytes[ue_idx].dl = rlc.txpdu_bytes;

  return meas_record;
}

/* 3GPP TS 28.522 - section 5.1.1.3.3
  note: per spec, average UE throughput in UL (taken into consideration values from all UEs, and averaged)
        here calculated as: UE specific throughput in UL */
static meas_record_lst_t fill_DRB_UEThpUl(__attribute__((unused)) const label_info_lst_t label,
                                          uint32_t gran_period_ms,
                                          cudu_ue_info_pair_t ue_info,
                                          const size_t ue_idx,
                                          __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};
  
  // Get RLC stats per DRB
  const int rb_id = 1;  // at the moment, only 1 DRB is supported
  nr_rlc_statistics_t rlc = get_rlc_stats_per_drb(ue_info.ue->rnti, rb_id);

  meas_record.value = REAL_MEAS_VALUE;

  // Calculate UL Thp
  meas_record.real_val = (double)(rlc.rxpdu_bytes - last_rlc_pdu_total_bytes[ue_idx].ul)*8/gran_period_ms;  // [kbps]
  last_rlc_pdu_total_bytes[ue_idx].ul = rlc.rxpdu_bytes;
  
  return meas_record;
}

/* 3GPP TS 28.522 - section 5.1.1.2.1
  note: this measurement is calculated as per spec */
static meas_record_lst_t fill_RRU_PrbTotDl(__attribute__((unused)) const label_info_lst_t label,
                                           __attribute__((unused)) uint32_t gran_period_ms,
                                           cudu_ue_info_pair_t ue_info,
                                           const size_t ue_idx,
                                           e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};
  
  meas_record.value = INTEGER_MEAS_VALUE;

  // Get the number of DL PRBs
  const uint64_t tot_prb_dl_aggr = node_stats[1].mac_stats.dl.total_prb_aggregate - node_stats[0].mac_stats.dl.total_prb_aggregate;
  meas_record.int_val = (tot_prb_dl_aggr == 0) ? 0 : (ue_info.ue->mac_stats.dl.total_rbs - last_total_prbs[ue_idx].dl) * 100 / tot_prb_dl_aggr;   // [%]
  last_total_prbs[ue_idx].dl = ue_info.ue->mac_stats.dl.total_rbs;

  return meas_record;
}

/* 3GPP TS 28.522 - section 5.1.1.2.2
  note: this measurement is calculated as per spec */
static meas_record_lst_t fill_RRU_PrbTotUl(__attribute__((unused)) const label_info_lst_t label,
                                           __attribute__((unused)) uint32_t gran_period_ms,
                                           cudu_ue_info_pair_t ue_info,
                                           const size_t ue_idx,
                                           e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  meas_record.value = INTEGER_MEAS_VALUE;

  // Get the number of UL PRBs
  const uint64_t tot_prb_ul_aggr = node_stats[1].mac_stats.ul.total_prb_aggregate - node_stats[0].mac_stats.ul.total_prb_aggregate;
  meas_record.int_val = (tot_prb_ul_aggr == 0) ? 0 : (ue_info.ue->mac_stats.ul.total_rbs - last_total_prbs[ue_idx].ul) * 100 / tot_prb_ul_aggr;   // [%]
  last_total_prbs[ue_idx].ul = ue_info.ue->mac_stats.ul.total_rbs;

  return meas_record;
}

/* 3GPP TS 28.552 - section 5.1.1.12.1*/
static meas_record_lst_t fill_CARR_PDSCHMCSDist(const label_info_lst_t label,
                                                __attribute__((unused)) uint32_t gran_period_ms,
                                                __attribute__((unused)) cudu_ue_info_pair_t ue_info,
                                                __attribute__((unused)) const size_t ue_idx,
                                                __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  if (label.distBinX == NULL || label.distBinY == NULL || label.distBinZ == NULL) {
    printf("[E2 AGENT][E2SM-KPM] CARR.PDSCHMCSDist requested without distBinX/Y/Z labels; "
           "TS 28.552 5.1.1.12.1 defines no aggregate value, reporting NO_VALUE\n");
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  const uint32_t bin_x = *label.distBinX;
  const uint32_t bin_y = *label.distBinY;
  const uint32_t bin_z = *label.distBinZ;

  if (bin_x < 1 || bin_x > NR_KPM_MAX_LAYERS || bin_y < 1 || bin_y > NR_KPM_NB_MCS_TABLE_DL || bin_z > NR_KPM_NB_MCS - 1) {
    printf("[E2 AGENT][E2SM-KPM] CARR.PDSCHMCSDist: bin out of range (X=%u Y=%u Z=%u), reporting NO_VALUE\n",
           bin_x, bin_y, bin_z);
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  const NR_du_stats_t* du_stats = &nr_mac_get_cell_by_idx(RC.nrmac[0], 0)->du_stats;
  meas_record.value = INTEGER_MEAS_VALUE;
  meas_record.int_val = du_stats->pdsch_mcs_dist[bin_x - 1][bin_y - 1][bin_z];
  return meas_record;
}

/* CARR.PUSCHMCSDist — 3GPP TS 28.552 - section 5.1.1.12.2 */
static meas_record_lst_t fill_CARR_PUSCHMCSDist(const label_info_lst_t label,
                                                __attribute__((unused)) uint32_t gran_period_ms,
                                                __attribute__((unused)) cudu_ue_info_pair_t ue_info,
                                                __attribute__((unused)) const size_t ue_idx,
                                                __attribute__((unused))e2_node_level_stats_t* node_stats)
{
  meas_record_lst_t meas_record = {0};

  if (label.distBinX == NULL || label.distBinY == NULL || label.distBinZ == NULL) {
    printf("[E2 AGENT][E2SM-KPM] CARR.PUSCHMCSDist requested without distBinX/Y/Z labels; "
           "TS 28.552 5.1.1.12.2 defines no aggregate value, reporting NO_VALUE\n");
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  const uint32_t bin_x = *label.distBinX;
  const uint32_t bin_y = *label.distBinY;
  const uint32_t bin_z = *label.distBinZ;

  if (bin_x < 1 || bin_x > NR_KPM_MAX_LAYERS || bin_y > NR_KPM_NB_MCS_TABLE_UL - 1 || bin_z > NR_KPM_NB_MCS - 1) {
    printf("[E2 AGENT][E2SM-KPM] CARR.PUSCHMCSDist: bin out of range (X=%u Y=%u Z=%u), reporting NO_VALUE\n",
           bin_x, bin_y, bin_z);
    meas_record.value = NO_VALUE_MEAS_VALUE;
    return meas_record;
  }

  const NR_du_stats_t* du_stats = &nr_mac_get_cell_by_idx(RC.nrmac[0], 0)->du_stats;
  meas_record.value = INTEGER_MEAS_VALUE;
  meas_record.int_val = du_stats->pusch_mcs_dist[bin_x - 1][bin_y][bin_z];
  return meas_record;
}
#endif

static kv_measure_t lst_measure[] = {
  {.key = "DRB.PdcpSduVolumeDL", .value = fill_DRB_PdcpSduVolumeDL },
  {.key = "DRB.PdcpSduVolumeUL", .value = fill_DRB_PdcpSduVolumeUL },
  {.key = "MR.NRScSSSINR",       .value = fill_MR_NRScSSSINR },
  {.key = "RRC.ConnMean",        .value = fill_RRC_ConnMean },
#if defined(NGRAN_GNB_DU)
  {.key = "L1M.SS-RSRP",         .value = fill_L1M_SS_RSRP },
  {.key = "DRB.RlcSduDelayDl", .value =  fill_DRB_RlcSduDelayDl }, 
  {.key = "DRB.UEThpDl", .value =  fill_DRB_UEThpDl }, 
  {.key = "DRB.UEThpUl", .value =  fill_DRB_UEThpUl }, 
  {.key = "RRU.PrbTotDl", .value =  fill_RRU_PrbTotDl }, 
  {.key = "RRU.PrbTotUl", .value =  fill_RRU_PrbTotUl }, 
  {.key = "CARR.PDSCHMCSDist", .value = fill_CARR_PDSCHMCSDist },
  {.key = "CARR.PUSCHMCSDist", .value = fill_CARR_PUSCHMCSDist },
#endif
}; 

void init_kpm_subs_data(void)
{
  const size_t ht_len = sizeof(lst_measure) / sizeof(lst_measure[0]);
  hcreate(ht_len);

  ENTRY kv_pair;

  for (size_t i = 0; i < ht_len; i++) {
    kv_pair.key = lst_measure[i].key;
    kv_pair.data = &lst_measure[i];
    hsearch(kv_pair, ENTER);
  }
}

meas_record_lst_t get_kpm_meas_value(char* kpm_meas_name, const label_info_lst_t label, uint32_t gran_period_ms, cudu_ue_info_pair_t ue_info, const size_t ue_idx, e2_node_level_stats_t* node_stats)
{
  assert(kpm_meas_name != NULL);

  ENTRY search_entry = {.key = kpm_meas_name};
  ENTRY *found_entry = hsearch(search_entry, FIND);
  assert(found_entry != NULL && "Unsupported KPM measurement name");

  kv_measure_t *kv_found = (kv_measure_t *)found_entry->data;
  meas_record_lst_t meas_record = kv_found->value(label, gran_period_ms, ue_info, ue_idx, node_stats);

  return meas_record;
}
