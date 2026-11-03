/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Configuration of the CUDA LDPC coding library (config module section nrLDPC_coding_cuda)
 */

#ifndef NRLDPC_CODING_CUDA_CONFIG_H_
#define NRLDPC_CODING_CUDA_CONFIG_H_

#define LDPC_CUDA_CONFIG_SECTION "nrLDPC_coding_cuda"
#define LDPC_CUDA_MAX_CTX 8

// how a host thread waits for the GPU (gpuStreamSynchronize() and alike)
typedef enum { LDPC_CUDA_WAIT_SPIN, LDPC_CUDA_WAIT_YIELD, LDPC_CUDA_WAIT_BLOCK } ldpc_cuda_wait_mode_t;
// where the code block CRCs are checked during decoding
typedef enum { LDPC_CUDA_CRC_CHECK_HOST, LDPC_CUDA_CRC_CHECK_GPU } ldpc_cuda_crc_check_t;

typedef struct {
  ldpc_cuda_wait_mode_t wait_mode;
  int num_contexts; // decoder contexts: transport blocks decoded concurrently
  int crc_check_interval; // decoder iterations between two code block CRC checks (early termination)
  ldpc_cuda_crc_check_t crc_check;
} ldpc_cuda_config_t;

/// names of the ldpc_cuda_wait_mode_t values in the configuration
extern const char *const ldpc_cuda_wait_mode_names[];

/// @brief Configuration of the library, read from the config module on the first call. Not thread-safe: called during
/// the library initialization only.
const ldpc_cuda_config_t *ldpc_cuda_get_config(void);

#endif /* NRLDPC_CODING_CUDA_CONFIG_H_ */
