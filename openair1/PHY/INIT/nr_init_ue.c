/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_phy_init.h"
#include "openair1/PHY/defs_RU.h"
#include "openair1/PHY/impl_defs_nr.h"
#include "assertions.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_REFSIG/pss_nr.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include "PHY/NR_REFSIG/sl_refsig_defs.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_REFSIG/nr_mod_table.h"
#include "openair2/COMMON/prs_nr_paramdef.h"
#include "SCHED_NR_UE/harq_nr.h"
#include "nr-uesoftmodem.h"
#include "common/config/config_userapi.h"
#include "nr_phy_common.h"
#ifdef LDPC_CUDA
#include "PHY/gpu_compat.h"
#include "PHY/gpu_alloc.h"
#endif

void RCconfig_nrUE_prs(void *cfg)
{
  int j = 0, k = 0, gNB_id = 0;
  char aprefix[MAX_OPTNAME_SIZE*2 + 8];
  char str[7][100] = {{'\0'}}; int16_t n[7] = {0};
  PHY_VARS_NR_UE *ue  = (PHY_VARS_NR_UE *)cfg;
  prs_config_t *prs_config = NULL;

  paramlist_def_t gParamList = {CONFIG_STRING_PRS_LIST,NULL,0};
  paramdef_t gParams[] = PRS_GLOBAL_PARAMS_DESC;
  config_getlist(config_get_if(), &gParamList, gParams, sizeofArray(gParams), NULL);
  if (gParamList.numelt > 0)
  {
    ue->prs_active_gNBs = *(gParamList.paramarray[j][PRS_ACTIVE_GNBS_IDX].uptr);
  } else {
    LOG_I(PHY,"%s configuration NOT found..!! Skipped configuring UE for the PRS reception\n", CONFIG_STRING_PRS_CONFIG);
  }

  paramlist_def_t PRS_ParamList = {{0},NULL,0};
  for(int i = 0; i < ue->prs_active_gNBs; i++)
  {
    paramdef_t PRS_Params[] = PRS_PARAMS_DESC;
    sprintf(PRS_ParamList.listname, "%s%i", CONFIG_STRING_PRS_CONFIG, i);

    sprintf(aprefix, "%s.[%i]", CONFIG_STRING_PRS_LIST, 0);
    config_getlist(config_get_if(), &PRS_ParamList, PRS_Params, sizeofArray(PRS_Params), aprefix);

    if (PRS_ParamList.numelt > 0) {
      for (j = 0; j < PRS_ParamList.numelt; j++) {
        gNB_id = *(PRS_ParamList.paramarray[j][PRS_GNB_ID].uptr);
        if(gNB_id != i)  gNB_id = i; // force gNB_id to avoid mismatch

        memset(n,0,sizeof(n));
        ue->prs_vars[gNB_id]->NumPRSResources = *(PRS_ParamList.paramarray[j][NUM_PRS_RESOURCES].uptr);
        for (k = 0; k < ue->prs_vars[gNB_id]->NumPRSResources; k++)
        {
          prs_config = &ue->prs_vars[gNB_id]->prs_resource[k].prs_cfg;
          prs_config->PRSResourceSetPeriod[0]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[0];
          prs_config->PRSResourceSetPeriod[1]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[1];
          // per PRS resources parameters
          prs_config->SymbolStart              = PRS_ParamList.paramarray[j][PRS_SYMBOL_START_LIST].uptr[k];
          prs_config->NumPRSSymbols            = PRS_ParamList.paramarray[j][PRS_NUM_SYMBOLS_LIST].uptr[k];
          prs_config->REOffset                 = PRS_ParamList.paramarray[j][PRS_RE_OFFSET_LIST].uptr[k];
          prs_config->NPRSID                   = PRS_ParamList.paramarray[j][PRS_ID_LIST].uptr[k];
          prs_config->PRSResourceOffset        = PRS_ParamList.paramarray[j][PRS_RESOURCE_OFFSET_LIST].uptr[k];
          // Common parameters to all PRS resources
          prs_config->NumRB                    = *(PRS_ParamList.paramarray[j][PRS_NUM_RB].uptr);
          prs_config->RBOffset                 = *(PRS_ParamList.paramarray[j][PRS_RB_OFFSET].uptr);
          prs_config->CombSize                 = *(PRS_ParamList.paramarray[j][PRS_COMB_SIZE].uptr);
          prs_config->PRSResourceRepetition    = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_REPETITION].uptr);
          prs_config->PRSResourceTimeGap       = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_TIME_GAP].uptr);

          prs_config->MutingBitRepetition      = *(PRS_ParamList.paramarray[j][PRS_MUTING_BIT_REPETITION].uptr);
          for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].numelt; l++)
          {
            prs_config->MutingPattern1[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].uptr[l];
            if (k == 0) // print only for 0th resource
              n[5] += snprintf(str[5]+n[5],sizeof(str[5]),"%d, ",prs_config->MutingPattern1[l]);
          }
          for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].numelt; l++)
          {
            prs_config->MutingPattern2[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].uptr[l];
            if (k == 0) // print only for 0th resource
              n[6] += snprintf(str[6]+n[6],sizeof(str[6]),"%d, ",prs_config->MutingPattern2[l]);
          }

          // print to buffer
          n[0] += snprintf(str[0]+n[0],sizeof(str[0]),"%d, ",prs_config->SymbolStart);
          n[1] += snprintf(str[1]+n[1],sizeof(str[1]),"%d, ",prs_config->NumPRSSymbols);
          n[2] += snprintf(str[2]+n[2],sizeof(str[2]),"%d, ",prs_config->REOffset);
          n[3] += snprintf(str[3]+n[3],sizeof(str[3]),"%d, ",prs_config->PRSResourceOffset);
          n[4] += snprintf(str[4]+n[4],sizeof(str[4]),"%d, ",prs_config->NPRSID);
        } // for k

        prs_config = &ue->prs_vars[gNB_id]->prs_resource[0].prs_cfg;
        LOG_I(PHY, "-----------------------------------------\n");
        LOG_I(PHY, "PRS Config for gNB_id %d @ %p\n", gNB_id, prs_config);
        LOG_I(PHY, "-----------------------------------------\n");
        LOG_I(PHY, "NumPRSResources \t%d\n", ue->prs_vars[gNB_id]->NumPRSResources);
        LOG_I(PHY, "PRSResourceSetPeriod \t[%d, %d]\n", prs_config->PRSResourceSetPeriod[0], prs_config->PRSResourceSetPeriod[1]);
        LOG_I(PHY, "NumRB \t\t\t%d\n", prs_config->NumRB);
        LOG_I(PHY, "RBOffset \t\t%d\n", prs_config->RBOffset);
        LOG_I(PHY, "CombSize \t\t%d\n", prs_config->CombSize);
        LOG_I(PHY, "PRSResourceRepetition \t%d\n", prs_config->PRSResourceRepetition);
        LOG_I(PHY, "PRSResourceTimeGap \t%d\n", prs_config->PRSResourceTimeGap);
        LOG_I(PHY, "MutingBitRepetition \t%d\n", prs_config->MutingBitRepetition);
        LOG_I(PHY, "SymbolStart \t\t[%s\b\b]\n", str[0]);
        LOG_I(PHY, "NumPRSSymbols \t\t[%s\b\b]\n", str[1]);
        LOG_I(PHY, "REOffset \t\t[%s\b\b]\n", str[2]);
        LOG_I(PHY, "PRSResourceOffset \t[%s\b\b]\n", str[3]);
        LOG_I(PHY, "NPRS_ID \t\t[%s\b\b]\n", str[4]);
        LOG_I(PHY, "MutingPattern1 \t\t[%s\b\b]\n", str[5]);
        LOG_I(PHY, "MutingPattern2 \t\t[%s\b\b]\n", str[6]);
        LOG_I(PHY, "-----------------------------------------\n");
      }
    } else {
      LOG_I(PHY,"No %s configuration found..!!\n", PRS_ParamList.listname);
    }
  }
}

void init_nr_prs_ue_vars(PHY_VARS_NR_UE *ue)
{
  NR_UE_PRS   **const prs_vars = ue->prs_vars;
  NR_DL_FRAME_PARMS *const fp  = &ue->frame_parms;

  // PRS vars init
  for(int idx = 0; idx < NR_MAX_PRS_COMB_SIZE; idx++)
  {
    prs_vars[idx] = malloc16_clear(sizeof(NR_UE_PRS));
    // PRS channel estimates

    for(int k = 0; k < NR_MAX_PRS_RESOURCES_PER_SET; k++)
    {
      prs_vars[idx]->prs_resource[k].prs_meas = malloc16_clear(fp->nb_antennas_rx * sizeof(prs_meas_t *));
      AssertFatal((prs_vars[idx]->prs_resource[k].prs_meas!=NULL), "%s: PRS measurements malloc failed for gNB_id %d\n", __FUNCTION__, idx);

      for (int j=0; j<fp->nb_antennas_rx; j++) {
        prs_vars[idx]->prs_resource[k].prs_meas[j] = malloc16_clear(sizeof(prs_meas_t));
        AssertFatal((prs_vars[idx]->prs_resource[k].prs_meas[j]!=NULL), "%s: PRS measurements malloc failed for gNB_id %d, rx_ant %d\n", __FUNCTION__, idx, j);
        prs_meas_t *m = prs_vars[idx]->prs_resource[k].prs_meas[j];
        m->next_dl_toa = m->dl_toa;
        pthread_mutex_init(&m->dl_toa_mtx, NULL);
      }
    }
  }

  // load the config file params
  RCconfig_nrUE_prs(ue);
}

int init_nr_ue_signal(PHY_VARS_NR_UE *ue, int nb_connected_gNB)
{
  // create shortcuts
  NR_DL_FRAME_PARMS *const fp            = &ue->frame_parms;
  NR_UE_COMMON *const common_vars        = &ue->common_vars;
  NR_UE_PRACH **const prach_vars         = ue->prach_vars;

  LOG_I(PHY, "Initializing UE vars for gNB TXant %u, UE RXant %u\n", fp->nb_antennas_tx, fp->nb_antennas_rx);

  phy_init_nr_top(ue);
  // many memory allocation sizes are hard coded
  AssertFatal( fp->nb_antennas_rx <= 4, "hard coded allocation for ue_common_vars->dl_ch_estimates[gNB_id]" );
  AssertFatal( nb_connected_gNB <= NUMBER_OF_CONNECTED_gNB_MAX, "n_connected_gNB is too large" );
  // init phy_vars_ue

  for (int i = 0; i < fp->Lmax; i++) {
    ue->measurements.ssb_rsrp_dBm[i] = INT_MIN;
    ue->measurements.ssb_sinr_dB[i] = INT_MIN;
  }

  for (int i = 0; i < 4; i++) {
    ue->rx_gain_max[i] = 135;
    ue->rx_gain_med[i] = 128;
    ue->rx_gain_byp[i] = 120;
  }

  ue->n_connected_gNB = nb_connected_gNB;

  for (int gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++) {
    ue->total_TBS[gNB_id] = 0;
    ue->total_TBS_last[gNB_id] = 0;
    ue->bitrate[gNB_id] = 0;
    ue->total_received_bits[gNB_id] = 0;
  }
  // init NR modulation lookup tables
  nr_generate_modulation_table();

  ///////////
  ////////////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////// PRS init /////////////////////////
  ///////////

  init_nr_prs_ue_vars(ue);

  ///////////
  ////////////////////////////////////////////////////////////////////////////////////////////

  /////////////////////////PUSCH DMRS init/////////////////////////
  ///////////

  ///////////
  ////////////////////////////////////////////////////////////////////////////////////////////

  /////////////////////////PUSCH PTRS init/////////////////////////
  ///////////

  //------------- config PTRS parameters--------------//
  // ptrs_Uplink_Config->timeDensity.ptrs_mcs1 = 2; // setting MCS values to 0 indicate abscence of time_density field in the configuration
  // ptrs_Uplink_Config->timeDensity.ptrs_mcs2 = 4;
  // ptrs_Uplink_Config->timeDensity.ptrs_mcs3 = 10;
  // ptrs_Uplink_Config->frequencyDensity.n_rb0 = 25;     // setting N_RB values to 0 indicate abscence of frequency_density field in the configuration
  // ptrs_Uplink_Config->frequencyDensity.n_rb1 = 75;
  // ptrs_Uplink_Config->resourceElementOffset = 0;
  //-------------------------------------------------//

  ///////////
  ////////////////////////////////////////////////////////////////////////////////////////////

  for (int i = 0; i < 10; i++)
    ue->tx_power_dBm[i]=-127;

  // init TX buffers
  common_vars->txData = malloc16(fp->nb_antennas_tx * sizeof(c16_t *));

  for (int i = 0; i < fp->nb_antennas_tx; i++) {
    common_vars->txData[i] = malloc16_clear((fp->samples_per_frame) * sizeof(c16_t));
  }

  // init RX buffers
  common_vars->rxdata = malloc16(fp->nb_antennas_rx * sizeof(c16_t *));

  int num_samples = 2 * fp->samples_per_frame + fp->ofdm_symbol_size;
  if (ue->sl_mode == 2)
    num_samples = (SL_NR_PSBCH_REPETITION_IN_FRAMES * fp->samples_per_frame) + fp->ofdm_symbol_size;

  for (int i = 0; i < fp->nb_antennas_rx; i++) {
    common_vars->rxdata[i] = malloc16_clear(num_samples * sizeof(c16_t));
  }

  // DLSCH
  for (int gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++) {
    prach_vars[gNB_id] = malloc16_clear(sizeof(NR_UE_PRACH));

    // ceil((NB_RB*8(max allocation per RB)*2(QPSK))/32)
    ue->nr_csi_info = malloc16_clear(sizeof(nr_csi_info_t));
    ue->nr_csi_info->csi_rs_generated_signal = malloc16(NR_MAX_CSI_PORTS * sizeof(*ue->nr_csi_info->csi_rs_generated_signal));
    for (int i = 0; i < NR_MAX_CSI_PORTS; i++) {
      ue->nr_csi_info->csi_rs_generated_signal[i] =
          malloc16_clear(fp->samples_per_slot_wCP * sizeof(**ue->nr_csi_info->csi_rs_generated_signal));
    }
  }

  ue->init_averaging = 1;
  init_symbol_rotation(fp);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_DL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);

  // initialize to false only for SA since in do-ra and phy-test it is already set to true before getting here
  if (IS_SA_MODE(get_softmodem_params()))
    ue->received_config_request = false;

  return 0;
}

static void sl_ue_free(PHY_VARS_NR_UE *UE)
{
  if (UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation) {
    free_and_zero(UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation[0]);
    free_and_zero(UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation[1]);
    free_and_zero(UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation);
  }
}

void term_nr_ue_signal(PHY_VARS_NR_UE *ue)
{
  const NR_DL_FRAME_PARMS* fp = &ue->frame_parms;
  phy_term_nr_top();

  NR_UE_COMMON* common_vars = &ue->common_vars;

  for (int i = 0; i < fp->nb_antennas_tx; i++) {
    free_and_zero(common_vars->txData[i]);
  }

  free_and_zero(common_vars->txData);

  for (int i = 0; i < fp->nb_antennas_rx; i++) {
    free_and_zero(common_vars->rxdata[i]);
  }
  free_and_zero(common_vars->rxdata);

  for (int gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++) {

    for (int i = 0; i < NR_MAX_CSI_PORTS; i++) {
      free_and_zero(ue->nr_csi_info->csi_rs_generated_signal[i]);
    }
    free_and_zero(ue->nr_csi_info->csi_rs_generated_signal);
    free_and_zero(ue->nr_csi_info);

    free_and_zero(ue->prach_vars[gNB_id]);
  }

  for(int idx = 0; idx < NR_MAX_PRS_COMB_SIZE; idx++)
  {
    for(int k = 0; k < NR_MAX_PRS_RESOURCES_PER_SET; k++)
    {
      for (int j=0; j<fp->nb_antennas_rx; j++)
      {
        free_and_zero(ue->prs_vars[idx]->prs_resource[k].prs_meas[j]);
      }
      free_and_zero(ue->prs_vars[idx]->prs_resource[k].prs_meas);
    }

    free_and_zero(ue->prs_vars[idx]);
  }

  sl_ue_free(ue);
}

void free_nr_ue_dl_harq(NR_DL_UE_HARQ_t harq_list[2][NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb)
{
  uint16_t a_segments = MAX_NUM_NR_DLSCH_SEGMENTS;
  if (num_rb != 273) {
    a_segments = a_segments*num_rb;
    a_segments = (a_segments/273)+1;
  }

  for (int j=0; j < 2; j++) {
    for (int i = 0; i < number_of_processes; i++) {
#ifdef LDPC_CUDA
      gpuFreeHost(harq_list[j][i].c);
#else
      free_and_zero(harq_list[j][i].c);
#endif
      free_and_zero(harq_list[j][i].d);
    }
  }
}

void free_nr_ue_ul_harq(NR_UL_UE_HARQ_t harq_list[NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb, int num_ant_tx)
{
  int max_layers = min(num_ant_tx, NR_MAX_NB_LAYERS);
  uint16_t a_segments = MAX_NUM_NR_ULSCH_SEGMENTS_PER_LAYER*max_layers;  //number of segments to be allocated

  if (num_rb != 273) {
    a_segments = a_segments*num_rb;
    a_segments = a_segments/273 +1;
  }

  for (int i = 0; i < number_of_processes; i++) {
    free_and_zero(harq_list[i].payload_AB);
#ifdef LDPC_CUDA
    {
      // nr_init_ul_harq_processes() makes single allocation(!)
      gpuFreeHost(harq_list[i].c[0]);
    }
#else
    for (int r = 0; r < a_segments; r++) {
      free_and_zero(harq_list[i].c[r]);
    }
    free_and_zero(harq_list[i].c);
#endif
    free_and_zero(harq_list[i].f);
  }
}

void free_nr_ue_pdsch_buffers(pdsch_scratch_t *buffers, int num_actors)
{
  for (int i = 0; i < num_actors; i++) {
    free_and_zero(buffers[i].rxdataF_comp);
    free_and_zero(buffers[i].dl_ch_mag);
    free_and_zero(buffers[i].dl_ch_magb);
    free_and_zero(buffers[i].dl_ch_magr);
    free_and_zero(buffers[i].rho_dl);
    free_and_zero(buffers[i].pdsch_dl_ch_estimates);
    for (int c = 0; c < 2; c++) {
#ifdef LDPC_CUDA
      // llr_dev[c] is the device alias of llr[c], not a separate allocation
      gpuFreeHost(buffers[i].llr[c]);
#else
      free_and_zero(buffers[i].llr[c]);
#endif
    }
  }
}

void term_nr_ue_transport(PHY_VARS_NR_UE *ue)
{
  const int N_RB_DL = ue->frame_parms.N_RB_DL;
  const int N_RB_UL = ue->frame_parms.N_RB_UL;
  free_nr_ue_dl_harq(ue->dl_harq_processes, NR_MAX_HARQ_PROCESSES, N_RB_DL);
  free_nr_ue_ul_harq(ue->ul_harq_processes, NR_MAX_HARQ_PROCESSES, N_RB_UL, ue->frame_parms.nb_antennas_tx);
  free_nr_ue_pdsch_buffers(ue->pdsch_scratch, ue->pdsch_num_actors);
  free_and_zero(ue->pdsch_scratch);
}

void nr_init_dl_harq_processes(NR_DL_UE_HARQ_t harq_list[2][NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb)
{
  int a_segments = MAX_NUM_NR_DLSCH_SEGMENTS; // number of segments to be allocated
  if (num_rb != 273) {
    a_segments = a_segments*num_rb;
    a_segments = (a_segments/273)+1;
  }

  for (int j=0; j<2; j++) {
    for (int i=0; i<number_of_processes; i++) {
      memset(harq_list[j] + i, 0, sizeof(NR_DL_UE_HARQ_t));
      init_downlink_harq_status(harq_list[j] + i);

#ifdef LDPC_CUDA
      harq_list[j][i].c = gpuHostAlloc_or_fail(a_segments * sizeof(uint8_t *) * 1056, gpuHostAllocMapped);
      harq_list[j][i].cdev = gpuHostGetDevicePointer_or_fail(harq_list[j][i].c);
#else
      harq_list[j][i].c = malloc16(a_segments * sizeof(*harq_list[j][i].c) * 1056);
#endif
      harq_list[j][i].d = malloc16(a_segments * sizeof(*harq_list[j][i].d) * 3 * 8448);
      init_abort(&harq_list[j][i].abort_decode);
      harq_list[j][i].status  = 0;
      harq_list[j][i].DLround = 0;
    }
  }
}

void nr_init_ul_harq_processes(NR_UL_UE_HARQ_t harq_list[NR_MAX_HARQ_PROCESSES], int number_of_processes, int num_rb, int num_ant_tx)
{
  int max_layers = min(num_ant_tx, NR_MAX_NB_LAYERS);
  uint16_t a_segments = MAX_NUM_NR_ULSCH_SEGMENTS_PER_LAYER*max_layers;  //number of segments to be allocated

  if (num_rb != 273) {
    a_segments = a_segments * num_rb;
    a_segments = a_segments / 273 + 1;
  }

  uint32_t ulsch_bytes = a_segments * 1056;

  for (int i = 0; i < number_of_processes; i++) {
    memset(&harq_list[i], 0, sizeof(NR_UL_UE_HARQ_t));

    harq_list[i].payload_AB = malloc16_clear(ulsch_bytes);

#ifdef LDPC_CUDA
    size_t total_c_size = a_segments * 8448;

    uint8_t *tmp_c = gpuHostAlloc_or_fail(total_c_size, gpuHostAllocMapped);
    memset(tmp_c, 0, total_c_size);

    harq_list[i].c = gpuHostAlloc_or_fail(a_segments * sizeof(uint8_t *), gpuHostAllocMapped);

    for (int r = 0; r < a_segments; r++) {
      harq_list[i].c[r] = tmp_c + (r * 8448);
    }

#else
    harq_list[i].c = malloc16(a_segments * sizeof(uint8_t *));

    for (int r = 0; r < a_segments; r++) {
      harq_list[i].c[r] = malloc16_clear(8448);
    }
#endif

    uint32_t f_size = 14 * num_rb * 12 * 16;
    harq_list[i].f = malloc16_clear(f_size);
    harq_list[i].round = 0;
  }
}

void nr_init_pdsch_buffers(pdsch_scratch_t *buffers, int num_actors, const NR_DL_FRAME_PARMS *fp, bool do_ml)
{
  const uint32_t pdsch_buf_size_max = (fp->N_RB_DL * NR_NB_SC_PER_RB + 15) & ~15;
  const uint32_t pdsch_est_size = fp->symbols_per_slot * fp->ofdm_symbol_size;

  /* Cap layer dimension at nb_antennas_rx instead of NR_MAX_NB_LAYERS to
   * right-size all per-actor scratch buffers, reducing the PDSCH working
   * set and easing L2 cache pressure. */
  const int max_layers = min(fp->nb_antennas_rx, NR_MAX_NB_LAYERS);
  const uint32_t llr_buf_max = NR_NB_SC_PER_RB * NR_SYMBOLS_PER_SLOT * fp->N_RB_DL * 8 * max_layers;
  /* Buffers are now 1-symbol-wide: channel compensation writes into them and
   * LLR+demapping consumes them within the same nr_rx_pdsch() call, so there
   * is no need to keep slot-wide storage. */
  const size_t comp_elems = (size_t)max_layers * pdsch_buf_size_max;
  const size_t ch_est_elems = (size_t)fp->nb_antennas_rx * max_layers * pdsch_est_size;

  /* rho_dl is only needed for 2-layer ML LLR computation; skip the
   * allocation entirely when ML is disabled to reduce memory footprint. */
  const size_t rho_elems = do_ml ? (size_t)max_layers * max_layers * pdsch_buf_size_max : 0;

  for (int i = 0; i < num_actors; i++) {
    buffers[i].pdsch_buf_size_max           = pdsch_buf_size_max;
    buffers[i].pdsch_est_size        = pdsch_est_size;
    buffers[i].llr_buf_max           = llr_buf_max;
    buffers[i].rxdataF_comp          = malloc16_clear(comp_elems   * sizeof(c16_t));
    buffers[i].dl_ch_mag             = malloc16_clear(comp_elems   * sizeof(c16_t));
    buffers[i].dl_ch_magb            = malloc16_clear(comp_elems   * sizeof(c16_t));
    buffers[i].dl_ch_magr            = malloc16_clear(comp_elems   * sizeof(c16_t));
    buffers[i].rho_dl                = rho_elems ? malloc16_clear(rho_elems * sizeof(c16_t)) : NULL;
    buffers[i].pdsch_dl_ch_estimates = malloc16_clear(ch_est_elems * sizeof(int32_t));
    /* Per TS 38.211 Table 7.3.1.3-1, 1 codeword is used for 1-4 layers.
     * NR_MAX_NB_LAYERS == 4, so llr[1] is never needed. */
    for (int c = 0; c < 1; c++) {
#ifdef LDPC_CUDA
      buffers[i].llr[c] = gpuHostAlloc_or_fail((66 * 3 * 8448) * sizeof(int16_t), gpuHostAllocMapped);
      buffers[i].llr_dev[c] = gpuHostGetDevicePointer_or_fail(buffers[i].llr[c]);
#else
      buffers[i].llr[c]              = malloc16(llr_buf_max * sizeof(int16_t));
#endif
    }
    buffers[i].llr[1] = NULL;
  }
}

void init_nr_ue_transport(PHY_VARS_NR_UE *ue)
{
  nr_init_dl_harq_processes(ue->dl_harq_processes, NR_MAX_HARQ_PROCESSES, ue->frame_parms.N_RB_DL);
  nr_init_ul_harq_processes(ue->ul_harq_processes, NR_MAX_HARQ_PROCESSES, ue->frame_parms.N_RB_UL, ue->frame_parms.nb_antennas_tx);
  const int num_actors = get_nrUE_params()->num_dl_actors > 0 ? get_nrUE_params()->num_dl_actors : 1;
  ue->pdsch_num_actors = num_actors;
  ue->pdsch_scratch = calloc_or_fail(num_actors, sizeof(*ue->pdsch_scratch));
  nr_init_pdsch_buffers(ue->pdsch_scratch, num_actors, &ue->frame_parms, ue->do_ml);
}

void init_phy_nr_measurements(PHY_VARS_NR_UE *ue)
{
  PHY_NR_MEASUREMENTS *measurements = &ue->measurements;
  measurements->meas_request_pending = false;
  measurements->search_new_cells_pending = false;
  measurements->last_blind_slot = -1;
  measurements->last_slot = -1;
}

void clean_UE_harq(PHY_VARS_NR_UE *UE)
{
  for (int harq_pid = 0; harq_pid < NR_MAX_HARQ_PROCESSES; harq_pid++) {
    for (int i = 0; i < 2; i++) {
      NR_DL_UE_HARQ_t *dl_harq_process = &UE->dl_harq_processes[i][harq_pid];
      init_downlink_harq_status(dl_harq_process);
    }
  }
  for (int harq_pid = 0; harq_pid < NR_MAX_HARQ_PROCESSES; harq_pid++) {
    NR_UL_UE_HARQ_t *ul_harq_process = &UE->ul_harq_processes[harq_pid];
    ul_harq_process->round = 0;
  }
}

void phy_init_nr_top(PHY_VARS_NR_UE *ue) {
  NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  init_delay_table(frame_parms->ofdm_symbol_size, MAX_DELAY_COMP, NR_MAX_OFDM_SYMBOL_SIZE, frame_parms->delay_table);
  crcTableInit();
  init_byte2m128i();
  load_dftslib();
  generate_ul_reference_signal_sequences(SHRT_MAX);
}

void phy_term_nr_top(void)
{
  free_ul_reference_signal_sequences();
}

static void sl_generate_psbch_dmrs_qpsk_sequences(PHY_VARS_NR_UE *UE, struct complex16 *modulated_dmrs_sym, uint16_t slss_id)
{
  uint8_t idx = 0;
  uint32_t *sl_dmrs_sequence = UE->SL_UE_PHY_PARAMS.init_params.psbch_dmrs_gold_sequences[slss_id];
  c16_t *mod_table = (c16_t *)nr_qpsk_mod_table;

#ifdef SL_DEBUG_INIT
  printf("SIDELINK INIT: PSBCH DMRS Generation with slss_id:%d\n", slss_id);
#endif

  /// QPSK modulation
  for (int m = 0; m < SL_NR_NUM_PSBCH_DMRS_RE; m++) {
    idx = (((sl_dmrs_sequence[(m << 1) >> 5]) >> ((m << 1) & 0x1f)) & 3);
    modulated_dmrs_sym[m].r = mod_table[idx].r;
    modulated_dmrs_sym[m].i = mod_table[idx].i;
  }

#ifdef SL_DUMP_INIT_SAMPLES
  char filename[40], varname[25];
  sprintf(filename, "sl_psbch_dmrs_slssid_%d.m", slss_id);
  sprintf(varname, "sl_dmrs_id_%d.m", slss_id);
  LOG_M(filename, varname, (void *)modulated_dmrs_sym, SL_NR_NUM_PSBCH_DMRS_RE, 1, 1);
#endif
}

void sl_ue_phy_init(PHY_VARS_NR_UE *UE)
{
  uint16_t scaling_value = ONE_OVER_SQRT2_Q15;

  NR_DL_FRAME_PARMS *sl_fp = &UE->SL_UE_PHY_PARAMS.sl_frame_params;

  if (!UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation) {
    UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation = malloc16_clear(SL_NR_NUM_IDs_IN_PSS * sizeof(int32_t *));
    UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation[0] = malloc16_clear(sizeof(int32_t) * sl_fp->ofdm_symbol_size);
    UE->SL_UE_PHY_PARAMS.init_params.sl_pss_for_correlation[1] = malloc16_clear(sizeof(int32_t) * sl_fp->ofdm_symbol_size);
  }
  LOG_I(PHY, "SIDELINK INIT: GENERATE PSS, SSS, GOLD SEQUENCES AND PSBCH DMRS SEQUENCES FOR ALL possible SLSS IDs 0- 671\n");

  // Generate PSS sequences for IDs 0,1 used in PSS
  sl_generate_pss(&UE->SL_UE_PHY_PARAMS.init_params, 0, scaling_value);
  sl_generate_pss(&UE->SL_UE_PHY_PARAMS.init_params, 1, scaling_value);

  // Generate psbch dmrs Gold Sequences and modulated dmrs symbols
  sl_init_psbch_dmrs_gold_sequences(UE);
  for (int slss_id = 0; slss_id < SL_NR_NUM_SLSS_IDs; slss_id++) {
    sl_generate_psbch_dmrs_qpsk_sequences(UE, UE->SL_UE_PHY_PARAMS.init_params.psbch_dmrs_modsym[slss_id], slss_id);
    sl_generate_sss(&UE->SL_UE_PHY_PARAMS.init_params, slss_id, scaling_value);
  }

  // Generate PSS time domain samples used for correlation during SLSS reception.
  sl_generate_pss_ifft_samples(&UE->SL_UE_PHY_PARAMS, &UE->SL_UE_PHY_PARAMS.init_params);

}
