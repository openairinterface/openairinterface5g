/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/sysinfo.h>
#include <math.h>

#include "common/utils/nr/nr_common.h"
#include "common/utils/assertions.h"
#include "common/utils/system.h"
#include "common/utils/fsn.h"
#include "common/ran_context.h"

#include "radio/ETHERNET/ethernet_lib.h"

#include "PHY/defs_nr_common.h"
#include "PHY/phy_extern.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/INIT/nr_phy_init.h"
#include "SCHED_NR/sched_nr.h"

#include "common/utils/LOG/log.h"
#include "common/utils/time_manager/time_manager.h"

#include <executables/softmodem-common.h>
/* these variables have to be defined before including ENB_APP/enb_paramdef.h and GNB_APP/gnb_paramdef.h */
static int DEFBANDS[] = {7};
static int DEFENBS[] = {0};
static int DEFBFW[] = {0x00007fff};
static int DEFRUTPCORES[] = {-1,-1,-1,-1};

#include "ENB_APP/enb_paramdef.h"
#include "GNB_APP/gnb_paramdef.h"
#include "common/config/config_userapi.h"

#include <openair1/PHY/TOOLS/phy_scope_interface.h>

#include "T.h"
#include "nfapi_interface.h"
#include <nfapi/oai_integration/vendor_ext.h>
#include "executables/nr-softmodem-common.h"

static void NRRCconfig_RU(configmodule_interface_t *cfg);

/*************************************************************/
/* Southbound Fronthaul functions, RCC/RAU                   */

// southbound IF5 fronthaul for 16-bit OAI format
void fh_if5_south_out(RU_t *ru, int frame, int slot, uint64_t timestamp)
{
  int offset = get_samples_slot_timestamp(ru->nr_frame_parms, slot);
  void *buffs[ru->nb_tx];
  for (int aid = 0; aid < ru->nb_tx; aid++)
    buffs[aid] = (void*)&ru->common.txdata[aid][offset];
  struct timespec txmeas;
  clock_gettime(CLOCK_MONOTONIC, &txmeas);
  LOG_D(NR_PHY,
        "IF5 TX %d.%d, TS %lu, buffs[0] %p, buffs[1] %p ener0 %f dB, tx start %d\n",
        frame,
        slot,
        timestamp,
        buffs[0],
        buffs[1],
        10 * log10((double)signal_energy(buffs[0], get_samples_per_slot(slot, ru->nr_frame_parms))),
        (int)txmeas.tv_nsec);
  ru->ifdevice.trx_write_func2(&ru->ifdevice, timestamp, buffs, 0, get_samples_per_slot(slot, ru->nr_frame_parms), 0, ru->nb_tx);
}

/*************************************************************/
/* Input Fronthaul from south RCC/RAU                        */

// Synchronous if5 from south

void fh_if5_south_in(RU_t *ru, int *frame, int *tti)
{
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  RU_proc_t *proc = &ru->proc;
  start_meas(&ru->rx_fhaul);

  ru->ifdevice.trx_read_func2(&ru->ifdevice, &proc->timestamp_rx, NULL, get_samples_per_slot(*tti, fp));
  if (proc->first_rx == 1)
    ru->ts_offset = proc->timestamp_rx;
  proc->frame_rx = ((proc->timestamp_rx - ru->ts_offset) / (fp->samples_per_subframe * 10)) & 1023;
  proc->tti_rx = get_slot_from_timestamp(proc->timestamp_rx - ru->ts_offset, fp);

  if (proc->first_rx == 0) {
    if (proc->tti_rx != *tti) {
      LOG_E(PHY,"Received Timestamp doesn't correspond to the time we think it is (proc->tti_rx %d, subframe %d)\n",proc->tti_rx,*tti);
      if (!oai_exit)
        exit_fun("Exiting");
      return;
    }

    if (proc->frame_rx != *frame) {
      LOG_E(PHY,"Received Timestamp doesn't correspond to the time we think it is (proc->frame_rx %d frame %d proc->tti_rx %d tti %d)\n",proc->frame_rx,*frame,proc->tti_rx,*tti);
      if (!oai_exit)
        exit_fun("Exiting");
      return;
    }
  } else {
    proc->first_rx = 0;
    *frame = proc->frame_rx;
    *tti = proc->tti_rx;
  }

  stop_meas(&ru->rx_fhaul);
  struct timespec rxmeas;
  clock_gettime(CLOCK_MONOTONIC, &rxmeas);
  double fhtime = ru->rx_fhaul.p_time/(1000.0);
  if (fhtime > 800)
    LOG_W(PHY,
          "IF5 %d.%d => RX %d.%d first_rx %d: time %f, rxstart %ld\n",
          *frame,
          *tti,
          proc->frame_rx,
          proc->tti_rx,
          proc->first_rx,
          ru->rx_fhaul.p_time / (1000.0),
          rxmeas.tv_nsec);
  else
    LOG_D(PHY,
          "IF5 %d.%d => RX %d.%d first_rx %d: time %f, rxstart %ld\n",
          *frame,
          *tti,
          proc->frame_rx,
          proc->tti_rx,
          proc->first_rx,
          ru->rx_fhaul.p_time / (1000.0),
          rxmeas.tv_nsec);
}

static void rx_rf(RU_t *ru, int *frame, int *slot)
{
  RU_proc_t *proc = &ru->proc;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  openair0_config_t *cfg   = &ru->openair0_cfg;
  uint32_t samples_per_slot = get_samples_per_slot(*slot, fp);
  AssertFatal(*slot < fp->slots_per_frame && *slot >= 0, "slot %d is illegal (%d)\n", *slot, fp->slots_per_frame);

  start_meas(&ru->rx_fhaul);
  int nb = ru->nb_rx;
  void *rxp[nb];
  for (int i = 0; i < nb; i++)
    rxp[i] = (void *)&ru->common.rxdata[i][get_samples_slot_timestamp(fp, *slot)];

  openair0_timestamp_t old_ts = proc->timestamp_rx;
  LOG_D(PHY,"Reading %d samples for slot %d (%p)\n", samples_per_slot, *slot, rxp[0]);

  openair0_timestamp_t ts;
  unsigned int rxs;
  rxs = ru->rfdevice.trx_read_func(&ru->rfdevice, &ts, rxp, samples_per_slot, nb);
  proc->timestamp_rx = ts-ru->ts_offset;

  if (rxs != samples_per_slot)
    LOG_E(PHY, "rx_rf: Asked for %d samples, got %d from USRP\n", samples_per_slot, rxs);

  if (proc->first_rx != 1) {
    uint32_t samples_per_slot_prev = get_samples_per_slot((*slot - 1) % fp->slots_per_frame, fp);

    if (proc->timestamp_rx - old_ts != samples_per_slot_prev) {
      LOG_D(PHY,
            "rx_rf: rfdevice timing drift of %" PRId64 " samples (ts_off %" PRId64 ")\n",
            proc->timestamp_rx - old_ts - samples_per_slot_prev,
            ru->ts_offset);
      ru->ts_offset += (proc->timestamp_rx - old_ts - samples_per_slot_prev);
      proc->timestamp_rx = ts-ru->ts_offset;
    }
  }

  // compute system frame number (SFN) according to O-RAN-WG4-CUS.0-v02.00 (using alpha=beta=0)
  //  this assumes that the USRP has been synchronized to the GPS time
  //  OAI uses timestamps in sample time stored in int64_t, but it will fit in double precision for many years to come.
  double gps_sec = ((double)ts) / cfg->sample_rate;

  // in fact the following line is the same as long as the timestamp_rx is synchronized to GPS. 
  proc->frame_rx    = (proc->timestamp_rx / (fp->samples_per_subframe*10))&1023;
  proc->tti_rx = get_slot_from_timestamp(proc->timestamp_rx, fp);
  // synchronize first reception to frame 0 subframe 0
  LOG_D(PHY,
        "RU %d/%d TS %ld, GPS %f, SR %f, frame %d, slot %d.%d / %d\n",
        ru->idx,
        0,
        ts,
        gps_sec,
        cfg->sample_rate,
        proc->frame_rx,
        proc->tti_rx,
        proc->tti_tx,
        fp->slots_per_frame);

  if (proc->first_rx == 0) {
    if (proc->tti_rx != *slot) {
      LOG_E(PHY,
            "Received Timestamp (%lu) doesn't correspond to the time we think it is (proc->tti_rx %d, slot %d)\n",
            proc->timestamp_rx,
            proc->tti_rx,
            *slot);
      exit_fun("Exiting");
    }

    if (proc->frame_rx != *frame) {
      LOG_E(PHY,
            "Received Timestamp (%lu) doesn't correspond to the time we think it is (proc->frame_rx %d frame %d, proc->tti_rx %d, "
            "slot %d)\n",
            proc->timestamp_rx,
            proc->frame_rx,
            *frame,
            proc->tti_rx,
            *slot);
      exit_fun("Exiting");
    }
  } else {
    proc->first_rx = 0;
    *frame = proc->frame_rx;
    *slot  = proc->tti_rx;

    // Align to slot boundary
    uint64_t samples_to_slot_boundary = 0;
    uint64_t sample_offset_within_frame = proc->timestamp_rx % fp->samples_per_frame;
    uint64_t sample_offset_within_slot = sample_offset_within_frame - get_samples_slot_timestamp(fp, *slot);
    if (sample_offset_within_slot > 0) {
      samples_to_slot_boundary = get_samples_per_slot(*slot, fp) - sample_offset_within_slot;
      LOG_A(NR_PHY, "Aligning to the slot boundary %lu\n", samples_to_slot_boundary);

      // Read and discard the samples in the first_rx to align to the slot boundary
      rxs = ru->rfdevice.trx_read_func(&ru->rfdevice, &ts, rxp, samples_to_slot_boundary, nb);
      if (rxs != samples_to_slot_boundary)
        LOG_E(PHY, "rx_rf: Asked for %ld samples, got %d from USRP\n", samples_to_slot_boundary, rxs);

      proc->timestamp_rx += samples_to_slot_boundary;
      if (*slot + 1 >= fp->slots_per_frame)
        *frame = *frame + 1;
      *slot = (*slot + 1) % fp->slots_per_frame;
    }
  }

  metadata mt = {.slot = *slot, .frame = *frame};
  gNBscopeCopyWithMetadata(ru, gNbTimeDomainSamples, rxp[0], sizeof(c16_t), 1, samples_per_slot, 0, &mt);

  stop_meas(&ru->rx_fhaul);
}

int tx_rf_symbols(RU_t *ru, int frame, int slot, uint64_t timestamp, int start_symbol, int num_symbols)
{
  RU_proc_t *proc = &ru->proc;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &ru->config;
  T(T_ENB_PHY_OUTPUT_SIGNAL,
    T_INT(0),
    T_INT(0),
    T_INT(frame),
    T_INT(slot),
    T_INT(0),
    T_BUFFER(&ru->common.txdata[0][get_samples_slot_timestamp(fp, slot)], get_samples_per_slot(slot, fp) * 4));
  int sf_extension = 0;
  int siglen = get_samples_per_slot(slot, fp);
  radio_tx_burst_flag_t flags_burst = TX_BURST_INVALID;
  int transmitted_symbols = num_symbols;

  if (cfg->cell_config.frame_duplex_type.value == TDD && !get_softmodem_params()->continuous_tx && !IS_SOFTMODEM_RFSIM) {
    int slot_type = nr_slot_select(cfg,frame,slot%fp->slots_per_frame);
    if(slot_type == NR_MIXED_SLOT) {
      int txsymb = 0;

      for(int symbol_count = 0; symbol_count < fp->symbols_per_slot; symbol_count++) {
        if (cfg->tdd_table.max_tdd_periodicity_list[slot].max_num_of_symbol_per_slot_list[symbol_count].slot_config.value == 0)
          txsymb++;
      }

      AssertFatal(txsymb > 0, "illegal txsymb %d\n", txsymb);

      if (txsymb < start_symbol) {
        // No DL symbols in this transmission
        return 0;
      }

      int end_symbol = start_symbol + num_symbols - 1;
      if (end_symbol >= txsymb) {
        flags_burst = TX_BURST_END;
      } else {
        flags_burst = TX_BURST_MIDDLE;
      }

      int num_symbols_this_transmission = min(txsymb, end_symbol) - start_symbol + 1;
      transmitted_symbols = num_symbols_this_transmission;

      siglen = get_samples_symbol_duration(fp, slot, start_symbol, num_symbols_this_transmission);
    } else if (slot_type == NR_DOWNLINK_SLOT) {
      int prevslot_type = nr_slot_select(cfg,frame,(slot+(fp->slots_per_frame-1))%fp->slots_per_frame);
      int nextslot_type = nr_slot_select(cfg,frame,(slot+1)%fp->slots_per_frame);
      if (prevslot_type == NR_UPLINK_SLOT) {
        flags_burst = TX_BURST_START;
        sf_extension = ru->sf_extension;
      } else if (nextslot_type == NR_UPLINK_SLOT) {
        flags_burst = TX_BURST_END;
      } else {
        flags_burst = proc->first_tx == 1 ? TX_BURST_START : TX_BURST_MIDDLE;
      }
      siglen = get_samples_symbol_duration(fp, slot, start_symbol, num_symbols);
    } else if (slot_type == NR_UPLINK_SLOT) {
      // Do not transmit during uplink slots
      return 0;
    }
  } else { // FDD
    flags_burst = proc->first_tx == 1 ? TX_BURST_START : TX_BURST_MIDDLE;
    siglen = get_samples_symbol_duration(fp, slot, start_symbol, num_symbols);
  }

  const int flags = flags_burst;
  proc->first_tx = 0;

  int nt = ru->nb_tx;
  void *txp[nt];
  uint32_t time_offset = get_samples_slot_timestamp(fp, slot) + get_samples_symbol_timestamp(fp, slot, start_symbol);
  for (int i = 0; i < nt; i++)
    txp[i] = (void *)&ru->common.txdata[i][time_offset] - sf_extension * sizeof(int32_t);

  // prepare tx buffer pointers
  uint32_t txs = ru->rfdevice.trx_write_func(&ru->rfdevice,
                                             timestamp + ru->ts_offset - sf_extension,
                                             txp,
                                             siglen + sf_extension,
                                             nt,
                                             flags);
  LOG_D(PHY,
        "[TXPATH] RU %d tx_rf, writing to TS %lu, %d.%d, unwrapped_frame %d, slot %d, flags %d, siglen+sf_extension %d, "
        "returned %d, E %f\n",
        ru->idx,
        timestamp + ru->ts_offset - sf_extension,
        frame,
        slot,
        proc->frame_tx_unwrap,
        slot,
        flags,
        siglen + sf_extension,
        txs,
        10 * log10((double)signal_energy(txp[0], siglen + sf_extension)));

  return transmitted_symbols;
}

// Pushes the per-antenna split 8 analog beam IDs assigned to this slot's symbols down to
// the RF device, one trx_set_beams() call per symbol at which the beam vector changes.
// However, USRP GPIO-controlled beamforming currently only handles one beam.
//
// Only calls trx_set_beams() when the beam vector actually changes between symbols, to avoid
// issuing redundant beam-switch commands to real hardware.
static void ctrl_rf(RU_t *ru, int frame, int slot, uint64_t timestamp)
{
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int nb_tx = ru->nb_tx;
  uint16_t **beam_id = ru->gNB_list[0]->common_vars.beam_id;

  uint16_t last_beams[nb_tx];
  memcpy(last_beams, beam_id[slot * fp->symbols_per_slot], nb_tx * sizeof(uint16_t));
  uint64_t event_ts = timestamp + ru->ts_offset;
  LOG_D(NR_PHY, "RU Control [%d.%d]: set beams at symbol 0, ts %lu\n", frame, slot, event_ts);
  ru->rfdevice.trx_set_beams(&ru->rfdevice, last_beams, nb_tx, event_ts);

  for (int j = 1; j < fp->symbols_per_slot; j++) {
    uint16_t *cur_beams = beam_id[slot * fp->symbols_per_slot + j];
    if (memcmp(cur_beams, last_beams, nb_tx * sizeof(uint16_t)) == 0)
      continue;
    memcpy(last_beams, cur_beams, nb_tx * sizeof(uint16_t));
    event_ts = timestamp + ru->ts_offset + get_samples_symbol_duration(fp, slot, 0, j);
    LOG_D(NR_PHY, "RU Control [%d.%d]: beam switch at symbol %d, ts %lu\n", frame, slot, j, event_ts);
    ru->rfdevice.trx_set_beams(&ru->rfdevice, last_beams, nb_tx, event_ts);
  }
}

void tx_rf(RU_t *ru, int frame, int slot, uint64_t timestamp)
{
  tx_rf_symbols(ru, frame, slot, timestamp, 0, 14);
}

void fill_rf_config(RU_t *ru, char *rf_config_file)
{
  NR_DL_FRAME_PARMS *fp   = ru->nr_frame_parms;
  nfapi_nr_config_request_scf_t *config = &ru->config; //tmp index
  openair0_config_t *cfg   = &ru->openair0_cfg;
  int mu = config->ssb_config.scs_common.value;
  int N_RB = config->carrier_config.dl_grid_size[config->ssb_config.scs_common.value].value;

  get_samplerate_and_bw(mu,
                        N_RB,
                        fp->threequarter_fs,
                        &cfg->sample_rate,
                        &cfg->tx_bw,
                        &cfg->rx_bw);

  if (config->cell_config.frame_duplex_type.value==TDD)
    cfg->duplex_mode = duplex_mode_TDD;
  else //FDD
    cfg->duplex_mode = duplex_mode_FDD;

  cfg->configFilename = rf_config_file;

  AssertFatal(ru->nb_tx > 0 && ru->nb_tx <= OPENAIR0_MAX_ANTENNAS, "openair0 does not support more than %d antennas\n", OPENAIR0_MAX_ANTENNAS);
  AssertFatal(ru->nb_rx > 0 && ru->nb_rx <= OPENAIR0_MAX_ANTENNAS, "openair0 does not support more than %d antennas\n", OPENAIR0_MAX_ANTENNAS);

  cfg->num_rb_dl = N_RB;
  cfg->tx_num_channels = ru->nb_tx;
  cfg->rx_num_channels = ru->nb_rx;
  cfg->num_distributed_ru = 1;
  LOG_I(PHY,"Setting RF config for N_RB %d, NB_RX %d, NB_TX %d\n",cfg->num_rb_dl,cfg->rx_num_channels,cfg->tx_num_channels);
  LOG_I(PHY,"tune_offset %.0f Hz, sample_rate %.0f Hz\n",cfg->tune_offset,cfg->sample_rate);
  cfg->nr_flag = 1;

  for (int i = 0; i < ru->nb_tx; i++) {
    if (ru->if_frequency == 0) {
      cfg->tx_freq[i] = fp->dl_CarrierFreq;
    } else if (ru->if_freq_offset) {
      cfg->tx_freq[i] = ru->if_frequency;
      LOG_I(PHY, "Setting IF TX frequency to %lu Hz with IF TX frequency offset %d Hz\n", ru->if_frequency, ru->if_freq_offset);
    } else {
      cfg->tx_freq[i] = ru->if_frequency;
    }

    cfg->tx_gain[i] = ru->att_tx;
    LOG_I(PHY, "Channel %d: setting tx_gain offset %.0f, tx_freq %.0f Hz\n", 
          i, cfg->tx_gain[i],cfg->tx_freq[i]);
  }

  for (int i = 0; i < ru->nb_rx; i++) {
    if (ru->if_frequency == 0) {
      cfg->rx_freq[i] = fp->ul_CarrierFreq;
    } else if (ru->if_freq_offset) {
      cfg->rx_freq[i] = ru->if_frequency + ru->if_freq_offset;
      LOG_I(PHY, "Setting IF RX frequency to %lu Hz with IF RX frequency offset %d Hz\n", ru->if_frequency, ru->if_freq_offset);
    } else {
      cfg->rx_freq[i] = ru->if_frequency + fp->ul_CarrierFreq - fp->dl_CarrierFreq;
    }

    cfg->rx_gain[i] = ru->max_rxgain-ru->att_rx;
    LOG_I(PHY, "Channel %d: setting rx_gain offset %.0f, rx_freq %.0f Hz\n",
          i,cfg->rx_gain[i],cfg->rx_freq[i]);
  }
}

void fill_split7_2_config(split7_config_t *split7, const nfapi_nr_config_request_scf_t *config, const NR_DL_FRAME_PARMS *fp)
{
  const nfapi_nr_prach_config_t *prach_config = &config->prach_config;
  const nfapi_nr_tdd_table_t *tdd_table = &config->tdd_table;
  const nfapi_nr_cell_config_t *cell_config = &config->cell_config;
  const nfapi_nr_carrier_config_t *carrier_config = &config->carrier_config;

  split7->mu = config->ssb_config.scs_common.value;
  DevAssert(prach_config->prach_ConfigurationIndex.tl.tag == NFAPI_NR_CONFIG_PRACH_CONFIG_INDEX_TAG);
  split7->prach_index = prach_config->prach_ConfigurationIndex.value;
  AssertFatal(prach_config->num_prach_fd_occasions.value >= 1, "must have at least one PRACH occasion\n");
  split7->prach_freq_start = prach_config->num_prach_fd_occasions_list[0].k1.value;

  DevAssert(cell_config->frame_duplex_type.tl.tag == NFAPI_NR_CONFIG_FRAME_DUPLEX_TYPE_TAG);
  if (cell_config->frame_duplex_type.value == 1 /* TDD */) {
    DevAssert(tdd_table->tdd_period.tl.tag == NFAPI_NR_CONFIG_TDD_PERIOD_TAG);
    int nb_periods_per_frame = get_nb_periods_per_frame(tdd_table->tdd_period.value);
    split7->n_tdd_period = fp->slots_per_frame / nb_periods_per_frame;
    for (int slot = 0; slot < split7->n_tdd_period; ++slot) {
      for (int sym = 0; sym < 14; ++sym) {
        split7->slot_dirs[slot].sym_dir[sym] = tdd_table->max_tdd_periodicity_list[slot].max_num_of_symbol_per_slot_list[sym].slot_config.value;
      }
    }
  }

  split7->prach_fftSize = prach_config->prach_sequence_length.value == 0 ? 10 : 8; // need to handle 5kHz cases better than this
  split7->fftSize = log2(fp->ofdm_symbol_size);

  // M-plane related parameters
  for (size_t i = 0; i < 5 ; i++) {
    split7->dl_k0[i] = carrier_config->dl_k0[i].value;
    split7->ul_k0[i] = carrier_config->ul_k0[i].value;
  }
  split7->cp_prefix0 = fp->nb_prefix_samples0;
  split7->cp_prefix_other = fp->nb_prefix_samples;
}

/* this function maps the RU tx and rx buffers to the available rf chains.
   Each rf chain is is addressed by the card number and the chain on the card. The
   rf_map specifies for each antenna port, on which rf chain the mapping should start. Multiple
   antennas are mapped to successive RF chains on the same card. */
int setup_RU_buffers(RU_t *ru)
{
  if (!ru)
    return (-1);

  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  nfapi_nr_config_request_scf_t *config = &ru->config;
  int mu = config->ssb_config.scs_common.value;
  int N_RB = config->carrier_config.dl_grid_size[config->ssb_config.scs_common.value].value;

  ru->N_TA_offset = set_default_nta_offset(fp->freq_range, fp->samples_per_subframe);
  LOG_I(PHY,
        "RU %d Setting N_TA_offset to %d samples (UL Freq %d, N_RB %d, mu %d)\n",
        ru->idx, ru->N_TA_offset, config->carrier_config.uplink_frequency.value, N_RB, mu);

  if (ru->openair0_cfg.mmapped_dma == 1) {
    // replace RX signal buffers with mmaped HW versions
    for (int i = 0; i < ru->nb_rx; i++) {
      int card = i / 4;
      int ant = i % 4;
      LOG_D(PHY, "Mapping RU id %u, rx_ant %d, on card %d, chain %d\n", ru->idx, i, ru->rf_map.card + card, ru->rf_map.chain + ant);
      free(ru->common.rxdata[i]);
      ru->common.rxdata[i] = ru->openair0_cfg.rxbase[ru->rf_map.chain + ant];

      for (int j = 0; j < 16; j++) {
        ru->common.rxdata[i][j] = 16 - j;
      }
    }

    for (int i = 0; i < ru->nb_tx; i++) {
      int card = i / 4;
      int ant = i % 4;
      LOG_D(PHY, "Mapping RU id %u, tx_ant %d, on card %d, chain %d\n", ru->idx, i, ru->rf_map.card + card, ru->rf_map.chain + ant);
      // nr_phy_init_RU() advanced the buffer by sf_extension
      free(&ru->common.txdata[i][-ru->sf_extension]);
      ru->common.txdata[i] = ru->openair0_cfg.txbase[ru->rf_map.chain + ant];

      for (int j = 0; j < 16; j++) {
        ru->common.txdata[i][j] = 16 - j;
      }
    }
  } else {
    // not memory-mapped DMA
    // nothing to do, everything already allocated in lte_init
  }

  return(0);
}

/* @brief wait for the next RX TTI to be free
 *
 * Certain radios, e.g., RFsim, can run faster than real-time. This might
 * create problems, e.g., if RX and TX get too far from each other. This
 * function ensures that a maximum of 4 RX slots are processed at a time (and
 * not more than those four are started).
 *
 * Through the queue L1_rx_out, we are informed about completed RX jobs.
 * rx_tti_busy keeps track of individual slots that have been started; this
 * function blocks until the current frame/slot is completed, signaled through
 * a message.
 *
 * @param L1_rx_out the queue from which to read completed RX jobs
 * @param rx_tti_busy array to mark RX job completion
 * @param frame_rx the frame to wait for
 * @param slot_rx the slot to wait for
 */
static bool wait_free_rx_tti(notifiedFIFO_t *L1_rx_out, bool rx_tti_busy[RU_RX_SLOT_DEPTH], int frame_rx, int slot_rx)
{
  int idx = slot_rx % RU_RX_SLOT_DEPTH;
  if (rx_tti_busy[idx]) {
    bool not_done = true;
    LOG_D(NR_PHY, "%d.%d Waiting to access RX slot %d\n", frame_rx, slot_rx, idx);
    // block and wait for frame_rx/slot_rx free from previous slot processing.
    // as we can get other slots, we loop on the queue
    while (not_done) {
      notifiedFIFO_elt_t *res = pullNotifiedFIFO(L1_rx_out);
      if (!res)
        return false;
      processingData_L1_t *info = NotifiedFifoData(res);
      LOG_D(NR_PHY, "%d.%d Got access to RX slot %d.%d (%d)\n", frame_rx, slot_rx, info->frame_rx, info->slot_rx, idx);
      rx_tti_busy[info->slot_rx % RU_RX_SLOT_DEPTH] = false;
      if ((info->slot_rx % RU_RX_SLOT_DEPTH) == idx)
        not_done = false;
      delNotifiedFIFO_elt(res);
    }
  }
  // set the tti to busy: the caller will process this slot now
  rx_tti_busy[idx] = true;
  return true;
}

void *ru_thread(void *param)
{
  static int ru_thread_status;
  RU_t               *ru      = (RU_t *)param;
  RU_proc_t          *proc    = &ru->proc;
  NR_DL_FRAME_PARMS  *fp      = ru->nr_frame_parms;
  PHY_VARS_gNB *gNB = RC.gNB[0]; // this RU main loop handes only one RU
  int                ret;
  int                slot     = fp->slots_per_frame-1;
  int                frame    = 1023;
  char               threadname[40];
  int initial_wait = 0;

  bool rx_tti_busy[RU_RX_SLOT_DEPTH] = {false};
  // set default return value
  ru_thread_status = 0;
  // set default return value
  sprintf(threadname,"ru_thread %u",ru->idx);
  LOG_I(PHY,"Starting RU %d (%s,%s) on cpu %d\n",ru->idx,NB_functions[ru->function],NB_timing[ru->if_timing],sched_getcpu());
  ru->config = gNB->gNB_config;

  nr_init_frame_parms(&ru->config, fp);
  nr_dump_frame_parms(fp);
  nr_phy_init_RU(ru);
  fill_rf_config(ru, ru->rf_config_file);
  fill_split7_2_config(&ru->openair0_cfg.split7, &ru->config, fp);

  // Start IF device if any
  if (ru->nr_start_if) {
    LOG_I(PHY, "starting transport\n");
    ret = openair0_transport_load(&ru->ifdevice, &ru->openair0_cfg);
    AssertFatal(ret == 0, "RU %u: openair0_transport_init() ret %d: cannot initialize transport protocol\n", ru->idx, ret);

    if (ru->ifdevice.get_internal_parameter) {
      /* it seems the device can "overwrite" (request?) to set the callbacks
       * for fh_south_in()/fh_south_out() differently */
      void *t = ru->ifdevice.get_internal_parameter("fh_if4p5_south_in");
      if (t != NULL)
        ru->fh_south_in = t;
      t = ru->ifdevice.get_internal_parameter("fh_if4p5_south_out");
      if (t != NULL)
        ru->fh_south_out = t;
      // this is temporarily until the new split 7.2 API
      t = ru->ifdevice.get_internal_parameter("fh_if4p5_ctrl");
      if (t != NULL)
        ru->fh_south_ctrl = t;
    }

    int cpu = sched_getcpu();
    if (ru->ru_thread_core > -1 && cpu != ru->ru_thread_core) {
      /* we start the ru_thread using threadCreate(), which already sets CPU
       * affinity; let's force it here again as per feature request #732 */
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(ru->ru_thread_core, &cpuset);
      int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      AssertFatal(ret == 0, "Error in pthread_getaffinity_np(): ret: %d, errno: %d", ret, errno);
      LOG_I(PHY, "RU %d: manually set CPU affinity to CPU %d\n", ru->idx, ru->ru_thread_core);
    }

    LOG_I(PHY, "Starting IF interface for RU %d, nb_rx %d\n", ru->idx, ru->nb_rx);
    AssertFatal(ru->nr_start_if(ru) == 0, "Could not start the IF device\n");

  } else if (ru->if_south == LOCAL_RF) { // configure RF parameters only
    ret = openair0_device_load(&ru->rfdevice,&ru->openair0_cfg);
    AssertFatal(ret==0,"Cannot connect to local radio\n");
  }

  if (setup_RU_buffers(ru)!=0) {
    LOG_E(PHY, "Exiting, cannot initialize RU Buffers\n");
    exit(-1);
  }

  LOG_I(PHY, "Signaling main thread that RU %d is ready, sl_ahead %d\n",ru->idx,ru->sl_ahead);
  pthread_mutex_lock(&RC.ru_mutex);
  RC.ru_mask &= ~(1<<ru->idx);
  pthread_cond_signal(&RC.ru_cond);
  pthread_mutex_unlock(&RC.ru_mutex);
  wait_sync("ru_thread");

  // Start RF device if any
  if (ru->start_rf) {
    ret = ru->start_rf(ru);
    AssertFatal(ret == 0, "RU %u: start_rf() ret %d: cannot start RF device\n", ru->idx, ret);
    LOG_I(PHY, "RU %d rf device ready\n", ru->idx);
  } else
    LOG_I(PHY, "RU %d no rf device\n", ru->idx);

  LOG_I(PHY, "RU %d RF started cpu_meas_enabled %d\n", ru->idx, cpu_meas_enabled);
  // start trx write thread
  if (usrp_tx_thread == 1) {
    if (ru->start_write_thread) {
      if (ru->start_write_thread(ru) != 0) {
        LOG_E(HW, "Could not start tx write thread\n");
      } else {
        LOG_I(PHY, "tx write thread ready\n");
      }
    }
  }

  // This is a forever while loop, it loops over subframes which are scheduled by incoming samples from HW devices
  struct timespec slot_start;
  clock_gettime(CLOCK_MONOTONIC, &slot_start);

  while (!oai_exit) {
    if (slot==(fp->slots_per_frame-1)) {
      slot=0;
      frame++;
      frame&=1023;
    } else {
      slot++;
    }

    // pretend we have 1 iq sample per slot
    // and so slots_per_frame * 100 iq samples per second (1 frame being 10ms)
    time_manager_iq_samples(1, fp->slots_per_frame * 100);

    // synchronization on input FH interface, acquire signals/data and block
    LOG_D(PHY,"[RU_thread] read data: frame_rx = %d, tti_rx = %d\n", frame, slot);

    AssertFatal(ru->fh_south_in, "No fronthaul interface at south port");
    ru->fh_south_in(ru, &frame, &slot);

    if (initial_wait == 1 && proc->frame_rx < 300) {
      if (proc->frame_rx > 0 && ((proc->frame_rx % 100) == 0) && proc->tti_rx == 0) {
        LOG_D(PHY, "delay processing to let RX stream settle, frame %d (trials %d)\n", proc->frame_rx, ru->rx_fhaul.trials);
        print_meas(&ru->rx_fhaul, "rx_fhaul", NULL, NULL);
        reset_meas(&ru->rx_fhaul);
      }
      continue;
    }
    if (proc->frame_rx>=300)  {
      initial_wait = 0;
    }
    if (initial_wait == 0 && ru->rx_fhaul.trials > 1000) {
        reset_meas(&ru->rx_fhaul);
        reset_meas(&ru->tx_fhaul);
    }
    proc->timestamp_tx = proc->timestamp_rx;
    for (int i = proc->tti_rx; i < proc->tti_rx + ru->sl_ahead; i++)
      proc->timestamp_tx += get_samples_per_slot(i % fp->slots_per_frame, fp);
    proc->tti_tx = (proc->tti_rx + ru->sl_ahead) % fp->slots_per_frame;
    proc->frame_tx = proc->tti_rx > proc->tti_tx ? (proc->frame_rx + 1) & 1023 : proc->frame_rx;
    LOG_D(PHY,
          "AFTER fh_south_in - SFN/SL:%d%d RU->proc[RX:%d.%d TX:%d.%d] RC.gNB[0]:[RX:%d%d TX(SFN):%d]\n",
          frame,
          slot,
          proc->frame_rx,
          proc->tti_rx,
          proc->frame_tx,
          proc->tti_tx,
          gNB->proc.frame_rx,
          gNB->proc.slot_rx,
          gNB->proc.frame_tx);

    if (ru->idx != 0)
      proc->frame_tx = (proc->frame_tx + proc->frame_offset) & 1023;

    // do RX front-end processing (frequency-shift, dft) if needed
    int slot_type = nr_slot_select(&ru->config, proc->frame_rx, proc->tti_rx);
    if (slot_type == NR_UPLINK_SLOT || slot_type == NR_MIXED_SLOT) {
      if (!wait_free_rx_tti(&gNB->L1_rx_out, rx_tti_busy, proc->frame_rx, proc->tti_rx))
        break; // nothing to wait for: we have to stop
      if (ru->feprx) {
        ru->feprx(ru,proc->tti_rx);
        LOG_D(NR_PHY, "Setting %d.%d (%d) to busy\n", proc->frame_rx, proc->tti_rx, proc->tti_rx % RU_RX_SLOT_DEPTH);
        //LOG_M("rxdata.m","rxs",ru->common.rxdata[0],1228800,1,1);
        LOG_D(PHY,"RU proc: frame_rx = %d, tti_rx = %d\n", proc->frame_rx, proc->tti_rx);
        gNBscopeCopy(gNB,
                     gNBRxdataF,
                     ru->common.rxdataF[0],
                     sizeof(c16_t),
                     1,
                     gNB->frame_parms.samples_per_slot_wCP,
                     proc->tti_rx * gNB->frame_parms.samples_per_slot_wCP);

        // Do PRACH RU processing
        fsn_t now = {.f = proc->frame_rx, .s = proc->tti_rx, .mu = fp->numerology_index};
        prach_item_t p;
        while (get_next_nr_prach(&gNB->prach_ru_queue, &now, &p)) {
          // need to extract RACH data for later processing by rx_nr_prach()
          rx_nr_prach_ru(&p, ru->common.rxdata, ru->nr_frame_parms, ru->N_TA_offset, gNB->enable_analog_das);
          bool success = spsc_q_put(&gNB->prach_l1rx_queue, &p, sizeof(p));
          // assume prach_l1rx_queue never full: prach_ru_queue filled at
          // constant pace, but prach_l1rx_queue emptied as fast as possible,
          // see rx_func()
          DevAssert(success);
        } // end if (prach_id >= 0)
      } // end if (ru->feprx)
    } // end if (slot_type == NR_UPLINK_SLOT || slot_type == NR_MIXED_SLOT) {

    notifiedFIFO_elt_t *resTx = newNotifiedFIFO_elt(sizeof(processingData_L1tx_t), 0, &gNB->L1_tx_out, NULL);
    resTx->key = proc->tti_tx;
    processingData_L1tx_t *syncMsgTx = NotifiedFifoData(resTx);
    *syncMsgTx = (processingData_L1tx_t){.gNB = gNB,
                                         .frame = proc->frame_tx,
                                         .slot = proc->tti_tx,
                                         .frame_rx = proc->frame_rx,
                                         .slot_rx = proc->tti_rx,
                                         .timestamp_tx = proc->timestamp_tx};
    pushNotifiedFIFO(&gNB->L1_tx_out, resTx);
  }

  ru_thread_status = 0;
  return &ru_thread_status;
}

int start_streaming(RU_t *ru) {
  LOG_I(PHY,"Starting streaming on third-party RRU\n");
  return ru->ifdevice.thirdparty_startstreaming(&ru->ifdevice);
}

int nr_start_if(struct RU_t_s *ru)
{
  if (ru->if_south <= REMOTE_IF5)
    for (int i = 0; i < ru->nb_rx; i++)
      ru->openair0_cfg.rxbase[i] = ru->common.rxdata[i];
  ru->openair0_cfg.rxsize = ru->nr_frame_parms->samples_per_subframe*10;
  return ru->ifdevice.trx_start_func(&ru->ifdevice);
}

int start_rf(RU_t *ru)
{
  return(ru->rfdevice.trx_start_func(&ru->rfdevice));
}

int stop_rf(RU_t *ru)
{
  if (ru->rfdevice.trx_get_stats_func) {
    ru->rfdevice.trx_get_stats_func(&ru->rfdevice);
  }
  ru->rfdevice.trx_end_func(&ru->rfdevice);
  return 0;
}

int start_write_thread(RU_t *ru) {
  return ru->rfdevice.trx_write_init(&ru->rfdevice);
}

void kill_NR_RU_proc(int inst) {
  RU_t *ru = RC.ru[inst];
  RU_proc_t *proc = &ru->proc;

  /* Note: it seems pthread_FH and and FEP thread below both use
   * mutex_fep/cond_fep. Thus, we unlocked above for pthread_FH above and do
   * the same for FEP thread below again (using broadcast() to ensure both
   * threads get the signal). This one will also destroy the mutex and cond. */
  pthread_mutex_lock(proc->mutex_fep);
  proc->instance_cnt_fep[0] = 0;
  pthread_cond_broadcast(proc->cond_fep);
  pthread_mutex_unlock(proc->mutex_fep);

  /* Join the RU thread BEFORE aborting the RU thread pool: ru_thread() is a
   * producer of that pool (nr_fep_tp()/feptx push tasks on every UL/DL slot).
   * abortTpool() frees the pool's queues, so a push from ru_thread() after
   * that point races a destroyed mutex (EINVAL) and asserts. oai_exit is
   * already set at this point and the RF reads are non-blocking, so the join
   * returns promptly. */
  pthread_join(proc->pthread_FH, NULL);

  if (ru->if_south != REMOTE_IF4p5) {
    abortTpool(ru->threadPool);
    abortNotifiedFIFO(ru->respfeprx);
    abortNotifiedFIFO(ru->respfeptx);
  }

  // everything should be stopped now, we can safely stop the RF device
  if (ru->stop_rf == NULL) {
    LOG_W(PHY, "No stop_rf() for RU %d defined, cannot stop RF!\n", ru->idx);
    return;
  }
  int rc = ru->stop_rf(ru);
  if (rc != 0) {
    LOG_W(PHY, "stop_rf() returned %d, RU %d RF device did not stop properly!\n", rc, ru->idx);
    return;
  }
  LOG_I(PHY, "RU %d RF device stopped\n",ru->idx);
}

void set_function_spec_param(RU_t *ru)
{
  switch (ru->if_south) {
    case LOCAL_RF:   // this is an RU with integrated RF (RRU, gNB)
      reset_meas(&ru->rx_fhaul);
      AssertFatal(ru->function == gNodeB_3GPP, "ru->function %d not supported for LOCAL_RF\n", ru->function);
      ru->feprx = nr_fep_tp; // this is frequency-shift + DFTs
      ru->feptx_ofdm = nr_feptx_tp; // this is fep with idft and precoding
      ru->feptx_prec = NULL;
      ru->nr_start_if = NULL; // no if interface
      ru->fh_south_in = rx_rf; // local synchronous RF RX
      ru->fh_south_out = tx_rf; // local synchronous RF TX
      ru->fh_south_ctrl = ctrl_rf; // beam API
      ru->start_rf = start_rf; // need to start the local RF interface
      ru->stop_rf = stop_rf;
      ru->start_write_thread = start_write_thread; // starting RF TX in different thread
      break;

    case REMOTE_IF5: // the remote unit is IF5 RRU
      ru->feprx = nr_fep_tp; // this is frequency-shift + DFTs
      ru->feptx_prec = NULL; // need to do transmit Precoding + IDFTs
      ru->feptx_ofdm = nr_feptx_tp; // need to do transmit Precoding + IDFTs
      ru->fh_south_in = fh_if5_south_in; // synchronous IF5 reception
      ru->fh_south_out = fh_if5_south_out; // synchronous IF5 transmission
      ru->start_rf = ru->ifdevice.eth_params.transp_preference == ETH_UDP_IF5_ECPRI_MODE ? start_streaming : NULL;
      ru->stop_rf = NULL;
      ru->start_write_thread = NULL;
      ru->nr_start_if = nr_start_if; // need to start if interface for IF5
      break;

    case REMOTE_IF4p5:
      ru->feprx = NULL; // DFTs
      ru->feptx_prec = nr_feptx_prec; // Precoding operation
      ru->feptx_ofdm = NULL; // no OFDM mod
      ru->fh_south_in = NULL;
      ru->fh_south_out = NULL;
      ru->start_rf = NULL; // no local RF
      ru->stop_rf = NULL;
      ru->start_write_thread = NULL;
      ru->nr_start_if = nr_start_if; // need to start if interface for IF4p5
      break;

    default:
      LOG_E(PHY, "RU with invalid or unknown southbound interface type %d\n", ru->if_south);
      break;
  } // switch on interface type
}

void init_NR_RU(configmodule_interface_t *cfg, char *rf_config_file)
{
  // create status mask
  RC.ru_mask = 0;
  pthread_mutex_init(&RC.ru_mutex,NULL);
  pthread_cond_init(&RC.ru_cond,NULL);
  // read in configuration file)
  NRRCconfig_RU(cfg);
  LOG_I(PHY,"number of L1 instances %d, number of RU %d, number of CPU cores %d\n",RC.nb_nr_L1_inst,RC.nb_RU,get_nprocs());
  LOG_D(PHY,"Process RUs RC.nb_RU:%d\n",RC.nb_RU);

  for (int ru_id = 0; ru_id < RC.nb_RU; ru_id++) {
    LOG_D(PHY,"Process RC.ru[%d]\n",ru_id);
    RU_t *ru = RC.ru[ru_id];
    ru->rf_config_file = rf_config_file;
    ru->idx            = ru_id;
    ru->ts_offset      = 0;
    // use gNB_list[0] as a reference for RU frame parameters
    // NOTE: multiple CC_id are not handled here yet!

    if (ru->num_gNB > 0) {
      LOG_D(PHY, "%s() RC.ru[%d].num_gNB:%d ru->gNB_list[0]:%p RC.gNB[0]:%p rf_config_file:%s\n", __FUNCTION__, ru_id, ru->num_gNB, ru->gNB_list[0], RC.gNB[0], ru->rf_config_file);

      if (ru->gNB_list[0] == 0) {
        LOG_E(PHY,"%s() DJP - ru->gNB_list ru->num_gNB are not initialized - so do it manually\n", __FUNCTION__);
        ru->gNB_list[0] = RC.gNB[0];
        ru->num_gNB=1;
      }
    }


    PHY_VARS_gNB *gNB_RC = NULL;
    PHY_VARS_gNB *gNB0 = NULL;
    if (RC.nb_nr_L1_inst > 0) {
      gNB_RC = RC.gNB[0];
      gNB0 = ru->gNB_list[0];
    }
    LOG_D(PHY, "RU FUnction:%d ru->if_south:%d\n", ru->function, ru->if_south);

    if (gNB0) {
      if (gNB_RC) {
        LOG_D(PHY, "Copying frame parms from gNB in RC to gNB %d in ru %d and frame_parms in ru\n", gNB0->Mod_id, ru->idx);
        *ru->nr_frame_parms = gNB_RC->frame_parms;
        gNB0->frame_parms = gNB_RC->frame_parms;
        // attach all RU to all gNBs in its list/
        LOG_D(PHY,"ru->num_gNB:%d gNB0->num_RU:%d\n", ru->num_gNB, gNB0->num_RU);
        for (int i = 0; i < ru->num_gNB; i++) {
          gNB0 = ru->gNB_list[i];
          gNB0->RU_list[gNB0->num_RU++] = ru;
        }
      }
    }

    set_function_spec_param(ru);

    // init RU_proc -> needed for timing alignment
    ru->proc = (RU_proc_t){.ru = ru, .first_rx = 1, .first_tx = 1};

    if (ru->if_south != REMOTE_IF4p5) {
      int threadCnt = ru->num_tpcores;
      if (threadCnt < 2)
        LOG_E(PHY, "Number of threads for gNB should be more than 1. Allocated only %d\n", threadCnt);
      char pool[80];
      int s_offset = sprintf(pool,"%d",ru->tpcores[0]);
      for (int icpu = 1; icpu < threadCnt; icpu++) {
        s_offset += sprintf(pool + s_offset, ",%d", ru->tpcores[icpu]);
      }
      LOG_I(PHY, "RU thread-pool core string %s (size %d)\n", pool, threadCnt);
      ru->threadPool = malloc(sizeof(tpool_t));
      initTpool(pool, ru->threadPool, cpumeas(CPUMEAS_GETSTATE));
      // FEP RX result FIFO
      ru->respfeprx = malloc(sizeof(notifiedFIFO_t));
      initNotifiedFIFO(ru->respfeprx);
      // FEP TX result FIFO
      ru->respfeptx = malloc(sizeof(notifiedFIFO_t));
      initNotifiedFIFO(ru->respfeptx);
    }
  } // for ru_id

  LOG_D(HW,"[nr-softmodem.c] RU threads created\n");
}

void start_NR_RU()
{
  RU_t *ru = RC.ru[0];
  threadCreate(&ru->proc.pthread_FH, ru_thread, ru, "ru_thread", ru->ru_thread_core, OAI_PRIORITY_RT_MAX);
}

void stop_RU(int nb_ru) {
  for (int inst = 0; inst < nb_ru; inst++) {
    LOG_I(PHY, "Stopping RU %d processing threads\n", inst);
    kill_NR_RU_proc(inst);
  }
}

/* --------------------------------------------------------*/
/* from here function to use configuration module          */
static void NRRCconfig_RU(configmodule_interface_t *cfg)
{
  paramdef_t RUParams[] = RUPARAMS_DESC;
  paramlist_def_t RUParamList = {CONFIG_STRING_RU_LIST, NULL, 0};
  config_getlist(cfg, &RUParamList, RUParams, sizeofArray(RUParams), NULL);

  if (RUParamList.numelt <= 0)
    return;

  RC.ru = (RU_t **)malloc(RC.nb_RU * sizeof(RU_t *));
  RC.ru_mask = (1 << RC.nb_RU) - 1;

  for (int j = 0; j < RC.nb_RU; j++) {
    RU_t *ru = RC.ru[j] = calloc(1, sizeof(*RC.ru[j]));
    ru->idx = j;
    ru->nr_frame_parms = calloc(1, sizeof(*ru->nr_frame_parms));
    paramdef_t *param = RUParamList.paramarray[j];
    if (RC.nb_nr_L1_inst > 0)
      ru->num_gNB = param[RU_ENB_LIST_IDX].numelt;
    else
      ru->num_gNB = 0;

    for (int i = 0; i < ru->num_gNB; i++)
      ru->gNB_list[i] = RC.gNB[param[RU_ENB_LIST_IDX].iptr[i]];

    if (config_isparamset(param, RU_SDR_ADDRS)) {
      ru->openair0_cfg.sdr_addrs = strdup(*param[RU_SDR_ADDRS].strptr);
    }

    if (config_isparamset(param, RU_GPIO_CONTROL)) {
      char *str = *param[RU_GPIO_CONTROL].strptr;
      if (strcmp(str, "generic") == 0) {
        ru->openair0_cfg.gpio_controller = RU_GPIO_CONTROL_GENERIC;
        LOG_I(PHY, "RU GPIO control set as 'generic'\n");
      } else if (strcmp(str, "interdigital") == 0) {
        ru->openair0_cfg.gpio_controller = RU_GPIO_CONTROL_INTERDIGITAL;
        LOG_I(PHY, "RU GPIO control set as 'interdigital'\n");
      } else {
        AssertFatal(false, "bad GPIO controller in configuration file: '%s'\n", str);
      }
    } else
      ru->openair0_cfg.gpio_controller = RU_GPIO_CONTROL_NONE;

    if (config_isparamset(param, RU_TX_SUBDEV)) {
      ru->openair0_cfg.tx_subdev = strdup(*param[RU_TX_SUBDEV].strptr);
      LOG_I(PHY, "RU USRP tx subdev == %s\n", ru->openair0_cfg.tx_subdev);
    }

    if (config_isparamset(param, RU_RX_SUBDEV)) {
      ru->openair0_cfg.rx_subdev = strdup(*param[RU_RX_SUBDEV].strptr);
      LOG_I(PHY, "RU USRP rx subdev == %s\n", ru->openair0_cfg.rx_subdev);
    }

    if (config_isparamset(param, RU_SDR_CLK_SRC)) {
      char *str = *param[RU_SDR_CLK_SRC].strptr;
      if (strcmp(str, "internal") == 0) {
        ru->openair0_cfg.clock_source = internal;
        LOG_I(PHY, "RU clock source set as internal\n");
      } else if (strcmp(str, "external") == 0) {
        ru->openair0_cfg.clock_source = external;
        LOG_I(PHY, "RU clock source set as external\n");
      } else if (strcmp(str, "gpsdo") == 0) {
        ru->openair0_cfg.clock_source = gpsdo;
        LOG_I(PHY, "RU clock source set as gpsdo\n");
      } else {
        LOG_E(PHY, "Erroneous RU clock source in the provided configuration file: '%s'\n", str);
      }
    } else {
      LOG_D(PHY, "Setting clock source to internal\n");
      ru->openair0_cfg.clock_source = internal;
    }

    if (config_isparamset(param, RU_SDR_TME_SRC)) {
      char *str = *param[RU_SDR_TME_SRC].strptr;
      if (strcmp(str, "internal") == 0) {
        ru->openair0_cfg.time_source = internal;
        LOG_I(PHY, "RU time source set as internal\n");
      } else if (strcmp(str, "external") == 0) {
        ru->openair0_cfg.time_source = external;
        LOG_I(PHY, "RU time source set as external\n");
      } else if (strcmp(str, "gpsdo") == 0) {
        ru->openair0_cfg.time_source = gpsdo;
        LOG_I(PHY, "RU time source set as gpsdo\n");
      } else {
        LOG_E(PHY, "Erroneous RU time source in the provided configuration file: '%s'\n", str);
      }
    } else {
      LOG_D(PHY, "Setting time source to internal\n");
      ru->openair0_cfg.time_source = internal;
    }

    ru->openair0_cfg.tune_offset = get_softmodem_params()->tune_offset;

    if (strcmp(*param[RU_LOCAL_RF_IDX].strptr, "yes") == 0) {
      AssertFatal(!config_isparamset(param, RU_LOCAL_IF_NAME_IDX), "RU_TRANSPORT_PREFERENCE not supported for local RF\n");
      ru->if_south = LOCAL_RF;
      ru->function = gNodeB_3GPP;
      LOG_D(PHY, "Setting function for RU %d to gNodeB_3GPP\n", j);
      ru->max_pdschReferenceSignalPower = *param[RU_MAX_RS_EPRE_IDX].uptr;
      ru->max_rxgain = *param[RU_MAX_RXGAIN_IDX].uptr;
      ru->sf_extension = *param[RU_SF_EXTENSION_IDX].uptr;
    } else {
      char *str = *param[RU_TRANSPORT_PREFERENCE_IDX].strptr;
      LOG_D(PHY, "RU %d: Transport %s\n", j, str);
      ru->ifdevice.eth_params.local_if_name = strdup(*param[RU_LOCAL_IF_NAME_IDX].strptr);
      ru->ifdevice.eth_params.my_addr = strdup(*param[RU_LOCAL_ADDRESS_IDX].strptr);
      ru->ifdevice.eth_params.remote_addr = strdup(*param[RU_REMOTE_ADDRESS_IDX].strptr);
      ru->ifdevice.eth_params.my_portc = *param[RU_LOCAL_PORTC_IDX].uptr;
      ru->ifdevice.eth_params.remote_portc = *param[RU_REMOTE_PORTC_IDX].uptr;
      ru->ifdevice.eth_params.my_portd = *param[RU_LOCAL_PORTD_IDX].uptr;
      ru->ifdevice.eth_params.remote_portd = *param[RU_REMOTE_PORTD_IDX].uptr;

      if (strcmp(str, "udp") == 0) {
        ru->if_south = REMOTE_IF5;
        ru->function = NGFI_RAU_IF5;
        ru->ifdevice.eth_params.transp_preference = ETH_UDP_MODE;
      } else if (strcmp(str, "udp_ecpri_if5") == 0) {
        ru->if_south = REMOTE_IF5;
        ru->function = NGFI_RAU_IF5;
        ru->ifdevice.eth_params.transp_preference = ETH_UDP_IF5_ECPRI_MODE;
      } else if (strcmp(str, "raw") == 0) {
        ru->if_south = REMOTE_IF5;
        ru->function = NGFI_RAU_IF5;
        ru->ifdevice.eth_params.transp_preference = ETH_RAW_MODE;
      } else if (strcmp(str, "raw_if4p5") == 0) {
        ru->if_south = REMOTE_IF4p5;
        ru->function = NGFI_RAU_IF4p5;
      }
    }

    ru->nb_tx = *param[RU_NB_TX_IDX].uptr;
    ru->nb_rx = *param[RU_NB_RX_IDX].uptr;
    ru->att_tx = *param[RU_ATT_TX_IDX].uptr;
    ru->att_rx = *param[RU_ATT_RX_IDX].uptr;
    ru->if_frequency = *param[RU_IF_FREQUENCY].u64ptr;
    ru->if_freq_offset = *param[RU_IF_FREQ_OFFSET].iptr;
    ru->sl_ahead = *param[RU_SL_AHEAD].iptr;
    ru->num_bands = param[RU_BAND_LIST_IDX].numelt;
    for (int i = 0; i < ru->num_bands; i++)
      ru->band[i] = param[RU_BAND_LIST_IDX].iptr[i];
    // TODO remove band from RU?
    ru->openair0_cfg.rxfh_cores[0] = *param[RU_RXFH_CORE_ID].iptr;
    ru->openair0_cfg.txfh_cores[0] = *param[RU_TXFH_CORE_ID].iptr;
    ru->num_tpcores = *param[RU_NUM_TP_CORES].iptr;
    ru->half_slot_parallelization = *param[RU_HALF_SLOT_PARALLELIZATION].iptr;
    ru->ru_thread_core = *param[RU_RU_THREAD_CORE].iptr;
    LOG_D(PHY, "[RU %d] Setting half-slot parallelization to %d\n", j, ru->half_slot_parallelization);
    AssertFatal(ru->num_tpcores <= param[RU_TP_CORES].numelt, "Number of TP cores should be <=16\n");
    for (int i = 0; i < ru->num_tpcores; i++)
      ru->tpcores[i] = param[RU_TP_CORES].iptr[i];
  } // j=0..num_rus
  return;
}

