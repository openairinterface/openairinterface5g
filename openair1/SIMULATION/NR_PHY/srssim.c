/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/utils/nr/nr_common.h"
#define inMicroS(a) (((double)(a)) / (1000.0))
#include "SIMULATION/LTE_PHY/common_sim.h"
#include "common/utils/assertions.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/phy_vars_nr_ue.h"
#include "e1ap_messages_types.h"
#include "executables/nr-uesoftmodem.h"
#include "SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "SIMULATION/TOOLS/sim.h"
#include "common/ran_context.h"
#include "time_meas.h"
#include "SCHED_NR/phy_frame_config_nr.h"

PHY_VARS_gNB *gNB;
PHY_VARS_NR_UE *UE;
RAN_CONTEXT_t RC;
char *uecap_file = NULL;
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];

uint64_t downlink_frequency[MAX_NUM_CCs][4];
uint64_t uplink_frequency[MAX_NUM_CCs][4];
THREAD_STRUCT thread_struct;

// needed for some functions
uint16_t n_rnti = 0x1234;
openair0_config_t openair0_cfg_g[MAX_CARDS] = {};
nrUE_params_t nrUE_params;

nrUE_params_t *get_nrUE_params(void)
{
  return &nrUE_params;
}

channel_desc_t *UE2gNB[MAX_MOBILES_PER_GNB][NUMBER_OF_gNB_MAX];
configmodule_interface_t *uniqCfg = NULL;

NR_IF_Module_t *NR_IF_Module_init(int Mod_id) { return (NULL); }

void e1_bearer_context_setup(const e1ap_bearer_setup_req_t *req)
{
  abort();
}
void e1_bearer_context_modif(const e1ap_bearer_mod_req_t *req)
{
  abort();
}
void e1_bearer_context_mod_confirm(const e1ap_bearer_mod_confirm_t *conf)
{
  abort();
}
void e1_bearer_release_cmd(const e1ap_bearer_release_cmd_t *cmd)
{
  abort();
}

int main(int argc, char *argv[])
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  double SNR, snr0 = 0, snr1 = 20.0;
  double snr_step = 1;
  uint8_t snr1set = 0;
  double **s_re, **s_im, **r_re, **r_im;
  int trial, n_trials = 1, delay = 0;
  double maxDoppler = 0.0;
  uint8_t n_tx = 1, n_rx = 1, n_ports = 0;
  channel_desc_t *UE2gNB;
  uint8_t extended_prefix_flag = 0;
  SCM_t channel_model = AWGN;
  corr_level_t corr_level = CORR_LEVEL_LOW;
  uint16_t N_RB_DL = 106, N_RB_UL = 106, mu = 1;
  int srs_start_symbol = 13;
  int srs_comb_size = 0;
  int srs_comb_offset = 0;
  int srs_cyclic_shift = 0;
  int nb_symb_srs = 0;
  int i;
  int loglvl = OAILOG_WARNING;
  int print_perf = 0;
  int slot = 8;
  int frame = 1;
  double DS_TDL = .03;
  int threequarter_fs = 0;
  uint64_t SSB_positions = 0x01;
  uint16_t Nid_cell = 0;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_SRSSIM] Error, configuration module init failed\n");
  }

  randominit();

  /* initialize the sin-cos table */
  InitSinLUT();

  int c;
  while ((c = getopt(argc, argv, "--:O:a:b:c:d:e:f:g:h:i:kl:m:n:p:q:s:u:y:z:A:B:C:H:PR:S:L:")) != -1) {
    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    printf("handling optarg %c\n", c);
    switch (c) {
      case 'a':
        srs_start_symbol = atoi(optarg);
        AssertFatal(srs_start_symbol >= 8 && srs_start_symbol <= 13, "start_symbol %d is not in 8..13\n", srs_start_symbol);
        break;

      case 'b':
        nb_symb_srs = atoi(optarg);
        AssertFatal(nb_symb_srs == 1 || nb_symb_srs == 2 || nb_symb_srs == 4,
                    "number of srs symbols %d is not 1,2,4\n",
                    nb_symb_srs);
        nb_symb_srs >>= 1; // Value: 0 = 1 symbol, 1 = 2 symbols, 2 = 4 symbols
        break;

      case 'c':
        n_rnti = atoi(optarg);
        AssertFatal(n_rnti > 0 && n_rnti <= 65535, "Illegal n_rnti %x\n", n_rnti);
        break;

      case 'd':
        delay = atoi(optarg);
        break;

      case 'e':
        srs_comb_size = atoi(optarg);
        AssertFatal(srs_comb_size >= 0 && srs_comb_size < 2, "comb_size %d is not 0 or 1\n", srs_comb_size);
        break;

      case 'f':
        srs_comb_offset = atoi(optarg);
        if (srs_comb_size == 0) {
          AssertFatal(srs_comb_offset >= 0 && srs_comb_offset <= 1, "comb_offset %d is not in 0...1\n", srs_comb_offset);
        } else if (srs_comb_size == 1) {
          AssertFatal(srs_comb_offset >= 0 && srs_comb_offset <= 3, "comb_offset %d is not in 0...3\n", srs_comb_offset);
        } else if (srs_comb_size == 2) {
          AssertFatal(srs_comb_offset >= 0 && srs_comb_offset <= 7, "comb_offset %d is not in 0...7\n", srs_comb_offset);
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

        if (optarg[1] == ',') {
          switch (optarg[2]) {
            case 'l':
              corr_level = CORR_LEVEL_LOW;
              break;
            case 'm':
              corr_level = CORR_LEVEL_MEDIUM;
              break;
            case 'h':
              corr_level = CORR_LEVEL_HIGH;
              break;
            default:
              printf("Invalid correlation level!\n");
          }
        }

        if (optarg[3] == ',') {
          maxDoppler = atoi(&optarg[4]);
          printf("Maximum Doppler Frequency: %.0f Hz\n", maxDoppler);
        }
        break;

      case 'i':
        srs_cyclic_shift = atoi(optarg);
        if (srs_comb_size == 0) {
          AssertFatal(srs_cyclic_shift >= 0 && srs_cyclic_shift <= 7, "cyclic_shift %d is not in 0...7\n", srs_cyclic_shift);
        } else if (srs_comb_size == 1) {
          AssertFatal(srs_cyclic_shift >= 0 && srs_cyclic_shift <= 11, "cyclic_shift %d is not in 0...11\n", srs_cyclic_shift);
        } else if (srs_comb_size == 2) {
          AssertFatal(srs_cyclic_shift >= 0 && srs_cyclic_shift <= 5, "cyclic_shift %d is not in 0...5\n", srs_cyclic_shift);
        }
        break;

      case 'k':
        printf("Setting threequarter_fs_flag\n");
        threequarter_fs = 1;
        break;

      case 'n':
        n_trials = atoi(optarg);
        break;

      case 'p':
        extended_prefix_flag = 1;
        break;

      case 'q':
        n_ports = atoi(optarg);
        if (n_ports > 3) {
          printf("Unsupported number of antenna ports %d\n", n_ports);
          exit(-1);
        }
        break;

      case 's':
        snr0 = atof(optarg);
        printf("Setting SNR0 to %f\n", snr0);
        break;

      case 'u':
        mu = atoi(optarg);
        break;

      case 'y':
        n_tx = atoi(optarg);
        if ((n_tx == 0) || (n_tx > 8)) {
          printf("Unsupported number of tx antennas %d\n", n_tx);
          exit(-1);
        }
        break;

      case 'z':
        n_rx = atoi(optarg);
        if ((n_rx == 0) || (n_rx > 8)) {
          printf("Unsupported number of rx antennas %d\n", n_rx);
          exit(-1);
        }
        break;

      case 'H':
        slot = atoi(optarg);
        break;

      case 'R':
        N_RB_DL = atoi(optarg);
        N_RB_UL = N_RB_DL;
        break;

      case 'S':
        snr1 = atof(optarg);
        snr1set = 1;
        printf("Setting SNR1 to %f\n", snr1);
        break;

      case 'P':
        print_perf = 1;
        cpu_meas_enabled = 1;
        break;

      case 'L':
        loglvl = atoi(optarg);
        break;

      default:
      case 'h':
        printf("%s -h(elp)\n", argv[0]);
        printf("-a SRS starting symbol (8 .. 13)\n");
        printf("-b SRS number of symbols (1, 2, 4)\n");
        printf("-c RNTI\n");
        printf("-d Introduce delay in terms of number of samples\n");
        printf("-e SRS comb size, 0 (comb size 2), 1 (comb size 4) \n");
        printf("-f SRS comb offset, 0 .. 1 (comb size 2), 0 .. 3 (comb size 4) \n");
        printf(
            "-g Channel model configuration. Arguments list: Number of arguments = 3, {Channel model: [A] TDLA30, [B] TDLB100, [C] "
            "TDLC300}, {Correlation: [l] Low, [m] Medium, [h] High}, {Maximum Doppler shift} e.g. -g A,l,10\n");
        printf("-h This message\n");
        printf("-i SRS cyclic shift, 0 .. 7 (comb size 2), 0 .. 11 (comb size 4) \n");
        printf("-k 3/4 sampling\n");
        printf("-n Number of trials to simulate\n");
        printf("-p Use extended prefix mode\n");
        printf("-q number of antenna ports, 0 (1 port), 1 (2 ports), 2 (4 ports), 3 (8 ports)\n");
        printf("-s Starting SNR, runs from SNR0 to SNR0 + 10 dB if ending SNR isn't given\n");
        printf("-S Ending SNR, runs from SNR0 to SNR1\n");
        printf("-u Set the numerology\n");
        printf("-y Number of TX antennas used at UE\n");
        printf("-z Number of RX antennas used at gNB\n");
        printf("-H Slot number\n");
        printf("-L <log level, 0(errors), 1(warning), 2(info) 3(debug) 4 (trace)>\n");
        printf("-P Print SRS performances\n");
        printf("-R Maximum number of available resorce blocks (N_RB_DL)\n");
        exit(-1);
        break;
    }
  }

  logInit();
  set_glog(loglvl);

  if ((1 << n_ports) > min(n_tx, n_rx)) {
    printf("Unsupported port configuration\n");
    exit(-1);
  }

  int ret = 1;

  if (snr1set == 0)
    snr1 = snr0 + 10;

  double sampling_rate, tx_bandwidth, rx_bandwidth;
  get_samplerate_and_bw(mu, N_RB_DL, threequarter_fs, &sampling_rate, &tx_bandwidth, &rx_bandwidth);

  // Initialize gNB
  gNB = calloc_or_fail(1, sizeof(PHY_VARS_gNB));
  gNB->ofdm_offset_divisor = UINT_MAX;
  gNB->RU_list[0] = calloc_or_fail(1, sizeof(**gNB->RU_list));
  gNB->RU_list[0]->rfdevice.openair0_cfg = openair0_cfg_g;

  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  fp->N_RB_DL = N_RB_DL;
  fp->N_RB_UL = N_RB_UL;
  fp->Ncp = extended_prefix_flag ? NR_EXTENDED : NR_NORMAL;
  fp->nb_antennas_tx = n_tx;
  fp->nb_antennas_rx = n_rx;
  fp->threequarter_fs = threequarter_fs;
  nr_phy_config_request_sim(gNB, N_RB_UL, N_RB_UL, mu, Nid_cell, SSB_positions);
  printf("dl freq %" PRIu64 " , ul freq %" PRIu64 " \n", fp->dl_CarrierFreq, fp->ul_CarrierFreq);
  do_tdd_config_sim(gNB, mu);
  phy_init_nr_gNB(gNB);

  // Initialize UE
  UE = calloc_or_fail(1, sizeof(PHY_VARS_NR_UE));
  memcpy(&UE->frame_parms, fp, sizeof(NR_DL_FRAME_PARMS)); // setting same frame parameters as gNB

  /* RU handles rxdataF, and gNB just has a pointer. Here, we don't have an RU,
   * so we need to allocate that memory as well. First index in rxdataF[0] index refers to beams*/
  for (i = 0; i < n_rx; i++)
    gNB->common_vars.rxdataF[i] = malloc16_clear(fp->samples_per_frame_wCP * sizeof(int32_t));

  /* no RU: need to have rxdata */
  c16_t **rxdata;
  rxdata = malloc_or_fail(n_rx * sizeof(*rxdata));
  for (i = 0; i < n_rx; i++)
    rxdata[i] = malloc16_clear(fp->samples_per_frame * sizeof(c16_t));

  /* no memory allocated for UE common_vars: need to have txdata and txdataF */
  c16_t **txdata;
  txdata = malloc_or_fail(n_tx * sizeof(*txdata));
  for (i = 0; i < n_tx; i++)
    txdata[i] = malloc16_clear(fp->samples_per_frame * sizeof(c16_t));

  c16_t **txdataF;
  txdataF = malloc_or_fail(n_tx * sizeof(*txdataF));
  for (i = 0; i < n_tx; i++)
    txdataF[i] = malloc16_clear(fp->samples_per_frame_wCP * sizeof(c16_t));

  // Configure channel model
  UE2gNB = new_channel_desc_scm(n_tx,
                                n_rx,
                                channel_model,
                                sampling_rate / 1e6,
                                fp->ul_CarrierFreq,
                                tx_bandwidth,
                                DS_TDL,
                                maxDoppler,
                                corr_level,
                                0,
                                delay,
                                0,
                                0);

  if (UE2gNB == NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  // Allocate memory to intermediate variables to apply channel
  int slot_length = get_samples_per_slot(slot, fp);

  s_re = malloc_or_fail(n_tx * sizeof(double *));
  s_im = malloc_or_fail(n_tx * sizeof(double *));
  r_re = malloc_or_fail(n_rx * sizeof(double *));
  r_im = malloc_or_fail(n_rx * sizeof(double *));

  for (int aatx = 0; aatx < n_tx; aatx++) {
    s_re[aatx] = calloc_or_fail(1, slot_length * sizeof(double));
    s_im[aatx] = calloc_or_fail(1, slot_length * sizeof(double));
  }

  for (int aarx = 0; aarx < n_rx; aarx++) {
    r_re[aarx] = calloc_or_fail(1, slot_length * sizeof(double));
    r_im[aarx] = calloc_or_fail(1, slot_length * sizeof(double));
  }

  //----------- configure SRS ----------------------------
  long locationAndBandwidth = PRBalloc_to_locationandbandwidth(N_RB_DL, 0);

  // Configure SRS parameters at gNB
  const uint16_t m_SRS[64] = {4,   8,   12,  16,  16,  20,  24,  24,  28,  32,  36,  40,  48,  48,  52,  56,
                              60,  64,  72,  72,  76,  80,  88,  96,  96,  104, 112, 120, 120, 120, 128, 128,
                              128, 132, 136, 144, 144, 144, 144, 152, 160, 160, 160, 168, 176, 184, 192, 192,
                              192, 192, 208, 216, 224, 240, 240, 240, 240, 256, 256, 256, 264, 272, 272, 272};
  nfapi_nr_srs_pdu_t srs_pdu = {.rnti = n_rnti,
                                .bwp_size = NRRIV2BW(locationAndBandwidth, 275),
                                .bwp_start = NRRIV2PRBOFFSET(locationAndBandwidth, 275),
                                .subcarrier_spacing = fp->numerology_index,
                                .cyclic_prefix = extended_prefix_flag,
                                .num_ant_ports = n_ports,
                                .num_symbols = nb_symb_srs,
                                .num_repetitions = 0, // Value: 0 = 1, 1 = 2, 2 = 4
                                .time_start_position = srs_start_symbol,
                                .bandwidth_index = 0,
                                .config_index = rrc_get_max_nr_csrs(srs_pdu.bwp_size, srs_pdu.bandwidth_index),
                                .sequence_id = 40,
                                .comb_size = srs_comb_size,
                                .comb_offset = srs_comb_offset,
                                .cyclic_shift = srs_cyclic_shift,
                                .frequency_position = 0,
                                .frequency_shift = 0,
                                .frequency_hopping = 0,
                                .group_or_sequence_hopping = 0,
                                .resource_type = NR_SRS_Resource__resourceType_PR_periodic,
                                .t_srs = 1,
                                .t_offset = 0,
                                .srs_parameters_v4.srs_bandwidth_size = m_SRS[srs_pdu.config_index],
                                .srs_parameters_v4.usage = 1 << NR_SRS_ResourceSet__usage_codebook,
                                .srs_parameters_v4.report_type[0] = 1,
                                .srs_parameters_v4.iq_representation = 1,
                                .srs_parameters_v4.prg_size = 1,
                                .srs_parameters_v4.num_total_ue_antennas = 1 << srs_pdu.num_ant_ports,
                                .srs_parameters_v4.num_ul_spatial_streams_ports = n_rx,
                                .beamforming.num_prgs = m_SRS[srs_pdu.config_index],
                                .beamforming.prg_size = 1};

  NR_gNB_SRS_job_t srs_job = {.frame = frame, .slot = slot, .srs_pdu = srs_pdu};

  // Configure SRS parameters at UE
  fapi_nr_ul_config_srs_pdu srs_config_pdu = {.rnti = srs_pdu.rnti,
                                              .bwp_size = srs_pdu.bwp_size,
                                              .bwp_start = srs_pdu.bwp_start,
                                              .subcarrier_spacing = srs_pdu.subcarrier_spacing,
                                              .cyclic_prefix = srs_pdu.cyclic_prefix,
                                              .num_ant_ports = srs_pdu.num_ant_ports,
                                              .num_symbols = srs_pdu.num_symbols,
                                              .num_repetitions = srs_pdu.num_repetitions,
                                              .time_start_position = srs_pdu.time_start_position,
                                              .bandwidth_index = srs_pdu.bandwidth_index,
                                              .config_index = srs_pdu.config_index,
                                              .sequence_id = srs_pdu.sequence_id,
                                              .comb_size = srs_pdu.comb_size,
                                              .comb_offset = srs_pdu.comb_offset,
                                              .cyclic_shift = srs_pdu.cyclic_shift,
                                              .frequency_position = srs_pdu.frequency_position,
                                              .frequency_shift = srs_pdu.frequency_shift,
                                              .frequency_hopping = srs_pdu.frequency_hopping,
                                              .group_or_sequence_hopping = srs_pdu.group_or_sequence_hopping,
                                              .resource_type = srs_pdu.resource_type,
                                              .t_srs = srs_pdu.t_srs,
                                              .t_offset = srs_pdu.t_offset,
                                              .beamforming.num_prgs = srs_pdu.beamforming.num_prgs,
                                              .beamforming.prg_size = srs_pdu.beamforming.prg_size};

  //----------- UE TX SRS procedures ---------------------

  UE_nr_rxtx_proc_t proc;
  proc.frame_rx = frame;
  proc.frame_tx = frame;
  proc.nr_slot_tx = slot;
  proc.nr_slot_rx = slot;
  bool was_symbol_used[NR_SYMBOLS_PER_SLOT] = {0};
  int slot_offset = get_samples_slot_timestamp(fp, slot);
  uint16_t ofdm_symbol_size = fp->ofdm_symbol_size;
  int slot_offsetF = (slot % RU_RX_SLOT_DEPTH) * fp->symbols_per_slot * ofdm_symbol_size;

  c16_t *txF[n_tx];
  for (i = 0; i < n_tx; i++)
    txF[i] = txdataF[i] + slot_offsetF;

  c16_t *txd[n_tx];
  for (i = 0; i < n_tx; i++)
    txd[i] = txdata[i] + slot_offset;

  ue_srs_procedures_nr(UE, &proc, (c16_t **)txF, &srs_config_pdu, was_symbol_used);

  //------------ TX rotation and OFDM Modulation --------------------------
  nr_tx_rotation_and_ofdm_mod(slot, fp, n_tx, txF, txd, link_type_ul, was_symbol_used, false);

  //----------- Apply propagation channel and add noise ----------------------------

  double ts = 1.0 / (fp->subcarrier_spacing * ofdm_symbol_size);

  for (i = 0; i < slot_length; i++) {
    for (int aa = 0; aa < n_tx; aa++) {
      s_re[aa][i] = (double)txdata[aa][slot_offset + i].r;
      s_im[aa][i] = (double)txdata[aa][slot_offset + i].i;
    }
  }

  // Compute SRS symbol offset and length
  int symbol_offset = slot_offset;
  int abs_first_symbol = slot * fp->symbols_per_slot;
  int idx_sym;
  for (idx_sym = abs_first_symbol; idx_sym < abs_first_symbol + srs_start_symbol; idx_sym++)
    symbol_offset += (idx_sym % (0x7 << fp->numerology_index)) ? fp->nb_prefix_samples : fp->nb_prefix_samples0;

  symbol_offset += ofdm_symbol_size * srs_start_symbol;
  int symbol_length =
      ofdm_symbol_size + ((idx_sym % (0x7 << fp->numerology_index)) ? fp->nb_prefix_samples : fp->nb_prefix_samples0);

  // Compute transmitter energy level
  double txlev_sum = compute_tx_energy_level(txdata, n_tx, symbol_offset, symbol_length, n_trials);

  init_sorted_list_meas(&gNB->rx_srs_stats, n_trials);

  for (SNR = snr0; SNR <= snr1 && !stop; SNR += snr_step) {
    reset_meas(&gNB->rx_srs_stats);
    reset_meas(&gNB->generate_srs_stats);
    reset_meas(&gNB->get_srs_signal_stats);
    reset_meas(&gNB->srs_channel_estimation_stats);
    reset_meas(&gNB->srs_timing_advance_stats);

    double sum_srs_snr = 0;
    int valid_srs_estimates = 0;
    int tao_ns_count = 0;
    for (trial = 0; trial < n_trials && !stop; trial++) {
      // Estimate noise power from the transmitter level and SNR
      int srs_K_TC = 2 << srs_comb_size;
      double sigma = compute_noise_variance(txlev_sum, ofdm_symbol_size, srs_pdu.bwp_size / srs_K_TC, 1 << n_ports, SNR, n_trials);

      //----------- Apply propagation channel ----------------------------
      multipath_channel(UE2gNB, s_re, s_im, r_re, r_im, slot_length, 0, (n_trials == 1) ? 1 : 0);

      //----------- Add noise ----------------------------
      add_noise(rxdata,
                (const double **)r_re,
                (const double **)r_im,
                sigma, // noise power
                slot_length,
                slot_offset,
                ts,
                0, // delay
                0, // pdu_bit_map
                0, // PTRS_BITMAP,
                n_rx);

      //----------- OFDM Demodulation and RX rotation--------------------------
      nr_ofdm_demod_and_rx_rotation(rxdata, gNB->common_vars.rxdataF, fp, n_rx, slot, slot_offsetF, link_type_ul, was_symbol_used);

      //----------- UE RX SRS procedures ---------------------

      start_meas(&gNB->rx_srs_stats);
      uint8_t N_symb_SRS = 1 << srs_pdu.num_symbols;
      uint8_t N_ap = 1 << srs_pdu.num_ant_ports;
      int16_t snr_per_rb[srs_pdu.bwp_size];
      uint16_t timing_advance_offset;
      int16_t timing_advance_offset_nsec[n_rx];
      int srs_est;
      c16_t srs_estimated_channel_freq[n_rx][N_ap][ofdm_symbol_size * N_symb_SRS] __attribute__((aligned(32)));

      int8_t snr;
      nr_srs_rx_procedures(gNB,
                           frame,
                           slot,
                           n_rx,
                           N_ap,
                           N_symb_SRS,
                           ofdm_symbol_size,
                           &srs_job,
                           &srs_est,
                           &snr,
                           srs_estimated_channel_freq,
                           snr_per_rb,
                           &timing_advance_offset,
                           timing_advance_offset_nsec);

      if (srs_est < 0) {
        stop_meas(&gNB->rx_srs_stats);
        continue;
      }

      valid_srs_estimates++;
      sum_srs_snr += pow(10, (double)snr / 10.0);

      int16_t delay_ns = delay * 1e9 / (fp->samples_per_frame * 100);
      for (int ant_idx = 0; ant_idx < n_rx; ant_idx++) {
        if (n_trials == 1)
          printf("[RX ant %d] SRS estimated: TA offset %d, TA offset ns %d \n",
                 ant_idx,
                 timing_advance_offset,
                 timing_advance_offset_nsec[ant_idx]);
        if (timing_advance_offset_nsec[ant_idx] == delay_ns)
          tao_ns_count++;
      }
      stop_meas(&gNB->rx_srs_stats);
    } // trail loop
    float tao_ns_rate = (float)tao_ns_count / (n_trials * n_rx);
    float SRS_SNR_dB = NAN;
    if (valid_srs_estimates > 0) {
      SRS_SNR_dB = 10 * log10(sum_srs_snr / valid_srs_estimates);
      printf("Actual SNR : %f, Estimated SNR from SRS %f (dB), TA offset success rate %f %%\n", SNR, SRS_SNR_dB, tao_ns_rate * 100);
    } else {
      printf("Actual SNR : %f, Estimated SNR from SRS unavailable (no valid estimates), TA offset success rate %f %%\n",
             SNR,
             tao_ns_rate * 100);
    }
    printf("Valid SRS estimates: %d/%d\n", valid_srs_estimates, trial);

    if (print_perf == 1) {
      printf("\ngNB RX\n");
      printDistribution(&gNB->rx_srs_stats, "RX SRS time");
      printStatIndent(&gNB->generate_srs_stats, "Generate SRS sequence time");
      printStatIndent(&gNB->get_srs_signal_stats, "Get SRS signal time");
      printStatIndent(&gNB->srs_channel_estimation_stats, "SRS channel estimation time");
      printStatIndent(&gNB->srs_timing_advance_stats, "SRS timing advance estimation time");
      printf("\n");
    }

    int srs_ret = 1;
    if (valid_srs_estimates > 0) {
      if (SNR > 30 && SRS_SNR_dB > 30) {
        srs_ret = 0;
      } else if (SNR >= SRS_SNR_dB) {
        srs_ret = SRS_SNR_dB >= 0.7 * SNR ? 0 : 1;
      } else if (SRS_SNR_dB > SNR) {
        srs_ret = SNR >= 0.7 * SRS_SNR_dB ? 0 : 1;
      }
    }

    if (tao_ns_rate > 0.9 && srs_ret == 0) {
      ret = 0;
      break;
    }

  } // SNR loop

  printf("*************\n");
  printf("SRS test %s\n", ret == 0 ? "OK" : "FAILED");
  printf("*************\n");

  // free memory

  free_sorted_list_meas(&gNB->rx_srs_stats);
  for (i = 0; i < n_tx; i++) {
    free(s_re[i]);
    free(s_im[i]);
    free(txdata[i]);
    free(txdataF[i]);
  }
  free(s_re);
  free(s_im);
  free(txdata);
  free(txdataF);

  for (i = 0; i < n_rx; i++) {
    free(r_re[i]);
    free(r_im[i]);
    free(rxdata[i]);
    free(gNB->common_vars.rxdataF[i]);
  }

  free(r_re);
  free(r_im);
  free(rxdata);

  phy_free_nr_gNB(gNB);
  free_channel_desc_scm(UE2gNB);
  free(gNB->RU_list[0]);
  free(UE);

  return ret;
}
