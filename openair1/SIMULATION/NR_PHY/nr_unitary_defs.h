/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_UNITARY_DEFS__H__
#define __NR_UNITARY_DEFS__H__

#include "NR_ServingCellConfigCommon.h"
#include "NR_ServingCellConfig.h"

// Define signal handler to attempt graceful termination
extern bool stop;

void sigint_handler(int arg);

const struct sigaction sigint_action = {.sa_handler = sigint_handler,
                                        // restore handler to default upon entry to the signal handler
                                        .sa_flags = SA_RESETHAND};

int oai_exit = 0;

signed char quantize(double D, double x, unsigned char B)
{
  double qxd;
  short maxlev;
  qxd = floor(x / D);
  maxlev = 1 << (B - 1); //(char)(pow(2,B-1));

  if (qxd <= -maxlev)
    qxd = -maxlev;
  else if (qxd >= maxlev)
    qxd = maxlev - 1;

  return (signed char)qxd; // plain char is unsigned on aarch64
}

void stop_nr_nfapi_vnf()
{
  abort();
}

void stop_nr_nfapi_pnf()
{
  abort();
}

void fill_scc_sim(NR_ServingCellConfigCommon_t *scc, uint64_t *ssb_bitmap, int N_RB_DL, int N_RB_UL, int mu_dl, int mu_ul);
void fix_scc(NR_ServingCellConfigCommon_t *scc, uint64_t ssbmap);
void prepare_scc(NR_ServingCellConfigCommon_t *scc);
void prepare_msgA_scc(NR_ServingCellConfigCommon_t *scc);
void prepare_scd(NR_ServingCellConfig_t *scd);
uint32_t ngap_generate_gNB_id(void)
{
  return 0;
}

#endif
