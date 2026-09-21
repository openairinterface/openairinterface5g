/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file PHY/NR_TRANSPORT/srs_rx.c
 * \brief Top-level routines for getting the SRS physical channel
 * \date 2021
 * \version 1.0
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "PHY/INIT/nr_phy_init.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_gNB.h"
#include "PHY/defs_RU.h"
#include "PHY/CODING/nrSmallBlock/nr_small_block_defs.h"
#include "common/utils/LOG/log.h"
#include "SCHED_NR/sched_nr.h"
#include "nfapi/oai_integration/vendor_ext.h"

#include "T.h"

//#define SRS_DEBUG

void nr_fill_srs(PHY_VARS_gNB *gNB, frame_t frame, slot_t slot, nfapi_nr_srs_pdu_t *srs_pdu)
{
  NR_gNB_SRS_job_t srs = {.frame = frame, .slot = slot, .srs_pdu = *srs_pdu};
  if (gNB->common_vars.beam_id) {
    const uint8_t l0 = srs_pdu->time_start_position; // L2 already sends the absolute symbol index
    int bitmap = SL_to_bitmap(l0, 1 << srs_pdu->num_symbols);
    int fapi_beam_idx = srs_pdu->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx;
    const nfapi_v4_srs_parameters_t *p = &srs_pdu->srs_parameters_v4;
    // We assume the ports are sequential so taking the first port index here
    const uint16_t ant_port_start = p->num_ul_spatial_streams_ports > 0 ? p->Ul_spatial_stream_ports[0] : 0;
    beam_index_allocation(fapi_beam_idx,
                          ant_port_start,
                          p->num_ul_spatial_streams_ports,
                          NR_SYMBOLS_PER_SLOT,
                          slot,
                          bitmap,
                          gNB->frame_parms.nb_antennas_rx,
                          gNB->common_vars.beam_id);
  }
  bool found = spsc_q_put(&gNB->srs_queue, &srs, sizeof(srs));
  if (!found)
    LOG_W(NR_PHY, "SRS list is full: dropping SRS UE %04x\n", srs_pdu->rnti);
}

int nr_get_srs_signal(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      slot_t slot,
                      const nfapi_nr_srs_pdu_t *srs_pdu,
                      nr_srs_info_t *nr_srs_info,
                      c16_t srs_received_signal[][gNB->frame_parms.ofdm_symbol_size * (1 << srs_pdu->num_symbols)])
{
  const NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;

  const uint16_t n_symbols = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot; // number of symbols until this slot
  const uint8_t l0 = srs_pdu->time_start_position; // starting symbol in this slot (L2 sends absolute symbol index)
  const uint64_t symbol_offset = (n_symbols + l0) * frame_parms->ofdm_symbol_size;
  const uint64_t subcarrier_offset = srs_pdu->bwp_start * NR_NB_SC_PER_RB;

  const uint8_t N_ap = 1 << srs_pdu->num_ant_ports;
  const uint8_t N_symb_SRS = 1 << srs_pdu->num_symbols;
  const uint8_t K_TC = 2 << srs_pdu->comb_size;
  const uint16_t M_sc_b_SRS = get_m_srs(srs_pdu->config_index, srs_pdu->bandwidth_index) * NR_NB_SC_PER_RB / K_TC;
  const uint8_t num_sp_streams = srs_pdu->srs_parameters_v4.num_ul_spatial_streams_ports;

  bool no_srs_signal = true;
  for (int ant = 0; ant < num_sp_streams; ant++) {
    memset(srs_received_signal[ant], 0, frame_parms->ofdm_symbol_size * N_symb_SRS * sizeof(c16_t));
    c16_t *rx_signal = &rxdataF[ant][symbol_offset];

    for (int l_line = 0; l_line < N_symb_SRS; l_line++) {
      const uint16_t l_line_offset = l_line * frame_parms->ofdm_symbol_size;

      for (int p_index = 0; p_index < N_ap; p_index++) {
#ifdef SRS_DEBUG
        LOG_I(NR_PHY, "===== UE port %d --> gNB Rx antenna %i =====\n", p_index, ant);
        LOG_I(NR_PHY, ":::::::: OFDM symbol %d ::::::::\n", l0 + l_line);
#endif

        uint32_t subcarrier = subcarrier_offset + nr_srs_info->k_0_p[p_index][l_line];
        int32_t signal_bits = 0;

        for (int k = 0; k < M_sc_b_SRS; k++) {
          const c16_t rx = rx_signal[l_line_offset + subcarrier];
          srs_received_signal[ant][l_line_offset + subcarrier] = rx;
          signal_bits |= rx.r | rx.i;

#ifdef SRS_DEBUG
          int subcarrier_log = subcarrier - subcarrier_offset;
          if (subcarrier_log % 12 == 0) {
            LOG_I(NR_PHY, "------------ %d ------------\n", subcarrier_log / 12);
          }
          LOG_I(NR_PHY, "(%i)  \t%i\t%i\n", subcarrier_log, rx.r, rx.i);
#endif

          // Subcarrier increment
          subcarrier += K_TC;
        } // for (int k = 0; k < M_sc_b_SRS; k++)

        if (signal_bits)
          no_srs_signal = false;
      } // for (int p_index = 0; p_index < N_ap; p_index++)

    } // for (int l_line = 0; l_line < N_symb_SRS; l_line++)
  } // for (int ant = 0; ant < num_sp_streams; ant++)

  if (no_srs_signal) {
    LOG_W(NR_PHY, "No SRS signal\n");
    return -1;
  } else {
    return 0;
  }
}
