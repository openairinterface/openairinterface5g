/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef XNAP_LIB_COMMON_H_
#define XNAP_LIB_COMMON_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"
#include "common/utils/assertions.h"
#include "common/utils/eq_check.h"
#include "common/utils/LOG/log.h"
#include "xnap_messages_types.h"
#include "xnap_lib_includes.h"

/* macro to look up XnAP IE. If mandatory and not found, macro will print
 * descriptive debug message to stderr and force exit in calling function */
#define XNAP_LIB_FIND_IE(IE_TYPE, ie, IE_LIST, IE_ID, mandatory)                                     \
  do {                                                                                               \
    ie = NULL;                                                                                       \
    for (IE_TYPE **ptr = (IE_LIST)->array; ptr < &(IE_LIST)->array[(IE_LIST)->count]; ptr++) {       \
      if ((*ptr)->id == IE_ID) {                                                                     \
        ie = *ptr;                                                                                   \
        break;                                                                                       \
      }                                                                                              \
    }                                                                                                \
    if ((mandatory) && (ie == NULL)) {                                                               \
      fprintf(stderr, "%s(): could not find XnAP IE " #IE_ID " of type " #IE_TYPE "\n", __func__);   \
      return false;                                                                                  \
    }                                                                                                \
  } while (0)

/* Function Prototypes */
bool eq_transport_layer_addr(const transport_layer_addr_t *a, const transport_layer_addr_t *b);
bool eq_xnap_guami(const nr_guami_t *a, const nr_guami_t *b);
bool eq_xnap_ngran_cgi(const xnap_ngran_cgi_t *a, const xnap_ngran_cgi_t *b);
bool eq_gtpu_tunnel(const gtpu_tunnel_t *a, const gtpu_tunnel_t *b);
bool eq_xnap_plmn(const plmn_id_t *a, const plmn_id_t *b);
bool eq_xnap_snssai(const nssai_t *a, const nssai_t *b);
bool eq_xnap_tai_support(const xnap_tai_support_t *a, const xnap_tai_support_t *b);
bool eq_xnap_plmn_support(const xnap_plmn_support_t *a, const xnap_plmn_support_t *b);
bool eq_xnap_cause(const xnap_cause_t *a, const xnap_cause_t *b);

int xnap_gNB_set_cause(XNAP_Cause_t *cause_p,const xnap_cause_t *in);
xnap_cause_t decode_xnap_cause(const XNAP_Cause_t *in);

XNAP_S_NSSAI_t xnap_encode_snssai(const nssai_t *in);
bool decode_xnap_snssai(const XNAP_S_NSSAI_t *in, nssai_t *out);

XNAP_Target_CGI_t xnap_encode_target_cgi(const xnap_ngran_cgi_t *in);
bool xnap_decode_target_cgi(const XNAP_Target_CGI_t *in, xnap_ngran_cgi_t *out);

XNAP_GUAMI_t encode_xnap_guami(const nr_guami_t *in);
nr_guami_t decode_xnap_guami(const XNAP_GUAMI_t *in);

#endif /* XNAP_LIB_COMMON_H_ */
