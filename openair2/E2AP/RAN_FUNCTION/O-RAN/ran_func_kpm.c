/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "ran_func_kpm.h"
#include "ran_func_kpm_subs.h"
#include "ran_e2sm_ue_id.h"

#include "common/utils/ds/byte_array.h"
#include "openair2/E1AP/e1ap_common.h"
#include "openair2/E2AP/flexric/src/util/time_now_us.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "ds/seq_arr.h"

static pthread_once_t once_kpm_mutex = PTHREAD_ONCE_INIT;

static void init_once_kpm(void)
{
  init_kpm_subs_data();
}

static ngran_node_t get_e2_node_type(void)
{
  ngran_node_t node_type = 0;

#if defined(NGRAN_GNB_DU) && defined(NGRAN_GNB_CUUP) && defined(NGRAN_GNB_CUCP)
  node_type = RC.nrrrc[0]->node_type;
#elif defined (NGRAN_GNB_CUUP)
  node_type =  ngran_gNB_CUUP;
#endif

  return node_type;
}

typedef struct {
  size_t sz;
  ue_id_e2sm_t ue_id[MAX_MOBILES_PER_GNB];

  // Optional
  // only used to retrieve MAC/RLC stats
  NR_UE_info_t* ue_info_list[MAX_MOBILES_PER_GNB];
}arr_ue_id_t;

static void free_arr_ue_id(arr_ue_id_t arr_ue_id)
{
  for (size_t i = 0; i < arr_ue_id.sz; i++) {
    free_ue_id_e2sm(&arr_ue_id.ue_id[i]);
  }
}

static e2_node_level_stats_t node_stats[2] = {0}; // [0] node stats collected in previous reporting period; [1] current node stats

static meas_data_lst_t fill_kpm_meas_data_item(const meas_info_format_1_lst_t* meas_info_lst, const size_t len, const uint32_t gran_period_ms, cudu_ue_info_pair_t ue_info, const size_t ue_idx)
{
  meas_data_lst_t data_item = {0};

  // Measurement Record
  size_t num_records = 0;
  for (size_t i = 0; i < len; i++) {
    // Measurement Type as requested in Action Definition
    assert(meas_info_lst[i].meas_type.type == NAME_MEAS_TYPE && "Only NAME supported");

    num_records += meas_info_lst[i].label_info_lst_len;
  }

  data_item.meas_record_len = num_records;
  data_item.meas_record_lst = calloc(num_records, sizeof(*data_item.meas_record_lst));
  assert(data_item.meas_record_lst != NULL && "Memory exhausted");

  num_records = 0;
  for (size_t i = 0; i < len; i++) {
    char* meas_info_name_str = cp_ba_to_str(meas_info_lst[i].meas_type.name);

    for (size_t j = 0; j < meas_info_lst[i].label_info_lst_len; j++) {
      data_item.meas_record_lst[num_records++] = get_kpm_meas_value(meas_info_name_str, meas_info_lst[i].label_info_lst[j], gran_period_ms, ue_info, ue_idx, node_stats);
    }
    free(meas_info_name_str);
  }

  return data_item;
}

static kpm_ind_msg_format_1_t fill_kpm_ind_msg_frm_1(cudu_ue_info_pair_t ue_info, const size_t ue_idx, const kpm_act_def_format_1_t* act_def_fr_1)
{
  kpm_ind_msg_format_1_t msg_frm_1 = {0};

  // Measurement Data contains a set of Meas Records, each collected at each granularity period
  msg_frm_1.meas_data_lst_len = 1;  /*  this value is equal to (kpm_ric_event_trigger_format_1.report_period_ms/act_def_fr_1->gran_period_ms)
                                        please, check their values in xApp */
  
  msg_frm_1.meas_data_lst = calloc(msg_frm_1.meas_data_lst_len, sizeof(*msg_frm_1.meas_data_lst));
  assert(msg_frm_1.meas_data_lst != NULL && "Memory exhausted" );

  msg_frm_1.meas_data_lst[0] = fill_kpm_meas_data_item(act_def_fr_1->meas_info_lst,
                                                       act_def_fr_1->meas_info_lst_len,
                                                       act_def_fr_1->gran_period_ms,
                                                       ue_info,
                                                       ue_idx);

  // Measurement Information - OPTIONAL
  msg_frm_1.meas_info_lst_len = act_def_fr_1->meas_info_lst_len;
  msg_frm_1.meas_info_lst = calloc(msg_frm_1.meas_info_lst_len, sizeof(meas_info_format_1_lst_t));
  assert(msg_frm_1.meas_info_lst != NULL && "Memory exhausted");
  for (size_t i = 0; i < msg_frm_1.meas_info_lst_len; i++) {
    msg_frm_1.meas_info_lst[i] = cp_meas_info_format_1_lst(&act_def_fr_1->meas_info_lst[i]);
  }

  return msg_frm_1;
}

static cudu_ue_info_pair_t fill_ue_related_info(arr_ue_id_t* arr_ue_id, const size_t ue_idx)
{
  cudu_ue_info_pair_t ue_info = {0};

  if (arr_ue_id->ue_id[ue_idx].type == GNB_UE_ID_E2SM) {
    ue_info.rrc_ue_id = *arr_ue_id->ue_id[ue_idx].gnb.ran_ue_id;  // rrc_ue_id
    ue_info.ue = arr_ue_id->ue_info_list[ue_idx];
  } else if (arr_ue_id->ue_id[ue_idx].type == GNB_CU_UP_UE_ID_E2SM) {
    /* in OAI implementation, CU-UP ue id = CU-CP ue id
                           => CU-UP ue id = rrc_ue_id, but it should not be the case by the spec */
    ue_info.rrc_ue_id = *arr_ue_id->ue_id[ue_idx].gnb_cu_up.ran_ue_id;  // cucp_ue_id = rrc_ue_id
  } else if (arr_ue_id->ue_id[ue_idx].type == GNB_DU_UE_ID_E2SM) {
    ue_info.rrc_ue_id = *arr_ue_id->ue_id[ue_idx].gnb_du.ran_ue_id;  // rrc_ue_id
    ue_info.ue = arr_ue_id->ue_info_list[ue_idx];
  }
  
  return ue_info;
}

static kpm_ind_msg_format_3_t fill_kpm_ind_msg_frm_3(arr_ue_id_t* arr_ue_id, const kpm_act_def_format_1_t* act_def_fr_1)
{
  assert(act_def_fr_1 != NULL);

  kpm_ind_msg_format_3_t msg_frm_3 = {0};

  // Fill UE Measurement Reports
  msg_frm_3.ue_meas_report_lst_len = arr_ue_id->sz;
  msg_frm_3.meas_report_per_ue = calloc(msg_frm_3.ue_meas_report_lst_len, sizeof(meas_report_per_ue_t));
  assert(msg_frm_3.meas_report_per_ue != NULL && "Memory exhausted");

  for (size_t i = 0; i<msg_frm_3.ue_meas_report_lst_len; i++) {
    // Fill UE ID data
    msg_frm_3.meas_report_per_ue[i].ue_meas_report_lst = cp_ue_id_e2sm(&arr_ue_id->ue_id[i]);

    // Fill UE related info
    cudu_ue_info_pair_t ue_info = fill_ue_related_info(arr_ue_id, i);

    // Fill UE measurements
    msg_frm_3.meas_report_per_ue[i].ind_msg_format_1 = fill_kpm_ind_msg_frm_1(ue_info, i, act_def_fr_1);
  }

  node_stats[0] = cp_node_level_stats(&node_stats[1]);

  return msg_frm_3;
}

static void capture_sst_sd(test_cond_value_t* test_cond_value, uint8_t *sst, uint32_t **sd)
{
  DevAssert(sst != NULL);
  DevAssert(sd != NULL);

  // S-NSSAI is an OCTET_STRING, as defined by spec
  switch (test_cond_value->type) {
    case OCTET_STRING_TEST_COND_VALUE: {
      if (test_cond_value->octet_string_value->len == 1) {
        *sst = test_cond_value->octet_string_value->buf[0];
        *sd = NULL;
      } else {
        DevAssert(test_cond_value->octet_string_value->len == 4);
        uint8_t *buf = test_cond_value->octet_string_value->buf;
        *sst = buf[0];
        *sd = malloc(sizeof(uint32_t));
        **sd = buf[1] << 16 | buf[2] << 8 | buf[3];
      }
      printf("[E2 AGENT][E2SM-KPM] Condition NSSAI (%d, %x).\n", *sst, (*sd) ? **sd : 0xffffff);
      break;
    }
    default:
      AssertFatal(false, "test condition value %d impossible\n", test_cond_value->type);
  }
}

static bool nssai_matches(nssai_t a_nssai, uint8_t b_sst, const uint32_t *b_sd)
{
  if (b_sd == NULL) {
    return a_nssai.sst == b_sst && a_nssai.sd == 0xffffff;
  } else {
    AssertFatal(*b_sd <= 0xffffff, "illegal SD %d\n", *b_sd);
    return a_nssai.sst == b_sst && a_nssai.sd == *b_sd;
  }
}

static arr_ue_id_t filter_ues_by_s_nssai_in_cu(const test_info_lst_t test_info)
{
  assert(test_info.S_NSSAI == TRUE_TEST_COND_TYPE && "Must be true");
  assert(test_info.test_cond != NULL && "Must be filled, even though it's optional by the specification");
  assert(*test_info.test_cond == EQUAL_TEST_COND && "Currently support EQUAL test condition");
  assert(test_info.test_cond_value != NULL && "Must be filled, even though it's optional by the specification");
  
  uint8_t sst = 0;
  uint32_t *sd = NULL;
  capture_sst_sd(test_info.test_cond_value, &sst, &sd);

  arr_ue_id_t arr_ue_id = {.sz = 0};

  struct rrc_gNB_ue_context_s* rrc_ue_context = NULL;
  RB_FOREACH(rrc_ue_context, rrc_nr_ue_tree_s, &RC.nrrrc[0]->rrc_ue_head) {
    gNB_RRC_UE_t *ue = &rrc_ue_context->ue_context;
    /* UE has no AMF UE NGAP ID yet => can't send message */
    if (ue->amf_ue_ngap_id >= (1LL << 40))
      continue;
    FOR_EACH_SEQ_ARR(rrc_pdu_session_param_t*, item, &ue->pduSessions) {
      pdusession_t* pdu = &item->param;
      if (nssai_matches(pdu->nssai, sst, sd)) {
        arr_ue_id.ue_id[arr_ue_id.sz] = fill_ue_id_data[ngran_gNB_CU](ue, 0, 0);

        arr_ue_id.sz++;
        break;
      }
    }
    AssertFatal(arr_ue_id.sz < MAX_MOBILES_PER_GNB, "cannot have more UEs than global UE number maximum\n");
  }

  free(sd); // if NULL, nothing happens

  return arr_ue_id;
}

static arr_ue_id_t filter_ues_by_s_nssai_in_cuup(const test_info_lst_t test_info)
{
  assert(test_info.S_NSSAI == TRUE_TEST_COND_TYPE && "Must be true");
  assert(test_info.test_cond != NULL && "Must be filled, even though it's optional by the specification");
  assert(*test_info.test_cond == EQUAL_TEST_COND && "Currently support EQUAL test condition");
  assert(test_info.test_cond_value != NULL && "Must be filled, even though it's optional by the specification");
  
  uint8_t sst = 0;
  uint32_t *sd = NULL;
  capture_sst_sd(test_info.test_cond_value, &sst, &sd);

  arr_ue_id_t arr_ue_id = {.sz = 0};

  // Get NSSAI info from E1 context
  const instance_t e1_inst = 0;
  const e1ap_upcp_inst_t *e1inst = getCxtE1(e1_inst);

  // currently the CU-UP does not store the slices, so we can only do this "coarse filtering"
  for(int s = 0; s < e1inst->cuup.setupReq.plmn[0].supported_slices; s++) {
    if (nssai_matches(e1inst->cuup.setupReq.plmn[0].slice[s], sst, sd)) {
      ue_id_t *pdcp_ue_id_list = calloc(MAX_MOBILES_PER_GNB, sizeof(ue_id_t));
      assert(pdcp_ue_id_list != NULL);

      arr_ue_id.sz = nr_pdcp_get_num_ues(pdcp_ue_id_list, MAX_MOBILES_PER_GNB);
      for (size_t i = 0; i < arr_ue_id.sz; i++) {
        arr_ue_id.ue_id[i] = fill_ue_id_data[ngran_gNB_CUUP](NULL, 0, pdcp_ue_id_list[i]);
      }
      free(pdcp_ue_id_list);
    }
  }

  free(sd); // if NULL, nothing happens

  return arr_ue_id;
}

static arr_ue_id_t filter_ues_by_s_nssai_in_du_or_monolithic(const test_info_lst_t test_info)
{
  assert(test_info.S_NSSAI == TRUE_TEST_COND_TYPE && "Must be true");
  assert(test_info.test_cond != NULL && "Must be filled, even though it's optional by the specification");
  assert(*test_info.test_cond == EQUAL_TEST_COND && "Currently support EQUAL test condition");
  assert(test_info.test_cond_value != NULL && "Must be filled, even though it's optional by the specification");
  
  uint8_t sst = 0;
  uint32_t *sd = NULL;
  capture_sst_sd(test_info.test_cond_value, &sst, &sd);

  arr_ue_id_t arr_ue_id = {.sz = 0};

  const ngran_node_t node_type = get_e2_node_type();

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  // Take MAC info
  FOR_EACH_CONNECTED_UE(&mac->UE_info, ue) {
    NR_UE_sched_ctrl_t *sched_ctrl = &ue->UE_sched_ctrl;
    // UE matches if any of its DRBs matches
    for (size_t l = 0; l < seq_arr_size(&sched_ctrl->lc_config); ++l) {
      const nr_lc_config_t *c = seq_arr_at(&sched_ctrl->lc_config, l);
      if (nssai_matches(c->nssai, sst, sd)) {
        if (node_type == ngran_gNB_DU) {
          f1_ue_data_t rrc_ue_id = du_get_f1_ue_data(ue->rnti); // gNB CU UE ID = rrc_ue_id
          arr_ue_id.ue_id[arr_ue_id.sz] = fill_ue_id_data[ngran_gNB_DU](NULL, rrc_ue_id.secondary_ue, 0);
        } else {
          rrc_gNB_ue_context_t* rrc_ue_context = rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[0], -1, ue->rnti);
          /* UE has no AMF UE NGAP ID yet => can't send message */
          if (rrc_ue_context->ue_context.amf_ue_ngap_id >= (1LL << 40))
            continue;
          arr_ue_id.ue_id[arr_ue_id.sz] = fill_ue_id_data[ngran_gNB](&rrc_ue_context->ue_context, 0, 0);
        }

        // store NR_UE_info_t
        arr_ue_id.ue_info_list[arr_ue_id.sz] = ue;

        arr_ue_id.sz++;
        break;
      }
    }
    AssertFatal(arr_ue_id.sz < MAX_MOBILES_PER_GNB, "cannot have more UEs than global UE number maximum\n");
  }

  node_stats[1].mac_stats.dl.total_prb_aggregate = mac->mac_stats.dl.total_prb_aggregate;
  node_stats[1].mac_stats.ul.total_prb_aggregate = mac->mac_stats.ul.total_prb_aggregate;
  NR_SCHED_UNLOCK(&mac->sched_lock);

  free(sd); // if NULL, nothing happens

  return arr_ue_id;
}

typedef arr_ue_id_t (*ue_type_matcher)(const test_info_lst_t test_info);

static ue_type_matcher match_s_nssai_test_cond_type[END_NGRAN_NODE_TYPE] = 
{
  NULL,
  NULL,
  filter_ues_by_s_nssai_in_du_or_monolithic,
  NULL,
  NULL,
  filter_ues_by_s_nssai_in_cu,
  NULL,
  filter_ues_by_s_nssai_in_du_or_monolithic,
  NULL,
  NULL, // for CU-CP we don't use this
  filter_ues_by_s_nssai_in_cuup,
};

typedef ue_type_matcher (*filter_ues)[END_NGRAN_NODE_TYPE];

static filter_ues match_cond_arr[END_TEST_COND_TYPE_KPM_V2_01] = { 
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  &match_s_nssai_test_cond_type,
};


static kpm_ric_ind_hdr_format_1_t kpm_ind_hdr_frm_1(void)
{
  kpm_ric_ind_hdr_format_1_t hdr_frm_1 = {0};

  int64_t const t = time_now_us();
#if defined KPM_V2_03
  hdr_frm_1.collectStartTime = t/1000000; // seconds
#elif defined KPM_V3_00 
  hdr_frm_1.collectStartTime = t; // microseconds
#else
  static_assert(0!=0, "Undefined KPM SM Version");
#endif

  hdr_frm_1.fileformat_version = NULL;

  // Check E2 Node NG-RAN Type
  const ngran_node_t node_type = get_e2_node_type();
  const char* sender_name = get_ngran_name(node_type);
  hdr_frm_1.sender_name = calloc(1, sizeof(byte_array_t));
  *hdr_frm_1.sender_name = cp_str_to_ba(sender_name);

  return hdr_frm_1;
}

kpm_ind_hdr_t kpm_ind_hdr(void)
{
  kpm_ind_hdr_t hdr = {0};

  hdr.type = FORMAT_1_INDICATION_HEADER;
  hdr.kpm_ric_ind_hdr_format_1 = kpm_ind_hdr_frm_1();

  return hdr;
}

bool read_kpm_sm(void* data)
{
  assert(data != NULL);
  // assert(data->type == KPM_STATS_V3_0);

  kpm_rd_ind_data_t* const kpm = (kpm_rd_ind_data_t*)data;

  assert(kpm->act_def != NULL && "Cannot be NULL");

  // 7.8 Supported RIC Styles and E2SM IE Formats
  // Action Definition Format 4 corresponds to Indication Message Format 3
  switch (kpm->act_def->type) {
    case FORMAT_4_ACTION_DEFINITION: {
      kpm->ind.hdr = kpm_ind_hdr();

      kpm->ind.msg.type = FORMAT_3_INDICATION_MESSAGE;
      // Filter the UE by the test condition criteria
      kpm_act_def_format_4_t const* frm_4 = &kpm->act_def->frm_4; // 8.2.1.2.4
      for (size_t i = 0; i < frm_4->matching_cond_lst_len; i++) {
        const test_info_lst_t test_info = frm_4->matching_cond_lst[i].test_info_lst;
        const ngran_node_t node_type = get_e2_node_type();

        /* Based on the node type (gNB-mono, CU, DU,...) and matching condition (S-NSSAI, DL RSRP, GBR,...)
           get array of E2SM UE IDs with optionally their MAC info */
        ue_type_matcher fp_match_cond_type = (*match_cond_arr[test_info.test_cond_type])[node_type];
        arr_ue_id_t arr_ue_id = fp_match_cond_type(test_info);

        if (arr_ue_id.sz == 0) {
          printf("[E2 AGENT][E2SM-KPM] No UE matches the condition criteria.\n");
          return false;
        }
        kpm->ind.msg.frm_3 = fill_kpm_ind_msg_frm_3(&arr_ue_id, &frm_4->action_def_format_1);
        free_arr_ue_id(arr_ue_id);
      }
      break;
    }
    case FORMAT_1_ACTION_DEFINITION: {
      kpm->ind.hdr = kpm_ind_hdr();

      kpm->ind.msg.type = FORMAT_1_INDICATION_MESSAGE;
      cudu_ue_info_pair_t ue_info = {0}; // not used
      const size_t ue_idx = 0; // not used
      kpm->ind.msg.frm_1 = fill_kpm_ind_msg_frm_1(ue_info, ue_idx, &kpm->act_def->frm_1);
      break;
    }

    default: {
      AssertFatal(false, "Action Definition Format %d not yet implemented", kpm->act_def->type);
    }
  }
  
  return true;
}

static const char* kpm_node_meas_du[] = {
  "CARR.PDSCHMCSDist",
  "CARR.PUSCHMCSDist",
  "L1M.SS-RSRP",
  NULL,
};

static const char* kpm_node_meas_cu[] = {
  "MR.NRScSSSINR",
  "RRC.ConnMean",
  NULL,
};

static const char* kpm_node_meas_gnb[] = {
  "CARR.PDSCHMCSDist",
  "CARR.PUSCHMCSDist",
  "L1M.SS-RSRP",
  "MR.NRScSSSINR",
  "RRC.ConnMean",
  NULL,
};

static const char* kpm_meas_du[] = {
  "DRB.RlcSduDelayDl",
  "DRB.UEThpDl",
  "DRB.UEThpUl",
  "RRU.PrbTotDl",
  "RRU.PrbTotUl",
  NULL,
};

static const char* kpm_meas_gnb[] = {
  "DRB.PdcpSduVolumeDL",
  "DRB.PdcpSduVolumeUL",
  "DRB.RlcSduDelayDl",
  "DRB.UEThpDl",
  "DRB.UEThpUl",
  "RRU.PrbTotDl",
  "RRU.PrbTotUl",
  NULL,
};

static const char* kpm_meas_cuup[] = {
  "DRB.PdcpSduVolumeDL",
  "DRB.PdcpSduVolumeUL",
  NULL,
};

typedef const char** meas_list;

static const meas_list ran_def_kpm[END_NGRAN_NODE_TYPE][END_RIC_SERVICE_REPORT] = {
  {NULL, NULL, NULL, NULL, NULL},
  {NULL, NULL, NULL, NULL, NULL},
  {kpm_node_meas_gnb, NULL, NULL, kpm_meas_gnb, NULL},
  {NULL, NULL, NULL, NULL, NULL},
  {NULL, NULL, NULL, NULL, NULL},
  {kpm_node_meas_cu, NULL, NULL, kpm_meas_cuup, NULL},
  {NULL, NULL, NULL, NULL, NULL},
  {kpm_node_meas_du, NULL, NULL, kpm_meas_du, NULL},
  {NULL, NULL, NULL, NULL, NULL},
  {kpm_node_meas_cu, NULL, NULL, NULL, NULL},
  {NULL, NULL, NULL, kpm_meas_cuup, NULL}
};

static meas_info_for_action_lst_t* fill_meas_info_list_for_act(const ngran_node_t node_type, const ric_service_report_e report_style, size_t *sz)
{
  *sz = 0;
  const char** kpm_meas = ran_def_kpm[node_type][report_style];
  if (kpm_meas == NULL)
    return NULL;
  while (kpm_meas[*sz] != NULL) {
    (*sz)++;
  }

  meas_info_for_action_lst_t *info_lst = calloc(*sz, sizeof(meas_info_for_action_lst_t));
  assert(info_lst != NULL && "Memory exhausted");

  for(size_t i = 0; i < *sz; i++) {
    // 8.3.9
    info_lst[i].name = cp_str_to_ba(kpm_meas[i]);

    // 8.3.10  -  OPTIONAL
    info_lst[i].id = NULL;

    // 8.3.26  -  OPTIONAL
    info_lst[i].bin_range_def = NULL;
  }

  return info_lst;
}

static ric_report_style_item_t fill_kpm_def_report_style_1(const ngran_node_t node_type)
{
  ric_report_style_item_t report_item = {0};

  report_item.meas_info_for_action_lst = fill_meas_info_list_for_act(node_type, STYLE_1_RIC_SERVICE_REPORT, &report_item.meas_info_for_action_lst_len);
  if (report_item.meas_info_for_action_lst == NULL)
    return report_item;

  report_item.report_style_type = STYLE_1_RIC_SERVICE_REPORT;
  const char report_style_name[] = "E2 Node Measurement";
  report_item.report_style_name = cp_str_to_ba(report_style_name);
  report_item.act_def_format_type = FORMAT_1_ACTION_DEFINITION;

  // Supported RIC Indication Formats
  report_item.ind_hdr_format_type = FORMAT_1_INDICATION_HEADER;  // 8.3.5
  report_item.ind_msg_format_type = FORMAT_1_INDICATION_MESSAGE;  // 8.3.5

  return report_item;
}

static ric_report_style_item_t fill_kpm_def_report_style_4(const ngran_node_t node_type)
{
  ric_report_style_item_t report_item = {0};

  report_item.meas_info_for_action_lst = fill_meas_info_list_for_act(node_type, STYLE_4_RIC_SERVICE_REPORT, &report_item.meas_info_for_action_lst_len);
  if (report_item.meas_info_for_action_lst == NULL)
    return report_item;

  report_item.report_style_type = STYLE_4_RIC_SERVICE_REPORT;
  const char report_style_name[] = "Common Condition-based, UE-level Measurement";
  report_item.report_style_name = cp_str_to_ba(report_style_name);
  report_item.act_def_format_type = FORMAT_4_ACTION_DEFINITION;

  // Supported RIC Indication Formats
  report_item.ind_hdr_format_type = FORMAT_1_INDICATION_HEADER;  // 8.3.5
  report_item.ind_msg_format_type = FORMAT_3_INDICATION_MESSAGE;  // 8.3.5

  return report_item;
}

static ric_report_style_item_t fill_kpm_def_report_style_null(const ngran_node_t node_type)
{
  (void)node_type;
  ric_report_style_item_t report_item = {0};
  return report_item;
}

typedef ric_report_style_item_t (*kpm_report_style)(const ngran_node_t node_type);

static kpm_report_style report_style[END_RIC_SERVICE_REPORT] = {
  fill_kpm_def_report_style_1,
  fill_kpm_def_report_style_null,
  fill_kpm_def_report_style_null,
  fill_kpm_def_report_style_4,
  fill_kpm_def_report_style_null
};

void read_kpm_setup_sm(void* e2ap)
{
  assert(e2ap != NULL);
//  assert(e2ap->type == KPM_V3_0_AGENT_IF_E2_SETUP_ANS_V0);

  kpm_e2_setup_t* kpm = (kpm_e2_setup_t*)(e2ap);

  /* Fill the RAN Function Definition with currently supported measurements */

  // RAN Function Name is already filled in fill_ran_function_name() in kpm_sm_agent.c

  // Fill supported measurements depending on the E2 node
  // [1, 65535]
  // 3GPP TS 28.552
  const ngran_node_t node_type = get_e2_node_type();
  size_t num_styles = 0;
  for (size_t i = 0; i < END_RIC_SERVICE_REPORT; i++) {
    ric_report_style_item_t report_item = report_style[i](node_type);
    if (report_item.meas_info_for_action_lst != NULL) {
      kpm->ran_func_def.ric_report_style_list = realloc(kpm->ran_func_def.ric_report_style_list, (num_styles + 1) * sizeof(*kpm->ran_func_def.ric_report_style_list));
      kpm->ran_func_def.ric_report_style_list[num_styles++] = cp_ric_report_style_item(&report_item);
      free_ric_report_style_item(&report_item);
    }
  }
  if (kpm->ran_func_def.ric_report_style_list == NULL)
    return; // no measurements supported for this node
  kpm->ran_func_def.sz_ric_report_style_list = num_styles;

  // Sequence of Event Trigger styles
  const size_t sz_ev_tr = 1;
  kpm->ran_func_def.sz_ric_event_trigger_style_list = sz_ev_tr;
  kpm->ran_func_def.ric_event_trigger_style_list = calloc(sz_ev_tr, sizeof(ric_event_trigger_style_item_t));
  assert(kpm->ran_func_def.ric_event_trigger_style_list != NULL && "Memory exhausted");

  ric_event_trigger_style_item_t* ev_tr_item = &kpm->ran_func_def.ric_event_trigger_style_list[0];

  ev_tr_item->style_type = STYLE_1_RIC_EVENT_TRIGGER;
  const char ev_style_name[] = "Periodic Report";
  ev_tr_item->style_name = cp_str_to_ba(ev_style_name);
  ev_tr_item->format_type = FORMAT_1_RIC_EVENT_TRIGGER;

  // E2 Setup Request is sent periodically until the connection is established
  // RC subscritpion data should be initialized only once
  const int ret = pthread_once(&once_kpm_mutex, init_once_kpm);
  DevAssert(ret == 0);
}

sm_ag_if_ans_t write_ctrl_kpm_sm(void const* src)
{
  printf("write_ctrl callback for KPM SM: operation not supported\n");
  (void)src;
  sm_ag_if_ans_t ans = {0};
  return ans;
}
