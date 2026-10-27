/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Defines the optimized LDPC encoder for NVidia GPUs
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "assertions.h"
#include "common/utils/LOG/log.h"
#include "time_meas.h"
#include "openair1/PHY/CODING/nrLDPC_defs.h"
#include "PHY/sse_intrin.h"
#include "openair1/PHY/CODING/nrLDPC_extern.h"

#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"

//#define DEBUG_LDPC 1

#include "ldpc_encode_parity_check_cuda.c"

// code blocks of a TB, and groups of 32 of them
#define MAX_SEG MAX_NUM_NR_DLSCH_SEGMENTS
#define MAX_SEGx32 ((MAX_SEG + 31) / 32)

uint32_t *c_dev;
uint32_t **c_host;
uint32_t *c_devh[MAX_SEGx32];
uint32_t *d_dev;
uint32_t **d_host;
uint32_t *d_devh[MAX_SEGx32];
uint32_t *input_dev;
uint32_t **input_host;
uint32_t *input_devh[MAX_SEG];
int managed = 0, concurrent = 0, uva = 0, pageable = 0, pageable_uses_host = 0, register_host = 0, integrated = 0;

int cuda_support_set = 0;

extern gpuStream_t encoderStreams[4];

int ldpc_input(uint32_t **input,uint32_t *cc[4],int nseg,int Zc,int ncols,gpuStream_t *s,int sidx);

void cuda_support_init()
{
  int dev = 0;
  gpuDeviceProp_t prop;
  gpuGetDeviceProperties(&prop, dev);

  gpuDeviceGetAttribute(&managed, gpuDevAttrManagedMemory, dev);
  gpuDeviceGetAttribute(&concurrent, gpuDevAttrConcurrentManagedAccess, dev);
  gpuDeviceGetAttribute(&uva, gpuDevAttrUnifiedAddressing, dev);
  gpuDeviceGetAttribute(&pageable, gpuDevAttrPageableMemoryAccess, dev);
  gpuDeviceGetAttribute(&pageable_uses_host, gpuDevAttrPageableMemoryAccessUsesHostPageTables, dev);
  gpuDeviceGetAttribute(&register_host, gpuDevAttrHostRegisterSupported, dev);
  gpuDeviceGetAttribute(&integrated, gpuDevAttrIntegrated, dev);

  LOG_I(NR_PHY, "Device: %s (cc %d.%d)\n", prop.name, prop.major, prop.minor);
  LOG_I(NR_PHY, "Unified Virtual Addressing (UVA): %s\n", uva ? "YES" : "NO");
  LOG_I(NR_PHY, "Managed (Unified) Memory:        %s\n", managed ? "YES" : "NO");
  LOG_I(NR_PHY, "Concurrent managed access:       %s\n", concurrent ? "YES" : "NO");
  LOG_I(NR_PHY, "Pageable memory access:          %s\n", pageable ? "YES" : "NO");
  LOG_I(NR_PHY, "Uses host page tables:           %s\n", pageable_uses_host ? "YES" : "NO");
  LOG_I(NR_PHY, "Host Register supported:         %s\n", register_host ? "YES" : "NO");
  LOG_I(NR_PHY, "Integrated (shared) Memory       %s\n", integrated ? "YES" : "NO");

  if (!pageable && !integrated) {
    LOG_I(NR_PHY, "Allocating c,d,cc arrays for GPU \n");
    c_dev = gpuMalloc_or_fail(MAX_SEGx32 * sizeof(uint32_t *));
    c_host = gpuHostAlloc_or_fail(MAX_SEGx32 * sizeof(uint32_t *), gpuHostAllocDefault);
    for (int i = 0; i < MAX_SEGx32; i++) {
      c_devh[i] = gpuMalloc_or_fail(2 * 22 * 384 * sizeof(uint32_t));
      c_host[i] = gpuHostAlloc_or_fail(2 * 22 * 384 * sizeof(uint32_t), gpuHostAllocDefault);
    }
    gpuError_t err = gpuMemcpy(c_dev, c_devh, MAX_SEGx32 * sizeof(uint32_t *), gpuMemcpyHostToDevice);
    AssertFatal(err == gpuSuccess, "CUDA Error (memcpy c_devh -> c_dev): %s\n", gpuGetErrorString(err));
    d_dev = gpuMalloc_or_fail(MAX_SEGx32 * sizeof(uint32_t*));
    d_host = gpuHostAlloc_or_fail(MAX_SEGx32 * sizeof(uint32_t*), gpuHostAllocDefault);
    for (int i = 0; i < MAX_SEGx32; i++) {
      d_devh[i] = gpuMalloc_or_fail(68 * 384 * sizeof(uint32_t));
      d_host[i] = gpuHostAlloc_or_fail(68 * 384 * sizeof(uint32_t), gpuHostAllocDefault);
    }
    err = gpuMemcpy(d_dev, d_devh, MAX_SEGx32 * sizeof(uint32_t*), gpuMemcpyHostToDevice);
    AssertFatal(err == gpuSuccess, "CUDA Error (memcpy d_devh -> d_dev): %s\n", gpuGetErrorString(err));
    input_dev = gpuMalloc_or_fail(MAX_SEG * sizeof(uint8_t *));
    input_host = gpuHostAlloc_or_fail(MAX_SEG * sizeof(uint8_t *), gpuHostAllocDefault);
    for (int i = 0; i < MAX_SEG; i++) {
      input_devh[i] = gpuMalloc_or_fail((8448 / 8) * sizeof(uint8_t));
      input_host[i] = gpuHostAlloc_or_fail((8448 / 8) * sizeof(uint8_t), gpuHostAllocDefault);
    }
    err = gpuMemcpy(input_dev, input_devh, MAX_SEG * sizeof(uint8_t *), gpuMemcpyHostToDevice);
    AssertFatal(err == gpuSuccess, "CUDA Error (memcpy cc_devh -> d_dev): %s\n", gpuGetErrorString(err));
  } else {
    LOG_I(NR_PHY, "Allocating c,d,cc arrays for CPU/GPU shared-memory\n");
    c_host = gpuHostAlloc_or_fail(MAX_SEGx32 * sizeof(uint32_t *), gpuHostAllocMapped | gpuHostAllocPortable);
    c_dev = gpuHostGetDevicePointer_or_fail(c_host);
    LOG_I(NR_PHY, "c_host %p, c_dev %p\n", c_host, c_dev);
    for (int i = 0; i < MAX_SEGx32; i++) {
      c_host[i] = gpuHostAlloc_or_fail(2 * 22 * 384 * sizeof(uint32_t), gpuHostAllocMapped);
      c_devh[i] = gpuHostGetDevicePointer_or_fail(c_host[i]);
    }
    gpuError_t err = gpuMemcpy(c_dev, c_devh, MAX_SEGx32 * sizeof(uint32_t *), gpuMemcpyHostToDevice);
    AssertFatal(err == gpuSuccess, "CUDA Error (memcpy c_devh -> c_dev): %s\n", gpuGetErrorString(err));
    d_host = gpuHostAlloc_or_fail(MAX_SEGx32 * sizeof(uint32_t *), gpuHostAllocMapped);
    d_dev = gpuHostGetDevicePointer_or_fail(d_host);
    LOG_I(NR_PHY, "d_host %p, d_dev %p\n", d_host, d_dev);
    for (int i = 0; i < MAX_SEGx32; i++) {
      d_host[i] = gpuHostAlloc_or_fail(68 * 384 * sizeof(uint32_t), gpuHostAllocMapped);
      d_devh[i] = gpuHostGetDevicePointer_or_fail(d_host[i]);
      LOG_I(NR_PHY, "d_host[%d] %p, d_devh[%d] %p\n", i, d_host[i], i, d_devh[i]);
    }
    err = gpuMemcpy(d_dev, d_devh, MAX_SEGx32 * sizeof(uint32_t*), gpuMemcpyHostToDevice);
    AssertFatal(err == gpuSuccess, "CUDA Error (memcpy d_devh -> d_dev): %s\n", gpuGetErrorString(err));
    input_host = gpuHostAlloc_or_fail(MAX_SEG * sizeof(uint8_t *), gpuHostAllocMapped);
    input_dev = gpuHostGetDevicePointer_or_fail(input_host);
    LOG_I(NR_PHY, "input_host %p, input_dev %p\n", input_host, input_dev);
  }

  cuda_support_set = 1;
}

pthread_mutex_t encoder_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t** LDPCencoder32(uint8_t** input, encoder_implemparams_t* impp)
{
  // set_log(PHY, 4);

  int Zc = impp->Zc;
  int Kb = impp->Kb;
  short block_length = impp->K;
  short BG = impp->BG;
  int ncols = 22;

  int encoder_stream = 0;

  AssertFatal(BG == 1, "BG %d is not supported for CUDA version\n", BG);
  // the supported set of lifting sizes is the one for which a parity check
  // kernel has been generated, see encode_parity_check_part_cuda() below

  if (impp->tinput != NULL)
    start_meas(impp->tinput);

#ifdef DEBUG_LDPC
  LOG_I(PHY,
        "ldpc_encoder_cuda32: BG %d, Zc %d, Kb %d, block_length %d, segments %d\n",
        BG,
        Zc,
        Kb,
        block_length,
        impp->n_segments);
  LOG_I(PHY, "ldpc_encoder_cuda32: PDU (seg 0) %x %x %x %x\n", input[0][0], input[0][1], input[0][2], input[0][3]);
#endif

  int n_inputs = (impp->n_segments / 32) + (((impp->n_segments & 31) > 0) ? 1 : 0);
  AssertFatal(impp->n_segments <= MAX_SEG,
              "encoder buffers allocated for %d code blocks, but requested %d\n",
              MAX_SEG,
              impp->n_segments);
  //  uint32_t  cc[4][22*Zc]; //padded input, unpacked, max size

  int ret = pthread_mutex_lock(&encoder_mutex);
  AssertFatal(ret == 0, "pthread_mutex_lock(): ret %d, errno %d, %s\n", ret, errno, strerror(errno));
  if (!pageable && !integrated) { // this means we are not on shared memory
    for (int r = 0; r < impp->n_segments; r++) {
      gpuMemcpyAsync_or_fail(input_devh[r], input[r], block_length >> 3, gpuMemcpyHostToDevice, encoderStreams[encoder_stream]);
    }
  }
  ldpc_input(pageable || integrated ? (uint32_t**)input : (uint32_t**)input_dev,
             (uint32_t**)c_dev,
             impp->n_segments,
             Zc,
             ncols,
             encoderStreams,
             encoder_stream);
  if (impp->tinput != NULL)
    stop_meas(impp->tinput);
  // parity check part
  if (impp->tparity != NULL)
    start_meas(impp->tparity);
  encode_parity_check_part_cuda((uint32_t**)c_dev, (uint32_t**)d_dev, BG, Zc, Kb, ncols, n_inputs, encoderStreams, encoder_stream);
  if (!pageable && !integrated) { // this means we are not on shared memory
    for (int r = 0; r < n_inputs; r++)
      gpuMemcpyAsync_or_fail(d_host[r], d_devh[r], 68 * Zc * sizeof(uint32_t), gpuMemcpyDeviceToHost, encoderStreams[encoder_stream]);
  }
  gpuStreamSynchronize(encoderStreams[encoder_stream]);
  if (impp->tparity != NULL)
    stop_meas(impp->tparity);
  ret = pthread_mutex_unlock(&encoder_mutex);
  AssertFatal(ret == 0, "pthread_mutex_unlock(): ret %d, errno %d, %s\n", ret, errno, strerror(errno));

  return d_host;
}
