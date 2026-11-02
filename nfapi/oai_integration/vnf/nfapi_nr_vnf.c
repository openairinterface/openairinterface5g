/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>

#include "nfapi_nr_vnf.h"
#include "vnf_nr.h"
#include "vnf_p7_nr.h"

#include "nfapi.h"
#include "nfapi/oai_integration/vendor_ext.h"

#include "openair1/PHY/defs_gNB.h"
#include "common/ran_context.h"
#include "gnb_ind_vars.h"
#include "nr_fapi_p7_utils.h"
#include "nr_fapi_p5_utils.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"

#ifdef ENABLE_AERIAL
#include "aerial/fapi_nvIPC.h"
#include "aerial/fapi_vnf_p7.h"
#include "nr_fapi_p5.h"
#include "nr_fapi_p7.h"
#endif

#ifdef ENABLE_WLS
#include <wls_integration/include/wls_vnf.h>
#include "nr_fapi_p5.h"
#include "nr_fapi_p7.h"
#endif

#ifdef ENABLE_SOCKET
#include <socket/include/socket_vnf.h>
static pthread_t vnf_p7_start_pthread;
#endif
static nfapi_nr_vnf_config_t *config;
extern RAN_CONTEXT_t RC;

#ifndef ENABLE_AERIAL
static pthread_t vnf_p5_init_and_receive_pthread;
#endif

nfapi_nr_vnf_config_t * get_nr_config()
{
  return config;
}

void set_config(nfapi_nr_vnf_config_t *cfg)
{
  config = cfg;
}

vnf_p7_t *get_p7_nr_vnf()
{
  vnf_info *vnf = config->user_data;
  return (vnf_p7_t *)vnf->p7_vnfs->config;
}

nfapi_vnf_p7_config_t *get_p7_nr_vnf_config()
{
  return &get_p7_nr_vnf()->_public;
}

int vnf_nr_pack_vendor_extension_tlv(void *ve, uint8_t **ppWritePackedMsg, uint8_t *end, nfapi_p4_p5_codec_config_t *codec)
{
  UNUSED(codec);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "vnf_pack_vendor_extension_tlv\n");
  nfapi_tl_t *tlv = (nfapi_tl_t *)ve;

  switch (tlv->tag) {
    case VENDOR_EXT_TLV_2_TAG: {
      // NFAPI_TRACE(NFAPI_TRACE_INFO, "Packing VENDOR_EXT_TLV_2\n");
      vendor_ext_tlv_2 *ve = (vendor_ext_tlv_2 *)tlv;

      if (!push32(ve->dummy, ppWritePackedMsg, end))
        return 0;

      return 1;
    } break;
  }

  return -1;
}

int vnf_nr_unpack_vendor_extension_tlv(nfapi_tl_t *tl,
                                       uint8_t **ppReadPackedMessage,
                                       uint8_t *end,
                                       void **ve,
                                       nfapi_p4_p5_codec_config_t *codec)
{
  UNUSED(tl);
  UNUSED(ppReadPackedMessage);
  UNUSED(end);
  UNUSED(ve);
  UNUSED(codec);
  return -1;
}

int pnf_nr_connection_indication_cb(nfapi_nr_vnf_config_t *config, int p5_idx) {
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] pnf connection indication idx:%d\n", p5_idx);
  nfapi_nr_pnf_param_request_t req;
  memset(&req, 0, sizeof(req));
  req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_REQUEST;
  nfapi_nr_vnf_pnf_param_req(config, p5_idx, &req);
  return 0;
}

int pnf_nr_disconnection_indication_cb(nfapi_nr_vnf_config_t *config, int p5_idx) {
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] pnf disconnection indication idx:%d\n", p5_idx);
  vnf_info *vnf = (vnf_info *)(config->user_data);
  pnf_info *pnf = vnf->pnfs;
  phy_info *phy = pnf->phys;
  vnf_p7_info *p7_vnf = vnf->p7_vnfs;
  nfapi_vnf_p7_del_pnf((p7_vnf->config), phy->id);
  return 0;
}

int pnf_nr_param_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_pnf_param_response_t *resp) {
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] pnf param response idx:%d error:%d\n", p5_idx, resp->error_code);
  vnf_info *vnf = (vnf_info *)(config->user_data);
  pnf_info *pnf = vnf->pnfs;

  for(int i = 0; i < resp->pnf_phy.number_of_phys; ++i) {
    phy_info phy;
    memset(&phy,0,sizeof(phy));
    phy.index = resp->pnf_phy.phy[i].phy_config_index;
    NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] (PHY:%d) phy_config_idx:%d\n", i, resp->pnf_phy.phy[i].phy_config_index);
    nfapi_nr_vnf_allocate_phy(config, p5_idx, &(phy.id));

    for(int j = 0; j < resp->pnf_phy.phy[i].number_of_rfs; ++j) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] (PHY:%d) (RF%d) %d\n", i, j, resp->pnf_phy.phy[i].rf_config[j].rf_config_index);
      phy.rfs[0] = resp->pnf_phy.phy[i].rf_config[j].rf_config_index;
    }

    pnf->phys[0] = phy;
  }
  nfapi_nr_pnf_config_request_t req;
  memset(&req, 0, sizeof(req));
  req.header.message_id = NFAPI_PNF_CONFIG_REQUEST;
  req.pnf_phy_rf_config.tl.tag = NFAPI_PNF_PHY_RF_TAG;
  req.pnf_phy_rf_config.number_phy_rf_config_info = 2; // pnf.phys.size();
  NFAPI_TRACE(NFAPI_TRACE_INFO, "Hard coded num phy rf to 2\n");

  for(unsigned i = 0; i < 2; ++i) {
    req.pnf_phy_rf_config.phy_rf_config[i].phy_id = pnf->phys[i].id;
    req.pnf_phy_rf_config.phy_rf_config[i].phy_config_index = pnf->phys[i].index;
    req.pnf_phy_rf_config.phy_rf_config[i].rf_config_index = pnf->phys[i].rfs[0];
  }

  nfapi_nr_vnf_pnf_config_req(config, p5_idx, &req);
  return 0;
}

int pnf_nr_config_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_pnf_config_response_t *resp) {
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] pnf config response idx:%d resp[header[phy_id:%u message_id:%02x message_length:%u]]\n", p5_idx, resp->header.phy_id, resp->header.message_id, resp->header.message_length);

  nfapi_nr_pnf_start_request_t req;
  memset(&req, 0, sizeof(req));
  req.header.phy_id = resp->header.phy_id;
  req.header.message_id = NFAPI_PNF_START_REQUEST;
  nfapi_nr_vnf_pnf_start_req(config, p5_idx, &req);

  return 0;
}

int phy_nr_rach_indication(nfapi_nr_rach_indication_t *ind)
{
  if (NFAPI_MODE == NFAPI_MODE_VNF || NFAPI_MODE == NFAPI_MODE_AERIAL) {
    nfapi_nr_rach_indication_t *rach_ind = CALLOC(1, sizeof(*rach_ind));
    copy_rach_indication(ind, rach_ind);
    if (!put_queue(&gnb_rach_ind_queue, rach_ind)) {
      LOG_E(NR_MAC, "Put_queue failed for rach_ind\n");
      free_rach_indication(rach_ind);
      free(rach_ind);
    }
  } else {
    LOG_E(NR_MAC, "NFAPI_MODE = %d not NFAPI_MODE_VNF(2)\n", nfapi_getmode());
  }
  return 1;
}

int phy_nr_uci_indication(nfapi_nr_uci_indication_t *ind)
{
  LOG_D(NR_MAC, "In %s() NFAPI SFN/SF: %d/%d number_of_pdus :%u\n", __FUNCTION__, ind->sfn, ind->slot, ind->num_ucis);
  if (NFAPI_MODE == NFAPI_MODE_VNF || NFAPI_MODE == NFAPI_MODE_AERIAL) {
    nfapi_nr_uci_indication_t *uci_ind = CALLOC(1, sizeof(*uci_ind));
    AssertFatal(uci_ind, "Memory not allocated for uci_ind in phy_nr_uci_indication.");
    copy_uci_indication(ind, uci_ind);
    if (!put_queue(&gnb_uci_ind_queue, uci_ind)) {
      LOG_E(NR_MAC, "Put_queue failed for uci_ind\n");
      free_uci_indication(uci_ind);
      free(uci_ind);
      uci_ind = NULL;
    }
  } else {
    LOG_E(NR_MAC, "NFAPI_MODE = %d not NFAPI_MODE_VNF(2)\n", nfapi_getmode());
  }
  return 1;
}

int phy_nr_crc_indication(nfapi_nr_crc_indication_t *ind)
{
  LOG_D(NR_MAC, "In %s() NFAPI SFN/SF: %d/%d number_of_pdus :%u\n", __FUNCTION__, ind->sfn, ind->slot, ind->number_crcs);

  if (NFAPI_MODE == NFAPI_MODE_VNF || NFAPI_MODE == NFAPI_MODE_AERIAL) {
    nfapi_nr_crc_indication_t *crc_ind = CALLOC(1, sizeof(*crc_ind));
    copy_crc_indication(ind, crc_ind);
    if (!put_queue(&gnb_crc_ind_queue, crc_ind)) {
      LOG_E(NR_MAC, "Put_queue failed for crc_ind\n");
      free_crc_indication(crc_ind);
      free(crc_ind);
    }
  } else {
    LOG_E(NR_MAC, "NFAPI_MODE = %d not NFAPI_MODE_VNF(2)\n", nfapi_getmode());
  }
  return 1;
}

int phy_nr_rx_data_indication(nfapi_nr_rx_data_indication_t *ind)
{
  LOG_D(NR_MAC,
        "In %s() NFAPI SFN/SF: %d/%d number_of_pdus :%u, and pdu %p\n",
        __FUNCTION__,
        ind->sfn,
        ind->slot,
        ind->number_of_pdus,
        ind->pdu_list[0].pdu);

  if (NFAPI_MODE == NFAPI_MODE_VNF || NFAPI_MODE == NFAPI_MODE_AERIAL) {
    nfapi_nr_rx_data_indication_t *rx_ind = CALLOC(1, sizeof(*rx_ind));
    copy_rx_data_indication(ind, rx_ind);
    if (!put_queue(&gnb_rx_ind_queue, rx_ind)) {
      LOG_E(NR_MAC, "Put_queue failed for rx_ind\n");
      free_rx_data_indication(rx_ind);
      free(rx_ind);
    }
  } else {
    LOG_E(NR_MAC, "NFAPI_MODE = %d not NFAPI_MODE_VNF(2)\n", nfapi_getmode());
  }
  return 1;
}

//NR phy indication

int oai_nfapi_dl_tti_req(nfapi_nr_dl_tti_request_t *dl_config_req);
int oai_nfapi_ul_tti_req(nfapi_nr_ul_tti_request_t *ul_tti_req);
int oai_nfapi_tx_data_req(nfapi_nr_tx_data_request_t* tx_data_req);
int oai_nfapi_ul_dci_req(nfapi_nr_ul_dci_request_t* ul_dci_req);

int phy_nr_slot_indication(nfapi_nr_slot_indication_scf_t *ind)
{
  LOG_D(MAC, "VNF SFN/Slot %d.%d \n", ind->sfn, ind->slot);

  // this variable is very big (multiple MB), so we put it into static storage
  // to not overflow the stack while still having it in local (function) scope
  // also, phy_nr_slot_indication() is only executed by one thread, serially
  static NR_Sched_Rsp_t sched_response;
  NR_IF_Module_t *ifi = RC.nrmac[0]->if_inst;
  ifi->NR_slot_indication(ind, &sched_response);

    // The scheduler does not stamp the cell identity onto the requests it produces,
    // so carry it over from the indication that triggered this slot. Once the MAC
    // sets it in reset_sched_response(), these four assignments can simply go away
    // and the senders keep reading it off the message.
    const uint8_t PHY_id = ind->header.phy_id;
    sched_response.DL_req.header.phy_id = PHY_id;
    sched_response.UL_tti_req.header.phy_id = PHY_id;
    sched_response.TX_req.header.phy_id = PHY_id;
    sched_response.UL_dci_req.header.phy_id = PHY_id;
#ifdef ENABLE_AERIAL

    bool send_slt_resp = false;
    if (sched_response.DL_req.dl_tti_request_body.nPDUs> 0) {
      oai_fapi_dl_tti_req(&sched_response.DL_req);
      send_slt_resp = true;
    }
    if (sched_response.UL_tti_req.n_pdus > 0) {
      oai_fapi_ul_tti_req(&sched_response.UL_tti_req);
      send_slt_resp = true;
    }
    if (sched_response.TX_req.Number_of_PDUs > 0) {
      oai_fapi_tx_data_req(&sched_response.TX_req);
      send_slt_resp = true;
    }
    if (sched_response.UL_dci_req.numPdus > 0) {
      oai_fapi_ul_dci_req(&sched_response.UL_dci_req);
      send_slt_resp = true;
    }
    if (send_slt_resp) {
      oai_fapi_send_end_request(ind->sfn, ind->slot, PHY_id);
    }
#else
  if (sched_response.DL_req.dl_tti_request_body.nPDUs > 0)
    oai_nfapi_dl_tti_req(&sched_response.DL_req);

  if (sched_response.UL_tti_req.n_pdus > 0)
    oai_nfapi_ul_tti_req(&sched_response.UL_tti_req);

  if (sched_response.TX_req.Number_of_PDUs > 0)
    oai_nfapi_tx_data_req(&sched_response.TX_req);

  if (sched_response.UL_dci_req.numPdus > 0)
    oai_nfapi_ul_dci_req(&sched_response.UL_dci_req);
#endif

  /* the below works because the function behind the callback collects
   * messages from queue into which messages have been copied.
   * TODO we should have different callbacks for received messages and call
   * into the scheduler separately for each message instead of one big one. */
#define INIT_UL_IND(X, FIELD, SFN, SLOT) \
    do {                                 \
      (X).sfn = SFN;                     \
      (X).slot = SLOT;                   \
      (X).FIELD = 0;                     \
    } while (0)
  // Intentionally not zero-initialized: each indication fills only its used PDUs.
  NR_UL_IND_t ul_ind;
  ul_ind.frame = ind->sfn;
  ul_ind.slot = ind->slot;
  ul_ind.phy_id = ind->header.phy_id;
  ul_ind.module_id = 0;
  INIT_UL_IND(ul_ind.crc_ind, number_crcs, ind->sfn, ind->slot);
  INIT_UL_IND(ul_ind.rx_ind, number_of_pdus, ind->sfn, ind->slot);
  INIT_UL_IND(ul_ind.rach_ind, number_of_pdus, ind->sfn, ind->slot);
  INIT_UL_IND(ul_ind.srs_ind, number_of_pdus, ind->sfn, ind->slot);
  INIT_UL_IND(ul_ind.uci_ind, num_ucis, ind->sfn, ind->slot);
  INIT_UL_IND(ul_ind.srs_toa_vendor_ext_ind, num_ta, ind->sfn, ind->slot);

  // the callback is picking up messages from queues
  ifi->NR_UL_indication(&ul_ind);

  return 1;
}

int phy_nr_srs_indication(nfapi_nr_srs_indication_t *ind)
{
  for (int i = 0; i < ind->number_of_pdus; ++i)
    handle_nr_srs_measurements(0, ind->header.phy_id, ind->sfn, ind->slot, &ind->pdu_list[i]);
  return 1;
}

int phy_nr_srs_toa_vendor_ext_indication(nfapi_nr_srs_toa_vendor_ext_indication_t *ind)
{
  handle_nr_srs_toa_vendor_ext_measurements(0, ind->sfn, ind->slot, ind->num_ta, ind->ta_offset_nsec, ind->rnti);
  return 1;
}
//end NR phy indication

void *vnf_nr_allocate(size_t size)
{
  // return (void*)memory_pool::allocate(size);
  return (void *)malloc(size);
}

void vnf_nr_deallocate(void *ptr)
{
  // memory_pool::deallocate((uint8_t*)ptr);
  free(ptr);
}

int phy_nr_vendor_ext(struct nfapi_vnf_p7_config *config, void *msg)
{
  UNUSED(config);
  if (((nfapi_nr_p7_message_header_t *)msg)->message_id == P7_VENDOR_EXT_IND) {
    // vendor_ext_p7_ind* ind = (vendor_ext_p7_ind*)msg;
    // NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] vendor_ext (error_code:%d)\n", ind->error_code);
  } else {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] unknown %02x\n", ((nfapi_nr_p7_message_header_t *)msg)->message_id);
  }

  return 0;
}

void *phy_nr_allocate_p7_vendor_ext(uint16_t message_id, uint16_t *msg_size)
{
  if (message_id == P7_VENDOR_EXT_IND) {
    *msg_size = sizeof(vendor_ext_p7_ind);
    return (nfapi_p7_message_header_t *)malloc(sizeof(vendor_ext_p7_ind));
  }

  return 0;
}

void phy_nr_deallocate_p7_vendor_ext(void *header)
{
  free(header);
}

int phy_nr_unpack_vendor_extension_tlv(nfapi_tl_t *tl,
                                       uint8_t **ppReadPackedMessage,
                                       uint8_t *end,
                                       void **ve,
                                       nfapi_p7_codec_config_t *codec)
{
  UNUSED(tl);
  UNUSED(ppReadPackedMessage);
  UNUSED(end);
  UNUSED(ve);
  UNUSED(codec);
  return -1;
}

int phy_nr_pack_vendor_extension_tlv(void *ve, uint8_t **ppWritePackedMsg, uint8_t *end, nfapi_p7_codec_config_t *codec)
{
  UNUSED(codec);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "phy_pack_vendor_extension_tlv\n");
  nfapi_tl_t *tlv = (nfapi_tl_t *)ve;

  switch (tlv->tag) {
    case VENDOR_EXT_TLV_1_TAG: {
      // NFAPI_TRACE(NFAPI_TRACE_INFO, "Packing VENDOR_EXT_TLV_1\n");
      vendor_ext_tlv_1 *ve = (vendor_ext_tlv_1 *)tlv;

      if (!push32(ve->dummy, ppWritePackedMsg, end))
        return 0;

      return 1;
    } break;

    default:
      return -1;
      break;
  }
}

int phy_nr_unpack_p7_vendor_extension(void *header, uint8_t **ppReadPackedMessage, uint8_t *end, nfapi_p7_codec_config_t *config)
{
  UNUSED(config);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);
  if (((nfapi_nr_p7_message_header_t *)header)->message_id == P7_VENDOR_EXT_IND) {
    vendor_ext_p7_ind *req = (vendor_ext_p7_ind *)(header);

    if (!pull16(ppReadPackedMessage, &req->error_code, end))
      return 0;
  }

  return 1;
}

int phy_nr_pack_p7_vendor_extension(void *header, uint8_t **ppWritePackedMsg, uint8_t *end, nfapi_p7_codec_config_t *config)
{
  UNUSED(config);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);
  if (((nfapi_nr_p7_message_header_t *)header)->message_id == P7_VENDOR_EXT_REQ) {
    // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);
    vendor_ext_p7_req *req = (vendor_ext_p7_req *)(header);

    if (!(push16(req->dummy1, ppWritePackedMsg, end) && push16(req->dummy2, ppWritePackedMsg, end)))
      return 0;
  }

  return 1;
}

int vnf_nr_pack_p4_p5_vendor_extension(void *header, uint8_t **ppWritePackedMsg, uint8_t *end, nfapi_p4_p5_codec_config_t *codec)
{
  UNUSED(codec);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);
  if (((nfapi_nr_p4_p5_message_header_t *)header)->message_id == P5_VENDOR_EXT_REQ) {
    vendor_ext_p5_req *req = (vendor_ext_p5_req *)(header);
    // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s %d %d\n", __FUNCTION__, req->dummy1, req->dummy2);
    return (!(push16(req->dummy1, ppWritePackedMsg, end) && push16(req->dummy2, ppWritePackedMsg, end)));
  }

  return 0;
}

void *configure_nr_p7_vnf(void *ptr)
{
  init_queue(&gnb_rach_ind_queue);
  init_queue(&gnb_rx_ind_queue);
  init_queue(&gnb_crc_ind_queue);
  init_queue(&gnb_uci_ind_queue);

  vnf_p7_info *p7_vnf = (vnf_p7_info *)ptr;
  p7_vnf->config->port = p7_vnf->local_port;
  p7_vnf->config->nr_crc_indication = &phy_nr_crc_indication;
  p7_vnf->config->nr_rx_data_indication = &phy_nr_rx_data_indication;
  p7_vnf->config->nr_rach_indication = &phy_nr_rach_indication;
  p7_vnf->config->nr_uci_indication = &phy_nr_uci_indication;
  p7_vnf->config->nr_slot_indication = &phy_nr_slot_indication;
  p7_vnf->config->nr_srs_indication = &phy_nr_srs_indication;
  p7_vnf->config->nr_srs_toa_vendor_ext_indication = &phy_nr_srs_toa_vendor_ext_indication;
  p7_vnf->config->malloc = &vnf_nr_allocate;
  p7_vnf->config->free = &vnf_nr_deallocate;
  p7_vnf->config->vendor_ext = &phy_nr_vendor_ext;
  p7_vnf->config->user_data = p7_vnf;
  p7_vnf->mac->user_data = p7_vnf;
  p7_vnf->config->codec_config.unpack_p7_vendor_extension = &phy_nr_unpack_p7_vendor_extension;
  p7_vnf->config->codec_config.pack_p7_vendor_extension = &phy_nr_pack_p7_vendor_extension;
  p7_vnf->config->codec_config.unpack_vendor_extension_tlv = &phy_nr_unpack_vendor_extension_tlv;
  p7_vnf->config->codec_config.pack_vendor_extension_tlv = &phy_nr_pack_vendor_extension_tlv;
  p7_vnf->config->codec_config.allocate = &vnf_nr_allocate;
  p7_vnf->config->codec_config.deallocate = &vnf_nr_deallocate;
  p7_vnf->config->allocate_p7_vendor_ext = &phy_nr_allocate_p7_vendor_ext;
  p7_vnf->config->deallocate_p7_vendor_ext = &phy_nr_deallocate_p7_vendor_ext;

#ifdef ENABLE_WLS
  p7_vnf->config->unpack_func = &fapi_nr_p7_message_unpack;
  p7_vnf->config->hdr_unpack_func = &fapi_nr_p7_message_header_unpack;
  p7_vnf->config->pack_func = &fapi_nr_p7_message_pack;
  p7_vnf->config->send_p7_msg = &wls_vnf_nr_send_p7_message;
#endif

#ifdef ENABLE_SOCKET
  p7_vnf->config->unpack_func = &nfapi_nr_p7_message_unpack;
  p7_vnf->config->hdr_unpack_func = &nfapi_nr_p7_message_header_unpack;
  p7_vnf->config->pack_func = &nfapi_nr_p7_message_pack;
  p7_vnf->config->send_p7_msg = &vnf_nr_send_p7_msg;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Creating VNF NFAPI P7 start thread %s\n", __FUNCTION__);
  threadCreate(&vnf_p7_start_pthread, &vnf_nr_start_p7_thread, p7_vnf->config, "vnf_p7_thread", -1, OAI_PRIORITY_RT_MAX);
#endif

#ifdef ENABLE_AERIAL
  p7_vnf->config->unpack_func = &fapi_nr_p7_message_unpack;
  p7_vnf->config->hdr_unpack_func = &fapi_nr_p7_message_header_unpack;
  p7_vnf->config->pack_func = &fapi_nr_p7_message_pack;
  p7_vnf->config->send_p7_msg = &aerial_nr_send_p7_message;
#endif
  return 0;
}

int pnf_nr_start_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_pnf_start_response_t *resp) {
  UNUSED(resp);
  vnf_info *vnf = (vnf_info *)(config->user_data);
  vnf_p7_info *p7_vnf = vnf->p7_vnfs;
  pnf_info *pnf = vnf->pnfs;
  nfapi_nr_param_request_scf_t req;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] pnf start response idx:%d config:%p user_data:%p p7_vnf[config:%p thread_started:%d]\n", p5_idx, config, config->user_data, vnf->p7_vnfs[0].config, vnf->p7_vnfs[0].thread_started);

  if(p7_vnf->thread_started == 0) {
    pthread_t vnf_p7_thread;
    threadCreate(&vnf_p7_thread, &configure_nr_p7_vnf, p7_vnf, "vnf_p7_thread", -1, OAI_PRIORITY_RT_MAX);
    p7_vnf->thread_started = 1;
  } else {
    // P7 thread already running.
  }

  // start all the phys in the pnf.
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Sending NFAPI_VNF_PARAM_REQUEST phy_id:%d\n", pnf->phys[0].id);
  memset(&req, 0, sizeof(req));
  req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST;
#ifdef ENABLE_WLS
  req.header.phy_id = 0;
#endif
#ifdef ENABLE_SOCKET
  req.header.phy_id = pnf->phys[0].id;
#endif
  nfapi_nr_vnf_param_req(config, p5_idx, &req);
  return 0;
}

int nr_param_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_param_response_scf_t *resp) {

  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Received NFAPI_PARAM_RESP idx:%d phy_id:%d\n", p5_idx, resp->header.phy_id);
  vnf_info *vnf = (vnf_info *)(config->user_data);
  vnf_p7_info *p7_vnf = vnf->p7_vnfs;
  pnf_info *pnf = vnf->pnfs;
  phy_info *phy = pnf->phys;
#ifdef ENABLE_AERIAL
  // Aerial has no P5 handshake, so no phy_id was ever allocated through
  // nfapi_vnf_allocate_phy(): take the (0-based) cell id the message carries.
  // On the native path phy->id already holds the id allocated in
  // pnf_nr_param_resp_cb() and registered in config->phy_list; overwriting it
  // here would break the nfapi_vnf_phy_info_list_find() lookup done by
  // nfapi_nr_vnf_config_req().
  phy->id = resp->header.phy_id;
#endif
  // NB: indexed by p5_idx, not resp->header.phy_id. Both are the 0-based cell id on
  // Aerial, but the native nFAPI path is still 1-based (nfapi_vnf_allocate_phy() starts
  // at 1), and config[] has NFAPI_CC_MAX == 1 entries. Switch to the message's phy_id
  // once the native path is converted to 0-based indexing.
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(RC.nrmac[0], resp->header.phy_id);
  nfapi_nr_config_request_scf_t *req = &cell->config; // check
#ifndef ENABLE_AERIAL
  struct sockaddr_in pnf_p7_sockaddr;
  phy->remote_port = resp->nfapi_config.p7_pnf_port.value;
  //phy->remote_port = 32123;//resp->nfapi_config.p7_pnf_port.value;
  memcpy(&pnf_p7_sockaddr.sin_addr.s_addr, &(resp->nfapi_config.p7_pnf_address_ipv4.address[0]), 4);
  phy->remote_addr = inet_ntoa(pnf_p7_sockaddr.sin_addr);
  // for now just 1
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] %d.%d pnf p7 %s:%d timing %u %u %u %u\n", p5_idx, phy->id, phy->remote_addr, phy->remote_port, p7_vnf->timing_window, p7_vnf->periodic_timing_period, p7_vnf->aperiodic_timing_enabled,
         p7_vnf->periodic_timing_period);
#endif
  // Hack? the VNF might need the subcarrier spacing for some calculations
  // (that we actually don't use as of now...). We therefore need to save the
  // mu, for the current PNF connection (together with where we have frame/slot
  // info). nfapi_vnf_p7_add_pnf() prepends the current P7 connection to the
  // beginning of the list. Pick it from there, and save the mu.
  // check that SCS has actually been set
  const nfapi_uint8_tlv_t *scs = &req->ssb_config.scs_common;
  DevAssert(scs->tl.tag == NFAPI_NR_CONFIG_SCS_COMMON_TAG);
  int mu = scs->value;
  nfapi_vnf_p7_add_pnf((p7_vnf->config), phy->remote_addr, phy->remote_port, phy->id, mu);

  req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_CONFIG_REQUEST;
  req->header.phy_id = phy->id;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Send NFAPI_CONFIG_REQUEST\n");
  //NFAPI_TRACE(NFAPI_TRACE_INFO, "\n NR bandP =%d\n",req->nfapi_config.rf_bands.rf_band[0]);
#ifndef ENABLE_AERIAL
  req->nfapi_config.p7_vnf_port.tl.tag = NFAPI_NR_NFAPI_P7_VNF_PORT_TAG;
  req->nfapi_config.p7_vnf_port.value = p7_vnf->local_port;
  req->num_tlv++;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Local_port:%d\n", ntohs(p7_vnf->local_port));
  req->nfapi_config.p7_vnf_address_ipv4.tl.tag = NFAPI_NR_NFAPI_P7_VNF_ADDRESS_IPV4_TAG;
  struct sockaddr_in vnf_p7_sockaddr;
  vnf_p7_sockaddr.sin_addr.s_addr = inet_addr(p7_vnf->local_addr);
  memcpy(&(req->nfapi_config.p7_vnf_address_ipv4.address[0]), &vnf_p7_sockaddr.sin_addr.s_addr, 4);
  req->num_tlv++;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Local_addr:%s\n", p7_vnf->local_addr);
  req->nfapi_config.timing_window.tl.tag = NFAPI_NR_NFAPI_TIMING_WINDOW_TAG;
  req->nfapi_config.timing_window.value = p7_vnf->timing_window;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "\n[VNF]Timing window tag : %d Timing window:%u\n",NFAPI_NR_NFAPI_TIMING_WINDOW_TAG, p7_vnf->timing_window);
  req->num_tlv++;

  if(p7_vnf->periodic_timing_enabled || p7_vnf->aperiodic_timing_enabled) {
    req->nfapi_config.timing_info_mode.tl.tag = NFAPI_NR_NFAPI_TIMING_INFO_MODE_TAG;
    req->nfapi_config.timing_info_mode.value = (p7_vnf->aperiodic_timing_enabled << 1) | (p7_vnf->periodic_timing_enabled);
    req->num_tlv++;

    if(p7_vnf->periodic_timing_enabled) {
      req->nfapi_config.timing_info_period.tl.tag = NFAPI_NR_NFAPI_TIMING_INFO_PERIOD_TAG;
      req->nfapi_config.timing_info_period.value = p7_vnf->periodic_timing_period;
      req->num_tlv++;
    }
  }
//TODO: Assign tag and value for P7 message offsets
req->nfapi_config.dl_tti_timing_offset.tl.tag = NFAPI_NR_NFAPI_DL_TTI_TIMING_OFFSET;
req->nfapi_config.ul_tti_timing_offset.tl.tag = NFAPI_NR_NFAPI_UL_TTI_TIMING_OFFSET;
req->nfapi_config.ul_dci_timing_offset.tl.tag = NFAPI_NR_NFAPI_UL_DCI_TIMING_OFFSET;
req->nfapi_config.tx_data_timing_offset.tl.tag = NFAPI_NR_NFAPI_TX_DATA_TIMING_OFFSET;

  vendor_ext_tlv_2 ve2;
  memset(&ve2, 0, sizeof(ve2));
  ve2.tl.tag = VENDOR_EXT_TLV_2_TAG;
  ve2.dummy = 2016;
  req->vendor_extension = &ve2.tl;
#endif
  nfapi_nr_vnf_config_req(config, p5_idx, req);
  printf("[VNF] Sent NFAPI_VNF_CONFIG_REQ num_tlv:%u\n",req->num_tlv);
  return 0;
}

int nr_config_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_config_response_scf_t *resp) {
  nfapi_nr_start_request_scf_t req;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Received NFAPI_CONFIG_RESP idx:%d phy_id:%d\n", p5_idx, resp->header.phy_id);
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Calling oai_enb_init()\n");
  memset(&req, 0, sizeof(req));
  req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_START_REQUEST;
  req.header.phy_id = resp->header.phy_id;
  nfapi_nr_vnf_start_req(config, p5_idx, &req);
  return 0;
}

int nr_start_resp_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_start_response_scf_t *resp) {
  UNUSED(config);
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Received NFAPI_START_RESP idx:%d phy_id:%d\n", p5_idx, resp->header.phy_id);
  return 0;
}

int nr_error_ind_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_error_indication_scf_t *resp)
{
  UNUSED(config);
  NFAPI_TRACE(NFAPI_TRACE_WARN,
              "[VNF] %4d.%2d Received NFAPI_NR_PHY_MSG_TYPE_ERROR_INDICATION (error code 0x%02x, %s) idx:%d phy_id:%d (Previous message 0x%02x)\n",
              resp->sfn,
              resp->slot,
              resp->error_code,
              error_ind_code_to_str(resp->error_code),
              p5_idx,
              resp->header.phy_id,
              resp->message_id);
  // TODO: add error handling to the VNF instead of only reporting the received error
  // - for specific slot errors: possibly reset/stop L1
  // - out of sync: could use Expected SFN/Slot to clean up state
  // - error for DL_TTI/UL_TTI/UL_DCI/Tx_data: "should assume that the UE did
  //   not receive data and control sent in this slot." => this is handled
  //   implicitly by OAI
  return 0;
}

int vendor_nr_ext_cb(nfapi_nr_vnf_config_t *config, int p5_idx, void *msg)
{
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] %s\n", __FUNCTION__);

  switch (((nfapi_nr_p4_p5_message_header_t *)msg)->message_id) {
    case P5_VENDOR_EXT_RSP: {
      vendor_ext_p5_rsp *rsp = (vendor_ext_p5_rsp *)msg;
      NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] P5_VENDOR_EXT_RSP error_code:%d\n", rsp->error_code);
      // send the start request
      nfapi_nr_pnf_start_request_t req = {0};
      req.header.message_id = NFAPI_PNF_START_REQUEST;
      nfapi_nr_vnf_pnf_start_req(config, p5_idx, &req);
    } break;
  }

  return 0;
}

int vnf_nr_unpack_p4_p5_vendor_extension(void *header,
                                         uint8_t **ppReadPackedMessage,
                                         uint8_t *end,
                                         nfapi_p4_p5_codec_config_t *codec)
{
  UNUSED(codec);
  // NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);
  if (((nfapi_nr_p4_p5_message_header_t *)header)->message_id == P5_VENDOR_EXT_RSP) {
    vendor_ext_p5_rsp *req = (vendor_ext_p5_rsp *)(header);
    return (!pull16(ppReadPackedMessage, &req->error_code, end));
  }

  return 0;
}

void *vnf_nr_allocate_p4_p5_vendor_ext(uint16_t message_id, uint16_t *msg_size)
{
  if (message_id == P5_VENDOR_EXT_RSP) {
    *msg_size = sizeof(vendor_ext_p5_rsp);
    return (nfapi_p4_p5_message_header_t *)malloc(sizeof(vendor_ext_p5_rsp));
  }

  return 0;
}

void vnf_nr_deallocate_p4_p5_vendor_ext(void *header) {
  free(header);
}

static bool has_stop_ind = false;
static bool waiting_stop_ind = false;

int nr_stop_ind_cb(nfapi_nr_vnf_config_t *config, int p5_idx, nfapi_nr_stop_indication_scf_t *resp)
{
  UNUSED(config);
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Received NFAPI_STOP_IND idx:%d phy_id:%d\n", p5_idx, resp->header.phy_id);
  has_stop_ind = true;
#ifdef ENABLE_AERIAL
  nvIPC_Stop();
#endif
#ifdef ENABLE_SOCKET
  nfapi_vnf_p7_stop(get_p7_nr_vnf_config());
#endif
#ifdef ENABLE_WLS
  wls_vnf_stop();
#endif
  if (!waiting_stop_ind) {
    // hasn't been initialized yet, means the PNF stopped before the VNF did
    // raise a SIGINT to stop the VNF
    raise(SIGINT);
  }
  return 0;
}

void stop_nr_nfapi_vnf()
{
  if (has_stop_ind) {
    // If it got here with the STOP.indication flag already set, it means it was triggered by the PNF,
    // no need to send a STOP.request
    return;
  }
#ifdef ENABLE_WLS
  wls_vnf_send_stop_request();
#endif
#ifdef ENABLE_AERIAL
  nvIPC_send_stop_request();
#endif
#ifdef ENABLE_SOCKET
  socket_nfapi_send_stop_request((vnf_nr_t *)get_nr_config());
#endif
  waiting_stop_ind = true;
  uint64_t counter = 0;
  vnf_p7_t *p7_vnf = get_p7_nr_vnf();
  while (p7_vnf->terminate == 0 && counter < 50) {
    NFAPI_TRACE(NFAPI_TRACE_DEBUG, "Not terminated yet, counter %ld\n", counter);
    usleep(1000);
    counter++;
  }
  if (p7_vnf->terminate == 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "STOP.indication timed out, exiting\n");
    nfapi_nr_stop_indication_scf_t msg = {.header.message_id = NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION, .header.phy_id = 0};
    nfapi_nr_vnf_config_t *config = get_nr_config();
    config->nr_stop_ind(config, 0, &msg);
  } else {
    NFAPI_TRACE(NFAPI_TRACE_DEBUG, "Terminated, exiting\n");
  }
}

void configure_nr_nfapi_vnf(const char *vnf_addr, uint16_t vnf_p5_port, uint16_t vnf_p7_port)
{
#ifndef ENABLE_AERIAL
  nfapi_setmode(NFAPI_MODE_VNF);
#else
  UNUSED(vnf_addr);
  UNUSED(vnf_p7_port);
#endif
  vnf_info *vnf = calloc(1, sizeof(vnf_info));
  memset(vnf->p7_vnfs, 0, sizeof(vnf->p7_vnfs));
  vnf->p7_vnfs[0].timing_window = 30;
  vnf->p7_vnfs[0].periodic_timing_enabled = 0;
  vnf->p7_vnfs[0].aperiodic_timing_enabled = 0;
  vnf->p7_vnfs[0].periodic_timing_period = 1;
  vnf->p7_vnfs[0].config = nfapi_vnf_p7_config_create();
#ifndef ENABLE_AERIAL
  NFAPI_TRACE(NFAPI_TRACE_INFO,
              "[VNF] %s() vnf->p7_vnfs[0].config:%p VNF ADDRESS:%s:%d\n",
              __FUNCTION__,
              vnf->p7_vnfs[0].config,
              vnf_addr,
              vnf_p5_port);
  strcpy(vnf->p7_vnfs[0].local_addr, vnf_addr);
  vnf->p7_vnfs[0].local_port = vnf_p7_port;
#endif
  vnf->p7_vnfs[0].mac = malloc(sizeof(mac_t));
  nfapi_nr_vnf_config_t *config = nfapi_nr_vnf_config_create();
  set_config(config);
  config->malloc = malloc;
  config->free = free;
  config->vnf_p5_port = vnf_p5_port;
  config->vnf_ipv4 = 1;
  config->vnf_ipv6 = 0;
  config->pnf_list = 0;
  config->phy_list = 0;

  config->pnf_nr_connection_indication = &pnf_nr_connection_indication_cb;
  config->pnf_disconnect_indication = &pnf_nr_disconnection_indication_cb;

  config->pnf_nr_param_resp = &pnf_nr_param_resp_cb;
  config->pnf_nr_config_resp = &pnf_nr_config_resp_cb;
  config->pnf_nr_start_resp = &pnf_nr_start_resp_cb;
  config->nr_param_resp = &nr_param_resp_cb;
  config->nr_config_resp = &nr_config_resp_cb;
  config->nr_start_resp = &nr_start_resp_cb;
  config->nr_stop_ind = &nr_stop_ind_cb;
  config->nr_error_ind = &nr_error_ind_cb;
  config->vendor_ext = &vendor_nr_ext_cb;
  config->user_data = vnf;
  // To allow custom vendor extentions to be added to nfapi
  config->codec_config.unpack_vendor_extension_tlv = &vnf_nr_unpack_vendor_extension_tlv;
  config->codec_config.pack_vendor_extension_tlv = &vnf_nr_pack_vendor_extension_tlv;
  config->codec_config.unpack_p4_p5_vendor_extension = &vnf_nr_unpack_p4_p5_vendor_extension;
  config->codec_config.pack_p4_p5_vendor_extension = &vnf_nr_pack_p4_p5_vendor_extension;
  config->allocate_p4_p5_vendor_ext = &vnf_nr_allocate_p4_p5_vendor_ext;
  config->deallocate_p4_p5_vendor_ext = &vnf_nr_deallocate_p4_p5_vendor_ext;
  config->codec_config.allocate = &vnf_nr_allocate;
  config->codec_config.deallocate = &vnf_nr_deallocate;

#ifdef ENABLE_WLS
  config->unpack_func = &fapi_nr_p5_message_unpack;
  config->hdr_unpack_func = &fapi_nr_message_header_unpack;
  config->pack_func = &fapi_nr_p5_message_pack;
  config->send_p5_msg = &wls_vnf_nr_send_p5_message;
  printf("WLS MODE PNF\n");
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[PNF] Creating WLS VNF NFAPI start thread %s\n", __FUNCTION__);
  //wls_fapi_pnf_nr_start_thread(config);
  threadCreate(&vnf_p5_init_and_receive_pthread, wls_fapi_vnf_nr_start_thread, config, "NFAPI_WLS_VNF", -1, OAI_PRIORITY_RT_MAX);
#endif

#ifdef ENABLE_SOCKET
  config->unpack_func = &nfapi_nr_p5_message_unpack;
  config->hdr_unpack_func = &nfapi_nr_p5_message_header_unpack;
  config->pack_func = &nfapi_nr_p5_message_pack;
  config->send_p5_msg = &vnf_nr_send_p5_msg;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Creating VNF NFAPI start thread %s\n", __FUNCTION__);
  pthread_create(&vnf_p5_init_and_receive_pthread, NULL, (void *)&vnf_start_p5_thread, config);
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Created VNF NFAPI start thread %s\n", __FUNCTION__);
#endif
#ifdef ENABLE_AERIAL
  config->unpack_func = &fapi_nr_p5_message_unpack;
  config->hdr_unpack_func = &fapi_nr_message_header_unpack;
  config->pack_func = &fapi_nr_p5_message_pack;
  config->send_p5_msg = &aerial_nr_send_p5_message;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] Created VNF NFAPI start thread %s\n", __FUNCTION__);
  // One pnf list entry per configured PHY so that aerial_nr_send_p5_message()
  // can route CONFIG/START requests by phy_id.
  uint8_t num_phys = RC.nrmac[0]->nvipc_params_s.num_phys;
  for (int i = 0; i < num_phys; i++) {
    nfapi_vnf_pnf_info_t *pnf = calloc(1, sizeof(*pnf));
    pnf->p5_idx = i;
    pnf->connected = 1;
    nfapi_nr_vnf_pnf_list_add(config, pnf);
    NFAPI_TRACE(NFAPI_TRACE_INFO, "Registered aerial PNF entry for phy_id %d\n", i);
  }

  vnf_p7_info *p7_vnf = vnf->p7_vnfs;

  NFAPI_TRACE(NFAPI_TRACE_INFO,
              "[VNF] pnf start response idx:%d config:%p user_data:%p p7_vnf[config:%p thread_started:%d]\n",
              1,
              config,
              config->user_data,
              vnf->p7_vnfs[0].config,
              vnf->p7_vnfs[0].thread_started);
  configure_nr_p7_vnf(p7_vnf);
#endif
}

int oai_nfapi_dl_tti_req(nfapi_nr_dl_tti_request_t *dl_config_req)
{
  LOG_D(NR_PHY, "Entering oai_nfapi_nr_dl_config_req sfn:%d,slot:%d\n", dl_config_req->SFN, dl_config_req->Slot);
  nfapi_vnf_p7_config_t *p7_config = get_p7_nr_vnf_config();
  dl_config_req->header.message_id= NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST;

  bool retval = nfapi_vnf_p7_dl_tti_req(p7_config, dl_config_req);

  dl_config_req->dl_tti_request_body.nPDUs                        = 0;
  dl_config_req->dl_tti_request_body.nGroup                       = 0;


  if (!retval) {
    LOG_E(PHY, "%s() Problem sending retval:%d\n", __FUNCTION__, retval);
  }
  return retval;
}

int oai_nfapi_tx_data_req(nfapi_nr_tx_data_request_t *tx_data_req)
{
  LOG_D(NR_PHY, "Entering oai_nfapi_nr_tx_data_req sfn:%d,slot:%d\n", tx_data_req->SFN, tx_data_req->Slot);
  nfapi_vnf_p7_config_t *p7_config = get_p7_nr_vnf_config();
  tx_data_req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST;
  //LOG_D(PHY, "[VNF] %s() TX_REQ sfn_sf:%d number_of_pdus:%d\n", __FUNCTION__, NFAPI_SFNSF2DEC(tx_req->sfn_sf), tx_req->tx_request_body.number_of_pdus);
  bool retval = nfapi_vnf_p7_tx_data_req(p7_config, tx_data_req);

  if (!retval) {
    LOG_E(PHY, "%s() Problem sending retval:%d\n", __FUNCTION__, retval);
  } else {
    tx_data_req->Number_of_PDUs = 0;
  }

  return retval;
}

int oai_nfapi_ul_dci_req(nfapi_nr_ul_dci_request_t *ul_dci_req) {
  nfapi_vnf_p7_config_t *p7_config = get_p7_nr_vnf_config();
  ul_dci_req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST;
  //LOG_D(PHY, "[VNF] %s() HI_DCI0_REQ sfn_sf:%d dci:%d hi:%d\n", __FUNCTION__, NFAPI_SFNSF2DEC(hi_dci0_req->sfn_sf), hi_dci0_req->hi_dci0_request_body.number_of_dci, hi_dci0_req->hi_dci0_request_body.number_of_hi);
  bool retval = nfapi_vnf_p7_ul_dci_req(p7_config, ul_dci_req);

  if (!retval) {
    LOG_E(PHY, "%s() Problem sending retval:%d\n", __FUNCTION__, retval);
  } else {
    ul_dci_req->numPdus = 0;
  }

  return retval;
}

int oai_nfapi_ul_tti_req(nfapi_nr_ul_tti_request_t *ul_tti_req) {
  nfapi_vnf_p7_config_t *p7_config = get_p7_nr_vnf_config();
  ul_tti_req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST;

  bool retval = nfapi_vnf_p7_ul_tti_req(p7_config, ul_tti_req);

  if (!retval) {
    LOG_E(PHY, "%s() Problem sending retval:%d\n", __FUNCTION__, retval);
  } else {
    // Reset number of PDUs so that it is not resent
    ul_tti_req->n_pdus = 0;
    ul_tti_req->n_group = 0;
    ul_tti_req->n_ulcch = 0;
    ul_tti_req->n_ulsch = 0;
  }
  return retval;
}
