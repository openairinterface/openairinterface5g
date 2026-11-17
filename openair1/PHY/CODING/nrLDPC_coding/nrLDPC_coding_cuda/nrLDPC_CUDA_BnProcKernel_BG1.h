/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Defines the kernels for bit node processing
 */

#include "PHY/gpu_compat.h"
#include "PHY/gpu_simd_intrin_compat.h"
#include <stdint.h>
#include <stdio.h>
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPCdecoder_defs.h"

__device__ __forceinline__ void unpack_and_sign_extend(uint32_t packed, uint32_t *val_lo, uint32_t *val_hi)
{
  uint32_t magic_sub = 0x00800080;

  uint32_t lo_zext = __byte_perm(packed, 0, 0x5140);
  *val_lo = gpu_vsub2(lo_zext ^ magic_sub, magic_sub);

  uint32_t hi_zext = __byte_perm(packed, 0, 0x7362);
  *val_hi = gpu_vsub2(hi_zext ^ magic_sub, magic_sub);
}

__device__ __forceinline__ uint32_t saturate_and_pack(uint32_t val_lo, uint32_t val_hi)
{
  uint32_t lo_clamped = gpu_vmins2(val_lo, 0x007F007F); // min(v, 127)
  lo_clamped = gpu_vmaxs2(lo_clamped, 0xFF80FF80); // max(v, -128)

  uint32_t hi_clamped = gpu_vmins2(val_hi, 0x007F007F);
  hi_clamped = gpu_vmaxs2(hi_clamped, 0xFF80FF80);

  return __byte_perm(lo_clamped, hi_clamped, 0x6420);
}

__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Node_R13(const int8_t *__restrict__ d_bnProcBuf,
                                                             int8_t *__restrict__ d_cnProcBuf,
                                                             const int8_t *__restrict__ d_llrProcBuf,
                                                             int8_t *__restrict__ d_llrRes,
                                                             uint32_t lane,
                                                             uint32_t BnGrpIdx,
                                                             uint32_t BnIdx,
                                                             uint32_t GrpNum,
                                                             uint32_t Bn2MsgStartIdx,
                                                             uint32_t Zc,
                                                             uint32_t ZcIdx)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)BnGrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < BnGrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);
  ((int32_t *)(d_llrRes))[lane] = saturated_llr; // posterior, for early termination

  uint32_t BricksToBeGet;
  if (BnGrpIdx == 1) {
    BricksToBeGet = packed_intrinsic;
    uint32_t MsgIdx = Bn2MsgStartIdx;
    uint32_t circShift = bn_cn_map_BG1_Z_R13[MsgIdx][ZcIdx];
    int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R13[MsgIdx][0]);

    moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
  } else {
    for (int temp_MsgIdx = 0; temp_MsgIdx < BnGrpIdx; temp_MsgIdx++) {
      uint32_t prevIdxWords = (temp_MsgIdx * GrpNum * NR_LDPC_ZMAX) >> 2;
      uint32_t prev = bnProcBufPtr[prevIdxWords];
      BricksToBeGet = gpu_vsubss4(saturated_llr, prev);
      uint32_t MsgIdx = Bn2MsgStartIdx + temp_MsgIdx;
      uint32_t circShift = bn_cn_map_BG1_Z_R13[MsgIdx][ZcIdx];
      int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R13[MsgIdx][0]);

      moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
    }
  }
}

__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Node_R23(const int8_t *__restrict__ d_bnProcBuf,
                                                             int8_t *__restrict__ d_cnProcBuf,
                                                             const int8_t *__restrict__ d_llrProcBuf,
                                                             int8_t *__restrict__ d_llrRes,
                                                             uint32_t lane,
                                                             uint32_t BnGrpIdx,
                                                             uint32_t BnIdx,
                                                             uint32_t GrpNum,
                                                             uint32_t Bn2MsgStartIdx,
                                                             uint32_t Zc,
                                                             uint32_t ZcIdx)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)BnGrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < BnGrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);
  ((int32_t *)(d_llrRes))[lane] = saturated_llr; // posterior, for early termination

  uint32_t BricksToBeGet;
  if (BnGrpIdx == 1) {
    BricksToBeGet = packed_intrinsic;
    uint32_t MsgIdx = Bn2MsgStartIdx;
    uint32_t circShift = bn_cn_map_BG1_Z_R23[MsgIdx][ZcIdx];
    int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R23[MsgIdx][0]);

    moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
  } else {
    for (int temp_MsgIdx = 0; temp_MsgIdx < BnGrpIdx; temp_MsgIdx++) {
      uint32_t prevIdxWords = (temp_MsgIdx * GrpNum * NR_LDPC_ZMAX) >> 2;
      uint32_t prev = bnProcBufPtr[prevIdxWords];
      BricksToBeGet = gpu_vsubss4(saturated_llr, prev);
      uint32_t MsgIdx = Bn2MsgStartIdx + temp_MsgIdx;
      uint32_t circShift = bn_cn_map_BG1_Z_R23[MsgIdx][ZcIdx];
      int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R23[MsgIdx][0]);

      moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
    }
  }
}

__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Node_R89(const int8_t *__restrict__ d_bnProcBuf,
                                                             int8_t *__restrict__ d_cnProcBuf,
                                                             const int8_t *__restrict__ d_llrProcBuf,
                                                             int8_t *__restrict__ d_llrRes,
                                                             uint32_t lane,
                                                             uint32_t BnGrpIdx,
                                                             uint32_t BnIdx,
                                                             uint32_t GrpNum,
                                                             uint32_t Bn2MsgStartIdx,
                                                             uint32_t Zc,
                                                             uint32_t ZcIdx)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)BnGrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < BnGrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);
  ((int32_t *)(d_llrRes))[lane] = saturated_llr; // posterior, for early termination

  uint32_t BricksToBeGet;
  if (BnGrpIdx == 1) {
    BricksToBeGet = packed_intrinsic;
    uint32_t MsgIdx = Bn2MsgStartIdx;
    uint32_t circShift = bn_cn_map_BG1_Z_R89[MsgIdx][ZcIdx];
    int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R89[MsgIdx][0]);

    moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
  } else {
    for (int temp_MsgIdx = 0; temp_MsgIdx < BnGrpIdx; temp_MsgIdx++) {
      uint32_t prevIdxWords = (temp_MsgIdx * GrpNum * NR_LDPC_ZMAX) >> 2;
      uint32_t prev = bnProcBufPtr[prevIdxWords];
      BricksToBeGet = gpu_vsubss4(saturated_llr, prev);
      uint32_t MsgIdx = Bn2MsgStartIdx + temp_MsgIdx;
      uint32_t circShift = bn_cn_map_BG1_Z_R89[MsgIdx][ZcIdx];
      int8_t *p_cnProcBuf = (int8_t *)(d_cnProcBuf + bn_cn_map_BG1_Z_R89[MsgIdx][0]);

      moveBricks_forput_circ(p_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
    }
  }
}

__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Node_last(const int8_t *__restrict__ d_bnProcBuf,
                                                              int8_t *__restrict__ d_cnProcBuf,
                                                              const int8_t *__restrict__ d_llrProcBuf,
                                                              int8_t *__restrict__ d_llrRes,
                                                              uint32_t lane,
                                                              uint32_t BnGrpIdx,
                                                              uint32_t BnIdx,
                                                              uint32_t GrpNum,
                                                              uint32_t Bn2MsgStartIdx,
                                                              uint32_t Zc,
                                                              uint32_t ZcIdx)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)BnGrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < BnGrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);

  ((int32_t *)(d_llrRes))[lane] = saturated_llr;
}


__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Edge(const int8_t *__restrict__ d_bnProcBuf,
                                                              int8_t *__restrict__ d_cnProcBuf,
                                                              const int8_t *__restrict__ d_llrProcBuf,
                                                              int8_t *__restrict__ d_llrRes,
                                                              uint32_t lane,
                                                              uint32_t GrpIdx,
                                                              uint32_t MsgIdx,
                                                              uint32_t BnIdx,
                                                              uint32_t GrpNum,
                                                              uint32_t circShift,
                                                              uint32_t Zc)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)GrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < GrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);
  if (MsgIdx == 0) // posterior, for early termination
    ((int32_t *)(d_llrRes))[lane] = saturated_llr;

  uint32_t BricksToBeGet;
  if (GrpIdx == 1) {
    BricksToBeGet = packed_intrinsic;
  } else {
    uint32_t prevIdxWords = (MsgIdx * GrpNum * NR_LDPC_ZMAX) >> 2;
    uint32_t prev = bnProcBufPtr[prevIdxWords];
    BricksToBeGet = gpu_vsubss4(saturated_llr, prev);
  }

  moveBricks_forput_circ(d_cnProcBuf, lane * 4, (uint8_t *)&BricksToBeGet, Zc, circShift);
}

__device__ __forceinline__ void bnProcKernel_BG1_int8_Gn_Edge_last(const int8_t *__restrict__ d_bnProcBuf,
                                                                   int8_t *__restrict__ d_cnProcBuf,
                                                                   const int8_t *__restrict__ d_llrProcBuf,
                                                                   int8_t *__restrict__ d_llrRes,
                                                                   uint32_t lane,
                                                                   uint32_t GrpIdx,
                                                                   uint32_t MsgIdx,
                                                                   uint32_t BnIdx,
                                                                   uint32_t GrpNum,
                                                                   uint32_t circShift,
                                                                   uint32_t Zc)
{
  const int32_t *bnProcBufPtr = (const int32_t *)(d_bnProcBuf) + lane;

  uint32_t packed_intrinsic = ((const int32_t *)(d_llrProcBuf))[lane];

  uint32_t MsgSumLo, MsgSumHi;
  unpack_and_sign_extend(packed_intrinsic, &MsgSumLo, &MsgSumHi);

  uint32_t off = (GrpNum * NR_LDPC_ZMAX) >> 2;
  const int32_t *currPtr = bnProcBufPtr;

  int i = 0;

  // ---  2-Way Unroll ---

  for (; i < (int)GrpIdx - 1; i += 2) {
    uint32_t val1 = *currPtr;
    uint32_t val2 = *(currPtr + off);

    uint32_t v1_lo, v1_hi;
    unpack_and_sign_extend(val1, &v1_lo, &v1_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v1_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v1_hi);

    uint32_t v2_lo, v2_hi;
    unpack_and_sign_extend(val2, &v2_lo, &v2_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v2_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v2_hi);

    currPtr += (off << 1);
  }

  if (i < GrpIdx) {
    uint32_t val = *currPtr;
    uint32_t v_lo, v_hi;
    unpack_and_sign_extend(val, &v_lo, &v_hi);
    MsgSumLo = gpu_vaddss2(MsgSumLo, v_lo);
    MsgSumHi = gpu_vaddss2(MsgSumHi, v_hi);
  }

  uint32_t saturated_llr = saturate_and_pack(MsgSumLo, MsgSumHi);

  if (MsgIdx == 0) {
    ((int32_t *)(d_llrRes))[lane] = saturated_llr;
  }
}
