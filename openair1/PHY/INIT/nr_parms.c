/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_phy_init.h"
#include "common/utils/nr/nr_common.h"
#include "common/utils/LOG/log.h"
#include "executables/softmodem-common.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"

/// Subcarrier spacings in Hz indexed by numerology index
static const uint32_t nr_subcarrier_spacing[MAX_NUM_SUBCARRIER_SPACING] = {15e3, 30e3, 60e3, 120e3, 240e3};
static const uint16_t nr_slots_per_subframe[MAX_NUM_SUBCARRIER_SPACING] = {1, 2, 4, 8, 16};

void set_Lmax(NR_DL_FRAME_PARMS *fp) {
  // definition of Lmax according to ts 38.213 section 4.1
  if (fp->dl_CarrierFreq < 6e9) {
    if(fp->frame_type && (fp->ssb_type==2))
      fp->Lmax = (fp->dl_CarrierFreq < 2.4e9)? 4 : 8;
    else
      fp->Lmax = (fp->dl_CarrierFreq < 3e9)? 4 : 8;
  } else {
    fp->Lmax = 64;
  }
}

int nr_get_ssb_start_symbol(const NR_DL_FRAME_PARMS *fp, uint8_t i_ssb)
{
  int mu = fp->numerology_index;
  int symbol = 0;
  uint8_t n, n_temp;
  nr_ssb_type_e type = fp->ssb_type;
  int case_AC[2] = {2,8};
  int case_BD[4] = {4,8,16,20};
  int case_E[8] = {8, 12, 16, 20, 32, 36, 40, 44};

  switch(mu) {
    case NR_MU_0: // case A
      n = i_ssb >> 1;
      symbol = case_AC[i_ssb % 2] + 14*n;
      break;
    case NR_MU_1:
      if (type == 1){ // case B
        n = i_ssb >> 2;
        symbol = case_BD[i_ssb % 4] + 28*n;
       }
       if (type == 2){ // case C
         n = i_ssb >> 1;
         symbol = case_AC[i_ssb % 2] + 14*n;
       }
       break;
     case NR_MU_3: // case D
       n_temp = i_ssb >> 2;
       n = n_temp + (n_temp >> 2);
       symbol = case_BD[i_ssb % 4] + 28*n;
       break;
     case NR_MU_4:  // case E
       n_temp = i_ssb >> 3;
       n = n_temp + (n_temp >> 2);
       symbol = case_E[i_ssb % 8] + 56*n;
       break;
     default:
       AssertFatal(0==1, "Invalid numerology index %d for the synchronization block\n", mu);
  }

  return symbol;
}

void set_scs_parameters(NR_DL_FRAME_PARMS *fp, int mu, int N_RB_DL, int ssb_case)
{
  switch(mu) {
    case NR_MU_0: //15kHz scs
      fp->subcarrier_spacing = nr_subcarrier_spacing[NR_MU_0];
      fp->slots_per_subframe = nr_slots_per_subframe[NR_MU_0];
      break;
    case NR_MU_1: //30kHz scs
      fp->subcarrier_spacing = nr_subcarrier_spacing[NR_MU_1];
      fp->slots_per_subframe = nr_slots_per_subframe[NR_MU_1];
      break;
    case NR_MU_2: //60kHz scs
      fp->subcarrier_spacing = nr_subcarrier_spacing[NR_MU_2];
      fp->slots_per_subframe = nr_slots_per_subframe[NR_MU_2];
      break;
    case NR_MU_3:
      fp->subcarrier_spacing = nr_subcarrier_spacing[NR_MU_3];
      fp->slots_per_subframe = nr_slots_per_subframe[NR_MU_3];
      break;
    case NR_MU_4:
      fp->subcarrier_spacing = nr_subcarrier_spacing[NR_MU_4];
      fp->slots_per_subframe = nr_slots_per_subframe[NR_MU_4];
      break;
    default:
      AssertFatal(false, "Invalid numerology index %d", mu);
  }

  fp->ssb_type = ssb_case;
  // Start with FFT size 512
  fp->ofdm_symbol_size = 512;

  // Choose the right FFT size for the BW
  while(fp->ofdm_symbol_size < N_RB_DL * NR_NB_SC_PER_RB)
    fp->ofdm_symbol_size <<= 1;

  // Do 3/4 sampling
  if (fp->threequarter_fs) {
    const uint16_t threeq_fft_size = fp->ofdm_symbol_size * 3 / 4;
    if (threeq_fft_size < (N_RB_DL * NR_NB_SC_PER_RB)) {
      // 3/4 sampling FFT size not enough
      warn_higher_threequarter_fs(N_RB_DL, fp->numerology_index);
      // Choose 2 times 3/4 sampling
      fp->ofdm_symbol_size = threeq_fft_size << 1;
    } else {
      fp->ofdm_symbol_size = threeq_fft_size;
    }
  }

  fp->first_carrier_offset = fp->ofdm_symbol_size - (N_RB_DL * 12 / 2);
  fp->nb_prefix_samples    = fp->ofdm_symbol_size / 128 * 9;
  fp->nb_prefix_samples0   = fp->ofdm_symbol_size / 128 * (9 + (1 << mu));
  LOG_I(PHY,
        "Init: N_RB_DL %d, first_carrier_offset %d, nb_prefix_samples %d,nb_prefix_samples0 %d, ofdm_symbol_size %d\n",
        N_RB_DL,
        fp->first_carrier_offset,
        fp->nb_prefix_samples,
        fp->nb_prefix_samples0,
        fp->ofdm_symbol_size);
}

void sl_set_scs_parameters(NR_DL_FRAME_PARMS *fp, int mu, int N_RB_SL)
{

  AssertFatal(mu >= NR_MU_0 && mu <= NR_MU_4,"Invalid numerology index %d", mu);

  fp->subcarrier_spacing = nr_subcarrier_spacing[mu];
  fp->slots_per_subframe = nr_slots_per_subframe[mu];

  if(fp->threequarter_fs)
    fp->ofdm_symbol_size = 3 * 128;
  else
    fp->ofdm_symbol_size = 4 * 128;

  while(fp->ofdm_symbol_size < N_RB_SL * 12)
    fp->ofdm_symbol_size <<= 1;

  fp->first_carrier_offset = fp->ofdm_symbol_size - (N_RB_SL * 12 / 2);
  fp->nb_prefix_samples    = fp->ofdm_symbol_size / 128 * 9;
  fp->nb_prefix_samples0   = fp->ofdm_symbol_size / 128 * (9 + (1 << mu));
  LOG_I(PHY,
        "Init: N_RB_SL %d, first_carrier_offset %d, nb_prefix_samples %d,nb_prefix_samples0 %d, ofdm_symbol_size %d\n",
        N_RB_SL,
        fp->first_carrier_offset,
        fp->nb_prefix_samples,
        fp->nb_prefix_samples0,
        fp->ofdm_symbol_size);
}

uint32_t get_samples_per_slot(int slot, const NR_DL_FRAME_PARMS *fp)
{
  uint32_t samp_count;

  if(fp->numerology_index == 0)
    samp_count = fp->samples_per_subframe;
  else
    samp_count = (slot % (fp->slots_per_subframe / 2)) ? fp->samples_per_slotN0 : fp->samples_per_slot0;

  return samp_count;
}

uint32_t get_samples_symbol_duration(const NR_DL_FRAME_PARMS *fp, int slot, int start_symbol, int num_symbols)
{
  int end_symbol = start_symbol + num_symbols - 1;
  AssertFatal(start_symbol <= end_symbol && start_symbol >= 0 && end_symbol < fp->symbols_per_slot,
              "Symbol range is invalid start_symbol %d, num_symbols %d symbols_per_slot %d\n",
              start_symbol,
              num_symbols,
              fp->symbols_per_slot);
  if (num_symbols == fp->symbols_per_slot) {
    return get_samples_per_slot(slot, fp);
  }
  uint32_t num_samples = 0;
  int num_symbols_added = 0;

  // Handle symbols with different nb_prefix_samples0
  if (fp->numerology_index == 0) {
    // Add symbol 0
    if (start_symbol == 0) {
      num_samples += fp->nb_prefix_samples0 + fp->ofdm_symbol_size;
      num_symbols_added++;
    }
    // Add symbol 7
    if (start_symbol <= 7 && end_symbol >= 7) {
      num_samples += fp->nb_prefix_samples0 + fp->ofdm_symbol_size;
      num_symbols_added++;
    }
  } else {
    // Add first symbol
    if (start_symbol == 0) {
      num_samples += (slot % (fp->slots_per_subframe / 2)) ? fp->nb_prefix_samples : fp->nb_prefix_samples0;
      num_samples += fp->ofdm_symbol_size;
      num_symbols_added++;
    }
  }

  int num_symbols_left = max(0, num_symbols - num_symbols_added);
  num_samples += num_symbols_left * (fp->nb_prefix_samples + fp->ofdm_symbol_size);
  return num_samples;
}

uint32_t get_samples_symbol_timestamp(const NR_DL_FRAME_PARMS *fp, int slot, int symbol)
{
  if (symbol == 0) {
    return 0;
  }

  return get_samples_symbol_duration(fp, slot, 0, symbol);
}

uint32_t get_slot_from_timestamp(openair0_timestamp_t timestamp_rx, const NR_DL_FRAME_PARMS *fp)
{
  timestamp_rx = timestamp_rx % fp->samples_per_frame;
  if (timestamp_rx < fp->samples_per_slot0)
    return 0;

  if (fp->numerology_index == 0)
    return timestamp_rx / fp->samples_per_subframe;

  const unsigned int num_half_sf = timestamp_rx / (fp->samples_per_subframe / 2);
  const unsigned int slot_per_half_sf = fp->slots_per_subframe / 2;

  uint32_t slot_count = num_half_sf * slot_per_half_sf;
  int samp_idx = num_half_sf * (fp->samples_per_subframe / 2) - 1;

  // Remaining samples
  if (samp_idx < timestamp_rx) {
    slot_count++;
    samp_idx += fp->samples_per_slot0; // First slot of half subframe
  }
  if (samp_idx < timestamp_rx) {
    const unsigned int rem_samples = timestamp_rx - samp_idx;
    const unsigned int num_slots = ceil((double)rem_samples / fp->samples_per_slotN0);
    slot_count += num_slots;
  }

  return slot_count - 1;
}

uint32_t get_samples_slot_timestamp(const NR_DL_FRAME_PARMS *fp, unsigned int slot)
{
  if (slot == 0)
    return 0;

  if (fp->numerology_index == 0)
    return slot * fp->samples_per_subframe;

  uint32_t samp_count = 0;
  // Number of half subframes
  const unsigned int num_half_sf = slot / (fp->slots_per_subframe / 2);
  samp_count += num_half_sf * fp->samples_per_subframe / 2;

  // Remaining slots
  unsigned int rem_slots = slot % (fp->slots_per_subframe / 2);
  if (rem_slots > 0) {
    samp_count += fp->samples_per_slot0; // First slot of the last half subframe
    rem_slots--;
  }
  samp_count += rem_slots * fp->samples_per_slotN0; // Remaining slots of the last half subframe

  return samp_count;
}

uint32_t get_samples_slot_duration(const NR_DL_FRAME_PARMS *fp, unsigned int start_slot, unsigned int num_slots)
{
  if (start_slot == 0 && num_slots == 0)
    return 0;

  return (get_samples_slot_timestamp(fp, start_slot + num_slots) - get_samples_slot_timestamp(fp, start_slot));
}

void nr_init_frame_parms(nfapi_nr_config_request_scf_t* cfg, NR_DL_FRAME_PARMS *fp)
{

  AssertFatal (cfg, "Null pointer to cfg!\n");

  fp->frame_type = cfg->cell_config.frame_duplex_type.value;
  fp->Nid_cell = cfg->cell_config.phy_cell_id.value;
  fp->L_ssb = (((uint64_t) cfg->ssb_table.ssb_mask_list[0].ssb_mask.value) << 32) | cfg->ssb_table.ssb_mask_list[1].ssb_mask.value;
  fp->N_RB_DL = cfg->carrier_config.dl_grid_size[cfg->ssb_config.scs_common.value].value;
  fp->N_RB_UL = cfg->carrier_config.ul_grid_size[cfg->ssb_config.scs_common.value].value;

  int Ncp = NFAPI_CP_NORMAL;
  int mu = cfg->ssb_config.scs_common.value;

  LOG_I(PHY,"Initializing frame parms for mu %d, N_RB %d, Ncp %d\n", mu, fp->N_RB_DL, Ncp);

  if (Ncp == NFAPI_CP_EXTENDED)
    AssertFatal(mu == NR_MU_2,"Invalid cyclic prefix %d for numerology index %d\n", Ncp, mu);

  fp->half_frame_bit = 0;  // half frame bit initialized to 0 here
  fp->numerology_index = mu;

  set_scs_parameters(fp, mu, fp->N_RB_DL, cfg->ssb_table.case_v3.value);

  fp->slots_per_frame = 10* fp->slots_per_subframe;

  fp->nb_antennas_rx = cfg->carrier_config.num_rx_ant.value;      // It denotes the number of rx antennas at gNB
  fp->nb_antennas_tx = cfg->carrier_config.num_tx_ant.value;      // It corresponds to pdsch_AntennaPorts (logical antenna ports)

  fp->symbols_per_slot = ((Ncp == NR_NORMAL)? 14 : 12); // to redefine for different slot formats
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot*fp->ofdm_symbol_size; 
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_slot0 = fp->numerology_index == 0 ? fp->samples_per_subframe
                                                    : fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples)
                                                          + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = get_freq_range_from_freq(fp->dl_CarrierFreq);

  fp->Ncp = Ncp;

  set_Lmax(fp);

  fp->N_ssb = 0;
  int num_tx_ant = cfg->carrier_config.num_tx_ant.value;

  for (int p=0; p<num_tx_ant; p++)
    fp->N_ssb += ((fp->L_ssb >> (63-p)) & 0x01);

  fp->print_ue_help_cmdline_log = true;
}

int nr_init_frame_parms_ue(NR_DL_FRAME_PARMS *fp, fapi_nr_config_request_t* config, uint16_t nr_band)
{

  uint8_t nb_ant_ports_gNB  = 1;
  uint8_t tdd_cfg           = 3;
  uint8_t Nid_cell          = 0;
  int Ncp = NR_NORMAL;

  if(fp->nb_antennas_rx == 0)
    fp->nb_antennas_rx = 1;
  if(fp->nb_antennas_tx == 0)
    fp->nb_antennas_tx = 1;

  // default values until overwritten by RRCConnectionReconfiguration
  fp->nb_antenna_ports_gNB = nb_ant_ports_gNB;
  fp->tdd_config           = tdd_cfg;
  fp->Nid_cell             = Nid_cell;

  LOG_I(PHY, "Initializing frame parms: set nb_antenna_ports_gNB %d, tdd_config, %d, Nid_cell %d\n", fp->nb_antenna_ports_gNB, fp->tdd_config, fp->Nid_cell);

  uint64_t dl_bw_khz = (12*config->carrier_config.dl_grid_size[config->ssb_config.scs_common])*(15<<config->ssb_config.scs_common);
  fp->dl_CarrierFreq = ((dl_bw_khz>>1) + config->carrier_config.dl_frequency)*1000 ;

  LOG_D(PHY,"dl_bw_kHz %lu\n",dl_bw_khz);
  LOG_D(PHY,"dl_CarrierFreq %lu\n",fp->dl_CarrierFreq);

  uint64_t ul_bw_khz = (12*config->carrier_config.ul_grid_size[config->ssb_config.scs_common])*(15<<config->ssb_config.scs_common);
  fp->ul_CarrierFreq = ((ul_bw_khz>>1) + config->carrier_config.uplink_frequency)*1000 ;

  fp->numerology_index = config->ssb_config.scs_common;
  fp->N_RB_UL = config->carrier_config.ul_grid_size[fp->numerology_index];
  fp->N_RB_DL = config->carrier_config.dl_grid_size[fp->numerology_index];
  fp->N_RB_SL = config->carrier_config.sl_grid_size[fp->numerology_index];

  fp->frame_type = get_frame_type(nr_band, fp->numerology_index);
  int64_t uplink_frequency_offset = fp->ul_CarrierFreq - fp->dl_CarrierFreq;
  uplink_frequency_offset *= 1000;

  LOG_I(PHY,
        "Initializing frame parms: DL frequency %lu Hz, UL frequency %lu Hz: band %d, uldl offset %ld Hz\n",
        fp->dl_CarrierFreq,
        fp->ul_CarrierFreq,
        nr_band,
        uplink_frequency_offset);

  AssertFatal(fp->frame_type == config->cell_config.frame_duplex_type,
              "Invalid duplex type (frame_type %d,cell_config.frame_duplex_type %d) in config request file for band %d\n",
              fp->frame_type,config->cell_config.frame_duplex_type,
              nr_band);

  LOG_I(PHY,"Initializing frame parms for mu %d, N_RB %d, Ncp %d\n",fp->numerology_index, fp->N_RB_DL, Ncp);


  if (Ncp == NFAPI_CP_EXTENDED)
    AssertFatal(fp->numerology_index == NR_MU_2,"Invalid cyclic prefix %d for numerology index %d\n", Ncp, fp->numerology_index);

  fp->Ncp = Ncp;
  int N_RB = fp->N_RB_DL;
  set_scs_parameters(fp, fp->numerology_index, N_RB, config->ssb_table.ssb_case);

  fp->slots_per_frame = 10* fp->slots_per_subframe;
  fp->symbols_per_slot = ((Ncp == NR_NORMAL)? NR_SYMBOLS_PER_SLOT : NR_SYMBOLS_PER_SLOT_EXTENDED_CP);
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot*fp->ofdm_symbol_size; 
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_slot0 = fp->numerology_index == 0 ? fp->samples_per_subframe
                                                    : fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples)
                                                          + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = get_freq_range_from_freq(fp->dl_CarrierFreq);

  fp->ssb_start_subcarrier = nr_get_ssb_start_sc(config->ssb_config.scs_common,
                                                 config->ssb_table.ssb_offset_point_a,
                                                 config->ssb_table.ssb_subcarrier_offset,
                                                 fp->freq_range);

  set_Lmax(fp);

  fp->L_ssb = (((uint64_t) config->ssb_table.ssb_mask_list[0].ssb_mask)<<32) | config->ssb_table.ssb_mask_list[1].ssb_mask;
  
  fp->N_ssb = 0;
  for (int p=0; p<fp->Lmax; p++)
    fp->N_ssb += ((fp->L_ssb >> (63-p)) & 0x01);

  return 0;
}

void nr_init_frame_parms_ue_sa(NR_DL_FRAME_PARMS *frame_parms, const nrUE_cell_params_t *cell)
{
  const uint64_t downlink_frequency = cell->rf_frequency;
  const int64_t delta_duplex = cell->rf_freq_offset;
  const uint8_t mu = cell->numerology;
  const int N_RB_DL = cell->N_RB_DL;
  const int ssb_start_subcarrier = cell->ssb_start;
  const uint16_t nr_band = cell->band;

  LOG_I(PHY,"SA init parameters. DL freq %lu UL offset %ld SSB numerology %d N_RB_DL %d\n",
        downlink_frequency,
        delta_duplex,
        mu,
        N_RB_DL);

  frame_parms->numerology_index = mu;
  frame_parms->dl_CarrierFreq = downlink_frequency;
  frame_parms->ul_CarrierFreq = downlink_frequency + delta_duplex;
  if (get_softmodem_params()->sl_mode == 0) {
    frame_parms->freq_range = get_freq_range_from_freq(frame_parms->dl_CarrierFreq);
  }
  frame_parms->N_RB_DL = N_RB_DL;
  frame_parms->N_RB_UL = frame_parms->N_RB_DL;

  frame_parms->frame_type = get_frame_type(nr_band, frame_parms->numerology_index);

  frame_parms->Ncp = NR_NORMAL;
  set_scs_parameters(frame_parms, frame_parms->numerology_index, frame_parms->N_RB_DL, set_ssb_case(mu, nr_band));
  set_Lmax(frame_parms);

  frame_parms->slots_per_frame = 10* frame_parms->slots_per_subframe;
  frame_parms->symbols_per_slot = ((frame_parms->Ncp == NR_NORMAL)? NR_SYMBOLS_PER_SLOT : NR_SYMBOLS_PER_SLOT_EXTENDED_CP);
  frame_parms->samples_per_subframe_wCP = frame_parms->ofdm_symbol_size * frame_parms->symbols_per_slot * frame_parms->slots_per_subframe;
  frame_parms->samples_per_frame_wCP = 10 * frame_parms->samples_per_subframe_wCP;
  frame_parms->samples_per_slot_wCP = frame_parms->symbols_per_slot*frame_parms->ofdm_symbol_size;
  frame_parms->samples_per_slotN0 = (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size) * frame_parms->symbols_per_slot;
  frame_parms->samples_per_subframe = (frame_parms->nb_prefix_samples0 + frame_parms->ofdm_symbol_size) * 2
                                      + (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size)
                                            * (frame_parms->symbols_per_slot * frame_parms->slots_per_subframe - 2);
  frame_parms->samples_per_slot0 = frame_parms->numerology_index == 0
                                       ? frame_parms->samples_per_subframe
                                       : frame_parms->nb_prefix_samples0
                                             + ((frame_parms->symbols_per_slot - 1) * frame_parms->nb_prefix_samples)
                                             + (frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size);
  frame_parms->samples_per_frame = 10 * frame_parms->samples_per_subframe;

  frame_parms->ssb_start_subcarrier = ssb_start_subcarrier;

  LOG_W(PHY, "samples_per_subframe %d/per second %d, wCP %d\n", frame_parms->samples_per_subframe, 1000*frame_parms->samples_per_subframe, frame_parms->samples_per_subframe_wCP);
}

void nr_dump_frame_parms(NR_DL_FRAME_PARMS *fp)
{
  LOG_I(PHY,"fp->scs=%d\n",fp->subcarrier_spacing);
  LOG_I(PHY,"fp->ofdm_symbol_size=%d\n",fp->ofdm_symbol_size);
  LOG_I(PHY,"fp->nb_prefix_samples0=%d\n",fp->nb_prefix_samples0);
  LOG_I(PHY,"fp->nb_prefix_samples=%d\n",fp->nb_prefix_samples);
  LOG_I(PHY,"fp->slots_per_subframe=%d\n",fp->slots_per_subframe);
  LOG_I(PHY,"fp->samples_per_subframe_wCP=%d\n",fp->samples_per_subframe_wCP);
  LOG_I(PHY,"fp->samples_per_frame_wCP=%d\n",fp->samples_per_frame_wCP);
  LOG_I(PHY,"fp->samples_per_subframe=%d\n",fp->samples_per_subframe);
  LOG_I(PHY,"fp->samples_per_frame=%d\n",fp->samples_per_frame);
  LOG_I(PHY,"fp->dl_CarrierFreq=%lu\n",fp->dl_CarrierFreq);
  LOG_I(PHY,"fp->ul_CarrierFreq=%lu\n",fp->ul_CarrierFreq);
  LOG_I(PHY, "fp->Nid_cell=%d\n", fp->Nid_cell);
  LOG_I(PHY, "fp->first_carrier_offset=%d\n", fp->first_carrier_offset);
  LOG_I(PHY, "fp->ssb_start_subcarrier=%d\n", fp->ssb_start_subcarrier);
  LOG_I(PHY, "fp->Ncp=%d\n", fp->Ncp);
  LOG_I(PHY, "fp->N_RB_DL=%d\n", fp->N_RB_DL);
  LOG_I(PHY, "fp->numerology_index=%d\n", fp->numerology_index);
  LOG_I(PHY, "fp->ofdm_offset_divisor=%d\n", fp->ofdm_offset_divisor);
  LOG_I(PHY, "fp->threequarter_fs=%d\n", fp->threequarter_fs);
  LOG_I(PHY, "fp->sl_CarrierFreq=%lu\n", fp->sl_CarrierFreq);
  LOG_I(PHY, "fp->N_RB_SL=%d\n", fp->N_RB_SL);
}

int nr_init_frame_parms_ue_sl(NR_DL_FRAME_PARMS *fp,
                              sl_nr_phy_config_request_t *config,
                              int threequarter_fs,
                              uint32_t ofdm_offset_divisor)
{
  // Set also these parameters here instead of some where else.
  fp->ofdm_offset_divisor = ofdm_offset_divisor;
  fp->threequarter_fs = threequarter_fs;

  fp->att_rx = 0;
  fp->att_tx = 0;
  fp->nb_antennas_rx = config->sl_carrier_config.sl_num_rx_ant;
  fp->nb_antennas_tx = config->sl_carrier_config.sl_num_tx_ant;

  fp->numerology_index = config->sl_bwp_config.sl_scs;
  fp->N_RB_SL = config->sl_carrier_config.sl_grid_size;
  fp->N_RB_DL = fp->N_RB_SL;
  fp->N_RB_UL = fp->N_RB_SL;
  fp->Ncp = config->sl_bwp_config.sl_cyclic_prefix;

  fp->frame_type = get_frame_type(config->sl_carrier_config.band, fp->numerology_index);

  uint64_t bw_khz = (12 * config->sl_carrier_config.sl_grid_size) * (15 << config->sl_bwp_config.sl_scs);
  // REfer to section 3GPP spec 38.101 5.4E.2.1
  // FrefV2x = Fref + deltashift + valueN*5Khz
  uint32_t deltashift = (config->sl_carrier_config.sl_frequency_shift_7p5khz) ? 7500 : 0; // In Hz
  deltashift += config->sl_carrier_config.sl_value_N * 5000; // In Hz
  fp->sl_CarrierFreq = ((bw_khz >> 1) + config->sl_carrier_config.sl_frequency);
  fp->sl_CarrierFreq += (deltashift >> 1);
  fp->dl_CarrierFreq = fp->sl_CarrierFreq;
  fp->ul_CarrierFreq = fp->sl_CarrierFreq;

  LOG_D(PHY, "bw_kHz %lu, deltashift:%d Hz\n", bw_khz, deltashift);
  LOG_D(PHY, "CarrierFreq %lu Hz\n", fp->sl_CarrierFreq);

  LOG_I(PHY,
        "Initializing frame parms: DL frequency %lu Hz, UL frequency %lu Hz SL frequency %lu Hz: band %d\n",
        fp->dl_CarrierFreq,
        fp->ul_CarrierFreq,
        fp->sl_CarrierFreq,
        config->sl_carrier_config.band);

  AssertFatal(fp->frame_type == TDD, "Sidelink bands only support TDD");


  LOG_I(PHY, "Initializing frame parms for mu %d, N_RB %d, Ncp %d\n", fp->numerology_index, fp->N_RB_DL, fp->Ncp);

  if (fp->Ncp == NR_EXTENDED)
    AssertFatal(fp->numerology_index == NR_MU_2,
                "Invalid cyclic prefix %d for numerology index %d\n",
                fp->Ncp,
                fp->numerology_index);

  sl_set_scs_parameters(fp, fp->numerology_index, fp->N_RB_SL);

  fp->slots_per_frame = 10 * fp->slots_per_subframe;
  fp->symbols_per_slot = ((fp->Ncp == NR_NORMAL) ? NR_SYMBOLS_PER_SLOT : NR_SYMBOLS_PER_SLOT_EXTENDED_CP);
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot * fp->ofdm_symbol_size;
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_slot0 = fp->numerology_index == 0 ? fp->samples_per_subframe
                                                    : fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples)
                                                          + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = get_freq_range_from_freq(fp->sl_CarrierFreq);

  // ssb_offset_pointa points to the first RE where Sidelink-PSBCH starts
  fp->ssb_start_subcarrier = config->sl_bwp_config.sl_ssb_offset_point_a;
  perform_symbol_rotation(fp->symbols_per_slot * fp->slots_per_frame / 10,
                          fp->numerology_index,
                          fp->sl_CarrierFreq,
                          fp->symbol_rotation[link_type_sl]);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_SL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);

  // Not used for Sidelink
  fp->Lmax = 0;
  fp->L_ssb = 0;
  fp->N_ssb = 0;
  fp->half_frame_bit = 0;
  fp->ssb_index = 0;
  fp->ssb_type = 0;

  LOG_I(PHY, "Dumping Sidelink Frame Parameters\n");
  nr_dump_frame_parms(fp);
  return 0;
}
