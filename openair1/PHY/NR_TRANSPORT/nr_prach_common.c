/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Common routines for NR UE/gNB PRACH physical channel V15.4 2019-03
 */
#include "PHY/sse_intrin.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/NR_TRANSPORT/nr_prach.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "common/utils/LOG/log.h"
#include "T.h"

/* Extended Euclidean Algorithm to compute modulo inverse */
static int modulo_multiplicative_inverse(const int a, const int m)
{
  DevAssert(a < m);
  int t = 0;
  int newt = 1;
  int r = m;
  int newr = a;

  while (newr != 0) {
    const int q = r / newr;
    int tmp = t;
    t = newt;
    newt = tmp - q * newt;
    tmp = r;
    r = newr;
    newr = tmp - q * newr;
  }

  AssertFatal(r <= 1, "Modulo inverse doesn't exist for a %d, m %d\n", a, m);
  t = (t < 0) ? t + m : t;
  LOG_D(PHY, "Modulo %d inverse of %d is %d\n", m, a, t);
  return t;
}

// This function computes the du
void nr_fill_du(uint16_t N_ZC, const uint16_t* prach_root_sequence_map, uint16_t nr_du[NR_PRACH_SEQ_LEN_L - 1])
{

  uint16_t iu,u,p;

  for (iu=0; iu<(N_ZC-1); iu++) {

    u=prach_root_sequence_map[iu];
    p=1;

    while (((u*p)%N_ZC)!=1)
      p++;

    nr_du[u] = ((p<(N_ZC>>1)) ? p : (N_ZC-p));
  }

}

void compute_nr_prach_seq(uint8_t short_sequence, uint8_t num_sequences, uint8_t rootSequenceIndex, c16_t X_u[64][839])
{
  // Compute DFT of x_u => X_u[k] = x_u(inv(u)*k)^* X_u[k] = exp(j\pi u*inv(u)*k*(inv(u)*k+1)/N_ZC)
  LOG_D(PHY,"compute_prach_seq: prach short sequence %x, num_sequences %d, rootSequenceIndex %d\n", short_sequence, num_sequences, rootSequenceIndex);

  const int N_ZC = short_sequence ? NR_PRACH_SEQ_LEN_S : NR_PRACH_SEQ_LEN_L;
  const uint16_t* prach_root_sequence_map = short_sequence ? prach_root_sequence_map_abc : prach_root_sequence_map_0_3;
  for (int i = 0; i < num_sequences; i++) {
    int index = (rootSequenceIndex + i) % (N_ZC - 1);
    if (short_sequence) {
      // prach_root_sequence_map points to prach_root_sequence_map4
      DevAssert(index < sizeofArray(prach_root_sequence_map_abc));
    } else {
      // prach_root_sequence_map points to prach_root_sequence_map0_3
      DevAssert(index < sizeofArray(prach_root_sequence_map_0_3));
    }

    const uint u = prach_root_sequence_map[index];
    LOG_D(PHY,"prach index %d => u=%d\n",index,u);
    const int inv_u = modulo_multiplicative_inverse(u, N_ZC); // multiplicative inverse of u

    // X_u[0] stores the first ZC sequence where the root u has a non-zero number of shifts
    // for the unrestricted case X_u[0] is the first root indicated by the rootSequenceIndex

    const int zc_inv_2 = modulo_multiplicative_inverse(2, N_ZC);
    for (int k = 0; k < N_ZC; k++) {
      const unsigned int j = (((k * (1 + inv_u * k)) % N_ZC) * zc_inv_2) % N_ZC;
      const double w = 2 * M_PI * j / N_ZC;
      const c16_t ru = {.r = (int16_t)(floor(32767.0 * cos(w))), .i = (int16_t)(floor(32767.0 * sin(w)))};
      // multiply by inverse of 2 (required since ru is exp[j 2\pi n])
      X_u[i][k] = ru;
    }
  }
  LOG_D(PHY, "compute_prach_seq: done init prach_tables\n");
}
