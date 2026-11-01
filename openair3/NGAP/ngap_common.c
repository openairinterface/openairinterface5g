/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap procedures for both gNB
 */

#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "aper_encoder.h"
#include "assertions.h"
#include "conversions.h"
#include "ngap_common.h"
#include "ngap_msg_includes.h"

void tnl_to_bitstring(BIT_STRING_t *out, const transport_layer_addr_t in)
{
  if (in.length) {
    out->buf = malloc_or_fail(in.length);
    memcpy(out->buf, in.buffer, in.length);
    out->size = in.length;
    out->bits_unused = 0;
  }
}

void bitstring_to_tnl(transport_layer_addr_t *out, const BIT_STRING_t in)
{
  if (in.size) {
    memcpy(out->buffer, in.buf, in.size);
    out->length = in.size;
  }
}

void encode_ngap_cause(NGAP_Cause_t *out, const ngap_cause_t *in)
{
  switch (in->type) {
    case NGAP_CAUSE_RADIO_NETWORK:
      out->present = NGAP_Cause_PR_radioNetwork;
      out->choice.radioNetwork = in->value;
      break;

    case NGAP_CAUSE_TRANSPORT:
      out->present = NGAP_Cause_PR_transport;
      out->choice.transport = in->value;
      break;

    case NGAP_CAUSE_NAS:
      out->present = NGAP_Cause_PR_nas;
      out->choice.nas = in->value;
      break;

    case NGAP_CAUSE_PROTOCOL:
      out->present = NGAP_Cause_PR_protocol;
      out->choice.protocol = in->value;
      break;

    case NGAP_CAUSE_MISC:
      out->present = NGAP_Cause_PR_misc;
      out->choice.misc = in->value;
      break;

    case NGAP_CAUSE_NOTHING:
    default:
      AssertFatal(false, "Unknown failure cause %d\n", in->type);
      break;
  }
}

ngap_cause_t decode_ngap_cause(const NGAP_Cause_t *in)
{
  ngap_cause_t out = {0};
  switch (in->present) {
    case NGAP_Cause_PR_radioNetwork:
      out.type = NGAP_CAUSE_RADIO_NETWORK;
      out.value = in->choice.radioNetwork;
      break;

    case NGAP_Cause_PR_transport:
      out.type = NGAP_CAUSE_TRANSPORT;
      out.value = in->choice.transport;
      break;

    case NGAP_Cause_PR_nas:
      out.type = NGAP_CAUSE_NAS;
      out.value = in->choice.nas;
      break;

    case NGAP_Cause_PR_protocol:
      out.type = NGAP_CAUSE_PROTOCOL;
      out.value = in->choice.protocol;
      break;

    case NGAP_Cause_PR_misc:
      out.type = NGAP_CAUSE_MISC;
      out.value = in->choice.misc;
      break;

    default:
      out.type = NGAP_CAUSE_NOTHING;
      NGAP_ERROR("Unknown failure cause %d\n", in->present);
      break;
  }
  return out;
}

nr_guami_t decode_ngap_guami(const NGAP_GUAMI_t *in)
{
  nr_guami_t out = {0};
  TBCD_TO_MCC_MNC(&in->pLMNIdentity, out.plmn.mcc, out.plmn.mnc, out.plmn.mnc_digit_length);
  out.amf_region_id = BIT_STRING_to_uint8(&in->aMFRegionID);
  out.amf_set_id = BIT_STRING_to_uint16(&in->aMFSetID);
  out.amf_pointer = BIT_STRING_to_uint8(&in->aMFPointer);
  return out;
}

ngap_ambr_t decode_ngap_UEAggregateMaximumBitRate(const NGAP_UEAggregateMaximumBitRate_t *in)
{
  ngap_ambr_t ambr = {0};
  asn_INTEGER2ulong(&in->uEAggregateMaximumBitRateUL, &ambr.br_ul);
  asn_INTEGER2ulong(&in->uEAggregateMaximumBitRateDL, &ambr.br_dl);
  return ambr;
}

nssai_t decode_ngap_nssai(const NGAP_S_NSSAI_t *in)
{
  nssai_t nssai = {0};
  OCTET_STRING_TO_INT8(&in->sST, nssai.sst);
  if (in->sD != NULL) {
    BUFFER_TO_INT24(in->sD->buf, nssai.sd);
  } else {
    nssai.sd = 0xffffff;
  }
  return nssai;
}

void encode_ngap_security_capabilities(NGAP_UESecurityCapabilities_t *out, const ngap_security_capabilities_t *in)
{
  ENCRALG_TO_BIT_STRING(in->nRencryption_algorithms, &out->nRencryptionAlgorithms);
  ENCRALG_TO_BIT_STRING(in->eUTRAencryption_algorithms, &out->eUTRAencryptionAlgorithms);
  INTPROTALG_TO_BIT_STRING(in->nRintegrity_algorithms, &out->nRintegrityProtectionAlgorithms);
  INTPROTALG_TO_BIT_STRING(in->eUTRAintegrity_algorithms, &out->eUTRAintegrityProtectionAlgorithms);
}

ngap_security_capabilities_t decode_ngap_security_capabilities(const NGAP_UESecurityCapabilities_t *in)
{
  ngap_security_capabilities_t out = {0};
  out.nRencryption_algorithms = BIT_STRING_to_uint16(&in->nRencryptionAlgorithms);
  out.nRintegrity_algorithms = BIT_STRING_to_uint16(&in->nRintegrityProtectionAlgorithms);
  out.eUTRAencryption_algorithms = BIT_STRING_to_uint16(&in->eUTRAencryptionAlgorithms);
  out.eUTRAintegrity_algorithms = BIT_STRING_to_uint16(&in->eUTRAintegrityProtectionAlgorithms);
  return out;
}

ngap_mobility_restriction_t decode_ngap_mobility_restriction(const NGAP_MobilityRestrictionList_t *in)
{
  ngap_mobility_restriction_t out = {0};
  TBCD_TO_MCC_MNC(&in->servingPLMN, out.serving_plmn.mcc, out.serving_plmn.mnc, out.serving_plmn.mnc_digit_length);
  return out;
}

void encode_ngap_target_id(NGAP_HandoverRequiredIEs_t *out, const target_ran_node_id_t *in)
{
  out->id = NGAP_ProtocolIE_ID_id_TargetID;
  out->criticality = NGAP_Criticality_reject;
  out->value.present = NGAP_HandoverRequiredIEs__value_PR_TargetID;
  // CHOICE Target ID: NG-RAN (M)
  out->value.choice.TargetID.present = NGAP_TargetID_PR_targetRANNodeID;
  asn1cCalloc(out->value.choice.TargetID.choice.targetRANNodeID, targetRan);
  // Global RAN Node ID (M)
  targetRan->globalRANNodeID.present = NGAP_GlobalRANNodeID_PR_globalGNB_ID;
  asn1cCalloc(targetRan->globalRANNodeID.choice.globalGNB_ID, globalGnbId);
  globalGnbId->gNB_ID.present = NGAP_GNB_ID_PR_gNB_ID;
  MACRO_GNB_ID_TO_BIT_STRING(in->targetgNBId, &globalGnbId->gNB_ID.choice.gNB_ID);
  MCC_MNC_TO_PLMNID(in->plmn_identity.mcc, in->plmn_identity.mnc, in->plmn_identity.mnc_digit_length, &globalGnbId->pLMNIdentity);
  // Selected TAI (M)
  INT24_TO_OCTET_STRING(in->tac, &targetRan->selectedTAI.tAC);
  MCC_MNC_TO_PLMNID(in->plmn_identity.mcc,
                    in->plmn_identity.mnc,
                    in->plmn_identity.mnc_digit_length,
                    &targetRan->selectedTAI.pLMNIdentity);
}

void encode_ngap_nr_cgi(NGAP_NR_CGI_t *out, const plmn_id_t *plmn, const uint32_t cell_id)
{
  out->iE_Extensions = NULL;
  MCC_MNC_TO_PLMNID(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &out->pLMNIdentity);
  NR_CELL_ID_TO_BIT_STRING(cell_id, &out->nRCellIdentity);
}

pdusession_level_qos_parameter_t fill_qos(uint8_t qfi, const NGAP_QosFlowLevelQosParameters_t *params)
{
  pdusession_level_qos_parameter_t out = {0};
  // QFI
  out.qfi = qfi;
  AssertFatal(params != NULL, "QoS parameters are NULL\n");
  // QosCharacteristics
  const NGAP_QosCharacteristics_t *qosChar = &params->qosCharacteristics;
  AssertFatal(qosChar != NULL, "QoS characteristics are NULL\n");
  if (qosChar->present == NGAP_QosCharacteristics_PR_nonDynamic5QI) {
    AssertFatal(qosChar->choice.nonDynamic5QI != NULL, "nonDynamic5QI is NULL\n");
    const NGAP_NonDynamic5QIDescriptor_t *nonDyn = qosChar->choice.nonDynamic5QI;
    non_dynamic_5qi_t *out_non_dyn = &out.qos_characteristics.non_dynamic;
    out.fiveQI_type = NON_DYNAMIC;
    /** For NonDynamic5QI, fiveQI is mandatory and indicates standardized or pre-configured 5QI
     * (5G QoS characteristics are not signaled, they are derived from the 5QI value) */
    out_non_dyn->fiveQI = nonDyn->fiveQI;
    // Extract priorityLevelQos if present (optional for NonDynamic5QI)
    if (nonDyn->priorityLevelQos != NULL) {
      out_non_dyn->qos_priority = calloc_or_fail(1, sizeof(*out_non_dyn->qos_priority));
      *out_non_dyn->qos_priority = *nonDyn->priorityLevelQos;
      DevAssert(*out_non_dyn->qos_priority >= MIN_QOS_PRIORITY_LEVEL && *out_non_dyn->qos_priority <= MAX_QOS_PRIORITY_LEVEL);
    }
  } else if (qosChar->present == NGAP_QosCharacteristics_PR_dynamic5QI) {
    AssertFatal(qosChar->choice.dynamic5QI != NULL, "dynamic5QI is NULL\n");
    const NGAP_Dynamic5QIDescriptor_t *dyn = qosChar->choice.dynamic5QI;
    dynamic_5qi_t *out_dyn = &out.qos_characteristics.dynamic;
    out.fiveQI_type = DYNAMIC;
    /** Extract fiveQI if present (optional for Dynamic5QI)
     * For Dynamic5QI, fiveQI is optional and indicates dynamically assigned 5QI
     * (5G QoS characteristics are signaled as part of the QoS profile) */
    if (dyn->fiveQI != NULL) {
      out_dyn->fiveQI = calloc_or_fail(1, sizeof(*out_dyn->fiveQI));
      *out_dyn->fiveQI = *dyn->fiveQI;
    }
    // Extract priorityLevelQos (mandatory)
    out_dyn->qos_priority = dyn->priorityLevelQos;
    DevAssert(out_dyn->qos_priority >= MIN_QOS_PRIORITY_LEVEL && out_dyn->qos_priority <= MAX_QOS_PRIORITY_LEVEL);
    /** Extract packetDelayBudget (mandatory)
     * Note: Per 3GPP TS 38.413 §9.3.1.18, this IE is ignored if Extended Packet Delay Budget
     * is present in iE_Extensions. Extended Packet Delay Budget parsing is not yet implemented. */
    out_dyn->packet_delay_budget = dyn->packetDelayBudget;
    // Extract packetErrorRate (mandatory)
    out_dyn->per.scalar = dyn->packetErrorRate.pERScalar;
    out_dyn->per.exponent = dyn->packetErrorRate.pERExponent;
  } else {
    AssertFatal(0, "Unsupported QoS Characteristics present value: %d\n", qosChar->present);
  }
  // Allocation and Retention Priority
  const NGAP_AllocationAndRetentionPriority_t *arp = &params->allocationAndRetentionPriority;
  out.arp.priority_level = arp->priorityLevelARP;
  out.arp.pre_emp_capability = arp->pre_emptionCapability;
  out.arp.pre_emp_vulnerability = arp->pre_emptionVulnerability;

  // Extract GBR QoS Flow Information (optional - only for GBR flows)
  if (params->gBR_QosInformation != NULL) {
    const NGAP_GBR_QosInformation_t *gBR = params->gBR_QosInformation;
    out.gbr_qos_flow_information = calloc_or_fail(1, sizeof(*out.gbr_qos_flow_information));
    gbr_qos_flow_information_t *gbr_info = out.gbr_qos_flow_information;

    // Extract bit rates (NGAP_BitRate_t is INTEGER_t, requires conversion function)
    asn_INTEGER2ulong(&gBR->guaranteedFlowBitRateDL, &gbr_info->dl.guaranteedFlowBitRate);
    asn_INTEGER2ulong(&gBR->maximumFlowBitRateDL, &gbr_info->dl.maximumFlowBitRate);
    asn_INTEGER2ulong(&gBR->guaranteedFlowBitRateUL, &gbr_info->ul.guaranteedFlowBitRate);
    asn_INTEGER2ulong(&gBR->maximumFlowBitRateUL, &gbr_info->ul.maximumFlowBitRate);
  }

  return out;
}

static gtpu_tunnel_t decode_TNLInformation(const NGAP_GTPTunnel_t *gTPTunnel_p)
{
  gtpu_tunnel_t out = {0};
  // Transport layer address
  memcpy(out.addr.buffer, gTPTunnel_p->transportLayerAddress.buf, gTPTunnel_p->transportLayerAddress.size);
  out.addr.length = gTPTunnel_p->transportLayerAddress.size - gTPTunnel_p->transportLayerAddress.bits_unused;
  // GTP tunnel endpoint ID
  OCTET_STRING_TO_INT32(&gTPTunnel_p->gTP_TEID, out.teid);
  return out;
}

void *decode_pdusession_transfer(const asn_TYPE_descriptor_t *td, const OCTET_STRING_t buf)
{
  void *decoded = NULL;
  asn_codec_ctx_t ctx = {.max_stack_size = 100 * 1000};
  asn_dec_rval_t rval = aper_decode(&ctx, td, &decoded, buf.buf, buf.size, 0, 0);
  if (rval.code != RC_OK) {
    NGAP_ERROR("Decoding failed for %s\n", td->name);
    return NULL;
  }
  return decoded;
}

/** @brief Decode PDU Session Resource Setup Request Transfer (9.3.4.1 3GPP TS 38.413) */
bool decodePDUSessionResourceSetup(pdusession_transfer_t *out, const OCTET_STRING_t in)
{
  void *decoded = decode_pdusession_transfer(&asn_DEF_NGAP_PDUSessionResourceSetupRequestTransfer, in);
  if (!decoded) {
    LOG_E(NR_RRC, "Failed to decode PDUSessionResourceSetupRequestTransfer\n");
    return false;
  }

  NGAP_PDUSessionResourceSetupRequestTransfer_t *pdusessionTransfer = (NGAP_PDUSessionResourceSetupRequestTransfer_t *)decoded;
  for (int i = 0; i < pdusessionTransfer->protocolIEs.list.count; i++) {
    NGAP_PDUSessionResourceSetupRequestTransferIEs_t *pdusessionTransfer_ies = pdusessionTransfer->protocolIEs.list.array[i];
    switch (pdusessionTransfer_ies->id) {
      // UL NG-U UP TNL Information (Mandatory)
      case NGAP_ProtocolIE_ID_id_UL_NGU_UP_TNLInformation:
        out->n3_incoming = decode_TNLInformation(pdusessionTransfer_ies->value.choice.UPTransportLayerInformation.choice.gTPTunnel);
        break;

      // PDU Session Type (Mandatory)
      case NGAP_ProtocolIE_ID_id_PDUSessionType:
        out->pdu_session_type = pdusessionTransfer_ies->value.choice.PDUSessionType;
        break;

      // QoS Flow Setup Request List (Mandatory)
      case NGAP_ProtocolIE_ID_id_QosFlowSetupRequestList:
        out->nb_qos = pdusessionTransfer_ies->value.choice.QosFlowSetupRequestList.list.count;
        for (int i = 0; i < out->nb_qos; i++) {
          NGAP_QosFlowSetupRequestItem_t *item = pdusessionTransfer_ies->value.choice.QosFlowSetupRequestList.list.array[i];
          out->qos[i] = fill_qos(item->qosFlowIdentifier, &item->qosFlowLevelQosParameters);
        }
        break;

      default:
        LOG_D(NR_RRC, "Unhandled optional IE %ld\n", pdusessionTransfer_ies->id);
    }
  }
  ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceSetupRequestTransfer, pdusessionTransfer);

  return true;
}

/** @brief PDU Session Resource Setup Response Transfer encoding (9.3.4.2 3GPP TS 38.413) */
byte_array_t encode_ngap_pdusession_setup_response_transfer(const pdusession_setup_t *pdusession)
{
  NGAP_PDUSessionResourceSetupResponseTransfer_t pdusessionTransfer = {0};
  byte_array_t out = {0};

  // DL QoS Flow per TNL Information (Mandatory)
  NGAP_QosFlowPerTNLInformation_t *dLQosFlowPerTNLInformation = &pdusessionTransfer.dLQosFlowPerTNLInformation;

  // UP Transport Layer Information (Mandatory)
  dLQosFlowPerTNLInformation->uPTransportLayerInformation.present = NGAP_UPTransportLayerInformation_PR_gTPTunnel;
  asn1cCalloc(dLQosFlowPerTNLInformation->uPTransportLayerInformation.choice.gTPTunnel, gtp);
  GTP_TEID_TO_ASN1(pdusession->n3_outgoing.teid, &gtp->gTP_TEID);
  tnl_to_bitstring(&gtp->transportLayerAddress, pdusession->n3_outgoing.addr);
  char ip_str[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, gtp->transportLayerAddress.buf, ip_str, sizeof(ip_str));
  NGAP_DEBUG("Encoded PDU Session Transfer (%d): TEID=0x%08x, Addr=%s\n",
             pdusession->pdusession_id,
             pdusession->n3_outgoing.teid,
             ip_str);

  // Associated QoS Flow List (Mandatory)
  for (int j = 0; j < pdusession->nb_of_qos_flow; j++) {
    asn1cSequenceAdd(dLQosFlowPerTNLInformation->associatedQosFlowList.list, NGAP_AssociatedQosFlowItem_t, qos_item);
    // QoS Flow Identifier (Mandatory)
    qos_item->qosFlowIdentifier = pdusession->associated_qos_flows[j].qfi;
  }

  void *buf = NULL;
  ssize_t encoded =
      aper_encode_to_new_buffer(&asn_DEF_NGAP_PDUSessionResourceSetupResponseTransfer, NULL, &pdusessionTransfer, &buf);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDUSessionResourceSetupResponseTransfer, &pdusessionTransfer);
  if (encoded < 0)
    return out;
  out.buf = buf;
  out.len = encoded;
  return out;
}

/** @brief PDU Session Resource Setup Unsuccessful Transfer encoding (9.3.4.16 3GPP TS 38.413) */
byte_array_t encode_ngap_pdusession_setup_unsuccessful_transfer(const ngap_cause_t *cause)
{
  DevAssert(cause != NULL);

  NGAP_PDUSessionResourceSetupUnsuccessfulTransfer_t transfer = {0};
  byte_array_t out = {0};

  encode_ngap_cause(&transfer.cause, cause);

  void *buf = NULL;
  ssize_t encoded = aper_encode_to_new_buffer(&asn_DEF_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, NULL, &transfer, &buf);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, &transfer);
  if (encoded < 0)
    return out;
  out.buf = buf;
  out.len = encoded;
  return out;
}

bool eq_ngap_plmn(const plmn_id_t *a, const plmn_id_t *b)
{
  if (a == NULL || b == NULL) {
    return false;
  }
  _EQ_CHECK_INT(a->mcc, b->mcc);
  _EQ_CHECK_INT(a->mnc, b->mnc);
  _EQ_CHECK_INT(a->mnc_digit_length, b->mnc_digit_length);
  return true;
}
