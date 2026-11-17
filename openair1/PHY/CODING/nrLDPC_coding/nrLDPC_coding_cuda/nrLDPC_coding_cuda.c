/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdint.h>
#include <dlfcn.h>

#include "log.h"
#include "openair1/PHY/CODING/nrLDPC_defs.h"
#include "openair1/PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"
#include "openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "common/utils/threadPool/thread-pool.h"
#include "common/utils/threadPool/task_ans.h"

/* segment interface */
extern int32_t LDPCinit_cuda();
int32_t LDPCinit()
{
  return LDPCinit_cuda();
}

// LDPCshutdown_cuda
extern int32_t LDPCshutdown_cuda();
int32_t LDPCshutdown()
{
  return LDPCshutdown_cuda();
}

// LDPCdecoder
extern int32_t LDPCdecoder_cuda(t_nrLDPC_dec_params *p_decParams,
                                int8_t *p_llr,
                                uint8_t *p_out,
                                t_nrLDPC_time_stats *p_profiler,
                                decode_abort_t *ab);
int32_t LDPCdecoder(t_nrLDPC_dec_params *p_decParams,
                    int8_t *p_llr,
                    uint8_t *p_out,
                    t_nrLDPC_time_stats *p_profiler,
                    decode_abort_t *ab)
{
  return LDPCdecoder_cuda(p_decParams, p_llr, p_out, p_profiler, ab);
}

// LDPCencoder <= (LDPCencoder32)
extern uint32_t **LDPCencoder32(uint8_t **input, encoder_implemparams_t *impp);
uint32_t LDPCencoder(uint8_t **input, uint8_t *output, encoder_implemparams_t *impp)
{
  fprintf(stderr, "%s() not implemented for LDPC CUDA, should use LDPCencoder32 instead\n", __func__);
  abort();
}

/* slot interface */

// fallback for LDPC sizes not implemented by CUDA LDPC
nrLDPC_coding_interface_t ldpc_cpu;
void *ldpc_lib_handle; // corresponding dlsym() handle, see also below

extern int nrLDPC_coding_encoder32(nrLDPC_slot_encoding_parameters_t *nrLDPC_slot_encoding_parameters,
                                   nrLDPC_TB_encoding_parameters_t *nrLDPC_TB_encoding_parameters);

/* the lifting sizes for which a parity check kernel has been generated, see
   the ldpc_BG1_Zc*_32bit.cu in this directory and encode_parity_check_part_cuda() */
static bool cuda_encoder_supports(const nrLDPC_TB_encoding_parameters_t *tbp)
{
  if (tbp->BG != 1)
    return false;
  switch (tbp->Z) {
    case 176:
    case 192:
    case 208:
    case 224:
    case 240:
    case 256:
    case 288:
    case 320:
    case 352:
    case 384:
      return true;
    default:
      return false;
  }
}

int nrLDPC_coding_encoder(nrLDPC_slot_encoding_parameters_t *slot_params)
{
  // this should be the same as previous nrLDPC_coding_encoder() in nrLDPC_coding_segment_encoder.c
  nrLDPC_TB_encoding_parameters_t tbCPU[32] = {0};
  nrLDPC_slot_encoding_parameters_t cpu = {
      .frame = slot_params->frame,
      .slot = slot_params->slot,
      .threadPool = slot_params->threadPool,
      // timing?
      .TBs = tbCPU,
  };
  int offset[32];
  for (int dlsch_id = 0; dlsch_id < slot_params->nb_TBs; dlsch_id++) {
    nrLDPC_TB_encoding_parameters_t *tbp = &slot_params->TBs[dlsch_id];
    if (cuda_encoder_supports(tbp) && tbp->C > 8) {
      nrLDPC_coding_encoder32(slot_params, tbp);
    } else {
      // this is not handled by CUDA, handle with CPU
      offset[cpu.nb_TBs] = dlsch_id;
      cpu.TBs[cpu.nb_TBs++] = *tbp;
    }
  }
  if (cpu.nb_TBs > 0) {
    ldpc_cpu.nrLDPC_coding_encoder(&cpu);
    for (int i = 0; i < cpu.nb_TBs; ++i)
      slot_params->TBs[offset[i]] = cpu.TBs[i];
  }
  return 0;
}

void nr_process_decode_segment_cuda(nrLDPC_TB_decoding_parameters_t *);
typedef struct {
  nrLDPC_TB_decoding_parameters_t *tb;
  task_ans_t *ans;
} cuda_tb_task_t;

static void cuda_decode_tb_task(void *arg)
{
  cuda_tb_task_t *t = arg;
  nr_process_decode_segment_cuda(t->tb);
  completed_task_ans(t->ans);
}

int32_t nrLDPC_coding_decoder(nrLDPC_slot_decoding_parameters_t *slot_params)
{
  // this should be the same as previous nrLDPC_coding_decoder() in nrLDPC_coding_segment_decoder.c
  nrLDPC_TB_decoding_parameters_t tbCPU[32] = {0};
  nrLDPC_slot_decoding_parameters_t cpu = {
      .frame = slot_params->frame,
      .slot = slot_params->slot,
      .threadPool = slot_params->threadPool,
      .TBs = tbCPU,
  };
  AssertFatal(slot_params->nb_TBs <= 32, "%d TBs, at most 32 supported\n", slot_params->nb_TBs);
  int offset[32];
  nrLDPC_TB_decoding_parameters_t *tbGPU[32];
  int nb_gpu = 0;
  for (int pusch_id = 0; pusch_id < slot_params->nb_TBs; pusch_id++) {
    nrLDPC_TB_decoding_parameters_t *tbp = &slot_params->TBs[pusch_id];
    if (tbp->Z >= 128 && tbp->BG == 1) {
      tbGPU[nb_gpu++] = tbp;
    } else {
      // this is not handled by CUDA, handle with CPU
      offset[cpu.nb_TBs] = pusch_id;
      cpu.TBs[cpu.nb_TBs++] = *tbp;
    }
  }
  // TBs decode concurrently, each in its own decoder context (see ldpc_cuda_ctx_acquire()), in the thread pool; the last
  // one in this thread unless it decodes the TBs the CPU handles
  task_ans_t ans;
  cuda_tb_task_t tasks[32];
  const int nb_pushed = !slot_params->threadPool ? 0 : (cpu.nb_TBs > 0 ? nb_gpu : (nb_gpu > 1 ? nb_gpu - 1 : 0));
  if (nb_pushed > 0) {
    init_task_ans(&ans, nb_pushed);
    for (int i = 0; i < nb_pushed; i++) {
      tasks[i] = (cuda_tb_task_t){.tb = tbGPU[i], .ans = &ans};
      pushTpool(slot_params->threadPool, (task_t){.func = cuda_decode_tb_task, .args = &tasks[i]});
    }
  }
  for (int i = nb_pushed; i < nb_gpu; i++)
    nr_process_decode_segment_cuda(tbGPU[i]);
  if (cpu.nb_TBs > 0) {
    ldpc_cpu.nrLDPC_coding_decoder(&cpu);
    for (int i = 0; i < cpu.nb_TBs; ++i)
      slot_params->TBs[offset[i]] = cpu.TBs[i];
  }
  if (nb_pushed > 0)
    join_task_ans(&ans);
  return 0;
}

#define CHECK_DLFCN_ERROR(COND, SYMB)                          \
  if (!(COND)) {                                               \
    LOG_E(NR_PHY, "could not open %s: %s\n", SYMB, dlerror()); \
    return false;                                              \
  }
static bool open_default_ldpc(nrLDPC_coding_interface_t *ldpc, int max_num_pxsch)
{
  const char *default_ldpc = "libldpc.so";
  if (ldpc_lib_handle) {
    LOG_W(NR_PHY, "%s already initialized\n", default_ldpc);
    return true;
  }
  /* Note: RTLD_DEEPBIND is IMPORTANT. The segment decoder abstraction just
   * calls LDPCencoder()/LDPCdecoder(), and without RTLD_DEEPBIND, will resolve
   * to the symbol that has been loaded first. The LDPC loader loaded LDPC CUDA
   * first, so it would resolve to LDPCencoder()/LDPCdecoder() in this file,
   * and RTLD_DEEPBIND does such that these symbols will resolve to the LDPC
   * CPU implementation instead. */
  ldpc_lib_handle = dlopen(default_ldpc, RTLD_NOW | RTLD_DEEPBIND);
  CHECK_DLFCN_ERROR(ldpc_lib_handle, default_ldpc);

  ldpc->nrLDPC_coding_init = dlsym(ldpc_lib_handle, "nrLDPC_coding_init");
  CHECK_DLFCN_ERROR(ldpc->nrLDPC_coding_init, "nrLDPC_coding_init");
  ldpc->nrLDPC_coding_shutdown = dlsym(ldpc_lib_handle, "nrLDPC_coding_shutdown");
  CHECK_DLFCN_ERROR(ldpc->nrLDPC_coding_shutdown, "nrLDPC_coding_shutdown");
  ldpc->nrLDPC_coding_decoder = dlsym(ldpc_lib_handle, "nrLDPC_coding_decoder");
  CHECK_DLFCN_ERROR(ldpc->nrLDPC_coding_decoder, "nrLDPC_coding_decoder");
  ldpc->nrLDPC_coding_encoder = dlsym(ldpc_lib_handle, "nrLDPC_coding_encoder");
  CHECK_DLFCN_ERROR(ldpc->nrLDPC_coding_encoder, "nrLDPC_coding_encoder");

  bool success = ldpc->nrLDPC_coding_init(max_num_pxsch) == 0;
  if (!success)
    LOG_E(NR_PHY, "could not initialize %s\n", default_ldpc);
  return success;
}

static void close_default_ldpc(nrLDPC_coding_interface_t *ldpc)
{
  if (!ldpc_lib_handle) {
    LOG_W(NR_PHY, "cannot stop LDPC, not loaded\n");
    return;
  }

  ldpc->nrLDPC_coding_shutdown();
  dlclose(ldpc_lib_handle);
  ldpc_lib_handle = NULL;
}

extern int32_t nrLDPC_coding_init_cuda(int max_num_pxsch);
int32_t nrLDPC_coding_init(int max_num_pxsch)
{
  // we can't use load_nrLDPC_coding_interface() because:
  // 1. it caches the lib under the "ldpc" name, but links to "ldpc_cuda", so
  //    we recursively load this function again => stack overflow
  // 2. the global --loader.ldpc.shlibversion _cuda would be reapplied => we
  //    cannot actually load ourselves
  // hence use dlsym() directly.
  bool success = open_default_ldpc(&ldpc_cpu, max_num_pxsch);
  AssertFatal(success, "error loading LDPC library\n");
  return nrLDPC_coding_init_cuda(max_num_pxsch);
}

extern int32_t nrLDPC_coding_shutdown_cuda(void);
int32_t nrLDPC_coding_shutdown(void)
{
  close_default_ldpc(&ldpc_cpu);
  return nrLDPC_coding_shutdown_cuda();
}
