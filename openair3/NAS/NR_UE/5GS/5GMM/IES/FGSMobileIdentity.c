/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "TLVEncoder.h"
#include "TLVDecoder.h"
#include "FGSMobileIdentity.h"

#define MIN_LEN_FGS_MOBILE_ID_IE 3 // type of identity "No identity"
#define LEN_5G_FGS_TMSI_IE 9
#define LEN_5G_GUTI_CONTENTS 11 // octets 4 to 14

static int decode_guti_5gs_mobile_identity(Guti5GSMobileIdentity_t *guti, const uint8_t *buffer);

static int encode_guti_5gs_mobile_identity(const Guti5GSMobileIdentity_t *guti, uint8_t *buffer);
static int encode_suci_5gs_mobile_identity(const Suci5GSMobileIdentity_t *suci, uint8_t *buffer, uint32_t len);
static int encode_imeisv_5gs_mobile_identity(const Imeisv5GSMobileIdentity_t *imeisv, uint8_t *buffer);

int encode_stmsi_5gs_mobile_identity(uint8_t *buffer, const Stmsi5GSMobileIdentity_t *stmsi, uint32_t len)
{
  // Return if buffer is shorter than min length
  if (len < LEN_5G_FGS_TMSI_IE)
    return -1;

  uint32_t encoded = 0;
  // octet 4
  *(buffer + encoded) = (DIGIT1 << 4) | ((stmsi->spare & 0x1) << 3) | (stmsi->typeofidentity & 0x7);
  encoded++;
  // octet 5-6
  uint16_t temp = 0x00 | ((stmsi->amfsetid) << 6) | (stmsi->amfpointer & 0x3f);
  IES_ENCODE_U16(buffer, encoded, temp);
  // octet 7
  IES_ENCODE_U32(buffer, encoded, stmsi->tmsi);
  return encoded;
}

int decode_stmsi_5gs_mobile_identity(Stmsi5GSMobileIdentity_t *stmsi, const uint8_t *buffer, uint32_t len)
{
  int decoded = 0;
  uint16_t temp;

  // Return if buffer is shorter than min length
  if (len < LEN_5G_FGS_TMSI_IE)
    return -1;

  // Octet 4: spare (bit 3) and type of identity (bits 0-2)
  stmsi->digit1 = DIGIT1;
  stmsi->spare = (*(buffer + decoded) >> 3) & 0x1;
  stmsi->typeofidentity = *(buffer + decoded) & 0x7;

  // Validate type of identity
  if (stmsi->typeofidentity != FGS_MOBILE_IDENTITY_5GS_TMSI) {
    return -1;
  }

  decoded++;

  // Octet 5-6: AMF Set ID (bits 6-15) and AMF Pointer (bits 0-5)
  IES_DECODE_U16(buffer, decoded, temp);
  stmsi->amfsetid = (temp >> 6) & 0x03ff;
  stmsi->amfpointer = temp & 0x3f;

  // Octet 7-10: 32-bit TMSI
  IES_DECODE_U32(buffer, decoded, stmsi->tmsi);

  return decoded;
}

int decode_5gs_mobile_identity(FGSMobileIdentity *fgsmobileidentity, uint8_t iei, const uint8_t *buffer, uint32_t len)
{
  int decoded_rc = TLV_DECODE_VALUE_DOESNT_MATCH;
  int decoded = 0;
  uint16_t ielen = 0;

  // IEI (if any), 2 octets of length and the type of identity (TS 24.501 §9.11.3.4)
  CHECK_LENGTH_DECODER(len, (iei > 0) + MIN_LEN_FGS_MOBILE_ID_IE);
  if (iei > 0) {
    CHECK_IEI_DECODER(iei, *buffer);
    decoded++;
  }
  IES_DECODE_U16(buffer, decoded, ielen);
  CHECK_LENGTH_DECODER(len - decoded, ielen);
  CHECK_LENGTH_DECODER(ielen, 1); // type of identity

  uint8_t typeofidentity = *(buffer + decoded) & 0x7;

  AssertFatal(typeofidentity == FGS_MOBILE_IDENTITY_5G_GUTI, "Mobile Identity encoding of type %d not implemented\n", typeofidentity);
  if (typeofidentity == FGS_MOBILE_IDENTITY_5G_GUTI) {
    CHECK_LENGTH_DECODER(ielen, LEN_5G_GUTI_CONTENTS);
    decoded_rc = decode_guti_5gs_mobile_identity(&fgsmobileidentity->guti, buffer + decoded);
  }

  if (decoded_rc < 0) {
    return decoded_rc;
  }

  return (decoded + decoded_rc);
}

int encode_5gs_mobile_identity(const FGSMobileIdentity *fgsmobileidentity, uint8_t iei, uint8_t *buffer, uint32_t len)
{
  // Return if buffer is shorter than min length
  if (len < MIN_LEN_FGS_MOBILE_ID_IE)
    return -1;

  int encoded_rc = TLV_ENCODE_VALUE_DOESNT_MATCH;
  uint32_t encoded = 0;

  if (iei > 0) {
    *buffer = iei;
    encoded++;
  }

  encoded = encoded + 2;
  len -= encoded;

  if (fgsmobileidentity->guti.typeofidentity == FGS_MOBILE_IDENTITY_5G_GUTI) {
    encoded_rc = encode_guti_5gs_mobile_identity(&fgsmobileidentity->guti, buffer + encoded);
  }

  if (fgsmobileidentity->suci.typeofidentity == FGS_MOBILE_IDENTITY_SUCI) {
    encoded_rc = encode_suci_5gs_mobile_identity(&fgsmobileidentity->suci, buffer + encoded, len);
  }

  if (fgsmobileidentity->imeisv.typeofidentity == FGS_MOBILE_IDENTITY_IMEISV) {
    encoded_rc = encode_imeisv_5gs_mobile_identity(&fgsmobileidentity->imeisv, buffer + encoded);
  }

  if (fgsmobileidentity->stmsi.typeofidentity == FGS_MOBILE_IDENTITY_5GS_TMSI) {
    encoded_rc = encode_stmsi_5gs_mobile_identity(buffer + encoded, &fgsmobileidentity->stmsi, len);
  }

  if (encoded_rc < 0) {
    return encoded_rc;
  }

  if (iei > 0) {
    uint16_t tmp = htons(encoded + encoded_rc - 3);
    memcpy(buffer + 1, &tmp, sizeof(tmp));
  } else {
    uint16_t tmp = htons(encoded + encoded_rc - 2);
    memcpy(buffer, &tmp, sizeof(tmp));
  }

  return (encoded + encoded_rc);
}

static int decode_guti_5gs_mobile_identity(Guti5GSMobileIdentity_t *guti, const uint8_t *buffer)
{
  int decoded = 0;
  uint16_t temp;
  guti->spare = (*(buffer + decoded) >> 4) & 0xf;

  /*
   * For the 5G-GUTI, bits 5 to 8 of octet 3 are coded as "1111"
   */
  if (guti->spare != 0xf) {
    return (TLV_ENCODE_VALUE_DOESNT_MATCH);
  }

  guti->oddeven = (*(buffer + decoded) >> 3) & 0x1;

  /*
   * For the 5G-GUTI, bits 4 of octet 3 are coded as "0"
   */
  if (guti->oddeven != 0) {
    return (TLV_ENCODE_VALUE_DOESNT_MATCH);
  }
  guti->typeofidentity = *(buffer + decoded) & 0x7;

  if (guti->typeofidentity != FGS_MOBILE_IDENTITY_5G_GUTI) {
    return (TLV_ENCODE_VALUE_DOESNT_MATCH);
  }

  decoded++;
  guti->mccdigit2 = (*(buffer + decoded) >> 4) & 0xf;
  guti->mccdigit1 = *(buffer + decoded) & 0xf;
  decoded++;
  guti->mncdigit3 = (*(buffer + decoded) >> 4) & 0xf;
  guti->mccdigit3 = *(buffer + decoded) & 0xf;
  decoded++;
  guti->mncdigit2 = (*(buffer + decoded) >> 4) & 0xf;
  guti->mncdigit1 = *(buffer + decoded) & 0xf;
  decoded++;

  guti->amfregionid = *(buffer + decoded);
  decoded++;
  IES_DECODE_U16(buffer, decoded, temp);
  guti->amfsetid = (temp >> 6) & 0x3FF;
  guti->amfpointer = temp & 0x3f;

  IES_DECODE_U32(buffer, decoded, guti->tmsi);
  return decoded;
}

static int encode_guti_5gs_mobile_identity(const Guti5GSMobileIdentity_t *guti, uint8_t *buffer)
{
  uint32_t encoded = 0;
  uint16_t temp;
  *(buffer + encoded) = 0xf0 | ((guti->oddeven & 0x1) << 3) | (guti->typeofidentity & 0x7);
  encoded++;
  *(buffer + encoded) = 0x00 | ((guti->mccdigit2 & 0xf) << 4) | (guti->mccdigit1 & 0xf);
  encoded++;
  *(buffer + encoded) = 0x00 | ((guti->mncdigit3 & 0xf) << 4) | (guti->mccdigit3 & 0xf);
  encoded++;
  *(buffer + encoded) = 0x00 | ((guti->mncdigit2 & 0xf) << 4) | (guti->mncdigit1 & 0xf);
  encoded++;

  *(buffer + encoded) = guti->amfregionid;
  encoded++;

  temp = 0x00 | ((guti->amfsetid) << 6) | (guti->amfpointer & 0x3f);

  IES_ENCODE_U16(buffer, encoded, temp);
  IES_ENCODE_U32(buffer, encoded, guti->tmsi);
  return encoded;
}

static int encode_suci_5gs_mobile_identity(const Suci5GSMobileIdentity_t *suci, uint8_t *buffer, uint32_t len)
{
  uint32_t encoded = 0;

  if (len < 8)
    return -1;

  *(buffer + encoded) = (suci->supiformat << 4) | (suci->typeofidentity);
  encoded++;
  *(buffer + encoded) = ((suci->mccdigit2 & 0xf) << 4) | (suci->mccdigit1 & 0xf);
  encoded++;
  *(buffer + encoded) = ((suci->mncdigit3 & 0xf) << 4) | (suci->mccdigit3 & 0xf);
  encoded++;
  *(buffer + encoded) = ((suci->mncdigit2 & 0xf) << 4) | (suci->mncdigit1 & 0xf);
  encoded++;

  *(buffer + encoded) = ((suci->routingindicatordigit2 & 0xf) << 4) | (suci->routingindicatordigit1 & 0xf);
  encoded++;

  *(buffer + encoded) = ((suci->routingindicatordigit4 & 0xf) << 4) | (suci->routingindicatordigit3 & 0xf);
  encoded++;

  *(buffer + encoded) = 0x00 | (suci->protectionschemeId & 0xf);
  encoded++;

  *(buffer + encoded) = suci->homenetworkpki;
  encoded++;

  DevAssert(suci->schemeoutput);
  size_t rawlen = strlen(suci->schemeoutput);
  size_t outlen = (rawlen + 1) / 2;

  if (len - encoded < outlen)
    return -1;

  if (suci->protectionschemeId == 0) {
    AssertFatal(suci->homenetworkpki == 0,
                "Invalid SUCI: homenetworkpki=%u with null protection scheme (must be 0)\n",
                suci->homenetworkpki);
    int rc = digit_string_to_bcd_value(buffer + encoded, suci->schemeoutput, outlen);
    AssertFatal(rc == 0,
                "BCD encoding failed (rc=%d, input=\"%s\", len=%zu, out_len=%zu)\n",
                rc,
                suci->schemeoutput,
                rawlen,
                outlen);
  } else {
    int rc = hex_string_to_hex_value(buffer + encoded, suci->schemeoutput, outlen);
    AssertFatal(rc == 0,
                "Encoding SUCI schemeoutput failed (rc=%d, input=\"%s\", len=%zu, out_len=%zu)\n",
                rc,
                suci->schemeoutput,
                rawlen,
                outlen);
  }
  encoded += outlen;
  return encoded;
}

static int encode_imeisv_5gs_mobile_identity(const Imeisv5GSMobileIdentity_t *imeisv, uint8_t *buffer)
{
  uint32_t encoded = 0;
  *(buffer + encoded) = 0x00 | (imeisv->digittac01 << 4) | (imeisv->oddeven << 3) | (imeisv->typeofidentity);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digittac03 << 4) | (imeisv->digittac02);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digittac05 << 4) | (imeisv->digittac04);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digittac07 << 4) | (imeisv->digittac06);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digit09 << 4) | (imeisv->digittac08);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digit11 << 4) | (imeisv->digit10);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digit13 << 4) | (imeisv->digit12);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->digitsv1 << 4) | (imeisv->digit14);
  encoded++;
  *(buffer + encoded) = 0x00 | (imeisv->spare << 4) | (imeisv->digitsv2);
  encoded++;

  return encoded;
}
