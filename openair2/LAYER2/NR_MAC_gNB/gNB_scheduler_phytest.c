/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief gNB scheduling procedures in phy_test mode
 */

#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "executables/nr-softmodem.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "executables/softmodem-common.h"
#include "common/utils/nr/nr_common.h"

// #define UL_HARQ_PRINT
// #define ENABLE_MAC_PAYLOAD_DEBUG 1

#define RNTI_PHY_TEST 0x1234

/* This function checks whether the given Dl/UL slot is set
   in the input bitmap (per period), which is a mask indicating in which
   slot to transmit (among those available in the TDD configuration)
   This function also checks if Dmod/Umod option is set, then the slot will be
   scheduled in case slot % val == 0.
   Use Dmod/Umod in case of FDD FR2. */
static bool is_xlsch_in_slot(uint64_t bitmap, uint32_t modval, slot_t slot)
{
  AssertFatal(modval || slot < 64, "Slot >= 64 cannot be scheduled using options 'D/U', use 'Dmod/Umod' option\n");

  bool val1 = (bitmap >> slot) & 0x01;
  bool val2 = (modval && (slot % modval == 0));
  return (val1 || val2);
}

uint32_t target_dl_mcs = 9;
uint32_t target_dl_mcs_table_index = 0;
uint32_t target_dl_Nl = 1;
uint32_t target_dl_pmi = 0;
uint32_t target_dl_bw = 50;
uint64_t dlsch_slot_bitmap = (1<<1);
uint32_t dlsch_slot_modval = 0;

/* schedules whole bandwidth for first user, all the time */
void nr_preprocessor_phytest(gNB_MAC_INST *mac, nr_cell_sched_t *cell, post_process_pdsch_t *pp_pdsch)
{
  frame_t frame = pp_pdsch->frame;
  slot_t slot = pp_pdsch->slot;

  /* already mutex protected: held in gNB_dlsch_ulsch_scheduler() */
  int slot_period = slot % cell->frame_structure.numb_slots_period;
  if (!is_xlsch_in_slot(dlsch_slot_bitmap, dlsch_slot_modval, slot_period))
    return;
  NR_UE_info_t *UE = find_connected_nr_ue(&mac->UE_info, RNTI_PHY_TEST);
  if (UE == NULL)
   return;
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;
  dl_bwp->mcsTableIdx = target_dl_mcs_table_index;

  /* return if all DL HARQ processes wait for feedback */
  if (sched_ctrl->retrans_dl_harq.head == -1 && sched_ctrl->available_dl_harq.head == -1) {
    LOG_D(NR_MAC, "[UE %04x][%4d.%2d] UE has no free DL HARQ process, skipping\n", UE->rnti, frame, slot);
    return;
  }

  const int tda = get_dl_tda(mac, cell, slot);
  NR_tda_info_t tda_info = get_dl_tda_info(dl_bwp,
                                           sched_ctrl->search_space->searchSpaceType->present,
                                           tda,
                                           scc->dmrs_TypeA_Position,
                                           1,
                                           TYPE_C_RNTI_,
                                           sched_ctrl->coreset->controlResourceSetId,
                                           false);
  if(!tda_info.valid_tda)
    return;

  /* find largest unallocated chunk */
  const int bwpSize = dl_bwp->BWPSize;
  const int BWPStart = dl_bwp->BWPStart;

  /* This is a primitive beam allocation procedure for PDSCH in phytest mode which is
  intended to be used to verify the beamformed signal in VSA. The beam allocation of PDSCH
  slots follows the SSB slots even in slots that don't have SSB. */
  uint8_t num_ssb = 0;
  const uint64_t ssbBitmap = get_ssb_bitmap_and_len(scc, &num_ssb);
  int ssb_idx_beam = 0;
  for (int i_ssb = 0; i_ssb < num_ssb; i_ssb++) {
    if (IS_BIT_SET(ssbBitmap, (63 - i_ssb))) {
      NR_SubcarrierSpacing_t scs = *scc->ssbSubcarrierSpacing;
      const long band = *scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0];
      uint16_t ssb_start_symbol = get_ssb_start_symbol(band, scs, i_ssb);
      // select beam for PDSCH in current slot based on SSB beam
      if ((ssb_start_symbol / NR_SYMBOLS_PER_SLOT) == (slot % cell->frame_structure.numb_slots_period)) {
        ssb_idx_beam = i_ssb;
        break;
      }
    }
  }
  int beam_idx = get_beam_from_ssbidx(cell, ssb_idx_beam);
  NR_beam_alloc_t beam = beam_allocation_procedure(&cell->beam_info, frame, slot, beam_idx, cell->frame_structure.numb_slots_frame);
  AssertFatal(beam.idx > -1, "Can't allocate beam %d in phytest scheduler\n", beam_idx);
  UE->UE_beam_index = get_allocated_beam(&cell->beam_info, frame, slot, cell->frame_structure.numb_slots_frame, beam.idx);

  int rbStart = 0;
  int rbSize = 0;
  if (target_dl_bw>bwpSize)
    target_dl_bw = bwpSize;
  uint16_t *vrb_map = cell->common_channels.vrb_map[beam.idx];
  const uint16_t slbitmap = SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);
  /* ensure that we allocate exactly target_dl_bw, or return */
  if (!get_rb_alloc(target_dl_bw, target_dl_bw, BWPStart, bwpSize, vrb_map, slbitmap, &rbStart, &rbSize))
    return;

  sched_ctrl->num_total_bytes = 0;
  DevAssert(seq_arr_size(&sched_ctrl->lc_config) == 1);
  const nr_lc_config_t *c = seq_arr_at(&sched_ctrl->lc_config, 0);
  const logical_chan_id_t lcid = c->lcid;
  const uint16_t rnti = UE->rnti;
  /* update sched_ctrl->num_total_bytes so that postprocessor schedules data,
   * if available */
  nr_mac_rlc_status_ind(rnti, frame, 1, &lcid, &sched_ctrl->rlc_status[lcid]);
  sched_ctrl->num_total_bytes += sched_ctrl->rlc_status[lcid].bytes_in_buffer;

  int CCEIndex = get_cce_index(cell,
                               slot,
                               UE->rnti,
                               &sched_ctrl->aggregation_level,
                               beam.idx,
                               sched_ctrl->search_space,
                               sched_ctrl->coreset,
                               &sched_ctrl->sched_pdcch,
                               0);
  AssertFatal(CCEIndex >= 0, "Could not find CCE for UE %04x\n", UE->rnti);

  int alloc = -1;
  int harq_pid = sched_ctrl->retrans_dl_harq.head;
  if (harq_pid < 0)
    harq_pid = sched_ctrl->available_dl_harq.head;
  if (!get_FeedbackDisabled(UE->sc_info.downlinkHARQ_FeedbackDisabled_r17, harq_pid)) {
    int r_pucch = nr_get_pucch_resource(sched_ctrl->coreset, UE->current_UL_BWP.pucch_Config, CCEIndex);
    alloc = nr_acknack_scheduling(cell, UE, frame, slot, 0, r_pucch, 0);
    if (alloc < 0) {
      LOG_D(NR_MAC, "Could not find PUCCH for UE %04x@%d.%d\n", rnti, frame, slot);
      return;
    }
  }

  sched_ctrl->cce_index = CCEIndex;

  fill_pdcch_vrb_map(cell, &sched_ctrl->sched_pdcch, CCEIndex, sched_ctrl->aggregation_level, beam.idx);

  NR_sched_pdsch_t sched_pdsch = {
      .rbSize = rbSize,
      .rbStart = rbStart,
      .alloc_type = PDSCH_TYPE1,
      .mcs = target_dl_mcs,
      .R = nr_get_code_rate_dl(target_dl_mcs, dl_bwp->mcsTableIdx),
      .Qm = nr_get_Qm_dl(target_dl_mcs, dl_bwp->mcsTableIdx),
      // tbSize further below
      .dl_harq_pid = sched_ctrl->retrans_dl_harq.head, // PID of HARQ awaiting retransmission, or -1 otherwise
      .pucch_allocation = alloc,
      .pm_index = target_dl_pmi,
      .nrOfLayers = target_dl_Nl,
      .bwp_info = get_pdsch_bwp_start_size(cell, UE),
      .dmrs_parms = get_dl_dmrs_params(scc, dl_bwp, &tda_info, target_dl_Nl),
      .time_domain_allocation = tda,
      .tda_info = tda_info,
  };
  // Map antenna ports for this UE
  const nr_pdsch_AntennaPorts_t *p = &cell->radio_config.pdsch_AntennaPorts;
  const uint16_t num_log_ports = p->XP * p->N1 * p->N2;
  sched_pdsch.ant_port_idx.numSpatialStreamIndices = num_log_ports;
  const int start_stream_idx = beam.idx * num_log_ports;
  for (int i = 0; i < sched_pdsch.ant_port_idx.numSpatialStreamIndices;i++)
    sched_pdsch.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[start_stream_idx + i];

  sched_ctrl->dl_bler_stats.mcs = target_dl_mcs; /* for logging output */
  sched_pdsch.tb_size = nr_compute_tbs(sched_pdsch.Qm,
                                       sched_pdsch.R,
                                       sched_pdsch.rbSize,
                                       tda_info.nrOfSymbols,
                                       sched_pdsch.dmrs_parms.N_PRB_DMRS * sched_pdsch.dmrs_parms.N_DMRS_SLOT,
                                       0 /* N_PRB_oh, 0 for initialBWP */,
                                       0 /* tb_scaling */,
                                       target_dl_Nl)
                        >> 3;

  nr_dl_candidate_t candidate = {
      .UE = UE,
      .rnti = rnti,
      .is_retx = sched_pdsch.dl_harq_pid >= 0,
      .retx_harq_pid = sched_pdsch.dl_harq_pid,
      .pending_bytes = sched_ctrl->num_total_bytes,
      .mcs_table = dl_bwp->mcsTableIdx,
      .bwp_start = sched_pdsch.bwp_info.bwpStart,
      .bwp_size = sched_pdsch.bwp_info.bwpSize,
  };
  candidate.pending_bytes_per_lcid[lcid] = sched_ctrl->rlc_status[lcid].bytes_in_buffer;

  post_process_dlsch(mac, cell, pp_pdsch, UE, &sched_pdsch, &candidate);

  /* mark the corresponding RBs as used */
  for (int rb = 0; rb < sched_pdsch.rbSize; rb++)
    vrb_map[rb + sched_pdsch.rbStart + BWPStart] = slbitmap;
}

uint32_t target_ul_mcs = 9;
uint32_t target_ul_mcs_table_index = 0;
uint32_t target_ul_bw = 50;
uint32_t target_ul_Nl = 1;
uint64_t ulsch_slot_bitmap = (1 << 8);
uint32_t ulsch_slot_modval = 0;
void nr_ul_preprocessor_phytest(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pusch_t *pp_pusch)
{
  int frame = pp_pusch->frame;
  int slot = pp_pusch->slot;

  /* already mutex protected: held in gNB_dlsch_ulsch_scheduler() */
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_UE_info_t *UE = find_connected_nr_ue(&nr_mac->UE_info, RNTI_PHY_TEST);
  if (UE == NULL)
    return;

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  ul_bwp->mcs_table = target_ul_mcs_table_index;

  /* return if all UL HARQ processes wait for feedback */
  if (sched_ctrl->retrans_ul_harq.head == -1 && sched_ctrl->available_ul_harq.head == -1) {
    LOG_D(NR_MAC, "[UE %04x][%4d.%2d] UE has no free UL HARQ process, skipping\n", UE->rnti, frame, slot);
    return;
  }

  const int bw = ul_bwp->BWPSize;
  const int BWPStart = ul_bwp->BWPStart;
  uint16_t rbStart = 0;
  uint16_t rbSize = min(bw, target_ul_bw);

  DevAssert(seq_arr_size(&cell->ul_tda) > 0);
  const int tda = 0;
  NR_tda_info_t tda_info = get_ul_tda_info(ul_bwp,
                                           sched_ctrl->coreset->controlResourceSetId,
                                           sched_ctrl->search_space->searchSpaceType->present,
                                           TYPE_C_RNTI_,
                                           tda);
  DevAssert(tda_info.valid_tda);

  int slots_frame = cell->frame_structure.numb_slots_frame;
  int NTN_gNB_Koffset = get_NTN_Koffset(scc);
  const fsn_t sched_fs = get_fb_frame_slot(frame, slot, tda_info.k2, slots_frame, NTN_gNB_Koffset);

  /* check if slot is UL, and that slot is 8 (assuming K2=6 because of UE
   * limitations).  Note that if K2 or the TDD configuration is changed, below
   * conditions might exclude each other and never be true */
  int slot_period = sched_fs.s % cell->frame_structure.numb_slots_period;
  if (!is_xlsch_in_slot(ulsch_slot_bitmap, ulsch_slot_modval, slot_period))
    return;

  // TODO implement beam procedures for phy-test mode
  int beam = 0;
  const NR_tda_info_t *tda_p;
  const int n_tda = get_num_ul_tda(nr_mac, cell, sched_fs.s, tda_info.k2, &tda_p);
  DevAssert(n_tda > 0);
  /* check only the first TDA: we are only interested in finding out if this TDA fits completely */
  int rb_s = rbStart, rb_l = rbSize;
  get_best_ul_tda(cell, beam, tda_p, 1, sched_fs.f, sched_fs.s, &rb_s, &rb_l);
  DevAssert(rb_s == rbStart && rb_l == rbSize);

  const int buffer_index = ul_buffer_index(sched_fs.f, sched_fs.s, slots_frame, cell->vrb_map_UL_size);
  uint16_t *vrb_map_UL = &cell->common_channels.vrb_map_UL[beam][buffer_index * MAX_BWP_SIZE];
  for (int i = rbStart; i < rbStart + rbSize; ++i) {
    if ((vrb_map_UL[i+BWPStart] & SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols)) != 0) {
      LOG_E(MAC, "%4d.%2d RB %d is already reserved, cannot schedule UE\n", frame, slot, i);
      return;
    }
  }

  int CCEIndex = get_cce_index(cell, slot, UE->rnti,
                               &sched_ctrl->aggregation_level,
                               beam,
                               sched_ctrl->search_space,
                               sched_ctrl->coreset,
                               &sched_ctrl->sched_pdcch,
                               0);
  if (CCEIndex < 0) {
    LOG_E(MAC, "%s(): CCE list not empty, couldn't schedule PUSCH\n", __func__);
    return;
  }

  sched_ctrl->cce_index = CCEIndex;

  NR_sched_pusch_t sched = {
      .frame = sched_fs.f,
      .slot = sched_fs.s,
      .rbSize = rbSize,
      .rbStart = rbStart,
      .mcs = target_ul_mcs,
      // R, Qm, tb_size set further below
      // PID of a HARQ process awaiting retransmission, or -1 for "any new"
      .ul_harq_pid = sched_ctrl->retrans_ul_harq.head,
      .nrOfLayers = target_ul_Nl,
      // tpmi in post-process
      .time_domain_allocation = tda,
      .tda_info = tda_info,
      .dmrs_info = get_ul_dmrs_params(scc, ul_bwp, &tda_info, target_ul_Nl, 0, 1),
      .bwp_info = get_pusch_bwp_start_size(UE),
      .ant_port_idx.numSpatialStreamIndices = cell->radio_config.pusch_AntennaPorts,
  };
  const uint16_t start_stream_idx = beam * cell->radio_config.pusch_AntennaPorts;
  for (int i = 0; i < sched.ant_port_idx.numSpatialStreamIndices; i++)
    sched.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[start_stream_idx + i];

  sched_ctrl->ul_bler_stats.mcs = sched.mcs; /* for logging output */

  /* Calculate TBS from MCS */
  sched.R = nr_get_code_rate_ul(sched.mcs, ul_bwp->mcs_table);
  sched.Qm = nr_get_Qm_ul(sched.mcs, ul_bwp->mcs_table);
  if (ul_bwp->pusch_Config->tp_pi2BPSK
      && ((ul_bwp->mcs_table == 3 && sched.mcs < 2) || (ul_bwp->mcs_table == 4 && sched.mcs < 6))) {
    sched.R >>= 1;
    sched.Qm <<= 1;
  }

  sched.tb_size = nr_compute_tbs(sched.Qm,
                                 sched.R,
                                 sched.rbSize,
                                 tda_info.nrOfSymbols,
                                 sched.dmrs_info.N_PRB_DMRS * sched.dmrs_info.num_dmrs_symb,
                                 0, // nb_rb_oh
                                 0,
                                 sched.nrOfLayers /* NrOfLayers */)
                  >> 3;

  /* save allocation to FAPI structures */
  post_process_ulsch(nr_mac, cell, pp_pusch, UE, &sched, 0);

  /* mark the corresponding RBs as used */
  fill_pdcch_vrb_map(cell,
                     &sched_ctrl->sched_pdcch,
                     CCEIndex,
                     sched_ctrl->aggregation_level,
                     beam);

  for (int rb = rbStart; rb < rbStart + rbSize; rb++)
    vrb_map_UL[rb+BWPStart] |= SL_to_bitmap(tda_info.startSymbolIndex, tda_info.nrOfSymbols);
}
