/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file radio/emulator/rf_emulator.c
 * \brief RF library that does nothing to be used in benchmarks with phy-test without RF
 */

#include <errno.h>
#include <string.h>

#include <common/utils/assertions.h>
#include <common/utils/LOG/log.h>
#include "common/utils/threadPool/pthread_utils.h"
#include "common_lib.h"
#include "noise_device.h"
#include "simde/x86/avx512.h"
#include "SIMULATION/TOOLS/sim.h"
#include "common/config/config_userapi.h"

#define RF_EMULATOR_SECTION "rf_emulator"
// clang-format off
#define RF_EMULATOR_PARAMS_DESC \
  { \
     {"enable_noise",     "Enable noise injection", 0, .iptr = &emulator_state->enable_noise,     .defintval = 1,                  TYPE_INT, 0},\
     {"noise_level_dBFS", "Noise level",            0, .iptr = &emulator_state->noise_level_dBFS, .defintval = INVALID_DBFS_VALUE, TYPE_INT, 0},\
  };
// clang-format on

// structures and timing thread job for timing
typedef struct {
  uint64_t timestamp;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  uint64_t late_reads;
  uint64_t late_writes;
} emulator_timestamp_t;

typedef struct {
  uint64_t last_received_sample;
  double timescale;
  double sample_rate;
  emulator_timestamp_t emulator_timestamp;
  pthread_t timing_thread;
  bool run_timing_thread;
  int enable_noise;
  int noise_level_dBFS;
  noise_device_t *noise_device;
} emulator_state_t;

static void *emulator_timing_job(void *arg)
{
  emulator_state_t *emulator_state = (emulator_state_t *)arg;
  struct timespec timestamp;
  if (clock_gettime(CLOCK_REALTIME, &timestamp)) {
    LOG_E(UTIL, "clock_gettime failed\n");
    exit(1);
  }
  double leftover_samples = 0;
  while (emulator_state->run_timing_thread) {
    struct timespec current_time;
    if (clock_gettime(CLOCK_REALTIME, &current_time)) {
      LOG_E(UTIL, "clock_gettime failed\n");
      exit(1);
    }

    emulator_timestamp_t *emulator_timestamp = &emulator_state->emulator_timestamp;
    if (current_time.tv_sec != timestamp.tv_sec) {
      if (emulator_timestamp->late_reads != 0) {
        LOG_W(HW, "%ld RF reads were late\n", emulator_timestamp->late_reads);
      }
      emulator_timestamp->late_reads = 0;
      if (emulator_timestamp->late_writes != 0) {
        LOG_W(HW, "%ld RF writes were late\n", emulator_timestamp->late_writes);
      }
      emulator_timestamp->late_writes = 0;
    }

    uint64_t diff = (current_time.tv_sec - timestamp.tv_sec) * 1000000000 + (current_time.tv_nsec - timestamp.tv_nsec);
    timestamp = current_time;
    double samples_to_produce = emulator_state->sample_rate * emulator_state->timescale * diff / 1e9;

    // Attempt to correct compounding rounding error
    leftover_samples += samples_to_produce - (uint64_t)samples_to_produce;
    if (leftover_samples > 1.0f) {
      samples_to_produce += 1;
      leftover_samples -= 1;
    }

    mutexlock(emulator_timestamp->mutex);
    emulator_timestamp->timestamp += samples_to_produce;
    condbroadcast(emulator_timestamp->cond);
    mutexunlock(emulator_timestamp->mutex);

    usleep(1);
  }
  return 0;
}

/*! \brief Called to start the RF transceiver. Return 0 if OK, < 0 if error
 * \param device pointer to the device structure specific to the RF hardware target
 */
static int emulator_start(openair0_device_t *device)
{
  // Start the timing thread and the noise device
  emulator_state_t *emulator_state = (emulator_state_t *)device->priv;
  if (!emulator_state->run_timing_thread) {
    if (emulator_state->enable_noise > 0) {
      float noise_level = emulator_state->noise_level_dBFS == INVALID_DBFS_VALUE
                                ? 0.0f
                                : (32767.0 / powf(10.0, .05 * -(emulator_state->noise_level_dBFS)));
      emulator_state->noise_device = init_noise_device(noise_level);
      LOG_I(HW, "Using noise level with %d dBFS/%f\n", emulator_state->noise_level_dBFS, noise_level);
    }

    emulator_timestamp_t *emulator_timestamp = &emulator_state->emulator_timestamp;
    memset(emulator_timestamp, 0, sizeof(emulator_timestamp_t));

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    int ret = pthread_mutexattr_init(&mutex_attr);
    AssertFatal(ret == 0, "pthread_mutexattr_init() failed: errno %d, %s\n", errno, strerror(errno));

    ret = pthread_condattr_init(&cond_attr);
    AssertFatal(ret == 0, "pthread_condattr_init() failed: errno %d, %s\n", errno, strerror(errno));

    ret = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    AssertFatal(ret == 0, "pthread_mutexattr_setpshared() failed: errno %d, %s\n", errno, strerror(errno));

    ret = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    AssertFatal(ret == 0, "pthread_condattr_setpshared() failed: errno %d, %s\n", errno, strerror(errno));

    ret = pthread_mutex_init(&emulator_timestamp->mutex, &mutex_attr);
    AssertFatal(ret == 0, "pthread_mutex_init() failed: errno %d, %s\n", errno, strerror(errno));

    ret = pthread_cond_init(&emulator_timestamp->cond, &cond_attr);
    AssertFatal(ret == 0, "pthread_cond_init() failed: errno %d, %s\n", errno, strerror(errno));

    emulator_state->run_timing_thread = true;
    ret = pthread_create(&emulator_state->timing_thread, NULL, emulator_timing_job, emulator_state);
    AssertFatal(ret == 0, "pthread_create() failed: errno: %d, %s\n", errno, strerror(errno));
  }

  return 0;
}

/*! \brief print the RF statistics
 * \param device pointer to the device structure specific to the RF hardware target
 * \returns  0 on success
 */
int emulator_get_stats(openair0_device_t *device)
{
  return (0);
}

/*! \brief Reset the RF statistics
 * \param device pointer to the device structure specific to the RF hardware target
 * \returns  0 on success
 */
int emulator_reset_stats(openair0_device_t *device)
{
  return (0);
}

/*! \brief Terminate operation of the RF transceiver -- free all associated resources (if any)
 * \param pointer to the device structure specific to the RF hardware target
 */
static void emulator_end(openair0_device_t *device)
{
  // Stop the timing thread and the noise device
  emulator_state_t *emulator_state = (emulator_state_t *)device->priv;
  if (emulator_state->run_timing_thread) {
    emulator_state->run_timing_thread = false;
    int ret = pthread_join(emulator_state->timing_thread, NULL);
    AssertFatal(ret == 0, "pthread_join() failed: errno: %d, %s\n", errno, strerror(errno));
    if (emulator_state->enable_noise > 0) {
      free_noise_device(emulator_state->noise_device);
      emulator_state->noise_device = NULL;
    }
  }
}

/*! \brief Stop RF
 * \param device pointer to the device structure specific to the RF hardware target
 */
int emulator_stop(openair0_device_t *device)
{
  return (0);
}

/*! \brief Set Gains (TX/RX)
 * \param device pointer to the device structure specific to the RF hardware target
 * \param openair0_cfg RF frontend parameters set by application
 * \returns 0 in success
 */
int emulator_set_gains(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  return (0);
}

/*! \brief Set frequencies (TX/RX).
 * \param device pointer to the device structure specific to the RF hardware target
 * \param openair0_cfg RF frontend parameters set by application
 * \returns 0 in success
 */
int emulator_set_freq(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  return (0);
}

int emulator_write_init(openair0_device_t *device)
{
  return (0);
}

/*! \brief Called to send samples to the RF target
 * \param device pointer to the device structure specific to the RF hardware target
 * \param timestamp The timestamp at which the first sample MUST be sent
 * \param buff Buffer which holds the samples
 * \param nsamps number of samples to be sent
 * \param nbAnt number of antennas
 * \param flags flags must be set to true if timestamp parameter needs to be applied
 */
static int emulator_write(openair0_device_t *device, openair0_timestamp_t timestamp, void **buff, int nsamps, int nbAnt, int flags)
{
  emulator_state_t *emulator_state = (emulator_state_t *)device->priv;

  // timestamp in the past
  emulator_timestamp_t *emulator_timestamp = &emulator_state->emulator_timestamp;
  uint64_t current_timestamp = emulator_timestamp->timestamp;
  if (timestamp < current_timestamp) {
    mutexlock(emulator_timestamp->mutex);
    emulator_timestamp->late_writes++;
    mutexunlock(emulator_timestamp->mutex);
  }

  return 0;
}

/*! \brief Store noise in the buffer buff
 * \param[out] buff A buffer for received samples.
 * The buffers must be large enough to hold the number of samples \ref nsamps.
 * \param nsamps Number of samples. One sample is 2 byte I + 2 byte Q => 4 byte.
 */
static void read_noise(noise_device_t *noise_device, void *buff, int nsamps)
{
  int aligned_nsamps = ceil_mod(nsamps, (512 / 8) / sizeof(cf_t));
  cf_t samples[aligned_nsamps] __attribute__((aligned(64)));
  // Apply noise from global settings
  get_noise_vector(noise_device, (float *)samples, nsamps * 2);

  // Convert to c16_t
  c16_t samples_out[aligned_nsamps] __attribute__((aligned(64)));
#if defined(__AVX512F__)
  for (int i = 0; i < aligned_nsamps / 8; i++) {
    simde__m512 *in = (simde__m512 *)&samples[i * 8];
    simde__m256i *out = (simde__m256i *)&samples_out[i * 8];
    *out = simde_mm512_cvtsepi32_epi16(simde_mm512_cvtps_epi32(*in));
  }
#elif defined(__AVX2__)
  for (int i = 0; i < aligned_nsamps / 4; i++) {
    simde__m256 *in = (simde__m256 *)&samples[i * 4];
    simde__m128i *out = (simde__m128i *)&samples_out[i * 4];
    *out = simde_mm256_cvtsepi32_epi16(simde_mm256_cvtps_epi32(*in));
  }
#else
  for (int i = 0; i < nsamps; i++) {
    samples_out[i].r = lroundf(samples[i].r);
    samples_out[i].i = lroundf(samples[i].i);
  }
#endif
  memcpy(buff, samples_out, nsamps * sizeof(c16_t));
}

/*! \brief Receive samples from hardware.
 * Read \ref nsamps samples from each channel to buffers. buff[0] is the array for
 * the first channel.
 * \param[out] ptimestamp time at which the first sample was received.
 * \param device pointer to the device structure specific to the RF hardware target
 * \param[out] ptimestamp the time at which the first sample was received.
 * \param[out] buff An array of pointers to buffers for received samples. The buffers must be large enough to hold the number of
 * samples \ref nsamps. \param nsamps Number of samples. One sample is 2 byte I + 2 byte Q => 4 byte.
 * \param nbAnt number of antennas
 * \returns the number of samples read
 */
static int emulator_read(openair0_device_t *device, openair0_timestamp_t *ptimestamp, void **buff, int nsamps, int nbAnt)
{
  emulator_state_t *emulator_state = (emulator_state_t *)device->priv;

  emulator_timestamp_t *emulator_timestamp = &emulator_state->emulator_timestamp;
  uint64_t timestamp = emulator_state->last_received_sample + nsamps;
  uint64_t current_timestamp = emulator_timestamp->timestamp;
  if (current_timestamp < timestamp) {
    mutexlock(emulator_timestamp->mutex);
    while (current_timestamp < timestamp) {
      condwait(emulator_timestamp->cond, emulator_timestamp->mutex);
      current_timestamp = emulator_timestamp->timestamp;
    }
    mutexunlock(emulator_timestamp->mutex);
  } else {
    mutexlock(emulator_timestamp->mutex);
    emulator_timestamp->late_reads++;
    mutexunlock(emulator_timestamp->mutex);
  }
  *ptimestamp = emulator_state->last_received_sample;
  emulator_state->last_received_sample += nsamps;

  if (emulator_state->enable_noise > 0) {
    for (int i = 0; i < nbAnt; i++) {
      read_noise(emulator_state->noise_device, buff[i], nsamps);
    }
  } else {
    for (int i = 0; i < nbAnt; i++) {
      memset(buff[i], 0, nsamps * sizeof(c16_t));
    }
  }

  return nsamps;
}

static void emulator_readconfig(emulator_state_t *emulator_state)
{
  paramdef_t emulator_params[] = RF_EMULATOR_PARAMS_DESC;
  int ret = config_get(config_get_if(), emulator_params, sizeofArray(emulator_params), RF_EMULATOR_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed\n");
}

int device_init(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  LOG_I(HW, "This is emulator RF that does nothing\n");

  emulator_state_t *emulator_state = calloc_or_fail(1, sizeof(emulator_state_t));
  emulator_state->last_received_sample = 0;
  emulator_state->timescale = 1.0;
  emulator_state->sample_rate = openair0_cfg->sample_rate;
  emulator_state->run_timing_thread = false;

  device->priv = emulator_state;
  device->openair0_cfg = openair0_cfg;
  device->trx_start_func = emulator_start;
  device->trx_get_stats_func = emulator_get_stats;
  device->trx_reset_stats_func = emulator_reset_stats;
  device->trx_end_func = emulator_end;
  device->trx_stop_func = emulator_stop;
  device->trx_set_freq_func = emulator_set_freq;
  device->trx_set_gains_func = emulator_set_gains;
  device->trx_write_init = emulator_write_init;
  device->type = RFSIMULATOR;
  device->trx_write_func = emulator_write;
  device->trx_read_func = emulator_read;

  emulator_readconfig(emulator_state);
  if (emulator_state->enable_noise)
    randominit(); // for noise device

  return 0;
}
