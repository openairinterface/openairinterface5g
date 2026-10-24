/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Implementation of UE procedures from 36.213 LTE specifications
 */

#include "PHY/defs_RU.h"
#include "utils.h"
#define _GNU_SOURCE

#include "nr/nr_common.h"
#include "assertions.h"
#include "defs.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common_srs.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_REFSIG/ss_pbch_nr.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_UE_ESTIMATION/nr_estimation.h"
#include "executables/softmodem-common.h"
#include "executables/nr-uesoftmodem.h"
#include "SCHED_NR_UE/pucch_uci_ue_nr.h"
#include <openair1/PHY/TOOLS/phy_scope_interface.h>
#include "nfapi/open-nFAPI/nfapi/public_inc/nfapi_nr_interface.h"

//#define DEBUG_PHY_PROC
//#define NR_PDCCH_SCHED_DEBUG
//#define NR_PUCCH_SCHED
//#define NR_PUCCH_SCHED_DEBUG
//#define NR_PDSCH_DEBUG

#ifndef PUCCH
#define PUCCH
#endif

#include "common/utils/LOG/log.h"

#include "UTIL/OPT/opt.h"
#include "T.h"
#include "instrumentation.h"

/// minimum time between two measurements of the neighboring cells (TS 38.133 9.2.5.1)
#define NR_UE_MEAS_PERIOD_MS 200

static const unsigned int gain_table[31] = {100,  112,  126,  141,  158,  178,  200,  224,  251, 282,  316,
                                            359,  398,  447,  501,  562,  631,  708,  794,  891, 1000, 1122,
                                            1258, 1412, 1585, 1778, 1995, 2239, 2512, 2818, 3162};

static void nr_ue_prach_procedures(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, c16_t **txData);

static uint32_t get_ssb_arfcn(NR_DL_FRAME_PARMS *frame_parms)
{
  uint32_t band_size_hz = frame_parms->N_RB_DL * 12 * frame_parms->subcarrier_spacing;
  int ssb_center_sc = frame_parms->ssb_start_subcarrier + 120; // ssb is 20 PRBs -> 240 sub-carriers
  uint64_t ssb_freq = frame_parms->dl_CarrierFreq - (band_size_hz / 2) + frame_parms->subcarrier_spacing * ssb_center_sc;
  return to_nrarfcn(ssb_freq);
}

void nr_fill_rx_indication(fapi_nr_rx_indication_t *rx_ind,
                           uint8_t pdu_type,
                           PHY_VARS_NR_UE *ue,
                           int cw_idx,
                           int harq_pid,
                           NR_UE_DLSCH_t *dlsch,
                           const UE_nr_rxtx_proc_t *proc,
                           void *typeSpecific)
{
  AssertFatal(rx_ind->number_pdus < NFAPI_RX_IND_MAX_PDU - 1, "Exceeded rx_ind array size\n");
  fapi_nr_rx_indication_body_t *rx = rx_ind->rx_indication_body + rx_ind->number_pdus;
  rx_ind->number_pdus++;
  *rx = (fapi_nr_rx_indication_body_t){.pdu_type = pdu_type};
  switch (pdu_type){
    case FAPI_NR_RX_PDU_TYPE_SIB:
    case FAPI_NR_RX_PDU_TYPE_RAR:
    case FAPI_NR_RX_PDU_TYPE_DLSCH:
    case FAPI_NR_RX_PDU_TYPE_PCCH:
      if(dlsch) {
        NR_DL_UE_HARQ_t *dl_harq = &ue->dl_harq_processes[cw_idx][harq_pid];
        rx->pdsch_pdu.harq_pid = harq_pid;
        rx->pdsch_pdu.cw_idx = cw_idx;
        rx->pdsch_pdu.ack_nack = dl_harq->decodeResult;
        rx->pdsch_pdu.pdu = (uint8_t *)typeSpecific;
        rx->pdsch_pdu.pdu_length = dlsch->cw_info.TBS / 8;
        if (dl_harq->decodeResult) {
          int t = WS_C_RNTI;
          if (pdu_type == FAPI_NR_RX_PDU_TYPE_RAR)
            t = WS_RA_RNTI;
          if (pdu_type == FAPI_NR_RX_PDU_TYPE_SIB)
            t = WS_SI_RNTI;
          if (pdu_type == FAPI_NR_RX_PDU_TYPE_PCCH)
            t = WS_P_RNTI;
          ws_trace_t tmp = {.nr = true,
                            .direction = DIRECTION_DOWNLINK,
                            .type = ue->frame_parms.frame_type == FDD ? FDD_RADIO : TDD_RADIO,
                            .pdu_buffer = rx->pdsch_pdu.pdu,
                            .pdu_buffer_size = rx->pdsch_pdu.pdu_length,
                            .ueid = 0,
                            .rntiType = t,
                            .rnti = dlsch->rnti,
                            .sysFrame = proc->frame_rx,
                            .subframe = proc->nr_slot_rx,
                            .harq_pid = harq_pid};
          trace_pdu(&tmp);
        }
      }
      break;
    case FAPI_NR_RX_PDU_TYPE_SSB: {
      if (typeSpecific) {
        NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
        fapiPbch_t *pbch = (fapiPbch_t *)typeSpecific;
        memcpy(rx->ssb_pdu.pdu, pbch->decoded_output, sizeof(pbch->decoded_output));
        rx->ssb_pdu.additional_bits = pbch->xtra_byte;
        rx->ssb_pdu.ssb_index = (frame_parms->ssb_index) & 0x7;
        rx->ssb_pdu.ssb_length = frame_parms->Lmax;
        rx->ssb_pdu.cell_id = frame_parms->Nid_cell;
        rx->ssb_pdu.ssb_start_subcarrier = frame_parms->ssb_start_subcarrier;
        rx->ssb_pdu.arfcn = get_ssb_arfcn(frame_parms);
        rx->ssb_pdu.radiolink_monitoring = RLM_in_sync; // TODO to be removed from here
        rx->ssb_pdu.decoded_pdu = true;
      } else {
        rx->ssb_pdu.radiolink_monitoring = RLM_out_of_sync; // TODO to be removed from here
        rx->ssb_pdu.decoded_pdu = false;
      }
    } break;
    case FAPI_NR_MEAS_IND:
      memcpy(&rx->l1_measurements, typeSpecific, sizeof(fapi_nr_l1_measurements_t));
      break;
    default:
    break;
  }
}

int get_tx_amp_prach(int power_dBm, int power_max_dBm, int N_RB_UL){

  int gain_dB = power_dBm - power_max_dBm, amp_x_100 = -1;

  switch (N_RB_UL) {
  case 6:
  amp_x_100 = AMP;      // PRACH is 6 PRBS so no scale
  break;
  case 15:
  amp_x_100 = 158*AMP;  // 158 = 100*sqrt(15/6)
  break;
  case 25:
  amp_x_100 = 204*AMP;  // 204 = 100*sqrt(25/6)
  break;
  case 50:
  amp_x_100 = 286*AMP;  // 286 = 100*sqrt(50/6)
  break;
  case 75:
  amp_x_100 = 354*AMP;  // 354 = 100*sqrt(75/6)
  break;
  case 100:
  amp_x_100 = 408*AMP;  // 408 = 100*sqrt(100/6)
  break;
  default:
  LOG_E(PHY, "Unknown PRB size %d\n", N_RB_UL);
  return (amp_x_100);
  break;
  }
  if (gain_dB < -30) {
    return (amp_x_100/3162);
  } else if (gain_dB > 0)
    return (amp_x_100);
  else
    return (amp_x_100/gain_table[-gain_dB]);  // 245 corresponds to the factor sqrt(25/6)

  return (amp_x_100);
}

// UL time alignment procedures:
// - If the current tx frame and slot match the TA configuration
//   then timing advance is processed and set to be applied in the next UL transmission
// - Application of timing adjustment according to TS 38.213 p4.2
// - handle RAR TA application as per ch 4.2 TS 38.213
void ue_ta_procedures(PHY_VARS_NR_UE *ue, int slot_tx, int frame_tx)
{
  if (frame_tx == ue->ta_frame && slot_tx == ue->ta_slot) {
    uint16_t ofdm_symbol_size = ue->frame_parms.ofdm_symbol_size;

    // convert time factor "16 * 64 * T_c / (2^mu)" in N_TA calculation in TS38.213 section 4.2 to samples by multiplying with
    // samples per second
    //   16 * 64 * T_c            / (2^mu) * samples_per_second
    // = 16 * T_s                 / (2^mu) * samples_per_second
    // = 16 * 1 / (15 kHz * 2048) / (2^mu) * (15 kHz * 2^mu * ofdm_symbol_size)
    // = 16 * 1 /           2048           *                  ofdm_symbol_size
    // = 16 * ofdm_symbol_size / 2048
    uint16_t bw_scaling = 16 * ofdm_symbol_size / 2048;

    const int timing_advance_before = ue->timing_advance;
    const bool ta_command_is_rar = ue->ta_command_is_rar;
    const int ta_units = ta_command_is_rar ? ue->ta_command : ue->ta_command - 31;
    const int ta_samples = ta_units * bw_scaling;

    if (ta_command_is_rar && timing_advance_before != 0)
      LOG_W(PHY,
            "[UE %d] %d.%d applying RAR TA command %d to non-zero timing advance %d samples; "
            "timing advance should have been reset before PRACH\n",
            ue->Mod_id,
            frame_tx,
            slot_tx,
            ue->ta_command,
            timing_advance_before);

    ue->timing_advance += ta_samples;

    LOG_D(PHY,
          "[UE %d] %d.%d applied %s command %d: TA-units %d, samples-per-unit %u, command-samples %d, "
          "timing-advance %d -> %d samples (N_TA_offset %d)\n",
          ue->Mod_id,
          frame_tx,
          slot_tx,
          ta_command_is_rar ? "RAR" : "relative MAC CE",
          ue->ta_command,
          ta_units,
          bw_scaling,
          ta_samples,
          timing_advance_before,
          ue->timing_advance,
          ue->N_TA_offset);

    ue->ta_frame = -1;
    ue->ta_slot = -1;
    ue->ta_command_is_rar = false;
  }
}

static void configure_srs_info(const fapi_nr_ul_config_srs_pdu *srs_config_pdu, nr_srs_info_t *nr_srs_info)
{
  nr_srs_info->B_SRS = srs_config_pdu->bandwidth_index;
  nr_srs_info->C_SRS = srs_config_pdu->config_index;
  nr_srs_info->b_hop = srs_config_pdu->frequency_hopping;
  nr_srs_info->comb_size = srs_config_pdu->comb_size;
  nr_srs_info->K_TC_overbar = srs_config_pdu->comb_offset;
  nr_srs_info->n_SRS_cs = srs_config_pdu->cyclic_shift;
  nr_srs_info->n_ID_SRS = srs_config_pdu->sequence_id;
  // It adjusts the SRS allocation to align with the common resource block grid in multiples of four
  nr_srs_info->n_shift = srs_config_pdu->frequency_shift;
  nr_srs_info->n_RRC = srs_config_pdu->frequency_position;
  nr_srs_info->groupOrSequenceHopping = srs_config_pdu->group_or_sequence_hopping;
  nr_srs_info->l_offset = srs_config_pdu->time_start_position;
  nr_srs_info->T_SRS = srs_config_pdu->t_srs;
  nr_srs_info->T_offset = srs_config_pdu->t_offset;
  nr_srs_info->R = 1 << srs_config_pdu->num_repetitions;
  nr_srs_info->N_symb_SRS = 1 << srs_config_pdu->num_symbols; // Number of consecutive OFDM symbols
  nr_srs_info->n_srs_ports = 1 << srs_config_pdu->num_ant_ports; // Number of antenna port for transmission
  nr_srs_info->resource_type = srs_config_pdu->resource_type;
}

/*******************************************************************
*
* NAME :         ue_srs_procedures_nr
*
* PARAMETERS :   pointer to ue context
*                pointer to rxtx context*
*
* DESCRIPTION :  ue srs procedure
*                send srs according to current configuration
*
*********************************************************************/
void ue_srs_procedures_nr(PHY_VARS_NR_UE *ue,
                          const UE_nr_rxtx_proc_t *proc,
                          c16_t **txdataF,
                          const fapi_nr_ul_config_srs_pdu *srs_config_pdu,
                          bool was_symbol_used[NR_SYMBOLS_PER_SLOT])
{
  NR_DL_FRAME_PARMS *frame_parms = &(ue->frame_parms);
  const uint8_t l0 = srs_config_pdu->time_start_position; // L2 sends the absolute symbol index
  // Num consecutive SRS symbols according to 38.211 6.4.1.4.1
  int num_srs_symbols[] = {1, 2, 4, 8, 12};
  int last_srs_symbol = l0 + num_srs_symbols[srs_config_pdu->num_symbols] - 1;
  for (int i = l0; i <= last_srs_symbol; i++) {
    was_symbol_used[i] = true;
  }

#ifdef SRS_DEBUG
  LOG_I(NR_PHY,"Frame = %i, slot = %i\n", proc->frame_tx, proc->nr_slot_tx);
  LOG_I(NR_PHY,"srs_config_pdu->rnti = 0x%04x\n", srs_config_pdu->rnti);
  LOG_I(NR_PHY,"srs_config_pdu->handle = %u\n", srs_config_pdu->handle);
  LOG_I(NR_PHY,"srs_config_pdu->bwp_size = %u\n", srs_config_pdu->bwp_size);
  LOG_I(NR_PHY,"srs_config_pdu->bwp_start = %u\n", srs_config_pdu->bwp_start);
  LOG_I(NR_PHY,"srs_config_pdu->subcarrier_spacing = %u\n", srs_config_pdu->subcarrier_spacing);
  LOG_I(NR_PHY,"srs_config_pdu->cyclic_prefix = %u (0: Normal; 1: Extended)\n", srs_config_pdu->cyclic_prefix);
  LOG_I(NR_PHY,"srs_config_pdu->num_ant_ports = %u (0 = 1 port, 1 = 2 ports, 2 = 4 ports)\n", srs_config_pdu->num_ant_ports);
  LOG_I(NR_PHY,"srs_config_pdu->num_symbols = %u (0 = 1 symbol, 1 = 2 symbols, 2 = 4 symbols)\n", srs_config_pdu->num_symbols);
  LOG_I(NR_PHY,"srs_config_pdu->num_repetitions = %u (0 = 1, 1 = 2, 2 = 4)\n", srs_config_pdu->num_repetitions);
  LOG_I(NR_PHY,"srs_config_pdu->time_start_position = %u\n", srs_config_pdu->time_start_position);
  LOG_I(NR_PHY,"srs_config_pdu->config_index = %u\n", srs_config_pdu->config_index);
  LOG_I(NR_PHY,"srs_config_pdu->sequence_id = %u\n", srs_config_pdu->sequence_id);
  LOG_I(NR_PHY,"srs_config_pdu->bandwidth_index = %u\n", srs_config_pdu->bandwidth_index);
  LOG_I(NR_PHY,"srs_config_pdu->comb_size = %u (0 = comb size 2, 1 = comb size 4, 2 = comb size 8)\n", srs_config_pdu->comb_size);
  LOG_I(NR_PHY,"srs_config_pdu->comb_offset = %u\n", srs_config_pdu->comb_offset);
  LOG_I(NR_PHY,"srs_config_pdu->cyclic_shift = %u\n", srs_config_pdu->cyclic_shift);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_position = %u\n", srs_config_pdu->frequency_position);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_shift = %u\n", srs_config_pdu->frequency_shift);
  LOG_I(NR_PHY,"srs_config_pdu->frequency_hopping = %u\n", srs_config_pdu->frequency_hopping);
  LOG_I(NR_PHY,"srs_config_pdu->group_or_sequence_hopping = %u (0 = No hopping, 1 = Group hopping groupOrSequenceHopping, 2 = Sequence hopping)\n", srs_config_pdu->group_or_sequence_hopping);
  LOG_I(NR_PHY,"srs_config_pdu->resource_type = %u (0: aperiodic, 1: semi-persistent, 2: periodic)\n", srs_config_pdu->resource_type);
  LOG_I(NR_PHY,"srs_config_pdu->t_srs = %u\n", srs_config_pdu->t_srs);
  LOG_I(NR_PHY,"srs_config_pdu->t_offset = %u\n", srs_config_pdu->t_offset);
#endif

  nr_srs_info_t nr_srs_info = {0};
  configure_srs_info(srs_config_pdu, &nr_srs_info);
  uint16_t symbol_offset = l0 * frame_parms->ofdm_symbol_size;
  bool generated = generate_srs_nr(frame_parms,
                                   txdataF,
                                   symbol_offset,
                                   srs_config_pdu->bwp_start,
                                   &nr_srs_info,
                                   AMP,
                                   proc->frame_tx,
                                   proc->nr_slot_tx,
                                   frame_parms->nb_antennas_tx);
  DevAssert(generated); // if we can't generate despite the SRS config, there
                        // is a problem
}

void phy_procedures_nrUE_TX(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_tx_t *phy_data, c16_t **txp)
{
  const int slot_tx = proc->nr_slot_tx;
  const int frame_tx = proc->frame_tx;

  AssertFatal(ue->CC_id == 0, "Transmission on secondary CCs is not supported yet\n");

#if T_TRACER
  T(T_UE_PHY_UL_TICK, T_INT(ue->Mod_id), T_INT(frame_tx % 1024), T_INT(slot_tx));
#endif

  const int samplesF_per_slot = ue->frame_parms.symbols_per_slot * ue->frame_parms.ofdm_symbol_size;
  c16_t txdataF_buf[ue->frame_parms.nb_antennas_tx * samplesF_per_slot] __attribute__((aligned(32)));
  /* the RE mapping below writes only the REs of the channels: zero the slot only when there is one. Else no symbol is used
     and nr_tx_rotation_and_ofdm_mod() zeroes the time samples without reading txdataF */
  const bool ul_channel = phy_data->ulsch.status == NR_ACTIVE || phy_data->pucch_vars.active[0] || phy_data->pucch_vars.active[1]
                          || phy_data->srs_vars.active;
  if (ul_channel)
    memset(txdataF_buf, 0, sizeof(txdataF_buf));
  c16_t *txdataF[ue->frame_parms.nb_antennas_tx]; /* workaround to be compatible with current txdataF usage in all tx procedures. */
  for(int i=0; i< ue->frame_parms.nb_antennas_tx; ++i)
    txdataF[i] = &txdataF_buf[i * samplesF_per_slot];

  LOG_D(PHY,"****** start TX-Chain for AbsSubframe %d.%d ******\n", frame_tx, slot_tx);
  bool was_symbol_used[NR_SYMBOLS_PER_SLOT] = {0};

  start_meas_nr_ue_phy(ue, PHY_PROC_TX);

  nr_ue_ulsch_procedures(ue, frame_tx, slot_tx, phy_data, (c16_t **)&txdataF, was_symbol_used);

  if (phy_data->srs_vars.active)
    ue_srs_procedures_nr(ue, proc, (c16_t **)&txdataF, &phy_data->srs_vars.srs_config_pdu, was_symbol_used);

  pucch_procedures_ue_nr(ue, proc, phy_data, (c16_t **)&txdataF, was_symbol_used);

  LOG_D(PHY, "Sending Uplink data \n");

  // Don't do OFDM Mod if txdata contains prach
  const NR_UE_PRACH *prach_var = ue->prach_vars[proc->gNB_id];
  if (!prach_var->active) {
    start_meas_nr_ue_phy(ue, OFDM_MOD_STATS);
    nr_tx_rotation_and_ofdm_mod(proc->nr_slot_tx,
                                &ue->frame_parms,
                                ue->frame_parms.nb_antennas_tx,
                                (c16_t **)txdataF,
                                txp,
                                link_type_ul,
                                was_symbol_used,
                                ue->no_phase_pre_comp);
    stop_meas_nr_ue_phy(ue, OFDM_MOD_STATS);
  }

  nr_ue_prach_procedures(ue, proc, txp);

  LOG_D(PHY, "****** end TX-Chain for AbsSubframe %d.%d ******\n", proc->frame_tx, proc->nr_slot_tx);

  stop_meas_nr_ue_phy(ue, PHY_PROC_TX);
}

static void nr_ue_measurement_procedures(uint16_t l,
                                         PHY_VARS_NR_UE *ue,
                                         const UE_nr_rxtx_proc_t *proc,
                                         int number_rbs,
                                         uint32_t pdsch_est_size,
                                         int32_t dl_ch_estimates[][pdsch_est_size])
{
  int nr_slot_rx = proc->nr_slot_rx;
  int gNB_id = proc->gNB_id;

  LOG_D(PHY,
        "Doing UE measurement procedures in symbol l %u Ncp %d nr_slot_rx %d, rxdata %p\n",
        l,
        ue->frame_parms.Ncp,
        nr_slot_rx,
        ue->common_vars.rxdata);
  nr_ue_measurements(ue, proc, number_rbs, l, pdsch_est_size, dl_ch_estimates);
#if T_TRACER
  if (nr_slot_rx == 0)
    T(T_UE_PHY_MEAS,
      T_INT(gNB_id),
      T_INT(proc->frame_rx % 1024),
      T_INT(nr_slot_rx),
      T_INT((int)(10 * log10(ue->measurements.rsrp[0]) - ue->rx_total_gain_dB)),
      T_INT((int)ue->measurements.rx_rssi_dBm[0]),
      T_INT((int)(ue->measurements.rx_power_avg_dB[0] - ue->measurements.n0_power_avg_dB)),
      T_INT((int)ue->measurements.rx_power_avg_dB[0]),
      T_INT((int)ue->measurements.n0_power_avg_dB),
      T_INT((int)ue->measurements.wideband_cqi_avg[0]),
      T_INT((int)ue->common_vars.freq_offset));
#endif

  // accumulate and filter timing offset estimation every subframe (instead of every frame)
  if (nr_slot_rx == 2) {
    // AGC
    //printf("start adjust gain power avg db %d\n", ue->measurements.rx_power_avg_dB[gNB_id]);
    phy_adjust_gain_nr (ue,ue->measurements.rx_power_avg_dB[gNB_id],gNB_id);
  }
}

static int nr_ue_pdsch_procedures(PHY_VARS_NR_UE *ue,
                                  const UE_nr_rxtx_proc_t *proc,
                                  NR_UE_DLSCH_t *dlsch,
                                  NR_DL_UE_HARQ_t *dlsch_harq,
                                  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlschCfg,
                                  int16_t *llr,
                                  c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP],
                                  freq_alloc_bitmap_t *freq_alloc)
{
  int frame_rx = proc->frame_rx;
  int nr_slot_rx = proc->nr_slot_rx;

  // We handle only one CW now
  if (NR_MAX_NB_LAYERS > 4) {
    LOG_E(NR_PHY, "Handling of more than one CW not implemented\n");
    return -1;
  }

  int harq_pid = dlschCfg->harq_process_nbr;
  const uint32_t dmrs_symb_bitmap = dlschCfg->dlDmrsSymbPos;

  LOG_D(PHY,
        "[UE %d] frame_rx %d, nr_slot_rx %d, harq_pid %d (%d), BWP start %d, start RB %d, end RB %d, symbol_start %d, nb_symbols "
        "%d, DMRS mask "
        "%x, Nl %d\n",
        ue->Mod_id,
        frame_rx,
        nr_slot_rx,
        harq_pid,
        dlsch_harq->status,
        dlschCfg->BWPStart,
        freq_alloc->first_rb,
        freq_alloc->last_rb,
        dlschCfg->start_symbol,
        dlschCfg->number_symbols,
        dmrs_symb_bitmap,
        dlsch->cw_info.Nl);

  const int actor_idx = proc->nr_slot_rx % ue->pdsch_num_actors;
  pdsch_scratch_t *scratch = &ue->pdsch_scratch[actor_idx];
  const uint32_t pdsch_est_size = scratch->pdsch_est_size;
  const uint32_t pdsch_buf_size_max = scratch->pdsch_buf_size_max;
  const int max_layers = min(ue->frame_parms.nb_antennas_rx, NR_MAX_NB_LAYERS);
  int32_t (*pdsch_dl_ch_estimates)[pdsch_est_size] = (int32_t (*)[pdsch_est_size])scratch->pdsch_dl_ch_estimates;
  c16_t(*rxdataF_comp)[pdsch_buf_size_max] = (c16_t(*)[pdsch_buf_size_max])scratch->rxdataF_comp;
  c16_t(*dl_ch_mag)[pdsch_buf_size_max] = (c16_t(*)[pdsch_buf_size_max])scratch->dl_ch_mag;
  c16_t(*dl_ch_magb)[pdsch_buf_size_max] = (c16_t(*)[pdsch_buf_size_max])scratch->dl_ch_magb;
  c16_t(*dl_ch_magr)[pdsch_buf_size_max] = (c16_t(*)[pdsch_buf_size_max])scratch->dl_ch_magr;
  c16_t(*rho_dl)[pdsch_buf_size_max] = (c16_t(*)[pdsch_buf_size_max])scratch->rho_dl;

  NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  uint32_t nvar = 0;

  start_meas_nr_ue_phy(ue, DLSCH_CHANNEL_ESTIMATION_STATS);
  for (int m = dlschCfg->start_symbol; m < (dlschCfg->start_symbol + dlschCfg->number_symbols); m++) {
    if (dmrs_symb_bitmap & (1 << m)) {
      for (int nl = 0; nl < dlsch->cw_info.Nl; nl++) { // for MIMO Config: it shall loop over no_layers
        LOG_D(PHY, "PDSCH Channel estimation layer %d, slot %d, symbol %d\n", nl, nr_slot_rx, m);
        uint32_t nvar_tmp = 0;
        nr_pdsch_channel_estimation(ue,
                                    proc,
                                    dlschCfg,
                                    freq_alloc,
                                    nl,
                                    get_dmrs_port(nl, dlschCfg->dmrs_ports),
                                    m,
                                    pdsch_est_size,
                                    pdsch_dl_ch_estimates,
                                    frame_parms->samples_per_slot_wCP,
                                    rxdataF,
                                    &nvar_tmp);
        nvar += nvar_tmp;
      }
    }
  }
  stop_meas_nr_ue_phy(ue, DLSCH_CHANNEL_ESTIMATION_STATS);
  nvar /= (count_bits(&dmrs_symb_bitmap, 1) * dlsch->cw_info.Nl * frame_parms->nb_antennas_rx);
  int first_dmrs_symbol = get_first_bit_index_mask(&dmrs_symb_bitmap, 1, 0, NR_SYMBOLS_PER_SLOT);
  nr_ue_measurement_procedures(first_dmrs_symbol, ue, proc, freq_alloc->num_rbs, pdsch_est_size, pdsch_dl_ch_estimates);

  // PTRS processing.
  const bool is_ptrs = dlschCfg->pduBitmap & 1;
  c16_t cpe[NR_SYMBOLS_PER_SLOT];
  for (uint s = 0; s < NR_SYMBOLS_PER_SLOT; s++)
    cpe[s] = (c16_t){.r = INT16_MAX}; // zero phase error.
  uint ptrs_re_symbol = 0;
  uint16_t ptrs_symb_pos = 0;
  if (is_ptrs) {
    if (dlschCfg->PTRSPortIndex != 1) {
      LOG_W(NR_PHY, "Multi-port PTRS not supported, skipping PTRS processing\n");
    } else {
      const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
      ptrs_proc_t p = {.k_ptrs = dlschCfg->PTRSFreqDensity,
                       .k_re_ref = dlschCfg->PTRSReOffset,
                       .symbols_per_slot = fp->symbols_per_slot,
                       .start_rb = freq_alloc->first_rb,
                       .num_rb = freq_alloc->num_rbs,
                       .N_RB = fp->N_RB_DL,
                       .start_symb = dlschCfg->start_symbol,
                       .num_symb = dlschCfg->number_symbols,
                       .dmrs_symb_pos = dmrs_symb_bitmap,
                       .nid = fp->Nid_cell,
                       .nscid = dlschCfg->nscid,
                       .ofdm_symbol_size = fp->ofdm_symbol_size,
                       .slot = proc->nr_slot_rx,
                       .rnti = dlsch->rnti};
      ptrs_re_symbol = nr_ptrs_run(&p, dlschCfg->PTRSTimeDensity, rxdataF[0], (const c16_t *)pdsch_dl_ch_estimates[0], cpe);
      ptrs_symb_pos = p.ptrs_symb_pos;
    }
  }

  if (ue->chest_time == 1) { // averaging time domain channel estimates
    AssertFatal(!is_ptrs, "Time domain averaging of DMRS estimates not allowed with PTRS\n");
    int32_t *ch_est_p[dlsch->cw_info.Nl * frame_parms->nb_antennas_rx];
    for (int nl = 0; nl < dlsch->cw_info.Nl; nl++)
      for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++)
        ch_est_p[nl * frame_parms->nb_antennas_rx + aarx] = pdsch_dl_ch_estimates[nl * frame_parms->nb_antennas_rx + aarx];
    nr_chest_time_domain_avg(frame_parms,
                             ch_est_p,
                             dlschCfg->number_symbols,
                             dlschCfg->start_symbol,
                             dmrs_symb_bitmap,
                             freq_alloc->num_rbs,
                             dlsch->cw_info.Nl,
                             frame_parms->nb_antennas_rx);
  }

  uint16_t first_symbol_with_data = dlschCfg->start_symbol;
  uint32_t dmrs_data_re;

  if (dlschCfg->dmrsConfigType == NFAPI_NR_DMRS_TYPE1)
    dmrs_data_re = 12 - 6 * dlschCfg->n_dmrs_cdm_groups;
  else
    dmrs_data_re = 12 - 4 * dlschCfg->n_dmrs_cdm_groups;

  while ((dmrs_data_re == 0) && (dmrs_symb_bitmap & (1 << first_symbol_with_data))) {
    first_symbol_with_data++;
  }

  int32_t log2_maxh = 0;

  start_meas_nr_ue_phy(ue, RX_PDSCH_STATS);
  pdsch_scope_req_t scope_req = {0};
  /* rxdataF_comp is 1-symbol-wide and overwritten each nr_rx_pdsch() call, so we lock
   * before the loop and copy each symbol's data inside nr_rx_pdsch while it is still in
   * the buffer.  The lock uses the same upper-bound RE count as the other scope buffers. */
  if (UEScopeHasTryLock(ue)) {
    metadata mt = {.frame = proc->frame_rx, .slot = proc->nr_slot_rx};
    int total_re = freq_alloc->num_rbs * NR_NB_SC_PER_RB * dlschCfg->number_symbols;
    scope_req.copy_chanest_to_scope = UETryLockScopeData(ue, pdschChanEstimates, sizeof(c16_t), 1, total_re, &mt);
    scope_req.copy_rxdataF_to_scope = UETryLockScopeData(ue, pdschRxdataF, sizeof(c16_t), 1, total_re, &mt);
    scope_req.copy_rxdataF_comp_to_scope = UETryLockScopeData(ue, pdschRxdataF_comp, sizeof(c16_t), 1, total_re, &mt);
  }

  int16_t *llr_out = llr;
  for (int m = dlschCfg->start_symbol; m < (dlschCfg->number_symbols + dlschCfg->start_symbol); m++) {
    // Symbol fully occupied by DMRS with no multiplexed data (e.g. DMRS Type 1 with
    // n_dmrs_cdm_groups == 2): skip the call entirely, nr_rx_pdsch would just report 0 REs.
    if (dmrs_data_re == 0 && (dlschCfg->dlDmrsSymbPos & (1 << m)))
      continue;

    uint32_t nb_re = nr_rx_pdsch(ue,
                                 proc,
                                 dlsch,
                                 freq_alloc,
                                 dlschCfg,
                                 dlsch_harq,
                                 m,
                                 (m == first_symbol_with_data),
                                 harq_pid,
                                 pdsch_est_size,
                                 pdsch_dl_ch_estimates,
                                 llr_out,
                                 rxdataF,
                                 &log2_maxh,
                                 pdsch_buf_size_max,
                                 frame_parms->nb_antennas_rx,
                                 max_layers,
                                 rxdataF_comp,
                                 dl_ch_mag,
                                 dl_ch_magb,
                                 dl_ch_magr,
                                 cpe[m],
                                 IS_BIT_SET(ptrs_symb_pos, m) ? ptrs_re_symbol : 0,
                                 nvar,
                                 &scope_req,
                                 rho_dl,
                                 IS_BIT_SET(ptrs_symb_pos, m));
    llr_out += (size_t)nb_re * dlsch->cw_info.qamModOrder * dlsch->cw_info.Nl;
  } // CRNTI active
  stop_meas_nr_ue_phy(ue, RX_PDSCH_STATS);
  if (scope_req.copy_rxdataF_comp_to_scope) {
    UEunlockScopeData(ue, pdschRxdataF_comp)
  }
  if (scope_req.copy_chanest_to_scope) {
    UEunlockScopeData(ue, pdschChanEstimates);
  }
  if (scope_req.copy_rxdataF_to_scope) {
    UEunlockScopeData(ue, pdschRxdataF);
  }
  return 0;
}

static uint32_t compute_csi_rm_unav_res(fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config, freq_alloc_bitmap_t *freq_alloc)
{
  uint32_t unav_res = 0;
  for (int i = 0; i < dlsch_config->numCsiRsForRateMatching; i++) {
    fapi_nr_dl_config_csirs_pdu_rel15_t *csi_pdu = &dlsch_config->csiRsForRateMatching[i];
    // check overlapping symbols
    int num_overlap_symb = 0;
    // num of consecutive csi symbols from l0 included
    int num_l0 [18] = {1, 1, 1, 1, 2, 1, 2, 2, 1, 2, 2, 2, 2, 2, 4, 2, 2, 4};
    int num_symb = num_l0[csi_pdu->row - 1];
    for (int s = 0; s < num_symb; s++) {
      int l0_symb = csi_pdu->symb_l0 + s;
      if (l0_symb >= dlsch_config->start_symbol && l0_symb < dlsch_config->start_symbol + dlsch_config->number_symbols)
        num_overlap_symb++;
    }
    // check also l1 if relevant
    if (csi_pdu->row == 13 || csi_pdu->row == 14 || csi_pdu->row == 16 || csi_pdu->row == 17) {
      num_symb += 2;
      for (int s = 0; s < 2; s++) { // two consecutive symbols including l1
        int l1_symb = csi_pdu->symb_l1 + s;
        if (l1_symb >= dlsch_config->start_symbol && l1_symb < dlsch_config->start_symbol + dlsch_config->number_symbols)
          num_overlap_symb++;
      }
    }
    if (num_overlap_symb == 0)
      continue;
    // check number overlapping prbs
    // assuming CSI is spanning the whole BW
    AssertFatal(dlsch_config->BWPSize <= csi_pdu->nr_of_rbs, "Assuming CSI-RS is spanning the whold BWP this shouldn't happen\n");
    int num_overlapping_prbs = 0;
    for (int rb = freq_alloc->first_rb; rb <= freq_alloc->last_rb; rb++) {
      if (!check_rb_in_bitmap(freq_alloc, rb))
        continue;
      if (csi_pdu->freq_density < 2) {
        int abs_rb = rb + dlsch_config->BWPStart;
        if ((abs_rb % 2) == csi_pdu->freq_density)
          num_overlapping_prbs++;
      } else {
        num_overlapping_prbs++;
      }
    }
    // density is number or res per port per rb (over all symbols)
    int ports [18] = {1, 1, 2, 4, 4, 8, 8, 8, 12, 12, 16, 16, 24, 24, 24, 32, 32, 32};
    int num_csi_res_per_prb = csi_pdu->freq_density == 3 ? 3 : 1;
    num_csi_res_per_prb *= ports[csi_pdu->row - 1];
    unav_res += num_overlapping_prbs * num_csi_res_per_prb * num_overlap_symb / num_symb;
  }
  return unav_res;
}

/*! \brief Process the whole DLSCH slot
 */
static void nr_ue_dlsch_procedures(PHY_VARS_NR_UE *ue,
                                   const UE_nr_rxtx_proc_t *proc,
                                   NR_UE_DLSCH_t *dlsch,
                                   int cw_idx,
                                   int G,
                                   freq_alloc_bitmap_t *freq_alloc,
                                   fapi_nr_dl_config_dlsch_pdu_rel15_t *config,
                                   int16_t *llr)
{
  if (dlsch->active == false) {
    LOG_E(PHY, "DLSCH should be active when calling this function\n");
    return;
  }

  int harq_pid = config->harq_process_nbr;
  int frame_rx = proc->frame_rx;
  int nr_slot_rx = proc->nr_slot_rx;

  LOG_D(PHY, "AbsSubframe %d.%d Start LDPC Decoder for CW%d [harq_pid %d]\n", frame_rx % 1024, nr_slot_rx, cw_idx, harq_pid);

  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  // exit dlsch procedures as there are no active dlsch
  NR_DL_UE_HARQ_t *dl_harq = &ue->dl_harq_processes[cw_idx][harq_pid];
  if (dl_harq->status != NR_ACTIVE) {
    /* Grant arrived (dlsch active, LLRs demodulated) but the HARQ process is no longer ACTIVE. */
    LOG_E(NR_PHY, "Internal error nr_ue_dlsch_procedure() no active cw slot %d harq %d (now %d.%d status %d activated %d.%d)\n",
          nr_slot_rx, harq_pid, frame_rx, nr_slot_rx, dl_harq->status, dl_harq->activated_frame, dl_harq->activated_slot);
    if (config->k1_feedback) {
      const int ack_nack_slot_and_frame = (proc->nr_slot_rx + config->k1_feedback) + proc->frame_rx * fp->slots_per_frame;
      dynamic_barrier_join(&ue->process_slot_tx_barriers[ack_nack_slot_and_frame % NUM_PROCESS_SLOT_TX_BARRIERS]);
    }
    return;
  }

  /* Late finish of a TB that no longer owns the PID: do not decode, ACK, or write HARQ state. */
  if (dl_harq->activated_frame != frame_rx || dl_harq->activated_slot != nr_slot_rx) {
    LOG_W(NR_PHY, "%d.%d DLSCH harq %d late decode skipped, re-armed %d.%d first_rx %d\n",
          frame_rx, nr_slot_rx, harq_pid, dl_harq->activated_frame, dl_harq->activated_slot, dl_harq->first_rx);
    if (config->k1_feedback) {
      const int ack_nack_slot_and_frame = (proc->nr_slot_rx + config->k1_feedback) + proc->frame_rx * fp->slots_per_frame;
      dynamic_barrier_join(&ue->process_slot_tx_barriers[ack_nack_slot_and_frame % NUM_PROCESS_SLOT_TX_BARRIERS]);
    }
    return;
  }

  start_meas_nr_ue_phy(ue, DLSCH_UNSCRAMBLING_STATS);
  nr_dlsch_unscrambling(llr, G, 0, config->dlDataScramblingId, dlsch->rnti);
  stop_meas_nr_ue_phy(ue, DLSCH_UNSCRAMBLING_STATS);

  start_meas_nr_ue_phy(ue, DLSCH_DECODING_STATS);
  uint8_t output[lenWithCrc(1, dlsch->cw_info.TBS) / 8];
  nr_dlsch_decoding(ue, proc, dlsch, cw_idx, config, llr, output, freq_alloc->num_rbs, G);
  stop_meas_nr_ue_phy(ue, DLSCH_DECODING_STATS);

  int ind_type = -1;
  switch (dlsch->rnti_type) {
    case TYPE_RA_RNTI_:
      ind_type = FAPI_NR_RX_PDU_TYPE_RAR;
      break;
    case TYPE_SI_RNTI_:
      ind_type = FAPI_NR_RX_PDU_TYPE_SIB;
      break;
    case TYPE_C_RNTI_:
      ind_type = FAPI_NR_RX_PDU_TYPE_DLSCH;
      break;
    case TYPE_P_RNTI_:
      ind_type = FAPI_NR_RX_PDU_TYPE_PCCH;
      break;
    default:
      AssertFatal(false, "Invalid DLSCH type %d\n", dlsch->rnti_type);
      break;
  }

  LOG_D(PHY, "DL PDU length in bits: %d, in bytes: %d \n", dlsch->cw_info.TBS, dlsch->cw_info.TBS / 8);
  if (cpumeas(CPUMEAS_GETSTATE)) {
    LOG_D(PHY,
          " --> Unscrambling %5.3f\n",
          ue->phy_cpu_stats.cpu_time_stats[DLSCH_UNSCRAMBLING_STATS].p_time / (1000.0));
    LOG_D(PHY,
          "AbsSubframe %d.%d --> LDPC Decoding %5.3f\n",
          frame_rx % 1024,
          nr_slot_rx,
          ue->phy_cpu_stats.cpu_time_stats[DLSCH_DECODING_STATS].p_time / (1000.0));
  }

  // send to mac
  if (ue->if_inst && ue->if_inst->dl_indication) {
    fapi_nr_rx_indication_t rx_ind;
    rx_ind.number_pdus = 0;
    nr_fill_rx_indication(&rx_ind, ind_type, ue, cw_idx, harq_pid, dlsch, proc, output);
    nr_downlink_indication_t dl_indication = (nr_downlink_indication_t){
        .gNB_index = proc->gNB_id,
        .module_id = ue->Mod_id,
        .cc_id = ue->CC_id,
        .hfn = proc->hfn_rx,
        .frame = proc->frame_rx,
        .slot = proc->nr_slot_rx,
        .rx_ind = &rx_ind,
    };
    ue->if_inst->dl_indication(&dl_indication);
  }

  // DLSCH decoding finished! don't wait anymore in Tx process, we know if we should answer ACK/NACK PUCCH
  if ((dlsch->rnti_type == TYPE_C_RNTI_ || dlsch->rnti_type == TYPE_RA_RNTI_) && config->k1_feedback) {
    const int ack_nack_slot_and_frame = (proc->nr_slot_rx + config->k1_feedback) + proc->frame_rx * fp->slots_per_frame;
    dynamic_barrier_join(&ue->process_slot_tx_barriers[ack_nack_slot_and_frame % NUM_PROCESS_SLOT_TX_BARRIERS]);
  }

  int a_segments = MAX_NUM_NR_DLSCH_SEGMENTS; // number of segments to be allocated
  if (freq_alloc->num_rbs != 273) {
    a_segments = a_segments * freq_alloc->num_rbs;
    a_segments = (a_segments / 273) + 1;
  }

  if (ue->phy_sim_dlsch_b)
    memcpy(ue->phy_sim_dlsch_b, output, sizeof(output));
}

static bool check_neighboring_cells_task(PHY_VARS_NR_UE *ue, bool task_pending)
{
  if (task_pending == true) {
    return false;
  }

  // Check if any intra-frequency neighbor cells are configured
  uint32_t serving_ssb_freq = get_ssb_arfcn(&ue->frame_parms);
  bool has_intra_freq_neighbors = false;

  for (int cell_idx = 0; cell_idx < NUMBER_OF_NEIGHBORING_CELLS_MAX; cell_idx++) {
    fapi_nr_neighboring_cell_t *nr_neighboring_cell = &ue->nrUE_config.meas_config.nr_neighboring_cell[cell_idx];
    if (nr_neighboring_cell->active == 1) {
      if (nr_neighboring_cell->ssb_freq == 0 || nr_neighboring_cell->ssb_freq == serving_ssb_freq) {
        has_intra_freq_neighbors = true;
      }
    }
  }

  return has_intra_freq_neighbors;
}

static bool is_ssb_index_transmitted(const PHY_VARS_NR_UE *ue, const int index)
{
  if (ue->received_config_request) {
    const fapi_nr_config_request_t *cfg = &ue->nrUE_config;
    const uint32_t curr_mask = cfg->ssb_table.ssb_mask_list[index / 32].ssb_mask;
    return ((curr_mask >> (31 - (index % 32))) & 0x01);
  } else
    return ue->frame_parms.ssb_index == index;
}

static int get_pdcch_max_rbs(NR_UE_PDCCH_CONFIG *phy_pdcch_config)
{
  int nb_rb = 0;
  int rb_offset = 0;
  for (int i = 0; i < phy_pdcch_config->nb_search_space; i++) {
    int tmp = 0;
    get_coreset_rballoc(phy_pdcch_config->pdcch_config[i].coreset.frequency_domain_resource, &tmp, &rb_offset);
    if (tmp > nb_rb)
      nb_rb = tmp;
  }
  return nb_rb;
}

static int get_max_pdcch_symb(const NR_UE_PDCCH_CONFIG *phy_pdcch_config)
{
  int max_pdcch_symb = 0;
  for (int i = 0; i < phy_pdcch_config->nb_search_space; i++)
    if (phy_pdcch_config->pdcch_config[i].coreset.duration > max_pdcch_symb)
      max_pdcch_symb = phy_pdcch_config->pdcch_config[i].coreset.duration;

  return max_pdcch_symb;
}

void pdcch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data)
{
  NR_UE_PDCCH_CONFIG *phy_pdcch_config = &phy_data->phy_pdcch_config;
  if (phy_pdcch_config->nb_search_space == 0)
    return;

  TracyCZone(ctx, true);
  /* process PDCCH */
  LOG_D(PHY, " ------ --> PDCCH ChannelComp/LLR Frame.slot %d.%d ------  \n", proc->frame_rx % 1024, proc->nr_slot_rx);
  start_meas_nr_ue_phy(ue, DLSCH_RX_PDCCH_STATS);
  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  int num_monitoring_occ = get_max_pdcch_monOcc(phy_pdcch_config, fp->symbols_per_slot);
  int max_nb_symb_pdcch = get_max_pdcch_symb(phy_pdcch_config);
  int llr_size_symbol = get_pdcch_max_rbs(phy_pdcch_config) * 9;
  c16_t pdcch_llr[phy_pdcch_config->nb_search_space][num_monitoring_occ][max_nb_symb_pdcch * llr_size_symbol];
  int start_symb_pdcch, last_symb_pdcch;
  set_first_last_pdcch_symb(phy_pdcch_config, fp->symbols_per_slot, &start_symb_pdcch, &last_symb_pdcch);

  /* Temporarily loop over symbols in the slot, perform OFDM demod and process PDCCH.
     When symbol based proc design is fully merged, this function will be called to process only one symbol
     and OFDM demod will be removed from here. */
  const uint32_t rxdataF_sz = fp->samples_per_slot_wCP;
  __attribute__((aligned(32))) c16_t rxdataF[fp->nb_antennas_rx][rxdataF_sz];

  for (int symbol = start_symb_pdcch; symbol <= last_symb_pdcch; symbol++) {
    nr_slot_fep(ue, fp, proc->nr_slot_rx, symbol, rxdataF, link_type_dl, 0, ue->common_vars.rxdata);
    __attribute__((aligned(32))) c16_t rxdataF_symb[fp->nb_antennas_rx][((fp->ofdm_symbol_size + 7) / 8) * 8];

    for (int ant = 0; ant < fp->nb_antennas_rx; ant++)
      memcpy(rxdataF_symb[ant], &rxdataF[ant][symbol * fp->ofdm_symbol_size], sizeof(c16_t) * fp->ofdm_symbol_size);

    nr_pdcch_generate_llr(ue, proc, symbol, phy_data, llr_size_symbol, num_monitoring_occ, max_nb_symb_pdcch, rxdataF_symb, pdcch_llr);
    if (symbol == last_symb_pdcch) {
      nr_pdcch_dci_indication(proc, llr_size_symbol * max_nb_symb_pdcch, num_monitoring_occ, ue, phy_data, pdcch_llr);
      UEscopeCopy(ue, pdcchLlr, pdcch_llr, sizeof(c16_t), 1, sizeof(pdcch_llr) / sizeof(c16_t), 0);
    }
  }
  stop_meas_nr_ue_phy(ue, DLSCH_RX_PDCCH_STATS);
  TracyCZoneEnd(ctx);
  if (ue->phy_sim_rxdataF)
    memcpy(ue->phy_sim_rxdataF, rxdataF[0], sizeof(int32_t) * max_nb_symb_pdcch * fp->ofdm_symbol_size);
}

int is_ssb_in_symbol(const PHY_VARS_NR_UE *ue,
                     const int symbIdxInFrame,
                     const int slot,
                     const int ssbMask,
                     const int ssbIndex,
                     const int ssb_period)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  // Skip if current SSB index is not transmitted
  if (!is_ssb_index_transmitted(ue, ssbIndex)) {
    return false;
  }

  const int startPbchSymb = nr_get_ssb_start_symbol(fp, ssbIndex) + 1;
  const int startPbchSymbHf = (ssb_period == 0) ? (startPbchSymb + (fp->slots_per_frame * NR_SYMBOLS_PER_SLOT / 2))
                                                : (fp->slots_per_frame * NR_SYMBOLS_PER_SLOT);

  // Skip if no SSB in current symbol
  if ((symbIdxInFrame >= startPbchSymb && symbIdxInFrame < (startPbchSymb + NB_SYMBOLS_PBCH))
      || (symbIdxInFrame >= startPbchSymbHf && symbIdxInFrame < (startPbchSymbHf + NB_SYMBOLS_PBCH))) {
    return true;
  }

  return false;
}

int get_ssb_index_in_symbol(const PHY_VARS_NR_UE *ue, const int symbIdxInFrame, const int slot, const int frame)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  const fapi_nr_config_request_t *cfg = &ue->nrUE_config;
  // Checking if current frame is compatible with SSB periodicity
  const int default_ssb_period = 2;
  const int ssb_period = ue->received_config_request ? cfg->ssb_table.ssb_period : default_ssb_period;
  if (ssb_period != 0 && (frame % (1 << (ssb_period - 1)))) {
    return -1;
  }

  // Find the SSB index corresponding to current symbol
  for (int ssbIndex = 0; ssbIndex < fp->Lmax; ssbIndex++) {
    const int ssbMask = cfg->ssb_table.ssb_mask_list[ssbIndex / 32].ssb_mask;
    if (is_ssb_in_symbol(ue, symbIdxInFrame, slot, ssbMask, ssbIndex, ssb_period))
      return ssbIndex;
  }

  return -1;
}

/* Description: Generates PBCH LLRs from frequency domain signal for one OFDM symbol.
                Generates PBCH time domain channel response.
   Returns    : SSB index if symbol contains SSB. Else returns -1. */
int nr_process_pbch_symbol(PHY_VARS_NR_UE *ue,
                           const UE_nr_rxtx_proc_t *proc,
                           const int symbol,
                           const int ssbIndexIn,
                           c16_t dl_ch_estimates_time[ue->frame_parms.nb_antennas_rx][ue->frame_parms.ofdm_symbol_size],
                           c16_t *dl_ch_estimates_symbol,
                           int16_t pbch_e_rx[NR_POLAR_PBCH_E],
                           uint8_t *log2_maxh)
{
  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  const int symbIdxInFrame = symbol + NR_SYMBOLS_PER_SLOT * proc->nr_slot_rx;

  // Search for SSB index if given SSB index is invalid
  const int ssbIndex =
      (ssbIndexIn < 0) ? get_ssb_index_in_symbol(ue, symbIdxInFrame, proc->nr_slot_rx, proc->frame_rx) : ssbIndexIn;

  if (ssbIndex < 0)
    return -1;

  LOG_D(PHY, "Frame %d, Slot %d, Symbol %d, SSB Index %d\n", proc->frame_rx, proc->nr_slot_rx, symbol, ssbIndex);
  const int startPbchSymb = nr_get_ssb_start_symbol(fp, ssbIndex) + 1;
  const int startPbchSymbHf = startPbchSymb + (fp->slots_per_frame * NR_SYMBOLS_PER_SLOT / 2);

  // Found PBCH. Process it
  __attribute__((aligned(32))) c16_t rxdataF[fp->nb_antennas_rx][fp->ofdm_symbol_size];
  {
    __attribute__((aligned(32))) c16_t tmp[fp->nb_antennas_rx][fp->samples_per_slot_wCP];
    nr_slot_fep(ue, fp, proc->nr_slot_rx, symbol, tmp, link_type_dl, 0, ue->common_vars.rxdata);
    for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
      memcpy(rxdataF[aarx], tmp[aarx] + symbol * fp->ofdm_symbol_size, sizeof(c16_t) * fp->ofdm_symbol_size);
    }
  }
  c16_t dl_ch_estimates[fp->nb_antennas_rx][fp->ofdm_symbol_size];

  const int relPbchSymb = (symbIdxInFrame > (fp->slots_per_frame * NR_SYMBOLS_PER_SLOT / 2)) ? (symbIdxInFrame - startPbchSymbHf)
                                                                                             : (symbIdxInFrame - startPbchSymb);

  const int nid = fp->Nid_cell;
  const int ssb_start_subcarrier = fp->ssb_start_subcarrier;
  for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
    nr_pbch_channel_estimation(&ue->frame_parms,
                               NULL,
                               dl_ch_estimates[aarx],
                               proc,
                               relPbchSymb,
                               ssbIndex & 7,
                               symbIdxInFrame > (fp->slots_per_frame * NR_SYMBOLS_PER_SLOT / 2),
                               ssb_start_subcarrier,
                               rxdataF[aarx],
                               false,
                               nid);
    // Get channel response to measure timing error
    if ((fp->ssb_index == ssbIndex) && (relPbchSymb == NB_SYMBOLS_PBCH - 1)) {
      // do ifft of channel estimate
      freq2time(fp->ofdm_symbol_size, (int16_t *)&dl_ch_estimates[aarx], (int16_t *)dl_ch_estimates_time[aarx]);
      UEscopeCopy(ue, pbchDlChEstimateTime, (void *)dl_ch_estimates_time, sizeof(c16_t), fp->nb_antennas_rx, fp->ofdm_symbol_size, 0);
    }
  }

  // Copy current symbol estimate for FO estimation
  if (dl_ch_estimates_symbol != NULL)
    memcpy(dl_ch_estimates_symbol, dl_ch_estimates[0], sizeof(*dl_ch_estimates_symbol) * NR_PBCH_NUM_RB * NR_NB_SC_PER_RB);

  const int symbIdxInSSB = relPbchSymb + 1;
  nr_generate_pbch_llr(ue,
                       proc,
                       fp,
                       symbIdxInSSB,
                       ssbIndex,
                       nid,
                       ssb_start_subcarrier,
                       rxdataF,
                       dl_ch_estimates,
                       pbch_e_rx,
                       log2_maxh);
  // Do measurements on middle symbol of PBCH block
  if (relPbchSymb == 1) {
    nr_ue_ssb_rsrp_measurements(ue, ssbIndex, proc, rxdataF);
    nr_ue_rrc_measurements(ue, proc, rxdataF);
    // resetting ssb index for PBCH detection if there is a stronger SSB index
    if (ue->measurements.ssb_rsrp_dBm[ssbIndex] > ue->measurements.ssb_rsrp_dBm[fp->ssb_index]) {
      fp->ssb_index = ssbIndex;
    }
  }

  return ssbIndex;
}

static int pbch_process(PHY_VARS_NR_UE *UE,
                        const UE_nr_rxtx_proc_t *proc,
                        const int symbol,
                        int *ssbIndex,
                        c16_t pbch_ch_est_sym1[NR_PBCH_NUM_RB * NR_NB_SC_PER_RB],
                        c16_t pbch_ch_est_time[UE->frame_parms.nb_antennas_rx][UE->frame_parms.ofdm_symbol_size],
                        int16_t pbch_e_rx[NR_POLAR_PBCH_E],
                        int *pbchSymbCnt,
                        uint8_t *log2_maxh)
{
  int sampleShift = INT_MAX;

  // Buffer to hold symbol 3 estimates for FO estimation
  c16_t pbch_ch_est_sym3[NR_PBCH_NUM_RB * NR_NB_SC_PER_RB];
  // Choose estimates buffer for FO compensation based on current PBCH symbol
  c16_t *cur_pbch_est = NULL;
  if (*pbchSymbCnt == 0)
    cur_pbch_est = pbch_ch_est_sym1;
  else if (*pbchSymbCnt == 2)
    cur_pbch_est = pbch_ch_est_sym3;

  *ssbIndex = nr_process_pbch_symbol(UE, proc, symbol, *ssbIndex, pbch_ch_est_time, cur_pbch_est, pbch_e_rx, log2_maxh);
  // If valid PBCH symbol, increment symbol count.
  if (*ssbIndex > -1)
    (*pbchSymbCnt)++;
  // Current symbol is last PBCH symbol, decode it.
  if (*pbchSymbCnt == 3) {
    if (*ssbIndex == UE->frame_parms.ssb_index) {
      fapiPbch_t pbchResult; // TODO: Not used anywhere. To be cleaned later
      int hfb, ssb_idx, symb_offset = 0;
      const int nid = UE->frame_parms.Nid_cell;
      const int pbchSuccess =
          nr_pbch_decode(UE, &UE->frame_parms, proc, *ssbIndex, nid, pbch_e_rx, &hfb, &ssb_idx, &symb_offset, &pbchResult);
      if (pbchSuccess != 0)
        LOG_E(PHY, "Frame %d, slot %d, SSB Index %d. Error decoding PBCH!\n", proc->frame_rx, proc->nr_slot_rx, *ssbIndex);
      else
        T(T_NRUE_PHY_MIB, T_INT(proc->frame_rx), T_INT(proc->nr_slot_rx), T_INT(*ssbIndex), T_BUFFER(pbchResult.decoded_output, 3));
      // Measure timing offset if PBCH is present in slot
      if (UE->no_timing_correction == 0 && pbchSuccess == 0) {
        sampleShift = nr_adjust_synch_ue(&UE->frame_parms, UE, pbch_ch_est_time, proc->frame_rx, proc->nr_slot_rx, 16384);
      }

      // Continuous FO estimation and compensation
      if (get_nrUE_params()->cont_fo_comp && pbchSuccess == 0) {
        double freq_offset = nr_ue_pbch_freq_offset(&UE->frame_parms, pbch_ch_est_sym1, pbch_ch_est_sym3);
        LOG_D(PHY,
              "compensated freq offset = %.3f Hz, detected residual freq offset = %.3f Hz, accumulated freq offset = %.3f Hz\n",
              UE->freq_offset,
              freq_offset,
              UE->freq_off_acc);

        // PI controller
        const double PID_P = get_nrUE_params()->freq_sync_P;
        const double PID_I = get_nrUE_params()->freq_sync_I;
        UE->freq_offset += freq_offset * PID_P + UE->freq_off_acc * PID_I;
        UE->freq_off_acc += freq_offset;
      }
    }
    *pbchSymbCnt = 0; // For next SSB index
    *ssbIndex = -1;
  }
  return sampleShift;
}

static nr_meas_task_args_t *create_meas_task_args(const UE_nr_rxtx_proc_t *proc, PHY_VARS_NR_UE *ue)
{
  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  // Extra headroom so that nr_slot_fep() can read all NR_N_SYMBOLS_SSB symbols
  // when the PSS is detected at the very end of the samples_per_slot_wCP search window
  uint32_t rxdata_size = fp->samples_per_slot_wCP
                       + NR_N_SYMBOLS_SSB * (fp->ofdm_symbol_size + fp->nb_prefix_samples);
  size_t total_size = sizeof(nr_meas_task_args_t) + fp->nb_antennas_rx * rxdata_size * sizeof(c16_t);
  nr_meas_task_args_t *args = malloc_or_fail(total_size);
  args->proc = *proc;
  args->ue = ue;
  args->rxdata_size = rxdata_size;
  args->nb_ant = fp->nb_antennas_rx;
  uint32_t slot_offset = get_samples_slot_timestamp(fp, proc->nr_slot_rx);
  for (int i = 0; i < fp->nb_antennas_rx; i++)
    memcpy(args->rxdata_ant + i * rxdata_size, &ue->common_vars.rxdata[i][slot_offset], rxdata_size * sizeof(c16_t));
  return args;
}

int pbch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data)
{
  TracyCZone(ctx, true);
  int frame_rx = proc->frame_rx;
  int nr_slot_rx = proc->nr_slot_rx;
  NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  int sampleShift = INT_MAX;
  nr_ue_dlsch_init(phy_data->dlsch, NR_MAX_NB_LAYERS > 4 ? 2 : 1, ue->max_ldpc_iterations);

#if T_TRACER
  T(T_UE_PHY_DL_TICK, T_INT(ue->Mod_id), T_INT(frame_rx % 1024), T_INT(nr_slot_rx));
#endif

  LOG_D(PHY," ****** start RX-Chain for Frame.Slot %d.%d ******  \n",
        frame_rx%1024, nr_slot_rx);

  {
    int pbchSymbCnt = 0;
    __attribute__((aligned(32))) c16_t pbch_ch_est_time[ue->frame_parms.nb_antennas_rx][ue->frame_parms.ofdm_symbol_size];
    int16_t pbch_e_rx[NR_POLAR_PBCH_E];
    // Buffer to hold estimates of symbol 1 for FO compensation in symbol 3
    c16_t pbch_ch_est_sym1[NR_PBCH_NUM_RB * NR_NB_SC_PER_RB];

    int ssbIndex = -1;
    uint8_t log2_maxh = 0;
    // TODO: Remove loopover symbols when symbol based receiver is fully integrated.
    for (int symbol = 0; symbol < fp->symbols_per_slot; symbol++) {
      const int pbch_sampleShift =
          pbch_process(ue, proc, symbol, &ssbIndex, pbch_ch_est_sym1, pbch_ch_est_time, pbch_e_rx, &pbchSymbCnt, &log2_maxh);
      // To prevent overwrite estimated shift by consecutive symbol calls
      sampleShift = (sampleShift == INT_MAX) ? pbch_sampleShift : sampleShift;
    }
  }

  // Check for PRS slot - section 7.4.1.7.4 in 3GPP rel16 38.211
  for(int gNB_id = 0; gNB_id < ue->prs_active_gNBs; gNB_id++)
  {
    __attribute__((aligned(32))) c16_t rxdataF[ue->frame_parms.nb_antennas_rx][ue->frame_parms.samples_per_slot_wCP];
    for(int rsc_id = 0; rsc_id < ue->prs_vars[gNB_id]->NumPRSResources; rsc_id++)
    {
      prs_config_t *prs_config = &ue->prs_vars[gNB_id]->prs_resource[rsc_id].prs_cfg;
      for (int i = 0; i < prs_config->PRSResourceRepetition; i++)
      {
        if( (((frame_rx*fp->slots_per_frame + nr_slot_rx) - (prs_config->PRSResourceSetPeriod[1] + prs_config->PRSResourceOffset) + prs_config->PRSResourceSetPeriod[0])%prs_config->PRSResourceSetPeriod[0]) == i*prs_config->PRSResourceTimeGap)
        {
          for(int j = prs_config->SymbolStart; j < (prs_config->SymbolStart+prs_config->NumPRSSymbols); j++)
          {
            nr_slot_fep(ue, fp, proc->nr_slot_rx, (j % fp->symbols_per_slot), rxdataF, link_type_dl, 0, ue->common_vars.rxdata);
          }
          nr_prs_channel_estimation(gNB_id, rsc_id, i, ue, proc, rxdataF);
        }
      } // for i
    } // for rsc_id
  } // for gNB_id

  PHY_NR_MEASUREMENTS *measurements = &ue->measurements;

  // Measurements on known neighboring cells, at most once per measurement period: TS 38.133 9.2.5.1, the intra-frequency
  // SSB measurement period is at least 200 ms
  const int spf = ue->frame_parms.slots_per_frame;
  const int cycle = 1024 * spf;
  const int now = proc->frame_rx * spf + proc->nr_slot_rx;
  const bool meas_period_elapsed =
      measurements->last_meas_slot < 0 || (now - measurements->last_meas_slot + cycle) % cycle >= NR_UE_MEAS_PERIOD_MS * spf / 10;
  if (meas_period_elapsed && check_neighboring_cells_task(ue, measurements->meas_request_pending)) {
    measurements->meas_request_pending = true;
    measurements->last_meas_slot = now;
    nr_meas_task_args_t *args = create_meas_task_args(proc, ue);
    task_t t = {.func = nr_ue_meas_neighboring_cell, .args = args};
    pushTpool(&get_nrUE_params()->Tpool, t);
  }

  // Search for unknown neighboring cells
  if (!ue->disable_blind_search && proc->frame_rx % 256 == 0 && proc->nr_slot_rx == 0
      && measurements->last_blind_slot == measurements->last_slot)
    measurements->last_blind_slot = -1;
  uint16_t slots_per_frame = ue->frame_parms.slots_per_frame;
  bool is_meas_slot = proc->frame_rx % 256 == 0 && proc->nr_slot_rx >= CIRCULAR_INC(measurements->last_blind_slot, 1, slots_per_frame);
  if (!ue->disable_blind_search && is_meas_slot && check_neighboring_cells_task(ue, measurements->search_new_cells_pending)) {
    measurements->search_new_cells_pending = true;
    measurements->last_blind_slot = proc->nr_slot_rx;
    nr_meas_task_args_t *args = create_meas_task_args(proc, ue);
    task_t t = {.func = nr_ue_search_new_neighboring_cell, .args = args};
    pushTpool(&get_nrUE_params()->Tpool, t);
  }
  measurements->last_slot = proc->nr_slot_rx;

  TracyCZoneEnd(ctx);
  return sampleShift;
}

void pdsch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data)
{
  int frame_rx = proc->frame_rx;
  int nr_slot_rx = proc->nr_slot_rx;
  int gNB_id = proc->gNB_id;

  // do procedures for C-RNTI

  bool slot_fep_map[14] = {0};
  const uint32_t rxdataF_sz = ue->frame_parms.samples_per_slot_wCP;
  __attribute__ ((aligned(32))) c16_t rxdataF[ue->frame_parms.nb_antennas_rx][rxdataF_sz];

  // do procedures for CSI-IM
  if (phy_data->csiim_vars.active == 1) {
    for(int symb_idx = 0; symb_idx < 4; symb_idx++) {
      int symb = phy_data->csiim_vars.csiim_config_pdu.l_csiim[symb_idx];
      if (!slot_fep_map[symb]) {
        nr_slot_fep(ue, &ue->frame_parms, proc->nr_slot_rx, symb, rxdataF, link_type_dl, 0, ue->common_vars.rxdata);
        slot_fep_map[symb] = true;
      }
    }
    nr_ue_csi_im_procedures(ue, rxdataF, &phy_data->csiim_vars.csiim_config_pdu);
  }

  // do procedures for CSI-RS
  {
    /*
    CSI-RS for tracking use only one port.
    Number of CSI-RS resources for tracking is always 2 per slot.
    Computed estimates from first resource is saved and used while estimating second resource.
    */
    c16_t trs_estimates[ue->frame_parms.nb_antennas_rx][1][ue->frame_parms.ofdm_symbol_size];
    for (int res = 0; res < MAX_CSI_RES_SLOT; res++) {
      if (phy_data->csirs_vars[res].active == 1) {
        for (int symb = 0; symb < ue->frame_parms.symbols_per_slot; symb++) {
          if (is_csi_rs_in_symbol(phy_data->csirs_vars[res].csirs_config_pdu, symb)) {
            if (!slot_fep_map[symb]) {
              nr_slot_fep(ue, &ue->frame_parms, proc->nr_slot_rx, symb, rxdataF, link_type_dl, 0, ue->common_vars.rxdata);
              slot_fep_map[symb] = true;
            }
          }
        }
        if (res > 0 && phy_data->csirs_vars[res].csirs_config_pdu.csi_type == 0) // tracking CSI
          AssertFatal(phy_data->csirs_vars[res - 1].active && (phy_data->csirs_vars[res - 1].csirs_config_pdu.csi_type == 0),
                      "CSI-RS for tracking must have two consecutive active resources\n");
        nr_ue_csi_rs_procedures(ue,
                                proc,
                                rxdataF,
                                &phy_data->csirs_vars[res].csirs_config_pdu,
                                trs_estimates,
                                res,
                                (res == 1) ? phy_data->csirs_vars[0].csirs_config_pdu.symb_l0 : -1);
      }
    }
  }

  const int actor_idx_llr = proc->nr_slot_rx % ue->pdsch_num_actors;
  int16_t **llr = ue->pdsch_scratch[actor_idx_llr].llr;
  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config = &phy_data->dlsch_config;
  for (int c = 0; c < phy_data->n_dlsch_codewords; c++) {
    NR_UE_DLSCH_t *dlsch = &phy_data->dlsch[c];
    if (!dlsch->active)
      continue;

    uint16_t nb_symb_sch = dlsch_config->number_symbols;
    uint16_t start_symb_sch = dlsch_config->start_symbol;

    LOG_D(PHY," ------ --> PDSCH ChannelComp/LLR Frame.slot %d.%d ------  \n", frame_rx % 1024, nr_slot_rx);

    for (int m = start_symb_sch; m < (nb_symb_sch + start_symb_sch) ; m++) {
      if (!slot_fep_map[m]) {
        nr_slot_fep(ue, &ue->frame_parms, proc->nr_slot_rx, m, rxdataF, link_type_dl, 0, ue->common_vars.rxdata);
        slot_fep_map[m] = true;
      }
    }

    freq_alloc_bitmap_t freq_alloc = {0};
    if (dlsch_config->resource_alloc == 0) {
      int alloc_size = (dlsch_config->BWPSize / 8) + (dlsch_config->BWPSize % 8 > 0);
      freq_alloc = set_start_end_from_bitmap(dlsch_config->BWPSize, alloc_size, dlsch_config->rb_bitmap);
    } else
      freq_alloc = set_bitmap_from_start_size(dlsch_config->start_rb, dlsch_config->number_rbs);

    const uint8_t nb_re_dmrs = get_num_dmrs_re_per_rb(dlsch_config->dmrsConfigType, dlsch_config->n_dmrs_cdm_groups);
    uint16_t dmrs_len = get_num_dmrs(dlsch_config->dlDmrsSymbPos);
    uint32_t unav_res = 0;
    if(dlsch_config->pduBitmap & 0x1) {
      uint16_t ptrsSymbPos = get_ptrs_symb_idx(dlsch_config->number_symbols,
                                               dlsch_config->start_symbol,
                                               1 << dlsch_config->PTRSTimeDensity,
                                               dlsch_config->dlDmrsSymbPos);
      int n_ptrs = (freq_alloc.num_rbs + dlsch_config->PTRSFreqDensity - 1) / dlsch_config->PTRSFreqDensity;
      int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ptrsSymbPos, dlsch_config->start_symbol, dlsch_config->number_symbols);
      unav_res = n_ptrs * ptrsSymbPerSlot;
    }
    unav_res += compute_csi_rm_unav_res(dlsch_config, &freq_alloc);
    int G = nr_get_G(freq_alloc.num_rbs,
                     dlsch_config->number_symbols,
                     nb_re_dmrs,
                     dmrs_len,
                     unav_res,
                     dlsch->cw_info.qamModOrder,
                     dlsch->cw_info.Nl);
    const uint32_t rx_llr_buf_sz = ALIGNARRAYSIZE(G, 32); // each LLR is 2 bytes hence 64 byte aligned

    // dlsch_harq contains the previous transmissions data for this harq pid
    NR_DL_UE_HARQ_t *harq = &ue->dl_harq_processes[c][dlsch_config->harq_process_nbr];
    // it returns -1 in case of internal failure, or 0 in case of normal result
    int ret_pdsch = nr_ue_pdsch_procedures(ue, proc, dlsch, harq, dlsch_config, llr[c], rxdataF, &freq_alloc);
    TracyCPlot("pdsch mcs", dlsch->cw_info.mcs);

    UEscopeCopy(ue, pdschLlr, llr[c], sizeof(int16_t), 1, G, 0);

    LOG_D(PHY, "DLSCH data reception at nr_slot_rx: %d\n", nr_slot_rx);
    start_meas_nr_ue_phy(ue, DLSCH_PROCEDURES_STATS);

    if (ret_pdsch >= 0) {
      nr_ue_dlsch_procedures(ue, proc, dlsch, c, G, &freq_alloc, dlsch_config, llr[c]);
    } else {
      LOG_E(NR_PHY, "Demodulation impossible, internal error\n");
      if (dlsch_config->k1_feedback) {
        const int ack_nack_slot_and_frame =
            proc->nr_slot_rx + dlsch_config->k1_feedback + proc->frame_rx * ue->frame_parms.slots_per_frame;
        dynamic_barrier_join(&ue->process_slot_tx_barriers[ack_nack_slot_and_frame % NUM_PROCESS_SLOT_TX_BARRIERS]);
      }
      LOG_W(NR_PHY, "nr_ue_pdsch_procedures failed in slot %d\n", proc->nr_slot_rx);
    }

    stop_meas_nr_ue_phy(ue, DLSCH_PROCEDURES_STATS);
    if (cpumeas(CPUMEAS_GETSTATE)) {
      LOG_D(PHY, "[SFN %d] Slot0 Slot1: Dlsch Proc %5.2f\n",nr_slot_rx,ue->phy_cpu_stats.cpu_time_stats[DLSCH_PROCEDURES_STATS].p_time/(1000.0));
    }

    if (ue->phy_sim_rxdataF)
      memcpy(ue->phy_sim_rxdataF + start_symb_sch * ue->frame_parms.ofdm_symbol_size * sizeof(c16_t),
             &rxdataF[0][start_symb_sch * ue->frame_parms.ofdm_symbol_size],
             sizeof(int32_t) * nb_symb_sch * ue->frame_parms.ofdm_symbol_size);
    if (ue->phy_sim_pdsch_llr)
      memcpy(ue->phy_sim_pdsch_llr, llr[c], sizeof(int16_t) * rx_llr_buf_sz);

  }

  if (nr_slot_rx==9) {
    if (frame_rx % 10 == 0) {
      if ((ue->dlsch_received[gNB_id] - ue->dlsch_received_last[gNB_id]) != 0)
        ue->dlsch_fer[gNB_id] = (100*(ue->dlsch_errors[gNB_id] - ue->dlsch_errors_last[gNB_id]))/(ue->dlsch_received[gNB_id] - ue->dlsch_received_last[gNB_id]);

      ue->dlsch_errors_last[gNB_id] = ue->dlsch_errors[gNB_id];
      ue->dlsch_received_last[gNB_id] = ue->dlsch_received[gNB_id];
    }


    ue->bitrate[gNB_id] = (ue->total_TBS[gNB_id] - ue->total_TBS_last[gNB_id])*100;
    ue->total_TBS_last[gNB_id] = ue->total_TBS[gNB_id];
    LOG_D(PHY,"[UE %d] Calculating bitrate Frame %d: total_TBS = %d, total_TBS_last = %d, bitrate %f kbits\n",
          ue->Mod_id,frame_rx,ue->total_TBS[gNB_id],
          ue->total_TBS_last[gNB_id],(float) ue->bitrate[gNB_id]/1000.0);
  }

  LOG_D(PHY," ****** end RX-Chain  for AbsSubframe %d.%d ******  \n", frame_rx%1024, nr_slot_rx);
  UEscopeCopy(ue, commonRxdataF, rxdataF, sizeof(int32_t), ue->frame_parms.nb_antennas_rx, rxdataF_sz, 0);
}

// TODO: Actuate the MAC-requested PRACH power after defining a calibrated dBm-to-waveform/radio mapping.
static void nr_ue_prach_procedures(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, c16_t **txData)
{
  int gNB_id = proc->gNB_id;
  int frame_tx = proc->frame_tx, nr_slot_tx = proc->nr_slot_tx, generated_prach_power;
  uint8_t mod_id = ue->Mod_id;

  NR_UE_PRACH *prach_var = ue->prach_vars[gNB_id];
  if (prach_var->active) {
    fapi_nr_ul_config_prach_pdu *prach_pdu = &prach_var->prach_pdu;
    // Generate PRACH in first slot. For L839, the following slots are also filled in this slot.
    if (prach_pdu->prach_slot == nr_slot_tx) {
      ue->tx_power_dBm[nr_slot_tx] = prach_pdu->prach_tx_power;
      const int16_t tx_amp = AMP;

      LOG_D(PHY,
            "In %s: [UE %d][RAPROC][%d.%d]: Generating PRACH Msg1 (preamble %d, requested TX power %d dBm, digital "
            "amplitude %d)\n",
            __FUNCTION__,
            mod_id,
            frame_tx,
            nr_slot_tx,
            prach_pdu->ra_PreambleIndex,
            ue->tx_power_dBm[nr_slot_tx],
            tx_amp);

      start_meas_nr_ue_phy(ue, PRACH_GEN_STATS);
      generated_prach_power = generate_nr_prach(ue, gNB_id, frame_tx, nr_slot_tx, tx_amp, txData);
      stop_meas_nr_ue_phy(ue, PRACH_GEN_STATS);
      if (cpumeas(CPUMEAS_GETSTATE)) {
        LOG_D(PHY,
              "[SFN %d.%d] PRACH Proc %5.2f\n",
              proc->frame_tx,
              proc->nr_slot_tx,
              ue->phy_cpu_stats.cpu_time_stats[PRACH_GEN_STATS].p_time / (1000.0));
      }

      LOG_D(PHY,
            "In %s: [UE %d][RAPROC][%d.%d]: Generated PRACH Msg1 (requested TX power %d dBm, digital amplitude %d, "
            "digital power %d dB)\n",
            __FUNCTION__,
            mod_id,
            frame_tx,
            nr_slot_tx,
            ue->tx_power_dBm[nr_slot_tx],
            tx_amp,
            dB_fixed(generated_prach_power));

      // set duration of prach slots so we know when to skip OFDM modulation
      const int prach_format = ue->prach_vars[gNB_id]->prach_pdu.prach_format;
      const int prach_slots = (prach_format < 4) ? get_long_prach_dur(prach_format, ue->frame_parms.numerology_index) : 1;
      prach_var->num_prach_slots = prach_slots;
    }

    // set as inactive in the last slot
    prach_var->active = !(nr_slot_tx == (prach_pdu->prach_slot + prach_var->num_prach_slots - 1));
  }
}
