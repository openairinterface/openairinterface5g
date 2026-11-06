/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_PHY_COMMON_SRS__H__
#define __NR_PHY_COMMON_SRS__H__

#include "common/platform_types.h"

#define NR_SRS_GEN_MAX_SUBCARRIER 4096

// forward declarations
typedef struct NR_DL_FRAME_PARMS_s NR_DL_FRAME_PARMS;
typedef struct nr_srs_info_s nr_srs_info_t;

bool generate_srs_nr(const NR_DL_FRAME_PARMS *frame_parms,
                     c16_t **txdataF,
                     uint16_t symbol_offset,
                     int bwp_start,
                     nr_srs_info_t *nr_srs_info,
                     int16_t amp,
                     frame_t frame_number,
                     slot_t slot_number,
                     uint8_t nb_antennas);

#endif /* __NR_PHY_COMMON_SRS__H__ */
