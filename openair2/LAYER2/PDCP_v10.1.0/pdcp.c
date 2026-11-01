/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief pdcp interface with RLC
 */

#define PDCP_C

#define MBMS_MULTICAST_OUT

#include "assertions.h"
#include "ds/hashtable.h"
#include "pdcp.h"
#include "pdcp_util.h"
#include "pdcp_sequence_manager.h"
#include "LAYER2/RLC/rlc.h"
#include "LAYER2/MAC/mac_extern.h"
#include "RRC/LTE/rrc_proto.h"
#include "pdcp_primitives.h"
#include "common/utils/LOG/log.h"
#include <inttypes.h>
#include "common/platform_constants.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "common/ngran_types.h"
#include "common/openairinterface5g_limits.h"
#include "executables/lte-softmodem.h"
#include "common/utils/tuntap_if.h"
#include "intertask_interface.h"
#include "openair3/S1AP/s1ap_eNB.h"
#include <pthread.h>
#include "pdcp.h"

#include "openair3/ocp-gtpu/gtp_itf.h"
#include <openair3/ocp-gtpu/gtp_itf.h>

#include "ENB_APP/enb_config.h"



extern int otg_enabled;
extern uint8_t nfapi_mode;
#include "common/ran_context.h"
extern RAN_CONTEXT_t RC;
hash_table_t  *pdcp_coll_p = NULL;

#ifdef MBMS_MULTICAST_OUT
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/ip.h>
  #include <netinet/udp.h>
  #include <unistd.h>

  static int mbms_socket = -1;
#endif

uint32_t Pdcp_stats_tx_window_ms[MAX_eNB][MAX_MOBILES_PER_ENB];
uint32_t Pdcp_stats_tx_bytes[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_bytes_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_bytes_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_sn[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_throughput_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_aiat[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_aiat_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_aiat_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_tx_iat[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];

uint32_t Pdcp_stats_rx_window_ms[MAX_eNB][MAX_MOBILES_PER_ENB];
uint32_t Pdcp_stats_rx[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_bytes[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_bytes_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_bytes_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_sn[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_goodput_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_aiat[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_aiat_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_aiat_tmp_w[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_iat[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
uint32_t Pdcp_stats_rx_outoforder[MAX_eNB][MAX_MOBILES_PER_ENB][NB_RB_MAX];
int pdcp_pc5_sockfd;
struct sockaddr_in prose_ctrl_addr;
struct sockaddr_in prose_pdcp_addr;
struct sockaddr_in pdcp_sin;
/* pdcp module parameters and related functions*/
static pdcp_params_t pdcp_params = {0};
rnti_t                 pdcp_UE_UE_module_id_to_rnti[MAX_MOBILES_PER_ENB];
rnti_t                 pdcp_eNB_UE_instance_to_rnti[MAX_MOBILES_PER_ENB]; // for noS1 mode
unsigned int           pdcp_eNB_UE_instance_to_rnti_index;

signed int             pdcp_2_nas_irq;
pdcp_stats_t              UE_pdcp_stats[MAX_MOBILES_PER_ENB];
pdcp_stats_t              eNB_pdcp_stats[NUMBER_OF_eNB_MAX];

static pdcp_mbms_t               pdcp_mbms_array_ue[MAX_MOBILES_PER_ENB][LTE_maxServiceCount][LTE_maxSessionPerPMCH];   // some constants from openair2/RRC/LTE/MESSAGES/asn1_constants.h
static pdcp_mbms_t               pdcp_mbms_array_eNB[NUMBER_OF_eNB_MAX][LTE_maxServiceCount][LTE_maxSessionPerPMCH]; // some constants from openair2/RRC/LTE/MESSAGES/asn1_constants.h
static sdu_size_t             pdcp_input_sdu_remaining_size_to_read;
static sdu_size_t             pdcp_output_header_bytes_to_write;
static sdu_size_t             pdcp_output_sdu_bytes_to_write;
notifiedFIFO_t         pdcp_sdu_list;

void pdcp_free(void *pdcp_pP);

pdcp_enb_t pdcp_enb[MAX_NUM_CCs];
pthread_t pdcp_stats_thread_desc;
/*! \fn bool pdcp_config_req_asn1 (const protocol_ctxt_t* const ctxt_pP, srb_flag_t srb_flagP, uint32_t  action, rb_id_t rb_id,
 * uint8_t rb_sn, uint8_t rb_report, uint16_t header_compression_profile, uint8_t security_mode) \brief  Function for RRC to
 * configure a Radio Bearer. \param[in]  ctxt_pP           Running context. \param[in]  pdcp_pP            Pointer on PDCP
 * structure. \param[in]  enb_mod_idP        Virtualized enb module identifier, Not used if eNB_flagP = 0. \param[in]  ue_mod_idP
 * Virtualized ue module identifier. \param[in]  frame              Frame index. \param[in]  eNB_flag           Flag to indicate eNB
 * (1) or UE (0) \param[in]  srb_flagP          Flag to indicate SRB (1) or DRB (0) \param[in]  action             add, remove,
 * modify a RB \param[in]  rb_id              radio bearer id \param[in]  rb_sn              sequence number for this radio bearer
 * \param[in]  drb_report         set a pdcp report for this drb
 * \param[in]  header_compression set the rohc profile
 * \param[in]  security_mode      set the integrity and ciphering algs
 * \param[in]  kRRCenc            RRC encryption key
 * \param[in]  kRRCint            RRC integrity key
 * \param[in]  kUPenc             User-Plane encryption key
 * \return     A status about the processing, OK or error code.
 */
static bool pdcp_config_req_asn1(const protocol_ctxt_t *const ctxt_pP,
                                 pdcp_t *const pdcp_pP,
                                 const srb_flag_t srb_flagP,
                                 const rlc_mode_t rlc_modeP,
                                 const config_action_t actionP,
                                 const uint16_t lc_idP,
                                 const uint16_t mch_idP,
                                 const rb_id_t rb_idP,
                                 const uint8_t rb_snP,
                                 const uint8_t rb_reportP,
                                 const uint16_t header_compression_profileP,
                                 const uint8_t security_modeP,
                                 uint8_t *const kRRCenc_pP,
                                 uint8_t *const kRRCint_pP,
                                 uint8_t *const kUPenc_pP);

void *pdcp_stats_thread(void *param)
{
   UNUSED(param);
   FILE *fd;
   int old_byte_cnt[MAX_MOBILES_PER_ENB][NB_RB_MAX],old_byte_cnt_rx[MAX_MOBILES_PER_ENB][NB_RB_MAX];
   for (int i=0;i<MAX_MOBILES_PER_ENB;i++)
     for (int j=0;j<NB_RB_MAX;j++) {
	old_byte_cnt[i][j]=0;
	old_byte_cnt_rx[i][j]=0;
     }
   while (!oai_exit) {
     sleep(1);

     fd=fopen("PDCP_stats.log","w+");
     AssertFatal(fd!=NULL,"Cannot open MAC_stats.log\n");
     int drb_id=3;
     for (int UE_id = 0; UE_id < MAX_MOBILES_PER_ENB; UE_id++) {
        if (pdcp_enb[0].rnti[UE_id]>0) {
           fprintf(fd,"PDCP: CRNTI %x, DRB %d: tx_bytes %d, DL Throughput %e\n",
                   pdcp_enb[0].rnti[UE_id],drb_id,
                   Pdcp_stats_tx_bytes[0][UE_id][drb_id],
                   (double)((Pdcp_stats_tx_bytes[0][UE_id][drb_id]-old_byte_cnt[UE_id][drb_id])<<3));
           fprintf(fd,"                              rx_bytes %d, UL Throughput %e\n",             
                   Pdcp_stats_rx_bytes[0][UE_id][drb_id],
                   (double)((Pdcp_stats_rx_bytes[0][UE_id][drb_id]-old_byte_cnt_rx[UE_id][drb_id])<<3));
           old_byte_cnt[UE_id][drb_id]=Pdcp_stats_tx_bytes[0][UE_id][drb_id];
           old_byte_cnt_rx[UE_id][drb_id]=Pdcp_stats_rx_bytes[0][UE_id][drb_id];
        }
     }
     fclose(fd);
   }
   return(NULL);
}

uint64_t get_pdcp_optmask(void) {
  return pdcp_params.optmask;
}

//-----------------------------------------------------------------------------
/*
 * If PDCP_UNIT_TEST is set here then data flow between PDCP and RLC is broken
 * and PDCP has no longer anything to do with RLC. In this case, after it's handed
 * an SDU it appends PDCP header and returns (by filling in incoming pointer parameters)
 * this uint8_t to be dissected for testing purposes. For further details see test
 * code at targets/TEST/PDCP/test_pdcp.c:test_pdcp_data_req()
 */
bool pdcp_data_req(protocol_ctxt_t  *ctxt_pP,
                   const srb_flag_t     srb_flagP,
                   const rb_id_t        rb_idP,
                   const mui_t          muiP,
                   const confirm_t      confirmP,
                   const sdu_size_t     sdu_buffer_sizeP,
                   unsigned char *const sdu_buffer_pP,
                   const pdcp_transmission_mode_t modeP,
                   const uint32_t *const sourceL2Id,
                   const uint32_t *const destinationL2Id)
//-----------------------------------------------------------------------------
{
  pdcp_t            *pdcp_p          = NULL;
  uint8_t            i               = 0;
  uint8_t            pdcp_header_len = 0;
  uint8_t            pdcp_tailer_len = 0;
  uint16_t           pdcp_pdu_size   = 0;
  uint16_t           current_sn      = 0;
  uint8_t *pdcp_pdu_p = NULL;
  rlc_op_status_t    rlc_status;
  bool               ret             = true;
  hash_key_t         key             = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t     h_rc;
  uint8_t            rb_offset= (srb_flagP == 0) ? DTCH -1 : 0;
  uint16_t           pdcp_uid=0;
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ, VCD_FUNCTION_IN);
#if T_TRACER

  if (ctxt_pP->enb_flag != ENB_FLAG_NO)
    T(T_ENB_PDCP_DL, T_INT(ctxt_pP->module_id), T_INT(ctxt_pP->rntiMaybeUEid), T_INT(rb_idP), T_INT(sdu_buffer_sizeP));

#endif

  if (sdu_buffer_sizeP == 0) {
    LOG_W(PDCP, "Handed SDU is of size 0! Ignoring...\n");
    return false;
  }

  /*
   * XXX MAX_IP_PACKET_SIZE is 4096, shouldn't this be MAX SDU size, which is 8188 bytes?
   */
  AssertFatal(sdu_buffer_sizeP<= MAX_IP_PACKET_SIZE,"Requested SDU size (%d) is bigger than that can be handled by PDCP (%u)!\n",
              sdu_buffer_sizeP, MAX_IP_PACKET_SIZE);

  if (modeP == PDCP_TRANSMISSION_MODE_TRANSPARENT) {
    AssertError(rb_idP < NB_RB_MBMS_MAX, return false, "RB id is too high (%ld/%d) %u %lu!\n", rb_idP, NB_RB_MBMS_MAX, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
  } else {
    if (srb_flagP) {
      AssertError(rb_idP < 3, return false, "RB id is too high (%ld/%d) %u %lu!\n", rb_idP, 3, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
    } else {
      AssertError(rb_idP < LTE_maxDRB, return false, "RB id is too high (%ld/%d) %u %lu!\n", rb_idP, LTE_maxDRB, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
    }
  }

  key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, rb_idP, srb_flagP);
  h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

  if (h_rc != HASH_TABLE_OK) {
    if (modeP != PDCP_TRANSMISSION_MODE_TRANSPARENT) {
      LOG_W(PDCP, PROTOCOL_CTXT_FMT" Instance is not configured for rb_id %ld Ignoring SDU...\n",
            PROTOCOL_CTXT_ARGS(ctxt_pP),
            rb_idP);
      return false;
    }
  }

  if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
    start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
  } else {
    start_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
  }

  // PDCP transparent mode for MBMS traffic

  if (modeP == PDCP_TRANSMISSION_MODE_TRANSPARENT) {
    LOG_D(PDCP, " [TM] Asking for a new mem_block of size %d\n",sdu_buffer_sizeP);
    pdcp_pdu_p = malloc16(sdu_buffer_sizeP);

    if (pdcp_pdu_p != NULL) {
      memcpy(pdcp_pdu_p, sdu_buffer_pP, sdu_buffer_sizeP);

      if( LOG_DEBUGFLAG(DEBUG_PDCP) ) {
           rlc_util_print_hex_octets(PDCP, pdcp_pdu_p, sdu_buffer_sizeP);
           LOG_UI(PDCP, "Before rlc_data_req 1, srb_flagP: %d, rb_idP: %ld \n", srb_flagP, rb_idP);
      }

      rlc_status =
          rlc_data_req(ctxt_pP, srb_flagP, MBMS_FLAG_YES, rb_idP, muiP, confirmP, sdu_buffer_sizeP, pdcp_pdu_p, NULL, NULL);
    } else {
      rlc_status = RLC_OP_STATUS_OUT_OF_RESSOURCES;
      LOG_E(PDCP,PROTOCOL_CTXT_FMT" PDCP_DATA_REQ SDU DROPPED, OUT OF MEMORY \n",
            PROTOCOL_CTXT_ARGS(ctxt_pP));
    }
  } else {
    // calculate the pdcp header and trailer size
    if (srb_flagP) {
      pdcp_header_len = PDCP_CONTROL_PLANE_DATA_PDU_SN_SIZE;
      pdcp_tailer_len = PDCP_CONTROL_PLANE_DATA_PDU_MAC_I_SIZE;
    } else {
      pdcp_header_len = PDCP_USER_PLANE_DATA_PDU_LONG_SN_HEADER_SIZE;
      pdcp_tailer_len = 0;
    }

    pdcp_pdu_size = sdu_buffer_sizeP + pdcp_header_len + pdcp_tailer_len;
    LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT"Data request notification  pdu size %d (header%d, trailer%d)\n",
          PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
          pdcp_pdu_size,
          pdcp_header_len,
          pdcp_tailer_len);
    /*
     * Allocate a new block for the new PDU (i.e. PDU header and SDU payload)
     */
    pdcp_pdu_p = malloc16(pdcp_pdu_size);

    if (pdcp_pdu_p != NULL) {
      /*
       * Create a Data PDU with header and append data
       *
       * Place User Plane PDCP Data PDU header first
       */
      if (srb_flagP) { // this Control plane PDCP Data PDU
        pdcp_control_plane_data_pdu_header pdu_header;
        pdu_header.sn = pdcp_get_next_tx_seq_number(pdcp_p);
        current_sn = pdu_header.sn;
        memset(&pdu_header.mac_i[0],0,PDCP_CONTROL_PLANE_DATA_PDU_MAC_I_SIZE);
        memset(&pdcp_pdu_p[sdu_buffer_sizeP + pdcp_header_len], 0, PDCP_CONTROL_PLANE_DATA_PDU_MAC_I_SIZE);

        if (pdcp_serialize_control_plane_data_pdu_with_SRB_sn_buffer(pdcp_pdu_p, &pdu_header) == false) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" Cannot fill PDU buffer with relevant header fields!\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p));

          if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
            stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
          } else {
            stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
          }

          VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ,VCD_FUNCTION_OUT);
          return false;
        }
      } else {
        pdcp_user_plane_data_pdu_header_with_long_sn pdu_header;
        pdu_header.dc = (modeP == PDCP_TRANSMISSION_MODE_DATA) ? PDCP_DATA_PDU_BIT_SET :  PDCP_CONTROL_PDU_BIT_SET;
        pdu_header.sn = pdcp_get_next_tx_seq_number(pdcp_p);
        current_sn = pdu_header.sn ;

        if (pdcp_serialize_user_plane_data_pdu_with_long_sn_buffer(pdcp_pdu_p, &pdu_header) == false) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" Cannot fill PDU buffer with relevant header fields!\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p));

          if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
            stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
          } else {
            stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
          }

          VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ,VCD_FUNCTION_OUT);
          return false;
        }
      }

      /*
       * Validate incoming sequence number, there might be a problem with PDCP initialization
       */
      if (current_sn > pdcp_calculate_max_seq_num_for_given_size(pdcp_p->seq_num_size)) {
        LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" Generated sequence number (%"PRIu16") is greater than a sequence number could ever be!\n"\
              "There must be a problem with PDCP initialization, ignoring this PDU...\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
              current_sn);
        free(pdcp_pdu_p);

        if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
          stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
        } else {
          stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
        }

        VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ,VCD_FUNCTION_OUT);
        return false;
      }

      LOG_D(PDCP, "Sequence number %d is assigned to current PDU\n", current_sn);
      /* Then append data... */
      memcpy(&pdcp_pdu_p[pdcp_header_len], sdu_buffer_pP, sdu_buffer_sizeP);

      //For control plane data that are not integrity protected,
      // the MAC-I field is still present and should be padded with padding bits set to 0.
      // NOTE: user-plane data are never integrity protected
      for (i=0; i<pdcp_tailer_len; i++) {
        pdcp_pdu_p[pdcp_header_len + sdu_buffer_sizeP + i] = 0x00; // pdu_header.mac_i[i];
      }

      if ((pdcp_p->security_activated != 0) &&
          (((pdcp_p->cipheringAlgorithm) != 0) ||
           ((pdcp_p->integrityProtAlgorithm) != 0))) {
        if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
          start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].apply_security);
        } else {
          start_meas(&UE_pdcp_stats[ctxt_pP->module_id].apply_security);
        }

        pdcp_apply_security(ctxt_pP,
                            pdcp_p,
                            srb_flagP,
                            rb_idP % LTE_maxDRB,
                            pdcp_header_len,
                            current_sn,
                            pdcp_pdu_p,
                            sdu_buffer_sizeP);

        if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
          stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].apply_security);
        } else {
          stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].apply_security);
        }
      }

      /* Print octets of outgoing data in hexadecimal form */
      LOG_D(PDCP, "Following content with size %d will be sent over RLC (PDCP PDU header is the first two bytes)\n",
            pdcp_pdu_size);
      // util_print_hex_octets(PDCP, (unsigned char*)pdcp_pdu_p, pdcp_pdu_size);
      // util_flush_hex_octets(PDCP, (unsigned char*)pdcp_pdu, pdcp_pdu_size);
    } else {
      LOG_E(PDCP, "Cannot create a mem_block for a PDU!\n");

      if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
        stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
      } else {
        stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
      }

      LOG_E(PDCP,  "[FRAME %5u][%s][PDCP][MOD %u][RB %ld] PDCP_DATA_REQ SDU DROPPED, OUT OF MEMORY \n",
            ctxt_pP->frame,
            (ctxt_pP->enb_flag) ? "eNB" : "UE",
            ctxt_pP->module_id,
            rb_idP);
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ,VCD_FUNCTION_OUT);
      return false;
    }

    /*
     * Ask sublayer to transmit data and check return value
     * to see if RLC succeeded
     */
    LOG_DUMPMSG(PDCP,
                DEBUG_PDCP,
                (char *)pdcp_pdu_p,
                pdcp_pdu_size,
                "[MSG] PDCP DL %s PDU on rb_id %ld\n",
                (srb_flagP) ? "CONTROL" : "DATA",
                rb_idP);

    if ((pdcp_pdu_p!=NULL) && (srb_flagP == 0) && (ctxt_pP->enb_flag == 1)) {
      LOG_D(PDCP, "pdcp data req on drb %ld, size %d, rnti %lx\n", rb_idP, pdcp_pdu_size, ctxt_pP->rntiMaybeUEid);

      rlc_status = rlc_data_req(ctxt_pP,
                                srb_flagP,
                                MBMS_FLAG_NO,
                                rb_idP,
                                muiP,
                                confirmP,
                                pdcp_pdu_size,
                                pdcp_pdu_p,
                                sourceL2Id,
                                destinationL2Id);
      ret = false;
      switch (rlc_status) {
        case RLC_OP_STATUS_OK:
          LOG_D(PDCP, "Data sending request over RLC succeeded!\n");
          ret=true;
          break;

        case RLC_OP_STATUS_BAD_PARAMETER:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Bad Parameter' reason!\n");
          break;

        case RLC_OP_STATUS_INTERNAL_ERROR:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Internal Error' reason!\n");
          break;

        case RLC_OP_STATUS_OUT_OF_RESSOURCES:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Out of Resources' reason!\n");
          break;

        default:
          LOG_W(PDCP, "RLC returned an unknown status code after PDCP placed the order to send some data (Status Code:%d)\n", rlc_status);
          break;
      } // switch case
    } else { // SRB
      rlc_status = rlc_data_req(ctxt_pP
                                , srb_flagP
                                , MBMS_FLAG_NO
                                , rb_idP
                                , muiP
                                , confirmP
                                , pdcp_pdu_size
                                , pdcp_pdu_p
                                ,NULL
                                ,NULL
                               );

      switch (rlc_status) {
        case RLC_OP_STATUS_OK:
          LOG_D(PDCP, "Data sending request over RLC succeeded!\n");
          ret=true;
          break;

        case RLC_OP_STATUS_BAD_PARAMETER:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Bad Parameter' reason!\n");
          ret= false;
          break;

        case RLC_OP_STATUS_INTERNAL_ERROR:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Internal Error' reason!\n");
          ret= false;
          break;

        case RLC_OP_STATUS_OUT_OF_RESSOURCES:
          LOG_W(PDCP, "Data sending request over RLC failed with 'Out of Resources' reason!\n");
          ret= false;
          break;

        default:
          LOG_W(PDCP, "RLC returned an unknown status code after PDCP placed the order to send some data (Status Code:%d)\n", rlc_status);
          ret= false;
          break;
      } // switch case
    }
  }

  if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
    stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_req);
  } else {
    stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_req);
  }

  /*
   * Control arrives here only if rlc_data_req() returns RLC_OP_STATUS_OK
   * so we return true afterwards
   */

  for (pdcp_uid = 0; pdcp_uid < MAX_MOBILES_PER_ENB; pdcp_uid++) {
    if (pdcp_enb[ctxt_pP->module_id].rnti[pdcp_uid] == ctxt_pP->rntiMaybeUEid)
      break;
  }

  if (pdcp_uid == MAX_MOBILES_PER_ENB)
    return ret;

  LOG_D(PDCP,"ueid %d lcid %d tx seq num %d\n", pdcp_uid, (int)(rb_idP+rb_offset), current_sn);
  Pdcp_stats_tx[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]++;
  Pdcp_stats_tx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]++;
  Pdcp_stats_tx_bytes[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+=sdu_buffer_sizeP;
  Pdcp_stats_tx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+=sdu_buffer_sizeP;
  Pdcp_stats_tx_sn[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]=current_sn;
  Pdcp_stats_tx_aiat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+= (pdcp_enb[ctxt_pP->module_id].sfn - Pdcp_stats_tx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]);
  Pdcp_stats_tx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+= (pdcp_enb[ctxt_pP->module_id].sfn - Pdcp_stats_tx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]);
  Pdcp_stats_tx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]=pdcp_enb[ctxt_pP->module_id].sfn;
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_REQ,VCD_FUNCTION_OUT);
  return ret;
}


//-----------------------------------------------------------------------------
bool pdcp_data_ind(const protocol_ctxt_t *const ctxt_pP,
                   const srb_flag_t srb_flagP,
                   const MBMS_flag_t MBMS_flagP,
                   const rb_id_t rb_idP,
                   const sdu_size_t sdu_buffer_sizeP,
                   uint8_t *const sdu_buffer_pP)
//-----------------------------------------------------------------------------
{
  pdcp_t      *pdcp_p          = NULL;
  uint8_t      pdcp_header_len = 0;
  uint8_t      pdcp_tailer_len = 0;
  pdcp_sn_t    sequence_number = 0;
  volatile sdu_size_t   payload_offset  = 0;
  rb_id_t      rb_id            = rb_idP;
  bool         packet_forwarded = false;
  hash_key_t      key             = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t  h_rc;
  uint8_t      rb_offset= (srb_flagP == 0) ? DTCH -1 :0;
  uint16_t     pdcp_uid=0;

  uint32_t    rx_hfn_for_count;
  int         pdcp_sn_for_count;
  int         security_ok;
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_IN);
  LOG_DUMPMSG(PDCP,
              DEBUG_PDCP,
              (char *)sdu_buffer_pP,
              sdu_buffer_sizeP,
              "[MSG] PDCP UL %s PDU on rb_id %ld\n",
              (srb_flagP) ? "CONTROL" : "DATA",
              rb_idP);

  if (MBMS_flagP) {
    AssertError(rb_idP < NB_RB_MBMS_MAX, return false, "RB id is too high (%ld/%d) %u rnti %lx!\n", rb_idP, NB_RB_MBMS_MAX, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);

    if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
      LOG_D(PDCP,
            "e-MBMS Data indication notification for PDCP entity from eNB %u to UE %lx "
            "and radio bearer ID %ld rlc sdu size %d ctxt_pP->enb_flag %d\n",
            ctxt_pP->module_id,
            ctxt_pP->rntiMaybeUEid,
            rb_idP,
            sdu_buffer_sizeP,
            ctxt_pP->enb_flag);
    } else {
      LOG_D(PDCP,
            "Data indication notification for PDCP entity from UE %lx to eNB %u "
            "and radio bearer ID %ld rlc sdu size %d ctxt_pP->enb_flag %d\n",
            ctxt_pP->rntiMaybeUEid,
            ctxt_pP->module_id,
            rb_idP,
            sdu_buffer_sizeP,
            ctxt_pP->enb_flag);
    }
  } else {
    rb_id = rb_idP % LTE_maxDRB;
    AssertError(rb_id < LTE_maxDRB, return false, "RB id is too high (%ld/%d) %u UE %lx!\n", rb_id, LTE_maxDRB, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
    AssertError(rb_id > 0, return false, "RB id is too low (%ld/%d) %u UE %lx!\n", rb_id, LTE_maxDRB, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
    key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, rb_id, srb_flagP);
    h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

    if (h_rc != HASH_TABLE_OK) {
      LOG_W(PDCP,
            PROTOCOL_CTXT_FMT"Could not get PDCP instance key 0x%"PRIx64"\n",
            PROTOCOL_CTXT_ARGS(ctxt_pP),
            key);
      free(sdu_buffer_pP);
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_OUT);
      return false;
    }
  }


  if (sdu_buffer_sizeP == 0) {
    LOG_W(PDCP, "SDU buffer size is zero! Ignoring this chunk!\n");
    return false;
  }

  if (ctxt_pP->enb_flag) {
    start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_ind);
  } else {
    start_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_ind);
  }

  /*
   * Parse the PDU placed at the beginning of SDU to check
   * if incoming SN is in line with RX window
   */

  if (MBMS_flagP == 0 ) {
    if (srb_flagP) { //SRB1/2
      pdcp_header_len = PDCP_CONTROL_PLANE_DATA_PDU_SN_SIZE;
      pdcp_tailer_len = PDCP_CONTROL_PLANE_DATA_PDU_MAC_I_SIZE;
      sequence_number = pdcp_get_sequence_number_of_pdu_with_SRB_sn((unsigned char *)sdu_buffer_pP);
    } else { // DRB
      pdcp_tailer_len = 0;

      if (pdcp_p->seq_num_size == 7) {
        pdcp_header_len = PDCP_USER_PLANE_DATA_PDU_SHORT_SN_HEADER_SIZE;
        sequence_number = pdcp_get_sequence_number_of_pdu_with_short_sn((unsigned char *)sdu_buffer_pP);
      } else if (pdcp_p->seq_num_size == 12) {
        pdcp_header_len = PDCP_USER_PLANE_DATA_PDU_LONG_SN_HEADER_SIZE;
        sequence_number = pdcp_get_sequence_number_of_pdu_with_long_sn((unsigned char *)sdu_buffer_pP);
      } else {
        //sequence_number = 4095;
        LOG_E(PDCP,
              PROTOCOL_PDCP_CTXT_FMT"wrong sequence number  (%d) for this pdcp entity \n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
              pdcp_p->seq_num_size);
        exit(1);
      }

    }

    /*
     * Check if incoming SDU is long enough to carry a PDU header
     */
    if (sdu_buffer_sizeP < pdcp_header_len + pdcp_tailer_len ) {
      LOG_W(PDCP,
            PROTOCOL_PDCP_CTXT_FMT"Incoming (from RLC) SDU is short of size (size:%d)! Ignoring...\n",
            PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
            sdu_buffer_sizeP);
      free(sdu_buffer_pP);

      if (ctxt_pP->enb_flag) {
        stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_ind);
      } else {
        stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_ind);
      }

      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_OUT);
      return false;
    }

#if 0

    /* Removed by Cedric */
    if (pdcp_is_rx_seq_number_valid(sequence_number, pdcp_p, srb_flagP) == true) {
      LOG_T(PDCP, "Incoming PDU has a sequence number (%d) in accordance with RX window\n", sequence_number);
      /* if (dc == PDCP_DATA_PDU )
      LOG_D(PDCP, "Passing piggybacked SDU to NAS driver...\n");
      else
      LOG_D(PDCP, "Passing piggybacked SDU to RRC ...\n");*/
    } else {
      Pdcp_stats_rx_outoforder[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]++;
      LOG_E(PDCP,
            PROTOCOL_PDCP_CTXT_FMT"Incoming PDU has an unexpected sequence number (%d), RX window synchronisation have probably been lost!\n",
            PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
            sequence_number);
      /*
       * XXX Till we implement in-sequence delivery and duplicate discarding
       * mechanism all out-of-order packets will be delivered to RRC/IP
       */
      LOG_W(PDCP, "Ignoring PDU...\n");
      free(sdu_buffer_pP);
      return false;
    }

#endif

    // SRB1/2: control-plane data
    if (srb_flagP) {
      /* process as described in 36.323 5.1.2.2 */
      if (sequence_number < pdcp_p->next_pdcp_rx_sn) {
        rx_hfn_for_count  = pdcp_p->rx_hfn + 1;
        pdcp_sn_for_count = sequence_number;
      } else {
        rx_hfn_for_count  = pdcp_p->rx_hfn;
        pdcp_sn_for_count = sequence_number;
      }

      if (pdcp_p->security_activated == 1) {
        if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
          start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
        } else {
          start_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
        }

        security_ok = pdcp_validate_security(ctxt_pP,
                                             pdcp_p,
                                             srb_flagP,
                                             rb_idP,
                                             pdcp_header_len,
                                             rx_hfn_for_count,
                                             pdcp_sn_for_count,
                                             sdu_buffer_pP,
                                             sdu_buffer_sizeP - pdcp_tailer_len)
                      == 0;

        if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
          stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
        } else {
          stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
        }
      } else {
        security_ok = 1;
      }

      if (security_ok == 0) {
        LOG_W(PDCP,
              PROTOCOL_PDCP_CTXT_FMT"security not validated for incoming PDCP SRB PDU\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
        LOG_W(PDCP, "Ignoring PDU...\n");
        free(sdu_buffer_pP);
        /* TODO: indicate integrity verification failure to upper layer */
        return false;
      }

      if (sequence_number < pdcp_p->next_pdcp_rx_sn)
        pdcp_p->rx_hfn++;

      pdcp_p->next_pdcp_rx_sn = sequence_number + 1;

      if (pdcp_p->next_pdcp_rx_sn > pdcp_p->maximum_pdcp_rx_sn) {
        pdcp_p->next_pdcp_rx_sn = 0;
        pdcp_p->rx_hfn++;
      }

      rrc_data_ind(ctxt_pP,
                   rb_id,
                   sdu_buffer_sizeP - pdcp_header_len - pdcp_tailer_len,
                   (uint8_t *)&sdu_buffer_pP[pdcp_header_len]);
      free(sdu_buffer_pP);

      if (ctxt_pP->enb_flag) {
        stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_ind);
      } else {
        stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_ind);
      }

      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_OUT);
      return true;
    } /* if (srb_flagP) */

    /*
     * DRBs
     */
    payload_offset=pdcp_header_len;// PDCP_USER_PLANE_DATA_PDU_LONG_SN_HEADER_SIZE;

    switch (pdcp_p->rlc_mode) {
      case RLC_MODE_AM: {
        /* process as described in 36.323 5.1.2.1.2 */
        int reordering_window;

        if (pdcp_p->seq_num_size == 7)
          reordering_window = REORDERING_WINDOW_SN_7BIT;
        else
          reordering_window = REORDERING_WINDOW_SN_12BIT;

        if (sequence_number - pdcp_p->last_submitted_pdcp_rx_sn > reordering_window ||
            (pdcp_p->last_submitted_pdcp_rx_sn - sequence_number >= 0 &&
             pdcp_p->last_submitted_pdcp_rx_sn - sequence_number < reordering_window)) {
          /* TODO: specs say to decipher and do header decompression */
          LOG_W(PDCP,
                PROTOCOL_PDCP_CTXT_FMT"discard PDU, out of\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
          LOG_W(PDCP, "Ignoring PDU...\n");
          free(sdu_buffer_pP);
          /* TODO: indicate integrity verification failure to upper layer */
          return false;
        } else if (pdcp_p->next_pdcp_rx_sn - sequence_number > reordering_window) {
          pdcp_p->rx_hfn++;
          rx_hfn_for_count  = pdcp_p->rx_hfn;
          pdcp_sn_for_count = sequence_number;
          pdcp_p->next_pdcp_rx_sn = sequence_number + 1;
        } else if (sequence_number - pdcp_p->next_pdcp_rx_sn >= reordering_window) {
          rx_hfn_for_count  = pdcp_p->rx_hfn - 1;
          pdcp_sn_for_count = sequence_number;
        } else if (sequence_number >= pdcp_p->next_pdcp_rx_sn) {
          rx_hfn_for_count  = pdcp_p->rx_hfn;
          pdcp_sn_for_count = sequence_number;
          pdcp_p->next_pdcp_rx_sn = sequence_number + 1;

          if (pdcp_p->next_pdcp_rx_sn > pdcp_p->maximum_pdcp_rx_sn) {
            pdcp_p->next_pdcp_rx_sn = 0;
            pdcp_p->rx_hfn++;
          }
        } else { /* sequence_number < pdcp_p->next_pdcp_rx_sn */
          rx_hfn_for_count  = pdcp_p->rx_hfn;
          pdcp_sn_for_count = sequence_number;
        }

        if (pdcp_p->security_activated == 1) {
          if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
            start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
          } else {
            start_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
          }

          security_ok = pdcp_validate_security(ctxt_pP,
                                               pdcp_p,
                                               srb_flagP,
                                               rb_idP,
                                               pdcp_header_len,
                                               rx_hfn_for_count,
                                               pdcp_sn_for_count,
                                               sdu_buffer_pP,
                                               sdu_buffer_sizeP - pdcp_tailer_len)
                        == 0;

          if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
            stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
          } else {
            stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
          }
        } else {
          security_ok = 1;
        }

        if (security_ok == 0) {
          LOG_W(PDCP,
                PROTOCOL_PDCP_CTXT_FMT"security not validated for incoming PDPC DRB RLC/AM PDU\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
          LOG_W(PDCP, "Ignoring PDU...\n");
          free(sdu_buffer_pP);
          /* TODO: indicate integrity verification failure to upper layer */
          return false;
        }

        /* TODO: specs say we have to store this PDU in a list and then deliver
         *       stored packets to upper layers according to a well defined
         *       procedure. The code below that deals with delivery is today
         *       too complex to do this properly, so we only send the current
         *       received packet. This is not correct and has to be fixed
         *       some day.
         *       In the meantime, let's pretend the last submitted PDCP SDU
         *       is the current one.
         * TODO: we also have to deal with re-establishment PDU (control PDUs)
         *       that contain no SDU.
         */
        pdcp_p->last_submitted_pdcp_rx_sn = sequence_number;
        break;
        } /* case RLC_MODE_AM */

      case RLC_MODE_UM:

        /* process as described in 36.323 5.1.2.1.3 */
        if (sequence_number < pdcp_p->next_pdcp_rx_sn) {
          pdcp_p->rx_hfn++;
        }

        rx_hfn_for_count  = pdcp_p->rx_hfn;
        pdcp_sn_for_count = sequence_number;
        pdcp_p->next_pdcp_rx_sn = sequence_number + 1;

        if (pdcp_p->next_pdcp_rx_sn > pdcp_p->maximum_pdcp_rx_sn) {
          pdcp_p->next_pdcp_rx_sn = 0;
          pdcp_p->rx_hfn++;
        }

        if (pdcp_p->security_activated == 1) {
          if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
            start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
          } else {
            start_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
          }

          security_ok = pdcp_validate_security(ctxt_pP,
                                               pdcp_p,
                                               srb_flagP,
                                               rb_idP,
                                               pdcp_header_len,
                                               rx_hfn_for_count,
                                               pdcp_sn_for_count,
                                               sdu_buffer_pP,
                                               sdu_buffer_sizeP - pdcp_tailer_len)
                        == 0;

          if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
            stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].validate_security);
          } else {
            stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].validate_security);
          }
        } else {
          security_ok = 1;
        }

        if (security_ok == 0) {
          LOG_W(PDCP,
                PROTOCOL_PDCP_CTXT_FMT"security not validated for incoming PDPC DRB RLC/UM PDU\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
          LOG_W(PDCP, "Ignoring PDU...\n");
          free(sdu_buffer_pP);
          /* TODO: indicate integrity verification failure to upper layer */
          return false;
        }

        break;

      default:
        LOG_E(PDCP, "bad RLC mode, cannot happen.\n");
        exit(1);
    } /* switch (pdcp_p->rlc_mode) */
  } else { /* MBMS_flagP == 0 */
    payload_offset=0;
  }

  if (otg_enabled==1) {
    LOG_D(OTG,"Discarding received packed\n");
    free(sdu_buffer_pP);

    if (ctxt_pP->enb_flag) {
      stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_ind);
    } else {
      stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_ind);
    }

    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_OUT);
    return true;
  }

  // XXX Decompression would be done at this point
  /*
   * After checking incoming sequence number PDCP header
   * has to be stripped off so here we copy SDU buffer starting
   * from its second byte (skipping 0th and 1st octets, i.e.
   * PDCP header)
   */

  if (LINK_ENB_PDCP_TO_GTPV1U) {
    if ((true == ctxt_pP->enb_flag) && (false == srb_flagP)) {
      ue_id_t ue_id = ctxt_pP->rntiMaybeUEid;
      uint8_t *gtp_buf = sdu_buffer_pP + payload_offset;
      size_t gtp_len = sdu_buffer_sizeP - payload_offset;
      LOG_D(PDCP, "Sending packet to GTP  ue %lx rab %ld len %ld\n", ue_id, rb_id + 4, gtp_len);
      gtpv1uSendDirect(INSTANCE_DEFAULT, ue_id, rb_id + 4, gtp_buf, gtp_len, false, false);
      packet_forwarded = true;
    }
  } else {
    packet_forwarded = false;
  }

#ifdef MBMS_MULTICAST_OUT

  if ((MBMS_flagP != 0) && (mbms_socket != -1)) {
    // struct iphdr   *ip_header = (struct iphdr *)&sdu_buffer_pP[payload_offset];
    // struct udphdr *udp_header = (struct udphdr *)&sdu_buffer_pP[payload_offset + sizeof(struct iphdr)];
    // struct sockaddr_in dest_addr;
    // dest_addr.sin_family      = AF_INET;
    // dest_addr.sin_port        = udp_header->dest;
    // dest_addr.sin_addr.s_addr = ip_header->daddr;

    // sendto(mbms_socket, &sdu_buffer_pP[payload_offset], sdu_buffer_sizeP - payload_offset, MSG_DONTWAIT, (struct
    // sockaddr*)&dest_addr, sizeof(dest_addr));
    // //packet_forwarded = true;
  }

#endif

  if (false == packet_forwarded) {
    notifiedFIFO_elt_t * new_sdu_p = newNotifiedFIFO_elt(sdu_buffer_sizeP - payload_offset + sizeof (pdcp_data_ind_header_t), 0, NULL, NULL);

      if ((MBMS_flagP == 0) && (pdcp_p->rlc_mode == RLC_MODE_AM)) {
        pdcp_p->last_submitted_pdcp_rx_sn = sequence_number;
      }

      /*
       * Prepend PDCP indication header which is going to be removed at pdcp_fifo_flush_sdus()
       */
      pdcp_data_ind_header_t * pdcpHead=(pdcp_data_ind_header_t *)NotifiedFifoData(new_sdu_p);
      memset(pdcpHead, 0, sizeof (pdcp_data_ind_header_t));
      pdcpHead->data_size = sdu_buffer_sizeP - payload_offset;
      AssertFatal((sdu_buffer_sizeP - payload_offset >= 0), "invalid PDCP SDU size!");

      // Here there is no virtualization possible
      // set ((pdcp_data_ind_header_t *) new_sdu_p)->inst for IP layer here
      if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
        pdcpHead->rb_id = rb_id;

        if (!IS_SOFTMODEM_NOS1) {
          /* for the UE compiled in S1 mode, we need 1 here
           * for the UE compiled in noS1 mode, we need 0
           * TODO: be sure of this
           */
          if (NFAPI_MODE == NFAPI_UE_STUB_PNF || NFAPI_MODE == NFAPI_MODE_STANDALONE_PNF) {
            pdcpHead->inst  = ctxt_pP->module_id;
          } else {  // nfapi_mode
            if (UE_NAS_USE_TUN) {
              pdcpHead->inst  = ctxt_pP->module_id;
            } else {
              pdcpHead->inst  = 1;
            }
          } // nfapi_mode
        } else {
          if (UE_NAS_USE_TUN) {
            pdcpHead->inst = ctxt_pP->module_id;
          } else if (ENB_NAS_USE_TUN) {
            pdcpHead->inst = 0;
          }
        }
      } else {
        pdcpHead->rb_id = rb_id + (ctxt_pP->module_id * LTE_maxDRB);
        pdcpHead->inst  = ctxt_pP->module_id;
      }

      if( LOG_DEBUGFLAG(DEBUG_PDCP) ) {
        static uint32_t pdcp_inst = 0;
        pdcpHead->inst = pdcp_inst++;
        LOG_D(PDCP, "inst=%d size=%d\n", pdcpHead->inst, pdcpHead->data_size);
      }

      memcpy(pdcpHead + 1, &sdu_buffer_pP[payload_offset], sdu_buffer_sizeP - payload_offset);
      if( LOG_DEBUGFLAG(DEBUG_PDCP) )
	log_dump(PDCP, pdcpHead+1, min(sdu_buffer_sizeP - payload_offset,30) , LOG_DUMP_CHAR,
	         "Printing first bytes of PDCP SDU before adding it to the list: \n");
      pushNotifiedFIFO(&pdcp_sdu_list, new_sdu_p); 

    /* Print octets of incoming data in hexadecimal form */
      LOG_D(PDCP, "Following content has been received from RLC (%d,%d)(PDCP header has already been removed):\n",
          sdu_buffer_sizeP  - payload_offset + (int)sizeof(pdcp_data_ind_header_t),
          sdu_buffer_sizeP  - payload_offset);
      // util_print_hex_octets(PDCP, &new_sdu_p[sizeof (pdcp_data_ind_header_t)], sdu_buffer_sizeP - payload_offset);
      // util_flush_hex_octets(PDCP, &new_sdu_p[sizeof (pdcp_data_ind_header_t)], sdu_buffer_sizeP - payload_offset);
  }

  /* Update PDCP statistics */
  for (pdcp_uid = 0; pdcp_uid < MAX_MOBILES_PER_ENB; pdcp_uid++) {
    if (pdcp_enb[ctxt_pP->module_id].rnti[pdcp_uid] == ctxt_pP->rntiMaybeUEid) {
      break;
    }
  }
  if( pdcp_uid < MAX_MOBILES_PER_ENB ) {
    Pdcp_stats_rx[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]++;
    Pdcp_stats_rx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]++;
    Pdcp_stats_rx_bytes[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+=(sdu_buffer_sizeP  - payload_offset);
    Pdcp_stats_rx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+=(sdu_buffer_sizeP  - payload_offset);
    
    Pdcp_stats_rx_sn[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]=sequence_number;
    Pdcp_stats_rx_aiat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+= (pdcp_enb[ctxt_pP->module_id].sfn - Pdcp_stats_rx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]);
    Pdcp_stats_rx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]+=(pdcp_enb[ctxt_pP->module_id].sfn - Pdcp_stats_rx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]);
    Pdcp_stats_rx_iat[ctxt_pP->module_id][pdcp_uid][rb_idP+rb_offset]=pdcp_enb[ctxt_pP->module_id].sfn;
  }
  free(sdu_buffer_pP);

  if (ctxt_pP->enb_flag) {
    stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].data_ind);
  } else {
    stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].data_ind);
  }

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_DATA_IND,VCD_FUNCTION_OUT);
  return true;
}

void pdcp_update_stats(const protocol_ctxt_t *const  ctxt_pP) {
  uint16_t           pdcp_uid = 0;
  uint8_t            rb_id     = 0;

  // these stats are measured for both eNB and UE on per seond basis
  for (rb_id =0; rb_id < NB_RB_MAX; rb_id ++) {
    for (pdcp_uid=0; pdcp_uid< MAX_MOBILES_PER_ENB; pdcp_uid++) {
      //printf("frame %d and subframe %d \n", pdcp_enb[ctxt_pP->module_id].frame, pdcp_enb[ctxt_pP->module_id].subframe);
      // tx stats
      if (Pdcp_stats_tx_window_ms[ctxt_pP->module_id][pdcp_uid] > 0 &&
          pdcp_enb[ctxt_pP->module_id].sfn % Pdcp_stats_tx_window_ms[ctxt_pP->module_id][pdcp_uid] == 0) {
        // unit: bit/s
        Pdcp_stats_tx_throughput_w[ctxt_pP->module_id][pdcp_uid][rb_id]=Pdcp_stats_tx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]*8;
        Pdcp_stats_tx_w[ctxt_pP->module_id][pdcp_uid][rb_id]= Pdcp_stats_tx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id];
        Pdcp_stats_tx_bytes_w[ctxt_pP->module_id][pdcp_uid][rb_id]= Pdcp_stats_tx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id];

        if (Pdcp_stats_tx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id] > 0) {
          Pdcp_stats_tx_aiat_w[ctxt_pP->module_id][pdcp_uid][rb_id]=(Pdcp_stats_tx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]/Pdcp_stats_tx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]);
        } else {
          Pdcp_stats_tx_aiat_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        }

        // reset the tmp vars
        Pdcp_stats_tx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        Pdcp_stats_tx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        Pdcp_stats_tx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
      }

      if (Pdcp_stats_rx_window_ms[ctxt_pP->module_id][pdcp_uid] > 0 &&
          pdcp_enb[ctxt_pP->module_id].sfn % Pdcp_stats_rx_window_ms[ctxt_pP->module_id][pdcp_uid] == 0) {
        // rx stats
        Pdcp_stats_rx_goodput_w[ctxt_pP->module_id][pdcp_uid][rb_id]=Pdcp_stats_rx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]*8;
        Pdcp_stats_rx_w[ctxt_pP->module_id][pdcp_uid][rb_id]=   Pdcp_stats_rx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id];
        Pdcp_stats_rx_bytes_w[ctxt_pP->module_id][pdcp_uid][rb_id]= Pdcp_stats_rx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id];

        if(Pdcp_stats_rx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id] > 0) {
          Pdcp_stats_rx_aiat_w[ctxt_pP->module_id][pdcp_uid][rb_id]= (Pdcp_stats_rx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]/Pdcp_stats_rx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]);
        } else {
          Pdcp_stats_rx_aiat_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        }

        // reset the tmp vars
        Pdcp_stats_rx_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        Pdcp_stats_rx_bytes_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
        Pdcp_stats_rx_aiat_tmp_w[ctxt_pP->module_id][pdcp_uid][rb_id]=0;
      }
    }
  }
}


//-----------------------------------------------------------------------------
void
pdcp_run (
  const protocol_ctxt_t *const  ctxt_pP
)
//-----------------------------------------------------------------------------
{
  if (ctxt_pP->enb_flag) {
    start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].pdcp_run);
  } else {
    start_meas(&UE_pdcp_stats[ctxt_pP->module_id].pdcp_run);
  }

  pdcp_enb[ctxt_pP->module_id].sfn++; // range: 0 to 18,446,744,073,709,551,615
  pdcp_enb[ctxt_pP->module_id].frame=ctxt_pP->frame; // 1023
  pdcp_enb[ctxt_pP->module_id].subframe= ctxt_pP->subframe;
  pdcp_update_stats(ctxt_pP);
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_RUN, VCD_FUNCTION_IN);
  MessageDef   *msg_p;
  int           result;
  protocol_ctxt_t  ctxt;

  do {
    // Checks if a message has been sent to PDCP sub-task
    itti_poll_msg (ctxt_pP->enb_flag ? TASK_PDCP_ENB : TASK_PDCP_UE, &msg_p);

    if (msg_p != NULL) {
      switch (ITTI_MSG_ID(msg_p)) {
        case RRC_DCCH_DATA_REQ:
          PROTOCOL_CTXT_SET_BY_MODULE_ID(
            &ctxt,
            RRC_DCCH_DATA_REQ (msg_p).module_id,
            RRC_DCCH_DATA_REQ (msg_p).enb_flag,
            RRC_DCCH_DATA_REQ (msg_p).rnti,
            RRC_DCCH_DATA_REQ (msg_p).frame,
            0,
            RRC_DCCH_DATA_REQ (msg_p).eNB_index);
          LOG_D(PDCP, PROTOCOL_CTXT_FMT"Received %s from %s: instance %ld, rb_id %ld, muiP %d, confirmP %d, mode %d\n",
                PROTOCOL_CTXT_ARGS(&ctxt),
                ITTI_MSG_NAME (msg_p),
                ITTI_MSG_ORIGIN_NAME(msg_p),
                ITTI_MSG_DESTINATION_INSTANCE (msg_p),
                RRC_DCCH_DATA_REQ (msg_p).rb_id,
                RRC_DCCH_DATA_REQ (msg_p).muip,
                RRC_DCCH_DATA_REQ (msg_p).confirmp,
                RRC_DCCH_DATA_REQ (msg_p).mode);
          LOG_D(PDCP, "Before calling pdcp_data_req from pdcp_run! RRC_DCCH_DATA_REQ (msg_p).rb_id: %ld \n", RRC_DCCH_DATA_REQ (msg_p).rb_id);
          result = pdcp_data_req (&ctxt,
                                  SRB_FLAG_YES,
                                  RRC_DCCH_DATA_REQ (msg_p).rb_id,
                                  RRC_DCCH_DATA_REQ (msg_p).muip,
                                  RRC_DCCH_DATA_REQ (msg_p).confirmp,
                                  RRC_DCCH_DATA_REQ (msg_p).sdu_size,
                                  RRC_DCCH_DATA_REQ (msg_p).sdu_p,
                                  RRC_DCCH_DATA_REQ (msg_p).mode,
                                  NULL, NULL
                                 );

          if (result != true)
            LOG_E(PDCP, "PDCP data request failed!\n");

          // Message buffer has been processed, free it now.
          result = itti_free (ITTI_MSG_ORIGIN_ID(msg_p), RRC_DCCH_DATA_REQ (msg_p).sdu_p);
          AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
          break;

        case RRC_PCCH_DATA_REQ: {
          sdu_size_t     sdu_buffer_sizeP;
          sdu_buffer_sizeP = RRC_PCCH_DATA_REQ(msg_p).sdu_size;
          uint8_t CC_id = RRC_PCCH_DATA_REQ(msg_p).CC_id;
          uint8_t ue_index = RRC_PCCH_DATA_REQ(msg_p).ue_index;
          RC.rrc[ctxt_pP->module_id]->carrier[CC_id].sizeof_paging[ue_index] = sdu_buffer_sizeP;

          if (sdu_buffer_sizeP > 0) {
            memcpy(RC.rrc[ctxt_pP->module_id]->carrier[CC_id].paging[ue_index], RRC_PCCH_DATA_REQ(msg_p).sdu_p, sdu_buffer_sizeP);
          }

          //paging pdcp log
          LOG_D(PDCP, "PDCP Received RRC_PCCH_DATA_REQ CC_id %d length %d \n", CC_id, sdu_buffer_sizeP);
        }
        break;

        default:
          LOG_E(PDCP, "Received unexpected message %s\n", ITTI_MSG_NAME (msg_p));
          break;
      }

      result = itti_free (ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
      AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    }
  } while(msg_p != NULL);

  // IP/NAS -> PDCP traffic : TX, read the pkt from the upper layer buffer
  //  if (LINK_ENB_PDCP_TO_GTPV1U && ctxt_pP->enb_flag == ENB_FLAG_NO) {
  if (IS_SOFTMODEM_NOS1 || ctxt_pP->enb_flag == ENB_FLAG_NO) {
    pdcp_fifo_read_input_sdus(ctxt_pP);
  }

  // PDCP -> NAS/IP traffic: RX
  if (ctxt_pP->enb_flag) {
    start_meas(&eNB_pdcp_stats[ctxt_pP->module_id].pdcp_ip);
  } else {
    start_meas(&UE_pdcp_stats[ctxt_pP->module_id].pdcp_ip);
  }
  pdcp_fifo_flush_sdus(ctxt_pP);

  if (ctxt_pP->enb_flag) {
    stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].pdcp_ip);
  } else {
    stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].pdcp_ip);
  }

  if (ctxt_pP->enb_flag) {
    stop_meas(&eNB_pdcp_stats[ctxt_pP->module_id].pdcp_run);
  } else {
    stop_meas(&UE_pdcp_stats[ctxt_pP->module_id].pdcp_run);
  }

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PDCP_RUN, VCD_FUNCTION_OUT);
}

//-----------------------------------------------------------------------------
void
pdcp_mbms_run (
  const protocol_ctxt_t *const  ctxt_pP
)
//-----------------------------------------------------------------------------
{
  pdcp_fifo_read_input_mbms_sdus_fromtun(ctxt_pP);
}

void pdcp_init_stats_UE(module_id_t mod, uint16_t uid) {
  Pdcp_stats_tx_window_ms[mod][uid] = 100;
  Pdcp_stats_rx_window_ms[mod][uid] = 100;

  for (int i = 0; i < NB_RB_MAX; ++i) {
    Pdcp_stats_tx_bytes[mod][uid][i] = 0;
    Pdcp_stats_tx_bytes_w[mod][uid][i] = 0;
    Pdcp_stats_tx_bytes_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_tx[mod][uid][i] = 0;
    Pdcp_stats_tx_w[mod][uid][i] = 0;
    Pdcp_stats_tx_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_tx_sn[mod][uid][i] = 0;
    Pdcp_stats_tx_throughput_w[mod][uid][i] = 0;
    Pdcp_stats_tx_aiat[mod][uid][i] = 0;
    Pdcp_stats_tx_aiat_w[mod][uid][i] = 0;
    Pdcp_stats_tx_aiat_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_tx_iat[mod][uid][i] = 0;
    Pdcp_stats_rx[mod][uid][i] = 0;
    Pdcp_stats_rx_w[mod][uid][i] = 0;
    Pdcp_stats_rx_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_rx_bytes[mod][uid][i] = 0;
    Pdcp_stats_rx_bytes_w[mod][uid][i] = 0;
    Pdcp_stats_rx_bytes_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_rx_sn[mod][uid][i] = 0;
    Pdcp_stats_rx_goodput_w[mod][uid][i] = 0;
    Pdcp_stats_rx_aiat[mod][uid][i] = 0;
    Pdcp_stats_rx_aiat_w[mod][uid][i] = 0;
    Pdcp_stats_rx_aiat_tmp_w[mod][uid][i] = 0;
    Pdcp_stats_rx_iat[mod][uid][i] = 0;
    Pdcp_stats_rx_outoforder[mod][uid][i] = 0;
  }
}

void pdcp_add_UE(const protocol_ctxt_t *const  ctxt_pP) {
  int i, ue_flag=1; //, ret=-1; to be decied later

  for (i=0; i < MAX_MOBILES_PER_ENB; i++) {
    if (pdcp_enb[ctxt_pP->module_id].rnti[i] == ctxt_pP->rntiMaybeUEid) {
      ue_flag=-1;
      break;
    }
  }

  if (ue_flag == 1 ) {
    for (i=0; i < MAX_MOBILES_PER_ENB ; i++) {
      if (pdcp_enb[ctxt_pP->module_id].rnti[i] == 0 ) {
        pdcp_enb[ctxt_pP->module_id].rnti[i] = ctxt_pP->rntiMaybeUEid;
        pdcp_enb[ctxt_pP->module_id].uid[i]=i;
        pdcp_enb[ctxt_pP->module_id].num_ues++;
        LOG_I(PDCP, "add new uid is %d %lx\n", i, ctxt_pP->rntiMaybeUEid);
        pdcp_init_stats_UE(ctxt_pP->module_id, i);
        // ret=1;
        break;
      }
    }
  }

  //return ret;
}

//-----------------------------------------------------------------------------
bool pdcp_remove_UE(const protocol_ctxt_t *const  ctxt_pP)
//-----------------------------------------------------------------------------
{
  LTE_DRB_Identity_t  srb_id         = 0;
  LTE_DRB_Identity_t  drb_id         = 0;
  hash_key_t      key            = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t  h_rc;
  int i;
  // check and remove SRBs first

  for(int i = 0; i<MAX_MOBILES_PER_ENB; i++) {
    if (pdcp_eNB_UE_instance_to_rnti[i] == ctxt_pP->rntiMaybeUEid) {
      pdcp_eNB_UE_instance_to_rnti[i] = NOT_A_RNTI;
      break;
    }
  }

  for (srb_id=1; srb_id<3; srb_id++) {
    key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, srb_id, SRB_FLAG_YES);
    h_rc = hashtable_remove(pdcp_coll_p, key, pdcp_free);
  }

  for (drb_id=0; drb_id<LTE_maxDRB; drb_id++) {
    key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, drb_id, SRB_FLAG_NO);
    h_rc = hashtable_remove(pdcp_coll_p, key, pdcp_free);
  }

  (void)h_rc; /* remove gcc warning "set but not used" */

  // remove ue for pdcp enb inst
  for (i=0; i < MAX_MOBILES_PER_ENB; i++) {
    if (pdcp_enb[ctxt_pP->module_id].rnti[i] == ctxt_pP->rntiMaybeUEid) {
      LOG_I(PDCP, "remove uid is %d/%d %x\n", i,
            pdcp_enb[ctxt_pP->module_id].uid[i],
            pdcp_enb[ctxt_pP->module_id].rnti[i]);
      pdcp_enb[ctxt_pP->module_id].uid[i]=0;
      pdcp_enb[ctxt_pP->module_id].rnti[i]=0;
      pdcp_enb[ctxt_pP->module_id].num_ues--;
      break;
    }
  }

  return 1;
}


//-----------------------------------------------------------------------------
bool
rrc_pdcp_config_asn1_req(const protocol_ctxt_t *const  ctxt_pP,
                         LTE_SRB_ToAddModList_t  *const srb2add_list_pP,
                         LTE_DRB_ToAddModList_t  *const drb2add_list_pP,
                         LTE_DRB_ToReleaseList_t *const drb2release_list_pP,
                         const uint8_t                   security_modeP,
                         uint8_t                  *const kRRCenc_pP,
                         uint8_t                  *const kRRCint_pP,
                         uint8_t                  *const kUPenc_pP,
                         LTE_PMCH_InfoList_r9_t  *const pmch_InfoList_r9_pP,
                         rb_id_t                 *const defaultDRB)
//-----------------------------------------------------------------------------
{
  long int        lc_id          = 0;
  LTE_DRB_Identity_t  srb_id     = 0;
  long int        mch_id         = 0;
  rlc_mode_t      rlc_type       = RLC_MODE_NONE;
  LTE_DRB_Identity_t  drb_id     = 0;
  LTE_DRB_Identity_t *pdrb_id_p  = NULL;
  uint8_t         drb_sn         = 12;
  uint8_t         srb_sn         = 5; // fixed sn for SRBs
  uint8_t         drb_report     = 0;
  long int        cnt            = 0;
  uint16_t        header_compression_profile = 0;
  config_action_t action                     = CONFIG_ACTION_ADD;
  LTE_SRB_ToAddMod_t *srb_toaddmod_p = NULL;
  LTE_DRB_ToAddMod_t *drb_toaddmod_p = NULL;
  pdcp_t         *pdcp_p         = NULL;
  hash_key_t      key            = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t  h_rc;
  hash_key_t      key_defaultDRB = HASHTABLE_NOT_A_KEY_VALUE;
  hashtable_rc_t  h_defaultDRB_rc;
  int i,j;
  LTE_MBMS_SessionInfoList_r9_t *mbms_SessionInfoList_r9_p = NULL;
  LTE_MBMS_SessionInfo_r9_t     *MBMS_SessionInfo_p        = NULL;
  LOG_T(PDCP, PROTOCOL_CTXT_FMT" %s() SRB2ADD %p DRB2ADD %p DRB2RELEASE %p\n",
        PROTOCOL_CTXT_ARGS(ctxt_pP),
        __FUNCTION__,
        srb2add_list_pP,
        drb2add_list_pP,
        drb2release_list_pP);

  // srb2add_list does not define pdcp config, we use rlc info to setup the pdcp dcch0 and dcch1 channels

  if (srb2add_list_pP != NULL) {
    for (cnt=0; cnt<srb2add_list_pP->list.count; cnt++) {
      srb_id = srb2add_list_pP->list.array[cnt]->srb_Identity;
      srb_toaddmod_p = srb2add_list_pP->list.array[cnt];
      rlc_type = RLC_MODE_AM;
      lc_id = srb_id;
      key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, srb_id, SRB_FLAG_YES);
      h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

      if (h_rc == HASH_TABLE_OK) {
        action = CONFIG_ACTION_MODIFY;
        LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_MODIFY key 0x%"PRIx64"\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
              key);
      } else {
        action = CONFIG_ACTION_ADD;
        pdcp_p = calloc(1, sizeof(pdcp_t));
        h_rc = hashtable_insert(pdcp_coll_p, key, pdcp_p, pdcp_free);

        if (h_rc != HASH_TABLE_OK) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_ADD key 0x%"PRIx64" FAILED\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
                key);
          free(pdcp_p);
          return true;
        } else {
          LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_ADD key 0x%"PRIx64"\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
                key);
        }
      }

      if (srb_toaddmod_p->rlc_Config) {
        switch (srb_toaddmod_p->rlc_Config->present) {
          case LTE_SRB_ToAddMod__rlc_Config_PR_NOTHING:
            break;

          case LTE_SRB_ToAddMod__rlc_Config_PR_explicitValue:
            switch (srb_toaddmod_p->rlc_Config->choice.explicitValue.present) {
              case LTE_RLC_Config_PR_NOTHING:
                break;

              default:
                pdcp_config_req_asn1 (
                  ctxt_pP,
                  pdcp_p,
                  SRB_FLAG_YES,
                  rlc_type,
                  action,
                  lc_id,
                  mch_id,
                  srb_id,
                  srb_sn,
                  0, // drb_report
                  0, // header compression
                  security_modeP,
                  kRRCenc_pP,
                  kRRCint_pP,
                  kUPenc_pP);
                break;
            }

            break;

          case LTE_SRB_ToAddMod__rlc_Config_PR_defaultValue:
            pdcp_config_req_asn1 (
              ctxt_pP,
              pdcp_p,
              SRB_FLAG_YES,
              rlc_type,
              action,
              lc_id,
              mch_id,
              srb_id,
              srb_sn,
              0, // drb_report
              0, // header compression
              security_modeP,
              kRRCenc_pP,
              kRRCint_pP,
              kUPenc_pP);
            // already the default values
            break;

          default:
            DevParam(srb_toaddmod_p->rlc_Config->present, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
            break;
        }
      }
    }
  }

  // reset the action

  if (drb2add_list_pP != NULL) {
    for (cnt=0; cnt<drb2add_list_pP->list.count; cnt++) {
      drb_toaddmod_p = drb2add_list_pP->list.array[cnt];
      drb_id = drb_toaddmod_p->drb_Identity;// + drb_id_offset;

      if (drb_toaddmod_p->logicalChannelIdentity) {
        lc_id = *(drb_toaddmod_p->logicalChannelIdentity);
      } else {
        LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" logicalChannelIdentity is missing in DRB-ToAddMod information element!\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
        continue;
      }

      if (lc_id == 1 || lc_id == 2) {
        LOG_E(RLC, PROTOCOL_CTXT_FMT" logicalChannelIdentity = %ld is invalid in RRC message when adding DRB!\n", PROTOCOL_CTXT_ARGS(ctxt_pP), lc_id);
        continue;
      }

      DevCheck4(drb_id < LTE_maxDRB, drb_id, LTE_maxDRB, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
      key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, drb_id, SRB_FLAG_NO);
      h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

      if (h_rc == HASH_TABLE_OK) {
        action = CONFIG_ACTION_MODIFY;
        LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_MODIFY key 0x%"PRIx64"\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
              key);
      } else {
        action = CONFIG_ACTION_ADD;
        pdcp_p = calloc(1, sizeof(pdcp_t));
        h_rc = hashtable_insert(pdcp_coll_p, key, pdcp_p, pdcp_free);

        // save the first configured DRB-ID as the default DRB-ID
        if ((defaultDRB != NULL) && (*defaultDRB == drb_id)) {
          key_defaultDRB = PDCP_COLL_KEY_DEFAULT_DRB_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag);
          h_defaultDRB_rc = hashtable_insert(pdcp_coll_p, key_defaultDRB, pdcp_p, pdcp_free);
        } else {
          h_defaultDRB_rc = HASH_TABLE_OK; // do not trigger any error handling if this is not a default DRB
        }

        if (h_defaultDRB_rc != HASH_TABLE_OK) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_ADD ADD default DRB key 0x%"PRIx64" FAILED\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
                key_defaultDRB);
          free(pdcp_p);
          return true;
        } else if (h_rc != HASH_TABLE_OK) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_ADD ADD key 0x%"PRIx64" FAILED\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
                key);
          free(pdcp_p);
          return true;
        } else {
          LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_ADD ADD key 0x%"PRIx64"\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p),
                key);
        }
      }

      if (drb_toaddmod_p->pdcp_Config) {
        if (drb_toaddmod_p->pdcp_Config->discardTimer) {
          // set the value of the timer
        }

        if (drb_toaddmod_p->pdcp_Config->rlc_AM) {
          drb_report = drb_toaddmod_p->pdcp_Config->rlc_AM->statusReportRequired;
          drb_sn = LTE_PDCP_Config__rlc_UM__pdcp_SN_Size_len12bits; // default SN size
          rlc_type = RLC_MODE_AM;
        }

        if (drb_toaddmod_p->pdcp_Config->rlc_UM) {
          drb_sn = drb_toaddmod_p->pdcp_Config->rlc_UM->pdcp_SN_Size;
          rlc_type =RLC_MODE_UM;
        }

        switch (drb_toaddmod_p->pdcp_Config->headerCompression.present) {
          case LTE_PDCP_Config__headerCompression_PR_NOTHING:
          case LTE_PDCP_Config__headerCompression_PR_notUsed:
            header_compression_profile=0x0;
            break;

          case LTE_PDCP_Config__headerCompression_PR_rohc:

            // parse the struc and get the rohc profile
            if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0001) {
              header_compression_profile=0x0001;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0002) {
              header_compression_profile=0x0002;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0003) {
              header_compression_profile=0x0003;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0004) {
              header_compression_profile=0x0004;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0006) {
              header_compression_profile=0x0006;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0101) {
              header_compression_profile=0x0101;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0102) {
              header_compression_profile=0x0102;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0103) {
              header_compression_profile=0x0103;
            } else if(drb_toaddmod_p->pdcp_Config->headerCompression.choice.rohc.profiles.profile0x0104) {
              header_compression_profile=0x0104;
            } else {
              header_compression_profile=0x0;
              LOG_W(PDCP,"unknown header compresion profile\n");
            }

            // set the applicable profile
            break;

          default:
            LOG_W(PDCP,PROTOCOL_PDCP_CTXT_FMT"[RB %ld] unknown drb_toaddmod->PDCP_Config->headerCompression->present \n",
                  PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p), drb_id);
            break;
        }

        pdcp_config_req_asn1 (
          ctxt_pP,
          pdcp_p,
          SRB_FLAG_NO,
          rlc_type,
          action,
          lc_id,
          mch_id,
          drb_id,
          drb_sn,
          drb_report,
          header_compression_profile,
          security_modeP,
          kRRCenc_pP,
          kRRCint_pP,
          kUPenc_pP);
      }
    }
  }

  if (drb2release_list_pP != NULL) {
    for (cnt=0; cnt<drb2release_list_pP->list.count; cnt++) {
      pdrb_id_p = drb2release_list_pP->list.array[cnt];
      drb_id =  *pdrb_id_p;
      key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, drb_id, SRB_FLAG_NO);
      h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

      if (h_rc != HASH_TABLE_OK) {
        LOG_E(PDCP, PROTOCOL_CTXT_FMT" PDCP REMOVE FAILED drb_id %ld\n",
              PROTOCOL_CTXT_ARGS(ctxt_pP),
              drb_id);
        continue;
      }

      lc_id = pdcp_p->lcid;
      action = CONFIG_ACTION_REMOVE;
      pdcp_config_req_asn1 (
        ctxt_pP,
        pdcp_p,
        SRB_FLAG_NO,
        rlc_type,
        action,
        lc_id,
        mch_id,
        drb_id,
        0,
        0,
        0,
        security_modeP,
        kRRCenc_pP,
        kRRCint_pP,
        kUPenc_pP);
      h_rc = hashtable_remove(pdcp_coll_p, key, pdcp_free);

      if ((defaultDRB != NULL) && (*defaultDRB == drb_id)) {
        // default DRB being removed. nevertheless this shouldn't happen as removing default DRB is not allowed in standard
        key_defaultDRB = PDCP_COLL_KEY_DEFAULT_DRB_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag);
        h_defaultDRB_rc = hashtable_get(pdcp_coll_p, key_defaultDRB, (void **)&pdcp_p);

        if (h_defaultDRB_rc == HASH_TABLE_OK) {
          h_defaultDRB_rc = hashtable_remove(pdcp_coll_p, key_defaultDRB, pdcp_free);
        } else {
          LOG_E(PDCP, PROTOCOL_CTXT_FMT" PDCP REMOVE FAILED default DRB\n", PROTOCOL_CTXT_ARGS(ctxt_pP));
        }
      } else {
        key_defaultDRB = HASH_TABLE_OK; // do not trigger any error handling if this is not a default DRB
      }
    }
  }

  if (pmch_InfoList_r9_pP != NULL) {
    for (i=0; i<pmch_InfoList_r9_pP->list.count; i++) {
      mbms_SessionInfoList_r9_p = &(pmch_InfoList_r9_pP->list.array[i]->mbms_SessionInfoList_r9);

      for (j=0; j<mbms_SessionInfoList_r9_p->list.count; j++) {
        MBMS_SessionInfo_p = mbms_SessionInfoList_r9_p->list.array[j];

        if (0/*MBMS_SessionInfo_p->sessionId_r9*/)
          lc_id = MBMS_SessionInfo_p->sessionId_r9->buf[0];
        else
          lc_id = MBMS_SessionInfo_p->logicalChannelIdentity_r9;

        mch_id = MBMS_SessionInfo_p->tmgi_r9.serviceId_r9.buf[2]; //serviceId is 3-octet string
        //        mch_id = j;

        // can set the mch_id = i
        if (ctxt_pP->enb_flag) {
          drb_id =  (mch_id * LTE_maxSessionPerPMCH ) + lc_id ;//+ (LTE_maxDRB + 3)*MAX_MOBILES_PER_ENB; // 1

          if (pdcp_mbms_array_eNB[ctxt_pP->module_id][mch_id][lc_id].instanciated_instance == true) {
            action = CONFIG_ACTION_MBMS_MODIFY;
          } else {
            action = CONFIG_ACTION_MBMS_ADD;
          }
        } else {
          drb_id =  (mch_id * LTE_maxSessionPerPMCH ) + lc_id; // + (LTE_maxDRB + 3); // 15

          if (pdcp_mbms_array_ue[ctxt_pP->module_id][mch_id][lc_id].instanciated_instance == true) {
            action = CONFIG_ACTION_MBMS_MODIFY;
          } else {
            action = CONFIG_ACTION_MBMS_ADD;
          }
        }

        LOG_I(PDCP, "lc_id (%02ld) mch_id(%02x,%02x,%02x) drb_id(%ld) action(%d)\n",
              lc_id,
              MBMS_SessionInfo_p->tmgi_r9.serviceId_r9.buf[0],
              MBMS_SessionInfo_p->tmgi_r9.serviceId_r9.buf[1],
              MBMS_SessionInfo_p->tmgi_r9.serviceId_r9.buf[2],
              drb_id,
              action);
        pdcp_config_req_asn1 (
          ctxt_pP,
          NULL,  // unused for MBMS
          SRB_FLAG_NO,
          RLC_MODE_NONE,
          action,
          lc_id,
          mch_id,
          drb_id,
          0,   // unused for MBMS
          0,   // unused for MBMS
          0,   // unused for MBMS
          0,   // unused for MBMS
          NULL,  // unused for MBMS
          NULL,  // unused for MBMS
          NULL); // unused for MBMS
      }
    }
  }

  return 0;
}
static bool pdcp_config_req_asn1(const protocol_ctxt_t *const ctxt_pP,
                                 pdcp_t *const pdcp_pP,
                                 const srb_flag_t srb_flagP,
                                 const rlc_mode_t rlc_modeP,
                                 const config_action_t actionP,
                                 const uint16_t lc_idP,
                                 const uint16_t mch_idP,
                                 const rb_id_t rb_idP,
                                 const uint8_t rb_snP,
                                 const uint8_t rb_reportP,
                                 const uint16_t header_compression_profileP,
                                 const uint8_t security_modeP,
                                 uint8_t *const kRRCenc_pP,
                                 uint8_t *const kRRCint_pP,
                                 uint8_t *const kUPenc_pP)
//-----------------------------------------------------------------------------
{

  switch (actionP) {
    case CONFIG_ACTION_ADD:
      DevAssert(pdcp_pP != NULL);

      if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
        pdcp_pP->is_ue = false;
        pdcp_add_UE(ctxt_pP);

        // pdcp_eNB_UE_instance_to_rnti[ctxtP->module_id] = ctxt_pP->rntiMaybeUEid;
        //       pdcp_eNB_UE_instance_to_rnti[pdcp_eNB_UE_instance_to_rnti_index] = ctxt_pP->rntiMaybeUEid;
        if( srb_flagP == SRB_FLAG_NO ) {
          for(int i = 0; i<MAX_MOBILES_PER_ENB; i++) {
            if(pdcp_eNB_UE_instance_to_rnti[pdcp_eNB_UE_instance_to_rnti_index] == NOT_A_RNTI) {
              break;
            }

            pdcp_eNB_UE_instance_to_rnti_index = (pdcp_eNB_UE_instance_to_rnti_index + 1) % MAX_MOBILES_PER_ENB;
          }

          pdcp_eNB_UE_instance_to_rnti[pdcp_eNB_UE_instance_to_rnti_index] = ctxt_pP->rntiMaybeUEid;
          pdcp_eNB_UE_instance_to_rnti_index = (pdcp_eNB_UE_instance_to_rnti_index + 1) % MAX_MOBILES_PER_ENB;
        }

        //pdcp_eNB_UE_instance_to_rnti_index = (pdcp_eNB_UE_instance_to_rnti_index + 1) % MAX_MOBILES_PER_ENB;
      } else {
        pdcp_pP->is_ue = true;
        pdcp_UE_UE_module_id_to_rnti[ctxt_pP->module_id] = ctxt_pP->rntiMaybeUEid;
      }

      pdcp_pP->is_srb                     = srb_flagP == SRB_FLAG_YES;
      pdcp_pP->lcid                       = lc_idP;
      pdcp_pP->rb_id                      = rb_idP;
      pdcp_pP->header_compression_profile = header_compression_profileP;
      pdcp_pP->status_report              = rb_reportP;

      if (rb_snP == LTE_PDCP_Config__rlc_UM__pdcp_SN_Size_len12bits) {
        pdcp_pP->seq_num_size = 12;
        pdcp_pP->maximum_pdcp_rx_sn = (1 << 12) - 1;
      } else if (rb_snP == LTE_PDCP_Config__rlc_UM__pdcp_SN_Size_len7bits) {
        pdcp_pP->seq_num_size = 7;
        pdcp_pP->maximum_pdcp_rx_sn = (1 << 7) - 1;
      } else {
        pdcp_pP->seq_num_size = 5;
        pdcp_pP->maximum_pdcp_rx_sn = (1 << 5) - 1;
      }

      pdcp_pP->rlc_mode                         = rlc_modeP;
      pdcp_pP->next_pdcp_tx_sn                  = 0;
      pdcp_pP->next_pdcp_rx_sn                  = 0;
      pdcp_pP->tx_hfn                           = 0;
      pdcp_pP->rx_hfn                           = 0;
      pdcp_pP->last_submitted_pdcp_rx_sn        = 4095;
      pdcp_pP->first_missing_pdu                = -1;
      LOG_I(PDCP, PROTOCOL_PDCP_CTXT_FMT" Action ADD  LCID %d (%s id %ld) "
            "configured with SN size %d bits and RLC %s\n",
            PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_pP),
            lc_idP,
            (srb_flagP == SRB_FLAG_YES) ? "SRB" : "DRB",
            rb_idP,
            pdcp_pP->seq_num_size,
            (rlc_modeP == RLC_MODE_AM ) ? "AM" : (rlc_modeP == RLC_MODE_TM) ? "TM" : "UM");

      /* Setup security */
      if (security_modeP != 0xff) {
        pdcp_config_set_security(
          ctxt_pP,
          pdcp_pP,
          security_modeP,
          kRRCenc_pP,
          kRRCint_pP,
          kUPenc_pP);
      }

      break;

    case CONFIG_ACTION_MODIFY:
      DevAssert(pdcp_pP != NULL);
      pdcp_pP->header_compression_profile=header_compression_profileP;
      pdcp_pP->status_report = rb_reportP;
      pdcp_pP->rlc_mode = rlc_modeP;

      /* Setup security */
      if (security_modeP != 0xff) {
        pdcp_config_set_security(
          ctxt_pP,
          pdcp_pP,
          security_modeP,
          kRRCenc_pP,
          kRRCint_pP,
          kUPenc_pP);
      }

      if (rb_snP == LTE_PDCP_Config__rlc_UM__pdcp_SN_Size_len7bits) {
        pdcp_pP->seq_num_size = 7;
      } else if (rb_snP == LTE_PDCP_Config__rlc_UM__pdcp_SN_Size_len12bits) {
        pdcp_pP->seq_num_size = 12;
      } else {
        pdcp_pP->seq_num_size=5;
      }

      LOG_I(PDCP,PROTOCOL_PDCP_CTXT_FMT" Action MODIFY LCID %d "
            "RB id %ld reconfigured with SN size %d and RLC %s \n",
            PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_pP),
            lc_idP,
            rb_idP,
            rb_snP,
            (rlc_modeP == RLC_MODE_AM) ? "AM" : (rlc_modeP == RLC_MODE_TM) ? "TM" : "UM");
      break;

    case CONFIG_ACTION_REMOVE:
      DevAssert(pdcp_pP != NULL);
      //#warning "TODO pdcp_module_id_to_rnti"
      //pdcp_module_id_to_rnti[ctxt_pP.module_id ][dst_id] = NOT_A_RNTI;
      LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_REMOVE LCID %d RBID %ld configured\n",
            PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_pP),
            lc_idP,
            rb_idP);

      if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
        // pdcp_remove_UE(ctxt_pP);
      }

      memset(pdcp_pP, 0, sizeof(pdcp_t));
      break;

    case CONFIG_ACTION_MBMS_ADD:
    case CONFIG_ACTION_MBMS_MODIFY:
      LOG_D(PDCP," %s service_id/mch index %d, session_id/lcid %d, rbid %ld configured\n",
            //PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_pP),
            actionP == CONFIG_ACTION_MBMS_ADD ? "CONFIG_ACTION_MBMS_ADD" : "CONFIG_ACTION_MBMS_MODIFY",
            mch_idP,
            lc_idP,
            rb_idP);

      if (ctxt_pP->enb_flag == ENB_FLAG_YES) {
        pdcp_mbms_array_eNB[ctxt_pP->module_id][mch_idP][lc_idP].instanciated_instance = true ;
        pdcp_mbms_array_eNB[ctxt_pP->module_id][mch_idP][lc_idP].rb_id = rb_idP;
      } else {
        pdcp_mbms_array_ue[ctxt_pP->module_id][mch_idP][lc_idP].instanciated_instance = true ;
        pdcp_mbms_array_ue[ctxt_pP->module_id][mch_idP][lc_idP].rb_id = rb_idP;
      }

      break;

    case CONFIG_ACTION_SET_SECURITY_MODE:
      pdcp_config_set_security(
        ctxt_pP,
        pdcp_pP,
        security_modeP,
        kRRCenc_pP,
        kRRCint_pP,
        kUPenc_pP);
      break;

    default:
      DevParam(actionP, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
      break;
  }

  return 0;
}

//-----------------------------------------------------------------------------
void
pdcp_config_set_security(
  const protocol_ctxt_t *const  ctxt_pP,
  pdcp_t          *const pdcp_pP,
  const uint8_t         security_modeP,
  uint8_t         *const kRRCenc,
  uint8_t         *const kRRCint,
  uint8_t         *const  kUPenc)
//-----------------------------------------------------------------------------
{
  DevAssert(pdcp_pP != NULL);

  if ((security_modeP >= 0) && (security_modeP <= 0x77)) {
    pdcp_pP->cipheringAlgorithm     = security_modeP & 0x0f;
    pdcp_pP->integrityProtAlgorithm = (security_modeP>>4) & 0xf;
    LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_SET_SECURITY_MODE: cipheringAlgorithm %d integrityProtAlgorithm %d\n",
          PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_pP),
          pdcp_pP->cipheringAlgorithm,
          pdcp_pP->integrityProtAlgorithm);

    kRRCenc != NULL ? memcpy(pdcp_pP->kRRCenc, kRRCenc+16, 16) : memset(pdcp_pP->kRRCenc, 0, 16);
    kRRCint != NULL ? memcpy(pdcp_pP->kRRCint, kRRCint+16, 16) : memset(pdcp_pP->kRRCint, 0, 16);
    kUPenc != NULL ? memcpy(pdcp_pP->kUPenc, kUPenc+16, 16) : memset(pdcp_pP->kUPenc, 0, 16);

    pdcp_pP->security_container_rrc = stream_security_container_init(pdcp_pP->cipheringAlgorithm, pdcp_pP->integrityProtAlgorithm, pdcp_pP->kRRCenc, pdcp_pP->kRRCint);
    pdcp_pP->security_container_up = stream_security_container_init(pdcp_pP->cipheringAlgorithm, 0, pdcp_pP->kUPenc, NULL);

    /* Activate security */
    pdcp_pP->security_activated = 1;
  }
}

//-----------------------------------------------------------------------------
void rrc_pdcp_config_req (
  const protocol_ctxt_t *const  ctxt_pP,
  const srb_flag_t srb_flagP,
  const uint32_t actionP,
  const rb_id_t rb_idP,
  const uint8_t security_modeP)
//-----------------------------------------------------------------------------
{
  pdcp_t *pdcp_p = NULL;
  hash_key_t key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, rb_idP, srb_flagP);
  hashtable_rc_t   h_rc;
  h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

  if (h_rc == HASH_TABLE_OK) {
    /*
     * Initialize sequence number state variables of relevant PDCP entity
     */
    switch (actionP) {
      case CONFIG_ACTION_ADD:
        pdcp_p->is_srb = srb_flagP;
        pdcp_p->rb_id  = rb_idP;

        if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
          pdcp_p->is_ue = true;
          pdcp_UE_UE_module_id_to_rnti[ctxt_pP->module_id] = ctxt_pP->rntiMaybeUEid;
        } else {
          pdcp_p->is_ue = false;
        }

        pdcp_p->next_pdcp_tx_sn = 0;
        pdcp_p->next_pdcp_rx_sn = 0;
        pdcp_p->tx_hfn = 0;
        pdcp_p->rx_hfn = 0;
        /* SN of the last PDCP SDU delivered to upper layers */
        pdcp_p->last_submitted_pdcp_rx_sn = 4095;

        if (rb_idP < DTCH) { // SRB
          pdcp_p->seq_num_size = 5;
        } else { // DRB
          pdcp_p->seq_num_size = 12;
        }

        pdcp_p->first_missing_pdu = -1;
        LOG_D(PDCP,PROTOCOL_PDCP_CTXT_FMT" Config request : Action ADD:  radio bearer id %ld (already added) configured\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
              rb_idP);
        break;

      case CONFIG_ACTION_MODIFY:
        break;

      case CONFIG_ACTION_REMOVE:
        LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_REMOVE: radio bearer id %ld configured\n",
              PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
              rb_idP);
        pdcp_p->next_pdcp_tx_sn = 0;
        pdcp_p->next_pdcp_rx_sn = 0;
        pdcp_p->tx_hfn = 0;
        pdcp_p->rx_hfn = 0;
        pdcp_p->last_submitted_pdcp_rx_sn = 4095;
        pdcp_p->seq_num_size = 0;
        pdcp_p->first_missing_pdu = -1;
        stream_security_container_delete(pdcp_p->security_container_rrc);
        stream_security_container_delete(pdcp_p->security_container_up);
        pdcp_p->security_activated = 0;
        h_rc = hashtable_remove(pdcp_coll_p, key, pdcp_free);
        break;

      case CONFIG_ACTION_SET_SECURITY_MODE:
        if ((security_modeP >= 0) && (security_modeP <= 0x77)) {
          pdcp_p->cipheringAlgorithm= security_modeP & 0x0f;
          pdcp_p->integrityProtAlgorithm = (security_modeP>>4) & 0xf;
          LOG_D(PDCP, PROTOCOL_PDCP_CTXT_FMT" CONFIG_ACTION_SET_SECURITY_MODE: cipheringAlgorithm %d integrityProtAlgorithm %d\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
                pdcp_p->cipheringAlgorithm,
                pdcp_p->integrityProtAlgorithm );
        } else {
          LOG_W(PDCP,PROTOCOL_PDCP_CTXT_FMT" bad security mode %d", PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p), security_modeP);
        }

        break;

      default:
        DevParam(actionP, ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid);
        break;
    }
  } else {
    switch (actionP) {
      case CONFIG_ACTION_ADD:
        pdcp_p = calloc(1, sizeof(pdcp_t));
        h_rc = hashtable_insert(pdcp_coll_p, key, pdcp_p, pdcp_free);

        if (h_rc != HASH_TABLE_OK) {
          LOG_E(PDCP, PROTOCOL_PDCP_CTXT_FMT" PDCP ADD FAILED\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP, pdcp_p));
          free(pdcp_p);
        } else {
          pdcp_p->is_srb = srb_flagP;
          pdcp_p->rb_id  = rb_idP;

          if (ctxt_pP->enb_flag == ENB_FLAG_NO) {
            pdcp_p->is_ue = true;
            pdcp_UE_UE_module_id_to_rnti[ctxt_pP->module_id] = ctxt_pP->rntiMaybeUEid;
          } else {
            pdcp_p->is_ue = false;
          }

          pdcp_p->next_pdcp_tx_sn = 0;
          pdcp_p->next_pdcp_rx_sn = 0;
          pdcp_p->tx_hfn = 0;
          pdcp_p->rx_hfn = 0;
          /* SN of the last PDCP SDU delivered to upper layers */
          pdcp_p->last_submitted_pdcp_rx_sn = 4095;

          if (rb_idP < DTCH) { // SRB
            pdcp_p->seq_num_size = 5;
          } else { // DRB
            pdcp_p->seq_num_size = 12;
          }

          pdcp_p->first_missing_pdu = -1;
          LOG_D(PDCP,PROTOCOL_PDCP_CTXT_FMT" Inserting PDCP instance in collection key 0x%"PRIx64"\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p), key);
          LOG_D(PDCP,PROTOCOL_PDCP_CTXT_FMT" Config request : Action ADD:  radio bearer id %ld configured\n",
                PROTOCOL_PDCP_CTXT_ARGS(ctxt_pP,pdcp_p),
                rb_idP);
        }

        break;

      case CONFIG_ACTION_REMOVE:
        LOG_D(PDCP, PROTOCOL_CTXT_FMT" CONFIG_REQ PDCP CONFIG_ACTION_REMOVE PDCP instance not found\n",
              PROTOCOL_CTXT_ARGS(ctxt_pP));
        break;

      default:
        LOG_E(PDCP, PROTOCOL_CTXT_FMT" CONFIG_REQ PDCP NOT FOUND\n",
              PROTOCOL_CTXT_ARGS(ctxt_pP));
    }
  }
}

uint64_t pdcp_module_init( uint64_t pdcp_optmask, int id) {
  /* temporary enforce netlink when UE_NAS_USE_TUN is set,
     this is while switching from noS1 as build option
     to noS1 as config option                               */
  if ( pdcp_optmask & UE_NAS_USE_TUN_BIT) {
    pdcp_params.optmask = pdcp_params.optmask;
  }

  pdcp_params.optmask = pdcp_params.optmask | pdcp_optmask ;

  if (UE_NAS_USE_TUN) {
    int num_if = (NFAPI_MODE == NFAPI_UE_STUB_PNF || IS_SOFTMODEM_SIML1 || NFAPI_MODE == NFAPI_MODE_STANDALONE_PNF) ? MAX_MOBILES_PER_ENB : 1;
    int begx = (id == 0) ? 0 : id - 1;
    int endx = (id == 0) ? num_if : id;
    for (int i = begx; i < endx; i++) {
      char ifname[IFNAMSIZ];
      tun_generate_ifname(ifname, "oaitun_ue", i);
      tun_init(ifname, i);
    }
    char ifname[IFNAMSIZ];
    tun_generate_ifname(ifname, "oaitun_uem", id + 1);
    tun_init_mbms(ifname);
    tun_config(ifname, "10.0.2.2", NULL);
    LOG_I(PDCP, "UE pdcp will use tun interface\n");
  } else if (ENB_NAS_USE_TUN) {
    char ifname[IFNAMSIZ];
    tun_generate_ifname(ifname, "oaitun_enb", 0);
    tun_init(ifname, 0);
    tun_config(ifname, "10.0.1.1", NULL);
    if (pdcp_optmask & ENB_NAS_USE_TUN_W_MBMS_BIT) {
      tun_generate_ifname(ifname, "oaitun_enm", 0);
      tun_init_mbms(ifname);
      tun_config(ifname, "10.0.2.1", NULL);
      LOG_I(PDCP, "ENB pdcp will use mbms tun interface\n");
    }
    LOG_I(PDCP, "ENB pdcp will use tun interface\n");
  } else if (pdcp_optmask & ENB_NAS_USE_TUN_W_MBMS_BIT) {
    char ifname[IFNAMSIZ];
    tun_generate_ifname(ifname, "oaitun_enm", 0);
    tun_init_mbms(ifname);
    tun_config(ifname, "10.0.2.1", NULL);
    LOG_I(PDCP, "ENB pdcp will use mbms tun interface\n");
  }

  pthread_create(&pdcp_stats_thread_desc,NULL,pdcp_stats_thread,NULL);

  return pdcp_params.optmask ;
}


//-----------------------------------------------------------------------------
void
pdcp_free (
  void *pdcp_pP
)
//-----------------------------------------------------------------------------
{
  pdcp_t *pdcp_p = (pdcp_t *)pdcp_pP;

  if (pdcp_p != NULL) {
    memset(pdcp_pP, 0, sizeof(pdcp_t));
    free(pdcp_pP);
  }
}

//-----------------------------------------------------------------------------
void pdcp_module_cleanup (void)
//-----------------------------------------------------------------------------
{
  // empty - we could free all contexts, not implemented
}

//-----------------------------------------------------------------------------
void pdcp_layer_init(void)
//-----------------------------------------------------------------------------
{
  module_id_t       instance;
  int i,j;
  mbms_session_id_t session_id;
  mbms_service_id_t service_id;
  /*
   * Initialize SDU list
   */
  initNotifiedFIFO(&pdcp_sdu_list);
  pdcp_coll_p = malloc_or_fail(sizeof(*pdcp_coll_p));
  *pdcp_coll_p = hashtable_create ((LTE_maxDRB + 2) * NUMBER_OF_UE_MAX, NULL);

  for (instance = 0; instance < MAX_MOBILES_PER_ENB; instance++) {
    for (service_id = 0; service_id < LTE_maxServiceCount; service_id++) {
      for (session_id = 0; session_id < LTE_maxSessionPerPMCH; session_id++) {
        memset(&pdcp_mbms_array_ue[instance][service_id][session_id], 0, sizeof(pdcp_mbms_t));
      }
    }

    pdcp_eNB_UE_instance_to_rnti[instance] = NOT_A_RNTI;
  }

  pdcp_eNB_UE_instance_to_rnti_index = 0;

  for (instance = 0; instance < NUMBER_OF_eNB_MAX; instance++) {
    for (service_id = 0; service_id < LTE_maxServiceCount; service_id++) {
      for (session_id = 0; session_id < LTE_maxSessionPerPMCH; session_id++) {
        memset(&pdcp_mbms_array_eNB[instance][service_id][session_id], 0, sizeof(pdcp_mbms_t));
      }
    }
  }

#ifdef MBMS_MULTICAST_OUT
  mbms_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

  if (mbms_socket == -1)
    LOG_W(PDCP, "Could not create RAW socket, MBMS packets will not be put to the network\n");

#endif
  LOG_I(PDCP, "PDCP layer has been initialized\n");
  pdcp_output_sdu_bytes_to_write=0;
  pdcp_output_header_bytes_to_write=0;
  pdcp_input_sdu_remaining_size_to_read=0;
  memset(pdcp_enb, 0, sizeof(pdcp_enb_t));
  memset(Pdcp_stats_tx_window_ms, 0, sizeof(Pdcp_stats_tx_window_ms));
  memset(Pdcp_stats_rx_window_ms, 0, sizeof(Pdcp_stats_rx_window_ms));

  for (i = 0; i < MAX_eNB; i++) {
    for (j = 0; j < MAX_MOBILES_PER_ENB; j++) {
      Pdcp_stats_tx_window_ms[i][j]=100;
      Pdcp_stats_rx_window_ms[i][j]=100;
    }
  }

  memset(Pdcp_stats_tx, 0, sizeof(Pdcp_stats_tx));
  memset(Pdcp_stats_tx_w, 0, sizeof(Pdcp_stats_tx_w));
  memset(Pdcp_stats_tx_tmp_w, 0, sizeof(Pdcp_stats_tx_tmp_w));
  memset(Pdcp_stats_tx_bytes, 0, sizeof(Pdcp_stats_tx_bytes));
  memset(Pdcp_stats_tx_bytes_w, 0, sizeof(Pdcp_stats_tx_bytes_w));
  memset(Pdcp_stats_tx_bytes_tmp_w, 0, sizeof(Pdcp_stats_tx_bytes_tmp_w));
  memset(Pdcp_stats_tx_sn, 0, sizeof(Pdcp_stats_tx_sn));
  memset(Pdcp_stats_tx_throughput_w, 0, sizeof(Pdcp_stats_tx_throughput_w));
  memset(Pdcp_stats_tx_aiat, 0, sizeof(Pdcp_stats_tx_aiat));
  memset(Pdcp_stats_tx_iat, 0, sizeof(Pdcp_stats_tx_iat));
  memset(Pdcp_stats_rx, 0, sizeof(Pdcp_stats_rx));
  memset(Pdcp_stats_rx_w, 0, sizeof(Pdcp_stats_rx_w));
  memset(Pdcp_stats_rx_tmp_w, 0, sizeof(Pdcp_stats_rx_tmp_w));
  memset(Pdcp_stats_rx_bytes, 0, sizeof(Pdcp_stats_rx_bytes));
  memset(Pdcp_stats_rx_bytes_w, 0, sizeof(Pdcp_stats_rx_bytes_w));
  memset(Pdcp_stats_rx_bytes_tmp_w, 0, sizeof(Pdcp_stats_rx_bytes_tmp_w));
  memset(Pdcp_stats_rx_sn, 0, sizeof(Pdcp_stats_rx_sn));
  memset(Pdcp_stats_rx_goodput_w, 0, sizeof(Pdcp_stats_rx_goodput_w));
  memset(Pdcp_stats_rx_aiat, 0, sizeof(Pdcp_stats_rx_aiat));
  memset(Pdcp_stats_rx_iat, 0, sizeof(Pdcp_stats_rx_iat));
  memset(Pdcp_stats_rx_outoforder, 0, sizeof(Pdcp_stats_rx_outoforder));
}

//-----------------------------------------------------------------------------
void pdcp_layer_cleanup (void)
//-----------------------------------------------------------------------------
{
  //list_free (&pdcp_sdu_list);
  while(pollNotifiedFIFO(&pdcp_sdu_list)) {};
  hashtable_destroy(pdcp_coll_p, pdcp_free);
  free(pdcp_coll_p);
#ifdef MBMS_MULTICAST_OUT

  if(mbms_socket != -1) {
    close(mbms_socket);
    mbms_socket = -1;
  }

#endif
}
