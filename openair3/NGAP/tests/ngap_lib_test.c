/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h> // abort
#include <stdint.h> // uint*_t
#include <stdio.h> // printf
#include <stdbool.h> // bool

#include "common/utils/assertions.h" // AssertFatal, DevAssert
#include "common/utils/utils.h" // malloc_or_fail, calloc_or_fail
#include "common/utils/ds/byte_array.h" // create_byte_array
#include "conversions.h" // TBCD_TO_MCC_MNC, OCTET_STRING_TO_INT*
#include "ngap_common.h" // NGAP_ERROR, NGAP_DEBUG, ngap_messages_types.h
#include "ngap_gNB_paging.h" // encode_ng_paging, decode_ng_paging, free_ng_paging
#include "ngap_msg_includes.h" // NGAP_NGAP_PDU_t, NGAP_Paging_t, etc.
#include "oai_asn1.h" // ASN_STRUCT_FREE, asn_DEF_NGAP_NGAP_PDU
#include "config/config_load_configmodule.h" // configmodule_interface_t
#include "common/utils/LOG/log.h" // log_init

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  UNUSED(assert);
  printf("detected error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

static NGAP_NGAP_PDU_t *ngap_encode_decode(const NGAP_NGAP_PDU_t *enc_pdu)
{
  DevAssert(enc_pdu != NULL);
  char errbuf[1024];
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_NGAP_NGAP_PDU, enc_pdu, errbuf, &errlen);
  AssertFatal(ret == 0, "asn_check_constraints() failed: %s\n", errbuf);

  uint8_t msgbuf[16384];
  asn_enc_rval_t enc = aper_encode_to_buffer(&asn_DEF_NGAP_NGAP_PDU, NULL, enc_pdu, msgbuf, sizeof(msgbuf));
  AssertFatal(enc.encoded > 0, "aper_encode_to_buffer() failed\n");

  NGAP_NGAP_PDU_t *dec_pdu = NULL;
  asn_codec_ctx_t st = {.max_stack_size = 100 * 1000};
  asn_dec_rval_t dec = aper_decode(&st, &asn_DEF_NGAP_NGAP_PDU, (void **)&dec_pdu, msgbuf, enc.encoded, 0, 0);
  AssertFatal(dec.code == RC_OK, "aper_decode() failed\n");

  return dec_pdu;
}

static void ngap_msg_free(NGAP_NGAP_PDU_t *pdu)
{
  ASN_STRUCT_FREE(asn_DEF_NGAP_NGAP_PDU, pdu);
}

/** @brief Test NGAP Paging encoding/decoding roundtrip */
static void test_ngap_paging_encode_decode(void)
{
  // Create test paging indication
  ngap_paging_ind_t orig = {0};
  orig.ue_paging_identity.s_tmsi.amf_set_id = 123; // Valid range: 0-1023 (10 bits)
  orig.ue_paging_identity.s_tmsi.amf_pointer = 45; // Valid range: 0-63 (6 bits)
  orig.ue_paging_identity.s_tmsi.m_tmsi = 0x12345678;
  orig.n_tai = 2;
  orig.tai_list[0].plmn.mcc = 208;
  orig.tai_list[0].plmn.mnc = 95;
  orig.tai_list[0].plmn.mnc_digit_length = 2;
  orig.tai_list[0].tac = 12345;
  orig.tai_list[1].plmn.mcc = 001;
  orig.tai_list[1].plmn.mnc = 01;
  orig.tai_list[1].plmn.mnc_digit_length = 2;
  orig.tai_list[1].tac = 23456;
  orig.paging_drx = malloc_or_fail(sizeof(ngap_paging_drx_t));
  *orig.paging_drx = NGAP_PAGING_DRX_128;
  orig.paging_priority = malloc_or_fail(sizeof(ngap_paging_priority_t));
  *orig.paging_priority = NGAP_PAGING_PRIO_LEVEL2;
  uint8_t ue_cap_data[] = {0x01, 0x02, 0x03, 0x04};
  orig.ue_radio_capability = malloc_or_fail(sizeof(byte_array_t));
  *orig.ue_radio_capability = create_byte_array(sizeof(ue_cap_data), ue_cap_data);
  orig.origin = calloc_or_fail(1, sizeof(*orig.origin));
  *orig.origin = NGAP_PAGING_ORIGIN_NON_3GPP;

  // Encode
  NGAP_NGAP_PDU_t *enc_pdu = encode_ng_paging(&orig);
  AssertFatal(enc_pdu != NULL, "encode_ng_paging(): failed to encode message\n");
  LOG_A(NGAP, "encoded: m_tmsi=0x%x, n_tai=%d\n", orig.ue_paging_identity.s_tmsi.m_tmsi, orig.n_tai);

  // Encode/decode roundtrip
  NGAP_NGAP_PDU_t *dec_pdu = ngap_encode_decode(enc_pdu);
  ngap_msg_free(enc_pdu);

  // Decode
  ngap_paging_ind_t decoded = {0};
  bool decode_ret = decode_ng_paging(&decoded, dec_pdu);
  AssertFatal(decode_ret, "decode_ng_paging(): could not decode message\n");
  ngap_msg_free(dec_pdu);
  LOG_A(NGAP, "decoded: m_tmsi=0x%x, n_tai=%d\n", decoded.ue_paging_identity.s_tmsi.m_tmsi, decoded.n_tai);

  // Verify decoded values using equality function
  bool eq_ret = eq_ng_paging(&orig, &decoded);
  AssertFatal(eq_ret, "eq_ng_paging(): decoded message doesn't match original\n");
  LOG_A(NGAP, "eq_ng_paging(): decoded message matches original\n");

  free_ng_paging(&decoded);
  free_ng_paging(&orig);
}

/** @brief Test decoding of all GUAMI fields */
static void test_ngap_guami_decode(void)
{
  const uint16_t set_ids[] = {1, 0x155, 0x3ff};
  const uint8_t pointers[] = {1, 0x2a, 0x3f};
  for (size_t i = 0; i < sizeofArray(set_ids); i++) {
    NGAP_GUAMI_t guami = {0};
    MCC_MNC_TO_PLMNID(208, 95, 2, &guami.pLMNIdentity);
    AMF_REGION_TO_BIT_STRING(0x80, &guami.aMFRegionID);
    AMF_SETID_TO_BIT_STRING(set_ids[i], &guami.aMFSetID);
    AMF_POINTER_TO_BIT_STRING(pointers[i], &guami.aMFPointer);

    nr_guami_t decoded = decode_ngap_guami(&guami);
    AssertFatal(decoded.plmn.mcc == 208 && decoded.plmn.mnc == 95 && decoded.plmn.mnc_digit_length == 2, "PLMN mismatch\n");
    AssertFatal(decoded.amf_region_id == 0x80, "amf_region_id 0x%x != 0x80\n", decoded.amf_region_id);
    AssertFatal(decoded.amf_set_id == set_ids[i], "amf_set_id 0x%x != 0x%x\n", decoded.amf_set_id, set_ids[i]);
    AssertFatal(decoded.amf_pointer == pointers[i], "amf_pointer 0x%x != 0x%x\n", decoded.amf_pointer, pointers[i]);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_GUAMI, &guami);
  }
  LOG_A(NGAP, "decode_ngap_guami() test passed\n");
}

// Stub for uniqCfg required by logging/config system
configmodule_interface_t *uniqCfg = NULL;

int main(void)
{
  // Initialize logging system
  logInit();
  set_log(NGAP, OAILOG_INFO);

  test_ngap_paging_encode_decode();
  test_ngap_guami_decode();
  LOG_A(NGAP, "All NGAP tests passed!\n");
  return 0;
}
