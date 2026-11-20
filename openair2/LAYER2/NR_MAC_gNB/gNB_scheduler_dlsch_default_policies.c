/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief       Default pluggable policy functions for the DL scheduler pipeline.
 *
 * These are the built-in implementations behind the function pointers
 * (dl_ri_pmi_select, dl_tda_select, dl_beam_select, dl_mcs_select, dl_rb_alloc, dl_lcid_alloc)
 * wired up at MAC init time.  They can be replaced at runtime by external
 * scheduler plug-ins without touching the core scheduling loop in
 * gNB_scheduler_dlsch.c.
 */

#include "common/utils/nr/nr_common.h"
#include "gNB_scheduler_dlsch_default_policies.h"
/*MAC*/
#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"

/*TAG*/
#include "NR_TAG-Id.h"

/*Softmodem params*/
#include "executables/softmodem-common.h"
#include "../../../nfapi/oai_integration/vendor_ext.h"

// Default RI/PMI selector: reads rank and PMI from CSI feedback for new-tx,
// or from HARQ process state for retx.
void nr_dl_ri_pmi_select_default(const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates)
{
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    NR_UE_sched_ctrl_t *sched_ctrl = &cand->UE->UE_sched_ctrl;
    NR_UE_DL_BWP_t *dl_bwp = &cand->UE->current_DL_BWP;
    if (cand->is_retx) {
      cand->sched_pdsch.nrOfLayers = sched_ctrl->harq_processes[cand->retx_harq_pid].sched_pdsch.nrOfLayers;
      cand->sched_pdsch.pm_index =
          get_pm_index(cell, cand->UE, dl_bwp->dci_format, cand->sched_pdsch.nrOfLayers, cell->radio_config.pdsch_AntennaPorts.XP);
    } else {
      cand->sched_pdsch.nrOfLayers = cand->csi_ri + 1;
      cand->sched_pdsch.pm_index = cand->csi_pm_index;
    }
  }
}

// Default TDA selector: picks the slot-wide TDA index from get_dl_tda(),
// then resolves tda_info per candidate using each UE's own BWP / search
// space / coreset. Marks invalids with skipped=true.
int nr_dl_tda_select_default(const gNB_MAC_INST *mac, const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates, frame_t frame, slot_t slot)
{
  int tda = get_dl_tda(mac, cell, slot);
  AssertFatal(tda >= 0, "Unable to find PDSCH time domain allocation in list\n");
  const NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;

  int n_valid = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    if (cand->skipped)
      continue;
    NR_UE_info_t *UE = cand->UE;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;
    int coresetid = sched_ctrl->coreset->controlResourceSetId;
    NR_tda_info_t tda_info = get_dl_tda_info(dl_bwp,
                                             sched_ctrl->search_space->searchSpaceType->present,
                                             tda,
                                             scc->dmrs_TypeA_Position,
                                             1,
                                             TYPE_C_RNTI_,
                                             coresetid,
                                             false);
    if (!tda_info.valid_tda) {
      cand->skipped = true;
      continue;
    }

    /* For retransmissions with a changed TDA, refit rbSize to preserve TBS */
    if (cand->is_retx) {
      const NR_sched_pdsch_t *orig = &sched_ctrl->harq_processes[cand->retx_harq_pid].sched_pdsch;
      bool tda_changed =
          tda_info.startSymbolIndex != orig->tda_info.startSymbolIndex || tda_info.nrOfSymbols != orig->tda_info.nrOfSymbols;
      if (tda_changed) {
        uint16_t new_rbSize = check_dl_retx_feasibility(cand, tda, &tda_info, scc, dl_bwp->BWPSize);
        if (!new_rbSize) {
          cand->skipped = true;
          continue;
        }
        cand->retx_rbSize = new_rbSize;
      }
    }

    cand->sched_pdsch.time_domain_allocation = tda;
    cand->sched_pdsch.tda_info = tda_info;
    cand->alloc_slbitmap = SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);
    n_valid++;
  }
  return n_valid;
}

static int compare_dl_pf_ptrs(const void *a, const void *b)
{
  const nr_dl_candidate_t *ca = *(const nr_dl_candidate_t *const *)a;
  const nr_dl_candidate_t *cb = *(const nr_dl_candidate_t *const *)b;
  /* retx first (INFINITY weight), then highest PF weight */
  float wa = ca->is_retx ? INFINITY : dl_pf_weight(ca->current_mcs, ca->mcs_table, ca->sched_pdsch.nrOfLayers, ca->avg_throughput);
  float wb = cb->is_retx ? INFINITY : dl_pf_weight(cb->current_mcs, cb->mcs_table, cb->sched_pdsch.nrOfLayers, cb->avg_throughput);
  return (wa < wb) - (wa > wb);
}

int nr_dl_beam_select_default(NR_beam_info_t *beam_info,
                              const int16_t *beam_index_list,
                              nr_dl_candidate_t *candidates,
                              int n_candidates,
                              frame_t frame,
                              slot_t slot,
                              int slots_per_frame)
{
  /* Build pointer array sorted by PF priority so retx and high-priority UEs claim beams first. */
  nr_dl_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  if (!cand->skipped)
    order[n_active++] = cand;
  qsort(order, n_active, sizeof(*order), compare_dl_pf_ptrs);

  int n_valid = 0;
  for (int i = 0; i < n_active; i++) {
    nr_dl_candidate_t *cand = order[i];
    NR_beam_alloc_t beam = beam_allocation_procedure(beam_info, frame, slot, cand->alloc_beam_dir, slots_per_frame);
    if (beam.idx < 0) {
      cand->skipped = true;
      continue;
    }

    cand->alloc_beam_idx = beam.idx;
    cand->alloc_new_beam = beam.new_beam;
    n_valid++;
  }
  return n_valid;
}

void nr_dl_mcs_select_default(const nr_cell_sched_t *cell, nr_dl_candidate_t *candidates, int n_candidates)
{
  const NR_bler_options_t *bo = &cell->dl_bler;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    int mcs;
    if (cand->is_retx) {
      mcs = cand->current_mcs; /* retx MCS is fixed by the HARQ round */
    } else if (bo->harq_round_max == 1) {
      mcs = max(bo->min_mcs, min(bo->max_mcs, cand->max_mcs));
    } else if (!cand->bler_updated) {
      mcs = cand->current_mcs;
    } else {
      mcs = nr_adapt_mcs_from_bler(cand->current_mcs,
                                   bo->min_mcs,
                                   cand->max_mcs,
                                   cand->bler,
                                   bo->lower,
                                   bo->upper,
                                   cand->last_num_sched);
    }
    cand->sched_pdsch.mcs = mcs;
    /* Persist for all candidates — BLER-based MCS ramps even for UEs the
     * policy rejects this slot (failed CCE, no free RBs, etc.). */
    if (!cand->is_retx)
      cand->UE->UE_sched_ctrl.dl_bler_stats.mcs = mcs;
  }
}

static int compare_dl_pf_rb_ptrs(const void *a, const void *b)
{
  const nr_dl_candidate_t *ca = *(const nr_dl_candidate_t *const *)a;
  const nr_dl_candidate_t *cb = *(const nr_dl_candidate_t *const *)b;
  /* retx first, then highest PF weight (uses sched_pdsch.mcs, which is set by mcs_select) */
  float wa =
      ca->is_retx ? INFINITY : dl_pf_weight(ca->sched_pdsch.mcs, ca->mcs_table, ca->sched_pdsch.nrOfLayers, ca->avg_throughput);
  float wb =
      cb->is_retx ? INFINITY : dl_pf_weight(cb->sched_pdsch.mcs, cb->mcs_table, cb->sched_pdsch.nrOfLayers, cb->avg_throughput);
  return (wa < wb) - (wa > wb);
}

/* number of RBs to send the pending data of a DL candidate, between min_rbSize and max_rbSize */
static uint16_t dl_needed_rbs(const nr_dl_sched_params_t *params, const nr_dl_candidate_t *cand, int min_rbSize, int max_rbSize)
{
  int mcs = cand->sched_pdsch.mcs;
  uint8_t Qm = nr_get_Qm_dl(mcs, cand->mcs_table);
  uint16_t R = nr_get_code_rate_dl(mcs, cand->mcs_table);
  const nr_cell_sched_t *cell = params->cell;
  NR_pdsch_dmrs_t dmrs = get_dl_dmrs_params(cell->common_channels.ServingCellConfigCommon,
                                            &cand->UE->current_DL_BWP,
                                            &cand->sched_pdsch.tda_info,
                                            cand->sched_pdsch.nrOfLayers);
  const int oh = 3 * 4 + (cand->UE->UE_sched_ctrl.ta_apply ? 2 : 0);
  uint32_t tbs;
  uint16_t rbs = 0;
  nr_find_nb_rb(Qm,
                R,
                1,
                cand->sched_pdsch.nrOfLayers,
                cand->sched_pdsch.tda_info.nrOfSymbols,
                dmrs.N_PRB_DMRS * dmrs.N_DMRS_SLOT,
                cand->pending_bytes + oh,
                min_rbSize,
                max_rbSize,
                &tbs,
                &rbs);
  return rbs;
}

int nr_dl_proportional_fair(const nr_dl_sched_params_t *params, nr_dl_candidate_t *candidates, int n_candidates)
{
  const int min_rbSize = 5;
  int n_scheduled = 0;

  /* Build pointer array sorted by PF priority (retx first, then highest weight) */
  nr_dl_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  if (!cand->skipped)
    order[n_active++] = cand;
  qsort(order, n_active, sizeof(*order), compare_dl_pf_rb_ptrs);

  /* Phase 1: HARQ retransmissions (highest priority, exact RBs) */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];
    if (!cand->is_retx)
      continue;

    int needed_rbs = cand->retx_rbSize;
    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    int rbStart, rbSize;
    if (!get_rb_alloc(needed_rbs,
                      cand->bwp_size,
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    COMMIT_ALLOC(params, cand, rbStart, needed_rbs, cand->sched_pdsch.mcs, n_scheduled);
  }

  /* Phase 2: No-data UEs (TA command or beam switch MAC CE, no RLC data) */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];
    if (cand->is_retx || cand->pending_bytes > 0)
      continue;

    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    int rbStart, rbSize;
    if (!get_rb_alloc(min_rbSize,
                      cand->bwp_size,
                      cand->bwp_start,
                      cand->bwp_size,
                      vrb_map,
                      cand->alloc_slbitmap,
                      &rbStart,
                      &rbSize))
      continue;

    COMMIT_ALLOC(params, cand, rbStart, min_rbSize, cand->sched_pdsch.mcs, n_scheduled);
  }

  /* BW is the same across all beams, just use beam 0 */
  int max_rbSize = params->n_rb_avail[0];
  DevAssert(max_rbSize >= min_rbSize);
  int n_remain_ue = params->max_num_ue - n_scheduled;
  // share RBs fairly between remaining allocatable UEs
  int n_rb_per_ue = max(min_rbSize, max_rbSize / n_remain_ue);

  /* Phase 3: New data UEs — PF priority order, count number of RBs required,
   * store number of excess RBs for UEs. Check two additional UEs in case the
   * first ones cannot be allocated (DCI alloc fail). This is only necessary
   * because we use type-1 allocation, if we used type-0, we could fix the UEs,
   * then iteratively give RBs as needed. */
  uint16_t rbs_ue[MAX_MOBILES_PER_GNB] = {0};
  int excess_total_rbs = max_rbSize;
  for (int j = 0, n = 0; j < n_active && n < n_remain_ue + 2; j++) {
    nr_dl_candidate_t *cand = order[j];
    if (cand->is_retx || cand->pending_bytes == 0)
      continue;

    // calculate the number of RBs that UE would like to have
    rbs_ue[j] = dl_needed_rbs(params, cand, min_rbSize, max_rbSize);
    if (n < n_remain_ue) {
      // for the first n_remain_ue UEs: account number of RBs
      // so excess RBs not used by some UEs could be given to others
      excess_total_rbs -= min(rbs_ue[j], n_rb_per_ue);
      excess_total_rbs = max(excess_total_rbs, 0);
    }
    n++;
  }

  /* allocate up to all UEs checked above */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];
    if (cand->is_retx || cand->pending_bytes == 0 || rbs_ue[j] == 0)
      continue;

    // give every UE its chunk of data. If total_rbs indicates excess RBs, give
    // additionally as appropriate.
    int rb_req = min(rbs_ue[j], n_rb_per_ue);
    int excess_req = max(rbs_ue[j] - rb_req, 0);
    if (excess_total_rbs > 0 && excess_req > 0) {
      int excess_ack = min(excess_total_rbs, excess_req);
      rb_req += excess_ack;
      excess_total_rbs -= excess_ack;
      DevAssert(excess_total_rbs >= 0);
    }
    int rbStart, rbSize;
    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    if (!get_rb_alloc(min_rbSize, rb_req, cand->bwp_start, cand->bwp_size, vrb_map, cand->alloc_slbitmap, &rbStart, &rbSize))
      continue;

    int mcs = cand->sched_pdsch.mcs;
    COMMIT_ALLOC(params, cand, rbStart, rbSize, mcs, n_scheduled);
  }

  /* Phase 4: the RBs of the UEs above that could not be allocated (no CCE, no PUCCH resource for the HARQ
   * feedback) are left: give them to the other UEs with data, in PF order. With many UEs, the PUCCH resources
   * of an UL slot are taken by the first UEs scheduled in the period, the UEs with the highest PF weight then
   * fail on PUCCH while the UEs that already have a PUCCH allocation are further in the list. */
  for (int j = 0; j < n_active; j++) {
    nr_dl_candidate_t *cand = order[j];
    // skip the UEs already tried above
    if (cand->is_retx || cand->pending_bytes == 0 || rbs_ue[j] != 0)
      continue;
    int rb_req = dl_needed_rbs(params, cand, min_rbSize, max_rbSize);
    int rbStart, rbSize;
    uint16_t *vrb_map = params->vrb_map[cand->alloc_beam_idx];
    if (!get_rb_alloc(min_rbSize, rb_req, cand->bwp_start, cand->bwp_size, vrb_map, cand->alloc_slbitmap, &rbStart, &rbSize))
      continue;
    COMMIT_ALLOC(params, cand, rbStart, rbSize, cand->sched_pdsch.mcs, n_scheduled);
  }

  return n_scheduled;
}

void nr_dl_lcid_alloc_default(const gNB_MAC_INST *mac,
                              const nr_dl_candidate_t *candidate,
                              int tbs_available,
                              int lcid_alloc[NR_MAX_NUM_LCID])
{
  (void)mac;
  (void)tbs_available;
  memset(lcid_alloc, 0, NR_MAX_NUM_LCID * sizeof(int));
  for (int lcid = 0; lcid < NR_MAX_NUM_LCID; lcid++)
    lcid_alloc[lcid] = candidate->pending_bytes_per_lcid[lcid];
}
