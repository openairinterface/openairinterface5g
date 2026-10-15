/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* xn-simulator-test: N gNB instances without radio, running Xn Setup and Xn handovers */

#include "common/ran_context.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "openair2/GNB_APP/gnb_config_ng.h"
#include "openair2/GNB_APP/gnb_config_xn.h"
#include "openair2/GNB_APP/gnb_paramdef.h"
#include "openair2/XNAP/xnap_gNB.h"
#include "openair3/SCTP/sctp_default_values.h"
#include "openair3/NAS/NR_UE/nr_nas_msg.h"
#include "xn-ho-sim.h"

RAN_CONTEXT_t RC;
THREAD_STRUCT thread_struct;
uint64_t downlink_frequency[MAX_NUM_CCs][4];
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
int oai_exit = 0;

/* Multi-instance support. */
static uint32_t nb_gnb_inst = 0;
static uint32_t *gnb_id_tab = NULL;
static uint32_t *tac_tab = NULL;
static uint16_t *mcc_tab = NULL;
static uint16_t *mnc_tab = NULL;
static uint8_t *mnc_len_tab = NULL;

static xnap_net_config_t *xn_net_config_tab = NULL;

/* --xn-setup-only: stop after Xn Setup, else Xn handovers of a UE stub (xn-ho-sim.c) */
static bool xn_setup_only = false;

uint32_t gnb_id = 0;
uint32_t tac = 1;
uint16_t mcc = 1;
uint16_t mnc = 1;
uint8_t mnc_len = 2;

char *gnb_ipv4_address_for_NGU = NULL;

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

configmodule_interface_t *uniqCfg = NULL;

/* Load a configuration file in place of the current one. The libconfig backend has
 * a single instance, so one file at a time; the config module uses argv until it ends. */
static void switch_config(int argc, char **argv)
{
  static char **cfg_argv = NULL;
  end_configmodule(uniqCfg);
  /* load_configmodule() reads the current module through config_get_if() */
  uniqCfg = NULL;
  free(cfg_argv);
  cfg_argv = argv;
  uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  if (uniqCfg == NULL) {
    fprintf(stderr, "[SOFTMODEM] Error, configuration module init failed\n");
    exit(1);
  }
}

/* Looks up the Xn/SCTP config cached for this gNB instance at startup (each
 * instance's config file is loaded in turn with switch_config(), so it is no
 * longer available via config_get_if() by the time Xn registration runs). */
static xnap_net_config_t xn_net_config_from_cache(uint32_t gnb_idx)
{
  AssertFatal(xn_net_config_tab != NULL && gnb_idx < nb_gnb_inst,
              "No cached Xn network config for gNB instance %u (nb_gnb_inst=%u)\n",
              gnb_idx, nb_gnb_inst);
  return xn_net_config_tab[gnb_idx];
}

void gNB_app_register_xn(instance_t instance, ngap_register_gnb_cnf_t *cnf)
{
  MessageDef *msg_p = itti_alloc_new_message(TASK_GNB_APP, 0, XNAP_REGISTER_GNB_REQ);
  xnap_register_gnb_req_t *msg = &XNAP_REGISTER_GNB_REQ(msg_p);
  msg->net_config = xn_net_config_from_cache(instance);
  msg->ng_setup_info = read_ng_setup_info(cnf, instance);
  LOG_I(GNB_APP,
        "[gNB %ld] Sending XNAP_REGISTER_GNB_REQ for gNB ID %u with %d candidate(s)\n",
        instance,
        msg->ng_setup_info.gNB_id,
        msg->net_config.nb_of_candidate_gNBs);
  itti_send_msg_to_task(TASK_XNAP, GNB_MODULE_ID_TO_INSTANCE(instance), msg_p);
}

void *gNB_app_task(void *args_p)
{
  UNUSED(args_p);
  MessageDef *msg_p = NULL;
  const char *msg_name = NULL;
  instance_t instance;
  int result;
  /* for no gcc warnings */
  (void)instance;

  itti_mark_task_ready(TASK_GNB_APP);
  do {
    itti_receive_msg(TASK_GNB_APP, &msg_p);
    msg_name = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);
    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(GNB_APP, " *** Exiting GNB_APP thread\n");
        itti_exit_task();
        break;

      case NGAP_REGISTER_GNB_CNF:
        LOG_I(GNB_APP,
              "[gNB %ld] Received %s: associated AMF %d\n",
              instance,
              msg_name,
              NGAP_REGISTER_GNB_CNF(msg_p).nb_amf);
        LOG_I(GNB_APP, "[gNB %ld] Starting Xn procedure sequence\n", instance);
        gNB_app_register_xn(instance, &NGAP_REGISTER_GNB_CNF(msg_p));
        if (!xn_setup_only)
          xn_ho_sim_ng_setup_done(instance);
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

void *rrc_gnb_task(void *args_p)
{
  UNUSED(args_p);
  MessageDef *msg_p;
  instance_t instance;
  int result;
  static uint32_t nb_done = 0;

  itti_mark_task_ready(TASK_RRC_GNB);
  LOG_I(NR_RRC, "Entering main loop of NR_RRC message task\n");

  while (1) {
    itti_receive_msg(TASK_RRC_GNB, &msg_p);
    const char *msg_name_p = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);
    LOG_D(NR_RRC,
          "RRC GNB Task Received %s for instance %ld from task %s\n",
          ITTI_MSG_NAME(msg_p),
          instance,
          ITTI_MSG_ORIGIN_NAME(msg_p));

    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(NR_RRC, " *** Exiting NR_RRC thread\n");
        itti_exit_task();
        break;

      case XNAP_SETUP_IND:
        nb_done++;
        LOG_I(NR_RRC, "[gNB %ld] Received Xn setup indication %u / %u for %u instance(s))\n", instance, nb_done, nb_gnb_inst * (nb_gnb_inst -1), nb_gnb_inst);
        if (nb_done >= (nb_gnb_inst * (nb_gnb_inst - 1))) {
          LOG_I(NR_RRC, "Xn setup flow completed successfully for all %u gNB instance(s)!\n", nb_gnb_inst);
          exit(EXIT_SUCCESS);
        }
        break;

      case XNAP_PEER_SHUTDOWN_IND:
        nb_done++;
        LOG_I(NR_RRC,
              "[gNB %ld] Received Xn peer shutdown indications %u of %u instance(s) done)\n",
              instance, nb_done, nb_gnb_inst);
        if (nb_done >= nb_gnb_inst) {
          LOG_I(NR_RRC, "Xn setup flow completed successfully for all %u gNB instance(s)!\n", nb_gnb_inst);
          exit(EXIT_SUCCESS);
        }
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

/* value of option name, or def */
static char *get_opt(int argc, char **argv, const char *name, char *def)
{
  for (int i = 1; i < argc - 1; i++)
    if (strcmp(argv[i], name) == 0)
      return argv[i + 1];
  return def;
}

static int extract_conf_files(int argc, char **argv, char **Ofiles, int max_files)
{
  int n = 0;
  for (int i = 0; i < argc - 1; i++) {
    if (strlen(argv[i]) >= 2 && argv[i][1] == 'O') {
      if (n >= max_files) {
        fprintf(stderr, "[XN-SIM] warning: more than %d -O files given, ignoring \"%s\"\n", max_files, argv[i + 1]);
        continue;
      }
      Ofiles[n++] = argv[i + 1];
    }
  }
  return n;
}

int main(int argc, char **argv)
{
  /* --- collect every -O file from the real command line -------------- */
  char *Ofiles[8];
  int nOfiles = extract_conf_files(argc, argv, Ofiles, sizeofArray(Ofiles));
  nb_gnb_inst = (nOfiles > 0) ? nOfiles : 1;
  for (int i = 1; i < argc; i++)
    xn_setup_only |= strcmp(argv[i], "--xn-setup-only") == 0;
  if (nOfiles < 2) {
    fprintf(stderr, "Testing Xn setup/handover requires at least 2 gNBs\n");
    fprintf(stderr,
            "usage: %s -O <gNB1 config> -O <gNB2 config> [-O <gNB3 config> ...] [--ue-conf <UE config>] [--xn-setup-only]\n"
            "       [config options, e.g. --log_config.xnap_log_level debug]\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  gnb_id_tab = calloc_or_fail(nb_gnb_inst, sizeof(*gnb_id_tab));
  tac_tab = calloc_or_fail(nb_gnb_inst, sizeof(*tac_tab));
  mcc_tab = calloc_or_fail(nb_gnb_inst, sizeof(*mcc_tab));
  mnc_tab = calloc_or_fail(nb_gnb_inst, sizeof(*mnc_tab));
  mnc_len_tab = calloc_or_fail(nb_gnb_inst, sizeof(*mnc_len_tab));
  xn_net_config_tab = calloc_or_fail(nb_gnb_inst, sizeof(*xn_net_config_tab));

  /* the first config file and the other options, e.g. --log_config.xnap_log_level debug */
  char **synth_argv = calloc_or_fail(argc + 2, sizeof(*synth_argv));
  int synth_argc = 0;
  synth_argv[synth_argc++] = argv[0];
  synth_argv[synth_argc++] = "-O";
  synth_argv[synth_argc++] = Ofiles[0];
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--xn-setup-only") == 0)
      continue;
    if ((strlen(argv[i]) >= 2 && argv[i][1] == 'O') || strcmp(argv[i], "--ue-conf") == 0) {
      i++;
      continue;
    }
    synth_argv[synth_argc++] = argv[i];
  }

  switch_config(synth_argc, synth_argv);

  logInit();
  set_softmodem_sighandler();

  LOG_I(GNB_APP, "%u gNB instance(s) / config file(s) for the Xn simulator\n", nb_gnb_inst);

  /* Initialize ITTI tasks before we start sending any registration
   * message, exactly as in the single-instance version. */
  itti_init(TASK_MAX, tasks_info);
  int rc;
  rc = itti_create_task(TASK_SCTP, sctp_eNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for SCTP failed\n");
  rc = itti_create_task(TASK_NGAP, ngap_gNB_task, NULL);
  AssertFatal(rc >= 0, "Create task for NGAP failed\n");
  rc = itti_create_task(TASK_XNAP, xnap_task, NULL);
  AssertFatal(rc >= 0, "Create task for XNAP failed\n");
  rc = itti_create_task(TASK_RRC_GNB, xn_setup_only ? rrc_gnb_task : xn_ho_rrc_gnb_task, NULL);
  AssertFatal(rc >= 0, "Create task for RRC failed\n");
  rc = itti_create_task(TASK_GNB_APP, gNB_app_task, NULL);
  AssertFatal(rc >= 0, "Create task for GNB APP failed\n");

  /* --- load and extract each file in turn - fully independent, never
   * merged, one file loaded at a time. File 0's config is already loaded
   * above, so this loop does NOT reload it. */
  for (uint32_t i = 0; i < nb_gnb_inst; i++) {
    if (i > 0) {
      char **this_argv = calloc_or_fail(3, sizeof(*this_argv));
      this_argv[0] = argv[0];
      this_argv[1] = "-O";
      this_argv[2] = Ofiles[i];
      switch_config(3, this_argv);
    }

    MessageDef *msg_p = itti_alloc_new_message(TASK_GNB_APP, 0, NGAP_REGISTER_GNB_REQ);
    RCconfig_NR_NG(msg_p, 0);

    gnb_id_tab[i] = NGAP_REGISTER_GNB_REQ(msg_p).gNB_id;
    tac_tab[i] = NGAP_REGISTER_GNB_REQ(msg_p).tac;
    mcc_tab[i] = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mcc;
    mnc_tab[i] = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mnc;
    mnc_len_tab[i] = NGAP_REGISTER_GNB_REQ(msg_p).plmn[0].plmn.mnc_digit_length;
    xn_net_config_tab[i] = read_ip_config_xn(0);
    uint64_t cell_id = 0;
    paramdef_t cell_id_param[] = {{.optname = GNB_CONFIG_STRING_NRCELLID, .u64ptr = &cell_id, .type = TYPE_UINT64}};
    config_get(uniqCfg, cell_id_param, 1, "gNBs.[0]");
    char *ngu_ip = NULL;
    paramdef_t ngu_param[] = {{.optname = GNB_CONFIG_STRING_GNB_IPV4_ADDR_FOR_NGU, .strptr = &ngu_ip, .type = TYPE_STRING}};
    config_get(uniqCfg, ngu_param, 1, "gNBs.[0]." GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
    AssertFatal(ngu_ip != NULL, "no %s in %s\n", GNB_CONFIG_STRING_GNB_IPV4_ADDR_FOR_NGU, Ofiles[i]);

    if (i == 0) {
      gnb_id = gnb_id_tab[0];
      tac = tac_tab[0];
      mcc = mcc_tab[0];
      mnc = mnc_tab[0];
      mnc_len = mnc_len_tab[0];
    }

    LOG_I(GNB_APP,
          "Sending NGAP_REGISTER_GNB_REQ to TASK_NGAP for gNB instance %u (gNB ID %u, file %s)\n",
          i,
          gnb_id_tab[i],
          (nOfiles > 0) ? Ofiles[i] : "(default)");
    if (xn_setup_only)
      itti_send_msg_to_task(TASK_NGAP, i, msg_p);
    else
      xn_ho_sim_add_gnb(i, msg_p, cell_id, ngu_ip);
  }

  if (!xn_setup_only) {
    /* the UE stub: NR UE NAS, its identity (uicc0) from its own configuration file */
    char *ue_conf = get_opt(argc, argv, "--ue-conf", OAI_SOURCE_DIR "/tests/nr-ue-nas-simulator/test.conf");
    char **ue_argv = calloc_or_fail(4, sizeof(*ue_argv));
    ue_argv[0] = argv[0];
    ue_argv[1] = "-O";
    ue_argv[2] = ue_conf;
    switch_config(3, ue_argv);
    nas_init_nrue(1);
    rc = itti_create_task(TASK_NAS_NRUE, nas_nrue_task, NULL);
    AssertFatal(rc >= 0, "Create task for NAS UE failed\n");
    rc = itti_create_task(TASK_RRC_NRUE, xn_ho_rrc_ue_task, NULL);
    AssertFatal(rc >= 0, "Create task for RRC UE failed\n");
    xn_ho_sim_start();
  }

  printf("TYPE <CTRL-C> TO TERMINATE\n");
  itti_wait_tasks_end(NULL);
  logClean();
  printf("Bye.\n");
  return 0;
}
