/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level routines for generating and decoding the PRACH physical channel V15.4 2018-12
 */

#include "PHY/defs_gNB.h"
#include "SCHED_NR/sched_nr.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "openair1/PHY/NR_TRANSPORT/nr_prach.h"

typedef struct {
  int reps;
  int Ncp;
  int dftlen;
  int N_ZC;
  int k;
  dft_size_idx_t dftsize;
  int sample_offset_slot;
} prach_ru_params_t;

static prach_ru_params_t get_prach_ru_params(prach_item_t *p,
                                             int prachStartSymbol,
                                             NR_DL_FRAME_PARMS *fp)
{
  prach_ru_params_t par = {0};
  const int sum = fp->ofdm_symbol_size + fp->nb_prefix_samples;
  const int sum0 = fp->ofdm_symbol_size + fp->nb_prefix_samples0;
  if (prachStartSymbol == 0) {
    par.sample_offset_slot = 0;
  } else if (fp->slots_per_subframe == 1) {
    if (prachStartSymbol <= 7)
      par.sample_offset_slot = sum * (prachStartSymbol - 1) + sum0;
    else
      par.sample_offset_slot = sum * (prachStartSymbol - 2) + sum0 * 2;
  } else {
    if (!(p->slot % (fp->slots_per_subframe / 2)))
      par.sample_offset_slot = sum * (prachStartSymbol - 1) + sum0;
    else
      par.sample_offset_slot = sum * prachStartSymbol;
  }

  int mu = p->numerology_index;

  if (p->prach_sequence_length == 0) {
    switch (p->pdu.prach_format) {
      case 0:
        par.reps = 1;
        par.Ncp = 3168;
        par.dftlen = 24576;
        break;

      case 1:
        par.reps = 2;
        par.Ncp = 21024;
        par.dftlen = 24576;
        break;

      case 2:
        par.reps = 4;
        par.Ncp = 4688;
        par.dftlen = 24576;
        break;

      case 3:
        par.reps = 4;
        par.Ncp = 3168;
        par.dftlen = 6144;
        break;

      default:
        AssertFatal(1 == 0, "Illegal prach format %d for length 839\n", p->pdu.prach_format);
        break;
    }
  } else {
    switch (p->pdu.prach_format) {
      case 4: // A1
        par.reps = 2;
        par.Ncp = 288 >> mu;
        break;

      case 5: // A2
        par.reps = 4;
        par.Ncp = 576 >> mu;
        break;

      case 6: // A3
        par.reps = 6;
        par.Ncp = 864 >> mu;
        break;

      case 7: // B1
        par.reps = 2;
        par.Ncp = 216 >> mu;
        break;

      case 8: // B4
        par.reps = 12;
        par.Ncp = 936 >> mu;
        break;

      case 9: // C0
        par.reps = 1;
        par.Ncp = 1240 >> mu;
        break;

      case 10: // C2
        par.reps = 4;
        par.Ncp = 2048 >> mu;
        break;

      default:
        AssertFatal(1 == 0, "unknown prach format %x\n", p->pdu.prach_format);
        break;
    }
    par.dftlen = 2048 >> mu;
  }

  if (p->numerology_index == 0) {
    if (prachStartSymbol == 0 || prachStartSymbol == 7)
      par.Ncp += 16;
  } else {
    if (p->slot % (fp->slots_per_subframe / 2) == 0 && prachStartSymbol == 0)
      par.Ncp += 16;
  }

  switch(fp->samples_per_subframe) {
  case 7680:
    // 5 MHz @ 7.68 Ms/s
    par.Ncp >>= 2;
    par.dftlen >>= 2;
    break;

  case 15360:
    // 10, 15 MHz @ 15.36 Ms/s
    par.Ncp >>= 1;
    par.dftlen >>= 1;
    break;

  case 23040:
    // 20 MHz @ 23.04 Ms/s
    par.Ncp = (par.Ncp * 3) / 4;
    par.dftlen = (par.dftlen * 3) / 4;
    break;

  case 30720:
    // 20, 25, 30 MHz @ 30.72 Ms/s
    break;

  case 46080:
    // 40 MHz @ 46.08 Ms/s
    par.Ncp = (par.Ncp*3)/2;
    par.dftlen = (par.dftlen*3)/2;
    break;

  case 61440:
    // 40, 50, 60 MHz @ 61.44 Ms/s
    par.Ncp <<= 1;
    par.dftlen <<= 1;
    break;

  case 92160:
    // 50, 60, 70, 80, 90 MHz @ 92.16 Ms/s
    par.Ncp *= 3;
    par.dftlen *= 3;
    break;

  case 122880:
    // 70, 80, 90, 100 MHz @ 122.88 Ms/s
    par.Ncp <<= 2;
    par.dftlen <<= 2;
    break;

  case 184320:
    // 100 MHz @ 184.32 Ms/s
    par.Ncp = par.Ncp*6;
    par.dftlen = par.dftlen*6;
    break;

  case 245760:
    // 200 MHz @ 245.76 Ms/s
    par.Ncp <<= 3;
    par.dftlen <<= 3;
    break;

  default:
    AssertFatal(1==0,"sample rate %f MHz not supported for numerology %d\n", fp->samples_per_subframe / 1000.0, mu);
  }

  par.dftsize = get_dft(par.dftlen);
  par.N_ZC = (p->prach_sequence_length == 0) ? 839 : 139;

  const unsigned int K = get_prach_K(p->prach_sequence_length, p->pdu.prach_format, p->numerology_index, p->mu);
  const uint8_t kbar = get_PRACH_k_bar(p->mu, p->numerology_index);

  int n_ra_prb = p->msg1_frequencystart;
  int k                   = (12*n_ra_prb) - 6*fp->N_RB_UL;

  if (k<0) k+=(fp->ofdm_symbol_size);
  
  k*=K;
  k+=kbar;
  par.k = k;

  return par;
}

void init_nr_prach(PHY_VARS_gNB *gNB)
{
  int num_prach = 16;
  bool ret;
  ret = spsc_q_alloc(&gNB->prach_ru_queue, num_prach, sizeof(prach_item_t));
  DevAssert(ret);
  ret = spsc_q_alloc(&gNB->prach_l1rx_queue, num_prach, sizeof(prach_item_t));
  DevAssert(ret);
}

void reset_nr_prach(PHY_VARS_gNB *gNB)
{
  spsc_q_free(&gNB->prach_ru_queue);
  spsc_q_free(&gNB->prach_l1rx_queue);
}

void free_nr_prach_entry(prach_item_t *p)
{
  free(p->prach_buf);
}

static bool drop_old_prach(const void *data, void *user)
{
  const prach_item_t *p = data;
  const fsn_t *now = user;
  // account for long PRACH over more than 1 slot
  const fsn_t t = {p->frame, p->slot + p->num_slots - 1, now->mu};
  bool drop = fsn_in_the_past(t, *now);
  if (drop)
    LOG_E(NR_PHY, "%4d.%2d PRACH job is in the past (%4d.%2d)\n", now->f, now->s, t.f, t.s);
  return drop;
}

static bool get_current_prach(const void *data, void *user)
{
  const prach_item_t *p = data;
  const fsn_t *now = user;
  // account for long PRACH over more than 1 slot
  const fsn_t t = {p->frame, p->slot + p->num_slots - 1, now->mu};
  return fsn_equal(t, *now);
}

bool get_next_nr_prach(spsc_q_t *q, const fsn_t *now, prach_item_t *p)
{
  spsc_q_drop_while(q, drop_old_prach, (void *)now);
  return spsc_q_get_if(q, get_current_prach, (void *)now, p, sizeof(*p));
}

void nr_schedule_rx_prach(PHY_VARS_gNB *gNB, int SFN, int Slot, nfapi_nr_prach_pdu_t *prach_pdu)
{
  const int fmt = prach_pdu->prach_format;
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  const nfapi_nr_prach_config_t *cfg = &gNB->gNB_config.prach_config;
  const nfapi_nr_num_prach_fd_occasions_t *occ = &cfg->num_prach_fd_occasions_list[prach_pdu->num_ra];
  const int num_rx_per_beam = gNB->frame_parms.nb_antennas_rx / gNB->common_vars.num_beams_period;
  prach_item_t prach = {
      .frame = SFN,
      .slot = Slot,
      .num_slots = fmt < 4 ? get_long_prach_dur(fmt, fp->numerology_index) : 1,
      .pdu = *prach_pdu,
      .rootSequenceIndex = occ->prach_root_sequence_index.value,
      .numrootSequenceIndex = occ->num_root_sequences.value,
      .msg1_frequencystart = occ->k1.value,
      .mu = cfg->prach_sub_c_spacing.value,
      .prach_sequence_length = cfg->prach_sequence_length.value,
      .restricted_set = cfg->restricted_set_config.value,
      .numerology_index = fp->numerology_index,
      .nb_rx = num_rx_per_beam,
      .Xu = gNB->X_u,
      .rx_prach = &gNB->rx_prach,
      // TODO can be made permanently allocated?
      .prach_buf = calloc_or_fail(1, sizeof(c16_t) * prach.nb_rx * NUMBER_OF_NR_RU_PRACH_OCCASIONS_MAX * NR_PRACH_SEQ_LEN_L),
  };
  const int num_beams = prach_pdu->beamforming.dig_bf_interface;
  int n_symb = get_nr_prach_duration(prach_pdu->prach_format);
  AssertFatal(num_beams < NFAPI_MAX_NUM_BG_IF, "impossible beams size %d\n", num_beams);
  for (int i = 0; i < num_beams; i++) {
    int fapi_beam_idx = prach_pdu->beamforming.prgs_list[0].dig_bf_interface_list[i].beam_idx;
    int start_symb = prach_pdu->prach_start_symbol + i * n_symb;
    int bitmap = SL_to_bitmap(start_symb, n_symb);
    if (gNB->common_vars.beam_id) {
      // TODO: Remove assumption of contiguous ports after DAS is properly handled in beamforming
      uint16_t ant_start = get_first_ant_idx(gNB->enable_analog_das,
                                             num_rx_per_beam,
                                             fapi_beam_idx,
                                             prach_pdu->param_v4.spatialStreamIndices[i * num_rx_per_beam]);
      beam_index_allocation(fapi_beam_idx,
                            ant_start,
                            num_rx_per_beam,
                            NR_SYMBOLS_PER_SLOT,
                            Slot,
                            bitmap,
                            gNB->frame_parms.nb_antennas_rx,
                            gNB->common_vars.beam_id);
      prach.ant_start = ant_start;
    }
  }
  bool found = spsc_q_put(&gNB->prach_ru_queue, &prach, sizeof(prach));
  if (!found)
    LOG_W(NR_PHY, "%4d.%2d PRACH occ queue is full: dropping PRACH request\n", SFN, Slot);
}

static void rx_nr_prach_ru_internal_rep(prach_item_t *p,
                                        int ant_offset,
                                        int32_t **rxdata,
                                        NR_DL_FRAME_PARMS *fp,
                                        int N_TA_offset,
                                        int rep,
                                        const prach_ru_params_t *params,
                                        c16_t (*rxsigF)[NR_PRACH_SEQ_LEN_L])
{
  AssertFatal(rep >= 0 && rep < params->reps, "rep %d is out of range (reps = %d)\n", rep, params->reps);

  int slot2 = p->prach_sequence_length ? p->slot : p->slot;
  int sample_offset = get_samples_slot_timestamp(fp, slot2) + params->sample_offset_slot - N_TA_offset + params->Ncp + rep * params->dftlen;

  for (int aa = 0; aa < p->nb_rx; aa++) {
    int idx = ant_offset + aa;
    c16_t *prach2 = (c16_t *)&rxdata[idx][sample_offset];

    // do DFT for the specific repetition
    c16_t tmp[params->dftlen] __attribute__((aligned(32)));
    dft(params->dftsize, (int16_t *)prach2, (int16_t *)tmp, 1);
    // Coherent combining of PRACH repetitions (assumes channel does not change, to be revisted for "long" PRACH)
    LOG_D(PHY, "Doing PRACH combining of repetition %d/%d N_ZC %d\n", rep, params->reps, params->N_ZC);
    int k2 = params->k;
    for (int j = 0; j < params->N_ZC; j++, k2++) {
      if (k2 == params->dftlen)
        k2 = 0;
      rxsigF[aa][j] = c16add(rxsigF[aa][j], tmp[k2]);
    }
  }
}

static void rx_nr_prach_ru_internal(prach_item_t *p,
                                    int prachStartSymbol,
                                    int prachOccasion,
                                    int32_t **rxdata,
                                    NR_DL_FRAME_PARMS *fp,
                                    int N_TA_offset,
                                    bool das)
{
  prach_ru_params_t params = get_prach_ru_params(p, prachStartSymbol, fp);
  c16_t rxsigF_tmp[p->nb_rx][NR_PRACH_SEQ_LEN_L];
  memset(rxsigF_tmp, 0, sizeof(rxsigF_tmp));

  const uint8_t num_beams = p->pdu.beamforming.dig_bf_interface;
  // When more than one beams, then each occasion is on one beam
  int ant_offset = 0;
  if (num_beams > 1) {
    AssertFatal(prachOccasion < num_beams, "Num of PRACH Occasions must be same as number of beams in beamforming mode\n");
    ant_offset = prachOccasion * p->nb_rx;
  }

  // TODO: Remove assumption of contiguous ports after DAS is properly handled in beamforming
  uint16_t ant_start =
      get_first_ant_idx(das,
                        p->nb_rx,
                        p->pdu.beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx,
                        p->pdu.param_v4.numSpatialStreamIndices > 0 ? p->pdu.param_v4.spatialStreamIndices[ant_offset] : 0);

  for (int rep = 0; rep < params.reps; rep++) {
    rx_nr_prach_ru_internal_rep(p, ant_start, rxdata, fp, N_TA_offset, rep, &params, rxsigF_tmp);
  }

  for (int aa = 0; aa < p->nb_rx; aa++) {
    memcpy(p->prach_buf[aa][prachOccasion], rxsigF_tmp[aa], sizeof(c16_t) * params.N_ZC);
  }
}

void rx_nr_prach_ru(prach_item_t *p, int32_t **rxdata, NR_DL_FRAME_PARMS *fp, int N_TA_offset, bool das)
{
  int N_dur = get_nr_prach_duration(p->pdu.prach_format);
  LOG_D(NR_PHY_RACH, "%d.%d try to decode %d occasions \n", p->frame, p->slot, p->pdu.num_prach_ocas);
  for (int prach_oc = 0; prach_oc < p->pdu.num_prach_ocas; prach_oc++) {
    int prachStartSymbol = p->pdu.prach_start_symbol + prach_oc * N_dur;
    // comment FK: the standard 38.211 section 5.3.2 has one extra term +14*N_RA_slot. This is because there prachStartSymbol is
    // given wrt to start of the 15kHz slot or 60kHz slot. Here we work slot based, so this function is anyway only called in slots
    // where there is PRACH. Its up to the MAC to schedule another PRACH PDU in the case there are there N_RA_slot \in {0,1}.
    rx_nr_prach_ru_internal(p, prachStartSymbol, prach_oc, rxdata, fp, N_TA_offset, das);
  }
}

void rx_nr_prach_ru_rep(prach_item_t *p,
                        int32_t **rxdata,
                        NR_DL_FRAME_PARMS *fp,
                        int N_TA_offset,
                        int rep,
                        int prachOccasion,
                        c16_t (*rxsigF)[NR_PRACH_SEQ_LEN_L])
{
  int N_dur = get_nr_prach_duration(p->pdu.prach_format);
  int prachStartSymbol = p->pdu.prach_start_symbol + prachOccasion * N_dur;
  prach_ru_params_t params = get_prach_ru_params(p, prachStartSymbol, fp);
  rx_nr_prach_ru_internal_rep(p, 0, rxdata, fp, N_TA_offset, rep, &params, rxsigF);
}

rx_prach_out_t rx_nr_prach(const prach_item_t *in, int occasion)
{
  const int nb_rx = in->nb_rx;
  const int NCS = in->pdu.num_cs;
  const int prach_fmt = in->pdu.prach_format;
  const int N_ZC = in->prach_sequence_length == 0 ? 839 : 139;

  LOG_D(NR_PHY_RACH,
        "L1 PRACH RX: rooSequenceIndex %d, numRootSeqeuences %d, NCS %d, N_ZC %d, format %d \n",
        in->rootSequenceIndex,
        in->numrootSequenceIndex,
        NCS,
        N_ZC,
        prach_fmt);

  if (LOG_DEBUGFLAG(DEBUG_PRACH)) {
    if ((in->frame & 1023) < 20)
      LOG_D(PHY, "PRACH (gNB) : running rx_prach for slot %d, rootSequenceIndex %d\n", in->slot, in->rootSequenceIndex);
  }

  start_meas(in->rx_prach);

  const uint16_t *prach_root_sequence_map =
      in->prach_sequence_length == 0 ? prach_root_sequence_map_0_3 : prach_root_sequence_map_abc;

  // PDP is oversampled, e.g. 1024 sample instead of 839
  // Adapt the NCS (zero-correlation zones) with oversampling factor e.g. 1024/839
  int NCS2 = N_ZC == 839 ? (NCS << 10) / 839 : (NCS << 8) / 139;

  if (NCS2 == 0)
    NCS2 = N_ZC;

  int old_offset = INT_MIN;
  int preamble_offset = 0, preamble_offset_old = 99, preamble_shift = 0, preamble_index0 = 0, numshift = 0,
      first_nonzero_root_idx = 0;
  const int dft_sz = N_ZC == 839 ? 1024 : 256;
  uint64_t prach_ifft[dft_sz] __attribute__((aligned(32)));

  // we look only for the max RACH, discard any other one in same slot
  struct res {
    uint64_t energy;
    uint distance;
    uint preamble;
  } max = {0, UINT_MAX, 0};

  for (int preamble_index = 0; preamble_index < 64; preamble_index++) {
    if (LOG_DEBUGFLAG(DEBUG_PRACH)) {
      int en = dB_fixed(signal_energy((int32_t *)in->prach_buf[0][occasion], N_ZC == 839 ? 840 : 140));
      if (en > 60)
        LOG_D(PHY, "frame %d, slot %d : Trying preamble %d \n", in->frame, in->slot, preamble_index);
    }
    if (in->restricted_set == 0) {
      // This is the relative offset in the root sequence table (5.7.2-4 from 36.211) for the given preamble index
      preamble_offset = NCS == 0 ? preamble_index : preamble_index / (N_ZC / NCS);
      if (preamble_offset != preamble_offset_old) {
        preamble_offset_old = preamble_offset;
        // This is the \nu corresponding to the preamble index
        preamble_shift  = 0;
      } else {
        preamble_shift -= NCS;
        if (preamble_shift < 0)
          preamble_shift += N_ZC;
      }
    } else { // This is the high-speed case
      uint16_t nr_du[NR_PRACH_SEQ_LEN_L - 1];
      nr_fill_du(N_ZC, prach_root_sequence_map, nr_du);
      // set preamble_offset to initial rootSequenceIndex and look if we need more root sequences for this
      // preamble index and find the corresponding cyclic shift
      // Check if all shifts for that root have been processed
      int n_shift_ra = 0, n_shift_ra_bar, d_start = 0;
      if (preamble_index0 == numshift) {
        preamble_index0 -= numshift;
        bool found = false;
        while (!found) {
          // current root depending on rootSequenceIndex
          int index = (in->rootSequenceIndex + preamble_offset) % N_ZC;
          int u = nr_du[prach_root_sequence_map[index]];
          int n_group_ra = 0;

          if (u < (N_ZC / 3) && u >= NCS) {
            n_shift_ra = u / NCS;
            d_start = u * 2 + n_shift_ra * NCS;
            n_group_ra = N_ZC / d_start;
            n_shift_ra_bar = max(0, (N_ZC - u * 2 - n_group_ra * d_start) / N_ZC);
          } else if (u >= (N_ZC / 3) && u <= ((N_ZC - NCS) >> 1)) {
            n_shift_ra = (N_ZC - u * 2) / NCS;
            d_start = N_ZC - u * 2 + n_shift_ra * NCS;
            n_group_ra = u / d_start;
            n_shift_ra_bar = min(n_shift_ra, max(0, (u - n_group_ra * d_start) / NCS));
          } else {
            n_shift_ra = 0;
            n_shift_ra_bar = 0;
          }

          // This is the number of cyclic shifts for the current root u
          numshift = n_shift_ra * n_group_ra + n_shift_ra_bar;
          // skip to next root and recompute parameters if numshift==0
          if (numshift > 0)
            found = true;
          else
            preamble_offset++;
        }
      }

      if (n_shift_ra>0)
        preamble_shift = -(d_start * (preamble_index0 / n_shift_ra)
                           + (preamble_index0 % n_shift_ra) * NCS); // minus because the channel is h(t -\tau + Cv)
      else
        preamble_shift = 0;
      if (preamble_shift < 0)
        preamble_shift+=N_ZC;

      preamble_index0++;
      if (preamble_index == 0)
        first_nonzero_root_idx = preamble_offset;
    }

    // Compute DFT of RX signal (conjugate in->rxsigF[occasion], results in conjugate output) for each new rootSequenceIndex
    if (preamble_offset - first_nonzero_root_idx != old_offset) {
      old_offset = preamble_offset - first_nonzero_root_idx;
      c16_t *Xu = in->Xu[preamble_offset - first_nonzero_root_idx];
      LOG_D(PHY, "PRACH RX new dft preamble_offset-first_nonzero_root_idx %d\n", preamble_offset - first_nonzero_root_idx);
      memset(prach_ifft, 0, sizeof(prach_ifft));
      c16_t prachF[dft_sz] __attribute__((aligned(32)));
      // set 0 in unused parts of the frequency domain
      memset(prachF + N_ZC, 0, sizeof(*prachF) * (dft_sz - N_ZC));
      for (int aa = 0; aa < nb_rx; aa++) {
        // Do componentwise product with Xu* on each antenna
        for (int offset = 0; offset < N_ZC; offset++) {
          prachF[offset] = c16MulConjShift(Xu[offset], in->prach_buf[aa][occasion][offset], 15);
        }
        // Now do IFFT of size 1024 (N_ZC=839) or 256 (N_ZC=139)
        c16_t prach_ifft_tmp[dft_sz] __attribute__((aligned(32)));
        idft(get_idft(dft_sz), (int16_t *)prachF, (int16_t *)prach_ifft_tmp, 1);
        // compute energy and accumulate over receive antennas
        for (int i = 0; i < dft_sz; i++) {
          c64_t tmp = (c64_t){prach_ifft_tmp[i].r, prach_ifft_tmp[i].i};
          prach_ifft[i] += squaredMod(tmp);
        }
      }
    }

    // Search window of this preamble: its cyclic shift converted to IFFT bins
    uint64_t *search = prach_ifft + (preamble_shift * dft_sz) / N_ZC;

    for (int i = 0; i < NCS2; i++) {
      if (search[i] > max.energy || (search[i] == max.energy && max.distance > i)) {
        max.energy = search[i];
        max.distance = i;
        max.preamble = preamble_index;
        LOG_D(NR_PHY_RACH,
              "preamble_index %d, delay %d en %d dB/10\n",
              preamble_index,
              i,
              dB_fixed_times10(max.energy / (dft_sz * nb_rx)));
      }
    }
  } // preamble_index

  // The conversion from raw PRACH delay to TA value is done here.
  // It is normalized to the 30.72 Ms/s, considering the numerology, N_RB and the sampling rate
  // See table 6.3.3.1 -1 and -2 in 38211.

  // Format 0, 1, 2: 24576 samples @ 30.72 Ms/s, 98304 samples @ 122.88 Ms/s
  // By solving:
  // max_preamble_delay * ( (24576*(fs/30.72M)) / 1024 ) / fs = TA * 16 * 64 / 2^mu * Tc

  // Format 3: 6144 samples @ 30.72 Ms/s, 24576 samples @ 122.88 Ms/s
  // By solving:
  // max_preamble_delay * ( (6144*(fs/30.72M)) / 1024 ) / fs = TA * 16 * 64 / 2^mu * Tc

  // Format >3: 2048/2^mu samples @ 30.72 Ms/s, 2048/2^mu * 4 samples @ 122.88 Ms/s
  // By solving:
  // max_preamble_delay * ( (2048/2^mu*(fs/30.72M)) / 256 ) / fs = TA * 16 * 64 / 2^mu * Tc

  // Energy normalized over the IFFT size and the receive antennas (-900, i.e. 0 in dB x10, for an empty occasion)
  rx_prach_out_t out = {.max_preamble_energy = dB_fixed_times10(max.energy / (dft_sz * nb_rx))};
  if (max.energy > 0) {
    out.max_preamble = max.preamble;
    out.max_preamble_delay_raw = max.distance;
    const uint32_t raw_delay = out.max_preamble_delay_raw;
    const int mu = in->numerology_index;
    if (in->prach_sequence_length == 0) {
      const uint32_t mu_scale = 1U << mu;

      if (prach_fmt == 0 || prach_fmt == 1 || prach_fmt == 2)
        out.max_preamble_delay = (raw_delay * 3U * mu_scale + 1U) / 2U;
      else if (prach_fmt == 3)
        out.max_preamble_delay = (raw_delay * 3U * mu_scale + 4U) / 8U;
    } else {
      out.max_preamble_delay = (raw_delay + 1U) / 2U;
    }
  }
  stop_meas(in->rx_prach);
  return out;
}
