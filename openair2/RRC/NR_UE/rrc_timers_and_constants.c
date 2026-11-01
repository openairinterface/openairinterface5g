/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "openair2/RRC/NR_UE/rrc_proto.h"
#include "executables/softmodem-common.h"

void init_SI_timers(NR_UE_RRC_SI_INFO *SInfo)
{
  // delete any stored version of a SIB after 3 hours
  // from the moment it was successfully confirmed as valid
  nr_timer_setup(&SInfo->sib1_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib2_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib3_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib4_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib5_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib6_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib7_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib8_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib9_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib10_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib11_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib12_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib13_timer, 10800000, 10);
  nr_timer_setup(&SInfo->sib14_timer, 10800000, 10);
  nr_timer_setup(&SInfo->SInfo_r17.sib19_timer, 10800000, 10);
}

void nr_rrc_SI_timers(NR_UE_RRC_SI_INFO *SInfo)
{
  if (SInfo->sib1_validity) {
   bool sib1_expired = nr_timer_tick(&SInfo->sib1_timer);
   if (sib1_expired)
     SInfo->sib1_validity = false;
  }
  if (SInfo->sib2_validity) {
   bool sib2_expired = nr_timer_tick(&SInfo->sib2_timer);
   if (sib2_expired)
     SInfo->sib2_validity = false;
  }
  if (SInfo->sib3_validity) {
   bool sib3_expired = nr_timer_tick(&SInfo->sib3_timer);
   if (sib3_expired)
     SInfo->sib3_validity = false;
  }
  if (SInfo->sib4_validity) {
   bool sib4_expired = nr_timer_tick(&SInfo->sib4_timer);
   if (sib4_expired)
     SInfo->sib4_validity = false;
  }
  if (SInfo->sib5_validity) {
   bool sib5_expired = nr_timer_tick(&SInfo->sib5_timer);
   if (sib5_expired)
     SInfo->sib5_validity = false;
  }
  if (SInfo->sib6_validity) {
   bool sib6_expired = nr_timer_tick(&SInfo->sib6_timer);
   if (sib6_expired)
     SInfo->sib6_validity = false;
  }
  if (SInfo->sib7_validity) {
   bool sib7_expired = nr_timer_tick(&SInfo->sib7_timer);
   if (sib7_expired)
     SInfo->sib7_validity = false;
  }
  if (SInfo->sib8_validity) {
   bool sib8_expired = nr_timer_tick(&SInfo->sib8_timer);
   if (sib8_expired)
     SInfo->sib8_validity = false;
  }
  if (SInfo->sib9_validity) {
   bool sib9_expired = nr_timer_tick(&SInfo->sib9_timer);
   if (sib9_expired)
     SInfo->sib9_validity = false;
  }
  if (SInfo->sib10_validity) {
   bool sib10_expired = nr_timer_tick(&SInfo->sib10_timer);
   if (sib10_expired)
     SInfo->sib10_validity = false;
  }
  if (SInfo->sib11_validity) {
   bool sib11_expired = nr_timer_tick(&SInfo->sib11_timer);
   if (sib11_expired)
     SInfo->sib11_validity = false;
  }
  if (SInfo->sib12_validity) {
   bool sib12_expired = nr_timer_tick(&SInfo->sib12_timer);
   if (sib12_expired)
     SInfo->sib12_validity = false;
  }
  if (SInfo->sib13_validity) {
   bool sib13_expired = nr_timer_tick(&SInfo->sib13_timer);
   if (sib13_expired)
     SInfo->sib13_validity = false;
  }
  if (SInfo->sib14_validity) {
   bool sib14_expired = nr_timer_tick(&SInfo->sib14_timer);
   if (sib14_expired)
     SInfo->sib14_validity = false;
  }
  if (SInfo->SInfo_r17.sib19_validity) {
   bool sib19_expired = nr_timer_tick(&SInfo->SInfo_r17.sib19_timer);
   if (sib19_expired)
     SInfo->SInfo_r17.sib19_validity = false;
  }
}

void handle_meas_timers(NR_UE_RRC_INST_t *rrc)
{
  for (int i = 0; i < NB_CNX_UE; i++) {
    rrcPerNB_t *nb = &rrc->perNB[i];
    l3_measurements_t *l3_measurements = &nb->l3_measurements;

    for (int meas_id = 0; meas_id < NR_MAX_MEAS_ID; meas_id++) {
      meas_report_params_t *params = &l3_measurements->meas_report[meas_id];

      // Check if this measId is configured
      if (nb->MeasId[meas_id] == NULL)
        continue;

      // Handle Event A2 timer expiry
      bool ta2_expired = nr_timer_tick(&params->TA2);
      if (ta2_expired && params->trigger_quantity > 0) {
        rrc_ue_generate_measurementReport(nb, rrc->ue_id, meas_id);
        params->reports_sent = 1;

        if (params->reports_sent < params->max_reports && !nr_timer_is_active(&params->periodic_report_timer)) {
          nr_timer_setup(&params->periodic_report_timer, params->report_interval_ms, 10);
          nr_timer_start(&params->periodic_report_timer);
        }
      }

      // Handle Event A3 timer expiry
      bool ta3_expired = nr_timer_tick(&params->TA3);
      if (ta3_expired && params->trigger_quantity > 0) {
        rrc_ue_generate_measurementReport(nb, rrc->ue_id, meas_id);
        params->reports_sent = 1;

        if (params->reports_sent < params->max_reports && !nr_timer_is_active(&params->periodic_report_timer)) {
          nr_timer_setup(&params->periodic_report_timer, params->report_interval_ms, 10);
          nr_timer_start(&params->periodic_report_timer);
        }
      }

      // Handle periodical report
      // Per TS 38.331 Section 5.5.4.1, initiate measurement reporting immediately
      // after the quantity to be reported becomes available for the NR SpCell
      bool meas_available = (params->rs_type == NR_NR_RS_Type_ssb) ? l3_measurements->serving_cell.ss_rsrp_dBm.init
                                                                   : l3_measurements->serving_cell.csi_rsrp_dBm.init;
      bool initial_condition = meas_available && params->trigger_quantity == 0 && params->reports_sent == 0
                               && nr_timer_is_active(&params->periodic_report_timer);
      bool periodic_condition =
          nr_timer_tick(&params->periodic_report_timer) && params->reports_sent > 0 && params->reports_sent < params->max_reports;
      if (initial_condition || periodic_condition) {
        params->reports_sent++;
        rrc_ue_generate_measurementReport(nb, rrc->ue_id, meas_id);
        if (params->reports_sent < params->max_reports) {
          nr_timer_setup(&params->periodic_report_timer, params->report_interval_ms, 10);
          nr_timer_start(&params->periodic_report_timer);
        }
      }
    }
  }
}

void nr_rrc_handle_timers(NR_UE_RRC_INST_t *rrc)
{
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;

  bool release_timer_expired = nr_timer_tick(&rrc->release_timer);
  if (release_timer_expired)
    handle_RRCRelease(rrc);

  bool t300_expired = nr_timer_tick(&timers->T300);
  if(t300_expired) {
    RRCLOG_W("Timer T300 expired! No timely response to RRCSetupRequest\n");
    handle_t300_expiry(rrc);
  }

  bool t301_expired = nr_timer_tick(&timers->T301);
  // Upon T301 expiry, the UE shall perform the actions upon going to RRC_IDLE
  // with release cause 'RRC connection failure'
  if(t301_expired) {
    RRCLOG_W("Timer T301 expired! No timely response to RRCReestabilshmentRequest\n");
    nr_rrc_going_to_IDLE(rrc, RRC_CONNECTION_FAILURE, NULL);
  }

  bool t302_expired = nr_timer_tick(&timers->T302);
  // 5.3.14.4 in 38.331
  // consider the barring for this Access Category to be alleviated
  if (t302_expired) {
    RRCLOG_W("Timer T302 expired! Access barring alleviated!\n");
    handle_302_expired_stopped(rrc);
  }

  bool t304_expired = nr_timer_tick(&timers->T304);
  if(t304_expired) {
    RRCLOG_W("Timer T304 expired\n");
    // For T304 of MCG, in case of the handover from NR or intra-NR
    // handover, initiate the RRC re-establishment procedure;
    handle_t304_expiry(rrc);
    // In case of handover to NR, perform the actions defined in the
    // specifications applicable for the source RAT.
  }

  bool t310_expired = nr_timer_tick(&timers->T310);
  if(t310_expired) {
    RRCLOG_W("Timer T310 expired\n");
    // handle detection of radio link failure
    // as described in 5.3.10.3 of 38.331
    handle_rlf_detection(rrc);
  }

  bool t311_expired = nr_timer_tick(&timers->T311);
  if (t311_expired) {
    RRCLOG_W("Timer T311 expired! No suitable cell found in time after initiation of re-establishment\n");
    // Upon T311 expiry, the UE shall perform the actions upon going to RRC_IDLE
    // with release cause 'RRC connection failure'
    nr_rrc_going_to_IDLE(rrc, RRC_CONNECTION_FAILURE, NULL);
  }

  bool t430_expired = nr_timer_tick(&rrc->timers_and_constants.T430);
  if (t430_expired && rrc->nrRrcState == RRC_STATE_CONNECTED_NR && rrc->is_NTN_UE) {
    RRCLOG_W("Timer T430 expired! Indicate UL SYNC LOSS to MAC\n");
    // Upon T430 expiry, the UE shall reacquire SIB19 and re-obtain UL-SYNC
    // Spec 38.331 Section 5.2.2.6
    handle_t430_expiry(rrc);
  }

  handle_meas_timers(rrc);
}

int nr_rrc_get_T304(long t304)
{
  int target = 0;
  switch (t304) {
    case NR_ReconfigurationWithSync__t304_ms50 :
      target = 50;
      break;
    case NR_ReconfigurationWithSync__t304_ms100 :
      target = 100;
      break;
    case NR_ReconfigurationWithSync__t304_ms150 :
      target = 150;
      break;
    case NR_ReconfigurationWithSync__t304_ms200 :
      target = 200;
      break;
    case NR_ReconfigurationWithSync__t304_ms500 :
      target = 500;
      break;
    case NR_ReconfigurationWithSync__t304_ms1000 :
      target = 1000;
      break;
    case NR_ReconfigurationWithSync__t304_ms2000 :
      target = 2000;
      break;
    case NR_ReconfigurationWithSync__t304_ms10000 :
      target = 10000;
      break;
    default :
      AssertFatal(false, "Invalid T304 %ld\n", t304);
  }
  return target;
}

void set_rlf_sib1_timers_and_constants(NR_UE_Timers_Constants_t *tac, NR_UE_TimersAndConstants_t *ue_TimersAndConstants)
{
  if(ue_TimersAndConstants) {
    int k = 0;
    switch (ue_TimersAndConstants->t301) {
      case NR_UE_TimersAndConstants__t301_ms100 :
        k = 100;
        break;
      case NR_UE_TimersAndConstants__t301_ms200 :
        k = 200;
        break;
      case NR_UE_TimersAndConstants__t301_ms300 :
        k = 300;
        break;
      case NR_UE_TimersAndConstants__t301_ms400 :
        k = 400;
        break;
      case NR_UE_TimersAndConstants__t301_ms600 :
        k = 600;
        break;
      case NR_UE_TimersAndConstants__t301_ms1000 :
        k = 1000;
        break;
      case NR_UE_TimersAndConstants__t301_ms1500 :
        k = 1500;
        break;
      case NR_UE_TimersAndConstants__t301_ms2000 :
        k = 2000;
        break;
      default :
        AssertFatal(false, "Invalid T301 %ld\n", ue_TimersAndConstants->t301);
    }
    nr_timer_setup(&tac->T301, k, 10); // 10ms step
    switch (ue_TimersAndConstants->t310) {
      case NR_UE_TimersAndConstants__t310_ms0 :
        k = 0;
        break;
      case NR_UE_TimersAndConstants__t310_ms50 :
        k = 50;
        break;
      case NR_UE_TimersAndConstants__t310_ms100 :
        k = 100;
        break;
      case NR_UE_TimersAndConstants__t310_ms200 :
        k = 200;
        break;
      case NR_UE_TimersAndConstants__t310_ms500 :
        k = 500;
        break;
      case NR_UE_TimersAndConstants__t310_ms1000 :
        k = 1000;
        break;
      case NR_UE_TimersAndConstants__t310_ms2000 :
        k = 2000;
        break;
      default :
        AssertFatal(false, "Invalid T310 %ld\n", ue_TimersAndConstants->t310);
    }
    nr_timer_setup(&tac->T310, k, 10); // 10ms step
    switch (ue_TimersAndConstants->t311) {
      case NR_UE_TimersAndConstants__t311_ms1000 :
        k = 1000;
        break;
      case NR_UE_TimersAndConstants__t311_ms3000 :
        k = 3000;
        break;
      case NR_UE_TimersAndConstants__t311_ms5000 :
        k = 5000;
        break;
      case NR_UE_TimersAndConstants__t311_ms10000 :
        k = 10000;
        break;
      case NR_UE_TimersAndConstants__t311_ms15000 :
        k = 15000;
        break;
      case NR_UE_TimersAndConstants__t311_ms20000 :
        k = 20000;
        break;
      case NR_UE_TimersAndConstants__t311_ms30000 :
        k = 30000;
        break;
      default :
        AssertFatal(false, "Invalid T311 %ld\n", ue_TimersAndConstants->t311);
    }
    nr_timer_setup(&tac->T311, k, 10); // 10ms step
    switch (ue_TimersAndConstants->n310) {
      case NR_UE_TimersAndConstants__n310_n1 :
        tac->N310_k = 1;
        break;
      case NR_UE_TimersAndConstants__n310_n2 :
        tac->N310_k = 2;
        break;
      case NR_UE_TimersAndConstants__n310_n3 :
        tac->N310_k = 3;
        break;
      case NR_UE_TimersAndConstants__n310_n4 :
        tac->N310_k = 4;
        break;
      case NR_UE_TimersAndConstants__n310_n6 :
        tac->N310_k = 6;
        break;
      case NR_UE_TimersAndConstants__n310_n8 :
        tac->N310_k = 8;
        break;
      case NR_UE_TimersAndConstants__n310_n10 :
        tac->N310_k = 10;
        break;
      case NR_UE_TimersAndConstants__n310_n20 :
        tac->N310_k = 20;
        break;
      default :
        AssertFatal(false, "Invalid N310 %ld\n", ue_TimersAndConstants->n310);
    }
    switch (ue_TimersAndConstants->n311) {
      case NR_UE_TimersAndConstants__n311_n1 :
        tac->N311_k = 1;
        break;
      case NR_UE_TimersAndConstants__n311_n2 :
        tac->N311_k = 2;
        break;
      case NR_UE_TimersAndConstants__n311_n3 :
        tac->N311_k = 3;
        break;
      case NR_UE_TimersAndConstants__n311_n4 :
        tac->N311_k = 4;
        break;
      case NR_UE_TimersAndConstants__n311_n5 :
        tac->N311_k = 5;
        break;
      case NR_UE_TimersAndConstants__n311_n6 :
        tac->N311_k = 6;
        break;
      case NR_UE_TimersAndConstants__n311_n8 :
        tac->N311_k = 8;
        break;
      case NR_UE_TimersAndConstants__n311_n10 :
        tac->N311_k = 10;
        break;
      default :
        AssertFatal(false, "Invalid N311 %ld\n", ue_TimersAndConstants->n311);
    }
  }
  else
    LOG_E(NR_RRC,"UE_Timers_Constants should not be NULL\n");
}

void nr_rrc_set_sib1_timers_and_constants(NR_UE_Timers_Constants_t *tac, NR_SIB1_t *sib1)
{
  set_rlf_sib1_timers_and_constants(tac, sib1->ue_TimersAndConstants);
  if(sib1 && sib1->ue_TimersAndConstants) {
    int k = 0;
    switch (sib1->ue_TimersAndConstants->t300) {
      case NR_UE_TimersAndConstants__t300_ms100 :
        k = 100;
        break;
      case NR_UE_TimersAndConstants__t300_ms200 :
        k = 200;
        break;
      case NR_UE_TimersAndConstants__t300_ms300 :
        k = 300;
        break;
      case NR_UE_TimersAndConstants__t300_ms400 :
        k = 400;
        break;
      case NR_UE_TimersAndConstants__t300_ms600 :
        k = 600;
        break;
      case NR_UE_TimersAndConstants__t300_ms1000 :
        k = 1000;
        break;
      case NR_UE_TimersAndConstants__t300_ms1500 :
        k = 1500;
        break;
      case NR_UE_TimersAndConstants__t300_ms2000 :
        k = 2000;
        break;
      default :
        AssertFatal(false, "Invalid T300 %ld\n", sib1->ue_TimersAndConstants->t300);
    }
    nr_timer_setup(&tac->T300, k, 10); // 10ms step
    switch (sib1->ue_TimersAndConstants->t319) {
      case NR_UE_TimersAndConstants__t319_ms100 :
        k = 100;
        break;
      case NR_UE_TimersAndConstants__t319_ms200 :
        k = 200;
        break;
      case NR_UE_TimersAndConstants__t319_ms300 :
        k = 300;
        break;
      case NR_UE_TimersAndConstants__t319_ms400 :
        k = 400;
        break;
      case NR_UE_TimersAndConstants__t319_ms600 :
        k = 600;
        break;
      case NR_UE_TimersAndConstants__t319_ms1000 :
        k = 1000;
        break;
      case NR_UE_TimersAndConstants__t319_ms1500 :
        k = 1500;
        break;
      case NR_UE_TimersAndConstants__t319_ms2000 :
        k = 2000;
        break;
      default :
        AssertFatal(false, "Invalid T319 %ld\n", sib1->ue_TimersAndConstants->t319);
    }
    nr_timer_setup(&tac->T319, k, 10); // 10ms step
  }
  else
    LOG_E(NR_RRC,"SIB1 should not be NULL and neither UE_Timers_Constants\n");
}

void nr_rrc_handle_SetupRelease_RLF_TimersAndConstants(NR_UE_RRC_INST_t *rrc,
                                                       struct NR_SetupRelease_RLF_TimersAndConstants *rlf_TimersAndConstants)
{
  if(rlf_TimersAndConstants == NULL)
    return;

  NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
  NR_RLF_TimersAndConstants_t *rlf_tac = NULL;
  switch(rlf_TimersAndConstants->present){
    case NR_SetupRelease_RLF_TimersAndConstants_PR_release :
      // use values for timers T301, T310, T311 and constants N310, N311, as included in ue-TimersAndConstants received in SIB1
      set_rlf_sib1_timers_and_constants(tac, rrc->timers_and_constants.sib1_TimersAndConstants);
      break;
    case NR_SetupRelease_RLF_TimersAndConstants_PR_setup :
      rlf_tac = rlf_TimersAndConstants->choice.setup;
      if (rlf_tac == NULL)
        return;
      // (re-)configure the value of timers and constants in accordance with received rlf-TimersAndConstants
      int k = 0;
      switch (rlf_tac->t310) {
        case NR_RLF_TimersAndConstants__t310_ms0 :
          k = 0;
          break;
        case NR_RLF_TimersAndConstants__t310_ms50 :
          k = 50;
          break;
        case NR_RLF_TimersAndConstants__t310_ms100 :
          k = 100;
          break;
        case NR_RLF_TimersAndConstants__t310_ms200 :
          k = 200;
          break;
        case NR_RLF_TimersAndConstants__t310_ms500 :
          k = 500;
          break;
        case NR_RLF_TimersAndConstants__t310_ms1000 :
          k = 1000;
          break;
        case NR_RLF_TimersAndConstants__t310_ms2000 :
          k = 2000;
          break;
        case NR_RLF_TimersAndConstants__t310_ms4000 :
          k = 4000;
          break;
        case NR_RLF_TimersAndConstants__t310_ms6000 :
          k = 6000;
          break;
        default :
          AssertFatal(false, "Invalid T310 %ld\n", rlf_tac->t310);
      }
      nr_timer_setup(&tac->T310, k, 10); // 10ms step
      switch (rlf_tac->n310) {
        case NR_RLF_TimersAndConstants__n310_n1 :
          tac->N310_k = 1;
          break;
        case NR_RLF_TimersAndConstants__n310_n2 :
          tac->N310_k = 2;
          break;
        case NR_RLF_TimersAndConstants__n310_n3 :
          tac->N310_k = 3;
          break;
        case NR_RLF_TimersAndConstants__n310_n4 :
          tac->N310_k = 4;
          break;
        case NR_RLF_TimersAndConstants__n310_n6 :
          tac->N310_k = 6;
          break;
        case NR_RLF_TimersAndConstants__n310_n8 :
          tac->N310_k = 8;
          break;
        case NR_RLF_TimersAndConstants__n310_n10 :
          tac->N310_k = 10;
          break;
        case NR_RLF_TimersAndConstants__n310_n20 :
          tac->N310_k = 20;
          break;
        default :
          AssertFatal(false, "Invalid N310 %ld\n", rlf_tac->n310);
      }
      switch (rlf_tac->n311) {
        case NR_RLF_TimersAndConstants__n311_n1 :
          tac->N311_k = 1;
          break;
        case NR_RLF_TimersAndConstants__n311_n2 :
          tac->N311_k = 2;
          break;
        case NR_RLF_TimersAndConstants__n311_n3 :
          tac->N311_k = 3;
          break;
        case NR_RLF_TimersAndConstants__n311_n4 :
          tac->N311_k = 4;
          break;
        case NR_RLF_TimersAndConstants__n311_n5 :
          tac->N311_k = 5;
          break;
        case NR_RLF_TimersAndConstants__n311_n6 :
          tac->N311_k = 6;
          break;
        case NR_RLF_TimersAndConstants__n311_n8 :
          tac->N311_k = 8;
          break;
        case NR_RLF_TimersAndConstants__n311_n10 :
          tac->N311_k = 10;
          break;
        default :
          AssertFatal(false, "Invalid N311 %ld\n", rlf_tac->n311);
      }
      if (rlf_tac->ext1) {
        switch (rlf_tac->ext1->t311) {
          case NR_RLF_TimersAndConstants__ext1__t311_ms1000 :
            k = 1000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms3000 :
            k = 3000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms5000 :
            k = 5000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms10000 :
            k = 10000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms15000 :
            k = 15000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms20000 :
            k = 20000;
            break;
          case NR_RLF_TimersAndConstants__ext1__t311_ms30000 :
            k = 30000;
            break;
          default :
            AssertFatal(false, "Invalid T311 %ld\n", rlf_tac->ext1->t311);
        }
        nr_timer_setup(&tac->T311, k, 10); // 10ms step
      }
      reset_rlf_timers_and_constants(tac);
      break;
    default :
      AssertFatal(false, "Invalid rlf_TimersAndConstants\n");
  }
}

void handle_rlf_sync(NR_UE_Timers_Constants_t *tac,
                     nr_sync_msg_t sync_msg)
{
  if (sync_msg == IN_SYNC) {
    tac->N310_cnt = 0;
    if (nr_timer_is_active(&tac->T310)) {
      tac->N311_cnt++;
      // Upon receiving N311 consecutive "in-sync" indications
      if (tac->N311_cnt >= tac->N311_k) {
        // stop timer T310
        nr_timer_stop(&tac->T310);
        tac->N311_cnt = 0;
      }
    }
  }
  else {
    // OUT_OF_SYNC
    tac->N311_cnt = 0;
    if(get_softmodem_params()->phy_test ||
       nr_timer_is_active(&tac->T300) ||
       nr_timer_is_active(&tac->T301) ||
       nr_timer_is_active(&tac->T304) ||
       nr_timer_is_active(&tac->T310) ||
       nr_timer_is_active(&tac->T311) ||
       nr_timer_is_active(&tac->T319))
      return;
    tac->N310_cnt++;
    // upon receiving N310 consecutive "out-of-sync" indications
    if (tac->N310_cnt >= tac->N310_k) {
      // start timer T310
      nr_timer_start(&tac->T310);
      tac->N310_cnt = 0;
    }
  }
}

void set_default_timers_and_constants(NR_UE_Timers_Constants_t *tac)
{
  // 38.331 9.2.3 Default values timers and constants
  nr_timer_setup(&tac->T310, 1000, 10); // 10ms step
  nr_timer_setup(&tac->T311, 30000, 10); // 10ms step
  tac->N310_k = 1;
  tac->N311_k = 1;
}

void reset_rlf_timers_and_constants(NR_UE_Timers_Constants_t *tac)
{
  // stop timer T310 for this cell group, if running
  nr_timer_stop(&tac->T310);
  // reset the counters N310 and N311
  tac->N310_cnt = 0;
  tac->N311_cnt = 0;
}

int get_event_time_to_trigger(long time_to_trigger)
{
  switch (time_to_trigger) {
    case NR_TimeToTrigger_ms0:
      return 0;
    case NR_TimeToTrigger_ms40:
      return 40;
    case NR_TimeToTrigger_ms64:
      return 64;
    case NR_TimeToTrigger_ms80:
      return 80;
    case NR_TimeToTrigger_ms100:
      return 100;
    case NR_TimeToTrigger_ms128:
      return 128;
    case NR_TimeToTrigger_ms160:
      return 160;
    case NR_TimeToTrigger_ms256:
      return 256;
    case NR_TimeToTrigger_ms320:
      return 320;
    case NR_TimeToTrigger_ms480:
      return 480;
    case NR_TimeToTrigger_ms512:
      return 512;
    case NR_TimeToTrigger_ms640:
      return 640;
    case NR_TimeToTrigger_ms1024:
      return 1024;
    case NR_TimeToTrigger_ms1280:
      return 1280;
    case NR_TimeToTrigger_ms2560:
      return 2560;
    case NR_TimeToTrigger_ms5120:
      return 5120;
    default:
      AssertFatal(false, "Invalid TimeToTrigger %ld\n", time_to_trigger);
  }
}
