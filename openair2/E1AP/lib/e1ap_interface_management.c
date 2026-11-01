/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <stdlib.h>

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"

#include "e1ap_lib_common.h"
#include "e1ap_lib_includes.h"
#include "e1ap_messages_types.h"
#include "e1ap_interface_management.h"

void encode_criticality_diagnostics(const criticality_diagnostics_t *msg, E1AP_CriticalityDiagnostics_t *out)
{
  // Procedure Code (O)
  if (msg->procedure_code) {
    out->procedureCode = calloc_or_fail(1, sizeof(*out->procedureCode));
    *out->procedureCode = *msg->procedure_code;
  }
  // Triggering Message (O)
  if (msg->triggering_msg) {
    out->triggeringMessage = calloc_or_fail(1, sizeof(*out->triggeringMessage));
    *out->triggeringMessage = *msg->triggering_msg;
  }
  // Procedure Criticality (O)
  if (msg->procedure_criticality) {
    out->procedureCriticality = calloc_or_fail(1, sizeof(*out->procedureCriticality));
    *out->procedureCriticality = *msg->procedure_criticality;
  }
  for (int i = 0; i < msg->num_errors; i++) {
    out->iEsCriticalityDiagnostics = calloc_or_fail(1, sizeof(*out->iEsCriticalityDiagnostics));
    asn1cSequenceAdd(out->iEsCriticalityDiagnostics->list, struct E1AP_CriticalityDiagnostics_IE_List__Member, ieC0);
    ieC0->iE_ID = msg->errors[i].ie_id;
    ieC0->typeOfError = msg->errors[i].error_type;
    ieC0->iECriticality = msg->errors[i].criticality;
  }
}

bool decode_criticality_diagnostics(const E1AP_CriticalityDiagnostics_t *in, criticality_diagnostics_t *out)
{
  // Procedure Code (O)
  if (in->procedureCode) {
    out->procedure_code = malloc_or_fail(sizeof(*out->procedure_code));
    *out->procedure_code = *in->procedureCode;
  }
  // Triggering Message (O)
  if (in->triggeringMessage) {
    out->triggering_msg = malloc_or_fail(sizeof(*out->triggering_msg));
    *out->triggering_msg = *in->triggeringMessage;
  }
  // Procedure Criticality (O)
  if (in->procedureCriticality) {
    out->procedure_criticality = malloc_or_fail(sizeof(*out->procedure_criticality));
    *out->procedure_criticality = *in->procedureCriticality;
  }
  // Criticality Diagnostics IE list
  if (in->iEsCriticalityDiagnostics) {
    for (int i = 0; i < in->iEsCriticalityDiagnostics->list.count; i++) {
      struct E1AP_CriticalityDiagnostics_IE_List__Member *ie = in->iEsCriticalityDiagnostics->list.array[i];
      out->num_errors++;
      out->errors[i].ie_id = ie->iE_ID;
      out->errors[i].error_type = ie->typeOfError;
      out->errors[i].criticality = ie->iECriticality;
    }
  }

  return true;
}

/* ====================================
 *      GNB-CU-UP E1 SETUP REQUEST
 * ==================================== */

/**
 * @brief Encode GNB-CU-UP E1 Setup Request
 */
E1AP_E1AP_PDU_t *encode_e1ap_cuup_setup_request(const e1ap_setup_req_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  // Message Type (M)
  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, m);
  m->procedureCode = E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup;
  m->criticality = E1AP_Criticality_reject;
  m->value.present = E1AP_InitiatingMessage__value_PR_GNB_CU_UP_E1SetupRequest;
  E1AP_GNB_CU_UP_E1SetupRequest_t *e1SetupUP = &m->value.choice.GNB_CU_UP_E1SetupRequest;
  // Transaction ID (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC1);
  ieC1->id = E1AP_ProtocolIE_ID_id_TransactionID;
  ieC1->criticality = E1AP_Criticality_reject;
  ieC1->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_TransactionID;
  ieC1->value.choice.TransactionID = msg->transac_id;
  // gNB-CU-UP ID (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC2);
  ieC2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID;
  ieC2->criticality = E1AP_Criticality_reject;
  ieC2->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_ID;
  asn_int642INTEGER(&ieC2->value.choice.GNB_CU_UP_ID, msg->gNB_cu_up_id);
  // gNB-CU-UP Name (O)
  if (msg->gNB_cu_up_name) {
    asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC3);
    ieC3->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name;
    ieC3->criticality = E1AP_Criticality_ignore;
    ieC3->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_Name;
    OCTET_STRING_fromBuf(&ieC3->value.choice.GNB_CU_UP_Name, msg->gNB_cu_up_name, strlen(msg->gNB_cu_up_name));
  }
  // CN Support (M)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC4);
  ieC4->id = E1AP_ProtocolIE_ID_id_CNSupport;
  ieC4->criticality = E1AP_Criticality_reject;
  ieC4->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_CNSupport;
  DevAssert(msg->cn_support == cn_support_5GC); /* only 5GC supported */
  ieC4->value.choice.CNSupport = msg->cn_support == cn_support_5GC ? E1AP_CNSupport_c_5gc : E1AP_CNSupport_c_epc;
  // Supported PLMNs (1..<maxnoofSPLMNs>)
  asn1cSequenceAdd(e1SetupUP->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ieC5);
  ieC5->id = E1AP_ProtocolIE_ID_id_SupportedPLMNs;
  ieC5->criticality = E1AP_Criticality_reject;
  ieC5->value.present = E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_SupportedPLMNs_List;
  for (int i = 0; i < msg->supported_plmns; i++) {
    asn1cSequenceAdd(ieC5->value.choice.SupportedPLMNs_List.list, E1AP_SupportedPLMNs_Item_t, supportedPLMN);
    // PLMN Identity (M)
    const PLMN_ID_t *id = &msg->plmn[i].id;
    MCC_MNC_TO_PLMNID(id->mcc, id->mnc, id->mnc_digit_length, &supportedPLMN->pLMN_Identity);
    // Slice Support List (O)
    int n = msg->plmn[i].supported_slices;
    if (msg->plmn[i].slice != NULL && n > 0) {
      supportedPLMN->slice_Support_List = calloc_or_fail(1, sizeof(*supportedPLMN->slice_Support_List));
      for (int s = 0; s < n; ++s) {
        asn1cSequenceAdd(supportedPLMN->slice_Support_List->list, E1AP_Slice_Support_Item_t, slice);
        e1ap_nssai_t *nssai = &msg->plmn[i].slice[s];
        INT8_TO_OCTET_STRING(nssai->sst, &slice->sNSSAI.sST);
        if (nssai->sd != 0xffffff) {
          slice->sNSSAI.sD = malloc_or_fail(sizeof(*slice->sNSSAI.sD));
          INT24_TO_OCTET_STRING(nssai->sd, slice->sNSSAI.sD);
        }
      }
    }
  }
  return pdu;
}

bool decode_e1ap_cuup_setup_request(const E1AP_E1AP_PDU_t *pdu, e1ap_setup_req_t *out)
{
  E1AP_GNB_CU_UP_E1SetupRequestIEs_t *ie;
  E1AP_GNB_CU_UP_E1SetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.GNB_CU_UP_E1SetupRequest;
  // Transaction ID (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_TransactionID, true);
  // gNB-CU-UP ID (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID, true);
  // CN Support (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_CNSupport, true);
  // Supported PLMNs (1..<maxnoofSPLMNs>)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_SupportedPLMNs, true);
  // Loop over all IEs
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_TransactionID:
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_TransactionID);
        out->transac_id = ie->value.choice.TransactionID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_ID);
        asn_INTEGER2ulong(&ie->value.choice.GNB_CU_UP_ID, &out->gNB_cu_up_id);
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name:
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_GNB_CU_UP_Name);
        // gNB-CU-UP Name (O)
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_Name, false);
        if (ie != NULL) {
          out->gNB_cu_up_name = calloc_or_fail(ie->value.choice.GNB_CU_UP_Name.size + 1, sizeof(char));
          memcpy(out->gNB_cu_up_name, ie->value.choice.GNB_CU_UP_Name.buf, ie->value.choice.GNB_CU_UP_Name.size);
        }
        break;
      case E1AP_ProtocolIE_ID_id_CNSupport:
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_CNSupport);
        _EQ_CHECK_LONG((long)ie->value.choice.CNSupport, (long)E1AP_CNSupport_c_5gc); // only 5GC CN Support supported
        out->cn_support = ie->value.choice.CNSupport;
        break;
      case E1AP_ProtocolIE_ID_id_SupportedPLMNs:
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupRequestIEs__value_PR_SupportedPLMNs_List);
        out->supported_plmns = ie->value.choice.SupportedPLMNs_List.list.count;

        for (int i = 0; i < out->supported_plmns; i++) {
          E1AP_SupportedPLMNs_Item_t *supported_plmn_item =
              (E1AP_SupportedPLMNs_Item_t *)(ie->value.choice.SupportedPLMNs_List.list.array[i]);

          /* PLMN Identity */
          PLMN_ID_t *id = &out->plmn[i].id;
          PLMNID_TO_MCC_MNC(&supported_plmn_item->pLMN_Identity, id->mcc, id->mnc, id->mnc_digit_length);

          /* NSSAI */
          if (supported_plmn_item->slice_Support_List) {
            int n = supported_plmn_item->slice_Support_List->list.count;
            out->plmn[i].supported_slices = n;
            out->plmn[i].slice = calloc_or_fail(n, sizeof(*out->plmn[i].slice));
            for (int s = 0; s < n; ++s) {
              e1ap_nssai_t *slice = &out->plmn[i].slice[s];
              const E1AP_SNSSAI_t *es = &supported_plmn_item->slice_Support_List->list.array[s]->sNSSAI;
              OCTET_STRING_TO_INT8(&es->sST, slice->sst);
              slice->sd = 0xffffff;
              if (es->sD != NULL)
                OCTET_STRING_TO_INT24(es->sD, slice->sd);
            }
          }
        }
        break;
      default:
        PRINT_ERROR("Handle for this IE %ld is not implemented (or) invalid IE detected\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief Deep copy function for E1 CUUP Setup Request
 */
e1ap_setup_req_t cp_e1ap_cuup_setup_request(const e1ap_setup_req_t *msg)
{
  e1ap_setup_req_t cp = {0};
  cp = *msg;
  if (msg->gNB_cu_up_name) {
    cp.gNB_cu_up_name = strdup(msg->gNB_cu_up_name);
  }
  for (int i = 0; i < msg->supported_plmns; i++) {
    if (msg->plmn[i].slice && msg->plmn[i].supported_slices > 0) {
      cp.plmn[i].slice = calloc_or_fail(msg->plmn[i].supported_slices, sizeof(*cp.plmn[i].slice));
      for (int s = 0; s < msg->plmn[i].supported_slices; ++s) {
        cp.plmn[i].slice[s] = msg->plmn[i].slice[s];
      }
    }
  }
  return cp;
}

/**
 * @brief Free memory allocated for E1 CUUP Setup Request
 */
void free_e1ap_cuup_setup_request(e1ap_setup_req_t *msg)
{
  // gNB-CU-UP Name
  free(msg->gNB_cu_up_name);
  // Slice support lists within PLMNs
  for (int i = 0; i < msg->supported_plmns; i++) {
    free(msg->plmn[i].slice);
  }
}

/**
 * @brief Equality check function for E1 CUUP Setup Request
 */
bool eq_e1ap_cuup_setup_request(const e1ap_setup_req_t *a, const e1ap_setup_req_t *b)
{
  _EQ_CHECK_UINT64(a->transac_id, b->transac_id);
  _EQ_CHECK_UINT64(a->gNB_cu_up_id, b->gNB_cu_up_id);
  if (a->gNB_cu_up_name && b->gNB_cu_up_name)
    _EQ_CHECK_STR(a->gNB_cu_up_name, b->gNB_cu_up_name);
  _EQ_CHECK_INT(a->supported_plmns, b->supported_plmns);
  for (int i = 0; i < a->supported_plmns; i++) {
    _EQ_CHECK_INT(a->plmn[i].id.mcc, b->plmn[i].id.mcc);
    _EQ_CHECK_INT(a->plmn[i].id.mnc, b->plmn[i].id.mnc);
    _EQ_CHECK_INT(a->plmn[i].id.mnc_digit_length, b->plmn[i].id.mnc_digit_length);
    _EQ_CHECK_INT(a->plmn[i].supported_slices, b->plmn[i].supported_slices);
    for (int s = 0; s < a->plmn[i].supported_slices; ++s) {
      _EQ_CHECK_INT(a->plmn[i].slice[s].sst, b->plmn[i].slice[s].sst);
      _EQ_CHECK_INT(a->plmn[i].slice[s].sd, b->plmn[i].slice[s].sd);
    }
  }
  return true;
}

/* ====================================
 *     GNB-CU-UP E1 SETUP RESPONSE
 * ==================================== */

/**
 * @brief Encode GNB-CU-UP E1 Setup Response
 */
E1AP_E1AP_PDU_t *encode_e1ap_cuup_setup_response(const e1ap_setup_resp_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  /* Create */
  /* 0. pdu Type */
  pdu->present = E1AP_E1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, initMsg);
  initMsg->procedureCode = E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup;
  initMsg->criticality = E1AP_Criticality_reject;
  initMsg->value.present = E1AP_SuccessfulOutcome__value_PR_GNB_CU_UP_E1SetupResponse;
  E1AP_GNB_CU_UP_E1SetupResponse_t *out = &pdu->choice.successfulOutcome->value.choice.GNB_CU_UP_E1SetupResponse;
  // Transaction ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ieC1);
  ieC1->id = E1AP_ProtocolIE_ID_id_TransactionID;
  ieC1->criticality = E1AP_Criticality_reject;
  ieC1->value.present = E1AP_GNB_CU_UP_E1SetupResponseIEs__value_PR_TransactionID;
  ieC1->value.choice.TransactionID = msg->transac_id;
  // Encode gNB_CU_CP_Name (O)
  if (msg->gNB_cu_cp_name) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ieC2);
    ieC2->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_Name;
    ieC2->criticality = E1AP_Criticality_ignore;
    ieC2->value.present = E1AP_GNB_CU_UP_E1SetupResponseIEs__value_PR_GNB_CU_CP_Name;
    OCTET_STRING_fromBuf(&ieC2->value.choice.GNB_CU_CP_Name, msg->gNB_cu_cp_name, strlen(msg->gNB_cu_cp_name));
  }
  // Encode TNLA Info (O)
  if (msg->tnla_info) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ieC3);
    ieC3->id = E1AP_ProtocolIE_ID_id_Transport_Layer_Address_Info;
    ieC3->criticality = E1AP_Criticality_ignore;
    ieC3->value.present = E1AP_GNB_CU_UP_E1SetupResponseIEs__value_PR_Transport_Layer_Address_Info;
    // Transport UP Layer Addresses Info to Add List
    for (int i = 0; i < msg->tnla_info->num_addresses_to_add; i++) {
      E1AP_Transport_UP_Layer_Addresses_Info_To_Add_List_t *a = calloc_or_fail(1, sizeof(*a));
      ieC3->value.choice.Transport_Layer_Address_Info.transport_UP_Layer_Addresses_Info_To_Add_List =
          (struct E1AP_Transport_UP_Layer_Addresses_Info_To_Add_List *)a;
      asn1cSequenceAdd(a->list, E1AP_Transport_UP_Layer_Addresses_Info_To_Add_Item_t, ieC4);
      in_addr_t *ipsec = &msg->tnla_info->addresses_to_add[i].ipsec_tl_address;
      TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(*ipsec, &ieC4->iP_SecTransportLayerAddress);
      for (int j = 0; j < msg->tnla_info->addresses_to_add[i].num_gtp_tl_addresses; j++) {
        E1AP_GTPTLAs_t *g = calloc_or_fail(1, sizeof(*g));
        ieC4->gTPTransportLayerAddressesToAdd = (struct E1AP_GTPTLAs *)g;
        asn1cSequenceAdd(g->list, E1AP_GTPTLA_Item_t, ieC5);
        in_addr_t *gtp_tlna = &msg->tnla_info->addresses_to_add[i].gtp_tl_addresses[j];
        TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(*gtp_tlna, &ieC5->gTPTransportLayerAddresses);
      }
    }
    // Transport UP Layer Addresses Info to Remove List
    for (int i = 0; i < msg->tnla_info->num_addresses_to_remove; i++) {
      E1AP_Transport_UP_Layer_Addresses_Info_To_Remove_List_t *a = calloc_or_fail(1, sizeof(*a));
      ieC3->value.choice.Transport_Layer_Address_Info.transport_UP_Layer_Addresses_Info_To_Remove_List =
          (struct E1AP_Transport_UP_Layer_Addresses_Info_To_Remove_List *)a;
      asn1cSequenceAdd(a->list, E1AP_Transport_UP_Layer_Addresses_Info_To_Remove_Item_t, ieC4);
      in_addr_t *ipsec = &msg->tnla_info->addresses_to_remove[i].ipsec_tl_address;
      TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(*ipsec, &ieC4->iP_SecTransportLayerAddress);
      for (int j = 0; j < msg->tnla_info->addresses_to_remove[i].num_gtp_tl_addresses; j++) {
        E1AP_GTPTLAs_t *g = calloc_or_fail(1, sizeof(*g));
        ieC4->gTPTransportLayerAddressesToRemove = (struct E1AP_GTPTLAs *)g;
        asn1cSequenceAdd(g->list, E1AP_GTPTLA_Item_t, ieC5);
        in_addr_t *gtp_tlna = &msg->tnla_info->addresses_to_remove[i].gtp_tl_addresses[j];
        TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(*gtp_tlna, &ieC5->gTPTransportLayerAddresses);
      }
    }
  }
  return pdu;
}

/**
 * @brief Decode GNB-CU-UP E1 Setup Response
 */
bool decode_e1ap_cuup_setup_response(const E1AP_E1AP_PDU_t *pdu, e1ap_setup_resp_t *out)
{
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_successfulOutcome);
  _EQ_CHECK_LONG(pdu->choice.successfulOutcome->procedureCode, E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup);
  _EQ_CHECK_INT(pdu->choice.successfulOutcome->value.present, E1AP_SuccessfulOutcome__value_PR_GNB_CU_UP_E1SetupResponse);
  const E1AP_GNB_CU_UP_E1SetupResponse_t *in = &pdu->choice.successfulOutcome->value.choice.GNB_CU_UP_E1SetupResponse;
  E1AP_GNB_CU_UP_E1SetupResponseIEs_t *ie;
  // Transaction ID (M)
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_TransactionID, true);
  // Decode TNLA Info (O)
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_TransactionID:
        out->transac_id = ie->value.choice.TransactionID;
        break;

      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_Name:
        // gNB-CU-CP Name (O)
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_Name, false);
        _EQ_CHECK_INT(ie->value.present, E1AP_GNB_CU_UP_E1SetupResponseIEs__value_PR_GNB_CU_CP_Name);
        if (ie != NULL) {
          out->gNB_cu_cp_name = calloc_or_fail(ie->value.choice.GNB_CU_CP_Name.size + 1, sizeof(char));
          memcpy(out->gNB_cu_cp_name, ie->value.choice.GNB_CU_CP_Name.buf, ie->value.choice.GNB_CU_CP_Name.size);
        }
        break;

      case E1AP_ProtocolIE_ID_id_Transport_Layer_Address_Info:
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Transport_Layer_Address_Info, false);
        out->tnla_info = calloc_or_fail(1, sizeof(*out->tnla_info));
        // Transport UP Layer Addresses Info to Add List
        const E1AP_Transport_UP_Layer_Addresses_Info_To_Add_List_t *a =
            ie->value.choice.Transport_Layer_Address_Info.transport_UP_Layer_Addresses_Info_To_Add_List;
        out->tnla_info->num_addresses_to_add = a->list.count;
        for (int i = 0; i < a->list.count; i++) {
          const E1AP_Transport_UP_Layer_Addresses_Info_To_Add_Item_t *item = a->list.array[i];
          tnl_address_info_item_t *to_add = &out->tnla_info->addresses_to_add[i];
          BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&item->iP_SecTransportLayerAddress, to_add->ipsec_tl_address);
          const E1AP_GTPTLAs_t *gtp_list = item->gTPTransportLayerAddressesToAdd;
          to_add->num_gtp_tl_addresses = gtp_list->list.count;
          for (int j = 0; j < gtp_list->list.count; j++) {
            const E1AP_GTPTLA_Item_t *gtp_item = gtp_list->list.array[j];
            BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gtp_item->gTPTransportLayerAddresses, to_add->gtp_tl_addresses[j]);
          }
        }
        // Transport UP Layer Addresses Info to Remove List
        const E1AP_Transport_UP_Layer_Addresses_Info_To_Remove_List_t *r =
            ie->value.choice.Transport_Layer_Address_Info.transport_UP_Layer_Addresses_Info_To_Remove_List;
        out->tnla_info->num_addresses_to_remove = r->list.count;

        for (int i = 0; i < r->list.count; i++) {
          const E1AP_Transport_UP_Layer_Addresses_Info_To_Remove_Item_t *item = r->list.array[i];
          tnl_address_info_item_t *to_remove = &out->tnla_info->addresses_to_remove[i];
          BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&item->iP_SecTransportLayerAddress, to_remove->ipsec_tl_address);

          const E1AP_GTPTLAs_t *gtp_list = item->gTPTransportLayerAddressesToRemove;
          to_remove->num_gtp_tl_addresses = gtp_list->list.count;
          for (int j = 0; j < gtp_list->list.count; j++) {
            const E1AP_GTPTLA_Item_t *gtp_item = gtp_list->list.array[j];
            BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gtp_item->gTPTransportLayerAddresses, to_remove->gtp_tl_addresses[j]);
          }
        }
        break;

      default:
        PRINT_ERROR("Handle for this IE %ld is not implemented (or) invalid IE detected\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief Deep copy GNB-CU-UP E1 Setup Response
 */
e1ap_setup_resp_t cp_e1ap_cuup_setup_response(const e1ap_setup_resp_t *msg)
{
  e1ap_setup_resp_t cp = {0};
  cp.transac_id = msg->transac_id;
  if (msg->gNB_cu_cp_name) {
    cp.gNB_cu_cp_name = strdup(msg->gNB_cu_cp_name);
  }
  if (msg->tnla_info) {
    cp.tnla_info = calloc_or_fail(1, sizeof(*cp.tnla_info));
    *cp.tnla_info = *msg->tnla_info;
  }
  return cp;
}

/**
 * @brief Free GNB-CU-UP E1 Setup Response
 */
void free_e1ap_cuup_setup_response(e1ap_setup_resp_t *msg)
{
  free(msg->gNB_cu_cp_name);
  free(msg->tnla_info);
}

/**
 * @brief Equality check for GNB-CU-UP E1 Setup Response
 */
bool eq_e1ap_cuup_setup_response(const e1ap_setup_resp_t *a, const e1ap_setup_resp_t *b)
{
  _EQ_CHECK_LONG(a->transac_id, b->transac_id);
  if ((a->gNB_cu_cp_name && !b->gNB_cu_cp_name) || (!a->gNB_cu_cp_name && b->gNB_cu_cp_name))
    return false;
  if (a->gNB_cu_cp_name && b->gNB_cu_cp_name)
    _EQ_CHECK_STR(a->gNB_cu_cp_name, b->gNB_cu_cp_name);
  if ((a->tnla_info && !b->tnla_info) || (!a->tnla_info && b->tnla_info))
    return false;
  if (a->tnla_info && b->tnla_info) {
    _EQ_CHECK_INT(a->tnla_info->num_addresses_to_add, b->tnla_info->num_addresses_to_add);
    _EQ_CHECK_INT(a->tnla_info->num_addresses_to_remove, b->tnla_info->num_addresses_to_remove);
    for (int i = 0; i < a->tnla_info->num_addresses_to_add; i++) {
      tnl_address_info_item_t *a_to_add = &a->tnla_info->addresses_to_add[i];
      tnl_address_info_item_t *b_to_add = &b->tnla_info->addresses_to_add[i];
      _EQ_CHECK_UINT32(a_to_add->ipsec_tl_address, b_to_add->ipsec_tl_address);
      for (int j = 0; j < a_to_add->num_gtp_tl_addresses; j++) {
        _EQ_CHECK_UINT32(a_to_add->gtp_tl_addresses[j], b_to_add->gtp_tl_addresses[j]);
      }
    }
    for (int i = 0; i < a->tnla_info->num_addresses_to_remove; i++) {
      tnl_address_info_item_t *a_to_rem = &a->tnla_info->addresses_to_remove[i];
      tnl_address_info_item_t *b_to_rem = &b->tnla_info->addresses_to_remove[i];
      _EQ_CHECK_UINT32(a_to_rem->ipsec_tl_address, b_to_rem->ipsec_tl_address);
      for (int j = 0; j < a_to_rem->num_gtp_tl_addresses; j++) {
        _EQ_CHECK_UINT32(a_to_rem->gtp_tl_addresses[j], b_to_rem->gtp_tl_addresses[j]);
      }
    }
  }
  return true;
}

/* ====================================
 *     GNB-CU-UP E1 SETUP FAILURE
 * ==================================== */

/**
 * @brief Encoder for GNB-CU-UP E1 Setup Failure
 * @ref 9.2.1.6 GNB-CU-UP E1 SETUP FAILURE of 3GPP TS 38.463
 */
E1AP_E1AP_PDU_t *encode_e1ap_cuup_setup_failure(const e1ap_setup_fail_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(E1AP_E1AP_PDU_t));
  // Allocate and populate the unsuccessfulOutcome structure
  pdu->present = E1AP_E1AP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, initMsg);
  initMsg->procedureCode = E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup;
  initMsg->criticality = E1AP_Criticality_reject;
  initMsg->value.present = E1AP_UnsuccessfulOutcome__value_PR_GNB_CU_UP_E1SetupFailure;
  E1AP_GNB_CU_UP_E1SetupFailure_t *out = &pdu->choice.unsuccessfulOutcome->value.choice.GNB_CU_UP_E1SetupFailure;
  // Transaction ID
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_TransactionID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_GNB_CU_UP_E1SetupFailureIEs__value_PR_TransactionID;
  ie1->value.choice.TransactionID = msg->transac_id;
  // Cause
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_Cause;
  ie2->criticality = E1AP_Criticality_ignore;
  ie2->value.present = E1AP_GNB_CU_UP_E1SetupFailureIEs__value_PR_Cause;
  ie2->value.choice.Cause = e1_encode_cause_ie(&msg->cause);
  // Time To Wait (O)
  if (msg->time_to_wait) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie3);
    ie3->id = E1AP_ProtocolIE_ID_id_TimeToWait;
    ie3->criticality = E1AP_Criticality_ignore;
    ie3->value.present = E1AP_GNB_CU_UP_E1SetupFailureIEs__value_PR_TimeToWait;
    ie3->value.choice.TimeToWait = *msg->time_to_wait;
  }
  // Criticality Diagnostics (O)
  if (msg->crit_diag) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie4);
    ie4->id = E1AP_ProtocolIE_ID_id_CriticalityDiagnostics;
    ie4->criticality = E1AP_Criticality_ignore;
    ie4->value.present = E1AP_GNB_CU_UP_E1SetupFailureIEs__value_PR_CriticalityDiagnostics;
    encode_criticality_diagnostics(msg->crit_diag, &ie4->value.choice.CriticalityDiagnostics);
  }
  return pdu;
}

/**
 * @brief Decoder for GNB-CU-UP E1 Setup Failure
 */
bool decode_e1ap_cuup_setup_failure(const E1AP_E1AP_PDU_t *pdu, e1ap_setup_fail_t *out)
{
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_unsuccessfulOutcome);
  _EQ_CHECK_LONG(pdu->choice.unsuccessfulOutcome->procedureCode, E1AP_ProcedureCode_id_gNB_CU_UP_E1Setup);
  const E1AP_GNB_CU_UP_E1SetupFailure_t *in = &pdu->choice.unsuccessfulOutcome->value.choice.GNB_CU_UP_E1SetupFailure;
  E1AP_GNB_CU_UP_E1SetupFailureIEs_t *ie;
  // Check mandatory IEs first
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_TransactionID, true);
  E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Cause, true);
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_TransactionID:
        out->transac_id = ie->value.choice.TransactionID;
        break;
      case E1AP_ProtocolIE_ID_id_Cause:
        // Cause
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Cause, true);
        out->cause = e1_decode_cause_ie(&ie->value.choice.Cause);
        break;
      case E1AP_ProtocolIE_ID_id_TimeToWait:
        // Time To Wait (O)
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_TimeToWait, false);
        out->time_to_wait = calloc_or_fail(1, sizeof(*out->time_to_wait));
        *out->time_to_wait = ie->value.choice.TimeToWait;
        break;
      case E1AP_ProtocolIE_ID_id_CriticalityDiagnostics:
        // Criticality Diagnostics (O)
        E1AP_LIB_FIND_IE(E1AP_GNB_CU_UP_E1SetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_CriticalityDiagnostics, false);
        out->crit_diag = calloc_or_fail(1, sizeof(*out->crit_diag));
        decode_criticality_diagnostics(&ie->value.choice.CriticalityDiagnostics, out->crit_diag);
        break;
      default:
        PRINT_ERROR("Handle for this IE %ld is not implemented (or) invalid IE detected\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief Deep copy of GNB-CU-UP E1 Setup Failure message
 */
e1ap_setup_fail_t cp_e1ap_cuup_setup_failure(const e1ap_setup_fail_t *msg)
{
  e1ap_setup_fail_t cp = {0};
  cp.transac_id = msg->transac_id;
  cp.cause = msg->cause;
  if (msg->time_to_wait) {
    cp.time_to_wait = calloc_or_fail(1, sizeof(*cp.time_to_wait));
    *cp.time_to_wait = *msg->time_to_wait;
  }
  if (msg->crit_diag) {
    cp.crit_diag = calloc_or_fail(1, sizeof(*cp.crit_diag));
    *cp.crit_diag = *msg->crit_diag;
    if (msg->crit_diag->procedure_code) {
      cp.crit_diag->procedure_code = calloc_or_fail(1, sizeof(*cp.crit_diag->procedure_code));
      *cp.crit_diag->procedure_code = *msg->crit_diag->procedure_code;
    }
    if (msg->crit_diag->procedure_criticality) {
      cp.crit_diag->procedure_criticality = calloc_or_fail(1, sizeof(*cp.crit_diag->procedure_criticality));
      *cp.crit_diag->procedure_criticality = *msg->crit_diag->procedure_criticality;
    }
    if (msg->crit_diag->triggering_msg) {
      cp.crit_diag->triggering_msg = calloc_or_fail(1, sizeof(*cp.crit_diag->triggering_msg));
      *cp.crit_diag->triggering_msg = *msg->crit_diag->triggering_msg;
    }
  }
  return cp;
}

/**
 * @brief Free memory allocated for GNB-CU-UP E1 Setup Failure message
 */
void free_e1ap_cuup_setup_failure(e1ap_setup_fail_t *msg)
{
  free(msg->time_to_wait);
  if (msg->crit_diag) {
    free(msg->crit_diag->triggering_msg);
    free(msg->crit_diag->procedure_code);
    free(msg->crit_diag->procedure_criticality);
  }
  free(msg->crit_diag);
}

/**
 * @brief Equality check for GNB-CU-UP E1 Setup Failure message
 */
bool eq_e1ap_cuup_setup_failure(const e1ap_setup_fail_t *a, const e1ap_setup_fail_t *b)
{
  _EQ_CHECK_LONG(a->transac_id, b->transac_id);
  _EQ_CHECK_INT(a->cause.type, b->cause.type);
  _EQ_CHECK_INT(a->cause.value, b->cause.value);
  _EQ_CHECK_OPTIONAL_IE(a, b, time_to_wait, _EQ_CHECK_LONG);
  if (a->crit_diag && b->crit_diag) {
    if (a->crit_diag->procedure_code && b->crit_diag->procedure_code)
      _EQ_CHECK_INT(*(a->crit_diag->procedure_code), *(b->crit_diag->procedure_code));
    if (a->crit_diag->triggering_msg && b->crit_diag->triggering_msg)
      _EQ_CHECK_UINT32(*(a->crit_diag->triggering_msg), *(b->crit_diag->triggering_msg));
    if (a->crit_diag->procedure_criticality && b->crit_diag->procedure_criticality)
      _EQ_CHECK_UINT32(*(a->crit_diag->procedure_criticality), *(b->crit_diag->procedure_criticality));
    _EQ_CHECK_INT(a->crit_diag->num_errors, b->crit_diag->num_errors);
    for (int i = 0; i < a->crit_diag->num_errors; i++) {
      const criticality_diagnostics_ie_t *a_err = &a->crit_diag->errors[i];
      const criticality_diagnostics_ie_t *b_err = &b->crit_diag->errors[i];
      _EQ_CHECK_INT(a_err->ie_id, b_err->ie_id);
      _EQ_CHECK_INT(a_err->error_type, b_err->error_type);
      _EQ_CHECK_INT(a_err->criticality, b_err->criticality);
    }
  }
  return true;
}
