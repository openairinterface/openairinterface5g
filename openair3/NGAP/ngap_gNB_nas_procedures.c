/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief NGAP gNb NAS procedure handler
 * @ingroup _ngap
 */

#include "ngap_gNB_nas_procedures.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "BIT_STRING.h"
#include "INTEGER.h"
#include "ngap_msg_includes.h"
#include "OCTET_STRING.h"
#include "T.h"
#include "aper_encoder.h"
#include "asn_application.h"
#include "asn_codecs.h"
#include "assertions.h"
#include "common/utils/T/T.h"
#include "constr_TYPE.h"
#include "conversions.h"
#include "ngap_common.h"
#include "ngap_gNB_defs.h"
#include "ngap_gNB_encoder.h"
#include "ngap_gNB_itti_messaging.h"
#include "ngap_gNB_management_procedures.h"
#include "ngap_gNB_nnsf.h"
#include "ngap_gNB_ue_context.h"
#include "oai_asn1.h"
#include "s1ap_messages_types.h"
#include "xer_encoder.h"
#include "ds/byte_array.h"
#include "intertask_interface.h"
/** @brief Selects the AMF instance for a given UE based on identity information.
 *         It attempts to select an AMF using the following prioritized criteria:
 *         1. GUAMI, if provided and valid (via Region ID, Set ID, Pointer).
 *         2. 5G-S-TMSI, using AMF Set ID if GUAMI is unavailable or unusable.
 *         3. Selected PLMN identity and local PLMN configuration.
 *         4. Fallback to AMF with highest relative capacity, considering load balancing.
 * @ref AMF Discovery and Selection (3GPP TS 23.501 clause 6.3.5) */
static ngap_gNB_amf_data_t *select_amf(ngap_gNB_instance_t *instance_p, const ngap_nas_first_req_t *msg)
{
  ngap_gNB_amf_data_t *amf = NULL;

  // Select the AMF corresponding to the GUAMI from the RegisteredAMF IE
  if (msg->ue_identity.presenceMask & NGAP_UE_IDENTITIES_guami) {
    const nr_guami_t *guami = &msg->ue_identity.guami;
    LOG_D(NGAP,
          "GUAMI is present: MCC=%03d MNC=%0*d RegionID=%d SetID=%d Pointer=%d\n",
          guami->plmn.mcc,
          guami->plmn.mnc_digit_length,
          guami->plmn.mnc,
          guami->amf_region_id,
          guami->amf_set_id,
          guami->amf_pointer);
    amf = ngap_gNB_nnsf_select_amf_by_guami(instance_p, msg->establishment_cause, *guami);
    if (amf) {
      LOG_I(NGAP,
            "UE %d: Selected AMF '%s' (assoc_id %d) through GUAMI MCC=%03d MNC=%0*d AMFRI %d AMFSI %d AMFPT %d\n",
            msg->gNB_ue_ngap_id,
            amf->amf_name,
            amf->assoc_id,
            guami->plmn.mcc,
            guami->plmn.mnc_digit_length,
            guami->plmn.mnc,
            guami->amf_region_id,
            guami->amf_set_id,
            guami->amf_pointer);
      return amf;
    }
  }

  // Select the AMF corresponding to the provided 5G-S-TMSI
  if (amf == NULL) {
    if (msg->ue_identity.presenceMask & NGAP_UE_IDENTITIES_FiveG_s_tmsi) {
      const fiveg_s_tmsi_t *fgs_tmsi = &msg->ue_identity.s_tmsi;
      amf = ngap_gNB_nnsf_select_amf_by_amf_setid(instance_p, msg->establishment_cause, msg->plmn, fgs_tmsi->amf_set_id);
      if (amf) {
        LOG_I(NGAP,
              "UE %d: Selected AMF '%s' (assoc_id %d) through S-TMSI AMFSI %d and selected PLMN MCC=%03d MNC=%0*d\n",
              msg->gNB_ue_ngap_id,
              amf->amf_name,
              amf->assoc_id,
              fgs_tmsi->amf_set_id,
              msg->plmn.mcc,
              msg->plmn.mnc_digit_length,
              msg->plmn.mnc);
        return amf;
      }
    }
  }

  // No UE identity (5G-S-TMSI or GUAMI) is present
  // Select the AMF based on the selected PLMN identity received through RRCSetupComplete
  amf = ngap_gNB_nnsf_select_amf_by_plmn_id(instance_p, msg->establishment_cause, msg->plmn);
  if (amf) {
    LOG_I(NGAP,
          "UE %d: Selected AMF '%s' (assoc_id %d) through selected PLMN MCC=%03d MNC=%0*d\n",
          msg->gNB_ue_ngap_id,
          amf->amf_name,
          amf->assoc_id,
          msg->plmn.mcc,
          msg->plmn.mnc_digit_length,
          msg->plmn.mnc);
    return amf;
  } else {
    // Select the AMF with the highest capacity
    amf = ngap_gNB_nnsf_select_amf(instance_p, msg->establishment_cause);
    if (amf) {
      NGAP_INFO("UE %d: Selected AMF '%s' (assoc_id %d) through highest relative capacity\n",
                msg->gNB_ue_ngap_id,
                amf->amf_name,
                amf->assoc_id);
      return amf;
    }
  }

  return amf;
}

/** @brief Map UE EstablishmentCause (TS 38.331) to NGAP RRCEstablishmentCause (clause 9.3.1.111).
 *  Values 0-9 pass through, otherwise return notAvailable. */
static NGAP_RRCEstablishmentCause_t rrc2ngap_establishment_cause(ngap_rrc_establishment_cause_t cause)
{
  if (cause <= NGAP_RRC_CAUSE_MCS_PRIORITY_ACCESS)
    return cause;
  /* clause 9.3.1.111: notAvailable when the UE cause does not map to any other value */
  return NGAP_RRCEstablishmentCause_notAvailable;
}

/** @brief NAS Transport Messages: Initial UE Message
 *         forward the first received (layer 3) uplink NAS message
 *         from the radio interface to the AMF over N2
 *         NG-RAN node -> AMF (9.2.5.1, 3GPP TS 38.413) */
int ngap_gNB_handle_nas_first_req(instance_t instance, ngap_nas_first_req_t *UEfirstReq)
{
  NGAP_NGAP_PDU_t pdu = {0};
  uint8_t *buffer = NULL;
  uint32_t length = 0;
  DevAssert(UEfirstReq != NULL);
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_t *instance_p = ngap_gNB_get_instance(instance);
  DevAssert(instance_p != NULL);

  // AMF selection from the Initial UE message contents
  struct ngap_gNB_amf_data_s *amf = select_amf(instance_p, UEfirstReq);
  if (amf == NULL) {
    NGAP_WARN("No AMF is associated to the gNB\n");
    return -1;
  }

  // Message Type (M)
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_InitialUEMessage;
  head->criticality = NGAP_Criticality_ignore;
  head->value.present = NGAP_InitiatingMessage__value_PR_InitialUEMessage;
  NGAP_InitialUEMessage_t *out = &head->value.choice.InitialUEMessage;

  /* Create and store NGAP UE context */
  ngap_gNB_ue_context_t ue_desc_p = {
    .amf_ref = amf,
    .gNB_ue_ngap_id = UEfirstReq->gNB_ue_ngap_id,
    .gNB_instance = instance_p,
  };

  // RAN UE NGAP ID (M)
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = ue_desc_p.gNB_ue_ngap_id;
  }

  /* NAS-PDU (M): transferred transparently */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_NAS_PDU;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_NAS_PDU;
    OCTET_STRING_fromBuf(&ie->value.choice.NAS_PDU, (const char *)UEfirstReq->nas_pdu.buf, UEfirstReq->nas_pdu.len);
  }

  // User Location Information (M)
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_UserLocationInformation;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_UserLocationInformation;
    ie->value.choice.UserLocationInformation.present = NGAP_UserLocationInformation_PR_userLocationInformationNR;
    asn1cCalloc(ie->value.choice.UserLocationInformation.choice.userLocationInformationNR, userinfo_nr_p);

    /* NR CGI: use gNB ID and Cell ID passed from RRC */
    MACRO_GNB_ID_TO_CELL_IDENTITY(instance_p->gNB_id, UEfirstReq->nr_cell_id, &userinfo_nr_p->nR_CGI.nRCellIdentity);

    /* Use UE's selected PLMN for nR-CGI (from Initial UE Message) */
    plmn_id_t *plmn = &UEfirstReq->plmn;
    MCC_MNC_TO_TBCD(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &userinfo_nr_p->nR_CGI.pLMNIdentity);

    /* Set TAI - use UE's selected PLMN */
    INT24_TO_OCTET_STRING(instance_p->tac, &userinfo_nr_p->tAI.tAC);
    MCC_MNC_TO_PLMNID(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &userinfo_nr_p->tAI.pLMNIdentity);
  }

  // RRC Establishment Cause (M)
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RRCEstablishmentCause;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_RRCEstablishmentCause;
    ie->value.choice.RRCEstablishmentCause = rrc2ngap_establishment_cause(UEfirstReq->establishment_cause);
  }

  // 5G-S-TMSI (O)
  if (UEfirstReq->ue_identity.presenceMask & NGAP_UE_IDENTITIES_FiveG_s_tmsi) {
    NGAP_DEBUG("FIVEG_S_TMSI_PRESENT\n");
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_FiveG_S_TMSI;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_FiveG_S_TMSI;
    AMF_SETID_TO_BIT_STRING(UEfirstReq->ue_identity.s_tmsi.amf_set_id, &ie->value.choice.FiveG_S_TMSI.aMFSetID);
    AMF_POINTER_TO_BIT_STRING(UEfirstReq->ue_identity.s_tmsi.amf_pointer, &ie->value.choice.FiveG_S_TMSI.aMFPointer);
    M_TMSI_TO_OCTET_STRING(UEfirstReq->ue_identity.s_tmsi.m_tmsi, &ie->value.choice.FiveG_S_TMSI.fiveG_TMSI);
  }

  /* UE Context Request (O): instruct the AMF to trigger an
     Initial Context Setup procedure towards the NG-RAN node. */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialUEMessage_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_UEContextRequest;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialUEMessage_IEs__value_PR_UEContextRequest;
    ie->value.choice.UEContextRequest = NGAP_UEContextRequest_requested;
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    DevMessage("Failed to encode initial UE message\n");
    return -1;
  }

  /* Update the current NGAP UE state */
  ue_desc_p.ue_state = NGAP_UE_WAITING_CSR;
  /* Assign a stream for this UE :
   * From 3GPP 38.412 7)Transport layers:
   *  Within the SCTP association established between one AMF and gNB pair:
   *  - a single pair of stream identifiers shall be reserved for the sole use
   *      of NGAP elementary procedures that utilize non UE-associated signalling.
   *  - At least one pair of stream identifiers shall be reserved for the sole use
   *      of NGAP elementary procedures that utilize UE-associated signallings.
   *      However a few pairs (i.e. more than one) should be reserved.
   *  - A single UE-associated signalling shall use one SCTP stream and
   *      the stream should not be changed during the communication of the
   *      UE-associated signalling.
   */
  amf->nextstream = (amf->nextstream + 1) % amf->out_streams;
  if ((amf->nextstream == 0) && (amf->out_streams > 1)) {
    amf->nextstream += 1;
  }
  ue_desc_p.tx_stream = amf->nextstream;

  /* Create and store UE context */
  ngap_store_ue_context(&ue_desc_p);

  /* Send encoded message over sctp */
  ngap_gNB_itti_send_sctp_data_req(instance_p->instance, amf->assoc_id, buffer, length, ue_desc_p.tx_stream);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}

//------------------------------------------------------------------------------
int ngap_gNB_handle_nas_downlink(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
//------------------------------------------------------------------------------
{

  ngap_gNB_amf_data_t             *amf_desc_p        = NULL;
  ngap_gNB_ue_context_t           *ue_desc_p         = NULL;
  ngap_gNB_instance_t             *ngap_gNB_instance = NULL;
  NGAP_DownlinkNASTransport_t     *container;
  NGAP_DownlinkNASTransport_IEs_t *ie;
  NGAP_RAN_UE_NGAP_ID_t            gnb_ue_ngap_id;
  uint64_t                         amf_ue_ngap_id;
  DevAssert(pdu != NULL);

  /* UE-related procedure -> stream != 0 */
  // if (stream == 0) {
  //     NGAP_ERROR("[SCTP %d] Received UE-related procedure on stream == 0\n",
  //                assoc_id);
  //     return -1;
  // }

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received NAS downlink message for non existing AMF context\n", assoc_id);
    return -1;
  }

  ngap_gNB_instance = amf_desc_p->ngap_gNB_instance;
  /* Prepare the NGAP message to encode */
  container = &pdu->choice.initiatingMessage->value.choice.DownlinkNASTransport;
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkNASTransport_IEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);
  asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);


  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkNASTransport_IEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);
  gnb_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;

  if ((ue_desc_p = ngap_get_ue_context(gnb_ue_ngap_id)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received NAS downlink message for non existing UE context gNB_UE_NGAP_ID: 0x%lx\n",
               assoc_id,
               gnb_ue_ngap_id);
    return -1;
  }

  if (0 == ue_desc_p->rx_stream) {
    ue_desc_p->rx_stream = stream;
  } else if (stream != ue_desc_p->rx_stream) {
    NGAP_ERROR("[SCTP %u] Received UE-related procedure on stream %u, expecting %d\n",
               assoc_id, stream, ue_desc_p->rx_stream);
    return -1;
  }

  /* Is it the first outcome of the AMF for this UE ? If so store the amf
   * UE ngap id.
   */
  if (ue_desc_p->amf_ue_ngap_id == 0) {
    ue_desc_p->amf_ue_ngap_id = amf_ue_ngap_id;
  } else {
    /* We already have a amf ue ngap id check the received is the same */
    if (ue_desc_p->amf_ue_ngap_id != amf_ue_ngap_id) {
      NGAP_ERROR("[SCTP %d] Mismatch in AMF UE NGAP ID (0x%lx != 0x%" PRIx64 "\n", assoc_id, amf_ue_ngap_id, (uint64_t)ue_desc_p->amf_ue_ngap_id);
      return -1;
    }
  }

  /* UE Aggregate Maximum Bit Rate (optional): the NG-RAN node shall store it (TS 38.413 8.6.2.2), the AMF does not
   * repeat it in a later PDU Session Resource Setup Request */
  ngap_ambr_t ue_ambr;
  bool has_ue_ambr = false;
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkNASTransport_IEs_t,
                             ie,
                             container,
                             NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate,
                             false);
  if (ie != NULL) {
    ue_ambr = decode_ngap_UEAggregateMaximumBitRate(&ie->value.choice.UEAggregateMaximumBitRate);
    has_ue_ambr = true;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkNASTransport_IEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_NAS_PDU, true);
  /* Forward the NAS PDU to NR-RRC */
  ngap_gNB_itti_send_nas_downlink_ind(ngap_gNB_instance->instance,
                                      ue_desc_p->gNB_ue_ngap_id,
                                      ue_desc_p->amf_ue_ngap_id,
                                      ie->value.choice.NAS_PDU.buf,
                                      ie->value.choice.NAS_PDU.size,
                                      has_ue_ambr ? &ue_ambr : NULL);

  return 0;
}

//------------------------------------------------------------------------------
int ngap_gNB_nas_uplink(instance_t instance, ngap_uplink_nas_t *ngap_uplink_nas_p)
//------------------------------------------------------------------------------
{
  struct ngap_gNB_ue_context_s  *ue_context_p;
  ngap_gNB_instance_t           *ngap_gNB_instance_p;
  NGAP_NGAP_PDU_t pdu;
  uint8_t  *buffer;
  uint32_t  length;
  DevAssert(ngap_uplink_nas_p != NULL);
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(ngap_uplink_nas_p->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: %08x\n",
              ngap_uplink_nas_p->gNB_ue_ngap_id);
    return -1;
  }

  /* Uplink NAS transport can occur either during an ngap connected state
   * or during initial attach (for example: NAS authentication).
   */
  if (!(ue_context_p->ue_state == NGAP_UE_CONNECTED || ue_context_p->ue_state == NGAP_UE_WAITING_CSR)) {
    NGAP_WARN("You are attempting to send NAS data over non-connected "
              "gNB ue ngap id: %u, current state: %d\n",
              ngap_uplink_nas_p->gNB_ue_ngap_id, ue_context_p->ue_state);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  memset(&pdu, 0, sizeof(pdu));
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_UplinkNASTransport;
  head->criticality = NGAP_Criticality_ignore;
  head->value.present = NGAP_InitiatingMessage__value_PR_UplinkNASTransport;
  NGAP_UplinkNASTransport_t *out = &head->value.choice.UplinkNASTransport;
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UplinkNASTransport_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UplinkNASTransport_IEs__value_PR_AMF_UE_NGAP_ID;
    // ie->value.choice.AMF_UE_NGAP_ID = ue_context_p->amf_ue_ngap_id;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UplinkNASTransport_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UplinkNASTransport_IEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = ue_context_p->gNB_ue_ngap_id;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UplinkNASTransport_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_NAS_PDU;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UplinkNASTransport_IEs__value_PR_NAS_PDU;
    ie->value.choice.NAS_PDU.buf = ngap_uplink_nas_p->nas_pdu.buf;
    ie->value.choice.NAS_PDU.size = ngap_uplink_nas_p->nas_pdu.len;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UplinkNASTransport_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_UserLocationInformation;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_UplinkNASTransport_IEs__value_PR_UserLocationInformation;

    ie->value.choice.UserLocationInformation.present = NGAP_UserLocationInformation_PR_userLocationInformationNR;
    asn1cCalloc(ie->value.choice.UserLocationInformation.choice.userLocationInformationNR, userinfo_nr_p);

    /* Set nRCellIdentity: use gNB ID and Cell ID */
    MACRO_GNB_ID_TO_CELL_IDENTITY(ngap_gNB_instance_p->gNB_id,
                                  ngap_uplink_nas_p->nr_cell_id,
                                  &userinfo_nr_p->nR_CGI.nRCellIdentity);

    /* NR CGI */
    plmn_id_t *plmn = &ngap_uplink_nas_p->plmn;
    MCC_MNC_TO_TBCD(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &userinfo_nr_p->nR_CGI.pLMNIdentity);

    /* TAI */
    INT24_TO_OCTET_STRING(ngap_uplink_nas_p->tac, &userinfo_nr_p->tAI.tAC);
    MCC_MNC_TO_PLMNID(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &userinfo_nr_p->tAI.pLMNIdentity);
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    NGAP_ERROR("Failed to encode uplink NAS transport\n");
    /* Encode procedure has failed... */
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id, buffer,
                                   length, ue_context_p->tx_stream);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}


//------------------------------------------------------------------------------
int ngap_gNB_nas_non_delivery_ind(instance_t instance,
                                  ngap_nas_non_delivery_ind_t *ngap_nas_non_delivery_ind)
//------------------------------------------------------------------------------
{
  struct ngap_gNB_ue_context_s        *ue_context_p;
  ngap_gNB_instance_t                 *ngap_gNB_instance_p;
  NGAP_NGAP_PDU_t pdu;
  uint8_t  *buffer;
  uint32_t  length;
  DevAssert(ngap_nas_non_delivery_ind != NULL);
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(ngap_nas_non_delivery_ind->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: %08x\n",
              ngap_nas_non_delivery_ind->gNB_ue_ngap_id);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  memset(&pdu, 0, sizeof(pdu));
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_NASNonDeliveryIndication;
  head->criticality = NGAP_Criticality_ignore;
  head->value.present = NGAP_InitiatingMessage__value_PR_NASNonDeliveryIndication;
  NGAP_NASNonDeliveryIndication_t *out = &head->value.choice.NASNonDeliveryIndication;
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_NASNonDeliveryIndication_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_NASNonDeliveryIndication_IEs__value_PR_AMF_UE_NGAP_ID;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_NASNonDeliveryIndication_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_NASNonDeliveryIndication_IEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = ue_context_p->gNB_ue_ngap_id;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_NASNonDeliveryIndication_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_NAS_PDU;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_NASNonDeliveryIndication_IEs__value_PR_NAS_PDU;
    ie->value.choice.NAS_PDU.buf = ngap_nas_non_delivery_ind->nas_pdu.buf;
    ie->value.choice.NAS_PDU.size = ngap_nas_non_delivery_ind->nas_pdu.len;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_NASNonDeliveryIndication_IEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_Cause;
    ie->criticality = NGAP_Criticality_ignore;
    /* Send a dummy cause */
    ie->value.present = NGAP_NASNonDeliveryIndication_IEs__value_PR_Cause;
    ie->value.choice.Cause.present = NGAP_Cause_PR_radioNetwork;
    ie->value.choice.Cause.choice.radioNetwork = NGAP_CauseRadioNetwork_radio_connection_with_ue_lost;
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    NGAP_ERROR("Failed to encode NAS NON delivery indication\n");
    /* Encode procedure has failed... */
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id, buffer,
                                   length, ue_context_p->tx_stream);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}

//------------------------------------------------------------------------------
int ngap_gNB_initial_ctxt_resp(instance_t instance, ngap_initial_context_setup_resp_t *initial_ctxt_resp_p)
//------------------------------------------------------------------------------
{

  ngap_gNB_instance_t                   *ngap_gNB_instance_p = NULL;
  struct ngap_gNB_ue_context_s          *ue_context_p        = NULL;
  NGAP_NGAP_PDU_t pdu;
  uint8_t *buffer = NULL;
  uint32_t length;
  int i;

  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(initial_ctxt_resp_p != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(initial_ctxt_resp_p->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: 0x%08x\n",
              initial_ctxt_resp_p->gNB_ue_ngap_id);
    return -1;
  }

  /* Uplink NAS transport can occur either during an ngap connected state
   * or during initial attach (for example: NAS authentication).
   */
  if (!(ue_context_p->ue_state == NGAP_UE_CONNECTED || ue_context_p->ue_state == NGAP_UE_WAITING_CSR)) {
    NGAP_WARN("You are attempting to send NAS data over non-connected "
              "gNB ue ngap id: %08x, current state: %d\n",
              initial_ctxt_resp_p->gNB_ue_ngap_id, ue_context_p->ue_state);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  memset(&pdu, 0, sizeof(pdu));
  pdu.present = NGAP_NGAP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu.choice.successfulOutcome, head);
  head->procedureCode = NGAP_ProcedureCode_id_InitialContextSetup;
  head->criticality = NGAP_Criticality_reject;
  head->value.present = NGAP_SuccessfulOutcome__value_PR_InitialContextSetupResponse;
  NGAP_InitialContextSetupResponse_t *out = &head->value.choice.InitialContextSetupResponse;
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialContextSetupResponseIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupResponseIEs__value_PR_AMF_UE_NGAP_ID;
    // ie->value.choice.AMF_UE_NGAP_ID = ue_context_p->amf_ue_ngap_id;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialContextSetupResponseIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupResponseIEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = initial_ctxt_resp_p->gNB_ue_ngap_id;
  }
  /* optional */
  if (initial_ctxt_resp_p->nb_of_pdusessions){
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialContextSetupResponseIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListCxtRes;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupResponseIEs__value_PR_PDUSessionResourceSetupListCxtRes;
    for (i = 0; i < initial_ctxt_resp_p->nb_of_pdusessions; i++) {
      asn1cSequenceAdd(ie->value.choice.PDUSessionResourceSetupListCxtRes.list, NGAP_PDUSessionResourceSetupItemCxtRes_t, item);
      /* pDUSessionID */
      item->pDUSessionID = initial_ctxt_resp_p->pdusessions[i].pdusession_id;
      // PDU Session Resource Setup Response Transfer (Mandatory)
      byte_array_t ba = encode_ngap_pdusession_setup_response_transfer(&initial_ctxt_resp_p->pdusessions[i]);
      if (ba.buf == NULL) {
        NGAP_ERROR("Failed to encode PDUSessionResourceSetupResponseTransfer for PDU session %ld\n", item->pDUSessionID);
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
        return -1;
      }
      item->pDUSessionResourceSetupResponseTransfer.buf = ba.buf;
      item->pDUSessionResourceSetupResponseTransfer.size = ba.len;
    }
  }
  /* optional */
  if (initial_ctxt_resp_p->nb_of_pdusessions_failed) {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialContextSetupResponseIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListCxtRes;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupResponseIEs__value_PR_PDUSessionResourceFailedToSetupListCxtRes;

    for (i = 0; i < initial_ctxt_resp_p->nb_of_pdusessions_failed; i++) {
      asn1cSequenceAdd(ie->value.choice.PDUSessionResourceFailedToSetupListCxtRes.list, NGAP_PDUSessionResourceFailedToSetupItemCxtRes_t, item);
      /* pDUSessionID */
      item->pDUSessionID = initial_ctxt_resp_p->pdusessions_failed[i].pdusession_id;
      NGAP_INFO("initial context setup response: failed pdusession ID %ld\n", item->pDUSessionID);

      byte_array_t ba = encode_ngap_pdusession_setup_unsuccessful_transfer(&initial_ctxt_resp_p->pdusessions_failed[i].cause);
      if (ba.buf == NULL) {
        NGAP_ERROR("Failed to encode PDUSessionResourceSetupUnsuccessfulTransfer for PDU session %ld\n", item->pDUSessionID);
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
        return -1;
      }
      item->pDUSessionResourceSetupUnsuccessfulTransfer.buf = ba.buf;
      item->pDUSessionResourceSetupUnsuccessfulTransfer.size = ba.len;
    }
  }

  /* optional */
  if (0) {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_InitialContextSetupResponseIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_CriticalityDiagnostics;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupResponseIEs__value_PR_CriticalityDiagnostics;
    // ie->value.choice.CriticalityDiagnostics =;
  }

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NGAP_NGAP_PDU, &pdu);
  }

  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    NGAP_ERROR("Failed to encode InitialContextSetupResponse\n");
    /* Encode procedure has failed... */
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

    /* UE associated signalling -> use the allocated stream */
    LOG_I(NR_RRC,"Send message to sctp: NGAP_InitialContextSetupResponse\n");
    ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance, ue_context_p->amf_ref->assoc_id, buffer, length, ue_context_p->tx_stream);

    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return 0;
}

//---------------------------------------------------------------------------------------------------------
int ngap_gNB_initial_ctxt_fail(instance_t instance, ngap_initial_context_setup_fail_t *initial_ctxt_fail)
//---------------------------------------------------------------------------------------------------------
{
  ngap_gNB_instance_t *ngap_gNB_instance_p = NULL;
  struct ngap_gNB_ue_context_s *ue_context_p = NULL;
  NGAP_NGAP_PDU_t pdu;
  uint8_t *buffer = NULL;
  uint32_t length;

  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(initial_ctxt_fail != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(initial_ctxt_fail->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: 0x%08x\n", initial_ctxt_fail->gNB_ue_ngap_id);
    return -1;
  }
  /* Uplink NAS transport can occur either during an ngap connected state
   * or during initial attach (for example: NAS authentication).
   */
  if (!(ue_context_p->ue_state == NGAP_UE_CONNECTED || ue_context_p->ue_state == NGAP_UE_WAITING_CSR)) {
    NGAP_WARN(
        "You are attempting to send NAS data over non-connected "
        "gNB ue ngap id: %08x, current state: %d\n",
        initial_ctxt_fail->gNB_ue_ngap_id,
        ue_context_p->ue_state);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  memset(&pdu, 0, sizeof(pdu));
  pdu.present = NGAP_NGAP_PDU_PR_unsuccessfulOutcome;
  asn1cCalloc(pdu.choice.unsuccessfulOutcome, out);
  out->procedureCode = NGAP_ProcedureCode_id_InitialContextSetup;
  out->criticality = NGAP_Criticality_reject;
  out->value.present = NGAP_UnsuccessfulOutcome__value_PR_InitialContextSetupFailure;
  NGAP_InitialContextSetupFailure_t *fail = &out->value.choice.InitialContextSetupFailure;
  /* mandatory */
  {
    asn1cSequenceAdd(fail->protocolIEs.list, NGAP_InitialContextSetupFailureIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupFailureIEs__value_PR_AMF_UE_NGAP_ID;
    // ie->value.choice.AMF_UE_NGAP_ID = ue_context_p->amf_ue_ngap_id;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }
  /* mandatory */
  {
    asn1cSequenceAdd(fail->protocolIEs.list, NGAP_InitialContextSetupFailureIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupFailureIEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = initial_ctxt_fail->gNB_ue_ngap_id;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(fail->protocolIEs.list, NGAP_InitialContextSetupFailureIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_Cause;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_InitialContextSetupFailureIEs__value_PR_Cause;
    encode_ngap_cause(&ie->value.choice.Cause, &initial_ctxt_fail->cause);
  }
  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NGAP_NGAP_PDU, &pdu);
  }
  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    NGAP_ERROR("Failed to encode InitialContextSetupFailure\n");
    /* Encode procedure has failed... */
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }
  /* UE associated signalling -> use the allocated stream */
  LOG_I(NR_RRC, "Send message to sctp: NGAP_InitialContextSetupFailure\n");
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance,
                                   ue_context_p->amf_ref->assoc_id,
                                   buffer,
                                   length,
                                   ue_context_p->tx_stream);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}

//------------------------------------------------------------------------------
int ngap_gNB_ue_capabilities(instance_t instance, ngap_ue_cap_info_ind_t *ue_cap_info_ind_p)
//------------------------------------------------------------------------------
{
  ngap_gNB_instance_t          *ngap_gNB_instance_p;
  struct ngap_gNB_ue_context_s *ue_context_p;
  uint8_t  *buffer;
  uint32_t length;
  /* Retrieve the NGAP gNB instance associated with Mod_id */
  ngap_gNB_instance_p = ngap_gNB_get_instance(instance);
  DevAssert(ue_cap_info_ind_p != NULL);
  DevAssert(ngap_gNB_instance_p != NULL);

  if ((ue_context_p = ngap_get_ue_context(ue_cap_info_ind_p->gNB_ue_ngap_id)) == NULL) {
    /* The context for this gNB ue ngap id doesn't exist in the map of gNB UEs */
    NGAP_WARN("Failed to find ue context associated with gNB ue ngap id: %u\n", ue_cap_info_ind_p->gNB_ue_ngap_id);
    return -1;
  }

  /* UE radio capabilities message can occur either during an ngap connected state
   * or during initial attach (for example: NAS authentication).
   */
  if (!(ue_context_p->ue_state == NGAP_UE_CONNECTED || ue_context_p->ue_state == NGAP_UE_WAITING_CSR)) {
    NGAP_WARN(
        "You are attempting to send NAS data over non-connected "
        "gNB ue ngap id: %u, current state: %d\n",
        ue_cap_info_ind_p->gNB_ue_ngap_id,
        ue_context_p->ue_state);
    return -1;
  }

  /* Prepare the NGAP message to encode */
  NGAP_NGAP_PDU_t pdu = {0};
  pdu.present = NGAP_NGAP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu.choice.initiatingMessage, head);
  head->procedureCode = NGAP_ProcedureCode_id_UERadioCapabilityInfoIndication;
  head->criticality = NGAP_Criticality_ignore;
  head->value.present = NGAP_InitiatingMessage__value_PR_UERadioCapabilityInfoIndication;
  NGAP_UERadioCapabilityInfoIndication_t *out = &head->value.choice.UERadioCapabilityInfoIndication;
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UERadioCapabilityInfoIndicationIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UERadioCapabilityInfoIndicationIEs__value_PR_AMF_UE_NGAP_ID;
    asn_uint642INTEGER(&ie->value.choice.AMF_UE_NGAP_ID, ue_context_p->amf_ue_ngap_id);
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UERadioCapabilityInfoIndicationIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID;
    ie->criticality = NGAP_Criticality_reject;
    ie->value.present = NGAP_UERadioCapabilityInfoIndicationIEs__value_PR_RAN_UE_NGAP_ID;
    ie->value.choice.RAN_UE_NGAP_ID = (int64_t)ue_cap_info_ind_p->gNB_ue_ngap_id;
  }
  /* mandatory */
  {
    asn1cSequenceAdd(out->protocolIEs.list, NGAP_UERadioCapabilityInfoIndicationIEs_t, ie);
    ie->id = NGAP_ProtocolIE_ID_id_UERadioCapability;
    ie->criticality = NGAP_Criticality_ignore;
    ie->value.present = NGAP_UERadioCapabilityInfoIndicationIEs__value_PR_UERadioCapability;
    ie->value.choice.UERadioCapability.buf = ue_cap_info_ind_p->ue_radio_cap.buf;
    ie->value.choice.UERadioCapability.size = ue_cap_info_ind_p->ue_radio_cap.len;
  }
  /* optional */
  //NGAP_UERadioCapabilityForPaging TBD
  if (ngap_gNB_encode_pdu(&pdu, &buffer, &length) < 0) {
    /* Encode procedure has failed... */
    NGAP_ERROR("Failed to encode UE radio capabilities indication\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* UE associated signalling -> use the allocated stream */
  ngap_gNB_itti_send_sctp_data_req(ngap_gNB_instance_p->instance, ue_context_p->amf_ref->assoc_id, buffer, length, ue_context_p->tx_stream);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return 0;
}
