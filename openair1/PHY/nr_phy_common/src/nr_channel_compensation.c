/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_channel_compensation.h"
#include "PHY/sse_intrin.h"
#include "PHY/impl_defs_top.h"
#include <simde/x86/avx2.h>
#ifdef __aarch64__
#define USE_128BIT
#endif

void nr_channel_compensation(uint32_t buffer_length,
                             uint32_t pdsch_buf_size_max,
                             int nb_rx_ant,
                             int nb_layers,
                             c16_t rxFext[nb_rx_ant][buffer_length],
                             c16_t chFext[nb_layers][nb_rx_ant][buffer_length],
                             c16_t ch_maga[nb_layers][pdsch_buf_size_max],
                             c16_t ch_magb[nb_layers][pdsch_buf_size_max],
                             c16_t ch_magc[nb_layers][pdsch_buf_size_max],
                             c16_t **rxComp,
                             c16_t (*rho)[nb_layers][pdsch_buf_size_max],
                             c16_t cpe,
                             int mod_order,
                             uint32_t symbol,
                             uint32_t output_shift)
{
#ifndef USE_128BIT
  simde__m256i QAM_ampa_256 = simde_mm256_setzero_si256();
  simde__m256i QAM_ampb_256 = simde_mm256_setzero_si256();
  simde__m256i QAM_ampc_256 = simde_mm256_setzero_si256();

  if (mod_order == 4) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM16_n1);
  } else if (mod_order == 6) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM64_n1);
    QAM_ampb_256 = simde_mm256_set1_epi16(QAM64_n2);
  } else if (mod_order == 8) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM256_n1);
    QAM_ampb_256 = simde_mm256_set1_epi16(QAM256_n2);
    QAM_ampc_256 = simde_mm256_set1_epi16(QAM256_n3);
  }

  const simde__m256i cpe256 = simde_mm256_set_epi16(cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r,
                                                    cpe.i,
                                                    cpe.r);
  for (int aatx = 0; aatx < nb_layers; aatx++) {
    simde__m256i *rxComp_256 = (simde__m256i *)&rxComp[aatx][symbol * buffer_length];
    simde__m256i *ch_maga_256 = (simde__m256i *)ch_maga[aatx];
    simde__m256i *ch_magb_256 = (simde__m256i *)ch_magb[aatx];
    simde__m256i *ch_magc_256 = (simde__m256i *)ch_magc[aatx];

    // First Rx antenna: direct store — eliminates need to pre memset the output buffers
    {
      simde__m256i *rxF_256 = (simde__m256i *)rxFext[0];
      simde__m256i *chF_256 = (simde__m256i *)chFext[aatx][0];

      for (uint32_t i = 0; i < buffer_length >> 3; i++) {
        const simde__m256i chF_cpe256 = oai_mm256_cpx_mult(chF_256[i], cpe256, 15);
        rxComp_256[i] = oai_mm256_cpx_mult_conj(chF_cpe256, rxF_256[i], output_shift);

        if (mod_order > 2) {
          simde__m256i mag = oai_mm256_smadd(chF_256[i], chF_256[i], output_shift);
          mag = simde_mm256_packs_epi32(mag, mag);
          mag = simde_mm256_unpacklo_epi16(mag, mag);
          ch_maga_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampa_256);

          if (mod_order > 4)
            ch_magb_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampb_256);

          if (mod_order > 6)
            ch_magc_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampc_256);
        }
      }

      if (rho) {
        for (int atx = 0; atx < nb_layers; atx++) {
          simde__m256i *rho_256 = (simde__m256i *)rho[aatx][atx];
          simde__m256i *chF2_256 = (simde__m256i *)chFext[atx][0];
          for (uint32_t i = 0; i < buffer_length >> 3; i++)
            rho_256[i] = oai_mm256_cpx_mult_conj(chF_256[i], chF2_256[i], output_shift);
        }
      }
    }

    // Remaining Rx antennas: accumulate (MRC)
    for (int aarx = 1; aarx < nb_rx_ant; aarx++) {
      simde__m256i *rxF_256 = (simde__m256i *)rxFext[aarx];
      simde__m256i *chF_256 = (simde__m256i *)chFext[aatx][aarx];

      for (uint32_t i = 0; i < buffer_length >> 3; i++) {
        const simde__m256i chF_cpe256 = oai_mm256_cpx_mult(chF_256[i], cpe256, 15);
        const simde__m256i comp = oai_mm256_cpx_mult_conj(chF_cpe256, rxF_256[i], output_shift);
        rxComp_256[i] = simde_mm256_add_epi16(rxComp_256[i], comp);

        if (mod_order > 2) {
          simde__m256i mag = oai_mm256_smadd(chF_256[i], chF_256[i], output_shift);
          mag = simde_mm256_packs_epi32(mag, mag);
          mag = simde_mm256_unpacklo_epi16(mag, mag);
          ch_maga_256[i] = simde_mm256_add_epi16(ch_maga_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampa_256));

          if (mod_order > 4)
            ch_magb_256[i] = simde_mm256_add_epi16(ch_magb_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampb_256));

          if (mod_order > 6)
            ch_magc_256[i] = simde_mm256_add_epi16(ch_magc_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampc_256));
        }
      }

      if (rho) {
        for (int atx = 0; atx < nb_layers; atx++) {
          simde__m256i *rho_256 = (simde__m256i *)rho[aatx][atx];
          simde__m256i *chF2_256 = (simde__m256i *)chFext[atx][aarx];
          for (uint32_t i = 0; i < buffer_length >> 3; i++)
            rho_256[i] = simde_mm256_adds_epi16(rho_256[i], oai_mm256_cpx_mult_conj(chF_256[i], chF2_256[i], output_shift));
        }
      }
    }
  }
#else
  simde__m128i QAM_ampa_128 = simde_mm_setzero_si128();
  simde__m128i QAM_ampb_128 = simde_mm_setzero_si128();
  simde__m128i QAM_ampc_128 = simde_mm_setzero_si128();

  if (mod_order == 4) {
    QAM_ampa_128 = simde_mm_set1_epi16(QAM16_n1);
  } else if (mod_order == 6) {
    QAM_ampa_128 = simde_mm_set1_epi16(QAM64_n1);
    QAM_ampb_128 = simde_mm_set1_epi16(QAM64_n2);
  } else if (mod_order == 8) {
    QAM_ampa_128 = simde_mm_set1_epi16(QAM256_n1);
    QAM_ampb_128 = simde_mm_set1_epi16(QAM256_n2);
    QAM_ampc_128 = simde_mm_set1_epi16(QAM256_n3);
  }

  const simde__m128i cpe128 = simde_mm_set_epi16(cpe.i, cpe.r, cpe.i, cpe.r, cpe.i, cpe.r, cpe.i, cpe.r);
  for (int aatx = 0; aatx < nb_layers; aatx++) {
    simde__m128i *rxComp_128 = (simde__m128i *)&rxComp[aatx][symbol * buffer_length];
    simde__m128i *ch_maga_128 = (simde__m128i *)ch_maga[aatx];
    simde__m128i *ch_magb_128 = (simde__m128i *)ch_magb[aatx];
    simde__m128i *ch_magc_128 = (simde__m128i *)ch_magc[aatx];

    // First Rx antenna: direct store — eliminates need to pre memset the output buffers
    {
      simde__m128i *rxF_128 = (simde__m128i *)rxFext[0];
      simde__m128i *chF_128 = (simde__m128i *)chFext[aatx][0];

      for (uint32_t i = 0; i < buffer_length >> 2; i++) {
        const simde__m128i chF_cpe128 = oai_mm_cpx_mult(chF_128[i], cpe128, 15);
        rxComp_128[i] = oai_mm_cpx_mult_conj(chF_cpe128, rxF_128[i], output_shift);

        if (mod_order > 2) {
          simde__m128i mag = oai_mm_smadd(chF_128[i], chF_128[i], output_shift);
          mag = simde_mm_packs_epi32(mag, mag);
          mag = simde_mm_unpacklo_epi16(mag, mag);
          ch_maga_128[i] = simde_mm_mulhrs_epi16(mag, QAM_ampa_128);

          if (mod_order > 4)
            ch_magb_128[i] = simde_mm_mulhrs_epi16(mag, QAM_ampb_128);

          if (mod_order > 6)
            ch_magc_128[i] = simde_mm_mulhrs_epi16(mag, QAM_ampc_128);
        }
      }

      if (rho) {
        for (int atx = 0; atx < nb_layers; atx++) {
          simde__m128i *rho_128 = (simde__m128i *)rho[aatx][atx];
          simde__m128i *chF2_128 = (simde__m128i *)chFext[atx][0];
          for (uint32_t i = 0; i < buffer_length >> 2; i++)
            rho_128[i] = oai_mm_cpx_mult_conj(chF_128[i], chF2_128[i], output_shift);
        }
      }
    }

    // Remaining Rx antennas: accumulate (MRC)
    for (int aarx = 1; aarx < nb_rx_ant; aarx++) {
      simde__m128i *rxF_128 = (simde__m128i *)rxFext[aarx];
      simde__m128i *chF_128 = (simde__m128i *)chFext[aatx][aarx];

      for (uint32_t i = 0; i < buffer_length >> 2; i++) {
        const simde__m128i chF_cpe128 = oai_mm_cpx_mult(chF_128[i], cpe128, 15);
        simde__m128i comp = oai_mm_cpx_mult_conj(chF_cpe128, rxF_128[i], output_shift);
        rxComp_128[i] = simde_mm_add_epi16(rxComp_128[i], comp);

        if (mod_order > 2) {
          simde__m128i mag = oai_mm_smadd(chF_128[i], chF_128[i], output_shift);
          mag = simde_mm_packs_epi32(mag, mag);
          mag = simde_mm_unpacklo_epi16(mag, mag);
          ch_maga_128[i] = simde_mm_add_epi16(ch_maga_128[i], simde_mm_mulhrs_epi16(mag, QAM_ampa_128));

          if (mod_order > 4)
            ch_magb_128[i] = simde_mm_add_epi16(ch_magb_128[i], simde_mm_mulhrs_epi16(mag, QAM_ampb_128));

          if (mod_order > 6)
            ch_magc_128[i] = simde_mm_add_epi16(ch_magc_128[i], simde_mm_mulhrs_epi16(mag, QAM_ampc_128));
        }
      }

      if (rho) {
        for (int atx = 0; atx < nb_layers; atx++) {
          simde__m128i *rho_128 = (simde__m128i *)rho[aatx][atx];
          simde__m128i *chF2_128 = (simde__m128i *)chFext[atx][aarx];
          for (uint32_t i = 0; i < buffer_length >> 2; i++)
            rho_128[i] = simde_mm_adds_epi16(rho_128[i], oai_mm_cpx_mult_conj(chF_128[i], chF2_128[i], output_shift));
        }
      }
    }
  }
#endif
}
