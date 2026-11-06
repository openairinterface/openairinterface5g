/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief xnap mobility management functions for gNB
 */

#include "xnap_gNB_mobility_management.h"
#include "xnap_lib_common.h"

/**
 * @brief XnAP security capabilities encoding
 */
static XNAP_UESecurityCapabilities_t xnap_encode_security_capabilities(const xnap_security_capabilities_t *in)
{
  DevAssert(in != NULL);

  XNAP_UESecurityCapabilities_t out = {0};
  ENCRALG_TO_BIT_STRING(in->nRencryption_algorithms, &out.nr_EncyptionAlgorithms);
  INTPROTALG_TO_BIT_STRING(in->nRintegrity_algorithms, &out.nr_IntegrityProtectionAlgorithms);
  ENCRALG_TO_BIT_STRING(in->eUTRAencryption_algorithms, &out.e_utra_EncyptionAlgorithms);
  INTPROTALG_TO_BIT_STRING(in->eUTRAintegrity_algorithms, &out.e_utra_IntegrityProtectionAlgorithms);

  return out;
}

/**
 * @brief XnAP security capabilities decoding
 */
static bool decode_xnap_ue_security_capabilities(const XNAP_UESecurityCapabilities_t *in, xnap_security_capabilities_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  out->nRencryption_algorithms = BIT_STRING_to_uint16(&in->nr_EncyptionAlgorithms);
  out->nRintegrity_algorithms = BIT_STRING_to_uint16(&in->nr_IntegrityProtectionAlgorithms);
  out->eUTRAencryption_algorithms = BIT_STRING_to_uint16(&in->e_utra_EncyptionAlgorithms);
  out->eUTRAintegrity_algorithms = BIT_STRING_to_uint16(&in->e_utra_IntegrityProtectionAlgorithms);

  return true;
}

/**
 * @brief XnAP UL NGU TNL information encoding
 */
static XNAP_UPTransportLayerInformation_t xnap_encode_ul_ngu_tnl_info(const gtpu_tunnel_t *in)
{
  DevAssert(in != NULL);

  XNAP_UPTransportLayerInformation_t out = {0};
  out.present = XNAP_UPTransportLayerInformation_PR_gtpTunnel;
  asn1cCalloc(out.choice.gtpTunnel, gtp);
  uint32_t addr = 0;
  memcpy(&addr, in->addr.buffer, sizeof(addr));
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(addr, &gtp->tnl_address);
  INT32_TO_OCTET_STRING(in->teid, &gtp->gtp_teid);

  return out;
}

static bool decode_xnap_ul_ngu_tnl_info(const XNAP_UPTransportLayerInformation_t *in, gtpu_tunnel_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  if (in->present != XNAP_UPTransportLayerInformation_PR_gtpTunnel)
    return false;

  const XNAP_GTPtunnelTransportLayerInformation_t *gtp = in->choice.gtpTunnel;
  DevAssert(gtp != NULL);

  uint32_t addr = 0;
  BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gtp->tnl_address, addr);
  memcpy(out->addr.buffer, &addr, sizeof(addr));
  out->addr.length = 4;
  OCTET_STRING_TO_INT32(&gtp->gtp_teid, out->teid);

  return true;
}

static bool decode_xnap_ue_history_information(const XNAP_UEHistoryInformation_t *in, xnap_handover_req_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  out->num_last_visited_cells = in->list.count;
  DevAssert(out->num_last_visited_cells > 0);

  out->ue_history_info = calloc(out->num_last_visited_cells, sizeof(*out->ue_history_info));
  DevAssert(out->ue_history_info != NULL);

  for (int i = 0; i < out->num_last_visited_cells; i++) {
    XNAP_LastVisitedCell_Item_t *lv = in->list.array[i];
    if (lv == NULL)
      continue;

    if (lv->present != XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell)
      continue;

    XNAP_LastVisitedNGRANCellInformation_t *nr = &lv->choice.nG_RAN_Cell;
    ue_history_info_t *cell = &out->ue_history_info[i];

    cell->xnap_cell_type = XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell;
    cell->last_visited_cell_info = create_byte_array(nr->size, nr->buf);
  }

  return true;
}

static bool decode_xnap_qos_flows_to_be_setup_list(const XNAP_QoSFlowsToBeSetup_List_t *in,
                                                   xnap_pdusession_resources_tobe_setup_item_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  out->num_qos = in->list.count;
  DevAssert(out->num_qos > 0);

  out->qos_list = calloc_or_fail(out->num_qos, sizeof(*out->qos_list));

  for (int q = 0; q < out->num_qos; q++) {
    XNAP_QoSFlowsToBeSetup_Item_t *qos = in->list.array[q];
    xnap_qos_flow_tobe_setup_item_t *qo = &out->qos_list[q];

    qo->qfi = qos->qfi;

    const XNAP_QoSFlowLevelQoSParameters_t *params = &qos->qosFlowLevelQoSParameters;

    qo->qos_params.arp.priority_level = params->allocationAndRetentionPrio.priorityLevel;
    qo->qos_params.arp.pre_emp_capability = params->allocationAndRetentionPrio.pre_emption_capability;
    qo->qos_params.arp.pre_emp_vulnerability = params->allocationAndRetentionPrio.pre_emption_vulnerability;

    const XNAP_QoSCharacteristics_t *qchar = &params->qos_characteristics;

    if (qchar->present == XNAP_QoSCharacteristics_PR_non_dynamic) {
      qo->qos_params.qos_type = NON_DYNAMIC;
      qo->qos_params.nondyn.fiveQI = qchar->choice.non_dynamic->fiveQI;
    } else {
      const XNAP_Dynamic5QIDescriptor_t *dyn = qchar->choice.dynamic;

      qo->qos_params.qos_type = DYNAMIC;
      qo->qos_params.dyn.prio = dyn->priorityLevelQoS;
      qo->qos_params.dyn.pdb = dyn->packetDelayBudget;
      qo->qos_params.dyn.per.scalar = dyn->packetErrorRate.pER_Scalar;
      qo->qos_params.dyn.per.exponent = dyn->packetErrorRate.pER_Exponent;
    }
  }

  return true;
}

static XNAP_DRBBStatusTransferChoice_t xnap_encode_pdcp_count(const xnap_drb_count_value_t *in)
{
  XNAP_DRBBStatusTransferChoice_t out = {0};

  if (in->sn_len == XNAP_SN_LENGTH_18) {
    out.present = XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_18bits;
    asn1cCalloc(out.choice.pdcp_sn_18bits, pdcp18);
    pdcp18->cOUNTValue.pdcp_SN18 = in->pdcp_sn;
    pdcp18->cOUNTValue.hfn_PDCP_SN18 = in->hfn;
  } else {
    out.present = XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_12bits;
    asn1cCalloc(out.choice.pdcp_sn_12bits, pdcp12);
    pdcp12->cOUNTValue.pdcp_SN12 = in->pdcp_sn;
    pdcp12->cOUNTValue.hfn_PDCP_SN12 = in->hfn;
  }

  return out;
}

static bool decode_xnap_pdcp_count(const XNAP_DRBBStatusTransferChoice_t *in, xnap_drb_count_value_t *out)
{
  switch (in->present) {
    case XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_18bits:
      out->sn_len = XNAP_SN_LENGTH_18;
      out->pdcp_sn = in->choice.pdcp_sn_18bits->cOUNTValue.pdcp_SN18;
      out->hfn = in->choice.pdcp_sn_18bits->cOUNTValue.hfn_PDCP_SN18;
      break;

    case XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_12bits:
      out->sn_len = XNAP_SN_LENGTH_12;
      out->pdcp_sn = in->choice.pdcp_sn_12bits->cOUNTValue.pdcp_SN12;
      out->hfn = in->choice.pdcp_sn_12bits->cOUNTValue.hfn_PDCP_SN12;
      break;

    default:
      PRINT_ERROR("Unknown DRBBStatusTransferChoice.present=%d\n", in->present);
      return false;
  }

  return true;
}

/**
 * @brief XnAP PDU Session Resources To Be Setup List encoding
 */
static void xnap_encode_pdusession_resources_to_be_setup_list(const xnap_pdusession_resources_tobe_setup_item_t *in,
                                                               uint16_t num_pdu,
                                                               XNAP_PDUSessionResourcesToBeSetup_List_t *out)
{
  for (int i = 0; i < num_pdu; i++) {
    const xnap_pdusession_resources_tobe_setup_item_t *pdu = &in[i];
    asn1cSequenceAdd(out->list, XNAP_PDUSessionResourcesToBeSetup_Item_t, pduItem);

    pduItem->pduSessionId = pdu->pdusession_id;
    pduItem->s_NSSAI = xnap_encode_snssai(pdu->nssai);
    pduItem->uL_NG_U_TNLatUPF = xnap_encode_ul_ngu_tnl_info(&pdu->n3_incoming);
    pduItem->pduSessionType = pdu->pdu_session_type;

    for (int j = 0; j < pdu->num_qos; j++) {
      const xnap_qos_flow_tobe_setup_item_t *qos = &pdu->qos_list[j];
      asn1cSequenceAdd(pduItem->qosFlowsToBeSetup_List.list, XNAP_QoSFlowsToBeSetup_Item_t, qosItem);

      qosItem->qfi = qos->qfi;

      XNAP_QoSCharacteristics_t *qosChar = &qosItem->qosFlowLevelQoSParameters.qos_characteristics;
      if (qos->qos_params.qos_type == NON_DYNAMIC) {
        qosChar->present = XNAP_QoSCharacteristics_PR_non_dynamic;
        asn1cCalloc(qosChar->choice.non_dynamic, nonDyn);
        nonDyn->fiveQI = qos->qos_params.nondyn.fiveQI;
      } else {
        qosChar->present = XNAP_QoSCharacteristics_PR_dynamic;
        asn1cCalloc(qosChar->choice.dynamic, dyn);
        dyn->priorityLevelQoS = qos->qos_params.dyn.prio;
        dyn->packetDelayBudget = qos->qos_params.dyn.pdb;
        dyn->packetErrorRate.pER_Scalar = qos->qos_params.dyn.per.scalar;
        dyn->packetErrorRate.pER_Exponent = qos->qos_params.dyn.per.exponent;
      }

      XNAP_AllocationandRetentionPriority_t *arp = &qosItem->qosFlowLevelQoSParameters.allocationAndRetentionPrio;
      arp->priorityLevel = qos->qos_params.arp.priority_level;
      arp->pre_emption_capability = qos->qos_params.arp.pre_emp_capability;
      arp->pre_emption_vulnerability = qos->qos_params.arp.pre_emp_vulnerability;
    }
  }
}

/**
 * @brief XnAP PDU Session Resources To Be Setup List decoding
 */
static bool decode_xnap_pdusession_resources_to_be_setup_list(const XNAP_PDUSessionResourcesToBeSetup_List_t *in,
                                                               xnap_pdusession_resources_tobe_setup_item_t **out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  *out = calloc_or_fail(in->list.count, sizeof(**out));

  for (int p = 0; p < in->list.count; p++) {
    XNAP_PDUSessionResourcesToBeSetup_Item_t *pdu = in->list.array[p];
    xnap_pdusession_resources_tobe_setup_item_t *dst = &(*out)[p];

    dst->pdusession_id = pdu->pduSessionId;

    dst->nssai = calloc_or_fail(1, sizeof(*dst->nssai));
    if (!decode_xnap_snssai(&pdu->s_NSSAI, dst->nssai))
      return false;

    if (!decode_xnap_ul_ngu_tnl_info(&pdu->uL_NG_U_TNLatUPF, &dst->n3_incoming))
      return false;

    dst->pdu_session_type = pdu->pduSessionType;

    if (!decode_xnap_qos_flows_to_be_setup_list(&pdu->qosFlowsToBeSetup_List, dst))
      return false;
  }

  return true;
}

/**
 * @brief XnAP Handover Request encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_handover_request(const xnap_handover_req_t *req)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type */
  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_handoverPreparation;
  initMsg->criticality = XNAP_Criticality_reject;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_HandoverRequest;

  XNAP_HandoverRequest_t *out = &initMsg->value.choice.HandoverRequest;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_HandoverRequest_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = req->s_ng_node_ue_xnap_id;

  /* Cause (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_Cause;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_HandoverRequest_IEs__value_PR_Cause;
  xnap_gNB_set_cause(&ie2->value.choice.Cause, &req->cause);

  /* Target Cell Global ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_targetCellGlobalID;
  ie3->criticality = XNAP_Criticality_reject;
  ie3->value.present = XNAP_HandoverRequest_IEs__value_PR_Target_CGI;
  ie3->value.choice.Target_CGI = xnap_encode_target_cgi(&req->target_cgi);

  /* GUAMI (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie4);
  ie4->id = XNAP_ProtocolIE_ID_id_GUAMI;
  ie4->criticality = XNAP_Criticality_reject;
  ie4->value.present = XNAP_HandoverRequest_IEs__value_PR_GUAMI;
  MCC_MNC_TO_PLMNID(req->guami.plmn.mcc, req->guami.plmn.mnc, req->guami.plmn.mnc_digit_length, &ie4->value.choice.GUAMI.plmn_ID);
  AMF_REGION_TO_BIT_STRING(req->guami.amf_region_id, &ie4->value.choice.GUAMI.amf_region_id);
  AMF_SETID_TO_BIT_STRING(req->guami.amf_set_id, &ie4->value.choice.GUAMI.amf_set_id);
  AMF_POINTER_TO_BIT_STRING(req->guami.amf_pointer, &ie4->value.choice.GUAMI.amf_pointer);

  /* UE Context Information (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie5);
  ie5->id = XNAP_ProtocolIE_ID_id_UEContextInfoHORequest;
  ie5->criticality = XNAP_Criticality_reject;
  ie5->value.present = XNAP_HandoverRequest_IEs__value_PR_UEContextInfoHORequest;
  XNAP_UEContextInfoHORequest_t *ctx = &ie5->value.choice.UEContextInfoHORequest;

  /* NG-C UE associated Signalling reference */
  asn_uint642INTEGER(&ctx->ng_c_UE_reference, req->ue_context.ngc_ue_sig_ref);

  /* Signalling TNL association address at source NG-C side */
  ctx->cp_TNL_info_source.present = XNAP_CPTransportLayerInformation_PR_endpointIPAddress;
  uint32_t cp_tnl_addr = 0;
  memcpy(&cp_tnl_addr, req->ue_context.cp_tnl_ip_source.buffer, sizeof(cp_tnl_addr));
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(cp_tnl_addr, &ctx->cp_TNL_info_source.choice.endpointIPAddress);

  /* UE Security Capabilities */
  const xnap_security_capabilities_t *s_cap = &req->ue_context.security_capabilities;
  ctx->ueSecurityCapabilities = xnap_encode_security_capabilities(s_cap);

  /* AS Security Information */
  AS_KEY_STAR_TO_BIT_STRING(req->ue_context.as_security_key_ranstar, &ctx->securityInformation.key_NG_RAN_Star);
  ctx->securityInformation.ncc = req->ue_context.as_security_ncc;

  /* RRC Context (Handover Preparation Information) */
  OCTET_STRING_fromBuf(&ctx->rrc_Context, (const char *)req->ue_context.rrc_context.buf, req->ue_context.rrc_context.len);

  /* UE Aggregate Maximum Bit Rate */
  UEAGMAXBITRTD_TO_ASN_PRIMITIVES(req->ue_context.ue_ambr.br_dl, &ctx->ue_AMBR.dl_UE_AMBR);
  UEAGMAXBITRTU_TO_ASN_PRIMITIVES(req->ue_context.ue_ambr.br_ul, &ctx->ue_AMBR.ul_UE_AMBR);

  /* PDU Session Resources To Be Setup List */
  xnap_encode_pdusession_resources_to_be_setup_list(req->ue_context.pdusession_resources_tobe_setup_list,
                                                    req->ue_context.num_pdu,
                                                    &ctx->pduSessionResourcesToBeSetup_List);

  /* UE History Information (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequest_IEs_t, ie6);
  ie6->id = XNAP_ProtocolIE_ID_id_UEHistoryInformation;
  ie6->criticality = XNAP_Criticality_ignore;
  ie6->value.present = XNAP_HandoverRequest_IEs__value_PR_UEHistoryInformation;

  for (int i = 0; i < req->num_last_visited_cells; i++) {
    asn1cSequenceAdd(ie6->value.choice.UEHistoryInformation.list, XNAP_LastVisitedCell_Item_t, lastVisitedCellItem);
    if (req->ue_history_info[i].xnap_cell_type == XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell) {
      lastVisitedCellItem->present = XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell;
      XNAP_LastVisitedNGRANCellInformation_t *nrInfo = &lastVisitedCellItem->choice.nG_RAN_Cell;
      OCTET_STRING_fromBuf(nrInfo,
                           (const char *)req->ue_history_info[i].last_visited_cell_info.buf,
                           req->ue_history_info[i].last_visited_cell_info.len);
    }
  }

  return pdu;
}

/**
 * @brief XnAP Handover Request decoding
 */
bool decode_xnap_handover_request(xnap_handover_req_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_handoverPreparation);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_HandoverRequest);

  XNAP_HandoverRequest_t *in = &pdu->choice.initiatingMessage->value.choice.HandoverRequest;
  XNAP_HandoverRequest_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_Cause, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_targetCellGlobalID, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_GUAMI, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UEContextInfoHORequest, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UEHistoryInformation, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequest_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_Cause: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequest_IEs__value_PR_Cause);
        out->cause = decode_xnap_cause(&ie->value.choice.Cause);
      } break;

      case XNAP_ProtocolIE_ID_id_targetCellGlobalID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequest_IEs__value_PR_Target_CGI);
        if (!xnap_decode_target_cgi(&ie->value.choice.Target_CGI, &out->target_cgi))
          return false;
      } break;

      case XNAP_ProtocolIE_ID_id_GUAMI: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequest_IEs__value_PR_GUAMI);
        const XNAP_GUAMI_t *guami = &ie->value.choice.GUAMI;
        plmn_id_t *plmn = &out->guami.plmn;
        PLMNID_TO_MCC_MNC(&guami->plmn_ID, plmn->mcc, plmn->mnc, plmn->mnc_digit_length);
        out->guami.amf_region_id = BIT_STRING_to_uint8(&guami->amf_region_id);
        out->guami.amf_set_id = BIT_STRING_to_uint16(&guami->amf_set_id);
        out->guami.amf_pointer = BIT_STRING_to_uint8(&guami->amf_pointer);
      } break;

      case XNAP_ProtocolIE_ID_id_UEContextInfoHORequest: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequest_IEs__value_PR_UEContextInfoHORequest);
        XNAP_UEContextInfoHORequest_t *ctx = &ie->value.choice.UEContextInfoHORequest;

        asn_INTEGER2uint64(&ctx->ng_c_UE_reference, &out->ue_context.ngc_ue_sig_ref);

        uint32_t cp_tnl_addr = 0;
        BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&ctx->cp_TNL_info_source.choice.endpointIPAddress, cp_tnl_addr);
        memcpy(out->ue_context.cp_tnl_ip_source.buffer, &cp_tnl_addr, sizeof(cp_tnl_addr));
        out->ue_context.cp_tnl_ip_source.length = 4;

        asn_INTEGER2ulong(&ctx->ue_AMBR.ul_UE_AMBR, &out->ue_context.ue_ambr.br_ul);
        asn_INTEGER2ulong(&ctx->ue_AMBR.dl_UE_AMBR, &out->ue_context.ue_ambr.br_dl);

        xnap_security_capabilities_t *cap = &out->ue_context.security_capabilities;
        if (!decode_xnap_ue_security_capabilities(&ctx->ueSecurityCapabilities, cap))
          return false;

        memcpy(out->ue_context.as_security_key_ranstar, ctx->securityInformation.key_NG_RAN_Star.buf, 32);
        out->ue_context.as_security_ncc = ctx->securityInformation.ncc;

        out->ue_context.rrc_context = create_byte_array(ctx->rrc_Context.size, ctx->rrc_Context.buf);

        out->ue_context.num_pdu = ctx->pduSessionResourcesToBeSetup_List.list.count;
        if (!decode_xnap_pdusession_resources_to_be_setup_list(&ctx->pduSessionResourcesToBeSetup_List,
                                                               &out->ue_context.pdusession_resources_tobe_setup_list))
          return false;
      } break;

      case XNAP_ProtocolIE_ID_id_UEHistoryInformation:
        if (!decode_xnap_ue_history_information(&ie->value.choice.UEHistoryInformation, out))
          return false;
        break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }
  return true;
}

static bool eq_xnap_qos_flow_param(const xnap_qos_flow_param_t *a, const xnap_qos_flow_param_t *b)
{
  _EQ_CHECK_INT(a->qos_type, b->qos_type);
  if (a->qos_type == NON_DYNAMIC) {
    _EQ_CHECK_INT(a->nondyn.fiveQI, b->nondyn.fiveQI);
  } else {
    DevAssert(a->qos_type == DYNAMIC);
    DevAssert(b->qos_type == DYNAMIC);
    const xnap_dynamic_5qi_t *adyn = &a->dyn, *bdyn = &b->dyn;
    _EQ_CHECK_INT(adyn->prio, bdyn->prio);
    _EQ_CHECK_INT(adyn->pdb, bdyn->pdb);
    _EQ_CHECK_INT(adyn->per.scalar, bdyn->per.scalar);
    _EQ_CHECK_INT(adyn->per.exponent, bdyn->per.exponent);
  }
  _EQ_CHECK_INT(a->arp.priority_level, b->arp.priority_level);
  _EQ_CHECK_INT(a->arp.pre_emp_capability, b->arp.pre_emp_capability);
  _EQ_CHECK_INT(a->arp.pre_emp_vulnerability, b->arp.pre_emp_vulnerability);
  return true;
}

static bool eq_xnap_qos_tobe_setup_item(const xnap_qos_flow_tobe_setup_item_t *a, const xnap_qos_flow_tobe_setup_item_t *b)
{
  _EQ_CHECK_INT(a->qfi, b->qfi);
  return eq_xnap_qos_flow_param(&a->qos_params, &b->qos_params);
}

static bool eq_xnap_pdusession_resources_tobe_setup_item(const xnap_pdusession_resources_tobe_setup_item_t *a,
                                                         const xnap_pdusession_resources_tobe_setup_item_t *b)
{
  _EQ_CHECK_INT(a->pdusession_id, b->pdusession_id);

  if (!eq_xnap_snssai(a->nssai, b->nssai))
    return false;

  if (!eq_gtpu_tunnel(&a->n3_incoming, &b->n3_incoming))
    return false;

  _EQ_CHECK_INT(a->pdu_session_type, b->pdu_session_type);
  _EQ_CHECK_INT(a->num_qos, b->num_qos);

  for (int i = 0; i < a->num_qos; i++) {
    if (!eq_xnap_qos_tobe_setup_item(&a->qos_list[i], &b->qos_list[i]))
      return false;
  }

  return true;
}

static bool eq_xnap_security_capabilities(const xnap_security_capabilities_t *a, const xnap_security_capabilities_t *b)
{
  _EQ_CHECK_INT(a->nRencryption_algorithms, b->nRencryption_algorithms);
  _EQ_CHECK_INT(a->nRintegrity_algorithms, b->nRintegrity_algorithms);
  _EQ_CHECK_INT(a->eUTRAencryption_algorithms, b->eUTRAencryption_algorithms);
  _EQ_CHECK_INT(a->eUTRAintegrity_algorithms, b->eUTRAintegrity_algorithms);
  return true;
}

static bool eq_xnap_ue_context_info(const xnap_ue_context_info_t *a, const xnap_ue_context_info_t *b)
{
  _EQ_CHECK_LONG(a->ngc_ue_sig_ref, b->ngc_ue_sig_ref);

  if (!eq_transport_layer_addr(&a->cp_tnl_ip_source, &b->cp_tnl_ip_source))
    return false;

  if (!eq_xnap_security_capabilities(&a->security_capabilities, &b->security_capabilities))
    return false;

  if (memcmp(a->as_security_key_ranstar, b->as_security_key_ranstar, 32) != 0) {
    PRINT_ERROR("XnAP Equality failed: as_security_key_ranstar differs\n");
    return false;
  }

  _EQ_CHECK_LONG(a->as_security_ncc, b->as_security_ncc);
  _EQ_CHECK_LONG(a->ue_ambr.br_ul, b->ue_ambr.br_ul);
  _EQ_CHECK_LONG(a->ue_ambr.br_dl, b->ue_ambr.br_dl);

  if (!eq_byte_array(&a->rrc_context, &b->rrc_context))
    return false;

  _EQ_CHECK_INT(a->num_pdu, b->num_pdu);

  for (int i = 0; i < a->num_pdu; i++) {
    if (!eq_xnap_pdusession_resources_tobe_setup_item(&a->pdusession_resources_tobe_setup_list[i],
                                                      &b->pdusession_resources_tobe_setup_list[i]))
      return false;
  }

  return true;
}

static bool eq_xnap_ue_history_info(const ue_history_info_t *a, const ue_history_info_t *b)
{
  _EQ_CHECK_INT(a->xnap_cell_type, b->xnap_cell_type);

  if (!eq_byte_array(&a->last_visited_cell_info, &b->last_visited_cell_info))
    return false;

  return true;
}

/**
 * @brief XnAP Handover Request Equality Funciton
 */
bool eq_xnap_handover_request(const xnap_handover_req_t *a, const xnap_handover_req_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);

  if (!eq_xnap_cause(&a->cause, &b->cause))
    return false;

  if (!eq_xnap_ngran_cgi(&a->target_cgi, &b->target_cgi))
    return false;

  if (!eq_nr_guami(&a->guami, &b->guami))
    return false;

  if (!eq_xnap_ue_context_info(&a->ue_context, &b->ue_context))
    return false;

  _EQ_CHECK_INT(a->num_last_visited_cells, b->num_last_visited_cells);

  for (int i = 0; i < a->num_last_visited_cells; i++) {
    if (!eq_xnap_ue_history_info(&a->ue_history_info[i], &b->ue_history_info[i]))
      return false;
  }

  return true;
}

static void free_xnap_qos_flow_param(xnap_qos_flow_param_t *p)
{
  // nothing to free
  UNUSED(p);
}

/**
 * @brief Free QoS flows to be setup list
 */
static void free_xnap_qos_tobe_setup_list(xnap_qos_flow_tobe_setup_item_t *qos_list, uint8_t num_qos)
{
  DevAssert(qos_list && num_qos > 0);

  for (int i = 0; i < num_qos; i++) {
    free_xnap_qos_flow_param(&qos_list[i].qos_params);
  }
  free(qos_list);
}

/**
 * @brief Free PDU session resources to be setup list
 */
static void free_xnap_pdusession_resources_tobe_setup_list(xnap_pdusession_resources_tobe_setup_item_t *pdu_list, uint16_t num_pdu)
{
  DevAssert(pdu_list && num_pdu > 0);

  for (int i = 0; i < num_pdu; i++) {
    free(pdu_list[i].nssai);
    free_xnap_qos_tobe_setup_list(pdu_list[i].qos_list, pdu_list[i].num_qos);
  }
  free(pdu_list);
}

/**
 * @brief Free UE context information
 */
static void free_xnap_ue_context_info(const xnap_ue_context_info_t *ue_context)
{
  DevAssert(ue_context);

  // Free RRC context buffer
  free_byte_array(ue_context->rrc_context);

  // Free PDU session resources
  free_xnap_pdusession_resources_tobe_setup_list(ue_context->pdusession_resources_tobe_setup_list, ue_context->num_pdu);
}

/**
 * @brief Free UE history information list
 */
static void free_xnap_ue_history_info_list(ue_history_info_t *history_list, uint8_t num_cells)
{
  DevAssert(history_list && num_cells > 0);

  for (int i = 0; i < num_cells; i++) {
    free_byte_array(history_list[i].last_visited_cell_info);
  }

  free(history_list);
}

/**
 * @brief XnAP Handover Request memory management
 */
void free_xnap_handover_request(xnap_handover_req_t *msg)
{
  DevAssert(msg != NULL);

  // Free UE context information
  free_xnap_ue_context_info(&msg->ue_context);

  // Free UE history information
  free_xnap_ue_history_info_list(msg->ue_history_info, msg->num_last_visited_cells);
}

/**
 * @brief XnAP Handover Request Acknowledge encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_handover_request_acknowledge(const xnap_handover_req_ack_t *ack)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type: Successful Outcome */
  pdu->present = XNAP_XnAP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, succMsg);
  succMsg->procedureCode = XNAP_ProcedureCode_id_handoverPreparation;
  succMsg->criticality = XNAP_Criticality_reject;
  succMsg->value.present = XNAP_SuccessfulOutcome__value_PR_HandoverRequestAcknowledge;

  XNAP_HandoverRequestAcknowledge_t *out = &succMsg->value.choice.HandoverRequestAcknowledge;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequestAcknowledge_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_ignore;
  ie1->value.present = XNAP_HandoverRequestAcknowledge_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = ack->s_ng_node_ue_xnap_id;

  /* Target NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequestAcknowledge_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_HandoverRequestAcknowledge_IEs__value_PR_NG_RANnodeUEXnAPID_1;
  ie2->value.choice.NG_RANnodeUEXnAPID_1 = ack->t_ng_node_ue_xnap_id;

  /* PDU Session Resources Admitted List (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequestAcknowledge_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_PDUSessionResourcesAdmitted_List;
  ie3->criticality = XNAP_Criticality_ignore;
  ie3->value.present = XNAP_HandoverRequestAcknowledge_IEs__value_PR_PDUSessionResourcesAdmitted_List;

  for (int i = 0; i < ack->num_pdu_admitted; i++) {
    const xnap_pdusession_admitted_item_t *pdu = &ack->pdusession_admitted_list[i];
    asn1cSequenceAdd(ie3->value.choice.PDUSessionResourcesAdmitted_List.list, XNAP_PDUSessionResourcesAdmitted_Item_t, pduItem);

    /* PDU Session ID */
    pduItem->pduSessionId = pdu->pdusession_id;

    /* QoS Flows Admitted List */
    for (int j = 0; j < pdu->num_qos; j++) {
      const xnap_qos_admitted_item_t *qos = &pdu->qos_list[j];
      asn1cSequenceAdd(pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List.list, XNAP_QoSFlowsAdmitted_Item_t, qosItem);

      /* QoS Flow Identifier */
      qosItem->qfi = qos->qfi;
    }
  }

  /* Target NG-RAN node To Source NG-RAN node Transparent Container (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverRequestAcknowledge_IEs_t, ie4);
  ie4->id = XNAP_ProtocolIE_ID_id_Target2SourceNG_RANnodeTranspContainer;
  ie4->criticality = XNAP_Criticality_ignore;
  ie4->value.present = XNAP_HandoverRequestAcknowledge_IEs__value_PR_OCTET_STRING;
  OCTET_STRING_fromBuf(&ie4->value.choice.OCTET_STRING, (const char *)ack->target2source.buf, ack->target2source.len);

  return pdu;
}

/**
 * @brief XnAP Handover Request Acknowledge Decoding
 */
bool decode_xnap_handover_request_acknowledge(xnap_handover_req_ack_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_successfulOutcome);
  AssertError(pdu->choice.successfulOutcome != NULL, return false, "successfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.successfulOutcome->procedureCode, XNAP_ProcedureCode_id_handoverPreparation);
  _EQ_CHECK_INT(pdu->choice.successfulOutcome->value.present, XNAP_SuccessfulOutcome__value_PR_HandoverRequestAcknowledge);

  XNAP_HandoverRequestAcknowledge_t *in = &pdu->choice.successfulOutcome->value.choice.HandoverRequestAcknowledge;
  XNAP_HandoverRequestAcknowledge_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_HandoverRequestAcknowledge_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID,
                   true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequestAcknowledge_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID,
                   true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequestAcknowledge_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_PDUSessionResourcesAdmitted_List,
                   true);
  XNAP_LIB_FIND_IE(XNAP_HandoverRequestAcknowledge_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_Target2SourceNG_RANnodeTranspContainer,
                   true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);

    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequestAcknowledge_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequestAcknowledge_IEs__value_PR_NG_RANnodeUEXnAPID_1);
        out->t_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID_1;
      } break;

      case XNAP_ProtocolIE_ID_id_PDUSessionResourcesAdmitted_List: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequestAcknowledge_IEs__value_PR_PDUSessionResourcesAdmitted_List);

        XNAP_PDUSessionResourcesAdmitted_List_t *pduList = &ie->value.choice.PDUSessionResourcesAdmitted_List;
        out->num_pdu_admitted = pduList->list.count;

        if (out->num_pdu_admitted > 0) {
          out->pdusession_admitted_list = calloc_or_fail(out->num_pdu_admitted, sizeof(*out->pdusession_admitted_list));

          for (int j = 0; j < out->num_pdu_admitted; j++) {
            XNAP_PDUSessionResourcesAdmitted_Item_t *pduItem = pduList->list.array[j];
            xnap_pdusession_admitted_item_t *dst = &out->pdusession_admitted_list[j];

            /* PDU Session ID */
            dst->pdusession_id = pduItem->pduSessionId;

            /* QoS Flows Admitted List */
            dst->num_qos = pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List.list.count;
            if (dst->num_qos > 0) {
              dst->qos_list = calloc_or_fail(dst->num_qos, sizeof(*dst->qos_list));

              for (int k = 0; k < dst->num_qos; k++) {
                XNAP_QoSFlowsAdmitted_Item_t *qosItem = pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List.list.array[k];
                dst->qos_list[k].qfi = qosItem->qfi;
              }
            }
          }
        }
      } break;

      case XNAP_ProtocolIE_ID_id_Target2SourceNG_RANnodeTranspContainer: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverRequestAcknowledge_IEs__value_PR_OCTET_STRING);
        OCTET_STRING_t *container = &ie->value.choice.OCTET_STRING;
        out->target2source = create_byte_array(container->size, container->buf);
      } break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief Compare two QoS Flows Admitted items for equality
 */
static bool eq_xnap_qos_admitted_item(const xnap_qos_admitted_item_t *a, const xnap_qos_admitted_item_t *b)
{
  _EQ_CHECK_INT(a->qfi, b->qfi);
  return true;
}

/**
 * @brief Compare two PDU Session Resources Admitted items for equality
 */
static bool eq_xnap_pdusession_admitted_item(const xnap_pdusession_admitted_item_t *a, const xnap_pdusession_admitted_item_t *b)
{
  _EQ_CHECK_INT(a->pdusession_id, b->pdusession_id);

  _EQ_CHECK_INT(a->num_qos, b->num_qos);
  for (int i = 0; i < a->num_qos; i++) {
    if (!eq_xnap_qos_admitted_item(&a->qos_list[i], &b->qos_list[i]))
      return false;
  }

  return true;
}

/**
 * @brief XnAP Handover Request Acknowledge equality function
 **/
bool eq_xnap_handover_request_acknowledge(const xnap_handover_req_ack_t *a, const xnap_handover_req_ack_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);
  _EQ_CHECK_UINT32(a->t_ng_node_ue_xnap_id, b->t_ng_node_ue_xnap_id);

  _EQ_CHECK_INT(a->num_pdu_admitted, b->num_pdu_admitted);
  for (int i = 0; i < a->num_pdu_admitted; i++) {
    if (!eq_xnap_pdusession_admitted_item(&a->pdusession_admitted_list[i], &b->pdusession_admitted_list[i]))
      return false;
  }

  if (!eq_byte_array(&a->target2source, &b->target2source))
    return false;

  return true;
}

/**
 * @brief Free QoS Flows Admitted list
 */
static void free_xnap_qos_admitted_list(xnap_qos_admitted_item_t *qos_list)
{
  DevAssert(qos_list);

  /* QoS admitted items have no dynamically allocated members */
  free(qos_list);
}

/**
 * @brief Free PDU Session Resources Admitted list
 */
static void free_xnap_pdusession_admitted_list(xnap_pdusession_admitted_item_t *pdu_list, uint8_t num_pdu)
{
  DevAssert(pdu_list && num_pdu > 0);

  for (int i = 0; i < num_pdu; i++) {
    free_xnap_qos_admitted_list(pdu_list[i].qos_list);
  }
  free(pdu_list);
}

/**
 * @brief XnAP Handover Request Acknowledge memory management
 */
void free_xnap_handover_request_acknowledge(xnap_handover_req_ack_t *msg)
{
  if (!msg)
    return;

  /* Free PDU Session Resources Admitted List */
  free_xnap_pdusession_admitted_list(msg->pdusession_admitted_list, msg->num_pdu_admitted);

  /* Free Target to Source transparent container */
  free_byte_array(msg->target2source);
}

/**
 * @brief XnAP Handover Preparation Failure encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_handover_preparation_failure(const xnap_handover_preparation_failure_t *failure)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type: Unsuccessful Outcome */
  pdu->present = XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, unsuccMsg);
  unsuccMsg->procedureCode = XNAP_ProcedureCode_id_handoverPreparation;
  unsuccMsg->criticality = XNAP_Criticality_reject;
  unsuccMsg->value.present = XNAP_UnsuccessfulOutcome__value_PR_HandoverPreparationFailure;

  XNAP_HandoverPreparationFailure_t *out = &unsuccMsg->value.choice.HandoverPreparationFailure;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverPreparationFailure_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_ignore;
  ie1->value.present = XNAP_HandoverPreparationFailure_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = failure->s_ng_node_ue_xnap_id;

  /* Cause (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverPreparationFailure_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_Cause;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_HandoverPreparationFailure_IEs__value_PR_Cause;
  xnap_gNB_set_cause(&ie2->value.choice.Cause, &failure->cause);

  return pdu;
}

/**
 * @brief XnAP Handover Preparation Failure decoding
 */
bool decode_xnap_handover_preparation_failure(xnap_handover_preparation_failure_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_unsuccessfulOutcome);
  AssertError(pdu->choice.unsuccessfulOutcome != NULL, return false, "unsuccessfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.unsuccessfulOutcome->procedureCode, XNAP_ProcedureCode_id_handoverPreparation);
  _EQ_CHECK_INT(pdu->choice.unsuccessfulOutcome->value.present, XNAP_UnsuccessfulOutcome__value_PR_HandoverPreparationFailure);

  XNAP_HandoverPreparationFailure_t *in = &pdu->choice.unsuccessfulOutcome->value.choice.HandoverPreparationFailure;
  XNAP_HandoverPreparationFailure_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_HandoverPreparationFailure_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID,
                   true);
  XNAP_LIB_FIND_IE(XNAP_HandoverPreparationFailure_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_Cause, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverPreparationFailure_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_Cause: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverPreparationFailure_IEs__value_PR_Cause);
        out->cause = decode_xnap_cause(&ie->value.choice.Cause);
      } break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Handover Preparation Failure equality function
 */
bool eq_xnap_handover_preparation_failure(const xnap_handover_preparation_failure_t *a,
                                          const xnap_handover_preparation_failure_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);

  if (!eq_xnap_cause(&a->cause, &b->cause))
    return false;

  return true;
}

/**
 * @brief XnAP Handover Preparation Failure memory management
 */
void free_xnap_handover_preparation_failure(xnap_handover_preparation_failure_t *msg)
{
  // nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP SN Status Transfer encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_sn_status_transfer(const xnap_sn_status_transfer_t *sn_status)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type */
  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_sNStatusTransfer;
  initMsg->criticality = XNAP_Criticality_ignore;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_SNStatusTransfer;

  XNAP_SNStatusTransfer_t *out = &initMsg->value.choice.SNStatusTransfer;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_SNStatusTransfer_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_SNStatusTransfer_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = sn_status->s_ng_node_ue_xnap_id;

  /* Target NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_SNStatusTransfer_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_SNStatusTransfer_IEs__value_PR_NG_RANnodeUEXnAPID_1;
  ie2->value.choice.NG_RANnodeUEXnAPID_1 = sn_status->t_ng_node_ue_xnap_id;

  /* DRBs Subject To Status Transfer List (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_SNStatusTransfer_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_DRBsSubjectToStatusTransfer_List;
  ie3->criticality = XNAP_Criticality_ignore;
  ie3->value.present = XNAP_SNStatusTransfer_IEs__value_PR_DRBsSubjectToStatusTransfer_List;

  XNAP_DRBsSubjectToStatusTransfer_List_t *container = &ie3->value.choice.DRBsSubjectToStatusTransfer_List;

  for (int i = 0; i < sn_status->ran_status.nb_drb; i++) {
    const xnap_drb_status_t *s = &sn_status->ran_status.drb_status_list[i];
    asn1cSequenceAdd(container->list, XNAP_DRBsSubjectToStatusTransfer_Item_t, drb_item);

    /* DRB ID */
    drb_item->drbID = s->drb_id;

    /* UL COUNT */
    drb_item->pdcpStatusTransfer_UL = xnap_encode_pdcp_count(&s->ul_count);

    /* DL COUNT */
    drb_item->pdcpStatusTransfer_DL = xnap_encode_pdcp_count(&s->dl_count);
  }

  return pdu;
}

/**
 * @brief XnAP SN Status Transfer decoding
 */
bool decode_xnap_sn_status_transfer(xnap_sn_status_transfer_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_sNStatusTransfer);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_SNStatusTransfer);

  XNAP_SNStatusTransfer_t *in = &pdu->choice.initiatingMessage->value.choice.SNStatusTransfer;
  XNAP_SNStatusTransfer_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_SNStatusTransfer_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_SNStatusTransfer_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_SNStatusTransfer_IEs_t,
                   ie,
                   &in->protocolIEs.list,
                   XNAP_ProtocolIE_ID_id_DRBsSubjectToStatusTransfer_List,
                   true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_SNStatusTransfer_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_SNStatusTransfer_IEs__value_PR_NG_RANnodeUEXnAPID_1);
        out->t_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID_1;
      } break;

      case XNAP_ProtocolIE_ID_id_DRBsSubjectToStatusTransfer_List: {
        _EQ_CHECK_INT(ie->value.present, XNAP_SNStatusTransfer_IEs__value_PR_DRBsSubjectToStatusTransfer_List);

        XNAP_DRBsSubjectToStatusTransfer_List_t *drb_list = &ie->value.choice.DRBsSubjectToStatusTransfer_List;
        out->ran_status.nb_drb = drb_list->list.count;

        for (int j = 0; j < out->ran_status.nb_drb; j++) {
          XNAP_DRBsSubjectToStatusTransfer_Item_t *item = drb_list->list.array[j];
          xnap_drb_status_t *dst = &out->ran_status.drb_status_list[j];

          /* DRB ID */
          dst->drb_id = item->drbID;

          /* UL COUNT */
	  if (!decode_xnap_pdcp_count(&item->pdcpStatusTransfer_UL, &dst->ul_count))
 	    return false;

          /* DL COUNT */
          if (!decode_xnap_pdcp_count(&item->pdcpStatusTransfer_DL, &dst->dl_count))
            return false;
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
 * @brief Compare two DRB count values for equality
 */
static bool eq_xnap_drb_count_value(const xnap_drb_count_value_t *a, const xnap_drb_count_value_t *b)
{
  _EQ_CHECK_UINT32(a->pdcp_sn, b->pdcp_sn);
  _EQ_CHECK_UINT32(a->hfn, b->hfn);
  _EQ_CHECK_INT(a->sn_len, b->sn_len);
  return true;
}

/**
 * @brief Compare two DRB status items for equality
 */
static bool eq_xnap_drb_status(const xnap_drb_status_t *a, const xnap_drb_status_t *b)
{
  _EQ_CHECK_INT(a->drb_id, b->drb_id);
  if (!eq_xnap_drb_count_value(&a->ul_count, &b->ul_count))
    return false;
  if (!eq_xnap_drb_count_value(&a->dl_count, &b->dl_count))
    return false;

  return true;
}

/**
 * @brief Compare two RAN status containers for equality
 */
static bool eq_xnap_ran_status_container(const xnap_ran_status_container_t *a, const xnap_ran_status_container_t *b)
{
  _EQ_CHECK_INT(a->nb_drb, b->nb_drb);

  for (int i = 0; i < a->nb_drb; i++) {
    if (!eq_xnap_drb_status(&a->drb_status_list[i], &b->drb_status_list[i]))
      return false;
  }

  return true;
}

/**
 * @brief XnAP SN Status Transfer equality function
 */
bool eq_xnap_sn_status_transfer(const xnap_sn_status_transfer_t *a, const xnap_sn_status_transfer_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);
  _EQ_CHECK_UINT32(a->t_ng_node_ue_xnap_id, b->t_ng_node_ue_xnap_id);
  if (!eq_xnap_ran_status_container(&a->ran_status, &b->ran_status))
    return false;

  return true;
}

/**
 * @brief XnAP SN Status Transfer memory management
 */
void free_xnap_sn_status_transfer(xnap_sn_status_transfer_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP UE Context Release encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_ue_context_release(const xnap_ue_context_release_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message type */
  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_uEContextRelease;
  initMsg->criticality = XNAP_Criticality_reject;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_UEContextRelease;

  XNAP_UEContextRelease_t *out = &initMsg->value.choice.UEContextRelease;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_UEContextRelease_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_UEContextRelease_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->s_ng_node_ue_xnap_id;

  /* Target NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_UEContextRelease_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_UEContextRelease_IEs__value_PR_NG_RANnodeUEXnAPID_1;
  ie2->value.choice.NG_RANnodeUEXnAPID_1 = msg->t_ng_node_ue_xnap_id;

  return pdu;
}

/**
 * @brief XnAP UE Context Release decoding
 */
bool decode_xnap_ue_context_release(xnap_ue_context_release_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  /* Check message type */
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_uEContextRelease);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_UEContextRelease);

  XNAP_UEContextRelease_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextRelease;
  XNAP_UEContextRelease_IEs_t *ie;

  /* Check presence of mandatory IEs */
  XNAP_LIB_FIND_IE(XNAP_UEContextRelease_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_UEContextRelease_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID, true);

  /* Loop over all IEs */
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_UEContextRelease_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_UEContextRelease_IEs__value_PR_NG_RANnodeUEXnAPID_1);
        out->t_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID_1;
      } break;

      default:
        AssertError(0, return false, "Unknown XnAP IE id %ld\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP UE Context Release equality function
 */
bool eq_xnap_ue_context_release(const xnap_ue_context_release_t *a, const xnap_ue_context_release_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);
  _EQ_CHECK_UINT32(a->t_ng_node_ue_xnap_id, b->t_ng_node_ue_xnap_id);

  return true;
}

/**
 * @brief XnAP UE Context Release memory management
 */
void free_xnap_ue_context_release(xnap_ue_context_release_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP Handover Cancel encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_handover_cancel(const xnap_handover_cancel_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_handoverCancel;
  initMsg->criticality = XNAP_Criticality_ignore;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_HandoverCancel;

  XNAP_HandoverCancel_t *out = &initMsg->value.choice.HandoverCancel;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverCancel_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_HandoverCancel_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->s_ng_node_ue_xnap_id;

  /* Cause (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverCancel_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_Cause;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_HandoverCancel_IEs__value_PR_Cause;
  xnap_gNB_set_cause(&ie2->value.choice.Cause, &msg->cause);

  return pdu;
}

/**
 * @brief XnAP Handover Cancel decoding
 */
bool decode_xnap_handover_cancel(xnap_handover_cancel_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_handoverCancel);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_HandoverCancel);

  XNAP_HandoverCancel_t *in = &pdu->choice.initiatingMessage->value.choice.HandoverCancel;
  XNAP_HandoverCancel_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_HandoverCancel_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverCancel_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverCancel_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_Cause: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverCancel_IEs__value_PR_Cause);
        out->cause = decode_xnap_cause(&ie->value.choice.Cause);
      } break;

      case XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;

      case XNAP_ProtocolIE_ID_id_targetCellsToCancel:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Handover Cancel equality function
 */
bool eq_xnap_handover_cancel(const xnap_handover_cancel_t *a, const xnap_handover_cancel_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);
  if (!eq_xnap_cause(&a->cause, &b->cause))
    return false;
  return true;
}

/**
 * @brief XnAP Handover Cancel memory management
 */
void free_xnap_handover_cancel(xnap_handover_cancel_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP Handover Success encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_handover_success(const xnap_handover_success_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_handoverSuccess;
  initMsg->criticality = XNAP_Criticality_ignore;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_HandoverSuccess;

  XNAP_HandoverSuccess_t *out = &initMsg->value.choice.HandoverSuccess;

  /* Source NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverSuccess_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_HandoverSuccess_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->s_ng_node_ue_xnap_id;

  /* Target NG-RAN node UE XnAP ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverSuccess_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_HandoverSuccess_IEs__value_PR_NG_RANnodeUEXnAPID_1;
  ie2->value.choice.NG_RANnodeUEXnAPID_1 = msg->t_ng_node_ue_xnap_id;

  /* Requested Target Cell Global ID (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_HandoverSuccess_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_requestedTargetCellGlobalID;
  ie3->criticality = XNAP_Criticality_reject;
  ie3->value.present = XNAP_HandoverSuccess_IEs__value_PR_Target_CGI;
  ie3->value.choice.Target_CGI = xnap_encode_target_cgi(&msg->target_cgi);

  return pdu;
}

/**
 * @brief XnAP Handover Success decoding
 */
bool decode_xnap_handover_success(xnap_handover_success_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_handoverSuccess);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_HandoverSuccess);

  XNAP_HandoverSuccess_t *in = &pdu->choice.initiatingMessage->value.choice.HandoverSuccess;
  XNAP_HandoverSuccess_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_HandoverSuccess_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverSuccess_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_HandoverSuccess_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_requestedTargetCellGlobalID, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_sourceNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverSuccess_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->s_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_targetNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverSuccess_IEs__value_PR_NG_RANnodeUEXnAPID_1);
        out->t_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID_1;
      } break;

      case XNAP_ProtocolIE_ID_id_requestedTargetCellGlobalID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_HandoverSuccess_IEs__value_PR_Target_CGI);
        if (!xnap_decode_target_cgi(&ie->value.choice.Target_CGI, &out->target_cgi))
          return false;
      } break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Handover Success equality function
 */
bool eq_xnap_handover_success(const xnap_handover_success_t *a, const xnap_handover_success_t *b)
{
  _EQ_CHECK_UINT32(a->s_ng_node_ue_xnap_id, b->s_ng_node_ue_xnap_id);
  _EQ_CHECK_UINT32(a->t_ng_node_ue_xnap_id, b->t_ng_node_ue_xnap_id);
  if (!eq_xnap_ngran_cgi(&a->target_cgi, &b->target_cgi))
    return false;
  return true;
}

/**
 * @brief XnAP Handover Success memory management
 */
void free_xnap_handover_success(xnap_handover_success_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP RAN Paging encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_ran_paging(const xnap_ran_paging_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_rANPaging;
  initMsg->criticality = XNAP_Criticality_reject;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_RANPaging;

  XNAP_RANPaging_t *out = &initMsg->value.choice.RANPaging;

  /* UE Identity Index Value (M) – 10-bit BIT STRING */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_UEIdentityIndexValue;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_RANPaging_IEs__value_PR_UEIdentityIndexValue;
  XNAP_UEIdentityIndexValue_t *ue_idx = &ie1->value.choice.UEIdentityIndexValue;
  ue_idx->present = XNAP_UEIdentityIndexValue_PR_indexLength10;
  BIT_STRING_t *idx_bs = &ue_idx->choice.indexLength10;
  UEIDENTITYINDEX_TO_BIT_STRING(msg->ue_identity_index_value, idx_bs);

  /* UE RAN Paging Identity (M) – I-RNTI, 40-bit BIT STRING */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_UERANPagingIdentity;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_RANPaging_IEs__value_PR_UERANPagingIdentity;
  XNAP_UERANPagingIdentity_t *ue_paging_id = &ie2->value.choice.UERANPagingIdentity;
  ue_paging_id->present = XNAP_UERANPagingIdentity_PR_i_RNTI_full;
  BIT_STRING_t *rnti_bs = &ue_paging_id->choice.i_RNTI_full;
  IRNTI_TO_BIT_STRING(msg->ue_ran_paging_identity, rnti_bs);

  /* Paging DRX (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_PagingDRX;
  ie3->criticality = XNAP_Criticality_ignore;
  ie3->value.present = XNAP_RANPaging_IEs__value_PR_PagingDRX;
  ie3->value.choice.PagingDRX = msg->paging_drx;

  /* RAN Paging Area (M) */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie4);
  ie4->id = XNAP_ProtocolIE_ID_id_RANPagingArea;
  ie4->criticality = XNAP_Criticality_reject;
  ie4->value.present = XNAP_RANPaging_IEs__value_PR_RANPagingArea;
  XNAP_RANPagingArea_t *paging_area = &ie4->value.choice.RANPagingArea;
  MCC_MNC_TO_PLMNID(msg->ran_paging_area.plmn.mcc,
                    msg->ran_paging_area.plmn.mnc,
                    msg->ran_paging_area.plmn.mnc_digit_length,
                    &paging_area->pLMN_Identity);
  if (msg->ran_paging_area.choice == XNAP_RAN_PAGING_AREA_CELL_LIST) {
    paging_area->rANPagingAreaChoice.present = XNAP_RANPagingAreaChoice_PR_cell_List;
    asn1cCalloc(paging_area->rANPagingAreaChoice.choice.cell_List, cell_list);
    for (int i = 0; i < msg->ran_paging_area.num_cells; i++) {
      asn1cSequenceAdd(cell_list->list, XNAP_NG_RAN_Cell_Identity_t, cell_item);
      cell_item->present = XNAP_NG_RAN_Cell_Identity_PR_nr;
      NR_CELL_ID_TO_BIT_STRING(msg->ran_paging_area.cell_ids[i], &cell_item->choice.nr);
    }
  } else {
    paging_area->rANPagingAreaChoice.present = XNAP_RANPagingAreaChoice_PR_rANAreaID_List;
    asn1cCalloc(paging_area->rANPagingAreaChoice.choice.rANAreaID_List, ran_area_list);
    for (int i = 0; i < msg->ran_paging_area.num_ran_area_ids; i++) {
      const xnap_ran_area_id_t *src = &msg->ran_paging_area.ran_area_ids[i];
      asn1cSequenceAdd(ran_area_list->list, XNAP_RANAreaID_t, ran_area_item);
      INT24_TO_OCTET_STRING(src->tac, &ran_area_item->tAC);
      if (src->ranac_present) {
        ran_area_item->rANAC = calloc_or_fail(1, sizeof(*ran_area_item->rANAC));
        *ran_area_item->rANAC = src->ranac;
      }
    }
  }

  /* Paging Priority (O) */
  if (msg->paging_priority) {
    asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie5);
    ie5->id = XNAP_ProtocolIE_ID_id_PagingPriority;
    ie5->criticality = XNAP_Criticality_ignore;
    ie5->value.present = XNAP_RANPaging_IEs__value_PR_PagingPriority;
    ie5->value.choice.PagingPriority = *msg->paging_priority;
  }

  /* Assistance Data for RAN Paging (O) – RAN Paging Attempt Information */
  if (msg->ran_paging_attempt_info) {
    const xnap_ran_paging_attempt_info_t *info = msg->ran_paging_attempt_info;
    asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie6);
    ie6->id = XNAP_ProtocolIE_ID_id_AssistanceDataForRANPaging;
    ie6->criticality = XNAP_Criticality_ignore;
    ie6->value.present = XNAP_RANPaging_IEs__value_PR_AssistanceDataForRANPaging;
    asn1cCalloc(ie6->value.choice.AssistanceDataForRANPaging.ran_paging_attempt_info, attempt);
    attempt->pagingAttemptCount = info->paging_attempt_count;
    attempt->intendedNumberOfPagingAttempts = info->intended_num_paging_attempts;
    if (info->next_paging_area_scope_changed)
      asn1cCallocOne(attempt->nextPagingAreaScope, XNAP_RANPagingAttemptInfo__nextPagingAreaScope_changed);
  }

  /* UE Radio Capability for Paging (O) */
  if (msg->ue_radio_cap_for_paging.buf && msg->ue_radio_cap_for_paging.len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, XNAP_RANPaging_IEs_t, ie7);
    ie7->id = XNAP_ProtocolIE_ID_id_UERadioCapabilityForPaging;
    ie7->criticality = XNAP_Criticality_ignore;
    ie7->value.present = XNAP_RANPaging_IEs__value_PR_UERadioCapabilityForPaging;
    const byte_array_t *cap = &msg->ue_radio_cap_for_paging;
    asn1cCalloc(ie7->value.choice.UERadioCapabilityForPaging.uERadioCapabilityForPagingOfNR, nr);
    OCTET_STRING_fromBuf(nr, (const char *)cap->buf, cap->len);
  }

  return pdu;
}

/**
 * @brief XnAP RAN Paging decoding
 */
bool decode_xnap_ran_paging(xnap_ran_paging_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_rANPaging);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_RANPaging);

  XNAP_RANPaging_t *in = &pdu->choice.initiatingMessage->value.choice.RANPaging;
  XNAP_RANPaging_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_RANPaging_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UEIdentityIndexValue, true);
  XNAP_LIB_FIND_IE(XNAP_RANPaging_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UERANPagingIdentity, true);
  XNAP_LIB_FIND_IE(XNAP_RANPaging_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_PagingDRX, true);
  XNAP_LIB_FIND_IE(XNAP_RANPaging_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_RANPagingArea, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_UEIdentityIndexValue: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_UEIdentityIndexValue);
        const XNAP_UEIdentityIndexValue_t *ue_idx = &ie->value.choice.UEIdentityIndexValue;
        if (ue_idx->present != XNAP_UEIdentityIndexValue_PR_indexLength10)
          return false;
        out->ue_identity_index_value = BIT_STRING_to_uint16(&ue_idx->choice.indexLength10);
      } break;

      case XNAP_ProtocolIE_ID_id_UERANPagingIdentity: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_UERANPagingIdentity);
        const XNAP_UERANPagingIdentity_t *ue_paging_id = &ie->value.choice.UERANPagingIdentity;
        if (ue_paging_id->present != XNAP_UERANPagingIdentity_PR_i_RNTI_full)
          return false;
        out->ue_ran_paging_identity = BIT_STRING_to_uint64(&ue_paging_id->choice.i_RNTI_full);
      } break;

      case XNAP_ProtocolIE_ID_id_PagingDRX: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_PagingDRX);
        out->paging_drx = ie->value.choice.PagingDRX;
      } break;

      case XNAP_ProtocolIE_ID_id_RANPagingArea: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_RANPagingArea);
        const XNAP_RANPagingArea_t *paging_area = &ie->value.choice.RANPagingArea;
        PLMNID_TO_MCC_MNC(&paging_area->pLMN_Identity,
                          out->ran_paging_area.plmn.mcc,
                          out->ran_paging_area.plmn.mnc,
                          out->ran_paging_area.plmn.mnc_digit_length);
        switch (paging_area->rANPagingAreaChoice.present) {
          case XNAP_RANPagingAreaChoice_PR_cell_List: {
            out->ran_paging_area.choice = XNAP_RAN_PAGING_AREA_CELL_LIST;
            const XNAP_NG_RAN_Cell_Identity_ListinRANPagingArea_t *cell_list = paging_area->rANPagingAreaChoice.choice.cell_List;
            out->ran_paging_area.num_cells = cell_list->list.count;
            out->ran_paging_area.cell_ids = calloc_or_fail(out->ran_paging_area.num_cells, sizeof(uint64_t));
            for (int j = 0; j < cell_list->list.count; j++) {
              const XNAP_NG_RAN_Cell_Identity_t *cell = cell_list->list.array[j];
              if (cell->present != XNAP_NG_RAN_Cell_Identity_PR_nr)
                return false;
              BIT_STRING_TO_NR_CELL_IDENTITY(&cell->choice.nr, out->ran_paging_area.cell_ids[j]);
            }
          } break;
          case XNAP_RANPagingAreaChoice_PR_rANAreaID_List: {
            out->ran_paging_area.choice = XNAP_RAN_PAGING_AREA_RAN_AREA_ID;
            const XNAP_RANAreaID_List_t *ran_area_list = paging_area->rANPagingAreaChoice.choice.rANAreaID_List;
            out->ran_paging_area.num_ran_area_ids = ran_area_list->list.count;
            out->ran_paging_area.ran_area_ids = calloc_or_fail(out->ran_paging_area.num_ran_area_ids, sizeof(xnap_ran_area_id_t));
            for (int j = 0; j < ran_area_list->list.count; j++) {
              const XNAP_RANAreaID_t *ran_area = ran_area_list->list.array[j];
              xnap_ran_area_id_t *dst = &out->ran_paging_area.ran_area_ids[j];
              OCTET_STRING_TO_INT24(&ran_area->tAC, dst->tac);
              if (ran_area->rANAC) {
                dst->ranac_present = true;
                dst->ranac = *ran_area->rANAC;
              }
            }
          } break;
          default:
            AssertError(0, return false, "Unknown RANPagingAreaChoice.present=%d\n", paging_area->rANPagingAreaChoice.present);
        }
      } break;

      case XNAP_ProtocolIE_ID_id_PagingPriority: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_PagingPriority);
        out->paging_priority = malloc_or_fail(sizeof(*out->paging_priority));
        *out->paging_priority = ie->value.choice.PagingPriority;
      } break;

      case XNAP_ProtocolIE_ID_id_AssistanceDataForRANPaging: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_AssistanceDataForRANPaging);
        const XNAP_RANPagingAttemptInfo_t *attempt = ie->value.choice.AssistanceDataForRANPaging.ran_paging_attempt_info;
        if (attempt) {
          xnap_ran_paging_attempt_info_t *info = calloc_or_fail(1, sizeof(*info));
          out->ran_paging_attempt_info = info;
          info->paging_attempt_count = attempt->pagingAttemptCount;
          info->intended_num_paging_attempts = attempt->intendedNumberOfPagingAttempts;
          info->next_paging_area_scope_changed =
              attempt->nextPagingAreaScope
              && *attempt->nextPagingAreaScope == XNAP_RANPagingAttemptInfo__nextPagingAreaScope_changed;
        }
      } break;

      case XNAP_ProtocolIE_ID_id_UERadioCapabilityForPaging: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RANPaging_IEs__value_PR_UERadioCapabilityForPaging);
        const OCTET_STRING_t *nr = ie->value.choice.UERadioCapabilityForPaging.uERadioCapabilityForPagingOfNR;
        if (nr)
          out->ue_radio_cap_for_paging = create_byte_array(nr->size, nr->buf);
      } break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP RAN Paging equality function
 */
bool eq_xnap_ran_paging(const xnap_ran_paging_t *a, const xnap_ran_paging_t *b)
{
  _EQ_CHECK_INT(a->ue_identity_index_value, b->ue_identity_index_value);
  _EQ_CHECK_UINT64(a->ue_ran_paging_identity, b->ue_ran_paging_identity);
  _EQ_CHECK_INT(a->paging_drx, b->paging_drx);

  if (!eq_xnap_plmn(&a->ran_paging_area.plmn, &b->ran_paging_area.plmn))
    return false;
  _EQ_CHECK_INT(a->ran_paging_area.choice, b->ran_paging_area.choice);
  if (a->ran_paging_area.choice == XNAP_RAN_PAGING_AREA_CELL_LIST) {
    _EQ_CHECK_INT(a->ran_paging_area.num_cells, b->ran_paging_area.num_cells);
    for (int i = 0; i < a->ran_paging_area.num_cells; i++)
      _EQ_CHECK_UINT64(a->ran_paging_area.cell_ids[i], b->ran_paging_area.cell_ids[i]);
  } else {
    _EQ_CHECK_INT(a->ran_paging_area.num_ran_area_ids, b->ran_paging_area.num_ran_area_ids);
    for (int i = 0; i < a->ran_paging_area.num_ran_area_ids; i++) {
      _EQ_CHECK_UINT32(a->ran_paging_area.ran_area_ids[i].tac, b->ran_paging_area.ran_area_ids[i].tac);
      _EQ_CHECK_INT(a->ran_paging_area.ran_area_ids[i].ranac_present, b->ran_paging_area.ran_area_ids[i].ranac_present);
      if (a->ran_paging_area.ran_area_ids[i].ranac_present)
        _EQ_CHECK_INT(a->ran_paging_area.ran_area_ids[i].ranac, b->ran_paging_area.ran_area_ids[i].ranac);
    }
  }

  _EQ_CHECK_OPTIONAL_IE(a, b, paging_priority, _EQ_CHECK_INT);

  _EQ_CHECK_OPTIONAL_PTR(a, b, ran_paging_attempt_info);
  if (a->ran_paging_attempt_info) {
    const xnap_ran_paging_attempt_info_t *ia = a->ran_paging_attempt_info;
    const xnap_ran_paging_attempt_info_t *ib = b->ran_paging_attempt_info;
    _EQ_CHECK_INT(ia->paging_attempt_count, ib->paging_attempt_count);
    _EQ_CHECK_INT(ia->intended_num_paging_attempts, ib->intended_num_paging_attempts);
    _EQ_CHECK_INT(ia->next_paging_area_scope_changed, ib->next_paging_area_scope_changed);
  }

  if ((a->ue_radio_cap_for_paging.len || b->ue_radio_cap_for_paging.len)
      && !eq_byte_array(&a->ue_radio_cap_for_paging, &b->ue_radio_cap_for_paging))
    return false;

  return true;
}

/**
 * @brief XnAP RAN Paging memory management
 */
void free_xnap_ran_paging(xnap_ran_paging_t *msg)
{
  if (msg->ran_paging_area.choice == XNAP_RAN_PAGING_AREA_CELL_LIST)
    free(msg->ran_paging_area.cell_ids);
  else
    free(msg->ran_paging_area.ran_area_ids);
  free(msg->paging_priority);
  free(msg->ran_paging_attempt_info);
  free_byte_array(msg->ue_radio_cap_for_paging);
}

/* ======================================================================
 * 3GPP TS 38.423 – Retrieve UE Context procedure (id-retrieveUEContext)
 *   9.1.1.8  RETRIEVE UE CONTEXT REQUEST  (initiating message)
 *   9.1.1.9  RETRIEVE UE CONTEXT RESPONSE (successful outcome)
 *   9.1.1.10 RETRIEVE UE CONTEXT FAILURE  (unsuccessful outcome)
 * Only mandatory IEs are implemented.
 * ====================================================================== */

/**
 * @brief XnAP UE Context ID (9.2.3.40) encoding
 */
static XNAP_UEContextID_t xnap_encode_ue_context_id(const xnap_ue_context_id_t *in)
{
  DevAssert(in != NULL);

  XNAP_UEContextID_t out = {0};
  if (in->choice == XNAP_UE_CONTEXT_ID_RRC_RESUME) {
    out.present = XNAP_UEContextID_PR_rRCResume;
    asn1cCalloc(out.choice.rRCResume, resume);
    if (in->rrc_resume.i_rnti_type == XNAP_I_RNTI_SHORT) {
      resume->i_rnti.present = XNAP_I_RNTI_PR_i_RNTI_short;
      INT24_TO_BIT_STRING(in->rrc_resume.i_rnti, &resume->i_rnti.choice.i_RNTI_short);
    } else {
      resume->i_rnti.present = XNAP_I_RNTI_PR_i_RNTI_full;
      IRNTI_TO_BIT_STRING(in->rrc_resume.i_rnti, &resume->i_rnti.choice.i_RNTI_full);
    }
    INT16_TO_BIT_STRING(in->rrc_resume.allocated_c_rnti, &resume->allocated_c_rnti);
    /* Only the NR branch of NG-RAN-CellPCI is supported (E-UTRA is not). */
    resume->accessPCI.present = XNAP_NG_RAN_CellPCI_PR_nr;
    resume->accessPCI.choice.nr = in->rrc_resume.access_pci;
  } else if (in->choice == XNAP_UE_CONTEXT_ID_RRC_REESTABLISHMENT) {
    out.present = XNAP_UEContextID_PR_rRRCReestablishment;
    asn1cCalloc(out.choice.rRRCReestablishment, reest);
    INT16_TO_BIT_STRING(in->rrc_reest.c_rnti, &reest->c_rnti);
    /* Only the NR branch of NG-RAN-CellPCI is supported (E-UTRA is not). */
    reest->failureCellPCI.present = XNAP_NG_RAN_CellPCI_PR_nr;
    reest->failureCellPCI.choice.nr = in->rrc_reest.failure_cell_pci;
  } else {
    AssertError(0, return out, "Unknown UE context ID choice %d\n", in->choice);
  }

  return out;
}

/**
 * @brief XnAP UE Context ID (9.2.3.40) decoding
 */
static bool decode_xnap_ue_context_id(const XNAP_UEContextID_t *in, xnap_ue_context_id_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  switch (in->present) {
    case XNAP_UEContextID_PR_rRCResume: {
      const XNAP_UEContextIDforRRCResume_t *resume = in->choice.rRCResume;
      DevAssert(resume != NULL);
      out->choice = XNAP_UE_CONTEXT_ID_RRC_RESUME;
      switch (resume->i_rnti.present) {
        case XNAP_I_RNTI_PR_i_RNTI_full:
          out->rrc_resume.i_rnti_type = XNAP_I_RNTI_FULL;
          out->rrc_resume.i_rnti = BIT_STRING_to_uint64(&resume->i_rnti.choice.i_RNTI_full);
          break;
        case XNAP_I_RNTI_PR_i_RNTI_short:
          out->rrc_resume.i_rnti_type = XNAP_I_RNTI_SHORT;
          out->rrc_resume.i_rnti = BIT_STRING_to_uint64(&resume->i_rnti.choice.i_RNTI_short);
          break;
        default:
          PRINT_ERROR("Unsupported I-RNTI choice %d\n", resume->i_rnti.present);
          return false;
      }
      out->rrc_resume.allocated_c_rnti = BIT_STRING_to_uint16(&resume->allocated_c_rnti);
      if (resume->accessPCI.present != XNAP_NG_RAN_CellPCI_PR_nr) {
        PRINT_ERROR("Unsupported NG-RAN-CellPCI choice %d (only NR is supported, E-UTRA is not)\n", resume->accessPCI.present);
        return false;
      }
      out->rrc_resume.access_pci = resume->accessPCI.choice.nr;
    } break;

    case XNAP_UEContextID_PR_rRRCReestablishment: {
      const XNAP_UEContextIDforRRCReestablishment_t *reest = in->choice.rRRCReestablishment;
      DevAssert(reest != NULL);
      out->choice = XNAP_UE_CONTEXT_ID_RRC_REESTABLISHMENT;
      out->rrc_reest.c_rnti = BIT_STRING_to_uint16(&reest->c_rnti);
      if (reest->failureCellPCI.present != XNAP_NG_RAN_CellPCI_PR_nr) {
        PRINT_ERROR("Unsupported NG-RAN-CellPCI choice %d (only NR is supported, E-UTRA is not)\n",
                    reest->failureCellPCI.present);
        return false;
      }
      out->rrc_reest.failure_cell_pci = reest->failureCellPCI.choice.nr;
    } break;

    default:
      PRINT_ERROR("Unknown UEContextID.present=%d\n", in->present);
      return false;
  }

  return true;
}

static bool eq_xnap_ue_context_id(const xnap_ue_context_id_t *a, const xnap_ue_context_id_t *b)
{
  _EQ_CHECK_INT(a->choice, b->choice);
  if (a->choice == XNAP_UE_CONTEXT_ID_RRC_RESUME) {
    _EQ_CHECK_INT(a->rrc_resume.i_rnti_type, b->rrc_resume.i_rnti_type);
    _EQ_CHECK_UINT64(a->rrc_resume.i_rnti, b->rrc_resume.i_rnti);
    _EQ_CHECK_INT(a->rrc_resume.allocated_c_rnti, b->rrc_resume.allocated_c_rnti);
    _EQ_CHECK_INT(a->rrc_resume.access_pci, b->rrc_resume.access_pci);
  } else {
    _EQ_CHECK_INT(a->rrc_reest.c_rnti, b->rrc_reest.c_rnti);
    _EQ_CHECK_INT(a->rrc_reest.failure_cell_pci, b->rrc_reest.failure_cell_pci);
  }
  return true;
}

/**
 * @brief XnAP Retrieve UE Context Request (9.1.1.8) encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_retrieve_ue_context_request(const xnap_retrieve_ue_context_request_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = XNAP_ProcedureCode_id_retrieveUEContext;
  initMsg->criticality = XNAP_Criticality_reject;
  initMsg->value.present = XNAP_InitiatingMessage__value_PR_RetrieveUEContextRequest;

  XNAP_RetrieveUEContextRequest_t *out = &initMsg->value.choice.RetrieveUEContextRequest;

  /* New NG-RAN node UE XnAP ID (M) 9.2.3.16 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextRequest_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_reject;
  ie1->value.present = XNAP_RetrieveUEContextRequest_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->new_ng_node_ue_xnap_id;

  /* UE Context ID (M) 9.2.3.40 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextRequest_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_UEContextID;
  ie2->criticality = XNAP_Criticality_reject;
  ie2->value.present = XNAP_RetrieveUEContextRequest_IEs__value_PR_UEContextID;
  ie2->value.choice.UEContextID = xnap_encode_ue_context_id(&msg->ue_context_id);

  /* MAC-I (M) – 16-bit integrity code */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextRequest_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_MAC_I;
  ie3->criticality = XNAP_Criticality_reject;
  ie3->value.present = XNAP_RetrieveUEContextRequest_IEs__value_PR_MAC_I;
  INT16_TO_BIT_STRING(msg->integrity_protection, &ie3->value.choice.MAC_I);

  /* New NG-RAN Cell Identity (M) 9.2.2.9 – NR branch only */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextRequest_IEs_t, ie4);
  ie4->id = XNAP_ProtocolIE_ID_id_new_NG_RAN_Cell_Identity;
  ie4->criticality = XNAP_Criticality_reject;
  ie4->value.present = XNAP_RetrieveUEContextRequest_IEs__value_PR_NG_RAN_Cell_Identity;
  ie4->value.choice.NG_RAN_Cell_Identity.present = XNAP_NG_RAN_Cell_Identity_PR_nr;
  NR_CELL_ID_TO_BIT_STRING(msg->new_cell_id, &ie4->value.choice.NG_RAN_Cell_Identity.choice.nr);

  return pdu;
}

/**
 * @brief XnAP Retrieve UE Context Request (9.1.1.8) decoding
 */
bool decode_xnap_retrieve_ue_context_request(xnap_retrieve_ue_context_request_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_initiatingMessage);
  AssertError(pdu->choice.initiatingMessage != NULL, return false, "initiatingMessage is NULL");
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, XNAP_ProcedureCode_id_retrieveUEContext);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, XNAP_InitiatingMessage__value_PR_RetrieveUEContextRequest);

  XNAP_RetrieveUEContextRequest_t *in = &pdu->choice.initiatingMessage->value.choice.RetrieveUEContextRequest;
  XNAP_RetrieveUEContextRequest_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UEContextID, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_MAC_I, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextRequest_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_new_NG_RAN_Cell_Identity, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextRequest_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->new_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_UEContextID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextRequest_IEs__value_PR_UEContextID);
        if (!decode_xnap_ue_context_id(&ie->value.choice.UEContextID, &out->ue_context_id))
          return false;
      } break;

      case XNAP_ProtocolIE_ID_id_MAC_I: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextRequest_IEs__value_PR_MAC_I);
        out->integrity_protection = BIT_STRING_to_uint16(&ie->value.choice.MAC_I);
      } break;

      case XNAP_ProtocolIE_ID_id_new_NG_RAN_Cell_Identity: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextRequest_IEs__value_PR_NG_RAN_Cell_Identity);
        const XNAP_NG_RAN_Cell_Identity_t *cell = &ie->value.choice.NG_RAN_Cell_Identity;
        /* New NG-RAN Cell Identity – NR branch only */
        if (cell->present != XNAP_NG_RAN_Cell_Identity_PR_nr) {
          PRINT_ERROR("Unsupported NG-RAN-Cell-Identity choice %d (only NR is supported, E-UTRA is not)\n", cell->present);
          return false;
        }
        BIT_STRING_TO_NR_CELL_IDENTITY(&cell->choice.nr, out->new_cell_id);
      } break;

      case XNAP_ProtocolIE_ID_id_RRCResumeCause:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Retrieve UE Context Request (9.1.1.8) equality function
 */
bool eq_xnap_retrieve_ue_context_request(const xnap_retrieve_ue_context_request_t *a, const xnap_retrieve_ue_context_request_t *b)
{
  _EQ_CHECK_UINT32(a->new_ng_node_ue_xnap_id, b->new_ng_node_ue_xnap_id);
  if (!eq_xnap_ue_context_id(&a->ue_context_id, &b->ue_context_id))
    return false;
  _EQ_CHECK_INT(a->integrity_protection, b->integrity_protection);
  _EQ_CHECK_UINT64(a->new_cell_id, b->new_cell_id);
  return true;
}

/**
 * @brief XnAP Retrieve UE Context Request (9.1.1.8) memory management
 */
void free_xnap_retrieve_ue_context_request(xnap_retrieve_ue_context_request_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

/**
 * @brief XnAP UE Context Information (Retrieve UE Context Response) encoding.
 *        Only the mandatory sub-IEs are handled; the container shares its
 *        layout with the Handover Request UE Context Information.
 */
static void xnap_encode_ue_ctx_info_retr(const xnap_ue_context_info_t *in, XNAP_UEContextInfoRetrUECtxtResp_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  /* NG-C UE associated Signalling reference (M) 9.2.3.26 */
  asn_uint642INTEGER(&out->ng_c_UE_signalling_ref, in->ngc_ue_sig_ref);

  /* Signalling TNL association address at source NG-C side (M) 9.2.3.31 */
  out->signalling_TNL_at_source.present = XNAP_CPTransportLayerInformation_PR_endpointIPAddress;
  uint32_t cp_tnl_addr = 0;
  memcpy(&cp_tnl_addr, in->cp_tnl_ip_source.buffer, sizeof(cp_tnl_addr));
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(cp_tnl_addr, &out->signalling_TNL_at_source.choice.endpointIPAddress);

  /* UE Security Capabilities (M) 9.2.3.49 */
  out->ueSecurityCapabilities = xnap_encode_security_capabilities(&in->security_capabilities);

  /* AS Security Information (M) 9.2.3.50 */
  AS_KEY_STAR_TO_BIT_STRING(in->as_security_key_ranstar, &out->securityInformation.key_NG_RAN_Star);
  out->securityInformation.ncc = in->as_security_ncc;

  /* UE Aggregate Maximum Bit Rate (M) 9.2.3.17 */
  UEAGMAXBITRTD_TO_ASN_PRIMITIVES(in->ue_ambr.br_dl, &out->ue_AMBR.dl_UE_AMBR);
  UEAGMAXBITRTU_TO_ASN_PRIMITIVES(in->ue_ambr.br_ul, &out->ue_AMBR.ul_UE_AMBR);

  /* PDU Session Resources To Be Setup List (M) 9.2.1.1 */
  xnap_encode_pdusession_resources_to_be_setup_list(in->pdusession_resources_tobe_setup_list,
                                                    in->num_pdu,
                                                    &out->pduSessionResourcesToBeSetup_List);

  /* RRC Context (M) TS 38.331 11.2.2 if the old and new serving NG-RAN nodes are gNBs */
  OCTET_STRING_fromBuf(&out->rrc_Context, (const char *)in->rrc_context.buf, in->rrc_context.len);
}

/**
 * @brief XnAP UE Context Information (Retrieve UE Context Response) decoding.
 */
static bool decode_xnap_ue_ctx_info_retr(const XNAP_UEContextInfoRetrUECtxtResp_t *in, xnap_ue_context_info_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  asn_INTEGER2uint64(&in->ng_c_UE_signalling_ref, &out->ngc_ue_sig_ref);

  if (in->signalling_TNL_at_source.present != XNAP_CPTransportLayerInformation_PR_endpointIPAddress) {
    PRINT_ERROR("Unsupported CPTransportLayerInformation choice %d\n", in->signalling_TNL_at_source.present);
    return false;
  }
  uint32_t cp_tnl_addr = 0;
  BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&in->signalling_TNL_at_source.choice.endpointIPAddress, cp_tnl_addr);
  memcpy(out->cp_tnl_ip_source.buffer, &cp_tnl_addr, sizeof(cp_tnl_addr));
  out->cp_tnl_ip_source.length = 4;

  if (!decode_xnap_ue_security_capabilities(&in->ueSecurityCapabilities, &out->security_capabilities))
    return false;

  memcpy(out->as_security_key_ranstar, in->securityInformation.key_NG_RAN_Star.buf, 32);
  out->as_security_ncc = in->securityInformation.ncc;

  asn_INTEGER2ulong(&in->ue_AMBR.ul_UE_AMBR, &out->ue_ambr.br_ul);
  asn_INTEGER2ulong(&in->ue_AMBR.dl_UE_AMBR, &out->ue_ambr.br_dl);

  out->rrc_context = create_byte_array(in->rrc_Context.size, in->rrc_Context.buf);

  out->num_pdu = in->pduSessionResourcesToBeSetup_List.list.count;
  if (!decode_xnap_pdusession_resources_to_be_setup_list(&in->pduSessionResourcesToBeSetup_List,
                                                         &out->pdusession_resources_tobe_setup_list))
    return false;

  return true;
}

/**
 * @brief XnAP Retrieve UE Context Response (9.1.1.9) encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_retrieve_ue_context_response(const xnap_retrieve_ue_context_response_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, succMsg);
  succMsg->procedureCode = XNAP_ProcedureCode_id_retrieveUEContext;
  succMsg->criticality = XNAP_Criticality_reject;
  succMsg->value.present = XNAP_SuccessfulOutcome__value_PR_RetrieveUEContextResponse;

  XNAP_RetrieveUEContextResponse_t *out = &succMsg->value.choice.RetrieveUEContextResponse;

  /* New NG-RAN node UE XnAP ID (M) 9.2.3.16 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextResponse_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_ignore;
  ie1->value.present = XNAP_RetrieveUEContextResponse_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->new_ng_node_ue_xnap_id;

  /* Old NG-RAN node UE XnAP ID (M) 9.2.3.16 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextResponse_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_oldNG_RANnodeUEXnAPID;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_RetrieveUEContextResponse_IEs__value_PR_NG_RANnodeUEXnAPID_1;
  ie2->value.choice.NG_RANnodeUEXnAPID_1 = msg->old_ng_node_ue_xnap_id;

  /* GUAMI (M) 9.2.3.24 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextResponse_IEs_t, ie3);
  ie3->id = XNAP_ProtocolIE_ID_id_GUAMI;
  ie3->criticality = XNAP_Criticality_reject;
  ie3->value.present = XNAP_RetrieveUEContextResponse_IEs__value_PR_GUAMI;
  MCC_MNC_TO_PLMNID(msg->guami.plmn.mcc, msg->guami.plmn.mnc, msg->guami.plmn.mnc_digit_length, &ie3->value.choice.GUAMI.plmn_ID);
  AMF_REGION_TO_BIT_STRING(msg->guami.amf_region_id, &ie3->value.choice.GUAMI.amf_region_id);
  AMF_SETID_TO_BIT_STRING(msg->guami.amf_set_id, &ie3->value.choice.GUAMI.amf_set_id);
  AMF_POINTER_TO_BIT_STRING(msg->guami.amf_pointer, &ie3->value.choice.GUAMI.amf_pointer);

  /* UE Context Information – Retrieve UE Context Response (M) 9.2.1.13 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextResponse_IEs_t, ie4);
  ie4->id = XNAP_ProtocolIE_ID_id_UEContextInfoRetrUECtxtResp;
  ie4->criticality = XNAP_Criticality_reject;
  ie4->value.present = XNAP_RetrieveUEContextResponse_IEs__value_PR_UEContextInfoRetrUECtxtResp;
  xnap_encode_ue_ctx_info_retr(&msg->ue_context, &ie4->value.choice.UEContextInfoRetrUECtxtResp);

  return pdu;
}

/**
 * @brief XnAP Retrieve UE Context Response (9.1.1.9) decoding
 */
bool decode_xnap_retrieve_ue_context_response(xnap_retrieve_ue_context_response_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_successfulOutcome);
  AssertError(pdu->choice.successfulOutcome != NULL, return false, "successfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.successfulOutcome->procedureCode, XNAP_ProcedureCode_id_retrieveUEContext);
  _EQ_CHECK_INT(pdu->choice.successfulOutcome->value.present, XNAP_SuccessfulOutcome__value_PR_RetrieveUEContextResponse);

  XNAP_RetrieveUEContextResponse_t *in = &pdu->choice.successfulOutcome->value.choice.RetrieveUEContextResponse;
  XNAP_RetrieveUEContextResponse_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_oldNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_GUAMI, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextResponse_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_UEContextInfoRetrUECtxtResp, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextResponse_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->new_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_oldNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextResponse_IEs__value_PR_NG_RANnodeUEXnAPID_1);
        out->old_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID_1;
      } break;

      case XNAP_ProtocolIE_ID_id_GUAMI: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextResponse_IEs__value_PR_GUAMI);
        const XNAP_GUAMI_t *guami = &ie->value.choice.GUAMI;
        plmn_id_t *plmn = &out->guami.plmn;
        PLMNID_TO_MCC_MNC(&guami->plmn_ID, plmn->mcc, plmn->mnc, plmn->mnc_digit_length);
        out->guami.amf_region_id = BIT_STRING_to_uint8(&guami->amf_region_id);
        out->guami.amf_set_id = BIT_STRING_to_uint16(&guami->amf_set_id);
        out->guami.amf_pointer = BIT_STRING_to_uint8(&guami->amf_pointer);
      } break;

      case XNAP_ProtocolIE_ID_id_UEContextInfoRetrUECtxtResp: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextResponse_IEs__value_PR_UEContextInfoRetrUECtxtResp);
        if (!decode_xnap_ue_ctx_info_retr(&ie->value.choice.UEContextInfoRetrUECtxtResp, &out->ue_context))
          return false;
      } break;

      case XNAP_ProtocolIE_ID_id_TraceActivation:
      case XNAP_ProtocolIE_ID_id_MaskedIMEISV:
      case XNAP_ProtocolIE_ID_id_LocationReportingInformation:
      case XNAP_ProtocolIE_ID_id_CriticalityDiagnostics:
      case XNAP_ProtocolIE_ID_id_NRV2XServicesAuthorized:
      case XNAP_ProtocolIE_ID_id_LTEV2XServicesAuthorized:
      case XNAP_ProtocolIE_ID_id_PC5QoSParameters:
      case XNAP_ProtocolIE_ID_id_UEHistoryInformation:
      case XNAP_ProtocolIE_ID_id_UEHistoryInformationFromTheUE:
      case XNAP_ProtocolIE_ID_id_MDTPLMNList:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Retrieve UE Context Response (9.1.1.9) equality function
 */
bool eq_xnap_retrieve_ue_context_response(const xnap_retrieve_ue_context_response_t *a, const xnap_retrieve_ue_context_response_t *b)
{
  _EQ_CHECK_UINT32(a->new_ng_node_ue_xnap_id, b->new_ng_node_ue_xnap_id);
  _EQ_CHECK_UINT32(a->old_ng_node_ue_xnap_id, b->old_ng_node_ue_xnap_id);
  if (!eq_nr_guami(&a->guami, &b->guami))
    return false;
  if (!eq_xnap_ue_context_info(&a->ue_context, &b->ue_context))
    return false;
  return true;
}

/**
 * @brief XnAP Retrieve UE Context Response (9.1.1.9) memory management
 */
void free_xnap_retrieve_ue_context_response(xnap_retrieve_ue_context_response_t *msg)
{
  free_xnap_ue_context_info(&msg->ue_context);
}

/**
 * @brief XnAP Retrieve UE Context Failure (9.1.1.10) encoding
 */
XNAP_XnAP_PDU_t *encode_xnap_retrieve_ue_context_failure(const xnap_retrieve_ue_context_failure_t *msg)
{
  XNAP_XnAP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, unsuccMsg);
  unsuccMsg->procedureCode = XNAP_ProcedureCode_id_retrieveUEContext;
  unsuccMsg->criticality = XNAP_Criticality_reject;
  unsuccMsg->value.present = XNAP_UnsuccessfulOutcome__value_PR_RetrieveUEContextFailure;

  XNAP_RetrieveUEContextFailure_t *out = &unsuccMsg->value.choice.RetrieveUEContextFailure;

  /* New NG-RAN node UE XnAP ID (M) 9.2.3.16 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextFailure_IEs_t, ie1);
  ie1->id = XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID;
  ie1->criticality = XNAP_Criticality_ignore;
  ie1->value.present = XNAP_RetrieveUEContextFailure_IEs__value_PR_NG_RANnodeUEXnAPID;
  ie1->value.choice.NG_RANnodeUEXnAPID = msg->new_ng_node_ue_xnap_id;

  /* Old NG-RAN node To New NG-RAN node Resume Container (O) */
  if (msg->old_to_new_resume_container.buf && msg->old_to_new_resume_container.len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextFailure_IEs_t, ie_rc);
    ie_rc->id = XNAP_ProtocolIE_ID_id_OldtoNewNG_RANnodeResumeContainer;
    ie_rc->criticality = XNAP_Criticality_ignore;
    ie_rc->value.present = XNAP_RetrieveUEContextFailure_IEs__value_PR_OCTET_STRING;
    OCTET_STRING_fromBuf(&ie_rc->value.choice.OCTET_STRING,
                         (const char *)msg->old_to_new_resume_container.buf,
                         msg->old_to_new_resume_container.len);
  }

  /* Cause (M) 9.2.3.2 */
  asn1cSequenceAdd(out->protocolIEs.list, XNAP_RetrieveUEContextFailure_IEs_t, ie2);
  ie2->id = XNAP_ProtocolIE_ID_id_Cause;
  ie2->criticality = XNAP_Criticality_ignore;
  ie2->value.present = XNAP_RetrieveUEContextFailure_IEs__value_PR_Cause;
  xnap_gNB_set_cause(&ie2->value.choice.Cause, &msg->cause);

  return pdu;
}

/**
 * @brief XnAP Retrieve UE Context Failure (9.1.1.10) decoding
 */
bool decode_xnap_retrieve_ue_context_failure(xnap_retrieve_ue_context_failure_t *out, const XNAP_XnAP_PDU_t *pdu)
{
  _EQ_CHECK_INT(pdu->present, XNAP_XnAP_PDU_PR_unsuccessfulOutcome);
  AssertError(pdu->choice.unsuccessfulOutcome != NULL, return false, "unsuccessfulOutcome is NULL");
  _EQ_CHECK_LONG(pdu->choice.unsuccessfulOutcome->procedureCode, XNAP_ProcedureCode_id_retrieveUEContext);
  _EQ_CHECK_INT(pdu->choice.unsuccessfulOutcome->value.present, XNAP_UnsuccessfulOutcome__value_PR_RetrieveUEContextFailure);

  XNAP_RetrieveUEContextFailure_t *in = &pdu->choice.unsuccessfulOutcome->value.choice.RetrieveUEContextFailure;
  XNAP_RetrieveUEContextFailure_IEs_t *ie;

  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextFailure_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID, true);
  XNAP_LIB_FIND_IE(XNAP_RetrieveUEContextFailure_IEs_t, ie, &in->protocolIEs.list, XNAP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    DevAssert(in->protocolIEs.list.array[i]);
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case XNAP_ProtocolIE_ID_id_newNG_RANnodeUEXnAPID: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextFailure_IEs__value_PR_NG_RANnodeUEXnAPID);
        out->new_ng_node_ue_xnap_id = ie->value.choice.NG_RANnodeUEXnAPID;
      } break;

      case XNAP_ProtocolIE_ID_id_Cause: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextFailure_IEs__value_PR_Cause);
        out->cause = decode_xnap_cause(&ie->value.choice.Cause);
      } break;

      case XNAP_ProtocolIE_ID_id_OldtoNewNG_RANnodeResumeContainer: {
        _EQ_CHECK_INT(ie->value.present, XNAP_RetrieveUEContextFailure_IEs__value_PR_OCTET_STRING);
        OCTET_STRING_t *container = &ie->value.choice.OCTET_STRING;
        out->old_to_new_resume_container = create_byte_array(container->size, container->buf);
      } break;

      case XNAP_ProtocolIE_ID_id_CriticalityDiagnostics:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld not handled, skipping\n", ie->id);
        break;

      default:
        PRINT_ERROR("XNAP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief XnAP Retrieve UE Context Failure (9.1.1.10) equality function
 */
bool eq_xnap_retrieve_ue_context_failure(const xnap_retrieve_ue_context_failure_t *a, const xnap_retrieve_ue_context_failure_t *b)
{
  _EQ_CHECK_UINT32(a->new_ng_node_ue_xnap_id, b->new_ng_node_ue_xnap_id);
  if ((a->old_to_new_resume_container.len || b->old_to_new_resume_container.len)
      && !eq_byte_array(&a->old_to_new_resume_container, &b->old_to_new_resume_container))
    return false;
  if (!eq_xnap_cause(&a->cause, &b->cause))
    return false;
  return true;
}

/**
 * @brief XnAP Retrieve UE Context Failure (9.1.1.10) memory management
 */
void free_xnap_retrieve_ue_context_failure(xnap_retrieve_ue_context_failure_t *msg)
{
  free_byte_array(msg->old_to_new_resume_container);
}
