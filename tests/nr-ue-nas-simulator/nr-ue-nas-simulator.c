/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "executables/softmodem-common.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "common/ran_context.h"
#include "common/utils/system.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "openair2/GNB_APP/gnb_config_common.h"
#include "openair2/GNB_APP/gnb_config_ng.h"
#include "openair2/GNB_APP/gnb_paramdef.h"
#include "openair3/SCTP/sctp_default_values.h"
#include "openair3/NAS/NR_UE/nr_nas_msg.h"
#include "openair3/UTILS/conversions.h"
#include "executables/nr-uesoftmodem.h"
#include "openair3/ocp-gtpu/gtp_itf.h"
#include "SIMULATION/TOOLS/sim.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_qos_rule.h"
#include <stdlib.h>

RAN_CONTEXT_t RC;
THREAD_STRUCT thread_struct;
uint64_t downlink_frequency[MAX_NUM_CCs][4];
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
int oai_exit = 0;
int gnb_id = 0;
uint32_t tac = 1;
uint16_t mcc = 1;
uint16_t mnc = 1;
uint8_t mnc_len = 2;
char *gnb_ipv4_address_for_NGU = NULL;
int NB_UE_INST = 1;

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  LOG_E(GNB_APP, "error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

nfapi_mode_t nfapi_mod = -1;
void nfapi_setmode(nfapi_mode_t nfapi_mode)
{
  nfapi_mod = nfapi_mode;
}
nfapi_mode_t nfapi_getmode(void)
{
  return nfapi_mod;
}

ngran_node_t get_node_type()
{
  return ngran_gNB_CUUP;
}

nrUE_params_t *get_nrUE_params(void)
{
  static nrUE_params_t params = {0};
  return &params;
}

void nr_sdap_qos_rule_add(ue_id_t ue_id,
                         int pdusession_id,
                         uint8_t rule_id,
                         uint8_t qfi,
                         uint8_t precedence,
                         bool is_default,
                         const packet_filter_decoded_t *pf_list,
                         int num_pf)
{
}

void nr_sdap_qos_rule_remove(ue_id_t ue_id, int pdusession_id, uint8_t rule_id)
{
}

void nr_sdap_qos_rule_update(ue_id_t ue_id,
                            int pdusession_id,
                            uint8_t rule_id,
                            uint8_t qfi,
                            uint8_t precedence,
                            bool is_default,
                            const packet_filter_decoded_t *pf_list,
                            int num_pf,
                            bool replace)
{
}

void nr_sdap_qos_rule_delete_pf(ue_id_t ue_id, int pdusession_id, uint8_t rule_id, const uint8_t *pf_ids, int num_ids)
{
}

int nr_rlc_get_available_tx_space(const rnti_t rntiP, const logical_chan_id_t channel_idP)
{
  abort();
  return 0;
}

configmodule_interface_t *uniqCfg = NULL;

static int identity_guti = 0;

static paramdef_t nas_sim_params[] = {
    BOOLPARAM("identity-guti",
              "Registration with random 5G-GUTI to trigger AMF Identity Request (SUCI)\n",
              PARAMFLAG_BOOL,
              &identity_guti,
              0),
};

/** Seed a 5G-GUTI unknown to the AMF (TS 23.502 step 6: AMF should request SUCI) */
static void seed_unknown_guti(nr_ue_nas_t *nas, const plmn_id_t *plmn)
{
  nas->guti = calloc_or_fail(1, sizeof(*nas->guti));
  Guti5GSMobileIdentity_t *guti = nas->guti;
  guti->typeofidentity = FGS_MOBILE_IDENTITY_5G_GUTI;
  guti->mccdigit1 = MCC_HUNDREDS(plmn->mcc);
  guti->mccdigit2 = MCC_MNC_DECIMAL(plmn->mcc);
  guti->mccdigit3 = MCC_MNC_DIGIT(plmn->mcc);
  guti->mncdigit1 = MNC_HUNDREDS(plmn->mnc, plmn->mnc_digit_length);
  guti->mncdigit2 = MCC_MNC_DECIMAL(plmn->mnc);
  guti->mncdigit3 = MCC_MNC_DIGIT(plmn->mnc);
  /* OAI CN5G served GUAMI */
  guti->amfregionid = 0x01;
  guti->amfsetid = 0x001;
  guti->amfpointer = 0x01;
  fill_random(&guti->tmsi, sizeof(guti->tmsi));
  LOG_I(NAS,
        "Unknown-GUTI test: Registration Request will use 5G-GUTI MCC=%u%u%u MNC=%u%u AMFRI=%u AMFSI=%u AMFPT=%u TMSI=0x%08x\n",
        guti->mccdigit1,
        guti->mccdigit2,
        guti->mccdigit3,
        guti->mncdigit1,
        guti->mncdigit2,
        guti->amfregionid,
        guti->amfsetid,
        guti->amfpointer,
        guti->tmsi);
}

// Emulate registration request to register UE in the AMF
void send_initial_ue_message(instance_t instance)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_NAS_FIRST_REQ);

  NGAP_NAS_FIRST_REQ(msg_p).gNB_ue_ngap_id = 1; // Simulated UE ID

  plmn_id_t plmn = {.mcc = mcc, .mnc = mnc, .mnc_digit_length = mnc_len};
  NGAP_NAS_FIRST_REQ(msg_p).plmn = plmn;

  NGAP_NAS_FIRST_REQ(msg_p).nr_cell_id = gnb_id;

  NGAP_NAS_FIRST_REQ(msg_p).establishment_cause = NGAP_RRC_CAUSE_MO_SIGNALLING;

  as_nas_info_t initialNasMsg = {0};
  nr_ue_nas_t *nr_ue_nas = get_ue_nas_info(0);
  // serving network name (snn)
  // Ideally snn should be filled from SIB1, but we operate at NAS/RRC level and we dont decode SIB1
  nr_ue_nas->sn_id = calloc(1, sizeof(plmn_id_t));
  *nr_ue_nas->sn_id = plmn;
  if (identity_guti) {
    seed_unknown_guti(nr_ue_nas, &plmn);

    ngap_ue_identity_t *ue_identity = &NGAP_NAS_FIRST_REQ(msg_p).ue_identity;
    const Guti5GSMobileIdentity_t *guti = nr_ue_nas->guti;

    ue_identity->presenceMask = NGAP_UE_IDENTITIES_FiveG_s_tmsi | NGAP_UE_IDENTITIES_guami;
    ue_identity->s_tmsi.amf_set_id = guti->amfsetid;
    ue_identity->s_tmsi.amf_pointer = guti->amfpointer;
    ue_identity->s_tmsi.m_tmsi = guti->tmsi;
    ue_identity->guami.plmn = plmn;
    ue_identity->guami.amf_region_id = guti->amfregionid;
    ue_identity->guami.amf_set_id = guti->amfsetid;
    ue_identity->guami.amf_pointer = guti->amfpointer;
  }
  generateRegistrationRequest(&initialNasMsg, nr_ue_nas, false);

  NGAP_NAS_FIRST_REQ(msg_p).nas_pdu.buf = initialNasMsg.nas_data;
  NGAP_NAS_FIRST_REQ(msg_p).nas_pdu.len = initialNasMsg.length;

  itti_send_msg_to_task(TASK_NGAP, instance, msg_p);
}

void send_uplink_nas_to_amf(instance_t instance, as_nas_info_t *nas_msg)
{
  if (nas_msg->length > 0 && nas_msg->nas_data != NULL) {
    MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, instance, NGAP_UPLINK_NAS);

    NGAP_UPLINK_NAS(msg).gNB_ue_ngap_id = 1; // Simulated UE ID
    NGAP_UPLINK_NAS(msg).nas_pdu.buf = nas_msg->nas_data;
    NGAP_UPLINK_NAS(msg).nas_pdu.len = nas_msg->length;

    NGAP_UPLINK_NAS(msg).plmn.mcc = mcc;
    NGAP_UPLINK_NAS(msg).plmn.mnc = mnc;
    NGAP_UPLINK_NAS(msg).plmn.mnc_digit_length = mnc_len;

    NGAP_UPLINK_NAS(msg).nr_cell_id = gnb_id;
    NGAP_UPLINK_NAS(msg).tac = tac;

    itti_send_msg_to_task(TASK_NGAP, instance, msg);
  }
}

void *rrc_ue_task_local(void *args_p)
{
  MessageDef *msg_p;
  instance_t instance;
  int result;

  itti_mark_task_ready(TASK_RRC_NRUE);
  LOG_I(NR_RRC, "Entering RRC UE task (TASK_RRC_NRUE)\n");

  while (1) {
    itti_receive_msg(TASK_RRC_NRUE, &msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);

    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(NR_RRC, " *** Exiting RRC UE thread\n");
        itti_exit_task();
        break;
      case NAS_UPLINK_DATA_REQ:
        LOG_I(NR_RRC, "Received NAS_UPLINK_DATA_REQ from NAS Library (via RRC UE Task)\n");
        as_nas_info_t nas_msg;
        nas_msg.nas_data = NAS_UPLINK_DATA_REQ(msg_p).nasMsg.nas_data;
        nas_msg.length = NAS_UPLINK_DATA_REQ(msg_p).nasMsg.length;
        send_uplink_nas_to_amf(instance, &nas_msg);
        break;
      case NAS_5GMM_IND:
        LOG_I(NR_RRC, "Received NAS_5GMM_IND (ignored for nr_ue_nas_simulator test)\n");
        break;
      case NAS_KENB_REFRESH_REQ:
        LOG_I(NR_RRC, "Received NAS_KENB_REFRESH_REQ (ignored for nr_ue_nas_simulator test)\n");
        break;
      case NAS_DETACH_REQ:
        LOG_I(NR_RRC, "Received NAS_DETACH_REQ (ignored for nr_ue_nas_simulator test)\n");
        break;
      default:
        LOG_E(NR_RRC, "RRC UE Task received unexpected message %s\n", ITTI_MSG_NAME(msg_p));
        break;
    }
    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    msg_p = NULL;
  }
  return NULL;
}

void *gNB_app_task(void *args_p)
{
  MessageDef *msg_p = NULL;
  const char *msg_name = NULL;
  instance_t instance;
  int result;
  /* for no gcc warnings */
  (void)instance;

  itti_mark_task_ready(TASK_GNB_APP);
  do {
    // Wait for a message
    itti_receive_msg(TASK_GNB_APP, &msg_p);

    msg_name = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);

    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(GNB_APP, " *** Exiting GNB_APP thread\n");
        itti_exit_task();
        break;

      case NGAP_REGISTER_GNB_CNF:
        LOG_I(GNB_APP, "[gNB %ld] Received %s: associated AMF %d\n", instance, msg_name, NGAP_REGISTER_GNB_CNF(msg_p).nb_amf);
        LOG_I(GNB_APP, "Triggering NAS Registration sequence\n");
        send_initial_ue_message(instance);
        break;

      default:
        LOG_E(GNB_APP, "Received unexpected message %s\n", msg_name);
        break;
    }
    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  } while (1);

  return NULL;
}

// Forward NAS payload from the AMF directly to the NAS NRUE Task
void forward_downlink_nas_to_ue(uint8_t *pdu_buf, uint32_t pdu_len)
{
  if (pdu_len > 0 && pdu_buf != NULL) {
    MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, 0, NAS_DOWNLINK_DATA_IND);

    NAS_DOWNLINK_DATA_IND(msg_p).UEid = 0;
    NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.length = pdu_len;
    NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.nas_data = malloc_or_fail(pdu_len);
    memcpy(NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.nas_data, pdu_buf, pdu_len);
    free(pdu_buf);

    LOG_I(NR_RRC, "Forwarding Downlink NAS message to real NAS Task (Length: %d)\n", pdu_len);
    itti_send_msg_to_task(TASK_NAS_NRUE, 0, msg_p);
  }
}

void *rrc_gnb_task(void *args_p)
{
  MessageDef *msg_p;
  MessageDef *msg_p_resp;
  instance_t instance;
  int result;

  itti_mark_task_ready(TASK_RRC_GNB);
  LOG_I(NR_RRC, "Entering main loop of NR_RRC message task\n");

  while (1) {
    // Wait for a message
    itti_receive_msg(TASK_RRC_GNB, &msg_p);
    const char *msg_name_p = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);
    LOG_D(NR_RRC,
          "RRC GNB Task Received %s for instance %ld from task %s\n",
          ITTI_MSG_NAME(msg_p),
          ITTI_MSG_DESTINATION_INSTANCE(msg_p),
          ITTI_MSG_ORIGIN_NAME(msg_p));
    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(NR_RRC, " *** Exiting NR_RRC thread\n");
        itti_exit_task();
        break;
      case NGAP_DOWNLINK_NAS:
        LOG_I(NR_RRC, "Received NGAP_DOWNLINK_NAS\n");
        forward_downlink_nas_to_ue(NGAP_DOWNLINK_NAS(msg_p).nas_pdu.buf, NGAP_DOWNLINK_NAS(msg_p).nas_pdu.len);
        break;
      case NGAP_INITIAL_CONTEXT_SETUP_REQ:
        LOG_I(NR_RRC, "Received NGAP_INITIAL_CONTEXT_SETUP_REQ\n");

        if (NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p).nas_pdu.len > 0) {
          LOG_I(NR_RRC, "Processing NAS PDU in Initial Context Setup\n");
          forward_downlink_nas_to_ue(NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p).nas_pdu.buf,
                                     NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p).nas_pdu.len);
        }

        LOG_I(NR_RRC, "Sending NGAP_INITIAL_CONTEXT_SETUP_RESP\n");
        msg_p_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_INITIAL_CONTEXT_SETUP_RESP);
        NGAP_INITIAL_CONTEXT_SETUP_RESP(msg_p_resp).gNB_ue_ngap_id = NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p).gNB_ue_ngap_id;
        itti_send_msg_to_task(TASK_NGAP, instance, msg_p_resp);
        break;
      case NAS_UPLINK_DATA_REQ:
        LOG_I(NR_RRC, "Received NAS_UPLINK_DATA_REQ from NAS Library\n");
        as_nas_info_t nas_msg;
        nas_msg.nas_data = NAS_UPLINK_DATA_REQ(msg_p).nasMsg.nas_data;
        nas_msg.length = NAS_UPLINK_DATA_REQ(msg_p).nasMsg.length;
        send_uplink_nas_to_amf(instance, &nas_msg);
        break;
      case NGAP_PDUSESSION_SETUP_REQ:
        LOG_I(NR_RRC, "Received NGAP_PDUSESSION_SETUP_REQ\n");

        for (int i = 0; i < NGAP_PDUSESSION_SETUP_REQ(msg_p).nb_pdusessions_tosetup; i++) {
          if (NGAP_PDUSESSION_SETUP_REQ(msg_p).pdusession[i].nas_pdu.len > 0) {
            forward_downlink_nas_to_ue(NGAP_PDUSESSION_SETUP_REQ(msg_p).pdusession[i].nas_pdu.buf,
                                       NGAP_PDUSESSION_SETUP_REQ(msg_p).pdusession[i].nas_pdu.len);
          }
        }

        msg_p_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_PDUSESSION_SETUP_RESP);
        NGAP_PDUSESSION_SETUP_RESP(msg_p_resp).gNB_ue_ngap_id = NGAP_PDUSESSION_SETUP_REQ(msg_p).gNB_ue_ngap_id;
        NGAP_PDUSESSION_SETUP_RESP(msg_p_resp).nb_of_pdusessions = NGAP_PDUSESSION_SETUP_REQ(msg_p).nb_pdusessions_tosetup;

        for (int i = 0; i < NGAP_PDUSESSION_SETUP_REQ(msg_p).nb_pdusessions_tosetup; i++) {
          pdusession_setup_t *resp_item = &NGAP_PDUSESSION_SETUP_RESP(msg_p_resp).pdusessions[i];
          pdusession_resource_item_t *req_item = &NGAP_PDUSESSION_SETUP_REQ(msg_p).pdusession[i];

          resp_item->pdusession_id = req_item->pdusession_id;
          resp_item->pdu_session_type = req_item->pdusessionTransfer.pdu_session_type;
          resp_item->n3_outgoing.teid = 0x00000001 + i;
          struct in_addr addr;
          inet_pton(AF_INET, gnb_ipv4_address_for_NGU, &addr);
          resp_item->n3_outgoing.addr.length = 4;
          memcpy(resp_item->n3_outgoing.addr.buffer, &addr.s_addr, resp_item->n3_outgoing.addr.length);
          resp_item->nb_of_qos_flow = req_item->pdusessionTransfer.nb_qos;
          DevAssert(resp_item->nb_of_qos_flow < MAX_QOS_FLOWS);
          for (int q = 0; q < resp_item->nb_of_qos_flow; q++) {
            resp_item->associated_qos_flows[q].qfi = req_item->pdusessionTransfer.qos[q].qfi;
            resp_item->associated_qos_flows[q].qos_flow_mapping_ind = QOSFLOW_MAPPING_INDICATION_DL;
          }
        }

        LOG_I(NR_RRC, "Sending NGAP_PDUSESSION_SETUP_RESP\n");
        itti_send_msg_to_task(TASK_NGAP, instance, msg_p_resp);

        // Initiate UE Deregistration
        sleep(1);
        MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, 0, NAS_DEREGISTRATION_REQ);
        NAS_DEREGISTRATION_REQ(msg).cause = AS_DETACH;
        itti_send_msg_to_task(TASK_NAS_NRUE, 0, msg);
        break;
      case NGAP_UE_CONTEXT_RELEASE_COMMAND:
        LOG_I(NR_RRC, "Received NGAP_UE_CONTEXT_RELEASE_COMMAND\n");

        msg_p_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_UE_CONTEXT_RELEASE_COMPLETE);
        NGAP_UE_CONTEXT_RELEASE_COMPLETE(msg_p_resp).gNB_ue_ngap_id = NGAP_UE_CONTEXT_RELEASE_COMMAND(msg_p).gNB_ue_ngap_id;

        LOG_I(NR_RRC, "Sending NGAP_UE_CONTEXT_RELEASE_COMPLETE\n");
        itti_send_msg_to_task(TASK_NGAP, instance, msg_p_resp);
        LOG_I(NR_RRC, "Simulator flow finished successfully!\n");
        exit(EXIT_SUCCESS);
        break;
      default:
        LOG_E(NR_RRC, "[gNB %ld] Received unexpected message %s\n", instance, msg_name_p);
        break;
    }
    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    msg_p = NULL;
  }
}

int main(int argc, char **argv)
{
  start_background_system();

  // load the config file
  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL) {
    exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  }
  config_get(uniqCfg, nas_sim_params, sizeofArray(nas_sim_params), NULL);

  logInit();

  nas_init_nrue(NB_UE_INST);

  set_softmodem_sighandler();

  // Initialize ITTI tasks
  itti_init(TASK_MAX, tasks_info);

  int rc;
  rc = itti_create_task(TASK_SCTP, sctp_eNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for SCTP failed\n");

  rc = itti_create_task(TASK_NGAP, ngap_gNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for NGAP failed\n");

  rc = itti_create_task(TASK_RRC_GNB, rrc_gnb_task, NULL);
  AssertFatal(rc >= 0, "Create task for RRC failed\n");

  rc = itti_create_task(TASK_GNB_APP, gNB_app_task, NULL);
  AssertFatal(rc >= 0, "Create task for gNB_app failed\n");

  rc = itti_create_task(TASK_NAS_NRUE, nas_nrue_task, NULL);
  AssertFatal(rc >= 0, "Create task for nas_nrue_task failed\n");

  rc = itti_create_task(TASK_RRC_NRUE, rrc_ue_task_local, NULL);
  AssertFatal(rc >= 0, "Create task for RRC_NRUE failed\n");

  MessageDef *msg_p;
  msg_p = itti_alloc_new_message(TASK_GNB_APP, 0, NGAP_REGISTER_GNB_REQ);
  RCconfig_NR_NG(msg_p, 0);
  gnb_id = NGAP_REGISTER_GNB_REQ(msg_p).gNB_id;
  tac = NGAP_REGISTER_GNB_REQ(msg_p).tac;
  mcc = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mcc;
  mnc = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mnc;
  mnc_len = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mnc_digit_length;
  gnb_ipv4_address_for_NGU = NGAP_REGISTER_GNB_REQ(msg_p).gnb_ip_address.ipv4_address;

  LOG_I(GNB_APP, "Sending NGAP_REGISTER_GNB_REQ to TASK_NGAP\n");
  itti_send_msg_to_task(TASK_NGAP, 0, msg_p);

  printf("TYPE <CTRL-C> TO TERMINATE\n");
  itti_wait_tasks_end(NULL);

  logClean();
  printf("Bye.\n");
  return 0;
}
