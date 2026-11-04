/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief Test functions for XnAP encoding/decoding library
 */

#include <arpa/inet.h>
#include "common/utils/assertions.h"
#include "openair2/XNAP/lib/xnap_lib_common.h"
#include "openair2/XNAP/lib/xnap_gNB_interface_management.h"
#include "openair2/XNAP/lib/xnap_gNB_mobility_management.h"

#include "common/config/config_userapi.h"

configmodule_interface_t *uniqCfg = NULL;

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  UNUSED(assert);
  printf("detected error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

/* Helper for ASN.1 Encoding/Decoding loop */
static XNAP_XnAP_PDU_t *xnap_encode_decode(const XNAP_XnAP_PDU_t *enc_pdu)
{
  //xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, enc_pdu);
  DevAssert(enc_pdu != NULL);
  char errbuf[1024];
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_XNAP_XnAP_PDU, enc_pdu, errbuf, &errlen);
  AssertFatal(ret == 0, "asn_check_constraints() failed: %s\n", errbuf);

  uint8_t msgbuf[16384];
  // 1. Encode
  asn_enc_rval_t enc = aper_encode_to_buffer(&asn_DEF_XNAP_XnAP_PDU, NULL, enc_pdu, msgbuf, sizeof(msgbuf));
  AssertFatal(enc.encoded > 0, "aper_encode_to_buffer() failed\n");

  // 2. Decode
  XNAP_XnAP_PDU_t *dec_pdu = NULL;
  asn_codec_ctx_t st = {.max_stack_size = 100 * 1000};
  asn_dec_rval_t dec = aper_decode(&st, &asn_DEF_XNAP_XnAP_PDU, (void **)&dec_pdu, msgbuf, enc.encoded, 0, 0);
  AssertFatal(dec.code == RC_OK, "aper_decode() failed\n");

  //xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, dec_pdu);

  return dec_pdu;
}

static void xnap_msg_free(XNAP_XnAP_PDU_t *pdu)
{
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
}

/**
 * 1. Xn Setup Request
 */
static void test_xn_setup_request(void)
{
  /* ---------- Common PLMNs ---------- */
  plmn_id_t plmn0 = { .mcc = 208, .mnc = 95, .mnc_digit_length = 2 };
  plmn_id_t plmn1 = { .mcc = 208, .mnc = 93, .mnc_digit_length = 2 };
  
  /* ---------- TAI support (2) ---------- */
  xnap_tai_support_t *tai_support = calloc_or_fail(2, sizeof(*tai_support));
  
  /* ================= TAI 0 ================= */
  tai_support[0].tac = 1;
  tai_support[0].num_plmn = 2;
  tai_support[0].plmn_support = calloc_or_fail(2, sizeof(*tai_support[0].plmn_support));
  
  /* ---- TAI 0 / PLMN 0 ---- */
  tai_support[0].plmn_support[0].plmn = plmn0;
  tai_support[0].plmn_support[0].num_nssai = 3;
  tai_support[0].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[0].nssai[0] = (nssai_t){ .sst = 1, .sd = 0x010203 };
  tai_support[0].plmn_support[0].nssai[1] = (nssai_t){ .sst = 1, .sd = 0x010204 };
  tai_support[0].plmn_support[0].nssai[2] = (nssai_t){ .sst = 1, .sd = 0x010205 };
  
  /* ---- TAI 0 / PLMN 1 ---- */
  tai_support[0].plmn_support[1].plmn = plmn1;
  tai_support[0].plmn_support[1].num_nssai = 3;
  tai_support[0].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[1].nssai[0] = (nssai_t){ .sst = 2, .sd = 0x020203 };
  tai_support[0].plmn_support[1].nssai[1] = (nssai_t){ .sst = 2, .sd = 0x020204 };
  tai_support[0].plmn_support[1].nssai[2] = (nssai_t){ .sst = 2, .sd = 0x020205 };
  
  /* ================= TAI 1 ================= */
  tai_support[1].tac = 2;
  tai_support[1].num_plmn = 2;
  tai_support[1].plmn_support = calloc_or_fail(2, sizeof(*tai_support[1].plmn_support));
  
  /* ---- TAI 1 / PLMN 0 ---- */
  tai_support[1].plmn_support[0].plmn = plmn0;
  tai_support[1].plmn_support[0].num_nssai = 3;
  tai_support[1].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[0].nssai[0] = (nssai_t){ .sst = 3, .sd = 0x030203 };
  tai_support[1].plmn_support[0].nssai[1] = (nssai_t){ .sst = 3, .sd = 0x030204 };
  tai_support[1].plmn_support[0].nssai[2] = (nssai_t){ .sst = 3, .sd = 0x030205 };
  
  /* ---- TAI 1 / PLMN 1 ---- */
  tai_support[1].plmn_support[1].plmn = plmn1;
  tai_support[1].plmn_support[1].num_nssai = 3;
  tai_support[1].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[1].nssai[0] = (nssai_t){ .sst = 4, .sd = 0x040203 };
  tai_support[1].plmn_support[1].nssai[1] = (nssai_t){ .sst = 4, .sd = 0x040204 };
  tai_support[1].plmn_support[1].nssai[2] = (nssai_t){ .sst = 4, .sd = 0x040205 };
  
  /* ---------- AMF region info (2) ---------- */
  xnap_amf_region_info_t *amf_region_info = calloc_or_fail(2, sizeof(*amf_region_info));
  
  amf_region_info[0].plmn = plmn0;
  amf_region_info[0].amf_region_id = 1;
  
  amf_region_info[1].plmn = plmn1;
  amf_region_info[1].amf_region_id = 2;
  
  /* ---------- create message ---------- */
  xnap_setup_req_t orig = {
      .gNB_id = 0xABCDE,
      .plmn = plmn0,
      .num_tai = 2,
      .tai_support = tai_support,
      .num_amf_regions = 2,
      .amf_region_info = amf_region_info,
  };
  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_request(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_req_t decoded = {0};
  bool ret = decode_xn_setup_request(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_request failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_request(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Req mismatch\n");
  free_xnap_setup_request(&decoded);
  free_xnap_setup_request(&orig);
  printf("%s() successful \n", __func__);
}

/**
 * 2. Xn Setup Response
 */
static void test_xn_setup_response(void)
{
  /* ---------- Common PLMNs ---------- */
  plmn_id_t plmn0 = { .mcc = 208, .mnc = 95, .mnc_digit_length = 2 };
  plmn_id_t plmn1 = { .mcc = 208, .mnc = 93, .mnc_digit_length = 2 };
  
  /* ---------- TAI support (2) ---------- */
  xnap_tai_support_t *tai_support = calloc_or_fail(2, sizeof(*tai_support));
  
  /* ================= TAI 0 ================= */
  tai_support[0].tac = 1;
  tai_support[0].num_plmn = 2;
  tai_support[0].plmn_support = calloc_or_fail(2, sizeof(*tai_support[0].plmn_support));
  
  /* ---- TAI 0 / PLMN 0 ---- */
  tai_support[0].plmn_support[0].plmn = plmn0;
  tai_support[0].plmn_support[0].num_nssai = 3;
  tai_support[0].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[0].nssai[0] = (nssai_t){ .sst = 1, .sd = 0x010203 };
  tai_support[0].plmn_support[0].nssai[1] = (nssai_t){ .sst = 1, .sd = 0x010204 };
  tai_support[0].plmn_support[0].nssai[2] = (nssai_t){ .sst = 1, .sd = 0x010205 };
  
  /* ---- TAI 0 / PLMN 1 ---- */
  tai_support[0].plmn_support[1].plmn = plmn1;
  tai_support[0].plmn_support[1].num_nssai = 3;
  tai_support[0].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[1].nssai[0] = (nssai_t){ .sst = 2, .sd = 0x020203 };
  tai_support[0].plmn_support[1].nssai[1] = (nssai_t){ .sst = 2, .sd = 0x020204 };
  tai_support[0].plmn_support[1].nssai[2] = (nssai_t){ .sst = 2, .sd = 0x020205 };
  
  /* ================= TAI 1 ================= */
  tai_support[1].tac = 2;
  tai_support[1].num_plmn = 2;
  tai_support[1].plmn_support = calloc_or_fail(2, sizeof(*tai_support[1].plmn_support));
  
  /* ---- TAI 1 / PLMN 0 ---- */
  tai_support[1].plmn_support[0].plmn = plmn0;
  tai_support[1].plmn_support[0].num_nssai = 3;
  tai_support[1].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[0].nssai[0] = (nssai_t){ .sst = 3, .sd = 0x030203 };
  tai_support[1].plmn_support[0].nssai[1] = (nssai_t){ .sst = 3, .sd = 0x030204 };
  tai_support[1].plmn_support[0].nssai[2] = (nssai_t){ .sst = 3, .sd = 0x030205 };
  
  /* ---- TAI 1 / PLMN 1 ---- */
  tai_support[1].plmn_support[1].plmn = plmn1;
  tai_support[1].plmn_support[1].num_nssai = 3;
  tai_support[1].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[1].nssai[0] = (nssai_t){ .sst = 4, .sd = 0x040203 };
  tai_support[1].plmn_support[1].nssai[1] = (nssai_t){ .sst = 4, .sd = 0x040204 };
  tai_support[1].plmn_support[1].nssai[2] = (nssai_t){ .sst = 4, .sd = 0x040205 };
  
  /* ---------- create message ---------- */
  xnap_setup_resp_t orig = {
      .gNB_id = 0xABCDE,
      .plmn = plmn0,
      .num_tai = 2,
      .tai_support = tai_support,
  };
  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_response(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_resp_t decoded = {0};
  bool ret = decode_xn_setup_response(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_response failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_response(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Response mismatch\n");
  free_xnap_setup_response(&decoded);
  free_xnap_setup_response(&orig);
  printf("%s() successful \n", __func__);
}

/**
 * 3. Xn Setup Failure
 */
static void test_xn_setup_failure(void)
{
  /* ---------- create message ---------- */
  xnap_setup_failure_t orig = {
    .cause.type = XNAP_CAUSE_TRANSPORT,
    .cause.value = XNAP_CAUSE_TRANSPORT_LAYER_TRANSPORT_RESOURCE_UNAVAILABLE,
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_failure(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_failure_t decoded = {0};
  bool ret = decode_xn_setup_failure(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_failure failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_failure(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Failure mismatch\n");
  free_xnap_setup_failure(&decoded);
  free_xnap_setup_failure(&orig);
  printf("%s() successful \n", __func__);
}

/**
 * 4. Xn Handover Request
 */
static void test_xn_handover_request(void)
{
  /* ---------- Common identifiers ---------- */
  plmn_id_t plmn0 = {.mcc = 208, .mnc = 95, .mnc_digit_length = 2};

  /* ---------- Transport Layer Address (IPv4) ---------- */
  transport_layer_addr_t tnl_addr_source = {.length = 4, .buffer = {192, 168, 1, 100}};

  transport_layer_addr_t tnl_addr_n3 = {.length = 4, .buffer = {10, 0, 0, 1}};

  /* ---------- S-NSSAI ---------- */
  nssai_t nssai0 = {.sst = 1, .sd = 0x000001};

  /* ---------- QoS Flow Level QoS Parameters ---------- */
  xnap_qos_flow_param_t qos_params = {.qos_type = NON_DYNAMIC,
                                      .nondyn = {.fiveQI = 9},
                                      .arp = {.priority_level = 2,
                                              .pre_emp_capability = PEC_SHALL_NOT_TRIGGER_PREEMPTION,
                                              .pre_emp_vulnerability = PEV_NOT_PREEMPTABLE}};

  /* ---------- QoS Flows To Be Setup List (Internet) ---------- */
  xnap_qos_flow_tobe_setup_item_t *qos_list = calloc_or_fail(2, sizeof(*qos_list));

  qos_list[0].qfi = 1;
  qos_list[0].qos_params = qos_params;

  qos_list[1].qfi = 2;
  qos_list[1].qos_params = qos_params;
  qos_list[1].qos_params.nondyn.fiveQI = 5;

  /* ---------- QoS Flows (IMS) ---------- */
  xnap_qos_flow_tobe_setup_item_t *qos_list_ims = calloc_or_fail(1, sizeof(*qos_list_ims));

  qos_list_ims[0].qfi = 5;
  qos_list_ims[0].qos_params = qos_params;
  qos_list_ims[0].qos_params.nondyn.fiveQI = 2;

  /* ---------- PDU Session Resources ---------- */
  xnap_pdusession_resources_tobe_setup_item_t *pdusession_list = calloc_or_fail(2, sizeof(*pdusession_list));

  /* ----- PDU Session 1 (Internet) ----- */
  pdusession_list[0].pdusession_id = 1;

  pdusession_list[0].nssai = calloc_or_fail(1, sizeof(nssai_t));
  *pdusession_list[0].nssai = nssai0;

  pdusession_list[0].n3_incoming = (gtpu_tunnel_t){.teid = 0x12345678, .addr = tnl_addr_n3};

  pdusession_list[0].pdu_session_type = PDUSessionType_ipv4;
  pdusession_list[0].num_qos = 2;
  pdusession_list[0].qos_list = qos_list;

  /* ----- PDU Session 2 (IMS) ----- */
  nssai_t nssai_ims = {.sst = 1, .sd = 0x000002};

  pdusession_list[1].pdusession_id = 2;

  pdusession_list[1].nssai = calloc_or_fail(1, sizeof(nssai_t));
  *pdusession_list[1].nssai = nssai_ims;

  pdusession_list[1].n3_incoming = (gtpu_tunnel_t){.teid = 0x87654321, .addr = tnl_addr_n3};

  pdusession_list[1].pdu_session_type = PDUSessionType_ipv4;
  pdusession_list[1].num_qos = 1;
  pdusession_list[1].qos_list = qos_list_ims;

  /* ---------- UE Security Capabilities ---------- */
  xnap_security_capabilities_t security_cap = {.nRencryption_algorithms = 0xC000,
                                               .nRintegrity_algorithms = 0xC000,
                                               .eUTRAencryption_algorithms = 0xC000,
                                               .eUTRAintegrity_algorithms = 0xC000};

  /* ---------- AS Security Key ---------- */
  uint8_t as_key[32];
  for (int i = 0; i < 32; i++)
    as_key[i] = i;

  /* ---------- RRC Context ---------- */
  uint8_t rrc_data[] = {1, 2, 3, 4, 5};
  byte_array_t rrc_context = {.buf = calloc_or_fail(5, sizeof(uint8_t)), .len = 5};
  memcpy(rrc_context.buf, rrc_data, 5);

  /* ---------- UE Context ---------- */
  xnap_ue_context_info_t ue_context = {.ngc_ue_sig_ref = 0x123456789,
                                       .cp_tnl_ip_source = tnl_addr_source,
                                       .security_capabilities = security_cap,
                                       .as_security_ncc = 1,
                                       .ue_ambr = {.br_ul = 100000000, .br_dl = 200000000},
                                       .rrc_context = rrc_context,
                                       .num_pdu = 2,
                                       .pdusession_resources_tobe_setup_list = pdusession_list};

  memcpy(ue_context.as_security_key_ranstar, as_key, 32);

  /* ---------- UE History Information ---------- */
  ue_history_info_t *history_list = calloc_or_fail(1, sizeof(*history_list));
  history_list[0].xnap_cell_type = XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell;

  uint8_t history_buf[] = {0x01, 0x02, 0x03, 0x04};
  history_list[0].last_visited_cell_info.buf = calloc_or_fail(sizeof(history_buf), 1);

  memcpy(history_list[0].last_visited_cell_info.buf, history_buf, sizeof(history_buf));

  history_list[0].last_visited_cell_info.len = sizeof(history_buf);

  /* ---------- GUAMI ---------- */
  nr_guami_t guami = {.plmn = plmn0, .amf_region_id = 0x01, .amf_set_id = 0x0001, .amf_pointer = 0x01};

  /* ---------- Target CGI ---------- */
  xnap_ngran_cgi_t target_cgi = {.plmn_id = plmn0, .nrcell_id = 0x123456789};

  /* ---------- Create message ---------- */
  xnap_handover_req_t orig = {
      .s_ng_node_ue_xnap_id = 0xABCD,
      .cause = {.type = XNAP_CAUSE_RADIO_NETWORK, .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_HANDOVER_DESIRABLE_FOR_RADIO_REASONS},
      .target_cgi = target_cgi,
      .guami = guami,
      .ue_context = ue_context,
      .num_last_visited_cells = 1,
      .ue_history_info = history_list};

  /* ---------- Encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_handover_request(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- Decode ---------- */
  xnap_handover_req_t decoded = {0};
  bool ret = decode_xnap_handover_request(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_handover_request failed");
  xnap_msg_free(xndec);

  /* ---------- Equality ---------- */
  ret = eq_xnap_handover_request(&orig, &decoded);
  AssertFatal(ret, "Xn Handover Request mismatch\n");

  /* ---------- Cleanup ---------- */
  free_xnap_handover_request(&decoded);
  free_xnap_handover_request(&orig);

  printf("%s() successful\n", __func__);
}

/**
 * 5. Xn Handover Request Acknowledge
 */
static void test_xn_handover_request_acknowledge(void)
{
  /* ---------- create message ---------- */

  /* Create QoS Flows Admitted List for first PDU session */
  xnap_qos_admitted_item_t *qos_list_1 = calloc_or_fail(2, sizeof(xnap_qos_admitted_item_t));
  qos_list_1[0].qfi = 5;
  qos_list_1[1].qfi = 9;

  /* Create QoS Flows Admitted List for second PDU session */
  xnap_qos_admitted_item_t *qos_list_2 = calloc_or_fail(1, sizeof(xnap_qos_admitted_item_t));
  qos_list_2[0].qfi = 1;

  /* Create PDU Session Resources Admitted List */
  xnap_pdusession_admitted_item_t *pdu_list = calloc_or_fail(2, sizeof(xnap_pdusession_admitted_item_t));
  pdu_list[0].pdusession_id = 10;
  pdu_list[0].num_qos = 2;
  pdu_list[0].qos_list = qos_list_1;

  pdu_list[1].pdusession_id = 15;
  pdu_list[1].num_qos = 1;
  pdu_list[1].qos_list = qos_list_2;

  /* Create transparent container ( Dummy data for RRC HandoverCommand) */
  uint8_t container_data[] = {
      0x00,
      0x01,
      0x02,
      0x03,
      0x04,
      0x05,
      0x06,
      0x07,
      0x08,
      0x09,
      0x0a,
      0x0b,
      0x0c,
      0x0d,
      0x0e,
      0x0f,
  };

  uint8_t *container_buf = malloc(sizeof(container_data));
  memcpy(container_buf, container_data, sizeof(container_data));

  xnap_handover_req_ack_t orig = {
      .s_ng_node_ue_xnap_id = 123456,
      .t_ng_node_ue_xnap_id = 789012,
      .num_pdu_admitted = 2,
      .pdusession_admitted_list = pdu_list,
      .target2source =
          {
              .len = sizeof(container_data),
              .buf = container_buf,
          },
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_handover_request_acknowledge(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_handover_request_acknowledge failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_handover_req_ack_t decoded = {0};
  bool ret = decode_xnap_handover_request_acknowledge(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_handover_request_acknowledge failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_handover_request_acknowledge(&orig, &decoded);
  AssertFatal(ret, "XnAP Handover Request Acknowledge mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_handover_request_acknowledge(&decoded);
  free_xnap_handover_request_acknowledge(&orig);

  printf("%s() successful \n", __func__);
}

/**
 * 6. Xn Handover Preparation Failure
 */
static void test_xn_handover_preparation_failure(void)
{
  /* ---------- create message ---------- */
  xnap_handover_preparation_failure_t orig = {
      .s_ng_node_ue_xnap_id = 123456,
      .cause =
          {
              .type = XNAP_CAUSE_RADIO_NETWORK,
              .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_NO_RADIO_RESOURCES_AVAILABLE_IN_TARGET_CELL,
          },
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_handover_preparation_failure(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_handover_preparation_failure failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_handover_preparation_failure_t decoded = {0};
  bool ret = decode_xnap_handover_preparation_failure(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_handover_preparation_failure failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_handover_preparation_failure(&orig, &decoded);
  AssertFatal(ret, "XnAP Handover Preparation Failure mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_handover_preparation_failure(&decoded);
  free_xnap_handover_preparation_failure(&orig);

  printf("%s() successful \n", __func__);
}

/**
 * 7. XnAP SN Status Transfer Testing
 */
static void test_xn_sn_status_transfer(void)
{
  /* ---------- create message ---------- */
  xnap_sn_status_transfer_t orig = {
      .s_ng_node_ue_xnap_id = 123456,
      .t_ng_node_ue_xnap_id = 789012,
      .ran_status = {
              .nb_drb = 3,
              .drb_status_list = {
                      /* DRB 1 - 12-bit PDCP SN */
                      {
                          .drb_id = 1,
                          .ul_count = {
                                  .pdcp_sn = 2048,
                                  .hfn = 15,
                                  .sn_len = XNAP_SN_LENGTH_12,
                           },
                          .dl_count = {
                                  .pdcp_sn = 3072,
                                  .hfn = 20,
                                  .sn_len = XNAP_SN_LENGTH_12,
                           },
                      },
                      /* DRB 2 - 18-bit PDCP SN */
                      {
                          .drb_id = 5,
                          .ul_count = {
                                  .pdcp_sn = 131072,
                                  .hfn = 100,
                                  .sn_len = XNAP_SN_LENGTH_18,
                           },
                          .dl_count = {
                                  .pdcp_sn = 200000,
                                  .hfn = 150,
                                  .sn_len = XNAP_SN_LENGTH_18,
                           },
                      },
                      /* DRB 3 - Mixed: 12-bit UL, 18-bit DL */
                      {
                          .drb_id = 9,
                          .ul_count = {
                                  .pdcp_sn = 1024,
                                  .hfn = 5,
                                  .sn_len = XNAP_SN_LENGTH_12,
                           },
                          .dl_count = {
                                  .pdcp_sn = 100000,
                                  .hfn = 75,
                                  .sn_len = XNAP_SN_LENGTH_18,
                           },
                      },
              },
        },
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_sn_status_transfer(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_sn_status_transfer failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_sn_status_transfer_t decoded = {0};
  bool ret = decode_xnap_sn_status_transfer(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_sn_status_transfer failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_sn_status_transfer(&orig, &decoded);
  AssertFatal(ret, "XnAP SN Status Transfer mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_sn_status_transfer(&decoded);
  free_xnap_sn_status_transfer(&orig);

  printf("%s() successful \n", __func__);
}

/**
 * 8. XnAP UE Context Release Testing
 */
static void test_xn_ue_context_release(void)
{
  /* ---------- create message ---------- */
  xnap_ue_context_release_t orig = {
      .s_ng_node_ue_xnap_id = 123456,
      .t_ng_node_ue_xnap_id = 789012,
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_ue_context_release(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_ue_context_release failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_ue_context_release_t decoded = {0};
  bool ret = decode_xnap_ue_context_release(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_ue_context_release failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_ue_context_release(&orig, &decoded);
  AssertFatal(ret, "XnAP UE Context Release mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_ue_context_release(&decoded);
  free_xnap_ue_context_release(&orig);

  printf("%s() successful \n", __func__);
}

/**
 * 9. XnAP Handover Cancel Testing
 */
static void test_xn_handover_cancel(void)
{
  /* ---------- create message ---------- */
  xnap_handover_cancel_t orig = {
      .s_ng_node_ue_xnap_id = 111111,
      .cause = {
          .type = XNAP_CAUSE_RADIO_NETWORK,
          .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_PROCEDURE_CANCELLED,
      },
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_handover_cancel(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_handover_cancel failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_handover_cancel_t decoded = {0};
  bool ret = decode_xnap_handover_cancel(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_handover_cancel failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_handover_cancel(&orig, &decoded);
  AssertFatal(ret, "XnAP Handover Cancel mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_handover_cancel(&decoded);
  free_xnap_handover_cancel(&orig);

  printf("%s() successful\n", __func__);
}

/**
 * 10. XnAP Handover Success Testing
 */
static void test_xn_handover_success(void)
{
  /* ---------- create message ---------- */
  plmn_id_t plmn0 = {.mcc = 208, .mnc = 95, .mnc_digit_length = 2};

  xnap_handover_success_t orig = {
      .s_ng_node_ue_xnap_id = 333333,
      .t_ng_node_ue_xnap_id = 444444,
      .target_cgi = {.plmn_id = plmn0, .nrcell_id = 0xABCDEF012ULL},
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_handover_success(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_handover_success failed");

  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_handover_success_t decoded = {0};
  bool ret = decode_xnap_handover_success(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_handover_success failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_handover_success(&orig, &decoded);
  AssertFatal(ret, "XnAP Handover Success mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_handover_success(&decoded);
  free_xnap_handover_success(&orig);

  printf("%s() successful\n", __func__);
}

/**
 * 11. XnAP RAN Paging Testing – cell_List and rANAreaID_List variants
 */
static void test_xn_ran_paging(void)
{
  /* ---------- create message ---------- */
  plmn_id_t plmn0 = {.mcc = 208, .mnc = 95, .mnc_digit_length = 2};

  uint64_t *cell_ids = calloc_or_fail(3, sizeof(uint64_t));
  cell_ids[0] = 0x123456789ULL & 0xFFFFFFFFFULL;
  cell_ids[1] = 0xABCDEF012ULL & 0xFFFFFFFFFULL;
  cell_ids[2] = 0x100200300ULL & 0xFFFFFFFFFULL;

  xnap_ran_area_id_t *ran_area_ids = calloc_or_fail(2, sizeof(xnap_ran_area_id_t));
  ran_area_ids[0] = (xnap_ran_area_id_t){.tac = 0x001234, .ranac_present = true, .ranac = 7};
  ran_area_ids[1] = (xnap_ran_area_id_t){.tac = 0x005678, .ranac_present = false};

  uint8_t nr_cap[] = {0x10, 0x11, 0x12, 0x13};

  xnap_ran_paging_t cases[] = {
      {.ue_identity_index_value = 0x2A5,
       .ue_ran_paging_identity = 0xABCDE01234ULL,
       .paging_drx = XNAP_PAGING_DRX_128,
       .ran_paging_area = {.plmn = plmn0,
                           .choice = XNAP_RAN_PAGING_AREA_CELL_LIST,
                           .num_cells = 3,
                           .cell_ids = cell_ids}},
      {.ue_identity_index_value = 0x155,
       .ue_ran_paging_identity = 0x1234567890ULL,
       .paging_drx = XNAP_PAGING_DRX_64,
       .ran_paging_area = {.plmn = plmn0,
                           .choice = XNAP_RAN_PAGING_AREA_RAN_AREA_ID,
                           .num_ran_area_ids = 2,
                           .ran_area_ids = ran_area_ids}},
  };

  cases[1].paging_priority = malloc_or_fail(sizeof(xnap_paging_priority_t));
  *cases[1].paging_priority = XNAP_PAGING_PRIO_LEVEL8;
  cases[1].ran_paging_attempt_info = calloc_or_fail(1, sizeof(xnap_ran_paging_attempt_info_t));
  *cases[1].ran_paging_attempt_info =
      (xnap_ran_paging_attempt_info_t){.paging_attempt_count = 2, .intended_num_paging_attempts = 16};
  cases[1].ran_paging_attempt_info->next_paging_area_scope_changed = true;
  cases[1].ue_radio_cap_for_paging = create_byte_array(sizeof(nr_cap), nr_cap);

  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    /* ---------- encode ---------- */
    XNAP_XnAP_PDU_t *xnenc = encode_xnap_ran_paging(&cases[i]);
    AssertFatal(xnenc != NULL, "encode_xnap_ran_paging failed");
    XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
    xnap_msg_free(xnenc);

    /* ---------- decode ---------- */
    xnap_ran_paging_t decoded = {0};
    bool ret = decode_xnap_ran_paging(&decoded, xndec);
    AssertFatal(ret, "decode_xnap_ran_paging failed");
    xnap_msg_free(xndec);

    /* ---------- equality ---------- */
    ret = eq_xnap_ran_paging(&cases[i], &decoded);
    AssertFatal(ret, "XnAP RAN Paging mismatch (case %zu)\n", i);

    /* ---------- cleanup ---------- */
    free_xnap_ran_paging(&decoded);
    free_xnap_ran_paging(&cases[i]);
  }

  printf("%s() successful\n", __func__);
}

/**
 * 12. XnAP Retrieve UE Context Request (9.1.1.8) – RRC Resume (full/short I-RNTI)
 *     and RRC Reestablishment variants
 */
static void test_xn_retrieve_ue_context_request(void)
{
  xnap_retrieve_ue_context_request_t cases[] = {
      {
          .new_ng_node_ue_xnap_id = 0x0001ABCD,
          .ue_context_id = {.choice = XNAP_UE_CONTEXT_ID_RRC_RESUME,
                            .rrc_resume = {.i_rnti_type = XNAP_I_RNTI_FULL,
                                           .i_rnti = 0xAABBCCDDEEULL,
                                           .allocated_c_rnti = 0x1234,
                                           .access_pci = 511}},
          .integrity_protection = 0xBEEF,
          .new_cell_id = 0x123456789ULL,
      },
      {
          .new_ng_node_ue_xnap_id = 0x00ABCDEF,
          .ue_context_id = {.choice = XNAP_UE_CONTEXT_ID_RRC_RESUME,
                            .rrc_resume = {.i_rnti_type = XNAP_I_RNTI_SHORT,
                                           .i_rnti = 0xA5C3F0ULL,
                                           .allocated_c_rnti = 0xFFFF,
                                           .access_pci = 0}},
          .integrity_protection = 0x4321,
          .new_cell_id = 0x0ULL,
      },
      {
          .new_ng_node_ue_xnap_id = 0xFFFFFFFF,
          .ue_context_id = {.choice = XNAP_UE_CONTEXT_ID_RRC_REESTABLISHMENT,
                            .rrc_reest = {.c_rnti = 0x9ABC, .failure_cell_pci = 1007}},
          .integrity_protection = 0x0001,
          .new_cell_id = 0xABCDEF012ULL,
      },
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    /* ---------- encode ---------- */
    XNAP_XnAP_PDU_t *xnenc = encode_xnap_retrieve_ue_context_request(&cases[i]);
    AssertFatal(xnenc != NULL, "encode_xnap_retrieve_ue_context_request failed");
    XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
    xnap_msg_free(xnenc);

    /* ---------- decode ---------- */
    xnap_retrieve_ue_context_request_t decoded = {0};
    bool ret = decode_xnap_retrieve_ue_context_request(&decoded, xndec);
    AssertFatal(ret, "decode_xnap_retrieve_ue_context_request failed");
    xnap_msg_free(xndec);

    /* ---------- equality ---------- */
    ret = eq_xnap_retrieve_ue_context_request(&cases[i], &decoded);
    AssertFatal(ret, "XnAP Retrieve UE Context Request mismatch (case %zu)\n", i);

    /* ---------- cleanup ---------- */
    free_xnap_retrieve_ue_context_request(&decoded);
    free_xnap_retrieve_ue_context_request(&cases[i]);
  }

  printf("%s() successful\n", __func__);
}

/**
 * 13. XnAP Retrieve UE Context Response (9.1.1.9)
 */
static void test_xn_retrieve_ue_context_response(void)
{
  plmn_id_t plmn0 = {.mcc = 208, .mnc = 95, .mnc_digit_length = 2};

  transport_layer_addr_t tnl_addr_source = {.length = 4, .buffer = {192, 168, 1, 100}};
  transport_layer_addr_t tnl_addr_n3 = {.length = 4, .buffer = {10, 0, 0, 1}};

  nssai_t nssai0 = {.sst = 1, .sd = 0x000001};

  xnap_qos_flow_param_t qos_params = {.qos_type = NON_DYNAMIC,
                                      .nondyn = {.fiveQI = 9},
                                      .arp = {.priority_level = 2,
                                              .pre_emp_capability = PEC_SHALL_NOT_TRIGGER_PREEMPTION,
                                              .pre_emp_vulnerability = PEV_NOT_PREEMPTABLE}};

  xnap_qos_flow_tobe_setup_item_t *qos_list = calloc_or_fail(2, sizeof(*qos_list));
  qos_list[0].qfi = 1;
  qos_list[0].qos_params = qos_params;
  qos_list[1].qfi = 2;
  qos_list[1].qos_params = qos_params;
  qos_list[1].qos_params.nondyn.fiveQI = 5;

  xnap_pdusession_resources_tobe_setup_item_t *pdusession_list = calloc_or_fail(1, sizeof(*pdusession_list));
  pdusession_list[0].pdusession_id = 1;
  pdusession_list[0].nssai = calloc_or_fail(1, sizeof(nssai_t));
  *pdusession_list[0].nssai = nssai0;
  pdusession_list[0].n3_incoming = (gtpu_tunnel_t){.teid = 0x12345678, .addr = tnl_addr_n3};
  pdusession_list[0].pdu_session_type = PDUSessionType_ipv4;
  pdusession_list[0].num_qos = 2;
  pdusession_list[0].qos_list = qos_list;

  xnap_security_capabilities_t security_cap = {.nRencryption_algorithms = 0xC000,
                                               .nRintegrity_algorithms = 0xC000,
                                               .eUTRAencryption_algorithms = 0xC000,
                                               .eUTRAintegrity_algorithms = 0xC000};

  uint8_t as_key[32];
  for (int i = 0; i < 32; i++)
    as_key[i] = i;

  uint8_t rrc_data[] = {1, 2, 3, 4, 5};
  byte_array_t rrc_context = {.buf = calloc_or_fail(5, sizeof(uint8_t)), .len = 5};
  memcpy(rrc_context.buf, rrc_data, 5);

  xnap_ue_context_info_t ue_context = {.ngc_ue_sig_ref = 0x123456789,
                                       .cp_tnl_ip_source = tnl_addr_source,
                                       .security_capabilities = security_cap,
                                       .as_security_ncc = 1,
                                       .ue_ambr = {.br_ul = 100000000, .br_dl = 200000000},
                                       .rrc_context = rrc_context,
                                       .num_pdu = 1,
                                       .pdusession_resources_tobe_setup_list = pdusession_list};
  memcpy(ue_context.as_security_key_ranstar, as_key, 32);

  nr_guami_t guami = {.plmn = plmn0, .amf_region_id = 0x01, .amf_set_id = 0x0001, .amf_pointer = 0x01};

  xnap_retrieve_ue_context_response_t orig = {
      .new_ng_node_ue_xnap_id = 0xABCD,
      .old_ng_node_ue_xnap_id = 0x1234,
      .guami = guami,
      .ue_context = ue_context,
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_retrieve_ue_context_response(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_retrieve_ue_context_response failed");
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_retrieve_ue_context_response_t decoded = {0};
  bool ret = decode_xnap_retrieve_ue_context_response(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_retrieve_ue_context_response failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_retrieve_ue_context_response(&orig, &decoded);
  AssertFatal(ret, "XnAP Retrieve UE Context Response mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_retrieve_ue_context_response(&decoded);
  free_xnap_retrieve_ue_context_response(&orig);

  printf("%s() successful\n", __func__);
}

/**
 * 14. XnAP Retrieve UE Context Failure (9.1.1.10)
 */
static void test_xn_retrieve_ue_context_failure(void)
{
  xnap_retrieve_ue_context_failure_t orig = {
      .new_ng_node_ue_xnap_id = 0x00ABCDEF,
      .cause = {.type = XNAP_CAUSE_RADIO_NETWORK, .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_UNKNOWN_LOCAL_NG_RAN_NODE_UE_XNAP_ID},
  };
  uint8_t resume_container[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35};
  orig.old_to_new_resume_container = create_byte_array(sizeof(resume_container), resume_container);

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xnap_retrieve_ue_context_failure(&orig);
  AssertFatal(xnenc != NULL, "encode_xnap_retrieve_ue_context_failure failed");
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_retrieve_ue_context_failure_t decoded = {0};
  bool ret = decode_xnap_retrieve_ue_context_failure(&decoded, xndec);
  AssertFatal(ret, "decode_xnap_retrieve_ue_context_failure failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_retrieve_ue_context_failure(&orig, &decoded);
  AssertFatal(ret, "XnAP Retrieve UE Context Failure mismatch\n");

  /* ---------- cleanup ---------- */
  free_xnap_retrieve_ue_context_failure(&decoded);
  free_xnap_retrieve_ue_context_failure(&orig);

  printf("%s() successful\n", __func__);
}

int main() {
  printf("Starting XnAP Library Unit Tests...\n");

  /* Xn Interface Testing */
  test_xn_setup_request();
  test_xn_setup_response();
  test_xn_setup_failure();

  /* Xn Handover Testing*/
  test_xn_handover_request();
  test_xn_handover_request_acknowledge();
  test_xn_handover_preparation_failure();
  test_xn_sn_status_transfer();
  test_xn_ue_context_release();
  test_xn_handover_cancel();
  test_xn_handover_success();
  test_xn_ran_paging();

  /* Xn Retrieve UE Context Testing */
  test_xn_retrieve_ue_context_request();
  test_xn_retrieve_ue_context_response();
  test_xn_retrieve_ue_context_failure();

  printf("All XnAP tests passed!\n");
  return 0;
}
