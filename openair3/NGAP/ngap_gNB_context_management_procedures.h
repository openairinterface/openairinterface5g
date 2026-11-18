/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NGAP context management procedures
 * @ingroup _ngap
 */

#include "assertions.h"
#include "ngap_messages_types.h"

#ifndef NGAP_GNB_CONTEXT_MANAGEMENT_PROCEDURES_H_
#define NGAP_GNB_CONTEXT_MANAGEMENT_PROCEDURES_H_


int ngap_ue_context_release_complete(instance_t instance,
                                     ngap_ue_release_complete_t *ue_release_complete_p);

void ngap_ue_context_local_release(const ngap_ue_local_release_t *rel);

int ngap_ue_context_release_req(instance_t instance,
                                ngap_ue_release_req_t *ue_release_req_p);

#endif /* NGAP_GNB_CONTEXT_MANAGEMENT_PROCEDURES_H_ */
