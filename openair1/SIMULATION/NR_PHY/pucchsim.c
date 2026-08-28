/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#include "common/utils/nr/nr_common.h"
#include "common/utils/LOG/log.h"
#include "common/ran_context.h"
#include "PHY/types.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/defs_gNB.h"
#include "PHY/MODULATION/modulation_eNB.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include "SCHED_NR/sched_nr.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "openair1/SIMULATION/RF/rf.h"
#include "openair1/SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "executables/nr-uesoftmodem.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "executables/softmodem-common.h"
#include "openair1/PHY/phy_vars_nr_ue.h"
#include "PHY/nr_phy_common/inc/nr_sequences_tables.h"
#include "SIMULATION/LTE_PHY/common_sim.h"

THREAD_STRUCT thread_struct;
PHY_VARS_gNB *gNB;
PHY_VARS_NR_UE *UE;
RAN_CONTEXT_t RC;
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
uint64_t downlink_frequency[MAX_NUM_CCs][4];

uint8_t const nr_rv_round_map[4] = {0, 2, 3, 1};
// needed for some functions
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}

void init_downlink_harq_status(NR_DL_UE_HARQ_t *dl_harq)
{
}
NR_IF_Module_t *NR_IF_Module_init(int Mod_id)
{
  return (NULL);
}
nfapi_mode_t nfapi_getmode(void)
{
  return NFAPI_MODE_UNKNOWN;
}

void inc_ref_sched_response(int _)
{
  LOG_E(PHY, "fatal\n");
  exit(1);
}
void deref_sched_response(int _)
{
  LOG_E(PHY, "fatal\n");
  exit(1);
}

nrUE_params_t nrUE_params = {0};

nrUE_params_t *get_nrUE_params(void)
{
  return &nrUE_params;
}

configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv)
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);
  PHY_VARS_NR_UE *uedata_ptr = calloc(1, sizeof(PHY_VARS_NR_UE));
  PHY_VARS_NR_UE **uedata_ptrr = &uedata_ptr;
  nrPHY_vars_UE_g = &uedata_ptrr;

  int i;
  double SNR, snr0 = -2.0, snr1 = 2.0;
  double cfo = 0;
  uint8_t snr1set = 0;
  double **s_re, **s_im, **r_re, **r_im;
  FILE *output_fd = NULL;
  int trial, n_trials = 100, ack_nack_errors = 0, sr_errors = 0;
  int ret = 1;
  uint8_t transmission_mode = 1, n_tx = 1, n_rx = 1;
  uint16_t Nid_cell = 0;
  uint64_t SSB_positions = 0x01;
  channel_desc_t *UE2gNB;
  int format = 0;
  bool format0_freq_hop = false;
  FILE *input_fd = NULL;
  int16_t amp = 0x7FFF;
  int nr_slot_tx = 0;
  int nr_frame_tx = 0;
  uint64_t actual_payload = 0;
  bool random_payload = true;
  int nr_bit = 1; // maximum value possible is 2
  uint8_t m0 = 0; // higher layer paramater initial cyclic shift
  uint8_t nrofSymbols = 1; // number of OFDM symbols can be 1-2 for format 1
  // resource allocated see 9.2.1, 38.213 for more info.should be actually present in the resource set provided
  uint8_t startingSymbolIndex = 0;
  uint16_t startingPRB = 0;
  // PRB number not sure see 9.2.1, 38.213 for more info. Should be actually present in the resource set provided
  uint16_t startingPRB_intraSlotHopping = 0;
  uint16_t nrofPRB = 2;
  uint8_t timeDomainOCC = 0;
  SCM_t channel_model = AWGN; // Rayleigh1_anticorr;

  double DS_TDL = .03;

  double delay_us = 0;
  int N_RB_DL = 273, mu = 1;
  float target_error_rate = 0.001;
  int frame_length_complex_samples;
  NR_DL_FRAME_PARMS *frame_parms;
  int loglvl = OAILOG_WARNING;
  int sr_flag = 0;
  int pucch_DTX_thres = 0;
  bool print_perf = false;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_PUCCHSIM] Error, configuration module init failed\n");
  }

  randominit();
  logInit();

  int c;
  int nrofSymbols_set = 0;
  while ((c = getopt(argc, argv, "--:O:f:hA:f:g:i:I:P:B:b:t:T:m:n:r:o:s:S:x:y:z:N:F:GR:IL:q:cd:CH")) != -1) {
    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    printf("handling optarg %c\n", c);
    switch (c) {
      case 'f':
        // write_output_file=1;
        output_fd = fopen(optarg, "w");

        if (output_fd == NULL) {
          printf("Error opening %s\n", optarg);
          exit(-1);
        }
        break;

      case 'g':
        switch ((char)*optarg) {
          case 'A':
            channel_model = SCM_A;
            break;

          case 'B':
            channel_model = SCM_B;
            break;

          case 'C':
            channel_model = SCM_C;
            break;

          case 'D':
            channel_model = SCM_D;
            break;

          case 'E':
            channel_model = EPA;
            break;

          case 'F':
            channel_model = EVA;
            break;

          case 'G':
            channel_model = ETU;
            break;
          case 'H':
            channel_model = TDL_C;
            DS_TDL = .030; // 30 ns
            break;

          case 'I':
            channel_model = TDL_C;
            DS_TDL = .3; // 300ns
            break;

          case 'J':
            channel_model = TDL_D;
            DS_TDL = .03;
            break;

          default:
            printf("Unsupported channel model!\n");
            exit(-1);
        }
        break;

      case 'n':
        n_trials = atoi(optarg);
        break;

      case 'o':
        cfo = atof(optarg);
        printf("Setting CFO to %f Hz\n", cfo);
        break;

      case 's':
        snr0 = atof(optarg);
        printf("Setting SNR0 to %f\n", snr0);
        break;

      case 'S':
        snr1 = atof(optarg);
        snr1set = 1;
        printf("Setting SNR1 to %f\n", snr1);
        break;

      case 't':
        pucch_DTX_thres = atoi(optarg);
        break;

      case 'd':
        delay_us = atof(optarg);
        break;
      case 'x':
        transmission_mode = atoi(optarg);

        if ((transmission_mode != 1) && (transmission_mode != 2) && (transmission_mode != 6)) {
          printf("Unsupported transmission mode %d\n", transmission_mode);
          exit(-1);
        }
        break;

      case 'y':
        n_tx = atoi(optarg);

        if ((n_tx == 0) || (n_tx > 2)) {
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

      case 'N':
        Nid_cell = atoi(optarg);
        break;

      case 'R':
        N_RB_DL = atoi(optarg);
        break;

      case 'F':
        input_fd = fopen(optarg, "r");

        if (input_fd == NULL) {
          printf("Problem with filename %s\n", optarg);
          exit(-1);
        }
        break;

      case 'L':
        loglvl = atoi(optarg);
        break;
      case 'H':
        format0_freq_hop = true;
        break;
      case 'i':
        nrofSymbols = (uint8_t)atoi(optarg);
        nrofSymbols_set = 1;
        break;
      case 'I':
        startingSymbolIndex = (uint8_t)atoi(optarg);
        break;
      case 'r':
        startingPRB = atoi(optarg);
        break;
      case 'q':
        nrofPRB = atoi(optarg);
        break;
      case 'P':
        format = atoi(optarg);
        if ((format == 1 || format == 3) && nrofSymbols_set == 0)
          nrofSymbols = 14;
        break;
      case 'm':
        m0 = atoi(optarg);
        break;
      case 'b':
        nr_bit = atoi(optarg);
        break;
      case 'c':
        sr_flag = 1;
        break;
      case 'B':
        actual_payload = atoi(optarg);
        random_payload = false;
        printf("Setting payload to %llx\n", (unsigned long long)actual_payload);
        break;
      case 'T':
        // nacktoack_flag=(uint8_t)atoi(optarg);
        target_error_rate = 0.001;
        break;
      case 'C':
        print_perf = 1;
        cpu_meas_enabled = 1;
        break;
      default:
      case 'h':
        printf(
            "%s -h(elp) -p(extended_prefix) -N cell_id -f output_filename -F input_filename -g channel_model -n n_frames -t "
            "Delayspread -s snr0 -S snr1 -x transmission_mode -y TXant -z RXant -i Intefrence0 -j Interference1 -A "
            "interpolation_file -C(alibration offset dB) -N CellId\n",
            argv[0]);
        // printf("-C Generate Calibration information for Abstraction (effective SNR adjustment to remove Pe bias w.r.t. AWGN)\n");
        printf("-A Interpolation_filname Run with Abstraction to generate Scatter plot using interpolation polynomial in file\n");
        printf("-b number of HARQ bits (1-2)\n");
        printf("-B payload to be transmitted on PUCCH\n");
        printf("-c Activate scheduling request\n");
        printf("-d Use TDD\n");
        printf("-f Output filename (.txt format) for Pe/SNR results\n");
        printf("-F Input filename (.txt format) for RX conformance testing\n");
        printf(
            "-g [A,B,C,D,E,F,G] Use 3GPP SCM (A,B,C,D) or 36-101 (E-EPA,F-EVA,G-ETU) models (ignores delay spread and Ricean "
            "factor)\n");
        printf("-i Enter number of ofdm symbols for pucch\n");
        printf("-I Starting symbol index for pucch\n");
        printf("-h This message\n");
        printf("-H Enable frequency hopping for two-symbol PUCCH format 0\n");
        printf("-m initial cyclic shift m0\n");
        printf("-n Number of frames to simulate\n");
        printf("-N Nid_cell\n");
        printf("-o Carrier frequency offset in Hz\n");
        printf("-O oversampling factor (1,2,4,8,16)\n");
        printf("-p Use extended prefix mode\n");
        printf("-P Enter the format of PUCCH\n");
        printf("-q PUCCH number of PRB\n");
        printf("-r PUCCH starting PRB\n");
        printf("-R N_RB_DL\n");
        printf("-s Starting SNR, runs from SNR0 to SNR0 + 5 dB.  If n_frames is 1 then just SNR is simulated\n");
        printf("-S Ending SNR, runs from SNR0 to SNR1\n");
        printf("-t Delay spread for multipath channel\n");
        printf("-T to check nacktoack miss for format 1\n");
        printf("-x Transmission mode (1,2,6 for the moment)\n");
        printf("-y Number of TX antennas used in eNB\n");
        printf("-z Number of RX antennas used in UE\n");
        printf("-C print CPU cost\n");
        exit(-1);
        break;
    }
  }

  double phase = (1 << mu) * 30e-3 * delay_us;

  set_glog(loglvl);

  if (snr1set == 0)
    snr1 = snr0 + 10;

  printf("Initializing gNodeB for mu %d, N_RB_DL %d, n_rx %d\n", mu, N_RB_DL, n_rx);

  if ((format != 0) && (format != 1) && (format != 2)) {
    printf("PUCCH format %d not supported\n", format);
    exit(0);
  }

  AssertFatal(((format < 2) && (nr_bit < 3) && (actual_payload < 5)) || ((format == 2) && (nr_bit > 2) && (nr_bit < 65)),
              "illegal combination format %d, nr_bit %d\n",
              format,
              nr_bit);
  AssertFatal(!format0_freq_hop || (format == 0 && nrofSymbols == 2), "-H requires PUCCH format 0 with two symbols\n");
  int do_DTX = 0;
  if ((format < 2) && (actual_payload == 4))
    do_DTX = 1;

  if (random_payload)
    fill_random(&actual_payload, sizeof(actual_payload));
  actual_payload &= nr_bit < 64 ? (1UL << nr_bit) - 1 : 0xffffffffffffffff;

  printf("Transmitted payload is %lu, do_DTX = %d\n", actual_payload, do_DTX);

  RC.gNB = calloc(1, sizeof(PHY_VARS_gNB *));
  RC.gNB[0] = calloc(1, sizeof(PHY_VARS_gNB));
  gNB = RC.gNB[0];
  gNB->pucch0_thres = pucch_DTX_thres;
  frame_parms = &gNB->frame_parms; // to be initialized I suppose (maybe not necessary for PBCH)
  frame_parms->nb_antennas_tx = n_tx;
  frame_parms->nb_antennas_rx = n_rx;
  frame_parms->N_RB_DL = N_RB_DL;
  frame_parms->N_RB_UL = N_RB_DL;
  frame_parms->Nid_cell = Nid_cell;

  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  cfg->carrier_config.num_tx_ant.value = n_tx;
  cfg->carrier_config.num_rx_ant.value = n_rx;
  nr_phy_config_request_sim(gNB, N_RB_DL, N_RB_DL, mu, Nid_cell, SSB_positions);
  // TDD configuration
  gNB->gNB_config.tdd_table.tdd_period.value = 6;
  do_tdd_config_sim(gNB, mu);

  phy_init_nr_gNB(gNB);
  /* RU handles rxdataF, and gNB just has a pointer. Here, we don't have an RU,
   * so we need to allocate that memory as well. */
  for (i = 0; i < n_rx; i++)
    gNB->common_vars.rxdataF[i] = malloc16_clear(gNB->frame_parms.samples_per_frame_wCP * sizeof(c16_t));

  double fs, txbw, rxbw;
  get_samplerate_and_bw(mu, N_RB_DL, frame_parms->threequarter_fs, &fs, &txbw, &rxbw);

  UE2gNB = new_channel_desc_scm(n_tx, n_rx, channel_model, fs, 0, txbw, DS_TDL, 0.0, CORR_LEVEL_LOW, 0, 0, 0, 0);

  if (UE2gNB == NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  frame_length_complex_samples = frame_parms->samples_per_subframe * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  // frame_length_complex_samples_no_prefix = frame_parms->samples_per_subframe_wCP;

  s_re = malloc(n_tx * sizeof(double *));
  s_im = malloc(n_tx * sizeof(double *));
  r_re = malloc(n_rx * sizeof(double *));
  r_im = malloc(n_rx * sizeof(double *));
  memcpy((void *)&gNB->frame_parms, (void *)frame_parms, sizeof(*frame_parms));
  for (int aatx = 0; aatx < n_tx; aatx++) {
    s_re[aatx] = calloc(1, frame_length_complex_samples * sizeof(double));
    s_im[aatx] = calloc(1, frame_length_complex_samples * sizeof(double));
  }

  for (int aarx = 0; aarx < n_rx; aarx++) {
    r_re[aarx] = calloc(1, frame_length_complex_samples * sizeof(double));
    r_im[aarx] = calloc(1, frame_length_complex_samples * sizeof(double));
  }

  uint8_t mcs = 0;
  int shift = 0;
  if (format == 0) {
    if (sr_flag)
      shift = 1 << nr_bit;
    if (nr_bit == 0)
      mcs = table1_mcs[0];
    else if (nr_bit == 1)
      mcs = table1_mcs[actual_payload + shift];
    else if (nr_bit == 2)
      mcs = table2_mcs[actual_payload + shift];
    else
      AssertFatal(1 == 0, "Either nr_bit %d or sr_flag %d must be non-zero\n", nr_bit, sr_flag);
  }

  startingPRB_intraSlotHopping = N_RB_DL - 1;
  uint32_t hopping_id = Nid_cell;
  uint32_t dmrs_scrambling_id = 0;
  uint32_t data_scrambling_id = 0;

  // configure UE
  UE = calloc(1, sizeof(PHY_VARS_NR_UE));
  memcpy(&UE->frame_parms, frame_parms, sizeof(NR_DL_FRAME_PARMS));
  UE->frame_parms.nb_antennas_rx = 1;

  if (init_nr_ue_signal(UE, 1) != 0) {
    printf("Error at UE NR initialisation\n");
    exit(-1);
  }

  fapi_nr_ul_config_pucch_pdu pucch_tx_pdu = {0};
  if (format == 0) {
    pucch_tx_pdu.format_type = 0;
    pucch_tx_pdu.nr_of_symbols = nrofSymbols;
    pucch_tx_pdu.start_symbol_index = startingSymbolIndex;
    pucch_tx_pdu.bwp_start = 0;
    pucch_tx_pdu.prb_start = startingPRB;
    pucch_tx_pdu.hopping_id = hopping_id;
    pucch_tx_pdu.group_hop_flag = 0;
    pucch_tx_pdu.sequence_hop_flag = 0;
    pucch_tx_pdu.freq_hop_flag = format0_freq_hop;
    pucch_tx_pdu.mcs = mcs;
    pucch_tx_pdu.initial_cyclic_shift = 0;
    pucch_tx_pdu.second_hop_prb = startingPRB_intraSlotHopping;
  }
  else if (format == 1) {
    pucch_tx_pdu.format_type = 0;
    pucch_tx_pdu.n_bit = nr_bit;
    pucch_tx_pdu.payload = actual_payload;
    pucch_tx_pdu.nr_of_symbols = nrofSymbols;
    pucch_tx_pdu.start_symbol_index = startingSymbolIndex;
    pucch_tx_pdu.bwp_start = 0;
    pucch_tx_pdu.prb_start = startingPRB;
    pucch_tx_pdu.hopping_id = hopping_id;
    pucch_tx_pdu.group_hop_flag = 0;
    pucch_tx_pdu.sequence_hop_flag = 0;
    pucch_tx_pdu.freq_hop_flag = 1;
    pucch_tx_pdu.initial_cyclic_shift = m0;
    pucch_tx_pdu.second_hop_prb = startingPRB_intraSlotHopping;
    pucch_tx_pdu.time_domain_occ_idx = timeDomainOCC;
  } else if (format == 2) {
    pucch_tx_pdu.format_type = 2;
    pucch_tx_pdu.rnti = 0x1234;
    pucch_tx_pdu.n_bit = nr_bit;
    pucch_tx_pdu.payload = actual_payload;
    pucch_tx_pdu.nr_of_symbols = nrofSymbols;
    pucch_tx_pdu.start_symbol_index = startingSymbolIndex;
    pucch_tx_pdu.bwp_start = 0;
    pucch_tx_pdu.prb_start = startingPRB;
    pucch_tx_pdu.prb_size = nrofPRB;
    pucch_tx_pdu.hopping_id = hopping_id;
    pucch_tx_pdu.group_hop_flag = 0;
    pucch_tx_pdu.sequence_hop_flag = 0;
    pucch_tx_pdu.freq_hop_flag = 0;
    pucch_tx_pdu.dmrs_scrambling_id = dmrs_scrambling_id;
    pucch_tx_pdu.data_scrambling_id = data_scrambling_id;
    pucch_tx_pdu.second_hop_prb = startingPRB_intraSlotHopping;
  }

  pucch_GroupHopping_t PUCCH_GroupHopping = pucch_tx_pdu.group_hop_flag + (pucch_tx_pdu.sequence_hop_flag << 1);
  double tx_level_fp = 100.0;
  c16_t **rxdataF = gNB->common_vars.rxdataF;
  for (SNR = snr0; SNR <= snr1 && !stop; SNR += 1) {
    reset_meas(&gNB->pucch01_proc_rx);
    reset_meas(&gNB->pucch23_proc_rx);

    ack_nack_errors = 0;
    sr_errors = 0;
    c16_t **txdataF = gNB->common_vars.txdataF;
    for (trial = 0; trial < n_trials && !stop; trial++) {
      for (int aatx = 0; aatx < 1; aatx++)
        bzero(txdataF[aatx], frame_parms->ofdm_symbol_size * sizeof(int));
      if (format == 0 && do_DTX == 0) {
        nr_generate_pucch0(txdataF, frame_parms, amp, nr_slot_tx, &pucch_tx_pdu);
      } else if (format == 1 && do_DTX == 0) {
        nr_generate_pucch1(txdataF, frame_parms, amp, nr_slot_tx, &pucch_tx_pdu);
      } else if (do_DTX == 0) {
        nr_generate_pucch2(txdataF, frame_parms, amp, nr_slot_tx, &pucch_tx_pdu);
      }

      // SNR Computation
      // standard says: SNR = S / N, where S is the total signal energy, N is the noise energy in the transmission bandwidth (i.e.
      // N_RB_DL resource blocks) txlev = S.

      // Compute transmitter energy level
      int symbol_offset = startingSymbolIndex * frame_parms->ofdm_symbol_size;
      int symbol_length = frame_parms->ofdm_symbol_size;
      double txlev = compute_tx_energy_level(txdataF, 1, symbol_offset, symbol_length, n_trials);

      // sigma2 is variance per dimension, so N/(N_RB*12)
      // so, sigma2 = N/(N_RB_DL*12) => (S/SNR)/(N_RB*12)
      int N_RB = (format == 0 || format == 1) ? 1 : nrofPRB;
      double sigma2 = compute_noise_variance(txlev, N_RB_DL * 12, N_RB, 1, SNR, n_trials);

      for (int symb = 0; symb < gNB->frame_parms.symbols_per_slot; symb++) {
        if (symb < startingSymbolIndex || symb >= startingSymbolIndex + nrofSymbols) {
          int i0 = symb * gNB->frame_parms.ofdm_symbol_size;
          for (int re = 0; re < N_RB_DL * 12; re++) {
            i = i0 + re;
            for (int aarx = 0; aarx < n_rx; aarx++) {
              double nr = sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0);
              double ni = sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0);
              rxdataF[aarx][i].r = (int16_t)(tx_level_fp * (nr) / sqrt((double)txlev));
              rxdataF[aarx][i].i = (int16_t)(tx_level_fp * (ni) / sqrt((double)txlev));
            }
          }
        }
      }

      random_channel(UE2gNB, 0);
      freq_channel(UE2gNB, N_RB_DL, 2 * N_RB_DL + 1, 15 << mu);
      allocCast3D(chF, struct complexd, UE2gNB->chF, UE2gNB->nb_tx, UE2gNB->nb_rx, UE2gNB->channelF_len, false);
      for (int symb = 0; symb < nrofSymbols; symb++) {
        int i0 = (startingSymbolIndex + symb) * gNB->frame_parms.ofdm_symbol_size;
        for (int re = 0; re < N_RB_DL * 12; re++) {
          i = i0 + re;
          struct complexd phasor;
          phasor.r = cos(2 * M_PI * phase * re);
          phasor.i = sin(2 * M_PI * phase * re);
          for (int aarx = 0; aarx < n_rx; aarx++) {
            double txr = txdataF[0][i].r;
            double txi = txdataF[0][i].i;
            double rxr = {0}, rxi = {0};
            for (int l = 0; l < UE2gNB->channel_length; l++) {
              rxr = txr * chF[0][aarx][l].r - txi * chF[0][aarx][l].i;
              rxi = txr * chF[0][aarx][l].i + txi * chF[0][aarx][l].r;
            }
            double rxr_tmp = rxr * phasor.r - rxi * phasor.i;
            rxi = rxr * phasor.i + rxi * phasor.r;
            rxr = rxr_tmp;
            double nr = sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0);
            double ni = sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0);
            rxdataF[aarx][i0 + re].r = (int16_t)(tx_level_fp * (rxr + nr) / sqrt((double)txlev));
            rxdataF[aarx][i0 + re].i = (int16_t)(tx_level_fp * (rxi + ni) / sqrt((double)txlev));

            if (n_trials == 1 && fabs(txr) > 0)
              printf("symb %d, re %d , aarx %d : txr %f, txi %f, chr %f, chi %f, nr %f, ni %f, rxr %f, rxi %f => %d,%d\n",
                     symb,
                     re,
                     aarx,
                     txr,
                     txi,
                     chF[0][aarx][re].r,
                     chF[0][aarx][re].i,
                     nr,
                     ni,
                     rxr,
                     rxi,
                     rxdataF[aarx][i].r,
                     rxdataF[aarx][i].i);
          }
        }
      }

      int rxlev = 0;
      for (int aarx = 0; aarx < n_rx; aarx++)
        rxlev += signal_energy((int32_t *)&rxdataF[aarx][startingSymbolIndex * frame_parms->ofdm_symbol_size],
                               frame_parms->ofdm_symbol_size);

      int rxlev_pucch = 0;

      for (int aarx = 0; aarx < n_rx; aarx++)
        rxlev_pucch += signal_energy((int32_t *)&rxdataF[aarx][startingSymbolIndex * frame_parms->ofdm_symbol_size], 12);

      // set UL mask for pucch allocation
      uint32_t rb_mask_ul[14][MAX_BWP_SIZE] = {0};

      if (format == 1) {
        for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
          if (s >= startingSymbolIndex && s < (startingSymbolIndex + nrofSymbols / 2))
            for (int rb = 0; rb < N_RB; rb++) {
              rb_mask_ul[s][rb + startingPRB] = 1;
            }
          else if (s >= (startingSymbolIndex + nrofSymbols / 2) && s < startingSymbolIndex + nrofSymbols)
            for (int rb = 0; rb < N_RB; rb++) {
              rb_mask_ul[s][rb + startingPRB_intraSlotHopping] = 1;
            }
        }
      } else {
        for (int s = 0; s < frame_parms->symbols_per_slot; s++) {
          if (s >= startingSymbolIndex && s < (startingSymbolIndex + nrofSymbols))
            for (int rb = 0; rb < N_RB; rb++) {
              rb_mask_ul[s][rb + startingPRB] = 1;
            }
        }
      }

      // noise measurement (all PRBs)
      gNB_I0_measurements(gNB, nr_slot_tx, 0, gNB->frame_parms.symbols_per_slot, rb_mask_ul);

      if (n_trials == 1)
        printf("noise rxlev %d (%d dB), rxlev pucch %d dB sigma2 %f dB, SNR %f, TX %f, I0 (pucch) %d, I0 (avg) %d\n",
               rxlev,
               dB_fixed(rxlev),
               dB_fixed(rxlev_pucch),
               10 * log10(sigma2),
               SNR,
               10 * log10((double)txlev * UE->frame_parms.ofdm_symbol_size / 12),
               gNB->measurements.n0_subband_power_tot_dB[startingPRB],
               gNB->measurements.n0_subband_power_avg_dB);
      nfapi_nr_pucch_pdu_t pucch_pdu = {.param_v4.numSpatialStreamIndices = n_rx};
      if (format == 0) {
        nfapi_nr_uci_pucch_pdu_format_0_1_t uci_pdu;
        gNB->phy_stats[0].rnti = 0x1234;
        pucch_pdu.rnti = 0x1234;
        pucch_pdu.subcarrier_spacing = 1;
        pucch_pdu.group_hop_flag = PUCCH_GroupHopping & 1;
        pucch_pdu.sequence_hop_flag = (PUCCH_GroupHopping >> 1) & 1;
        pucch_pdu.bit_len_harq = nr_bit;
        pucch_pdu.bit_len_csi_part1 = 0;
        pucch_pdu.bit_len_csi_part2 = 0;
        pucch_pdu.sr_flag = sr_flag;
        pucch_pdu.nr_of_symbols = nrofSymbols;
        pucch_pdu.hopping_id = hopping_id;
        pucch_pdu.initial_cyclic_shift = 0;
        pucch_pdu.start_symbol_index = startingSymbolIndex;
        pucch_pdu.prb_start = startingPRB;
        pucch_pdu.prb_size = 1;
        pucch_pdu.bwp_start = 0;
        pucch_pdu.bwp_size = N_RB_DL;
        /* Use the same frequency-hopping configuration at the UE and gNB. */
        pucch_pdu.freq_hop_flag = pucch_tx_pdu.freq_hop_flag;
        pucch_pdu.second_hop_prb = pucch_tx_pdu.second_hop_prb;

        start_meas(&gNB->pucch01_proc_rx);
        nr_decode_pucch0(gNB, rxdataF, nr_frame_tx, nr_slot_tx, &uci_pdu, &pucch_pdu);
        stop_meas(&gNB->pucch01_proc_rx);
        if (sr_flag == 1) {
          if (uci_pdu.sr.sr_indication == 0 || uci_pdu.sr.sr_confidence_level == 1)
            sr_errors += 1;
        }
        // harq value 0 -> pass
        nfapi_nr_harq_t *harq_list = uci_pdu.harq.harq_list;
        // confidence value 0 -> good confidence
        const int confidence_lvl = uci_pdu.harq.harq_confidence_level;
        if (nr_bit > 0) {
          if (nr_bit == 1 && do_DTX == 0)
            ack_nack_errors += (actual_payload ^ (!harq_list[0].harq_value));
          else if (do_DTX == 0)
            ack_nack_errors +=
                (((actual_payload & 1) ^ (!harq_list[1].harq_value)) + ((actual_payload >> 1) ^ (!harq_list[0].harq_value)));
          else if ((!confidence_lvl && !harq_list[0].harq_value) || (!confidence_lvl && nr_bit == 2 && !harq_list[1].harq_value))
            ack_nack_errors++;
        }
      } else if (format == 1) {
        nfapi_nr_uci_pucch_pdu_format_0_1_t uci_pdu;
        gNB->phy_stats[0].rnti = 0x1234;
        pucch_pdu.rnti = 0x1234;
        pucch_pdu.subcarrier_spacing = 1;
        pucch_pdu.group_hop_flag = PUCCH_GroupHopping & 1;
        pucch_pdu.sequence_hop_flag = (PUCCH_GroupHopping >> 1) & 1;
        pucch_pdu.bit_len_harq = nr_bit;
        pucch_pdu.bit_len_csi_part1 = 0;
        pucch_pdu.bit_len_csi_part2 = 0;
        pucch_pdu.sr_flag = sr_flag;
        pucch_pdu.nr_of_symbols = nrofSymbols;
        pucch_pdu.hopping_id = hopping_id;
        pucch_pdu.initial_cyclic_shift = m0;
        pucch_pdu.start_symbol_index = startingSymbolIndex;
        pucch_pdu.prb_start = startingPRB;
        pucch_pdu.prb_size = 1;
        pucch_pdu.bwp_start = 0;
        pucch_pdu.bwp_size = N_RB_DL;
        pucch_pdu.freq_hop_flag = 1;
        pucch_pdu.second_hop_prb = N_RB_DL - 1;
        pucch_pdu.time_domain_occ_idx = timeDomainOCC;

        start_meas(&gNB->pucch01_proc_rx);
        nr_decode_pucch1(gNB, rxdataF, nr_frame_tx, nr_slot_tx, &uci_pdu, &pucch_pdu);      
        stop_meas(&gNB->pucch01_proc_rx);
        // harq value 0 -> pass
        nfapi_nr_harq_t *harq_list = uci_pdu.harq.harq_list;
        // confidence value 0 -> good confidence
        const int confidence_lvl = uci_pdu.harq.harq_confidence_level;
        if (nr_bit > 0) {
          if (nr_bit == 1 && do_DTX == 0)
            ack_nack_errors += (actual_payload ^ (!harq_list[0].harq_value));
          else if (do_DTX == 0)
            ack_nack_errors +=
                (((actual_payload & 1) ^ (!harq_list[1].harq_value)) + ((actual_payload >> 1) ^ (!harq_list[0].harq_value)));
          else if ((!confidence_lvl && !harq_list[0].harq_value) || (!confidence_lvl && nr_bit == 2 && !harq_list[1].harq_value))
            ack_nack_errors++;
        }

      } else if (format == 2) {
        nfapi_nr_uci_pucch_pdu_format_2_3_4_t uci_pdu = {0};
        pucch_pdu.rnti = 0x1234;
        pucch_pdu.subcarrier_spacing = 1;
        pucch_pdu.group_hop_flag = PUCCH_GroupHopping & 1;
        pucch_pdu.sequence_hop_flag = (PUCCH_GroupHopping >> 1) & 1;
        pucch_pdu.bit_len_csi_part1 = nr_bit;
        pucch_pdu.bit_len_harq = 0;
        pucch_pdu.bit_len_csi_part2 = 0;
        pucch_pdu.sr_flag = 0;
        pucch_pdu.nr_of_symbols = nrofSymbols;
        pucch_pdu.hopping_id = hopping_id;
        pucch_pdu.initial_cyclic_shift = 0;
        pucch_pdu.start_symbol_index = startingSymbolIndex;
        pucch_pdu.prb_size = nrofPRB;
        pucch_pdu.prb_start = startingPRB;
        pucch_pdu.dmrs_scrambling_id = dmrs_scrambling_id;
        pucch_pdu.data_scrambling_id = data_scrambling_id;
        if (nrofSymbols > 1) {
          pucch_pdu.freq_hop_flag = 1;
          pucch_pdu.second_hop_prb = N_RB_DL - 1;
        } else
          pucch_pdu.freq_hop_flag = 0;
        start_meas(&gNB->pucch23_proc_rx);
        nr_decode_pucch2(gNB, rxdataF, nr_frame_tx, nr_slot_tx, &uci_pdu, &pucch_pdu);
        stop_meas(&gNB->pucch23_proc_rx);
        if (uci_pdu.csi_part1.csi_part1_crc == 1) // missed reception
          ack_nack_errors++;
        else {
          int csi_part1_bytes = pucch_pdu.bit_len_csi_part1 >> 3;
          if ((pucch_pdu.bit_len_csi_part1 & 7) > 0)
            csi_part1_bytes++;
          for (int i = 0; i < csi_part1_bytes; i++) {
            if (uci_pdu.csi_part1.csi_part1_payload[i] != ((uint8_t *)&actual_payload)[i]) {
              ack_nack_errors++;
              break;
            }
          }
        }
        free(uci_pdu.csi_part1.csi_part1_payload);
      }
    }
    if (sr_flag == 1)
      printf("SR: SNR=%f, n_trials=%d, n_bit_errors=%d\n", SNR, n_trials, sr_errors);
    if (print_perf && gNB->pucch01_proc_rx.trials > 0)
      printStatIndent(&gNB->pucch01_proc_rx, "PUCCH01 RX");
    if (print_perf && gNB->pucch23_proc_rx.trials > 0)
      printStatIndent(&gNB->pucch23_proc_rx, "PUCCH23 RX");
    if (nr_bit > 0)
      printf("ACK/NACK: SNR=%f, n_trials=%d, n_bit_errors=%d\n", SNR, n_trials, ack_nack_errors);
    if ((float)(ack_nack_errors + sr_errors) / (float)(n_trials) <= target_error_rate) {
      printf("PUCCH test OK\n");
      ret = 0;
      break;
    }
  }
  free_channel_desc_scm(UE2gNB);
  term_freq_channel();

  int nb_slots_to_set = (1 << mu) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  for (int i = 0; i < nb_slots_to_set; ++i)
    free(gNB->gNB_config.tdd_table.max_tdd_periodicity_list[i].max_num_of_symbol_per_slot_list);
  free(gNB->gNB_config.tdd_table.max_tdd_periodicity_list);

  for (i = 0; i < n_rx; i++)
    free(gNB->common_vars.rxdataF[i]);
  phy_free_nr_gNB(gNB);
  free(RC.gNB[0]);
  free(RC.gNB);

  term_nr_ue_signal(UE);
  free(UE);

  for (int aatx = 0; aatx < n_tx; aatx++) {
    free(s_re[aatx]);
    free(s_im[aatx]);
  }
  for (int aarx = 0; aarx < n_rx; aarx++) {
    free(r_re[aarx]);
    free(r_im[aarx]);
  }
  free(s_re);
  free(s_im);
  free(r_re);
  free(r_im);
  free(uedata_ptr);
  if (output_fd)
    fclose(output_fd);
  if (input_fd)
    fclose(input_fd);

  loader_reset();
  logTerm();

  return ret;
}
