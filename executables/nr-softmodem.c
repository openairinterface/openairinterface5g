/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */


#define _GNU_SOURCE             /* See feature_test_macros(7) */

#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#ifdef SMBV
#include "PHY/TOOLS/smbv.h"
unsigned short config_frames[4] = {2,9,11,13};
#endif
#include "common/utils/time_manager/time_manager.h"
#ifdef ENABLE_AERIAL
#include "nfapi/oai_integration/aerial/fapi_nvIPC.h"
#endif
#ifdef E2_AGENT
#include "openair2/E2AP/flexric/src/agent/e2_agent_api.h"
#include "openair2/E2AP/RAN_FUNCTION/init_ran_func.h"
#endif

#ifdef E3_AGENT
#include "openair2/E3AP/e3_agent.h"
#endif

#include "nr-softmodem.h"
#include <common/utils/assertions.h>
#include <openair2/GNB_APP/gnb_app.h>
#include <openair3/ocp-gtpu/gtp_itf.h>
#include <pthread.h>
#include <sched.h>
#include <simple_executable.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "NR_PHY_INTERFACE/NR_IF_Module.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/TOOLS/phy_scope_interface.h"
#include "PHY/defs_gNB.h"
#include "PHY/defs_nr_common.h"
#include "PHY/phy_vars.h"
#include "RRC/NR/nr_rrc_defs.h"
#include "RRC/NR/nr_rrc_proto.h"
#include "RRC_nr_paramsvalues.h"
#include "SIMULATION/TOOLS/sim.h"
#include "T.h"
#include "UTIL/OPT/opt.h"
#include "common/config/config_userapi.h"
#include "common/ngran_types.h"
#include "common/oai_version.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"
#include "e1ap_messages_types.h"
#include "executables/softmodem-common.h"
#include "gnb_config.h"
#include "gnb_paramdef.h"
#include "intertask_interface.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "nfapi_nr_interface_scf.h"
#include "ngap_gNB.h"
#include "nr-softmodem-common.h"
#include "openair2/E1AP/e1ap_common.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "s1ap_eNB.h"
#include "sctp_eNB_task.h"
#include "system.h"
#include "time_meas.h"
#include "utils.h"
#include "openair2/X2AP/x2ap_eNB.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "openair2/SDAP/nr_sdap/nr_sdap.h"
#include "openair3/NRPPA/nrppa_gNB.h"

RAN_CONTEXT_t RC;
pthread_cond_t nfapi_sync_cond;
pthread_mutex_t nfapi_sync_mutex;
int nfapi_sync_var=-1; //!< protected by mutex \ref nfapi_sync_mutex
THREAD_STRUCT thread_struct;
pthread_cond_t sync_cond;
pthread_mutex_t sync_mutex;
int sync_var=-1; //!< protected by mutex \ref sync_mutex.
int config_sync_var=-1;
int oai_exit = 0;

uint64_t downlink_frequency[MAX_NUM_CCs][4];
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
char *uecap_file;

/*------------------------------------------------------------------------*/

unsigned int build_rflocal(int txi, int txq, int rxi, int rxq) {
  return (txi + (txq<<6) + (rxi<<12) + (rxq<<18));
}
unsigned int build_rfdc(int dcoff_i_rxfe, int dcoff_q_rxfe) {
  return (dcoff_i_rxfe + (dcoff_q_rxfe<<8));
}


void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  int ru_id;

  if (s != NULL) {
    printf("%s:%d %s() Exiting OAI softmodem: %s\n",file,line, function, s);
  }

  if (RC.ru == NULL)
    exit(-1); // likely init not completed, prevent crash or hang, exit now...

  // Signal worker threads (ru_thread, L1) to stop before tearing down the radio
  oai_exit = 1;

  for (ru_id=0; ru_id<RC.nb_RU; ru_id++) {
    if (RC.ru[ru_id] == NULL) {
      continue;
    }
    if (RC.ru[ru_id]->ifdevice.trx_stop_func) {
      RC.ru[ru_id]->ifdevice.trx_stop_func(&RC.ru[ru_id]->ifdevice);
      RC.ru[ru_id]->ifdevice.trx_stop_func = NULL;
    }
    if (RC.ru[ru_id]->rfdevice.trx_stop_func) {
      RC.ru[ru_id]->rfdevice.trx_stop_func(&RC.ru[ru_id]->rfdevice);
      RC.ru[ru_id]->rfdevice.trx_stop_func = NULL;
    }
    if (RC.ru[ru_id]->rfdevice.trx_end_func) {
      if (RC.ru[ru_id]->rfdevice.trx_get_stats_func) {
        RC.ru[ru_id]->rfdevice.trx_get_stats_func(&RC.ru[ru_id]->rfdevice);
        RC.ru[ru_id]->rfdevice.trx_get_stats_func = NULL;
      }
      RC.ru[ru_id]->rfdevice.trx_end_func(&RC.ru[ru_id]->rfdevice);
      RC.ru[ru_id]->rfdevice.trx_end_func = NULL;
    }

    if (RC.ru[ru_id] && RC.ru[ru_id]->ifdevice.trx_end_func) {
      if (RC.ru[ru_id]->ifdevice.trx_get_stats_func) {
        RC.ru[ru_id]->ifdevice.trx_get_stats_func(&RC.ru[ru_id]->ifdevice);
        RC.ru[ru_id]->ifdevice.trx_get_stats_func = NULL;
      }
      RC.ru[ru_id]->ifdevice.trx_end_func(&RC.ru[ru_id]->ifdevice);
      RC.ru[ru_id]->ifdevice.trx_end_func = NULL;
    }
  }

  if (assert) {
    abort();
  } else {
    sleep(1); // allow nr-softmodem threads to exit first
    exit(EXIT_SUCCESS);
  }
}

static int create_gNB_tasks(ngran_node_t node_type, configmodule_interface_t *cfg)
{
  uint32_t                        gnb_nb = RC.nb_nr_inst; 
  uint32_t                        gnb_id_start = 0;
  uint32_t                        gnb_id_end = gnb_id_start + gnb_nb;
  LOG_D(GNB_APP, "%s(gnb_nb:%d)\n", __FUNCTION__, gnb_nb);
  itti_wait_ready(1);
  LOG_D(PHY, "%s() Task ready initialize structures\n", __FUNCTION__);

#ifdef ENABLE_AERIAL
  AssertFatal(NFAPI_MODE == NFAPI_MODE_AERIAL,"Can only be run with '--nfapi AERIAL' when compiled with AERIAL support, if you want to run other (n)FAPI modes, please run ./build_oai without -w AERIAL");
#endif

  RCconfig_verify(cfg, node_type);

  if (RC.nb_nr_macrlc_inst > 0)
    RCconfig_nr_macrlc(cfg);

  if (RC.nb_nr_L1_inst > 0) {
    int ret = l1_north_init_gNB();
    AssertFatal(ret == 0, "could not initialize L1 north interface\n");
  }

  AssertFatal (gnb_nb <= RC.nb_nr_inst,
               "Number of gNB is greater than gNB defined in configuration file (%d/%d)!",
               gnb_nb, RC.nb_nr_inst);

  LOG_D(GNB_APP, "Allocating gNB_RRC_INST\n");
  RC.nrrrc = calloc(1, sizeof(*RC.nrrrc));
  RC.nrrrc[0] = RCconfig_NRRRC();

  if (node_type != ngran_gNB_DU) {
    // we start pdcp in both cuup (for drb) and cucp (for srb)
    nr_pdcp_layer_init();
  }

  if (get_softmodem_params()->nsa) { //&& !NODE_IS_DU(node_type)
	  LOG_I(X2AP, "X2AP enabled \n");
	  __attribute__((unused)) uint32_t x2_register_gnb_pending = gNB_app_register_x2 (gnb_id_start, gnb_id_end);
  }

  /* For the CU case the gNB registration with the AMF might have to take place after the F1 setup, as the PLMN info
     * can originate from the DU. Add check on whether x2ap is enabled to account for ENDC NSA scenario.*/
  if (IS_SA_MODE(get_softmodem_params()) && !NODE_IS_DU(node_type)) {
    /* Try to register each gNB */
    //registered_gnb = 0;
    __attribute__((unused)) uint32_t register_gnb_pending = gNB_app_register (gnb_id_start, gnb_id_end);
  }

  if (gnb_nb > 0) {
    if(itti_create_task(TASK_SCTP, sctp_eNB_task, NULL) < 0) {
      LOG_E(SCTP, "Create task for SCTP failed\n");
      return -1;
    }

    if (get_softmodem_params()->nsa) {
      if(itti_create_task(TASK_X2AP, x2ap_task, NULL) < 0) {
        LOG_E(X2AP, "Create task for X2AP failed\n");
      }
    } else {
      LOG_I(X2AP, "X2AP is disabled.\n");
    }
  }

  if (IS_SA_MODE(get_softmodem_params()) && !NODE_IS_DU(node_type)) {

    char*             gnb_ipv4_address_for_NGU      = NULL;
    uint32_t          gnb_port_for_NGU              = 0;
    char*             gnb_ipv4_address_for_S1U      = NULL;
    uint32_t          gnb_port_for_S1U              = 0;
    paramdef_t NETParams[]  =  GNBNETPARAMS_DESC;
    char aprefix[MAX_OPTNAME_SIZE*2 + 8];
    sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,0,GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
    config_get(cfg, NETParams, sizeofArray(NETParams), aprefix);

    if (gnb_nb > 0) {
      if (itti_create_task (TASK_NGAP, ngap_gNB_task, NULL) < 0) {
        LOG_E(NGAP, "Create task for NGAP failed\n");
        return -1;
      }

      if (itti_create_task(TASK_NRPPA, nrppa_gNB_task, NULL) < 0) {
        LOG_E(NGAP, "Create task for NRPPA failed\n");
        return -1;
      }
    }
  }

  if (gnb_nb > 0) {
    if (!NODE_IS_DU(node_type)) {
      if (itti_create_task (TASK_RRC_GNB, rrc_gnb_task, NULL) < 0) {
        LOG_E(NR_RRC, "Create task for NR RRC gNB failed\n");
        return -1;
      }
    }

    // E1AP initialisation, whether the node is a CU or has integrated CU
    if (node_type == ngran_gNB_CU || node_type == ngran_gNB) {
      MessageDef *msg = RCconfig_NR_CU_E1(NULL);
      instance_t inst = 0;
      createE1inst(UPtype, inst, E1AP_REGISTER_REQ(msg).gnb_id, &E1AP_REGISTER_REQ(msg).net_config, NULL);
      cuup_init_n3(inst);
      RC.nrrrc[gnb_id_start]->e1_inst = inst; // stupid instance !!!*/

      /* send E1 Setup Request to RRC */
      MessageDef *new_msg = itti_alloc_new_message(TASK_GNB_APP, 0, E1AP_SETUP_REQ);
      E1AP_SETUP_REQ(new_msg) = E1AP_REGISTER_REQ(msg).setup_req;
      new_msg->ittiMsgHeader.originInstance = -1; /* meaning, it is local */
      itti_send_msg_to_task(TASK_RRC_GNB, 0 /*unused by callee*/, new_msg);
      itti_free(TASK_UNKNOWN, msg);
    }

    if (itti_create_task (TASK_GNB_APP, gNB_app_task, NULL) < 0) {
      LOG_E(GNB_APP, "Create task for gNB APP failed\n");
      return -1;
    }

    //Use check on x2ap to consider the NSA scenario 
    if((is_x2ap_enabled() || IS_SA_MODE(get_softmodem_params())) && (node_type != ngran_gNB_CUCP)) {
      if (itti_create_task (TASK_GTPV1_U, &gtpv1uTask, NULL) < 0) {
        LOG_E(GTPU, "Create task for GTPV1U failed\n");
        return -1;
      }
    }
  }

  return 0;
}

static void get_options(configmodule_interface_t *cfg)
{
  paramdef_t cmdline_params[] = CMDLINE_PARAMS_DESC_GNB ;
  CONFIG_SETRTFLAG(CONFIG_NOEXITONHELP);
  IS_SOFTMODEM_GNB = true;
  get_common_options(cfg);
  config_process_cmdline(cfg, cmdline_params, sizeofArray(cmdline_params), NULL);
  CONFIG_CLEARRTFLAG(CONFIG_NOEXITONHELP);
}

void wait_RUs(void) {
  LOG_D(PHY, "Waiting for RUs to be configured ... RC.ru_mask:%02lx\n", RC.ru_mask);
  // wait for all RUs to be configured over fronthaul
  pthread_mutex_lock(&RC.ru_mutex);

  while (RC.ru_mask>0) {
    pthread_cond_wait(&RC.ru_cond,&RC.ru_mutex);
  }

  pthread_mutex_unlock(&RC.ru_mutex);
  LOG_D(PHY, "RUs configured\n");
}

void wait_gNBs(void) {
  int i;
  int waiting=1;

  while (waiting==1) {
    LOG_D(GNB_APP, "Waiting for gNB L1 instances to all get configured ... sleeping 50ms (nb_nr_sL1_inst %d)\n", RC.nb_nr_L1_inst);
    usleep(50*1000);
    waiting=0;

    for (i=0; i<RC.nb_nr_L1_inst; i++) {
      if (RC.gNB[i]->configured==0) {
        waiting=1;
        break;
      }
    }
  }

  LOG_D(GNB_APP, "gNB L1 are configured\n");
}

/*
 * helper function to terminate a certain ITTI task
 */
void terminate_task(task_id_t task_id, module_id_t mod_id) {
  LOG_I(GNB_APP, "sending TERMINATE_MESSAGE to task %s (%d)\n", itti_get_task_name(task_id), task_id);
  MessageDef *msg;
  msg = itti_alloc_new_message (TASK_ENB_APP, 0, TERMINATE_MESSAGE);
  itti_send_msg_to_task (task_id, ENB_MODULE_ID_TO_INSTANCE(mod_id), msg);
}

int stop_L1(module_id_t gnb_id)
{
  RU_t *ru = RC.ru[gnb_id];
  if (!ru) {
    LOG_W(GNB_APP, "no RU configured\n");
    return -1;
  }

  if (!RC.gNB[gnb_id]->configured) {
    LOG_W(GNB_APP, "L1 already stopped\n");
    return -1;
  }

  LOG_I(GNB_APP, "stopping nr-softmodem\n");
  oai_exit = 1;

  /* these tasks/layers need to pick up new configuration */
  if (RC.nb_nr_L1_inst > 0)
    stop_gNB(RC.nb_nr_L1_inst);

  /* stop trx devices, multiple carrier currently not supported by RU */
  if (ru->rfdevice.trx_get_stats_func) {
    ru->rfdevice.trx_get_stats_func(&ru->rfdevice);
  }
  if (ru->rfdevice.trx_stop_func) {
    ru->rfdevice.trx_stop_func(&ru->rfdevice);
    LOG_I(GNB_APP, "turned off RU rfdevice\n");
  }

  if (ru->ifdevice.trx_get_stats_func) {
    ru->ifdevice.trx_get_stats_func(&ru->rfdevice);
  }
  if (ru->ifdevice.trx_stop_func) {
    ru->ifdevice.trx_stop_func(&ru->ifdevice);
    LOG_I(GNB_APP, "turned off RU ifdevice\n");
  }

  if (RC.nb_RU > 0)
    stop_RU(RC.nb_RU);

  /* release memory used by the RU/gNB threads (incomplete), after all
   * threads have been stopped (they partially use the same memory) */
  for (int inst = 0; inst < RC.nb_RU; inst++) {
    nr_phy_free_RU(RC.ru[inst]);
  }

  for (int inst = 0; inst < RC.nb_nr_L1_inst; inst++) {
    phy_free_nr_gNB(RC.gNB[inst]);
  }

  RC.gNB[gnb_id]->configured = 0;
  return 0;
}

/*
 * Restart the nr-softmodem after it has been soft-stopped with stop_L1L2()
 */
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"
int start_L1L2(module_id_t gnb_id, nr_cell_sched_t *cell)
{
  LOG_I(GNB_APP, "starting nr-softmodem\n");
  /* block threads */
  oai_exit = 0;
  sync_var = -1;

  /* update config */
  gNB_MAC_INST *mac = RC.nrmac[gnb_id];
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  nr_mac_cell_config_scc(cell, scc, &cell->radio_config);
  nr_mac_send_config_req(mac, &cell->config);

  NR_BCCH_BCH_Message_t *mib = cell->common_channels.mib;
  const NR_BCCH_DL_SCH_Message_t *sib1 = cell->common_channels.sib1;
  f1ap_setup_req_t *sr = mac->f1_config.setup_req;
  DevAssert(sr->num_cells_available == 1);
  f1ap_served_cell_info_t *info = &sr->cell[0].info;
  DevAssert(info->mode == F1AP_MODE_TDD);
  /* update existing config in F1 Setup request structures */
  DevAssert(scc->tdd_UL_DL_ConfigurationCommon != NULL);
  info->tdd = read_tdd_config(scc); /* updates radio config */
  prepare_du_configuration_update(mac, info, mib, sib1);

  init_NR_RU(config_get_if(), NULL);

  start_NR_RU();
  wait_RUs();
  init_eNB_afterRU();
  LOG_I(GNB_APP, "Sending sync to all threads\n");
  pthread_mutex_lock(&sync_mutex);
  sync_var=0;
  pthread_cond_broadcast(&sync_cond);
  pthread_mutex_unlock(&sync_mutex);
  return 0;
}

static void wait_nfapi_init()
{
  pthread_mutex_lock( &nfapi_sync_mutex );

  while (nfapi_sync_var<0)
    pthread_cond_wait( &nfapi_sync_cond, &nfapi_sync_mutex );

  pthread_mutex_unlock(&nfapi_sync_mutex);
}

#ifdef E2_AGENT
static void initialize_agent(ngran_node_t node_type, e2_agent_args_t oai_args)
{
  AssertFatal(oai_args.sm_dir != NULL , "Please, specify the directory where the SMs are located in the config file, i.e., add in config file the next line: e2_agent = {near_ric_ip_addr = \"127.0.0.1\"; sm_dir = \"/usr/local/lib/flexric/\");} ");
  AssertFatal(oai_args.ip != NULL , "Please, specify the IP address of the nearRT-RIC in the config file, i.e., e2_agent = {near_ric_ip_addr = \"127.0.0.1\"; sm_dir = \"/usr/local/lib/flexric/\"");

  printf("After RCconfig_NR_E2agent %s %s \n",oai_args.sm_dir, oai_args.ip  );

  fr_args_t args = {0};
  memcpy(args.ip, oai_args.ip, FR_IP_ADDRESS_LEN);
  memcpy(args.libs_dir, oai_args.sm_dir, FR_CONF_FILE_LEN);

  sleep(1);
  const gNB_RRC_INST* rrc = RC.nrrrc[0];
  assert(rrc != NULL && "rrc cannot be NULL");

  const int mcc = rrc->configuration.plmn[0].mcc;
  const int mnc = rrc->configuration.plmn[0].mnc;
  const int mnc_digit_len = rrc->configuration.plmn[0].mnc_digit_length;
  // const ngran_node_t node_type = rrc->node_type;
  int nb_id = 0;
  int cu_du_id = 0;
  if (node_type == ngran_gNB) {
    nb_id = rrc->node_id;
  } else if (node_type == ngran_gNB_DU) {
    const gNB_MAC_INST* mac = RC.nrmac[0];
    AssertFatal(mac, "MAC not initialized\n");
    cu_du_id = mac->f1_config.gnb_id;
    nb_id = mac->f1_config.setup_req->gNB_DU_id;
  } else if (node_type == ngran_gNB_CU || node_type == ngran_gNB_CUCP) {
    // agent buggy: the CU has no second ID, it is the CU-UP ID
    // however, that is not a problem her for us, so put the same ID twice
    nb_id = rrc->node_id;
    cu_du_id = rrc->node_id;
  } else {
    LOG_E(NR_RRC, "not supported ran type detect\n");
  }

  printf("[E2 NODE]: mcc = %d mnc = %d mnc_digit = %d nb_id = %d \n", mcc, mnc, mnc_digit_len, nb_id);

  printf("[E2 NODE]: Args %s %s \n", args.ip, args.libs_dir);

  sm_io_ag_ran_t io = init_ran_func_ag();
  init_agent_api(mcc, mnc, mnc_digit_len, nb_id, cu_du_id, node_type, io, &args);
}
#endif

void init_eNB_afterRU(void);
configmodule_interface_t *uniqCfg = NULL;
int main( int argc, char **argv ) {
  int ru_id, CC_id = 0;
  start_background_system();

  ///static configuration for NR at the moment
  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL || CONFIG_ISFLAGSET(CONFIG_ABORT)) {
    exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  }

  set_softmodem_sighandler();
#ifdef DEBUG_CONSOLE
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif
  logInit();
  lock_memory_to_ram();
  get_options(uniqCfg);

  if (!has_cap_sys_nice())
    LOG_W(UTIL,
          "no SYS_NICE capability: cannot set thread priority and affinity, consider running with sudo for optimum performance\n");

  softmodem_verify_mode(get_softmodem_params());

//////////////////////////////////
//// Init the E3 Agent
#ifdef E3_AGENT
  printf("Init E3 Agent\n");
  e3_init();
#endif // E3_AGENT

#if T_TRACER
  T_Config_Init();
#endif
  set_taus_seed (0);

  itti_init(TASK_MAX, tasks_info);
  // initialize mscgen log after ITTI
  init_opt();

  // strdup to put the sring in the core file for post mortem identification
  char *pckg = strdup(OAI_PACKAGE_VERSION);
  LOG_I(HW, "Version: %s\n", pckg);

  // Init RAN context
  if (!(CONFIG_ISFLAGSET(CONFIG_ABORT)))
    NRRCConfig();

  if (RC.nb_nr_L1_inst > 0) {
    // Initialize gNB structure in RAN context
    init_gNB();
    // Initialize L1
    RCconfig_NR_L1();
    // Initialize Positioning Reference Signal configuration
    if(NFAPI_MODE != NFAPI_MODE_PNF && NFAPI_MODE != NFAPI_MODE_AERIAL)
      RCconfig_nr_prs();
  }

  // don't create if node doesn't connect to RRC/S1/GTP
  const ngran_node_t node_type = get_node_type();

  if (NFAPI_MODE != NFAPI_MODE_PNF) {
    int ret = create_gNB_tasks(node_type, uniqCfg);
    AssertFatal(ret == 0, "cannot create ITTI tasks\n");
  }

  pthread_cond_init(&sync_cond,NULL);
  pthread_mutex_init(&sync_mutex, NULL);
  usleep(1000);

  if (NFAPI_MODE && NFAPI_MODE != NFAPI_MODE_AERIAL) {
    pthread_cond_init(&sync_cond,NULL);
    pthread_mutex_init(&sync_mutex, NULL);
  }

  // start time manager with some reasonable default for the running mode
  // (may be overwritten in configuration file or command line)
  void nr_pdcp_ms_tick(void);
  void x2ap_ms_tick();
  void nr_rlc_ms_tick(void);
  time_manager_tick_function_t tick_functions[3];
  int tick_functions_count = 0;
  if (NODE_IS_MONOLITHIC(node_type)) {
    /* monolithic */
    tick_functions[tick_functions_count++] = nr_pdcp_ms_tick;
    tick_functions[tick_functions_count++] = nr_rlc_ms_tick;
    /* x2ap is enabled when in NSA mode */
    if (get_softmodem_params()->nsa)
      tick_functions[tick_functions_count++] = x2ap_ms_tick;
  } else if (NODE_IS_CU(node_type)) {
     /* CU */
    tick_functions[tick_functions_count++] = nr_pdcp_ms_tick;
    /* x2ap is enabled when in NSA mode */
    if (get_softmodem_params()->nsa)
      tick_functions[tick_functions_count++] = x2ap_ms_tick;
  } else {
     /* DU */
    tick_functions[tick_functions_count++] = nr_rlc_ms_tick;
  }
  time_manager_start(tick_functions, tick_functions_count,
                     // iq_samples time source for monolithic/du with rfsim,
                     // realtime time source for other cases
                     IS_SOFTMODEM_RFSIM
                     && (NODE_IS_MONOLITHIC(node_type) || NODE_IS_DU(node_type))
                         ? TIME_SOURCE_IQ_SAMPLES
                         : TIME_SOURCE_REALTIME);

  // start the main threads

  wait_gNBs();
  int sl_ahead = NFAPI_MODE == NFAPI_MODE_AERIAL ? 0 : 6;
  if (RC.nb_RU >0) {
    init_NR_RU(uniqCfg, get_softmodem_params()->rf_config_file);

    for (ru_id=0; ru_id<RC.nb_RU; ru_id++) {
      RC.ru[ru_id]->rf_map.card=0;
      RC.ru[ru_id]->rf_map.chain = CC_id;
      if (ru_id==0) sl_ahead = RC.ru[ru_id]->sl_ahead;	
      else AssertFatal(RC.ru[ru_id]->sl_ahead != RC.ru[0]->sl_ahead,"RU %d has different sl_ahead %d than RU 0 %d\n",ru_id,RC.ru[ru_id]->sl_ahead,RC.ru[0]->sl_ahead);
    }
    
  }

  if (NFAPI_MODE != NFAPI_MODE_PNF)
    config_sync_var = 0;

#ifdef E2_AGENT

//////////////////////////////////
//////////////////////////////////
//// Init the E2 Agent

  // OAI Wrapper 
  e2_agent_args_t oai_args = RCconfig_NR_E2agent();

  if (oai_args.enabled) {
    initialize_agent(node_type, oai_args);
  }

#endif // E2_AGENT

  // wait for F1 Setup Response before starting L1 for real
  if (NFAPI_MODE != NFAPI_MODE_PNF && (NODE_IS_DU(node_type) || NODE_IS_MONOLITHIC(node_type)))
    wait_f1_setup_response();

  if (RC.nb_RU > 0)
    start_NR_RU();

  if (NFAPI_MODE == NFAPI_MODE_PNF) {
    wait_RUs();
    config_sync_var = 0;
  }

#ifdef ENABLE_AERIAL
  gNB_MAC_INST *nrmac = RC.nrmac[0];
  nvIPC_Init(nrmac->nvipc_params_s);
#endif

  for (int idx = 0; idx < RC.nb_nr_L1_inst; idx++)
    RC.gNB[idx]->if_inst->sl_ahead = sl_ahead;

  if (NFAPI_MODE==NFAPI_MODE_PNF) {
    wait_nfapi_init();
  }

  if (IS_SOFTMODEM_IMSCOPE_ENABLED || IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED) {
    sleep(1);
    scopeParms_t p;
    p.argc = &argc;
    p.argv = argv;
    p.gNB = RC.gNB[0];
    p.ru = RC.ru[0];
    if (IS_SOFTMODEM_IMSCOPE_ENABLED) {
      load_softscope("im", &p);
    }
    AssertFatal(!(IS_SOFTMODEM_IMSCOPE_ENABLED && IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED),
                "Data recoding and ImScope cannot be enabled at the same time\n");
    if (IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED) {
      load_module_shlib("imscope_record", NULL, 0, &p);
    }
  }

  if (RC.nb_nr_L1_inst > 0) {
    wait_RUs();
    // once all RUs are ready initialize the rest of the gNBs ((dependence on final RU parameters after configuration)

    if (IS_SOFTMODEM_DOSCOPE) {
      sleep(1);
      scopeParms_t p;
      p.argc = &argc;
      p.argv = argv;
      p.gNB = RC.gNB[0];
      p.ru = RC.ru[0];
      if (IS_SOFTMODEM_DOSCOPE) {
        load_softscope("nr", &p);
      }
    }

    if (NFAPI_MODE != NFAPI_MODE_PNF && NFAPI_MODE != NFAPI_MODE_VNF && NFAPI_MODE != NFAPI_MODE_AERIAL) {
      init_eNB_afterRU();
    }

    // connect the TX/RX buffers
    pthread_mutex_lock(&sync_mutex);
    sync_var=0;
    pthread_cond_broadcast(&sync_cond);
    pthread_mutex_unlock(&sync_mutex);
  }

  // wait for end of program
  printf("TYPE <CTRL-C> TO TERMINATE\n");
  // Sleep a while before checking all parameters have been used
  // Some are used directly in external threads, asynchronously
  sleep(2);
  config_check_unknown_cmdlineopt(uniqCfg, CONFIG_CHECKALLSECTIONS);

  itti_wait_tasks_end(NULL);
  printf("Returned from ITTI signal handler\n");

  nfapi_stop_l1();

  if (RC.nb_nr_L1_inst > 0 || RC.nb_RU > 0)
    stop_L1(0);

  if (RC.nb_nr_macrlc_inst > 0) {
    DevAssert(RC.nb_nr_macrlc_inst == 1);
    mac_top_destroy_gNB(RC.nrmac[0]);
  }

  pthread_cond_destroy(&sync_cond);
  pthread_mutex_destroy(&sync_mutex);
  pthread_cond_destroy(&nfapi_sync_cond);
  pthread_mutex_destroy(&nfapi_sync_mutex);

  time_manager_finish();

#ifdef E3_AGENT
  printf("Destroy E3 Agent\n");
  e3_destroy();
#endif // E3_AGENT

  free(pckg);
  logClean();
  printf("Bye.\n");
  return 0;
}
