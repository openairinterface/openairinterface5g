/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Routines for UE PRACH physical channel
 */
#include "PHY/sse_intrin.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"

#include "common/utils/LOG/log.h"

#include "T.h"

//#define NR_PRACH_DEBUG 1
#include "openair1/PHY/NR_TRANSPORT/nr_prach.h"

// Note:
// - prach_fmt_id is an ID used to map to the corresponding PRACH format value in prachfmt
// WIP todo:
// - take prach start symbol into account
// - idft for short sequence assumes we are transmitting starting in symbol 0 of a PRACH slot
// - Assumes that PRACH SCS is same as PUSCH SCS @ 30 kHz, take values for formats 0-2 and adjust for others below
// - Preamble index different from 0 is not detected by gNB
int32_t generate_nr_prach(PHY_VARS_NR_UE *ue, uint8_t gNB_id, int frame, uint8_t slot, int16_t tx_amp, c16_t **txData)
{
  NR_DL_FRAME_PARMS *fp=&ue->frame_parms;
  fapi_nr_config_request_t *nrUE_config = &ue->nrUE_config;
  fapi_nr_ul_config_prach_pdu *prach_pdu = &ue->prach_vars[gNB_id]->prach_pdu;

  const int fd_occasion = prach_pdu->num_ra;
  fapi_nr_num_prach_fd_occasions_t *occ = nrUE_config->prach_config.num_prach_fd_occasions_list + fd_occasion;
  const int prach_sequence_length = nrUE_config->prach_config.prach_sequence_length;
  const int N_ZC = prach_sequence_length ? 139 : 839;
  const int mu = nrUE_config->prach_config.prach_sub_c_spacing;
  const int restricted_set = prach_pdu->restricted_set;
  const int rootSequenceIndex = prach_pdu->root_seq_id;
  const int n_ra_prb = occ->k1; // prach_pdu->freq_msg1;
  const int NCS = prach_pdu->num_cs;
  const int prach_fmt_id = prach_pdu->prach_format;
  const int preamble_index = prach_pdu->ra_PreambleIndex;
  int k = 12 * n_ra_prb - 6 * fp->N_RB_UL;
  const int prachStartSymbol = prach_pdu->prach_start_symbol;

  LOG_D(PHY,"Generate NR PRACH %d.%d\n", frame, slot);

  if (nrUE_config->prach_config.root_seq_computed == 0) {
    compute_nr_prach_seq(nrUE_config->prach_config.prach_sequence_length,
                         occ->num_root_sequences,
                         occ->prach_root_sequence_index,
                         ue->X_u);
    nrUE_config->prach_config.root_seq_computed = 1;
  }

  int prach_start;
  if (prachStartSymbol == 0) {
    prach_start = 0;
  } else if (fp->slots_per_subframe == 1) {
    if (prachStartSymbol <= 7)
      prach_start =
          (fp->ofdm_symbol_size + fp->nb_prefix_samples) * (prachStartSymbol - 1) + (fp->ofdm_symbol_size + fp->nb_prefix_samples0);
    else
      prach_start = (fp->ofdm_symbol_size + fp->nb_prefix_samples) * (prachStartSymbol - 2)
                    + (fp->ofdm_symbol_size + fp->nb_prefix_samples0) * 2;
  } else {
    if (slot % (fp->slots_per_subframe / 2) == 0)
      prach_start =
          (fp->ofdm_symbol_size + fp->nb_prefix_samples) * (prachStartSymbol - 1) + (fp->ofdm_symbol_size + fp->nb_prefix_samples0);
    else
      prach_start = (fp->ofdm_symbol_size + fp->nb_prefix_samples) * prachStartSymbol;
  }

  // First compute physical root sequence
  /************************************************************************
  * 4G and NR NCS tables are slightly different and depend on prach format
  * Table 6.3.3.1-5:  for preamble formats with delta_f_RA = 1.25 Khz (formats 0,1,2)
  * Table 6.3.3.1-6:  for preamble formats with delta_f_RA = 5 Khz (formats 3)
  * NOTE: Restricted set type B is not implemented
  *************************************************************************/

  const uint16_t *prach_root_sequence_map =
      (prach_sequence_length == 0) ? prach_root_sequence_map_0_3 : prach_root_sequence_map_abc;
  int preamble_offset, preamble_shift = 0, first_nonzero_root_idx = 0;
  if (restricted_set == 0) {
    // This is the relative offset (for unrestricted case) in the root sequence table (5.7.2-4 from 36.211) for the given preamble index
    preamble_offset = NCS == 0 ? preamble_index : preamble_index / (N_ZC / NCS);
    // This is the \nu corresponding to the preamble index
    preamble_shift = NCS == 0 ? 0 : preamble_index % (N_ZC / NCS);
    preamble_shift *= NCS;
  } else { // This is the high-speed case

#ifdef NR_PRACH_DEBUG
    LOG_I(PHY, "PRACH [UE %d] High-speed mode, NCS %d\n", ue->Mod_id, NCS);
#endif

    uint16_t nr_du[NR_PRACH_SEQ_LEN_L - 1];
    nr_fill_du(N_ZC, prach_root_sequence_map, nr_du);
    int preamble_index0 = preamble_index;
    // set preamble_offset to initial rootSequenceIndex and look if we need more root sequences for this
    // preamble index and find the corresponding cyclic shift
    preamble_offset = 0; // relative rootSequenceIndex;

    bool not_found = true;
    while (not_found) {
      // current root depending on rootSequenceIndex and preamble_offset
      int index = (rootSequenceIndex + preamble_offset) % N_ZC;
      uint16_t n_group_ra = 0;

      if (prach_fmt_id<4) {
        // prach_root_sequence_map points to prach_root_sequence_map0_3
        DevAssert( index < sizeof(prach_root_sequence_map_0_3) / sizeof(prach_root_sequence_map_0_3[0]) );
      } else {
        // prach_root_sequence_map points to prach_root_sequence_map4
        DevAssert( index < sizeof(prach_root_sequence_map_abc) / sizeof(prach_root_sequence_map_abc[0]) );
      }

      int u = prach_root_sequence_map[index];
      int n_shift_ra, d_start = INT16_MAX, n_shift_ra_bar;
      if ( (nr_du[u]<(N_ZC/3)) && (nr_du[u]>=NCS) ) {
        n_shift_ra     = nr_du[u]/NCS;
        d_start        = (nr_du[u]<<1) + (n_shift_ra * NCS);
        n_group_ra     = N_ZC/d_start;
        n_shift_ra_bar = max(0, (N_ZC - nr_du[u] * 2 - n_group_ra * d_start) / N_ZC);
      } else if (nr_du[u] >= N_ZC / 3 && nr_du[u] <= (N_ZC - NCS) / 2) {
        n_shift_ra = (N_ZC - nr_du[u] * 2) / NCS;
        d_start = N_ZC - nr_du[u] * 2 + n_shift_ra * NCS;
        n_group_ra     = nr_du[u]/d_start;
        n_shift_ra_bar = min(n_shift_ra, max(0, (nr_du[u] - n_group_ra * d_start) / NCS));
      } else {
        n_shift_ra     = 0;
        n_shift_ra_bar = 0;
      }

      // This is the number of cyclic shifts for the current root u
      int numshift = (n_shift_ra * n_group_ra) + n_shift_ra_bar;
      if (numshift>0 && preamble_index0==preamble_index)
        first_nonzero_root_idx = preamble_offset;

      if (preamble_index0 < numshift) {
        not_found = false;
        preamble_shift = (d_start * (preamble_index0/n_shift_ra)) + ((preamble_index0%n_shift_ra)*NCS);

      } else { // skip to next rootSequenceIndex and recompute parameters
        preamble_offset++;
        preamble_index0 -= numshift;
      }
    }
  }

  // now generate PRACH signal
#ifdef NR_PRACH_DEBUG
  if (NCS > 0)
    LOG_I(PHY,
          "PRACH [UE %d] generate PRACH in frame.slot %d.%d for RootSeqIndex %d, Preamble Index %d, PRACH Format %s, NCS %d "
          "(N_ZC %d): Preamble_offset %d, Preamble_shift %d msg1 frequency start %d\n",
          ue->Mod_id,
          frame,
          slot,
          rootSequenceIndex,
          preamble_index,
          prachfmt[prach_fmt_id],
          NCS,
          N_ZC,
          preamble_offset,
          preamble_shift,
          n_ra_prb);
#endif

  const unsigned int K = get_prach_K(prach_sequence_length, prach_fmt_id, fp->numerology_index, mu);
  const uint8_t kbar = get_PRACH_k_bar(mu, fp->numerology_index);

  if (k < 0)
    k += fp->ofdm_symbol_size;

  k *= K;
  k += kbar;

  LOG_I(PHY,
        "PRACH [UE %d] in frame.slot %d.%d, placing PRACH in position %d, Msg1/MsgA-Preamble frequency start %d (k1 %d), "
        "preamble_offset %d, first_nonzero_root_idx %d, preambleIndex = %d\n",
        ue->Mod_id,
        frame,
        slot,
        k * 2,
        n_ra_prb,
        occ->k1,
        preamble_offset,
        first_nonzero_root_idx,
        prach_pdu->ra_PreambleIndex);

  // Ncp and dftlen here is given in terms of T_s wich is 30.72MHz sampling
  int dftlen, Ncp;
  if (prach_sequence_length == 0) {
    AssertFatal(prach_fmt_id >= 0 && prach_fmt_id <= 3, "Unknown PRACH format ID %d for sequence length 839\n", prach_fmt_id);
    const int ncp[4] = {3168, 21024, 4688, 3168};
    const int dft[4] = {24576, 24576, 24576, 6144};
     Ncp = ncp[prach_fmt_id];
    dftlen = dft[prach_fmt_id];
  } else {
    AssertFatal(prach_fmt_id >= 4 && prach_fmt_id <= 10, "Unknown PRACH format ID %d\n", prach_fmt_id);
    const int ncp[7] = {288, 576, 864, 216, 936, 1240, 2048};
    Ncp = ncp[prach_fmt_id - 4] >> mu;
    dftlen = 2048 >> mu;
  }

  // actually what we should be checking here is how often the current prach crosses a 0.5ms boundary. I am not quite sure for
  // which paramter set this would be the case, so I will ignore it for now and just check if the prach starts on a 0.5ms boundary
  if (fp->numerology_index == 0) {
    if (prachStartSymbol == 0 || prachStartSymbol == 7)
      Ncp += 16;
  } else {
    if (slot % (fp->slots_per_subframe / 2) == 0 && prachStartSymbol == 0)
      Ncp += 16;
  }

  switch (fp->samples_per_subframe) {
    case 7680:
      // 5 MHz @ 7.68 Ms/s
      Ncp >>= 2;
      dftlen >>= 2;
      break;

    case 15360:
      // 10, 15 MHz @ 15.36 Ms/s
      Ncp >>= 1;
      dftlen >>= 1;
      break;

    case 23040:
      // 20 MHz @ 23.04 Ms/s
      Ncp = (Ncp * 3) / 4;
      dftlen = (dftlen * 3) / 4;
      break;

    case 30720:
      // 20, 25, 30 MHz @ 30.72 Ms/s
      break;

    case 46080:
      // 40 MHz @ 46.08 Ms/s
      Ncp = (Ncp * 3) / 2;
      dftlen = (dftlen * 3) / 2;
      break;

    case 61440:
      // 40, 50, 60 MHz @ 61.44 Ms/s
      Ncp <<= 1;
      dftlen <<= 1;
      break;

    case 92160:
      // 50, 60, 70, 80, 90 MHz @ 92.16 Ms/s
      Ncp *= 3;
      dftlen *= 3;
      break;

    case 122880:
      // 70, 80, 90, 100 MHz @ 122.88 Ms/s
      Ncp <<= 2;
      dftlen <<= 2;
      break;

    case 184320:
      // 100 MHz @ 184.32 Ms/s
      Ncp = Ncp * 6;
      dftlen = dftlen * 6;
      break;

    case 245760:
      // 200 MHz @ 245.76 Ms/s
      Ncp <<= 3;
      dftlen <<= 3;
      break;

    default:
      AssertFatal(1 == 0, "sample rate %f MHz not supported for numerology %d\n", fp->samples_per_subframe / 1000.0, mu);
  }

#ifdef NR_PRACH_DEBUG
  LOG_I(PHY, "PRACH [UE %d] Ncp %d, dftlen %d \n", ue->Mod_id, Ncp, dftlen);
#endif

  /********************************************************
   *
   * In function init_prach_tables:
   * to compute quantized roots of unity ru(n) = INT16_MAX * exp j*[ (2 * PI * n) / N_ZC ]
   *
   * In compute_prach_seq:
   * to calculate Xu = DFT xu = xu (inv_u*k) * Xu[0] (This is a Zadoff-Chou sequence property: DFT ZC sequence is another ZC
   * sequence)
   *
   * In generate_prach:
   * to do the cyclic-shifted DFT by multiplying Xu[k] * ru[k*preamble_shift] as:
   * If X[k] = DFT x(n) -> X_shifted[k] = DFT x(n+preamble_shift) = X[k] * exp -j*[ (2*PI*k*preamble_shift) / N_ZC ]
   *
   *********************************************************/

  const c16_t *Xu = ue->X_u[preamble_offset - first_nonzero_root_idx];

#if defined (PRACH_WRITE_OUTPUT_DEBUG)
  LOG_M("X_u.m", "X_u", (int16_t *)ue->X_u[preamble_offset - first_nonzero_root_idx], N_ZC, 1, 1);
#endif

  c16_t prach[dftlen] __attribute__((aligned(32)));
  {
    c16_t prachF[dftlen] __attribute__((aligned(32)));
    memset(prachF, 0, sizeof(prachF));
    for (int offset = 0, offset2 = 0; offset < N_ZC; offset++, offset2 += preamble_shift) {
      if (offset2 >= N_ZC)
        offset2 -= N_ZC;
      const c16_t Xu_t = c16xmulConstShift(Xu[offset], tx_amp, 15);
      const double w = 2 * M_PI * (double)offset2 / N_ZC;
      const c16_t ru = {.r = (int16_t)(floor(INT16_MAX * cos(w))), .i = (int16_t)(floor(INT16_MAX * sin(w)))};
      const c16_t p = c16mulShift(Xu_t, ru, 15);
      prachF[k++] = p;
      if (k == dftlen)
        k = 0;
    }

#if defined(PRACH_WRITE_OUTPUT_DEBUG)
    LOG_M("prachF.m", "prachF", &prachF[1804], 1024, 1, 1);
    LOG_M("Xu.m", "Xu", Xu, N_ZC, 1, 1);
#endif

    // This is after cyclic prefix
    const idft_size_idx_t idft_size = get_idft(dftlen);
    idft(idft_size, (int16_t *)prachF, (int16_t *)prach, 1);
  }

  c16_t *out = txData[0] + prach_start;
  memcpy(out, prach + dftlen - Ncp, Ncp * sizeof(*prach));
  out += Ncp;
  const int copies[11] = {1, 2, 4, 4, 2, 4, 6, 2, 12, 1, 4};
  DevAssert(prach_fmt_id < sizeofArray(copies));
  for (int i = 0; i < copies[prach_fmt_id]; i++) {
    memcpy(out, prach, dftlen * sizeof(*prach));
    out += dftlen;
  }

#ifdef NR_PRACH_DEBUG
  LOG_I(PHY,
        "PRACH [UE %d] N_RB_UL %d prach_start %d, prach_len %d\n",
        ue->Mod_id,
        fp->N_RB_UL,
        prach_start,
        out - (txData[0] + prach_start));
#endif

  return signal_energy((int *)prach, 256);
}
