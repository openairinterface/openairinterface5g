/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "common/ran_context.h"
#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"
#include "common/utils/load_module_shlib.h"
#include "PHY/defs_gNB.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/NR_TRANSPORT/nr_dlsch.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "SCHED_NR/sched_nr.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "openair1/SIMULATION/NR_PHY/nr_unitary_defs.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "executables/nr-uesoftmodem.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "openair1/PHY/phy_extern_nr_ue.h"
#include "nfapi/open-nFAPI/fapi/inc/nr_fapi_p5_utils.h"

//#define DEBUG_NR_DLSCHSIM

RAN_CONTEXT_t RC;
UE_nr_rxtx_proc_t proc;

// needed for some functions
PHY_VARS_NR_UE ***nrPHY_vars_UE_g;
uint16_t n_rnti = 0x1234;
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void) {
  return &softmodem_params;
}
void init_downlink_harq_status(NR_DL_UE_HARQ_t *dl_harq) {}
NR_IF_Module_t *NR_IF_Module_init(int Mod_id)
{
  return (NULL);
}

nrUE_params_t nrUE_params={0};

nrUE_params_t *get_nrUE_params(void) {
  return &nrUE_params;
}
configmodule_interface_t *uniqCfg = NULL;

int main(int argc, char **argv)
{
  PHY_VARS_gNB *gNB;
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  int i;
  double SNR, SNR_lin, snr0 = -2.0, snr1 = 2.0;
  double snr_step = 0.1;
  uint8_t snr1set = 0;
  int **txdata;
  double **s_re, **s_im, **r_re, **r_im;
  FILE *output_fd = NULL;
  int trial, n_trials = 1, n_errors = 0, n_false_positive = 0;
  uint8_t n_tx = 1, n_rx = 1;
  uint16_t Nid_cell = 0;
  channel_desc_t *gNB2UE;
  uint8_t extended_prefix_flag = 0;
  FILE *input_fd = NULL, *pbch_file_fd = NULL;
  SCM_t channel_model = AWGN;  //Rayleigh1_anticorr;
  uint16_t N_RB_DL = 106, mu = 1;
  unsigned char pbch_phase = 0;
  int frame = 0, slot = 0;
  int frame_length_complex_samples;
  NR_DL_FRAME_PARMS *frame_parms;
  double sigma;
  unsigned char qbits = 8;
  int loglvl = OAILOG_WARNING;
  uint8_t dlsch_threads = 0;
  float target_error_rate = 0.01;
  uint64_t SSB_positions=0x01;
  uint16_t nb_symb_sch = 12;
  uint16_t nb_rb = 50;
  uint8_t Imcs = 9;
  uint8_t mcs_table = 0;
  double DS_TDL = .03;
  char gNBthreads[128]="n";
  int Tbslbrm = 950984;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_DLSCHSIM] Error, configuration module init failed\n");
  }

  randominit();

  int c;
  while ((c = getopt(argc, argv, "--:O:df:hpg:i:j:n:l:m:r:s:S:y:z:M:N:F:R:P:L:X:")) != -1) {

    /* ignore long options starting with '--' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    switch (c) {

		case 'd':
			dlsch_threads = atoi(optarg);
			break;

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
			break;

		case 's':
			snr0 = atof(optarg);
#ifdef DEBUG_NR_DLSCHSIM
			printf("Setting SNR0 to %f\n", snr0);
#endif
			break;

		case 'S':
			snr1 = atof(optarg);
			snr1set = 1;
			printf("Setting SNR1 to %f\n", snr1);
#ifdef DEBUG_NR_DLSCHSIM
			printf("Setting SNR1 to %f\n", snr1);
#endif
			break;

		case 'p':
			extended_prefix_flag = 1;
			break;

		case 'y':
			n_tx = atoi(optarg);

			if ((n_tx == 0) || (n_tx > 2)) {
				printf("Unsupported number of TX antennas %d. Exiting.\n", n_tx);
				exit(-1);
			}

			break;

		case 'z':
			n_rx = atoi(optarg);

			if ((n_rx == 0) || (n_rx > 2)) {
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
			N_RB_DL = atoi(optarg);
			break;

		case 'F':
			input_fd = fopen(optarg, "r");

			if (input_fd == NULL) {
				printf("Problem with filename %s. Exiting.\n", optarg);
				exit(-1);
			}

			break;

		case 'P':
			pbch_phase = atoi(optarg);

			if (pbch_phase > 3)
				printf("Illegal PBCH phase (0-3) got %d\n", pbch_phase);

			break;

		case 'L':
		  loglvl = atoi(optarg);
		  break;

		case 'm':
			Imcs = atoi(optarg);
			break;

		case 'l':
			nb_symb_sch = atoi(optarg);
			break;

		case 'r':
			nb_rb = atoi(optarg);
			break;

		case 'X':
		  strncpy(gNBthreads, optarg, sizeof(gNBthreads)-1);
		  gNBthreads[sizeof(gNBthreads)-1]=0;
		  break;

    case '?':
      printf("Unknown option or missing argument: -%c\n", optopt);
    case 'h':
    default:
      printf(
          "%s -h(elp) -p(extended_prefix) -N cell_id -f output_filename -F input_filename -g channel_model -n n_frames -t "
          "Delayspread -s snr0 -S snr1  -y TXant -z RXant -i Intefrence0 -j Interference1 -A interpolation_file -C(alibration "
          "offset dB) -N CellId\n",
          argv[0]);
      printf("-A Interpolation_filname Run with Abstraction to generate Scatter plot using interpolation polynomial in file\n");
      printf("-d number of dlsch threads, 0: no dlsch parallelization\n");
      printf("-F Input filename (.txt format) for RX conformance testing\n");
      printf(
          "-g [A,B,C,D,E,F,G] Use 3GPP SCM (A,B,C,D) or 36-101 (E-EPA,F-EVA,G-ETU) models (ignores delay spread and Ricean "
          "factor)\n");
      printf("-h This message\n");
      printf("-M Multiple SSB positions in burst\n");
      printf("-n Number of frames to simulate\n");
      printf("-N Nid_cell\n");
      printf("-O oversampling factor (1,2,4,8,16)\n");
      printf("-p Use extended prefix mode\n");
      printf("-R N_RB_DL\n");
      printf("-s Starting SNR, runs from SNR0 to SNR0 + 5 dB.  If n_frames is 1 then just SNR is simulated\n");
      printf("-S Ending SNR, runs from SNR0 to SNR1\n");
      printf("-t Delay spread for multipath channel\n");
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
	gNB = RC.gNB[0];
	initNamedTpool(gNBthreads, &gNB->threadPool, true, "gNB-tpool");
  initFloatingCoresTpool(dlsch_threads, &nrUE_params.Tpool, false, "UE-tpool");
	frame_parms = &gNB->frame_parms; //to be initialized I suppose (maybe not necessary for PBCH)
	frame_parms->nb_antennas_tx = n_tx;
	frame_parms->nb_antennas_rx = n_rx;
	frame_parms->N_RB_DL = N_RB_DL;
	frame_parms->Ncp = extended_prefix_flag ? NR_EXTENDED : NR_NORMAL;
	crcTableInit();
	nr_phy_config_request_sim(gNB, N_RB_DL, N_RB_DL, mu, Nid_cell,SSB_positions);
    // TDD configuration
    gNB->gNB_config.tdd_table.tdd_period.value = 6;
    do_tdd_config_sim(gNB, mu);
	phy_init_nr_gNB(gNB);
	frame_length_complex_samples = frame_parms->samples_per_subframe;
	s_re = malloc(2 * sizeof(double *));
	s_im = malloc(2 * sizeof(double *));
	r_re = malloc(2 * sizeof(double *));
	r_im = malloc(2 * sizeof(double *));
	txdata = malloc(2 * sizeof(int *));

	for (i = 0; i < 2; i++) {
		s_re[i] = malloc(frame_length_complex_samples * sizeof(double));
		bzero(s_re[i], frame_length_complex_samples * sizeof(double));
		s_im[i] = malloc(frame_length_complex_samples * sizeof(double));
		bzero(s_im[i], frame_length_complex_samples * sizeof(double));
		r_re[i] = malloc(frame_length_complex_samples * sizeof(double));
		bzero(r_re[i], frame_length_complex_samples * sizeof(double));
		r_im[i] = malloc(frame_length_complex_samples * sizeof(double));
		bzero(r_im[i], frame_length_complex_samples * sizeof(double));
		txdata[i] = malloc(frame_length_complex_samples * sizeof(int));
		bzero(r_re[i], frame_length_complex_samples * sizeof(int)); // [hna] r_re should be txdata
	}

	if (pbch_file_fd != NULL) {
		load_pbch_desc(pbch_file_fd);
	}

	//configure UE
  PHY_VARS_NR_UE *UE = calloc(1, sizeof(*UE));
  PHY_VARS_NR_UE **uedata_ptrr = &UE;
  nrPHY_vars_UE_g = &uedata_ptrr;

  memcpy(&UE->frame_parms, frame_parms, sizeof(NR_DL_FRAME_PARMS));

	if (init_nr_ue_signal(UE, 1) != 0) {
		printf("Error at UE NR initialisation\n");
		exit(-1);
	}

  UE->nrLDPC_coding_interface = gNB->nrLDPC_coding_interface;

  NR_UE_DLSCH_t dlsch_ue[NR_MAX_NB_LAYERS > 4 ? 2 : 1] = {0};
  int num_codeword = NR_MAX_NB_LAYERS > 4 ? 2 : 1;
  nr_ue_dlsch_init(dlsch_ue, num_codeword, 5);
  for (int i=0; i < num_codeword; i++)
    dlsch_ue[0].rnti = n_rnti;
  nr_init_dl_harq_processes(UE->dl_harq_processes, 8, nb_rb);

  unsigned char harq_pid = 0; //dlsch->harq_ids[subframe];
  NR_gNB_DLSCH_t *dlsch = &gNB->dlsch[0];
  dlsch->freq_alloc = set_bitmap_from_start_size(0, nb_rb);
	unsigned int TBS = 8424;
	uint8_t nb_re_dmrs = 6;  // No data in dmrs symbol
	uint16_t length_dmrs = 1;
	uint8_t Nl = 1;
	uint8_t rvidx = 0;
	unsigned char mod_order = nr_get_Qm_dl(Imcs, mcs_table);
  uint16_t rate = nr_get_code_rate_dl(Imcs, mcs_table);
	unsigned int available_bits = nr_get_G(nb_rb, nb_symb_sch, nb_re_dmrs, length_dmrs, 0, mod_order, Nl);
	TBS = nr_compute_tbs(mod_order,rate, nb_rb, nb_symb_sch, nb_re_dmrs*length_dmrs, 0, 0, Nl);
	printf("available bits %u TBS %u mod_order %d\n", available_bits, TBS, mod_order);
  nfapi_nr_dl_tti_pdsch_pdu pdsch_pdu = {
      .pdsch_pdu_rel15 = {
        .rbSize = nb_rb,
        .NrOfSymbols = nb_symb_sch,
        .qamModOrder[0] = mod_order,
        .nrOfLayers = Nl,
        .TBSize[0] = TBS >> 3,
        .targetCodeRate[0] = rate,
        .NrOfCodewords = 1,
        .dmrsConfigType = NFAPI_NR_DMRS_TYPE1,
        .dlDmrsSymbPos = 4,
        .mcsIndex[0] = Imcs,
        .numDmrsCdmGrpsNoData = 1,
        .maintenance_parms_v3.tbSizeLbrmBytes = Tbslbrm,
        .maintenance_parms_v3.ldpcBaseGraph = get_BG(TBS, rate),
      },
  };
  dlsch->pdsch_pdu = &pdsch_pdu;
	double modulated_input[16 * 68 * 384]; // [hna] 16 segments, 68*Zc
	short channel_output_fixed[16 * 68 * 384];
	unsigned int errors_bit = 0;
	unsigned char test_input_bit[16 * 68 * 384];
	unsigned char estimated_output_bit[16 * 68 * 384];
	NR_UE_DLSCH_t *dlsch0_ue = &dlsch_ue[0];
  NR_DL_UE_HARQ_t *harq_process = &UE->dl_harq_processes[0][harq_pid];
  harq_process->first_rx = 1;
  harq_process->activated_frame = proc.frame_rx;
  harq_process->activated_slot = proc.nr_slot_rx;
  fapi_nr_dl_config_dlsch_pdu_rel15_t dlsch_config;
  dlsch_config.cw_info[0].mcs = Imcs;
  dlsch_config.mcs_table = mcs_table;
  dlsch_config.number_rbs = nb_rb;
  dlsch_config.cw_info[0].Nl = Nl;
  dlsch_config.cw_info[0].qamModOrder = mod_order;
  dlsch_config.cw_info[0].rv = rvidx;
  dlsch_config.cw_info[0].targetCodeRate = rate;
  dlsch_config.cw_info[0].TBS = TBS;
  dlsch_config.cw_info[0].ldpcBaseGraph = get_BG(TBS, rate);
  dlsch_config.dmrsConfigType = NFAPI_NR_DMRS_TYPE1;
  dlsch_config.dlDmrsSymbPos = 4;
  dlsch_config.n_dmrs_cdm_groups = 1;
  dlsch_config.tbslbrm = Tbslbrm;
  dlsch0_ue->cw_info = dlsch_config.cw_info[0];
  printf("harq process ue mcs = %d Qm = %d, symb %d\n", dlsch_config.cw_info[0].mcs, dlsch_config.cw_info[0].qamModOrder, nb_symb_sch);

  uint8_t test_input[TBS / 8 + 4]; // + 3 for CRC + 1 additional byte, see nr_dlsch_encoding()
  dlsch->pdu = test_input;
	for (i = 0; i < TBS / 8; i++)
		test_input[i] = (unsigned char) rand();

#ifdef DEBUG_NR_DLSCHSIM
	for (i = 0; i < TBS / 8; i++) printf("test_input[i]=%hhu \n",test_input[i]);
#endif

	// generate signal
  unsigned char output[nb_rb * NR_SYMBOLS_PER_SLOT * NR_NB_SC_PER_RB * NR_MAX_NB_LAYERS] __attribute__((aligned(64)));
  bzero(output, sizeof(output));
        if (input_fd == NULL) {
          nr_dlsch_encoding(gNB, 1, dlsch, frame, slot, output);
	}

	for (SNR = snr0; SNR < snr1 && !stop; SNR += snr_step) {
		n_errors = 0;
		n_false_positive = 0;

		for (trial = 0; trial < n_trials && !stop; trial++) {
			for (i = 0; i < available_bits; i++) {
#ifdef DEBUG_CODER
				if ((i&0xf)==0)
				printf("\ne %d..%d:    ",i,i+15);
#endif

				if ((output[i >> 3] & (1 << (i & 7))) == 0)
					modulated_input[i] = 1.0;        ///sqrt(2);  //QPSK
				else
					modulated_input[i] = -1.0;        ///sqrt(2);

				SNR_lin = pow(10, SNR / 10.0);
				sigma = 1.0 / sqrt(2 * SNR_lin);
				channel_output_fixed[i] = (short) quantize(sigma / 4.0 / 4.0,
						modulated_input[i] + sigma * gaussdouble(0.0, 1.0),
						qbits);

			}

#ifdef DEBUG_CODER
			printf("\n");
			exit(-1);
#endif

      int a_segments = MAX_NUM_NR_DLSCH_SEGMENTS; // number of segments to be allocated
      int num_rb = dlsch_config.number_rbs;
      if (num_rb != 273) {
        a_segments = a_segments*num_rb;
        a_segments = (a_segments/273)+1;
      }
      uint32_t dlsch_bytes = a_segments*1056;  // allocated bytes per segment
      __attribute__ ((aligned(32))) uint8_t b[dlsch_bytes];
      nr_dlsch_decoding(UE,
                        &proc,
                        dlsch0_ue,
                        0,
                        &dlsch_config,
                        channel_output_fixed,
                        b,
                        num_rb,
                        available_bits);

      if (dlsch0_ue->last_iteration_cnt > dlsch0_ue->max_ldpc_iterations)
				n_errors++;

			//count errors
			errors_bit = 0;

			for (i = 0; i < TBS; i++) {
				estimated_output_bit[i] = (b[i / 8] & (1 << (i & 7))) >> (i & 7);
				test_input_bit[i] = (test_input[i / 8] & (1 << (i & 7))) >> (i & 7); // Further correct for multiple segments

				if (estimated_output_bit[i] != test_input_bit[i]) {
					errors_bit++;
				}
			}

			if (errors_bit > 0) {
				n_false_positive++;
				if (n_trials == 1)
					printf("errors_bit %u (trial %d)\n", errors_bit, trial);
			}
		}

		printf("SNR %f, BLER %f (false positive %f)\n", SNR,
				(float) n_errors / (float) n_trials,
				(float) n_false_positive / (float) n_trials);

		if ((float) n_errors / (float) n_trials < target_error_rate) {
		  printf("PDSCH test OK\n");
		  break;
		}
	}

  free_channel_desc_scm(gNB2UE);

  free_config_request(&gNB->gNB_config);

  abortTpool(&gNB->threadPool);

  phy_free_nr_gNB(gNB);
  free(RC.gNB[0]);
  free(RC.gNB);

  free_nr_ue_dl_harq(UE->dl_harq_processes, 8, nb_rb);
  term_nr_ue_signal(UE);
  free(UE);

	for (i = 0; i < 2; i++) {
		free(s_re[i]);
		free(s_im[i]);
		free(r_re[i]);
		free(r_im[i]);
		free(txdata[i]);
	}

	free(s_re);
	free(s_im);
	free(r_re);
	free(r_im);
	free(txdata);

	if (output_fd)
		fclose(output_fd);

	if (input_fd)
		fclose(input_fd);

  end_configmodule(uniqCfg);
  loader_reset();
  logTerm();

  return (n_errors);
}
