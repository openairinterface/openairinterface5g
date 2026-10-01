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

int rrc_gNB_process_XNAP_HANDOVER_REQUEST(gNB_RRC_INST *rrc, xnap_handover_req_t *req);

void rrc_gNB_send_XNAP_HANDOVER_REQ_ACK(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, byte_array_t ho_command);

#endif /* RRC_GNB_XNAP_H_ */
