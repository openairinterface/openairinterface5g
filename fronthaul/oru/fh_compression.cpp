/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "fh_compression.h"
#include "assertions.h"
#include <limits.h>
#include <string.h>
#include <simde/x86/avx2.h>

/* ---- bit-stream helpers ---- */

// A PRB is FH_VALS_PER_PRB values of `width` bits, MSB first, back to back: 3 * width bytes, so the
// stream always ends on a byte boundary. Stream it through a 64-bit accumulator a whole byte at a
// time instead of addressing each value's bits individually. Forced inline so that a caller passing
// a constant width gets a fully unrolled, branch-free packer for that width.
__attribute__((always_inline)) static inline void pack_prb(uint8_t *out, const int32_t *vals, int width)
{
  const uint32_t mask = (1u << width) - 1;
  uint64_t acc = 0;
  int nbits = 0;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    acc = (acc << width) | ((uint32_t)vals[i] & mask);
    nbits += width;
    while (nbits >= 8) {
      nbits -= 8;
      *out++ = (uint8_t)(acc >> nbits);
    }
  }
}

__attribute__((always_inline)) static inline void unpack_prb(const uint8_t *in, int32_t *vals, int width)
{
  uint64_t acc = 0;
  int nbits = 0;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    while (nbits < width) {
      acc = (acc << 8) | *in++;
      nbits += 8;
    }
    nbits -= width;
    // Move the value's sign bit to bit 63, then arithmetic-shift it back down.
    vals[i] = (int32_t)((int64_t)(acc << (64 - width - nbits)) >> (64 - width));
  }
}

// Any-length bit reader for streams that are not whole PRBs (beamforming weights): one
// sign-extended value of `width` bits starting at bit `offset`, MSB first.
int32_t unpack_bits(const uint8_t *stream, int offset, int width)
{
  uint32_t bits = 0;
  for (int i = 0; i < width; i++) {
    int bit_offset = offset + i;
    int pos = bit_offset >> 3;
    int shift = 7 - (bit_offset & 7);
    bits = (bits << 1) | ((stream[pos] >> shift) & 1u);
  }
  uint32_t sign = 1u << (width - 1);
  return (bits & sign) ? (int32_t)(bits | ~((1u << width) - 1)) : (int32_t)bits;
}

/* ---- BFP ---- */

static uint16_t saturating_abs16(int16_t v)
{
  if (v == INT16_MIN)
    return INT16_MAX;
  return (uint16_t)(v < 0 ? -v : v);
}

static int leading_zero_count16(uint16_t v)
{
  return v == 0 ? 16 : __builtin_clz(v) - 16;
}

__attribute__((always_inline)) static inline int bfp_exponent(uint16_t max_abs, int iq_bits)
{
  int exponent = 16 - iq_bits + 1 - leading_zero_count16(max_abs < 1 ? 1 : max_abs);
  return exponent < 0 ? 0 : exponent;
}

// The BFP kernels below take the width as a template parameter, so each width gets its own
// kernel with constant shifts, masks and copy sizes.

// Eight consecutive values of W bits are exactly W bytes. These keep such a group MSB-first in the
// low 8 * W bits of a 128-bit integer and move it to/from the wire big-endian, touching only its
// W bytes.
template <int W>
__attribute__((always_inline)) static inline void put_group_be(uint8_t *out, unsigned __int128 group)
{
  group <<= 128 - 8 * W;
  const uint64_t be[2] = {__builtin_bswap64((uint64_t)(group >> 64)), __builtin_bswap64((uint64_t)group)};
  memcpy(out, be, W);
}

template <int W>
__attribute__((always_inline)) static inline unsigned __int128 get_group_be(const uint8_t *in)
{
  uint64_t be[2] = {0, 0};
  memcpy(be, in, W);
  const unsigned __int128 group = ((unsigned __int128)__builtin_bswap64(be[0]) << 64) | __builtin_bswap64(be[1]);
  return group >> (128 - 8 * W);
}

// Scalar BFP for any width: the reference for the vector path below.
template <int W>
__attribute__((always_inline)) static inline void bfp_compress_prb_scalar(const int16_t *src, int8_t *dst)
{
  uint16_t max_abs = 1;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    uint16_t v = saturating_abs16(src[i]);
    if (v > max_abs)
      max_abs = v;
  }
  int exponent = bfp_exponent(max_abs, W);
  dst[0] = (int8_t)exponent;
  int32_t vals[FH_VALS_PER_PRB];
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    vals[i] = src[i] >> exponent;
  pack_prb((uint8_t *)(dst + 1), vals, W);
}

// Vector BFP, W <= 14. The PRB's 24 values are one 256-bit and one 128-bit register:
//  - exponent: saturating |x| (like saturating_abs16()), vertical max, then a horizontal max
//    through minpos of the complement;
//  - mantissas: arithmetic shift by the exponent, masked to W bits;
//  - packing: madd merges each value pair into a 32-bit lane (v0 << W | v1, both factors fit
//    int16 because W <= 14), a 64-bit shift/or merges lane pairs (4 values, 4W bits), and each
//    two such lanes form one 8-value group of W bytes, stored big-endian.
template <int W>
__attribute__((always_inline)) static inline void bfp_compress_prb_simd(const int16_t *src, int8_t *dst)
{
  static_assert(W <= 14, "madd factors must fit int16");
  const simde__m256i lo = simde_mm256_loadu_si256((const simde__m256i *)src);
  const simde__m128i hi = simde_mm_loadu_si128((const simde__m128i *)(src + 16));

  const simde__m256i sat = simde_mm256_set1_epi16(INT16_MAX);
  const simde__m256i abs_lo = simde_mm256_min_epu16(simde_mm256_abs_epi16(lo), sat);
  const simde__m128i abs_hi = simde_mm_min_epu16(simde_mm_abs_epi16(hi), simde_mm256_castsi256_si128(sat));
  simde__m128i m = simde_mm_max_epu16(simde_mm256_castsi256_si128(abs_lo), simde_mm256_extracti128_si256(abs_lo, 1));
  m = simde_mm_max_epu16(m, abs_hi);
  const simde__m128i inv_min = simde_mm_minpos_epu16(simde_mm_xor_si128(m, simde_mm_set1_epi16(-1)));
  const uint16_t max_abs = (uint16_t)~simde_mm_extract_epi16(inv_min, 0);
  const int exponent = bfp_exponent(max_abs, W);
  dst[0] = (int8_t)exponent;

  const simde__m128i exp_cnt = simde_mm_cvtsi32_si128(exponent);
  const simde__m256i mask = simde_mm256_set1_epi16((int16_t)((1 << W) - 1));
  const simde__m256i pair_mul = simde_mm256_set1_epi32((1 << 16) | (1 << W)); // even value * 2^W + odd value * 1
  const simde__m256i low32 = simde_mm256_set1_epi64x(0xFFFFFFFF);

  simde__m256i v_lo = simde_mm256_and_si256(simde_mm256_sra_epi16(lo, exp_cnt), mask);
  simde__m256i p_lo = simde_mm256_madd_epi16(v_lo, pair_mul);
  simde__m256i q_lo =
      simde_mm256_or_si256(simde_mm256_slli_epi64(simde_mm256_and_si256(p_lo, low32), 2 * W), simde_mm256_srli_epi64(p_lo, 32));
  simde__m128i v_hi = simde_mm_and_si128(simde_mm_sra_epi16(hi, exp_cnt), simde_mm256_castsi256_si128(mask));
  simde__m128i p_hi = simde_mm_madd_epi16(v_hi, simde_mm256_castsi256_si128(pair_mul));
  simde__m128i q_hi = simde_mm_or_si128(simde_mm_slli_epi64(simde_mm_and_si128(p_hi, simde_mm256_castsi256_si128(low32)), 2 * W),
                                        simde_mm_srli_epi64(p_hi, 32));

  uint64_t quads[6]; // each holds 4 values (4W bits), in order
  simde_mm256_storeu_si256((simde__m256i *)quads, q_lo);
  simde_mm_storeu_si128((simde__m128i *)(quads + 4), q_hi);
  uint8_t *out = (uint8_t *)(dst + 1);
  for (int g = 0; g < 3; g++)
    put_group_be<W>(out + g * W, ((unsigned __int128)quads[2 * g] << (4 * W)) | quads[2 * g + 1]);
}

// Two-PRB BFP compression, 9 <= W <= 14 (at W = 8 a group is one qword and the single-PRB kernel is
// faster). Two PRBs are 48 values, three 256-bit registers in which every 128-bit lane is one 8-value
// group: [P0 g0 | P0 g1], [P0 g2 | P1 g0], [P1 g1 | P1 g2]. So both PRBs' exponents are computed side
// by side, one per lane, without leaving the vector unit:
//  - exponent: per-lane max |x|, its bit length from the float exponent field, then as bfp_exponent();
//  - mantissas: x >> e masked to W bits are the top W bits of x << (16 - W - e), which is a 16-bit
//    multiply by a per-lane power of two (e <= 16 - W since max |x| <= INT16_MAX);
//  - packing: as bfp_compress_prb_simd() down to 4W-bit qwords, then a funnel shift joins each
//    lane's two qwords into its group's 8W bits and a byte shuffle puts them first, big-endian.
// Each group is written with a 16-byte store that runs 16 - W bytes past it; later stores overwrite
// that, and the last one runs into the PRB after the pair, which the caller must not have written.
template <int W>
static simde__m256i bfp_pack_order()
{
  uint8_t order[32];
  for (int i = 0; i < 16; i++) {
    // the high qword (bytes 7..0), then the top W - 8 bytes of the low one (15, 14, ...)
    order[i] = order[16 + i] = i < 8 ? 7 - i : i < W ? 23 - i : 0x80;
  }
  return simde_mm256_loadu_si256((const simde__m256i *)order);
}

template <int W>
__attribute__((always_inline)) static inline simde__m256i bfp_pack_groups(simde__m256i v, simde__m256i order)
{
  const simde__m256i p = simde_mm256_madd_epi16(v, simde_mm256_set1_epi32((1 << 16) | (1 << W)));
  const simde__m256i q =
      simde_mm256_or_si256(simde_mm256_slli_epi64(simde_mm256_and_si256(p, simde_mm256_set1_epi64x(0xFFFFFFFF)), 2 * W),
                           simde_mm256_srli_epi64(p, 32));
  // high qword: Q0 << (64 - 4W) | Q1 >> (8W - 64), low qword: Q1 << (128 - 8W)
  const simde__m256i hi = simde_mm256_sllv_epi64(q, simde_mm256_setr_epi64x(64 - 4 * W, 128 - 8 * W, 64 - 4 * W, 128 - 8 * W));
  const simde__m256i lo =
      simde_mm256_srlv_epi64(simde_mm256_shuffle_epi32(q, 0x4E), simde_mm256_setr_epi64x(8 * W - 64, 64, 8 * W - 64, 64));
  return simde_mm256_shuffle_epi8(simde_mm256_or_si256(hi, lo), order);
}

template <int W>
__attribute__((always_inline)) static inline void bfp_compress_prb_pair(const int16_t *src, int8_t *dst, simde__m256i order)
{
  static_assert(W >= 9 && W <= 14, "one group per 128-bit lane, madd factors must fit int16");
  const simde__m256i x0 = simde_mm256_loadu_si256((const simde__m256i *)src);
  const simde__m256i x1 = simde_mm256_loadu_si256((const simde__m256i *)(src + 16));
  const simde__m256i x2 = simde_mm256_loadu_si256((const simde__m256i *)(src + 32));
  const simde__m256i sat = simde_mm256_set1_epi16(INT16_MAX);
  const simde__m256i a0 = simde_mm256_min_epu16(simde_mm256_abs_epi16(x0), sat);
  const simde__m256i a1 = simde_mm256_min_epu16(simde_mm256_abs_epi16(x1), sat);
  const simde__m256i a2 = simde_mm256_min_epu16(simde_mm256_abs_epi16(x2), sat);
  // lane 0: max over P0's groups, lane 1: over P1's; then across the lane, into every 32-bit word
  simde__m256i m = simde_mm256_max_epu16(simde_mm256_permute2x128_si256(a0, a2, 0x21), simde_mm256_blend_epi32(a0, a2, 0xF0));
  m = simde_mm256_max_epu16(m, a1);
  m = simde_mm256_max_epu16(m, simde_mm256_shuffle_epi32(m, 0x4E));
  m = simde_mm256_max_epu16(m, simde_mm256_shuffle_epi32(m, 0xB1));
  m = simde_mm256_max_epu16(m, simde_mm256_srli_epi32(m, 16));
  m = simde_mm256_max_epi32(simde_mm256_and_si256(m, simde_mm256_set1_epi32(0xFFFF)), simde_mm256_set1_epi32(1));
  // floor(log2(max |x|)) + 2 - W, as bfp_exponent()
  const simde__m256i log2 = simde_mm256_srli_epi32(simde_mm256_castps_si256(simde_mm256_cvtepi32_ps(m)), 23);
  const simde__m256i exponent =
      simde_mm256_max_epi32(simde_mm256_sub_epi32(log2, simde_mm256_set1_epi32(127 - 2 + W)), simde_mm256_setzero_si256());
  simde__m256i scale =
      simde_mm256_sllv_epi32(simde_mm256_set1_epi32(1), simde_mm256_sub_epi32(simde_mm256_set1_epi32(16 - W), exponent));
  scale = simde_mm256_or_si256(scale, simde_mm256_slli_epi32(scale, 16));
  const simde__m256i scale0 = simde_mm256_permute2x128_si256(scale, scale, 0x00);
  const simde__m256i scale1 = simde_mm256_permute2x128_si256(scale, scale, 0x11);
  const simde__m256i packed0 = bfp_pack_groups<W>(simde_mm256_srli_epi16(simde_mm256_mullo_epi16(x0, scale0), 16 - W), order);
  const simde__m256i packed1 = bfp_pack_groups<W>(simde_mm256_srli_epi16(simde_mm256_mullo_epi16(x1, scale), 16 - W), order);
  const simde__m256i packed2 = bfp_pack_groups<W>(simde_mm256_srli_epi16(simde_mm256_mullo_epi16(x2, scale1), 16 - W), order);

  // In memory order, so that each store's overrun is overwritten by the next one.
  uint8_t *out0 = (uint8_t *)dst;
  uint8_t *out1 = out0 + FH_COMP_PRB_BYTES(W);
  out0[0] = (uint8_t)simde_mm256_extract_epi32(exponent, 0);
  simde_mm_storeu_si128((simde__m128i *)(out0 + 1), simde_mm256_castsi256_si128(packed0));
  simde_mm_storeu_si128((simde__m128i *)(out0 + 1 + W), simde_mm256_extracti128_si256(packed0, 1));
  simde_mm_storeu_si128((simde__m128i *)(out0 + 1 + 2 * W), simde_mm256_castsi256_si128(packed1));
  out1[0] = (uint8_t)simde_mm256_extract_epi32(exponent, 4);
  simde_mm_storeu_si128((simde__m128i *)(out1 + 1), simde_mm256_extracti128_si256(packed1, 1));
  simde_mm_storeu_si128((simde__m128i *)(out1 + 1 + W), simde_mm256_castsi256_si128(packed2));
  simde_mm_storeu_si128((simde__m128i *)(out1 + 1 + 2 * W), simde_mm256_extracti128_si256(packed2, 1));
}

template <int W>
static void bfp_compress_prbs(int n_prb, const int16_t *src, int8_t *dst)
{
  int p = 0;
  if constexpr (W >= 9 && W <= 14) {
    const simde__m256i order = bfp_pack_order<W>();
    for (; p + 2 < n_prb; p += 2)
      bfp_compress_prb_pair<W>(src + p * FH_VALS_PER_PRB, dst + p * FH_COMP_PRB_BYTES(W), order);
  }
  for (; p < n_prb; p++) {
    if constexpr (W <= 14)
      bfp_compress_prb_simd<W>(src + p * FH_VALS_PER_PRB, dst + p * FH_COMP_PRB_BYTES(W));
    else
      bfp_compress_prb_scalar<W>(src + p * FH_VALS_PER_PRB, dst + p * FH_COMP_PRB_BYTES(W));
  }
}

// Scalar BFP decompression for any width: one 8-value group (W bytes) at a time, each value
// sign-extended from bit W - 1 and scaled back up.
template <int W>
__attribute__((always_inline)) static inline void bfp_decompress_prb_scalar(const int8_t *src, int16_t *dst)
{
  const int exponent = (int)(uint8_t)src[0];
  const uint8_t *in = (const uint8_t *)(src + 1);
  for (int g = 0; g < 3; g++) {
    const unsigned __int128 group = get_group_be<W>(in + g * W);
    for (int i = 0; i < 8; i++) {
      const uint64_t raw = (uint64_t)(group >> (W * (7 - i)));
      const int32_t v = (int32_t)((int64_t)(raw << (64 - W)) >> (64 - W));
      dst[8 * g + i] = (int16_t)((uint32_t)v << exponent);
    }
  }
}

// Constants for the vector decompressor: for each of a group's 8 values, the byte shuffle that
// puts the (up to) 3 bytes holding it big-endian into the top of a 32-bit lane, and the left
// shift that then brings its first bit to bit 31.
struct bfp_unpack_ctl_t {
  simde__m256i gather;
  simde__m256i align;
};

template <int W>
static bfp_unpack_ctl_t bfp_unpack_ctl()
{
  uint8_t gather[32];
  uint32_t align[8];
  for (int i = 0; i < 8; i++) {
    const int bit = i * W;
    // Both 128-bit halves hold the same group bytes, so indices are relative to the group.
    gather[4 * i + 0] = 0x80; // zero
    gather[4 * i + 1] = (uint8_t)(bit / 8 + 2);
    gather[4 * i + 2] = (uint8_t)(bit / 8 + 1);
    gather[4 * i + 3] = (uint8_t)(bit / 8);
    align[i] = bit % 8;
  }
  bfp_unpack_ctl_t ctl;
  ctl.gather = simde_mm256_loadu_si256((const simde__m256i *)gather);
  ctl.align = simde_mm256_loadu_si256((const simde__m256i *)align);
  return ctl;
}

// One group of 8 values -> 8 sign-extended 32-bit lanes. Reads 16 bytes from `in` (W of them
// are the group), so the caller must guarantee 16 readable bytes.
template <int W>
__attribute__((always_inline)) static inline simde__m256i bfp_unpack_group(const uint8_t *in, const bfp_unpack_ctl_t &ctl)
{
  const simde__m256i bytes = simde_mm256_broadcastsi128_si256(simde_mm_loadu_si128((const simde__m128i *)in));
  const simde__m256i lanes = simde_mm256_sllv_epi32(simde_mm256_shuffle_epi8(bytes, ctl.gather), ctl.align);
  return simde_mm256_srai_epi32(lanes, 32 - W);
}

// Vector BFP decompression. The last group of a PRB reads past its end (up to 16 - W bytes),
// so this is only used when at least 16 - W more bytes follow the PRB.
template <int W>
__attribute__((always_inline)) static inline void bfp_decompress_prb_simd(const int8_t *src,
                                                                          int16_t *dst,
                                                                          const bfp_unpack_ctl_t &ctl)
{
  const simde__m128i exp_cnt = simde_mm_cvtsi32_si128((uint8_t)src[0]);
  const uint8_t *in = (const uint8_t *)(src + 1);
  // Truncate to 16 bits like the scalar path's (int16_t) cast: mask, then an unsigned pack that
  // can no longer saturate. packus interleaves the 128-bit halves, permute4x64 restores order.
  const simde__m256i low16 = simde_mm256_set1_epi32(0xFFFF);
  const simde__m256i g0 = simde_mm256_and_si256(bfp_unpack_group<W>(in, ctl), low16);
  const simde__m256i g1 = simde_mm256_and_si256(bfp_unpack_group<W>(in + W, ctl), low16);
  const simde__m256i g2 = simde_mm256_and_si256(bfp_unpack_group<W>(in + 2 * W, ctl), low16);
  const simde__m256i v01 = simde_mm256_permute4x64_epi64(simde_mm256_packus_epi32(g0, g1), 0xD8);
  const simde__m256i v22 = simde_mm256_permute4x64_epi64(simde_mm256_packus_epi32(g2, g2), 0xD8);
  simde_mm256_storeu_si256((simde__m256i *)dst, simde_mm256_sll_epi16(v01, exp_cnt));
  simde_mm_storeu_si128((simde__m128i *)(dst + 16), simde_mm_sll_epi16(simde_mm256_castsi256_si128(v22), exp_cnt));
}

// Two-PRB BFP decompression, 4 <= W <= 9, where each value spans at most two bytes. So values stay
// in 16-bit lanes, 16 per register, one group per 128-bit lane as in bfp_compress_prb_pair(): a byte
// shuffle puts each value's two bytes big-endian in its lane, a multiply by 2^(bit offset) brings
// its first bit to bit 15, an arithmetic shift sign-extends it, and a multiply by 2^exponent
// (per lane, so per PRB) scales it. Each lane loads 16 bytes from its group, so the last one reads
// up to 16 - W bytes into the PRB after the pair, which must exist.
struct bfp_unpack16_ctl_t {
  simde__m256i gather;
  simde__m256i align;
};

template <int W>
static bfp_unpack16_ctl_t bfp_unpack16_ctl()
{
  uint8_t gather[32];
  int16_t align[16];
  for (int i = 0; i < 8; i++) {
    const int bit = i * W;
    gather[2 * i] = gather[16 + 2 * i] = (uint8_t)(bit / 8 + 1);
    gather[2 * i + 1] = gather[16 + 2 * i + 1] = (uint8_t)(bit / 8);
    align[i] = align[8 + i] = (int16_t)(1 << (bit % 8));
  }
  bfp_unpack16_ctl_t ctl;
  ctl.gather = simde_mm256_loadu_si256((const simde__m256i *)gather);
  ctl.align = simde_mm256_loadu_si256((const simde__m256i *)align);
  return ctl;
}

// 2^exponent truncated to 16 bits, like the scalar path's (int16_t)(v << exponent)
__attribute__((always_inline)) static inline simde__m128i bfp_scale16(int8_t exponent_byte)
{
  const int exponent = (uint8_t)exponent_byte;
  return simde_mm_set1_epi16(exponent < 16 ? (int16_t)(1u << exponent) : 0);
}

// Two groups (one per 128-bit lane, 16 readable bytes each) -> 16 values scaled by `scale`.
template <int W>
__attribute__((always_inline)) static inline simde__m256i bfp_unpack16_groups(const uint8_t *in_lo,
                                                                              const uint8_t *in_hi,
                                                                              simde__m256i scale,
                                                                              const bfp_unpack16_ctl_t &ctl)
{
  const simde__m256i bytes = simde_mm256_loadu2_m128i((const simde__m128i *)in_hi, (const simde__m128i *)in_lo);
  const simde__m256i lanes = simde_mm256_mullo_epi16(simde_mm256_shuffle_epi8(bytes, ctl.gather), ctl.align);
  return simde_mm256_mullo_epi16(simde_mm256_srai_epi16(lanes, 16 - W), scale);
}

template <int W>
__attribute__((always_inline)) static inline void bfp_decompress_prb_pair(const int8_t *src,
                                                                          int16_t *dst,
                                                                          const bfp_unpack16_ctl_t &ctl)
{
  static_assert(W >= 4 && W <= 9, "values must span at most two bytes");
  const int8_t *src1 = src + FH_COMP_PRB_BYTES(W);
  const uint8_t *in0 = (const uint8_t *)(src + 1);
  const uint8_t *in1 = (const uint8_t *)(src1 + 1);
  const simde__m128i scale0 = bfp_scale16(src[0]);
  const simde__m128i scale1 = bfp_scale16(src1[0]);
  simde_mm256_storeu_si256((simde__m256i *)dst, bfp_unpack16_groups<W>(in0, in0 + W, simde_mm256_set_m128i(scale0, scale0), ctl));
  simde_mm256_storeu_si256((simde__m256i *)(dst + 16),
                           bfp_unpack16_groups<W>(in0 + 2 * W, in1, simde_mm256_set_m128i(scale1, scale0), ctl));
  simde_mm256_storeu_si256((simde__m256i *)(dst + 32),
                           bfp_unpack16_groups<W>(in1 + W, in1 + 2 * W, simde_mm256_set_m128i(scale1, scale1), ctl));
}

template <int W>
static void bfp_decompress_prbs(int n_prb, const int8_t *src, int16_t *dst)
{
  int p = 0;
  if constexpr (W >= 4 && W <= 9) {
    const bfp_unpack16_ctl_t ctl = bfp_unpack16_ctl<W>();
    for (; p + 2 < n_prb; p += 2)
      bfp_decompress_prb_pair<W>(src + p * FH_COMP_PRB_BYTES(W), dst + p * FH_VALS_PER_PRB, ctl);
  }
  // The vector path reads 16 bytes from the PRB's last group (offset 1 + 2W): up to 16 - W bytes
  // past its end, which stays inside the next PRB (1 + 3W bytes) for W >= 4. So every PRB but the
  // last one takes it, at the widths where that holds.
  if constexpr (W >= 4) {
    const bfp_unpack_ctl_t ctl = bfp_unpack_ctl<W>();
    for (; p < n_prb - 1; p++)
      bfp_decompress_prb_simd<W>(src + p * FH_COMP_PRB_BYTES(W), dst + p * FH_VALS_PER_PRB, ctl);
  }
  for (; p < n_prb; p++)
    bfp_decompress_prb_scalar<W>(src + p * FH_COMP_PRB_BYTES(W), dst + p * FH_VALS_PER_PRB);
}

static void bfp_compress_prbs(int iq_bits, int n_prb, const int16_t *src, int8_t *dst)
{
  switch (iq_bits) {
    case 1:
      return bfp_compress_prbs<1>(n_prb, src, dst);
    case 2:
      return bfp_compress_prbs<2>(n_prb, src, dst);
    case 3:
      return bfp_compress_prbs<3>(n_prb, src, dst);
    case 4:
      return bfp_compress_prbs<4>(n_prb, src, dst);
    case 5:
      return bfp_compress_prbs<5>(n_prb, src, dst);
    case 6:
      return bfp_compress_prbs<6>(n_prb, src, dst);
    case 7:
      return bfp_compress_prbs<7>(n_prb, src, dst);
    case 8:
      return bfp_compress_prbs<8>(n_prb, src, dst);
    case 9:
      return bfp_compress_prbs<9>(n_prb, src, dst);
    case 10:
      return bfp_compress_prbs<10>(n_prb, src, dst);
    case 11:
      return bfp_compress_prbs<11>(n_prb, src, dst);
    case 12:
      return bfp_compress_prbs<12>(n_prb, src, dst);
    case 13:
      return bfp_compress_prbs<13>(n_prb, src, dst);
    case 14:
      return bfp_compress_prbs<14>(n_prb, src, dst);
    case 15:
      return bfp_compress_prbs<15>(n_prb, src, dst);
    case 16:
      return bfp_compress_prbs<16>(n_prb, src, dst);
    default:
      AssertFatal(0, "iq_bits %d out of range [1..16]\n", iq_bits);
  }
}

static void bfp_decompress_prbs(int iq_bits, int n_prb, const int8_t *src, int16_t *dst)
{
  switch (iq_bits) {
    case 1:
      return bfp_decompress_prbs<1>(n_prb, src, dst);
    case 2:
      return bfp_decompress_prbs<2>(n_prb, src, dst);
    case 3:
      return bfp_decompress_prbs<3>(n_prb, src, dst);
    case 4:
      return bfp_decompress_prbs<4>(n_prb, src, dst);
    case 5:
      return bfp_decompress_prbs<5>(n_prb, src, dst);
    case 6:
      return bfp_decompress_prbs<6>(n_prb, src, dst);
    case 7:
      return bfp_decompress_prbs<7>(n_prb, src, dst);
    case 8:
      return bfp_decompress_prbs<8>(n_prb, src, dst);
    case 9:
      return bfp_decompress_prbs<9>(n_prb, src, dst);
    case 10:
      return bfp_decompress_prbs<10>(n_prb, src, dst);
    case 11:
      return bfp_decompress_prbs<11>(n_prb, src, dst);
    case 12:
      return bfp_decompress_prbs<12>(n_prb, src, dst);
    case 13:
      return bfp_decompress_prbs<13>(n_prb, src, dst);
    case 14:
      return bfp_decompress_prbs<14>(n_prb, src, dst);
    case 15:
      return bfp_decompress_prbs<15>(n_prb, src, dst);
    case 16:
      return bfp_decompress_prbs<16>(n_prb, src, dst);
    default:
      AssertFatal(0, "iq_bits %d out of range [1..16]\n", iq_bits);
  }
}

/* ---- BLKSCALE ---- */

static void blkscale_compress_prb(const int16_t *src, int8_t *dst, int iq_bits)
{
  int32_t max_abs = 1;
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    int32_t v = src[i] < 0 ? -(int32_t)src[i] : (int32_t)src[i];
    if (v > max_abs)
      max_abs = v;
  }
  int bit_width = 32 - __builtin_clz((uint32_t)max_abs);
  int shift = bit_width - (iq_bits - 1);
  if (shift < 0)
    shift = 0;
  dst[0] = (int8_t)shift;
  int32_t vals[FH_VALS_PER_PRB];
  const int32_t clip_max = (1 << (iq_bits - 1)) - 1;
  const int32_t clip_min = -(1 << (iq_bits - 1));
  for (int i = 0; i < FH_VALS_PER_PRB; i++) {
    int32_t val = src[i] >> shift;
    if (val > clip_max)
      val = clip_max;
    else if (val < clip_min)
      val = clip_min;
    vals[i] = val;
  }
  pack_prb((uint8_t *)(dst + 1), vals, iq_bits);
}

static void blkscale_decompress_prb(const int8_t *src, int16_t *dst, int iq_bits)
{
  int shift = (int)(uint8_t)src[0];
  int32_t vals[FH_VALS_PER_PRB];
  unpack_prb((const uint8_t *)(src + 1), vals, iq_bits);
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    dst[i] = (int16_t)((uint32_t)vals[i] << shift);
}

/* ---- ULAW ---- */

/* G.711 mu-law constants (ITU-T G.711) */
constexpr int ULAW_BIAS = 33; /* additive bias before segmentation */
constexpr int ULAW_CLIP = 32734; /* saturation level: INT16_MAX - ULAW_BIAS */
constexpr int ULAW_NORM = 127; /* segment normalization factor */
constexpr int ULAW_SEG_MASK = 0x07; /* 3-bit segment index */

static int16_t ulaw_encode(int16_t s, int iq_bits)
{
  int sign = s < 0 ? -1 : 1;
  int x = s < 0 ? -(int)s : (int)s;
  x = x > ULAW_CLIP ? ULAW_CLIP : x;
  x += ULAW_BIAS;
  int seg = 0;
  for (int t = x >> 5; t > 1 && seg < 7; t >>= 1)
    seg++;
  int code = (((x >> (seg + 1)) & 0x0F) | (seg << 4)) ^ 0x7F;
  int peak = (1 << (iq_bits - 1)) - 1;
  return (int16_t)(sign * (code * peak / ULAW_NORM));
}

static int16_t ulaw_decode(int16_t s, int iq_bits)
{
  int sign = s < 0 ? -1 : 1;
  int x = s < 0 ? -(int)s : (int)s;
  int peak = (1 << (iq_bits - 1)) - 1;
  if (peak == 0)
    return 0;
  int code = x * ULAW_NORM / peak;
  code ^= 0x7F;
  int seg = (code >> 4) & ULAW_SEG_MASK;
  int decoded = (((code & 0x0F) << 1) | 1) << (seg + 2);
  decoded -= ULAW_BIAS;
  if (decoded < 0)
    decoded = 0;
  return (int16_t)(sign * decoded);
}

static void ulaw_compress_prb(const int16_t *src, int8_t *dst, int iq_bits)
{
  dst[0] = 0;
  int32_t vals[FH_VALS_PER_PRB];
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    vals[i] = ulaw_encode(src[i], iq_bits);
  pack_prb((uint8_t *)(dst + 1), vals, iq_bits);
}

static void ulaw_decompress_prb(const int8_t *src, int16_t *dst, int iq_bits)
{
  int32_t vals[FH_VALS_PER_PRB];
  unpack_prb((const uint8_t *)(src + 1), vals, iq_bits);
  for (int i = 0; i < FH_VALS_PER_PRB; i++)
    dst[i] = ulaw_decode((int16_t)vals[i], iq_bits);
}

/* ---- public API ---- */

void fh_compress_prbs(fh_comp_method_t method, int iq_bits, int n_prb, const int16_t *src, int8_t *dst)
{
  AssertFatal(method != FH_COMP_NONE, "fh_compress_prbs called with FH_COMP_NONE\n");
  AssertFatal(iq_bits >= 1 && iq_bits <= 16, "iq_bits %d out of range [1..16]\n", iq_bits);
  if (method == FH_COMP_BFP) {
    bfp_compress_prbs(iq_bits, n_prb, src, dst);
    return;
  }
  const int src_stride = FH_VALS_PER_PRB;
  const int dst_stride = FH_COMP_PRB_BYTES(iq_bits);
  for (int p = 0; p < n_prb; p++) {
    switch (method) {
      case FH_COMP_BLKSCALE:
        blkscale_compress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      case FH_COMP_ULAW:
        ulaw_compress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      default:
        AssertFatal(0, "Unsupported compression method %d\n", method);
    }
  }
}

void fh_decompress_prbs(fh_comp_method_t method, int iq_bits, int n_prb, const int8_t *src, int16_t *dst)
{
  AssertFatal(method != FH_COMP_NONE, "fh_decompress_prbs called with FH_COMP_NONE\n");
  AssertFatal(iq_bits >= 1 && iq_bits <= 16, "iq_bits %d out of range [1..16]\n", iq_bits);
  if (method == FH_COMP_BFP) {
    bfp_decompress_prbs(iq_bits, n_prb, src, dst);
    return;
  }
  const int src_stride = FH_COMP_PRB_BYTES(iq_bits);
  const int dst_stride = FH_VALS_PER_PRB;
  for (int p = 0; p < n_prb; p++) {
    switch (method) {
      case FH_COMP_BLKSCALE:
        blkscale_decompress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      case FH_COMP_ULAW:
        ulaw_decompress_prb(src + p * src_stride, dst + p * dst_stride, iq_bits);
        break;
      default:
        AssertFatal(0, "Unsupported compression method %d\n", method);
    }
  }
}

// Decompresses n_vals values that need not be a whole number of PRBs (beamforming weights).
// BFP and block scaling both shift each mantissa up by the comp param byte.
void fh_decompress_block(fh_comp_method_t method, int iq_bits, int n_vals, const int8_t *src, int16_t *dst)
{
  AssertFatal(method != FH_COMP_NONE, "fh_decompress_block called with FH_COMP_NONE\n");
  AssertFatal(iq_bits >= 1 && iq_bits <= 16, "iq_bits %d out of range [1..16]\n", iq_bits);
  AssertFatal(method == FH_COMP_BFP || method == FH_COMP_BLKSCALE || method == FH_COMP_ULAW,
              "Unsupported compression method %d\n",
              method);
  const int param = (int)(uint8_t)src[0];
  const uint8_t *in = (const uint8_t *)(src + 1);
  for (int i = 0; i < n_vals; i++) {
    const int32_t v = unpack_bits(in, i * iq_bits, iq_bits);
    dst[i] = method == FH_COMP_ULAW ? ulaw_decode((int16_t)v, iq_bits) : (int16_t)((uint32_t)v << param);
  }
}

void fh_compress_prach(fh_comp_method_t method, int iq_bits, int kbar, const int16_t *src, int8_t *dst)
{
  const int total_vals = FH_PRACH_NUM_PRBS * FH_VALS_PER_PRB;
  int16_t padded[FH_PRACH_NUM_PRBS * FH_VALS_PER_PRB];
  AssertFatal(kbar >= 0 && kbar + FH_PRACH_NUM_SUBCARRIERS * 2 <= total_vals, "PRACH kbar %d out of range\n", kbar);
  memset(padded, 0, total_vals * sizeof(int16_t));
  memcpy(padded + kbar, src, FH_PRACH_NUM_SUBCARRIERS * 2 * sizeof(int16_t));
  fh_compress_prbs(method, iq_bits, FH_PRACH_NUM_PRBS, padded, dst);
}
