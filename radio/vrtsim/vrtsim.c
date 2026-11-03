/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/TOOLS/tools_defs.h"
#include "system.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <linux/limits.h>

#include <common/utils/assertions.h>
#include <common/utils/LOG/log.h>
#include <common/utils/load_module_shlib.h>
#include <common/utils/telnetsrv/telnetsrv.h>
#include <common/config/config_userapi.h>
#include <common/openairinterface5g_limits.h>
#include "common_lib.h"
#include "shm_td_iq_channel.h"
#include "SIMULATION/TOOLS/sim.h"
#include "simde/x86/avx512.h"
#include "taps_client.h"
#include "cirdb_provider.h"
#include "cirdb_yaml.h"
#include "channel_pipeline.h"
#include "thread-pool.h"

// Simulator role
typedef enum { ROLE_SERVER = 1, ROLE_CLIENT } role;

#define SAVED_SAMPLES_LEN 256
#define MAX_NUM_UES MAX_MOBILES_PER_GNB

#define ROLE_CLIENT_STRING "client"
#define ROLE_SERVER_STRING "server"

#define VRTSIM_SECTION "vrtsim"
#define TIME_SCALE_HLP \
  "sample time scale. 1.0 means realtime. Values > 1 mean faster than realtime. Values < 1 mean slower than realtime\n"
#define TAPS_SOCKET_HLP "Socket to connect to the channel emulation server\n"
#define CLIENT_NUM_RX_HLP "Number of RX antennas of the client, specified on the server\n"
#define CONNECTION_DESCRIPTOR_HLP "Path to the file written by the server that the client can use to connect."
#define CONNECTION_TIMEOUT_HLP                                                                                                 \
  "Seconds the client waits for the peer unix socket (0 = wait forever). Useful when the peer starts slowly (e.g. O-RU after " \
  "DPDK/xRAN init).\n"
#define DEFAULT_CHANNEL_NAME "vrtsim_channel"
#define DEFAULT_DESCRIPTOR "/tmp/vrtsim_connection"
#define DEFAULT_CONNECTION_TIMEOUT 120
#define TPOOL_HLP "Thread pool for channel modelling. Only used if CUDA support is disabled."

// clang-format off
#define VRTSIM_PARAMS_DESC \
  { \
     {"connection_descriptor",  CONNECTION_DESCRIPTOR_HLP,   0, .strptr = &vrtsim_state->connection_descriptor,  .defstrval = DEFAULT_DESCRIPTOR, TYPE_STRING, 0}, \
     {"connection_timeout",     CONNECTION_TIMEOUT_HLP,      0, .uptr = &vrtsim_state->connection_timeout,       .defuintval = DEFAULT_CONNECTION_TIMEOUT, TYPE_UINT, 0}, \
     {"role",                   "either client or server\n", 0, .strptr = &role,                                 .defstrval = ROLE_CLIENT_STRING, TYPE_STRING, 0}, \
     {"timescale",              TIME_SCALE_HLP,              0, .dblptr = &vrtsim_state->timescale,              .defdblval = 1.0,                TYPE_DOUBLE, 0}, \
     {"chanmod",                "Enable channel modelling",  0, .iptr = &vrtsim_state->chanmod,                  .defintval = 0,                  TYPE_INT,    0}, \
     {"taps-socket",            TAPS_SOCKET_HLP,             0, .strptr = &vrtsim_state->taps_socket,            .defstrval = NULL,               TYPE_STRING, 0}, \
     /* CIR DB enable and paths */ \
     {"cirdb",                  "Use CIR database for channel taps (1 yes, 0 no)", 0, .iptr = &vrtsim_state->use_cirdb,  .defintval = 0, TYPE_INT, 0}, \
     {"cirdb-path",             "Directory that holds vrtsim.yaml and cir_db.bin", 0, .strptr = &vrtsim_state->cirdb_path, .defstrval = NULL, TYPE_STRING, 0}, \
     {"cirdb_yaml",             "Absolute path to CIR DB YAML file (optional, overrides cirdb-path)", 0, .strptr = &vrtsim_state->cirdb_yaml, .defstrval = NULL, TYPE_STRING, 0}, \
     {"cirdb_file",             "Absolute path to CIR DB binary file (optional, overrides cirdb-path)", 0, .strptr = &vrtsim_state->cirdb_file, .defstrval = NULL, TYPE_STRING, 0}, \
     /* CIR DB selection knobs */ \
     {"cirdb_model_id",         "Preferred TDL/RT model id 0..5 (5=RT-Beam-forming)", 0, .iptr  = &vrtsim_state->cirdb_model_id,  .defintval = 0,    TYPE_INT,    0}, \
     {"cirdb_ds_ns",            "Desired RMS delay spread in ns", 0, .dblptr = &vrtsim_state->cirdb_ds_ns, .defdblval = 10.0, TYPE_DOUBLE, 0}, \
     {"cirdb_speed_mps",        "Desired speed in m/s", 0, .dblptr = &vrtsim_state->cirdb_speed_mps, .defdblval = 1.5, TYPE_DOUBLE, 0}, \
     {"cirdb_aoa_deg",          "Desired AoA in degrees (TDL-D/E only)", 0, .dblptr = &vrtsim_state->cirdb_aoa_deg, .defdblval = 0.0, TYPE_DOUBLE, 0}, \
     {"num_ues",                "Number of UE slots (server only)\n", 0, .iptr = &vrtsim_state->num_ues,        .defintval = 1,                  TYPE_INT,    0}, \
     {"ue_id",                  "UE slot index 0..num_ues-1 (client only)\n", 0, .iptr = &vrtsim_state->ue_id, .defintval = 0,                  TYPE_INT,    0}, \
     {"thread-pool",            TPOOL_HLP, .strptr = &vrtsim_state->thread_pool_cores, .defstrval = "-1,-1,-1,-1", TYPE_STRING, 0}, \
     {"shm_channel_name",       "Shared memory channel name\n", 0, .strptr = &vrtsim_state->shm_channel_name, .defstrval = DEFAULT_CHANNEL_NAME, TYPE_STRING, 0}, \
     {"disable-timing-thread",  "Disable timing thread for testing", 0, .iptr = &vrtsim_state->disable_timing_thread, .defintval = 0, TYPE_INT, 0} \
  };
// clang-format on

typedef struct histogram_s {
  uint64_t diff[30];
  int num_samples;
  int min_samples;
  double range;
} histogram_t;

typedef struct tx_timing_s {
  uint64_t tx_samples_late;
  uint64_t tx_early;
  uint64_t tx_samples_total;
  double average_tx_budget;
  histogram_t tx_histogram;
} tx_timing_t;

typedef struct {
  int model_id;
  double ds_ns;
  double speed_mps;
  double aoa_deg;
} cirdb_conf_t;

typedef struct {
  int tx_ant;
  int rx_ant;
  int tx_offset;
  int rx_offset;
  cirdb_conf_t cir_conf;
} ue_conf_t;

typedef struct client_info_s {
  int num_ues;
  int gnb_num_tx_ant;
  int gnb_num_rx_ant;
  ue_conf_t ues[MAX_NUM_UES];
} client_info_t;

typedef struct {
  int role;
  char *connection_descriptor;
  uint32_t connection_timeout;
  ShmTDIQChannel *channel;
  uint64_t last_received_sample;
  pthread_t timing_thread;
  bool run_timing_thread;
  double timescale;
  double sample_rate;
  uint64_t rx_samples_late;
  uint64_t rx_early;
  uint64_t rx_samples_total;
  tx_timing_t tx_timing;
  histogram_t chanmod_histogram;
  int chanmod;
  double rx_freq;
  double tx_bw;
  int tx_num_channels;
  int rx_num_channels;
  channel_desc_t *channel_desc[MAX_NUM_UES];
  cirdb_provider_t *cirdb_providers[MAX_NUM_UES];
  char *taps_socket;
  void *taps_client;
  int peer_tx_ant;
  int peer_rx_ant;
  struct timespec start_ts;
  /* CIR DB state */
  int use_cirdb;
  char *cirdb_path;
  char *cirdb_yaml;
  char *cirdb_file;
  int cirdb_model_id;
  double cirdb_ds_ns;
  double cirdb_speed_mps;
  double cirdb_aoa_deg;
  // Multi-UE support
  int num_ues;
  int ue_id;

  ue_conf_t ue_conf[MAX_NUM_UES];
  ue_conf_t ue;

  tpool_t tpool;
  void *channel_pipeline_context;
  char *thread_pool_cores;
  char *shm_channel_name;
  int disable_timing_thread;

  client_info_t client_info;
  pthread_t ipc_thread;
  bool run_ipc_thread;
  int ipc_listen_fd;
} vrtsim_state_t;

static void histogram_add(histogram_t *histogram, double diff)
{
  histogram->num_samples++;
  if (histogram->num_samples >= histogram->min_samples) {
    int bin = min(sizeofArray(histogram->diff) - 1, max(0, (int)(diff / histogram->range * sizeofArray(histogram->diff))));
    histogram->diff[bin]++;
  }
}

static void histogram_print(histogram_t *histogram, char *title)
{
  LOG_I(HW, "%s: %d samples\n", title, histogram->num_samples);
  float bin_size = histogram->range / sizeofArray(histogram->diff);
  float bin_start = 0;
  for (int i = 0; i < sizeofArray(histogram->diff); i++) {
    LOG_I(HW, "Bin %d\t[%.1f - %.1fuS]:\t\t%lu\n", i, bin_start, bin_start + bin_size, histogram->diff[i]);
    bin_start += bin_size;
  }
}

static void load_channel_model(vrtsim_state_t *vrtsim_state, int nb_rx)
{
  load_channellist(vrtsim_state->tx_num_channels, nb_rx, vrtsim_state->sample_rate, vrtsim_state->rx_freq, vrtsim_state->tx_bw);
  char *model_name = vrtsim_state->role == ROLE_CLIENT ? "client_tx_channel_model" : "server_tx_channel_model";
  vrtsim_state->channel_desc[0] = find_channel_desc_fromname(model_name);
  channel_desc_t *channel_desc = vrtsim_state->channel_desc[0];
  AssertFatal(channel_desc != NULL, "Could not find model name %s. Make sure it is present in the config file\n", model_name);
  LOG_A(HW,
        "Channel model %s parameters: path_loss_dB=%.2f, nb_tx=%d, nb_rx=%d, channel_length=%d\n",
        model_name,
        channel_desc->path_loss_dB,
        channel_desc->nb_tx,
        channel_desc->nb_rx,
        channel_desc->channel_length);
  random_channel(channel_desc, 0);
  channel_desc->ch_ps = malloc(sizeof(*channel_desc->ch_ps) * channel_desc->nb_tx * channel_desc->nb_rx);
  allocCast3D(ch, struct complexd, channel_desc->ch, channel_desc->nb_tx, channel_desc->nb_rx, channel_desc->channel_length, false);
  for (int aarx = 0; aarx < channel_desc->nb_rx; aarx++) {
    for (int aatx = 0; aatx < channel_desc->nb_tx; aatx++) {
      cf_t *channel_ps = (cf_t *)malloc(sizeof(cf_t) * channel_desc->channel_length);
      channel_desc->ch_ps[aarx + (aatx * channel_desc->nb_rx)] = channel_ps;
      for (int i = 0; i < channel_desc->channel_length; i++) {
        struct complexd src = ch[aatx][aarx][i];
        channel_ps[i] = (cf_t){src.r, src.i};
      }
    }
  }
}

static void vrtsim_readconfig(vrtsim_state_t *vrtsim_state)
{
  char *role = NULL;
  paramdef_t vrtsim_params[] = VRTSIM_PARAMS_DESC;
  int ret = config_get(config_get_if(), vrtsim_params, sizeofArray(vrtsim_params), VRTSIM_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed\n");
  if (strncmp(role, ROLE_CLIENT_STRING, strlen(ROLE_CLIENT_STRING)) == 0) {
    vrtsim_state->role = ROLE_CLIENT;
  } else if (strncmp(role, ROLE_SERVER_STRING, strlen(ROLE_SERVER_STRING)) == 0) {
    vrtsim_state->role = ROLE_SERVER;
  } else {
    AssertFatal(false, "Invalid role configuration\n");
  }
#ifdef OAI_VRTSIM_TAPS_CLIENT
  if (vrtsim_state->taps_socket) {
    LOG_A(HW, "VRTSIM: will use taps socket %s\n", vrtsim_state->taps_socket);
  }
#else
  if (vrtsim_state->taps_socket) {
    AssertFatal(false, "Invalid configuration: Build with OAI_VRTSIM_TAPS_CLIENT to use taps socket\n");
  }
#endif
  if (vrtsim_state->use_cirdb) {
    LOG_A(HW, "VRTSIM: CIR DB is enabled at runtime\n");
  }
}

static void *vrtsim_timing_job(void *arg)
{
  vrtsim_state_t *vrtsim_state = arg;
  if (clock_gettime(CLOCK_REALTIME, &vrtsim_state->start_ts)) {
    LOG_E(UTIL, "clock_gettime failed\n");
    exit(1);
  }
  int64_t last_sample_index = 0;
  while (vrtsim_state->run_timing_thread) {
    struct timespec current_time;
    if (clock_gettime(CLOCK_REALTIME, &current_time)) {
      LOG_E(UTIL, "clock_gettime failed\n");
      exit(1);
    }
    int64_t diff = (current_time.tv_sec - vrtsim_state->start_ts.tv_sec) * 1000000000
                   + (current_time.tv_nsec - vrtsim_state->start_ts.tv_nsec);
    double sample_index = vrtsim_state->sample_rate * vrtsim_state->timescale * diff / 1e9;
    int64_t samples_to_produce = sample_index - last_sample_index;
    if (samples_to_produce > 0) {
      shm_td_iq_channel_produce_samples(vrtsim_state->channel, samples_to_produce);
      last_sample_index = sample_index;
    }
    usleep(20);
  }
  return 0;
}

static void *vrtsim_ipc_thread(void *arg)
{
  vrtsim_state_t *vrtsim_state = arg;
  while (vrtsim_state->run_ipc_thread) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(vrtsim_state->ipc_listen_fd, &read_fds);

    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000}; // 200ms timeout
    int ret = select(vrtsim_state->ipc_listen_fd + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (ret == 0) {
      // Timeout, check run_ipc_thread again
      continue;
    }

    int client_fd = accept(vrtsim_state->ipc_listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR || !vrtsim_state->run_ipc_thread) {
        break;
      }
      usleep(10000);
      continue;
    }

    // Send client info structure
    size_t total_sent = 0;
    char *ptr = (char *)&vrtsim_state->client_info;
    while (total_sent < sizeof(client_info_t)) {
      ssize_t sent = write(client_fd, ptr + total_sent, sizeof(client_info_t) - total_sent);
      if (sent < 0) {
        if (errno == EINTR) {
          continue;
        }
        LOG_E(HW, "Failed to send client info to client: %s\n", strerror(errno));
        break;
      }
      total_sent += sent;
    }
    close(client_fd);
  }
  return NULL;
}

/**
 * @brief Publishes the client information to a Unix domain socket.
 */
static void server_publish_client_info(vrtsim_state_t *vrtsim_state)
{
  char *socket_path = vrtsim_state->connection_descriptor;
  unlink(socket_path);

  vrtsim_state->ipc_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  AssertFatal(vrtsim_state->ipc_listen_fd >= 0, "Failed to create IPC socket: %s\n", strerror(errno));

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  int ret = bind(vrtsim_state->ipc_listen_fd, (struct sockaddr *)&addr, sizeof(addr));
  AssertFatal(ret == 0, "Failed to bind IPC socket to %s: %s\n", socket_path, strerror(errno));

  ret = listen(vrtsim_state->ipc_listen_fd, 10);
  AssertFatal(ret == 0, "Failed to listen on IPC socket: %s\n", strerror(errno));

  vrtsim_state->run_ipc_thread = true;
  threadCreate(&vrtsim_state->ipc_thread, vrtsim_ipc_thread, vrtsim_state, "vrtsim_ipc", -1, OAI_PRIORITY_RT_MAX);
  LOG_A(HW, "VRTSIM: Started IPC server on socket %s\n", socket_path);
}

static size_t try_read_client_info(int fd, client_info_t *client_info)
{
  size_t total_read = 0;
  char *ptr = (char *)client_info;
  while (total_read < sizeof(*client_info)) {
    ssize_t r = read(fd, ptr + total_read, sizeof(*client_info) - total_read);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (r == 0) {
      break; // Connection closed by peer
    }
    total_read += r;
  }
  return total_read;
}

static client_info_t client_read_info(char *socket_path, uint32_t connection_timeout)
{
  client_info_t client_info;
  uint32_t tries = 0;
  struct sockaddr_un addr;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  /* connection_timeout == 0 means wait forever */
  while (connection_timeout == 0 || tries < connection_timeout) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd >= 0) {
      if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        size_t total_read = try_read_client_info(sock_fd, &client_info);
        close(sock_fd);
        if (total_read == sizeof(client_info)) {
          return client_info;
        }
      } else {
        close(sock_fd);
      }
    }
    sleep(1);
    tries++;
  }
  AssertFatal(0, "Timeout waiting for client info on socket %s after %u s\n", socket_path, connection_timeout);
  return client_info;
}

// 0..4: TDL-A..E; 5: RT-Beam-forming (ray-traced CIR from the raytracing-channel-emulator)
static const char *cirdb_model_id_label(int model_id)
{
  static const char *tdl_labels[] = {"TDL-A", "TDL-B", "TDL-C", "TDL-D", "TDL-E"};
  if (model_id >= 0 && model_id <= 4)
    return tdl_labels[model_id];
  if (model_id == 5)
    return "RT-Beam-forming";
  return "unknown";
}

static void parse_ue_config(vrtsim_state_t *vrtsim_state)
{
  AssertFatal(vrtsim_state->num_ues > 0 && vrtsim_state->num_ues <= MAX_NUM_UES,
              "num_ues=%d out of range (1..%d)\n",
              vrtsim_state->num_ues,
              MAX_NUM_UES);

  for (int i = 0; i < vrtsim_state->num_ues; i++) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s.ue_config.[%d]", VRTSIM_SECTION, i);

    char *antennas = NULL;
    int model_id = vrtsim_state->cirdb_model_id;
    double ds_ns = vrtsim_state->cirdb_ds_ns;
    double speed_mps = vrtsim_state->cirdb_speed_mps;
    double aoa_deg = vrtsim_state->cirdb_aoa_deg;

    paramdef_t ue_params[] = {
        {"antennas", "Antenna config e.g. \"1x2\"", 0, .strptr = &antennas, .defstrval = NULL, TYPE_STRING, 0},
        {"model_id",
         "TDL/RT model id 0..5 (5=RT-Beam-forming)",
         0,
         .iptr = &model_id,
         .defintval = vrtsim_state->cirdb_model_id,
         TYPE_INT,
         0},
        {"ds_ns", "Delay spread in ns", 0, .dblptr = &ds_ns, .defdblval = vrtsim_state->cirdb_ds_ns, TYPE_DOUBLE, 0},
        {"speed_mps", "Speed in m/s", 0, .dblptr = &speed_mps, .defdblval = vrtsim_state->cirdb_speed_mps, TYPE_DOUBLE, 0},
        {"aoa_deg",
         "LOS AoA in degrees (TDL-D/E only)",
         0,
         .dblptr = &aoa_deg,
         .defdblval = vrtsim_state->cirdb_aoa_deg,
         TYPE_DOUBLE,
         0},
    };

    config_get(config_get_if(), ue_params, sizeofArray(ue_params), prefix);

    if (antennas != NULL) {
      int tx_ant, rx_ant;
      AssertFatal(sscanf(antennas, "%dx%d", &tx_ant, &rx_ant) == 2,
                  "Invalid antenna format '%s' for UE %d, use e.g. '1x2'\n",
                  antennas,
                  i);
      AssertFatal(tx_ant > 0 && tx_ant <= OPENAIR0_MAX_ANTENNAS,
                  "Invalid TX antenna count %d for UE %d (max %d)\n",
                  tx_ant,
                  i,
                  OPENAIR0_MAX_ANTENNAS);
      AssertFatal(rx_ant > 0 && rx_ant <= OPENAIR0_MAX_ANTENNAS,
                  "Invalid RX antenna count %d for UE %d (max %d)\n",
                  rx_ant,
                  i,
                  OPENAIR0_MAX_ANTENNAS);
      vrtsim_state->ue_conf[i].tx_ant = tx_ant;
      vrtsim_state->ue_conf[i].rx_ant = rx_ant;
    } else {
      vrtsim_state->ue_conf[i].tx_ant = 1;
      vrtsim_state->ue_conf[i].rx_ant = 1;
    }

    AssertFatal(model_id >= 0 && model_id <= 5, "Invalid model_id %d for UE %d (must be 0-5)\n", model_id, i);
    AssertFatal(ds_ns > 0, "Invalid ds_ns %.1f for UE %d (must be > 0)\n", ds_ns, i);
    AssertFatal(speed_mps >= 0, "Invalid speed_mps %.1f for UE %d (must be >= 0)\n", speed_mps, i);

    vrtsim_state->ue_conf[i].cir_conf.model_id = model_id;
    vrtsim_state->ue_conf[i].cir_conf.ds_ns = ds_ns;
    vrtsim_state->ue_conf[i].cir_conf.speed_mps = speed_mps;
    vrtsim_state->ue_conf[i].cir_conf.aoa_deg = aoa_deg;

    LOG_I(HW,
          "VRTSIM: UE %d configuration: UE_tx=%d UE_rx=%d, Model %d (%s), DS %.1fns, Speed %.1fm/s, AoA %.1fdeg\n",
          i,
          vrtsim_state->ue_conf[i].tx_ant,
          vrtsim_state->ue_conf[i].rx_ant,
          model_id,
          cirdb_model_id_label(model_id),
          ds_ns,
          speed_mps,
          aoa_deg);
  }
}

static void compute_ue_antenna_offsets(vrtsim_state_t *vrtsim_state)
{
  int tx_offset = 0;
  int rx_offset = 0;

  for (int i = 0; i < vrtsim_state->num_ues; i++) {
    vrtsim_state->ue_conf[i].tx_offset = tx_offset;
    vrtsim_state->ue_conf[i].rx_offset = rx_offset;

    LOG_I(HW,
          "VRTSIM: UE %d - TX: %d antennas (offset %d), RX: %d antennas (offset %d)\n",
          i,
          vrtsim_state->ue_conf[i].tx_ant,
          tx_offset,
          vrtsim_state->ue_conf[i].rx_ant,
          rx_offset);

    tx_offset += vrtsim_state->ue_conf[i].tx_ant;
    rx_offset += vrtsim_state->ue_conf[i].rx_ant;
  }
}

static int vrtsim_connect(openair0_device_t *device)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;

  // Setup a shared memory channel
  if (vrtsim_state->role == ROLE_SERVER) {
    parse_ue_config(vrtsim_state);
    compute_ue_antenna_offsets(vrtsim_state);
    int num_tx_streams = 0;
    int num_rx_streams = vrtsim_state->num_ues * device->openair0_cfg[0].rx_num_channels;
    for (int i = 0; i < vrtsim_state->num_ues; i++) {
      num_tx_streams += vrtsim_state->ue_conf[i].rx_ant;
    }
    vrtsim_state->channel = shm_td_iq_channel_create(vrtsim_state->shm_channel_name, num_tx_streams, num_rx_streams);
    LOG_A(HW,
          "vrtsim created a shm_td_iq_channel %s with config tx: %d rx: %d\n",
          vrtsim_state->shm_channel_name,
          num_tx_streams,
          num_rx_streams);
    // Exchange peer info

    vrtsim_state->client_info = (client_info_t){
        .num_ues = vrtsim_state->num_ues,
        .gnb_num_tx_ant = device->openair0_cfg[0].tx_num_channels,
        .gnb_num_rx_ant = device->openair0_cfg[0].rx_num_channels,
    };

    for (int i = 0; i < vrtsim_state->num_ues; i++)
      vrtsim_state->client_info.ues[i] = vrtsim_state->ue_conf[i];

    server_publish_client_info(vrtsim_state);
    if (vrtsim_state->num_ues > 0) {
      vrtsim_state->peer_tx_ant = vrtsim_state->ue_conf[0].tx_ant;
      vrtsim_state->peer_rx_ant = vrtsim_state->ue_conf[0].rx_ant;
    }
    vrtsim_state->last_received_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
    if (!vrtsim_state->disable_timing_thread) {
      vrtsim_state->run_timing_thread = true;
      threadCreate(&vrtsim_state->timing_thread, vrtsim_timing_job, vrtsim_state, "vrtsim_timing", -1, OAI_PRIORITY_RT_MAX);
    }
  } else {
    client_info_t client_info = client_read_info(vrtsim_state->connection_descriptor, vrtsim_state->connection_timeout);
    AssertFatal(client_info.num_ues > 0, "Server did not publish valid num_ues\n");
    AssertFatal(vrtsim_state->ue_id < client_info.num_ues, "ue_id %d >= num_ues %d\n", vrtsim_state->ue_id, client_info.num_ues);

    vrtsim_state->ue = client_info.ues[vrtsim_state->ue_id];

    AssertFatal(vrtsim_state->ue.tx_ant == device->openair0_cfg[0].tx_num_channels,
                "Server expects UE %d to have %d TX antennas, this UE has %d\n",
                vrtsim_state->ue_id,
                vrtsim_state->ue.tx_ant,
                device->openair0_cfg[0].tx_num_channels);
    AssertFatal(vrtsim_state->ue.rx_ant == device->openair0_cfg[0].rx_num_channels,
                "Server expects UE %d to have %d RX antennas, this UE has %d\n",
                vrtsim_state->ue_id,
                vrtsim_state->ue.rx_ant,
                device->openair0_cfg[0].rx_num_channels);

    LOG_I(HW,
          "VRTSIM: UE %d/%d - %dx%d antennas, UL offset=%d, DL offset=%d\n",
          vrtsim_state->ue_id,
          client_info.num_ues,
          vrtsim_state->ue.tx_ant,
          vrtsim_state->ue.rx_ant,
          vrtsim_state->ue.tx_offset,
          vrtsim_state->ue.rx_offset);
    if (vrtsim_state->use_cirdb) {
      vrtsim_state->cirdb_model_id = vrtsim_state->ue.cir_conf.model_id;
      vrtsim_state->cirdb_ds_ns = vrtsim_state->ue.cir_conf.ds_ns;
      vrtsim_state->cirdb_speed_mps = vrtsim_state->ue.cir_conf.speed_mps;
      vrtsim_state->cirdb_aoa_deg = vrtsim_state->ue.cir_conf.aoa_deg;

      LOG_I(HW,
            "VRTSIM: UE %d channel - Model %d (%s), DS %.1fns, Speed %.1fm/s, AoA %.1fdeg\n",
            vrtsim_state->ue_id,
            vrtsim_state->cirdb_model_id,
            cirdb_model_id_label(vrtsim_state->cirdb_model_id),
            vrtsim_state->cirdb_ds_ns,
            vrtsim_state->cirdb_speed_mps,
            vrtsim_state->cirdb_aoa_deg);
    }
    vrtsim_state->peer_tx_ant = client_info.gnb_num_tx_ant;
    vrtsim_state->peer_rx_ant = client_info.gnb_num_rx_ant;
    vrtsim_state->channel = shm_td_iq_channel_connect(vrtsim_state->shm_channel_name, 10);
    vrtsim_state->last_received_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
  }

  // Handle channel modelling after number of RX antennas are known
  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
#ifdef OAI_VRTSIM_TAPS_CLIENT
    if (vrtsim_state->taps_socket) {
      vrtsim_state->taps_client =
          taps_client_connect(vrtsim_state->taps_socket, vrtsim_state->tx_num_channels, vrtsim_state->peer_rx_ant);
    } else if (vrtsim_state->use_cirdb) {
#else
    if (vrtsim_state->taps_socket) {
      AssertFatal(false, "Build with OAI_VRTSIM_TAPS_CLIENT to use taps socket\n");
    }
    if (vrtsim_state->use_cirdb) {
#endif
      const char *yaml_path = NULL;
      const char *bin_path = NULL;

      if (vrtsim_state->cirdb_yaml && vrtsim_state->cirdb_yaml[0])
        yaml_path = vrtsim_state->cirdb_yaml;
      if (vrtsim_state->cirdb_file && vrtsim_state->cirdb_file[0])
        bin_path = vrtsim_state->cirdb_file;

      char yaml_buf[PATH_MAX];
      char bin_buf[PATH_MAX];

      if (!yaml_path || !bin_path) {
        const char *base = vrtsim_state->cirdb_path;
        if (base && base[0]) {
          if (!yaml_path) {
            snprintf(yaml_buf, sizeof(yaml_buf), "%s/%s", base, "vrtsim.yaml");
            yaml_path = yaml_buf;
          }
          if (!bin_path) {
            snprintf(bin_buf, sizeof(bin_buf), "%s/%s", base, "cir_db.bin");
            bin_path = bin_buf;
          }
        }
      }

      cirdb_select_opts_t sel = (cirdb_select_opts_t){0};
      sel.yaml_path = yaml_path;
      sel.bin_path = bin_path;
      AssertFatal(vrtsim_state->cirdb_model_id >= 0 && vrtsim_state->cirdb_model_id <= 5,
                  "Invalid cirdb_model_id=%d (valid: 0..5)\n",
                  vrtsim_state->cirdb_model_id);
      sel.want_model_id = vrtsim_state->cirdb_model_id;
      sel.want_ds_ns = (float)(vrtsim_state->cirdb_ds_ns > 0.0 ? vrtsim_state->cirdb_ds_ns : -1.0);
      sel.want_speed_mps = (float)(vrtsim_state->cirdb_speed_mps > 0.0 ? vrtsim_state->cirdb_speed_mps : -1.0);
      sel.want_aoa_deg = (float)vrtsim_state->cirdb_aoa_deg;

      LOG_A(HW,
            "VRTSIM: CIR DB select yaml='%s' bin='%s'\n",
            sel.yaml_path ? sel.yaml_path : "(auto)",
            sel.bin_path ? sel.bin_path : "(auto)");

      // Multi-UE server: create per-UE channel descriptors
      if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
        for (int u = 0; u < vrtsim_state->num_ues; u++) {
          cirdb_select_opts_t ue_sel = sel;
          ue_sel.want_model_id = vrtsim_state->ue_conf[u].cir_conf.model_id;
          ue_sel.want_ds_ns = vrtsim_state->ue_conf[u].cir_conf.ds_ns;
          ue_sel.want_speed_mps = vrtsim_state->ue_conf[u].cir_conf.speed_mps;
          ue_sel.want_aoa_deg = (float)vrtsim_state->ue_conf[u].cir_conf.aoa_deg;

          AssertFatal(ue_sel.want_model_id >= 0 && ue_sel.want_model_id <= 5,
                      "Invalid CIRDB model_id=%d for UE %d\n",
                      ue_sel.want_model_id,
                      u);

          vrtsim_state->cirdb_providers[u] = cirdb_connect(
                        device->openair0_cfg[0].tx_num_channels,
                        vrtsim_state->ue_conf[u].rx_ant,
                        &ue_sel,
                        &vrtsim_state->channel_desc[u]);

          channel_desc_t *cd = vrtsim_state->channel_desc[u];
          AssertFatal(cd != NULL, "CIRDB failed to create channel_desc for UE %d\n", u);
          AssertFatal(cd->nb_tx == device->openair0_cfg[0].tx_num_channels,
                      "CIRDB shape mismatch UE%d: nb_tx=%d expected %d\n",
                      u,
                      cd->nb_tx,
                      device->openair0_cfg[0].tx_num_channels);
          LOG_I(HW,
                "VRTSIM: UE %d channel_desc=%p ch_ps=%p ch=%p nb_tx=%d nb_rx=%d\n",
                u,
                cd,
                cd->ch_ps,
                cd->ch,
                cd->nb_tx,
                cd->nb_rx);
          LOG_I(HW,
                "VRTSIM: UE %d channel model configuration: channel model antenna dimension %dx%d (RU_tx x UE_rx), Model %d "
                "(%s), DS %.1fns, Speed %.1fm/s, AoA %.1fdeg\n",
                u,
                device->openair0_cfg[0].tx_num_channels,
                vrtsim_state->ue_conf[u].rx_ant,
                ue_sel.want_model_id,
                cirdb_model_id_label(ue_sel.want_model_id),
                ue_sel.want_ds_ns,
                ue_sel.want_speed_mps,
                ue_sel.want_aoa_deg);
        }
        LOG_A(HW, "VRTSIM: Multi-UE channel taps via CIR DB\n");
      } else {
        vrtsim_state->cirdb_providers[0] = cirdb_connect(device->openair0_cfg[0].tx_num_channels, vrtsim_state->peer_rx_ant, &sel, &vrtsim_state->channel_desc[0]);
        LOG_A(HW, "VRTSIM: channel taps via CIR DB\n");
      }
    } else {
      load_channel_model(vrtsim_state, vrtsim_state->peer_rx_ant);
    }
#ifdef CHANNEL_SIM_CUDA
    // RX / TX antenna config known per UE config -> init with optimized config rather than worst case config (64TX64RX)
    int max_rx_ant = vrtsim_state->peer_rx_ant;
    // Taps supplied by the taps client are dynamic and no channel descriptor is available here.
    int max_channel_length = vrtsim_state->taps_socket
                                 ? MAX_TAPS_LEN
                                 : (vrtsim_state->channel_desc[0] ? vrtsim_state->channel_desc[0]->channel_length : 1);
    if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) {
      for (int u = 0; u < vrtsim_state->num_ues; u++) {
        if (vrtsim_state->ue_conf[u].rx_ant > max_rx_ant)
          max_rx_ant = vrtsim_state->ue_conf[u].rx_ant;
        if (vrtsim_state->channel_desc[u] && vrtsim_state->channel_desc[u]->channel_length > max_channel_length)
          max_channel_length = vrtsim_state->channel_desc[u]->channel_length;
      }
    }
    size_t samples_in_one_ms = device->openair0_cfg->sample_rate / 1000;
    vrtsim_state->channel_pipeline_context =
        cuda_channel_pipeline_init(samples_in_one_ms, device->openair0_cfg[0].tx_num_channels, max_rx_ant, max_channel_length);
#endif
  }
  vrtsim_state->tx_timing.tx_histogram.min_samples = 100;
  // Set the histogram range to 3000uS. Anything above that is not interesting
  vrtsim_state->tx_timing.tx_histogram.range = 3000.0;
  vrtsim_state->chanmod_histogram.range = 300.0;
  vrtsim_state->chanmod_histogram.min_samples = 100;
  return 0;
}

static int vrtsim_write_internal(vrtsim_state_t *vrtsim_state, openair0_timestamp_t timestamp, c16_t *samples, int nsamps, int aarx)
{
  tx_timing_t *tx_timing = &vrtsim_state->tx_timing;

  uint64_t sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
  int64_t diff = timestamp - sample;
  double budget = diff / (vrtsim_state->sample_rate / 1e6);
  tx_timing->average_tx_budget = .05 * budget + .95 * tx_timing->average_tx_budget;
  histogram_add(&tx_timing->tx_histogram, budget);

  int ret = shm_td_iq_channel_tx(vrtsim_state->channel, timestamp, nsamps, aarx, (sample_t *)samples);

  if (ret == CHANNEL_ERROR_TOO_LATE) {
    tx_timing->tx_samples_late += nsamps;
  } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
    tx_timing->tx_early += 1;
  }
  tx_timing->tx_samples_total += nsamps;

  return nsamps;
}

static int vrtsim_write_with_chanmod(vrtsim_state_t *vrtsim_state,
                                     openair0_timestamp_t timestamp,
                                     void **samplesVoid,
                                     int nsamps,
                                     int nbAnt)
{
  // Sample history for channel impulse response
  static c16_t saved_samples[OPENAIR0_MAX_ANTENNAS][SAVED_SAMPLES_LEN] __attribute__((aligned(32))) = {0};
  if (vrtsim_state->use_cirdb) {
    double seconds = (double)timestamp / vrtsim_state->sample_rate;
    uint64_t elapsed_ns = (uint64_t)(seconds * 1e9 + 0.5);
    int num_providers = (vrtsim_state->role == ROLE_SERVER && vrtsim_state->num_ues > 1) ? vrtsim_state->num_ues : 1;
    for (int p = 0; p < num_providers; p++) {
      if (vrtsim_state->cirdb_providers[p]) {
        cirdb_update(vrtsim_state->cirdb_providers[p], elapsed_ns);
      }
    }
  }

  int noise_power_dBFS = get_noise_power_dBFS();
  int16_t noise_power = noise_power_dBFS == INVALID_DBFS_VALUE ? 0 : (int16_t)(32767.0 / powf(10.0, .05 * -noise_power_dBFS));

  int num_chan_desc = 1;
  if (vrtsim_state->role == ROLE_SERVER) {
    num_chan_desc = vrtsim_state->num_ues;
  }
  int rx_antenna_offset = 0;
  if (vrtsim_state->role == ROLE_CLIENT) {
    rx_antenna_offset = vrtsim_state->ue_id * vrtsim_state->peer_rx_ant;
  }
  int nb_tx = nbAnt;
  for (int i = 0; i < num_chan_desc; i++) {
    channel_desc_t *chan_desc = NULL;
#ifdef OAI_VRTSIM_TAPS_CLIENT
    if (vrtsim_state->taps_client) {
      chan_desc = taps_client_get_model(vrtsim_state->taps_client, i);
    } else {
      chan_desc = vrtsim_state->channel_desc[i];
    }
#else
    chan_desc = vrtsim_state->channel_desc[i];
#endif
    if (!chan_desc) {
      continue;
    }
    int nb_rx = chan_desc->nb_rx;
    size_t channel_length = chan_desc->channel_length;
    AssertFatal((channel_length - 1) < SAVED_SAMPLES_LEN,
                "Need to ensure at least %ld samples are saved between calls\n",
                channel_length - 1);

    cf_t channel_impulse_response[nb_rx * nb_tx][channel_length] __attribute__((aligned(32)));
    cf_t *channel_impulse_response_p[nb_rx * nb_tx] __attribute__((aligned(32)));
    int channel_index = 0;
    if (!vrtsim_state->taps_socket && !vrtsim_state->use_cirdb) {
      const float path_gain_linear = powf(10, -chan_desc->path_loss_dB / 20.0);
      for (int aarx = 0; aarx < nb_rx; aarx++) {
        for (int aatx = 0; aatx < nb_tx; aatx++) {
          const cf_t *channelModel = chan_desc->ch_ps[aarx + (aatx * chan_desc->nb_rx)];
          for (int i = 0; i < channel_length; i++) {
            channel_impulse_response[channel_index][i].r = channelModel[i].r * path_gain_linear;
            channel_impulse_response[channel_index][i].i = channelModel[i].i * path_gain_linear;
          }
          channel_impulse_response_p[channel_index] = channel_impulse_response[channel_index];
          channel_index++;
        }
      }
    } else {
      for (int aarx = 0; aarx < nb_rx; aarx++) {
        for (int aatx = 0; aatx < nb_tx; aatx++) {
          struct complexf *channelModel = chan_desc->ch_ps[aarx + (aatx * chan_desc->nb_rx)];
          channel_impulse_response_p[channel_index++] = channelModel;
        }
      }
    }
    c16_t output[nb_rx][nsamps] __attribute__((aligned(32)));
    c16_t *output_ptr[nb_rx] __attribute__((aligned(32)));
    for (int aarx = 0; aarx < nb_rx; aarx++) {
      output_ptr[aarx] = output[aarx];
    }
    c16_t *input_ptr[nb_tx] __attribute__((aligned(32)));
    for (int i = 0; i < nb_tx; i++) {
      input_ptr[i] = samplesVoid[i];
    }
    c16_t *saved_samples_ptr[nb_tx] __attribute__((aligned(32)));
    for (int i = 0; i < nb_tx; i++) {
      saved_samples_ptr[i] = &saved_samples[i][SAVED_SAMPLES_LEN - (channel_length - 1)];
    }
    size_t saved_samples_input_len = channel_length - 1;

#ifdef CHANNEL_SIM_CUDA
    cuda_channel_pipeline(vrtsim_state->channel_pipeline_context,
                          (const cf_t **)channel_impulse_response_p,
                          (const c16_t **)saved_samples_ptr,
                          (const c16_t **)input_ptr,
                          saved_samples_input_len,
                          output_ptr,
                          NULL,
                          nsamps,
                          nsamps,
                          channel_length,
                          nb_tx,
                          nb_rx,
                          noise_power);
#else
    channel_pipeline(&vrtsim_state->tpool,
                     (const cf_t **)channel_impulse_response_p,
                     (const c16_t **)saved_samples_ptr,
                     (const c16_t **)input_ptr,
                     saved_samples_input_len,
                     output_ptr,
                     NULL,
                     nsamps,
                     nsamps,
                     channel_length,
                     nb_tx,
                     nb_rx,
                     noise_power);
#endif

    for (int aarx = 0; aarx < nb_rx; aarx++) {
      vrtsim_write_internal(vrtsim_state, timestamp, output[aarx], nsamps, rx_antenna_offset + aarx);
    }
    rx_antenna_offset += nb_rx;
  }

  // Save samples for next round
  if (nsamps < SAVED_SAMPLES_LEN) {
    for (int aatx = 0; aatx < nbAnt; aatx++) {
      memmove(&saved_samples[aatx][0], &saved_samples[aatx][nsamps], sizeof(c16_t) * (SAVED_SAMPLES_LEN - nsamps));
      memcpy(&saved_samples[aatx][SAVED_SAMPLES_LEN - nsamps], samplesVoid[aatx], sizeof(c16_t) * nsamps);
    }
  } else {
    for (int aatx = 0; aatx < nbAnt; aatx++) {
      c16_t *samples = (c16_t *)samplesVoid[aatx];
      memcpy(saved_samples[aatx], &samples[nsamps - SAVED_SAMPLES_LEN], sizeof(c16_t) * (SAVED_SAMPLES_LEN));
    }
  }

  return nsamps;
}

static int vrtsim_write(openair0_device_t *device,
                        openair0_timestamp_t timestamp,
                        void **samplesVoid,
                        int nsamps,
                        int nbAnt,
                        int flags)
{
  AssertFatal(nsamps > 0, "Number of samples must be greater than 0\n");
  AssertFatal(nbAnt > 0 && nbAnt <= OPENAIR0_MAX_ANTENNAS,
              "Number of antennas %d must be between 1 and %d\n",
              nbAnt,
              OPENAIR0_MAX_ANTENNAS);
  AssertFatal(timestamp >= 0, "Timestamp must be non-negative, got %ld\n", timestamp);
  timestamp -= device->openair0_cfg->command_line_sample_advance;
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  bool channel_modelling = vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb;
  if (channel_modelling) {
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    AssertFatal(ret == 0, "clock_gettime failed\n");
    int num_samples_processed = vrtsim_write_with_chanmod(vrtsim_state, timestamp, samplesVoid, nsamps, nbAnt);
    struct timespec end;
    ret = clock_gettime(CLOCK_MONOTONIC, &end);
    AssertFatal(ret == 0, "clock_gettime failed\n");
    double microseconds = (end.tv_sec - ts.tv_sec) * 1e6 + (end.tv_nsec - ts.tv_nsec) / 1e3;
    histogram_add(&vrtsim_state->chanmod_histogram, microseconds);
    return num_samples_processed;
  }

  // No channel model means there is no user-defined mapping between TX and RX antennas.
  // We map the antennas in order: first TX stream is mapped to first RX stream and so on.
  if (vrtsim_state->role == ROLE_CLIENT) {
    for (int aatx = 0; aatx < nbAnt && aatx < vrtsim_state->peer_rx_ant; aatx++) {
      int global_ul_ant = vrtsim_state->ue_id * vrtsim_state->peer_rx_ant + aatx;
      vrtsim_write_internal(vrtsim_state, timestamp, (c16_t *)samplesVoid[aatx], nsamps, global_ul_ant);
    }
    return nsamps;
  } else {
    for (int u = 0; u < vrtsim_state->num_ues; u++) {
      int rx_offset = vrtsim_state->ue_conf[u].rx_offset;
      int num_rx_ant = vrtsim_state->ue_conf[u].rx_ant;
      for (int aarx = 0; aarx < num_rx_ant && aarx < nbAnt; aarx++) {
        int global_dl_ant = rx_offset + aarx;
        vrtsim_write_internal(vrtsim_state, timestamp, (c16_t *)samplesVoid[aarx], nsamps, global_dl_ant);
      }
    }
    return nsamps;
  }
}

static int vrtsim_read(openair0_device_t *device, openair0_timestamp_t *ptimestamp, void **samplesVoid, int nsamps, int nbAnt)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  if (shm_td_iq_channel_is_aborted(vrtsim_state->channel)) {
    return 0;
  }
  if (vrtsim_state->role == ROLE_SERVER) {
    uint64_t timeout_uS = 0; // 0 means no timeout
    shm_td_iq_channel_wait(vrtsim_state->channel, vrtsim_state->last_received_sample + nsamps, timeout_uS);
  } else {
    uint64_t start_sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
    uint64_t timeout_uS = 2 * 1000 * 1000; // 2 seconds timeout waiting for sample number to change
    //
    while (shm_td_iq_channel_wait(vrtsim_state->channel, vrtsim_state->last_received_sample + nsamps, timeout_uS) == 1) {
      uint64_t sample = shm_td_iq_channel_get_current_sample(vrtsim_state->channel);
      if (sample == start_sample) {
        LOG_E(HW,
              "VRTSIM: Read timeout waiting for sample %lu to change, aborting channel\n",
              vrtsim_state->last_received_sample + nsamps);
        shm_td_iq_channel_abort(vrtsim_state->channel);
        break;
      } else {
        start_sample = sample;
      }
    }
  }
  if (vrtsim_state->role == ROLE_SERVER) {
    if (vrtsim_state->num_ues > 1) {
      /* Multi-UE UL combining */
      for (int aarx = 0; aarx < nbAnt; aarx++)
        memset(samplesVoid[aarx], 0, nsamps * sizeof(sample_t));
      for (int u = 0; u < vrtsim_state->num_ues; u++) {
        for (int aarx = 0; aarx < nbAnt; aarx++) {
          int stream = u * nbAnt + aarx;
          sample_t buffer[nsamps];
          int ret = shm_td_iq_channel_rx(vrtsim_state->channel, vrtsim_state->last_received_sample, nsamps, stream, buffer);
          if (ret == CHANNEL_ERROR_TOO_LATE) {
            vrtsim_state->rx_samples_late += nsamps;
          } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
            vrtsim_state->rx_early += 1;
          }
          int16_t *out = (int16_t *)samplesVoid[aarx];
          int16_t *in = (int16_t *)buffer;
          for (int i = 0; i < nsamps * 2; i++) {
            int32_t sum = (int32_t)out[i] + (int32_t)in[i];
            out[i] = (int16_t)((sum > 32767) ? 32767 : (sum < -32768) ? -32768 : sum);
          }
        }
      }
    } else {
      /* Single-UE server UL read */
      for (int aarx = 0; aarx < nbAnt; aarx++) {
        int ret = shm_td_iq_channel_rx(vrtsim_state->channel, vrtsim_state->last_received_sample, nsamps, aarx, samplesVoid[aarx]);
        if (aarx == 0) {
          if (ret == CHANNEL_ERROR_TOO_LATE) {
            vrtsim_state->rx_samples_late += nsamps;
          } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
            vrtsim_state->rx_early += 1;
          }
        }
      }
    }
  } else {
    for (int aarx = 0; aarx < nbAnt; aarx++) {
      int global_dl_ant = vrtsim_state->ue.rx_offset + aarx;
      int ret =
          shm_td_iq_channel_rx(vrtsim_state->channel, vrtsim_state->last_received_sample, nsamps, global_dl_ant, samplesVoid[aarx]);
      if (ret == CHANNEL_ERROR_TOO_LATE) {
        vrtsim_state->rx_samples_late += nsamps;
      } else if (ret == CHANNEL_ERROR_TOO_EARLY) {
        vrtsim_state->rx_early += 1;
      }
    }
  }
  vrtsim_state->rx_samples_total += nsamps;
  *ptimestamp = vrtsim_state->last_received_sample;
  vrtsim_state->last_received_sample += nsamps;
  return nsamps;
}

static void vrtsim_end(openair0_device_t *device)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  if (vrtsim_state->role == ROLE_SERVER && vrtsim_state->run_timing_thread) {
    vrtsim_state->run_timing_thread = false;
    int ret = pthread_join(vrtsim_state->timing_thread, NULL);
    AssertFatal(ret == 0, "pthread_join() failed: errno: %d, %s\n", errno, strerror(errno));
  }

  tx_timing_t *tx_timing = &vrtsim_state->tx_timing;
  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
#ifdef CHANNEL_SIM_CUDA
    cuda_channel_pipeline_shutdown(vrtsim_state->channel_pipeline_context);
#else
    abortTpool(&vrtsim_state->tpool);
#endif
    if (vrtsim_state->use_cirdb) {
      for (int p = 0; p < MAX_NUM_UES; p++) {
        if (vrtsim_state->cirdb_providers[p]) {
          cirdb_stop(vrtsim_state->cirdb_providers[p]);
          vrtsim_state->cirdb_providers[p] = NULL;
        }
      }
#ifdef OAI_VRTSIM_TAPS_CLIENT
    } else if (vrtsim_state->taps_client) {
      taps_client_stop(vrtsim_state->taps_client);
#endif
    }
  }
  shm_td_iq_channel_abort(vrtsim_state->channel);
  usleep(1000);
  shm_td_iq_channel_destroy(vrtsim_state->channel);

  LOG_I(HW,
        "VRTSIM: Realtime issues: TX %.2f%%, RX %.2f%%\n",
        tx_timing->tx_samples_late / (float)tx_timing->tx_samples_total * 100,
        vrtsim_state->rx_samples_late / (float)vrtsim_state->rx_samples_total * 100);
  LOG_I(HW,
        "VRTSIM: Read/write too early (suspected radio implementaton error) TX: %lu, RX: %lu\n",
        tx_timing->tx_early,
        vrtsim_state->rx_early);
  LOG_I(HW, "VRTSIM: Average TX budget %.3lf uS (more is better)\n", tx_timing->average_tx_budget);
  histogram_print(&tx_timing->tx_histogram, "VRTSIM: TX budget histogram");
  if (vrtsim_state->chanmod_histogram.num_samples > 0) {
    LOG_I(HW, "VRTSIM: Channel modelling delay in uS (less is better)\n");
    histogram_print(&vrtsim_state->chanmod_histogram, "VRTSIM: Channel modelling delay histogram");
  }
  if (vrtsim_state->role == ROLE_SERVER) {
    if (vrtsim_state->run_ipc_thread) {
      vrtsim_state->run_ipc_thread = false;
      if (vrtsim_state->ipc_listen_fd >= 0) {
        close(vrtsim_state->ipc_listen_fd);
        vrtsim_state->ipc_listen_fd = -1;
      }
      int ret = pthread_join(vrtsim_state->ipc_thread, NULL);
      AssertFatal(ret == 0, "pthread_join() failed: errno: %d, %s\n", errno, strerror(errno));
    }
    unlink(vrtsim_state->connection_descriptor);
  }
  free(device->priv);
  device->priv = NULL;
}

static int vrtsim_stub(openair0_device_t *device)
{
  return 0;
}

static int vrtsim_stub2(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  return 0;
}

static int vrtsim_set_freq(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  vrtsim_state_t *s = device->priv;
  s->rx_freq = openair0_cfg->rx_freq[0];
  return 0;
}

static int vrtsim_set_beams(openair0_device_t *device, uint16_t *beam_ids, int num_beams, openair0_timestamp_t timestamp)
{
  return 0;
}

__attribute__((__visibility__("default"))) void vrtsim_produce_samples(openair0_device_t *device, size_t num_samples)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  shm_td_iq_channel_produce_samples(vrtsim_state->channel, num_samples);
}

openair0_timestamp_t vrtsim_get_timestamp(openair0_device_t *device, struct timespec *ts)
{
  vrtsim_state_t *vrtsim_state = (vrtsim_state_t *)device->priv;
  int64_t diff = (ts->tv_sec - vrtsim_state->start_ts.tv_sec) * 1000000000 + (ts->tv_nsec - vrtsim_state->start_ts.tv_nsec);
  double diff_samples = vrtsim_state->sample_rate * vrtsim_state->timescale * diff / 1e9;
  return diff_samples;
}

__attribute__((__visibility__("default"))) int device_init(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  randominit();
  vrtsim_state_t *vrtsim_state = calloc_or_fail(1, sizeof(vrtsim_state_t));
  vrtsim_readconfig(vrtsim_state);
  LOG_I(HW,
        "Running as %s\n",
        vrtsim_state->role == ROLE_SERVER ? "server: waiting for client to connect" : "client: will connect to a vrtsim server");
  device->trx_start_func = vrtsim_connect;
  device->trx_reset_stats_func = vrtsim_stub;
  device->trx_end_func = vrtsim_end;
  device->trx_stop_func = vrtsim_stub;
  device->trx_set_freq_func = vrtsim_set_freq;
  device->trx_set_gains_func = vrtsim_stub2;
  device->trx_write_func = vrtsim_write;
  device->trx_read_func = vrtsim_read;
  device->trx_set_beams = vrtsim_set_beams;
  if (vrtsim_state->role == ROLE_SERVER) {
    device->get_timestamp = vrtsim_get_timestamp;
  }

  device->type = RFSIMULATOR;
  device->openair0_cfg = &openair0_cfg[0];
  device->priv = vrtsim_state;
  device->trx_write_init = vrtsim_stub;
  vrtsim_state->last_received_sample = 0;
  vrtsim_state->ipc_listen_fd = -1;
  vrtsim_state->sample_rate = openair0_cfg->sample_rate;
  vrtsim_state->rx_freq = openair0_cfg->rx_freq[0];
  vrtsim_state->tx_bw = openair0_cfg->tx_bw;
  vrtsim_state->tx_num_channels = openair0_cfg->tx_num_channels;
  vrtsim_state->rx_num_channels = openair0_cfg->rx_num_channels;

  if (vrtsim_state->chanmod || vrtsim_state->taps_socket || vrtsim_state->use_cirdb) {
    init_channelmod();
    randominit();
    int noise_power_dBFS = get_noise_power_dBFS();
    int16_t noise_power = noise_power_dBFS == INVALID_DBFS_VALUE ? 0 : (int16_t)(32767.0 / powf(10.0, .05 * -noise_power_dBFS));
    LOG_A(HW, "VRTSIM: Noise power %d sample value\n", noise_power);
#ifndef CHANNEL_SIM_CUDA
    channel_pipeline_init(noise_power);
    initNamedTpool(vrtsim_state->thread_pool_cores, &vrtsim_state->tpool, false, "vrtsim_chanmod");
#endif
  }
  openair0_cfg->rx_gain[0] = 0;
  return 0;
}
