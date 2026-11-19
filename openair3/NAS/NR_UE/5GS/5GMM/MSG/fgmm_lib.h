/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * This header file defines structures, macros,
 * and functions for the handling of common NAS 5GMM IEs.
 * This library is intended for use within the 5GMM encode/decode library only.
 */

#ifndef FGS_LIB_H
#define FGS_LIB_H

#include <stdint.h>
#include <stdbool.h>
#include "fgs_nas_utils.h"
#include "common/utils/ds/byte_array.h"
#include "common/utils/utils.h"

/* The following defines are valide for
both PDU Session Status and PDU session Reactivation Result IEs */
#define MIN_PDU_SESSION_CONTENTS_LEN 2
#define MAX_PDU_SESSION_CONTENTS_LEN 32
#define MAX_NUM_PSI 16
#define MAX_NUM_PDU_ERRORS 256
#define MAX_EAP_CONTENTS_LEN 1500

/* This macro updates the local copy of
   a byte_array_t pointer used for enc/dec */
#define UPDATE_BYTE_ARRAY(ba, shift) \
  do {                               \
    (ba).buf += (shift);             \
    (ba).len -= (shift);             \
  } while (0)

/* This macro checks the result of the encoder.
   If successful, it updates the encoded bytes counter
   and the local copy of the byte_array_t pointer.
   Otherwise, it returns -1. */
#define ENCODE_NAS_IE(ba, exp, encoded) \
  do {                                  \
    int ret = (exp);                    \
    if (ret < 0) {                      \
      return -1;                        \
    }                                   \
    (encoded) += ret;                   \
    UPDATE_BYTE_ARRAY((ba), ret);       \
  } while (0)

/* This macro checks the result of the decoder.
   If successful, it updates the decoded bytes counter
   and the local copy of the byte_array_t pointer.
   Otherwise, it returns -1. */
#define DECODE_NAS_IE(ba, exp, decoded) \
  do {                                  \
    int ret = (exp);                    \
    if (ret < 0) {                      \
      return -1;                        \
    }                                   \
    (decoded) += ret;                   \
    UPDATE_BYTE_ARRAY((ba), ret);       \
  } while (0)

/* Timer value is incremented in multiples of 2 seconds,
   1 minute, etc (10.5.7.3 of 3GPP TS 24.008) */
#define FOREACH_TIMER_UNIT(TIMER_UNIT) \
  TIMER_UNIT(TWO_SECONDS, 0x00)        \
  TIMER_UNIT(ONE_MINUTE, 0x01)         \
  TIMER_UNIT(DECIHOURS, 0x02)          \
  TIMER_UNIT(DEACTIVATED, 0x07)

typedef enum { FOREACH_TIMER_UNIT(TO_ENUM) } GPRS_Timer_t;

typedef enum { PDU_SESSION_INACTIVE = 0, PDU_SESSION_ACTIVE } PSI_status_t;

typedef enum { REACTIVATION_SUCCESS = 0, REACTIVATION_FAILED } PSI_reactivation_t;

#define FOREACH_SA_IEI(IEI_DEF)                           \
  IEI_DEF(IEI_ALLOWED_PDU_SESSION_STATUS, 0x25)           \
  IEI_DEF(IEI_PDU_SESSION_REACT_RESULT, 0x26)             \
  IEI_DEF(IEI_PAGING_RESTRICTION, 0x28)                   \
  IEI_DEF(IEI_UE_REQUEST_TYPE, 0x29)                      \
  IEI_DEF(IEI_UPLINK_DATA_STATUS, 0x40)                   \
  IEI_DEF(IEI_PDU_SESSION_STATUS, 0x50)                   \
  IEI_DEF(IEI_T3446_VALUE, 0x5F)                          \
  IEI_DEF(IEI_T3448_VALUE, 0x6B)                          \
  IEI_DEF(IEI_FGS_NAS_MESSAGE_CONTAINER, 0x71)            \
  IEI_DEF(IEI_PDU_SESSION_REACT_RESULT_ERROR_CAUSE, 0x72) \
  IEI_DEF(IEI_CAG_INFO_LIST, 0x75)                        \
  IEI_DEF(IEI_EAPMSG, 0x78)

// Enum for Service Request procedure IEIs
typedef enum { FOREACH_SA_IEI(TO_ENUM) } nas_service_IEI_t;

static const text_info_t sa_iei_s[] = {FOREACH_SA_IEI(TO_TEXT)};

// table  9.11.3.2.1
#define FOREACH_CAUSE(CAUSE_DEF)                                                \
  CAUSE_DEF(Illegal_UE, 0x3)                                                    \
  CAUSE_DEF(PEI_not_accepted, 0x5)                                              \
  CAUSE_DEF(Illegal_ME, 0x6)                                                    \
  CAUSE_DEF(SGS_services_not_allowed, 0x7)                                      \
  CAUSE_DEF(UE_identity_cannot_be_derived_by_the_network, 0x9)                  \
  CAUSE_DEF(Implicitly_de_registered, 0x0a)                                     \
  CAUSE_DEF(PLMN_not_allowed, 0x0b)                                             \
  CAUSE_DEF(Tracking_area_not_allowed, 0x0c)                                    \
  CAUSE_DEF(Roaming_not_allowed_in_this_tracking_area, 0x0d)                    \
  CAUSE_DEF(No_suitable_cells_in_tracking_area, 0x0f)                           \
  CAUSE_DEF(MAC_failure, 0x14)                                                  \
  CAUSE_DEF(Synch_failure, 0x15)                                                \
  CAUSE_DEF(Congestion, 0x16)                                                   \
  CAUSE_DEF(UE_security_capabilities_mismatch, 0x17)                            \
  CAUSE_DEF(Security_mode_rejected_unspecified, 0x18)                           \
  CAUSE_DEF(Non_5G_authentication_unacceptable, 0x1a)                           \
  CAUSE_DEF(N1_mode_not_allowed, 0x1b)                                          \
  CAUSE_DEF(Restricted_service_area, 0x1c)                                      \
  CAUSE_DEF(Redirection_to_EPC_required, 0x1f)                                  \
  CAUSE_DEF(LADN_not_available, 0x2b)                                           \
  CAUSE_DEF(No_network_slices_available, 0x3e)                                  \
  CAUSE_DEF(Maximum_number_of_PDU_sessions_reached, 0x41)                       \
  CAUSE_DEF(Insufficient_resources_for_specific_slice_and_DNN, 0x43)            \
  CAUSE_DEF(Insufficient_resources_for_specific_slice, 0x45)                    \
  CAUSE_DEF(ngKSI_already_in_use, 0x47)                                         \
  CAUSE_DEF(Non_3GPP_access_to_5GCN_not_allowed, 0x48)                          \
  CAUSE_DEF(Serving_network_not_authorized, 0x49)                               \
  CAUSE_DEF(Temporarily_not_authorized_for_this_SNPN, 0x4A)                     \
  CAUSE_DEF(Permanently_not_authorized_for_this_SNPN, 0x4b)                     \
  CAUSE_DEF(Not_authorized_for_this_CAG_or_authorized_for_CAG_cells_only, 0x4c) \
  CAUSE_DEF(Wireline_access_area_not_allowed, 0x4d)                             \
  CAUSE_DEF(Payload_was_not_forwarded, 0x5a)                                    \
  CAUSE_DEF(DNN_not_supported_or_not_subscribed_in_the_slice, 0x5b)             \
  CAUSE_DEF(Insufficient_user_plane_resources_for_the_PDU_session, 0x5c)        \
  CAUSE_DEF(Semantically_incorrect_message, 0x5f)                               \
  CAUSE_DEF(Invalid_mandatory_information, 0x60)                                \
  CAUSE_DEF(Message_type_non_existent_or_not_implemented, 0x61)                 \
  CAUSE_DEF(Message_type_not_compatible_with_the_protocol_state, 0x62)          \
  CAUSE_DEF(Information_element_non_existent_or_not_implemented, 0x63)          \
  CAUSE_DEF(Conditional_IE_error, 0x64)                                         \
  CAUSE_DEF(Message_not_compatible_with_the_protocol_state, 0x65)               \
  CAUSE_DEF(Protocol_error_unspecified, 0x67)

typedef enum { FOREACH_CAUSE(TO_ENUM) } cause_id_t;

static const text_info_t fgmm_cause_s[] = {FOREACH_CAUSE(TO_TEXT)};

/* 10.5.7.3 3GPP TS 24.008 */
typedef struct {
  uint8_t value;
  uint8_t unit;
} gprs_timer_t;

int encode_pdu_session_ie(byte_array_t *buffer, nas_service_IEI_t iei, const uint8_t *psi);
int decode_pdu_session_ie(uint8_t *psi, const byte_array_t *buffer);
int encode_gprs_timer_ie(byte_array_t *buffer, nas_service_IEI_t iei, const gprs_timer_t *timer);
int decode_gprs_timer_ie(gprs_timer_t *timer, const byte_array_t *buffer);
bool eq_gprs_timer(const gprs_timer_t *a, const gprs_timer_t *b);
int decode_eap_msg_ie(byte_array_t *eap_msg, const byte_array_t *buffer);
int encode_eap_msg_ie(byte_array_t *buffer, const byte_array_t *msg);
int encode_fgs_nas_cause(byte_array_t *buffer, const cause_id_t *cause);
int decode_fgs_nas_cause(cause_id_t *cause, const byte_array_t *buffer);

#endif /* FGS_LIB_H */
