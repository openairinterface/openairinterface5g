/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "taps_generated.h"
#include "taps_client.h"
#include "SIMULATION/TOOLS/sim.h"
extern "C" {
#include "assertions.h"
#include "common/utils/LOG/log.h"
#include "sim.h"
}
#include <cfloat>
#include <cmath>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#define NUM_TAPS_BUFFERS 16
#define MAX_NUM_IDS 4
#define MAX_TX_RX_ANTENNAS 8
#define MAX_TAPS_MSG_SIZE (sizeof(struct complexf) * MAX_TAPS_LEN * MAX_TX_RX_ANTENNAS * MAX_TX_RX_ANTENNAS + 256)

struct taps_client_t {
  int sock_;
  std::atomic<bool> should_run_;

  // Raw message buffer pool
  std::vector<std::unique_ptr<uint8_t[]>> buffer_storage_;
  std::queue<uint8_t *> free_buffers_;

  // Per-ID incoming queue and buffer currently backing channel_desc ch_ps
  std::queue<uint8_t *> read_buffers_[MAX_NUM_IDS];
  uint8_t *current_buffers_[MAX_NUM_IDS];

  // Per-ID channel descriptors with pre-allocated ch_ps arrays
  channel_desc_t channel_descs_[MAX_NUM_IDS];
  struct complexf *ch_ps_storage_[MAX_NUM_IDS][MAX_TX_RX_ANTENNAS * MAX_TX_RX_ANTENNAS];

  // Last geometry announced per ID, so a steady tap stream is not logged on every update
  uint32_t logged_taps_len_[MAX_NUM_IDS];
  uint32_t logged_num_rx_[MAX_NUM_IDS];
  uint32_t logged_num_tx_[MAX_NUM_IDS];

  std::mutex mutex_;
  std::thread thread_;

  uint num_tx_ant_;
  uint num_rx_ant_;

  taps_client_t(const char *socket_path, int num_tx_ant, int num_rx_ant)
      : should_run_(true), num_tx_ant_(num_tx_ant), num_rx_ant_(num_rx_ant)
  {
    memset(current_buffers_, 0, sizeof(current_buffers_));
    memset(channel_descs_, 0, sizeof(channel_descs_));
    memset(logged_taps_len_, 0, sizeof(logged_taps_len_));
    memset(logged_num_rx_, 0, sizeof(logged_num_rx_));
    memset(logged_num_tx_, 0, sizeof(logged_num_tx_));

    for (int i = 0; i < MAX_NUM_IDS; i++)
      channel_descs_[i].ch_ps = ch_ps_storage_[i];

    buffer_storage_.reserve(NUM_TAPS_BUFFERS);
    for (int i = 0; i < NUM_TAPS_BUFFERS; i++) {
      buffer_storage_.push_back(std::make_unique<uint8_t[]>(MAX_TAPS_MSG_SIZE));
      free_buffers_.push(buffer_storage_.back().get());
    }

    sock_ = nn_socket(AF_SP, NN_SUB);
    AssertFatal(sock_ >= 0, "nn_socket() failed: errno: %d, %s\n", errno, strerror(errno));

    int ret = nn_connect(sock_, socket_path);
    AssertFatal(ret >= 0, "nn_connect() failed: errno: %d, %s\n", errno, strerror(errno));

    ret = nn_setsockopt(sock_, NN_SUB, NN_SUB_SUBSCRIBE, "", 0);
    AssertFatal(ret == 0, "nn_setsockopt() failed: errno: %d, %s\n", errno, strerror(errno));

    thread_ = std::thread(&taps_client_t::run, this);
  }

  ~taps_client_t()
  {
    should_run_ = false;
    thread_.join();
    nn_close(sock_);
  }

  void run()
  {
    while (should_run_) {
      uint8_t *buf = nullptr;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!free_buffers_.empty()) {
          buf = free_buffers_.front();
          free_buffers_.pop();
        }
      }
      if (!buf) {
        usleep(100);
        continue;
      }

      int ret = nn_recv(sock_, buf, MAX_TAPS_MSG_SIZE, NN_DONTWAIT);
      if (ret < 0) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          free_buffers_.push(buf);
        }
        if (errno != EAGAIN)
          LOG_E(HW, "nn_recv() failed: errno: %d, %s\n", errno, strerror(errno));
        usleep(100);
        continue;
      }

      auto *msg = Phy::GetTaps(buf);
      uint32_t id = msg->id();
      if (id >= MAX_NUM_IDS) {
        LOG_E(HW, "Received taps message with invalid id %u (max %d)\n", id, MAX_NUM_IDS - 1);
        std::lock_guard<std::mutex> lock(mutex_);
        free_buffers_.push(buf);
        continue;
      }
      uint32_t num_rx = msg->num_rx_antennas();
      uint32_t num_tx = msg->num_tx_antennas();
      if (num_tx != num_tx_ant_ || num_rx != num_rx_ant_) {
        LOG_E(HW,
              "Mismatch between received and expected channel model %d: %ux%u vs %ux%u\n",
              id,
              num_rx,
              num_tx,
              num_rx_ant_,
              num_tx_ant_);
        std::lock_guard<std::mutex> lock(mutex_);
        free_buffers_.push(buf);
        continue;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      read_buffers_[id].push(buf);
    }
  }

  channel_desc_t *get_model(int id, bool *updated)
  {
    *updated = false;
    if (id < 0 || id >= MAX_NUM_IDS)
      return nullptr;

    uint8_t *buf = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (read_buffers_[id].empty()) {
        if (current_buffers_[id]) {
          return &channel_descs_[id];
        } else {
          return nullptr;
        }
      }
      buf = read_buffers_[id].front();
      read_buffers_[id].pop();
    }

    auto *msg = Phy::GetTaps(buf);
    uint32_t num_rx = msg->num_rx_antennas();
    uint32_t num_tx = msg->num_tx_antennas();
    uint32_t taps_len = msg->taps_len();

    AssertFatal(taps_len <= MAX_TAPS_LEN, "Too many taps: %u (max %d)\n", taps_len, MAX_TAPS_LEN);

    auto *base = (struct complexf *)msg->taps()->data();
    channel_desc_t *desc = &channel_descs_[id];
    desc->nb_rx = num_rx;
    desc->nb_tx = num_tx;
    desc->nb_taps = taps_len;

    for (uint32_t aarx = 0; aarx < num_rx; aarx++) {
      for (uint32_t aatx = 0; aatx < num_tx; aatx++) {
        desc->ch_ps[aarx + num_rx * aatx] = &base[(aarx + num_rx * aatx) * taps_len];
      }
    }
    desc->path_loss_dB = 0;
    desc->channel_length = taps_len;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (current_buffers_[id])
        free_buffers_.push(current_buffers_[id]);
      current_buffers_[id] = buf;
    }

    /* Called from the vrtsim sample path, so this runs once per slot that has taps pending —
     * 10/s for a 100 ms publisher. An always-printed line here is a blocking write to the
     * container's stdout pipe, which is enough to overrun the slot deadline and show up as
     * dropped PDSCH PDUs, stale UL configs and PBCH failures. Announce the geometry only when
     * it actually changes (that is what catches a publisher/RU antenna mismatch); the steady
     * stream stays at LOG_D. */
    if (taps_len != logged_taps_len_[id] || num_rx != logged_num_rx_[id] || num_tx != logged_num_tx_[id]) {
      logged_taps_len_[id] = taps_len;
      logged_num_rx_[id] = num_rx;
      logged_num_tx_[id] = num_tx;
      LOG_A(HW, "New taps for id %d: channel_length %u, %ux%u antennas\n", id, taps_len, num_rx, num_tx);
    } else {
      LOG_D(HW, "New taps for id %d: channel_length %u, %ux%u antennas\n", id, taps_len, num_rx, num_tx);
    }

    *updated = true;
    return desc;
  }
};

extern "C" void *taps_client_connect(const char *socket_path, int num_tx_ant, int num_rx_ant)
{
  return new taps_client_t(socket_path, num_tx_ant, num_rx_ant);
}

extern "C" channel_desc_t *taps_client_get_model(void *handle, int id, bool *updated)
{
  return static_cast<taps_client_t *>(handle)->get_model(id, updated);
}

extern "C" void taps_client_stop(void *handle)
{
  delete static_cast<taps_client_t *>(handle);
}
