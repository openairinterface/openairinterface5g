/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "dci_payload_utils.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"

static void fill_srs_toa_vendor_ext_indication(nfapi_nr_srs_toa_vendor_ext_indication_t *msg)
{
  msg->sfn = rand16_range(0, 1023);
  msg->slot = rand16_range(0, 159);
  msg->number_of_pdus = rand8_range(1, 8);
  msg->pdu_list = calloc_or_fail(msg->number_of_pdus, sizeof(*msg->pdu_list));
  for (int pdu_idx = 0; pdu_idx < msg->number_of_pdus; ++pdu_idx) {
    nfapi_nr_srs_toa_vendor_ext_pdu_t *pdu = &msg->pdu_list[pdu_idx];
    pdu->rnti = rand16_range(1, 65535);
    pdu->num_ta = rand8_range(1, NFAPI_NR_MAX_NUM_TA_NSEC);
    for (int ta_idx = 0; ta_idx < pdu->num_ta; ++ta_idx) {
      pdu->ta_offset_nsec[ta_idx] = rands16_range(-16800, 16800);
    }
  }
}

static void test_pack_unpack(nfapi_nr_srs_toa_vendor_ext_indication_t *req)
{
  size_t message_size = get_srs_toa_vendor_ext_indication_size(req);
  uint8_t *msg_buf = calloc_or_fail(message_size, sizeof(uint8_t));
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
  nfapi_nr_srs_toa_vendor_ext_indication_t unpacked_req = {0};
  int unpack_result =
      fapi_nr_p7_message_unpack(msg_buf, header.message_length + NFAPI_HEADER_LENGTH, &unpacked_req, sizeof(unpacked_req), 0);
  DevAssert(unpack_result >= 0);
  DevAssert(eq_srs_toa_vendor_ext_indication(&unpacked_req, req));
  free_srs_toa_vendor_ext_indication(&unpacked_req);
  free(msg_buf);
}

static void test_copy(const nfapi_nr_srs_toa_vendor_ext_indication_t *msg)
{
  // Test copy function
  nfapi_nr_srs_toa_vendor_ext_indication_t copy = {0};
  copy_srs_toa_vendor_ext_indication(msg, &copy);
  DevAssert(eq_srs_toa_vendor_ext_indication(msg, &copy));
  free_srs_toa_vendor_ext_indication(&copy);
}

int main()
{
  fapi_test_init();

  nfapi_nr_srs_toa_vendor_ext_indication_t *req = calloc_or_fail(1, sizeof(nfapi_nr_srs_toa_vendor_ext_indication_t));
  req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_SRS_TOA_VENDOR_EXTENSION_INDICATION;
  // Get the actual allocated size
  printf("Allocated size before filling: %zu bytes\n", get_srs_toa_vendor_ext_indication_size(req));
  // Fill TX_DATA request
  fill_srs_toa_vendor_ext_indication(req);
  printf("Allocated size after filling: %zu bytes\n", get_srs_toa_vendor_ext_indication_size(req));
  // Perform tests
  test_pack_unpack(req);
  test_copy(req);
  // All tests successful!
  free_srs_toa_vendor_ext_indication(req);
  free(req);
  return 0;
}
