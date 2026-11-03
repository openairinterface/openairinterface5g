/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief CUDA implementation of NR LDPC Decoder (BG1) with CUDA Graphs support.
 */

#include "PHY/gpu_compat.h"
#include "PHY/gpu_simd_intrin_compat.h" // GPU_SHFL_ALL_LANES
#include <stdint.h>
#include <stdio.h>
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"

#include "nrLDPC_CUDA_lut.h"
#include "nrLDPC_CUDA_CnProcKernel_BG1.h"
#include "nrLDPC_CUDA_BnProcKernel_BG1.h"
#include "nrLDPC_CUDA_mPassKernel_BG1.h"
#include "nrLDPC_CUDA_shared_param.h"
#include "nrLDPC_coding_cuda_ctx.h"

#ifndef JETSON_TARGET
#define CUDA_THREADS 1024
#define CUDA_BLOCKS_R13 30
#define CUDA_BLOCKS_R23 14
#else
#define CUDA_THREADS 128
#define CUDA_BLOCKS_R13 237 // ceil(30336/128)
#define CUDA_BLOCKS_R23 108 // ceil(13824/128)
#endif
// Edge
KernelLaunchConfig Kdim_R13_Edge[LDPC_CUDA_KDIM_SLOTS]; //
KernelLaunchConfig Kdim_R23_Edge[LDPC_CUDA_KDIM_SLOTS];
KernelLaunchConfig Kdim_R89_Edge[LDPC_CUDA_KDIM_SLOTS];
KernelLaunchConfig Kdim_llr[LDPC_CUDA_KDIM_SLOTS];

// Node
KernelLaunchConfig Kdim_cn_R13_Node[LDPC_CUDA_KDIM_SLOTS]; //
KernelLaunchConfig Kdim_bn_R13_Node[LDPC_CUDA_KDIM_SLOTS]; //
KernelLaunchConfig Kdim_cn_R23_Node[LDPC_CUDA_KDIM_SLOTS];
KernelLaunchConfig Kdim_bn_R23_Node[LDPC_CUDA_KDIM_SLOTS];
KernelLaunchConfig Kdim_cn_R89_Node[LDPC_CUDA_KDIM_SLOTS];
KernelLaunchConfig Kdim_bn_R89_Node[LDPC_CUDA_KDIM_SLOTS];

// === CUDA Error Checking ===
// Wrap any CUDA API call with CHECK(...) to automatically print error info with file and line number
// Example usage: CHECK(gpuMalloc(&ptr, size));
#define CHECK(call) ErrorCheck((call), __FILE__, __LINE__)
/**
 * @brief Checks CUDA error status and prints detailed diagnostic info if an error occurred.
 *
 * @param error_code The CUDA error code returned from a CUDA runtime API call.
 * @param filename   The name of the source file where the error occurred.
 * @param lineNumber The line number in the source file where the error occurred.
 * @return gpuError_t Returns the same error code passed in, for optional further handling.
 */
inline gpuError_t ErrorCheck(gpuError_t error_code, const char *filename, int lineNumber)
{
  if (error_code != gpuSuccess) {
    printf("[CUDA ERROR] %s (%d): %s\nOccurred in file: %s at line %d\n",
           gpuGetErrorName(error_code),
           error_code,
           gpuGetErrorString(error_code),
           filename,
           lineNumber);
  }
  return error_code;
}

//-----------------------------------------↓↓↓ R13 ↓↓↓----------------------------------------
__global__ void cnProcKernel_BG1_R13_int8_Edge(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R13_Edge)
    return;

  uint32_t groupIdx = lut_CnGrpIdx_BG1_R13_Edge[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R13_Edge[row] - 1;
  uint32_t MsgIdx = lut_CnMsgIdx_BG1_R13_Edge[row] - 1;
  uint32_t InnerOffset = d_lut_startAddrCnGroups_BG1[groupIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t idxBn = cn_bn_map_BG1_Z_R13[row][0];
  uint32_t circShift = cn_bn_map_BG1_Z_R13[row][ZcIdx];

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + InnerOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  switch (groupIdx) {
    case 0:
      cnProcKernel_BG1_int8_G3(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 1:
      cnProcKernel_BG1_int8_G4(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 2:
      cnProcKernel_BG1_int8_G5(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 3:
      cnProcKernel_BG1_int8_G6(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 4:
      cnProcKernel_BG1_int8_G7(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 5:
      cnProcKernel_BG1_int8_G8(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 6:
      cnProcKernel_BG1_int8_G9(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 7:
      cnProcKernel_BG1_int8_G10(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 8:
      cnProcKernel_BG1_int8_G19(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
  }
}

__global__ void cnProcKernel_BG1_R13_int8_Node(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_cn_BG1_R13_Node)
    return;

  uint32_t CnGrpIdx = lut_CnGrpIdx_BG1_R13_Node[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R13_Node[row] - 1;
  uint32_t InnerOffset = d_lut_startAddrCnGroups_BG1[CnGrpIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t Cn2MsgStartIdx = lut_CnStartMsgIdx_BG1_R13_Node[row];
  uint32_t CnGrpIdxNum = d_lut_numBnInCnGroups_BG1_R13[CnGrpIdx];
  uint32_t CnNumInGrp = d_lut_numCnInCnGroups_BG1_R13[CnGrpIdx]; // R13 and R23 use the same lut here

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + InnerOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  cnProcKernel_BG1_int8_Gn_R13_node(p_cnProcBuf, p_bnProcBuf, lane, CnIdx, CnNumInGrp, CnGrpIdxNum, Cn2MsgStartIdx, Zc, ZcIdx);
}

void nrLDPC_cnProc_BG1_R13_cuda_stream_core(int8_t *cnProcBuf,
                                            int8_t *bnProcBuf,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Cn_R13) {
    cnProcKernel_BG1_R13_int8_Node<<<Kdim_cn_R13_Node[CudaStreamIdx].grid,
                                     Kdim_cn_R13_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  } else {
    cnProcKernel_BG1_R13_int8_Edge<<<Kdim_R13_Edge[CudaStreamIdx].grid,
                                     Kdim_R13_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  }

  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R13_int8_Edge(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R13_Edge)
    return;
  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R13_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R13_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R13_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R13[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R13[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R13[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R13[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge(p_bnProcBuf_Grp,
                                (int8_t *)p_cnProcBuf_Grp,
                                p_llrProcBuf_Grp,
                                (int8_t *)p_llrRes_Grp,
                                lane,
                                GrpIdx,
                                MsgIdx,
                                BnIdx,
                                GrpNum,
                                circShift,
                                Zc);
}

__global__ void bnProcKernel_BG1_R13_int8_Node(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R13_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R13_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R13_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R13[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R13[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R13_Node[row];
  // uint32_t circShift = bn_cn_map_BG1_Z_R13[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_R13(p_bnProcBuf_Grp,
                                    (int8_t *)p_cnProcBuf_Grp,
                                    p_llrProcBuf_Grp,
                                    (int8_t *)p_llrRes_Grp,
                                    lane,
                                    BnGrpIdx,
                                    BnIdx,
                                    GrpNum,
                                    Bn2MsgStartIdx,
                                    Zc,
                                    ZcIdx);
}

void nrLDPC_bnProc_BG1_R13_cuda_stream_core(int8_t *bnProcBuf,
                                            int8_t *cnProcBuf,
                                            int8_t *llrProcBuf,
                                            int8_t *llrRes,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R13) {
    bnProcKernel_BG1_R13_int8_Node<<<Kdim_bn_R13_Node[CudaStreamIdx].grid,
                                     Kdim_bn_R13_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R13_int8_Edge<<<Kdim_R13_Edge[CudaStreamIdx].grid,
                                     Kdim_R13_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R13_int8_Edge_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R13_Edge)
    return;
  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R13_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R13_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R13_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R13[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R13[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R13[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R13[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     GrpIdx,
                                     MsgIdx,
                                     BnIdx,
                                     GrpNum,
                                     circShift,
                                     Zc);
}

__global__ void bnProcKernel_BG1_R13_int8_Node_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R13_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R13_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R13_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R13[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R13[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R13_Node[row];
  // uint32_t circShift = bn_cn_map_BG1_Z_R13[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R13[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     BnGrpIdx,
                                     BnIdx,
                                     GrpNum,
                                     Bn2MsgStartIdx,
                                     Zc,
                                     ZcIdx);
}

void nrLDPC_bnProc_BG1_R13_cuda_stream_core_last(int8_t *bnProcBuf,
                                                 int8_t *cnProcBuf,
                                                 int8_t *llrProcBuf,
                                                 int8_t *llrRes,
                                                 uint32_t n_segments,
                                                 uint32_t Z,
                                                 uint32_t ZcIdx,
                                                 gpuStream_t *streams,
                                                 int8_t CudaStreamIdx,
                                                 const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R13) {
    bnProcKernel_BG1_R13_int8_Node_last<<<Kdim_bn_R13_Node[CudaStreamIdx].grid,
                                          Kdim_bn_R13_Node[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R13_int8_Edge_last<<<Kdim_R13_Edge[CudaStreamIdx].grid,
                                          Kdim_R13_Edge[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

//-----------------------------------------↑↑↑ R13 ↑↑↑----------------------------------------

//-----------------------------------------↓↓↓ R23 ↓↓↓----------------------------------------

__global__ void cnProcKernel_BG1_R23_int8_Edge(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R23_Edge)
    return;

  uint32_t groupIdx = lut_CnGrpIdx_BG1_R23_Edge[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R23_Edge[row] - 1;
  uint32_t MsgIdx = lut_CnMsgIdx_BG1_R23_Edge[row] - 1;
  uint32_t inOffset = d_lut_startAddrCnGroups_BG1[groupIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t idxBn = cn_bn_map_BG1_Z_R23[row][0];
  uint32_t circShift = cn_bn_map_BG1_Z_R23[row][ZcIdx];

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + inOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  switch (groupIdx) {
    case 0:
      cnProcKernel_BG1_int8_G3(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 1:
      printf("Shouldn't see case 1 in R23");
      break;
    case 2:
      printf("Shouldn't see case 2 in R23");
      break;
    case 3:
      printf("Shouldn't see case 3 in R23");
      break;
    case 4:
      cnProcKernel_BG1_int8_G7(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 5:
      cnProcKernel_BG1_int8_G8(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 6:
      cnProcKernel_BG1_int8_G9(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 7:
      cnProcKernel_BG1_int8_G10(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 8:
      cnProcKernel_BG1_int8_G19(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
  }
}

__global__ void cnProcKernel_BG1_R23_int8_Node(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_cn_BG1_R23_Node)
    return;

  uint32_t CnGrpIdx = lut_CnGrpIdx_BG1_R23_Node[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R23_Node[row] - 1;
  uint32_t InnerOffset = d_lut_startAddrCnGroups_BG1[CnGrpIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t Cn2MsgStartIdx = lut_CnStartMsgIdx_BG1_R23_Node[row];
  uint32_t CnGrpIdxNum = d_lut_numBnInCnGroups_BG1_R13[CnGrpIdx]; // R13 and R23 use the same lut here
  uint32_t CnNumInGrp = d_lut_numCnInCnGroups_BG1_R13[CnGrpIdx]; // R13 and R23 use the same lut here

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + InnerOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  cnProcKernel_BG1_int8_Gn_R23_node(p_cnProcBuf, p_bnProcBuf, lane, CnIdx, CnNumInGrp, CnGrpIdxNum, Cn2MsgStartIdx, Zc, ZcIdx);
}

void nrLDPC_cnProc_BG1_R23_cuda_stream_core(int8_t *cnProcBuf,
                                            int8_t *bnProcBuf,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Cn_R23) {
    cnProcKernel_BG1_R23_int8_Node<<<Kdim_cn_R23_Node[CudaStreamIdx].grid,
                                     Kdim_cn_R23_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  } else {
    cnProcKernel_BG1_R23_int8_Edge<<<Kdim_R23_Edge[CudaStreamIdx].grid,
                                     Kdim_R23_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R23_int8_Edge(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R23_Edge)
    return;

  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R23_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R23_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R23_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R23[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R23[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R23[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R23[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge(p_bnProcBuf_Grp,
                                (int8_t *)p_cnProcBuf_Grp,
                                p_llrProcBuf_Grp,
                                (int8_t *)p_llrRes_Grp,
                                lane,
                                GrpIdx,
                                MsgIdx,
                                BnIdx,
                                GrpNum,
                                circShift,
                                Zc);
}

__global__ void bnProcKernel_BG1_R23_int8_Node(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R23_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R23_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R23_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R23[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R23[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R23_Node[row];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_R23(p_bnProcBuf_Grp,
                                    (int8_t *)p_cnProcBuf_Grp,
                                    p_llrProcBuf_Grp,
                                    (int8_t *)p_llrRes_Grp,
                                    lane,
                                    BnGrpIdx,
                                    BnIdx,
                                    GrpNum,
                                    Bn2MsgStartIdx,
                                    Zc,
                                    ZcIdx);
}

void nrLDPC_bnProc_BG1_R23_cuda_stream_core(int8_t *bnProcBuf,
                                            int8_t *cnProcBuf,
                                            int8_t *llrProcBuf,
                                            int8_t *llrRes,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R23) {
    bnProcKernel_BG1_R23_int8_Node<<<Kdim_bn_R23_Node[CudaStreamIdx].grid,
                                     Kdim_bn_R23_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R23_int8_Edge<<<Kdim_R23_Edge[CudaStreamIdx].grid,
                                     Kdim_R23_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R23_int8_Edge_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R23_Edge)
    return;

  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R23_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R23_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R23_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R23[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R23[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R23[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R23[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     GrpIdx,
                                     MsgIdx,
                                     BnIdx,
                                     GrpNum,
                                     circShift,
                                     Zc);
}

__global__ void bnProcKernel_BG1_R23_int8_Node_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R23_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R23_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R23_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R23[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R23[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R23_Node[row];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R23[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     BnGrpIdx,
                                     BnIdx,
                                     GrpNum,
                                     Bn2MsgStartIdx,
                                     Zc,
                                     ZcIdx);
}

void nrLDPC_bnProc_BG1_R23_cuda_stream_core_last(int8_t *bnProcBuf,
                                                 int8_t *cnProcBuf,
                                                 int8_t *llrProcBuf,
                                                 int8_t *llrRes,
                                                 uint32_t n_segments,
                                                 uint32_t Z,
                                                 uint32_t ZcIdx,
                                                 gpuStream_t *streams,
                                                 int8_t CudaStreamIdx,
                                                 const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R23) {
    bnProcKernel_BG1_R23_int8_Node_last<<<Kdim_bn_R23_Node[CudaStreamIdx].grid,
                                          Kdim_bn_R23_Node[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R23_int8_Edge_last<<<Kdim_R23_Edge[CudaStreamIdx].grid,
                                          Kdim_R23_Edge[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}
//-----------------------------------------↑↑↑ R23 ↑↑↑----------------------------------------
//-----------------------------------------↓↓↓ R89 ↓↓↓----------------------------------------

__global__ void cnProcKernel_BG1_R89_int8_Edge(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R89_Edge)
    return;

  uint32_t groupIdx = lut_CnGrpIdx_BG1_R89_Edge[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R89_Edge[row] - 1;
  uint32_t MsgIdx = lut_CnMsgIdx_BG1_R89_Edge[row] - 1;
  uint32_t inOffset = d_lut_startAddrCnGroups_BG1[groupIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t idxBn = cn_bn_map_BG1_Z_R89[row][0];
  uint32_t circShift = cn_bn_map_BG1_Z_R89[row][ZcIdx];

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + inOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  switch (groupIdx) {
    case 0:
      cnProcKernel_BG1_int8_G3(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
    case 1:
      printf("Shouldn't see case 1 in R89");
      break;
    case 2:
      printf("Shouldn't see case 2 in R89");
      break;
    case 3:
      printf("Shouldn't see case 3 in R89");
      break;
    case 4:
      printf("Shouldn't see case 4 in R89");
      break;
    case 5:
      printf("Shouldn't see case 5 in R89");
      break;
    case 6:
      printf("Shouldn't see case 6 in R89");
      break;
    case 7:
      printf("Shouldn't see case 7 in R89");
      break;
    case 8:
      cnProcKernel_BG1_int8_G19(p_cnProcBuf, p_bnProcBuf, MsgIdx, lane, idxBn, circShift, Zc);
      break;
  }
}

__global__ void cnProcKernel_BG1_R89_int8_Node(const int8_t *__restrict__ d_cnBufAll,
                                               int8_t *__restrict__ d_bnBufAll,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_cn_BG1_R89_Node)
    return;

  uint32_t CnGrpIdx = lut_CnGrpIdx_BG1_R89_Node[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R89_Node[row] - 1;
  uint32_t InnerOffset = d_lut_startAddrCnGroups_BG1[CnGrpIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t Cn2MsgStartIdx = lut_CnStartMsgIdx_BG1_R89_Node[row];
  uint32_t CnGrpIdxNum = d_lut_numBnInCnGroups_BG1_R13[CnGrpIdx]; // R13, R23 and R89 use the same lut here
  uint32_t CnNumInGrp = d_lut_numCnInCnGroups_BG1_R13[CnGrpIdx]; // R13, R23 and R89 use the same lut here

  const int8_t *p_cnProcBuf = (const int8_t *)(d_cnBufAll + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + InnerOffset);
  int8_t *p_bnProcBuf = (int8_t *)(d_bnBufAll + segIdx * NR_LDPC_SIZE_BN_PROC_BUF);

  cnProcKernel_BG1_int8_Gn_R89_node(p_cnProcBuf, p_bnProcBuf, lane, CnIdx, CnNumInGrp, CnGrpIdxNum, Cn2MsgStartIdx, Zc, ZcIdx);
}

void nrLDPC_cnProc_BG1_R89_cuda_stream_core(int8_t *cnProcBuf,
                                            int8_t *bnProcBuf,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Cn_R89) {
    cnProcKernel_BG1_R89_int8_Node<<<Kdim_cn_R89_Node[CudaStreamIdx].grid,
                                     Kdim_cn_R89_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  } else {
    cnProcKernel_BG1_R89_int8_Edge<<<Kdim_R89_Edge[CudaStreamIdx].grid,
                                     Kdim_R89_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(cnProcBuf, bnProcBuf, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R89_int8_Edge(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R89_Edge)
    return;

  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R89_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R89_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R89_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R89[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R89[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R89[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R89[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge(p_bnProcBuf_Grp,
                                (int8_t *)p_cnProcBuf_Grp,
                                p_llrProcBuf_Grp,
                                (int8_t *)p_llrRes_Grp,
                                lane,
                                GrpIdx,
                                MsgIdx,
                                BnIdx,
                                GrpNum,
                                circShift,
                                Zc);
}

__global__ void bnProcKernel_BG1_R89_int8_Node(const int8_t *__restrict__ d_bnProcBuf,
                                               int8_t *__restrict__ d_cnProcBuf,
                                               int8_t *__restrict__ d_llrProcBuf,
                                               int8_t *__restrict__ d_llrRes,
                                               uint32_t Zc,
                                               uint32_t ZcIdx,
                                               const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R89_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R89_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R89_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R89[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R89[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R89_Node[row];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_R89(p_bnProcBuf_Grp,
                                    (int8_t *)p_cnProcBuf_Grp,
                                    p_llrProcBuf_Grp,
                                    (int8_t *)p_llrRes_Grp,
                                    lane,
                                    BnGrpIdx,
                                    BnIdx,
                                    GrpNum,
                                    Bn2MsgStartIdx,
                                    Zc,
                                    ZcIdx);
}

void nrLDPC_bnProc_BG1_R89_cuda_stream_core(int8_t *bnProcBuf,
                                            int8_t *cnProcBuf,
                                            int8_t *llrProcBuf,
                                            int8_t *llrRes,
                                            uint32_t n_segments,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx,
                                            const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R89) {
    bnProcKernel_BG1_R89_int8_Node<<<Kdim_bn_R89_Node[CudaStreamIdx].grid,
                                     Kdim_bn_R89_Node[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R89_int8_Edge<<<Kdim_R89_Edge[CudaStreamIdx].grid,
                                     Kdim_R89_Edge[CudaStreamIdx].block,
                                     0,
                                     streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}

__global__ void bnProcKernel_BG1_R89_int8_Edge_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_BG1_R89_Edge)
    return;

  uint32_t GrpIdx = lut_BnGrpIdx_BG1_R89_Edge[row];
  uint32_t MsgIdx = lut_BnMsgIdx_BG1_R89_Edge[row] - 1;
  uint32_t BnIdx = lut_BnIdx_BG1_R89_Edge[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R89[GrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R89[GrpIdx - 1];
  uint32_t circShift = bn_cn_map_BG1_Z_R89[row][ZcIdx];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + bn_cn_map_BG1_Z_R89[row][0]);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Edge_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     GrpIdx,
                                     MsgIdx,
                                     BnIdx,
                                     GrpNum,
                                     circShift,
                                     Zc);
}

__global__ void bnProcKernel_BG1_R89_int8_Node_last(const int8_t *__restrict__ d_bnProcBuf,
                                                    int8_t *__restrict__ d_cnProcBuf,
                                                    int8_t *__restrict__ d_llrProcBuf,
                                                    int8_t *__restrict__ d_llrRes,
                                                    uint32_t Zc,
                                                    uint32_t ZcIdx,
                                                    const uint8_t *skip)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  if (row >= num_TotalBlocks_bn_BG1_R89_Node)
    return;
  uint32_t BnGrpIdx = lut_BnGrpIdx_BG1_R89_Node[row];
  uint32_t BnIdx = lut_BnIdx_BG1_R89_Node[row];
  uint32_t BnToAddrIdx = lut_BnToAddrIdx_BG1_R89[BnGrpIdx - 1];
  uint32_t GrpNum = d_lut_numBnInBnGroups_BG1_R89[BnGrpIdx - 1];
  uint32_t Bn2MsgStartIdx = lut_BnStartMsgIdx_BG1_R89_Node[row];
  const uint32_t baseBn = (BnIdx - 1) * NR_LDPC_ZMAX;

  const int8_t *p_bnProcBuf_Grp =
      (const int8_t *)(d_bnProcBuf + baseBn + segIdx * NR_LDPC_SIZE_BN_PROC_BUF + d_lut_startAddrBnGroups_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_cnProcBuf_Grp = (const int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF);
  const int8_t *p_llrProcBuf_Grp =
      (const int8_t *)(d_llrProcBuf + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);
  const int8_t *p_llrRes_Grp =
      (const int8_t *)(d_llrRes + baseBn + segIdx * NR_LDPC_MAX_NUM_LLR + d_lut_startAddrBnGroupsLlr_BG1_R89[BnToAddrIdx - 1]);

  bnProcKernel_BG1_int8_Gn_Node_last(p_bnProcBuf_Grp,
                                     (int8_t *)p_cnProcBuf_Grp,
                                     p_llrProcBuf_Grp,
                                     (int8_t *)p_llrRes_Grp,
                                     lane,
                                     BnGrpIdx,
                                     BnIdx,
                                     GrpNum,
                                     Bn2MsgStartIdx,
                                     Zc,
                                     ZcIdx);
}

void nrLDPC_bnProc_BG1_R89_cuda_stream_core_last(int8_t *bnProcBuf,
                                                 int8_t *cnProcBuf,
                                                 int8_t *llrProcBuf,
                                                 int8_t *llrRes,
                                                 uint32_t n_segments,
                                                 uint32_t Z,
                                                 uint32_t ZcIdx,
                                                 gpuStream_t *streams,
                                                 int8_t CudaStreamIdx,
                                                 const uint8_t *skip)
{
  if (n_segments > NodeEdge_Switch_Bn_R89) {
    bnProcKernel_BG1_R89_int8_Node_last<<<Kdim_bn_R89_Node[CudaStreamIdx].grid,
                                          Kdim_bn_R89_Node[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  } else {
    bnProcKernel_BG1_R89_int8_Edge_last<<<Kdim_R89_Edge[CudaStreamIdx].grid,
                                          Kdim_R89_Edge[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(bnProcBuf, cnProcBuf, llrProcBuf, llrRes, Z, ZcIdx, skip);
  }
  CHECK(gpuGetLastError());
}
//-----------------------------------------↑↑↑ R89 ↑↑↑----------------------------------------
//-------------------------------------↓↓↓ general R ↓↓↓----------------------------------------
__global__ void llrPreProc_Kernel_BG1_int8_BIG_stream(ldpc_cuda_bridge_t *d_buffer,
                                                      uint32_t numLLR,
                                                      int8_t *__restrict__ d_llrProcBuf,
                                                      int8_t *__restrict__ d_cnProcBuf,
                                                      uint32_t Zc,
                                                      uint32_t ZcIdx,
                                                      uint32_t R)
{
  uint32_t lane = threadIdx.x;
  uint32_t row = (blockIdx.x << 2) + threadIdx.y;

  uint32_t segIdx = blockIdx.y;
  if (row >= num_TotalBlocks_BG1_R13_Edge)
    return;

  uint32_t groupIdx = lut_CnGrpIdx_BG1_R13_Edge[row] - 1;
  uint32_t CnIdx = lut_CnIdx_BG1_R13_Edge[row] - 1;
  uint32_t MsgIdx = lut_CnMsgIdx_BG1_R13_Edge[row] - 1;
  uint32_t InnerOffset = d_lut_startAddrCnGroups_BG1[groupIdx] + NR_LDPC_ZMAX * CnIdx;
  uint32_t idxBn = llr_cn_preProc_map_BG1_Z_R13[row][0];
  uint32_t circShift = llr_cn_preProc_map_BG1_Z_R13[row][ZcIdx];

  int8_t *d_llr = d_buffer->p_llr_ptr;
  int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + segIdx * NR_LDPC_SIZE_CN_PROC_BUF + InnerOffset);
  int8_t *p_llr = (int8_t *)(d_llr + segIdx * 68 * NR_LDPC_ZMAX);
  int8_t *p_llrProcBuf = (int8_t *)(d_llrProcBuf + segIdx * NR_LDPC_MAX_NUM_LLR);

  llrPreProc_Kernel_BG1_int8_Gn_stream(p_llr, p_llrProcBuf, p_cnProcBuf, MsgIdx, lane, row, idxBn, groupIdx, circShift, Zc, R);
}

void nrLDPC_llrPreProc_BG1_cuda_stream_core(ldpc_cuda_bridge_t *buffer,
                                            uint32_t numLLR,
                                            int8_t *llrProcBuf,
                                            int8_t *cnProcBuf,
                                            uint32_t Z,
                                            uint32_t ZcIdx,
                                            uint32_t R,
                                            gpuStream_t *streams,
                                            int8_t CudaStreamIdx)
{
  llrPreProc_Kernel_BG1_int8_BIG_stream<<<Kdim_R13_Edge[CudaStreamIdx].grid,
                                          Kdim_R13_Edge[CudaStreamIdx].block,
                                          0,
                                          streams[CudaStreamIdx]>>>(buffer, numLLR, llrProcBuf, cnProcBuf, Z, ZcIdx, R);

  CHECK(gpuGetLastError());
}

__global__ void llrOutPut_Kernel_BG1_int8_BIG_stream(uint32_t R,
                                                     int8_t *d_llrRes,
                                                     uint32_t Zc,
                                                     e_nrLDPC_outMode outMode,
                                                     ldpc_cuda_bridge_t *d_buffer,
                                                     uint32_t numLLR,
                                                     uint32_t K,
                                                     const uint8_t *skip)
{
  uint32_t segIdx = blockIdx.y;
  if (skip && skip[segIdx]) // code block already decoded (early termination)
    return;

  int8_t *d_out = d_buffer->p_out_ptr;

  int8_t *p_llrRes = (int8_t *)(d_llrRes + segIdx * NR_LDPC_MAX_NUM_LLR);
  // output
  if (outMode == nrLDPC_outMode_BIT) {
    int8_t *p_out = d_out + segIdx * (K >> 3);
    llr2bitPacked_Kernel_BG1_int8(R, (uint8_t *)p_out, p_llrRes, numLLR, Zc);
  } else if (outMode == nrLDPC_outMode_BITINT8) {
    int8_t *p_out = d_out + segIdx * K;
    llr2bit_Kernel_BG1_int8(R, (uint8_t *)p_out, p_llrRes, numLLR, Zc);
  }
}
void nrLDPC_OutPut_BG1_cuda_stream_core(int8_t *llrRes,
                                        uint32_t Z,
                                        uint8_t R,
                                        e_nrLDPC_outMode outMode,
                                        ldpc_cuda_bridge_t *buffer,
                                        uint32_t numLLR,
                                        uint32_t K,
                                        gpuStream_t *streams,
                                        int8_t CudaStreamIdx,
                                        const uint8_t *skip)
{
  llrOutPut_Kernel_BG1_int8_BIG_stream<<<Kdim_llr[CudaStreamIdx].grid, Kdim_llr[CudaStreamIdx].block, 0, streams[CudaStreamIdx]>>>(
      R,
      llrRes,
      Z,
      outMode,
      buffer,
      numLLR,
      K,
      skip);

  CHECK(gpuGetLastError());
}
//---------------------------------↑↑↑ general R ↑↑↑----------------------------------------
static inline uint32_t get_lut_col_index_host(uint32_t Zc)
{
  switch (Zc) {
    case 128:
      return 9;
    case 144:
      return 1;
    case 160:
      return 5;
    case 176:
      return 2;
    case 192:
      return 10;
    case 208:
      return 3;
    case 224:
      return 6;
    case 240:
      return 4;
    case 256:
      return 12;
    case 288:
      return 7;
    case 320:
      return 11;
    case 352:
      return 8;
    case 384:
      return 13;
    default:
      return 0; // Error or Fallback
  }
}
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//-----------------------CUDA Scheduler Area------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------

#define ENQUEUE_LDPC_DECODER_SEQUENCE(q_streams, q_idx)                                                             \
  do {                                                                                                              \
    uint8_t ZcIdx = get_lut_col_index_host(Z);                                                                      \
    nrLDPC_llrPreProc_BG1_cuda_stream_core(buffer, numLLR, llrProcBuf, cnProcBuf, Z, ZcIdx, R, q_streams, q_idx);   \
    if (R == 13) {                                                                                                  \
      for (int i = 0; i <= numMaxIter; i++) {                                                                       \
        nrLDPC_cnProc_BG1_R13_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, NULL); \
        if (i == numMaxIter)                                                                                        \
          nrLDPC_bnProc_BG1_R13_cuda_stream_core_last(bnProcBuf,                                                    \
                                                      cnProcBuf,                                                    \
                                                      llrProcBuf,                                                   \
                                                      llrRes,                                                       \
                                                      n_segments,                                                   \
                                                      Z,                                                            \
                                                      ZcIdx,                                                        \
                                                      q_streams,                                                    \
                                                      q_idx,                                                        \
                                                      NULL);                                                        \
        else                                                                                                        \
          nrLDPC_bnProc_BG1_R13_cuda_stream_core(bnProcBuf,                                                         \
                                                 cnProcBuf,                                                         \
                                                 llrProcBuf,                                                        \
                                                 llrRes,                                                            \
                                                 n_segments,                                                        \
                                                 Z,                                                                 \
                                                 ZcIdx,                                                             \
                                                 q_streams,                                                         \
                                                 q_idx,                                                             \
                                                 NULL);                                                             \
      }                                                                                                             \
    } else if (R == 23) {                                                                                           \
      for (int i = 0; i <= numMaxIter; i++) {                                                                       \
        nrLDPC_cnProc_BG1_R23_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, NULL); \
        if (i == numMaxIter)                                                                                        \
          nrLDPC_bnProc_BG1_R23_cuda_stream_core_last(bnProcBuf,                                                    \
                                                      cnProcBuf,                                                    \
                                                      llrProcBuf,                                                   \
                                                      llrRes,                                                       \
                                                      n_segments,                                                   \
                                                      Z,                                                            \
                                                      ZcIdx,                                                        \
                                                      q_streams,                                                    \
                                                      q_idx,                                                        \
                                                      NULL);                                                        \
        else                                                                                                        \
          nrLDPC_bnProc_BG1_R23_cuda_stream_core(bnProcBuf,                                                         \
                                                 cnProcBuf,                                                         \
                                                 llrProcBuf,                                                        \
                                                 llrRes,                                                            \
                                                 n_segments,                                                        \
                                                 Z,                                                                 \
                                                 ZcIdx,                                                             \
                                                 q_streams,                                                         \
                                                 q_idx,                                                             \
                                                 NULL);                                                             \
      }                                                                                                             \
    } else if (R == 89) {                                                                                           \
      for (int i = 0; i <= numMaxIter; i++) {                                                                       \
        nrLDPC_cnProc_BG1_R89_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, NULL); \
        if (i == numMaxIter)                                                                                        \
          nrLDPC_bnProc_BG1_R89_cuda_stream_core_last(bnProcBuf,                                                    \
                                                      cnProcBuf,                                                    \
                                                      llrProcBuf,                                                   \
                                                      llrRes,                                                       \
                                                      n_segments,                                                   \
                                                      Z,                                                            \
                                                      ZcIdx,                                                        \
                                                      q_streams,                                                    \
                                                      q_idx,                                                        \
                                                      NULL);                                                        \
        else                                                                                                        \
          nrLDPC_bnProc_BG1_R89_cuda_stream_core(bnProcBuf,                                                         \
                                                 cnProcBuf,                                                         \
                                                 llrProcBuf,                                                        \
                                                 llrRes,                                                            \
                                                 n_segments,                                                        \
                                                 Z,                                                                 \
                                                 ZcIdx,                                                             \
                                                 q_streams,                                                         \
                                                 q_idx,                                                             \
                                                 NULL);                                                             \
      }                                                                                                             \
    }                                                                                                               \
    nrLDPC_OutPut_BG1_cuda_stream_core(llrRes, Z, R, outMode, buffer, numLLR, K, q_streams, q_idx, NULL);           \
  } while (0)

// Check-node half of one iteration. Code blocks with skip[r] set (already decoded) are skipped.
#define ENQUEUE_LDPC_CN(q_streams, q_idx, skip)                                                                   \
  do {                                                                                                            \
    if (R == 13)                                                                                                  \
      nrLDPC_cnProc_BG1_R13_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, skip); \
    else if (R == 23)                                                                                             \
      nrLDPC_cnProc_BG1_R23_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, skip); \
    else if (R == 89)                                                                                             \
      nrLDPC_cnProc_BG1_R89_cuda_stream_core(cnProcBuf, bnProcBuf, n_segments, Z, ZcIdx, q_streams, q_idx, skip); \
  } while (0)

// Bit-node half of one iteration; the regular kernels also store the posterior LLRs, the _last variants (last
// iteration) only the posterior LLRs.
#define ENQUEUE_LDPC_BN(q_streams, q_idx, skip, last)           \
  do {                                                          \
    if (R == 13) {                                              \
      if (last)                                                 \
        nrLDPC_bnProc_BG1_R13_cuda_stream_core_last(bnProcBuf,  \
                                                    cnProcBuf,  \
                                                    llrProcBuf, \
                                                    llrRes,     \
                                                    n_segments, \
                                                    Z,          \
                                                    ZcIdx,      \
                                                    q_streams,  \
                                                    q_idx,      \
                                                    skip);      \
      else                                                      \
        nrLDPC_bnProc_BG1_R13_cuda_stream_core(bnProcBuf,       \
                                               cnProcBuf,       \
                                               llrProcBuf,      \
                                               llrRes,          \
                                               n_segments,      \
                                               Z,               \
                                               ZcIdx,           \
                                               q_streams,       \
                                               q_idx,           \
                                               skip);           \
    } else if (R == 23) {                                       \
      if (last)                                                 \
        nrLDPC_bnProc_BG1_R23_cuda_stream_core_last(bnProcBuf,  \
                                                    cnProcBuf,  \
                                                    llrProcBuf, \
                                                    llrRes,     \
                                                    n_segments, \
                                                    Z,          \
                                                    ZcIdx,      \
                                                    q_streams,  \
                                                    q_idx,      \
                                                    skip);      \
      else                                                      \
        nrLDPC_bnProc_BG1_R23_cuda_stream_core(bnProcBuf,       \
                                               cnProcBuf,       \
                                               llrProcBuf,      \
                                               llrRes,          \
                                               n_segments,      \
                                               Z,               \
                                               ZcIdx,           \
                                               q_streams,       \
                                               q_idx,           \
                                               skip);           \
    } else if (R == 89) {                                       \
      if (last)                                                 \
        nrLDPC_bnProc_BG1_R89_cuda_stream_core_last(bnProcBuf,  \
                                                    cnProcBuf,  \
                                                    llrProcBuf, \
                                                    llrRes,     \
                                                    n_segments, \
                                                    Z,          \
                                                    ZcIdx,      \
                                                    q_streams,  \
                                                    q_idx,      \
                                                    skip);      \
      else                                                      \
        nrLDPC_bnProc_BG1_R89_cuda_stream_core(bnProcBuf,       \
                                               cnProcBuf,       \
                                               llrProcBuf,      \
                                               llrRes,          \
                                               n_segments,      \
                                               Z,               \
                                               ZcIdx,           \
                                               q_streams,       \
                                               q_idx,           \
                                               skip);           \
    }                                                           \
  } while (0)

// Early-termination chunks: the prologue does the LLR pre-processing, then `iters` full iterations (check nodes then bit
// nodes; the regular bit-node kernels also store the posterior LLRs) and the hard decision; a continuation chunk does
// `iters` more iterations and the hard decision. The host checks the code-block CRCs between chunks.
#define ENQUEUE_LDPC_DECODER_CHUNK(q_streams, q_idx, prologue, iters)                                               \
  do {                                                                                                              \
    uint8_t ZcIdx = get_lut_col_index_host(Z);                                                                      \
    if (prologue)                                                                                                   \
      nrLDPC_llrPreProc_BG1_cuda_stream_core(buffer, numLLR, llrProcBuf, cnProcBuf, Z, ZcIdx, R, q_streams, q_idx); \
    for (int i = 0; i < (iters); i++) {                                                                             \
      ENQUEUE_LDPC_CN(q_streams, q_idx, NULL);                                                                      \
      ENQUEUE_LDPC_BN(q_streams, q_idx, NULL, 0);                                                                   \
    }                                                                                                               \
    nrLDPC_OutPut_BG1_cuda_stream_core(llrRes, Z, R, outMode, buffer, numLLR, K, q_streams, q_idx, NULL);           \
  } while (0)

static void set_kernel_dims(uint8_t CudaStreamIdx, uint32_t Z, uint8_t n_segments)
{
  Kdim_R13_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_R13_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R13_Edge + 3) >> 2, n_segments, 1);
  Kdim_R23_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_R23_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R23_Edge + 3) >> 2, n_segments, 1);
  Kdim_R89_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_R89_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R89_Edge + 3) >> 2, n_segments, 1);
  Kdim_llr[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_llr[CudaStreamIdx].grid = dim3((num_TotalBlocks_llr_llrRes + 3) >> 2, n_segments, 1);
  Kdim_cn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_cn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R13_Node + 3) >> 2, n_segments, 1);
  Kdim_bn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_bn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_bn_BG1_R13_Node + 3) >> 2, n_segments, 1);
  Kdim_cn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_cn_R23_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R23_Node + 3) >> 2, n_segments, 1);
  Kdim_bn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_bn_R23_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_bn_BG1_R23_Node + 3) >> 2, n_segments, 1);
  Kdim_cn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_cn_R89_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R89_Node + 3) >> 2, n_segments, 1);
  Kdim_bn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
  Kdim_bn_R89_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_bn_BG1_R89_Node + 3) >> 2, n_segments, 1);
}

// Device-side early termination. A code block passes when its hard decision (data followed by its CRC, zero initial
// value, no final XOR) is divisible by the CRC generator g: the same test as check_crc(). One thread block per code
// block; thread t takes the remainder of bytes [t L, (t+1) L) of the message left-padded with zero bytes to
// L * LDPC_ET_THREADS (which leaves the remainder unchanged), multiplies it by xpow[t] = x^(8 L (T-1-t)) mod g
// (precomputed on the host) and the products are XORed.

// a * b mod (x^deg + low), polynomials of degree < deg
__device__ __forceinline__ uint32_t gf2_mulmod(uint32_t a, uint32_t b, uint32_t low, uint32_t deg)
{
  const uint32_t top = 1u << deg;
  uint32_t r = 0;
  for (int i = deg - 1; i >= 0; i--) {
    r <<= 1;
    if (r & top)
      r ^= top | low;
    if ((b >> i) & 1)
      r ^= a;
  }
  return r;
}

// Per-decode setup, launched before the graphs (stream-ordered after the previous decode on this context): TB
// parameters, and the code blocks past C (padding up to the graph's bucket) marked as decoded so that every kernel skips
// them.
__global__ void ldpc_et_setup_kernel(ldpc_cuda_et_state_t *st,
                                     int32_t *pass_it,
                                     int C,
                                     int Cb,
                                     uint32_t nbytes,
                                     uint32_t crc_deg,
                                     uint32_t crc_low,
                                     const uint32_t *xpow)
{
  for (int i = threadIdx.x; i < (int)sizeof(st->done); i += blockDim.x)
    st->done[i] = i >= C;
  for (int i = threadIdx.x; i < Cb; i += blockDim.x) // the host reads it after the first graph
    pass_it[i] = 0;
  if (threadIdx.x == 0) {
    st->nbytes = nbytes;
    st->crc_deg = crc_deg;
    st->crc_low = crc_low;
    st->xpow = xpow;
  }
}

// Hard decision and CRC check after `it` iterations. A code block that passes is skipped by every later kernel; its hard
// decision goes to out and `it` to pass_it. On the last chunk (`final`) the code blocks that never passed write their last
// hard decision and pass_it 0. out and pass_it are mapped host memory on GPUs coherent with the host, else device memory
// copied to the host at the end of the decode (see nrLDPC_decoder_cuda_EnqueueET()).
__global__ void ldpc_et_crc_kernel(const int8_t *__restrict__ llrRes,
                                   uint32_t R,
                                   uint32_t Zc,
                                   uint8_t *__restrict__ out,
                                   int32_t *__restrict__ pass_it,
                                   uint32_t Kb,
                                   ldpc_cuda_et_state_t *st,
                                   int it,
                                   int final)
{
  __shared__ uint8_t hd[LDPC_ET_MAX_KB];
  __shared__ uint32_t warp_rem[LDPC_ET_THREADS / 32];
  const int r = blockIdx.x;
  const int t = threadIdx.x;
  if (st->done[r])
    return;
  const uint32_t nbytes = st->nbytes, deg = st->crc_deg, low = st->crc_low;
  const uint32_t top = 1u << deg;
  const uint32_t xp = st->xpow[t];
  // hard decision of the systematic columns, as llr2bitPacked_Kernel_BG1_int8(): MSB first. The LUT addresses are
  // multiples of NR_LDPC_ZMAX, so the 8-byte loads are aligned
  const uint32_t *lut_Addr =
      (R == 13) ? d_llr2llrProcBufAddr_BG1_R13 : ((R == 89) ? d_llr2llrProcBufAddr_BG1_R89 : d_llr2llrProcBufAddr_BG1_R23);
  const uint32_t *lut_Pos =
      (R == 13) ? d_llr2llrProcBufBnPos_BG1_R13 : ((R == 89) ? d_llr2llrProcBufBnPos_BG1_R89 : d_llr2llrProcBufBnPos_BG1_R23);
  const int8_t *p_llrRes = llrRes + (size_t)r * NR_LDPC_MAX_NUM_LLR;
  const uint32_t bytesPerCol = Zc >> 3;
  for (uint32_t i = t; i < Kb; i += LDPC_ET_THREADS) {
    const uint32_t col = i / bytesPerCol;
    const uint64_t v = *(const uint64_t *)(p_llrRes + lut_Addr[col] + lut_Pos[col] * NR_LDPC_ZMAX + (i - col * bytesPerCol) * 8);
    uint32_t b = 0;
#pragma unroll
    for (int k = 0; k < 8; k++)
      b |= ((v >> (8 * k + 7)) & 1) << (7 - k);
    hd[i] = b;
  }
  __syncthreads();
  const int L = (nbytes + LDPC_ET_THREADS - 1) / LDPC_ET_THREADS;
  const int pad = L * LDPC_ET_THREADS - nbytes;
  uint32_t rr = 0;
  for (int j = t * L; j < (t + 1) * L; j++) {
    if (j < pad)
      continue;
    const uint32_t b = hd[j - pad];
    for (int k = 7; k >= 0; k--) {
      rr = (rr << 1) | ((b >> k) & 1);
      if (rr & top)
        rr ^= top | low;
    }
  }
  rr = gf2_mulmod(rr, xp, low, deg);
  for (int o = 16; o > 0; o >>= 1)
    rr ^= __shfl_xor_sync(GPU_SHFL_ALL_LANES, rr, o);
  if ((t & 31) == 0)
    warp_rem[t >> 5] = rr;
  __syncthreads();
  rr = 0;
  for (int w = 0; w < LDPC_ET_THREADS / 32; w++)
    rr ^= warp_rem[w];
  if (rr != 0 && !final)
    return;
  for (uint32_t i = t; i < Kb; i += LDPC_ET_THREADS)
    out[(size_t)r * Kb + i] = hd[i];
  if (t == 0) {
    pass_it[r] = rr == 0 ? it : 0;
    if (rr == 0)
      st->done[r] = 1;
  }
}

  extern "C" {

  // Record and instantiate a graph from the work captured on `stream` since gpuStreamBeginCapture()
  static gpuError_t et_end_capture(gpuStream_t stream, gpuGraph_t *graphPtr, gpuGraphExec_t *graphExecPtr)
  {
    gpuError_t err = gpuStreamEndCapture(stream, graphPtr);
    if (err != gpuSuccess)
      return err;
    err = gpuGraphInstantiateWithFlags(graphExecPtr, *graphPtr, 0);
    if (err != gpuSuccess) {
      gpuGraphDestroy(*graphPtr);
      *graphPtr = NULL;
      *graphExecPtr = NULL;
    }
    return err;
  }

  // Locals of context c that the ENQUEUE_* macros use
#define LDPC_CTX_BUFFERS(c)               \
  ldpc_cuda_bridge_t *buffer = c->bridge; \
  int8_t *cnProcBuf = c->cnProcBuf;       \
  int8_t *bnProcBuf = c->bnProcBuf;       \
  int8_t *llrRes = c->llrRes;             \
  int8_t *llrProcBuf = c->llrProcBuf;     \
  const e_nrLDPC_outMode outMode = nrLDPC_outMode_BIT

  // Record one early-termination chunk (see ENQUEUE_LDPC_DECODER_CHUNK) of context c as a CUDA graph on its stream.
  gpuError_t nrLDPC_decoder_cuda_GraphRecordChunk(ldpc_cuda_ctx_t *c,
                                                  uint32_t Z,
                                                  uint8_t R,
                                                  uint32_t numLLR,
                                                  uint8_t n_segments,
                                                  int prologue,
                                                  int iters,
                                                  gpuGraph_t *graphPtr,
                                                  gpuGraphExec_t *graphExecPtr)
  {
    LDPC_CTX_BUFFERS(c);
    const uint32_t K = 22 * Z;
    gpuStream_t *streams = c->lane.streams;
    const uint8_t CudaStreamIdx = c->lane.slot;
    gpuStream_t stream = streams[CudaStreamIdx];
    set_kernel_dims(CudaStreamIdx, Z, n_segments);
    gpuError_t err = gpuStreamBeginCapture(stream, gpuStreamCaptureModeThreadLocal);
    if (err != gpuSuccess)
      return err;
    ENQUEUE_LDPC_DECODER_CHUNK(streams, CudaStreamIdx, prologue, iters);
    return et_end_capture(stream, graphPtr, graphExecPtr);
  }

  // Same chunk launched directly (no graph).
  void nrLDPC_decoder_cuda_NormalExecuteChunk(ldpc_cuda_ctx_t *c,
                                              uint32_t Z,
                                              uint8_t R,
                                              uint32_t numLLR,
                                              uint8_t n_segments,
                                              int prologue,
                                              int iters)
  {
    LDPC_CTX_BUFFERS(c);
    const uint32_t K = 22 * Z;
    gpuStream_t *streams = c->lane.streams;
    const uint8_t CudaStreamIdx = c->lane.slot;
    set_kernel_dims(CudaStreamIdx, Z, n_segments);
    ENQUEUE_LDPC_DECODER_CHUNK(streams, CudaStreamIdx, prologue, iters);
  }

  // Per-decode setup (see ldpc_et_setup_kernel()), before the graphs
  void nrLDPC_decoder_cuda_ETSetup(ldpc_cuda_ctx_t *c,
                                   int C,
                                   int Cb,
                                   uint32_t nbytes,
                                   uint32_t crc_deg,
                                   uint32_t crc_low,
                                   const uint32_t *xpow)
  {
    gpuStream_t stream = c->lane.streams[c->lane.slot];
    int32_t *pass_it = (int32_t *)c->et_dev;
    ldpc_et_setup_kernel<<<1, LDPC_ET_THREADS, 0, stream>>>(c->st, pass_it, C, Cb, nbytes, crc_deg, crc_low, xpow);
  }

  // Early-terminating decode for n_segments code blocks (a bucket: the TB's own count and CRC parameters are set by
  // nrLDPC_decoder_cuda_ETSetup() before each decode, so the same work serves every TB size of the bucket), either
  // recorded as two static graphs (graphs, execs) or, with graphs NULL, launched directly on streams[CudaStreamIdx] with
  // ev_first recorded after the first part. The first part: LLR pre-processing and one chunk of {chunk iterations, hard
  // decision and CRC}. The second (none if the budget is a single chunk): the remaining chunks, the last one shorter if
  // chunk does not divide the budget. CRC checks after chunk, 2 chunk, ..., budget iterations, as the host loop.
  // The host enqueues both back to back and first waits for the first only: at high SNR every code block passed after it
  // and the host returns at once, otherwise the second one is already running.
  // The CRC kernels write the output (out, pass_it) straight to mapped host memory on GPUs coherent with the host
  // (copy_bytes 0), to device memory then copied in one piece from copy_src to copy_dst at the end of each graph
  // otherwise. The kernels skip the code blocks that already passed, so the chunks after the last one passed cost only
  // their launches.
  // The CRC kernel reads llrRes, which the check-node kernels do not touch, and sets st->done, which they read: within the
  // second graph it runs on a side branch (side_stream) next to the following check-node kernel, and only the following
  // bit-node kernel waits for it. That check-node kernel may not see the code blocks that just passed and process them
  // once more, which does not change their output.
  gpuError_t nrLDPC_decoder_cuda_EnqueueET(ldpc_cuda_ctx_t *c, ldpc_et_graph_t *g, const ldpc_cuda_lane_t *lane, bool record)
  {
    LDPC_CTX_BUFFERS(c);
    (void)outMode;
    const uint32_t Z = g->Z, K = 22 * Z, Kb = K >> 3, numLLR = g->numLLR;
    const uint8_t R = g->R, n_segments = g->n_segments;
    const int budget = g->budget, chunk = g->chunk;
    // output (out, pass_it) in et_dev, copied to et_host at the end of each part if not zero copy
    uint8_t *out = c->et_dev + LDPC_ET_PASS_IT_BYTES;
    int32_t *pass_it = (int32_t *)c->et_dev;
    ldpc_cuda_et_state_t *st = c->st;
    uint8_t *copy_dst = c->et_host;
    const uint8_t *copy_src = c->et_dev;
    const size_t copy_bytes = c->et_copy ? LDPC_ET_PASS_IT_BYTES + (size_t)n_segments * Kb : 0;
    gpuStream_t *streams = lane->streams;
    const uint8_t CudaStreamIdx = lane->slot;
    gpuStream_t side_stream = lane->side_stream;
    gpuEvent_t ev_fork = lane->ev_fork, ev_join = lane->ev_join, ev_first = c->ev_first;
    gpuGraph_t *graphs = record ? g->graph : NULL;
    gpuGraphExec_t *execs = record ? g->exec : NULL;
    const bool direct = !record;
    const uint8_t *skip = st->done;
    const uint8_t ZcIdx = get_lut_col_index_host(Z);
    gpuStream_t stream = streams[CudaStreamIdx];
    if (!direct) {
      graphs[1] = NULL;
      execs[1] = NULL;
    }
    set_kernel_dims(CudaStreamIdx, Z, n_segments);
    gpuError_t err = direct ? gpuSuccess : gpuStreamBeginCapture(stream, gpuStreamCaptureModeThreadLocal);
    if (err != gpuSuccess)
      return err;
    nrLDPC_llrPreProc_BG1_cuda_stream_core(buffer, numLLR, llrProcBuf, cnProcBuf, Z, ZcIdx, R, streams, CudaStreamIdx);
    bool crc_pending = false;
    for (int it = 0, k; it < budget; it += k) {
      k = budget - it < chunk ? budget - it : chunk;
      for (int i = 0; i < k; i++) {
        ENQUEUE_LDPC_CN(streams, CudaStreamIdx, skip);
        if (crc_pending) // the previous CRC reads llrRes, which the bit-node kernel writes
          gpuStreamWaitEvent(stream, ev_join, 0);
        crc_pending = false;
        ENQUEUE_LDPC_BN(streams, CudaStreamIdx, skip, it + i == budget - 1);
      }
      const bool final = it + k == budget;
      const bool first = it == 0; // the first graph is the first chunk
      // the last chunk of each graph has nothing left to overlap its CRC with: on the main stream
      gpuStream_t crc_stream = final || first ? stream : side_stream;
      if (crc_stream != stream) {
        gpuEventRecord(ev_fork, stream);
        gpuStreamWaitEvent(side_stream, ev_fork, 0);
      }
      ldpc_et_crc_kernel<<<n_segments, LDPC_ET_THREADS, 0, crc_stream>>>(llrRes, R, Z, out, pass_it, Kb, st, it + k, final);
      if (crc_stream != stream)
        gpuEventRecord(ev_join, side_stream);
      crc_pending = crc_stream != stream;
      if (first || final) {
        if (copy_bytes)
          gpuMemcpyAsync(copy_dst, copy_src, copy_bytes, gpuMemcpyDeviceToHost, stream);
        if (direct) {
          if (!final)
            gpuEventRecord(ev_first, stream);
          continue;
        }
        err = et_end_capture(stream, &graphs[first ? 0 : 1], &execs[first ? 0 : 1]);
        if (err != gpuSuccess || final)
          break;
        err = gpuStreamBeginCapture(stream, gpuStreamCaptureModeThreadLocal);
        if (err != gpuSuccess)
          break;
      }
    }
    if (err != gpuSuccess && !direct) {
      for (int g = 0; g < 2; g++) {
        if (execs[g])
          gpuGraphExecDestroy(execs[g]);
        if (graphs[g])
          gpuGraphDestroy(graphs[g]);
        execs[g] = NULL;
        graphs[g] = NULL;
      }
    }
    return direct ? gpuGetLastError() : err; // direct launches: launch errors
  }

  gpuError_t nrLDPC_decoder_cuda_GraphRecord(ldpc_cuda_bridge_t *buffer,
                                              uint32_t numLLR,
                                              int8_t *cnProcBuf,
                                              int8_t *bnProcBuf,
                                              int8_t *llrRes,
                                              int8_t *llrProcBuf,
                                              uint32_t Z,
                                              uint32_t K,
                                              uint8_t BG,
                                              uint8_t R,
                                              uint8_t numMaxIter,
                                              uint8_t n_segments,
                                              e_nrLDPC_outMode outMode,
                                              gpuStream_t *streams,
                                              uint8_t CudaStreamIdx,
                                              gpuGraph_t *graphPtr,
                                              gpuGraphExec_t *graphExecPtr,
                                              uint8_t *isCreatedFlag)
  {
    gpuStream_t stream = streams[CudaStreamIdx];
    *isCreatedFlag = 0;
    gpuError_t err = gpuSuccess;

    Kdim_R13_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R13_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R13_Edge + 3) >> 2, n_segments, 1);
    Kdim_R23_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R23_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R23_Edge + 3) >> 2, n_segments, 1);
    Kdim_R89_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R89_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R89_Edge + 3) >> 2, n_segments, 1);
    Kdim_llr[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_llr[CudaStreamIdx].grid = dim3((num_TotalBlocks_llr_llrRes + 3) >> 2, n_segments, 1);

    Kdim_cn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R13_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_bn_BG1_R13_Node + 3) >> 2, n_segments, 1);
    Kdim_cn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R23_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R23_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R23_Node[CudaStreamIdx].grid =
        dim3((num_TotalBlocks_bn_BG1_R23_Node + 3) >> 2, n_segments, 1); // 35 is not devidable with 2^n
    Kdim_cn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R89_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R89_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R89_Node[CudaStreamIdx].grid =
        dim3((num_TotalBlocks_bn_BG1_R89_Node + 3) >> 2, n_segments, 1); // 27 is not devidable with 2^n

    err = gpuStreamBeginCapture(stream, gpuStreamCaptureModeThreadLocal);
    if (err != gpuSuccess) {
      return err;
    }

    ENQUEUE_LDPC_DECODER_SEQUENCE(streams, CudaStreamIdx);

    err = gpuStreamEndCapture(stream, graphPtr);
    if (err != gpuSuccess) {
      gpuStreamSynchronize(stream);
      return err;
    }

    err = gpuGraphInstantiate(graphExecPtr, *graphPtr, NULL, NULL, 0);
    if (err != gpuSuccess) {
      gpuGraphDestroy(*graphPtr);
      return err;
    }

    *isCreatedFlag = 1;
    return gpuSuccess;
  }

  gpuError_t nrLDPC_decoder_cuda_GraphExecute(gpuGraphExec_t graphExec,
                                               gpuStream_t stream,
                                               gpuEvent_t *doneEvent,
                                               uint8_t CudaStreamIdx)
  {
    gpuError_t err = gpuGraphLaunch(graphExec, stream);
    //gpuStreamSynchronize(stream);
    if (err != gpuSuccess) {
      return err;
    }

    if (doneEvent) {
      err = gpuEventRecord(doneEvent[CudaStreamIdx], stream);
    }

    return err;
  }

  void nrLDPC_decoder_cuda_NormalExecute(ldpc_cuda_bridge_t *buffer,
                                         uint32_t numLLR,
                                         int8_t *cnProcBuf,
                                         int8_t *bnProcBuf,
                                         int8_t *llrRes,
                                         int8_t *llrProcBuf,
                                         uint32_t Z,
                                         uint32_t K,
                                         uint8_t BG,
                                         uint8_t R,
                                         uint8_t numMaxIter,
                                         uint8_t n_segments,
                                         e_nrLDPC_outMode outMode,
                                         gpuStream_t *streams,
                                         uint8_t CudaStreamIdx,
                                         gpuEvent_t *doneEvent)
  {
    gpuStream_t stream = streams[CudaStreamIdx];

    Kdim_R13_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R13_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R13_Edge + 3) >> 2, n_segments, 1);
    Kdim_R23_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R23_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R23_Edge + 3) >> 2, n_segments, 1);
    Kdim_R89_Edge[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_R89_Edge[CudaStreamIdx].grid = dim3((num_TotalBlocks_BG1_R89_Edge + 3) >> 2, n_segments, 1);
    Kdim_llr[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_llr[CudaStreamIdx].grid = dim3((num_TotalBlocks_llr_llrRes + 3) >> 2, n_segments, 1);

    Kdim_cn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R13_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R13_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R13_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_bn_BG1_R13_Node + 3) >> 2, n_segments, 1);
    Kdim_cn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R23_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R23_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R23_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R23_Node[CudaStreamIdx].grid =
        dim3((num_TotalBlocks_bn_BG1_R23_Node + 3) >> 2, n_segments, 1); // 35 is not devidable with 2^n
    Kdim_cn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_cn_R89_Node[CudaStreamIdx].grid = dim3((num_TotalBlocks_cn_BG1_R89_Node + 3) >> 2, n_segments, 1);
    Kdim_bn_R89_Node[CudaStreamIdx].block = dim3(Z >> 2, 4, 1);
    Kdim_bn_R89_Node[CudaStreamIdx].grid =
        dim3((num_TotalBlocks_bn_BG1_R89_Node + 3) >> 2, n_segments, 1); // 27 is not devidable with 2^n

    ENQUEUE_LDPC_DECODER_SEQUENCE(streams, CudaStreamIdx);

    if (doneEvent) {
      gpuEventRecord(doneEvent[CudaStreamIdx], stream);
    }
  }

  } // extern "C"
