/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief      Functional helpers to configure the RF boards at UE side
 */

#include "common_lib.h"
#include "openair1/PHY/defs_nr_UE.h"
#include "openair1/PHY/phy_extern_nr_ue.h"
#include "nr_transport_proto_ue.h"

void nr_get_carrier_frequencies(const PHY_VARS_NR_UE *ue, uint64_t *dl_carrier, uint64_t *ul_carrier)
{
  const NR_DL_FRAME_PARMS *fp = &ue->frame_parms;
  if (ue->if_freq!=0) {
    *dl_carrier = ue->if_freq;
    *ul_carrier = *dl_carrier + ue->if_freq_off;
  }
  else{
    *dl_carrier = fp->dl_CarrierFreq;
    *ul_carrier = fp->ul_CarrierFreq;
  }
}

void nr_rf_card_config_gain(openair0_config_t *openair0_cfg)
{
  uint8_t mod_id     = 0;
  uint8_t cc_id      = 0;
  PHY_VARS_NR_UE *ue = nrPHY_vars_UE_g[mod_id][cc_id];
  int rf_chain       = ue->rf_map.chain;
  double rx_gain     = ue->rx_total_gain_dB;
  double tx_gain     = ue->tx_total_gain_dB;

  for (int i = rf_chain; i < rf_chain + 4; i++) {

    if (tx_gain)
      openair0_cfg->tx_gain[i] = tx_gain;
    if (rx_gain)
      openair0_cfg->rx_gain[i] = rx_gain;

    openair0_cfg->autocal[i] = 1;

    if (i < openair0_cfg->rx_num_channels) {
      LOG_I(PHY, "HW: Configuring channel %d (rf_chain %d): setting tx_gain %.0f, rx_gain %.0f\n",
        i,
        rf_chain,
        openair0_cfg->tx_gain[i],
        openair0_cfg->rx_gain[i]);
    }

  }
}

void nr_rf_card_config_freq(openair0_config_t *openair0_cfg,
                            uint64_t ul_carrier,
                            uint64_t dl_carrier,
                            int freq_offset){

  uint8_t mod_id     = 0;
  uint8_t cc_id      = 0;
  PHY_VARS_NR_UE *ue = nrPHY_vars_UE_g[mod_id][cc_id];
  int rf_chain       = ue->rf_map.chain;
  double freq_scale  = (double)(dl_carrier + freq_offset) / dl_carrier;

  for (int i = rf_chain; i < rf_chain + 4; i++) {

    if (i < openair0_cfg->rx_num_channels)
      openair0_cfg->rx_freq[i + rf_chain] = dl_carrier * freq_scale;
    else
      openair0_cfg->rx_freq[i] = 0.0;

    if (i<openair0_cfg->tx_num_channels)
      openair0_cfg->tx_freq[i] = ul_carrier * freq_scale;
    else
      openair0_cfg->tx_freq[i] = 0.0;

    openair0_cfg->autocal[i] = 1;

    if (i < openair0_cfg->rx_num_channels) {
      LOG_I(PHY, "HW: Configuring channel %d (rf_chain %d): setting tx_freq %.0f Hz, rx_freq %.0f Hz, tune_offset %.0f\n",
        i,
        rf_chain,
        openair0_cfg->tx_freq[i],
        openair0_cfg->rx_freq[i],
        openair0_cfg->tune_offset);
    }

  }
}

void nr_rf_card_get_freq(const openair0_config_t *openair0_cfg, uint64_t *ul_freq, uint64_t *dl_freq, int channel_id)
{
  // Sanity check assuming max nof channels is 4.
  if (channel_id >= 4) {
    LOG_E(PHY, "HW: Invalid channel id %d. Max is 4\n", channel_id);
    return;
  }

  *ul_freq = openair0_cfg->tx_freq[channel_id];
  *dl_freq = openair0_cfg->rx_freq[channel_id];
}

void nr_sl_rf_card_config_freq(PHY_VARS_NR_UE *ue, openair0_config_t *openair0_cfg, int freq_offset) {

  for (int i = 0; i < openair0_cfg->rx_num_channels; i++) {
    openair0_cfg->rx_gain[ue->rf_map.chain + i] = ue->rx_total_gain_dB;
    if (ue->UE_scan_carrier == 1) {
      if (freq_offset >= 0)
        openair0_cfg->rx_freq[ue->rf_map.chain + i] += abs(freq_offset);
      else
        openair0_cfg->rx_freq[ue->rf_map.chain + i] -= abs(freq_offset);
      freq_offset=0;
    }
  }
}
