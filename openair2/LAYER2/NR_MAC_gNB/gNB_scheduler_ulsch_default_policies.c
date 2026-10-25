/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Default pluggable UL scheduler policy functions
 *
 * These are the built-in defaults behind the UL scheduler function pointers.
 * Extracted from gNB_scheduler_ulsch.c to allow clean replacement by custom
 * scheduler plugins.
 */

#include "gNB_scheduler_ulsch_default_policies.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "executables/softmodem-common.h"
#include "common/utils/nr/nr_common.h"
#include "utils.h"
#include <openair2/UTIL/OPT/opt.h>
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"

/* A UE with no more bytes pending than this is served by a default grant. */
#define NR_UL_SMALL_BSR_BYTES 100

/* Default UL RI/TPMI selector: reads rank and TPMI from SRS feedback.
 * A custom impl can compute joint (rank, TPMI) from the H matrix on the candidates. */
void nr_ul_ri_tpmi_select_default(gNB_MAC_INST *mac, nr_ul_candidate_t *cands, int n_cand)
{
  FOR_EACH_CANDIDATE(cand, cands, n_cand)
  {
    NR_UE_sched_ctrl_t *sched_ctrl = &cand->UE->UE_sched_ctrl;
    NR_UE_UL_BWP_t *current_BWP = &cand->UE->current_UL_BWP;
    cand->sched_pusch.nrOfLayers = (current_BWP->dci_format == NR_UL_DCI_FORMAT_0_0) ? 1 : sched_ctrl->srs_feedback.ul_ri + 1;
    cand->sched_pusch.tpmi = sched_ctrl->srs_feedback.tpmi;
  }
}

static NR_tda_info_t *get_new_tda_for_srs(nr_cell_sched_t *cell, const NR_tda_info_t *tda_info)
{
  // by current design, the next TDA would be the one for SRS with one less symbol
  NR_tda_info_t *next = seq_arr_next(&cell->ul_tda, tda_info);
  if (next == seq_arr_end(&cell->ul_tda))
    return NULL;
  AssertFatal(next->k2 == tda_info->k2,
              "K2 in TDA information for SRS %ld doesn't match with current one %ld\n",
              next->k2,
              tda_info->k2);
  AssertFatal(next->startSymbolIndex == tda_info->startSymbolIndex,
              "startSymbolIndex in TDA information for SRS %d doesn't match with current one %d\n",
              next->startSymbolIndex,
              tda_info->startSymbolIndex);
  AssertFatal(next->nrOfSymbols == tda_info->nrOfSymbols - 1,
              "nrOfSymbols in TDA information for SRS %d should be 1 more than current one %d\n",
              next->nrOfSymbols,
              tda_info->nrOfSymbols);
  return next;
}


/* Default UL TDA selector: picks the best TDA per candidate using each candidate's
 * allocated beam to check the correct VRB map. For retransmissions, reuses the original
 * TDA when it is among the valid candidates for this slot; otherwise falls back to the
 * per-beam best TDA with TBS refit. */
int nr_ul_tda_select_default(gNB_MAC_INST *mac,
                             nr_cell_sched_t *cell,
                             nr_ul_candidate_t *cands,
                             int n_cand,
                             frame_t sched_frame,
                             slot_t sched_slot,
                             int k2)
{
  const NR_tda_info_t *tda_list = NULL;
  int n_tda = get_num_ul_tda(mac, cell, sched_slot, k2, &tda_list);
  if (n_tda == 0)
    return 0;

  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;

  int n_valid = 0;
  FOR_EACH_CANDIDATE(cand, cands, n_cand)
  {
    if (cand->skipped)
      continue;

    int beam = cand->alloc_beam_idx;
    int rb_start = 0, rb_len = cand->bwp_size;
    const NR_tda_info_t *best = get_best_ul_tda(cell, beam, tda_list, n_tda, sched_frame, sched_slot, &rb_start, &rb_len);
    DevAssert(best->valid_tda);

    if (cand->is_retx) {
      /* Try to reuse the original TDA if it is among the valid candidates for this slot */
      const NR_sched_pusch_t *retInfo = &cand->UE->UE_sched_ctrl.ul_harq_processes[cand->retx_harq_pid].sched_pusch;
      /* Check exact TDA index, not just K2 — same K2 doesn't guarantee valid symbols in mixed slots */
      const NR_tda_info_t *orig = seq_arr_at(&cell->ul_tda,retInfo->time_domain_allocation);
      ptrdiff_t offset = orig - tda_list;
      if (offset >= 0 && offset < n_tda) {
        /* Original TDA is valid — reuse it directly */
        cand->sched_pusch.time_domain_allocation = retInfo->time_domain_allocation;
        cand->sched_pusch.tda_info = *orig;
        cand->alloc_slbitmap = SL_to_bitmap(orig->startSymbolIndex, orig->nrOfSymbols);
        cand->retx_rbSize = retInfo->rbSize;
        n_valid++;
        continue;
      }
      /* Original TDA not available — try the per-beam best TDA with TBS refit */
      int tda = seq_arr_dist(&cell->ul_tda, seq_arr_front(&cell->ul_tda),best);
      AssertFatal(tda >= 0 && tda < 16, "illegal TDA index %d\n", tda);
      uint16_t needed = check_ul_retx_feasibility(cand, tda, best, scc, cand->bwp_size);
      if (needed == 0) {
        LOG_D(NR_MAC, "[UE %04x] retx infeasible with TDA %d, deferring\n", cand->UE->rnti, tda);
        cand->skipped = true;
        continue;
      }
      cand->sched_pusch.time_domain_allocation = tda;
      cand->sched_pusch.tda_info = *best;
      cand->alloc_slbitmap = SL_to_bitmap(best->startSymbolIndex, best->nrOfSymbols);
      cand->retx_rbSize = needed;
    } else {
      NR_tda_info_t *srs_best = NULL;
      if (cand->sched_srs > 0) {
        srs_best = get_new_tda_for_srs(cell, best);
        if (!srs_best)
          cand->sched_srs = 0;
      }
      const NR_tda_info_t *new_best = srs_best ? srs_best : best;
      int tda = seq_arr_dist(&cell->ul_tda, seq_arr_front(&cell->ul_tda),new_best);
      AssertFatal(tda >= 0 && tda < 16, "illegal TDA index %d\n", tda);
      cand->sched_pusch.time_domain_allocation = tda;
      cand->sched_pusch.tda_info = *new_best;
      cand->alloc_slbitmap = SL_to_bitmap(new_best->startSymbolIndex, new_best->nrOfSymbols);
    }
    n_valid++;
  }
  return n_valid;
}

/* A helper function to determine if a UE needs a default grant. */
static bool needs_default_grant(const nr_ul_candidate_t *c)
{
  return c->sched_long_inactivity || (c->pending_bytes <= NR_UL_SMALL_BSR_BYTES && c->sr_cnt > 0);
}

/* Orders the candidates the way nr_ul_proportional_fair() allocates them:
 * retransmissions, default grants, then UEs with data. mcs_a/mcs_b is the MCS
 * as known at the calling stage, see the two comparators below. */
static int compare_ul_pf(const nr_ul_candidate_t *ca, const nr_ul_candidate_t *cb, int mcs_a, int mcs_b)
{
  /* Retransmissions first, largest first: they need an exact number of
   * contiguous RBs, so the largest are the hardest to place. */
  if (ca->is_retx != cb->is_retx)
    return ca->is_retx ? -1 : 1;
  if (ca->is_retx)
    return (ca->retx_rbSize < cb->retx_rbSize) - (ca->retx_rbSize > cb->retx_rbSize);

  const bool dg_a = needs_default_grant(ca);
  const bool dg_b = needs_default_grant(cb);
  if (dg_a != dg_b)
    return dg_a ? -1 : 1;

  /* most SRs first: the PF weight only favours a starved UE
   * once its throughput average has decayed, far slower than sr_TransMax
   * arrives. A grant resets sr_cnt, so PF fairness returns at once. */
  if (ca->sr_cnt != cb->sr_cnt)
    return (ca->sr_cnt < cb->sr_cnt) - (ca->sr_cnt > cb->sr_cnt);

  /* default grants are all min_rb: nothing else to order them by */
  if (dg_a)
    return 0;

  /* Finally the UEs with data, highest PF weight first. */
  const float wa = ul_pf_weight(mcs_a, ca->mcs_table, ca->sched_pusch.nrOfLayers, ca->avg_throughput);
  const float wb = ul_pf_weight(mcs_b, cb->mcs_table, cb->sched_pusch.nrOfLayers, cb->avg_throughput);
  return (wa < wb) - (wa > wb);
}

static int compare_ul_pf_ptrs(const void *a, const void *b)
{
  const nr_ul_candidate_t *ca = *(const nr_ul_candidate_t *const *)a;
  const nr_ul_candidate_t *cb = *(const nr_ul_candidate_t *const *)b;
  return compare_ul_pf(ca, cb, ca->current_mcs, cb->current_mcs);
}

int nr_ul_beam_select_default(NR_beam_info_t *beam_info,
                              const int16_t *beam_index_list,
                              nr_ul_candidate_t *candidates,
                              int n_candidates,
                              frame_t frame,
                              slot_t slot,
                              frame_t sched_frame,
                              slot_t sched_slot,
                              int slots_per_frame)
{
  /* Build pointer array sorted by PF priority so retx and high-priority UEs claim beams first. */
  nr_ul_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  if (!cand->skipped)
    order[n_active++] = cand;
  qsort(order, n_active, sizeof(*order), compare_ul_pf_ptrs);

  int n_valid = 0;
  for (int i = 0; i < n_active; i++) {
    nr_ul_candidate_t *cand = order[i];

    /* Allocate beam for DCI slot */
    NR_beam_alloc_t dci_beam = beam_allocation_procedure(beam_info, frame, slot, cand->beam_index, slots_per_frame);
    if (dci_beam.idx < 0) {
      LOG_D(NR_MAC, "[UE %04x][%4d.%2d] DCI beam could not be allocated\n", cand->UE->rnti, frame, slot);
      cand->skipped = true;
      continue;
    }

    /* Allocate beam for scheduled PUSCH slot */
    NR_beam_alloc_t sched_beam = beam_allocation_procedure(beam_info, sched_frame, sched_slot, cand->beam_index, slots_per_frame);
    if (sched_beam.idx < 0) {
      LOG_D(NR_MAC, "[UE %04x][%4d.%2d] Sched beam could not be allocated\n", cand->UE->rnti, frame, slot);
      reset_beam_status(beam_info, frame, slot, cand->beam_index, slots_per_frame, dci_beam.new_beam);
      cand->skipped = true;
      continue;
    }

    cand->alloc_dci_beam_idx = dci_beam.idx;
    cand->alloc_dci_beam_new = dci_beam.new_beam;
    cand->alloc_beam_idx = sched_beam.idx;
    cand->alloc_sched_beam_new = sched_beam.new_beam;
    n_valid++;
  }
  return n_valid;
}

void nr_ul_mcs_select_default(const nr_cell_sched_t *cell, nr_ul_candidate_t *candidates, int n_candidates)
{
  const NR_bler_options_t *bo = &cell->ul_bler;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    int mcs;
    if (cand->is_retx) {
      mcs = cand->current_mcs;
    } else if (bo->harq_round_max == 1) {
      mcs = get_mcs_from_SINRx10(cand->mcs_table, cand->snrx10, cand->sched_pusch.nrOfLayers);
      mcs = max(bo->min_mcs, min(bo->max_mcs, min(cand->max_mcs, mcs)));
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
    cand->sched_pusch.mcs = mcs;
    if (!cand->is_retx)
      cand->UE->UE_sched_ctrl.ul_bler_stats.mcs = mcs;
  }
}

static int compare_ul_pf_rb_ptrs(const void *a, const void *b)
{
  const nr_ul_candidate_t *ca = *(const nr_ul_candidate_t *const *)a;
  const nr_ul_candidate_t *cb = *(const nr_ul_candidate_t *const *)b;
  return compare_ul_pf(ca, cb, ca->sched_pusch.mcs, cb->sched_pusch.mcs);
}

static void nr_ul_port_select_default(const nr_ul_sched_params_t *params, nr_ul_candidate_t *cand)
{
  if (cand->is_retx) {
    const NR_sched_pusch_t *retInfo = &cand->UE->UE_sched_ctrl.ul_harq_processes[cand->retx_harq_pid].sched_pusch;
    cand->sched_pusch.dmrs_info = retInfo->dmrs_info;
  } else {
    int layers = cand->sched_pusch.nrOfLayers;
    NR_UE_UL_BWP_t *current_BWP = &cand->UE->current_UL_BWP;
    NR_tda_info_t *tda_info = &cand->sched_pusch.tda_info;
    uint8_t cdm_groups;
    if (current_BWP->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled) {
      cdm_groups = 2;
    } else if (current_BWP->dci_format == NR_UL_DCI_FORMAT_0_0) {
      cdm_groups = (tda_info->nrOfSymbols <= 2) ? 1 : 2;
    } else {
      cdm_groups = (layers < 3) ? 1 : 2;
    }
    cand->sched_pusch.dmrs_info.dmrs_ports = (uint16_t)((1 << layers) - 1);
    cand->sched_pusch.dmrs_info =
        get_ul_dmrs_params(params->scc, current_BWP, tda_info, layers, cand->sched_pusch.dmrs_info.dmrs_ports, cdm_groups);
  }
}

/* number of RBs to send the pending data of an UL candidate, between min_rbSize and max_rbSize */
static uint16_t ul_needed_rbs(const nr_ul_candidate_t *cand, int min_rbSize, int max_rbSize)
{
  const NR_pusch_dmrs_t *dmrs_info = &cand->sched_pusch.dmrs_info;
  const NR_UE_UL_BWP_t *current_BWP = &cand->UE->current_UL_BWP;
  uint16_t Rt;
  uint8_t Qt;
  update_ul_ue_R_Qm(cand->sched_pusch.mcs, current_BWP->mcs_table, current_BWP->pusch_Config, &Rt, &Qt);
  uint32_t tb_size;
  uint16_t rbs = 0;
  nr_find_nb_rb(Qt,
                Rt,
                current_BWP->transform_precoding,
                cand->sched_pusch.nrOfLayers,
                cand->sched_pusch.tda_info.nrOfSymbols,
                dmrs_info->N_PRB_DMRS * dmrs_info->num_dmrs_symb,
                cand->pending_bytes,
                min_rbSize,
                max_rbSize,
                &tb_size,
                &rbs);
  return rbs;
}

int nr_ul_proportional_fair(const nr_ul_sched_params_t *params, nr_ul_candidate_t *candidates, int n_candidates)
{
  int n_scheduled = 0;
  const int min_rb = params->min_rb;
  const int max_rbSize = params->n_rb_avail[0];
  DevAssert(max_rbSize >= min_rb);

  nr_ul_candidate_t *order[MAX_MOBILES_PER_GNB];
  int n_active = 0;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    if (cand->skipped)
      continue;
    order[n_active++] = cand;
  }
  qsort(order, n_active, sizeof(*order), compare_ul_pf_rb_ptrs);

  /* The default grants leave half the budget to the UEs with data */
  const int dg_budget = params->max_num_ue / 2;
  if (dg_budget == 0)
      LOG_W(NR_MAC,
            "max_num_ue %d leaves no budget for the default grants (max_num_ue / 2 == 0): phase 2 never runs, UEs "
            "waiting for a default grant are served after the UEs with data\n",
            params->max_num_ue);
    
  

  /* compare_ul_pf() sorted the candidates into retransmissions, then UEs
   * waiting for a default grant, then UEs with data, so every phase below
   * walks the array on from where the previous one stopped. */
  nr_ul_candidate_t **ue_it = order;
  nr_ul_candidate_t **const ue_end = order + n_active;

  /* Phase 1: HARQ retransmissions, largest first: they need an exact number of
   * contiguous RBs, so the largest are the hardest to place. */
  for (; ue_it < ue_end && (*ue_it)->is_retx; ue_it++) {
    nr_ul_candidate_t *cand = *ue_it;

    nr_ul_port_select_default(params, cand);

    int rbStart;
    uint16_t *vrb_map = params->vrb_map_UL[cand->alloc_beam_idx];
    int block_len = find_largest_free_block(vrb_map, cand->alloc_slbitmap, cand->bwp_start, cand->bwp_size, &rbStart);
    if (block_len < cand->retx_rbSize) {
      LOG_D(NR_MAC,
            "[UE %04x] retx needs %d RB, largest free block is %d, deferring to next slot\n",
            cand->UE->rnti,
            cand->retx_rbSize,
            block_len);
      continue;
    }

    COMMIT_UL_ALLOC(params, cand, rbStart, cand->retx_rbSize, cand->sched_pusch.mcs, n_scheduled);
  }

  /* Phase 2: allocate all default grants or up to half of what is allowed in this slot (max_grant)*/
  for (int n = 0; ue_it < ue_end && needs_default_grant(*ue_it) && n < dg_budget; ue_it++) {
    nr_ul_candidate_t *cand = *ue_it;

    nr_ul_port_select_default(params, cand);

    int rbStart;
    uint16_t *vrb_map = params->vrb_map_UL[cand->alloc_beam_idx];
    int block_len = find_largest_free_block(vrb_map, cand->alloc_slbitmap, cand->bwp_start, cand->bwp_size, &rbStart);
    if (block_len < min_rb)
      continue;

    COMMIT_UL_ALLOC(params, cand, rbStart, min_rb, cand->sched_pusch.mcs, n_scheduled);
    if (cand->scheduled)
      n++;
  }

  /* The UEs the share did not cover are kept for phase 4, in case the UEs with
   * data leave any of the budget unspent. */
  nr_ul_candidate_t **ue_res = ue_it;
  while (ue_it < ue_end && needs_default_grant(*ue_it))
    ue_it++;
  nr_ul_candidate_t **const ue_res_end = ue_it;

  /* Phase 3: UEs with data. */
  nr_ul_candidate_t **const ue_data = ue_it;
  const int n_remain_ue = params->max_num_ue - n_scheduled;
  // share RBs fairly between remaining allocatable UEs
  const int n_rb_per_ue = max(min_rb, max_rbSize / n_remain_ue);
  uint16_t rbs_ue[MAX_MOBILES_PER_GNB] = {0};
  int excess_total_rbs = max_rbSize;

  for (int n = 0; ue_it < ue_end && n < n_remain_ue + 2; ue_it++, n++) {
    nr_ul_candidate_t *cand = *ue_it;

    nr_ul_port_select_default(params, cand);

    // calculate the number of RBs that UE would like to have. Power limitation
    // is later
    uint16_t *want = &rbs_ue[ue_it - order];
    *want = ul_needed_rbs(cand, min_rb, max_rbSize);
    if (n < n_remain_ue) {
      // for the first n_remain_ue UEs: account number of RBs
      // so excess RBs not used by some UEs could be given to others
      excess_total_rbs -= min(*want, n_rb_per_ue);
      excess_total_rbs = max(excess_total_rbs, 0);
    }
  }

  /* allocate up to all UEs checked above */
  for (ue_it = ue_data; ue_it < ue_end; ue_it++) {
    nr_ul_candidate_t *cand = *ue_it;
    const int j = ue_it - order;
    if (rbs_ue[j] == 0)
      continue;

    // give every UE its chunk of data. If total_rbs indicates excess RBs, give
    // additionally as appropriate.
    int rb_req = min(rbs_ue[j], n_rb_per_ue);
    int excess_req = max(rbs_ue[j] - rb_req, 0);
    uint8_t mcs = cand->sched_pusch.mcs;
    // check if power is enough for rb_req + excess_req if actually received a
    // PHR (PCmax > 0, otherwise nothing is scheduled)
    nr_ul_phr_advice_t advice;
    if (cand->pcmax != 0 && !nr_ul_check_phr(params, cand, rb_req + excess_req, mcs, &advice)) {
      int lim_rb = advice.max_mcs_min_rb.rbSize;
      if (lim_rb > rb_req) {
        // enough for rb_req, but not excess_req
        excess_req = lim_rb - rb_req;
      } else {
        // not enough for rb_req
        excess_req = 0;
        rb_req = lim_rb;
      }
      mcs = advice.max_mcs_min_rb.mcs;
    }
    if (excess_total_rbs > 0 && excess_req > 0) {
      int excess_ack = min(excess_total_rbs, excess_req);
      rb_req += excess_ack;
      excess_total_rbs -= excess_ack;
      DevAssert(excess_total_rbs >= 0);
    }
    int rbStart, rbSize;
    uint16_t *vrb_map = params->vrb_map_UL[cand->alloc_beam_idx];
    if (!get_rb_alloc(min_rb, rb_req, cand->bwp_start, cand->bwp_size, vrb_map, cand->alloc_slbitmap, &rbStart, &rbSize))
      continue;
    COMMIT_UL_ALLOC(params, cand, rbStart, rbSize, mcs, n_scheduled);
  }


  /* Phase 4: if data requests did not use all DCIs, continue with default grants*/
  for (; ue_res < ue_res_end; ue_res++) {
    nr_ul_candidate_t *cand = *ue_res;

    nr_ul_port_select_default(params, cand);

    uint16_t *vrb_map = params->vrb_map_UL[cand->alloc_beam_idx];
    int rbStart;
    int block_len = find_largest_free_block(vrb_map, cand->alloc_slbitmap, cand->bwp_start, cand->bwp_size, &rbStart);
    if (block_len < min_rb)
      continue;

    COMMIT_UL_ALLOC(params, cand, rbStart, min_rb, cand->sched_pusch.mcs, n_scheduled);
  }

  return n_scheduled;
}
