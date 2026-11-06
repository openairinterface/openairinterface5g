/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
* \brief Top-level routines for generating the PUCCH physical channel
*/
#ifndef __PUCCH_NR__H__
#define __PUCCH_NR__H__

//#include "PHY/defs.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
//#include "PHY/extern.h"

#include "common/utils/LOG/log.h"
#include "T.h"
#define ONE_OVER_SQRT2 23170 // 32767/sqrt(2) = 23170 (ONE_OVER_SQRT2)

void nr_generate_pucch0(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu);

void nr_generate_pucch1(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu);

void nr_generate_pucch2(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu);

void nr_generate_pucch3_4(c16_t **txdataF,
                          const NR_DL_FRAME_PARMS *frame_parms,
                          const int16_t amp,
                          const int nr_slot_tx,
                          const fapi_nr_ul_config_pucch_pdu *pucch_pdu);

void nr_uci_encoding(uint64_t payload, uint8_t nr_bit, uint8_t aggregation_level, uint16_t E, uint8_t Qm, uint64_t *b, bool onPUSCH);

static const uint8_t list_of_prime_numbers[46] = {2,  3,  5,  7,  11, 13, 17, 19, 23, 29,
                                         31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
                                         73, 79, 83, 89, 97, 101,103,107,109,113,
                                         127,131,137,139,149,151,157,163,167,173,
                                         179,181,191,193,197,199};
#endif
