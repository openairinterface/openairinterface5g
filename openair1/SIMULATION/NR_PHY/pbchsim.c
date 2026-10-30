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
#include "common/utils/LOG/log.h"
#include "common/utils/load_module_shlib.h"
#include "common/ran_context.h"
#include "common/utils/nr/nr_common.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/defs_gNB.h"
#include "PHY/MODULATION/phy_ofdm_mod.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/NR_UE_ESTIMATION/nr_estimation.h"
#include "SCHED_NR/sched_nr.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "openair1/SIMULATION/RF/rf.h"
#include "openair1/SIMULATION/NR_PHY/nr_unitary_defs.h"
#include <executables/softmodem-common.h>
#include <executables/nr-uesoftmodem.h>
#include "nfapi/oai_integration/vendor_ext.h"
#include "nfapi/open-nFAPI/fapi/inc/nr_fapi_p5_utils.h"
//#define DEBUG_NR_PBCHSIM

RAN_CONTEXT_t RC;

// needed for some functions
openair0_config_t openair0_cfg_g[MAX_CARDS] = {};

static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void) {
  return &softmodem_params;
}

nrUE_params_t nrUE_params={0};

nrUE_params_t *get_nrUE_params(void) {
  return &nrUE_params;
}

void init_downlink_harq_status(NR_DL_UE_HARQ_t *dl_harq) {}
NR_IF_Module_t *NR_IF_Module_init(int Mod_id)
{
  return (NULL);
}

void nr_fill_rx_indication(fapi_nr_rx_indication_t *rx_ind,
                           uint8_t pdu_type,
                           PHY_VARS_NR_UE *ue,
                           int cw_idx,
                           int harq_pid,
                           NR_UE_DLSCH_t *dlsch,
                           const UE_nr_rxtx_proc_t *proc,
                           void *typeSpecific)
{
}

configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv)
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  int i,aa,start_symbol;
  double sigma2, sigma2_dB=10,SNR,snr0=-2.0,snr1=2.0;
  double cfo=0;
  uint8_t snr1set=0;
  c16_t **txdata;
  double **s_re,**s_im,**r_re,**r_im;
  double ip =0.0;
  FILE *output_fd = NULL;
  int trial,n_trials=1,n_errors=0,n_errors_payload=0;
  int ret_test = 1;
  uint8_t transmission_mode = 1,n_tx=1,n_rx=1;
  uint16_t Nid_cell=0;
  uint64_t SSB_positions=0x01;
  int ssb_subcarrier_offset = 0;
  int ssb_scan_threads = 0;
  channel_desc_t *gNB2UE;
  FILE *input_fd=NULL,*pbch_file_fd=NULL;
  SCM_t channel_model=AWGN;//Rayleigh1_anticorr;
  int N_RB_DL=273,mu=1;
  unsigned char pbch_phase = 0;
  int frame=0;
  int frame_length_complex_samples;
  __attribute__((unused))
  int frame_length_complex_samples_no_prefix;
  NR_DL_FRAME_PARMS *frame_parms;
  int ret;
  int run_initial_sync=0;
  int loglvl=OAILOG_WARNING;
  float target_error_rate = 0.01;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_PBCHSIM] Error, configuration module init failed\n");
  }

  int c;
  while ((c = getopt(argc, argv, "--:O:c:F:g:hIL:m:M:n:N:o:P:R:s:S:x:y:z:")) != -1) {
    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    switch (c) {

    case 'c':
      ssb_subcarrier_offset = atoi(optarg);
      break;

    case 'F':
      input_fd = fopen(optarg,"r");
      if (input_fd==NULL) {
        printf("Problem with filename %s. Exiting.\n", optarg);
        exit(-1);
      }
      break;

    case 'g':
      switch((char)*optarg) {
      case 'A':
        channel_model=SCM_A;
        break;

      case 'B':
        channel_model=SCM_B;
        break;

      case 'C':
        channel_model=SCM_C;
        break;

      case 'D':
        channel_model=SCM_D;
        break;

      case 'E':
        channel_model=EPA;
        break;

      case 'F':
        channel_model=EVA;
        break;

      case 'G':
        channel_model=ETU;
        break;

      default:
        printf("Unsupported channel model! Exiting.\n");
        exit(-1);
      }

      break;

    case 'I':
      run_initial_sync=1;
      target_error_rate=0.1;
      break;

    case 'L':
      loglvl = atoi(optarg);
      break;

    case 'm':
      mu = atoi(optarg);
      break;

    case 'M':
      SSB_positions = atoi(optarg);
      break;

    case 'n':
      n_trials = atoi(optarg);
      break;

    case 'N':
      Nid_cell = atoi(optarg);
      break;

    case 'o':
      cfo = atof(optarg);
#ifdef DEBUG_NR_PBCHSIM
      printf("Setting CFO to %f Hz\n",cfo);
#endif
      break;

    case 'P':
      pbch_phase = atoi(optarg);
      if (pbch_phase>3)
        printf("Illegal PBCH phase (0-3) got %d\n",pbch_phase);
      break;

    case 'R':
      N_RB_DL = atoi(optarg);
      break;

    case 's':
      snr0 = atof(optarg);
#ifdef DEBUG_NR_PBCHSIM
      printf("Setting SNR0 to %f\n",snr0);
#endif
      break;

    case 'S':
      snr1 = atof(optarg);
      snr1set=1;
#ifdef DEBUG_NR_PBCHSIM
      printf("Setting SNR1 to %f\n",snr1);
#endif
      break;

    case 'x':
      transmission_mode=atoi(optarg);

      if ((transmission_mode!=1) && (transmission_mode!=2) && (transmission_mode!=6)) {
        printf("Unsupported transmission mode %d. Exiting.\n",transmission_mode);
        exit(-1);
      }

      break;

    case 'y':
      n_tx=atoi(optarg);
      if ((n_tx==0) || (n_tx>2)) {
        printf("Unsupported number of TX antennas %d. Exiting.\n", n_tx);
        exit(-1);
      }
      break;

    case 'z':
      n_rx=atoi(optarg);
      if ((n_rx==0) || (n_rx>2)) {
        printf("Unsupported number of RX antennas %d. Exiting.\n", n_rx);
        exit(-1);
      }
      break;

    case '?':
      printf("Unknown option or missing argument: -%c\n", optopt);
    case 'h':
    default:
      printf(
          "OAI_RNGSEED=xxx ./%s -F input_filename -g channel_mod -h(elp) -I(nitial sync) -L log_lvl -n n_frames -M SSBs -n frames "
          "-N cell_id -o FO -P phase -R RBs -s snr0 -S snr1 -x transmission_mode -y TXant -z RXant\n",
          argv[0]);
      printf("-c SSB subcarrier offset\n");
      printf("-F Input filename (.txt format) for RX conformance testing\n");
      printf("-g [A,B,C,D,E,F,G] Use 3GPP SCM (A,B,C,D) or 36-101 (E-EPA,F-EVA,G-ETU) models (ignores delay spread and Ricean factor)\n");
      printf("-h This message\n");
      printf("-I run initial sync with target error rate 0.1\n");
      printf("-L set the log level (-1 disable, 0 error, 1 warning, 2 info, 3 debug, 4 trace)\n");
      printf("-m Numerology index\n");
      printf("-M Multiple SSB positions in burst\n");
      printf("-n Number of frames to simulate\n");
      printf("-N Nid_cell\n");
      printf("-o Carrier frequency offset in Hz\n");
      printf("-P PBCH phase, allowed values 0-3\n");
      printf("-R N_RB_DL\n");
      printf("-s Starting SNR, runs from SNR0 to SNR0 + 10 dB if not -S given. If -n 1, then just SNR is simulated\n");
      printf("-S Ending SNR, runs from SNR0 to SNR1\n");
      printf("-x Transmission mode (1,2,6 for the moment)\n");
      printf("-y Number of TX antennas used in eNB\n");
      printf("-z Number of RX antennas used in UE\n");
      exit (-1);
      break;
    }
  }

  randominit();

  logInit();
  set_glog(loglvl);

  if (snr1set==0)
    snr1 = snr0+10;

  printf("Initializing gNodeB for mu %d, N_RB_DL %d\n",mu,N_RB_DL);


  RC.gNB = (PHY_VARS_gNB**) malloc(sizeof(PHY_VARS_gNB *));
  RC.gNB[0] = malloc16_clear(sizeof(*(RC.gNB[0])));
  PHY_VARS_gNB *gNB = RC.gNB[0];
  gNB->ofdm_offset_divisor = UINT_MAX;
  gNB->phase_comp = true;
  frame_parms = &gNB->frame_parms; //to be initialized I suppose (maybe not necessary for PBCH)
  frame_parms->nb_antennas_tx = n_tx;
  frame_parms->nb_antennas_rx = n_rx;
  frame_parms->nb_antenna_ports_gNB = n_tx;
  frame_parms->N_RB_DL = N_RB_DL;
  frame_parms->Nid_cell = Nid_cell;
  frame_parms->ssb_type = nr_ssb_type_C;
  frame_parms->freq_range = mu<2 ? FR1 : FR2;

  nr_phy_config_request_sim(gNB, N_RB_DL, N_RB_DL, mu, Nid_cell, SSB_positions);
  // TDD configuration
  gNB->gNB_config.tdd_table.tdd_period.value = 6;
  do_tdd_config_sim(gNB, mu);

  phy_init_nr_gNB(gNB);
  frame_parms->ssb_start_subcarrier = 12 * gNB->gNB_config.ssb_table.ssb_offset_point_a.value + ssb_subcarrier_offset;
  initFloatingCoresTpool(ssb_scan_threads, &nrUE_params.Tpool, false, "UE-tpool");

  int n_hf = 0;
  int cyclic_prefix_type = NFAPI_CP_NORMAL;

  double fs=0, eps;
  double scs = 30000;
  double txbw, rxbw;
  get_samplerate_and_bw(mu, N_RB_DL, frame_parms->threequarter_fs, &fs, &txbw, &rxbw);

  // cfo with respect to sub-carrier spacing
  eps = cfo/scs;

  // computation of integer and fractional FO to compare with estimation results
  int IFO;
  if(eps!=0.0){
	printf("Introducing a CFO of %lf relative to SCS of %d kHz\n",eps,(int)(scs/1000));
	if (eps>0)	
  	  IFO=(int)(eps+0.5);
	else
	  IFO=(int)(eps-0.5);
	printf("FFO = %lf; IFO = %d\n",eps-IFO,IFO);
  }

  gNB2UE = new_channel_desc_scm(n_tx, n_rx, channel_model, fs, 0, txbw, 300e-9, 0.0, CORR_LEVEL_LOW, 0, 0, 0, 0);

  if (gNB2UE==NULL) {
	printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  frame_length_complex_samples = frame_parms->samples_per_subframe*NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  frame_length_complex_samples_no_prefix = frame_parms->samples_per_subframe_wCP;

  s_re = malloc(2*sizeof(double*));
  s_im = malloc(2*sizeof(double*));
  r_re = malloc(2*sizeof(double*));
  r_im = malloc(2*sizeof(double*));
  txdata = calloc(2, sizeof(c16_t*));

  for (i=0; i<2; i++) {

    s_re[i] = malloc16_clear(frame_length_complex_samples*sizeof(double));
    s_im[i] = malloc16_clear(frame_length_complex_samples*sizeof(double));
    r_re[i] = malloc16_clear(frame_length_complex_samples*sizeof(double));
    r_im[i] = malloc16_clear(frame_length_complex_samples*sizeof(double));
    printf("Allocating %d samples for txdata\n",frame_length_complex_samples);
    txdata[i] = malloc16_clear(frame_length_complex_samples * sizeof(c16_t));
  }

  if (pbch_file_fd!=NULL) {
    load_pbch_desc(pbch_file_fd);
  }

  //configure UE
  PHY_VARS_NR_UE *UE = malloc16_clear(sizeof(*UE));
  memcpy(&UE->frame_parms,frame_parms,sizeof(UE->frame_parms));
  if (run_initial_sync==1)  UE->is_synchronized = 0;
  else                      UE->is_synchronized = 1;
                      
  if(eps!=0.0)
	UE->UE_fo_compensation = 1; // if a frequency offset is set then perform fo estimation and compensation

  if (init_nr_ue_signal(UE, 1) != 0) {
    printf("Error at UE NR initialisation\n");
    exit(-1);
  }

  // generate signal
  const uint32_t rxdataF_sz = UE->frame_parms.samples_per_slot_wCP;
  __attribute__ ((aligned(32))) c16_t rxdataF[UE->frame_parms.nb_antennas_rx][rxdataF_sz];
  nfapi_nr_dl_tti_ssb_pdu ssb_pdu[64] = {0};
  if (input_fd==NULL) {
    const int prb_mask_words = (frame_parms->N_RB_DL + 63) / 64;

    for (i=0; i<frame_parms->Lmax; i++) {
      if((SSB_positions >> i) & 0x01) {

        const int sc_offset = frame_parms->freq_range == FR1 ? ssb_subcarrier_offset<<mu : ssb_subcarrier_offset;
        const int prb_offset = frame_parms->freq_range == FR1 ? gNB->gNB_config.ssb_table.ssb_offset_point_a.value<<mu : gNB->gNB_config.ssb_table.ssb_offset_point_a.value << (mu - 2);
        ssb_pdu[i].ssb_pdu_rel15.bchPayload = 0x55dd33;
        ssb_pdu[i].ssb_pdu_rel15.SsbBlockIndex = i;
        ssb_pdu[i].ssb_pdu_rel15.SsbSubcarrierOffset = sc_offset;
        ssb_pdu[i].ssb_pdu_rel15.ssbOffsetPointA = prb_offset;

        start_symbol = nr_get_ssb_start_symbol(frame_parms,i);
        int slot = start_symbol/14;

        for (aa=0; aa<gNB->frame_parms.nb_antennas_tx; aa++)
          memset(gNB->common_vars.txdataF[aa], 0, frame_parms->samples_per_slot_wCP * sizeof(int32_t));

        nr_common_signal_procedures(gNB, frame, slot, &ssb_pdu[i], prb_mask_words);

        int samp = get_samples_slot_timestamp(frame_parms, slot);
        for (aa = 0; aa < gNB->frame_parms.nb_antennas_tx; aa++) {
          if (cyclic_prefix_type == 1) {
            for (int i = 0; i < 12; i++)
              fftshift_inverse_inplace(gNB->common_vars.txdataF[aa] + i * frame_parms->ofdm_symbol_size,
                                       frame_parms->N_RB_DL * NR_NB_SC_PER_RB,
                                       frame_parms->ofdm_symbol_size);

            PHY_ofdm_mod((int *)gNB->common_vars.txdataF[aa],
                         (int *)&txdata[aa][samp],
                         frame_parms->ofdm_symbol_size,
                         12,
                         frame_parms->nb_prefix_samples,
                         CYCLIC_PREFIX);
          } else {
            for (int i = 0; i < 14; i++)
              fftshift_inverse_inplace(gNB->common_vars.txdataF[aa] + i * frame_parms->ofdm_symbol_size,
                                       frame_parms->N_RB_DL * NR_NB_SC_PER_RB,
                                       frame_parms->ofdm_symbol_size);

            PHY_ofdm_mod((int *)gNB->common_vars.txdataF[aa],
                         (int *)&txdata[aa][samp],
                         frame_parms->ofdm_symbol_size,
                         1,
                         frame_parms->nb_prefix_samples0,
                         CYCLIC_PREFIX);

            PHY_ofdm_mod((int *)gNB->common_vars.txdataF[aa] + frame_parms->ofdm_symbol_size,
                         (int *)&txdata[aa][samp + frame_parms->nb_prefix_samples0 + frame_parms->ofdm_symbol_size],
                         frame_parms->ofdm_symbol_size,
                         13,
                         frame_parms->nb_prefix_samples,
                         CYCLIC_PREFIX);
          }
        }
      }
    }
    LOG_M("txsigF0.m", "txsF0", gNB->common_vars.txdataF[0], frame_parms->samples_per_slot_wCP, 1, 1);
    if (gNB->frame_parms.nb_antennas_tx > 1)
      LOG_M("txsigF1.m", "txsF1", gNB->common_vars.txdataF[1], frame_parms->samples_per_slot_wCP, 1, 1);

  } else {
    printf("Reading %d samples from file to antenna buffer %d\n",frame_length_complex_samples,0);
    UE->UE_fo_compensation = 1; // perform fo compensation when samples from file are used
    if (fread(txdata[0],
        sizeof(int32_t),
        frame_length_complex_samples,
        input_fd) != frame_length_complex_samples) {
      printf("error reading from file\n");
    }
  }

  LOG_M("txsig0.m","txs0", txdata[0],frame_length_complex_samples,1,1);
  if (gNB->frame_parms.nb_antennas_tx>1)
    LOG_M("txsig1.m","txs1", txdata[1],frame_length_complex_samples,1,1);

  if (output_fd) 
    fwrite(txdata[0],sizeof(int32_t),frame_length_complex_samples,output_fd);
  
  for (SNR = snr0; SNR < snr1 && !stop; SNR+=.2) {

    n_errors = 0;
    n_errors_payload = 0;

    for (trial = 0; trial < n_trials && !stop; trial++) {

      for (i=0; i<frame_length_complex_samples; i++) {
        for (aa=0; aa<frame_parms->nb_antennas_tx; aa++) {
          r_re[aa][i] = (double)txdata[aa][i].r;
          r_im[aa][i] = (double)txdata[aa][i].i;
        }
      }

      // multipath channel
      
      //AWGN
      sigma2_dB = 20*log10((double)AMP/4)-SNR;
      sigma2 = pow(10,sigma2_dB/10);

      if(eps!=0.0)
        rf_rx(r_re,  // real part of txdata
           r_im,  // imag part of txdata
           NULL,  // interference real part
           NULL, // interference imag part
           0,  // interference power
           frame_parms->nb_antennas_rx,  // number of rx antennas
           frame_length_complex_samples,  // number of samples in frame
           1.0e9/fs,   //sampling time (ns)
           cfo,	// frequency offset in Hz
           0.0, // drift (not implemented)
           0.0, // noise figure (not implemented)
           0.0, // rx gain in dB ?
           200, // 3rd order non-linearity in dB ?
           &ip, // initial phase
           30.0e3,  // phase noise cutoff in kHz
           -500.0, // phase noise amplitude in dBc
           0.0,  // IQ imbalance (dB),
	   0.0); // IQ phase imbalance (rad)

      c16_t *rxdata[frame_parms->nb_antennas_rx];
      for (aa = 0; aa < frame_parms->nb_antennas_rx; aa++)
        rxdata[aa] = malloc(frame_length_complex_samples * sizeof(**rxdata));
      for (i=0; i<frame_length_complex_samples; i++) {
        for (aa=0; aa<frame_parms->nb_antennas_rx; aa++) {
          rxdata[aa][i].r = (short)(r_re[aa][i] + sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0));
          rxdata[aa][i].i = (short)(r_im[aa][i] + sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0));
        }
      }

      if (n_trials==1) {
        LOG_M("rxsig0.m", "rxs0", rxdata[0], frame_parms->samples_per_frame, 1, 1);
        if (gNB->frame_parms.nb_antennas_tx > 1)
          LOG_M("rxsig1.m", "rxs1", rxdata[1], frame_parms->samples_per_frame, 1, 1);
      }
      if (UE->is_synchronized == 0) {
        UE_nr_rxtx_proc_t proc = {0};
        nr_gscn_info_t gscnInfo[MAX_GSCN_BAND] = {0};
        const int numGscn = 1;
        gscnInfo[0].ssbFirstSC = frame_parms->ssb_start_subcarrier;
        nr_initial_sync_t ret = nr_initial_sync(&proc, UE, frame_length_complex_samples, rxdata, gscnInfo, numGscn);
        printf("nr_initial_sync1 returns %s\n", ret.cell_detected ? "cell detected" : "cell not detected");
        if (!ret.cell_detected)
          n_errors++;
      }
      else {
        UE_nr_rxtx_proc_t proc={0};

        uint8_t ssb_index = 0;
        while (!((SSB_positions >> ssb_index) & 0x01))
          ssb_index++; // to select the first transmitted ssb
        UE->symbol_offset = nr_get_ssb_start_symbol(frame_parms, ssb_index);

        int ssb_slot = (UE->symbol_offset/14)+(n_hf*(frame_parms->slots_per_frame>>1));
        proc.nr_slot_rx = ssb_slot;
        proc.gNB_id = 0;
        int16_t pbch_e_rx[NR_POLAR_PBCH_E];
        uint8_t log2_maxh = 0;
        for (int i = UE->symbol_offset + 1; i < UE->symbol_offset + 4; i++) {
          nr_slot_fep(UE, frame_parms, proc.nr_slot_rx, i % frame_parms->symbols_per_slot, rxdataF, link_type_dl, 0, rxdata);
          __attribute__((aligned(32))) struct complex16 rxdataF_symb[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size];
          __attribute__((aligned(32))) struct complex16 dl_ch_estimates[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size];

          for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
            memcpy(rxdataF_symb[aarx],
                   &rxdataF[0][i * frame_parms->ofdm_symbol_size],
                   sizeof(c16_t) * frame_parms->ofdm_symbol_size);
            nr_pbch_channel_estimation(frame_parms,
                                       &UE->SL_UE_PHY_PARAMS,
                                       dl_ch_estimates[aarx],
                                       &proc,
                                       i - (UE->symbol_offset + 1),
                                       ssb_index % 8,
                                       n_hf,
                                       frame_parms->ssb_start_subcarrier,
                                       rxdataF_symb[aarx],
                                       false,
                                       frame_parms->Nid_cell);
          }
          nr_generate_pbch_llr(UE,
                               &proc,
                               frame_parms,
                               i - UE->symbol_offset,
                               ssb_index % 8,
                               Nid_cell,
                               frame_parms->ssb_start_subcarrier,
                               rxdataF_symb,
                               dl_ch_estimates,
                               pbch_e_rx,
                               &log2_maxh);
        }
        fapiPbch_t result;
        int ret_ssb_idx;
        int ret_symbol_offset;
        ret = nr_pbch_decode(UE,
                             frame_parms,
                             &proc,
                             ssb_index % 8,
                             Nid_cell,
                             pbch_e_rx,
                             &n_hf,
                             &ret_ssb_idx,
                             &ret_symbol_offset,
                             &result);

        if (ret == 0) {
          uint32_t xtra_byte = nr_pbch_extra_byte_generation(frame,
                                                             n_hf,
                                                             ssb_index,
                                                             gNB->gNB_config.ssb_table.ssb_subcarrier_offset.value,
                                                             frame_parms->Lmax);
          int payload_ret = (result.xtra_byte == xtra_byte);
          nfapi_nr_dl_tti_ssb_pdu_rel15_t *pdu = &ssb_pdu[ssb_index].ssb_pdu_rel15;
          for (int i = 0; i < 3; i++)
            payload_ret += (result.decoded_output[i] == ((pdu->bchPayload >> (8 * i)) & 0xff));
          if (payload_ret != 4)
            n_errors_payload++;
        }

        if (ret != 0)
          n_errors++;
      }
      for (aa = 0; aa < frame_parms->nb_antennas_rx; aa++)
        free(rxdata[aa]);
    } //noise trials
    printf("SNR %f: trials %d, n_errors_crc = %d, n_errors_payload %d\n", SNR,n_trials,n_errors,n_errors_payload);

    if (((float)n_errors/(float)n_trials <= target_error_rate) && (n_errors_payload==0)) {
      printf("PBCH test OK\n");
      ret_test = 0;
      break;
    }
      
    if (n_trials==1)
      break;

  } // NSR

  free_channel_desc_scm(gNB2UE);

  free_config_request(&gNB->gNB_config);

  phy_free_nr_gNB(gNB);
  free(RC.gNB[0]);
  free(RC.gNB);

  term_nr_ue_signal(UE);
  free(UE);

  for (i=0; i<2; i++) {
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

  loader_reset();
  logTerm();

  return ret_test;

}
