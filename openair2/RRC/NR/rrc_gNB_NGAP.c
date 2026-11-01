/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief rrc NGAP procedures for gNB
 */

#include "rrc_gNB_NGAP.h"
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <openair3/ocp-gtpu/gtp_itf.h>
#include <stdbool.h>
#include "common/utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "E1AP_ConfidentialityProtectionIndication.h"
#include "E1AP_IntegrityProtectionIndication.h"
#include "NR_UE-CapabilityRAT-ContainerList.h"
#include "NR_HandoverCommand.h"
#include "NR_HandoverCommand-IEs.h"
#include "E1AP_RLC-Mode.h"
#include "NR_PDCP-Config.h"
#include "NGAP_CauseRadioNetwork.h"
#include "NGAP_Dynamic5QIDescriptor.h"
#include "NGAP_GTPTunnel.h"
#include "NGAP_NonDynamic5QIDescriptor.h"
#include "NGAP_PDUSessionResourceModifyRequestTransfer.h"
#include "NGAP_PDUSessionResourceSetupRequestTransfer.h"
#include "NGAP_QosFlowAddOrModifyRequestItem.h"
#include "NGAP_QosFlowSetupRequestItem.h"
#include "NGAP_asn_constant.h"
#include "NGAP_ProtocolIE-Field.h"
#include "NGAP_CellSize.h"
#include "NR_UE-NR-Capability.h"
#include "NR_UERadioAccessCapabilityInformation.h"
#include "OCTET_STRING.h"
#include "RRC/NR/MESSAGES/asn1_msg.h"
#include "RRC/NR/nr_rrc_common.h"
#include "RRC/NR/nr_rrc_defs.h"
#include "RRC/NR/nr_rrc_proto.h"
#include "RRC/NR/rrc_gNB_UE_context.h"
#include "RRC/NR/rrc_gNB_radio_bearers.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "T.h"
#include "aper_decoder.h"
#include "asn_codecs.h"
#include "assertions.h"
#include "common/ngran_types.h"
#include "common/platform_constants.h"
#include "common/ran_context.h"
#include "common/utils/T/T.h"
#include "constr_TYPE.h"
#include "conversions.h"
#include "e1ap_messages_types.h"
#include "E1AP/lib/e1ap_bearer_context_management.h"
#include "f1ap_messages_types.h"
#include "gtpv1_u_messages_types.h"
#include "intertask_interface.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "oai_asn1.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/lib/f1ap_paging.h"
#include "openair3/SECU/key_nas_deriver.h"
#include "rrc_messages_types.h"
#include "s1ap_messages_types.h"
#include "uper_encoder.h"
#include "rrc_gNB_mobility.h"
#include "rrc_gNB_du.h"
#include "rrc_cell_management.h"
#include "common/utils/alg/find.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/E1AP/lib/e1ap_bearer_context_management.h"

#ifdef E2_AGENT
#include "openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_rc_extern.h"
#endif

/* Masks for NGAP Encryption algorithms, NEA0 is always supported (not coded) */
static const uint16_t NGAP_ENCRYPTION_NEA1_MASK = 0x8000;
static const uint16_t NGAP_ENCRYPTION_NEA2_MASK = 0x4000;
static const uint16_t NGAP_ENCRYPTION_NEA3_MASK = 0x2000;

/* Masks for NGAP Integrity algorithms, NIA0 is always supported (not coded) */
static const uint16_t NGAP_INTEGRITY_NIA1_MASK = 0x8000;
static const uint16_t NGAP_INTEGRITY_NIA2_MASK = 0x4000;
static const uint16_t NGAP_INTEGRITY_NIA3_MASK = 0x2000;

#define INTEGRITY_ALGORITHM_NONE NR_IntegrityProtAlgorithm_nia0

static void set_UE_security_algos(const gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const ngap_security_capabilities_t *cap);

/** @brief Validates PLMN against allowed PLMN list and returns pointer to matching PLMN
 * @param rrc RRC instance
 * @param plmn PLMN to validate
 * @return pointer to matching PLMN in configuration, or NULL if not found */
static const plmn_id_t *get_serving_plmn(gNB_RRC_INST *rrc, const plmn_id_t *plmn)
{
  // Find matching PLMN in configuration
  for (int idx = 0; idx < rrc->configuration.num_plmn; idx++) {
    const plmn_id_t *allowed = &rrc->configuration.plmn[idx];
    if (allowed->mcc == plmn->mcc && allowed->mnc == plmn->mnc && allowed->mnc_digit_length == plmn->mnc_digit_length) {
      LOG_D(NR_RRC, "PLMN (MCC:%d, MNC:%*d) matched with allowed PLMN[%d]\n", plmn->mcc, plmn->mnc_digit_length, plmn->mnc, idx);
      return allowed;
    }
  }
  LOG_W(NR_RRC, "PLMN (MCC:%d, MNC:%*d) not found in allowed PLMN list\n", plmn->mcc, plmn->mnc_digit_length, plmn->mnc);
  return NULL;
}

/*!
 *\brief save security key.
 *\param UE              UE context.
 *\param security_key_pP The security key received from NGAP.
 */
static void set_UE_security_key(gNB_RRC_UE_t *UE, uint8_t *security_key_pP)
{
  int i;

  /* Saves the security key */
  memcpy(UE->kgnb, security_key_pP, SECURITY_KEY_LENGTH);
  memset(UE->nh, 0, SECURITY_KEY_LENGTH);
  /* 3GPP TS 33.501 §6.9.2.1.1: On Initial Context Setup, AMF does not send NH;
   * gNB shall initialize NCC to 0. */
  UE->nh_ncc = 0;

  char ascii_buffer[65];
  for (i = 0; i < 32; i++) {
    sprintf(&ascii_buffer[2 * i], "%02X", UE->kgnb[i]);
  }
  ascii_buffer[2 * 1] = '\0';

  LOG_I(NR_RRC, "[UE %x] Saved security key %s\n", UE->rnti, ascii_buffer);
}

void nr_rrc_pdcp_config_security(gNB_RRC_UE_t *UE, bool enable_ciphering)
{
  static int                          print_keys= 1;

  /* Derive the keys from kgnb */
  nr_pdcp_entity_security_keys_and_algos_t security_parameters;
  /* set ciphering algorithm depending on 'enable_ciphering' */
  security_parameters.ciphering_algorithm = enable_ciphering ? UE->ciphering_algorithm : 0;
  security_parameters.integrity_algorithm = UE->integrity_algorithm;
  /* use current ciphering algorithm, independently of 'enable_ciphering' to derive ciphering key */
  nr_derive_key(RRC_ENC_ALG, UE->ciphering_algorithm, UE->kgnb, security_parameters.ciphering_key);
  nr_derive_key(RRC_INT_ALG, UE->integrity_algorithm, UE->kgnb, security_parameters.integrity_key);

  if ( LOG_DUMPFLAG( DEBUG_SECURITY ) ) {
    if (print_keys == 1 ) {
      print_keys =0;
      LOG_DUMPMSG(NR_RRC, DEBUG_SECURITY, UE->kgnb, 32, "\nKgNB:");
      LOG_DUMPMSG(NR_RRC, DEBUG_SECURITY, security_parameters.ciphering_key, 16,"\nKRRCenc:" );
      LOG_DUMPMSG(NR_RRC, DEBUG_SECURITY, security_parameters.integrity_key, 16,"\nKRRCint:" );
    }
  }

  nr_pdcp_config_set_security(UE->rrc_ue_id, DL_SCH_LCID_DCCH, true, &security_parameters);
}

/** @brief Process AMF Identifier and fill GUAMI struct members */
static nr_guami_t get_guami(const uint32_t amf_Id, const plmn_id_t plmn)
{
  nr_guami_t guami = {0};
  guami.amf_region_id = (amf_Id >> 16) & 0xff;
  guami.amf_set_id = (amf_Id >> 6) & 0x3ff;
  guami.amf_pointer = amf_Id & 0x3f;
  guami.plmn = plmn;
  return guami;
}

/** @brief Copy NGAP PDU Session Transfer item to RRC pdusession_t struct */
static void cp_pdusession_transfer_to_pdusession(pdusession_t *dst, const pdusession_transfer_t *src, int ue_id)
{
  dst->pdu_session_type = src->pdu_session_type;
  dst->n3_incoming = src->n3_incoming;
  /* QoS handling */
  DevAssert(!dst->qos.data);
  DevAssert(src->nb_qos <= MAX_QOS_FLOWS);
  // Initialise mapped QoS list per PDU Session
  seq_arr_init(&dst->qos, sizeof(nr_rrc_qos_t));
  // Add QoS flow to list
  for (uint8_t i = 0; i < src->nb_qos; ++i) {
    if (!add_qos(&dst->qos, &src->qos[i])) {
      LOG_E(NR_RRC, "Failed to add QoS flow %d for PDU session %d\n", src->qos[i].qfi, dst->pdusession_id);
      continue;
    }
    LOG_I(NR_RRC,
          "UE %d: added QoS flow with QFI=%d, total number of QoS flows = %ld\n",
          ue_id,
          src->qos[i].qfi,
          seq_arr_size(&dst->qos));
  }
}

/** @brief Copy NGAP PDU Session Resource item to RRC pdusession_t struct to setup */
static void cp_pdusession_resource_item_to_pdusession(pdusession_t *dst, const pdusession_resource_item_t *src, int ue_id)
{
  dst->pdusession_id = src->pdusession_id;
  dst->nas_pdu = src->nas_pdu;
  dst->nssai = src->nssai;

  // Use the PDU session transfer function to handle QoS and other PDU session transfer-specific fields
  cp_pdusession_transfer_to_pdusession(dst, &src->pdusessionTransfer, ue_id);
}

/**
 * @brief Prepare the Initial UE Message (Uplink NAS) to be forwarded to the AMF over N2
 *        extracts NAS PDU, Selected PLMN and Registered AMF from the RRCSetupComplete
 */
void rrc_gNB_send_NGAP_NAS_FIRST_REQ(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, NR_RRCSetupComplete_IEs_t *rrcSetupComplete)
{
  MessageDef *message_p = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, NGAP_NAS_FIRST_REQ);
  ngap_nas_first_req_t *req = &NGAP_NAS_FIRST_REQ(message_p);

  // RAN UE NGAP ID
  req->gNB_ue_ngap_id = UE->rrc_ue_id;

  // RRC Establishment Cause
  req->establishment_cause =
      UE->establishment_cause <= NGAP_RRC_CAUSE_MCS_PRIORITY_ACCESS ? UE->establishment_cause : NGAP_RRC_CAUSE_NOTAVAILABLE;

  // NAS-PDU
  req->nas_pdu = create_byte_array(rrcSetupComplete->dedicatedNAS_Message.size, rrcSetupComplete->dedicatedNAS_Message.buf);

  /* Selected PLMN Identity (Optional)
   * selectedPLMN-Identity in RRCSetupComplete: Index of the PLMN selected by the UE from the plmn-IdentityInfoList (SIB1) */
  int idx = rrcSetupComplete->selectedPLMN_Identity - 1; // Convert 1-based PLMN Identity IE to 0-based index
  if (idx < 0 || idx >= rrc->configuration.num_plmn) {
    LOG_E(NGAP,
          "Failed to send Initial UE Message: selected PLMN index (%d) is out of bounds [0..%d)\n",
          idx,
          rrc->configuration.num_plmn);
    return;
  }
  req->plmn = rrc->configuration.plmn[idx]; // Select from the stored list
  /* UE is connected to only one PLMN at a time: store this as the serving PLMN */
  UE->serving_plmn = req->plmn;

  nr_rrc_cell_container_t *cell = rrc_get_pcell_for_ue(rrc, UE);
  DevAssert(cell);
  req->nr_cell_id = cell->info.cell_id;
  // PLMN (NR CGI and TAI)
  plmn_id_t *p = &req->plmn;
  LOG_I(NGAP, "Selected PLMN in the NG Initial UE Message: MCC=%03d MNC=%0*d\n", p->mcc, p->mnc_digit_length, p->mnc);

  /* 5G-S-TMSI */
  if (UE->Initialue_identity_5g_s_TMSI.presence) {
    req->ue_identity.presenceMask |= NGAP_UE_IDENTITIES_FiveG_s_tmsi;
    req->ue_identity.s_tmsi.amf_set_id = UE->Initialue_identity_5g_s_TMSI.amf_set_id;
    req->ue_identity.s_tmsi.amf_pointer = UE->Initialue_identity_5g_s_TMSI.amf_pointer;
    req->ue_identity.s_tmsi.m_tmsi = UE->Initialue_identity_5g_s_TMSI.fiveg_tmsi;
  }

  /* Process Registered AMF IE */
  if (rrcSetupComplete->registeredAMF != NULL) {
    /* Fetch the AMF-Identifier from the registeredAMF IE
     * The IE AMF-Identifier (AMFI) comprises of an AMF Region ID (8b),
     * an AMF Set ID (10b) and an AMF Pointer (6b)
     * as specified in TS 23.003 [21], clause 2.10.1. */
    NR_RegisteredAMF_t *r_amf = rrcSetupComplete->registeredAMF;
    req->ue_identity.presenceMask |= NGAP_UE_IDENTITIES_guami;
    uint32_t amf_Id = BIT_STRING_to_uint32(&r_amf->amf_Identifier);
    UE->ue_guami = req->ue_identity.guami = get_guami(amf_Id, req->plmn);
    LOG_I(NGAP,
          "GUAMI in NGAP_NAS_FIRST_REQ (UE %04x): AMF Set ID %u, Region ID %u, Pointer %u\n",
          UE->rnti,
          req->ue_identity.guami.amf_set_id,
          req->ue_identity.guami.amf_region_id,
          req->ue_identity.guami.amf_pointer);
  }

  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, message_p);
}

/// @brief set QoS Flows to Setup in E1 DRB To Setup List
static qos_flow_to_setup_t fill_e1_qos_flow_to_setup(const pdusession_level_qos_parameter_t *qos)
{
  qos_flow_to_setup_t qos_flow = { .qfi = qos->qfi };
  // ARP
  ngran_allocation_retention_priority_t *arp_out = &qos_flow.qos_params.alloc_reten_priority;
  const qos_arp_t *arp_in = &qos->arp;
  arp_out->priority_level = arp_in->priority_level;
  arp_out->preemption_capability = arp_in->pre_emp_capability;
  arp_out->preemption_vulnerability = arp_in->pre_emp_vulnerability;
  // QoS Characteristics
  qos_characteristics_t *qos_characteristics = &qos_flow.qos_params.qos_characteristics;
  if (qos->fiveQI_type == NON_DYNAMIC) {
    const non_dynamic_5qi_t *non_dynamic = &qos->qos_characteristics.non_dynamic;
    typeof(qos_characteristics->non_dynamic) *out_non_dynamic = &qos_characteristics->non_dynamic;
    out_non_dynamic->fiveqi = non_dynamic->fiveQI;
    if (non_dynamic->qos_priority) {
      out_non_dynamic->qos_priority_level = *non_dynamic->qos_priority;
    }
  } else {
    const dynamic_5qi_t *dynamic = &qos->qos_characteristics.dynamic;
    typeof(qos_characteristics->dynamic) *out_dynamic = &qos_characteristics->dynamic;
    if (dynamic->fiveQI != NULL) {
      out_dynamic->fiveqi = *dynamic->fiveQI;
    }
    out_dynamic->qos_priority_level = dynamic->qos_priority;
    out_dynamic->packet_delay_budget = dynamic->packet_delay_budget;
    out_dynamic->packet_error_rate.per_scalar = dynamic->per.scalar;
    out_dynamic->packet_error_rate.per_exponent = dynamic->per.exponent;
  }
  qos_characteristics->qos_type = qos->fiveQI_type;

  return qos_flow;
}

/** @brief Fills E1 PDU Session to Setup item IE */
static pdu_session_to_setup_t fill_e1_pdusession_to_setup(const pdusession_t *session, const nr_security_configuration_t *security)
{
  pdu_session_to_setup_t pdu = {0};
  // Session ID
  pdu.sessionId = session->pdusession_id;
  // NSSAI
  pdu.nssai = session->nssai;
  // Security indication
  security_indication_t *sec = &pdu.securityIndication;
  sec->integrityProtectionIndication = security->do_drb_integrity ? SECURITY_REQUIRED : SECURITY_NOT_NEEDED;
  sec->confidentialityProtectionIndication = security->do_drb_ciphering ? SECURITY_REQUIRED : SECURITY_NOT_NEEDED;
  // UP TL information
  const gtpu_tunnel_t *n3_incoming = &session->n3_incoming;
  pdu.UP_TL_information.teId = n3_incoming->teid;
  memcpy(&pdu.UP_TL_information.tlAddress, n3_incoming->addr.buffer, sizeof(in_addr_t));
  char ip_str[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, n3_incoming->addr.buffer, ip_str, sizeof(ip_str));
  LOG_I(NR_RRC, "PDU Session to Setup: PDU Session ID=%d, incoming TEID=0x%08x, Addr=%s\n", session->pdusession_id, n3_incoming->teid, ip_str);
  return pdu;
}

/** @brief Returns an instance of E1AP DRB To Setup List */
static DRB_nGRAN_to_setup_t fill_e1_drb_to_setup(const drb_t *rrc_drb,
                                                 const pdusession_t *session,
                                                 bool const um_on_default_drb,
                                                 const nr_redcap_ue_cap_t *redcap_cap)
{
  DRB_nGRAN_to_setup_t drb_ngran = {0};
  drb_ngran.id = rrc_drb->drb_id;

  drb_ngran.sdap_config.defaultDRB = (session->sdap_config.default_drb == drb_ngran.id);
  drb_ngran.sdap_config.sDAP_Header_UL = !session->sdap_config.header_ul_absent;
  drb_ngran.sdap_config.sDAP_Header_DL = !session->sdap_config.header_dl_absent;

  drb_ngran.pdcp_config = set_bearer_context_pdcp_config(rrc_drb->pdcp_config, um_on_default_drb, redcap_cap);

  drb_ngran.numCellGroups = 1;
  for (int k = 0; k < drb_ngran.numCellGroups; k++) {
    drb_ngran.cellGroupList[k] = MCG; // 1 cellGroup only
  }

  FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, qos, &session->qos) {
    if (qos->drb_id != drb_ngran.id)
      continue;
    const pdusession_level_qos_parameter_t *qos_session = &qos->qos;
    DevAssert(drb_ngran.numQosFlow2Setup < MAX_QOS_FLOWS);
    drb_ngran.qosFlows[drb_ngran.numQosFlow2Setup++] = fill_e1_qos_flow_to_setup(qos_session);
  }
  DevAssert(drb_ngran.numQosFlow2Setup > 0);

  return drb_ngran;
}

/** @brief Triggers E1 Bearer Context Setup
 * Initiates the setup of bearer contexts for a given UE by preparing and
 * sending an E1AP Bearer Context Setup Request message to the CU-UP.
 * Precondition: CU-UP association is checked by callers before invocation. */
void trigger_bearer_setup(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint64_t ueAggMaxBitRateDownlink)
{
  if (ueAggMaxBitRateDownlink == UINT64_MAX) {
    LOG_E(NR_RRC, "UE %d: UE aggregate maximum bitrate must be known by the NG-RAN node\n", UE->rrc_ue_id);
    return;
  }
  AssertFatal(UE->as_security_active, "logic bug: security should be active when activating DRBs\n");

  // E1 Bearer Context Setup Request
  e1ap_bearer_setup_req_t bearer_req = {
      .gNB_cu_cp_ue_id = UE->rrc_ue_id,
      .secInfo.cipheringAlgorithm = rrc->security.do_drb_ciphering ? UE->ciphering_algorithm : 0,
      .secInfo.integrityProtectionAlgorithm = rrc->security.do_drb_integrity ? UE->integrity_algorithm : 0,
      .ueDlAggMaxBitRate = ueAggMaxBitRateDownlink,
      .anl = activity_notification_level_pdu_session,
  };

  // Collect PDU sessions with NEW status first - store pdusession_t directly to avoid stack overflow
  pdusession_t to_setup[MAX_PDUS_PER_UE];
  uint16_t n_to_setup = 0;
  FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, pduSession, &UE->pduSessions) {
    if (pduSession->status == PDU_SESSION_STATUS_NEW) {
      if (n_to_setup >= MAX_PDUS_PER_UE) {
        LOG_E(NR_RRC, "UE %d: Maximum number of PDU sessions (%d) exceeded\n", UE->rrc_ue_id, MAX_PDUS_PER_UE);
        break;
      }
      memcpy(&to_setup[n_to_setup++], &pduSession->param, sizeof(pdusession_t));
    }
  }
  // Allocate only the exact size needed
  if (n_to_setup > 0) {
    bearer_req.pduSession = calloc_or_fail(n_to_setup, sizeof(*bearer_req.pduSession));
  }

  security_information_t *secInfo = &bearer_req.secInfo;
  nr_derive_key(UP_ENC_ALG, secInfo->cipheringAlgorithm, UE->kgnb, (uint8_t *)secInfo->encryptionKey);
  nr_derive_key(UP_INT_ALG, secInfo->integrityProtectionAlgorithm, UE->kgnb, (uint8_t *)secInfo->integrityProtectionKey);

  e1ap_nssai_t cuup_nssai = {0};
  // Fill allocated array with collected PDU sessions
  for (size_t i = 0; i < n_to_setup; i++) {
    pdusession_t *session = &to_setup[i];
    // Fill E1 PDU Session to setup item
    pdu_session_to_setup_t *pdu = &bearer_req.pduSession[bearer_req.numPDUSessions++];
    *pdu = fill_e1_pdusession_to_setup(session, &rrc->security);
    if (cuup_nssai.sst == 0)
      cuup_nssai = pdu->nssai; /* for CU-UP selection below */
    // Fill E1 DRB to setup item
    FOR_EACH_SEQ_ARR(drb_t *, drb, &UE->drbs) {
      if (drb->pdusession_id != session->pdusession_id) {
        continue;
      }
      // Bounds check for DRB array
      if (pdu->numDRB2Setup >= E1AP_MAX_NUM_DRBS) {
        LOG_E(NR_RRC,
              "UE %d: Maximum number of DRBs (%d) exceeded for PDU session %d\n",
              UE->rrc_ue_id,
              E1AP_MAX_NUM_DRBS,
              session->pdusession_id);
        break;
      }
      pdu->DRBnGRanList[pdu->numDRB2Setup++] = fill_e1_drb_to_setup(drb, session, rrc->configuration.um_on_default_drb, UE->redcap_cap);
    }
    DevAssert(pdu->numDRB2Setup > 0);
  }
  // Check if we have any PDU sessions to setup
  if (bearer_req.numPDUSessions == 0) {
    LOG_W(NR_RRC, "UE %d: No PDU sessions to setup, skipping bearer context setup\n", UE->rrc_ue_id);
    free_e1ap_context_setup_request(&bearer_req);
    return;
  }
  /* Limitation: we assume one fixed CU-UP per UE. We base the selection on
   * NSSAI, but the UE might have multiple PDU sessions with differing slices,
   * in which we might need to select different CU-UPs. In this case, we would
   * actually need to group the E1 bearer context setup for the different
   * CU-UPs, and send them to the different CU-UPs. */
  sctp_assoc_t assoc_id = get_new_cuup_for_ue(rrc, UE, cuup_nssai.sst, cuup_nssai.sd);
  rrc->cucp_cuup.bearer_context_setup(assoc_id, &bearer_req);
  free_e1ap_context_setup_request(&bearer_req);
}

/**
 * @brief Fill PDU Session Resource Failed to Setup Item of the
 *        PDU Session Resource Failed to Setup List for either:
 *        - NGAP PDU Session Resource Setup Response
 *        - NGAP Initial Context Setup Response
 */
static void fill_pdu_session_resource_failed_to_setup_item(pdusession_failed_t *f, int pdusession_id, ngap_cause_t cause)
{
  f->pdusession_id = pdusession_id;
  f->cause = cause;
}

/**
 * @brief Fill Initial Context Setup Response with a PDU Session Resource Failed to Setup List
 *        and send ITTI message to TASK_NGAP
 */
static void send_ngap_initial_context_setup_resp_fail(instance_t instance,
                                                      ngap_initial_context_setup_req_t *msg,
                                                      ngap_cause_t cause)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, instance, NGAP_INITIAL_CONTEXT_SETUP_RESP);
  ngap_initial_context_setup_resp_t *resp = &NGAP_INITIAL_CONTEXT_SETUP_RESP(msg_p);
  resp->gNB_ue_ngap_id = msg->gNB_ue_ngap_id;

  for (int i = 0; i < msg->nb_of_pdusessions; i++) {
    fill_pdu_session_resource_failed_to_setup_item(&resp->pdusessions_failed[i], msg->pdusession[i].pdusession_id, cause);
  }
  resp->nb_of_pdusessions = 0;
  resp->nb_of_pdusessions_failed = msg->nb_of_pdusessions;
  itti_send_msg_to_task(TASK_NGAP, instance, msg_p);
}

//------------------------------------------------------------------------------
int rrc_gNB_process_NGAP_INITIAL_CONTEXT_SETUP_REQ(MessageDef *msg_p, instance_t instance)
//------------------------------------------------------------------------------
{
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  ngap_initial_context_setup_req_t *req = &NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p);

  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, req->gNB_ue_ngap_id);
  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to NGAP and discard it! */
    LOG_W(NR_RRC, "[gNB %ld] In NGAP_INITIAL_CONTEXT_SETUP_REQ: unknown UE from NGAP ids (%u)\n", instance, req->gNB_ue_ngap_id);
    ngap_cause_t cause = { .type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_LOCAL_UE_NGAP_ID};
    rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_FAIL(req->gNB_ue_ngap_id, cause);
    return (-1);
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  UE->amf_ue_ngap_id = req->amf_ue_ngap_id;

  // Directly copy the entire guami structure
  UE->ue_guami = req->guami;

  /* NAS PDU */
  // this is malloced pointers, we pass it for later free()
  UE->nas_pdu = req->nas_pdu;

  /* TS 38.413 8.3.1.2: store the UE Aggregate Maximum Bit Rate when received, even without PDU session to set up:
   * the AMF does not repeat it in a later PDU Session Resource Setup Request */
  if (req->has_ue_ambr) {
    UE->ambr.dl_br = req->ue_ambr.br_dl;
    UE->ambr.ul_br = req->ue_ambr.br_ul;
  }

  if (req->nb_of_pdusessions > 0) {
    AssertFatal(req->has_ue_ambr, "UE aggregate maximum bitrate is required when there are PDU sessions to setup");

    /* Build the list of PDU sessions to actually set up, filtering out those
     * whose PDU Session ID already identifies an active PDU session for this UE.
     * According to TS 38.413, establishment of such a PDU session shall be
     * reported as failed in the response. */
    int ps_count = 0;
    pdusession_t psessions[NR_MAX_NB_PDU_SESSIONS] = {0};

    for (int i = 0; i < req->nb_of_pdusessions; ++i) {
      const pdusession_resource_item_t *src = &req->pdusession[i];

      rrc_pdu_session_param_t *existing = find_pduSession(&UE->pduSessions, src->pdusession_id);
      if (existing != NULL && existing->status == PDU_SESSION_STATUS_ESTABLISHED) {
        LOG_W(NR_RRC, "UE %d: InitialContextSetup contains already active PDU session ID %d\n", UE->rrc_ue_id, src->pdusession_id);
        continue;
      }

      /* This is a PDU session that is not yet active for this UE: add it to the list */
      DevAssert(ps_count < req->nb_of_pdusessions);
      cp_pdusession_resource_item_to_pdusession(&psessions[ps_count++], src, UE->rrc_ue_id);
    }

    UE->n_initial_pdu = 0;
    if (ps_count > 0) {
      UE->initial_pdus = calloc_or_fail(ps_count, sizeof(*UE->initial_pdus));
      memcpy(UE->initial_pdus, psessions, ps_count * sizeof(*UE->initial_pdus));
      UE->n_initial_pdu = ps_count;
    }
  }

  /* security */
  set_UE_security_algos(rrc, UE, &req->security_capabilities);
  set_UE_security_key(UE, req->security_key);

  /* TS 38.413: "store the received Security Key in the UE context and, if the
   * NG-RAN node is required to activate security for the UE, take this
   * security key into use.": I interpret this as "if AS security is already
   * active, don't do anything" */
  if (!UE->as_security_active) {
    /* configure only integrity, ciphering comes after receiving SecurityModeComplete */
    nr_rrc_pdcp_config_security(UE, false);
    rrc_gNB_generate_SecurityModeCommand(rrc, UE);
  } else {
    /* if AS security key is active, we also have the UE capabilities. Then,
     * there are two possibilities: we should set up PDU sessions, and/or
     * forward the NAS message. */
    if (req->nb_of_pdusessions > 0) {
      // do not remove the above allocation which is reused here: this is used
      // in handle_rrcReconfigurationComplete() to know that we need to send a
      // Initial context setup response message
      // Reject bearers setup if there's no CU-UP associated
      if (!is_cuup_associated(rrc)) {
        LOG_W(NR_RRC, "UE %d: reject PDU Session Setup in Initial Context Setup Response\n", UE->rrc_ue_id);
        ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_RESOURCES_NOT_AVAILABLE_FOR_THE_SLICE};
        send_ngap_initial_context_setup_resp_fail(rrc->module_id, req, cause);
        rrc_forward_ue_nas_message(rrc, UE);
        return -1;
      }
      nr_rrc_add_bearers(rrc, UE, UE->n_initial_pdu, UE->initial_pdus);
      trigger_bearer_setup(rrc, UE, UE->ambr.dl_br);
    } else {
      /* no PDU sesion to setup: acknowledge this message, and forward NAS
       * message, if required */
      rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_RESP(rrc, UE);
      rrc_forward_ue_nas_message(rrc, UE);
    }
  }

#ifdef E2_AGENT
  signal_rrc_state_changed_to(UE, RRC_CONNECTED_RRC_STATE_E2SM_RC);
#endif

  return 0;
}

static gtpu_tunnel_t cp_gtp_tunnel(const gtpu_tunnel_t in)
{
  gtpu_tunnel_t out = {0};
  out.teid = in.teid;
  out.addr.length = in.addr.length;
  memcpy(out.addr.buffer, in.addr.buffer, out.addr.length);
  return out;
}

/** @brief Fill NG PDU Session Setup item from stored setup PDU Session in RRC list */
static pdusession_setup_t fill_ngap_pdusession_setup(pdusession_t *session)
{
  DevAssert(session != NULL);
  pdusession_setup_t out = {0};
  out.pdusession_id = session->pdusession_id;
  out.pdu_session_type = session->pdu_session_type;
  out.n3_outgoing = cp_gtp_tunnel(session->n3_outgoing);
  FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, qos_session, &session->qos) {
    DevAssert(out.nb_of_qos_flow < MAX_QOS_FLOWS);
    pdusession_associate_qosflow_t *q = &out.associated_qos_flows[out.nb_of_qos_flow++];
    q->qfi = qos_session->qos.qfi;
    q->qos_flow_mapping_ind = QOSFLOW_MAPPING_INDICATION_DL;
  }
  char ip_str[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, out.n3_outgoing.addr.buffer, ip_str, sizeof(ip_str));
  LOG_I(NR_RRC, "PDU Session Setup: ID=%d, outgoing TEID=0x%08x, Addr=%s\n", out.pdusession_id, out.n3_outgoing.teid, ip_str);
  return out;
}

void rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  MessageDef *msg_p = NULL;
  int pdu_sessions_done = 0;
  int pdu_sessions_failed = 0;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, rrc->module_id, NGAP_INITIAL_CONTEXT_SETUP_RESP);
  ngap_initial_context_setup_resp_t *resp = &NGAP_INITIAL_CONTEXT_SETUP_RESP(msg_p);

  resp->gNB_ue_ngap_id = UE->rrc_ue_id;

  // Optional: PDU Sessions to Setup
  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    if (session->status == PDU_SESSION_STATUS_NEW) {
      resp->pdusessions[pdu_sessions_done++] = fill_ngap_pdusession_setup(&session->param);
      session->status = PDU_SESSION_STATUS_ESTABLISHED;
    } else if (session->status != PDU_SESSION_STATUS_ESTABLISHED) {
      session->status = PDU_SESSION_STATUS_FAILED;
      ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_PDU_SESSION_ID};
      fill_pdu_session_resource_failed_to_setup_item(&resp->pdusessions_failed[pdu_sessions_failed],
                                                     session->param.pdusession_id,
                                                     cause);
      pdu_sessions_failed++;
    }
  }

  resp->nb_of_pdusessions = pdu_sessions_done;
  resp->nb_of_pdusessions_failed = pdu_sessions_failed;
  itti_send_msg_to_task (TASK_NGAP, rrc->module_id, msg_p);
}

void rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_FAIL(uint32_t gnb, const ngap_cause_t causeP)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_INITIAL_CONTEXT_SETUP_FAIL);
  ngap_initial_context_setup_fail_t *fail = &NGAP_INITIAL_CONTEXT_SETUP_FAIL(msg_p);
  fail->gNB_ue_ngap_id = gnb;
  fail->cause = causeP;
  itti_send_msg_to_task(TASK_NGAP, 0, msg_p);
}

static NR_CipheringAlgorithm_t rrc_gNB_select_ciphering(const gNB_RRC_INST *rrc, uint16_t algorithms)
{
  int i;
  /* preset nea0 as fallback */
  int ret = 0;

  /* Select ciphering algorithm based on gNB configuration file and
   * UE's supported algorithms.
   * We take the first from the list that is supported by the UE.
   * The ordering of the list comes from the configuration file.
   */
  for (i = 0; i < rrc->security.ciphering_algorithms_count; i++) {
    int nea_mask[4] = {
      0,
      NGAP_ENCRYPTION_NEA1_MASK,
      NGAP_ENCRYPTION_NEA2_MASK,
      NGAP_ENCRYPTION_NEA3_MASK
    };
    if (rrc->security.ciphering_algorithms[i] == 0) {
      /* nea0 */
      break;
    }
    if (algorithms & nea_mask[rrc->security.ciphering_algorithms[i]]) {
      ret = rrc->security.ciphering_algorithms[i];
      break;
    }
  }

  LOG_D(RRC, "selecting ciphering algorithm %d\n", ret);

  return ret;
}

static e_NR_IntegrityProtAlgorithm rrc_gNB_select_integrity(const gNB_RRC_INST *rrc, uint16_t algorithms)
{
  int i;
  /* preset nia0 as fallback */
  int ret = 0;

  /* Select integrity algorithm based on gNB configuration file and
   * UE's supported algorithms.
   * We take the first from the list that is supported by the UE.
   * The ordering of the list comes from the configuration file.
   */
  for (i = 0; i < rrc->security.integrity_algorithms_count; i++) {
    int nia_mask[4] = {
      0,
      NGAP_INTEGRITY_NIA1_MASK,
      NGAP_INTEGRITY_NIA2_MASK,
      NGAP_INTEGRITY_NIA3_MASK
    };
    if (rrc->security.integrity_algorithms[i] == 0) {
      /* nia0 */
      break;
    }
    if (algorithms & nia_mask[rrc->security.integrity_algorithms[i]]) {
      ret = rrc->security.integrity_algorithms[i];
      break;
    }
  }

  LOG_D(RRC, "selecting integrity algorithm %d\n", ret);

  return ret;
}

/*
 * \brief set security algorithms
 * \param rrc     pointer to RRC context
 * \param UE      UE context
 * \param cap     security capabilities for this UE
 */
static void set_UE_security_algos(const gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const ngap_security_capabilities_t *cap)
{
  /* Save security parameters */
  UE->security_capabilities = *cap;

  /* Select relevant algorithms */
  NR_CipheringAlgorithm_t cipheringAlgorithm = rrc_gNB_select_ciphering(rrc, cap->nRencryption_algorithms);
  e_NR_IntegrityProtAlgorithm integrityProtAlgorithm = rrc_gNB_select_integrity(rrc, cap->nRintegrity_algorithms);

  UE->ciphering_algorithm = cipheringAlgorithm;
  UE->integrity_algorithm = integrityProtAlgorithm;

  LOG_UE_EVENT(UE,
               "Selected security algorithms: ciphering %lx, integrity %x\n",
               cipheringAlgorithm,
               integrityProtAlgorithm);
}

//------------------------------------------------------------------------------
int rrc_gNB_process_NGAP_DOWNLINK_NAS(MessageDef *msg_p, instance_t instance)
//------------------------------------------------------------------------------
{
  ngap_downlink_nas_t *req = &NGAP_DOWNLINK_NAS(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, req->gNB_ue_ngap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to NGAP and discard it! */
    MessageDef *msg_fail_p;
    LOG_W(NR_RRC, "[gNB %ld] In NGAP_DOWNLINK_NAS: unknown UE from NGAP ids (%u)\n", instance, req->gNB_ue_ngap_id);
    msg_fail_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_NAS_NON_DELIVERY_IND);
    ngap_nas_non_delivery_ind_t *msg = &NGAP_NAS_NON_DELIVERY_IND(msg_fail_p);
    msg->gNB_ue_ngap_id = req->gNB_ue_ngap_id;
    msg->nas_pdu = req->nas_pdu;
    // TODO add failure cause when defined!
    itti_send_msg_to_task(TASK_NGAP, instance, msg_fail_p);
    return (-1);
  }

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  UE->amf_ue_ngap_id = req->amf_ue_ngap_id;
  if (req->has_ue_ambr) {
    UE->ambr.dl_br = req->ue_ambr.br_dl;
    UE->ambr.ul_br = req->ue_ambr.br_ul;
  }
  UE->nas_pdu = req->nas_pdu;
  rrc_forward_ue_nas_message(rrc, UE);
  return 0;
}

void rrc_gNB_send_NGAP_UPLINK_NAS(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const NR_UL_DCCH_Message_t *const ul_dcch_msg)
{
  NR_ULInformationTransfer_t *ulInformationTransfer = ul_dcch_msg->message.choice.c1->choice.ulInformationTransfer;

  NR_ULInformationTransfer__criticalExtensions_PR p = ulInformationTransfer->criticalExtensions.present;
  if (p != NR_ULInformationTransfer__criticalExtensions_PR_ulInformationTransfer) {
    LOG_E(NR_RRC, "UE %d: expected presence of ulInformationTransfer, but message has %d\n", UE->rrc_ue_id, p);
    return;
  }

  NR_DedicatedNAS_Message_t *nas = ulInformationTransfer->criticalExtensions.choice.ulInformationTransfer->dedicatedNAS_Message;
  if (!nas) {
    LOG_E(NR_RRC, "UE %d: expected NAS message in ulInformation, but it is NULL\n", UE->rrc_ue_id);
    return;
  }

  uint8_t *buf = malloc_or_fail(nas->size);
  memcpy(buf, nas->buf, nas->size);
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, NGAP_UPLINK_NAS);
  NGAP_UPLINK_NAS(msg_p).gNB_ue_ngap_id = UE->rrc_ue_id;
  NGAP_UPLINK_NAS(msg_p).nas_pdu.len = nas->size;
  NGAP_UPLINK_NAS(msg_p).nas_pdu.buf = buf;
  /* Fill PLMN and location info: use serving PLMN of the UE */
  NGAP_UPLINK_NAS(msg_p).plmn = UE->serving_plmn;
  nr_rrc_cell_container_t *cell = rrc_get_pcell_for_ue(rrc, UE);
  DevAssert(cell);
  NGAP_UPLINK_NAS(msg_p).nr_cell_id = cell->info.cell_id;
  NGAP_UPLINK_NAS(msg_p).tac = cell->info.tac != 0 ? cell->info.tac : rrc->configuration.tac;
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
}

void rrc_gNB_send_NGAP_PDUSESSION_SETUP_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  MessageDef *msg_p;
  int pdu_sessions_done = 0;
  int pdu_sessions_failed = 0;

  msg_p = itti_alloc_new_message (TASK_RRC_GNB, rrc->module_id, NGAP_PDUSESSION_SETUP_RESP);
  ngap_pdusession_setup_resp_t *resp = &NGAP_PDUSESSION_SETUP_RESP(msg_p);
  resp->gNB_ue_ngap_id = UE->rrc_ue_id;

  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    if (session->status == PDU_SESSION_STATUS_NEW) {
      resp->pdusessions[pdu_sessions_done++] = fill_ngap_pdusession_setup(&session->param);
      session->status = PDU_SESSION_STATUS_ESTABLISHED;
    } else if (session->status != PDU_SESSION_STATUS_ESTABLISHED) {
      session->status = PDU_SESSION_STATUS_FAILED;
      pdusession_failed_t *fail = &resp->pdusessions_failed[pdu_sessions_failed];
      fail->pdusession_id = session->param.pdusession_id;
      fail->cause.type = NGAP_CAUSE_RADIO_NETWORK;
      fail->cause.value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_PDU_SESSION_ID;
      pdu_sessions_failed++;
    }
    resp->nb_of_pdusessions = pdu_sessions_done;
    resp->nb_of_pdusessions_failed = pdu_sessions_failed;
  }

  if ((pdu_sessions_done > 0 || pdu_sessions_failed)) {
    LOG_I(NR_RRC, "NGAP_PDUSESSION_SETUP_RESP: sending the message\n");
    itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
  }

  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    session->xid = -1;
  }

  return;
}

/**
 * @brief Fill PDU Session Resource Setup Response with a list of PDU Session Resources Failed to Setup
 *        and send ITTI message to TASK_NGAP
 */
static void send_ngap_pdu_session_setup_resp_fail(instance_t instance, ngap_pdusession_setup_req_t *msg, ngap_cause_t cause)
{
  MessageDef *msg_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_PDUSESSION_SETUP_RESP);
  ngap_pdusession_setup_resp_t *resp = &NGAP_PDUSESSION_SETUP_RESP(msg_resp);
  resp->gNB_ue_ngap_id = msg->gNB_ue_ngap_id;
  resp->nb_of_pdusessions_failed = msg->nb_pdusessions_tosetup;
  resp->nb_of_pdusessions = 0;
  for (int i = 0; i < resp->nb_of_pdusessions_failed; ++i) {
    fill_pdu_session_resource_failed_to_setup_item(&resp->pdusessions_failed[i], msg->pdusession[i].pdusession_id, cause);
  }
  itti_send_msg_to_task(TASK_NGAP, instance, msg_resp);
}

void rrc_gNB_process_NGAP_PDUSESSION_SETUP_REQ(MessageDef *msg_p, instance_t instance)
{
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  ngap_pdusession_setup_req_t* msg=&NGAP_PDUSESSION_SETUP_REQ(msg_p);
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, msg->gNB_ue_ngap_id);
  // Reject PDU Session Resource setup if no UE context is found
  if (ue_context_p == NULL) {
    LOG_W(NR_RRC,
          "[gNB %ld] In NGAP_PDUSESSION_SETUP_REQ: no UE context found from UE NGAP ID (%u)\n",
          instance,
          msg->gNB_ue_ngap_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_LOCAL_UE_NGAP_ID};
    send_ngap_pdu_session_setup_resp_fail(instance, msg, cause);
    return;
  }

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  LOG_I(NR_RRC, "UE %d: received PDU Session Resource Setup Request\n", UE->rrc_ue_id);

  // Reject PDU Session Resource setup if gNB_ue_ngap_id is not matching
  if (UE->rrc_ue_id != msg->gNB_ue_ngap_id) {
    LOG_W(NR_RRC, "[gNB %ld] In NGAP_PDUSESSION_SETUP_REQ: unknown UE NGAP ID (%u)\n", instance, msg->gNB_ue_ngap_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_LOCAL_UE_NGAP_ID};
    send_ngap_pdu_session_setup_resp_fail(instance, msg, cause);
    rrc_forward_ue_nas_message(rrc, UE);
    return;
  }

  // Reject PDU Session Resource setup if there is no security context active
  if (!UE->as_security_active) {
    LOG_E(NR_RRC, "UE %d: no security context active for UE, rejecting PDU Session Resource Setup Request\n", UE->rrc_ue_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_PROTOCOL, .value = NGAP_CAUSE_PROTOCOL_MSG_NOT_COMPATIBLE_WITH_RECEIVER_STATE};
    send_ngap_pdu_session_setup_resp_fail(instance, msg, cause);
    rrc_forward_ue_nas_message(rrc, UE);
    return;
  }

  // Reject PDU session if at least one exists already with that ID.
  // At least one because marking one as existing, and setting up another, that
  // might be more work than is worth it. See 8.2.1.4 in 38.413
  for (int i = 0; i < msg->nb_pdusessions_tosetup; ++i) {
    const pdusession_resource_item_t *p = &msg->pdusession[i];
    rrc_pdu_session_param_t *exist = find_pduSession(&UE->pduSessions, p->pdusession_id);
    if (exist) {
      LOG_E(NR_RRC, "UE %d: already has existing PDU session %d rejecting PDU Session Resource Setup Request\n", UE->rrc_ue_id, p->pdusession_id);
      ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_MULTIPLE_PDU_SESSION_ID_INSTANCES};
      send_ngap_pdu_session_setup_resp_fail(instance, msg, cause);
      rrc_forward_ue_nas_message(rrc, UE);
      return;
    }
  }

  UE->amf_ue_ngap_id = msg->amf_ue_ngap_id;

  pdusession_t to_setup[NR_MAX_NB_PDU_SESSIONS] = {0};
  for (int i = 0; i < msg->nb_pdusessions_tosetup; ++i)
    cp_pdusession_resource_item_to_pdusession(&to_setup[i], &msg->pdusession[i], UE->rrc_ue_id);

  /** Set AMBR if provided (optional) */
  if (msg->has_ue_ambr) {
    UE->ambr.dl_br = msg->ueAggMaxBitRate.br_dl;
    UE->ambr.ul_br = msg->ueAggMaxBitRate.br_ul;
  }

  if (!is_cuup_associated(rrc)) {
    // Reject PDU Session Resource setup if there's no CU-UP associated
    LOG_W(NR_RRC, "UE %d: reject PDU Session Setup in PDU Session Resource Setup Response\n", UE->rrc_ue_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_RESOURCES_NOT_AVAILABLE_FOR_THE_SLICE};
    send_ngap_pdu_session_setup_resp_fail(instance, msg, cause);
    rrc_forward_ue_nas_message(rrc, UE);
    return;
  }
  // Add to UE context lists
  nr_rrc_add_bearers(rrc, UE, msg->nb_pdusessions_tosetup, to_setup);
  // Trigger bearer setup
  trigger_bearer_setup(rrc, UE, UE->ambr.dl_br);
  // Set ongoing_transaction flag to true
  init_delayed_action(&UE->delayed_action);
}

/* Find or create pdu_session_to_mod_t entry in e1_req */
static pdu_session_to_mod_t *find_or_add_pdu_session_mod(e1ap_bearer_mod_req_t *e1_req, int pdusession_id)
{
  // Check if PDU session already exists in modify list
  for (uint8_t i = 0; i < e1_req->numPDUSessionsMod; ++i) {
    if (e1_req->pduSessionMod[i].sessionId == pdusession_id)
      return &e1_req->pduSessionMod[i];
  }
  // Create new entry
  DevAssert(e1_req->numPDUSessionsMod < NR_MAX_NB_PDU_SESSIONS);
  pdu_session_to_mod_t *pdu_mod = &e1_req->pduSessionMod[e1_req->numPDUSessionsMod++];
  memset(pdu_mod, 0, sizeof(*pdu_mod));
  pdu_mod->sessionId = pdusession_id;
  return pdu_mod;
}

/** @brief Find or create E1 DRB_nGRAN_to_mod_t entry inside a PDU session modification
 * If creating a new entry, populates all QoS flows that map to this DRB */
static DRB_nGRAN_to_mod_t *find_or_add_e1_drb_mod(pdu_session_to_mod_t *pdu_mod, long drb_id, const pdusession_t *session)
{
  // Check if DRB already exists in modify list
  for (int i = 0; i < pdu_mod->numDRB2Modify; ++i) {
    if (pdu_mod->DRBnGRanModList[i].id == drb_id)
      return &pdu_mod->DRBnGRanModList[i];
  }
  // Create new entry and populate all QoS flows that map to this DRB
  DevAssert(pdu_mod->numDRB2Modify < E1AP_MAX_NUM_DRBS);
  DRB_nGRAN_to_mod_t *mod = &pdu_mod->DRBnGRanModList[pdu_mod->numDRB2Modify++];
  memset(mod, 0, sizeof(*mod));
  mod->id = drb_id;
  // Populate all QoS flows that map to this DRB
  FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, q, &session->qos) {
    if (q->drb_id != drb_id)
      continue;
    DevAssert(mod->numQosFlowsMod < E1AP_MAX_NUM_QOS_FLOWS);
    qos_flow_to_setup_t *dst = &mod->qosFlows[mod->numQosFlowsMod++];
    *dst = fill_e1_qos_flow_to_setup(&q->qos);
  }
  return mod;
}

/** @brief Override E1 Flow Mapping Information for a given DRB */
static void nr_rrc_override_flow_mapping_info(DRB_nGRAN_to_mod_t *mod, long drb_id, const pdusession_t *session)
{
  mod->numQosFlowsMod = 0;
  FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, q, &session->qos) {
    if (q->drb_id != drb_id)
      continue;
    DevAssert(mod->numQosFlowsMod < E1AP_MAX_NUM_QOS_FLOWS);
    qos_flow_to_setup_t *dst = &mod->qosFlows[mod->numQosFlowsMod++];
    *dst = fill_e1_qos_flow_to_setup(&q->qos);
  }
}

/** @brief Finalize E1 DRB actions after QoS update/release processing:
 *         - remove DRBs no longer referenced by any QoS flow
 *         - refresh DRB-To-Modify QoS list for changed DRBs that are still used
 * @param UE UE context
 * @param pduSession PDU session to check
 * @param e1_req E1AP bearer modification request
 * @param drb_needs_e1_refresh true for DRBs touched by QoS add/modify/release in current request */
static void nr_rrc_send_e1_after_qos_update(gNB_RRC_UE_t *UE,
                                            rrc_pdu_session_param_t *pduSession,
                                            e1ap_bearer_mod_req_t *e1_req,
                                            const bool drb_needs_e1_refresh[MAX_DRBS_PER_UE + 1])
{
  pdusession_t *dst = &pduSession->param;
  // Find or create PDU session entry in e1_req
  pdu_session_to_mod_t *pdu_mod = find_or_add_pdu_session_mod(e1_req, dst->pdusession_id);

  // Check if any remaining QoS for this PDU session still references the same DRB
  FOR_EACH_SEQ_ARR (drb_t *, drb, &UE->drbs) {
    if (drb->pdusession_id != dst->pdusession_id)
      continue;
    bool drb_still_used = false;
    FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, qos, &dst->qos) {
      if (qos->drb_id == drb->drb_id) {
        drb_still_used = true;
        break;
      }
    }
    if (!drb_still_used) {
      if (nr_rrc_remove_drb_by_id(&UE->drbs, drb->drb_id)) {
        LOG_I(NR_RRC,
              "UE %d: removed DRB ID %d for PDU session %d (no remaining QoS flows mapped)\n",
              UE->rrc_ue_id,
              drb->drb_id,
              dst->pdusession_id);
      }
      // Add DRB to remove directly to e1_req
      DevAssert(pdu_mod->n_drb_to_remove < E1AP_MAX_NUM_DRBS);
      drb_to_remove_t *rem = &pdu_mod->drbs_to_remove[pdu_mod->n_drb_to_remove++];
      rem->id = drb->drb_id;
      continue;
    }

    /** For changed DRBs that are still used, refresh Flow Mapping Information in DRB-To-Modify
     * 3GPP TS 38.463 9.3.3.11: overrides previous mapping information. */
    if (drb_needs_e1_refresh[drb->drb_id]) {
      bool found = false;
      for (int j = 0; j < pdu_mod->numDRB2Modify; ++j) {
        if (pdu_mod->DRBnGRanModList[j].id != drb->drb_id)
          continue;
        nr_rrc_override_flow_mapping_info(&pdu_mod->DRBnGRanModList[j], drb->drb_id, dst);
        found = true;
        break;
      }
      if (!found)
        find_or_add_e1_drb_mod(pdu_mod, drb->drb_id, dst);
    }
  }
}

static void nr_rrc_apply_qos_add_modify(gNB_RRC_INST *rrc,
                                        gNB_RRC_UE_t *UE,
                                        pdusession_t *dst,
                                        const pdusession_mod_req_transfer_t *transfer,
                                        e1ap_bearer_mod_req_t *e1_req,
                                        bool drb_needs_e1_refresh[MAX_DRBS_PER_UE + 1])
{
  DevAssert(transfer->nb_qos_to_add_modify <= MAX_QOS_FLOWS);
  DevAssert(e1_req->numPDUSessionsMod < NR_MAX_NB_PDU_SESSIONS);
  pdu_session_to_mod_t *pdu_mod = &e1_req->pduSessionMod[e1_req->numPDUSessionsMod++];
  pdu_mod->sessionId = dst->pdusession_id;

  for (uint8_t i = 0; i < transfer->nb_qos_to_add_modify; ++i) {
    const pdusession_level_qos_parameter_t *q_in = &transfer->qos_to_add_modify[i];
    if (q_in->qfi < 0 || q_in->qfi > 63) {
      LOG_E(NR_RRC, "QoS flow QFI=%d: Invalid QFI (must be 0-63). Skipping QoS flow.\n", q_in->qfi);
      continue;
    }

    const non_dynamic_5qi_t *in_non_dynamic = &q_in->qos_characteristics.non_dynamic;
    const dynamic_5qi_t *in_dynamic = &q_in->qos_characteristics.dynamic;
    if (q_in->fiveQI_type == NON_DYNAMIC && !is_5qi_standardized(in_non_dynamic->fiveQI)) {
      // This is a pre-configured 5QI, not a standardized non-dynamic value: not implemented
      LOG_W(NR_RRC,
            "QoS flow QFI=%d: 5QI %u is not a standardized value (1-9, 65-90). Skipping QoS flow.\n",
            q_in->qfi,
            in_non_dynamic->fiveQI);
      continue;
    }

    nr_rrc_qos_t *qos = find_qos(&dst->qos, q_in->qfi);
    if (qos) {
      // Existing QoS flow: update it and mark DRB for modification
      const int drb_id = qos->drb_id;
      pdusession_level_qos_parameter_t *dst_qos = &qos->qos;
      AssertFatal(qos->qos.qfi == q_in->qfi, "QoS Flow to modify must match existing one");
      LOG_I(NR_RRC, "Updating QoS for QFI=%d\n", q_in->qfi);
      DevAssert(dst_qos->fiveQI_type == q_in->fiveQI_type);

      dst_qos->qfi = q_in->qfi;
      dst_qos->arp = q_in->arp;
      if (q_in->fiveQI_type == DYNAMIC) {
        dynamic_5qi_t *out_dyn = &dst_qos->qos_characteristics.dynamic;
        out_dyn->qos_priority = in_dynamic->qos_priority;
        out_dyn->packet_delay_budget = in_dynamic->packet_delay_budget;
        out_dyn->per = in_dynamic->per;
        if (in_dynamic->fiveQI == NULL) {
          free_and_zero(out_dyn->fiveQI);
        } else {
          if (out_dyn->fiveQI == NULL)
            out_dyn->fiveQI = calloc_or_fail(1, sizeof(*out_dyn->fiveQI));
          *out_dyn->fiveQI = *in_dynamic->fiveQI;
        }
      } else { /* NON_DYNAMIC */
        non_dynamic_5qi_t *out_non_dyn = &dst_qos->qos_characteristics.non_dynamic;
        out_non_dyn->fiveQI = in_non_dynamic->fiveQI;
        if (in_non_dynamic->qos_priority == NULL) {
          free_and_zero(out_non_dyn->qos_priority);
        } else {
          if (out_non_dyn->qos_priority == NULL)
            out_non_dyn->qos_priority = calloc_or_fail(1, sizeof(*out_non_dyn->qos_priority));
          *out_non_dyn->qos_priority = *in_non_dynamic->qos_priority;
        }
      }
      LOG_I(NR_RRC, "UE %d: updating QoS for QFI=%d (DRB=%d)\n", UE->rrc_ue_id, q_in->qfi, drb_id);
      find_or_add_e1_drb_mod(pdu_mod, drb_id, dst);
      drb_needs_e1_refresh[drb_id] = true;
    } else {
      // New QoS flow: add it and either setup new DRB or modify existing DRB
      qos = add_qos(&dst->qos, q_in);
      if (!qos) {
        LOG_E(NR_RRC, "Failed to add QoS flow for QFI=%d\n", q_in->qfi);
        continue;
      }
      LOG_I(NR_RRC,
            "UE %d: added QoS flow with QFI=%d, total number of QoS flows = %ld\n",
            UE->rrc_ue_id,
            q_in->qfi,
            seq_arr_size(&dst->qos));
      bool is_new_drb = nr_rrc_assign_drb_to_qos_flow(rrc, UE, dst, qos);
      if (is_new_drb) {
        // New DRB: add to setup list (DRBnGRanList) and populates QoS flows internally
        drb_t *rrc_drb = get_drb(&UE->drbs, qos->drb_id);
        DevAssert(rrc_drb);
        DevAssert(pdu_mod->numDRB2Setup < E1AP_MAX_NUM_DRBS);
        pdu_mod->DRBnGRanList[pdu_mod->numDRB2Setup++] =
            fill_e1_drb_to_setup(rrc_drb, dst, rrc->configuration.um_on_default_drb, UE->redcap_cap);
      } else {
        // Existing DRB found: mark it for modification (adds to DRBnGRanModList and populates QoS flows)
        DevAssert(qos->drb_id > 0 && qos->drb_id <= MAX_DRBS_PER_UE);
        find_or_add_e1_drb_mod(pdu_mod, qos->drb_id, dst);
        drb_needs_e1_refresh[qos->drb_id] = true;
      }
    }
    qos->ngap_pending = true;
  }
}

static void nr_rrc_apply_qos_release(gNB_RRC_UE_t *UE,
                                     pdusession_t *dst,
                                     const pdusession_mod_req_transfer_t *transfer,
                                     bool drb_needs_e1_refresh[MAX_DRBS_PER_UE + 1])
{
  for (uint8_t i = 0; i < transfer->nb_qos_to_release; ++i) {
    const int qfi = transfer->qos_to_release[i].qfi;
    nr_rrc_qos_t *qos = find_qos(&dst->qos, qfi);
    if (!qos) {
      LOG_W(NR_RRC,
            "UE %d: QoS Flow To Release with QFI=%d not found in PDU session %d, skipping release\n",
            UE->rrc_ue_id,
            qfi,
            dst->pdusession_id);
      continue;
    }
    const int drb_id = qos->drb_id;
    if (!rm_qos(&dst->qos, qfi)) {
      LOG_E(NR_RRC, "UE %d: Failed to remove QoS flow with QFI=%d\n", UE->rrc_ue_id, qfi);
      continue;
    }
    LOG_I(NR_RRC, "UE %d: removed QoS flow with QFI=%d\n", UE->rrc_ue_id, qfi);
    drb_needs_e1_refresh[drb_id] = true;
  }
}

/** @brief Apply one NGAP PDU session modification item (add/modify, release, then E1 Bearer Context Modification). */
static void nr_rrc_update_pdusession(gNB_RRC_INST *rrc,
                                     gNB_RRC_UE_t *UE,
                                     rrc_pdu_session_param_t *session,
                                     const pdusession_resource_mod_item_t *sessMod,
                                     e1ap_bearer_mod_req_t *e1_req)
{
  DevAssert(rrc);
  DevAssert(UE);
  DevAssert(e1_req);
  const pdusession_mod_req_transfer_t *transfer = &sessMod->pdusessionTransfer;
  DevAssert(transfer->nb_qos_to_add_modify <= MAX_QOS_FLOWS);
  bool drb_needs_e1_refresh[MAX_DRBS_PER_UE + 1] = {false};
  pdusession_t *dst = &session->param;
  dst->pdusession_id = sessMod->pdusession_id;
  dst->nas_pdu = sessMod->nas_pdu;
  dst->nssai = sessMod->nssai;
  DevAssert(dst->qos.data);

  nr_rrc_apply_qos_add_modify(rrc, UE, dst, transfer, e1_req, drb_needs_e1_refresh);
  nr_rrc_apply_qos_release(UE, dst, transfer, drb_needs_e1_refresh);

  // Update default DRB in SDAP configuration
  if (!dst->sdap_config.default_drb) {
    bool default_set = false;
    // TS 23.501 §5.7.2.7: the QoS Flow associated with the default QoS rule
    // should be a Non-GBR QoS Flow and the subscribed default 5QI is a
    // Non-GBR 5QI from the standardized value range.
    FOR_EACH_SEQ_ARR (nr_rrc_qos_t *, qos, &dst->qos) {
      if (qos->qos.fiveQI_type == NON_DYNAMIC
          && nr_rrc_is_non_gbr_fiveqi(qos->qos.qos_characteristics.non_dynamic.fiveQI)
          && !default_set) {
        dst->sdap_config.default_drb = qos->drb_id;
        default_set = true;
        break;
      }
    }
    if (!default_set) {
      LOG_E(NR_RRC,
            "No standardized Non-GBR default 5QI QoS flow found in PDU session %d; cannot set SDAP default DRB\n",
            dst->pdusession_id);
    }
  }

  nr_rrc_send_e1_after_qos_update(UE, session, e1_req, drb_needs_e1_refresh);
}

//------------------------------------------------------------------------------
/** @brief Handle abnormal conditions in PDU Session Modify procedure.
 * Send a response with failed of PDU sessions only, for all PDU Sessions in the request */
static int rrc_gNB_ngap_pdusession_mod_failure(int module_id, const ngap_pdusession_modify_req_t *req, const ngap_cause_t cause)
{
  MessageDef *msg_p = itti_alloc_new_message (TASK_RRC_GNB, module_id, NGAP_PDUSESSION_MODIFY_RESP);
  if (msg_p == NULL) {
    LOG_E(NR_RRC, "itti_alloc_new_message failed, msg_p is NULL \n");
    return -1;
  }
  ngap_pdusession_modify_resp_t *resp = &NGAP_PDUSESSION_MODIFY_RESP(msg_p);
  resp->amf_ue_ngap_id = req->amf_ue_ngap_id;
  resp->gNB_ue_ngap_id = req->gNB_ue_ngap_id;
  resp->nb_of_pdusessions_failed = req->nb_pdusessions_tomodify;
  resp->pdusessions_failed->cause = cause;
  for (int i = 0; i < req->nb_pdusessions_tomodify; i++)
    resp->pdusessions_failed[i].pdusession_id = req->pdusession[i].pdusession_id;
  itti_send_msg_to_task(TASK_NGAP, module_id, msg_p);
  return 0;
}

/** Clear ngap_pending on all QoS flows before applying a new NGAP PDU Session Modify */
static void clear_ue_ngap_modify_qos_pending(gNB_RRC_UE_t *UE)
{
  DevAssert(UE != NULL);
  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, s, &UE->pduSessions) {
    FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, q, &s->param.qos)
      q->ngap_pending = false;
  }
}

int rrc_gNB_process_NGAP_PDUSESSION_MODIFY_REQ(const ngap_pdusession_modify_req_t *req, instance_t instance)
{
  gNB_RRC_INST *rrc = RC.nrrrc[instance];

  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, req->gNB_ue_ngap_id);
  if (ue_context_p == NULL) {
    LOG_W(NR_RRC, "[gNB %ld] In NGAP_PDUSESSION_MODIFY_REQ: unknown UE from NGAP ids (%u)\n", instance, req->gNB_ue_ngap_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_INCONSISTENT_REMOTE_UE_NGAP_ID};
    rrc_gNB_ngap_pdusession_mod_failure(rrc->module_id, req, cause);
    return (-1);
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  if (UE->amf_ue_ngap_id != req->amf_ue_ngap_id) {
    LOG_W(NR_RRC,
          "Stored amf_ue_ngap_id %ld for UE %x does not match the requested one %ld\n",
          UE->amf_ue_ngap_id,
          UE->rrc_ue_id,
          req->amf_ue_ngap_id);
    ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_INCONSISTENT_REMOTE_UE_NGAP_ID};
    rrc_gNB_ngap_pdusession_mod_failure(rrc->module_id, req, cause);
    return -1;
  }

  clear_ue_ngap_modify_qos_pending(UE);

  uint8_t xid = rrc_gNB_get_next_transaction_identifier(rrc->module_id);
  bool all_failed = true;
  DevAssert(req->nb_pdusessions_tomodify <= NR_MAX_NB_PDU_SESSIONS);
  e1ap_bearer_mod_req_t e1_req = {0};
  if (req->nb_pdusessions_tomodify > 0) {
    e1_req.pduSessionMod = calloc_or_fail(req->nb_pdusessions_tomodify, sizeof(*e1_req.pduSessionMod));
  }
  for (int i = 0; i < req->nb_pdusessions_tomodify; i++) {
    const pdusession_resource_mod_item_t *sessMod = &req->pdusession[i];
    rrc_pdu_session_param_t *session = find_pduSession(&UE->pduSessions, sessMod->pdusession_id);
    if (!session) {
      /* 8.2.3.4 3GPP TS 38.413: If the NG-RAN node receives unrecognized PDU Session ID IEs,
        the NG-RAN node shall report the corresponding invalid PDU sessions as failed. So we add it to the list. */
      LOG_W(NR_RRC, "Requested modification of non-existing PDU session, refusing modification\n");
      ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_UNKNOWN_PDU_SESSION_ID};
      pdusession_t session = {.pdusession_id = sessMod->pdusession_id};
      rrc_pdu_session_param_t *added = add_pduSession(&UE->pduSessions, &session);
      added->status = PDU_SESSION_STATUS_FAILED;
      added->cause = cause;
    } else {
      all_failed = false;
      session->status = PDU_SESSION_STATUS_TOMODIFY;
      session->xid = xid;
      session->cause.type = NGAP_CAUSE_NOTHING;
      nr_rrc_update_pdusession(rrc, UE, session, sessMod, &e1_req);
    }
  }

  if (!all_failed) {
    // If needed, send E1 Bearer Context Modification for this PDU Session Modify
    if (e1_req.numPDUSessionsMod > 0 && ue_associated_to_cuup(UE)) {
      e1_req.gNB_cu_cp_ue_id = UE->rrc_ue_id;
      e1_req.gNB_cu_up_ue_id = UE->rrc_ue_id;
      sctp_assoc_t assoc_id = get_existing_cuup_for_ue(UE);
      rrc->cucp_cuup.bearer_context_mod(assoc_id, &e1_req);
    }
    // Init delayed PDU Session Modify transaction
    init_delayed_action(&UE->delayed_action);
  } else {
    MessageDef *msg_fail_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_PDUSESSION_MODIFY_RESP);
    if (msg_fail_p == NULL) {
      LOG_E(NR_RRC, "itti_alloc_new_message failed, msg_fail_p is NULL \n");
      return -1;
    }
    rrc_gNB_send_NGAP_PDUSESSION_MODIFY_RESP(rrc, UE, xid);
  }
  free_e1ap_context_mod_request(&e1_req);
  return (0);
}

/** @brief Send PDU Session Resource Setup Response (9.2.1.6 3GPP TS 38.413)
 *  Direction: Direction: NG-RAN node → AMF */
int rrc_gNB_send_NGAP_PDUSESSION_MODIFY_RESP(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid)
{
  if (!seq_arr_size(&UE->pduSessions)) {
    LOG_W(NR_RRC, "UE %d: No PDU sessions in the list, don't send NG PDU Session Modify Response\n", UE->rrc_ue_id);
    return -1;
  }

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, NGAP_PDUSESSION_MODIFY_RESP);
  if (msg_p == NULL) {
    LOG_E(NR_RRC, "itti_alloc_new_message failed, msg_p is NULL \n");
    return (-1);
  }

  ngap_pdusession_modify_resp_t *resp = &NGAP_PDUSESSION_MODIFY_RESP(msg_p);
  resp->gNB_ue_ngap_id = UE->rrc_ue_id;
  resp->amf_ue_ngap_id = UE->amf_ue_ngap_id;

  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    if (xid != session->xid) {
      LOG_D(NR_RRC,
            "xid does not correspond (PDU Session %d, status %d, xid %d/%d) \n ",
            session->param.pdusession_id,
            session->status,
            xid,
            session->xid);
      continue;
    }
    if (session->status == PDU_SESSION_STATUS_TOMODIFY) {
      LOG_I(NR_RRC, "PDU Session Modify successful (pdusession_id=%d) \n", session->param.pdusession_id);
      // Update status
      session->status = PDU_SESSION_STATUS_ESTABLISHED;
      session->cause.type = NGAP_CAUSE_NOTHING;
      // Fill response
      DevAssert(resp->nb_of_pdusessions <= NR_MAX_NB_PDU_SESSIONS);
      pdusession_modify_t *p = &resp->pdusessions[resp->nb_of_pdusessions++];
      p->pdusession_id = session->param.pdusession_id;
      /* QoSFlowAddOrModifyResponseList: flows marked in during QoS flow modification. */
      p->nb_of_qos_flow = 0;
      FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, q, &session->param.qos) {
        if (!q->ngap_pending)
          continue;
        DevAssert(p->nb_of_qos_flow < MAX_QOS_FLOWS);
        p->qos[p->nb_of_qos_flow++].qfi = q->qos.qfi;
        q->ngap_pending = false;
      }
      if (p->nb_of_qos_flow == 0) {
        LOG_D(NR_RRC,
              "UE %d: empty QoSFlowAddOrModifyResponseList (no pending QoS flows for pdu_session_id=%d)\n",
              UE->rrc_ue_id,
              session->param.pdusession_id);
      }
    } else if (session->status == PDU_SESSION_STATUS_FAILED) {
      DevAssert(resp->nb_of_pdusessions_failed <= NR_MAX_NB_PDU_SESSIONS);
      pdusession_failed_t *failed = &resp->pdusessions_failed[resp->nb_of_pdusessions_failed++];
      failed->pdusession_id = session->param.pdusession_id;
      failed->cause = session->cause;
      rm_pduSession(&UE->pduSessions, &UE->drbs, session->param.pdusession_id);
    } else
      LOG_W(NR_RRC, "PDU Session Modify: unexpected PDU Session status %d for PDU Session %d \n ", session->status, session->param.pdusession_id);
  }

  // Send message to NGAP (always send: if no PDU sessions, only mandatory IEs)
  LOG_D(NR_RRC, "Send NG PDU Session Modify Response (nb_of_pdusessions %d, xid %d)\n", resp->nb_of_pdusessions, xid);
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);

  return 0;
}

/** @brief Send UE Context Release Request (NG-RAN node initiated)
 * Direction: NG-RAN node -> AMF (8.3.2.2 3GPP TS 38.413) */
void rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(const module_id_t gnb_mod_idP,
                                              const rrc_gNB_ue_context_t *const ue_context_pP,
                                              const ngap_cause_t causeP)
//------------------------------------------------------------------------------
{
  if (ue_context_pP == NULL) {
    LOG_E(RRC, "[gNB] In NGAP_UE_CONTEXT_RELEASE_REQ: invalid UE\n");
  } else {
    const gNB_RRC_UE_t *UE = &ue_context_pP->ue_context;
    MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_UE_CONTEXT_RELEASE_REQ);
    ngap_ue_release_req_t *req = &NGAP_UE_CONTEXT_RELEASE_REQ(msg);
    req->gNB_ue_ngap_id = UE->rrc_ue_id;
    req->cause.type = causeP.type;
    req->cause.value = causeP.value;
    // PDU Session Resource List (optional)
    FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
      DevAssert(req->nb_of_pdusessions < NR_MAX_NB_PDU_SESSIONS);
      req->pdusession_ids[req->nb_of_pdusessions] = session->param.pdusession_id;
      req->nb_of_pdusessions++;
    }
    itti_send_msg_to_task(TASK_NGAP, GNB_MODULE_ID_TO_INSTANCE(gnb_mod_idP), msg);
  }
}

/** @brief Sends the NG Handover Failure from the Target NG-RAN to the AMF */
void rrc_gNB_send_NGAP_HANDOVER_FAILURE(gNB_RRC_INST *rrc, ngap_handover_failure_t *msg)
{
  LOG_I(NR_RRC, "Send NG Handover Failure message (amf_ue_ngap_id %ld) with cause %d \n ", msg->amf_ue_ngap_id, msg->cause.value);
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_HANDOVER_FAILURE);
  NGAP_HANDOVER_FAILURE(msg_p) = *msg;
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
}

/** @brief Process NG Handover Request message (8.4.2.2 3GPP TS 38.413) */
int rrc_gNB_process_Handover_Request(gNB_RRC_INST *rrc, ngap_handover_request_t *msg)
{
  rrc_gNB_ue_context_t *existing_ue_context = rrc_gNB_get_ue_context_by_amf_ue_ngap_id(rrc, msg->amf_ue_ngap_id);
  if (existing_ue_context != NULL) {
    gNB_RRC_UE_t *ue = &existing_ue_context->ue_context;
    /* A UE with this AMF-UE-NGAP-ID is already present on this node as an N2
     * source. This can happen when source and target NG-RAN node are the same or
     * when the UE is already in a N2 HO procedure. Allow the request only while that
     * UE is still in N2 source preparation (target not yet allocated), otherwise reject
     * to prevent duplicate target contexts. */
    const bool n2_source_only = ue->ho_context && ue->ho_context->source && !ue->ho_context->target;
    if (!n2_source_only) {
      LOG_E(NR_RRC,
            "Reject Handover Request: ongoing procedure for UE %u (AMF UE NGAP ID %ld)\n",
            ue->rrc_ue_id,
            msg->amf_ue_ngap_id);
      ngap_handover_failure_t fail = {
          .amf_ue_ngap_id = msg->amf_ue_ngap_id,
          .cause.type = NGAP_CAUSE_RADIO_NETWORK,
          .cause.value = NGAP_CAUSE_RADIO_NETWORK_INTERACTION_WITH_OTHER_PROCEDURE,
      };
      rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
      return -1;
    }
    LOG_I(NR_RRC, "N2 HO to self (AMF UE NGAP ID %ld): creating target context for UE %u\n", msg->amf_ue_ngap_id, ue->rrc_ue_id);
  }

  // Get cell by cell_id
  nr_rrc_cell_container_t *cell = get_cell_by_cell_id(&rrc->cells, msg->nr_cell_id);
  if (cell == NULL) {
    /* Cell Not Found! Return HO Request Failure*/
    LOG_E(RRC, "Failed to process Handover Request: no cell found with NR Cell ID=%lu \n", msg->nr_cell_id);
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_RADIO_RESOURCES_NOT_AVAILABLE,
    };
    rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
    return -1;
  }

  struct nr_rrc_du_container_t *du = get_du_by_assoc_id(rrc, cell->assoc_id);
  if(du == NULL) {
    LOG_E(NR_RRC, "Failed to process Handover Request: no DU found with assoc_id=%d\n", cell->assoc_id);
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM,
    };
    rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
    return -1;
  }

  // Validate PLMN from GUAMI against allowed PLMN list
  const plmn_id_t *serving_plmn = get_serving_plmn(rrc, &msg->guami.plmn);
  if (!serving_plmn) {
    LOG_E(NR_RRC, "PLMN from GUAMI not supported - rejecting handover\n");
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_TARGET_NOT_ALLOWED,
    };
    rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
    return -1;
  }

  // Create UE context
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_create_ue_context(du->assoc_id, UINT16_MAX, rrc, UINT64_MAX, UINT32_MAX);
  if (!ue_context_p) {
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM,
    };
    rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
    return -1;
  }

  uint16_t pci = cell->info.pci;
  LOG_I(NR_RRC, "Received Handover Request (on NR Cell ID=%lu, PCI=%u) \n", msg->nr_cell_id, pci);

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  // allocate context for target
  UE->ho_context = alloc_ho_ctx(HO_CTX_TARGET);
  UE->ho_context->target->cell = cell;
  UE->ho_context->target->ho_trigger = nr_rrc_trigger_n2_ho_target;

  // Store IDs in UE context
  UE->amf_ue_ngap_id = msg->amf_ue_ngap_id;
  UE->ue_guami = msg->guami;
  // Store the serving PLMN
  UE->serving_plmn = *serving_plmn;

  UE->ho_context->target->ue_ho_prep_info = copy_byte_array(msg->ue_ho_prep_info);
  // store the received UE Security Capabilities in the UE context
  FREE_AND_ZERO_BYTE_ARRAY(UE->ue_cap_buffer);
  UE->ue_cap_buffer = copy_byte_array(msg->ue_cap);

  /* store the received Security Context in the UE context
     and take it into use as defined in TS 33.501 */
  set_UE_security_algos(rrc, UE, &msg->security_capabilities);
  UE->nh_ncc = msg->security_context.next_hop_chain_count;
  memcpy(UE->nh, msg->security_context.next_hop, SECURITY_KEY_LENGTH);
  // Reset KgNB
  memset(UE->kgnb, 0, SECURITY_KEY_LENGTH);
  // Derive KgNB*
  const nr_rrc_cell_info_t *cell_info = &cell->info;
  uint32_t ssb_arfcn = get_ssb_arfcn(cell);
  nr_derive_key_ng_ran_star(cell_info->pci, ssb_arfcn, UE->nh, UE->kgnb);
  UE->as_security_active = true;
  // Activate SRBs
  activate_srb(UE, SRB1);
  activate_srb(UE, SRB2);
  // During N2 handover, the UE continues using the old security context from the source gNB
  // until it receives and processes the RRC Reconfiguration with masterKeyUpdate. The first
  // PDUs sent after CFRA are still ciphered with the old keys. The target gNB must enable
  // ciphering during handover setup to correctly decipher and verify integrity of these PDUs.
  nr_rrc_pdcp_config_security(UE, true);

  // Process all PDU Session Resource Setup items from handover request
  DevAssert(msg->nb_of_pdusessions <= NR_MAX_NB_PDU_SESSIONS);
  pdusession_t to_setup[NR_MAX_NB_PDU_SESSIONS] = {0};
  for (int i = 0; i < msg->nb_of_pdusessions; i++) {
    ho_request_pdusession_t *ho_pdu = &msg->pduSessionResourceSetupList[i];
    pdusession_t *pdu = &to_setup[i];
    pdu->nssai = ho_pdu->nssai;
    pdu->pdu_session_type = ho_pdu->pdu_session_type;
    pdu->pdusession_id = ho_pdu->pdusession_id;
    cp_pdusession_transfer_to_pdusession(pdu, &ho_pdu->pdusessionTransfer, UE->rrc_ue_id);
  }

  // Store UE aggregate maximum bitrate
  UE->ambr.dl_br = msg->ue_ambr.br_dl;
  UE->ambr.ul_br = msg->ue_ambr.br_ul;

  if (!is_cuup_associated(rrc)) {
    LOG_E(NR_RRC, "Failed to establish PDU session: handover failed\n");
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM,
    };
    rrc_gNB_send_NGAP_HANDOVER_FAILURE(rrc, &fail);
    rrc_remove_ue(rrc, ue_context_p);
    return -1;
  }
  // Add to UE context lists
  nr_rrc_add_bearers(rrc, UE, msg->nb_of_pdusessions, to_setup);
  // Trigger bearer setup
  trigger_bearer_setup(rrc, UE, UE->ambr.dl_br);
  return 0;
}

void rrc_gNB_free_Handover_Request(ngap_handover_request_t *msg)
{
  free_byte_array(msg->ue_cap);
  free_byte_array(msg->ue_ho_prep_info);
  free(msg->mobility_restriction);
}

/** @brief Send NG Uplink RAN Status Transfer message (8.4.6 3GPP TS 38.413)
 * Direction: source NG-RAN node -> AMF */
int rrc_gNB_send_NGAP_ul_ran_status_transfer(gNB_RRC_INST *rrc,
                                             gNB_RRC_UE_t *UE,
                                             const int n_to_mod,
                                             const int *drb_ids,
                                             const e1_pdcp_status_info_t *pdcp_status)
{
  AssertFatal(UE != NULL, "UE context is NULL\n");
  DevAssert(n_to_mod <= MAX_DRBS_PER_UE);
  DevAssert(drb_ids);

  LOG_I(NR_RRC,
        "Sending NGAP Uplink RAN Status Transfer (AMF_UE_NGAP_ID=%" PRIu64 ", GNB_UE_NGAP_ID=%u)\n",
        UE->amf_ue_ngap_id,
        UE->rrc_ue_id);

  ngap_ran_status_transfer_t msg = {
      .amf_ue_ngap_id = UE->amf_ue_ngap_id,
      .gnb_ue_ngap_id = UE->rrc_ue_id,
  };

  // Loop through DRBs and extract COUNT values
  for (int i = 0; i < n_to_mod; ++i) {
    int drb_id = drb_ids[i];

    // Find the DRB in the UE's DRB list
    drb_t *drb = get_drb(&UE->drbs, drb_id);
    if (!drb) {
      LOG_E(NR_RRC, "Failed to send UL RAN Status Transfer: DRB %d not found\n", drb_id);
      continue;
    }

    bool sn_length_18 = drb->pdcp_config.drb.sn_size == 18;

    DevAssert(msg.ran_status.nb_drb < MAX_DRBS_PER_UE);
    ngap_drb_status_t *item = &msg.ran_status.drb_status_list[msg.ran_status.nb_drb++];
    item->drb_id = drb_id;

    const e1_pdcp_count_t *ul_pdcp = &pdcp_status[i].ul_count;
    const e1_pdcp_count_t *dl_pdcp = &pdcp_status[i].dl_count;

    item->ul_count.pdcp_sn = ul_pdcp->sn;
    item->ul_count.hfn = ul_pdcp->hfn;
    item->ul_count.sn_len = sn_length_18 ? NGAP_SN_LENGTH_18 : NGAP_SN_LENGTH_12;

    item->dl_count.pdcp_sn = dl_pdcp->sn;
    item->dl_count.hfn = dl_pdcp->hfn;
    item->dl_count.sn_len = sn_length_18 ? NGAP_SN_LENGTH_18 : NGAP_SN_LENGTH_12;
  }

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_UL_RAN_STATUS_TRANSFER);
  NGAP_UL_RAN_STATUS_TRANSFER(msg_p) = msg;
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);

  return 0;
}

/** @brief Process NG Handover Command on Source gNB */
void rrc_gNB_process_HandoverCommand(gNB_RRC_INST *rrc, const ngap_handover_command_t *msg)
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, msg->gNB_ue_ngap_id);

  if (ue_context_p == NULL) {
    LOG_W(NR_RRC, "Unknown UE context associated to gNB_ue_ngap_id (%u)\n", msg->gNB_ue_ngap_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  byte_array_t buffer = doRRCReconfiguration_from_HandoverCommand(msg->handoverCommand);
  if (!buffer.buf || buffer.len == 0) {
    LOG_E(NR_RRC, "Failed to decode/encode RRCReconfiguration from HandoverCommand\n");
    // Notify AMF that handover was cancelled
    DevAssert(UE->ho_context);
    DevAssert(UE->ho_context->source);
    DevAssert(UE->ho_context->source->ho_cancel);
    UE->ho_context->source->ho_cancel(rrc, UE);
    return;
  }

  rrc_gNB_trigger_reconfiguration_for_handover(rrc, UE, buffer.buf, buffer.len);
  LOG_A(NR_RRC, "Send reconfiguration (HO Command) to UE %u/RNTI %04x\n", UE->rrc_ue_id, UE->rnti);
  free_byte_array(buffer);
}

void rrc_gNB_free_Handover_Command(ngap_handover_command_t *msg)
{
  free_byte_array(msg->handoverCommand);
}

/*
* Process the NG command NGAP_UE_CONTEXT_RELEASE_COMMAND, sent by AMF.
* The gNB should remove all pdu session, NG context, and other context of the UE.
*/
int rrc_gNB_process_NGAP_UE_CONTEXT_RELEASE_COMMAND(MessageDef *msg_p, instance_t instance)
{
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  uint32_t gNB_ue_ngap_id = NGAP_UE_CONTEXT_RELEASE_COMMAND(msg_p).gNB_ue_ngap_id;
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(RC.nrrrc[instance], gNB_ue_ngap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index */
    LOG_W(NR_RRC, "[gNB %ld] In NGAP_UE_CONTEXT_RELEASE_COMMAND: unknown UE from gNB_ue_ngap_id (%u)\n",
          instance,
          gNB_ue_ngap_id);
    rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_COMPLETE(instance, gNB_ue_ngap_id, NULL);
    return -1;
  }

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  // Counts the number of F1 and N2 handovers to target_nci cell.
  if (UE->ho_context && UE->ho_context->source)
    nr_rrc_mark_ho_completed(rrc, UE->ho_context->source->cell->info.cell_id, UE->ho_context->source->target_nci);

  UE->an_release = true;
#ifdef E2_AGENT
  signal_rrc_state_changed_to(UE, RRC_IDLE_RRC_STATE_E2SM_RC);
#endif

  /* a UE might not be associated to a CU-UP if it never requested a PDU
   * session (intentionally, or because of erros) */
  if (ue_associated_to_cuup(UE)) {
    sctp_assoc_t assoc_id = get_existing_cuup_for_ue(UE);
    e1ap_cause_t cause = {.type = E1AP_CAUSE_RADIO_NETWORK, .value = E1AP_RADIO_CAUSE_NORMAL_RELEASE};
    e1ap_bearer_release_cmd_t cmd = {
      .gNB_cu_cp_ue_id = UE->rrc_ue_id,
      .gNB_cu_up_ue_id = UE->rrc_ue_id,
      .cause = cause,
    };
    rrc->cucp_cuup.bearer_context_release(assoc_id, &cmd);
  }

  /* special case: the DU might be offline, in which case the f1_ue_data exists
   * but is set to 0 */
  if (cu_exists_f1_ue_data(UE->rrc_ue_id) && cu_get_f1_ue_data(UE->rrc_ue_id).du_assoc_id != 0) {
    rrc_gNB_generate_RRCRelease(rrc, UE);

    /* UE will be freed after UE context release complete */
  } else {
    // the DU is offline already
    rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_COMPLETE(0, UE->rrc_ue_id, &UE->pduSessions);
    rrc_remove_ue(rrc, ue_context_p);
  }

  return 0;
}

void rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_COMPLETE(instance_t instance, uint32_t gNB_ue_ngap_id, seq_arr_t *pdusessions)
{
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_UE_CONTEXT_RELEASE_COMPLETE);
  ngap_ue_release_complete_t *complete = &NGAP_UE_CONTEXT_RELEASE_COMPLETE(msg);
  complete->gNB_ue_ngap_id = gNB_ue_ngap_id;
  // PDU Session Resource List (Optional)
  if (pdusessions && seq_arr_size(pdusessions)) {
    FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, pdusessions) {
      complete->pdu_session_id[complete->num_pdu_sessions++] = session->param.pdusession_id;
    }
  }
  itti_send_msg_to_task(TASK_NGAP, instance, msg);
}

void rrc_gNB_send_NGAP_UE_CAPABILITIES_IND(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, const NR_UECapabilityInformation_t *const ue_cap_info)
//------------------------------------------------------------------------------
{
  NR_UE_CapabilityRAT_ContainerList_t *ueCapabilityRATContainerList =
      ue_cap_info->criticalExtensions.choice.ueCapabilityInformation->ue_CapabilityRAT_ContainerList;
  void *buf;
  NR_UERadioAccessCapabilityInformation_t rac = {0};

  if (ueCapabilityRATContainerList->list.count == 0) {
    LOG_W(RRC, "[UE %d] bad UE capabilities\n", UE->rrc_ue_id);
    }

    int ret = uper_encode_to_new_buffer(&asn_DEF_NR_UE_CapabilityRAT_ContainerList, NULL, ueCapabilityRATContainerList, &buf);
    AssertFatal(ret > 0, "fail to encode ue capabilities\n");

    rac.criticalExtensions.present = NR_UERadioAccessCapabilityInformation__criticalExtensions_PR_c1;
    asn1cCalloc(rac.criticalExtensions.choice.c1, c1);
    c1->present = NR_UERadioAccessCapabilityInformation__criticalExtensions__c1_PR_ueRadioAccessCapabilityInformation;
    asn1cCalloc(c1->choice.ueRadioAccessCapabilityInformation, info);
    info->ue_RadioAccessCapabilityInfo.buf = buf;
    info->ue_RadioAccessCapabilityInfo.size = ret;
    info->nonCriticalExtension = NULL;
    /* 8192 is arbitrary, should be big enough */
    void *buf2 = NULL;
    int encoded = uper_encode_to_new_buffer(&asn_DEF_NR_UERadioAccessCapabilityInformation, NULL, &rac, &buf2);

    AssertFatal(encoded > 0, "fail to encode ue capabilities\n");
    ;
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UERadioAccessCapabilityInformation, &rac);
    MessageDef *msg_p;
    msg_p = itti_alloc_new_message (TASK_RRC_GNB, rrc->module_id, NGAP_UE_CAPABILITIES_IND);
    ngap_ue_cap_info_ind_t *ind = &NGAP_UE_CAPABILITIES_IND(msg_p);
    ind->gNB_ue_ngap_id = UE->rrc_ue_id;
    ind->ue_radio_cap.len = encoded;
    ind->ue_radio_cap.buf = buf2;
    itti_send_msg_to_task (TASK_NGAP, rrc->module_id, msg_p);
    LOG_I(NR_RRC,"Send message to ngap: NGAP_UE_CAPABILITIES_IND\n");
}

void rrc_gNB_send_NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, byte_array_t ho_command)
{
  LOG_D(NR_RRC, "Sending Handover Request Acknowledge\n");

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_HANDOVER_REQUEST_ACKNOWLEDGE);
  ngap_handover_request_ack_t *msg = &NGAP_HANDOVER_REQUEST_ACKNOWLEDGE(msg_p);

  // RAN UE NGAP ID
  msg->gNB_ue_ngap_id = UE->rrc_ue_id;
  // AMF UE NGAP ID
  msg->amf_ue_ngap_id = UE->amf_ue_ngap_id;
  // PDU Session Resource Admitted List
  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t*, session, &UE->pduSessions) {
    session->status = PDU_SESSION_STATUS_ESTABLISHED;
    // PDU Session ID
    DevAssert(msg->nb_of_pdusessions < NR_MAX_NB_PDU_SESSIONS);
    pdu_session_resource_admitted_t *pdu = &msg->pdusessions[msg->nb_of_pdusessions++];
    pdu->pdu_session_id = session->param.pdusession_id;
    // Handover Request Acknowledge Transfer
    ho_request_ack_transfer_t *transfer = &pdu->ack_transfer;
    transfer->gtp_teid = session->param.n3_outgoing.teid;
    memcpy(transfer->gNB_addr.buffer, session->param.n3_outgoing.addr.buffer, session->param.n3_outgoing.addr.length);
    transfer->gNB_addr.length = session->param.n3_outgoing.addr.length;
    FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, qos, &session->param.qos) {
      DevAssert(transfer->nb_of_qos_flow < MAX_QOS_FLOWS);
      pdusession_associate_qosflow_t *qos_setup = &transfer->qos_setup_list[transfer->nb_of_qos_flow++];
      qos_setup->qfi = qos->qos.qfi;
      qos_setup->qos_flow_mapping_ind = QOSFLOW_MAPPING_INDICATION_DL;
    }
  }
  // Target to Source Transparent Container
  msg->target2source = copy_byte_array(ho_command);

  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
}

/** @brief Prepare NG Handover Notify message and inform NGAP */
void rrc_gNB_send_NGAP_HANDOVER_NOTIFY(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  LOG_I(NR_RRC, "Triggering NGAP Handover Notify\n");
  nr_rrc_cell_container_t *cell = rrc_get_pcell_for_ue(rrc, UE);
  if (cell == NULL) {
    LOG_E(NR_RRC, "Failed to send Handover Notify: no PCell found for UE %d\n", UE->rrc_ue_id);
    return;
  }
  nr_rrc_du_container_t *du = get_du_for_ue(rrc, UE->rrc_ue_id);
  if (du == NULL) {
    LOG_E(NR_RRC, "Failed to send Handover Notify: no DU found for UE %d\n", UE->rrc_ue_id);
    return;
  }
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_HANDOVER_NOTIFY);
  ngap_handover_notify_t *ho_notify = &NGAP_HANDOVER_NOTIFY(msg_p);

  ho_notify->gNB_ue_ngap_id = UE->rrc_ue_id;
  ho_notify->amf_ue_ngap_id = UE->amf_ue_ngap_id;
  ho_notify->user_info.nrCellIdentity = cell->info.cell_id;

  target_ran_node_id_t *target_ng_ran = &ho_notify->user_info.target_ng_ran;
  target_ng_ran->tac = cell->info.tac;
  target_ng_ran->targetgNBId = rrc->node_id;
  target_ng_ran->plmn_identity = cell->info.plmn;

  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
}

/** @brief Prepare NG Handover Cancel (source NG-RAN) and inform NGAP (N2-based HO only)
 *  3GPP TS 38.413 §8.4.5 (Handover Cancellation) - source NG-RAN -> AMF
 *  3GPP TS 38.413 §9.2.3.11 (HANDOVER CANCEL)
 *  @param rrc CU/RRC instance
 *  @param UE UE context
 *  @param cause cancellation cause */
void rrc_gNB_send_NGAP_HANDOVER_CANCEL(int module_id, gNB_RRC_UE_t *UE, ngap_cause_t cause)
{
  DevAssert(UE != NULL);

  LOG_I(NR_RRC, "Triggering NGAP Handover Cancel (gNB_ue_ngap_id=%d, amf_ue_ngap_id=%ld)\n", UE->rrc_ue_id, UE->amf_ue_ngap_id);

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_HANDOVER_CANCEL);
  ngap_handover_cancel_t *ho_cancel = &NGAP_HANDOVER_CANCEL(msg_p);

  /* Mandatory IEs (38.413 §9.2.3.11) */
  ho_cancel->gNB_ue_ngap_id = UE->rrc_ue_id;
  ho_cancel->amf_ue_ngap_id = UE->amf_ue_ngap_id;
  ho_cancel->cause = cause;

  itti_send_msg_to_task(TASK_NGAP, module_id, msg_p);
}

/**
 * @brief Enforce TS 38.331 §5.3.1.1 after the last DRB of a PDU session release.
 * A UE is not allowed to have a configuration with SRB2 but no DRB, and vice versa.
 * @note Per TS 23.502 section 4.3.4.2 step 5, NG-RAN releases PDU-session AN resources
 * via RRCReconfiguration. If that leaves no DRB with SRB2 still up, request UE context
 * release (TS 38.413 section 8.3.2.2). AMF UE Context Release Command triggers RRCRelease
 * on the existing CU-CP path. */
static void rrc_gNB_cleanup_srb2_only_connected(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  DevAssert(seq_arr_size(&UE->drbs) == 0);
  DevAssert(UE->Srb[SRB2].Active);

  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, UE->rrc_ue_id);
  if (!ue_context_p) {
    LOG_W(NR_RRC, "UE %u: no RRC context for connection release after last DRB\n", UE->rrc_ue_id);
    return;
  }

  LOG_I(NR_RRC, "UE %u: last DRB released with SRB2 still active: requesting RRC connection release\n", UE->rrc_ue_id);

  ngap_cause_t cause = {
      .type = NGAP_CAUSE_RADIO_NETWORK,
      .value = NGAP_CAUSE_RADIO_NETWORK_RELEASE_DUE_TO_NGRAN_GENERATED_REASON,
  };
  rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(rrc->module_id, ue_context_p, cause);
}

void rrc_gNB_send_NGAP_PDUSESSION_RELEASE_RESPONSE(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid)
{
  MessageDef   *msg_p;
  msg_p = itti_alloc_new_message (TASK_RRC_GNB, rrc->module_id, NGAP_PDUSESSION_RELEASE_RESPONSE);
  ngap_pdusession_release_resp_t *resp = &NGAP_PDUSESSION_RELEASE_RESPONSE(msg_p);
  resp->gNB_ue_ngap_id = UE->rrc_ue_id;

  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    if (xid == session->xid) {
      const pdusession_t *pdusession = &session->param;
      if (session->status == PDU_SESSION_STATUS_TORELEASE) {
        DevAssert(resp->nb_of_pdusessions_released < NR_MAX_NB_PDU_SESSIONS);
        resp->pdusession_release[resp->nb_of_pdusessions_released++].pdusession_id = pdusession->pdusession_id;
      }
    }
  }

  for (int i = 0; i < resp->nb_of_pdusessions_released; ++i) {
    rm_pduSession(&UE->pduSessions, &UE->drbs, resp->pdusession_release[i].pdusession_id);
  }

  LOG_I(NR_RRC, "NGAP PDUSESSION RELEASE RESPONSE: rrc_ue_id %u release_pdu_sessions %d\n", resp->gNB_ue_ngap_id, resp->nb_of_pdusessions_released);
  itti_send_msg_to_task (TASK_NGAP, rrc->module_id, msg_p);

  /* TS 38.331 §5.3.1.1: cannot keep SRB2 in RRC_CONNECTED after all DRBs are gone. */
  if (seq_arr_size(&UE->drbs) == 0 && UE->Srb[SRB2].Active)
    rrc_gNB_cleanup_srb2_only_connected(rrc, UE);
}

/** @brief Process NG PDU Session Resource Release command (8.2.2 of 3GPP TS 38.413)
 * upon reception the NG-RAN node shall execute the release of the requested PDU sessions.
 * For each PDU session to be released the NG-RAN node shall release the corresponding
 * resources over Uu and over NG, if any. */
int rrc_gNB_process_NGAP_PDUSESSION_RELEASE_COMMAND(ngap_pdusession_release_command_t *cmd, gNB_RRC_INST *rrc)
{
  uint32_t gNB_ue_ngap_id = cmd->gNB_ue_ngap_id;
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, gNB_ue_ngap_id);

  if (!ue_context_p) {
    LOG_E(NR_RRC, "[gNB %d] UE context not found for gNB_ue_ngap_id %u \n", rrc->module_id, gNB_ue_ngap_id);
    return -1;
  }

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  LOG_I(NR_RRC, "NG PDU Session Release command: AMF_UE_NGAP_ID=%lu, rrc_ue_id=%u, nb_pdusessions_torelease=%d \n",
        cmd->amf_ue_ngap_id,
        gNB_ue_ngap_id,
        cmd->nb_pdusessions_torelease);
  e1ap_bearer_mod_req_t req = {0};
  if (cmd->nb_pdusessions_torelease > 0) {
    req.pduSessionRem = calloc_or_fail(cmd->nb_pdusessions_torelease, sizeof(*req.pduSessionRem));
  }
  for (int pdusession = 0; pdusession < cmd->nb_pdusessions_torelease; pdusession++) {
    rrc_pdu_session_param_t *pduSession = find_pduSession(&UE->pduSessions, cmd->pdusession_ids[pdusession]);
    if (!pduSession) {
      LOG_E(NR_RRC, "Failed to release non-existing PDU Session %d\n", cmd->pdusession_ids[pdusession]);
      continue;
    }
    if (pduSession->status == PDU_SESSION_STATUS_TORELEASE) {
      LOG_W(NR_RRC, "PDU Session %d already set to be released\n", pduSession->param.pdusession_id);
      continue;
    }
    // Set PDU session to release, regardless of the status
    LOG_I(NR_RRC, "Set PDU Session %d to release\n", pduSession->param.pdusession_id);
    pdu_session_to_remove_t *release = &req.pduSessionRem[req.numPDUSessionsRem++];
    release->sessionId = pduSession->param.pdusession_id;
    release->cause.type = E1AP_CAUSE_RADIO_NETWORK;
    release->cause.value = E1AP_RADIO_CAUSE_NORMAL_RELEASE;
    pduSession->status = PDU_SESSION_STATUS_TORELEASE;
  }

  if (req.numPDUSessionsRem == 0) {
    LOG_E(NR_RRC, "Received NG PDU Session Release Command but no PDU Sessions to release\n");
    free_e1ap_context_mod_request(&req);
    return -1;
  } else if (!ue_associated_to_cuup(UE)) {
    LOG_E(NR_RRC, "UE %d is not associated to CU-UP\n", UE->rrc_ue_id);
    // TODO handle, e.g., only trigger F1 release
  } else {
    /* If present, store NAS-PDU in the UE context for later inclusion in RRCReconfiguration */
    if (cmd->nas_pdu.len > 0) {
      UE->nas_pdu = copy_byte_array(cmd->nas_pdu);
    }
    LOG_I(NR_RRC, "Send F1 Bearer Context Modification Request with PDU Session release \n");
    req.gNB_cu_cp_ue_id = UE->rrc_ue_id;
    req.gNB_cu_up_ue_id = UE->rrc_ue_id;
    sctp_assoc_t assoc_id = get_existing_cuup_for_ue(UE);
    rrc->cucp_cuup.bearer_context_mod(assoc_id, &req);
  }
  free_e1ap_context_mod_request(&req);
  init_delayed_action(&UE->delayed_action);
  return 0;
}

/** @brief Send NGAP PDU Session Resource Notify (TS 38.413 section 8.2.4) after local release
 * (triggered by TS 23.527 clause 5.3.3.1) */
void rrc_gNB_send_NGAP_PDUSESSION_RESOURCE_NOTIFY(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t xid)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, rrc->module_id, NGAP_PDUSESSION_RESOURCE_NOTIFY);
  ngap_pdusession_resource_notify_t *notify = &NGAP_PDUSESSION_RESOURCE_NOTIFY(msg_p);
  notify->gNB_ue_ngap_id = UE->rrc_ue_id;

  FOR_EACH_SEQ_ARR (rrc_pdu_session_param_t *, session, &UE->pduSessions) {
    if (xid != session->xid || session->status != PDU_SESSION_STATUS_NOTIFYONRELEASE)
      continue;
    DevAssert(notify->nb_pdu_sessions_released < NR_MAX_NB_PDU_SESSIONS);
    ngap_pdusession_notify_item_t *item = &notify->pdu_sessions[notify->nb_pdu_sessions_released++];
    item->pdu_session_id = session->param.pdusession_id;
    item->cause.type = NGAP_CAUSE_TRANSPORT;
    item->cause.value = NGAP_CAUSE_TRANSPORT_RESOURCE_UNAVAILABLE;
  }

  if (notify->nb_pdu_sessions_released == 0) {
    LOG_E(NR_RRC, "UE %u: PDU Session Resource Notify xid %u with no sessions\n", UE->rrc_ue_id, xid);
    itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    return;
  }

  const int n = notify->nb_pdu_sessions_released;
  int released_ids[NR_MAX_NB_PDU_SESSIONS];
  for (int i = 0; i < n; ++i)
    released_ids[i] = notify->pdu_sessions[i].pdu_session_id;

  LOG_I(NR_RRC, "NGAP PDU Session Resource Notify: rrc_ue_id %u nb_released %d\n", notify->gNB_ue_ngap_id, n);
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
  for (int i = 0; i < n; ++i)
    rm_pduSession(&UE->pduSessions, &UE->drbs, released_ids[i]);

  if (seq_arr_size(&UE->drbs) == 0 && UE->Srb[SRB2].Active)
    rrc_gNB_cleanup_srb2_only_connected(rrc, UE);
}

/** @brief Handle NGAP Paging Indication from the AMF.
 *  For each TAI in the message, matches (PLMN + TAC) against the gNB configuration; on match,
 *  builds an F1AP Paging message and distributes it to all DUs that serve cells in that PLMN. */
int rrc_gNB_process_PAGING_IND(gNB_RRC_INST *rrc, const instance_t instance, const ngap_paging_ind_t *msg)
{
  DevAssert(rrc);
  DevAssert(msg);

  // Construct 5G-S-TMSI from paging identity components
  const fiveg_s_tmsi_t *s_tmsi = &msg->ue_paging_identity.s_tmsi;
  const uint64_t fiveg_s_tmsi = nr_construct_5g_s_tmsi(s_tmsi->amf_set_id, s_tmsi->amf_pointer, s_tmsi->m_tmsi);
  const nr_rrc_config_t *req = &rrc->configuration;
  DevAssert(req->num_plmn > 0);

  LOG_D(NR_RRC, "[gNB %ld] Processing NGAP Paging Indication for %d TAIs\n", instance, msg->n_tai);

  // Paging Attempt Information (TS 38.413 §9.3.1.72, TS 38.300 §9.2.5)
  // AMF provides attempt count and intended attempts for paging optimization (area expansion, retransmission logic).
  if (msg->paging_attempt_info != NULL) {
    LOG_D(NR_RRC,
          "[gNB %ld] Paging Attempt Count: %u, Intended Number of Attempts: %u\n",
          instance,
          msg->paging_attempt_info->paging_attempt_count,
          msg->paging_attempt_info->intended_paging_attempts);
  }

  if (msg->n_tai > NGAP_MAX_NO_TAI_PAGING) {
    LOG_E(NR_RRC,
          "[gNB %ld] Paging message error: n_tai (%d) exceeds NGAP_MAX_NO_TAI_PAGING (%d)\n",
          instance,
          msg->n_tai,
          NGAP_MAX_NO_TAI_PAGING);
    return -1;
  }

  // Build F1AP paging message once for all TAIs
  f1ap_paging_t f1ap_msg = {0};
  f1ap_msg.ue_identity_index_value = s_tmsi->m_tmsi % 1024;
  f1ap_msg.identity_type = F1AP_PAGING_IDENTITY_CN_UE;
  f1ap_msg.identity.cn_ue_paging_identity = fiveg_s_tmsi;
  if (msg->paging_drx != NULL) {
    /* Mapping 1:1 between NGAP and F1AP Paging DRX */
    f1ap_msg.drx = malloc_or_fail(sizeof(*f1ap_msg.drx));
    *f1ap_msg.drx = (f1ap_paging_drx_t)*msg->paging_drx;
  }
  if (msg->paging_priority != NULL) {
    /* Mapping 1:1 between NGAP and F1AP Paging Priority */
    f1ap_msg.priority = malloc_or_fail(sizeof(*f1ap_msg.priority));
    *f1ap_msg.priority = (f1ap_paging_priority_t)*msg->paging_priority;
  }
  if (msg->origin != NULL) {
    /* Mapping 1:1 between NGAP and F1AP Paging Origin */
    f1ap_msg.origin = malloc_or_fail(sizeof(*f1ap_msg.origin));
    *f1ap_msg.origin = (f1ap_paging_origin_t)*msg->origin;
  }

  // For each DU, collect cells matching any AMF TAI (PLMN+TAC) and send
  // one Paging with that cell list.
  rrc_send_paging_to_dus(rrc, msg->tai_list, msg->n_tai, &f1ap_msg);

  free_f1ap_paging(&f1ap_msg);

  return 0;
}

/** @brief Callback for NGAP Handover Required message (3GPP TS 38.413 9.2.3.1)
 * Direction: source gNB -> AMF */
void rrc_gNB_send_NGAP_HANDOVER_REQUIRED(gNB_RRC_INST *rrc,
                                         gNB_RRC_UE_t *UE,
                                         const nr_neighbour_cell_t *neighbour,
                                         const byte_array_t hoPrepInfo)
{
  LOG_I(NR_RRC, "Handover Preparation: send Handover Required (target gNB ID=%d, PCI=%d)\n", neighbour->gNB_ID, neighbour->physicalCellId);

  const plmn_id_t plmn = neighbour->plmn;
  const target_ran_node_id_t target = {
      .plmn_identity = plmn,
      .tac = neighbour->tac,
      .targetgNBId = neighbour->gNB_ID,
  };

  ngap_handover_required_t msg = {
      .amf_ue_ngap_id = UE->amf_ue_ngap_id,
      .gNB_ue_ngap_id = UE->rrc_ue_id,
      .cause.type = NGAP_CAUSE_RADIO_NETWORK,
      .cause.value = NGAP_CAUSE_RADIO_NETWORK_HANDOVER_DESIRABLE_FOR_RADIO_REASON,
      .handoverType = HANDOVER_TYPE_INTRA5GS,
      .target_gnb_id = target,
  };

  cell_id_t target_cell = {
      .nrCellIdentity = neighbour->nrcell_id,
      .plmn_identity = plmn,
  };

  // Source to Target Transparent Container (M)
  msg.source2target = calloc_or_fail(1, sizeof(*msg.source2target));
  // Target Cell ID (M)
  msg.source2target->targetCellId = target_cell;
  // RRC Container (M)
  msg.source2target->handoverInfo = copy_byte_array(hoPrepInfo);
  // UE History Info (M)
  msg.source2target->ue_history_info.cause = malloc_or_fail(sizeof(*msg.source2target->ue_history_info.cause));
  msg.source2target->ue_history_info.cause->type = NGAP_CAUSE_RADIO_NETWORK;
  msg.source2target->ue_history_info.cause->value = NGAP_CAUSE_RADIO_NETWORK_HANDOVER_DESIRABLE_FOR_RADIO_REASON;
  msg.source2target->ue_history_info.type = NGAP_CellSize_small;
  msg.source2target->ue_history_info.time_in_cell = min(time(NULL) - UE->last_seen, 4095);
  msg.source2target->ue_history_info.id = target_cell;

  /* Fill both PDU Session Resource List IE (M) in Handover Required
     and PDU Session Resource Information List IE (O) in the Source NG-RAN
     Node to Target NG-RAN Node Transparent Container */
  FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t*, pduSession, &UE->pduSessions) {
    if (pduSession->status != PDU_SESSION_STATUS_ESTABLISHED)
      continue;
    pdusession_t *session = &pduSession->param;
    DevAssert(msg.nb_of_pdusessions < NR_MAX_NB_PDU_SESSIONS);
    pdusession_resource_t *pdu = &msg.pdusessions[msg.nb_of_pdusessions++];
    // PDU Session ID
    pdu->pdusession_id = session->pdusession_id;
    // PDU Session Resource Information List (O)
    DevAssert(msg.source2target->nb_pdu_session_resource < NR_MAX_NB_PDU_SESSIONS);
    pdusession_resource_info_t *pdu_info = &msg.source2target->pdu_session_resource[msg.source2target->nb_pdu_session_resource++];
    pdu_info->pdusession_id = session->pdusession_id;
    FOR_EACH_SEQ_ARR(nr_rrc_qos_t *, qos, &session->qos) {
      DevAssert(pdu_info->nb_of_qos_flow < MAX_QOS_FLOWS);
      qosflow_info_t *qos_flow = &pdu_info->qos_flow_info[pdu_info->nb_of_qos_flow++];
      qos_flow->qfi = qos->qos.qfi;
    }
  }

  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_HANDOVER_REQUIRED);
  NGAP_HANDOVER_REQUIRED(msg_p) = msg;
  itti_send_msg_to_task(TASK_NGAP, rrc->module_id, msg_p);
}

int rrc_gNB_process_NGAP_DL_RAN_STATUS_TRANSFER(MessageDef *msg_p, instance_t instance)
{
  const ngap_ran_status_transfer_t *cmd = &NGAP_DL_RAN_STATUS_TRANSFER(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, cmd->gnb_ue_ngap_id);

  if (!ue_context_p) {
    LOG_E(NR_RRC, "[gNB %ld] No UE context for gNB_ue_ngap_id %u\n", instance, cmd->gnb_ue_ngap_id);
    return -1;
  }

  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  LOG_I(NR_RRC,
        "[gNB %ld] DL RAN Status Transfer for gNB_ue_ngap_id %u AMF_UE_NGAP_ID %lu\n",
        instance,
        cmd->gnb_ue_ngap_id,
        cmd->amf_ue_ngap_id);

  for (int i = 0; i < cmd->ran_status.nb_drb; ++i) {
    const ngap_drb_status_t *s = &cmd->ran_status.drb_status_list[i];
    LOG_I(NR_RRC,
          "DL RAN Status Transfer - DRB ID %d:\n"
          "  UL COUNT: PDCP SN = %u, HFN = %u (%s)\n"
          "  DL COUNT: PDCP SN = %u, HFN = %u (%s)\n",
          s->drb_id,
          s->ul_count.pdcp_sn,
          s->ul_count.hfn,
          s->ul_count.sn_len == NGAP_SN_LENGTH_18 ? "18-bit" : "12-bit",
          s->dl_count.pdcp_sn,
          s->dl_count.hfn,
          s->dl_count.sn_len == NGAP_SN_LENGTH_18 ? "18-bit" : "12-bit");

    // Send to PDCP layer
    e1_notify_pdcp_status(rrc, UE, s);
  }

  return 0;
}
