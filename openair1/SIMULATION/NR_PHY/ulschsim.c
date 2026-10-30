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
#include "common/utils/LOG/log.h"
#include "common/ran_context.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/defs_gNB.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/NR_TRANSPORT/nr_ulsch.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "SCHED_NR/sched_nr.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "openair1/SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "executables/nr-uesoftmodem.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "bits.h"
#include "nfapi/open-nFAPI/fapi/inc/nr_fapi_p5_utils.h"

//#define DEBUG_NR_ULSCHSIM

RAN_CONTEXT_t RC;

static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void) {
  return &softmodem_params;
}
void init_downlink_harq_status(NR_DL_UE_HARQ_t *dl_harq) {}
NR_IF_Module_t *NR_IF_Module_init(int Mod_id)
{
  return (NULL);
}

// needed for some functions
uint16_t n_rnti = 0x1234;

nrUE_params_t nrUE_params;

nrUE_params_t *get_nrUE_params(void) {
  return &nrUE_params;
}

configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv)
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  int i;
  double SNR, snr0 = -2.0, snr1 = 2.0, SNR_lin;
  double snr_step = 0.1;
  uint8_t snr1set = 0;
  FILE *output_fd = NULL;
  int trial, n_trials = 1, n_errors = 0, n_false_positive = 0;
  uint8_t n_tx = 1, n_rx = 1, nb_codewords = 1;
  uint16_t Nid_cell = 0;
  channel_desc_t *gNB2UE;
  uint8_t extended_prefix_flag = 0;
  FILE *input_fd = NULL;
  SCM_t channel_model = AWGN;  //Rayleigh1_anticorr;
  uint16_t N_RB_DL = 106, N_RB_UL = 106, mu = 1;
  int frame = 0, subframe = 0;
  NR_DL_FRAME_PARMS *frame_parms;
  double sigma;
  unsigned char qbits = 8;
  int loglvl = OAILOG_WARNING;
  uint64_t SSB_positions=0x01;
  uint16_t nb_symb_sch = 12;
  uint16_t nb_rb = 50;
  uint8_t Imcs = 9;
  uint8_t Nl = 1;
  uint8_t max_ldpc_iterations = 5;
  uint8_t mcs_table = 0;

  double DS_TDL = .03;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_ULSCHSIM] Error, configuration module init failed\n");
  }

  randominit();

  int c;
  while ((c = getopt(argc, argv, "--:O:hg:n:s:S:py:z:M:N:R:F:m:l:q:r:W:")) != -1) {

    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    switch (c) {

      case 'g':
        switch ((char) *optarg) {
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

          default:
            printf("Unsupported channel model! Exiting.\n");
            exit(-1);
        }

        break;

      case 'n':
        n_trials = atoi(optarg);
#ifdef DEBUG_NR_ULSCHSIM
        printf("n_trials (-n) = %d\n", n_trials);
#endif
        break;

      case 's':
        snr0 = atof(optarg);
#ifdef DEBUG_NR_ULSCHSIM
        printf("Setting SNR0 to %f\n", snr0);
#endif
        break;

      case 'S':
        snr1 = atof(optarg);
        snr1set = 1;
#ifdef DEBUG_NR_ULSCHSIM
        printf("Setting SNR1 to %f\n", snr1);
#endif
        break;

      case 'p':
        extended_prefix_flag = 1;
        break;

      case 'y':
        n_tx = atoi(optarg);

        if ((n_tx == 0) || (n_tx > 4)) {
          printf("Unsupported number of TX antennas %d. Exiting.\n", n_tx);
          exit(-1);
        }

        break;

      case 'z':
        n_rx = atoi(optarg);

        if ((n_rx == 0) || (n_rx > 4)) {
          printf("Unsupported number of RX antennas %d. Exiting.\n", n_rx);
          exit(-1);
        }

        break;

      case 'M':
        SSB_positions = atoi(optarg);
        break;

      case 'N':
        Nid_cell = atoi(optarg);
        break;

      case 'R':
        N_RB_UL = atoi(optarg);
#ifdef DEBUG_NR_ULSCHSIM
        printf("N_RB_UL (-R) = %d\n", N_RB_UL);
#endif
        break;

      case 'F':
        input_fd = fopen(optarg, "r");

        if (input_fd == NULL) {
            printf("Problem with filename %s. Exiting.\n", optarg);
            exit(-1);
        }

        break;

      case 'W':
        Nl = atoi(optarg);
      break;

      case 'm':
        Imcs = atoi(optarg);
#ifdef DEBUG_NR_ULSCHSIM
        printf("Imcs (-m) = %d\n", Imcs);
#endif
        break;

      case 'l':
        nb_symb_sch = atoi(optarg);
        break;

      case 'q':
        mcs_table = atoi(optarg);
        break;

      case 'r':
        nb_rb = atoi(optarg);
        break;

      case '?':
        printf("Unknown option or missing argument: -%c\n", optopt);
      case 'h':
      default:
        printf(
            "%s -h(elp) -g channel_model -n n_frames -s snr0 -S snr1 -p(extended_prefix) -y TXant -z RXant -M -N cell_id -R -F "
            "input_filename -m -l -r\n",
            argv[0]);
        printf("-F Input filename (.txt format) for RX conformance testing\n");
        printf(
            "-g [A,B,C,D,E,F,G] Use 3GPP SCM (A,B,C,D) or 36-101 (E-EPA,F-EVA,G-ETU) models (ignores delay spread and Ricean "
            "factor)\n");
        printf("-h This message\n");
        printf("-l number of symbol\n");
        printf("-m MCS\n");
        printf("-M Multiple SSB positions in burst\n");
        printf("-n Number of frames to simulate\n");
        printf("-N Nid_cell\n");
        printf("-p Use extended prefix mode\n");
        printf("-r number of RB\n");
        printf("-R N_RB_UL\n");
        printf("-s Starting SNR, runs from SNR0 to SNR0 + 5 dB.  If n_frames is 1 then just SNR is simulated\n");
        printf("-S Ending SNR, runs from SNR0 to SNR1\n");
        printf("-W number of layer\n");
        printf("-y Number of TX antennas used in eNB\n");
        printf("-z Number of RX antennas used in UE\n");
        exit(-1);
        break;
    }
  }

  logInit();
  set_glog(loglvl);

  if (snr1set == 0)
    snr1 = snr0 + 10;

  gNB2UE = new_channel_desc_scm(n_tx,
                                n_rx,
                                channel_model,
                                61.44e6, //N_RB2sampling_rate(N_RB_DL),
                                0,
                                40e6, //N_RB2channel_bandwidth(N_RB_DL),
                                DS_TDL,
                                0.0,
                                CORR_LEVEL_LOW,
                                0,
                                0,
                                0,
                                0);

  if (gNB2UE == NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }


  RC.gNB = (PHY_VARS_gNB **) malloc(sizeof(PHY_VARS_gNB *));
  RC.gNB[0] = calloc(1, sizeof(PHY_VARS_gNB));
  PHY_VARS_gNB *gNB = RC.gNB[0];

  initTpool("n", &gNB->threadPool, true);
  frame_parms = &gNB->frame_parms; //to be initialized I suppose (maybe not necessary for PBCH)
  frame_parms->N_RB_DL = N_RB_DL;
  frame_parms->N_RB_UL = N_RB_UL;
  frame_parms->Ncp = extended_prefix_flag ? NR_EXTENDED : NR_NORMAL;
  gNB->max_ldpc_iterations = max_ldpc_iterations;

  crcTableInit();

  memcpy(&gNB->frame_parms, frame_parms, sizeof(NR_DL_FRAME_PARMS));

  gNB->frame_parms.nb_antennas_tx = 1;
  gNB->frame_parms.nb_antennas_rx = n_rx;

  nr_phy_config_request_sim(gNB, N_RB_UL, N_RB_UL, mu, Nid_cell, SSB_positions);
  // TDD configuration
  gNB->gNB_config.tdd_table.tdd_period.value = 6;
  do_tdd_config_sim(gNB, mu);

  phy_init_nr_gNB(gNB);

  //configure UE
  PHY_VARS_NR_UE *UE = calloc(1, sizeof(*UE));
  memcpy(&UE->frame_parms, frame_parms, sizeof(NR_DL_FRAME_PARMS));

  UE->frame_parms.nb_antennas_tx = n_tx;
  UE->frame_parms.nb_antennas_rx = 1;
  UE->nrLDPC_coding_interface = gNB->nrLDPC_coding_interface;

  if (init_nr_ue_signal(UE, 1) != 0) {
    printf("Error at UE NR initialisation.\n");
    exit(-1);
  }

  nr_init_ul_harq_processes(UE->ul_harq_processes, NR_MAX_HARQ_PROCESSES, UE->frame_parms.N_RB_UL, UE->frame_parms.nb_antennas_tx);

  initFloatingCoresTpool(1, &nrUE_params.Tpool, false, "UE-tpool");

  unsigned char harq_pid = 0;
  unsigned int TBS = 8424;
  unsigned int available_bits;
  uint8_t nb_re_dmrs = 6;
  uint8_t length_dmrs = 2;
  uint8_t N_PRB_oh;
  uint16_t N_RE_prime,code_rate;
  unsigned char mod_order;  
  uint8_t rvidx = 0;
  int UE_id = 0;

  NR_gNB_ULSCH_t *ulsch_gNB = &gNB->ulsch[UE_id];
  NR_UL_gNB_HARQ_t *harq_process_gNB = ulsch_gNB->harq_process;
  ulsch_gNB->harq_pid = 0;
  nfapi_nr_pusch_pdu_t *rel15_ul = &harq_process_gNB->ulsch_pdu;
  NR_gNB_PUSCH *pusch_vars = &gNB->pusch_vars[UE_id];

  nr_phy_data_tx_t phy_data = {0};
  NR_UE_ULSCH_t *ulsch_ue = &phy_data.ulsch;

  mod_order = nr_get_Qm_ul(Imcs, mcs_table);
  code_rate = nr_get_code_rate_ul(Imcs, mcs_table);
  available_bits = nr_get_G(nb_rb, nb_symb_sch, nb_re_dmrs, length_dmrs, 0, mod_order, Nl);
  TBS = nr_compute_tbs(mod_order,code_rate, nb_rb, nb_symb_sch, nb_re_dmrs*length_dmrs, 0, 0, Nl);
  pusch_vars->uci_info.G_ulsch = available_bits;
  printf("\nAvailable bits %u TBS %u mod_order %d\n", available_bits, TBS, mod_order);

  /////////// setting rel15_ul parameters ///////////
  rel15_ul->rb_size             = nb_rb;
  rel15_ul->nr_of_symbols       = nb_symb_sch;
  rel15_ul->qam_mod_order       = mod_order;
  rel15_ul->mcs_index           = Imcs;
  rel15_ul->pusch_data.rv_index = rvidx;
  rel15_ul->ul_dmrs_symb_pos    = nb_re_dmrs;
  rel15_ul->dmrs_config_type = pusch_dmrs_type1;
  rel15_ul->num_dmrs_cdm_grps_no_data = 1;
  rel15_ul->nrOfLayers          = Nl;
  rel15_ul->target_code_rate    = code_rate;
  rel15_ul->pusch_data.tb_size  = TBS>>3;
  rel15_ul->maintenance_parms_v3.ldpcBaseGraph = get_BG(TBS, code_rate);
  int bits = count_bits64_with_mask(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, rel15_ul->nr_of_symbols);
  AssertFatal(length_dmrs == bits, "length_dmrs %d bits %d\n", length_dmrs, bits);
  rel15_ul->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
  ///////////////////////////////////////////////////

  double modulated_input[16 * 68 * 384]; // [hna] 16 segments, 68*Zc
  short channel_output_uncoded[16 * 68 * 384];
  unsigned int errors_bit_uncoded = 0;

  /////////////////////////[adk] preparing UL harq_process parameters/////////////////////////
  ///////////
  NR_UL_UE_HARQ_t *harq_process_ul_ue = &UE->ul_harq_processes[harq_pid];
  DevAssert(harq_process_ul_ue);

  N_PRB_oh   = 0; // higher layer (RRC) parameter xOverhead in PUSCH-ServingCellConfig
  N_RE_prime = NR_NB_SC_PER_RB*nb_symb_sch - nb_re_dmrs - N_PRB_oh;

  ulsch_ue->pusch_pdu.rnti = n_rnti;
  ulsch_ue->pusch_pdu.mcs_table = mcs_table;
  ulsch_ue->pusch_pdu.mcs_index = Imcs;
  ulsch_ue->pusch_pdu.nrOfLayers = Nl;
  ulsch_ue->pusch_pdu.rb_size = nb_rb;
  ulsch_ue->pusch_pdu.nr_of_symbols = nb_symb_sch;
  harq_process_ul_ue->num_of_mod_symbols = N_RE_prime*nb_rb*nb_codewords;
  ulsch_ue->pusch_pdu.pusch_data.rv_index = rvidx;
  ulsch_ue->pusch_pdu.pusch_data.tb_size  = TBS>>3;
  ulsch_ue->pusch_pdu.target_code_rate = code_rate;
  ulsch_ue->pusch_pdu.qam_mod_order = mod_order;
  ulsch_ue->pusch_pdu.ldpcBaseGraph = get_BG(TBS, code_rate);
  unsigned char *test_input = harq_process_ul_ue->payload_AB;

  ///////////
  ////////////////////////////////////////////////////////////////////////////////////////////

  for (i = 0; i < TBS / 8; i++)
    test_input[i] = (unsigned char) rand();

#ifdef DEBUG_NR_ULSCHSIM
  for (i = 0; i < TBS / 8; i++) printf("test_input[i]=%hhu \n",test_input[i]);
#endif

  /////////////////////////ULSCH coding/////////////////////////
  ///////////
  unsigned int G = available_bits;

  if (input_fd == NULL) {
    uint8_t ULSCH_ids[] = {0};
    const nfapi_nr_ue_pusch_pdu_t *p = &ulsch_ue->pusch_pdu;
    nr_ulsch_pre_encoding(harq_process_ul_ue, p->pusch_data.tb_size, p->nrOfLayers, p->ldpcBaseGraph);
    nr_ulsch_encoding(UE, ulsch_ue, 0, 0, &G, 1, ULSCH_ids);
  }
  
  printf("\n");

  ///////////
  ////////////////////////////////////////////////////////////////////

  for (SNR = snr0; SNR < snr1 && !stop; SNR += snr_step) {
    errors_bit_uncoded = 0;
    n_errors = 0;
    n_false_positive = 0;

    for (trial = 0; trial < n_trials && !stop; trial++) {
      memset(pusch_vars->ulsch_llrs, 0, (8 * ((3 * 8 * 6144) + 12)) * sizeof(int16_t));
      harq_process_gNB->harq_to_be_cleared = true;

      for (i = 0; i < available_bits; i++) {

#ifdef DEBUG_CODER
        if ((i&0xf)==0)
          printf("\ne %d..%d:    ",i,i+15);
#endif

        if ((harq_process_ul_ue->f[i >> 3] & (1 << (i & 7))) == 0)
          modulated_input[i] = 1.0;        ///sqrt(2);  //QPSK
        else
          modulated_input[i] = -1.0;        ///sqrt(2);
  
#if 1
        SNR_lin = pow(10, SNR / 10.0);
        sigma = 1.0 / sqrt(2 * SNR_lin);
        pusch_vars->ulsch_llrs[i] = (int16_t) quantize(sigma / 4.0 / 4.0,
                                                modulated_input[i] + sigma * gaussdouble(0.0, 1.0),
                                                qbits);
#else
        pusch_vars->ulsch_llrs[i] = (int16_t) quantize(0.01, modulated_input[i], qbits);
#endif

        //Uncoded BER
        if (pusch_vars->ulsch_llrs[i] < 0)
          channel_output_uncoded[i] = 1;  //QPSK demod
        else
          channel_output_uncoded[i] = 0;

        if (channel_output_uncoded[i] != harq_process_ul_ue->f[i])
          errors_bit_uncoded = errors_bit_uncoded + 1;
      }
#ifdef DEBUG_CODER
      printf("\n");
      exit(-1);
#endif
      nr_ulsch_decoding(gNB, frame_parms, frame, subframe, &UE_id, 1);
      if (harq_process_gNB->processedSegments == harq_process_gNB->C) {
        bool crc_valid = check_crc(harq_process_gNB->b, lenWithCrc(1, TBS << 3), crcType(1, TBS << 3));
        if (!crc_valid) {
          n_false_positive++;
        }
      } else {
        n_errors++;
      }
    }

    printf("*****************************************\n");
    printf("SNR %f, uncoded BER %f, BLER %f (false positive %f)\n", SNR,
           (float) errors_bit_uncoded / (float) available_bits / (float) n_trials,
           (float) n_errors / (float) n_trials,
           (float) n_false_positive / (float) n_trials);
    printf("*****************************************\n");
    printf("\n");

    if (n_errors == 0) {
      printf("PUSCH test OK\n");
      printf("\n");
      break;
    }
    printf("\n");
  }

  free_nr_ue_ul_harq(UE->ul_harq_processes, NR_MAX_HARQ_PROCESSES, UE->frame_parms.N_RB_UL, UE->frame_parms.nb_antennas_tx);

  free_config_request(&gNB->gNB_config);

  term_nr_ue_signal(UE);
  free(UE);

  abortTpool(&gNB->threadPool);
  phy_free_nr_gNB(gNB);
  free(RC.gNB[0]);
  free(RC.gNB);

  free_channel_desc_scm(gNB2UE);

  if (output_fd)
    fclose(output_fd);

  if (input_fd)
    fclose(input_fd);

  loader_reset();
  logTerm();

  return (n_errors);
}
