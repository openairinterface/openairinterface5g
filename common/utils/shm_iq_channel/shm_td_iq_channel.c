/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "shm_td_iq_channel.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "assertions.h"
#include "utils.h"
#include "common/utils/threadPool/pthread_utils.h"

#define CIRCULAR_BUFFER_SIZE (30720 * 20)

typedef struct {
  int magic;
  int num_antennas_tx;
  int num_antennas_rx;
  _Atomic uint64_t timestamp;
  _Atomic uint32_t wake_seq;
} ShmTDIQChannelData;

typedef struct ShmTDIQChannel_s {
  IQChannelType type;
  ShmTDIQChannelData *data;
  char name[256];
  sample_t *tx_iq_data;
  sample_t *rx_iq_data;
  int nb_tx_ant;
  int nb_rx_ant;
  _Atomic bool abort;
} ShmTDIQChannel;

ShmTDIQChannel *shm_td_iq_channel_create(const char *name, int num_tx_ant, int num_rx_ant)
{
  // Unlink any stale shared memory segment first to ensure we start fresh and reclaim space
  shm_unlink(name);

  // Create shared memory segment
  int fd = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  AssertFatal(fd != -1, "shm_open failed: %s\n", strerror(errno));
  size_t tx_buffer_size = CIRCULAR_BUFFER_SIZE * sizeof(sample_t) * num_tx_ant;
  size_t rx_buffer_size = CIRCULAR_BUFFER_SIZE * sizeof(sample_t) * num_rx_ant;
  size_t total_size = sizeof(ShmTDIQChannelData) + tx_buffer_size + rx_buffer_size;

  // Set the size of the shared memory segment
  int res = ftruncate(fd, total_size);
  AssertFatal(res != -1, "ftruncate failed: %s\n", strerror(errno));

  // Map shared memory segment to address space
  ShmTDIQChannelData *shm_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  AssertFatal(shm_ptr != MAP_FAILED, "mmap failed: %s\n", strerror(errno));

  // Initialize shared memory
  memset(shm_ptr, 0, total_size);
  shm_ptr->num_antennas_tx = num_tx_ant;
  shm_ptr->num_antennas_rx = num_rx_ant;
  ShmTDIQChannel *channel = calloc_or_fail(1, sizeof(ShmTDIQChannel));
  strncpy(channel->name, name, sizeof(channel->name) - 1);
  channel->tx_iq_data = (sample_t *)(shm_ptr + 1);
  channel->rx_iq_data = channel->tx_iq_data + tx_buffer_size / sizeof(sample_t);
  channel->nb_tx_ant = num_tx_ant;
  channel->nb_rx_ant = num_rx_ant;
  channel->data = shm_ptr;
  channel->type = IQ_CHANNEL_TYPE_SERVER;

  shm_ptr->magic = SHM_MAGIC_NUMBER;
  close(fd);
  return channel;
}

ShmTDIQChannel *shm_td_iq_channel_connect(const char *name, int timeout_in_seconds)
{
  // Create shared memory segment
  int fd = -1;
  double timeout_in_uS = timeout_in_seconds * 1000000.0;
  while (timeout_in_uS > 0 && fd == -1) {
    for (int i = 0; i < 1000; i++) {
      fd = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR);
      if (fd != -1) {
        break;
      }
      usleep(1000);
      timeout_in_uS -= 1000;
    }
    if (fd == -1) {
      printf("Waiting for server to create shared memory segment\n");
    }
  }
  AssertFatal(fd != -1, "shm_open() failed: errno %d, %s", errno, strerror(errno));

  // Map just the header first to wait for initialization
  ShmTDIQChannelData *shm_ptr = mmap(NULL, sizeof(ShmTDIQChannelData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm_ptr == MAP_FAILED) {
    perror("mmap header");
    exit(1);
  }

  // Wait until server initializes the segment
  while (timeout_in_uS > 0 && shm_ptr->magic != SHM_MAGIC_NUMBER) {
    usleep(1000);
    timeout_in_uS -= 1000;
  }
  AssertFatal(shm_ptr->magic == SHM_MAGIC_NUMBER, "Timeout waiting for server to initialize shared memory segment\n");

  // Now that it's initialized, ftruncate has finished, so we can get the actual size
  struct stat buf;
  fstat(fd, &buf);
  size_t total_size = buf.st_size;

  // Unmap the temporary header mapping
  munmap(shm_ptr, sizeof(ShmTDIQChannelData));

  // Map the entire shared memory segment
  shm_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm_ptr == MAP_FAILED) {
    perror("mmap full");
    exit(1);
  }

  ShmTDIQChannel *channel = calloc_or_fail(1, sizeof(ShmTDIQChannel));
  channel->data = shm_ptr;
  sample_t *tx_buffer = (sample_t *)(shm_ptr + 1);
  size_t tx_buffer_size = CIRCULAR_BUFFER_SIZE * sizeof(sample_t) * channel->data->num_antennas_tx;
  sample_t *rx_buffer = tx_buffer + tx_buffer_size / sizeof(sample_t);
  // Flip the order between TX and RX to simplify tx/rx functions
  channel->tx_iq_data = rx_buffer;
  channel->rx_iq_data = tx_buffer;
  channel->nb_tx_ant = channel->data->num_antennas_rx;
  channel->nb_rx_ant = channel->data->num_antennas_tx;
  printf("\033[38;5;208mnb_tx_ant, nb_rx_ant: %d, %d\n\033[0m", channel->nb_tx_ant, channel->nb_rx_ant);
  channel->type = IQ_CHANNEL_TYPE_CLIENT;
  close(fd);
  return channel;
}

static int shm_futex_wait(_Atomic uint32_t *address, uint32_t expected, const struct timespec *timeout)
{
  if (syscall(SYS_futex, address, FUTEX_WAIT, expected, timeout, NULL, 0) == 0)
    return 0;
  return errno;
}

static void shm_futex_wake(_Atomic uint32_t *address)
{
  int ret = syscall(SYS_futex, address, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
  AssertFatal(ret >= 0, "futex wake failed: %d %s\n", errno, strerror(errno));
}

static bool shm_remaining_timeout(const struct timespec *deadline, struct timespec *remaining)
{
  struct timespec now;
  int ret = clock_gettime(CLOCK_MONOTONIC, &now);
  AssertFatal(ret == 0, "clock_gettime failed: %d %s\n", errno, strerror(errno));
  if (now.tv_sec > deadline->tv_sec || (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec))
    return false;
  remaining->tv_sec = deadline->tv_sec - now.tv_sec;
  remaining->tv_nsec = deadline->tv_nsec - now.tv_nsec;
  if (remaining->tv_nsec < 0) {
    remaining->tv_sec--;
    remaining->tv_nsec += 1000000000;
  }
  return true;
}

IQChannelErrorType shm_td_iq_channel_tx(ShmTDIQChannel *channel,
                                        uint64_t timestamp,
                                        uint64_t num_samples,
                                        int antenna,
                                        const sample_t *tx_iq_data)
{
  AssertFatal(antenna < channel->nb_tx_ant, "Invalid antenna index %d num_antennas %d\n", antenna, channel->nb_tx_ant);
  ShmTDIQChannelData *data = channel->data;
  // timestamp in the past
  uint64_t current_time = atomic_load_explicit(&data->timestamp, memory_order_relaxed);
  if (timestamp < current_time) {
    return CHANNEL_ERROR_TOO_LATE;
  }
  // timestamp is too far in the future
  if (timestamp - current_time + num_samples >= CIRCULAR_BUFFER_SIZE) {
    return CHANNEL_ERROR_TOO_EARLY;
  }

  sample_t *base_ptr = channel->tx_iq_data + antenna * CIRCULAR_BUFFER_SIZE;

  uint64_t first_sample = timestamp % CIRCULAR_BUFFER_SIZE;
  uint64_t last_sample = first_sample + num_samples - 1;
  if (last_sample >= CIRCULAR_BUFFER_SIZE) {
    size_t num_samples_first_copy = CIRCULAR_BUFFER_SIZE - first_sample;
    memcpy(base_ptr + first_sample, tx_iq_data, num_samples_first_copy * sizeof(sample_t));
    memcpy(base_ptr, tx_iq_data + num_samples_first_copy, (num_samples - num_samples_first_copy) * sizeof(sample_t));
  } else {
    memcpy(base_ptr + first_sample, tx_iq_data, num_samples * sizeof(sample_t));
  }
  return CHANNEL_NO_ERROR;
}

IQChannelErrorType shm_td_iq_channel_rx(ShmTDIQChannel *channel,
                                        uint64_t timestamp,
                                        uint64_t num_samples,
                                        int antenna,
                                        sample_t *tx_iq_data)
{
  AssertFatal(antenna < channel->nb_rx_ant, "Invalid antenna index %d num_antennas %d\n", antenna, channel->nb_rx_ant);
  ShmTDIQChannelData *data = channel->data;
  // timestamp in the future
  uint64_t current_time = atomic_load_explicit(&data->timestamp, memory_order_relaxed);
  if (timestamp > current_time) {
    return CHANNEL_ERROR_TOO_EARLY;
  }
  // timestamp is too far in the past
  if (current_time - timestamp >= CIRCULAR_BUFFER_SIZE) {
    return CHANNEL_ERROR_TOO_LATE;
  }

  sample_t *base_ptr = channel->rx_iq_data + antenna * CIRCULAR_BUFFER_SIZE;

  uint64_t first_sample = timestamp % CIRCULAR_BUFFER_SIZE;
  uint64_t last_sample = first_sample + num_samples - 1;
  if (last_sample >= CIRCULAR_BUFFER_SIZE) {
    size_t num_samples_first_copy = CIRCULAR_BUFFER_SIZE - first_sample;
    memcpy(tx_iq_data, base_ptr + first_sample, num_samples_first_copy * sizeof(sample_t));
    memcpy(tx_iq_data + num_samples_first_copy, base_ptr, (num_samples - num_samples_first_copy) * sizeof(sample_t));
    memset(base_ptr + first_sample, 0, num_samples_first_copy * sizeof(sample_t));
    memset(base_ptr, 0, (num_samples - num_samples_first_copy) * sizeof(sample_t));
  } else {
    memcpy(tx_iq_data, base_ptr + first_sample, num_samples * sizeof(sample_t));
    memset(base_ptr + first_sample, 0, num_samples * sizeof(sample_t));
  }
  return CHANNEL_NO_ERROR;
}

void shm_td_iq_channel_produce_samples(ShmTDIQChannel *channel, size_t num_samples)
{
  ShmTDIQChannelData *data = channel->data;
  atomic_fetch_add_explicit(&data->timestamp, num_samples, memory_order_relaxed);
  atomic_fetch_add_explicit(&data->wake_seq, 1, memory_order_release);
  shm_futex_wake(&data->wake_seq);
}

int shm_td_iq_channel_wait(ShmTDIQChannel *channel, uint64_t timestamp, uint64_t timeout_uS)
{
  ShmTDIQChannelData *data = channel->data;
  struct timespec deadline;
  if (timeout_uS != 0) {
    int ret = clock_gettime(CLOCK_MONOTONIC, &deadline);
    AssertFatal(ret == 0, "clock_gettime failed: %d %s\n", errno, strerror(errno));
    deadline.tv_sec += timeout_uS / 1000000;
    deadline.tv_nsec += (timeout_uS % 1000000) * 1000;
    if (deadline.tv_nsec >= 1000000000) {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000;
    }
  }

  for (;;) {
    /* The acquire load pairs with the release increment in produce and abort. Sampling the
     * sequence before the abort check closes both orderings: an abort that landed first is
     * visible here, and one that lands after changes the word so FUTEX_WAIT returns EAGAIN. */
    uint32_t wake_seq = atomic_load_explicit(&data->wake_seq, memory_order_acquire);
    if (atomic_load_explicit(&channel->abort, memory_order_relaxed))
      return 0;
    if (atomic_load_explicit(&data->timestamp, memory_order_relaxed) >= timestamp)
      return 0;

    struct timespec remaining;
    const struct timespec *wait_timeout = NULL;
    if (timeout_uS != 0) {
      if (!shm_remaining_timeout(&deadline, &remaining))
        return 1;
      wait_timeout = &remaining;
    }

    int ret = shm_futex_wait(&data->wake_seq, wake_seq, wait_timeout);
    if (ret == 0 || ret == EAGAIN || ret == EINTR)
      continue;
    if (ret == ETIMEDOUT)
      return 1;
    fprintf(stderr, "futex wait failed: %s\n", strerror(ret));
    return 1;
  }
}

uint64_t shm_td_iq_channel_get_current_sample(const ShmTDIQChannel *channel)
{
  ShmTDIQChannelData *data = channel->data;
  return atomic_load_explicit(&data->timestamp, memory_order_relaxed);
}

void shm_td_iq_channel_abort(ShmTDIQChannel *channel)
{
  ShmTDIQChannelData *data = channel->data;
  atomic_store_explicit(&channel->abort, true, memory_order_relaxed);
  /* A wake on an unchanged word is lost if the waiter samples the word after this wake and
   * before it sleeps. The increment makes that sleep return EAGAIN. With timeout 0 the
   * waiter would otherwise stay blocked, which is the shutdown hang. */
  atomic_fetch_add_explicit(&data->wake_seq, 1, memory_order_release);
  shm_futex_wake(&data->wake_seq);
}

bool shm_td_iq_channel_is_aborted(const ShmTDIQChannel *channel)
{
  return atomic_load_explicit(&channel->abort, memory_order_relaxed);
}

void shm_td_iq_channel_destroy(ShmTDIQChannel *channel)
{
  ShmTDIQChannelData *data = channel->data;
  size_t tx_buffer_size = CIRCULAR_BUFFER_SIZE * sizeof(sample_t) * data->num_antennas_tx;
  size_t rx_buffer_size = CIRCULAR_BUFFER_SIZE * sizeof(sample_t) * data->num_antennas_rx;
  size_t total_size = sizeof(ShmTDIQChannelData) + tx_buffer_size + rx_buffer_size;
  if (channel->type == IQ_CHANNEL_TYPE_SERVER) {
    munmap(data, total_size);
    shm_unlink(channel->name);
  } else {
    munmap(data, total_size);
  }
  free(channel);
}
