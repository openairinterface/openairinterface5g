/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap messages handlers for gNB part
 */
 
#include "ngap_gNB_handlers.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "INTEGER.h"
#include "ngap_msg_includes.h"
#include "OCTET_STRING.h"
#include "T.h"
#include "assertions.h"
#include "common/utils/T/T.h"
#include "common/utils/utils.h"
#include "constr_TYPE.h"
#include "conversions.h"
#include "intertask_interface.h"
#include "ngap_common.h"
#include "ngap_gNB_decoder.h"
#include "ngap_gNB_defs.h"
#include "ngap_gNB_encoder.h"
#include "ngap_gNB_nnsf.h"
#include "ngap_gNB_management_procedures.h"
#include "ngap_gNB_mobility_management.h"
#include "ngap_gNB_pdu_session_management.h"
#include "ngap_gNB_nas_procedures.h"
#include "ngap_gNB_paging.h"
#include "ngap_gNB_NRPPa_transport_procedures.h"
#include "ngap_gNB_ue_context.h"
#include "ngap_messages_types.h"
#include "oai_asn1.h"
#include "queue.h"

#ifdef E2_AGENT
#include "openair2/E2AP/RAN_FUNCTION/setup_msg_store.h"
#endif

char *ngap_direction2String(int ngap_dir) {
  static char *ngap_direction_String[] = {
    "", /* Nothing */
    "Originating message", /* originating message */
    "Successfull outcome", /* successfull outcome */
    "UnSuccessfull outcome", /* successfull outcome */
  };
  return(ngap_direction_String[ngap_dir]);
}

static void ngap_send_register_gnb_cnf(ngap_gNB_instance_t *inst)
{
  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_REGISTER_GNB_CNF);
  ngap_register_gnb_cnf_t *cnf = &NGAP_REGISTER_GNB_CNF(message_p);

  cnf->nb_amf = inst->ngap_amf_associated_nb;
  cnf->gNB_id = inst->gNB_id;
  cnf->tac = inst->tac;
  cnf->num_plmn = inst->num_plmn;
  memcpy(cnf->plmn, inst->plmn, inst->num_plmn * sizeof(*inst->plmn));

  int r = 0;
  ngap_gNB_amf_data_t *amf_node;
  struct served_guami_s *guami;
  RB_FOREACH(amf_node, ngap_amf_map, &inst->ngap_amf_head) {
    STAILQ_FOREACH(guami, &amf_node->served_guami, next) {
      if (r >= NGAP_MAX_NB_AMF_REGIONS)
        break;
      ngap_amf_region_info_t *amf_region = &cnf->amf_region_info[r];
      amf_region->plmn = guami->guami.plmn;
      amf_region->amf_region_id = guami->guami.amf_region_id;
      r++;
    }
  }
  cnf->num_amf_regions = r;

  itti_send_msg_to_task(TASK_GNB_APP, inst->instance, message_p);
}

void ngap_handle_ng_setup_message(ngap_gNB_amf_data_t *amf_desc_p, int sctp_shutdown) {
  if (sctp_shutdown) {
    /* A previously connected AMF has been shutdown */

    /* TODO check if it was used by some gNB and send a message to inform these gNB if there is no more associated AMF */
    if (amf_desc_p->state == NGAP_GNB_STATE_CONNECTED) {
      amf_desc_p->state = NGAP_GNB_STATE_DISCONNECTED;

      if (amf_desc_p->ngap_gNB_instance->ngap_amf_associated_nb > 0) {
        /* Decrease associated AMF number */
        amf_desc_p->ngap_gNB_instance->ngap_amf_associated_nb --;
      }
      /* Release UE context and start reconnection process*/
      ngap_release_ues_for_amf(amf_desc_p);

      /* If there are no more associated AMF, inform gNB app */
      if (amf_desc_p->ngap_gNB_instance->ngap_amf_associated_nb == 0) {
        MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_DEREGISTERED_GNB_IND);
        NGAP_DEREGISTERED_GNB_IND(message_p).nb_amf = 0;
        itti_send_msg_to_task(TASK_GNB_APP, amf_desc_p->ngap_gNB_instance->instance, message_p);
      }
    }
  } else {
    if (amf_desc_p->t_reconnect != -1 && amf_desc_p->ngap_gNB_instance->ngap_amf_associated_nb > 0) {
      timer_remove(amf_desc_p->t_reconnect);
      amf_desc_p->t_reconnect = -1;
    }

    /* Check that at least one setup message is pending */
    DevCheck(amf_desc_p->ngap_gNB_instance->ngap_amf_pending_nb > 0,
             amf_desc_p->ngap_gNB_instance->instance,
             amf_desc_p->ngap_gNB_instance->ngap_amf_pending_nb,
             0);

    if (amf_desc_p->ngap_gNB_instance->ngap_amf_pending_nb > 0) {
      /* Decrease pending messages number */
      amf_desc_p->ngap_gNB_instance->ngap_amf_pending_nb --;
    }

    /* If there are no more pending messages, inform gNB app */
    if (amf_desc_p->ngap_gNB_instance->ngap_amf_pending_nb == 0) {
      ngap_send_register_gnb_cnf(amf_desc_p->ngap_gNB_instance);
    }
  }
}

static int ngap_gNB_handle_ng_setup_failure(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_NGSetupFailure_t      *container;
  NGAP_NGSetupFailureIEs_t   *ie;
  ngap_gNB_amf_data_t        *amf_desc_p;
  DevAssert(pdu != NULL);
  container = &pdu->choice.unsuccessfulOutcome->value.choice.NGSetupFailure;

  /* S1 Setup Failure == Non UE-related procedure -> stream 0 */
  if (stream != 0) {
    NGAP_WARN("[SCTP %u] Received s1 setup failure on stream != 0 (%u)\n",
              assoc_id, stream);
  }

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received S1 setup response for non existing "
               "AMF context\n", assoc_id);
    return -1;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_NGSetupFailureIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_Cause,true);

  if ((ie->value.choice.Cause.present == NGAP_Cause_PR_misc) &&
      (ie->value.choice.Cause.choice.misc == NGAP_CauseMisc_unspecified)) {
    NGAP_WARN("Received NG setup failure for AMF... AMF is not ready\n");
  } else {
    NGAP_ERROR("Received NG setup failure for AMF... please check your parameters\n");
  }

  amf_desc_p->state = NGAP_GNB_STATE_WAITING;
  ngap_handle_ng_setup_message(amf_desc_p, 0);
  return 0;
}

static void ngap_dump_served_guami(const ngap_gNB_amf_data_t *amf_desc_p)
{
  const struct served_guami_s *guami_p;

  NGAP_DEBUG("Served GUAMIs for AMF %s (assoc_id=%d):\n",
             amf_desc_p->amf_name ? amf_desc_p->amf_name : "(no name)",
             amf_desc_p->assoc_id);

  STAILQ_FOREACH(guami_p, &amf_desc_p->served_guami, next)
  {
    NGAP_DEBUG("  GUAMI: PLMN MCC=%03d MNC=%0*d region=%u set=%u pointer=%u\n",
               guami_p->guami.plmn.mcc,
               guami_p->guami.plmn.mnc_digit_length,
               guami_p->guami.plmn.mnc,
               guami_p->guami.amf_region_id,
               guami_p->guami.amf_set_id,
               guami_p->guami.amf_pointer);
  }
}

static int ngap_gNB_handle_ng_setup_response(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_NGSetupResponse_t    *container;
  NGAP_NGSetupResponseIEs_t *ie;
  ngap_gNB_amf_data_t       *amf_desc_p;
  int i;
  DevAssert(pdu != NULL);
#ifdef E2_AGENT
  uint8_t *buffer = NULL;
  uint32_t len = 0;
  if (ngap_gNB_encode_pdu(pdu, &buffer, &len) == 0)
    e2ap_store_setup_resp(E2AP_SETUP_MSG_NGAP, buffer, len);
  free(buffer);
#endif
  container = &pdu->choice.successfulOutcome->value.choice.NGSetupResponse;

  /* NG Setup Response == Non UE-related procedure -> stream 0 */
  if (stream != 0) {
    NGAP_ERROR("[SCTP %u] Received ng setup response on stream != 0 (%u)\n",
               assoc_id, stream);
    return -1;
  }

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received NG setup response for non existing "
               "AMF context\n", assoc_id);
    return -1;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_NGSetupResponseIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_ServedGUAMIList, true);

  /* The list of served guami can contain at most 256 elements.
   * NR related guami is the first element in the list, i.e with an id of 0.
   */
  DevAssert(ie->value.choice.ServedGUAMIList.list.count > 0);
  DevAssert(ie->value.choice.ServedGUAMIList.list.count <= NGAP_maxnoofServedGUAMIs);

  for (i = 0; i < ie->value.choice.ServedGUAMIList.list.count; i++) {
    NGAP_ServedGUAMIItem_t  *guami_item_p;
    struct served_guami_s   *new_guami_p;

    guami_item_p = ie->value.choice.ServedGUAMIList.list.array[i];
    new_guami_p = calloc_or_fail(1, sizeof(*new_guami_p));
    new_guami_p->guami = decode_ngap_guami(&guami_item_p->gUAMI);

    STAILQ_INSERT_TAIL(&amf_desc_p->served_guami, new_guami_p, next);
  }

  /* Set the capacity of this AMF */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_NGSetupResponseIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RelativeAMFCapacity, true);

  amf_desc_p->relative_amf_capacity = ie->value.choice.RelativeAMFCapacity;

  /* mandatory set the amf name */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_NGSetupResponseIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMFName, true);
  amf_desc_p->amf_name = strndup((const char *)ie->value.choice.AMFName.buf, ie->value.choice.AMFName.size);
  if (amf_desc_p->amf_name == NULL) {
    NGAP_ERROR("Failed to allocate AMF name\n");
    return -1;
  }

  ngap_dump_served_guami(amf_desc_p);

  /* PLMN Support List (Mandatory) */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_NGSetupResponseIEs_t, ie, container,
                               NGAP_ProtocolIE_ID_id_PLMNSupportList, true);

  DevAssert(ie->value.choice.PLMNSupportList.list.count > 0);
  DevAssert(ie->value.choice.PLMNSupportList.list.count <= NGAP_maxnoofPLMNs);

  for (i = 0; i < ie->value.choice.PLMNSupportList.list.count; i++) {
    NGAP_PLMNSupportItem_t *plmn_support_item_p = ie->value.choice.PLMNSupportList.list.array[i];
    plmn_id_t plmn = {0};
    TBCD_TO_MCC_MNC(&plmn_support_item_p->pLMNIdentity, plmn.mcc, plmn.mnc, plmn.mnc_digit_length);
    NGAP_INFO("Supported PLMN %d: MCC=%03d MNC=%0*d\n", i, plmn.mcc, plmn.mnc_digit_length, plmn.mnc);
    DevAssert(plmn_support_item_p->sliceSupportList.list.count > 0);
    DevAssert(plmn_support_item_p->sliceSupportList.list.count <= NGAP_maxnoofSliceItems);

    for (int j = 0; j < plmn_support_item_p->sliceSupportList.list.count; j++) {
      NGAP_SliceSupportItem_t *slice_support_item_p = plmn_support_item_p->sliceSupportList.list.array[j];
      uint8_t sst;
      uint8_t sd[3] = {0};
      OCTET_STRING_TO_INT8(&slice_support_item_p->s_NSSAI.sST, sst);

      if (slice_support_item_p->s_NSSAI.sD != NULL) {
        memcpy(sd, slice_support_item_p->s_NSSAI.sD->buf, sizeof(sd));
      }
      NGAP_INFO("Supported slice (PLMN %d): SST=0x%02x SD=0x%02x%02x%02x\n", i, sst, sd[0], sd[1], sd[2]);
    }
  }

  /* The association is now ready as gNB and AMF know parameters of each other.
   * Mark the association as UP to enable UE contexts creation.
   */
  amf_desc_p->state = NGAP_GNB_STATE_CONNECTED;
  amf_desc_p->ngap_gNB_instance->ngap_amf_associated_nb ++;
  ngap_handle_ng_setup_message(amf_desc_p, 0);
  LOG_A(NGAP, "Received NGSetupResponse from AMF\n");

  return 0;
}

static int ngap_gNB_handle_error_indication(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_ErrorIndication_t    *container;
  NGAP_ErrorIndicationIEs_t *ie;
  ngap_gNB_amf_data_t        *amf_desc_p;
  uint64_t                 amf_ue_ngap_id;
    
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.ErrorIndication;

  /* NG Setup Failure == Non UE-related procedure -> stream 0 */
  if (stream != 0) {
    NGAP_WARN("[SCTP %u] Received ng Error indication on stream != 0 (%u)\n",
              assoc_id, stream);
  }

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received ng Error indication for non existing "
               "AMF context\n", assoc_id);
    return -1;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_ErrorIndicationIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, false);

  /* optional */
  if (ie != NULL) {
    asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);
    NGAP_WARN("Received NG Error indication AMF UE NGAP ID 0x%lx\n", amf_ue_ngap_id);
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_ErrorIndicationIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, false);

  /* optional */
  if (ie != NULL) {
    NGAP_WARN("Received NG Error indication gNB UE NGAP ID 0x%lx\n", ie->value.choice.RAN_UE_NGAP_ID);
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_ErrorIndicationIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_Cause, false);

  /* optional */
  if (ie) {
    switch(ie->value.choice.Cause.present) {
      case NGAP_Cause_PR_NOTHING:
        NGAP_WARN("Received NG Error indication cause NOTHING\n");
        break;

      case NGAP_Cause_PR_radioNetwork:
        switch (ie->value.choice.Cause.choice.radioNetwork) {
          case NGAP_CauseRadioNetwork_unspecified:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_unspecified\n");
            break;

          case NGAP_CauseRadioNetwork_txnrelocoverall_expiry:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_txnrelocoverall_expiry\n");
            break;

          case NGAP_CauseRadioNetwork_successful_handover:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_successful_handover\n");
            break;

          case NGAP_CauseRadioNetwork_release_due_to_ngran_generated_reason:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_release_due_to_ngran_generated_reason\n");
            break;
          
          case NGAP_CauseRadioNetwork_release_due_to_5gc_generated_reason:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_release_due_to_5gc_generated_reason\n");
            break;

          case NGAP_CauseRadioNetwork_handover_cancelled:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_handover_cancelled\n");
            break;

          case NGAP_CauseRadioNetwork_partial_handover:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_partial_handover\n");
            break;

          case NGAP_CauseRadioNetwork_ho_failure_in_target_5GC_ngran_node_or_target_system:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ho_failure_in_target_5GC_ngran_node_or_target_system\n");
            break;

          case NGAP_CauseRadioNetwork_ho_target_not_allowed:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ho_target_not_allowed\n");
            break;

          case NGAP_CauseRadioNetwork_tngrelocoverall_expiry:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_tngrelocoverall_expiry\n");
            break;

          case NGAP_CauseRadioNetwork_tngrelocprep_expiry:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_tngrelocprep_expiry\n");
            break;

          case NGAP_CauseRadioNetwork_cell_not_available:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_cell_not_available\n");
            break;

          case NGAP_CauseRadioNetwork_unknown_targetID:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_unknown_targetID\n");
            break;

          case NGAP_CauseRadioNetwork_no_radio_resources_available_in_target_cell:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_no_radio_resources_available_in_target_cell\n");
            break;

          case NGAP_CauseRadioNetwork_unknown_local_UE_NGAP_ID:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_unknown_local_UE_NGAP_ID\n");
            break;

          case NGAP_CauseRadioNetwork_inconsistent_remote_UE_NGAP_ID:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_inconsistent_remote_UE_NGAP_ID\n");
            break;

          case NGAP_CauseRadioNetwork_handover_desirable_for_radio_reason:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_handover_desirable_for_radio_reason\n");
            break;

          case NGAP_CauseRadioNetwork_time_critical_handover:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_time_critical_handover\n");
            break;

          case NGAP_CauseRadioNetwork_resource_optimisation_handover:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_resource_optimisation_handover\n");
            break;

          case NGAP_CauseRadioNetwork_reduce_load_in_serving_cell:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_reduce_load_in_serving_cell\n");
            break;

          case NGAP_CauseRadioNetwork_user_inactivity:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_user_inactivity\n");
            break;

          case NGAP_CauseRadioNetwork_radio_connection_with_ue_lost:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_radio_connection_with_ue_lost\n");
            break;

          case NGAP_CauseRadioNetwork_radio_resources_not_available:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_radio_resources_not_available\n");
            break;

          case NGAP_CauseRadioNetwork_invalid_qos_combination:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_invalid_qos_combination\n");
            break;

          case NGAP_CauseRadioNetwork_failure_in_radio_interface_procedure:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_failure_in_radio_interface_procedure\n");
            break;

          case NGAP_CauseRadioNetwork_interaction_with_other_procedure:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_interaction_with_other_procedure\n");
            break;

          case NGAP_CauseRadioNetwork_unknown_PDU_session_ID:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_unknown_PDU_session_ID\n");
            break;

          case NGAP_CauseRadioNetwork_unkown_qos_flow_ID:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_unkown_qos_flow_ID\n");
            break;

          case NGAP_CauseRadioNetwork_multiple_PDU_session_ID_instances:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_multiple_PDU_session_ID_instances\n");
            break;

          case NGAP_CauseRadioNetwork_multiple_qos_flow_ID_instances:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_multiple_qos_flow_ID_instances\n");
            break;

          case NGAP_CauseRadioNetwork_encryption_and_or_integrity_protection_algorithms_not_supported:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_encryption_and_or_integrity_protection_algorithms_not_supported\n");
            break;

          case NGAP_CauseRadioNetwork_ng_intra_system_handover_triggered:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ng_intra_system_handover_triggered\n");
            break;

          case NGAP_CauseRadioNetwork_ng_inter_system_handover_triggered:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ng_inter_system_handover_triggered\n");
            break;

          case NGAP_CauseRadioNetwork_xn_handover_triggered:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_xn_handover_triggered\n");
            break;

          case NGAP_CauseRadioNetwork_not_supported_5QI_value:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_not_supported_5QI_value\n");
            break;

          case NGAP_CauseRadioNetwork_ue_context_transfer:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ue_context_transfer\n");
            break;

          case NGAP_CauseRadioNetwork_ims_voice_eps_fallback_or_rat_fallback_triggered:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ims_voice_eps_fallback_or_rat_fallback_triggered\n");
            break;

          case NGAP_CauseRadioNetwork_up_integrity_protection_not_possible:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_up_integrity_protection_not_possible\n");
            break;

          case NGAP_CauseRadioNetwork_up_confidentiality_protection_not_possible:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_up_confidentiality_protection_not_possible\n");
            break;

          case NGAP_CauseRadioNetwork_slice_not_supported:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_slice_not_supported\n");
            break;

          case NGAP_CauseRadioNetwork_ue_in_rrc_inactive_state_not_reachable:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ue_in_rrc_inactive_state_not_reachable\n");
            break;

          case NGAP_CauseRadioNetwork_redirection:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_redirection\n");
            break;

          case NGAP_CauseRadioNetwork_resources_not_available_for_the_slice:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_resources_not_available_for_the_slice\n");
            break;

          case NGAP_CauseRadioNetwork_ue_max_integrity_protected_data_rate_reason:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_ue_max_integrity_protected_data_rate_reason\n");
            break;

          case NGAP_CauseRadioNetwork_release_due_to_cn_detected_mobility:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_release_due_to_cn_detected_mobility\n");
            break;

          case NGAP_CauseRadioNetwork_n26_interface_not_available:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_n26_interface_not_available\n");
            break;

          case NGAP_CauseRadioNetwork_release_due_to_pre_emption:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_release_due_to_pre_emption\n");
            break;

          case NGAP_CauseRadioNetwork_multiple_location_reporting_reference_ID_instances:
            NGAP_WARN("Received NG Error indication NGAP_CauseRadioNetwork_multiple_location_reporting_reference_ID_instances\n");
            break;

          default:
            NGAP_WARN("Received NG Error indication cause radio network case not handled\n");
        }

        break;

      case NGAP_Cause_PR_transport:
        switch (ie->value.choice.Cause.choice.transport) {
          case NGAP_CauseTransport_transport_resource_unavailable:
            NGAP_WARN("Received NG Error indication NGAP_CauseTransport_transport_resource_unavailable\n");
            break;

          case NGAP_CauseTransport_unspecified:
            NGAP_WARN("Received NG Error indication NGAP_CauseTransport_unspecified\n");
            break;

          default:
            NGAP_WARN("Received NG Error indication cause transport case not handled\n");
        }

        break;

      case NGAP_Cause_PR_nas:
        switch (ie->value.choice.Cause.choice.nas) {
          case NGAP_CauseNas_normal_release:
            NGAP_WARN("Received NG Error indication NGAP_CauseNas_normal_release\n");
            break;

          case NGAP_CauseNas_authentication_failure:
            NGAP_WARN("Received NG Error indication NGAP_CauseNas_authentication_failure\n");
            break;

          case NGAP_CauseNas_deregister:
            NGAP_WARN("Received NG Error indication NGAP_CauseNas_deregister\n");
            break;

          case NGAP_CauseNas_unspecified:
            NGAP_WARN("Received NG Error indication NGAP_CauseNas_unspecified\n");
            break;

          default:
            NGAP_WARN("Received NG Error indication cause nas case not handled\n");
        }

        break;

      case NGAP_Cause_PR_protocol:
        switch (ie->value.choice.Cause.choice.protocol) {
          case NGAP_CauseProtocol_transfer_syntax_error:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_transfer_syntax_error\n");
            break;

          case NGAP_CauseProtocol_abstract_syntax_error_reject:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_abstract_syntax_error_reject\n");
            break;

          case NGAP_CauseProtocol_abstract_syntax_error_ignore_and_notify:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_abstract_syntax_error_ignore_and_notify\n");
            break;

          case NGAP_CauseProtocol_message_not_compatible_with_receiver_state:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_message_not_compatible_with_receiver_state\n");
            break;

          case NGAP_CauseProtocol_semantic_error:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_semantic_error\n");
            break;

          case NGAP_CauseProtocol_abstract_syntax_error_falsely_constructed_message:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_abstract_syntax_error_falsely_constructed_message\n");
            break;

          case NGAP_CauseProtocol_unspecified:
            NGAP_WARN("Received NG Error indication NGAP_CauseProtocol_unspecified\n");
            break;

          default:
            NGAP_WARN("Received NG Error indication cause protocol case not handled\n");
        }

        break;

      case NGAP_Cause_PR_misc:
        switch (ie->value.choice.Cause.choice.protocol) {
          case NGAP_CauseMisc_control_processing_overload:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_control_processing_overload\n");
            break;

          case NGAP_CauseMisc_not_enough_user_plane_processing_resources:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_not_enough_user_plane_processing_resources\n");
            break;

          case NGAP_CauseMisc_hardware_failure:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_hardware_failure\n");
            break;

          case NGAP_CauseMisc_om_intervention:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_om_intervention\n");
            break;

          case NGAP_CauseMisc_unspecified:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_unspecified\n");
            break;

          case NGAP_CauseMisc_unknown_PLMN:
            NGAP_WARN("Received NG Error indication NGAP_CauseMisc_unknown_PLMN\n");
            break;

          default:
            NGAP_WARN("Received NG Error indication cause misc case not handled\n");
        }

        break;
        
      default:
       NGAP_WARN("Received NG Error indication cause NGAP_Cause_PR_choice_Extensions\n");
       break;
      
    }
  }


  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_ErrorIndicationIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_CriticalityDiagnostics, false);

  if (ie) {
    // TODO continue
  }


  // TODO continue
  return 0;
}

/** @brief Handler for NGAP Handover Request message (AMF -> target NG-RAN)
 *        Decode the NGAP message and transfer to RRC */
static int ngap_gNB_handle_handover_request(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  UNUSED(stream);
  DevAssert(pdu != NULL);

  ngap_gNB_amf_data_t *amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0);
  if (amf_desc_p == NULL) {
    NGAP_ERROR("Received Handover Request for non existing AMF context\n");
    return -1;
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_HANDOVER_REQUEST);
  ngap_handover_request_t *msg = &NGAP_HANDOVER_REQUEST(message_p);

  if (decode_ng_handover_request(msg, pdu) < 0) {
    NGAP_ERROR("Failed to decode NG Handover Request\n");
    ngap_handover_failure_t fail = {
        .amf_ue_ngap_id = msg->amf_ue_ngap_id,
        .cause.type = NGAP_CAUSE_RADIO_NETWORK,
        .cause.value = NGAP_CAUSE_RADIO_NETWORK_HO_FAILURE_IN_TARGET_5GC_NGRAN_NODE_OR_TARGET_SYSTEM,
    };
    NGAP_INFO("Send NG Handover Failure message (amf_ue_ngap_id %ld) with cause %d \n ", fail.amf_ue_ngap_id, fail.cause.value);
    MessageDef *msg_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_HANDOVER_FAILURE);
    NGAP_HANDOVER_FAILURE(msg_p) = fail;
    itti_send_msg_to_task(TASK_NGAP, amf_desc_p->ngap_gNB_instance->instance, msg_p);
    return -1;
  }

  NGAP_INFO("Received NG Handover Request from AMF %s (ID=%lu)\n", amf_desc_p->amf_name, msg->amf_ue_ngap_id);
  itti_send_msg_to_task(TASK_RRC_GNB, amf_desc_p->ngap_gNB_instance->instance, message_p);

  return 0;
}

static int ngap_gNB_handle_initial_context_request(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  int i;
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  NGAP_InitialContextSetupRequest_t    *container;
  NGAP_InitialContextSetupRequestIEs_t *ie;
  uint64_t                 amf_ue_ngap_id;
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.InitialContextSetupRequest;

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received initial context setup request for non "
               "existing AMF context\n", assoc_id);
    return -1;
  }

  /* id-AMF-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);

  asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);

  /* id-RAN-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);

  const NGAP_RAN_UE_NGAP_ID_t ran_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;
  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(ran_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR(
        "[SCTP %u] Received initial context setup request for non "
        "existing UE context 0x%06lx\n",
        assoc_id,
        ran_ue_ngap_id);
    return -1;
  }

  /* Initial context request = UE-related procedure -> stream != 0 */
  //if (stream == 0) {
  //  NGAP_ERROR("[SCTP %d] Received UE-related procedure on stream (%d)\n",
  //             assoc_id, stream);
  //  return -1;
  //}

  ue_desc_p->rx_stream = stream;
  ue_desc_p->amf_ue_ngap_id = amf_ue_ngap_id;
  
  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_INITIAL_CONTEXT_SETUP_REQ);
  ngap_initial_context_setup_req_t * msg=&NGAP_INITIAL_CONTEXT_SETUP_REQ(message_p);
  msg->gNB_ue_ngap_id = ue_desc_p->gNB_ue_ngap_id;
  msg->amf_ue_ngap_id = ue_desc_p->amf_ue_ngap_id;
  /* id-UEAggregateMaximumBitRate */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate, false);
  if (ie != NULL) {
    msg->ue_ambr = decode_ngap_UEAggregateMaximumBitRate(&ie->value.choice.UEAggregateMaximumBitRate);
    msg->has_ue_ambr = true;
  }

  /* id-GUAMI */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_GUAMI, true);
  msg->guami = decode_ngap_guami(&ie->value.choice.GUAMI);

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListCxtReq, false);
  if (ie != NULL) {
    msg->nb_of_pdusessions = ie->value.choice.PDUSessionResourceSetupListCxtReq.list.count;

    for (i = 0; i < ie->value.choice.PDUSessionResourceSetupListCxtReq.list.count; i++) {
      NGAP_PDUSessionResourceSetupItemCxtReq_t *item_p = ie->value.choice.PDUSessionResourceSetupListCxtReq.list.array[i];
      msg->pdusession[i].pdusession_id = item_p->pDUSessionID;
      msg->pdusession[i].nssai = decode_ngap_nssai(&item_p->s_NSSAI);
      if (item_p->nAS_PDU) {
        msg->pdusession[i].nas_pdu = create_byte_array(item_p->nAS_PDU->size, item_p->nAS_PDU->buf);
      }
      bool ret = decodePDUSessionResourceSetup(&msg->pdusession[i].pdusessionTransfer, item_p->pDUSessionResourceSetupRequestTransfer);
      if (!ret) {
        NGAP_ERROR("Failed to decode pDUSessionResourceSetupRequestTransfer in NG Initial Context Setup Request\n");
        return -1;
      }
    }
  }

  /* id-AllowedNSSAI */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_AllowedNSSAI, true);
  NGAP_DEBUG("AllowedNSSAI.list.count %d\n", ie->value.choice.AllowedNSSAI.list.count);
  msg->nb_allowed_nssais = ie->value.choice.AllowedNSSAI.list.count;
  for (i = 0; i < ie->value.choice.AllowedNSSAI.list.count; i++) {
    msg->allowed_nssai[i] = decode_ngap_nssai(&ie->value.choice.AllowedNSSAI.list.array[i]->s_NSSAI);
  }

  /* id-UESecurityCapabilities */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_UESecurityCapabilities, true);
  msg->security_capabilities = decode_ngap_security_capabilities(&ie->value.choice.UESecurityCapabilities);

  /* id-SecurityKey : Copy the security key */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_SecurityKey, true);
  memcpy(&msg->security_key, ie->value.choice.SecurityKey.buf, ie->value.choice.SecurityKey.size);

  /* id-MobilityRestrictionList */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t,
                             ie,
                             container,
                             NGAP_ProtocolIE_ID_id_MobilityRestrictionList,
                             false);
  if (ie != NULL) {
    msg->mobility_restriction = decode_ngap_mobility_restriction(&ie->value.choice.MobilityRestrictionList);
    msg->mobility_restriction_flag = true;
  }

  /* id-NAS-PDU */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_InitialContextSetupRequestIEs_t, ie, container,
                                 NGAP_ProtocolIE_ID_id_NAS_PDU, false);

  if (ie)
    msg->nas_pdu = create_byte_array(ie->value.choice.NAS_PDU.size, ie->value.choice.NAS_PDU.buf);

  itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);

  NGAP_INFO("Initial Context Setup UE RAN ID %d UE AMF ID %ld: %d PDU session(s)\n",
            msg->gNB_ue_ngap_id,
            msg->amf_ue_ngap_id,
            msg->nb_of_pdusessions);

  return 0;
}

static int ngap_gNB_handle_ue_context_release_command(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  UNUSED(stream);
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  MessageDef            *message_p        = NULL;
  uint64_t                            amf_ue_ngap_id;
  NGAP_RAN_UE_NGAP_ID_t               gnb_ue_ngap_id;
  NGAP_UEContextReleaseCommand_t     *container;
  NGAP_UEContextReleaseCommand_IEs_t *ie;
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.UEContextReleaseCommand;

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received UE context release command for non "
               "existing AMF context\n", assoc_id);
    return -1;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_UEContextReleaseCommand_IEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_UE_NGAP_IDs, true);

  switch (ie->value.choice.UE_NGAP_IDs.present) {
    case NGAP_UE_NGAP_IDs_PR_uE_NGAP_ID_pair:
      gnb_ue_ngap_id = ie->value.choice.UE_NGAP_IDs.choice.uE_NGAP_ID_pair->rAN_UE_NGAP_ID;
      asn_INTEGER2ulong(&(ie->value.choice.UE_NGAP_IDs.choice.uE_NGAP_ID_pair->aMF_UE_NGAP_ID), &amf_ue_ngap_id);
      ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(gnb_ue_ngap_id);
      if (!ue_desc_p) {
        NGAP_ERROR(
            "[SCTP %u] Received UE context release command for non "
            "existing UE context 0x%06lx\n",
            assoc_id,
            gnb_ue_ngap_id);
        return -1;
      } else {
        message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_UE_CONTEXT_RELEASE_COMMAND);

        if (ue_desc_p->amf_ue_ngap_id == 0) { // case of Detach Request and switch off from RRC_IDLE mode
          ue_desc_p->amf_ue_ngap_id = amf_ue_ngap_id;
        }

        NGAP_UE_CONTEXT_RELEASE_COMMAND(message_p).gNB_ue_ngap_id = gnb_ue_ngap_id;
        itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);
        return 0;
      }

      break;

      //#warning "TODO mapping amf_ue_ngap_id  gnb_ue_ngap_id?"

    case NGAP_UE_NGAP_IDs_PR_aMF_UE_NGAP_ID:
      asn_INTEGER2ulong(&(ie->value.choice.UE_NGAP_IDs.choice.aMF_UE_NGAP_ID), &amf_ue_ngap_id);
      NGAP_ERROR("TO DO mapping amf_ue_ngap_id  gnb_ue_ngap_id");
      (void)amf_ue_ngap_id; /* TODO: remove - it's to remove gcc warning about unused var */

    case NGAP_UE_NGAP_IDs_PR_NOTHING:
    default:
      NGAP_ERROR("NGAP_UE_CONTEXT_RELEASE_COMMAND not processed, missing info elements");
      return -1;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_UEContextReleaseCommand_IEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_Cause, true);
  /* TBD */

  return 0;
}

static int ngap_gNB_handle_pdusession_setup_request(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  //  NGAP_AMF_UE_NGAP_ID_t       amf_ue_ngap_id;
  uint64_t                      amf_ue_ngap_id;
  NGAP_RAN_UE_NGAP_ID_t         ran_ue_ngap_id;
  ngap_gNB_amf_data_t          *amf_desc_p       = NULL;

  NGAP_PDUSessionResourceSetupRequest_t     *container;
  NGAP_PDUSessionResourceSetupRequestIEs_t  *ie;
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.PDUSessionResourceSetupRequest;

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received pdu session resource setup request for non "
               "existing AMF context\n", assoc_id);
    return -1;
  }

  /* id-AMF-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);
  asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);

  /* id-gNB-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceSetupRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);
  ran_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(ran_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR("[SCTP %u] Received pdu session resource setup request for non "
               "existing UE context 0x%06lx\n", assoc_id,
               ran_ue_ngap_id);
    return -1;
  }

  ue_desc_p->rx_stream = stream;

  if ( ue_desc_p->amf_ue_ngap_id != amf_ue_ngap_id) {
    NGAP_WARN("UE context amf_ue_ngap_id is different form that of the message (%lu != %lu)",
              (uint64_t)ue_desc_p->amf_ue_ngap_id, amf_ue_ngap_id);
  }

  MessageDef * message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PDUSESSION_SETUP_REQ);
  ngap_pdusession_setup_req_t * msg=&NGAP_PDUSESSION_SETUP_REQ(message_p);
  msg->gNB_ue_ngap_id = ue_desc_p->gNB_ue_ngap_id;
  msg->amf_ue_ngap_id = ue_desc_p->amf_ue_ngap_id;

  /* UE Aggregated Maximum Bitrate */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceSetupRequestIEs_t, ie, container,
                         NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate, false);
  if (ie != NULL) {
    msg->ueAggMaxBitRate = decode_ngap_UEAggregateMaximumBitRate(&ie->value.choice.UEAggregateMaximumBitRate);
    msg->has_ue_ambr = true;
  }

  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceSetupRequestIEs_t, ie, container,
                         NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListSUReq, true);

  msg->nb_pdusessions_tosetup = ie->value.choice.PDUSessionResourceSetupListSUReq.list.count;

  for (int i = 0; i < ie->value.choice.PDUSessionResourceSetupListSUReq.list.count; i++) {
    NGAP_PDUSessionResourceSetupItemSUReq_t *item_p = ie->value.choice.PDUSessionResourceSetupListSUReq.list.array[i];
    msg->pdusession[i].pdusession_id = item_p->pDUSessionID;

    // S-NSSAI
    msg->pdusession[i].nssai = decode_ngap_nssai(&item_p->s_NSSAI);

    // NAS PDU (optional)
    if (item_p->pDUSessionNAS_PDU)
      msg->pdusession[i].nas_pdu = create_byte_array(item_p->pDUSessionNAS_PDU->size, item_p->pDUSessionNAS_PDU->buf);

    bool ret = decodePDUSessionResourceSetup(&msg->pdusession[i].pdusessionTransfer, item_p->pDUSessionResourceSetupRequestTransfer);
    if (!ret) {
      NGAP_ERROR("Failed to decode pDUSessionResourceSetupRequestTransfer in NG Setup Request\n");
      return -1;
    }
  }
  itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);

  return 0;
}

/** @brief Handler for the NG Handover Command
 *         AMF -> source NG-RAN */
static int ngap_gNB_handle_handover_command(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_INFO("Received NG Handover Command\n");
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  DevAssert(pdu != NULL);

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR(
        "[SCTP %u] Received Handover Command for non "
        "existing AMF context\n",
        assoc_id);
    return -1;
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_HANDOVER_COMMAND);
  ngap_handover_command_t *msg = &NGAP_HANDOVER_COMMAND(message_p);
  if (decode_ng_handover_command(msg, pdu) < 0) {
    NGAP_ERROR("Failed to decode NG Handover Command");
    free_ng_handover_command(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(msg->gNB_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR(
        "[SCTP %u] Received HO Command Msg for non existing UE context (gNB_ue_ngap_id %d) \n",
        assoc_id,
        msg->gNB_ue_ngap_id);
    free_ng_handover_command(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  ue_desc_p->rx_stream = stream;

  if (ue_desc_p->amf_ue_ngap_id != msg->amf_ue_ngap_id) {
    NGAP_ERROR("UE context amf_ue_ngap_id is different form that of the message (%ld != %ld)",
               ue_desc_p->amf_ue_ngap_id,
               msg->amf_ue_ngap_id);
    free_ng_handover_command(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  NGAP_DEBUG("Handover Command is parsed with a size of: %ld. Sending to RRC GNB!\n", msg->handoverCommand.len);
  itti_send_msg_to_task(TASK_RRC_GNB, amf_desc_p->ngap_gNB_instance->instance, message_p);

  return 0;
}

/** @brief Handler for NG Handover Cancel Acknowledge */
static int ngap_gNB_handle_handover_cancel_ack(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_INFO("Received NG Handover Cancel Acknowledge\n");
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  DevAssert(pdu != NULL);

  amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0);
  if (amf_desc_p == NULL) {
    NGAP_ERROR("[SCTP %u] Received Handover Cancel Ack for non-existing AMF context\n", assoc_id);
    return -1;
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_HANDOVER_CANCEL_ACK);
  ngap_handover_cancel_ack_t *msg = &NGAP_HANDOVER_CANCEL_ACK(message_p);

  if (decode_ng_handover_cancel_ack(msg, pdu) < 0) {
    NGAP_ERROR("Failed to decode NG Handover Cancel Acknowledge\n");
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(msg->gNB_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR("[SCTP %u] Handover Cancel Ack for unknown UE context (gNB_ue_ngap_id %d)\n", assoc_id, msg->gNB_ue_ngap_id);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  if (ue_desc_p->amf_ue_ngap_id != msg->amf_ue_ngap_id) {
    NGAP_WARN("AMF UE NGAP ID mismatch in HO Cancel Ack (ctx %ld != msg %ld)", ue_desc_p->amf_ue_ngap_id, msg->amf_ue_ngap_id);
  }

  ue_desc_p->rx_stream = stream;

  // cancel was already handled at RRC level, do not forward

  return 0;
}

/** @brief Handler for NGAP Path Switch Request Acknowledge
 *   AMF -> NG-RAN Node */
static int ngap_gNB_handle_ng_path_switch_request_ack(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  NGAP_INFO("Received NG Path Switch Request Acknowledge\n");
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  DevAssert(pdu != NULL);

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received Path Switch Request "
               "Acknowledge for non existing AMF context\n", assoc_id);
    return -1;
  }
  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PATH_SWITCH_REQ_ACK);
  ngap_path_switch_req_ack_t *msg = &NGAP_PATH_SWITCH_REQ_ACK(message_p);
  memset(msg, 0, sizeof(*msg));
  if (decode_ng_path_switch_request_acknowledge(msg, pdu) < 0) {
    NGAP_ERROR("Failed to decode NG Path Switch Request Acknowledge\n");
    free_ng_path_switch_req_ack(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(msg->gNB_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR("[SCTP %u] Received Path Switch Request Acknowledge for non "
               "existing UE context (gNB_ue_ngap_id %d)\n",
               assoc_id,
               msg->gNB_ue_ngap_id);
    free_ng_path_switch_req_ack(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  ue_desc_p->rx_stream = stream;
  if (ue_desc_p->amf_ue_ngap_id != msg->amf_ue_ngap_id) {
    NGAP_ERROR("UE context amf_ue_ngap_id is different from that of the message (%ld != %ld)",
               ue_desc_p->amf_ue_ngap_id,
               msg->amf_ue_ngap_id);
    free_ng_path_switch_req_ack(msg);
    itti_free(TASK_NGAP, message_p);
    return -1;
  }

  itti_send_msg_to_task(TASK_RRC_GNB, amf_desc_p->ngap_gNB_instance->instance, message_p);
  return 0;
}

/**
 * @brief Handle NGAP Paging message from AMF
 * @param assoc_id SCTP association ID
 * @param stream SCTP stream ID (must be 0 for paging per TS 38.412 Clause 7)
 * @param pdu NGAP PDU containing Paging message
 * @return 0 on success, -1 on failure */
static int ngap_gNB_handle_paging(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  ngap_gNB_amf_data_t   *amf_desc_p        = NULL;
  ngap_gNB_instance_t   *ngap_gNB_instance = NULL;
  DevAssert(pdu != NULL);
  // received Paging Message from AMF
  NGAP_DEBUG("[SCTP %u] Received Paging Message From AMF\n",assoc_id);

  /* Per 3GPP TS 38.412 Clause 7 (Transport layer):
   * Paging is classified as "non-UE-associated signalling" because:
   * - UE is in RRC_IDLE or RRC_INACTIVE state
   * - No active UE context exists in the RAN
   * - Paging targets UE by 5G-S-TMSI, not by RAN-UE-NGAP-ID
   * Non-UE-associated signalling MUST use stream 0 (or a reserved stream pair) */
  if (stream != 0) {
    LOG_W(NGAP,"[SCTP %d] Received Paging procedure on stream != 0 (expected stream 0 per TS 38.412)\n", assoc_id);
    return -1;
  }

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received Paging for non "
               "existing AMF context\n", assoc_id);
    return -1;
  }

  ngap_gNB_instance = amf_desc_p->ngap_gNB_instance;

  if (ngap_gNB_instance == NULL) {
    NGAP_ERROR("[SCTP %u] Received Paging for non existing AMF context : ngap_gNB_instance is NULL\n",
               assoc_id);
    return -1;
  }

   MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PAGING_IND);
   ngap_paging_ind_t *msg = &NGAP_PAGING_IND(message_p);

   /* Decode NGAP Paging message */
   if (!decode_ng_paging(msg, pdu)) {
     NGAP_ERROR("[SCTP %u] Failed to decode NGAP Paging message\n", assoc_id);
     free_ng_paging(msg);
     itti_free(TASK_NGAP, message_p);
     return -1;
   }

   /* send message to RRC */
   itti_send_msg_to_task(TASK_RRC_GNB, ngap_gNB_instance->instance, message_p);

   return 0;
}

static bool decodePDUSessionResourceModify(pdusession_mod_req_transfer_t *out, const OCTET_STRING_t in)
{
  void *decoded = decode_pdusession_transfer(&asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, in);
  if (!decoded) {
    LOG_E(NR_RRC, "Failed to decode PDUSessionResourceModifyRequestTransfer\n");
    return false;
  }

  // Reset counters
  out->nb_qos_to_add_modify = 0;
  out->nb_qos_to_release = 0;

  NGAP_PDUSessionResourceModifyRequestTransfer_t *pdusessionTransfer = (NGAP_PDUSessionResourceModifyRequestTransfer_t *)decoded;
  for (int j = 0; j < pdusessionTransfer->protocolIEs.list.count; j++) {
    NGAP_PDUSessionResourceModifyRequestTransferIEs_t *pdusessionTransfer_ies = pdusessionTransfer->protocolIEs.list.array[j];
    if (!pdusessionTransfer_ies) {
      LOG_E(NGAP, "NULL protocol IE at index %d in PDUSessionResourceModifyRequestTransfer\n", j);
      ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
      return false;
    }
    switch (pdusessionTransfer_ies->id) {
        /* optional QosFlowAddOrModifyRequestList */
      case NGAP_ProtocolIE_ID_id_QosFlowAddOrModifyRequestList: {
        NGAP_QosFlowAddOrModifyRequestList_t *addmod = &pdusessionTransfer_ies->value.choice.QosFlowAddOrModifyRequestList;
        out->nb_qos_to_add_modify = addmod->list.count;
        if (out->nb_qos_to_add_modify > MAX_QOS_FLOWS) {
          LOG_E(NGAP, "QosFlowAddOrModifyRequestList count %d exceeds MAX_QOS_FLOWS %d\n",
                out->nb_qos_to_add_modify, MAX_QOS_FLOWS);
          ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
          return false;
        }
        for (int i = 0; i < out->nb_qos_to_add_modify; i++) {
          NGAP_QosFlowAddOrModifyRequestItem_t *item = addmod->list.array[i];
          if (!item) {
            LOG_E(NGAP, "NULL QosFlowAddOrModifyRequestItem at index %d\n", i);
            ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
            return false;
          }
          if (!item->qosFlowLevelQosParameters) {
            LOG_E(NGAP, "NULL qosFlowLevelQosParameters for QFI %ld\n", item->qosFlowIdentifier);
            ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
            return false;
          }
          out->qos_to_add_modify[i] = fill_qos(item->qosFlowIdentifier, item->qosFlowLevelQosParameters);
        }
        break;
      }
      case NGAP_ProtocolIE_ID_id_QosFlowToReleaseList: {
        NGAP_QosFlowListWithCause_t *list = &pdusessionTransfer_ies->value.choice.QosFlowListWithCause;
        out->nb_qos_to_release = list->list.count;
        if (out->nb_qos_to_release > MAX_QOS_FLOWS) {
          LOG_E(NGAP, "QosFlowToReleaseList count %d exceeds MAX_QOS_FLOWS %d\n",
                out->nb_qos_to_release, MAX_QOS_FLOWS);
          ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
          return false;
        }
        for (int i = 0; i < out->nb_qos_to_release; i++) {
          NGAP_QosFlowWithCauseItem_t *item = list->list.array[i];
          if (!item) {
            LOG_E(NGAP, "NULL QosFlowWithCauseItem at index %d\n", i);
            ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
            return false;
          }
          out->qos_to_release[i].qfi = item->qosFlowIdentifier;
          out->qos_to_release[i].cause = decode_ngap_cause(&item->cause);
        }
        break;
      }
      default:
        LOG_W(NGAP, "Unhandled optional IE %ld\n", pdusessionTransfer_ies->id);
        continue;
    }
  }
  ASN_STRUCT_FREE(asn_DEF_NGAP_PDUSessionResourceModifyRequestTransfer, pdusessionTransfer);
  return true;
}

static int ngap_gNB_handle_pdusession_modify_request(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  NGAP_PDUSessionResourceModifyRequest_t     *container;
  NGAP_PDUSessionResourceModifyRequestIEs_t  *ie;
  NGAP_RAN_UE_NGAP_ID_t         gnb_ue_ngap_id;
  uint64_t                      amf_ue_ngap_id;
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.PDUSessionResourceModifyRequest;

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received PDUSession Resource modify request for non "
               "existing AMF context\n", assoc_id);
    return -1;
  }

  /* id-AMF-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceModifyRequestIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);

  if (ie != NULL) { /* checked by macro but cppcheck doesn't see it */
    asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);
  } else {
    return -1;
  }

  /* id-RAN-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceModifyRequestIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);

    gnb_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;
    ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(gnb_ue_ngap_id);
    if (!ue_desc_p) {
      NGAP_ERROR(
          "[SCTP %u] Received PDUSession Resource modify request for non "
          "existing UE context 0x%08lx\n",
          assoc_id,
          gnb_ue_ngap_id);
      return -1;
    }

  ue_desc_p->rx_stream = stream;
  /* id-PDUSessionResourceModifyListModReq */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceModifyRequestIEs_t, ie, container, NGAP_ProtocolIE_ID_id_PDUSessionResourceModifyListModReq, true);

  if (ue_desc_p->amf_ue_ngap_id != amf_ue_ngap_id) {
    NGAP_WARN("UE context amf_ue_ngap_id is different form that of the message (%lu != %lu)",
              (uint64_t)ue_desc_p->amf_ue_ngap_id, amf_ue_ngap_id);
    MessageDef *message_p = itti_alloc_new_message (TASK_RRC_GNB, 0, NGAP_PDUSESSION_MODIFY_RESP);
    ngap_pdusession_modify_resp_t* msg=&NGAP_PDUSESSION_MODIFY_RESP(message_p);
    msg->gNB_ue_ngap_id = gnb_ue_ngap_id;
    for (int nb_of_pdusessions_failed = 0; nb_of_pdusessions_failed < ie->value.choice.PDUSessionResourceModifyListModReq.list.count; nb_of_pdusessions_failed++) {
        NGAP_PDUSessionResourceModifyItemModReq_t *item_p;
        item_p = (NGAP_PDUSessionResourceModifyItemModReq_t *)ie->value.choice.PDUSessionResourceModifyListModReq.list.array[nb_of_pdusessions_failed];
        pdusession_failed_t *tmp = &msg->pdusessions_failed[nb_of_pdusessions_failed];
        tmp->pdusession_id = item_p->pDUSessionID;
        tmp->cause.type = NGAP_CAUSE_RADIO_NETWORK;
        tmp->cause.value = NGAP_CauseRadioNetwork_unknown_local_UE_NGAP_ID;
      }
    msg->nb_of_pdusessions_failed = ie->value.choice.PDUSessionResourceModifyListModReq.list.count;
    ngap_gNB_pdusession_modify_resp(amf_desc_p->ngap_gNB_instance->instance,msg);
    itti_free(TASK_RRC_GNB, message_p);
    return -1;
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PDUSESSION_MODIFY_REQ);
  ngap_pdusession_modify_req_t * msg=&NGAP_PDUSESSION_MODIFY_REQ(message_p);
  msg->amf_ue_ngap_id  = amf_ue_ngap_id;
  msg->gNB_ue_ngap_id = gnb_ue_ngap_id;

  msg->nb_pdusessions_tomodify = ie->value.choice.PDUSessionResourceModifyListModReq.list.count;

  for (int i = 0; i < ie->value.choice.PDUSessionResourceModifyListModReq.list.count; i++) {
    NGAP_PDUSessionResourceModifyItemModReq_t *item_p = ie->value.choice.PDUSessionResourceModifyListModReq.list.array[i];

    msg->pdusession[i].pdusession_id = item_p->pDUSessionID;

    // check for the NAS PDU
    if (item_p->nAS_PDU != NULL && item_p->nAS_PDU->size > 0) {
      msg->pdusession[i].nas_pdu = create_byte_array(item_p->nAS_PDU->size, item_p->nAS_PDU->buf);
      if (!decodePDUSessionResourceModify(&msg->pdusession[i].pdusessionTransfer, item_p->pDUSessionResourceModifyRequestTransfer)) {
        NGAP_ERROR("Failed to decode pDUSessionResourceModifyRequestTransfer for PDU session %lu\n", item_p->pDUSessionID);
        itti_free(TASK_NGAP, message_p);
        return -1;
      }
    } else {
      LOG_W(NGAP, "received pdu session modify with void content for UE %u, pdu session %lu\n", msg->gNB_ue_ngap_id, item_p->pDUSessionID);
      continue;
    }
  }
  itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);

  return 0;
}

// handle pdu session release command and send it to rrc_end
static int ngap_gNB_handle_pdusession_release_command(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  int i;
  ngap_gNB_amf_data_t *amf_desc_p = NULL;
  NGAP_PDUSessionResourceReleaseCommand_t     *container;
  NGAP_PDUSessionResourceReleaseCommandIEs_t  *ie;
  NGAP_RAN_UE_NGAP_ID_t           gnb_ue_ngap_id;
  uint64_t                        amf_ue_ngap_id;
  DevAssert(pdu != NULL);
  container = &pdu->choice.initiatingMessage->value.choice.PDUSessionResourceReleaseCommand;

  if ((amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0)) == NULL) {
    NGAP_ERROR("[SCTP %u] Received pdu session release command for non existing AMF context\n", assoc_id);
    return -1;
  }


  /* id-AMF-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceReleaseCommandIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);

  if (ie != NULL) { /* checked by macro but cppcheck doesn't see it */
    asn_INTEGER2ulong(&(ie->value.choice.AMF_UE_NGAP_ID), &amf_ue_ngap_id);
  } else {
    return -1;
  }

  /* id-RAN-UE-NGAP-ID */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceReleaseCommandIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);

  if (ie != NULL) { /* checked by macro but cppcheck doesn't see it */
    gnb_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;
  } else {
    return -1;
  }

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(gnb_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR("[SCTP %u] Received PDUSession Resource release command for non existing UE context 0x%08lx\n", assoc_id,
               ie->value.choice.RAN_UE_NGAP_ID);
    return -1;
  }

  ue_desc_p->rx_stream = stream;

  if (ue_desc_p->amf_ue_ngap_id != amf_ue_ngap_id) {
    NGAP_WARN("UE context amf_ue_ngap_id is different form that of the message (%lu != %lu)",
              (uint64_t)ue_desc_p->amf_ue_ngap_id, amf_ue_ngap_id);
  }

  NGAP_DEBUG("[SCTP %u] Received pdu session release command for gNB_UE_NGAP_ID %lu amf_ue_ngap_id %lu\n",
             assoc_id, gnb_ue_ngap_id, amf_ue_ngap_id);
  MessageDef * message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PDUSESSION_RELEASE_COMMAND);
  ngap_pdusession_release_command_t * msg=&NGAP_PDUSESSION_RELEASE_COMMAND(message_p);

  msg->gNB_ue_ngap_id = gnb_ue_ngap_id;
  msg->amf_ue_ngap_id = amf_ue_ngap_id;
  /* id-NAS-PDU */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceReleaseCommandIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_NAS_PDU, false);

  if (ie)
    msg->nas_pdu = create_byte_array(ie->value.choice.NAS_PDU.size, ie->value.choice.NAS_PDU.buf);

  /* id-PDUSessionResourceToReleaseListRelCmd */
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_PDUSessionResourceReleaseCommandIEs_t, ie, container,
                             NGAP_ProtocolIE_ID_id_PDUSessionResourceToReleaseListRelCmd, true);

  msg->nb_pdusessions_torelease = ie->value.choice.PDUSessionResourceToReleaseListRelCmd.list.count;

  for (i = 0; i < ie->value.choice.PDUSessionResourceToReleaseListRelCmd.list.count; i++) {
    NGAP_PDUSessionResourceToReleaseItemRelCmd_t *item_p;
    item_p = ie->value.choice.PDUSessionResourceToReleaseListRelCmd.list.array[i];
    msg->pdusession_ids[i] = item_p->pDUSessionID;

    /* PDUSessionResourceReleaseCommandTransfer (Mandatory) */
    void *decoded = decode_pdusession_transfer(&asn_DEF_NGAP_PDUSessionResourceReleaseCommandTransfer,
                                               item_p->pDUSessionResourceReleaseCommandTransfer);
    if (!decoded) {
      NGAP_ERROR("Failed to decode PDUSessionResourceReleaseCommandTransfer for PDU Session %d\n", msg->pdusession_ids[i]);
      return -1;
    }
    NGAP_PDUSessionResourceReleaseCommandTransfer_t *transfer = decoded;
    ngap_cause_t cause = decode_ngap_cause(&transfer->cause);
    NGAP_INFO("PDU Session %d release command: Cause type=%d value=%d\n", msg->pdusession_ids[i], cause.type, cause.value);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDUSessionResourceReleaseCommandTransfer, transfer);
    free(transfer);
  }

  itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);

  return 0;
}

static int ngap_gNB_handle_dl_ran_status_transfer(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
  DevAssert(pdu != NULL);

  NGAP_DownlinkRANStatusTransfer_t *container = &pdu->choice.initiatingMessage->value.choice.DownlinkRANStatusTransfer;
  NGAP_DownlinkRANStatusTransferIEs_t *ie = NULL;

  ngap_gNB_amf_data_t *amf_desc_p = ngap_gNB_get_AMF(NULL, assoc_id, 0);
  if (!amf_desc_p) {
    NGAP_ERROR("[SCTP %u] Received DL RAN Status Transfer for unknown AMF\n", assoc_id);
    return -1;
  }

  // AMF UE NGAP ID
  uint64_t amf_ue_ngap_id = 0;
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkRANStatusTransferIEs_t, ie, container, NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID, true);
  asn_INTEGER2ulong(&ie->value.choice.AMF_UE_NGAP_ID, &amf_ue_ngap_id);

  // RAN UE NGAP ID
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkRANStatusTransferIEs_t, ie, container, NGAP_ProtocolIE_ID_id_RAN_UE_NGAP_ID, true);
  NGAP_RAN_UE_NGAP_ID_t gnb_ue_ngap_id = ie->value.choice.RAN_UE_NGAP_ID;

  ngap_gNB_ue_context_t *ue_desc_p = ngap_get_ue_context(gnb_ue_ngap_id);
  if (!ue_desc_p) {
    NGAP_ERROR("No UE context for gNB_ue_ngap_id %lu\n", gnb_ue_ngap_id);
    return -1;
  }

  ue_desc_p->rx_stream = stream;
  if (ue_desc_p->amf_ue_ngap_id != amf_ue_ngap_id)
    NGAP_WARN("UE context mismatch: amf_ue_ngap_id (%lu != %lu)\n", ue_desc_p->amf_ue_ngap_id, amf_ue_ngap_id);

  // Transparent container
  NGAP_FIND_PROTOCOLIE_BY_ID(NGAP_DownlinkRANStatusTransferIEs_t,
                             ie,
                             container,
                             NGAP_ProtocolIE_ID_id_RANStatusTransfer_TransparentContainer,
                             true);
  NGAP_RANStatusTransfer_TransparentContainer_t *tcont = &ie->value.choice.RANStatusTransfer_TransparentContainer;

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_DL_RAN_STATUS_TRANSFER);
  ngap_ran_status_transfer_t *msg = &NGAP_DL_RAN_STATUS_TRANSFER(message_p);

  msg->amf_ue_ngap_id = amf_ue_ngap_id;
  msg->gnb_ue_ngap_id = gnb_ue_ngap_id;

  int nb = tcont->dRBsSubjectToStatusTransferList.list.count;
  for (int i = 0; i < nb; ++i) {
    NGAP_DRBsSubjectToStatusTransferItem_t *item = tcont->dRBsSubjectToStatusTransferList.list.array[i];
    ngap_drb_status_t *s = &msg->ran_status.drb_status_list[i];
    s->drb_id = item->dRB_ID;

    // UL COUNT
    switch (item->dRBStatusUL.present) {
      case NGAP_DRBStatusUL_PR_dRBStatusUL18:
        s->ul_count.sn_len = NGAP_SN_LENGTH_18;
        s->ul_count.pdcp_sn = item->dRBStatusUL.choice.dRBStatusUL18->uL_COUNTValue.pDCP_SN18;
        s->ul_count.hfn = item->dRBStatusUL.choice.dRBStatusUL18->uL_COUNTValue.hFN_PDCP_SN18;
        break;
      case NGAP_DRBStatusUL_PR_dRBStatusUL12:
        s->ul_count.sn_len = NGAP_SN_LENGTH_12;
        s->ul_count.pdcp_sn = item->dRBStatusUL.choice.dRBStatusUL12->uL_COUNTValue.pDCP_SN12;
        s->ul_count.hfn = item->dRBStatusUL.choice.dRBStatusUL12->uL_COUNTValue.hFN_PDCP_SN12;
        break;
      default:
        NGAP_ERROR("Unknown dRBStatusUL.present=%d\n", item->dRBStatusUL.present);
        break;
    }

    // DL COUNT
    switch (item->dRBStatusDL.present) {
      case NGAP_DRBStatusDL_PR_dRBStatusDL18:
        s->dl_count.sn_len = NGAP_SN_LENGTH_18;
        s->dl_count.pdcp_sn = item->dRBStatusDL.choice.dRBStatusDL18->dL_COUNTValue.pDCP_SN18;
        s->dl_count.hfn = item->dRBStatusDL.choice.dRBStatusDL18->dL_COUNTValue.hFN_PDCP_SN18;
        break;
      case NGAP_DRBStatusDL_PR_dRBStatusDL12:
        s->dl_count.sn_len = NGAP_SN_LENGTH_12;
        s->dl_count.pdcp_sn = item->dRBStatusDL.choice.dRBStatusDL12->dL_COUNTValue.pDCP_SN12;
        s->dl_count.hfn = item->dRBStatusDL.choice.dRBStatusDL12->dL_COUNTValue.hFN_PDCP_SN12;
        break;
      default:
        NGAP_ERROR("Unknown dRBStatusDL.present=%d\n", item->dRBStatusDL.present);
        break;
    }

    msg->ran_status.nb_drb++;
  }

  itti_send_msg_to_task(TASK_RRC_GNB, ue_desc_p->gNB_instance->instance, message_p);
  return 0;
}

typedef int (*ngap_message_decoded_callback)(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu);

/* Handlers matrix. Only gNB related procedure present here */
const ngap_message_decoded_callback ngap_messages_callback[][3] = {
    {0, 0, 0}, /* AMFConfigurationUpdate */
    {0, 0, 0}, /* AMFStatusIndication */
    {0, 0, 0}, /* CellTrafficTrace */
    {0, 0, 0}, /* DeactivateTrace */
    {ngap_gNB_handle_nas_downlink, 0, 0}, /* DownlinkNASTransport */
    {ngap_gNB_handle_downlink_non_ue_associated_nrppa_transport, 0, 0}, /* DownlinkNonUEAssociatedNRPPaTransport */
    {0, 0, 0}, /* DownlinkRANConfigurationTransfer */
    {ngap_gNB_handle_dl_ran_status_transfer, 0, 0}, /* DownlinkRANStatusTransfer */
    {ngap_gNB_handle_downlink_ue_associated_nrppa_transport, 0, 0}, /* DownlinkUEAssociatedNRPPaTransport */
    {ngap_gNB_handle_error_indication, 0, 0}, /* ErrorIndication */
    {0, ngap_gNB_handle_handover_cancel_ack, 0}, /* HandoverCancel */
    {0, 0, 0}, /* HandoverNotification */
    {0, ngap_gNB_handle_handover_command, 0}, /* HandoverPreparation */
    {ngap_gNB_handle_handover_request, 0, 0}, /* HandoverResourceAllocation */
    {ngap_gNB_handle_initial_context_request, 0, 0}, /* InitialContextSetup */
    {0, 0, 0}, /* InitialUEMessage */
    {0, 0, 0}, /* LocationReportingControl */
    {0, 0, 0}, /* LocationReportingFailureIndication */
    {0, 0, 0}, /* LocationReport */
    {0, 0, 0}, /* NASNonDeliveryIndication */
    {0, 0, 0}, /* NGReset */
    {0, ngap_gNB_handle_ng_setup_response, ngap_gNB_handle_ng_setup_failure}, /* NGSetup */
    {0, 0, 0}, /* OverloadStart */
    {0, 0, 0}, /* OverloadStop */
    {ngap_gNB_handle_paging, 0, 0}, /* Paging */
    {0, ngap_gNB_handle_ng_path_switch_request_ack, 0}, /* PathSwitchRequest */
    {ngap_gNB_handle_pdusession_modify_request, 0, 0}, /* PDUSessionResourceModify */
    {0, 0, 0}, /* PDUSessionResourceModifyIndication */
    {ngap_gNB_handle_pdusession_release_command, 0, 0}, /* PDUSessionResourceRelease */
    {ngap_gNB_handle_pdusession_setup_request, 0, 0}, /* PDUSessionResourceSetup */
    {0, 0, 0}, /* PDUSessionResourceNotify */
    {0, 0, 0}, /* PrivateMessage */
    {0, 0, 0}, /* PWSCancel */
    {0, 0, 0}, /* PWSFailureIndication */
    {0, 0, 0}, /* PWSRestartIndication */
    {0, 0, 0}, /* RANConfigurationUpdate */
    {0, 0, 0}, /* RerouteNASRequest */
    {0, 0, 0}, /* RRCInactiveTransitionReport */
    {0, 0, 0}, /* TraceFailureIndication */
    {0, 0, 0}, /* TraceStart */
    {0, 0, 0}, /* UEContextModification */
    {ngap_gNB_handle_ue_context_release_command, 0, 0}, /* UEContextRelease */
    {0, 0, 0}, /* UEContextReleaseRequest */
    {0, 0, 0}, /* UERadioCapabilityCheck */
    {0, 0, 0}, /* UERadioCapabilityInfoIndication */
    {0, 0, 0}, /* UETNLABindingRelease */
    {0, 0, 0}, /* UplinkNASTransport */
    {0, 0, 0}, /* UplinkNonUEAssociatedNRPPaTransport */
    {0, 0, 0}, /* UplinkRANConfigurationTransfer */
    {0, 0, 0}, /* UplinkRANStatusTransfer */
    {0, 0, 0}, /* UplinkUEAssociatedNRPPaTransport */
    {0, 0, 0}, /* WriteReplaceWarning */
    {0, 0, 0}, /* SecondaryRATDataUsageReport */

};

int ngap_gNB_handle_message(sctp_assoc_t assoc_id, int32_t stream, const uint8_t *const data, const uint32_t data_length)
{
  NGAP_NGAP_PDU_t pdu;
  int ret;
  DevAssert(data != NULL);
  memset(&pdu, 0, sizeof(pdu));

  if (ngap_gNB_decode_pdu(&pdu, data, data_length) < 0) {
    NGAP_ERROR("Failed to decode PDU\n");
    return -1;
  }

  /* Checking procedure Code and direction of message */
  if (pdu.choice.initiatingMessage->procedureCode >= (long)(sizeof(ngap_messages_callback) / (3 * sizeof(ngap_message_decoded_callback))) || (pdu.present > NGAP_NGAP_PDU_PR_unsuccessfulOutcome)) {
    NGAP_ERROR("[SCTP %u] Either procedureCode %ld or direction %d exceed expected\n", assoc_id, pdu.choice.initiatingMessage->procedureCode, pdu.present);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* No handler present.
   * This can mean not implemented or no procedure for gNB (wrong direction).
   */
  if (ngap_messages_callback[pdu.choice.initiatingMessage->procedureCode][pdu.present - 1] == NULL) {
    NGAP_ERROR("[SCTP %u] No handler for procedureCode %ld in %s\n", assoc_id, pdu.choice.initiatingMessage->procedureCode, ngap_direction2String(pdu.present - 1));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
    return -1;
  }

  /* Calling the right handler */
  ret = (*ngap_messages_callback[pdu.choice.initiatingMessage->procedureCode][pdu.present - 1])(assoc_id, stream, &pdu);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_NGAP_PDU, &pdu);
  return ret;
}
