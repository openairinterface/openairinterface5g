/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "rrc_gNB_security.h"
#include "common/utils/LOG/log.h"

/* Masks for NR Encryption algorithms (NGAP/XnAP), NEA0 is always supported (not coded) */
static const uint16_t NR_ENCRYPTION_NEA1_MASK = 0x8000;
static const uint16_t NR_ENCRYPTION_NEA2_MASK = 0x4000;
static const uint16_t NR_ENCRYPTION_NEA3_MASK = 0x2000;

/* Masks for NR Integrity algorithms (NGAP/XnAP), NIA0 is always supported (not coded) */
static const uint16_t NR_INTEGRITY_NIA1_MASK = 0x8000;
static const uint16_t NR_INTEGRITY_NIA2_MASK = 0x4000;
static const uint16_t NR_INTEGRITY_NIA3_MASK = 0x2000;

NR_CipheringAlgorithm_t rrc_gNB_select_ciphering(const gNB_RRC_INST *rrc, uint16_t algorithms)
{
  int i;
  /* preset nea0 as fallback */
  int ret = 0;

  /* Select ciphering algorithm based on gNB configuration file and
   * UE's supported algorithms.
   * We take the first from the list that is supported by the UE.
   * The ordering of the list comes from the configuration file.
   */
  for (i = 0; i < rrc->security.ciphering_algorithms_count; i++) {
    int nea_mask[4] = {
      0,
      NR_ENCRYPTION_NEA1_MASK,
      NR_ENCRYPTION_NEA2_MASK,
      NR_ENCRYPTION_NEA3_MASK
    };
    if (rrc->security.ciphering_algorithms[i] == 0) {
      /* nea0 */
      break;
    }
    if (algorithms & nea_mask[rrc->security.ciphering_algorithms[i]]) {
      ret = rrc->security.ciphering_algorithms[i];
      break;
    }
  }

  LOG_D(RRC, "selecting ciphering algorithm %d\n", ret);

  return ret;
}

e_NR_IntegrityProtAlgorithm rrc_gNB_select_integrity(const gNB_RRC_INST *rrc, uint16_t algorithms)
{
  int i;
  /* preset nia0 as fallback */
  int ret = 0;

  /* Select integrity algorithm based on gNB configuration file and
   * UE's supported algorithms.
   * We take the first from the list that is supported by the UE.
   * The ordering of the list comes from the configuration file.
   */
  for (i = 0; i < rrc->security.integrity_algorithms_count; i++) {
    int nia_mask[4] = {
      0,
      NR_INTEGRITY_NIA1_MASK,
      NR_INTEGRITY_NIA2_MASK,
      NR_INTEGRITY_NIA3_MASK
    };
    if (rrc->security.integrity_algorithms[i] == 0) {
      /* nia0 */
      break;
    }
    if (algorithms & nia_mask[rrc->security.integrity_algorithms[i]]) {
      ret = rrc->security.integrity_algorithms[i];
      break;
    }
  }

  LOG_D(RRC, "selecting integrity algorithm %d\n", ret);

  return ret;
}
