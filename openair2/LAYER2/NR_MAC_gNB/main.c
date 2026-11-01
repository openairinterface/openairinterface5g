/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "NR_DRB-ToAddMod.h"
#include "NR_DRB-ToAddModList.h"
#include "mac_proto.h"
#include "mac_rrc_ul.h"
#include "nr_mac_gNB.h"
#include "common/utils/utils.h"
#include "NR_PHY_INTERFACE/NR_IF_Module.h"
#include "NR_RLC-BearerConfig.h"
#include "NR_RadioBearerConfig.h"
#include "NR_ServingCellConfig.h"
#include "NR_ServingCellConfigCommon.h"
#include "NR_TAG.h"
#include "assertions.h"
#include "common/ngran_types.h"
#include "common/ran_context.h"
#include "executables/softmodem-common.h"
#include "linear_alloc.h"
#include "LAYER2/nr_pdcp/nr_pdcp_entity.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/lib/f1ap_interface_management.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "seq_arr.h"
#include "system.h"
#include "time_meas.h"
#include "utils.h"
#ifdef E3_AGENT
#include "openair2/E3AP/ran_func_spectrum_extern.h"
#endif /* E3_AGENT */

#define MACSTATSSTRLEN 36256

void *nrmac_stats_thread(void *arg) {

  gNB_MAC_INST *gNB = (gNB_MAC_INST *)arg;

  char output[MACSTATSSTRLEN] = {0};
  const char *end = output + MACSTATSSTRLEN;
  FILE *file = fopen("nrMAC_stats.log","w");
  if (!file) {
    LOG_W(NR_MAC, "Cannot open nrMAC_stats.log: %d, %s\n", errno, strerror(errno));
    return NULL;
  }

  while (oai_exit == 0) {
    char *p = output;
    NR_SCHED_LOCK(&gNB->sched_lock);
    for (int i = 0; i < NR_MAX_CELLS; i++) {
      nr_cell_sched_t *cell = &gNB->cells[i];
      if (!cell->common_channels.ServingCellConfigCommon)
        continue;
      p += snprintf(p, end - p, "=== Cell %d ===\n", i);
      p += dump_mac_stats(gNB, cell, p, end - p, false);
      p += snprintf(p, end - p, "\n");
      p += print_meas_log(&cell->gNB_scheduler, "gNB_scheduler", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->rx_ulsch_sdu, "rx_ulsch_sdu", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->schedule_dlsch, "dlsch scheduler", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->schedule_ulsch, "ulsch scheduler", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->schedule_ra, "RA scheduler", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->schedule_periodic, "periodic channels scheduler", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->rlc_data_req, "rlc_data_req", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->nr_srs_ri_computation_timer, "UL-RI computation time", NULL, NULL, p, end - p);
      p += print_meas_log(&cell->nr_srs_tpmi_computation_timer, "UL-TPMI computation time", NULL, NULL, p, end - p);
    }
    NR_SCHED_UNLOCK(&gNB->sched_lock);
    size_t len = p - output;
    if (fwrite(output, len, 1, file) != 1 || fflush(file) != 0) {
      LOG_E(NR_MAC, "error while writing nrMAC_stats.log: %d, %s\n", errno, strerror(errno));
      break;
    }
    sleep(1);
    if (ftruncate(fileno(file), 0) != 0 || fseek(file, 0, SEEK_SET) != 0) {
      LOG_E(NR_MAC, "error while writing nrMAC_stats.log: %d, %s\n", errno, strerror(errno));
      break;
    }
  }
  fclose(file);
  return NULL;
}

static char *st_append(char *start, const char *end, const char *format, ...)
{
  size_t space = end - start;
  va_list args;
  va_start(args, format);
  size_t written = vsnprintf(start, space, format, args);
  va_end(args);
  if (written <= space)
    return start + written;
  else
    return (char *)end;
}

size_t dump_mac_stats(gNB_MAC_INST *gNB, const nr_cell_sched_t *cell, char *output, size_t strlen, bool reset_rsrp)
{
  const char *begin = output;
  const char *end = output + strlen;

  FOR_EACH_CONNECTED_UE(&gNB->UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    NR_mac_stats_t *stats = &UE->mac_stats;
    const int avg_rsrp = stats->num_rsrp_meas > 0 ? stats->cumul_rsrp / (int)stats->num_rsrp_meas : 0;
    const int avg_sinrx10 = stats->num_sinr_meas > 0 ? stats->cumul_sinrx10 / (int)stats->num_sinr_meas : 0;

    output = st_append(output, end, "UE RNTI %04x CU-UE-ID ", UE->rnti);
    if (du_exists_f1_ue_data(UE->rnti)) {
      f1_ue_data_t ued = du_get_f1_ue_data(UE->rnti);
      output = st_append(output, end, "%d", ued.secondary_ue);
    } else {
      output = st_append(output, end, "(none)");
    }

    bool in_sync = !sched_ctrl->ul_failure;
    output = st_append(output,
                       end,
                       " %s PH %d dB PCMAX %d dBm",
                       in_sync ? "in-sync" : "out-of-sync",
                       sched_ctrl->ph,
                       sched_ctrl->pcmax);

    if (stats->num_rsrp_meas)
      output = st_append(output, end, ", average RSRP %d (%u meas)", avg_rsrp, stats->num_rsrp_meas);

    if (stats->num_sinr_meas) {
      output = st_append(output,
                         end,
                         ", average SINR %d.%d (%u meas)",
                         avg_sinrx10 / 10,
                         avg_sinrx10 % 10,
                         stats->num_sinr_meas);
    }

    output = st_append(output, end, "\n");

    bool csirep = sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.print_report;
    bool srsrep = stats->srs_stats[0] != '\0';
    if(csirep || srsrep) {
      output = st_append(output, end, "UE %04x:", UE->rnti);
      const struct CRI_RI_LI_PMI_CQI *r = &sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report;
      if (csirep)
        output = st_append(output, end, " CSI [CQI %d RI %d PMI (%d,%d)]", r->wb_cqi_1tb, r->ri + 1, r->pmi_x1, r->pmi_x2);
      if (srsrep)
        output = st_append(output, end, " SRS [%s]", stats->srs_stats);
      output = st_append(output, end, "\n");
    }

    output = st_append(output,
                       end,
                       "UE %04x: dlsch_rounds ", UE->rnti);
    output = st_append(output, end, "%"PRIu64, stats->dl.rounds[0]);
    for (int i = 1; i < cell->dl_bler.harq_round_max; i++)
      output = st_append(output, end, "/%"PRIu64, stats->dl.rounds[i]);

    float pucch_snr = nr_mac_get_snr(&sched_ctrl->pucch_pc);
    float pucch_snr_diff = (pucch_snr * 10.0f - sched_ctrl->pucch_pc.target_snrx10) / 10.0f;
    float pucch_rssi = nr_mac_get_rssi(&sched_ctrl->pucch_pc);
    output = st_append(output,
                       end,
                       ", dlsch_errors %" PRIu64
                       ", pucch0_DTX %d (SNR %.1f%+.1f) RSSI %.1f, BLER %.5f MCS (%d) %d (Qm %d) CCE fail %d\n",
                       stats->dl.errors,
                       stats->pucch0_DTX,
                       pucch_snr,
                       pucch_snr_diff,
                       pucch_rssi,
                       sched_ctrl->dl_bler_stats.bler,
                       UE->current_DL_BWP.mcsTableIdx,
                       sched_ctrl->dl_bler_stats.mcs,
                       nr_get_Qm_dl(sched_ctrl->dl_bler_stats.mcs, UE->current_DL_BWP.mcsTableIdx),
                       sched_ctrl->dl_cce_fail);
    if (reset_rsrp) {
      stats->num_rsrp_meas = 0;
      stats->cumul_rsrp = 0;
      stats->num_sinr_meas = 0;
      stats->cumul_sinrx10 = 0;
    }
    output = st_append(output,
                       end,
                       "UE %04x: ulsch_rounds ", UE->rnti);
    output = st_append(output, end, "%"PRIu64, stats->ul.rounds[0]);
    for (int i = 1; i < cell->ul_bler.harq_round_max; i++)
      output = st_append(output, end, "/%"PRIu64, stats->ul.rounds[i]);

    float snr = nr_mac_get_snr(&sched_ctrl->pusch_pc);
    float rssi = nr_mac_get_rssi(&sched_ctrl->pusch_pc);
    float diff_target = (snr * 10.0f - sched_ctrl->pusch_pc.target_snrx10) / 10.0f;
    output = st_append(
        output,
        end,
        ", ulsch_errors %" PRIu64
        ", ulsch_DTX %d, BLER %.5f MCS (%d) %d (Qm %d deltaMCS %d) NPRB %d SNR %.1f (%+.1f) RSSI %.1f CCE fail %d\n",
        stats->ul.errors,
        stats->ulsch_DTX,
        sched_ctrl->ul_bler_stats.bler,
        UE->current_UL_BWP.mcs_table,
        sched_ctrl->ul_bler_stats.mcs,
        nr_get_Qm_ul(sched_ctrl->ul_bler_stats.mcs, UE->current_UL_BWP.mcs_table),
        UE->mac_stats.deltaMCS,
        UE->mac_stats.NPRB,
        snr,
        diff_target,
        rssi,
        sched_ctrl->ul_cce_fail);

    // normally a UE should have at least one LCID, 1 in SA or 4 in NSA/phy-test
    output = st_append(output, end, "UE %04x: LCID ", UE->rnti);
    for (int i = 0; i < seq_arr_size(&sched_ctrl->lc_config); i++) {
      const nr_lc_config_t *c = seq_arr_at(&sched_ctrl->lc_config, i);
      output = st_append(output, end, "%d,", c->lcid);
    }
    float dl_thr = UE->dl_thr_ue_display / 1e6;
    float ul_thr = UE->ul_thr_ue_display / 1e6;
    output = st_append(output, end, " goodput DL %7.2f UL %7.2f Mbps\n",  dl_thr, ul_thr);
  }
  DevAssert(output <= end);
  return output - begin;
}

static void mac_rrc_init(gNB_MAC_INST *mac, ngran_node_t node_type)
{
  switch (node_type) {
    case ngran_gNB_CU:
      AssertFatal(1 == 0, "nothing to do for CU\n");
      break;
    case ngran_gNB_DU:
      mac_rrc_ul_f1ap_init(&mac->mac_rrc);
      break;
    case ngran_gNB:
      mac_rrc_ul_direct_init(&mac->mac_rrc);
      break;
    default:
      AssertFatal(0 == 1, "Unknown node type %d\n", node_type);
      break;
  }
}

gNB_MAC_INST *mac_top_init_gNB(ngran_node_t node_type, const nr_rlc_configuration_t *default_rlc_config, nr_cell_sched_t **cell_ptr)
{
  AssertFatal(RC.nb_nr_macrlc_inst == 1, "what is the point of calling %s() if you don't need exactly one MAC?\n", __func__);

  RC.nrmac = calloc_or_fail(1, sizeof(gNB_MAC_INST *));
  RC.nrmac[0] = calloc_or_fail(1, sizeof(gNB_MAC_INST));
  gNB_MAC_INST *nrmac = RC.nrmac[0];

  // TODO: handle multiple cells later, for now there's only one cell ever initialized and used
  // the current work only adds the structure and updates the references to use the cell pointer
  *cell_ptr = &nrmac->cells[0];
  nr_cell_sched_t *cell = *cell_ptr;
  nr_mac_pcch_queue_init(&cell->common_channels);
  nrmac->Mod_id = 0;

  nrmac->tag = (NR_TAG_t*)malloc(sizeof(NR_TAG_t));
  memset((void*)nrmac->tag,0,sizeof(NR_TAG_t));
  for(int n = 0; n < MAX_NUM_OF_SSB; n++)
    cell->sib1_pdsch[n].time_domain_allocation = -1;

  nrmac->rlc_config = *default_rlc_config;

  cell->num_scheduled_prach_rx = 0;

  cell->cset0_bwp_start = 0;
  cell->cset0_bwp_size = 0;

  nrmac->print_ue_stats = true;

  pthread_mutex_init(&nrmac->sched_lock, NULL);

  uid_linear_allocator_init(&nrmac->UE_info.uid_allocator);
  seq_arr_init(&nrmac->pos_act_ue_arr, sizeof(positioning_activation_info_t));

  seq_arr_init(&nrmac->UE_info.access_ue_list, sizeof(NR_UE_info_t *));
  // slightly larger to avoid too many collisions
  seq_arr_init(&nrmac->UE_info.connected_ues_list, sizeof(NR_UE_info_t *));
  int num_ues = MAX_MOBILES_PER_GNB * 2 + 1;
  // no hash func as RNTI is key (is random)
  nrmac->UE_info.connected_ues = hashtable_create(num_ues, NULL);

  nrmac->ul_ri_tpmi_select = nr_ul_ri_tpmi_select_default;
  nrmac->ul_tda_select = nr_ul_tda_select_default;
  nrmac->ul_beam_select = nr_ul_beam_select_default;
  nrmac->ul_mcs_select = nr_ul_mcs_select_default;
  nrmac->ul_rb_alloc = nr_ul_proportional_fair;

  nrmac->dl_lcid_alloc = nr_dl_lcid_alloc_default;

  if (get_softmodem_params()->phy_test) {
    nrmac->pre_processor_dl = nr_preprocessor_phytest;
    nrmac->pre_processor_ul = nr_ul_preprocessor_phytest;
  } else {
    nrmac->pre_processor_dl = nr_dlsch_preprocessor;
    nrmac->pre_processor_ul = nr_ulsch_preprocessor;
    nrmac->dl_ri_pmi_select = nr_dl_ri_pmi_select_default;
    nrmac->dl_mcs_select = nr_dl_mcs_select_default;
    nrmac->dl_beam_select = nr_dl_beam_select_default;
    nrmac->dl_tda_select = nr_dl_tda_select_default;
    nrmac->dl_rb_alloc = nr_dl_proportional_fair;
  }
  if (!IS_SOFTMODEM_NOSTATS)
    threadCreate(&nrmac->stats_thread,
                 nrmac_stats_thread,
                 nrmac,
                 "MAC_STATS",
                 -1,
                 sched_get_priority_min(SCHED_OAI) + 1);
  mac_rrc_init(nrmac, node_type);

  nr_rlc_op_mode_t mode = NODE_IS_MONOLITHIC(node_type) ? NR_RLC_OP_MODE_MONO_GNB : NR_RLC_OP_MODE_SPLIT_GNB;
  int success = nr_rlc_module_init(mode);
  AssertFatal(success == 0,"Could not initialize RLC layer\n");

  nrmac->if_inst = NR_IF_Module_init(0);

  du_init_f1_ue_data();

  srand48(0);

  return nrmac;
}

periodic_ue_sched_t create_period_structure(nr_cell_sched_t *cell, nr_periodic_channel_t channel)
{
  int max_period = get_ideal_period(cell, SRS, 0);
  const frame_structure_t *fs = &cell->frame_structure;
  AssertFatal(max_period > 0 && max_period % fs->numb_slots_period == 0, "Invalid SRS periodicity\n");

  bool count_mixed = channel != SRS;
  int valid_slots_per_period = count_mixed ? get_ul_slots_per_period(fs) : get_full_ul_slots_per_period(fs);
  int nb_slots = (max_period / fs->numb_slots_period) * valid_slots_per_period;
  periodic_ue_sched_t p = {.max_period = max_period, .max_ue_per_slot = 1};
  p.list = calloc_or_fail(nb_slots * p.max_ue_per_slot, sizeof(NR_UE_info_t *));
  return p;
}

static void destroy_periodic_sched(periodic_ue_sched_t p)
{
  free(p.list);
}

void ind_delete_nr_ue_data(void *p)
{
  // access_ue_list has pointers to NR_UE_info_t, and this functions gets a
  // pointer to the pointer
  NR_UE_info_t **UE = p;
  delete_nr_ue_data(*UE);
}

void mac_top_destroy_gNB(gNB_MAC_INST *mac)
{
  for (size_t i = 0; i < sizeofArray(mac->cells); i++) {
    nr_cell_sched_t *cell = &mac->cells[i];
    free(cell->radio_config.ssb_beams);
    if (cell->common_channels.ServingCellConfigCommon == NULL)
      continue;
    NR_COMMON_channels_t *cc = &cell->common_channels;
    nr_mac_pcch_queue_free(cc);
    ASN_STRUCT_FREE(asn_DEF_NR_BCCH_BCH_Message, cc->mib);
    ASN_STRUCT_FREE(asn_DEF_NR_BCCH_DL_SCH_Message, cc->sib1);
    ASN_STRUCT_FREE(asn_DEF_NR_ServingCellConfigCommon, cc->ServingCellConfigCommon);
#ifdef E3_AGENT
    e3_spectrum_mac_detach_cell(cell);
#endif /* E3_AGENT */
  }
  NR_UEs_t *UE_info = &mac->UE_info;
  hashtable_destroy(&UE_info->connected_ues, delete_nr_ue_data);
  seq_arr_free(&UE_info->connected_ues_list, NULL);
  seq_arr_free(&UE_info->access_ue_list, ind_delete_nr_ue_data);
  uid_linear_allocator_init(&UE_info->uid_allocator); // no UE present
  if (mac->f1_config.setup_resp)
    free_f1ap_setup_response(mac->f1_config.setup_resp);
  free(mac->f1_config.setup_resp);
  free(mac->positioning_config);

  for (size_t i = 0; i < sizeofArray(mac->cells); i++) {
    nr_cell_sched_t *cell = &mac->cells[i];
    destroy_periodic_sched(cell->periodic_srs_config);
  }
}

void nr_mac_send_f1_setup_req(void)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  DevAssert(mac);
  mac->mac_rrc.f1_setup_request(mac->f1_config.setup_req);
}

void nr_mac_send_config_req(gNB_MAC_INST *nrmac, nfapi_nr_config_request_scf_t *cfg)
{
  if (NFAPI_MODE == NFAPI_MONOLITHIC) {
    // nothing to be sent in the other cases
    NR_PHY_Config_t phycfg = {.phy_id = 0, .cfg = cfg};
    DevAssert(nrmac->if_inst->NR_PHY_config_req);
    nrmac->if_inst->NR_PHY_config_req(&phycfg);
  }
}
