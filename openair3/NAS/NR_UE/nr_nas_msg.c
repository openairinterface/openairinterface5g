/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Definitions of handlers and callbacks for NR NAS UE task
 */

#include "nr_nas_msg.h"
#include <netinet/in.h>
#include "NR_NAS_defs.h"
#include <openssl/opensslv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/utils/ds/byte_array.h"
#include "common/utils/nr/nr_common.h"
#include "AuthenticationResponseParameter.h"
#include "FGCNasMessageContainer.h"
#include "FGSDeregistrationRequestUEOriginating.h"
#include "FGSDeregistrationType.h"
#include "NasKeySetIdentifier.h"
#include "NrUESecurityCapability.h"
#include "OctetString.h"
#include "PduSessionEstablishRequest.h"
#include "PduSessionEstablishmentAccept.h"
#include "PduSessionModificationCommand.h"
#include "PduSessionModificationComplete.h"
#include "RegistrationAccept.h"
#include "SORTransparentContainer.h"
#include "FGSIdentityResponse.h"
#include "fgmm_authentication_request.h"
#include "fgmm_identity_request.h"
#include "T.h"
#include "aka_functions.h"
#include "assertions.h"
#include "common/utils/ds/byte_array.h"
#include "commonDef.h"
#include "intertask_interface.h"
#include "kdf.h"
#include "key_nas_deriver.h"
#include "nas_log.h"
#include "openair3/UICC/usim_interface.h"
#include "openair3/UTILS/conversions.h"
#include "secu_defs.h"
#include "utils.h"
#include "fgs_nas_utils.h"
#include "fgmm_service_accept.h"
#include "fgmm_service_reject.h"
#include "tuntap_if.h"
#include "fgmm_registration_reject.h"
#include "fgmm_authentication_reject.h"
#include "ds/byte_array.h"
#include "key_nas_deriver.h"
#include "nr-uesoftmodem.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_qos_rule.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include "curve_25519.h"
#include "aes_128_ctr.h"
#include "x963_kdf.h"
#include "sha_256_hmac.h"
static const char hex[] = "0123456789abcdef";
#elif OPENSSL_VERSION_NUMBER >= 0x10100000L
#define MY_OPENSSL_VERSION_STR OpenSSL_version(OPENSSL_VERSION)
#else
#define MY_OPENSSL_VERSION_STR SSLeay_version(SSLEAY_VERSION)
#endif

static nr_ue_nas_t nr_ue_nas[MAX_NUM_NR_UE_INST] = {0};

nr_ue_nas_t *get_nr_ue_nas_info(uint8_t ue_inst)
{
  AssertFatal(ue_inst >= 0 && ue_inst < MAX_NUM_NR_UE_INST, "Invalid UE instance\n");
  return &nr_ue_nas[ue_inst];
}

#define FOREACH_STATE(TYPE_DEF)                  \
  TYPE_DEF(NAS_SECURITY_NO_SECURITY_CONTEXT, 0)  \
  TYPE_DEF(NAS_SECURITY_UNPROTECTED, 1)          \
  TYPE_DEF(NAS_SECURITY_INTEGRITY_PASSED, 2)     \
  TYPE_DEF(NAS_SECURITY_NEW_SECURITY_CONTEXT, 3) \
  TYPE_DEF(NAS_SECURITY_INTEGRITY_FAILED, 4)     \
  TYPE_DEF(NAS_SECURITY_BAD_INPUT, 5)

const char *nr_release_cause_desc[] = {"RRC_CONNECTION_FAILURE", "RRC_RESUME_FAILURE", "OTHER"};

typedef enum { FOREACH_STATE(TO_ENUM) } security_state_t;

static const text_info_t security_state_info[] = {FOREACH_STATE(TO_TEXT)};

static fgmm_msg_header_t set_mm_header(fgs_nas_msg_t type, Security_header_t security)
{
  fgmm_msg_header_t mm_header = {0};
  mm_header.ex_protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  mm_header.security_header_type = security;
  mm_header.message_type = type;
  return mm_header;
}

static void servingNetworkName(uint8_t *msg, plmn_id_t *plmn_id)
{
  // SNN-network-identifier in TS 24.501
  // TS 24.501: If the MNC of the serving PLMN has two digits, then a zero is added at the beginning.
  int size = 64;

  snprintf((char *)msg, size, "5G:mnc%03d.mcc%03d.3gppnetwork.org", plmn_id->mnc, plmn_id->mcc);
}

static const char *print_info(uint8_t id, const text_info_t *array, uint8_t array_size)
{
  for (uint8_t i = 0; i < array_size; i++) {
    if (array[i].id == id) {
      return array[i].text;
    }
  }
  return "N/A";
}

/** @brief Check whether a message belongs to the list of messages that
 *  are allowed to be integrity unprotected (4.4.4.2 3GPP TS 24.501) */
static bool unprotected_allowed(byte_array_t buffer, fgs_nas_msg_t msg_type)
{
  switch (msg_type) {
    case FGS_IDENTITY_REQUEST: // check on SUCI done in the handler
    case FGS_AUTHENTICATION_REQUEST:
    case FGS_AUTHENTICATION_RESULT:
    case FGS_AUTHENTICATION_REJECT:
    case FGS_DEREGISTRATION_ACCEPT_UE_ORIGINATING: // for non switch off: deregistration type IE set to NORMAL_DEREGISTRATION
      return true;
    case FGS_REGISTRATION_REJECT:
      // unprotected if the 5GMM cause is not #76 (9.11.3.2)
      return buffer.len >= sizeof(fgmm_msg_header_t) + 1
             && buffer.buf[3] != Not_authorized_for_this_CAG_or_authorized_for_CAG_cells_only;
    case FGS_SERVICE_REJECT:
      // unprotected if the 5GMM cause is not #76
      return buffer.buf[4] != Not_authorized_for_this_CAG_or_authorized_for_CAG_cells_only;
    default:
      return false;
  }
}

/**
 * @brief Get the MAC of a Security Protected NAS message
 * @param[in] pdu_buffer The buffer containing the NAS message
 * @param[in] pdu_length The length of the NAS message
 * @param[out] mac The MAC of the NAS message
 * @return true if the MAC was successfully extracted, false otherwise
 */
bool nas_security_get_mac(uint8_t *pdu_buffer, int pdu_length, uint8_t *mac)
{
  /* Check for Security Protected Header */
  /* at least 8 bytes for Security Protected MM Message */
  if (pdu_length < 8)
    return false;

  /* Only message type, that is not protected (c.f.
     TS 24.501 9.3 Security Header Type) */
  if ((Security_header_t)pdu_buffer[1] == PLAIN_5GS_MSG)
    return false;

  /* Get the MAC [EPD][SecHdr][MAC0]..[MAC3] - success */
  for (int i = 0; i < 4; i++)
    mac[i] = pdu_buffer[2 + i];
  return true;
}

/*
 * @brief Get the Security Header of a NAS message
 * @param[in] msg The buffer containing the NAS message
 * @param[in] msg_length The length of the NAS message
 * @param[out] sec_hdr The Security Header of the NAS message
 * @return true if the Security Header was successfully extracted, false otherwise
 */
bool nas_security_get_sec_hdr(uint8_t *msg, int msg_length, Security_header_t *sec_hdr)
{
  /* Shortest message is [EPD][SecHdrType][Payl]*/
  if (msg_length < 3) {
    LOG_E(NAS, "Invalid NAS message length %d\n", msg_length);
    return false;
  }

  /* Get the SecHdr and check for validity */
  uint8_t sec_hdr_type = msg[1] & 0x0f;
  if (sec_hdr_type > INTEGRITY_PROTECTED_AND_CIPHERED_WITH_NEW_SECU_CTX) {
    LOG_E(NAS, "Invalid Security Header Type %d\n", sec_hdr_type);
    return false;
  }

  /* Check ok. */
  *sec_hdr = (Security_header_t)sec_hdr_type;
  return true;
}

/*
 * @brief Compute the MAC of a NAS message
 * @param[in] nas The NAS context
 * @param[in] pdu_buffer The buffer containing the NAS message
 * @param[in] is_uplink True if the message is uplink, false Downlink
 * @param[in] is3gpp_access True if the message is 3GPP access, false otherwise
 * @param[in] pdu_length The length of the NAS message
 * @param[out] mac The MAC of the NAS message
 */
static void nas_security_compute_mac(nr_ue_nas_t *nas,
                                     byte_array_t buffer,
                                     const bool is_uplink,
                                     const bool is3gpp_access,
                                     uint8_t *mac)
{
  uint8_t *buf = buffer.buf + SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH - 1;
  int len = buffer.len - SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH + 1;

  /* Compute the MAC */
  nas_stream_cipher_t stream_cipher;
  stream_cipher.context = nas->security_container->integrity_context;

  /* Select the right count in the context */
  if (is_uplink)
    stream_cipher.count = nas->security.nas_count_ul;
  else
    stream_cipher.count = nas->security.nas_count_dl;

  /* 3GPP access is 1, non-3GPP access is 2 - see 3GPP TS 33.501 6.4.2.2 */
  stream_cipher.bearer = is3gpp_access ? 1 : 2;

  /* Configure Direction for MAC protection */
  stream_cipher.direction = is_uplink ? 0 : 1;

  /* Possibly encrypted message  */
  stream_cipher.message = buf;

  /* length in bits */
  stream_cipher.blength = len << 3;

  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);
}

/*
 * @brief: Decrypt the payload of a NAS message. The buffer is modified in place
 * @param[in] nas The NAS context
 * @param[in] pdu_buffer The buffer containing the full (header + payload) NAS message
 * @param[in] is3gpp_access True if the message is 3GPP access, false otherwise
 * @param[in] pdu_length The length of the NAS message
 */
static void nas_security_decrypt_payload(nr_ue_nas_t *nas, byte_array_t buffer, const bool is3gpp_access)
{
  Security_header_t sec_hdr;

  if (!nas_security_get_sec_hdr(buffer.buf, buffer.len, &sec_hdr)) {
    LOG_E(NAS, "Failed to get Security Header\n");
    return;
  }

  /* Nothing to do for unencrypted msgs */
  if (sec_hdr == PLAIN_5GS_MSG || sec_hdr == INTEGRITY_PROTECTED) {
    return;
  }

  /* Get integrity keys, and algorithms */
  nas_stream_cipher_t stream_cipher;
  stream_cipher.context = nas->security_container->ciphering_context;

  /* Use the estimated count the right count in the context */
  stream_cipher.count = nas->security.nas_count_dl;
  /* 3GPP access is 1, non-3GPP access is 2 - see 3GPP TS 33.501 6.4.2.2 */
  stream_cipher.bearer = is3gpp_access ? 1 : 2;

  /* Decryption only in downlink direction */
  stream_cipher.direction = 1;

  /* [EPD][SHR][MAC0]..[MAC3][PDU...]*/
  uint8_t *plain_payload = buffer.buf + SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH;
  stream_cipher.message = plain_payload;

  /* length in bits */
  int plain_length = buffer.len - SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH;
  stream_cipher.blength = plain_length << 3;

  /* Allocate output buffer for body only */
  uint8_t *decrypted = malloc_or_fail(plain_length);

  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, decrypted);

  /* Override and free the decrypted payload */
  memcpy(plain_payload, decrypted, plain_length);
  free(decrypted);
}

static fgs_nas_msg_t get_msg_type(uint8_t *pdu_buffer, uint32_t length)
{
  if (pdu_buffer == NULL)
    goto error;

  /* get security header type */
  if (length < 2)
    goto error;

  int security_header_type = pdu_buffer[1];

  if (security_header_type == 0) {
    /* plain NAS message */
    if (length < 3)
      goto error;
    return pdu_buffer[2];
  }

  if (length < 10)
    goto error;

  int msg_type = pdu_buffer[9];

  if (msg_type == FGS_DOWNLINK_NAS_TRANSPORT) {
    if (length < 17)
      goto error;

    msg_type = pdu_buffer[16];
  }

  return msg_type;

error:
  LOG_E(NAS, "[UE] Received invalid downlink message\n");
  return 0;
}

static security_state_t nas_security_rx_process(nr_ue_nas_t *nas, byte_array_t buffer)
{
  if (nas->security_container == NULL)
    return NAS_SECURITY_NO_SECURITY_CONTEXT;

  if (buffer.len < sizeof(fgmm_msg_header_t)) {
    LOG_E(NAS, "Invalid buffer length = %ld: must hold at least a plain 5GMM header\n", buffer.len);
    return NAS_SECURITY_BAD_INPUT;
  }
  int security_type = buffer.buf[1];
  LOG_D(NAS, "Security type is: %s\n", print_info(security_type, security_header_type_s, sizeofArray(security_header_type_s)));

  switch (security_type) {
    case PLAIN_5GS_MSG: {
      fgs_nas_msg_t msg_type = get_msg_type(buffer.buf, buffer.len);
      return unprotected_allowed(buffer, msg_type) ? NAS_SECURITY_UNPROTECTED : NAS_SECURITY_BAD_INPUT;
      break;
    }
    case INTEGRITY_PROTECTED_WITH_NEW_SECU_CTX:
      stream_security_container_delete(nas->security_container);
      nas->security_container = NULL;
      nas->security.nas_count_dl = 0;
      return NAS_SECURITY_NO_SECURITY_CONTEXT;
      break;
    case INTEGRITY_PROTECTED:
    case INTEGRITY_PROTECTED_AND_CIPHERED_WITH_NEW_SECU_CTX:
      /* only accept "integrity protected and ciphered" messages */
      if (buffer.buf[6] == 0)
        LOG_E(NAS, "Received nas_count_dl = %d\n", buffer.buf[6]);
      LOG_E(NAS, "todo: unhandled security type %s (buffer.buf[1] = %d)\n", security_header_type_s[buffer.buf[1]].text, buffer.buf[1]);
      return NAS_SECURITY_BAD_INPUT;
      break;
    default:
      break;
  }

  /* header is 7 bytes, require at least one byte of payload */
  if (buffer.len < 8) {
    LOG_E(NAS, "Invalid buffer length = %ld\n", buffer.len);
    return NAS_SECURITY_BAD_INPUT;
  }

  /* synchronize NAS SQN, based on 24.501 4.4.3.1 */
  // Sequence number
  int nas_sqn = buffer.buf[6];
  int target_sqn = nas->security.nas_count_dl & 0xff;
  if (nas_sqn != target_sqn) {
    if (nas_sqn < target_sqn)
      nas->security.nas_count_dl += 256;
    nas->security.nas_count_dl &= ~255;
    nas->security.nas_count_dl |= nas_sqn;
  }
  if (nas->security.nas_count_dl > 0x00ffffff) {
    /* it's doubtful that this will happen, so let's simply exit for the time being */
    /* to be refined if needed */
    LOG_E(NAS, "max NAS COUNT DL reached\n");
    exit(1);
  }

  /* check integrity */
  /* [EPD][SHD][MAC0]...[MAC3][SEQNO][PAYL]*/
  uint8_t *received_mac = buffer.buf + 2;
  uint8_t computed_mac[NAS_INTEGRITY_SIZE];
  nas_security_compute_mac(nas,
                           buffer,
                           false,
                           true,
                           computed_mac);

  if (memcmp(received_mac, computed_mac, NAS_INTEGRITY_SIZE) != 0)
    return NAS_SECURITY_INTEGRITY_FAILED;

  /* decipher */
  nas_security_decrypt_payload(nas, buffer, true);

  /* update estimated DL Counter */
  nas->security.nas_count_dl++;

  return NAS_SECURITY_INTEGRITY_PASSED;
}

static void suci_profile_scheme_a_generate_output(const uint8_t home_network_public_key[32],
                                                  const char *msin,
                                                  const size_t msin_len,
                                                  char *schemeoutput)
{
  /* 1> Eph. key pair generation */
  uint8_t eph_priv[32] = {0};
  uint8_t eph_pub[32] = {0};
  x25519_generate_keypair(eph_priv, eph_pub);

  /* 2> Key agreement */
  uint8_t eph_shared_key[32] = {0};
  x25519_shared_secret(eph_priv, home_network_public_key, eph_shared_key);

  explicit_bzero(eph_priv, 32);

  /* 3> Key derivation */
  uint8_t kdf_output[64] = {0};
  byte_array_t kdf_secret = {.buf = eph_shared_key, .len = 32};
  byte_array_t kdf_info = {.buf = eph_pub, .len = 32};
  x963_kdf(kdf_secret, kdf_info, 64, kdf_output);

  explicit_bzero(eph_shared_key, 32);

  aes_128_t aes_ctx;
  aes_ctx.type = AES_INITIALIZATION_VECTOR_16;
  memcpy(aes_ctx.key, kdf_output, 16);
  memcpy(aes_ctx.iv16.iv, kdf_output + 16, 16);

  uint8_t eph_mac_key[32] = {0};
  memcpy(eph_mac_key, kdf_output + 32, 32);

  explicit_bzero(kdf_output, 64);

  /* 4> Symmetric encryption */
  size_t msin_bcd_len = (msin_len + 1) / 2;
  uint8_t msin_bcd[msin_bcd_len];
  memset(msin_bcd, 0, msin_bcd_len);

  int rc = digit_string_to_bcd_value(msin_bcd, msin, msin_bcd_len);
  AssertFatal(rc == 0, "Encoding MSIN failed (rc=%d, input=\"%s\", len=%zu, out_len=%zu)", rc, msin, msin_len, msin_bcd_len);

  byte_array_t payload = {.buf = msin_bcd, .len = msin_bcd_len};
  uint8_t ciphertext[msin_bcd_len];
  aes_128_ctr(&aes_ctx, payload, msin_bcd_len, ciphertext);

  explicit_bzero(aes_ctx.key, 16);
  explicit_bzero(aes_ctx.iv16.iv, 16);
  explicit_bzero(msin_bcd, msin_bcd_len);

  /* 5> MAC function */
  uint8_t mac_full[32] = {0};
  byte_array_t mac_input = {.buf = ciphertext, .len = msin_bcd_len};
  sha_256_hmac(eph_mac_key, mac_input, 32, mac_full);

  explicit_bzero(eph_mac_key, 32);

  /* Build SUCI scheme output --- */
  /* eph_pub (32 bytes -> 64 hex chars) */
  for (int i = 0; i < 32; i++) {
    *schemeoutput++ = hex[eph_pub[i] >> 4];
    *schemeoutput++ = hex[eph_pub[i] & 0x0F];
  }

  /* ciphertext (~45 bytes -> ~90 hex chars) */
  for (int i = 0; i < msin_bcd_len; i++) {
    *schemeoutput++ = hex[ciphertext[i] >> 4];
    *schemeoutput++ = hex[ciphertext[i] & 0x0F];
  }

  /* MAC (8 bytes -> 16 hex chars) */
  for (int i = 0; i < 8; i++) {
    *schemeoutput++ = hex[mac_full[i] >> 4];
    *schemeoutput++ = hex[mac_full[i] & 0x0F];
  }
}

static int fill_suci(FGSMobileIdentity *mi, const uicc_t *uicc)
{
  mi->suci.typeofidentity = FGS_MOBILE_IDENTITY_SUCI;
  mi->suci.mncdigit1 = uicc->nmc_size == 2 ? uicc->imsiStr[3] - '0' : uicc->imsiStr[4] - '0';
  mi->suci.mncdigit2 = uicc->nmc_size == 2 ? uicc->imsiStr[4] - '0' : uicc->imsiStr[5] - '0';
  mi->suci.mncdigit3 = uicc->nmc_size == 2 ? 0xF : uicc->imsiStr[3] - '0';
  mi->suci.mccdigit1 = uicc->imsiStr[0] - '0';
  mi->suci.mccdigit2 = uicc->imsiStr[1] - '0';
  mi->suci.mccdigit3 = uicc->imsiStr[2] - '0';

  mi->suci.routingindicatordigit1 = uicc->routing_indicatorStr[0] - '0';
  mi->suci.routingindicatordigit2 = uicc->routing_indicatorStr[1] - '0';
  mi->suci.routingindicatordigit3 = uicc->routing_indicatorStr[2] - '0';
  mi->suci.routingindicatordigit4 = uicc->routing_indicatorStr[3] - '0';

  char *msin = uicc->imsiStr + 3 + uicc->nmc_size;
  uint8_t msin_len = strlen(msin);

  mi->suci.protectionschemeId = uicc->protection_scheme;
  switch (uicc->protection_scheme) {
    case 0: /* Null scheme (TS 33.501 C.2) */
    {
      mi->suci.homenetworkpki = 0;
      memcpy(mi->suci.schemeoutput, msin, msin_len);
      break;
    }
    case 1: /* Profile A (TS 33.501 C.3.4.1) */
    {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      mi->suci.homenetworkpki = uicc->home_network_public_key_id;
      suci_profile_scheme_a_generate_output(uicc->home_network_public_key, msin, msin_len, mi->suci.schemeoutput);
#else
      AssertFatal(false,
                  "Protection Scheme not supported when using a version below OpenSSL 3.0 %d  (OpenSSL version: %s)\n",
                  uicc->protection_scheme,
                  MY_OPENSSL_VERSION_STR);
#endif
      break;
    }
    case 2: /* Profile B (TS 33.501 C.3.4.2) */
    {
      mi->suci.homenetworkpki = uicc->home_network_public_key_id;

      AssertFatal(false, "Unsupported Protection Scheme in UICC %d\n", uicc->protection_scheme);

      break;
    }
    default: // Unknown schemes
    {
      AssertFatal(false, "Unknown Protection Scheme in UICC %d\n", uicc->protection_scheme);
    }
  }

  LOG_D(NAS,
        "SUCI in registration request: SUPI type: %d Type of Identity: %u MCC: %u%u%u, MNC: %u%u%u, \
     Routing Indicator %d%d%d%d Protection Scheme ID: %u, Home Network PKI: %u, Scheme Output: %s\n",
        mi->suci.supiformat,
        mi->suci.typeofidentity,
        mi->suci.mccdigit1,
        mi->suci.mccdigit2,
        mi->suci.mccdigit3,
        mi->suci.mncdigit1,
        mi->suci.mncdigit2,
        uicc->nmc_size == 2 ? 0 : mi->suci.mncdigit3,
        mi->suci.routingindicatordigit1,
        mi->suci.routingindicatordigit2,
        mi->suci.routingindicatordigit3,
        mi->suci.routingindicatordigit4,
        mi->suci.protectionschemeId,
        mi->suci.homenetworkpki,
        mi->suci.schemeoutput);
  return sizeof(Suci5GSMobileIdentity_t);
}

static int fill_guti(FGSMobileIdentity *mi, const Guti5GSMobileIdentity_t *guti)
{
  AssertFatal(guti != NULL, "UE has no GUTI\n");
  mi->guti = *guti;
  LOG_D(NAS,
        "5G-GUTI in registration request: MCC: %u%u%u, MNC: %u%u, AMF Region ID: %u, AMF Set ID: %u, AMF Pointer: %u, 5G-TMSI: "
        "%u\n",
        mi->guti.mccdigit1,
        mi->guti.mccdigit2,
        mi->guti.mccdigit3,
        mi->guti.mncdigit1,
        mi->guti.mncdigit2,
        mi->guti.amfregionid,
        mi->guti.amfsetid,
        mi->guti.amfpointer,
        mi->guti.tmsi);
  return 13;
}

static int fill_fgstmsi(Stmsi5GSMobileIdentity_t *stmsi, const Guti5GSMobileIdentity_t *guti)
{
  AssertFatal(guti != NULL, "UE has no GUTI\n");
  stmsi->amfpointer = guti->amfpointer;
  stmsi->amfsetid = guti->amfsetid;
  stmsi->tmsi = guti->tmsi;
  stmsi->digit1 = DIGIT1;
  stmsi->spare = 0;
  stmsi->typeofidentity = FGS_MOBILE_IDENTITY_5GS_TMSI;
  /* 2 octets length field + 7 octets encoded 5G-S-TMSI contents */
  return 9;
}

static int fill_imeisv(FGSMobileIdentity *mi, const uicc_t *uicc)
{
  int i = 0;
  mi->imeisv.typeofidentity = FGS_MOBILE_IDENTITY_IMEISV;
  mi->imeisv.digittac01 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac02 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac03 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac04 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac05 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac06 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac07 = getImeisvDigit(uicc, i++);
  mi->imeisv.digittac08 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit09 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit10 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit11 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit12 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit13 = getImeisvDigit(uicc, i++);
  mi->imeisv.digit14 = getImeisvDigit(uicc, i++);
  mi->imeisv.digitsv1 = getImeisvDigit(uicc, i++);
  mi->imeisv.digitsv2 = getImeisvDigit(uicc, i++);
  mi->imeisv.spare = 0x0f;
  mi->imeisv.oddeven = 0;
  return 19;
}

/** @brief Fill requested 5GS Mobile Identity (TS 24.501, 9.11.3.4) according to identity type.
 *
 * @param mobile_identity Pointer to the mobile identity structure to fill
 * @param nas Pointer to the UE NAS context
 * @param identitytype Type of identity to fill
 * @return Encoded identity size in bytes, 0 on failure
 */
static int nas_fill_5gs_mobile_identity(FGSMobileIdentity *mobile_identity, const nr_ue_nas_t *nas, uint8_t identitytype)
{
  switch (identitytype) {
    case FGS_MOBILE_IDENTITY_SUCI:
      return fill_suci(mobile_identity, nas->uicc);
    case FGS_MOBILE_IDENTITY_5G_GUTI:
      if (!nas->guti) {
        LOG_W(NAS, "Cannot build Identity Response with 5G-GUTI: UE has no valid GUTI\n");
        return 0;
      }
      return fill_guti(mobile_identity, nas->guti);
    case FGS_MOBILE_IDENTITY_IMEISV:
      return fill_imeisv(mobile_identity, nas->uicc);
    case FGS_MOBILE_IDENTITY_5GS_TMSI:
      if (!nas->guti) {
        LOG_W(NAS, "Cannot build Identity Response with 5G-S-TMSI: UE has no valid GUTI\n");
        return 0;
      }
      return fill_fgstmsi(&mobile_identity->stmsi, nas->guti);
    default:
      LOG_W(NAS, "Cannot build Identity Response: requested identity type %d is unsupported\n", identitytype);
      return 0;
  }
}

void transferRES(uint8_t ck[16], uint8_t ik[16], uint8_t *input, uint8_t rand[16], uint8_t *output, plmn_id_t *plmn_id)
{
  uint8_t S[100] = {0};
  S[0] = 0x6B;
  servingNetworkName(S + 1, plmn_id);
  int netNamesize = strlen((char *)S + 1);
  S[1 + netNamesize] = (netNamesize & 0xff00) >> 8;
  S[2 + netNamesize] = (netNamesize & 0x00ff);
  for (int i = 0; i < 16; i++)
    S[3 + netNamesize + i] = rand[i];
  S[19 + netNamesize] = 0x00;
  S[20 + netNamesize] = 0x10;
  for (int i = 0; i < 8; i++)
    S[21 + netNamesize + i] = input[i];
  S[29 + netNamesize] = 0x00;
  S[30 + netNamesize] = 0x08;

  uint8_t plmn[3] = {0x02, 0xf8, 0x39};
  uint8_t oldS[100];
  oldS[0] = 0x6B;
  memcpy(&oldS[1], plmn, 3);
  oldS[4] = 0x00;
  oldS[5] = 0x03;
  for (int i = 0; i < 16; i++)
    oldS[6 + i] = rand[i];
  oldS[22] = 0x00;
  oldS[23] = 0x10;
  for (int i = 0; i < 8; i++)
    oldS[24 + i] = input[i];
  oldS[32] = 0x00;
  oldS[33] = 0x08;

  uint8_t key[32] = {0};
  memcpy(&key[0], ck, 16);
  memcpy(&key[16], ik, 16); // KEY
  uint8_t out[32] = {0};

  byte_array_t data = {.buf = S, .len = 31 + netNamesize};
  kdf(key, data, 32, out);

  memcpy(output, out + 16, 16);
}

void derive_kausf(uint8_t ck[16], uint8_t ik[16], uint8_t sqn[6], uint8_t kausf[32], plmn_id_t *plmn_id)
{
  uint8_t S[100] = {0};
  uint8_t key[32] = {0};

  memcpy(&key[0], ck, 16);
  memcpy(&key[16], ik, 16); // KEY
  S[0] = 0x6A;
  servingNetworkName(S + 1, plmn_id);
  int netNamesize = strlen((char *)S + 1);
  S[1 + netNamesize] = (uint8_t)((netNamesize & 0xff00) >> 8);
  S[2 + netNamesize] = (uint8_t)(netNamesize & 0x00ff);
  for (int i = 0; i < 6; i++) {
    S[3 + netNamesize + i] = sqn[i];
  }
  S[9 + netNamesize] = 0x00;
  S[10 + netNamesize] = 0x06;

  byte_array_t data = {.buf = S, .len = 11 + netNamesize};
  kdf(key, data, 32, kausf);
}

void derive_kseaf(uint8_t kausf[32], uint8_t kseaf[32], plmn_id_t *plmn_id)
{
  uint8_t S[100] = {0};
  S[0] = 0x6C; // FC
  servingNetworkName(S + 1, plmn_id);
  int netNamesize = strlen((char *)S + 1);
  S[1 + netNamesize] = (uint8_t)((netNamesize & 0xff00) >> 8);
  S[2 + netNamesize] = (uint8_t)(netNamesize & 0x00ff);

  byte_array_t data = {.buf = S, .len = 3 + netNamesize};
  kdf(kausf, data, 32, kseaf);
}

void derive_kamf(uint8_t *kseaf, uint8_t *kamf, uint16_t abba, uicc_t *uicc)
{
  int imsiLen = strlen(uicc->imsiStr);
  uint8_t S[100] = {0};
  S[0] = 0x6D; // FC = 0x6D
  memcpy(&S[1], uicc->imsiStr, imsiLen);
  S[1 + imsiLen] = (uint8_t)((imsiLen & 0xff00) >> 8);
  S[2 + imsiLen] = (uint8_t)(imsiLen & 0x00ff);
  S[3 + imsiLen] = abba & 0x00ff;
  S[4 + imsiLen] = (abba & 0xff00) >> 8;
  S[5 + imsiLen] = 0x00;
  S[6 + imsiLen] = 0x02;

  byte_array_t data = {.buf = S, .len = 7 + imsiLen};
  kdf(kseaf, data, 32, kamf);
}

//------------------------------------------------------------------------------
void derive_knas(algorithm_type_dist_t nas_alg_type, uint8_t nas_alg_id, uint8_t kamf[32], uint8_t *knas)
{
  uint8_t S[20] = {0};
  uint8_t out[32] = {0};
  S[0] = 0x69; // FC
  S[1] = (uint8_t)(nas_alg_type & 0xFF);
  S[2] = 0x00;
  S[3] = 0x01;
  S[4] = nas_alg_id;
  S[5] = 0x00;
  S[6] = 0x01;

  byte_array_t data = {.buf = S, .len = 7};
  kdf(kamf, data, 32, out);

  memcpy(knas, out + 16, 16);
}

static void derive_ue_keys(uint8_t *buf, nr_ue_nas_t *nas)
{
  uint8_t ak[6];
  uint8_t sqn[6];

  DevAssert(nas != NULL);
  uint8_t *kausf = nas->security.kausf;
  uint8_t *kseaf = nas->security.kseaf;
  uint8_t *kamf = nas->security.kamf;
  uint8_t *output = nas->security.res;
  uint8_t *rand = nas->security.rand;
  uint8_t *kgnb = nas->security.kgnb;

  // get RAND for authentication request
  for (int index = 0; index < 16; index++) {
    rand[index] = buf[8 + index];
  }

  uint8_t resTemp[16];
  uint8_t ck[16], ik[16];
  f2345(nas->uicc->key, rand, resTemp, ck, ik, ak, nas->uicc->opc);

  transferRES(ck, ik, resTemp, rand, output, nas->sn_id);

  for (int index = 0; index < 6; index++) {
    sqn[index] = buf[26 + index];
  }

  derive_kausf(ck, ik, sqn, kausf, nas->sn_id);
  derive_kseaf(kausf, kseaf, nas->sn_id);
  derive_kamf(kseaf, kamf, 0x0000, nas->uicc);
  derive_kgnb(kamf, nas->security.nas_count_ul, kgnb);

  printf("kausf:");
  for (int i = 0; i < 32; i++) {
    printf("%x ", kausf[i]);
  }
  printf("\n");

  printf("kseaf:");
  for (int i = 0; i < 32; i++) {
    printf("%x ", kseaf[i]);
  }

  printf("\n");

  printf("kamf:");
  for (int i = 0; i < 32; i++) {
    printf("%x ", kamf[i]);
  }
  printf("\n");
}

nr_ue_nas_t *get_ue_nas_info(module_id_t module_id)
{
  AssertFatal(module_id < MAX_NUM_NR_UE_INST, "Invalid module_id %d\n", module_id);
  if (!nr_ue_nas[module_id].uicc) {
    nr_ue_nas[module_id].uicc = checkUicc(module_id);
    nr_ue_nas[module_id].UE_id = module_id;
    for (int i = 0; i < MAX_NUM_PSI; i++) {
      nr_ue_nas[module_id].pdu_tun[i].sock = -1;
      nr_ue_nas[module_id].pdu_tun[i].qfi = -1;
    }
  }
  return &nr_ue_nas[module_id];
}

/** @brief Select KSI for outgoing initial NAS message
 * use stored KSI when integrity context exists, else NOT_AVAILABLE. */
static FGSRegistrationType set_fgs_ksi(nr_ue_nas_t *nas)
{
  if (nas->security_container && nas->security_container->integrity_context && nas->ksi)
    return *nas->ksi & 0x07;

  return NAS_KEY_SET_IDENTIFIER_NOT_AVAILABLE;
}

/**
 * @brief Set contents of 5GMM capability
 * @note  Currently hardcoded, sending min length only (1 octet)
 */
static FGMMCapability set_fgmm_capability()
{
  FGMMCapability cap = {0};
  cap.iei = REGISTRATION_REQUEST_5GMM_CAPABILITY_IEI;
  cap.length = 1;

  cap.sgc = 0;
  cap.iphc_cp_cIoT = 0;
  cap.n3_data = 0;
  cap.cp_cIoT = 0;
  cap.restrict_ec = 0;
  cap.lpp = 1;
  cap.ho_attach = 1;
  cap.s1_mode = 0;

  if (cap.length == 1)
    return cap; // Send minimum length only, 1 octet

  cap.racs = 0;
  cap.nssaa = 0;
  cap.lcs = 0;
  cap.v2x_cnpc5 = 0;
  cap.v2x_cepc5 = 0;
  cap.v2x = 0;
  cap.up_cIoT = 0;
  cap.srvcc = 0;
  cap.ehc_CP_ciot = 0;
  cap.multiple_eUP = 0;
  cap.wusa = 0;
  cap.cag = 0;

  return cap;
}

static FGSRegistrationType set_fgs_registration_type(nr_ue_nas_t *nas)
{
  if (nas->fiveGMM_state == FGS_REGISTERED && nas->fiveGMM_mode == FGS_IDLE && nas->t3512) {
    // TODO: if the timer expires, do PERIODIC_REGISTRATION_UPDATING
    /** The UE shall initiate the registration procedure for
     *  mobility and periodic registration update according to
     *  5.5.1.3.2 of 3GPP TS 24.501: Mobility and periodic
     *  registration update initiation */
    LOG_E(NAS, "Registration type periodic registration updating is not handled\n");
    return REG_TYPE_RESERVED;
  } else if (nas->fiveGMM_state == FGS_REGISTERED) {
    // in any other case, The UE in state 5GMM-REGISTERED shall indicate "mobility registration updating".
    return MOBILITY_REGISTRATION_UPDATING;
  }

  if (nas->fiveGMM_mode == FGS_CONNECTED && nas->is_rrc_inactive) {
    /** the UE shall do the registration procedure for mobility
     *  and/or periodic registration update depending on the
     *  indication received from the lower layers according to
     *  5.3.1.4 of 3GPP TS 24.501: 5GMM-CONNECTED mode with RRC inactive indication */
    LOG_E(NAS, "RRC inactive indication not handled by NAS\n");
    return REG_TYPE_RESERVED;
  }

  return INITIAL_REGISTRATION;
}

/**
 * @brief Generate 5GS Registration Request (8.2.6 of 3GPP TS 24.501)
 */
void generateRegistrationRequest(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas, bool is_security_mode)
{
  LOG_I(NAS, "Generate Initial NAS Message: Registration Request\n");
  int size = sizeof(fgmm_msg_header_t); // cleartext size
  fgmm_nas_msg_security_protected_t sp = {0};

  /** Check whether the UE has a valid current 5G NAS security context
      and set security protected 5GS NAS message header (see 9.1.1 of 3GPP TS 24.501) */
  bool has_security_context = nas->security_container && nas->security_container->integrity_context;
  if (has_security_context) {
    sp.header.protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
    sp.header.security_header_type = INTEGRITY_PROTECTED;
    sp.header.sequence_number = nas->security.nas_count_ul & 0xff;
    size += 7;
  }

  // Plain 5GMM message
  sp.plain.header = set_mm_header(FGS_REGISTRATION_REQUEST, PLAIN_5GS_MSG);
  size += sizeof(sp.plain.header);
  registration_request_msg *rr = &sp.plain.mm_msg.registration_request;

  // 5GMM Registration Type
  rr->fgsregistrationtype = set_fgs_registration_type(nas);
  size += 1;
  if (rr->fgsregistrationtype == REG_TYPE_RESERVED) {
    // currently only REG_TYPE_RESERVED is supported
    LOG_E(NAS, "Initial NAS Message: Registration Request failed\n");
    return;
  }
  // NAS Key Set Identifier
  rr->naskeysetidentifier.tsc = NAS_KEY_SET_IDENTIFIER_NATIVE;
  rr->naskeysetidentifier.naskeysetidentifier = set_fgs_ksi(nas);
  size += 1;

  // 5GMM Mobile Identity
  if(nas->guti){
    size += fill_guti(&rr->fgsmobileidentity, nas->guti);
  } else {
    size += fill_suci(&rr->fgsmobileidentity, nas->uicc);
  }

  // Security Capability
  rr->presencemask |= REGISTRATION_REQUEST_UE_SECURITY_CAPABILITY_PRESENT;
  rr->nruesecuritycapability.iei = REGISTRATION_REQUEST_UE_SECURITY_CAPABILITY_IEI;
  rr->nruesecuritycapability.length = 8;
  rr->nruesecuritycapability.fg_EA = 0xe0;
  rr->nruesecuritycapability.fg_IA = 0x60;
  rr->nruesecuritycapability.EEA = 0;
  rr->nruesecuritycapability.EIA = 0;
  size += 10;

  /* Create a copy of the cleartext 5GMM message, add non-cleartext IEs if necessary */
  fgmm_nas_message_plain_t full_mm = sp.plain;
  registration_request_msg *full_rr = &full_mm.mm_msg.registration_request;
  int size_nct = size; // non-cleartext size
  bool cleartext_only = true;
  /* 5GMM Capability (non-cleartext IE) - 24.501 8.2.6.3
    The UE shall include this IE, unless the UE performs a periodic registration updating procedure. */
  if (full_rr->fgsregistrationtype != PERIODIC_REGISTRATION_UPDATING) {
    cleartext_only = false; // The UE needs to send non-cleartext IE
    full_rr->presencemask |= REGISTRATION_REQUEST_5GMM_CAPABILITY_PRESENT;
    full_rr->fgmmcapability = set_fgmm_capability();
    FGMMCapability *cap = &full_rr->fgmmcapability;
    size_nct += sizeof(cap->length) + sizeof(cap->iei) + cap->length;
  }

  if (is_security_mode) {
    /* Encode both cleartext IEs and non-cleartext IEs Registration Request message in Security Mode Complete.
       The UE includes the full Registration Request in the NAS container IE
       and sends it within the Security Mode Complete message. (24.501 4.4.6, 23.502 4.2.2.2.2) */
    LOG_D(NAS, "Full Initial NAS Message: Registration Request in the NAS container of Security Mode Complete\n");
    initialNasMsg->nas_data = malloc_or_fail(size_nct * sizeof(*initialNasMsg->nas_data));
    initialNasMsg->length = mm_msg_encode(&full_mm, initialNasMsg->nas_data, size_nct);
  } else if (!has_security_context) {
    /* If no valid 5G NAS security context exists, the UE sends a plain Registration Request including cleartext IEs only. */
    LOG_D(NAS, "Plain Initial NAS Message: Registration Request\n");
    initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));
    initialNasMsg->length = mm_msg_encode(&sp.plain, initialNasMsg->nas_data, size);
  } else {
    /* If the UE has a valid current 5G NAS security context, then it includes the entire 5GMM NAS Registration Request
       (with both cleartext and non-cleartext IEs) in the NAS message container IE. The value of the NAS message container IE is
       then ciphered. The UE sends a 5GMM NAS Registration Request message containing cleartext IEs along with the NAS message
       container IE. */
    LOG_D(NAS, "Initial NAS Message: Registration Request with ciphered NAS container\n");

    // NAS message container
    if (!cleartext_only) {
      OctetString *nasmessagecontainercontents = &rr->fgsnasmessagecontainer.nasmessagecontainercontents;
      nasmessagecontainercontents->value = calloc_or_fail(size_nct, sizeof(*nasmessagecontainercontents->value));
      nasmessagecontainercontents->length = mm_msg_encode(&full_mm, nasmessagecontainercontents->value, size_nct);
      size += (nasmessagecontainercontents->length + 2);
      rr->presencemask |= REGISTRATION_REQUEST_NAS_MESSAGE_CONTAINER_PRESENT;
      // Workaround to pass integrity in RRC_IDLE
      uint8_t *kamf = nas->security.kamf;
      uint8_t *kgnb = nas->security.kgnb;
      derive_kgnb(kamf, nas->security.nas_count_ul, kgnb);
      nas_itti_kgnb_refresh_req(nas->UE_id, nas->security.kgnb);
    }
    // Allocate buffer (including NAS message container size)
    initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));

    // Security protected header encoding
    int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, &sp.header, size);
    initialNasMsg->length =
        security_header_len
        + mm_msg_encode(&sp.plain, initialNasMsg->nas_data + security_header_len, size - security_header_len);
    /* integrity protection */
    nas_stream_cipher_t stream_cipher;
    AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
    uint8_t mac[4];
    stream_cipher.context = nas->security_container->integrity_context;
    stream_cipher.count = nas->security.nas_count_ul++;
    stream_cipher.bearer = 1;
    stream_cipher.direction = 0;
    // Security protected header is cleartext except the SN field
    uint8_t cleartext_len = sizeof(sp.header) - 1;
    // Message to be integrity protected
    stream_cipher.message = initialNasMsg->nas_data + cleartext_len;
    // Length of integrity protected message in bits
    stream_cipher.blength = (initialNasMsg->length - cleartext_len) << 3;
    stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);
    uint8_t mac_len = sizeof(sp.header.message_authentication_code);
    uint8_t mac_start_octet = 2;
    LOG_D(NAS, "Integrity protected initial NAS message: mac = %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
    for (int i = 0; i < mac_len; i++) {
      initialNasMsg->nas_data[mac_start_octet + i] = mac[i];
    }
  }
}

void generateServiceRequest(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas, bool mo_ul_data)
{
  LOG_I(NAS, "Generate initial NAS message: Service Request\n");
  int size = 0;

  // NAS is security protected if has valid security contexts
  bool security_protected = nas->security_container && nas->security_container->integrity_context;

  // Set 5GMM plain header
  fgmm_nas_message_plain_t plain = {0};
  plain.header = set_mm_header(FGS_SERVICE_REQUEST, PLAIN_5GS_MSG);
  size += sizeof(plain.header);
  // Set plain FGMM Service Request
  fgs_service_request_msg_t *mm_msg = &plain.mm_msg.service_request;
  // Service Type
  mm_msg->serviceType = SERVICE_TYPE_DATA;
  // NAS key set identifier
  mm_msg->naskeysetidentifier.tsc = NAS_KEY_SET_IDENTIFIER_NATIVE;
  mm_msg->naskeysetidentifier.naskeysetidentifier = set_fgs_ksi(nas);
  size += 1;
  // 5G-S-TMSI
  size += fill_fgstmsi(&mm_msg->fiveg_s_tmsi, nas->guti);

  /* Optional non-cleartext IEs (TS 24.501 4.4.6, TS 33.501 6.4.6):
   * - PDU session status (8.2.16.3 / 9.11.3.44) from preserved 5GSM state
   * - Uplink data status (8.2.16.2 / 9.11.3.57) if MO UL data pending */
  uint8_t pdu_session_status[MAX_NUM_PSI] = {0};
  uint8_t uplink_data_status[MAX_NUM_PSI] = {0};
  bool has_pdu_session_status = false;
  bool has_uplink_data_status = false;
  for (int pdu_id = 1; pdu_id < MAX_NUM_PSI; pdu_id++) {
    if (nas->psi_status[pdu_id] == PDU_SESSION_INACTIVE)
      continue;
    pdu_session_status[pdu_id] = PDU_SESSION_ACTIVE;
    has_pdu_session_status = true;
    if (mo_ul_data) {
      uplink_data_status[pdu_id] = PDU_SESSION_ACTIVE;
      has_uplink_data_status = true;
    }
  }
  const bool has_non_cleartext_ies = has_pdu_session_status || has_uplink_data_status;

  /* message encoding */
  if (security_protected) {
    fgmm_nas_msg_security_protected_t sp = {0};
    FGCNasMessageContainer nas_container = {0};

    // Set security protected 5GS NAS message header (see 9.1.1 of 3GPP TS 24.501)
    sp.header.protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
    sp.header.security_header_type = INTEGRITY_PROTECTED;
    sp.header.sequence_number = nas->security.nas_count_ul & 0xff;
    const int plain_sr_size = size;
    size += sizeof(sp.header);

    // TS 24.501 4.4.6.b.1: when non-cleartext IEs are present, place the full Service Request
    // in the NAS message container and cipher only that container value before outer integrity.
    if (has_non_cleartext_ies) {
      fgmm_nas_message_plain_t full_sr = plain;
      fgs_service_request_msg_t *full_mm_msg = &full_sr.mm_msg.service_request;
      if (has_uplink_data_status) {
        full_mm_msg->has_uplink_data_status = true;
        memcpy(full_mm_msg->uplink_data_status, uplink_data_status, sizeof(full_mm_msg->uplink_data_status));
      }
      if (has_pdu_session_status) {
        full_mm_msg->has_pdu_session_status = true;
        memcpy(full_mm_msg->pdu_session_status, pdu_session_status, sizeof(full_mm_msg->pdu_session_status));
      }

      int inner_buf_size = plain_sr_size;
      if (has_uplink_data_status)
        inner_buf_size += MIN_PDU_SESSION_CONTENTS_LEN + 2;
      if (has_pdu_session_status)
        inner_buf_size += MIN_PDU_SESSION_CONTENTS_LEN + 2;
      uint8_t *inner_sr = calloc_or_fail(inner_buf_size, sizeof(*inner_sr));
      const int inner_sr_len = mm_msg_encode(&full_sr, inner_sr, inner_buf_size);
      if (inner_sr_len <= 0) {
        free(inner_sr);
        AssertFatal(false, "Failed to encode Service Request NAS container payload\n");
      }

      nas_container.nasmessagecontainercontents.value = inner_sr;
      nas_container.nasmessagecontainercontents.length = inner_sr_len;

      uint8_t ciphered_container[inner_sr_len];
      nas_stream_cipher_t container_cipher = {0};
      AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
      container_cipher.context = nas->security_container->ciphering_context;
      container_cipher.count = nas->security.nas_count_ul;
      container_cipher.bearer = 1;
      container_cipher.message = inner_sr;
      container_cipher.blength = inner_sr_len << 3;
      stream_compute_encrypt(nas->security_container->ciphering_algorithm, &container_cipher, ciphered_container);
      memcpy(inner_sr, ciphered_container, inner_sr_len);

      mm_msg->fgsnasmessagecontainer = &nas_container;
      size += inner_sr_len + 3;
    }

    initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));

    // Payload: plain message
    sp.plain = plain;

    // security protected encoding
    int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, &sp.header, size);
    initialNasMsg->length =
        security_header_len
        + mm_msg_encode(&sp.plain, (uint8_t *)(initialNasMsg->nas_data + security_header_len), size - security_header_len);
    /* integrity protection */
    uint8_t mac[4];
    nas_stream_cipher_t stream_cipher = {0};
    stream_cipher.context = nas->security_container->integrity_context;
    const uint32_t sr_ul_count = nas->security.nas_count_ul;
    stream_cipher.count = sr_ul_count;
    nas->security.nas_count_ul++;
    stream_cipher.bearer = 1;
    stream_cipher.direction = 0;
    stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
    /* length in bits */
    stream_cipher.blength = (initialNasMsg->length - 6) << 3;
    stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);
    LOG_D(NAS, "Integrity protected initial NAS message: mac = %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
    for (int i = 0; i < 4; i++)
      initialNasMsg->nas_data[2 + i] = mac[i];

    /* Keep AS security in sync with updated NAS UL count for post-paging reconnect */
    derive_kgnb(nas->security.kamf, sr_ul_count, nas->security.kgnb);
    nas_itti_kgnb_refresh_req(nas->UE_id, nas->security.kgnb);
  } else {
    initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));
    // plain encoding
    initialNasMsg->length = mm_msg_encode(&plain, initialNasMsg->nas_data, size);
    LOG_I(NAS, "PLAIN_5GS_MSG initial NAS message: Service Request with length %d \n", initialNasMsg->length);
  }
}

/** @brief Build Identity Response according to requested identity type (8.2.22 of 3GPP TS 24.501) */
static void generateIdentityResponse(const nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, uint8_t identitytype)
{
  int size = sizeof(fgmm_msg_header_t);
  fgmm_nas_message_plain_t plain = {0};

  // Plain 5GMM header
  plain.header = set_mm_header(FGS_IDENTITY_RESPONSE, PLAIN_5GS_MSG);
  size += sizeof(plain.header);

  /** Build Identity Response according to requested identity type: if identity
   * is unavailable in UE context (e.g., missing 5G-GUTI), return */
  fgmm_identity_response_msg *mm_msg = &plain.mm_msg.fgs_identity_response;
  int identity_size = nas_fill_5gs_mobile_identity(&mm_msg->fgsmobileidentity, nas, identitytype);
  if (identity_size <= 0)
    return;
  size += identity_size;

  // encode the message
  initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));

  initialNasMsg->length = mm_msg_encode(&plain, initialNasMsg->nas_data, size);
}

static void handle_identity_request(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas, const byte_array_t buffer)
{
  fgmm_msg_header_t mm_header = {0};
  fgs_identity_request_msg_t msg = {0};

  // Decode plain NAS header
  int decoded = decode_5gmm_msg_header(&mm_header, buffer.buf, buffer.len);
  if (decoded < 0) {
    LOG_E(NAS, "Failed to decode NAS Identity Request header\n");
    return;
  }

  byte_array_t payload = {.buf = &buffer.buf[decoded], .len = buffer.len - decoded};

  // Decode identity request payload
  if (decode_fgs_identity_request(&msg, &payload) < 0) {
    LOG_E(NAS, "Failed to decode Identity Request message\n");
    return;
  }

  LOG_I(NAS,
        "Received IDENTITY REQUEST for identity type: %s\n",
        print_info(msg.fgsmobileidentity, fgs_identity_type_text, sizeofArray(fgs_identity_type_text)));

  if (mm_header.security_header_type == PLAIN_5GS_MSG && msg.fgsmobileidentity != FGS_MOBILE_IDENTITY_SUCI) {
    // TS 24.501, 4.4.4.2: unprotected Identity Request is processable only when the requested identity is SUCI.
    LOG_E(NAS, "Drop unprotected Identity Request: SUCI required (TS 24.501 4.4.4.2), got %d\n", msg.fgsmobileidentity);
    return;
  }

  generateIdentityResponse(nas, initialNasMsg, msg.fgsmobileidentity);
  if (initialNasMsg->length <= 0)
    LOG_W(NAS, "Dropped Identity Response: unable to build Identity Response for identity type %d\n", msg.fgsmobileidentity);
}

static void generateAuthenticationResp(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, uint8_t *buf)
{
  derive_ue_keys(buf, nas);
  OctetString res;
  res.length = 16;
  res.value = calloc(1, 16);
  memcpy(res.value, nas->security.res, 16);

  int size = sizeof(fgmm_msg_header_t);
  fgmm_nas_message_plain_t plain = {0};

  // Plain 5GMM header
  plain.header = set_mm_header(FGS_AUTHENTICATION_RESPONSE, PLAIN_5GS_MSG);
  size += sizeof(plain.header);

  // set response parameter
  fgs_authentication_response_msg *mm_msg = &plain.mm_msg.fgs_auth_response;
  mm_msg->authenticationresponseparameter.res = res;
  size += 18;
  // encode the message
  initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));

  initialNasMsg->length = mm_msg_encode(&plain, initialNasMsg->nas_data, size);
  // Free res value after encode
  free(res.value);
}

/** @brief Send Authentication Failure message from the UE to the AMF to
 * indicate that authentication of the network has failed */
static void generateAuthenticationFailure(as_nas_info_t *initialNasMsg, cause_id_t cause)
{
  int size = sizeof(fgmm_msg_header_t);
  fgmm_nas_message_plain_t plain = {0};

  // Plain 5GMM header
  plain.header = set_mm_header(FGS_AUTHENTICATION_FAILURE, PLAIN_5GS_MSG);
  size += sizeof(plain.header);
  // 5GMM Cause (Mandatory)
  fgmm_auth_failure_t *mm_msg = &plain.mm_msg.fgmm_auth_failure;
  mm_msg->cause = cause;
  size += 1;
  initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));
  initialNasMsg->length = mm_msg_encode(&plain, initialNasMsg->nas_data, size);
}

static void handle_fgmm_authentication_request(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, byte_array_t *buffer)
{
  LOG_D(NAS, "Received NAS Authentication Request\n");
  fgs_authentication_request_t msg = {0};
  int size = 0;
  int decoded = 0;

  // decode plain 5GMM message header
  fgmm_msg_header_t mm_header = {0};
  if ((decoded = decode_5gmm_msg_header(&mm_header, buffer->buf + size, buffer->len - size)) < 0) {
    LOG_E(NAS, "decode_5gmm_msg_header failure in NAS Authentication Request handling\n");
    return;
  }
  size += decoded;
  // NAS key set identifier (Mandatory)
  if ((decoded = decode_nas_key_set_identifier(&msg.ngKSI, 0, buffer->buf[decoded])) < 0) {
    LOG_E(NAS, "decode_nas_key_set_identifier failure in NAS Authentication Request handling\n");
    return;
  }
  size += decoded;
  if (!nas->ksi) {
    nas->ksi = malloc_or_fail(sizeof(*nas->ksi));
    *nas->ksi = msg.ngKSI.naskeysetidentifier;
    LOG_D(NAS, "Stored NAS Key Set Identifier %d\n", *nas->ksi);
  } else if (*nas->ksi == msg.ngKSI.naskeysetidentifier) {
    /* If the ngKSI value received is already associated with one
    of the 5G security contexts stored in the UE, send failure message */
    LOG_E(NAS, "Invalid NAS Key Set Identifier: send Authentication Failure\n");
    generateAuthenticationFailure(initialNasMsg, ngKSI_already_in_use);
    return;
  }
  generateAuthenticationResp(nas, initialNasMsg, buffer->buf);
}

/** @brief Handle authentication not accepted by the network
 * This function assumes the message is not integrity protected, processes the received
 * NAS message and logs whether a EAP-failure is enclosed. The UE enters state: 5GMM-DEREGISTERED.
 * @todo The UE shall performs actions as per 5.4.1.3.5 of 3GPP TS 24.501, including
 * (1) Abort any ongoing 5GMM procedure (2) Stop all active timers: T3510, T3516, T3517,
 * T3519, T3520, T3521 (3) Delete stored SUCI. (4) handle EAP-failure message. */
static void handle_authentication_reject(nr_ue_nas_t *nas, uint8_t *pdu, int pdu_length)
{
  LOG_E(NAS, "Received Authentication Reject message from the network\n");
  uint8_t eap_msg[MAX_EAP_CONTENTS_LEN] = {0};
  fgmm_auth_reject_msg_t msg = {.eap_msg.buf = eap_msg};

  if (!pdu || pdu_length < sizeof(fgmm_msg_header_t)) {
    LOG_E(NAS, "Authentication Reject header is missing or too short\n");
    return;
  }

  byte_array_t ba = {.buf = pdu + sizeof(fgmm_msg_header_t), .len = pdu_length - sizeof(fgmm_msg_header_t)};
  if (decode_fgmm_auth_reject(&msg, &ba) < 0) {
    LOG_E(NAS, "Could not decode Authentication Reject\n");
    return;
  }

  if (msg.eap_msg.len > 0) {
    /** @todo UE handling EAP-failure message (5.4.1.2.2.11 of 3GPP TS 24.501) */
    LOG_W(NAS, "NAS Authentication Reject contains an EAP message: handling is not implemented\n");
    log_hex_buffer("EAP-Failure", msg.eap_msg.buf, msg.eap_msg.len);
  }

  nas->fiveGMM_state = FGS_DEREGISTERED;
}

int nas_itti_kgnb_refresh_req(instance_t instance, const uint8_t kgnb[32])
{
  MessageDef *message_p;
  message_p = itti_alloc_new_message(TASK_NAS_NRUE, instance, NAS_KENB_REFRESH_REQ);
  memcpy(NAS_KENB_REFRESH_REQ(message_p).kenb, kgnb, sizeof(NAS_KENB_REFRESH_REQ(message_p).kenb));
  return itti_send_msg_to_task(TASK_RRC_NRUE, instance, message_p);
}

static void generateSecurityModeComplete(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg)
{
  int size = sizeof(fgmm_msg_header_t);
  fgmm_nas_msg_security_protected_t nas_msg = {0};
  nas_stream_cipher_t stream_cipher;
  uint8_t mac[NAS_INTEGRITY_SIZE];
  // set security protected header
  fgs_nas_message_security_header_t *sp = &nas_msg.header;
  sp->protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  sp->security_header_type = INTEGRITY_PROTECTED_AND_CIPHERED_WITH_NEW_SECU_CTX;
  sp->sequence_number = nas->security.nas_count_ul & 0xff;
  size += 7;

  // Plain 5GMM Security Mode Complete msg
  fgmm_nas_message_plain_t *plain = &nas_msg.plain;

  // Plain 5GMM header
  plain->header = set_mm_header(FGS_SECURITY_MODE_COMPLETE, PLAIN_5GS_MSG);
  size += sizeof(plain->header);

  // Plain 5GMM payload
  fgs_security_mode_complete_msg *mm_msg = &plain->mm_msg.fgs_security_mode_complete;
  size += fill_imeisv(&mm_msg->fgsmobileidentity, nas->uicc);

  /* After activating a 5G NAS security context resulting from a security mode control send the full
     NAS Registration Request in the message container IE of the SECURITY MODE COMPLETE message (24.501 4.4.6) */
  as_nas_info_t rr;
  generateRegistrationRequest(&rr, nas, true);
  FGCNasMessageContainer *container = &mm_msg->fgsnasmessagecontainer;
  container->nasmessagecontainercontents.value = rr.nas_data;
  container->nasmessagecontainercontents.length = rr.length;
  size += (rr.length + 2);

  // encode the message
  initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));

  int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, sp, size);

  initialNasMsg->length =
      security_header_len
      + mm_msg_encode(plain, (uint8_t *)(initialNasMsg->nas_data + security_header_len), size - security_header_len);

  if (rr.nas_data) {
    free(rr.nas_data);
  }

  /* ciphering */
  uint8_t buf[initialNasMsg->length - 7];
  stream_cipher.context = nas->security_container->ciphering_context;
  AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
  stream_cipher.count = nas->security.nas_count_ul;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 7);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 7) << 3;
  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
  memcpy(stream_cipher.message, buf, initialNasMsg->length - 7);

  /* integrity protection */
  stream_cipher.context = nas->security_container->integrity_context;
  stream_cipher.count = nas->security.nas_count_ul++;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 6) << 3;

  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);

  printf("mac %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
  for (int i = 0; i < 4; i++) {
    initialNasMsg->nas_data[2 + i] = mac[i];
  }
}

/** @brief Generates the Security Mode Reject message to be sent by the UE to the AMF
 * to indicate that the corresponding security mode command has been rejected  */
static void generateSecurityModeReject(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, cause_id_t cause)
{
  LOG_I(NAS, "Send Security Mode Reject\n");
  fgmm_msg_header_t plain_header = set_mm_header(FGS_SECURITY_MODE_REJECT, PLAIN_5GS_MSG);
  int size = sizeof(plain_header);
  fgs_security_mode_reject_msg msg = {.cause = cause};
  size += 1;

  /** The UE shall apply the 5G NAS security context in use
   * before the initiation of the security mode control procedure,
   * if any, to protect the SECURITY MODE REJECT message */
  bool has_security_context = nas->security_container && nas->security_container->integrity_context;
  if (has_security_context) {
    fgmm_nas_msg_security_protected_t sp = {0};
    sp.header.protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
    sp.header.security_header_type = INTEGRITY_PROTECTED;
    sp.header.sequence_number = nas->security.nas_count_ul & 0xff;
    size += sizeof(sp.header);
    sp.plain.header = plain_header;
    sp.plain.mm_msg.fgs_security_mode_reject = msg;
    // Security protected header encoding
    int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, &sp.header, size);
    initialNasMsg->length =
        security_header_len + mm_msg_encode(&sp.plain, initialNasMsg->nas_data + security_header_len, size - security_header_len);
    /* ciphering */
    uint8_t buf[initialNasMsg->length - 7];
    nas_stream_cipher_t stream_cipher;
    stream_cipher.context = nas->security_container->ciphering_context;
    AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
    stream_cipher.count = nas->security.nas_count_ul;
    stream_cipher.bearer = 1;
    stream_cipher.direction = 0;
    stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 7);
    /* length in bits */
    stream_cipher.blength = (initialNasMsg->length - 7) << 3;
    stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
    memcpy(stream_cipher.message, buf, initialNasMsg->length - 7);
    /* integrity protection */
    uint8_t mac[4];
    stream_cipher.context = nas->security_container->integrity_context;
    stream_cipher.count = nas->security.nas_count_ul++;
    stream_cipher.bearer = 1;
    stream_cipher.direction = 0;
    stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
    /* length in bits */
    stream_cipher.blength = (initialNasMsg->length - 6) << 3;
    stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);
    LOG_D(NAS, "Integrity protected initial NAS message: mac = %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
    for (int i = 0; i < 4; i++)
      initialNasMsg->nas_data[2 + i] = mac[i];
  } else {
    fgmm_nas_message_plain_t plain = {0};
    plain.header = plain_header;
    plain.mm_msg.fgs_security_mode_reject = msg;
    // encode the message
    initialNasMsg->nas_data = malloc_or_fail(size);
    initialNasMsg->length = mm_msg_encode(&plain, initialNasMsg->nas_data, size);
  }
}

static void handle_security_mode_command(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, uint8_t *pdu, int pdu_length)
{
  /* Handle security mode command: must be authenticated, especially if no security
    context has been previously established. */
  uint8_t recv_mac[4];
  Security_header_t sec_hdr;

  /* Must have valid security header*/
  if(!nas_security_get_sec_hdr(pdu, pdu_length, &sec_hdr)) {
    LOG_E(NAS, "Received Security Mode Command without integrity protection.\n");
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
    return;
  }

  /* Must be integrity protected with new context (=3), see 3GPP TS 24.501 5.4.2.2 */
  if(sec_hdr != INTEGRITY_PROTECTED_WITH_NEW_SECU_CTX) {
    LOG_E(NAS, "Received Security Mode Command with invalid security header type %d.\n", sec_hdr);
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
    return;
  }

  /* Must have a MAC - checked after deriving keys */
  if(!nas_security_get_mac(pdu, pdu_length, recv_mac)) {
    LOG_E(NAS, "Received Security Mode Command with invalid MAC.\n");
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
    return;
  }

  /* retrieve integrity and ciphering algorithms  */
  if (pdu_length < 10) {
    LOG_E(NAS, "Invalid pdu_length=%d : send Security Mode Reject\n", pdu_length);
    // 3GPP TS 24.501 5.4.2.5 NAS security mode command not accepted by the UE
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
  }

  int ciphering_algorithm = (pdu[10] >> 4) & 0x0f;
  int integrity_algorithm = pdu[10] & 0x0f;

  uint8_t *kamf = nas->security.kamf;
  uint8_t *knas_enc = nas->security.knas_enc;
  uint8_t *knas_int = nas->security.knas_int;

  /* derive keys */
  derive_knas(0x01, ciphering_algorithm, kamf, knas_enc);
  derive_knas(0x02, integrity_algorithm, kamf, knas_int);

  printf("knas_int: ");
  for (int i = 0; i < 16; i++) {
    printf("%x ", knas_int[i]);
  }
  printf("\n");

  printf("knas_enc: ");
  for (int i = 0; i < 16; i++) {
    printf("%x ", knas_enc[i]);
  }
  printf("\n");

  if (integrity_algorithm != EIA0_ALG_ID) {
    nas->security_container = stream_security_container_init(ciphering_algorithm, integrity_algorithm, knas_enc, knas_int);
  } else {
    LOG_E(NAS, "Rejecting Invalid NULL integrity %d for 5G!\n",
      integrity_algorithm);
    nas->security_container = NULL;
  }

  /* Handle the invalid container with a reject message */
  if(nas->security_container == NULL) {
    LOG_W(NAS, "Could not create security container!\n");
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
    return;
  }

  /* Check MAC and delete context if it does not match */
  uint8_t computed_mac[NAS_INTEGRITY_SIZE];
  byte_array_t ba = {.buf = pdu, .len = pdu_length};
  nas_security_compute_mac(nas, ba, false, true, computed_mac);

  /* Teardown security container if mismatch. */
  if(memcmp(computed_mac, recv_mac, NAS_INTEGRITY_SIZE) != 0) {
    LOG_W(NAS, "MAC does not match\n");
    LOG_W(NAS, "Expected: %x %x %x %x\n", computed_mac[0], computed_mac[1], computed_mac[2], computed_mac[3]);
    LOG_W(NAS, "Received: %x %x %x %x\n", recv_mac[0], recv_mac[1], recv_mac[2], recv_mac[3]);
    stream_security_container_delete(nas->security_container);
    nas->security_container = NULL;

    /* Signal rejection */
    generateSecurityModeReject(nas, initialNasMsg, Security_mode_rejected_unspecified);
    return;
  }

  nas_itti_kgnb_refresh_req(nas->UE_id, nas->security.kgnb);
  generateSecurityModeComplete(nas, initialNasMsg);
}

static void generateRegistrationComplete(nr_ue_nas_t *nas,
                                         as_nas_info_t *initialNasMsg,
                                         SORTransparentContainer *sortransparentcontainer)
{
  int length = 0;
  nas_stream_cipher_t stream_cipher;
  uint8_t mac[NAS_INTEGRITY_SIZE];
  fgmm_nas_msg_security_protected_t sp = {0};

  // set security protected header
  sp.header.protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  sp.header.security_header_type = INTEGRITY_PROTECTED_AND_CIPHERED;
  sp.header.message_authentication_code = 0;
  sp.header.sequence_number = nas->security.nas_count_ul & 0xff;
  length = 7;
  // set plain 5GMM header
  sp.plain.header = set_mm_header(FGS_REGISTRATION_COMPLETE, PLAIN_5GS_MSG);
  length += sizeof(sp.plain.header);

  registration_complete_msg *mm_msg = &sp.plain.mm_msg.registration_complete;
  if (sortransparentcontainer) {
    mm_msg->sortransparentcontainer = sortransparentcontainer;
    length += sortransparentcontainer->sortransparentcontainercontents.length;
  }

  // encode the message
  initialNasMsg->nas_data = malloc_or_fail(length * sizeof(*initialNasMsg->nas_data));
  initialNasMsg->length = length;
  // encode security protected header
  int encoded = nas_protected_security_header_encode(initialNasMsg->nas_data, &sp.header, length);
  if (encoded < 0) {
    LOG_E(NAS, "generateRegistrationComplete: failed to encode security protected header\n");
    return;
  }
  // encode 5GMM plain header
  encoded = _nas_mm_msg_encode_header(&sp.plain.header, initialNasMsg->nas_data + encoded, length - encoded);
  if (encoded < 0) {
    LOG_E(NAS, "generateRegistrationComplete: failed to encode 5GMM plain header\n");
    return;
  }

  encode_registration_complete(mm_msg, initialNasMsg->nas_data + encoded, length - encoded);

  /* ciphering */
  uint8_t buf[initialNasMsg->length - 7];
  stream_cipher.context = nas->security_container->ciphering_context;
  AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
  stream_cipher.count = nas->security.nas_count_ul;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 7);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 7) << 3;
  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
  memcpy(stream_cipher.message, buf, initialNasMsg->length - 7);

  /* integrity protection */
  stream_cipher.context = nas->security_container->integrity_context;
  stream_cipher.count = nas->security.nas_count_ul++;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 6) << 3;
  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);

  printf("mac %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
  for (int i = 0; i < 4; i++) {
    initialNasMsg->nas_data[2 + i] = mac[i];
  }
  /* Set NAS 5GMM state */
  nas->fiveGMM_state = FGS_REGISTERED;
}

/**
 * @brief Capture IPv4 PDU Session Address
 */
static int capture_ipv4_addr(const uint8_t *addr, char *ip, size_t len)
{
  return snprintf(ip, len, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
}

/**
 * @brief Capture IPv6 PDU Session Address
 */
static int capture_ipv6_addr(const uint8_t *addr, char *ip, size_t len)
{
  // 24.501 Sec 9.11.4.10: "an interface identifier for the IPv6 link local
  // address": link local starts with fe80::, and only the last 64bits are
  // given (middle is zero)
  return snprintf(ip,
                  len,
                  "fe80::%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                  addr[0],
                  addr[1],
                  addr[2],
                  addr[3],
                  addr[4],
                  addr[5],
                  addr[6],
                  addr[7]);
}

/**
 * @brief Process PDU Session Address in PDU Session Establishment Accept message
 *        and configure the tun interface
 */
static void process_pdu_session_addr(pdu_session_establishment_accept_msg_t *msg,
                                     int ue_id,
                                     int pdu_session_id,
                                     nas_ue_pdu_tun_t *t)
{
  uint8_t *addr = msg->pdu_addr_ie.pdu_addr_oct;
  AssertFatal(pdu_session_id > 0 && pdu_session_id < MAX_NUM_PSI, "invalid PDU session ID %d\n", pdu_session_id);

  switch (msg->pdu_addr_ie.pdu_type) {
    case PDU_SESSION_TYPE_IPV4: {
      char ip[20];
      capture_ipv4_addr(&addr[0], ip, sizeof(ip));
      nr_ue_tun_create_ip_if(t, ip, NULL, ue_id, pdu_session_id);
    } break;

    case PDU_SESSION_TYPE_IPV6: {
      char ipv6[40];
      capture_ipv6_addr(addr, ipv6, sizeof(ipv6));
      nr_ue_tun_create_ip_if(t, NULL, ipv6, ue_id, pdu_session_id);
    } break;

    case PDU_SESSION_TYPE_IPV4V6: {
      char ipv6[40];
      capture_ipv6_addr(addr, ipv6, sizeof(ipv6));
      char ipv4[20];
      capture_ipv4_addr(&addr[IPv6_INTERFACE_ID_LENGTH], ipv4, sizeof(ipv4));
      nr_ue_tun_create_ip_if(t, ipv4, ipv6, ue_id, pdu_session_id);
    } break;

    default:
      LOG_E(NAS, "Unknown PDU Session Address type %d\n", msg->pdu_addr_ie.pdu_type);
      break;
  }
}

/** @brief Set one PSI from a NAS TUN entry */
static void nas_tun_psi_set(nas_tun_psi_t *out, int pdusession_id, nas_ue_pdu_tun_t *t)
{
  out->pdusession_id = pdusession_id;
  out->sock = t->sock;
  out->qfi = t->qfi;
  out->reader_thread = &t->reader_thread;
}

/** @brief Ask RRC to start the user-plane TUN reader for one PSI */
static void send_nas_tun_req(nr_ue_nas_t *nas, int pdusession_id)
{
  DevAssert(pdusession_id > 0 && pdusession_id < MAX_NUM_PSI);
  nas_ue_pdu_tun_t *t = &nas->pdu_tun[pdusession_id];
  DevAssert(t->sock >= 0);
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_TUN_REQ);
  nas_tun_req_t *req = &NAS_TUN_REQ(msg);
  req->action = NAS_TUN_START_USER_PLANE;
  req->n_psi = 1;
  nas_tun_psi_set(&req->psi[0], pdusession_id, t);
  itti_send_msg_to_task(TASK_RRC_NRUE, nas->UE_id, msg);
}

/** @brief Ask RRC to change how TUN uplink is read for this UE
 * User-plane mode: include every active PDU session so RRC can bind SDAP and start the reader
 * Idle-listener mode: send only the mode with an empty session list */
static void send_nas_tun_req_action(nr_ue_nas_t *nas, nas_tun_req_action_t action)
{
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_TUN_REQ);
  nas_tun_req_t *req = &NAS_TUN_REQ(msg);
  req->action = action;
  if (action == NAS_TUN_START_USER_PLANE) {
    for (int psi = 1; psi < MAX_NUM_PSI && req->n_psi < NAS_TUN_LIST_MAX; psi++) {
      nas_ue_pdu_tun_t *t = &nas->pdu_tun[psi];
      if (nas->psi_status[psi] == PDU_SESSION_INACTIVE || t->sock < 0)
        continue;
      nas_tun_psi_set(&req->psi[req->n_psi], psi, t);
      req->n_psi++;
    }
  }
  itti_send_msg_to_task(TASK_RRC_NRUE, nas->UE_id, msg);
}

/**
 * @brief Return the PDU session config for matching PDU session ID and type
 */
static const pdu_session_config_t *get_pdu_session_config(const uicc_t *uicc, int id, const pdu_session_establishment_accept_msg_t *msg)
{
  for (int idx = 0; idx < uicc->n_pdu_sessions; ++idx) {
    const pdu_session_config_t *pdu = &uicc->pdu_sessions[idx];
    bool msg_ipv4or6 = msg->pdu_type == PDU_SESSION_TYPE_IPV4 || msg->pdu_type == PDU_SESSION_TYPE_IPV6;
    bool correct_type = pdu->type == msg->pdu_type || (pdu->type == PDU_SESSION_TYPE_IPV4V6 && msg_ipv4or6);
    if (pdu->id == id && correct_type)
      return pdu;
  }
  return NULL;
}

/**
 * @brief Handle PDU Session Establishment Accept and process decoded message
 */
static void handle_pdu_session_accept(nr_ue_nas_t *nas, uint8_t *pdu_buffer, uint32_t msg_length)
{
  pdu_session_establishment_accept_msg_t msg = {0};
  int size = 0;
  int decoded = 0;

  // Security protected NAS header (7 bytes)
  fgs_nas_message_security_header_t sec_nas_hdr = {0};
  if ((decoded = decode_5gs_security_protected_header(&sec_nas_hdr, pdu_buffer, msg_length)) < 0) {
    LOG_E(NAS, "decode_5gs_security_protected_header failure in PDU Session Establishment Accept decoding\n");
    return;
  }
  size += decoded;

  // decode plain 5GMM message header
  fgmm_msg_header_t mm_header = {0};
  if ((decoded = decode_5gmm_msg_header(&mm_header, pdu_buffer + size, msg_length - size)) < 0) {
    LOG_E(NAS, "decode_5gmm_msg_header failure in PDU Session Establishment Accept decoding\n");
    return;
  }
  size += decoded;

  /* Process container (5GSM message) */
  // Payload container type and spare (1 octet)
  size++;
  // Payload container length
  uint16_t iei_len = 0;
  GET_SHORT(pdu_buffer + size, iei_len);
  size += sizeof(iei_len);
  // decode plain 5GSM message header
  fgsm_msg_header_t sm_header = {0};
  if ((decoded = decode_5gsm_msg_header(&sm_header, pdu_buffer + size, msg_length - size)) < 0) {
    LOG_E(NAS, "decode_5gsm_msg_header failure in PDU Session Establishment Accept decoding\n");
    return;
  }
  size += decoded;

  // decode PDU Session Establishment Accept
  if (!decode_pdu_session_establishment_accept_msg(&msg, pdu_buffer + size, msg_length)) {
    LOG_E(NAS, "decode_pdu_session_establishment_accept_msg failure\n");
    return;
  }

  const pdu_session_config_t *pdu = get_pdu_session_config(nas->uicc, sm_header.pdu_session_id, &msg);
  if (!pdu) {
    LOG_E(NAS,
          "PDU session establishment accept for ID %d type %d not in list of configured PDU sessions, abort transaction\n",
          sm_header.pdu_session_id,
          msg.pdu_type);
    return;
  }

  AssertFatal(sm_header.pdu_session_id > 0 && sm_header.pdu_session_id < MAX_NUM_PSI,
              "invalid PDU session ID %d\n",
              sm_header.pdu_session_id);
  nas_ue_pdu_tun_t *t = &nas->pdu_tun[sm_header.pdu_session_id];

  // Set QFI before starting UE interface thread to avoid early SDUs using 0-initialized QFI.
  nr_ue_tun_store_qfi(t, msg.qos_rules.rule->qfi);

  // process PDU Session: pass ID -1 to not append PDU ID to interface
  bool is_default = pdu == nas->uicc->pdu_sessions; // if first index
  // if a name has been provided, use that, else auto-generate interface name
  if (pdu->ifname) {
    int ret = snprintf(t->ifname, IFNAMSIZ, "%s", pdu->ifname);
    if (ret >= IFNAMSIZ) {
      LOG_E(NAS, "could not format interface name '%s': resulting length is %d\n", pdu->ifname, ret);
      return;
    }
  } else {
    const int ifname_pdu_id = is_default ? -1 : sm_header.pdu_session_id;
    bool is_eth = msg.pdu_type == PDU_SESSION_TYPE_ETHER;
    tuntap_generate_ue_ifname(t->ifname, is_eth ? IFF_TAP : IFF_TUN, nas->UE_id, ifname_pdu_id);
  }
  if (msg.pdu_type == PDU_SESSION_TYPE_ETHER) {
    nr_ue_tun_create_eth_if(t);
  } else if (msg.pdu_addr_ie.pdu_length) {
    process_pdu_session_addr(&msg, nas->UE_id, sm_header.pdu_session_id, t);
  } else {
    LOG_W(NAS, "Unhandled PDU session type %d, ignoring PDU session ID %d\n", msg.pdu_type, sm_header.pdu_session_id);
    return;
  }
  /* Track active PDU session for later PDU session status IE (24.501 8.2.16.3) */
  nas->psi_status[sm_header.pdu_session_id] = PDU_SESSION_ACTIVE;
  // ask RRC to start connected reader for this PSI
  if (t->sock > 0)
    send_nas_tun_req(nas, sm_header.pdu_session_id);
  else
    LOG_W(NAS, "could not create data interface reader thread, cannot inject data traffic\n");
}

static void send_nas_uplink_data_req(nr_ue_nas_t *nas, const as_nas_info_t *initial_nas_msg)
{
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_UPLINK_DATA_REQ);
  ul_info_transfer_req_t *req = &NAS_UPLINK_DATA_REQ(msg);
  req->UEid = nas->UE_id;
  req->nasMsg.nas_data = initial_nas_msg->nas_data;
  req->nasMsg.length = initial_nas_msg->length;
  itti_send_msg_to_task(TASK_RRC_NRUE, nas->UE_id, msg);
}

static void generatePduSessionModificationComplete(nr_ue_nas_t *nas,
                                                   as_nas_info_t *responseMsg,
                                                   uint8_t pdu_session_id,
                                                   uint8_t pti)
{
  int size = 0;

  // Setup PDU session modification complete message
  uint16_t req_length = 4;
  uint8_t *req_buffer = malloc_or_fail(req_length);
  pdu_session_modification_complete_msg pdu_session_mod_complete;
  pdu_session_mod_complete.protocoldiscriminator = FGS_SESSION_MANAGEMENT_MESSAGE;
  pdu_session_mod_complete.pdusessionid = pdu_session_id;
  pdu_session_mod_complete.pti = pti;
  pdu_session_mod_complete.pdusessionmodificationcompletemsgtype = FGS_PDU_SESSION_MODIFICATION_COMPLETE;
  encode_pdu_session_modification_complete(&pdu_session_mod_complete, req_buffer, req_length);

  nas_stream_cipher_t stream_cipher = {0};
  uint8_t mac[NAS_INTEGRITY_SIZE];

  // 5GMM security protected message
  fgmm_nas_msg_security_protected_t sp_msg = {0};
  // 5GMM security protected message header
  fgs_nas_message_security_header_t *sp_header = &sp_msg.header;
  sp_header->protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  sp_header->security_header_type = INTEGRITY_PROTECTED_AND_CIPHERED;
  sp_header->sequence_number = nas->security.nas_count_ul & 0xff;

  size += 7;

  fgmm_nas_message_plain_t *plain = &sp_msg.plain;

  // Plain 5GMM header
  plain->header = set_mm_header(FGS_UPLINK_NAS_TRANSPORT, PLAIN_5GS_MSG);
  size += sizeof(plain->header);

  fgs_uplink_nas_transport_msg *mm_msg = &plain->mm_msg.uplink_nas_transport;
  mm_msg->payloadcontainertype.type = 1;
  size += 1;
  mm_msg->fgspayloadcontainer.payloadcontainercontents.length = req_length;
  mm_msg->fgspayloadcontainer.payloadcontainercontents.value = req_buffer;
  size += (2 + req_length);
  mm_msg->pdusessionid = pdu_session_id;
  size += 2;

  // Encode the message
  responseMsg->nas_data = malloc_or_fail(size * sizeof(*responseMsg->nas_data));
  int security_header_len = nas_protected_security_header_encode(responseMsg->nas_data, sp_header, size);

  responseMsg->length =
      security_header_len
      + mm_msg_encode(plain, (uint8_t *)(responseMsg->nas_data + security_header_len), size - security_header_len);

  // Free allocated memory after encode
  free(req_buffer);

  /* ciphering */
  uint8_t buf[responseMsg->length - 7];
  stream_cipher.context = nas->security_container->ciphering_context;
  AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
  stream_cipher.count = nas->security.nas_count_ul;
  stream_cipher.bearer = 1;
  stream_cipher.message = (unsigned char *)(responseMsg->nas_data + 7);
  /* length in bits */
  stream_cipher.blength = (responseMsg->length - 7) << 3;
  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
  memcpy(stream_cipher.message, buf, responseMsg->length - 7);

  /* integrity protection */
  stream_cipher.context = nas->security_container->integrity_context;
  stream_cipher.count = nas->security.nas_count_ul++;
  stream_cipher.bearer = 1;
  stream_cipher.message = (unsigned char *)(responseMsg->nas_data + 6);
  /* length in bits */
  stream_cipher.blength = (responseMsg->length - 6) << 3;
  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);

  for (int i = 0; i < 4; i++) {
    responseMsg->nas_data[2 + i] = mac[i];
  }
}

/**
 * @brief Handle PDU Session Modification Command
 */
static void handle_pdu_session_modification_command(nr_ue_nas_t *nas, uint8_t *pdu_buffer, uint32_t msg_length)
{
  pdu_session_modification_command_msg_t msg = {0};
  int size = 0;
  int decoded = 0;

  // Security protected NAS header (7 bytes)
  fgs_nas_message_security_header_t sec_nas_hdr = {0};
  if ((decoded = decode_5gs_security_protected_header(&sec_nas_hdr, pdu_buffer, msg_length)) < 0) {
    LOG_E(NAS, "decode_5gs_security_protected_header failure in PDU Session Modification Command decoding\n");
    return;
  }
  size += decoded;

  // decode plain 5GMM message header
  fgmm_msg_header_t mm_header = {0};
  if ((decoded = decode_5gmm_msg_header(&mm_header, pdu_buffer + size, msg_length - size)) < 0) {
    LOG_E(NAS, "decode_5gmm_msg_header failure in PDU Session Modification Command decoding\n");
    return;
  }
  size += decoded;

  /* Process container (5GSM message) */
  // Payload container type and spare (1 octet)
  size++;
  // Payload container length
  uint16_t iei_len = 0;
  GET_SHORT(pdu_buffer + size, iei_len);
  size += sizeof(iei_len);
  // decode plain 5GSM message header
  fgsm_msg_header_t sm_header = {0};
  if ((decoded = decode_5gsm_msg_header(&sm_header, pdu_buffer + size, msg_length - size)) < 0) {
    LOG_E(NAS, "decode_5gsm_msg_header failure in PDU Session Modification Command decoding\n");
    return;
  }
  size += decoded;

  // decode PDU Session Modification Command
  if (iei_len < decoded || size + (iei_len - decoded) > msg_length) {
    LOG_E(NAS, "Invalid payload container length in PDU Session Modification Command\n");
    return;
  }
  uint32_t msg_body_len = iei_len - decoded;
  if (decode_pdu_session_modification_command(&msg, pdu_buffer + size, msg_body_len) < 0) {
    LOG_E(NAS, "decode_pdu_session_modification_command failure\n");
    return;
  }

  // Process QoS rules if present
  if (msg.qos_rules_present) {
    auth_qos_rule_t *qos_rules = &msg.qos_rules;

    for (int i = 0; i < qos_rules->num_rules; i++) {
      qos_rule_t *rule = &qos_rules->rule[i];

      switch (rule->oc) {
        case ROC_CREATE_NEW_QOS_RULE:
          LOG_I(NAS,
                "PDU session %d: CREATE new QoS rule %d with QFI %d (precedence %d, %d filters)\n",
                sm_header.pdu_session_id,
                rule->id,
                rule->qfi,
                rule->precedence,
                rule->nb_pf);
          // Only update the legacy entity QFI when this is the default QoS rule
          if (rule->dqr) {
            nr_ue_tun_store_qfi(&nas->pdu_tun[sm_header.pdu_session_id], rule->qfi);
            LOG_I(NAS, "PDU session %d: set default QFI to %d\n", sm_header.pdu_session_id, rule->qfi);
          }
          nr_sdap_qos_rule_add(nas->UE_id,
                               sm_header.pdu_session_id,
                               rule->id,
                               rule->qfi,
                               rule->precedence,
                               rule->dqr,
                               rule->packet_filters,
                               rule->num_packet_filters);
          break;

        case ROC_DELETE_QOS_RULE:
          LOG_I(NAS, "PDU session %d: DELETE QoS rule %d\n", sm_header.pdu_session_id, rule->id);
          nr_sdap_qos_rule_remove(nas->UE_id, sm_header.pdu_session_id, rule->id);
          break;

        case ROC_MODIFY_QOS_RULE_ADD_PF:
          LOG_I(NAS,
                "PDU session %d: MODIFY QoS rule %d - ADD %d packet filters (QFI %d)\n",
                sm_header.pdu_session_id,
                rule->id,
                rule->nb_pf,
                rule->qfi);
          nr_sdap_qos_rule_update(nas->UE_id,
                                  sm_header.pdu_session_id,
                                  rule->id,
                                  rule->qfi,
                                  rule->precedence,
                                  rule->dqr,
                                  rule->packet_filters,
                                  rule->num_packet_filters,
                                  false);
          break;

        case ROC_MODIFY_QOS_RULE_REPLACE_PF:
          LOG_I(NAS,
                "PDU session %d: MODIFY QoS rule %d - REPLACE packet filters with %d new filters (QFI %d)\n",
                sm_header.pdu_session_id,
                rule->id,
                rule->nb_pf,
                rule->qfi);
          nr_sdap_qos_rule_update(nas->UE_id,
                                  sm_header.pdu_session_id,
                                  rule->id,
                                  rule->qfi,
                                  rule->precedence,
                                  rule->dqr,
                                  rule->packet_filters,
                                  rule->num_packet_filters,
                                  true);
          break;

        case ROC_MODIFY_QOS_RULE_DELETE_PF:
          LOG_I(NAS,
                "PDU session %d: MODIFY QoS rule %d - DELETE %d packet filters (QFI %d)\n",
                sm_header.pdu_session_id,
                rule->id,
                rule->num_pf_delete,
                rule->qfi);
          nr_sdap_qos_rule_delete_pf(nas->UE_id,
                                     sm_header.pdu_session_id,
                                     rule->id,
                                     rule->pf_delete_ids,
                                     rule->num_pf_delete);
          break;

        case ROC_MODIFY_QOS_RULE_WITHOUT_PF:
          LOG_I(NAS,
                "PDU session %d: MODIFY QoS rule %d without changing packet filters (QFI %d)\n",
                sm_header.pdu_session_id,
                rule->id,
                rule->qfi);
          nr_sdap_qos_rule_update(nas->UE_id,
                                  sm_header.pdu_session_id,
                                  rule->id,
                                  rule->qfi,
                                  rule->precedence,
                                  rule->dqr,
                                  NULL,
                                  0,
                                  false);
          break;

        default:
          LOG_W(NAS, "PDU session %d: Unknown rule operation code %d for rule %d\n", sm_header.pdu_session_id, rule->oc, rule->id);
          break;
      }
    }
  }

  if (msg.sess_ambr_present) {
    LOG_I(NAS, "PDU session %d: Session-AMBR updated\n", sm_header.pdu_session_id);
  }

  if (msg.cause_present) {
    LOG_I(NAS, "PDU session %d: 5GSM cause: 0x%02x\n", sm_header.pdu_session_id, msg.cause);
  }

  // Send PDU Session Modification Complete response
  as_nas_info_t response_msg = {0};
  generatePduSessionModificationComplete(nas, &response_msg, sm_header.pdu_session_id, sm_header.pti);
  send_nas_uplink_data_req(nas, &response_msg);
  LOG_I(NAS, "Send NAS_UPLINK_DATA_REQ message(PduSessionModificationComplete)\n");
}

/**
 * @brief Handle DL NAS Transport and process piggybacked 5GSM messages
 */
void handleDownlinkNASTransport(nr_ue_nas_t *nas, uint8_t *pdu_buffer, int pdu_length)
{
  if (pdu_length < 17) {
    LOG_E(NAS, "Received DL NAS Transport message too short (%d)\n", pdu_length);
    return;
  }
  uint8_t msg_type = *(pdu_buffer + 16);
  if (msg_type == FGS_PDU_SESSION_ESTABLISHMENT_ACC) {
    LOG_A(NAS, "Received PDU Session Establishment Accept in DL NAS Transport\n");
    handle_pdu_session_accept(nas, pdu_buffer, pdu_length);
  } else if (msg_type == FGS_PDU_SESSION_MODIFICATION_COMMAND) {
    LOG_A(NAS, "Received PDU Session Modification Command in DL NAS Transport\n");
    handle_pdu_session_modification_command(nas, pdu_buffer, pdu_length);
  } else {
    LOG_E(NAS, "Received unexpected message in DLinformationTransfer %d\n", msg_type);
  }
}

static void generateDeregistrationRequest(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg)
{
  fgmm_nas_msg_security_protected_t sp_msg = {0};
  fgs_nas_message_security_header_t *sp_header = &sp_msg.header;
  sp_header->protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  sp_header->security_header_type = INTEGRITY_PROTECTED_AND_CIPHERED;
  sp_header->message_authentication_code = 0;
  sp_header->sequence_number = nas->security.nas_count_ul & 0xff;
  int size = sizeof(sp_msg.header);

  // Plain 5GMM header
  sp_msg.plain.header = set_mm_header(FGS_DEREGISTRATION_REQUEST_UE_ORIGINATING, INTEGRITY_PROTECTED_AND_CIPHERED_WITH_NEW_SECU_CTX);
  size += sizeof(sp_msg.plain.header);

  // Plain 5GMM
  fgs_deregistration_request_ue_originating_msg *mm_msg = &sp_msg.plain.mm_msg.fgs_deregistration_request_ue_originating;
  mm_msg->deregistrationtype.switchoff = NORMAL_DEREGISTRATION; // note: in case this is changed to SWITCH_OFF, handle in unprotected_allowed
  mm_msg->deregistrationtype.reregistration_required = REREGISTRATION_NOT_REQUIRED;
  mm_msg->deregistrationtype.access_type = TGPP_ACCESS;
  mm_msg->naskeysetidentifier.naskeysetidentifier = 1;
  size += 1;
  size += fill_guti(&mm_msg->fgsmobileidentity, nas->guti);

  // encode the message
  initialNasMsg->nas_data = calloc_or_fail(size, sizeof(*initialNasMsg->nas_data));
  int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, sp_header, size);

  initialNasMsg->length =
      security_header_len
      + mm_msg_encode(&sp_msg.plain, (uint8_t *)(initialNasMsg->nas_data + security_header_len), size - security_header_len);

  nas_stream_cipher_t stream_cipher;

  /* ciphering */
  uint8_t buf[initialNasMsg->length - 7];
  stream_cipher.context = nas->security_container->ciphering_context;
  AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
  stream_cipher.count = nas->security.nas_count_ul;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 7);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 7) << 3;
  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
  memcpy(stream_cipher.message, buf, initialNasMsg->length - 7);

  /* integrity protection */
  stream_cipher.context = nas->security_container->integrity_context;
  stream_cipher.count = nas->security.nas_count_ul++;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 6) << 3;
  uint8_t mac[NAS_INTEGRITY_SIZE];
  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);

  printf("mac %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
  for (int i = 0; i < 4; i++) {
    initialNasMsg->nas_data[2 + i] = mac[i];
  }
  /* Set NAS 5GMM state */
  nas->fiveGMM_state = FGS_DEREGISTERED_INITIATED;
}

static void generatePduSessionEstablishRequest(nr_ue_nas_t *nas, as_nas_info_t *initialNasMsg, nas_pdu_session_req_t *pdu_req)
{
  int size = 0;

  // setup pdu session establishment request
  uint16_t req_length = 7;
  uint8_t *req_buffer = malloc(req_length);
  pdu_session_establishment_request_msg pdu_session_establish;
  pdu_session_establish.protocoldiscriminator = FGS_SESSION_MANAGEMENT_MESSAGE;
  pdu_session_establish.pdusessionid = pdu_req->pdusession_id;
  pdu_session_establish.pti = 1;
  pdu_session_establish.pdusessionestblishmsgtype = FGS_PDU_SESSION_ESTABLISHMENT_REQ;
  pdu_session_establish.maxdatarate = 0xffff;
  pdu_session_establish.pdusessiontype = pdu_req->pdusession_type;
  encode_pdu_session_establishment_request(&pdu_session_establish, req_buffer);

  nas_stream_cipher_t stream_cipher;
  uint8_t mac[NAS_INTEGRITY_SIZE];

  // 5GMM security protected message
  fgmm_nas_msg_security_protected_t sp_msg = {0};
  // 5GMM security protected message header
  fgs_nas_message_security_header_t *sp_header = &sp_msg.header;
  sp_header->protocol_discriminator = FGS_MOBILITY_MANAGEMENT_MESSAGE;
  sp_header->security_header_type = INTEGRITY_PROTECTED_AND_CIPHERED;
  sp_header->sequence_number = nas->security.nas_count_ul & 0xff;

  size += 7;

  fgmm_nas_message_plain_t *plain = &sp_msg.plain;

  // Plain 5GMM header
  plain->header = set_mm_header(FGS_UPLINK_NAS_TRANSPORT, PLAIN_5GS_MSG);
  size += sizeof(plain->header);

  fgs_uplink_nas_transport_msg *mm_msg = &plain->mm_msg.uplink_nas_transport;
  mm_msg->payloadcontainertype.iei = 0;
  mm_msg->payloadcontainertype.type = 1;
  size += 1;
  mm_msg->fgspayloadcontainer.payloadcontainercontents.length = req_length;
  mm_msg->fgspayloadcontainer.payloadcontainercontents.value = req_buffer;
  size += (2 + req_length);
  mm_msg->pdusessionid = pdu_req->pdusession_id;
  mm_msg->requesttype = 1;
  mm_msg->requesttype_present = true;
  mm_msg->snssai_present = true;
  mm_msg->dnn_present = true;
  size += 3;
  const bool has_nssai_sd = pdu_req->sd != 0xffffff; // 0xffffff means "no SD", TS 23.003
  const size_t nssai_len = has_nssai_sd ? 4 : 1;
  mm_msg->snssai.length = nssai_len;
  mm_msg->snssai.value = calloc(1, nssai_len);
  mm_msg->snssai.value[0] = pdu_req->sst;
  if (has_nssai_sd)
    INT24_TO_BUFFER(pdu_req->sd, &mm_msg->snssai.value[1]);
  size += 1 + 1 + nssai_len;
  int dnnSize = strlen(pdu_req->dnn);
  mm_msg->dnn.value = calloc(1, dnnSize + 1);
  mm_msg->dnn.length = dnnSize + 1;
  mm_msg->dnn.value[0] = dnnSize;
  memcpy(mm_msg->dnn.value + 1, pdu_req->dnn, dnnSize);
  size += (1 + 1 + dnnSize + 1);

  // encode the message
  initialNasMsg->nas_data = malloc_or_fail(size * sizeof(*initialNasMsg->nas_data));
  int security_header_len = nas_protected_security_header_encode(initialNasMsg->nas_data, sp_header, size);

  initialNasMsg->length =
      security_header_len
      + mm_msg_encode(plain, (uint8_t *)(initialNasMsg->nas_data + security_header_len), size - security_header_len);

  // Free allocated memory after encode
  free(req_buffer);
  free(mm_msg->dnn.value);
  free(mm_msg->snssai.value);

  /* ciphering */
  uint8_t buf[initialNasMsg->length - 7];
  stream_cipher.context = nas->security_container->ciphering_context;
  AssertFatal(nas->security.nas_count_ul <= 0xffffff, "fatal: NAS COUNT UL too big (todo: fix that)\n");
  stream_cipher.count = nas->security.nas_count_ul;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 7);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 7) << 3;
  stream_compute_encrypt(nas->security_container->ciphering_algorithm, &stream_cipher, buf);
  memcpy(stream_cipher.message, buf, initialNasMsg->length - 7);

  /* integrity protection */
  stream_cipher.context = nas->security_container->integrity_context;
  stream_cipher.count = nas->security.nas_count_ul++;
  stream_cipher.bearer = 1;
  stream_cipher.direction = 0;
  stream_cipher.message = (unsigned char *)(initialNasMsg->nas_data + 6);
  /* length in bits */
  stream_cipher.blength = (initialNasMsg->length - 6) << 3;
  stream_compute_integrity(nas->security_container->integrity_algorithm, &stream_cipher, mac);

  printf("mac %x %x %x %x \n", mac[0], mac[1], mac[2], mac[3]);
  for (int i = 0; i < 4; i++) {
    initialNasMsg->nas_data[2 + i] = mac[i];
  }
}

/** Send initial NAS to RRC as NAS_INITIAL_UL_TRANSFER_REQ (e.g. paging Service Request, TS 24.501 §5.6.1).
 *  RRC buffers for RRCSetupComplete dedicatedNAS when no SRB (TS 38.331 §5.3.3.4). */
static void send_nas_initial_ul_transfer_req(nr_ue_nas_t *nas, const as_nas_info_t *initial_nas_msg)
{
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_INITIAL_UL_TRANSFER_REQ);
  ul_info_transfer_req_t *req = &NAS_INITIAL_UL_TRANSFER_REQ(msg);
  req->UEid = nas->UE_id;
  req->nasMsg.nas_data = (uint8_t *)initial_nas_msg->nas_data;
  req->nasMsg.length = initial_nas_msg->length;
  itti_send_msg_to_task(TASK_RRC_NRUE, nas->UE_id, msg);
}

/** @brief Initiate Service Request and send it as initial NAS UL to RRC
 * Used for MT paging and MO UL-data indication */
static bool initiate_service_request(nr_ue_nas_t *nas, bool mo_ul_data)
{
  if (!nas->guti) {
    LOG_W(NAS, "[UE %ld] no GUTI available, cannot generate Service Request\n", nas->UE_id);
    return false;
  }

  /* TS 24.501 §5.6.1.1.2: while the service request procedure is ongoing the UE
   * shall not initiate another 5GMM procedure (5GMM-SERVICE-REQUEST-INITIATED state) */
  if (nas->fiveGMM_state == FGS_SERVICE_REQUEST_INITIATED) {
    LOG_D(NAS, "[UE %ld] Service Request already pending, ignore\n", nas->UE_id);
    return false;
  }

  if (nas->fiveGMM_state != FGS_REGISTERED) {
    LOG_W(NAS, "[UE %ld] UE not in 5GMM-REGISTERED (state=%d), cannot generate Service Request\n", nas->UE_id, nas->fiveGMM_state);
    return false;
  }

  /* If UE is 5GMM-CONNECTED, Service Request is not needed (TS 24.501 §5.6.2.2.1) */
  if (nas->fiveGMM_mode != FGS_IDLE) {
    LOG_W(NAS, "[UE %ld] UE already in 5GMM-CONNECTED (mode=%d), dropping Service Request\n", nas->UE_id, nas->fiveGMM_mode);
    return false;
  }

  as_nas_info_t initialNasMsg = {0};
  generateServiceRequest(&initialNasMsg, nas, mo_ul_data);
  if (initialNasMsg.length <= 0) {
    LOG_E(NAS, "[UE %ld] Failed to generate Service Request\n", nas->UE_id);
    return false;
  }
  /* TS 24.501 §5.6.1.2: send SERVICE REQUEST, enter 5GMM-SERVICE-REQUEST-INITIATED (§5.1.3.2.1.2.6) */
  nas->fiveGMM_state = FGS_SERVICE_REQUEST_INITIATED;
  send_nas_initial_ul_transfer_req(nas, &initialNasMsg);
  LOG_I(NAS, "[UE %ld] Service Request (%u B) sent to RRC (NAS_INITIAL_UL_TRANSFER_REQ)\n", nas->UE_id, initialNasMsg.length);
  return true;
}

static void send_nas_detach_req(nr_ue_nas_t *nas, bool wait_release)
{
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_DETACH_REQ);
  nas_detach_req_t *req = &NAS_DETACH_REQ(msg);
  req->wait_release = wait_release;
  itti_send_msg_to_task(TASK_RRC_NRUE, nas->UE_id, msg);
}

static void send_nas_5gmm_ind(instance_t instance, const Guti5GSMobileIdentity_t *guti)
{
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, 0, NAS_5GMM_IND);
  nas_5gmm_ind_t *ind = &NAS_5GMM_IND(msg);
  LOG_I(NR_RRC, "5G-GUTI: AMF pointer %u, AMF Set ID %u, 5G-TMSI %u \n", guti->amfpointer, guti->amfsetid, guti->tmsi);
  ind->fiveG_STMSI = nr_construct_5g_s_tmsi(guti->amfsetid, guti->amfpointer, guti->tmsi);
  itti_send_msg_to_task(TASK_RRC_NRUE, instance, msg);
}

void request_pdusession(nr_ue_nas_t *nas, const pdu_session_config_t *pdu)
{
  int t = pdu->type;
  AssertFatal(t == PDU_SESSION_TYPE_IPV4 || t == PDU_SESSION_TYPE_IPV6 || t == PDU_SESSION_TYPE_IPV4V6
                  || t == PDU_SESSION_TYPE_UNSTRUCT || t == PDU_SESSION_TYPE_ETHER,
              "illegal PDU session type %d\n",
              t);
  AssertFatal(t != PDU_SESSION_TYPE_UNSTRUCT, "unstructured PDU sessions not handled yet\n");
  MessageDef *message_p = itti_alloc_new_message(TASK_NAS_NRUE, nas->UE_id, NAS_PDU_SESSION_REQ);
  nas_pdu_session_req_t *pdu_req = &NAS_PDU_SESSION_REQ(message_p);
  pdu_req->pdusession_id = pdu->id;
  // 24.501: joint PDU session type IEI (0x9-, Table 8.3.1.1.1) and type (9.11.4.11)
  pdu_req->pdusession_type = 0x90 | t;
  pdu_req->sst = pdu->nssai.sst;
  pdu_req->sd = pdu->nssai.sd;
  snprintf(pdu_req->dnn, sizeof(pdu_req->dnn), "%s", pdu->dnn);
  itti_send_msg_to_task(TASK_NAS_NRUE, nas->UE_id, message_p);
}

static int get_user_nssai_idx(nssai_t ch_nssai, const nr_nas_msg_snssai_t allowed_nssai[NAS_MAX_NUMBER_SLICES])
{
  for (int i = 0; i < NAS_MAX_NUMBER_SLICES; i++) {
    const nr_nas_msg_snssai_t *nssai = allowed_nssai + i;
    /* If it was received in Registration Accept, check the SD
       in the stored Allowed N-SSAI, else, consider the SD valid */
    bool sd_match = !nssai->sd || (ch_nssai.sd == *nssai->sd);
    if ((ch_nssai.sst == nssai->sst) && sd_match)
      return i;
  }
  return -1;
}

void *nas_nrue_task(void *args_p)
{
  UNUSED(args_p);
  while (1) {
    nas_nrue(NULL);
  }
}

static void process_guti(Guti5GSMobileIdentity_t *guti, nr_ue_nas_t *nas)
{
  AssertFatal(guti->typeofidentity == FGS_MOBILE_IDENTITY_5G_GUTI,
              "registration accept 5GS Mobile Identity is not GUTI, but %d\n",
              guti->typeofidentity);
  nas->guti = malloc_or_fail(sizeof(*nas->guti));
  *nas->guti = *guti;
}

static void handle_registration_accept(nr_ue_nas_t *nas, const uint8_t *pdu_buffer, uint32_t msg_length)
{
  registration_accept_msg msg = {0};
  fgs_nas_message_security_header_t sp_header = {0};
  const uint8_t *end = pdu_buffer + msg_length;
  // security protected header
  int decoded = decode_5gs_security_protected_header(&sp_header, pdu_buffer, msg_length);
  if (!decoded) {
    LOG_E(NAS, "NAS Registration Accept: failed to decode security protected header\n");
    return;
  }
  pdu_buffer += decoded;
  // plain header
  fgmm_msg_header_t mm_header = {0};
  if ((decoded = decode_5gmm_msg_header(&mm_header, pdu_buffer, end - pdu_buffer)) < 0) {
    LOG_E(NAS, "Failed to decode NAS Registration Accept\n");
    return;
  }
  if (mm_header.message_type != FGS_REGISTRATION_ACCEPT) {
    LOG_E(NAS, "Failed to process NAS Registration Accept: wrong message type\n");
    return;
  }
  pdu_buffer += decoded;
  // plain payload
  const byte_array_t ba = {.buf = (uint8_t *)pdu_buffer, .len = end - pdu_buffer};
  if ((decoded = decode_registration_accept(&msg, ba)) < 0) {
    LOG_E(NAS, "Failed to decode NAS Registration Accept\n");
    return;
  }
  LOG_I(NAS,
        "Received Registration Accept with result %s\n",
        msg.result == FGS_REGISTRATION_RESULT_3GPP       ? "3GPP"
        : msg.result == FGS_REGISTRATION_RESULT_NON_3GPP ? "non-3PP"
                                                         : "3GPP and non-3GPP");
  LOG_I(NAS, "SMS %s in 5GS Registration Result\n", msg.sms_allowed ? "allowed" : "not allowed");

  pdu_buffer += decoded;
  // process GUTI
  if (msg.guti) {
    process_guti(&msg.guti->guti, nas);
  } else {
    LOG_W(NAS, "no GUTI in registration accept\n");
  }

  if(nas->guti)
    send_nas_5gmm_ind(nas->UE_id, nas->guti);

  as_nas_info_t initialNasMsg = {0};
  generateRegistrationComplete(nas, &initialNasMsg, NULL);
  if (initialNasMsg.length > 0) {
    send_nas_uplink_data_req(nas, &initialNasMsg);
    LOG_I(NAS, "Send NAS_UPLINK_DATA_REQ message(RegistrationComplete)\n");
  }
  if (nas->uicc->n_pdu_sessions == 0)
    LOG_W(SIM, "no PDU sessions to request configured\n");
  for (const pdu_session_config_t *pdu = nas->uicc->pdu_sessions; pdu < nas->uicc->pdu_sessions + nas->uicc->n_pdu_sessions; ++pdu) {
    if (get_user_nssai_idx(pdu->nssai, msg.nas_allowed_nssai) < 0) {
      LOG_E(NAS,
            "PDU session ID %d NSSAI %d.%d: mismatch for allowed NSSAI. Couldn't request PDU session.\n",
            pdu->id,
            pdu->nssai.sst,
            pdu->nssai.sd);
    } else {
      LOG_I(NAS, "requested PDU session ID %d type %d NSSAI %d.%d DNN %s\n", pdu->id, pdu->type, pdu->nssai.sst, pdu->nssai.sd, pdu->dnn);
      request_pdusession(nas, pdu);
    }
  }
  // Free local message after processing
  free_fgmm_registration_accept(&msg);
}

/* 3GPP TS 24.008 10.5.7.3 GPRS Timer */
static int process_gprs_timer(gprs_timer_t *timer)
{
  if (!timer)
    return -1;

  int factor = 0;
  switch (timer->unit) {
    case TWO_SECONDS:
      factor = 2;
      break;
    case ONE_MINUTE:
      factor = 60;
      break;
    case DECIHOURS:
      factor = 360; // 6 minutes
      break;
    case DEACTIVATED:
      return -1;
    default:
      factor = 60; // default is 60 seconds
      break;
  }

  return timer->value * factor;
}

/** @brief Abort an ongoing Service Request and return to 5GMM-REGISTERED
 * When fiveGMM_state is 5GMM-SERVICE-REQUEST-INITIATED, set it to 5GMM-REGISTERED
 * (TS 24.501 clause 5.6.1.5 Service Reject, clause 5.6.1.7 abnormal cases)
 * @note Does not stop T3517 (not implemented yet) */
static void abort_service_request(nr_ue_nas_t *nas)
{
  DevAssert(nas != NULL);
  if (nas->fiveGMM_state == FGS_SERVICE_REQUEST_INITIATED)
    nas->fiveGMM_state = FGS_REGISTERED;
}

static void handle_service_accept(nr_ue_nas_t *nas, const byte_array_t *buffer)
{
  LOG_I(NAS, "Received NAS Service Accept message\n");
  /** TS 24.501 §5.6.1.4: SERVICE ACCEPT - successful service request,
   * exit 5GMM-SERVICE-REQUEST-INITIATED, enter 5GMM-REGISTERED (§5.1.3.2.1.2.6). */
  nas->fiveGMM_state = FGS_REGISTERED;
  fgs_service_accept_msg_t msg = {0};
  decode_fgs_service_accept(&msg, buffer);
  // Extract timer t3448 in seconds (optional IE)
  nas->t3448 = process_gprs_timer(msg.t3448);
  // Extract possible reactivation errors
  for (int i = 0; i < msg.num_errors; i++)
    LOG_E(NAS,
          "Received PDU Session %d reactivation result error cause %s\n",
          msg.cause->pdu_session_id,
          print_info(msg.cause->cause, cause_text_info, sizeofArray(cause_text_info)));
  send_nas_tun_req_action(nas, NAS_TUN_START_USER_PLANE);
}

static void handle_service_reject(nr_ue_nas_t *nas, const byte_array_t *buffer)
{
  abort_service_request(nas);
  /* If still 5GMM-IDLE (MO path stopped them for SR), restart idle TUN listeners */
  if (nas->fiveGMM_mode == FGS_IDLE)
    send_nas_tun_req_action(nas, NAS_TUN_START_IDLE_LISTENER);
  // The buffer is the whole NAS PDU: skip the security protected header, if any, and the 5GMM header.
  uint8_t *pdu = buffer->buf;
  const uint8_t *end = buffer->buf + buffer->len;
  if (buffer->len > 1 && pdu[1] != PLAIN_5GS_MSG) {
    fgs_nas_message_security_header_t sp_header = {0};
    const int decoded = decode_5gs_security_protected_header(&sp_header, pdu, end - pdu);
    if (decoded < 0) {
      LOG_E(NAS, "Service Reject: failed to decode security protected header\n");
      return;
    }
    pdu += decoded;
  }
  fgmm_msg_header_t mm_header = {0};
  const int decoded = decode_5gmm_msg_header(&mm_header, pdu, end - pdu);
  if (decoded < 0) {
    LOG_E(NAS, "Service Reject: failed to decode NAS message header\n");
    return;
  }
  pdu += decoded;
  const byte_array_t ba = {.buf = pdu, .len = end - pdu};
  fgs_service_reject_msg_t msg = {0};
  if (decode_fgs_service_reject(&msg, &ba) < 0) {
    LOG_E(NAS, "Service Reject: failed to decode NAS message body\n");
    return;
  }
  // Extract timer t3448 in seconds (optional IE)
  nas->t3448 = process_gprs_timer(msg.t3448);
  // Extract timer t3446 in seconds (optional IE)
  nas->t3446 = process_gprs_timer(msg.t3446);
  LOG_E(NAS, "Received NAS Service Reject message with cause %s\n", print_info(msg.cause, fgmm_cause_s, sizeofArray(fgmm_cause_s)));
  /* Retrying the Service Request would loop forever, and re-registering is not supported:
   * terminate as after a Registration Reject. */
  LOG_E(NAS, "Service Reject: cannot connect, exiting\n");
  nas->fiveGMM_state = FGS_DEREGISTERED;
  nas->termination_procedure = true;
  send_nas_detach_req(nas, false);
}

/** @brief Handle Registration Reject (8.2.7 / 5.5.1.2.5 of 3GPP TS 24.501)
 * @todo Per §5.5.1.2.5: forbidden PLMN/TAI, registration attempt counter
 * @todo N1 NAS signalling release per §5.3.1.3
 * @todo Optional IEs T3346/T3502, EAP per §5.4.1.2.2.11 */
static void handle_registration_reject(nr_ue_nas_t *nas, const byte_array_t *buffer)
{
  DevAssert(buffer && buffer->buf);
  if (nas->termination_procedure) {
    return; // already in termination procedure, ignore registration reject
  }

  fgs_registration_reject_msg_t msg = {0};

  if (buffer->len < sizeof(fgmm_msg_header_t)) {
    LOG_E(NAS, "Failed to extract Registration Reject message body: buffer length too short\n");
    return;
  }

  const uint8_t *pdu_buffer = buffer->buf;
  uint32_t msg_length = buffer->len;
  const uint8_t *end = pdu_buffer + msg_length;

  if (pdu_buffer[1] != PLAIN_5GS_MSG) {
    fgs_nas_message_security_header_t sp_header = {0};
    int decoded = decode_5gs_security_protected_header(&sp_header, pdu_buffer, msg_length);
    if (decoded < 0) {
      LOG_E(NAS, "Registration Reject: failed to decode security protected header\n");
      return;
    }
    pdu_buffer += decoded;
  }

  fgmm_msg_header_t mm_header = {0};
  int decoded = decode_5gmm_msg_header(&mm_header, pdu_buffer, end - pdu_buffer);
  if (decoded < 0) {
    LOG_E(NAS, "Registration Reject: failed to decode NAS message header\n");
    return;
  }
  if (mm_header.message_type != FGS_REGISTRATION_REJECT) {
    LOG_E(NAS, "Expected NAS message type FGS_REGISTRATION_REJECT, got %#x\n", mm_header.message_type);
    return;
  }
  pdu_buffer += decoded;

  const byte_array_t ba = {.buf = (uint8_t *)pdu_buffer, .len = end - pdu_buffer};
  if (decode_fgs_registration_reject(&msg, &ba) < 0) {
    LOG_E(NAS, "Registration Reject: failed to decode NAS message body\n");
    free_fgs_registration_reject(&msg);
    return;
  }

  LOG_E(NAS, "Received Registration Reject cause: %s\n", print_info(msg.cause, fgmm_cause_s, sizeofArray(fgmm_cause_s)));
  free_fgs_registration_reject(&msg);
  nas->fiveGMM_state = FGS_DEREGISTERED;
  nas->termination_procedure = true;
  send_nas_detach_req(nas, false);
}

void *nas_nrue(void *args_p)
{
  UNUSED(args_p);
  // Wait for a message or an event
  MessageDef *msg_p;
  itti_receive_msg(TASK_NAS_NRUE, &msg_p);

  if (msg_p != NULL) {
    nr_ue_nas_t *nas = get_ue_nas_info(msg_p->ittiMsgHeader.destinationInstance);

    switch (ITTI_MSG_ID(msg_p)) {
      case INITIALIZE_MESSAGE:

        break;

      case TERMINATE_MESSAGE:
        itti_exit_task();
        break;

      case MESSAGE_TEST:
        break;

      case NAS_CELL_SELECTION_CNF:
        LOG_I(NAS,
              "[UE %ld] Received %s: errCode %u, cellID %u, tac %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              NAS_CELL_SELECTION_CNF(msg_p).errCode,
              NAS_CELL_SELECTION_CNF(msg_p).cellID,
              NAS_CELL_SELECTION_CNF(msg_p).tac);
        break;

      case NAS_CELL_SELECTION_IND:
        LOG_I(NAS,
              "[UE %ld] Received %s: cellID %u, tac %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              NAS_CELL_SELECTION_IND(msg_p).cellID,
              NAS_CELL_SELECTION_IND(msg_p).tac);

        /* TODO not processed by NAS currently */
        break;

      case NAS_PAGING_IND: {
        LOG_I(NAS, "[UE %ld] Received %s: cause %u\n", nas->UE_id, ITTI_MSG_NAME(msg_p), NAS_PAGING_IND(msg_p).cause);
        if (NAS_PAGING_IND(msg_p).cause != AS_CONNECTION_ESTABLISH) {
          LOG_I(NAS,
                "[UE %ld] Paging received with unsupported cause %u, not triggering Service Request\n",
                nas->UE_id,
                NAS_PAGING_IND(msg_p).cause);
          break;
        }

        /** Paging for 5GS services (TS 24.501 §5.6.2.2.1) and
         *  network-triggered Service Request (TS 23.502 §4.2.3.3 step 6)
         *  Upon reception of a paging indication the UE shall,
         *  when 5GMM-REGISTERED and in 5GMM-IDLE without suspend indication,
         *  initiate a Service Request over 3GPP access
         *
         * TODO (future work):
         *  - Implement T3346 and stop it here if running
         *  - Add explicit "suspend indication" handling for the 5GMM-IDLE-with-suspend case
         *    as per TS 24.501 §5.6.2.2.1 ("proceed as specified in subclause 5.3.1.5") */
        LOG_I(NAS, "[UE %ld] Paging: initiate Service Request\n", nas->UE_id);
        initiate_service_request(nas, false);
        break;
      }

      case NAS_MO_UL_DATA_IND: {
        /** MO Service Request for pending UL user data (TS 24.501 clause 5.6.1 case d)
         * initiate Service Request and include Uplink data status */
        LOG_D(NAS, "[UE %ld] MO UL data: request Service Request\n", nas->UE_id);
        initiate_service_request(nas, true);
        break;
      }

      case NAS_PDU_SESSION_REQ: {
        as_nas_info_t pduEstablishMsg = {0};
        nas_pdu_session_req_t *pduReq = &NAS_PDU_SESSION_REQ(msg_p);
        generatePduSessionEstablishRequest(nas, &pduEstablishMsg, pduReq);
        if (pduEstablishMsg.length > 0) {
          send_nas_uplink_data_req(nas, &pduEstablishMsg);
          LOG_I(NAS, "Send NAS_UPLINK_DATA_REQ message(PduSessionEstablishRequest)\n");
        }
        break;
      }

      case NR_NAS_CONN_ESTABLISH_IND: {
        nas->fiveGMM_mode = FGS_CONNECTED;
        LOG_I(NAS,
              "[UE %ld] Received %s: asCause %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              NR_NAS_CONN_ESTABLISH_IND(msg_p).asCause);
        break;
      }

      case NAS_CONN_ESTABLI_CNF: {
        LOG_I(NAS,
              "[UE %ld] Received %s: errCode %u, length %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              NAS_CONN_ESTABLI_CNF(msg_p).errCode,
              NAS_CONN_ESTABLI_CNF(msg_p).nasMsg.length);

        byte_array_t ba = {.buf = NAS_CONN_ESTABLI_CNF(msg_p).nasMsg.nas_data, .len = NAS_CONN_ESTABLI_CNF(msg_p).nasMsg.length};
        security_state_t security_state = nas_security_rx_process(nas, ba);
        if (security_state > NAS_SECURITY_INTEGRITY_PASSED) {
          LOG_E(NAS, "NAS integrity failed, discard incoming message: security state is %s\n", security_state_info[security_state].text);
          break;
        }

        fgs_nas_msg_t msg_type = get_msg_type(ba.buf, ba.len);
        LOG_D(NAS,
              "[UE %ld] NAS_CONN_ESTABLI_CNF decoded NAS msg_type=%s (%d)\n",
              nas->UE_id,
              print_info(msg_type, message_text_info, sizeofArray(message_text_info)),
              msg_type);
        if (msg_type == FGS_REGISTRATION_ACCEPT) {
          handle_registration_accept(nas, ba.buf, ba.len);
        } else if (msg_type == FGS_PDU_SESSION_ESTABLISHMENT_ACC) {
          handle_pdu_session_accept(nas, ba.buf, ba.len);
        } else if (msg_type == FGS_SERVICE_ACCEPT) {
          handle_service_accept(nas, &ba);
        } else if (msg_type == FGS_PDU_SESSION_MODIFICATION_COMMAND) {
          handle_pdu_session_modification_command(nas, ba.buf, ba.len);
        }

        // Free NAS buffer memory after use (coming from RRC)
        free_byte_array(ba);
        break;
      }

      case NR_NAS_CONN_RELEASE_IND: {
        LOG_I(NAS, "[UE %ld] Received %s: cause %s\n",
              nas->UE_id, ITTI_MSG_NAME (msg_p), nr_release_cause_desc[NR_NAS_CONN_RELEASE_IND (msg_p).cause]);
        /* In N1 mode, upon indication from lower layers that the access stratum connection has been released,
           the UE shall enter 5GMM-IDLE mode and consider the N1 NAS signalling connection released (TS 24.501 §5.3.1.3).
           If SR incomplete (5GMM-SERVICE-REQUEST-INITIATED) §5.6.1.7 l): abort SR, enter 5GMM-REGISTERED */
        abort_service_request(nas);
        nas->fiveGMM_mode = FGS_IDLE;
        // TODO handle connection release
        if (nas->termination_procedure) {
          /* the following is not clean, but probably necessary: we need to give
           * time to RLC to Ack the SRB1 PDU which contained the RRC release
           * message. Hence, we just below wait some time, before finally
           * unblocking the nr-uesoftmodem, which will terminate the process. */
          usleep(100000);
          itti_wait_tasks_unblock(); /* will unblock ITTI to stop nr-uesoftmodem */
        }
        break;
      }
      case NAS_UPLINK_DATA_CNF:
        LOG_I(NAS,
              "[UE %ld] Received %s: UEid %u, errCode %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              NAS_UPLINK_DATA_CNF(msg_p).UEid,
              NAS_UPLINK_DATA_CNF(msg_p).errCode);

        break;

      case NAS_DEREGISTRATION_REQ: {
        LOG_I(NAS, "[UE %ld] Received %s\n", nas->UE_id, ITTI_MSG_NAME(msg_p));
        nas_deregistration_req_t *req = &NAS_DEREGISTRATION_REQ(msg_p);
        if (nas->guti) {
          if (req->cause == AS_DETACH) {
            nas->termination_procedure = true;
            send_nas_detach_req(nas, true);
          }
          as_nas_info_t initialNasMsg = {0};
          generateDeregistrationRequest(nas, &initialNasMsg);
          send_nas_uplink_data_req(nas, &initialNasMsg);
        } else {
          LOG_W(NAS, "No GUTI, cannot trigger deregistration request.\n");
          if (req->cause == AS_DETACH)
            send_nas_detach_req(nas, false);
        }
      } break;

      case NAS_DOWNLINK_DATA_IND: {
        as_nas_info_t initialNasMsg = {0};
        uint8_t *pdu_buffer = NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.nas_data;
        int pdu_length = NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.length;
        byte_array_t buffer = {.buf = pdu_buffer, .len = pdu_length};
        security_state_t security_state = nas_security_rx_process(nas, buffer);
        if (security_state > NAS_SECURITY_INTEGRITY_PASSED) {
          LOG_E(NAS, "NAS integrity failed, discard incoming message\n");
          break;
        }

        fgs_nas_msg_t msg_type = get_msg_type(pdu_buffer, pdu_length);
        LOG_I(NAS,
              "[UE %ld] Received %s type %s with length %u\n",
              nas->UE_id,
              ITTI_MSG_NAME(msg_p),
              print_info(msg_type, message_text_info, sizeofArray(message_text_info)),
              NAS_DOWNLINK_DATA_IND(msg_p).nasMsg.length);

        switch (msg_type) {
          case FGS_IDENTITY_REQUEST:
            handle_identity_request(&initialNasMsg, nas, buffer);
            break;
          case FGS_AUTHENTICATION_REQUEST:
            handle_fgmm_authentication_request(nas, &initialNasMsg, &buffer);
            break;
          case FGS_AUTHENTICATION_REJECT:
            handle_authentication_reject(nas, pdu_buffer, pdu_length);
            break;
          case FGS_SECURITY_MODE_COMMAND:
            handle_security_mode_command(nas, &initialNasMsg, pdu_buffer, pdu_length);
            break;
          case FGS_DOWNLINK_NAS_TRANSPORT:
            handleDownlinkNASTransport(nas, pdu_buffer, pdu_length);
            break;
          case FGS_REGISTRATION_ACCEPT:
            handle_registration_accept(nas, pdu_buffer, pdu_length);
            break;
          case FGS_DEREGISTRATION_ACCEPT_UE_ORIGINATING:
            LOG_I(NAS, "received deregistration accept\n");
            /* Set NAS 5GMM state */
            nas->fiveGMM_state = FGS_DEREGISTERED;
            break;
          case FGS_PDU_SESSION_ESTABLISHMENT_ACC:
            handle_pdu_session_accept(nas, pdu_buffer, pdu_length);
            break;
          case FGS_PDU_SESSION_ESTABLISHMENT_REJ:
            LOG_E(NAS, "Received PDU Session Establishment reject\n");
            break;
          case FGS_PDU_SESSION_MODIFICATION_COMMAND:
            handle_pdu_session_modification_command(nas, pdu_buffer, pdu_length);
            break;
          case FGS_REGISTRATION_REJECT:
            handle_registration_reject(nas, &buffer);
            break;
          case FGS_SERVICE_ACCEPT: {
            handle_service_accept(nas, &buffer);
            break;
          }

          case FGS_SERVICE_REJECT: {
            byte_array_t buffer = {.buf = pdu_buffer, .len = pdu_length};
            handle_service_reject(nas, &buffer);
            break;
          }

          default:
            LOG_W(NR_RRC, "unknown message type %d\n", msg_type);
            break;
        }
        // Free NAS buffer memory after use (coming from RRC)
        free(pdu_buffer);

        if (initialNasMsg.length > 0)
          send_nas_uplink_data_req(nas, &initialNasMsg);
      } break;

      case NAS_INIT_NOS1_IF: {
        const int pdu_session_id = DEFAULT_NOS1_PDU_ID;
        const char *ip = "10.0.1.2";
        const int qfi = 7;
        AssertFatal(pdu_session_id > 0 && pdu_session_id < MAX_NUM_PSI, "invalid PDU session ID %d\n", pdu_session_id);
        nas_ue_pdu_tun_t *t = &nas->pdu_tun[pdu_session_id];
        nr_ue_tun_store_qfi(t, qfi);
        tuntap_generate_ue_ifname(t->ifname, IFF_TUN, nas->UE_id, -1);
        nr_ue_tun_create_ip_if(t, ip, NULL, nas->UE_id, pdu_session_id);
        DevAssert(t->sock >= 0);
        nas->psi_status[pdu_session_id] = PDU_SESSION_ACTIVE;
        send_nas_tun_req(nas, pdu_session_id);
        break;
      }

      default:
        LOG_E(NAS, "[UE %ld] Received unexpected message %s\n", nas->UE_id, ITTI_MSG_NAME(msg_p));
        break;
    }

    int result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  }
  return NULL;
}

void nas_init_nrue(int num_ues) {
  for (int i = 0; i < num_ues; i++) {
    (void)get_ue_nas_info(i);
  }
}
