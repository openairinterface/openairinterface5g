/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <assert.h>
#include <ctype.h>
#include <net/if.h>

#include "pdu_session.h"
// we can't include the file for PDU session type defines, as it pulls in too
// much. However, we only use it for mapping types to integers, which are
// standard defined, so it should be ok.
//#include "openair3/NAS/NR_UE/5GS/5GSM/MSG/PduSessionEstablishmentAccept.h"
#include "common/utils/utils.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"

// PDU session identity values 1..15 (TS 24.007 11.2.3.1b)
#define MAX_PDU_SESSION_ID 15

// clang-format off
// Note: Default PDU session ID 0 does not exist and will be replaced with PDU
// session index if not set to a parameter > 0!
#define PDU_SESS_PARAMS_DESC {                                              \
    {"id",        "PDU session ID\n",             .defintval=0,        .type=TYPE_INT,    .chkPptr=&(checkedparam_t){.s2 = {config_check_intrange, {0, MAX_PDU_SESSION_ID}}}}, \
    {"type",      "PDU session type\n",           .defstrval="IPv4",   .type=TYPE_STRING, \
      .chkPptr=&(checkedparam_t){.s3a = {config_checkstr_assign_integer, {"IPv4", "IPv6", "IPv4v6", "Ethernet"}, \
                                                                         {     1,      2,        3,          5},   4}}}, \
    {"nssai_sst", "NSSAI slice type\n",           .defintval=1,        .type=TYPE_UINT8,  .chkPptr=&(checkedparam_t){.s2 = {config_check_intrange, {0, 127}}}}, \
    {"nssai_sd",  "NSSAI slice differentiator\n", .defintval=0xffffff, .type=TYPE_UINT32, .chkPptr=&(checkedparam_t){.s2 = {config_check_intrange, {0, 0xffffff}}}}, \
    {"dnn",       "PDU session DNN (APN)\n",      .defstrval="oai",    .type=TYPE_STRING, .chkPptr=&(checkedparam_t){.s4 = {config_check_dnn}}}, \
    {"ifname",    "interface name\n",             .defstrval=NULL,     .type=TYPE_STRING, .chkPptr=&(checkedparam_t){.s4 = {config_check_ifname}}}, \
}
// clang-format on

static int config_check_dnn(configmodule_interface_t *cfg, paramdef_t *param)
{
  UNUSED(cfg);
  const char *dnn = *param->strptr;
  int dnn_len = strlen(dnn);
  if (dnn_len > 102) {
    printf("DNN '%s' exceeds max len of 102 (is %d) (TS 24.501 §9.11.2.1A)\n", dnn, dnn_len);
    return -1;
  }

  // check for valid characters in the APN (TS 23.003 §9.1)
  // we don't enforce all conditions
  const char *allowed = "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                        "0123456789.-";
  while (*dnn) {
    if (!strchr(allowed, *dnn)) {
      printf("character '%c' in DNN '%s' is not allowed (TS 23.003 §9.1)\n", *dnn, *param->strptr);
      return -1;
    }
    dnn++;
  }

  return 0;
}

static int config_check_ifname(configmodule_interface_t *cfg, paramdef_t *param)
{
  UNUSED(cfg);
  if (!param->strptr)
    return 0; // nothing provided, stack will auto-generate ifname
  const char *ifname = *param->strptr;
  int ifname_len = strlen(ifname);
  /* check if name validity similar to linux kernel */
  if (ifname_len >= IFNAMSIZ)
    return -1;
  if (strcmp(ifname, ".") == 0 || strcmp(ifname, "..") == 0)
    return -1;
  while(*ifname) {
    if (*ifname == '/' || *ifname == ':' || isspace(*ifname))
      return -1;
    ifname++;
  }
  return 0;
}

int get_pdu_session_configs(const char *uiccName, pdu_session_config_t *configs, int max_len)
{
  paramdef_t pdu_sess_params[] = PDU_SESS_PARAMS_DESC;
  const int npsp = sizeofArray(pdu_sess_params);
  paramlist_def_t pdu_sess_list = {"pdu_sessions"};
  int ret = config_getlist(config_get_if(), &pdu_sess_list, pdu_sess_params, sizeofArray(pdu_sess_params), uiccName);
  if (ret < 0) {
    // couldn't load configuration from pdu_sessions, caller has to handle
    return -1;
  }

  int num = pdu_sess_list.numelt;
  AssertFatal(num <= max_len, "cannot handle more than %d PDU sessions\n", max_len);

  for (int i = 0; i < num; ++i) {
    pdu_session_config_t *pdu = configs + i;
    paramdef_t *p = pdu_sess_list.paramarray[i];
    int id = *gpd(p, npsp, "id")->iptr;
    pdu->id = id > 0 ? id : i + 1; // if not user-specified ID, use PDU session index
    AssertFatal(pdu->id <= MAX_PDU_SESSION_ID, "PDU session %d: ID %d is above %d\n", i, pdu->id, MAX_PDU_SESSION_ID);
    pdu->type = config_get_processedint(config_get_if(), (paramdef_t *)gpd(p, npsp, "type"));
    pdu->nssai.sst = *gpd(p, npsp, "nssai_sst")->iptr;
    pdu->nssai.sd = *gpd(p, npsp, "nssai_sd")->iptr;
    pdu->dnn = *gpd(p, npsp, "dnn")->strptr;
    char **strptr = gpd(p, npsp, "ifname")->strptr;
    if (strptr)
      pdu->ifname = *strptr;
  }

  return num;
}
