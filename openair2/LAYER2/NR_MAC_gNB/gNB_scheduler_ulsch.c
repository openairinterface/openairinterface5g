/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief gNB procedures for the ULSCH transport channel
 */


#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "executables/softmodem-common.h"
#include "common/utils/nr/nr_common.h"
#include "utils.h"
#include <openair2/UTIL/OPT/opt.h>
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair3/NRPPA/nrppa_gNB_config.h"
#include "openair2/F1AP/lib/f1ap_positioning.h"

static const uint16_t NR_TRANSFORM_PRECODE_RB_LUT[274] = {
    0,   1,   2,   3,   4,   5,   6,   6,   8,   9,   10,  10,  12,  12,  12,  15,  16,  16,  18,  18,  20,  20,  20,  20,  24,
    25,  25,  27,  27,  27,  30,  30,  32,  32,  32,  32,  36,  36,  36,  36,  40,  40,  40,  40,  40,  45,  45,  45,  48,  48,
    50,  50,  50,  50,  54,  54,  54,  54,  54,  54,  60,  60,  60,  60,  64,  64,  64,  64,  64,  64,  64,  64,  72,  72,  72,
    75,  75,  75,  75,  75,  80,  81,  81,  81,  81,  81,  81,  81,  81,  81,  90,  90,  90,  90,  90,  90,  96,  96,  96,  96,
    100, 100, 100, 100, 100, 100, 100, 100, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 120, 120, 120, 120, 120,
    125, 125, 125, 128, 128, 128, 128, 128, 128, 128, 135, 135, 135, 135, 135, 135, 135, 135, 135, 144, 144, 144, 144, 144, 144,
    150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 160, 160, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162,
    162, 162, 162, 162, 162, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 192, 192, 192, 192, 192, 192, 192, 192,
    200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 216, 216, 216, 216, 216, 216, 216, 216, 216,
    225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 240, 240, 240, 243, 243, 243, 243, 243, 243, 243,
    250, 250, 250, 250, 250, 250, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 270, 270, 270, 270};

//#define SRS_IND_DEBUG
#define MAX_NUM_DATA_IND 1024

// With SC-FDMA the scheduler in uplink needs to schedule N_PRB=2^x3^y5^z
// Check 6.3.1.4 of 38.211
int check_sc_fdma_rbsize(long transform_precoding, uint16_t rb)
{
  DevAssert(rb < 274);
  if (transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled) {
    return NR_TRANSFORM_PRECODE_RB_LUT[rb];
  }
  return rb;
}

/* \brief Get the number of UL TDAs that could be used in slot, reachable
 * via specific k2. The output parameter first_idx is a pointer to the first
 * suitable TDA, and the function returns the number of suitable TDAs, or 0. */
int get_num_ul_tda(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, int slot, int k2, const NR_tda_info_t **first_idx)
{
  const uint16_t ul_bitmap = get_ul_bitmap(&cell->frame_structure, slot);
  *first_idx = NULL;
  FOR_EACH_SEQ_ARR(NR_tda_info_t *, tda, &cell->ul_tda) {
    DevAssert(tda->valid_tda);
    // nr_rrc_config_ul_tda() orders by k2, so skip smaller and return for
    // bigger ones
    if (tda->k2 < k2)
      continue;
    if (tda->k2 > k2)
      break; // there won't be a suitable k2 anymore

    uint16_t tda_bitmap = SL_to_bitmap(tda->startSymbolIndex, tda->nrOfSymbols);
    // nr_rrc_config_ul_tda() assures TDAs are from largest to smallest symbol number.
    // check that this TDA fits entirely into this slot's mask (slot might be
    // smaller in mixed slots)., A mixed slot TDA would be checked last, so a
    // full slot TDA will be used for a full UL slot.
    if ((tda_bitmap & ul_bitmap) == tda_bitmap) { // if TDA fits entiry
      *first_idx = tda;
      break;
    }
  }
  if (*first_idx == NULL) /* nothing fit */
    return 0;

  NR_tda_info_t *end_it = seq_arr_next(&cell->ul_tda, *first_idx);
  while (end_it != seq_arr_end(&cell->ul_tda) && end_it->k2 == k2) {
    /* the following TDAs should all fit as long as the k2 is the same */
    uint16_t tda_bitmap = SL_to_bitmap(end_it->startSymbolIndex, end_it->nrOfSymbols);
    AssertFatal((tda_bitmap & ul_bitmap) == tda_bitmap,
                "TDA should fit inside slot, but is not the case for k2 %ld bitmap 0x%04x\n",
                end_it->k2,
                tda_bitmap);
    end_it = seq_arr_next(&cell->ul_tda, end_it);
  }

  ptrdiff_t diff = seq_arr_dist(&cell->ul_tda, *first_idx, end_it);
  AssertFatal(diff > 0 && diff <= seq_arr_size(&cell->ul_tda), "dist %ld\n", diff);
  return diff;
}

static void get_max_rb_range(const uint16_t *vrb_map_ul, const uint16_t *ulprbbl, uint16_t mask, int *rb_start, int *rb_len)
{
  int best_start = *rb_start;
  int best_len = 0;
  int current_start = *rb_start;
  int current_len = 0;
  for (int rb = *rb_start; rb < *rb_len; ++rb) {
    if (ulprbbl[rb] == 0 && (mask & vrb_map_ul[rb]) == 0) {
      current_len++;
      continue;
    }

    /* this RB is blocked for UL, or already in use */
    if (current_len > best_len) {
      best_start = current_start;
      best_len = current_len;
    }
    current_start = rb + 1; /* will start in next RB, or updated later */
    current_len = 0;
  }
  if (current_len > best_len) {
    best_start = current_start;
    best_len = current_len;
  }
  *rb_start = best_start;
  *rb_len = best_len;
}

const NR_tda_info_t *get_best_ul_tda(const nr_cell_sched_t *cell, int beam, const NR_tda_info_t *tdas, int n_tda, int frame, int slot, int *rb_start, int *rb_len)
{
  /* there is a mixed slot only when in TDD */
  const frame_structure_t *fs = &cell->frame_structure;
  const int index = ul_buffer_index(frame, slot, fs->numb_slots_frame, cell->vrb_map_UL_size);
  uint16_t *vrb_map_UL = &cell->common_channels.vrb_map_UL[beam][index * MAX_BWP_SIZE];

  DevAssert(n_tda <= 16);
  const NR_tda_info_t *best_tda = tdas;
  uint64_t score = 0;
  int check_rb_start = *rb_start;
  int check_rb_len = *rb_len;

  for (int i = 0; i < n_tda; ++i, tdas++) {
    int start = check_rb_start;
    int len = check_rb_len;
    uint16_t tda_mask = SL_to_bitmap(tdas->startSymbolIndex, tdas->nrOfSymbols);
    get_max_rb_range(vrb_map_UL, cell->ulprbbl, tda_mask, &start, &len);
    uint64_t s = (uint64_t)tdas->nrOfSymbols * len;
    if (s > score) {
      best_tda = tdas;
      score = s;
      *rb_start = start;
      *rb_len = len;
    }
    LOG_D(NR_MAC, "%4d.%2d tda k2 %ld mask 0x%04x has PRB start/len %d/%d score %ld\n", frame, slot, tdas->k2, tda_mask, start, len, s);
  }

  return best_tda;
}

bwp_info_t get_pusch_bwp_start_size(NR_UE_info_t *UE)
{
  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  bwp_info_t bwp_info;
  bwp_info.bwpStart = ul_bwp->BWPStart;

  // 3GPP TS 38.214 Section 6.1.2.2.2 Uplink resource allocation type 1
  // In uplink resource allocation of type 1, the resource block assignment information indicates to a scheduled UE a set of
  // contiguously allocated non-interleaved virtual resource blocks within the active bandwidth part of size   PRBs except for the
  // case when DCI format 0_0 is decoded in any common search space in which case the size of the initial UL bandwidth part shall
  // be used.
  if (ul_bwp->dci_format == NR_UL_DCI_FORMAT_0_0
      && sched_ctrl->search_space->searchSpaceType
      && sched_ctrl->search_space->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_common) {
    bwp_info.bwpSize = min(ul_bwp->BWPSize, UE->sc_info.initial_ul_BWPSize);
  } else {
    bwp_info.bwpSize = ul_bwp->BWPSize;
  }
  return bwp_info;
}

static float compute_ph_rb_factor(int mu, int rb)
{
  return roundf(10 * log10(rb << mu));
}

static float compute_ph_mcs_factor(const NR_sched_pusch_t *pusch)
{
  // 38.213 7.1.1
  // if the PUSCH transmission is over more than one layer delta_tf = 0
  if (pusch->nrOfLayers != 1)
    return 0.0f;

  const NR_pusch_dmrs_t *dmrs = &pusch->dmrs_info;
  const int n_dmrs = dmrs->num_dmrs_symb * dmrs->N_PRB_DMRS;
  const int n_re = (NR_NB_SC_PER_RB * pusch->tda_info.nrOfSymbols - n_dmrs) * pusch->rbSize;
  const float BPRE = (float)(pusch->tb_size << 3) / n_re; // TODO change for PUSCH with CSI
  const float f = pow(2, BPRE * 1.25);
  const float beta = 1.0f; // TODO change for PUSCH with CSI
  return 10 * log10((f - 1) * beta);
}

/* \brief over-estimate the BSR index, given real_index.
 *
 * BSR does not account for headers, so we need to estimate. See 38.321
 * 6.1.3.1: "The size of the RLC headers and MAC subheaders are not considered
 * in the buffer size computation." */
static int overestim_bsr_index(int real_index)
{
  /* if UE reports BSR 0, it means "no data"; otherwise, overestimate to
   * account for headers */
  const int add_overestim = 1;
  return real_index > 0 ? real_index + add_overestim : real_index;
}

static int estimate_ul_buffer_short_bsr(const NR_BSR_SHORT *bsr)
{
  /* NOTE: the short BSR might be for different LCGID than 0, but we do not
   * differentiate them */
  int rep_idx = bsr->Buffer_size;
  int estim_idx = overestim_bsr_index(rep_idx);
  int max = NR_SHORT_BSR_TABLE_SIZE - 1;
  int idx = min(estim_idx, max);
  int estim_size = get_short_bsr_value(idx);
  LOG_D(NR_MAC, "short BSR LCGID %d index %d estim index %d size %d\n", bsr->LcgID, rep_idx, estim_idx, estim_size);
  return estim_size;
}

static int estimate_ul_buffer_long_bsr(const NR_BSR_LONG *bsr)
{
  LOG_D(NR_MAC,
        "LONG BSR, LCG ID(7-0) %d/%d/%d/%d/%d/%d/%d/%d\n",
        bsr->LcgID7,
        bsr->LcgID6,
        bsr->LcgID5,
        bsr->LcgID4,
        bsr->LcgID3,
        bsr->LcgID2,
        bsr->LcgID1,
        bsr->LcgID0);
  bool bsr_active[8] = {bsr->LcgID0 != 0, bsr->LcgID1 != 0, bsr->LcgID2 != 0, bsr->LcgID3 != 0, bsr->LcgID4 != 0, bsr->LcgID5 != 0, bsr->LcgID6 != 0, bsr->LcgID7 != 0};

  int estim_size = 0;
  int max = NR_LONG_BSR_TABLE_SIZE - 1;
  uint8_t *payload = ((uint8_t*) bsr) + 1;
  int m = 0;
  const int total_lcgids = 8; /* see 38.321 6.1.3.1 */
  for (int n = 0; n < total_lcgids; n++) {
    if (!bsr_active[n])
      continue;
    int rep_idx = payload[m];
    int estim_idx = overestim_bsr_index(rep_idx);
    int idx = min(estim_idx, max);
    estim_size += get_long_bsr_value(idx);

    LOG_D(NR_MAC, "LONG BSR LCGID/m %d/%d Index %d estim index %d size %d", n, m, rep_idx, estim_idx, estim_size);
    m++;
  }
  return estim_size;
}

static void handle_single_entry_phr(const NR_UE_UL_BWP_t *ul_bwp,
                                    NR_UE_sched_ctrl_t *sched_ctrl,
                                    int harq_pid,
                                    const NR_SINGLE_ENTRY_PHR_MAC_CE *phr)
{
  if (harq_pid < 0) {
    LOG_E(NR_MAC, "Invalid HARQ PID %d\n", harq_pid);
    return;
  }
  NR_sched_pusch_t *sched_pusch = &sched_ctrl->ul_harq_processes[harq_pid].sched_pusch;

  /* Save the phr info */
  int PH;
  const int PCMAX = phr->PCMAX;
  /* 38.133 Table10.1.17.1-1 */
  if (phr->PH < 55) {
    PH = phr->PH - 32;
  } else if (phr->PH < 63) {
    PH = 24 + (phr->PH - 55) * 2;
  } else {
    PH = 38;
  }
  // in sched_ctrl we set normalized PH wrt MCS and PRBs
  sched_ctrl->ph = PH + compute_ph_rb_factor(ul_bwp->scs, sched_pusch->rbSize);
  bool hasDeltaMCS = ul_bwp->pusch_Config && ul_bwp->pusch_Config->pusch_PowerControl->deltaMCS;
  if (hasDeltaMCS)
    sched_ctrl->ph += compute_ph_mcs_factor(sched_pusch);
  /* 38.133 Table10.1.18.1-1 */
  sched_ctrl->pcmax = PCMAX - 29;
  LOG_D(NR_MAC,
        "SINGLE ENTRY PHR %d PH %d (%d dB) R2 %d PCMAX %d (%d dBm)\n",
        phr->R1,
        PH,
        sched_ctrl->ph,
        phr->R2,
        PCMAX,
        sched_ctrl->pcmax);
}

//  For both UL-SCH except:
//   - UL-SCH: fixed-size MAC CE(known by LCID)
//   - UL-SCH: padding
//   - UL-SCH: MSG3 48-bits
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |       L       |
//
//  For:
//   - UL-SCH: fixed-size MAC CE(known by LCID)
//   - UL-SCH: padding, for single/multiple 1-oct padding CE(s)
//   - UL-SCH: MSG3 48-bits
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|R|   LCID    |
//
//  LCID: The Logical Channel ID field identifies the logical channel instance of the corresponding MAC SDU or the type of the corresponding MAC CE or padding as described in Tables 6.2.1-1 and 6.2.1-2 for the DL-SCH and UL-SCH respectively. There is one LCID field per MAC subheader. The LCID field size is 6 bits;
//  L: The Length field indicates the length of the corresponding MAC SDU or variable-sized MAC CE in bytes. There is one L field per MAC subheader except for subheaders corresponding to fixed-sized MAC CEs and padding. The size of the L field is indicated by the F field;
//  F: length of L is 0:8 or 1:16 bits wide
//  R: Reserved bit, set to zero.

// return: length of subPdu header
// 3GPP TS 38.321 Section 6
static uint8_t decode_ul_mac_sub_pdu_header(uint8_t *pduP, uint8_t *lcid, uint16_t *length)
{
  uint16_t mac_subheader_len = 1;
  *lcid = pduP[0] & 0x3F;

  switch (*lcid) {
    case UL_SCH_LCID_CCCH_64_BITS:
      *length = 8;
      break;
    case UL_SCH_LCID_SRB1:
    case UL_SCH_LCID_SRB2:
    case UL_SCH_LCID_DTCH ...(UL_SCH_LCID_DTCH + 28):
    case UL_SCH_LCID_L_TRUNCATED_BSR:
    case UL_SCH_LCID_L_BSR:
      if (pduP[0] & 0x40) { // F = 1
        mac_subheader_len = 3;
        *length = (pduP[1] << 8) + pduP[2];
      } else { // F = 0
        mac_subheader_len = 2;
        *length = pduP[1];
      }
      break;
    case UL_SCH_LCID_CCCH_48_BITS_REDCAP:
    case UL_SCH_LCID_CCCH_48_BITS:
      *length = 6;
      break;
    case UL_SCH_LCID_SINGLE_ENTRY_PHR:
    case UL_SCH_LCID_C_RNTI:
      *length = 2;
      break;
    case UL_SCH_LCID_S_TRUNCATED_BSR:
    case UL_SCH_LCID_S_BSR:
      *length = 1;
      break;
    case UL_SCH_LCID_PADDING:
      // Nothing to do
      break;
    default:
      LOG_E(NR_MAC, "LCID %0x not handled yet!\n", *lcid);
      break;
  }

  LOG_D(NR_MAC, "Decoded LCID 0x%X, header bytes: %d, payload bytes %d\n", *lcid, mac_subheader_len, *length);

  return mac_subheader_len;
}

static rnti_t lcid_crnti_lookahead(uint8_t *pdu, uint32_t pdu_len)
{
  while (pdu_len > 0) {
    uint16_t mac_len = 0;
    uint8_t lcid = 0;
    uint16_t mac_subheader_len = decode_ul_mac_sub_pdu_header(pdu, &lcid, &mac_len);
    // Check for valid PDU
    if (mac_subheader_len + mac_len > pdu_len) {
      LOG_E(NR_MAC,
            "Invalid PDU! mac_subheader_len: %d, mac_len: %d, remaining pdu_len: %d\n",
            mac_subheader_len,
            mac_len,
            pdu_len);

      LOG_E(NR_MAC, "Residual UL MAC PDU: ");
      uint32_t print_len = pdu_len > 30 ? 30 : pdu_len; // Only printf 1st - 30nd bytes
      for (int i = 0; i < print_len; i++)
        printf("%02x ", pdu[i]);
      printf("\n");
      return 0;
    }

    if (lcid == UL_SCH_LCID_C_RNTI) {
      // Extract C-RNTI value
      rnti_t crnti = ((pdu[1] & 0xFF) << 8) | (pdu[2] & 0xFF);
      LOG_A(NR_MAC, "Received a MAC CE for C-RNTI with %04x\n", crnti);
      return crnti;
    } else if (lcid == UL_SCH_LCID_PADDING) {
      // End of MAC PDU, can ignore the remaining bytes
      return 0;
    }

    pdu += mac_len + mac_subheader_len;
    pdu_len -= mac_len + mac_subheader_len;
  }
  return 0;
}

/* Decodes the MAC PDU; its RLC PDUs are returned in data_ind[] (pointing into pduP) for
 * the caller to hand to RLC once sched_lock is released. */
static int nr_process_mac_pdu(gNB_MAC_INST *nrmac,
                              nr_cell_sched_t *cell,
                              NR_UE_info_t *UE,
                              frame_t frameP,
                              slot_t slot,
                              uint8_t *pduP,
                              uint32_t pdu_len,
                              const int8_t harq_pid,
                              nr_rlc_data_ind_t *data_ind,
                              int *num_data_ind)
{
  int sdus = 0;
  NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;

  if (pduP[0] != UL_SCH_LCID_PADDING) {
    ws_trace_t tmp = {.nr = true,
                      .direction = DIRECTION_UPLINK,
                      .type = cell->common_channels.frame_type == FDD ? FDD_RADIO : TDD_RADIO,
                      .pdu_buffer = pduP,
                      .pdu_buffer_size = pdu_len,
                      .ueid = 0,
                      .rntiType = WS_C_RNTI,
                      .rnti = UE->rnti,
                      .sysFrame = frameP,
                      .subframe = slot,
                      .harq_pid = harq_pid};
    trace_pdu(&tmp);
  }

#ifdef ENABLE_MAC_PAYLOAD_DEBUG
  LOG_I(NR_MAC, "In %s: dumping MAC PDU in %d.%d:\n", __func__, frameP, slot);
  log_dump(NR_MAC, pduP, pdu_len, LOG_DUMP_CHAR, "\n");
#endif

  while (pdu_len > 0) {
    uint16_t mac_len = 0;
    uint8_t lcid = 0;

    uint16_t mac_subheader_len = decode_ul_mac_sub_pdu_header(pduP, &lcid, &mac_len);
    // Check for valid PDU
    if (mac_subheader_len + mac_len > pdu_len) {
      LOG_E(NR_MAC,
            "Invalid PDU in %d.%d for RNTI %04X! mac_subheader_len: %d, mac_len: %d, remaining pdu_len: %d\n",
            frameP,
            slot,
            UE->rnti,
            mac_subheader_len,
            mac_len,
            pdu_len);

      LOG_E(NR_MAC, "Residual UL MAC PDU: ");
      int print_len = pdu_len > 30 ? 30 : pdu_len; // Only printf 1st - 30nd bytes
      for (int i = 0; i < print_len; i++)
        printf("%02x ", pduP[i]);
      printf("\n");
      break;
    }

    LOG_D(NR_MAC,
          "Received UL-SCH sub-PDU with LCID 0x%X in %d.%d for RNTI %04X (remaining PDU length %d)\n",
          lcid,
          frameP,
          slot,
          UE->rnti,
          pdu_len);

    unsigned char *ce_ptr;

    switch (lcid) {
      case UL_SCH_LCID_CCCH_64_BITS:
      case UL_SCH_LCID_CCCH_48_BITS_REDCAP:
      case UL_SCH_LCID_CCCH_48_BITS:
        if (lcid == UL_SCH_LCID_CCCH_64_BITS) {
          // Check if it is a valid CCCH1 message, we get all 00's messages very often
          bool valid_pdu = false;
          for (int i = 0; i < (mac_subheader_len + mac_len); i++) {
            if (pduP[i] != 0) {
              valid_pdu = true;
              break;
            }
          }
          if (!valid_pdu) {
            LOG_D(NR_MAC, "%s() Invalid CCCH1 message!, pdu_len: %d\n", __func__, pdu_len);
            return 0;
          }
        }

        LOG_I(MAC, "[RAPROC] Received SDU for CCCH length %d for UE %04x\n", mac_len, UE->rnti);

        if (lcid == UL_SCH_LCID_CCCH_48_BITS_REDCAP) {
          LOG_I(MAC, "UE with RNTI %04x is RedCap\n", UE->rnti);
          UE->is_redcap = true;
        }

        if (prepare_initial_ul_rrc_message(nrmac, cell, UE)) {
          nr_rlc_data_ind_t ind = {.ch = 0, .buf = pduP + mac_subheader_len, .len = mac_len};
          data_ind[(*num_data_ind)++] = ind;
          DevAssert(*num_data_ind < MAX_NUM_DATA_IND);
        } else {
          LOG_E(NR_MAC, "prepare_initial_ul_rrc_message() returned false, cannot forward CCCH message\n");
        }
        break;

      case UL_SCH_LCID_SRB1:
      case UL_SCH_LCID_SRB2:
        AssertFatal(UE->CellGroup,
                    "UE %04x %d.%d: Received LCID %d which is not configured (UE has no CellGroup)\n",
                    UE->rnti,
                    frameP,
                    slot,
                    lcid);

        const nr_lc_config_t *srbc = nr_mac_get_lc_config(sched_ctrl, lcid);
        if (!srbc || srbc->suspended) {
          LOG_I(NR_MAC, "RNTI %04x LCID %d: ignoring %d bytes\n", UE->rnti, lcid, mac_len);
        } else {
          nr_rlc_data_ind_t ind = {.ch = lcid, .buf = pduP + mac_subheader_len, .len = mac_len};
          data_ind[(*num_data_ind)++] = ind;
          DevAssert(*num_data_ind < MAX_NUM_DATA_IND);

          UE->mac_stats.ul.total_sdu_bytes += mac_len;
          UE->mac_stats.ul.lc_bytes[lcid] += mac_len;
        }
        T(T_GNB_MAC_LCID_UL, T_INT(UE->rnti), T_INT(frameP), T_INT(slot), T_INT(lcid), T_INT(mac_len * 8));
        break;

      case UL_SCH_LCID_DTCH ...(UL_SCH_LCID_DTCH + 28):
        LOG_D(NR_MAC,
              "[UE %04x] %d.%d : ULSCH -> UL-%s %d (gNB %d, %d bytes)\n",
              UE->rnti,
              frameP,
              slot,
              lcid < 4 ? "DCCH" : "DTCH",
              lcid,
              nrmac->Mod_id,
              mac_len);
        const nr_lc_config_t *c = nr_mac_get_lc_config(sched_ctrl, lcid);
        if (!c || c->suspended) {
          LOG_I(NR_MAC, "RNTI %04x LCID %d: ignoring %d bytes\n", UE->rnti, lcid, mac_len);
        } else {
          UE->mac_stats.ul.total_sdu_bytes += mac_len;
          UE->mac_stats.ul.lc_bytes[lcid] += mac_len;

          nr_rlc_data_ind_t ind = {.ch = lcid, .buf = pduP + mac_subheader_len, .len = mac_len};
          data_ind[(*num_data_ind)++] = ind;
          DevAssert(*num_data_ind < MAX_NUM_DATA_IND);

          sdus += 1;
          /* Updated estimated buffer when receiving data */
          if (sched_ctrl->estimated_ul_buffer >= mac_len)
            sched_ctrl->estimated_ul_buffer -= mac_len;
          else
            sched_ctrl->estimated_ul_buffer = 0;
        }
        T(T_GNB_MAC_LCID_UL, T_INT(UE->rnti), T_INT(frameP), T_INT(slot), T_INT(lcid), T_INT(mac_len * 8));
        break;

      case UL_SCH_LCID_RECOMMENDED_BITRATE_QUERY:
        // 38.321 Ch6.1.3.20
        break;

      case UL_SCH_LCID_MULTI_ENTRY_PHR_4_OCT:
        LOG_E(NR_MAC, "Multi entry PHR not supported\n");
        break;

      case UL_SCH_LCID_CONFIGURED_GRANT_CONFIRMATION:
        // 38.321 Ch6.1.3.7
        break;

      case UL_SCH_LCID_MULTI_ENTRY_PHR_1_OCT:
        LOG_E(NR_MAC, "Multi entry PHR not supported\n");
        break;

      case UL_SCH_LCID_SINGLE_ENTRY_PHR:
        /* Extract SINGLE ENTRY PHR elements for PHR calculation */
        ce_ptr = &pduP[mac_subheader_len];
        NR_SINGLE_ENTRY_PHR_MAC_CE *phr = (NR_SINGLE_ENTRY_PHR_MAC_CE *)ce_ptr;
        handle_single_entry_phr(ul_bwp, sched_ctrl, harq_pid, phr);
        break;

      case UL_SCH_LCID_C_RNTI:
        if (UE->ra && UE->ra->ra_state == nrRA_gNB_IDLE) {
          // Extract C-RNTI value
          rnti_t crnti = ((pduP[1] & 0xFF) << 8) | (pduP[2] & 0xFF);
          AssertFatal(false,
                      "Received MAC CE for C-RNTI %04x without RA running, procedure exists? Or is it a bug while decoding the "
                      "MAC PDU?\n",
                      crnti);
        }
        break;

      case UL_SCH_LCID_S_TRUNCATED_BSR:
      case UL_SCH_LCID_S_BSR:
        /* Extract short BSR value */
        ce_ptr = &pduP[mac_subheader_len];
        sched_ctrl->estimated_ul_buffer = estimate_ul_buffer_short_bsr((NR_BSR_SHORT *)ce_ptr);
        sched_ctrl->sched_ul_bytes = 0;
        LOG_D(NR_MAC, "SHORT BSR at %4d.%2d, est buf %d\n", frameP, slot, sched_ctrl->estimated_ul_buffer);
        break;

      case UL_SCH_LCID_L_TRUNCATED_BSR:
      case UL_SCH_LCID_L_BSR:
        /* Extract long BSR value */
        ce_ptr = &pduP[mac_subheader_len];
        sched_ctrl->estimated_ul_buffer = estimate_ul_buffer_long_bsr((NR_BSR_LONG *)ce_ptr);
        sched_ctrl->sched_ul_bytes = 0;
        LOG_D(NR_MAC, "LONG BSR at %4d.%2d, estim buf %d\n", frameP, slot, sched_ctrl->estimated_ul_buffer);
        break;

      case UL_SCH_LCID_PADDING:
        // End of MAC PDU, can ignore the rest.
        // 38.321 Sec 6.1.5: "Presence and length of padding is implicit based
        // on TB size, size of MAC subPDU(s)."
        mac_len = pdu_len - mac_subheader_len; // will make pdu_len go to 0
        break;

      default:
        LOG_E(NR_MAC, "RNTI %0x [%d.%d], received unknown MAC header (LCID = 0x%02x)\n", UE->rnti, frameP, slot, lcid);
        mac_len = pdu_len - mac_subheader_len; // will make pdu_len go to 0
        break;
    }


#ifdef ENABLE_MAC_PAYLOAD_DEBUG
    if (lcid < 45 || lcid == 52 || lcid == 63) {
      LOG_I(NR_MAC, "In %s: dumping UL MAC SDU sub-header with length %d (LCID = 0x%02x):\n", __func__, mac_subheader_len, lcid);
      log_dump(NR_MAC, pduP, mac_subheader_len, LOG_DUMP_CHAR, "\n");
      LOG_I(NR_MAC, "In %s: dumping UL MAC SDU with length %d (LCID = 0x%02x):\n", __func__, mac_len, lcid);
      log_dump(NR_MAC, pduP + mac_subheader_len, mac_len, LOG_DUMP_CHAR, "\n");
    } else {
      LOG_I(NR_MAC, "In %s: dumping UL MAC CE with length %d (LCID = 0x%02x):\n", __func__, mac_len, lcid);
      log_dump(NR_MAC, pduP + mac_subheader_len + mac_len, mac_len, LOG_DUMP_CHAR, "\n");
    }
#endif

    pduP += (mac_subheader_len + mac_len);
    pdu_len -= (mac_subheader_len + mac_len);
  }

  UE->mac_stats.ul.num_mac_sdu += sdus;

  return 0;
}

static void finish_nr_ul_harq(NR_UE_sched_ctrl_t *sched_ctrl, int harq_pid)
{
  NR_UE_ul_harq_t *harq = &sched_ctrl->ul_harq_processes[harq_pid];

  harq->ndi ^= 1;
  harq->round = 0;

  add_tail_nr_list(&sched_ctrl->available_ul_harq, harq_pid);
}

static void abort_nr_ul_harq(NR_UE_info_t *UE, int8_t harq_pid)
{
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_ul_harq_t *harq = &sched_ctrl->ul_harq_processes[harq_pid];

  finish_nr_ul_harq(sched_ctrl, harq_pid);
  UE->mac_stats.ul.errors++;

  /* the transmission failed: the UE won't send the data we expected initially,
   * so retrieve to correctly schedule after next BSR */
  sched_ctrl->sched_ul_bytes -= harq->sched_pusch.tb_size;
  if (sched_ctrl->sched_ul_bytes < 0)
    sched_ctrl->sched_ul_bytes = 0;
}

static void handle_nr_ul_harq(nr_cell_sched_t *cell, NR_UE_info_t *UE, rnti_t rnti, int crc_harq_id, bool crc_status)
{
  if (cell->radio_config.disable_harq) {
    LOG_D(NR_MAC, "skipping UL feedback handling as HARQ is disabled\n");
    return;
  }

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  int8_t harq_pid = sched_ctrl->feedback_ul_harq.head;
  LOG_D(NR_MAC, "Comparing crc harq_id vs feedback harq_pid = %d %d\n", crc_harq_id, harq_pid);
  while (crc_harq_id != harq_pid || harq_pid < 0) {
    LOG_W(NR_MAC, "Unexpected ULSCH HARQ PID %d (have %d) for RNTI 0x%04x\n", crc_harq_id, harq_pid, rnti);
    if (harq_pid < 0)
      return;

    remove_front_nr_list(&sched_ctrl->feedback_ul_harq);
    sched_ctrl->ul_harq_processes[harq_pid].is_waiting = false;

    if(sched_ctrl->ul_harq_processes[harq_pid].round >= cell->ul_bler.harq_round_max - 1) {
      abort_nr_ul_harq(UE, harq_pid);
    } else {
      sched_ctrl->ul_harq_processes[harq_pid].round++;
      add_tail_nr_list(&sched_ctrl->retrans_ul_harq, harq_pid);
    }
    harq_pid = sched_ctrl->feedback_ul_harq.head;
  }
  remove_front_nr_list(&sched_ctrl->feedback_ul_harq);
  NR_UE_ul_harq_t *harq = &sched_ctrl->ul_harq_processes[harq_pid];
  DevAssert(harq->is_waiting);
  harq->feedback_slot = -1;
  harq->is_waiting = false;
  if (!crc_status) {
    finish_nr_ul_harq(sched_ctrl, harq_pid);
    LOG_D(NR_MAC,
          "Ulharq id %d crc passed for RNTI %04x\n",
          harq_pid,
          rnti);
  } else if (harq->round >= cell->ul_bler.harq_round_max  - 1) {
    abort_nr_ul_harq(UE, harq_pid);
    LOG_D(NR_MAC,
          "RNTI %04x: Ulharq id %d crc failed in all rounds\n",
          rnti,
          harq_pid);
  } else {
    harq->round++;
    LOG_D(NR_MAC,
          "Ulharq id %d crc failed for RNTI %04x\n",
          harq_pid,
          rnti);
    add_tail_nr_list(&sched_ctrl->retrans_ul_harq, harq_pid);
  }
}

static void handle_msg3_failed_rx(gNB_MAC_INST *mac, NR_RA_t *ra, rnti_t rnti, int harq_round_max)
{
  // For RA 2-Step consider RA failed if MsgA-PUSCH not detected
  if (ra->ra_type == RA_2_STEP) {
    LOG_W(NR_MAC, "UE %04x RA failed at state %s (MsgA-PUSCH reception failed)\n", rnti, nrra_text[ra->ra_state]);
    NR_UE_info_t *UE = remove_ra_ue(&mac->UE_info, rnti);
    DevAssert(UE); // must exist, otherwise would not be here.
    release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
    return;
  }

  if (ra->msg3_round >= harq_round_max - 1) {
    LOG_W(NR_MAC, "UE %04x RA failed at state %s (Reached msg3 max harq rounds)\n", rnti, nrra_text[ra->ra_state]);
    NR_UE_info_t *UE = remove_ra_ue(&mac->UE_info, rnti);
    DevAssert(UE); // must exist, otherwise would not be here.
    release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
    return;
  }

  LOG_D(NR_MAC, "UE %04x Msg3 CRC did not pass\n", rnti);
  ra->msg3_round++;
  ra->ra_state = nrRA_Msg3_retransmission;
}

static void nr_rx_ra_sdu(gNB_MAC_INST *mac,
                         nr_cell_sched_t *cell,
                         const frame_t frame,
                         const sub_frame_t slot,
                         rnti_t rnti,
                         uint8_t *sdu,
                         const uint32_t sdu_len,
                         uint8_t harq_pid,
                         const uint16_t timing_advance,
                         const uint8_t ul_cqi,
                         const uint16_t rssi,
                         nr_rlc_data_ind_t *data_ind,
                         int *num_data_ind,
                         rnti_t *rlc_rnti)
{
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  NR_UE_info_t *UE = find_ra_ue(&mac->UE_info, rnti);
  if (!UE) {
    LOG_D(NR_MAC, "UL SDU discarded. Couldn't find UE with RNTI %04x \n", rnti);
    return;
  }

  NR_RA_t *ra = UE->ra;
  if (ra->ra_type == RA_4_STEP && ra->ra_state != nrRA_WAIT_Msg3) {
    LOG_W(NR_MAC, "UL SDU discarded for RNTI %04x RA state not waiting for PUSCH\n", UE->rnti);
    return;
  }

  // CFRA: we scheduled Msg3 (which does not exist in CFRA, see also
  // nr_generate_Msg2()). We did not mark RA as complete right away, as the
  // DLSCH scheduler might schedule in the same slot as Msg2 if RLC has data
  // (which can only happen in do-ra), so we mark it as complete now.
  // RA is free and zero when CFRA, store values before free for later usage
  bool cfra = ra->cfra;
  int ra_type = ra->ra_type;
  if (cfra) {
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    reset_dl_harq_list(sched_ctrl);
    reset_ul_harq_list(sched_ctrl);
    // we configure the UE using dedicated search space: In SA (CFRA used for
    // handover) and NSA (or do-ra), the UE has the full config already.
    int ss_type = NR_SearchSpace__searchSpaceType_PR_ue_Specific;
    configure_UE_BWP(cell, scc, UE, false, ss_type, -1, -1);
    // initialize ta_frame in case there is no Msg3 received
    UE->UE_sched_ctrl.ta_frame = (frame + 100) % MAX_FRAME_NUMBER;
    if (!transition_ra_connected_nr_ue(mac, UE)) {
      LOG_E(NR_MAC, "cannot add UE %04x: list is full\n", UE->rnti);
      release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
    } else {
      LOG_A(NR_MAC, "(rnti 0x%04x) CFRA procedure succeeded!\n", UE->rnti);
    }
  }

  if (ul_cqi != 0xff) {
    // Msg3: reset average with first measurement. If this fails (e.g., ul_cqi == 0xff)
    // everything starts from predetermined value
    nr_mac_pc_reset_snr(&UE->UE_sched_ctrl.pusch_pc, ul_cqi * 5 - 640, rssi);
  }

  if (!sdu) { // NACK
    if (cfra)  // no Msg3 on CFRA, no problem
      return;

    if (ra_type == RA_4_STEP && ra->ra_state != nrRA_WAIT_Msg3)
      return;

    if (ra_type == RA_2_STEP && ra->ra_state != nrRA_WAIT_MsgA_PUSCH)
      return;

    if (ra_type == RA_4_STEP && ((frame != ra->Msg3_frame) || (slot != ra->Msg3_slot)))
      return;

    if (ul_cqi != 0xff)
      ra->msg3_TPC = nr_mac_get_tpc(&UE->UE_sched_ctrl.pusch_pc);

    handle_msg3_failed_rx(mac, ra, UE->rnti, cell->ul_bler.harq_round_max);
    return;
  }


  bool no_sig = true;
  for (uint32_t k = 0; k < sdu_len; k++) {
    if (sdu[k] != 0) {
      no_sig = false;
      break;
    }
  }

  T(T_GNB_MAC_UL_PDU_WITH_DATA, T_INT(mac->Mod_id), T_INT(cell->nr_cellid),
    T_INT(rnti), T_INT(frame), T_INT(slot), T_INT(-1) /* harq_pid */,
    T_BUFFER(sdu, sdu_len));


  if (no_sig) {
    LOG_W(NR_MAC, "%s ULSCH with no signal\n", ra_type == RA_2_STEP ? "MsgA-PUSCH" : "MSG3");
    if (!cfra)
      handle_msg3_failed_rx(mac, ra, UE->rnti, cell->ul_bler.harq_round_max);
    return;
  }
  if (!cfra && ra_type == RA_2_STEP) {
    // random access pusch with RA-RNTI
    if (ra->RA_rnti != rnti) {
      LOG_E(NR_MAC, "expected RA_RNTI %04x to match current RNTI %04x\n", ra->RA_rnti, rnti);
      return;
    }
  }

  // re-initialize ta update variables after RA procedure completion
  UE->UE_sched_ctrl.ta_frame = (frame + 100) % MAX_FRAME_NUMBER;

  if (!cfra)
    LOG_I(NR_MAC,
          "%4d.%2d PUSCH with %s 0x%04x received correctly\n",
          frame,
          slot,
          ra_type == RA_2_STEP ? "RA_RNTI" : "TC_RNTI",
          rnti);

  NR_UE_sched_ctrl_t *UE_scheduling_control = &UE->UE_sched_ctrl;
  DevAssert(harq_pid >= 0 && harq_pid < 8);
  if (timing_advance != 0xffff)
    UE_scheduling_control->ta_update = timing_advance;

  LOG_D(NR_MAC, "[RAPROC] Received %s:\n", ra_type == RA_2_STEP ? "MsgA-PUSCH" : "Msg3");
  for (uint32_t k = 0; k < sdu_len; k++) {
    LOG_D(NR_MAC, "(%i): 0x%x\n", k, sdu[k]);
  }

  // 3GPP TS 38.321 Section 5.4.3 Multiplexing and assembly
  // Logical channels shall be prioritised in accordance with the following order (highest priority listed first):
  // - MAC CE for C-RNTI, or data from UL-CCCH;
  // This way, we need to process MAC CE for C-RNTI if RA is active and it is present in the MAC PDU
  // Search for MAC CE for C-RNTI
  rnti_t crnti = lcid_crnti_lookahead(sdu, sdu_len);
  if (crnti != 0) { // 3GPP TS 38.321 Table 7.1-1: RNTI values, RNTI 0x0000: N/A
    // Replace the current UE by the UE identified by C-RNTI
    NR_UE_info_t *old_UE = find_connected_nr_ue(&mac->UE_info, crnti);
    if (!old_UE) {
      // The UE identified by C-RNTI no longer exists at the gNB
      // Let's abort the current RA, so the UE will trigger a new RA later but using RRCSetupRequest instead. A better
      // solution may be implemented
      LOG_W(NR_MAC, "No UE found with C-RNTI %04x, ignoring Msg3 to have UE come back with new RA attempt\n", UE->rnti);
      NR_UE_info_t *UE = remove_ra_ue(&mac->UE_info, rnti);
      if (UE)
        release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
      return;
    }
    // in case UE beam has changed
    old_UE->UE_beam_index = UE->UE_beam_index;
    // Reset UL failure for old UE
    nr_mac_reset_ul_failure(&old_UE->UE_sched_ctrl);
    // Reset HARQ processes
    reset_dl_harq_list(&old_UE->UE_sched_ctrl);
    reset_ul_harq_list(&old_UE->UE_sched_ctrl);

    // Only trigger RRCReconfiguration if UE is not performing RRCReestablishment
    // The RRCReconfiguration will be triggered by the RRCReestablishmentComplete
    // reconfigCellGroup: same-DU re-establishment, reestablish_rlc: cross-DU re-establishment
    if (!old_UE->reconfigCellGroup && !old_UE->reestablish_rlc) {
      LOG_I(NR_MAC, "Received UL_SCH_LCID_C_RNTI with C-RNTI 0x%04x, triggering RRC Reconfiguration\n", crnti);
      // Trigger RRCReconfiguration
      nr_mac_trigger_reconfiguration(mac, cell, old_UE, -1, -1);
      // we configure the UE using common search space with DCIX0 while waiting for a reconfiguration
      configure_UE_BWP(cell, scc, old_UE, false, NR_SearchSpace__searchSpaceType_PR_common, -1, -1);
    }
    NR_UE_info_t *UE = remove_ra_ue(&mac->UE_info, rnti);
    DevAssert(UE); // must exist, otherwise would not be here.
    release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
    LOG_A(NR_MAC, "%4d.%2d RA with C-RNTI %04x complete\n", frame, slot, crnti);

    // Decode the entire MAC PDU
    // It may have multiple MAC subPDUs, for example, a MAC subPDU with LCID 1 caring a RRCReestablishmentComplete
    *rlc_rnti = old_UE->rnti;
    nr_process_mac_pdu(mac, cell, old_UE, frame, slot, sdu, sdu_len, harq_pid, data_ind, num_data_ind);
    return;
  }

  if (cfra)
    return; // rest not relevant for CFRA

  // UE Contention Resolution Identity
  // Store the first 48 bits belonging to the uplink CCCH SDU within Msg3 to fill in Msg4
  // First byte corresponds to R/LCID MAC sub-header
  memcpy(ra->cont_res_id, &sdu[1], sizeof(uint8_t) * 6);

  // Decode MAC PDU
  *rlc_rnti = UE->rnti;
  nr_process_mac_pdu(mac, cell, UE, frame, slot, sdu, sdu_len, harq_pid, data_ind, num_data_ind);

  LOG_I(NR_MAC,
        "Activating scheduling %s for %s 0x%04x (state %s)\n",
        ra_type == RA_2_STEP ? "MsgB" : "Msg4",
        ra_type == RA_2_STEP ? "MsgB_rnti" : "TC_RNTI",
        ra_type == RA_2_STEP ? ra->MsgB_rnti : UE->rnti,
        nrra_text[ra->ra_state]);
  ra->ra_state = ra_type == RA_2_STEP ? nrRA_MsgB : nrRA_Msg4;
  LOG_D(NR_MAC, "TC_RNTI 0x%04x next RA state %s\n", UE->rnti, nrra_text[ra->ra_state]);
  return;
}

static void _nr_rx_sdu(gNB_MAC_INST *gNB_mac,
                       nr_cell_sched_t *cell,
                       const frame_t frameP,
                       const slot_t slotP,
                       const rnti_t rntiP,
                       uint8_t *sduP,
                       const uint32_t sdu_lenP,
                       const int8_t harq_pid,
                       const uint16_t timing_advance,
                       const uint8_t ul_cqi,
                       const uint16_t rssi,
                       nr_rlc_data_ind_t *data_ind,
                       int *num_data_ind,
                       rnti_t *rlc_rnti)
{
  const int current_rnti = rntiP;
  LOG_D(NR_MAC, "rx_sdu for rnti %04x\n", current_rnti);
  const int pusch_failure_thres = cell->radio_config.pusch.failure_thres;
  NR_UE_info_t *UE = find_connected_nr_ue(&gNB_mac->UE_info, current_rnti);
  if (UE) {
    NR_UE_sched_ctrl_t *UE_scheduling_control = &UE->UE_sched_ctrl;
    if (sduP)
      T(T_GNB_MAC_UL_PDU_WITH_DATA, T_INT(gNB_mac->Mod_id), T_INT( 0/*CC_idP*/),
        T_INT(rntiP), T_INT(frameP), T_INT(slotP), T_INT(harq_pid),
        T_BUFFER(sduP, sdu_lenP));

    UE->mac_stats.ul.total_bytes += sdu_lenP;
    LOG_D(NR_MAC, "[gNB %d][PUSCH %d] %d.%d Received ULSCH sdu from PHY (rnti %04x) ul_cqi %d TA %d sduP %p, rssi %d\n",
          gNB_mac->Mod_id,
          harq_pid,
          frameP,
          slotP,
          current_rnti,
          ul_cqi,
          timing_advance,
          sduP,
          rssi);
    if (harq_pid < 0) {
      LOG_E(NR_MAC, "UE %04x received ULSCH when feedback UL HARQ %d (unexpected ULSCH transmission)\n", rntiP, harq_pid);
      return;
    }

    // if not missed detection (10dB threshold for now)
    if (rssi > 0) {
      int txpower_calc = UE_scheduling_control->ul_harq_processes[harq_pid].sched_pusch.phr_txpower_calc;
      UE->mac_stats.deltaMCS = txpower_calc;
      UE->mac_stats.NPRB = UE_scheduling_control->ul_harq_processes[harq_pid].sched_pusch.rbSize;

      int pusch_snrx10 = ul_cqi * 5 - 640 - txpower_calc * 10; // TODO don't log T?
      if (ul_cqi != 0xff)
        nr_mac_pc_snr(&UE_scheduling_control->pusch_pc, pusch_snrx10, rssi);

      if (timing_advance != 0xffff)
        UE_scheduling_control->ta_update = timing_advance;

      const NR_sched_pusch_t *sched_pusch = &UE_scheduling_control->ul_harq_processes[harq_pid].sched_pusch;
      UNUSED(sched_pusch); // avoids warnings of unused sched_pusch when compiling without T
      T(T_GNB_MAC_PUSCH_POWER_CONTROL, T_INT(rntiP), T_INT(frameP), T_INT(slotP),
        T_INT(pusch_snrx10),
        T_INT(UE_scheduling_control->ph),
        T_INT(sched_pusch->tpc_pusch),
        T_INT(sched_pusch->tb_size),
        T_INT(txpower_calc),
        T_INT(sched_pusch->rbSize),
        T_INT(sched_pusch->mcs),
        T_INT(rssi));
    }

#if defined(ENABLE_MAC_PAYLOAD_DEBUG)

    LOG_I(NR_MAC, "Printing received UL MAC payload at gNB side: %d \n");
    for (uint32_t i = 0; i < sdu_lenP; i++) {
      // harq_process_ul_ue->a[i] = (unsigned char) rand();
      // printf("a[%d]=0x%02x\n",i,harq_process_ul_ue->a[i]);
      printf("%02x ", (unsigned char)sduP[i]);
    }
    printf("\n");

#endif

    if (sduP != NULL) {
      LOG_D(NR_MAC, "Received PDU at MAC gNB \n");
      UE->UE_sched_ctrl.pusch_consecutive_dtx_cnt = 0;
      UE_scheduling_control->sched_ul_bytes -= sdu_lenP;
      if (UE_scheduling_control->sched_ul_bytes < 0)
        UE_scheduling_control->sched_ul_bytes = 0;

      *rlc_rnti = UE->rnti;
      nr_process_mac_pdu(gNB_mac, cell, UE, frameP, slotP, sduP, sdu_lenP, harq_pid, data_ind, num_data_ind);
    } else {
      if (ul_cqi == 0xff || ul_cqi <= 128) {
        UE->UE_sched_ctrl.pusch_consecutive_dtx_cnt++;
        UE->mac_stats.ulsch_DTX++;
        nr_mac_signal_dtx(&UE_scheduling_control->pusch_pc);
      }

      if (!get_softmodem_params()->phy_test && UE->UE_sched_ctrl.pusch_consecutive_dtx_cnt >= pusch_failure_thres) {
        LOG_W(NR_MAC,
              "%4d.%2d UE %04x: Detected UL Failure on PUSCH after %d PUSCH DTX, stopping scheduling\n",
              frameP,
              slotP,
              UE->rnti,
              UE->UE_sched_ctrl.pusch_consecutive_dtx_cnt);
        nr_mac_trigger_ul_failure(&UE->UE_sched_ctrl, UE->current_UL_BWP.scs);
      }
    }
    handle_nr_ul_harq(cell, UE, current_rnti, harq_pid, sduP == NULL);
  } else {
    nr_rx_ra_sdu(gNB_mac, cell, frameP, slotP, current_rnti, sduP, sdu_lenP, harq_pid, timing_advance, ul_cqi, rssi, data_ind, num_data_ind, rlc_rnti);
  }
}

void nr_rx_sdu(const module_id_t gnb_mod_idP,
               const int CC_idP,
               const frame_t frameP,
               const slot_t slotP,
               const rnti_t rntiP,
               uint8_t *sduP,
               const uint32_t sdu_lenP,
               const int8_t harq_pid,
               const uint16_t timing_advance,
               const uint8_t ul_cqi,
               const uint16_t rssi)
{
  gNB_MAC_INST *gNB_mac = RC.nrmac[gnb_mod_idP];
  NR_SCHED_LOCK(&gNB_mac->sched_lock);
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(gNB_mac, CC_idP);
  nr_rlc_data_ind_t data_ind[MAX_NUM_DATA_IND];
  int num_data_ind = 0;
  rnti_t rlc_rnti = 0;
  start_meas(&cell->rx_ulsch_sdu);
  _nr_rx_sdu(gNB_mac, cell, frameP, slotP, rntiP, sduP, sdu_lenP, harq_pid, timing_advance, ul_cqi, rssi, data_ind, &num_data_ind, &rlc_rnti);
  stop_meas(&cell->rx_ulsch_sdu);
  NR_SCHED_UNLOCK(&gNB_mac->sched_lock);
  // RLC has its own lock: deliver outside sched_lock. data_ind[] points into sduP.
  if (num_data_ind > 0)
    nr_mac_rlc_data_ind(gNB_mac->Mod_id, rlc_rnti, true, data_ind, num_data_ind);
}

static uint32_t calc_power_complex(const int16_t *x, const int16_t *y, const uint32_t size)
{
  // Real part value
  int64_t sum_x = 0;
  int64_t sum_x2 = 0;
  for(int k = 0; k<size; k++) {
    sum_x = sum_x + x[k];
    sum_x2 = sum_x2 + x[k]*x[k];
  }
  uint32_t power_re = sum_x2/size - (sum_x/size)*(sum_x/size);

  // Imaginary part power
  int64_t sum_y = 0;
  int64_t sum_y2 = 0;
  for(int k = 0; k<size; k++) {
    sum_y = sum_y + y[k];
    sum_y2 = sum_y2 + y[k]*y[k];
  }
  uint32_t power_im = sum_y2/size - (sum_y/size)*(sum_y/size);

  return power_re+power_im;
}

static c16_t nr_h_times_w(c16_t h, char w)
{
  c16_t output;
    switch (w) {
      case '0': // 0
        output.r = 0;
        output.i = 0;
        break;
      case '1': // 1
        output.r = h.r;
        output.i = h.i;
        break;
      case 'n': // -1
        output.r = -h.r;
        output.i = -h.i;
        break;
      case 'j': // j
        output.r = -h.i;
        output.i = h.r;
        break;
      case 'o': // -j
        output.r = h.i;
        output.i = -h.r;
        break;
      default:
        AssertFatal(1==0,"Invalid precoder value %c\n", w);
    }
  return output;
}

static uint8_t get_max_tpmi(const NR_PUSCH_Config_t *pusch_Config,
                            const uint16_t num_ue_srs_ports,
                            const uint8_t *nrOfLayers,
                            int *additional_max_tpmi)
{
  uint8_t max_tpmi = 0;

  if (!pusch_Config
      || (pusch_Config->txConfig != NULL && *pusch_Config->txConfig == NR_PUSCH_Config__txConfig_nonCodebook)
      || num_ue_srs_ports == 1)
    return max_tpmi;

  long max_rank = *pusch_Config->maxRank;
  long *ul_FullPowerTransmission = pusch_Config->ext1 ? pusch_Config->ext1->ul_FullPowerTransmission_r16 : NULL;
  long *codebookSubset = pusch_Config->codebookSubset;

  if (num_ue_srs_ports == 2) {

    if (max_rank == 1) {
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        max_tpmi = 2;
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          max_tpmi = 1;
        } else {
          max_tpmi = 5;
        }
      }
    } else {
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        max_tpmi = *nrOfLayers == 1 ? 2 : 0;
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          max_tpmi = *nrOfLayers == 1 ? 1 : 0;
        } else {
          max_tpmi = *nrOfLayers == 1 ? 5 : 2;
        }
      }
    }

  } else if (num_ue_srs_ports == 4) {

    if (max_rank == 1) {
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          max_tpmi = 3;
          *additional_max_tpmi = 13;
        } else {
          max_tpmi = 15;
        }
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          max_tpmi = 3;
        } else if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent) {
          max_tpmi = 11;
        } else {
          max_tpmi = 27;
        }
      }
    } else {
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        if (max_rank == 2) {
          if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
            max_tpmi = *nrOfLayers == 1 ? 3 : 6;
            if (*nrOfLayers == 1) {
              *additional_max_tpmi = 13;
            }
          } else {
            max_tpmi = *nrOfLayers == 1 ? 15 : 13;
          }
        } else {
          if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
            switch (*nrOfLayers) {
              case 1:
                max_tpmi = 3;
                *additional_max_tpmi = 13;
                break;
              case 2:
                max_tpmi = 6;
                break;
              case 3:
                max_tpmi = 1;
                break;
              case 4:
                max_tpmi = 0;
                break;
              default:
                LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
            }
          } else {
            switch (*nrOfLayers) {
              case 1:
                max_tpmi = 15;
                break;
              case 2:
                max_tpmi = 13;
                break;
              case 3:
              case 4:
                max_tpmi = 2;
                break;
              default:
                LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
            }
          }
        }
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          switch (*nrOfLayers) {
            case 1:
              max_tpmi = 3;
              break;
            case 2:
              max_tpmi = 5;
              break;
            case 3:
            case 4:
              max_tpmi = 0;
              break;
            default:
              LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
          }
        } else if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent) {
          switch (*nrOfLayers) {
            case 1:
              max_tpmi = 11;
              break;
            case 2:
              max_tpmi = 13;
              break;
            case 3:
            case 4:
              max_tpmi = 2;
              break;
            default:
              LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
          }
        } else {
          switch (*nrOfLayers) {
            case 1:
              max_tpmi = 28;
              break;
            case 2:
              max_tpmi = 22;
              break;
            case 3:
              max_tpmi = 7;
              break;
            case 4:
              max_tpmi = 5;
              break;
            default:
              LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
          }
        }
      }
    }

  }

  return max_tpmi;
}

// TS 38.211 - Table 6.3.1.5-1: Precoding matrix W for single-layer transmission using two antenna ports, 'n' = -1 and 'o' = -j
const char table_38211_6_3_1_5_1[6][2][1] = {
    {{'1'}, {'0'}}, // tpmi 0
    {{'0'}, {'1'}}, // tpmi 1
    {{'1'}, {'1'}}, // tpmi 2
    {{'1'}, {'n'}}, // tpmi 3
    {{'1'}, {'j'}}, // tpmi 4
    {{'1'}, {'o'}}  // tpmi 5
};

// TS 38.211 - Table 6.3.1.5-2: Precoding matrix W for single-layer transmission using four antenna ports with transform precoding enabled, 'n' = -1 and 'o' = -j
const char table_38211_6_3_1_5_2[28][4][1] = {
    {{'1'}, {'0'}, {'0'}, {'0'}}, // tpmi 0
    {{'0'}, {'1'}, {'0'}, {'0'}}, // tpmi 1
    {{'0'}, {'0'}, {'1'}, {'0'}}, // tpmi 2
    {{'0'}, {'0'}, {'0'}, {'1'}}, // tpmi 3
    {{'1'}, {'0'}, {'1'}, {'0'}}, // tpmi 4
    {{'1'}, {'0'}, {'n'}, {'0'}}, // tpmi 5
    {{'1'}, {'0'}, {'j'}, {'0'}}, // tpmi 6
    {{'1'}, {'0'}, {'o'}, {'0'}}, // tpmi 7
    {{'0'}, {'1'}, {'0'}, {'1'}}, // tpmi 8
    {{'0'}, {'1'}, {'0'}, {'n'}}, // tpmi 9
    {{'0'}, {'1'}, {'0'}, {'j'}}, // tpmi 10
    {{'0'}, {'1'}, {'0'}, {'o'}}, // tpmi 11
    {{'1'}, {'1'}, {'1'}, {'n'}}, // tpmi 12
    {{'1'}, {'1'}, {'j'}, {'j'}}, // tpmi 13
    {{'1'}, {'1'}, {'n'}, {'1'}}, // tpmi 14
    {{'1'}, {'1'}, {'o'}, {'o'}}, // tpmi 15
    {{'1'}, {'j'}, {'1'}, {'j'}}, // tpmi 16
    {{'1'}, {'j'}, {'j'}, {'1'}}, // tpmi 17
    {{'1'}, {'j'}, {'n'}, {'o'}}, // tpmi 18
    {{'1'}, {'j'}, {'o'}, {'n'}}, // tpmi 19
    {{'1'}, {'n'}, {'1'}, {'1'}}, // tpmi 20
    {{'1'}, {'n'}, {'j'}, {'o'}}, // tpmi 21
    {{'1'}, {'n'}, {'n'}, {'n'}}, // tpmi 22
    {{'1'}, {'n'}, {'o'}, {'j'}}, // tpmi 23
    {{'1'}, {'o'}, {'1'}, {'o'}}, // tpmi 24
    {{'1'}, {'o'}, {'j'}, {'n'}}, // tpmi 25
    {{'1'}, {'o'}, {'n'}, {'j'}}, // tpmi 26
    {{'1'}, {'o'}, {'o'}, {'1'}}  // tpmi 27
};

// TS 38.211 - Table 6.3.1.5-3: Precoding matrix W for single-layer transmission using four antenna ports with transform precoding disabled, 'n' = -1 and 'o' = -j
const char table_38211_6_3_1_5_3[28][4][1] = {
    {{'1'}, {'0'}, {'0'}, {'0'}}, // tpmi 0
    {{'0'}, {'1'}, {'0'}, {'0'}}, // tpmi 1
    {{'0'}, {'0'}, {'1'}, {'0'}}, // tpmi 2
    {{'0'}, {'0'}, {'0'}, {'1'}}, // tpmi 3
    {{'1'}, {'0'}, {'1'}, {'0'}}, // tpmi 4
    {{'1'}, {'0'}, {'n'}, {'0'}}, // tpmi 5
    {{'1'}, {'0'}, {'j'}, {'0'}}, // tpmi 6
    {{'1'}, {'0'}, {'o'}, {'0'}}, // tpmi 7
    {{'0'}, {'1'}, {'0'}, {'1'}}, // tpmi 8
    {{'0'}, {'1'}, {'0'}, {'n'}}, // tpmi 9
    {{'0'}, {'1'}, {'0'}, {'j'}}, // tpmi 10
    {{'0'}, {'1'}, {'0'}, {'o'}}, // tpmi 11
    {{'1'}, {'1'}, {'1'}, {'1'}}, // tpmi 12
    {{'1'}, {'1'}, {'j'}, {'j'}}, // tpmi 13
    {{'1'}, {'1'}, {'n'}, {'n'}}, // tpmi 14
    {{'1'}, {'1'}, {'o'}, {'o'}}, // tpmi 15
    {{'1'}, {'j'}, {'1'}, {'j'}}, // tpmi 16
    {{'1'}, {'j'}, {'j'}, {'n'}}, // tpmi 17
    {{'1'}, {'j'}, {'n'}, {'o'}}, // tpmi 18
    {{'1'}, {'j'}, {'o'}, {'1'}}, // tpmi 19
    {{'1'}, {'n'}, {'1'}, {'n'}}, // tpmi 20
    {{'1'}, {'n'}, {'j'}, {'o'}}, // tpmi 21
    {{'1'}, {'n'}, {'n'}, {'1'}}, // tpmi 22
    {{'1'}, {'n'}, {'o'}, {'j'}}, // tpmi 23
    {{'1'}, {'o'}, {'1'}, {'o'}}, // tpmi 24
    {{'1'}, {'o'}, {'j'}, {'1'}}, // tpmi 25
    {{'1'}, {'o'}, {'n'}, {'j'}}, // tpmi 26
    {{'1'}, {'o'}, {'o'}, {'n'}}  // tpmi 27
};

// TS 38.211 - Table 6.3.1.5-4: Precoding matrix W for two-layer transmission using two antenna ports, 'n' = -1 and 'o' = -j
const char table_38211_6_3_1_5_4[3][2][2] = {
    {{'1', '0'}, {'0', '1'}}, // tpmi 0
    {{'1', '1'}, {'1', 'n'}}, // tpmi 1
    {{'1', '1'}, {'j', 'o'}}  // tpmi 2
};

// TS 38.211 - Table 6.3.1.5-5: Precoding matrix W for two-layer transmission using four antenna ports, 'n' = -1 and 'o' = -j
const char table_38211_6_3_1_5_5[22][4][2] = {
    {{'1', '0'}, {'0', '1'}, {'0', '0'}, {'0', '0'}}, // tpmi 0
    {{'1', '0'}, {'0', '0'}, {'0', '1'}, {'0', '0'}}, // tpmi 1
    {{'1', '0'}, {'0', '0'}, {'0', '0'}, {'0', '1'}}, // tpmi 2
    {{'0', '0'}, {'1', '0'}, {'0', '1'}, {'0', '0'}}, // tpmi 3
    {{'0', '0'}, {'1', '0'}, {'0', '0'}, {'0', '1'}}, // tpmi 4
    {{'0', '0'}, {'0', '0'}, {'1', '0'}, {'0', '1'}}, // tpmi 5
    {{'1', '0'}, {'0', '1'}, {'1', '0'}, {'0', 'o'}}, // tpmi 6
    {{'1', '0'}, {'0', '1'}, {'1', '0'}, {'0', 'j'}}, // tpmi 7
    {{'1', '0'}, {'0', '1'}, {'o', '0'}, {'0', '1'}}, // tpmi 8
    {{'1', '0'}, {'0', '1'}, {'o', '0'}, {'0', 'n'}}, // tpmi 9
    {{'1', '0'}, {'0', '1'}, {'n', '0'}, {'0', 'o'}}, // tpmi 10
    {{'1', '0'}, {'0', '1'}, {'n', '0'}, {'0', 'j'}}, // tpmi 11
    {{'1', '0'}, {'0', '1'}, {'j', '0'}, {'0', '1'}}, // tpmi 12
    {{'1', '0'}, {'0', '1'}, {'j', '0'}, {'0', 'n'}}, // tpmi 13
    {{'1', '1'}, {'1', '1'}, {'1', 'n'}, {'1', 'n'}}, // tpmi 14
    {{'1', '1'}, {'1', '1'}, {'j', 'o'}, {'j', 'o'}}, // tpmi 15
    {{'1', '1'}, {'j', 'j'}, {'1', 'n'}, {'j', 'o'}}, // tpmi 16
    {{'1', '1'}, {'j', 'j'}, {'j', 'o'}, {'n', '1'}}, // tpmi 17
    {{'1', '1'}, {'n', 'n'}, {'1', 'n'}, {'n', '1'}}, // tpmi 18
    {{'1', '1'}, {'n', 'n'}, {'j', 'o'}, {'o', 'j'}}, // tpmi 19
    {{'1', '1'}, {'o', 'o'}, {'1', 'n'}, {'o', 'j'}}, // tpmi 20
    {{'1', '1'}, {'o', 'o'}, {'j', 'o'}, {'1', 'n'}}  // tpmi 21
};

static void get_precoder_matrix_coef(char *w,
                                     const uint8_t ul_ri,
                                     const uint16_t num_ue_srs_ports,
                                     const long transform_precoding,
                                     const uint8_t tpmi,
                                     const uint8_t uI,
                                     int layer_idx)
{
  if (ul_ri == 0) {
    if (num_ue_srs_ports == 2) {
      *w = table_38211_6_3_1_5_1[tpmi][uI][layer_idx];
    } else {
      if (transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled) {
        *w = table_38211_6_3_1_5_2[tpmi][uI][layer_idx];
      } else {
        *w = table_38211_6_3_1_5_3[tpmi][uI][layer_idx];
      }
    }
  } else if (ul_ri == 1) {
    if (num_ue_srs_ports == 2) {
      *w = table_38211_6_3_1_5_4[tpmi][uI][layer_idx];
    } else {
      *w = table_38211_6_3_1_5_5[tpmi][uI][layer_idx];
    }
  } else {
    AssertFatal(1 == 0, "Function get_precoder_matrix_coef() does not support %i layers yet!\n", ul_ri + 1);
  }
}

static int nr_srs_tpmi_estimation(const NR_PUSCH_Config_t *pusch_Config,
                                  const long transform_precoding,
                                  const uint8_t *channel_matrix,
                                  const uint16_t num_gnb_antenna_elements,
                                  const uint16_t num_ue_srs_ports,
                                  const uint16_t num_prgs,
                                  const uint8_t ul_ri)
{
  if (ul_ri > 1) {
    LOG_D(NR_MAC, "TPMI computation for ul_ri %i is not implemented yet!\n", ul_ri);
    return 0;
  }

  uint8_t tpmi_sel = 0;
  const uint8_t nrOfLayers = ul_ri + 1;
  int16_t precoded_channel_matrix_re[num_prgs * num_gnb_antenna_elements];
  int16_t precoded_channel_matrix_im[num_prgs * num_gnb_antenna_elements];
  c16_t *channel_matrix16 = (c16_t *)channel_matrix;
  uint32_t max_precoded_signal_power = 0;
  int additional_max_tpmi = -1;
  char w;

  uint8_t max_tpmi = get_max_tpmi(pusch_Config, num_ue_srs_ports, &nrOfLayers, &additional_max_tpmi);
  uint8_t end_tpmi_loop = additional_max_tpmi > max_tpmi ? additional_max_tpmi : max_tpmi;

  //                      channel_matrix                          x   precoder_matrix
  // [ (gI=0,uI=0) (gI=0,uI=1) ... (gI=0,uI=num_ue_srs_ports-1) ] x   [uI=0]
  // [ (gI=1,uI=0) (gI=1,uI=1) ... (gI=1,uI=num_ue_srs_ports-1) ]     [uI=1]
  // [ (gI=2,uI=0) (gI=2,uI=1) ... (gI=2,uI=num_ue_srs_ports-1) ]     [uI=2]
  //                           ...                                     ...

  for (uint8_t tpmi = 0; tpmi <= end_tpmi_loop && end_tpmi_loop > 0; tpmi++) {
    if (tpmi > max_tpmi) {
      tpmi = end_tpmi_loop;
    }

    for (int pI = 0; pI < num_prgs; pI++) {
      for (int gI = 0; gI < num_gnb_antenna_elements; gI++) {
        uint16_t index_gI_pI = gI * num_prgs + pI;
        precoded_channel_matrix_re[index_gI_pI] = 0;
        precoded_channel_matrix_im[index_gI_pI] = 0;

        for (int uI = 0; uI < num_ue_srs_ports; uI++) {
          for (int layer_idx = 0; layer_idx < nrOfLayers; layer_idx++) {
            uint16_t index = uI * num_gnb_antenna_elements * num_prgs + index_gI_pI;
            get_precoder_matrix_coef(&w, ul_ri, num_ue_srs_ports, transform_precoding, tpmi, uI, layer_idx);
            c16_t h_times_w = nr_h_times_w(channel_matrix16[index], w);

            precoded_channel_matrix_re[index_gI_pI] += h_times_w.r;
            precoded_channel_matrix_im[index_gI_pI] += h_times_w.i;

#ifdef SRS_IND_DEBUG
            LOG_I(NR_MAC, "(pI %i, gI %i,  uI %i, layer_idx %i) w = %c, channel_matrix --> real %i, imag %i\n",
                  pI, gI, uI, layer_idx, w, channel_matrix16[index].r, channel_matrix16[index].i);
#endif
          }
        }

#ifdef SRS_IND_DEBUG
        LOG_I(NR_MAC, "(pI %i, gI %i) precoded_channel_coef --> real %i, imag %i\n",
              pI, gI, precoded_channel_matrix_re[index_gI_pI], precoded_channel_matrix_im[index_gI_pI]);
#endif
      }
    }

    uint32_t precoded_signal_power = calc_power_complex(precoded_channel_matrix_re,
                                                        precoded_channel_matrix_im,
                                                        num_prgs * num_gnb_antenna_elements);

#ifdef SRS_IND_DEBUG
    LOG_I(NR_MAC, "(tpmi %i) precoded_signal_power = %i\n", tpmi, precoded_signal_power);
#endif

    if (precoded_signal_power > max_precoded_signal_power) {
      max_precoded_signal_power = precoded_signal_power;
      tpmi_sel = tpmi;
    }
  }

  return tpmi_sel;
}

void handle_nr_srs_measurements(const module_id_t module_id,
                                const int cell_id,
                                const frame_t frame,
                                const slot_t slot,
                                nfapi_nr_srs_indication_pdu_t *srs_ind)
{
  gNB_MAC_INST *nrmac = RC.nrmac[module_id];
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(nrmac, cell_id);
  LOG_D(NR_MAC, "(%d.%d) Received SRS indication for UE %04x\n", frame, slot, srs_ind->rnti);
  if (srs_ind->report_type == 0) {
    //SCF 222.10.04 Table 3-129 Report type = 0 means a null report, we can skip unpacking it
    return;
  }

  if (srs_ind->timing_advance_offset == 0xFFFF) {
    LOG_W(NR_MAC, "Invalid timing advance offset for RNTI %04x\n", srs_ind->rnti);
    return;
  }
  NR_SCHED_LOCK(&nrmac->sched_lock);

#ifdef SRS_IND_DEBUG
  LOG_I(NR_MAC, "frame = %i\n", frame);
  LOG_I(NR_MAC, "slot = %i\n", slot);
  LOG_I(NR_MAC, "srs_ind->rnti = %04x\n", srs_ind->rnti);
  LOG_I(NR_MAC, "srs_ind->timing_advance_offset = %i\n", srs_ind->timing_advance_offset);
  LOG_I(NR_MAC, "srs_ind->timing_advance_offset_nsec = %i\n", srs_ind->timing_advance_offset_nsec);
  LOG_I(NR_MAC, "srs_ind->srs_usage = %i\n", srs_ind->srs_usage);
  LOG_I(NR_MAC, "srs_ind->report_type = %i\n", srs_ind->report_type);
#endif

  NR_UE_info_t *UE = find_connected_nr_ue(&nrmac->UE_info, srs_ind->rnti);
  if (!UE) {
    LOG_W(NR_MAC, "Could not find UE for RNTI %04x\n", srs_ind->rnti);
    NR_SCHED_UNLOCK(&nrmac->sched_lock);
    return;
  }

  NR_mac_stats_t *stats = &UE->mac_stats;
  nfapi_srs_report_tlv_t *report_tlv = &srs_ind->report_tlv;

  switch (srs_ind->srs_usage) {
    case NR_SRS_ResourceSet__usage_beamManagement: {
      nfapi_nr_srs_beamforming_report_t nr_srs_bf_report;
      unpack_nr_srs_beamforming_report(report_tlv->value,
                                       report_tlv->length,
                                       &nr_srs_bf_report,
                                       sizeof(nfapi_nr_srs_beamforming_report_t));

      if (nr_srs_bf_report.wide_band_snr == 0xFF) {
        LOG_W(NR_MAC, "Invalid wide_band_snr for RNTI %04x\n", srs_ind->rnti);
        NR_SCHED_UNLOCK(&nrmac->sched_lock);
        return;
      }

      int wide_band_snr_dB = (nr_srs_bf_report.wide_band_snr >> 1) - 64;

#ifdef SRS_IND_DEBUG
      LOG_I(NR_MAC, "nr_srs_bf_report.prg_size = %i\n", nr_srs_bf_report.prg_size);
      LOG_I(NR_MAC, "nr_srs_bf_report.num_symbols = %i\n", nr_srs_bf_report.num_symbols);
      LOG_I(NR_MAC, "nr_srs_bf_report.wide_band_snr = %i (%i dB)\n", nr_srs_bf_report.wide_band_snr, wide_band_snr_dB);
      LOG_I(NR_MAC, "nr_srs_bf_report.num_reported_symbols = %i\n", nr_srs_bf_report.num_reported_symbols);
      LOG_I(NR_MAC, "nr_srs_bf_report.reported_symbol_list[0].num_prgs = %i\n", nr_srs_bf_report.reported_symbol_list[0].num_prgs);
      for (int prg_idx = 0; prg_idx < nr_srs_bf_report.reported_symbol_list[0].num_prgs; prg_idx++) {
        LOG_I(NR_MAC,
              "nr_srs_bf_report.reported_symbol_list[0].prg_list[%3i].rb_snr = %i (%i dB)\n",
              prg_idx,
              nr_srs_bf_report.reported_symbol_list[0].prg_list[prg_idx].rb_snr,
              (nr_srs_bf_report.reported_symbol_list[0].prg_list[prg_idx].rb_snr >> 1) - 64);
      }
#endif

      sprintf(stats->srs_stats, "UL-SNR %i dB", wide_band_snr_dB);

      const int ul_prbblack_SNR_threshold = cell->radio_config.ul_prbblack_SNR_threshold;
      uint16_t *ulprbbl = cell->ulprbbl;

      uint16_t num_rbs = nr_srs_bf_report.prg_size * nr_srs_bf_report.reported_symbol_list[0].num_prgs;
      memset(ulprbbl, 0, num_rbs * sizeof(uint16_t));
      for (int rb = 0; rb < num_rbs; rb++) {
        int snr = (nr_srs_bf_report.reported_symbol_list[0].prg_list[rb / nr_srs_bf_report.prg_size].rb_snr >> 1) - 64;
        if (snr < wide_band_snr_dB - ul_prbblack_SNR_threshold) {
          ulprbbl[rb] = 0x3FFF; // all symbols taken
        }
        LOG_D(NR_MAC, "ulprbbl[%3i] = 0x%x\n", rb, ulprbbl[rb]);
      }

      break;
    }

    case NR_SRS_ResourceSet__usage_codebook: {
      nfapi_nr_srs_normalized_channel_iq_matrix_t nr_srs_channel_iq_matrix;
      unpack_nr_srs_normalized_channel_iq_matrix(report_tlv->value,
                                                 report_tlv->length,
                                                 &nr_srs_channel_iq_matrix,
                                                 sizeof(nfapi_nr_srs_normalized_channel_iq_matrix_t));

#ifdef SRS_IND_DEBUG
      LOG_I(NR_MAC, "nr_srs_channel_iq_matrix.normalized_iq_representation = %i\n", nr_srs_channel_iq_matrix.normalized_iq_representation);
      LOG_I(NR_MAC, "nr_srs_channel_iq_matrix.num_gnb_antenna_elements = %i\n", nr_srs_channel_iq_matrix.num_gnb_antenna_elements);
      LOG_I(NR_MAC, "nr_srs_channel_iq_matrix.num_ue_srs_ports = %i\n", nr_srs_channel_iq_matrix.num_ue_srs_ports);
      LOG_I(NR_MAC, "nr_srs_channel_iq_matrix.prg_size = %i\n", nr_srs_channel_iq_matrix.prg_size);
      LOG_I(NR_MAC, "nr_srs_channel_iq_matrix.num_prgs = %i\n", nr_srs_channel_iq_matrix.num_prgs);
      c16_t *channel_matrix16 = (c16_t *)nr_srs_channel_iq_matrix.channel_matrix;
      c8_t *channel_matrix8 = (c8_t *)nr_srs_channel_iq_matrix.channel_matrix;
      for (int uI = 0; uI < nr_srs_channel_iq_matrix.num_ue_srs_ports; uI++) {
        for (int gI = 0; gI < nr_srs_channel_iq_matrix.num_gnb_antenna_elements; gI++) {
          for (int pI = 0; pI < nr_srs_channel_iq_matrix.num_prgs; pI++) {
            uint32_t index = (uint32_t)uI * nr_srs_channel_iq_matrix.num_gnb_antenna_elements * nr_srs_channel_iq_matrix.num_prgs + gI * nr_srs_channel_iq_matrix.num_prgs + pI;
            LOG_I(NR_MAC,
                  "(uI %i, gI %i, pI %i) channel_matrix --> real %i, imag %i\n",
                  uI,
                  gI,
                  pI,
                  nr_srs_channel_iq_matrix.normalized_iq_representation == 0 ? channel_matrix8[index].r : channel_matrix16[index].r,
                  nr_srs_channel_iq_matrix.normalized_iq_representation == 0 ? channel_matrix8[index].i : channel_matrix16[index].i);
          }
        }
      }
#endif

      NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
      NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;
      sched_ctrl->srs_feedback.sri = NR_SRS_SRI_0;

      start_meas(&cell->nr_srs_ri_computation_timer);
      nr_srs_ri_computation(&nr_srs_channel_iq_matrix, current_BWP, &sched_ctrl->srs_feedback.ul_ri);
      stop_meas(&cell->nr_srs_ri_computation_timer);

      start_meas(&cell->nr_srs_tpmi_computation_timer);
      sched_ctrl->srs_feedback.tpmi = nr_srs_tpmi_estimation(current_BWP->pusch_Config,
                                                             current_BWP->transform_precoding,
                                                             nr_srs_channel_iq_matrix.channel_matrix,
                                                             nr_srs_channel_iq_matrix.num_gnb_antenna_elements,
                                                             nr_srs_channel_iq_matrix.num_ue_srs_ports,
                                                             nr_srs_channel_iq_matrix.num_prgs,
                                                             sched_ctrl->srs_feedback.ul_ri);
      stop_meas(&cell->nr_srs_tpmi_computation_timer);

      sprintf(stats->srs_stats, "UL-RI %d, TPMI %d", sched_ctrl->srs_feedback.ul_ri + 1, sched_ctrl->srs_feedback.tpmi);

      break;
    }

    case NR_SRS_ResourceSet__usage_nonCodebook:
    case NR_SRS_ResourceSet__usage_antennaSwitching:
      LOG_W(NR_MAC, "MAC procedures for this SRS usage are not implemented yet!\n");
      break;

    // Ignore Positioning usage here: handled in handle_nr_srs_toa_vendor_ext_measurements
    case NFAPI_NR_SRS_POSITIONING:
      break;

    default:
      AssertFatal(1 == 0, "Invalid SRS usage\n");
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
}

static int32_t compute_k_value_ul_tdoa(int mu, int16_t ta_offset_nsec, uint32_t trp_id)
{
  int32_t Tc_inv = 4096 * 480000;
  int32_t T_inv = Tc_inv >> mu;
  int64_t T_ns_inv = 1000000000;
  int64_t T_ns_inv_by_2 = T_ns_inv >> 1;
  int32_t k_offset = 985024 >> mu;
  int32_t k_max = (1970048 >> mu) + 1;

  int32_t k_value;
  if (ta_offset_nsec != (int16_t)0x8000) {
    int64_t num = (int64_t)ta_offset_nsec * T_inv;
    if (num >= 0)
      k_value = (int32_t)((num + T_ns_inv_by_2) / T_ns_inv) + k_offset;
    else
      k_value = (int32_t)((num - T_ns_inv_by_2) / T_ns_inv) + k_offset;

    if (k_value < 0)
      k_value = 0;
    if (k_value > k_max)
      k_value = k_max;

    LOG_I(NR_MAC, "TRP %u, ToA  %d, mu %d, k value %d\n", trp_id, ta_offset_nsec, mu, k_value);
  } else {
    k_value = k_max;
    LOG_I(NR_MAC, "Invalid Measurement: TRP %u, ToA %d, k value %d\n", trp_id, ta_offset_nsec, k_value);
  }
  return k_value;
}

static f1ap_pos_measurement_result_list_t generate_pos_measurement_result(const f1ap_pos_measurement_quantities_t *meas_q,
                                                                          const uint8_t num_trps,
                                                                          const uint32_t *trp_ids_list,
                                                                          int mu,
                                                                          const int16_t *ta_offset_nsec,
                                                                          const frame_t frame,
                                                                          const slot_t slot)
{
  f1ap_pos_measurement_result_list_t meas_result_list = {0};
  uint32_t q_len = meas_q->pos_measurement_quantities_length;
  f1ap_pos_measurement_quantities_item_t *q_item = meas_q->pos_measurement_quantities_item;

  meas_result_list.pos_measurement_result_list_length = num_trps;
  meas_result_list.pos_measurement_result_list_item =
      calloc_or_fail(num_trps, sizeof(*meas_result_list.pos_measurement_result_list_item));
  f1ap_pos_measurement_result_list_item_t *res_item = meas_result_list.pos_measurement_result_list_item;

  // check for trp_id and fill the measurements
  for (int i = 0; i < num_trps; i++) {
    res_item[i].trp_id = trp_ids_list[i];
    f1ap_pos_measurement_result_t *m_res = &res_item[i].pos_measurement_result;
    m_res->pos_measurement_result_item = calloc_or_fail(q_len, sizeof(*m_res->pos_measurement_result_item));
    m_res->pos_measurement_result_item_length = q_len;
    f1ap_pos_measurement_result_item_t *m_res_item = m_res->pos_measurement_result_item;
    for (int j = 0; j < q_len; j++) {
      f1ap_measured_results_value_t *m_res_value = &m_res_item[j].measured_results_value;
      switch (q_item[j].pos_measurement_type) {
        case F1AP_POSMEASUREMENTTYPE_UL_RTOA:
          m_res_value->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_RTOA;
          f1ap_ul_rtoa_measurement_item_t *ul_rtoa = &m_res_value->choice.ul_rtoa.ul_rtoa_measurement_item;

          int32_t k_value = compute_k_value_ul_tdoa(mu, ta_offset_nsec[i], trp_ids_list[i]);

          switch (mu) {
            case 0:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K0;
              ul_rtoa->choice.k0 = k_value;
              break;
            case 1:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K1;
              ul_rtoa->choice.k1 = k_value;
              break;
            case 2:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K2;
              ul_rtoa->choice.k2 = k_value;
              break;
            case 3:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K3;
              ul_rtoa->choice.k3 = k_value;
              break;
            case 4:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K4;
              ul_rtoa->choice.k4 = k_value;
              break;
            case 5:
              ul_rtoa->present = F1AP_ULRTOAMEAS_PR_K5;
              ul_rtoa->choice.k5 = k_value;
              break;
            default:
              break;
          }
          break;
        default:
          AssertFatal(false, "Unsupported/Illegal Measurement Type\n");
          break;
      }

      f1ap_time_stamp_t *time_stamp = &m_res_item[j].time_stamp;
      // System Frame Number
      time_stamp->system_frame_number = frame;
      // Slot number
      f1ap_time_stamp_slot_index_t *slot_index = &time_stamp->slot_index;
      switch (mu) {
        case 0:
          slot_index->present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_15;
          slot_index->choice.scs_15 = slot;
          break;
        case 1:
          slot_index->present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_30;
          slot_index->choice.scs_30 = slot;
          break;
        case 2:
          slot_index->present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_60;
          slot_index->choice.scs_60 = slot;
          break;
        case 3:
          slot_index->present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_120;
          slot_index->choice.scs_120 = slot;
          break;
        default:
          AssertFatal(false, "Illegal SCS\n");
          break;
      }
    }
  }
  return meas_result_list;
}

void handle_nr_srs_toa_vendor_ext_measurements(const module_id_t module_id,
                                               const frame_t frame,
                                               const slot_t slot,
                                               const uint8_t num_ta,
                                               const int16_t *ta_offset_nsec,
                                               const rnti_t rnti)
{
  gNB_MAC_INST *mac = RC.nrmac[module_id];
  // Return if there is no active measurement request from LMF
  if (!mac->pos_meas_info.active) {
    return;
  }
  LOG_I(NR_MAC, "Fill Positioning Measurement Response\n");
  f1ap_positioning_measurement_req_t *req = &mac->pos_meas_info.meas_req;
  f1ap_positioning_measurement_resp_t resp = {.transaction_id = req->transaction_id,
                                              .lmf_measurement_id = req->lmf_measurement_id,
                                              .ran_measurement_id = req->ran_measurement_id};
  f1ap_srs_configuration_t *srs_configuration = req->srs_configuration;
  f1ap_srs_carrier_list_t *srs_carrier_list = &srs_configuration->srs_carrier_list;
  // We dont use carrier aggregation, hence a single srs_carrier_list
  f1ap_srs_carrier_list_item_t *srs_carrier_list_item = &srs_carrier_list->srs_carrier_list_item[0];
  f1ap_active_ul_bwp_t *active_ul_bwp = &srs_carrier_list_item->active_ul_bwp;
  f1ap_subcarrier_spacing_pr subcarrier_spacing = active_ul_bwp->subcarrier_spacing;

  int mu;
  switch (subcarrier_spacing) {
    case F1AP_SUBCARRIER_SPACING_15KHZ:
      mu = 0;
      break;
    case F1AP_SUBCARRIER_SPACING_30KHZ:
      mu = 1;
      break;
    case F1AP_SUBCARRIER_SPACING_60KHZ:
      mu = 2;
      break;
    case F1AP_SUBCARRIER_SPACING_120KHZ:
      mu = 3;
      break;
    default:
      LOG_E(NR_MAC, "unsupported numerology\n");
      return;
      break;
  }

  positioning_config_t *positioning_config = mac->positioning_config;
  if (positioning_config == NULL) {
    LOG_E(NR_MAC, "No TRPs configured for positioning in the configuration file\n");
    f1ap_positioning_measurement_failure_t fail = {.transaction_id = req->transaction_id,
                                                   .lmf_measurement_id = req->lmf_measurement_id,
                                                   .ran_measurement_id = req->ran_measurement_id};
    fail.cause = F1AP_CAUSE_RADIO_NETWORK;
    mac->mac_rrc.positioning_measurement_failure(&fail);
    // since we suppport 1 measurement report per positioning request, we deactive the UE after sending failure message
    rm_pos_act_ue_context(mac, rnti);
    mac->pos_meas_info.active = false;
    return;
  }
  const f1ap_trp_measurement_request_list_t *req_list = &req->trp_measurement_request_list;
  uint32_t req_list_len = req_list->trp_measurement_request_list_length;
  f1ap_trp_measurement_request_item_t *req_item = req_list->trp_measurement_request_item;
  uint32_t trp_ids_list[MAX_NUM_MEASURE_TRPS] = {0};
  uint8_t num_trps = 0;

  // Count the number of relavent TRPs
  for (int i = 0; i < req_list_len; i++) {
    for (int j = 0; j < positioning_config->num_trp; j++) {
      if (req_item[i].tRPID == positioning_config->trps[j].id) {
        if (num_trps < MAX_NUM_MEASURE_TRPS) {
          trp_ids_list[num_trps] = req_item[i].tRPID;
          num_trps++;
        }
        break;
      }
    }
  }

  if (num_ta != num_trps) {
    LOG_E(NR_MAC, "Number of TRPs doesn't match with number of ToAs\n");
    f1ap_positioning_measurement_failure_t fail = {.transaction_id = req->transaction_id,
                                                   .lmf_measurement_id = req->lmf_measurement_id,
                                                   .ran_measurement_id = req->ran_measurement_id};
    fail.cause = F1AP_CAUSE_RADIO_NETWORK;
    mac->mac_rrc.positioning_measurement_failure(&fail);
    // since we suppport 1 measurement report per positioning request, we deactive the UE after sending failure message
    rm_pos_act_ue_context(mac, rnti);
    mac->pos_meas_info.active = false;
    return;
  }

  resp.pos_measurement_result_list = calloc_or_fail(1, sizeof(*resp.pos_measurement_result_list));
  *resp.pos_measurement_result_list =
      generate_pos_measurement_result(&req->pos_measurement_quantities, num_trps, trp_ids_list, mu, ta_offset_nsec, frame, slot);
  mac->mac_rrc.positioning_measurement_response(&resp);
  free_positioning_measurement_resp(&resp);
  // We suppport 1 measurement report per positioning request (no periodic measurement report support)
  // We deactive the UE after sending measurement report
  rm_pos_act_ue_context(mac, rnti);
  mac->pos_meas_info.active = false;
}

void update_ul_ue_R_Qm(int mcs, int mcs_table, const NR_PUSCH_Config_t *pusch_Config, uint16_t *R, uint8_t *Qm)
{
  *R = nr_get_code_rate_ul(mcs, mcs_table);
  *Qm = nr_get_Qm_ul(mcs, mcs_table);

  if (pusch_Config && pusch_Config->tp_pi2BPSK && ((mcs_table == 3 && mcs < 2) || (mcs_table == 4 && mcs < 6))) {
    *R >>= 1;
    *Qm <<= 1;
  }
}

static int verify_aperiodic_srs(nr_cell_sched_t *cell, int slot, int k2, NR_timer_t *aperiodic_srs, NR_UE_info_t *UE)
{
  NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;
  if (cell->radio_config.do_SRS != APERIODIC_SRS || current_BWP->dci_format == NR_UL_DCI_FORMAT_0_0 || !current_BWP->srs_Config)
    return 0;

  const frame_structure_t *fs = &cell->frame_structure;
  const int slot_period = slot % fs->numb_slots_period;
  const tdd_bitmap_t *bm = &fs->period_cfg.tdd_slot_bitmap[slot_period];
  // we schedule SRS only in full slots
  if (bm->slot_type == TDD_NR_MIXED_SLOT)
    return 0;

  // finally we check if the aperiodic SRS timer has expired
  if (nr_timer_expired(aperiodic_srs)) {
    NR_sched_srs_t *sched_srs = &UE->sched_srs;
    int offset = sched_srs->aperiodic_sched.aperiodic_slotOffset ? *sched_srs->aperiodic_sched.aperiodic_slotOffset : 0;
    if (offset == k2)
      return sched_srs->aperiodic_sched.aperiodic_ResourceTrigger;
  }
  return 0;
}

/* Check retx feasibility against a new TDA.
 * Refits rbSize via nr_find_nb_rb so the new TDA preserves TBS.
 * Returns the new rbSize needed (>0), or 0 if infeasible. */
uint16_t check_ul_retx_feasibility(const nr_ul_candidate_t *cand,
                                   int tda,
                                   const NR_tda_info_t *tda_info,
                                   const NR_ServingCellConfigCommon_t *scc,
                                   uint16_t max_rbSize)
{
  NR_UE_sched_ctrl_t *sched_ctrl = &cand->UE->UE_sched_ctrl;
  NR_UE_UL_BWP_t *current_BWP = &cand->UE->current_UL_BWP;
  const NR_sched_pusch_t *retInfo = &sched_ctrl->ul_harq_processes[cand->retx_harq_pid].sched_pusch;

  NR_pusch_dmrs_t dmrs_info = get_ul_dmrs_params(scc,
                                                 current_BWP,
                                                 tda_info,
                                                 retInfo->nrOfLayers,
                                                 retInfo->dmrs_info.dmrs_ports,
                                                 retInfo->dmrs_info.num_dmrs_cdm_grps_no_data);
  uint32_t new_tbs;
  uint16_t new_rbSize;
  bool ok = nr_find_nb_rb(retInfo->Qm,
                          retInfo->R,
                          current_BWP->transform_precoding,
                          retInfo->nrOfLayers,
                          tda_info->nrOfSymbols,
                          dmrs_info.N_PRB_DMRS * dmrs_info.num_dmrs_symb,
                          retInfo->tb_size,
                          1,
                          max_rbSize,
                          &new_tbs,
                          &new_rbSize);
  if (!ok || new_tbs != retInfo->tb_size) {
    LOG_D(NR_MAC, "[UE %04x] retx TDA change infeasible: new TBS %d != old TBS %d\n", cand->UE->rnti, new_tbs, retInfo->tb_size);
    return 0;
  }
  LOG_D(NR_MAC,
        "retx TDA change %d->%d feasible: rbSize %d->%d (TBS %d preserved)\n",
        retInfo->time_domain_allocation,
        tda,
        retInfo->rbSize,
        new_rbSize,
        retInfo->tb_size);
  return new_rbSize;
}

/* Apply a retransmission allocation. Builds NR_sched_pusch_t from cand+alloc,
 * calls post_process_ulsch, marks VRB map.
 * Retx feasibility was validated by ul_tda_select (retx_rbSize set on cand).
 * RBs + CCE were validated by the policy via commit_ul_alloc.
 * Returns rbSize used. */
static int apply_ul_retransmission(gNB_MAC_INST *nrmac,
                                   nr_cell_sched_t *cell,
                                   post_process_pusch_t *pp_pusch,
                                   const nr_ul_candidate_t *cand,
                                   const NR_ServingCellConfigCommon_t *scc,
                                   int tda,
                                   const NR_tda_info_t *tda_info,
                                   uint16_t *rballoc_mask,
                                   bwp_info_t bi,
                                   int sched_frame,
                                   int sched_slot)
{
  NR_UE_info_t *UE = cand->UE;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;

  int harq_pid = cand->retx_harq_pid;
  const NR_sched_pusch_t *retInfo = &sched_ctrl->ul_harq_processes[harq_pid].sched_pusch;
  NR_sched_pusch_t new_sched = *retInfo;
  const uint8_t nrOfLayers = retInfo->nrOfLayers;

  new_sched.frame = sched_frame;
  new_sched.slot = sched_slot;
  new_sched.bwp_info = bi;
  DevAssert(new_sched.ul_harq_pid == harq_pid);

  bool reuse_tda = (retInfo->time_domain_allocation == tda && nrOfLayers == retInfo->nrOfLayers);
  if (reuse_tda) {
    new_sched.rbStart = cand->sched_pusch.rbStart;
  } else {
    /* TDA changed vs original retx: recompute DMRS and TB size for new TDA */
    NR_pusch_dmrs_t dmrs_info = get_ul_dmrs_params(scc,
                                                   current_BWP,
                                                   tda_info,
                                                   nrOfLayers,
                                                   retInfo->dmrs_info.dmrs_ports,
                                                   retInfo->dmrs_info.num_dmrs_cdm_grps_no_data);
    new_sched.rbSize = check_sc_fdma_rbsize(current_BWP->transform_precoding, cand->sched_pusch.rbSize);
    new_sched.rbStart = cand->sched_pusch.rbStart;
    new_sched.tb_size = nr_compute_tbs(retInfo->Qm,
                                       retInfo->R,
                                       new_sched.rbSize,
                                       tda_info->nrOfSymbols,
                                       dmrs_info.N_PRB_DMRS * dmrs_info.num_dmrs_symb,
                                       0,
                                       0,
                                       nrOfLayers)
                        >> 3;
    new_sched.time_domain_allocation = tda;
    new_sched.tda_info = *tda_info;
    new_sched.dmrs_info = dmrs_info;
  }

  DevAssert(new_sched.time_domain_allocation == tda);
  post_process_ulsch(nrmac, cell, pp_pusch, UE, &new_sched, 0);

  const uint16_t retx_slbitmap = SL_to_bitmap(new_sched.tda_info.startSymbolIndex, new_sched.tda_info.nrOfSymbols);
  for (int rb = 0; rb < new_sched.rbSize; rb++)
    rballoc_mask[new_sched.rbStart + bi.bwpStart + rb] |= retx_slbitmap;

  LOG_D(NR_MAC,
        "%4d.%2d Allocate UL retransmission RNTI %04x sched %4d.%2d (%d RBs)\n",
        pp_pusch->frame,
        pp_pusch->slot,
        UE->rnti,
        new_sched.frame,
        new_sched.slot,
        new_sched.rbSize);

  return new_sched.rbSize;
}

/* Apply a new-transmission allocation. Starts from cand->sched_pusch (mcs, rbStart,
 * rbSize, nrOfLayers already set by pipeline stages), fills remaining fields (R, Qm,
 * tb_size, dmrs_info, bwp_info), calls post_process_ulsch, marks VRB map.
 * CCE and PDCCH VRB map are already handled by commit_ul_alloc.
 * Returns rbSize used. */
static int apply_ul_new_transmission(gNB_MAC_INST *nrmac,
                                     nr_cell_sched_t *cell,
                                     post_process_pusch_t *pp_pusch,
                                     const nr_ul_candidate_t *cand,
                                     const NR_ServingCellConfigCommon_t *scc,
                                     int tda,
                                     const NR_tda_info_t *tda_info,
                                     uint16_t *rballoc_mask,
                                     uint16_t slbitmap,
                                     bwp_info_t bi,
                                     int sched_frame,
                                     int sched_slot)
{
  NR_UE_info_t *UE = cand->UE;
  NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;

  /* Start from candidate's sched_pusch (mcs, rbStart, rbSize, nrOfLayers, tpmi,
   * time_domain_allocation, tda_info already set by pipeline stages).
   * Fill remaining dispatch fields: R, Qm, tb_size, dmrs_info, bwp_info. */
  NR_sched_pusch_t sched = cand->sched_pusch;
  sched.rbSize = check_sc_fdma_rbsize(current_BWP->transform_precoding, sched.rbSize);
  sched.frame = sched_frame;
  sched.slot = sched_slot;
  sched.ul_harq_pid = -1;
  sched.time_domain_allocation = tda;
  sched.tda_info = *tda_info;
  sched.dmrs_info = get_ul_dmrs_params(scc,
                                       current_BWP,
                                       tda_info,
                                       sched.nrOfLayers,
                                       cand->sched_pusch.dmrs_info.dmrs_ports,
                                       cand->sched_pusch.dmrs_info.num_dmrs_cdm_grps_no_data);
  sched.bwp_info = bi;

  // Map antenna ports for this UE
  sched.ant_port_idx.numSpatialStreamIndices = cell->radio_config.pusch_AntennaPorts;
  const int start_stream_idx = cand->alloc_beam_idx * cell->radio_config.pusch_AntennaPorts;
  for (int i = 0; i < cell->radio_config.pusch_AntennaPorts; i++)
    sched.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[start_stream_idx + i];
  sched.dci_ant_idx = cand->alloc_dci_beam_idx;

  update_ul_ue_R_Qm(sched.mcs, current_BWP->mcs_table, current_BWP->pusch_Config, &sched.R, &sched.Qm);
  sched.tb_size = nr_compute_tbs(sched.Qm,
                                 sched.R,
                                 sched.rbSize,
                                 sched.tda_info.nrOfSymbols,
                                 sched.dmrs_info.N_PRB_DMRS * sched.dmrs_info.num_dmrs_symb,
                                 0,
                                 0,
                                 sched.nrOfLayers)
                  >> 3;

  post_process_ulsch(nrmac, cell, pp_pusch, UE, &sched, cand->sched_srs);

  for (int rb = 0; rb < sched.rbSize; rb++)
    rballoc_mask[sched.rbStart + bi.bwpStart + rb] |= slbitmap;

  return sched.rbSize;
}

/* ---- PF weight utility (used by default beam_select and rb_alloc policies) ---- */

float ul_pf_weight(int mcs, int mcs_table, int nrOfLayers, float avg_throughput)
{
  uint8_t Qm = nr_get_Qm_ul(mcs, mcs_table);
  uint16_t R = nr_get_code_rate_ul(mcs, mcs_table);
  uint32_t tbs = nr_compute_tbs(Qm, R, 1, 10, 0, 0, 0, nrOfLayers) >> 3;
  return (float)tbs / fmaxf(avg_throughput, 1e-9f);
}

static int compare_ul_beam_idx(const void *a, const void *b)
{
  return ((const nr_ul_candidate_t *)a)->alloc_beam_idx - ((const nr_ul_candidate_t *)b)->alloc_beam_idx;
}

static int nr_ul_schedule(gNB_MAC_INST *nrmac,
                          nr_cell_sched_t *cell,
                          post_process_pusch_t *pp_pusch,
                          nr_ul_candidate_t *candidates,
                          int n_cand,
                          int max_num_ue,
                          int num_beams,
                          int n_rb_sched[num_beams],
                          frame_t sched_frame,
                          slot_t sched_slot,
                          int k2)
{
  int frame = pp_pusch->frame;
  int slot = pp_pusch->slot;
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  int slots_per_frame = cell->frame_structure.numb_slots_frame;

  if (n_cand == 0)
    return 0;

  /* Step 2: RI/TPMI selection */
  nrmac->ul_ri_tpmi_select(nrmac, candidates, n_cand);

  /* Step 3: Beam allocation — done before TDA so that TDA selection can use
   * the allocated beam to check the correct VRB map per beam. */
  FOR_EACH_CANDIDATE(cand, candidates, n_cand)
  cand->skipped = false;
  int n_beam_valid = nrmac->ul_beam_select(&cell->beam_info,
                                           cell->beam_index_list,
                                           candidates,
                                           n_cand,
                                           frame,
                                           slot,
                                           sched_frame,
                                           sched_slot,
                                           slots_per_frame);
  if (n_beam_valid == 0)
    return 0;

  /* Step 4: TDA selection, picks per-cand TDA, validates retx */
  int n_valid = nrmac->ul_tda_select(nrmac, cell, candidates, n_cand, sched_frame, sched_slot, k2);
  if (n_valid == 0)
    return 0;

  const int min_rb = cell->min_grant_prb;
  int remainUEs[num_beams];
  for (int i = 0; i < num_beams; i++)
    remainUEs[i] = max_num_ue;
  int num_ue_sched = 0;

  /* Step 5: MCS selection — updates BLER-based MCS for all candidates
   * (including skipped ones, so the BLER ramp evolves even for unscheduled UEs). */
  nrmac->ul_mcs_select(cell, candidates, n_cand);

  /* Step 6: Sort by beam, then call RB allocation policy per beam */
  qsort(candidates, n_cand, sizeof(*candidates), compare_ul_beam_idx);

  nr_ul_sched_params_t params = {
      .mac = nrmac,
      .cell = cell,
      .dci_frame = frame,
      .dci_slot = slot,
      .frame = sched_frame,
      .slot = sched_slot,
      .num_beams = num_beams,
      .max_num_ue = max_num_ue,
      .min_rb = min_rb,
      .min_mcs = cell->ul_bler.min_mcs,
      .bler_lower = cell->ul_bler.lower,
      .bler_upper = cell->ul_bler.upper,
      .bler_opts = &cell->ul_bler,
      .scc = scc,
  };
  const int index = ul_buffer_index(sched_frame, sched_slot, slots_per_frame, cell->vrb_map_UL_size);
  for (int b = 0; b < num_beams; b++) {
    params.vrb_map_UL[b] = &cell->common_channels.vrb_map_UL[b][index * MAX_BWP_SIZE];
    params.n_rb_avail[b] = n_rb_sched[b];
  }

  int i = 0;
  while (i < n_cand) {
    int beam = candidates[i].alloc_beam_idx;
    int start = i;
    while (i < n_cand && candidates[i].alloc_beam_idx == beam)
      i++;
    /* Skip group of candidates whose beam allocation failed (beam < 0) */
    if (beam < 0)
      continue;
    int count = i - start;

    nrmac->ul_rb_alloc(&params, candidates + start, count);
  }

  /* Release beam reservations for candidates the policy rejected (failed
   * CCE validation, no free RBs, max_num_ue reached, etc.).
   * UL has two beams per candidate: DCI slot + scheduled PUSCH slot. */
  for (int i = 0; i < n_cand; i++) {
    if (!candidates[i].scheduled && !candidates[i].skipped) {
      reset_beam_status(&cell->beam_info,
                        frame,
                        slot,
                        candidates[i].beam_index,
                        slots_per_frame,
                        candidates[i].alloc_dci_beam_new);
      reset_beam_status(&cell->beam_info,
                        sched_frame,
                        sched_slot,
                        candidates[i].beam_index,
                        slots_per_frame,
                        candidates[i].alloc_sched_beam_new);
    }
  }

  /* Step 7: Dispatch all allocations, apply CCE/PUCCH, compute TBS, post_process */
  for (int j = 0; j < n_cand; j++) {
    nr_ul_candidate_t *cand = &candidates[j];
    if (!cand->scheduled)
      continue;
    NR_UE_info_t *UE = cand->UE;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    int beam_idx = cand->alloc_beam_idx;

    /* CCE was validated inside the policy, apply it */
    sched_ctrl->cce_index = cand->alloc_cce_index;
    sched_ctrl->aggregation_level = cand->alloc_aggregation_level;
    sched_ctrl->sched_pdcch = cand->alloc_sched_pdcch;
    /* fill_pdcch_vrb_map already called in commit_ul_alloc */

    const int buf_index = ul_buffer_index(sched_frame, sched_slot, slots_per_frame, cell->vrb_map_UL_size);
    uint16_t *rballoc_mask = &cell->common_channels.vrb_map_UL[beam_idx][buf_index * MAX_BWP_SIZE];
    bwp_info_t bi = get_pusch_bwp_start_size(UE);

    /* Use per-candidate TDA — each candidate carries its own sched_pusch.time_domain_allocation/tda_info */
    int rbSize_used;
    if (cand->is_retx) {
      rbSize_used = apply_ul_retransmission(nrmac,
                                            cell,
                                            pp_pusch,
                                            cand,
                                            scc,
                                            cand->sched_pusch.time_domain_allocation,
                                            &cand->sched_pusch.tda_info,
                                            rballoc_mask,
                                            bi,
                                            sched_frame,
                                            sched_slot);
    } else {
      uint16_t cand_slbitmap = SL_to_bitmap(cand->sched_pusch.tda_info.startSymbolIndex, cand->sched_pusch.tda_info.nrOfSymbols);
      rbSize_used = apply_ul_new_transmission(nrmac,
                                              cell,
                                              pp_pusch,
                                              cand,
                                              scc,
                                              cand->sched_pusch.time_domain_allocation,
                                              &cand->sched_pusch.tda_info,
                                              rballoc_mask,
                                              cand_slbitmap,
                                              bi,
                                              sched_frame,
                                              sched_slot);
    }


    n_rb_sched[beam_idx] -= rbSize_used;
    remainUEs[beam_idx]--;
    num_ue_sched++;
  }

  return num_ue_sched;
}

nfapi_nr_pusch_pdu_t *prepare_pusch_pdu(nfapi_nr_ul_tti_request_t *future_ul_tti_req,
                                        const NR_UE_info_t *UE,
                                        const NR_ServingCellConfigCommon_t *scc,
                                        const NR_sched_pusch_t *sched_pusch,
                                        int transform_precoding,
                                        int harq_id,
                                        int harq_round,
                                        int fh,
                                        int rnti,
                                        const NR_beam_info_t *beam_info)
{
  nfapi_nr_pusch_pdu_t *pusch_pdu = &future_ul_tti_req->pdus_list[future_ul_tti_req->n_pdus].pusch_pdu;
  memset(pusch_pdu, 0, sizeof(nfapi_nr_pusch_pdu_t));
  const NR_UE_UL_BWP_t *ul_bwp = &UE->current_UL_BWP;

  pusch_pdu->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
  pusch_pdu->rnti = rnti;
  pusch_pdu->handle = 0; //not yet used
  pusch_pdu->bwp_size = sched_pusch->bwp_info.bwpSize;
  pusch_pdu->bwp_start = sched_pusch->bwp_info.bwpStart;
  pusch_pdu->subcarrier_spacing = ul_bwp->scs;
  pusch_pdu->cyclic_prefix = 0;
  pusch_pdu->target_code_rate = sched_pusch->R;
  pusch_pdu->qam_mod_order = sched_pusch->Qm;
  pusch_pdu->mcs_index = sched_pusch->mcs;
  pusch_pdu->mcs_table = ul_bwp->mcs_table;
  pusch_pdu->transform_precoding = transform_precoding;
  if (ul_bwp->pusch_Config && ul_bwp->pusch_Config->dataScramblingIdentityPUSCH)
    pusch_pdu->data_scrambling_id = *ul_bwp->pusch_Config->dataScramblingIdentityPUSCH;
  else
    pusch_pdu->data_scrambling_id = *scc->physCellId;
  pusch_pdu->nrOfLayers = sched_pusch->nrOfLayers;
  // DMRS
  pusch_pdu->num_dmrs_cdm_grps_no_data = sched_pusch->dmrs_info.num_dmrs_cdm_grps_no_data;
  pusch_pdu->dmrs_ports = sched_pusch->dmrs_info.dmrs_ports;
  pusch_pdu->ul_dmrs_symb_pos = sched_pusch->dmrs_info.ul_dmrs_symb_pos;
  pusch_pdu->dmrs_config_type = sched_pusch->dmrs_info.dmrs_config_type;
  pusch_pdu->scid = sched_pusch->dmrs_info.scid; // DMRS sequence initialization [TS38.211, sec 6.4.1.1.1]
  pusch_pdu->pusch_identity = sched_pusch->dmrs_info.pusch_identity;
  pusch_pdu->ul_dmrs_scrambling_id = sched_pusch->dmrs_info.dmrs_scrambling_id;
  /* Allocation in frequency domain */
  pusch_pdu->resource_alloc = 1; //type 1
  pusch_pdu->rb_start = sched_pusch->rbStart;
  pusch_pdu->rb_size = sched_pusch->rbSize;
  pusch_pdu->vrb_to_prb_mapping = 0;
  pusch_pdu->frequency_hopping = fh;
  /* Resource Allocation in time domain */
  pusch_pdu->start_symbol_index = sched_pusch->tda_info.startSymbolIndex;
  pusch_pdu->nr_of_symbols = sched_pusch->tda_info.nrOfSymbols;
  /* PUSCH PDU */
  pusch_pdu->pusch_data.rv_index = nr_get_rv(harq_round % 4);
  pusch_pdu->pusch_data.harq_process_id = harq_id;
  pusch_pdu->pusch_data.new_data_indicator = (harq_round == 0) ? 1 : 0; // not NDI but indicator for new transmission
  pusch_pdu->pusch_data.tb_size = sched_pusch->tb_size;
  pusch_pdu->pusch_data.num_cb = 0; // CBG not supported
  // Beamforming
  pusch_pdu->beamforming.num_prgs = 1;
  pusch_pdu->beamforming.prg_size = pusch_pdu->bwp_size;
  pusch_pdu->beamforming.dig_bf_interface = sched_pusch->ant_port_idx.numSpatialStreamIndices;
  memcpy(&pusch_pdu->param_v4, &sched_pusch->ant_port_idx, sizeof(pusch_pdu->param_v4));
  const uint16_t fapi_beam_id = convert_to_fapi_beam(UE->UE_beam_index, beam_info);
  for (int i = 0; i < sched_pusch->ant_port_idx.numSpatialStreamIndices;i++)
    pusch_pdu->beamforming.prgs_list[0].dig_bf_interface_list[i].beam_idx = fapi_beam_id;
  /* TRANSFORM PRECODING --------------------------------------------------------*/
  if (pusch_pdu->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled) {
    // U as specified in section 6.4.1.1.1.2 in 38.211, if sequence hopping and group hopping are disabled
    pusch_pdu->dfts_ofdm.low_papr_group_number = sched_pusch->dmrs_info.pusch_identity % 30;
    pusch_pdu->dfts_ofdm.low_papr_sequence_number = sched_pusch->dmrs_info.low_papr_sequence_number;
  }


  pusch_pdu->maintenance_parms_v3.ldpcBaseGraph = get_BG(sched_pusch->tb_size << 3, sched_pusch->R);
  pusch_pdu->maintenance_parms_v3.tbSizeLbrmBytes = 0;
  if (UE->sc_info.rateMatching_PUSCH) {
    // TBS_LBRM according to section 5.4.2.1 of 38.212
    long *maxMIMO_Layers = UE->sc_info.maxMIMO_Layers_PUSCH;
    if (!maxMIMO_Layers && ul_bwp && ul_bwp->pusch_Config)
      maxMIMO_Layers = ul_bwp->pusch_Config->maxRank;
    int id = UE->uecap_fs_ids.ul_feature_set_percc_id;
    long lbrm_layers = maxMIMO_Layers ? *maxMIMO_Layers : ue_supported_ul_layers(UE->capability, id);
    AssertFatal (maxMIMO_Layers != NULL,"Option with max MIMO layers not configured is not supported\n");
    pusch_pdu->maintenance_parms_v3.tbSizeLbrmBytes = nr_compute_tbslbrm(ul_bwp->mcs_table, UE->sc_info.ul_bw_tbslbrm, lbrm_layers);
  }
  /* PUSCH PTRS */
  if (sched_pusch->dmrs_info.ptrsConfig) {
    bool valid_ptrs_setup = false;
    pusch_pdu->pusch_ptrs.ptrs_ports_list = calloc_or_fail(2, sizeof(nfapi_nr_ptrs_ports_t));
    valid_ptrs_setup = set_ul_ptrs_values(sched_pusch->dmrs_info.ptrsConfig,
                                          pusch_pdu->rb_size,
                                          pusch_pdu->mcs_index,
                                          pusch_pdu->mcs_table,
                                          &pusch_pdu->pusch_ptrs.ptrs_freq_density,
                                          &pusch_pdu->pusch_ptrs.ptrs_time_density,
                                          &pusch_pdu->pusch_ptrs.ptrs_ports_list->ptrs_re_offset,
                                          &pusch_pdu->pusch_ptrs.num_ptrs_ports,
                                          &pusch_pdu->pusch_ptrs.ul_ptrs_power,
                                          pusch_pdu->nr_of_symbols);
    if (valid_ptrs_setup == true)
      pusch_pdu->pdu_bit_map |= PUSCH_PDU_BITMAP_PUSCH_PTRS; // enable PUSCH PTRS
  } else
    pusch_pdu->pdu_bit_map &= ~PUSCH_PDU_BITMAP_PUSCH_PTRS; // disable PUSCH PTRS
  return pusch_pdu;
}

void post_process_ulsch(gNB_MAC_INST *nr_mac,
                        nr_cell_sched_t *cell,
                        post_process_pusch_t *pusch,
                        NR_UE_info_t *UE,
                        NR_sched_pusch_t *sched_pusch,
                        int sched_srs)
{
  frame_t frame = pusch->frame;
  slot_t slot = pusch->slot;

  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;

  /* the UE now has the grant for the request */
  sched_ctrl->sr_cnt = 0;

  int8_t harq_id = sched_pusch->ul_harq_pid;
  if (harq_id < 0) {
    /* PP has not selected a specific HARQ Process, get a new one */
    harq_id = sched_ctrl->available_ul_harq.head;
    AssertFatal(harq_id >= 0, "no free HARQ process available for UE %04x\n", UE->rnti);
    remove_front_nr_list(&sched_ctrl->available_ul_harq);
    sched_pusch->ul_harq_pid = harq_id;
  } else {
    /* PP selected a specific HARQ process. Check whether it will be a new
     * transmission or a retransmission, and remove from the corresponding
     * list */
    if (sched_ctrl->ul_harq_processes[harq_id].round == 0)
      remove_nr_list(&sched_ctrl->available_ul_harq, harq_id);
    else
      remove_nr_list(&sched_ctrl->retrans_ul_harq, harq_id);
  }
  NR_UE_ul_harq_t *cur_harq = &sched_ctrl->ul_harq_processes[harq_id];
  DevAssert(!cur_harq->is_waiting);
  if (cell->radio_config.disable_harq) {
    finish_nr_ul_harq(sched_ctrl, harq_id);
  } else {
    add_tail_nr_list(&sched_ctrl->feedback_ul_harq, harq_id);
    cur_harq->feedback_slot = sched_pusch->slot;
    cur_harq->is_waiting = true;
  }

  /* Statistics */
  AssertFatal(cur_harq->round < cell->ul_bler.harq_round_max, "Indexing ulsch_rounds[%d] is out of bounds\n", cur_harq->round);
  UE->mac_stats.ul.rounds[cur_harq->round]++;
  if (cur_harq->round == 0) {
    UE->mac_stats.ulsch_total_bytes_scheduled += sched_pusch->tb_size;
    sched_ctrl->sched_ul_bytes += sched_pusch->tb_size;
    UE->mac_stats.ul.total_rbs += sched_pusch->rbSize;

  } else {
    UE->mac_stats.ul.total_rbs_retx += sched_pusch->rbSize;
  }
  UE->mac_stats.ul.current_bytes += sched_pusch->tb_size;
  UE->mac_stats.ul.current_rbs = sched_pusch->rbSize;
  nr_mac->mac_stats.ul.used_prb_aggregate += sched_pusch->rbSize;
  sched_ctrl->last_ul_frame = sched_pusch->frame;
  sched_ctrl->last_ul_slot = sched_pusch->slot;

  T(T_GNB_MAC_UL, T_INT(UE->rnti), T_INT(frame), T_INT(slot), T_INT(sched_pusch->mcs), T_INT(sched_pusch->tb_size));

  DevAssert(sched_pusch->nrOfLayers >= 1 && sched_pusch->nrOfLayers <= NR_KPM_MAX_LAYERS);
  DevAssert(current_BWP->mcs_table == 0 || current_BWP->mcs_table == 1 || current_BWP->mcs_table == 3);
  DevAssert(sched_pusch->mcs < NR_KPM_NB_MCS);
  NR_du_stats_t *stats = &cell->du_stats;
  stats->pusch_mcs_dist[sched_pusch->nrOfLayers - 1][current_BWP->mcs_table][sched_pusch->mcs] += sched_pusch->rbSize;

  /* PUSCH in a later slot, but corresponding DCI now! */
  const int index = ul_buffer_index(sched_pusch->frame,
                                    sched_pusch->slot,
                                    cell->frame_structure.numb_slots_frame,
                                    cell->UL_tti_req_ahead_size);
  nfapi_nr_ul_tti_request_t *req = &cell->UL_tti_req_ahead[index];
  if (req->SFN != sched_pusch->frame || req->Slot != sched_pusch->slot)
    LOG_W(NR_MAC,
          "%d.%d future UL_tti_req's frame.slot %d.%d does not match PUSCH %d.%d\n",
          frame,
          slot,
          req->SFN,
          req->Slot,
          sched_pusch->frame,
          sched_pusch->slot);
  AssertFatal(req->n_pdus < sizeof(req->pdus_list) / sizeof(req->pdus_list[0]),
              "Invalid UL_tti_req_ahead->n_pdus %d\n",
              req->n_pdus);

  req->pdus_list[req->n_pdus].pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
  req->pdus_list[req->n_pdus].pdu_size = sizeof(nfapi_nr_pusch_pdu_t);
  nfapi_nr_pusch_pdu_t *pusch_pdu = prepare_pusch_pdu(req,
                                                      UE,
                                                      scc,
                                                      sched_pusch,
                                                      current_BWP->transform_precoding,
                                                      harq_id,
                                                      cur_harq->round,
                                                      current_BWP->pusch_Config && current_BWP->pusch_Config->frequencyHopping,
                                                      UE->rnti,
                                                      &cell->beam_info);
  req->n_pdus += 1;

  // Calculate the normalized tx_power for PHR
  bool hasDeltaMCS = current_BWP->pusch_Config && current_BWP->pusch_Config->pusch_PowerControl->deltaMCS;
  sched_pusch->phr_txpower_calc = hasDeltaMCS ? compute_ph_mcs_factor(sched_pusch) : 0;

  int tpc = nr_mac_get_tpc(&sched_ctrl->pusch_pc);
  LOG_D(NR_MAC,
        "ULSCH/PUSCH: %4d.%2d RNTI %04x UL sched %4d.%2d DCI L %d start %2d RBS %3d TDA %2d dmrs_pos %x MCS "
        "Table %2d MCS %2d nrOfLayers %2d num_dmrs_cdm_grps_no_data %2d TBS %4d HARQ PID %2d round %d RV %d NDI %d est %6d sched "
        "%6d est BSR %6d TPC %d\n",
        frame,
        slot,
        UE->rnti,
        sched_pusch->frame,
        sched_pusch->slot,
        sched_ctrl->aggregation_level,
        sched_pusch->rbStart,
        sched_pusch->rbSize,
        sched_pusch->time_domain_allocation,
        sched_pusch->dmrs_info.ul_dmrs_symb_pos,
        current_BWP->mcs_table,
        sched_pusch->mcs,
        sched_pusch->nrOfLayers,
        sched_pusch->dmrs_info.num_dmrs_cdm_grps_no_data,
        sched_pusch->tb_size,
        harq_id,
        cur_harq->round,
        nr_get_rv(cur_harq->round % 4),
        cur_harq->ndi,
        sched_ctrl->estimated_ul_buffer,
        sched_ctrl->sched_ul_bytes,
        sched_ctrl->estimated_ul_buffer - sched_ctrl->sched_ul_bytes,
        tpc);

  /* Save information on MCS, TBS etc for the current initial transmission
   * so we have access to it when retransmitting */
  cur_harq->sched_pusch = *sched_pusch;

  /* a PDCCH PDU groups DCIs per BWP and CORESET. Save a pointer to each
   * allocated PDCCH so we can easily allocate UE's DCIs independent of any
   * CORESET order */
  NR_SearchSpace_t *ss = sched_ctrl->search_space;
  NR_ControlResourceSet_t *coreset = sched_ctrl->coreset;
  const int coresetid = coreset->controlResourceSetId;
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu = pusch->pdcch_pdu_coreset[coresetid];
  if (!pdcch_pdu) {
    nfapi_nr_ul_dci_request_pdus_t *ul_dci_request_pdu = &pusch->ul_dci_req->ul_dci_pdu_list[pusch->ul_dci_req->numPdus];
    memset(ul_dci_request_pdu, 0, sizeof(nfapi_nr_ul_dci_request_pdus_t));
    ul_dci_request_pdu->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    ul_dci_request_pdu->PDUSize = (uint8_t)(4 + sizeof(nfapi_nr_dl_tti_pdcch_pdu));
    pdcch_pdu = &ul_dci_request_pdu->pdcch_pdu.pdcch_pdu_rel15;
    pusch->ul_dci_req->numPdus += 1;
    nr_configure_pdcch(pdcch_pdu, coreset, &sched_ctrl->sched_pdcch);
    pusch->pdcch_pdu_coreset[coresetid] = pdcch_pdu;
  }

  LOG_D(NR_MAC, "Configuring ULDCI/PDCCH in %d.%d at CCE %d, rnti %04x\n", frame, slot, sched_ctrl->cce_index, UE->rnti);

  /* Fill PDCCH DL DCI PDU */
  nfapi_nr_dl_dci_pdu_t *dci_pdu = prepare_dci_pdu(pdcch_pdu,
                                                   scc,
                                                   ss,
                                                   coreset,
                                                   &sched_pusch->dci_ant_idx,
                                                   sched_ctrl->aggregation_level,
                                                   sched_ctrl->cce_index,
                                                   convert_to_fapi_beam(UE->UE_beam_index, &cell->beam_info),
                                                   UE->rnti);
  pdcch_pdu->numDlDci++;

  dci_pdu_rel15_t uldci_payload;
  memset(&uldci_payload, 0, sizeof(uldci_payload));
  if (current_BWP->dci_format == NR_UL_DCI_FORMAT_0_1)
    LOG_D(NR_MAC_DCI,
          "add ul dci harq %d for %d.%d %d.%d round %d\n",
          harq_id,
          frame,
          slot,
          sched_pusch->frame,
          sched_pusch->slot,
          sched_ctrl->ul_harq_processes[harq_id].round);

  // If nrOfLayers is the same as in srs_feedback, we use the best TPMI, i.e. the one in srs_feedback.
  // Otherwise, we use the valid TPMI that we saved in the first transmission.
  int *tpmi = NULL;
  if (pusch_pdu->nrOfLayers != (sched_ctrl->srs_feedback.ul_ri + 1))
    tpmi = &sched_pusch->tpmi;
  config_uldci(&UE->sc_info,
               pusch_pdu,
               &uldci_payload,
               &sched_ctrl->srs_feedback,
               tpmi,
               sched_pusch->time_domain_allocation,
               tpc,
               cur_harq->ndi,
               current_BWP,
               ss->searchSpaceType->present);

  cur_harq->sched_pusch.tpc_pusch = tpc;

  if (sched_srs > 0) {
    if (!nr_schedule_aperiodic_srs(nr_mac, cell, UE, sched_pusch->frame, sched_pusch->slot, sched_pusch->tda_info.k2, sched_srs))
      sched_srs = 0;  // if we can't schedule aperiodic SRS we do not set the DCI field to trigger the UE transmission
  }
  fill_dci_pdu_rel15(&UE->sc_info,
                     &UE->current_DL_BWP,
                     current_BWP,
                     dci_pdu,
                     &uldci_payload,
                     current_BWP->dci_format,
                     TYPE_C_RNTI_,
                     sched_srs,
                     ss,
                     coreset,
                     UE->pdsch_HARQ_ACK_Codebook,
                     cell->cset0_bwp_size);

}

/* Slots elapsed since the UE was last scheduled in UL. */
static int nr_ue_ul_inactivity_slots(const frame_structure_t *fs,
                                     const NR_UE_sched_ctrl_t *sched_ctrl,
                                     int sched_frame,
                                     int sched_slot)
{
  const int slots_per_frame = fs->numb_slots_frame;
  const int last_ul_slot = fs->frame_type == TDD ? get_first_ul_slot(fs, false) : sched_ctrl->last_ul_slot;
  const int last_ul_sched = sched_ctrl->last_ul_frame * slots_per_frame + last_ul_slot;
  return (sched_frame * slots_per_frame + sched_slot - last_ul_sched + 1024 * slots_per_frame) % (1024 * slots_per_frame);
}

/* Slots of UL inactivity allowed before a UE gets a default grant. Depends
   only on the cell, so it is the same for every UE. */
static int nr_ue_max_ul_inactivity_slots(const frame_structure_t *fs, int ulsch_max_frame_inactivity)
{
  return ulsch_max_frame_inactivity > 0 ? ulsch_max_frame_inactivity * fs->numb_slots_frame : fs->numb_slots_period;
}

static int collect_ul_candidates(gNB_MAC_INST *mac,
                                 nr_cell_sched_t *cell,
                                 NR_UEs_t *UEs,
                                 nr_ul_candidate_t *candidates,
                                 int max_candidates,
                                 frame_t frame,
                                 slot_t slot,
                                 int k2,
                                 int sched_frame,
                                 int sched_slot)
{
  int numUE = 0;
  bool aperiodic_srs_scheduled = false;
  const frame_structure_t *fs = &cell->frame_structure;
  const float ul_slots_per_s = (float)get_ul_slots_per_period(fs) / fs->numb_slots_period * fs->numb_slots_frame * 100;
  const int max_inactivity = nr_ue_max_ul_inactivity_slots(fs, cell->ulsch_max_frame_inactivity);

  FOR_EACH_CONNECTED_UE(UEs, UE) {
    if (UE->pcell != cell)
      continue;
    if (numUE >= max_candidates)
      break;
    /* Per-slot UL goodput EWMA (bps) — update once per DL slot, called for up
     * to every UL slot. current_bytes was set by the previous slot's dispatch.
     * Slow EWMA (alpha=0.001) for stable display. */
    NR_mac_dir_stats_t *stats = &UE->mac_stats.ul;
    float instant_bps = (float)stats->current_bytes * 8.0f * ul_slots_per_s;
    UE->ul_thr_ue = (1 - 0.01f) * UE->ul_thr_ue + 0.01f * instant_bps;
    UE->ul_thr_ue_display = (1 - 0.001f) * UE->ul_thr_ue_display + 0.001f * instant_bps;
    stats->current_bytes = 0;

    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    if (!nr_mac_ue_is_active(UE))
      continue;

    LOG_D(NR_MAC, "collect_ul_candidates: preparing UL scheduling for UE %04x\n", UE->rnti);
    NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;
    stats->current_rbs = 0;

    bwp_info_t bi = get_pusch_bwp_start_size(UE);

    /* QoS / slice info, extract from first DRB for external policies */
    uint64_t fiveQI = 0;
    int lc_priority = 0;
    nssai_t nssai = {0, 0xFFFFFF};
    for (int j = 0; j < seq_arr_size(&sched_ctrl->lc_config); j++) {
      const nr_lc_config_t *c = seq_arr_at(&sched_ctrl->lc_config, j);
      if (c->lcid >= 4) {
        lc_priority = c->priority;
        nssai = c->nssai;
        for (int q = 0; q < NR_MAX_NUM_QFI; q++) {
          if (c->qos_config[q].fiveQI > 0) {
            fiveQI = c->qos_config[q].fiveQI;
            break;
          }
        }
        if (fiveQI > 0)
          break;
      }
    }
    uint16_t cqi = sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.wb_cqi_1tb;
    nr_ul_candidate_t cand = {0};
    cand.UE = UE;
    cand.rnti = UE->rnti;
    cand.beam_index = UE->UE_beam_index;
    cand.alloc_beam_idx = -1;
    cand.bwp_start = bi.bwpStart;
    cand.bwp_size = bi.bwpSize;
    /* sched_pusch.nrOfLayers and sched_pusch.tpmi are set by ul_ri_tpmi_select after collect */
    cand.mcs_table = current_BWP->mcs_table;
    cand.avg_throughput = UE->ul_thr_ue;
    cand.ph = sched_ctrl->ph;
    cand.pcmax = sched_ctrl->pcmax;
    cand.fiveQI = fiveQI;
    cand.priority = lc_priority;
    cand.nssai = nssai;
    cand.cqi = cqi;
    cand.beam_rsrp = UE->beam_rsrp;
    cand.beam_sinr = UE->beam_sinr;

    /* Check if retransmission is necessary */
    int ul_harq_pid = sched_ctrl->retrans_ul_harq.head;
    LOG_D(NR_MAC, "collect_ul_candidates: UE %04x harq_pid %d\n", UE->rnti, ul_harq_pid);
    if (ul_harq_pid >= 0) {
      const NR_sched_pusch_t *retInfo = &sched_ctrl->ul_harq_processes[ul_harq_pid].sched_pusch;
      cand.is_retx = true;
      cand.retx_harq_pid = ul_harq_pid;
      cand.retx_rbSize = retInfo->rbSize;
      cand.current_mcs = retInfo->mcs;
      cand.bler = sched_ctrl->ul_bler_stats.bler;
      candidates[numUE++] = cand;
      continue;
    }

    /* Skip UE if no free HARQ processes */
    if (sched_ctrl->available_ul_harq.head < 0) {
      LOG_D(NR_MAC, "[UE %04x][%4d.%2d] has no free UL HARQ process, skipping\n", UE->rnti, frame, slot);
      continue;
    }

    /* number of bytes we think the UE still has to transmit */
    const int B = max(0, sched_ctrl->estimated_ul_buffer - sched_ctrl->sched_ul_bytes);

    const int inactivity = nr_ue_ul_inactivity_slots(fs, sched_ctrl, sched_frame, sched_slot);
    const bool long_inactivity = inactivity >= max_inactivity;

    /* Consider the UE if (1) we think it has more bytes awaiting than what we
     * scheduled, (2) it requested a grant through SR, or (3) we did not schedule
     * it for too long, in which case we give it a default grant so that it can
     * send a BSR. */
    if ((B == 0 && sched_ctrl->sr_cnt == 0 && !long_inactivity) || nr_timer_is_active(&sched_ctrl->transm_interrupt))
      continue;

    /* Update BLER stats; MCS adaptation is done by ul_mcs_select pipeline stage */
    const int max_mcs_table = (current_BWP->mcs_table == 0 || current_BWP->mcs_table == 2) ? 28 : 27;
    const int max_mcs = min(cell->ul_bler.max_mcs, max_mcs_table);
    bool bler_updated = update_bler_stats(&cell->ul_bler, stats, &sched_ctrl->ul_bler_stats, frame);

    cand.is_retx = false;
    if (!aperiodic_srs_scheduled) {
      cand.sched_srs = verify_aperiodic_srs(cell, sched_slot, k2, &UE->sched_srs.aperiodic_sched.aperiodic_srs_timer, UE);
      aperiodic_srs_scheduled = cand.sched_srs > 0;
    } else
      cand.sched_srs = 0;
    cand.retx_harq_pid = -1;
    cand.sched_long_inactivity = long_inactivity;
    cand.pending_bytes = B;
    cand.bler = sched_ctrl->ul_bler_stats.bler;
    cand.bler_updated = bler_updated;
    cand.current_mcs = sched_ctrl->ul_bler_stats.mcs;
    cand.max_mcs = max_mcs;
    cand.last_num_sched = sched_ctrl->ul_bler_stats.last_num_sched;
    cand.snrx10 = (int)(sched_ctrl->pusch_pc.avg_snr * 10);
    cand.sr_cnt = sched_ctrl->sr_cnt;
    LOG_D(NR_MAC,
          "[UE %04x][%4d.%2d] b %d, ul_thr_ue %f, mcs %d, SR count %d, UL inactivity %d/%d slots, sched_long_inactivity %d sched_srs %d\n",
          UE->rnti,
          frame,
          slot,
          B,
          UE->ul_thr_ue,
          cand.current_mcs,
          sched_ctrl->sr_cnt,
          inactivity,
          max_inactivity,
          cand.sched_long_inactivity,
          cand.sched_srs);

    candidates[numUE++] = cand;
  }

  return numUE;
}

void nr_ulsch_preprocessor(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pusch_t *pp_pusch)
{
  int frame = pp_pusch->frame;
  int slot = pp_pusch->slot;

  NR_COMMON_channels_t *cc = &cell->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  AssertFatal(scc, "We need one serving cell config common\n");
  const frame_structure_t *fs = &cell->frame_structure;

  // we assume the same K2 for all UEs
  const int koffset = get_NTN_Koffset(scc);
  const int min_rxtx = cell->radio_config.minRXTXTIME + koffset;

  int num_beams = cell->beam_info.beam_allocation ? cell->beam_info.config.beams_per_period : 1;
  int bw = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;

  // FAPI cannot handle more than MAX_DCI_CORESET DCIs
  static_assert(4 < MAX_DCI_CORESET, "cannot have more concurrent UEs than MAX_DCI_CORESET\n");
  int max_dci = 4;

  fsn_t current = {frame, slot, *scc->ssbSubcarrierSpacing};
  fsn_t min_next = fsn_add_delta(current, min_rxtx);
  /* if it's the last DL slot, we should try all TDAs to make sure that the
   * scheduler can reach e.g. the next mixed slot. Otherwise, if we don't, we
   * might starve HARQ processes that need a retransmission in a specific slot
   * but we might not necessarily reach it */
  bool last_dl = (current.s % fs->numb_slots_period) == (fs->period_cfg.num_dl_slots - 1);

  fsn_t *next = &cell->ul_next;
  while (max_dci > 0) {
    /* go to the next UL slot, skipping DL if necessary */
    *next = fsn_get_max(*next, min_next);
    while (!is_ul_slot(next->s, fs))
      *next = fsn_add_delta(*next, 1);
    if (!is_dl_slot(current.s, fs)) // if current slot is not DL, nothing to do
      break;

    /* get a TDA so that next UL scheduling slot can be reached from current,
     * and exit if there is no such TDA. Remove koffset, as it is
     * "cell-specific", i.e., the UE will add it on the computation. */
    int k2 = fsn_get_diff(*next, current) - koffset;
    DevAssert(k2 > 0);
    int slots_per_frame = cell->frame_structure.numb_slots_frame;
    fsn_t sched_fs = get_fb_frame_slot(frame, slot, k2, slots_per_frame, koffset);

    /* Check that at least one TDA can reach this slot, if not, no future slot is reachable either */
    {
      const NR_tda_info_t *tda_check = NULL;
      if (get_num_ul_tda(nr_mac, cell, sched_fs.s, k2, &tda_check) == 0)
        break;
    }

    /* Step 1: Collect candidates fresh each iteration. Although the candidate
     * inputs don't depend on TDA/k2, nr_ul_schedule mutates UE state (consumes
     * HARQ processes, bumps sched_ul_bytes) so a stale snapshot would cause
     * double-grants and retx on already-dispatched HARQ PIDs. */
    nr_ul_candidate_t candidates[MAX_MOBILES_PER_GNB] = {0};
    int n_cand = collect_ul_candidates(nr_mac,
                                       cell,
                                       &nr_mac->UE_info,
                                       candidates,
                                       MAX_MOBILES_PER_GNB,
                                       frame,
                                       slot,
                                       k2,
                                       sched_fs.f,
                                       sched_fs.s);
    if (n_cand == 0 && !last_dl)
      break;

    nr_mac->mac_stats.ul.total_prb_aggregate += bw;
    int len[num_beams];
    for (int i = 0; i < num_beams; i++)
      len[i] = bw;
    int sched = nr_ul_schedule(nr_mac, cell, pp_pusch, candidates, n_cand, max_dci, num_beams, len, sched_fs.f, sched_fs.s, k2);
    LOG_D(NR_MAC,
          "run nr_ul_schedule() at %4d.%2d k2 %d (ULSCH at %4d.%2d) scheduled %d last_dl %d\n",
          frame,
          slot,
          k2,
          next->f,
          next->s,
          sched,
          last_dl);
    /* if we did not schedule anything, and it's not the last slot, break. In
     * the case we did schedule or it's the last slot (see above!), continue
     * advancing till there is no TDA anymore */
    if (sched == 0 && !last_dl)
      break;
    max_dci -= sched;

    *next = fsn_add_delta(*next, 1);
  }
}

void nr_schedule_ulsch(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, nfapi_nr_ul_dci_request_t *ul_dci_req)
{
  ul_dci_req->SFN = frame;
  ul_dci_req->Slot = slot;
  post_process_pusch_t pusch = { .frame = frame, .slot = slot, .ul_dci_req = ul_dci_req, .pdcch_pdu_coreset = {NULL}, };

  nr_mac->pre_processor_ul(nr_mac, cell, &pusch);
}

bool nr_ul_check_phr(const nr_ul_sched_params_t *params,
                     const nr_ul_candidate_t *cand,
                     uint16_t rbSize,
                     uint8_t mcs,
                     nr_ul_phr_advice_t *advice)
{
  NR_UE_UL_BWP_t *current_BWP = &cand->UE->current_UL_BWP;
  bool hasDeltaMCS = current_BWP->pusch_Config && current_BWP->pusch_Config->pusch_PowerControl->deltaMCS;
  NR_pusch_dmrs_t dmrs_info = get_ul_dmrs_params(params->scc,
                                                 current_BWP,
                                                 &cand->sched_pusch.tda_info,
                                                 cand->sched_pusch.nrOfLayers,
                                                 cand->sched_pusch.dmrs_info.dmrs_ports,
                                                 cand->sched_pusch.dmrs_info.num_dmrs_cdm_grps_no_data);
  int n_dmrs = dmrs_info.N_PRB_DMRS * dmrs_info.num_dmrs_symb;

  /* Transient NR_sched_pusch_t scratchpad, feeds compute_ph_mcs_factor() which
     reads mcs / tda_info / nrOfLayers / dmrs_info / tb_size off the struct. */
  NR_sched_pusch_t pot = {0};
  pot.tda_info = cand->sched_pusch.tda_info;
  pot.dmrs_info = dmrs_info;
  pot.nrOfLayers = cand->sched_pusch.nrOfLayers;
  pot.rbSize = rbSize;
  pot.rbSize = check_sc_fdma_rbsize(current_BWP->transform_precoding, pot.rbSize);
  pot.mcs = mcs;

  uint16_t R;
  uint8_t Qm;
  update_ul_ue_R_Qm(pot.mcs, current_BWP->mcs_table, current_BWP->pusch_Config, &R, &Qm);
  pot.R = R;
  pot.Qm = Qm;
  pot.tb_size = nr_compute_tbs(Qm, R, pot.rbSize, pot.tda_info.nrOfSymbols, n_dmrs, 0, 0, pot.nrOfLayers) >> 3;
  int tx_power = compute_ph_rb_factor(current_BWP->scs, pot.rbSize) + (hasDeltaMCS ? compute_ph_mcs_factor(&pot) : 0);

  if (cand->ph >= tx_power)
    return true; /* allocation respects PHR */

  /* PHR violated, compute two suggestions */

  /* Suggestion 1: max_mcs_min_rb, reduce RBs first (down to min_rb), then MCS */
  {
    nr_ul_phr_suggestion_t *s = &advice->max_mcs_min_rb;
    NR_sched_pusch_t p1 = pot;
    int tp = tx_power;

    while (cand->ph < tp && p1.rbSize > params->min_rb
           && check_sc_fdma_rbsize(current_BWP->transform_precoding, p1.rbSize - 1) >= params->min_rb) {
      p1.rbSize = check_sc_fdma_rbsize(current_BWP->transform_precoding, --p1.rbSize);
      p1.tb_size = nr_compute_tbs(p1.Qm, p1.R, p1.rbSize, p1.tda_info.nrOfSymbols, n_dmrs, 0, 0, p1.nrOfLayers) >> 3;
      tp = compute_ph_rb_factor(current_BWP->scs, p1.rbSize) + (hasDeltaMCS ? compute_ph_mcs_factor(&p1) : 0);
    }
    while (cand->ph < tp && p1.mcs > 0) {
      p1.mcs--;
      update_ul_ue_R_Qm(p1.mcs, current_BWP->mcs_table, current_BWP->pusch_Config, &p1.R, &p1.Qm);
      p1.tb_size = nr_compute_tbs(p1.Qm, p1.R, p1.rbSize, p1.tda_info.nrOfSymbols, n_dmrs, 0, 0, p1.nrOfLayers) >> 3;
      tp = compute_ph_rb_factor(current_BWP->scs, p1.rbSize) + (hasDeltaMCS ? compute_ph_mcs_factor(&p1) : 0);
    }
    s->rbSize = p1.rbSize;
    s->mcs = p1.mcs;
    s->valid = (cand->ph >= tp);
  }

  /* Suggestion 2: same_rb_min_mcs, keep RBs, only reduce MCS */
  {
    nr_ul_phr_suggestion_t *s = &advice->same_rb_min_mcs;
    NR_sched_pusch_t p2 = pot;
    int tp = tx_power;

    while (cand->ph < tp && p2.mcs > 0) {
      p2.mcs--;
      update_ul_ue_R_Qm(p2.mcs, current_BWP->mcs_table, current_BWP->pusch_Config, &p2.R, &p2.Qm);
      p2.tb_size = nr_compute_tbs(p2.Qm, p2.R, p2.rbSize, p2.tda_info.nrOfSymbols, n_dmrs, 0, 0, p2.nrOfLayers) >> 3;
      tp = compute_ph_rb_factor(current_BWP->scs, p2.rbSize) + (hasDeltaMCS ? compute_ph_mcs_factor(&p2) : 0);
    }
    s->rbSize = p2.rbSize;
    s->mcs = p2.mcs;
    s->valid = (cand->ph >= tp);
  }

  return false;
}

/* CCE validation for UL DCI. Called by commit_ul_alloc inside the policy.
 * No PUCCH allocation needed (UL grants don't trigger PUCCH HARQ-ACK). */
bool nr_ul_validate_cce(const nr_ul_sched_params_t *params, nr_ul_candidate_t *cand)
{
  NR_UE_sched_ctrl_t *sched_ctrl = &cand->UE->UE_sched_ctrl;
  int agg_level = sched_ctrl->aggregation_level;
  NR_sched_pdcch_t sched_pdcch = sched_ctrl->sched_pdcch;
  int CCEIndex = get_cce_index(params->cell,
                               params->dci_slot,
                               cand->UE->rnti,
                               &agg_level,
                               cand->alloc_dci_beam_idx,
                               sched_ctrl->search_space,
                               sched_ctrl->coreset,
                               &sched_pdcch,
                               sched_ctrl->pdcch_cl_adjust);
  if (CCEIndex < 0) {
    sched_ctrl->ul_cce_fail++;
    LOG_D(NR_MAC, "[UE %04x][%4d.%2d] no free CCE for UL DCI\n", cand->UE->rnti, params->dci_frame, params->dci_slot);
    return false;
  }
  cand->alloc_cce_index = CCEIndex;
  cand->alloc_aggregation_level = agg_level;
  cand->alloc_sched_pdcch = sched_pdcch;
  return true;
}

/* Validate CCE, mark VRBs, write alloc fields onto cand. Returns false if CCE fails. */
bool commit_ul_alloc(const nr_ul_sched_params_t *params, nr_ul_candidate_t *cand)
{
  if (!nr_ul_validate_cce(params, cand))
    return false;

  /* Mark CCE as used so subsequent UEs in the same slot see it as taken */
  fill_pdcch_vrb_map(params->cell,
                     &cand->alloc_sched_pdcch,
                     cand->alloc_cce_index,
                     cand->alloc_aggregation_level,
                     cand->alloc_dci_beam_idx);

  /* Mark VRB map for this candidate's beam */
  uint16_t *vrb_map = params->vrb_map_UL[cand->alloc_beam_idx];
  for (int rb = 0; rb < cand->sched_pusch.rbSize; rb++)
    vrb_map[cand->sched_pusch.rbStart + cand->bwp_start + rb] |= cand->alloc_slbitmap;
  return true;
}
