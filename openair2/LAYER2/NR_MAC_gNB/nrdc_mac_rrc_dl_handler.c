/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nrdc_mac_rrc_dl_handler.h"

#include "common/utils/LOG/log.h"
#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "mac_rrc_dl_handler.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/lib/f1ap_ue_context.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair3/ocp-gtpu/gtp_itf.h"

static NR_UE_info_t *nrdc_create_new_UE(gNB_MAC_INST *mac, nr_cell_sched_t *cell, uint32_t cu_id, const NR_CG_ConfigInfo_t *cgci)
{
  rnti_t rnti;
  bool found = nr_mac_get_new_rnti(&mac->UE_info, &rnti);
  if (!found)
    return NULL;

  f1_ue_data_t new_ue_data = {.secondary_ue = cu_id};
  bool success = du_add_f1_ue_data(rnti, &new_ue_data);
  DevAssert(success);

  NR_UE_info_t *UE = get_new_nr_ue_inst(&mac->UE_info.uid_allocator, rnti, NULL, cell);
  AssertFatal(UE->uid < MAX_MOBILES_PER_GNB, "cannot create UE context, UE context setup failure not implemented\n");

  NR_COMMON_channels_t *cc = &cell->common_channels;
  const NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;

  /* mimic NSA way to create a new UE (with adaptations) */
  NR_UE_NR_Capability_t *cap = get_ue_nr_cap_from_cg_config_info(cgci);
  int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
  UE->uecap_fs_ids = feature_set_ids_handler(cap, scc->downlinkConfigCommon->frequencyInfoDL, NR_DC);
  NR_CellGroupConfig_t *cellGroupConfig = get_default_secondaryCellGroup(scc, cap, &UE->uecap_fs_ids, 1, 1, &cell->radio_config, cell, UE->uid, ssb_index);
  cellGroupConfig->spCellConfig->reconfigurationWithSync = get_reconfiguration_with_sync(UE->rnti, UE->uid, scc, mac->frame);
  UE->capability = cap;
  UE->local_bwp_id = 1; // get_default_secondaryCellGroup sets 1st active BWP as 1

  // note: we don't pass the cellGroupConfig to add_new_nr_ue() because we need
  // the uid to create the CellGroupConfig (which is in the UE context created
  // by add_new_nr_ue(); it's a kind of chicken-and-egg problem), so below we
  // complete the UE context with the information that add_new_nr_ue() would
  // have added
  UE->CellGroup = cellGroupConfig;

  UE->nrdc_mode = true;

  if (!add_new_ra_ue(&mac->UE_info, UE)) {
    release_nr_ue_inst(&mac->UE_info.uid_allocator, UE);
    LOG_E(NR_MAC, "UE list full while creating new UE\n");
    return NULL;
  }
  nr_mac_prepare_ra_ue(mac, cell, UE);

  return UE;
}

void nrdc_ue_context_setup_request(const f1ap_ue_context_setup_req_t *req)
{
  LOG_D(NR_MAC, "nrdc_ue_context_setup_request called!\n");

  gNB_MAC_INST *mac = RC.nrmac[0];
  // TODO: Add proper error handling
  nr_cell_sched_t *cell = nr_mac_get_cell_by_cgi(mac, req->plmn, req->nr_cellid);
  AssertFatal(cell != NULL, "cell with CGI (cellid=%lu) not found\n", req->nr_cellid);

  f1ap_ue_context_setup_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
  };

  const f1ap_cu_to_du_rrc_info_t *cu2du = &req->cu_to_du_rrc_info;
  NR_CG_ConfigInfo_t *cg_configinfo = get_cg_config_info(cu2du->cg_configinfo->buf, cu2du->cg_configinfo->len);

  NR_SCHED_LOCK(&mac->sched_lock);

  NR_UE_info_t *UE = nrdc_create_new_UE(mac, cell, req->gNB_CU_ue_id, cg_configinfo);
  AssertFatal(UE, "NR-DC: cannot create a new UE, but UE Context Setup Failed not implemented yet\n");
  resp.gNB_DU_ue_id = UE->rnti;
  resp.crnti = malloc_or_fail(sizeof(*resp.crnti));
  *resp.crnti = UE->rnti;

  NR_CellGroupConfig_t *new_CellGroup = clone_CellGroupConfig(UE->CellGroup);

  if (req->drbs_len > 0)
    resp.drbs_len = handle_ue_context_drbs_setup(UE, req->drbs_len, req->drbs, &resp.drbs, new_CellGroup, &mac->rlc_config);

  /* in other parts of the code, we have: UE->reconfigCellGroup = new_CellGroup;
   * but doing so crashes the DU, so let's do UE->CellGroup = new_CellGroup;
   * (to be fixed?)
   */
  UE->CellGroup = new_CellGroup;
  int ss_type =  NR_SearchSpace__searchSpaceType_PR_common;
  NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  configure_UE_BWP(cell, scc, UE, true, ss_type, -1, -1);

  NR_SCHED_UNLOCK(&mac->sched_lock);

  byte_array_t cgc = { .buf = calloc_or_fail(1,1024) };
  asn_enc_rval_t enc_rval =
      uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig, NULL, new_CellGroup, cgc.buf, 1024);
  AssertFatal(enc_rval.encoded > 0, "Could not encode CellGroup, failed element %s\n", enc_rval.failed_type->name);
  cgc.len = (enc_rval.encoded + 7) >> 3;
  resp.du_to_cu_rrc_info.cell_group_config = cgc;

  mac->mac_rrc.ue_context_setup_response(&resp);

  /* free the memory we allocated above */
  free_ue_context_setup_resp(&resp);
  ASN_STRUCT_FREE(asn_DEF_NR_CG_ConfigInfo, cg_configinfo);
}

static instance_t get_f1_gtp_instance(void)
{
  const f1ap_cudu_inst_t *inst = getCxt(0);
  if (!inst)
    return -1; // means no F1
  return inst->gtpInst;
}

void nrdc_ue_context_modification_request(const f1ap_ue_context_mod_req_t *req)
{
  /* this only works in F1 mode for the moment */
  instance_t f1inst = get_f1_gtp_instance();
  DevAssert(f1inst >= 0);

  gNB_MAC_INST *mac = RC.nrmac[0];
  f1ap_ue_context_mod_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_connected_nr_ue(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);
  if (!UE) {
    LOG_E(NR_MAC, "could not find UE with RNTI %04x\n", req->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  /* only support release of 1 drb */
  if (req->srbs_len > 0 || req->drbs_len > 0 || req->rrc_container != NULL
     || req->cu_to_du_rrc_info != NULL) {
    LOG_E(NR_MAC, "NR-DC UE Context Modification Request contains unhandled fields, ignoring\n");
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  if (req->drbs_rel_len != 1) {
    LOG_E(NR_MAC, "NR-DC UE Context Modification Request contains wrong number of drbs to release (%d, expecting 1), ignoring\n", req->drbs_rel_len);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  int drb_id = req->drbs_rel[0].id;
  int lcid = get_lcid_from_drbid(drb_id);

  /* remove the bearer everywhere it has to */
  nr_mac_remove_lcid(&UE->UE_sched_ctrl, lcid);
  nr_rlc_release_entity(UE->rnti, lcid);
  newGtpuDeleteOneTunnel(f1inst, UE->rnti, drb_id);

  /* remove the RLC from UE->CellGroup->rlc_BearerToAddModList */
  NR_CellGroupConfig_t *cellGroupConfig = UE->CellGroup;
  int idx = 0;
  while (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
    const NR_RLC_BearerConfig_t *bc = cellGroupConfig->rlc_BearerToAddModList->list.array[idx];
    if (bc->logicalChannelIdentity == lcid)
      break;
    idx++;
  }
  if (idx < cellGroupConfig->rlc_BearerToAddModList->list.count)
    asn_sequence_del(&cellGroupConfig->rlc_BearerToAddModList->list, idx, 1);

  /* generate Context Modification Response */
  NR_CellGroupConfig_t cell_group = {
    .cellGroupId = 1,
    .rlc_BearerToReleaseList = &(struct NR_CellGroupConfig__rlc_BearerToReleaseList) {
      .list = {
        .array = (NR_LogicalChannelIdentity_t *[]) {
          &(NR_LogicalChannelIdentity_t) { lcid }
        },
        .count = 1
      }
    }
  };

  resp.du_to_cu_rrc_info = calloc_or_fail(1, sizeof(du_to_cu_rrc_information_t));
  resp.du_to_cu_rrc_info->cell_group_config = encode_cellgroup_config(&cell_group);

  NR_SCHED_UNLOCK(&mac->sched_lock);

  mac->mac_rrc.ue_context_modification_response(&resp);

  free_ue_context_mod_resp(&resp);
}
