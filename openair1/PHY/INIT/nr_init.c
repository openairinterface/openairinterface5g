/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "executables/softmodem-common.h"
#include "executables/nr-softmodem-common.h"
#include "common/utils/nr/nr_common.h"
#include "common/ran_context.h"
#include "PHY/defs_gNB.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_pbch_defs.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "openair1/PHY/defs_RU.h"
#include "openair1/PHY/CODING/nrLDPC_extern.h"
#include "assertions.h"
#include <math.h>
#include <complex.h>
#include "PHY/NR_TRANSPORT/nr_ulsch.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include <string.h>
#include "nfapi/open-nFAPI/fapi/inc/nr_fapi_p5_utils.h"
#include "nr_phy_common.h"

#ifdef LDPC_CUDA
#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"
#endif

static void init_DLSCH_struct(PHY_VARS_gNB *gNB);
static void destroy_DLSCH_struct(const PHY_VARS_gNB *gNB);

int l1_north_init_gNB()
{
  AssertFatal(RC.nb_nr_L1_inst > 0, "Failed to init PHY callbacks: nb_nr_L1_inst = %d\n", RC.nb_nr_L1_inst);
  AssertFatal(RC.gNB != NULL, "Failed to init PHY callbacks: RC.gNB is null\n");

  for (uint8_t i = 0; i < RC.nb_nr_L1_inst; i++) {

    if ((RC.gNB[i]->if_inst = NR_IF_Module_init(i)) < 0) {
      LOG_E(NR_PHY, "Error: Failed to initialize NR_IF_Module for gNB[%d]\n", i);
      return -1;
    }

    LOG_D(NR_PHY, "RC.gNB[%d]: installing callbacks\n", i);
    RC.gNB[i]->if_inst->NR_PHY_config_req = nr_phy_config_request;
  }

  return 0;
}

NR_gNB_PHY_STATS_t *get_phy_stats(PHY_VARS_gNB *gNB, uint16_t rnti)
{
  // TODO reimplement with hashtable? also called from both UL/DL => not
  // thread-safe
  NR_gNB_PHY_STATS_t *stats;
  int first_free = -1;
  for (int i = 0; i < MAX_MOBILES_PER_GNB; i++) {
    stats = &gNB->phy_stats[i];
    if (stats->active && stats->rnti == rnti)
      return stats;
    else if (!stats->active && first_free == -1)
      first_free = i;
  }

  if (first_free < 0)
    return NULL;

  // new stats
  stats = &gNB->phy_stats[first_free];
  stats->active = true;
  stats->rnti = rnti;
  memset(&stats->dlsch_stats, 0, sizeof(stats->dlsch_stats));
  memset(&stats->ulsch_stats, 0, sizeof(stats->ulsch_stats));
  memset(&stats->uci_stats, 0, sizeof(stats->uci_stats));
  return stats;
}

void reset_active_stats(PHY_VARS_gNB *gNB, int frame)
{
  // disactivate PHY stats if UE is inactive for a given number of frames
  for (int i = 0; i < MAX_MOBILES_PER_GNB; i++) {
    NR_gNB_PHY_STATS_t *stats = &gNB->phy_stats[i];
    if (stats->active && (((frame - stats->frame + 1024) % 1024) > NUMBER_FRAMES_PHY_UE_INACTIVE))
      stats->active = false;
  }
}

void phy_init_nr_gNB(PHY_VARS_gNB *gNB)
{
  // shortcuts
  NR_DL_FRAME_PARMS *const fp       = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  NR_gNB_COMMON *const common_vars = &gNB->common_vars;
  common_vars->analog_bf = cfg->analog_beamforming_ve.analog_bf_vendor_ext.value;
  LOG_I(PHY, "L1 configured with%s analog beamforming\n", common_vars->analog_bf ? "" : "out");
  if (common_vars->analog_bf) {
    common_vars->num_beams_period = cfg->analog_beamforming_ve.num_beams_period_vendor_ext.value;
    LOG_I(PHY, "Max number of concurrent beams: %d\n", common_vars->num_beams_period);
  } else
    common_vars->num_beams_period = 1;

  int Ptx = cfg->carrier_config.num_tx_ant.value;
  int Prx = cfg->carrier_config.num_rx_ant.value;
  int max_ul_mimo_layers = NR_MAX_NB_LAYERS;

  AssertFatal(Ptx > 0 && Ptx < 9,"Ptx %d is not supported\n", Ptx);
  AssertFatal(Prx > 0 && Prx < 9,"Prx %d is not supported\n", Prx);
  LOG_D(PHY, "[gNB %d]About to wait for gNB to be configured\n", gNB->Mod_id);

  while(gNB->configured == 0)
    usleep(10000);

  load_dftslib();

  crcTableInit();
  init_byte2m128i();
  init_pucch2_luts();

  nr_init_fde(); // Init array for frequency equalization of transform precoding of PUSCH

  gNB->max_nb_pdsch = NR_MAX_SCHED_PER_SLOT;
  init_delay_table(fp->ofdm_symbol_size, MAX_DELAY_COMP, NR_MAX_OFDM_SYMBOL_SIZE, fp->delay_table);
  init_delay_table(128, MAX_DELAY_COMP, 128, fp->delay_table128);

  gNB->bad_pucch = 0;
  if (gNB->TX_AMP == 0)
    gNB->TX_AMP = AMP;
  // ceil(((NB_RB<<1)*3)/32) // 3 RE *2(QPSK)
  nr_generate_modulation_table();
  nr_init_pbch_interleaver(gNB->nr_pbch_interleaver);

  generate_ul_reference_signal_sequences(SHRT_MAX);

  /* Generate low PAPR type 1 sequences for PUSCH DMRS, these are used if transform precoding is enabled.  */
  generate_lowpapr_typ1_refsig_sequences(SHRT_MAX);

  /// Transport init necessary for NR synchro
  init_nr_transport(gNB);

  int ret_loader = load_nrLDPC_coding_interface(NULL, &gNB->nrLDPC_coding_interface, 16 * gNB->max_nb_pusch);
  AssertFatal(ret_loader == 0, "error loading LDPC library\n");

  init_DLSCH_struct(gNB);

  /* Do NOT allocate per-antenna rxdataF: the gNB gets a pointer to the
   * RU to copy/recover freq-domain memory from there */
  common_vars->rxdataF = malloc16_clear(Prx * sizeof(*common_vars->rxdataF));

  /* beam_id array is common for tx and rx so the max number of both is taken */
  const unsigned int num_antenna_ports = max(Ptx, Prx);
  if (cfg->analog_beamforming_ve.analog_bf_vendor_ext.value) {
    common_vars->beam_id = (uint16_t **)malloc16(fp->slots_per_frame * fp->symbols_per_slot * sizeof(*common_vars->beam_id));
    for (int i = 0; i < fp->slots_per_frame * fp->symbols_per_slot; i++)
      common_vars->beam_id[i] = (uint16_t *)malloc16_clear(num_antenna_ports * sizeof(**common_vars->beam_id));
  }

  common_vars->txdataF = (c16_t **)malloc16_clear(Ptx * sizeof(*common_vars->txdataF));
  for (int j = 0; j < Ptx; j++)
    common_vars->txdataF[j] = (c16_t *)malloc16_clear(fp->samples_per_slot_wCP * sizeof(**common_vars->txdataF));

  // PRACH
  init_nr_prach(gNB);

  int N_RB_UL = cfg->carrier_config.ul_grid_size[cfg->ssb_config.scs_common.value].value;
  int n_buf = Prx*max_ul_mimo_layers;

  int nb_re_pusch = N_RB_UL * NR_NB_SC_PER_RB;
  int nb_re_pusch2 = ceil_mod(nb_re_pusch, 16);

  gNB->pusch_vars = (NR_gNB_PUSCH *)malloc16_clear(gNB->max_nb_pusch * sizeof(NR_gNB_PUSCH));
  for (int ULSCH_id = 0; ULSCH_id < gNB->max_nb_pusch; ULSCH_id++) {
    NR_gNB_PUSCH *pusch = &gNB->pusch_vars[ULSCH_id];
    pusch->ul_ch_estimates = (int32_t **)malloc16(n_buf * sizeof(int32_t *));
    for (int i = 0; i < n_buf; i++) {
      pusch->ul_ch_estimates[i] = (int32_t *)malloc16_clear(sizeof(int32_t) * fp->ofdm_symbol_size * fp->symbols_per_slot);
    }

    pusch->rxdataF_comp = (c16_t **)malloc16(max_ul_mimo_layers * sizeof(*pusch->rxdataF_comp));
    for (int i = 0; i < max_ul_mimo_layers; i++) {
      pusch->rxdataF_comp[i] = (c16_t *)malloc16_clear(sizeof(**pusch->rxdataF_comp) * nb_re_pusch2 * fp->symbols_per_slot);
    }
#ifdef LDPC_CUDA
    // 144 segments 8448*3 coded bits per segment
    pusch->ulsch_llrs = gpuHostAlloc_or_fail((144 * 3 * 8448) * sizeof(int16_t), gpuHostAllocMapped);
    pusch->llr_dev = gpuHostGetDevicePointer_or_fail(pusch->ulsch_llrs);
#else
    pusch->ulsch_llrs = (int16_t *)malloc16_clear((144 * 3 * 8448) * sizeof(int16_t)); // 144 segments 3*8448 coded bits per segment
#endif
    // TODO refine the size
    pusch->ack_llrs = (int16_t *)malloc16_clear((144 * 3 * 8448) * sizeof(int16_t));
    pusch->csi1_llrs = (int16_t *)malloc16_clear((144 * 3 * 8448) * sizeof(int16_t));
    pusch->csi2_llrs = (int16_t *)malloc16_clear((144 * 3 * 8448) * sizeof(int16_t));
    pusch->ul_valid_re_per_slot = (int16_t *)malloc16_clear(sizeof(int16_t) * fp->symbols_per_slot);
  } // ulsch_id
}

void phy_free_nr_gNB(PHY_VARS_gNB *gNB)
{
  const int Prx = gNB->gNB_config.carrier_config.num_rx_ant.value;
  const int max_ul_mimo_layers = NR_MAX_NB_LAYERS;
  const int n_buf = Prx * max_ul_mimo_layers;

  PHY_MEASUREMENTS_gNB *meas = &gNB->measurements;
  free_and_zero(meas->n0_subband_power);

  free_ul_reference_signal_sequences();
  free_gnb_lowpapr_sequences();

  reset_nr_transport(gNB);
  reset_nr_prach(gNB);

  destroy_DLSCH_struct(gNB);

  NR_gNB_COMMON * common_vars = &gNB->common_vars;
  if (common_vars->beam_id) {
    for (int j = 0; j < gNB->frame_parms.slots_per_frame * gNB->frame_parms.symbols_per_slot; j++) {
      free_and_zero(common_vars->beam_id[j]);
    }
  }
  free_and_zero(common_vars->beam_id);

  for (int i = 0; i < gNB->frame_parms.nb_antennas_tx; i++) {
    free_and_zero(common_vars->txdataF[i]);
  }
  free_and_zero(common_vars->txdataF);

  /* Do NOT free per-antenna txdataF/rxdataF: the gNB gets a pointer to the
   * RU's txdataF/rxdataF, and the RU will free that */
  free_and_zero(common_vars->rxdataF);

  for (int ULSCH_id = 0; ULSCH_id < gNB->max_nb_pusch; ULSCH_id++) {
    NR_gNB_PUSCH *pusch_vars = &gNB->pusch_vars[ULSCH_id];
    for (int i = 0; i < n_buf; i++) {
      free_and_zero(pusch_vars->ul_ch_estimates[i]);
    }
    for (int i = 0; i < max_ul_mimo_layers; i++)
      free_and_zero(pusch_vars->rxdataF_comp[i]);

    free_and_zero(pusch_vars->ul_ch_estimates);
    free_and_zero(pusch_vars->ul_valid_re_per_slot);
    free_and_zero(pusch_vars->rxdataF_comp);

#ifdef LDPC_CUDA
    gpuFreeHost(pusch_vars->llr_dev);
#else
    free_and_zero(pusch_vars->ulsch_llrs);
#endif
    free_and_zero(pusch_vars->ack_llrs);
    free_and_zero(pusch_vars->csi1_llrs);
    free_and_zero(pusch_vars->csi2_llrs);
  } // ULSCH_id
  free(gNB->pusch_vars);

  free_nrLDPC_coding_interface(&gNB->nrLDPC_coding_interface);
}

void nr_phy_config_request_sim(PHY_VARS_gNB *gNB,
                               int N_RB_DL,
                               int N_RB_UL,
                               int mu,
                               int Nid_cell,
                               uint64_t position_in_burst)
{
  NR_DL_FRAME_PARMS *fp                                   = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *gNB_config               = &gNB->gNB_config;

  // overwrite with new NR parameters
  uint64_t rev_burst=0;
  for (int i=0; i<64; i++)
    rev_burst |= (((position_in_burst>>(63-i))&0x01)<<i);

  gNB_config->cell_config.phy_cell_id.value             = Nid_cell;
  gNB_config->ssb_config.scs_common.value               = mu;
  gNB_config->ssb_table.ssb_subcarrier_offset.value     = 0;
  gNB_config->ssb_table.ssb_offset_point_a.value        = (N_RB_DL-20)>>1;
  gNB_config->ssb_table.ssb_mask_list[1].ssb_mask.value = (rev_burst)&(0xFFFFFFFF);
  gNB_config->ssb_table.ssb_mask_list[0].ssb_mask.value = (rev_burst>>32)&(0xFFFFFFFF);
  gNB_config->cell_config.frame_duplex_type.value       = TDD;
  gNB_config->ssb_table.ssb_period.value		= 1; //10ms
  gNB_config->carrier_config.dl_grid_size[mu].value     = N_RB_DL;
  gNB_config->carrier_config.ul_grid_size[mu].value     = N_RB_UL;
  gNB_config->carrier_config.num_tx_ant.value           = fp->nb_antennas_tx;
  gNB_config->carrier_config.num_rx_ant.value           = fp->nb_antennas_rx;

  int nr_band = 78;
  switch (mu) {
    case 0:
      gNB->gNB_config.tdd_table.tdd_period.value = 7;
      fp->dl_CarrierFreq = 2600000000;
      fp->ul_CarrierFreq = 2600000000;
      nr_band = 38;
      break;
    case 1:
      gNB->gNB_config.tdd_table.tdd_period.value = 6;
      fp->dl_CarrierFreq = 3600000000;
      fp->ul_CarrierFreq = 3600000000;
      nr_band = 78;
      break;
    case 3:
      gNB->gNB_config.tdd_table.tdd_period.value = 3;
      fp->dl_CarrierFreq = 27524520000;
      fp->ul_CarrierFreq = 27524520000;
      nr_band = 261;
      break;
    default:
      printf("unsupported numerology %d\n", mu);
      exit(-1);
  }

  frequency_range_t frequency_range = get_freq_range_from_band(nr_band);
  int bw_index = get_supported_band_index(mu, frequency_range, N_RB_DL);
  gNB_config->carrier_config.dl_bandwidth.value = get_supported_bw_mhz(frequency_range, bw_index);

  nr_init_frame_parms(gNB_config, fp);

  fp->ofdm_offset_divisor = UINT_MAX;
  init_symbol_rotation(fp);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_UL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);

  gNB->configured = 1;
}

void nr_phy_config_request(NR_PHY_Config_t *phy_config)
{
  uint8_t Mod_id = 0;
  uint8_t short_sequence, num_sequences, rootSequenceIndex, fd_occasion;
  NR_DL_FRAME_PARMS *fp = &RC.gNB[Mod_id]->frame_parms;
  nfapi_nr_config_request_scf_t *gNB_config = &RC.gNB[Mod_id]->gNB_config;

  copy_config_request(phy_config->cfg, gNB_config);

  uint64_t dl_bw_khz = (12*gNB_config->carrier_config.dl_grid_size[gNB_config->ssb_config.scs_common.value].value)*(15<<gNB_config->ssb_config.scs_common.value);
  fp->dl_CarrierFreq = ((dl_bw_khz>>1) + gNB_config->carrier_config.dl_frequency.value)*1000 ;
  
  uint64_t ul_bw_khz = (12*gNB_config->carrier_config.ul_grid_size[gNB_config->ssb_config.scs_common.value].value)*(15<<gNB_config->ssb_config.scs_common.value);
  fp->ul_CarrierFreq = ((ul_bw_khz>>1) + gNB_config->carrier_config.uplink_frequency.value)*1000 ;

  int32_t dlul_offset = fp->ul_CarrierFreq - fp->dl_CarrierFreq;

  LOG_I(PHY, "DL frequency %lu Hz, UL frequency %lu Hz: uldl offset %d Hz\n", fp->dl_CarrierFreq, fp->ul_CarrierFreq, dlul_offset);

  fp->threequarter_fs = get_softmodem_params()->threequarter_fs;
  LOG_D(PHY,"Configuring MIB for instance %d, : (Nid_cell %d,DL freq %llu, UL freq %llu)\n",
        Mod_id,
        gNB_config->cell_config.phy_cell_id.value,
        (unsigned long long)fp->dl_CarrierFreq,
        (unsigned long long)fp->ul_CarrierFreq);

  nr_init_frame_parms(gNB_config, fp);
  

  if (RC.gNB[Mod_id]->configured == 1) {
    LOG_E(PHY,"Already gNB already configured, do nothing\n");
    return;
  }

  fd_occasion = 0;
  nfapi_nr_prach_config_t *prach_config = &gNB_config->prach_config;
  short_sequence = prach_config->prach_sequence_length.value;
//  for(fd_occasion = 0; fd_occasion <= prach_config->num_prach_fd_occasions.value ; fd_occasion) { // TODO Need to handle for msg1-fdm > 1
  num_sequences = prach_config->num_prach_fd_occasions_list[fd_occasion].num_root_sequences.value;
  rootSequenceIndex = prach_config->num_prach_fd_occasions_list[fd_occasion].prach_root_sequence_index.value;

  compute_nr_prach_seq(short_sequence, num_sequences, rootSequenceIndex, RC.gNB[Mod_id]->X_u);
//  }
  RC.gNB[Mod_id]->configured     = 1;

  fp->ofdm_offset_divisor = RC.gNB[Mod_id]->ofdm_offset_divisor;
  init_symbol_rotation(fp);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_UL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);
}

static void init_DLSCH_struct(PHY_VARS_gNB *gNB)
{
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  uint16_t grid_size = cfg->carrier_config.dl_grid_size[fp->numerology_index].value;
  gNB->dlsch = calloc(gNB->max_nb_pdsch, sizeof(*gNB->dlsch));
  for (int i = 0; i < gNB->max_nb_pdsch; i++) {
    LOG_D(PHY, "Allocating Transport Channel Buffers for DLSCH %d/%d\n", i, gNB->max_nb_pdsch);
    gNB->dlsch[i] = new_gNB_dlsch(fp, grid_size);
  }
}

static void destroy_DLSCH_struct(const PHY_VARS_gNB *gNB)
{
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  const nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  const uint16_t grid_size = cfg->carrier_config.dl_grid_size[fp->numerology_index].value;
  for (int i = 0; i < gNB->max_nb_pdsch; i++) {
    free_gNB_dlsch(&gNB->dlsch[i], grid_size, fp);
  }
  free(gNB->dlsch);
}

void init_nr_transport(PHY_VARS_gNB *gNB)
{

  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  const nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;

  int nb_slots_per_period = cfg->cell_config.frame_duplex_type.value ?
                            fp->slots_per_frame / get_nb_periods_per_frame(cfg->tdd_table.tdd_period.value) :
                            fp->slots_per_frame;
  int nb_ul_slots_period = 0;
  if (cfg->cell_config.frame_duplex_type.value) {
    for(int i = 0; i < nb_slots_per_period; i++) {
      for(int j = 0; j < fp->symbols_per_slot; j++) {
        if(cfg->tdd_table.max_tdd_periodicity_list[i].max_num_of_symbol_per_slot_list[j].slot_config.value == 1) { // UL symbol
          nb_ul_slots_period++;
          break;
        }  
      }
    }
  }
  else
    nb_ul_slots_period = fp->slots_per_frame;

  int buffer_ul_slots; // the UL channels are scheduled sl_ahead before they are transmitted
  int slot_ahead = gNB->if_inst ? gNB->if_inst->sl_ahead : 6;
  if (slot_ahead > nb_slots_per_period)
    buffer_ul_slots = nb_ul_slots_period + (slot_ahead - nb_slots_per_period);
  else
    buffer_ul_slots = (nb_ul_slots_period < slot_ahead) ? nb_ul_slots_period : slot_ahead;

  gNB->max_nb_pusch = buffer_ul_slots ? NR_MAX_SCHED_PER_SLOT * buffer_ul_slots : 1;

  int max_nb_pucch = buffer_ul_slots ? MAX_MOBILES_PER_GNB * buffer_ul_slots : 1;
  bool ret;
  ret = spsc_q_alloc(&gNB->pucch_queue, max_nb_pucch, sizeof(NR_gNB_PUCCH_job_t));
  DevAssert(ret);
  ret = spsc_q_alloc(&gNB->pusch_queue, gNB->max_nb_pusch, sizeof(NR_gNB_PUSCH_job_t));
  DevAssert(ret);

  int max_nb_srs = buffer_ul_slots ? buffer_ul_slots << 1 : 1; // assuming at most 2 SRS per slot
  ret = spsc_q_alloc(&gNB->srs_queue, max_nb_srs, sizeof(NR_gNB_SRS_job_t));
  DevAssert(ret);

  gNB->ulsch = (NR_gNB_ULSCH_t *)malloc16(gNB->max_nb_pusch * sizeof(NR_gNB_ULSCH_t));
  for (int i = 0; i < gNB->max_nb_pusch; i++) {
    LOG_D(PHY, "Allocating Transport Channel Buffers for ULSCH %d/%d\n", i, gNB->max_nb_pusch);
    gNB->ulsch[i] = new_gNB_ulsch(gNB->max_ldpc_iterations, fp->N_RB_UL);
  }

  gNB->rx_total_gain_dB=130;

  //fp->pucch_config_common.deltaPUCCH_Shift = 1;
}

void reset_nr_transport(PHY_VARS_gNB *gNB)
{
  const NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;

  spsc_q_free(&gNB->pucch_queue);
  spsc_q_free(&gNB->pusch_queue);
  spsc_q_free(&gNB->srs_queue);

  for (int i = 0; i < gNB->max_nb_pusch; i++)
    free_gNB_ulsch(&gNB->ulsch[i], fp->N_RB_UL);
  free(gNB->ulsch);
}
