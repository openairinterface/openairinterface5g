/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NOISE_DEVICE_H
#define NOISE_DEVICE_H

#include "SIMULATION/TOOLS/sim.h"

// A table of Gaussian noise with standard deviation noise_power, rewritten in the background by its
// own thread. Reads may run concurrently from any number of threads.
typedef struct noise_device_s noise_device_t;

noise_device_t *init_noise_device(float noise_power);
void free_noise_device(noise_device_t *dev);
void get_noise_vector(noise_device_t *dev, float *noise_vector, int length);

#endif
