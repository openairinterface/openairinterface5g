/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level routines for decoding LDPC transport channels
 */

// [from gNB coding]
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"
#include "openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "openair1/PHY/CODING/coding_defs.h"
#include "log.h"
#include "defs.h"

#include <stdalign.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <pthread.h>
#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"

#include "nrLDPC_CUDA_shared_param.h"

// #define gNB_DEBUG_TRACE

#define OAI_LDPC_DECODER_MAX_NUM_LLR 27000 // 26112 // NR_LDPC_NCOL_BG1*NR_LDPC_ZMAX = 68*384
// #define DEBUG_CRC
#ifdef DEBUG_CRC
#define PRINT_CRC_CHECK(a) a
#else
#define PRINT_CRC_CHECK(a)
#endif

#define USE_GPU_FOR_RM_DEINTER 1

gpuStream_t decoderStreams[MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4];

int d_array_size = 0;

void launch_deinterleave_i16(int Qm, int E1, int E2, int C, int r_firstE2,int16_t* e, const int16_t* f,gpuStream_t *s,int8_t sidx);
int nr_rate_matching_ldpc_rx_cuda(uint32_t Tbslbrm,
                                  uint8_t BG,
                                  uint16_t Z,
                                  int16_t *d,
                                  int16_t *soft_input,
                                  int8_t *llr_buffer,
                                  uint32_t K,
                                  uint8_t C,
                                  uint8_t rvidx,
                                  uint8_t clear,
                                  uint32_t E1,
                                  uint32_t E2,
                                  uint32_t r_firstE2,
                                  uint32_t F,
                                  uint32_t Foffset,
                                  gpuStream_t *s,
                                  int8_t sidx);

extern int pageable, integrated;
extern int8_t *p_llr_dev, *p_out_dev;
int16_t **harq_d_array;
int16_t *harq_d_array_dev;
int16_t *harq_e_dev;
int16_t *harq_f_dev;
pthread_mutex_t decoder_mutex = PTHREAD_MUTEX_INITIALIZER;

extern int32_t LDPCdecoder_cuda(t_nrLDPC_dec_params *p_decParams,
                                int8_t *p_llr,
                                uint8_t *p_out,
                                t_nrLDPC_time_stats *p_profiler,
                                decode_abort_t *ab);

void nr_process_decode_segment_cuda(nrLDPC_TB_decoding_parameters_t *segs)
{
  // arg points to RDATA array (nrLDPC_decoding_parameters_t *RDATA)
  DevAssert(segs != NULL);

  // use seg0 as canonical
  const int C = segs->C;
  const int Z = segs->Z;
  const int Kc = segs->BG == 2 ? 52 : 68;
  const int K = segs->K;
  const int Kprime = K - segs->F;
  const int segLen = Kc * Z; // int16 length; after packing we store int8 [segLen]

#ifdef USE_GPU_FOR_RM_DEINTER
  int E1 = segs->E;
  int E2 = segs->E2;
  int r_firstE2 = segs->first_rE2;

  LOG_D(NR_PHY, "locking decoder (llr %p)\n", segs->llr);
  pthread_mutex_lock(&decoder_mutex);

  *segs->processedSegments = 0;

  // for PCIe GPU copy llrs to device memory
  if (!pageable && !integrated) {
    LOG_I(NR_PHY, "gpuMemcpyAsynch llr->harq_f_dev\n");
    gpuMemcpyAsync_or_fail(harq_f_dev,
                           segs->llr,
                           ((r_firstE2 * E1) + (C - r_firstE2) * E2) * sizeof(int16_t),
                           gpuMemcpyHostToDevice,
                           decoderStreams[0]);
  }
#if 0
  if (1/*segs->rv_index==2*/)
    for (int r=0;r<C;r++) {
      int roffset = r < r_firstE2 ? r*E1 : ((r_firstE2*E1)+(r-r_firstE2)*E2);
      int E = r < r_firstE2 ? E1 : E2;
      for (int i=0;i<16;i++) {
         printf("f(%d,%d) %d\n",r,i,segs->llr[roffset + i]);
      }
    }
#endif
  LOG_D(NR_PHY, "deinter: e %p llr %p\n", harq_e_dev, pageable || integrated ? segs->llr : harq_f_dev);
  launch_deinterleave_i16(segs->Qm,
                          E1,
                          E2,
                          C,
                          r_firstE2,
                          harq_e_dev,
                          pageable || integrated ? segs->llr : harq_f_dev,
                          decoderStreams,
                          0);
#if 0
  gpuError_t err;
  if (1/*segs->rv_index == 2*/) {
    int16_t elocal[E2];
    for (int r=0;r<C;r++) {
      int E = r<r_firstE2 ? E1 : E2;
      int r_off = r<r_firstE2 ? (r*E1) : ((r_firstE2*E1) + (r-r_firstE2)*E2);
      gpuMemcpyAsync_or_fail(elocal,&harq_e_dev[r_off],sizeof(elocal),gpuMemcpyDeviceToHost,decoderStreams[0]);
      for (int i=0;i<E;i++) {
	 if (elocal[i] != segs->llr[r_off + ((i<(E/2)) ? 2*i : (-E+1+2*i))])  { 
            printf("e(%d,%d,%d) %d",r,i,E,elocal[i]);
            printf("(%d**)\n",segs->llr[r_off + ((i<(E/2)) ? 2*i : -E+1+2*i)]); 
	 }
      }
    }
  }
#endif
  // printf("Running RM with id %d, d_to_be_cleared %d, rv_idx %d, Z %d, C %d, E1 %d, E2 %d, F %d,r_firstE2
  // %d\n",segs->harq_unique_pid,segs->d_to_be_cleared,segs->rv_index,Z,C,E1,E2,segs->F,r_firstE2);
#if 0
  if (segs->d_to_be_cleared == 1) err = gpuMemsetAsync(harq_d_array[segs->harq_unique_pid],0,C*68*Z*sizeof(int16_t),decoderStreams[0]);
  AssertFatal(err==gpuSuccess,"gpuMemsetAsync failed with error %s on harq_d_array[%d] %p for %d bytes\n",gpuGetErrorString(err),segs->harq_unique_pid,harq_d_array[segs->harq_unique_pid],C*68*Z*sizeof(int16_t));
  gpuMemsetAsync(p_llr_dev,0,C*68*Z*sizeof(int8_t),decoderStreams[0]);
  AssertFatal(err==gpuSuccess,"gpuMemsetAsync failed on p_llr_dev %p\n",p_llr_dev);
#endif
  AssertFatal(segs->harq_unique_pid < d_array_size, "harq_unique_pid %d > %d\n", segs->harq_unique_pid, d_array_size);
  nr_rate_matching_ldpc_rx_cuda(segs->tbslbrm,
                                segs->BG,
                                Z,
                                harq_d_array[segs->harq_unique_pid],
                                harq_e_dev,
                                p_llr_dev,
                                K,
                                C, // TB segments count
                                segs->rv_index,
                                segs->d_to_be_cleared,
                                E1,
                                E2,
                                r_firstE2,
                                segs->F,
                                Kprime - 2 * Z,
                                decoderStreams,
                                0);
  // The GPU rate recovery and the decoder place segment r at r * 68 * 384 (fixed maximum stride), not at r * Kc * Z
  const size_t segStride = 68 * 384;
  for (int r = 0; r < C; ++r) {
    gpuMemsetAsync(p_llr_dev + (size_t)r * segStride, 0, sizeof(int8_t) * 2 * Z, decoderStreams[0]);
#if 0
     int8_t llr_local[segLen];
     if (r==1 && segs->rv_index==2) {
       gpuMemcpyAsync_or_fail(llr_local,p_llr_dev+(size_t)r*segLen,sizeof(int8_t)*segLen,gpuMemcpyDeviceToHost,decoderStreams[0]);
       for (int i=0;i<segLen;i++) printf("llr(%d,%d,%d/%d) %d\n",segs->rv_index,r,i,segLen,llr_local[i]);
     }
#endif
  }

#else // USE_GPU_FOR_RM_DEINTER
  int16_t *z_local = (int16_t *)alloca(sizeof(int16_t) * segLen); // segLen is safe small
  for (int r = 0; r < C; ++r) {
    // deinterleave
    int16_t *harq_e = (int16_t *)alloca(sizeof(int16_t) * segs->segments[r].E);
    //    for (int i=0;i<segs->segments[r].E;i++) printf("llr_in[%d] %d\n",i,segs->segments[r].llr[i]);
    nr_deinterleaving_ldpc(segs->segments[r].E, segs->Qm, harq_e, segs->segments[r].llr);
    //    for (int i=0;i<16;i++) printf("harq_e[%d] %d\n",i,harq_e[i]);
    // rate matching
    if (nr_rate_matching_ldpc_rx(segs->tbslbrm,
                                 segs->BG,
                                 Z,
                                 segs->segments[r].d,
                                 harq_e,
                                 C, // TB segments count
                                 segs->rv_index,
                                 segs->segments[r].d_to_be_cleared,
                                 segs->segments[r].E,
                                 segs->F,
                                 Kprime - 2 * Z)
        == -1) {
      LOG_E(PHY, "rate matching failed seg %d\n", r);
      memset(segs->segments[r].c, 0, K);
      //*rdata->decodeSuccess = false;
      continue; // skip this segment
    }
    segs->segments[r].d_to_be_cleared = false;

    memset(z_local, 0, sizeof(int16_t) * 2 * Z);
    memset(z_local + Kprime, 127, sizeof(int16_t) * segs->F);
    memcpy(z_local + 2 * Z, segs->segments[r].d, (size_t)(Kprime - 2 * Z) * sizeof(int16_t));
    memcpy(z_local + K, segs->segments[r].d + (K - 2 * Z), (size_t)(Kc * Z - K) * sizeof(int16_t));

    // pack int16 -> int8 into llrBuffer[r * segLen]
    simde__m128i *pv = (simde__m128i *)z_local;
    simde__m128i *pl = (simde__m128i *)(llrBuffer + (size_t)r * segLen);
    int vecCount = ((Kc * Z) >> 4);
    for (int j = 0, idx = 0; j < vecCount; ++j, idx += 2) {
      pl[j] = simde_mm_packs_epi16(pv[idx], pv[idx + 1]);
    }
  }
#endif

  start_single_meas(&segs->ts_ldpc_decode[0]);

  t_nrLDPC_dec_params decParams = {.check_crc = check_crc};
  decParams.Z = Z;
  decParams.R = segs->R;
  decParams.BG = segs->BG;
  decParams.crc_type = crcType(C, segs->A);
  decParams.Kprime = lenWithCrc(C, segs->A);
  decParams.n_segments = C;
  decParams.outMode = nrLDPC_outMode_BIT;
  decParams.numMaxIter = segs->max_ldpc_iterations;

  LOG_D(NR_PHY, "decoder (llr %p): %d segments, Z %d, R %d \n", segs->llr, C, Z, segs->R);

  int decodeIterations = LDPCdecoder_cuda(&decParams, p_llr_dev, segs->c, NULL, segs->abort_decode);
  stop_single_meas(&segs->ts_ldpc_decode[0]);

  if (decodeIterations <= segs->max_ldpc_iterations) {
    *segs->processedSegments = C;
    for (int r = 0; r < C; r++)
      segs->decodeSuccess[r] = true;
    LOG_D(NR_PHY, "Set all segs->decodeSuccess to true\n");
  } else {
    memset(segs->c, 0, C * (K >> 3));
    for (int r = 0; r < C; r++)
      segs->decodeSuccess[r] = false;
    LOG_D(NR_PHY, "Set all segs->decodeSuccess to false\n");
  }
  LOG_D(NR_PHY, "unlocking decoder (llr %p)\n", segs->llr);
  pthread_mutex_unlock(&decoder_mutex);
}

#define MAXE 4 * 14 * 273 * 12 * 8 // 4 antennas, 14 symbols, 273 PRBs, 12 RE/prb, 8 bits/RE

void LDPCint_rm_init(int max_num_pxsch)
{
  LOG_I(NR_PHY, "RM init for %d pxsch\n", max_num_pxsch);
  LOG_I(NR_PHY, "Allocating device array for harq_d/harq_e \n");
  harq_e_dev = gpuMalloc_or_fail(MAXE * sizeof(int16_t));
  harq_d_array = malloc(sizeof(int16_t *) * max_num_pxsch);
  harq_d_array_dev = gpuMalloc_or_fail(sizeof(int16_t *) * max_num_pxsch);
  LOG_I(PHY, "Allocated %ld bytes for harq_d_array_dev @ %p\n", sizeof(int16_t *) * max_num_pxsch, harq_d_array_dev);
  for (int i = 0; i < max_num_pxsch; i++) {
    harq_d_array[i] = gpuMalloc_or_fail(MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * OAI_LDPC_DECODER_MAX_NUM_LLR * sizeof(int16_t));
    LOG_I(PHY,
          "Allocating %ld bytes for harq_d_array[%d] @ %p\n",
          MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * OAI_LDPC_DECODER_MAX_NUM_LLR * sizeof(int16_t),
          i,
          harq_d_array[i]);
  }
  gpuMemcpy(harq_d_array_dev, harq_d_array, sizeof(int16_t *) * max_num_pxsch, gpuMemcpyHostToDevice);
  if (!pageable && !integrated) {
    LOG_I(PHY, "Allocating device array for harq_f \n");
    harq_f_dev = gpuMalloc_or_fail(MAXE * sizeof(int16_t));
  }
  d_array_size = max_num_pxsch;
}

extern void LDPCinit_cuda(void);
extern void cuda_support_init();

int32_t nrLDPC_coding_init_cuda(int max_num_pxsch)
{
  cuda_support_init();

  LDPCinit_cuda();
  LDPCint_rm_init(max_num_pxsch);
  return 0;
}

void LDPCshutdown_cuda(void);

int32_t nrLDPC_coding_shutdown_cuda(void)
{
  LDPCshutdown_cuda();
  return 0;
}
