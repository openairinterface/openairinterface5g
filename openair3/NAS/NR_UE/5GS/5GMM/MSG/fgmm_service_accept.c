/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "fgmm_service_accept.h"
#include <string.h>
#include <arpa/inet.h> // For htons and ntohs
#include <stdlib.h> // For malloc and free
#include "common/utils/utils.h" // For malloc_or_fail
#include "common/utils/ds/byte_array.h" // For byte_array_t
#include "common/utils/eq_check.h"
#include "fgmm_lib.h"

/** @brief Encode Service Accept (8.2.17 of 3GPP TS 24.501) */
int encode_fgs_service_accept(byte_array_t *buffer, const fgs_service_accept_msg_t *msg)
{
  if (buffer->len < 1)
    return -1; // No IEs present

  byte_array_t ba = *buffer; // Local copy of buffer
  uint32_t encoded = 0;

  // Encode PDU Session Status (Optional)
  if (msg->has_psi_status) {
    ENCODE_NAS_IE(ba, encode_pdu_session_ie(&ba, IEI_PDU_SESSION_STATUS, msg->psi_status), encoded);
  }

  // Encode PDU Session Reactivation Result (Optional)
  if (msg->has_psi_res) {
    ENCODE_NAS_IE(ba, encode_pdu_session_ie(&ba, IEI_PDU_SESSION_REACT_RESULT, msg->psi_res), encoded);
  }

  // Encode PDU Session Reactivation Result Error Cause (Optional)
  if (msg->num_errors) {
    int start = encoded;
    buffer->buf[encoded++] = IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE;
    // encode length of PDU session reactivation result error cause
    uint16_t error_len = msg->num_errors * sizeof(msg->cause[0]);
    if (error_len > MAX_NUM_PDU_ERRORS) {
      PRINT_ERROR("encoded length is out of bound (IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE)\n");
      return -1;
    }
    uint16_t n_len = htons(error_len);
    memcpy(&buffer->buf[encoded], &n_len, sizeof(n_len)); // length payload PDU Session Status
    encoded += sizeof(error_len);
    // encode PDU Session IDs and Causes
    for (int i = 0; i < msg->num_errors; i++) {
      buffer->buf[encoded++] = msg->cause[i].pdu_session_id;
      buffer->buf[encoded++] = msg->cause[i].cause;
    }
    UPDATE_BYTE_ARRAY(ba, encoded - start);
  }

  /* Encode T3448 Timer (Optional)
     3GPP TS 24.008 10.5.7.3 GPRS Timer */
  if (msg->t3448) {
    ENCODE_NAS_IE(ba, encode_gprs_timer_ie(&ba, IEI_T3448_VALUE, msg->t3448), encoded);
  }

  return encoded;
}

/** @brief Decode Service Accept (8.2.17 of 3GPP TS 24.501) */
int decode_fgs_service_accept(fgs_service_accept_msg_t *msg, const byte_array_t *buffer)
{
  if (buffer->len < 1)
    return -1; // nothing to decode

  byte_array_t ba = *buffer; // Local copy of buffer (to be used in the decoding sub-functions)
  uint32_t decoded = 0;

  // Default output message before decoding
  msg->has_psi_res = false;
  msg->has_psi_status = false;
  msg->num_errors = 0;
  msg->t3448 = NULL;

  // Decode Optional IEs
  while (decoded < buffer->len) {
    uint8_t iei = buffer->buf[decoded++];
    UPDATE_BYTE_ARRAY(ba, 1);

    switch (iei) {
      case IEI_PDU_SESSION_STATUS:
        msg->has_psi_status = true;
        DECODE_NAS_IE(ba, decode_pdu_session_ie(msg->psi_status, &ba), decoded);
      break;

      case IEI_PDU_SESSION_REACT_RESULT:
        msg->has_psi_res = true;
        DECODE_NAS_IE(ba, decode_pdu_session_ie(msg->psi_res, &ba), decoded);
      break;

      case IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE: {
        int start = decoded;
        // Decode the length of the IE
        uint16_t tmp;
        if (buffer->len - decoded < sizeof(tmp)) {
          PRINT_ERROR("IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE: missing length\n");
          return -1;
        }
        memcpy(&tmp, &buffer->buf[decoded], sizeof(tmp));
        uint16_t error_len = ntohs(tmp);
        decoded += sizeof(error_len);
        // pairs of PDU session ID and cause, which must fit in the message and in msg->cause
        if (error_len % 2 != 0 || error_len > buffer->len - decoded || msg->num_errors + error_len / 2 > MAX_NUM_PDU_ERRORS) {
          PRINT_ERROR("IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE: decoded length is out of bound\n");
          return -1;
        }
        // Decode each PDU Session ID and Cause
        for (int i = 0; i < error_len; i += 2) {
          msg->cause[msg->num_errors].pdu_session_id = buffer->buf[decoded++];
          msg->cause[msg->num_errors].cause = buffer->buf[decoded++];
          msg->num_errors++;
        }
        UPDATE_BYTE_ARRAY(ba, decoded - start);
      } break;

      case IEI_T3448_VALUE:
        msg->t3448 = malloc_or_fail(sizeof(*msg->t3448));
        DECODE_NAS_IE(ba, decode_gprs_timer_ie(msg->t3448, &ba), decoded);
        break;

      case IEI_EAPMSG: {
        // decode_eap_msg_ie() copies the contents into eap.buf
        uint8_t eap_buf[MAX_EAP_CONTENTS_LEN];
        byte_array_t eap = {.buf = eap_buf};
        DECODE_NAS_IE(ba, decode_eap_msg_ie(&eap, &ba), decoded);
        msg->eap_msg = create_byte_array(eap.len, eap.buf);
      } break;

      default:
        PRINT_ERROR("Unkwown or invalid IEI %d\n", iei);
        return -1;
    }
  }

  return decoded;
}

/**
 * @brief Equality check for Service Accept (enc/dec)
 */
bool eq_service_accept(const fgs_service_accept_msg_t *a, const fgs_service_accept_msg_t *b)
{
  _EQ_CHECK_INT(a->has_psi_status, b->has_psi_status);
  if (a->has_psi_status && b->has_psi_status) {
    for (int i = 0; i < MAX_NUM_PSI; i++)
      _EQ_CHECK_INT(a->psi_status[i], b->psi_status[i]);
  }
  _EQ_CHECK_INT(a->has_psi_res, b->has_psi_res);
  if (a->has_psi_res && b->has_psi_res) {
    for (int i = 0; i < MAX_NUM_PSI; i++)
      _EQ_CHECK_INT(a->psi_res[i], b->psi_res[i]);
  }
  _EQ_CHECK_INT(a->num_errors, b->num_errors);
  if (a->num_errors && b->num_errors) {
    for (int i = 0; i < a->num_errors; i++) {
      _EQ_CHECK_INT(a->cause[i].cause, b->cause[i].cause);
      _EQ_CHECK_INT(a->cause[i].pdu_session_id, b->cause[i].pdu_session_id);
    }
  }
  if (a->t3448 != NULL || b->t3448 != NULL) {
    if (a->t3448 == NULL || b->t3448 == NULL) {
      PRINT_ERROR("t3448 equality check failed\n");
      return false;
    }
    if (!eq_gprs_timer(a->t3448, b->t3448))
      return false;
  }
  return true;
}

void free_fgs_service_accept(fgs_service_accept_msg_t *msg)
{
  free(msg->t3448);
  free_byte_array(msg->eap_msg);
}
