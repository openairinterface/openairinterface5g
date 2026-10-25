/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */


#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "T.h"
#include "common/oai_version.h"
#include "assertions.h"
#include "PHY/defs_nr_UE.h"
#include "SCHED_NR_UE/defs.h"
#include "common/ran_context.h"
#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#include "common/utils/nr/nr_common.h"
#include "radio/ETHERNET/if_defs.h"
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/phy_vars_nr_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"

#include "PHY_INTERFACE/phy_interface_vars.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "openair2/RRC/NR_UE/L2_interface_ue.h"
#include "openair1/PHY/phy_extern_nr_ue.h"

#ifdef SMBV
#include "PHY/TOOLS/smbv.h"
unsigned short config_frames[4] = {2,9,11,13};
#endif
#include "common/utils/LOG/log.h"
#include "common/utils/time_manager/time_manager.h"

#include "UTIL/OPT/opt.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"

#include "intertask_interface.h"

#include "PHY/INIT/nr_phy_init.h"
#include "system.h"
#include <openair2/RRC/NR_UE/rrc_proto.h>
#include <openair2/LAYER2/NR_MAC_UE/mac_defs.h>
#include <openair2/LAYER2/NR_MAC_UE/mac_proto.h>
#include <openair2/NR_UE_PHY_INTERFACE/NR_IF_Module.h>
#include <openair1/SCHED_NR_UE/fapi_nr_ue_l1.h>
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
/* Callbacks, globals and object handlers */

//#include "stats.h"
// current status is that every UE has a DL scope for a SINGLE eNB (eNB_id=0)
#include "PHY/TOOLS/phy_scope_interface.h"
#include "PHY/TOOLS/nr_phy_scope.h"
#include "executables/nr-ue-ru.h"
#include "executables/nr-ue-fd.h"
#include <executables/nr-uesoftmodem.h>
#include "executables/position_interface.h"
#include "executables/softmodem-common.h"
#include "executables/thread-common.h"

#include "nr_nas_msg.h"

THREAD_STRUCT thread_struct;
nrUE_params_t nrUE_params = {0};

int get_node_type() {return -1;}

RAN_CONTEXT_t RC;
int oai_exit = 0;

uint64_t        downlink_frequency[MAX_NUM_CCs][4];
int64_t         uplink_frequency_offset[MAX_NUM_CCs][4];
uint64_t        sidelink_frequency[MAX_NUM_CCs][4];

// UE and OAI config variables

// TODO sidelink: avoid linker errors for including gNB
NR_IF_Module_t *NR_IF_Module_init(int Mod_id) { abort(); return NULL; }
int get_nr_prach_duration(uint8_t prach_format) { abort(); return 0; }
int beam_index_allocation(bool das,
                          int fapi_beam_index,
                          NR_gNB_COMMON *common_vars,
                          int slot,
                          int symbols_per_slot,
                          int bitmap_symbols)
{
  abort(); return 0;
}
int nr_slot_select() { abort(); }
int get_first_ant_idx() { abort(); }

int create_tasks_nrue(uint32_t ue_nb) {
  LOG_D(NR_RRC, "%s(ue_nb:%d)\n", __FUNCTION__, ue_nb);
  itti_wait_ready(1);

  if (ue_nb > 0) {
    LOG_I(NR_RRC,"create TASK_RRC_NRUE \n");
    const ittiTask_parms_t parmsRRC = {NULL, rrc_nrue};
    if (itti_create_task(TASK_RRC_NRUE, rrc_nrue_task, &parmsRRC) < 0) {
      LOG_E(NR_RRC, "Create task for RRC UE failed\n");
      return -1;
    }
    const ittiTask_parms_t parmsNAS = {NULL, nas_nrue};
    if (itti_create_task(TASK_NAS_NRUE, nas_nrue_task, &parmsNAS) < 0) {
      LOG_E(NR_RRC, "Create task for NAS UE failed\n");
      return -1;
    }
  }

  itti_wait_ready(0);

  return 0;
}

uint64_t get_nrUE_optmask(void) {
  return nrUE_params.optmask;
}

uint64_t set_nrUE_optmask(uint64_t bitmask) {
  nrUE_params.optmask = nrUE_params.optmask | bitmask;
  return nrUE_params.optmask;
}

nrUE_params_t *get_nrUE_params(void) {
  return &nrUE_params;
}
static void get_options(configmodule_interface_t *cfg)
{
  paramdef_t cmdline_params[] = CMDLINE_NRUEPARAMS_DESC;
  int numparams = sizeofArray(cmdline_params);
  config_get(cfg, cmdline_params, numparams, NULL);
}

// set PHY vars from command line
static void set_UE_options(int CC_id, PHY_VARS_NR_UE *UE, int ru_id)
{
  nrUE_RU_params_t *RU = nrue_get_ru(ru_id);
  UE->ru_client = RU->nb_clients++;
  // Set UE variables
  UE->rx_total_gain_dB = RU->max_rxgain - RU->att_rx;
  UE->tx_total_gain_dB = RU->att_tx;
  UE->if_freq          = RU->if_frequency;
  UE->if_freq_off      = RU->if_freq_offset;
  UE->rf_map.card      = ru_id;
  UE->rf_map.chain     = CC_id;

  UE->tx_power_max_dBm     = nrUE_params.tx_max_power;
  UE->max_ldpc_iterations  = nrUE_params.max_ldpc_iterations;
  UE->UE_scan_carrier      = nrUE_params.UE_scan_carrier;
  UE->UE_fo_compensation   = nrUE_params.UE_fo_compensation;
  UE->chest_freq           = nrUE_params.chest_freq;
  UE->chest_time           = nrUE_params.chest_time;
  UE->no_timing_correction = nrUE_params.no_timing_correction;
  UE->initial_fo           = nrUE_params.initial_fo;
  UE->cont_fo_comp         = nrUE_params.cont_fo_comp;
  UE->disable_blind_search = nrUE_params.disable_blind_search;

  LOG_I(PHY,"Set UE_fo_compensation %d, UE_scan_carrier %d, UE_no_timing_correction %d \n, chest-freq %d, chest-time %d\n",
        UE->UE_fo_compensation, UE->UE_scan_carrier, UE->no_timing_correction, UE->chest_freq, UE->chest_time);
}

static void set_fp_options(int ru_id, int cell_id)
{
  const nrUE_RU_params_t *RU = nrue_get_ru(ru_id);
  NR_DL_FRAME_PARMS *fp = RU->cell[cell_id].fp;
  // Set FP variables
  fp->nb_antennas_rx = RU->nb_rx;
  fp->nb_antennas_tx = RU->nb_tx;

  fp->threequarter_fs     = get_softmodem_params()->threequarter_fs;
  fp->ofdm_offset_divisor = nrUE_params.ofdm_offset_divisor;

  LOG_I(PHY, "Set UE nb_rx_antenna %d, nb_tx_antenna %d, threequarter_fs %d, ofdm_offset_divisor %d\n", fp->nb_antennas_rx, fp->nb_antennas_tx, fp->threequarter_fs, fp->ofdm_offset_divisor);
}

static bool stop_immediately = false;
static void trigger_stop(int sig)
{
  if (!oai_exit)
    itti_wait_tasks_unblock();
}
static void trigger_deregistration(int sig)
{
  // Sidelink has no NAS registration; detaching would release MAC state still used by the PHY workers.
  if (!stop_immediately && IS_SA_MODE(get_softmodem_params()) && !get_softmodem_params()->sl_mode) {
    for (int ue_inst = 0; ue_inst < NB_UE_INST; ue_inst++) {
      MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, ue_inst, NAS_DEREGISTRATION_REQ);
      NAS_DEREGISTRATION_REQ(msg).cause = AS_DETACH;
      itti_send_msg_to_task(TASK_NAS_NRUE, ue_inst, msg);
    }
    stop_immediately = true;
    static const char m[] = "Press ^C again to trigger immediate shutdown\n";
    __attribute__((unused)) int unused = write(STDOUT_FILENO, m, sizeof(m) - 1);
    signal(SIGALRM, trigger_stop);
    alarm(5);
  } else {
    itti_wait_tasks_unblock();
  }
}

void *nrue_ru_start_thread(void *arg)
{
  (void)arg;
  nrue_ru_start();
  return NULL;
}

int NB_UE_INST = 1;
configmodule_interface_t *uniqCfg = NULL;
nrLDPC_coding_interface_t nrLDPC_coding_interface = {0};

int main(int argc, char **argv)
{
  start_background_system();

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL || CONFIG_ISFLAGSET(CONFIG_ABORT)) {
    fprintf(stderr, "configuration module init failed\n");
    return 1;
  }
  //set_softmodem_sighandler();
  CONFIG_SETRTFLAG(CONFIG_NOEXITONHELP);
  // initialize logging
  logInit();
  // get options and fill parameters from configuration file

  get_options(uniqCfg); // Command-line options specific for NRUE
  IS_SOFTMODEM_5GUE = true;
  get_common_options(uniqCfg);
  CONFIG_CLEARRTFLAG(CONFIG_NOEXITONHELP);

  softmodem_verify_mode(get_softmodem_params());

#if T_TRACER
  T_Config_Init();
#endif
  initTpool(get_softmodem_params()->threadPoolConfig, &(nrUE_params.Tpool), cpumeas(CPUMEAS_GETSTATE));
  set_taus_seed (0);

  if (!has_cap_sys_nice())
    LOG_W(UTIL,
          "no SYS_NICE capability: cannot set thread priority and affinity, consider running with sudo for optimum performance\n");

  itti_init(TASK_MAX, tasks_info);

  init_opt();

  int ret_loader = load_nrLDPC_coding_interface(NULL, &nrLDPC_coding_interface, 32);
  AssertFatal(ret_loader == 0, "error loading LDPC library\n");

  // strdup to put the sring in the core file for post mortem identification
  char *pckg = strdup(OAI_PACKAGE_VERSION);
  LOG_I(HW, "Version: %s\n", pckg);

  AssertFatal(NB_UE_INST <= MAX_NUM_NR_UE_INST, "%d UEs requested, at most %d per process\n", NB_UE_INST, MAX_NUM_NR_UE_INST);
  nrPHY_vars_UE_g = calloc_or_fail(NB_UE_INST, sizeof(*nrPHY_vars_UE_g));
  for (int inst = 0; inst < NB_UE_INST; inst++) {
    nrPHY_vars_UE_g[inst] = calloc_or_fail(MAX_NUM_CCs, sizeof(*nrPHY_vars_UE_g[inst]));
    for (int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
      nrPHY_vars_UE_g[inst][CC_id] = calloc_or_fail(1, sizeof(*nrPHY_vars_UE_g[inst][CC_id]));
      // All instances use the same coding interface
      nrPHY_vars_UE_g[inst][CC_id]->nrLDPC_coding_interface = nrLDPC_coding_interface;
    }
  }

  if (create_tasks_nrue(1) < 0) {
    printf("cannot create ITTI tasks\n");
    exit(-1); // need a softer mode
  }

  nr_pdcp_layer_init();
  nas_init_nrue(NB_UE_INST);

  nrue_set_ru_params(uniqCfg);
  nrue_set_cell_params(uniqCfg);

  init_NR_UE(NB_UE_INST,
             get_nrUE_params()->uecap_file,
             get_nrUE_params()->reconfig_file,
             get_nrUE_params()->rbconfig_file,
             get_nrUE_params()->ue_capability_rx2tx,
             nrue_get_ru(0)->cell[0].numerology);

  // start time manager with some reasonable default for the running mode
  // (may be overwritten in configuration file or command line)
  void nr_pdcp_ms_tick(void);
  void nr_rlc_ms_tick(void);
  time_manager_tick_function_t tick_functions[] = {
    nr_pdcp_ms_tick,
    nr_rlc_ms_tick
  };
  int tick_functions_count = 2;
  time_manager_start(tick_functions, tick_functions_count,
                     // iq_samples time source for rfsim,
                     // realtime time source if not
                     IS_SOFTMODEM_RFSIM ? TIME_SOURCE_IQ_SAMPLES
                                        : TIME_SOURCE_REALTIME);

  // initialize per-cell frame parameters
  for (int ru_id = 0; ru_id < nrue_get_ru_count(); ru_id++) {
    nrUE_RU_params_t *ru = nrue_get_ru(ru_id);
    for (int i = 0; i < ru->nb_cells; i++) {
      if (!ru->cell[i].active) // skip RUs that have no cell definition
        continue;
      set_fp_options(ru_id, i);
      if (IS_SA_MODE(get_softmodem_params()) || get_softmodem_params()->sl_mode)
        nr_init_frame_parms_ue_sa(ru->cell[i].fp, ru->cell + i);
    }
  }

  for (int inst = 0; inst < NB_UE_INST; inst++) {
    // one RU per UE when the configuration has enough, else all the UEs share the first RU;
    // FD-UE instances always share one device (see nr-ue-fd.c)
    const int ru_id = !get_nrUE_params()->use_ue_fd && nrue_get_ru_count() >= NB_UE_INST ? inst : 0;
    position_t position = {0};
    get_position_coordinates(inst, &position);

    NR_UE_MAC_INST_t *mac = get_mac_inst(inst);
    for (int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
      PHY_VARS_NR_UE *UE_CC = nrPHY_vars_UE_g[inst][CC_id];
      UE_CC->position = position;

      set_UE_options(CC_id, UE_CC, ru_id);
      init_nr_ue_phy_cpu_stats(&UE_CC->phy_cpu_stats);
      nrUE_RU_params_t *ru = nrue_get_ru(ru_id);
      NR_DL_FRAME_PARMS *fp = ru->cell[0].fp;
      if (!IS_SA_MODE(get_softmodem_params()) && !get_softmodem_params()->sl_mode) {
        do {
          notifiedFIFO_elt_t *elt = pollNotifiedFIFO(&mac->input_nf);
          if (!elt) {
            break;
          }
          process_msg_rcc_to_mac(NotifiedFifoData(elt), inst);
          delNotifiedFIFO_elt(elt);
        } while (true);
        fapi_nr_config_request_t *nrUE_config = &UE_CC->nrUE_config;
        nr_init_frame_parms_ue(fp, nrUE_config, mac->nr_band);

        ru->cell[0].band = mac->nr_band;
        ru->cell[0].rf_frequency = fp->dl_CarrierFreq;
        ru->cell[0].rf_freq_offset = fp->ul_CarrierFreq - fp->dl_CarrierFreq;
        ru->cell[0].numerology = fp->numerology_index;
        ru->cell[0].N_RB_DL = fp->N_RB_DL;
        ru->cell[0].ssb_start = fp->ssb_start_subcarrier;
      }

      UE_CC->frame_parms = *fp;
      mac->nr_band = ru->cell[0].band;
      mac->ssb_start_subcarrier = ru->cell[0].ssb_start;
      mac->dl_frequency = ru->cell[0].rf_frequency;

      UE_CC->sl_mode = get_softmodem_params()->sl_mode;
      init_nr_ue_vars(UE_CC, inst);

      if (UE_CC->sl_mode) {
        AssertFatal(UE_CC->sl_mode == SL_MODE2_SUPPORTED, "Only Sidelink mode 2 supported. Mode 1 not yet supported\n");
        DevAssert(mac->if_module != NULL && mac->if_module->sl_phy_config_request != NULL);
        // Patch antenna counts from CLI and push to PHY so sl_config is fully populated.
        nr_sl_phy_config_t *phycfg = &mac->SL_MAC_PARAMS->sl_phy_config;
        phycfg->sl_config_req.sl_carrier_config.sl_num_rx_ant = get_nrUE_params()->nb_antennas_rx;
        phycfg->sl_config_req.sl_carrier_config.sl_num_tx_ant = get_nrUE_params()->nb_antennas_tx;
        mac->if_module->sl_phy_config_request(phycfg);
        // Derive sl_frame_params (N_RB_SL, ofdm_symbol_size, nb_antennas_tx ...) from sl_config
        // before HARQ buffer allocation and PHY signal init.
        sl_nr_ue_phy_params_t *sl_phy = &UE_CC->SL_UE_PHY_PARAMS;
        nr_init_frame_parms_ue_sl(&sl_phy->sl_frame_params,
                                  &sl_phy->sl_config,
                                  get_softmodem_params()->threequarter_fs,
                                  get_nrUE_params()->ofdm_offset_divisor);
        // Allocate SL HARQ buffers now that N_RB_SL and nb_antennas_tx are known.
        init_nr_ue_sl_transport(UE_CC);

        sl_ue_phy_init(UE_CC);
      }

    }
  }

  nrue_init_openair0();

  /* mlockall() makes every page resident, including the whole 8 MB stack of each thread: with tens of
     UEs in one process (simulation), that is gigabytes for no real-time gain */
  if (NB_UE_INST == 1)
    lock_memory_to_ram();

  if (IS_SOFTMODEM_DOSCOPE) {
    load_softscope("nr", nrPHY_vars_UE_g[0][0]);
  }
  if (IS_SOFTMODEM_IMSCOPE_ENABLED) {
    load_softscope("im", nrPHY_vars_UE_g[0][0]);
  }
  AssertFatal(!(IS_SOFTMODEM_IMSCOPE_ENABLED && IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED),
              "Data recoding and ImScope cannot be enabled at the same time\n");
  if (IS_SOFTMODEM_IMSCOPE_RECORD_ENABLED) {
    load_module_shlib("imscope_record", NULL, 0, nrPHY_vars_UE_g[0][0]);
  }

  // FD-UE manages its own RF device; skip the regular RU start.
  if (!get_nrUE_params()->use_ue_fd) {
    // Launch a temporary high-priority thread to start the UE RU, ensuring radio library threads inherit this priority
    pthread_t ru_start_thread;
    threadCreate(&ru_start_thread, nrue_ru_start_thread, NULL, "ru_start_thread", -1, OAI_PRIORITY_RT_MAX);
    int ret = pthread_join(ru_start_thread, NULL);
    AssertFatal(ret == 0, "pthread_join error %d, errno %d (%s)\n", ret, errno, strerror(errno));
  }

  if (get_nrUE_params()->use_ue_fd) {
    // Every instance is a client of one UE Low-PHY (one radio, one cell).
    PHY_VARS_NR_UE *UE_list[MAX_NUM_NR_UE_INST];
    for (int inst = 0; inst < NB_UE_INST; inst++)
      UE_list[inst] = nrPHY_vars_UE_g[inst][0];
    LOG_I(PHY, "Initializing FD-UE threads for %d instance(s) ...\n", NB_UE_INST);
    init_NR_UE_fd(UE_list, NB_UE_INST);
  } else {
    for (int inst = 0; inst < NB_UE_INST; inst++) {
      LOG_I(PHY, "Initializing UE Threads for instance %d ...\n", inst);
      init_NR_UE_threads(nrPHY_vars_UE_g[inst][0]);
    }
  }

  // wait for end of program
  printf("TYPE <CTRL-C> TO TERMINATE\n");

  // Sleep a while before checking all parameters have been used
  // Some are used directly in external threads, asynchronously
  sleep(2);
  config_check_unknown_cmdlineopt(uniqCfg, CONFIG_CHECKALLSECTIONS);

  // wait for end of program
  printf("Entering ITTI signals handler\n");
  printf("TYPE <CTRL-C> TO TERMINATE\n");
  itti_wait_tasks_end(trigger_deregistration);
  LOG_W(NR_PHY, "Returned from ITTI signal handler\n");
  oai_exit = 1;

  nrue_ru_stop();

  if (get_nrUE_params()->use_ue_fd) {
    PHY_VARS_NR_UE *UE_list[MAX_NUM_NR_UE_INST];
    for (int inst = 0; inst < NB_UE_INST; inst++)
      UE_list[inst] = nrPHY_vars_UE_g[inst][0];
    term_NR_UE_fd(UE_list, NB_UE_INST);
  } else if (nrPHY_vars_UE_g && nrPHY_vars_UE_g[0]) {
    for (int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
      PHY_VARS_NR_UE *phy_vars = nrPHY_vars_UE_g[0][CC_id];
      if (phy_vars) {
        int ret = pthread_join(phy_vars->main_thread, NULL);
        AssertFatal(ret == 0, "pthread_join error %d, errno %d (%s)\n", ret, errno, strerror(errno));
        if (!IS_SOFTMODEM_NOSTATS) {
          ret = pthread_join(phy_vars->stat_thread, NULL);
          AssertFatal(ret == 0, "pthread_join error %d, errno %d (%s)\n", ret, errno, strerror(errno));
        }
      }
    }
  }

  nrue_ru_end();

  free_nrLDPC_coding_interface(&nrLDPC_coding_interface);

  time_manager_finish();

  free(pckg);
  printf("Bye.\n");
  return 0;
}

