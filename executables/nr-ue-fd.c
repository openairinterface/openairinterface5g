/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * UE instances with frequency-domain I/O (FD-UE). Each instance runs its
 * own thread, like UE_thread() in nr-ue.c, as one client of a UE Low-PHY port
 * (executables/ue_low_phy/port.h): it reads demodulated DL symbols from the port's
 * shared ring and posts UL symbols/PRACH to its own TX ring. The Low-PHY device
 * serving the port owns the radio, FFT/IFFT, timing and the summing of every
 * client's UL.
 *
 * Each instance's interface device (PHY_VARS_NR_UE.fd_ifdevice) bypasses FFT/IFFT:
 * nr_slot_fep() reads symbols from the ring, phy_procedures_nrUE_TX() and
 * generate_nr_prach() write frequency-domain data instead of modulating.
 */

#define _GNU_SOURCE
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "PHY/defs_nr_UE.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "SCHED_NR_UE/defs.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"
#include "NR_UE_PHY_INTERFACE/NR_IF_Module.h"
#include "RRC/NR_UE/rrc_proto.h"
#include "RRC/NR_UE/L2_interface_ue.h"
#include "nr_phy_common.h"
#include "executables/nr-uesoftmodem.h"
#include "executables/softmodem-common.h"
#include "executables/nr-ue-ru.h"
#include "executables/nr-ue-fd.h"
#include "executables/ue_low_phy/device.h"
#include "common/utils/LOG/log.h"
#include "common/utils/actor/actor.h"
#include "common/utils/barrier/barrier.h"
#include "common/utils/threadPool/notified_fifo.h"
#include "common/utils/time_manager/time_manager.h"
#include "common/utils/system.h"
#include "common/utils/threadPool/pthread_utils.h"

/* Non-static functions from nr-ue.c. */
extern void UE_dl_processing(void *arg);
extern void nr_ue_dequeue_rrc_to_mac_msgs(PHY_VARS_NR_UE *UE);
extern int UE_dl_preprocessing(PHY_VARS_NR_UE *UE,
                               const UE_nr_rxtx_proc_t *proc,
                               int *tx_wait_for_dlsch,
                               nr_phy_data_t *phy_data,
                               bool *stats_printed);
extern void *nrL1_UE_stats_thread(void *param);
extern int nr_ue_slot_select(const fapi_nr_config_request_t *cfg, int nr_slot);
extern int determine_N_TA_offset(PHY_VARS_NR_UE *ue);
extern void processSlotTX(void *arg);
extern void start_process_slot_tx(void *arg);
extern void init_ue_actors(Actor_t *sync_actor, Actor_t *dl_actors, Actor_t *ul_actors);

// Per-TX-slot facts for the slot record, indexed slot % window: the TA is set when
// the slot is scheduled, the rest while its UL actor processes it. As deep as the
// TX barrier chain, the furthest the TX pipeline can lag.
#define FD_SLOT_WINDOW NUM_PROCESS_SLOT_TX_BARRIERS
// TX lag (scheduled - completed slots) at which the instance gives its pending TX
// slots up: the Low-PHY waits for every member, so while another member stalls,
// this one's TX backs up, and the barrier chain only holds NUM_PROCESS_SLOT_TX_BARRIERS.
#define FD_MAX_TX_LAG (NUM_PROCESS_SLOT_TX_BARRIERS / 4)
_Static_assert(MAX_FRAME_NUMBER % FD_SLOT_WINDOW == 0, "PRACH slots are found by SFN and slot");

typedef struct {
  PHY_VARS_NR_UE *UE;
  nr_ue_fd_ifdevice_t dev;
  ue_low_phy_client_t client;
  Actor_t *dl_actors;
  Actor_t *ul_actors;
  uint32_t ta[FD_SLOT_WINDOW];
  uint32_t used[FD_SLOT_WINDOW];
  int prach[FD_SLOT_WINDOW];
  uint64_t prach_abs_slot[FD_SLOT_WINDOW]; // TX slot of a PRACH being generated (set by write_slot before generating it)
  uint64_t declared[FD_SLOT_WINDOW]; // TX slot + 1 when its (empty) record was posted when scheduling it
  uint64_t last_tx_slot; // last TX slot scheduled (its UL job created)
  ue_low_phy_progress_t tx_completed; // highest TX slot completed (or given up)
  ue_low_phy_progress_t tx_jobs; // UL jobs finished, to tell when the TX chain has stopped
  uint64_t rrc_next_frame; // next frame to tick RRC with: absolute frame in sync, radio frame out of sync
} fd_ue_t;

// The Low-PHY every instance is a client of.
static ue_low_phy_device_t *fd_device;

// The IQ-sample time source follows the radio, once for all instances.
static pthread_mutex_t fd_time_mutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t fd_time_ticked;

static void fd_tick_time(const NR_DL_FRAME_PARMS *fp)
{
  mutexlock(fd_time_mutex);
  const int64_t now = ue_low_phy_device_rx_time(fd_device);
  if (now > fd_time_ticked) {
    time_manager_iq_samples(now - fd_time_ticked, fp->samples_per_subframe * 1000);
    fd_time_ticked = now;
  }
  mutexunlock(fd_time_mutex);
}

// RRC timers advance one tick per frame up to (excluding) frame, also over frames
// skipped or spent out of sync (out_of_sync_rrc_timer_tick() in nr-ue.c).
static void fd_tick_rrc(fd_ue_t *f, uint64_t frame)
{
  for (; f->rrc_next_frame < frame; f->rrc_next_frame++)
    nr_ue_rrc_timer_trigger(f->UE->Mod_id, f->rrc_next_frame / MAX_FRAME_NUMBER, f->rrc_next_frame % MAX_FRAME_NUMBER, 0);
}

static uint64_t fd_radio_frame(const NR_DL_FRAME_PARMS *fp)
{
  return ue_low_phy_device_rx_time(fd_device) / fp->samples_per_frame;
}

static uint64_t abs_slot_of(const NR_DL_FRAME_PARMS *fp, int hfn, int frame, int slot)
{
  return ((uint64_t)hfn * MAX_FRAME_NUMBER + frame) * fp->slots_per_frame + slot;
}

static bool fd_read_symbol(nr_ue_fd_ifdevice_t *dev, int frame, int slot, int symbol, c16_t *const *dst)
{
  fd_ue_t *f = dev->priv;
  const NR_DL_FRAME_PARMS *fp = &f->UE->frame_parms;
  // The PHY knows the SFN only (abs_frame % 1024 == SFN): the slot is the newest published
  // one with this SFN and slot. A DL actor never lags a whole SFN cycle.
  const uint64_t period = (uint64_t)MAX_FRAME_NUMBER * fp->slots_per_frame;
  const uint64_t newest = ue_low_phy_client_next_published_slot(&f->client) - 1;
  const uint64_t lag = (newest % period + period - ((uint64_t)frame * fp->slots_per_frame + slot)) % period;
  const uint64_t abs_slot = newest - lag;
  if (!ue_low_phy_client_rx_symbol(&f->client, abs_slot * fp->symbols_per_slot + symbol, dst, fp->nb_antennas_rx)) {
    LOG_W(NR_PHY, "[fd-ue] UE %d: symbol %d of %d.%d left the RX ring before it was read\n", f->UE->Mod_id, symbol, frame, slot);
    return false;
  }
  return true;
}

static void fd_write_prach(nr_ue_fd_ifdevice_t *dev,
                           int frame,
                           int slot,
                           const c16_t *prachF,
                           int dftlen,
                           int Ncp,
                           int prach_start,
                           int repetitions)
{
  fd_ue_t *f = dev->priv;
  // UL jobs of consecutive slots run concurrently: find this one's slot by frame and
  // slot (FD_SLOT_WINDOW divides the slots of an SFN cycle).
  const uint64_t abs_slot = f->prach_abs_slot[((uint64_t)frame * f->UE->frame_parms.slots_per_frame + slot) % FD_SLOT_WINDOW];
  const int entry = ue_low_phy_client_tx_prach(&f->client, abs_slot, prach_start, Ncp, dftlen, repetitions, prachF);
  if (entry < 0)
    LOG_E(NR_PHY, "[fd-ue] UE %d: no free PRACH entry for %d.%d\n", f->UE->Mod_id, frame, slot);
  f->prach[abs_slot % FD_SLOT_WINDOW] = entry;
}

static void
fd_write_slot(nr_ue_fd_ifdevice_t *dev, int hfn, int frame, int slot, int nb_ant_tx, c16_t **txdataF, const bool *was_symbol_used)
{
  fd_ue_t *f = dev->priv;
  const NR_DL_FRAME_PARMS *fp = &f->UE->frame_parms;
  const uint64_t abs_slot = abs_slot_of(fp, hfn, frame, slot);
  // The PHY writes this slot's PRACH next, if any.
  f->prach_abs_slot[abs_slot % FD_SLOT_WINDOW] = abs_slot;
  uint32_t used = 0;
  for (int sym = 0; sym < fp->symbols_per_slot; sym++) {
    if (!was_symbol_used[sym])
      continue;
    c16_t *data[nb_ant_tx];
    for (int a = 0; a < nb_ant_tx; a++)
      data[a] = &txdataF[a][sym * fp->ofdm_symbol_size];
    ue_low_phy_client_tx_symbol(&f->client, abs_slot * fp->symbols_per_slot + sym, data, nb_ant_tx);
    used |= 1u << sym;
  }
  f->used[abs_slot % FD_SLOT_WINDOW] = used;
}

// UL actor job: the regular TX slot processing, then the slot record. The Low-PHY
// transmits a slot only once every member has recorded it.
static void fd_process_slot_tx(void *arg)
{
  nr_rxtx_thread_data_t *d = arg;
  fd_ue_t *f = d->UE->fd_ifdevice->priv;
  const uint64_t abs_slot = abs_slot_of(&d->UE->frame_parms, d->proc.hfn_tx, d->proc.frame_tx, d->proc.nr_slot_tx);
  const int w = abs_slot % FD_SLOT_WINDOW;
  f->used[w] = 0;
  f->prach[w] = -1;
  processSlotTX(arg);
  if (f->declared[w] != abs_slot + 1)
    ue_low_phy_client_slot_done(&f->client, abs_slot, f->used[w], f->prach[w], f->ta[w]);
  ue_low_phy_progress_raise(&f->tx_completed, abs_slot);
  ue_low_phy_progress_add(&f->tx_jobs, 1);
}

static void fd_flush(fd_ue_t *f)
{
  for (int i = 0; i < get_nrUE_params()->num_dl_actors; i++)
    flush_actor(f->dl_actors + i);
  for (int i = 0; i < get_nrUE_params()->num_ul_actors; i++)
    flush_actor(f->ul_actors + i);
}

// Gives up every scheduled TX slot and restarts the TX chain: the slots are declared
// empty first, so UL actors waiting for room in the port return, then flushed.
static void fd_restart_tx(fd_ue_t *f, int tx_wait_for_dlsch[NR_MAX_SLOTS_PER_FRAME], enum stream_status_e *stream_status)
{
  PHY_VARS_NR_UE *UE = f->UE;
  ue_low_phy_client_drop_through(&f->client, f->last_tx_slot);
  fd_flush(f);
  // A TX job starts the next one through its barrier while running, possibly behind
  // a flush: flush the UL actors until a whole round runs no job, so the chain has
  // stopped before its barriers are reset.
  for (uint64_t before = ue_low_phy_progress_get(&f->tx_jobs);; before = ue_low_phy_progress_get(&f->tx_jobs)) {
    for (int i = 0; i < get_nrUE_params()->num_ul_actors; i++)
      flush_actor(f->ul_actors + i);
    if (ue_low_phy_progress_get(&f->tx_jobs) == before)
      break;
  }
  for (int i = 0; i < NUM_PROCESS_SLOT_TX_BARRIERS; i++)
    dynamic_barrier_reset(&UE->process_slot_tx_barriers[i]);
  memset(tx_wait_for_dlsch, 0, NR_MAX_SLOTS_PER_FRAME * sizeof(int));
  *stream_status = STREAM_STATUS_UNSYNC;
  ue_low_phy_progress_raise(&f->tx_completed, f->last_tx_slot);
}

// Rewrites the SFN carried by a PBCH payload (inverse of nr_ue_decode_mib()):
// 6 MSBs after the BCCH-BCH message-type bit, 4 LSBs bit-reversed in the extra bits.
static void fd_set_mib_sfn(fapiPbch_t *pbch, int sfn)
{
  pbch->decoded_output[0] = (pbch->decoded_output[0] & ~0x7e) | (((sfn >> 4) & 0x3f) << 1);
  pbch->xtra_byte &= ~0xf;
  for (int i = 0; i < 4; i++)
    pbch->xtra_byte |= ((sfn >> (3 - i)) & 1) << i;
}

// Hands the anchored cell's MIB to the MAC, as a monolithic initial sync does. The
// MIB was decoded when the Low-PHY anchored, maybe long before this instance
// joined: its SFN is moved to the frame this instance starts with.
static void fd_apply_cell(PHY_VARS_NR_UE *UE, const ue_low_phy_cell_t *cell, uint64_t first_slot)
{
  NR_DL_FRAME_PARMS *fp = &UE->frame_parms;
  // nr_fill_rx_indication()'s SSB case reads these from frame_parms.
  fp->Nid_cell = cell->pci;
  fp->ssb_start_subcarrier = cell->ssb_start_subcarrier;
  fp->half_frame_bit = cell->half_frame_bit;
  fp->ssb_index = cell->ssb_index;
  UE->symbol_offset = cell->symbol_offset;
  UE->is_synchronized = 1;

  fapiPbch_t pbch = {.xtra_byte = cell->mib_xtra};
  memcpy(pbch.decoded_output, cell->mib, sizeof(cell->mib));
  const int sfn = (first_slot / fp->slots_per_frame) % MAX_FRAME_NUMBER;
  fd_set_mib_sfn(&pbch, sfn);
  UE_nr_rxtx_proc_t proc = {0};
  fapi_nr_rx_indication_t rx_ind = {0};
  nr_downlink_indication_t dl_indication = {.module_id = UE->Mod_id, .cc_id = UE->CC_id, .rx_ind = &rx_ind};
  nr_fill_rx_indication(&rx_ind, FAPI_NR_RX_PDU_TYPE_SSB, UE, 0, 0, NULL, &proc, &pbch);
  if (UE->if_inst && UE->if_inst->dl_indication)
    UE->if_inst->dl_indication(&dl_indication);

  NR_UE_MAC_INST_t *mac = get_mac_inst(UE->Mod_id);
  notifiedFIFO_elt_t *elt = pullNotifiedFIFO(&mac->input_nf);
  AssertFatal(elt, "[fd-ue] FIFO error waiting for the MIB (UE %d)\n", UE->Mod_id);
  process_msg_rcc_to_mac(NotifiedFifoData(elt), UE->Mod_id);
  delNotifiedFIFO_elt(elt);
  AssertFatal(mac->mib_frame == sfn,
              "[fd-ue] UE %d: MAC decoded SFN %d from a MIB carrying SFN %d\n",
              UE->Mod_id,
              mac->mib_frame,
              sfn);
  LOG_A(NR_PHY, "[fd-ue] UE %d synchronized: PCI %u, SFN %d\n", UE->Mod_id, cell->pci, mac->mib_frame);
}

static void *UE_fd_thread(void *arg)
{
  fd_ue_t *f = arg;
  PHY_VARS_NR_UE *UE = f->UE;
  ue_low_phy_client_t *c = &f->client;
  NR_DL_FRAME_PARMS *fp = &UE->frame_parms;
  const int nb_slot_frame = fp->slots_per_frame;
  const int rx2tx = get_nrUE_params()->ue_capability_rx2tx;

  InitSinLUT();
  for (int i = 0; i < NUM_PROCESS_SLOT_TX_BARRIERS; i++)
    dynamic_barrier_init(&UE->process_slot_tx_barriers[i]);
  UE->max_pos_acc = get_nrUE_params()->time_sync_I
                        ? get_nrUE_params()->ntn_init_time_drift * 1e-6 * fp->samples_per_frame / get_nrUE_params()->time_sync_I
                        : 0;
  UE->is_synchronized = 0;

  enum stream_status_e stream_status = STREAM_STATUS_UNSYNC;
  int tx_wait_for_dlsch[NR_MAX_SLOTS_PER_FRAME] = {0};
  bool stats_printed = false;
  int shiftForNextFrame = 0;
  uint32_t ta_samples = UINT32_MAX;
  uint32_t sync_req = 0;
  uint64_t next_slot = 0;
  f->rrc_next_frame = fd_radio_frame(fp);

  while (!oai_exit) {
    if (!UE->is_synchronized) {
      // As UE_thread: RRC may act while out of sync (T304 expiry, re-establishment).
      nr_ue_dequeue_rrc_to_mac_msgs(UE);
      // The Low-PHY keeps its anchor, so a request raised meanwhile only retargets.
      if (UE->synch_request.received_synch_request == 1) {
        UE->target_Nid_cell = UE->synch_request.synch_req.target_Nid_cell;
        UE->synch_request.received_synch_request = 0;
      }
      if (!sync_req)
        sync_req = ue_low_phy_client_request_sync(c, UE->target_Nid_cell, get_nrUE_params()->ue_fd_sync_timeout_ms);
      ue_low_phy_cell_t cell;
      // Short waits: time and RRC timers are ticked between them.
      const int rc = ue_low_phy_client_sync_result(c, sync_req, &cell, 10000);
      fd_tick_time(fp);
      if (rc <= 0)
        fd_tick_rrc(f, fd_radio_frame(fp));
      if (rc == 0)
        continue;
      sync_req = 0;
      if (rc < 0)
        continue; // the next pass asks for a fresh search
      // Start at the newest slot: whatever is older was received while searching.
      next_slot = ue_low_phy_client_next_published_slot(c);
      if (next_slot < cell.first_slot)
        next_slot = cell.first_slot;
      fd_apply_cell(UE, &cell, next_slot);
      f->rrc_next_frame = next_slot / nb_slot_frame;
      stream_status = STREAM_STATUS_UNSYNC;
      memset(tx_wait_for_dlsch, 0, sizeof(tx_wait_for_dlsch));
      for (int i = 0; i < NUM_PROCESS_SLOT_TX_BARRIERS; i++)
        dynamic_barrier_reset(&UE->process_slot_tx_barriers[i]);
      ta_samples = UINT32_MAX;
      // As after a monolithic sync: the TA of a previous connection does not apply.
      UE->timing_advance = 0;
      UE->N_TA_offset = determine_N_TA_offset(UE);
      f->last_tx_slot = next_slot + rx2tx - 1;
      ue_low_phy_progress_raise(&f->tx_completed, f->last_tx_slot);
      if (!ue_low_phy_client_join(c, next_slot + rx2tx, UE->N_TA_offset + UE->timing_advance + UE->timing_advance_ntn))
        LOG_E(NR_PHY, "[fd-ue] UE %d: could not join the Low-PHY uplink\n", UE->Mod_id);
      continue;
    }

    // MAC-triggered re-sync (handover, re-establishment).
    if (UE->synch_request.received_synch_request == 1) {
      UE->target_Nid_cell = UE->synch_request.synch_req.target_Nid_cell;
      const fapi_nr_config_request_t *cfg = &UE->nrUE_config;
      fp->ssb_start_subcarrier = nr_get_ssb_start_sc(fp->numerology_index,
                                                     cfg->ssb_table.ssb_offset_point_a,
                                                     cfg->ssb_table.ssb_subcarrier_offset,
                                                     fp->freq_range);
      // Every scheduled TX slot is either recorded by now or given up: leaving after
      // the last one is exact.
      fd_restart_tx(f, tx_wait_for_dlsch, &stream_status);
      if (!ue_low_phy_client_leave(c, f->last_tx_slot, 1000000))
        LOG_E(NR_PHY, "[fd-ue] UE %d: Low-PHY did not confirm leaving\n", UE->Mod_id);
      clean_UE_harq(UE);
      UE->is_synchronized = 0;
      f->rrc_next_frame = fd_radio_frame(fp);
      UE->synch_request.received_synch_request = 0;
      continue;
    }

    const int wait = ue_low_phy_client_wait_slot(c, next_slot, 100000);
    if (wait == 1)
      continue;
    if (wait == -2)
      break; // the Low-PHY stopped
    if (wait < 0) {
      // Fell more than the ring behind: restart the slot stream at the newest slot,
      // like a resync, so the TX barrier chain starts over instead of waiting for
      // slots that will never be processed. Still a member: the TX slots skipped
      // (or dropped with the barriers) are declared empty.
      const uint64_t newest = ue_low_phy_client_next_published_slot(c) - 1;
      LOG_W(NR_PHY, "[fd-ue] UE %d: slot %lu left the ring, skipping %lu slots\n", UE->Mod_id, next_slot, newest - next_slot);
      fd_restart_tx(f, tx_wait_for_dlsch, &stream_status);
      next_slot = newest;
      f->last_tx_slot = next_slot + rx2tx - 1;
      ue_low_phy_client_skip_through(c, f->last_tx_slot);
      ue_low_phy_progress_raise(&f->tx_completed, f->last_tx_slot);
      continue;
    }
    fd_tick_time(fp);

    // Another member stalling the Low-PHY backs this instance's TX up; give the
    // backlog up before the TX barrier chain wraps.
    const uint64_t tx_completed = ue_low_phy_progress_get(&f->tx_completed);
    if (f->last_tx_slot > tx_completed + FD_MAX_TX_LAG) {
      LOG_W(NR_PHY,
            "[fd-ue] UE %d: TX %lu slots behind (Low-PHY waiting for a member), giving up slots up to %lu\n",
            UE->Mod_id,
            f->last_tx_slot - tx_completed,
            f->last_tx_slot);
      fd_restart_tx(f, tx_wait_for_dlsch, &stream_status);
    }

    const uint64_t absolute_slot = next_slot++;
    const uint64_t abs_frame_rx = absolute_slot / nb_slot_frame;
    const int slot_nr = absolute_slot % nb_slot_frame;
    const uint64_t abs_slot_tx = absolute_slot + rx2tx;
    const uint64_t abs_frame_tx = abs_slot_tx / nb_slot_frame;

    // Steer the shared RX/TX boundary once per frame, as UE_thread() does (only
    // the port's timing master is listened to: one radio, one boundary).
    if (slot_nr == nb_slot_frame - 1) {
      ue_low_phy_client_adjust_timing(c, shiftForNextFrame);
      shiftForNextFrame = -(int)round(UE->max_pos_acc * get_nrUE_params()->time_sync_I);
    }

    nr_rxtx_thread_data_t curMsg = {.UE = UE};
    curMsg.proc.nr_slot_rx = slot_nr;
    curMsg.proc.nr_slot_tx = abs_slot_tx % nb_slot_frame;
    curMsg.proc.frame_rx = abs_frame_rx % MAX_FRAME_NUMBER;
    curMsg.proc.frame_tx = abs_frame_tx % MAX_FRAME_NUMBER;
    curMsg.proc.hfn_rx = abs_frame_rx / MAX_FRAME_NUMBER;
    curMsg.proc.hfn_tx = abs_frame_tx / MAX_FRAME_NUMBER;
    if (UE->received_config_request) {
      curMsg.proc.rx_slot_type = nr_ue_slot_select(&UE->nrUE_config, curMsg.proc.nr_slot_rx);
      curMsg.proc.tx_slot_type = nr_ue_slot_select(&UE->nrUE_config, curMsg.proc.nr_slot_tx);
    } else {
      curMsg.proc.rx_slot_type = NR_DOWNLINK_SLOT;
      curMsg.proc.tx_slot_type = NR_DOWNLINK_SLOT;
    }
    // The Low-PHY places the UL in time (TA included); no timestamp here.
    curMsg.proc.timestamp_tx = 0;

    fd_tick_rrc(f, abs_frame_rx + 1);

    notifiedFIFO_elt_t *newRx = newNotifiedFIFO_elt(sizeof(nr_rxtx_thread_data_t), curMsg.proc.nr_slot_tx, NULL, UE_dl_processing);
    nr_rxtx_thread_data_t *curMsgRx = NotifiedFifoData(newRx);
    *curMsgRx = (nr_rxtx_thread_data_t){.proc = curMsg.proc, .UE = UE};
    const int ret = UE_dl_preprocessing(UE, &curMsgRx->proc, tx_wait_for_dlsch, &curMsgRx->phy_data, &stats_printed);
    if (ret != INT_MAX)
      shiftForNextFrame = ret;

    // n-TimingAdvanceOffset arrives in SIB1, after sync: re-evaluate every slot like UE_thread().
    UE->N_TA_offset = determine_N_TA_offset(UE);
    const uint32_t new_ta = UE->N_TA_offset + UE->timing_advance + UE->timing_advance_ntn;
    if (new_ta != ta_samples) {
      LOG_I(NR_PHY, "[fd-ue] UE %d timing advance %u samples (N_TA_offset %d)\n", UE->Mod_id, new_ta, UE->N_TA_offset);
      ta_samples = new_ta;
    }
    f->ta[abs_slot_tx % FD_SLOT_WINDOW] = new_ta;
    f->last_tx_slot = abs_slot_tx;
    // A slot processSlotTX() will not transmit in is complete already: tell the
    // Low-PHY now, so a burst before it ends without waiting for this slot's TX job.
    f->declared[abs_slot_tx % FD_SLOT_WINDOW] = 0;
    const bool may_send = curMsg.proc.tx_slot_type == NR_UPLINK_SLOT || curMsg.proc.tx_slot_type == NR_MIXED_SLOT;
    if (!may_send && ue_low_phy_client_try_slot_done(c, abs_slot_tx, 0, -1, new_ta))
      f->declared[abs_slot_tx % FD_SLOT_WINDOW] = abs_slot_tx + 1;

    if (get_nrUE_params()->num_dl_actors > 0)
      pushNotifiedFIFO(&f->dl_actors[curMsg.proc.nr_slot_rx % get_nrUE_params()->num_dl_actors].fifo, newRx);
    else {
      // inline: the actor thread is not there to free the message
      newRx->processingFunc(curMsgRx);
      delNotifiedFIFO_elt(newRx);
    }

    // writeBlockSize 0: processSlotTX()'s RU_write() does nothing, the Low-PHY transmits.
    notifiedFIFO_elt_t *newTx = newNotifiedFIFO_elt(sizeof(nr_rxtx_thread_data_t), 0, 0, fd_process_slot_tx);
    nr_rxtx_thread_data_t *curMsgTx = NotifiedFifoData(newTx);
    *curMsgTx = (nr_rxtx_thread_data_t){.proc = curMsg.proc, .UE = UE, .writeBlockSize = 0, .ul_actors = f->ul_actors};
    const int slot_tx = curMsgTx->proc.nr_slot_tx;
    const uint64_t saf = abs_slot_tx;
    const int wait_prev = stream_status == STREAM_STATUS_SYNCED ? 1 : 0;
    curMsgTx->next_barrier = &UE->process_slot_tx_barriers[(saf + 1) % NUM_PROCESS_SLOT_TX_BARRIERS];
    dynamic_barrier_update(&UE->process_slot_tx_barriers[saf % NUM_PROCESS_SLOT_TX_BARRIERS],
                           tx_wait_for_dlsch[slot_tx] + wait_prev,
                           start_process_slot_tx,
                           newTx);
    stream_status = STREAM_STATUS_SYNCED;
    tx_wait_for_dlsch[slot_tx] = 0;
  }
  return NULL;
}

static void fd_start_lowphy(const PHY_VARS_NR_UE *UE, int num_ues)
{
  ue_low_phy_device_cfg_t cfg = {
      .fp = &UE->frame_parms,
      .rx_link = link_type_dl,
      .tx_link = link_type_ul,
      // As filled by nrue_init_openair0() for the monolithic UE.
      .radio_cfg = openair0_cfg_g[UE->rf_map.card],
      // The rfsimulator gNB waits for this TX before sending later samples.
      .lockstep = IS_SOFTMODEM_RFSIM,
      .max_clients = num_ues,
  };

  fd_device = ue_low_phy_device_create(&cfg);
  // As a monolithic initial sync: the band's GSCNs with --ue-scan-carrier, else the configured SSB.
  const int scan_band = get_nrUE_params()->UE_scan_carrier ? nrue_get_band(UE) : 0;
  ue_low_phy_device_enable_cell_search(fd_device, scan_band, UE->frame_parms.ssb_start_subcarrier);
  AssertFatal(ue_low_phy_device_start(fd_device) == 0, "[fd-ue] failed to start the UE Low-PHY\n");
}

void init_NR_UE_fd(PHY_VARS_NR_UE **UE_list, int num_ues)
{
  AssertFatal(UE_list[0]->sl_mode == 0, "[fd-ue] sidelink is not supported in FD-UE mode\n");
  AssertFatal(usrp_tx_thread == 0,
              "[fd-ue] --usrp-tx-thread-config is not supported in FD-UE mode (the Low-PHY has its own TX thread)\n");
  fd_start_lowphy(UE_list[0], num_ues);

  for (int u = 0; u < num_ues; u++) {
    PHY_VARS_NR_UE *UE = UE_list[u];
    fd_ue_t *f = calloc(1, sizeof(*f));
    AssertFatal(f, "OOM\n");
    f->UE = UE;
    ue_low_phy_progress_init(&f->tx_completed, 0);
    ue_low_phy_progress_init(&f->tx_jobs, 0);
    const int num_dl_actors = get_nrUE_params()->num_dl_actors, num_ul_actors = get_nrUE_params()->num_ul_actors;
    f->dl_actors = calloc(num_dl_actors > 0 ? num_dl_actors : 1, sizeof(*f->dl_actors));
    f->ul_actors = calloc(num_ul_actors > 0 ? num_ul_actors : 1, sizeof(*f->ul_actors));
    AssertFatal(f->dl_actors && f->ul_actors, "OOM\n");
    init_ue_actors(NULL, f->dl_actors, f->ul_actors);
    AssertFatal(ue_low_phy_client_open(ue_low_phy_device_port(fd_device), &f->client) == 0,
                "[fd-ue] UE Low-PHY port has no free client slot for UE %d\n",
                u);
    f->dev = (nr_ue_fd_ifdevice_t){
        .read_symbol = fd_read_symbol,
        .write_slot = fd_write_slot,
        .write_prach = fd_write_prach,
        .priv = f,
    };
    UE->fd_ifdevice = &f->dev;

    char name[32];
    snprintf(name, sizeof(name), "UEfd%d", UE->Mod_id);
    threadCreate(&UE->main_thread, UE_fd_thread, f, name, -1, OAI_PRIORITY_RT_MAX);
    if (!IS_SOFTMODEM_NOSTATS) {
      snprintf(name, sizeof(name), "L1_UE_stats_fd_%d", UE->Mod_id);
      threadCreate(&UE->stat_thread, nrL1_UE_stats_thread, UE, name, -1, OAI_PRIORITY_RT_LOW);
    }
  }
}

static void join_thread(pthread_t thread)
{
  const int ret = pthread_join(thread, NULL);
  AssertFatal(ret == 0, "ret=%d\n", ret);
}

// After oai_exit: every instance's thread and actors stop before the port goes away.
void term_NR_UE_fd(PHY_VARS_NR_UE **UE_list, int num_ues)
{
  // Every instance's thread is stopped and asks to leave (without waiting) before any
  // instance waits for its leave: the Low-PHY waits for members, so waiting while
  // another instance is still a member would never finish.
  for (int u = 0; u < num_ues; u++) {
    PHY_VARS_NR_UE *UE = UE_list[u];
    join_thread(UE->main_thread);
    fd_ue_t *f = UE->fd_ifdevice->priv;
    ue_low_phy_client_leave(&f->client, f->last_tx_slot, 0);
  }
  for (int u = 0; u < num_ues; u++) {
    PHY_VARS_NR_UE *UE = UE_list[u];
    fd_ue_t *f = UE->fd_ifdevice->priv;
    for (int i = 0; i < get_nrUE_params()->num_ul_actors; i++)
      shutdown_actor(&f->ul_actors[i]);
    for (int i = 0; i < get_nrUE_params()->num_dl_actors; i++)
      shutdown_actor(&f->dl_actors[i]);
    if (!IS_SOFTMODEM_NOSTATS)
      join_thread(UE->stat_thread);
    if (!ue_low_phy_client_leave(&f->client, f->last_tx_slot, 200000))
      LOG_W(NR_PHY, "[fd-ue] UE %d: Low-PHY did not confirm leaving\n", UE->Mod_id);
    UE->fd_ifdevice = NULL;
    ue_low_phy_client_close(&f->client);
    free(f->dl_actors);
    free(f->ul_actors);
    ue_low_phy_progress_destroy(&f->tx_completed);
    ue_low_phy_progress_destroy(&f->tx_jobs);
    free(f);
  }
  ue_low_phy_device_destroy(fd_device);
  fd_device = NULL;
}
