/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Shared parameters in CUDA implementation of LDPC decoder 
 */

#ifndef NRLDPC_CUDA_SHARED_PARAM_H_
#define NRLDPC_CUDA_SHARED_PARAM_H_

#include "PHY/gpu_compat.h"

#ifndef MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER
#define MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER 36
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RowLength 96 //Zc = 384/4 = 96

#define num_TotalBlocks_BG1_R13_Edge 316//based on number of Cn2Bn Msgs
#define num_TotalBlocks_BG1_R23_Edge 144
#define num_TotalBlocks_BG1_R89_Edge 79
#define num_TotalBlocks_llr_llrRes 22 //Only includes systematic bits

#define num_TotalBlocks_cn_BG1_R13_Node 46 //based on number of CNs
#define num_TotalBlocks_bn_BG1_R13_Node 68 //based on number of BNs
#define num_TotalBlocks_cn_BG1_R23_Node 13
#define num_TotalBlocks_bn_BG1_R23_Node 35
#define num_TotalBlocks_cn_BG1_R89_Node 5
#define num_TotalBlocks_bn_BG1_R89_Node 27

#define JETSON_ORIN 
//#define GH200

#if defined(GH200)
#define NodeEdge_Switch_Cn_R13 32
#define NodeEdge_Switch_Bn_R13 10
#define NodeEdge_Switch_Cn_R23 32
#define NodeEdge_Switch_Bn_R23 12
#define NodeEdge_Switch_Cn_R89 48
#define NodeEdge_Switch_Bn_R89 24
#elif defined(JETSON_ORIN)
#define NodeEdge_Switch_Cn_R13 3
#define NodeEdge_Switch_Bn_R13 1
#define NodeEdge_Switch_Cn_R23 3
#define NodeEdge_Switch_Bn_R23 1
#define NodeEdge_Switch_Cn_R89 4
#define NodeEdge_Switch_Bn_R89 2
#endif

#ifdef __cplusplus
}
#endif

typedef struct KernelLaunchConfig {
  dim3 grid;
  dim3 block;
} KernelLaunchConfig;

typedef struct {
  int idxBn;
  int idxCn;
  int preBuf;
  int circShift;
  int8_t dd;
} DumpEntry;

typedef struct {
  int8_t* p_llr_ptr;
  int8_t* p_out_ptr;
} ldpc_cuda_bridge_t;

// early termination CRC kernel: threads per code block, bytes of a code block's hard decision (K = 22 Zc bits at most)
// and bytes per thread
#define LDPC_ET_THREADS 256
#define LDPC_ET_MAX_KB (22 * 384 / 8)
#define LDPC_ET_MAX_L ((LDPC_ET_MAX_KB + LDPC_ET_THREADS - 1) / LDPC_ET_THREADS)

// launch-dimension slots (Kdim_* arrays), indexed by stream: up to 8 decoder contexts, then the background graph recorder
#define LDPC_CUDA_KDIM_SLOTS 9
#define LDPC_ET_REC_SLOT 8

// Device-side early termination state, one per decoder context. The graphs are recorded for a number of code blocks
// rounded up to a bucket; the TB parameters below are set before each decode by ldpc_et_setup_kernel().
typedef struct {
  uint8_t done[MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4]; // code block passed its CRC (or padding): skipped by every kernel
  uint32_t nbytes; // bytes covered by the CRC check (Kprime / 8)
  uint32_t crc_deg, crc_low; // CRC generator x^deg + low
  const uint32_t* xpow; // CRC combining factors (see ldpc_et_crc_kernel())
} ldpc_cuda_et_state_t;

#endif /* NRLDPC_CUDA_SHARED_PARAM_H_ */
