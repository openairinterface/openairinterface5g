/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <math.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include "common/cmake_defs.h"
#include "common/config/config_load_configmodule.h"
#include "common/utils/LOG/log.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "PHY/types.h"
#include "PHY/defs_eNB.h"
#include "PHY/defs_UE.h"
#include "PHY/phy_vars.h"
#include "PHY/INIT/phy_init.h"
#include "PHY/LTE_TRANSPORT/transport_proto.h"
#include "PHY/LTE_UE_TRANSPORT/transport_proto_ue.h"
#include "PHY/MODULATION/modulation_eNB.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "PHY/MODULATION/phy_ofdm_mod.h"
#include "SCHED/sched_eNB.h"
#include "SCHED_UE/sched_UE.h"
#include "SIMULATION/TOOLS/sim.h"
#include "unitary_defs.h"
#include "dummy_functions.c"
#include "executables/thread-common.h"
#include "common/ran_context.h"
#include "executables/softmodem-common.h"
void feptx_ofdm(RU_t *ru, int frame, int subframe);
void feptx_prec(RU_t *ru, int frame, int subframe);

const char *__asan_default_options()
{
  /* don't do leak checking in nr_ulsim, not finished yet */
  return "detect_leaks=0";
}


#define inMicroS(a) (((double)(a))/(1000.0))
//#define MCS_COUNT 23//added for PHY abstraction
#include <openair1/SIMULATION/LTE_PHY/common_sim.h>

int otg_enabled=0;
THREAD_STRUCT thread_struct;
nfapi_ue_release_request_body_t release_rntis;

/*the following parameters are used to control the processing times calculations*/
int n_tx_dropped = 0; /*!< \brief initial max process time for tx */
int n_rx_dropped = 0; /*!< \brief initial max process time for rx */

double DS_TDL = .03;
RAN_CONTEXT_t RC;

void handler(int sig) {
  void *array[10];
  size_t size;
  // get void*'s for all entries on the stack
  size = backtrace(array, 10);
  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, 2);
  exit(1);
}

//DCI2_5MHz_2A_M10PRB_TDD_t DLSCH_alloc_pdu2_2A[2];

DCI1E_5MHz_2A_M10PRB_TDD_t  DLSCH_alloc_pdu2_1E[2];
uint64_t DLSCH_alloc_pdu_1[2];

#define UL_RB_ALLOC 0x1ff;
#define CCCH_RB_ALLOC computeRIV(eNB->frame_parms.N_RB_UL,0,2)
//#define DLSCH_RB_ALLOC 0x1fbf // igore DC component,RB13
//#define DLSCH_RB_ALLOC 0x0001
void do_OFDM_mod_l(int32_t **txdataF, int32_t **txdata, uint16_t next_slot, LTE_DL_FRAME_PARMS *frame_parms) {
  int aa, slot_offset, slot_offset_F;
  slot_offset_F = (next_slot)*(frame_parms->ofdm_symbol_size)*((frame_parms->Ncp==1) ? 6 : 7);
  slot_offset = (next_slot)*(frame_parms->samples_per_tti>>1);

  for (aa=0; aa<frame_parms->nb_antennas_tx; aa++) {
    if (frame_parms->Ncp == 1)
      PHY_ofdm_mod(&txdataF[aa][slot_offset_F],        // input
                   &txdata[aa][slot_offset],         // output
                   frame_parms->ofdm_symbol_size,
                   6,                 // number of symbols
                   frame_parms->nb_prefix_samples,               // number of prefix samples
                   CYCLIC_PREFIX);
    else {
      normal_prefix_mod(&txdataF[aa][slot_offset_F],
                        &txdata[aa][slot_offset],
                        7,
                        frame_parms);
    }
  }
}

void DL_channel(RU_t *ru,PHY_VARS_UE *UE,uint subframe,int awgn_flag,double SNR, int tx_lev,int hold_channel,int abstx, int num_rounds, int trials, int round, channel_desc_t *eNB2UE[4],
                double *s_re[NB_ANTENNAS_TX],double *s_im[NB_ANTENNAS_TX],double *r_re[NB_ANTENNAS_RX],double *r_im[NB_ANTENNAS_RX],FILE *csv_fd) {
  int i,u;
  int aa,aarx,aatx;
  double channelx,channely;
  double sigma2_dB,sigma2;
  double iqim=0.0;

  //    printf("Copying tx ..., nsymb %d (n_tx %d), awgn %d\n",nsymb,eNB->frame_parms.nb_antennas_tx,awgn_flag);
  for (i=0; i<2*UE->frame_parms.samples_per_tti; i++) {
    for (aa=0; aa<ru->frame_parms->nb_antennas_tx; aa++) {
      if (awgn_flag == 0) {
        s_re[aa][i] = ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) + (i<<1)]);
        s_im[aa][i] = ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) +(i<<1)+1]);
      } else {
        for (aarx=0; aarx<UE->frame_parms.nb_antennas_rx; aarx++) {
          if (aa==0) {
            r_re[aarx][i] = ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) +(i<<1)]);
            r_im[aarx][i] = ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) +(i<<1)+1]);
          } else {
            r_re[aarx][i] += ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) +(i<<1)]);
            r_im[aarx][i] += ((double)(((short *)ru->common.txdata[aa]))[(2*subframe*UE->frame_parms.samples_per_tti) +(i<<1)+1]);
          }
        }
      }
    }
  }

  // Multipath channel
  if (awgn_flag == 0) {
    multipath_channel(eNB2UE[round],s_re,s_im,r_re,r_im,
                      2*UE->frame_parms.samples_per_tti,hold_channel,0);

    //      printf("amc: ****************** eNB2UE[%d]->n_rx = %d,dd %d\n",round,eNB2UE[round]->nb_rx,eNB2UE[round]->channel_offset);
    if(abstx==1 && num_rounds>1)
      if(round==0 && hold_channel==0) {
        random_channel(eNB2UE[1],0);
        random_channel(eNB2UE[2],0);
        random_channel(eNB2UE[3],0);
      }

    if (UE->perfect_ce==1) {
      // fill in perfect channel estimates
      freq_channel(eNB2UE[round],UE->frame_parms.N_RB_DL,12*UE->frame_parms.N_RB_DL + 1, 15);
      /*
      LOG_M("channel.m","ch",eNB2UE[round]->ch[0],eNB2UE[round]->channel_length,1,8);
      LOG_M("channelF.m","chF",eNB2UE[round]->chF[0],12*UE->frame_parms.N_RB_DL + 1,1,8);
      */
    }
  }

  if(abstx) {
    if (trials==0 && round==0) {
      allocCast3D(chF0, struct complexd, eNB2UE[0]->chF, eNB2UE[0]->nb_tx, eNB2UE[0]->nb_rx, eNB2UE[0]->channelF_len, false);
      // calculate freq domain representation to compute SINR
      freq_channel(eNB2UE[0], ru->frame_parms->N_RB_DL,2*ru->frame_parms->N_RB_DL + 1, 15);
      // snr=pow(10.0,.1*SNR);
      fprintf(csv_fd,"%f,",SNR);

      for (u=0; u<2*ru->frame_parms->N_RB_DL; u++) {
        for (aarx=0; aarx<eNB2UE[0]->nb_rx; aarx++) {
          for (aatx=0; aatx<eNB2UE[0]->nb_tx; aatx++) {
            channelx = chF0[aatx][aarx][u].r;
            channely = chF0[aatx][aarx][u].i;
            fprintf(csv_fd,"%e+i*(%e),",channelx,channely);
          }
        }
      }

      if(num_rounds>1) {
        allocCast3D(chF1, struct complexd, eNB2UE[1]->chF, eNB2UE[1]->nb_tx, eNB2UE[1]->nb_rx, eNB2UE[1]->channelF_len, false);
        freq_channel(eNB2UE[1], ru->frame_parms->N_RB_DL,2*ru->frame_parms->N_RB_DL + 1, 15);

        for (u=0; u<2*ru->frame_parms->N_RB_DL; u++) {
          for (aarx=0; aarx<eNB2UE[1]->nb_rx; aarx++) {
            for (aatx=0; aatx<eNB2UE[1]->nb_tx; aatx++) {
              channelx = chF1[aatx][aarx][u].r;
              channely = chF1[aatx][aarx][u].i;
              fprintf(csv_fd,"%e+i*(%e),",channelx,channely);
            }
          }
        }

        freq_channel(eNB2UE[2], ru->frame_parms->N_RB_DL,2*ru->frame_parms->N_RB_DL + 1, 15);
        allocCast3D(chF2, struct complexd, eNB2UE[2]->chF, eNB2UE[2]->nb_tx, eNB2UE[2]->nb_rx, eNB2UE[2]->channelF_len, false);

        for (u=0; u<2*ru->frame_parms->N_RB_DL; u++) {
          for (aarx=0; aarx<eNB2UE[2]->nb_rx; aarx++) {
            for (aatx=0; aatx<eNB2UE[2]->nb_tx; aatx++) {
              channelx = chF2[aatx][aarx][u].r;
              channely = chF2[aatx][aarx][u].i;
              fprintf(csv_fd,"%e+i*(%e),",channelx,channely);
            }
          }
        }

        allocCast3D(chF3, struct complexd, eNB2UE[3]->chF, eNB2UE[3]->nb_tx, eNB2UE[3]->nb_rx, eNB2UE[3]->channelF_len, false);
        freq_channel(eNB2UE[3], ru->frame_parms->N_RB_DL,2*ru->frame_parms->N_RB_DL + 1, 15);

        for (u=0; u<2*ru->frame_parms->N_RB_DL; u++) {
          for (aarx=0; aarx<eNB2UE[3]->nb_rx; aarx++) {
            for (aatx=0; aatx<eNB2UE[3]->nb_tx; aatx++) {
              channelx = chF3[aatx][aarx][u].r;
              channely = chF3[aatx][aarx][u].i;
              fprintf(csv_fd,"%e+i*(%e),",channelx,channely);
            }
          }
        }
      }
    }
  }

  //AWGN
  // tx_lev is the average energy over the whole subframe
  // but SNR should be better defined wrt the energy in the reference symbols
  sigma2_dB = 10*log10((double)tx_lev) +10*log10((double)ru->frame_parms->ofdm_symbol_size/(double)(ru->frame_parms->N_RB_DL*12)) - SNR;
  sigma2 = pow(10,sigma2_dB/10);

  for (i=0; i<2*UE->frame_parms.samples_per_tti; i++) {
    for (aa=0; aa<UE->frame_parms.nb_antennas_rx; aa++) {
      //printf("s_re[0][%d]=> %f , r_re[0][%d]=> %f\n",i,s_re[aa][i],i,r_re[aa][i]);
      ((short *) UE->common_vars.rxdata[aa])[(2*subframe*UE->frame_parms.samples_per_tti)+2*i] =
        (short) (r_re[aa][i] + sqrt(sigma2/2)*gaussdouble(0.0,1.0));
      ((short *) UE->common_vars.rxdata[aa])[(2*subframe*UE->frame_parms.samples_per_tti)+2*i+1] =
        (short) (r_im[aa][i] + (iqim*r_re[aa][i]) + sqrt(sigma2/2)*gaussdouble(0.0,1.0));
    }
  }
}

uint16_t
fill_tx_req(nfapi_tx_request_body_t *tx_req_body,
            uint16_t                absSF,
            uint16_t                pdu_length,
            uint16_t                pdu_index,
            uint8_t                 *pdu) {
  nfapi_tx_request_pdu_t *TX_req = &tx_req_body->tx_pdu_list[tx_req_body->number_of_pdus];
  LOG_D(MAC, "Filling TX_req %d for pdu length %d\n",
        tx_req_body->number_of_pdus, pdu_length);
  TX_req->pdu_length                 = pdu_length;
  TX_req->pdu_index                  = pdu_index;
  TX_req->num_segments               = 1;
  TX_req->segments[0].segment_length = pdu_length;
  TX_req->segments[0].segment_data   = pdu;
  tx_req_body->tl.tag                = NFAPI_TX_REQUEST_BODY_TAG;
  tx_req_body->number_of_pdus++;
  return (((absSF / 10) << 4) + (absSF % 10));
}

void
fill_dlsch_config(nfapi_dl_config_request_body_t *dl_req,
                  uint16_t length,
                  uint16_t pdu_index,
                  uint16_t rnti,
                  uint8_t resource_allocation_type,
                  uint8_t virtual_resource_block_assignment_flag,
                  uint16_t resource_block_coding,
                  uint8_t modulation,
                  uint8_t redundancy_version,
                  uint8_t transport_blocks,
                  uint8_t transport_block_to_codeword_swap_flag,
                  uint8_t transmission_scheme,
                  uint8_t number_of_layers,
                  uint8_t number_of_subbands,
                  //                             uint8_t codebook_index,
                  uint8_t ue_category_capacity,
                  uint8_t pa,
                  uint8_t delta_power_offset_index,
                  uint8_t ngap,
                  uint8_t nprb,
                  uint8_t transmission_mode,
                  uint8_t num_bf_prb_per_subband,
                  uint8_t num_bf_vector) {
  nfapi_dl_config_request_pdu_t *dl_config_pdu =
    &dl_req->dl_config_pdu_list[dl_req->number_pdu];
  memset((void *) dl_config_pdu, 0,
         sizeof(nfapi_dl_config_request_pdu_t));
  dl_config_pdu->pdu_type                                                        = NFAPI_DL_CONFIG_DLSCH_PDU_TYPE;
  dl_config_pdu->pdu_size                                                        = (uint8_t) (2 + sizeof(nfapi_dl_config_dlsch_pdu));
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.tl.tag                                 = NFAPI_DL_CONFIG_REQUEST_DLSCH_PDU_REL8_TAG;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.length                                 = length;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.pdu_index                              = pdu_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.rnti                                   = rnti;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.resource_allocation_type               = resource_allocation_type;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.virtual_resource_block_assignment_flag = virtual_resource_block_assignment_flag;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.resource_block_coding                  = resource_block_coding;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.modulation                             = modulation;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.redundancy_version                     = redundancy_version;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transport_blocks                       = transport_blocks;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transport_block_to_codeword_swap_flag  = transport_block_to_codeword_swap_flag;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transmission_scheme                    = transmission_scheme;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.number_of_layers                       = number_of_layers;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.number_of_subbands                     = number_of_subbands;
  //  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.codebook_index                         = codebook_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.ue_category_capacity                   = ue_category_capacity;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.pa                                     = pa;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.delta_power_offset_index               = delta_power_offset_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.ngap                                   = ngap;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.nprb                                   = nprb;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transmission_mode                      = transmission_mode;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.num_bf_prb_per_subband                 = num_bf_prb_per_subband;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.num_bf_vector                          = num_bf_vector;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel13.initial_transmission_sf_io            = 0xFFFF;
  dl_req->number_pdu++;
}

void fill_DCI(PHY_VARS_eNB *eNB,
              int frame,
              int subframe,
              Sched_Rsp_t *sched_resp,
              uint8_t input_buffer[NUMBER_OF_UE_MAX][20000],
              int n_rnti,
              int n_users,
              int transmission_mode,
              int retrans,
              int common_flag,
              int NB_RB,
              int DLSCH_RB_ALLOC,
              int TPC,
              int mcs1,
              int mcs2,
              int ndi,
              int rv,
              int pa,
              int *num_common_dci,
              int *num_ue_spec_dci,
              int *num_dci) {
  int k;
  nfapi_dl_config_request_body_t *dl_req=&sched_resp->DL_req->dl_config_request_body;
  nfapi_dl_config_request_pdu_t  *dl_config_pdu;
  nfapi_tx_request_body_t        *TX_req=&sched_resp->TX_req->tx_request_body;
  int NB_RB4TBS = common_flag == 0 ? NB_RB : (2+TPC);
  dl_req->number_dci=0;
  dl_req->number_pdu=0;
  TX_req->number_of_pdus=0;

  for(k=0; k<n_users; k++) {
    switch(transmission_mode) {
      case 1:
      case 2:
      case 7:
        dl_config_pdu = &dl_req->dl_config_pdu_list[dl_req->number_pdu];
        memset((void *) dl_config_pdu, 0,
               sizeof(nfapi_dl_config_request_pdu_t));
        dl_config_pdu->pdu_type = NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE;
        dl_config_pdu->pdu_size = (uint8_t) (2 + sizeof(nfapi_dl_config_dci_dl_pdu));
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.dci_format = (common_flag == 0) ? NFAPI_DL_DCI_FORMAT_1 : NFAPI_DL_DCI_FORMAT_1A;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level = 4;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tl.tag = NFAPI_DL_CONFIG_REQUEST_DCI_DL_PDU_REL8_TAG;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti = (common_flag == 0) ? n_rnti+k : SI_RNTI;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type = (common_flag ==0 ) ? 1: 2;  // CRNTI : see Table 4-10 from SCF082 - nFAPI specifications
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.transmission_power = 6000;  // equal to RS power
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.harq_process = 0;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tpc = TPC;  // dont adjust power when retransmitting
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.new_data_indicator_1 = (common_flag == 0) ? ndi : 0;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.mcs_1 = mcs1;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.redundancy_version_1 = rv;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.resource_block_coding = (common_flag == 0) ? DLSCH_RB_ALLOC : computeRIV(eNB->frame_parms.N_RB_DL,0,NB_RB);
        //deactivate second codeword
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.mcs_2 = 0;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.redundancy_version_2 = 1;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.downlink_assignment_index = 0;
        dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.cce_idx = 0;
        dl_req->number_dci++;
        dl_req->number_pdu++;
        dl_req->tl.tag = NFAPI_DL_CONFIG_REQUEST_BODY_TAG;
        AssertFatal(TPC>=0 && TPC<2, "TPC should be 0 or 1\n");
        fill_dlsch_config(dl_req,
                          get_TBS_DL(mcs1,NB_RB4TBS),
                          (retrans > 0) ? -1 : 0, /* retransmission, no pdu_index */
                          (common_flag == 0) ? n_rnti : SI_RNTI,
                          0,  // type 0 allocation from 7.1.6 in 36.213
                          0,  // virtual_resource_block_assignment_flag, unused here
                          DLSCH_RB_ALLOC, // resource_block_coding,
                          get_Qm(mcs1),
                          rv, // redundancy version
                          1,  // transport blocks
                          0,  // transport block to codeword swap flag
                          transmission_mode == 1 ? 0 : 1, // transmission_scheme
                          1,  // number of layers
                          1,  // number of subbands
                          //                      uint8_t codebook_index,
                          4,  // UE category capacity
                          pa,    // pa
                          0,  // delta_power_offset for TM5
                          0,  // ngap
                          0,  // nprb
                          transmission_mode,
                          0,  //number of PRBs treated as one subband, not used here
                          0 // number of beamforming vectors, not used here
                         );
        fill_tx_req(TX_req,
                    (frame * 10) + subframe,
                    get_TBS_DL(mcs1,NB_RB4TBS),
                    0,
                    input_buffer[k]);
        break;

      case 3:
        if (common_flag == 0) {
          if (eNB->frame_parms.nb_antennas_tx == 2) {
            if (eNB->frame_parms.frame_type == TDD) {
            } else {
            }
          }
        }

        break;

      case 4:
        if (common_flag == 0) {
          if (eNB->frame_parms.nb_antennas_tx == 2) {
            if (eNB->frame_parms.frame_type == TDD) {
            } else {
            }
          } else if (eNB->frame_parms.nb_antennas_tx == 4) {
          }
        } else {
        }

        break;

      case 5:
      case 6:
        break;

      default:
        printf("Unsupported Transmission Mode %d!!!\n",transmission_mode);
        exit(-1);
        break;
    }
  }

  *num_dci         = dl_req->number_dci;
  *num_ue_spec_dci = dl_req->number_dci;
  *num_common_dci  = 0;
}

int n_users = 1;
int subframe=7;
int num_common_dci=0,num_ue_spec_dci=0,num_dci=0,num_pdcch_symbols=1;
uint16_t n_rnti=0x1234;

int abstx=0;
int Nid_cell=0;
int N_RB_DL=25;
int tdd_config=3;
int dci_flag=0;
int threequarter_fs=0;
double snr_step=1,input_snr_step=1, snr_int=30;
double forgetting_factor=0.0; //in [0,1] 0 means a new channel every time, 1 means keep the same channel
int test_perf=0;
int n_frames;
int n_ch_rlz = 1;
int rx_sample_offset = 0;
int dump_table=0;
int loglvl=OAILOG_INFO;
int mcs1=0,mcs2=0,mcs_i=0,dual_stream_UE = 0,awgn_flag=0;
int two_thread_flag=0;
int num_rounds = 4;//,fix_rounds=0;
int perfect_ce = 0;
int extended_prefix_flag=0;
int verbose=0, help=0;
double SNR,snr0=-2.0,snr1,rate = 0;
int print_perf=0;
configmodule_interface_t *uniqCfg = NULL;

int main(int argc, char **argv) {
  int k,i,j,aa;
  int re;
  int s,Kr,Kr_bytes;
  LTE_DL_FRAME_PARMS *frame_parms;
  double *tmpTX[4], *tmpRX[4] ;
  for (int i = 0; i < 4; i++) {
    tmpTX[i] = malloc(30720 * NB_ANTENNAS_TX * sizeof(*tmpTX));
    tmpRX[i] = malloc(30720 * NB_ANTENNAS_RX * sizeof(*tmpRX));
  }
  double *s_re[NB_ANTENNAS_TX] = {tmpTX[0], tmpTX[1]};
  double *s_im[NB_ANTENNAS_TX] = {tmpTX[2], tmpTX[3]};
  double *r_re[NB_ANTENNAS_RX] = {tmpRX[0], tmpRX[1]};
  double *r_im[NB_ANTENNAS_RX] = {tmpRX[2], tmpRX[3]};
  
  uint8_t transmission_mode=1,n_tx_port=1,n_tx_phy=1,n_rx=2;
  int eNB_id = 0;
  unsigned char round;
  unsigned char i_mod = 2;
  int NB_RB;
  SCM_t channel_model=Rayleigh1;
  //  unsigned char *input_data,*decoded_output;
  DCI_ALLOC_t da;
  DCI_ALLOC_t *dci_alloc = &da;
  unsigned int coded_bits_per_codeword=0,nsymb; //,tbs=0;
  unsigned int tx_lev = 0, trials;
  int tx_lev_dB = 0;
  unsigned int errs[4],errs2[4],round_trials[4],dci_errors[4];//,num_layers;
  memset(errs,0,4*sizeof(unsigned int));
  memset(errs2,0,4*sizeof(unsigned int));
  memset(round_trials,0,4*sizeof(unsigned int));
  memset(dci_errors,0,4*sizeof(unsigned int));
  //int re_allocated;
  char fname[32],vname[32];
  FILE *bler_fd;
  char bler_fname[256];
  FILE *time_meas_fd=NULL;
  char time_meas_fname[256];
  //  FILE *tikz_fd;
  //  char tikz_fname[256];
  FILE *input_trch_fd=NULL;
  unsigned char input_trch_file=0;
  FILE *input_fd=NULL;
  unsigned char input_file=0;
  channel_desc_t *eNB2UE[4];
  //uint8_t num_pdcch_symbols_2=0;
  //char stats_buffer[4096];
  //int len;
  //int u;
  int n=0;
  //int iii;
  int ch_realization;
  //int pmi_feedback=0;
  int hold_channel=0;
  // void *data;
  // int ii;
  //  int bler;
  double blerr[4];
  short *uncoded_ber_bit=NULL;
  int osf=1;
  frame_t frame_type = FDD;
  int numCCE=0;
  //int dci_length_bytes=0,dci_length=0;
  //double channel_bandwidth = 5.0, sampling_rate=7.68;
  int common_flag=0,TPC=0;
  //  time_stats_t ts;//,sts,usts;
  int avg_iter,iter_trials;
  int rballocset=0;
  int test_passed=0;
  double effective_rate=0.0;
  char channel_model_input[10]="I";
  int TB0_active = 1;
  //  LTE_DL_UE_HARQ_t *dlsch0_ue_harq;
  //  LTE_DL_eNB_HARQ_t *dlsch0_eNB_harq;
  uint8_t Kmimo;
  uint8_t ue_category=4;
  uint32_t Nsoft;
  int sf;
  int CCE_table[800];
  cpu_meas_enabled = 1; // to enable the time meas
  FILE *csv_fd=NULL;
  char csv_fname[FILENAME_MAX];
  int DLSCH_RB_ALLOC = 0;
  int dci_received;
  PHY_VARS_eNB *eNB;
  RU_t *ru;
  PHY_VARS_UE *UE=NULL;
  nfapi_dl_config_request_t DL_req;
  nfapi_ul_config_request_t UL_req;
  nfapi_hi_dci0_request_t HI_DCI0_req;
  nfapi_dl_config_request_pdu_t dl_config_pdu_list[MAX_NUM_DL_PDU];
  nfapi_tx_request_pdu_t tx_pdu_list[MAX_NUM_TX_REQUEST_PDU];
  nfapi_tx_request_t TX_req;
  Sched_Rsp_t sched_resp;
  int pa=dB0;
  memset((void *)&sched_resp,0,sizeof(sched_resp));
  sched_resp.DL_req = &DL_req;
  sched_resp.UL_req = &UL_req;
  sched_resp.HI_DCI0_req = &HI_DCI0_req;
  sched_resp.TX_req = &TX_req;
  memset((void *)&DL_req,0,sizeof(DL_req));
  memset((void *)&UL_req,0,sizeof(UL_req));
  memset((void *)&HI_DCI0_req,0,sizeof(HI_DCI0_req));
  memset((void *)&TX_req,0,sizeof(TX_req));
  DL_req.dl_config_request_body.dl_config_pdu_list = dl_config_pdu_list;
  TX_req.tx_request_body.tx_pdu_list = tx_pdu_list;
  set_parallel_conf("PARALLEL_SINGLE_THREAD");
  //signal(SIGSEGV, handler);
  //signal(SIGABRT, handler);
  // default parameters
  n_frames = 1000;
  snr0 = 0;
  //  num_layers = 1;
  perfect_ce = 0;
  static paramdef_t options[] = {
    { "awgn", "Use AWGN channel and not multipath", PARAMFLAG_BOOL, .iptr=&awgn_flag, .defintval=0, TYPE_INT, 0, NULL, NULL },
    { "Abstx", "Turns on calibration mode for abstraction.", PARAMFLAG_BOOL, .iptr=&abstx,  .defintval=0, TYPE_INT, 0 },
    { "bTDD", "Set the tdd configuration mode",0, .iptr=&tdd_config,  .defintval=3, TYPE_INT, 0 },
    { "BnbRBs", "The LTE bandwith in RBs (100 is 20MHz)",0, .iptr=&N_RB_DL,  .defintval=25, TYPE_INT, 0 },
    { "cPdcch", "Number of PDCCH symbols",0, .iptr=&num_pdcch_symbols,  .defintval=1, TYPE_INT, 0 },
    { "CnidCell", "The cell id ",0, .iptr=&Nid_cell,  .defintval=0, TYPE_INT, 0 },
    { "dciFlag", "Transmit the DCI and compute its error statistics", PARAMFLAG_BOOL, .iptr=&dci_flag,  .defintval=0, TYPE_INT, 0 },
    { "Dtdd", "Enable tdd", PARAMFLAG_BOOL,  .strptr=NULL, .defintval=0, TYPE_INT, 0, NULL, NULL },
    { "eRounds", "Number of rounds",0, .iptr=NULL,  .defintval=25, TYPE_INT, 0 },
    { "EsubSampling","three quarters sub-sampling",PARAMFLAG_BOOL, .iptr=&threequarter_fs, .defintval=0, TYPE_INT, 0 },
    { "f_snr_step", "step size of SNR, default value is 1.",0, .dblptr=&input_snr_step,  .defdblval=1, TYPE_DOUBLE, 0 },
    { "Forgetting", "forgetting factor (0 new channel every trial, 1 channel constant)",0, .dblptr=&forgetting_factor,  .defdblval=0.0, TYPE_DOUBLE, 0 },
    { "input_file", "input IQ data file",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "Input_file_trch", " Input filename for TrCH data (binary)",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "WtwoThreads", "two_thread_flag", PARAMFLAG_BOOL, .iptr=&two_thread_flag,  .defintval=0, TYPE_INT, 0 },
    { "lMuMimo", "offset_mumimo_llr_drange_fix",0, .u8ptr=&offset_mumimo_llr_drange_fix,  .defintval=0, TYPE_UINT8, 0 },
    { "mcs1", "The MCS for TB 1", 0, .iptr=&mcs1,  .defintval=0, TYPE_INT, 0 },
    { "Mcs2", "The MCS for TB 2", 0, .iptr=&mcs2,  .defintval=0, TYPE_INT, 0 },
    { "Tperf", "Set the percenatge of effective rate to testbench the modem performance (typically 30 and 70, range 1-100)",0, .iptr=&test_perf,  .defintval=0, TYPE_INT, 0 },
    { "tmcs_i", "MCS of interfering UE",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "nb_frame", "number of frame in a test",0, .iptr=&n_frames,  .defintval=1, TYPE_INT, 0 },
    { "offsetRxSample", "Sample offset for receiver", 0, .iptr=&rx_sample_offset,  .defintval=0, TYPE_INT, 0 },
    { "rballocset", "ressource block allocation (see  section 7.1.6.3 in 36.213)",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "snr", "Starting SNR, runs from SNR to SNR+%.1fdB in steps of %.1fdB. If n_frames is 1 then just SNR is simulated and MATLAB/OCTAVE output is generated", .dblptr=&snr0,  .defdblval=-2.0, TYPE_DOUBLE, 0 },
    { "wsnrInterrupt", "snr int ?", 0, .dblptr=&snr_int,  .defdblval=30, TYPE_DOUBLE, 0 },
    { "N_ch_rlzN0", "Determines the number of Channel Realizations in Abstraction mode. Default value is 1",0, .iptr=&n_ch_rlz,  .defintval=1, TYPE_INT, 0 },
    { "prefix_extended","Enable extended prefix", PARAMFLAG_BOOL, .iptr=&extended_prefix_flag,  .defintval=0, TYPE_INT, 0 },
    { "RNumRound", "Number of HARQ rounds (fixed)",0, .iptr=&num_rounds,  .defintval=4, TYPE_INT, 0 },
    { "Subframe", "subframe ",0, .iptr=&subframe,  .defintval=7, TYPE_INT, 0 },
    { "Trnti", "rnti",0, .u16ptr=&n_rnti,  .defuintval=0x1234, TYPE_UINT16, 0 },
    { "vi_mod", "i_mod",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "Qparallel", "Enable parallel execution",0, .strptr=NULL,  .defstrval=NULL, TYPE_STRING,  0 },
    { "Performance", "Display CPU perfomance of each L1 piece", PARAMFLAG_BOOL,  .iptr=&print_perf,  .defintval=0, TYPE_INT, 0 },
    { "q_tx_port", "Number of TX antennas ports used in eNB",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "uEdual", "Enables the Interference Aware Receiver for TM5 (default is normal receiver)",0, .iptr=NULL,  .defintval=0, TYPE_INT, 0 },
    { "xTransmission","Transmission mode (1,2,6,7 for the moment)",0, .iptr=NULL,  .defintval=25, TYPE_INT, 0 },
    { "yn_tx_phy","Number of TX antennas used in eNB",0, .iptr=NULL,  .defintval=25, TYPE_INT, 0 },
    { "Yperfect_ce","Perfect CE", PARAMFLAG_BOOL, .iptr=&perfect_ce,  .defintval=0, TYPE_INT, 0 },
    { "Zdump", "dump table",PARAMFLAG_BOOL,  .iptr=&dump_table, .defintval=0, TYPE_INT, 0 },
    { "Loglvl", "log level",0, .iptr=&loglvl,  .defintval=OAILOG_INFO, TYPE_INT, 0 },
    { "zn_rx", "Number of RX antennas used in UE",0, .iptr=NULL,  .defintval=2, TYPE_INT, 0 },
    { "gchannel", "[A:M] Use 3GPP 25.814 SCM-A/B/C/D('A','B','C','D') or 36-101 EPA('E'), EVA ('F'),ETU('G') models (ignores delay spread and Ricean factor), Rayghleigh8 ('H'), Rayleigh1('I'), Rayleigh1_corr('J'), Rayleigh1_anticorr ('K'),  Rice8('L'), Rice1('M')",0, .strptr=NULL,  .defstrval=NULL, TYPE_STRING, 0 },
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
      case 'a':
        awgn_flag = 1;
        channel_model = AWGN;
        break;

      case 'D':
        frame_type=TDD;
        break;

      case 'e':
        num_rounds=1;
        common_flag = 1;
        TPC = atoi(optarg);
        break;

      case 'i':
        input_fd = fopen(optarg,"r");
        input_file=1;
        dci_flag = 1;
        break;

      case 'I':
        input_trch_fd = fopen(optarg,"r");
        input_trch_file=1;
        break;

      case 't':
        mcs_i = atoi(optarg);
        i_mod = get_Qm(mcs_i);
        break;

      case 'r':
        DLSCH_RB_ALLOC = atoi(optarg);
        rballocset = 1;
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
          {'F',EVA,6},
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
            break;
          }

        AssertFatal(ptr->opt != 0, "Unsupported channel model: %s !\n", optarg );
        break;

      case 'u':
        dual_stream_UE=1;

        if (UE != NULL)
          UE->use_ia_receiver = 1;
        else {
          printf("UE  is NULL\n");
          exit(-1);
        }

        if ((n_tx_port!=2) || (transmission_mode!=5)) {
          printf("IA receiver only supported for TM5!");
          exit(-1);
        }

        break;

      case 'v':
        i_mod = atoi(optarg);

        if (i_mod!=2 && i_mod!=4 && i_mod!=6) {
          printf("Wrong i_mod %d, should be 2,4 or 6\n",i_mod);
          exit(-1);
        }

        break;

      case 'q':
        n_tx_port=atoi(optarg);

        if ((n_tx_port==0) || ((n_tx_port>2))) {
          printf("Unsupported number of cell specific antennas ports %d\n",n_tx_port);
          exit(-1);
        }

        break;

      case 'x':
        transmission_mode=atoi(optarg);

        if ((transmission_mode!=1) &&
            (transmission_mode!=2) &&
            (transmission_mode!=3) &&
            (transmission_mode!=4) &&
            (transmission_mode!=5) &&
            (transmission_mode!=6) &&
            (transmission_mode!=7)) {
          printf("Unsupported transmission mode %d\n",transmission_mode);
          exit(-1);
        }

        if (transmission_mode>1 && transmission_mode<7) {
          n_tx_port = 2;
        }

        break;

      case 'y':
        n_tx_phy=atoi(optarg);

        if (n_tx_phy < n_tx_port) {
          printf("n_tx_phy mush not be smaller than n_tx_port");
          exit(-1);
        }

        if ((transmission_mode>1 && transmission_mode<7) && n_tx_port<2) {
          printf("n_tx_port must be >1 for transmission_mode %d\n",transmission_mode);
          exit(-1);
        }

        if (transmission_mode==7 && (n_tx_phy!=1 && n_tx_phy!=2 && n_tx_phy!=4 && n_tx_phy!=8 && n_tx_phy!=16 && n_tx_phy!=64 && n_tx_phy!=128)) {
          printf("Physical number of antennas not supported for TM7.\n");
          exit(-1);
        }

        break;

      case 'z':
        n_rx=atoi(optarg);

        if ((n_rx==0) || (n_rx>2)) {
          printf("Unsupported number of rx antennas %d\n",n_rx);
          exit(-1);
        }

        break;

      case 'Q':
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

  if (transmission_mode>1) pa=dBm3;

  printf("dlsim: tmode %d, pa %d\n",transmission_mode,pa);
  uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  AssertFatal(uniqCfg != NULL, "Cannot load configuration module, exiting\n");
  logInit();
  set_glog_onlinelog(true);
  // enable these lines if you need debug info
  set_glog(loglvl);
  // SET_LOG_DEBUG(DEBUG_UE_TIMING);
  // moreover you need to init itti with the following line
  // however itti will catch all signals, so ctrl-c won't work anymore
  // alternatively you can disable ITTI completely in CMakeLists.txt

  if (common_flag == 0) {
    switch (N_RB_DL) {
      case 6:
        if (rballocset==0) DLSCH_RB_ALLOC = 0x3f;

        num_pdcch_symbols = 3;
        break;

      case 25:
        if (rballocset==0) DLSCH_RB_ALLOC = 0x1fff;

        break;

      case 50:
        if (rballocset==0) DLSCH_RB_ALLOC = 0x1ffff;

        break;

      case 100:
        if (rballocset==0) DLSCH_RB_ALLOC = 0x1ffffff;

        break;
    }

    NB_RB = conv_nprb(0,DLSCH_RB_ALLOC,N_RB_DL);
  } else {
    if (rballocset==0) NB_RB = 2+TPC;
    else               NB_RB = DLSCH_RB_ALLOC;

    printf("Common PDSCH: NB_RB = %d\n",NB_RB);
    AssertFatal(NB_RB <= N_RB_DL,"illegal NB_RB %d\n",NB_RB);
  }


  if (transmission_mode==5) {
    n_users = 2;
    printf("dual_stream_UE=%d\n", dual_stream_UE);
  }

  RC.nb_L1_inst = 1;
  RC.nb_RU = 1;
  lte_param_init(&eNB,&UE,&ru,
                 n_tx_port,
                 n_tx_phy,
                 1,
                 n_rx,
                 transmission_mode,
                 extended_prefix_flag,
                 frame_type,
                 Nid_cell,
                 tdd_config,
                 N_RB_DL,
                 pa,
                 threequarter_fs,
                 osf,
                 perfect_ce);
  RC.eNB = (PHY_VARS_eNB ** *)malloc(sizeof(PHY_VARS_eNB **));
  RC.eNB[0] = (PHY_VARS_eNB **)malloc(sizeof(PHY_VARS_eNB *));
  RC.ru = (RU_t **)malloc(sizeof(RC.ru));
  RC.eNB[0][0] = eNB;
  RC.ru[0] = ru;
  printf("lte_param_init done\n");

  if ((transmission_mode==1) || (transmission_mode==7)) {
    for (aa=0; aa<ru->nb_tx; aa++)
      for (re=0; re<ru->frame_parms->ofdm_symbol_size; re++)
        ru->beam_weights[0][0][aa][re] = 0x00007fff/eNB->frame_parms.nb_antennas_tx;
  }

  if (transmission_mode<7)
    ru->do_precoding=0;
  else
    ru->do_precoding=1;

  eNB->mac_enabled=1;

  // callback functions required for phy_procedures_tx
  //  eNB_id_i = UE->n_connected_eNB;
  printf("Setting mcs1 = %d\n",mcs1);
  printf("Setting mcs2 = %d\n",mcs2);
  printf("NPRB = %d\n",NB_RB);
  printf("n_frames = %d\n",n_frames);
  printf("Transmission mode %d with %dx%d antenna configuration, Extended Prefix %d\n",transmission_mode,n_tx_phy,n_rx,extended_prefix_flag);
  snr1 = snr0+snr_int;
  printf("SNR0 %f, SNR1 %f\n",snr0,snr1);
  uint8_t input_buffer[NUMBER_OF_UE_MAX][20000];

  for (i=0; i<n_users; i++)
    for (j=0; j<20000; j++) input_buffer[i][j] = (uint8_t)((taus())&255);

  frame_parms = &eNB->frame_parms;
  nsymb = (eNB->frame_parms.Ncp == 0) ? 14 : 12;
  printf("Channel Model= (%s,%d)\n",channel_model_input, channel_model);
  printf("SCM-A=%d, SCM-B=%d, SCM-C=%d, SCM-D=%d, EPA=%d, EVA=%d, ETU=%d, Rayleigh8=%d, Rayleigh1=%d, Rayleigh1_corr=%d, Rayleigh1_anticorr=%d, Rice1=%d, Rice8=%d\n",
         SCM_A, SCM_B, SCM_C, SCM_D, EPA, EVA, ETU, Rayleigh8, Rayleigh1, Rayleigh1_corr, Rayleigh1_anticorr, Rice1, Rice8);

  if(transmission_mode==5)
    sprintf(bler_fname,"bler_tx%d_chan%d_nrx%d_mcs%d_mcsi%d_u%d_imod%d.csv",transmission_mode,channel_model,n_rx,mcs1,mcs_i,dual_stream_UE,i_mod);
  else
    sprintf(bler_fname,"bler_tx%d_chan%d_nrx%d_mcs%d.csv",transmission_mode,channel_model,n_rx,mcs1);

  bler_fd = fopen(bler_fname,"w");

  if (bler_fd==NULL) {
    fprintf(stderr,"Cannot create file %s!\n",bler_fname);
    exit(-1);
  }

  fprintf(bler_fd,"SNR; MCS; TBS; rate; err0; trials0; err1; trials1; err2; trials2; err3; trials3; dci_err\n");

  if (test_perf != 0) {
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    printf("Hostname: %s\n", hostname);
    sprintf(time_meas_fname,"time_meas_prb%d_mcs%d_anttx%d_antrx%d_pdcch%d_channel%s_tx%d.csv",
            N_RB_DL,mcs1,n_tx_phy,n_rx,num_pdcch_symbols,channel_model_input,transmission_mode);
    //mkdir(dirname,0777);
    time_meas_fd = fopen(time_meas_fname,"w");

    if (time_meas_fd==NULL) {
      fprintf(stderr,"Cannot create file %s!\n",time_meas_fname);
      exit(-1);
    }
  }

  if(abstx) {
    // CSV file
    sprintf(csv_fname,"dataout_tx%d_u2%d_mcs%d_chan%d_nsimus%d_R%d.m",transmission_mode,dual_stream_UE,mcs1,channel_model,n_frames,num_rounds);
    csv_fd = fopen(csv_fname,"w");

    if (csv_fd==NULL) {
      fprintf(stderr,"Cannot create file %s!\n",csv_fname);
      exit(-1);
    }

    fprintf(csv_fd,"data_all%d=[",mcs1);
  }

  /*
  //sprintf(tikz_fname, "second_bler_tx%d_u2=%d_mcs%d_chan%d_nsimus%d.tex",transmission_mode,dual_stream_UE,mcs,channel_model,n_frames);
  sprintf(tikz_fname, "second_bler_tx%d_u2%d_mcs%d_chan%d_nsimus%d",transmission_mode,dual_stream_UE,mcs,channel_model,n_frames);
  tikz_fd = fopen(tikz_fname,"w");
  //fprintf(tikz_fd,"\\addplot[color=red, mark=o] plot coordinates {");
  switch (mcs)
    {
    case 0:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=star] plot coordinates {");
      break;
    case 1:
      fprintf(tikz_fd,"\\addplot[color=red, mark=star] plot coordinates {");
      break;
    case 2:
      fprintf(tikz_fd,"\\addplot[color=green, mark=star] plot coordinates {");
      break;
    case 3:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=star] plot coordinates {");
      break;
    case 4:
      fprintf(tikz_fd,"\\addplot[color=black, mark=star] plot coordinates {");
      break;
    case 5:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=o] plot coordinates {");
      break;
    case 6:
      fprintf(tikz_fd,"\\addplot[color=red, mark=o] plot coordinates {");
      break;
    case 7:
      fprintf(tikz_fd,"\\addplot[color=green, mark=o] plot coordinates {");
      break;
    case 8:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=o] plot coordinates {");
      break;
    case 9:
      fprintf(tikz_fd,"\\addplot[color=black, mark=o] plot coordinates {");
      break;
    case 10:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=square] plot coordinates {");
      break;
    case 11:
      fprintf(tikz_fd,"\\addplot[color=red, mark=square] plot coordinates {");
      break;
    case 12:
      fprintf(tikz_fd,"\\addplot[color=green, mark=square] plot coordinates {");
      break;
    case 13:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=square] plot coordinates {");
      break;
    case 14:
      fprintf(tikz_fd,"\\addplot[color=black, mark=square] plot coordinates {");
      break;
    case 15:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=diamond] plot coordinates {");
      break;
    case 16:
      fprintf(tikz_fd,"\\addplot[color=red, mark=diamond] plot coordinates {");
      break;
    case 17:
      fprintf(tikz_fd,"\\addplot[color=green, mark=diamond] plot coordinates {");
      break;
    case 18:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=diamond] plot coordinates {");
      break;
    case 19:
      fprintf(tikz_fd,"\\addplot[color=black, mark=diamond] plot coordinates {");
      break;
    case 20:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=x] plot coordinates {");
      break;
    case 21:
      fprintf(tikz_fd,"\\addplot[color=red, mark=x] plot coordinates {");
      break;
    case 22:
      fprintf(tikz_fd,"\\addplot[color=green, mark=x] plot coordinates {");
      break;
    case 23:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=x] plot coordinates {");
      break;
    case 24:
      fprintf(tikz_fd,"\\addplot[color=black, mark=x] plot coordinates {");
      break;
    case 25:
      fprintf(tikz_fd,"\\addplot[color=blue, mark=x] plot coordinates {");
      break;
    case 26:
      fprintf(tikz_fd,"\\addplot[color=red, mark=+] plot coordinates {");
      break;
    case 27:
      fprintf(tikz_fd,"\\addplot[color=green, mark=+] plot coordinates {");
      break;
    case 28:
      fprintf(tikz_fd,"\\addplot[color=yellow, mark=+] plot coordinates {");
      break;
    }
  */
  UE->pdcch_vars[UE->current_thread_id[subframe]][0]->crnti = n_rnti;
  UE->n_connected_eNB = 1;
  printf("Allocating %dx%d eNB->UE channel descriptor\n",eNB->frame_parms.nb_antennas_tx,UE->frame_parms.nb_antennas_rx);
  eNB2UE[0] = new_channel_desc_scm(eNB->frame_parms.nb_antennas_tx,
                                   UE->frame_parms.nb_antennas_rx,
                                   channel_model,
                                   N_RB2sampling_rate(eNB->frame_parms.N_RB_DL),
                                   0,
                                   N_RB2channel_bandwidth(eNB->frame_parms.N_RB_DL),
                                   DS_TDL,
                                   0.0,
                                   CORR_LEVEL_LOW,
                                   forgetting_factor,
                                   rx_sample_offset,
                                   0,
                                   0);
  reset_meas(&eNB2UE[0]->random_channel);
  reset_meas(&eNB2UE[0]->interp_time);

  if(num_rounds>1) {
    for(n=1; n<4; n++) {
      eNB2UE[n] = new_channel_desc_scm(eNB->frame_parms.nb_antennas_tx,
                                       UE->frame_parms.nb_antennas_rx,
                                       channel_model,
                                       N_RB2sampling_rate(eNB->frame_parms.N_RB_DL),
                                       0,
                                       N_RB2channel_bandwidth(eNB->frame_parms.N_RB_DL),
                                       DS_TDL,
                                       0.0,
                                       CORR_LEVEL_LOW,
                                       forgetting_factor,
                                       rx_sample_offset,
                                       0,
                                       0);
      reset_meas(&eNB2UE[n]->random_channel);
      reset_meas(&eNB2UE[n]->interp_time);
    }
  }

  if (eNB2UE[0]==NULL) {
    printf("Problem generating channel model. Exiting.\n");
    exit(-1);
  }

  if ((transmission_mode == 3) || (transmission_mode==4))
    Kmimo=2;
  else
    Kmimo=1;

  switch (ue_category) {
    case 1:
      Nsoft = 250368;
      break;

    case 2:
    case 3:
      Nsoft = 1237248;
      break;

    case 4:
      Nsoft = 1827072;
      break;

    default:
      printf("Unsupported UE category %d\n",ue_category);
      exit(-1);
      break;
  }

  for (k=0; k<NUMBER_OF_DLSCH_MAX; k++) {
    // Create transport channel structures for 2 transport blocks (MIMO)
    for (i=0; i<2; i++) {
      eNB->dlsch[k][i] = new_eNB_dlsch(Kmimo,8,Nsoft,N_RB_DL,0,&eNB->frame_parms);

      if (!eNB->dlsch[k][i]) {
        printf("Can't get eNB dlsch structures\n");
        exit(-1);
      }

      eNB->dlsch[k][i]->rnti = n_rnti+k;
    }
  }
  
  for (int i=0;i<NUMBER_OF_ULSCH_MAX; i++) {
    
    LOG_I(PHY,"Allocating Transport Channel Buffer for ULSCH %d/%d\n",i,NUMBER_OF_ULSCH_MAX);
    eNB->ulsch[i] = new_eNB_ulsch(MAX_TURBO_ITERATIONS,eNB->frame_parms.N_RB_UL, 0);
    
    if (!eNB->ulsch[i]) {
      LOG_E(PHY,"Can't get eNB ulsch structures\n");
      exit(-1);
    }
  }

  /* allocate memory for both subframes (only one is really used
   * but there is now "copy_harq_proc_struct" which needs both
   * to be valid)
   * TODO: refine this somehow (necessary?)
   */
  for (sf = 0; sf < 2; sf++) {
    for (i=0; i<2; i++) {
      UE->dlsch[sf][0][i]  = new_ue_dlsch(Kmimo,8,Nsoft,MAX_TURBO_ITERATIONS,N_RB_DL,0);

      if (!UE->dlsch[sf][0][i]) {
        printf("Can't get ue dlsch structures\n");
        exit(-1);
      }

      UE->dlsch[sf][0][i]->rnti   = n_rnti;
    }
  }

  UE->dlsch_SI[0]  = new_ue_dlsch(1,1,Nsoft,MAX_TURBO_ITERATIONS,N_RB_DL,0);
  UE->dlsch_ra[0]  = new_ue_dlsch(1,1,Nsoft,MAX_TURBO_ITERATIONS,N_RB_DL,0);
  UE->ulsch[0] = new_ue_ulsch(N_RB_DL,0);
  // structure for SIC at UE
  UE->dlsch_eNB[0] = new_eNB_dlsch(Kmimo,8,Nsoft,N_RB_DL,0,&eNB->frame_parms);

  if (DLSCH_alloc_pdu2_1E[0].tpmi == 5) {
    eNB->UE_stats[0].DL_pmi_single = (unsigned short)(taus()&0xffff);

    if (n_users>1)
      eNB->UE_stats[1].DL_pmi_single = (eNB->UE_stats[0].DL_pmi_single ^ 0x1555); //opposite PMI
  } else {
    eNB->UE_stats[0].DL_pmi_single = 0;

    if (n_users>1)
      eNB->UE_stats[1].DL_pmi_single = 0;
  }

  L1_rxtx_proc_t *proc_eNB = &eNB->proc.L1_proc;
  proc_eNB->threadPool = (tpool_t *)malloc(sizeof(tpool_t));
  initTpool("n", proc_eNB->threadPool, true);

  proc_eNB->frame_tx=0;

  if (input_fd==NULL) {
    DL_req.dl_config_request_body.number_pdcch_ofdm_symbols = num_pdcch_symbols;
    DL_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
    TX_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
    // UE specific DCI
    fill_DCI(eNB,
             proc_eNB->frame_tx,subframe,
             &sched_resp,
             input_buffer,
             n_rnti,
             n_users,
             transmission_mode,
             0,
             common_flag,
             NB_RB,
             DLSCH_RB_ALLOC,
             TPC,
             mcs1,
             mcs2,
             1,
             0,
             pa,
             &num_common_dci,
             &num_ue_spec_dci,
             &num_dci);
    numCCE = get_nCCE(num_pdcch_symbols,&eNB->frame_parms,get_mi(&eNB->frame_parms,subframe));

    if (n_frames==1) printf("num_pdcch_symbols %d, numCCE %d, num_dci %d/%d/%d\n",num_pdcch_symbols,numCCE, num_dci,num_ue_spec_dci,num_common_dci);
  }

  snr_step = input_snr_step;
  UE->high_speed_flag = 1;
  UE->ch_est_alpha=0;

  // initialize sorted lists for timers for which we want the distribution
  init_sorted_list_meas(&eNB->phy_proc_tx, n_frames * num_rounds);
  init_sorted_list_meas(&eNB->ofdm_mod_stats, n_frames * num_rounds);
  init_sorted_list_meas(&eNB->dlsch_modulation_stats, n_frames * num_rounds);
  init_sorted_list_meas(&eNB->dlsch_encoding_stats, n_frames * num_rounds);
  init_sorted_list_meas(&UE->phy_proc_rx[0], n_frames * num_rounds);
  init_sorted_list_meas(&UE->phy_proc_rx[1], n_frames * num_rounds);
  time_stats_t phy_proc_rx_tot;
  init_sorted_list_meas(&phy_proc_rx_tot, 2 * n_frames * num_rounds);
  init_sorted_list_meas(&UE->ofdm_demod_stats, n_frames * num_rounds);
  init_sorted_list_meas(&UE->dlsch_rx_pdcch_stats, n_frames * num_rounds);
  init_sorted_list_meas(&UE->dlsch_decoding_stats[0], n_frames * num_rounds);
  init_sorted_list_meas(&UE->dlsch_decoding_stats[1], n_frames * num_rounds);

  for (ch_realization=0; ch_realization<n_ch_rlz; ch_realization++) {
    if(abstx) {
      printf("**********************Channel Realization Index = %d **************************\n", ch_realization);
    }

    for (SNR=snr0; SNR<snr1; SNR+=snr_step) {
      UE->proc.proc_rxtx[UE->current_thread_id[subframe]].frame_rx=0;
      errs[0]=0;
      errs[1]=0;
      errs[2]=0;
      errs[3]=0;
      errs2[0]=0;
      errs2[1]=0;
      errs2[2]=0;
      errs2[3]=0;
      round_trials[0] = 0;
      round_trials[1] = 0;
      round_trials[2] = 0;
      round_trials[3] = 0;
      dci_errors[0]=0;
      dci_errors[1]=0;
      dci_errors[2]=0;
      dci_errors[3]=0;
      //      avg_ber = 0;
      round=0;
      avg_iter = 0;
      iter_trials=0;
      reset_meas(&eNB->phy_proc_tx); // total eNB tx
      reset_meas(&eNB->dlsch_scrambling_stats);
      reset_meas(&UE->dlsch_unscrambling_stats);
      reset_meas(&eNB->ofdm_mod_stats);
      reset_meas(&eNB->dlsch_modulation_stats);
      reset_meas(&eNB->dlsch_encoding_stats);
      reset_meas(&eNB->dlsch_interleaving_stats);
      reset_meas(&eNB->dlsch_rate_matching_stats);
      reset_meas(&eNB->dlsch_turbo_encoding_stats);
      reset_meas(&eNB->dlsch_common_and_dci);
      reset_meas(&eNB->dlsch_ue_specific);

      for (int i=0; i<RX_NB_TH; i++) {
        reset_meas(&UE->phy_proc_rx[i]); // total UE rx
        reset_meas(&UE->ue_front_end_stat[i]);
        reset_meas(&UE->pdsch_procedures_stat[i]);
        reset_meas(&UE->dlsch_procedures_stat[i]);
        reset_meas(&UE->dlsch_decoding_stats[i]);
        reset_meas(&UE->dlsch_llr_stats_parallelization[i][0]);
        reset_meas(&UE->dlsch_llr_stats_parallelization[i][1]);
      }

      reset_meas(&UE->ofdm_demod_stats);
      reset_meas(&UE->crnti_procedures_stats);
      reset_meas(&UE->dlsch_channel_estimation_stats);
      reset_meas(&UE->dlsch_freq_offset_estimation_stats);
      reset_meas(&UE->rx_dft_stats);
      reset_meas(&UE->dlsch_rx_pdcch_stats);
      reset_meas(&UE->dlsch_decoding_stats[0]);
      reset_meas(&UE->dlsch_decoding_stats[1]);
      reset_meas(&UE->dlsch_turbo_decoding_stats);
      reset_meas(&UE->dlsch_deinterleaving_stats);
      reset_meas(&UE->dlsch_rate_unmatching_stats);
      reset_meas(&UE->dlsch_tc_init_stats);
      reset_meas(&UE->dlsch_tc_alpha_stats);
      reset_meas(&UE->dlsch_tc_beta_stats);
      reset_meas(&UE->dlsch_tc_gamma_stats);
      reset_meas(&UE->dlsch_tc_ext_stats);
      reset_meas(&UE->dlsch_tc_intl1_stats);
      reset_meas(&UE->dlsch_tc_intl2_stats);
      // initialization
      // initialization
      time_stats_t pdsch_procedures_tot;
      time_stats_t dlsch_procedures_tot;
      time_stats_t dlsch_decoding_tot;
      time_stats_t dlsch_llr_tot;
      time_stats_t ue_front_end_tot;

      for (trials = 0; trials<n_frames; trials++) {
        //printf("Trial %d\n",trials);
        fflush(stdout);
        round=0;
        //if (trials%100==0)
        eNB2UE[0]->first_run = 1;
        UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->harq_ack[subframe].ack = 0;
        UE->dlsch[UE->current_thread_id[subframe]][eNB_id][1]->harq_ack[subframe].ack = 0;

        while ((round < num_rounds) && (UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->harq_ack[subframe].ack == 0)) {
          //    printf("Trial %d, round %d\n",trials,round);
          round_trials[round]++;

          //if(transmission_mode>=5)
          //  pmi_feedback=1;
          //else
          //  pmi_feedback=0;

          if (abstx) {
            if (trials==0 && round==0 && SNR==snr0)  //generate a new channel
              hold_channel = 0;
            else
              hold_channel = 1;
          } else
            hold_channel = 0;//(round==0) ? 0 : 1;

          //PMI_FEEDBACK:

          //  printf("Trial %d : Round %d, pmi_feedback %d \n",trials,round,pmi_feedback);
          for (aa=0; aa<eNB->frame_parms.nb_antennas_tx; aa++) {
            memset(&eNB->common_vars.txdataF[aa][0],0,FRAME_LENGTH_COMPLEX_SAMPLES_NO_PREFIX*sizeof(int32_t));
          }

          if (input_fd==NULL) {
            // Simulate HARQ procedures!!!
            memset(CCE_table,0,800*sizeof(int));

            if (/*common_flag == 0*/ 1) {
              num_dci=0;
              num_common_dci=0;
              num_ue_spec_dci=0;

              if (round == 0) {   // First round
                TB0_active = 1;
                eNB->dlsch[0][0]->harq_processes[0]->rvidx = round&3;
                DL_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
                TX_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
                fill_DCI(eNB,proc_eNB->frame_tx,subframe,&sched_resp,input_buffer,n_rnti,n_users,transmission_mode,0,common_flag,NB_RB,DLSCH_RB_ALLOC,TPC,
                         mcs1,mcs2,!(trials&1),round&3,pa,&num_common_dci,&num_ue_spec_dci,&num_dci);
              } else {
                DL_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
                TX_req.sfn_sf = (proc_eNB->frame_tx<<4)+subframe;
                fill_DCI(eNB,proc_eNB->frame_tx,subframe,&sched_resp,input_buffer,n_rnti,n_users,transmission_mode,1,common_flag,NB_RB,DLSCH_RB_ALLOC,TPC,
                         (TB0_active==1)?mcs1:0,mcs2,!(trials&1),(TB0_active==1)?round&3:0,pa,&num_common_dci,&num_ue_spec_dci,&num_dci);
              }
            }

            proc_eNB->subframe_tx = subframe;
            sched_resp.subframe=subframe;
            sched_resp.frame=proc_eNB->frame_tx;
            eNB->abstraction_flag=0;
            schedule_response(&sched_resp, proc_eNB);
            phy_procedures_eNB_TX(eNB,proc_eNB,1);

            if (uncoded_ber_bit == NULL) {
              // this is for user 0 only
              printf("nb_rb %d, rb_alloc %x, mcs %d\n",
                     eNB->dlsch[0][0]->harq_processes[0]->nb_rb,
                     eNB->dlsch[0][0]->harq_processes[0]->rb_alloc[0],
                     eNB->dlsch[0][0]->harq_processes[0]->mcs);
              coded_bits_per_codeword = get_G(&eNB->frame_parms,
                                              eNB->dlsch[0][0]->harq_processes[0]->nb_rb,
                                              eNB->dlsch[0][0]->harq_processes[0]->rb_alloc,
                                              get_Qm(eNB->dlsch[0][0]->harq_processes[0]->mcs),
                                              eNB->dlsch[0][0]->harq_processes[0]->Nl,
                                              num_pdcch_symbols,
                                              0,
                                              subframe,
                                              transmission_mode>=7?transmission_mode:0);
              uncoded_ber_bit = (short *) malloc(sizeof(short)*coded_bits_per_codeword);
              printf("uncoded_ber_bit=%p\n",uncoded_ber_bit);
            }

            start_meas(&eNB->ofdm_mod_stats);
            ru->proc.tti_tx=subframe;
            memcpy((void *)ru->frame_parms,(void *)&eNB->frame_parms,sizeof(LTE_DL_FRAME_PARMS));
            feptx_prec(ru,proc_eNB->frame_tx,subframe);
            feptx_ofdm(ru,proc_eNB->frame_tx,subframe);
            stop_meas(&eNB->ofdm_mod_stats);
            // generate next subframe for channel estimation
            DL_req.dl_config_request_body.number_dci=0;
            DL_req.dl_config_request_body.number_pdu=0;
            TX_req.tx_request_body.number_of_pdus=0;
            proc_eNB->subframe_tx = subframe+1;
            sched_resp.subframe=subframe+1;
            schedule_response(&sched_resp, proc_eNB);
            phy_procedures_eNB_TX(eNB,proc_eNB,0);
            ru->proc.tti_tx=(subframe+1)%10;
            feptx_prec(ru,proc_eNB->frame_tx,subframe+1);
            feptx_ofdm(ru,proc_eNB->frame_tx,subframe+1);
            proc_eNB->frame_tx++;
            tx_lev = 0;

            for (aa=0; aa<eNB->frame_parms.nb_antennas_tx; aa++) {
              tx_lev += signal_energy(&ru->common.txdata[aa]
                                      [subframe*eNB->frame_parms.samples_per_tti],
                                      eNB->frame_parms.samples_per_tti);
            }

            tx_lev_dB = dB_fixed(tx_lev);

            if (n_frames==1) {
              printf("tx_lev = %u (%u dB)\n",tx_lev,tx_lev_dB);
              LOG_M("txsig0.m","txs0", &ru->common.txdata[0][subframe* eNB->frame_parms.samples_per_tti], eNB->frame_parms.samples_per_tti,1,1);

              if (transmission_mode<7) {
                LOG_M("txsigF0.m","txsF0x", &ru->common.txdataF_BF[0][0],nsymb*eNB->frame_parms.ofdm_symbol_size,1,1);
              } else if (transmission_mode == 7) {
                LOG_M("txsigF0.m","txsF0", &ru->common.txdataF_BF[5][0],nsymb*eNB->frame_parms.ofdm_symbol_size,1,1);
                LOG_M("txsigF0_BF.m","txsF0_BF", &ru->common.txdataF_BF[0][0],eNB->frame_parms.ofdm_symbol_size,1,1);
              }
            }
          }

          DL_channel(ru,UE,subframe,awgn_flag,SNR,tx_lev,hold_channel,abstx,num_rounds,trials,round,eNB2UE,s_re,s_im,r_re,r_im,csv_fd);
          UE_rxtx_proc_t *proc = &UE->proc.proc_rxtx[UE->current_thread_id[subframe]];
          proc->subframe_rx = subframe;
          UE->UE_mode[0] = PUSCH;
          // first symbol has to be done separately in one-shot mode
          slot_fep(UE,
                   0,
                   (proc->subframe_rx<<1),
                   UE->rx_offset,
                   0,
                   0);

          if (n_frames==1) printf("Running phy_procedures_UE_RX\n");

          if (dci_flag==0) {
            memcpy(dci_alloc,eNB->pdcch_vars[subframe&1].dci_alloc,num_dci*sizeof(DCI_ALLOC_t));
            UE->pdcch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id]->num_pdcch_symbols = num_pdcch_symbols;

            if (n_frames==1)
              printf("bypassing PDCCH/DCI detection\n");

            if  (generate_ue_dlsch_params_from_dci(proc->frame_rx,
                                                   proc->subframe_rx,
                                                   (void *)&dci_alloc[0].dci_pdu,
                                                   common_flag == 0 ? n_rnti : SI_RNTI,
                                                   dci_alloc[0].format,
                                                   UE->pdcch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id],
                                                   UE->pdsch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id],
                                                   UE->dlsch[UE->current_thread_id[proc->subframe_rx]][0],
                                                   &UE->frame_parms,
                                                   UE->pdsch_config_dedicated,
                                                   SI_RNTI,
                                                   0,
                                                   P_RNTI,
                                                   UE->transmission_mode[eNB_id]<7?0:UE->transmission_mode[eNB_id],
                                                   0)==0) {
              dump_dci(&UE->frame_parms, &dci_alloc[0]);
              //UE->dlsch[UE->current_thread_id[proc->subframe_rx]][eNB_id][0]->active = 1;
              //UE->dlsch[UE->current_thread_id[proc->subframe_rx]][eNB_id][1]->active = 1;
              UE->pdcch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id]->num_pdcch_symbols = num_pdcch_symbols;
              UE->dlsch_received[eNB_id]++;
            } else {
              LOG_E(PHY,"Problem in DCI!\n");
            }
          }

          dci_received = UE->pdcch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id]->dci_received;
          phy_procedures_UE_RX(UE,proc,0,0,dci_flag,normal_txrx);
          dci_received = dci_received - UE->pdcch_vars[UE->current_thread_id[proc->subframe_rx]][eNB_id]->dci_received;

          if (dci_flag && (dci_received == 0)) {
            printf("DCI not received\n");
            dci_errors[round]++;
            LOG_M("pdcchF0_ext.m","pdcchF_ext", UE->pdcch_vars[0][eNB_id]->rxdataF_ext[0],2*3*UE->frame_parms.ofdm_symbol_size,1,1);
            LOG_M("pdcch00_ch0_ext.m","pdcch00_ch0_ext",UE->pdcch_vars[0][eNB_id]->dl_ch_estimates_ext[0],300*3,1,1);
            LOG_M("pdcch_rxF_comp0.m","pdcch0_rxF_comp0",UE->pdcch_vars[0][eNB_id]->rxdataF_comp[0],4*300,1,1);
            LOG_M("pdcch_rxF_llr.m","pdcch_llr",UE->pdcch_vars[0][eNB_id]->llr,2400,1,4);
            LOG_M("rxsig0.m","rxs0", &UE->common_vars.rxdata[0][0],10*UE->frame_parms.samples_per_tti,1,1);
            LOG_M("rxsigF0.m","rxsF0", &UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].rxdataF[0][0],UE->frame_parms.ofdm_symbol_size*nsymb,1,1);
            exit(-1);
          }

          int bit_errors=0;

          if ((test_perf ==0 ) && (n_frames==1)) {
            dlsch_unscrambling(&eNB->frame_parms,
                               0,
                               UE->dlsch[UE->current_thread_id[subframe]][0][0],
                               coded_bits_per_codeword,
                               UE->pdsch_vars[UE->current_thread_id[subframe]][0]->llr[0],
                               0,
                               subframe<<1);

            for (i=0; i<coded_bits_per_codeword; i++)
              if ((eNB->dlsch[0][0]->harq_processes[0]->eDL[i]==1 && UE->pdsch_vars[UE->current_thread_id[subframe]][0]->llr[0][i] > 0)||
                  (eNB->dlsch[0][0]->harq_processes[0]->eDL[i]==0 && UE->pdsch_vars[UE->current_thread_id[subframe]][0]->llr[0][i] < 0)) {
                uncoded_ber_bit[bit_errors++] = 1;
                printf("error in pos %d : %d => %d\n",i,
                       eNB->dlsch[0][0]->harq_processes[0]->eDL[i],
                       UE->pdsch_vars[UE->current_thread_id[subframe]][0]->llr[0][i]);
              } else {
                /*
                printf("no error in pos %d : %d => %d\n",i,
                       eNB->dlsch[0][0]->harq_processes[0]->e[i],
                       UE->pdsch_vars[UE->current_thread_id[subframe]][0]->llr[0][i]);
                */
              }

            LOG_M("dlsch_ber_bit.m","ber_bit",uncoded_ber_bit,coded_bits_per_codeword,1,0);
            allocCast3D(ch, struct complexd, eNB2UE[0]->ch, eNB2UE[0]->nb_tx, eNB2UE[0]->nb_rx, eNB2UE[0]->channel_length, false);
            LOG_M("ch0.m", "ch0", ch[0], eNB2UE[0]->channel_length, 1, 8);

            if (eNB->frame_parms.nb_antennas_tx > 1)
              LOG_M("ch1.m", "ch1", ch[1], eNB2UE[0]->channel_length, 1, 8);

            //common vars
            LOG_M("rxsig0.m","rxs0", &UE->common_vars.rxdata[0][0],10*UE->frame_parms.samples_per_tti,1,1);
            LOG_M("rxsigF0.m","rxsF0", &UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].rxdataF[0][0],UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

            if (UE->frame_parms.nb_antennas_rx>1) {
              LOG_M("rxsig1.m","rxs1", UE->common_vars.rxdata[1],UE->frame_parms.samples_per_tti,1,1);
              LOG_M("rxsigF1.m","rxsF1", UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].rxdataF[1],UE->frame_parms.ofdm_symbol_size*nsymb,1,1);
            }

            LOG_M("dlsch00_r0.m","dl00_r0",
                  &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][0][0]),
                  UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

            if (UE->frame_parms.nb_antennas_rx>1)
              LOG_M("dlsch01_r0.m","dl01_r0",
                    &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][1][0]),
                    UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

            if (eNB->frame_parms.nb_antennas_tx>1)
              LOG_M("dlsch10_r0.m","dl10_r0",
                    &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][2][0]),
                    UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

            if ((UE->frame_parms.nb_antennas_rx>1) && (eNB->frame_parms.nb_antennas_tx>1))
              LOG_M("dlsch11_r0.m","dl11_r0",
                    &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][3][0]),
                    UE->frame_parms.ofdm_symbol_size*nsymb/2,1,1);

            //pdsch_vars
            printf("coded_bits_per_codeword %u\n",coded_bits_per_codeword);
            dump_dlsch2(UE,eNB_id,subframe,&coded_bits_per_codeword,round, UE->dlsch[UE->current_thread_id[subframe]][0][0]->current_harq_pid);
            LOG_M("dlsch_e.m","e",eNB->dlsch[0][0]->harq_processes[0]->eDL,coded_bits_per_codeword,1,4);
            //pdcch_vars
            LOG_M("pdcchF0_ext.m","pdcchF_ext", UE->pdcch_vars[0][eNB_id]->rxdataF_ext[0],2*3*UE->frame_parms.ofdm_symbol_size,1,1);
            LOG_M("pdcch00_ch0_ext.m","pdcch00_ch0_ext",UE->pdcch_vars[0][eNB_id]->dl_ch_estimates_ext[0],300*3,1,1);
            LOG_M("pdcch_rxF_comp0.m","pdcch0_rxF_comp0",UE->pdcch_vars[0][eNB_id]->rxdataF_comp[0],4*300,1,1);
            LOG_M("pdcch_rxF_llr.m","pdcch_llr",UE->pdcch_vars[0][eNB_id]->llr,2400,1,4);
          }

          if (UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->harq_ack[subframe].ack == 1) {
            avg_iter += UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->last_iteration_cnt;
            iter_trials++;

            if (n_frames==1)
              printf("No DLSCH errors found (round %d),uncoded ber %f\n",round,(double)bit_errors/coded_bits_per_codeword);

            UE->total_TBS[eNB_id] =  UE->total_TBS[eNB_id] + UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->harq_processes[UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->current_harq_pid]->TBS;
            TB0_active = 0;
          } // DLSCH received ok
          else {
            errs[round]++;
            avg_iter += UE->dlsch[UE->current_thread_id[subframe]][eNB_id][0]->last_iteration_cnt-1;
            iter_trials++;

            if (n_frames==1) {
              //if ((n_frames==1) || (SNR>=30)) {
              printf("DLSCH errors found (round %d), uncoded ber %f\n",round,(double)bit_errors/coded_bits_per_codeword);

              for (s=0; s<UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->C; s++) {
                if (s<UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Cminus)
                  Kr = UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Kminus;
                else
                  Kr = UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Kplus;

                Kr_bytes = Kr>>3;
                printf("Decoded_output (Segment %d):\n",s);

                for (i=0; i<Kr_bytes; i++)
                  printf("%d : %x (%x)\n",i,UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->c[s][i],
                         UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->c[s][i]^eNB->dlsch[0][0]->harq_processes[0]->c[s][i]);
              }

              sprintf(fname,"rxsig0_r%d.m",round);
              sprintf(vname,"rxs0_r%d",round);
              LOG_M(fname,vname, &UE->common_vars.rxdata[0][0],10*UE->frame_parms.samples_per_tti,1,1);
              sprintf(fname,"rxsigF0_r%d.m",round);
              sprintf(vname,"rxs0F_r%d",round);
              LOG_M(fname,vname, &UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].rxdataF[0][0],UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

              if (UE->frame_parms.nb_antennas_rx>1) {
                sprintf(fname,"rxsig1_r%d.m",round);
                sprintf(vname,"rxs1_r%d.m",round);
                LOG_M(fname,vname, UE->common_vars.rxdata[1],UE->frame_parms.samples_per_tti,1,1);
                sprintf(fname,"rxsigF1_r%d.m",round);
                sprintf(vname,"rxs1F_r%d.m",round);
                LOG_M(fname,vname, UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].rxdataF[1],UE->frame_parms.ofdm_symbol_size*nsymb,1,1);
              }

              sprintf(fname,"dlsch00_r%d.m",round);
              sprintf(vname,"dl00_r%d",round);
              LOG_M(fname,vname,
                    &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][0][0]),
                    UE->frame_parms.ofdm_symbol_size*nsymb,1,1);

              if (UE->frame_parms.nb_antennas_rx>1) {
                sprintf(fname,"dlsch01_r%d.m",round);
                sprintf(vname,"dl01_r%d",round);
                LOG_M(fname,vname,
                      &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][1][0]),
                      UE->frame_parms.ofdm_symbol_size*nsymb/2,1,1);
              }

              if (eNB->frame_parms.nb_antennas_tx>1) {
                sprintf(fname,"dlsch10_r%d.m",round);
                sprintf(vname,"dl10_r%d",round);
                LOG_M(fname,vname,
                      &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][2][0]),
                      UE->frame_parms.ofdm_symbol_size*nsymb/2,1,1);
              }

              if ((UE->frame_parms.nb_antennas_rx>1) && (eNB->frame_parms.nb_antennas_tx>1)) {
                sprintf(fname,"dlsch11_r%d.m",round);
                sprintf(vname,"dl11_r%d",round);
                LOG_M(fname,vname,
                      &(UE->common_vars.common_vars_rx_data_per_thread[UE->current_thread_id[subframe]].dl_ch_estimates[eNB_id][3][0]),
                      UE->frame_parms.ofdm_symbol_size*nsymb/2,1,1);
              }

              //pdsch_vars
              dump_dlsch2(UE,eNB_id,subframe,&coded_bits_per_codeword,round, UE->dlsch[UE->current_thread_id[subframe]][0][0]->current_harq_pid);
              //LOG_M("dlsch_e.m","e",eNB->dlsch[0][0]->harq_processes[0]->e,coded_bits_per_codeword,1,4);
              //LOG_M("dlsch_ber_bit.m","ber_bit",uncoded_ber_bit,coded_bits_per_codeword,1,0);
              //LOG_M("dlsch_w.m","w",eNB->dlsch[0][0]->harq_processes[0]->w[0],3*(tbs+64),1,4);
              //LOG_M("dlsch_w.m","w",UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->w[0],3*(tbs+64),1,0);
              //pdcch_vars
              LOG_M("pdcchF0_ext.m","pdcchF_ext", UE->pdcch_vars[0][eNB_id]->rxdataF_ext[0],2*3*UE->frame_parms.ofdm_symbol_size,1,1);
              LOG_M("pdcch00_ch0_ext.m","pdcch00_ch0_ext",UE->pdcch_vars[0][eNB_id]->dl_ch_estimates_ext[0],300*3,1,1);
              LOG_M("pdcch_rxF_comp0.m","pdcch0_rxF_comp0",UE->pdcch_vars[0][eNB_id]->rxdataF_comp[0],4*300,1,1);
              LOG_M("pdcch_rxF_llr.m","pdcch_llr",UE->pdcch_vars[0][eNB_id]->llr,2400,1,4);

              if (round == 3) exit(-1);
            }

            //      printf("round %d errors %d/%d\n",round,errs[round],trials);
            round++;
            //      UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->round++;
          }


          UE->proc.proc_rxtx[UE->current_thread_id[subframe]].frame_rx++;
        }  //round

        //      printf("\n");

        if ((errs[0]>=n_frames/10) && (trials>(n_frames/2)))
          break;

        //len = chbch_stats_read(stats_buffer,NULL,0,4096);
        //printf("%s\n\n",stats_buffer);

        if (UE->proc.proc_rxtx[UE->current_thread_id[subframe]].frame_rx % 10 == 0) {
          UE->bitrate[eNB_id] = (UE->total_TBS[eNB_id] - UE->total_TBS_last[eNB_id])*10;
          LOG_D(PHY,"[UE %d] Calculating bitrate: total_TBS = %d, total_TBS_last = %d, bitrate = %d kbits/s\n",UE->Mod_id,UE->total_TBS[eNB_id],UE->total_TBS_last[eNB_id],
                UE->bitrate[eNB_id]/1000);
          UE->total_TBS_last[eNB_id] = UE->total_TBS[eNB_id];
        }

        /* calculate the total processing time for each packet,
         * get the max, min, and number of packets that exceed t>2000us
         */
        double t_tx = inMicroS(eNB->phy_proc_tx.p_time);
        double t_rx = inMicroS(UE->phy_proc_rx[UE->current_thread_id[subframe]].p_time);
        sumUpStats(&phy_proc_rx_tot, UE->phy_proc_rx, UE->current_thread_id[subframe]);
        sumUpStats(&ue_front_end_tot, UE->ue_front_end_stat, UE->current_thread_id[subframe]);
        sumUpStats(&pdsch_procedures_tot, UE->pdsch_procedures_stat, UE->current_thread_id[subframe]);
        sumUpStats(&dlsch_procedures_tot, UE->dlsch_procedures_stat, UE->current_thread_id[subframe]);
        sumUpStats(&dlsch_decoding_tot, UE->dlsch_decoding_stats, UE->current_thread_id[subframe]);
        sumUpStatsSlot(&dlsch_llr_tot, UE->dlsch_llr_stats_parallelization, UE->current_thread_id[subframe]);

        if (t_tx > 2000 )// 2ms is too much time for a subframe
          n_tx_dropped++;

        if (t_rx > 2000 )
          n_rx_dropped++;
      }   //trials

      // round_trials[0]: number of code word : goodput the protocol
      // sort table
      if (dump_table == 1 ) {
        set_component_filelog(SIM);  // file located in /tmp/usim.txt
      }

      effective_rate = 1.0-((double)(errs[0]+errs[1]+errs[2]+errs[3])/((double)round_trials[0] + round_trials[1] + round_trials[2] + round_trials[3]));
      printf("\n**********************SNR = %f dB (tx_lev %f)**************************\n",
             SNR,
             (double)tx_lev_dB+10*log10(UE->frame_parms.ofdm_symbol_size/(NB_RB*12)));
      printf("Errors (%u(%u)/%u %u/%u %u/%u %u/%u), Pe = (%e,%e,%e,%e), dci_errors %u/%u, Pe = %e => effective rate %f, normalized delay %f (%f)\n",
             errs[0],
             errs2[0],
             round_trials[0],
             errs[1],
             round_trials[1],
             errs[2],
             round_trials[2],
             errs[3],
             round_trials[3],
             (double)errs[0]/(round_trials[0]),
             (double)errs[1]/(round_trials[1]),
             (double)errs[2]/(round_trials[2]),
             (double)errs[3]/(round_trials[3]),
             dci_errors[0]+dci_errors[1]+dci_errors[2]+dci_errors[3],
             round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3],
             (double)(dci_errors[0]+dci_errors[1]+dci_errors[2]+dci_errors[3])/(round_trials[0]+round_trials[1]+round_trials[2]+round_trials[3]),
             //rate*effective_rate,
             100*effective_rate,
             //rate,
             //rate*get_Qm(UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[UE->dlsch[UE->current_thread_id[subframe]][0][0]->current_harq_pid]->mcs),
             (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0])/
             (double)eNB->dlsch[0][0]->harq_processes[0]->TBS,
             (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0]));
      double timeBase = 1e-3;

      if (print_perf==1) {
        printf("\neNB TX function statistics (per 1ms subframe)\n");
        printDistribution(&eNB->phy_proc_tx, "PHY proc tx");
        printStatIndent(&eNB->dlsch_common_and_dci,"DL common channels and dci time");
        printStatIndent(&eNB->dlsch_ue_specific,"DL per ue part time");
        printStatIndent2(&eNB->dlsch_encoding_stats,"DLSCH encoding time");
        printStatIndent3(&eNB->dlsch_rate_matching_stats,"DLSCH rate matching time");
        printStatIndent3(&eNB->dlsch_turbo_encoding_stats,"DLSCH turbo encoding time");
        printStatIndent3(&eNB->dlsch_interleaving_stats,"DLSCH interleaving time");
        printStatIndent2(&eNB->dlsch_scrambling_stats,  "DLSCH scrambling time");
        printStatIndent2(&eNB->dlsch_modulation_stats, "DLSCH modulation time");
        printDistribution(&eNB->ofdm_mod_stats, "OFDM_mod (idft) time");
        printf("\nUE RX function statistics (per 1ms subframe)\n");
        printDistribution(&phy_proc_rx_tot, "Total PHY proc rx");
        printStatIndent(&ue_front_end_tot,"Front end processing");
        printStatIndent(&dlsch_llr_tot,"rx_pdsch processing");
        printStatIndent2(&pdsch_procedures_tot,"pdsch processing");
        printStatIndent2(&dlsch_procedures_tot,"dlsch processing");
        printStatIndent2(&UE->crnti_procedures_stats,"C-RNTI processing");
        printStatIndent(&UE->ofdm_demod_stats,"ofdm demodulation");
        printStatIndent(&UE->dlsch_channel_estimation_stats,"DLSCH channel estimation time");
        printStatIndent(&UE->dlsch_freq_offset_estimation_stats,"DLSCH frequency offset estimation time");
        printStatIndent(&dlsch_decoding_tot, "DLSCH Decoding time ");
        printStatIndent(&UE->dlsch_unscrambling_stats,"DLSCH unscrambling time");
        printStatIndent(&UE->dlsch_rate_unmatching_stats,"DLSCH Rate Unmatching");
        printf("|__ DLSCH Turbo Decoding(%d bits), avg iterations: %.1f       %.2f us (%d cycles, %d trials)\n",
               UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Cminus ?
               UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Kminus :
               UE->dlsch[UE->current_thread_id[subframe]][0][0]->harq_processes[0]->Kplus,
               UE->dlsch_tc_intl1_stats.trials/(double)UE->dlsch_tc_init_stats.trials,
               (double)UE->dlsch_turbo_decoding_stats.diff/UE->dlsch_turbo_decoding_stats.trials*timeBase,
               (int)((double)UE->dlsch_turbo_decoding_stats.diff/UE->dlsch_turbo_decoding_stats.trials),
               UE->dlsch_turbo_decoding_stats.trials);
        printStatIndent2(&UE->dlsch_tc_init_stats,"init");
        printStatIndent2(&UE->dlsch_tc_alpha_stats,"alpha");
        printStatIndent2(&UE->dlsch_tc_beta_stats,"beta");
        printStatIndent2(&UE->dlsch_tc_gamma_stats,"gamma");
        printStatIndent2(&UE->dlsch_tc_ext_stats,"ext");
        printStatIndent2(&UE->dlsch_tc_intl1_stats,"turbo internal interleaver");
        printStatIndent2(&UE->dlsch_tc_intl2_stats,"intl2+HardDecode+CRC");
      }

      if ((transmission_mode != 3) && (transmission_mode != 4)) {
        fprintf(bler_fd,"%f;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u;%u\n",
                SNR,
                mcs1,
                eNB->dlsch[0][0]->harq_processes[0]->TBS,
                rate,
                errs[0],
                round_trials[0],
                errs[1],
                round_trials[1],
                errs[2],
                round_trials[2],
                errs[3],
                round_trials[3],
                dci_errors[0]);
      } else {
        fprintf(bler_fd,"%f;%d;%d;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u;%u\n",
                SNR,
                mcs1,mcs2,
                eNB->dlsch[0][0]->harq_processes[0]->TBS,
                eNB->dlsch[0][1]->harq_processes[0]->TBS,
                rate,
                errs[0],
                round_trials[0],
                errs[1],
                round_trials[1],
                errs[2],
                round_trials[2],
                errs[3],
                round_trials[3],
                dci_errors[0]);
      }

      if(abstx) { //ABSTRACTION
        blerr[0] = (double)errs[0]/(round_trials[0]);

        if(num_rounds>1) {
          blerr[1] = (double)errs[1]/(round_trials[1]);
          blerr[2] = (double)errs[2]/(round_trials[2]);
          blerr[3] = (double)errs[3]/(round_trials[3]);
          fprintf(csv_fd,"%e,%e,%e,%e;\n",blerr[0],blerr[1],blerr[2],blerr[3]);
        } else {
          fprintf(csv_fd,"%e;\n",blerr[0]);
        }
      } //ABStraction

      if ( (test_perf != 0) && (100 * effective_rate > test_perf )) {
        //fprintf(time_meas_fd,"SNR; MCS; TBS; rate; err0; trials0; err1; trials1; err2; trials2; err3; trials3; dci_err\n");
        if ((transmission_mode != 3) && (transmission_mode != 4)) {
          fprintf(time_meas_fd,"%f;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u;%u;",
                  SNR,
                  mcs1,
                  eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  rate,
                  errs[0],
                  round_trials[0],
                  errs[1],
                  round_trials[1],
                  errs[2],
                  round_trials[2],
                  errs[3],
                  round_trials[3],
                  dci_errors[0]);
          //fprintf(time_meas_fd,"SNR; MCS; TBS; rate; DL_DECOD_ITER; err0; trials0; err1; trials1; err2; trials2; err3; trials3; PE; dci_err;PE;ND;\n");
          fprintf(time_meas_fd,"%f;%d;%d;%f; %2.1f%%;%f;%f;%u;%u;%u;%u;%u;%u;%u;%u;%e;%e;%e;%e;%u;%u;%e;%f;%f;",
                  SNR,
                  mcs1,
                  eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  rate*effective_rate,
                  100*effective_rate,
                  rate,
                  (double)avg_iter/iter_trials,
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
                  dci_errors[0],
                  round_trials[0],
                  (double)dci_errors[0]/(round_trials[0]),
                  (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0])/
                  (double)eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0]));
        } else {
          fprintf(time_meas_fd,"%f;%d;%d;%d;%d;%f;%u;%u;%u;%u;%u;%u;%u;%u;%u;",
                  SNR,
                  mcs1,mcs2,
                  eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  eNB->dlsch[0][1]->harq_processes[0]->TBS,
                  rate,
                  errs[0],
                  round_trials[0],
                  errs[1],
                  round_trials[1],
                  errs[2],
                  round_trials[2],
                  errs[3],
                  round_trials[3],
                  dci_errors[0]);
          //fprintf(time_meas_fd,"SNR; MCS; TBS; rate; DL_DECOD_ITER; err0; trials0; err1; trials1; err2; trials2; err3; trials3; PE; dci_err;PE;ND;\n");
          fprintf(time_meas_fd,"%f;%d;%d;%d;%d;%f;%2.1f;%f;%f;%u;%u;%u;%u;%u;%u;%u;%u;%e;%e;%e;%e;%u;%u;%e;%f;%f;",
                  SNR,
                  mcs1,mcs2,
                  eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  eNB->dlsch[0][1]->harq_processes[0]->TBS,
                  rate*effective_rate,
                  100*effective_rate,
                  rate,
                  (double)avg_iter/iter_trials,
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
                  dci_errors[0],
                  round_trials[0],
                  (double)dci_errors[0]/(round_trials[0]),
                  (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0])/
                  (double)eNB->dlsch[0][0]->harq_processes[0]->TBS,
                  (1.0*(round_trials[0]-errs[0])+2.0*(round_trials[1]-errs[1])+3.0*(round_trials[2]-errs[2])+4.0*(round_trials[3]-errs[3]))/((double)round_trials[0]));
        }

        //fprintf(time_meas_fd,"eNB_PROC_TX(%d); OFDM_MOD(%d); DL_MOD(%d); DL_SCR(%d); DL_ENC(%d); UE_PROC_RX(%d); OFDM_DEMOD_CH_EST(%d); RX_PDCCH(%d); CH_COMP_LLR(%d); DL_USCR(%d); DL_DECOD(%d);\n",
        fprintf(time_meas_fd,"%d; %d; %d; %d; %d; %d; %d; %d; %d; %d; %d;",
                eNB->phy_proc_tx.trials,
                eNB->ofdm_mod_stats.trials,
                eNB->dlsch_modulation_stats.trials,
                eNB->dlsch_scrambling_stats.trials,
                eNB->dlsch_encoding_stats.trials,
                phy_proc_rx_tot.trials,
                UE->ofdm_demod_stats.trials,
                UE->dlsch_rx_pdcch_stats.trials,
                UE->dlsch_llr_stats.trials,
                UE->dlsch_unscrambling_stats.trials,
                UE->dlsch_decoding_stats[UE->current_thread_id[subframe]].trials
               );
        fprintf(time_meas_fd,"%f;%f;%f;%f;%f;%f;%f;%f;%f;%f;%f;",
                get_time_meas_us(&eNB->phy_proc_tx),
                get_time_meas_us(&eNB->ofdm_mod_stats),
                get_time_meas_us(&eNB->dlsch_modulation_stats),
                get_time_meas_us(&eNB->dlsch_scrambling_stats),
                get_time_meas_us(&eNB->dlsch_encoding_stats),
                get_time_meas_us(&phy_proc_rx_tot),
                nsymb*get_time_meas_us(&UE->ofdm_demod_stats),
                get_time_meas_us(&UE->dlsch_rx_pdcch_stats),
                3*get_time_meas_us(&UE->dlsch_llr_stats),
                get_time_meas_us(&UE->dlsch_unscrambling_stats),
                get_time_meas_us(&UE->dlsch_decoding_stats[UE->current_thread_id[subframe]])
               );
        fprintf(time_meas_fd,"TIMER;STD_DEV;MAX;Q1;MED;Q3;MIN;DROPPED;\n");
        printDistributionDroppedCsv(time_meas_fd, &eNB->phy_proc_tx, n_tx_dropped, "eNB TX TOTAL");
        printDistributionCsv(time_meas_fd, &eNB->ofdm_mod_stats, "eNB TX IFFT");
        printDistributionCsv(time_meas_fd, &eNB->dlsch_modulation_stats, "eNB TX MOD");
        printDistributionCsv(time_meas_fd, &eNB->dlsch_encoding_stats, "eNB TX ENC");
        printDistributionDroppedCsv(time_meas_fd, &phy_proc_rx_tot, n_rx_dropped, "UE RX TOTAL");
        printDistributionCsv(time_meas_fd, &UE->ofdm_demod_stats, "UE RX FFT");
        printDistributionCsv(time_meas_fd, &UE->dlsch_rx_pdcch_stats, "UE RX DEMOD");
        printDistributionCsv(time_meas_fd, &UE->dlsch_decoding_stats[UE->current_thread_id[subframe]], "UE RX DEC");
        printf("[passed] effective rate : %f  (%2.1f%%,%f)): log and break \n",rate*effective_rate, 100*effective_rate, rate );
        test_passed = 1;
        break;
      } else if (test_perf !=0 ) {
        printf("[continue] effective rate : %f  (%2.1f%%,%f)): increase snr \n",rate*effective_rate, 100*effective_rate, rate);
        test_passed = 0;
      }

      if (((double)errs[0]/(round_trials[0]))<(10.0/n_frames))
        break;
    }// SNR
  } //ch_realization

  // free sorted lists for timers for which we wanted the distribution
  free_sorted_list_meas(&eNB->phy_proc_tx);
  free_sorted_list_meas(&eNB->ofdm_mod_stats);
  free_sorted_list_meas(&eNB->dlsch_modulation_stats);
  free_sorted_list_meas(&eNB->dlsch_encoding_stats);
  free_sorted_list_meas(&UE->phy_proc_rx[0]);
  free_sorted_list_meas(&UE->phy_proc_rx[1]);
  free_sorted_list_meas(&phy_proc_rx_tot);
  free_sorted_list_meas(&UE->ofdm_demod_stats);
  free_sorted_list_meas(&UE->dlsch_rx_pdcch_stats);
  free_sorted_list_meas(&UE->dlsch_decoding_stats[0]);
  free_sorted_list_meas(&UE->dlsch_decoding_stats[1]);

  fclose(bler_fd);

  if (test_perf !=0)
    fclose (time_meas_fd);

  //fprintf(tikz_fd,"};\n");
  //fclose(tikz_fd);

  if (input_trch_file==1)
    fclose(input_trch_fd);

  if (input_file==1)
    fclose(input_fd);

  if(abstx) { // ABSTRACTION
    fprintf(csv_fd,"];");
    fclose(csv_fd);
  }

  if (uncoded_ber_bit)
    free(uncoded_ber_bit);

  uncoded_ber_bit = NULL;
  printf("Freeing dlsch structures\n");

  for (i=0; i<2; i++) {
    printf("eNB %d\n",i);
    free_eNB_dlsch(eNB->dlsch[0][i]);
    printf("UE %d\n",i);
    free_ue_dlsch(UE->dlsch[UE->current_thread_id[subframe]][0][i]);
  }

  if (test_perf && !test_passed)
    return(-1);
  else
    return(0);
}
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}
