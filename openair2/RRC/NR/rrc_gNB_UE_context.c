/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief rrc procedures for UE context
 */

#include "rrc_gNB_UE_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "RRC/NR/nr_rrc_defs.h"
#include "T.h"
#include "assertions.h"
#include "common/platform_constants.h"
#include "common/utils/LOG/log.h"
#include "linear_alloc.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "tree.h"
#include "rrc_gNB_radio_bearers.h"
#include "rrc_cell_management.h"

static void rrc_gNB_ue_context_update_time(rrc_gNB_ue_context_t *ctxt)
{
  ctxt->ue_context.last_seen = time(NULL);
}

//------------------------------------------------------------------------------
int rrc_gNB_compare_ue_rnti_id(rrc_gNB_ue_context_t *c1_pP, rrc_gNB_ue_context_t *c2_pP)
//------------------------------------------------------------------------------
{
  if (c1_pP->ue_context.rrc_ue_id > c2_pP->ue_context.rrc_ue_id) {
    return 1;
  }

  if (c1_pP->ue_context.rrc_ue_id < c2_pP->ue_context.rrc_ue_id) {
    return -1;
  }

  return 0;
}

/* Generate the tree management functions */
RB_GENERATE(rrc_nr_ue_tree_s, rrc_gNB_ue_context_s, entries,
            rrc_gNB_compare_ue_rnti_id);

//------------------------------------------------------------------------------
static rrc_gNB_ue_context_t *rrc_gNB_allocate_new_ue_context(gNB_RRC_INST *rrc_instance_pP)
//------------------------------------------------------------------------------
{
  rrc_gNB_ue_context_t *new_p = calloc(1, sizeof(*new_p));
  if (new_p == NULL) {
    LOG_E(NR_RRC, "Cannot allocate new ue context, no free memory\n");
    return NULL;
  }

  uid_t uid = uid_linear_allocator_new(&rrc_instance_pP->uid_allocator);
  if (uid == UINT_MAX) {
    LOG_E(NR_RRC, "Cannot allocate new UE context, no free UID\n");
    rrc_gNB_free_mem_ue_context(new_p);
    return NULL;
  }

  new_p->ue_context.rrc_ue_id = uid + 1;
  rrc_gNB_ue_context_update_time(new_p);
  LOG_D(NR_RRC, "Returning new RRC UE context RRC ue id: %d\n", new_p->ue_context.rrc_ue_id);
  return(new_p);
}


//------------------------------------------------------------------------------
rrc_gNB_ue_context_t *rrc_gNB_get_ue_context(gNB_RRC_INST *rrc_instance_pP, ue_id_t ue)
//------------------------------------------------------------------------------
{
  rrc_gNB_ue_context_t temp;
  /* gNB ue rrc id = 24 bits wide */
  temp.ue_context.rrc_ue_id = ue;
  return RB_FIND(rrc_nr_ue_tree_s, &rrc_instance_pP->rrc_ue_head, &temp);
}

rrc_gNB_ue_context_t *rrc_gNB_get_ue_context_by_rnti(gNB_RRC_INST *rrc_instance_pP, sctp_assoc_t assoc_id, rnti_t rntiP)
{
  rrc_gNB_ue_context_t *ue_context_p;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &(rrc_instance_pP->rrc_ue_head)) {
    f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_context_p->ue_context.rrc_ue_id);
    if (ue_data.du_assoc_id == assoc_id && ue_context_p->ue_context.rnti == rntiP) {
      rrc_gNB_ue_context_update_time(ue_context_p);
      return ue_context_p;
    }
  }
  LOG_W(NR_RRC, "search by RNTI %04x and assoc_id %d: no UE found\n", rntiP, assoc_id);
  return NULL;
}

/** @brief Fetch UE context by RNTI on any DU; if pci >= 0, the UE's PCell PCI must also match */
rrc_gNB_ue_context_t *rrc_gNB_get_ue_context_by_rnti_any_du(gNB_RRC_INST *rrc_instance_pP, rnti_t rntiP, int pci)
{
  rrc_gNB_ue_context_t *ue_context_p;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &(rrc_instance_pP->rrc_ue_head))
  {
    if (ue_context_p->ue_context.rnti != rntiP)
      continue;
    const nr_rrc_cell_container_t *pcell = pci >= 0 ? rrc_get_pcell_for_ue(rrc_instance_pP, &ue_context_p->ue_context) : NULL;
    if (pci < 0 || (pcell != NULL && pcell->info.pci == pci)) {
      rrc_gNB_ue_context_update_time(ue_context_p);
      return ue_context_p;
    }
  }
  LOG_W(NR_RRC, "search by rnti not found %04x\n", rntiP);
  return NULL;
}

/** @brief Fetch UE Context by the unique AMF UE NGAP ID */
rrc_gNB_ue_context_t *rrc_gNB_get_ue_context_by_amf_ue_ngap_id(gNB_RRC_INST *rrc_instance_pP, uint64_t amf_ue_ngap_id)
{
  rrc_gNB_ue_context_t *ue_context_p;
  RB_FOREACH (ue_context_p, rrc_nr_ue_tree_s, &(rrc_instance_pP->rrc_ue_head)) {
    if (ue_context_p->ue_context.amf_ue_ngap_id == amf_ue_ngap_id) {
      return ue_context_p;
    }
  }
  return NULL;
}

void rrc_gNB_free_mem_ue_context(rrc_gNB_ue_context_t *const ue_context_pP)
//-----------------------------------------------------------------------------
{
  LOG_T(NR_RRC, " Clearing UE context 0x%p (free internal structs)\n", ue_context_pP);
  free(ue_context_pP);
}

//------------------------------------------------------------------------------
void rrc_gNB_remove_ue_context(gNB_RRC_INST *rrc_instance_pP, rrc_gNB_ue_context_t *ue_context_pP)
//------------------------------------------------------------------------------
{
  if (rrc_instance_pP == NULL) {
    LOG_E(NR_RRC, " Bad RRC instance\n");
    return;
  }

  if (ue_context_pP == NULL) {
    LOG_E(NR_RRC, "Trying to free a NULL UE context\n");
    return;
  }

  gNB_RRC_UE_t *ue = &ue_context_pP->ue_context;
  LOG_I(NR_RRC, "[--] (UE ID %d RNTI %04x) Remove UE context\n", ue->rrc_ue_id, ue->rnti);
  RB_REMOVE(rrc_nr_ue_tree_s, &rrc_instance_pP->rrc_ue_head, ue_context_pP);
  uid_linear_allocator_free(&rrc_instance_pP->uid_allocator, ue->rrc_ue_id - 1);
  cu_remove_f1_ue_data(ue->rrc_ue_id);
  rrc_gNB_free_mem_ue_context(ue_context_pP);
}

//-----------------------------------------------------------------------------
// return the ue context if there is already an UE with ue_identityP, NULL otherwise
rrc_gNB_ue_context_t *rrc_gNB_ue_context_random_exist(gNB_RRC_INST *rrc_instance_pP, const uint64_t ue_identityP)
//-----------------------------------------------------------------------------
{
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &rrc_instance_pP->rrc_ue_head) {
    if (ue_context_p->ue_context.random_ue_identity == ue_identityP) {
      rrc_gNB_ue_context_update_time(ue_context_p);
      return ue_context_p;
    }
  }
  return NULL;
}

//-----------------------------------------------------------------------------
// return the ue context if there is already an UE with the same S-TMSI, NULL otherwise
rrc_gNB_ue_context_t *rrc_gNB_ue_context_5g_s_tmsi_exist(gNB_RRC_INST *rrc_instance_pP, const uint64_t s_TMSI)
//-----------------------------------------------------------------------------
{
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &rrc_instance_pP->rrc_ue_head)
  {
    LOG_I(NR_RRC, "Checking for UE 5G S-TMSI %ld: RNTI %04x\n", s_TMSI, ue_context_p->ue_context.rnti);
    if (ue_context_p->ue_context.ng_5G_S_TMSI_Part1 == s_TMSI) {
      rrc_gNB_ue_context_update_time(ue_context_p);
      return ue_context_p;
    }
  }
    return NULL;
}

//-----------------------------------------------------------------------------
// return a new ue context structure if ue_identityP, rnti not found in collection
rrc_gNB_ue_context_t *rrc_gNB_create_ue_context(sctp_assoc_t assoc_id,
                                                rnti_t rnti,
                                                gNB_RRC_INST *rrc_instance_pP,
                                                const uint64_t ue_identityP,
                                                uint32_t du_ue_id)
//-----------------------------------------------------------------------------
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_allocate_new_ue_context(rrc_instance_pP);
  if (ue_context_p == NULL) {
    LOG_E(NR_RRC, "Cannot create UE context\n");
    return NULL;
  }

  gNB_RRC_UE_t *ue = &ue_context_p->ue_context;
  ue->rnti = rnti;
  ue->random_ue_identity = ue_identityP;
  // signal "no AMF UE NGAP ID" because valid range is 0..2^40-1
  ue->amf_ue_ngap_id = INT64_MAX;
  f1_ue_data_t ue_data = {.secondary_ue = du_ue_id, .du_assoc_id = assoc_id};
  AssertFatal(!cu_exists_f1_ue_data(ue->rrc_ue_id),
              "UE F1 Context for ID %d already exists, logic bug\n",
              ue->rrc_ue_id);
  bool success = cu_add_f1_ue_data(ue->rrc_ue_id, &ue_data);
  DevAssert(success);
  ue->ambr.dl_br = UINT64_MAX;
  ue->ambr.ul_br = UINT64_MAX;

  // Initialise setup PDU Sessions list
  seq_arr_init(&ue->pduSessions, sizeof(rrc_pdu_session_param_t));
  // Initialise setup DRBs list
  seq_arr_init(&ue->drbs, sizeof(drb_t));
  // Initialise serving cells list
  seq_arr_init(&ue->serving_cells, sizeof(ue_serving_cell_t));

  RB_INSERT(rrc_nr_ue_tree_s, &rrc_instance_pP->rrc_ue_head, ue_context_p);
  return ue_context_p;
}
