/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "fgmm_lib.h"
#include <string.h>
#include <arpa/inet.h> // For htons and ntohs
#include <stdlib.h> // For malloc and free
#include "common/utils/ds/byte_array.h"
#include "common/utils/eq_check.h"

#define IEI_LEN 1
#define GPRS_TIMER_LENGTH 3 // octets
#define FGS_NAS_CAUSE_LENGTH 1
#define MIN_EAP_MSG_LEN 7
#define MIN_EAP_CONTENTS_LEN 4

/**
 * Encode PDU Session Status and PDU session reactivation result (optional IEs)
 * According to 9.11.3.42 and 9.11.3.44 of 3GPP TS 24.501, both IEs share the same
 * structure, therefore this function covers them both.
 * @param iei IE identifier to distinguish between PDU Session Status and PDU session reactivation result.
 * @param psi pointer to PDU Session Status IE buffer to be encoded: index i corresponds to PDU Session i
 */
int encode_pdu_session_ie(byte_array_t *buffer, nas_service_IEI_t iei, const uint8_t *psi)
{
  if (buffer->len < MIN_PDU_SESSION_CONTENTS_LEN + 2) {
    PRINT_ERROR("%s failed: buffer is too small", __func__);
    return -1;
  }

  uint8_t *buf = buffer->buf;
  int encoded = 0;
  buf[encoded++] = iei;
  buf[encoded++] = MIN_PDU_SESSION_CONTENTS_LEN; // no spare octets

  // Initialize the PDU session status octets to zero
  buf[encoded] = 0x00;
  buf[encoded + 1] = 0x00;

  /* Encode Octet 3 (PSI 1-7) and Octet 4 (PSI 8-15).
     Set each PSI bit according to the psi array containing the content IEs.
     Start from bit 1 of octet 3 (i.e. PSI(0)), which is spare and shall be coded as zero.
     The enabled bit translates to either PDU Session Active or PDU Session Reactivation Result */
  for (int i = 1; i < 8; i++) { // skip spare bit
    buf[encoded] |= psi[i] << i;
  }
  encoded++;

  for (int i = 8; i < MAX_NUM_PSI; i++) {
    buf[encoded] |= psi[i] << i % 8;
  }
  encoded++;

  return encoded;
}

/**
 * Decode PDU Session Status and PDU session reactivation result (optional IEs)
 * According to 9.11.3.42 and 9.11.3.44 of 3GPP TS 24.501, both IEs share the same
 * structure, therefore this function covers them both.
 * @param psi pointer to buffer for the PDU Session Status IE to be decoded
 */
int decode_pdu_session_ie(uint8_t *psi, const byte_array_t *buffer)
{
  int decoded = 0;
  uint8_t *buf = buffer->buf;

  if (buffer->len < MIN_PDU_SESSION_CONTENTS_LEN + 2) {
    PRINT_ERROR("%s failed: buffer length is too small", __func__);
    return -1;
  }

  // start decoding from length IE (IEI is decoded in the caller)
  uint8_t pdu_session_status_len = buf[decoded++];
  if (pdu_session_status_len > MAX_PDU_SESSION_CONTENTS_LEN) {
    PRINT_ERROR("decoded length is out of bound for PDU Session Status/Reactivation result\n");
    return -1;
  }

  /* Decode PDU Session Status content IEs bits in octet 3 (PSI 1-7).
     Store the result psi array.
     Skip bit 1 of octet 3 (i.e. PSI(0)), which is spare and shall be coded as zero. */
  for (int i = 1; i < 8; i++) { // skip spare bit
    psi[i] = (buf[decoded] >> i) & 0x01;
  }
  decoded++;

  /* Decode PDU Session Status content IEs bits in octet 4 (PSI 8-15).
     Store the result psi array. */
  for (int i = 8; i < MAX_NUM_PSI; i++) {
    psi[i] = (buf[decoded] >> i % 8) & 0x01;
  }
  decoded++;

  return decoded;
}

/* Encode GPRS Timer IE (3GPP TS 24.008 10.5.7.3) */
int encode_gprs_timer_ie(byte_array_t *buffer, nas_service_IEI_t iei, const gprs_timer_t *timer)
{
  if (buffer->len < GPRS_TIMER_LENGTH) {
    PRINT_ERROR("Buffer length is too short to encode the GPRS Timer IE\n");
    return -1;
  }

  uint8_t *buf = buffer->buf;

  int encoded = 0;
  // IEI
  buf[encoded++] = iei;
  // length
  buf[encoded++] = 1; // length field (1 octet)
  // Unit and value (1 octet)
  uint8_t encoded_byte = 0;
  encoded_byte |= (timer->unit & 0x07) << 5;
  encoded_byte |= timer->value & 0x1F;
  buf[encoded++] = encoded_byte;

  return encoded;
}

/* Decode GPRS Timer IE (3GPP TS 24.008 10.5.7.3) */
int decode_gprs_timer_ie(gprs_timer_t *timer, const byte_array_t *buffer)
{
  if (buffer->len < GPRS_TIMER_LENGTH - IEI_LEN) {
    PRINT_ERROR("Buffer length is too short to decode the GPRS Timer IE\n");
    return -1;
  }

  int decoded = 1; // Skip length ie (1 octet)
  // Extract unit and value
  uint8_t *buf = buffer->buf;
  timer->unit = (buf[decoded] >> 5) & 0x07;
  timer->value = buf[decoded++] & 0x1F;
  return decoded;
}

/** @brief Equality check for GPRS Timer */
bool eq_gprs_timer(const gprs_timer_t *a, const gprs_timer_t *b)
{
  _EQ_CHECK_INT(a->value, b->value);
  _EQ_CHECK_INT(a->unit, b->unit);
  return true;
}

/** @brief Encode EAP message IE (3GPP TS 24.501 9.11.2.2) */
int encode_eap_msg_ie(byte_array_t *buffer, const byte_array_t *msg)
{
  int encoded = 0;

  // Sanity check on buffer length
  if (buffer->len < MIN_EAP_MSG_LEN) {
    PRINT_ERROR("Invalid buffer length %ld", buffer->len);
    return -1;
  }

  // Sanity check on input length
  if (msg->len < MIN_EAP_CONTENTS_LEN || msg->len > MAX_EAP_CONTENTS_LEN) {
    PRINT_ERROR("Invalid EAP message length %ld", msg->len);
    return -1;
  }

  // Encode IEI
  buffer->buf[encoded++] = IEI_EAPMSG;

  // Encode length of EAP message content
  uint16_t len = htons(msg->len);
  memcpy(&buffer->buf[encoded], &len, sizeof(len));
  encoded += sizeof(len);

  // Encode EAP message content
  memcpy(&buffer->buf[encoded], msg->buf, msg->len);
  encoded += msg->len;

  return encoded;
}

/** @brief Decode EAP message IE (3GPP TS 24.501 9.11.2.2):
    length of contents and message payload */
int decode_eap_msg_ie(byte_array_t *eap_msg, const byte_array_t *buffer)
{
  int decoded = 0;
  int min_len_to_decode = MIN_EAP_MSG_LEN - IEI_LEN;

  // Check buffer length validity
  if (buffer->len < min_len_to_decode) {
    PRINT_ERROR("Invalid buffer for decoding EAP message IE");
    return -1;
  }

  // Decode length IE
  eap_msg->len = (buffer->buf[decoded] << 8) | buffer->buf[decoded + 1];
  decoded += 2;

  // Check payload length validity
  if (eap_msg->len < MIN_EAP_CONTENTS_LEN || eap_msg->len > MAX_EAP_CONTENTS_LEN) {
    PRINT_ERROR("Invalid EAP message length %ld", eap_msg->len);
    return -1;
  }

  // Check for buffer overflow
  if (buffer->len < decoded + eap_msg->len) {
    PRINT_ERROR("Buffer too short for EAP message: %ld < %ld", buffer->len, decoded + eap_msg->len);
    return -1;
  }

  // Copy EAP payload
  memcpy(eap_msg->buf, &buffer->buf[decoded], eap_msg->len);
  decoded += eap_msg->len;

  return decoded;
}

int encode_fgs_nas_cause(byte_array_t *buffer, const cause_id_t *cause)
{
  uint32_t encoded = 0;

  if (buffer->len < FGS_NAS_CAUSE_LENGTH)
    return -1;

  buffer->buf[encoded++] = *cause;

  return encoded;
}

int decode_fgs_nas_cause(cause_id_t *cause, const byte_array_t *buffer)
{
  int decoded = 0;

  if (buffer->len < FGS_NAS_CAUSE_LENGTH)
    return -1; // nothing to decode

  *cause = buffer->buf[decoded++];

  return decoded;
}
