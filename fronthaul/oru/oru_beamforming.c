/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "oru_beamforming.h"
#include "openair1/PHY/TOOLS/tools_defs.h"
#include "common/utils/nr/nr_common.h"
#include "log.h"
#include <string.h>

static void combine_passthrough(c16_t **txDataF,
                                int nb_tx,
                                int n_sc,
                                const dl_iq_stream_t *streams,
                                int num_streams,
                                c16_t rotation)
{
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    memset(txDataF[aatx], 0, n_sc * sizeof(c16_t));
  }

  // Direct overwrite, assume no overalpping streams on the same tx antenna.
  for (int i = 0; i < num_streams; i++) {
    const dl_iq_stream_t *stream = &streams[i];
    if (stream->ant_id >= (unsigned)nb_tx) {
      continue;
    }
    c16_t *dst = &txDataF[stream->ant_id][stream->start_prb * NR_NB_SC_PER_RB];
    const c16_t *src = (const c16_t *)stream->iq;
    rotate_cpx_vector(src, rotation, dst, stream->num_prb * NR_NB_SC_PER_RB, 15);
  }
}

static void combine_codebook(c16_t **txDataF,
                             int nb_tx,
                             int n_sc,
                             const dl_iq_stream_t *streams,
                             int num_streams,
                             const oru_codebook_t *cb,
                             c16_t rotation)
{
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    memset(txDataF[aatx], 0, n_sc * sizeof(c16_t));
  }

  for (int i = 0; i < num_streams; i++) {
    const dl_iq_stream_t *stream = &streams[i];
    if (stream->ant_id >= cb->nb_fh_streams) {
      LOG_W(PHY, "DL stream ant_id %d exceeds nb_fh_streams %d, dropping\n", stream->ant_id, cb->nb_fh_streams);
      continue;
    }
    int bidx = stream->beam_id;
    if (bidx >= cb->nb_beams) {
      LOG_W(PHY, "beam_id %u out of range (nb_beams=%d), falling back to beam 0\n", stream->beam_id, cb->nb_beams);
      bidx = 0;
    }
    const c16_t *src = (const c16_t *)stream->iq;
    const int n_re = stream->num_prb * NR_NB_SC_PER_RB;
    const int re_off = stream->start_prb * NR_NB_SC_PER_RB;
    for (int txru = 0; txru < nb_tx; txru++) {
      // Fold rotation into the weight once, instead of a separate rotate pass afterward. Fused
      // multiply+saturating-accumulate straight into txDataF - no scratch buffer, one pass. The fold
      // saturates too: weights are arbitrary int16 from config, so w * rotation can exceed Q1.15.
      c16_t w_rot = c16mulShiftSat(cb->w[bidx][txru][stream->ant_id], rotation, 15);
      rotate_add_cpx_vector(src, w_rot, &txDataF[txru][re_off], n_re, 15);
    }
  }
}

void combine_dl_streams(c16_t **txDataF,
                        int nb_tx,
                        int n_sc,
                        const dl_iq_stream_t *streams,
                        int num_streams,
                        const oru_codebook_t *cb,
                        c16_t rotation)
{
  if (cb->nb_fh_streams <= 0) {
    combine_passthrough(txDataF, nb_tx, n_sc, streams, num_streams, rotation);
  } else {
    combine_codebook(txDataF, nb_tx, n_sc, streams, num_streams, cb, rotation);
  }
}

void combine_ul_beam_fd(const c16_t *const fft_data[],
                        int nb_rx,
                        int k0,
                        int nk,
                        const oru_codebook_t *cb,
                        int beam_id,
                        int stream_id,
                        c16_t *out)
{
  if (stream_id >= cb->nb_fh_streams) {
    LOG_W(PHY, "UL stream %d exceeds nb_fh_streams %d, dropping\n", stream_id, cb->nb_fh_streams);
    memset(&out[k0], 0, nk * sizeof(c16_t));
    return;
  }
  int bidx = beam_id;
  if (bidx >= cb->nb_beams) {
    LOG_W(PHY, "beam_id %u out of range (nb_beams=%d), falling back to beam 0\n", beam_id, cb->nb_beams);
    bidx = 0;
  }
  memset(&out[k0], 0, nk * sizeof(c16_t));
  if (nb_rx > ORU_CODEBOOK_MAX_NB_TX) {
    LOG_W(PHY, "nb_rx %d exceeds codebook antenna dimension %d, combining first %d antennas\n",
          nb_rx, ORU_CODEBOOK_MAX_NB_TX, ORU_CODEBOOK_MAX_NB_TX);
  }
  const int nb_ant = nb_rx < ORU_CODEBOOK_MAX_NB_TX ? nb_rx : ORU_CODEBOOK_MAX_NB_TX;
  for (int a = 0; a < nb_ant; a++) {
    // Same per-bin saturating multiply-accumulate as the DL codebook path, with the same weight:
    // see the header for why UL uses w, not conj(w).
    rotate_add_cpx_vector(&fft_data[a][k0], cb->w[bidx][a][stream_id], &out[k0], nk, 15);
  }
}

int oru_prb_fft_runs(int fft_size, int nbins, int start_prb, int num_prb, oru_fft_run_t runs[2])
{
  const int half = nbins / 2;
  int c0 = start_prb * NR_NB_SC_PER_RB;
  int c1 = c0 + num_prb * NR_NB_SC_PER_RB;
  c0 = c0 < nbins ? c0 : nbins;
  c1 = c1 < nbins ? c1 : nbins;
  int n = 0;
  if (c0 < half && c0 < c1) { // negative-frequency half: the tail of the FFT window
    const int end = c1 < half ? c1 : half;
    runs[n++] = (oru_fft_run_t){.bin = fft_size - half + c0, .re = c0, .len = end - c0};
  }
  if (c1 > half && c0 < c1) { // DC and positive-frequency half: the head of the FFT window
    const int lo = c0 > half ? c0 : half;
    runs[n++] = (oru_fft_run_t){.bin = lo - half, .re = lo, .len = c1 - lo};
  }
  return n;
}

void combine_ul_beam_prbs(const c16_t *const fft_data[],
                          int nb_rx,
                          int fft_size,
                          int nbins,
                          int start_prb,
                          int num_prb,
                          const oru_codebook_t *cb,
                          int beam_id,
                          int stream_id,
                          c16_t *out)
{
  oru_fft_run_t runs[2];
  const int n = oru_prb_fft_runs(fft_size, nbins, start_prb, num_prb, runs);
  for (int r = 0; r < n; r++)
    combine_ul_beam_fd(fft_data, nb_rx, runs[r].bin, runs[r].len, cb, beam_id, stream_id, out);
}
