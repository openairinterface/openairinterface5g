/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "xran_pkt_ext.h"
#include "xran_pkt_cp.h"
#include "common/utils/assertions.h"
#include <stdbool.h>

const char *xran_ext_strerror(xran_ext_err_t err)
{
  switch (err) {
    case XRAN_EXT_OK:
      return "no error";
    case XRAN_EXT_ERR_ARG:
      return "invalid argument";
    case XRAN_EXT_ERR_TRUNCATED:
      return "runs past the end of the buffer";
    case XRAN_EXT_ERR_EXT_LEN:
      return "extLen doesn't match the content";
    case XRAN_EXT_ERR_COMP_METH:
      return "unsupported bfwCompMeth";
    case XRAN_EXT_ERR_NUM_BUND_PRB:
      return "reserved numBundPrb 0";
    case XRAN_EXT_ERR_TOO_MANY:
      return "more elements than the caller has room for";
    default:
      return "unknown error";
  }
}

xran_ext_err_t xran_section_ext_len(uint8_t ext_type, const uint8_t *ext, size_t avail, size_t *len_out)
{
  AssertError(ext != NULL && len_out != NULL, return XRAN_EXT_ERR_ARG, "ext %p len_out %p\n", ext, len_out);
  // extLen follows the ef/extType byte
  bool wide_ext_len =
      ext_type == XRAN_CP_SECTIONEXTCMD_11 || ext_type == XRAN_CP_SECTIONEXTCMD_19 || ext_type == XRAN_CP_SECTIONEXTCMD_20;
  size_t ext_len_bytes = wide_ext_len ? 2 : 1;
  if (avail < 1 + ext_len_bytes)
    return XRAN_EXT_ERR_TRUNCATED;
  size_t words = ext_len_bytes == 2 ? ((size_t)ext[1] << 8) | ext[2] : ext[1];
  if (words == 0)
    return XRAN_EXT_ERR_EXT_LEN;
  if (words * 4 > avail)
    return XRAN_EXT_ERR_TRUNCATED;
  *len_out = words * 4;
  return XRAN_EXT_OK;
}
