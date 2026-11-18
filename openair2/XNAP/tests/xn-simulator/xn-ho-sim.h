/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef XN_HO_SIM_H
#define XN_HO_SIM_H

#include "common/utils/ocp_itti/intertask_interface.h"

/* Xn handover test with N gNB instances and one UE stub (real NR UE NAS, no RRC/radio) */

#define XN_HO_MAX_GNB 8

/* gNB instance idx (0..N-1) with its NG Setup Request, NR Cell ID and N3 (NG-U) address */
void xn_ho_sim_add_gnb(int idx, MessageDef *ng_register_req, uint64_t cell_id, const char *ngu_ip);
/* register gNB1 to the AMF; the other gNBs are registered once the UE stub is attached to gNB1 */
void xn_ho_sim_start(void);
/* NG Setup of a gNB instance completed */
void xn_ho_sim_ng_setup_done(instance_t inst);
void *xn_ho_rrc_gnb_task(void *arg);
void *xn_ho_rrc_ue_task(void *arg);

#endif
