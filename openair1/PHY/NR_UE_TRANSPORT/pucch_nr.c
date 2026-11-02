/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
* \brief Top-level routines for generating and decoding the PUCCH physical channel
*/
//#include "PHY/defs.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
//#include "PHY/extern.h"
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include <openair1/PHY/CODING/nrSmallBlock/nr_small_block_defs.h>
#include "common/utils/LOG/log.h"
#include "bits.h"
#include "openair1/PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "nr_sequences_tables.h"

#include "T.h"
//#define NR_UNIT_TEST 1
#ifdef NR_UNIT_TEST
  #define DEBUG_PUCCH_TX
  #define DEBUG_NR_PUCCH_TX
#endif

//#define ONE_OVER_SQRT2 23170 // 32767/sqrt(2) = 23170 (ONE_OVER_SQRT2)
//#define POLAR_CODING_DEBUG

void nr_generate_pucch0(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp16,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch0] start function at slot(nr_slot_tx)=%d\n",nr_slot_tx);
#endif
  /*
   * Implement TS 38.211 Subclause 6.3.2.3.1 Sequence generation
   *
   */
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch0] sequence generation\n");
#endif
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // alpha is cyclic shift
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH
  // transmission
  // uint8_t lnormal;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the
  // slot given by [5, TS 38.213]
  // uint8_t lprime;
  // mcs is provided by TC 38.213 subclauses 9.2.3, 9.2.4, 9.2.5 FIXME!
  // uint8_t mcs;
  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.1, the sequence x(n) shall be generated according to:
   * x(l*12+n) = r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u[2] = {0}, v[2] = {0};

  LOG_D(PHY,"pucch0: slot %d nr_symbols %d, start_symbol %d, prb_start %d, second_hop_prb %d,  group_hop_flag %d, sequence_hop_flag %d, mcs %d\n",
        nr_slot_tx,pucch_pdu->nr_of_symbols,pucch_pdu->start_symbol_index,pucch_pdu->prb_start,pucch_pdu->second_hop_prb,pucch_pdu->group_hop_flag,pucch_pdu->sequence_hop_flag,pucch_pdu->mcs);

#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch0] sequence generation: variable initialization for test\n");
#endif
  int startingPRB = pucch_pdu->prb_start + pucch_pdu->bwp_start;
  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);

  // we proceed to calculate alpha according to TS 38.211 Subclause 6.3.2.2.2
  int prb_offset[2]={startingPRB,startingPRB};
  nr_group_sequence_hopping(pucch_GroupHopping, pucch_pdu->hopping_id, 0, nr_slot_tx, u, v); // calculating u and v value
  if (pucch_pdu->freq_hop_flag == 1) {
    nr_group_sequence_hopping(pucch_GroupHopping, pucch_pdu->hopping_id, 1, nr_slot_tx, u + 1, v + 1); // calculating u and v value
    prb_offset[1] = pucch_pdu->second_hop_prb + pucch_pdu->bwp_start;
  }
  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.2 Mapping to physical resources FIXME!
   */

  for (int l=0; l<pucch_pdu->nr_of_symbols; l++) {
    const double alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id,
                                                 pucch_pdu->initial_cyclic_shift,
                                                 pucch_pdu->mcs,
                                                 l,
                                                 pucch_pdu->start_symbol_index,
                                                 nr_slot_tx);
    int l2 = l + pucch_pdu->start_symbol_index;
    int re_offset = NR_NB_SC_PER_RB * prb_offset[l];

    //txptr = &txdataF[0][re_offset];
#ifdef DEBUG_NR_PUCCH_TX
    printf("\t [nr_generate_pucch0] symbol %d PRB %d (%d)\n",l,prb_offset[l], re_offset);
#endif
    c16_t *txdataFptr = txdataF[0] + l2 * frame_parms->ofdm_symbol_size;
    const int32_t amp = amp16;
    // TS 38.211, 6.3.2.2.1: use hop index 0 throughout a non-hopping PUCCH.
    const unsigned group = u[pucch_pdu->freq_hop_flag ? l : 0];
    for (int n=0; n<12; n++) {
      const c16_t angle = {lround(32767 * cos(alpha * n)), lround(32767 * sin(alpha * n))};
      const c16_t table = {table_5_2_2_2_2_Re[group][n], table_5_2_2_2_2_Im[group][n]};
      txdataFptr[re_offset] = c16mulRealShift(c16mulShift(angle, table, 15), amp, 15);
#ifdef DEBUG_NR_PUCCH_TX
      printf(
          "\t [nr_generate_pucch0] mapping to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d "
          "\ttxptr(%u)=(x_n(l=%d,n=%d)=(%d,%d))\n",
          amp,
          frame_parms->ofdm_symbol_size,
          frame_parms->N_RB_DL,
          (l2 * frame_parms->ofdm_symbol_size) + re_offset,
          l2,
          n,
          txdataFptr[re_offset].r,
          txdataFptr[re_offset].i);
#endif
      re_offset++;
    }
  }
}

void nr_generate_pucch1(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp16,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
  uint16_t m0 = pucch_pdu->initial_cyclic_shift;
  uint64_t payload = pucch_pdu->payload;
  uint8_t startingSymbolIndex = pucch_pdu->start_symbol_index;
  uint8_t nrofSymbols = pucch_pdu->nr_of_symbols;
  uint16_t startingPRB = pucch_pdu->prb_start + pucch_pdu->bwp_start;
  uint8_t timeDomainOCC = pucch_pdu->time_domain_occ_idx;

#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch1] start function at slot(nr_slot_tx)=%d payload=%lu m0=%d nrofSymbols=%d startingSymbolIndex=%d startingPRB=%d second_hop_prb=%d timeDomainOCC=%d nr_bit=%d\n",
         nr_slot_tx,payload,m0,nrofSymbols,startingSymbolIndex,startingPRB,pucch_pdu->second_hop_prb,timeDomainOCC,pucch_pdu->n_bit);
#endif
  /*
   * Implement TS 38.211 Subclause 6.3.2.4.1 Sequence modulation
   *
   */
  // complex-valued symbol d_re, d_im containing complex-valued symbol d(0):
  c16_t d = {0};
  const int32_t amp = amp16;
  const int16_t baseVal = (amp * ONE_OVER_SQRT2) >> 15;
  const c16_t qpskSymbols[4] = {{baseVal, baseVal}, {baseVal, -baseVal}, {-baseVal, baseVal}, {-baseVal, -baseVal}};
  if (pucch_pdu->n_bit == 1) { // using BPSK if M_bit=1 according to TC 38.211 Subclause 5.1.2
    d = (payload & 1) == 0 ? qpskSymbols[0] : qpskSymbols[3];
  }

  if (pucch_pdu->n_bit == 2) { // using QPSK if M_bit=2 according to TC 38.211 Subclause 5.1.2
    int tmp = ((payload & 1) << 1) + ((payload >> 1) & 1);
    d = qpskSymbols[tmp];
  }
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch1] sequence modulation (amp %d/%d): payload=%lx \tde_re=%d \tde_im=%d\n", amp, baseVal,payload, d.r, d.i);
#endif
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal = 0 ;
  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  /*
   * the complex-valued symbol d_0 shall be multiplied with a sequence r_u_v_alpha_delta(n): y(n) = d_0 * r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled, intraSlotFrequencyHopping is not provided
  //              n_hop = 0
  // if frequency hopping is enabled,  intraSlotFrequencyHopping is     provided
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;
  // Intra-slot frequency hopping shall be assumed when the higher-layer parameter intraSlotFrequencyHopping is provided,
  // regardless of whether the frequency-hop distance is zero or not,
  // otherwise no intra-slot frequency hopping shall be assumed
  //uint8_t PUCCH_Frequency_Hopping = 0 ; // from higher layers
  uint8_t intraSlotFrequencyHopping = 0;

  if (pucch_pdu->freq_hop_flag) {
    intraSlotFrequencyHopping = 1;
  }

#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch1] intraSlotFrequencyHopping = %d \n", intraSlotFrequencyHopping);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.4.2 Mapping to physical resources
   */
  //int32_t *txptr;
  uint32_t re_offset=0;
  int i=0;
#define MAX_SIZE_Z 168 // this value has to be calculated from mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n
  c16_t z[MAX_SIZE_Z];
  c16_t z_dmrs[MAX_SIZE_Z];

  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  int lprime = startingSymbolIndex;

  for (int l = 0; l < nrofSymbols; l++) {
#ifdef DEBUG_NR_PUCCH_TX
    printf("\t [nr_generate_pucch1] for symbol l=%d, lprime=%d\n",
           l,lprime);
#endif
    // y_n contains the complex value d multiplied by the sequence r_u_v
    c16_t y_n[12];

    if ((intraSlotFrequencyHopping == 1) && (l >= (int)floor(nrofSymbols / 2)))
      n_hop = 1; // n_hop = 1 for second hop

#ifdef DEBUG_NR_PUCCH_TX
    printf("\t [nr_generate_pucch1] entering function nr_group_sequence_hopping with n_hop=%d, nr_slot_tx=%d\n",
           n_hop,nr_slot_tx);
#endif
    pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);
    nr_group_sequence_hopping(pucch_GroupHopping,pucch_pdu->hopping_id,n_hop,nr_slot_tx,&u,&v); // calculating u and v value
    // mcs = 0 except for PUCCH format 0
    int mcs = 0;
    double alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id, m0, mcs, l, lprime, nr_slot_tx);

    // r_u_v_alpha_delta_re and r_u_v_alpha_delta_im tables containing the sequence y(n) for the PUCCH, when they are multiplied by d(0)
    // r_u_v_alpha_delta_dmrs_re and r_u_v_alpha_delta_dmrs_im tables containing the sequence for the DM-RS.
    c16_t r_u_v_alpha_delta[12], r_u_v_alpha_delta_dmrs[12];
    for (int n = 0; n < 12; n++) {
      c16_t angle = {lround(32767 * cos(alpha * n)), lround(32767 * sin(alpha * n))};
      c16_t table = {table_5_2_2_2_2_Re[u][n], table_5_2_2_2_2_Im[u][n]};
      r_u_v_alpha_delta[n] = c16mulShift(angle, table, 15);
      r_u_v_alpha_delta_dmrs[n] = c16mulRealShift(r_u_v_alpha_delta[n], amp, 15);
      // PUCCH sequence = DM-RS sequence multiplied by d(0)
      y_n[n] = c16mulShift(r_u_v_alpha_delta[n], d, 15);
#ifdef DEBUG_NR_PUCCH_TX
      printf(
          "\t [nr_generate_pucch1] sequence generation \tu=%d \tv=%d \talpha=%lf \tr_u_v_alpha_delta[n=%d]=(%d,%d) "
          "\td=(%d,%d)\ty_n[n=%d]=(%d,%d)\n",
          u,
          v,
          alpha,
          n,
          r_u_v_alpha_delta[n].r,
          r_u_v_alpha_delta[n].i,
	  d.r,d.i,
          n,
          y_n[n].r,
          y_n[n].i);
#endif
    }

    /*
     * The block of complex-valued symbols y(n) shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     * The block of complex-valued symbols r_u_v_alpha_dmrs_delta(n) for DM-RS shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     */
    // the orthogonal sequence index for wi(m) defined in TS 38.213 Subclause 9.2.1
    // the index of the orthogonal cover code is from a set determined as described in [4, TS 38.211]
    // and is indicated by higher layer parameter PUCCH-F1-time-domain-OCC
    // In the PUCCH_Config IE, the PUCCH-format1, timeDomainOCC field
    const uint8_t w_index = timeDomainOCC;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime
    // and intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on
    // number of PUCCH symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled) N_SF_mprime_PUCCH_1 contains N_SF_mprime
    // from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH symbols nrofSymbols, mprime=0
    // and intra-slot hopping enabled/disabled) mprime is 0 if no intra-slot hopping / mprime is {0,1} if intra-slot hopping
    uint8_t mprime = 0;

    if (intraSlotFrequencyHopping == 0) { // intra-slot hopping disabled
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping disabled\n",
             intraSlotFrequencyHopping);
#endif
      int N_SF_mprime_PUCCH_1 =
          table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols - 1]; // only if intra-slot hopping not enabled (PUCCH)
      int N_SF_mprime_PUCCH_DMRS_1 =
          table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols - 1]; // only if intra-slot hopping not enabled (DM-RS)
      int N_SF_mprime0_PUCCH_1 =
          table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols - 1]; // only if intra-slot hopping not enabled mprime = 0 (PUCCH)
      int N_SF_mprime0_PUCCH_DMRS_1 =
          table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols
                                                        - 1]; // only if intra-slot hopping not enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif

      for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
        c16_t *zPtr = z + mprime * 12 * N_SF_mprime0_PUCCH_1 + m * 12;
        c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],
                       table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m]};
        for (int n=0; n<12 ; n++) {
          zPtr[n] = c16mulShift(table, y_n[n], 15);
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d "
              "+ %d * %d)) = (%d,%d)\n",
              mprime,
              m,
              n,
              (mprime * 12 * N_SF_mprime0_PUCCH_1) + (m * 12) + n,
              table.r,
              y_n[n].r,
              table.i,
              y_n[n].i,
              table.r,
              y_n[n].i,
              table.i,
              y_n[n].r,
              zPtr[n].r,
              zPtr[n].i);
#endif
        }
      }

      for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
        c16_t *zDmrsPtr = z_dmrs + mprime * 12 * N_SF_mprime0_PUCCH_DMRS_1 + m * 12;
        c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m],
                       table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m]};
        for (int n=0; n<12 ; n++) {
          zDmrsPtr[n] = c16mulShift(table, r_u_v_alpha_delta_dmrs[n], 15);
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d "
              "+ %d * %d)) = (%d,%d)\n",
              mprime,
              m,
              n,
              (mprime * 12 * N_SF_mprime0_PUCCH_1) + (m * 12) + n,
              table.r,
              r_u_v_alpha_delta_dmrs[n].r,
              table.i,
              r_u_v_alpha_delta_dmrs[n].i,
              table.r,
              r_u_v_alpha_delta_dmrs[n].i,
              table.i,
              r_u_v_alpha_delta_dmrs[n].r,
              zDmrsPtr[n].r,
              zDmrsPtr[n].i);
#endif
        }
      }
    }

    if (intraSlotFrequencyHopping == 1) { // intra-slot hopping enabled
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping enabled\n",
             intraSlotFrequencyHopping);
#endif
      int N_SF_mprime_PUCCH_1 =
          table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols - 1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      int N_SF_mprime_PUCCH_DMRS_1 =
          table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols - 1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
      int N_SF_mprime0_PUCCH_1 =
          table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols - 1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      int N_SF_mprime0_PUCCH_DMRS_1 =
          table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols - 1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif

      for (mprime = 0; mprime<2; mprime++) { // mprime can get values {0,1}
        for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
          c16_t *zPtr = z + mprime * 12 * N_SF_mprime0_PUCCH_1 + m * 12;
          c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],
                         table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m]};
          for (int n = 0; n < 12; n++)
            zPtr[n] = c16mulShift(table, y_n[n], 15);
        }

        for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
          c16_t *zDmrsPtr = z_dmrs + mprime * 12 * N_SF_mprime0_PUCCH_DMRS_1 + m * 12;
          c16_t table = {table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m],
                         table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m]};
          for (int n = 0; n < 12; n++)
            zDmrsPtr[n] = c16mulShift(table, r_u_v_alpha_delta_dmrs[n], 15);
        }

        N_SF_mprime_PUCCH_1 =
            table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols - 1]; // only if intra-slot hopping enabled mprime = 1 (PUCCH)
        N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 1 (DM-RS)
      }
    }

    if (n_hop) { // intra-slot hopping enabled, we need to calculate new offset PRB
      startingPRB = pucch_pdu->second_hop_prb + pucch_pdu->bwp_start;
    }

    re_offset = (l + startingSymbolIndex) * frame_parms->ofdm_symbol_size + NR_NB_SC_PER_RB * startingPRB;

    for (int n = 0; n < NR_NB_SC_PER_RB; n++) {
      if (l%2 == 1) { // mapping PUCCH according to TS38.211 subclause 6.4.1.3.1
        txdataF[0][re_offset] = z[i + n];
#ifdef DEBUG_NR_PUCCH_TX
        printf("\t [nr_generate_pucch1] mapping PUCCH to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tz_pucch[%d]=txptr(%u)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp, frame_parms->ofdm_symbol_size, frame_parms->N_RB_DL, i + n, re_offset,
               l, n, txdataF[0][re_offset].r, txdataF[0][re_offset].i);
#endif
      }

      if (l % 2 == 0) { // mapping DM-RS signal according to TS38.211 subclause 6.4.1.3.1
        txdataF[0][re_offset] = z_dmrs[i + n];
#ifdef DEBUG_NR_PUCCH_TX
        printf("\t [nr_generate_pucch1] mapping DM-RS to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tz_dm-rs[%d]=txptr(%u)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp, frame_parms->ofdm_symbol_size, frame_parms->N_RB_DL, i+n, re_offset,
               l, n, txdataF[0][re_offset].r, txdataF[0][re_offset].i);
#endif
      }

      re_offset++;
    }

    if (l % 2 == 1)
      i += 12;
  }
}

// Calculate number of DMRS symbols for PUCCH formats 3 and 4
static uint8_t nr_pucch_get_dmrs_symbols(uint8_t nrofSymbols, uint8_t add_dmrs)
{
  if (nrofSymbols == 4) {
    return 1;
  } else if (nrofSymbols > 4 && nrofSymbols <= 9) {
    return 2;
  } else if (nrofSymbols > 9) {
    return (add_dmrs == 0) ? 2 : 4;
  }
  return 0;
}

// Calculate PUCCH format 2/3/4 rate matching output sequence length according to TS 38.212 Table 6.3.1.4-1
static uint16_t nr_pucch_output_sequence_length(uint8_t format_type,
                                                uint8_t nrofSymbols,
                                                uint16_t nrofPRB,
                                                uint8_t n_SF_PUCCH_s,
                                                uint8_t is_pi_over_2_bpsk_enabled,
                                                uint8_t add_dmrs)
{
  uint16_t M_bit = 0;

  if (format_type == 2) {
    M_bit = 16 * nrofSymbols * nrofPRB;
  } else if (format_type == 3) {
    uint16_t E_init = (is_pi_over_2_bpsk_enabled == 0) ? 24 : 12;
    uint8_t num_dmrs_symbols = nr_pucch_get_dmrs_symbols(nrofSymbols, add_dmrs);
    M_bit = E_init * (nrofSymbols - num_dmrs_symbols) * nrofPRB / n_SF_PUCCH_s;
  } else if (format_type == 4) {
    nrofPRB = 1;
    uint16_t E_init = (is_pi_over_2_bpsk_enabled == 0) ? 24 : 12;
    uint8_t num_dmrs_symbols = nr_pucch_get_dmrs_symbols(nrofSymbols, add_dmrs);
    M_bit = E_init * (nrofSymbols - num_dmrs_symbols) * nrofPRB / n_SF_PUCCH_s;
  }

  return M_bit;
}

static inline void nr_pucch2_3_4_scrambling(uint16_t M_bit, uint16_t rnti, uint16_t n_id, uint64_t *B64, uint8_t *btilde)
{
  // c_init=nRNTI*2^15+n_id according to TS 38.211 Subclause 6.3.2.6.1
  const int roundedSz = (M_bit + 31) / 32;
  uint32_t *seq = gold_cache((rnti << 15) + n_id, roundedSz);
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t\t [nr_pucch2_3_4_scrambling] gold sequence s=%x, M_bit %d\n", *seq, M_bit);
#endif

  uint8_t *btildep = btilde;
  uint32_t *B32 = (uint32_t *)B64;

  for (int iprime = 0; iprime < roundedSz; iprime++, btildep += 32) {
    const uint32_t s = seq[iprime];
    const uint32_t B = B32[iprime];
    LOG_D(PHY, "PUCCH2 encoded: %02x\n", B);
    int M_bit2 = iprime == M_bit / 32 ? M_bit % 32 : 32;
    for (int i = 0; i < M_bit2; i++) {
      uint8_t c = (uint8_t)((s >> i) & 1);
      btildep[i] = (((B>>i)&1) ^ c);
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t\t\t btilde[%d]=%x from unscrambled bit %u and scrambling %d (%x)\n",
             i + (iprime << 5),
             btilde[i],
             ((B >> i) & 1),
             c,
             s >> i);
#endif
    }
  }

#ifdef DEBUG_NR_PUCCH_TX
  printf("\t\t [nr_pucch2_3_4_scrambling] scrambling M_bit=%d bits\n", M_bit);
#endif
}

/*
 * Build a tiled pattern of N bits repeated to fill the required output words,
 * and write it into b[]. Tiling only covers ceil(E/64) words — no unnecessary
 * computation or writes beyond what the caller will read.
 * Requires 0 < N <= 32.
 */
static inline void fill_pattern(uint64_t *b, uint32_t pattern, int N, uint16_t E)
{
  int nwords = (E + 63) / 64;
  memset(b, 0, nwords * sizeof(uint64_t));
  int ncopies = (E + N - 1) / N;
  for (int k = 0; k < ncopies; k++) {
    int bit = k * N;
    int w = bit / 64;
    int shift = bit % 64;
    b[w] |= (uint64_t)pattern << shift;
    if (shift > 0 && shift + N > 64 && w + 1 < nwords)
      b[w + 1] |= pattern >> (64 - shift);
  }
}

void nr_uci_encoding(uint64_t payload, uint8_t nr_bit, uint8_t aggregation_level, uint16_t E, uint8_t Qm, uint64_t *b, bool onPUSCH)
{
  /*
   * Implementing TS 38.212 Subclause 6.3.1.2 and 6.3.2
   *
   * For A<=11, encoding and rate matching are combined: each branch builds
   * the base N-bit codeword as a pattern and fills E output bits directly
   * via fill_pattern().
   *
   * For A>=12, polar_encoder_fast handles encoding and rate matching internally.
   *
   * Output b[] is always a packed bitfield: bit i is at b[i/64] position (i%64).
   *
   * Placeholder values from Table 5.3.3.2-1 are resolved at encoding time:
   *   x -> 1
   *   y -> b(i-1)  (previous output bit; only appears in the A=1 case)
   */
  uint8_t A = nr_bit;

#ifdef DEBUG_NR_PUCCH_TX
  printf("\t\t [nr_uci_encoding] start function with encoding A=%d bits into M_bit=%d\n", A, E);
#endif

  memset(b, 0, 8 * sizeof(uint64_t));
  if (A < 12) {
    uint32_t pattern = encodeSmallBlock(payload, A, Qm);
    int N = A == 1 ? Qm : A == 2 ? 3 * Qm : 32;
    fill_pattern(b, pattern, N, E);
  } else { // A >= 12
    // Polar encoder handles encoding and rate matching internally
    payload = reverse_bits(payload, A);
    int type = onPUSCH ? NR_POLAR_UCI_PUSCH_MESSAGE_TYPE : NR_POLAR_UCI_PUCCH_MESSAGE_TYPE;
    polar_encoder_fast(&payload, b, 0, 0, type, A, aggregation_level);
  }
}

void nr_generate_pucch2(c16_t **txdataF,
                        const NR_DL_FRAME_PARMS *frame_parms,
                        const int16_t amp16,
                        const int nr_slot_tx,
                        const fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch2] start function at slot(nr_slot_tx)=%d  with payload=%lu and nr_bit=%d\n",nr_slot_tx, pucch_pdu->payload, pucch_pdu->n_bit);
#endif
  // b is the block of bits transmitted on the physical channel after payload coding
  uint64_t b[16] = {0}; // limit to 1024-bit encoded length
  // M_bit is the number of bits of block b (payload after encoding)
  uint16_t M_bit = nr_pucch_output_sequence_length(pucch_pdu->format_type, pucch_pdu->nr_of_symbols, pucch_pdu->prb_size, 0, 0, 0);
  nr_uci_encoding(pucch_pdu->payload, pucch_pdu->n_bit, pucch_pdu->prb_size, M_bit, 0, &b[0], false);
  /*
   * Implementing TS 38.211
   * Subclauses 6.3.2.5.1 Scrambling (PUCCH format 2)
   * The block of bits b(0),..., b(M_bit-1 ), where M_bit is the number of bits transmitted on the physical channel,
   * shall be scrambled prior to modulation,
   * resulting in a block of scrambled bits btilde(0),...,btilde(M_bit-1) according to
   *                     btilde(i)=(b(i)+c(i))mod 2
   * where the scrambling sequence c(i) is given by clause 5.2.1.
   * The scrambling sequence generator shall be initialized with c_init=nRNTI*2^15+n_id
   * n_id = {0,1,...,1023}  equals the higher-layer parameter Data-scrambling-Identity if configured
   * n_id = N_ID_cell       if higher layer parameter not configured
   */
  uint8_t btilde[M_bit];
  memset(btilde, 0, sizeof(btilde));
  // rnti is given by the C-RNTI
  uint16_t rnti=pucch_pdu->rnti;
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch2] rnti = %d ,\n",rnti);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.5.1 scrambling format 2
   */
  nr_pucch2_3_4_scrambling(M_bit, rnti, pucch_pdu->data_scrambling_id, b, btilde);

#ifdef POLAR_CODING_DEBUG
  printf("bt:");
  for (int n = 0; n < M_bit; n++) {
    if (n % 4 == 0) {
      printf(" ");
    }
    printf("%u", btilde[n]);
  }
  printf("\n");
#endif

  /*
   * Implementing TS 38.211 Subclause 6.3.2.5.2 modulation format 2
   * btilde shall be modulated as described in subclause 5.1 using QPSK
   * resulting in a block of complex-valued modulation symbols d(0),...,d(m_symbol) where m_symbol=M_bit/2
   */
  //#define ONE_OVER_SQRT2_S 23171 // 32767/sqrt(2) = 23170 (ONE_OVER_SQRT2)
  // complex-valued symbol d(0)
  c16_t d[M_bit];
  memset(d, 0, sizeof(d));
  uint16_t m_symbol = (M_bit%2==0) ? M_bit/2 : floor(M_bit/2)+1;
  const int32_t amp = amp16;
  const int16_t baseVal = (amp * ONE_OVER_SQRT2) >> 15;
  c16_t qpskSymbols[4] = {{baseVal, baseVal}, {baseVal, -baseVal}, {-baseVal, baseVal}, {-baseVal, -baseVal}};
  for (int i=0; i < m_symbol; i++) { // QPSK modulation subclause 5.1.3
    int tmp = (btilde[2 * i] & 1) * 2 + (btilde[(2 * i) + 1] & 1);
    d[i] = qpskSymbols[tmp];

#ifdef DEBUG_NR_PUCCH_TX
    printf("\t [nr_generate_pucch2] modulation of bit pair btilde(%d,%d), m_symbol=%d, d(%d)=(%d,%d)\n",
           (btilde[2 * i] & 1),
           (btilde[(2 * i) + 1] & 1),
           m_symbol,
           i,
           d[i].r,
           d[i].i);
#endif
  }

  /*
   * Implementing TS 38.211 Subclause 6.3.2.5.3 Mapping to physical resources
   */
  // int32_t *txptr;
  int outSample = 0;
  uint8_t  startingSymbolIndex = pucch_pdu->start_symbol_index;
  uint16_t startingPRB = pucch_pdu->prb_start + pucch_pdu->bwp_start;

  for (int l = 0; l < pucch_pdu->nr_of_symbols; l++) {
    // c_init calculation according to TS38.211 subclause
    uint64_t temp_x2 = 1ll << 17;
    temp_x2 *= 14UL * nr_slot_tx + l + startingSymbolIndex + 1;
    temp_x2 *= 2UL * pucch_pdu->dmrs_scrambling_id + 1;
    temp_x2 = (temp_x2 + 2ULL * pucch_pdu->dmrs_scrambling_id) % (1UL << 31);
    uint32_t idxGold = startingPRB * NR_PUCCH_DMRS_RB * DMRS_MOD_ORDER / 32;
    uint32_t m = (startingPRB * NR_PUCCH_DMRS_RB * DMRS_MOD_ORDER) % 32;
    const int seq_sz = ((startingPRB + pucch_pdu->prb_size) * NR_PUCCH_DMRS_RB * DMRS_MOD_ORDER + 31) / 32;
    uint32_t *seq = gold_cache(temp_x2, seq_sz);
    int symbol_offset = (l + startingSymbolIndex) * frame_parms->ofdm_symbol_size;
    for (int rb = 0; rb < pucch_pdu->prb_size; rb++) {
      const int baseRB = rb + startingPRB;
      int re_offset = NR_NB_SC_PER_RB * baseRB;
      int k=0;
#ifdef DEBUG_NR_PUCCH_TX
      int kk=0;
#endif

      for (int n = 0; n < 12; n++) {

        if (n % 3 != 1) { // mapping PUCCH according to TS38.211 subclause 6.3.2.5.3
          txdataF[0][re_offset + symbol_offset] = d[outSample + k];
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch2] (n=%d) mapping PUCCH to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d "
              "\tz_pucch[%d]=txptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
              n,
              amp,
              frame_parms->ofdm_symbol_size,
              frame_parms->N_RB_DL,
              k,
              re_offset,
              l,
              n,
              txdataF[0][re_offset + symbol_offset].r,
              txdataF[0][re_offset + symbol_offset].i);
#endif
          k++;
        }

        if (n % 3 == 1) { // mapping DM-RS signal according to TS38.211 subclause 6.4.1.3.2
          txdataF[0][re_offset + symbol_offset].r = (int16_t)(baseVal * (1 - (2 * ((uint8_t)((seq[idxGold] >> (m)) & 1)))));
          txdataF[0][re_offset + symbol_offset].i = (int16_t)(baseVal * (1 - (2 * ((uint8_t)((seq[idxGold] >> (m + 1)) & 1)))));
          m += 2;
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch2] (n=%d) mapping DM-RS to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d "
              "\tz_dm-rs[%d]=txptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
              n,
              amp,
              frame_parms->ofdm_symbol_size,
              frame_parms->N_RB_DL,
              kk,
              re_offset,
              l,
              n,
              txdataF[0][re_offset + symbol_offset].r,
              txdataF[0][re_offset + symbol_offset].i);
          kk++;
#endif
        }
        re_offset++;
      }

      outSample += 8;

      if (m % 32 == 0) {
        idxGold++;
        m = 0;
      }
    }
  }
}

void nr_generate_pucch3_4(c16_t **txdataF,
                          const NR_DL_FRAME_PARMS *frame_parms,
                          const int16_t amp16,
                          const int nr_slot_tx,
                          const fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch3_4] start function at slot(nr_slot_tx)=%d with payload=%lu and nr_bit=%d\n", nr_slot_tx, pucch_pdu->payload, pucch_pdu->n_bit);
#endif
  // b is the block of bits transmitted on the physical channel after payload coding
  uint64_t b[16];
  // M_bit is the number of bits of block b (payload after encoding)
  uint16_t M_bit = 0;
  // parameter PUCCH-F4-preDFT-OCC-length set of {2,4} -> to use table -1 or -2
  // in format 4, n_SF_PUCCH_s = {2,4}, provided by higher layer parameter PUCCH-F4-preDFT-OCC-length (in format 3 n_SF_PUCCH_s=1)
  uint8_t n_SF_PUCCH_s;
  if (pucch_pdu->format_type == 3)
    n_SF_PUCCH_s = 1;
  else
    n_SF_PUCCH_s = pucch_pdu->pre_dft_occ_len;
  uint8_t is_pi_over_2_bpsk_enabled = pucch_pdu->pi_2bpsk;
  // Intra-slot frequency hopping shall be assumed when the higher-layer parameter intraSlotFrequencyHopping is provided,
  // regardless of whether the frequency-hop distance is zero or not,
  // otherwise no intra-slot frequency hopping shall be assumed
  //uint8_t PUCCH_Frequency_Hopping = 0 ; // from higher layers
  uint8_t intraSlotFrequencyHopping = 0;

  if (pucch_pdu->freq_hop_flag) {
    intraSlotFrequencyHopping=1;
#ifdef DEBUG_NR_PUCCH_TX
    printf("\t [nr_generate_pucch3_4] intraSlotFrequencyHopping=%d \n",intraSlotFrequencyHopping);
#endif
  }
  const int32_t amp = amp16;
  uint8_t nrofSymbols = pucch_pdu->nr_of_symbols;
  uint16_t nrofPRB = pucch_pdu->prb_size;
  uint16_t startingPRB = pucch_pdu->prb_start + pucch_pdu->bwp_start;
  uint8_t add_dmrs = pucch_pdu->add_dmrs_flag;

  M_bit = nr_pucch_output_sequence_length(pucch_pdu->format_type,
                                          nrofSymbols,
                                          nrofPRB,
                                          n_SF_PUCCH_s,
                                          is_pi_over_2_bpsk_enabled,
                                          add_dmrs);

  nr_uci_encoding(pucch_pdu->payload, pucch_pdu->n_bit, nrofPRB, M_bit, 0, b, false);
  /*
   * Implementing TS 38.211
   * Subclauses 6.3.2.6.1 Scrambling (PUCCH formats 3 and 4)
   * The block of bits b(0),..., b(M_bit-1 ), where M_bit is the number of bits transmitted on the physical channel,
   * shall be scrambled prior to modulation,
   * resulting in a block of scrambled bits btilde(0),...,btilde(M_bit-1) according to
   *                     btilde(i)=(b(i)+c(i))mod 2
   * where the scrambling sequence c(i) is given by clause 5.2.1.
   * The scrambling sequence generator shall be initialized with c_init=nRNTI*2^15+n_id
   * n_id = {0,1,...,1023}  equals the higher-layer parameter Data-scrambling-Identity if configured
   * n_id = N_ID_cell       if higher layer parameter not configured
   */
  uint8_t btilde[M_bit];
  memset(btilde, 0, sizeof(btilde));
  
  // rnti is given by the C-RNTI
  uint16_t rnti=pucch_pdu->rnti, n_id=0;
#ifdef DEBUG_NR_PUCCH_TX
  printf("\t [nr_generate_pucch3_4] rnti = %d ,\n",rnti);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.6.1 scrambling formats 3 and 4
   */
  nr_pucch2_3_4_scrambling(M_bit,rnti,n_id,&b[0],btilde);
  /*
   * Implementing TS 38.211 Subclause 6.3.2.6.2 modulation formats 3 and 4
   *
   * Subclause 5.1.1 PI/2-BPSK
   * Subclause 5.1.3 QPSK
   */
  // complex-valued symbol d(0)
  c16_t d[M_bit];
  memset(d, 0, sizeof(d));

  uint16_t m_symbol = (M_bit%2==0) ? M_bit/2 : floor(M_bit/2)+1;
  const int16_t baseVal = (amp * ONE_OVER_SQRT2) >> 15;

  if (is_pi_over_2_bpsk_enabled == 0) {
    // using QPSK if PUCCH format 3,4 and pi/2-BPSK is not configured, according to subclause 6.3.2.6.2
    c16_t qpskSymbols[4] = {{baseVal, baseVal}, {-baseVal, baseVal}, {-baseVal, baseVal}, {-baseVal, -baseVal}};
    for (int i=0; i < m_symbol; i++) { // QPSK modulation subclause 5.1.3
      int tmp = (btilde[2 * i] & 1) * 2 + (btilde[(2 * i) + 1] & 1);
      d[i] = qpskSymbols[tmp];
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch3_4] modulation QPSK of bit pair btilde(%d,%d), m_symbol=%d, d(%d)=(%d,%d)\n",
             (btilde[2 * i] & 1),
             (btilde[(2 * i) + 1] & 1),
             m_symbol,
             i,
             d[i].r,
             d[i].i);
#endif
    }
  }

  if (is_pi_over_2_bpsk_enabled == 1) {
    // using PI/2-BPSK if PUCCH format 3,4 and pi/2-BPSK is configured, according to subclause 6.3.2.6.2
    m_symbol = M_bit;
    c16_t qpskSymbols[4] = {{baseVal, baseVal}, {-baseVal, baseVal}, {-baseVal, -baseVal}, {baseVal, -baseVal}};
    for (int i=0; i<m_symbol; i++) { // PI/2-BPSK modulation subclause 5.1.1
      int tmp = (btilde[i] & 1) * 2 + (i & 1);
      d[i] = qpskSymbols[tmp];
#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch3_4] modulation PI/2-BPSK of bit btilde(%d), m_symbol=%d, d(%d)=(%d,%d)\n",
             (btilde[i] & 1),
             m_symbol,
             i,
             d[i].r,
             d[i].i);
#endif
    }
  }

  /*
   * Implementing Block-wise spreading subclause 6.3.2.6.3
   */
  // number of PRBs per PUCCH, provided by higher layers parameters PUCCH-F2-number-of-PRBs or PUCCH-F3-number-of-PRBs (for format 4, it is equal to 1)
  // for PUCCH 3 -> nrofPRBs = (2^alpa2 * 3^alpha3 * 5^alpha5)
  // for PUCCH 4 -> nrofPRBs = 1
  // uint8_t nrofPRBs;
  // number of symbols, provided by higher layers parameters PUCCH-F0-F2-number-of-symbols or PUCCH-F1-F3-F4-number-of-symbols
  // uint8_t nrofSymbols;
  // complex-valued symbol d(0)
  c16_t y_n[4 * M_bit]; // 4 is the maximum number n_SF_PUCCH_s, so is the maximunm size of y_n
  memset(y_n, 0, sizeof(y_n));
  // Re part orthogonal sequences w_n(k) for PUCCH format 4 when N_SF_PUCCH4 = 2 (Table 6.3.2.6.3-1)
  // k={0,..11} n={0,1,2,3}
  // parameter PUCCH-F4-preDFT-OCC-index set of {0,1,2,3} -> n
  const uint16_t table_6_3_2_6_3_1_Wn_Re[2][12] = {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
                                                   {1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1}};
  // Im part orthogonal sequences w_n(k) for PUCCH format 4 when N_SF_PUCCH4 = 2 (Table 6.3.2.6.3-1)
  // k={0,..11} n={0,1}
  const uint16_t table_6_3_2_6_3_1_Wn_Im[2][12] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  // Re part orthogonal sequences w_n(k) for PUCCH format 4 when N_SF_PUCCH4 = 4 (Table 6.3.2.6.3-2)
  // k={0,..11} n={0,1,2.3}
  const uint16_t table_6_3_2_6_3_2_Wn_Re[4][12] = {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
                                                   {1, 1, 1, 0, 0, 0, -1, -1, -1, 0, 0, 0},
                                                   {1, 1, 1, -1, -1, -1, 1, 1, 1, -1, -1, -1},
                                                   {1, 1, 1, 0, 0, 0, -1, -1, -1, 0, 0, 0}};
  // Im part orthogonal sequences w_n(k) for PUCCH format 4 when N_SF_PUCCH4 = 4 (Table 6.3.2.6.3-2)
  // k={0,..11} n={0,1,2,3}
  const uint16_t table_6_3_2_6_3_2_Wn_Im[4][12] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                   {0, 0, 0, -1, -1, -1, 0, 0, 0, 1, 1, 1},
                                                   {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                   {0, 0, 0, 1, 1, 1, 0, 0, 0, -1, -1, -1}};

  uint8_t occ_Index  = pucch_pdu->pre_dft_occ_idx;  // higher layer parameter occ-Index

  //occ_Index = 1; //only for testing purposes; to be removed FIXME!!!
  if (pucch_pdu->format_type == 3) { // no block-wise spreading for format 3

    for (int l=0; l < floor(m_symbol/(12*nrofPRB)); l++) {
      for (int k = l * 12 * nrofPRB; k < (l + 1) * 12 * nrofPRB; k++) {
        y_n[k] = d[k];
#ifdef DEBUG_NR_PUCCH_TX
        printf(
            "\t [nr_generate_pucch3_4] block-wise spreading for format 3 (no block-wise spreading): (l,k)=(%d,%d)\ty_n(%d)   = "
            "\t(d_re=%d, d_im=%d)\n",
            l,
            k,
            k,
            d[k].r,
            d[k].i);
#endif
      }
    }
  }

  if (pucch_pdu->format_type == 4) {
    nrofPRB = 1;

    for (int l=0; l < floor((n_SF_PUCCH_s*m_symbol)/(12*nrofPRB)); l++) {
      for (int k=0; k < (12*nrofPRB); k++) {
        c16_t dVal = d[l * (12 * nrofPRB / n_SF_PUCCH_s) + k % (12 * nrofPRB / n_SF_PUCCH_s)];
        c16_t *yPtr = y_n + l * 12 * nrofPRB + k;
        if (n_SF_PUCCH_s == 2) {
          c16_t table = {table_6_3_2_6_3_1_Wn_Re[occ_Index][k], table_6_3_2_6_3_1_Wn_Im[occ_Index][k]};
          *yPtr = c16mulShift(table, dVal, 15);
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch3_4] block-wise spreading for format 4 (n_SF_PUCCH_s 2) (occ_Index=%d): (l,k)=(%d,%d)\ty_n(%d) "
              "  = \t(d_re=%d, d_im=%d)\n",
              occ_Index,
              l,
              k,
              l * (12 * nrofPRB) + k,
              yPtr->r,
              yPtr->i);
#endif
        }

        if (n_SF_PUCCH_s == 4) {
          c16_t table = {table_6_3_2_6_3_2_Wn_Re[occ_Index][k], table_6_3_2_6_3_2_Wn_Im[occ_Index][k]};
          *yPtr = c16mulShift(table, dVal, 15);
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch3_4] block-wise spreading for format 4 (n_SF_PUCCH_s 4) (occ_Index=%d): (l,k)=(%d,%d)\ty_n(%d) "
              "  = \t(d_re=%d, d_im=%d)\n",
              occ_Index,
              l,
              k,
              l * 12 * nrofPRB + k,
              yPtr->r,
              yPtr->i);
#endif
        }
      }
    }
  }

  /*
   * Implementing Transform pre-coding subclause 6.3.2.6.4
   */
  c16_t z[4 * M_bit]; // 4 is the maximum number n_SF_PUCCH_s
  memset(z, 0 , sizeof(z));

#define M_PI 3.14159265358979323846 // pi
  const int64_t base = round(32767 / sqrt(12 * nrofPRB));
  for (int l=0; l<floor((n_SF_PUCCH_s*m_symbol)/(12*nrofPRB)); l++) {
    for (int k=0; k<(12*nrofPRB); k++) {
      c16_t *yPtr = y_n + l * 12 * nrofPRB;
      c16_t *zPtr = z + l * 12 * nrofPRB + k;
      *zPtr = (c16_t){0};
      for (int m = l * 12 * nrofPRB; m < (l + 1) * 12 * nrofPRB; m++) {
        const c16_t angle = {lround(32767 * cos(2 * M_PI * m * k / (12 * nrofPRB))),
                             lround(32767 * sin(2 * M_PI * m * k / (12 * nrofPRB)))};
        c16_t tmp = c16mulShift(yPtr[m], angle, 15);
        csum(*zPtr, *zPtr, c16mulRealShift(tmp, base, 15));
      }

#ifdef DEBUG_NR_PUCCH_TX
      printf("\t [nr_generate_pucch3_4] transform precoding for formats 3 and 4: (l,k)=(%d,%d)\tz(%d)   = \t(%d, %d)\n",
             l,
             k,
             l * (12 * nrofPRB) + k,
             zPtr->r,
             zPtr->i);
#endif
    }
  }

  /*
   * Implementing TS 38.211 Subclauses 6.3.2.5.3 and 6.3.2.6.5 Mapping to physical resources
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled, intraSlotFrequencyHopping is not provided
  //              n_hop = 0
  // if frequency hopping is enabled,  intraSlotFrequencyHopping is     provided
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal = 0 ;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  //uint8_t lprime = startingSymbolIndex;
  // m0 is the cyclic shift index calculated depending on the Orthogonal sequence index n, according to table 6.4.1.3.3.1-1 from TS 38.211 subclause 6.4.1.3.3.1
  uint8_t m0 = 0;
  uint8_t mcs=0;

  if (pucch_pdu->format_type == 3)
    m0 = 0;

  if (pucch_pdu->format_type == 4) {
    if (n_SF_PUCCH_s == 2) {
      m0 = (occ_Index == 0) ? 0 : 6;
    }

    if (n_SF_PUCCH_s == 4) {
      m0 = (occ_Index == 3) ? 9 : ((occ_Index == 2) ? 3 : ((occ_Index == 1) ? 6 : 0));
    }
  }

  double alpha;
  int N_ZC = 12 * nrofPRB;
  c16_t r_u_v_base[N_ZC];
  uint32_t re_offset = 0;
  uint8_t table_6_4_1_3_3_2_1_dmrs_positions[11][14] = {
    {(intraSlotFrequencyHopping==0)?0:1,(intraSlotFrequencyHopping==0)?1:0,(intraSlotFrequencyHopping==0)?0:1,0,0,0,0,0,0,0,0,0,0,0}, // PUCCH length = 4
    {1,0,0,1,0,0,0,0,0,0,0,0,0,0}, // PUCCH length = 5
    {0,1,0,0,1,0,0,0,0,0,0,0,0,0}, // PUCCH length = 6
    {0,1,0,0,1,0,0,0,0,0,0,0,0,0}, // PUCCH length = 7
    {0,1,0,0,0,1,0,0,0,0,0,0,0,0}, // PUCCH length = 8
    {0,1,0,0,0,0,1,0,0,0,0,0,0,0}, // PUCCH length = 9
    {0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),(add_dmrs==0?0:1),0,0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),(add_dmrs==0?0:1),0,0,0,0,0}, // PUCCH length = 10
    {0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),(add_dmrs==0?0:1),0,0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0,0,0}, // PUCCH length = 11
    {0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0,0}, // PUCCH length = 12
    {0,(add_dmrs==0?0:1),(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0,(add_dmrs==0?0:1),0,(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0}, // PUCCH length = 13
    {0,(add_dmrs==0?0:1),0,(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0,0,(add_dmrs==0?0:1),0,(add_dmrs==0?1:0),0,(add_dmrs==0?0:1),0}  // PUCCH length = 14
  };
  int k = 0;

  for (int l=0; l<nrofSymbols; l++) {
    if ((intraSlotFrequencyHopping == 1) && (l >= (int)floor(nrofSymbols / 2)))
      n_hop = 1; // n_hop = 1 for second hop

    pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);
    nr_group_sequence_hopping(pucch_GroupHopping,pucch_pdu->hopping_id,n_hop,nr_slot_tx,&u,&v); // calculating u and v value

    // Next we proceed to calculate base sequence for DM-RS signal, according to TS 38.211 subclause 6.4.1.33
    if (nrofPRB >= 3) { // TS 38.211 subclause 5.2.2.1 (Base sequences of length 36 or larger) applies
      int i = 4;

      while (list_of_prime_numbers[i] < (12*nrofPRB)) i++;

      N_ZC = list_of_prime_numbers[i+1]; // N_ZC is given by the largest prime number such that N_ZC < (12*nrofPRB)
      double q_base = (N_ZC*(u+1))/31;
      int8_t q = (uint8_t)floor(q_base + (1/2));
      q = ((uint8_t)floor(2*q_base)%2 == 0 ? q+v : q-v);

      for (int n=0; n<(12*nrofPRB); n++) {
        const double tmp = M_PI * q * (n % N_ZC) * ((n % N_ZC) + 1) / N_ZC;
        r_u_v_base[n].r = lround(amp * 32767 * cos(tmp)) >> 15;
        r_u_v_base[n].i = -lround(amp * 32767 * sin(tmp)) >> 15;
#ifdef DEBUG_NR_PUCCH_TX
        printf("\t [nr_generate_pucch3_4] generation DM-RS base sequence when nrofPRB=%d >= 3: r_u_v_base[n=%d]=(%d,%d)\n",
               nrofPRB,
               n,
               r_u_v_base[n].r,
               r_u_v_base[n].i);
#endif
      }
    }

    if (nrofPRB == 2) { // TS 38.211 subclause 5.2.2.2 (Base sequences of length less than 36 using table 5.2.2.2-4) applies
      for (int n = 0; n < 12 * nrofPRB; n++) {
        c16_t table = {table_5_2_2_2_4_Re[u][n], table_5_2_2_2_4_Im[u][n]};
        r_u_v_base[n] = c16mulRealShift(table, amp, 15);
#ifdef DEBUG_NR_PUCCH_TX
        printf("\t [nr_generate_pucch3_4] generation DM-RS base sequence when nrofPRB=%d == 2: r_u_v_base[n=%d]=(%d,%d)\n",
               nrofPRB,
               n,
               r_u_v_base[n].r,
               r_u_v_base[n].i);
#endif
      }
    }

    if (nrofPRB == 1) { // TS 38.211 subclause 5.2.2.2 (Base sequences of length less than 36 using table 5.2.2.2-2) applies
      for (int n = 0; n < 12 * nrofPRB; n++) {
        c16_t table = {table_5_2_2_2_2_Re[u][n], table_5_2_2_2_2_Im[u][n]};
        r_u_v_base[n] = c16mulRealShift(table, amp, 15);
#ifdef DEBUG_NR_PUCCH_TX
        printf("\t [nr_generate_pucch3_4] generation DM-RS base sequence when nrofPRB=%d == 1: r_u_v_base[n=%d]=(%d,%d)\n",
               nrofPRB,
               n,
               r_u_v_base[n].r,
               r_u_v_base[n].i);
#endif
      }
    }

    uint8_t  startingSymbolIndex = pucch_pdu->start_symbol_index;
    uint16_t j=0;
    alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id,m0,mcs,l,startingSymbolIndex,nr_slot_tx);

    for (int rb=0; rb<nrofPRB; rb++) {
      const int baseRB = rb + startingPRB;
      if ((intraSlotFrequencyHopping == 1) && (l<floor(nrofSymbols/2))) { // intra-slot hopping enabled, we need to calculate new offset PRB
        startingPRB = startingPRB + pucch_pdu->second_hop_prb;
      }
      re_offset = (l + startingSymbolIndex) * frame_parms->ofdm_symbol_size + NR_NB_SC_PER_RB * baseRB;

#ifdef DEBUG_NR_PUCCH_TX
      printf("re_offset=%u,baseRB=%d\n", re_offset, baseRB);
#endif

      for (int n = 0; n < NR_NB_SC_PER_RB; n++) {
        if (table_6_4_1_3_3_2_1_dmrs_positions[nrofSymbols-4][l] == 0) { // mapping PUCCH according to TS38.211 subclause 6.3.2.5.3
          txdataF[0][re_offset] = z[n + k];
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch3_4] (l=%d,rb=%d,n=%d,k=%d) mapping PUCCH to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d "
              "\tz_pucch[%d]=txptr(%u)=(z(l=%d,n=%d)=(%d,%d))\n",
              l,
              rb,
              n,
              k,
              amp,
              frame_parms->ofdm_symbol_size,
              frame_parms->N_RB_DL,
              n + k,
              re_offset,
              l,
              n,
              txdataF[0][re_offset].r,
              txdataF[0][re_offset].i);
#endif
        }
        if (table_6_4_1_3_3_2_1_dmrs_positions[nrofSymbols-4][l] == 1) { // mapping DM-RS signal according to TS38.211 subclause 6.4.1.3.2
          const c16_t angle = {lround(32767 * cos(alpha * ((n + j) % N_ZC))), lround(32767 * sin(alpha * ((n + j) % N_ZC)))};
          txdataF[0][re_offset] = c16mulShift(angle, r_u_v_base[n + j], 15);
#ifdef DEBUG_NR_PUCCH_TX
          printf(
              "\t [nr_generate_pucch3_4] (l=%d,rb=%d,n=%d,j=%d) mapping DM-RS to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d "
              "\tz_dm-rs[%d]=txptr(%u)=(r_u_v(l=%d,n=%d)=(%d,%d))\n",
              l,
              rb,
              n,
              j,
              amp,
              frame_parms->ofdm_symbol_size,
              frame_parms->N_RB_DL,
              n + j,
              re_offset,
              l,
              n,
              txdataF[0][re_offset].r,
              txdataF[0][re_offset].i);
#endif
        }

        re_offset++;
      }

      if (table_6_4_1_3_3_2_1_dmrs_positions[nrofSymbols-4][l] == 0) k+=12;

      if (table_6_4_1_3_3_2_1_dmrs_positions[nrofSymbols-4][l] == 1) j+=12;
    }
  }
}
