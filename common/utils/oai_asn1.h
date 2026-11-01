/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef ASN1_CONVERSIONS_H_
#define ASN1_CONVERSIONS_H_

#include "BIT_STRING.h"
#include "assertions.h"

//-----------------------begin func -------------------

// Macro updates DESTINATION with configuration from ORIGIN by swapping pointers
// Old configuration is freed after completing configuration
#define UPDATE_IE(DESTINATION, ORIGIN, TYPE)     \
  do {                                           \
    TYPE *tmp = ORIGIN;                          \
    ORIGIN = DESTINATION;                        \
    DESTINATION = tmp;                           \
  } while (0)                                    \

// Same as above but swapping ASN1 elements that are not pointers
#define UPDATE_NP_IE(DESTINATION, ORIGIN, TYPE)     \
  do {                                              \
    TYPE tmp = ORIGIN;                              \
    ORIGIN = DESTINATION;                           \
    DESTINATION = tmp;                              \
  } while (0)                                       \

// Macro handles reception of SetupRelease element ORIGIN (see NR_SetupRelease.h)
// If release (NR_SetupRelease_xxx_PR_release equivalent to 1), removing structure from DESTINATION
// If setup (NR_SetupRelease_xxx_PR_setup equivalent to 2), add or modify structure in DESTINATION
// Destination is not a SetupRelease structure
#define HANDLE_SETUPRELEASE_DIRECT(DESTINATION, ORIGIN, TYPE, ASN_DEF) \
  do {                                                                 \
    if (ORIGIN->present == 1) {                                        \
      asn1cFreeStruc(ASN_DEF, DESTINATION);                            \
    }                                                                  \
    if (ORIGIN->present == 2)                                          \
      UPDATE_IE(DESTINATION, ORIGIN->choice.setup, TYPE);              \
  } while (0)                                                          \

// Macro handles reception of SetupRelease element ORIGIN (see NR_SetupRelease.h)
// If release (NR_SetupRelease_xxx_PR_release equivalent to 1), removing structure from DESTINATION
// If setup (NR_SetupRelease_xxx_PR_setup equivalent to 2), add or modify structure in DESTINATION
// Destination is a SetupRelease structure
#define HANDLE_SETUPRELEASE_IE(DESTINATION, ORIGIN, TYPE, ASN_DEF)          \
  do {                                                                      \
    if (ORIGIN->present == 1) {                                             \
      asn1cFreeStruc(ASN_DEF, DESTINATION);                                 \
    }                                                                       \
    if (ORIGIN->present == 2) {                                             \
      if (!DESTINATION)                                                     \
        DESTINATION = calloc(1, sizeof(*DESTINATION));                      \
      DESTINATION->present = ORIGIN->present;                               \
      UPDATE_IE(DESTINATION->choice.setup, ORIGIN->choice.setup, TYPE);     \
    }                                                                       \
  } while (0)                                                               \

// Macro releases entries in list TARGET if the corresponding ID is found in list SOURCE.
// Prints an error if ID not found in list.
#define RELEASE_IE_FROMLIST(SOURCE, TARGET, FIELD)                                 \
  do {                                                                             \
    if (!TARGET) {                                                                 \
      LOG_E(NR_MAC, "Target list not present, impossible to release element\n");   \
      break;                                                                       \
    }                                                                              \
    for (int iI = 0; iI < SOURCE->list.count; iI++) {                              \
      long eL = *SOURCE->list.array[iI];                                           \
      int iJ;                                                                      \
      for (iJ = 0; iJ < TARGET->list.count; iJ++) {                                \
        if (eL == TARGET->list.array[iJ]->FIELD)                                   \
          break;                                                                   \
      }                                                                            \
      if (iJ < TARGET->list.count)                                                 \
        asn_sequence_del(&TARGET->list, iJ, 1);                                    \
      else                                                                         \
        LOG_E(NR_MAC, "Element not present in the list, impossible to release\n"); \
    }                                                                              \
  } while (0)                                                                      \

// Macro adds or modifies entries of type TYPE in list TARGET with elements received in list SOURCE
#define ADDMOD_IE_FROMLIST(SOURCE, TARGET, FIELD, TYPE) \
  do {                                                  \
    for (int iI = 0; iI < SOURCE->list.count; iI++) {   \
      long eL = SOURCE->list.array[iI]->FIELD;          \
      int iJ;                                           \
      for (iJ = 0; iJ < TARGET->list.count; iJ++) {     \
        if (eL == TARGET->list.array[iJ]->FIELD)        \
          break;                                        \
      }                                                 \
      if (iJ == TARGET->list.count) {                   \
        TYPE *nEW = calloc(1, sizeof(*nEW));            \
        ASN_SEQUENCE_ADD(&TARGET->list, nEW);           \
      }                                                 \
      UPDATE_IE(TARGET->list.array[iJ],                 \
                SOURCE->list.array[iI],                 \
                TYPE);                                  \
    }                                                   \
  } while (0)                                           \

// Macro adds or modifies entries of type TYPE in list TARGET with elements received in list SOURCE
// Action performed by function FUNC
#define ADDMOD_IE_FROMLIST_WFUNCTION(SOURCE, TARGET, FIELD, TYPE, FUNC) \
  do {                                                                  \
    for (int iI = 0; iI < SOURCE->list.count; iI++) {                   \
      long eL = SOURCE->list.array[iI]->FIELD;                          \
      int iJ;                                                           \
      for (iJ = 0; iJ < TARGET->list.count; iJ++) {                     \
        if (eL == TARGET->list.array[iJ]->FIELD)                        \
          break;                                                        \
      }                                                                 \
      if (iJ == TARGET->list.count) {                                   \
        TYPE *nEW = calloc(1, sizeof(*nEW));                            \
        ASN_SEQUENCE_ADD(&TARGET->list, nEW);                           \
      }                                                                 \
      FUNC(TARGET->list.array[iJ],                                      \
           SOURCE->list.array[iI]);                                     \
    }                                                                   \
  } while (0)

/*! \fn uint8_t BIT_STRING_to_uint8(BIT_STRING_t *)
 *\brief  This function extract at most a 8 bits value from a BIT_STRING_t object, the exact bits number depend on the BIT_STRING_t contents.
 *\param[in] pointer to the BIT_STRING_t object.
 *\return the extracted value.
 */
static inline uint8_t BIT_STRING_to_uint8(const BIT_STRING_t *asn) {
  DevCheck ((asn->size == 1), asn->size, 0, 0);

  return asn->buf[0] >> asn->bits_unused;
}

/*! \fn uint16_t BIT_STRING_to_uint16(BIT_STRING_t *)
 *\brief  This function extract at most a 16 bits value from a BIT_STRING_t object, the exact bits number depend on the BIT_STRING_t contents.
 *\param[in] pointer to the BIT_STRING_t object.
 *\return the extracted value.
 */
static inline uint16_t BIT_STRING_to_uint16(const BIT_STRING_t *asn) {
  uint16_t result = 0;
  int index = 0;

  DevCheck ((asn->size > 0) && (asn->size <= 2), asn->size, 0, 0);

  if (asn->size == 2) {
    result |= asn->buf[index++] << (8 - asn->bits_unused);
  }
  result |= asn->buf[index] >> asn->bits_unused;

  return result;
}

/*! \fn uint32_t BIT_STRING_to_uint32(BIT_STRING_t *)
 *\brief  This function extract at most a 32 bits value from a BIT_STRING_t object, the exact bits number depend on the BIT_STRING_t contents.
 *\param[in] pointer to the BIT_STRING_t object.
 *\return the extracted value.
 */
static inline uint32_t BIT_STRING_to_uint32(const BIT_STRING_t *asn) {
  uint32_t result = 0;
  size_t index;

  DevCheck ((asn->size > 0) && (asn->size <= 4), asn->size, 0, 0);

  int shift = ((asn->size - 1) * 8) - asn->bits_unused;
  for (index = 0; index < (asn->size - 1); index++) {
    result |= asn->buf[index] << shift;
    shift -= 8;
  }

  result |= asn->buf[index] >> asn->bits_unused;

  return result;
}

/*! \fn uint64_t BIT_STRING_to_uint64(BIT_STRING_t *)
 *\brief  This function extract at most a 64 bits value from a BIT_STRING_t object, the exact bits number depend on the BIT_STRING_t contents.
 *\param[in] pointer to the BIT_STRING_t object.
 *\return the extracted value.
 */
static inline uint64_t BIT_STRING_to_uint64(const BIT_STRING_t *asn) {
  uint64_t result = 0;
  size_t index;
  int shift;

  DevCheck ((asn->size > 0) && (asn->size <= 8), asn->size, 0, 0);

  shift = ((asn->size - 1) * 8) - asn->bits_unused;
  for (index = 0; index < (asn->size - 1); index++) {
    result |= ((uint64_t)asn->buf[index]) << shift;
    shift -= 8;
  }

  result |= ((uint64_t)asn->buf[index]) >> asn->bits_unused;

  return result;
}

#define asn1cSeqAdd(VaR, PtR) if (ASN_SEQUENCE_ADD(VaR,PtR)!=0) AssertFatal(false, "ASN.1 encoding error " #VaR "\n")
#define asn1cCallocOne(VaR, VaLue) \
  do {                             \
    VaR = calloc(1,sizeof(*VaR));  \
    *VaR = VaLue;                  \
  } while (0)
#define asn1cCalloc(VaR, lOcPtr) \
  typeof(VaR) lOcPtr = VaR = calloc(1,sizeof(*VaR))
#define asn1cSequenceAdd(VaR, TyPe, lOcPtr) \
  TyPe *lOcPtr= calloc(1,sizeof(TyPe)); \
  asn1cSeqAdd(&VaR,lOcPtr)
#define asn1cFreeStruc(ASN_DEF, STRUCT) \
  do {                                  \
    ASN_STRUCT_RESET(ASN_DEF, STRUCT);  \
    free(STRUCT);                       \
    STRUCT = NULL;                      \
  } while (0)

#endif
