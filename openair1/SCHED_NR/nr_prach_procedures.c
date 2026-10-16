/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Implementation of gNB prach procedures from 38.213 LTE specifications
 */

#include "PHY/defs_gNB.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "nfapi_nr_interface_scf.h"
#include "nfapi_pnf.h"
#include "common/utils/LOG/log.h"
#include "assertions.h"
#include <time.h>

int get_nr_prach_duration(uint8_t prach_format)
{
  const int val[14] = {0, 0, 0, 0, 2, 4, 6, 2, 12, 2, 6, 2, 4, 6};
  AssertFatal(prach_format < sizeofArray(val), "Invalid Prach format %d\n", prach_format);
  return val[prach_format];
}

void L1_nr_prach_procedures(PHY_VARS_gNB *gNB, prach_item_t *prach_id, nfapi_nr_rach_indication_t *rach_ind)
{
  const frame_t frame = prach_id->frame;
  const slot_t slot = prach_id->slot;
  rach_ind->sfn = frame;
  rach_ind->slot = slot;
  nfapi_nr_prach_pdu_t *prach_pdu = &prach_id->pdu;
  LOG_D(NR_PHY_RACH, "%d.%d, prachstart slot %d prach entry occas %d\n", frame, slot, prach_id->slot, prach_pdu->num_prach_ocas);
  int N_dur = get_nr_prach_duration(prach_pdu->prach_format);

  for (int prach_oc = 0; prach_oc < prach_pdu->num_prach_ocas; prach_oc++) {
    uint prachStartSymbol = prach_pdu->prach_start_symbol + prach_oc * N_dur;
    // comment FK: the standard 38.211 section 5.3.2 has one extra term +14*N_RA_slot. This is because there prachStartSymbol is
    // given wrt to start of the 15kHz slot or 60kHz slot. Here we work slot based, so this function is anyway only called in slots
    // where there is PRACH. Its up to the MAC to schedule another PRACH PDU in the case there are there N_RA_slot \in {0,1}.
    rx_prach_out_t res = rx_nr_prach(prach_id, prach_oc);
    const bool prach_noise_ready = gNB->prach_energy_counter == NUM_PRACH_RX_FOR_NOISE_ESTIMATE;
    const bool prach_above_threshold = res.max_preamble_energy > gNB->measurements.prach_I0 + gNB->prach_thres;
    const bool prach_ind_has_space = rach_ind->number_of_pdus < MAX_NUM_NR_RX_RACH_PDUS;
    const bool prach_accepted = prach_noise_ready && prach_above_threshold && prach_ind_has_space;
    LOG_D(NR_PHY,
          "[RAPROC] %d.%d occasion %d symbol %u format %u sequence-length %d N_ZC %d PRACH-SCS %d UL-mu %d NCS %u "
          "RAPID %u energy %.1f dB I0 %.1f dB threshold %.1f dB raw-delay %u TA %u "
          "noise %d/%d noise-ready %d threshold-pass %d indication-space %d accepted %d\n",
          frame,
          slot,
          prach_oc,
          prachStartSymbol,
          prach_pdu->prach_format,
          prach_id->prach_sequence_length,
          prach_id->prach_sequence_length == 0 ? 839 : 139,
          prach_id->mu,
          prach_id->numerology_index,
          prach_pdu->num_cs,
          res.max_preamble,
          res.max_preamble_energy / 10.0,
          gNB->measurements.prach_I0 / 10.0,
          gNB->prach_thres / 10.0,
          res.max_preamble_delay_raw,
          res.max_preamble_delay,
          gNB->prach_energy_counter,
          NUM_PRACH_RX_FOR_NOISE_ESTIMATE,
          prach_noise_ready,
          prach_above_threshold,
          prach_ind_has_space,
          prach_accepted);

    if (prach_accepted) {
      LOG_A(NR_PHY,
            "[RAPROC] %d.%d Initiating RA procedure with preamble %d, energy %.1f dB (I0 %.1f dB, thres %.1f dB), delay %d "
            "start symbol %u freq index %u\n",
            frame,
            slot,
            res.max_preamble,
            res.max_preamble_energy / 10.0,
            gNB->measurements.prach_I0 / 10.0,
            gNB->prach_thres / 10.0,
            res.max_preamble_delay,
            prachStartSymbol,
            prach_pdu->num_ra);

      T(T_ENB_PHY_INITIATE_RA_PROCEDURE,
        T_INT(gNB->Mod_id),
        T_INT(frame),
        T_INT(slot),
        T_INT(res.max_preamble),
        T_INT(res.max_preamble_energy),
        T_INT(res.max_preamble_delay));

      nfapi_nr_prach_indication_pdu_t *ind = rach_ind->pdu_list + rach_ind->number_of_pdus;
      *ind = (nfapi_nr_prach_indication_pdu_t){
          .phy_cell_id = gNB->gNB_config.cell_config.phy_cell_id.value,
          .symbol_index = prachStartSymbol,
          .slot_index = slot,
          .freq_index = prach_pdu->num_ra,
          .avg_rssi = (res.max_preamble_energy < 631) ? (128 + (res.max_preamble_energy / 5)) : 254,
          .avg_snr = 0xff, // invalid for now
          .num_preamble = 1,
          .preamble_list = {
              {.preamble_index = res.max_preamble, .timing_advance = res.max_preamble_delay, .preamble_pwr = 0xffffffff}}};
      rach_ind->number_of_pdus++;
    }
    gNB->measurements.prach_I0 = ((gNB->measurements.prach_I0 * 900) >> 10) + ((res.max_preamble_energy * 124) >> 10);
    if (frame == 0)
      LOG_I(PHY, "prach_I0 = %.1f dB\n", gNB->measurements.prach_I0 / 10.0);
    if (gNB->prach_energy_counter < NUM_PRACH_RX_FOR_NOISE_ESTIMATE)
      gNB->prach_energy_counter++;
  } // if prach_id>0
  LOG_D(NR_PHY_RACH, "Freeing PRACH entry\n");
  free_nr_prach_entry(prach_id);
}
