/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_polar_defs.h"
#include "nr_polar_psbch_defs.h"
#include "common/utils/LOG/log.h"

#define PolarKey (((uint64_t)messageType << 48) | ((uint64_t)messageLength << 32) | (uint32_t)aggregation_level)
static t_nrPolar_params *PolarList = NULL;
pthread_mutex_t PolarListMutex = PTHREAD_MUTEX_INITIALIZER;

static int intcmp(const void *p1, const void *p2)
{
  return (*(int16_t *)p1 > *(int16_t *)p2);
}

static void nr_polar_delete_list(t_nrPolar_params *polarParams)
{
  if (!polarParams)
    return;
  if (polarParams->nextPtr)
    nr_polar_delete_list(polarParams->nextPtr);

  // From build_polar_tables()
  free(polarParams->rm_tab);
  if (polarParams->crc_generator_matrix)
    free(polarParams->crc_generator_matrix);
  // Polar Coding vectors
  free(polarParams->interleaving_pattern);
  free(polarParams->rate_matching_pattern);
  free(polarParams->information_bit_pattern);
  free(polarParams->parity_check_bit_pattern);
  free(polarParams->i_bil_pattern);
  free(polarParams->Q_I_N);
  free(polarParams->Q_F_N);
  free(polarParams->Q_PC_N);
  free(polarParams);
}

static void nr_polar_delete(void)
{
  pthread_mutex_lock(&PolarListMutex);
  nr_polar_delete_list(PolarList);
  PolarList = NULL;
  pthread_mutex_unlock(&PolarListMutex);
}

t_nrPolar_params *nr_polar_params(int8_t messageType, uint16_t messageLength, int aggregation_level)
{
  // The lock is weak, because we never delete in the list, only at exit time
  // therefore, returning t_nrPolar_params * from the list is safe for future usage
  pthread_mutex_lock(&PolarListMutex);
  if (!PolarList)
    atexit(nr_polar_delete);

  t_nrPolar_params *currentPtr = PolarList;
  // Parse the list. If the node is already created, return without initialization.
  while (currentPtr != NULL) {
    // printf("currentPtr->idx %d, (%d,%d)\n",currentPtr->idx,currentPtr->payloadBits,currentPtr->encoderLength);
    // LOG_D(PHY,"Looking for index %d\n",(messageType * messageLength * aggregation_prime));
    if (currentPtr->busy == false && currentPtr->idx == PolarKey) {
      currentPtr->busy = true;
      pthread_mutex_unlock(&PolarListMutex);
      return currentPtr;
    } else
      currentPtr = currentPtr->nextPtr;
  }

  //  printf("currentPtr %p (polarParams %p)\n",currentPtr,polarParams);
  // Else, initialize and add node to the end of the linked list.
  t_nrPolar_params *newPolarInitNode = memalign(32, sizeof(t_nrPolar_params));

  AssertFatal(newPolarInitNode, "[nr_polar_init] New t_nrPolar_params * could not be created");
  *newPolarInitNode = (t_nrPolar_params){.busy = true, .nextPtr = PolarList, .tree_linearization.is_initialized = false};
  PolarList = newPolarInitNode;
  pthread_mutex_unlock(&PolarListMutex);
  //   LOG_D(PHY,"Setting new polarParams index %d, messageType %d, messageLength %d, aggregation_prime %d\n",(messageType *
  //   messageLength * aggregation_prime),messageType,messageLength,aggregation_prime);
  newPolarInitNode->idx = PolarKey;
  // printf("newPolarInitNode->idx %d,
  // (%d,%d,%d:%d)\n",newPolarInitNode->idx,messageType,messageLength,aggregation_prime,aggregation_level);

  if (messageType == NR_POLAR_PBCH_MESSAGE_TYPE) {
    newPolarInitNode->n_max = NR_POLAR_PBCH_N_MAX;
    newPolarInitNode->i_il = NR_POLAR_PBCH_I_IL;
    newPolarInitNode->i_seg = NR_POLAR_PBCH_I_SEG;
    newPolarInitNode->n_pc = NR_POLAR_PBCH_N_PC;
    newPolarInitNode->n_pc_wm = NR_POLAR_PBCH_N_PC_WM;
    newPolarInitNode->i_bil = NR_POLAR_PBCH_I_BIL;
    newPolarInitNode->crcParityBits = NR_POLAR_PBCH_CRC_PARITY_BITS;
    newPolarInitNode->payloadBits = NR_POLAR_PBCH_PAYLOAD_BITS;
    newPolarInitNode->encoderLength = NR_POLAR_PBCH_E;
    newPolarInitNode->crcCorrectionBits = NR_POLAR_PBCH_CRC_ERROR_CORRECTION_BITS;
    newPolarInitNode->crc_generator_matrix = crc24c_generator_matrix(newPolarInitNode->payloadBits); // G_P
    // printf("Initializing polar parameters for PBCH (K %d, E
    // %d)\n",newPolarInitNode->payloadBits,newPolarInitNode->encoderLength);

  } else if (messageType == NR_POLAR_DCI_MESSAGE_TYPE) {
    newPolarInitNode->n_max = NR_POLAR_DCI_N_MAX;
    newPolarInitNode->i_il = NR_POLAR_DCI_I_IL;
    newPolarInitNode->i_seg = NR_POLAR_DCI_I_SEG;
    newPolarInitNode->n_pc = NR_POLAR_DCI_N_PC;
    newPolarInitNode->n_pc_wm = NR_POLAR_DCI_N_PC_WM;
    newPolarInitNode->i_bil = NR_POLAR_DCI_I_BIL;
    newPolarInitNode->crcParityBits = NR_POLAR_DCI_CRC_PARITY_BITS;
    newPolarInitNode->payloadBits = messageLength;
    newPolarInitNode->encoderLength = aggregation_level * 108;
    newPolarInitNode->crcCorrectionBits = NR_POLAR_DCI_CRC_ERROR_CORRECTION_BITS;
    newPolarInitNode->crc_generator_matrix =
        crc24c_generator_matrix(newPolarInitNode->payloadBits + newPolarInitNode->crcParityBits); // G_P
    // printf("Initializing polar parameters for DCI (K %d, E %d, L
    // %d)\n",newPolarInitNode->payloadBits,newPolarInitNode->encoderLength,aggregation_level);

  } else if (messageType == NR_POLAR_UCI_PUCCH_MESSAGE_TYPE || messageType == NR_POLAR_UCI_PUSCH_MESSAGE_TYPE) {
    AssertFatal(aggregation_level > 2, "Aggregation level %d for PUCCH 2 encoding is NPRB and should be > 2\n", aggregation_level);
    AssertFatal(messageLength > 11, "Message length %d is too short for polar encoding of UCI\n", messageLength);

    // TS 38.212 - Section 6.3.1.2.1 UCI encoded by Polar code
    int L = 0;
    if (messageLength >= 12 && messageLength <= 19) {
      L = 6;
    } else if (messageLength >= 20) {
      L = 11;
    } else {
      AssertFatal(1 == 0, "L = %i is an invalid value\n", L);
    }

    // For UCI on PUSCH the encoder length (value E at rate matching output) is computed outside and passed as aggregation level
    newPolarInitNode->encoderLength = messageType == NR_POLAR_UCI_PUCCH_MESSAGE_TYPE ? aggregation_level * 16 : aggregation_level;
    newPolarInitNode->i_seg = 0;
    if ((messageLength >= 360 && newPolarInitNode->encoderLength >= 1088) || (messageLength >= 1013)) {
      newPolarInitNode->i_seg = 1;
      AssertFatal(1 == 0, "Segmentation is not supported yet (i_seg = %i)\n", newPolarInitNode->i_seg);
    }

    // TS 38.212 - Section 6.3.1.3.1 UCI encoded by Polar code
    newPolarInitNode->n_max = NR_POLAR_UCI_N_MAX;
    newPolarInitNode->i_il = NR_POLAR_UCI_I_IL;
    newPolarInitNode->crcParityBits = L;
    int Kr = messageLength + L;
    if (Kr >= 18 && Kr <= 25) {
      newPolarInitNode->n_pc = 3;
      if ((newPolarInitNode->encoderLength - Kr + 3) > 192) {
        newPolarInitNode->n_pc_wm = 1;
      } else {
        newPolarInitNode->n_pc_wm = 0;
      }
    } else if (Kr > 30) {
      newPolarInitNode->n_pc = 0;
      newPolarInitNode->n_pc_wm = 0;
    } else {
      AssertFatal(1 == 0, "Kr = %i is an invalid value\n", Kr);
    }

    newPolarInitNode->i_bil = NR_POLAR_UCI_I_BIL;
    newPolarInitNode->payloadBits = messageLength;
    newPolarInitNode->crcCorrectionBits = NR_POLAR_PUCCH_CRC_ERROR_CORRECTION_BITS;
    // LOG_D(PHY,"New polar node, encoderLength %d, aggregation_level %d\n",newPolarInitNode->encoderLength,aggregation_level);
  } else if (messageType == SL_NR_POLAR_PSBCH_MESSAGE_TYPE) { // PSBCH
    newPolarInitNode->n_max = SL_NR_POLAR_PSBCH_N_MAX;
    newPolarInitNode->i_il = SL_NR_POLAR_PSBCH_I_IL;
    newPolarInitNode->i_seg = SL_NR_POLAR_PSBCH_I_SEG;
    newPolarInitNode->n_pc = SL_NR_POLAR_PSBCH_N_PC;
    newPolarInitNode->n_pc_wm = SL_NR_POLAR_PSBCH_N_PC_WM;
    newPolarInitNode->i_bil = SL_NR_POLAR_PSBCH_I_BIL;
    newPolarInitNode->crcParityBits = SL_NR_POLAR_PSBCH_CRC_PARITY_BITS;
    newPolarInitNode->payloadBits = SL_NR_POLAR_PSBCH_PAYLOAD_BITS;
    newPolarInitNode->encoderLength = SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2;
    newPolarInitNode->crcCorrectionBits = SL_NR_POLAR_PSBCH_CRC_ERROR_CORRECTION_BITS;
    newPolarInitNode->crc_generator_matrix = crc24c_generator_matrix(newPolarInitNode->payloadBits); // G_P
    LOG_D(PHY,
          "SIDELINK: Initializing polar parameters for PSBCH (K %d, E %d)\n",
          newPolarInitNode->payloadBits,
          newPolarInitNode->encoderLength);
  } else {
    AssertFatal(1 == 0, "[nr_polar_init] Incorrect Message Type(%d)", messageType);
  }

  newPolarInitNode->K = newPolarInitNode->payloadBits + newPolarInitNode->crcParityBits; // Number of bits to encode.
  newPolarInitNode->N = nr_polar_output_length(newPolarInitNode->K, newPolarInitNode->encoderLength, newPolarInitNode->n_max);
  newPolarInitNode->n = log2(newPolarInitNode->N);
  newPolarInitNode->G_N = nr_polar_kronecker_power_matrices(newPolarInitNode->n);
  // polar_encoder vectors:
  newPolarInitNode->Q_0_Nminus1 = nr_polar_sequence_pattern(newPolarInitNode->n);
  newPolarInitNode->interleaving_pattern = malloc(sizeof(uint16_t) * newPolarInitNode->K);
  nr_polar_interleaving_pattern(newPolarInitNode->K, newPolarInitNode->i_il, newPolarInitNode->interleaving_pattern);

  newPolarInitNode->rate_matching_pattern = malloc(sizeof(uint16_t) * newPolarInitNode->encoderLength);
  uint16_t J[newPolarInitNode->N];
  nr_polar_rate_matching_pattern(newPolarInitNode->rate_matching_pattern,
                                 J,
                                 nr_polar_subblock_interleaver_pattern,
                                 newPolarInitNode->K,
                                 newPolarInitNode->N,
                                 newPolarInitNode->encoderLength);
  if (newPolarInitNode->i_bil) {
    newPolarInitNode->i_bil_pattern = malloc(sizeof(uint16_t) * newPolarInitNode->encoderLength);
    nr_polar_rm_deinterleaving_lut(newPolarInitNode->i_bil_pattern, newPolarInitNode->encoderLength);
  }
  newPolarInitNode->information_bit_pattern = malloc(sizeof(uint8_t) * newPolarInitNode->N);
  newPolarInitNode->parity_check_bit_pattern = malloc(sizeof(uint8_t) * newPolarInitNode->N);
  newPolarInitNode->Q_I_N = malloc(sizeof(int16_t) * (newPolarInitNode->K + newPolarInitNode->n_pc));
  newPolarInitNode->Q_F_N =
      malloc(sizeof(int16_t) * (newPolarInitNode->N + 1)); // Last element shows the final array index assigned a value.
  newPolarInitNode->Q_PC_N = malloc(sizeof(int16_t) * (newPolarInitNode->n_pc));

  for (int i = 0; i <= newPolarInitNode->N; i++)
    newPolarInitNode->Q_F_N[i] = -1; // Empty array.

  nr_polar_info_bit_pattern(newPolarInitNode->information_bit_pattern,
                            newPolarInitNode->parity_check_bit_pattern,
                            newPolarInitNode->Q_I_N,
                            newPolarInitNode->Q_F_N,
                            newPolarInitNode->Q_PC_N,
                            J,
                            newPolarInitNode->Q_0_Nminus1,
                            newPolarInitNode->K,
                            newPolarInitNode->N,
                            newPolarInitNode->encoderLength,
                            newPolarInitNode->n_pc,
                            newPolarInitNode->n_pc_wm);

  // sort the Q_I_N array in ascending order (first K positions)
  qsort((void *)newPolarInitNode->Q_I_N, newPolarInitNode->K, sizeof(int16_t), intcmp);

  build_polar_tables(newPolarInitNode);
  return newPolarInitNode;
}

void nr_polar_print_polarParams()
{
  int i = 0;
  if (PolarList == NULL) {
    printf("polarParams is empty.\n");
  } else {
    t_nrPolar_params *polarParams = PolarList;
    while (polarParams != NULL) {
      printf("polarParams[%d] = %d, %d, %d\n",
             i,
             (int)(polarParams->idx >> 48),
             (int)((polarParams->idx >> 32) & 0xFFFF),
             (int)(polarParams->idx & 0xFFFFFFFF));
      polarParams = polarParams->nextPtr;
      i++;
    }
  }
  return;
}

uint16_t nr_polar_aggregation_prime(uint8_t aggregation_level)
{
  if (aggregation_level == 0)
    return 0;
  else if (aggregation_level == 1)
    return NR_POLAR_AGGREGATION_LEVEL_1_PRIME;
  else if (aggregation_level == 2)
    return NR_POLAR_AGGREGATION_LEVEL_2_PRIME;
  else if (aggregation_level == 4)
    return NR_POLAR_AGGREGATION_LEVEL_4_PRIME;
  else if (aggregation_level == 8)
    return NR_POLAR_AGGREGATION_LEVEL_8_PRIME;
  else
    return NR_POLAR_AGGREGATION_LEVEL_16_PRIME; // aggregation_level == 16
}
