/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef RRC_GNB_SECURITY_H_
#define RRC_GNB_SECURITY_H_

#include <stdint.h>
#include "nr_rrc_defs.h"

/* Select the AS security algorithms from the gNB configuration and the UE's NR
 * encryption/integrity algorithms bitmaps, as received over NGAP or XnAP */
NR_CipheringAlgorithm_t rrc_gNB_select_ciphering(const gNB_RRC_INST *rrc, uint16_t algorithms);
e_NR_IntegrityProtAlgorithm rrc_gNB_select_integrity(const gNB_RRC_INST *rrc, uint16_t algorithms);

#endif /* RRC_GNB_SECURITY_H_ */
