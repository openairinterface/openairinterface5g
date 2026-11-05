/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "dci_payload_utils.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"

static void fill_tx_data_request_PDU(nfapi_nr_pdu_t *pdu)
{
  // PDU_Length The total length (in bytes) of the PDU description and PDU data, without the padding bytes.
  // Minimum length = 4 PDULength + 2 PDUIndex + 4 numTLV + 2 tag + 4 length + 4 value = 20
  // ( assuming 1 TLV and "empty payload" sends 4 bytes of zeroes - For testing purposes only )
  // Max Length => 10 bytes PDU Header + 2 * (2 bytes tag, 4 bytes length, 38016*4 bytes value (on value.direct))
  // = 304150
  pdu->PDU_index = rand16();
  pdu->num_TLV = rand8_range(1, NFAPI_NR_MAX_TX_REQUEST_TLV);
  pdu->PDU_length = rand32_range(20, 10 + pdu->num_TLV * (4 + (38016 * 4)));
  uint32_t remaining_size = pdu->PDU_length - 10 - (pdu->num_TLV * 4);
  for (int tlv_idx = 0; tlv_idx < pdu->num_TLV; ++tlv_idx) {
    nfapi_nr_tx_data_request_tlv_t *tlv = &pdu->TLVs[tlv_idx];
    tlv->tag = rand16_range(0, 1);
    uint32_t maxSize = 0;
    if (tlv->tag == 0) {
      maxSize = sizeof(tlv->value.direct);
    } else {
      maxSize = (remaining_size / pdu->num_TLV);
    }
    if (pdu->num_TLV == 1) {
      tlv->length = remaining_size;
    } else {
      if (tlv_idx == pdu->num_TLV - 1) {
        tlv->length = min(maxSize, sizeof(tlv->value.direct));
      } else {
        tlv->length = min(rand32_range(0, maxSize), sizeof(tlv->value.direct));
        remaining_size -= tlv->length - 4;
      }
    }
    switch (tlv->tag) {
      case 0:
        // value.direct
        if (tlv->length % 4 != 0) {
          for (int idx = 0; idx < ((tlv->length + 3) / 4) - 1; ++idx) {
            tlv->value.direct[idx] = rand32();
          }
          uint32_t bytesToAdd = 4 - (4 - (tlv->length % 4)) % 4;
          if (bytesToAdd != 4) {
            for (int j = 0; j < bytesToAdd; j++) {
              tlv->value.direct[((tlv->length + 3) / 4) - 1] |= rand8();
              tlv->value.direct[((tlv->length + 3) / 4) - 1] = tlv->value.direct[((tlv->length + 3) / 4) - 1] << (j * 8);
            }
          }
        } else {
          // no padding needed
          for (int idx = 0; idx < (tlv->length + 3) / 4; ++idx) {
            tlv->value.direct[idx] = rand32();
          }
        }
        break;
      case 1:
        // value.ptr
        tlv->value.ptr = calloc((tlv->length + 3) / 4, sizeof(uint32_t));
        if (tlv->length % 4 != 0) {
          for (int idx = 0; idx < ((tlv->length + 3) / 4) - 1; ++idx) {
            tlv->value.ptr[idx] = rand32();
          }
          uint32_t bytesToAdd = 4 - (4 - (tlv->length % 4)) % 4;
          if (bytesToAdd != 4) {
            uint32_t value = 0;
            for (int j = 0; j < bytesToAdd; j++) {
              value |= rand8() << (j * 8);
            }
            tlv->value.ptr[((tlv->length + 3) / 4) - 1] = value;
          }
        } else {
          // no padding needed
          for (int idx = 0; idx < (tlv->length + 3) / 4; ++idx) {
            tlv->value.ptr[idx] = rand32();
          }
        }
        break;
      default:
        AssertFatal(1 == 0, "Unsupported tag value %d\n", tlv->tag);
    }
  }
}

static void fill_tx_data_request(nfapi_nr_tx_data_request_t *msg)
{
  msg->SFN = rand16_range(0, 1023);
  msg->Slot = rand16_range(0, 159);
  msg->Number_of_PDUs = rand8_range(1, NFAPI_NR_MAX_TX_REQUEST_PDUS); // Minimum 1 PDUs in order to test at least one
  for (int pdu_idx = 0; pdu_idx < msg->Number_of_PDUs; ++pdu_idx) {
    fill_tx_data_request_PDU(&msg->pdu_list[pdu_idx]);
  }
}

static void test_pack_unpack(nfapi_nr_tx_data_request_t *req)
{
  size_t message_size = get_tx_data_request_size(req);
  uint8_t *msg_buf = calloc_or_fail(message_size, sizeof(uint8_t));
  /*uint8_t msg_buf[1024*1024*3];
  size_t message_size = sizeof(msg_buf);*/
  // first test the packing procedure
  int pack_result = fapi_nr_p7_message_pack(req, msg_buf, message_size, NULL);

  DevAssert(pack_result >= 0 + NFAPI_HEADER_LENGTH);
  // test the unpacking of the header
  // copy first NFAPI_HEADER_LENGTH bytes into a new buffer, to simulate SCTP PEEK
  fapi_message_header_t header;
  uint32_t header_buffer_size = NFAPI_HEADER_LENGTH;
  uint8_t header_buffer[header_buffer_size];
  for (int idx = 0; idx < header_buffer_size; idx++) {
    header_buffer[idx] = msg_buf[idx];
  }
  uint8_t *pReadPackedMessage = header_buffer;
  int unpack_header_result = fapi_nr_p7_message_header_unpack(pReadPackedMessage, NFAPI_HEADER_LENGTH, &header, sizeof(header), 0);
  DevAssert(unpack_header_result >= 0);
  DevAssert(header.message_id == req->header.message_id);
  DevAssert(header.message_length == req->header.message_length);
  // test the unpacking and compare with initial message
  // nfapi_nr_tx_data_request_t is several MB: allocate it on the heap, not the stack
  nfapi_nr_tx_data_request_t *unpacked_req = calloc_or_fail(1, sizeof(nfapi_nr_tx_data_request_t));
  int unpack_result =
      fapi_nr_p7_message_unpack(msg_buf, header.message_length + NFAPI_HEADER_LENGTH, unpacked_req, sizeof(*unpacked_req), 0);
  DevAssert(unpack_result >= 0);
  DevAssert(eq_tx_data_request(unpacked_req, req));
  free_tx_data_request(unpacked_req);
  free(unpacked_req);
  free(msg_buf);
}

static void test_copy(const nfapi_nr_tx_data_request_t *msg)
{
  // Test copy function
  nfapi_nr_tx_data_request_t *copy = calloc_or_fail(1, sizeof(nfapi_nr_tx_data_request_t));
  copy_tx_data_request(msg, copy);
  DevAssert(eq_tx_data_request(msg, copy));
  free_tx_data_request(copy);
  free(copy);
}

int main()
{
  fapi_test_init();

  nfapi_nr_tx_data_request_t *req = calloc_or_fail(1, sizeof(nfapi_nr_tx_data_request_t));
  req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST;
  // Get the actual allocated size
  printf("Allocated size before filling: %zu bytes\n", get_tx_data_request_size(req));
  // Fill TX_DATA request
  fill_tx_data_request(req);
  printf("Allocated size after filling: %zu bytes\n", get_tx_data_request_size(req));
  // Perform tests
  test_pack_unpack(req);
  test_copy(req);
  // All tests successful!
  free_tx_data_request(req);
  free(req);
  return 0;
}
