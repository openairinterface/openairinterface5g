/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/utils/nr/nr_common.h"
#include "SIMULATION/LTE_PHY/common_sim.h"
#include "common/utils/assertions.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_UE_ESTIMATION/nr_estimation.h"
#include "executables/nr-uesoftmodem.h"
#include "SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "SIMULATION/TOOLS/sim.h"
#include "common/ran_context.h"
#include "time_meas.h"
#include "SCHED_NR/phy_frame_config_nr.h"
#include "SCHED_NR/sched_nr.h"
#include "NR_PHY_INTERFACE/NR_IF_Module.h"
#include "nfapi/open-nFAPI/fapi/inc/nr_fapi_p5_utils.h"

#define MAX_PRS_GNBS 4
#define MAX_PRS_RSC 8

RAN_CONTEXT_t RC;
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];

uint64_t downlink_frequency[MAX_NUM_CCs][4];
THREAD_STRUCT thread_struct;

// needed for some functions
openair0_config_t openair0_cfg_g[MAX_CARDS] = {};
nrUE_params_t nrUE_params;

nrUE_params_t *get_nrUE_params(void)
{
  return &nrUE_params;
}

configmodule_interface_t *uniqCfg = NULL;

NR_IF_Module_t *NR_IF_Module_init(int Mod_id)
{
  return (NULL);
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
  int trial, n_trials = 1;
  double maxDoppler = 0.0;
  uint8_t n_tx = 1, n_rx = 1;
  int n_gnb = 1;
  int n_rsc = 1;
  bool share_comb_offset = false;
  uint8_t extended_prefix_flag = 0;
  SCM_t channel_model = AWGN;
  corr_level_t corr_level = CORR_LEVEL_LOW;
  uint16_t N_RB_DL = 106, N_RB_UL = 106, mu = 1;
  int prs_start_symbol = 7;
  int prs_num_symbols = 4;
  int prs_comb_size = 4;
  int prs_re_offset = 0;
  int prs_num_rb = 0; // 0: whole carrier
  int prs_rb_offset = 0;
  int prs_id = 0;
  int i;
  PHY_VARS_gNB *gNB;
  PHY_VARS_NR_UE *UE;
  int loglvl = OAILOG_WARNING;
  int print_perf = 0;
  int slot = 2;
  int frame = 1;
  double DS_TDL = .03;
  int threequarter_fs = 0;
  uint64_t SSB_positions = 0x01;
  uint16_t Nid_cell = 0;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_PRSSIM] Error, configuration module init failed\n");
  }

  randominit();

  /* initialize the sin-cos table */
  InitSinLUT();

  int c;
  while ((c = getopt(argc, argv, "--:O:a:b:ce:f:g:hkn:o:ps:r:u:z:G:H:L:M:N:PR:S:")) != -1) {
    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    switch (c) {
      case 'a':
        prs_start_symbol = atoi(optarg);
        AssertFatal(prs_start_symbol >= 0 && prs_start_symbol <= 12, "start_symbol %d is not in 0..12\n", prs_start_symbol);
        break;

      case 'b':
        prs_num_symbols = atoi(optarg);
        AssertFatal(prs_num_symbols == 2 || prs_num_symbols == 4 || prs_num_symbols == 6 || prs_num_symbols == 12,
                    "number of PRS symbols %d is not 2,4,6,12\n",
                    prs_num_symbols);
        break;

      case 'c':
        share_comb_offset = true;
        break;

      case 'e':
        prs_comb_size = atoi(optarg);
        AssertFatal(prs_comb_size == 2 || prs_comb_size == 4,
                    "comb_size %d is not 2 or 4 (only ones supported by the UE)\n",
                    prs_comb_size);
        break;

      case 'f':
        prs_re_offset = atoi(optarg);
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

        if (strlen(optarg) > 2 && optarg[1] == ',') {
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
              exit(-1);
          }
        }

        if (strlen(optarg) > 3 && optarg[3] == ',') {
          maxDoppler = atoi(&optarg[4]);
          printf("Maximum Doppler Frequency: %.0f Hz\n", maxDoppler);
        }
        break;

      case 'k':
        printf("Setting threequarter_fs_flag\n");
        threequarter_fs = 1;
        break;

      case 'n':
        n_trials = atoi(optarg);
        break;

      case 'o':
        prs_rb_offset = atoi(optarg);
        break;

      case 'p':
        extended_prefix_flag = 1;
        break;

      case 'r':
        prs_num_rb = atoi(optarg);
        AssertFatal(prs_num_rb >= 24 && prs_num_rb <= 272 && prs_num_rb % 4 == 0,
                    "number of PRS RBs %d is not a multiple of 4 in 24..272\n",
                    prs_num_rb);
        break;

      case 's':
        snr0 = atof(optarg);
        printf("Setting SNR0 to %f\n", snr0);
        break;

      case 'u':
        mu = atoi(optarg);
        break;

      case 'z':
        n_rx = atoi(optarg);
        if ((n_rx == 0) || (n_rx > 4)) {
          printf("Unsupported number of rx antennas %d\n", n_rx);
          exit(-1);
        }
        break;

      case 'G':
        n_gnb = atoi(optarg);
        AssertFatal(n_gnb >= 1 && n_gnb <= MAX_PRS_GNBS, "number of gNBs %d is not in 1..%d\n", n_gnb, MAX_PRS_GNBS);
        break;
      case 'H':
        slot = atoi(optarg);
        break;
      case 'M':
        n_rsc = atoi(optarg);
        AssertFatal(n_rsc >= 1 && n_rsc <= MAX_PRS_RSC, "number of PRS resources %d is not in 1..%d\n", n_rsc, MAX_PRS_RSC);
        break;

      case 'N':
        prs_id = atoi(optarg);
        AssertFatal(prs_id >= 0 && prs_id <= 4095, "PRS sequence id %d is not in 0..4095\n", prs_id);
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

      case 'h':
      default:
        if (c != 'h')
          printf("Unknown option -%c\n", optopt);
        printf("%s -h(elp)\n", argv[0]);
        printf("-a PRS starting symbol (0 .. 12)\n");
        printf("-b PRS number of symbols (2, 4, 6, 12)\n");
        printf("-c gNBs share the RE offset and differ only by the PRS sequence id\n");
        printf("-e PRS comb size (2, 4)\n");
        printf("-f PRS RE offset, 0 .. comb size - 1\n");
        printf(
            "-g Channel model configuration. Arguments list: Number of arguments = 3, {Channel model: [A] TDLA30, [B] TDLB100, [C] "
            "TDLC300}, {Correlation: [l] Low, [m] Medium, [h] High}, {Maximum Doppler shift} e.g. -g A,l,10\n");
        printf("-G Number of gNBs transmitting PRS in the same slots\n");
        printf("-h This message\n");
        printf("-H Slot number of the first PRS resource\n");
        printf("-k 3/4 sampling\n");
        printf("-L <log level, 0(errors), 1(warning), 2(info) 3(debug) 4 (trace)>\n");
        printf("-M Number of PRS resources per gNB\n");
        printf("-n Number of trials to simulate\n");
        printf("-N PRS sequence id, 0 .. 4095\n");
        printf("-o PRS RB offset\n");
        printf("-p Use extended prefix mode\n");
        printf("-P Print PRS performances\n");
        printf("-r PRS number of RBs (multiple of 4, 24 .. 272), default is N_RB_DL rounded down to a multiple of 4\n");
        printf("-R Maximum number of available resorce blocks (N_RB_DL)\n");
        printf("-s Starting SNR, runs from SNR0 to SNR0 + 10 dB if ending SNR isn't given\n");
        printf("-S Ending SNR, runs from SNR0 to SNR1\n");
        printf("-u Set the numerology\n");
        printf("-z Number of RX antennas used at UE\n");
        exit(-1);
        break;
    }
  }

  logInit();
  set_glog(loglvl);

  int ret = 1;

  if (snr1set == 0)
    snr1 = snr0 + 10;

  if (prs_num_rb == 0)
    prs_num_rb = N_RB_DL & ~3;
  AssertFatal(prs_rb_offset + prs_num_rb <= N_RB_DL,
              "PRS allocation %d+%d exceeds N_RB_DL %d\n",
              prs_rb_offset,
              prs_num_rb,
              N_RB_DL);
  AssertFatal(prs_start_symbol + prs_num_symbols <= NR_SYMBOLS_PER_SLOT,
              "PRS symbols %d+%d do not fit in a slot\n",
              prs_start_symbol,
              prs_num_symbols);
  AssertFatal(prs_re_offset >= 0 && prs_re_offset < prs_comb_size,
              "RE offset %d is not in 0..%d\n",
              prs_re_offset,
              prs_comb_size - 1);
  AssertFatal(share_comb_offset || n_gnb <= prs_comb_size,
              "%d gNBs cannot have different RE offsets with comb size %d\n",
              n_gnb,
              prs_comb_size);
  AssertFatal(prs_num_symbols >= prs_comb_size, "%d PRS symbols cannot cover comb size %d\n", prs_num_symbols, prs_comb_size);

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
  gNB->phase_comp = true;
  phy_init_nr_gNB(gNB);

  // Initialize UE
  UE = calloc_or_fail(1, sizeof(PHY_VARS_NR_UE));
  memcpy(&UE->frame_parms, fp, sizeof(NR_DL_FRAME_PARMS)); // setting same frame parameters as gNB
  UE->is_synchronized = 1;
  UE->prs_active_gNBs = n_gnb;
  if (init_nr_ue_signal(UE, 1) != 0) {
    printf("Error at UE NR initialisation\n");
    exit(-1);
  }
  c16_t **rxdata = UE->common_vars.rxdata;

  // one transmit waveform per gNB
  c16_t **txdata[MAX_PRS_GNBS];
  for (int g = 0; g < n_gnb; g++) {
    txdata[g] = malloc_or_fail(n_tx * sizeof(**txdata));
    for (i = 0; i < n_tx; i++)
      txdata[g][i] = malloc16_clear(fp->samples_per_frame * sizeof(c16_t));
  }

  // the DFT needs a 32-byte aligned output buffer
  c16_t(*rxdataF)[fp->samples_per_slot_wCP] = aligned_alloc(32, n_rx * sizeof(*rxdataF));
  AssertFatal(rxdataF != NULL, "aligned_alloc failed\n");
  memset(rxdataF, 0, n_rx * sizeof(*rxdataF));

  // Configure channel models
  // The delay of each gNB is random in [0, ofdm_symbol_size / 64] samples
  const int delay_max = fp->ofdm_symbol_size / 64;
  int delay[MAX_PRS_GNBS];
  channel_desc_t *chan[MAX_PRS_GNBS];
  for (int g = 0; g < n_gnb; g++) {
    delay[g] = (int)(uniformrandom() * (delay_max + 1));
    printf("gNB %d: delay %d samples\n", g, delay[g]);
    chan[g] = new_channel_desc_scm(n_tx,
                                   n_rx,
                                   channel_model,
                                   sampling_rate / 1e6,
                                   fp->dl_CarrierFreq,
                                   tx_bandwidth,
                                   DS_TDL,
                                   maxDoppler,
                                   corr_level,
                                   0,
                                   delay[g],
                                   0,
                                   0);
    if (chan[g] == NULL) {
      printf("Problem generating channel model. Exiting.\n");
      exit(-1);
    }
  }

  // Allocate memory to intermediate variables to apply channel
  int slot_length = 0;
  for (int r = 0; r < n_rsc; r++) {
    AssertFatal(nr_slot_select(&gNB->gNB_config, frame, slot + r) != NR_UPLINK_SLOT, "slot %d is an uplink slot\n", slot + r);
    slot_length = max(slot_length, get_samples_per_slot(slot + r, fp));
  }
  double **acc_re = malloc_or_fail(n_rx * sizeof(double *));
  double **acc_im = malloc_or_fail(n_rx * sizeof(double *));
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
    acc_re[aarx] = calloc_or_fail(1, slot_length * sizeof(double));
    acc_im[aarx] = calloc_or_fail(1, slot_length * sizeof(double));
  }

  //----------- configure PRS ----------------------------
  // resource r of gNB g is sent in slot (slot + r) with RE offset (prs_re_offset + g) and sequence id (prs_id + g * n_rsc + r),
  // all gNBs use the same RE offset when share_comb_offset is set
  nfapi_nr_dl_tti_prs_pdu_rel16_t prs_pdu[MAX_PRS_GNBS][MAX_PRS_RSC];
  fapi_nr_dl_config_prs_pdu_rel16_t prs_config_pdu[MAX_PRS_GNBS][MAX_PRS_RSC];
  prs_meas_t **prs_meas[MAX_PRS_GNBS][MAX_PRS_RSC];
  for (int g = 0; g < n_gnb; g++) {
    UE->prs_vars[g]->NumPRSResources = n_rsc;
    for (int r = 0; r < n_rsc; r++) {
      // gNB side: FAPI DL_TTI.request PRS PDU
      prs_pdu[g][r] = (nfapi_nr_dl_tti_prs_pdu_rel16_t){.subcarrierSpacing = mu,
                                                        .cyclicPrefix = extended_prefix_flag,
                                                        .nIdPrs = (prs_id + g * n_rsc + r) % 4096,
                                                        .combSize = prs_comb_size,
                                                        .combOffset = (prs_re_offset + (share_comb_offset ? 0 : g)) % prs_comb_size,
                                                        .numSymbols = prs_num_symbols,
                                                        .firstSymbol = prs_start_symbol,
                                                        .numRbs = prs_num_rb,
                                                        .startRb = prs_rb_offset,
                                                        .symbolPuncturingBitmap = 0x3FFF};
      // UE side: FAPI DL_CONFIG PRS PDU
      prs_config_pdu[g][r] = (fapi_nr_dl_config_prs_pdu_rel16_t){.subcarrier_spacing = prs_pdu[g][r].subcarrierSpacing,
                                                                 .cyclic_prefix = prs_pdu[g][r].cyclicPrefix,
                                                                 .nid_prs = prs_pdu[g][r].nIdPrs,
                                                                 .comb_size = prs_pdu[g][r].combSize,
                                                                 .comb_offset = prs_pdu[g][r].combOffset,
                                                                 .num_symbols = prs_pdu[g][r].numSymbols,
                                                                 .first_symbol = prs_pdu[g][r].firstSymbol,
                                                                 .num_rbs = prs_pdu[g][r].numRbs,
                                                                 .start_rb = prs_pdu[g][r].startRb};
      prs_meas[g][r] = UE->prs_vars[g]->prs_resource[r].prs_meas;
    }
  }

  //----------- gNB TX PRS procedures ---------------------
  bool was_symbol_used[NR_SYMBOLS_PER_SLOT] = {0};
  for (i = prs_start_symbol; i < prs_start_symbol + prs_num_symbols; i++)
    was_symbol_used[i] = true;
  int ofdm_symbol_size = fp->ofdm_symbol_size;
  double ts = 1.0 / (fp->subcarrier_spacing * ofdm_symbol_size);

  // the scheduled response is expected to be 32-byte aligned
  NR_Sched_Rsp_t *sched = aligned_alloc(32, (sizeof(*sched) + 31) & ~31UL);
  AssertFatal(sched != NULL, "aligned_alloc failed\n");

  double txlev_sum[MAX_PRS_RSC];
  for (int r = 0; r < n_rsc; r++) {
    const int slot_r = slot + r;
    const int slot_offset = get_samples_slot_timestamp(fp, slot_r);
    for (int g = 0; g < n_gnb; g++) {
      memset(sched, 0, sizeof(*sched));
      nfapi_nr_dl_tti_request_body_t *dl_req = &sched->DL_req.dl_tti_request_body;
      dl_req->nPDUs = 1;
      dl_req->dl_tti_pdu_list[0].PDUType = NFAPI_NR_DL_TTI_PRS_PDU_TYPE;
      dl_req->dl_tti_pdu_list[0].PDUSize = sizeof(dl_req->dl_tti_pdu_list[0]);
      dl_req->dl_tti_pdu_list[0].prs_pdu.prs_pdu_rel16 = prs_pdu[g][r];
      phy_procedures_gNB_TX(gNB, &sched->DL_req, &sched->TX_req, &sched->UL_dci_req, frame, slot_r);

      c16_t *txd[n_tx];
      for (i = 0; i < n_tx; i++)
        txd[i] = txdata[g][i] + slot_offset;
      nr_tx_rotation_and_ofdm_mod(slot_r, fp, n_tx, gNB->common_vars.txdataF, txd, link_type_dl, was_symbol_used, true);
    }
    // Compute offset of the first PRS symbol
    int symbol_offset = slot_offset;
    int abs_first_symbol = slot_r * fp->symbols_per_slot;
    for (int idx_sym = abs_first_symbol; idx_sym < abs_first_symbol + prs_start_symbol; idx_sym++)
      symbol_offset += (idx_sym % (0x7 << fp->numerology_index)) ? fp->nb_prefix_samples : fp->nb_prefix_samples0;
    symbol_offset += ofdm_symbol_size * prs_start_symbol;
    // Compute transmitter energy level, the SNR is defined per gNB
    txlev_sum[r] = compute_tx_energy_level(txdata[0], n_tx, symbol_offset, ofdm_symbol_size, n_trials);
  }

  time_stats_t prs_rx_stats = {0};
  reset_meas(&prs_rx_stats);

  ret = 0;
  for (SNR = snr0; SNR <= snr1 && !stop; SNR += snr_step) {
    reset_meas(&prs_rx_stats);
    int toa_count[MAX_PRS_GNBS][MAX_PRS_RSC] = {0};
    for (trial = 0; trial < n_trials && !stop; trial++) {
      for (int r = 0; r < n_rsc; r++) {
        const int slot_r = slot + r;
        const int slot_offset = get_samples_slot_timestamp(fp, slot_r);
        const int slot_len_r = get_samples_per_slot(slot_r, fp);
        // Estimate noise power from the transmitter level and SNR
        double sigma = compute_noise_variance(txlev_sum[r], ofdm_symbol_size, prs_num_rb, 1, SNR, n_trials);

        //----------- Apply the propagation channel of each gNB and add the signals ----------------------------
        for (int aarx = 0; aarx < n_rx; aarx++) {
          memset(acc_re[aarx], 0, slot_len_r * sizeof(double));
          memset(acc_im[aarx], 0, slot_len_r * sizeof(double));
        }
        for (int g = 0; g < n_gnb; g++) {
          for (i = 0; i < slot_len_r; i++) {
            for (int aa = 0; aa < n_tx; aa++) {
              s_re[aa][i] = (double)txdata[g][aa][slot_offset + i].r;
              s_im[aa][i] = (double)txdata[g][aa][slot_offset + i].i;
            }
          }
          multipath_channel(chan[g], s_re, s_im, r_re, r_im, slot_len_r, 0, (n_trials == 1) ? 1 : 0);
          for (int aarx = 0; aarx < n_rx; aarx++) {
            for (i = 0; i < slot_len_r; i++) {
              acc_re[aarx][i] += r_re[aarx][i];
              acc_im[aarx][i] += r_im[aarx][i];
            }
          }
        }

        //----------- Add noise ----------------------------
        add_noise(rxdata,
                  (const double **)acc_re,
                  (const double **)acc_im,
                  sigma, // noise power
                  slot_len_r,
                  slot_offset,
                  ts,
                  0, // delay
                  0, // pdu_bit_map
                  0, // PTRS_BITMAP,
                  n_rx);

        //----------- UE RX PRS procedures ---------------------
        UE_nr_rxtx_proc_t proc = {.frame_rx = frame, .frame_tx = frame, .nr_slot_rx = slot_r, .nr_slot_tx = slot_r};
        start_meas(&prs_rx_stats);
        for (int l = prs_start_symbol; l < prs_start_symbol + prs_num_symbols; l++)
          nr_slot_fep(UE, fp, slot_r, l, rxdataF, link_type_dl, 0, UE->common_vars.rxdata);
        for (int g = 0; g < n_gnb; g++) {
          for (i = 0; i < n_rx; i++)
            prs_meas[g][r][i]->next_dl_toa = prs_meas[g][r][i]->dl_toa;
          nr_prs_channel_estimation(g, r, slot_r, &prs_config_pdu[g][r], UE, &proc, rxdataF);
        }
        stop_meas(&prs_rx_stats);

        for (int g = 0; g < n_gnb; g++) {
          for (int ant_idx = 0; ant_idx < n_rx; ant_idx++) {
            float toa = prs_meas[g][r][ant_idx]->dl_toa[0];
            if (n_trials == 1)
              printf("[gNB %d][rsc %d][RX ant %d] PRS estimated: ToA %.1f samples\n", g, r, ant_idx, toa);
            if (toa == delay[g])
              toa_count[g][r]++;
          }
        }
      } // resource (slot) loop
    } // trial loop

    for (int g = 0; g < n_gnb; g++) {
      for (int r = 0; r < n_rsc; r++) {
        float toa_rate = (float)toa_count[g][r] / (n_trials * n_rx);
        printf("Actual SNR : %f, gNB %d, rsc %d, ToA success rate %f %%\n", SNR, g, r, toa_rate * 100);
        if (toa_rate <= 0.9)
          ret = 1;
      }
    }
    if (print_perf == 1) {
      printf("\nUE RX\n");
      printStatIndent(&prs_rx_stats, "RX PRS time (FEP + channel estimation)");
      printf("\n");
    }
  } // SNR loop

  printf("*************\n");
  printf("PRS test %s\n", ret == 0 ? "OK" : "FAILED");
  printf("*************\n");

  // free memory
  for (i = 0; i < n_tx; i++) {
    free(s_re[i]);
    free(s_im[i]);
  }
  for (int g = 0; g < n_gnb; g++) {
    for (i = 0; i < n_tx; i++)
      free(txdata[g][i]);
    free(txdata[g]);
  }
  free(s_re);
  free(s_im);
  free(sched);

  for (i = 0; i < n_rx; i++) {
    free(r_re[i]);
    free(r_im[i]);
    free(acc_re[i]);
    free(acc_im[i]);
  }
  free(acc_re);
  free(acc_im);
  free(r_re);
  free(r_im);
  free(rxdataF);
  for (int g = 0; g < n_gnb; g++) {
    free_channel_desc_scm(chan[g]);
  }

  free_config_request(&gNB->gNB_config);

  phy_free_nr_gNB(gNB);
  free(gNB->RU_list[0]);
  free(gNB);
  term_nr_ue_signal(UE);
  free(UE);

  return ret;
}
