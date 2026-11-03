/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief CUDA implementation of NR LDPC Decoder (BG1) with CUDA Graphs support.
 */

#include "PHY/gpu_compat.h"
#include <stdint.h>
#include <stdio.h>
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"

#include "nrLDPC_CUDA_lut.h"
#include "nrLDPC_CUDA_CnProcKernel_BG1.h"
#include "nrLDPC_CUDA_BnProcKernel_BG1.h"
#include "nrLDPC_CUDA_mPassKernel_BG1.h"
#include "nrLDPC_CUDA_shared_param.h"

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
KernelLaunchConfig Kdim_R13_Edge[8]; //
KernelLaunchConfig Kdim_R23_Edge[8];
KernelLaunchConfig Kdim_R89_Edge[8];
KernelLaunchConfig Kdim_llr[8];

// Node
KernelLaunchConfig Kdim_cn_R13_Node[8]; //
KernelLaunchConfig Kdim_bn_R13_Node[8]; //
KernelLaunchConfig Kdim_cn_R23_Node[8];
KernelLaunchConfig Kdim_bn_R23_Node[8];
KernelLaunchConfig Kdim_cn_R89_Node[8];
KernelLaunchConfig Kdim_bn_R89_Node[8];

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

  extern "C" {

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
