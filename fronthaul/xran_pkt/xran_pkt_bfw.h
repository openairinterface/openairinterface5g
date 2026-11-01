/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Result of the section extension decoders below. All errors but XRAN_BFW_ERR_ARG come from the packet.
typedef enum {
  XRAN_BFW_OK = 0,
  XRAN_BFW_ERR_ARG = -1, // NULL pointer or non-positive count from the caller
  XRAN_BFW_ERR_TRUNCATED = -2, // the header or the extension runs past the end of the packet
  XRAN_BFW_ERR_EXT_LEN = -3, // extLen is 0 (reserved) or doesn't match the header, weights and padding
  XRAN_BFW_ERR_COMP_METH = -4, // bfwCompMeth is beamspace or reserved
  XRAN_BFW_ERR_NUM_BUND_PRB = -5, // numBundPrb is 0 (reserved)
  XRAN_BFW_ERR_TOO_MANY_BUNDLES = -6, // more PRB bundles than the caller has room for
} xran_bfw_err_t;

// Description of an xran_bfw_err_t value
const char *xran_bfw_strerror(xran_bfw_err_t err);

// Writes the length in bytes of the section extension of type ext_type at ext, with avail bytes left in the packet,
// to len_out. extLen is 16 bits for extType 11, 19 and 20 and 8 bits otherwise (CUS v16.01 7.6.2.3). Returns
// XRAN_BFW_OK, XRAN_BFW_ERR_TRUNCATED if the extension header doesn't fit or the extension runs past avail, or
// XRAN_BFW_ERR_EXT_LEN if extLen is 0.
xran_bfw_err_t xran_section_ext_len(uint8_t ext_type, const uint8_t *ext, size_t avail, size_t *len_out);

// Section Extension 1: Beamforming weights (CUS v16.01 7.7.1).
// Decodes exactly n_weights (bfwI, bfwQ) pairs into weights_out. Returns XRAN_BFW_OK, or an error if the extension is
// malformed, uses an unsupported bfwCompMeth, or its extLen doesn't match n_weights.
xran_bfw_err_t xran_decode_bfw_ext1(const uint8_t *ext, size_t len, int n_weights, c16_t *weights_out);

// Section Extension 11 header fields (CUS v16.01 7.7.11). bfwCompMeth/bfwIqWidth are only set if !disableBFWs.
typedef struct {
  bool disableBFWs; // only beamIds are carried, no weights
  bool RAD;
  uint8_t bundleOffset;
  uint8_t numBundPrb;
  uint8_t bfwCompMeth;
  uint8_t bfwIqWidth; // 1..16
} xran_bfw_ext11_hdr_t;

// Section Extension 11: Flexible beamforming weights (CUS v16.01 7.7.11), for a section of num_prb PRBs.
// The bundle count, ceil((bundleOffset + num_prb) / numBundPrb), goes to n_bundles_out; bundle b gets its beamId in
// beam_ids_out[b] and, unless disableBFWs, exactly n_weights (bfwI, bfwQ) pairs in weights_out[b * n_weights ...].
// Both arrays must have room for max_bundles bundles. Returns XRAN_BFW_OK, or an error if the extension is
// malformed, uses an unsupported bfwCompMeth, has more than max_bundles bundles, or its extLen doesn't match.
// Nothing is written on error.
xran_bfw_err_t xran_decode_bfw_ext11(const uint8_t *ext,
                                     size_t len,
                                     int num_prb,
                                     int n_weights,
                                     int max_bundles,
                                     xran_bfw_ext11_hdr_t *hdr_out,
                                     int *n_bundles_out,
                                     uint16_t *beam_ids_out,
                                     c16_t *weights_out);

#ifdef __cplusplus
}
#endif
