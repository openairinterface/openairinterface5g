/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
 
/*!
 * \brief ngap UE context management within gNB
 */

#include <stdint.h>
#include "ngap_gNB_defs.h"
#include "tree.h"

#ifndef NGAP_GNB_UE_CONTEXT_H_
#define NGAP_GNB_UE_CONTEXT_H_

// TS 38.413 clause 9.3.3.1 permits every 40-bit value, including zero.
#define NGAP_AMF_UE_NGAP_ID_UNSET UINT64_MAX

// Forward declarations
struct ngap_gNB_amf_data_s;
struct gNB_amf_desc_s;

typedef enum {
  /* UE has not been registered to a AMF or UE association has failed. */
  NGAP_UE_DECONNECTED = 0x0,
  /* UE ngap state is waiting for initial context setup request message. */
  NGAP_UE_WAITING_CSR = 0x1,
  /* UE association is ready and bearers are established. */
  NGAP_UE_CONNECTED   = 0x2,
  NGAP_UE_STATE_MAX,
} ngap_ue_state;

typedef struct ngap_gNB_ue_context_s {
  /* Tree related data */
  RB_ENTRY(ngap_gNB_ue_context_s) entries;

  /* Uniquely identifies the UE between AMF and gNB within the gNB.
   * This id is encoded on 32bits.
   */
  uint32_t gNB_ue_ngap_id;

  uint64_t amf_ue_ngap_id;

  /* Stream used for this particular UE */
  int32_t tx_stream;
  int32_t rx_stream;

  /* Current UE state. */
  ngap_ue_state ue_state;

  /* Reference to AMF data this UE is attached to */
  struct ngap_gNB_amf_data_s *amf_ref;

  /* Reference to gNB data this UE is attached to */
  ngap_gNB_instance_t *gNB_instance;
} ngap_gNB_ue_context_t;

void ngap_store_ue_context(const ngap_gNB_ue_context_t *ue_desc_p);
void ngap_release_ues_for_amf(ngap_gNB_amf_data_t *amf_desc_p);

ngap_gNB_ue_context_t *ngap_get_ue_context(uint32_t gNB_ue_ngap_id);
ngap_gNB_ue_context_t *ngap_get_ue_context_from_amf_ue_ngap_id(uint32_t amf_ue_ngap_id);
ngap_gNB_ue_context_t *ngap_detach_ue_context(uint32_t gNB_ue_ngap_id);

#endif /* NGAP_GNB_UE_CONTEXT_H_ */
