/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Defines the CUDA version of nrLDPC decoder, including initialization and driver warmup mechanisms.
 * \note Optimized for NVIDIA GH200 (Grace Hopper) architecture using Zero-Copy access.
 */

#include <stdint.h>
#include "PHY/sse_intrin.h"
#include "log.h"
#include "openair1/PHY/CODING/coding_defs.h"

#include "openair1/PHY/CODING/nrLDPC_extern.h"

#ifdef NR_LDPC_DEBUG_MODE
#include "nrLDPC_tools/nrLDPC_debug.h"
#endif

// decoder interface
/**
   \brief LDPC decoder API type definition
   \param p_decParams LDPC decoder parameters
   \param p_llr Input LLRs
   \param p_llrOut Output vector
   \param p_profiler LDPC profiler statistics
*/

//--------------------------CUDA Area---------------------------
#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"
#include "nrLDPC_CUDA_shared_param.h"

_Static_assert(MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 >= NR_LDPC_MAX_NUM_CB, "the CUDA buffers must hold the code blocks of a TB");

extern gpuStream_t decoderStreams[MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4];
static bool decoder_streamsCreated = false;
static volatile int cuda_graph_breaker = 0;// 0 by default to enable the CUDA graph
gpuError_t Err;

int8_t* cnProcBuf_dev;
int8_t* bnProcBuf_dev;
int8_t* llrRes_dev;
int8_t* llrProcBuf_dev;

int8_t* p_llr_dev;
int8_t* p_out_dev;

extern int pageable, integrated;

int cuda_support_init_decoder()
{
  // use gpuMalloc for all inner buffers
  cnProcBuf_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_SIZE_CN_PROC_BUF);
  bnProcBuf_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_SIZE_BN_PROC_BUF);
  llrRes_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_MAX_NUM_LLR);
  llrProcBuf_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_MAX_NUM_LLR);
  p_llr_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_MAX_NUM_LLR);
  gpuMemset(p_llr_dev, 0, sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_MAX_NUM_LLR);
  p_out_dev = gpuMalloc_or_fail(sizeof(int8_t) * MAX_NUM_NR_DLSCH_SEGMENTS_PER_LAYER * 4 * NR_LDPC_MAX_NUM_LLR);

  printf("[CUDA] Intermediate buffers allocated in Device Memory.\n");

  return 0;
}

static ldpc_cuda_bridge_t* stream_bridges[8];

extern gpuError_t nrLDPC_decoder_cuda_GraphRecord(ldpc_cuda_bridge_t* buffer,
                                                   uint32_t numLLR,
                                                   int8_t* cnProcBuf,
                                                   int8_t* bnProcBuf,
                                                   int8_t* llrRes,
                                                   int8_t* llrProcBuf,
                                                   uint32_t Z,
                                                   uint32_t K,
                                                   uint8_t BG,
                                                   uint8_t R,
                                                   uint8_t numMaxIter,
                                                   uint8_t n_segments,
                                                   e_nrLDPC_outMode outMode,
                                                   gpuStream_t* streams,
                                                   uint8_t CudaStreamIdx,
                                                   gpuGraph_t* graphPtr,
                                                   gpuGraphExec_t* graphExecPtr,
                                                   uint8_t* isCreatedFlag);

extern gpuError_t nrLDPC_decoder_cuda_GraphExecute(gpuGraphExec_t graphExec,
                                                    gpuStream_t stream,
                                                    gpuEvent_t* doneEvent,
                                                    uint8_t CudaStreamIdx);

extern void nrLDPC_decoder_cuda_NormalExecute(ldpc_cuda_bridge_t* buffer,
                                              uint32_t numLLR,
                                              int8_t* cnProcBuf,
                                              int8_t* bnProcBuf,
                                              int8_t* llrRes,
                                              int8_t* llrProcBuf,
                                              uint32_t Z,
                                              uint32_t K,
                                              uint8_t BG,
                                              uint8_t R,
                                              uint8_t numMaxIter,
                                              uint8_t n_segments,
                                              e_nrLDPC_outMode outMode,
                                              gpuStream_t* streams,
                                              uint8_t CudaStreamIdx,
                                              gpuEvent_t* doneEvent);

static inline uint32_t nrLDPC_decoder_core_dynamic(int8_t* p_llr,
                                                   int8_t* p_out,
                                                   int n_segments,
                                                   t_nrLDPC_dec_params* p_decParams,
                                                   t_nrLDPC_time_stats* p_profiler,
                                                   decode_abort_t* ab);
#define MAX_GRAPH_CACHE_SIZE 16
#define PRE_RECORDED_COUNT 6
#define STATIC_SEG_SIZE 9 // n_segments in pre-record graphs, should be determined for real cases

typedef struct {
  uint32_t Z;
  uint32_t K;
  uint32_t numLLR;
  uint8_t R;
  uint8_t BG;
  uint8_t numMaxIter;
  uint16_t n_segments;
  e_nrLDPC_outMode outMode;
  gpuGraph_t graph;
  gpuGraphExec_t exec;
  ldpc_cuda_bridge_t* bridge_ptr;
  bool occupied;
} gpu_graph_node_t;

static gpu_graph_node_t gpu_graph_cache[MAX_GRAPH_CACHE_SIZE];
static int dynamic_cache_idx = PRE_RECORDED_COUNT;

void init_decoder_warmup()
{
  // =====================================================================
  // CUDA Driver Warm-up
  // Purpose: Execute a few representative graphs to trigger CUDA context
  // initialization and driver-level JIT/lazy loading.
  // Note: These specific Z/R combinations might not match the actual
  // run-time traffic, but running them ensures the GPU pipeline is ready.
  // =====================================================================

  gpuError_t err_warmup = gpuSuccess;

  // Sample configurations for warmup
  uint32_t Z_list[] = {320, 352, 384};
  uint8_t R_list[] = {13, 23};
  int node_idx = 0;

  int8_t* dummy_input_llr = NULL;
  int8_t* dummy_output_bits = NULL;
  uint32_t max_z = 384;
  uint32_t max_n_segs = STATIC_SEG_SIZE;

  size_t input_size_bytes = 68 * max_z * max_n_segs * sizeof(int8_t);
  size_t output_size_bytes = 8448 * max_n_segs * sizeof(int8_t);

  // Allocate Pinned/Mapped Memory for Zero-Copy access (GH200 friendly)
  dummy_input_llr = gpuHostAlloc_or_fail(input_size_bytes, gpuHostAllocMapped);
  dummy_output_bits = gpuHostAlloc_or_fail(output_size_bytes, gpuHostAllocMapped);

  memset(dummy_input_llr, 0, input_size_bytes);
  memset(dummy_output_bits, 0, output_size_bytes);

  printf("[CUDA] Initializing & Warming up Driver Pipeline...\n");
  if (cuda_graph_breaker == 0) {
    for (int r_idx = 0; r_idx < 2; r_idx++) {
      for (int z_idx = 0; z_idx < 3; z_idx++) {
        uint32_t Z = Z_list[z_idx];
        uint8_t R = R_list[r_idx];
        uint8_t BG = 1;
        uint32_t K = 22 * Z;
        uint32_t numLLR = (R == 13) ? NR_LDPC_NCOL_BG1_R13 * Z : NR_LDPC_NCOL_BG1_R23 * Z;
        uint8_t numMaxIter = 2; // 2 iterations for faster warmup
        uint8_t n_segments = STATIC_SEG_SIZE;

        // Bind dummy buffers
        gpu_graph_cache[node_idx].bridge_ptr->p_llr_ptr = dummy_input_llr;
        gpu_graph_cache[node_idx].bridge_ptr->p_out_ptr = dummy_output_bits;

        // Record graph
        err_warmup = nrLDPC_decoder_cuda_GraphRecord(gpu_graph_cache[node_idx].bridge_ptr,
                                                     numLLR,
                                                     cnProcBuf_dev,
                                                     bnProcBuf_dev,
                                                     llrRes_dev,
                                                     llrProcBuf_dev,
                                                     Z,
                                                     K,
                                                     BG,
                                                     R,
                                                     numMaxIter,
                                                     n_segments,
                                                     nrLDPC_outMode_BIT,
                                                     decoderStreams,
                                                     0,
                                                     &gpu_graph_cache[node_idx].graph,
                                                     &gpu_graph_cache[node_idx].exec,
                                                     (uint8_t*)&gpu_graph_cache[node_idx].occupied);
        if (err_warmup != gpuSuccess) {
          cuda_graph_breaker = 1; // Once a graph recording fails, forbidden the graph recording permanently
          printf("[CUDA] Warmup Graph Record Failed (err=%s). Circuit breaker triggered\n", gpuGetErrorString(err_warmup));
          break;
        }

        // Save metadata
        gpu_graph_cache[node_idx].Z = Z;
        gpu_graph_cache[node_idx].R = R;
        gpu_graph_cache[node_idx].K = K;
        gpu_graph_cache[node_idx].numLLR = numLLR;
        gpu_graph_cache[node_idx].BG = BG;
        gpu_graph_cache[node_idx].numMaxIter = numMaxIter;
        gpu_graph_cache[node_idx].n_segments = n_segments;
        gpu_graph_cache[node_idx].outMode = nrLDPC_outMode_BIT;

        node_idx++;
      }
    }
    dynamic_cache_idx = node_idx;

    // Execute to trigger driver initialization
    if (dynamic_cache_idx > 0) {
      for (int i = 0; i < dynamic_cache_idx; i++) {
        if (gpu_graph_cache[i].occupied) {
          nrLDPC_decoder_cuda_GraphExecute(gpu_graph_cache[i].exec, decoderStreams[0], NULL, 0);
        }
      }
      gpuDeviceSynchronize();
      printf("[CUDA] Driver warm-up complete. Executed %d dummy graphs.\n", dynamic_cache_idx);

    for (int i = 0; i < dynamic_cache_idx; i++) {
        if (gpu_graph_cache[i].occupied) {
            if (gpu_graph_cache[i].exec) {
                gpuGraphExecDestroy(gpu_graph_cache[i].exec);
                gpu_graph_cache[i].exec = NULL;
            }
            if (gpu_graph_cache[i].graph) {
                gpuGraphDestroy(gpu_graph_cache[i].graph);
                gpu_graph_cache[i].graph = NULL;
            }
            
            if (gpu_graph_cache[i].bridge_ptr) {
                gpu_graph_cache[i].bridge_ptr->p_llr_ptr = NULL;
                gpu_graph_cache[i].bridge_ptr->p_out_ptr = NULL;
            }
            
            gpu_graph_cache[i].occupied = false;
        }
    }

      // Mark slots as free and reset index so real traffic starts from slot 0
      for (int i = 0; i < dynamic_cache_idx; i++) {
        gpu_graph_cache[i].occupied = false;
      }
      printf("[CUDA] Cache cleared. Ready for dynamic recording.\n");
    }
  }
  dynamic_cache_idx = 0;

  if (cuda_graph_breaker == 1) {
    printf("[CUDA] Using Normal Execution for Warmup (Graph Disabled).\n");
    for (int r_idx = 0; r_idx < 2; r_idx++) {
      for (int z_idx = 0; z_idx < 3; z_idx++) {
        uint32_t Z = Z_list[z_idx];
        uint8_t R = R_list[r_idx];
        uint8_t BG = 1;
        uint32_t K = 22 * Z;
        uint32_t numLLR = (R == 13) ? NR_LDPC_NCOL_BG1_R13 * Z : NR_LDPC_NCOL_BG1_R23 * Z;
        uint8_t numMaxIter = 2; // 2 iterations for faster warmup
        uint8_t n_segments = STATIC_SEG_SIZE;

        // Bind dummy buffers
        gpu_graph_cache[0].bridge_ptr->p_llr_ptr = dummy_input_llr;
        gpu_graph_cache[0].bridge_ptr->p_out_ptr = dummy_output_bits;

        // normal execution
        nrLDPC_decoder_cuda_NormalExecute(gpu_graph_cache[0].bridge_ptr,
                                          numLLR,
                                          cnProcBuf_dev,
                                          bnProcBuf_dev,
                                          llrRes_dev,
                                          llrProcBuf_dev,
                                          Z,
                                          K,
                                          BG,
                                          R,
                                          numMaxIter,
                                          n_segments,
                                          nrLDPC_outMode_BIT,
                                          decoderStreams,
                                          0,
                                          NULL);
      }
    }
    gpuDeviceSynchronize();
    printf("[CUDA] Driver warm-up complete with normal execution\n");
  }
  gpuFreeHost(dummy_input_llr);
  gpuFreeHost(dummy_output_bits);
}
void init_decoder_gpu_structures()
{
  printf("[CUDA] Initializing Global GPU Structures...\n");
  // Bridge for graphs
  for (int i = 0; i < MAX_GRAPH_CACHE_SIZE; i++) {
    if (gpu_graph_cache[i].bridge_ptr == NULL) {
      gpu_graph_cache[i].bridge_ptr = gpuHostAlloc_or_fail(sizeof(ldpc_cuda_bridge_t), gpuHostAllocMapped);

      gpu_graph_cache[i].bridge_ptr->p_llr_ptr = NULL;
      gpu_graph_cache[i].bridge_ptr->p_out_ptr = NULL;
      gpu_graph_cache[i].occupied = false;
    }
  }
  printf("[CUDA] Allocated %d Graph Bridges.\n", MAX_GRAPH_CACHE_SIZE);
  // Bridge for normal execute
  for (int i = 0; i < 8; i++) {
    if (stream_bridges[i] == NULL) {
      stream_bridges[i] = gpuHostAlloc_or_fail(sizeof(ldpc_cuda_bridge_t), gpuHostAllocMapped);
      stream_bridges[i]->p_llr_ptr = NULL;
      stream_bridges[i]->p_out_ptr = NULL;
    }
  }
  printf("[CUDA] Allocated %d Stream Bridges for Fallback case.\n", 8);
}

void init_decoder_graphs()
{
  for (int i = 0; i < MAX_GRAPH_CACHE_SIZE; i++) {
    gpu_graph_cache[i].occupied = false;
    gpu_graph_cache[i].graph = NULL;
    gpu_graph_cache[i].exec = NULL;
    gpu_graph_cache[i].bridge_ptr = NULL;
    gpu_graph_cache[i].Z = 0;
    gpu_graph_cache[i].R = 0;
  }

  dynamic_cache_idx = 0;

  printf("[decoder_graphs] initialized %d dynamic cache slots\n", MAX_GRAPH_CACHE_SIZE);
}

void free_graphs()
{
  for (int i = 0; i < MAX_GRAPH_CACHE_SIZE; i++) {
    if (gpu_graph_cache[i].occupied) {
      if (gpu_graph_cache[i].exec)
        gpuGraphExecDestroy(gpu_graph_cache[i].exec);
      if (gpu_graph_cache[i].graph)
        gpuGraphDestroy(gpu_graph_cache[i].graph);
      gpu_graph_cache[i].occupied = false;
    }
  }
  printf("[decoder_graphs] shutdown complete (Dynamic Cache Cleared)\n");
}

extern int cuda_support_set;

bool encoder_streamsCreated = false;
gpuStream_t encoderStreams[4];
void cuda_support_init();

int32_t LDPCinit_cuda()
{
  if (cuda_support_set == 0) {
    printf("Calling encoder initializations\n");
    cuda_support_init();
  }
  if (!decoder_streamsCreated) {
    for (int s = 0; s < 8; ++s) {
      gpuStreamCreateWithFlags(&decoderStreams[s], gpuStreamNonBlocking);
    }
    decoder_streamsCreated = true;
  }

  if (!encoder_streamsCreated) {
    for (int s = 0; s < 4; ++s) {
      gpuStreamCreateWithFlags(&encoderStreams[s], gpuStreamNonBlocking);
    }
    encoder_streamsCreated = true;
  }
  printf("CUDA LDPC decoder initiating\n");
  cuda_support_init_decoder();
  init_decoder_graphs();
  init_decoder_gpu_structures();
  init_decoder_warmup();
  return 0;
}

int32_t LDPCshutdown_cuda()
{
  if (cnProcBuf_dev) { gpuFree(cnProcBuf_dev); cnProcBuf_dev = NULL; }
  if (bnProcBuf_dev) { gpuFree(bnProcBuf_dev); bnProcBuf_dev = NULL; }
  if (llrRes_dev)   { gpuFree(llrRes_dev);   llrRes_dev = NULL; }
  if (llrProcBuf_dev) { gpuFree(llrProcBuf_dev); llrProcBuf_dev = NULL; }

  if (p_llr_dev)   { gpuFree(p_llr_dev);   p_llr_dev = NULL; }
  if (p_out_dev)   { gpuFree(p_out_dev);   p_out_dev = NULL; }

  for (int i = 0; i < MAX_GRAPH_CACHE_SIZE; i++) {
    if (gpu_graph_cache[i].bridge_ptr) {
        gpuFreeHost(gpu_graph_cache[i].bridge_ptr);
        gpu_graph_cache[i].bridge_ptr = NULL;
    }
}
  for (int i = 0; i < 8; i++) {
    if (stream_bridges[i]) {
        gpuFreeHost(stream_bridges[i]);
        stream_bridges[i] = NULL;
    }
}

  for (int s = 0; s < 8; ++s) {
    if (decoder_streamsCreated) {
      gpuStreamDestroy(decoderStreams[s]);
    }
  }

  for (int s = 0; s < 4; s++) {
    if (encoder_streamsCreated) {
      gpuStreamDestroy(encoderStreams[s]);
    }
  }

  free_graphs();

  decoder_streamsCreated = false;
  encoder_streamsCreated = false;

  printf("[CUDA] Intermediate buffers and streams destroyed.\n");

  return 0;
}

int32_t LDPCdecoder_cuda(t_nrLDPC_dec_params* p_decParams,
                         int8_t* p_llr,
                         uint8_t* p_out,
                         t_nrLDPC_time_stats* p_profiler,
                         decode_abort_t* ab)
{
  if (!((p_decParams->R == 89 || p_decParams->R == 23 || p_decParams->R == 13) && p_decParams->BG == 1 && p_decParams->Z % 4 == 0
        && p_decParams->Z >= 128 && p_decParams->Z <= 384)) { // format check
    printf("Current format: BG = %d, R = %d, Zc = %d\n", p_decParams->BG, p_decParams->R, p_decParams->Z);
    AssertFatal(false, "Format cuda not support, only support BG = 1, Zc >= 128 and R = 13, 23, 89 right now\n");
    return 0;
  }
  // Launch LDPC decoder core for all segments
  int n_segments = p_decParams->n_segments;

  int numIter = nrLDPC_decoder_core_dynamic(p_llr, (int8_t*)p_out, n_segments, p_decParams, p_profiler, ab);

  set_abort(ab, false);

  return numIter;
}

/**
   \brief PerformsnrLDPC decoding of one code block
   \param p_llr Input LLRs
   \param p_out Output vector
   \param numLLR Number of LLRs
   \param p_decParamsnrLDPC decoder parameters
   \param p_profilernrLDPC profiler statistics
*/

static inline uint32_t nrLDPC_decoder_core_dynamic(int8_t* p_llr,
                                                   int8_t* p_out,
                                                   int n_segments,
                                                   t_nrLDPC_dec_params* p_decParams,
                                                   t_nrLDPC_time_stats* p_profiler,
                                                   decode_abort_t* ab)
{
  gpuError_t err_core = gpuSuccess;
  bool graph_executed = false;
  uint16_t Z = p_decParams->Z;
  uint8_t BG = p_decParams->BG;
  uint8_t R = p_decParams->R;
  uint8_t numMaxIter = p_decParams->numMaxIter;
  e_nrLDPC_outMode outMode = p_decParams->outMode;
  uint32_t K = Z * 22;
  // Calculate LLR size per segment based on Rate
  uint32_t numLLR = (R == 13) ? NR_LDPC_NCOL_BG1_R13 * Z : ((R == 89) ? NR_LDPC_NCOL_BG1_R89 * Z : NR_LDPC_NCOL_BG1_R23 * Z);
  if (p_llr != p_llr_dev)
    gpuMemcpyAsync_or_fail(p_llr_dev, p_llr, n_segments * 68 * 384, gpuMemcpyHostToDevice, decoderStreams[0]);

  // Output size safety: assume worst-case unpacked bytes (K * n_segments)
  size_t total_output_size = n_segments * K * sizeof(int8_t);

/*
  // for debug, remember to remove it---------
  cuda_graph_breaker = 1; // skipping all the graph recording
  //-----------------------------------------
  */

  if (cuda_graph_breaker == 0) {
    int found_idx = -1;
    // Search in Graph Cache
    for (int i = 0; i < dynamic_cache_idx; i++) {
      if (gpu_graph_cache[i].occupied && gpu_graph_cache[i].Z == Z && gpu_graph_cache[i].R == R && gpu_graph_cache[i].BG == BG
          && gpu_graph_cache[i].K == K && gpu_graph_cache[i].numLLR == numLLR && gpu_graph_cache[i].numMaxIter == numMaxIter
          && gpu_graph_cache[i].n_segments == n_segments && gpu_graph_cache[i].outMode == outMode) {
        found_idx = i;
        break;
      }
    }
    if (found_idx >= 0) {
      // === Cache HIT: Execute Recorded Graph ===
      gpu_graph_cache[found_idx].bridge_ptr->p_llr_ptr = p_llr_dev;
      gpu_graph_cache[found_idx].bridge_ptr->p_out_ptr = (pageable || integrated) ? p_out : p_out_dev;

      err_core = nrLDPC_decoder_cuda_GraphExecute(gpu_graph_cache[found_idx].exec,
                                                  decoderStreams[0],
                                                  NULL, // doneEvent
                                                  0); // Stream Index
      if (err_core == gpuSuccess) {
        graph_executed = true;
      } else {
        cuda_graph_breaker = 1;
      }
    } else if (dynamic_cache_idx < MAX_GRAPH_CACHE_SIZE) {
      // === Cache MISS: Record New Graph and Execute ===
      int new_idx = dynamic_cache_idx;

      gpu_graph_cache[new_idx].occupied = true;
      gpu_graph_cache[new_idx].Z = Z;
      gpu_graph_cache[new_idx].R = R;
      gpu_graph_cache[new_idx].BG = BG;
      gpu_graph_cache[new_idx].K = K;
      gpu_graph_cache[new_idx].numLLR = numLLR;
      gpu_graph_cache[new_idx].numMaxIter = numMaxIter;
      gpu_graph_cache[new_idx].n_segments = n_segments;
      gpu_graph_cache[new_idx].outMode = outMode;
      // Use the determined pointers (Device ptrs for PCIe, Host ptrs for GH200)
      gpu_graph_cache[new_idx].bridge_ptr->p_llr_ptr = p_llr_dev;
      gpu_graph_cache[new_idx].bridge_ptr->p_out_ptr = pageable || integrated ? p_out : p_out_dev;

      err_core = nrLDPC_decoder_cuda_GraphRecord(gpu_graph_cache[new_idx].bridge_ptr,
                                                 numLLR,
                                                 cnProcBuf_dev,
                                                 bnProcBuf_dev,
                                                 llrRes_dev,
                                                 llrProcBuf_dev,
                                                 Z,
                                                 K,
                                                 BG,
                                                 R,
                                                 numMaxIter,
                                                 n_segments,
                                                 outMode,
                                                 decoderStreams,
                                                 0, // CudaStreamIdx
                                                 &gpu_graph_cache[new_idx].graph,
                                                 &gpu_graph_cache[new_idx].exec,
                                                 (uint8_t*)&gpu_graph_cache[new_idx].occupied);

      if (err_core == gpuSuccess) {
        err_core = nrLDPC_decoder_cuda_GraphExecute(gpu_graph_cache[new_idx].exec, decoderStreams[0], NULL, 0);

        if (err_core == gpuSuccess) {
          graph_executed = true;
          dynamic_cache_idx++;
        } else {
          cuda_graph_breaker = 1; // graph execution fail
        }
      } else {
        cuda_graph_breaker = 1; // graph record fail
      }
    }
  }

  if (!graph_executed) {
    // === Fallback to Normal Execution ===
    // If the cache is full, we cannot record new graphs.
    // Or graph operation is not safe in this device or environment.
    // Execute kernel directly using standard launch.
/*    if (cuda_graph_breaker == 1) {
      LOG_W(PHY, "Graph opereations failed, falling back to normal.\n");
    }*/
    ldpc_cuda_bridge_t* perpack_buffer = stream_bridges[0];
    perpack_buffer->p_llr_ptr = p_llr_dev;
    perpack_buffer->p_out_ptr = pageable || integrated ? p_out : p_out_dev;

    nrLDPC_decoder_cuda_NormalExecute(perpack_buffer,
                                      numLLR,
                                      cnProcBuf_dev,
                                      bnProcBuf_dev,
                                      llrRes_dev,
                                      llrProcBuf_dev,
                                      Z,
                                      K,
                                      BG,
                                      R,
                                      numMaxIter,
                                      n_segments,
                                      outMode,
                                      decoderStreams,
                                      0,
                                      NULL);
  }

  // Copy back and Cleanup for Discrete GPU
  if (!pageable && !integrated) {
    // Copy Output from Device to Host
    if (outMode == nrLDPC_outMode_BIT) {
      gpuMemcpyAsync_or_fail(p_out, p_out_dev, total_output_size >> 3, gpuMemcpyDeviceToHost, decoderStreams[0]);
    }
    if (outMode == nrLDPC_outMode_BITINT8) {
      gpuMemcpyAsync_or_fail(p_out, p_out_dev, total_output_size, gpuMemcpyDeviceToHost, decoderStreams[0]);
    }
  }

  gpuStreamSynchronize(decoderStreams[0]);
  if (p_decParams->check_crc) {
    for (int r = 0; r < n_segments; r++) {
      //      if (r<=1) for (int i=0;i<(K>>3);i++) printf("byte (%d,%d) %x\n",r,i,((uint8_t*)(p_out+r*(K>>3)))[i]);

      if (!p_decParams->check_crc((uint8_t*)(p_out + (r * (K >> 3))), p_decParams->Kprime, p_decParams->crc_type)) {
        LOG_D(PHY, "Segment %d/%d CRC NOK\n", r, n_segments);
        return (1 + numMaxIter);
      }
      /*
      uint8_t *b=(uint8_t*)(p_out + (r*(K>>3)));
      int i=0;
      if (b[K-2] == 0 && b[K - 1] == 0) {
            while (b[i] == 0 && i < K)
           i++;
            if (i == K) {
              LOG_E(PHY, "received all 0 pdu (K %d, r %d)\n",K,r);
            }
      }
      */
    }
  }
  return numMaxIter;
}
