/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <math.h>
#include <unistd.h>
#include "common/cmake_defs.h"
#include "PHY/types.h"
#include "PHY/defs_common.h"
#include "PHY/defs_eNB.h"
#include "PHY/defs_UE.h"
#include "PHY/phy_vars.h"
#include "PHY/INIT/phy_init.h"
#include "PHY/LTE_TRANSPORT/transport_proto.h"
#include "PHY/LTE_UE_TRANSPORT/transport_proto_ue.h"
#include "SCHED/sched_eNB.h"
#include "SCHED_UE/sched_UE.h"
#include "SIMULATION/TOOLS/sim.h"
#include "unitary_defs.h"
#include "dummy_functions.c"
#include "nfapi/oai_integration/vendor_ext.h"
#include "common/config/config_load_configmodule.h"
#include "executables/thread-common.h"
#include "executables/lte-softmodem.h"
#include "common/ran_context.h"
#include "PHY/LTE_ESTIMATION/lte_estimation.h"
#include "openair1/PHY/LTE_TRANSPORT/dlsch_tbs_full.h"
#include "PHY/phy_extern.h"

const char *__asan_default_options()
{
  /* don't do leak checking in ulsim, not finished yet */
  return "detect_leaks=0";
}

#define inMicroS(a) (((double)(a))/(1000.0))
//#define MCS_COUNT 23//added for PHY abstraction
#include <openair1/SIMULATION/LTE_PHY/common_sim.h>
channel_desc_t *eNB2UE[NUMBER_OF_eNB_MAX][NUMBER_OF_UE_MAX];
channel_desc_t *UE2eNB[NUMBER_OF_UE_MAX][NUMBER_OF_eNB_MAX];
node_desc_t *enb_data[NUMBER_OF_eNB_MAX];
node_desc_t *ue_data[NUMBER_OF_UE_MAX];

THREAD_STRUCT thread_struct;
nfapi_ue_release_request_body_t release_rntis;

char title[255];

/*the following parameters are used to control the processing times*/
int n_tx_dropped = 0; /*!< \brief initial max process time for tx */
int n_rx_dropped = 0; /*!< \brief initial max process time for rx */
RAN_CONTEXT_t RC;
extern void fep_full(RU_t *ru, int subframe);
extern void ru_fep_full_2thread(RU_t *ru, int subframe);
//extern void eNB_fep_full(PHY_VARS_eNB *eNB,L1_rxtx_proc_t *proc);
//extern void eNB_fep_full_2thread(PHY_VARS_eNB *eNB,L1_rxtx_proc_t *proc);

nfapi_dl_config_request_t DL_req;
nfapi_ul_config_request_t UL_req;
nfapi_hi_dci0_request_t HI_DCI0_req;
nfapi_ul_config_request_pdu_t ul_config_pdu_list[MAX_NUM_DL_PDU];
nfapi_tx_request_pdu_t tx_pdu_list[MAX_NUM_TX_REQUEST_PDU];
nfapi_tx_request_t TX_req;
Sched_Rsp_t sched_resp;


void
fill_nfapi_ulsch_config_request(nfapi_ul_config_request_pdu_t *ul_config_pdu,
                                uint8_t                        cqi_req,
                                uint8_t                        p_eNB,
                                uint8_t                        cqi_ReportModeAperiodic,
                                uint8_t                        betaOffset_CQI_Index,
                                uint8_t                        betaOffset_RI_Index,
                                uint8_t                        dl_cqi_pmi_size,
                                uint8_t                        tmode,
                                uint32_t                       handle,
                                uint16_t                       rnti,
                                uint8_t                        resource_block_start,
                                uint8_t                        number_of_resource_blocks,
                                uint8_t                        modulation_type,
                                uint8_t                        cyclic_shift_2_for_drms,
                                uint8_t                        frequency_hopping_enabled_flag,
                                uint8_t                        frequency_hopping_bits,
                                uint8_t                        new_data_indication,
                                uint8_t                        redundancy_version,
                                uint8_t                        harq_process_number,
                                uint8_t                        ul_tx_mode,
                                uint8_t                        current_tx_nb,
                                uint8_t                        n_srs,
                                uint16_t                       size) {
  memset((void *) ul_config_pdu, 0, sizeof(nfapi_ul_config_request_pdu_t));
  ul_config_pdu->pdu_type                                                    = NFAPI_UL_CONFIG_ULSCH_PDU_TYPE;
  ul_config_pdu->pdu_size                                                    = (uint8_t) (2 + sizeof(nfapi_ul_config_ulsch_pdu));
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.tl.tag                             = NFAPI_UL_CONFIG_REQUEST_ULSCH_PDU_REL8_TAG;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.handle                             = handle;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.rnti                               = rnti;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.resource_block_start               = resource_block_start;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.number_of_resource_blocks          = number_of_resource_blocks;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.modulation_type                    = modulation_type;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.cyclic_shift_2_for_drms            = cyclic_shift_2_for_drms;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.frequency_hopping_enabled_flag     = frequency_hopping_enabled_flag;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.frequency_hopping_bits             = frequency_hopping_bits;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.new_data_indication                = new_data_indication;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.redundancy_version                 = redundancy_version;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.harq_process_number                = harq_process_number;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.ul_tx_mode                         = ul_tx_mode;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.current_tx_nb                      = current_tx_nb;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.n_srs                              = n_srs;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.size                               = size;

  //printf("Filling ul_config_pdu : Q %d, TBS %d, rv %d, ndi %d\n", modulation_type,size,redundancy_version,new_data_indication);

  if (cqi_req == 1) {
    // Add CQI portion
    ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_CQI_RI_PDU_TYPE;
    ul_config_pdu->pdu_size = (uint8_t) (2 + sizeof(nfapi_ul_config_ulsch_cqi_ri_pdu));
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.tl.tag = NFAPI_UL_CONFIG_REQUEST_CQI_RI_INFORMATION_REL9_TAG;
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.report_type = 1;
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.number_of_cc = 1;
    LOG_D(MAC, "report_type %d\n",ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.report_type);

    if (p_eNB <= 2
        && (tmode == 3 || tmode == 4 || tmode == 8 || tmode == 9 || tmode == 10))
      ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].ri_size = 1;
    else if (p_eNB <= 2) ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].ri_size = 0;
    else if (p_eNB == 4) ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].ri_size = 2;

    for (int ri = 0;
         ri <  (1 << ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].ri_size);
         ri++)
      ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].dl_cqi_pmi_size[ri] =  dl_cqi_pmi_size;

    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.delta_offset_cqi = betaOffset_CQI_Index;
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.delta_offset_ri  = betaOffset_RI_Index;
  }
}

void fill_ulsch_dci(PHY_VARS_eNB *eNB,
                    int frame,
                    int subframe,
                    Sched_Rsp_t *sched_resp,
                    uint16_t rnti,
                    void *UL_dci,
                    int first_rb,
                    int nb_rb,
                    int mcs,
                    int modulation_type,
                    int ndi,
                    int TBS,
                    int cqi_flag,
                    uint8_t beta_CQI,
                    uint8_t beta_RI,
                    uint8_t cqi_size) {
  nfapi_ul_config_request_body_t *ul_req=&sched_resp->UL_req->ul_config_request_body;
  int harq_pid = ((frame*10)+subframe)&7;

  //printf("ulsch in frame %d, subframe %d => harq_pid %d, mcs %d, ndi %d\n",frame,subframe,harq_pid,mcs,ndi);

  switch (eNB->frame_parms.N_RB_UL) {
    case 6:
      break;

    case 25:
      if (eNB->frame_parms.frame_type == TDD) {
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->type    = 0;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //printf("nb_rb %d/%d, rballoc %d (dci %x)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_5MHz_TDD_1_6_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci);
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->mcs     = mcs;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->ndi     = ndi;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->TPC     = 0;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->cshift  = 0;
        ((DCI0_5MHz_TDD_1_6_t *)UL_dci)->dai     = 1;
      } else {
        ((DCI0_5MHz_FDD_t *)UL_dci)->type    = 0;
        ((DCI0_5MHz_FDD_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //      printf("nb_rb %d/%d, rballoc %d (dci %x) (dcip %p)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_5MHz_FDD_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci,UL_dci);
        ((DCI0_5MHz_FDD_t *)UL_dci)->mcs     = mcs;
        ((DCI0_5MHz_FDD_t *)UL_dci)->ndi     = ndi;
        ((DCI0_5MHz_FDD_t *)UL_dci)->TPC     = 0;
        ((DCI0_5MHz_FDD_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_5MHz_FDD_t *)UL_dci)->cshift  = 0;
      }

      break;

    case 50:
      if (eNB->frame_parms.frame_type == TDD) {
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->type    = 0;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //      printf("nb_rb %d/%d, rballoc %d (dci %x)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_10MHz_TDD_1_6_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci);
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->mcs     = mcs;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->ndi     = ndi;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->TPC     = 0;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->cshift  = 0;
        ((DCI0_10MHz_TDD_1_6_t *)UL_dci)->dai     = 1;
      } else {
        ((DCI0_10MHz_FDD_t *)UL_dci)->type    = 0;
        ((DCI0_10MHz_FDD_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //printf("nb_rb %d/%d, rballoc %d (dci %x)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_10MHz_FDD_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci);
        ((DCI0_10MHz_FDD_t *)UL_dci)->mcs     = mcs;
        ((DCI0_10MHz_FDD_t *)UL_dci)->ndi     = ndi;
        ((DCI0_10MHz_FDD_t *)UL_dci)->TPC     = 0;
        ((DCI0_10MHz_FDD_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_10MHz_FDD_t *)UL_dci)->cshift  = 0;
      }

      break;

    case 100:
      if (eNB->frame_parms.frame_type == TDD) {
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->type    = 0;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //      printf("nb_rb %d/%d, rballoc %d (dci %x)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_20MHz_TDD_1_6_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci);
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->mcs     = mcs;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->ndi     = ndi;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->TPC     = 0;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->cshift  = 0;
        ((DCI0_20MHz_TDD_1_6_t *)UL_dci)->dai     = 1;
      } else {
        ((DCI0_20MHz_FDD_t *)UL_dci)->type    = 0;
        ((DCI0_20MHz_FDD_t *)UL_dci)->rballoc = computeRIV(eNB->frame_parms.N_RB_UL,first_rb,nb_rb); // 12 RBs from position 8
        //   printf("nb_rb %d/%d, rballoc %d (dci %x) (UL_dci %p)\n",nb_rb,eNB->frame_parms.N_RB_UL,((DCI0_20MHz_FDD_t*)UL_dci)->rballoc,*(uint32_t *)UL_dci,(void*)UL_dci);
        ((DCI0_20MHz_FDD_t *)UL_dci)->mcs     = mcs;
        ((DCI0_20MHz_FDD_t *)UL_dci)->ndi     = ndi;
        ((DCI0_20MHz_FDD_t *)UL_dci)->TPC     = 0;
        ((DCI0_20MHz_FDD_t *)UL_dci)->cqi_req = cqi_flag&1;
        ((DCI0_20MHz_FDD_t *)UL_dci)->cshift  = 0;
      }

      break;

    default:
      break;
  }

  fill_nfapi_ulsch_config_request(&ul_req->ul_config_pdu_list[0],
                                  cqi_flag&1,
                                  1,  // p_eNB
                                  0,  // reportmode Aperiodic
                                  beta_CQI,
                                  beta_RI,
                                  cqi_size,
                                  //cc,
                                  //UE_template->physicalConfigDedicated,
                                  1,
                                  0,
                                  14,     // rnti
                                  first_rb, // resource_block_start
                                  nb_rb,  // number_of_resource_blocks
                                  modulation_type,
                                  0,  // cyclic_shift_2_for_drms
                                  0,  // frequency_hopping_enabled_flag
                                  0,  // frequency_hopping_bits
                                  ndi,  // new_data_indication
                                  mcs>28?(mcs-28):0,  // redundancy_version
                                  harq_pid, // harq_process_number
                                  0,  // ul_tx_mode
                                  0,  // current_tx_nb
                                  0,  // n_srs
                                  TBS);
  sched_resp->UL_req->header.message_id = NFAPI_UL_CONFIG_REQUEST;
  ul_req->number_of_pdus=1;
  ul_req->tl.tag = NFAPI_UL_CONFIG_REQUEST_BODY_TAG;
}

enum eTypes { eBool, eInt, eFloat, eText };
static int verbose,help,disable_bundling=0,cqi_flag=0, extended_prefix_flag=0, test_perf=0, subframe=3, transmission_m=1,n_rx=1;
configmodule_interface_t *uniqCfg = NULL;
int main(int argc, char **argv) {
  int i,j,aa,u;
  PHY_VARS_eNB *eNB;
  PHY_VARS_UE *UE;
  RU_t *ru;
  int aarx,aatx;
  double channelx,channely;
  static double sigma2, sigma2_dB=10,SNR,SNR2=0,snr0=-2.0,snr1,SNRmeas,rate,saving_bler=0;
  static double input_snr_step=.2,snr_int=30;
  double blerr;
  int rvidx[8]= {0,2,3,1,0,2,3,1};
  int **txdata;
  LTE_DL_FRAME_PARMS *frame_parms;
  double s_re0[30720],s_im0[30720],r_re0[30720],r_im0[30720];
  double s_re1[30720],s_im1[30720],r_re1[30720],r_im1[30720];
  double r_re2[30720],r_im2[30720];
  double r_re3[30720],r_im3[30720];
  double *s_re[NB_ANTENNAS_TX]= {s_re0,s_re1, NULL, NULL};
  double *s_im[NB_ANTENNAS_TX]= {s_im0,s_im1, NULL, NULL};
  double *r_re[NB_ANTENNAS_RX]= {r_re0,r_re1,r_re2,r_re3};
  double *r_im[NB_ANTENNAS_RX]= {r_im0,r_im1,r_im2,r_im3};
  double forgetting_factor=0.0; //in [0,1] 0 means a new channel every time, 1 means keep the same channel
  double iqim=0.0;
  int cqi_error,cqi_errors,ack_errors,cqi_crc_falsepositives,cqi_crc_falsenegatives;
  int ch_realization;
  int eNB_id = 0;
  int chMod = 0 ;
  int UE_id = 0;
  static int nb_rb=25,first_rb=0,mcs=0,round=0;
  //unsigned char l;
  SCM_t channel_model=Rice1;
  unsigned char *input_buffer=0,harq_pid;
  unsigned short input_buffer_length;
  unsigned int ret;
  unsigned int coded_bits_per_codeword,nsymb;
  unsigned int tx_lev = 0, trials, errs[6] = {0}, round_trials[4] = {0};
  int tx_lev_dB = 0;
  FILE *bler_fd=NULL;
  char bler_fname[512];
  FILE *time_meas_fd=NULL;
  char time_meas_fname[256];
  FILE *input_fdUL=NULL,*trch_out_fdUL=NULL;
  //  unsigned char input_file=0;
  char input_val_str[50],input_val_str2[50];
  //  FILE *rx_frame_file;
  FILE *csv_fdUL=NULL;
  char csv_fname[512];
  static int n_frames=5000;
  static int n_ch_rlz = 1;
  static int abstx = 0;
  int hold_channel=0;
  channel_desc_t *UE2eNB;
  //uint8_t control_only_flag = 0;
  static int delay = 0;
  static double maxDoppler = 0.0;
  static int srs_flag = 0;
  static int N_RB_DL=25,osf=1;
  //uint8_t cyclic_shift = 0;
  static uint8_t beta_ACK=0,beta_RI=0,beta_CQI=2,cqi_size=11;
  static uint8_t tdd_config=3,frame_type=FDD;
  static int N0=30;
  static double tx_gain=1.0;
  int iter_trials;
  uint32_t UL_alloc_pdu;
  int dump_perf=0;
  static int dump_table =0;
  double effective_rate=0.0;
  char channel_model_input[10]= {0};
  static int max_turbo_iterations=4;
  int nb_rb_set = 0;
  int sf;
  static int threequarter_fs=0;
  int ndi;
  sched_resp.DL_req = &DL_req;
  sched_resp.UL_req = &UL_req;
  sched_resp.HI_DCI0_req = &HI_DCI0_req;
  sched_resp.TX_req = &TX_req;
  memset((void *)&DL_req,0,sizeof(DL_req));
  memset((void *)&UL_req,0,sizeof(UL_req));
  memset((void *)&HI_DCI0_req,0,sizeof(HI_DCI0_req));
  memset((void *)&TX_req,0,sizeof(TX_req));
  UL_req.ul_config_request_body.ul_config_pdu_list = ul_config_pdu_list;
  TX_req.tx_request_body.tx_pdu_list = tx_pdu_list;
  set_parallel_conf("PARALLEL_SINGLE_THREAD");
  uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  AssertFatal(uniqCfg != NULL, "Cannot load configuration module, exiting\n");
  logInit();
  set_glog(OAILOG_INFO);
  // enable these lines if you need debug info
  // however itti will catch all signals, so ctrl-c won't work anymore
  // alternatively you can disable ITTI completely in CMakeLists.txt
  //  set_comp_log(PHY,LOG_DEBUG,LOG_HIGH,1);
  //  set_glog(LOG_DEBUG,LOG_HIGH);
  //hapZEbm:n:Y:X:x:s:w:e:q:d:D:O:c:r:i:f:y:c:oA:C:R:g:N:l:S:T:QB:PI:LF
  static paramdef_t options[] = {
    { "awgn", "Use AWGN channel and not multipath", PARAMFLAG_BOOL, .strptr=NULL, .defintval=0, TYPE_INT, 0, NULL, NULL },
    { "BnbRBs", "The LTE bandwith in RBs (100 is 20MHz)",0, .iptr=&N_RB_DL,  .defintval=25, TYPE_INT, 0 },
    { "mcs", "The MCS to use", 0, .iptr=&mcs,  .defintval=10, TYPE_INT, 0 },
    { "nb_frame", "number of frame in a test",0, .iptr=&n_frames,  .defintval=1, TYPE_INT, 0 },
    { "snr", "starting snr", 0, .dblptr=&snr0,  .defdblval=-2.9, TYPE_DOUBLE, 0 },
    { "wsnrInterrupt", "snr int ?", 0, .dblptr=&snr_int,  .defdblval=30, TYPE_DOUBLE, 0 },
    { "e_snr_step", "step increasing snr",0, .dblptr=&input_snr_step,  .defdblval=0.2, TYPE_DOUBLE, 0 },
    { "rb_dynamic", "number of rb in dynamic allocation",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "first_rb", "first rb used in dynamic allocation",0, .iptr=&first_rb,  .defintval=0, TYPE_INT, 0 },
    { "osrs", "enable srs generation",PARAMFLAG_BOOL, .iptr=&srs_flag,  .defintval=0, TYPE_INT, 0 },
    { "gchannel", "[A:M] Use 3GPP 25.814 SCM-A/B/C/D('A','B','C','D') or 36-101 EPA('E'), EVA ('F'),ETU('G') models (ignores delay spread and Ricean factor), Rayghleigh8 ('H'), Rayleigh1('I'), Rayleigh1_corr('J'), Rayleigh1_anticorr ('K'),  Rice8('L'), Rice1('M')",0, .strptr=NULL,  .defstrval=NULL, TYPE_STRING, 0 },
    { "delay_chan", "Channel delay",0, .iptr=&delay,  .defintval=0, TYPE_INT, 0 },
    { "Doppler", "Maximum doppler shift",0, .dblptr=&maxDoppler,  .defdblval=0.0, TYPE_DOUBLE, 0 },
    { "Zdump", "dump table",PARAMFLAG_BOOL,  .iptr=&dump_table, .defintval=0, TYPE_INT, 0 },
    { "Lparallel", "Enable parallel execution",0, .strptr=NULL,  .defstrval=NULL, TYPE_STRING,  0 },
    { "Iterations", "Number of iterations of turbo decoder", 0, .iptr=&max_turbo_iterations,  .defintval=4, TYPE_INT, 0 },
    { "Performance", "Display CPU perfomance of each L1 piece", PARAMFLAG_BOOL,  .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "Q_cqi", "Enable CQI", PARAMFLAG_BOOL, .iptr=&cqi_flag,  .defintval=0, TYPE_INT, 0 },
    { "prefix_extended","Extended prefix", PARAMFLAG_BOOL, .iptr=&extended_prefix_flag,  .defintval=0, TYPE_INT, 0 },
    { "RI_beta", "TBD", 0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "CQI_beta", "TBD",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "ACK_beta", "TBD",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "input_file", "input IQ data file",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "N0", "N0",0, .iptr=&N0,  .defintval=30, TYPE_INT, 0 },
    { "EsubSampling","three quarters sub-sampling",PARAMFLAG_BOOL, .iptr=&threequarter_fs, .defintval=0, TYPE_INT, 0 },
    { "TDD", "Enable TDD and set the tdd configuration mode",0, .iptr=NULL,  .defintval=25, TYPE_INT, 0 },
    { "Subframe", "subframe to use",0, .iptr=&subframe,  .defintval=3, TYPE_INT, 0 },
    { "xTransmission","transmission mode (1 or 2 are supported)",0, .iptr=NULL,  .defintval=25, TYPE_INT, 0 },
    { "yN_rx", "TBD: n_rx",0, .iptr=&n_rx,  .defintval=1, TYPE_INT, 0 },
    { "bundling_disable", "bundling disable",PARAMFLAG_BOOL,  .iptr=&disable_bundling, .defintval=0, TYPE_INT, 0 },
    { "Y",  "n_ch_rlz",0, .iptr=&n_ch_rlz,  .defintval=1, TYPE_INT, 0 },
    { "X",  "abstx", PARAMFLAG_BOOL,  .iptr=&abstx, .defintval=0, TYPE_INT, 0 },
    { "Tperf", "Set the percentage of effective rate to testbench the modem performance (typically 30 and 70, range 1-100)",0, .iptr=&test_perf,  .defintval=0, TYPE_INT, 0 },
    { "verbose", "display debug text", PARAMFLAG_BOOL,  .iptr=&verbose, .defintval=0, TYPE_INT, 0 },
    { "help", "display help and exit", PARAMFLAG_BOOL,  .iptr=&help, .defintval=0, TYPE_INT, 0 },
    { "", "",0,  .iptr=NULL, .defintval=0, TYPE_INT, 0 },
  };
  struct option *long_options = parse_oai_options(options);
  int option_index;
  int res;

  /* disable error messages from getopt_long_only */
  opterr = 0;
  while ((res=getopt_long_only(argc, argv, "-", long_options, &option_index)) >= 0) {

    /* ignore configmodule options and their arguments*/
    /* with these opstring and long_options getopt returns 1 for non-option arguments and '?' for unrecognized long options, refer to 'man 3 getopt' */
    if (res == 1 || res == '?')
      continue;

    if (options[option_index].voidptr != NULL ) {
      if (long_options[option_index].has_arg==no_argument)
        *(bool *)options[option_index].iptr=1;
      else switch (options[option_index].type) {
          case TYPE_INT:
            *(int *)options[option_index].iptr=atoi(optarg);
            break;

          case TYPE_DOUBLE:
            *(double *)options[option_index].dblptr=atof(optarg);
            break;

          case TYPE_UINT8:
            *(uint8_t *)options[option_index].dblptr=atoi(optarg);
            break;

          case TYPE_UINT16:
            *(uint16_t *)options[option_index].dblptr=atoi(optarg);
            break;

          default:
            printf("not decoded type.\n");
            exit(1);
        }

      continue;
    }

    switch (long_options[option_index].name[0]) {
      case 'T':
        tdd_config=atoi(optarg);
        frame_type=TDD;
        break;

      case 'a':
        channel_model = AWGN;
        chMod = 1;
        break;

      case 'g':
        strncpy(channel_model_input,optarg,9);
        struct tmp {
          char opt;
          int m;
          int M;
        }
        tmp[]= {
          {'A',SCM_A,2},
          {'B',SCM_B,3},
          {'C',SCM_C,4},
          {'D',SCM_D,5},
          {'E',EPA,6},
          {'G',ETU,8},
          {'H',Rayleigh8,9},
          {'I',Rayleigh1,10},
          {'J',Rayleigh1_corr,11},
          {'K',Rayleigh1_anticorr,12},
          {'L',Rice8,13},
          {'M',Rice1,14},
          {'N',AWGN,1},
          {0,0,0}
        };
        struct tmp *ptr;

        for (ptr=tmp; ptr->opt!=0; ptr++)
          if ( ptr->opt == optarg[0] ) {
            channel_model=ptr->m;
            chMod=ptr->M;
            break;
          }

        AssertFatal(ptr->opt != 0, "Unsupported channel model: %s !\n", optarg );
        break;

      case 'x':
        transmission_m=atoi(optarg);
        AssertFatal(transmission_m==1 || transmission_m==2,
                    "Unsupported transmission mode %d\n",transmission_m);
        break;

      case 'r':
        nb_rb = atoi(optarg);
        nb_rb_set = 1;
        break;

      //case 'c':
      //  cyclic_shift = atoi(optarg);
      //  break;

      case 'i':
        input_fdUL = fopen(optarg,"r");
        printf("Reading in %s (%p)\n",optarg,input_fdUL);
        AssertFatal(input_fdUL != (FILE *)NULL,"Unknown file %s\n",optarg);
        break;

      case 'A':
        beta_ACK = atoi(optarg);
        AssertFatal(beta_ACK>15,"beta_ack must be in (0..15)\n");
        break;

      case 'C':
        beta_CQI = atoi(optarg);
        AssertFatal((beta_CQI>15)||(beta_CQI<2),"beta_cqi must be in (2..15)\n");
        break;

      case 'R':
        beta_RI = atoi(optarg);
        AssertFatal((beta_RI>15)||(beta_RI<2),"beta_ri must be in (0..13)\n");
        break;

      case 'P':
        dump_perf=1;
        cpu_meas_enabled = 1;
        break;

      case 'L':
        set_parallel_conf(optarg);
        break;

      default:
        printf("Wrong option: %s\n",long_options[option_index].name);
        exit(1);
        break;
    }
  }

  if ( res != -1 ) {
    printf("A wrong option has been found\n");
    exit(1);
  }

  if (help || verbose )
    display_options_values(options, true);

  if (help)
    exit(0);

  if (thread_struct.parallel_conf != PARALLEL_SINGLE_THREAD)
    set_worker_conf("WORKER_ENABLE");

  RC.nb_L1_inst = 1;
  RC.nb_RU = 1;
  lte_param_init(&eNB,&UE,&ru,
                 1,
                 1,
                 n_rx,
                 1,
                 1,
                 extended_prefix_flag,
                 frame_type,
                 0,
                 tdd_config,
                 N_RB_DL,
                 4,
                 threequarter_fs,
                 osf,
                 0);
  RC.eNB = (PHY_VARS_eNB ** *)malloc(sizeof(PHY_VARS_eNB **));
  RC.eNB[0] = (PHY_VARS_eNB **)malloc(sizeof(PHY_VARS_eNB *));
  RC.ru = (RU_t **)malloc(sizeof(RC.ru));
  RC.eNB[0][0] = eNB;
  RC.ru[0] = ru;

  for (int k=0; k<eNB->RU_list[0]->nb_rx; k++) eNB->common_vars.rxdataF[k]     =  eNB->RU_list[0]->common.rxdataF[k];

  memset((void *)&eNB->UL_INFO,0,sizeof(eNB->UL_INFO));
  printf("Setting indication lists\n");
  eNB->UL_INFO.rx_ind.rx_indication_body.rx_pdu_list   = eNB->rx_pdu_list;
  eNB->UL_INFO.crc_ind.crc_indication_body.crc_pdu_list = eNB->crc_pdu_list;
  eNB->UL_INFO.sr_ind.sr_indication_body.sr_pdu_list = eNB->sr_pdu_list;
  eNB->UL_INFO.harq_ind.harq_indication_body.harq_pdu_list = eNB->harq_pdu_list;
  eNB->UL_INFO.cqi_ind.cqi_indication_body.cqi_pdu_list = eNB->cqi_pdu_list;
  eNB->UL_INFO.cqi_ind.cqi_indication_body.cqi_raw_pdu_list = eNB->cqi_raw_pdu_list;
  printf("lte_param_init done\n");
  // for a call to phy_reset_ue later we need PHY_vars_UE_g allocated and pointing to UE
  PHY_vars_UE_g = (PHY_VARS_UE ** *)malloc(sizeof(PHY_VARS_UE **));
  PHY_vars_UE_g[0] = (PHY_VARS_UE **) malloc(sizeof(PHY_VARS_UE *));
  PHY_vars_UE_g[0][0] = UE;

  if (nb_rb_set == 0)
    nb_rb = eNB->frame_parms.N_RB_UL;

  printf("1 . rxdataF_comp[0] %p\n",eNB->pusch_vars[0]->rxdataF_comp[0]);
  printf("Setting mcs = %d\n",mcs);
  printf("n_frames = %d\n", n_frames);
  snr1 = snr0+snr_int;
  printf("SNR0 %f, SNR1 %f\n",snr0,snr1);
  frame_parms = &eNB->frame_parms;
  txdata = UE->common_vars.txdata;
  nsymb = (eNB->frame_parms.Ncp == NORMAL) ? 14 : 12;
  sprintf(bler_fname,"ULbler_mcs%d_nrb%d_ChannelModel%d_nsim%d.csv",mcs,nb_rb,chMod,n_frames);
  bler_fd = fopen(bler_fname,"w");

  if (bler_fd==NULL) {
    fprintf(stderr,"Problem creating file %s\n",bler_fname);
    exit(-1);
  }

  fprintf(bler_fd,"#SNR;mcs;nb_rb;TBS;rate;errors[0];trials[0];errors[1];trials[1];errors[2];trials[2];errors[3];trials[3]\n");

  if (test_perf != 0) {
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    printf("Hostname: %s\n", hostname);
    sprintf(time_meas_fname,"time_meas_prb%d_mcs%d_antrx%d_channel%s_tx%d.csv",
            N_RB_DL,mcs,n_rx,channel_model_input,transmission_m);
    time_meas_fd = fopen(time_meas_fname,"w");

    if (time_meas_fd==NULL) {
      fprintf(stderr,"Cannot create file %s!\n",time_meas_fname);
      exit(-1);
    }
  }

  if(abstx) {
    // CSV file
    sprintf(csv_fname,"EULdataout_tx%d_mcs%d_nbrb%d_chan%d_nsimus%d_eren.m",transmission_m,mcs,nb_rb,chMod,n_frames);
    csv_fdUL = fopen(csv_fname,"w");

    if (csv_fdUL == NULL) {
      fprintf(stderr,"Problem opening file %s\n",csv_fname);
      exit(-1);
    }

    fprintf(csv_fdUL,"data_all%d=[",mcs);
  }

  UE->pdcch_vars[0][0]->crnti = 14;
  UE->frame_parms.soundingrs_ul_config_common.enabled_flag = srs_flag;
  UE->frame_parms.soundingrs_ul_config_common.srs_BandwidthConfig = 2;
  UE->frame_parms.soundingrs_ul_config_common.srs_SubframeConfig = 3;
  UE->soundingrs_ul_config_dedicated[eNB_id].srsConfigDedicatedSetup = srs_flag;
  UE->soundingrs_ul_config_dedicated[eNB_id].duration = 1;
  UE->soundingrs_ul_config_dedicated[eNB_id].srs_ConfigIndex = 2;
  UE->soundingrs_ul_config_dedicated[eNB_id].srs_Bandwidth = 0;
  UE->soundingrs_ul_config_dedicated[eNB_id].transmissionComb = 0;
  UE->soundingrs_ul_config_dedicated[eNB_id].freqDomainPosition = 0;
  UE->soundingrs_ul_config_dedicated[eNB_id].cyclicShift = 0;
  eNB->frame_parms.soundingrs_ul_config_common.enabled_flag = srs_flag;
  eNB->frame_parms.soundingrs_ul_config_common.srs_BandwidthConfig = 2;
  eNB->frame_parms.soundingrs_ul_config_common.srs_SubframeConfig = 3;
  eNB->soundingrs_ul_config_dedicated[UE_id].srsConfigDedicatedSetup = srs_flag;
  eNB->soundingrs_ul_config_dedicated[UE_id].duration = 1;
  eNB->soundingrs_ul_config_dedicated[UE_id].srs_ConfigIndex = 2;
  eNB->soundingrs_ul_config_dedicated[UE_id].srs_Bandwidth = 0;
  eNB->soundingrs_ul_config_dedicated[UE_id].transmissionComb = 0;
  eNB->soundingrs_ul_config_dedicated[UE_id].freqDomainPosition = 0;
  eNB->soundingrs_ul_config_dedicated[UE_id].cyclicShift = 0;
  eNB->pusch_config_dedicated[UE_id].betaOffset_ACK_Index = beta_ACK;
  eNB->pusch_config_dedicated[UE_id].betaOffset_RI_Index  = beta_RI;
  eNB->pusch_config_dedicated[UE_id].betaOffset_CQI_Index = beta_CQI;
  UE->pusch_config_dedicated[eNB_id].betaOffset_ACK_Index = beta_ACK;
  UE->pusch_config_dedicated[eNB_id].betaOffset_RI_Index  = beta_RI;
  UE->pusch_config_dedicated[eNB_id].betaOffset_CQI_Index = beta_CQI;
  UE->ul_power_control_dedicated[eNB_id].deltaMCS_Enabled = 1;
  // disable periodic cqi/ri reporting
  UE->cqi_report_config[eNB_id].CQI_ReportPeriodic.ri_ConfigIndex = -1;
  UE->cqi_report_config[eNB_id].CQI_ReportPeriodic.cqi_PMI_ConfigIndex = -1;
  printf("PUSCH Beta : ACK %f, RI %f, CQI %f\n",(double)beta_ack[beta_ACK]/8,(double)beta_ri[beta_RI]/8,(double)beta_cqi[beta_CQI]/8);
  UE2eNB = new_channel_desc_scm(1,
                                n_rx,
                                channel_model,
                                N_RB2sampling_rate(eNB->frame_parms.N_RB_UL),
                                0,
                                N_RB2channel_bandwidth(eNB->frame_parms.N_RB_UL),
                                30e-9,
                                maxDoppler,
                                CORR_LEVEL_LOW,
                                forgetting_factor,
                                delay,
                                0,
                                0);

  // NN: N_RB_UL has to be defined in ulsim
  for (int k=0; k<NUMBER_OF_ULSCH_MAX; k++) eNB->ulsch[k] = new_eNB_ulsch(max_turbo_iterations,N_RB_DL,0);

  UE->ulsch[0]   = new_ue_ulsch(N_RB_DL,0);
  printf("ULSCH %p\n",UE->ulsch[0]);

  if(get_thread_worker_conf() == WORKER_ENABLE) {
    extern void init_fep_thread(RU_t *, pthread_attr_t *);
    init_fep_thread(ru, NULL);
  }

  // Create transport channel structures for 2 transport blocks (MIMO)
  for (i=0; i<2; i++) {
    eNB->dlsch[0][i] = new_eNB_dlsch(1,8,1827072,N_RB_DL,0,&eNB->frame_parms);

    if (!eNB->dlsch[0][i]) {
      printf("Can't get eNB dlsch structures\n");
      exit(-1);
    }

    eNB->dlsch[0][i]->rnti = 14;
  }

  /* allocate memory for both subframes (only one is really used
     but there is now "copy_harq_proc_struct" which needs both
     to be valid)
     TODO: refine this somehow (necessary?)
  */
  for (sf = 0; sf < 2; sf++) {
    for (i=0; i<2; i++) {
      UE->dlsch[sf][0][i]  = new_ue_dlsch(1,8,1827072,MAX_TURBO_ITERATIONS,N_RB_DL,0);

      if (!UE->dlsch[sf][0][i]) {
        printf("Can't get ue dlsch structures\n");
        exit(-1);
      }

      UE->dlsch[sf][0][i]->rnti   = 14;
    }
  }

  UE->dlsch_SI[0]  = new_ue_dlsch(1,1,1827072,MAX_TURBO_ITERATIONS,N_RB_DL,0);
  UE->dlsch_ra[0]  = new_ue_dlsch(1,1,1827072,MAX_TURBO_ITERATIONS,N_RB_DL,0);
  UE->measurements.rank[0] = 0;
  UE->transmission_mode[0] = 2;
  UE->pucch_config_dedicated[0].tdd_AckNackFeedbackMode = disable_bundling == 0 ? bundling : multiplexing;
  eNB->transmission_mode[0] = 2;
  eNB->pucch_config_dedicated[0].tdd_AckNackFeedbackMode = disable_bundling == 0 ? bundling : multiplexing;
  UE->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.groupHoppingEnabled = 1;
  eNB->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.groupHoppingEnabled = 1;
  UE->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.sequenceHoppingEnabled = 0;
  eNB->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.sequenceHoppingEnabled = 0;
  UE->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.groupAssignmentPUSCH = 0;
  eNB->frame_parms.pusch_config_common.ul_ReferenceSignalsPUSCH.groupAssignmentPUSCH = 0;
  UE->mac_enabled=0;
  L1_rxtx_proc_t *proc_rxtx         = &eNB->proc.L1_proc;
  UE_rxtx_proc_t *proc_rxtx_ue = &UE->proc.proc_rxtx[subframe&1];
  proc_rxtx->frame_rx=1;
  proc_rxtx->subframe_rx=subframe;
  proc_rxtx->frame_tx=pdcch_alloc2ul_frame(&eNB->frame_parms,1,subframe);
  proc_rxtx->subframe_tx=pdcch_alloc2ul_subframe(&eNB->frame_parms,subframe);
  proc_rxtx_ue->frame_tx = proc_rxtx->frame_rx;
  proc_rxtx_ue->frame_rx = (subframe<4)?(proc_rxtx->frame_tx-1):(proc_rxtx->frame_tx);
  proc_rxtx_ue->subframe_tx = proc_rxtx->subframe_rx;
  proc_rxtx_ue->subframe_rx = (proc_rxtx->subframe_tx+6)%10;
  proc_rxtx->threadPool = (tpool_t *)malloc(sizeof(tpool_t));
  initTpool("n", proc_rxtx->threadPool, true);

  printf("Init UL hopping UE\n");
  init_ul_hopping(&UE->frame_parms);
  printf("Init UL hopping eNB\n");
  init_ul_hopping(&eNB->frame_parms);
  UE->dlsch[subframe&1][0][0]->harq_ack[ul_subframe2pdcch_alloc_subframe(&eNB->frame_parms,subframe)].send_harq_status = 1;
  UE->ulsch_Msg3_active[eNB_id] = 0;
  UE->ul_power_control_dedicated[eNB_id].accumulationEnabled=1;
  coded_bits_per_codeword = nb_rb * (12 * get_Qm_ul(mcs)) * nsymb;

  if (cqi_flag == 1) coded_bits_per_codeword-=UE->ulsch[0]->O;

  rate = (double)TBStable[get_I_TBS(mcs)][nb_rb - 1] / (coded_bits_per_codeword);
  printf("Rate = %f (mod %d), coded bits %u\n",rate,get_Qm_ul(mcs),coded_bits_per_codeword);

  // initialize sorted lists for timers for which we want the distribution
  init_sorted_list_meas(&UE->phy_proc_tx, n_frames * 4);
  init_sorted_list_meas(&UE->ofdm_mod_stats, n_frames * 4);
  init_sorted_list_meas(&UE->ulsch_modulation_stats, n_frames * 4);
  init_sorted_list_meas(&UE->ulsch_encoding_stats, n_frames * 4);
  init_sorted_list_meas(&eNB->phy_proc_rx, n_frames * 4);
  init_sorted_list_meas(&ru->ofdm_demod_stats, n_frames * 4);
  init_sorted_list_meas(&eNB->ulsch_demodulation_stats, n_frames * 4);
  init_sorted_list_meas(&eNB->ulsch_decoding_stats, n_frames * 4);

  for (ch_realization=0; ch_realization<n_ch_rlz; ch_realization++) {
    /*
      if(abstx){
      int ulchestim_f[300*12];
      int ulchestim_t[2*(frame_parms->ofdm_symbol_size)];
      }
    */
    if(abstx) {
      printf("**********************Channel Realization Index = %d **************************\n", ch_realization);
      saving_bler=1;
    }

    //    if ((subframe>5) || (subframe < 4))
    //      UE->frame++;

    for (SNR=snr0; SNR<snr1; SNR+=input_snr_step) {
      errs[0]=0;
      errs[1]=0;
      errs[2]=0;
      errs[3]=0;
      round_trials[0] = 0;
      round_trials[1] = 0;
      round_trials[2] = 0;
      round_trials[3] = 0;
      cqi_errors=0;
      ack_errors=0;
      cqi_crc_falsepositives=0;
      cqi_crc_falsenegatives=0;
      round = 0;
      harq_pid = subframe2harq_pid(&UE->frame_parms,proc_rxtx_ue->frame_tx,subframe);
      input_buffer_length = UE->ulsch[0]->harq_processes[harq_pid]->TBS/8;

      if ( input_buffer != NULL )
        free(input_buffer);

      input_buffer = (unsigned char *)memalign(32,input_buffer_length+64);

      //      printf("UL frame %d/subframe %d, harq_pid %d\n",UE->frame,subframe,harq_pid);
      if (input_fdUL == NULL) {
        if (n_frames == 1) {
          trch_out_fdUL= fopen("ulsch_trchUL.txt","w");

          for (i=0; i<input_buffer_length; i++) {
            input_buffer[i] = taus()&0xff;

            for (j=0; j<8; j++)
              fprintf(trch_out_fdUL,"%d\n",(input_buffer[i]>>(7-j))&1);
          }

          fclose(trch_out_fdUL);
        } else {
          for (i=0; i<input_buffer_length; i++)
            input_buffer[i] = taus()&0xff;
        }
      } else {
        n_frames=1;
        i=0;

        while (!feof(input_fdUL)) {
          ret=fscanf(input_fdUL,"%49s %49s",input_val_str,input_val_str2);//&input_val1,&input_val2);

          if (ret != 2) printf("ERROR: error reading file\n");

          if ((i%4)==0) {
            ((short *)txdata[0])[i/2] = (short)((1<<15)*strtod(input_val_str,NULL));
            ((short *)txdata[0])[(i/2)+1] = (short)((1<<15)*strtod(input_val_str2,NULL));

            if ((i/4)<100)
              printf("sample %d => %e + j%e (%d +j%d)\n",i/4,strtod(input_val_str,NULL),strtod(input_val_str2,NULL),((short *)txdata[0])[i/4],((short *)txdata[0])[(i/4)+1]); //1,input_val2,);
          }

          i++;

          if (i>(FRAME_LENGTH_SAMPLES))
            break;
        }

        printf("Read in %d samples\n",i/4);
        //      LOG_M("txsig0UL.m","txs0", txdata[0],2*frame_parms->samples_per_tti,1,1);
        //    LOG_M("txsig1.m","txs1", txdata[1],FRAME_LENGTH_COMPLEX_SAMPLES,1,1);
        tx_lev = signal_energy(&txdata[0][0],
                               OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES);
        tx_lev_dB = dB_fixed(tx_lev);
      }

      iter_trials=0;
      reset_meas(&UE->phy_proc_tx);
      reset_meas(&UE->ofdm_mod_stats);
      reset_meas(&UE->ulsch_modulation_stats);
      reset_meas(&UE->ulsch_encoding_stats);
      reset_meas(&UE->ulsch_interleaving_stats);
      reset_meas(&UE->ulsch_rate_matching_stats);
      reset_meas(&UE->ulsch_turbo_encoding_stats);
      reset_meas(&UE->ulsch_segmentation_stats);
      reset_meas(&UE->ulsch_multiplexing_stats);
      reset_meas(&eNB->phy_proc_rx);
      reset_meas(&eNB->ulsch_channel_estimation_stats);
      reset_meas(&eNB->ulsch_freq_offset_estimation_stats);
      reset_meas(&eNB->rx_dft_stats);
      reset_meas(&eNB->ulsch_decoding_stats);
      reset_meas(&eNB->ulsch_turbo_decoding_stats);
      reset_meas(&eNB->ulsch_deinterleaving_stats);
      reset_meas(&eNB->ulsch_demultiplexing_stats);
      reset_meas(&eNB->ulsch_rate_unmatching_stats);
      reset_meas(&eNB->ulsch_demodulation_stats);
      reset_meas(&eNB->ulsch_tc_init_stats);
      reset_meas(&eNB->ulsch_tc_alpha_stats);
      reset_meas(&eNB->ulsch_tc_beta_stats);
      reset_meas(&eNB->ulsch_tc_gamma_stats);
      reset_meas(&eNB->ulsch_tc_ext_stats);
      reset_meas(&eNB->ulsch_tc_intl1_stats);
      reset_meas(&eNB->ulsch_tc_intl2_stats);
      // initialization
      ndi=0;
      phy_reset_ue(0,0,0);
      UE->UE_mode[eNB_id]=PUSCH;
      set_log_debug("UE_TIMING", true);

      for (trials = 0; trials<n_frames; trials++) {
        //      printf("*");
        //        UE->frame++;
        //        eNB->frame++;
        ndi = (1-ndi);
        fflush(stdout);
        round=0;

        while (round < 4) {
          proc_rxtx->frame_rx=1;
          proc_rxtx->subframe_rx=subframe;
          proc_rxtx->frame_tx=pdcch_alloc2ul_frame(&eNB->frame_parms,1,subframe);
          proc_rxtx->subframe_tx=pdcch_alloc2ul_subframe(&eNB->frame_parms,subframe);
          proc_rxtx_ue->frame_tx = proc_rxtx->frame_rx;
          proc_rxtx_ue->frame_rx = (subframe<4)?(proc_rxtx->frame_tx-1):(proc_rxtx->frame_tx);
          proc_rxtx_ue->subframe_tx = proc_rxtx->subframe_rx;
          proc_rxtx_ue->subframe_rx = (proc_rxtx->subframe_tx+6)%10;
          eNB->ulsch[0]->harq_processes[harq_pid]->round=round;
          UE->ulsch[0]->harq_processes[harq_pid]->round=round;

          if (n_frames==1) printf("filling ulsch: Trial %u : Round %d (subframe %d, frame %d)\n",trials,round,proc_rxtx_ue->subframe_tx,proc_rxtx_ue->frame_tx);

          round_trials[round]++;
          UL_req.sfn_sf = (1<<4)+subframe;

          if (n_frames==1) printf("filling ulsch: eNB prog frame %d, subframe %d (%d,%d)\n",proc_rxtx->frame_rx,subframe,sched_resp.frame,sched_resp.subframe);

          int modulation_type;

          if (mcs < 11)      modulation_type = 2;
          else if (mcs < 21) modulation_type = 4;
          else if (mcs < 29) modulation_type = 6;
          else {
            LOG_E(SIM,"mcs %i is not valid\n",mcs);
            exit(-1);
          }

          fill_ulsch_dci(eNB, proc_rxtx->frame_rx, subframe, &sched_resp, 14,
                         (void *)&UL_alloc_pdu, first_rb,nb_rb, (round==0)?mcs:(28+rvidx[round]),
                         modulation_type, ndi, get_TBS_UL(mcs,nb_rb), cqi_flag, beta_CQI,
                         beta_RI, cqi_size);
          UE->ulsch_Msg3_active[eNB_id] = 0;
          UE->ul_power_control_dedicated[eNB_id].accumulationEnabled=1;

          if (n_frames==1)
            printf("filling ulsch: ue prog SFN/SF %d/%d\n",proc_rxtx_ue->frame_rx,proc_rxtx_ue->subframe_rx);

          generate_ue_ulsch_params_from_dci((void *)&UL_alloc_pdu,
                                            14,
                                            (subframe+6)%10,
                                            format0,
                                            UE,
                                            proc_rxtx_ue,
                                            SI_RNTI,
                                            0,
                                            P_RNTI,
                                            CBA_RNTI,
                                            0,
                                            srs_flag);
          sched_resp.subframe=(subframe+6)%10;
          sched_resp.frame=(1024+eNB->proc.frame_rx+((subframe<4)?-1:0))&1023;
          schedule_response(&sched_resp, proc_rxtx);

          /////////////////////
          if (abstx) {
            if (trials==0 && round==0 && SNR==snr0) { //generate a new channel
              hold_channel = 0;
            } else {
              hold_channel = 1;
            }
          } else {
            hold_channel = 0;
          }

          ///////////////////////////////////////

          if (input_fdUL == NULL) {
            eNB->proc.frame_rx = 1;
            eNB->proc.subframe_rx = subframe;
            ru->proc.frame_rx = 1;
            ru->proc.tti_rx = subframe;
            proc_rxtx_ue->frame_tx = proc_rxtx->frame_rx;
            proc_rxtx_ue->frame_rx = proc_rxtx->frame_tx;
            proc_rxtx_ue->subframe_tx = proc_rxtx->subframe_rx;
            proc_rxtx_ue->subframe_rx = proc_rxtx->subframe_tx;
            phy_procedures_UE_TX(UE,proc_rxtx_ue,0,0,normal_txrx);
            tx_lev = signal_energy(&UE->common_vars.txdata[0][eNB->frame_parms.samples_per_tti*subframe],
                                   eNB->frame_parms.samples_per_tti);

            if (n_frames==1) {
              LOG_M("txsigF0UL.m","txsF0", &UE->common_vars.txdataF[0][eNB->frame_parms.ofdm_symbol_size*nsymb*subframe],eNB->frame_parms.ofdm_symbol_size*nsymb,1,
                    1);
              //LOG_M("txsigF1.m","txsF1", UE->common_vars.txdataF[0],FRAME_LENGTH_COMPLEX_SAMPLES_NO_PREFIX,1,1);
            }
          }  // input_fd == NULL

          tx_lev_dB = (unsigned int) dB_fixed_times10(tx_lev);

          if (n_frames==1) {
            LOG_M("txsig0UL.m","txs0", &txdata[0][eNB->frame_parms.samples_per_tti*subframe],2*frame_parms->samples_per_tti,1,1);
            //        LOG_M("txsig1UL.m","txs1", &txdata[1][eNB->frame_parms.samples_per_tti*subframe],2*frame_parms->samples_per_tti,1,1);
          }

          //AWGN
          //Set target wideband RX noise level to N0
          sigma2_dB = N0;//-10*log10(UE->frame_parms.ofdm_symbol_size/(UE->frame_parms.N_RB_DL*12));//10*log10((double)tx_lev)  +10*log10(UE->frame_parms.ofdm_symbol_size/(UE->frame_parms.N_RB_DL*12)) - SNR;
          sigma2 = pow(10,sigma2_dB/10);
          // compute tx_gain to achieve target SNR (per resource element!)
          tx_gain = sqrt(pow(10.0,.1*(N0+SNR))/(double)tx_lev);// *(nb_rb*12/(double)UE->frame_parms.ofdm_symbol_size)/(double)tx_lev);

          if (n_frames==1)
            printf("tx_lev = %u (%u.%u dB,%f), gain %f\n",tx_lev,tx_lev_dB/10,tx_lev_dB,10*log10((double)tx_lev),10*log10(tx_gain));

          // fill measurement symbol (19) with noise
          for (i=0; i<OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES; i++) {
            for (aa=0; aa<eNB->frame_parms.nb_antennas_rx; aa++) {
              ((short *) &ru->common.rxdata[aa][(frame_parms->samples_per_tti<<1) -frame_parms->ofdm_symbol_size])[2*i] = (short) ((sqrt(sigma2/2)*gaussdouble(0.0,1.0)));
              ((short *) &ru->common.rxdata[aa][(frame_parms->samples_per_tti<<1) -frame_parms->ofdm_symbol_size])[2*i+1] = (short) ((sqrt(sigma2/2)*gaussdouble(0.0,1.0)));
            }
          }

          // multipath channel

          for (i=0; i<eNB->frame_parms.samples_per_tti; i++) {
            for (aa=0; aa<1; aa++) {
              s_re[aa][i] = ((double)(((short *)&UE->common_vars.txdata[aa][eNB->frame_parms.samples_per_tti*subframe]))[(i<<1)]);
              s_im[aa][i] = ((double)(((short *)&UE->common_vars.txdata[aa][eNB->frame_parms.samples_per_tti*subframe]))[(i<<1)+1]);
            }
          }

          if (UE2eNB->max_Doppler == 0) {
            multipath_channel(UE2eNB,s_re,s_im,r_re,r_im,
                              eNB->frame_parms.samples_per_tti,hold_channel,0);
          } else {
            multipath_tv_channel(UE2eNB,s_re,s_im,r_re,r_im,
                                 2*eNB->frame_parms.samples_per_tti,hold_channel);
          }

          if(abstx) {
            if(saving_bler==0)
              if (trials==0 && round==0) {
                // calculate freq domain representation to compute SINR
                freq_channel(UE2eNB, N_RB_DL,12*N_RB_DL + 1, 15);
                // snr=pow(10.0,.1*SNR);
                fprintf(csv_fdUL,"%f,%u,%u,%f,%f,%f,",SNR,tx_lev,tx_lev_dB,sigma2_dB,tx_gain,SNR2);
                allocCast3D(chF, struct complexd, UE2eNB->chF, UE2eNB->nb_tx, UE2eNB->nb_rx, UE2eNB->channelF_len, false);

                //fprintf(csv_fdUL,"%f,",SNR);
                for (u=0; u<12*nb_rb; u++) {
                  for (aarx=0; aarx<UE2eNB->nb_rx; aarx++) {
                    for (aatx=0; aatx<UE2eNB->nb_tx; aatx++) {
                      channelx = chF[aatx][aarx][u].r;
                      channely = chF[aatx][aarx][u].i;
                      // if(transmission_m==5){
                      fprintf(csv_fdUL,"%e+i*(%e),",channelx,channely);
                      // }
                      // else{
                      //  pilot_sinr = 10*log10(snr*abs_channel);
                      //  fprintf(csv_fd,"%e,",pilot_sinr);
                      // }
                    }
                  }
                }
              }
          }

          if (n_frames==1)
            printf("Sigma2 %f (sigma2_dB %f), tx_gain %f (%f dB)\n",sigma2,sigma2_dB,tx_gain,20*log10(tx_gain));

          for (i=0; i<eNB->frame_parms.samples_per_tti; i++) {
            for (aa=0; aa<eNB->frame_parms.nb_antennas_rx; aa++) {
              ((short *) &ru->common.rxdata[aa][eNB->frame_parms.samples_per_tti*subframe])[2*i] =
                (short) ((tx_gain*r_re[aa][i]) + sqrt(sigma2/2)*gaussdouble(0.0,1.0));
              ((short *) &ru->common.rxdata[aa][eNB->frame_parms.samples_per_tti*subframe])[2*i+1] =
                (short) ((tx_gain*r_im[aa][i]) + (iqim*tx_gain*r_re[aa][i]) +
                         sqrt(sigma2/2)*gaussdouble(0.0,1.0));
            }
          }
          if (n_frames==1)
            for (i=0; i<eNB->frame_parms.samples_per_tti; i++) {
              for (aa=0; aa<eNB->frame_parms.nb_antennas_rx; aa++) {
                ((short *) &ru->common.rxdata[aa][eNB->frame_parms.samples_per_tti*(subframe+1)%10])[2*i] =
                  (short) (sqrt(sigma2/2)*gaussdouble(0.0,1.0));
                ((short *) &ru->common.rxdata[aa][eNB->frame_parms.samples_per_tti*(subframe+1)%10])[2*i+1] =
                  (short) (sqrt(sigma2/2)*gaussdouble(0.0,1.0));
              }
            }

          if (n_frames<=10) {
            printf("rx_level Null symbol %f\n",10*log10((double)signal_energy((int *)
                   &ru->common.rxdata[0][(eNB->frame_parms.samples_per_tti<<1) -
                                         eNB->frame_parms.ofdm_symbol_size],OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES/2)));
            printf("rx_level data symbol %f\n",
                   10*log10(signal_energy((int *)&ru->common.rxdata[0][160+(eNB->frame_parms.samples_per_tti*subframe)],
                                          OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES/2)));
          }

          SNRmeas = 10*log10(((double)signal_energy((int *)&ru->common.rxdata[0][160+(eNB->frame_parms.samples_per_tti*subframe)],
                              OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES/2))/((double)signal_energy((int *)
                                  &ru->common.rxdata[0][(eNB->frame_parms.samples_per_tti<<1) -eNB->frame_parms.ofdm_symbol_size],
                                  OFDM_SYMBOL_SIZE_COMPLEX_SAMPLES/2)) - 1)+10*log10(eNB->frame_parms.N_RB_UL/nb_rb);

          if (n_frames<=10) {
            printf("SNRmeas %f\n",SNRmeas);
            LOG_M("rxsig0UL.m","rxs0", &ru->common.rxdata[0][eNB->frame_parms.samples_per_tti*subframe],eNB->frame_parms.samples_per_tti,1,1);

            if (eNB->frame_parms.nb_antennas_rx>1) LOG_M("rxsig1UL.m","rxs1", &ru->common.rxdata[1][eNB->frame_parms.samples_per_tti*subframe],eNB->frame_parms.samples_per_tti,1,1);
          }

          start_meas(&eNB->phy_proc_rx);
          ru->feprx = (get_thread_worker_conf() == WORKER_ENABLE) ? ru_fep_full_2thread        : fep_full;
          ru->feprx(ru,subframe);
          if (n_frames==1) lte_eNB_I0_measurements(eNB,(subframe+1)%10,0,1);

          phy_procedures_eNB_uespec_RX(eNB,proc_rxtx);
          stop_meas(&eNB->phy_proc_rx);
          if (cqi_flag > 0) {
            cqi_error = 0;

            if (eNB->ulsch[0]->harq_processes[harq_pid]->Or1 < 32) {
              for (i=2; i<4; i++) {
                //                printf("cqi %d : %d (%d)\n",i,eNB->ulsch[0]->o[i],UE->ulsch[0]->o[i]);
                if (eNB->ulsch[0]->harq_processes[harq_pid]->o[i] != UE->ulsch[0]->o[i])
                  cqi_error = 1;
              }
            } else {
            }

            if (cqi_error == 1) {
              cqi_errors++;

              if (eNB->ulsch[0]->harq_processes[harq_pid]->cqi_crc_status == 1)
                cqi_crc_falsepositives++;
            } else {
              if (eNB->ulsch[0]->harq_processes[harq_pid]->cqi_crc_status == 0)
                cqi_crc_falsenegatives++;
            }
          }

          if (eNB->ulsch[0]->harq_processes[harq_pid]->o_ACK[0] != UE->ulsch[0]->o_ACK[0])
            ack_errors++;

          //    printf("ulsch_coding: O[%d] %d\n",i,o_flip[i]);
          //          if (ret <= eNB->ulsch[0]->max_turbo_iterations) {
          iter_trials++;

          if (eNB->ulsch[0]->harq_processes[harq_pid]->status == SCH_IDLE) {
            if (n_frames==1) {
              printf("No ULSCH errors found, o_ACK[0]= %d, cqi_crc_status=%d\n",eNB->ulsch[0]->harq_processes[harq_pid]->o_ACK[0],eNB->ulsch[0]->harq_processes[harq_pid]->cqi_crc_status);

              if (eNB->ulsch[0]->harq_processes[harq_pid]->cqi_crc_status==1)
                print_CQI(eNB->ulsch[0]->harq_processes[harq_pid]->o,
                          eNB->ulsch[0]->harq_processes[harq_pid]->uci_format,0,eNB->frame_parms.N_RB_DL);

              dump_ulsch(eNB,eNB->proc.frame_rx,subframe,0,round);
              dump_I0_stats(stdout,eNB);
              dump_ulsch_stats(stdout,eNB,0);

              exit(-1);
            }

            round=5;
          } else {
            errs[round]++;

            if (n_frames==1) {
              printf("ULSCH errors found o_ACK[0]= %d\n",eNB->ulsch[0]->harq_processes[harq_pid]->o_ACK[0]);
#ifdef DUMP_EACH_VALUE
              int Kr_bytes;
              for (s=0; s<eNB->ulsch[0]->harq_processes[harq_pid]->C; s++) {
                if (s<eNB->ulsch[0]->harq_processes[harq_pid]->Cminus)
                  Kr_bytes = eNB->ulsch[0]->harq_processes[harq_pid]->Kminus;
                else
                  Kr_bytes = eNB->ulsch[0]->harq_processes[harq_pid]->Kplus;
                Kr_bytes = Kr_bytes >> 3;
                printf("Decoded_output (Segment %d):\n",s);

                for (i=0; i<Kr_bytes; i++)
                  printf("%d : %x (%x)\n",i,eNB->ulsch[0]->harq_processes[harq_pid]->c[s][i],
                         eNB->ulsch[0]->harq_processes[harq_pid]->c[s][i]^UE->ulsch[0]->harq_processes[harq_pid]->c[s][i]);
              }
#endif

              dump_ulsch(eNB,eNB->proc.frame_rx,subframe,0,round);
              round=5;
            }

            if (n_frames==1) printf("round %d errors %u/%u\n",round,errs[round],trials);

            round++;

            if (n_frames==1) {
              printf("ULSCH in error in round %d\n",round);
            }
          }  // ulsch error
        } // round

        //      printf("\n");
        if ((errs[0]>=100) && (trials>(n_frames/2)))
          break;

        double t_tx = inMicroS(UE->phy_proc_tx.p_time);
        double t_rx = inMicroS(eNB->phy_proc_rx.p_time);

        if (t_tx > 2000 )// 2ms is too much time for a subframe
          n_tx_dropped++;

        if (t_rx > 2000 )
          n_rx_dropped++;

        if (trials < 1000) {
       }
      }   //trials

      // sort table
      if (dump_table == 1 ) {
        set_component_filelog(SIM); // file located in /tmp/usim.txt
      }

      dump_ulsch_stats(stdout,eNB,0);
      printf("\n**********rb: %d ***mcs : %d  *********SNR = %f dB (%f): TX %u dB (gain %f dB), N0W %f dB, I0 %u dB, delta_IF %d [ (%d,%d) dB / (%u,%u) dB ]**************************\n",
             nb_rb,mcs,SNR,SNR2,
             tx_lev_dB,
             20*log10(tx_gain),
             (double)N0,
             eNB->measurements.n0_power_tot_dB,
             get_hundred_times_delta_IF(UE,eNB_id,harq_pid),
             dB_fixed(eNB->pusch_vars[0]->ulsch_power[0]),
             dB_fixed(eNB->pusch_vars[0]->ulsch_power[1]),
             eNB->measurements.n0_power_dB[0],
             eNB->measurements.n0_power_dB[1]);
      effective_rate = ((double)(round_trials[0])/((double)round_trials[0] + round_trials[1] + round_trials[2] + round_trials[3]));
      printf("Errors (%u/%u %u/%u %u/%u %u/%u), Pe = (%e,%e,%e,%e) => effective rate %f (%3.1f%%,%f,%f), normalized delay %f (%f)\n",
             errs[0],
             round_trials[0],
             errs[1],
             round_trials[1],
             errs[2],
             round_trials[2],
             errs[3],
             round_trials[3],
             (double)errs[0]/(round_trials[0]),
             (double)errs[1]/(round_trials[0]),
             (double)errs[2]/(round_trials[0]),
             (double)errs[3]/(round_trials[0]),
             rate*effective_rate,
             100*effective_rate,
             rate,
             rate*get_Qm_ul(mcs),
             (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0])/
             (double)eNB->ulsch[0]->harq_processes[harq_pid]->TBS,
             (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0]));

      if (cqi_flag >0) {
        printf("CQI errors %d/%u,false positives %d/%u, CQI false negatives %d/%u\n",
               cqi_errors,round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3],
               cqi_crc_falsepositives,round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3],
               cqi_crc_falsenegatives,round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3]);
      }

      if (eNB->ulsch[0]->harq_processes[harq_pid]->o_ACK[0] > 0)
        printf("ACK/NAK errors %d/%u\n",ack_errors,round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3]);

      fprintf(bler_fd,"%f;%d;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u\n",
              SNR,
              mcs,
              nb_rb,
              eNB->ulsch[0]->harq_processes[harq_pid]->TBS,
              rate,
              errs[0],
              round_trials[0],
              errs[1],
              round_trials[1],
              errs[2],
              round_trials[2],
              errs[3],
              round_trials[3]);
      double timeBase = 1e-3;

      if (dump_perf==1) {
        printf("UE TX function statistics (per 1ms subframe)\n\n");
        printDistribution(&UE->phy_proc_tx, "Total PHY proc tx");
        printDistribution(&UE->ofdm_mod_stats, "OFDM_mod time");
        printDistribution(&UE->ulsch_modulation_stats, "ULSCH modulation time");
        printDistribution(&UE->ulsch_encoding_stats, "ULSCH encoding time");
        printStatIndent(&UE->ulsch_segmentation_stats,"ULSCH segmentation time");
        printStatIndent(&UE->ulsch_turbo_encoding_stats,"ULSCH turbo encoding time");
        printStatIndent(&UE->ulsch_rate_matching_stats,"ULSCH rate-matching time");
        printStatIndent(&UE->ulsch_interleaving_stats,"ULSCH sub-block interleaving");
        printStatIndent(&UE->ulsch_multiplexing_stats,"ULSCH multiplexing time");
        printf("\n");
        printDistribution(&eNB->phy_proc_rx, "Total PHY proc rx subframe");
        printDistribution(&ru->ofdm_demod_stats, "|__ OFDM_demod time");
        printDistribution(&eNB->ulsch_demodulation_stats, "|__ ULSCH demodulation time");
        printDistribution(&eNB->ulsch_decoding_stats, "|__ ULSCH Decoding time");
        printf("     (%.2f Mbit/s, avg iter %.2f, max %.2f)\n",
               UE->ulsch[0]->harq_processes[harq_pid]->TBS/1000.0,
               (double)iter_trials,
               (double)eNB->ulsch_decoding_stats.max*timeBase);
        printStatIndent2(&eNB->ulsch_deinterleaving_stats,"sub-block interleaving" );
        printStatIndent2(&eNB->ulsch_demultiplexing_stats,"sub-block demultiplexing" );
        printStatIndent2(&eNB->ulsch_rate_unmatching_stats,"sub-block rate-matching" );
        printf("    |__ harqID: %d turbo_decoder(%d bits), avg iterations: %.1f       %.2f us (%d cycles, %d trials)\n",
               harq_pid,
               eNB->ulsch[0]->harq_processes[harq_pid]->Cminus ? eNB->ulsch[0]->harq_processes[harq_pid]->Kminus : eNB->ulsch[0]->harq_processes[harq_pid]->Kplus,
               eNB->ulsch_tc_intl1_stats.trials / (double)eNB->ulsch_tc_init_stats.trials,
               (double)eNB->ulsch_turbo_decoding_stats.diff / eNB->ulsch_turbo_decoding_stats.trials * timeBase,
               (int)((double)eNB->ulsch_turbo_decoding_stats.diff / eNB->ulsch_turbo_decoding_stats.trials),
               eNB->ulsch_turbo_decoding_stats.trials);
        printStatIndent3(&eNB->ulsch_tc_init_stats,"init");
        printStatIndent3(&eNB->ulsch_tc_alpha_stats,"alpha");
        printStatIndent3(&eNB->ulsch_tc_beta_stats,"beta");
        printStatIndent3(&eNB->ulsch_tc_gamma_stats,"gamma");
        printStatIndent3(&eNB->ulsch_tc_ext_stats,"ext");
        printStatIndent3(&eNB->ulsch_tc_intl1_stats,"turbo internal interleaver");
        printStatIndent3(&eNB->ulsch_tc_intl2_stats,"intl2+HardDecode+CRC");
      }

      if(abstx) { //ABSTRACTION
        blerr= (double)errs[1]/(round_trials[1]);
        //printf("hata yok XX,");
        blerr = (double)errs[0]/(round_trials[0]);

        if(saving_bler==0)
          fprintf(csv_fdUL,"%e;\n",blerr);

        //    printf("hata yok XX,");

        if(blerr<1)
          saving_bler = 0;
        else saving_bler =1;
      } //ABStraction

      if ( (test_perf != 0) && (100 * effective_rate > test_perf )) {
        //fprintf(time_meas_fd,"SNR; MCS; TBS; rate; err0; trials0; err1; trials1; err2; trials2; err3; trials3\n");
        fprintf(time_meas_fd,"%f;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u;",
                SNR,
                mcs,
                eNB->ulsch[0]->harq_processes[harq_pid]->TBS,
                rate,
                errs[0],
                round_trials[0],
                errs[1],
                round_trials[1],
                errs[2],
                round_trials[2],
                errs[3],
                round_trials[3]);
        //fprintf(time_meas_fd,"SNR; MCS; TBS; rate; err0; trials0; err1; trials1; err2; trials2; err3; trials3;ND;\n");
        fprintf(time_meas_fd,"%f;%d;%d;%f;%2.1f;%f;%u;%u;%u;%u;%u;%u;%u;%u;%e;%e;%e;%e;%f;%f;",
                SNR,
                mcs,
                eNB->ulsch[0]->harq_processes[harq_pid]->TBS,
                rate*effective_rate,
                100*effective_rate,
                rate,
                errs[0],
                round_trials[0],
                errs[1],
                round_trials[1],
                errs[2],
                round_trials[2],
                errs[3],
                round_trials[3],
                (double)errs[0]/(round_trials[0]),
                (double)errs[1]/(round_trials[0]),
                (double)errs[2]/(round_trials[0]),
                (double)errs[3]/(round_trials[0]),
                (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0])/
                (double)eNB->ulsch[0]->harq_processes[harq_pid]->TBS,
                (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0]));
        //fprintf(time_meas_fd,"UE_PROC_TX(%d); OFDM_MOD(%d); UL_MOD(%d); UL_ENC(%d); eNB_PROC_RX(%d); OFDM_DEMOD(%d); UL_DEMOD(%d); UL_DECOD(%d);\n",
        fprintf(time_meas_fd,"%d; %d; %d; %d; %d; %d; %d; %d;",
                UE->phy_proc_tx.trials,
                UE->ofdm_mod_stats.trials,
                UE->ulsch_modulation_stats.trials,
                UE->ulsch_encoding_stats.trials,
                eNB->phy_proc_rx.trials,
                ru->ofdm_demod_stats.trials,
                eNB->ulsch_demodulation_stats.trials,
                eNB->ulsch_decoding_stats.trials
               );
        fprintf(time_meas_fd,"%f;%f;%f;%f;%f;%f;%f;%f;",
                get_time_meas_us(&UE->phy_proc_tx),
                get_time_meas_us(&UE->ofdm_mod_stats),
                get_time_meas_us(&UE->ulsch_modulation_stats),
                get_time_meas_us(&UE->ulsch_encoding_stats),
                get_time_meas_us(&eNB->phy_proc_rx),
                get_time_meas_us(&ru->ofdm_demod_stats),
                get_time_meas_us(&eNB->ulsch_demodulation_stats),
                get_time_meas_us(&eNB->ulsch_decoding_stats)
               );
        fprintf(time_meas_fd,"TIMER;STD_DEV;MAX;Q1;MED;Q3;MIN;DROPPED;\n");
        printDistributionDroppedCsv(time_meas_fd, &UE->phy_proc_tx, n_tx_dropped, "UE TX TOTAL");
        printDistributionCsv(time_meas_fd, &UE->ofdm_mod_stats, "UE TX IFFT");
        printDistributionCsv(time_meas_fd, &UE->ulsch_modulation_stats, "UE TX MOD");
        printDistributionCsv(time_meas_fd, &UE->ulsch_encoding_stats, "UE TX ENC");
        printDistributionDroppedCsv(time_meas_fd, &eNB->phy_proc_rx, n_rx_dropped, "eNB RX TOTAL");
        printDistributionCsv(time_meas_fd, &ru->ofdm_demod_stats, "RU RX FFT");
        printDistributionCsv(time_meas_fd, &eNB->ulsch_demodulation_stats, "eNB RX DEMOD");
        printDistributionCsv(time_meas_fd, &eNB->ulsch_decoding_stats, "eNB RX DEC");
        printf("[passed] effective rate : %f  (%2.1f%%,%f)): log and break \n",rate*effective_rate, 100*effective_rate, rate );
        break;
      } else if (test_perf !=0 ) {
        printf("[continue] effective rate : %f  (%2.1f%%,%f)): increase snr \n",rate*effective_rate, 100*effective_rate, rate);
      }

      if (((double)errs[0]/(round_trials[0]))<1e-2)
        break;
    } // SNR

    //
    //LOG_M("chestim_f.m","chestf",eNB->pusch_vars[0]->drs_ch_estimates[0][0],300*12,2,1);
    // LOG_M("chestim_t.m","chestt",eNB->pusch_vars[0]->drs_ch_estimates_time[0][0], (frame_parms->ofdm_symbol_size)*2,2,1);
  }//ch realization

  oai_exit=1;
  pthread_cond_signal(&ru->proc.cond_fep[0]);

  // initialize sorted lists for timers for which we want the distribution
  free_sorted_list_meas(&UE->phy_proc_tx);
  free_sorted_list_meas(&UE->ofdm_mod_stats);
  free_sorted_list_meas(&UE->ulsch_modulation_stats);
  free_sorted_list_meas(&UE->ulsch_encoding_stats);
  free_sorted_list_meas(&eNB->phy_proc_rx);
  free_sorted_list_meas(&ru->ofdm_demod_stats);
  free_sorted_list_meas(&eNB->ulsch_demodulation_stats);
  free_sorted_list_meas(&eNB->ulsch_decoding_stats);

  if (abstx) { // ABSTRACTION
    fprintf(csv_fdUL,"];");
    fclose(csv_fdUL);
  }

  fclose(bler_fd);

  if (test_perf !=0)
    fclose (time_meas_fd);

  return(0);
}
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}
