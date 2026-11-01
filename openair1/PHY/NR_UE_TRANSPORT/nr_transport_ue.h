/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief data structures for PDSCH/DLSCH/PUSCH/ULSCH physical and transport channel descriptors (TX/RX)
 */
#ifndef __NR_TRANSPORT_UE__H__
#define __NR_TRANSPORT_UE__H__
#include <limits.h>
#include "PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"
#include "nfapi/open-nFAPI/nfapi/public_inc/fapi_nr_ue_interface.h"
#include "../NR_TRANSPORT/nr_transport_common_proto.h"

typedef struct {
  /// Index of current HARQ round for this ULSCH
  uint8_t round;
  /// pointer to pdu from MAC interface (TS 36.212 V15.4.0, Sec 5.1 p. 8)
  unsigned char *payload_AB;
  /// Pointers to transport block segments
  uint8_t **c;
  /// Rate matching (Interleaving) outputs (TS 36.212 V15.4.0, Sec 5.4.2.2 p. 30)
  uint8_t *f;
  /// Number of code segments
  uint32_t C;
  /// Number of bits in code segments
  uint32_t K;
  ///
  uint32_t Kb;
  /// Number of "Filler" bits
  uint32_t F;
  /// Number of soft channel bits
  uint32_t G;
  // Number of modulated symbols carrying data
  uint32_t num_of_mod_symbols;
  // Encoder BG
  uint8_t BG;
  // LDPC lifting size
  uint32_t Z;
} NR_UL_UE_HARQ_t;

typedef enum {
  NR_SCH_IDLE = 0,
  NR_ACTIVE
} NR_SCH_status_t;

typedef struct {
  NR_SCH_status_t status;
  /// NDAPI struct for UE
  nfapi_nr_ue_pusch_pdu_t pusch_pdu;
  /// Cell ID
  int     Nid_cell;
} NR_UE_ULSCH_t;

typedef struct {
  /// Set on new data; cleared after nr_dlsch_decoding() segments the TB. Stays set if decode never ran.
  uint8_t first_rx;
  /// DLSCH status flag indicating
  NR_SCH_status_t status;
  /// Pointer to transport block segments
  uint8_t *c;
#ifdef LDPC_CUDA
  /// Pointer to transport block segments (GPU pointer for c when using shared memory, should be the same on Jetson
  uint8_t *cdev;
#endif
  /// soft bits for each received segment ("d"-sequence)(for definition see 36-212 V8.6 2009-03, p.15)
  /// Accumulates the soft bits for each round to increase decoding success (HARQ)
  int16_t *d;
  /// Index of current HARQ round for this DLSCH
  uint8_t DLround;
  /// Number of code segments 
  uint32_t C;
  /// Number of bits in code segments
  uint32_t K;
  /// Number of "Filler" bits 
  uint32_t F;
  /// LDPC lifting factor
  uint32_t Z;
  /// codeword this transport block is mapped to
  uint8_t codeword;
  /// HARQ-ACKs
  bool decodeResult;
  /// Last index of LLR buffer that contains information.
  /// Used for computing LDPC decoder R
  int llrLen;
  /// Number of segments processed so far
  uint32_t processedSegments;
  decode_abort_t abort_decode;
  /* Slot of the grant that armed this process. A late decode only retires if it still matches. */
  int activated_frame;
  int activated_slot;
} NR_DL_UE_HARQ_t;

typedef struct {
  fapi_nr_dl_cw_info_t cw_info;
  /// RNTI
  uint16_t rnti;
  /// RNTI type
  uint8_t rnti_type;
  /// Active flag for DLSCH demodulation
  bool active;
  /// Maximum number of LDPC iterations
  uint8_t max_ldpc_iterations;
  /// number of iterations used in last turbo decoding
  int8_t last_iteration_cnt;
} NR_UE_DLSCH_t;
#endif
