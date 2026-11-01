/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "assertions.h"
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include "noise_device.h"

// Use block size equal to cache size
#define BLOCK_SIZE (64 / sizeof(float)) // 4 bytes per float
// Use a power of 2 for block size length
#define NUM_BLOCKS (8192)
#define NOISE_VECTOR_LENGTH (BLOCK_SIZE * NUM_BLOCKS)

struct noise_device_s {
  float noise[NOISE_VECTOR_LENGTH] __attribute__((aligned(64)));
  float noise_power;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t stop_cond;
  bool running; // protected by lock
  // Picks the start block of each read; shared by every thread reading from the device.
  _Atomic uint32_t counter;
  uint64_t rng;
};

// Random start block, safe to call from several threads at once.
static int random_block(noise_device_t *dev)
{
  uint32_t x = atomic_fetch_add_explicit(&dev->counter, 0x9e3779b9u, memory_order_relaxed);
  x ^= x >> 16;
  x *= 0x85ebca6bu;
  x ^= x >> 13;
  return x % NUM_BLOCKS;
}

static uint64_t splitmix64_next(uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// Two independent standard normal samples from one 64-bit uniform draw.
static void box_muller(uint64_t uniform, double *z0, double *z1)
{
  double u1 = ((uniform >> 32) + 1.0) / 4294967296.0; // (0, 1]
  double u2 = (uint32_t)uniform / 4294967296.0; // [0, 1)
  double r = sqrt(-2.0 * log(u1));
  *z0 = r * cos(2 * M_PI * u2);
  *z1 = r * sin(2 * M_PI * u2);
}

static void generate_one_block_noise(noise_device_t *dev, int write_block_index)
{
  float block[BLOCK_SIZE];
  for (int i = 0; i < BLOCK_SIZE; i += 2) {
    double z0, z1;
    box_muller(splitmix64_next(&dev->rng), &z0, &z1);
    block[i] = dev->noise_power * z0;
    block[i + 1] = dev->noise_power * z1;
  }
  memcpy(dev->noise + write_block_index * BLOCK_SIZE, block, sizeof(block));
}

// Keeps rewriting a random block of the table every 10 ms while readers use it.
static void *noise_device_thread_function(void *arg)
{
  noise_device_t *dev = arg;
  pthread_mutex_lock(&dev->lock);
  while (dev->running) {
    pthread_mutex_unlock(&dev->lock);
    generate_one_block_noise(dev, random_block(dev));
    pthread_mutex_lock(&dev->lock);
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += 10 * 1000 * 1000;
    if (t.tv_nsec >= 1000 * 1000 * 1000) {
      t.tv_sec++;
      t.tv_nsec -= 1000 * 1000 * 1000;
    }
    if (dev->running)
      pthread_cond_timedwait(&dev->stop_cond, &dev->lock, &t);
  }
  pthread_mutex_unlock(&dev->lock);
  return NULL;
}

noise_device_t *init_noise_device(float noise_power)
{
  noise_device_t *dev = aligned_alloc(64, (sizeof(*dev) + 63) / 64 * 64);
  AssertFatal(dev, "out of memory\n");
  dev->noise_power = noise_power;
  dev->counter = 123456789;
  static _Atomic uint64_t num_devices;
  dev->rng = atomic_fetch_add(&num_devices, 1);
  for (int i = 0; i < NUM_BLOCKS; i++) {
    generate_one_block_noise(dev, i);
  }
  pthread_mutex_init(&dev->lock, NULL);
  pthread_cond_init(&dev->stop_cond, NULL);
  dev->running = true;
  int ret = pthread_create(&dev->thread, NULL, noise_device_thread_function, dev);
  AssertFatal(ret == 0, "pthread_create failed: %d, errno %d (%s)\n", ret, errno, strerror(errno));
  return dev;
}

void free_noise_device(noise_device_t *dev)
{
  pthread_mutex_lock(&dev->lock);
  dev->running = false;
  pthread_cond_signal(&dev->stop_cond);
  pthread_mutex_unlock(&dev->lock);
  int ret = pthread_join(dev->thread, NULL);
  AssertFatal(ret == 0, "pthread_join failed: %d, errno %d (%s)\n", ret, errno, strerror(errno));
  pthread_cond_destroy(&dev->stop_cond);
  pthread_mutex_destroy(&dev->lock);
  free(dev);
}

void get_noise_vector(noise_device_t *dev, float *noise_vector, int length)
{
  int start_block = random_block(dev);
  while (length > 0) {
    int copy_size = min(length, (NUM_BLOCKS - start_block) * BLOCK_SIZE);
    memcpy(noise_vector, &dev->noise[start_block * BLOCK_SIZE], copy_size * sizeof(float));
    start_block = 0;
    length -= copy_size;
    noise_vector += copy_size;
  }
}
