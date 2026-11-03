/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Decoder contexts of the CUDA LDPC coding library: types and functions shared by the host code
 * (nrLDPC_decoder_cuda.c, nrLDPC_coding_segment_decoder_cuda.c) and the device code (nrLDPC_decoder_BG1_cuda.cu)
 */

#ifndef NRLDPC_CODING_CUDA_CTX_H_
#define NRLDPC_CODING_CUDA_CTX_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "PHY/gpu_compat.h"
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"
#include "nrLDPC_CUDA_shared_param.h"

#define LDPC_CUDA_CTX_GRAPHS 64 // chunk graphs per context (CRC check on the host)
#define LDPC_ET_CTX_GRAPHS 256 // early-termination graphs per context
// et_host / et_dev: pass_it, then the output
#define LDPC_ET_PASS_IT_BYTES (MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * sizeof(int32_t))

// Where work is enqueued: a context's own stream, or the background recorder's for graph capture
typedef struct {
  gpuStream_t *streams; // the stream is streams[slot]
  uint8_t slot; // stream index and launch-dimension slot (Kdim_* arrays)
  gpuStream_t side_stream; // side branch: CRC checks next to the check-node kernels
  gpuEvent_t ev_fork, ev_join;
} ldpc_cuda_lane_t;

// Early-termination graphs are recorded per (Z, R, bucket of the number of code blocks, iteration budget): a handful
// serve every TB size. A shape seen for the first time is decoded with direct launches while the background recorder
// records its graphs, used from then on.
typedef enum { ET_GRAPH_PENDING, ET_GRAPH_READY, ET_GRAPH_FAILED } ldpc_et_graph_state_t;
typedef struct {
  uint32_t Z;
  uint8_t R, n_segments; // n_segments: the bucket
  int budget, chunk;
  uint32_t numLLR;
  ldpc_et_graph_state_t state;
  uint64_t last_use; // LRU eviction
  // first chunk, then the remaining ones (NULL if the budget is a single chunk)
  gpuGraph_t graph[2];
  gpuGraphExec_t exec[2];
} ldpc_et_graph_t;

typedef struct {
  uint32_t Z, numLLR;
  uint8_t R, n_segments, prologue, iters;
  gpuGraph_t graph;
  gpuGraphExec_t exec;
} ldpc_chunk_graph_t;

typedef struct {
  pthread_mutex_t mutex;
  ldpc_cuda_lane_t lane; // decoderStreams[context index], its launch-dimension slot, side stream and events
  int8_t *cnProcBuf, *bnProcBuf, *llrRes, *llrProcBuf; // decoder working buffers (device)
  int8_t *llr; // decoder input: code block r at r * 68 * 384 (device)
  int8_t *out; // hard decisions, C * K/8 bytes (device)
  uint8_t *out_host; // pinned mirror of out
  int16_t *harq_e; // deinterleaver output (device)
  int16_t *harq_f; // input LLRs copied to the device (discrete GPUs only)
  ldpc_cuda_bridge_t *bridge; // mapped host memory read by the kernels: llr / out pointers
  ldpc_chunk_graph_t graphs[LDPC_CUDA_CTX_GRAPHS];
  int n_graphs;
  // device-side early termination
  ldpc_cuda_et_state_t *st; // (device)
  // et_host: pass_it (iterations after which each code block passed, 0 if it did not), then the hard decision of each
  // code block from the chunk where it first passed. Pinned host memory, written by the GPU directly (zero copy) on GPUs
  // coherent with the host, else copied from et_dev at the end of the decode (et_copy).
  uint8_t *et_host;
  uint8_t *et_dev; // device view of et_host (zero copy) or device buffer
  bool et_copy;
  gpuEvent_t ev_first; // end of the first part of a decode
  pthread_mutex_t et_graphs_mutex; // et_graphs is shared with the background recorder
  ldpc_et_graph_t et_graphs[LDPC_ET_CTX_GRAPHS];
  int n_et_graphs;
  uint64_t et_uses;
} ldpc_cuda_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

// Device code (nrLDPC_decoder_BG1_cuda.cu)

/// @brief Early-terminating decode of shape g in context c, enqueued on lane: recorded as the two graphs of g if
/// record, else launched directly (c->ev_first recorded after the first part)
gpuError_t nrLDPC_decoder_cuda_EnqueueET(ldpc_cuda_ctx_t *c, ldpc_et_graph_t *g, const ldpc_cuda_lane_t *lane, bool record);
/// @brief Per-decode setup of the early termination (TB parameters, padding code blocks), before the decode
void nrLDPC_decoder_cuda_ETSetup(ldpc_cuda_ctx_t *c,
                                 int C,
                                 int Cb,
                                 uint32_t nbytes,
                                 uint32_t crc_deg,
                                 uint32_t crc_low,
                                 const uint32_t *xpow);
/// @brief Chunk of the decode with the CRC check on the host, recorded as a graph
gpuError_t nrLDPC_decoder_cuda_GraphRecordChunk(ldpc_cuda_ctx_t *c,
                                                uint32_t Z,
                                                uint8_t R,
                                                uint32_t numLLR,
                                                uint8_t n_segments,
                                                int prologue,
                                                int iters,
                                                gpuGraph_t *graph,
                                                gpuGraphExec_t *exec);
/// @brief Same chunk, launched directly
void nrLDPC_decoder_cuda_NormalExecuteChunk(ldpc_cuda_ctx_t *c,
                                            uint32_t Z,
                                            uint8_t R,
                                            uint32_t numLLR,
                                            uint8_t n_segments,
                                            int prologue,
                                            int iters);

// Host code (nrLDPC_decoder_cuda.c), used by the segment decoder

void ldpc_cuda_ctx_init(void);
void ldpc_cuda_ctx_shutdown(void);
int ldpc_cuda_ctx_acquire(void);
void ldpc_cuda_ctx_release(int ci);
int8_t *ldpc_cuda_ctx_llr(int ci);
int16_t *ldpc_cuda_ctx_harq_e(int ci);
int16_t *ldpc_cuda_ctx_harq_f(int ci);
int LDPCdecoder_cuda_ctx(int ci, t_nrLDPC_dec_params *p_decParams, uint8_t *p_out, bool *passed);

#ifdef __cplusplus
}
#endif

#endif /* NRLDPC_CODING_CUDA_CTX_H_ */
