/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief xnap interface handler procedures for gNB
 */

#include "xnap_gNB_interface_management.h"
#include "xnap_lib_common.h"

/**
 * @brief XnAP Setup Request encoding
 */
XNAP_XnAP_PDU_t *encode_xn_setup_request(const xnap_setup_req_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* PDU type */
  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_xnSetup;
  initMsg->criticality = XNAP_Criticality_reject;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_XnSetupRequest;

  XNAP_XnSetupRequest_t *out = &initMsg->value.choice.XnSetupRequest;

  /* Global NG-RAN Node ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupRequest_IEs_t, ieC1);
  ieC1->id = XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID;
  ieC1->criticality = XNAP_Criticality_reject;
  ieC1->value.present = XNAP_XnSetupRequest_IEs__value_PR_GlobalNG_RANNode_ID;

  XNAP_GlobalNG_RANNode_ID_t *global = &ieC1->value.choice.GlobalNG_RANNode_ID;
  global->present = XNAP_GlobalNG_RANNode_ID_PR_gNB;
  asn1cCalloc(global->choice.gNB, gnb);

  MCC_MNC_TO_PLMNID(msg->plmn.mcc, msg->plmn.mnc, msg->plmn.mnc_digit_length, &gnb->plmn_id);
  gnb->gnb_id.present = XNAP_GNB_ID_Choice_PR_gnb_ID;
  MACRO_GNB_ID_TO_BIT_STRING(msg->gNB_id, &gnb->gnb_id.choice.gnb_ID);

  /* TAI Support List (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupRequest_IEs_t, ieC2);
  ieC2->id = XNAP_ProtocolIE_ID_id_TAISupport_list;
  ieC2->criticality = XNAP_Criticality_reject;
  ieC2->value.present = XNAP_XnSetupRequest_IEs__value_PR_TAISupport_List;

  for (int i = 0; i < msg->num_tai; i++) {
    asn1cSequenceAdd(ieC2->value.choice.TAISupport_List.list, XNAP_TAISupport_Item_t, taiItem);
    INT24_TO_OCTET_STRING(msg->tai_support[i].tac, &taiItem->tac);

    for (int j = 0; j < msg->tai_support[i].num_plmn; j++) {
      const xnap_plmn_support_t *plmn = &msg->tai_support[i].plmn_support[j];
      asn1cSequenceAdd(taiItem->broadcastPLMNs.list, XNAP_BroadcastPLMNinTAISupport_Item_t, plmnItem);
      MCC_MNC_TO_PLMNID(plmn->plmn.mcc, plmn->plmn.mnc, plmn->plmn.mnc_digit_length, &plmnItem->plmn_id);

      for (int k = 0; k < plmn->num_nssai; k++) {
        asn1cSequenceAdd(plmnItem->tAISliceSupport_List.list, XNAP_S_NSSAI_t, nssai);
        INT8_TO_OCTET_STRING(plmn->nssai[k].sst, &nssai->sst);
        if (plmn->nssai[k].sd) { asn1cCalloc(nssai->sd, sd); INT24_TO_OCTET_STRING(plmn->nssai[k].sd, sd); }
      }
    }
  }

  /* AMF Region Information (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupRequest_IEs_t, ieC3);
  ieC3->id = XNAP_ProtocolIE_ID_id_AMF_Region_Information;
  ieC3->criticality = XNAP_Criticality_reject;
  ieC3->value.present = XNAP_XnSetupRequest_IEs__value_PR_AMF_Region_Information;

  for (int i = 0; i < msg->num_amf_regions; i++) {
    const xnap_amf_region_info_t *amf = &msg->amf_region_info[i];
    asn1cSequenceAdd(ieC3->value.choice.AMF_Region_Information.list, XNAP_GlobalAMF_Region_Information_t, amfItem);
    MCC_MNC_TO_PLMNID(amf->plmn.mcc, amf->plmn.mnc, amf->plmn.mnc_digit_length, &amfItem->plmn_ID);
    AMF_REGION_TO_BIT_STRING(amf->amf_region_id, &amfItem->amf_region_id);
  }

  return pdu;
}

/**
 * @brief XnAP Setup Request decoding
 */
bool decode_xn_setup_request(xnap_setup_req_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check presence of message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "pdu->choice.initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_xnSetup);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_XnSetupRequest);
  /* XnSetupRequest container */
  XNAP_XnSetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.XnSetupRequest;

  XNAP_XnSetupRequest_IEs_t *ie;
  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_XnSetupRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID, true);
  XNAP_LIB_FIND_IE(XNAP_XnSetupRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_TAISupport_list, true);
  XNAP_LIB_FIND_IE(XNAP_XnSetupRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_AMF_Region_Information, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    AssertError(in->protocolIEs.list.array[i] != NULL, return false, "protocolIEs.list.array[i] is NULL");
    ie = in->protocolIEs.list.array[i];
    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID: {
        /* Global NG-RAN Node ID (M) */
        _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupRequest_IEs__value_PR_GlobalNG_RANNode_ID);
        XNAP_GlobalNG_RANNode_ID_t *global_id = &ie->value.choice.GlobalNG_RANNode_ID;
        AssertError(global_id->present == XNAP_GlobalNG_RANNode_ID_PR_gNB, return false, "Only gNB supported");
        AssertError(global_id->choice.gNB != NULL, return false, "gNB is NULL");
        /* Decode PLMN */
        PLMNID_TO_MCC_MNC(&global_id->choice.gNB->plmn_id,
                          out->plmn.mcc,
                          out->plmn.mnc,
                          out->plmn.mnc_digit_length);
        /* Decode gNB ID */
        AssertError(global_id->choice.gNB->gnb_id.present == XNAP_GNB_ID_Choice_PR_gnb_ID, return false, "Unsupported gNB ID choice");
        MACRO_BIT_STRING_TO_GNB_ID(&global_id->choice.gNB->gnb_id.choice.gnb_ID, out->gNB_id);
      } break;

      case XNAP_ProtocolIE_ID_id_TAISupport_list: {
        /* TAI Support List (M) */
        _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupRequest_IEs__value_PR_TAISupport_List);
        int tai_count = ie->value.choice.TAISupport_List.list.count;
        out->num_tai = tai_count;
        out->tai_support = calloc_or_fail(tai_count, sizeof(*out->tai_support));
        for (int m = 0; m < tai_count; m++) {
          XNAP_TAISupport_Item_t *tai_item = ie->value.choice.TAISupport_List.list.array[m];
          /* TAC */
          OCTET_STRING_TO_INT24(&tai_item->tac, out->tai_support[m].tac);
          /* PLMN list */
          out->tai_support[m].num_plmn = tai_item->broadcastPLMNs.list.count;
          out->tai_support[m].plmn_support = calloc_or_fail(out->tai_support[m].num_plmn, sizeof(*out->tai_support[m].plmn_support));
          for (int j = 0; j < out->tai_support[m].num_plmn; j++) {
            XNAP_BroadcastPLMNinTAISupport_Item_t *plmn_item = tai_item->broadcastPLMNs.list.array[j];
            xnap_plmn_support_t *plmn = &out->tai_support[m].plmn_support[j];
            PLMNID_TO_MCC_MNC(&plmn_item->plmn_id,
	                      plmn->plmn.mcc,
                              plmn->plmn.mnc,
                              plmn->plmn.mnc_digit_length);
            /* NSSAI */
            plmn->num_nssai = plmn_item->tAISliceSupport_List.list.count;
            plmn->nssai = calloc_or_fail(plmn->num_nssai, sizeof(*plmn->nssai));
            for (int k = 0; k < plmn->num_nssai; k++) {
              OCTET_STRING_TO_INT8(&plmn_item->tAISliceSupport_List.list.array[k]->sst, plmn->nssai[k].sst);
              if (plmn_item->tAISliceSupport_List.list.array[k]->sd) {
                OCTET_STRING_TO_INT24(plmn_item->tAISliceSupport_List.list.array[k]->sd, plmn->nssai[k].sd);
              }
            }
          }
        }
      } break;

      case XNAP_ProtocolIE_ID_id_AMF_Region_Information: {
        /* AMF Region Information (M) */
        _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupRequest_IEs__value_PR_AMF_Region_Information);
        out->num_amf_regions = ie->value.choice.AMF_Region_Information.list.count;
        out->amf_region_info = calloc_or_fail(out->num_amf_regions, sizeof(*out->amf_region_info));
        for (int a = 0; a < out->num_amf_regions; a++) {
          XNAP_GlobalAMF_Region_Information_t *amf = ie->value.choice.AMF_Region_Information.list.array[a];
          PLMNID_TO_MCC_MNC(&amf->plmn_ID,
                            out->amf_region_info[a].plmn.mcc,
                            out->amf_region_info[a].plmn.mnc,
                            out->amf_region_info[a].plmn.mnc_digit_length);
          out->amf_region_info[a].amf_region_id = BIT_STRING_to_uint8(&amf->amf_region_id);
        }
      } break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief XnAP Setup Request main equality check
 */
bool eq_xnap_setup_request(const xnap_setup_req_t *a, const xnap_setup_req_t *b)
{
  _EQ_CHECK_UINT32(a->gNB_id, b->gNB_id);
  if (!eq_xnap_plmn(&a->plmn, &b->plmn)) return false;

  _EQ_CHECK_INT(a->num_tai, b->num_tai);
  for (int i = 0; i < a->num_tai; i++) {
    if (!eq_xnap_tai_support(&a->tai_support[i], &b->tai_support[i]))
      return false;
  }

  _EQ_CHECK_INT(a->num_amf_regions, b->num_amf_regions);
  for (int i = 0; i < a->num_amf_regions; i++) {
    if (!eq_xnap_plmn(&a->amf_region_info[i].plmn, &b->amf_region_info[i].plmn))
      return false;
    _EQ_CHECK_INT(a->amf_region_info[i].amf_region_id, b->amf_region_info[i].amf_region_id);
  }

  return true;
}

static void free_xnap_plmn_support(xnap_plmn_support_t *plmn_support, uint8_t num_plmn)
{
  if (!plmn_support) return;
  for (int i = 0; i < num_plmn; i++) {
    free(plmn_support[i].nssai);
  }
  free(plmn_support);
}

static void free_xnap_tai_support(xnap_tai_support_t *tai_support, uint16_t num_tai)
{
  if (!tai_support) return;
  for (int i = 0; i < num_tai; i++) {
    free_xnap_plmn_support(tai_support[i].plmn_support,
                           tai_support[i].num_plmn);
  }
  free(tai_support);
}

/**
 * @brief XnAP Setup Request memory management
 */
void free_xnap_setup_request(const xnap_setup_req_t *msg)
{
  DevAssert(msg != NULL);
  free_xnap_tai_support(msg->tai_support, msg->num_tai);
  free(msg->amf_region_info);
}

/**
 * @brief XnAP Setup Response encoding
 */
XNAP_XnAP_PDU_t *encode_xn_setup_response(const xnap_setup_resp_t *resp)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* PDU type */
  pdu->present = XNAP_XnAP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, succMsg);
  succMsg->procedureCode = XNAP_ProcedureCode_id_xnSetup;
  succMsg->criticality = XNAP_Criticality_reject;
  succMsg->value.present = XNAP_SuccessfulOutcome__value_PR_XnSetupResponse;

  XNAP_XnSetupResponse_t *out = &succMsg->value.choice.XnSetupResponse;

  /* Global NG-RAN Node ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupResponse_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_XnSetupResponse_IEs__value_PR_GlobalNG_RANNode_ID;

  XNAP_GlobalNG_RANNode_ID_t *global = &ie1->value.choice.GlobalNG_RANNode_ID;
  global->present = XNAP_GlobalNG_RANNode_ID_PR_gNB;
  asn1cCalloc(global->choice.gNB, gnb);

  MCC_MNC_TO_PLMNID(resp->plmn.mcc, resp->plmn.mnc, resp->plmn.mnc_digit_length, &gnb->plmn_id);
  gnb->gnb_id.present = XNAP_GNB_ID_Choice_PR_gnb_ID;
  MACRO_GNB_ID_TO_BIT_STRING(resp->gNB_id, &gnb->gnb_id.choice.gnb_ID);

  /* TAI Support List (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupResponse_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_TAISupport_list;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_XnSetupResponse_IEs__value_PR_TAISupport_List;

  for (int i = 0; i < resp->num_tai; i++) {
    asn1cSequenceAdd(ie2->value.choice.TAISupport_List.list, XNAP_TAISupport_Item_t, taiItem);
    INT24_TO_OCTET_STRING(resp->tai_support[i].tac, &taiItem->tac);

    for (int j = 0; j < resp->tai_support[i].num_plmn; j++) {
      const xnap_plmn_support_t *plmn = &resp->tai_support[i].plmn_support[j];
      asn1cSequenceAdd(taiItem->broadcastPLMNs.list, XNAP_BroadcastPLMNinTAISupport_Item_t, plmnItem);
      MCC_MNC_TO_PLMNID(plmn->plmn.mcc, plmn->plmn.mnc, plmn->plmn.mnc_digit_length, &plmnItem->plmn_id);

      for (int k = 0; k < plmn->num_nssai; k++) {
        asn1cSequenceAdd(plmnItem->tAISliceSupport_List.list, XNAP_S_NSSAI_t, nssai);
        INT8_TO_OCTET_STRING(plmn->nssai[k].sst, &nssai->sst);
        if (plmn->nssai[k].sd) { asn1cCalloc(nssai->sd, sd); INT24_TO_OCTET_STRING(plmn->nssai[k].sd, sd); }
      }
    }
  }

  return pdu;
}

/**
 * @brief XnAP Setup Response decoding
 */
bool decode_xn_setup_response(xnap_setup_resp_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check presence of message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_successfulOutcome);
  AssertError(pdu->choice.successfulOutcome != NULL, return false, "successfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.successfulOutcome->procedureCode, XNAP_ProcedureCode_id_xnSetup);
  _EQ_CHECK_INT(pdu->choice.successfulOutcome->value.present, XNAP_SuccessfulOutcome__value_PR_XnSetupResponse);

  /* XnSetupResponse container */
  XNAP_XnSetupResponse_t *in = &pdu->choice.successfulOutcome->value.choice.XnSetupResponse;
  XNAP_XnSetupResponse_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_XnSetupResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID, true);
  XNAP_LIB_FIND_IE(XNAP_XnSetupResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_TAISupport_list, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    AssertError(in->protocolIEs.list.array[i] != NULL, return false, "protocolIEs.list.array[i] is NULL");
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {

      case XNAP_ProtocolIE_ID_id_GlobalNG_RAN_node_ID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupResponse_IEs__value_PR_GlobalNG_RANNode_ID);
        XNAP_GlobalNG_RANNode_ID_t *global = &ie->value.choice.GlobalNG_RANNode_ID;
        AssertError(global->present == XNAP_GlobalNG_RANNode_ID_PR_gNB, return false, "Only gNB supported");
        AssertError(global->choice.gNB != NULL, return false, "gNB is NULL");

        PLMNID_TO_MCC_MNC(&global->choice.gNB->plmn_id, out->plmn.mcc, out->plmn.mnc, out->plmn.mnc_digit_length);
        AssertError(global->choice.gNB->gnb_id.present == XNAP_GNB_ID_Choice_PR_gnb_ID, return false, "Unsupported gNB ID");
        MACRO_BIT_STRING_TO_GNB_ID(&global->choice.gNB->gnb_id.choice.gnb_ID, out->gNB_id);
      } break;

      case XNAP_ProtocolIE_ID_id_TAISupport_list: {
        _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupResponse_IEs__value_PR_TAISupport_List);
        out->num_tai = ie->value.choice.TAISupport_List.list.count;
        out->tai_support = calloc_or_fail(out->num_tai, sizeof(*out->tai_support));

        for (int m = 0; m < out->num_tai; m++) {
          XNAP_TAISupport_Item_t *tai = ie->value.choice.TAISupport_List.list.array[m];
          OCTET_STRING_TO_INT24(&tai->tac, out->tai_support[m].tac);

          out->tai_support[m].num_plmn = tai->broadcastPLMNs.list.count;
          out->tai_support[m].plmn_support = calloc_or_fail(out->tai_support[m].num_plmn, sizeof(*out->tai_support[m].plmn_support));

          for (int j = 0; j < out->tai_support[m].num_plmn; j++) {
            XNAP_BroadcastPLMNinTAISupport_Item_t *plmn_item = tai->broadcastPLMNs.list.array[j];
            xnap_plmn_support_t *plmn = &out->tai_support[m].plmn_support[j];

            PLMNID_TO_MCC_MNC(&plmn_item->plmn_id, plmn->plmn.mcc, plmn->plmn.mnc, plmn->plmn.mnc_digit_length);

            plmn->num_nssai = plmn_item->tAISliceSupport_List.list.count;
            plmn->nssai = calloc_or_fail(plmn->num_nssai, sizeof(*plmn->nssai));

            for (int k = 0; k < plmn->num_nssai; k++) {
              OCTET_STRING_TO_INT8(&plmn_item->tAISliceSupport_List.list.array[k]->sst, plmn->nssai[k].sst);
              if (plmn_item->tAISliceSupport_List.list.array[k]->sd) {
                OCTET_STRING_TO_INT24(plmn_item->tAISliceSupport_List.list.array[k]->sd, plmn->nssai[k].sd);
              }
            }
          }
        }
      } break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }
  return true;
}

/**
 * @brief XnAP Setup Response main equality check
 */
bool eq_xnap_setup_response(const xnap_setup_resp_t *a, const xnap_setup_resp_t *b)
{
  _EQ_CHECK_UINT32(a->gNB_id, b->gNB_id);
  if (!eq_xnap_plmn(&a->plmn, &b->plmn)) return false;

  _EQ_CHECK_INT(a->num_tai, b->num_tai);
  for (int i = 0; i < a->num_tai; i++) {
    if (!eq_xnap_tai_support(&a->tai_support[i], &b->tai_support[i]))
      return false;
  }

  return true;
}

/**
 * @brief XnAP Setup Response memory management
 */
void free_xnap_setup_response(const xnap_setup_resp_t *msg)
{
  DevAssert(msg != NULL);
  free_xnap_tai_support(msg->tai_support, msg->num_tai);
}

/**
 * @brief XnAP Setup Failure encoding
 */
XNAP_XnAP_PDU_t *encode_xn_setup_failure(const xnap_setup_failure_t *fail)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type */
  pdu->present = XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, failMsg);
  failMsg->procedureCode = XNAP_ProcedureCode_id_xnSetup;
  failMsg->criticality = XNAP_Criticality_reject;
  failMsg->value.present = XNAP_UnsuccessfulOutcome__value_PR_XnSetupFailure;

  XNAP_XnSetupFailure_t *out = &failMsg->value.choice.XnSetupFailure;

  /* Cause (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_XnSetupFailure_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_Cause;
  ie1->criticality = XNAP_Criticality_ignore;
  ie1->value.present = XNAP_XnSetupFailure_IEs__value_PR_Cause;
  xnap_gNB_set_cause(&ie1->value.choice.Cause, &fail->cause);
  
  return pdu;
}

/**
 * @brief XnAP Setup Failure decoding
 */
bool decode_xn_setup_failure(xnap_setup_failure_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_unsuccessfulOutcome);
  AssertError(pdu->choice.unsuccessfulOutcome != NULL, return false, "unsuccessfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.unsuccessfulOutcome->procedureCode, XNAP_ProcedureCode_id_xnSetup);
  _EQ_CHECK_INT(pdu->choice.unsuccessfulOutcome->value.present, XNAP_UnsuccessfulOutcome__value_PR_XnSetupFailure);

  /* XnSetupFailure container */
  XNAP_XnSetupFailure_t *in = &pdu->choice.unsuccessfulOutcome->value.choice.XnSetupFailure;
  XNAP_XnSetupFailure_IEs_t *ie;

  /* Check presence of mandatory IE */
  XNAP_LIB_FIND_IE(XNAP_XnSetupFailure_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_Cause, true);

  /* Decode Cause */
  _EQ_CHECK_INT(ie->value.present, XNAP_XnSetupFailure_IEs__value_PR_Cause);
  out->cause = decode_xnap_cause(&ie->value.choice.Cause);

  return true;
}

/**
 * @brief XnAP Setup Failure equality check
 */
bool eq_xnap_setup_failure(const xnap_setup_failure_t *a, const xnap_setup_failure_t *b)
{
  return eq_xnap_cause(&a->cause, &b->cause);
}

/**
 * @brief XnAP Setup Failure memory management
 */
void free_xnap_setup_failure(xnap_setup_failure_t *msg)
{
  // nothing to free
  UNUSED(msg);
}
