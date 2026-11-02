/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// #define DEBUG_POLAR_ENCODER
// #define DEBUG_POLAR_ENCODER_DCI
// #define DEBUG_POLAR_MATLAB
// #define POLAR_CODING_DEBUG

#include "PHY/CODING/nrPolar_tools/nr_polar_defs.h"
#include "assertions.h"
#include <stdint.h>

// input  [a_31 a_30 ... a_0]
// output [f_31 f_30 ... f_0] [f_63 f_62 ... f_32] ...

void polar_encoder(uint32_t *in, uint32_t *out, int8_t messageType, uint16_t messageLength, uint8_t aggregation_level)
{
  t_nrPolar_params *polarParams = nr_polar_params(messageType, messageLength, aggregation_level);
  uint8_t nr_polar_A[polarParams->payloadBits];
  nr_bit2byte_uint32_8(in, polarParams->payloadBits, nr_polar_A);
  /*
   * Bytewise operations
   */
  // Calculate CRC.
  uint8_t nr_polar_crc[polarParams->crcParityBits];
  nr_matrix_multiplication_uint8_1D_uint8_2D(nr_polar_A,
                                             polarParams->crc_generator_matrix,
                                             nr_polar_crc,
                                             polarParams->payloadBits,
                                             polarParams->crcParityBits);

  for (uint i = 0; i < polarParams->crcParityBits; i++)
    nr_polar_crc[i] %= 2;

  uint8_t nr_polar_B[polarParams->K];
  // Attach CRC to the Transport Block. (a to b)
  memcpy(nr_polar_B, nr_polar_A, polarParams->payloadBits);
  for (uint i = polarParams->payloadBits; i < polarParams->K; i++)
    nr_polar_B[i] = nr_polar_crc[i - (polarParams->payloadBits)];

#ifdef DEBUG_POLAR_ENCODER
  uint64_t B2 = 0;

  for (int i = 0; i < polarParams->K; i++)
    B2 |= ((uint64_t)nr_polar_B[i] << i);

  printf("polar_B %lx\n", B2);
  for (int i = 0; i < polarParams->payloadBits; i++)
    printf("a[%d]=%d\n", i, nr_polar_A[i]);
  for (int i = 0; i < polarParams->K; i++)
    printf("b[%d]=%d\n", i, nr_polar_B[i]);
#endif

  // Interleaving (c to c')
  uint8_t nr_polar_CPrime[polarParams->K];
  nr_polar_interleaver(nr_polar_B, nr_polar_CPrime, polarParams->interleaving_pattern, polarParams->K);
#ifdef DEBUG_POLAR_ENCODER
  uint64_t Cprime = 0;

  for (int i = 0; i < polarParams->K; i++) {
    Cprime = Cprime | ((uint64_t)nr_polar_CPrime[i] << i);
    if (nr_polar_CPrime[i] == 1)
      printf("pos %d : %lx\n", i, Cprime);
  }

  printf("polar_Cprime %lx\n", Cprime);
#endif
  // Bit insertion (c' to u)
  uint8_t nr_polar_U[polarParams->N];
  nr_polar_bit_insertion(nr_polar_CPrime,
                         nr_polar_U,
                         polarParams->N,
                         polarParams->K,
                         polarParams->Q_I_N,
                         polarParams->Q_PC_N,
                         polarParams->n_pc);
  uint8_t nr_polar_D[polarParams->N];
  nr_matrix_multiplication_uint8_1D_uint8_2D(nr_polar_U, polarParams->G_N, nr_polar_D, polarParams->N, polarParams->N);

  for (uint i = 0; i < polarParams->N; i++)
    nr_polar_D[i] %= 2;

  uint64_t D[8];
  memset(D, 0, sizeof(D));
#ifdef DEBUG_POLAR_ENCODER

  for (int i = 0; i < polarParams->N; i++)
    D[i / 64] |= ((uint64_t)nr_polar_D[i]) << (i & 63);

  printf("D %llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx\n", D[0], D[1], D[2], D[3], D[4], D[5], D[6], D[7]);
#endif
  // Rate matching
  // Sub-block interleaving (d to y) and Bit selection (y to e)
  uint8_t nr_polar_E[polarParams->encoderLength];
  nr_polar_interleaver(nr_polar_D, nr_polar_E, polarParams->rate_matching_pattern, polarParams->encoderLength);
  /*
   * Return bits.
   */
#ifdef DEBUG_POLAR_ENCODER

  for (int i = 0; i < polarParams->encoderLength; i++)
    printf("f[%d]=%d\n", i, nr_polar_E[i]);

#endif
  nr_byte2bit_uint8_32(nr_polar_E, polarParams->encoderLength, out);

  polarReturn(polarParams);
}

void polar_encoder_dci(uint32_t *in,
                       uint32_t *out,
                       uint16_t n_RNTI,
                       int8_t messageType,
                       uint16_t messageLength,
                       uint8_t aggregation_level)
{
  t_nrPolar_params *polarParams = nr_polar_params(messageType, messageLength, aggregation_level);

#ifdef DEBUG_POLAR_ENCODER_DCI
  printf("[polar_encoder_dci] in: [0]->0x%08x \t [1]->0x%08x \t [2]->0x%08x \t [3]->0x%08x\n", in[0], in[1], in[2], in[3]);
#endif
  /*
   * Bytewise operations
   */
  //(a to a')
  uint8_t nr_polar_A[polarParams->payloadBits];
  nr_bit2byte_uint32_8(in, polarParams->payloadBits, nr_polar_A);
  uint8_t nr_polar_APrime[polarParams->K];
  for (int i = 0; i < polarParams->crcParityBits; i++)
    nr_polar_APrime[i] = 1;
  const int end = polarParams->crcParityBits + polarParams->payloadBits;
  for (int i = polarParams->crcParityBits; i < end; i++)
    nr_polar_APrime[i] = nr_polar_A[i];

#ifdef DEBUG_POLAR_ENCODER_DCI
  printf("[polar_encoder_dci] A: ");
  for (int i = 0; i < polarParams->payloadBits; i++)
    printf("%d-", nr_polar_A[i]);
  printf("\n");

  printf("[polar_encoder_dci] APrime: ");
  for (int i = 0; i < polarParams->K; i++)
    printf("%d-", nr_polar_APrime[i]);
  printf("\n");

  printf("[polar_encoder_dci] GP: ");
  for (int i = 0; i < polarParams->crcParityBits; i++)
    printf("%d-", polarParams->crc_generator_matrix[0][i]);
  printf("\n");
#endif
  // Calculate CRC.
  uint8_t nr_polar_crc[polarParams->crcParityBits];
  nr_matrix_multiplication_uint8_1D_uint8_2D(nr_polar_APrime,
                                             polarParams->crc_generator_matrix,
                                             nr_polar_crc,
                                             polarParams->K,
                                             polarParams->crcParityBits);

  for (uint i = 0; i < polarParams->crcParityBits; i++)
    nr_polar_crc[i] %= 2;

#ifdef DEBUG_POLAR_ENCODER_DCI
  printf("[polar_encoder_dci] CRC: ");
  for (int i = 0; i < polarParams->crcParityBits; i++)
    printf("%d-", nr_polar_crc[i]);
  printf("\n");
#endif
  uint8_t nr_polar_B[polarParams->payloadBits + 8 + 16];
  // Attach CRC to the Transport Block. (a to b)
  memcpy(nr_polar_B, nr_polar_A, polarParams->payloadBits);

  for (uint i = polarParams->payloadBits; i < polarParams->K; i++)
    nr_polar_B[i] = nr_polar_crc[i - polarParams->payloadBits];

  // Scrambling (b to c)
  for (int i = 0; i < 16; i++)
    nr_polar_B[polarParams->payloadBits + 8 + i] = (nr_polar_B[polarParams->payloadBits + 8 + i] + ((n_RNTI >> (15 - i)) & 1)) % 2;

#ifdef DEBUG_POLAR_ENCODER_DCI
  printf("[polar_encoder_dci] B: ");
  for (int i = 0; i < polarParams->K; i++)
    printf("%d-", nr_polar_B[i]);
  printf("\n");
#endif
  // Interleaving (c to c')
  uint8_t nr_polar_CPrime[polarParams->K];
  nr_polar_interleaver(nr_polar_B, nr_polar_CPrime, polarParams->interleaving_pattern, polarParams->K);
  // Bit insertion (c' to u)
  uint8_t nr_polar_U[polarParams->N];
  nr_polar_bit_insertion(nr_polar_CPrime,
                         nr_polar_U,
                         polarParams->N,
                         polarParams->K,
                         polarParams->Q_I_N,
                         polarParams->Q_PC_N,
                         polarParams->n_pc);
  // Encoding (u to d)
  uint8_t nr_polar_D[polarParams->N];
  nr_matrix_multiplication_uint8_1D_uint8_2D(nr_polar_U, polarParams->G_N, nr_polar_D, polarParams->N, polarParams->N);
  for (uint i = 0; i < polarParams->N; i++)
    nr_polar_D[i] %= 2;

  // Rate matching
  // Sub-block interleaving (d to y) and Bit selection (y to e)
  uint8_t nr_polar_E[polarParams->encoderLength];
  nr_polar_interleaver(nr_polar_D, nr_polar_E, polarParams->rate_matching_pattern, polarParams->encoderLength);
  /*
   * Return bits.
   */
  nr_byte2bit_uint8_32(nr_polar_E, polarParams->encoderLength, out);
#ifdef DEBUG_POLAR_ENCODER_DCI
  printf("[polar_encoder_dci] E: ");
  for (int i = 0; i < polarParams->encoderLength; i++)
    printf("%d-", nr_polar_E[i]);

  uint8_t outputInd = ceil(polarParams->encoderLength / 32.0);
  printf("\n[polar_encoder_dci] out: ");
  for (int i = 0; i < outputInd; i++)
    printf("[%d]->0x%08x\t", i, out[i]);
#endif
  polarReturn(polarParams);
}

/*
 * Interleaving of coded bits implementation
 * TS 138.212: Section 5.4.1.3 - Interleaving of coded bits
 */
void nr_polar_rm_interleaving_cb(void *in, void *out, uint16_t E)
{
  int T = ceil((sqrt(8 * E + 1) - 1) / 2);
  int v[T][T];
  int k = 0;
  uint64_t *in64 = (uint64_t *)in;
  for (int i = 0; i < T; i++) {
    for (int j = 0; j < T - i; j++) {
      if (k < E) {
        int k1 = k >> 6;
        int k2 = k - (k1 << 6);
        v[i][j] = (in64[k1] >> k2) & 1;
      } else {
        v[i][j] = -1;
      }
      k++;
    }
  }

  k = 0;
  uint64_t *out64 = (uint64_t *)out;
  memset(out64, 0, E >> 3);
  for (int j = 0; j < T; j++) {
    for (int i = 0; i < T - j; i++) {
      if (v[i][j] != -1) {
        int k1 = k >> 6;
        int k2 = k - (k1 << 6);
        out64[k1] |= (uint64_t)v[i][j] << k2;
        k++;
      }
    }
  }
}

__attribute__((always_inline)) static inline void polar_rate_matching(const t_nrPolar_params *polarParams, void *in, void *out)
{
  // handle rate matching with a single 128 bit word using bit shuffling
  // can be done with SIMD intrisics if needed
  if (polarParams->groupsize < 8) {
    AssertFatal(polarParams->encoderLength <= 512,
                "Need to handle groupsize(%d)<8 and N(%d)>512\n",
                polarParams->groupsize,
                polarParams->encoderLength);
    uint128_t *out128 = (uint128_t *)out;
    uint128_t *in128 = (uint128_t *)in;
    for (int i = 0; i < polarParams->encoderLength >> 7; i++)
      out128[i] = 0;
#ifdef DEBUG_POLAR_ENCODER
    uint128_t tmp1;
#endif
    for (int i = 0; i < polarParams->encoderLength; i++) {
#ifdef DEBUG_POLAR_ENCODER
      printf("%d<-%u : %llx.%llx =>", i, polarParams->rate_matching_pattern[i], ((uint64_t *)out)[1], ((uint64_t *)out)[0]);
#endif
      uint8_t pi = polarParams->rate_matching_pattern[i];
      uint8_t pi7 = pi >> 7;
      uint8_t pimod128 = pi & 127;
      uint8_t imod128 = i & 127;
      uint8_t i7 = i >> 7;
      uint128_t tmp0 = (in128[pi7] & (((uint128_t)1) << (pimod128)));

      if (tmp0 != 0) {
        out128[i7] = out128[i7] | ((uint128_t)1) << imod128;
#ifdef DEBUG_POLAR_ENCODER
        printf("%llx.%llx<->%llx.%llx => %llx.%llx\n",
               ((uint64_t *)&tmp0)[1],
               ((uint64_t *)&tmp0)[0],
               ((uint64_t *)&tmp1)[1],
               ((uint64_t *)&tmp1)[0],
               ((uint64_t *)out)[1],
               ((uint64_t *)out)[0]);
#endif
      }
    }

  }
  // These are based on LUTs for byte and short word groups
  else if (polarParams->groupsize == 8)
    for (int i = 0; i < polarParams->encoderLength >> 3; i++)
      ((uint8_t *)out)[i] = ((uint8_t *)in)[polarParams->rm_tab[i]];
  else // groupsize==16
    for (int i = 0; i < polarParams->encoderLength >> 4; i++) {
      ((uint16_t *)out)[i] = ((uint16_t *)in)[polarParams->rm_tab[i]];
    }

  if (polarParams->i_bil == 1) {
    nr_polar_rm_interleaving_cb(out, out, polarParams->encoderLength);
  }
}

void build_polar_tables(t_nrPolar_params *polarParams)
{
  // build table b -> c'
  AssertFatal(polarParams->K > 17, "K = %d < 18, is not possible\n", polarParams->K);
  AssertFatal(polarParams->K < 129, "K = %d > 128, is not supported yet\n", polarParams->K);
  const int numbytes = (polarParams->K + 7) / 8;
  const int residue = polarParams->K & 7;
  uint deinterleaving_pattern[polarParams->K];

  for (int i = 0; i < polarParams->K; i++)
    deinterleaving_pattern[polarParams->interleaving_pattern[i]] = i;

  for (int byte = 0; byte < numbytes; byte++) {
    int numbits = byte < (polarParams->K >> 3) ? 8 : residue;
    for (uint64_t val = 0; val < 256LU; val++) {
      // uint16_t * tmp=polarParams->deinterleaving_pattern+polarParams->K - 1 - 8 * byte;
      union {
        uint128_t full;
        uint64_t pieces[2];
      } tab = {0};
      uint *tmp = deinterleaving_pattern + polarParams->K - 8 * byte - 1;
      for (int i = 0; i < numbits; i++) {
        // flip bit endian of B bitstring
        const int ip = *tmp--;
        AssertFatal(ip < 128, "ip = %d\n", ip);
        const uint128_t bit_i = (val >> i) & 1;
        tab.full |= bit_i << ip;
      }
      polarParams->cprime_tab0[byte][val] = tab.pieces[0];
      polarParams->cprime_tab1[byte][val] = tab.pieces[1];
    }
  }

  AssertFatal(polarParams->N == 512 || polarParams->N == 256 || polarParams->N == 128 || polarParams->N == 64,
              "N = %d, not done yet\n",
              polarParams->N);

  // rate matching table
  int iplast = polarParams->rate_matching_pattern[0];
  int ccnt = 0;
#ifdef DEBUG_POLAR_ENCODER
  int groupcnt = 0;
  int firstingroup_out = 0;
  int firstingroup_in = iplast;
#endif
  int mingroupsize = 1024;

  // compute minimum group size of rate-matching pattern
  for (int outpos = 1; outpos < polarParams->encoderLength; outpos++) {
    int ip = polarParams->rate_matching_pattern[outpos];
#ifdef DEBUG_POLAR_ENCODER
    printf("rm: outpos %d, inpos %d\n", outpos, ip);
#endif
    if ((ip - iplast) == 1)
      ccnt++;
    else {
#ifdef DEBUG_POLAR_ENCODER
      groupcnt++;
      printf("group %d (size %d): (%d:%d) => (%d:%d)\n",
             groupcnt,
             ccnt + 1,
             firstingroup_in,
             firstingroup_in + ccnt,
             firstingroup_out,
             firstingroup_out + ccnt);
#endif

      if ((ccnt + 1) < mingroupsize)
        mingroupsize = ccnt + 1;

      ccnt = 0;
#ifdef DEBUG_POLAR_ENCODER
      firstingroup_out = outpos;
      firstingroup_in = ip;
#endif
    }

    iplast = ip;
  }
#ifdef DEBUG_POLAR_ENCODER
  groupcnt++;
#endif
  if ((ccnt + 1) < mingroupsize)
    mingroupsize = ccnt + 1;
#ifdef DEBUG_POLAR_ENCODER
  printf("group %d (size %d): (%d:%d) => (%d:%d)\n",
         groupcnt,
         ccnt + 1,
         firstingroup_in,
         firstingroup_in + ccnt,
         firstingroup_out,
         firstingroup_out + ccnt);
#endif

  polarParams->groupsize = mingroupsize;

  if (mingroupsize >= 8) {
    int shift = 0;
    switch (mingroupsize) {
      case 8:
        shift = 3;
        break;
      case 16:
        shift = 4;
        break;
      default:
        AssertFatal(false, "mingroupsize = %i is not supported\n", mingroupsize);
        break;
    }
    polarParams->rm_tab = malloc(sizeof(*polarParams->rm_tab) * (polarParams->encoderLength >> shift));
    // rerun again to create groups
    for (int outpos = 0, tcnt = 0; outpos < polarParams->encoderLength; outpos += mingroupsize, tcnt++)
      polarParams->rm_tab[tcnt] = polarParams->rate_matching_pattern[outpos] >> shift;
  } else {
    // groupsize < 8: polar_rate_matching() uses the generic bit-shuffle path,
    // which indexes rate_matching_pattern directly and needs no rm_tab.
    polarParams->rm_tab = NULL;
  }

  build_decoder_tree(polarParams);
}

void polar_encoder_fast(uint64_t *A,
                        void *out,
                        int32_t crcmask,
                        uint8_t ones_flag,
                        int8_t messageType,
                        uint16_t messageLength,
                        uint8_t aggregation_level)
{
  t_nrPolar_params *polarParams = nr_polar_params(messageType, messageLength, aggregation_level);

#ifdef POLAR_CODING_DEBUG
  printf("polarParams->payloadBits = %i\n", polarParams->payloadBits);
  printf("polarParams->crcParityBits = %i\n", polarParams->crcParityBits);
  printf("polarParams->K = %i\n", polarParams->K);
  printf("polarParams->N = %i\n", polarParams->N);
  printf("polarParams->encoderLength = %i\n", polarParams->encoderLength);
  printf("polarParams->n_pc = %i\n", polarParams->n_pc);
  printf("polarParams->n_pc_wm = %i\n", polarParams->n_pc_wm);
  printf("polarParams->groupsize = %i\n", polarParams->groupsize);
  printf("polarParams->i_il = %i\n", polarParams->i_il);
  printf("polarParams->i_bil = %i\n", polarParams->i_bil);
  printf("polarParams->n_max = %i\n", polarParams->n_max);
#endif

  //  AssertFatal(polarParams->K > 32, "K = %d < 33, is not supported yet\n",polarParams->K);
  AssertFatal(polarParams->K < 129, "K = %d > 128, is not supported yet\n", polarParams->K);
  AssertFatal(polarParams->payloadBits < 65, "payload bits = %d > 64, is not supported yet\n", polarParams->payloadBits);
  int bitlen = polarParams->payloadBits;
  // append crc
  AssertFatal(bitlen < 129, "support for payloads <= 128 bits\n");
  //  AssertFatal(polarParams->crcParityBits == 24,"support for 24-bit crc only for now\n");
  // int bitlen0=bitlen;

#ifdef POLAR_CODING_DEBUG
  printf("\nTX\n");
  printf("a: ");
  for (int n = (bitlen + 63) / 64; n >= 0; n--) {
    if (n % 4 == 0)
      printf(" ");
    if (n < bitlen)
      printf("%lu", (A[n / 64] >> (n % 64)) & 1);
  }
  printf("\n");
#endif

  uint64_t tcrc = 0;
  uint offset = 0;

  // appending 24 ones before a0 for DCI as stated in 38.212 7.3.2
  if (ones_flag)
    offset = 3;

  // A bit string should be stored as 0, 0, ..., 0, a'_0, a'_1, ..., a'_A-1,
  //???a'_{N-1} a'_{N-2} ... a'_{N-A} 0 .... 0, where N=64,128,192,..., N is smallest multiple of 64 greater than or equal to A

  // First flip A bitstring byte endian for CRC routines (optimized for DLSCH/ULSCH, not PBCH/PDCCH)
  // CRC reads in each byte in bit positions 7 down to 0, for PBCH/PDCCH we need to read in a_{A-1} down to a_{0}, A = length of bit
  // string (e.g. 32 for PBCH)
  if (bitlen <= 32) {
    uint8_t A32_flip[4 + offset];
    if (ones_flag) {
      A32_flip[0] = 0xff;
      A32_flip[1] = 0xff;
      A32_flip[2] = 0xff;
    }
    uint32_t Aprime = (uint32_t)(((uint32_t)*A) << (32 - bitlen));
    A32_flip[0 + offset] = ((uint8_t *)&Aprime)[3];
    A32_flip[1 + offset] = ((uint8_t *)&Aprime)[2];
    A32_flip[2 + offset] = ((uint8_t *)&Aprime)[1];
    A32_flip[3 + offset] = ((uint8_t *)&Aprime)[0];
    if (polarParams->crcParityBits == 24)
      tcrc = (uint64_t)(((crcmask ^ (crc24c(A32_flip, 8 * offset + bitlen) >> 8))) & 0xffffff);
    else if (polarParams->crcParityBits == 11)
      tcrc = (uint64_t)(((crcmask ^ (crc11(A32_flip, bitlen) >> 21))) & 0x7ff);
    else if (polarParams->crcParityBits == 6)
      tcrc = (uint64_t)(((crcmask ^ (crc6(A32_flip, bitlen) >> 26))) & 0x3f);
  } else if (bitlen <= 64) {
    uint8_t A64_flip[8 + offset];
    if (ones_flag) {
      A64_flip[0] = 0xff;
      A64_flip[1] = 0xff;
      A64_flip[2] = 0xff;
    }
    uint64_t Aprime = (uint64_t)(((uint64_t)*A) << (64 - bitlen));
    A64_flip[0 + offset] = ((uint8_t *)&Aprime)[7];
    A64_flip[1 + offset] = ((uint8_t *)&Aprime)[6];
    A64_flip[2 + offset] = ((uint8_t *)&Aprime)[5];
    A64_flip[3 + offset] = ((uint8_t *)&Aprime)[4];
    A64_flip[4 + offset] = ((uint8_t *)&Aprime)[3];
    A64_flip[5 + offset] = ((uint8_t *)&Aprime)[2];
    A64_flip[6 + offset] = ((uint8_t *)&Aprime)[1];
    A64_flip[7 + offset] = ((uint8_t *)&Aprime)[0];
    if (polarParams->crcParityBits == 24)
      tcrc = (uint64_t)((crcmask ^ (crc24c(A64_flip, 8 * offset + bitlen) >> 8))) & 0xffffff;
    else if (polarParams->crcParityBits == 11)
      tcrc = (uint64_t)((crcmask ^ (crc11(A64_flip, bitlen) >> 21))) & 0x7ff;
  } else if (bitlen <= 128) {
    uint8_t A128_flip[16 + offset];
    if (ones_flag) {
      A128_flip[0] = 0xff;
      A128_flip[1] = 0xff;
      A128_flip[2] = 0xff;
    }
    uint128_t Aprime = (uint128_t)(((uint128_t)*A) << (128 - bitlen));
    for (int i = 0; i < 16; i++)
      A128_flip[i + offset] = ((uint8_t *)&Aprime)[15 - i];
    if (polarParams->crcParityBits == 24)
      tcrc = (uint64_t)((crcmask ^ (crc24c(A128_flip, 8 * offset + bitlen) >> 8))) & 0xffffff;
    else if (polarParams->crcParityBits == 11)
      tcrc = (uint64_t)((crcmask ^ (crc11(A128_flip, bitlen) >> 21))) & 0x7ff;
  }

  // this is number of quadwords in the bit string
  int quadwlen = (polarParams->K + 63) / 64;

  // Create the B bit string as
  // 0, 0, ..., 0, a'_0, a'_1, ..., a'_A-1, p_0, p_1, ..., p_{N_parity-1}

  //??? b_{N'-1} b_{N'-2} ... b_{N'-A} b_{N'-A-1} ... b_{N'-A-Nparity} = a_{N-1} a_{N-2} ... a_{N-A} p_{N_parity-1} ... p_0
  uint64_t B[4] = {0};
  B[0] = (A[0] << polarParams->crcParityBits) | tcrc;
  for (int n = 1; n < quadwlen; n++)
    if ((bitlen + 63) / 64 > n)
      B[n] = (A[n] << polarParams->crcParityBits) | (A[n - 1] >> (64 - polarParams->crcParityBits));
    else
      B[n] = (A[n - 1] >> (64 - polarParams->crcParityBits));

#ifdef POLAR_CODING_DEBUG
  int bitlen_B = bitlen + polarParams->crcParityBits;
  int B_array = (bitlen_B + 63) >> 6;
  int n_start = (B_array << 6) - bitlen_B;
  printf("b: ");
  for (int n = 0; n < bitlen_B; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    int n1 = (n + n_start) >> 6;
    int n2 = (n + n_start) - (n1 << 6);
    printf("%lu", (B[B_array - 1 - n1] >> (63 - n2)) & 1);
  }
  printf("\n");
#endif

  // TS 38.212 - Section 5.3.1.1 Interleaving
  // For each byte of B, lookup in corresponding table for 64-bit word corresponding to that byte and its position
  uint64_t Cprime[4] = {0};
  uint8_t *Bbyte = (uint8_t *)B;
  if (polarParams->K < 65) {
    Cprime[0] = polarParams->cprime_tab0[0][Bbyte[0]] | polarParams->cprime_tab0[1][Bbyte[1]]
                | polarParams->cprime_tab0[2][Bbyte[2]] | polarParams->cprime_tab0[3][Bbyte[3]]
                | polarParams->cprime_tab0[4][Bbyte[4]] | polarParams->cprime_tab0[5][Bbyte[5]]
                | polarParams->cprime_tab0[6][Bbyte[6]] | polarParams->cprime_tab0[7][Bbyte[7]];
  } else if (polarParams->K < 129) {
    for (int i = 0; i < 1 + (polarParams->K / 8); i++) {
      Cprime[0] |= polarParams->cprime_tab0[i][Bbyte[i]];
      Cprime[1] |= polarParams->cprime_tab1[i][Bbyte[i]];
    }
  }

#ifdef DEBUG_POLAR_MATLAB
  // Cprime = pbchCprime
  for (int i = 0; i < quadwlen; i++)
    printf("[polar_encoder_fast]C'[%d]= 0x%llx\n", i, (unsigned long long)(Cprime[i]));
#endif

#ifdef POLAR_CODING_DEBUG
  printf("c: ");
  for (int n = 0; n < bitlen_B; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    int n1 = n >> 6;
    int n2 = n - (n1 << 6);
    printf("%lu", (Cprime[n1] >> n2) & 1);
  }
  printf("\n");
#endif

  uint64_t u[16] = {0};
  nr_polar_generate_u(u,
                      Cprime,
                      polarParams->information_bit_pattern,
                      polarParams->parity_check_bit_pattern,
                      polarParams->N,
                      polarParams->n_pc);

#ifdef POLAR_CODING_DEBUG
  int N_array = polarParams->N >> 6;
  printf("u: ");
  for (int n = 0; n < polarParams->N; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    int n1 = n >> 6;
    int n2 = n - (n1 << 6);
    printf("%lu", (u[N_array - 1 - n1] >> (63 - n2)) & 1);
  }
  printf("\n");
#endif

  uint64_t D[8];
  nr_polar_uxG((uint8_t *)u, polarParams->N, (uint8_t *)D);

#ifdef POLAR_CODING_DEBUG
  printf("d: ");
  for (int n = 0; n < polarParams->N; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    int n1 = n >> 6;
    int n2 = n - (n1 << 6);
    printf("%lu", (D[n1] >> n2) & 1);
  }
  printf("\n");
  fflush(stdout);
#endif

  memset((void *)out, 0, polarParams->encoderLength >> 3);
  polar_rate_matching(polarParams, (void *)D, out);

#ifdef POLAR_CODING_DEBUG
  uint64_t *out64 = (uint64_t *)out;
  printf("rm:");
  for (int n = 0; n < polarParams->encoderLength; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    int n1 = n >> 6;
    int n2 = n - (n1 << 6);
    printf("%lu", (out64[n1] >> n2) & 1);
  }
  printf("\n");
#endif

  polarReturn(polarParams);
}
