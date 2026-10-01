/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* XnAP UE ID management — XNAP allocates the local XnAP UE IDs and keeps, per
 * Xn handover, the XnAP UE IDs of both gNBs and the SCTP association to the peer.
 * RRC only refers to a handover by its rrc_ue_id.
 *
 * A UE has at most one entry: this gNB is either the source or the target of its handover.
 * Source gNB: allocates s_ng_node_ue_xnap_id when sending HandoverRequest,
 *   learns t_ng_node_ue_xnap_id from HandoverRequestAcknowledge.
 * Target gNB: allocates t_ng_node_ue_xnap_id when receiving HandoverRequest,
 *   RRC binds it to the rrc_ue_id of the UE it creates for the handover. */

#ifndef XNAP_IDS_H_
#define XNAP_IDS_H_

#include <stdbool.h>
#include <stdint.h>
#include "common/platform_types.h"
#include "openair2/COMMON/sctp_messages_types.h"

typedef enum { XN_ROLE_SOURCE, XN_ROLE_TARGET } xn_role_t;

typedef struct {
  // role of this gNB in the handover
  xn_role_t role;
  // RRC UE ID at this gNB (0 at target until bound)
  uint32_t rrc_ue_id;
  // Source NG-RAN node UE XnAP ID
  uint32_t s_ng_node_ue_xnap_id;
  // Target NG-RAN node UE XnAP ID (0 at source until HandoverRequestAcknowledge)
  uint32_t t_ng_node_ue_xnap_id;
  // SCTP association to the peer gNB
  sctp_assoc_t assoc_id;
} xn_ue_data_t;

/* Call once when the XNAP task starts */
void xn_init_ue_data(void);

/* Entries are keyed on the local XnAP UE ID (s_ng_node_ue_xnap_id at source,
 * t_ng_node_ue_xnap_id at target) and indexed by rrc_ue_id once known. */

/* Allocate the local XnAP UE ID of a new handover: rrc_ue_id at source, 0 at target until bound;
 * peer_xnap_id 0 at source, the source XnAP UE ID at target. Returns 0 at target if the
 * handover peer_xnap_id from assoc_id already has an entry (repeated HandoverRequest) */
uint32_t xn_add_ue(xn_role_t role, uint32_t rrc_ue_id, uint32_t peer_xnap_id, sctp_assoc_t assoc_id);
/* Store the XnAP UE ID of the peer (source: on HandoverRequestAcknowledge) */
bool xn_set_ue_peer_id(uint32_t xnap_id, uint32_t peer_xnap_id);
/* Bind the entry to the RRC UE created for the handover (target) */
bool xn_bind_ue(uint32_t xnap_id, uint32_t rrc_ue_id);

/* Look up the entry in the given role, by rrc_ue_id or by local XnAP UE ID */
bool xn_get_ue(xn_role_t role, uint32_t rrc_ue_id, xn_ue_data_t *out);
bool xn_get_ue_by_xnap_id(xn_role_t role, uint32_t xnap_id, xn_ue_data_t *out);

/* Remove the entry of the local XnAP UE ID; returns false if there is none */
bool xn_remove_ue_by_xnap_id(uint32_t xnap_id);
/* Remove up to max entries towards assoc_id (e.g. the Xn peer is lost),
 * copying them to out; returns the number of entries removed */
int xn_remove_ues_by_assoc(sctp_assoc_t assoc_id, xn_ue_data_t *out, int max);
/* Remove the entry of rrc_ue_id, e.g. the UE context is removed:
 * a late XnAP message must not reach a new UE reusing the rrc_ue_id.
 * Returns true if an entry was removed */
bool xn_remove_ue(uint32_t rrc_ue_id);

#endif /* XNAP_IDS_H_ */
