/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "xran_pkt_bfw.h"
#include "xran_pkt_cp.h"

void exit_function(const char *file, const char *function, const int line, const char *s, const int assertflag)
{
  fprintf(stderr, "Error at %s:%s:%d - %s\n", file, function, line, s ? s : "None");
  exit(1);
}

// Writes the low width bits of v MSB first at bit_offset (buf must be zeroed)
static void pack_value(uint8_t *buf, size_t bit_offset, int32_t v, int width)
{
  uint32_t bits = (uint32_t)v & ((1u << width) - 1);
  for (int b = 0; b < width; b++) {
    size_t bo = bit_offset + b;
    buf[bo / 8] |= (uint8_t)(((bits >> (width - 1 - b)) & 1u) << (7 - bo % 8));
  }
}

// Builds a well-formed ext1 buffer by hand: 3-byte fixed header, optional 1-byte
// comp param, then n_weights (bfwI, bfwQ) pairs packed at iq_bits width, zero-padded
// to a 4-byte boundary. Returns the total length written.
static size_t build_ext1(uint8_t *buf,
                         uint8_t comp_meth,
                         uint8_t iq_bits_field,
                         int iq_bits,
                         uint8_t comp_param,
                         const int16_t *iq_pairs,
                         int n_weights)
{
  memset(buf, 0, 256);
  buf[0] = 1; // extType=1, ef=0
  buf[2] = (uint8_t)((comp_meth & 0x0F) | ((iq_bits_field & 0x0F) << 4));

  size_t bit_offset = 24; // after the 3 fixed header bytes
  bool has_comp_param = comp_meth != 0;
  if (has_comp_param) {
    buf[3] = comp_param;
    bit_offset += 8;
  }
  for (int i = 0; i < 2 * n_weights; i++)
    pack_value(buf, bit_offset + i * iq_bits, iq_pairs[i], iq_bits);
  size_t total_bits = bit_offset + (size_t)2 * n_weights * iq_bits;
  size_t total_bytes = (total_bits + 7) / 8;
  size_t padded = ((total_bytes + 3) / 4) * 4;
  buf[1] = (uint8_t)(padded / 4);
  return padded;
}

typedef struct {
  bool disable_bfws;
  bool rad;
  uint8_t bundle_offset;
  uint8_t num_bund_prb;
  uint8_t comp_meth;
  uint8_t iq_width_field; // bfwIqWidth as sent, 0 = 16 bits
  int iq_bits;
  int n_weights;
  int n_bundles;
  const uint8_t *comp_params; // one per bundle, unused if NONE
  const uint16_t *beam_ids; // raw 16-bit field, contInd included
  const int16_t *iq; // n_bundles * 2 * n_weights values
} ext11_desc_t;

// Builds a well-formed ext11 buffer by hand (CUS v16.01 Tables 7.7.11.1-1/-2): 6-byte header (5 if disableBFWs), then
// per bundle [bfwCompParam] contInd|beamId [(bfwI, bfwQ)+], zero-padded to a 4-byte boundary. Returns the length.
static size_t build_ext11(uint8_t *buf, const ext11_desc_t *d)
{
  memset(buf, 0, 1024);
  buf[0] = 11; // extType=11, ef=0
  buf[3] = (uint8_t)((d->disable_bfws << 7) | (d->rad << 6) | (d->bundle_offset & 0x3F));
  buf[4] = d->num_bund_prb;
  size_t offset = 5;
  if (!d->disable_bfws)
    buf[offset++] = (uint8_t)((d->comp_meth & 0x0F) | (d->iq_width_field << 4));
  for (int b = 0; b < d->n_bundles; b++) {
    if (!d->disable_bfws && d->comp_meth != XRAN_BFWCOMPMETHOD_NONE)
      buf[offset++] = d->comp_params[b];
    buf[offset++] = d->beam_ids[b] >> 8;
    buf[offset++] = d->beam_ids[b] & 0xFF;
    if (d->disable_bfws)
      continue;
    for (int i = 0; i < 2 * d->n_weights; i++)
      pack_value(buf, offset * 8 + i * d->iq_bits, d->iq[2 * b * d->n_weights + i], d->iq_bits);
    offset += ((size_t)2 * d->n_weights * d->iq_bits + 7) / 8;
  }
  size_t padded = (offset + 3) & ~(size_t)3;
  buf[1] = (uint8_t)((padded / 4) >> 8);
  buf[2] = (uint8_t)(padded / 4);
  return padded;
}

static void test_bfp_known_vector(void)
{
  printf("Testing ext1 BFP decode of hand-constructed vector...\n");
  // BFP, iq_bits=8, exponent=0: weights (I=4,Q=-4), (I=100,Q=-100).
  int16_t iq[4] = {4, -4, 100, -100};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKFLOAT, 8, 8, 0 /* exponent */, iq, 2);

  c16_t weights[2];
  assert(xran_decode_bfw_ext1(buf, len, 2, weights) == XRAN_BFW_OK);
  assert(weights[0].r == 4 && weights[0].i == -4);
  assert(weights[1].r == 100 && weights[1].i == -100);
  printf("ext1 BFP known-vector check passed!\n");
}

static void test_bfp_odd_width_known_vector(void)
{
  printf("Testing ext1 BFP decode with 9-bit values crossing byte boundaries...\n");
  // BFP, iq_bits=9, exponent=0: 3 weights -> 54 bits of payload, not byte aligned.
  int16_t iq[6] = {255, -256, 1, -1, -100, 77};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKFLOAT, 9, 9, 0, iq, 3);

  c16_t weights[3];
  assert(xran_decode_bfw_ext1(buf, len, 3, weights) == XRAN_BFW_OK);
  for (int i = 0; i < 3; i++)
    assert(weights[i].r == iq[2 * i] && weights[i].i == iq[2 * i + 1]);
  printf("ext1 BFP 9-bit check passed!\n");
}

static void test_none_known_vector(void)
{
  printf("Testing ext1 NONE (uncompressed) decode of hand-constructed vector...\n");
  // NONE, iq_bits=16: no comp param byte, raw 16-bit signed values.
  int16_t iq[4] = {12345, -12345, 1, -1};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_NONE, 0 /* -> iq_bits=16 */, 16, 0, iq, 2);

  c16_t weights[2];
  assert(xran_decode_bfw_ext1(buf, len, 2, weights) == XRAN_BFW_OK);
  assert(weights[0].r == 12345 && weights[0].i == -12345);
  assert(weights[1].r == 1 && weights[1].i == -1);
  printf("ext1 NONE known-vector check passed!\n");
}

static void test_blkscale_known_vector(void)
{
  printf("Testing ext1 BLKSCALE decode of hand-constructed vector...\n");
  // BLKSCALE, iq_bits=8, shift=0: weights (I=10,Q=-10).
  int16_t iq[2] = {10, -10};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKSCALE, 8, 8, 0 /* shift */, iq, 1);

  c16_t weights[1];
  assert(xran_decode_bfw_ext1(buf, len, 1, weights) == XRAN_BFW_OK);
  assert(weights[0].r == 10 && weights[0].i == -10);
  printf("ext1 BLKSCALE known-vector check passed!\n");
}

static void test_ulaw_known_vector(void)
{
  printf("Testing ext1 ULAW decode of hand-constructed vector...\n");
  // ULAW, iq_bits=8: raw packed values (I=32,Q=-32) feed ulaw_decode()'s mu-law expansion.
  // Hand-computed per fh_compression.c's ulaw_decode(): x=32, code = 32*127/127 = 32,
  // code ^= 0x7F -> 95, seg = (95>>4)&7 = 5, decoded = (((95&0xF)<<1)|1)<<(5+2) = 31<<7 = 3968,
  // decoded -= 33 (ULAW_BIAS) -> 3935. Sign follows the raw input's sign.
  int16_t iq[2] = {32, -32};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_ULAW, 8, 8, 0 /* comp param unused by ulaw decode */, iq, 1);

  c16_t weights[1];
  assert(xran_decode_bfw_ext1(buf, len, 1, weights) == XRAN_BFW_OK);
  assert(weights[0].r == 3935 && weights[0].i == -3935);
  printf("ext1 ULAW known-vector check passed!\n");
}

static void test_weight_count_mismatch_rejected(void)
{
  printf("Testing ext1 decode rejects a weight count that doesn't match extLen...\n");
  // 1 weight (3 hdr + 1 param + 2 IQ bytes) padded to 8 bytes. The decoder only ever produces
  // the caller's configured count, and a count whose padded size differs from extLen is rejected.
  // (A count that fits in the padding, here 2, gives the same extLen and can't be told apart on
  // the wire, which is why the count comes from configuration and not from extLen.)
  int16_t iq[2] = {10, -10};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKSCALE, 8, 8, 0, iq, 1);
  assert(len == 8);

  c16_t weights[8];
  memset(weights, 0x5A, sizeof(weights));
  assert(xran_decode_bfw_ext1(buf, len, 3, weights) == XRAN_BFW_ERR_EXT_LEN);
  assert(xran_decode_bfw_ext1(buf, len, 8, weights) == XRAN_BFW_ERR_EXT_LEN);
  // Nothing written on rejection.
  for (int i = 0; i < 8; i++)
    assert(weights[i].r == 0x5A5A && weights[i].i == 0x5A5A);

  // Fewer weights than carried is also a mismatch (4 weights = 12 bytes, 2 weights = 8 bytes).
  int16_t iq4[8] = {1, -1, 2, -2, 3, -3, 4, -4};
  len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKFLOAT, 8, 8, 0, iq4, 4);
  assert(len == 12);
  assert(xran_decode_bfw_ext1(buf, len, 2, weights) == XRAN_BFW_ERR_EXT_LEN);
  assert(xran_decode_bfw_ext1(buf, len, 4, weights) == XRAN_BFW_OK);
  printf("ext1 weight count mismatch rejection passed!\n");
}

static void test_malformed_inputs_rejected(void)
{
  printf("Testing ext1 decode rejects malformed input...\n");
  int16_t iq[2] = {1, -1};
  uint8_t buf[256];
  size_t len = build_ext1(buf, XRAN_BFWCOMPMETHOD_BLKFLOAT, 8, 8, 0, iq, 1);
  c16_t weights[1];

  // Truncated buffer (len smaller than extLen*4 claims).
  assert(xran_decode_bfw_ext1(buf, len - 1, 1, weights) == XRAN_BFW_ERR_TRUNCATED);
  assert(xran_decode_bfw_ext1(buf, 2, 1, weights) == XRAN_BFW_ERR_TRUNCATED);
  // NULL/zero-sized arguments are caller errors (AssertError() logs them).
  assert(xran_decode_bfw_ext1(NULL, len, 1, weights) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext1(buf, len, 1, NULL) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext1(buf, len, 0, weights) == XRAN_BFW_ERR_ARG);

  // extLen claiming more words than the weights need.
  buf[1]++;
  assert(xran_decode_bfw_ext1(buf, sizeof(buf), 1, weights) == XRAN_BFW_ERR_EXT_LEN);
  buf[1]--;
  // extLen = 0.
  uint8_t saved = buf[1];
  buf[1] = 0;
  assert(xran_decode_bfw_ext1(buf, len, 1, weights) == XRAN_BFW_ERR_EXT_LEN);
  buf[1] = saved;

  // Beamspace and reserved bfwCompMeth values come from the wire and must not abort.
  for (int meth = XRAN_BFWCOMPMETHOD_BEAMSPACE; meth <= 0xF; meth++) {
    buf[2] = (uint8_t)((meth & 0x0F) | (8 << 4));
    assert(xran_decode_bfw_ext1(buf, len, 1, weights) == XRAN_BFW_ERR_COMP_METH);
  }
  printf("ext1 malformed-input rejection passed!\n");
}

static void test_section_ext_len(void)
{
  printf("Testing section extension length...\n");
  uint8_t ext1[8] = {0x81, 2}; // ef=1, extType=1, extLen=2
  size_t len = 0;
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, ext1, sizeof(ext1), &len) == XRAN_BFW_OK && len == 8);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, ext1, 7, &len) == XRAN_BFW_ERR_TRUNCATED); // runs past the packet
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, ext1, 1, &len) == XRAN_BFW_ERR_TRUNCATED);
  ext1[1] = 0;
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, ext1, sizeof(ext1), &len) == XRAN_BFW_ERR_EXT_LEN); // reserved
  assert(len == 8); // not written on error
  // extLen is 16 bits for ext11/19/20: 0x0102 words
  uint8_t ext11[0x102 * 4] = {11, 0x01, 0x02};
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_11, ext11, sizeof(ext11), &len) == XRAN_BFW_OK && len == 0x102 * 4);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_11, ext11, 2, &len) == XRAN_BFW_ERR_TRUNCATED);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_19, ext11, sizeof(ext11), &len) == XRAN_BFW_OK && len == 0x102 * 4);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_20, ext11, sizeof(ext11), &len) == XRAN_BFW_OK && len == 0x102 * 4);
  // Any other type has an 8-bit extLen: 1 word
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_6, ext11, sizeof(ext11), &len) == XRAN_BFW_OK && len == 4);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, NULL, sizeof(ext11), &len) == XRAN_BFW_ERR_ARG);
  assert(xran_section_ext_len(XRAN_CP_SECTIONEXTCMD_1, ext1, sizeof(ext1), NULL) == XRAN_BFW_ERR_ARG);
  printf("section extension length passed!\n");
}

static void test_ext11_none_known_vector(void)
{
  printf("Testing ext11 NONE decode with an orphan PRB bundle...\n");
  // 5 PRBs, 2 per bundle: bundles {0,1} {2,3} {4}, 2 weights each, 16-bit.
  // The middle beamId has contInd set, which is not part of the beamId.
  uint16_t beam_ids[3] = {5, 0x8000 | 0x7001, 0x7FFF};
  int16_t iq[12] = {12345, -12345, 1, -1, 2, -2, 3, -3, -32768, 32767, 0, 0};
  ext11_desc_t d = {.rad = true,
                    .num_bund_prb = 2,
                    .comp_meth = XRAN_BFWCOMPMETHOD_NONE,
                    .iq_width_field = 0,
                    .iq_bits = 16,
                    .n_weights = 2,
                    .n_bundles = 3,
                    .beam_ids = beam_ids,
                    .iq = iq};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  assert(len == 36); // 6 + 3 * (2 + 8) = 36

  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[4];
  c16_t w[8];
  assert(xran_decode_bfw_ext11(buf, len, 5, 2, 4, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 3);
  assert(!hdr.disableBFWs && hdr.RAD && hdr.bundleOffset == 0 && hdr.numBundPrb == 2);
  assert(hdr.bfwCompMeth == XRAN_BFWCOMPMETHOD_NONE && hdr.bfwIqWidth == 16);
  assert(ids[0] == 5 && ids[1] == 0x7001 && ids[2] == 0x7FFF);
  for (int i = 0; i < 6; i++)
    assert(w[i].r == iq[2 * i] && w[i].i == iq[2 * i + 1]);
  printf("ext11 NONE known-vector check passed!\n");
}

static void test_ext11_bfp_per_bundle_exponent(void)
{
  printf("Testing ext11 BFP decode uses each bundle's own exponent...\n");
  // 9-bit mantissas (bundles not byte aligned), exponent 0 then 3.
  uint8_t exps[2] = {0, 3};
  uint16_t beam_ids[2] = {1, 2};
  int16_t iq[8] = {255, -256, 1, -1, 100, -100, -7, 7};
  ext11_desc_t d = {.rad = true,
                    .num_bund_prb = 1,
                    .comp_meth = XRAN_BFWCOMPMETHOD_BLKFLOAT,
                    .iq_width_field = 9,
                    .iq_bits = 9,
                    .n_weights = 2,
                    .n_bundles = 2,
                    .comp_params = exps,
                    .beam_ids = beam_ids,
                    .iq = iq};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  assert(len == 24); // 6 + 2 * (1 + 2 + 5) = 22, padded to 24
  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[2];
  c16_t w[4];
  assert(xran_decode_bfw_ext11(buf, len, 2, 2, 2, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 2);
  assert(hdr.bfwCompMeth == XRAN_BFWCOMPMETHOD_BLKFLOAT && hdr.bfwIqWidth == 9);
  assert(ids[0] == 1 && ids[1] == 2);
  assert(w[0].r == 255 && w[0].i == -256 && w[1].r == 1 && w[1].i == -1);
  assert(w[2].r == 800 && w[2].i == -800 && w[3].r == -56 && w[3].i == 56);
  printf("ext11 BFP per-bundle exponent check passed!\n");
}

static void test_ext11_blkscale_ulaw(void)
{
  printf("Testing ext11 BLKSCALE and ULAW decode...\n");
  uint8_t params[1] = {2};
  uint16_t beam_ids[1] = {9};
  int16_t iq[2] = {10, -10};
  ext11_desc_t d = {.rad = true,
                    .num_bund_prb = 4,
                    .comp_meth = XRAN_BFWCOMPMETHOD_BLKSCALE,
                    .iq_width_field = 8,
                    .iq_bits = 8,
                    .n_weights = 1,
                    .n_bundles = 1,
                    .comp_params = params,
                    .beam_ids = beam_ids,
                    .iq = iq};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[1];
  c16_t w[1];
  assert(xran_decode_bfw_ext11(buf, len, 4, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 1);
  assert(ids[0] == 9 && w[0].r == 40 && w[0].i == -40);

  // ULAW: same expansion as test_ulaw_known_vector, (32, -32) -> (3935, -3935)
  iq[0] = 32;
  iq[1] = -32;
  d.comp_meth = XRAN_BFWCOMPMETHOD_ULAW;
  len = build_ext11(buf, &d);
  assert(xran_decode_bfw_ext11(buf, len, 4, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 1);
  assert(hdr.bfwCompMeth == XRAN_BFWCOMPMETHOD_ULAW);
  assert(ids[0] == 9 && w[0].r == 3935 && w[0].i == -3935);
  printf("ext11 BLKSCALE/ULAW check passed!\n");
}

static void test_ext11_disable_bfws(void)
{
  printf("Testing ext11 with disableBFWs carries beamIds only...\n");
  // No bfwCompHdr: 5-byte header + 3 beamIds = 11 bytes, padded to 12.
  uint16_t beam_ids[3] = {7, 8, 0x8000 | 9};
  ext11_desc_t d = {.disable_bfws = true, .num_bund_prb = 3, .n_weights = 4, .n_bundles = 3, .beam_ids = beam_ids};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  assert(len == 12);

  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[3];
  c16_t w[12];
  memset(w, 0x5A, sizeof(w));
  assert(xran_decode_bfw_ext11(buf, len, 9, 4, 3, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 3);
  assert(hdr.disableBFWs && !hdr.RAD && hdr.numBundPrb == 3);
  assert(ids[0] == 7 && ids[1] == 8 && ids[2] == 9);
  for (int i = 0; i < 12; i++) // no weights written
    assert(w[i].r == 0x5A5A && w[i].i == 0x5A5A);
  // 10 PRBs would need 4 bundles: 13 bytes, padded to 16
  assert(xran_decode_bfw_ext11(buf, len, 10, 4, 4, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN);
  printf("ext11 disableBFWs check passed!\n");
}

static void test_ext11_bundle_offset(void)
{
  printf("Testing ext11 bundleOffset shifts the bundle boundaries...\n");
  // 4 PRBs, 4 per bundle: 1 bundle, or 2 with bundleOffset 1 (the first bundle starts 1 PRB before startPrbc).
  uint16_t beam_ids[2] = {1, 2};
  ext11_desc_t d = {.disable_bfws = true, .bundle_offset = 1, .num_bund_prb = 4, .n_bundles = 2, .beam_ids = beam_ids};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[2];
  c16_t w[2];
  assert(xran_decode_bfw_ext11(buf, len, 4, 1, 2, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 2);
  assert(hdr.bundleOffset == 1 && ids[0] == 1 && ids[1] == 2);
  // Without the offset the same 4 PRBs are one bundle: 7 bytes, padded to 8, not 12
  buf[3] &= ~0x3F;
  assert(xran_decode_bfw_ext11(buf, len, 4, 1, 2, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN);
  printf("ext11 bundleOffset check passed!\n");
}

static void test_ext11_count_mismatch_rejected(void)
{
  printf("Testing ext11 decode rejects weight/PRB counts that don't match extLen...\n");
  // 4 PRBs, 1 per bundle, 2 x 16-bit weights: 6 + 4 * 10 = 46, padded to 48.
  uint16_t beam_ids[4] = {1, 2, 3, 4};
  int16_t iq[16] = {0};
  ext11_desc_t d = {.rad = true,
                    .num_bund_prb = 1,
                    .comp_meth = XRAN_BFWCOMPMETHOD_NONE,
                    .iq_bits = 16,
                    .n_weights = 2,
                    .n_bundles = 4,
                    .beam_ids = beam_ids,
                    .iq = iq};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  assert(len == 48);

  xran_bfw_ext11_hdr_t hdr;
  int n_bundles = -1;
  memset(&hdr, 0x5A, sizeof(hdr));
  uint16_t ids[8];
  c16_t w[32];
  memset(w, 0x5A, sizeof(w));
  assert(xran_decode_bfw_ext11(buf, len, 4, 1, 8, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN); // fewer weights than sent
  assert(xran_decode_bfw_ext11(buf, len, 4, 3, 8, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN); // more weights than sent
  assert(xran_decode_bfw_ext11(buf, len, 3, 2, 8, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN); // fewer PRBs than sent
  assert(xran_decode_bfw_ext11(buf, len, 5, 2, 8, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN); // more PRBs than sent
  // more bundles than the caller has room for
  assert(xran_decode_bfw_ext11(buf, len, 4, 2, 3, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_TOO_MANY_BUNDLES);
  // Nothing written on rejection
  for (int i = 0; i < 32; i++)
    assert(w[i].r == 0x5A5A && w[i].i == 0x5A5A);
  assert(hdr.numBundPrb == 0x5A && n_bundles == -1);
  assert(xran_decode_bfw_ext11(buf, len, 4, 2, 4, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 4);
  printf("ext11 count mismatch rejection passed!\n");
}

static void test_ext11_malformed_inputs_rejected(void)
{
  printf("Testing ext11 decode rejects malformed input...\n");
  uint8_t exps[1] = {0};
  uint16_t beam_ids[1] = {1};
  int16_t iq[2] = {1, -1};
  ext11_desc_t d = {.rad = true,
                    .num_bund_prb = 1,
                    .comp_meth = XRAN_BFWCOMPMETHOD_BLKFLOAT,
                    .iq_width_field = 8,
                    .iq_bits = 8,
                    .n_weights = 1,
                    .n_bundles = 1,
                    .comp_params = exps,
                    .beam_ids = beam_ids,
                    .iq = iq};
  uint8_t buf[1024];
  size_t len = build_ext11(buf, &d);
  assert(len == 12);
  xran_bfw_ext11_hdr_t hdr;
  int n_bundles;
  uint16_t ids[1];
  c16_t w[1];
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_OK && n_bundles == 1);

  // Truncated buffer
  assert(xran_decode_bfw_ext11(buf, len - 1, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_TRUNCATED);
  assert(xran_decode_bfw_ext11(buf, 4, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_TRUNCATED);
  // NULL/zero-sized arguments are caller errors (AssertError() logs them)
  assert(xran_decode_bfw_ext11(NULL, len, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, NULL, &n_bundles, ids, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, NULL, ids, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, NULL, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, ids, NULL) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 0, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 0, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_ARG);
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 0, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_ARG);

  // extLen one word too long, and 0; the upper extLen byte must be used
  buf[2]++;
  assert(xran_decode_bfw_ext11(buf, sizeof(buf), 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN);
  buf[2] = 0;
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN);
  buf[1] = 1;
  buf[2] = 3;
  assert(xran_decode_bfw_ext11(buf, sizeof(buf), 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_EXT_LEN);
  buf[1] = 0;

  // numBundPrb 0 is reserved
  buf[4] = 0;
  assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_NUM_BUND_PRB);
  buf[4] = 1;

  // Beamspace and reserved bfwCompMeth values come from the wire and must not abort
  for (int meth = XRAN_BFWCOMPMETHOD_BEAMSPACE; meth <= 0xF; meth++) {
    buf[5] = (uint8_t)(meth | (8 << 4));
    assert(xran_decode_bfw_ext11(buf, len, 1, 1, 1, &hdr, &n_bundles, ids, w) == XRAN_BFW_ERR_COMP_METH);
  }
  printf("ext11 malformed-input rejection passed!\n");
}

static void test_strerror(void)
{
  printf("Testing error descriptions...\n");
  const char *unknown = xran_bfw_strerror(1);
  for (int err = XRAN_BFW_ERR_TOO_MANY_BUNDLES; err <= XRAN_BFW_OK; err++) {
    assert(strcmp(xran_bfw_strerror(err), unknown) != 0);
    for (int other = err + 1; other <= XRAN_BFW_OK; other++)
      assert(strcmp(xran_bfw_strerror(err), xran_bfw_strerror(other)) != 0);
  }
  printf("error descriptions passed!\n");
}

int main(void)
{
  test_bfp_known_vector();
  test_bfp_odd_width_known_vector();
  test_none_known_vector();
  test_blkscale_known_vector();
  test_ulaw_known_vector();
  test_weight_count_mismatch_rejected();
  test_malformed_inputs_rejected();
  test_section_ext_len();
  test_ext11_none_known_vector();
  test_ext11_bfp_per_bundle_exponent();
  test_ext11_blkscale_ulaw();
  test_ext11_disable_bfws();
  test_ext11_bundle_offset();
  test_ext11_count_mismatch_rejected();
  test_ext11_malformed_inputs_rejected();
  test_strerror();
  printf("All xran_pkt_bfw tests passed!\n");
  return 0;
}
