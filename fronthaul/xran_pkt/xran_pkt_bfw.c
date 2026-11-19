/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "xran_pkt_bfw.h"
#include "xran_pkt_cp.h"
#include "fh_compression.h"
#include "common/utils/assertions.h"
#include <string.h>

xran_ext_err_t xran_decode_bfw_ext1(const uint8_t *ext, size_t len, int n_weights, c16_t *weights_out)
{
  AssertError(ext != NULL && weights_out != NULL && n_weights > 0,
              return XRAN_EXT_ERR_ARG,
              "ext %p weights_out %p n_weights %d\n",
              ext,
              weights_out,
              n_weights);
  if (len < sizeof(struct xran_cp_radioapp_section_ext1))
    return XRAN_EXT_ERR_TRUNCATED;

  const struct xran_cp_radioapp_section_ext1 *hdr = (const struct xran_cp_radioapp_section_ext1 *)ext;
  if (hdr->bfwCompMeth > XRAN_BFWCOMPMETHOD_ULAW) // beamspace and reserved methods not supported
    return XRAN_EXT_ERR_COMP_METH;

  // bfwIqWidth = 0 means 16 bits, otherwise 1..15 (CUS v16.01 Table 7.7.1.2-2)
  int iq_bits = hdr->bfwIqWidth == 0 ? 16 : hdr->bfwIqWidth;
  fh_comp_method_t method = (fh_comp_method_t)hdr->bfwCompMeth;
  size_t offset = sizeof(*hdr) + (method != FH_COMP_NONE ? 1 : 0); // bfwCompParam present if compressed

  // extLen (4-byte words) must match the header, the n_weights (bfwI, bfwQ) pairs and the zero padding exactly
  size_t expected_len = offset + ((size_t)2 * n_weights * iq_bits + 7) / 8;
  expected_len = (expected_len + 3) & ~(size_t)3;
  size_t ext_len = (size_t)hdr->extLen * 4;
  if (ext_len != expected_len)
    return XRAN_EXT_ERR_EXT_LEN;
  if (ext_len > len)
    return XRAN_EXT_ERR_TRUNCATED;

  // c16_t is {int16_t r, i}, i.e. the same layout as the (bfwI, bfwQ) value stream
  int16_t *out = (int16_t *)weights_out;
  if (method != FH_COMP_NONE) {
    // fh_decompress_block() expects the comp param byte first
    fh_decompress_block(method, iq_bits, 2 * n_weights, (const int8_t *)(ext + offset - 1), out);
  } else {
    for (int i = 0; i < 2 * n_weights; i++)
      out[i] = (int16_t)unpack_bits(ext + offset, i * iq_bits, iq_bits);
  }
  return XRAN_EXT_OK;
}

xran_ext_err_t xran_decode_bfw_ext11(const uint8_t *ext,
                                     size_t len,
                                     int num_prb,
                                     int n_weights,
                                     int max_bundles,
                                     xran_bfw_ext11_hdr_t *hdr_out,
                                     int *n_bundles_out,
                                     uint16_t *beam_ids_out,
                                     c16_t *weights_out)
{
  AssertError(ext != NULL && hdr_out != NULL && n_bundles_out != NULL && beam_ids_out != NULL && weights_out != NULL,
              return XRAN_EXT_ERR_ARG,
              "ext %p hdr_out %p n_bundles_out %p beam_ids_out %p weights_out %p\n",
              ext,
              hdr_out,
              n_bundles_out,
              beam_ids_out,
              weights_out);
  AssertError(num_prb > 0 && n_weights > 0 && max_bundles > 0,
              return XRAN_EXT_ERR_ARG,
              "num_prb %d n_weights %d max_bundles %d\n",
              num_prb,
              n_weights,
              max_bundles);
  // bfwCompHdr is absent when disableBFWs = 1, so the fixed part is 5 or 6 bytes
  const size_t hdr_len = sizeof(struct xran_cp_radioapp_section_ext11);
  if (len < hdr_len - 1)
    return XRAN_EXT_ERR_TRUNCATED;

  xran_bfw_ext11_hdr_t hdr = {.disableBFWs = ext[3] >> 7,
                              .RAD = (ext[3] >> 6) & 1,
                              .bundleOffset = ext[3] & 0x3F,
                              .numBundPrb = ext[4]};
  if (hdr.numBundPrb == 0) // reserved
    return XRAN_EXT_ERR_NUM_BUND_PRB;
  int n_bundles = (hdr.bundleOffset + num_prb + hdr.numBundPrb - 1) / hdr.numBundPrb;
  if (n_bundles > max_bundles)
    return XRAN_EXT_ERR_TOO_MANY;

  // Per bundle: [bfwCompParam] beamId [(bfwI, bfwQ) x n_weights], or only beamId if disableBFWs
  fh_comp_method_t method = FH_COMP_NONE;
  int iq_bits = 0;
  size_t offset = hdr_len - 1;
  size_t param_len = 0, iq_len = 0;
  if (!hdr.disableBFWs) {
    if (len < hdr_len)
      return XRAN_EXT_ERR_TRUNCATED;
    hdr.bfwCompMeth = ext[5] & 0x0F;
    if (hdr.bfwCompMeth > XRAN_BFWCOMPMETHOD_ULAW) // beamspace and reserved methods not supported
      return XRAN_EXT_ERR_COMP_METH;
    hdr.bfwIqWidth = ext[5] >> 4 == 0 ? 16 : ext[5] >> 4; // 0 means 16 bits (CUS v16.01 Table 7.7.1.2-2)
    method = (fh_comp_method_t)hdr.bfwCompMeth;
    iq_bits = hdr.bfwIqWidth;
    offset = hdr_len;
    param_len = method != FH_COMP_NONE ? 1 : 0;
    iq_len = ((size_t)2 * n_weights * iq_bits + 7) / 8;
  }
  const size_t bundle_len = param_len + 2 + iq_len;

  // extLen (4-byte words) must match the header, the bundles and the zero padding exactly
  size_t expected_len = (offset + n_bundles * bundle_len + 3) & ~(size_t)3;
  size_t ext_len = (((size_t)ext[1] << 8) | ext[2]) * 4;
  if (ext_len != expected_len)
    return XRAN_EXT_ERR_EXT_LEN;
  if (ext_len > len)
    return XRAN_EXT_ERR_TRUNCATED;

  int16_t *out = (int16_t *)weights_out;
  for (int b = 0; b < n_bundles; b++, offset += bundle_len) {
    const uint8_t *bundle = ext + offset;
    beam_ids_out[b] = ((bundle[param_len] << 8) | bundle[param_len + 1]) & 0x7FFF; // top bit is contInd
    if (hdr.disableBFWs)
      continue;
    const uint8_t *iq = bundle + param_len + 2;
    int16_t *w = out + (size_t)2 * b * n_weights;
    if (method != FH_COMP_NONE) {
      // fh_decompress_block() expects the comp param byte right before the IQ, but beamId sits in between
      uint8_t block[1 + iq_len];
      block[0] = bundle[0];
      memcpy(block + 1, iq, iq_len);
      fh_decompress_block(method, iq_bits, 2 * n_weights, (const int8_t *)block, w);
    } else {
      for (int i = 0; i < 2 * n_weights; i++)
        w[i] = (int16_t)unpack_bits(iq, i * iq_bits, iq_bits);
    }
  }
  *hdr_out = hdr;
  *n_bundles_out = n_bundles;
  return XRAN_EXT_OK;
}
