/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Spectrum RAN function, DU/MAC side: the per-slot scan that publishes the
 * free tiles, the hard-reservation of the operator's sensing slots and (Aerial
 * only) a dummy PUSCH that forces the L1 to capture sensing IQ.
 *
 * The scheduler only calls the hooks in ran_func_spectrum_extern.h; sensing
 * configuration (E3Configuration) and sensing state live here, not in the MAC.
 * Sensing observes a receiver, so it is UL-only and needs a TDD pattern to
 * name slots in; there is no DL counterpart in the gNB and FDD is not covered.
 */

#include <stdatomic.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>

#include "ran_func_spectrum.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "common/utils/nr/nr_common.h"
#include "common/utils/ds/seqlock.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/ran_context.h"
#include "openair2/E3AP/config/e3_config.h"
#include "openair2/E3AP/service_models/pub_channel.h"

/* ---- Configuration (E3Configuration) ----
 * Read once, at the first attach; the section is a singleton, so every cell
 * sees the same values. */
typedef struct {
  int num_target_slots;
  int *target_slots; /* slot indices within the TDD period */
  e3_spectrum_pusch_cfg_t pusch;
} e3_spectrum_cfg_t;

static void e3_spectrum_read_config(e3_spectrum_cfg_t *c)
{
  int *target_slots = NULL, *beams = NULL;
  int n_target = 0, n_beams = 0;
  memset(c, 0, sizeof(*c));

  // clang-format off
  paramdef_t p[] = {
    /* optname                    helpstr                                                    paramflags       value                    defval             type          numelt */
    {"sensing_target_slots",    "Slots (modulo the TDD period) hard-reserved for sensing: no UE UL grant there, the dApp sees a full symbol 0-13 range", PARAMFLAG_NOFREE, .iptr = NULL,             .defintarrayval = NULL, TYPE_INTARRAY, 0},
    {"sensing_pusch_mcs",       "MCS index (table 0) of the sensing PUSCH PDU (Aerial)",   0,               .iptr = &c->pusch.mcs,          .defintval = 9,    TYPE_INT,      0},
    {"sensing_pusch_rb_size",   "PRBs the sensing PUSCH PDU covers (Aerial)",              0,               .iptr = &c->pusch.rb_size,      .defintval = 1,    TYPE_INT,      0},
    {"sensing_pusch_rb_start",  "Starting PRB (BWP-relative) of the sensing PUSCH PDU",    0,               .iptr = &c->pusch.rb_start,     .defintval = 0,    TYPE_INT,      0},
    {"sensing_pusch_nrOfLayers","nrOfLayers of the sensing PUSCH PDU; drives the dig_bf_interface fanout, not spatial multiplexing", 0, .iptr = &c->pusch.nr_of_layers, .defintval = 1, TYPE_INT, 0},
    {"sensing_pusch_beams",     "Beam indices the sensing PUSCH PDU is fanned out to (empty = beam 0)", PARAMFLAG_NOFREE, .iptr = NULL,     .defintarrayval = NULL, TYPE_INTARRAY, 0},
  };
  checkedparam_t checks[] = {
    {.s5 = {NULL}},
    {.s2 = {config_check_intrange, {0, 27}}},
    {.s2 = {config_check_intrange, {1, MAX_BWP_SIZE}}},
    {.s2 = {config_check_intrange, {0, MAX_BWP_SIZE - 1}}},
    {.s2 = {config_check_intrange, {1, 4}}},
    {.s5 = {NULL}},
  };
  // clang-format on
  config_set_checkfunctions(p, checks, sizeofArray(p));

  if (config_get(config_get_if(), p, sizeofArray(p), E3CONFIG_SECTION) < 0)
    return; /* no E3Configuration section: sensing stays off */

  n_target = p[0].numelt;
  target_slots = p[0].iptr;
  n_beams = p[5].numelt;
  beams = p[5].iptr;

  if (n_target > 0 && target_slots) {
    c->target_slots = malloc(n_target * sizeof(*c->target_slots));
    AssertFatal(c->target_slots != NULL, "out of memory reading sensing_target_slots\n");
    memcpy(c->target_slots, target_slots, n_target * sizeof(*c->target_slots));
    c->num_target_slots = n_target;
  }

  if (n_beams > 0 && beams) {
    AssertFatal(n_beams <= SENSING_MAX_BEAMS, "sensing_pusch_beams: too many entries (%d, max %d)\n", n_beams, SENSING_MAX_BEAMS);
    for (int i = 0; i < n_beams; i++) {
      AssertFatal(beams[i] >= 0, "sensing_pusch_beams[%d]=%d must be >= 0\n", i, beams[i]);
      c->pusch.beams[i] = beams[i];
    }
    c->pusch.num_beams = n_beams;
  } else {
    c->pusch.num_beams = 1; /* default: a single beam at index 0 */
    c->pusch.beams[0] = 0;
  }
}

/* What one (beam, slot) seqlock protects: the ranges the UL pass found free. */
typedef struct {
  uint8_t n;
  sensing_range_t ranges[MAX_SENSING_RANGES];
} sensing_snapshot_t;

/* ---- Per-cell state ----
 * Allocated only for a cell sensing is enabled on, so a gNB that does not use
 * sensing carries none of it. The (beam, slot) range snapshot is the largest
 * part (~1.25 MiB); it used to be embedded, unconditionally, in every
 * NR_COMMON_channels_t of every cell of every build. */
typedef struct {
  const nr_cell_sched_t *cell;
  int period; /* slots per TDD period the target slots are indices into */
  bool reserved[NR_MAX_SLOTS_PER_FRAME]; /* slot-in-period -> hard-reserved for sensing */
  e3_spectrum_pusch_cfg_t pusch;
  /* Per-(beam, slot) snapshot of the sensing ranges: written each UL pass by
   * record_sensing_ranges(), read async via nr_mac_get_sensing_ranges() under a
   * per-slot seqlock (writer never blocks, reader retries on a torn read). */
  struct {
    seqlock_t seq;
    sensing_snapshot_t snap;
  } snapshot[MAX_NUM_BEAM_PERIODS][NR_MAX_SLOTS_PER_FRAME];
} e3_spectrum_cell_t;

static e3_spectrum_cell_t *g_cells[NR_MAX_CELLS];

static e3_spectrum_cell_t *find_cell(const nr_cell_sched_t *cell)
{
  for (int i = 0; i < NR_MAX_CELLS; i++)
    if (g_cells[i] != NULL && g_cells[i]->cell == cell)
      return g_cells[i];
  return NULL;
}

void e3_spectrum_mac_attach_cell(nr_cell_sched_t *cell)
{
  static e3_spectrum_cfg_t cfg;
  static bool cfg_read;
  if (!cfg_read) {
    e3_spectrum_read_config(&cfg);
    cfg_read = true;
  }
  if (cfg.num_target_slots == 0)
    return;

  const frame_structure_t *fs = &cell->frame_structure;
  if (fs->frame_type == FDD) {
    LOG_W(NR_MAC, "sensing_target_slots is set but sensing needs a TDD pattern to index slots in: not enabled on this FDD cell\n");
    return;
  }
  AssertFatal(find_cell(cell) == NULL, "sensing is already attached to this cell\n");
  int free_idx = 0;
  while (free_idx < NR_MAX_CELLS && g_cells[free_idx] != NULL)
    free_idx++;
  AssertFatal(free_idx < NR_MAX_CELLS, "no room to attach sensing to another cell\n");

  e3_spectrum_cell_t *rec = calloc(1, sizeof(*rec));
  AssertFatal(rec != NULL, "out of memory allocating the sensing state\n");
  rec->cell = cell;
  rec->period = fs->numb_slots_period;
  rec->pusch = cfg.pusch;
  for (int i = 0; i < cfg.num_target_slots; i++) {
    const int s = cfg.target_slots[i];
    AssertFatal(s >= 0 && s < rec->period,
                "sensing_target_slots[%d]=%d out of range [0,%d] for this TDD period\n",
                i,
                s,
                rec->period - 1);
    rec->reserved[s] = true;
  }
  g_cells[free_idx] = rec;
  LOG_I(NR_MAC, "Sensing mode enabled: %d hard-reserved slots\n", cfg.num_target_slots);
}

void e3_spectrum_mac_detach_cell(nr_cell_sched_t *cell)
{
  for (int i = 0; i < NR_MAX_CELLS; i++) {
    if (g_cells[i] != NULL && g_cells[i]->cell == cell) {
      free(g_cells[i]);
      g_cells[i] = NULL;
    }
  }
}

const e3_spectrum_pusch_cfg_t *e3_spectrum_pusch_cfg(const nr_cell_sched_t *cell)
{
  const e3_spectrum_cell_t *rec = find_cell(cell);
  return rec ? &rec->pusch : NULL;
}

/* Is this (absolute) slot hard-reserved for sensing, i.e. its slot-in-TDD-period
 * is in the configured sensing_target_slots list? When true, the scheduler
 * blocks every UE allocator from it, then frees it just before the scan. */
bool nr_mac_ul_slot_is_sensing_reserved(const nr_cell_sched_t *cell, int slot)
{
  const e3_spectrum_cell_t *rec = find_cell(cell);
  return rec != NULL && rec->reserved[slot % rec->period];
}

/* Beams active this period (1 unless multi-beam is configured). */
static inline int nr_mac_active_beams(const nr_cell_sched_t *cell)
{
  return cell->beam_info.beam_allocation ? cell->beam_info.config.beams_per_period : 1;
}

/* Hard-reserve the just-reseeded UL slot for sensing: stamp every beam's row
 * 0x3FFF so no UE allocator can claim it. No-op unless the slot is reserved.
 * prev_slot is the absolute slot the scheduler reseeded. */
void nr_mac_sensing_reserve_ul_slot(nr_cell_sched_t *cell, int prev_slot, frame_t frame)
{
  if (!nr_mac_ul_slot_is_sensing_reserved(cell, prev_slot))
    return;
  const int size = cell->vrb_map_UL_size;
  const int slots_frame = cell->frame_structure.numb_slots_frame;
  const int num_beams = nr_mac_active_beams(cell);
  for (int b = 0; b < num_beams; b++) {
    uint16_t *row = &cell->common_channels.vrb_map_UL[b][(prev_slot % size) * MAX_BWP_SIZE];
    for (int prb = 0; prb < MAX_BWP_SIZE; prb++)
      row[prb] = 0x3FFF;
  }
  if ((frame & 0x7F) == 0)
    LOG_I(NR_MAC,
          "Sensing slot reserved: %d.%d -- UE UL blocked\n",
          (prev_slot / slots_frame) % MAX_FRAME_NUMBER,
          prev_slot % slots_frame);
}

/* Undo the reserve for the current slot just before the scan, restoring the row
 * to ulprbbl so the scanner sees a clean full-PRB window. No-op unless reserved. */
void nr_mac_sensing_restore_ul_slot(nr_cell_sched_t *cell, frame_t frame, slot_t slot)
{
  if (!nr_mac_ul_slot_is_sensing_reserved(cell, slot))
    return;
  const int slots_frame = cell->frame_structure.numb_slots_frame;
  const int buf_idx = ul_buffer_index(frame, slot, slots_frame, cell->vrb_map_UL_size);
  const int num_beams = nr_mac_active_beams(cell);
  for (int b = 0; b < num_beams; b++) {
    uint16_t *row = &cell->common_channels.vrb_map_UL[b][buf_idx * MAX_BWP_SIZE];
    /* Reset to the static block list so the reserved slot is clean for sensing. */
    memcpy(row, &cell->ulprbbl, sizeof(uint16_t) * MAX_BWP_SIZE);
  }
}

/* ---- Sensing publish notification ----
 * Wakes a consumer (the Spectrum SM worker) when a new sensing-range write
 * happens. Only the small metadata travels here, in a ring indexed by the
 * channel's publish_sequence: the MAC tick can bunch several UL slots
 * back-to-back, so a late consumer drains every publish in order instead of
 * keeping only the newest. 8 entries outlast any observed burst while the
 * deepest catch-up stays inside the 10 ms after which the per-(beam, slot)
 * range storage is recycled. See openair2/E3AP/service_models/pub_channel.h. */
#define SENSING_PUB_RING_SLOTS 8u
static pub_channel_t g_sensing_chan = PUB_CHANNEL_INIT;
static nr_mac_sensing_publish_meta_t g_sensing_ring[SENSING_PUB_RING_SLOTS];

/* Reader for the sensing-range snapshot (declared in the types header). Serves
 * the first cell sensing is attached to -- cell 0 on every deployment so far,
 * matching the beam-0 assumption of the writer. Lock-free seqlock against the
 * writer below; bounded spin (8 tries) so the worker stays responsive even when
 * it lands on the slot being written. */
bool nr_mac_get_sensing_ranges(int mod_id, int beam, int slot, sensing_range_t *out_ranges, int max_out, uint8_t *out_n)
{
  (void)mod_id;
  if (!out_ranges || !out_n || max_out <= 0)
    return false;
  if (beam < 0 || beam >= MAX_NUM_BEAM_PERIODS)
    return false;
  e3_spectrum_cell_t *rec = NULL;
  for (int i = 0; i < NR_MAX_CELLS && rec == NULL; i++)
    rec = g_cells[i];
  if (rec == NULL)
    return false;
  const int idx = slot % NR_MAX_SLOTS_PER_FRAME;

  sensing_snapshot_t snap;
  if (!seqlock_read(&rec->snapshot[beam][idx].seq, &snap, &rec->snapshot[beam][idx].snap, sizeof(snap), 8))
    return false;

  uint8_t n = snap.n;
  if (n > max_out)
    n = (uint8_t)max_out;
  memcpy(out_ranges, snap.ranges, n * sizeof(sensing_range_t));
  *out_n = n;
  return true;
}

/* Writer side, called after nr_scan_sensing_tiles(). Stores the (beam, slot)
 * ranges under the seqlock, then wakes one waiter. One call = one wake event. */
static void record_sensing_ranges(e3_spectrum_cell_t *rec,
                                  int beam,
                                  int frame,
                                  int slot,
                                  const sensing_range_t *ranges,
                                  int n_ranges)
{
  if (beam < 0 || beam >= MAX_NUM_BEAM_PERIODS)
    return;
  if (n_ranges < 0)
    n_ranges = 0;
  if (n_ranges > MAX_SENSING_RANGES)
    n_ranges = MAX_SENSING_RANGES;
  const int idx = slot % NR_MAX_SLOTS_PER_FRAME;

  /* Publish into the per-(beam, slot) seqlock; lock-free, the random-access
   * reader nr_mac_get_sensing_ranges() retries on a torn read. Only the used
   * prefix is written; the reader takes the whole snapshot and uses snap.n. */
  sensing_snapshot_t snap;
  snap.n = (uint8_t)n_ranges;
  if (n_ranges > 0)
    memcpy(snap.ranges, ranges, (size_t)n_ranges * sizeof(sensing_range_t));
  seqlock_write(&rec->snapshot[beam][idx].seq,
                &rec->snapshot[beam][idx].snap,
                &snap,
                offsetof(sensing_snapshot_t, ranges) + (size_t)n_ranges * sizeof(sensing_range_t));

  /* Wake the Spectrum SM worker: stamp this publish's ring slot + bump, all
   * under the lock. The ranges are NOT copied here -- consumers fetch them via
   * nr_mac_get_sensing_ranges() under its own per-(beam, slot) seqlock. */
  const uint64_t ts_ns = pub_channel_now_ns();
  pub_channel_lock(&g_sensing_chan);
  nr_mac_sensing_publish_meta_t *m = &g_sensing_ring[(g_sensing_chan.publish_sequence + 1) % SENSING_PUB_RING_SLOTS];
  m->beam = (uint16_t)beam;
  m->frame = (uint16_t)frame;
  m->slot = (uint16_t)slot;
  m->timestamp_ns = ts_ns;
  pub_channel_publish_and_wake(&g_sensing_chan);
  pub_channel_unlock(&g_sensing_chan);
}

/* Block until a publish newer than *inout_seq arrives (timeout_ns: 0 = poll,
 * UINT64_MAX = forever, else a CLOCK_MONOTONIC deadline). *inout_seq is the
 * consumer's read cursor: each true return hands back the oldest unconsumed
 * publish and advances by one, so bunched publishes drain across successive
 * calls. A fresh consumer (cursor 0) starts at the newest; one that fell
 * behind the ring skips ahead to the oldest meta still held. */
bool nr_mac_wait_for_sensing_publish(uint64_t timeout_ns, uint64_t *inout_seq, nr_mac_sensing_publish_meta_t *out_meta)
{
  if (!inout_seq || !out_meta)
    return false;

  nr_mac_sensing_publish_meta_t snap[SENSING_PUB_RING_SLOTS];
  uint64_t newest = *inout_seq;
  if (!pub_channel_wait(&g_sensing_chan, g_sensing_ring, snap, sizeof(snap), timeout_ns, &newest))
    return false;

  uint64_t next = *inout_seq + 1;
  if (*inout_seq == 0) {
    next = newest; /* fresh consumer: no history owed, start at the newest */
  } else if (next + SENSING_PUB_RING_SLOTS <= newest) {
    /* Overran the ring: the older metas are already overwritten. Should not
     * happen (the ring outlasts any observed tick burst); rate-limited. */
    static unsigned overruns;
    if (overruns++ % 100 == 0)
      LOG_W(NR_MAC,
            "sensing publish ring overrun: consumer %" PRIu64 " publishes behind (count %u)\n",
            newest - *inout_seq,
            overruns);
    next = newest - SENSING_PUB_RING_SLOTS + 1;
  }
  *out_meta = snap[next % SENSING_PUB_RING_SLOTS];
  *inout_seq = next;
  return true;
}

/* Force-wake every nr_mac_wait_for_sensing_publish() waiter by bumping
 * publish_seq (so the predicate flips) and broadcasting the condvar; used to
 * unblock indefinite waiters at SM teardown. */
void nr_mac_signal_sensing_shutdown(void)
{
  pub_channel_signal_shutdown(&g_sensing_chan);
}
/* OR a (PRB-span x symbol-run) rectangle into occupancy[]: clamp the symbols to
 * the 0..14 grid, then set the symbol bitmap on every in-grid PRB of the span. */
static inline void mark_occupancy_rect(uint16_t *occupancy, int rb_start, int num_rb, int start_symbol, int num_symbols)
{
  if (start_symbol < 0)
    start_symbol = 0;
  if (start_symbol + num_symbols > 14)
    num_symbols = 14 - start_symbol;
  if (num_symbols <= 0)
    return;
  const uint16_t symbol_mask = SL_to_bitmap(start_symbol, num_symbols);
  for (int prb = rb_start; prb < rb_start + num_rb; prb++)
    if (prb >= 0 && prb < MAX_BWP_SIZE)
      occupancy[prb] |= symbol_mask;
}

/* OR the (PRB x symbol) footprint of PUCCH resource `res_id` (both freq hops,
 * absolute carrier PRB) into occupancy[]. Used for the SR and periodic-CSI PUCCH
 * resources, which the UE transmits autonomously on their occasions. */
static void nr_sensing_add_pucch_occupancy(const NR_PUCCH_Config_t *pc,
                                           long res_id,
                                           int bwp_start,
                                           uint16_t occupancy[MAX_BWP_SIZE])
{
  if (pc == NULL || pc->resourceToAddModList == NULL)
    return;
  for (int j = 0; j < pc->resourceToAddModList->list.count; j++) {
    const NR_PUCCH_Resource_t *r = pc->resourceToAddModList->list.array[j];
    if (r->pucch_ResourceId != res_id)
      continue;
    int len = 1; /* PRBs; format0/1/4 occupy 1 PRB */
    uint16_t mask = 0; /* symbol bitmap */
    switch (r->format.present) {
      case NR_PUCCH_Resource__format_PR_format0:
        mask = SL_to_bitmap(r->format.choice.format0->startingSymbolIndex, r->format.choice.format0->nrofSymbols);
        break;
      case NR_PUCCH_Resource__format_PR_format1:
        mask = SL_to_bitmap(r->format.choice.format1->startingSymbolIndex, r->format.choice.format1->nrofSymbols);
        break;
      case NR_PUCCH_Resource__format_PR_format2:
        len = r->format.choice.format2->nrofPRBs;
        mask = SL_to_bitmap(r->format.choice.format2->startingSymbolIndex, r->format.choice.format2->nrofSymbols);
        break;
      case NR_PUCCH_Resource__format_PR_format3:
        len = r->format.choice.format3->nrofPRBs;
        mask = SL_to_bitmap(r->format.choice.format3->startingSymbolIndex, r->format.choice.format3->nrofSymbols);
        break;
      case NR_PUCCH_Resource__format_PR_format4:
        mask = SL_to_bitmap(r->format.choice.format4->startingSymbolIndex, r->format.choice.format4->nrofSymbols);
        break;
      default:
        return;
    }
    if (mask == 0)
      return;
    const int hop = (r->secondHopPRB != NULL) ? (int)*r->secondHopPRB : -1;
    for (int p = 0; p < len; p++) {
      const int rb0 = bwp_start + r->startingPRB + p;
      if (rb0 >= 0 && rb0 < MAX_BWP_SIZE)
        occupancy[rb0] |= mask;
      if (hop >= 0) {
        const int rb1 = bwp_start + hop + p;
        if (rb1 >= 0 && rb1 < MAX_BWP_SIZE)
          occupancy[rb1] |= mask;
      }
    }
    return; /* resource found and reserved */
  }
}

/* Reserve the PRACH msg1 band, but only on actual PRACH-occasion slots and only
 * its symbols. Re-derived from cell config (mirrors schedule_nr_prach()) since
 * the reserved-slot reset wiped it from vrb_map. */
static void nr_sensing_add_prach_occupancy(const nr_cell_sched_t *cell,
                                           frame_t frame,
                                           slot_t slot,
                                           uint16_t occupancy[MAX_BWP_SIZE])
{
  const NR_COMMON_channels_t *cc = &cell->common_channels;
  const NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  if (scc == NULL || !is_ul_slot(slot, &cell->frame_structure))
    return;
  NR_BWP_UplinkCommon_t *iul = scc->uplinkConfigCommon->initialUplinkBWP;
  if (iul->rach_ConfigCommon == NULL || iul->rach_ConfigCommon->choice.setup == NULL)
    return;
  const NR_RACH_ConfigCommon_t *rcc = iul->rach_ConfigCommon->choice.setup;
  const NR_RACH_ConfigGeneric_t *rcg = &rcc->rach_ConfigGeneric;
  const nfapi_nr_config_request_scf_t *cfg = &cell->config;
  const uint8_t config_index = rcg->prach_ConfigurationIndex;
  const uint8_t fdm = cfg->prach_config.num_prach_fd_occasions.value;
  const int ul_mu = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
  NR_MsgA_ConfigCommon_r16_t *msgacc =
      (iul->ext1 && iul->ext1->msgA_ConfigCommon_r16) ? iul->ext1->msgA_ConfigCommon_r16->choice.setup : NULL;
  const int mu = nr_get_prach_or_ul_mu(msgacc, rcc, ul_mu);
  const frequency_range_t fr = get_freq_range_from_arfcn(scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA);
  uint16_t RA_sfn_index = 0xffff;
  if (!get_nr_prach_sched_from_info(cc->prach_info, config_index, frame, slot, mu, fr, &RA_sfn_index, cc->frame_type))
    return; /* not a PRACH occasion on this slot — leave PRBs free */

  const int bwp_start = NRRIV2PRBOFFSET(iul->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  const int n_ra_rb = get_N_RA_RB(cfg->prach_config.prach_sub_c_spacing.value, ul_mu);
  const int rb0 = bwp_start + rcg->msg1_FrequencyStart;
  const int w = n_ra_rb * (fdm > 0 ? fdm : 1);
  const uint16_t format0 = cc->prach_info.format & 0xff;
  const uint32_t N_dur = (format0 < 4) ? 14u : (uint32_t)cc->prach_info.N_dur;
  const int s0 = cc->prach_info.start_symbol;
  const int ns = (int)(cc->prach_info.N_t_slot * N_dur);
  mark_occupancy_rect(occupancy, rb0, w, s0, ns);
}

/* Mark every UL reception the gNB will actually receive this slot
 * (PUSCH/PUCCH/SRS, from UL_tti_req_ahead) — the dynamic ones config-gating
 * can't predict. PRACH is handled separately; absolute carrier PRB. */
static void nr_sensing_add_committed_ul_occupancy(const nr_cell_sched_t *cell,
                                                  frame_t frame,
                                                  slot_t slot,
                                                  uint16_t occupancy[MAX_BWP_SIZE])
{
  const int slots_frame = cell->frame_structure.numb_slots_frame;
  const int idx = ul_buffer_index(frame, slot, slots_frame, cell->UL_tti_req_ahead_size);
  const nfapi_nr_ul_tti_request_t *req = &cell->UL_tti_req_ahead[idx];
  for (int i = 0; i < req->n_pdus; i++) {
    const nfapi_nr_ul_tti_request_number_of_pdus_t *pdu = &req->pdus_list[i];
    int rb0 = 0, w = 0, hop = -1, s0 = 0, ns = 0;
    switch (pdu->pdu_type) {
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE: {
        const nfapi_nr_pusch_pdu_t *p = &pdu->pusch_pdu;
        rb0 = p->bwp_start + p->rb_start;
        w = p->rb_size;
        s0 = p->start_symbol_index;
        ns = p->nr_of_symbols;
        break;
      }
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE: {
        const nfapi_nr_pucch_pdu_t *p = &pdu->pucch_pdu;
        rb0 = p->bwp_start + p->prb_start;
        w = (p->prb_size > 0) ? p->prb_size : 1;
        s0 = p->start_symbol_index;
        ns = p->nr_of_symbols;
        if (p->freq_hop_flag)
          hop = p->bwp_start + p->second_hop_prb;
        break;
      }
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE: {
        const nfapi_nr_srs_pdu_t *p = &pdu->srs_pdu;
        rb0 = p->bwp_start;
        w = p->bwp_size;
        s0 = NR_SYMBOLS_PER_SLOT - 1 - p->time_start_position;
        ns = 1 << p->num_symbols;
        break;
      }
      default: /* PRACH: see nr_sensing_add_prach_occupancy() */
        continue;
    }
    if (w <= 0 || ns <= 0)
      continue;
    mark_occupancy_rect(occupancy, rb0, w, s0, ns);
    if (hop >= 0)
      mark_occupancy_rect(occupancy, hop, w, s0, ns);
  }
}

/* Rebuild the periodic UL control occupancy (SR / periodic CSI / SRS PUCCH) the
 * UE transmits autonomously per RRC config, which the reserved-slot VRB reset
 * erased. Occasion-gated (only on each resource's configured slots) and written
 * into occupancy[] for the caller to OR in — never touches vrb_map or scheduling.
 * occupancy[] is indexed by absolute carrier PRB; returns the PRB count reserved. */
static int nr_sensing_add_ul_ctrl_occupancy(gNB_MAC_INST *mac,
                                            const nr_cell_sched_t *cell,
                                            frame_t frame,
                                            slot_t slot,
                                            uint16_t occupancy[MAX_BWP_SIZE])
{
  const int slots_frame = cell->frame_structure.numb_slots_frame;
  const int sfn_sf = frame * slots_frame + slot;

  /* Cover every RRC-connected UE, not just active ones: an inactive UE still
   * transmits its configured SR/CSI/SRS per RRC (e.g. SR to recover from a UL
   * failure), so an is_active gate would leak those PRBs into the free-tile
   * telemetry. Telemetry-only, so no scheduling impact. */
  FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
    const NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
    const int bwp_start = ul_bwp->BWPStart;
    const int bwp_size = ul_bwp->BWPSize;
    const NR_PUCCH_Config_t *pc = ul_bwp->pucch_Config;

    /* (1) SR: reserve the SR PUCCH resource only on its periodic occasion,
     * mirroring nr_sr_reporting()'s (sfn_sf - offset) % period test. */
    if (pc != NULL && pc->schedulingRequestResourceToAddModList != NULL) {
      for (int i = 0; i < pc->schedulingRequestResourceToAddModList->list.count; i++) {
        const NR_SchedulingRequestResourceConfig_t *sr = pc->schedulingRequestResourceToAddModList->list.array[i];
        if (sr->resource == NULL)
          continue;
        int period = 0, offset = 0;
        find_period_offset_SR(sr, &period, &offset);
        if (period <= 0 || ((sfn_sf - offset) % period) != 0)
          continue;
        nr_sensing_add_pucch_occupancy(pc, *sr->resource, bwp_start, occupancy);
      }
    }

    /* (2) Periodic CSI report: reserve the CSI PUCCH resource only on its
     * occasion, mirroring nr_csi_meas_reporting()'s csi_period_offset test. */
    const NR_CSI_MeasConfig_t *csi = UE->sc_info.csi_MeasConfig;
    if (pc != NULL && csi != NULL && csi->csi_ReportConfigToAddModList != NULL) {
      for (int i = 0; i < csi->csi_ReportConfigToAddModList->list.count; i++) {
        const NR_CSI_ReportConfig_t *rep = csi->csi_ReportConfigToAddModList->list.array[i];
        if (rep->reportConfigType.present != NR_CSI_ReportConfig__reportConfigType_PR_periodic
            || rep->reportConfigType.choice.periodic == NULL)
          continue;
        int period = 0, offset = 0;
        csi_period_offset(rep, NULL, &period, &offset);
        if (period <= 0 || ((sfn_sf - offset) % period) != 0)
          continue;
        if (rep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.count <= 0)
          continue;
        const NR_PUCCH_CSI_Resource_t *cres = rep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.array[0];
        nr_sensing_add_pucch_occupancy(pc, cres->pucch_Resource, bwp_start, occupancy);
      }
    }

    /* (3) Periodic SRS: gate on this slot's occasion, then reserve the SRS
     * symbol mask across the full BWP — mirrors nr_configure_srs()'s own
     * vrb_map_UL occupancy. No-op when SRS is not configured (do_SRS=0). */
    const NR_SRS_Config_t *sc = ul_bwp->srs_Config;
    if (sc != NULL && sc->srs_ResourceSetToAddModList != NULL && sc->srs_ResourceToAddModList != NULL) {
      for (int rs = 0; rs < sc->srs_ResourceSetToAddModList->list.count; rs++) {
        const NR_SRS_ResourceSet_t *set = sc->srs_ResourceSetToAddModList->list.array[rs];
        if (set->resourceType.present != NR_SRS_ResourceSet__resourceType_PR_periodic || set->srs_ResourceIdList == NULL)
          continue;
        for (int r1 = 0; r1 < set->srs_ResourceIdList->list.count; r1++) {
          for (int r2 = 0; r2 < sc->srs_ResourceToAddModList->list.count; r2++) {
            const NR_SRS_Resource_t *res = sc->srs_ResourceToAddModList->list.array[r2];
            if (*set->srs_ResourceIdList->list.array[r1] != res->srs_ResourceId
                || res->resourceType.present != NR_SRS_Resource__resourceType_PR_periodic)
              continue;
            const uint16_t period = srs_period[res->resourceType.choice.periodic->periodicityAndOffset_p.present];
            const uint16_t offset = get_nr_srs_offset(res->resourceType.choice.periodic->periodicityAndOffset_p);
            if (period == 0 || ((sfn_sf - offset) % period) != 0)
              continue;
            const int num = 1 << res->resourceMapping.nrofSymbols; /* 1,2,4 */
            const int l0 = NR_SYMBOLS_PER_SLOT - 1 - res->resourceMapping.startPosition;
            mark_occupancy_rect(occupancy, bwp_start, bwp_size, l0, num);
          }
        }
      }
    }
  }

  /* (4) Cell-common PRACH (occasion-gated): the band-edge RA region UEs
   * transmit preambles on, also wiped from vrb_map on reserved slots. */
  nr_sensing_add_prach_occupancy(cell, frame, slot, occupancy);

  /* (5) Committed UL receptions for this slot (PUSCH/PUCCH/SRS) from the gNB's
   * own scheduled-reception list — catches dynamic HARQ-ACK PUCCH and PUSCH at
   * their exact locations, which periodic config-gating (1)-(3) cannot predict. */
  nr_sensing_add_committed_ul_occupancy(cell, frame, slot, occupancy);

  int n_prb = 0;
  for (int rb = 0; rb < MAX_BWP_SIZE; rb++)
    if (occupancy[rb])
      n_prb++;
  return n_prb;
}

/* Part 2: Sensing-tile scan. Finds the free (symbol, PRB-span) rectangles in a
 * LOCAL copy of vrb_map_UL (ORing in occasion-gated UL-control occupancy) and
 * emits one absolute-PRB sensing_range_t per tile. Telemetry-only: never writes
 * the live vrb_map. Returns 0 when sensing is off or on non-UL slots. The tiles
 * are what the Spectrum SM (RF=1) publishes. */
static int nr_scan_sensing_tiles(gNB_MAC_INST *mac,
                                 nr_cell_sched_t *cell,
                                 frame_t frame,
                                 slot_t slot,
                                 sensing_range_t *ranges,
                                 int max_ranges)
{
  if (find_cell(cell) == NULL)
    return 0;

  /* Real UL/MIXED gate first: get_ul_bitmap() returns 0x3FFF for any non-MIXED
   * slot, so without this check a pure DL slot would look fully UL and we'd
   * publish bogus ranges for it. */
  if (!is_ul_or_mixed_slot(slot, &cell->frame_structure))
    return 0;

  /* Safe now: 0x3FFF for UL slots, just the UL-symbol bits for MIXED. */
  const uint16_t ul_bitmap = get_ul_bitmap(&cell->frame_structure, slot);
  if (ul_bitmap == 0)
    return 0;

  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  /* Scan the FULL UL carrier (absolute PRBs), not the initial UL BWP: bounding
   * to the BWP hid PRBs >= 75 from the tiles, blanking the band the detector
   * must police. Safe because every UL allocator still notches itself out of
   * vrb_map wherever it lands. */
  const struct NR_FrequencyInfoUL *ful = scc->uplinkConfigCommon->frequencyInfoUL;
  int bwp_size = ful->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;
  int bwp_start = ful->scs_SpecificCarrierList.list.array[0]->offsetToCarrier;

  int slots_frame = cell->frame_structure.numb_slots_frame;
  const int buf_idx = ul_buffer_index(frame, slot, slots_frame, cell->vrb_map_UL_size);
  /* beam 0 for now */
  uint16_t *vrb_map_UL = &cell->common_channels.vrb_map_UL[0][buf_idx * MAX_BWP_SIZE];

  /* Add the RRC-configured periodic UL control occupancy (SR + periodic CSI +
   * SRS) the UE transmits autonomously but that the reserved-slot VRB reset
   * erased — occasion-gated, OR'd into the LOCAL sense_vrb copy only (scheduling
   * untouched), so those cells aren't emitted as free tiles on their occasions.
   * See nr_sensing_add_ul_ctrl_occupancy(). */
  uint16_t ctrl_occ[MAX_BWP_SIZE];
  memset(ctrl_occ, 0, sizeof(ctrl_occ));
  const int n_ctrl_prb = nr_sensing_add_ul_ctrl_occupancy(mac, cell, frame, slot, ctrl_occ);

  /* Build the block-subtracted, control-occupancy-augmented snapshot: subtract
   * the dApp block (when present), then OR the per-slot control occupancy. */
  uint16_t sense_vrb[MAX_BWP_SIZE];
  for (int rb = 0; rb < MAX_BWP_SIZE; rb++)
    sense_vrb[rb] = vrb_map_UL[rb] | ctrl_occ[rb];
  if (n_ctrl_prb > 0)
    LOG_D(NR_MAC, "%d.%d sensing mask: %d PRB(s) reserved for UL control\n", frame, slot, n_ctrl_prb);

  /* PRB-granular scan: for each free UL symbol, emit one sensing_range_t per
   * contiguous PRB span clear at that symbol (num_symbols = 1). Anything already
   * in vrb_map_UL (PRACH/SRS/PUCCH/SR/CSI/PUSCH/Msg3) excludes itself; the dApp
   * may coalesce adjacent tiles. */
  int n_ranges = 0;
  for (int sym = 0; sym < 14 && n_ranges < max_ranges; sym++) {
    if (!(ul_bitmap & (1u << sym)))
      continue;
    const uint16_t sym_bit = (uint16_t)(1u << sym);

    int prb = bwp_start;
    const int prb_end = bwp_start + bwp_size;
    while (prb < prb_end && n_ranges < max_ranges) {
      /* skip PRBs occupied at this symbol, then measure the next free run */
      while (prb < prb_end && (sense_vrb[prb] & sym_bit))
        prb++;
      const int run_start = prb;
      while (prb < prb_end && !(sense_vrb[prb] & sym_bit))
        prb++;
      const int run_len = prb - run_start;
      if (run_len <= 0)
        break;

      /* rb_start is ABSOLUTE (carrier/grid PRB index, the same coordinate as
       * the vrb_map_UL[prb] index walked here and the L1 rxdataF tap) so
       * dApps line it up directly. The Aerial dummy-PUSCH path converts back
       * to BWP-relative on its way into FAPI; see nr_fill_sensing_pusch(). */
      ranges[n_ranges].start_symbol = sym;
      ranges[n_ranges].num_symbols = 1;
      ranges[n_ranges].rb_start = run_start;
      ranges[n_ranges].rb_size = run_len;
      n_ranges++;

      LOG_D(NR_MAC, "Sensing tile %d.%d: sym %d RB %d+%d (abs)\n", frame, slot, sym, run_start, run_len);
    }
  }

  return n_ranges;
}

/* Per-slot sensing driver: scan the UL for free tiles, then (Aerial) inject a
 * capture PUSCH and (E3) publish the ranges. Inert when sensing is disabled.
 * Defined after nr_scan_sensing_tiles so it can stay static; the Aerial
 * capture-PUSCH (nr_fill_sensing_pusch) lives in ran_func_spectrum_aerial.c. */
void nr_mac_sensing_scan_and_publish(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot)
{
  sensing_range_t sensing_ranges[MAX_SENSING_RANGES];
  int n_sensing = nr_scan_sensing_tiles(mac, cell, frame, slot, sensing_ranges, MAX_SENSING_RANGES);
  if (n_sensing > 0)
    LOG_D(NR_MAC, "%d.%d: identified %d sensing tile(s)\n", frame, slot, n_sensing);
#ifdef ENABLE_AERIAL
  nr_fill_sensing_pusch(cell, frame, slot, sensing_ranges, n_sensing);
#endif
  /* Publish every UL/MIXED slot (even n_sensing==0, to clear last frame's stale
   * ranges); DL slots have no consumer. */
  e3_spectrum_cell_t *rec = find_cell(cell);
  if (rec != NULL && is_ul_or_mixed_slot(slot, &cell->frame_structure))
    record_sensing_ranges(rec, /*beam=*/0, frame, slot, sensing_ranges, n_sensing);
}
