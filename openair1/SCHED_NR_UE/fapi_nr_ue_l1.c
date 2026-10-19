/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief functions for NR UE FAPI-like interface
 */

#include <stdio.h>
#include "openair1/PHY/phy_extern_nr_ue.h"
#include "fapi_nr_ue_interface.h"
#include "fapi_nr_ue_l1.h"
#include "harq_nr.h"
#include "openair2/NR_UE_PHY_INTERFACE/NR_IF_Module.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/impl_defs_nr.h"
#include "utils.h"
#include "SCHED_NR_UE/phy_sch_processing_time.h"

const char *const dl_pdu_type[] = {"DCI", "DLSCH", "RA_DLSCH", "SI_DLSCH", "P_DLSCH", "CSI_RS", "CSI_IM", "TA"};
const char *const ul_pdu_type[] = {"PRACH", "PUCCH", "PUSCH", "SRS"};

static void configure_dlsch(NR_UE_DLSCH_t *dlsch,
                            NR_DL_UE_HARQ_t *harq_list,
                            fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu,
                            NR_UE_MAC_INST_t *mac,
                            int cw_idx,
                            int rnti,
                            const fapi_nr_dl_config_request_t *dl_config)
{
  const uint8_t current_harq_pid = dlsch_config_pdu->harq_process_nbr;
  dlsch->active = true;
  dlsch->rnti = rnti;

  LOG_D(PHY,"Configure DLSCH for pid = %d\n", current_harq_pid);

  NR_DL_UE_HARQ_t *dlsch_harq = &harq_list[current_harq_pid];
  dlsch->cw_info = dlsch_config_pdu->cw_info[cw_idx];

  if (dlsch->cw_info.Nl == 0) {
    LOG_E(PHY, "Invalid number of layers %d for DLSCH\n", dlsch->cw_info.Nl);
    // setting NACK for this TB
    dlsch->active = false;
    update_harq_status(mac, current_harq_pid, cw_idx, 0);
    return;
  }

  /* Retransmission of a TB this HARQ process already decoded (the gNB missed our ACK): its soft
   * buffer is no longer valid for combining, so decoding again would fail and NACK it every round.
   * TS 38.321 5.3.2.2: for a TB previously successfully decoded, generate an ACK.
   * status == NR_SCH_IDLE is enough to know the TB was decoded: downlink_harq_process() sets every
   * scheduled PID to NR_ACTIVE, a failed decode leaves it NR_ACTIVE, and only a successful decode
   * (nr_dlsch_decoding()) sets it back to NR_SCH_IDLE. The other NR_SCH_IDLE source,
   * init_downlink_harq_status() on a PHY resync, cannot reach this check: every resync requested
   * by the MAC goes through reset_mac_inst(), which sets last_ndi = -1, so the next DCI on each
   * PID is new data. */
  if (!dlsch->cw_info.new_data_indicator && dlsch->rnti_type == TYPE_C_RNTI_ && dlsch_harq->status == NR_SCH_IDLE) {
    dlsch->active = false;
    LOG_D(PHY, "DLSCH harq pid %d: retransmission of an already decoded TB, ACK again\n", current_harq_pid);
    update_harq_status(mac, current_harq_pid, cw_idx, 1);
    return;
  }

  /* Do not clear first_rx on a retransmission: decode never ran still needs to re-segment. */
  if (dlsch->cw_info.new_data_indicator) {
    dlsch_harq->first_rx = true;
    dlsch_harq->DLround = 0;
  } else {
    dlsch_harq->DLround++;
  }
  downlink_harq_process(dlsch_harq, current_harq_pid, dlsch->cw_info.new_data_indicator, dlsch->cw_info.rv, dlsch->rnti_type);
  dlsch_harq->activated_frame = dl_config->sfn;
  dlsch_harq->activated_slot = dl_config->slot;
}

static void configure_ta_command(PHY_VARS_NR_UE *ue, fapi_nr_ta_command_pdu *ta_command_pdu)
{
  /* Time Alignment procedure
  // - UE processing capability 1
  // - Setting the TA update to be applied after the reception of the TA command
  // - Timing adjustment computed according to TS 38.213 section 4.2
  // - Durations of N1 and N2 symbols corresponding to PDSCH and PUSCH are
  //   computed according to sections 5.3 and 6.4 of TS 38.214 */
  const int numerology = ue->frame_parms.numerology_index;
  const int ofdm_symbol_size = ue->frame_parms.ofdm_symbol_size;
  const int nb_prefix_samples = ue->frame_parms.nb_prefix_samples;
  const int samples_per_subframe = ue->frame_parms.samples_per_subframe;
  const int slots_per_frame = ue->frame_parms.slots_per_frame;
  const int slots_per_subframe = ue->frame_parms.slots_per_subframe;

  const double tc_factor = 1.0 / samples_per_subframe;
  // convert time factor "16 * 64 * T_c / (2^mu)" in N_TA calculation in TS38.213 section 4.2 to samples by multiplying with samples per second
  //   16 * 64 * T_c            / (2^mu) * samples_per_second
  // = 16 * T_s                 / (2^mu) * samples_per_second
  // = 16 * 1 / (15 kHz * 2048) / (2^mu) * (15 kHz * 2^mu * ofdm_symbol_size)
  // = 16 * 1 /           2048           *                  ofdm_symbol_size
  // = 16 * ofdm_symbol_size / 2048
  uint16_t bw_scaling = 16 * ofdm_symbol_size / 2048;

  const int Ta_max = 3846; // Max value of 12 bits TA Command
  const double N_TA_max = Ta_max * bw_scaling * tc_factor;

  // symbols corresponding to a PDSCH processing time for UE processing capability 1
  // when additional PDSCH DM-RS is configured
  int N_1 = pdsch_N_1_capability_1[numerology][3];

  /* PUSCH preapration time N_2 for processing capability 1 */
  const int N_2 = pusch_N_2_timing_capability_1[numerology][1];

  /* N_t_1 time duration in msec of N_1 symbols corresponding to a PDSCH reception time
  // N_t_2 time duration in msec of N_2 symbols corresponding to a PUSCH preparation time */
  double N_t_1 = N_1 * (ofdm_symbol_size + nb_prefix_samples) * tc_factor;
  double N_t_2 = N_2 * (ofdm_symbol_size + nb_prefix_samples) * tc_factor;

  /* Time alignment procedure */
  // N_t_1 + N_t_2 + N_TA_max must be in msec
  const double t_subframe = 1.0; // subframe duration of 1 msec
  const int ul_tx_timing_adjustment = 1 + (int)ceil(slots_per_subframe * (N_t_1 + N_t_2 + N_TA_max + 0.5) / t_subframe);

  if (ta_command_pdu->is_rar) {
    ue->ta_slot = ta_command_pdu->ta_slot;
    ue->ta_frame = ta_command_pdu->ta_frame;
    ue->ta_command = ta_command_pdu->ta_command;
  } else {
    const int target_slot = ta_command_pdu->ta_slot + ul_tx_timing_adjustment;
    ue->ta_slot = target_slot % slots_per_frame;
    ue->ta_frame = (ta_command_pdu->ta_frame + target_slot / slots_per_frame) % 1024;
    ue->ta_command = ta_command_pdu->ta_command;
  }
  ue->ta_command_is_rar = ta_command_pdu->is_rar;

  LOG_D(PHY,
        "[UE %d] TA command received in %d.%d: %s command %d, apply in %d.%d (application-delay %d slots)\n",
        ue->Mod_id,
        ta_command_pdu->ta_frame,
        ta_command_pdu->ta_slot,
        ta_command_pdu->is_rar ? "RAR" : "relative MAC CE",
        ue->ta_command,
        ue->ta_frame,
        ue->ta_slot,
        ta_command_pdu->is_rar ? 0 : ul_tx_timing_adjustment);
}

static void nr_ue_scheduled_response_dl(NR_UE_MAC_INST_t *mac,
                                        PHY_VARS_NR_UE *phy,
                                        fapi_nr_dl_config_request_t *dl_config,
                                        nr_phy_data_t *phy_data)
{
  AssertFatal(dl_config->number_pdus < FAPI_NR_DL_CONFIG_LIST_NUM,
              "dl_config->number_pdus %d out of bounds\n",
              dl_config->number_pdus);

  for (int i = 0; i < dl_config->number_pdus; ++i) {
    fapi_nr_dl_config_request_pdu_t *pdu = dl_config->dl_config_list + i;
    AssertFatal(pdu->pdu_type <= FAPI_NR_DL_CONFIG_TYPES, "pdu_type %d\n", pdu->pdu_type);
    LOG_D(PHY, "Copying DL %s PDU of %d total DL PDUs:\n", dl_pdu_type[pdu->pdu_type - 1], dl_config->number_pdus);

    switch (pdu->pdu_type) {
      case FAPI_NR_DL_CONFIG_TYPE_DCI:
        AssertFatal(phy_data->phy_pdcch_config.nb_search_space < FAPI_NR_MAX_SS, "Fix array size not large enough\n");
        const int nextFree = phy_data->phy_pdcch_config.nb_search_space;
        phy_data->phy_pdcch_config.pdcch_config[nextFree] = pdu->dci_config_pdu.dci_config_rel15;
        phy_data->phy_pdcch_config.nb_search_space++;
        LOG_D(PHY, "Number of DCI SearchSpaces %d\n", phy_data->phy_pdcch_config.nb_search_space);
        break;
      case FAPI_NR_DL_CONFIG_TYPE_CSI_IM:
        phy_data->csiim_vars.csiim_config_pdu = pdu->csiim_config_pdu.csiim_config_rel15;
        phy_data->csiim_vars.active = true;
        break;
      case FAPI_NR_DL_CONFIG_TYPE_CSI_RS:
        AssertFatal(phy_data->num_csirs < MAX_CSI_RES_SLOT, "CSI resources per slot exceeded limit\n");
        const int c = phy_data->num_csirs;
        if (phy_data->csirs_vars[c].active) {
          AssertFatal(false, "Resource should not be active before its configured\n");
        }
        phy_data->csirs_vars[c].csirs_config_pdu = pdu->csirs_config_pdu.csirs_config_rel15;
        phy_data->csirs_vars[c].active = true;
        phy_data->num_csirs++;
        break;
      case FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH:
      case FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH:
      case FAPI_NR_DL_CONFIG_TYPE_DLSCH:
      case FAPI_NR_DL_CONFIG_TYPE_P_DLSCH: {
        fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu = &pdu->dlsch_config_pdu.dlsch_config_rel15;
        phy_data->n_dlsch_codewords = dlsch_config_pdu->n_codewords;
        phy_data->dlsch_config = *dlsch_config_pdu;

        nr_rnti_type_t rnti_type;
        int n_codewords = 1;
        switch (pdu->pdu_type) {
          case FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH:
            rnti_type = TYPE_RA_RNTI_;
            break;
          case FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH:
            rnti_type = TYPE_SI_RNTI_;
            break;
          case FAPI_NR_DL_CONFIG_TYPE_DLSCH:
            rnti_type = TYPE_C_RNTI_;
            n_codewords = dlsch_config_pdu->n_codewords;
            break;
          case FAPI_NR_DL_CONFIG_TYPE_P_DLSCH:
            rnti_type = TYPE_P_RNTI_;
            n_codewords = dlsch_config_pdu->n_codewords;
            break;
          default:
            AssertFatal(false, "Unexpected DL DLSCH-family pdu_type %d\n", pdu->pdu_type);
        }

        for (int c = 0; c < n_codewords; c++) {
          NR_UE_DLSCH_t *dlsch = &phy_data->dlsch[c];
          dlsch->rnti_type = rnti_type;
          configure_dlsch(dlsch, phy->dl_harq_processes[c], dlsch_config_pdu, mac, c, pdu->dlsch_config_pdu.rnti, dl_config);
        }
      } break;
      case FAPI_NR_CONFIG_TA_COMMAND:
        configure_ta_command(phy, &pdu->ta_command_pdu);
        break;
      default:
        LOG_W(PHY, "unhandled dl pdu type %d \n", pdu->pdu_type);
    }
  }
  dl_config->number_pdus = 0;
}

static void dump_pusch_pdu(int instance, int frame, int slot, nfapi_nr_ue_pusch_pdu_t *pusch_pdu)
{
  LOG_D(PHY,
        "[UE %d] %d.%d ULSCH PDU pdu_bit_map %u "
        "rnti %u "
        "handle %u "
        "bwp_size %u "
        "bwp_start %u "
        "subcarrier_spacing %u "
        "cyclic_prefix %u "
        "target_code_rate %u "
        "qam_mod_order %u "
        "mcs_index %u "
        "mcs_table %u "
        "transform_precoding %u "
        "data_scrambling_id %u "
        "nrOfLayers %u "
        "Tpmi %u "
        "ul_dmrs_symb_pos %u "
        "dmrs_config_type %u "
        "ul_dmrs_scrambling_id %u "
        "scid %u "
        "num_dmrs_cdm_grps_no_data %u "
        "dmrs_ports %u "
        "resource_alloc %u "
        "rb_start %u "
        "rb_size %u "
        "vrb_to_prb_mapping %u "
        "frequency_hopping %u "
        "tx_direct_current_location %u "
        "uplink_frequency_shift_7p5khz %u "
        "start_symbol_index %u "
        "nr_of_symbols %u "
        "tbslbrm %u "
        "ldpcBaseGraph %u "
        "ulsch_indicator %u "
        "pusch_data->rv_index %u "
        "pusch_data->harq_process_id %u "
        "pusch_data->new_data_indicator %u "
        "pusch_data->num_cb %u\n",
        instance,
        frame,
        slot,
        pusch_pdu->pdu_bit_map,
        pusch_pdu->rnti,
        pusch_pdu->handle,
        pusch_pdu->bwp_size,
        pusch_pdu->bwp_start,
        pusch_pdu->subcarrier_spacing,
        pusch_pdu->cyclic_prefix,
        pusch_pdu->target_code_rate,
        pusch_pdu->qam_mod_order,
        pusch_pdu->mcs_index,
        pusch_pdu->mcs_table,
        pusch_pdu->transform_precoding,
        pusch_pdu->data_scrambling_id,
        pusch_pdu->nrOfLayers,
        pusch_pdu->Tpmi,
        pusch_pdu->ul_dmrs_symb_pos,
        pusch_pdu->dmrs_config_type,
        pusch_pdu->ul_dmrs_scrambling_id,
        pusch_pdu->scid,
        pusch_pdu->num_dmrs_cdm_grps_no_data,
        pusch_pdu->dmrs_ports,
        pusch_pdu->resource_alloc,
        pusch_pdu->rb_start,
        pusch_pdu->rb_size,
        pusch_pdu->vrb_to_prb_mapping,
        pusch_pdu->frequency_hopping,
        pusch_pdu->tx_direct_current_location,
        pusch_pdu->uplink_frequency_shift_7p5khz,
        pusch_pdu->start_symbol_index,
        pusch_pdu->nr_of_symbols,
        pusch_pdu->tbslbrm,
        pusch_pdu->ldpcBaseGraph,
        pusch_pdu->ulsch_indicator,
        pusch_pdu->pusch_data.rv_index,
        pusch_pdu->pusch_data.harq_process_id,
        pusch_pdu->pusch_data.new_data_indicator,
        pusch_pdu->pusch_data.num_cb);
}

static void nr_ue_scheduled_response_ul(PHY_VARS_NR_UE *phy, fapi_nr_ul_config_request_t *ul_config, nr_phy_data_tx_t *phy_data)
{
  fapi_nr_ul_config_request_pdu_t *pdu = fapiLockIterator(ul_config, ul_config->frame, ul_config->slot);
  if (!pdu) {
    LOG_E(NR_MAC, "Error in locking ul scheduler dtata\n");
    return;
  }

  while (pdu->pdu_type != FAPI_NR_END) {
    switch (pdu->pdu_type) {
      case FAPI_NR_UL_CONFIG_TYPE_PUSCH: {
        // pusch config pdu
        int current_harq_pid = pdu->pusch_config_pdu.pusch_data.harq_process_id;
        NR_UL_UE_HARQ_t *harq_process_ul_ue = &phy->ul_harq_processes[current_harq_pid];
        nfapi_nr_ue_pusch_pdu_t *pusch_pdu = &phy_data->ulsch.pusch_pdu;
        LOG_D(PHY,
              "copy pusch_config_pdu nrOfLayers: %d, num_dmrs_cdm_grps_no_data: %d\n",
              pdu->pusch_config_pdu.nrOfLayers,
              pdu->pusch_config_pdu.num_dmrs_cdm_grps_no_data);

        memcpy(pusch_pdu, &pdu->pusch_config_pdu, sizeof(*pusch_pdu));
        dump_pusch_pdu(phy->Mod_id, ul_config->frame, ul_config->slot, pusch_pdu);
        if (pdu->pusch_config_pdu.tx_request_body.fapiTxPdu) {
          LOG_D(PHY,
                "%d.%d Copying %d bytes to harq_process_ul_ue->a (harq_pid %d)\n",
                ul_config->frame,
                ul_config->slot,
                pdu->pusch_config_pdu.tx_request_body.pdu_length,
                current_harq_pid);
          memcpy(harq_process_ul_ue->payload_AB,
                 pdu->pusch_config_pdu.tx_request_body.fapiTxPdu,
                 pdu->pusch_config_pdu.tx_request_body.pdu_length);
        }

        phy_data->ulsch.status = NR_ACTIVE;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_PUCCH: {
        bool found = false;
        for (int j = 0; j < 2; j++) {
          if (phy_data->pucch_vars.active[j] == false) {
            LOG_D(PHY, "Copying pucch pdu to UE PHY\n");
            phy_data->pucch_vars.pucch_pdu[j] = pdu->pucch_config_pdu;
            phy_data->pucch_vars.active[j] = true;
            found = true;
            pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
            break;
          }
        }
        if (!found)
          LOG_E(PHY, "Couldn't find allocation for PUCCH PDU in PUCCH VARS\n");
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_PRACH: {
        phy->prach_vars[0]->prach_pdu = pdu->prach_config_pdu;
        phy->prach_vars[0]->active = true;
        phy->timing_advance = 0;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
      } break;

      case FAPI_NR_UL_CONFIG_TYPE_DONE:
        break;

      case FAPI_NR_UL_CONFIG_TYPE_SRS:
        // srs config pdu
        phy_data->srs_vars.srs_config_pdu = pdu->srs_config_pdu;
        phy_data->srs_vars.active = true;
        pdu->pdu_type = FAPI_NR_UL_CONFIG_TYPE_DONE; // not handle it any more
        break;

      default:
        LOG_W(PHY, "unhandled ul pdu type %d \n", pdu->pdu_type);
        break;
    }
    pdu++;
  }

  LOG_D(PHY, "clear ul_config\n");
  release_ul_config(pdu, true);
}

int8_t nr_ue_scheduled_response(nr_scheduled_response_t *scheduled_response)
{
  PHY_VARS_NR_UE *phy = nrPHY_vars_UE_g[scheduled_response->module_id][0];
  AssertFatal(!scheduled_response->dl_config || !scheduled_response->ul_config || !scheduled_response->sl_rx_config
                  || !scheduled_response->sl_tx_config,
              "phy_data parameter will be cast to two different types!\n");

  if (scheduled_response->dl_config)
    nr_ue_scheduled_response_dl(scheduled_response->mac,
                                phy,
                                scheduled_response->dl_config,
                                (nr_phy_data_t *)scheduled_response->phy_data);
  if (scheduled_response->ul_config)
    nr_ue_scheduled_response_ul(phy, scheduled_response->ul_config, (nr_phy_data_tx_t *)scheduled_response->phy_data);

  if (scheduled_response->sl_rx_config || scheduled_response->sl_tx_config) {
    sl_handle_scheduled_response(scheduled_response);
  }

  return 0;
}

void nr_ue_phy_config_request(nr_phy_config_t *phy_config)
{
  PHY_VARS_NR_UE *phy = nrPHY_vars_UE_g[phy_config->Mod_id][phy_config->CC_id];
  fapi_nr_config_request_t *nrUE_config = &phy->nrUE_config;
  if(phy_config != NULL) {
    phy->received_config_request = true;
    memcpy(nrUE_config, &phy_config->config_req, sizeof(fapi_nr_config_request_t));
  }
}

void nr_ue_synch_request(nr_synch_request_t *synch_request)
{
  fapi_nr_synch_request_t *synch_req = &nrPHY_vars_UE_g[synch_request->Mod_id][synch_request->CC_id]->synch_request.synch_req;
  memcpy(synch_req, &synch_request->synch_req, sizeof(fapi_nr_synch_request_t));
  nrPHY_vars_UE_g[synch_request->Mod_id][synch_request->CC_id]->synch_request.received_synch_request = 1;
}

void nr_ue_sl_phy_config_request(nr_sl_phy_config_t *phy_config)
{
  PHY_VARS_NR_UE *phy = nrPHY_vars_UE_g[phy_config->Mod_id][phy_config->CC_id];
  sl_nr_phy_config_request_t *sl_config = &phy->SL_UE_PHY_PARAMS.sl_config;
  if (phy_config != NULL) {
    phy->received_config_request = true;
    memcpy(sl_config, &phy_config->sl_config_req, sizeof(sl_nr_phy_config_request_t));
  }
}

/*
 * MAC sends the scheduled response with either TX configrequest for Sidelink Transmission requests
 * or RX config request for Sidelink Reception requests.
 * This procedure handles these TX/RX config requests received in this slot and configures PHY
 * with a TTI action to be performed in this slot(TTI)
 */
void sl_handle_scheduled_response(nr_scheduled_response_t *scheduled_response)
{
  module_id_t module_id = scheduled_response->module_id;
  const char *sl_rx_action[] = {"NONE", "RX_PSBCH", "RX_PSCCH", "RX_SCI2_ON_PSSCH", "RX_SLSCH_ON_PSSCH"};
  const char *sl_tx_action[] = {"TX_PSBCH", "TX_PSCCH_PSSCH", "TX_PSFCH"};

  if (scheduled_response->sl_rx_config != NULL) {
    sl_nr_rx_config_request_t *sl_rx_config = scheduled_response->sl_rx_config;
    nr_phy_data_t *phy_data = (nr_phy_data_t *)scheduled_response->phy_data;

    AssertFatal(sl_rx_config->number_pdus == SL_NR_RX_CONFIG_LIST_NUM, "sl_rx_config->number_pdus incorrect\n");

    switch (sl_rx_config->sl_rx_config_list[0].pdu_type) {
      case SL_NR_CONFIG_TYPE_RX_PSBCH:
        phy_data->sl_rx_action = SL_NR_CONFIG_TYPE_RX_PSBCH;
        LOG_D(PHY, "Recvd CONFIG_TYPE_RX_PSBCH\n");
        break;
      default:
        AssertFatal(0, "Incorrect sl_rx config req pdutype \n");
        break;
    }

    LOG_D(PHY,
          "[UE%d] TTI %d:%d, SL-RX action:%s\n",
          module_id,
          sl_rx_config->sfn,
          sl_rx_config->slot,
          sl_rx_action[phy_data->sl_rx_action]);

  } else if (scheduled_response->sl_tx_config != NULL) {
    sl_nr_tx_config_request_t *sl_tx_config = scheduled_response->sl_tx_config;
    nr_phy_data_tx_t *phy_data_tx = (nr_phy_data_tx_t *)scheduled_response->phy_data;

    AssertFatal(sl_tx_config->number_pdus == SL_NR_TX_CONFIG_LIST_NUM, "sl_tx_config->number_pdus incorrect \n");

    switch (sl_tx_config->tx_config_list[0].pdu_type) {
      case SL_NR_CONFIG_TYPE_TX_PSBCH:
        phy_data_tx->sl_tx_action = SL_NR_CONFIG_TYPE_TX_PSBCH;
        LOG_D(PHY, "Recvd CONFIG_TYPE_TX_PSBCH\n");
        *((uint32_t *)phy_data_tx->psbch_vars.psbch_payload) =
            *((uint32_t *)sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.psbch_payload);
        phy_data_tx->psbch_vars.psbch_tx_power = sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.psbch_tx_power;
        phy_data_tx->psbch_vars.tx_slss_id = sl_tx_config->tx_config_list[0].tx_psbch_config_pdu.tx_slss_id;
        break;
      default:
        AssertFatal(0, "Incorrect sl_tx config req pdutype \n");
        break;
    }

    LOG_D(PHY,
          "[UE%d] TTI %d:%d, SL-TX action:%s slss_id:%d, sl-mib:%x, psbch pwr:%d\n",
          module_id,
          sl_tx_config->sfn,
          sl_tx_config->slot,
          sl_tx_action[phy_data_tx->sl_tx_action - 6],
          phy_data_tx->psbch_vars.tx_slss_id,
          *((uint32_t *)phy_data_tx->psbch_vars.psbch_payload),
          phy_data_tx->psbch_vars.psbch_tx_power);
  }

}
