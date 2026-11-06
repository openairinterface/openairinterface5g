/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common/utils/LOG/log.h"
#include "common/utils/assertions.h"
#include "SIMULATION/TOOLS/sim.h"

#include "cirdb_provider.h"
#include "cirdb_yaml.h"

#define NUM_TAPS_BUFFERS 4
#define MAX_L_PUBLISH 8

static inline double dabs(double x)
{
  return x < 0.0 ? -x : x;
}

typedef struct {
  void *taps_blob; /* contiguous complexf for all links */
  channel_desc_t *ch; /* ch_ps points into taps_blob */
} cirdb_buffer_t;

typedef struct cirdb_provider_s {
  /* I/O */
  FILE *fh;

  /* Selected entry meta */
  int model_id;
  int n_tx;
  int n_rx;
  int L_full;
  int S;
  double fs_hz;
  double snapshot_dt_s;
  float ds_ns;
  float speed_mps;
  uint64_t sel_offset;
  uint64_t sel_nbytes;

  /* Rx (UE) trajectory events copied from the selected entry's yaml metadata,
   * used to log the UE's angle per snapshot for model_id==5 (RT-Beam-forming). */
  double events[MAX_CIRDB_EVENTS][6];
  int num_events;

  /* Publication control */
  uint32_t L_out;
  uint32_t snap_idx;

  /* Shape in publication */
  int num_tx;
  int num_rx;

  /* Double buffering for readers */
  cirdb_buffer_t bufs[NUM_TAPS_BUFFERS];
  int cur;

  /* Temporary read buffer for one snapshot of full L_full taps */
  void *snapshot_tmp;
  size_t snapshot_tmp_bytes;

  /* Published pointer location owned by caller */
  channel_desc_t **channel_desc_out;

  /* Bookkeeping of last step computed from elapsed time */
  int64_t last_step_applied; /* -1 until first update */
} cirdb_g;

static inline void fread_exact(void *dst, size_t sz, FILE *fh)
{
  size_t r = fread(dst, 1, sz, fh);
  if (r != sz) {
    LOG_E(HW, "CIRDB short read: expected %zu got %zu errno=%d\n", sz, r, errno);
    abort();
  }
}

static inline size_t cf_bytes(size_t n)
{
  return n * sizeof(struct complexf);
}

/* Point channel_desc ch_ps into a compacted taps buffer */
static void point_channel_desc(channel_desc_t *ch, struct complexf *base, int num_tx, int num_rx, size_t L_out)
{
  for (int aarx = 0; aarx < num_rx; aarx++) {
    for (int aatx = 0; aatx < num_tx; aatx++) {
      int link = aarx + num_rx * aatx;
      ch->ch_ps[link] = &base[link * L_out];
    }
  }
  ch->channel_length = L_out;
  ch->path_loss_dB = 0;
  ch->nb_tx = num_tx;
  ch->nb_rx = num_rx;
}

/* Copy first L_out taps per link from a full L_full snapshot */
static void compact_L_out(struct complexf *dst,
                          const struct complexf *src_full,
                          int num_tx,
                          int num_rx,
                          size_t L_full,
                          size_t L_out)
{
  for (int aatx = 0; aatx < num_tx; aatx++) {
    for (int aarx = 0; aarx < num_rx; aarx++) {
      size_t link = aarx + num_rx * aatx;
      const struct complexf *src = &src_full[link * L_full];
      struct complexf *dst_link = &dst[link * L_out];
      memcpy(dst_link, src, cf_bytes(L_out));
    }
  }
}

/* Rx (UE) angle, in degrees offset from tx boresight, at CIR snapshot index k --
 * replays the same event-based linear interpolation generate_moving_rx_cir.py used
 * to build the trajectory (see build_waypoints() there). Returns NAN if this entry
 * carries no events (e.g. a plain TDL entry, or truncated by MAX_CIRDB_EVENTS). */
static double cirdb_angle_at_snapshot(const double events[][6], int num_events, double snapshot_dt_s, uint32_t k)
{
  if (num_events <= 0 || snapshot_dt_s <= 0.0)
    return NAN;

  uint32_t remaining = k;
  for (int i = 0; i < num_events; i++) {
    double rxstart_deg = events[i][0];
    double rxstop_deg = events[i][2];
    double duration_sec = events[i][5];
    uint32_t n = (uint32_t)llround(duration_sec / snapshot_dt_s);
    if (n == 0)
      continue;
    if (remaining < n) {
      double frac = (remaining * snapshot_dt_s) / duration_sec;
      if (frac < 0.0)
        frac = 0.0;
      if (frac > 1.0)
        frac = 1.0;
      return rxstart_deg + frac * (rxstop_deg - rxstart_deg);
    }
    remaining -= n;
  }
  /* k landed past the last computed waypoint (rounding at the final sample) --
   * clamp to the final event's stop angle. */
  return events[num_events - 1][2];
}

/* Load snapshot index s into the next publication buffer and flip */
static void load_snapshot_and_publish(cirdb_g *G, uint32_t s)
{
  uint64_t stride = (uint64_t)G->n_tx * G->n_rx * G->L_full * sizeof(struct complexf);
  uint64_t off = G->sel_offset + (uint64_t)s * stride;

  if (fseeko(G->fh, (off_t)off, SEEK_SET) != 0) {
    LOG_E(HW, "CIRDB fseoko failed errno=%d\n", errno);
    abort();
  }
  fread_exact(G->snapshot_tmp, (size_t)stride, G->fh);

  int next = (G->cur + 1) % NUM_TAPS_BUFFERS;
  cirdb_buffer_t *buf = &G->bufs[next];

  compact_L_out((struct complexf *)buf->taps_blob,
                (const struct complexf *)G->snapshot_tmp,
                G->n_tx,
                G->n_rx,
                G->L_full,
                G->L_out);

  point_channel_desc(buf->ch, (struct complexf *)buf->taps_blob, G->n_tx, G->n_rx, G->L_out);

  *G->channel_desc_out = buf->ch;
  G->cur = next;
}

cirdb_provider_t *cirdb_connect(int num_tx_antennas,
                                int num_rx_antennas,
                                const cirdb_select_opts_t *sel,
                                channel_desc_t **channel_desc_out)
{
  cirdb_g G;
  memset(&G, 0, sizeof(G));
  G.channel_desc_out = channel_desc_out;
  G.num_tx = num_tx_antennas;
  G.num_rx = num_rx_antennas;
  G.cur = 0;
  G.last_step_applied = -1;

  /* Require explicit paths in cirdb_select_opts_t */
  AssertFatal(sel != NULL, "cirdb_select_opts_t must not be NULL\n");
  AssertFatal(sel->bin_path && sel->bin_path[0], "CIRDB requires explicit bin_path in cirdb_select_opts_t\n");
  AssertFatal(sel->yaml_path && sel->yaml_path[0], "CIRDB requires explicit yaml_path in cirdb_select_opts_t\n");

  const char *use_bin = sel->bin_path;
  const char *sidecar = sel->yaml_path;

  LOG_I(HW, "CIRDB: using database %s with metadata %s\n", use_bin, sidecar);

  G.fh = fopen(use_bin, "rb");
  AssertFatal(G.fh != NULL, "Cannot open CIRDB database file '%s': %s\n", use_bin, strerror(errno));

  /* Selection request */
  int want_model_id = sel->want_model_id;
  AssertFatal(want_model_id >= 0 && want_model_id <= 5, "Invalid model_id=%d (valid: 0..5)\n", want_model_id);
  float want_ds = (sel && sel->want_ds_ns > 0) ? sel->want_ds_ns : -1.0f;
  float want_speed = (sel && sel->want_speed_mps > 0) ? sel->want_speed_mps : -1.0f;

  LOG_I(HW,
        "CIRDB: Searching for entry with model_id=%d, antennas=%dx%d, DS=%.1fns, speed=%.1fm/s, AoA=%.1fdeg\n",
        want_model_id,
        G.num_tx,
        G.num_rx,
        want_ds,
        want_speed,
        sel->want_aoa_deg);

  cirdb_entry_meta_t m = (cirdb_entry_meta_t){0};
  cirdb_select_req_t req = {.want_model_id = want_model_id,
                            .want_tx = G.num_tx,
                            .want_rx = G.num_rx,
                            .want_ds_ns = want_ds,
                            .want_speed_mps = want_speed,
                            .want_aoa_deg = sel->want_aoa_deg,
                            .allow_shape_swap = 0,
                            .w_ds = 1.0f,
                            .w_speed = 0.2f,
                            .yaml_path = sidecar};

  int ok = cirdb_yaml_select(&req, &m);

  // STRICT validation: fail if no match found
  AssertFatal(ok > 0,
              "No suitable CIR entry found in YAML %s\n"
              "  Requested: model_id=%d, antennas=%dx%d, DS=%.1fns, speed=%.1fm/s\n"
              "  Possible causes:\n"
              "    - No entry with model_id=%d in database\n"
              "    - No entry with antenna configuration %dx%d in database\n"
              "  Check the YAML file for available entries\n",
              sidecar,
              want_model_id,
              G.num_tx,
              G.num_rx,
              want_ds,
              want_speed,
              want_model_id,
              G.num_tx,
              G.num_rx);

  // STRICT validation: antenna dimensions must match exactly
  AssertFatal(m.n_tx == G.num_tx && m.n_rx == G.num_rx,
              "CIRDB antenna mismatch: requested %dx%d but selected entry has %dx%d\n"
              "  This should never happen - indicates a bug in selection logic\n",
              G.num_tx,
              G.num_rx,
              m.n_tx,
              m.n_rx);

  // STRICT validation: model_id must match if specified
  if (want_model_id >= 0) {
    AssertFatal(m.model_id == want_model_id,
                "CIRDB model_id mismatch: requested %d but selected entry has %d\n"
                "  This should never happen - indicates a bug in selection logic\n",
                want_model_id,
                m.model_id);
  }

  // FLEXIBLE: warn if delay spread doesn't match exactly (using closest available)
  if (want_ds > 0 && dabs(m.ds_ns - want_ds) > 0.1) {
    LOG_W(HW, "CIRDB: Requested delay spread %.1f ns not available in database, using closest match: %.1f ns\n", want_ds, m.ds_ns);
  }

  // FLEXIBLE: warn if speed doesn't match exactly (using closest available)
  if (want_speed > 0 && dabs(m.speed_mps - want_speed) > 0.1) {
    LOG_W(HW,
          "CIRDB: Requested speed %.1f m/s not available in database, using closest match: %.1f m/s\n",
          want_speed,
          m.speed_mps);
  }

  G.model_id = m.model_id;
  G.n_tx = m.n_tx;
  G.n_rx = m.n_rx;
  G.L_full = m.L;
  G.S = m.S;
  G.fs_hz = m.fs_hz;
  G.snapshot_dt_s = m.snapshot_dt_s;
  G.ds_ns = m.ds_ns;
  G.speed_mps = m.speed_mps;
  G.sel_offset = m.offset_bytes;
  G.sel_nbytes = m.nbytes;
  G.num_events = m.num_events;
  memcpy(G.events, m.events, sizeof(G.events));
  G.L_out = (m.L <= MAX_L_PUBLISH ? (uint32_t)m.L : MAX_L_PUBLISH);
  G.snap_idx = 0;

  G.snapshot_tmp_bytes = (size_t)G.n_tx * G.n_rx * G.L_full * sizeof(struct complexf);
  G.snapshot_tmp = calloc_or_fail(1, G.snapshot_tmp_bytes);

  for (int i = 0; i < NUM_TAPS_BUFFERS; i++) {
    size_t blob_cf = (size_t)G.n_tx * G.n_rx * G.L_out;
    G.bufs[i].taps_blob = calloc_or_fail(blob_cf, sizeof(struct complexf));
    G.bufs[i].ch = calloc_or_fail(1, sizeof(channel_desc_t));
    G.bufs[i].ch->ch_ps = calloc_or_fail(G.n_rx * G.n_tx, sizeof(struct complexf *));
    G.bufs[i].ch->nb_tx = G.n_tx;
    G.bufs[i].ch->nb_rx = G.n_rx;
  }

  if (G.S > 0) {
    load_snapshot_and_publish(&G, 0);
  }

  LOG_I(HW,
        "CIRDB: Selected entry - model=%d DS=%.3fns shape=%ux%u L=%u/%u S=%u fs=%.0f dt=%.6fs speed=%.3fm/s aoa=%.1fdeg\n",
        G.model_id,
        G.ds_ns,
        G.n_tx,
        G.n_rx,
        G.L_out,
        G.L_full,
        G.S,
        G.fs_hz,
        G.snapshot_dt_s,
        G.speed_mps,
        sel->want_aoa_deg);

  cirdb_g *p = calloc_or_fail(1, sizeof(cirdb_g));
  *p = G;
  return p;
}

bool cirdb_update(cirdb_provider_t *G, uint64_t ns_since_start)
{
  if (!G)
    return false;
  if (!G->channel_desc_out || !*G->channel_desc_out)
    return false;
  if (G->S <= 0)
    return false;

  double dt_s = (G->snapshot_dt_s > 0.0 ? G->snapshot_dt_s : 0.5);
  if (dt_s <= 0.0)
    dt_s = 0.5;

  double steps_f = (ns_since_start * 1e-9) / dt_s;
  int64_t step = (int64_t)(steps_f >= 0.0 ? steps_f : 0.0);

  if (step != G->last_step_applied) {
    uint32_t s = (uint32_t)(step % G->S);
    load_snapshot_and_publish(G, s);
    G->snap_idx = s;
    G->last_step_applied = step;

    if (G->model_id == 5) {
      double angle_deg = cirdb_angle_at_snapshot(G->events, G->num_events, G->snapshot_dt_s, s);
      if (!isnan(angle_deg))
        LOG_I(HW, "CIRDB: UE angle at snapshot %u/%d (t=%.3fs): %.2f deg from tx boresight\n", s, G->S, s * dt_s, angle_deg);
    }
    return true;
  }
  return false;
}

void cirdb_stop(cirdb_provider_t *G)
{
  if (!G)
    return;
  for (int i = 0; i < NUM_TAPS_BUFFERS; i++) {
    free(G->bufs[i].taps_blob);
    if (G->bufs[i].ch) {
      free(G->bufs[i].ch->ch_ps);
      free(G->bufs[i].ch);
    }
  }
  free(G->snapshot_tmp);
  if (G->fh)
    fclose(G->fh);
  free(G);
}
