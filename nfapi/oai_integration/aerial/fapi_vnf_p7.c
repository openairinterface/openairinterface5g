/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
* \brief OAI FAPI VNF P7 message handler procedures
 */

#include "fapi_vnf_p7.h"
#include "nr_nfapi_p7.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"

static uint8_t aerial_unpack_nr_rx_data_indication_body(nfapi_nr_rx_data_pdu_t *value,
                                                        uint8_t **ppReadPackedMsg,
                                                        uint8_t *end,
                                                        nfapi_p7_codec_config_t *config)
{
  if (!(pull32(ppReadPackedMsg, &value->handle, end) && pull16(ppReadPackedMsg, &value->rnti, end)
        && pull8(ppReadPackedMsg, &value->harq_id, end) &&
        // For Aerial, RX_DATA.indication PDULength is changed to 32 bit field
        pull32(ppReadPackedMsg, &value->pdu_length, end) && pull8(ppReadPackedMsg, &value->ul_cqi, end)
        && pull16(ppReadPackedMsg, &value->timing_advance, end) && pull16(ppReadPackedMsg, &value->rssi, end))) {
    return 0;
  }

  // Allocate space for the pdu to be unpacked later
  value->pdu = nfapi_p7_allocate(sizeof(*value->pdu) * value->pdu_length, config);

  return 1;
}

uint8_t aerial_unpack_nr_rx_data_indication(uint8_t **ppReadPackedMsg,
                                            uint8_t *end,
                                            uint8_t **pDataMsg,
                                            uint8_t *data_end,
                                            nfapi_nr_rx_data_indication_t *msg,
                                            nfapi_p7_codec_config_t *config)
{
  // For Aerial unpacking, the PDU data is packed in pDataMsg, and we read it after unpacking the PDU headers

  nfapi_nr_rx_data_indication_t *pNfapiMsg = (nfapi_nr_rx_data_indication_t *)msg;
  // Unpack SFN, slot, nPDU
  if (!(pull16(ppReadPackedMsg, &pNfapiMsg->sfn, end) && pull16(ppReadPackedMsg, &pNfapiMsg->slot, end)
        && pull16(ppReadPackedMsg, &pNfapiMsg->number_of_pdus, end))) {
    return 0;
  }
  // Allocate the PDU list for number of PDUs
  if (pNfapiMsg->number_of_pdus > 0) {
    pNfapiMsg->pdu_list = nfapi_p7_allocate(sizeof(*pNfapiMsg->pdu_list) * pNfapiMsg->number_of_pdus, config);
  }
  // For each PDU, unpack its header
  for (int i = 0; i < pNfapiMsg->number_of_pdus; i++) {
    if (!aerial_unpack_nr_rx_data_indication_body(&pNfapiMsg->pdu_list[i], ppReadPackedMsg, end, config))
      return 0;
  }
  // After unpacking the PDU headers, unpack the PDU data from the separate buffer
  for (int i = 0; i < pNfapiMsg->number_of_pdus; i++) {
    if (!pullarray8(pDataMsg,
                    pNfapiMsg->pdu_list[i].pdu,
                    pNfapiMsg->pdu_list[i].pdu_length,
                    pNfapiMsg->pdu_list[i].pdu_length,
                    data_end)) {
      return 0;
    }
  }

  return 1;
}

uint8_t aerial_unpack_nr_srs_indication(uint8_t **ppReadPackedMsg, uint8_t *end, uint8_t **pDataMsg, uint8_t *data_end, void *msg)
{
  uint8_t retval = unpack_nr_srs_indication(ppReadPackedMsg, end, msg);
  nfapi_nr_srs_indication_t *srs_ind = (nfapi_nr_srs_indication_t *)msg;
  for (uint8_t pdu_idx = 0; pdu_idx < srs_ind->number_of_pdus; pdu_idx++) {
    nfapi_nr_srs_indication_pdu_t *pdu = &srs_ind->pdu_list[pdu_idx];
    if (!unpack_nr_srs_report_tlv_value(&pdu->report_tlv, pDataMsg, data_end)) {
      return 0;
    }
  }
  return retval;
}

static int32_t aerial_pack_tx_data_request(void *pMessageBuf,
                                           void *pPackedBuf,
                                           void *pDataBuf,
                                           uint32_t packedBufLen,
                                           int32_t dataBufLen,
                                           nfapi_p7_codec_config_t *config,
                                           nfapi_vnf_p7_config_t *p7_config,
                                           int32_t *data_len)
{
  if (pMessageBuf == NULL || pPackedBuf == NULL) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "P7 Pack supplied pointers are null\n");
    return -1;
  }

  nfapi_nr_p7_message_header_t *pMessageHeader = pMessageBuf;
  uint8_t *data_end = pDataBuf + dataBufLen;
  uint8_t *pDataPackedMessage = pDataBuf;
  uint8_t *pPackedDataField = &pDataPackedMessage[0];
  uint8_t *pPackedDataFieldStart = &pDataPackedMessage[0];
  uint8_t **ppWriteData = &pPackedDataField;
  const int32_t dataBufLen32 = (dataBufLen + 3) / 4;
  nfapi_nr_tx_data_request_t *pNfapiMsg = (nfapi_nr_tx_data_request_t *)pMessageHeader;
  // Actual payloads are packed in a separate buffer
  for (int i = 0; i < pNfapiMsg->Number_of_PDUs; i++) {
    nfapi_nr_pdu_t *value = (nfapi_nr_pdu_t *)&pNfapiMsg->pdu_list[i];
    // recalculate PDU_Length for Aerial (leave only the size occupied in the payload buffer afterward)
    // assuming there is only 1 TLV present
    value->PDU_length = value->TLVs[0].length;
    for (uint32_t k = 0; k < value->num_TLV; ++k) {
      uint32_t *buf = value->TLVs[k].tag == 0 ? value->TLVs[k].value.direct : value->TLVs[k].value.ptr;
      // Ensure tag is 2
      value->TLVs[k].tag = 2;
      uint32_t num_values_to_push = ((value->TLVs[k].length + 3) / 4);
      if (value->TLVs[k].length > 0) {
        if (value->TLVs[k].length % 4 != 0) {
          if (!pusharray32(buf, dataBufLen32, num_values_to_push - 1, ppWriteData, data_end)) {
            return 0;
          }
          int bytesToAdd = 4 - (4 - (value->TLVs[k].length % 4)) % 4;
          if (bytesToAdd != 4) {
            for (int j = 0; j < bytesToAdd; j++) {
              uint8_t toPush = (uint8_t)(buf[num_values_to_push - 1] >> (j * 8));
              if (!push8(toPush, ppWriteData, data_end)) {
                return 0;
              }
            }
          }
        } else {
          // no padding needed
          if (!pusharray32(buf, dataBufLen32, num_values_to_push, ppWriteData, data_end)) {
            return 0;
          }
        }
      } else {
        LOG_E(NR_MAC, "value->TLVs[i].length was 0! (%d.%d) \n", pNfapiMsg->SFN, pNfapiMsg->Slot);
      }
    }
  }

  // calculate data_len
  uintptr_t dataHead = (uintptr_t)pPackedDataFieldStart;
  uintptr_t dataEnd = (uintptr_t)pPackedDataField;
  data_len[0] = dataEnd - dataHead;

  if (!p7_config->pack_func(pMessageBuf, pPackedBuf, packedBufLen, config)) {
    return -1;
  }

  return (int32_t)(pMessageHeader->message_length);
}

bool aerial_nr_send_p7_message(vnf_p7_t *vnf_p7, nfapi_nr_p7_message_header_t *header)
{
  // Common to both cases, we can set it now
  // In most cases we don't need the data pool, only the CPU_MSG
  nv_ipc_msg_t send_msg = {.msg_id = header->message_id, .data_pool = NV_IPC_MEMPOOL_CPU_MSG, .cell_id = header->phy_id};
  // Check if TX_DATA request, if true, need to pack to data_buf
  if (header->message_id == NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST) {
    // for this we need the secondary CPU_DATA pool
    send_msg.data_pool = NV_IPC_MEMPOOL_CPU_DATA;
    // Allocate the message after setting the data pool
    if (!allocate_msg(&send_msg)) {
      LOG_E(NFAPI_VNF, "Error in allocating nvipc message\n");
      return false;
    }

    nfapi_nr_tx_data_request_t *pNfapiMsg = (nfapi_nr_tx_data_request_t *)header;
    uint64_t size = 0;
    for (int i = 0; i < pNfapiMsg->Number_of_PDUs; ++i) {
      size += pNfapiMsg->pdu_list[i].PDU_length;
    }
    AssertFatal(size <= get_cpu_data_buf_size(), "Message data larger than available buffer, tried to pack %" PRId64, size);
    int32_t data_len = 0;
    int32_t len_FAPI = aerial_pack_tx_data_request(header,
                                                   send_msg.msg_buf,
                                                   send_msg.data_buf,
                                                   get_cpu_msg_buf_size(),
                                                   get_cpu_data_buf_size(),
                                                   &vnf_p7->_public.codec_config,
                                                   ((nfapi_vnf_p7_config_t *)vnf_p7),
                                                   &data_len);

    // Set both lengths
    send_msg.msg_len = len_FAPI + 8; // adding 8 to account for the size of the FAPI header
    send_msg.data_len = data_len;

    if (len_FAPI <= 0) {
      LOG_E(NFAPI_VNF, "Problem packing TX_DATA_request\n");
      release_msg(&send_msg);
      return false;
    }
    // Send
    return send_nvipc_msg(&send_msg);
  } else {
    send_msg.data_len = 0;
    send_msg.data_buf = NULL;
    // Allocate the message
    if (!allocate_msg(&send_msg)) {
      LOG_E(NFAPI_VNF, "Error in allocating nvipc message\n");
      return false;
    }
    // Pack directly into it
    nfapi_vnf_p7_config_t* p7_config = (nfapi_vnf_p7_config_t *)vnf_p7;
    int len_FAPI = p7_config->pack_func(header, send_msg.msg_buf, get_cpu_msg_buf_size(), &vnf_p7->_public.codec_config);
    if (len_FAPI <= 0) {
      LOG_E(NFAPI_VNF, "Problem packing message 0x%02x\n", header->message_id);
      release_msg(&send_msg);
      return false;
    }
    // Set the length
    send_msg.msg_len = len_FAPI;
    // Send
    return send_nvipc_msg(&send_msg);
  }
}
