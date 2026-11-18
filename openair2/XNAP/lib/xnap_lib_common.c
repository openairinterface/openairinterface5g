/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "xnap_lib_common.h"

int xnap_gNB_set_cause(XNAP_Cause_t *cause_p,const xnap_cause_t *in)
{
  DevAssert(cause_p != NULL);
  switch (in->type) {
    case XNAP_CAUSE_RADIO_NETWORK:
      cause_p->present = XNAP_Cause_PR_radioNetwork;
      cause_p->choice.radioNetwork = in->value;
      break;
    case XNAP_CAUSE_TRANSPORT:
      cause_p->present = XNAP_Cause_PR_transport;
      cause_p->choice.transport = in->value;
      break;
    case XNAP_CAUSE_PROTOCOL:
      cause_p->present = XNAP_Cause_PR_protocol;
      cause_p->choice.protocol = in->value;
      break;
    case XNAP_CAUSE_MISC:
      cause_p->present = XNAP_Cause_PR_misc;
      cause_p->choice.misc = in->value;
      break;
    case XNAP_CAUSE_NOTHING:
    default:
      AssertFatal(false, "unknown cause value %d\n", in->type);
      break;
  }
  return 0;
}

xnap_cause_t decode_xnap_cause(const XNAP_Cause_t *in)
{
  xnap_cause_t out = {0};
  switch (in->present) {
     case XNAP_Cause_PR_radioNetwork:
         out.type = XNAP_CAUSE_RADIO_NETWORK;
         out.value = in->choice.radioNetwork;
         break;

     case XNAP_Cause_PR_transport:
         out.type = XNAP_CAUSE_TRANSPORT;
         out.value = in->choice.transport;
         break;

     case XNAP_Cause_PR_protocol:
         out.type = XNAP_CAUSE_PROTOCOL;
         out.value = in->choice.protocol;
         break;

     case XNAP_Cause_PR_misc:
         out.type = XNAP_CAUSE_MISC;
         out.value = in->choice.misc;
         break;
     case XNAP_Cause_PR_NOTHING:
     default:
         PRINT_ERROR("received illegal XNAP cause %d\n", in->present);
         break;
   }
   return out;
}

/* GUAMI (TS 38.423 §9.2.3.24): the AMF Region ID, Set ID and Pointer are BIT STRINGs */
XNAP_GUAMI_t encode_xnap_guami(const nr_guami_t *in)
{
  XNAP_GUAMI_t out = {0};
  MCC_MNC_TO_PLMNID(in->plmn.mcc, in->plmn.mnc, in->plmn.mnc_digit_length, &out.plmn_ID);
  AMF_REGION_TO_BIT_STRING(in->amf_region_id, &out.amf_region_id);
  AMF_SETID_TO_BIT_STRING(in->amf_set_id, &out.amf_set_id);
  AMF_POINTER_TO_BIT_STRING(in->amf_pointer, &out.amf_pointer);
  return out;
}

nr_guami_t decode_xnap_guami(const XNAP_GUAMI_t *in)
{
  nr_guami_t out = {0};
  PLMNID_TO_MCC_MNC(&in->plmn_ID, out.plmn.mcc, out.plmn.mnc, out.plmn.mnc_digit_length);
  out.amf_region_id = BIT_STRING_to_uint8(&in->amf_region_id);
  out.amf_set_id = BIT_STRING_to_uint16(&in->amf_set_id);
  out.amf_pointer = BIT_STRING_to_uint8(&in->amf_pointer);
  return out;
}

XNAP_Target_CGI_t xnap_encode_target_cgi(const xnap_ngran_cgi_t *in)
{
  DevAssert(in != NULL);

  XNAP_Target_CGI_t out = {0};
  out.present = XNAP_Target_CGI_PR_nr;
  asn1cCalloc(out.choice.nr, nr);
  const plmn_id_t *plmn_id = &in->plmn_id;
  MCC_MNC_TO_PLMNID(plmn_id->mcc, plmn_id->mnc, plmn_id->mnc_digit_length, &nr->plmn_id);
  NR_CELL_ID_TO_BIT_STRING(in->nrcell_id, &nr->nr_CI);

  return out;
}

bool xnap_decode_target_cgi(const XNAP_Target_CGI_t *in, xnap_ngran_cgi_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  memset(out, 0, sizeof(*out));

  if (in->present != XNAP_Target_CGI_PR_nr || in->choice.nr == NULL)
    return false;

  const XNAP_NR_CGI_t *nr = in->choice.nr;
  plmn_id_t *plmn_id = &out->plmn_id;
  PLMNID_TO_MCC_MNC(&nr->plmn_id, plmn_id->mcc, plmn_id->mnc, plmn_id->mnc_digit_length);

  BIT_STRING_TO_NR_CELL_IDENTITY(&nr->nr_CI, out->nrcell_id);

  return true;
}

XNAP_S_NSSAI_t xnap_encode_snssai(const nssai_t *in)
{
  DevAssert(in != NULL);

  XNAP_S_NSSAI_t out = {0};
  INT8_TO_OCTET_STRING(in->sst, &out.sst);
  if (in->sd) {
    asn1cCalloc(out.sd, sd);
    INT24_TO_OCTET_STRING(in->sd, sd);
  }

  return out;
}

bool decode_xnap_snssai(const XNAP_S_NSSAI_t *in, nssai_t *out)
{
  DevAssert(in != NULL);
  DevAssert(out != NULL);

  memset(out, 0, sizeof(*out));
  OCTET_STRING_TO_INT8(&in->sst, out->sst);
  if (in->sd)
    OCTET_STRING_TO_INT24(in->sd, out->sd);

  return true;
}

bool eq_xnap_guami(const nr_guami_t *a, const nr_guami_t *b)
{
  if (!eq_xnap_plmn(&a->plmn, &b->plmn))
    return false;

  _EQ_CHECK_INT(a->amf_region_id, b->amf_region_id);
  _EQ_CHECK_INT(a->amf_set_id, b->amf_set_id);
  _EQ_CHECK_INT(a->amf_pointer, b->amf_pointer);
  return true;
}

bool eq_xnap_ngran_cgi(const xnap_ngran_cgi_t *a, const xnap_ngran_cgi_t *b)
{
  if (!eq_xnap_plmn(&a->plmn_id, &b->plmn_id))
    return false;
  _EQ_CHECK_UINT64(a->nrcell_id, b->nrcell_id);
  return true;
}

bool eq_transport_layer_addr(const transport_layer_addr_t *a, const transport_layer_addr_t *b)
{
  _EQ_CHECK_INT(a->length, b->length);
  if (memcmp(a->buffer, b->buffer, a->length) != 0) {
    PRINT_ERROR("XnAP Equality failed: transport layer address buffers differ\n");
    return false;
  }
  return true;
}

bool eq_gtpu_tunnel(const gtpu_tunnel_t *a, const gtpu_tunnel_t *b)
{
  _EQ_CHECK_UINT32(a->teid, b->teid);
  return eq_transport_layer_addr(&a->addr, &b->addr);
}

bool eq_xnap_cause(const xnap_cause_t *a, const xnap_cause_t *b)
{
  _EQ_CHECK_INT(a->type,  b->type);
  _EQ_CHECK_INT(a->value, b->value);
  return true;
}

bool eq_xnap_plmn(const plmn_id_t *a, const plmn_id_t *b)
{
  _EQ_CHECK_INT(a->mcc, b->mcc);
  _EQ_CHECK_INT(a->mnc, b->mnc);
  _EQ_CHECK_INT(a->mnc_digit_length, b->mnc_digit_length);
  return true;
}

bool eq_xnap_snssai(const nssai_t *a, const nssai_t *b)
{
  _EQ_CHECK_INT(a->sst, b->sst);
  /* Handle optional sd */
  if (a->sd && b->sd) {
    _EQ_CHECK_UINT32(a->sd, b->sd);
  } else if (a->sd || b->sd) {
    return false;
  }
  return true;
}

bool eq_xnap_plmn_support(const xnap_plmn_support_t *a, const xnap_plmn_support_t *b)
{
  if (!eq_xnap_plmn(&a->plmn, &b->plmn)) return false;
  _EQ_CHECK_INT(a->num_nssai, b->num_nssai);
  for (int i = 0; i < a->num_nssai; i++) {
    if (!eq_xnap_snssai(&a->nssai[i], &b->nssai[i]))
      return false;
  }
  return true;
}

bool eq_xnap_tai_support(const xnap_tai_support_t *a, const xnap_tai_support_t *b)
{
  _EQ_CHECK_UINT32(a->tac, b->tac);
  _EQ_CHECK_INT(a->num_plmn, b->num_plmn);
  for (int i = 0; i < a->num_plmn; i++) {
    if (!eq_xnap_plmn_support(&a->plmn_support[i], &b->plmn_support[i]))
      return false;
  }
  return true;
}
