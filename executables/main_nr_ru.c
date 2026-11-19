/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <sched.h>
#include <string.h>
#include "assertions.h"
#include "PHY/defs_RU.h"
#include "common/oai_version.h"
#include "common/config/config_userapi.h"
#include "common/ran_context.h"
#include "common/utils/LOG/log.h"
#include "system.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include <executables/softmodem-common.h>
#include <executables/thread-common.h>
#include "executables/nr-softmodem.h"
#include "nr-oru.h"
#include "common/utils/threadPool/thread-pool.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/INIT/nr_parms.h"

pthread_cond_t sync_cond;
pthread_mutex_t sync_mutex;
int sync_var = -1; //!< protected by mutex \ref sync_mutex.
int config_sync_var = -1;

int oai_exit = 0;
int sf_ahead = 4;
int emulate_rf = 0;

RAN_CONTEXT_t RC;



int64_t uplink_frequency_offset[MAX_NUM_CCs][4];

void nfapi_setmode(nfapi_mode_t nfapi_mode)
{
  return;
}

static void get_options(configmodule_interface_t *cfg)
{
  CONFIG_SETRTFLAG(CONFIG_NOEXITONHELP);
  get_common_options(cfg);
  CONFIG_CLEARRTFLAG(CONFIG_NOEXITONHELP);

  //  NRCConfig();
}

nfapi_mode_t nfapi_getmode(void)
{
  return (NFAPI_MODE_PNF);
}

void oai_nfapi_rach_ind(nfapi_rach_indication_t *rach_ind)
{
  AssertFatal(1 == 0, "This is bad ... please check why we get here\n");
}

void wait_eNBs(void)
{
  return;
}
void wait_gNBs(void)
{
  return;
}

struct timespec timespec_add(struct timespec, struct timespec)
{
  struct timespec t = {0};
  return t;
};
struct timespec timespec_sub(struct timespec, struct timespec)
{
  struct timespec t = {0};
  return t;
};
void beam_index_allocation(uint16_t fapi_beam_index,
                           int ant,
                           int num_ports,
                           int symbols_per_slot,
                           int slot,
                           uint16_t bitmap_symbols,
                           int num_ant_max,
                           int num_beams_period,
                           uint16_t **ant_beam_id_list)
{
}
uint16_t get_first_ant_idx(bool das, uint16_t num_ports_beams, uint16_t beam_id, uint16_t fapi_start_port)
{
  return 0;
};
void nr_fill_du(uint16_t N_ZC, const uint16_t *prach_root_sequence_map, uint16_t nr_du[NR_PRACH_SEQ_LEN_L - 1])
{
  return;
};

static void sig_handler(int sig_num)
{
  oai_exit = 1;
}

uint16_t nr_du[838];

uint64_t downlink_frequency[MAX_NUM_CCs][4];

configmodule_interface_t *uniqCfg = NULL;
THREAD_STRUCT thread_struct;

int main(int argc, char **argv)
{
  memset(&RC, 0, sizeof(RC));
  if ((uniqCfg = load_configmodule(argc, argv, 0)) == NULL) {
    fprintf(stderr, "configuration module init failed\n");
    return 1;
  }

  logInit();
  LOG_W(PHY, "%s is experimental software and at this point is not an implementation of a 7.2 O-RAN RU\n", argv[0]);
  printf("Reading in command-line options\n");
  get_options(uniqCfg);

  if (CONFIG_ISFLAGSET(CONFIG_ABORT)) {
    fprintf(stderr, "Getting configuration failed\n");
    exit(-1);
  }

#if T_TRACER
  T_Config_Init();
#endif
  printf("configuring for RRU\n");
  // strdup to put the sring in the core file for post mortem identification
  LOG_I(HW, "Version: %s\n", strdup(OAI_PACKAGE_VERSION));

  /* Read configuration */

  printf("About to Init RU threads\n");

  lock_memory_to_ram();
  load_dftslib();

  RC.nb_RU = 1;
  RC.ru = malloc(sizeof(RC.ru));

  NRRCconfig_RU(config_get_if());

  RU_t *ru = RC.ru[0];
  ORU_t oru = {0};
  oru.ru = ru;
  int ret = get_oru_options(&oru);
  AssertFatal(ret == 0, "Cannot configure oru, check your config file/cmdline");
  ru->numerology = oru.numerology;
  oru_init_frame_parms(&oru);
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  nr_dump_frame_parms(fp);
  init_symbol_rotation(fp);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_UL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);
  ru->if_south = LOCAL_RF;
  nr_phy_init_RU(oru.ru);
  fill_rf_config(ru, ru->rf_config_file);
  ru->N_TA_offset = set_default_nta_offset(fp->freq_range, fp->samples_per_subframe);

  /* set PRACH configuration */
  nfapi_nr_prach_config_t *prach_config = &ru->config.prach_config;
  prach_config->prach_ConfigurationIndex.value = oru.prach_config_index;
  prach_config->num_prach_fd_occasions_list[0].k1.value = oru.prach_msg1_freq;
  prach_config->prach_sequence_length.value = 1;
  prach_config->prach_sub_c_spacing.value = 1;
  prach_config->num_prach_fd_occasions.value = 1;

  reset_meas(&oru.rx_prach);
  oru.prach_info = get_nr_prach_occasion_info_from_index(oru.prach_config_index, FR1, fp->frame_type);
  LOG_A(PHY, "PRACH configuration index %d\n", oru.prach_config_index);
  LOG_A(PHY,
        "PRACH format %d start_symbol %d duration %d\n",
        oru.prach_info.format,
        oru.prach_info.start_symbol,
        oru.prach_info.N_dur);
  prepare_prach_item(&oru);
  AssertFatal(oru.prach_item.pdu.prach_format == 8,
              "prach_config_index %d: only PRACH format B4 is supported (got FAPI format %d)\n",
              oru.prach_config_index,
              oru.prach_item.pdu.prach_format);
  if (oru.fh_config.prach_kbar < 0) {
    // Short format: the PRACH SCS is the numerology
    oru.fh_config.prach_kbar = 2 * get_PRACH_k_bar(fp->numerology_index, fp->numerology_index);
  }
  LOG_A(PHY, "PRACH kbar %d (int16)\n", oru.fh_config.prach_kbar);

  oru.fronthaul = oru_fh_init(&oru.fh_config);
  AssertFatal(oru.fronthaul != NULL, "Cannot configure oru fronthaul, check your config file/cmdline");

  LOG_I(PHY, "starting rfdevice\n");
  ret = openair0_device_load(&ru->rfdevice, &ru->openair0_cfg);
  AssertFatal(ret == 0, "RU %u: openair0_device_load() ret %d: cannot initialize rfdevice\n", ru->idx, ret);
  ret = ru->rfdevice.trx_start_func(&ru->rfdevice);
  AssertFatal(ret == 0, "RU %u: trx_start_func() ret %d: cannot start rfdevice\n", ru->idx, ret);

  // Signal handler
  signal(SIGINT, sig_handler);

  ret = oru_fh_start(oru.fronthaul);
  AssertFatal(ret == 0, "Cannot start O-RU fronthaul\n");

  int rc = pthread_mutex_init(&oru.tx_write.mutex, NULL);
  AssertFatal(rc == 0, "pthread_mutex_init() failed: %d, %s\n", rc, strerror(rc));
  rc = pthread_cond_init(&oru.tx_write.cond, NULL);
  AssertFatal(rc == 0, "pthread_cond_init() failed: %d, %s\n", rc, strerror(rc));
  oru.tx_write.initialized = false;

  /* Prefer fronthaul.ul_worker_cores when set so north/south pins stay exclusive of the UL Tpool. */
  int num_pool_cores;
  const int *pool_cores;
  if (oru.num_ul_worker_cores > 0) {
    num_pool_cores = oru.num_ul_worker_cores;
    pool_cores = oru.ul_worker_cores;
  } else {
    AssertFatal(ru->num_tpcores > 0, "RU %u: num_tp_cores must be > 0 (or set fronthaul.ul_worker_cores)\n", ru->idx);
    num_pool_cores = ru->num_tpcores;
    pool_cores = ru->tpcores;
  }
  char pool[80];
  int s_offset = sprintf(pool, "%d", pool_cores[0]);
  for (int icpu = 1; icpu < num_pool_cores; icpu++) {
    s_offset += sprintf(pool + s_offset, ",%d", pool_cores[icpu]);
  }
  LOG_I(PHY, "O-RU thread-pool core string %s (size %d)\n", pool, num_pool_cores);
  ru->threadPool = malloc(sizeof(tpool_t));
  initNamedTpool(pool, ru->threadPool, false, "ul_worker");

  /* Create south before north so TX/RX start even if a north reader stalls. */
  threadCreate(&oru.south_read_thread,
               oru_south_read_thread,
               (void *)&oru,
               "south_read_thread",
               oru.south_core,
               OAI_PRIORITY_RT_MAX);
  threadCreate(&oru.south_write_thread,
               oru_south_write_thread,
               (void *)&oru,
               "south_write_thread",
               oru.tx_write.core,
               OAI_PRIORITY_RT_MAX);
  for (int i = 0; i < oru.num_dl_read_threads; i++) {
    char thread_name[32];
    snprintf(thread_name, sizeof(thread_name), "north_read_%d", i);
    threadCreate(&oru.dl_read_threads[i], oru_north_read_worker, (void *)&oru, thread_name, oru.north_cores[i], OAI_PRIORITY_RT_MAX);
  }

  int diag_countdown_s = 10;
  while (oai_exit == 0) {
    if (--diag_countdown_s <= 0) {
      oru_fh_print_stats(oru.fronthaul);
      oru_self_diagnosis(&oru);
      diag_countdown_s = 10;
    }
    sleep(1);
  }

  pthread_mutex_lock(&oru.tx_write.mutex);
  pthread_cond_signal(&oru.tx_write.cond);
  pthread_mutex_unlock(&oru.tx_write.mutex);
  if (oru.dl_reorder) {
    // Wakes oru_south_write_thread() if it's blocked in symbol_reorder_wait_at_least() rather than
    // the tx_write.cond startup wait above.
    symbol_reorder_notify_all(oru.dl_reorder);
  }

  for (int i = 0; i < oru.num_dl_read_threads; i++) {
    pthread_join(oru.dl_read_threads[i], NULL);
  }
  pthread_join(oru.south_read_thread, NULL);
  pthread_join(oru.south_write_thread, NULL);

  if (ru->threadPool) {
    abortTpool(ru->threadPool);
    free(ru->threadPool);
    ru->threadPool = NULL;
  }

  pthread_mutex_destroy(&oru.tx_write.mutex);
  pthread_cond_destroy(&oru.tx_write.cond);
  if (oru.dl_reorder) {
    symbol_reorder_destroy(oru.dl_reorder);
  }

  oru_fh_stop(oru.fronthaul);

  if (ru->rfdevice.trx_stop_func) {
    ru->rfdevice.trx_stop_func(&ru->rfdevice);
    ru->rfdevice.trx_stop_func = NULL;
  }

  if (ru->rfdevice.trx_end_func) {
    ru->rfdevice.trx_end_func(&ru->rfdevice);
    ru->rfdevice.trx_end_func = NULL;
  }

  end_configmodule(uniqCfg);

  logClean();
  printf("Bye.\n");
  return 0;
}
