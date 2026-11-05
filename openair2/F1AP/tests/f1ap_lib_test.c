/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "common/utils/assertions.h"
#include "common/utils/utils.h"

#include "f1ap_messages_types.h"
#include "F1AP_F1AP-PDU.h"

#include "f1ap_lib_common.h"
#include "f1ap_rrc_message_transfer.h"
#include "f1ap_interface_management.h"
#include "f1ap_ue_context.h"
#include "f1ap_positioning.h"
#include "f1ap_paging.h"

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  UNUSED(assert);
  printf("detected error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

static F1AP_F1AP_PDU_t *f1ap_encode_decode(const F1AP_F1AP_PDU_t *enc_pdu)
{
  //xer_fprint(stdout, &asn_DEF_F1AP_F1AP_PDU, enc_pdu);

  DevAssert(enc_pdu != NULL);
  char errbuf[1024];
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_F1AP_F1AP_PDU, enc_pdu, errbuf, &errlen);
  AssertFatal(ret == 0, "asn_check_constraints() failed: %s\n", errbuf);

  uint8_t msgbuf[16384];
  asn_enc_rval_t enc = aper_encode_to_buffer(&asn_DEF_F1AP_F1AP_PDU, NULL, enc_pdu, msgbuf, sizeof(msgbuf));
  AssertFatal(enc.encoded > 0, "aper_encode_to_buffer() failed\n");

  F1AP_F1AP_PDU_t *dec_pdu = NULL;
  asn_codec_ctx_t st = {.max_stack_size = 100 * 1000};
  asn_dec_rval_t dec = aper_decode(&st, &asn_DEF_F1AP_F1AP_PDU, (void **)&dec_pdu, msgbuf, enc.encoded, 0, 0);
  AssertFatal(dec.code == RC_OK, "aper_decode() failed\n");

  //xer_fprint(stdout, &asn_DEF_F1AP_F1AP_PDU, dec_pdu);
  return dec_pdu;
}

static void f1ap_msg_free(F1AP_F1AP_PDU_t *pdu)
{
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
}

/** @brief Decode an F1AP PDU given in hex, e.g., a malformed message from a peer */
static F1AP_F1AP_PDU_t *f1ap_decode_hex(const char *hex)
{
  const size_t len = strlen(hex) / 2;
  uint8_t buf[len];
  for (size_t i = 0; i < len; i++)
    AssertFatal(sscanf(&hex[2 * i], "%2hhx", &buf[i]) == 1, "invalid hex string\n");
  F1AP_F1AP_PDU_t *pdu = NULL;
  asn_dec_rval_t dec = aper_decode(NULL, &asn_DEF_F1AP_F1AP_PDU, (void **)&pdu, buf, len, 0, 0);
  AssertFatal(dec.code == RC_OK, "aper_decode() failed\n");
  return pdu;
}

/**
 * @brief Test Initial UL RRC Message Transfer encoding/decoding
 */
static void test_initial_ul_rrc_message_transfer(void)
{
  plmn_id_t plmn = {.mcc = 208, .mnc = 95, .mnc_digit_length = 2};
  uint8_t rrc[] = "RRC Container";
  uint8_t du2cu[] = "DU2CU Container";
  f1ap_initial_ul_rrc_message_t orig = {
    .gNB_DU_ue_id = 12,
    .plmn = plmn,
    .nr_cellid = 135,
    .crnti = 0x1234,
    .rrc_container = rrc,
    .rrc_container_length = sizeof(rrc),
    .du2cu_rrc_container = du2cu,
    .du2cu_rrc_container_length = sizeof(du2cu),
    .transaction_id = 2,
  };

  F1AP_F1AP_PDU_t *f1enc = encode_initial_ul_rrc_message_transfer(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_initial_ul_rrc_message_t decoded;
  bool ret = decode_initial_ul_rrc_message_transfer(f1dec, &decoded);
  AssertFatal(ret, "decode_initial_ul_rrc_message_transfer(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_initial_ul_rrc_message_transfer(&orig, &decoded);
  AssertFatal(ret, "eq_initial_ul_rrc_message_transfer(): decoded message doesn't match\n");
  free_initial_ul_rrc_message_transfer(&decoded);

  f1ap_initial_ul_rrc_message_t cp = cp_initial_ul_rrc_message_transfer(&orig);
  ret = eq_initial_ul_rrc_message_transfer(&orig, &cp);
  AssertFatal(ret, "eq_initial_ul_rrc_message_transfer(): copied message doesn't match\n");
  free_initial_ul_rrc_message_transfer(&cp);
}

/**
 * @brief Test DL RRC Message Transfer encoding/decoding
 */
static void test_dl_rrc_message_transfer(void)
{
  uint8_t *rrc = calloc_or_fail(strlen("RRC Container") + 1, sizeof(*rrc));
  uint32_t *old_gNB_DU_ue_id = calloc_or_fail(1, sizeof(*old_gNB_DU_ue_id));
  memcpy((void *)rrc, "RRC Container", strlen("RRC Container") + 1);

  f1ap_dl_rrc_message_t orig = {
    .gNB_DU_ue_id = 12,
    .gNB_CU_ue_id = 12,
    .old_gNB_DU_ue_id = old_gNB_DU_ue_id,
    .srb_id = 1,
    .rrc_container = rrc,
    .rrc_container_length = sizeof(rrc),
  };

  F1AP_F1AP_PDU_t *f1enc = encode_dl_rrc_message_transfer(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_dl_rrc_message_t decoded = {0};
  bool ret = decode_dl_rrc_message_transfer(f1dec, &decoded);
  AssertFatal(ret, "decode_initial_ul_rrc_message_transfer(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_dl_rrc_message_transfer(&orig, &decoded);
  AssertFatal(ret, "eq_dl_rrc_message_transfer(): decoded message doesn't match\n");
  free_dl_rrc_message_transfer(&decoded);

  f1ap_dl_rrc_message_t cp = cp_dl_rrc_message_transfer(&orig);
  ret = eq_dl_rrc_message_transfer(&orig, &cp);
  AssertFatal(ret, "eq_dl_rrc_message_transfer(): copied message doesn't match\n");
  free_dl_rrc_message_transfer(&orig);
  free_dl_rrc_message_transfer(&cp);
}

/**
 * @brief Test UL RRC Message Transfer encoding/decoding
 */
static void test_ul_rrc_message_transfer(void)
{
  uint8_t rrc[] = "RRC Container";

  f1ap_ul_rrc_message_t orig = {
    .gNB_DU_ue_id = 12,
    .gNB_CU_ue_id = 12,
    .srb_id = 1,
    .rrc_container = rrc,
    .rrc_container_length = sizeof(rrc),
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ul_rrc_message_transfer(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ul_rrc_message_t decoded = {0};
  bool ret = decode_ul_rrc_message_transfer(f1dec, &decoded);
  AssertFatal(ret, "decode_initial_ul_rrc_message_transfer(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ul_rrc_message_transfer(&orig, &decoded);
  AssertFatal(ret, "eq_dl_rrc_message_transfer(): decoded message doesn't match\n");
  free_ul_rrc_message_transfer(&decoded);

  f1ap_ul_rrc_message_t cp = cp_ul_rrc_message_transfer(&orig);
  ret = eq_ul_rrc_message_transfer(&orig, &cp);
  AssertFatal(ret, "eq_dl_rrc_message_transfer(): copied message doesn't match\n");
  free_ul_rrc_message_transfer(&cp);
}

/**
 * @brief Test F1AP Setup Request Encoding/Decoding
 */
static void test_f1ap_setup_request(void)
{
  /* allocate memory */
  /* gNB_DU_name */
  uint8_t *gNB_DU_name = calloc_or_fail(strlen("OAI DU") + 1, sizeof(*gNB_DU_name));
  memcpy(gNB_DU_name, "OAI DU", strlen("OAI DU") + 1);
  /* sys_info */
  uint8_t *mib = calloc_or_fail(3, sizeof(*mib));
  uint8_t *sib1 = calloc_or_fail(3, sizeof(*sib1));
  f1ap_gnb_du_system_info_t sys_info = {
      .mib_length = 3,
      .mib = mib,
      .sib1_length = 3,
      .sib1 = sib1,
  };
  /* measurement_timing_information */
  int measurement_timing_config_len = strlen("0") + 1;
  uint8_t *measurement_timing_information = calloc_or_fail(measurement_timing_config_len, sizeof(*measurement_timing_information));
  memcpy((void *)measurement_timing_information, "0", measurement_timing_config_len);
  /* TAC */
  uint32_t *tac = calloc_or_fail(1, sizeof(*tac));
  /*
   * TDD test
   */
  // Served Cell Info
  f1ap_served_cell_info_t info = {
      .mode = F1AP_MODE_TDD,
      .tdd.freqinfo.arfcn = 640000,
      .tdd.freqinfo.band = 78,
      .tdd.tbw.nrb = 66,
      .tdd.tbw.scs = 1,
      .measurement_timing_config_len = measurement_timing_config_len,
      .measurement_timing_config = measurement_timing_information,
      .nr_cellid = 123456,
      .plmn.mcc = 1,
      .plmn.mnc = 1,
      .plmn.mnc_digit_length = 3,
      .num_ssi = 1,
      .nssai[0].sst = 1,
      .nssai[0].sd = 1,
      .tac = tac,
  };
  // create message
  f1ap_setup_req_t orig = {
      .gNB_DU_id = 1,
      .gNB_DU_name = (char *)gNB_DU_name,
      .num_cells_available = 1,
      .transaction_id = 2,
      .rrc_ver[0] = 12,
      .rrc_ver[1] = 34,
      .rrc_ver[2] = 56,
  };
  orig.cell = calloc_or_fail(orig.num_cells_available, sizeof(*orig.cell));
  orig.cell[0].info = info;
  orig.cell[0].sys_info = calloc_or_fail(1, sizeof(*orig.cell[0].sys_info));
  *orig.cell[0].sys_info = sys_info;
  // encode
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_setup_request(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);
  // decode
  f1ap_setup_req_t decoded = {0};
  bool ret = decode_f1ap_setup_request(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_request(): could not decode message\n");
  f1ap_msg_free(f1dec);
  // equality check
  ret = eq_f1ap_setup_request(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_request(): decoded message doesn't match\n");
  free_f1ap_setup_request(&decoded);
  // deep copy
  f1ap_setup_req_t cp = cp_f1ap_setup_request(&orig);
  ret = eq_f1ap_setup_request(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_request(): copied message doesn't match\n");
  free_f1ap_setup_request(&cp);
  /*
   * FDD test
   */
  info.mode = F1AP_MODE_FDD;
  info.tdd.freqinfo.arfcn = 0;
  info.tdd.freqinfo.band = 0;
  info.tdd.tbw.nrb = 0;
  info.tdd.tbw.scs = 0;
  info.fdd.ul_freqinfo.arfcn = 640000;
  info.fdd.ul_freqinfo.band = 78;
  info.fdd.dl_freqinfo.arfcn = 641000;
  info.fdd.dl_freqinfo.band = 78;
  info.fdd.ul_tbw.nrb = 66;
  info.fdd.ul_tbw.scs = 1;
  info.fdd.dl_tbw.nrb = 66;
  info.fdd.dl_tbw.scs = 1;
  // encode
  f1enc = encode_f1ap_setup_request(&orig);
  f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);
  // decode
  ret = decode_f1ap_setup_request(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_request(): could not decode message\n");
  f1ap_msg_free(f1dec);
  // equality check
  ret = eq_f1ap_setup_request(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_request(): decoded message doesn't match\n");
  free_f1ap_setup_request(&decoded);
  // copy
  cp = cp_f1ap_setup_request(&orig);
  ret = eq_f1ap_setup_request(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_request(): copied message doesn't match\n");
  free_f1ap_setup_request(&cp);
  // free original message
  free_f1ap_setup_request(&orig);

  // gNB-DU Served Cells item with an unexpected IE ID
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex(
        "00010048000004004e00020002002a00020001002c00270000004e0021080000011000001e2400000000000100000110410009c40000004d002500"
        "02300000ab000a80000000c74003000000");
    f1ap_setup_req_t msg = {0};
    AssertFatal(!decode_f1ap_setup_request(pdu, &msg), "decoded malformed gNB-DU Served Cells item\n");
    free_f1ap_setup_request(&msg);
    f1ap_msg_free(pdu);
  }

  // Served PLMN with only an NPN Support Information extension: no slices
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex(
        "0001004a000004004e00020001002a0003200e00002c00310000002b002b000000f110123456780000000800f11000000180000700000000012340"
        "410009c40800004d00270002300000ab000120");
    f1ap_setup_req_t msg = {0};
    AssertFatal(decode_f1ap_setup_request(pdu, &msg), "could not decode served PLMN with NPN Support Information\n");
    AssertFatal(msg.cell[0].info.num_ssi == 0, "unexpected slices: %d\n", msg.cell[0].info.num_ssi);
    free_f1ap_setup_request(&msg);
    f1ap_msg_free(pdu);
  }

  // gNB-DU RRC Version with an unknown extension
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex("0001002000000300ab000ad0000077cc80037d51ad004e00020013002a4005608ad13451");
    f1ap_setup_req_t msg = {0};
    AssertFatal(decode_f1ap_setup_request(pdu, &msg), "could not decode RRC Version with unknown extension\n");
    free_f1ap_setup_request(&msg);
    f1ap_msg_free(pdu);
  }
}

/**
 * @brief Test F1AP Setup Response Encoding/Decoding
 */
static void test_f1ap_setup_response(void)
{
  /* allocate memory */
  /* gNB_CU_name */
  char *cu_name = "OAI-CU";
  int len = strlen(cu_name) + 1;
  uint8_t *gNB_CU_name = calloc_or_fail(len, sizeof(*gNB_CU_name));
  memcpy((void *)gNB_CU_name, cu_name, len);
  /* create message */
  f1ap_setup_resp_t orig = {
      .gNB_CU_name = (char *)gNB_CU_name,
      .transaction_id = 2,
      .rrc_ver[0] = 12,
      .rrc_ver[1] = 34,
      .rrc_ver[2] = 56,
      .num_cells_to_activate = 1,
  };
  /* Allocate memory for cells_to_activate array */
  orig.cells_to_activate = calloc_or_fail(orig.num_cells_to_activate, sizeof(*orig.cells_to_activate));
  orig.cells_to_activate[0].nr_cellid = 123456;
  orig.cells_to_activate[0].nrpci = 1;
  orig.cells_to_activate[0].plmn.mcc = 12;
  orig.cells_to_activate[0].plmn.mnc = 123;
  orig.cells_to_activate[0].plmn.mnc_digit_length = 3;
  /* Cells to activate */
  if (orig.num_cells_to_activate) {
    /* SI_container */
    char *s = "test";
    int SI_container_length = strlen(s) + 1;
    orig.cells_to_activate[0].num_SI = 1;
    f1ap_sib_msg_t *SI_msg = &orig.cells_to_activate[0].SI_msg[0];
    SI_msg->SI_container.buf = malloc_or_fail(SI_container_length);
    memcpy(SI_msg->SI_container.buf, s, SI_container_length);
    SI_msg->SI_container.len = SI_container_length;
    SI_msg->SI_type = 7;
  }
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_setup_response(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_setup_resp_t decoded = {0};
  bool ret = decode_f1ap_setup_response(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_response(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_setup_response(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_response(): decoded message doesn't match\n");
  free_f1ap_setup_response(&decoded);

  f1ap_setup_resp_t cp = cp_f1ap_setup_response(&orig);
  ret = eq_f1ap_setup_response(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_response(): copied message doesn't match\n");
  free_f1ap_setup_response(&cp);
  free_f1ap_setup_response(&orig);

  // gNB-CU RRC Version with an unknown extension
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex(
        "400100400000030003002500000004001f20a22427226377cc700000007600104002030af08a44000000ef00033451de00aa000ad000009f2d0003"
        "9860af004e000200a2");
    f1ap_setup_resp_t msg = {0};
    AssertFatal(decode_f1ap_setup_response(pdu, &msg), "could not decode RRC Version with unknown extension\n");
    free_f1ap_setup_response(&msg);
    f1ap_msg_free(pdu);
  }
}

/**
 * @brief Test F1AP Setup Failure Encoding/Decoding
 */
static void test_f1ap_setup_failure(void)
{
  /* create message */
  f1ap_setup_failure_t orig = {
      .transaction_id = 2,
      .cause = 4,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_setup_failure(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_setup_failure_t decoded = {0};
  bool ret = decode_f1ap_setup_failure(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_failure(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_setup_failure(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_failure(): decoded message doesn't match\n");

  f1ap_setup_failure_t cp = cp_f1ap_setup_failure(&orig);
  ret = eq_f1ap_setup_failure(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_failure(): copied message doesn't match\n");
}

static void _test_f1ap_reset_msg(const f1ap_reset_t *orig)
{
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_reset(orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_reset_t decoded = {0};
  bool ret = decode_f1ap_reset(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_reset(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_reset(orig, &decoded);
  AssertFatal(ret, "eq_f1ap_reset(): decoded message doesn't match original\n");
  free_f1ap_reset(&decoded);

  f1ap_reset_t cp = cp_f1ap_reset(orig);
  ret = eq_f1ap_reset(orig, &cp);
  AssertFatal(ret, "eq_f1ap_reset(): copied message doesn't match original\n");
  free_f1ap_reset(&cp);

  printf("f1ap_reset successful\n");
}

/**
 * @brief Test F1AP Reset message where the entire F1 interface is reset
 */
static void test_f1ap_reset_all(void)
{
  f1ap_reset_t orig = {
      .transaction_id = 2,
      .cause = F1AP_CAUSE_TRANSPORT,
      .cause_value = 3, /* no type -> whatever */
      .reset_type = F1AP_RESET_ALL,
  };
  _test_f1ap_reset_msg(&orig);
  free_f1ap_reset(&orig);
}

/**
 * @brief Test F1AP Reset message where only some UEs are marked to reset
 */
static void test_f1ap_reset_part(void)
{
  f1ap_reset_t orig = {
      .transaction_id = 3,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3, /* no type -> whatever */
      .reset_type = F1AP_RESET_PART_OF_F1_INTERFACE,
      .num_ue_to_reset = 4,
  };
  orig.ue_to_reset = calloc_or_fail(orig.num_ue_to_reset, sizeof(*orig.ue_to_reset));
  orig.ue_to_reset[0].gNB_CU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[0].gNB_CU_ue_id));
  *orig.ue_to_reset[0].gNB_CU_ue_id = 10;
  orig.ue_to_reset[1].gNB_DU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[1].gNB_DU_ue_id));
  *orig.ue_to_reset[1].gNB_DU_ue_id = 11;
  orig.ue_to_reset[2].gNB_CU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[2].gNB_CU_ue_id));
  *orig.ue_to_reset[2].gNB_CU_ue_id = 12;
  orig.ue_to_reset[2].gNB_DU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[2].gNB_DU_ue_id));
  *orig.ue_to_reset[2].gNB_DU_ue_id = 13;
  // orig.ue_to_reset[3] intentionally empty (because it is allowed, both IDs are optional)
  _test_f1ap_reset_msg(&orig);
  free_f1ap_reset(&orig);
}

/**
 * @brief Test F1 reset ack with differently ack'd UEs ("generic ack" is
 * special case with no individual UE acked)
 */
static void test_f1ap_reset_ack(void)
{
  f1ap_reset_ack_t orig = {
    .transaction_id = 4,
    .num_ue_to_reset = 4,
  };
  orig.ue_to_reset = calloc_or_fail(orig.num_ue_to_reset, sizeof(*orig.ue_to_reset));
  orig.ue_to_reset[0].gNB_CU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[0].gNB_CU_ue_id));
  *orig.ue_to_reset[0].gNB_CU_ue_id = 10;
  orig.ue_to_reset[1].gNB_DU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[1].gNB_DU_ue_id));
  *orig.ue_to_reset[1].gNB_DU_ue_id = 11;
  orig.ue_to_reset[2].gNB_CU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[2].gNB_CU_ue_id));
  *orig.ue_to_reset[2].gNB_CU_ue_id = 12;
  orig.ue_to_reset[2].gNB_DU_ue_id = malloc_or_fail(sizeof(*orig.ue_to_reset[2].gNB_DU_ue_id));
  *orig.ue_to_reset[2].gNB_DU_ue_id = 13;
  // orig.ue_to_reset[3] intentionally empty (because it is allowed, both IDs are optional)

  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_reset_ack(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_reset_ack_t decoded = {0};
  bool ret = decode_f1ap_reset_ack(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_reset_ack(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_reset_ack(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_reset_ack(): decoded message doesn't match original\n");
  free_f1ap_reset_ack(&decoded);

  f1ap_reset_ack_t cp = cp_f1ap_reset_ack(&orig);
  ret = eq_f1ap_reset_ack(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_reset_ack(): copied message doesn't match original\n");
  free_f1ap_reset_ack(&cp);

  printf("f1ap_reset_ack successful\n");

  free_f1ap_reset_ack(&orig);
}

/**
 * @brief Test F1 gNB-DU Configuration Update
 */
static void test_f1ap_du_configuration_update(void)
{
  /* sys_info */
  uint8_t *mib = calloc_or_fail(3, sizeof(*mib));
  uint8_t *sib1 = calloc_or_fail(3, sizeof(*sib1));
  f1ap_gnb_du_system_info_t sys_info = {
      .mib_length = 3,
      .mib = mib,
      .sib1_length = 3,
      .sib1 = sib1,
  };
  /* measurement_timing_information modify */
  char *s = "1";
  int measurement_timing_config_len = strlen(s) + 1;
  uint8_t *measurement_timing_config_mod = calloc_or_fail(measurement_timing_config_len, sizeof(*measurement_timing_config_mod));
  memcpy((void *)measurement_timing_config_mod, s, measurement_timing_config_len);
  /* TAC modify */
  uint32_t *tac = calloc_or_fail(1, sizeof(*tac));
  *tac = 456;
  /* info modify */
  f1ap_served_cell_info_t info = {
      .mode = F1AP_MODE_TDD,
      .tdd.freqinfo.arfcn = 640000,
      .tdd.freqinfo.band = 78,
      .tdd.tbw.nrb = 66,
      .tdd.tbw.scs = 1,
      .measurement_timing_config_len = measurement_timing_config_len,
      .measurement_timing_config = measurement_timing_config_mod,
      .nr_cellid = 123456,
      .plmn.mcc = 1,
      .plmn.mnc = 1,
      .plmn.mnc_digit_length = 3,
      .tac = tac,
  };
  char *mtc2_data = "mtc2";
  uint8_t *mtc2 = (void*)strdup(mtc2_data);
  int mtc2_len = strlen(mtc2_data);
  f1ap_served_cell_info_t info2 = {
      .mode = F1AP_MODE_FDD,
      .fdd.ul_freqinfo.arfcn = 640000,
      .fdd.ul_freqinfo.band = 78,
      .fdd.dl_freqinfo.arfcn = 600000,
      .fdd.dl_freqinfo.band = 78,
      .fdd.ul_tbw.nrb = 66,
      .fdd.ul_tbw.scs = 1,
      .fdd.dl_tbw.nrb = 66,
      .fdd.dl_tbw.scs = 1,
      .measurement_timing_config_len = mtc2_len,
      .measurement_timing_config = mtc2,
      .nr_cellid = 123456,
      .plmn.mcc = 2,
      .plmn.mnc = 2,
      .plmn.mnc_digit_length = 2,
  };
  /* create message */
  f1ap_gnb_du_configuration_update_t orig = {
      .transaction_id = 2,
      .num_cells_to_add = 1,
      .num_cells_to_modify = 1,
      .num_cells_to_delete = 1,
      .num_status = 2,
  };
  /* Allocate memory for arrays */
  orig.cell_to_add = calloc_or_fail(orig.num_cells_to_add, sizeof(*orig.cell_to_add));
  orig.cell_to_modify = calloc_or_fail(orig.num_cells_to_modify, sizeof(*orig.cell_to_modify));
  orig.cell_to_delete = calloc_or_fail(orig.num_cells_to_delete, sizeof(*orig.cell_to_delete));
  orig.status = calloc_or_fail(orig.num_status, sizeof(*orig.status));

  /* Fill in the data */
  orig.cell_to_add[0].info = info2;
  orig.cell_to_modify[0].info = info;
  orig.cell_to_modify[0].old_nr_cellid = 1235UL;
  orig.cell_to_modify[0].old_plmn.mcc = 208;
  orig.cell_to_modify[0].old_plmn.mnc = 88;
  orig.cell_to_modify[0].old_plmn.mnc_digit_length = 2;
  orig.cell_to_delete[0].nr_cellid = 1234UL;
  orig.cell_to_delete[0].plmn.mcc = 1;
  orig.cell_to_delete[0].plmn.mnc = 1;
  orig.cell_to_delete[0].plmn.mnc_digit_length = 3;
  orig.status[0].nr_cellid = 542UL;
  orig.status[0].plmn.mcc = 1;
  orig.status[0].plmn.mnc = 2;
  orig.status[0].plmn.mnc_digit_length = 3;
  orig.status[0].service_state = F1AP_STATE_IN_SERVICE;
  orig.status[1].nr_cellid = 33UL;
  orig.status[1].plmn.mcc = 5;
  orig.status[1].plmn.mnc = 13;
  orig.status[1].plmn.mnc_digit_length = 2;
  orig.status[1].service_state = F1AP_STATE_OUT_OF_SERVICE;
  orig.cell_to_modify[0].sys_info = calloc_or_fail(1, sizeof(*orig.cell_to_modify[0].sys_info));
  *orig.cell_to_modify[0].sys_info = sys_info;
  orig.gNB_DU_ID = malloc_or_fail(sizeof(*orig.gNB_DU_ID));
  *orig.gNB_DU_ID = 12;

  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_du_configuration_update(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_gnb_du_configuration_update_t decoded = {0};
  bool ret = decode_f1ap_du_configuration_update(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_request(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_du_configuration_update(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_request(): decoded message doesn't match\n");
  free_f1ap_du_configuration_update(&decoded);

  f1ap_gnb_du_configuration_update_t cp = cp_f1ap_du_configuration_update(&orig);
  ret = eq_f1ap_du_configuration_update(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_request(): copied message doesn't match\n");
  free_f1ap_du_configuration_update(&cp);
  free_f1ap_du_configuration_update(&orig);

  // Served Cells To Add item with an unexpected IE ID
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex(
        "0003003c000002003a002f0000000800290000a22427226377cc700013000af08a0100090f590002710040277dba0000760400002d0a023801f8004e"
        "000200df");
    f1ap_gnb_du_configuration_update_t msg = {0};
    AssertFatal(!decode_f1ap_du_configuration_update(pdu, &msg), "decoded malformed Served Cells To Add item\n");
    free_f1ap_du_configuration_update(&msg);
    f1ap_msg_free(pdu);
  }

  // Served Cells To Modify item with an unexpected IE ID
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex(
        "000300809c000002003e00808e00003d00808700a22427226377cc71003f130af08ad134a0036200b2307601000349f7000167080003110000df10"
        "1aae4e0000816000039c00026a0003780000590000d80003bc0003b700022f00038d0000d9fff4830000a100037e00032100023a00027a00032100"
        "01000003ae0003d2000110100001ee0001ac00029910233402f6d2000000fb000400dd43cc004e000200b5");
    f1ap_gnb_du_configuration_update_t msg = {0};
    AssertFatal(!decode_f1ap_du_configuration_update(pdu, &msg), "decoded malformed Served Cells To Modify item\n");
    free_f1ap_du_configuration_update(&msg);
    f1ap_msg_free(pdu);
  }
}

/**
 * @brief Test F1 gNB-DU Configuration Update Acknowledge
 */
static void test_f1ap_du_configuration_update_acknowledge(void)
{
  // Create message
  f1ap_gnb_du_configuration_update_acknowledge_t orig = {
      .transaction_id = 5,
      .num_cells_to_activate = 1,
  };
  // Allocate memory for cells_to_activate array
  orig.cells_to_activate = calloc_or_fail(orig.num_cells_to_activate, sizeof(*orig.cells_to_activate));
  orig.cells_to_activate[0].nr_cellid = 987654321;
  orig.cells_to_activate[0].nrpci = 50;
  orig.cells_to_activate[0].plmn.mcc = 001;
  orig.cells_to_activate[0].plmn.mnc = 01;
  orig.cells_to_activate[0].plmn.mnc_digit_length = 2;
  orig.cells_to_activate[0].num_SI = 1;
  orig.cells_to_activate[0].SI_msg[0].SI_type = 7;
  orig.cells_to_activate[0].SI_msg[0].SI_container.len = 10;
  orig.cells_to_activate[0].SI_msg[0].SI_container.buf = malloc(sizeof(uint8_t) * 10);
  for (int i = 0; i < (int)orig.cells_to_activate[0].SI_msg[0].SI_container.len; i++) {
    orig.cells_to_activate[0].SI_msg[0].SI_container.buf[i] = i;
  }
  // ASN.1 enc/dec
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_du_configuration_update_acknowledge(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);
  // Decoding
  f1ap_gnb_du_configuration_update_acknowledge_t decoded = {0};
  bool ret = decode_f1ap_du_configuration_update_acknowledge(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_du_configuration_update_acknowledge(): could not decode message\n");
  f1ap_msg_free(f1dec);
  // Equality check
  ret = eq_f1ap_du_configuration_update_acknowledge(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_du_configuration_update_acknowledge(): decoded message doesn't match\n");
  free_f1ap_du_configuration_update_acknowledge(&decoded);
  // Deep copy
  f1ap_gnb_du_configuration_update_acknowledge_t cp = cp_f1ap_du_configuration_update_acknowledge(&orig);
  ret = eq_f1ap_du_configuration_update_acknowledge(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_du_configuration_update_acknowledge(): copied message doesn't match\n");
  // Free
  free_f1ap_du_configuration_update_acknowledge(&cp);
  free_f1ap_du_configuration_update_acknowledge(&orig);
}

/**
 * @brief Test F1 gNB-CU Configuration Update
 */
static void test_f1ap_cu_configuration_update(void)
{
  /* create message */
  f1ap_gnb_cu_configuration_update_t orig = {
      .transaction_id = 2,
      .num_cells_to_activate = 1,
  };
  // Allocate memory for cells_to_activate array
  orig.cells_to_activate = calloc_or_fail(orig.num_cells_to_activate, sizeof(*orig.cells_to_activate));
  orig.cells_to_activate[0].nr_cellid = 123456789;
  orig.cells_to_activate[0].nrpci = 100;
  orig.cells_to_activate[0].plmn.mcc = 001;
  orig.cells_to_activate[0].plmn.mnc = 01;
  orig.cells_to_activate[0].plmn.mnc_digit_length = 2;
  orig.cells_to_activate[0].num_SI = 1;
  orig.cells_to_activate[0].SI_msg[0].SI_type = 7;
  orig.cells_to_activate[0].SI_msg[0].SI_container.len = 10;
  orig.cells_to_activate[0].SI_msg[0].SI_container.buf = malloc(sizeof(uint8_t) * 10);
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_cu_configuration_update(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_gnb_cu_configuration_update_t decoded = {0};
  bool ret = decode_f1ap_cu_configuration_update(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_setup_request(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_cu_configuration_update(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_setup_request(): decoded message doesn't match\n");
  free_f1ap_cu_configuration_update(&decoded);

  f1ap_gnb_cu_configuration_update_t cp = cp_f1ap_cu_configuration_update(&orig);
  ret = eq_f1ap_cu_configuration_update(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_setup_request(): copied message doesn't match\n");
  free_f1ap_cu_configuration_update(&cp);
  free_f1ap_cu_configuration_update(&orig);
}

/**
 * @brief Test F1 gNB-CU Configuration Update Acknowledge
 */
static void test_f1ap_cu_configuration_update_acknowledge(void)
{
  // Create the original message
  f1ap_gnb_cu_configuration_update_acknowledge_t orig = {
      .transaction_id = 2,
      .num_cells_failed_to_be_activated = 1,
  };
  // Allocate memory for cells_failed_to_be_activated array
  orig.cells_failed_to_be_activated = calloc_or_fail(orig.num_cells_failed_to_be_activated, sizeof(*orig.cells_failed_to_be_activated));
  orig.cells_failed_to_be_activated[0].nr_cellid = 123456789;
  orig.cells_failed_to_be_activated[0].plmn.mcc = 001;
  orig.cells_failed_to_be_activated[0].plmn.mnc = 01;
  orig.cells_failed_to_be_activated[0].plmn.mnc_digit_length = 2;
  orig.cells_failed_to_be_activated[0].cause = F1AP_CAUSE_RADIO_NETWORK;
  // F1AP Enc/dec
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_cu_configuration_update_acknowledge(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);
  // Decoding
  f1ap_gnb_cu_configuration_update_acknowledge_t decoded = {0};
  bool ret = decode_f1ap_cu_configuration_update_acknowledge(f1dec, &decoded);
  AssertFatal(ret, "decode_f1ap_cu_configuration_update_acknowledge(): could not decode message\n");
  f1ap_msg_free(f1dec);
  // Equality check
  ret = eq_f1ap_cu_configuration_update_acknowledge(&orig, &decoded);
  AssertFatal(ret, "eq_f1ap_cu_configuration_update_acknowledge(): decoded message doesn't match\n");
  free_f1ap_cu_configuration_update_acknowledge(&decoded);
  // Deep copy
  f1ap_gnb_cu_configuration_update_acknowledge_t cp = cp_f1ap_cu_configuration_update_acknowledge(&orig);
  // Equality check
  ret = eq_f1ap_cu_configuration_update_acknowledge(&orig, &cp);
  AssertFatal(ret, "eq_f1ap_cu_configuration_update_acknowledge(): copied message doesn't match\n");
  free_f1ap_cu_configuration_update_acknowledge(&cp);
  free_f1ap_cu_configuration_update_acknowledge(&orig);

  // Cells Failed to be Activated item with an unexpected IE ID
  {
    F1AP_F1AP_PDU_t *pdu = f1ap_decode_hex("4004001d0000020001001000007ebf000a00a22427226377cc7030004e00020013");
    f1ap_gnb_cu_configuration_update_acknowledge_t msg = {0};
    AssertFatal(!decode_f1ap_cu_configuration_update_acknowledge(pdu, &msg),
                "decoded malformed Cells Failed to be Activated item\n");
    free_f1ap_cu_configuration_update_acknowledge(&msg);
    f1ap_msg_free(pdu);
  }
}

static byte_array_t get_test_ba(const char *s)
{
  byte_array_t ba = {
      .len = strlen(s) + 1, /* binary data: needs to include null-byte of strdup() */
      .buf = (uint8_t *) strdup(s),
  };
  return ba;
}

static byte_array_t *get_malloced_test_ba(const char *s)
{
  byte_array_t *ba = calloc_or_fail(1, sizeof(*ba));
  *ba = get_test_ba(s);
  return ba;
}

/** Minimal Flows-Mapped item: Non-Dynamic @p five_qi, @p qfi */
static f1ap_drb_flows_mapped_t f1ap_drb_nr_mapped_flow_template(const f1ap_arp_t *arp, int qfi, int five_qi)
{
  DevAssert(arp != NULL);
  f1ap_drb_flows_mapped_t f = {0};
  f.qfi = qfi;
  f.param.qos_type = NON_DYNAMIC;
  f.param.nondyn.fiveQI = five_qi;
  f.param.arp = *arp;
  return f;
}

static void test_f1ap_ue_context_setup_request()
{
  plmn_id_t plmn = { .mcc = 001, .mnc = 01, .mnc_digit_length = 2 };
  f1ap_ue_context_setup_req_t orig = {
    .gNB_CU_ue_id = 12,
    //.gNB_DU_ue_id below
    .plmn = plmn,
    .nr_cellid = 132,
    .servCellIndex = 1,
    //.cu_to_du_rrc_info below
    //.srbs below
    //.drbs below
    //gnb_du_ue_agg_mbr_ul below
  };
  _F1_MALLOC(orig.gNB_DU_ue_id, 13);
  orig.cu_to_du_rrc_info.cg_configinfo = get_malloced_test_ba("CG-CONFIGINFO");
  orig.cu_to_du_rrc_info.ue_cap = get_malloced_test_ba("UE CAP");
  orig.cu_to_du_rrc_info.meas_config = get_malloced_test_ba("UE MEASUREMENT CONFIGURATION");
  orig.cu_to_du_rrc_info.meas_timing_config = get_malloced_test_ba("MEASUREMENT TIMING CONFIGURATION");
  orig.cu_to_du_rrc_info.ho_prep_info = get_malloced_test_ba("PREPARATION DATA FOR HANDOVER");
  orig.srbs_len = 2;
  orig.srbs = calloc_or_fail(orig.srbs_len, sizeof(*orig.srbs));
  for (int i = 0; i < orig.srbs_len; ++i)
    orig.srbs[i].id = 1 + i;

  orig.drbs_len = 2;
  orig.drbs = calloc_or_fail(orig.drbs_len, sizeof(*orig.drbs));
  f1ap_drb_to_setup_t *drb1 = &orig.drbs[0];
  drb1->id = 4;
  drb1->qos_choice = F1AP_QOS_CHOICE_NR;
  f1ap_arp_t arp = { 1, MAY_TRIGGER_PREEMPTION, PREEMPTABLE, };
  drb1->nr.drb_qos.qos_type = NON_DYNAMIC;
  drb1->nr.drb_qos.nondyn.fiveQI = 2;
  drb1->nr.drb_qos.arp = arp;
  // DRB-level GBR from highest priority flow (flow[0] has GBR)
  drb1->nr.drb_qos.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  drb1->nr.drb_qos.gbr_qos_flow_information->dl.guaranteedFlowBitRate = 100000; // 100 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->dl.maximumFlowBitRate = 200000; // 200 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->ul.guaranteedFlowBitRate = 50000; // 50 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->ul.maximumFlowBitRate = 100000; // 100 Mbps
  drb1->nr.nssai = (nssai_t) {.sst = 1, .sd = 123};
  // Test case: Support multiple QoS flows
  drb1->nr.flows_len = 2; // Test with 2 QoS flows
  drb1->nr.flows = calloc_or_fail(drb1->nr.flows_len, sizeof(*drb1->nr.flows));
  drb1->nr.flows[0] = f1ap_drb_nr_mapped_flow_template(&arp, 3, 2);
  drb1->nr.flows[1] = f1ap_drb_nr_mapped_flow_template(&arp, 4, 8);
  drb1->nr.flows[0].param.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  *drb1->nr.flows[0].param.gbr_qos_flow_information =
      (gbr_qos_flow_information_t){
          .dl = {.guaranteedFlowBitRate = 100000, .maximumFlowBitRate = 200000},
          .ul = {.guaranteedFlowBitRate = 50000, .maximumFlowBitRate = 100000},
      };
  drb1->up_ul_tnl_len = 1;
  inet_pton(AF_INET, "192.168.40.23", &drb1->up_ul_tnl[0].tl_address);
  drb1->up_ul_tnl[0].teid = 0x11223344;
  drb1->rlc_mode = F1AP_RLC_MODE_UM_BIDIR;
  _F1_MALLOC(drb1->dl_pdcp_sn_len, F1AP_PDCP_SN_18B);
  _F1_MALLOC(drb1->ul_pdcp_sn_len, F1AP_PDCP_SN_18B);

  f1ap_drb_to_setup_t *drb2 = &orig.drbs[1];
  drb2->id = 12;
  drb2->qos_choice = F1AP_QOS_CHOICE_NR;
  drb2->nr.drb_qos.qos_type = DYNAMIC;
  f1ap_dynamic_5qi_t *dyn = &drb2->nr.drb_qos.dyn;
  dyn->prio = 4;
  dyn->pdb = 1000;
  dyn->per.scalar = 4;
  dyn->per.exponent = 8;
  _F1_MALLOC(dyn->delay_critical, true);
  _F1_MALLOC(dyn->avg_win, 3000);
  f1ap_arp_t arp2 = { 13, SHALL_NOT_TRIGGER_PREEMPTION, NOT_PREEMPTABLE, };
  drb2->nr.drb_qos.arp = arp2;
  // DRB-level GBR from highest priority flow (flow2[0] has GBR)
  drb2->nr.drb_qos.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  drb2->nr.drb_qos.gbr_qos_flow_information->dl.guaranteedFlowBitRate = 50000; // 50 Mbps
  drb2->nr.drb_qos.gbr_qos_flow_information->dl.maximumFlowBitRate = 150000; // 150 Mbps
  drb2->nr.drb_qos.gbr_qos_flow_information->ul.guaranteedFlowBitRate = 25000; // 25 Mbps
  drb2->nr.drb_qos.gbr_qos_flow_information->ul.maximumFlowBitRate = 75000; // 75 Mbps
  drb2->nr.nssai = (nssai_t) {.sst = 2, .sd = 0xffffff};
  drb2->nr.flows_len = 2;
  f1ap_drb_flows_mapped_t *flow2 = drb2->nr.flows = calloc_or_fail(drb2->nr.flows_len, sizeof(*flow2));
  flow2[0].qfi = 4;
  flow2[0].param.qos_type = DYNAMIC;
  flow2[0].param.dyn.prio = 4;
  flow2[0].param.dyn.pdb = 999;
  flow2[0].param.dyn.per.scalar = 4;
  flow2[0].param.dyn.per.exponent = 8;
  flow2[0].param.arp = arp2;
  // Add GBR information for Dynamic5QI GBR flow
  flow2[0].param.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  flow2[0].param.gbr_qos_flow_information->dl.guaranteedFlowBitRate = 50000; // 50 Mbps
  flow2[0].param.gbr_qos_flow_information->dl.maximumFlowBitRate = 150000; // 150 Mbps
  flow2[0].param.gbr_qos_flow_information->ul.guaranteedFlowBitRate = 25000; // 25 Mbps
  flow2[0].param.gbr_qos_flow_information->ul.maximumFlowBitRate = 75000; // 75 Mbps
  flow2[1].qfi = 5;
  flow2[1].param.qos_type = NON_DYNAMIC;
  flow2[1].param.nondyn.fiveQI = 3;
  flow2[1].param.arp = arp;
  drb2->up_ul_tnl_len = 2;
  inet_pton(AF_INET, "10.0.0.2", &drb2->up_ul_tnl[0].tl_address);
  drb2->up_ul_tnl[0].teid = 0x1234;
  inet_pton(AF_INET, "10.0.0.2", &drb2->up_ul_tnl[1].tl_address);
  drb2->up_ul_tnl[1].teid = 0x1235;
  drb2->rlc_mode = F1AP_RLC_MODE_AM;
  _F1_MALLOC(drb2->dl_pdcp_sn_len, F1AP_PDCP_SN_12B);
  _F1_MALLOC(drb2->ul_pdcp_sn_len, F1AP_PDCP_SN_12B);

  orig.rrc_container = get_malloced_test_ba("RRC container");

  _F1_MALLOC(orig.gnb_du_ue_agg_mbr_ul, 5LL * 1000 * 1000 * 1000);

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_setup_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_setup_req_t decoded = {0};
  bool ret = decode_ue_context_setup_req(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_setup_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_setup_req(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_setup_req(): decoded message doesn't match\n");
  char ip_ret[100];
  inet_ntop(AF_INET, &decoded.drbs[0].up_ul_tnl[0].tl_address, ip_ret, sizeof(ip_ret));
  printf("%s\n", ip_ret);
  inet_ntop(AF_INET, &decoded.drbs[1].up_ul_tnl[0].tl_address, ip_ret, sizeof(ip_ret));
  printf("%s\n", ip_ret);
  free_ue_context_setup_req(&decoded);

  f1ap_ue_context_setup_req_t cp = cp_ue_context_setup_req(&orig);
  ret = eq_ue_context_setup_req(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_setup_req(): copied message doesn't match\n");
  free_ue_context_setup_req(&orig);
  free_ue_context_setup_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_setup_request_simple()
{
  plmn_id_t plmn = { .mcc = 001, .mnc = 01, .mnc_digit_length = 2 };
  f1ap_ue_context_setup_req_t orig = {
    .gNB_CU_ue_id = 12,
    .plmn = plmn,
    .nr_cellid = 132,
    .servCellIndex = 1,
    //.cu_to_du_rrc_info: all fields optional, intentionally left empty
    // rest is optional and intentionally left empty
    // .gnb_du_ue_agg_mbr_ul is C-ifDRBSetup => no DRB => no IE
  };
  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_setup_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_setup_req_t decoded = {0};
  bool ret = decode_ue_context_setup_req(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_setup_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_setup_req(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_setup_req(): decoded message doesn't match\n");
  free_ue_context_setup_req(&decoded);

  f1ap_ue_context_setup_req_t cp = cp_ue_context_setup_req(&orig);
  ret = eq_ue_context_setup_req(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_setup_req(): copied message doesn't match\n");
  free_ue_context_setup_req(&orig);
  free_ue_context_setup_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_setup_response()
{
  f1ap_ue_context_setup_resp_t orig = {
    .gNB_CU_ue_id = 12,
    .gNB_DU_ue_id = 13,
    //.du_to_cu_rrc_info below
    //.crnti below
    //.drbs below
    //.srbs below
  };

  orig.du_to_cu_rrc_info.cell_group_config = get_test_ba("CELL GROUP CONFIG");
  orig.du_to_cu_rrc_info.meas_gap_config = get_malloced_test_ba("MGC");

  _F1_MALLOC(orig.crnti, 0x1234);

  orig.drbs_len = 2;
  orig.drbs = calloc_or_fail(orig.drbs_len, sizeof(*orig.drbs));
  f1ap_drb_setup_t *drb1 = &orig.drbs[0];
  drb1->id = 12;
  _F1_MALLOC(drb1->lcid, 12);
  drb1->up_dl_tnl_len = 2;
  inet_pton(AF_INET, "192.168.40.23", &drb1->up_dl_tnl[0].tl_address);
  drb1->up_dl_tnl[0].teid = 0x11223344;
  inet_pton(AF_INET, "192.168.40.23", &drb1->up_dl_tnl[1].tl_address);
  drb1->up_dl_tnl[1].teid = 0x11223345;
  f1ap_drb_setup_t *drb2 = &orig.drbs[1];
  drb2->id = 13;
  _F1_MALLOC(drb2->lcid, 13);
  drb2->up_dl_tnl_len = 2;
  inet_pton(AF_INET, "192.168.50.23", &drb2->up_dl_tnl[0].tl_address);
  drb2->up_dl_tnl[0].teid = 0x21223354;
  inet_pton(AF_INET, "192.168.50.23", &drb2->up_dl_tnl[1].tl_address);
  drb2->up_dl_tnl[1].teid = 0x21223355;

  orig.srbs_len = 2;
  orig.srbs = calloc_or_fail(orig.srbs_len, sizeof(*orig.srbs));
  for (int i = 0; i < orig.srbs_len; ++i) {
    orig.srbs[i].id = 1 + i;
    orig.srbs[i].lcid = 1+i+10;
  }

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_setup_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_setup_resp_t decoded = {0};
  bool ret = decode_ue_context_setup_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_setup_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_setup_resp(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_setup_resp(): decoded message doesn't match\n");
  free_ue_context_setup_resp(&decoded);

  f1ap_ue_context_setup_resp_t cp = cp_ue_context_setup_resp(&orig);
  ret = eq_ue_context_setup_resp(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_setup_resp(): copied message doesn't match\n");
  free_ue_context_setup_resp(&orig);
  free_ue_context_setup_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_setup_response_simple()
{
  f1ap_ue_context_setup_resp_t orig = {
    .gNB_CU_ue_id = 12,
    .gNB_DU_ue_id = 13,
    .du_to_cu_rrc_info.cell_group_config = get_test_ba("small cg"),
    // rest is optional and intentionally left empty
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_setup_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_setup_resp_t decoded = {0};
  bool ret = decode_ue_context_setup_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_setup_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_setup_resp(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_setup_resp(): decoded message doesn't match\n");
  free_ue_context_setup_resp(&decoded);

  f1ap_ue_context_setup_resp_t cp = cp_ue_context_setup_resp(&orig);
  ret = eq_ue_context_setup_resp(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_setup_resp(): copied message doesn't match\n");
  free_ue_context_setup_resp(&orig);
  free_ue_context_setup_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_modification_request()
{
  f1ap_ue_context_mod_req_t orig = {
    .gNB_CU_ue_id = 1111,
    .gNB_DU_ue_id = 2222,
    // rest below
  };
  plmn_id_t plmn = { .mcc = 999, .mnc = 101, .mnc_digit_length = 3 };
  _F1_MALLOC(orig.plmn, plmn);
  _F1_MALLOC(orig.nr_cellid, 1532);
  _F1_MALLOC(orig.servCellIndex, 3);

  f1ap_cu_to_du_rrc_info_t cu2du = {
    .cg_configinfo = get_malloced_test_ba("Modified"),
    .ue_cap = get_malloced_test_ba("UE forgot to send earlier"),
    .meas_config = get_malloced_test_ba("with updates from another cell"),
    .meas_timing_config = get_malloced_test_ba("measurement timing configuration"),
    .ho_prep_info = get_malloced_test_ba("prepare to ho soon!"),
  };
  _F1_MALLOC(orig.cu_to_du_rrc_info, cu2du);

  orig.rrc_container = get_malloced_test_ba("important RRC message!!");

  orig.srbs_len = 1;
  orig.srbs = calloc_or_fail(orig.srbs_len, sizeof(*orig.srbs));
  orig.srbs[0].id = 1;

  orig.drbs_len = 1;
  orig.drbs = calloc_or_fail(orig.drbs_len, sizeof(*orig.drbs));
  f1ap_drb_to_setup_t *drb1 = &orig.drbs[0];
  drb1->id = 4;
  drb1->qos_choice = F1AP_QOS_CHOICE_NR;
  f1ap_arp_t arp = { 2, SHALL_NOT_TRIGGER_PREEMPTION, NOT_PREEMPTABLE, };
  drb1->nr.drb_qos.qos_type = NON_DYNAMIC;
  drb1->nr.drb_qos.nondyn.fiveQI = 3;
  drb1->nr.drb_qos.arp = arp;
  // DRB-level GBR from highest priority flow (flow[0] has GBR)
  drb1->nr.drb_qos.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  drb1->nr.drb_qos.gbr_qos_flow_information->dl.guaranteedFlowBitRate = 80000; // 80 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->dl.maximumFlowBitRate = 180000; // 180 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->ul.guaranteedFlowBitRate = 40000; // 40 Mbps
  drb1->nr.drb_qos.gbr_qos_flow_information->ul.maximumFlowBitRate = 90000; // 90 Mbps
  drb1->nr.nssai = (nssai_t) {.sst = 2, .sd = 0xffffff};
  // Test case: Support multiple QoS flows
  drb1->nr.flows_len = 2; // Test with 2 QoS flows
  drb1->nr.flows = calloc_or_fail(drb1->nr.flows_len, sizeof(*drb1->nr.flows));
  drb1->nr.flows[0] = f1ap_drb_nr_mapped_flow_template(&arp, 5, 3);
  drb1->nr.flows[1] = f1ap_drb_nr_mapped_flow_template(&arp, 6, 7);
  drb1->nr.flows[0].param.gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));
  *drb1->nr.flows[0].param.gbr_qos_flow_information =
      (gbr_qos_flow_information_t){
          .dl = {.guaranteedFlowBitRate = 80000, .maximumFlowBitRate = 180000},
          .ul = {.guaranteedFlowBitRate = 40000, .maximumFlowBitRate = 90000},
      };
  drb1->up_ul_tnl_len = 1;
  inet_pton(AF_INET, "8.8.8.8", &drb1->up_ul_tnl[0].tl_address);
  drb1->up_ul_tnl[0].teid = 0x9876541;
  drb1->rlc_mode = F1AP_RLC_MODE_AM;
  _F1_MALLOC(drb1->dl_pdcp_sn_len, F1AP_PDCP_SN_12B);
  _F1_MALLOC(drb1->ul_pdcp_sn_len, F1AP_PDCP_SN_18B);

  orig.drbs_rel_len = 3;
  orig.drbs_rel = calloc_or_fail(orig.drbs_rel_len, sizeof(*orig.drbs_rel));
  orig.drbs_rel[0].id = 13;
  orig.drbs_rel[1].id = 4;
  orig.drbs_rel[2].id = 19;

  _F1_MALLOC(orig.status, LOWER_LAYERS_RESUME);
  _F1_MALLOC(orig.gNB_DU_Configuration_Query, true);

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_mod_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_mod_req_t decoded = {0};
  bool ret = decode_ue_context_mod_req(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_mod_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_mod_req(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_mod_req(): decoded message doesn't match\n");
  free_ue_context_mod_req(&decoded);

  f1ap_ue_context_mod_req_t cp = cp_ue_context_mod_req(&orig);
  ret = eq_ue_context_mod_req(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_mod_req(): copied message doesn't match\n");
  free_ue_context_mod_req(&orig);
  free_ue_context_mod_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_modification_request_simple()
{
  f1ap_ue_context_mod_req_t orig = {
    .gNB_CU_ue_id = 1111,
    .gNB_DU_ue_id = 2222,
    // rest is optional and intentionally left out
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_mod_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_mod_req_t decoded = {0};
  bool ret = decode_ue_context_mod_req(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_mod_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_mod_req(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_mod_req(): decoded message doesn't match\n");
  free_ue_context_mod_req(&decoded);

  f1ap_ue_context_mod_req_t cp = cp_ue_context_mod_req(&orig);
  ret = eq_ue_context_mod_req(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_mod_req(): copied message doesn't match\n");
  free_ue_context_mod_req(&orig);
  free_ue_context_mod_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_modification_response()
{
  f1ap_ue_context_mod_resp_t orig = {
    .gNB_CU_ue_id = 9876,
    .gNB_DU_ue_id = 5432,
    // rest below
  };

  f1ap_du_to_cu_rrc_info_t cu2du = {
    .cell_group_config = get_test_ba("update for the cellgroupconfig, extra long"),
    .meas_gap_config = get_malloced_test_ba("new and shiny MeAs GaP cOnFiG"),
  };
  _F1_MALLOC(orig.du_to_cu_rrc_info, cu2du);

  orig.drbs_len = 1;
  orig.drbs = calloc_or_fail(orig.drbs_len, sizeof(*orig.drbs));
  f1ap_drb_setup_t *drb1 = &orig.drbs[0];
  drb1->id = 4;
  _F1_MALLOC(drb1->lcid, 7);
  drb1->up_dl_tnl_len = 1;
  inet_pton(AF_INET, "127.0.0.1", &drb1->up_dl_tnl[0].tl_address);
  drb1->up_dl_tnl[0].teid = 0xcafe;

  orig.srbs_len = 2;
  orig.srbs = calloc_or_fail(orig.srbs_len, sizeof(*orig.srbs));
  orig.srbs[0].id = 1;
  orig.srbs[0].lcid = 1;
  orig.srbs[1].id = 2;
  orig.srbs[1].lcid = 2;

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_mod_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_mod_resp_t decoded = {0};
  bool ret = decode_ue_context_mod_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_mod_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_mod_resp(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_mod_resp(): decoded message doesn't match\n");
  free_ue_context_mod_resp(&decoded);

  f1ap_ue_context_mod_resp_t cp = cp_ue_context_mod_resp(&orig);
  ret = eq_ue_context_mod_resp(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_mod_resp(): copied message doesn't match\n");
  free_ue_context_mod_resp(&orig);
  free_ue_context_mod_resp(&cp);

  // DL UP TNL Information with a 1-byte transport layer address
  {
    F1AP_F1AP_PDU_t *pdu =
        f1ap_decode_hex("4007002d000003001d001400001c000f2100039051ad3f1300000af000011000288005c03451de7100290005c0efa2b230");
    f1ap_ue_context_mod_resp_t msg = {0};
    AssertFatal(!decode_ue_context_mod_resp(pdu, &msg), "decoded non-IPv4 transport layer address\n");
    free_ue_context_mod_resp(&msg);
    f1ap_msg_free(pdu);
  }

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_modification_response_simple()
{
  f1ap_ue_context_mod_resp_t orig = {
    .gNB_CU_ue_id = 9876,
    .gNB_DU_ue_id = 5432,
    // rest is optional and intentionally left out
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_mod_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_mod_resp_t decoded = {0};
  bool ret = decode_ue_context_mod_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_mod_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_mod_resp(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_mod_resp(): decoded message doesn't match\n");
  free_ue_context_mod_resp(&decoded);

  f1ap_ue_context_mod_resp_t cp = cp_ue_context_mod_resp(&orig);
  ret = eq_ue_context_mod_resp(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_mod_resp(): copied message doesn't match\n");
  free_ue_context_mod_resp(&orig);
  free_ue_context_mod_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_release_request()
{
  f1ap_ue_context_rel_req_t orig = {
    .gNB_CU_ue_id = 1111,
    .gNB_DU_ue_id = 2222,
    .cause = F1AP_CAUSE_MISC,
    .cause_value = 3,
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_rel_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_rel_req_t decoded = {0};
  bool ret = decode_ue_context_rel_req(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_rel_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_rel_req(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_rel_req(): decoded message doesn't match\n");
  free_ue_context_rel_req(&decoded);

  f1ap_ue_context_rel_req_t cp = cp_ue_context_rel_req(&orig);
  ret = eq_ue_context_rel_req(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_rel_req(): copied message doesn't match\n");
  free_ue_context_rel_req(&orig);
  free_ue_context_rel_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_release_command()
{
  f1ap_ue_context_rel_cmd_t orig = {
    .gNB_CU_ue_id = 1111,
    .gNB_DU_ue_id = 2222,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = 2,
  };

  orig.rrc_container = get_malloced_test_ba("some reject message");
  _F1_MALLOC(orig.srb_id, 1);
  _F1_MALLOC(orig.old_gNB_DU_ue_id, 3333);

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_rel_cmd(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_rel_cmd_t decoded = {0};
  bool ret = decode_ue_context_rel_cmd(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_rel_cmd(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_rel_cmd(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_rel_cmd(): decoded message doesn't match\n");
  free_ue_context_rel_cmd(&decoded);

  f1ap_ue_context_rel_cmd_t cp = cp_ue_context_rel_cmd(&orig);
  ret = eq_ue_context_rel_cmd(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_rel_cmd(): copied message doesn't match\n");
  free_ue_context_rel_cmd(&orig);
  free_ue_context_rel_cmd(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_ue_context_release_complete()
{
  f1ap_ue_context_rel_cplt_t orig = {
    .gNB_CU_ue_id = 1111,
    .gNB_DU_ue_id = 2222,
  };

  F1AP_F1AP_PDU_t *f1enc = encode_ue_context_rel_cplt(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_ue_context_rel_cplt_t decoded = {0};
  bool ret = decode_ue_context_rel_cplt(f1dec, &decoded);
  AssertFatal(ret, "decode_ue_context_rel_cplt(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_ue_context_rel_cplt(&orig, &decoded);
  AssertFatal(ret, "eq_ue_context_rel_cplt(): decoded message doesn't match\n");
  free_ue_context_rel_cplt(&decoded);

  f1ap_ue_context_rel_cplt_t cp = cp_ue_context_rel_cplt(&orig);
  ret = eq_ue_context_rel_cplt(&orig, &cp);
  AssertFatal(ret, "eq_ue_context_rel_cplt(): copied message doesn't match\n");
  free_ue_context_rel_cplt(&orig);
  free_ue_context_rel_cplt(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_information_request()
{
  f1ap_positioning_information_req_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_information_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_information_req_t decoded = {0};
  bool ret = decode_positioning_information_req(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_information_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_information_req(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_information_req(): decoded message doesn't match\n");
  free_positioning_information_req(&decoded);

  f1ap_positioning_information_req_t cp = cp_positioning_information_req(&orig);
  ret = eq_positioning_information_req(&orig, &cp);
  AssertFatal(ret, "eq_positioning_information_req(): copied message doesn't match\n");
  free_positioning_information_req(&orig);
  free_positioning_information_req(&cp);

  printf("%s() successful\n", __func__);
}

static void fill_srs_carrier_list(f1ap_srs_carrier_list_t *srs_carrier_list)
{
  uint32_t srs_carrier_list_len = 2;
  srs_carrier_list->srs_carrier_list_length = srs_carrier_list_len;
  srs_carrier_list->srs_carrier_list_item = calloc_or_fail(srs_carrier_list_len, sizeof(*srs_carrier_list->srs_carrier_list_item));
  for (int i = 0; i < srs_carrier_list_len; i++) {
    f1ap_srs_carrier_list_item_t *srs_carrier_list_item = &srs_carrier_list->srs_carrier_list_item[i];
    // pointA
    srs_carrier_list_item->pointA = 87641 + i;

    // Uplink Channel BW-PerSCS-List
    f1ap_uplink_channel_bw_per_scs_list_t *uplink_channel_bw_per_scs_list = &srs_carrier_list_item->uplink_channel_bw_per_scs_list;

    uint32_t scs_specific_carrier_list_length = 3;
    uplink_channel_bw_per_scs_list->scs_specific_carrier_list_length = scs_specific_carrier_list_length;
    uplink_channel_bw_per_scs_list->scs_specific_carrier =
        calloc_or_fail(scs_specific_carrier_list_length, sizeof(*uplink_channel_bw_per_scs_list->scs_specific_carrier));
    for (int j = 0; j < scs_specific_carrier_list_length; j++) {
      f1ap_scs_specific_carrier_t *scs_specific_carrier = &uplink_channel_bw_per_scs_list->scs_specific_carrier[j];
      // offset to carrier
      scs_specific_carrier->offset_to_carrier = 100 + i + j;
      // subcarrier spacing
      scs_specific_carrier->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_30KHZ;
      // carrier bandwidth
      scs_specific_carrier->carrier_bandwidth = 106 + i + j;
    }

    // Active UL BWP
    f1ap_active_ul_bwp_t *active_ul_bwp = &srs_carrier_list_item->active_ul_bwp;

    // location and bandwidth
    active_ul_bwp->location_and_bandwidth = 2775 + i;
    // subcarrier spacing
    active_ul_bwp->subcarrier_spacing = F1AP_SUBCARRIER_SPACING_30KHZ;
    // cyclic prefix
    active_ul_bwp->cyclic_prefix = F1AP_CP_TYPE_NORMAL;
    // Tx Direct Current Location
    active_ul_bwp->tx_direct_current_location = 10 + i;

    // SRS Config
    f1ap_srs_config_t *sRSConfig = &active_ul_bwp->srs_config;

    // srs_resource_list
    sRSConfig->srs_resource_list = calloc_or_fail(1, sizeof(*sRSConfig->srs_resource_list));
    f1ap_srs_resource_list_t *srs_resource_list = sRSConfig->srs_resource_list;
    uint32_t srs_resource_list_length = 3;
    srs_resource_list->srs_resource_list_length = srs_resource_list_length;
    srs_resource_list->srs_resource = calloc_or_fail(srs_resource_list_length, sizeof(*srs_resource_list->srs_resource));

    // SRS resource config 1 : periodic
    f1ap_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[0];
    srs_resource->srs_resource_id = 22;
    srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N2;
    srs_resource->transmission_comb.present = F1AP_TRANSMISSION_COMB_PR_N2;
    srs_resource->transmission_comb.choice.n2.comb_offset_n2 = 1;
    srs_resource->transmission_comb.choice.n2.cyclic_shift_n2 = 3;
    srs_resource->start_position = 8;
    srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N4;
    srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF2;
    srs_resource->freq_domain_position = 32;
    srs_resource->freq_domain_shift = 108;
    srs_resource->c_srs = 31;
    srs_resource->b_srs = 1;
    srs_resource->b_hop = 3;
    srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
    srs_resource->resource_type.present = F1AP_RESOURCE_TYPE_PR_PERIODIC;

    f1ap_resource_type_periodic_t *periodic = &srs_resource->resource_type.choice.periodic;
    periodic->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT16;
    periodic->offset = 1000;
    srs_resource->sequence_id = 18;

    // SRS resource config 2 : semi-persistent
    srs_resource = &srs_resource_list->srs_resource[1];
    srs_resource->srs_resource_id = 24;
    srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N4;
    srs_resource->transmission_comb.present = F1AP_TRANSMISSION_COMB_PR_N4;
    srs_resource->transmission_comb.choice.n2.comb_offset_n2 = 3;
    srs_resource->transmission_comb.choice.n2.cyclic_shift_n2 = 5;
    srs_resource->start_position = 8;
    srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N2;
    srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF2;
    srs_resource->freq_domain_position = 32;
    srs_resource->freq_domain_shift = 108;
    srs_resource->c_srs = 31;
    srs_resource->b_srs = 1;
    srs_resource->b_hop = 3;
    srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
    srs_resource->resource_type.present = F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT;

    srs_resource->resource_type.present = F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT;
    f1ap_resource_type_semi_persistent_t *semi_persistent = &srs_resource->resource_type.choice.semi_persistent;
    semi_persistent->periodicity = F1AP_SRS_RESOURCE_TYPE_PERIODICITY_SLOT40;
    semi_persistent->offset = 1000;
    srs_resource->sequence_id = 12;

    // SRS resource config 2 : aperiodic
    srs_resource = &srs_resource_list->srs_resource[2];
    srs_resource->srs_resource_id = 26;
    srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N2;
    srs_resource->transmission_comb.present = F1AP_TRANSMISSION_COMB_PR_N2;
    srs_resource->transmission_comb.choice.n2.comb_offset_n2 = 1;
    srs_resource->transmission_comb.choice.n2.cyclic_shift_n2 = 3;
    srs_resource->start_position = 8;
    srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N4;
    srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF2;
    srs_resource->freq_domain_position = 32;
    srs_resource->freq_domain_shift = 108;
    srs_resource->c_srs = 31;
    srs_resource->b_srs = 1;
    srs_resource->b_hop = 3;
    srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
    srs_resource->resource_type.present = F1AP_RESOURCE_TYPE_PR_APERIODIC;
    // set aperiodic = 0 to enable
    srs_resource->resource_type.choice.aperiodic = 0;
    srs_resource->sequence_id = 10;

    // optional: pos_srs_resource_list
    sRSConfig->pos_srs_resource_list = calloc_or_fail(1, sizeof(*sRSConfig->pos_srs_resource_list));
    f1ap_pos_srs_resource_list_t *pos_srs_resource_list = sRSConfig->pos_srs_resource_list;
    uint32_t pos_srs_resource_list_length = 3;
    pos_srs_resource_list->pos_srs_resource_list_length = pos_srs_resource_list_length;
    pos_srs_resource_list->pos_srs_resource_item =
        calloc_or_fail(pos_srs_resource_list_length, sizeof(*pos_srs_resource_list->pos_srs_resource_item));

    // periodic
    f1ap_pos_srs_resource_item_t *pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[0];
    pos_srs_resource->srs_pos_resource_id = 12;
    pos_srs_resource->transmission_comb_pos.present = F1AP_TRANSMISSION_COMB_POS_PR_N2;
    pos_srs_resource->transmission_comb_pos.choice.n2.comb_offset_n2 = 1;
    pos_srs_resource->transmission_comb_pos.choice.n2.cyclic_shift_n2 = 4;
    pos_srs_resource->start_position = 9;
    pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N8;
    pos_srs_resource->freq_domain_shift = 100;
    pos_srs_resource->c_srs = 20;
    pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
    pos_srs_resource->resource_type_pos.present = F1AP_RESOURCE_TYPE_POS_PR_PERIODIC;
    pos_srs_resource->resource_type_pos.choice.periodic.periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5;
    pos_srs_resource->resource_type_pos.choice.periodic.offset = 10;
    pos_srs_resource->sequence_id = 1;

    // semi-persistent
    pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[1];
    pos_srs_resource->srs_pos_resource_id = 13;
    pos_srs_resource->transmission_comb_pos.present = F1AP_TRANSMISSION_COMB_POS_PR_N4;
    pos_srs_resource->transmission_comb_pos.choice.n4.comb_offset_n4 = 3;
    pos_srs_resource->transmission_comb_pos.choice.n4.cyclic_shift_n4 = 5;
    pos_srs_resource->start_position = 12;
    pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N12;
    pos_srs_resource->freq_domain_shift = 100;
    pos_srs_resource->c_srs = 20;
    pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
    pos_srs_resource->resource_type_pos.present = F1AP_RESOURCE_TYPE_POS_PR_SEMI_PERSISTENT;
    pos_srs_resource->resource_type_pos.choice.semi_persistent.periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT32;
    pos_srs_resource->resource_type_pos.choice.semi_persistent.offset = 5;
    pos_srs_resource->sequence_id = 2;

    // aperiodic
    pos_srs_resource = &pos_srs_resource_list->pos_srs_resource_item[2];
    pos_srs_resource->srs_pos_resource_id = 14;
    pos_srs_resource->transmission_comb_pos.present = F1AP_TRANSMISSION_COMB_POS_PR_N8;
    pos_srs_resource->transmission_comb_pos.choice.n8.comb_offset_n8 = 5;
    pos_srs_resource->transmission_comb_pos.choice.n8.cyclic_shift_n8 = 2;
    pos_srs_resource->start_position = 9;
    pos_srs_resource->nr_of_symbols = F1AP_SRS_RESOURCE_ITEM_NUMBER_OF_SYMBOLS_N4;
    pos_srs_resource->freq_domain_shift = 100;
    pos_srs_resource->c_srs = 20;
    pos_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
    pos_srs_resource->resource_type_pos.present = F1AP_RESOURCE_TYPE_POS_PR_APERIODIC;
    pos_srs_resource->resource_type_pos.choice.periodic.periodicity = F1AP_SRS_RESOURCE_TYPE_POS_PERIODICITY_SLOT5;
    pos_srs_resource->resource_type_pos.choice.aperiodic.slot_offset = 31;
    pos_srs_resource->sequence_id = 3;

    // optional: srs_resource_set_list
    sRSConfig->srs_resource_set_list = calloc_or_fail(1, sizeof(*sRSConfig->srs_resource_set_list));
    f1ap_srs_resource_set_list_t *srs_resource_set_list = sRSConfig->srs_resource_set_list;
    uint32_t srs_resource_set_list_length = 3;
    srs_resource_set_list->srs_resource_set_list_length = srs_resource_set_list_length;
    srs_resource_set_list->srs_resource_set =
        calloc_or_fail(srs_resource_set_list_length, sizeof(*srs_resource_set_list->srs_resource_set));

    // periodic
    f1ap_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[0];
    srs_resource_set->srs_resource_set_id = 0;
    uint8_t srs_resource_id_list_length = 5;
    srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
    srs_resource_set->srs_resource_id_list.srs_resource_id =
        calloc_or_fail(srs_resource_id_list_length, sizeof(*srs_resource_set->srs_resource_id_list.srs_resource_id));
    for (int k = 0; k < srs_resource_id_list_length; k++) {
      srs_resource_set->srs_resource_id_list.srs_resource_id[k] = k;
    }
    srs_resource_set->resource_set_type.present = F1AP_RESOURCE_SET_TYPE_PR_PERIODIC;
    // set periodic = 0 to enable
    srs_resource_set->resource_set_type.choice.periodic = 0;

    // semi-persistent
    srs_resource_set = &srs_resource_set_list->srs_resource_set[1];
    srs_resource_set->srs_resource_set_id = 1;
    srs_resource_id_list_length = 5;
    srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
    srs_resource_set->srs_resource_id_list.srs_resource_id =
        calloc_or_fail(srs_resource_id_list_length, sizeof(srs_resource_set->srs_resource_id_list.srs_resource_id));
    for (int k = 0; k < srs_resource_id_list_length; k++) {
      srs_resource_set->srs_resource_id_list.srs_resource_id[k] = k;
    }
    srs_resource_set->resource_set_type.present = F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
    // set semi_persistent = 0 to enable
    srs_resource_set->resource_set_type.choice.semi_persistent = 0;

    // aperiodic
    srs_resource_set = &srs_resource_set_list->srs_resource_set[2];
    srs_resource_set->srs_resource_set_id = 2;
    srs_resource_id_list_length = 5;
    srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
    srs_resource_set->srs_resource_id_list.srs_resource_id =
        calloc_or_fail(srs_resource_id_list_length, sizeof(*srs_resource_set->srs_resource_id_list.srs_resource_id));
    for (int k = 0; k < srs_resource_id_list_length; k++) {
      srs_resource_set->srs_resource_id_list.srs_resource_id[k] = k;
    }

    srs_resource_set->resource_set_type.present = F1AP_RESOURCE_SET_TYPE_PR_APERIODIC;
    srs_resource_set->resource_set_type.choice.aperiodic.srs_resource_trigger = 2;
    srs_resource_set->resource_set_type.choice.aperiodic.slot_offset = 20;

    // optional: pos_srs_resource_set_list
    sRSConfig->pos_srs_resource_set_list = calloc_or_fail(1, sizeof(*sRSConfig->pos_srs_resource_set_list));
    f1ap_pos_srs_resource_set_list_t *pos_srs_resource_set_list = sRSConfig->pos_srs_resource_set_list;
    uint32_t pos_srs_resource_set_list_length = 3;
    pos_srs_resource_set_list->pos_srs_resource_set_list_length = pos_srs_resource_set_list_length;
    pos_srs_resource_set_list->pos_srs_resource_set_item =
        calloc_or_fail(pos_srs_resource_set_list_length, sizeof(*pos_srs_resource_set_list->pos_srs_resource_set_item));

    // periodic
    f1ap_pos_srs_resource_set_item_t *pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[0];
    pos_srs_resource_set->pos_srs_resource_set_id = 0;
    uint8_t pos_srs_resource_id_list_length = 5;
    pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length = pos_srs_resource_id_list_length;
    pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id =
        calloc_or_fail(pos_srs_resource_id_list_length,
                       sizeof(*pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id));
    for (int k = 0; k < pos_srs_resource_id_list_length; k++) {
      pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[k] = k;
    }
    pos_srs_resource_set->pos_resource_set_type.present = F1AP_POS_RESOURCE_SET_TYPE_PR_PERIODIC;
    // set periodic = 0 to enable
    pos_srs_resource_set->pos_resource_set_type.choice.periodic = 0;

    // semi-persistent
    pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[1];
    pos_srs_resource_set->pos_srs_resource_set_id = 1;
    pos_srs_resource_id_list_length = 5;
    pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length = pos_srs_resource_id_list_length;
    pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id =
        calloc_or_fail(pos_srs_resource_id_list_length,
                       sizeof(*pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id));
    for (int k = 0; k < pos_srs_resource_id_list_length; k++) {
      pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[k] = k;
    }
    pos_srs_resource_set->pos_resource_set_type.present = F1AP_POS_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
    // set semi_persistent = 0 to enable
    pos_srs_resource_set->pos_resource_set_type.choice.semi_persistent = 0;

    // aperiodic
    pos_srs_resource_set = &pos_srs_resource_set_list->pos_srs_resource_set_item[2];
    pos_srs_resource_set->pos_srs_resource_set_id = 2;
    pos_srs_resource_id_list_length = 5;
    pos_srs_resource_set->pos_srs_resource_id_list.pos_srs_resource_id_list_length = pos_srs_resource_id_list_length;
    pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id =
        calloc_or_fail(pos_srs_resource_id_list_length,
                       sizeof(*pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id));
    for (int k = 0; k < pos_srs_resource_id_list_length; k++) {
      pos_srs_resource_set->pos_srs_resource_id_list.srs_pos_resource_id[k] = k;
    }
    pos_srs_resource_set->pos_resource_set_type.present = F1AP_POS_RESOURCE_SET_TYPE_PR_APERIODIC;
    pos_srs_resource_set->pos_resource_set_type.choice.srs_resource = 3;
  }
}

static void test_f1ap_positioning_information_response()
{
  f1ap_positioning_information_resp_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
  };

  // SRS configuration
  orig.srs_configuration = calloc_or_fail(1, sizeof(*orig.srs_configuration));
  f1ap_srs_carrier_list_t *srs_carrier_list = &orig.srs_configuration->srs_carrier_list;
  fill_srs_carrier_list(srs_carrier_list);

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_information_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_information_resp_t decoded = {0};
  bool ret = decode_positioning_information_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_information_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_information_resp(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_information_resp(): decoded message doesn't match\n");
  free_positioning_information_resp(&decoded);

  f1ap_positioning_information_resp_t cp = cp_positioning_information_resp(&orig);
  ret = eq_positioning_information_resp(&orig, &cp);
  AssertFatal(ret, "eq_positioning_information_resp(): copied message doesn't match\n");
  free_positioning_information_resp(&orig);
  free_positioning_information_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_information_failure()
{
  f1ap_positioning_information_failure_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3,
  };

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_information_failure(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_information_failure_t decoded = {0};
  bool ret = decode_positioning_information_failure(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_information_failure(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_information_failure(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_information_failure(): decoded message doesn't match\n");
  free_positioning_information_failure(&decoded);

  f1ap_positioning_information_failure_t cp = cp_positioning_information_failure(&orig);
  ret = eq_positioning_information_failure(&orig, &cp);
  AssertFatal(ret, "eq_positioning_information_failure(): copied message doesn't match\n");
  free_positioning_information_failure(&orig);
  free_positioning_information_failure(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_activation_request_msg(f1ap_positioning_activation_req_t *orig)
{
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_activation_req(orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_activation_req_t decoded = {0};
  bool ret = decode_positioning_activation_req(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_activation_request(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_activation_req(orig, &decoded);
  AssertFatal(ret, "eq_positioning_activation_request(): decoded message doesn't match\n");
  free_positioning_activation_req(&decoded);

  f1ap_positioning_activation_req_t cp = cp_positioning_activation_req(orig);
  ret = eq_positioning_activation_req(orig, &cp);
  AssertFatal(ret, "eq_positioning_activation_request(): copied message doesn't match\n");
  free_positioning_activation_req(&cp);
  free_positioning_activation_req(orig);
}

static void test_f1ap_positioning_activation_request()
{
  /* semipersistent srs test*/
  f1ap_positioning_activation_req_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
      .srs_type.present = F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS,
  };

  orig.srs_type.choice.srs_resource_set_id = calloc_or_fail(1, sizeof(*orig.srs_type.choice.srs_resource_set_id));
  *orig.srs_type.choice.srs_resource_set_id = 1;
  test_f1ap_positioning_activation_request_msg(&orig);

  /* aperiodic srs test*/
  orig.gNB_CU_ue_id = 12;
  orig.gNB_DU_ue_id = 19;
  orig.srs_type.present = F1AP_SRS_TYPE_PR_APERIODICSRS;
  orig.srs_type.choice.aperiodic = calloc_or_fail(1, sizeof(*orig.srs_type.choice.aperiodic));
  // set aperiodic = 0 to enable
  *orig.srs_type.choice.aperiodic = 0;
  test_f1ap_positioning_activation_request_msg(&orig);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_activation_response()
{
  f1ap_positioning_activation_resp_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
      // rest is optional and intentionally left empty
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_activation_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_activation_resp_t decoded = {0};
  bool ret = decode_positioning_activation_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_activation_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_activation_resp(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_activation_resp(): decoded message doesn't match\n");
  free_positioning_activation_resp(&decoded);

  f1ap_positioning_activation_resp_t cp = cp_positioning_activation_resp(&orig);
  ret = eq_positioning_activation_resp(&orig, &cp);
  AssertFatal(ret, "eq_positioning_activation_resp(): copied message doesn't match\n");
  free_positioning_activation_resp(&orig);
  free_positioning_activation_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_activation_failure()
{
  f1ap_positioning_activation_failure_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_activation_failure(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_activation_failure_t decoded = {0};
  bool ret = decode_positioning_activation_failure(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_activation_failure(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_activation_failure(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_activation_failure(): decoded message doesn't match\n");
  free_positioning_activation_failure(&decoded);

  f1ap_positioning_activation_failure_t cp = cp_positioning_activation_failure(&orig);
  ret = eq_positioning_activation_failure(&orig, &cp);
  AssertFatal(ret, "eq_positioning_activation_failure(): copied message doesn't match\n");
  free_positioning_activation_failure(&orig);
  free_positioning_activation_failure(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_deactivation_msg(f1ap_positioning_deactivation_t *orig)
{
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_deactivation(orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_deactivation_t decoded = {0};
  bool ret = decode_positioning_deactivation(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_deactivation(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_deactivation(orig, &decoded);
  AssertFatal(ret, "eq_positioning_deactivation(): decoded message doesn't match\n");
  free_positioning_deactivation(&decoded);

  f1ap_positioning_deactivation_t cp = cp_positioning_deactivation(orig);
  ret = eq_positioning_deactivation(orig, &cp);
  AssertFatal(ret, "eq_positioning_deactivation(): copied message doesn't match\n");
  free_positioning_deactivation(orig);
  free_positioning_deactivation(&cp);
}

static void test_f1ap_positioning_deactivation()
{
  /* Abort Transmission SRS Resource Set ID test*/
  f1ap_positioning_deactivation_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
      .abort_transmission.present = F1AP_ABORT_TRANSMISSION_PR_SRSRESOURCESETID,
      .abort_transmission.choice.srs_resource_set_id = 10,
  };
  test_f1ap_positioning_deactivation_msg(&orig);

  /* Abort Transmission release ALL test*/
  orig.gNB_CU_ue_id = 12;
  orig.gNB_DU_ue_id = 19;
  orig.abort_transmission.present = F1AP_ABORT_TRANSMISSION_PR_RELEASEALL;
  orig.abort_transmission.choice.release_all = true;
  test_f1ap_positioning_deactivation_msg(&orig);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_information_update()
{
  f1ap_positioning_information_update_t orig = {
      .gNB_CU_ue_id = 12,
      .gNB_DU_ue_id = 19,
  };

  // SRS configuration
  orig.srs_configuration = calloc_or_fail(1, sizeof(*orig.srs_configuration));
  f1ap_srs_carrier_list_t *srs_carrier_list = &orig.srs_configuration->srs_carrier_list;
  fill_srs_carrier_list(srs_carrier_list);

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_information_update(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_information_update_t decoded = {0};
  bool ret = decode_positioning_information_update(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_information_update(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_information_update(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_information_update(): decoded message doesn't match\n");
  free_positioning_information_update(&decoded);

  f1ap_positioning_information_update_t cp = cp_positioning_information_update(&orig);
  ret = eq_positioning_information_update(&orig, &cp);
  AssertFatal(ret, "eq_positioning_information_update(): copied message doesn't match\n");
  free_positioning_information_update(&orig);
  free_positioning_information_update(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_trp_information_request()
{
  f1ap_trp_information_req_t orig = {
      .transaction_id = 12,
      .has_trp_list = true,
  };

  uint32_t trp_list_length = 4;
  orig.trp_list.trp_list_length = trp_list_length;
  orig.trp_list.trp_list_item = calloc_or_fail(trp_list_length, sizeof(*orig.trp_list.trp_list_item));
  for (int i = 0; i < trp_list_length; i++) {
    orig.trp_list.trp_list_item[i].trp_id = i + 1;
  }
  uint8_t trp_info_type_list_length = 2;
  f1ap_trp_information_type_list_t *trp_information_type_list = &orig.trp_information_type_list;
  trp_information_type_list->trp_information_type_list_length = trp_info_type_list_length;
  trp_information_type_list->trp_information_type_item =
      calloc_or_fail(trp_info_type_list_length, sizeof(*trp_information_type_list->trp_information_type_item));
  trp_information_type_list->trp_information_type_item[0] = F1AP_TRP_INFORMATION_TYPE_ITEM_NR_ARFCN;
  trp_information_type_list->trp_information_type_item[1] = F1AP_TRP_INFORMATION_TYPE_ITEM_PRS_CONFIG;

  F1AP_F1AP_PDU_t *f1enc = encode_trp_information_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_trp_information_req_t decoded = {0};
  bool ret = decode_trp_information_req(f1dec, &decoded);
  AssertFatal(ret, "decode_trp_information_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_trp_information_req(&orig, &decoded);
  AssertFatal(ret, "eq_trp_information_req(): decoded message doesn't match\n");
  free_trp_information_req(&decoded);

  f1ap_trp_information_req_t cp = cp_trp_information_req(&orig);
  ret = eq_trp_information_req(&orig, &cp);
  AssertFatal(ret, "eq_trp_information_req(): copied message doesn't match\n");
  free_trp_information_req(&orig);
  free_trp_information_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_trp_information_response()
{
  f1ap_trp_information_resp_t orig = {
      .transaction_id = 12,
  };

  orig.trp_information_list.trp_information_item_length = 3;
  uint32_t trp_info_item_len = orig.trp_information_list.trp_information_item_length;
  orig.trp_information_list.trp_information_item =
      calloc_or_fail(trp_info_item_len, sizeof(*orig.trp_information_list.trp_information_item));

  uint8_t trp_info_type_resp_item_len = 3;
  for (int i = 0; i < trp_info_item_len; i++) {
    f1ap_trp_information_t *tRPInformation = &orig.trp_information_list.trp_information_item[i];
    tRPInformation->trp_information_type_response_list.trp_information_type_response_item_length = trp_info_type_resp_item_len;
    tRPInformation->trp_id = 11 + i;
    f1ap_trp_information_type_response_item_t *trp_info_type_resp_item =
        calloc_or_fail(trp_info_type_resp_item_len, sizeof(*trp_info_type_resp_item));
    tRPInformation->trp_information_type_response_list.trp_information_type_response_item = trp_info_type_resp_item;

    // nrPCI
    trp_info_type_resp_item[0].present = F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PCI_NR;
    trp_info_type_resp_item[0].choice.pci_nr = 123 + i;

    // nG_RAN_CGI
    trp_info_type_resp_item[1].present = F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NG_RAN_CGI;
    trp_info_type_resp_item[1].choice.ng_ran_cgi.plmn.mcc = 208;
    trp_info_type_resp_item[1].choice.ng_ran_cgi.plmn.mnc = 92 + i;
    trp_info_type_resp_item[1].choice.ng_ran_cgi.plmn.mnc_digit_length = 2;
    trp_info_type_resp_item[1].choice.ng_ran_cgi.nr_cellid = 117 + i;

    // geographicalCoordinates
    trp_info_type_resp_item[2].present = F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_GEOGRAPHICALCOORDINATES;
    f1ap_geographical_coordinates_t *geographical_coordinates = &trp_info_type_resp_item[2].choice.geographical_coordinates;
    f1ap_trp_position_definition_type_t *tRPPositionDefinitionType = &geographical_coordinates->trp_position_definition_type;
    // referenced
    tRPPositionDefinitionType->present = F1AP_TRP_POSITION_DEFINITION_TYPE_PR_REFERENCED;
    f1ap_trp_position_referenced_t *referenced = &tRPPositionDefinitionType->choice.referenced;
    // coordinate ID
    referenced->reference_point.present = F1AP_REFERENCE_POINT_PR_COORDINATEID;
    referenced->reference_point.choice.coordinate_id = 2;
    f1ap_trp_reference_point_type_t *referencePointType = &referenced->reference_point_type;
    // relative cartesian
    referencePointType->present = F1AP_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_CARTESIAN;
    f1ap_relative_cartesian_location_t *trp_pos_cart = &referencePointType->choice.trp_position_relative_cartesian;
    // 0 = millimeter
    trp_pos_cart->xyz_unit = 0;
    // random reference cartesian coordinates in millimeter
    trp_pos_cart->xvalue = 7100 + i * 2;
    trp_pos_cart->yvalue = 1100 + i * 2;
    trp_pos_cart->zvalue = -4100 + i * 2;
    // testing random values for uncertainity and confidence
    trp_pos_cart->location_uncertainty.horizontal_uncertainty = i + 1;
    trp_pos_cart->location_uncertainty.horizontal_confidence = i + 2;
    trp_pos_cart->location_uncertainty.vertical_uncertainty = i + 3;
    trp_pos_cart->location_uncertainty.vertical_confidence = i + 4;
  }

  F1AP_F1AP_PDU_t *f1enc = encode_trp_information_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_trp_information_resp_t decoded = {0};
  bool ret = decode_trp_information_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_trp_information_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_trp_information_resp(&orig, &decoded);
  AssertFatal(ret, "eq_trp_information_resp(): decoded message doesn't match\n");
  free_trp_information_resp(&decoded);

  f1ap_trp_information_resp_t cp = cp_trp_information_resp(&orig);
  ret = eq_trp_information_resp(&orig, &cp);
  AssertFatal(ret, "eq_trp_information_resp(): copied message doesn't match\n");
  free_trp_information_resp(&orig);
  free_trp_information_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_trp_information_failure()
{
  f1ap_trp_information_failure_t orig = {
      .transaction_id = 12,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_trp_information_failure(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_trp_information_failure_t decoded = {0};
  bool ret = decode_trp_information_failure(f1dec, &decoded);
  AssertFatal(ret, "decode_trp_information_failure(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_trp_information_failure(&orig, &decoded);
  AssertFatal(ret, "eq_trp_information_failure(): decoded message doesn't match\n");
  free_trp_information_failure(&decoded);

  f1ap_trp_information_failure_t cp = cp_trp_information_failure(&orig);
  ret = eq_trp_information_failure(&orig, &cp);
  AssertFatal(ret, "eq_trp_information_failure(): copied message doesn't match\n");
  free_trp_information_failure(&orig);
  free_trp_information_failure(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_request()
{
  f1ap_positioning_measurement_req_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
  };

  uint32_t trp_meas_req_list_len = 3;
  orig.trp_measurement_request_list.trp_measurement_request_list_length = trp_meas_req_list_len;
  orig.trp_measurement_request_list.trp_measurement_request_item =
      calloc_or_fail(trp_meas_req_list_len, sizeof(*orig.trp_measurement_request_list.trp_measurement_request_item));
  for (int i = 0; i < trp_meas_req_list_len; i++) {
    orig.trp_measurement_request_list.trp_measurement_request_item[i].tRPID = 115 + i;
  }

  orig.pos_report_characteristics = F1AP_POSREPORTCHARACTERISTICS_PERIODIC;
  orig.measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS480;

  uint32_t pos_meas_quantities_len = 4;
  orig.pos_measurement_quantities.pos_measurement_quantities_length = pos_meas_quantities_len;

  f1ap_pos_measurement_quantities_item_t *pos_meas_quantities_item =
      calloc_or_fail(pos_meas_quantities_len, sizeof(*pos_meas_quantities_item));
  orig.pos_measurement_quantities.pos_measurement_quantities_item = pos_meas_quantities_item;

  pos_meas_quantities_item[0].pos_measurement_type = F1AP_POSMEASUREMENTTYPE_GNB_RX_TX;
  pos_meas_quantities_item[1].pos_measurement_type = F1AP_POSMEASUREMENTTYPE_UL_SRS_RSRP;
  pos_meas_quantities_item[2].pos_measurement_type = F1AP_POSMEASUREMENTTYPE_UL_AOA;
  pos_meas_quantities_item[3].pos_measurement_type = F1AP_POSMEASUREMENTTYPE_UL_RTOA;

  // SRS configuration
  orig.srs_configuration = calloc_or_fail(1, sizeof(*orig.srs_configuration));
  f1ap_srs_carrier_list_t *srs_carrier_list = &orig.srs_configuration->srs_carrier_list;
  fill_srs_carrier_list(srs_carrier_list);

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_req(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_req_t decoded = {0};
  bool ret = decode_positioning_measurement_req(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_req(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_req(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_req(): decoded message doesn't match\n");
  free_positioning_measurement_req(&decoded);

  f1ap_positioning_measurement_req_t cp = cp_positioning_measurement_req(&orig);
  ret = eq_positioning_measurement_req(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_req(): copied message doesn't match\n");
  free_positioning_measurement_req(&orig);
  free_positioning_measurement_req(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_response()
{
  f1ap_positioning_measurement_resp_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
  };

  orig.pos_measurement_result_list = calloc_or_fail(1, sizeof(*orig.pos_measurement_result_list));
  uint32_t pos_meas_result_list_len = 2;
  orig.pos_measurement_result_list->pos_measurement_result_list_length = pos_meas_result_list_len;
  orig.pos_measurement_result_list->pos_measurement_result_list_item =
      calloc_or_fail(pos_meas_result_list_len, sizeof(*orig.pos_measurement_result_list->pos_measurement_result_list_item));
  for (int i = 0; i < pos_meas_result_list_len; i++) {
    f1ap_pos_measurement_result_list_item_t *pos_meas_result_list_item =
        &orig.pos_measurement_result_list->pos_measurement_result_list_item[i];
    pos_meas_result_list_item->trp_id = 10 + i;

    f1ap_pos_measurement_result_t *posMeasurementResult = &pos_meas_result_list_item->pos_measurement_result;
    uint32_t pos_meas_result_length = 4;
    posMeasurementResult->pos_measurement_result_item_length = pos_meas_result_length;
    posMeasurementResult->pos_measurement_result_item =
        calloc_or_fail(pos_meas_result_length, sizeof(*posMeasurementResult->pos_measurement_result_item));

    // Angle of arrival
    f1ap_pos_measurement_result_item_t *pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[0];
    f1ap_measured_results_value_t *measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_ANGLEOFARRIVAL;
    f1ap_ul_aoa_t *uL_AngleOfArrival = &measuredResultsValue->choice.ul_angle_of_arrival;
    uL_AngleOfArrival->azimuth_aoa = 700 + i;
    uL_AngleOfArrival->zenith_aoa = calloc_or_fail(1, sizeof(*uL_AngleOfArrival->zenith_aoa));
    *uL_AngleOfArrival->zenith_aoa = 450 + i;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa = calloc_or_fail(1, sizeof(*uL_AngleOfArrival->lcs_to_gcs_translation_aoa));
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->alpha = 200;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->beta = 220;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->gamma = 280;

    f1ap_time_stamp_t *timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 100;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_30;
    timeStamp->slot_index.choice.scs_30 = 15;

    // UL SRS RSRP
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[1];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_SRS_RSRP;
    measuredResultsValue->choice.ul_srs_rsrp = 100 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 101;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_15;
    timeStamp->slot_index.choice.scs_15 = 8;

    // UL RToA
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[2];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_RTOA;
    f1ap_ul_rtoa_measurement_t *uL_RTOA = &measuredResultsValue->choice.ul_rtoa;
    uL_RTOA->ul_rtoa_measurement_item.present = F1AP_ULRTOAMEAS_PR_K1;
    uL_RTOA->ul_rtoa_measurement_item.choice.k1 = 98500 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 102;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_60;
    timeStamp->slot_index.choice.scs_60 = 31;

    // gNB RX-TX Time Diff
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[3];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_GNB_RXTXTIMEDIFF;
    f1ap_gnb_rx_tx_time_diff_t *gNB_RxTxTimeDiff = &measuredResultsValue->choice.gnb_rx_tx_time_diff;
    gNB_RxTxTimeDiff->rx_tx_time_diff.present = F1AP_GNBRXTXTIMEDIFFMEAS_PR_K3;
    gNB_RxTxTimeDiff->rx_tx_time_diff.choice.k3 = 98412 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 103;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_120;
    timeStamp->slot_index.choice.scs_120 = 70;
  }

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_resp(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_resp_t decoded = {0};
  bool ret = decode_positioning_measurement_resp(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_resp(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_resp(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_resp(): decoded message doesn't match\n");
  free_positioning_measurement_resp(&decoded);

  f1ap_positioning_measurement_resp_t cp = cp_positioning_measurement_resp(&orig);
  ret = eq_positioning_measurement_resp(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_resp(): copied message doesn't match\n");
  free_positioning_measurement_resp(&orig);
  free_positioning_measurement_resp(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_failure()
{
  f1ap_positioning_measurement_failure_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_failure(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_failure_t decoded = {0};
  bool ret = decode_positioning_measurement_failure(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_failure(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_failure(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_failure(): decoded message doesn't match\n");
  free_positioning_measurement_failure(&decoded);

  f1ap_positioning_measurement_failure_t cp = cp_positioning_measurement_failure(&orig);
  ret = eq_positioning_measurement_failure(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_failure(): copied message doesn't match\n");
  free_positioning_measurement_failure(&orig);
  free_positioning_measurement_failure(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_report()
{
  f1ap_positioning_measurement_report_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
  };

  orig.pos_measurement_result_list = calloc_or_fail(1, sizeof(*orig.pos_measurement_result_list));
  uint32_t pos_meas_result_list_len = 2;
  orig.pos_measurement_result_list->pos_measurement_result_list_length = pos_meas_result_list_len;
  orig.pos_measurement_result_list->pos_measurement_result_list_item =
      calloc_or_fail(pos_meas_result_list_len, sizeof(*orig.pos_measurement_result_list->pos_measurement_result_list_item));
  for (int i = 0; i < pos_meas_result_list_len; i++) {
    f1ap_pos_measurement_result_list_item_t *pos_meas_result_list_item =
        &orig.pos_measurement_result_list->pos_measurement_result_list_item[i];
    pos_meas_result_list_item->trp_id = 10 + i;

    f1ap_pos_measurement_result_t *posMeasurementResult = &pos_meas_result_list_item->pos_measurement_result;
    uint32_t pos_meas_result_length = 4;
    posMeasurementResult->pos_measurement_result_item_length = pos_meas_result_length;
    posMeasurementResult->pos_measurement_result_item =
        calloc_or_fail(pos_meas_result_length, sizeof(*posMeasurementResult->pos_measurement_result_item));

    // Angle of arrival
    f1ap_pos_measurement_result_item_t *pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[0];
    f1ap_measured_results_value_t *measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_ANGLEOFARRIVAL;
    f1ap_ul_aoa_t *uL_AngleOfArrival = &measuredResultsValue->choice.ul_angle_of_arrival;
    uL_AngleOfArrival->azimuth_aoa = 700 + i;
    uL_AngleOfArrival->zenith_aoa = calloc_or_fail(1, sizeof(*uL_AngleOfArrival->zenith_aoa));
    *uL_AngleOfArrival->zenith_aoa = 450 + i;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa = calloc_or_fail(1, sizeof(*uL_AngleOfArrival->lcs_to_gcs_translation_aoa));
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->alpha = 200;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->beta = 220;
    uL_AngleOfArrival->lcs_to_gcs_translation_aoa->gamma = 280;

    f1ap_time_stamp_t *timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 100;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_30;
    timeStamp->slot_index.choice.scs_30 = 15;

    // UL SRS RSRP
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[1];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_SRS_RSRP;
    measuredResultsValue->choice.ul_srs_rsrp = 100 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 101;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_15;
    timeStamp->slot_index.choice.scs_15 = 8;

    // UL RToA
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[2];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_UL_RTOA;
    f1ap_ul_rtoa_measurement_t *uL_RTOA = &measuredResultsValue->choice.ul_rtoa;
    uL_RTOA->ul_rtoa_measurement_item.present = F1AP_ULRTOAMEAS_PR_K1;
    uL_RTOA->ul_rtoa_measurement_item.choice.k1 = 98500 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 102;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_60;
    timeStamp->slot_index.choice.scs_60 = 31;

    // gNB RX-TX Time Diff
    pos_measurement_result_item = &posMeasurementResult->pos_measurement_result_item[3];
    measuredResultsValue = &pos_measurement_result_item->measured_results_value;
    measuredResultsValue->present = F1AP_MEASURED_RESULTS_VALUE_PR_GNB_RXTXTIMEDIFF;
    f1ap_gnb_rx_tx_time_diff_t *gNB_RxTxTimeDiff = &measuredResultsValue->choice.gnb_rx_tx_time_diff;
    gNB_RxTxTimeDiff->rx_tx_time_diff.present = F1AP_GNBRXTXTIMEDIFFMEAS_PR_K3;
    gNB_RxTxTimeDiff->rx_tx_time_diff.choice.k3 = 98412 + i;

    timeStamp = &pos_measurement_result_item->time_stamp;
    timeStamp->system_frame_number = 103;
    timeStamp->slot_index.present = F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_120;
    timeStamp->slot_index.choice.scs_120 = 70;
  }

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_report(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_report_t decoded = {0};
  bool ret = decode_positioning_measurement_report(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_report(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_report(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_report(): decoded message doesn't match\n");
  free_positioning_measurement_report(&decoded);

  f1ap_positioning_measurement_report_t cp = cp_positioning_measurement_report(&orig);
  ret = eq_positioning_measurement_report(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_report(): copied message doesn't match\n");
  free_positioning_measurement_report(&orig);
  free_positioning_measurement_report(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_abort()
{
  f1ap_positioning_measurement_abort_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_abort(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_abort_t decoded = {0};
  bool ret = decode_positioning_measurement_abort(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_abort(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_abort(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_abort(): decoded message doesn't match\n");
  free_positioning_measurement_abort(&decoded);

  f1ap_positioning_measurement_abort_t cp = cp_positioning_measurement_abort(&orig);
  ret = eq_positioning_measurement_abort(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_abort(): copied message doesn't match\n");
  free_positioning_measurement_abort(&orig);
  free_positioning_measurement_abort(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_failure_indication()
{
  f1ap_positioning_measurement_failure_indication_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
      .cause = F1AP_CAUSE_MISC,
      .cause_value = 3,
  };
  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_failure_indication(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_failure_indication_t decoded = {0};
  bool ret = decode_positioning_measurement_failure_indication(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_failure_indication(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_failure_indication(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_failure_indication(): decoded message doesn't match\n");
  free_positioning_measurement_failure_indication(&decoded);

  f1ap_positioning_measurement_failure_indication_t cp = cp_positioning_measurement_failure_indication(&orig);
  ret = eq_positioning_measurement_failure_indication(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_failure_indication(): copied message doesn't match\n");
  free_positioning_measurement_failure_indication(&orig);
  free_positioning_measurement_failure_indication(&cp);

  printf("%s() successful\n", __func__);
}

static void test_f1ap_positioning_measurement_update()
{
  f1ap_positioning_measurement_update_t orig = {
      .transaction_id = 12,
      .lmf_measurement_id = 1999,
      .ran_measurement_id = 2225,
  };

  // SRS configuration
  orig.srs_configuration = calloc_or_fail(1, sizeof(*orig.srs_configuration));
  f1ap_srs_carrier_list_t *srs_carrier_list = &orig.srs_configuration->srs_carrier_list;
  fill_srs_carrier_list(srs_carrier_list);

  F1AP_F1AP_PDU_t *f1enc = encode_positioning_measurement_update(&orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_positioning_measurement_update_t decoded = {0};
  bool ret = decode_positioning_measurement_update(f1dec, &decoded);
  AssertFatal(ret, "decode_positioning_measurement_update(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_positioning_measurement_update(&orig, &decoded);
  AssertFatal(ret, "eq_positioning_measurement_update(): decoded message doesn't match\n");
  free_positioning_measurement_update(&decoded);

  f1ap_positioning_measurement_update_t cp = cp_positioning_measurement_update(&orig);
  ret = eq_positioning_measurement_update(&orig, &cp);
  AssertFatal(ret, "eq_positioning_measurement_update(): copied message doesn't match\n");
  free_positioning_measurement_update(&orig);
  free_positioning_measurement_update(&cp);

  printf("%s() successful\n", __func__);
}

/** @brief Test F1 Paging encode/decode roundtrip and copy operations */
static void test_f1ap_paging_rt(f1ap_paging_t *orig)
{
  F1AP_F1AP_PDU_t *f1enc = encode_f1ap_paging(orig);
  F1AP_F1AP_PDU_t *f1dec = f1ap_encode_decode(f1enc);
  f1ap_msg_free(f1enc);

  f1ap_paging_t decoded = {0};
  bool ret = decode_f1ap_paging(&decoded, f1dec);
  AssertFatal(ret, "decode_f1ap_paging(): could not decode message\n");
  f1ap_msg_free(f1dec);

  ret = eq_f1ap_paging(orig, &decoded);
  AssertFatal(ret, "eq_f1ap_paging(): decoded message doesn't match\n");
  free_f1ap_paging(&decoded);

  f1ap_paging_t cp = cp_f1ap_paging(orig);
  ret = eq_f1ap_paging(orig, &cp);
  AssertFatal(ret, "eq_f1ap_paging(): copied message doesn't match\n");
  free_f1ap_paging(&cp);
}

/** @brief Test F1 Paging encoding/decoding */
static void test_f1ap_paging(void)
{
  // Test with CN UE Paging Identity (5G-S-TMSI)
  {
    f1ap_paging_t orig = {0};
    orig.ue_identity_index_value = 123;
    orig.identity_type = F1AP_PAGING_IDENTITY_CN_UE;
    orig.identity.cn_ue_paging_identity = 0x1234567890AB;
    orig.drx = malloc_or_fail(sizeof(*orig.drx));
    *orig.drx = F1AP_PAGING_DRX_32;
    orig.n_cells = 2;
    orig.cells = calloc_or_fail(orig.n_cells, sizeof(*orig.cells));
    orig.cells[0].plmn.mcc = 208;
    orig.cells[0].plmn.mnc = 95;
    orig.cells[0].plmn.mnc_digit_length = 2;
    orig.cells[0].nr_cellid = 123456;
    orig.cells[1].plmn.mcc = 208;
    orig.cells[1].plmn.mnc = 93;
    orig.cells[1].plmn.mnc_digit_length = 2;
    orig.cells[1].nr_cellid = 789012;

    test_f1ap_paging_rt(&orig);
    free_f1ap_paging(&orig);
  }

  // Test with RAN UE Paging Identity (I-RNTI)
  {
    f1ap_paging_t orig = {0};
    orig.ue_identity_index_value = 456;
    orig.identity_type = F1AP_PAGING_IDENTITY_RAN_UE;
    orig.identity.ran_ue_paging_identity = 0x1234567890;
    orig.drx = NULL;
    orig.priority = malloc_or_fail(sizeof(*orig.priority));
    *orig.priority = F1AP_PAGING_PRIO_LEVEL3;
    orig.origin = malloc_or_fail(sizeof(*orig.origin));
    *orig.origin = F1AP_PAGING_ORIGIN_NON_3GPP;
    orig.n_cells = 1;
    orig.cells = calloc_or_fail(orig.n_cells, sizeof(*orig.cells));
    orig.cells[0].plmn.mcc = 1;
    orig.cells[0].plmn.mnc = 1;
    orig.cells[0].plmn.mnc_digit_length = 2;
    orig.cells[0].nr_cellid = 987654;

    test_f1ap_paging_rt(&orig);
    free_f1ap_paging(&orig);
  }

  // Paging Cell item with an unexpected IE ID
  {
    F1AP_F1AP_PDU_t *pdu =
        f1ap_decode_hex("00120027000003007500020f60007f0007401234567890ab0071000f0000004e00090002f85900001e2400");
    f1ap_paging_t msg = {0};
    AssertFatal(!decode_f1ap_paging(&msg, pdu), "decoded malformed Paging Cell item\n");
    free_f1ap_paging(&msg);
    f1ap_msg_free(pdu);
  }

  printf("%s() successful\n", __func__);
}

int main()
{
  test_initial_ul_rrc_message_transfer();
  test_dl_rrc_message_transfer();
  test_ul_rrc_message_transfer();
  test_f1ap_setup_request();
  test_f1ap_setup_response();
  test_f1ap_setup_failure();
  test_f1ap_reset_all();
  test_f1ap_reset_part();
  test_f1ap_reset_ack();
  test_f1ap_du_configuration_update();
  test_f1ap_cu_configuration_update();
  test_f1ap_cu_configuration_update_acknowledge();
  test_f1ap_du_configuration_update_acknowledge();
  test_f1ap_ue_context_setup_request();
  test_f1ap_ue_context_setup_request_simple();
  test_f1ap_ue_context_setup_response();
  test_f1ap_ue_context_setup_response_simple();
  test_f1ap_ue_context_modification_request();
  test_f1ap_ue_context_modification_request_simple();
  test_f1ap_ue_context_modification_response();
  test_f1ap_ue_context_modification_response_simple();
  test_f1ap_ue_context_release_request();
  test_f1ap_ue_context_release_command();
  test_f1ap_ue_context_release_complete();
  test_f1ap_positioning_information_request();
  test_f1ap_positioning_information_response();
  test_f1ap_positioning_information_failure();
  test_f1ap_positioning_activation_request();
  test_f1ap_positioning_activation_response();
  test_f1ap_positioning_activation_failure();
  test_f1ap_positioning_deactivation();
  test_f1ap_positioning_information_update();
  test_f1ap_trp_information_request();
  test_f1ap_trp_information_response();
  test_f1ap_trp_information_failure();
  test_f1ap_positioning_measurement_request();
  test_f1ap_positioning_measurement_response();
  test_f1ap_positioning_measurement_failure();
  test_f1ap_positioning_measurement_report();
  test_f1ap_positioning_measurement_abort();
  test_f1ap_positioning_measurement_failure_indication();
  test_f1ap_positioning_measurement_update();
  test_f1ap_paging();
  return 0;
}
