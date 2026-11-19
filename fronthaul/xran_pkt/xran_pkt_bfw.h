/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/platform_types.h"
#include "xran_pkt_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

// Section Extension 1: Beamforming weights (CUS v16.01 7.7.1).
// Decodes exactly n_weights (bfwI, bfwQ) pairs into weights_out. Returns XRAN_EXT_OK, or an error if the extension is
// malformed, uses an unsupported bfwCompMeth, or its extLen doesn't match n_weights.
xran_ext_err_t xran_decode_bfw_ext1(const uint8_t *ext, size_t len, int n_weights, c16_t *weights_out);

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
// Both arrays must have room for max_bundles bundles. Returns XRAN_EXT_OK, or an error if the extension is
// malformed, uses an unsupported bfwCompMeth, has more than max_bundles bundles, or its extLen doesn't match.
// Nothing is written on error.
xran_ext_err_t xran_decode_bfw_ext11(const uint8_t *ext,
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
