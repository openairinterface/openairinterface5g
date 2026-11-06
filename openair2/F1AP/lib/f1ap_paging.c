/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"

#include "f1ap_paging.h"
#include "f1ap_lib_common.h"
#include "f1ap_lib_includes.h"
#include "f1ap_messages_types.h"

/** @brief encode to F1AP_PagingDRX_t */
static F1AP_PagingDRX_t encode_f1_paging_drx(const f1ap_paging_drx_t drx)
{
  switch (drx) {
    case F1AP_PAGING_DRX_32:
      return F1AP_PagingDRX_v32;
    case F1AP_PAGING_DRX_64:
      return F1AP_PagingDRX_v64;
    case F1AP_PAGING_DRX_128:
      return F1AP_PagingDRX_v128;
    case F1AP_PAGING_DRX_256:
      return F1AP_PagingDRX_v256;
    default:
      AssertFatal(1 == 0, "Unknown Paging DRX: %u\n", drx);
  }
  return 0;
}

/** @brief decode from F1AP_PagingDRX_t */
static f1ap_paging_drx_t decode_f1_paging_drx(const F1AP_PagingDRX_t drx)
{
  switch (drx) {
    case F1AP_PagingDRX_v32:
      return F1AP_PAGING_DRX_32;
    case F1AP_PagingDRX_v64:
      return F1AP_PAGING_DRX_64;
    case F1AP_PagingDRX_v128:
      return F1AP_PAGING_DRX_128;
    case F1AP_PagingDRX_v256:
      return F1AP_PAGING_DRX_256;
    default:
      AssertFatal(1 == 0, "Unknown Paging DRX: %ld\n", drx);
  }
  return 0;
}

/** @brief encode F1 Paging (9.2.6 Paging 3GPP TS 38.473) */
F1AP_F1AP_PDU_t *encode_f1ap_paging(const f1ap_paging_t *msg)
{
  DevAssert(msg);
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = F1AP_ProcedureCode_id_Paging;
  initMsg->criticality = F1AP_Criticality_reject;
  initMsg->value.present = F1AP_InitiatingMessage__value_PR_Paging;
  F1AP_Paging_t *paging = &initMsg->value.choice.Paging;

  /* UEIdentityIndexValue (Mandatory) 10 bits */
  {
    AssertFatal(msg->ue_identity_index_value <= 1023,
                "UE Identity Index Value exceeds 10 bits: %d\n",
                msg->ue_identity_index_value);
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_UEIdentityIndexValue;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_PagingIEs__value_PR_UEIdentityIndexValue;
    F1AP_UEIdentityIndexValue_t *ue_identity_index = &ie->value.choice.UEIdentityIndexValue;
    ue_identity_index->present = F1AP_UEIdentityIndexValue_PR_indexLength10;
    UEIDENTITYINDEX_TO_BIT_STRING(msg->ue_identity_index_value, &ue_identity_index->choice.indexLength10);
  }

  /* PagingIdentity (Mandatory) */
  {
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_PagingIdentity;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_PagingIEs__value_PR_PagingIdentity;

    F1AP_PagingIdentity_t *paging_identity = &ie->value.choice.PagingIdentity;
    const f1ap_paging_identity_t *paging_id = &msg->identity;

    if (msg->identity_type == F1AP_PAGING_IDENTITY_CN_UE) {
      // CN UE Paging Identity (5G-S-TMSI)
      paging_identity->present = F1AP_PagingIdentity_PR_cNUEPagingIdentity;
      asn1cCalloc(paging_identity->choice.cNUEPagingIdentity, id);
      id->present = F1AP_CNUEPagingIdentity_PR_fiveG_S_TMSI;
      FIVEG_S_TMSI_TO_BIT_STRING(paging_id->cn_ue_paging_identity, &id->choice.fiveG_S_TMSI);
    } else {
      // RAN UE Paging Identity (I-RNTI)
      paging_identity->present = F1AP_PagingIdentity_PR_rANUEPagingIdentity;
      asn1cCalloc(paging_identity->choice.rANUEPagingIdentity, ran_id);
      // I-RNTI is 40 bits, stored in 5 bytes
      IRNTI_TO_BIT_STRING(paging_id->ran_ue_paging_identity, &ran_id->iRNTI);
    }
  }

  /* Paging Priority (Optional) */
  if (msg->priority != NULL) {
    AssertFatal(*msg->priority >= F1AP_PAGING_PRIO_LEVEL1 && *msg->priority <= F1AP_PAGING_PRIO_LEVEL8,
                "Unknown Paging Priority: %d\n",
                *msg->priority);
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_PagingPriority;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_PagingIEs__value_PR_PagingPriority;
    ie->value.choice.PagingPriority = (F1AP_PagingPriority_t)*msg->priority;
  }

  /* Paging Origin (Optional) */
  if (msg->origin != NULL) {
    AssertFatal(*msg->origin == F1AP_PAGING_ORIGIN_NON_3GPP, "Unknown Paging Origin: %d\n", *msg->origin);
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_PagingOrigin;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_PagingIEs__value_PR_PagingOrigin;
    ie->value.choice.PagingOrigin = F1AP_PagingOrigin_non_3gpp;
  }

  /* PagingDRX (Optional) */
  if (msg->drx != NULL) {
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_PagingDRX;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_PagingIEs__value_PR_PagingDRX;
    ie->value.choice.PagingDRX = encode_f1_paging_drx(*msg->drx);
  }

  /* PagingCell_list (Mandatory) */
  {
    AssertFatal(msg->n_cells > 0 && msg->n_cells <= F1AP_MAX_NB_CELLS,
                "invalid n_cells %u (must be 1..%d)\n",
                msg->n_cells,
                F1AP_MAX_NB_CELLS);
    DevAssert(msg->cells != NULL);
    asn1cSequenceAdd(paging->protocolIEs.list, F1AP_PagingIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_PagingCell_List;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_PagingIEs__value_PR_PagingCell_list;

    // Add all cells in the paging cell list
    for (uint16_t cell_idx = 0; cell_idx < msg->n_cells; cell_idx++) {
      asn1cSequenceAdd(ie->value.choice.PagingCell_list, F1AP_PagingCell_ItemIEs_t, item);
      item->id = F1AP_ProtocolIE_ID_id_PagingCell_Item;
      item->criticality = F1AP_Criticality_reject;
      item->value.present = F1AP_PagingCell_ItemIEs__value_PR_PagingCell_Item;
      // NRCGI (Mandatory)
      F1AP_NRCGI_t *nRCGI = &item->value.choice.PagingCell_Item.nRCGI;
      const f1ap_paging_cell_item_t *cell = &msg->cells[cell_idx];
      MCC_MNC_TO_PLMNID(cell->plmn.mcc, cell->plmn.mnc, cell->plmn.mnc_digit_length, &nRCGI->pLMN_Identity);
      NR_CELL_ID_TO_BIT_STRING(cell->nr_cellid, &nRCGI->nRCellIdentity);
    }
  }

  return pdu;
}

/** @brief decode F1 Paging (9.2.6 Paging 3GPP TS 38.473) */
bool decode_f1ap_paging(f1ap_paging_t *out, const F1AP_F1AP_PDU_t *pdu)
{
  DevAssert(out);
  DevAssert(pdu);
  DevAssert(pdu->choice.initiatingMessage);
  memset(out, 0, sizeof(*out));

  _EQ_CHECK_INT(pdu->present, F1AP_F1AP_PDU_PR_initiatingMessage);
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, F1AP_ProcedureCode_id_Paging);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, F1AP_InitiatingMessage__value_PR_Paging);

  /* Check presence of mandatory IEs */
  const F1AP_Paging_t *in = &pdu->choice.initiatingMessage->value.choice.Paging;
  F1AP_PagingIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_PagingIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_UEIdentityIndexValue, true);
  F1AP_LIB_FIND_IE(F1AP_PagingIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_PagingIdentity, true);
  F1AP_LIB_FIND_IE(F1AP_PagingIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_PagingCell_List, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i] != NULL);
    ie = in->protocolIEs.list.array[i];
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_UEIdentityIndexValue:
        // (M) UE Identity Index Value
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_UEIdentityIndexValue);
        out->ue_identity_index_value = BIT_STRING_to_uint32(&ie->value.choice.UEIdentityIndexValue.choice.indexLength10);
        break;

      case F1AP_ProtocolIE_ID_id_PagingIdentity:
        // (M) Paging Identity
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_PagingIdentity);
        if (ie->value.choice.PagingIdentity.present == F1AP_PagingIdentity_PR_cNUEPagingIdentity) {
          // CN UE Paging Identity (5G-S-TMSI)
          out->identity_type = F1AP_PAGING_IDENTITY_CN_UE;
          BIT_STRING_t *fiveg_tmsi = &ie->value.choice.PagingIdentity.choice.cNUEPagingIdentity->choice.fiveG_S_TMSI;
          out->identity.cn_ue_paging_identity = BIT_STRING_to_uint64(fiveg_tmsi);
        } else if (ie->value.choice.PagingIdentity.present == F1AP_PagingIdentity_PR_rANUEPagingIdentity) {
          // RAN UE Paging Identity (I-RNTI)
          out->identity_type = F1AP_PAGING_IDENTITY_RAN_UE;
          BIT_STRING_t *irnti = &ie->value.choice.PagingIdentity.choice.rANUEPagingIdentity->iRNTI;
          out->identity.ran_ue_paging_identity = BIT_STRING_to_uint64(irnti);
        } else {
          PRINT_ERROR("Unknown PagingIdentity type: %d\n", ie->value.choice.PagingIdentity.present);
          return false;
        }
        break;

      case F1AP_ProtocolIE_ID_id_PagingDRX:
        // (O) Paging DRX
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_PagingDRX);
        out->drx = malloc_or_fail(sizeof(*out->drx));
        *out->drx = decode_f1_paging_drx(ie->value.choice.PagingDRX);
        break;

      case F1AP_ProtocolIE_ID_id_PagingPriority:
        // (O) Paging Priority
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_PagingPriority);
        AssertFatal(ie->value.choice.PagingPriority >= F1AP_PagingPriority_priolevel1
                        && ie->value.choice.PagingPriority <= F1AP_PagingPriority_priolevel8,
                    "Unknown Paging Priority: %ld\n",
                    ie->value.choice.PagingPriority);
        out->priority = malloc_or_fail(sizeof(*out->priority));
        *out->priority = (f1ap_paging_priority_t)ie->value.choice.PagingPriority;
        break;

      case F1AP_ProtocolIE_ID_id_PagingOrigin:
        // (O) Paging Origin
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_PagingOrigin);
        AssertFatal(ie->value.choice.PagingOrigin == F1AP_PagingOrigin_non_3gpp,
                    "Unknown Paging Origin: %ld\n",
                    ie->value.choice.PagingOrigin);
        out->origin = malloc_or_fail(sizeof(*out->origin));
        *out->origin = F1AP_PAGING_ORIGIN_NON_3GPP;
        break;

      case F1AP_ProtocolIE_ID_id_PagingCell_List:
        // (M) Paging Cell List (max 512 cells)
        _EQ_CHECK_INT(ie->value.present, F1AP_PagingIEs__value_PR_PagingCell_list);
        out->n_cells = ie->value.choice.PagingCell_list.list.count;
        AssertError(out->n_cells > 0, return false, "at least 1 cell must be present");
        AssertError(out->n_cells <= F1AP_MAX_NB_CELLS, return false, "too many cells");
        out->cells = calloc_or_fail(out->n_cells, sizeof(*out->cells));
        for (int j = 0; j < out->n_cells; j++) {
          F1AP_PagingCell_ItemIEs_t *cell_item = (F1AP_PagingCell_ItemIEs_t *)ie->value.choice.PagingCell_list.list.array[j];
          DevAssert(cell_item);
          _EQ_CHECK_LONG(cell_item->id, F1AP_ProtocolIE_ID_id_PagingCell_Item);
          _EQ_CHECK_INT(cell_item->value.present, F1AP_PagingCell_ItemIEs__value_PR_PagingCell_Item);
          F1AP_NRCGI_t *nRCGI = &cell_item->value.choice.PagingCell_Item.nRCGI;
          f1ap_paging_cell_item_t *cell = &out->cells[j];
          PLMNID_TO_MCC_MNC(&nRCGI->pLMN_Identity, cell->plmn.mcc, cell->plmn.mnc, cell->plmn.mnc_digit_length);
          BIT_STRING_TO_NR_CELL_IDENTITY(&nRCGI->nRCellIdentity, cell->nr_cellid);
        }
        break;

      default:
        AssertError(1 == 0, return false, "F1AP_ProtocolIE_ID_id %ld unknown\n", ie->id);
        break;
    }
  }
  return true;
}

/** @brief free F1 Paging */
void free_f1ap_paging(f1ap_paging_t *msg)
{
  DevAssert(msg);
  free(msg->drx);
  free(msg->priority);
  free(msg->origin);
  free(msg->cells);
}

/** @brief Equality check for two F1 Paging */
bool eq_f1ap_paging(const f1ap_paging_t *a, const f1ap_paging_t *b)
{
  DevAssert(a);
  DevAssert(b);
  _EQ_CHECK_INT(a->ue_identity_index_value, b->ue_identity_index_value);
  _EQ_CHECK_INT(a->identity_type, b->identity_type);
  if (a->identity_type == F1AP_PAGING_IDENTITY_CN_UE) {
    _EQ_CHECK_LONG(a->identity.cn_ue_paging_identity, b->identity.cn_ue_paging_identity);
  } else {
    _EQ_CHECK_LONG(a->identity.ran_ue_paging_identity, b->identity.ran_ue_paging_identity);
  }
  _EQ_CHECK_OPTIONAL_IE(a, b, drx, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, priority, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, origin, _EQ_CHECK_INT);
  _EQ_CHECK_INT(a->n_cells, b->n_cells);
  if ((a->n_cells > 0 && (a->cells == NULL || b->cells == NULL)))
    return false;
  for (int i = 0; i < a->n_cells; i++) {
    if (!eq_f1ap_plmn(&a->cells[i].plmn, &b->cells[i].plmn))
      return false;
    _EQ_CHECK_LONG(a->cells[i].nr_cellid, b->cells[i].nr_cellid);
  }
  return true;
}

/** @brief deep copy F1 Paging */
f1ap_paging_t cp_f1ap_paging(const f1ap_paging_t *orig)
{
  DevAssert(orig);
  f1ap_paging_t cp = {0};
  cp.ue_identity_index_value = orig->ue_identity_index_value;
  cp.identity_type = orig->identity_type;
  cp.identity = orig->identity;
  if (orig->drx != NULL) {
    cp.drx = malloc_or_fail(sizeof(*cp.drx));
    *cp.drx = *orig->drx;
  }
  if (orig->priority != NULL) {
    cp.priority = malloc_or_fail(sizeof(*cp.priority));
    *cp.priority = *orig->priority;
  }
  if (orig->origin != NULL) {
    cp.origin = malloc_or_fail(sizeof(*cp.origin));
    *cp.origin = *orig->origin;
  }
  cp.n_cells = orig->n_cells;
  if (cp.n_cells > 0) {
    DevAssert(orig->cells != NULL);
    cp.cells = calloc_or_fail(cp.n_cells, sizeof(*cp.cells));
    for (int i = 0; i < cp.n_cells; i++) {
      cp.cells[i] = orig->cells[i];
    }
  }
  return cp;
}
