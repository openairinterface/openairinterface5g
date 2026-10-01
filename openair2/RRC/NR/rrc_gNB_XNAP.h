/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef RRC_GNB_XNAP_H_
#define RRC_GNB_XNAP_H_

#include "nr_rrc_defs.h"

bool rrc_gNB_send_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc,
                                        gNB_RRC_UE_t *UE,
                                        const nr_neighbour_cell_t *neighbour,
                                        const byte_array_t hoPrepInfo);
#endif /* RRC_GNB_XNAP_H_ */
