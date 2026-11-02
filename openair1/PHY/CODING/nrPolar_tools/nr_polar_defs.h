/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __NR_POLAR_DEFS__H__
#define __NR_POLAR_DEFS__H__

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "PHY/CODING/nrPolar_tools/nr_polar_dci_defs.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_uci_defs.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_pbch_defs.h"
#include "PHY/CODING/coding_defs.h"
#include "PHY/sse_intrin.h"
// #include "SIMULATION/TOOLS/sim.h"

#define NR_POLAR_DECODER_LISTSIZE 8 // uint8_t

#define NR_POLAR_AGGREGATION_LEVEL_1_PRIME 149 // uint16_t
#define NR_POLAR_AGGREGATION_LEVEL_2_PRIME 151 // uint16_t
#define NR_POLAR_AGGREGATION_LEVEL_4_PRIME 157 // uint16_t
#define NR_POLAR_AGGREGATION_LEVEL_8_PRIME 163 // uint16_t
#define NR_POLAR_AGGREGATION_LEVEL_16_PRIME 167 // uint16_t

static const uint8_t nr_polar_subblock_interleaver_pattern[32] = {0,  1,  2,  4,  3,  5,  6,  7,  8,  16, 9,  17, 10, 18, 11, 19,
                                                                  12, 20, 13, 21, 14, 22, 15, 23, 24, 25, 26, 28, 27, 29, 30, 31};

#define Nmax 1024
#define nmax 10

#define uint128_t __uint128_t

#define POLAR_OP_CODE_LEFT 0
#define POLAR_OP_CODE_RIGHT 1
#define POLAR_OP_CODE_BETA 2

typedef struct decoder_node_t_s {
  struct decoder_node_t_s *left;
  struct decoder_node_t_s *right;
  uint64_t level: 16;
  uint64_t leaf: 16;
  uint64_t first_leaf_index: 16;
  uint64_t all_frozen: 1;
  uint64_t betaInit: 1;
  uint32_t alpha;
  uint32_t beta;
} decoder_node_t;

typedef struct decoder_tree_t_s {
  decoder_node_t *root;
  simde__m256i buffer[1024]; // seems enough but to be refined
} decoder_tree_t;

typedef struct nrPolar_params {
  // messageType: 0=PBCH, 1=DCI, -1=UCI

  struct nrPolar_params *nextPtr __attribute__((aligned(16)));
  bool busy;
  uint64_t idx;
  uint8_t n_max;
  uint8_t i_il;
  uint8_t i_seg;
  uint8_t n_pc;
  uint8_t n_pc_wm;
  uint8_t i_bil;
  uint16_t payloadBits;
  uint16_t encoderLength;
  uint8_t crcParityBits;
  uint8_t crcCorrectionBits;
  uint16_t K;
  uint16_t N;
  uint8_t n;
  uint32_t crcBit;

  uint16_t *interleaving_pattern;
  uint16_t *rate_matching_pattern;
  uint16_t *i_bil_pattern;
  const uint16_t *Q_0_Nminus1;
  int16_t *Q_I_N;
  int16_t *Q_F_N;
  int16_t *Q_PC_N;
  uint8_t *information_bit_pattern;
  uint8_t *parity_check_bit_pattern;
  const uint8_t **crc_generator_matrix; // G_P
  const uint8_t **G_N;
  int groupsize;
  int *rm_tab;
  uint64_t cprime_tab0[16][256];
  uint64_t cprime_tab1[16][256];
  decoder_tree_t decoder;
  struct {
    int iter;
    bool is_initialized;
    struct {
      int op_code;
      decoder_node_t *node;
    } op_list[600];
  } tree_linearization;
} t_nrPolar_params;

void polar_encoder(uint32_t *input, uint32_t *output, int8_t messageType, uint16_t messageLength, uint8_t aggregation_level);

void polar_encoder_dci(uint32_t *in,
                       uint32_t *out,
                       uint16_t n_RNTI,
                       int8_t messageType,
                       uint16_t messageLength,
                       uint8_t aggregation_level);

void polar_encoder_fast(uint64_t *A,
                        void *out,
                        int32_t crcmask,
                        uint8_t ones_flag,
                        int8_t messageType,
                        uint16_t messageLength,
                        uint8_t aggregation_level);

int8_t polar_decoder(double *input,
                     uint32_t *output,
                     uint8_t listSize,
                     int8_t messageType,
                     uint16_t messageLength,
                     uint8_t aggregation_level);

uint32_t polar_decoder_int16(const int16_t *input,
                             uint64_t *out,
                             uint8_t ones_flag,
                             int8_t messageType,
                             uint16_t messageLength,
                             uint8_t aggregation_level);

int8_t polar_decoder_dci(double *input,
                         uint32_t *out,
                         uint8_t listSize,
                         uint16_t n_RNTI,
                         int8_t messageType,
                         uint16_t messageLength,
                         uint8_t aggregation_level);

void generic_polar_decoder(t_nrPolar_params *pp, decoder_node_t *node, uint8_t *nr_polar_U);

static inline int16_t *treeAlpha(decoder_node_t *node)
{
  return (int16_t *)((uint8_t *)node + node->alpha);
}

static inline int8_t *treeBeta(decoder_node_t *node)
{
  return (int8_t *)node + node->beta;
}

void build_decoder_tree(t_nrPolar_params *pp);
void build_polar_tables(t_nrPolar_params *polarParams);

void nr_polar_print_polarParams(void);

t_nrPolar_params *nr_polar_params(int8_t messageType, uint16_t messageLength, int aggregation_level);

uint16_t nr_polar_aggregation_prime(uint8_t aggregation_level);

const uint8_t **nr_polar_kronecker_power_matrices(uint8_t n);

const uint16_t *nr_polar_sequence_pattern(uint8_t n);

/*!@fn uint32_t nr_polar_output_length(uint16_t K, uint16_t E, uint8_t n_max)
 * @brief Computes...
 * @param K Number of bits to encode (=payloadBits+crcParityBits)
 * @param E
 * @param n_max */
uint32_t nr_polar_output_length(uint16_t K, uint16_t E, uint8_t n_max);

void nr_polar_rate_matching_pattern(uint16_t *rmp, uint16_t *J, const uint8_t *P_i_, uint16_t K, uint16_t N, uint16_t E);

void nr_polar_rate_matching(double *input, double *output, uint16_t *rmp, uint16_t K, uint16_t N, uint16_t E);

void nr_polar_interleaving_pattern(uint16_t K, uint8_t I_IL, uint16_t *PI_k_);

void nr_polar_info_bit_pattern(uint8_t *ibp,
                               uint8_t *pcbp,
                               int16_t *Q_I_N,
                               int16_t *Q_F_N,
                               int16_t *Q_PC_N,
                               const uint16_t *J,
                               const uint16_t *Q_0_Nminus1,
                               const uint16_t K,
                               const uint16_t N,
                               const uint16_t E,
                               const uint8_t n_PC,
                               const uint8_t n_pc_wm);

void nr_polar_info_bit_extraction(uint8_t *input, uint8_t *output, uint8_t *pattern, uint16_t size);

void nr_bit2byte_uint32_8(uint32_t *in, uint16_t arraySize, uint8_t *out);

void nr_byte2bit_uint8_32(uint8_t *in, uint16_t arraySize, uint32_t *out);

const uint8_t **crc24c_generator_matrix(uint16_t payloadSizeBits);

void nr_polar_generate_u(uint64_t *u,
                         const uint64_t *Cprime,
                         const uint8_t *information_bit_pattern,
                         const uint8_t *parity_check_bit_pattern,
                         uint16_t N,
                         uint8_t n_pc);

void nr_polar_uxG(uint8_t const *u, size_t N, uint8_t *D);

void nr_polar_bit_insertion(uint8_t *input, uint8_t *output, uint16_t N, uint16_t K, int16_t *Q_I_N, int16_t *Q_PC_N, uint8_t n_PC);

void nr_matrix_multiplication_uint8_1D_uint8_2D(const uint8_t *matrix1,
                                                const uint8_t **matrix2,
                                                uint8_t *output,
                                                uint16_t row,
                                                uint16_t col);

void nr_sort_asc_double_1D_array_ind(double *matrix, uint8_t *ind, uint8_t len);

void nr_free_double_2D_array(double **input, uint16_t xlen);

#ifndef __cplusplus
void updateLLR(uint8_t listSize,
               uint16_t row,
               uint16_t col,
               uint16_t xlen,
               uint8_t ylen,
               int zlen,
               double llr[xlen][ylen][zlen],
               uint8_t llrU[xlen][ylen],
               uint8_t bit[xlen][ylen][zlen],
               uint8_t bitU[xlen][ylen]);
void updatePathMetric(double *pathMetric,
                      uint8_t listSize,
                      uint8_t bitValue,
                      uint16_t row,
                      int xlen,
                      int ylen,
                      int zlen,
                      double llr[xlen][ylen][zlen]);
void updatePathMetric2(double *pathMetric,
                       uint8_t listSize,
                       uint16_t row,
                       int xlen,
                       int ylen,
                       int zlen,
                       double llr[xlen][ylen][zlen]);
#endif
// Also nr_polar_rate_matcher
static inline void nr_polar_interleaver(uint8_t *input, uint8_t *output, uint16_t *pattern, uint16_t size)
{
  for (int i = 0; i < size; i++)
    output[i] = input[pattern[i]];
}

static inline void nr_polar_deinterleaver(uint8_t *input, uint8_t *output, uint16_t *pattern, uint16_t size)
{
  for (int i = 0; i < size; i++)
    output[pattern[i]] = input[i];
}

/*
 * De-interleaving of coded bits implementation
 * TS 138.212: Section 5.4.1.3 - Interleaving of coded bits
 */
static inline void nr_polar_rm_deinterleaving_lut(uint16_t *out, const uint E)
{
  uint T = ceil((sqrt(8 * E + 1) - 1) / 2);

  bool v_tab[T][T];
  memset(v_tab, false, sizeof(v_tab));
  for (uint i = 0, k = 0; i < T; i++) {
    for (uint j = 0; j < T - i; j++, k++) {
      v_tab[i][j] = k < E;
    }
  }

  int v[T][T];
  memset(v, -1, sizeof(v));
  for (uint j = 0, k = 0; j < T; j++) {
    for (uint i = 0; i < T - j; i++) {
      if (k < E && v_tab[i][j]) {
        v[i][j] = k;
        k++;
      }
    }
  }

  memset(out, 0, E * sizeof(*out));
  for (uint i = 0, k = 0; i < T; i++) {
    for (uint j = 0; j < T - i; j++) {
      if (v[i][j] != -1) {
        out[k] = v[i][j];
        k++;
      }
    }
  }
}

extern pthread_mutex_t PolarListMutex;
static inline void polarReturn(t_nrPolar_params *polarParams)
{
  pthread_mutex_lock(&PolarListMutex);
  polarParams->busy = false;
  pthread_mutex_unlock(&PolarListMutex);
}

#endif
