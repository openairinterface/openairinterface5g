/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Configuration of the CUDA LDPC coding library, read from the config module (section nrLDPC_coding_cuda)
 */

#include <stdbool.h>
#include "nrLDPC_coding_cuda_config.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"

#define OPT_WAIT_MODE "wait_mode"

#define HLP_WAIT_MODE "how a host thread waits for the GPU: spin, yield or block"

// names and values of the enums, in the same order
#define WAIT_MODE_NAMES "spin", "yield", "block"
#define WAIT_MODE_VALUES LDPC_CUDA_WAIT_SPIN, LDPC_CUDA_WAIT_YIELD, LDPC_CUDA_WAIT_BLOCK

const char *const ldpc_cuda_wait_mode_names[] = {WAIT_MODE_NAMES};

static const ldpc_cuda_config_t ldpc_cuda_config_default = {
    .wait_mode = LDPC_CUDA_WAIT_YIELD,
};

// value set by config_checkstr_assign_integer() for string option name, def if its check failed (if the config module
// does not abort on it). The value is read in place: the config module frees it in end_configmodule(), which
// config_get_processedint() would make a double free.
static int processed_or_default(paramdef_t *params, int nparams, const char *name, int def)
{
  paramdef_t *p = &params[config_paramidx_fromname(params, nparams, (char *)name)];
  return p->processedvalue ? *p->processedvalue : def;
}

const ldpc_cuda_config_t *ldpc_cuda_get_config(void)
{
  static ldpc_cuda_config_t cfg;
  static bool read = false;
  if (read)
    return &cfg;
  read = true;
  cfg = ldpc_cuda_config_default; // also used if the config module is not available

  // the config module checks the values and converts the strings to the enum values
  const ldpc_cuda_config_t *def = &ldpc_cuda_config_default;
  char *wait_mode = NULL, *def_wait = (char *)ldpc_cuda_wait_mode_names[def->wait_mode];
  // clang-format off
  checkedparam_t wait_check     = {.s3a = {config_checkstr_assign_integer, {WAIT_MODE_NAMES}, {WAIT_MODE_VALUES}, 3}};
  paramdef_t params[] = {
    {OPT_WAIT_MODE,          HLP_WAIT_MODE,          0, .strptr = &wait_mode,            .defstrval = def_wait,                TYPE_STRING, 0, &wait_check},
  };
  // clang-format on
  const int nparams = sizeofArray(params);
  if (config_get(config_get_if(), params, nparams, LDPC_CUDA_CONFIG_SECTION) < 0) {
    LOG_W(NR_PHY, "CUDA LDPC: config module not available, default configuration\n");
    cfg = *def;
    return &cfg;
  }
  cfg.wait_mode = processed_or_default(params, nparams, OPT_WAIT_MODE, def->wait_mode);
  return &cfg;
}
