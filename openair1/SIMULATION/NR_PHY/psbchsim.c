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
#include "common/ran_context.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/defs_gNB.h"
#include "NR_MasterInformationBlockSidelink.h"
#include "openair1/SIMULATION/TOOLS/sim.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/RRC/LTE/rrc_vars.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/INIT/nr_phy_init.h"
#include "SIMULATION/RF/rf.h"
#include "common/utils/load_module_shlib.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "NR_SL-SSB-TimeAllocation-r16.h"
#include "nr-uesoftmodem.h"
#include "nr_unitary_defs.h"

uint64_t downlink_frequency[MAX_NUM_CCs][4];
int64_t uplink_frequency_offset[MAX_NUM_CCs][4];
THREAD_STRUCT thread_struct;
openair0_config_t openair0_cfg_g[MAX_CARDS] = {};

nrUE_params_t nrUE_params = {0};

nrUE_params_t *get_nrUE_params(void)
{
  return &nrUE_params;
}
//////////////////////////////////////////////////////////////////////////
static void prepare_mib_bits(uint8_t *buf, uint32_t frame_tx, uint32_t slot_tx)
{
  NR_MasterInformationBlockSidelink_t *sl_mib;
  asn_enc_rval_t enc_rval;

  void *buffer = (void *)buf;

  sl_mib = CALLOC(1, sizeof(NR_MasterInformationBlockSidelink_t));

  sl_mib->inCoverage_r16 = 0; // TRUE;

  // allocate buffer for 7 bits slotnumber
  sl_mib->slotIndex_r16.size = 1;
  sl_mib->slotIndex_r16.buf = CALLOC(1, sl_mib->slotIndex_r16.size);
  sl_mib->slotIndex_r16.bits_unused = sl_mib->slotIndex_r16.size * 8 - 7;
  sl_mib->slotIndex_r16.buf[0] = slot_tx << sl_mib->slotIndex_r16.bits_unused;

  sl_mib->directFrameNumber_r16.size = 2;
  sl_mib->directFrameNumber_r16.buf = CALLOC(1, sl_mib->directFrameNumber_r16.size);
  sl_mib->directFrameNumber_r16.bits_unused = sl_mib->directFrameNumber_r16.size * 8 - 10;
  sl_mib->directFrameNumber_r16.buf[0] = frame_tx >> (8 - sl_mib->directFrameNumber_r16.bits_unused);
  sl_mib->directFrameNumber_r16.buf[1] = frame_tx << sl_mib->directFrameNumber_r16.bits_unused;

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_MasterInformationBlockSidelink, NULL, (void *)sl_mib, buffer, 100);

  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);

  asn_DEF_NR_MasterInformationBlockSidelink.op->free_struct(&asn_DEF_NR_MasterInformationBlockSidelink,
                                                            sl_mib,
                                                            ASFM_FREE_EVERYTHING);
}

static int test_rx_mib(uint8_t *decoded_output, uint16_t frame, uint16_t slot)
{
  uint32_t sl_mib = *(uint32_t *)decoded_output;

  uint32_t fn = 0, sl = 0;
  fn = (((sl_mib & 0x0700) >> 1) | ((sl_mib & 0xFE0000) >> 17));
  sl = (((sl_mib & 0x010000) >> 10) | ((sl_mib & 0xFC000000) >> 26));

  printf("decoded output:%x, TX %d:%d, timing decoded from sl-MIB %d:%d\n", *(uint32_t *)decoded_output, frame, slot, fn, sl);

  if (frame == fn && slot == sl)
    return 0;

  return -1;
}

//////////////////////////////////////////////////////////////////////////

static void configure_NR_UE(PHY_VARS_NR_UE *UE, int mu, int N_RB)
{
  fapi_nr_config_request_t config;
  NR_DL_FRAME_PARMS *fp = &UE->frame_parms;

  config.ssb_config.scs_common = mu;
  config.cell_config.frame_duplex_type = TDD;
  config.carrier_config.dl_grid_size[mu] = N_RB;
  config.carrier_config.ul_grid_size[mu] = N_RB;
  config.carrier_config.dl_frequency = 3300000;
  config.carrier_config.uplink_frequency = 3300000;

  int band;
  if (mu == 1)
    band = 78;
  if (mu == 0) {
    band = 34;
    config.carrier_config.dl_frequency = 2010000;
    config.carrier_config.uplink_frequency = 2010000;
  }
  nr_init_frame_parms_ue(fp, &config, band);
  fp->ofdm_offset_divisor = 8;
  nr_dump_frame_parms(fp);

  if (init_nr_ue_signal(UE, 1) != 0) {
    printf("Error at UE NR initialisation\n");
    exit(-1);
  }
}

static void sl_init_frame_parameters(PHY_VARS_NR_UE *UE)
{
  NR_DL_FRAME_PARMS *nr_fp = &UE->frame_parms;
  NR_DL_FRAME_PARMS *sl_fp = &UE->SL_UE_PHY_PARAMS.sl_frame_params;

  memcpy(sl_fp, nr_fp, sizeof(NR_DL_FRAME_PARMS));
  sl_fp->ofdm_offset_divisor = 8; // What is this used for?

  sl_fp->att_tx = 1;
  sl_fp->att_rx = 1;
  // band47 //UL freq will be set to Sidelink freq
  sl_fp->sl_CarrierFreq = 5880000000;
  sl_fp->N_RB_SL = sl_fp->N_RB_DL;

  sl_fp->ssb_start_subcarrier = UE->SL_UE_PHY_PARAMS.sl_config.sl_bwp_config.sl_ssb_offset_point_a;
  sl_fp->Nid_cell = UE->SL_UE_PHY_PARAMS.sl_config.sl_sync_source.rx_slss_id;

#ifdef DEBUG_INIT
  LOG_I(PHY, "Dumping Sidelink Frame Parameters\n");
  nr_dump_frame_parms(sl_fp);
#endif
}

static void configure_SL_UE(PHY_VARS_NR_UE *UE, int mu, int N_RB, int ssb_offset, int slss_id)
{
  sl_nr_phy_config_request_t *config = &UE->SL_UE_PHY_PARAMS.sl_config;
  NR_DL_FRAME_PARMS *fp = &UE->SL_UE_PHY_PARAMS.sl_frame_params;

  config->sl_bwp_config.sl_scs = mu;
  config->sl_bwp_config.sl_ssb_offset_point_a = ssb_offset;
  config->sl_carrier_config.sl_bandwidth = N_RB;
  config->sl_carrier_config.sl_grid_size = 106;
  config->sl_sync_source.rx_slss_id = slss_id;

  sl_init_frame_parameters(UE);
  sl_ue_phy_init(UE);
  perform_symbol_rotation(fp->symbols_per_slot * fp->slots_per_frame / 10,
                          fp->numerology_index,
                          fp->sl_CarrierFreq,
                          fp->symbol_rotation[link_type_sl]);
  init_timeshift_rotation(fp->ofdm_symbol_size,
                          fp->N_RB_SL * NR_NB_SC_PER_RB,
                          fp->nb_prefix_samples,
                          fp->ofdm_offset_divisor,
                          fp->timeshift_symbol_rotation);
  LOG_I(PHY, "Dumping Sidelink Frame Parameters\n");
  nr_dump_frame_parms(fp);
}

static int freq_domain_loopback(PHY_VARS_NR_UE *UE_tx, PHY_VARS_NR_UE *UE_rx, int frame, int slot, nr_phy_data_tx_t *phy_data)
{
  sl_nr_ue_phy_params_t *sl_ue1 = &UE_tx->SL_UE_PHY_PARAMS;
  sl_nr_ue_phy_params_t *sl_ue2 = &UE_rx->SL_UE_PHY_PARAMS;

  printf("\nPSBCH SIM -F: %d:%d slss id TX UE:%d, RX UE:%d\n",
         frame,
         slot,
         phy_data->psbch_vars.tx_slss_id,
         sl_ue2->sl_config.sl_sync_source.rx_slss_id);

  NR_DL_FRAME_PARMS *fp = &sl_ue1->sl_frame_params;
  const int samplesF_per_slot = fp->symbols_per_slot * fp->ofdm_symbol_size;
  c16_t txdataF_buf[fp->nb_antennas_tx * samplesF_per_slot] __attribute__((aligned(32)));
  memset(txdataF_buf, 0, sizeof(txdataF_buf));
  c16_t *txdataF[fp->nb_antennas_tx]; /* workaround to be compatible with current txdataF usage in all tx procedures. */
  for (int i = 0; i < fp->nb_antennas_tx; ++i)
    txdataF[i] = &txdataF_buf[i * samplesF_per_slot];

  nr_tx_psbch(UE_tx, frame, slot, &phy_data->psbch_vars, txdataF);

  UE_nr_rxtx_proc_t proc;
  proc.frame_rx = frame;
  proc.nr_slot_rx = slot;

  uint8_t decoded_output[4] = {0};

  int psbch_e_rx_offset = 0;
  int16_t psbch_e_rx[SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2];
  int16_t psbch_unClipped[SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2];
  LOG_I(PHY, "DEBUG: HIJACKING DL CHANNEL ESTIMATES.\n");
  for (int s = 0; s < 14;) {
    __attribute__((aligned(32))) c16_t rxdataF[fp->nb_antennas_rx][fp->ofdm_symbol_size];
    __attribute__((aligned(32))) c16_t dl_ch_estimates[fp->nb_antennas_rx][fp->ofdm_symbol_size];
    /* Copy freq domain buffer from Tx to Rx */
    memcpy(rxdataF[0], &txdataF[0][s * fp->ofdm_symbol_size], sizeof(c16_t) * fp->ofdm_symbol_size);
    /* Fill perfect channel estimates */
    for (int j = 0; j < sl_ue2->sl_frame_params.ofdm_symbol_size; j++) {
      struct complex16 *dlch = dl_ch_estimates[0];
      dlch[j].r = 128;
      dlch[j].i = 0;
    }
    /* Extract and produce LLRs */
    nr_generate_psbch_llr(fp, rxdataF, dl_ch_estimates, s, &psbch_e_rx_offset, psbch_e_rx, psbch_unClipped);
    s = (s == 0) ? 5 : s + 1;
  }

  const int err_status = nr_psbch_decode(UE_rx,
                                         psbch_e_rx,
                                         &proc,
                                         psbch_e_rx_offset,
                                         sl_ue2->sl_config.sl_sync_source.rx_slss_id,
                                         NULL,
                                         decoded_output);

  int error_payload = 0;
  error_payload = test_rx_mib(decoded_output, frame, slot);

  if (err_status == 0 || error_payload == 0) {
    LOG_I(PHY, "---------PSBCH -F TEST OK.\n");
    return 0;
  }
  LOG_E(PHY, "--------PSBCH -F TEST NOK. FAIL.\n");
  return -1;
}

PHY_VARS_NR_UE *UE_TX; // for tx
PHY_VARS_NR_UE *UE_RX; // for rx

configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv)
{
  stop = false;
  __attribute__((unused)) struct sigaction oldaction;
  sigaction(SIGINT, &sigint_action, &oldaction);

  int test_freqdomain_loopback = 0, test_slss_search = 0;
  int frame = 5, slot = 10, frame_tx = 0, slot_tx = 0;
  int loglvl = OAILOG_INFO;
  uint16_t slss_id = 336, ssb_offset = 0;
  double snr1 = 2.0, snr0 = 2.0, SNR;
  double sigma2 = 0.0, sigma2_dB = 0.0;
  double cfo = 0, ip = 0.0;

  SCM_t channel_model = AWGN; // Rayleigh1_anticorr;
  int N_RB_DL = 106, mu = 1;

  uint16_t errors = 0, n_trials = 1;

  int frame_length_complex_samples;
  NR_DL_FRAME_PARMS *frame_parms;

  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == 0) {
    exit_fun("[NR_PSBCHSIM] Error, configuration module init failed\n");
  }

  randominit();

  int c;
  while ((c = getopt(argc, argv, "--:O:c:hn:o:s:FIL:N:R:S:T:")) != -1) {

    /* ignore long options starting with '--', option '-O' and their arguments that are handled by configmodule */
    /* with this opstring getopt returns 1 for non-option arguments, refer to 'man 3 getopt' */
    if (c == 1 || c == '-' || c == 'O')
      continue;

    switch (c) {
      case 'c':
        cfo = atof(optarg);
        printf("Setting CFO to %f Hz\n", cfo);
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

          default:
            printf("Unsupported channel model! Exiting.\n");
            exit(-1);
        }
        break;

      case 'n':
        n_trials = atoi(optarg);
        break;

      case 'o':
        ssb_offset = atoi(optarg);
        printf("SIDELINK PSBCH SIM: ssb offset from pointA:%d\n", ssb_offset);
        break;

      case 's':
        slss_id = atoi(optarg);
        printf("SIDELINK PSBCH SIM: slss_id from arg:%d\n", slss_id);
        AssertFatal(slss_id >= 0 && slss_id <= 671, "SLSS ID not within Range 0-671\n");
        break;

      case 'F':
        test_freqdomain_loopback = 1;
        break;

      case 'I':
        test_slss_search = 1;
        printf("SIDELINK PSBCH SIM: SLSS search will be tested\n");
        break;

      case 'L':
        loglvl = atoi(optarg);
        break;

      case 'N':
        snr0 = atoi(optarg);
        snr1 = snr0;
        printf("Setting SNR0 to %f. Test uses this SNR as target SNR\n", snr0);
        break;

      case 'R':
        N_RB_DL = atoi(optarg);
        printf("SIDELINK PSBCH SIM: N_RB_DL:%d\n", N_RB_DL);
        break;

      case 'S':
        snr1 = atof(optarg);
        printf("Setting SNR1 to %f. Test will run until this SNR as target SNR\n", snr1);
        AssertFatal(snr1 <= snr0, "Test runs SNR down, set snr1 to a lower value than %f\n", snr0);
        break;

      case 'T':
        frame = atoi(optarg);
        slot = atoi(argv[optind]);
        printf("PSBCH SIM: frame timing- %d:%d\n", frame, slot);
        break;

      case '?':
        printf("Unknown option or missing argument: -%c\n", optopt);
      case 'h':
      default:
        printf("\n\nSIDELINK PSBCH SIM OPTIONS LIST - hus:FL:T:\n");
        printf("-c Carrier frequency offset in Hz\n");
        printf("-F: Run PSBCH frequency domain loopback test of the samples\n");
        printf("-h: HELP\n");
        printf("-I: Sidelink SLSS search will be tested.\n");
        printf("-L: Set Log Level.\n");
        printf("-n Number of trials\n");
        printf("-N: Test with Noise. target SNR0 eg -N 10\n");
        printf("-o ssb offset from PointA - indicates ssb_start subcarrier\n");
        printf("-R N_RB_DL\n");
        printf("-s: set Sidelink sync id slss_id. ex -s 100\n");
        printf("-S Ending SNR, runs from SNR0 to SNR1\n");
        printf("-T: Frame,Slot to be sent in sl-MIB eg -T 4 2\n");
        return 1;
    }
  }

  randominit();

  logInit();
  set_glog(loglvl);

  double fs = 0, eps;
  double scs = 30000;
  double bw = 100e6;

  switch (mu) {
    case 1:
      scs = 30000;
      if (N_RB_DL == 217) {
        fs = 122.88e6;
        bw = 80e6;
      } else if (N_RB_DL == 245) {
        fs = 122.88e6;
        bw = 90e6;
      } else if (N_RB_DL == 273) {
        fs = 122.88e6;
        bw = 100e6;
      } else if (N_RB_DL == 106) {
        fs = 61.44e6;
        bw = 40e6;
      } else
        AssertFatal(1 == 0, "Unsupported numerology for mu %d, N_RB %d\n", mu, N_RB_DL);
      break;
    case 3:
      scs = 120000;
      if (N_RB_DL == 66) {
        fs = 122.88e6;
        bw = 100e6;
      } else
        AssertFatal(1 == 0, "Unsupported numerology for mu %d, N_RB %d\n", mu, N_RB_DL);
      break;
  }

  // cfo with respect to sub-carrier spacing
  eps = cfo / scs;

  // computation of integer and fractional FO to compare with estimation results
  int IFO;
  if (eps != 0.0) {
    printf("Introducing a CFO of %lf relative to SCS of %d kHz\n", eps, (int)(scs / 1000));
    if (eps > 0)
      IFO = (int)(eps + 0.5);
    else
      IFO = (int)(eps - 0.5);
    printf("FFO = %lf; IFO = %d\n", eps - IFO, IFO);
  }

  channel_desc_t *UE2UE;
  int n_tx = 1, n_rx = 1;
  UE2UE = new_channel_desc_scm(n_tx, n_rx, channel_model, fs, 0, bw, 300e-9, 0.0, CORR_LEVEL_LOW, 0, 0, 0, 0);

  if (UE2UE == NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  /*****configure UE *************************/
  UE_TX = calloc(1, sizeof(PHY_VARS_NR_UE));
  UE_RX = calloc(1, sizeof(PHY_VARS_NR_UE));
  LOG_I(PHY, "Configure UE-TX and sidelink UE-TX.\n");
  configure_NR_UE(UE_TX, mu, N_RB_DL);
  configure_SL_UE(UE_TX, mu, N_RB_DL, ssb_offset, 0xFFFF);

  LOG_I(PHY, "Configure UE-RX and sidelink UE-RX.\n");
  configure_NR_UE(UE_RX, mu, N_RB_DL);
  UE_RX->is_synchronized = (test_slss_search) ? 0 : 1;
  configure_SL_UE(UE_RX, mu, N_RB_DL, ssb_offset, slss_id);
  /*****************************************/
  sl_nr_ue_phy_params_t *sl_uetx = &UE_TX->SL_UE_PHY_PARAMS;
  sl_nr_ue_phy_params_t *sl_uerx = &UE_RX->SL_UE_PHY_PARAMS;
  frame_parms = &sl_uetx->sl_frame_params;
  frame_tx = frame % 1024;
  slot_tx = slot % frame_parms->slots_per_frame;

  frame_length_complex_samples = frame_parms->samples_per_subframe * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;

  double **s_re, **s_im, **r_re, **r_im;
  s_re = malloc(2 * sizeof(double *));
  s_im = malloc(2 * sizeof(double *));
  r_re = malloc(2 * sizeof(double *));
  r_im = malloc(2 * sizeof(double *));

  s_re[0] = malloc16_clear(frame_length_complex_samples * sizeof(double));
  s_im[0] = malloc16_clear(frame_length_complex_samples * sizeof(double));
  r_re[0] = malloc16_clear(frame_length_complex_samples * sizeof(double));
  r_im[0] = malloc16_clear(frame_length_complex_samples * sizeof(double));

  if (eps != 0.0)
    UE_RX->UE_fo_compensation = 1; // if a frequency offset is set then perform fo estimation and compensation

  UE_nr_rxtx_proc_t proc;
  proc.frame_tx = frame;
  proc.nr_slot_tx = slot;
  nr_phy_data_tx_t phy_data_tx;
  phy_data_tx.psbch_vars.tx_slss_id = slss_id;

  uint8_t sl_mib[4] = {0};
  prepare_mib_bits(sl_mib, frame, slot);
  memcpy(phy_data_tx.psbch_vars.psbch_payload, sl_mib, 4);

  phy_data_tx.sl_tx_action = SL_NR_CONFIG_TYPE_TX_PSBCH;
  proc.frame_rx = frame;
  proc.nr_slot_rx = slot;
  nr_phy_data_t phy_data_rx;
  phy_data_rx.sl_rx_action = SL_NR_CONFIG_TYPE_RX_PSBCH;

  if (test_freqdomain_loopback) {
    errors += freq_domain_loopback(UE_TX, UE_RX, frame_tx, slot_tx, &phy_data_tx);
  }

  printf("\nSidelink TX UE - Frame.Slot %d.%d SLSS id:%d\n", frame, slot, phy_data_tx.psbch_vars.tx_slss_id);
  printf("Sidelink RX UE - Frame.Slot %d.%d SLSS id:%d\n",
         proc.frame_rx,
         proc.nr_slot_rx,
         sl_uerx->sl_config.sl_sync_source.rx_slss_id);
  int slot_start = get_samples_slot_timestamp(frame_parms, slot);
  c16_t *tx[frame_parms->nb_antennas_rx];
  for (int i = 0; i < frame_parms->nb_antennas_rx; i++)
    tx[i] = UE_TX->common_vars.txData[i] + slot_start;
  phy_procedures_nrUE_SL_TX(UE_TX, &proc, &phy_data_tx, tx);

  for (SNR = snr0; SNR >= snr1 && !stop; SNR -= 1) {
    for (int trial = 0; trial < n_trials && !stop; trial++) {
      for (int i = 0; i < frame_length_complex_samples; i++) {
        for (int aa = 0; aa < frame_parms->nb_antennas_tx; aa++) {
          struct complex16 *txdata_ptr = (struct complex16 *)&UE_TX->common_vars.txData[aa][i];
          r_re[aa][i] = (double)txdata_ptr->r;
          r_im[aa][i] = (double)txdata_ptr->i;
        }
      }

      LOG_M("txData0.m", "txd0", UE_TX->common_vars.txData[0], frame_parms->samples_per_frame, 1, 1);

      // AWGN
      sigma2_dB = 20 * log10((double)AMP / 4) - SNR;
      sigma2 = pow(10, sigma2_dB / 10);

      if (eps != 0.0) {
        rf_rx(r_re, // real part of txdata
              r_im, // imag part of txdata
              NULL, // interference real part
              NULL, // interference imag part
              0, // interference power
              frame_parms->nb_antennas_rx, // number of rx antennas
              frame_length_complex_samples, // number of samples in frame
              1.0e9 / fs, // sampling time (ns)
              cfo, // frequency offset in Hz
              0.0, // drift (not implemented)
              0.0, // noise figure (not implemented)
              0.0, // rx gain in dB ?
              200, // 3rd order non-linearity in dB ?
              &ip, // initial phase
              30.0e3, // phase noise cutoff in kHz
              -500.0, // phase noise amplitude in dBc
              0.0, // IQ imbalance (dB),
              0.0); // IQ phase imbalance (rad)
      }

      for (int i = 0; i < frame_length_complex_samples; i++) {
        for (int aa = 0; aa < frame_parms->nb_antennas_rx; aa++) {
          UE_RX->common_vars.rxdata[aa][i].r = (short)(r_re[aa][i] + sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0));
          UE_RX->common_vars.rxdata[aa][i].i = (short)(r_im[aa][i] + sqrt(sigma2 / 2) * gaussdouble(0.0, 1.0));
        }
      }

      if (UE_RX->is_synchronized == 0) {
        nr_initial_sync_t ret = {false, 0};
        UE_nr_rxtx_proc_t proc = {0};
        // Should not have SLSS id configured. Search should find SLSS id from TX UE
        UE_RX->SL_UE_PHY_PARAMS.sl_config.sl_sync_source.rx_slss_id = 0xFFFF;
        ret = sl_nr_slss_search(UE_RX, &proc, 1, frame_length_complex_samples, UE_RX->common_vars.rxdata);
        printf("Sidelink SLSS search returns status:%d, rx_offset:%d\n", ret.cell_detected, ret.rx_offset);
        if (!ret.cell_detected)
          sl_uerx->psbch.rx_errors = 1;
        else {
          AssertFatal(UE_RX->SL_UE_PHY_PARAMS.sync_params.N_sl_id == slss_id,
                      "DETECTED INCORRECT SLSS ID in SEARCH.CHECK id:%d\n",
                      UE_RX->SL_UE_PHY_PARAMS.sync_params.N_sl_id);
          sl_uerx->psbch.rx_ok = 1;
        }
      } else
        psbch_pscch_processing(UE_RX, &proc, &phy_data_rx);

    } // noise trials

    printf("Runs:%d SNR %f: SLSS Search:%d crc ERRORs = %d, OK = %d\n",
           n_trials,
           SNR,
           !UE_RX->is_synchronized,
           sl_uerx->psbch.rx_errors,
           sl_uerx->psbch.rx_ok);
    errors += sl_uerx->psbch.rx_errors;
    sl_uerx->psbch.rx_errors = 0;
    sl_uerx->psbch.rx_ok = 0;

  } // NSR

  if (errors == 0)
    LOG_I(PHY, "PSBCH test OK\n");
  else
    LOG_E(PHY, "PSBCH test NOT OK\n");

  free_channel_desc_scm(UE2UE);

  free(s_re[0]);
  free(s_im[0]);
  free(r_re[0]);
  free(r_im[0]);
  free(s_re);
  free(s_im);
  free(r_re);
  free(r_im);

  term_nr_ue_signal(UE_TX);
  term_nr_ue_signal(UE_RX);

  free(UE_TX);
  free(UE_RX);
  logTerm();
  loader_reset();

  return errors;
}
