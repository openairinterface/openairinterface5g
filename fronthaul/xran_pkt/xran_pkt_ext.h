/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Result of the section extension decoders (xran_pkt_bfw.h). XRAN_EXT_ERR_ARG and XRAN_EXT_ERR_TOO_MANY come from the
// caller; the other errors come from the packet.
typedef enum {
  XRAN_EXT_OK = 0,
  XRAN_EXT_ERR_ARG = -1, // NULL pointer or bad count from the caller
  XRAN_EXT_ERR_TRUNCATED = -2, // the header or the extension runs past the end of the packet
  XRAN_EXT_ERR_EXT_LEN = -3, // extLen is 0 (reserved) or doesn't match the content and the padding
  XRAN_EXT_ERR_COMP_METH = -4, // SE 1 and SE 11 bfwCompMeth is beamspace or reserved
  XRAN_EXT_ERR_NUM_BUND_PRB = -5, // SE 11 numBundPrb is 0 (reserved)
  XRAN_EXT_ERR_TOO_MANY = -6, // more elements than the caller has room for; keep it the lowest value
} xran_ext_err_t;

// Description of an xran_ext_err_t value
const char *xran_ext_strerror(xran_ext_err_t err);

// Writes the length in bytes of the section extension of type ext_type at ext, with avail bytes left in the packet,
// to len_out. extLen is 16 bits for extType 11, 19 and 20 and 8 bits otherwise (CUS v16.01 7.6.2.3). Returns
// XRAN_EXT_OK, XRAN_EXT_ERR_TRUNCATED if the extension header doesn't fit or the extension runs past avail, or
// XRAN_EXT_ERR_EXT_LEN if extLen is 0.
xran_ext_err_t xran_section_ext_len(uint8_t ext_type, const uint8_t *ext, size_t avail, size_t *len_out);

#ifdef __cplusplus
}
#endif
