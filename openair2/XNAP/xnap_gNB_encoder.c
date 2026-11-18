/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* APER encoding of an XnAP PDU, after an ASN.1 constraint check */

#include "xnap_gNB_encoder.h"
#include "asn_application.h"
#include "asn_codecs.h"
#include "assertions.h"
#include "common/utils/LOG/log.h"
#include "xer_encoder.h"

int xnap_gNB_encode_pdu(XNAP_XnAP_PDU_t *pdu, uint8_t **buffer, uint32_t *len)
{
  DevAssert(pdu != NULL);
  DevAssert(buffer != NULL);
  DevAssert(len != NULL);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, pdu);

  char errbuf[4096];
  size_t errlen = sizeof(errbuf);
  if (asn_check_constraints(&asn_DEF_XNAP_XnAP_PDU, pdu, errbuf, &errlen)) {
    xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, pdu);
    LOG_E(XNAP, "Constraint validation failed: %s\n", errbuf);
    return -1;
  }

  asn_encode_to_new_buffer_result_t res =
      asn_encode_to_new_buffer(NULL, ATS_ALIGNED_CANONICAL_PER, &asn_DEF_XNAP_XnAP_PDU, pdu);
  if (res.result.encoded <= 0) {
    LOG_E(XNAP, "Failed to encode XNAP PDU (present=%d)\n", pdu->present);
    return -1;
  }
  *buffer = res.buffer;
  *len = res.result.encoded;
  return 0;
}
