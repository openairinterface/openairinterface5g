/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file PHY/NR_TRANSPORT/nr_dlsch_coding_slot.c
 * \brief Top-level routines for implementing LDPC-coded (DLSCH) transport channels from 38-212, 15.2
 */

#include "PHY/defs_gNB.h"
#include "PHY/CODING/coding_extern.h"
#include "PHY/CODING/coding_defs.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/CODING/nrLDPC_extern.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_dlsch.h"
#include "PHY/nr_phy_common/inc/nr_phy_meas.h"
#include "SCHED_NR/sched_nr.h"
#include "common/utils/LOG/log.h"
#include "common/utils/nr/nr_common.h"
#include <syscall.h>
#include <openair2/UTIL/OPT/opt.h>

#ifdef LDPC_CUDA
#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"
#endif
// #define DEBUG_DLSCH_CODING
// #define DEBUG_DLSCH_FREE 1

void free_gNB_dlsch(NR_gNB_DLSCH_t *dlsch, uint16_t N_RB, const NR_DL_FRAME_PARMS *frame_parms)
{
  int max_layers = min(frame_parms->nb_antennas_tx, NR_MAX_NB_LAYERS);
  uint16_t a_segments = MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * max_layers;

  if (N_RB != 273) {
    a_segments = a_segments * N_RB;
    a_segments = a_segments / 273 + 1;
  }

  if (dlsch->b) {
    free16(dlsch->b, a_segments * 1056); // allocated with malloc16() in all builds, used only on the host
    dlsch->b = NULL;
  }
  if (dlsch->f) {
    free16(dlsch->f, N_RB * frame_parms->symbols_per_slot * NR_NB_SC_PER_RB * 8 * NR_MAX_NB_LAYERS);
    dlsch->f = NULL;
  }
  for (int r = 0; r < a_segments; r++) {
#ifdef LDPC_CUDA
    gpuFreeHost(dlsch->c[r]);
#else
    free(dlsch->c[r]);
#endif
    dlsch->c[r] = NULL;
  }
  free(dlsch->c);
}

NR_gNB_DLSCH_t new_gNB_dlsch(NR_DL_FRAME_PARMS *frame_parms, uint16_t N_RB)
{
  int max_layers = min(frame_parms->nb_antennas_tx, NR_MAX_NB_LAYERS);
  uint16_t a_segments = MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * max_layers; // number of segments to be allocated

  if (N_RB != 273) {
    a_segments = a_segments * N_RB;
    a_segments = a_segments / 273 + 1;
  }

  LOG_D(PHY, "Allocating %d segments (MAX %d, N_PRB %d)\n", a_segments, MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER, N_RB);
  uint32_t dlsch_bytes = a_segments * 1056; // allocated bytes per segment
  NR_gNB_DLSCH_t dlsch = {0};

  dlsch.b = malloc16(dlsch_bytes);
  AssertFatal(dlsch.b, "cannot allocate memory for dlsch.b\n");
  bzero(dlsch.b, dlsch_bytes);

  dlsch.c = (uint8_t **)malloc16(a_segments * sizeof(uint8_t *));
#ifdef LDPC_CUDA
  dlsch.c_devh = gpuHostAlloc_or_fail(a_segments * sizeof(uint8_t *), gpuHostAllocMapped);
  dlsch.c_dev = gpuHostGetDevicePointer_or_fail(dlsch.c_devh);
#endif
  for (int r = 0; r < a_segments; r++) {
    // account for filler in first segment and CRCs for multiple segment case
    // [hna] 8448 is the maximum CB size in NR
    //       68*348 = 68*(maximum size of Zc)
    //       In section 5.3.2 in 38.212, the for loop is up to N + 2*Zc (maximum size of N is 66*Zc, therefore 68*Zc)
#ifdef LDPC_CUDA
    dlsch.c[r] = gpuHostAlloc_or_fail((8448 / 8) * sizeof(uint8_t), gpuHostAllocMapped);
    ((uint8_t **)dlsch.c_devh)[r] = gpuHostGetDevicePointer_or_fail(dlsch.c[r]);
#else
    dlsch.c[r] = malloc16(8448 / 8);
#endif
    AssertFatal(dlsch.c[r], "cannot allocate dlsch->c[%d]\n", r);
    bzero(dlsch.c[r], 8448 / 8);
  }

  dlsch.f = malloc16(N_RB * frame_parms->symbols_per_slot * NR_NB_SC_PER_RB * 8 * NR_MAX_NB_LAYERS);
  AssertFatal(dlsch.f, "cannot allocate dlsch->f\n");
  bzero(dlsch.f, N_RB * frame_parms->symbols_per_slot * NR_NB_SC_PER_RB * 8 * NR_MAX_NB_LAYERS);

  return (dlsch);
}

int nr_dlsch_encoding(PHY_VARS_gNB *gNB,
                      int n_dlsch,
                      NR_gNB_DLSCH_t *dlsch_array,
                      int frame,
                      uint8_t slot,
                      unsigned char *output)
{
  time_stats_t *dlsch_crc_stats = &gNB->dlsch_crc_stats;
  nrLDPC_TB_encoding_parameters_t TBs[n_dlsch];
  memset(TBs, 0, sizeof(TBs));

  int num_segments = 0;

  for (int i = 0; i < n_dlsch; i++) {
    NR_gNB_DLSCH_t *dlsch = &dlsch_array[i];

    unsigned int crc = 1;
    const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *rel15 = &dlsch->pdsch_pdu->pdsch_pdu_rel15;
    uint32_t A = rel15->TBSize[0] << 3;
    unsigned char *a = dlsch->pdu;
    if (rel15->rnti != SI_RNTI) {
      ws_trace_t tmp = {.nr = true,
                        .direction = DIRECTION_DOWNLINK,
                        .type = gNB->frame_parms.frame_type == FDD ? FDD_RADIO : TDD_RADIO,
                        .pdu_buffer = a,
                        .pdu_buffer_size = rel15->TBSize[0],
                        .ueid = 0,
                        .rntiType = WS_C_RNTI,
                        .rnti = rel15->rnti,
                        .sysFrame = frame,
                        .subframe = slot,
                        .harq_pid = 0, // difficult to find the harq pid here
                        .oob_event = 0,
                        .oob_event_value = 0};
      trace_pdu(&tmp);
    }

    NR_gNB_PHY_STATS_t *phy_stats = NULL;
    if (rel15->rnti != 0xFFFF)
      phy_stats = get_phy_stats(gNB, rel15->rnti);

    if (phy_stats) {
      phy_stats->frame = frame;
      phy_stats->dlsch_stats.total_bytes_tx += rel15->TBSize[0];
      phy_stats->dlsch_stats.current_RI = rel15->nrOfLayers;
      phy_stats->dlsch_stats.current_Qm = rel15->qamModOrder[0];
    }

    start_meas(dlsch_crc_stats);
    int max_bytes = MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * rel15->nrOfLayers * 1056;
    int B;
    if (A > NR_MAX_PDSCH_TBS) {
      // Add 24-bit crc (polynomial A) to payload
      crc = crc24a(a, A) >> 8;
      a[A >> 3] = ((uint8_t *)&crc)[2];
      a[1 + (A >> 3)] = ((uint8_t *)&crc)[1];
      a[2 + (A >> 3)] = ((uint8_t *)&crc)[0];
      // printf("CRC %x (A %d)\n",crc,A);
      // printf("a0 %d a1 %d a2 %d\n", a[A>>3], a[1+(A>>3)], a[2+(A>>3)]);
      B = A + 24;
      AssertFatal((A / 8) + 4 <= max_bytes, "A %d is too big (A/8+4 = %d > %d)\n", A, (A / 8) + 4, max_bytes);
      memcpy(dlsch->b, a, (A / 8) + 4); // why is this +4 if the CRC is only 3 bytes?
    } else {
      // Add 16-bit crc (polynomial A) to payload
      crc = crc16(a, A) >> 16;
      a[A >> 3] = ((uint8_t *)&crc)[1];
      a[1 + (A >> 3)] = ((uint8_t *)&crc)[0];
      // printf("CRC %x (A %d)\n",crc,A);
      // printf("a0 %d a1 %d \n", a[A>>3], a[1+(A>>3)]);
      B = A + 16;
      AssertFatal((A / 8) + 3 <= max_bytes, "A %d is too big (A/8+3 = %d > %d)\n", A, (A / 8) + 3, max_bytes);
      memcpy(dlsch->b, a, (A / 8) + 3); // using 3 bytes to mimic the case of 24 bit crc
    }

    stop_meas(dlsch_crc_stats);
    nrLDPC_TB_encoding_parameters_t *TB_parameters = &TBs[i];

    // The harq_pid is not unique among the active HARQ processes in the instance so we use i instead
    TB_parameters->harq_unique_pid = i;
    TB_parameters->BG = rel15->maintenance_parms_v3.ldpcBaseGraph;
    TB_parameters->A = A;
    TB_parameters->Kb = nr_segmentation(dlsch->b,
                                        dlsch->c,
                                        B,
                                        &TB_parameters->C,
                                        &TB_parameters->K,
                                        &TB_parameters->Z,
                                        &TB_parameters->F,
                                        TB_parameters->BG);

    if (TB_parameters->C > MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * rel15->nrOfLayers) {
      LOG_E(PHY, "nr_segmentation.c: too many segments %d, B %d\n", TB_parameters->C, B);
      return (-1);
    }
    num_segments += TB_parameters->C;
  }

  nrLDPC_segment_encoding_parameters_t segments[num_segments];
  memset(segments, 0, sizeof(segments));
  size_t segments_offset = 0;
  size_t dlsch_offset = 0;

  for (int i = 0; i < n_dlsch; i++) {
    NR_gNB_DLSCH_t *dlsch = &dlsch_array[i];
    const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *rel15 = &dlsch->pdsch_pdu->pdsch_pdu_rel15;

    nrLDPC_TB_encoding_parameters_t *TB_parameters = &TBs[i];

#ifdef DEBUG_DLSCH_CODING
    for (int r = 0; r < TB_parameters->C; r++) {
      LOG_D(PHY, "Encoder: B %d F %d \n", dlsch->B, TB_parameters->F);
      LOG_D(PHY, "start ldpc encoder segment %d/%d\n", r, TB_parameters->C);
      LOG_D(PHY, "input %d %d %d %d %d \n", dlsch->c[r][0], dlsch->c[r][1], dlsch->c[r][2], dlsch->c[r][3], dlsch->c[r][4]);
      for (int cnt = 0; cnt < 22 * (TB_parameters->Z) / 8; cnt++) {
        LOG_D(PHY, "%d ", dlsch->c[r][cnt]);
      }
      LOG_D(PHY, "\n");
    }
#endif

    int rbsize = dlsch->freq_alloc.num_rbs;
    TB_parameters->nb_rb = rbsize;
    TB_parameters->Qm = rel15->qamModOrder[0];
    TB_parameters->mcs = rel15->mcsIndex[0];
    TB_parameters->nb_layers = rel15->nrOfLayers;
    TB_parameters->rv_index = rel15->rvIndex[0];

#ifdef LDPC_CUDA
    TB_parameters->c_dev = (uint8_t **)dlsch->c_dev;
#endif
    int nb_re_dmrs =
        (rel15->dmrsConfigType == NFAPI_NR_DMRS_TYPE1) ? (6 * rel15->numDmrsCdmGrpsNoData) : (4 * rel15->numDmrsCdmGrpsNoData);
    TB_parameters->G = nr_get_G(rbsize,
                                rel15->NrOfSymbols,
                                nb_re_dmrs,
                                get_num_dmrs(rel15->dlDmrsSymbPos),
                                dlsch->unav_res,
                                rel15->qamModOrder[0],
                                rel15->nrOfLayers);

    TB_parameters->tbslbrm = rel15->maintenance_parms_v3.tbSizeLbrmBytes;
    TB_parameters->output = &output[dlsch_offset >> 3];
    TB_parameters->segments = &segments[segments_offset];

    for (int r = 0; r < TB_parameters->C; r++) {
      nrLDPC_segment_encoding_parameters_t *segment_parameters = &TB_parameters->segments[r];
      int E = nr_get_E(TB_parameters->G, TB_parameters->C, TB_parameters->Qm, rel15->nrOfLayers, r);
      if (E < 0)
        return -1;
      segment_parameters->c = dlsch->c[r];
      segment_parameters->E = E;

      reset_single_meas(&segment_parameters->ts_ldpc_encode);
    }

    segments_offset += TB_parameters->C;

    /* output and its parts for each dlsch should be aligned on 64 bytes (or 8 * 64 bits)
     * => dlsch_offset should remain a multiple of 8 * 64 with enough offset to fit each dlsch
     */
    const size_t dlsch_size = rbsize * gNB->frame_parms.symbols_per_slot * NR_NB_SC_PER_RB * rel15->qamModOrder[0] * rel15->nrOfLayers;
    dlsch_offset += ceil_mod(dlsch_size, 8 * 64);
  }

  nrLDPC_slot_encoding_parameters_t slot_parameters = {.frame = frame,
                                                       .slot = slot,
                                                       .nb_TBs = n_dlsch,
                                                       .threadPool = &gNB->threadPool,
                                                       .TBs = TBs};
  gNB->nrLDPC_coding_interface.nrLDPC_coding_encoder(&slot_parameters);

  int slot_type = nr_slot_select(&gNB->gNB_config, frame, slot);
  for (int i = 0; i < n_dlsch; i++) {
    nrLDPC_TB_encoding_parameters_t *TB_parameters = &TBs[i];
    for (int r = 0; r < TB_parameters->C; r++) {
      nrLDPC_segment_encoding_parameters_t *segment_parameters = &TB_parameters->segments[r];
      MERGE_SINGLE_MEAS_FULL_SLOT(&gNB->dlsch_ldpc_encode_stats, &segment_parameters->ts_ldpc_encode, slot_type, NR_DOWNLINK_SLOT);
    }
  }
  return 0;
}
