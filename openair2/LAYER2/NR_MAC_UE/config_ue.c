/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief UE configuration performed by RRC or as a consequence of RRC procedures
 */

#define _GNU_SOURCE

#include "mac_defs.h"
#include "NR_MAC-CellGroupConfig.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "common/utils/nr/nr_common.h"
#include "executables/softmodem-common.h"
#include "SCHED_NR/phy_frame_config_nr.h"
#include "RRC/NR_UE/verify_RRC.h"
#include "RRC/NR_UE/L2_interface_ue.h"
#include "oai_asn1.h"

#define ASIGN_P_VAL(dst, src) \
  do {                        \
    if (src)                  \
      dst = *src;             \
    else                      \
      dst = -1;               \
  } while (0)

// Build the list of all the valid/transmitted SSBs according to the config
static void build_ssb_list(NR_UE_MAC_INST_t *mac)
{
  // Create the list of transmitted SSBs
  memset(&mac->ssb_list, 0, sizeof(ssb_list_info_t));
  ssb_list_info_t *ssb_list = &mac->ssb_list;
  fapi_nr_config_request_t *cfg = &mac->phy_config.config_req;
  ssb_list->nb_tx_ssb = 0;

  for (int ssb_index = 0; ssb_index < MAX_NB_SSB; ssb_index++) {
    uint32_t curr_mask = cfg->ssb_table.ssb_mask_list[ssb_index / 32].ssb_mask;
    // check if if current SSB is transmitted
    if ((curr_mask >> (31 - (ssb_index % 32))) & 0x01) {
      ssb_list->nb_ssb_per_index[ssb_index] = ssb_list->nb_tx_ssb;
      ssb_list->nb_tx_ssb++;
    } else
      ssb_list->nb_ssb_per_index[ssb_index] = -1;
  }
}

static int get_ta_offset(long *n_TimingAdvanceOffset)
{
  if (!n_TimingAdvanceOffset)
    return -1;

  switch (*n_TimingAdvanceOffset) {
    case NR_ServingCellConfigCommonSIB__n_TimingAdvanceOffset_n0 :
      return 0;
    case NR_ServingCellConfigCommonSIB__n_TimingAdvanceOffset_n25600 :
      return 25600;
    case NR_ServingCellConfigCommonSIB__n_TimingAdvanceOffset_n39936 :
      return 39936;
    default :
      AssertFatal(false, "Invalid n-TimingAdvanceOffset\n");
  }
  return -1;
}

static void set_tdd_config_nr_ue(fapi_nr_tdd_table_t *tdd_table, const frame_structure_t *fs)
{
  tdd_table->tdd_period_in_slots = fs->numb_slots_period;
  // calloc: a slot only sets the fields of its own type (num_dl of an UL slot must read 0).
  tdd_table->max_tdd_periodicity_list = calloc_or_fail(fs->numb_slots_period, sizeof(*tdd_table->max_tdd_periodicity_list));

  const tdd_period_config_t *pc = &fs->period_cfg;
  for (int i = 0; i < fs->numb_slots_period; i++) {
    fapi_nr_max_tdd_periodicity_t *period_list = &tdd_table->max_tdd_periodicity_list[i];
    if (pc->tdd_slot_bitmap[i].slot_type == TDD_NR_DOWNLINK_SLOT) {
      period_list->num_dl_ul_symbols_list.num_dl = NR_SYMBOLS_PER_SLOT;
    }
    if (pc->tdd_slot_bitmap[i].slot_type == TDD_NR_UPLINK_SLOT) {
      period_list->num_dl_ul_symbols_list.num_ul = NR_SYMBOLS_PER_SLOT;
    }
    if (pc->tdd_slot_bitmap[i].slot_type == TDD_NR_MIXED_SLOT) {
      int dl_symb = pc->tdd_slot_bitmap[i].num_dl_symbols;
      int ul_symb = pc->tdd_slot_bitmap[i].num_ul_symbols;
      period_list->num_dl_ul_symbols_list.num_dl = dl_symb;
      period_list->num_dl_ul_symbols_list.num_ul = ul_symb;
    }
  }
}

static void config_common_ue_sa(NR_UE_MAC_INST_t *mac, NR_ServingCellConfigCommonSIB_t *scc, int cc_idP)
{
  fapi_nr_config_request_t *cfg = &mac->phy_config.config_req;
  mac->phy_config.Mod_id = mac->ue_id;
  mac->phy_config.CC_id = cc_idP;

  LOG_D(MAC, "Entering SA UE Config Common\n");

  // carrier config
  NR_FrequencyInfoDL_SIB_t *frequencyInfoDL = &scc->downlinkConfigCommon.frequencyInfoDL;
  AssertFatal(frequencyInfoDL->frequencyBandList.list.array[0]->freqBandIndicatorNR, "Field mandatory present for DL in SIB1\n");
  mac->nr_band = *frequencyInfoDL->frequencyBandList.list.array[0]->freqBandIndicatorNR;

  AssertFatal(mac->numerology == frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing, "DL and SSB subcarrierSpacing must be the same!\n");
  int bw_index = get_supported_band_index(frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
                                          mac->frequency_range,
                                          frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth);
  cfg->carrier_config.dl_bandwidth = get_supported_bw_mhz(mac->frequency_range, bw_index);

  /** Only set frequency if not already initialized (e.g., from handover reconfigurationWithSync)
  * MAC maintains its own frequency state, don't overwrite it with command-line parameter which
  * is related to the initial cell selection. */
  if (cfg->carrier_config.dl_frequency == 0) {
    // Initial cell selection: derive from command-line parameter
    uint64_t dl_bw_khz = (12 * frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth) *
                         (15 << frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing);
    cfg->carrier_config.dl_frequency = mac->dl_frequency / 1000 - (dl_bw_khz >> 1);
    LOG_I(NR_MAC,
          "[UE %d] Initial cell selection: dl_frequency=%u kHz (from command-line, band=%d, scs=%ld)\n",
          mac->ue_id,
          cfg->carrier_config.dl_frequency,
          mac->nr_band,
          frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing);
  }

  for (int i = 0; i < 5; i++) {
    if (i == frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing) {
      cfg->carrier_config.dl_grid_size[i] = frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;
      cfg->carrier_config.dl_k0[i] = frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier;
    }
    else {
      cfg->carrier_config.dl_grid_size[i] = 0;
      cfg->carrier_config.dl_k0[i] = 0;
    }
  }

  NR_FrequencyInfoUL_SIB_t *frequencyInfoUL = &scc->uplinkConfigCommon->frequencyInfoUL;
  mac->p_Max = frequencyInfoUL->p_Max ? *frequencyInfoUL->p_Max : INT_MIN;

  AssertFatal(mac->numerology == frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing, "UL and SSB subcarrierSpacing must be the same!\n");
  bw_index = get_supported_band_index(frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
                                      mac->frequency_range,
                                      frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth);
  cfg->carrier_config.uplink_bandwidth = get_supported_bw_mhz(mac->frequency_range, bw_index);

  /** Only set UL frequency if not already initialized (e.g., from handover reconfigurationWithSync)
   * MAC maintains its own frequency state, don't overwrite it with command-line parameter which
   * is related to the initial cell selection. */
  if (cfg->carrier_config.uplink_frequency == 0) {
    // Initial cell selection: derive from DL frequency or frequencyInfoUL->absoluteFrequencyPointA
    if (frequencyInfoUL->absoluteFrequencyPointA == NULL) {
      cfg->carrier_config.uplink_frequency = cfg->carrier_config.dl_frequency;
      LOG_I(NR_MAC,
            "Initial cell selection: uplink_frequency=%u kHz (from dl_frequency=%u kHz)\n",
            cfg->carrier_config.uplink_frequency,
            cfg->carrier_config.dl_frequency);
    } else {
      cfg->carrier_config.uplink_frequency = from_nrarfcn(*frequencyInfoUL->frequencyBandList->list.array[0]->freqBandIndicatorNR,
                                                          frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
                                                          *frequencyInfoUL->absoluteFrequencyPointA)
                                             / 1000; // freq in kHz
      LOG_I(NR_MAC,
            "Initial cell selection: uplink_frequency=%u kHz (from absoluteFrequencyPointA=%ld, band=%ld, scs=%ld)\n",
            cfg->carrier_config.uplink_frequency,
            *frequencyInfoUL->absoluteFrequencyPointA,
            *frequencyInfoUL->frequencyBandList->list.array[0]->freqBandIndicatorNR,
            frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing);
    }
  }

  for (int i = 0; i < 5; i++) {
    if (i == frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing) {
      cfg->carrier_config.ul_grid_size[i] = frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;
      cfg->carrier_config.ul_k0[i] = frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier;
    }
    else {
      cfg->carrier_config.ul_grid_size[i] = 0;
      cfg->carrier_config.ul_k0[i] = 0;
    }
  }

  frame_type_t frame_type = get_frame_type(mac->nr_band, mac->numerology);
  // cell config
  cfg->cell_config.phy_cell_id = mac->physCellId;
  cfg->cell_config.frame_duplex_type = frame_type;
  cfg->cell_config.N_TA_offset = get_ta_offset(scc->n_TimingAdvanceOffset);

  // SSB config
  cfg->ssb_config.ss_pbch_power = scc->ss_PBCH_BlockPower;
  cfg->ssb_config.scs_common = mac->numerology;

  // SSB Table config
  cfg->ssb_table.ssb_offset_point_a = frequencyInfoDL->offsetToPointA;
  cfg->ssb_table.ssb_period = scc->ssb_PeriodicityServingCell;
  cfg->ssb_table.ssb_subcarrier_offset = mac->ssb_subcarrier_offset;
  cfg->ssb_table.ssb_case = set_ssb_case(mac->numerology, mac->nr_band);

  if (mac->frequency_range == FR1){
    cfg->ssb_table.ssb_mask_list[0].ssb_mask = ((uint32_t) scc->ssb_PositionsInBurst.inOneGroup.buf[0]) << 24;
    cfg->ssb_table.ssb_mask_list[1].ssb_mask = 0;
  }
  else{
    for (int i=0; i<8; i++){
      if ((scc->ssb_PositionsInBurst.groupPresence->buf[0]>>(7-i))&0x01)
        cfg->ssb_table.ssb_mask_list[i>>2].ssb_mask |= ((uint32_t)scc->ssb_PositionsInBurst.inOneGroup.buf[0])<<(24-8*(i%4));
    }
  }

  int period_idx = mac->tdd_UL_DL_ConfigurationCommon ? get_tdd_period_idx(mac->tdd_UL_DL_ConfigurationCommon) : 0;
  config_frame_structure(mac->numerology,
                         mac->tdd_UL_DL_ConfigurationCommon,
                         period_idx,
                         frame_type,
                         &mac->frame_structure);

  // TDD Table Configuration
  if (cfg->cell_config.frame_duplex_type == TDD)
    set_tdd_config_nr_ue(&cfg->tdd_table, &mac->frame_structure);

  // PRACH configuration
  uint8_t nb_preambles = 64;
  NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP.rach_ConfigCommon->choice.setup;
  if(rach_ConfigCommon->totalNumberOfRA_Preambles != NULL)
     nb_preambles = *rach_ConfigCommon->totalNumberOfRA_Preambles;

  cfg->prach_config.prach_sequence_length = rach_ConfigCommon->prach_RootSequenceIndex.present-1;

  if (rach_ConfigCommon->msg1_SubcarrierSpacing)
    cfg->prach_config.prach_sub_c_spacing = *rach_ConfigCommon->msg1_SubcarrierSpacing;
  else {
    // If absent, the UE applies the SCS as derived from the prach-ConfigurationIndex (for 839)
    int config_index = rach_ConfigCommon->rach_ConfigGeneric.prach_ConfigurationIndex;
    int format = get_format0(config_index, frame_type, mac->frequency_range);
    cfg->prach_config.prach_sub_c_spacing = get_delta_f_RA_long(format);
  }

  cfg->prach_config.restricted_set_config = rach_ConfigCommon->restrictedSetConfig;

  AssertFatal(rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM < 4,
              "msg1 FDM identifier %ld undefined (0,1,2,3)\n", rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM);
  cfg->prach_config.num_prach_fd_occasions = 1 << rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM;


  cfg->prach_config.num_prach_fd_occasions_list = (fapi_nr_num_prach_fd_occasions_t *) malloc(cfg->prach_config.num_prach_fd_occasions*sizeof(fapi_nr_num_prach_fd_occasions_t));
  for (int i=0; i<cfg->prach_config.num_prach_fd_occasions; i++) {
    fapi_nr_num_prach_fd_occasions_t *prach_fd_occasion = &cfg->prach_config.num_prach_fd_occasions_list[i];
    prach_fd_occasion->num_prach_fd_occasions = i;
    if (cfg->prach_config.prach_sequence_length)
      prach_fd_occasion->prach_root_sequence_index = rach_ConfigCommon->prach_RootSequenceIndex.choice.l139;
    else
      prach_fd_occasion->prach_root_sequence_index = rach_ConfigCommon->prach_RootSequenceIndex.choice.l839;
    prach_fd_occasion->k1 = NRRIV2PRBOFFSET(scc->uplinkConfigCommon->initialUplinkBWP.genericParameters.locationAndBandwidth, MAX_BWP_SIZE) +
                                            rach_ConfigCommon->rach_ConfigGeneric.msg1_FrequencyStart +
                                            (get_N_RA_RB(cfg->prach_config.prach_sub_c_spacing, frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing ) * i);
    prach_fd_occasion->prach_zero_corr_conf = rach_ConfigCommon->rach_ConfigGeneric.zeroCorrelationZoneConfig;
    prach_fd_occasion->num_root_sequences = compute_nr_root_seq(rach_ConfigCommon,
                                                                nb_preambles,
                                                                frame_type,
                                                                mac->frequency_range);
    //prach_fd_occasion->num_unused_root_sequences = ???
  }
}

// prepare data for orbit propagation based on SIB19 ephemeris data to be able to compute the round-trip-time between ue and sat
static void prepare_ue_sat_ta(const NR_PositionVelocity_r17_t *sat_pos, fapi_nr_ntn_config_t *ntn_ta)
{
  // get sat position coordinates
  const position_t pos_sat = {sat_pos->positionX_r17 * 1.3, sat_pos->positionY_r17 * 1.3, sat_pos->positionZ_r17 * 1.3};

  // get sat velocity vector
  const position_t vel_sat = {sat_pos->velocityVX_r17 * 0.06, sat_pos->velocityVY_r17 * 0.06, sat_pos->velocityVZ_r17 * 0.06};

  // calculate orbital radius
  const double radius_2 = pos_sat.X * pos_sat.X + pos_sat.Y * pos_sat.Y + pos_sat.Z * pos_sat.Z;
  const double radius = sqrt(radius_2);

  // calculate sat velocity magnitude
  const double vel_sat_2 = vel_sat.X * vel_sat.X + vel_sat.Y * vel_sat.Y + vel_sat.Z * vel_sat.Z;
  const double vel_mag = sqrt(vel_sat_2);

  double omega; // angular velocity in rad/ms
  position_t pos_sat_90; // sat position vector in 90° orbit
  position_t vel_sat_90; // sat velocity vector in 90° orbit

  // assuming circular orbit when satellite moves faster than 1000 m/s
  // 1000 m/s are chosen because according to Wikipedia, this seems to be a reasonable minimal
  // orbital velocity: https://en.wikipedia.org/wiki/Orbital_speed#Tangential_velocities_at_altitude
  if (vel_mag > 1000) {
    omega = vel_mag / (radius * 1000);
    double scaling = radius / vel_mag;
    pos_sat_90 = (position_t){vel_sat.X * scaling, vel_sat.Y * scaling, vel_sat.Z * scaling};
    scaling = -vel_mag / radius;
    vel_sat_90 = (position_t){pos_sat.X * scaling, pos_sat.Y * scaling, pos_sat.Z * scaling};
  } else {
    omega = 0;
    pos_sat_90 = pos_sat;
    vel_sat_90 = vel_sat;
  }

  LOG_I(NR_MAC,
        "Satellite orbital radius %f m, pos_sat_0 = {%f, %f, %f}, pos_sat_90 = {%f, %f, %f}\n",
        radius,
        pos_sat.X,
        pos_sat.Y,
        pos_sat.Z,
        pos_sat_90.X,
        pos_sat_90.Y,
        pos_sat_90.Z);

  LOG_I(NR_MAC,
        "Satellite velocity %f m/s, angular velocity = %e rad/ms, vel_sat_0 = {%f, %f, %f}, vel_sat_90 = {%f, %f, %f}\n",
        vel_mag,
        omega,
        vel_sat.X,
        vel_sat.Y,
        vel_sat.Z,
        vel_sat_90.X,
        vel_sat_90.Y,
        vel_sat_90.Z);

  ntn_ta->omega = omega;
  ntn_ta->pos_sat_0 = pos_sat;
  ntn_ta->pos_sat_90 = pos_sat_90;
  ntn_ta->vel_sat_0 = vel_sat;
  ntn_ta->vel_sat_90 = vel_sat_90;
}

// populate ntn_ta structure from mac
static void configure_ntn_ta(fapi_nr_ntn_config_t *ntn_ta,
                             const NR_NTN_Config_r17_t *ntn_Config_r17,
                             int hfn,
                             int frame,
                             bool is_targetcell)
{
  if (!ntn_Config_r17)
    return;

  // epochTime_r17 must be present (this is assured by function `eval_epoch_time()`)
  const NR_EpochTime_r17_t *epoch_time_r17 = ntn_Config_r17->epochTime_r17;
  AssertFatal(epoch_time_r17, "epoch_time_r17 should not be NULL here\n");
  if (epoch_time_r17->sfn_r17 >= frame)
    ntn_ta->epoch_hfn = hfn;
  else
    ntn_ta->epoch_hfn = hfn + 1;
  ntn_ta->epoch_sfn = epoch_time_r17->sfn_r17;
  ntn_ta->epoch_subframe = epoch_time_r17->subFrameNR_r17;

  // handle cellSpecificKoffset_r17
  if (ntn_Config_r17->cellSpecificKoffset_r17)
    ntn_ta->cell_specific_k_offset = *ntn_Config_r17->cellSpecificKoffset_r17;
  else // Need R - Release if not present
    ntn_ta->cell_specific_k_offset = 0;

  // Check if ta_Info_r17 is present and convert the time units
  if (ntn_Config_r17->ta_Info_r17) {
    // ta_Common_r17 (is in units of 4.072e-3 µs)
    ntn_ta->N_common_ta_adj = ntn_Config_r17->ta_Info_r17->ta_Common_r17 * 4.072e-6;
    // ta_CommonDrift_r17 (is in units of 0.2e-3 µs/s)
    if (ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17)
      ntn_ta->N_common_ta_drift = *ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17 * 0.2e-3;
    else // Need R - Release if not present
      ntn_ta->N_common_ta_drift = 0;
    // ta_CommonDriftVariant_r17 (is in units of 0.2e-4 µs/s²)
    if (ntn_Config_r17->ta_Info_r17->ta_CommonDriftVariant_r17)
      ntn_ta->N_common_ta_drift_variant = *ntn_Config_r17->ta_Info_r17->ta_CommonDriftVariant_r17 * 0.2e-4;
    else // Need R - Release if not present
      ntn_ta->N_common_ta_drift_variant = 0;
  } else { // Need R - Release if not present
    ntn_ta->N_common_ta_adj = 0;
    ntn_ta->N_common_ta_drift = 0;
    ntn_ta->N_common_ta_drift_variant = 0;
  }

  LOG_I(NR_MAC,
        "NTN Config Rxd. Epoch HFN: %d, Epoch SFN: %d, Epoch Subframe: %d, k_offset: %ldms, "
        "N_Common_Ta: %fms, drift: %fµs/s, variant: %fµs/s²\n",
        ntn_ta->epoch_hfn,
        ntn_ta->epoch_sfn,
        ntn_ta->epoch_subframe,
        ntn_ta->cell_specific_k_offset,
        ntn_ta->N_common_ta_adj,
        ntn_ta->N_common_ta_drift,
        ntn_ta->N_common_ta_drift_variant);

  // handle ephemerisInfo_r17
  const NR_EphemerisInfo_r17_t *ephemeris_info = ntn_Config_r17->ephemerisInfo_r17;
  if (ephemeris_info) {
    if (ephemeris_info->present == NR_EphemerisInfo_r17_PR_positionVelocity_r17) {
      const NR_PositionVelocity_r17_t *position_velocity = ephemeris_info->choice.positionVelocity_r17;
      AssertFatal(position_velocity, "position_velocity should not be NULL here\n");
      prepare_ue_sat_ta(position_velocity, ntn_ta);
    } else {
      LOG_W(NR_MAC, "NR UE currently supports only ephemerisInfo_r17 of type positionVelocity_r17\n");
      ntn_ta->omega = 0;
      ntn_ta->pos_sat_0 = (position_t){0, 0, 0};
      ntn_ta->pos_sat_90 = (position_t){0, 0, 0};
      ntn_ta->vel_sat_0 = (position_t){0, 0, 0};
      ntn_ta->vel_sat_90 = (position_t){0, 0, 0};
    }
  } else { // Need R - Release if not present
    ntn_ta->omega = 0;
    ntn_ta->pos_sat_0 = (position_t){0, 0, 0};
    ntn_ta->pos_sat_90 = (position_t){0, 0, 0};
    ntn_ta->vel_sat_0 = (position_t){0, 0, 0};
    ntn_ta->vel_sat_90 = (position_t){0, 0, 0};
  }

  ntn_ta->is_targetcell = is_targetcell;
  ntn_ta->params_changed = true;
}

static void config_common_ue(NR_UE_MAC_INST_t *mac, NR_ServingCellConfigCommon_t *scc, int cc_idP, int hfn, int frame)
{
  fapi_nr_config_request_t *cfg = &mac->phy_config.config_req;

  mac->phy_config.Mod_id = mac->ue_id;
  mac->phy_config.CC_id = cc_idP;
  frame_type_t frame_type = mac->frame_structure.frame_type;

  // carrier config
  LOG_D(MAC, "[UE %d] Entering UE Config Common\n", mac->ue_id);
  mac->numerology = *scc->ssbSubcarrierSpacing;

  AssertFatal(scc->downlinkConfigCommon, "Not expecting downlinkConfigCommon to be NULL here\n");
  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  if (frequencyInfoDL) { // NeedM for inter-freq handover
    mac->nr_band = *frequencyInfoDL->frequencyBandList.list.array[0];
    frame_type = get_frame_type(mac->nr_band, mac->numerology);
    mac->frequency_range = get_freq_range_from_band(mac->nr_band);

    AssertFatal(mac->numerology == frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing, "DL and SSB subcarrierSpacing must be the same!\n");
    int bw_index = get_supported_band_index(frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
                                            mac->frequency_range,
                                            frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth);
    cfg->carrier_config.dl_bandwidth = get_supported_bw_mhz(mac->frequency_range, bw_index);

    cfg->carrier_config.dl_frequency = from_nrarfcn(mac->nr_band,
                                                    *scc->ssbSubcarrierSpacing,
                                                    frequencyInfoDL->absoluteFrequencyPointA)
                                       / 1000; // freq in kHz
    LOG_I(NR_MAC,
          "[UE %d] Set dl_frequency=%u kHz (from absoluteFrequencyPointA=%ld, band=%d, scs=%ld)\n",
          mac->ue_id,
          cfg->carrier_config.dl_frequency,
          frequencyInfoDL->absoluteFrequencyPointA,
          mac->nr_band,
          *scc->ssbSubcarrierSpacing);

    for (int i = 0; i < 5; i++) {
      if (i == frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing) {
        cfg->carrier_config.dl_grid_size[i] = frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;
        cfg->carrier_config.dl_k0[i] = frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier;
      } else {
        cfg->carrier_config.dl_grid_size[i] = 0;
        cfg->carrier_config.dl_k0[i] = 0;
      }
    }
  }

  if (scc->uplinkConfigCommon && scc->uplinkConfigCommon->frequencyInfoUL) {
    NR_FrequencyInfoUL_t *frequencyInfoUL = scc->uplinkConfigCommon->frequencyInfoUL;
    mac->p_Max = frequencyInfoUL->p_Max ? *frequencyInfoUL->p_Max : INT_MIN;

    AssertFatal(mac->numerology == frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing, "UL and SSB subcarrierSpacing must be the same!\n");
    int bw_index = get_supported_band_index(frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
                                            mac->frequency_range,
                                            frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth);
    cfg->carrier_config.uplink_bandwidth = get_supported_bw_mhz(mac->frequency_range, bw_index);

    long *UL_pointA = NULL;
    if (frequencyInfoUL->absoluteFrequencyPointA)
      UL_pointA = frequencyInfoUL->absoluteFrequencyPointA;
    else if (frequencyInfoDL)
      UL_pointA = &frequencyInfoDL->absoluteFrequencyPointA;

    if (UL_pointA)
      cfg->carrier_config.uplink_frequency = from_nrarfcn(*frequencyInfoUL->frequencyBandList->list.array[0],
                                                          *scc->ssbSubcarrierSpacing,
                                                          *UL_pointA)
                                             / 1000; // freq in kHz

    for (int i = 0; i < 5; i++) {
      if (i == frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing) {
        cfg->carrier_config.ul_grid_size[i] = frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;
        cfg->carrier_config.ul_k0[i] = frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier;
      } else {
        cfg->carrier_config.ul_grid_size[i] = 0;
        cfg->carrier_config.ul_k0[i] = 0;
      }
    }
  }

  // cell config
  cfg->cell_config.phy_cell_id = *scc->physCellId;
  cfg->cell_config.frame_duplex_type = frame_type;
  cfg->cell_config.N_TA_offset = get_ta_offset(scc->n_TimingAdvanceOffset);

  // SSB config
  cfg->ssb_config.ss_pbch_power = scc->ss_PBCH_BlockPower;
  cfg->ssb_config.scs_common = *scc->ssbSubcarrierSpacing;

  // SSB Table config
  if (frequencyInfoDL && frequencyInfoDL->absoluteFrequencySSB) {
    cfg->ssb_table.ssb_offset_point_a = get_ssb_offset_to_pointA(*frequencyInfoDL->absoluteFrequencySSB,
                                                                 frequencyInfoDL->absoluteFrequencyPointA,
                                                                 cfg->ssb_config.scs_common,
                                                                 mac->frequency_range);
    cfg->ssb_table.ssb_period = *scc->ssb_periodicityServingCell;
    // NSA -> take ssb offset from SCS
    cfg->ssb_table.ssb_subcarrier_offset = get_ssb_subcarrier_offset(*frequencyInfoDL->absoluteFrequencySSB,
                                                                     frequencyInfoDL->absoluteFrequencyPointA,
                                                                     cfg->ssb_config.scs_common);
    cfg->ssb_table.ssb_case = set_ssb_case(*scc->ssbSubcarrierSpacing, mac->nr_band);
  }

  switch (scc->ssb_PositionsInBurst->present) {
  case 1 :
    cfg->ssb_table.ssb_mask_list[0].ssb_mask = ((uint32_t) scc->ssb_PositionsInBurst->choice.shortBitmap.buf[0]) << 24;
    cfg->ssb_table.ssb_mask_list[1].ssb_mask = 0;
    break;
  case 2 :
    cfg->ssb_table.ssb_mask_list[0].ssb_mask = ((uint32_t) scc->ssb_PositionsInBurst->choice.mediumBitmap.buf[0]) << 24;
    cfg->ssb_table.ssb_mask_list[1].ssb_mask = 0;
    break;
  case 3 :
    cfg->ssb_table.ssb_mask_list[0].ssb_mask = 0;
    cfg->ssb_table.ssb_mask_list[1].ssb_mask = 0;
    for (int i = 0; i < 4; i++) {
      cfg->ssb_table.ssb_mask_list[0].ssb_mask += (uint32_t) scc->ssb_PositionsInBurst->choice.longBitmap.buf[3 - i] << i * 8;
      cfg->ssb_table.ssb_mask_list[1].ssb_mask += (uint32_t) scc->ssb_PositionsInBurst->choice.longBitmap.buf[7 - i] << i * 8;
    }
    break;
  default:
    AssertFatal(1==0,"SSB bitmap size value %d undefined (allowed values 1,2,3) \n", scc->ssb_PositionsInBurst->present);
  }

  int period_idx = mac->tdd_UL_DL_ConfigurationCommon ? get_tdd_period_idx(mac->tdd_UL_DL_ConfigurationCommon) : 0;
  config_frame_structure(*scc->ssbSubcarrierSpacing,
                         mac->tdd_UL_DL_ConfigurationCommon,
                         period_idx,
                         frame_type,
                         &mac->frame_structure);

  // TDD Table Configuration
  if (cfg->cell_config.frame_duplex_type == TDD)
    set_tdd_config_nr_ue(&cfg->tdd_table, &mac->frame_structure);

  // PRACH configuration
  uint8_t nb_preambles = 64;
  if (scc->uplinkConfigCommon && scc->uplinkConfigCommon->initialUplinkBWP
      && scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon) { // all NeedM

    NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
    if (rach_ConfigCommon->totalNumberOfRA_Preambles != NULL)
      nb_preambles = *rach_ConfigCommon->totalNumberOfRA_Preambles;

    cfg->prach_config.prach_sequence_length = rach_ConfigCommon->prach_RootSequenceIndex.present - 1;

    if (rach_ConfigCommon->msg1_SubcarrierSpacing)
      cfg->prach_config.prach_sub_c_spacing = *rach_ConfigCommon->msg1_SubcarrierSpacing;
    else {
      // If absent, the UE applies the SCS as derived from the prach-ConfigurationIndex (for 839)
      int config_index = rach_ConfigCommon->rach_ConfigGeneric.prach_ConfigurationIndex;
      int format = get_format0(config_index, frame_type, mac->frequency_range);
      cfg->prach_config.prach_sub_c_spacing = format == 3 ? 5 : 4;
    }

    cfg->prach_config.restricted_set_config = rach_ConfigCommon->restrictedSetConfig;

    AssertFatal(rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM < 4,
                "msg1 FDM identifier %ld undefined (0,1,2,3)\n", rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM);
    cfg->prach_config.num_prach_fd_occasions = 1 << rach_ConfigCommon->rach_ConfigGeneric.msg1_FDM;

    cfg->prach_config.num_prach_fd_occasions_list = (fapi_nr_num_prach_fd_occasions_t *)malloc(
        cfg->prach_config.num_prach_fd_occasions * sizeof(fapi_nr_num_prach_fd_occasions_t));

    // Get UL carrier SCS for PRACH k1 calculation
    int ul_carrier_scs;
    if (scc->uplinkConfigCommon->frequencyInfoUL
        && scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.count > 0) {
      ul_carrier_scs = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
    } else {
      ul_carrier_scs = scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.subcarrierSpacing;
    }

    for (int i = 0; i < cfg->prach_config.num_prach_fd_occasions; i++) {
      fapi_nr_num_prach_fd_occasions_t *prach_fd_occasion = &cfg->prach_config.num_prach_fd_occasions_list[i];
      prach_fd_occasion->num_prach_fd_occasions = i;
      if (cfg->prach_config.prach_sequence_length)
        prach_fd_occasion->prach_root_sequence_index = rach_ConfigCommon->prach_RootSequenceIndex.choice.l139;
      else
        prach_fd_occasion->prach_root_sequence_index = rach_ConfigCommon->prach_RootSequenceIndex.choice.l839;

      prach_fd_occasion->k1 = rach_ConfigCommon->rach_ConfigGeneric.msg1_FrequencyStart
          + (get_N_RA_RB(cfg->prach_config.prach_sub_c_spacing, ul_carrier_scs) * i);
      prach_fd_occasion->prach_zero_corr_conf = rach_ConfigCommon->rach_ConfigGeneric.zeroCorrelationZoneConfig;
      prach_fd_occasion->num_root_sequences =
          compute_nr_root_seq(rach_ConfigCommon, nb_preambles, frame_type, mac->frequency_range);

      // prach_fd_occasion->num_unused_root_sequences = ???
    }
  }

  // NTN Config
  if (scc->ext2) {
    UPDATE_IE(mac->sc_info.ntn_Config_r17, scc->ext2->ntn_Config_r17, NR_NTN_Config_r17_t);
    configure_ntn_ta(&mac->phy_config.config_req.ntn_config, mac->sc_info.ntn_Config_r17, hfn, frame, true);
  } else {
    asn1cFreeStruc(asn_DEF_NR_NTN_Config_r17, mac->sc_info.ntn_Config_r17);
  }
}

static void free_coreset(NR_ControlResourceSet_t *coreset)
{
  ASN_STRUCT_FREE(asn_DEF_NR_ControlResourceSet, coreset);
}

static void free_search_space(NR_SearchSpace_t *search_space)
{
  ASN_STRUCT_FREE(asn_DEF_NR_SearchSpace, search_space);
}

static void release_common_ss_cset(NR_BWP_PDCCH_t *pdcch)
{
  pdcch->otherSI_SS_id = -1;
  pdcch->ra_SS_id = -1;
  pdcch->paging_SS_id = -1;
  asn_sequence_empty(&pdcch->list_common_SS);
  asn1cFreeStruc(asn_DEF_NR_ControlResourceSet, pdcch->commonControlResourceSet);
}

static void modlist_ss(NR_SearchSpace_t *source, NR_SearchSpace_t *target)
{
  target->searchSpaceId = source->searchSpaceId;
  if (source->controlResourceSetId)
    UPDATE_IE(target->controlResourceSetId, source->controlResourceSetId, NR_ControlResourceSetId_t);
  if (source->monitoringSlotPeriodicityAndOffset)
    UPDATE_IE(target->monitoringSlotPeriodicityAndOffset,
              source->monitoringSlotPeriodicityAndOffset,
              struct NR_SearchSpace__monitoringSlotPeriodicityAndOffset);
  UPDATE_IE(target->duration, source->duration, long);
  if (source->monitoringSymbolsWithinSlot)
    UPDATE_IE(target->monitoringSymbolsWithinSlot, source->monitoringSymbolsWithinSlot, BIT_STRING_t);
  if (source->nrofCandidates)
    UPDATE_IE(target->nrofCandidates, source->nrofCandidates, struct NR_SearchSpace__nrofCandidates);
  if (source->searchSpaceType)
    UPDATE_IE(target->searchSpaceType, source->searchSpaceType, struct NR_SearchSpace__searchSpaceType);
}

NR_SearchSpace_t *get_common_search_space(const NR_UE_MAC_INST_t *mac, const NR_SearchSpaceId_t ss_id)
{
  if (ss_id == 0)
    return mac->search_space_zero;

  NR_SearchSpace_t *css = NULL;
  // if current DL BWP is not set, use first BWP
  const int bwp_id = mac->current_DL_BWP ? mac->current_DL_BWP->bwp_id : 0;
  for (int i = 0; i < mac->config_BWP_PDCCH[bwp_id].list_common_SS.count; i++) {
    css = mac->config_BWP_PDCCH[bwp_id].list_common_SS.array[i];
    if (css->searchSpaceId == ss_id) {
      return css;
    }
  }
  AssertFatal(css, "Couldn't find CSS with Id %ld\n", ss_id);
  return css;
}

static void update_ss(void *ue_ss_in, void *nw_ss_in)
{
  if (!nw_ss_in || !ue_ss_in)
    return;

  asn_anonymous_sequence_ *ue_ss = _A_SEQUENCE_FROM_VOID(ue_ss_in);
  asn_anonymous_sequence_ *nw_ss = _A_SEQUENCE_FROM_VOID(nw_ss_in);
  for (int i = 0; i < nw_ss->count; i++) {
    NR_SearchSpace_t *source_ss = (NR_SearchSpace_t *)nw_ss->array[i];
    NR_SearchSpace_t *target_ss = NULL;
    for (int j = 0; j < ue_ss->count; j++) {
      NR_SearchSpace_t **s = (NR_SearchSpace_t **)ue_ss->array;
      if (s[j]->searchSpaceId == source_ss->searchSpaceId) {
        target_ss = s[j];
        break;
      }
    }
    if (!target_ss) {
      target_ss = calloc(1, sizeof(*target_ss));
      ASN_SEQUENCE_ADD(ue_ss, target_ss);
    }
    modlist_ss(source_ss, target_ss);
  }
}

static void configure_common_ss_coreset(NR_BWP_PDCCH_t *pdcch, NR_PDCCH_ConfigCommon_t *pdcch_ConfigCommon)
{
  if (!pdcch_ConfigCommon)
    return;

  pdcch->list_common_SS.free = free_search_space;
  if (pdcch_ConfigCommon->commonSearchSpaceList)
    update_ss((void *)&pdcch->list_common_SS, (void *)&pdcch_ConfigCommon->commonSearchSpaceList->list);

  ASIGN_P_VAL(pdcch->otherSI_SS_id, pdcch_ConfigCommon->searchSpaceOtherSystemInformation);
  ASIGN_P_VAL(pdcch->ra_SS_id, pdcch_ConfigCommon->ra_SearchSpace);
  ASIGN_P_VAL(pdcch->paging_SS_id, pdcch_ConfigCommon->pagingSearchSpace);

  UPDATE_IE(pdcch->commonControlResourceSet, pdcch_ConfigCommon->commonControlResourceSet, NR_ControlResourceSet_t);
}

static void modlist_coreset(NR_ControlResourceSet_t *source, NR_ControlResourceSet_t *target)
{
  target->controlResourceSetId = source->controlResourceSetId;
  target->frequencyDomainResources.size = source->frequencyDomainResources.size;
  if (!target->frequencyDomainResources.buf)
    target->frequencyDomainResources.buf =
        calloc(target->frequencyDomainResources.size, sizeof(*target->frequencyDomainResources.buf));
  for (int i = 0; i < source->frequencyDomainResources.size; i++)
    target->frequencyDomainResources.buf[i] = source->frequencyDomainResources.buf[i];
  target->duration = source->duration;
  target->precoderGranularity = source->precoderGranularity;
  long *shiftIndex = NULL;
  if (target->cce_REG_MappingType.present == NR_ControlResourceSet__cce_REG_MappingType_PR_interleaved)
    shiftIndex = target->cce_REG_MappingType.choice.interleaved->shiftIndex;
  if (source->cce_REG_MappingType.present == NR_ControlResourceSet__cce_REG_MappingType_PR_interleaved) {
    target->cce_REG_MappingType.present = NR_ControlResourceSet__cce_REG_MappingType_PR_interleaved;
    target->cce_REG_MappingType.choice.interleaved->reg_BundleSize = source->cce_REG_MappingType.choice.interleaved->reg_BundleSize;
    target->cce_REG_MappingType.choice.interleaved->interleaverSize =
        source->cce_REG_MappingType.choice.interleaved->interleaverSize;
    UPDATE_IE(target->cce_REG_MappingType.choice.interleaved->shiftIndex,
              source->cce_REG_MappingType.choice.interleaved->shiftIndex,
              long);
  } else {
    free(shiftIndex);
    target->cce_REG_MappingType = source->cce_REG_MappingType;
  }
  UPDATE_IE(target->tci_PresentInDCI, source->tci_PresentInDCI, long);
  UPDATE_IE(target->pdcch_DMRS_ScramblingID, source->pdcch_DMRS_ScramblingID, long);
  // TCI States
  if (source->tci_StatesPDCCH_ToReleaseList) {
    for (int i = 0; i < source->tci_StatesPDCCH_ToReleaseList->list.count; i++) {
      long id = *source->tci_StatesPDCCH_ToReleaseList->list.array[i];
      int j;
      for (j = 0; j < target->tci_StatesPDCCH_ToAddList->list.count; j++) {
        if (id == *target->tci_StatesPDCCH_ToAddList->list.array[j])
          break;
      }
      if (j < target->tci_StatesPDCCH_ToAddList->list.count)
        asn_sequence_del(&target->tci_StatesPDCCH_ToAddList->list, j, 1);
      else
        LOG_E(NR_MAC, "Element not present in the list, impossible to release\n");
    }
  }
  if (source->tci_StatesPDCCH_ToAddList) {
    if (target->tci_StatesPDCCH_ToAddList) {
      for (int i = 0; i < source->tci_StatesPDCCH_ToAddList->list.count; i++) {
        long id = *source->tci_StatesPDCCH_ToAddList->list.array[i];
        int j;
        for (j = 0; j < target->tci_StatesPDCCH_ToAddList->list.count; j++) {
          if (id == *target->tci_StatesPDCCH_ToAddList->list.array[j])
            break;
        }
        if (j == target->tci_StatesPDCCH_ToAddList->list.count) {
          long *tci_id = calloc(1, sizeof(*tci_id));
          *tci_id = id;
          ASN_SEQUENCE_ADD(&target->tci_StatesPDCCH_ToAddList->list, tci_id);
        }
      }
    } else
      UPDATE_IE(target->tci_StatesPDCCH_ToAddList,
                source->tci_StatesPDCCH_ToAddList,
                struct NR_ControlResourceSet__tci_StatesPDCCH_ToAddList);
  }
  // end TCI States
}

static void configure_ss_coreset(NR_BWP_PDCCH_t *pdcch, NR_PDCCH_Config_t *pdcch_Config)
{
  if (!pdcch_Config)
    return;
  pdcch->list_Coreset.free = free_coreset;
  pdcch->list_SS.free = free_search_space;
  if (pdcch_Config->controlResourceSetToAddModList) {
    for (int i = 0; i < pdcch_Config->controlResourceSetToAddModList->list.count; i++) {
      NR_ControlResourceSet_t *source_coreset = pdcch_Config->controlResourceSetToAddModList->list.array[i];
      NR_ControlResourceSet_t *target_coreset = NULL;
      for (int j = 0; j < pdcch->list_Coreset.count; j++) {
        if (pdcch->list_Coreset.array[j]->controlResourceSetId == source_coreset->controlResourceSetId) {
          target_coreset = pdcch->list_Coreset.array[j];
          break;
        }
      }
      if (!target_coreset) {
        target_coreset = calloc(1, sizeof(*target_coreset));
        ASN_SEQUENCE_ADD(&pdcch->list_Coreset, target_coreset);
      }
      modlist_coreset(source_coreset, target_coreset);
    }
  }
  if (pdcch_Config->controlResourceSetToReleaseList) {
    for (int i = 0; i < pdcch_Config->controlResourceSetToReleaseList->list.count; i++) {
      NR_ControlResourceSetId_t id = *pdcch_Config->controlResourceSetToReleaseList->list.array[i];
      for (int j = 0; j < pdcch->list_Coreset.count; j++) {
        if (id == pdcch->list_Coreset.array[j]->controlResourceSetId) {
          asn_sequence_del(&pdcch->list_Coreset, j, 1);
          break;
        }
      }
    }
  }

  if (pdcch_Config->searchSpacesToAddModList)
    update_ss((void *)&pdcch->list_SS, (void *)&pdcch_Config->searchSpacesToAddModList->list);

  if (pdcch_Config->searchSpacesToReleaseList) {
    for (int i = 0; i < pdcch_Config->searchSpacesToReleaseList->list.count; i++) {
      NR_ControlResourceSetId_t id = *pdcch_Config->searchSpacesToReleaseList->list.array[i];
      for (int j = 0; j < pdcch->list_SS.count; j++) {
        if (id == pdcch->list_SS.array[j]->searchSpaceId) {
          asn_sequence_del(&pdcch->list_SS, j, 1);
          break;
        }
      }
    }
  }
}

static int lcid_cmp(const void *a, const void *b)
{
  long priority_a = (*((nr_lcordered_info_t**)a))->priority;
  AssertFatal(priority_a > 0 && priority_a < 17, "Invalid priority value %ld\n", priority_a);
  long priority_b = (*((nr_lcordered_info_t**)b))->priority;
  AssertFatal(priority_b > 0 && priority_b < 17, "Invalid priority value %ld\n", priority_b);
  return priority_a - priority_b;
}

static int nr_get_ms_bucketsizeduration(long bucketsizeduration)
{
  switch (bucketsizeduration) {
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms5:
      return 5;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms10:
      return 10;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms20:
      return 20;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms50:
      return 50;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms100:
      return 100;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms150:
      return 150;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms300:
      return 300;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms500:
      return 500;
    case NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms1000:
      return 1000;
    default:
      AssertFatal(false, "Invalid bucketSizeDuration %ld\n", bucketsizeduration);
  }
}

static uint32_t nr_get_pbr(long prioritizedbitrate)  // returns Bps
{
  uint32_t pbr = 0;
  switch (prioritizedbitrate) {
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps0:
      pbr = 0;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps8:
      pbr = 8;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps16:
      pbr = 16;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps32:
      pbr = 32;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps64:
      pbr = 64;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps128:
      pbr = 128;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps256:
      pbr = 256;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps512:
      pbr = 512;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps1024:
      pbr = 1024;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps2048:
      pbr = 2048;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps4096:
      pbr = 4096;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps8192:
      pbr = 8192;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps16384:
      pbr = 16384;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps32768:
      pbr = 32768;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_kBps65536:
      pbr = 65536;
      break;
    case NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_infinity:
      pbr = UINT_MAX;
      break;
    default:
      AssertFatal(false, "The proritized bit rate value is not one of the enum values\n");
  }
  uint32_t pbr_bytes =
      (prioritizedbitrate < NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_infinity) ? pbr * 1000 : pbr;
  return pbr_bytes;
}

static uint32_t get_lc_bucket_size(long prioritisedBitRate, long bucketSizeDuration)
{
  uint32_t pbr = nr_get_pbr(prioritisedBitRate);
  // if infinite pbr, the bucket is saturated by pbr
  int bsd = 0;
  if (prioritisedBitRate == NR_LogicalChannelConfig__ul_SpecificParameters__prioritisedBitRate_infinity)
    bsd = 1;
  else
    bsd = nr_get_ms_bucketsizeduration(bucketSizeDuration);

  return pbr * bsd;
}

// default configuration as per 38.331 section 9.2.1
static void set_default_logicalchannelconfig(nr_lcordered_info_t *lc_info, int srb_id)
{
  lc_info->lcid = srb_id;
  lc_info->priority = srb_id == 2 ? 3 : 1;
  lc_info->pbr = UINT_MAX;
  lc_info->bucket_size = UINT_MAX;
}

static void nr_configure_lc_config(NR_UE_MAC_INST_t *mac,
                                   nr_lcordered_info_t *lc_info,
                                   NR_LogicalChannelConfig_t *mac_lc_config,
                                   nr_lcid_rb_t rb)
{
  NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lc_info->lcid);
  if (rb.type != NR_LCID_NONE)
    lc_info->rb = rb;
  lc_info->rb_suspended = false;
  if (rb.type == NR_LCID_SRB && !mac_lc_config->ul_SpecificParameters) {
    // release configuration and reset to default
    set_default_logicalchannelconfig(lc_info, rb.choice.srb_id);
    // invalid LCGID to signal it is absent in the configuration
    lc_sched_info->LCGID = NR_INVALID_LCGID;
    return;
  }
  AssertFatal(mac_lc_config->ul_SpecificParameters, "UL parameters shouldn't be NULL for DRBs\n");
  struct NR_LogicalChannelConfig__ul_SpecificParameters *ul_parm = mac_lc_config->ul_SpecificParameters;
  lc_info->priority = ul_parm->priority;
  lc_info->sr_id = ul_parm->schedulingRequestID ? *ul_parm->schedulingRequestID : -1;
  lc_info->sr_DelayTimerApplied = ul_parm->logicalChannelSR_DelayTimerApplied;
  lc_info->lc_SRMask = ul_parm->logicalChannelSR_Mask;
  lc_info->pbr = nr_get_pbr(ul_parm->prioritisedBitRate);
  // if logicalChannelGroup we release LCGID and set it to invalid
  lc_sched_info->LCGID = ul_parm->logicalChannelGroup ? *ul_parm->logicalChannelGroup : NR_INVALID_LCGID;
  lc_info->bucket_size = get_lc_bucket_size(ul_parm->prioritisedBitRate, ul_parm->bucketSizeDuration);
  // setup and start Bj timer for this LC
  NR_timer_t *bjt = &lc_sched_info->Bj_timer;
  nr_timer_setup(bjt, UINT_MAX, 1);  // this timer never expires in principle, counter incremented by number of slots
  nr_timer_start(bjt);
}

static nr_lcid_rb_t configure_lcid_rb(NR_RLC_BearerConfig_t *rlc_bearer)
{
  nr_lcid_rb_t rb;
  if (!rlc_bearer->servedRadioBearer) {
    LOG_W(NR_MAC, "RLC bearer config does not contain servedRadioBearer.\n");
    rb.type = NR_LCID_NONE;
    return rb;
  }

  if (rlc_bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity) {
    rb.type = NR_LCID_SRB;
    rb.choice.srb_id = rlc_bearer->servedRadioBearer->choice.srb_Identity;
    return rb;
  }

  if (rlc_bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_drb_Identity) {
    rb.type = NR_LCID_DRB;
    rb.choice.drb_id = rlc_bearer->servedRadioBearer->choice.drb_Identity;
    return rb;
  }
  LOG_E(NR_MAC, "Error. RLC should be linked to either DRB or SRB.\n");
  rb.type = NR_LCID_NONE;
  return rb;
}

static void configure_logicalChannelBearer(NR_UE_MAC_INST_t *mac,
                                           struct NR_CellGroupConfig__rlc_BearerToAddModList *rlc_toadd_list,
                                           struct NR_CellGroupConfig__rlc_BearerToReleaseList *rlc_torelease_list)
{
  if (rlc_torelease_list) {
    for (int i = 0; i < rlc_torelease_list->list.count; i++) {
      long id = *rlc_torelease_list->list.array[i];
      int j;
      for (j = 0; j < mac->lc_ordered_list.count; j++) {
        if (id == mac->lc_ordered_list.array[j]->lcid)
          break;
      }
      if (j < mac->lc_ordered_list.count)
        asn_sequence_del(&mac->lc_ordered_list, j, 1);
      else
        LOG_E(NR_MAC, "Element not present in the list, impossible to release\n");
    }
  }

  if (rlc_toadd_list) {
    for (int i = 0; i < rlc_toadd_list->list.count; i++) {
      NR_RLC_BearerConfig_t *rlc_bearer = rlc_toadd_list->list.array[i];
      nr_lcid_rb_t rb = configure_lcid_rb(rlc_bearer);
      int lc_identity = rlc_bearer->logicalChannelIdentity;
      NR_LogicalChannelConfig_t *mac_lc_config = rlc_bearer->mac_LogicalChannelConfig;
      int j;
      for (j = 0; j < mac->lc_ordered_list.count; j++) {
        if (lc_identity == mac->lc_ordered_list.array[j]->lcid)
          break;
      }
      if (j < mac->lc_ordered_list.count) {
        LOG_D(NR_MAC, "Logical channel %d is already established, Reconfiguring now\n", lc_identity);
        if (mac_lc_config != NULL)
          nr_configure_lc_config(mac, mac->lc_ordered_list.array[j], mac_lc_config, rb);
      } else {
        /* setup of new LCID*/
        nr_lcordered_info_t *lc_info = calloc(1, sizeof(*lc_info));
        lc_info->lcid = lc_identity;
        LOG_D(NR_MAC, "Establishing the logical channel %d\n", lc_identity);
        AssertFatal(rlc_bearer->servedRadioBearer, "servedRadioBearer should be present for LCID establishment\n");
        if (rlc_bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity) { /* SRB */
          NR_SRB_Identity_t srb_id = rlc_bearer->servedRadioBearer->choice.srb_Identity;
          if (mac_lc_config != NULL)
            nr_configure_lc_config(mac, lc_info, mac_lc_config, rb);
          else
            set_default_logicalchannelconfig(lc_info, srb_id);
        } else { /* DRB */
          AssertFatal(mac_lc_config, "When establishing a DRB, LogicalChannelConfig should be mandatorily present\n");
          nr_configure_lc_config(mac, lc_info, mac_lc_config, rb);
        }
        ASN_SEQUENCE_ADD(&mac->lc_ordered_list, lc_info);
      }
    }

    // reorder the logical channels as per its priority
    qsort(mac->lc_ordered_list.array, mac->lc_ordered_list.count, sizeof(nr_lcordered_info_t*), lcid_cmp);
  }
}

void ue_init_config_request(NR_UE_MAC_INST_t *mac, int slots_per_frame)
{
  LOG_I(NR_MAC, "Initializing dl and ul config_request. num_slots = %d\n", slots_per_frame);
  mac->dl_config_request = calloc(slots_per_frame, sizeof(*mac->dl_config_request));
  mac->ul_config_request = calloc(slots_per_frame, sizeof(*mac->ul_config_request));
  for (int i = 0; i < slots_per_frame; i++)
    pthread_mutex_init(&(mac->ul_config_request[i].mutex_ul_config), NULL);
}

static void update_mib_conf(NR_MIB_t *target, NR_MIB_t *source)
{
  target->systemFrameNumber.size = source->systemFrameNumber.size;
  target->systemFrameNumber.bits_unused = source->systemFrameNumber.bits_unused;
  if (!target->systemFrameNumber.buf)
    target->systemFrameNumber.buf = calloc(target->systemFrameNumber.size, sizeof(*target->systemFrameNumber.buf));
  for (int i = 0; i < target->systemFrameNumber.size; i++)
    target->systemFrameNumber.buf[i] = source->systemFrameNumber.buf[i];
  target->subCarrierSpacingCommon = source->subCarrierSpacingCommon;
  target->ssb_SubcarrierOffset = source->ssb_SubcarrierOffset;
  target->dmrs_TypeA_Position = source->dmrs_TypeA_Position;
  target->pdcch_ConfigSIB1 = source->pdcch_ConfigSIB1;
  target->cellBarred = source->cellBarred;
  target->intraFreqReselection = source->intraFreqReselection;
}

static bool is_cset0_present(frequency_range_t const fr, uint8_t const kssb)
{
  // TS 38.213 4.1 defines if CORESET 0 is present or not based on Kssb
  return (fr == FR1) ? (kssb < 24) : (kssb < 12);
}

void nr_rrc_mac_sched_sib(module_id_t module_id, int sched_sib)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  if (sched_sib == 1) {
    bool const is_c0 = is_cset0_present(mac->frequency_range, mac->ssb_subcarrier_offset);
    mac->get_sib1 = is_c0;
    AssertFatal(is_c0, "RRC scheduling SIB1 reception but MIB indicates no SIB1 present in current cell\n");
  } else if (sched_sib > 1)
    mac->get_otherSI[sched_sib - 2] = true;

  if (mac->state == UE_NOT_SYNC && mac->get_sib1)
    mac->state = UE_RECEIVING_SIB;
}

void nr_rrc_mac_config_req_mib(module_id_t module_id, int cc_idP, NR_MIB_t *mib, bool barred)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  AssertFatal(mib, "MIB should not be NULL\n");
  if (!mac->mib)
    mac->mib = calloc(1, sizeof(*mac->mib));
  if (barred)
    mac->state = UE_BARRED;
  else if (mac->state == UE_BARRED) {
    // it is synched as we received MIB
    // nr_ue_decode_mib is transitionining to the correct state
    mac->state = UE_NOT_SYNC;
  }
  update_mib_conf(mac->mib, mib);
  mac->phy_config.Mod_id = module_id;
  mac->phy_config.CC_id = cc_idP;

  nr_ue_decode_mib(mac, cc_idP);
  if (get_softmodem_params()->phy_test)
    mac->state = UE_CONNECTED;
  else if (mac->state == UE_NOT_SYNC_RECONF)
    // cell other than the serving cell: wait for its SIB1 before RA
    mac->state = mac->physCellId == mac->phy_config.config_req.cell_config.phy_cell_id ? UE_PERFORMING_RA : UE_NOT_SYNC;

  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

static void setup_puschpowercontrol(NR_UE_MAC_INST_t *mac, NR_PUSCH_PowerControl_t *source, NR_PUSCH_PowerControl_t *target)
{
  UPDATE_IE(target->tpc_Accumulation, source->tpc_Accumulation, long);
  UPDATE_IE(target->msg3_Alpha, source->msg3_Alpha, NR_Alpha_t);
  if (source->p0_NominalWithoutGrant)
    UPDATE_IE(target->p0_NominalWithoutGrant, source->p0_NominalWithoutGrant, long);
  if (source->p0_AlphaSets) {
    UPDATE_IE(target->p0_AlphaSets, source->p0_AlphaSets, struct NR_PUSCH_PowerControl__p0_AlphaSets);
    if (target->p0_AlphaSets->list.array[0]->alpha) {
      mac->f_b_f_c = 0;
    }
    if (target->p0_AlphaSets->list.array[0]->p0) {
      mac->f_b_f_c = 0;
    }
  }
  UPDATE_IE(target->twoPUSCH_PC_AdjustmentStates, source->twoPUSCH_PC_AdjustmentStates, long);
  UPDATE_IE(target->deltaMCS, source->deltaMCS, long);
  if (source->pathlossReferenceRSToReleaseList) {
    RELEASE_IE_FROMLIST(source->pathlossReferenceRSToReleaseList,
                        target->pathlossReferenceRSToAddModList,
                        pusch_PathlossReferenceRS_Id);
  }
  if (source->pathlossReferenceRSToAddModList) {
    if (!target->pathlossReferenceRSToAddModList)
      target->pathlossReferenceRSToAddModList = calloc(1, sizeof(*target->pathlossReferenceRSToAddModList));
    ADDMOD_IE_FROMLIST(source->pathlossReferenceRSToAddModList,
                       target->pathlossReferenceRSToAddModList,
                       pusch_PathlossReferenceRS_Id,
                       NR_PUSCH_PathlossReferenceRS_t);
  }
  if (source->sri_PUSCH_MappingToReleaseList) {
    RELEASE_IE_FROMLIST(source->sri_PUSCH_MappingToReleaseList,
                        target->sri_PUSCH_MappingToAddModList,
                        sri_PUSCH_PowerControlId);
  }
  if (source->sri_PUSCH_MappingToAddModList) {
    LOG_E(NR_MAC, "NR_SRI_PUSCH_PowerControl not implemented, power control will not work as intended\n");
    if (!target->sri_PUSCH_MappingToAddModList)
      target->sri_PUSCH_MappingToAddModList = calloc(1, sizeof(*target->sri_PUSCH_MappingToAddModList));
    ADDMOD_IE_FROMLIST(source->sri_PUSCH_MappingToAddModList,
                       target->sri_PUSCH_MappingToAddModList,
                       sri_PUSCH_PowerControlId,
                       NR_SRI_PUSCH_PowerControl_t);
  }
}

static void setup_puschconfig(NR_UE_MAC_INST_t *mac, NR_PUSCH_Config_t *source, NR_PUSCH_Config_t *target)
{
  UPDATE_IE(target->dataScramblingIdentityPUSCH, source->dataScramblingIdentityPUSCH, long);
  UPDATE_IE(target->txConfig, source->txConfig, long);
  if (source->dmrs_UplinkForPUSCH_MappingTypeA)
    HANDLE_SETUPRELEASE_IE(target->dmrs_UplinkForPUSCH_MappingTypeA,
                           source->dmrs_UplinkForPUSCH_MappingTypeA,
                           NR_DMRS_UplinkConfig_t,
                           asn_DEF_NR_SetupRelease_DMRS_UplinkConfig);
  if (source->dmrs_UplinkForPUSCH_MappingTypeB)
    HANDLE_SETUPRELEASE_IE(target->dmrs_UplinkForPUSCH_MappingTypeB,
                           source->dmrs_UplinkForPUSCH_MappingTypeB,
                           NR_DMRS_UplinkConfig_t,
                           asn_DEF_NR_SetupRelease_DMRS_UplinkConfig);
  if (source->pusch_PowerControl) {
    if (!target->pusch_PowerControl)
      target->pusch_PowerControl = calloc(1, sizeof(*target->pusch_PowerControl));
    setup_puschpowercontrol(mac, source->pusch_PowerControl, target->pusch_PowerControl);
  }
  UPDATE_IE(target->frequencyHopping, source->frequencyHopping, long);
  if (source->frequencyHoppingOffsetLists)
    UPDATE_IE(target->frequencyHoppingOffsetLists,
              source->frequencyHoppingOffsetLists,
              struct NR_PUSCH_Config__frequencyHoppingOffsetLists);
  target->resourceAllocation = source->resourceAllocation;
  if (source->pusch_TimeDomainAllocationList)
    HANDLE_SETUPRELEASE_IE(target->pusch_TimeDomainAllocationList,
                           source->pusch_TimeDomainAllocationList,
                           NR_PUSCH_TimeDomainResourceAllocationList_t,
                           asn_DEF_NR_SetupRelease_PUSCH_TimeDomainResourceAllocationList);
  UPDATE_IE(target->pusch_AggregationFactor, source->pusch_AggregationFactor, long);
  UPDATE_IE(target->mcs_Table, source->mcs_Table, long);
  UPDATE_IE(target->mcs_TableTransformPrecoder, source->mcs_TableTransformPrecoder, long);
  UPDATE_IE(target->transformPrecoder, source->transformPrecoder, long);
  UPDATE_IE(target->codebookSubset, source->codebookSubset, long);
  UPDATE_IE(target->maxRank, source->maxRank, long);
  UPDATE_IE(target->rbg_Size, source->rbg_Size, long);
  UPDATE_IE(target->tp_pi2BPSK, source->tp_pi2BPSK, long);
  if (source->uci_OnPUSCH) {
    if (source->uci_OnPUSCH->present == NR_SetupRelease_UCI_OnPUSCH_PR_release)
      asn1cFreeStruc(asn_DEF_NR_UCI_OnPUSCH, target->uci_OnPUSCH);
    if (source->uci_OnPUSCH->present == NR_SetupRelease_UCI_OnPUSCH_PR_setup) {
      if (!target->uci_OnPUSCH) {
        target->uci_OnPUSCH = calloc(1, sizeof(*target->uci_OnPUSCH));
        target->uci_OnPUSCH->choice.setup = calloc(1, sizeof(*target->uci_OnPUSCH->choice.setup));
      }
      target->uci_OnPUSCH->choice.setup->scaling = source->uci_OnPUSCH->choice.setup->scaling;
      if (source->uci_OnPUSCH->choice.setup->betaOffsets)
        UPDATE_IE(target->uci_OnPUSCH->choice.setup->betaOffsets,
                  source->uci_OnPUSCH->choice.setup->betaOffsets,
                  struct NR_UCI_OnPUSCH__betaOffsets);
    }
  }
  if (source->ext2) {
    if (!target->ext2)
      target->ext2 = calloc(1, sizeof(*target->ext2));
    UPDATE_IE(target->ext2->harq_ProcessNumberSizeDCI_0_1_r17, source->ext2->harq_ProcessNumberSizeDCI_0_1_r17, long);
  } else if (target->ext2) {
    free_and_zero(target->ext2->harq_ProcessNumberSizeDCI_0_1_r17);
  }
}

static void configure_csi_resourcemapping(NR_CSI_RS_ResourceMapping_t *target, NR_CSI_RS_ResourceMapping_t *source)
{
  if (target->frequencyDomainAllocation.present != source->frequencyDomainAllocation.present) {
    UPDATE_NP_IE(target->frequencyDomainAllocation,
                 source->frequencyDomainAllocation,
                 struct NR_CSI_RS_ResourceMapping__frequencyDomainAllocation);
  }
  else {
    switch (source->frequencyDomainAllocation.present) {
      case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row1:
        target->frequencyDomainAllocation.choice.row1.size = source->frequencyDomainAllocation.choice.row1.size;
        target->frequencyDomainAllocation.choice.row1.bits_unused = source->frequencyDomainAllocation.choice.row1.bits_unused;
        if (!target->frequencyDomainAllocation.choice.row1.buf)
          target->frequencyDomainAllocation.choice.row1.buf =
              calloc(target->frequencyDomainAllocation.choice.row1.size, sizeof(uint8_t));
        for (int i = 0; i < target->frequencyDomainAllocation.choice.row1.size; i++)
          target->frequencyDomainAllocation.choice.row1.buf[i] = source->frequencyDomainAllocation.choice.row1.buf[i];
        break;
      case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row2:
        target->frequencyDomainAllocation.choice.row2.size = source->frequencyDomainAllocation.choice.row2.size;
        target->frequencyDomainAllocation.choice.row2.bits_unused = source->frequencyDomainAllocation.choice.row2.bits_unused;
        if (!target->frequencyDomainAllocation.choice.row2.buf)
          target->frequencyDomainAllocation.choice.row2.buf =
              calloc(target->frequencyDomainAllocation.choice.row2.size, sizeof(uint8_t));
        for (int i = 0; i < target->frequencyDomainAllocation.choice.row2.size; i++)
          target->frequencyDomainAllocation.choice.row2.buf[i] = source->frequencyDomainAllocation.choice.row2.buf[i];
        break;
      case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row4:
        target->frequencyDomainAllocation.choice.row4.size = source->frequencyDomainAllocation.choice.row4.size;
        target->frequencyDomainAllocation.choice.row4.bits_unused = source->frequencyDomainAllocation.choice.row4.bits_unused;
        if (!target->frequencyDomainAllocation.choice.row4.buf)
          target->frequencyDomainAllocation.choice.row4.buf =
              calloc(target->frequencyDomainAllocation.choice.row4.size, sizeof(uint8_t));
        for (int i = 0; i < target->frequencyDomainAllocation.choice.row4.size; i++)
          target->frequencyDomainAllocation.choice.row4.buf[i] = source->frequencyDomainAllocation.choice.row4.buf[i];
        break;
      case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_other:
        target->frequencyDomainAllocation.choice.other.size = source->frequencyDomainAllocation.choice.other.size;
        target->frequencyDomainAllocation.choice.other.bits_unused = source->frequencyDomainAllocation.choice.other.bits_unused;
        if (!target->frequencyDomainAllocation.choice.other.buf)
          target->frequencyDomainAllocation.choice.other.buf =
              calloc(target->frequencyDomainAllocation.choice.other.size, sizeof(uint8_t));
        for (int i = 0; i < target->frequencyDomainAllocation.choice.other.size; i++)
          target->frequencyDomainAllocation.choice.other.buf[i] = source->frequencyDomainAllocation.choice.other.buf[i];
        break;
      default:
        AssertFatal(false, "Invalid entry\n");
    }
  }
  target->nrofPorts = source->nrofPorts;
  target->firstOFDMSymbolInTimeDomain = source->firstOFDMSymbolInTimeDomain;
  UPDATE_IE(target->firstOFDMSymbolInTimeDomain2, source->firstOFDMSymbolInTimeDomain2, long);
  target->cdm_Type = source->cdm_Type;
  target->density = source->density;
  target->freqBand = source->freqBand;
}

static void config_zp_CSI_RS_Resource(NR_ZP_CSI_RS_Resource_t *target, NR_ZP_CSI_RS_Resource_t *source)
{
  target->zp_CSI_RS_ResourceId = source->zp_CSI_RS_ResourceId;
  configure_csi_resourcemapping(&target->resourceMapping, &source->resourceMapping);
  if (source->periodicityAndOffset)
    UPDATE_IE(target->periodicityAndOffset,
              source->periodicityAndOffset,
              NR_CSI_ResourcePeriodicityAndOffset_t);
}

static void setup_pdschconfig(NR_PDSCH_Config_t *source, NR_PDSCH_Config_t *target)
{
  UPDATE_IE(target->dataScramblingIdentityPDSCH, source->dataScramblingIdentityPDSCH, long);
  if (source->dmrs_DownlinkForPDSCH_MappingTypeA)
    HANDLE_SETUPRELEASE_IE(target->dmrs_DownlinkForPDSCH_MappingTypeA,
                           source->dmrs_DownlinkForPDSCH_MappingTypeA,
                           NR_DMRS_DownlinkConfig_t,
                           asn_DEF_NR_SetupRelease_DMRS_DownlinkConfig);
  if (source->dmrs_DownlinkForPDSCH_MappingTypeB)
    HANDLE_SETUPRELEASE_IE(target->dmrs_DownlinkForPDSCH_MappingTypeB,
                           source->dmrs_DownlinkForPDSCH_MappingTypeB,
                           NR_DMRS_DownlinkConfig_t,
                           asn_DEF_NR_SetupRelease_DMRS_DownlinkConfig);
  // TCI States
  if (source->tci_StatesToReleaseList) {
    RELEASE_IE_FROMLIST(source->tci_StatesToReleaseList,
                        target->tci_StatesToAddModList,
                        tci_StateId);
  }
  if (source->tci_StatesToAddModList) {
    if (!target->tci_StatesToAddModList)
      target->tci_StatesToAddModList = calloc(1, sizeof(*target->tci_StatesToAddModList));
    ADDMOD_IE_FROMLIST(source->tci_StatesToAddModList,
                       target->tci_StatesToAddModList,
                       tci_StateId,
                       NR_TCI_State_t);
  }
  // end TCI States
  UPDATE_IE(target->vrb_ToPRB_Interleaver, source->vrb_ToPRB_Interleaver, long);
  target->resourceAllocation = source->resourceAllocation;
  if (source->pdsch_TimeDomainAllocationList)
    HANDLE_SETUPRELEASE_IE(target->pdsch_TimeDomainAllocationList,
                           source->pdsch_TimeDomainAllocationList,
                           NR_PDSCH_TimeDomainResourceAllocationList_t,
                           asn_DEF_NR_SetupRelease_PDSCH_TimeDomainResourceAllocationList);
  UPDATE_IE(target->pdsch_AggregationFactor, source->pdsch_AggregationFactor, long);
  // rateMatchPattern
  if (source->rateMatchPatternToReleaseList) {
    RELEASE_IE_FROMLIST(source->rateMatchPatternToReleaseList,
                        target->rateMatchPatternToAddModList,
                        rateMatchPatternId);
  }
  if (source->rateMatchPatternToAddModList) {
    if (!target->rateMatchPatternToAddModList)
      target->rateMatchPatternToAddModList = calloc(1, sizeof(*target->rateMatchPatternToAddModList));
    ADDMOD_IE_FROMLIST(source->rateMatchPatternToAddModList,
                       target->rateMatchPatternToAddModList,
                       rateMatchPatternId,
                       NR_RateMatchPattern_t);
  }
  // end rateMatchPattern
  UPDATE_IE(target->rateMatchPatternGroup1, source->rateMatchPatternGroup1, NR_RateMatchPatternGroup_t);
  UPDATE_IE(target->rateMatchPatternGroup2, source->rateMatchPatternGroup2, NR_RateMatchPatternGroup_t);
  target->rbg_Size = source->rbg_Size;
  UPDATE_IE(target->mcs_Table, source->mcs_Table, long);
  UPDATE_IE(target->maxNrofCodeWordsScheduledByDCI, source->maxNrofCodeWordsScheduledByDCI, long);
  UPDATE_NP_IE(target->prb_BundlingType, source->prb_BundlingType, struct NR_PDSCH_Config__prb_BundlingType);
  if (source->zp_CSI_RS_ResourceToAddModList) {
    if (!target->zp_CSI_RS_ResourceToAddModList)
      target->zp_CSI_RS_ResourceToAddModList = calloc(1, sizeof(*target->zp_CSI_RS_ResourceToAddModList));
    ADDMOD_IE_FROMLIST_WFUNCTION(source->zp_CSI_RS_ResourceToAddModList,
                                 target->zp_CSI_RS_ResourceToAddModList,
                                 zp_CSI_RS_ResourceId,
                                 NR_ZP_CSI_RS_Resource_t,
                                 config_zp_CSI_RS_Resource);
  }
  if (source->zp_CSI_RS_ResourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->zp_CSI_RS_ResourceToReleaseList,
                        target->zp_CSI_RS_ResourceToAddModList,
                        zp_CSI_RS_ResourceId);
  }
  if (source->p_ZP_CSI_RS_ResourceSet)
    HANDLE_SETUPRELEASE_IE(target->p_ZP_CSI_RS_ResourceSet,
                           source->p_ZP_CSI_RS_ResourceSet,
                           NR_ZP_CSI_RS_ResourceSet_t,
                           asn_DEF_NR_ZP_CSI_RS_ResourceSet);
  AssertFatal(source->aperiodic_ZP_CSI_RS_ResourceSetsToAddModList == NULL, "Not handled\n");
  AssertFatal(source->sp_ZP_CSI_RS_ResourceSetsToAddModList == NULL, "Not handled\n");
  if (source->ext3) {
    if (!target->ext3)
      target->ext3 = calloc(1, sizeof(*target->ext3));
    UPDATE_IE(target->ext3->harq_ProcessNumberSizeDCI_1_1_r17, source->ext3->harq_ProcessNumberSizeDCI_1_1_r17, long);
  } else if (target->ext3) {
    free_and_zero(target->ext3->harq_ProcessNumberSizeDCI_1_1_r17);
  }
}

static void setup_sr_resource(NR_SchedulingRequestResourceConfig_t *target, NR_SchedulingRequestResourceConfig_t *source)
{
  target->schedulingRequestResourceId = source->schedulingRequestResourceId;
  target->schedulingRequestID = source->schedulingRequestID;
  if (source->periodicityAndOffset)
    UPDATE_IE(target->periodicityAndOffset,
              source->periodicityAndOffset,
              struct NR_SchedulingRequestResourceConfig__periodicityAndOffset);
  if (source->resource)
    UPDATE_IE(target->resource, source->resource, NR_PUCCH_ResourceId_t);
}

static void setup_pucchconfig(NR_PUCCH_Config_t *source, NR_PUCCH_Config_t *target)
{
  // PUCCH-ResourceSet
  if (source->resourceSetToAddModList) {
    if (!target->resourceSetToAddModList)
      target->resourceSetToAddModList = calloc(1, sizeof(*target->resourceSetToAddModList));
    ADDMOD_IE_FROMLIST(source->resourceSetToAddModList,
                       target->resourceSetToAddModList,
                       pucch_ResourceSetId,
                       NR_PUCCH_ResourceSet_t);
  }
  if (source->resourceSetToReleaseList) {
    RELEASE_IE_FROMLIST(source->resourceSetToReleaseList,
                        target->resourceSetToAddModList,
                        pucch_ResourceSetId);
  }
  // PUCCH-Resource
  if (source->resourceToAddModList) {
    if (!target->resourceToAddModList)
      target->resourceToAddModList = calloc(1, sizeof(*target->resourceToAddModList));
    ADDMOD_IE_FROMLIST(source->resourceToAddModList,
                       target->resourceToAddModList,
                       pucch_ResourceId,
                       NR_PUCCH_Resource_t);
  }
  if (source->resourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->resourceToReleaseList,
                        target->resourceToAddModList,
                        pucch_ResourceId);
  }
  // PUCCH-FormatConfig
  if (source->format1)
    HANDLE_SETUPRELEASE_IE(target->format1,
                           source->format1,
                           NR_PUCCH_FormatConfig_t,
                           asn_DEF_NR_SetupRelease_PUCCH_FormatConfig);
  if (source->format2)
    HANDLE_SETUPRELEASE_IE(target->format2,
                           source->format2,
                           NR_PUCCH_FormatConfig_t,
                           asn_DEF_NR_SetupRelease_PUCCH_FormatConfig);
  if (source->format3)
    HANDLE_SETUPRELEASE_IE(target->format3,
                           source->format3,
                           NR_PUCCH_FormatConfig_t,
                           asn_DEF_NR_SetupRelease_PUCCH_FormatConfig);
  if (source->format4)
    HANDLE_SETUPRELEASE_IE(target->format4,
                           source->format4,
                           NR_PUCCH_FormatConfig_t,
                           asn_DEF_NR_SetupRelease_PUCCH_FormatConfig);
  // SchedulingRequestResourceConfig
  if (source->schedulingRequestResourceToAddModList) {
    if (!target->schedulingRequestResourceToAddModList)
      target->schedulingRequestResourceToAddModList = calloc(1, sizeof(*target->schedulingRequestResourceToAddModList));
    ADDMOD_IE_FROMLIST_WFUNCTION(source->schedulingRequestResourceToAddModList,
                                 target->schedulingRequestResourceToAddModList,
                                 schedulingRequestResourceId,
                                 NR_SchedulingRequestResourceConfig_t,
                                 setup_sr_resource);
  }
  if (source->schedulingRequestResourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->schedulingRequestResourceToReleaseList,
                        target->schedulingRequestResourceToAddModList,
                        schedulingRequestResourceId);
  }

  if (source->multi_CSI_PUCCH_ResourceList)
    UPDATE_IE(target->multi_CSI_PUCCH_ResourceList,
              source->multi_CSI_PUCCH_ResourceList,
              struct NR_PUCCH_Config__multi_CSI_PUCCH_ResourceList);
  if (source->dl_DataToUL_ACK)
    UPDATE_IE(target->dl_DataToUL_ACK, source->dl_DataToUL_ACK, struct NR_PUCCH_Config__dl_DataToUL_ACK);
  // PUCCH-SpatialRelationInfo
  if (source->spatialRelationInfoToAddModList) {
    if (!target->spatialRelationInfoToAddModList)
      target->spatialRelationInfoToAddModList = calloc(1, sizeof(*target->spatialRelationInfoToAddModList));
    ADDMOD_IE_FROMLIST(source->spatialRelationInfoToAddModList,
                       target->spatialRelationInfoToAddModList,
                       pucch_SpatialRelationInfoId,
                       NR_PUCCH_SpatialRelationInfo_t);
  }
  if (source->spatialRelationInfoToReleaseList) {
    RELEASE_IE_FROMLIST(source->spatialRelationInfoToReleaseList,
                        target->spatialRelationInfoToAddModList,
                        pucch_SpatialRelationInfoId);
  }

  if (source->pucch_PowerControl) {
    if (!target->pucch_PowerControl)
      target->pucch_PowerControl = calloc(1, sizeof(*target->pucch_PowerControl));
    UPDATE_IE(target->pucch_PowerControl->deltaF_PUCCH_f0, source->pucch_PowerControl->deltaF_PUCCH_f0, long);
    UPDATE_IE(target->pucch_PowerControl->deltaF_PUCCH_f1, source->pucch_PowerControl->deltaF_PUCCH_f1, long);
    UPDATE_IE(target->pucch_PowerControl->deltaF_PUCCH_f2, source->pucch_PowerControl->deltaF_PUCCH_f2, long);
    UPDATE_IE(target->pucch_PowerControl->deltaF_PUCCH_f3, source->pucch_PowerControl->deltaF_PUCCH_f3, long);
    UPDATE_IE(target->pucch_PowerControl->deltaF_PUCCH_f4, source->pucch_PowerControl->deltaF_PUCCH_f4, long);
    if (source->pucch_PowerControl->p0_Set)
      UPDATE_IE(target->pucch_PowerControl->p0_Set, source->pucch_PowerControl->p0_Set, struct NR_PUCCH_PowerControl__p0_Set);
    if (source->pucch_PowerControl->pathlossReferenceRSs)
      UPDATE_IE(target->pucch_PowerControl->pathlossReferenceRSs,
                source->pucch_PowerControl->pathlossReferenceRSs,
                struct NR_PUCCH_PowerControl__pathlossReferenceRSs);
    UPDATE_IE(target->pucch_PowerControl->twoPUCCH_PC_AdjustmentStates,
              source->pucch_PowerControl->twoPUCCH_PC_AdjustmentStates,
              long);
  }
}

static void handle_aperiodic_srs_type(struct NR_SRS_ResourceSet__resourceType__aperiodic *source,
                                      struct NR_SRS_ResourceSet__resourceType__aperiodic *target)
{
  target->aperiodicSRS_ResourceTrigger = source->aperiodicSRS_ResourceTrigger;
  if (source->csi_RS)
    UPDATE_IE(target->csi_RS, source->csi_RS, NR_NZP_CSI_RS_ResourceId_t);
  UPDATE_IE(target->slotOffset, source->slotOffset, long);
  if (source->ext1 && source->ext1->aperiodicSRS_ResourceTriggerList)
    UPDATE_IE(target->ext1->aperiodicSRS_ResourceTriggerList,
              source->ext1->aperiodicSRS_ResourceTriggerList,
              struct NR_SRS_ResourceSet__resourceType__aperiodic__ext1__aperiodicSRS_ResourceTriggerList);
}

static void setup_srsresourceset(NR_UE_UL_BWP_t *bwp, NR_SRS_ResourceSet_t *target, NR_SRS_ResourceSet_t *source)
{
  target->srs_ResourceSetId = source->srs_ResourceSetId;
  if (source->srs_ResourceIdList)
    UPDATE_IE(target->srs_ResourceIdList, source->srs_ResourceIdList, struct NR_SRS_ResourceSet__srs_ResourceIdList);

  if (target->resourceType.present != source->resourceType.present) {
    UPDATE_NP_IE(target->resourceType, source->resourceType, struct NR_SRS_ResourceSet__resourceType);
  }
  else {
    switch (source->resourceType.present) {
      case NR_SRS_ResourceSet__resourceType_PR_aperiodic:
        handle_aperiodic_srs_type(source->resourceType.choice.aperiodic, target->resourceType.choice.aperiodic);
        break;
      case NR_SRS_ResourceSet__resourceType_PR_periodic:
        if (source->resourceType.choice.periodic->associatedCSI_RS)
          UPDATE_IE(target->resourceType.choice.periodic->associatedCSI_RS,
                    source->resourceType.choice.periodic->associatedCSI_RS,
                    NR_NZP_CSI_RS_ResourceId_t);
        break;
      case NR_SRS_ResourceSet__resourceType_PR_semi_persistent:
        if (source->resourceType.choice.semi_persistent->associatedCSI_RS)
          UPDATE_IE(target->resourceType.choice.semi_persistent->associatedCSI_RS,
                    source->resourceType.choice.semi_persistent->associatedCSI_RS,
                    NR_NZP_CSI_RS_ResourceId_t);
        break;
      default:
        break;
    }
  }
  target->usage = source->usage;
  if (source->alpha) {
    bwp->h_b_f_c = 0;
  }
  UPDATE_IE(target->alpha, source->alpha, NR_Alpha_t);
  if (source->p0) {
    bwp->h_b_f_c = 0;
    UPDATE_IE(target->p0, source->p0, long);
  }
  if (source->pathlossReferenceRS)
    UPDATE_IE(target->pathlossReferenceRS, source->pathlossReferenceRS, struct NR_PathlossReferenceRS_Config);
  UPDATE_IE(target->srs_PowerControlAdjustmentStates, source->srs_PowerControlAdjustmentStates, long);
}

static void setup_srsconfig(NR_UE_UL_BWP_t *bwp, NR_SRS_Config_t *source, NR_SRS_Config_t *target)
{
  UPDATE_IE(target->tpc_Accumulation, source->tpc_Accumulation, long);
  // SRS-Resource
  if (source->srs_ResourceToAddModList) {
    if (!target->srs_ResourceToAddModList)
      target->srs_ResourceToAddModList = calloc(1, sizeof(*target->srs_ResourceToAddModList));
    ADDMOD_IE_FROMLIST(source->srs_ResourceToAddModList,
                       target->srs_ResourceToAddModList,
                       srs_ResourceId,
                       NR_SRS_Resource_t);
  }
  if (source->srs_ResourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->srs_ResourceToReleaseList,
                        target->srs_ResourceToAddModList,
                        srs_ResourceId);
  }
  // SRS-ResourceSet
  struct NR_SRS_Config__srs_ResourceSetToAddModList *source_srs_list = source->srs_ResourceSetToAddModList;
  if (source_srs_list) {
    if (!target->srs_ResourceSetToAddModList)
      target->srs_ResourceSetToAddModList = calloc(1, sizeof(*target->srs_ResourceSetToAddModList));
    struct NR_SRS_Config__srs_ResourceSetToAddModList *target_srs_list = target->srs_ResourceSetToAddModList;

    for (int i = 0; i < source_srs_list->list.count; i++) {
      long srs_resource_id = source_srs_list->list.array[i]->srs_ResourceSetId;
      int j;
      for (j = 0; j < target_srs_list->list.count; j++) {
        if (srs_resource_id == target_srs_list->list.array[j]->srs_ResourceSetId)
          break;
      }
      if (j == target_srs_list->list.count) {
        NR_SRS_ResourceSet_t *new = calloc(1, sizeof(*new));
        ASN_SEQUENCE_ADD(&target_srs_list->list, new);
      }
      setup_srsresourceset(bwp, target_srs_list->list.array[j], source_srs_list->list.array[i]);
    }
  }
  if (source->srs_ResourceSetToReleaseList) {
    RELEASE_IE_FROMLIST(source->srs_ResourceSetToReleaseList,
                        target->srs_ResourceSetToAddModList,
                        srs_ResourceSetId);
  }
}

NR_UE_DL_BWP_t *get_dl_bwp_structure(NR_UE_MAC_INST_t *mac, int bwp_id, bool setup)
{
  NR_UE_DL_BWP_t *bwp = NULL;
  for (int i = 0; i < mac->dl_BWPs.count; i++) {
    if (bwp_id == mac->dl_BWPs.array[i]->bwp_id) {
      bwp = mac->dl_BWPs.array[i];
      break;
    }
  }
  if (!bwp && setup) {
    bwp = calloc(1, sizeof(*bwp));
    ASN_SEQUENCE_ADD(&mac->dl_BWPs, bwp);
  }
  if (!setup) {
    mac->sc_info.n_dl_bwp = mac->dl_BWPs.count - 1;
    mac->sc_info.dl_bw_tbslbrm = 0;
    for (int i = 0; i < mac->dl_BWPs.count; i++) {
      if (mac->dl_BWPs.array[i]->BWPSize > mac->sc_info.dl_bw_tbslbrm)
        mac->sc_info.dl_bw_tbslbrm = mac->dl_BWPs.array[i]->BWPSize;
    }
  }
  return bwp;
}

NR_UE_UL_BWP_t *get_ul_bwp_structure(NR_UE_MAC_INST_t *mac, int bwp_id, bool setup)
{
  NR_UE_UL_BWP_t *bwp = NULL;
  for (int i = 0; i < mac->ul_BWPs.count; i++) {
    if (bwp_id == mac->ul_BWPs.array[i]->bwp_id) {
      bwp = mac->ul_BWPs.array[i];
      break;
    }
  }
  if (!bwp && setup) {
    bwp = calloc(1, sizeof(*bwp));
    ASN_SEQUENCE_ADD(&mac->ul_BWPs, bwp);
  }
  if (!setup) {
    mac->sc_info.n_ul_bwp = mac->ul_BWPs.count - 1;
    mac->sc_info.ul_bw_tbslbrm = 0;
    for (int i = 0; i < mac->ul_BWPs.count; i++) {
      if (mac->ul_BWPs.array[i]->BWPSize > mac->sc_info.ul_bw_tbslbrm)
        mac->sc_info.ul_bw_tbslbrm = mac->ul_BWPs.array[i]->BWPSize;
    }
  }
  return bwp;
}

static void configure_dedicated_BWP_dl(NR_UE_MAC_INST_t *mac, int bwp_id, NR_BWP_DownlinkDedicated_t *dl_dedicated)
{
  if (dl_dedicated) {
    NR_UE_DL_BWP_t *bwp = get_dl_bwp_structure(mac, bwp_id, true);
    bwp->bwp_id = bwp_id;
    NR_BWP_PDCCH_t *pdcch = &mac->config_BWP_PDCCH[bwp_id];
    if(dl_dedicated->pdsch_Config) {
      if (dl_dedicated->pdsch_Config->present == NR_SetupRelease_PDSCH_Config_PR_release)
        asn1cFreeStruc(asn_DEF_NR_PDSCH_Config, bwp->pdsch_Config);
      if (dl_dedicated->pdsch_Config->present == NR_SetupRelease_PDSCH_Config_PR_setup) {
        if (!bwp->pdsch_Config)
          bwp->pdsch_Config = calloc(1, sizeof(*bwp->pdsch_Config));
        setup_pdschconfig(dl_dedicated->pdsch_Config->choice.setup, bwp->pdsch_Config);
      }
    }
    if (dl_dedicated->pdcch_Config) {
      if (dl_dedicated->pdcch_Config->present == NR_SetupRelease_PDCCH_Config_PR_release) {
        asn_sequence_empty(&pdcch->list_Coreset);
        asn_sequence_empty(&pdcch->list_SS);
      }
      if (dl_dedicated->pdcch_Config->present == NR_SetupRelease_PDCCH_Config_PR_setup)
        configure_ss_coreset(pdcch, dl_dedicated->pdcch_Config->choice.setup);
    }
    AssertFatal(!dl_dedicated->sps_Config, "SPS handling not implemented\n");
  }
}

static void configure_dedicated_BWP_ul(NR_UE_MAC_INST_t *mac, int bwp_id, NR_BWP_UplinkDedicated_t *ul_dedicated)
{
  if (ul_dedicated) {
    NR_UE_UL_BWP_t *bwp = get_ul_bwp_structure(mac, bwp_id, true);
    bwp->bwp_id = bwp_id;
    if(ul_dedicated->pucch_Config) {
      if (ul_dedicated->pucch_Config->present == NR_SetupRelease_PUCCH_Config_PR_release)
        asn1cFreeStruc(asn_DEF_NR_PUCCH_Config, bwp->pucch_Config);
      if (ul_dedicated->pucch_Config->present == NR_SetupRelease_PUCCH_Config_PR_setup) {
        if (!bwp->pucch_Config)
          bwp->pucch_Config = calloc(1, sizeof(*bwp->pucch_Config));
        setup_pucchconfig(ul_dedicated->pucch_Config->choice.setup, bwp->pucch_Config);
      }
    }
    if(ul_dedicated->pusch_Config) {
      if (ul_dedicated->pusch_Config->present == NR_SetupRelease_PUSCH_Config_PR_release)
        asn1cFreeStruc(asn_DEF_NR_PUSCH_Config, bwp->pusch_Config);
      if (ul_dedicated->pusch_Config->present == NR_SetupRelease_PUSCH_Config_PR_setup) {
        if (!bwp->pusch_Config)
          bwp->pusch_Config = calloc(1, sizeof(*bwp->pusch_Config));
        setup_puschconfig(mac, ul_dedicated->pusch_Config->choice.setup, bwp->pusch_Config);
      }
    }
    if(ul_dedicated->srs_Config) {
      if (ul_dedicated->srs_Config->present == NR_SetupRelease_SRS_Config_PR_release)
        asn1cFreeStruc(asn_DEF_NR_SRS_Config, bwp->srs_Config);
      if (ul_dedicated->srs_Config->present == NR_SetupRelease_SRS_Config_PR_setup) {
        if (!bwp->srs_Config)
          bwp->srs_Config = calloc(1, sizeof(*bwp->srs_Config));
        setup_srsconfig(bwp, ul_dedicated->srs_Config->choice.setup, bwp->srs_Config);
      }
    }
    AssertFatal(!ul_dedicated->configuredGrantConfig, "configuredGrantConfig not supported\n");
  }
}

static void configure_common_BWP_dl(NR_UE_MAC_INST_t *mac, int bwp_id, NR_BWP_DownlinkCommon_t *dl_common)
{
  if (dl_common) {
    NR_UE_DL_BWP_t *bwp = get_dl_bwp_structure(mac, bwp_id, true);
    bwp->bwp_id = bwp_id;
    NR_BWP_t *dl_genericParameters = &dl_common->genericParameters;
    bwp->scs = dl_genericParameters->subcarrierSpacing;
    bwp->cyclicprefix = dl_genericParameters->cyclicPrefix;
    bwp->BWPSize = NRRIV2BW(dl_genericParameters->locationAndBandwidth, MAX_BWP_SIZE);
    bwp->BWPStart = NRRIV2PRBOFFSET(dl_genericParameters->locationAndBandwidth, MAX_BWP_SIZE);
    if (bwp_id == 0) {
      mac->sc_info.initial_dl_BWPSize = bwp->BWPSize;
      mac->sc_info.initial_dl_BWPStart = bwp->BWPStart;
    }
    if (dl_common->pdsch_ConfigCommon) {
      if (dl_common->pdsch_ConfigCommon->present == NR_SetupRelease_PDSCH_ConfigCommon_PR_setup)
        UPDATE_IE(bwp->tdaList_Common,
                  dl_common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList,
                  NR_PDSCH_TimeDomainResourceAllocationList_t);
      if (dl_common->pdsch_ConfigCommon->present == NR_SetupRelease_PDSCH_ConfigCommon_PR_release)
        asn1cFreeStruc(asn_DEF_NR_PDSCH_TimeDomainResourceAllocationList, bwp->tdaList_Common);
    }
    NR_BWP_PDCCH_t *pdcch = &mac->config_BWP_PDCCH[bwp_id];
    if (dl_common->pdcch_ConfigCommon) {
      if (dl_common->pdcch_ConfigCommon->present == NR_SetupRelease_PDCCH_ConfigCommon_PR_setup)
        configure_common_ss_coreset(pdcch, dl_common->pdcch_ConfigCommon->choice.setup);
      if (dl_common->pdcch_ConfigCommon->present == NR_SetupRelease_PDCCH_ConfigCommon_PR_release)
        release_common_ss_cset(pdcch);
    }
  }
}

static void configure_common_BWP_ul(NR_UE_MAC_INST_t *mac, int bwp_id, NR_BWP_UplinkCommon_t *ul_common)
{
  if (ul_common) {
    NR_UE_UL_BWP_t *bwp = get_ul_bwp_structure(mac, bwp_id, true);
    bwp->bwp_id = bwp_id;
    NR_BWP_t *ul_genericParameters = &ul_common->genericParameters;
    bwp->scs = ul_genericParameters->subcarrierSpacing;
    bwp->cyclicprefix = ul_genericParameters->cyclicPrefix;
    bwp->BWPSize = NRRIV2BW(ul_genericParameters->locationAndBandwidth, MAX_BWP_SIZE);
    bwp->BWPStart = NRRIV2PRBOFFSET(ul_genericParameters->locationAndBandwidth, MAX_BWP_SIZE);
    // For power calculations assume the UE channel is the smallest channel that can support the BWP
    int bw_index = get_smallest_supported_bandwidth_index(bwp->scs, mac->frequency_range, bwp->BWPSize);
    bwp->channel_bandwidth = get_supported_bw_mhz(mac->frequency_range, bw_index);
    // Minumum transmission power depends on bandwidth, precalculate it here
    bwp->P_CMIN = nr_get_Pcmin(bw_index);
    bwp->srs_power_control_initialized = false;
    if (bwp_id == 0) {
      mac->sc_info.initial_ul_BWPSize = bwp->BWPSize;
      mac->sc_info.initial_ul_BWPStart = bwp->BWPStart;
    }
    if (ul_common->rach_ConfigCommon) {
      // ssb_perRACH_OccasionAndCB_PreamblesPerSSB is need M
      // if NULL we need to maintain the information
      NR_SetupRelease_RACH_ConfigCommon_t *rachcommon = ul_common->rach_ConfigCommon;
      if (rachcommon->present == NR_SetupRelease_RACH_ConfigCommon_PR_setup
          && rachcommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB)
        mac->ssb_ro_preambles = get_ssb_ro_preambles_4step(rachcommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB);
      HANDLE_SETUPRELEASE_DIRECT(bwp->rach_ConfigCommon, rachcommon, NR_RACH_ConfigCommon_t, asn_DEF_NR_RACH_ConfigCommon);
    }
    if (ul_common->ext1 && ul_common->ext1->msgA_ConfigCommon_r16) {
      NR_MsgA_ConfigCommon_r16_t *msgacc = ul_common->ext1->msgA_ConfigCommon_r16->choice.setup;
      if (msgacc->rach_ConfigCommonTwoStepRA_r16.msgA_SSB_PerRACH_OccasionAndCB_PreamblesPerSSB_r16) {
        mac->ssb_ro_preambles =
            get_ssb_ro_preambles_2step(msgacc->rach_ConfigCommonTwoStepRA_r16.msgA_SSB_PerRACH_OccasionAndCB_PreamblesPerSSB_r16);
      }
      HANDLE_SETUPRELEASE_DIRECT(bwp->msgA_ConfigCommon_r16,
                                 ul_common->ext1->msgA_ConfigCommon_r16,
                                 NR_MsgA_ConfigCommon_r16_t,
                                 asn_DEF_NR_MsgA_ConfigCommon_r16);
    }
    if (ul_common->pucch_ConfigCommon)
      HANDLE_SETUPRELEASE_DIRECT(bwp->pucch_ConfigCommon,
                                 ul_common->pucch_ConfigCommon,
                                 NR_PUCCH_ConfigCommon_t,
                                 asn_DEF_NR_PUCCH_ConfigCommon);
    if (ul_common->pusch_ConfigCommon) {
      if (ul_common->pusch_ConfigCommon->present == NR_SetupRelease_PUSCH_ConfigCommon_PR_setup) {
        UPDATE_IE(bwp->tdaList_Common,
                  ul_common->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList,
                  NR_PUSCH_TimeDomainResourceAllocationList_t);
        UPDATE_IE(bwp->msg3_DeltaPreamble, ul_common->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble, long);
        UPDATE_IE(bwp->p0_NominalWithGrant, ul_common->pusch_ConfigCommon->choice.setup->p0_NominalWithGrant, long);
      }
      if (ul_common->pusch_ConfigCommon->present == NR_SetupRelease_PUSCH_ConfigCommon_PR_release) {
        asn1cFreeStruc(asn_DEF_NR_PUSCH_TimeDomainResourceAllocationList, bwp->tdaList_Common);
        free_and_zero(bwp->msg3_DeltaPreamble);
        free_and_zero(bwp->p0_NominalWithGrant);
      }
    }
  }
}

static void configure_timeAlignmentTimer(NR_timer_t *time_alignment_timer, NR_TimeAlignmentTimer_t timer_config, int scs)
{
  uint32_t timer_ms = 0;
  switch (timer_config) {
    case NR_TimeAlignmentTimer_ms500 :
      timer_ms = 500;
      break;
    case NR_TimeAlignmentTimer_ms750 :
      timer_ms = 750;
      break;
    case NR_TimeAlignmentTimer_ms1280 :
      timer_ms = 1280;
      break;
    case NR_TimeAlignmentTimer_ms1920 :
      timer_ms = 1920;
      break;
    case NR_TimeAlignmentTimer_ms2560 :
      timer_ms = 2560;
      break;
    case NR_TimeAlignmentTimer_ms5120 :
      timer_ms = 5120;
      break;
    case NR_TimeAlignmentTimer_ms10240 :
      timer_ms = 10240;
      break;
    case NR_TimeAlignmentTimer_infinity :
      timer_ms = UINT_MAX;
      break;
    default :
      AssertFatal(false, "Invalid timeAlignmentTimer\n");
  }
  // length of slot is (1/2^scs)ms
  uint32_t n_slots = timer_ms != UINT_MAX ? (timer_ms << scs) : UINT_MAX;
  bool timer_was_active = nr_timer_is_active(time_alignment_timer);
  nr_timer_setup(time_alignment_timer, n_slots, 1); // 1 slot update rate
  if (timer_was_active)
    nr_timer_start(time_alignment_timer);
}

void nr_rrc_mac_config_req_reset(module_id_t module_id, NR_UE_MAC_reset_cause_t cause)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  fapi_nr_synch_request_t sync_req = {.target_Nid_cell = -1, .ssb_bw_scan = true};
  switch (cause) {
    case GO_TO_IDLE:
      /* TS 38.331 §5.3.11: enter RRC_IDLE and perform cell selection per TS 38.304. */
      reset_ra(mac, true);
      nr_ue_init_mac(mac);
      release_mac_configuration(mac, cause);
      nr_ue_mac_default_configs(mac);
      // new sync but no target cell id -> -1
      nr_ue_send_synch_request(mac, module_id, 0, &sync_req);
      break;
    case GO_TO_IDLE_KEEP_CAMPED:
      /* Normal no-redirection RRCRelease: keep the selected camped cell context for TS 38.304 §5.2.5/§7.1
       * paging, while releasing connected-mode MAC state. RA stays idle until paging/NAS triggers access. */
      // Stop any in-progress RA and discard PRACH resources
      reset_ra(mac, true);
      // Apply TS 38.321 §5.12 MAC reset (no PHY sync reinit)
      reset_mac_inst(mac);
      // Release dedicated MAC config (keep SIB1/common BWP0/paging PDCCH for the camped cell)
      release_mac_configuration(mac, cause);
      // Restore default MAC Cell Group timers/config (after the reset)
      nr_ue_mac_default_configs(mac);
      mac->ra.ra_state = nrRA_UE_IDLE;
      mac->ra.RA_active = false;
      mac->state = UE_IDLE;
      break;
    case DETACH:
      LOG_A(NR_MAC, "Received detach indication\n");
      reset_ra(mac, true);
      reset_mac_inst(mac);
      nr_ue_reset_sync_state(mac, false);
      release_mac_configuration(mac, cause);
      mac->state = UE_DETACHING;
      break;
    case REJECT:
      reset_ra(mac, false);
      reset_mac_inst(mac);
      mac->state = UE_BARRED;
      break;
    case RRC_SETUP_REESTAB_RESUME:
      for (int i = mac->lc_ordered_list.count; i > 0; i--) {
        nr_lcordered_info_t *lc = mac->lc_ordered_list.array[i - 1];
        if (lc->rb.type == NR_LCID_SRB && lc->rb.choice.srb_id == 0)
          continue;
        asn_sequence_del(&mac->lc_ordered_list, i - 1, 1);
      }
      release_mac_configuration(mac, cause);
      nr_ue_mac_default_configs(mac);
      break;
    case UL_SYNC_LOST_T430_EXPIRED:
      // TS 38.331 Section 5.2.2.6, TS 38.321 Section 5.2a
      // Flush all HARQ buffers and Stop UL transmissions
      handle_ulsync_loss(mac);
      break;
    case T304_EXPIRED:
      // TS 38.331 5.3.5.8.3: release rach-ConfigDedicated, revert back to the source PCell
      if (mac->ra.rach_ConfigDedicated)
        asn1cFreeStruc(asn_DEF_NR_RACH_ConfigDedicated, mac->ra.rach_ConfigDedicated);
      mac->physCellId = mac->ho_source.physCellId;
      mac->phy_config.config_req.carrier_config.dl_frequency = mac->ho_source.dl_frequency;
      mac->phy_config.config_req.carrier_config.uplink_frequency = mac->ho_source.uplink_frequency;
      break;
    default:
      AssertFatal(false, "Invalid MAC reset cause %d\n", cause);
  }
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

bool is_lcid_suspended(NR_UE_MAC_INST_t *mac, int lcid)
{
  for (int j = 0; j < mac->lc_ordered_list.count; j++) {
    nr_lcordered_info_t *lc = mac->lc_ordered_list.array[j];
    if (lc->lcid == lcid)
      return lc->rb_suspended;
  }
  LOG_E(NR_MAC, "LCID %d not found in the MAC list\n", lcid);
  return false;
}

void nr_rrc_mac_resume_rb(module_id_t module_id, bool is_srb, int rb_id)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  for (int j = 0; j < mac->lc_ordered_list.count; j++) {
    nr_lcordered_info_t *lc = mac->lc_ordered_list.array[j];
    if (is_srb && lc->rb.type == NR_LCID_SRB && lc->rb.choice.srb_id == rb_id)
      lc->rb_suspended = false;
    if (!is_srb && lc->rb.type == NR_LCID_DRB && lc->rb.choice.drb_id == rb_id)
      lc->rb_suspended = false;
  }
}

static void configure_si_schedulingInfo(NR_UE_MAC_INST_t *mac,
                                        NR_SI_SchedulingInfo_t *si_SchedulingInfo,
                                        NR_SI_SchedulingInfo_v1700_t *si_SchedulingInfo_v1700)
{
  asn_sequence_empty(&mac->si_SchedInfo.si_SchedInfo_list);
  if (si_SchedulingInfo) {
    mac->si_SchedInfo.si_WindowLength = si_SchedulingInfo->si_WindowLength;
    for (int i = 0; i < si_SchedulingInfo->schedulingInfoList.list.count; i++) {
      si_schedinfo_config_t *config = calloc_or_fail(1, sizeof(*config));
      config->type = NR_SI_INFO;
      config->si_WindowPosition = i + 1;
      config->si_Periodicity = si_SchedulingInfo->schedulingInfoList.list.array[i]->si_Periodicity;
      ASN_SEQUENCE_ADD(&mac->si_SchedInfo.si_SchedInfo_list, config);
    }
  }
  if (si_SchedulingInfo_v1700) {
    for (int i = 0; i < si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.count; i++) {
      si_schedinfo_config_t *config = calloc_or_fail(1, sizeof(*config));
      config->type = NR_SI_INFO_v1700;
      config->si_WindowPosition = si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.array[i]->si_WindowPosition_r17;
      config->si_Periodicity = si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.array[i]->si_Periodicity_r17;
      ASN_SEQUENCE_ADD(&mac->si_SchedInfo.si_SchedInfo_list, config);
    }
  }
}

/** @brief Parse SIB1 PCCH-Config (TS 38.331) into UE MAC paging_cfg (T, N, PF_offset, Ns, X and the
 *  per-PO start-MO list). */
static void configure_pcch_config(NR_UE_MAC_INST_t *mac, const NR_ServingCellConfigCommonSIB_t *scc)
{
  DevAssert(mac);
  DevAssert(scc);
  const NR_PCCH_Config_t *pcch = &scc->downlinkConfigCommon.pcch_Config;
  nr_ue_paging_cfg_t *paging_cfg = &mac->paging_cfg;
  /* Reset the per-PO start-MO list */
  paging_cfg->first_mo_of_po_count = 0;
  memset(paging_cfg->first_mo_of_po, 0, sizeof(paging_cfg->first_mo_of_po));

  paging_cfg->T = nr_pcch_default_paging_cycle_rf(pcch);
  nr_pcch_n_and_paging_frame_offset(pcch, paging_cfg->T, &paging_cfg->N, &paging_cfg->PF_offset);
  paging_cfg->Ns = nr_pcch_ns_per_pf(pcch);

  /* TS 38.304 §7.1: X defaults to 1 when nrofPDCCH-MonitoringOccasionPerSSB-InPO-r16 is absent
   * (one PDCCH MO per SSB in a PO) */
  paging_cfg->X = 1;
  if (pcch->ext1 && pcch->ext1->nrofPDCCH_MonitoringOccasionPerSSB_InPO_r16) {
    const long x = *pcch->ext1->nrofPDCCH_MonitoringOccasionPerSSB_InPO_r16;
    AssertFatal(x >= 2 && x <= NR_PCCH_MAX_MO_PER_SSB_IN_PO,
                "TS 38.331 PCCH-Config: nrofPDCCH-MonitoringOccasionPerSSB-InPO-r16=%ld out of range (2..%d)\n",
                x,
                NR_PCCH_MAX_MO_PER_SSB_IN_PO);
    paging_cfg->X = x;
  }

  // TS 38.304 §7.1 / TS 38.331 PCCH-Config: when firstPDCCH-MonitoringOccasionOfPO is present, it
  // explicitly defines the starting PDCCH MO index of each PO within the PF, overriding the default
  // (i_s * S * X) computation.
  if (pcch->firstPDCCH_MonitoringOccasionOfPO) {
    for (uint8_t i = 0; i < sizeofArray(paging_cfg->first_mo_of_po); i++) {
      int v;
      if (!nr_pcch_first_pdcch_start_mo(pcch->firstPDCCH_MonitoringOccasionOfPO, i, &v))
        break;
      paging_cfg->first_mo_of_po[i] = v;
      paging_cfg->first_mo_of_po_count++;
    }
  }

  LOG_D(NR_MAC,
        "PCCH-Config parsed: T=%u N=%u Ns=%u PF_offset=%u X=%u first_mo_of_po_count=%u\n",
        paging_cfg->T,
        paging_cfg->N,
        paging_cfg->Ns,
        paging_cfg->PF_offset,
        paging_cfg->X,
        paging_cfg->first_mo_of_po_count);
}

/** @brief RRC to MAC: push 5G-S-TMSI for paging UE_ID derivation (TS 38.304 §7.1).
 * Lifecycle:
 * - RRC_IDLE after first NAS registration:       5G-S-TMSI known -> UE_ID = TMSI mod 1024.
 * - RRC_INACTIVE:                                same as above (UE also has fullI-RNTI).
 * The "no 5G-S-TMSI" window between MIB/SIB1 reception and NAS_5GMM_IND is legitimate per spec,
 * the UE must still monitor PF/PO with UE_ID = 0 for broadcast-style notifications.
 * @note Pass UINT64_MAX to invalidate (no 5G-S-TMSI). */
void nr_rrc_mac_config_req_paging_ue_id(module_id_t module_id, uint64_t fiveG_S_TMSI)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  /* TS 38.304 §7.1: UE_ID = 5G-S-TMSI mod 1024, or 0 when no 5G-S-TMSI is allocated. */
  mac->paging_cfg.ue_id = (fiveG_S_TMSI == UINT64_MAX) ? 0 : fiveG_S_TMSI % 1024;
  LOG_I(NR_MAC, "[UE %d] paging UE_ID=%u\n", mac->ue_id, mac->paging_cfg.ue_id);
  pthread_mutex_unlock(&mac->if_mutex);
}

static void nr_mac_start_ra(NR_UE_MAC_INST_t *mac, module_id_t module_id, nr_mac_ra_start_cause_t cause)
{
  switch (cause) {
    case NR_MAC_RA_START_SETUP:
    case NR_MAC_RA_START_T300:
      reset_ra(mac, false);
      reset_mac_inst(mac);
      mac->msg3_C_RNTI = false;
      mac->state = UE_PERFORMING_RA;
      break;
    case NR_MAC_RA_START_REESTABLISHMENT: {
      // cell selection on any PCI, intra-frequency only for now
      LOG_A(NR_MAC, "RA re-establishment: starting cell selection\n");
      fapi_nr_synch_request_t sync_req = {.target_Nid_cell = -1, .ssb_bw_scan = true};
      reset_mac_inst(mac);
      nr_ue_mac_default_configs(mac);
      nr_ue_reset_sync_state(mac, true);
      release_mac_configuration(mac, RE_ESTABLISHMENT);
      for (int j = 0; j < mac->lc_ordered_list.count; j++) {
        nr_lcordered_info_t *lc = mac->lc_ordered_list.array[j];
        if (lc->rb.type == NR_LCID_SRB && lc->rb.choice.srb_id == 0)
          continue;
        lc->rb_suspended = true;
      }
      configure_timeAlignmentTimer(&mac->time_alignment_timer, mac->timeAlignmentTimerCommon, mac->current_UL_BWP->scs);
      nr_ue_send_synch_request(mac, module_id, 0, &sync_req);
      break;
    }
    default:
      AssertFatal(false, "unknown nr_mac_ra_start_cause_t %d\n", cause);
  }
}

/** @brief All RRC-initiated RA entry points */
void nr_rrc_mac_start_ra(module_id_t module_id, nr_mac_ra_start_cause_t cause)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);

  nr_mac_start_ra(mac, module_id, cause);

  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

void nr_rrc_mac_config_req_sib1(module_id_t module_id, int cc_idP, NR_SIB1_t *sib1, bool can_start_ra, bool dedicated_sib1)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  NR_SI_SchedulingInfo_t *si_SchedulingInfo = sib1->si_SchedulingInfo;
  NR_SI_SchedulingInfo_v1700_t *si_SchedulingInfo_v1700 = NULL;
  if (sib1->nonCriticalExtension && sib1->nonCriticalExtension->nonCriticalExtension
      && sib1->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension) {
    si_SchedulingInfo_v1700 = sib1->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension->si_SchedulingInfo_v1700;
  }

  configure_si_schedulingInfo(mac, si_SchedulingInfo, si_SchedulingInfo_v1700);
  NR_ServingCellConfigCommonSIB_t *scc = sib1->servingCellConfigCommon;
  AssertFatal(scc, "SIB1 SCC should not be NULL\n");
  configure_pcch_config(mac, scc);

  if (scc->uplinkConfigCommon)
    mac->timeAlignmentTimerCommon = scc->uplinkConfigCommon->timeAlignmentTimerCommon;

  // If dedicatedSIB1-Delivery together with reconfigurationWithSync
  // The field has the same values as the corresponding configuration in servingCellConfigCommon
  if (!dedicated_sib1) {
    UPDATE_IE(mac->tdd_UL_DL_ConfigurationCommon, scc->tdd_UL_DL_ConfigurationCommon, NR_TDD_UL_DL_ConfigCommon_t);

    config_common_ue_sa(mac, scc, cc_idP);

    // Build the list of all the valid/transmitted SSBs according to the config
    LOG_D(NR_MAC, "Build SSB list\n");
    build_ssb_list(mac);

    int bwp_id = 0;
    configure_common_BWP_dl(mac, bwp_id, &scc->downlinkConfigCommon.initialDownlinkBWP);
    if (scc->uplinkConfigCommon)
      configure_common_BWP_ul(mac, bwp_id, &scc->uplinkConfigCommon->initialUplinkBWP);
    // set current BWP only if coming from non-connected state
    // otherwise it is just a periodically update of the SIB1 content
    if (mac->state < UE_CONNECTED) {
      mac->current_DL_BWP = get_dl_bwp_structure(mac, 0, false);
      AssertFatal(mac->current_DL_BWP, "Couldn't find DL-BWP0\n");
      mac->current_UL_BWP = get_ul_bwp_structure(mac, 0, false);
      AssertFatal(mac->current_UL_BWP, "Couldn't find DL-BWP0\n");
      configure_timeAlignmentTimer(&mac->time_alignment_timer, mac->timeAlignmentTimerCommon, mac->current_UL_BWP->scs);
    }
  }
  if (mac->state == UE_RECEIVING_SIB && can_start_ra)
    mac->state = UE_PERFORMING_RA;

  mac->if_module->phy_config_request(&mac->phy_config);
  mac->phy_config.config_req.ntn_config.params_changed = false;
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

void nr_rrc_mac_config_other_sib(module_id_t module_id, NR_SIB19_r17_t *sib19, int hfn, int frame, bool can_start_ra)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);

  if (sib19) {
    // update ntn_Config_r17 with received values
    UPDATE_IE(mac->sc_info.ntn_Config_r17, sib19->ntn_Config_r17, NR_NTN_Config_r17_t);
    configure_ntn_ta(&mac->phy_config.config_req.ntn_config, mac->sc_info.ntn_Config_r17, hfn, frame, false);
    mac->if_module->phy_config_request(&mac->phy_config);
    mac->phy_config.config_req.ntn_config.params_changed = false;
  }
  if (mac->state == UE_RECEIVING_SIB && can_start_ra)
    mac->state = UE_PERFORMING_RA;
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

static void handle_reconfiguration_with_sync(NR_UE_MAC_INST_t *mac,
                                             int cc_idP,
                                             int hfn,
                                             int frame,
                                             const NR_ReconfigurationWithSync_t *reconfWithSync)
{
  mac->ho_source.physCellId = mac->physCellId;
  mac->ho_source.dl_frequency = mac->phy_config.config_req.carrier_config.dl_frequency;
  mac->ho_source.uplink_frequency = mac->phy_config.config_req.carrier_config.uplink_frequency;

  reset_mac_inst(mac);
  mac->crnti = reconfWithSync->newUE_Identity;
  LOG_I(NR_MAC, "Configuring CRNTI %x\n", mac->crnti);

  RA_config_t *ra = &mac->ra;
  bool is_cfra = false;
  if (reconfWithSync->rach_ConfigDedicated) {
    AssertFatal(reconfWithSync->rach_ConfigDedicated->present == NR_ReconfigurationWithSync__rach_ConfigDedicated_PR_uplink,
                "RACH on supplementaryUplink not supported\n");
    NR_RACH_ConfigDedicated_t *rach_ConfigDedicated = reconfWithSync->rach_ConfigDedicated->choice.uplink;
    if (rach_ConfigDedicated->cfra)
      is_cfra = true;
    if (rach_ConfigDedicated->ext1 && rach_ConfigDedicated->ext1->cfra_TwoStep_r16)
      is_cfra = true;
    UPDATE_IE(ra->rach_ConfigDedicated, reconfWithSync->rach_ConfigDedicated->choice.uplink, NR_RACH_ConfigDedicated_t);
  }

  // in case of CBRA and reconfiguration with sync (handover)
  // the UE sends MSG3 with C-RNTI to identify itself
  if (!is_cfra)
    mac->msg3_C_RNTI = true;

  if (reconfWithSync->spCellConfigCommon) {
    NR_ServingCellConfigCommon_t *scc = reconfWithSync->spCellConfigCommon;
    if (scc->physCellId)
      mac->physCellId = *scc->physCellId;
    mac->dmrs_TypeA_Position = scc->dmrs_TypeA_Position;
    UPDATE_IE(mac->tdd_UL_DL_ConfigurationCommon, scc->tdd_UL_DL_ConfigurationCommon, NR_TDD_UL_DL_ConfigCommon_t);
    config_common_ue(mac, scc, cc_idP, hfn, frame);
    // Build the list of all the valid/transmitted SSBs according to the config
    LOG_D(NR_MAC,"Build SSB list\n");
    build_ssb_list(mac);

    const int bwp_id = 0;
    if (scc->downlinkConfigCommon)
      configure_common_BWP_dl(mac, bwp_id, scc->downlinkConfigCommon->initialDownlinkBWP);
    if (scc->uplinkConfigCommon)
      configure_common_BWP_ul(mac, bwp_id, scc->uplinkConfigCommon->initialUplinkBWP);

    // Update PDCCH config as MAC configuration has changed
    // Used only in SA mode.
    mac->update_pdcch_config = IS_SA_MODE(get_softmodem_params());
  }

  mac->state = UE_NOT_SYNC_RECONF;
  ra->ra_state = nrRA_UE_IDLE;
  nr_ue_mac_default_configs(mac);

  // PHY CONFIG request should be sent, ahead of SYNC request
  // As SYNC request processes the new config
  mac->if_module->phy_config_request(&mac->phy_config);
  mac->phy_config.config_req.ntn_config.params_changed = false;
  mac->synch_request.Mod_id = mac->ue_id;
  mac->synch_request.CC_id = cc_idP;
  mac->synch_request.synch_req.target_Nid_cell = mac->physCellId;
  mac->if_module->synch_request(&mac->synch_request);
}

static void configure_physicalcellgroup(NR_UE_MAC_INST_t *mac,
                                        const NR_PhysicalCellGroupConfig_t *phyConfig)
{
  mac->pdsch_HARQ_ACK_Codebook = phyConfig->pdsch_HARQ_ACK_Codebook;
  mac->harq_ACK_SpatialBundlingPUCCH = phyConfig->harq_ACK_SpatialBundlingPUCCH ? true : false;
  mac->harq_ACK_SpatialBundlingPUSCH = phyConfig->harq_ACK_SpatialBundlingPUSCH ? true : false;
  AssertFatal(!phyConfig->ext1 || !phyConfig->ext1->mcs_C_RNTI, "Handling of mcs-C-RNTI not implemented\n");
  NR_P_Max_t *p_NR_FR1 = phyConfig->p_NR_FR1;
  NR_P_Max_t *p_UE_FR1 = phyConfig->ext1 ? phyConfig->ext1->p_UE_FR1 : NULL;
  if (p_NR_FR1 == NULL)
    mac->p_Max_alt = p_UE_FR1 == NULL ? INT_MIN : *p_UE_FR1;
  else
    mac->p_Max_alt = p_UE_FR1 == NULL ? *p_NR_FR1 : (*p_UE_FR1 < *p_NR_FR1 ? *p_UE_FR1 : *p_NR_FR1);
}

static uint32_t get_sr_DelayTimer(long logicalChannelSR_DelayTimer)
{
  uint32_t timer = 0;
  switch (logicalChannelSR_DelayTimer) {
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf20 :
      timer = 20;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf40 :
      timer = 40;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf64 :
      timer = 64;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf128 :
      timer = 128;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf512 :
      timer = 512;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf1024 :
      timer = 1024;
      break;
    case NR_BSR_Config__logicalChannelSR_DelayTimer_sf2560 :
      timer = 2560;
      break;
    default :
      AssertFatal(false, "Invalid SR_DelayTimer %ld\n", logicalChannelSR_DelayTimer);
  }
  return timer;
}

static uint32_t nr_get_sf_retxBSRTimer(long retxBSR_Timer)
{
  uint32_t timer = 0;
  switch (retxBSR_Timer) {
    case NR_BSR_Config__retxBSR_Timer_sf10:
      timer = 10;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf20:
      timer = 20;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf40:
      timer = 40;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf80:
      timer = 80;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf160:
      timer = 160;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf320:
      timer = 320;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf640:
      timer = 640;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf1280:
      timer = 1280;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf2560:
      timer = 2560;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf5120:
      timer = 5120;
      break;
    case NR_BSR_Config__retxBSR_Timer_sf10240:
      timer = 10240;
      break;
    default:
      AssertFatal(false, "Invalid retxBSR_Timer %ld\n", retxBSR_Timer);
  }
  return timer;
}

static uint32_t nr_get_sf_periodicBSRTimer(long periodicBSR)
{
  uint32_t timer = 0;
  switch (periodicBSR) {
    case NR_BSR_Config__periodicBSR_Timer_sf1:
      timer = 1;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf5:
      timer = 5;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf10:
      timer = 10;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf16:
      timer = 16;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf20:
      timer = 20;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf32:
      timer = 32;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf40:
      timer = 40;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf64:
      timer = 64;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf80:
      timer = 80;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf128:
      timer = 128;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf160:
      timer = 160;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf320:
      timer = 320;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf640:
      timer = 640;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf1280:
      timer = 1280;
      break;
    case NR_BSR_Config__periodicBSR_Timer_sf2560:
      timer = 2560;
      break;
    case NR_BSR_Config__periodicBSR_Timer_infinity:
      timer = UINT_MAX;
      break;
    default:
      AssertFatal(false, "Invalid periodicBSR_Timer %ld\n", periodicBSR);
  }
  return timer;
}

static uint32_t get_data_inactivity_timer(long setup)
{
  uint32_t timer_s = 0;
  switch (setup) {
    case NR_DataInactivityTimer_s1 :
      timer_s = 1;
      break;
    case NR_DataInactivityTimer_s2 :
      timer_s = 2;
      break;
    case NR_DataInactivityTimer_s3 :
      timer_s = 3;
      break;
    case NR_DataInactivityTimer_s5 :
      timer_s = 5;
      break;
    case NR_DataInactivityTimer_s7 :
      timer_s = 7;
      break;
    case NR_DataInactivityTimer_s10 :
      timer_s = 10;
      break;
    case NR_DataInactivityTimer_s15 :
      timer_s = 15;
      break;
    case NR_DataInactivityTimer_s20 :
      timer_s = 20;
      break;
    case NR_DataInactivityTimer_s40 :
      timer_s = 40;
      break;
    case NR_DataInactivityTimer_s50 :
      timer_s = 50;
      break;
    case NR_DataInactivityTimer_s60 :
      timer_s = 60;
      break;
    case NR_DataInactivityTimer_s80 :
      timer_s = 80;
      break;
    case NR_DataInactivityTimer_s100 :
      timer_s = 100;
      break;
    case NR_DataInactivityTimer_s120 :
      timer_s = 120;
      break;
    case NR_DataInactivityTimer_s150 :
      timer_s = 150;
      break;
    case NR_DataInactivityTimer_s180 :
      timer_s = 180;
      break;
    default :
      AssertFatal(false, "Invalid data inactivity timer\n");
  }
  return timer_s;
}

static void configure_maccellgroup(NR_UE_MAC_INST_t *mac, const NR_MAC_CellGroupConfig_t *mcg)
{
  NR_UE_SCHEDULING_INFO *si = &mac->scheduling_info;
  int scs = mac->current_UL_BWP->scs;
  if (mcg->drx_Config)
    LOG_E(NR_MAC, "DRX not implemented! Configuration not handled!\n");
  if (mcg->schedulingRequestConfig) {
    const NR_SchedulingRequestConfig_t *src = mcg->schedulingRequestConfig;
    if (src->schedulingRequestToReleaseList) {
      for (int i = 0; i < src->schedulingRequestToReleaseList->list.count; i++) {
        NR_SchedulingRequestId_t id = *src->schedulingRequestToReleaseList->list.array[i];
        memset(&si->sr_info[id], 0, sizeof(si->sr_info[id]));
      }
    }
    if (src->schedulingRequestToAddModList) {
      for (int i = 0; i < src->schedulingRequestToAddModList->list.count; i++) {
        NR_SchedulingRequestToAddMod_t *sr = src->schedulingRequestToAddModList->list.array[i];
        nr_sr_info_t *sr_info = &si->sr_info[sr->schedulingRequestId];
        sr_info->active_SR_ID = true;
        // NR_SchedulingRequestToAddMod__sr_TransMax_n4	= 0 and so on
        // to obtain the value to configure we need to right shift 4 by the RRC parameter
        sr_info->maxTransmissions = 4 << sr->sr_TransMax;
        int target_ms = 0;
        if (sr->sr_ProhibitTimer)
          target_ms = 1 << *sr->sr_ProhibitTimer;
        if (mcg->ext4 && mcg->ext4->schedulingRequestConfig_v1700) {
          const NR_SchedulingRequestConfig_v1700_t *src_v1700 = mcg->ext4->schedulingRequestConfig_v1700;
          if (src_v1700->schedulingRequestToAddModListExt_v1700) {
            if (i < src_v1700->schedulingRequestToAddModListExt_v1700->list.count) {
              const NR_SchedulingRequestToAddModExt_v1700_t *sr_v1700 = src_v1700->schedulingRequestToAddModListExt_v1700->list.array[i];
              if (sr_v1700->sr_ProhibitTimer_v1700) {
                target_ms = 192 + 64 * *sr_v1700->sr_ProhibitTimer_v1700;
              }
            }
          }
        }
        // length of slot is (1/2^scs)ms
        nr_timer_setup(&sr_info->prohibitTimer, target_ms << scs, 1); // 1 slot update rate
      }
    }
  }
  int slots_per_subframe = mac->frame_structure.numb_slots_frame / 10;
  if (mcg->bsr_Config) {
    uint32_t periodic_sf = nr_get_sf_periodicBSRTimer(mcg->bsr_Config->periodicBSR_Timer);
    uint32_t target = periodic_sf < UINT_MAX ? periodic_sf * slots_per_subframe : periodic_sf;
    nr_timer_setup(&si->periodicBSR_Timer, target, 1); // 1 slot update rate
    nr_timer_start(&si->periodicBSR_Timer);
    uint32_t retx_sf = nr_get_sf_retxBSRTimer(mcg->bsr_Config->retxBSR_Timer);
    nr_timer_setup(&si->retxBSR_Timer, retx_sf * slots_per_subframe, 1); // 1 slot update rate
    if (mcg->bsr_Config->logicalChannelSR_DelayTimer) {
      uint32_t dt_sf = get_sr_DelayTimer(*mcg->bsr_Config->logicalChannelSR_DelayTimer);
      nr_timer_setup(&si->sr_DelayTimer, dt_sf * slots_per_subframe, 1); // 1 slot update rate
    }
  }
  if (mcg->tag_Config) {
    if (mcg->tag_Config->tag_ToReleaseList) {
      for (int i = 0; i < mcg->tag_Config->tag_ToReleaseList->list.count; i++) {
        for (int j = 0; j < mac->TAG_list.count; j++) {
          if (*mcg->tag_Config->tag_ToReleaseList->list.array[i] == mac->TAG_list.array[j]->tag_Id)
            asn_sequence_del(&mac->TAG_list, j, 1);
        }
      }
    }
    if (mcg->tag_Config->tag_ToAddModList) {
      for (int i = 0; i < mcg->tag_Config->tag_ToAddModList->list.count; i++) {
        int j;
        for (j = 0; j < mac->TAG_list.count; j++) {
          if (mac->TAG_list.array[j]->tag_Id == mcg->tag_Config->tag_ToAddModList->list.array[i]->tag_Id)
            break;
        }
        if (j < mac->TAG_list.count) {
          UPDATE_IE(mac->TAG_list.array[j], mcg->tag_Config->tag_ToAddModList->list.array[i], NR_TAG_t);
        }
        else {
          NR_TAG_t *local_tag = NULL;
          UPDATE_IE(local_tag, mcg->tag_Config->tag_ToAddModList->list.array[i], NR_TAG_t);
          ASN_SEQUENCE_ADD(&mac->TAG_list, local_tag);
        }
      }
    }
  }
  if (mcg->phr_Config) {
    nr_phr_info_t *phr_info = &si->phr_info;
    phr_info->is_configured = mcg->phr_Config->choice.setup != NULL;
    if (phr_info->is_configured) {
      struct NR_PHR_Config *config = mcg->phr_Config->choice.setup;
      AssertFatal(config->multiplePHR == 0, "mulitplePHR not supported");
      phr_info->PathlossChange_db = config->phr_Tx_PowerFactorChange;
      const int periodic_timer_sf_enum_to_sf[] = {10, 20, 50, 100, 200, 500, 1000, UINT_MAX};
      int periodic_timer_sf = periodic_timer_sf_enum_to_sf[config->phr_PeriodicTimer];
      nr_timer_setup(&phr_info->periodicPHR_Timer, periodic_timer_sf * slots_per_subframe, 1);
      const int prohibit_timer_sf_enum_to_sf[] = {0, 10, 20, 50, 100, 200, 500, 1000};
      int prohibit_timer_sf = prohibit_timer_sf_enum_to_sf[config->phr_ProhibitTimer];
      nr_timer_setup(&phr_info->prohibitPHR_Timer, prohibit_timer_sf * slots_per_subframe, 1);
      phr_info->phr_reporting = (1 << phr_cause_phr_config);
    }
  }

  if (mcg->ext1 && mcg->ext1->dataInactivityTimer) {
    struct NR_SetupRelease_DataInactivityTimer *setup_release = mcg->ext1->dataInactivityTimer;
    if (setup_release->present == NR_SetupRelease_DataInactivityTimer_PR_release)
      free(mac->data_inactivity_timer);
    if (setup_release->present == NR_SetupRelease_DataInactivityTimer_PR_setup) {
      if (!mac->data_inactivity_timer)
        mac->data_inactivity_timer = calloc(1, sizeof(*mac->data_inactivity_timer));
      uint32_t timer_s = get_data_inactivity_timer(setup_release->choice.setup); // timer in seconds
      int scs = mac->current_DL_BWP->scs;
      nr_timer_setup(mac->data_inactivity_timer, (timer_s * 1000) << scs, 1); // 1 slot update rate
    }
  }
}

static void configure_csirs_resource(NR_NZP_CSI_RS_Resource_t *target, NR_NZP_CSI_RS_Resource_t *source)
{
  configure_csi_resourcemapping(&target->resourceMapping, &source->resourceMapping);
  target->powerControlOffset = source->powerControlOffset;
  UPDATE_IE(target->powerControlOffsetSS, source->powerControlOffsetSS, long);
  target->scramblingID = source->scramblingID;
  if (source->periodicityAndOffset)
    UPDATE_IE(target->periodicityAndOffset, source->periodicityAndOffset, NR_CSI_ResourcePeriodicityAndOffset_t);
  if (source->qcl_InfoPeriodicCSI_RS)
    UPDATE_IE(target->qcl_InfoPeriodicCSI_RS, source->qcl_InfoPeriodicCSI_RS, NR_TCI_StateId_t);
}

static void configure_csiim_resource(NR_CSI_IM_Resource_t *target, NR_CSI_IM_Resource_t *source)
{
  if (source->csi_IM_ResourceElementPattern)
    UPDATE_IE(target->csi_IM_ResourceElementPattern,
              source->csi_IM_ResourceElementPattern,
              struct NR_CSI_IM_Resource__csi_IM_ResourceElementPattern);
  if (source->freqBand)
    UPDATE_IE(target->freqBand, source->freqBand, NR_CSI_FrequencyOccupation_t);
  if (source->periodicityAndOffset)
    UPDATE_IE(target->periodicityAndOffset, source->periodicityAndOffset, NR_CSI_ResourcePeriodicityAndOffset_t);
}

static void modify_csi_measconfig(NR_CSI_MeasConfig_t *source, NR_CSI_MeasConfig_t *target)
{
  if (source->reportTriggerSize)
    UPDATE_IE(target->reportTriggerSize, source->reportTriggerSize, long);
  if (source->semiPersistentOnPUSCH_TriggerStateList)
    HANDLE_SETUPRELEASE_IE(target->semiPersistentOnPUSCH_TriggerStateList,
                           source->semiPersistentOnPUSCH_TriggerStateList,
                           NR_CSI_SemiPersistentOnPUSCH_TriggerStateList_t,
                           asn_DEF_NR_SetupRelease_CSI_SemiPersistentOnPUSCH_TriggerStateList);
  // NZP-CSI-RS-Resources
  if (source->nzp_CSI_RS_ResourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->nzp_CSI_RS_ResourceToReleaseList,
                        target->nzp_CSI_RS_ResourceToAddModList,
                        nzp_CSI_RS_ResourceId);
  }
  if (source->nzp_CSI_RS_ResourceToAddModList) {
    if (!target->nzp_CSI_RS_ResourceToAddModList)
      target->nzp_CSI_RS_ResourceToAddModList = calloc(1, sizeof(*target->nzp_CSI_RS_ResourceToAddModList));
    ADDMOD_IE_FROMLIST_WFUNCTION(source->nzp_CSI_RS_ResourceToAddModList,
                                 target->nzp_CSI_RS_ResourceToAddModList,
                                 nzp_CSI_RS_ResourceId,
                                 NR_NZP_CSI_RS_Resource_t,
                                 configure_csirs_resource);
  }
  // NZP-CSI-RS-ResourceSets
  if (source->nzp_CSI_RS_ResourceSetToReleaseList) {
    RELEASE_IE_FROMLIST(source->nzp_CSI_RS_ResourceSetToReleaseList,
                        target->nzp_CSI_RS_ResourceSetToAddModList,
                        nzp_CSI_ResourceSetId);
  }
  if (source->nzp_CSI_RS_ResourceSetToAddModList) {
    if (!target->nzp_CSI_RS_ResourceSetToAddModList)
      target->nzp_CSI_RS_ResourceSetToAddModList = calloc(1, sizeof(*target->nzp_CSI_RS_ResourceSetToAddModList));
    ADDMOD_IE_FROMLIST(source->nzp_CSI_RS_ResourceSetToAddModList,
                       target->nzp_CSI_RS_ResourceSetToAddModList,
                       nzp_CSI_ResourceSetId,
                       NR_NZP_CSI_RS_ResourceSet_t);
  }
  // CSI-IM-Resource
  if (source->csi_IM_ResourceToReleaseList) {
    RELEASE_IE_FROMLIST(source->csi_IM_ResourceToReleaseList,
                        target->csi_IM_ResourceToAddModList,
                        csi_IM_ResourceId);
  }
  if (source->csi_IM_ResourceToAddModList) {
    if (!target->csi_IM_ResourceToAddModList)
      target->csi_IM_ResourceToAddModList = calloc(1, sizeof(*target->csi_IM_ResourceToAddModList));
    ADDMOD_IE_FROMLIST_WFUNCTION(source->csi_IM_ResourceToAddModList,
                                 target->csi_IM_ResourceToAddModList,
                                 csi_IM_ResourceId,
                                 NR_CSI_IM_Resource_t,
                                 configure_csiim_resource);
  }
  // CSI-IM-ResourceSets
  if (source->csi_IM_ResourceSetToReleaseList) {
    RELEASE_IE_FROMLIST(source->csi_IM_ResourceSetToReleaseList,
                        target->csi_IM_ResourceSetToAddModList,
                        csi_IM_ResourceSetId);
  }
  if (source->csi_IM_ResourceSetToAddModList) {
    if (!target->csi_IM_ResourceSetToAddModList)
      target->csi_IM_ResourceSetToAddModList = calloc(1, sizeof(*target->csi_IM_ResourceSetToAddModList));
    ADDMOD_IE_FROMLIST(source->csi_IM_ResourceSetToAddModList,
                       target->csi_IM_ResourceSetToAddModList,
                       csi_IM_ResourceSetId,
                       NR_CSI_IM_ResourceSet_t);
  }
  // CSI-SSB-ResourceSets
  if (source->csi_SSB_ResourceSetToReleaseList) {
    RELEASE_IE_FROMLIST(source->csi_SSB_ResourceSetToReleaseList,
                        target->csi_SSB_ResourceSetToAddModList,
                        csi_SSB_ResourceSetId);
  }
  if (source->csi_SSB_ResourceSetToAddModList) {
    if (!target->csi_SSB_ResourceSetToAddModList)
      target->csi_SSB_ResourceSetToAddModList = calloc(1, sizeof(*target->csi_SSB_ResourceSetToAddModList));
    ADDMOD_IE_FROMLIST(source->csi_SSB_ResourceSetToAddModList,
                       target->csi_SSB_ResourceSetToAddModList,
                       csi_SSB_ResourceSetId,
                       NR_CSI_SSB_ResourceSet_t);
  }
  // CSI-ResourceConfigs
  if (source->csi_ResourceConfigToReleaseList) {
    RELEASE_IE_FROMLIST(source->csi_ResourceConfigToReleaseList,
                        target->csi_ResourceConfigToAddModList,
                        csi_ResourceConfigId);
  }
  if (source->csi_ResourceConfigToAddModList) {
    if (!target->csi_ResourceConfigToAddModList)
      target->csi_ResourceConfigToAddModList = calloc(1, sizeof(*target->csi_ResourceConfigToAddModList));
    ADDMOD_IE_FROMLIST(source->csi_ResourceConfigToAddModList,
                       target->csi_ResourceConfigToAddModList,
                       csi_ResourceConfigId,
                       NR_CSI_ResourceConfig_t);
  }
  // CSI-ReportConfigs
  if (source->csi_ReportConfigToReleaseList) {
    RELEASE_IE_FROMLIST(source->csi_ReportConfigToReleaseList,
                        target->csi_ReportConfigToAddModList,
                        reportConfigId);
  }
  if (source->csi_ReportConfigToAddModList) {
    if (!target->csi_ReportConfigToAddModList)
      target->csi_ReportConfigToAddModList = calloc(1, sizeof(*target->csi_ReportConfigToAddModList));
    ADDMOD_IE_FROMLIST(source->csi_ReportConfigToAddModList,
                       target->csi_ReportConfigToAddModList,
                       reportConfigId,
                       NR_CSI_ReportConfig_t);
  }
}

static void configure_csiconfig(NR_UE_ServingCell_Info_t *sc_info,
                                struct NR_SetupRelease_CSI_MeasConfig *csi_MeasConfig_sr,
                                module_id_t ue_id)
{
  switch (csi_MeasConfig_sr->present) {
    case NR_SetupRelease_CSI_MeasConfig_PR_NOTHING:
      break;
    case NR_SetupRelease_CSI_MeasConfig_PR_release:
      asn1cFreeStruc(asn_DEF_NR_CSI_MeasConfig, sc_info->csi_MeasConfig);
      asn1cFreeStruc(asn_DEF_NR_CSI_AperiodicTriggerStateList, sc_info->aperiodicTriggerStateList);
      break;
    case NR_SetupRelease_CSI_MeasConfig_PR_setup: {
      // separately handling aperiodicTriggerStateList
      // because it is set directly into sc_info structure
      if (csi_MeasConfig_sr->choice.setup->aperiodicTriggerStateList)
        HANDLE_SETUPRELEASE_DIRECT(sc_info->aperiodicTriggerStateList,
                                   csi_MeasConfig_sr->choice.setup->aperiodicTriggerStateList,
                                   NR_CSI_AperiodicTriggerStateList_t,
                                   asn_DEF_NR_CSI_AperiodicTriggerStateList);
      if (!sc_info->csi_MeasConfig) { // setup
        UPDATE_IE(sc_info->csi_MeasConfig, csi_MeasConfig_sr->choice.setup, NR_CSI_MeasConfig_t);
      } else { // modification
        modify_csi_measconfig(csi_MeasConfig_sr->choice.setup, sc_info->csi_MeasConfig);
      }
      if (!check_csi_report_consistency(sc_info->csi_MeasConfig))
        nr_mac_rrc_verification_failed(ue_id);
      break;
    }
    default:
      AssertFatal(false, "Invalid case\n");
  }
}

static void configure_servingcell_info(NR_UE_MAC_INST_t *mac, NR_ServingCellConfig_t *scd)
{
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  if (scd->csi_MeasConfig) {
    configure_csiconfig(sc_info, scd->csi_MeasConfig, mac->ue_id);
    compute_csi_bitlen(sc_info->csi_MeasConfig, mac->csi_report_template);
  }

  if (scd->supplementaryUplink)
    UPDATE_IE(sc_info->supplementaryUplink, scd->supplementaryUplink, NR_UplinkConfig_t);
  if (scd->crossCarrierSchedulingConfig)
    UPDATE_IE(sc_info->crossCarrierSchedulingConfig, scd->crossCarrierSchedulingConfig, NR_CrossCarrierSchedulingConfig_t);
  if (scd->pdsch_ServingCellConfig) {
    switch (scd->pdsch_ServingCellConfig->present) {
      case NR_SetupRelease_PDSCH_ServingCellConfig_PR_NOTHING:
        break;
      case NR_SetupRelease_PDSCH_ServingCellConfig_PR_release:
        // release all configurations
        asn1cFreeStruc(asn_DEF_NR_PDSCH_CodeBlockGroupTransmission, sc_info->pdsch_CGB_Transmission);
        free_and_zero(sc_info->xOverhead_PDSCH);
        free_and_zero(sc_info->maxMIMO_Layers_PDSCH);
        free_and_zero(sc_info->nrofHARQ_ProcessesForPDSCH);
        free_and_zero(sc_info->nrofHARQ_ProcessesForPDSCH_v1700);
        asn1cFreeStruc(asn_DEF_NR_DownlinkHARQ_FeedbackDisabled_r17, sc_info->downlinkHARQ_FeedbackDisabled_r17);
        break;
      case NR_SetupRelease_PDSCH_ServingCellConfig_PR_setup: {
        NR_PDSCH_ServingCellConfig_t *pdsch_servingcellconfig = scd->pdsch_ServingCellConfig->choice.setup;
        if (pdsch_servingcellconfig->codeBlockGroupTransmission)
          HANDLE_SETUPRELEASE_DIRECT(sc_info->pdsch_CGB_Transmission,
                                     pdsch_servingcellconfig->codeBlockGroupTransmission,
                                     NR_PDSCH_CodeBlockGroupTransmission_t,
                                     asn_DEF_NR_PDSCH_CodeBlockGroupTransmission);
        UPDATE_IE(sc_info->xOverhead_PDSCH, pdsch_servingcellconfig->xOverhead, long);
        if (pdsch_servingcellconfig->ext1 && pdsch_servingcellconfig->ext1->maxMIMO_Layers)
          UPDATE_IE(sc_info->maxMIMO_Layers_PDSCH, pdsch_servingcellconfig->ext1->maxMIMO_Layers, long);
        UPDATE_IE(sc_info->nrofHARQ_ProcessesForPDSCH, pdsch_servingcellconfig->nrofHARQ_ProcessesForPDSCH, long);
        if (pdsch_servingcellconfig->ext3)
          UPDATE_IE(sc_info->nrofHARQ_ProcessesForPDSCH_v1700, pdsch_servingcellconfig->ext3->nrofHARQ_ProcessesForPDSCH_v1700, long);
        else
          free_and_zero(sc_info->nrofHARQ_ProcessesForPDSCH_v1700);
        if (pdsch_servingcellconfig->ext3 && pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17) {
          switch (pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17->present) {
            case NR_SetupRelease_DownlinkHARQ_FeedbackDisabled_r17_PR_NOTHING:
              break;
            case NR_SetupRelease_DownlinkHARQ_FeedbackDisabled_r17_PR_release:
              asn1cFreeStruc(asn_DEF_NR_DownlinkHARQ_FeedbackDisabled_r17, sc_info->downlinkHARQ_FeedbackDisabled_r17);
              break;
            case NR_SetupRelease_DownlinkHARQ_FeedbackDisabled_r17_PR_setup:
              if (sc_info->downlinkHARQ_FeedbackDisabled_r17 == NULL) {
                sc_info->downlinkHARQ_FeedbackDisabled_r17 = calloc(1, sizeof(*sc_info->downlinkHARQ_FeedbackDisabled_r17));
                sc_info->downlinkHARQ_FeedbackDisabled_r17->buf = calloc(4, sizeof(*sc_info->downlinkHARQ_FeedbackDisabled_r17->buf));
              }
              sc_info->downlinkHARQ_FeedbackDisabled_r17->size = pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17->choice.setup.size;
              sc_info->downlinkHARQ_FeedbackDisabled_r17->bits_unused = pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17->choice.setup.bits_unused;
              for (int i = 0; i < sc_info->downlinkHARQ_FeedbackDisabled_r17->size; i++)
                sc_info->downlinkHARQ_FeedbackDisabled_r17->buf[i] = pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17->choice.setup.buf[i];
              break;
            default:
              AssertFatal(false, "Invalid case\n");
          }
        }
        break;
      }
      default:
        AssertFatal(false, "Invalid case\n");
    }
  }
  if (scd->uplinkConfig && scd->uplinkConfig->pusch_ServingCellConfig) {
    switch (scd->uplinkConfig->pusch_ServingCellConfig->present) {
      case NR_SetupRelease_PUSCH_ServingCellConfig_PR_NOTHING:
        break;
      case NR_SetupRelease_PUSCH_ServingCellConfig_PR_release:
        // release all configurations
        asn1cFreeStruc(asn_DEF_NR_PUSCH_CodeBlockGroupTransmission, sc_info->pusch_CGB_Transmission);
        free_and_zero(sc_info->rateMatching_PUSCH);
        free_and_zero(sc_info->xOverhead_PUSCH);
        free_and_zero(sc_info->maxMIMO_Layers_PUSCH);
        free_and_zero(sc_info->nrofHARQ_ProcessesForPUSCH_r17);
        break;
      case NR_SetupRelease_PUSCH_ServingCellConfig_PR_setup: {
        NR_PUSCH_ServingCellConfig_t *pusch_servingcellconfig = scd->uplinkConfig->pusch_ServingCellConfig->choice.setup;
        UPDATE_IE(sc_info->rateMatching_PUSCH, pusch_servingcellconfig->rateMatching, long);
        UPDATE_IE(sc_info->xOverhead_PUSCH, pusch_servingcellconfig->xOverhead, long);
        if (pusch_servingcellconfig->ext1 && pusch_servingcellconfig->ext1->maxMIMO_Layers)
          UPDATE_IE(sc_info->maxMIMO_Layers_PUSCH, pusch_servingcellconfig->ext1->maxMIMO_Layers, long);
        if (pusch_servingcellconfig->codeBlockGroupTransmission)
          HANDLE_SETUPRELEASE_DIRECT(sc_info->pusch_CGB_Transmission,
                                     pusch_servingcellconfig->codeBlockGroupTransmission,
                                     NR_PUSCH_CodeBlockGroupTransmission_t,
                                     asn_DEF_NR_PUSCH_CodeBlockGroupTransmission);
        if (pusch_servingcellconfig->ext3)
          UPDATE_IE(sc_info->nrofHARQ_ProcessesForPUSCH_r17, pusch_servingcellconfig->ext3->nrofHARQ_ProcessesForPUSCH_r17, long);
        else
          free_and_zero(sc_info->nrofHARQ_ProcessesForPUSCH_r17);
        break;
      }
      default:
        AssertFatal(false, "Invalid case\n");
    }
  }
}

/// This function implements 38.331 Section 5.3.12: UE actions upon PUCCH/SRS release request
void release_PUCCH_SRS(NR_UE_MAC_INST_t *mac)
{
  // release PUCCH-CSI-Resources configured in CSI-ReportConfig
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  NR_CSI_MeasConfig_t *meas_config = sc_info->csi_MeasConfig;
  if (meas_config && meas_config->csi_ReportConfigToAddModList) {
    for (int i = 0; i < meas_config->csi_ReportConfigToAddModList->list.count; i++) {
      struct NR_CSI_ReportConfig__reportConfigType *type = &meas_config->csi_ReportConfigToAddModList->list.array[i]->reportConfigType;
      switch (type->present) {
        case NR_CSI_ReportConfig__reportConfigType_PR_periodic :
          for (int j = type->choice.periodic->pucch_CSI_ResourceList.list.count; j > 0 ; j--)
            asn_sequence_del(&type->choice.periodic->pucch_CSI_ResourceList.list, j - 1, 1);
          break;
        case NR_CSI_ReportConfig__reportConfigType_PR_semiPersistentOnPUCCH :
          for (int j = type->choice.semiPersistentOnPUCCH->pucch_CSI_ResourceList.list.count; j > 0 ; j--)
            asn_sequence_del(&type->choice.semiPersistentOnPUCCH->pucch_CSI_ResourceList.list, j - 1, 1);
          break;
        case NR_CSI_ReportConfig__reportConfigType_PR_semiPersistentOnPUSCH :
        case NR_CSI_ReportConfig__reportConfigType_PR_aperiodic :
          // no PUCCH config to release
          break;
        default :
          AssertFatal(false, "Invalid CSI report type\n");
      }
    }
  }

  for (int bwp = 0; bwp < mac->ul_BWPs.count; bwp++) {
    // release SchedulingRequestResourceConfig instances configured in PUCCH-Config
    NR_PUCCH_Config_t *pucch_Config = mac->ul_BWPs.array[bwp]->pucch_Config;
    if (pucch_Config)
      for (int j = pucch_Config->schedulingRequestResourceToAddModList->list.count; j > 0 ; j--)
        asn_sequence_del(&pucch_Config->schedulingRequestResourceToAddModList->list, j - 1, 1);
    // release SRS-Resource instances configured in SRS-Config
    // TODO not clear if only SRS-Resources or also the ResourceSet should be released
    NR_SRS_Config_t *srs_Config = mac->ul_BWPs.array[bwp]->srs_Config;
    if (srs_Config)
      for (int j = srs_Config->srs_ResourceToAddModList->list.count; j > 0 ; j--)
        asn_sequence_del(&srs_Config->srs_ResourceToAddModList->list, j - 1, 1);
  }
}

void release_dl_BWP(NR_UE_MAC_INST_t *mac, int index)
{
  NR_UE_DL_BWP_t *bwp = mac->dl_BWPs.array[index];
  int bwp_id = bwp->bwp_id;
  asn_sequence_del(&mac->dl_BWPs, index, 0);

  free(bwp->cyclicprefix);
  asn1cFreeStruc(asn_DEF_NR_PDSCH_TimeDomainResourceAllocationList, bwp->tdaList_Common);
  asn1cFreeStruc(asn_DEF_NR_PDSCH_Config, bwp->pdsch_Config);
  free(bwp);

  NR_BWP_PDCCH_t *pdcch = &mac->config_BWP_PDCCH[bwp_id];
  release_common_ss_cset(pdcch);
  asn_sequence_empty(&pdcch->list_Coreset);
  asn_sequence_empty(&pdcch->list_SS);
}

void release_ul_BWP(NR_UE_MAC_INST_t *mac, int index)
{
  NR_UE_UL_BWP_t *bwp = mac->ul_BWPs.array[index];
  asn_sequence_del(&mac->ul_BWPs, index, 0);

  free(bwp->cyclicprefix);
  asn1cFreeStruc(asn_DEF_NR_RACH_ConfigCommon, bwp->rach_ConfigCommon);
  asn1cFreeStruc(asn_DEF_NR_PUSCH_TimeDomainResourceAllocationList, bwp->tdaList_Common);
  asn1cFreeStruc(asn_DEF_NR_ConfiguredGrantConfig, bwp->configuredGrantConfig);
  asn1cFreeStruc(asn_DEF_NR_PUSCH_Config, bwp->pusch_Config);
  asn1cFreeStruc(asn_DEF_NR_PUCCH_Config, bwp->pucch_Config);
  asn1cFreeStruc(asn_DEF_NR_PUCCH_ConfigCommon, bwp->pucch_ConfigCommon);
  asn1cFreeStruc(asn_DEF_NR_SRS_Config, bwp->srs_Config);
  free_and_zero(bwp->msg3_DeltaPreamble);
  free_and_zero(bwp->p0_NominalWithGrant);
  free(bwp);
}

static void configure_BWPs(NR_UE_MAC_INST_t *mac, NR_ServingCellConfig_t *scd)
{
  configure_dedicated_BWP_dl(mac, 0, scd->initialDownlinkBWP);
  if (scd->downlinkBWP_ToReleaseList) {
    for (int i = 0; i < scd->downlinkBWP_ToReleaseList->list.count; i++) {
      for (int j = 0; j < mac->dl_BWPs.count; j++) {
        if (*scd->downlinkBWP_ToReleaseList->list.array[i] == mac->dl_BWPs.array[j]->bwp_id)
          release_dl_BWP(mac, j);
      }
    }
  }
  if (scd->downlinkBWP_ToAddModList) {
    for (int i = 0; i < scd->downlinkBWP_ToAddModList->list.count; i++) {
      NR_BWP_Downlink_t *bwp = scd->downlinkBWP_ToAddModList->list.array[i];
      configure_common_BWP_dl(mac, bwp->bwp_Id, bwp->bwp_Common);
      configure_dedicated_BWP_dl(mac, bwp->bwp_Id, bwp->bwp_Dedicated);
    }
  }
  if (scd->firstActiveDownlinkBWP_Id) {
    mac->current_DL_BWP = get_dl_bwp_structure(mac, *scd->firstActiveDownlinkBWP_Id, false);
    AssertFatal(mac->current_DL_BWP, "Couldn't find DL-BWP %ld\n", *scd->firstActiveDownlinkBWP_Id);
  }

  if (scd->uplinkConfig) {
    configure_dedicated_BWP_ul(mac, 0, scd->uplinkConfig->initialUplinkBWP);
    if (scd->uplinkConfig->uplinkBWP_ToReleaseList) {
      for (int i = 0; i < scd->uplinkConfig->uplinkBWP_ToReleaseList->list.count; i++) {
        for (int j = 0; j < mac->ul_BWPs.count; j++) {
          if (*scd->uplinkConfig->uplinkBWP_ToReleaseList->list.array[i] == mac->ul_BWPs.array[j]->bwp_id)
            release_ul_BWP(mac, j);
        }
      }
    }
    if (scd->uplinkConfig->uplinkBWP_ToAddModList) {
      for (int i = 0; i < scd->uplinkConfig->uplinkBWP_ToAddModList->list.count; i++) {
        NR_BWP_Uplink_t *bwp = scd->uplinkConfig->uplinkBWP_ToAddModList->list.array[i];
        configure_common_BWP_ul(mac, bwp->bwp_Id, bwp->bwp_Common);
        configure_dedicated_BWP_ul(mac, bwp->bwp_Id, bwp->bwp_Dedicated);
      }
    }
    if (scd->uplinkConfig->firstActiveUplinkBWP_Id) {
      mac->current_UL_BWP = get_ul_bwp_structure(mac, *scd->uplinkConfig->firstActiveUplinkBWP_Id, false);
      AssertFatal(mac->current_UL_BWP, "Couldn't find UL-BWP %ld\n", *scd->uplinkConfig->firstActiveUplinkBWP_Id);
    }
  }
}

static void handle_mac_uecap_info(NR_UE_MAC_INST_t *mac, NR_UE_NR_Capability_t *ue_Capability)
{
  if (!ue_Capability->featureSets)
    return;
  struct NR_FeatureSets__featureSetsDownlinkPerCC *fs_dlcc_list = ue_Capability->featureSets->featureSetsDownlinkPerCC;
  int dl_id = mac->uecap_fs_ids.dl_feature_set_percc_id;
  if (fs_dlcc_list && dl_id > 0 && dl_id <= fs_dlcc_list->list.count) {
    NR_FeatureSetDownlinkPerCC_t *fs_dl_cc = fs_dlcc_list->list.array[dl_id - 1];
    AssertFatal(mac->current_DL_BWP->scs == fs_dl_cc->supportedSubcarrierSpacingDL,
                "BWP subcarrier spacing doesn't match with featureSetsDownlinkPerCC\n");
    // No "oneLayer" value exists: single-layer PDSCH is baseline and never reported,
    // so absence here means 1 layer, not "unknown" (PDSCH always needs >= 1).
    mac->uecap_maxMIMO_PDSCH_layers = fs_dl_cc->maxNumberMIMO_LayersPDSCH ? 2 << *fs_dl_cc->maxNumberMIMO_LayersPDSCH : 1;
  }
  struct NR_FeatureSets__featureSetsUplinkPerCC *fs_ulcc_list = ue_Capability->featureSets->featureSetsUplinkPerCC;
  int ul_id = mac->uecap_fs_ids.ul_feature_set_percc_id;
  if (fs_ulcc_list && ul_id > 0 && ul_id <= fs_ulcc_list->list.count) {
    NR_FeatureSetUplinkPerCC_t *fs_ul_cc = fs_ulcc_list->list.array[ul_id - 1];
    AssertFatal(mac->current_UL_BWP->scs == fs_ul_cc->supportedSubcarrierSpacingUL,
                "BWP subcarrier spacing doesn't match with featureSetsUplinkPerCC\n");
    if (fs_ul_cc->maxNumberMIMO_LayersNonCB_PUSCH)
      mac->uecap_maxMIMO_PUSCH_layers_nocb = 1 << *fs_ul_cc->maxNumberMIMO_LayersNonCB_PUSCH;
    if (fs_ul_cc->mimo_CB_PUSCH && fs_ul_cc->mimo_CB_PUSCH->maxNumberMIMO_LayersCB_PUSCH)
      mac->uecap_maxMIMO_PUSCH_layers_cb = 1 << *fs_ul_cc->mimo_CB_PUSCH->maxNumberMIMO_LayersCB_PUSCH;
  }
}

void nr_rrc_mac_config_req_cg(module_id_t module_id,
                              int cc_idP,
                              int hfn,
                              int frame,
                              NR_CellGroupConfig_t *cell_group_config,
                              NR_UE_NR_Capability_t *ue_Capability)
{
  LOG_I(MAC,"[UE %d] Applying CellGroupConfig from gNodeB\n", module_id);
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);
  int ret = pthread_mutex_lock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
  AssertFatal(cell_group_config, "CellGroupConfig should not be NULL\n");

  if (cell_group_config->physicalCellGroupConfig)
    configure_physicalcellgroup(mac, cell_group_config->physicalCellGroupConfig);

  if (cell_group_config->spCellConfig) {
    NR_SpCellConfig_t *spCellConfig = cell_group_config->spCellConfig;
    NR_ServingCellConfig_t *scd = spCellConfig->spCellConfigDedicated;
    mac->servCellIndex = spCellConfig->servCellIndex ? *spCellConfig->servCellIndex : 0;
    if (spCellConfig->reconfigurationWithSync) {
      LOG_A(NR_MAC, "Received reconfigurationWithSync\n");
      handle_reconfiguration_with_sync(mac, cc_idP, hfn, frame, spCellConfig->reconfigurationWithSync);
    }
    if (scd) {
      mac->tag_Id = scd->tag_Id;
      configure_servingcell_info(mac, scd);
      configure_BWPs(mac, scd);
    }
  }

  if (cell_group_config->mac_CellGroupConfig)
    configure_maccellgroup(mac, cell_group_config->mac_CellGroupConfig);

  for (int j = 0; j < mac->TAG_list.count; j++) {
    // apply the Timing Advance Command for the indicated TAG
    if (mac->TAG_list.array[j]->tag_Id == mac->tag_Id)
      configure_timeAlignmentTimer(&mac->time_alignment_timer, mac->TAG_list.array[j]->timeAlignmentTimer, mac->current_UL_BWP->scs);
  }

  configure_logicalChannelBearer(mac, cell_group_config->rlc_BearerToAddModList, cell_group_config->rlc_BearerToReleaseList);

  if (ue_Capability) {
    mac->uecap_fs_ids = get_feature_set_ids(ue_Capability,
                                            mac->nr_band,
                                            mac->current_DL_BWP->scs,
                                            mac->phy_config.config_req.carrier_config.dl_bandwidth,
                                            IS_SA_MODE(get_softmodem_params()) ? NR_SA : EN_DC);
    handle_mac_uecap_info(mac, ue_Capability);
  }

  if (!mac->dl_config_request || !mac->ul_config_request)
    ue_init_config_request(mac, mac->frame_structure.numb_slots_frame);
  ret = pthread_mutex_unlock(&mac->if_mutex);
  AssertFatal(!ret, "mutex failed %d\n", ret);
}

void nr_rrc_mac_config_req_meas(module_id_t module_id, const nr_neighbor_cell_info_t *neighbor_cells, int num_neighbors)
{
  NR_UE_MAC_INST_t *mac = get_mac_inst(module_id);

  for (int i = 0; i < num_neighbors && i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
    fapi_nr_neighboring_cell_t *phy_cell = &mac->phy_config.config_req.meas_config.nr_neighboring_cell[i];
    phy_cell->Nid_cell = neighbor_cells[i].Nid_cell;
    phy_cell->Nid_cell_was_configured = phy_cell->Nid_cell == (uint16_t)-1 ? false : true;
    phy_cell->ssb_freq = neighbor_cells[i].ssb_freq;
    phy_cell->active = neighbor_cells[i].active;
  }

  mac->if_module->phy_config_request(&mac->phy_config);
}
