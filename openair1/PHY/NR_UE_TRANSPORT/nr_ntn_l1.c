/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "executables/nr-uesoftmodem.h"

void fix_ntn_epoch_hfn(PHY_VARS_NR_UE *UE, int hfn, int frame)
{
  if (!UE->nrUE_config.ntn_config.is_targetcell)
    return;
  UE->nrUE_config.ntn_config.is_targetcell = false;

  const int epoch_frame = UE->nrUE_config.ntn_config.epoch_sfn;
  const int diff1 = (frame - epoch_frame + 1024) % 1024;
  const int diff2 = (epoch_frame - frame + 1024) % 1024;

  int *epoch_hfn = &UE->nrUE_config.ntn_config.epoch_hfn;
  if (diff1 < diff2) { // epoch_frame in the past
    LOG_I(PHY, "epoch is %d frames in the past\n", diff1);
    if (epoch_frame <= frame)
      *epoch_hfn = hfn;
    else
      *epoch_hfn = hfn - 1;
  } else { // epoch_frame in the future
    LOG_I(PHY, "epoch is %d frames in the future\n", diff2);
    if (epoch_frame >= frame)
      *epoch_hfn = hfn;
    else
      *epoch_hfn = hfn + 1;
  }
  LOG_I(PHY, "setting epoch_hfn = %d\n", *epoch_hfn);
}

// calculate TA and Doppler based on the NTN-Config, if abs_subframe_tx < 0 then apply only Doppler
void apply_ntn_timing_advance_and_doppler(PHY_VARS_NR_UE *UE, const NR_DL_FRAME_PARMS *fp, int abs_subframe_tx)
{
  const fapi_nr_ntn_config_t *ntn_config_params = &UE->nrUE_config.ntn_config;

  // Handle terrestrial networks
  if (ntn_config_params->cell_specific_k_offset == 0) {
    if (abs_subframe_tx >= 0)
      UE->timing_advance_ntn = 0;
    UE->dl_Doppler_shift = 0;
    UE->ul_Doppler_shift = 0;
    return;
  }

  const int abs_subframe_epoch = ntn_config_params->epoch_subframe
                               + ntn_config_params->epoch_sfn * 10
                               + ntn_config_params->epoch_hfn * 10240;
  const int ms_since_epoch = abs_subframe_tx < 0 ? 0 : abs_subframe_tx - abs_subframe_epoch;

  const position_t pos_sat_0 = ntn_config_params->pos_sat_0;
  const position_t vel_sat_0 = ntn_config_params->vel_sat_0;

  position_t pos_sat;
  position_t vel_sat;

  const double omega = ntn_config_params->omega;
  if (omega) {
    const double cos_wt = cos(omega * ms_since_epoch);
    const double sin_wt = sin(omega * ms_since_epoch);

    const position_t pos_sat_90 = ntn_config_params->pos_sat_90;
    pos_sat = (position_t){pos_sat_0.X * cos_wt + pos_sat_90.X * sin_wt,
                           pos_sat_0.Y * cos_wt + pos_sat_90.Y * sin_wt,
                           pos_sat_0.Z * cos_wt + pos_sat_90.Z * sin_wt};

    const position_t vel_sat_90 = ntn_config_params->vel_sat_90;
    vel_sat = (position_t){vel_sat_0.X * cos_wt + vel_sat_90.X * sin_wt,
                           vel_sat_0.Y * cos_wt + vel_sat_90.Y * sin_wt,
                           vel_sat_0.Z * cos_wt + vel_sat_90.Z * sin_wt};
  } else {
    pos_sat = (position_t){pos_sat_0.X + vel_sat_0.X * ms_since_epoch / 1000,
                           pos_sat_0.Y + vel_sat_0.Y * ms_since_epoch / 1000,
                           pos_sat_0.Z + vel_sat_0.Z * ms_since_epoch / 1000};
    vel_sat = vel_sat_0;
  }

  const position_t pos_ue = UE->position;

  // calculate directional vector from SAT to UE
  const position_t dir_sat_ue = {pos_ue.X - pos_sat.X, pos_ue.Y - pos_sat.Y, pos_ue.Z - pos_sat.Z};

  // calculate distance between SAT and UE
  const double distance = sqrt(dir_sat_ue.X * dir_sat_ue.X + dir_sat_ue.Y * dir_sat_ue.Y + dir_sat_ue.Z * dir_sat_ue.Z);

  // calculate projected velocity from SAT towards UE
  const double vel_sat_ue = (vel_sat.X * dir_sat_ue.X + vel_sat.Y * dir_sat_ue.Y + vel_sat.Z * dir_sat_ue.Z) / distance;

  // calculate DL Doppler shift (moving source)
  const double dl_Doppler_shift = (vel_sat_ue / (SPEED_OF_LIGHT - vel_sat_ue)) * fp->dl_CarrierFreq;

  // calculate UL Doppler shift (moving target)
  const double ul_Doppler_shift = (vel_sat_ue / SPEED_OF_LIGHT) * fp->ul_CarrierFreq;

  // calculate round-trip-time (factor 2) between SAT and UE in ms (factor 1000)
  const double N_UE_TA_adj = 2000 * distance / SPEED_OF_LIGHT;

  const double N_common_ta_adj = ntn_config_params->N_common_ta_adj;
  const double N_common_ta_drift = ntn_config_params->N_common_ta_drift;
  const double N_common_ta_drift_variant = ntn_config_params->N_common_ta_drift_variant;

  if (abs_subframe_tx >= 0) {
    UE->timing_advance_ntn = (N_UE_TA_adj + N_common_ta_adj + N_common_ta_drift * ms_since_epoch / 1e6
                              + N_common_ta_drift_variant * ((int64_t)ms_since_epoch * ms_since_epoch) / 1e9)
                             * fp->samples_per_subframe;
  }

  UE->freq_offset -= dl_Doppler_shift - UE->dl_Doppler_shift;
  UE->dl_Doppler_shift = dl_Doppler_shift;
  UE->ul_Doppler_shift = ul_Doppler_shift;

  if (abs_subframe_tx < 0) {
    LOG_I(PHY,
          "satellite velocity towards UE = %f m/s, DL Doppler shift = %f kHz, UL Doppler shift = %f kHz\n",
          vel_sat_ue,
          dl_Doppler_shift / 1000,
          ul_Doppler_shift / 1000);
  }
}

// apply values based on the NTN-Config
void apply_ntn_config(PHY_VARS_NR_UE *UE,
                      const NR_DL_FRAME_PARMS *fp,
                      int hfn_rx,
                      int frame_rx,
                      int slot_rx,
                      int *duration_rx_to_tx,
                      int *timing_advance,
                      int *ntn_koffset,
                      bool *ntn_targetcell)
{
  // return if there is no pending update
  if (!UE->nrUE_config.ntn_config.params_changed)
    return;

  *ntn_targetcell = UE->nrUE_config.ntn_config.is_targetcell;

  // skip update if it is for the target cell
  if (UE->nrUE_config.ntn_config.is_targetcell)
    return;

  UE->nrUE_config.ntn_config.params_changed = false;

  const fapi_nr_ntn_config_t *ntn_config_params = &UE->nrUE_config.ntn_config;

  const int mu = fp->numerology_index;
  const int koffset = ntn_config_params->cell_specific_k_offset;

  *duration_rx_to_tx = get_nrUE_params()->ue_capability_rx2tx + (koffset << mu);
  if (koffset > *ntn_koffset)
    *timing_advance += get_samples_slot_duration(fp, slot_rx, (koffset - *ntn_koffset) << mu);
  else if (koffset < *ntn_koffset)
    *timing_advance -= get_samples_slot_duration(fp, slot_rx, (*ntn_koffset - koffset) << mu);
  *ntn_koffset = koffset;

  const int abs_subframe_tx = 10240 * hfn_rx + 10 * frame_rx + ((slot_rx + *duration_rx_to_tx) >> mu);
  apply_ntn_timing_advance_and_doppler(UE, fp, abs_subframe_tx);

  LOG_I(PHY,
        "k_offset: %dms, N_Common_Ta: %fms, drift: %fµs/s, variant: %fµs/s², "
        "timing_advance_ntn: %d samples, DL Doppler shift: %fkHz, UL Doppler shift: %fkHz\n",
        koffset << mu,
        ntn_config_params->N_common_ta_adj,
        ntn_config_params->N_common_ta_drift,
        ntn_config_params->N_common_ta_drift_variant,
        UE->timing_advance_ntn,
        UE->dl_Doppler_shift / 1000,
        UE->ul_Doppler_shift / 1000);
}
