/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "assertions.h"

#include "mac_proto.h"
#ifdef E3_AGENT
#include "openair2/E3AP/ran_func_spectrum_extern.h"
#endif /* E3_AGENT */

#include "common/utils/LOG/log.h"
#include "common/utils/nr/nr_common.h"
#include "UTIL/OPT/opt.h"

#include "intertask_interface.h"

#include "executables/softmodem-common.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "executables/nr-softmodem.h"

#include <errno.h>
#include <string.h>

uint8_t nr_get_rv(int rel_round)
{
  const uint8_t nr_rv_round_map[4] = {0, 2, 3, 1};
  AssertFatal(rel_round < 4, "Invalid index %d for rv\n", rel_round);
  return nr_rv_round_map[rel_round];
}

void clear_nr_nfapi_information(nr_cell_sched_t *cell, frame_t frameP, slot_t slotP)
{
  /* called below and in simulators, so we assume a lock but don't require it */
  const int num_slots = cell->frame_structure.numb_slots_frame;
  UL_tti_req_ahead_initialization(cell, num_slots, frameP, slotP);

  nfapi_nr_dl_tti_pdcch_pdu_rel15_t **pdcch = (nfapi_nr_dl_tti_pdcch_pdu_rel15_t **)cell->pdcch_pdu_idx;

  cell->pdu_index = 0;

  memset(pdcch, 0, sizeof(*pdcch) * MAX_NUM_CORESET);

  /* advance last round's future UL_tti_req to be ahead of current frame/slot */
  const int size = cell->UL_tti_req_ahead_size;
  const int prev_slot = frameP * num_slots + slotP + size - 1;
  nfapi_nr_ul_tti_request_t *future_ul_tti_req = &cell->UL_tti_req_ahead[prev_slot % size];
  future_ul_tti_req->SFN = (prev_slot / num_slots) % 1024;
  LOG_D(NR_MAC, "%d.%d UL_tti_req_ahead SFN.slot = %d.%d for index %d \n", frameP, slotP, future_ul_tti_req->SFN, future_ul_tti_req->Slot, prev_slot % size);
  /* future_ul_tti_req->Slot is fixed! */
  for (int i = 0; i < future_ul_tti_req->n_pdus; i++) {
    future_ul_tti_req->pdus_list[i].pdu_type = 0;
    future_ul_tti_req->pdus_list[i].pdu_size = 0;
  }
  future_ul_tti_req->n_pdus = 0;
  future_ul_tti_req->n_ulsch = 0;
  future_ul_tti_req->n_ulcch = 0;
  future_ul_tti_req->n_group = 0;
}

static void clear_beam_information(NR_beam_info_t *beam_info, int frame, int slot, int slots_per_frame)
{
  // for now we use the same logic of UL_tti_req_ahead
  // reset after 1 frame with the exception of 15kHz
  if (beam_info->config.bf_method == BF_METHOD_STRAIGHT_WIRE)
    return;
  // initialization done only once
  AssertFatal(beam_info->beam_allocation_size >= 0, "Beam information not initialized\n");
  int idx_to_clear = (frame * slots_per_frame + slot) / beam_info->config.beam_duration;
  idx_to_clear = (idx_to_clear + beam_info->beam_allocation_size - 1) % beam_info->beam_allocation_size;
  if (slot % beam_info->config.beam_duration == 0) {
    // resetting previous period allocation
    LOG_D(NR_MAC, "%d.%d Clear beam information for index %d\n", frame, slot, idx_to_clear);
    for (int i = 0; i < beam_info->config.beams_per_period; i++)
      beam_info->beam_allocation[i][idx_to_clear] = -1;
  }
}

/* the structure nfapi_nr_ul_tti_request_t is very big, let's copy only what is necessary */
static void copy_ul_tti_req(nfapi_nr_ul_tti_request_t *to, nfapi_nr_ul_tti_request_t *from)
{
  int i;

  to->header = from->header;
  to->SFN = from->SFN;
  to->Slot = from->Slot;
  to->n_pdus = from->n_pdus;
  to->rach_present = from->rach_present;
  to->n_ulsch = from->n_ulsch;
  to->n_ulcch = from->n_ulcch;
  to->n_group = from->n_group;

  for (i = 0; i < from->n_pdus; i++) {
    to->pdus_list[i].pdu_type = from->pdus_list[i].pdu_type;
    to->pdus_list[i].pdu_size = from->pdus_list[i].pdu_size;

    switch (from->pdus_list[i].pdu_type) {
      case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE:
        to->pdus_list[i].prach_pdu = from->pdus_list[i].prach_pdu;
        break;
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        to->pdus_list[i].pusch_pdu = from->pdus_list[i].pusch_pdu;
        break;
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
        to->pdus_list[i].pucch_pdu = from->pdus_list[i].pucch_pdu;
        break;
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
        to->pdus_list[i].srs_pdu = from->pdus_list[i].srs_pdu;
        break;
    }
  }

  for (i = 0; i < from->n_group; i++)
    to->groups_list[i] = from->groups_list[i];
}

static void nr_fill_pusch_fapi_groups(nfapi_nr_ul_tti_request_t *UL_tti_req)
{
  // Single User MIMO
  UL_tti_req->n_group = 0;
  for (int i = 0; i < UL_tti_req->n_pdus; i++) {
    if (UL_tti_req->pdus_list[i].pdu_type != NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE)
      continue;
    nfapi_nr_ul_tti_request_number_of_groups_t *group = &UL_tti_req->groups_list[UL_tti_req->n_group];
    group->n_ue = 0;
    group->ue_list[group->n_ue++].pdu_idx = i;
    UL_tti_req->n_group++;
  }
}

void gNB_dlsch_ulsch_scheduler(module_id_t module_idP, const int cell_id, frame_t frame, slot_t slot, NR_Sched_Rsp_t *sched_info)
{
  protocol_ctxt_t ctxt = {0};
  PROTOCOL_CTXT_SET_BY_MODULE_ID(&ctxt, module_idP, ENB_FLAG_YES, NOT_A_RNTI, frame, slot,module_idP);

  gNB_MAC_INST *gNB = RC.nrmac[module_idP];
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(gNB, cell_id);
  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;

  NR_SCHED_LOCK(&gNB->sched_lock);
  int slots_frame = cell->frame_structure.numb_slots_frame;
  clear_beam_information(&cell->beam_info, frame, slot, slots_frame);

  gNB->frame = frame;
  start_meas(&cell->gNB_scheduler);

  int num_beams = 1;
  if (cell->beam_info.config.bf_method != BF_METHOD_STRAIGHT_WIRE)
    num_beams = cell->beam_info.config.beams_per_period;
  // clear vrb_maps
  for (int i = 0; i < num_beams; i++)
    memset(cell->common_channels.vrb_map[i], 0, sizeof(uint16_t) * MAX_BWP_SIZE);
  // clear last scheduled slot's content (only)!
  const int size = cell->vrb_map_UL_size;
  const int prev_slot = frame * slots_frame + slot + size - 1;
  for (int i = 0; i < num_beams; i++) {
    uint16_t *vrb_map_UL = cell->common_channels.vrb_map_UL[i];
    memcpy(&vrb_map_UL[prev_slot % size * MAX_BWP_SIZE], &cell->ulprbbl, sizeof(uint16_t) * MAX_BWP_SIZE);
  }
  /* Hard-reserve prev_slot's UL for sensing so no UE allocator claims it. */
#ifdef E3_AGENT
  nr_mac_sensing_reserve_ul_slot(cell, prev_slot, frame);
#endif /* E3_AGENT */
  clear_nr_nfapi_information(cell, frame, slot);

  bool wait_prach_completed = cell->num_scheduled_prach_rx >= NUM_PRACH_RX_FOR_NOISE_ESTIMATE;
  if (gNB->print_ue_stats && (wait_prach_completed || get_softmodem_params()->phy_test) && (slot == 0) && (frame & 127) == 0) {
    char stats_output[32656] = {0};
    dump_mac_stats(gNB, cell, stats_output, sizeof(stats_output), true);
    LOG_I(NR_MAC, "Frame.Slot %d.%d\n%s\n", frame, slot, stats_output);

    int num_ue = get_num_connected_ue(&gNB->UE_info);
    if (gNB->print_ue_stats && num_ue > gNB->stats_max_ue) {
      gNB->print_ue_stats = false;
      LOG_W(NR_MAC,
            "periodical UE stats deactivated after reaching %d UEs, please check nrMAC_stats.log, or increase MACRLCs.[0].stats_max_ue\n",
            gNB->stats_max_ue);
    }
  }

  nr_measgap_scheduling(gNB, cell, frame, slot);
  nr_mac_update_timers(gNB, cell);

  if (wait_prach_completed || get_softmodem_params()->phy_test) {
    // This schedules MIB
    schedule_nr_mib(cell, frame, slot, &sched_info->DL_req);

    // This schedules SIB1
    // SIB19 will be scheduled if ntn_Config_r17 is initialized
    if (IS_SA_MODE(get_softmodem_params())) {
      schedule_nr_sib1(cell, frame, slot, &sched_info->DL_req, &sched_info->TX_req);
      schedule_nr_other_sib(cell, frame, slot, &sched_info->DL_req, &sched_info->TX_req);
      schedule_nr_pcch(gNB, cell, frame, slot, &sched_info->DL_req, &sched_info->TX_req);
    }
  }

  // This schedule PRACH if we are not in phy_test mode
  if (get_softmodem_params()->phy_test == 0) {
    /* we need to make sure that resources for PRACH are free. To avoid that
       e.g. PUSCH has already been scheduled, make sure we schedule before
       anything else: below, we simply assume an advance one frame (minus
       prach duration, because otherwise we would allocate the current slot in
       UL_tti_req_ahead), but be aware that, e.g., K2 is allowed to be larger
       (schedule_nr_prach will assert if resources are not free). */
    const int n_slots_ahead = slots_frame - cc->prach_len + get_NTN_Koffset(scc);
    const frame_t f = (frame + (slot + n_slots_ahead) / slots_frame) % 1024;
    const slot_t s = (slot + n_slots_ahead) % slots_frame;
    schedule_nr_prach(gNB, cell, f, s);
  }

  start_meas(&cell->schedule_periodic);
  // Schedule CSI-RS transmission
  nr_csirs_scheduling(gNB, cell, frame, slot, &sched_info->DL_req);

  // Schedule CSI measurement reporting
  nr_csi_meas_reporting(gNB, cell, frame, slot);

  nr_schedule_periodic_srs(gNB, cell, frame, slot);

  nr_sr_reporting(gNB, cell, frame, slot);
  stop_meas(&cell->schedule_periodic);

  // This schedule RA procedure if not in phy_test mode
  // Otherwise consider 5G already connected
  if (get_softmodem_params()->phy_test == 0) {
    nr_schedule_RA(gNB, cell, frame, slot, &sched_info->UL_dci_req, &sched_info->DL_req, &sched_info->TX_req);
  }

  // This schedules the DCI for Uplink and subsequently PUSCH
  start_meas(&cell->schedule_ulsch);
  nr_schedule_ulsch(gNB, cell, frame, slot, &sched_info->UL_dci_req);
  stop_meas(&cell->schedule_ulsch);

  // This schedules the DCI for Downlink and PDSCH
  start_meas(&cell->schedule_dlsch);
  nr_schedule_ue_spec(gNB, cell, frame, slot, &sched_info->DL_req, &sched_info->TX_req);
  stop_meas(&cell->schedule_dlsch);

  nr_schedule_pucch(gNB, cell, frame, slot);

  /* Now that all UE UL scheduling is done: restore the reserved slot and run the
   * scan + publish against the final vrb_map. Both no-ops unless sensing is on. */
#ifdef E3_AGENT
  nr_mac_sensing_restore_ul_slot(cell, frame, slot);
  nr_mac_sensing_scan_and_publish(gNB, cell, frame, slot);
#endif /* E3_AGENT */

  const int current_index = ul_buffer_index(frame, slot, slots_frame, cell->UL_tti_req_ahead_size);
  copy_ul_tti_req(&sched_info->UL_tti_req, &cell->UL_tti_req_ahead[current_index]);

  nr_fill_pusch_fapi_groups(&sched_info->UL_tti_req);

  stop_meas(&cell->gNB_scheduler);
  NR_SCHED_UNLOCK(&gNB->sched_lock);
}
