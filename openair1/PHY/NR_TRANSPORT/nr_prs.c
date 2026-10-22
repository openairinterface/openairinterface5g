/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/defs_gNB.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/sse_intrin.h"
#include "openair1/PHY/NR_REFSIG/nr_mod_table.h"
//#define DEBUG_PRS_MOD
//#define DEBUG_PRS_MAP

bool nr_generate_prs(int slot,
                     c16_t *txdataF,
                     int16_t amp,
                     const nfapi_nr_dl_tti_prs_pdu_rel16_t *prs_pdu,
                     const NR_DL_FRAME_PARMS *frame_parms)
{
  const bool valid_comb = prs_pdu->combSize == 2 || prs_pdu->combSize == 4 || prs_pdu->combSize == 6 || prs_pdu->combSize == 12;
  if (!valid_comb || prs_pdu->combOffset >= prs_pdu->combSize || prs_pdu->firstSymbol + prs_pdu->numSymbols > NR_SYMBOLS_PER_SLOT
      || prs_pdu->startRb + prs_pdu->numRbs > frame_parms->N_RB_DL) {
    LOG_E(NR_PHY,
          "Invalid PRS PDU, dropped: comb %d offset %d, symbols %d + %d, RBs %d + %d (N_RB_DL %d)\n",
          prs_pdu->combSize,
          prs_pdu->combOffset,
          prs_pdu->firstSymbol,
          prs_pdu->numSymbols,
          prs_pdu->startRb,
          prs_pdu->numRbs,
          frame_parms->N_RB_DL);
    return false;
  }

  // PRS resource mapping with combsize=k which means PRS symbols exist in every k-th subcarrier in frequency domain
  // According to ts138.211 sec.7.4.1.7.2
  for (int l = prs_pdu->firstSymbol; l < prs_pdu->firstSymbol + prs_pdu->numSymbols; l++) {
    // punctured symbol
    if (!(prs_pdu->symbolPuncturingBitmap & (1 << l)))
      continue;
    int k_prime = 0;
    int symInd = l - prs_pdu->firstSymbol;
    if (prs_pdu->combSize == 2) {
      k_prime = k_prime_table[0][symInd];
    } else if (prs_pdu->combSize == 4) {
      k_prime = k_prime_table[1][symInd];
    } else if (prs_pdu->combSize == 6) {
      k_prime = k_prime_table[2][symInd];
    } else if (prs_pdu->combSize == 12) {
      k_prime = k_prime_table[3][symInd];
    }

    int k = (prs_pdu->combOffset + k_prime) % prs_pdu->combSize + prs_pdu->startRb * NR_NB_SC_PER_RB;

    // QPSK modulation
    uint32_t *gold = nr_gold_prs(prs_pdu->nIdPrs, slot, l);
    c16_t *tx = txdataF + l * frame_parms->ofdm_symbol_size;
    for (int m = 0; m < (NR_NB_SC_PER_RB / prs_pdu->combSize) * prs_pdu->numRbs; m++) {
      int idx = (((gold[(m << 1) >> 5]) >> ((m << 1) & 0x1f)) & 3);
#ifdef DEBUG_PRS_MAP
      LOG_D("m %d at k %d of l %d reIdx %d\n", m, k, l, (l*frame_parms->ofdm_symbol_size + k)<<1);
#endif
      tx[k] = c16mulRealShift(nr_qpsk_mod_table[idx], amp, 15);
      k = k + prs_pdu->combSize;
    }
  }
#ifdef DEBUG_PRS_MAP
  LOG_M("nr_prs.m",
        "prs",
        (int16_t *)&txdataF[prs_pdu->firstSymbol * frame_parms->ofdm_symbol_size],
        prs_pdu->numSymbols * frame_parms->ofdm_symbol_size,
        1,
        1);
#endif
  return true;
}
