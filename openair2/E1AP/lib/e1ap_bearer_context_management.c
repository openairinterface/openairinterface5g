/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief E1AP library for E1 Bearer Context Management
 */

#include <string.h>
#include <stdint.h>

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"

#include "e1ap_lib_common.h"
#include "e1ap_bearer_context_management.h"
#include "e1ap_lib_includes.h"

/** @brief Encode E1AP_SecurityIndication_t (3GPP TS 38.463 9.3.1.23) */
static E1AP_SecurityIndication_t e1_encode_security_indication(const security_indication_t *in)
{
  E1AP_SecurityIndication_t out = {0};
  out.integrityProtectionIndication = in->integrityProtectionIndication;
  out.confidentialityProtectionIndication = in->confidentialityProtectionIndication;
  if (in->integrityProtectionIndication != SECURITY_NOT_NEEDED) {
    asn1cCalloc(out.maximumIPdatarate, ipDataRate);
    ipDataRate->maxIPrate = in->maxIPrate;
  }
  return out;
}

/** @brief Decode E1AP_SecurityIndication_t (3GPP TS 38.463 9.3.1.23) */
static bool e1_decode_security_indication(security_indication_t *out, const E1AP_SecurityIndication_t *in)
{
  // Confidentiality Protection Indication (M)
  out->confidentialityProtectionIndication = in->confidentialityProtectionIndication;
  // Maximum Integrity Protected Data Rate (Conditional)
  if (in->integrityProtectionIndication != SECURITY_NOT_NEEDED) {
    if (in->maximumIPdatarate)
      out->maxIPrate = in->maximumIPdatarate->maxIPrate;
    else {
      PRINT_ERROR("Received integrityProtectionIndication but maximumIPdatarate IE is missing\n");
      return false;
    }
  }
  return true;
}

/**
 * @brief Encode E1AP_SecurityInformation_t
 */
static E1AP_SecurityInformation_t e1_encode_security_info(const security_information_t *secInfo)
{
  E1AP_SecurityInformation_t ie = {0};
  E1AP_SecurityAlgorithm_t *securityAlgorithm = &ie.securityAlgorithm;
  E1AP_UPSecuritykey_t *uPSecuritykey = &ie.uPSecuritykey;
  securityAlgorithm->cipheringAlgorithm = secInfo->cipheringAlgorithm;
  OCTET_STRING_fromBuf(&uPSecuritykey->encryptionKey, secInfo->encryptionKey, E1AP_SECURITY_KEY_SIZE);
  asn1cCallocOne(securityAlgorithm->integrityProtectionAlgorithm, secInfo->integrityProtectionAlgorithm);
  asn1cCalloc(uPSecuritykey->integrityProtectionKey, protKey);
  OCTET_STRING_fromBuf(protKey, secInfo->integrityProtectionKey, E1AP_SECURITY_KEY_SIZE);
  return ie;
}

/** @brief Decode Security Information IE (3GPP TS 38.463, 9.3.1.10) */
static bool e1_decode_security_info(security_information_t *out, const E1AP_SecurityInformation_t *in)
{
  // Security Algorithm (M) (9.3.1.31)
  // Ciphering Algorithm (M)
  out->cipheringAlgorithm = in->securityAlgorithm.cipheringAlgorithm;
  // Integrity Protection Algorithm (O)
  out->integrityProtectionAlgorithm =
      in->securityAlgorithm.integrityProtectionAlgorithm == NULL ? 0 : *in->securityAlgorithm.integrityProtectionAlgorithm;

  // User Plane Security Keys (M) (9.3.1.32)
  // Encryption Key (M)
  _EQ_CHECK_INT((int)in->uPSecuritykey.encryptionKey.size, E1AP_SECURITY_KEY_SIZE);
  memcpy(out->encryptionKey, in->uPSecuritykey.encryptionKey.buf, in->uPSecuritykey.encryptionKey.size);
  // Integrity Protection Key (O)
  if (in->uPSecuritykey.integrityProtectionKey) {
    E1AP_IntegrityProtectionKey_t *ipKey = in->uPSecuritykey.integrityProtectionKey;
    _EQ_CHECK_INT((int)ipKey->size, E1AP_SECURITY_KEY_SIZE);
    memcpy(out->integrityProtectionKey, ipKey->buf, E1AP_SECURITY_KEY_SIZE);
  } else {
    memset(out->integrityProtectionKey, 0, E1AP_SECURITY_KEY_SIZE);
  }

  return true;
}

/**
 * @brief Encode E1AP_UP_TNL_Information_t
 */
static E1AP_UP_TNL_Information_t e1_encode_up_tnl_info(const UP_TL_information_t *in)
{
  E1AP_UP_TNL_Information_t out = {0};
  out.present = E1AP_UP_TNL_Information_PR_gTPTunnel;
  asn1cCalloc(out.choice.gTPTunnel, gTPTunnel);
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(in->tlAddress, &gTPTunnel->transportLayerAddress);
  INT32_TO_OCTET_STRING(in->teId, &gTPTunnel->gTP_TEID);
  return out;
}

/**
 * @brief Decode E1AP_UP_TNL_Information_t
 */
static bool e1_decode_up_tnl_info(UP_TL_information_t *out, const E1AP_UP_TNL_Information_t *in)
{
  /* NG UL UP Transport Layer Information (M) (9.3.2.1 of 3GPP TS 38.463) */
  // GTP Tunnel
  struct E1AP_GTPTunnel *gTPTunnel = in->choice.gTPTunnel;
  AssertFatal(gTPTunnel != NULL, "item->nG_UL_UP_TNL_Information.choice.gTPTunnel is a mandatory IE");
  _EQ_CHECK_INT(in->present, E1AP_UP_TNL_Information_PR_gTPTunnel);
  BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gTPTunnel->transportLayerAddress, out->tlAddress);
  // GTP-TEID
  OCTET_STRING_TO_INT32(&gTPTunnel->gTP_TEID, out->teId);
  return true;
}

/** @brief Encode PDCP Configuration IE */
static E1AP_PDCP_Configuration_t e1_encode_pdcp_config(const bearer_context_pdcp_config_t *in)
{
  E1AP_PDCP_Configuration_t out = {0};
  out.pDCP_SN_Size_UL = in->pDCP_SN_Size_UL;
  out.pDCP_SN_Size_DL = in->pDCP_SN_Size_DL;
  asn1cCallocOne(out.discardTimer, in->discardTimer);
  E1AP_T_ReorderingTimer_t *roTimer = calloc_or_fail(1, sizeof(*roTimer));
  out.t_ReorderingTimer = roTimer;
  roTimer->t_Reordering = in->reorderingTimer;
  out.rLC_Mode = in->rLC_Mode;
  if (in->pDCP_Reestablishment) {
    asn1cCallocOne(out.pDCP_Reestablishment, E1AP_PDCP_Reestablishment_true);
  }
  return out;
}

/** @brief Decode PDCP Configuration IE */
static bool e1_decode_pdcp_config(bearer_context_pdcp_config_t *out, const E1AP_PDCP_Configuration_t *in)
{
  out->pDCP_SN_Size_UL = in->pDCP_SN_Size_UL;
  out->pDCP_SN_Size_DL = in->pDCP_SN_Size_DL;
  if (in->discardTimer) {
    out->discardTimer = *in->discardTimer;
  }
  if (in->t_ReorderingTimer) {
    out->reorderingTimer = in->t_ReorderingTimer->t_Reordering;
  }
  out->rLC_Mode = in->rLC_Mode;
  if (in->pDCP_Reestablishment && *in->pDCP_Reestablishment == E1AP_PDCP_Reestablishment_true)
    out->pDCP_Reestablishment = true;
  return true;
}

/**
 * @brief Equality check for bearer_context_pdcp_config_t
 */
static bool eq_pdcp_config(const bearer_context_pdcp_config_t *a, const bearer_context_pdcp_config_t *b)
{
  _EQ_CHECK_LONG(a->pDCP_SN_Size_UL, b->pDCP_SN_Size_UL);
  _EQ_CHECK_LONG(a->pDCP_SN_Size_DL, b->pDCP_SN_Size_DL);
  _EQ_CHECK_LONG(a->rLC_Mode, b->rLC_Mode);
  _EQ_CHECK_LONG(a->reorderingTimer, b->reorderingTimer);
  _EQ_CHECK_LONG(a->discardTimer, b->discardTimer);
  _EQ_CHECK_INT(a->pDCP_Reestablishment, b->pDCP_Reestablishment);
  return true;
}

/** @brief Encode SDAP Configuration IE */
static E1AP_SDAP_Configuration_t e1_encode_sdap_config(const bearer_context_sdap_config_t *in)
{
  E1AP_SDAP_Configuration_t out = {0};
  out.defaultDRB = in->defaultDRB ? E1AP_DefaultDRB_true : E1AP_DefaultDRB_false;
  out.sDAP_Header_UL = in->sDAP_Header_UL ? E1AP_SDAP_Header_UL_present : E1AP_SDAP_Header_UL_absent;
  out.sDAP_Header_DL = in->sDAP_Header_DL ? E1AP_SDAP_Header_DL_present : E1AP_SDAP_Header_DL_absent;
  return out;
}

/** @brief Decode SDAP Configuration IE */
static bool e1_decode_sdap_config(bearer_context_sdap_config_t *out, const E1AP_SDAP_Configuration_t *in)
{
  out->defaultDRB = in->defaultDRB == E1AP_DefaultDRB_true;
  out->sDAP_Header_UL = in->sDAP_Header_UL == E1AP_SDAP_Header_UL_present;
  out->sDAP_Header_DL = in->sDAP_Header_DL == E1AP_SDAP_Header_DL_present;
  return true;
}

/**
 * @brief Equality check for bearer_context_sdap_config_t
 */
static bool eq_sdap_config(const bearer_context_sdap_config_t *a, const bearer_context_sdap_config_t *b)
{
  _EQ_CHECK_LONG(a->defaultDRB, b->defaultDRB);
  _EQ_CHECK_INT(a->sDAP_Header_UL, b->sDAP_Header_UL);
  _EQ_CHECK_INT(a->sDAP_Header_DL, b->sDAP_Header_DL);
  return true;
}

static E1AP_QoS_Flow_QoS_Parameter_Item_t e1_encode_qos_flow_to_setup(const qos_flow_to_setup_t *in)
{
  E1AP_QoS_Flow_QoS_Parameter_Item_t out = {0};
  out.qoS_Flow_Identifier = in->qfi;
  // QoS Characteristics
  const qos_characteristics_t *qos_char_in = &in->qos_params.qos_characteristics;
  if (qos_char_in->qos_type == NON_DYNAMIC) { // non Dynamic 5QI
    out.qoSFlowLevelQoSParameters.qoS_Characteristics.present = E1AP_QoS_Characteristics_PR_non_Dynamic_5QI;
    asn1cCalloc(out.qoSFlowLevelQoSParameters.qoS_Characteristics.choice.non_Dynamic_5QI, non_Dynamic_5QI);
    non_Dynamic_5QI->fiveQI = qos_char_in->non_dynamic.fiveqi;
  } else { // dynamic 5QI
    out.qoSFlowLevelQoSParameters.qoS_Characteristics.present = E1AP_QoS_Characteristics_PR_dynamic_5QI;
    asn1cCalloc(out.qoSFlowLevelQoSParameters.qoS_Characteristics.choice.dynamic_5QI, dynamic_5QI);
    dynamic_5QI->qoSPriorityLevel = qos_char_in->dynamic.qos_priority_level;
    dynamic_5QI->packetDelayBudget = qos_char_in->dynamic.packet_delay_budget;
    dynamic_5QI->packetErrorRate.pER_Scalar = qos_char_in->dynamic.packet_error_rate.per_scalar;
    dynamic_5QI->packetErrorRate.pER_Exponent = qos_char_in->dynamic.packet_error_rate.per_exponent;
  }
  // QoS Retention Priority
  const ngran_allocation_retention_priority_t *rent_priority_in = &in->qos_params.alloc_reten_priority;
  E1AP_NGRANAllocationAndRetentionPriority_t *arp = &out.qoSFlowLevelQoSParameters.nGRANallocationRetentionPriority;
  arp->priorityLevel = rent_priority_in->priority_level;
  arp->pre_emptionCapability = rent_priority_in->preemption_capability;
  arp->pre_emptionVulnerability = rent_priority_in->preemption_vulnerability;
  return out;
}

bool e1_decode_qos_flow_to_setup(qos_flow_to_setup_t *out, const E1AP_QoS_Flow_QoS_Parameter_Item_t *in)
{
  // QoS Flow Identifier (M)
  out->qfi = in->qoS_Flow_Identifier;
  qos_characteristics_t *qos_char = &out->qos_params.qos_characteristics;
  // QoS Flow Level QoS Parameters (M)
  const E1AP_QoSFlowLevelQoSParameters_t *qosParams = &in->qoSFlowLevelQoSParameters;
  const E1AP_QoS_Characteristics_t *qoS_Characteristics = &qosParams->qoS_Characteristics;
  switch (qoS_Characteristics->present) {
    case E1AP_QoS_Characteristics_PR_non_Dynamic_5QI: {
      const int fiveqi = qoS_Characteristics->choice.non_Dynamic_5QI->fiveQI;
      if (fiveqi < MIN_FIVEQI || fiveqi > MAX_FIVEQI) {
        PRINT_ERROR("non-dynamic fiveQI %d out of range %d..%d\n", fiveqi, MIN_FIVEQI, MAX_FIVEQI);
        return false;
      }
      qos_char->qos_type = NON_DYNAMIC;
      qos_char->non_dynamic.fiveqi = fiveqi;
      break;
    }
    case E1AP_QoS_Characteristics_PR_dynamic_5QI: {
      E1AP_Dynamic5QIDescriptor_t *dynamic5QI = qoS_Characteristics->choice.dynamic_5QI;
      qos_char->qos_type = DYNAMIC;
      qos_char->dynamic.qos_priority_level = dynamic5QI->qoSPriorityLevel;
      qos_char->dynamic.packet_delay_budget = dynamic5QI->packetDelayBudget;
      qos_char->dynamic.packet_error_rate.per_scalar = dynamic5QI->packetErrorRate.pER_Scalar;
      qos_char->dynamic.packet_error_rate.per_exponent = dynamic5QI->packetErrorRate.pER_Exponent;
      break;
    }
    default:
      PRINT_ERROR("Unexpected QoS Characteristics type: %d\n", qoS_Characteristics->present);
      return false;
      break;
  }
  // NG-RAN Allocation and Retention Priority (M)
  ngran_allocation_retention_priority_t *rent_priority = &out->qos_params.alloc_reten_priority;
  const E1AP_NGRANAllocationAndRetentionPriority_t *aRP = &qosParams->nGRANallocationRetentionPriority;
  rent_priority->priority_level = aRP->priorityLevel;
  rent_priority->preemption_capability = aRP->pre_emptionCapability;
  rent_priority->preemption_vulnerability = aRP->pre_emptionVulnerability;
  return true;
}

/**
 * @brief Equality check for qos_flow_to_setup_t
 */
static bool eq_qos_flow(const qos_flow_to_setup_t *a, const qos_flow_to_setup_t *b)
{
  _EQ_CHECK_LONG(a->qfi, b->qfi);
  const ngran_allocation_retention_priority_t *arp_a = &a->qos_params.alloc_reten_priority;
  const ngran_allocation_retention_priority_t *arp_b = &b->qos_params.alloc_reten_priority;
  _EQ_CHECK_INT(arp_a->preemption_capability, arp_b->preemption_capability);
  _EQ_CHECK_INT(arp_a->preemption_vulnerability, arp_b->preemption_vulnerability);
  _EQ_CHECK_INT(arp_a->priority_level, arp_b->priority_level);
  const qos_characteristics_t *qos_a = &a->qos_params.qos_characteristics;
  const qos_characteristics_t *qos_b = &b->qos_params.qos_characteristics;
  _EQ_CHECK_INT(qos_a->qos_type, qos_b->qos_type);
  _EQ_CHECK_INT(qos_a->dynamic.fiveqi, qos_b->dynamic.fiveqi);
  _EQ_CHECK_INT(qos_a->dynamic.packet_delay_budget, qos_b->dynamic.packet_delay_budget);
  _EQ_CHECK_INT(qos_a->dynamic.packet_error_rate.per_exponent, qos_b->dynamic.packet_error_rate.per_exponent);
  _EQ_CHECK_INT(qos_a->dynamic.packet_error_rate.per_scalar, qos_b->dynamic.packet_error_rate.per_scalar);
  _EQ_CHECK_INT(qos_a->dynamic.qos_priority_level, qos_b->dynamic.qos_priority_level);
  _EQ_CHECK_INT(qos_a->non_dynamic.fiveqi, qos_b->non_dynamic.fiveqi);
  _EQ_CHECK_INT(qos_a->non_dynamic.qos_priority_level, qos_b->non_dynamic.qos_priority_level);
  return true;
}

/** @brief S-NSSAI Encoding (3GPP TS 38.463 9.3.1.9) */
static E1AP_SNSSAI_t e1_encode_snssai(const nssai_t *in)
{
  E1AP_SNSSAI_t out = {0};
  INT8_TO_OCTET_STRING(in->sst, &out.sST);
  if (in->sd != 0xffffff) {
    out.sD = malloc_or_fail(sizeof(*out.sD));
    INT24_TO_OCTET_STRING(in->sd, out.sD);
  }
  return out;
}

/** @brief S-NSSAI Decoding (3GPP TS 38.463 9.3.1.9) */
static bool e1_decode_snssai(e1ap_nssai_t *out, const E1AP_SNSSAI_t *in)
{
  // SST (M)
  OCTET_STRING_TO_INT8(&in->sST, out->sst);
  out->sd = 0xffffff;
  // SD (O)
  if (in->sD != NULL)
    OCTET_STRING_TO_INT24(in->sD, out->sd);
  return true;
}

/**
 * @brief Decode E1AP_UP_Parameters_t
 */
static int decode_dl_up_parameters(up_params_t *dl_up_param, const E1AP_UP_Parameters_t *dl_up_paramList)
{
  for (int k = 0; k < dl_up_paramList->list.count; k++) {
    dl_up_param = dl_up_param + k;
    E1AP_UP_Parameters_Item_t *dl_up_param_in = dl_up_paramList->list.array[k];
    // GTP tunnel
    if (!e1_decode_up_tnl_info(&dl_up_param->tl_info, &dl_up_param_in->uP_TNL_Information)) {
      PRINT_ERROR("GTP tunnel decoding failed\n");
      return -1;
    }
    // Cell group ID
    dl_up_param->cell_group_id = dl_up_param_in->cell_Group_ID;
  }
  return dl_up_paramList->list.count;
}

/**
 * @brief Encode E1AP_UP_Parameters_t IE
 */
static E1AP_UP_Parameters_t encode_dl_up_parameters(const int numUpParam, const up_params_t *in)
{
  E1AP_UP_Parameters_t out = {0};
  for (const up_params_t *k = in; k < in + numUpParam; k++) {
    asn1cSequenceAdd(out.list, E1AP_UP_Parameters_Item_t, ieC3_1_1_1);
    ieC3_1_1_1->uP_TNL_Information = e1_encode_up_tnl_info(&k->tl_info);
  }
  return out;
}

/** @brief PDCP SN Status Information (9.3.1.58 3GPP TS 38.463) */
static E1AP_PDCP_SN_Status_Information_t encode_pdcp_status_info(const e1_pdcp_status_info_t *in)
{
  E1AP_PDCP_SN_Status_Information_t pdcp = {0};
  pdcp.pdcpStatusTransfer_DL.hFN = in->dl_count.hfn;
  pdcp.pdcpStatusTransfer_DL.pDCP_SN = in->dl_count.sn;
  pdcp.pdcpStatusTransfer_UL.countValue.hFN = in->ul_count.hfn;
  pdcp.pdcpStatusTransfer_UL.countValue.pDCP_SN = in->ul_count.sn;
  return pdcp;
}

/** @brief PDCP SN Status Information (9.3.1.58 3GPP TS 38.463) */
static e1_pdcp_status_info_t decode_pdcp_status_info(const E1AP_PDCP_SN_Status_Information_t* in)
{
  e1_pdcp_status_info_t out = {0};
  const E1AP_PDCP_Count_t *dl = &in->pdcpStatusTransfer_DL;
  const E1AP_PDCP_Count_t *ul = &in->pdcpStatusTransfer_UL.countValue;
  out.dl_count.hfn = dl->hFN;
  out.dl_count.sn = dl->pDCP_SN;
  out.ul_count.hfn = ul->hFN;
  out.ul_count.sn = ul->pDCP_SN;
  return out;
}

static bool eq_pdcp_info(const e1_pdcp_status_info_t *a, const e1_pdcp_status_info_t *b)
{
  _EQ_CHECK_INT(a->dl_count.hfn, b->dl_count.hfn);
  _EQ_CHECK_INT(a->dl_count.sn, b->dl_count.sn);
  _EQ_CHECK_INT(a->ul_count.hfn, b->ul_count.hfn);
  _EQ_CHECK_INT(a->ul_count.sn, b->ul_count.sn);
  return true;
}

/**
 * @brief Equality check for DRB_nGRAN_to_setup_t
 */
static bool eq_drb_to_setup(const DRB_nGRAN_to_setup_t *a, const DRB_nGRAN_to_setup_t *b)
{
  _EQ_CHECK_LONG(a->id, b->id);
  _EQ_CHECK_INT(a->numCellGroups, b->numCellGroups);
  for (int i = 0; i < a->numCellGroups; i++) {
    _EQ_CHECK_INT(a->cellGroupList[i], b->cellGroupList[i]);
  }
  _EQ_CHECK_INT(a->numQosFlow2Setup, b->numQosFlow2Setup);
  for (int i = 0; i < a->numQosFlow2Setup; i++) {
    if (!eq_qos_flow(&a->qosFlows[i], &b->qosFlows[i]))
      return false;
  }
  if (!eq_pdcp_config(&a->pdcp_config, &b->pdcp_config))
    return false;
  if (!eq_sdap_config(&a->sdap_config, &b->sdap_config))
    return false;
  return true;
}

static void free_drb_to_setup_item(const DRB_nGRAN_to_setup_t *msg)
{
  free(msg->drb_inactivity_timer);
}

/** @brief Encode PDU Session Resource To Setup List Item (3GPP TS 38.463 9.3.3.2) */
static bool e1_encode_pdu_session_to_setup_item(E1AP_PDU_Session_Resource_To_Setup_Item_t *item, const pdu_session_to_setup_t *in)
{
  // PDU Session ID (M)
  item->pDU_Session_ID = in->sessionId;
  // PDU Session Type (M)
  item->pDU_Session_Type = in->sessionType;
  // S-NSSAI (M)
  item->sNSSAI = e1_encode_snssai(&in->nssai);
  // Security Indication (M)
  item->securityIndication = e1_encode_security_indication(&in->securityIndication);
  // NG UL UP Transport Layer Information (M)
  item->nG_UL_UP_TNL_Information = e1_encode_up_tnl_info(&in->UP_TL_information);
  // DRB To Setup List (M)
  for (const DRB_nGRAN_to_setup_t *j = in->DRBnGRanList; j < in->DRBnGRanList + in->numDRB2Setup; j++) {
    asn1cSequenceAdd(item->dRB_To_Setup_List_NG_RAN.list, E1AP_DRB_To_Setup_Item_NG_RAN_t, ieC6_1_1);
    // DRB ID (M)
    ieC6_1_1->dRB_ID = j->id;
    // SDAP Configuration (M)
    ieC6_1_1->sDAP_Configuration = e1_encode_sdap_config(&j->sdap_config);
    // PDCP Configuration (M)
    ieC6_1_1->pDCP_Configuration = e1_encode_pdcp_config(&j->pdcp_config);
    // Cell Group Information (M)
    for (const cell_group_id_t *k = j->cellGroupList; k < j->cellGroupList + j->numCellGroups; k++) {
      asn1cSequenceAdd(ieC6_1_1->cell_Group_Information.list, E1AP_Cell_Group_Information_Item_t, ieC6_1_1_1);
      ieC6_1_1_1->cell_Group_ID = *k;
    }
    // QoS Flows Information To Be Setup (M)
    for (const qos_flow_to_setup_t *k = j->qosFlows; k < j->qosFlows + j->numQosFlow2Setup; k++) {
      asn1cSequenceAdd(ieC6_1_1->qos_flow_Information_To_Be_Setup.list, E1AP_QoS_Flow_QoS_Parameter_Item_t, ieC6_1_1_2);
      *ieC6_1_1_2 = e1_encode_qos_flow_to_setup(k);
    }
  }
  return true;
}

/** @brief Decode PDU Session Resource To Setup List Item (3GPP TS 38.463 9.3.3.2) */
static bool e1_decode_pdu_session_to_setup_item(pdu_session_to_setup_t *out, E1AP_PDU_Session_Resource_To_Setup_Item_t *item)
{
  // PDU Session ID (M)
  out->sessionId = item->pDU_Session_ID;
  // PDU Session Type (M)
  out->sessionType = item->pDU_Session_Type;
  // S-NSSAI (M)
  CHECK_E1AP_DEC(e1_decode_snssai(&out->nssai, &item->sNSSAI));
  // Security Indication (M)
  CHECK_E1AP_DEC(e1_decode_security_indication(&out->securityIndication, &item->securityIndication));
  /* NG UL UP Transport Layer Information (M) (9.3.2.1 of 3GPP TS 38.463) */
  // GTP Tunnel
  struct E1AP_GTPTunnel *gTPTunnel = item->nG_UL_UP_TNL_Information.choice.gTPTunnel;
  if (!gTPTunnel) {
    PRINT_ERROR("Missing mandatory IE item->nG_UL_UP_TNL_Information.choice.gTPTunnel\n");
    return false;
  }
  _EQ_CHECK_INT(item->nG_UL_UP_TNL_Information.present, E1AP_UP_TNL_Information_PR_gTPTunnel);
  CHECK_E1AP_DEC(e1_decode_up_tnl_info(&out->UP_TL_information, &item->nG_UL_UP_TNL_Information));
  /* DRB To Setup List ( > 1 item ) */
  E1AP_DRB_To_Setup_List_NG_RAN_t *drb2SetupList = &item->dRB_To_Setup_List_NG_RAN;
  EQ_CHECK_GENERIC(drb2SetupList->list.count > 0, "%d", drb2SetupList->list.count, 0);
  out->numDRB2Setup = drb2SetupList->list.count;
  for (int j = 0; j < drb2SetupList->list.count; j++) {
    DRB_nGRAN_to_setup_t *drb = out->DRBnGRanList + j;
    E1AP_DRB_To_Setup_Item_NG_RAN_t *drb2Setup = drb2SetupList->list.array[j];
    // DRB ID (M)
    drb->id = drb2Setup->dRB_ID;
    // SDAP Configuration (M)
    CHECK_E1AP_DEC(e1_decode_sdap_config(&drb->sdap_config, &drb2Setup->sDAP_Configuration));
    // PDCP Configuration (M)
    CHECK_E1AP_DEC(e1_decode_pdcp_config(&drb->pdcp_config, &drb2Setup->pDCP_Configuration));
    // Cell Group Information (M)
    E1AP_Cell_Group_Information_t *cellGroupList = &drb2Setup->cell_Group_Information;
    EQ_CHECK_GENERIC(cellGroupList->list.count > 0, "%d", cellGroupList->list.count, 0);
    drb->numCellGroups = cellGroupList->list.count;
    for (int k = 0; k < cellGroupList->list.count; k++) {
      E1AP_Cell_Group_Information_Item_t *cg2Setup = cellGroupList->list.array[k];
      // Cell Group ID
      drb->cellGroupList[k] = cg2Setup->cell_Group_ID;
    }
    // QoS Flows Information To Be Setup (M)
    E1AP_QoS_Flow_QoS_Parameter_List_t *qos2SetupList = &drb2Setup->qos_flow_Information_To_Be_Setup;
    drb->numQosFlow2Setup = qos2SetupList->list.count;
    for (int k = 0; k < qos2SetupList->list.count; k++) {
      CHECK_E1AP_DEC(e1_decode_qos_flow_to_setup(drb->qosFlows + k, qos2SetupList->list.array[k]));
    }
  }
  return true;
}

/** @brief Deep copy DRB To Setup List Item (3GPP TS 38.463 9.3.3.2) */
static DRB_nGRAN_to_setup_t cp_drb_to_setup_item(const DRB_nGRAN_to_setup_t *msg)
{
  DRB_nGRAN_to_setup_t cp = {0};
  cp.id = msg->id;
  cp.sdap_config = msg->sdap_config;
  cp.pdcp_config = msg->pdcp_config;
  cp.numCellGroups = msg->numCellGroups;
  // Cell Group List
  for (int k = 0; k < msg->numCellGroups; k++) {
    cp.cellGroupList[k] = msg->cellGroupList[k];
  }
  cp.numQosFlow2Setup = msg->numQosFlow2Setup;
  // QoS Flow To Setup List
  for (int k = 0; k < msg->numQosFlow2Setup; k++) {
    cp.qosFlows[k].qfi = msg->qosFlows[k].qfi;
    cp.qosFlows[k].qos_params = msg->qosFlows[k].qos_params;
  }
  return cp;
}

/** @brief Free PDU Session Resource To Setup List item (3GPP TS 38.463 9.3.3.2) */
void free_pdu_session_to_setup_item(const pdu_session_to_setup_t *msg)
{
  free(msg->dlAggregateMaxBitRate);
  free(msg->inactivityTimer);
  for (int i = 0; i < msg->numDRB2Setup; i++)
    free_drb_to_setup_item(&msg->DRBnGRanList[i]);
}

/** @brief Equality check for UP Transport Layer Information (3GPP TS 38.463 9.3.2.1)*/
static bool eq_up_tl_info(const UP_TL_information_t *a, const UP_TL_information_t *b)
{
  _EQ_CHECK_INT(a->tlAddress, b->tlAddress);
  _EQ_CHECK_INT(a->teId, b->teId);
  return true;
}

/**
 * @brief Equality check for PDU session item to modify/setup
 */
static bool eq_pdu_session_item(const pdu_session_to_setup_t *a, const pdu_session_to_setup_t *b)
{
  _EQ_CHECK_LONG(a->sessionId, b->sessionId);
  _EQ_CHECK_LONG(a->sessionType, b->sessionType);
  eq_up_tl_info(&a->UP_TL_information, &b->UP_TL_information);
  _EQ_CHECK_INT(a->numDRB2Setup, b->numDRB2Setup);
  for (int i = 0; i < a->numDRB2Setup; i++)
    if (!eq_drb_to_setup(&a->DRBnGRanList[i], &b->DRBnGRanList[i]))
      return false;
  _EQ_CHECK_OPTIONAL_PTR(a, b, inactivityTimer);
  if (a->inactivityTimer && b->inactivityTimer)
    _EQ_CHECK_INT(*a->inactivityTimer, *b->inactivityTimer);
  _EQ_CHECK_OPTIONAL_IE(a, b, dlAggregateMaxBitRate, _EQ_CHECK_LONG);
  return true;
}

/* ====================================
 *   E1AP Bearer Context Setup Request
 * ==================================== */

/**
 * @brief E1AP Bearer Context Setup Request encoding (9.2.2 of 3GPP TS 38.463)
 */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_setup_request(const e1ap_bearer_setup_req_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, initMsg);
  initMsg->procedureCode = E1AP_ProcedureCode_id_bearerContextSetup;
  initMsg->criticality = E1AP_Criticality_reject;
  initMsg->value.present = E1AP_InitiatingMessage__value_PR_BearerContextSetupRequest;
  E1AP_BearerContextSetupRequest_t *out = &pdu->choice.initiatingMessage->value.choice.BearerContextSetupRequest;
  /* mandatory */
  /* c1. gNB-CU-UP UE E1AP ID */
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC1);
  ieC1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ieC1->criticality = E1AP_Criticality_reject;
  ieC1->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ieC1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;
  /* mandatory */
  /* c2. Security Information */
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC2);
  ieC2->id = E1AP_ProtocolIE_ID_id_SecurityInformation;
  ieC2->criticality = E1AP_Criticality_reject;
  ieC2->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_SecurityInformation;
  ieC2->value.choice.SecurityInformation = e1_encode_security_info(&msg->secInfo);
  /* mandatory */
  /* c3. UE DL Aggregate Maximum Bit Rate */
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC3);
  ieC3->id = E1AP_ProtocolIE_ID_id_UEDLAggregateMaximumBitRate;
  ieC3->criticality = E1AP_Criticality_reject;
  ieC3->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_BitRate;
  asn_long2INTEGER(&ieC3->value.choice.BitRate, msg->ueDlAggMaxBitRate);
  /* mandatory */
  /* c4. Serving PLMN */
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC4);
  ieC4->id = E1AP_ProtocolIE_ID_id_Serving_PLMN;
  ieC4->criticality = E1AP_Criticality_ignore;
  ieC4->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_PLMN_Identity;
  const PLMN_ID_t *servingPLMN = &msg->servingPLMNid;
  MCC_MNC_TO_PLMNID(servingPLMN->mcc, servingPLMN->mnc, servingPLMN->mnc_digit_length, &ieC4->value.choice.PLMN_Identity);
  /* mandatory */
  /* Activity Notification Level */
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC5);
  ieC5->id = E1AP_ProtocolIE_ID_id_ActivityNotificationLevel;
  ieC5->criticality = E1AP_Criticality_reject;
  ieC5->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_ActivityNotificationLevel;
  ieC5->value.choice.ActivityNotificationLevel = (E1AP_ActivityNotificationLevel_t)msg->anl;
  // System (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupRequestIEs_t, ieC6);
  ieC6->id = E1AP_ProtocolIE_ID_id_System_BearerContextSetupRequest;
  ieC6->criticality = E1AP_Criticality_reject;
  ieC6->value.present = E1AP_BearerContextSetupRequestIEs__value_PR_System_BearerContextSetupRequest;
  ieC6->value.choice.System_BearerContextSetupRequest.present =
      E1AP_System_BearerContextSetupRequest_PR_nG_RAN_BearerContextSetupRequest;
  E1AP_ProtocolIE_Container_4932P19_t *msgNGRAN_list = calloc_or_fail(1, sizeof(*msgNGRAN_list));
  ieC6->value.choice.System_BearerContextSetupRequest.choice.nG_RAN_BearerContextSetupRequest =
      (struct E1AP_ProtocolIE_Container *)msgNGRAN_list;
  asn1cSequenceAdd(msgNGRAN_list->list, E1AP_NG_RAN_BearerContextSetupRequest_t, msgNGRAN);
  msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Setup_List;
  msgNGRAN->criticality = E1AP_Criticality_reject;
  msgNGRAN->value.present = E1AP_NG_RAN_BearerContextSetupRequest__value_PR_PDU_Session_Resource_To_Setup_List;
  E1AP_PDU_Session_Resource_To_Setup_List_t *pdu2Setup = &msgNGRAN->value.choice.PDU_Session_Resource_To_Setup_List;
  for (int i = 0; i < msg->numPDUSessions; i++) {
    const pdu_session_to_setup_t *pdu_session = &msg->pduSession[i];
    asn1cSequenceAdd(pdu2Setup->list, E1AP_PDU_Session_Resource_To_Setup_Item_t, ieC6_1);
    CHECK_E1AP_DEC(e1_encode_pdu_session_to_setup_item(ieC6_1, pdu_session));
  }
  return pdu;
}

/**
 * @brief E1AP Bearer Context Setup Request memory management
 */
void free_e1ap_context_setup_request(e1ap_bearer_setup_req_t *msg)
{
  free(msg->inactivityTimerUE);
  free(msg->ueDlMaxIPBitRate);
  free(msg->pduSession);
}

/**
 * @brief E1AP Bearer Context Setup Request decoding
 *        gNB-CU-CP → gNB-CU-UP (9.2.2.1 of 3GPP TS 38.463)
 */
bool decode_E1_bearer_context_setup_request(const E1AP_E1AP_PDU_t *pdu, e1ap_bearer_setup_req_t *out)
{
  DevAssert(pdu != NULL);

  // Check message type
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_initiatingMessage);
  _EQ_CHECK_LONG(pdu->choice.initiatingMessage->procedureCode, E1AP_ProcedureCode_id_bearerContextSetup);
  _EQ_CHECK_INT(pdu->choice.initiatingMessage->value.present, E1AP_InitiatingMessage__value_PR_BearerContextSetupRequest);

  const E1AP_BearerContextSetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.BearerContextSetupRequest;
  E1AP_BearerContextSetupRequestIEs_t *ie;

  // Check mandatory IEs first
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_SecurityInformation, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_UEDLAggregateMaximumBitRate, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Serving_PLMN, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_ActivityNotificationLevel, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_System_BearerContextSetupRequest, true);

  // Loop over all IEs
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_SecurityInformation:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_SecurityInformation);
        CHECK_E1AP_DEC(e1_decode_security_info(&out->secInfo, &ie->value.choice.SecurityInformation));
        break;

      case E1AP_ProtocolIE_ID_id_UEDLAggregateMaximumBitRate:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_BitRate);
        asn_INTEGER2long(&ie->value.choice.BitRate, &out->ueDlAggMaxBitRate);
        break;

      case E1AP_ProtocolIE_ID_id_Serving_PLMN:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_PLMN_Identity);
        PLMNID_TO_MCC_MNC(&ie->value.choice.PLMN_Identity,
                          out->servingPLMNid.mcc,
                          out->servingPLMNid.mnc,
                          out->servingPLMNid.mnc_digit_length);
        break;

      case E1AP_ProtocolIE_ID_id_ActivityNotificationLevel:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_ActivityNotificationLevel);
        out->anl = ie->value.choice.ActivityNotificationLevel;
        break;

      case E1AP_ProtocolIE_ID_id_System_BearerContextSetupRequest:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupRequestIEs__value_PR_System_BearerContextSetupRequest);
        E1AP_System_BearerContextSetupRequest_t *System_BearerContextSetupRequest =
            &ie->value.choice.System_BearerContextSetupRequest;
        switch (System_BearerContextSetupRequest->present) {
          case E1AP_System_BearerContextSetupRequest_PR_NOTHING:
            PRINT_ERROR("System Bearer Context Setup Request: no choice present\n");
            break;
          case E1AP_System_BearerContextSetupRequest_PR_e_UTRAN_BearerContextSetupRequest:
            AssertError(System_BearerContextSetupRequest->present == E1AP_System_BearerContextSetupRequest_PR_e_UTRAN_BearerContextSetupRequest,
                        return false, "E1AP_System_BearerContextSetupRequest_PR_e_UTRAN_BearerContextSetupRequest in E1 Setup Request not supported");
            break;
          case E1AP_System_BearerContextSetupRequest_PR_nG_RAN_BearerContextSetupRequest:
            // Handle nG-RAN Bearer Context Setup Request
            _EQ_CHECK_INT(System_BearerContextSetupRequest->present,
                          E1AP_System_BearerContextSetupRequest_PR_nG_RAN_BearerContextSetupRequest);
            AssertFatal(System_BearerContextSetupRequest->choice.nG_RAN_BearerContextSetupRequest != NULL,
                        "System_BearerContextSetupRequest->choice.nG_RAN_BearerContextSetupRequest shall not be null");
            break;
          default:
            PRINT_ERROR("System Bearer Context Setup Request: No valid choice present.\n");
            break;
        }

        E1AP_ProtocolIE_Container_4932P19_t *msgNGRAN_list =
            (E1AP_ProtocolIE_Container_4932P19_t *)System_BearerContextSetupRequest->choice.nG_RAN_BearerContextSetupRequest;
        E1AP_NG_RAN_BearerContextSetupRequest_t *msgNGRAN = msgNGRAN_list->list.array[0];
        // NG-RAN
        _EQ_CHECK_INT(msgNGRAN_list->list.count, 1); // only one RAN is expected
        _EQ_CHECK_LONG(msgNGRAN->id, E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Setup_List);
        _EQ_CHECK_INT(msgNGRAN->value.present, E1AP_NG_RAN_BearerContextSetupRequest__value_PR_PDU_Session_Resource_To_Setup_List);
        // PDU Session Resource To Setup List (9.3.3.2 of 3GPP TS 38.463)
        E1AP_PDU_Session_Resource_To_Setup_List_t *pdu2SetupList = &msgNGRAN->value.choice.PDU_Session_Resource_To_Setup_List;
        out->numPDUSessions = pdu2SetupList->list.count;
        DevAssert(out->numPDUSessions > 0 && out->numPDUSessions <= NR_MAX_NB_PDU_SESSIONS);
        // Allocate array for PDU sessions
        out->pduSession = calloc_or_fail(out->numPDUSessions, sizeof(*out->pduSession));
        // Loop through all PDU sessions
        for (int i = 0; i < pdu2SetupList->list.count; i++) {
          pdu_session_to_setup_t *pdu_session = out->pduSession + i;
          E1AP_PDU_Session_Resource_To_Setup_Item_t *pdu2Setup = pdu2SetupList->list.array[i];
          CHECK_E1AP_DEC(e1_decode_pdu_session_to_setup_item(pdu_session, pdu2Setup));
        }
        break;
      default:
        PRINT_ERROR("Handle for this IE %ld is not implemented (or) invalid IE detected\n", ie->id);
        break;
    }
  }
  return true;
}

/** @brief Deep copy PDU Session Resource To Setup List item (3GPP TS 38.463 9.3.3.2) */
static pdu_session_to_setup_t cp_pdu_session_item(const pdu_session_to_setup_t *msg)
{
  pdu_session_to_setup_t cp = {0};
  // Copy basic fields
  cp = *msg;
  // Copy DRB to Setup List
  for (int j = 0; j < msg->numDRB2Setup; j++)
    cp.DRBnGRanList[j] = cp_drb_to_setup_item(&msg->DRBnGRanList[j]);
  // Copy optional IEs
  _E1_CP_OPTIONAL_IE(&cp, msg, inactivityTimer);
  _E1_CP_OPTIONAL_IE(&cp, msg, dlAggregateMaxBitRate);
  return cp;
}

/** @brief Deep copy function for E1 BEARER CONTEXT SETUP REQUEST */
e1ap_bearer_setup_req_t cp_bearer_context_setup_request(const e1ap_bearer_setup_req_t *msg)
{
  e1ap_bearer_setup_req_t cp = {0};
  // Copy basic fields
  cp.gNB_cu_cp_ue_id = msg->gNB_cu_cp_ue_id;
  cp.secInfo = msg->secInfo;
  cp.ueDlAggMaxBitRate = msg->ueDlAggMaxBitRate;
  cp.servingPLMNid = msg->servingPLMNid;
  cp.anl = msg->anl;
  cp.bearerContextStatus = msg->bearerContextStatus;
  cp.numPDUSessions = msg->numPDUSessions;
  // Allocate and copy PDU Sessions array
  if (msg->numPDUSessions > 0 && msg->pduSession != NULL) {
    cp.pduSession = calloc_or_fail(msg->numPDUSessions, sizeof(*cp.pduSession));
    for (int i = 0; i < msg->numPDUSessions; i++)
      cp.pduSession[i] = cp_pdu_session_item(&msg->pduSession[i]);
  }
  // copy optional IEs
  _E1_CP_OPTIONAL_IE(&cp, msg, ueDlMaxIPBitRate);
  _E1_CP_OPTIONAL_IE(&cp, msg, inactivityTimerUE);
  return cp;
}

/**
 * @brief E1AP Bearer Context Setup Request equality check
 */
bool eq_bearer_context_setup_request(const e1ap_bearer_setup_req_t *a, const e1ap_bearer_setup_req_t *b)
{
  // Primitive data types
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_LONG(a->secInfo.cipheringAlgorithm, b->secInfo.cipheringAlgorithm);
  _EQ_CHECK_LONG(a->secInfo.integrityProtectionAlgorithm, b->secInfo.integrityProtectionAlgorithm);
  _EQ_CHECK_LONG(a->ueDlAggMaxBitRate, b->ueDlAggMaxBitRate);
  _EQ_CHECK_INT(a->anl, b->anl);
  _EQ_CHECK_INT(a->numPDUSessions, b->numPDUSessions);
  // PLMN
  _EQ_CHECK_INT(a->servingPLMNid.mcc, b->servingPLMNid.mcc);
  _EQ_CHECK_INT(a->servingPLMNid.mnc, b->servingPLMNid.mnc);
  _EQ_CHECK_INT(a->servingPLMNid.mnc_digit_length, b->servingPLMNid.mnc_digit_length);
  // Security Keys (binary, not NUL-terminated)
  if (memcmp(a->secInfo.encryptionKey, b->secInfo.encryptionKey, E1AP_SECURITY_KEY_SIZE) != 0)
    return false;
  if (memcmp(a->secInfo.integrityProtectionKey, b->secInfo.integrityProtectionKey, E1AP_SECURITY_KEY_SIZE) != 0)
    return false;
  // PDU Sessions
  _EQ_CHECK_INT(a->numPDUSessions, b->numPDUSessions);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSession);
  if (a->pduSession != NULL && b->pduSession != NULL) {
    for (int i = 0; i < a->numPDUSessions; i++)
      if (!eq_pdu_session_item(&a->pduSession[i], &b->pduSession[i]))
        return false;
  }
  // Check optional IEs
  _EQ_CHECK_OPTIONAL_PTR(a, b, inactivityTimerUE);
  if (a->inactivityTimerUE && b->inactivityTimerUE)
    _EQ_CHECK_INT(*a->inactivityTimerUE, *b->inactivityTimerUE);
  _EQ_CHECK_OPTIONAL_IE(a, b, ueDlMaxIPBitRate, _EQ_CHECK_LONG);
  return true;
}

/** Decode DRB Failed Item. Applies to both E1AP_DRB_Failed_To_Modify_Item_NG_RAN_t
 * and E1AP_DRB_Failed_Item_NG_RAN_t */
static void decode_drb_failed_item(DRB_nGRAN_failed_t *out, const E1AP_DRB_Failed_Item_NG_RAN_t *in)
{
  // DRB ID (M)
  out->id = in->dRB_ID;
  // Cause (M)
  out->cause = e1_decode_cause_ie(&in->cause);
}

/** Encode DRB Failed Item. Applies to both E1AP_DRB_Failed_To_Modify_Item_NG_RAN_t
 * and E1AP_DRB_Failed_Item_NG_RAN_t */
static void encode_drb_failed_item(E1AP_DRB_Failed_Item_NG_RAN_t *out, const DRB_nGRAN_failed_t *in)
{
  // DRB ID (M)
  out->dRB_ID = in->id;
  // Cause (M)
  out->cause = e1_encode_cause_ie(&in->cause);
}

/* ======================================
 *   E1AP Bearer Context Setup Response
 * ====================================== */

/* PDU Session Resource Setup Item */

/**
 * @brief Decode an item of the E1 PDU Session Resource Setup List (9.3.3.5 of 3GPP TS 38.463)
 */
static bool e1_decode_pdu_session_setup_item(pdu_session_setup_t *pduSetup, E1AP_PDU_Session_Resource_Setup_Item_t *in)
{
  // PDU Session ID
  pduSetup->id = in->pDU_Session_ID;
  pduSetup->numDRBSetup = in->dRB_Setup_List_NG_RAN.list.count;
  // NG DL UP Transport Layer Information (M)
  CHECK_E1AP_DEC(e1_decode_up_tnl_info(&pduSetup->tl_info, &in->nG_DL_UP_TNL_Information));
  // DRB Setup List (1..<maxnoofDRBs>)
  for (int j = 0; j < in->dRB_Setup_List_NG_RAN.list.count; j++) {
    DRB_nGRAN_setup_t *drbSetup = pduSetup->DRBnGRanList + j;
    E1AP_DRB_Setup_Item_NG_RAN_t *drb = in->dRB_Setup_List_NG_RAN.list.array[j];
    // DRB ID (M)
    drbSetup->id = drb->dRB_ID;
    // UL UP Parameters (M)
    if ((drbSetup->numUpParam = decode_dl_up_parameters(drbSetup->UpParamList, &drb->uL_UP_Transport_Parameters)) < 0) {
      PRINT_ERROR("UL UP Parameters decoding failed\n");
      return false;
    }
    // Flow Setup List (M)
    drbSetup->numQosFlowSetup = drb->flow_Setup_List.list.count;
    for (int q = 0; q < drb->flow_Setup_List.list.count; q++) {
      qos_flow_list_t *qosflowSetup = &drbSetup->qosFlows[q];
      E1AP_QoS_Flow_Item_t *in_qosflowSetup = drb->flow_Setup_List.list.array[q];
      qosflowSetup->qfi = in_qosflowSetup->qoS_Flow_Identifier;
    }
  }
  // DRB Failed to Setup List (O)
  if (in->dRB_Failed_List_NG_RAN) {
    pduSetup->numDRBFailed = in->dRB_Failed_List_NG_RAN->list.count;
    for (int j = 0; j < in->dRB_Failed_List_NG_RAN->list.count; j++) {
      decode_drb_failed_item(&pduSetup->DRBnGRanFailedList[j], in->dRB_Failed_List_NG_RAN->list.array[j]);
    }
  }
  return true;
}

/** @brief Deep copy PDU Session Resource Setup item (Bearer Context Setup Response) */
static pdu_session_setup_t cp_pdu_session_setup_item(const pdu_session_setup_t *msg)
{
  pdu_session_setup_t cp = {
    .id = msg->id,
    .tl_info = msg->tl_info,
    .numDRBSetup = msg->numDRBSetup,
    .numDRBFailed = msg->numDRBFailed,
  };
  for (int j = 0; j < msg->numDRBSetup; j++)
    cp.DRBnGRanList[j] = msg->DRBnGRanList[j];
  for (int j = 0; j < msg->numDRBFailed; j++)
    cp.DRBnGRanFailedList[j] = msg->DRBnGRanFailedList[j];
  return cp;
}

/**
 * @brief Encode an item of the E1 PDU Session Resource Setup List (9.3.3.5 of 3GPP TS 38.463)
 */
static bool e1_encode_pdu_session_setup_item(E1AP_PDU_Session_Resource_Setup_Item_t *item, const pdu_session_setup_t *in)
{
  // PDU Session ID (M)
  item->pDU_Session_ID = in->id;
  // NG DL UP Transport Layer Information (M)
  item->nG_DL_UP_TNL_Information = e1_encode_up_tnl_info(&in->tl_info);
  // DRB Setup List (1..<maxnoofDRBs>)
  for (const DRB_nGRAN_setup_t *j = in->DRBnGRanList; j < in->DRBnGRanList + in->numDRBSetup; j++) {
    asn1cSequenceAdd(item->dRB_Setup_List_NG_RAN.list, E1AP_DRB_Setup_Item_NG_RAN_t, ieC3_1_1);
    // DRB ID (M)
    ieC3_1_1->dRB_ID = j->id;
    // UL UP Parameters (M)
    ieC3_1_1->uL_UP_Transport_Parameters = encode_dl_up_parameters(j->numUpParam, j->UpParamList);
    // Flow Setup List(M)
    for (const qos_flow_list_t *k = j->qosFlows; k < j->qosFlows + j->numQosFlowSetup; k++) {
      asn1cSequenceAdd(ieC3_1_1->flow_Setup_List.list, E1AP_QoS_Flow_Item_t, ieC3_1_1_1);
      ieC3_1_1_1->qoS_Flow_Identifier = k->qfi;
    }
  }
  // DRB Failed List (O)
  if (in->numDRBFailed > 0)
    item->dRB_Failed_List_NG_RAN = calloc_or_fail(1, sizeof(*item->dRB_Failed_List_NG_RAN));
  // DRB Failed Item
  for (const DRB_nGRAN_failed_t *j = in->DRBnGRanFailedList; j < in->DRBnGRanFailedList + in->numDRBFailed; j++) {
    asn1cSequenceAdd(item->dRB_Failed_List_NG_RAN->list, E1AP_DRB_Failed_Item_NG_RAN_t, ieC3_1_1);
    encode_drb_failed_item(ieC3_1_1, j);
  }
  return true;
}

/**
 * @brief Encode E1 Bearer Context Setup Response (9.2.2.2 of 3GPP TS 38.463)
 */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *in)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  // PDU Type
  pdu->present = E1AP_E1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, msg);
  msg->procedureCode = E1AP_ProcedureCode_id_bearerContextSetup;
  msg->criticality = E1AP_Criticality_reject;
  msg->value.present = E1AP_SuccessfulOutcome__value_PR_BearerContextSetupResponse;
  E1AP_BearerContextSetupResponse_t *out = &pdu->choice.successfulOutcome->value.choice.BearerContextSetupResponse;
  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupResponseIEs_t, ieC1);
  ieC1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ieC1->criticality = E1AP_Criticality_reject;
  ieC1->value.present = E1AP_BearerContextSetupResponseIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ieC1->value.choice.GNB_CU_CP_UE_E1AP_ID = in->gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupResponseIEs_t, ieC2);
  ieC2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ieC2->criticality = E1AP_Criticality_reject;
  ieC2->value.present = E1AP_BearerContextSetupResponseIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ieC2->value.choice.GNB_CU_CP_UE_E1AP_ID = in->gNB_cu_up_ue_id;
  // Message Type (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupResponseIEs_t, ieC3);
  ieC3->id = E1AP_ProtocolIE_ID_id_System_BearerContextSetupResponse;
  ieC3->criticality = E1AP_Criticality_reject;
  ieC3->value.present = E1AP_BearerContextSetupResponseIEs__value_PR_System_BearerContextSetupResponse;
  // CHOICE System: default NG-RAN (M)
  ieC3->value.choice.System_BearerContextSetupResponse.present =
      E1AP_System_BearerContextSetupResponse_PR_nG_RAN_BearerContextSetupResponse;
  E1AP_ProtocolIE_Container_4932P22_t *msgNGRAN_list = calloc_or_fail(1, sizeof(*msgNGRAN_list));
  ieC3->value.choice.System_BearerContextSetupResponse.choice.nG_RAN_BearerContextSetupResponse =
      (struct E1AP_ProtocolIE_Container *)msgNGRAN_list;
  asn1cSequenceAdd(msgNGRAN_list->list, E1AP_NG_RAN_BearerContextSetupResponse_t, msgNGRAN);
  msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Setup_List;
  msgNGRAN->criticality = E1AP_Criticality_reject;
  msgNGRAN->value.present = E1AP_NG_RAN_BearerContextSetupResponse__value_PR_PDU_Session_Resource_Setup_List;
  E1AP_PDU_Session_Resource_Setup_List_t *pduSetup = &msgNGRAN->value.choice.PDU_Session_Resource_Setup_List;
  for (const pdu_session_setup_t *i = in->pduSession; i < in->pduSession + in->numPDUSessions; i++) {
    asn1cSequenceAdd(pduSetup->list, E1AP_PDU_Session_Resource_Setup_Item_t, ieC3_1);
    CHECK_E1AP_DEC(e1_encode_pdu_session_setup_item(ieC3_1, i));
  }
  return pdu;
}

/**
 * @brief Decode E1 Bearer Context Setup Response (9.2.2.2 of 3GPP TS 38.463)
 */
bool decode_E1_bearer_context_setup_response(const E1AP_E1AP_PDU_t *pdu, e1ap_bearer_setup_resp_t *out)
{
  // Check message type
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_successfulOutcome);
  _EQ_CHECK_LONG(pdu->choice.successfulOutcome->procedureCode, E1AP_ProcedureCode_id_bearerContextSetup);
  _EQ_CHECK_INT(pdu->choice.successfulOutcome->value.present, E1AP_SuccessfulOutcome__value_PR_BearerContextSetupResponse);

  const E1AP_BearerContextSetupResponse_t *in = &pdu->choice.successfulOutcome->value.choice.BearerContextSetupResponse;
  E1AP_BearerContextSetupResponseIEs_t *ie;
  // Check mandatory IEs first
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_System_BearerContextSetupResponse, true);
  // Loop over all IEs
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupResponseIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupResponseIEs__value_PR_GNB_CU_UP_UE_E1AP_ID);
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_System_BearerContextSetupResponse:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextSetupResponseIEs__value_PR_System_BearerContextSetupResponse);
        switch (ie->value.choice.System_BearerContextSetupResponse.present) {
          case E1AP_System_BearerContextSetupResponse_PR_nG_RAN_BearerContextSetupResponse: {
            E1AP_ProtocolIE_Container_4932P22_t *msgNGRAN_list =
                (E1AP_ProtocolIE_Container_4932P22_t *)
                    ie->value.choice.System_BearerContextSetupResponse.choice.nG_RAN_BearerContextSetupResponse;
            _EQ_CHECK_INT(msgNGRAN_list->list.count, 1);
            E1AP_NG_RAN_BearerContextSetupResponse_t *msgNGRAN = msgNGRAN_list->list.array[0];
            _EQ_CHECK_LONG(msgNGRAN->id, E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Setup_List);
            _EQ_CHECK_INT(msgNGRAN->value.present,
                          E1AP_NG_RAN_BearerContextSetupResponse__value_PR_PDU_Session_Resource_Setup_List);
            E1AP_PDU_Session_Resource_Setup_List_t *pduSetupList = &msgNGRAN->value.choice.PDU_Session_Resource_Setup_List;
            out->numPDUSessions = pduSetupList->list.count;
            DevAssert(out->numPDUSessions > 0 && out->numPDUSessions <= NR_MAX_NB_PDU_SESSIONS);
            // Allocate array for PDU sessions
            out->pduSession = calloc_or_fail(out->numPDUSessions, sizeof(*out->pduSession));
            for (int i = 0; i < pduSetupList->list.count; i++) {
              pdu_session_setup_t *pduSetup = out->pduSession + i;
              E1AP_PDU_Session_Resource_Setup_Item_t *pdu_session = pduSetupList->list.array[i];
              CHECK_E1AP_DEC(e1_decode_pdu_session_setup_item(pduSetup, pdu_session));
            }
          } break;

          case E1AP_System_BearerContextSetupResponse_PR_e_UTRAN_BearerContextSetupResponse:
            PRINT_ERROR("DRB Setup List E-UTRAN not supported in E1 Setup Response\n");
            return false;
            break;

          default:
            PRINT_ERROR("Unknown DRB Setup List in E1 Setup Response\n");
            return false;
            break;
        }
        break;

      default:
        PRINT_ERROR("Handle for this IE is not implemented (or) invalid IE detected\n");
        return false;
        break;
    }
  }
  return true;
}

/**
 * @brief Deep copy function for E1 BEARER CONTEXT SETUP RESPONSE
 */
e1ap_bearer_setup_resp_t cp_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *msg)
{
  e1ap_bearer_setup_resp_t cp = {0};
  cp.gNB_cu_cp_ue_id = msg->gNB_cu_cp_ue_id;
  cp.gNB_cu_up_ue_id = msg->gNB_cu_up_ue_id;
  cp.numPDUSessions = msg->numPDUSessions;
  if (msg->numPDUSessions > 0 && msg->pduSession != NULL) {
    cp.pduSession = calloc_or_fail(msg->numPDUSessions, sizeof(*cp.pduSession));
    for (int i = 0; i < msg->numPDUSessions; i++)
      cp.pduSession[i] = cp_pdu_session_setup_item(&msg->pduSession[i]);
  }
  return cp;
}

/**
 * @brief E1AP Bearer Context Setup Response equality check
 */
bool eq_bearer_context_setup_response(const e1ap_bearer_setup_resp_t *a, const e1ap_bearer_setup_resp_t *b)
{
  // Check basic members
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->numPDUSessions, b->numPDUSessions);
  // Check PDU Sessions Setup
  if (a->numPDUSessions != b->numPDUSessions)
    return false;
  for (int i = 0; i < a->numPDUSessions; i++) {
    const pdu_session_setup_t *ps_a = &a->pduSession[i];
    const pdu_session_setup_t *ps_b = &b->pduSession[i];
    _EQ_CHECK_LONG(ps_a->id, ps_b->id);
    eq_up_tl_info(&ps_a->tl_info, &ps_b->tl_info);
    _EQ_CHECK_INT(ps_a->numDRBSetup, ps_b->numDRBSetup);
    _EQ_CHECK_INT(ps_a->numDRBFailed, ps_b->numDRBFailed);
    // Check DRB Setup
    for (int j = 0; j < ps_a->numDRBSetup; j++) {
      const DRB_nGRAN_setup_t *drb_a = &ps_a->DRBnGRanList[j];
      const DRB_nGRAN_setup_t *drb_b = &ps_b->DRBnGRanList[j];
      _EQ_CHECK_LONG(drb_a->id, drb_b->id);
      _EQ_CHECK_INT(drb_a->numUpParam, drb_b->numUpParam);
      _EQ_CHECK_INT(drb_a->numQosFlowSetup, drb_b->numQosFlowSetup);
    }
    // Check DRB Failed
    for (int j = 0; j < ps_a->numDRBFailed; j++) {
      const DRB_nGRAN_failed_t *drbf_a = &ps_a->DRBnGRanFailedList[j];
      const DRB_nGRAN_failed_t *drbf_b = &ps_b->DRBnGRanFailedList[j];
      _EQ_CHECK_LONG(drbf_a->id, drbf_b->id);
      _EQ_CHECK_INT(drbf_a->cause.type, drbf_b->cause.type);
      _EQ_CHECK_INT(drbf_a->cause.value, drbf_b->cause.value);
    }
  }
  return true;
}

/**
 * @brief E1AP Bearer Context Setup Response memory management
 */
void free_e1ap_context_setup_response(const e1ap_bearer_setup_resp_t *msg)
{
  free(msg->pduSession);
}

/* =====================================
 *   E1AP Bearer Context Setup Failure
 * ===================================== */

/**
 * @brief Bearer Context Setup Failure encoding (9.2.2.3, 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_unsuccessfulOutcome;

  // Type (M)
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextSetup;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_UnsuccessfulOutcome__value_PR_BearerContextSetupFailure;
  E1AP_BearerContextSetupFailure_t *out = &pdu->choice.unsuccessfulOutcome->value.choice.BearerContextSetupFailure;

  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupFailureIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextSetupFailureIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;

  // gNB-CU-UP UE E1AP ID (O)
  if (msg->gNB_cu_up_ue_id) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupFailureIEs_t, ie2);
    ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
    ie2->criticality = E1AP_Criticality_reject;
    ie2->value.present = E1AP_BearerContextSetupFailureIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
    ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = *msg->gNB_cu_up_ue_id;
  }

  // Cause (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextSetupFailureIEs_t, ie3);
  ie3->id = E1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = E1AP_Criticality_reject;
  ie3->value.present = E1AP_BearerContextSetupFailureIEs__value_PR_Cause;
  ie3->value.choice.Cause = e1_encode_cause_ie(&msg->cause);

  return pdu;
}

/**
 * @brief Bearer Context Setup Failure decoding (9.2.2.3, 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
bool decode_E1_bearer_context_setup_failure(e1ap_bearer_context_setup_failure_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_unsuccessfulOutcome);
  E1AP_UnsuccessfulOutcome_t *type = pdu->choice.unsuccessfulOutcome;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextSetup);
  _EQ_CHECK_INT(type->value.present, E1AP_UnsuccessfulOutcome__value_PR_BearerContextSetupFailure);
  const E1AP_BearerContextSetupFailure_t *in = &type->value.choice.BearerContextSetupFailure;

  // Check mandatory IEs first
  E1AP_BearerContextSetupFailureIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextSetupFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        out->gNB_cu_up_ue_id = malloc_or_fail(sizeof(*out->gNB_cu_up_ue_id));
        *out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_Cause:
        out->cause = e1_decode_cause_ie(&ie->value.choice.Cause);
        break;
      default:
        break;
    }
  }
  return true;
}

/** @brief Bearer Context Setup Failure deep copy */
e1ap_bearer_context_setup_failure_t cp_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg)
{
  // Shallow copy
  e1ap_bearer_context_setup_failure_t cp = *msg;
  if (msg->gNB_cu_up_ue_id) {
    cp.gNB_cu_up_ue_id = malloc_or_fail(sizeof(*cp.gNB_cu_up_ue_id));
    *cp.gNB_cu_up_ue_id = *msg->gNB_cu_up_ue_id;
  }
  return cp;
}

/** @brief Bearer Context Setup Failure equality check */
bool eq_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *a, const e1ap_bearer_context_setup_failure_t *b)
{
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_OPTIONAL_IE(a, b, gNB_cu_up_ue_id, _EQ_CHECK_INT);
  _EQ_CHECK_INT(a->cause.type, b->cause.type);
  _EQ_CHECK_INT(a->cause.value, b->cause.value);
  return true;
}

/** @brief Bearer Context Setup Failure equality check */
void free_e1_bearer_context_setup_failure(const e1ap_bearer_context_setup_failure_t *msg)
{
  free(msg->gNB_cu_up_ue_id);
  // do nothing
}

/* ===============================
 *   E1AP Bearer Context Release
 * =============================== */

/** @brief Bearer Context Release Command encoding (9.2.2.9, 3GPP TS 38.463)
 *         gNB-CU-CP → gNB-CU-UP */
E1AP_E1AP_PDU_t *encode_e1_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;

  // Type (M)
  asn1cCalloc(pdu->choice.initiatingMessage, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextRelease;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_InitiatingMessage__value_PR_BearerContextReleaseCommand;
  E1AP_BearerContextReleaseCommand_t *out = &pdu->choice.initiatingMessage->value.choice.BearerContextReleaseCommand;

  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextReleaseCommandIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextReleaseCommandIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;

  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextReleaseCommandIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextReleaseCommandIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = msg->gNB_cu_up_ue_id;

  // Cause (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextReleaseCommandIEs_t, ie3);
  ie3->id = E1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = E1AP_Criticality_reject;
  ie3->value.present = E1AP_BearerContextReleaseCommandIEs__value_PR_Cause;
  ie3->value.choice.Cause = e1_encode_cause_ie(&msg->cause);

  return pdu;
}

/** @brief Bearer Context Release Command decoding (9.2.2.9, 3GPP TS 38.463)
 *         gNB-CU-CP → gNB-CU-UP */
bool decode_e1_bearer_context_release_command(e1ap_bearer_release_cmd_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_initiatingMessage);
  E1AP_InitiatingMessage_t *type = pdu->choice.initiatingMessage;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextRelease);
  _EQ_CHECK_INT(type->value.present, E1AP_InitiatingMessage__value_PR_BearerContextReleaseCommand);
  const E1AP_BearerContextReleaseCommand_t *in = &type->value.choice.BearerContextReleaseCommand;

  // Check mandatory IEs
  E1AP_BearerContextReleaseCommandIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextReleaseCommandIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextReleaseCommandIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Cause, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextReleaseCommandIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_Cause:
        out->cause = e1_decode_cause_ie(&ie->value.choice.Cause);
        break;
      default:
        PRINT_ERROR("%s decoding failure: IE %ld is invalid\n", __func__, ie->id);
        return false;
        break;
    }
  }
  return true;
}

/** @brief Bearer Context Release Command equality check */
bool eq_bearer_context_release_command(const e1ap_bearer_release_cmd_t *a, const e1ap_bearer_release_cmd_t *b)
{
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->cause.type, b->cause.type);
  _EQ_CHECK_INT(a->cause.value, b->cause.value);
  return true;
}

/** @brief Bearer Context Release Command deep copy */
e1ap_bearer_release_cmd_t cp_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg)
{
  // Shallow copy only
  e1ap_bearer_release_cmd_t cp = *msg;
  return cp;
}

/** @brief Bearer Context Release Command equality check */
void free_e1_bearer_context_release_command(const e1ap_bearer_release_cmd_t *msg)
{
  UNUSED(msg);
  // do nothing
}

/**
 * @brief Bearer Context Release Complete encoding (9.2.2.10, 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
E1AP_E1AP_PDU_t *encode_e1_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *cplt)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_successfulOutcome;
  // Message Type (M)
  asn1cCalloc(pdu->choice.successfulOutcome, msg);
  msg->procedureCode = E1AP_ProcedureCode_id_bearerContextRelease;
  msg->criticality = E1AP_Criticality_reject;
  msg->value.present = E1AP_SuccessfulOutcome__value_PR_BearerContextReleaseComplete;

  E1AP_BearerContextReleaseComplete_t *out = &msg->value.choice.BearerContextReleaseComplete;
  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextReleaseCompleteIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextReleaseCompleteIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = cplt->gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextReleaseCompleteIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextReleaseCompleteIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = cplt->gNB_cu_up_ue_id;
  return pdu;
}

/**
 * @brief Bearer Context Release Complete decoding (9.2.2.10, 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
bool decode_e1_bearer_context_release_complete(e1ap_bearer_release_cplt_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_successfulOutcome);
  E1AP_SuccessfulOutcome_t *type = pdu->choice.successfulOutcome;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextRelease);
  _EQ_CHECK_INT(type->value.present, E1AP_SuccessfulOutcome__value_PR_BearerContextReleaseComplete);
  const E1AP_BearerContextReleaseComplete_t *in = &type->value.choice.BearerContextReleaseComplete;

  // Check mandatory IEs
  E1AP_BearerContextReleaseCompleteIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextReleaseCompleteIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextReleaseCompleteIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      default:
        PRINT_ERROR("%s decoding failure: IE %ld is not handled or invalid\n", __func__, ie->id);
        break;
    }
  }
  return true;
}

/** @brief Bearer Context Release Complete equality check */
bool eq_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *a, const e1ap_bearer_release_cplt_t *b)
{
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  return true;
}

/** @brief Bearer Context Release Complete deep copy */
e1ap_bearer_release_cplt_t cp_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *src)
{
  e1ap_bearer_release_cplt_t dst = *src;
  return dst;
}

/** @brief Bearer Context Release Complete equality check */
void free_e1_bearer_context_release_complete(const e1ap_bearer_release_cplt_t *msg)
{
  UNUSED(msg);
  // No dynamic memory, do nothing
}

/* ============================================
 *   E1AP Bearer Context Modification Request
 * ============================================ */

/** @brief Encode PDU Session Item to modify */
static E1AP_PDU_Session_Resource_To_Modify_Item_t e1_encode_pdu_session_to_mod_item(const pdu_session_to_mod_t *in)
{
  E1AP_PDU_Session_Resource_To_Modify_Item_t out = {0};
  out.pDU_Session_ID = in->sessionId;
  if (in->numDRB2Modify > 0) {
    out.dRB_To_Modify_List_NG_RAN = calloc_or_fail(1, sizeof(*out.dRB_To_Modify_List_NG_RAN));
  }
  for (const DRB_nGRAN_to_mod_t *j = in->DRBnGRanModList; j < in->DRBnGRanModList + in->numDRB2Modify; j++) {
    asn1cSequenceAdd(out.dRB_To_Modify_List_NG_RAN->list, E1AP_DRB_To_Modify_Item_NG_RAN_t, drb2Mod);
    drb2Mod->dRB_ID = j->id;
    // PDCP Config (O)
    if (j->pdcp_config) {
      asn1cCalloc(drb2Mod->pDCP_Configuration, pDCP_Configuration);
      *pDCP_Configuration = e1_encode_pdcp_config(j->pdcp_config);
    }
    // SDAP Config (O)
    if (j->sdap_config) {
      asn1cCalloc(drb2Mod->sDAP_Configuration, sDAP_Configuration);
      *sDAP_Configuration = e1_encode_sdap_config(j->sdap_config);
    }
    // PDCP SN Status Request (O)
    if (j->pdcp_sn_status_requested) {
      asn1cCalloc(drb2Mod->pDCP_SN_Status_Request, pDCP_SN_Status_Request);
      *pDCP_SN_Status_Request = E1AP_PDCP_SN_Status_Request_requested;
    }
    // PDCP Status Information (O)
    if (j->pdcp_status) {
      asn1cCalloc(drb2Mod->pdcp_SN_Status_Information, pdcp);
      *pdcp = encode_pdcp_status_info(j->pdcp_status);
    }
    // DL UP TNL parameters (O)
    if (j->numDlUpParam > 0) {
      asn1cCalloc(drb2Mod->dL_UP_Parameters, DL_UP_Param_List);
      for (const up_params_t *k = j->DlUpParamList; k < j->DlUpParamList + j->numDlUpParam; k++) {
        asn1cSequenceAdd(DL_UP_Param_List->list, E1AP_UP_Parameters_Item_t, DL_UP_Param);
        DL_UP_Param->uP_TNL_Information = e1_encode_up_tnl_info(&k->tl_info);
        DL_UP_Param->cell_Group_ID = k->cell_group_id;
      }
    }
    // QoS Flows to setup (O)
    for (const qos_flow_to_setup_t *k = j->qosFlows; k < j->qosFlows + j->numQosFlowsMod; k++) {
      asn1cCalloc(drb2Mod->flow_Mapping_Information, flow_Mapping_Information);
      asn1cSequenceAdd(flow_Mapping_Information->list, E1AP_QoS_Flow_QoS_Parameter_Item_t, ie1_2);
      *ie1_2 = e1_encode_qos_flow_to_setup(k);
    }
  }
  // DRB To Remove List (O)
  if (in->n_drb_to_remove > 0) {
    asn1cCalloc(out.dRB_To_Remove_List_NG_RAN, drb2Remove_List);
    for (int r = 0; r < in->n_drb_to_remove; r++) {
      asn1cSequenceAdd(drb2Remove_List->list, E1AP_DRB_To_Remove_Item_NG_RAN_t, drb2Rem);
      drb2Rem->dRB_ID = in->drbs_to_remove[r].id;
    }
  }
  return out;
}

/** @brief Common encoding for the NG-RAN System IE */
static E1AP_NG_RAN_BearerContextModificationRequest_t *encode_ng_ran_to_mod(E1AP_BearerContextModificationRequestIEs_t *ie)
{
  ie->id = E1AP_ProtocolIE_ID_id_System_BearerContextModificationRequest;
  ie->criticality = E1AP_Criticality_reject;
  ie->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_System_BearerContextModificationRequest;
  E1AP_System_BearerContextModificationRequest_t *reqIE = &ie->value.choice.System_BearerContextModificationRequest;
  reqIE->present = E1AP_System_BearerContextModificationRequest_PR_nG_RAN_BearerContextModificationRequest;
  E1AP_ProtocolIE_Container_4932P26_t *msgNGRAN_list = calloc_or_fail(1, sizeof(*msgNGRAN_list));
  reqIE->choice.nG_RAN_BearerContextModificationRequest = (struct E1AP_ProtocolIE_Container *)msgNGRAN_list;
  asn1cSequenceAdd(msgNGRAN_list->list, E1AP_NG_RAN_BearerContextModificationRequest_t, msgNGRAN);
  return msgNGRAN;
}

/** @brief Encode PDU Session Resource To Setup Modification List item */
static E1AP_PDU_Session_Resource_To_Setup_Mod_Item_t e1_encode_pdu_session_to_setup_mod_item(const pdu_session_to_setup_t *in)
{
  E1AP_PDU_Session_Resource_To_Setup_Mod_Item_t item = {0};
  // PDU Session ID (M)
  item.pDU_Session_ID = in->sessionId;
  // PDU Session Type (M)
  item.pDU_Session_Type = in->sessionType;
  // SNSSAI (M)
  item.sNSSAI = e1_encode_snssai(&in->nssai);
  // Security indication (M)
  item.securityIndication = e1_encode_security_indication(&in->securityIndication);
  // NG UL UP Transport Layer Information (M)
  item.nG_UL_UP_TNL_Information = e1_encode_up_tnl_info(&in->UP_TL_information);
  // DRB to setup
  for (const DRB_nGRAN_to_setup_t *j = in->DRBnGRanList; j < in->DRBnGRanList + in->numDRB2Setup; j++) {
    asn1cSequenceAdd(item.dRB_To_Setup_Mod_List_NG_RAN.list, E1AP_DRB_To_Setup_Mod_Item_NG_RAN_t, ie1);
    // DRB ID (M)
    ie1->dRB_ID = j->id;
    // SDAP Config (M)
    ie1->sDAP_Configuration = e1_encode_sdap_config(&j->sdap_config);
    // PDCP Config (M)
    ie1->pDCP_Configuration = e1_encode_pdcp_config(&j->pdcp_config);
    // Cell Group Information (M)
    for (const cell_group_id_t *k = j->cellGroupList; k < j->cellGroupList + j->numCellGroups; k++) {
      asn1cSequenceAdd(ie1->cell_Group_Information.list, E1AP_Cell_Group_Information_Item_t, ie1_1);
      ie1_1->cell_Group_ID = *k;
    }
    // QoS Flows Information To Be Setup (M)
    for (const qos_flow_to_setup_t *k = j->qosFlows; k < j->qosFlows + j->numQosFlow2Setup; k++) {
      asn1cSequenceAdd(ie1->flow_Mapping_Information.list, E1AP_QoS_Flow_QoS_Parameter_Item_t, ie1_2);
      *ie1_2 = e1_encode_qos_flow_to_setup(k);
    }
  }
  return item;
}

/** @brief Encode Bit Rate IE (3GPP TS 38.463, 9.3.1.20) */
static bool e1_encode_bit_rate(E1AP_BitRate_t *out, uint64_t in)
{
  if (in > NR_MAX_BIT_RATE || asn_uint642INTEGER(out, in) != 0) {
    PRINT_ERROR("Failed to encode E1AP BitRate\n");
    return false;
  }
  return true;
}

/** @brief Decode Bit Rate IE (3GPP TS 38.463, 9.3.1.20) */
static bool e1_decode_bit_rate(uint64_t *out, const E1AP_BitRate_t *in)
{
  int64_t value;
  if (asn_INTEGER2int64(in, &value) != 0 || value < 0 || value > NR_MAX_BIT_RATE) {
    PRINT_ERROR("Unsupported E1AP BitRate\n");
    return false;
  }
  *out = value;
  return true;
}

/**
 * @brief Encodes the E1 Bearer Modification Request message
 */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_mod_request(const e1ap_bearer_mod_req_t *in)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  // PDU Type
  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, msg);
  msg->procedureCode = E1AP_ProcedureCode_id_bearerContextModification;
  msg->criticality = E1AP_Criticality_reject;
  msg->value.present = E1AP_InitiatingMessage__value_PR_BearerContextModificationRequest;

  E1AP_BearerContextModificationRequest_t *out = &msg->value.choice.BearerContextModificationRequest;

  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = in->gNB_cu_cp_ue_id;

  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = in->gNB_cu_up_ue_id;

  // Security Information (O)
  if (in->secInfo) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie3);
    ie3->id = E1AP_ProtocolIE_ID_id_SecurityInformation;
    ie3->criticality = E1AP_Criticality_reject;
    ie3->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_SecurityInformation;
    ie3->value.choice.SecurityInformation = e1_encode_security_info(in->secInfo);
  }

  // UE DL Aggregate Maximum Bit Rate (O)
  if (in->ueDlAggMaxBitRate) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie4);
    ie4->id = E1AP_ProtocolIE_ID_id_UEDLAggregateMaximumBitRate;
    ie4->criticality = E1AP_Criticality_reject;
    ie4->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_BitRate;
    if (!e1_encode_bit_rate(&ie4->value.choice.BitRate, *in->ueDlAggMaxBitRate)) {
      AssertFatal(0 == 1, "Failed to encode UE DL Aggregate Maximum Bit Rate\n");
    }
  }

  // UE DL Maximum Integrity Protected Data Rate (O)
  if (in->ueDlMaxIPBitRate) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie5);
    ie5->id = E1AP_ProtocolIE_ID_id_UEDLMaximumIntegrityProtectedDataRate;
    ie5->criticality = E1AP_Criticality_reject;
    ie5->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_BitRate_1;
    if (!e1_encode_bit_rate(&ie5->value.choice.BitRate_1, *in->ueDlMaxIPBitRate)) {
      AssertFatal(0 == 1, "Failed to encode UE DL Maximum Integrity Protected Data Rate\n");
    }
  }

  // Bearer Context Status Change (O)
  if (in->bearerContextStatus && *in->bearerContextStatus == BEARER_SUSPEND) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie6);
    ie6->id = E1AP_ProtocolIE_ID_id_BearerContextStatusChange;
    ie6->criticality = E1AP_Criticality_reject;
    ie6->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_BearerContextStatusChange;
    ie6->value.choice.BearerContextStatusChange = E1AP_BearerContextStatusChange_suspend;
  }

  // UE Inactivity Timer (O)
  if (in->inactivityTimer) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie7);
    ie7->id = E1AP_ProtocolIE_ID_id_UE_Inactivity_Timer;
    ie7->criticality = E1AP_Criticality_ignore;
    ie7->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_Inactivity_Timer;
    ie7->value.choice.Inactivity_Timer = *in->inactivityTimer;
  }

  // Data Discard Required (O)
  if (in->dataDiscardRequired) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, ie9);
    ie9->id = E1AP_ProtocolIE_ID_id_DataDiscardRequired;
    ie9->criticality = E1AP_Criticality_ignore;
    ie9->value.present = E1AP_BearerContextModificationRequestIEs__value_PR_DataDiscardRequired;
    ie9->value.choice.DataDiscardRequired = in->dataDiscardRequired;
  }

  // NG-RAN PDU Session Resource To Setup List (O)
  if (in->numPDUSessions > 0 && in->pduSession != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, setupList);
    E1AP_NG_RAN_BearerContextModificationRequest_t *msgNGRAN = encode_ng_ran_to_mod(setupList);
    msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Setup_Mod_List;
    msgNGRAN->criticality = E1AP_Criticality_reject;
    msgNGRAN->value.present = E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Setup_Mod_List;
    E1AP_PDU_Session_Resource_To_Setup_Mod_List_t *pdu2Setup = &msgNGRAN->value.choice.PDU_Session_Resource_To_Setup_Mod_List;
    for (int i = 0; i < in->numPDUSessions; i++) {
      const pdu_session_to_setup_t *pdu_session = &in->pduSession[i];
      asn1cSequenceAdd(pdu2Setup->list, E1AP_PDU_Session_Resource_To_Setup_Mod_Item_t, item);
      *item = e1_encode_pdu_session_to_setup_mod_item(pdu_session);
    }
  }

  // NG-RAN PDU Session Resource To Modify List (O)
  if (in->numPDUSessionsMod > 0 && in->pduSessionMod != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, modList);
    E1AP_NG_RAN_BearerContextModificationRequest_t *msgNGRAN = encode_ng_ran_to_mod(modList);
    msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Modify_List;
    msgNGRAN->criticality = E1AP_Criticality_reject;
    msgNGRAN->value.present = E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Modify_List;
    E1AP_PDU_Session_Resource_To_Modify_List_t *pdu2Setup = &msgNGRAN->value.choice.PDU_Session_Resource_To_Modify_List;
    for (const pdu_session_to_mod_t *i = in->pduSessionMod; i < in->pduSessionMod + in->numPDUSessionsMod; i++) {
      asn1cSequenceAdd(pdu2Setup->list, E1AP_PDU_Session_Resource_To_Modify_Item_t, ieC3_1);
      *ieC3_1 = e1_encode_pdu_session_to_mod_item(i);
    }
  }

  // NG-RAN PDU Session Resource To Remove List (O)
  if (in->numPDUSessionsRem > 0 && in->pduSessionRem != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequestIEs_t, remList);
    E1AP_NG_RAN_BearerContextModificationRequest_t *msgNGRAN = encode_ng_ran_to_mod(remList);
    msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Remove_List;
    msgNGRAN->criticality = E1AP_Criticality_reject;
    msgNGRAN->value.present = E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Remove_List;
    E1AP_PDU_Session_Resource_To_Remove_List_t *pdu2Remove = &msgNGRAN->value.choice.PDU_Session_Resource_To_Remove_List;
    for (const pdu_session_to_remove_t *i = in->pduSessionRem; i < in->pduSessionRem + in->numPDUSessionsRem; i++) {
      asn1cSequenceAdd(pdu2Remove->list, E1AP_PDU_Session_Resource_To_Remove_Item_t, ieC4_1);
      ieC4_1->pDU_Session_ID = i->sessionId;
    }
  }
  return pdu;
}

/** @brief Decode PDU Session Item to Setup (Bearer Context Modification Request) */
static bool e1_decode_pdu_session_to_setup_mod_item(pdu_session_to_setup_t *out,
                                                    const E1AP_PDU_Session_Resource_To_Setup_Mod_Item_t *item)
{
  // PDU Session ID (M)
  out->sessionId = item->pDU_Session_ID;
  // PDU Session Type (M)
  out->sessionType = item->pDU_Session_Type;
  // S-NSSAI (M)
  CHECK_E1AP_DEC(e1_decode_snssai(&out->nssai, &item->sNSSAI));
  // Security Indication (M)
  CHECK_E1AP_DEC(e1_decode_security_indication(&out->securityIndication, &item->securityIndication));
  /* NG UL UP Transport Layer Information (M) */
  _EQ_CHECK_INT(item->nG_UL_UP_TNL_Information.present, E1AP_UP_TNL_Information_PR_gTPTunnel);
  CHECK_E1AP_DEC(e1_decode_up_tnl_info(&out->UP_TL_information, &item->nG_UL_UP_TNL_Information));
  /* DRB To Setup List ( > 1 item ) */
  const E1AP_DRB_To_Setup_Mod_List_NG_RAN_t *drb2SetupList = &item->dRB_To_Setup_Mod_List_NG_RAN;
  EQ_CHECK_GENERIC(drb2SetupList->list.count > 0, "%d", drb2SetupList->list.count, 0);
  out->numDRB2Setup = drb2SetupList->list.count;
  for (int j = 0; j < drb2SetupList->list.count; j++) {
    DRB_nGRAN_to_setup_t *drb = out->DRBnGRanList + j;
    const E1AP_DRB_To_Setup_Mod_Item_NG_RAN_t *drb2Setup = drb2SetupList->list.array[j];
    // DRB ID (M)
    drb->id = drb2Setup->dRB_ID;
    // SDAP Configuration (M)
    CHECK_E1AP_DEC(e1_decode_sdap_config(&drb->sdap_config, &drb2Setup->sDAP_Configuration));
    // PDCP Configuration (M)
    CHECK_E1AP_DEC(e1_decode_pdcp_config(&drb->pdcp_config, &drb2Setup->pDCP_Configuration));
    // Cell Group Information (M)
    const E1AP_Cell_Group_Information_t *cellGroupList = &drb2Setup->cell_Group_Information;
    EQ_CHECK_GENERIC(cellGroupList->list.count > 0, "%d", cellGroupList->list.count, 0);
    drb->numCellGroups = cellGroupList->list.count;
    for (int k = 0; k < cellGroupList->list.count; k++) {
      const E1AP_Cell_Group_Information_Item_t *cg2Setup = cellGroupList->list.array[k];
      // Cell Group ID
      drb->cellGroupList[k] = cg2Setup->cell_Group_ID;
    }
    // QoS Flows Information To Be Setup (M)
    const E1AP_QoS_Flow_QoS_Parameter_List_t *qos2SetupList = &drb2Setup->flow_Mapping_Information;
    drb->numQosFlow2Setup = qos2SetupList->list.count;
    for (int k = 0; k < qos2SetupList->list.count; k++) {
      CHECK_E1AP_DEC(e1_decode_qos_flow_to_setup(drb->qosFlows + k, qos2SetupList->list.array[k]));
    }
  }
  return true;
}


/** @brief Decode PDU Session Item to modify (Bearer Context Modification Request) */
static bool e1_decode_pdu_session_to_mod_item(pdu_session_to_mod_t *out, const E1AP_PDU_Session_Resource_To_Modify_Item_t *in)
{
  // PDU Session ID
  out->sessionId = in->pDU_Session_ID;
  // DRB to modify list
  E1AP_DRB_To_Modify_List_NG_RAN_t *drb2ModList = in->dRB_To_Modify_List_NG_RAN;
  out->numDRB2Modify = drb2ModList->list.count;
  for (int j = 0; j < drb2ModList->list.count; j++) {
    DRB_nGRAN_to_mod_t *drb = out->DRBnGRanModList + j;
    // DRB to modify item
    E1AP_DRB_To_Modify_Item_NG_RAN_t *drb2Mod = drb2ModList->list.array[j];
    drb->id = drb2Mod->dRB_ID;
    // PDCP Config (O)
    if (drb2Mod->pDCP_Configuration) {
      drb->pdcp_config = calloc_or_fail(1, sizeof(*drb->pdcp_config));
      CHECK_E1AP_DEC(e1_decode_pdcp_config(drb->pdcp_config, drb2Mod->pDCP_Configuration));
    }
    // SDAP Config (O)
    if (drb2Mod->sDAP_Configuration) {
      drb->sdap_config = malloc_or_fail(sizeof(*drb->sdap_config));
      CHECK_E1AP_DEC(e1_decode_sdap_config(drb->sdap_config, drb2Mod->sDAP_Configuration));
    }
    // PDCP SN Status Request (O)
    if (drb2Mod->pDCP_SN_Status_Request && *drb2Mod->pDCP_SN_Status_Request == E1AP_PDCP_SN_Status_Request_requested) {
      drb->pdcp_sn_status_requested = true;
    }
    // PDCP SN Status Information (O)
    if (drb2Mod->pdcp_SN_Status_Information) {
      drb->pdcp_status = malloc_or_fail(sizeof(*drb->pdcp_status));
      *drb->pdcp_status = decode_pdcp_status_info(drb2Mod->pdcp_SN_Status_Information);
    }
    // DL UP TNL parameters (O)
    if (drb2Mod->dL_UP_Parameters) {
      E1AP_UP_Parameters_t *dl_up_paramList = drb2Mod->dL_UP_Parameters;
      if((drb->numDlUpParam = decode_dl_up_parameters(drb->DlUpParamList, dl_up_paramList)) < 0) {
        PRINT_ERROR("Failed to decode DL UP TNL parameters\n");
        return false;
      }
    }
    // QoS Flows Information To Be Setup (O)
    if (drb2Mod->flow_Mapping_Information) {
      E1AP_QoS_Flow_QoS_Parameter_List_t *qos2SetupList = drb2Mod->flow_Mapping_Information;
      drb->numQosFlowsMod = qos2SetupList->list.count;
      for (int k = 0; k < qos2SetupList->list.count; k++) {
        CHECK_E1AP_DEC(e1_decode_qos_flow_to_setup(drb->qosFlows + k, qos2SetupList->list.array[k]));
      }
    }
  }
  // DRB To Remove List (O)
  if (in->dRB_To_Remove_List_NG_RAN) {
    E1AP_DRB_To_Remove_List_NG_RAN_t *rm = in->dRB_To_Remove_List_NG_RAN;
    out->n_drb_to_remove = rm->list.count;
    for (int r = 0; r < rm->list.count; r++) {
      E1AP_DRB_To_Remove_Item_NG_RAN_t *item = rm->list.array[r];
      out->drbs_to_remove[r].id = item->dRB_ID;
    }
  }
  return true;
}


/**
 * @brief E1 Bearer Modification Request message decoding
 */
bool decode_E1_bearer_context_mod_request(const E1AP_E1AP_PDU_t *pdu, e1ap_bearer_mod_req_t *out)
{
  DevAssert(pdu != NULL);
  // Validate message type
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_initiatingMessage);
  const E1AP_InitiatingMessage_t *im = pdu->choice.initiatingMessage;
  _EQ_CHECK_LONG(im->procedureCode, E1AP_ProcedureCode_id_bearerContextModification);
  _EQ_CHECK_INT(im->value.present, E1AP_InitiatingMessage__value_PR_BearerContextModificationRequest);

  const E1AP_BearerContextModificationRequest_t *in = &im->value.choice.BearerContextModificationRequest;
  E1AP_BearerContextModificationRequestIEs_t *ie = NULL;

  // Mandatory IEs
  // gNB-CU-CP UE E1AP ID (M)
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  // gNB-CU-UP UE E1AP ID (M)
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationRequestIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);

  // Loop through all IEs and extract
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];

    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequestIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequestIEs__value_PR_GNB_CU_UP_UE_E1AP_ID);
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_SecurityInformation:
        out->secInfo = malloc_or_fail(sizeof(*out->secInfo));
        CHECK_E1AP_DEC(e1_decode_security_info(out->secInfo, &ie->value.choice.SecurityInformation));
        break;

      case E1AP_ProtocolIE_ID_id_UEDLAggregateMaximumBitRate:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequestIEs__value_PR_BitRate);
        out->ueDlAggMaxBitRate = malloc_or_fail(sizeof(*out->ueDlAggMaxBitRate));
        CHECK_E1AP_DEC(e1_decode_bit_rate(out->ueDlAggMaxBitRate, &ie->value.choice.BitRate));
        break;

      case E1AP_ProtocolIE_ID_id_UEDLMaximumIntegrityProtectedDataRate:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequestIEs__value_PR_BitRate_1);
        out->ueDlMaxIPBitRate = malloc_or_fail(sizeof(*out->ueDlMaxIPBitRate));
        CHECK_E1AP_DEC(e1_decode_bit_rate(out->ueDlMaxIPBitRate, &ie->value.choice.BitRate_1));
        break;

      case E1AP_ProtocolIE_ID_id_BearerContextStatusChange:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequestIEs__value_PR_BearerContextStatusChange);
        if (ie->value.choice.BearerContextStatusChange == E1AP_BearerContextStatusChange_suspend) {
          out->bearerContextStatus = malloc_or_fail(sizeof(*out->bearerContextStatus));
          *out->bearerContextStatus = BEARER_SUSPEND;
        }
        break;

      case E1AP_ProtocolIE_ID_id_UE_Inactivity_Timer:
        out->inactivityTimer = malloc_or_fail(sizeof(*out->inactivityTimer));
        *out->inactivityTimer = ie->value.choice.Inactivity_Timer;
        break;

      case E1AP_ProtocolIE_ID_id_DataDiscardRequired:
        out->dataDiscardRequired = ie->value.choice.DataDiscardRequired;
        break;

      case E1AP_ProtocolIE_ID_id_System_BearerContextModificationRequest: {
        _EQ_CHECK_INT(ie->value.present,
                      E1AP_BearerContextModificationRequestIEs__value_PR_System_BearerContextModificationRequest);
        E1AP_System_BearerContextModificationRequest_t *modReq = &ie->value.choice.System_BearerContextModificationRequest;
        DevAssert(ie->value.choice.System_BearerContextModificationRequest.choice.nG_RAN_BearerContextModificationRequest != NULL);
        // System IE can contain either UTRAN or NG-RAN
        switch (modReq->present) {
          case E1AP_System_BearerContextModificationRequest_PR_NOTHING:
            PRINT_ERROR("System Bearer Context Setup Request: no choice present\n");
            break;
          case E1AP_System_BearerContextModificationRequest_PR_e_UTRAN_BearerContextModificationRequest:
            PRINT_ERROR("UTRAN in E1 Setup Modification Request not supported\n");
            return false;
            break;
          case E1AP_System_BearerContextModificationRequest_PR_nG_RAN_BearerContextModificationRequest:
            // NG-RAN
            AssertFatal(modReq->choice.nG_RAN_BearerContextModificationRequest != NULL,
                        "System_BearerContextSetupRequest->choice.nG_RAN_BearerContextSetupRequest shall not be null");
            E1AP_ProtocolIE_Container_4932P26_t *msgNGRAN_list =
                (E1AP_ProtocolIE_Container_4932P26_t *)modReq->choice.nG_RAN_BearerContextModificationRequest;
            // loop through all items in the NG-RAN list (to setup, to modify, etc)
            for (int i = 0; i < msgNGRAN_list->list.count; i++) {
              E1AP_NG_RAN_BearerContextModificationRequest_t *msgNGRAN = msgNGRAN_list->list.array[i];
              switch (msgNGRAN->id) {
                case E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Setup_Mod_List:
                  _EQ_CHECK_LONG(msgNGRAN->id, E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Setup_Mod_List);
                  _EQ_CHECK_INT(msgNGRAN->value.present,
                                E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Setup_Mod_List);
                  E1AP_PDU_Session_Resource_To_Setup_Mod_List_t *setupList =
                      &msgNGRAN->value.choice.PDU_Session_Resource_To_Setup_Mod_List;
                  // PDU Session Resource To Modify List
                  out->numPDUSessions = setupList->list.count;
                  DevAssert(out->numPDUSessions > 0 && out->numPDUSessions <= NR_MAX_NB_PDU_SESSIONS);
                  out->pduSession = calloc_or_fail(out->numPDUSessions, sizeof(*out->pduSession));
                  // Loop through all PDU sessions
                  for (int i = 0; i < setupList->list.count; i++) {
                    pdu_session_to_setup_t *pdu_session = out->pduSession + i;
                    E1AP_PDU_Session_Resource_To_Setup_Mod_Item_t *pdu2Setup = setupList->list.array[i];
                    CHECK_E1AP_DEC(e1_decode_pdu_session_to_setup_mod_item(pdu_session, pdu2Setup));
                  }
                  break;
                case E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Modify_List:
                  /* PDU Session Resource To Modify List (see 9.3.3.11 of TS 38.463) */
                  _EQ_CHECK_INT(msgNGRAN->value.present,
                                E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Modify_List);
                  E1AP_PDU_Session_Resource_To_Modify_List_t *modList = &msgNGRAN->value.choice.PDU_Session_Resource_To_Modify_List;
                  out->numPDUSessionsMod = modList->list.count;
                  DevAssert(out->numPDUSessionsMod > 0 && out->numPDUSessionsMod <= NR_MAX_NB_PDU_SESSIONS);
                  out->pduSessionMod = calloc_or_fail(out->numPDUSessionsMod, sizeof(*out->pduSessionMod));
                  // Loop through all PDU sessions
                  for (int i = 0; i < modList->list.count; i++) {
                    CHECK_E1AP_DEC(e1_decode_pdu_session_to_mod_item(out->pduSessionMod + i, modList->list.array[i]));
                  }
                  break;
                case E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Remove_List:
                  /* PDU Session Resource To Remove List (see 9.3.3.12 of TS 38.463) */
                  _EQ_CHECK_INT(msgNGRAN->value.present,
                                E1AP_NG_RAN_BearerContextModificationRequest__value_PR_PDU_Session_Resource_To_Remove_List);
                  E1AP_PDU_Session_Resource_To_Remove_List_t *remList = &msgNGRAN->value.choice.PDU_Session_Resource_To_Remove_List;
                  out->numPDUSessionsRem = remList->list.count;
                  // Allocate array for PDU sessions to remove
                  DevAssert(out->numPDUSessionsRem > 0 && out->numPDUSessionsRem <= NR_MAX_NB_PDU_SESSIONS);
                  out->pduSessionRem = calloc_or_fail(out->numPDUSessionsRem, sizeof(*out->pduSessionRem));
                  // Loop through all PDU sessions to remove
                  for (int i = 0; i < remList->list.count; i++) {
                    E1AP_PDU_Session_Resource_To_Remove_Item_t *remItem = remList->list.array[i];
                    pdu_session_to_remove_t *pduRem = &out->pduSessionRem[i];
                    pduRem->sessionId = remItem->pDU_Session_ID;
                  }
                  break;
                default:
                  PRINT_ERROR("Unknown msgNGRAN->id in E1 Setup Modification Request\n");
                  return false;
                  break;
              }
            }

            break;
          default:
            PRINT_ERROR("System Bearer Context Modification Request: No valid choice present.\n");
            break;
        }

        break;
      }

      default:
        PRINT_ERROR("%s: handle for this IE %ld is not implemented (or) invalid IE detected\n", __func__, ie->id);
        return false;
        break;
    }
  }

  return true;
}

/** @brief Deep copy for DRB Item to modify */
static DRB_nGRAN_to_mod_t cp_drb_to_mod_item(const DRB_nGRAN_to_mod_t *msg)
{
  DRB_nGRAN_to_mod_t cp = {0};
  // shallow copy
  cp = *msg;
  _E1_CP_OPTIONAL_IE(&cp, msg, sdap_config);
  _E1_CP_OPTIONAL_IE(&cp, msg, pdcp_config);
  _E1_CP_OPTIONAL_IE(&cp, msg, pdcp_status);
  return cp;
}

/** @brief Deep copy for PDU Session Item to modify */
static pdu_session_to_mod_t cp_pdu_session_to_mod_item(const pdu_session_to_mod_t *msg)
{
  pdu_session_to_mod_t cp = {0};
  // shallow copy
  cp = *msg;
  // DRB to setup list
  for (int j = 0; j < msg->numDRB2Setup; j++)
    cp.DRBnGRanList[j] = cp_drb_to_setup_item(&msg->DRBnGRanList[j]);
  // DRB to modify list
  for (int j = 0; j < msg->numDRB2Modify; j++)
    cp.DRBnGRanModList[j] = cp_drb_to_mod_item(&msg->DRBnGRanModList[j]);
  // DRB to remove list
  for (int r = 0; r < msg->n_drb_to_remove; r++)
    cp.drbs_to_remove[r] = msg->drbs_to_remove[r];
  _E1_CP_OPTIONAL_IE(&cp, msg, securityIndication);
  _E1_CP_OPTIONAL_IE(&cp, msg, UP_TL_information);
  return cp;
}

/** @brief Deep copy for Bearer Context Modification Request */
e1ap_bearer_mod_req_t cp_bearer_context_mod_request(const e1ap_bearer_mod_req_t *msg)
{
  e1ap_bearer_mod_req_t cp = {0};
  // Copy basic fields
  cp.gNB_cu_cp_ue_id = msg->gNB_cu_cp_ue_id;
  cp.gNB_cu_up_ue_id = msg->gNB_cu_up_ue_id;
  cp.dataDiscardRequired = msg->dataDiscardRequired;
  cp.numPDUSessions = msg->numPDUSessions;
  cp.numPDUSessionsMod = msg->numPDUSessionsMod;
  cp.numPDUSessionsRem = msg->numPDUSessionsRem;
  // Deep copy for optional fields
  _E1_CP_OPTIONAL_IE(&cp, msg, secInfo);
  _E1_CP_OPTIONAL_IE(&cp, msg, bearerContextStatus);
  _E1_CP_OPTIONAL_IE(&cp, msg, inactivityTimer);
  _E1_CP_OPTIONAL_IE(&cp, msg, ueDlAggMaxBitRate);
  _E1_CP_OPTIONAL_IE(&cp, msg, ueDlMaxIPBitRate);
  // Allocate and deep copy PDU session arrays
  if (msg->numPDUSessions > 0 && msg->pduSession != NULL) {
    cp.pduSession = calloc_or_fail(msg->numPDUSessions, sizeof(*cp.pduSession));
    for (int i = 0; i < msg->numPDUSessions; i++)
      cp.pduSession[i] = cp_pdu_session_item(&msg->pduSession[i]);
  }
  if (msg->numPDUSessionsMod > 0 && msg->pduSessionMod != NULL) {
    cp.pduSessionMod = calloc_or_fail(msg->numPDUSessionsMod, sizeof(*cp.pduSessionMod));
    for (int i = 0; i < msg->numPDUSessionsMod; i++)
      cp.pduSessionMod[i] = cp_pdu_session_to_mod_item(&msg->pduSessionMod[i]);
  }
  // Copy PDU sessions to remove (simple struct copy, no deep copy needed)
  if (msg->numPDUSessionsRem > 0 && msg->pduSessionRem != NULL) {
    cp.pduSessionRem = calloc_or_fail(msg->numPDUSessionsRem, sizeof(*cp.pduSessionRem));
    for (int i = 0; i < msg->numPDUSessionsRem; i++)
      cp.pduSessionRem[i] = msg->pduSessionRem[i];
  }
  return cp;
}

/** @brief Equality check for Security Indication IE (3GPP TS 38.463 9.3.1.23) */
static bool eq_security_ind(security_indication_t *a, security_indication_t *b)
{
  _EQ_CHECK_INT(a->confidentialityProtectionIndication, b->confidentialityProtectionIndication);
  _EQ_CHECK_INT(a->integrityProtectionIndication, b->integrityProtectionIndication);
  _EQ_CHECK_LONG(a->maxIPrate, b->maxIPrate);
  return true;
}

/**
 * @brief Equality check for DRB_nGRAN_to_mod_t
 */
static bool eq_drb_to_mod(const DRB_nGRAN_to_mod_t *a, const DRB_nGRAN_to_mod_t *b)
{
  _EQ_CHECK_LONG(a->id, b->id);
  _EQ_CHECK_INT(a->numQosFlowsMod, b->numQosFlowsMod);
  for (int i = 0; i < a->numQosFlowsMod; i++) {
    if (!eq_qos_flow(&a->qosFlows[i], &b->qosFlows[i]))
      return false;
  }
  _EQ_CHECK_INT(a->numDlUpParam, b->numDlUpParam);
  for (int i = 0; i < a->numDlUpParam; i++) {
    _EQ_CHECK_INT(a->DlUpParamList[i].cell_group_id, b->DlUpParamList[i].cell_group_id);
    if (!eq_up_tl_info(&a->DlUpParamList[i].tl_info, &b->DlUpParamList[i].tl_info))
      return false;
  }
  _EQ_CHECK_OPTIONAL_PTR(a, b, pdcp_config);
  if (a->pdcp_config && b->pdcp_config) {
    if (!eq_pdcp_config(a->pdcp_config, b->pdcp_config))
      return false;
  }
  _EQ_CHECK_OPTIONAL_PTR(a, b, sdap_config);
  if (a->sdap_config && b->sdap_config) {
    if (!eq_sdap_config(a->sdap_config, b->sdap_config))
      return false;
  }
  _EQ_CHECK_OPTIONAL_PTR(a, b, pdcp_status);
  if (a->pdcp_status && b->pdcp_status) {
    if (!eq_pdcp_info(a->pdcp_status, b->pdcp_status))
      return false;
  }
  return true;
}

/** @brief Equality check for PDU session item to modify */
static bool eq_pdu_session_to_mod_item(const pdu_session_to_mod_t *a, const pdu_session_to_mod_t *b)
{
  _EQ_CHECK_LONG(a->sessionId, b->sessionId);
  _EQ_CHECK_OPTIONAL_PTR(a, b, UP_TL_information);
  if (a->UP_TL_information && b->UP_TL_information) {
    if (!eq_up_tl_info(a->UP_TL_information, b->UP_TL_information))
      return false;
  }
  _EQ_CHECK_INT(a->numDRB2Setup, b->numDRB2Setup);
  for (int i = 0; i < a->numDRB2Setup; i++) {
    if (!eq_drb_to_setup(&a->DRBnGRanList[i], &b->DRBnGRanList[i]))
      return false;
  }
  _EQ_CHECK_LONG(a->numDRB2Modify, b->numDRB2Modify);
  for (int i = 0; i < a->numDRB2Modify; i++) {
    if (!eq_drb_to_mod(&a->DRBnGRanModList[i], &b->DRBnGRanModList[i]))
      return false;
  }
  _EQ_CHECK_OPTIONAL_PTR(a, b, securityIndication);
  _EQ_CHECK_INT(a->n_drb_to_remove, b->n_drb_to_remove);
  for (int r = 0; r < a->n_drb_to_remove; r++) {
    _EQ_CHECK_LONG(a->drbs_to_remove[r].id, b->drbs_to_remove[r].id);
  }
  if (a->securityIndication && b->securityIndication) {
    if (!eq_security_ind(a->securityIndication, b->securityIndication))
      return false;
  }
  return true;
}

/**
 * @brief E1AP Bearer Modification Request equality check
 */
bool eq_bearer_context_mod_request(const e1ap_bearer_mod_req_t *a, const e1ap_bearer_mod_req_t *b)
{
  // basic members
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->numPDUSessions, b->numPDUSessions);
  _EQ_CHECK_INT(a->numPDUSessionsMod, b->numPDUSessionsMod);
  // Security Info
  _EQ_CHECK_OPTIONAL_PTR(a, b, secInfo);
  if (a->secInfo && b->secInfo) {
    _EQ_CHECK_LONG(a->secInfo->cipheringAlgorithm, b->secInfo->cipheringAlgorithm);
    _EQ_CHECK_LONG(a->secInfo->integrityProtectionAlgorithm, b->secInfo->integrityProtectionAlgorithm);
    if (memcmp(a->secInfo->encryptionKey, b->secInfo->encryptionKey, E1AP_SECURITY_KEY_SIZE) != 0)
      return false;
    if (memcmp(a->secInfo->integrityProtectionKey, b->secInfo->integrityProtectionKey, E1AP_SECURITY_KEY_SIZE) != 0)
      return false;
  }
  // Bit Rates (O)
  _EQ_CHECK_OPTIONAL_IE(a, b, ueDlAggMaxBitRate, _EQ_CHECK_UINT64);
  _EQ_CHECK_OPTIONAL_IE(a, b, ueDlMaxIPBitRate, _EQ_CHECK_UINT64);
  // Bearer Context Status (O)
  _EQ_CHECK_OPTIONAL_IE(a, b, bearerContextStatus, _EQ_CHECK_INT);
  // Inactivity Timer (O)
  _EQ_CHECK_OPTIONAL_PTR(a, b, inactivityTimer);
  if (a->inactivityTimer && b->inactivityTimer)
    _EQ_CHECK_INT(*a->inactivityTimer, *b->inactivityTimer);
  // Activity Notification Level (O)
  // PDU Sessions
  _EQ_CHECK_INT(a->numPDUSessions, b->numPDUSessions);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSession);
  if (a->pduSession != NULL && b->pduSession != NULL) {
    for (int i = 0; i < a->numPDUSessions; i++) {
      if (!eq_pdu_session_item(&a->pduSession[i], &b->pduSession[i]))
        return false;
    }
  }
  // Modified PDU Sessions
  _EQ_CHECK_INT(a->numPDUSessionsMod, b->numPDUSessionsMod);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSessionMod);
  if (a->pduSessionMod != NULL && b->pduSessionMod != NULL) {
    for (int i = 0; i < a->numPDUSessionsMod; i++) {
      if (!eq_pdu_session_to_mod_item(&a->pduSessionMod[i], &b->pduSessionMod[i]))
        return false;
    }
  }
  // PDU Sessions to Remove
  _EQ_CHECK_INT(a->numPDUSessionsRem, b->numPDUSessionsRem);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSessionRem);
  if (a->pduSessionRem != NULL && b->pduSessionRem != NULL) {
    for (int i = 0; i < a->numPDUSessionsRem; i++) {
      const pdu_session_to_remove_t *pduRemA = &a->pduSessionRem[i];
      const pdu_session_to_remove_t *pduRemB = &b->pduSessionRem[i];
      _EQ_CHECK_LONG(pduRemA->sessionId, pduRemB->sessionId);
      _EQ_CHECK_INT(pduRemA->cause.type, pduRemB->cause.type);
      _EQ_CHECK_INT(pduRemA->cause.value, pduRemB->cause.value);
    }
  }
  return true;
}

/* Free DRB to modify item */
static void free_drb_to_mod_item(const DRB_nGRAN_to_mod_t *msg)
{
  free(msg->pdcp_config);
  free(msg->sdap_config);
  free(msg->pdcp_status);
}

/* Free PDU Session to modify item */
void free_pdu_session_to_mod_item(const pdu_session_to_mod_t *msg)
{
  free(msg->securityIndication);
  free(msg->UP_TL_information);
  for (int i = 0; i < msg->numDRB2Modify; i++)
    free_drb_to_mod_item(&msg->DRBnGRanModList[i]);
  for (int i = 0; i < msg->numDRB2Setup; i++)
    free_drb_to_setup_item(&msg->DRBnGRanList[i]);
}

/**
 * @brief E1AP Bearer Context Modification Request memory management
 */
void free_e1ap_context_mod_request(const e1ap_bearer_mod_req_t *msg)
{
  free(msg->secInfo);
  free(msg->ueDlAggMaxBitRate);
  free(msg->ueDlMaxIPBitRate);
  free(msg->bearerContextStatus);
  free(msg->inactivityTimer);
  // Free PDU session arrays and their contents
  if (msg->pduSession != NULL) {
    for (int i = 0; i < msg->numPDUSessions; i++)
      free_pdu_session_to_setup_item(&msg->pduSession[i]);
    free(msg->pduSession);
  }
  if (msg->pduSessionMod != NULL) {
    for (int i = 0; i < msg->numPDUSessionsMod; i++)
      free_pdu_session_to_mod_item(&msg->pduSessionMod[i]);
    free(msg->pduSessionMod);
  }
  // pduSessionRem is a simple struct array, just free the array
  free(msg->pduSessionRem);
}

/* ============================================
 *   E1AP Bearer Context Modification Response
 * ============================================ */

/** Encode QoS Flow Item (9.3.1.12 3GPP TS 38.463) */
static void encode_qos_flow_failed_item(E1AP_QoS_Flow_Failed_Item_t *out, const qos_flow_failed_t *in)
{
  // QFI (M)
  out->qoS_Flow_Identifier = in->qfi;
  // Cause (M)
  out->cause = e1_encode_cause_ie(&in->cause);
}

/**
 * @brief Bearer Context Modification Response encoding (9.2.2.5 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
struct E1AP_E1AP_PDU *encode_E1_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  pdu->present = E1AP_E1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextModification;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_SuccessfulOutcome__value_PR_BearerContextModificationResponse;
  E1AP_BearerContextModificationResponse_t *out = &pdu->choice.successfulOutcome->value.choice.BearerContextModificationResponse;
  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationResponseIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextModificationResponseIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationResponseIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextModificationResponseIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = msg->gNB_cu_up_ue_id;
  if (msg->numPDUSessionsMod > 0) {
    // NG-RAN PDU Session Resource Modified List (O)
    asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationResponseIEs_t, ie3);
    ie3->id = E1AP_ProtocolIE_ID_id_System_BearerContextModificationResponse;
    ie3->criticality = E1AP_Criticality_ignore;
    // CHOICE System (O)
    ie3->value.present = E1AP_BearerContextModificationResponseIEs__value_PR_System_BearerContextModificationResponse;
    E1AP_System_BearerContextModificationResponse_t *sys = &ie3->value.choice.System_BearerContextModificationResponse;
    // NG-RAN
    E1AP_ProtocolIE_Container_4932P29_t *ngran = calloc_or_fail(1, sizeof(*ngran));
    sys->present = E1AP_System_BearerContextModificationResponse_PR_nG_RAN_BearerContextModificationResponse;
    sys->choice.nG_RAN_BearerContextModificationResponse = (struct E1AP_ProtocolIE_Container *)ngran;
    asn1cSequenceAdd(ngran->list, E1AP_NG_RAN_BearerContextModificationResponse_t, list);
    // PDU Session Resource Modified List (O)
    list->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Modified_List;
    list->criticality = E1AP_Criticality_reject;
    list->value.present = E1AP_NG_RAN_BearerContextModificationResponse__value_PR_PDU_Session_Resource_Modified_List;
    E1AP_PDU_Session_Resource_Modified_List_t *pdu_mod_l = &list->value.choice.PDU_Session_Resource_Modified_List;
    for (int i = 0; i < msg->numPDUSessionsMod; ++i) {
      // PDU Session Resource Modified Item (1..maxnoofPDUSessionResource)
      const pdu_session_modif_t *pdu = &msg->pduSessionMod[i];
      asn1cSequenceAdd(pdu_mod_l->list, E1AP_PDU_Session_Resource_Modified_Item_t, iePdu);
      // PDU Session ID (M)
      iePdu->pDU_Session_ID = pdu->id;
      // Security Result (O)
      if (pdu->confidentialityProtectionIndication || pdu->integrityProtectionIndication) {
        iePdu->securityResult = calloc_or_fail(1, sizeof(*iePdu->securityResult));
        if (pdu->confidentialityProtectionIndication)
          iePdu->securityResult->confidentialityProtectionResult = *pdu->confidentialityProtectionIndication;
        if (pdu->integrityProtectionIndication)
          iePdu->securityResult->integrityProtectionResult = *pdu->integrityProtectionIndication;
      }
      // NG DL UP Transport Layer Information (O)
      if (pdu->ng_DL_UP_TL_info) {
        iePdu->nG_DL_UP_TNL_Information = calloc_or_fail(1, sizeof(*iePdu->nG_DL_UP_TNL_Information));
        *iePdu->nG_DL_UP_TNL_Information = e1_encode_up_tnl_info(pdu->ng_DL_UP_TL_info);
      }
      // DRB Modified List (O)
      if (pdu->numDRBModified > 0) {
        iePdu->dRB_Modified_List_NG_RAN = calloc_or_fail(1, sizeof(*iePdu->dRB_Modified_List_NG_RAN));
        E1AP_DRB_Modified_List_NG_RAN_t *drb_mod_l = iePdu->dRB_Modified_List_NG_RAN;
        for (int j = 0; j < pdu->numDRBModified; ++j) {
          const DRB_nGRAN_modified_t *drb = &pdu->DRBnGRanModList[j];
          asn1cSequenceAdd(drb_mod_l->list, E1AP_DRB_Modified_Item_NG_RAN_t, drb_mod);
          // DRB ID (M)
          drb_mod->dRB_ID = drb->id;
          // Flow Setup List (O)
          if (drb->numQosFlowSetup)
            drb_mod->flow_Setup_List = calloc_or_fail(1, sizeof(*drb_mod->flow_Setup_List));
          for (int q = 0; q < drb->numQosFlowSetup; ++q) {
            asn1cSequenceAdd(drb_mod->flow_Setup_List->list, E1AP_QoS_Flow_Item_t, qos_mod);
            qos_mod->qoS_Flow_Identifier = drb->qosFlows[q].qfi;
          }
          // Flow Failed List (O)
          if (drb->numQosFlowFailed)
            drb_mod->flow_Failed_List = calloc_or_fail(1, sizeof(*drb_mod->flow_Failed_List));
          for (int q = 0; q < drb->numQosFlowFailed; ++q) {
            asn1cSequenceAdd(drb_mod->flow_Failed_List->list, E1AP_QoS_Flow_Failed_Item_t, fail);
            encode_qos_flow_failed_item(fail, &drb->qosFlowsFailed[q]);
          }
          // PDCP Status Information (O)
          if (drb->pdcp_status) {
            asn1cCalloc(drb_mod->pDCP_SN_Status_Information, pdcp);
            *pdcp = encode_pdcp_status_info(drb->pdcp_status);
          }
          // UL UP Parameters (O)
          if (drb->numUpParam)
            drb_mod->uL_UP_Transport_Parameters = calloc_or_fail(1, sizeof(*drb_mod->uL_UP_Transport_Parameters));
          for (const up_params_t *k = drb->ul_UP_Params; k < drb->ul_UP_Params + drb->numUpParam; k++) {
            asn1cSequenceAdd(drb_mod->uL_UP_Transport_Parameters->list, E1AP_UP_Parameters_Item_t, up);
            up->uP_TNL_Information = e1_encode_up_tnl_info(&k->tl_info);
            up->cell_Group_ID = k->cell_group_id;
          }
        }
      }
      // DRB Failed To Modify List (O)
      if (pdu->numDRBFailedToMod > 0) {
        iePdu->dRB_Failed_To_Modify_List_NG_RAN = calloc_or_fail(1, sizeof(*iePdu->dRB_Failed_To_Modify_List_NG_RAN));
        E1AP_DRB_Failed_To_Modify_List_NG_RAN_t *ngran = iePdu->dRB_Failed_To_Modify_List_NG_RAN;
        for (int j = 0; j < pdu->numDRBFailedToMod; ++j) {
          const DRB_nGRAN_failed_t *drb = &pdu->DRBnGRanFailedModList[j];
          asn1cSequenceAdd(ngran->list, E1AP_DRB_Failed_To_Modify_Item_NG_RAN_t, fail_to_mod);
          encode_drb_failed_item((E1AP_DRB_Failed_Item_NG_RAN_t *)fail_to_mod, drb);
        }
      }
      // DRB Setup List (O)
      if (pdu->numDRBSetup > 0) {
        iePdu->dRB_Setup_List_NG_RAN = calloc_or_fail(1, sizeof(*iePdu->dRB_Setup_List_NG_RAN));
        E1AP_DRB_Setup_List_NG_RAN_t *drb_setup = iePdu->dRB_Setup_List_NG_RAN;
        for (int j = 0; j < pdu->numDRBSetup; ++j) {
          const DRB_nGRAN_setup_t *drb = &pdu->DRBnGRanSetupList[j];
          asn1cSequenceAdd(drb_setup->list, E1AP_DRB_Setup_Item_NG_RAN_t, item);
          // DRB ID (M)
          item->dRB_ID = drb->id;
          // UL UP Parameters (M)
          for (const up_params_t *k = drb->UpParamList; k < drb->UpParamList + drb->numUpParam; k++) {
            asn1cSequenceAdd(item->uL_UP_Transport_Parameters.list, E1AP_UP_Parameters_Item_t, up);
            up->uP_TNL_Information = e1_encode_up_tnl_info(&k->tl_info);
            up->cell_Group_ID = drb->UpParamList->cell_group_id;
          }
          // Flow Setup List (M)
          for (int q = 0; q < drb->numQosFlowSetup; ++q) {
            asn1cSequenceAdd(item->flow_Setup_List.list, E1AP_QoS_Flow_Item_t, qos);
            qos->qoS_Flow_Identifier = drb->qosFlows[q].qfi;
          }
          // Flow Failed List (O)
          if (drb->numQosFlowFailed)
            item->flow_Failed_List = calloc_or_fail(1, sizeof(*item->flow_Failed_List));
          for (int q = 0; q < drb->numQosFlowFailed; ++q) {
            asn1cSequenceAdd(item->flow_Failed_List->list, E1AP_QoS_Flow_Failed_Item_t, fail);
            encode_qos_flow_failed_item(fail, &drb->qosFlowsFailed[q]);
          }
        }
      }
      // DRB Failed List (O)
      if (pdu->numDRBFailed) {
        iePdu->dRB_Failed_List_NG_RAN = calloc_or_fail(1, sizeof(*iePdu->dRB_Failed_List_NG_RAN));
        E1AP_DRB_Failed_List_NG_RAN_t *ngran = iePdu->dRB_Failed_List_NG_RAN;
        for (int j = 0; j < pdu->numDRBFailed; ++j) {
          const DRB_nGRAN_failed_t *drb = &pdu->DRBnGRanFailedList[j];
          asn1cSequenceAdd(ngran->list, E1AP_DRB_Failed_Item_NG_RAN_t, fail);
          encode_drb_failed_item(fail, drb);
        }
      }
    }
  }
  return pdu;
}

/** Decode QoS Flow Item (9.3.1.12 3GPP TS 38.463) */
static qos_flow_failed_t decode_qos_flow_failed_item(const E1AP_QoS_Flow_Failed_Item_t *in)
{
  qos_flow_failed_t out = {0};
  // QFI (M)
  out.qfi = in->qoS_Flow_Identifier;
  // Cause (M)
  out.cause = e1_decode_cause_ie(&in->cause);
  return out;
}

/**
 * @brief Bearer Context Modification Response decoding (9.2.2.5 3GPP TS 38.463)
 *        gNB-CU-UP → gNB-CU-CP
 */
bool decode_E1_bearer_context_mod_response(e1ap_bearer_modif_resp_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_successfulOutcome);
  E1AP_SuccessfulOutcome_t *type = pdu->choice.successfulOutcome;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextModification);
  _EQ_CHECK_INT(type->value.present, E1AP_SuccessfulOutcome__value_PR_BearerContextModificationResponse);
  const E1AP_BearerContextModificationResponse_t *in = &type->value.choice.BearerContextModificationResponse;
  // Check mandatory IEs first
  E1AP_BearerContextModificationResponseIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationResponseIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);
  // Process all IEs
  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        // gNB-CU-CP UE E1AP ID (M)
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationResponseIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        // gNB-CU-UP UE E1AP ID (M)
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationResponseIEs__value_PR_GNB_CU_UP_UE_E1AP_ID);
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;

      case E1AP_ProtocolIE_ID_id_System_BearerContextModificationResponse:
        // CHOICE System (O)
        _EQ_CHECK_INT(ie->value.present,
                      E1AP_BearerContextModificationResponseIEs__value_PR_System_BearerContextModificationResponse);
        // NG-RAN System
        E1AP_System_BearerContextModificationResponse_t *sys = &ie->value.choice.System_BearerContextModificationResponse;
        _EQ_CHECK_INT(sys->present, E1AP_System_BearerContextModificationResponse_PR_nG_RAN_BearerContextModificationResponse);
        // NG-RAN List
        E1AP_ProtocolIE_Container_4932P29_t *msgNGRAN_list = (E1AP_ProtocolIE_Container_4932P29_t *)sys->choice.nG_RAN_BearerContextModificationResponse;
        _EQ_CHECK_INT(msgNGRAN_list->list.count, 1);
        E1AP_NG_RAN_BearerContextModificationResponse_t *msgNGRAN = msgNGRAN_list->list.array[0];
        _EQ_CHECK_LONG(msgNGRAN->id, E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Modified_List);
        _EQ_CHECK_INT(msgNGRAN->value.present,
                      E1AP_NG_RAN_BearerContextModificationResponse__value_PR_PDU_Session_Resource_Modified_List);
        // PDU Session Resource Modified list (O)
        E1AP_PDU_Session_Resource_Modified_List_t *list = &msgNGRAN->value.choice.PDU_Session_Resource_Modified_List;
        out->numPDUSessionsMod = list->list.count;
        DevAssert(out->numPDUSessionsMod > 0 && out->numPDUSessionsMod <= NR_MAX_NB_PDU_SESSIONS);
        // Allocate array for PDU sessions
        out->pduSessionMod = calloc_or_fail(out->numPDUSessionsMod, sizeof(*out->pduSessionMod));
        for (int i = 0; i < list->list.count; i++) {
          // PDU Session Resource Modified item (1..maxnoofPDUSessionResource)
          pdu_session_modif_t *pduModified = &out->pduSessionMod[i];
          E1AP_PDU_Session_Resource_Modified_Item_t *pduIE = list->list.array[i];
          // PDU Session ID (M)
          pduModified->id = pduIE->pDU_Session_ID;
          // NG DL UP Transport Layer Information (O)
          if (pduIE->nG_DL_UP_TNL_Information) {
            pduModified->ng_DL_UP_TL_info = malloc_or_fail(sizeof(*pduModified->ng_DL_UP_TL_info));
            e1_decode_up_tnl_info(pduModified->ng_DL_UP_TL_info, pduIE->nG_DL_UP_TNL_Information);
          }
          // Security Result (O)
          if (pduIE->securityResult) {
            pduModified->confidentialityProtectionIndication = malloc_or_fail(sizeof(*pduModified->confidentialityProtectionIndication));
            *pduModified->confidentialityProtectionIndication = pduIE->securityResult->confidentialityProtectionResult;
            pduModified->integrityProtectionIndication = malloc_or_fail(sizeof(*pduModified->integrityProtectionIndication));
            *pduModified->integrityProtectionIndication = pduIE->securityResult->integrityProtectionResult;
          }
          // DRB Setup list
          if (pduIE->dRB_Setup_List_NG_RAN) {
            pduModified->numDRBSetup = pduIE->dRB_Setup_List_NG_RAN->list.count;
            for (int j = 0; j < pduIE->dRB_Setup_List_NG_RAN->list.count; j++) {
              // DRB Setup item
              DRB_nGRAN_setup_t *drbMod = &pduModified->DRBnGRanSetupList[j];
              E1AP_DRB_Setup_Item_NG_RAN_t *drbIE = pduIE->dRB_Setup_List_NG_RAN->list.array[j];
              drbMod->id = drbIE->dRB_ID;
              // UL UP Parameters (M)
              if (drbIE->uL_UP_Transport_Parameters.list.count) {
                drbMod->numUpParam = decode_dl_up_parameters(drbMod->UpParamList, &drbIE->uL_UP_Transport_Parameters);
              }
              // Flow Setup List (O)
              if (drbIE->flow_Failed_List) {
                drbMod->numQosFlowFailed = drbIE->flow_Failed_List->list.count;
                for (int i = 0; i < drbMod->numQosFlowSetup; i++) {
                  drbMod->qosFlowsFailed[i] = decode_qos_flow_failed_item(drbIE->flow_Failed_List->list.array[i]);
                }
              }
              // Flow Setup List (M)
              if (drbIE->flow_Setup_List.list.count) {
                drbMod->numQosFlowSetup = drbIE->flow_Setup_List.list.count;
                for (int i = 0; i < drbMod->numQosFlowSetup; i++)
                  drbMod->qosFlows[i].qfi = drbIE->flow_Setup_List.list.array[i]->qoS_Flow_Identifier;
              }
            }
          }
          // DRB Failed list
          if (pduIE->dRB_Failed_List_NG_RAN) {
            pduModified->numDRBFailed = pduIE->dRB_Failed_List_NG_RAN->list.count;
            for (int j = 0; j < pduIE->dRB_Failed_List_NG_RAN->list.count; j++) {
              decode_drb_failed_item(&pduModified->DRBnGRanFailedList[j], pduIE->dRB_Failed_List_NG_RAN->list.array[j]);
            }
          }
          // DRB Modified list (0..1)
          if (pduIE->dRB_Modified_List_NG_RAN) {
            pduModified->numDRBModified = pduIE->dRB_Modified_List_NG_RAN->list.count;
            for (int j = 0; j < pduIE->dRB_Modified_List_NG_RAN->list.count; j++) {
              // DRB Modified item
              DRB_nGRAN_modified_t *drbMod = &pduModified->DRBnGRanModList[j];
              E1AP_DRB_Modified_Item_NG_RAN_t *drbIE = pduIE->dRB_Modified_List_NG_RAN->list.array[j];
              // DRB ID (M)
              drbMod->id = drbIE->dRB_ID;
              // UL UP Parameters (O)
              if (drbIE->uL_UP_Transport_Parameters) {
                drbMod->numUpParam = decode_dl_up_parameters(drbMod->ul_UP_Params, drbIE->uL_UP_Transport_Parameters);
              }
              // Flow Setup List (O)
              if (drbIE->flow_Setup_List) {
                drbMod->numQosFlowSetup = drbIE->flow_Setup_List->list.count;
                for (int i = 0; i < drbMod->numQosFlowSetup; i++)
                  drbMod->qosFlows[i].qfi = drbIE->flow_Setup_List->list.array[i]->qoS_Flow_Identifier;
              }
              // Flow Failed List (O)
              if (drbIE->flow_Failed_List) {
                drbMod->numQosFlowFailed = drbIE->flow_Failed_List->list.count;
                for (int i = 0; i < drbMod->numQosFlowFailed; i++) {
                  drbMod->qosFlowsFailed[i] = decode_qos_flow_failed_item(drbIE->flow_Failed_List->list.array[i]);
                }
              }
              // PDCP Status Info (O)
              if (drbIE->pDCP_SN_Status_Information) {
                drbMod->pdcp_status = malloc_or_fail(sizeof(*drbMod->pdcp_status));
                *drbMod->pdcp_status = decode_pdcp_status_info(drbIE->pDCP_SN_Status_Information);
              }
            }
          }
          // DRB Failed to Mod list
          if (pduIE->dRB_Failed_To_Modify_List_NG_RAN) {
            pduModified->numDRBFailedToMod = pduIE->dRB_Failed_To_Modify_List_NG_RAN->list.count;
            for (int j = 0; j < pduIE->dRB_Failed_To_Modify_List_NG_RAN->list.count; j++) {
              decode_drb_failed_item(&pduModified->DRBnGRanFailedModList[j],
                                     (E1AP_DRB_Failed_Item_NG_RAN_t *)pduIE->dRB_Failed_To_Modify_List_NG_RAN->list.array[j]);
            }
          }
        }
        break;
    }
  }
  return true;
}

/** @brief Deep copy for PDU Session Modified Item */
static pdu_session_modif_t cp_pdu_session_mod_item(const pdu_session_modif_t *msg)
{
  pdu_session_modif_t cp = {0};
  // shallow copy
  cp = *msg;
  // Optional IE
  _E1_CP_OPTIONAL_IE(&cp, msg, integrityProtectionIndication);
  _E1_CP_OPTIONAL_IE(&cp, msg, confidentialityProtectionIndication);
  _E1_CP_OPTIONAL_IE(&cp, msg, ng_DL_UP_TL_info);
  return cp;
}

/** @brief Deep copy for Bearer Context Modification Response */
e1ap_bearer_modif_resp_t cp_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *msg)
{
  e1ap_bearer_modif_resp_t cp = {0};
  cp.gNB_cu_cp_ue_id = msg->gNB_cu_cp_ue_id;
  cp.gNB_cu_up_ue_id = msg->gNB_cu_up_ue_id;
  cp.numPDUSessionsMod = msg->numPDUSessionsMod;
  if (msg->numPDUSessionsMod > 0 && msg->pduSessionMod != NULL) {
    cp.pduSessionMod = calloc_or_fail(msg->numPDUSessionsMod, sizeof(*msg->pduSessionMod));
    for (int i = 0; i < msg->numPDUSessionsMod; i++) {
      cp.pduSessionMod[i] = cp_pdu_session_mod_item(&msg->pduSessionMod[i]);
      for (int j = 0; j < msg->pduSessionMod[i].numDRBModified; j++) {
        _E1_CP_OPTIONAL_IE(&cp.pduSessionMod[i].DRBnGRanModList[j], &msg->pduSessionMod[i].DRBnGRanModList[j], pdcp_status);
      }
    }
  }
  return cp;
}

/**
 * @brief E1AP Bearer Modification Response equality check
 */
bool eq_bearer_context_mod_response(const e1ap_bearer_modif_resp_t *a, const e1ap_bearer_modif_resp_t *b)
{
  if (!a || !b) return false; // Null-check both inputs

  // Basic members
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->numPDUSessionsMod, b->numPDUSessionsMod);

  // PDU Session Modified List
  for (int i = 0; i < a->numPDUSessionsMod; i++) {
    const pdu_session_modif_t *pduA = &a->pduSessionMod[i];
    const pdu_session_modif_t *pduB = &b->pduSessionMod[i];

    _EQ_CHECK_LONG(pduA->id, pduB->id);
    _EQ_CHECK_OPTIONAL_IE(pduA, pduB, confidentialityProtectionIndication, _EQ_CHECK_INT);
    _EQ_CHECK_OPTIONAL_IE(pduA, pduB, integrityProtectionIndication, _EQ_CHECK_INT);

    _EQ_CHECK_OPTIONAL_PTR(pduA, pduB, ng_DL_UP_TL_info);
    if (pduA->ng_DL_UP_TL_info && pduB->ng_DL_UP_TL_info) {
      if (!eq_up_tl_info(pduA->ng_DL_UP_TL_info, pduB->ng_DL_UP_TL_info))
        return false;
    }

    // DRB Modified List
    _EQ_CHECK_INT(pduA->numDRBModified, pduB->numDRBModified);
    for (int j = 0; j < pduA->numDRBModified; j++) {
      const DRB_nGRAN_modified_t *drbModA = &pduA->DRBnGRanModList[j];
      const DRB_nGRAN_modified_t *drbModB = &pduB->DRBnGRanModList[j];
      _EQ_CHECK_LONG(drbModA->id, drbModB->id);
      _EQ_CHECK_INT(drbModA->numQosFlowSetup, drbModB->numQosFlowSetup);
      for (int j = 0; j < drbModA->numQosFlowSetup; j++) {
        _EQ_CHECK_LONG(drbModA->qosFlows[j].qfi, drbModB->qosFlows[j].qfi);
      }
      _EQ_CHECK_OPTIONAL_PTR(drbModA, drbModB, pdcp_status);
      if (drbModA->pdcp_status && drbModB->pdcp_status) {
        if (!eq_pdcp_info(drbModA->pdcp_status, drbModB->pdcp_status))
          return false;
      }
    }

    // DRB Failed to Modify List
    _EQ_CHECK_INT(pduA->numDRBFailedToMod, pduB->numDRBFailedToMod);
    for (int j = 0; j < pduA->numDRBFailedToMod; j++) {
      _EQ_CHECK_LONG(pduA->DRBnGRanFailedModList[j].id, pduB->DRBnGRanFailedModList[j].id);
      _EQ_CHECK_INT(pduA->DRBnGRanFailedModList[j].cause.value, pduB->DRBnGRanFailedModList[j].cause.value);
      _EQ_CHECK_INT(pduA->DRBnGRanFailedModList[j].cause.type, pduB->DRBnGRanFailedModList[j].cause.type);
    }

    // DRB Setup List
    _EQ_CHECK_INT(pduA->numDRBSetup, pduB->numDRBSetup);
    for (int j = 0; j < pduA->numDRBSetup; j++) {
      const DRB_nGRAN_setup_t *drbSetupA = &pduA->DRBnGRanSetupList[j];
      const DRB_nGRAN_setup_t *drbSetupB = &pduB->DRBnGRanSetupList[j];

      _EQ_CHECK_LONG(drbSetupA->id, drbSetupB->id);
      _EQ_CHECK_INT(drbSetupA->numQosFlowSetup, drbSetupB->numQosFlowSetup);
      for (int j = 0; j < drbSetupA->numQosFlowSetup; j++) {
        _EQ_CHECK_LONG(drbSetupA->qosFlows[j].qfi, drbSetupB->qosFlows[j].qfi);
      }
    }

    // DRB Failed to Setup List
    _EQ_CHECK_INT(pduA->numDRBFailed, pduB->numDRBFailed);
    for (int j = 0; j < pduA->numDRBFailed; j++) {
      _EQ_CHECK_LONG(pduA->DRBnGRanFailedList[j].id, pduB->DRBnGRanFailedList[j].id);
      _EQ_CHECK_INT(pduA->DRBnGRanFailedList[j].cause.value, pduB->DRBnGRanFailedList[j].cause.value);
      _EQ_CHECK_INT(pduA->DRBnGRanFailedList[j].cause.type, pduB->DRBnGRanFailedList[j].cause.type);
    }

  }

  return true;
}

/**
 * @brief E1AP Bearer Context Modification Response memory management
 */
void free_e1ap_context_mod_response(const e1ap_bearer_modif_resp_t *msg)
{
  for (int i = 0; i < msg->numPDUSessionsMod; i++) {
    free(msg->pduSessionMod[i].confidentialityProtectionIndication);
    free(msg->pduSessionMod[i].integrityProtectionIndication);
    free(msg->pduSessionMod[i].ng_DL_UP_TL_info);
    for (int j = 0; j < msg->pduSessionMod[i].numDRBModified; j++) {
      free(msg->pduSessionMod[i].DRBnGRanModList[j].pdcp_status);
    }
  }
  free(msg->pduSessionMod);
}

/* ===========================================
 *   E1AP Bearer Context Modification Failure
 * ============================================ */

/** @brief Bearer Context Modification Failure encoding (9.2.2.6 38.463)
 *         gNB-CU-UP → gNB-CU-CP */
struct E1AP_E1AP_PDU *encode_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu->choice.unsuccessfulOutcome, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextModification;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_UnsuccessfulOutcome__value_PR_BearerContextModificationFailure;
  E1AP_BearerContextModificationFailure_t *out = &type->value.choice.BearerContextModificationFailure;
  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationFailureIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextModificationFailureIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;
  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationFailureIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextModificationFailureIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = msg->gNB_cu_up_ue_id;
  // Cause (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationFailureIEs_t, ie3);
  ie3->id = E1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = E1AP_Criticality_reject;
  ie3->value.present = E1AP_BearerContextModificationFailureIEs__value_PR_Cause;
  ie3->value.choice.Cause = e1_encode_cause_ie(&msg->cause);
  return pdu;
}

/** @brief Bearer Context Modification Failure decoding (9.2.2.6 38.463)
 *         gNB-CU-UP → gNB-CU-CP */
bool decode_E1_bearer_context_mod_failure(e1ap_bearer_context_mod_failure_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_unsuccessfulOutcome);
  E1AP_UnsuccessfulOutcome_t *type = pdu->choice.unsuccessfulOutcome;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextModification);
  _EQ_CHECK_INT(type->value.present, E1AP_UnsuccessfulOutcome__value_PR_BearerContextModificationFailure);
  const E1AP_BearerContextModificationFailure_t *in = &type->value.choice.BearerContextModificationFailure;
  // Check mandatory IEs first
  E1AP_BearerContextModificationFailureIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationFailureIEs_t, ie, in, E1AP_ProtocolIE_ID_id_Cause, true);
  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_Cause:
        out->cause = e1_decode_cause_ie(&ie->value.choice.Cause);
        break;
      default:
        PRINT_ERROR("Invalid or unhandled optional IE (%ld) in Bearer Context Modification Failure message\n", ie->id);
        break;
    }
  }
  return true;
}

/** @brief E1AP Bearer Context Modification Failure: deep copy */
e1ap_bearer_context_mod_failure_t cp_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg)
{
  e1ap_bearer_context_mod_failure_t cp = {0};
  cp = *msg;
  return cp;
}

/** @brief E1AP Bearer Context Modification Failure: equality check */
bool eq_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *a, const e1ap_bearer_context_mod_failure_t *b)
{
  if (!a || !b)
    return false; // Null-check both inputs
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->cause.type, b->cause.type);
  _EQ_CHECK_INT(a->cause.value, b->cause.value);
  return true;
}

/** @brief E1AP Bearer Context Modification Failure: free */
void free_E1_bearer_context_mod_failure(const e1ap_bearer_context_mod_failure_t *msg)
{
  UNUSED(msg);
  // do nothing
}

/* ============================================
 *   E1AP Bearer Context Modification Required
 * ============================================ */

/** @brief Encode PDU Session Resource To Remove Item */
static void e1_encode_pdu_session_to_remove_item(E1AP_PDU_Session_Resource_To_Remove_Item_t *out, const pdu_session_to_remove_t *in)
{
  // PDU Session ID (M)
  out->pDU_Session_ID = in->sessionId;
  // Cause (O) - PDU-Session-Resource-To-Remove-Item-ExtIEs id-Cause
  if (in->cause.type != E1AP_CAUSE_NOTHING) {
    E1AP_ProtocolExtensionContainer_4961P104_t *ext = calloc_or_fail(1, sizeof(*ext));
    out->iE_Extensions = (struct E1AP_ProtocolExtensionContainer *)ext;
    asn1cSequenceAdd(ext->list, E1AP_PDU_Session_Resource_To_Remove_Item_ExtIEs_t, ie);
    ie->id = E1AP_ProtocolIE_ID_id_Cause;
    ie->criticality = E1AP_Criticality_ignore;
    ie->extensionValue.present = E1AP_PDU_Session_Resource_To_Remove_Item_ExtIEs__extensionValue_PR_Cause;
    ie->extensionValue.choice.Cause = e1_encode_cause_ie(&in->cause);
  }
}

/** @brief Decode PDU Session Resource To Remove Item */
static bool e1_decode_pdu_session_to_remove_item(pdu_session_to_remove_t *out, const E1AP_PDU_Session_Resource_To_Remove_Item_t *in)
{
  *out = (pdu_session_to_remove_t){0};
  // PDU Session ID (M)
  out->sessionId = in->pDU_Session_ID;
  // Cause (O)
  if (in->iE_Extensions != NULL) {
    const E1AP_ProtocolExtensionContainer_4961P104_t *ext = (const E1AP_ProtocolExtensionContainer_4961P104_t *)in->iE_Extensions;
    for (int i = 0; i < ext->list.count; i++) {
      const E1AP_PDU_Session_Resource_To_Remove_Item_ExtIEs_t *ie = ext->list.array[i];
      AssertFatal(ie != NULL, "ext->list.array[i] shall not be null");
      if (ie->id != E1AP_ProtocolIE_ID_id_Cause)
        continue;
      _EQ_CHECK_INT(ie->extensionValue.present, E1AP_PDU_Session_Resource_To_Remove_Item_ExtIEs__extensionValue_PR_Cause);
      out->cause = e1_decode_cause_ie(&ie->extensionValue.choice.Cause);
      break;
    }
  }
  return true;
}

/** @brief Encode DRB Required To Modify Item (NG-RAN) */
static void e1_encode_drb_required_to_mod_item(E1AP_DRB_Required_To_Modify_Item_NG_RAN_t *out,
                                               const DRB_nGRAN_required_to_mod_t *in)
{
  // DRB ID (M)
  out->dRB_ID = in->id;
  // Cause (O)
  if (in->cause.type != E1AP_CAUSE_NOTHING)
    asn1cCallocOne(out->cause, e1_encode_cause_ie(&in->cause));
}

/** @brief Decode DRB Required To Modify Item (NG-RAN) */
static bool e1_decode_drb_required_to_mod_item(DRB_nGRAN_required_to_mod_t *out,
                                               const E1AP_DRB_Required_To_Modify_Item_NG_RAN_t *in)
{
  *out = (DRB_nGRAN_required_to_mod_t){0};
  // DRB ID (M)
  out->id = in->dRB_ID;
  // Cause (O)
  if (in->cause != NULL)
    out->cause = e1_decode_cause_ie(in->cause);
  return true;
}

/** @brief Encode PDU Session Resource Required To Modify Item */
static void e1_encode_pdu_session_required_to_mod_item(E1AP_PDU_Session_Resource_Required_To_Modify_Item_t *out,
                                                       const pdu_session_required_to_mod_t *in)
{
  // PDU Session ID (M)
  out->pDU_Session_ID = in->sessionId;
  // DRB Required To Modify List (O)
  if (in->numDRB2Modify > 0) {
    out->dRB_Required_To_Modify_List_NG_RAN = calloc_or_fail(1, sizeof(*out->dRB_Required_To_Modify_List_NG_RAN));
    for (int i = 0; i < in->numDRB2Modify; i++) {
      asn1cSequenceAdd(out->dRB_Required_To_Modify_List_NG_RAN->list, E1AP_DRB_Required_To_Modify_Item_NG_RAN_t, drb);
      e1_encode_drb_required_to_mod_item(drb, &in->DRBnGRanModList[i]);
    }
  }
}

/** @brief Decode PDU Session Resource Required To Modify Item */
static bool e1_decode_pdu_session_required_to_mod_item(pdu_session_required_to_mod_t *out,
                                                       const E1AP_PDU_Session_Resource_Required_To_Modify_Item_t *in)
{
  *out = (pdu_session_required_to_mod_t){0};
  // PDU Session ID (M)
  out->sessionId = in->pDU_Session_ID;
  // DRB Required To Modify List (O)
  if (in->dRB_Required_To_Modify_List_NG_RAN != NULL) {
    const E1AP_DRB_Required_To_Modify_List_NG_RAN_t *drbList = in->dRB_Required_To_Modify_List_NG_RAN;
    out->numDRB2Modify = drbList->list.count;
    if (out->numDRB2Modify <= 0 || out->numDRB2Modify > E1AP_MAX_NUM_DRBS) {
      PRINT_ERROR("DRB Required To Modify List count %d out of range (1..%d)\n", out->numDRB2Modify, E1AP_MAX_NUM_DRBS);
      return false;
    }
    for (int i = 0; i < drbList->list.count; i++)
      CHECK_E1AP_DEC(e1_decode_drb_required_to_mod_item(&out->DRBnGRanModList[i], drbList->list.array[i]));
  }
  return true;
}

/** @brief Bearer Context Modification Required encoding (9.2.2.7, 3GPP TS 38.463)
 *         gNB-CU-UP -> gNB-CU-CP */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_mod_required(const e1ap_bearer_mod_required_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextModificationRequired;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_InitiatingMessage__value_PR_BearerContextModificationRequired;
  E1AP_BearerContextModificationRequired_t *out = &type->value.choice.BearerContextModificationRequired;

  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequiredIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextModificationRequiredIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;

  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequiredIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextModificationRequiredIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = msg->gNB_cu_up_ue_id;

  // System Bearer Context Modification Required (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationRequiredIEs_t, ie3);
  ie3->id = E1AP_ProtocolIE_ID_id_System_BearerContextModificationRequired;
  ie3->criticality = E1AP_Criticality_reject;
  ie3->value.present = E1AP_BearerContextModificationRequiredIEs__value_PR_System_BearerContextModificationRequired;
  E1AP_System_BearerContextModificationRequired_t *sys = &ie3->value.choice.System_BearerContextModificationRequired;
  sys->present = E1AP_System_BearerContextModificationRequired_PR_nG_RAN_BearerContextModificationRequired;
  E1AP_ProtocolIE_Container_4932P33_t *msgNGRAN_list = calloc_or_fail(1, sizeof(*msgNGRAN_list));
  sys->choice.nG_RAN_BearerContextModificationRequired = (struct E1AP_ProtocolIE_Container *)msgNGRAN_list;

  // NG-RAN PDU Session Resource Required To Modify List (O)
  if (msg->numPDUSessionsMod > 0 && msg->pduSessionMod != NULL) {
    asn1cSequenceAdd(msgNGRAN_list->list, E1AP_NG_RAN_BearerContextModificationRequired_t, msgNGRAN);
    msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Required_To_Modify_List;
    msgNGRAN->criticality = E1AP_Criticality_reject;
    msgNGRAN->value.present = E1AP_NG_RAN_BearerContextModificationRequired__value_PR_PDU_Session_Resource_Required_To_Modify_List;
    E1AP_PDU_Session_Resource_Required_To_Modify_List_t *pdu2Mod =
        &msgNGRAN->value.choice.PDU_Session_Resource_Required_To_Modify_List;
    for (const pdu_session_required_to_mod_t *i = msg->pduSessionMod; i < msg->pduSessionMod + msg->numPDUSessionsMod; i++) {
      asn1cSequenceAdd(pdu2Mod->list, E1AP_PDU_Session_Resource_Required_To_Modify_Item_t, ie);
      e1_encode_pdu_session_required_to_mod_item(ie, i);
    }
  }

  // NG-RAN PDU Session Resource To Remove List (O)
  if (msg->numPDUSessionsRem > 0 && msg->pduSessionRem != NULL) {
    asn1cSequenceAdd(msgNGRAN_list->list, E1AP_NG_RAN_BearerContextModificationRequired_t, msgNGRAN);
    msgNGRAN->id = E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Remove_List;
    msgNGRAN->criticality = E1AP_Criticality_reject;
    msgNGRAN->value.present = E1AP_NG_RAN_BearerContextModificationRequired__value_PR_PDU_Session_Resource_To_Remove_List;
    E1AP_PDU_Session_Resource_To_Remove_List_t *pdu2Remove = &msgNGRAN->value.choice.PDU_Session_Resource_To_Remove_List;
    for (const pdu_session_to_remove_t *i = msg->pduSessionRem; i < msg->pduSessionRem + msg->numPDUSessionsRem; i++) {
      asn1cSequenceAdd(pdu2Remove->list, E1AP_PDU_Session_Resource_To_Remove_Item_t, ieC4_1);
      e1_encode_pdu_session_to_remove_item(ieC4_1, i);
    }
  }
  return pdu;
}

/** @brief Bearer Context Modification Required decoding (9.2.2.7, 3GPP TS 38.463)
 *         gNB-CU-UP -> gNB-CU-CP */
bool decode_E1_bearer_context_mod_required(const E1AP_E1AP_PDU_t *pdu, e1ap_bearer_mod_required_t *out)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_initiatingMessage);
  const E1AP_InitiatingMessage_t *im = pdu->choice.initiatingMessage;
  _EQ_CHECK_LONG(im->procedureCode, E1AP_ProcedureCode_id_bearerContextModificationRequired);
  _EQ_CHECK_INT(im->value.present, E1AP_InitiatingMessage__value_PR_BearerContextModificationRequired);
  const E1AP_BearerContextModificationRequired_t *in = &im->value.choice.BearerContextModificationRequired;

  // Mandatory IEs (TS 38.463 9.2.2.7)
  E1AP_BearerContextModificationRequiredIEs_t *ie = NULL;
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationRequiredIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationRequiredIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationRequiredIEs_t, ie, in, E1AP_ProtocolIE_ID_id_System_BearerContextModificationRequired, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequiredIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationRequiredIEs__value_PR_GNB_CU_UP_UE_E1AP_ID);
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_System_BearerContextModificationRequired: {
        _EQ_CHECK_INT(ie->value.present,
                      E1AP_BearerContextModificationRequiredIEs__value_PR_System_BearerContextModificationRequired);
        const E1AP_System_BearerContextModificationRequired_t *modReq = &ie->value.choice.System_BearerContextModificationRequired;
        switch (modReq->present) {
          case E1AP_System_BearerContextModificationRequired_PR_NOTHING:
            PRINT_ERROR("System Bearer Context Modification Required: no choice present\n");
            break;
          case E1AP_System_BearerContextModificationRequired_PR_e_UTRAN_BearerContextModificationRequired:
            PRINT_ERROR("UTRAN in Bearer Context Modification Required not supported\n");
            return false;
          case E1AP_System_BearerContextModificationRequired_PR_nG_RAN_BearerContextModificationRequired: {
            AssertFatal(modReq->choice.nG_RAN_BearerContextModificationRequired != NULL,
                        "nG_RAN_BearerContextModificationRequired shall not be null");
            const E1AP_ProtocolIE_Container_4932P33_t *msgNGRAN_list =
                (const E1AP_ProtocolIE_Container_4932P33_t *)modReq->choice.nG_RAN_BearerContextModificationRequired;
            for (int j = 0; j < msgNGRAN_list->list.count; j++) {
              const E1AP_NG_RAN_BearerContextModificationRequired_t *msgNGRAN = msgNGRAN_list->list.array[j];
              switch (msgNGRAN->id) {
                case E1AP_ProtocolIE_ID_id_PDU_Session_Resource_Required_To_Modify_List: {
                  _EQ_CHECK_INT(
                      msgNGRAN->value.present,
                      E1AP_NG_RAN_BearerContextModificationRequired__value_PR_PDU_Session_Resource_Required_To_Modify_List);
                  const E1AP_PDU_Session_Resource_Required_To_Modify_List_t *modList =
                      &msgNGRAN->value.choice.PDU_Session_Resource_Required_To_Modify_List;
                  out->numPDUSessionsMod = modList->list.count;
                  if (out->numPDUSessionsMod <= 0 || out->numPDUSessionsMod > NR_MAX_NB_PDU_SESSIONS) {
                    PRINT_ERROR("PDU Session Resource Required To Modify List count %d out of range (1..%d)\n",
                                out->numPDUSessionsMod,
                                NR_MAX_NB_PDU_SESSIONS);
                    return false;
                  }
                  out->pduSessionMod = calloc_or_fail(out->numPDUSessionsMod, sizeof(*out->pduSessionMod));
                  for (int k = 0; k < modList->list.count; k++)
                    CHECK_E1AP_DEC(e1_decode_pdu_session_required_to_mod_item(&out->pduSessionMod[k], modList->list.array[k]));
                  break;
                }
                case E1AP_ProtocolIE_ID_id_PDU_Session_Resource_To_Remove_List: {
                  _EQ_CHECK_INT(msgNGRAN->value.present,
                                E1AP_NG_RAN_BearerContextModificationRequired__value_PR_PDU_Session_Resource_To_Remove_List);
                  const E1AP_PDU_Session_Resource_To_Remove_List_t *remList =
                      &msgNGRAN->value.choice.PDU_Session_Resource_To_Remove_List;
                  out->numPDUSessionsRem = remList->list.count;
                  if (out->numPDUSessionsRem <= 0 || out->numPDUSessionsRem > NR_MAX_NB_PDU_SESSIONS) {
                    PRINT_ERROR("PDU Session Resource To Remove List count %d out of range (1..%d)\n",
                                out->numPDUSessionsRem,
                                NR_MAX_NB_PDU_SESSIONS);
                    return false;
                  }
                  out->pduSessionRem = calloc_or_fail(out->numPDUSessionsRem, sizeof(*out->pduSessionRem));
                  for (int k = 0; k < remList->list.count; k++)
                    CHECK_E1AP_DEC(e1_decode_pdu_session_to_remove_item(&out->pduSessionRem[k], remList->list.array[k]));
                  break;
                }
                default:
                  PRINT_ERROR("Unknown msgNGRAN->id in Bearer Context Modification Required\n");
                  return false;
              }
            }
            break;
          }
          default:
            PRINT_ERROR("System Bearer Context Modification Required: No valid choice present.\n");
            break;
        }
        break;
      }
      default:
        PRINT_ERROR("%s decoding failure: IE %ld is invalid\n", __func__, ie->id);
        return false;
    }
  }
  return true;
}

/** @brief E1AP Bearer Context Modification Required: deep copy */
e1ap_bearer_mod_required_t cp_bearer_context_mod_required(const e1ap_bearer_mod_required_t *msg)
{
  e1ap_bearer_mod_required_t cp = *msg;
  if (msg->numPDUSessionsMod > 0 && msg->pduSessionMod != NULL) {
    cp.pduSessionMod = calloc_or_fail(msg->numPDUSessionsMod, sizeof(*cp.pduSessionMod));
    for (int i = 0; i < msg->numPDUSessionsMod; i++)
      cp.pduSessionMod[i] = msg->pduSessionMod[i];
  }
  if (msg->numPDUSessionsRem > 0 && msg->pduSessionRem != NULL) {
    cp.pduSessionRem = calloc_or_fail(msg->numPDUSessionsRem, sizeof(*cp.pduSessionRem));
    for (int i = 0; i < msg->numPDUSessionsRem; i++)
      cp.pduSessionRem[i] = msg->pduSessionRem[i];
  }
  return cp;
}

/** @brief E1AP Bearer Context Modification Required: equality check */
bool eq_bearer_context_mod_required(const e1ap_bearer_mod_required_t *a, const e1ap_bearer_mod_required_t *b)
{
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  _EQ_CHECK_INT(a->numPDUSessionsMod, b->numPDUSessionsMod);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSessionMod);
  if (a->pduSessionMod != NULL && b->pduSessionMod != NULL) {
    for (int i = 0; i < a->numPDUSessionsMod; i++) {
      _EQ_CHECK_LONG(a->pduSessionMod[i].sessionId, b->pduSessionMod[i].sessionId);
      _EQ_CHECK_INT(a->pduSessionMod[i].numDRB2Modify, b->pduSessionMod[i].numDRB2Modify);
      for (int j = 0; j < a->pduSessionMod[i].numDRB2Modify; j++) {
        _EQ_CHECK_LONG(a->pduSessionMod[i].DRBnGRanModList[j].id, b->pduSessionMod[i].DRBnGRanModList[j].id);
        _EQ_CHECK_INT(a->pduSessionMod[i].DRBnGRanModList[j].cause.type, b->pduSessionMod[i].DRBnGRanModList[j].cause.type);
        _EQ_CHECK_INT(a->pduSessionMod[i].DRBnGRanModList[j].cause.value, b->pduSessionMod[i].DRBnGRanModList[j].cause.value);
      }
    }
  }
  _EQ_CHECK_INT(a->numPDUSessionsRem, b->numPDUSessionsRem);
  _EQ_CHECK_OPTIONAL_PTR(a, b, pduSessionRem);
  if (a->pduSessionRem != NULL && b->pduSessionRem != NULL) {
    for (int i = 0; i < a->numPDUSessionsRem; i++) {
      _EQ_CHECK_LONG(a->pduSessionRem[i].sessionId, b->pduSessionRem[i].sessionId);
      _EQ_CHECK_INT(a->pduSessionRem[i].cause.type, b->pduSessionRem[i].cause.type);
      _EQ_CHECK_INT(a->pduSessionRem[i].cause.value, b->pduSessionRem[i].cause.value);
    }
  }
  return true;
}

/** @brief E1AP Bearer Context Modification Required: free */
void free_e1ap_context_mod_required(const e1ap_bearer_mod_required_t *msg)
{
  free(msg->pduSessionMod);
  free(msg->pduSessionRem);
}

/* ============================================
 *   E1AP Bearer Context Modification Confirm
 * ============================================ */

/** @brief Bearer Context Modification Confirm encoding (9.2.2.8, 3GPP TS 38.463)
 *         gNB-CU-CP -> gNB-CU-UP */
E1AP_E1AP_PDU_t *encode_E1_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *msg)
{
  E1AP_E1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));
  pdu->present = E1AP_E1AP_PDU_PR_successfulOutcome;
  // Message Type (M)
  asn1cCalloc(pdu->choice.successfulOutcome, type);
  type->procedureCode = E1AP_ProcedureCode_id_bearerContextModificationRequired;
  type->criticality = E1AP_Criticality_reject;
  type->value.present = E1AP_SuccessfulOutcome__value_PR_BearerContextModificationConfirm;
  E1AP_BearerContextModificationConfirm_t *out = &type->value.choice.BearerContextModificationConfirm;

  // gNB-CU-CP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationConfirmIEs_t, ie1);
  ie1->id = E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID;
  ie1->criticality = E1AP_Criticality_reject;
  ie1->value.present = E1AP_BearerContextModificationConfirmIEs__value_PR_GNB_CU_CP_UE_E1AP_ID;
  ie1->value.choice.GNB_CU_CP_UE_E1AP_ID = msg->gNB_cu_cp_ue_id;

  // gNB-CU-UP UE E1AP ID (M)
  asn1cSequenceAdd(out->protocolIEs.list, E1AP_BearerContextModificationConfirmIEs_t, ie2);
  ie2->id = E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID;
  ie2->criticality = E1AP_Criticality_reject;
  ie2->value.present = E1AP_BearerContextModificationConfirmIEs__value_PR_GNB_CU_UP_UE_E1AP_ID;
  ie2->value.choice.GNB_CU_UP_UE_E1AP_ID = msg->gNB_cu_up_ue_id;

  return pdu;
}

/** @brief Bearer Context Modification Confirm decoding (9.2.2.8, 3GPP TS 38.463)
 *         gNB-CU-CP -> gNB-CU-UP */
bool decode_E1_bearer_context_mod_confirm(e1ap_bearer_mod_confirm_t *out, const E1AP_E1AP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);
  _EQ_CHECK_INT(pdu->present, E1AP_E1AP_PDU_PR_successfulOutcome);
  E1AP_SuccessfulOutcome_t *type = pdu->choice.successfulOutcome;
  _EQ_CHECK_LONG(type->procedureCode, E1AP_ProcedureCode_id_bearerContextModificationRequired);
  _EQ_CHECK_INT(type->value.present, E1AP_SuccessfulOutcome__value_PR_BearerContextModificationConfirm);
  const E1AP_BearerContextModificationConfirm_t *in = &type->value.choice.BearerContextModificationConfirm;

  // Check mandatory IEs
  E1AP_BearerContextModificationConfirmIEs_t *ie;
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationConfirmIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID, true);
  E1AP_LIB_FIND_IE(E1AP_BearerContextModificationConfirmIEs_t, ie, in, E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; i++) {
    ie = in->protocolIEs.list.array[i];
    AssertFatal(ie != NULL, "in->protocolIEs.list.array[i] shall not be null");
    switch (ie->id) {
      case E1AP_ProtocolIE_ID_id_gNB_CU_CP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationConfirmIEs__value_PR_GNB_CU_CP_UE_E1AP_ID);
        out->gNB_cu_cp_ue_id = ie->value.choice.GNB_CU_CP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_gNB_CU_UP_UE_E1AP_ID:
        _EQ_CHECK_INT(ie->value.present, E1AP_BearerContextModificationConfirmIEs__value_PR_GNB_CU_UP_UE_E1AP_ID);
        out->gNB_cu_up_ue_id = ie->value.choice.GNB_CU_UP_UE_E1AP_ID;
        break;
      case E1AP_ProtocolIE_ID_id_System_BearerContextModificationConfirm:
        break;
      default:
        PRINT_ERROR("%s decoding failure: IE %ld is not handled or invalid\n", __func__, ie->id);
        break;
    }
  }
  return true;
}

/** @brief Bearer Context Modification Confirm equality check */
bool eq_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *a, const e1ap_bearer_mod_confirm_t *b)
{
  _EQ_CHECK_INT(a->gNB_cu_cp_ue_id, b->gNB_cu_cp_ue_id);
  _EQ_CHECK_INT(a->gNB_cu_up_ue_id, b->gNB_cu_up_ue_id);
  return true;
}

/** @brief Bearer Context Modification Confirm deep copy */
e1ap_bearer_mod_confirm_t cp_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *msg)
{
  // Shallow copy only
  e1ap_bearer_mod_confirm_t cp = *msg;
  return cp;
}

/** @brief Bearer Context Modification Confirm free */
void free_e1ap_context_mod_confirm(const e1ap_bearer_mod_confirm_t *msg)
{
  UNUSED(msg);
  // do nothing
}
