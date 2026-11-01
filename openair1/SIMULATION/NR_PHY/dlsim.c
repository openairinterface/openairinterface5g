/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/utils/assertions.h"
#include "common/utils/nr/nr_common.h"
#include "executables/nr-uesoftmodem.h"
#include "executables/softmodem-common.h"
#include "LAYER2/NR_MAC_UE/mac_defs.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "LAYER2/NR_MAC_gNB/mac_rrc_dl_handler.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "LAYER2/NR_MAC_gNB/nr_radio_config.h"
#include "NR_BCCH-BCH-Message.h"
#include "NR_BWP-Downlink.h"
#include "NR_CellGroupConfig.h"
#include "NR_ReconfigurationWithSync.h"
#include "NR_ServingCellConfig.h"
#include "NR_SetupRelease.h"
#include "NR_UE_PHY_INTERFACE/NR_IF_Module.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_TRANSPORT/nr_dlsch.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/MODULATION/phy_ofdm_mod.h"
#include "PHY/TOOLS/tools_defs.h"
#include "PHY/defs_RU.h"
#include "PHY/defs_gNB.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/defs_nr_common.h"
#include "PHY/impl_defs_nr.h"
#include "PHY/phy_vars_nr_ue.h"
#include "SCHED_NR/sched_nr.h"
#include "SCHED_NR_UE/defs.h"
#include "SCHED_NR_UE/fapi_nr_ue_l1.h"
#include "T.h"
#include "asn_internal.h"
#include "assertions.h"
#include "common/config/config_load_configmodule.h"
#include "common/ngran_types.h"
#include "common/ran_context.h"
#include "common/utils/T/T.h"
#include "common/utils/nr/nr_common.h"
#include "e1ap_messages_types.h"
#include "fapi_nr_ue_interface.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "nr_ue_phy_meas.h"
#include "oai_asn1.h"
#include "openair1/SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "thread-pool.h"
#include "time_meas.h"
#include "utils.h"
#define inMicroS(a) (((double)(a))/(1000.0))
#include "SIMULATION/LTE_PHY/common_sim.h"

#ifdef CHANNEL_SIM_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#include "SIMULATION/TOOLS/oai_cuda.h"
#endif

const char *__asan_default_options()
{
  /* don't do leak checking in nr_ulsim, not finished yet */
  return "detect_leaks=0";
}

PHY_VARS_gNB *gNB;
PHY_VARS_NR_UE *UE;
RAN_CONTEXT_t RC;
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
char *uecap_file;

//uint8_t nfapi_mode = 0;
uint64_t downlink_frequency[MAX_NUM_CCs][4];
THREAD_STRUCT thread_struct;
nfapi_ue_release_request_body_t release_rntis;

// NTN cellSpecificKoffset-r17, but in slots for DL SCS
unsigned int NTN_UE_Koffset = 0;

void nr_derive_key_ng_ran_star(uint16_t pci, uint64_t nr_arfcn_dl, const uint8_t key[32], uint8_t *key_ng_ran_star)
{
}

/* this is a hack, but necessary for E2 agent. We compile in all of RRC
 * (because of CMakeLists.txt), but we don't need it (only nr_radio_config.c).
 * however, if E2 agent is defined, the following functions are used in
 * rrc_gNB.c, but defined in RAN functions. In order to avoid pulling this in
 * here as well, only provide a prototype (and abort if they are ever called). */
void signal_rrc_msg(void /*const nr_rrc_class_e nr_channel, const uint32_t rrc_msg_id, const byte_array_t rrc_ba*/ ) { abort(); }
void signal_rrc_state_changed_to(void /* const gNB_RRC_UE_t *rrc_ue_context, const rrc_state_e2sm_rc_e rrc_state */) { abort(); }
void signal_ue_id(void /* const gNB_RRC_UE_t *rrc_ue_context, const uint16_t class, const uint32_t msg_id */) { abort(); }

/* Function to set or overwrite PTRS DL RRC parameters */
static void rrc_config_dl_ptrs_params(NR_BWP_Downlink_t *bwp, long *ptrsNrb, long *ptrsMcs, long *epre_Ratio, long *reOffset)
{
  int i=0;
  NR_DMRS_DownlinkConfig_t *tmp = bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup;
  // struct NR_SetupRelease_PTRS_DownlinkConfig *tmp=bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->phaseTrackingRS;
  /* check for memory allocation  */
  if (tmp->phaseTrackingRS == NULL) {
    asn1cCalloc(tmp->phaseTrackingRS, TrackingRS);
    TrackingRS->present = NR_SetupRelease_PTRS_DownlinkConfig_PR_setup;
    asn1cCalloc(TrackingRS->choice.setup, setup);
    asn1cCalloc(setup->frequencyDensity, freqD);
    /* Fill the given values */
    for(i = 0; i < 2; i++) {
      asn1cSequenceAdd(freqD->list, long, nbr);
      *nbr = ptrsNrb[i];
    }
    asn1cCalloc(setup->timeDensity, timeD);
    for(i = 0; i < 3; i++) {
      asn1cSequenceAdd(timeD->list, long, mcs);
      *mcs = ptrsMcs[i];
    }
    asn1cCallocOne(setup->epre_Ratio, epre_Ratio[0]);
    asn1cCallocOne(setup->resourceElementOffset, reOffset[0]);
  } else {
    NR_PTRS_DownlinkConfig_t *TrackingRS = tmp->phaseTrackingRS->choice.setup;
    for(i = 0; i < 2; i++) {
      *TrackingRS->frequencyDensity->list.array[i] = ptrsNrb[i];
    }
    for(i = 0; i < 3; i++) {
      *TrackingRS->timeDensity->list.array[i] = ptrsMcs[i];
    }
    *TrackingRS->epre_Ratio = epre_Ratio[0];
    *TrackingRS->resourceElementOffset = reOffset[0];
  }
}

int dummy_nr_ue_ul_indication(nr_uplink_indication_t *ul_info) { return(0);  }

void e1_bearer_context_setup(const e1ap_bearer_setup_req_t *req) { abort(); }
void e1_bearer_context_modif(const e1ap_bearer_mod_req_t *req) { abort(); }
void e1_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *conf) { abort(); }
void e1_bearer_release_cmd(const e1ap_bearer_release_cmd_t *cmd) { abort(); }

int8_t nr_rrc_RA_succeeded(const module_id_t mod_id, const uint8_t gNB_index) {
  return 0;
}

void nr_derive_key(int alg_type, uint8_t alg_id, const uint8_t key[32], uint8_t out[16])
{
  (void)alg_type;
}

void processSlotTX(void *arg) {}

// needed for some functions
openair0_config_t openair0_cfg_g[MAX_CARDS] = {};
void update_ptrs_config(NR_BWP_Downlink_t *bwp, int *rbSize, uint8_t *mcsIndex,int8_t *ptrs_arg);
void update_dmrs_config(NR_BWP_Downlink_t *bwp, int8_t *dmrs_arg);

/* specific dlsim DL preprocessor: uses rbStart/rbSize/mcs/nrOfLayers from command line of dlsim */
int g_mcsIndex = 9, g_mcsTableIdx = 0, g_rbStart = 0, g_rbSize = 0, g_nrOfLayers = 1, g_pmi = 0, g_nb_rb_ranges = 0, N_RB_DL = 106;
int g_rnti = 0x1234;
/* PRBs used by retransmissions (0: same as initial), keeping the initial TBS through a reserved MCS index */
int g_retx_rbSize = 0;
nr_pdsch_allocation_type_t alloc_type = PDSCH_TYPE1;
#define MAX_RB_RANGES 16
typedef struct {
  int start;
  int size;
} rb_range_t;
rb_range_t g_rb_ranges[MAX_RB_RANGES];

void nr_dlsim_preprocessor(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pdsch_t *pp_pdsch)
{
  DevAssert(get_num_connected_ue(&nr_mac->UE_info) == 1);
  NR_UE_info_t *UE_info = find_connected_nr_ue(&nr_mac->UE_info, g_rnti);
  DevAssert(UE_info && UE_info->rnti == g_rnti);

  NR_UE_sched_ctrl_t *sched_ctrl = &UE_info->UE_sched_ctrl;
  NR_UE_DL_BWP_t *current_BWP = &UE_info->current_DL_BWP;
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;

  int nr_of_candidates = 0;
  if (g_mcsIndex < 4) {
    find_aggregation_candidates(&sched_ctrl->aggregation_level, &nr_of_candidates, sched_ctrl->search_space, 8);
  }
  if (nr_of_candidates == 0) {
    find_aggregation_candidates(&sched_ctrl->aggregation_level, &nr_of_candidates, sched_ctrl->search_space, 4);
  }
  uint32_t Y = get_Y(sched_ctrl->search_space, pp_pdsch->slot, UE_info->rnti);
  int CCEIndex = find_pdcch_candidate(cell,
                                      sched_ctrl->aggregation_level,
                                      nr_of_candidates,
                                      0,
                                      &sched_ctrl->sched_pdcch,
                                      sched_ctrl->coreset,
                                      Y);
  AssertFatal(CCEIndex >= 0,
              "%4d.%2d could not find CCE for DL DCI UE %d/RNTI %04x\n",
              pp_pdsch->frame,
              pp_pdsch->slot,
              0,
              UE_info->rnti);
  sched_ctrl->cce_index = CCEIndex;

  NR_sched_pdsch_t sched_pdsch = {
      .rbStart = g_rbStart,
      .rbSize = g_rbSize,
      .alloc_type = alloc_type,
      .bwp_info = get_pdsch_bwp_start_size(cell, UE_info),
      .mcs = g_mcsIndex,
      .nrOfLayers = g_nrOfLayers,
      .pm_index = g_pmi,
  };

  if (alloc_type == PDSCH_TYPE0) {
    sched_pdsch.rbSize = 0;
    memset(sched_pdsch.rbBitmap, 0, sizeof(sched_pdsch.rbBitmap));
    for (int r = 0; r < g_nb_rb_ranges; r++) {
      for (int rb = g_rb_ranges[r].start; rb < g_rb_ranges[r].start + g_rb_ranges[r].size; rb++) {
        AssertFatal(rb < N_RB_DL, "RB index %d exceeds BWP size %d\n", rb, N_RB_DL);
        sched_pdsch.rbBitmap[rb / 8] |= (1 << (rb % 8));
        sched_pdsch.rbSize++;
      }
    }
  }

  /* the following might override the table that is mandated by RRC
   * configuration */
  current_BWP->mcsTableIdx = g_mcsTableIdx;
  sched_pdsch.time_domain_allocation = get_dl_tda(nr_mac, cell, pp_pdsch->slot);
  AssertFatal(sched_pdsch.time_domain_allocation >= 0,"Unable to find PDSCH time domain allocation in list\n");

  sched_pdsch.tda_info = get_dl_tda_info(current_BWP,
                                          sched_ctrl->search_space->searchSpaceType->present,
                                          sched_pdsch.time_domain_allocation,
                                          NR_MIB__dmrs_TypeA_Position_pos2,
                                          1,
                                          TYPE_C_RNTI_,
                                          sched_ctrl->coreset->controlResourceSetId,
                                          false);

  sched_pdsch.dmrs_parms = get_dl_dmrs_params(scc,
                                               current_BWP,
                                               &sched_pdsch.tda_info,
                                               sched_pdsch.nrOfLayers);

  sched_pdsch.Qm = nr_get_Qm_dl(sched_pdsch.mcs, current_BWP->mcsTableIdx);
  sched_pdsch.R = nr_get_code_rate_dl(sched_pdsch.mcs, current_BWP->mcsTableIdx);
  sched_pdsch.tb_size = nr_compute_tbs(sched_pdsch.Qm,
                                       sched_pdsch.R,
                                       sched_pdsch.rbSize,
                                       sched_pdsch.tda_info.nrOfSymbols,
                                       sched_pdsch.dmrs_parms.N_PRB_DMRS * sched_pdsch.dmrs_parms.N_DMRS_SLOT,
                                       0 /* N_PRB_oh, 0 for initialBWP */,
                                       0 /* tb_scaling */,
                                       sched_pdsch.nrOfLayers) >> 3;

  /* retransmission on a smaller allocation: same TBS and code rate, signalled with the reserved MCS index
   * of the same modulation (38.214 5.1.3.1), as a gNB may do e.g. in a TDD special slot */
  static uint32_t first_tb_size[NR_MAX_HARQ_PROCESSES];
  static uint16_t first_R[NR_MAX_HARQ_PROCESSES];
  const int pid = pp_pdsch->slot;
  if (sched_ctrl->harq_processes[pid].round == 0) {
    first_tb_size[pid] = sched_pdsch.tb_size;
    first_R[pid] = sched_pdsch.R;
  } else if (g_retx_rbSize > 0) {
    AssertFatal(alloc_type == PDSCH_TYPE1, "-K only supported with resource allocation type 1\n");
    sched_pdsch.rbSize = g_retx_rbSize;
    const int first_reserved = current_BWP->mcsTableIdx == 1 ? 28 : 29; // 256QAM table: 28..31 = Qm 2..8
    sched_pdsch.mcs = first_reserved + sched_pdsch.Qm / 2 - 1;
    AssertFatal(nr_get_code_rate_dl(sched_pdsch.mcs, current_BWP->mcsTableIdx) == 0, "MCS %d is not reserved\n", sched_pdsch.mcs);
    sched_pdsch.tb_size = first_tb_size[pid];
    sched_pdsch.R = first_R[pid];
  }

  const nr_pdsch_AntennaPorts_t *p = &cell->radio_config.pdsch_AntennaPorts;
  sched_pdsch.ant_port_idx.numSpatialStreamIndices = p->XP * p->N1 * p->N2;
  for (int i = 0; i < sched_pdsch.ant_port_idx.numSpatialStreamIndices;i++)
    sched_pdsch.ant_port_idx.spatialStreamIndices[i] = cell->radio_config.spatial_stream_index[i];

  /* the simulator assumes the HARQ PID is equal to the slot number */
  sched_pdsch.dl_harq_pid = pp_pdsch->slot;

  /* The scheduler uses lists to track whether a HARQ process is
   * free/busy/awaiting retransmission, and updates the HARQ process states.
   * However, in the simulation, we never get ack or nack for any HARQ process,
   * thus the list and HARQ states don't match what the scheduler expects.
   * Therefore, below lines just "repair" everything so that the scheduler
   * won't remark that there is no HARQ feedback */
  sched_ctrl->feedback_dl_harq.head = -1; // always overwrite feedback HARQ process
  if (sched_ctrl->harq_processes[pp_pdsch->slot].round == 0) // depending on round set in simulation ...
    add_front_nr_list(&sched_ctrl->available_dl_harq, pp_pdsch->slot); // ... make PID available
  else
    add_front_nr_list(&sched_ctrl->retrans_dl_harq, pp_pdsch->slot);   // ... make PID retransmission
  sched_ctrl->harq_processes[pp_pdsch->slot].is_waiting = false;
  AssertFatal(sched_pdsch.rbStart >= 0, "invalid rbStart %d\n", sched_pdsch.rbStart);
  AssertFatal(sched_pdsch.rbSize > 0, "invalid rbSize %d\n", sched_pdsch.rbSize);
  AssertFatal(sched_pdsch.mcs >= 0, "invalid mcs %d\n", sched_pdsch.mcs);
  AssertFatal(current_BWP->mcsTableIdx >= 0 && current_BWP->mcsTableIdx <= 2, "invalid mcsTableIdx %d\n", current_BWP->mcsTableIdx);

  nr_dl_candidate_t candidate = {
      .UE = UE_info,
      .rnti = UE_info->rnti,
      .is_retx = sched_ctrl->harq_processes[sched_pdsch.dl_harq_pid].round > 0,
      .retx_harq_pid = sched_pdsch.dl_harq_pid,
      .pending_bytes = sched_ctrl->num_total_bytes,
      .mcs_table = current_BWP->mcsTableIdx,
      .bwp_start = sched_pdsch.bwp_info.bwpStart,
      .bwp_size = sched_pdsch.bwp_info.bwpSize,
  };
  for (int i = 0; i < seq_arr_size(&sched_ctrl->lc_config); ++i) {
    const nr_lc_config_t *c = seq_arr_at(&sched_ctrl->lc_config, i);
    candidate.pending_bytes_per_lcid[c->lcid] = sched_ctrl->rlc_status[c->lcid].bytes_in_buffer;
  }

  post_process_dlsch(nr_mac, cell, pp_pdsch, UE_info, &sched_pdsch, &candidate);
}

nrUE_params_t nrUE_params;

nrUE_params_t *get_nrUE_params(void) {
  return &nrUE_params;
}

void validate_input_pmi(nfapi_nr_config_request_scf_t *gNB_config,
                        nr_pdsch_AntennaPorts_t pdsch_AntennaPorts,
                        int nrOfLayers,
                        int pmi)
{
  if (pmi == 0)
    return;

  nfapi_nr_pm_pdu_t *pmi_pdu = &gNB_config->pmi_list.pmi_pdu[pmi - 1]; // pmi 0 is identity matrix
  AssertFatal(pmi == pmi_pdu->pm_idx, "PMI %d doesn't match to the one in precoding matrix %d\n", pmi, pmi_pdu->pm_idx);
  AssertFatal(nrOfLayers == pmi_pdu->numLayers, "Number of layers %d doesn't match to the one in precoding matrix %d for PMI %d\n",
              nrOfLayers, pmi_pdu->numLayers, pmi);
  int num_antenna_ports = pdsch_AntennaPorts.N1 * pdsch_AntennaPorts.N2 * pdsch_AntennaPorts.XP;
  AssertFatal(num_antenna_ports == pmi_pdu->num_ant_ports, "Configured antenna ports %d does not match precoding matrix AP size %d for PMI %d\n",
              num_antenna_ports, pmi_pdu->num_ant_ports, pmi);
}


configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv)
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  FILE *csv_file = NULL;
  char *filename_csv = NULL;
  setbuf(stdout, NULL);
  int c;
  int i,aa;//,l;
  double SNR, snr0 = -2.0, snr1 = 2.0;
  uint8_t snr1set=0;
  double effRate;
  //float psnr;
  double eff_tp_check = 0.7;
  uint32_t TBS = 0;
  c16_t **txdata;
  float **s_interleaved, **r_re, **r_im;
  //double iqim = 0.0;
  //unsigned char pbch_pdu[6];
  //  int sync_pos, sync_pos_slot;
  //  FILE *rx_frame_file;
  FILE *output_fd = NULL;
  //uint8_t write_output_file=0;
  //int result;
  //int freq_offset;
  //  int subframe_offset;
  //  char fname[40], vname[40];
  int trial, n_trials = 1, n_false_positive = 0;
  //int n_errors2, n_alamouti;
  uint8_t n_tx=1,n_rx=1;
  uint8_t round;
  uint8_t num_rounds = 4;
  char gNBthreads[128]="n";

  channel_desc_t *gNB2UE;
  //uint32_t nsymb,tx_lev,tx_lev1 = 0,tx_lev2 = 0;
  //uint8_t extended_prefix_flag=0;
  //int8_t interf1=-21,interf2=-21;

  FILE *input_fd=NULL,*pbch_file_fd=NULL;
  //char input_val_str[50],input_val_str2[50];

  //uint8_t frame_mod4,num_pdcch_symbols = 0;

  SCM_t channel_model = AWGN; // AWGN Rayleigh1 Rayleigh1_anticorr;
  double DS_TDL = .03;
  int delay = 0;

  //double pbch_sinr;
  //int pbch_tx_ant;
  int mu = 1;

  //unsigned char frame_type = 0;

  int frame=1,slot=1;
  int frame_length_complex_samples;
  //int frame_length_complex_samples_no_prefix;
  NR_DL_FRAME_PARMS *frame_parms;
  UE_nr_rxtx_proc_t UE_proc;
  NR_Sched_Rsp_t *Sched_INFO;
  NR_UE_MAC_INST_t *UE_mac;
  int cyclic_prefix_type = NFAPI_CP_NORMAL;
  int loglvl=OAILOG_WARNING;

  //float target_error_rate = 0.01;
  int8_t enable_ptrs = 0;
  int8_t modify_dmrs = 0;

  int8_t dmrs_arg[3] = {-1,-1,-1};// Invalid values
  /* L_PTRS = ptrs_arg[0], K_PTRS = ptrs_arg[1] */
  int8_t ptrs_arg[2] = {-1,-1};// Invalid values

  uint16_t ptrsRePerSymb = 0;
  uint16_t pdu_bit_map = 0x0;
  uint16_t dlPtrsSymPos = 0;
  uint16_t ptrsSymbPerSlot = 0;
  uint8_t  mcsIndex = 9;
  uint8_t  dlsch_threads = 0;
  int chest_type[2] = {0};
  uint8_t  max_ldpc_iterations = 5;
  // number of PDSCH symbols per thread = 0 means do not use thread pool
  int num_pdsch_symbols_per_thread = 0;
  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_DLSIM] Error, configuration module init failed\n");
  }
  int tx_amp = 36;

  randominit();

  int print_perf = 0;
  bool do_ml = false;

  int use_cuda = 0;

  void *h_tx_sig_pinned = NULL;

#ifdef CHANNEL_SIM_CUDA
  void *d_tx_sig = NULL, *d_intermediate_sig = NULL, *d_final_output = NULL;
  void *d_curand_states = NULL;
  void *h_final_output_pinned = NULL;
  void *d_channel_coeffs_gpu = NULL;
#endif

  while ((c = getopt(argc, argv, "--:O:f:hA:p:g:i:n:s:S:t:v:x:y:z:o:H:M:N:F:GR:d:D:PI:L:a:b:e:m:w:T:U:q:X:Y:Z:Q:EK:")) != -1) {
    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    printf("handling optarg %c\n",c);
    switch (c) {
    case 'f':
#ifdef CHANNEL_SIM_CUDA
      if (strcmp(optarg, "cuda") == 0) {
        use_cuda = 1;
      } else
#endif
      {
        printf("Unsupported flag '%s' for -f. Run '-h' to see the list of available options.\n", optarg);
        exit(-1);
      }
      break;

    case 'g':
      switch ((char)*optarg) {
        case 'A':
          channel_model = TDL_A;
          DS_TDL = 0.030; // 30 ns
          printf("Channel model: TDLA30\n");
          break;
        case 'B':
          channel_model = TDL_B;
          DS_TDL = 0.100; // 100ns
          printf("Channel model: TDLB100\n");
          break;
        case 'C':
          channel_model = TDL_C;
          DS_TDL = 0.300; // 300 ns
          printf("Channel model: TDLC300\n");
          break;
        default:
          printf("Unsupported channel model!\n");
          exit(-1);
      }

      break;

    case 'i':
      for(i=0; i < atoi(optarg); i++){
        chest_type[i] = atoi(argv[optind++]);
      }
      break;

    case 'n':
      n_trials = atoi(optarg);
      break;

    case 's':
      snr0 = atof(optarg);
      printf("Setting SNR0 to %f\n",snr0);
      break;

    case 'S':
      snr1 = atof(optarg);
      snr1set=1;
      printf("Setting SNR1 to %f\n",snr1);
      break;

    case 'x':
      g_nrOfLayers = atoi(optarg);

      if ((g_nrOfLayers == 0) || (g_nrOfLayers > 4)) {
        printf("Unsupported nr Of Layers %d\n", g_nrOfLayers);
        exit(-1);
      }
      break;

    case 'p':
     g_pmi = atoi(optarg);
     break;

    case 'v':
      num_rounds = atoi(optarg);

      AssertFatal(num_rounds > 0 && num_rounds < 16, "Unsupported number of rounds %d, should be in [1,16]\n", num_rounds);
      break;

    case 'y':
      n_tx=atoi(optarg);

      if ((n_tx==0) || (n_tx>4)) {//extend gNB to support n_tx = 4
        printf("Unsupported number of tx antennas %d\n",n_tx);
        exit(-1);
      }

      break;

    case 'z':
      n_rx=atoi(optarg);

      if ((n_rx==0) || (n_rx>4)) {//extend UE to support n_tx = 4
        printf("Unsupported number of rx antennas %d\n",n_rx);
        exit(-1);
      }

      break;

    case 'R':
      N_RB_DL = atoi(optarg);
      break;

    case 'D':
      alloc_type = PDSCH_TYPE0;
      char *s = strtok(optarg, ",");
      int i = 0;
      while (s) {
        AssertFatal(i < MAX_RB_RANGES, "Too many RB ranges, max is %d\n", MAX_RB_RANGES);
        if (sscanf(s, "%d:%d", &g_rb_ranges[i].start, &g_rb_ranges[i].size) != 2) {
          printf("Invalid RB range format in '%s', expected start:size\n", s);
          exit(-1);
        }
        i++;
        s = strtok(NULL, ",");
      }
      g_nb_rb_ranges = i;
      break;

    case 'F':
      input_fd = fopen(optarg,"r");

      if (input_fd==NULL) {
        printf("Problem with filename %s\n",optarg);
        exit(-1);
      }

      break;

    case 'E':
      do_ml = true;
      break;

    case 'K':
      g_retx_rbSize = atoi(optarg);
      break;

    case 'P':
      print_perf=1;
      cpu_meas_enabled = 1;
      break;
      
    case 'I':
      max_ldpc_iterations = atoi(optarg);
      break;

    case 'L':
      loglvl = atoi(optarg);
      break;

    case 'a':
      g_rbStart = atoi(optarg);
      break;

    case 'b':
      g_rbSize = atoi(optarg);
      break;

    case 'd':
      dlsch_threads = atoi(optarg);
      break;

    case 'e':
      g_mcsIndex = atoi(optarg);
      break;

    case 'q':
      g_mcsTableIdx = atoi(optarg);
      break;

    case 'm':
      mu = atoi(optarg);
      break;

    case 't':
      eff_tp_check = atof(optarg) / 100.0;
      break;

    case 'w':
      output_fd = fopen("txdata.dat", "w+");
      break;

    case 'T':
      enable_ptrs=1;
      for(i=0; i < atoi(optarg); i++) {
        ptrs_arg[i] = atoi(argv[optind++]);
      }
      break;

    case 'U':
      modify_dmrs = 1;
      for(i=0; i < atoi(optarg); i++) {
        dmrs_arg[i] = atoi(argv[optind++]);
      }
      break;

    case 'X':
      strncpy(gNBthreads, optarg, sizeof(gNBthreads)-1);
      gNBthreads[sizeof(gNBthreads)-1]=0;
      break;

    case 'Y':
      num_pdsch_symbols_per_thread = atoi(optarg);
      break;

    case 'Z' :
      filename_csv = strdup(optarg);
      AssertFatal(filename_csv != NULL, "strdup() error: errno %d\n", errno);
      break;

    case 'o':
      delay = atoi(optarg);
      break;

    case 'H':
      slot = atoi(optarg);
      break;

    case 'Q':
      tx_amp = atoi(optarg);
      break;

    default:
    case 'h':
      printf("%s -h(elp) -p(extended_prefix) -N cell_id -F input_filename -g channel_model -n n_frames -s snr0 -S snr1 -x transmission_mode -y TXant -z RXant -i Intefrence0 -j Interference1 -A interpolation_file -C(alibration offset dB) -N CellId\n",
             argv[0]);
      printf("-h This message\n");
      printf("-L <log level, 0(errors), 1(warning), 2(analysis), 3(info), 4(debug), 5(trace)>\n");
      printf("-i Change channel estimation technique. Arguments list: Frequency domain {0:Linear interpolation, 1:PRB based averaging}, Time domain {0:Estimates of last DMRS symbol, 1:Average of DMRS symbols}\n");
      printf("-O oversampling factor (1,2,4,8,16)\n");
      printf("-A Interpolation_filname Run with Abstraction to generate Scatter plot using interpolation polynomial in file\n");
      printf("-F Input filename (.txt format) for RX conformance testing\n");
      printf("-a Start PRB for PDSCH\n");
      printf("-b Number of PRB for PDSCH\n");
      printf("-d number of dlsch threads, 0: no dlsch parallelization\n");
      printf("-e MSC index\n");
      printf("-E Enable ML-based LLR for 2-layer MIMO (QPSK/16QAM/64QAM). Default: MMSE equalization\n");
      printf("-K Number of PRBs for retransmissions, keeping the initial TBS (reserved MCS index). Default: same as initial\n");
      printf("-f <flag> Enable optional feature flag. Available flags:\n");
#ifdef CHANNEL_SIM_CUDA
      printf("          cuda    Enable CUDA channel simulation\n");
#else
      printf("          (none)  No optional features were compiled into this executable\n");
#endif
      printf("-g Channel model: [A] TDLA30, [B] TDLB100, [C] TDLC300, e.g. -g A\n");
      printf("-H Slot number\n");
      printf("-m Numerology\n");
      printf("-n Number of frames to simulate\n");
      printf("-o Introduce delay in terms of number of samples\n");
      printf("-p Precoding matrix index\n");
      printf("-q MCS Table index\n");
      printf("-s Starting SNR, runs from SNR0 to SNR0 + 5 dB.  If n_frames is 1 then just SNR is simulated\n");
      printf("-S Ending SNR, runs from SNR0 to SNR1\n");
      printf("-t Acceptable effective throughput (in percentage)\n");
      printf("-v Maximum number of rounds\n");
      printf("-w Write txdata to binary file (one frame)\n");
      printf("-x Num of layer for PDSCH\n");
      printf("-y Number of TX antennas used in gNB\n");
      printf("-z Number of RX antennas used in UE\n");
      printf("-I Maximum LDPC decoder iterations\n");
      printf("-P Print DLSCH performances\n");
      printf("-Q Backoff in dB of the gNB modulation amplitude from full scale (default 36)\n");
      printf("-R N_RB_DL\n");
      printf("-T Enable PTRS, arguments list L_PTRS{0,1,2} K_PTRS{2,4}, e.g. -T 2 0 2 \n");
      printf("-U Change DMRS Config, arguments list DMRS TYPE{0=A,1=B} DMRS AddPos{0:2} DMRS ConfType{1:2}, e.g. -U 3 0 2 1 \n");
      printf("-X gNB thread pool configuration, n => no threads\n");
      printf("-Y Number of symbols processed per PDSCH generation thread\n");
      printf("-Z Output filename (.csv format) for stats\n");
      exit (-1);
      break;
    }
  }

  logInit();
  set_glog(loglvl);
  /* initialize the sin table */
  InitSinLUT();

#ifdef CHANNEL_SIM_CUDA
  init_cuda_chsim_buffers(use_cuda,
                          n_tx,
                          n_rx,
                          &d_tx_sig,
                          &d_intermediate_sig,
                          &d_final_output,
                          &d_curand_states,
                          &h_tx_sig_pinned,
                          &h_final_output_pinned,
                          &d_channel_coeffs_gpu);
#endif

#if !defined(CHANNEL_SIM_CUDA) || !use_cuda
  printf("Pre-allocating padded host memory for the CPU channel pipeline...\n");
  int num_samples_alloc = 153600;
  const int max_padding_alloc = 256 - 1;
  size_t padded_tx_alloc_bytes = n_tx * (num_samples_alloc + max_padding_alloc) * 2 * sizeof(float);
  h_tx_sig_pinned = malloc(padded_tx_alloc_bytes);
  if (h_tx_sig_pinned == NULL) {
    printf("Error: Failed to allocate host buffer for CPU path\n");
    exit(-1);
  }
#endif

  get_softmodem_params()->phy_test = 1;
  get_softmodem_params()->do_ra = 0;
  IS_SOFTMODEM_DLSIM = true;

  if (snr1set==0)
    snr1 = snr0+10;

  RC.gNB = malloc(sizeof(PHY_VARS_gNB *));
  RC.gNB[0] = calloc_or_fail(1, sizeof(PHY_VARS_gNB));

  gNB = RC.gNB[0];
  gNB->ofdm_offset_divisor = UINT_MAX;
  gNB->phase_comp = true; // we need to perform phase compensation, otherwise everything will fail
  gNB->TX_AMP = (int16_t)(32767.0 / pow(10.0, .05 * (double)(tx_amp)));
  frame_parms = &gNB->frame_parms; //to be initialized I suppose (maybe not necessary for PBCH)
  frame_parms->nb_antennas_tx = n_tx;
  frame_parms->nb_antennas_rx = n_rx;
  frame_parms->N_RB_DL = N_RB_DL;
  frame_parms->N_RB_UL = N_RB_DL;

  AssertFatal((gNB->if_inst = NR_IF_Module_init(0)) != NULL, "Cannot register interface");
  gNB->if_inst->NR_PHY_config_req = nr_phy_config_request;

  NR_ServingCellConfigCommon_t *scc = calloc(1,sizeof(*scc));;
  prepare_scc(scc);
  uint64_t ssb_bitmap = 1; // Enable only first SSB with index ssb_indx=0
  fill_scc_sim(scc, &ssb_bitmap, N_RB_DL, N_RB_DL, mu, mu);
  fix_scc(scc, ssb_bitmap);

  frame_structure_t frame_structure = {0};
  frame_type_t frame_type = TDD;
  config_frame_structure(mu,
                         scc->tdd_UL_DL_ConfigurationCommon,
                         get_tdd_period_idx(scc->tdd_UL_DL_ConfigurationCommon),
                         frame_type,
                         &frame_structure);
  AssertFatal(is_dl_slot(slot, &frame_structure), "The slot selected is not DL. Can't run DLSIM\n");

  // TODO do a UECAP for phy-sim
  nr_pdsch_AntennaPorts_t pdsch_AntennaPorts = {0};
  pdsch_AntennaPorts.N1 = n_tx > 1 ? n_tx >> 1 : 1;
  pdsch_AntennaPorts.N2 = 1;
  pdsch_AntennaPorts.XP = n_tx > 1 ? 2 : 1;
  const nr_mac_config_t conf = {.pdsch_AntennaPorts = pdsch_AntennaPorts,
                                .pusch_AntennaPorts = n_tx,
                                .minRXTXTIME = 6,
                                .do_CSIRS = 0,
                                .do_SRS = 0,
                                .num_dlharq = 16,
                                .num_ulharq = 16,
                                .maxMIMO_layers = g_nrOfLayers,
                                .force_256qam_off = false,
                                .timer_config.sr_ProhibitTimer = 0,
                                .timer_config.sr_TransMax = 64,
                                .timer_config.sr_ProhibitTimer_v1700 = 0,
                                .timer_config.t300 = 400,
                                .timer_config.t301 = 400,
                                .timer_config.t310 = 2000,
                                .timer_config.n310 = 10,
                                .timer_config.t311 = 3000,
                                .timer_config.n311 = 1,
                                .timer_config.t319 = 400,
                                .num_agg_level_candidates = {0, 0, 1, 1, 0},
                                .spatial_stream_index = {0, 1, 2, 3},
                                .beam_config.beams_per_period = 1};
  const nr_rlc_configuration_t rlc_config = {
    .srb = {
      .t_poll_retransmit = 45,
      .t_reassembly = 35,
      .t_status_prohibit = 0,
      .poll_pdu = -1,
      .poll_byte = -1,
      .max_retx_threshold = 8,
      .sn_field_length = 12,
    },
    .drb_am = {
      .t_poll_retransmit = 45,
      .t_reassembly = 15,
      .t_status_prohibit = 15,
      .poll_pdu = 64,
      .poll_byte = 1024 * 500,
      .max_retx_threshold = 32,
      .sn_field_length = 18,
    },
    .drb_um = {
      .t_reassembly = 15,
      .sn_field_length = 12,
    }
  };

  RC.nb_nr_macrlc_inst = 1;
  nr_cell_sched_t *cell;
  gNB_MAC_INST *gNB_mac = mac_top_init_gNB(ngran_gNB, &rlc_config, &cell);
  nr_mac_cell_config_scc(cell, scc, &conf);
  nr_mac_send_config_req(gNB_mac, &cell->config);

  cell->dl_bler.harq_round_max = num_rounds;

  validate_input_pmi(&cell->config, pdsch_AntennaPorts, g_nrOfLayers, g_pmi);

  NR_UE_NR_Capability_t *UE_Capability_nr = CALLOC(1,sizeof(NR_UE_NR_Capability_t));
  prepare_sim_uecap(UE_Capability_nr, scc, mu, N_RB_DL, g_mcsTableIdx, 0);
  int uid = 0;
  int ssb_index = 0;
  const NR_feature_set_ids_t uecap_fs_ids = {.dl_feature_set_percc_id = 1,
                                             .ul_feature_set_percc_id = 1};
  NR_CellGroupConfig_t *scg = get_default_secondaryCellGroup(scc, UE_Capability_nr, &uecap_fs_ids, 0, 1, &conf, cell, uid, ssb_index);
  scg->spCellConfig->reconfigurationWithSync = get_reconfiguration_with_sync(g_rnti, uid, scc, frame);
  NR_BWP_Downlink_t *bwp = scg->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList->list.array[0];

  if (alloc_type == PDSCH_TYPE0) {
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->resourceAllocation = NR_PDSCH_Config__resourceAllocation_resourceAllocationType0;
    g_rbSize = 0;
    for (int r = 0; r < g_nb_rb_ranges; r++)
      g_rbSize += g_rb_ranges[r].size;
  }

  /* nr_mac_add_test_ue() has created one user, so set the scheduling
   * parameters from command line in global variables that will be picked up by
   * scheduling preprocessor */
  AssertFatal(N_RB_DL > g_rbStart, "rbStart %d exceeds BWP size %d\n", g_rbStart, N_RB_DL);
  if (g_rbSize == 0)
    g_rbSize = N_RB_DL - g_rbStart;

  /* -U option modify DMRS */
  if(modify_dmrs) {
    update_dmrs_config(bwp, dmrs_arg);
  }
  /* -T option enable PTRS */
  if(enable_ptrs) {
    update_ptrs_config(bwp, &g_rbSize, &mcsIndex, ptrs_arg);
  }

  // UE dedicated configuration
  nr_mac_add_test_ue(gNB_mac, cell, g_rnti, scg);
  // reset preprocessor to the one of DLSIM after it has been set during
  // mac_top_init_gNB()
  gNB_mac->pre_processor_dl = nr_dlsim_preprocessor;
  phy_init_nr_gNB(gNB);
  gNB->num_pdsch_symbols_per_thread = num_pdsch_symbols_per_thread;
  N_RB_DL = gNB->frame_parms.N_RB_DL;
  NR_UE_info_t *UE_info = find_connected_nr_ue(&gNB_mac->UE_info, g_rnti);
  DevAssert(UE_info && UE_info->rnti == g_rnti);

  configure_UE_BWP(cell, scc, UE_info, false, NR_SearchSpace__searchSpaceType_PR_ue_Specific, -1, -1);

  // stub to configure frame_parms
  //  nr_phy_config_request_sim(gNB,N_RB_DL,N_RB_DL,mu,Nid_cell,SSB_positions);
  // call MAC to configure common parameters

  double fs,txbw,rxbw;
  get_samplerate_and_bw(mu,
                        N_RB_DL,
                        frame_parms->threequarter_fs,
                        &fs,
                        &txbw,
                        &rxbw);

  gNB2UE = new_channel_desc_scm(n_tx,
                                n_rx,
                                channel_model,
                                fs/1e6,//sampling frequency in MHz
                                0,
                                txbw,
                                DS_TDL,
                                0.0,
                                CORR_LEVEL_LOW,
                                0,
                                delay,
                                0,
                                0);

#ifdef CHANNEL_SIM_CUDA
  float *h_channel_coeffs = NULL;
  if (use_cuda) {
    int num_links = n_tx * n_rx;
    h_channel_coeffs = (float *)malloc(num_links * gNB2UE->channel_length * sizeof(float) * 2);
  }
#endif
  if (gNB2UE==NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  frame_length_complex_samples = frame_parms->samples_per_subframe*NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  //frame_length_complex_samples_no_prefix = frame_parms->samples_per_subframe_wCP*NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  int slot_offset = get_samples_slot_timestamp(frame_parms, slot);
  int slot_length = slot_offset - get_samples_slot_timestamp(frame_parms, slot - 1);

  s_interleaved = malloc(n_tx * sizeof(float *));
  r_re = malloc(n_rx * sizeof(float *));
  r_im = malloc(n_rx * sizeof(float *));
  txdata = malloc(n_tx * sizeof(int *));

  for (i = 0; i < n_tx; i++) {
    s_interleaved[i] = malloc(slot_length * 2 * sizeof(float));
    printf("Allocating %d samples for txdata\n", frame_length_complex_samples);
    txdata[i] = calloc(1, frame_length_complex_samples * sizeof(int));
  }

  for (i = 0; i < n_rx; i++) {
    r_re[i] = calloc(1, slot_length * sizeof(float));
    r_im[i] = calloc(1, slot_length * sizeof(float));
  }

  if (pbch_file_fd!=NULL) {
    load_pbch_desc(pbch_file_fd);
  }


  //configure UE
  UE = calloc(1, sizeof(PHY_VARS_NR_UE));
  nrPHY_vars_UE_g = malloc(sizeof(PHY_VARS_NR_UE **));
  nrPHY_vars_UE_g[0] = malloc(sizeof(PHY_VARS_NR_UE *));
  nrPHY_vars_UE_g[0][0] = UE;
  memcpy(&UE->frame_parms, frame_parms, sizeof(*frame_parms));
  UE->frame_parms.nb_antennas_rx = n_rx;
  UE->frame_parms.nb_antenna_ports_gNB = n_tx;
  UE->nrLDPC_coding_interface = gNB->nrLDPC_coding_interface;
  UE->max_ldpc_iterations = max_ldpc_iterations;
  UE->do_ml = do_ml;
  init_nr_ue_phy_cpu_stats(&UE->phy_cpu_stats);
  UE->is_synchronized = 1;

  if (init_nr_ue_signal(UE, 1) != 0)
  {
    printf("Error at UE NR initialisation\n");
    exit(-1);
  }

  init_nr_ue_transport(UE);
  int capability_rx2tx = 3;
  UE_mac = nr_l2_init_ue(0, mu, capability_rx2tx);
  ue_init_config_request(UE_mac, get_slots_per_frame_from_scs(mu));

  UE->if_inst = nr_ue_if_module_init(0);
  UE->if_inst->scheduled_response = nr_ue_scheduled_response;
  UE->if_inst->phy_config_request = nr_ue_phy_config_request;
  UE->if_inst->dl_indication = nr_ue_dl_indication;
  UE->if_inst->ul_indication = dummy_nr_ue_ul_indication;
  UE->chest_freq = chest_type[0];
  UE->chest_time = chest_type[1];

  UE_mac->if_module = nr_ue_if_module_init(0);

  unsigned int available_bits=0;
  unsigned char *estimated_output_bit=NULL;
  unsigned char *test_input_bit=NULL;
  unsigned int errors_bit = 0;

  initFloatingCoresTpool(dlsch_threads, &nrUE_params.Tpool, false, "UE-tpool");

  // generate signal
  AssertFatal(input_fd==NULL,"Not ready for input signal file\n");

  // clone CellGroup to have a separate copy at UE
  NR_CellGroupConfig_t *UE_CellGroup = clone_CellGroupConfig(scg);

  //Configure UE
  NR_BCCH_BCH_Message_t *mib = get_new_MIB_NR(scc);
  nr_rrc_mac_config_req_mib(0, 0, mib->message.choice.mib, false);
  nr_rrc_mac_config_req_cg(0, 0, 0, 0, UE_CellGroup, UE_Capability_nr);

  asn1cFreeStruc(asn_DEF_NR_CellGroupConfig, UE_CellGroup);

  UE_mac->state = UE_CONNECTED;
  UE_mac->ra.ra_state = nrRA_SUCCEEDED;

  nr_phy_data_t phy_data = {0};
  fapi_nr_dl_config_request_t dl_config = {.sfn = frame, .slot = slot};
  nr_scheduled_response_t scheduled_response = {.dl_config = &dl_config, .phy_data = &phy_data, .mac = UE_mac};

  nr_ue_phy_config_request(&UE_mac->phy_config);
  //NR_COMMON_channels_t *cc = RC.nrmac[0]->common_channels;
  int ret = 1;
  initNamedTpool(gNBthreads, &gNB->threadPool, true, "gNB-tpool");
  initNotifiedFIFO(&gNB->L1_tx_out);

  // Buffers to store internal memory of slot process
  int rx_size = (((14 * UE->frame_parms.N_RB_DL * 12 * sizeof(int32_t)) + 15) >> 4) << 4;
  UE->phy_sim_rxdataF = calloc(sizeof(int32_t *) * UE->frame_parms.nb_antennas_rx * g_nrOfLayers,
                               UE->frame_parms.samples_per_slot_wCP * sizeof(int32_t));
  UE->phy_sim_pdsch_llr = calloc(1, (8 * (3 * 8 * 8448)) * sizeof(int16_t)); // Max length
  UE->phy_sim_pdsch_rxdataF_ext = calloc(sizeof(int32_t *) * UE->frame_parms.nb_antennas_rx * g_nrOfLayers, rx_size);
  UE->phy_sim_pdsch_rxdataF_comp = calloc(sizeof(int32_t *) * UE->frame_parms.nb_antennas_rx * g_nrOfLayers, rx_size);
  UE->phy_sim_pdsch_dl_ch_estimates = calloc(sizeof(int32_t *) * UE->frame_parms.nb_antennas_rx * g_nrOfLayers, rx_size);
  UE->phy_sim_pdsch_dl_ch_estimates_ext = calloc(sizeof(int32_t *) * UE->frame_parms.nb_antennas_rx * g_nrOfLayers, rx_size);
  int a_segments = MAX_NUM_NR_DLSCH_SEGMENTS; // number of segments to be allocated
  if (g_rbSize != 273) {
    a_segments = a_segments*g_rbSize;
    a_segments = (a_segments/273)+1;
  }
  uint32_t dlsch_bytes = a_segments*1056;  // allocated bytes per segment
  UE->phy_sim_dlsch_b = calloc(1, dlsch_bytes);

  // csv file
  if (filename_csv != NULL) {
    csv_file = fopen(filename_csv, "a");
    if (csv_file == NULL) {
      printf("Can't open file \"%s\", errno %d\n", filename_csv, errno);
      free(s_interleaved);
      free(r_re);
      free(r_im);
      free(txdata);
      return 1;
    }
    // adding name of parameters into file
    fprintf(csv_file,"SNR,false_positive,");
    for (int r = 0; r < num_rounds; r++)
      fprintf(csv_file,"n_errors_%d,errors_scrambling_%d,channel_bler_%d,channel_ber_%d,",r,r,r,r);
    fprintf(csv_file,"avg_round,eff_rate,eff_throughput,TBS\n");
  }
  //---------------

  Sched_INFO = memalign(32, sizeof(*Sched_INFO));
  if (Sched_INFO == NULL) {
    LOG_E(PHY, "out of memory\n");
    exit(1);
  }

  time_stats_t channel_stats = {0};
  time_stats_t noise_stats = {0};
  time_stats_t pipeline_stats = {0};
  init_sorted_list_meas(&gNB->phy_proc_tx, num_rounds * n_trials);

  for (SNR = snr0; SNR < snr1 && !stop; SNR += .2) {

    reset_meas(&gNB->phy_proc_tx);
    reset_meas(&gNB->dlsch_scrambling_stats);
    reset_meas(&gNB->dlsch_crc_stats);
    reset_meas(&gNB->dlsch_modulation_stats);
    reset_meas(&gNB->dlsch_pdsch_generation_stats);
    reset_meas(&gNB->dlsch_precoding_stats);
    reset_meas(&gNB->dlsch_layer_mapping_stats);
    reset_meas(&gNB->dlsch_resource_mapping_stats);
    reset_meas(&gNB->dlsch_encoding_stats);
    reset_meas(&gNB->dci_generation_stats);

    uint32_t errors_scrambling[16] = {0};
    int n_errors[16] = {0};
    int round_trials[16] = {0};
    double roundStats = {0};
    double blerStats[16] = {0};
    double berStats[16] = {0};

    effRate = 0;
    //n_errors2 = 0;
    //n_alamouti = 0;
    n_false_positive = 0;
    if (n_trials== 1) num_rounds = 1;

    NR_gNB_DLSCH_t *gNB_dlsch = &gNB->dlsch[0];
    memset(Sched_INFO, 0, sizeof(*Sched_INFO));
    nfapi_nr_dl_tti_request_body_t *dl_req = &Sched_INFO->DL_req.dl_tti_request_body;
    // scheduler will place PDSCH second (after PDCCH), verification below
    nfapi_nr_dl_tti_request_pdu_t  *dl_tti_pdsch_pdu = &dl_req->dl_tti_pdu_list[1];
    nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdsch_pdu_rel15 = &dl_tti_pdsch_pdu->pdsch_pdu.pdsch_pdu_rel15;

    for (trial = 0; trial < n_trials && !stop; trial++) {

      errors_bit = 0;
      //multipath channel
      //multipath_channel(gNB2UE,s_re,s_im,r_re,r_im,frame_length_complex_samples,0);

      UE_proc.frame_rx = frame;
      UE_proc.nr_slot_rx = slot;
      UE_proc.gNB_id = 0;

      NR_UE_DLSCH_t *dlsch0 = &phy_data.dlsch[0];

      int harq_pid = slot;
      NR_DL_UE_HARQ_t *UE_harq_process = &UE->dl_harq_processes[0][harq_pid];

      UE_harq_process->decodeResult = false;
      round = 0;
      UE_harq_process->DLround = round;
      UE_harq_process->first_rx = 1;
      UE_harq_process->activated_frame = UE_proc.frame_rx;
      UE_harq_process->activated_slot = UE_proc.nr_slot_rx;

      while (round < num_rounds && !UE_harq_process->decodeResult && !stop) {
        reset_sched_response(Sched_INFO, frame, slot, 0, 0);
        clear_nr_nfapi_information(cell, frame, slot);
        UE_info->UE_sched_ctrl.harq_processes[harq_pid].ndi = !(trial&1);
        UE_info->UE_sched_ctrl.harq_processes[harq_pid].round = round;

        // nr_schedule_ue_spec() requires the mutex to be locked
        NR_SCHED_LOCK(&gNB_mac->sched_lock);
        nr_schedule_ue_spec(gNB_mac, cell, frame, slot, &Sched_INFO->DL_req, &Sched_INFO->TX_req);
        NR_SCHED_UNLOCK(&gNB_mac->sched_lock);

        /* check that second message is indeed PDSCH */
        DevAssert(dl_tti_pdsch_pdu->PDUType == NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE);

        /* PTRS values for DLSIM calculations   */
        pdu_bit_map = pdsch_pdu_rel15->pduBitmap;
	// This is already in DL procedures??
        if(pdu_bit_map & 0x1) {
          dlPtrsSymPos = get_ptrs_symb_idx(pdsch_pdu_rel15->NrOfSymbols,
                                           pdsch_pdu_rel15->StartSymbolIndex,
                                           1 << pdsch_pdu_rel15->PTRSTimeDensity,
                                           pdsch_pdu_rel15->dlDmrsSymbPos);
          ptrsSymbPerSlot = get_ptrs_symbols_in_slot(dlPtrsSymPos, pdsch_pdu_rel15->StartSymbolIndex, pdsch_pdu_rel15->NrOfSymbols);
          ptrsRePerSymb = ((g_rbSize + pdsch_pdu_rel15->PTRSFreqDensity - 1) / pdsch_pdu_rel15->PTRSFreqDensity);
          LOG_D(PHY,"[DLSIM] PTRS Symbols in a slot: %2u, RE per Symbol: %3u, RE in a slot %4d\n", ptrsSymbPerSlot, ptrsRePerSymb, ptrsSymbPerSlot * ptrsRePerSymb);
        }

        start_meas(&gNB->phy_proc_tx);
        phy_procedures_gNB_TX(gNB, &Sched_INFO->DL_req, &Sched_INFO->TX_req, &Sched_INFO->UL_dci_req, frame, slot);
        stop_meas(&gNB->phy_proc_tx);

        if (n_trials==1) {
          LOG_M("txsigF0.m",
                "txsF0=",
                &gNB->common_vars.txdataF[0][2 * frame_parms->ofdm_symbol_size],
                frame_parms->ofdm_symbol_size,
                1,
                1);
          if (gNB->frame_parms.nb_antennas_tx>1)
            LOG_M("txsigF1.m",
                  "txsF1=",
                  &gNB->common_vars.txdataF[1][2 * frame_parms->ofdm_symbol_size],
                  frame_parms->ofdm_symbol_size,
                  1,
                  1);
        }
        if (n_trials == 1)
          printf("slot_offset %d\n", slot_offset);

        //TODO: loop over slots
        for (aa = 0; aa < gNB->frame_parms.nb_antennas_tx; aa++) {
          if (cyclic_prefix_type == 1) {
            for (int i = 0; i < 12; i++)
              fftshift_inverse_inplace(gNB->common_vars.txdataF[aa] + i * frame_parms->ofdm_symbol_size,
                                       frame_parms->N_RB_DL * NR_NB_SC_PER_RB,
                                       frame_parms->ofdm_symbol_size);
            PHY_ofdm_mod((int *)gNB->common_vars.txdataF[aa],
                         (int *)&txdata[aa][slot_offset],
                         frame_parms->ofdm_symbol_size,
                         12,
                         frame_parms->nb_prefix_samples,
                         CYCLIC_PREFIX);
          } else {
            bool was_symbol_used[NR_SYMBOLS_PER_SLOT];
            for (int i = 0; i < 14; i++) {
              was_symbol_used[i] = true;
            }
            for (int i = 0; i < 14; i++)
              fftshift_inverse_inplace(gNB->common_vars.txdataF[aa] + i * frame_parms->ofdm_symbol_size,
                                       frame_parms->N_RB_DL * NR_NB_SC_PER_RB,
                                       frame_parms->ofdm_symbol_size);
            nr_normal_prefix_mod(gNB->common_vars.txdataF[aa], &txdata[aa][slot_offset], 14, frame_parms, slot, was_symbol_used);
          }
        }
        if (n_trials==1) {
          char filename[100];//LOG_M
          for (aa=0;aa<n_tx;aa++) {
            sprintf(filename,"txsig%d.m", aa);//LOG_M
            LOG_M(filename,"txs", &txdata[aa][slot_offset +frame_parms->ofdm_symbol_size+frame_parms->nb_prefix_samples0],6*(frame_parms->ofdm_symbol_size+frame_parms->nb_prefix_samples),1,1);
          }
          if (output_fd) {
            printf("writing txdata to binary file\n");
            fwrite(txdata[0], sizeof(int32_t), frame_length_complex_samples, output_fd);
          }
        }

        // Compute transmitter energy level
        int l_ofdm = 6;
        int symbol_offset = slot_offset + l_ofdm * frame_parms->ofdm_symbol_size + (l_ofdm - 1) * frame_parms->nb_prefix_samples
                            + frame_parms->nb_prefix_samples0;
        int symbol_length = frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples;
        double txlev_sum = compute_tx_energy_level(txdata, n_tx, symbol_offset, symbol_length, n_trials);

        for (int aa = 0; aa < frame_parms->nb_antennas_tx; aa++) {
          for (int i = 0; i < slot_length; i++) {
            s_interleaved[aa][2 * i] = (float)((short *)&txdata[aa][slot_offset])[2 * i];
            s_interleaved[aa][2 * i + 1] = (float)((short *)&txdata[aa][slot_offset])[2 * i + 1];
          }
        }

        double ts = 1.0/(frame_parms->subcarrier_spacing * frame_parms->ofdm_symbol_size);

        // Estimate noise power from the transmitter level and SNR
        double sigma2 = compute_noise_variance(txlev_sum, UE->frame_parms.ofdm_symbol_size, g_rbSize, 1, SNR, n_trials);
        const int padding_len = gNB2UE->channel_length - 1;
        const int padded_slot_length = slot_length + padding_len;
        float *h_tx_ptr = (float *)h_tx_sig_pinned;
        size_t total_padded_bytes_for_slot = n_tx * padded_slot_length * 2 * sizeof(float);
        memset(h_tx_ptr, 0, total_padded_bytes_for_slot);

        for (int j = 0; j < n_tx; j++) {
          float *data_start_ptr = h_tx_ptr + (j * padded_slot_length + padding_len) * 2;
          memcpy(data_start_ptr, s_interleaved[j], slot_length * 2 * sizeof(float));
        }

        for (aa = 0; aa < n_rx; aa++) {
          bzero(r_re[aa], slot_length * sizeof(float));
          bzero(r_im[aa], slot_length * sizeof(float));
        }

        // Apply MIMO Channel
#ifdef CHANNEL_SIM_CUDA
        if (use_cuda) {
#if defined(USE_UNIFIED_MEMORY)
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000 
          struct cudaMemLocation deviceId;
          deviceId.type = cudaMemLocationTypeDevice;
          cudaGetDevice(&deviceId.id);
          cudaMemPrefetchAsync(d_tx_sig, n_tx * padded_slot_length * sizeof(float) * 2, deviceId, 0, 0);
#else
          int deviceId;
          cudaGetDevice(&deviceId);
          cudaMemPrefetchAsync(d_tx_sig, n_tx * padded_slot_length * sizeof(float) * 2, deviceId, 0);
#endif
#endif		
          start_meas(&pipeline_stats);
          random_channel(gNB2UE, 0);
          int num_links = gNB2UE->nb_tx * gNB2UE->nb_rx;
          for (int link = 0; link < num_links; link++) {
            for (int l = 0; l < gNB2UE->channel_length; l++) {
              int idx = link * gNB2UE->channel_length + l;
              ((float2 *)h_channel_coeffs)[idx].x = (float)gNB2UE->ch[link][l].r;
              ((float2 *)h_channel_coeffs)[idx].y = (float)gNB2UE->ch[link][l].i;
            }
          }
          run_channel_pipeline_cuda(UE->common_vars.rxdata,
                                    n_tx,
                                    n_rx,
                                    gNB2UE->channel_length,
                                    slot_length,
                                    h_channel_coeffs,
                                    (float)sigma2,
                                    ts,
                                    pdu_bit_map,
                                    0x1,
                                    slot_offset,
                                    delay,
                                    d_tx_sig,
                                    d_intermediate_sig,
                                    d_final_output,
                                    d_curand_states,
                                    h_tx_sig_pinned,
                                    h_final_output_pinned,
                                    d_channel_coeffs_gpu);
          cudaDeviceSynchronize();
          stop_meas(&pipeline_stats);
        } else
#endif
        {
          float **tx_sig_for_cpu = malloc(n_tx * sizeof(float *));
          float *h_tx_ptr = (float *)h_tx_sig_pinned;
          const int padding_len = gNB2UE->channel_length - 1;
          const int padded_slot_length = slot_length + padding_len;

          for (int j = 0; j < n_tx; j++) {
            tx_sig_for_cpu[j] = h_tx_ptr + (j * padded_slot_length + padding_len) * 2;
          }

          for (aa = 0; aa < n_rx; aa++) {
            bzero(r_re[aa], slot_length * sizeof(float));
            bzero(r_im[aa], slot_length * sizeof(float));
          }
          start_meas(&channel_stats);
          multipath_channel_float(gNB2UE, tx_sig_for_cpu, r_re, r_im, slot_length, 0, (n_trials == 1) ? 1 : 0);
          stop_meas(&channel_stats);

          free(tx_sig_for_cpu);

          bool apply_phase_noise = (pdu_bit_map & 0x1);
          start_meas(&noise_stats);
          add_noise_float(UE->common_vars.rxdata,
                          (const float **)r_re,
                          (const float **)r_im,
                          (float)sigma2,
                          slot_length,
                          slot_offset,
                          ts,
                          delay,
                          apply_phase_noise,
                          UE->frame_parms.nb_antennas_rx);
          stop_meas(&noise_stats);
        }

        dl_config.sfn = frame;
        dl_config.slot = slot;
        ue_dci_configuration(UE_mac, &dl_config, frame, slot);
        nr_ue_scheduled_response(&scheduled_response);

        pbch_processing(UE, &UE_proc, &phy_data);
        pdcch_processing(UE, &UE_proc, &phy_data);
        NR_DL_UE_HARQ_t *decode_harq = &UE->dl_harq_processes[0][phy_data.dlsch_config.harq_process_nbr];
        decode_harq->activated_frame = UE_proc.frame_rx;
        decode_harq->activated_slot = UE_proc.nr_slot_rx;
        pdsch_processing(UE,
                         &UE_proc,
                         &phy_data);
        //----------------------------------------------------------
        //---------------------- count errors ----------------------
        //----------------------------------------------------------

        if (dlsch0->last_iteration_cnt >= dlsch0->max_ldpc_iterations)
          n_errors[round]++;

        if (dlsch0->active)
          round_trials[round]++;

        int16_t *UE_llr = (int16_t*)UE->phy_sim_pdsch_llr;

        TBS = phy_data.dlsch_config.cw_info[0].TBS;
        uint16_t length_dmrs = get_num_dmrs(phy_data.dlsch_config.dlDmrsSymbPos);
        uint16_t nb_rb = phy_data.dlsch_config.number_rbs;
        uint8_t nb_re_dmrs = phy_data.dlsch_config.dmrsConfigType == NFAPI_NR_DMRS_TYPE1 ?
                             6 * phy_data.dlsch_config.n_dmrs_cdm_groups :
                             4 * phy_data.dlsch_config.n_dmrs_cdm_groups;
        uint8_t mod_order = phy_data.dlsch_config.cw_info[0].qamModOrder;
        uint8_t nb_symb_sch = phy_data.dlsch_config.number_symbols;
        uint32_t unav_res = ptrsSymbPerSlot * ptrsRePerSymb;
        available_bits = nr_get_G(nb_rb, nb_symb_sch, nb_re_dmrs, length_dmrs, unav_res, mod_order, pdsch_pdu_rel15->nrOfLayers);
        if (pdu_bit_map & 0x1) {
          if (trial == 0 && round == 0) {
            printf("[DLSIM][PTRS] Available bits are: %5u, removed PTRS bits are: %5u \n", available_bits, (ptrsSymbPerSlot * ptrsRePerSymb * pdsch_pdu_rel15->nrOfLayers * mod_order));
          }
        }

        for (i = 0; i < available_bits; i++) {
          const uint8_t current_bit = (gNB_dlsch->f[i / 8] & (1 << (i & 7))) >> (i & 7);
          if (((current_bit == 0) && (UE_llr[i] <= 0)) || ((current_bit == 1) && (UE_llr[i] >= 0))) {
            if (errors_scrambling[round] == 0) {
              LOG_D(PHY,"First bit in error in unscrambling = %d\n",i);
            }
            errors_scrambling[round]++;
          }
        }

        //printf("dlsim round %d ends\n",round);
        round++;
      } // round

      if (test_input_bit == NULL) {
        test_input_bit = (unsigned char *)malloc16(8 * pdsch_pdu_rel15->TBSize[0]);
        estimated_output_bit = (unsigned char *)malloc16(8 * pdsch_pdu_rel15->TBSize[0]);
      }
      for (i = 0; i < TBS; i++) {

	estimated_output_bit[i] = (UE->phy_sim_dlsch_b[i/8] & (1 << (i & 7))) >> (i & 7);
	test_input_bit[i]       = (gNB_dlsch->b[i / 8] & (1 << (i & 7))) >> (i & 7); // Further correct for multiple segments
	
	if (estimated_output_bit[i] != test_input_bit[i]) {
	  if(errors_bit == 0)
	    LOG_D(PHY,"First bit in error in decoding = %d\n",i);
	  errors_bit++;
	}
	
      }
      ////////////////////////////////////////////////////////////

      if (errors_bit > 0) {
	n_false_positive++;
	if (n_trials == 1)
	  printf("errors_bit = %u (trial %d)\n", errors_bit, trial);
      }
      roundStats += round;
      if (UE_harq_process->decodeResult)
        effRate += ((double)TBS) / round;
    } // noise trials

    roundStats /= n_trials;

    for (int r = 0; r < num_rounds; r++) {
      blerStats[r] = (double)n_errors[r] / round_trials[r];
      berStats[r] = (double)errors_scrambling[r] / available_bits / round_trials[r];
    }

    effRate /= n_trials;
    printf("*****************************************\n");
    printf("SNR %f: n_errors (%d/%d", SNR, n_errors[0], round_trials[0]);
    for (int r = 1; r < num_rounds; r++)
      printf(",%d/%d", n_errors[r], round_trials[r]);
    printf(") (negative CRC), false_positive %d/%d, errors_scrambling (%u/%u", n_false_positive, n_trials, errors_scrambling[0], available_bits * round_trials[0]);
    for (int r = 1; r < num_rounds; r++)
      printf(",%u/%u", errors_scrambling[r], available_bits * round_trials[r]);
    printf(")\n\n");
    dump_pdsch_stats(stdout,gNB);
    printf("SNR %f: Channel BLER (%e", SNR, blerStats[0]);
    for (int r = 1; r < num_rounds; r++)
      printf(",%e", blerStats[r]);
    printf("), Channel BER (%e", berStats[0]);
    for (int r = 1; r < num_rounds; r++)
      printf(",%e", berStats[r]);
    printf(") Avg round %.2f, Eff Rate %.4f bits/slot, Eff Throughput %.2f, TBS %u bits/slot\n", roundStats, effRate, effRate / (8 * pdsch_pdu_rel15->TBSize[0]) * 100, 8 * pdsch_pdu_rel15->TBSize[0]);
    printf("*****************************************\n");
    printf("\n");
    // writing to csv file
    if (filename_csv != NULL) { // means we are asked to print stats to CSV
      fprintf(csv_file,"%f,%d/%d,",SNR,n_false_positive,n_trials);
      for (int r = 0; r < num_rounds; r++)
        fprintf(csv_file,"%d/%d,%u/%u,%f,%e,",n_errors[r], round_trials[r], errors_scrambling[r], available_bits * round_trials[r],blerStats[r],berStats[r]);
      fprintf(csv_file,"%.2f,%.4f,%.2f,%u\n", roundStats, effRate, effRate / (8 * pdsch_pdu_rel15->TBSize[0]) * 100, 8 * pdsch_pdu_rel15->TBSize[0]);
    }
    if (print_perf==1) {
      printf("\ngNB TX function statistics (per %d us slot, NPRB %d, mcs %d, C %d, block %d)\n",
             1000 >> *scc->ssbSubcarrierSpacing,
             g_rbSize,
             g_mcsIndex,
             UE->dl_harq_processes[0][slot].C,
             8 * pdsch_pdu_rel15->TBSize[0]);
      printDistribution(&gNB->phy_proc_tx, "PHY proc tx");
      printStatIndent2(&gNB->dci_generation_stats, "DCI encoding time");
      printStatIndent2(&gNB->dlsch_encoding_stats,"DLSCH encoding time");
      printStatIndent3(&gNB->dlsch_ldpc_encode_stats,"LDPC encoding time");
      printStatIndent2(&gNB->dlsch_modulation_stats,"DLSCH modulation time");
      printStatIndent2(&gNB->dlsch_scrambling_stats, "DLSCH scrambling time");
      printStatIndent3(&gNB->dlsch_crc_stats,"DLSCH Outer CRC time");
      printStatIndent2(&gNB->dlsch_pdsch_generation_stats,"DLSCH PDSCH Generation time");
      printStatIndent3(&gNB->dlsch_scrambling_stats, "DLSCH scrambling time");
      if (gNB->dlsch_layer_mapping_stats.trials > 0) {
        printStatIndent3(&gNB->dlsch_modulation_stats, "DLSCH modulation time");
        printStatIndent3(&gNB->dlsch_layer_mapping_stats, "DLSCH Layer Mapping time");
      } else {
        printStatIndent3(&gNB->dlsch_modulation_stats, "DLSCH mod/lm time");
      }
      if (num_pdsch_symbols_per_thread == 0) {
        gNB->dlsch_resource_mapping_stats.trials = gNB->dlsch_modulation_stats.trials;
        if (gNB->dlsch_precoding_stats.trials > 0)
          printStatIndent3(&gNB->dlsch_resource_mapping_stats, "DLSCH Resource Mapping time");
        gNB->dlsch_precoding_stats.trials = gNB->dlsch_modulation_stats.trials;
        if (gNB->dlsch_precoding_stats.trials > 0)
          printStatIndent3(&gNB->dlsch_precoding_stats, "DLSCH Precoding time");
      }

      if (use_cuda) {
        printStatIndent(&pipeline_stats, "GPU Channel Pipeline");
      } else {
        printStatIndent(&channel_stats, "Multipath Channel (CPU)");
        printStatIndent(&noise_stats, "Add Noise (CPU)");
      }

      printf("\nUE function statistics (per %d us slot)\n", 1000 >> *scc->ssbSubcarrierSpacing);
      for (int i = RX_PDSCH_STATS; i <= DLSCH_PROCEDURES_STATS; i++) {
        printStatIndent(&UE->phy_cpu_stats.cpu_time_stats[i], UE->phy_cpu_stats.cpu_time_stats[i].meas_name);
      }
    }

    if (n_trials == 1) {
      unsigned int op_format = 1;
      unsigned int dec = 1;
      if (output_fd) { // Write in bin format
        op_format |= MATLAB_RAW;
      }

      LOG_M("rxsig0.m", "rxs0", UE->common_vars.rxdata[0], frame_length_complex_samples, dec, op_format);
      if (UE->frame_parms.nb_antennas_rx>1)
        LOG_M("rxsig1.m", "rxs1", UE->common_vars.rxdata[1], frame_length_complex_samples, dec, op_format);
      LOG_M("rxF0.m", "rxF0", UE->phy_sim_rxdataF, frame_parms->samples_per_slot_wCP, dec, op_format);
      const uint32_t numReSym = (g_rbSize * 12 + 15) & (~15);
      for (int l = 0; l < g_nrOfLayers; l++) {
        for (int r = 0; r < n_rx; r++) {
          const int s = pdsch_pdu_rel15->StartSymbolIndex;
          const int n = pdsch_pdu_rel15->NrOfSymbols;
          for (int i = s; i < s + n; i++) {
            const uint32_t dmrsBitMap = phy_data.dlsch_config.dlDmrsSymbPos;
            const uint32_t dmrsCfg = phy_data.dlsch_config.dmrsConfigType;
            const uint32_t nrb = phy_data.dlsch_config.number_rbs;
            const uint32_t ncdmg = phy_data.dlsch_config.n_dmrs_cdm_groups;
            const uint32_t numValidReSym = ((dmrsBitMap >> i) & 1)
                                              ? ((dmrsCfg == NFAPI_NR_DMRS_TYPE1) ? nrb * (12 - 6 * ncdmg) : nrb * (12 - 4 * ncdmg))
                                              : (nrb * 12);
            char fName[50];
            snprintf(fName, sizeof(fName), "chestF_ext_l%d_r%d_s%d.m", l, r, i);
            uint32_t buff_offset = (i * g_nrOfLayers * n_rx * numReSym) + (l * n_rx * numReSym) + (r * numReSym);
            LOG_M(fName,
                  "chF0_ext",
                  ((c16_t *)UE->phy_sim_pdsch_dl_ch_estimates_ext) + buff_offset,
                  numValidReSym,
                  dec,
                  op_format);
            snprintf(fName, sizeof(fName), "rxF_comp_l%d_s%d.m", l, i);
            LOG_M(fName, "rxFc", ((c16_t *)UE->phy_sim_pdsch_rxdataF_comp) + (l * NR_SYMBOLS_PER_SLOT * numReSym) + (i * numReSym), numValidReSym, dec, op_format);
            if (l == 0) {
              snprintf(fName, sizeof(fName), "rxF_ext_r%d_s%d.m", r, i);
              buff_offset = (i * n_rx * numReSym) + (r * numReSym);
              LOG_M(fName, "rxFext", ((c16_t *)UE->phy_sim_pdsch_rxdataF_ext) + buff_offset, numValidReSym, dec, op_format);
            }
            snprintf(fName, sizeof(fName), "chestF0_s%d.m", i);
            LOG_M(fName, "chF0", ((c16_t *)UE->phy_sim_pdsch_dl_ch_estimates) + i * frame_parms->ofdm_symbol_size, nrb*12, dec, op_format);
          }
        }
      }
      LOG_M("chestF0.m", "chF0", UE->phy_sim_pdsch_dl_ch_estimates, frame_parms->ofdm_symbol_size * 14, dec, op_format);
      op_format = (op_format & (~1));
      LOG_M("rxF_llr.m", "rxFllr", UE->phy_sim_pdsch_llr, available_bits, dec, op_format);
      break;
    }

    if (effRate >= (eff_tp_check * 8 * pdsch_pdu_rel15->TBSize[0])) {
      printf("PDSCH test OK\n");
      ret = 0;
      break;
    }

  } // NSR

  free_sorted_list_meas(&gNB->phy_proc_tx);
  free(Sched_INFO);

  free_channel_desc_scm(gNB2UE);

  for (i = 0; i < n_tx; i++) {
    free(s_interleaved[i]);
    free(txdata[i]);
  }
  for (i = 0; i < n_rx; i++) {
    free(r_re[i]);
    free(r_im[i]);
  }

#ifdef CHANNEL_SIM_CUDA
  free_cuda_chsim_buffers(use_cuda,
                          &d_tx_sig,
                          &d_intermediate_sig,
                          &d_final_output,
                          &d_curand_states,
                          &h_tx_sig_pinned,
                          &h_final_output_pinned,
                          &h_channel_coeffs,
                          &d_channel_coeffs_gpu);
#endif

  free(s_interleaved);
  free(r_re);
  free(r_im);
  free(txdata);
  free(test_input_bit);
  free(estimated_output_bit);
  free(UE->phy_sim_rxdataF);
  free(UE->phy_sim_pdsch_llr);
  free(UE->phy_sim_pdsch_rxdataF_ext);
  free(UE->phy_sim_pdsch_rxdataF_comp);
  free(UE->phy_sim_pdsch_dl_ch_estimates);
  free(UE->phy_sim_pdsch_dl_ch_estimates_ext);
  free(UE->phy_sim_dlsch_b);
  free(UE);
  free(nrPHY_vars_UE_g[0]);
  free(nrPHY_vars_UE_g);

  free_nrLDPC_coding_interface(&gNB->nrLDPC_coding_interface);

  if (output_fd)
    fclose(output_fd);

  if (input_fd)
    fclose(input_fd);

  // closing csv file
  if (filename_csv != NULL) { // means we are asked to print stats to CSV
    fclose(csv_file);
    free(filename_csv);
  }

  return ret;
}


void update_ptrs_config(NR_BWP_Downlink_t *bwp, int *rbSize, uint8_t *mcsIndex, int8_t *ptrs_arg)
{
  long ptrsFreqDenst[] = {25, 115};
  long ptrsTimeDenst[] = {2, 4, 10};

  long epre_Ratio = 0;
  long reOffset = 0;

  if(ptrs_arg[0] ==0) {
    ptrsTimeDenst[2]= *mcsIndex -1;
  }
  else if(ptrs_arg[0] == 1) {
    ptrsTimeDenst[1]= *mcsIndex - 1;
    ptrsTimeDenst[2]= *mcsIndex + 1;
  }
  else if(ptrs_arg[0] ==2) {
    ptrsTimeDenst[0]= *mcsIndex - 1;
    ptrsTimeDenst[1]= *mcsIndex + 1;
  }
  else {
    printf("[DLSIM] Wrong L_PTRS value, using default values 1\n");
  }
  /* L = 4 if Imcs < MCS4 */
  if(ptrs_arg[1] ==2) {
    ptrsFreqDenst[0]= *rbSize - 1;
    ptrsFreqDenst[1]= *rbSize + 1;
  }
  else if(ptrs_arg[1] == 4) {
    ptrsFreqDenst[1]= *rbSize - 1;
  }
  else {
    printf("[DLSIM] Wrong K_PTRS value, using default values 2\n");
  }
  printf("[DLSIM] PTRS Enabled with L %d, K %d \n", 1<<ptrs_arg[0], ptrs_arg[1] );
  /* overwrite the values */
  rrc_config_dl_ptrs_params(bwp, ptrsFreqDenst, ptrsTimeDenst, &epre_Ratio, &reOffset);
}

void update_dmrs_config(NR_BWP_Downlink_t *bwp, int8_t* dmrs_arg)
{
  int8_t  mapping_type = typeA;//default value
  int8_t  add_pos = pdsch_dmrs_pos0;//default value
  int8_t  dmrs_config_type = NFAPI_NR_DMRS_TYPE1;//default value

  if(dmrs_arg[0] == 0) {
    mapping_type = typeA;
  }
  else if (dmrs_arg[0] == 1) {
    mapping_type = typeB;
  } else {
    AssertFatal(1==0,"Incorrect Mappingtype, valid options 0-typeA, 1-typeB\n");
  }

  /* Additional DMRS positions 0 ,1 ,2 and 3 */
  if (dmrs_arg[1] >= 0 && dmrs_arg[1] < 4) {
    add_pos = dmrs_arg[1];
  } else {
    AssertFatal(1==0,"Incorrect Additional Position, valid options 0-pos1, 1-pos1, 2-pos2, 3-pos3\n");
  }

  /* DMRS Conf Type 1 or 2 */
  if(dmrs_arg[2] == 1) {
    dmrs_config_type = NFAPI_NR_DMRS_TYPE1;
  } else if(dmrs_arg[2] == 2) {
    dmrs_config_type = NFAPI_NR_DMRS_TYPE2;
  }

  AssertFatal((bwp->bwp_Dedicated->pdsch_Config != NULL && bwp->bwp_Dedicated->pdsch_Config->choice.setup != NULL), "Base RRC reconfig structures are not allocated.\n");

  if(mapping_type == typeA) {
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA));
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->present= NR_SetupRelease_DMRS_DownlinkConfig_PR_setup;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup));
    if (dmrs_config_type == NFAPI_NR_DMRS_TYPE2)
      bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_Type = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_Type));
    else
      bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_Type = NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->maxLength=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->scramblingID0=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->scramblingID1=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->phaseTrackingRS=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_AdditionalPosition = NULL;
    printf("DLSIM: Allocated Mapping TypeA in RRC reconfig message\n");
  }

  if(mapping_type == typeB) {
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB));
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->present= NR_SetupRelease_DMRS_DownlinkConfig_PR_setup;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup));
    if (dmrs_config_type == NFAPI_NR_DMRS_TYPE2)
      bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->dmrs_Type = calloc(1,sizeof(*bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->dmrs_Type));
    else
      bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->dmrs_Type = NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->maxLength=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->scramblingID0=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->scramblingID1=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->phaseTrackingRS=NULL;
    bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup->dmrs_AdditionalPosition = NULL;
    printf("DLSIM: Allocated Mapping TypeB in RRC reconfig message\n");
  }

  struct NR_SetupRelease_DMRS_DownlinkConfig	*dmrs_MappingtypeA = bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA;
  struct NR_SetupRelease_DMRS_DownlinkConfig	*dmrs_MappingtypeB = bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeB;


  NR_DMRS_DownlinkConfig_t *dmrs_config = (mapping_type == typeA) ? dmrs_MappingtypeA->choice.setup : dmrs_MappingtypeB->choice.setup;

  if (add_pos != 2) { // pos0,pos1,pos3
    if (dmrs_config->dmrs_AdditionalPosition == NULL) {
      dmrs_config->dmrs_AdditionalPosition = calloc(1,sizeof(*dmrs_MappingtypeA->choice.setup->dmrs_AdditionalPosition));
    }
    switch (add_pos) {
      case 0:
        *dmrs_config->dmrs_AdditionalPosition = NR_DMRS_DownlinkConfig__dmrs_AdditionalPosition_pos0;
        break;
      case 1:
        *dmrs_config->dmrs_AdditionalPosition = NR_DMRS_DownlinkConfig__dmrs_AdditionalPosition_pos1;
        break;
      case 3:
        *dmrs_config->dmrs_AdditionalPosition = NR_DMRS_DownlinkConfig__dmrs_AdditionalPosition_pos3;
        break;
      default:
        AssertFatal(false, "DMRS additional position %d not valid\n", add_pos);
    }
  } else { // if NULL, Value pos2
    free(dmrs_config->dmrs_AdditionalPosition);
    dmrs_config->dmrs_AdditionalPosition = NULL;
  }

  for (int i=0;i<bwp->bwp_Common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list.count;i++) {
    bwp->bwp_Common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list.array[i]->mappingType = mapping_type;
  }

  printf("[DLSIM] DMRS Config is modified with Mapping Type %d, Additional Positions %d Config. Type %d \n", mapping_type, add_pos, dmrs_config_type);
}
