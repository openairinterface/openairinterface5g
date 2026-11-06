/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "openair2/GNB_APP/gnb_config_common.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/platform_constants.h"
#include "RRC_nr_paramsvalues.h"
#include "gnb_paramdef.h"

uint16_t set_snssai_config(nssai_t *nssai, const int max_num_ssi, uint8_t k, uint8_t l)
{
  char snssaistr[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(snssaistr, sizeof(snssaistr), "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, k, GNB_CONFIG_STRING_PLMN_LIST, l);
  GET_PARAMS_LIST(SNSSAIParamList,
                  SNSSAIParams,
                  GNBSNSSAIPARAMS_DESC,
                  GNB_CONFIG_STRING_SNSSAI_LIST,
                  snssaistr,
                  SNSSAIPARAMS_CHECK);
  uint16_t num_ssi = SNSSAIParamList.numelt;
  AssertFatal(num_ssi < max_num_ssi, "S-NSSAI size %d exceeds the max array size %d", num_ssi, max_num_ssi);
  for (int s = 0; s < num_ssi; ++s) {
    nssai[s].sst = *SNSSAIParamList.paramarray[s][GNB_SLICE_SERVICE_TYPE_IDX].uptr;
    // SD is optional
    // 0xffffff is "no SD", see 23.003 Sec 28.4.2
    nssai[s].sd = *SNSSAIParamList.paramarray[s][GNB_SLICE_DIFFERENTIATOR_IDX].uptr;
    AssertFatal(nssai[s].sd <= 0xffffff, "SD cannot be bigger than 0xffffff, but is %d\n", nssai[s].sd);
  }
  return num_ssi;
}

/** @brief Extract PLMN from parameter array
 * @param[in] params Parameter array containing PLMN fields
 * @return plmn_id_t structure filled from parameters */
plmn_id_t extract_plmn_from_params(const paramdef_t *params, int n_params)
{
  plmn_id_t plmn = {.mcc = *gpd(params, n_params, GNB_CONFIG_STRING_MOBILE_COUNTRY_CODE)->uptr,
                    .mnc = *gpd(params, n_params, GNB_CONFIG_STRING_MOBILE_NETWORK_CODE)->uptr,
                    .mnc_digit_length = *gpd(params, n_params, GNB_CONFIG_STRING_MNC_DIGIT_LENGTH)->uptr};
  AssertFatal((plmn.mnc_digit_length == 2) || (plmn.mnc_digit_length == 3), "BAD MNC DIGIT LENGTH %d", plmn.mnc_digit_length);
  return plmn;
}

uint8_t set_plmn_config(plmn_id_t *p, uint8_t idx)
{
  char gnbpath[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(gnbpath, sizeof(gnbpath), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, idx);
  GET_PARAMS_LIST(PLMNParamList, PLMNParams, GNBPLMNPARAMS_DESC, GNB_CONFIG_STRING_PLMN_LIST, gnbpath, PLMNPARAMS_CHECK);
  int num_plmn = PLMNParamList.numelt;
  const int n_plmn_params = sizeofArray(PLMNParams);
  AssertFatal(num_plmn >= 1 && num_plmn <= NR_MAX_NB_PLMNS,
              "The number of PLMN IDs must be in [1,%d], but is %d\n",
              NR_MAX_NB_PLMNS,
              num_plmn);
  for (int l = 0; l < num_plmn; ++l) {
    p[l] = extract_plmn_from_params(PLMNParamList.paramarray[l], n_plmn_params);
  }
  return num_plmn;
}
