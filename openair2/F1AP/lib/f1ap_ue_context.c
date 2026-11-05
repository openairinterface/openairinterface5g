/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "f1ap_ue_context.h"

#include "f1ap_lib_common.h"
#include "f1ap_lib_includes.h"
#include "f1ap_messages_types.h"

#include "common/utils/assertions.h"
#include "openair3/UTILS/conversions.h"
#include "common/utils/oai_asn1.h"
#include "common/utils/utils.h"
#include "common/utils/ds/byte_array.h"

static F1AP_CUtoDURRCInformation_t encode_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *cu2du)
{
  F1AP_CUtoDURRCInformation_t enc = {0};

  /* optional: cG_ConfigInfo */
  if (cu2du->cg_configinfo) {
    asn1cCalloc(enc.cG_ConfigInfo, cG_ConfigInfo);
    const byte_array_t *ba = cu2du->cg_configinfo;
    OCTET_STRING_fromBuf(cG_ConfigInfo, (const char *)ba->buf, ba->len);
  }

  /* optional: uE_CapabilityRAT_ContainerList */
  if (cu2du->ue_cap) {
    asn1cCalloc(enc.uE_CapabilityRAT_ContainerList, uE_CapabilityRAT_ContainerList);
    const byte_array_t *ba = cu2du->ue_cap;
    OCTET_STRING_fromBuf(uE_CapabilityRAT_ContainerList, (const char *)ba->buf, ba->len);
  }

  /* optional: measConfig */
  if (cu2du->meas_config) {
    asn1cCalloc(enc.measConfig, measConfig);
    const byte_array_t *ba = cu2du->meas_config;
    OCTET_STRING_fromBuf(measConfig, (const char *)ba->buf, ba->len);
  }

  /* optional: HandoverPreparationInformation */
  F1AP_ProtocolExtensionContainer_11023P60_t *p = NULL;
  if (cu2du->ho_prep_info) {
    p = calloc_or_fail(1, sizeof(*p));
    enc.iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)p;
    asn1cSequenceAdd(p->list, F1AP_CUtoDURRCInformation_ExtIEs_t, ie_ext);
    ie_ext->id = F1AP_ProtocolIE_ID_id_HandoverPreparationInformation;
    ie_ext->criticality = F1AP_Criticality_ignore;
    ie_ext->extensionValue.present = F1AP_CUtoDURRCInformation_ExtIEs__extensionValue_PR_HandoverPreparationInformation;
    const byte_array_t *ba = cu2du->ho_prep_info;
    F1AP_HandoverPreparationInformation_t *hpi = &ie_ext->extensionValue.choice.HandoverPreparationInformation;
    OCTET_STRING_fromBuf(hpi, (const char *)ba->buf, ba->len);
  }

  /* optional: iE_Extensions */
  if (cu2du->meas_timing_config) {
    if (!p)
      p = calloc_or_fail(1, sizeof(*p));
    enc.iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)p;
    asn1cSequenceAdd(p->list, F1AP_CUtoDURRCInformation_ExtIEs_t, ie_ext);
    ie_ext->id = F1AP_ProtocolIE_ID_id_MeasurementTimingConfiguration;
    ie_ext->criticality = F1AP_Criticality_ignore;
    ie_ext->extensionValue.present = F1AP_CUtoDURRCInformation_ExtIEs__extensionValue_PR_MeasurementTimingConfiguration;
    const byte_array_t *ba = cu2du->meas_timing_config;
    F1AP_MeasurementTimingConfiguration_t *mtc = &ie_ext->extensionValue.choice.MeasurementTimingConfiguration;
    OCTET_STRING_fromBuf(mtc, (const char *)ba->buf, ba->len);
  }
  return enc;
}

static bool decode_cu_to_du_rrc_info(f1ap_cu_to_du_rrc_info_t *dec, const F1AP_CUtoDURRCInformation_t *cu2du)
{
  if (cu2du->cG_ConfigInfo) {
    dec->cg_configinfo = calloc_or_fail(1, sizeof(*dec->cg_configinfo));
    const F1AP_CG_ConfigInfo_t *cgci = cu2du->cG_ConfigInfo;
    *dec->cg_configinfo = create_byte_array(cgci->size, (uint8_t*)cgci->buf);
  }
  if (cu2du->uE_CapabilityRAT_ContainerList) {
    dec->ue_cap = calloc_or_fail(1, sizeof(*dec->ue_cap));
    const F1AP_UE_CapabilityRAT_ContainerList_t *l = cu2du->uE_CapabilityRAT_ContainerList;
    *dec->ue_cap = create_byte_array(l->size, (uint8_t*)l->buf);
  }
  if (cu2du->measConfig) {
    dec->meas_config = calloc_or_fail(1, sizeof(*dec->meas_config));
    const F1AP_MeasConfig_t *mc = cu2du->measConfig;
    *dec->meas_config = create_byte_array(mc->size, (uint8_t*)mc->buf);
  }
  if (cu2du->iE_Extensions) {
    const F1AP_ProtocolExtensionContainer_11023P60_t *ext = (const F1AP_ProtocolExtensionContainer_11023P60_t *)cu2du->iE_Extensions;
    for (int i = 0; i < ext->list.count; ++i) {
      const F1AP_CUtoDURRCInformation_ExtIEs_t *cu2du_info_ext = ext->list.array[i];
      switch (cu2du_info_ext->id) {
        case F1AP_ProtocolIE_ID_id_HandoverPreparationInformation:
          dec->ho_prep_info = calloc_or_fail(1, sizeof(*dec->ho_prep_info));
          const F1AP_HandoverPreparationInformation_t *hopi = &cu2du_info_ext->extensionValue.choice.HandoverPreparationInformation;
          *dec->ho_prep_info = create_byte_array(hopi->size, (uint8_t*) hopi->buf);
          break;
        case F1AP_ProtocolIE_ID_id_MeasurementTimingConfiguration:
          dec->meas_timing_config = calloc_or_fail(1, sizeof(*dec->meas_timing_config));
          const F1AP_MeasurementTimingConfiguration_t *mtc = &cu2du_info_ext->extensionValue.choice.MeasurementTimingConfiguration;
          *dec->meas_timing_config = create_byte_array(mtc->size, (uint8_t *)mtc->buf);
          break;
        default:
          PRINT_ERROR("received unsupported F1AP CUtoDURRCInfo extension container %ld\n", cu2du_info_ext->id);
      }
    }
  }
  return true;
}

static f1ap_cu_to_du_rrc_info_t cp_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *src)
{
  f1ap_cu_to_du_rrc_info_t dst = {0};
  CP_OPT_BYTE_ARRAY(dst.cg_configinfo, src->cg_configinfo);
  CP_OPT_BYTE_ARRAY(dst.ue_cap, src->ue_cap);
  CP_OPT_BYTE_ARRAY(dst.meas_config, src->meas_config);
  CP_OPT_BYTE_ARRAY(dst.meas_timing_config, src->meas_timing_config);
  CP_OPT_BYTE_ARRAY(dst.ho_prep_info, src->ho_prep_info);
  return dst;
}

static bool eq_ba(const byte_array_t a, const byte_array_t b)
{
  return eq_byte_array(&a, &b);
}

static bool eq_cu_to_du_rrc_info(const f1ap_cu_to_du_rrc_info_t *a, const f1ap_cu_to_du_rrc_info_t *b)
{
  _EQ_CHECK_OPTIONAL_IE(a, b, cg_configinfo, eq_ba);
  _EQ_CHECK_OPTIONAL_IE(a, b, ue_cap, eq_ba);
  _EQ_CHECK_OPTIONAL_IE(a, b, meas_config, eq_ba);
  _EQ_CHECK_OPTIONAL_IE(a, b, meas_timing_config, eq_ba);
  _EQ_CHECK_OPTIONAL_IE(a, b, ho_prep_info, eq_ba);
  return true;
}

static void free_cu_to_du_rrc_info(f1ap_cu_to_du_rrc_info_t *cu2du)
{
  FREE_OPT_BYTE_ARRAY(cu2du->cg_configinfo);
  FREE_OPT_BYTE_ARRAY(cu2du->ue_cap);
  FREE_OPT_BYTE_ARRAY(cu2du->meas_config);
  FREE_OPT_BYTE_ARRAY(cu2du->meas_timing_config);
  FREE_OPT_BYTE_ARRAY(cu2du->ho_prep_info);
}

static F1AP_DUtoCURRCInformation_t encode_du_to_cu_rrc_info(const f1ap_du_to_cu_rrc_info_t *du2cu)
{
  F1AP_DUtoCURRCInformation_t enc = {0};

  /* mandatory: cellGroupConfig */
  const byte_array_t *ba = &du2cu->cell_group_config;
  OCTET_STRING_fromBuf(&enc.cellGroupConfig, (const char *)ba->buf, ba->len);

  /* optional: MeasGapConfig */
  if (du2cu->meas_gap_config) {
    ba = du2cu->meas_gap_config;
    asn1cCalloc(enc.measGapConfig, measGapConfig);
    OCTET_STRING_fromBuf(measGapConfig, (const char *)ba->buf, ba->len);
  }

  return enc;
}

static bool decode_du_to_cu_rrc_info(f1ap_du_to_cu_rrc_info_t *dec, const F1AP_DUtoCURRCInformation_t *du2cu)
{
  const F1AP_CellGroupConfig_t *cgc = &du2cu->cellGroupConfig;
  dec->cell_group_config = create_byte_array(cgc->size, (uint8_t *)cgc->buf);

  if (du2cu->measGapConfig) {
    const F1AP_MeasGapConfig_t *mgc = du2cu->measGapConfig;
    dec->meas_gap_config = calloc_or_fail(1, sizeof(*dec->meas_gap_config));
    *dec->meas_gap_config = create_byte_array(mgc->size, (uint8_t *)mgc->buf);
  }
  return true;
}

static f1ap_du_to_cu_rrc_info_t cp_du_to_cu_rrc_info(const f1ap_du_to_cu_rrc_info_t *src)
{
  f1ap_du_to_cu_rrc_info_t cp = {0};
  cp.cell_group_config = copy_byte_array(src->cell_group_config);
  CP_OPT_BYTE_ARRAY(cp.meas_gap_config, src->meas_gap_config);
  return cp;
}

static bool eq_du_to_cu_rrc_info(const f1ap_du_to_cu_rrc_info_t *a, const f1ap_du_to_cu_rrc_info_t *b)
{
  _F1_CHECK_EXP(eq_byte_array(&a->cell_group_config, &b->cell_group_config));
  _EQ_CHECK_OPTIONAL_IE(a, b, meas_gap_config, eq_ba);
  return true;
}

static void free_du_to_cu_rrc_info(f1ap_du_to_cu_rrc_info_t *du2cu)
{
  free_byte_array(du2cu->cell_group_config);
  FREE_OPT_BYTE_ARRAY(du2cu->meas_gap_config);
}

/* \brief Encode SRB-ToBeSetup List, for UE Context Setup Request, from
 * f1ap_srb_to_setup_t. */
static F1AP_SRBs_ToBeSetup_List_t encode_srbs_to_setup(int n, const f1ap_srb_to_setup_t *srbs)
{
  F1AP_SRBs_ToBeSetup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    asn1cSequenceAdd(list, F1AP_SRBs_ToBeSetup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_SRBs_ToBeSetup_ItemIEs__value_PR_SRBs_ToBeSetup_Item;
    itie->value.choice.SRBs_ToBeSetup_Item.sRBID = srbs[i].id;
  }
  return list;
}

/* \brief Encode SRB-ToBeSetupMod List, for UE context Modification Request,
 * from f1ap_srb_to_setup_t. The ToBeSetup/ToBeSetupMod lists are basically the
 * same in both cases, but the asn1c types are different. */
static F1AP_SRBs_ToBeSetupMod_List_t encode_srbs_to_setupmod(int n, const f1ap_srb_to_setup_t *srbs)
{
  F1AP_SRBs_ToBeSetupMod_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    asn1cSequenceAdd(list, F1AP_SRBs_ToBeSetupMod_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetupMod_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_SRBs_ToBeSetupMod_ItemIEs__value_PR_SRBs_ToBeSetupMod_Item;
    itie->value.choice.SRBs_ToBeSetupMod_Item.sRBID = srbs[i].id;
  }
  return list;
}

/* \brief Decode SRB-ToBeSetup List, from UE Context Setup Request, into
 * f1ap_srb_to_setup_t. */
static bool decode_srbs_to_setup(const F1AP_SRBs_ToBeSetup_List_t *f1ap, int *n, f1ap_srb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_srb_to_setup_t *srbs = *out = calloc_or_fail(count, sizeof(*srbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_SRBs_ToBeSetup_ItemIEs_t *itie = (F1AP_SRBs_ToBeSetup_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_SRBs_ToBeSetup_ItemIEs__value_PR_SRBs_ToBeSetup_Item);
    srbs[i].id = itie->value.choice.SRBs_ToBeSetup_Item.sRBID;
  }

  return true;
}

/* \brief Decode SRB-ToBeSetupMod List, from UE Context Modification Request,
 * into f1ap_srb_to_setup_t. The ToBeSetup/ToBeSetupMod lists are basically the
 * same, but the asn1c types are different. */
static bool decode_srbs_to_setupmod(const F1AP_SRBs_ToBeSetupMod_List_t *f1ap, int *n, f1ap_srb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_srb_to_setup_t *srbs = *out = calloc_or_fail(count, sizeof(*srbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_SRBs_ToBeSetupMod_ItemIEs_t *itie = (F1AP_SRBs_ToBeSetupMod_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_SRBs_ToBeSetupMod_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_SRBs_ToBeSetupMod_ItemIEs__value_PR_SRBs_ToBeSetupMod_Item);
    srbs[i].id = itie->value.choice.SRBs_ToBeSetupMod_Item.sRBID;
  }

  return true;
}

static f1ap_srb_to_setup_t cp_srb_to_setup(const f1ap_srb_to_setup_t *orig)
{
  f1ap_srb_to_setup_t cp = {
    .id = orig->id,
  };
  return cp;
}

static bool eq_srb_to_setup(const f1ap_srb_to_setup_t *a, const f1ap_srb_to_setup_t *b)
{
  _EQ_CHECK_INT(a->id, b->id);
  return true;
}

static void free_srb_to_setup(f1ap_srb_to_setup_t *srbs)
{
  // nothing to free
  UNUSED(srbs);
}

static F1AP_SRBs_Setup_List_t encode_srbs_setup(int n, const f1ap_srb_setup_t *srbs)
{
  F1AP_SRBs_Setup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    asn1cSequenceAdd(list, F1AP_SRBs_Setup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_SRBs_Setup_Item;
    itie->criticality = F1AP_Criticality_ignore;
    itie->value.present = F1AP_SRBs_Setup_ItemIEs__value_PR_SRBs_Setup_Item;
    F1AP_SRBs_Setup_Item_t *it = &itie->value.choice.SRBs_Setup_Item;
    it->sRBID = srbs[i].id;
    it->lCID = srbs[i].lcid;
  }
  return list;
}

static F1AP_SRBs_SetupMod_List_t encode_srbs_setupmod(int n, const f1ap_srb_setup_t *srbs)
{
  F1AP_SRBs_SetupMod_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    asn1cSequenceAdd(list, F1AP_SRBs_SetupMod_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_SRBs_SetupMod_Item;
    itie->criticality = F1AP_Criticality_ignore;
    itie->value.present = F1AP_SRBs_SetupMod_ItemIEs__value_PR_SRBs_SetupMod_Item;
    F1AP_SRBs_SetupMod_Item_t *it = &itie->value.choice.SRBs_SetupMod_Item;
    it->sRBID = srbs[i].id;
    it->lCID = srbs[i].lcid;
  }
  return list;
}

static bool decode_srbs_setup(const F1AP_SRBs_Setup_List_t *f1ap, int *n, f1ap_srb_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_srb_setup_t *srbs = *out = calloc_or_fail(count, sizeof(*srbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_SRBs_Setup_ItemIEs_t *itie = (F1AP_SRBs_Setup_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_SRBs_Setup_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_SRBs_Setup_ItemIEs__value_PR_SRBs_Setup_Item);
    const F1AP_SRBs_Setup_Item_t *it = &itie->value.choice.SRBs_Setup_Item;
    srbs[i].id = it->sRBID;
    srbs[i].lcid = it->lCID;
  }
  return true;
}

static bool decode_srbs_setupmod(const F1AP_SRBs_SetupMod_List_t *f1ap, int *n, f1ap_srb_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_srb_setup_t *srbs = *out = calloc_or_fail(count, sizeof(*srbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_SRBs_SetupMod_ItemIEs_t *itie = (F1AP_SRBs_SetupMod_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_SRBs_SetupMod_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_SRBs_SetupMod_ItemIEs__value_PR_SRBs_SetupMod_Item);
    const F1AP_SRBs_SetupMod_Item_t *it = &itie->value.choice.SRBs_SetupMod_Item;
    srbs[i].id = it->sRBID;
    srbs[i].lcid = it->lCID;
  }
  return true;
}

static f1ap_srb_setup_t cp_srb_setup(const f1ap_srb_setup_t *orig)
{
  f1ap_srb_setup_t cp = {.id = orig->id, .lcid = orig->lcid};
  return cp;
}

static bool eq_srb_setup(const f1ap_srb_setup_t *a, const f1ap_srb_setup_t *b)
{
  _EQ_CHECK_INT(a->id, b->id);
  _EQ_CHECK_INT(a->lcid, b->lcid);
  return true;
}

static void free_srb_setup(f1ap_srb_setup_t *srbs)
{
  // nothing to free
  UNUSED(srbs);
}

static F1AP_QoSFlowLevelQoSParameters_t encode_qos_flow_param(const f1ap_qos_flow_param_t *p)
{
  F1AP_QoSFlowLevelQoSParameters_t f1ap = {0};

  if (p->qos_type == NON_DYNAMIC) {
    f1ap.qoS_Characteristics.present = F1AP_QoS_Characteristics_PR_non_Dynamic_5QI;
    asn1cCalloc(f1ap.qoS_Characteristics.choice.non_Dynamic_5QI, nd);
    DevAssert(p->nondyn.fiveQI >= MIN_FIVEQI && p->nondyn.fiveQI <= MAX_FIVEQI);
    nd->fiveQI = p->nondyn.fiveQI;
  } else {
    DevAssert(p->qos_type == DYNAMIC);
    f1ap.qoS_Characteristics.present = F1AP_QoS_Characteristics_PR_dynamic_5QI;
    asn1cCalloc(f1ap.qoS_Characteristics.choice.dynamic_5QI, d);

    d->qoSPriorityLevel = p->dyn.prio;
    d->packetDelayBudget = p->dyn.pdb;
    d->packetErrorRate.pER_Scalar = p->dyn.per.scalar;
    d->packetErrorRate.pER_Exponent = p->dyn.per.exponent;

    if (p->dyn.delay_critical) {
      e_F1AP_Dynamic5QIDescriptor__delayCritical v = *p->dyn.delay_critical
                                                         ? F1AP_Dynamic5QIDescriptor__delayCritical_delay_critical
                                                         : F1AP_Dynamic5QIDescriptor__delayCritical_non_delay_critical;
      asn1cCallocOne(d->delayCritical, v);
    }

    if (p->dyn.avg_win)
      asn1cCallocOne(d->averagingWindow, *p->dyn.avg_win);
  }

  F1AP_NGRANAllocationAndRetentionPriority_t *arp = &f1ap.nGRANallocationRetentionPriority;
  arp->priorityLevel = p->arp.prio;
  arp->pre_emptionCapability = p->arp.preempt_cap;
  arp->pre_emptionVulnerability = p->arp.preempt_vuln;

  // GBR QoS Flow Information (optional)
  if (p->gbr_qos_flow_information != NULL) {
    const gbr_qos_flow_information_t *gbr = p->gbr_qos_flow_information;
    asn1cCalloc(f1ap.gBR_QoS_Flow_Information, gbr_ie);

    // Bit rates: uint64_t -> ASN.1 INTEGER_t
    asn_ulong2INTEGER(&gbr_ie->maxFlowBitRateDownlink, gbr->dl.maximumFlowBitRate);
    asn_ulong2INTEGER(&gbr_ie->maxFlowBitRateUplink, gbr->ul.maximumFlowBitRate);
    asn_ulong2INTEGER(&gbr_ie->guaranteedFlowBitRateDownlink, gbr->dl.guaranteedFlowBitRate);
    asn_ulong2INTEGER(&gbr_ie->guaranteedFlowBitRateUplink, gbr->ul.guaranteedFlowBitRate);
  }

  return f1ap;
}

static bool decode_qos_flow_param(const F1AP_QoSFlowLevelQoSParameters_t *f1ap, f1ap_qos_flow_param_t *out)
{
  if (f1ap->qoS_Characteristics.present == F1AP_QoS_Characteristics_PR_non_Dynamic_5QI) {
    out->qos_type = NON_DYNAMIC;
    const F1AP_NonDynamic5QIDescriptor_t *nondyn = f1ap->qoS_Characteristics.choice.non_Dynamic_5QI;
    const int fiveqi = nondyn->fiveQI;
    if (fiveqi < MIN_FIVEQI || fiveqi > MAX_FIVEQI) {
      PRINT_ERROR("non-dynamic fiveQI %d out of range %d..%d\n", fiveqi, MIN_FIVEQI, MAX_FIVEQI);
      return false;
    }
    out->nondyn.fiveQI = fiveqi;
  } else {
    _EQ_CHECK_INT(f1ap->qoS_Characteristics.present, F1AP_QoS_Characteristics_PR_dynamic_5QI);
    const F1AP_Dynamic5QIDescriptor_t *d = f1ap->qoS_Characteristics.choice.dynamic_5QI;
    out->qos_type = DYNAMIC;
    out->dyn.prio = d->qoSPriorityLevel;
    out->dyn.pdb = d->packetDelayBudget;
    out->dyn.per.scalar = d->packetErrorRate.pER_Scalar;
    out->dyn.per.exponent = d->packetErrorRate.pER_Exponent;

    if (d->delayCritical) {
      bool v = *d->delayCritical == F1AP_Dynamic5QIDescriptor__delayCritical_delay_critical;
      _F1_MALLOC(out->dyn.delay_critical, v);
    }

    if (d->averagingWindow)
      _F1_MALLOC(out->dyn.avg_win, *d->averagingWindow);
  }

  const F1AP_NGRANAllocationAndRetentionPriority_t *arp = &f1ap->nGRANallocationRetentionPriority;
  out->arp.prio = arp->priorityLevel;
  out->arp.preempt_cap = arp->pre_emptionCapability;
  out->arp.preempt_vuln = arp->pre_emptionVulnerability;

  // Decode GBR QoS Flow Information (optional)
  if (f1ap->gBR_QoS_Flow_Information != NULL) {
    const F1AP_GBR_QoSFlowInformation_t *gbr_ie = f1ap->gBR_QoS_Flow_Information;
    out->gbr_qos_flow_information = calloc_or_fail(1, sizeof(gbr_qos_flow_information_t));

    // Bit rates: ASN.1 INTEGER_t -> uint64_t
    asn_INTEGER2ulong(&gbr_ie->maxFlowBitRateDownlink, &out->gbr_qos_flow_information->dl.maximumFlowBitRate);
    asn_INTEGER2ulong(&gbr_ie->maxFlowBitRateUplink, &out->gbr_qos_flow_information->ul.maximumFlowBitRate);
    asn_INTEGER2ulong(&gbr_ie->guaranteedFlowBitRateDownlink, &out->gbr_qos_flow_information->dl.guaranteedFlowBitRate);
    asn_INTEGER2ulong(&gbr_ie->guaranteedFlowBitRateUplink, &out->gbr_qos_flow_information->ul.guaranteedFlowBitRate);
  }

  return true;
}

f1ap_qos_flow_param_t cp_qos_flow_param(const f1ap_qos_flow_param_t *orig)
{
  f1ap_qos_flow_param_t cp = {
    .qos_type = orig->qos_type,
    // dyn/nondyn below
    .arp = orig->arp,
  };
  if (cp.qos_type == NON_DYNAMIC) {
    cp.nondyn.fiveQI = orig->nondyn.fiveQI;
  } else {
    DevAssert(cp.qos_type == DYNAMIC);
    f1ap_dynamic_5qi_t *cpdyn = &cp.dyn;
    const f1ap_dynamic_5qi_t *odyn = &orig->dyn;
    cpdyn->prio = odyn->prio;
    cpdyn->pdb = odyn->pdb;
    cpdyn->per.scalar = odyn->per.scalar;
    cpdyn->per.exponent = odyn->per.exponent;
    if (odyn->delay_critical)
      _F1_MALLOC(cpdyn->delay_critical, *odyn->delay_critical);
    if (odyn->avg_win)
      _F1_MALLOC(cpdyn->avg_win, *odyn->avg_win);
  }
  // Copy GBR information
  if (orig->gbr_qos_flow_information != NULL) {
    cp.gbr_qos_flow_information = calloc_or_fail(1, sizeof(*cp.gbr_qos_flow_information));
    *cp.gbr_qos_flow_information = *orig->gbr_qos_flow_information;
  }
  return cp;
}

static bool eq_qos_flow_param(const f1ap_qos_flow_param_t *a, const f1ap_qos_flow_param_t *b)
{
  _EQ_CHECK_INT(a->qos_type, b->qos_type);
  if (a->qos_type == NON_DYNAMIC) {
    _EQ_CHECK_INT(a->nondyn.fiveQI, b->nondyn.fiveQI);
  } else {
    DevAssert(a->qos_type == DYNAMIC);
    DevAssert(b->qos_type == DYNAMIC);
    const f1ap_dynamic_5qi_t *adyn = &a->dyn, *bdyn = &b->dyn;
    _EQ_CHECK_INT(adyn->prio, bdyn->prio);
    _EQ_CHECK_INT(adyn->pdb, bdyn->pdb);
    _EQ_CHECK_INT(adyn->per.scalar, bdyn->per.scalar);
    _EQ_CHECK_INT(adyn->per.exponent, bdyn->per.exponent);
    _EQ_CHECK_OPTIONAL_IE(adyn, bdyn, delay_critical, _EQ_CHECK_INT);
    _EQ_CHECK_OPTIONAL_IE(adyn, bdyn, avg_win, _EQ_CHECK_INT);
  }
  _EQ_CHECK_INT(a->arp.prio, b->arp.prio);
  _EQ_CHECK_INT(a->arp.preempt_cap, b->arp.preempt_cap);
  _EQ_CHECK_INT(a->arp.preempt_vuln, b->arp.preempt_vuln);
  if (a->gbr_qos_flow_information != NULL && b->gbr_qos_flow_information != NULL) {
    const gbr_qos_flow_information_t *agbr = a->gbr_qos_flow_information;
    const gbr_qos_flow_information_t *bgbr = b->gbr_qos_flow_information;
    _EQ_CHECK_LONG(agbr->dl.guaranteedFlowBitRate, bgbr->dl.guaranteedFlowBitRate);
    _EQ_CHECK_LONG(agbr->dl.maximumFlowBitRate, bgbr->dl.maximumFlowBitRate);
    _EQ_CHECK_LONG(agbr->ul.guaranteedFlowBitRate, bgbr->ul.guaranteedFlowBitRate);
    _EQ_CHECK_LONG(agbr->ul.maximumFlowBitRate, bgbr->ul.maximumFlowBitRate);
  }
  return true;
}

static void free_qos_flow_param(f1ap_qos_flow_param_t *p)
{
  if (p->qos_type == NON_DYNAMIC) {
    // nothing to free
  } else {
    DevAssert(p->qos_type == DYNAMIC);
    free(p->dyn.delay_critical);
    free(p->dyn.avg_win);
  }
  free(p->gbr_qos_flow_information);
  // arp: nothing to free
}

static F1AP_DRB_Information_t encode_drb_info_nr(const f1ap_drb_info_nr_t *drb)
{
  F1AP_DRB_Information_t f1ap = {0};

  f1ap.dRB_QoS = encode_qos_flow_param(&drb->drb_qos);
  f1ap.sNSSAI = encode_nssai(&drb->nssai);

  for (int i = 0; i < drb->flows_len; ++i) {
    asn1cSequenceAdd(f1ap.flows_Mapped_To_DRB_List.list, F1AP_Flows_Mapped_To_DRB_Item_t, it);
    it->qoSFlowIdentifier = drb->flows[i].qfi;
    it->qoSFlowLevelQoSParameters = encode_qos_flow_param(&drb->flows[i].param);
  }

  return f1ap;
}

static bool decode_drb_info_nr(const F1AP_DRB_Information_t *f1ap, f1ap_drb_info_nr_t *out)
{
  _F1_CHECK_EXP(decode_qos_flow_param(&f1ap->dRB_QoS, &out->drb_qos));
  out->nssai = decode_nssai(&f1ap->sNSSAI);
  out->flows_len = f1ap->flows_Mapped_To_DRB_List.list.count;
  out->flows = calloc_or_fail(out->flows_len, sizeof(*out->flows));
  for (int i = 0; i < out->flows_len; ++i) {
    const F1AP_Flows_Mapped_To_DRB_Item_t *it = f1ap->flows_Mapped_To_DRB_List.list.array[i];
    out->flows[i].qfi = it->qoSFlowIdentifier;
    _F1_CHECK_EXP(decode_qos_flow_param(&it->qoSFlowLevelQoSParameters, &out->flows[i].param));
  }
  return true;
}

static f1ap_drb_info_nr_t cp_drb_info_nr(const f1ap_drb_info_nr_t *orig)
{
  f1ap_drb_info_nr_t cp = {
    .drb_qos = cp_qos_flow_param(&orig->drb_qos),
    .nssai = orig->nssai,
    .flows_len = orig->flows_len,
    // flows below
  };
  cp.flows = calloc_or_fail(cp.flows_len, sizeof(*cp.flows));
  for (int i = 0; i < cp.flows_len; ++i) {
    cp.flows[i].qfi = orig->flows[i].qfi;
    cp.flows[i].param = cp_qos_flow_param(&orig->flows[i].param);
  }
  return cp;
}

static bool eq_drb_info_nr(const f1ap_drb_info_nr_t *a, const f1ap_drb_info_nr_t *b)
{
  _F1_CHECK_EXP(eq_qos_flow_param(&a->drb_qos, &b->drb_qos));
  _EQ_CHECK_INT(a->nssai.sst, b->nssai.sst);
  _EQ_CHECK_INT(a->nssai.sd, b->nssai.sd);
  _EQ_CHECK_INT(a->flows_len, b->flows_len);
  for (int i = 0; i < a->flows_len; ++i) {
    _EQ_CHECK_INT(a->flows[i].qfi, b->flows[i].qfi);
    _F1_CHECK_EXP(eq_qos_flow_param(&a->flows[i].param, &b->flows[i].param));
  }
  return true;
}

static void free_drb_info_nr(f1ap_drb_info_nr_t *p)
{
  free_qos_flow_param(&p->drb_qos);
  // nssai: nothing
  for (int i = 0; i < p->flows_len; ++i)
    free_qos_flow_param(&p->flows[i].param);
  free(p->flows);
}

static F1AP_RLCMode_t rlc_mode_to_asn1(f1ap_rlc_mode_t mode)
{
  switch (mode) {
    case F1AP_RLC_MODE_AM:
      return F1AP_RLCMode_rlc_am;
    case F1AP_RLC_MODE_UM_BIDIR:
      return F1AP_RLCMode_rlc_um_bidirectional;
    default:
      break;
  }
  AssertFatal(false, "unsupported RLC Mode %d received\n", mode);
  return F1AP_RLCMode_rlc_am; // won't be reached
}

static bool rlc_mode_from_asn1(F1AP_RLCMode_t mode, f1ap_rlc_mode_t *out)
{
  switch (mode) {
    case F1AP_RLCMode_rlc_am:
      *out = F1AP_RLC_MODE_AM;
      return true;
    case F1AP_RLCMode_rlc_um_bidirectional:
      *out = F1AP_RLC_MODE_UM_BIDIR;
      return true;
    default:
      PRINT_ERROR("unsupported RLC Mode %ld received: setting AM\n", mode);
      break;
  }
  return false;
}

static F1AP_UPTransportLayerInformation_t encode_up_tnl(const f1ap_up_tnl_t *tnl)
{
  F1AP_UPTransportLayerInformation_t tnl_info = { .present = F1AP_UPTransportLayerInformation_PR_gTPTunnel, };
  asn1cCalloc(tnl_info.choice.gTPTunnel, gtp_tnl);
  TRANSPORT_LAYER_ADDRESS_IPv4_TO_BIT_STRING(tnl->tl_address, &gtp_tnl->transportLayerAddress);
  INT32_TO_OCTET_STRING(tnl->teid, &gtp_tnl->gTP_TEID);
  return tnl_info;
}

static bool decode_up_tnl(const F1AP_UPTransportLayerInformation_t *tnl_info, f1ap_up_tnl_t *out)
{
  _EQ_CHECK_INT(tnl_info->present, F1AP_UPTransportLayerInformation_PR_gTPTunnel);
  const F1AP_GTPTunnel_t *gtp_tnl = tnl_info->choice.gTPTunnel;
  // only IPv4 transport layer addresses are supported
  _EQ_CHECK_INT((int)gtp_tnl->transportLayerAddress.size, 4);
  _EQ_CHECK_INT(gtp_tnl->transportLayerAddress.bits_unused, 0);
  BIT_STRING_TO_TRANSPORT_LAYER_ADDRESS_IPv4(&gtp_tnl->transportLayerAddress, out->tl_address);
  OCTET_STRING_TO_UINT32(&gtp_tnl->gTP_TEID, out->teid);
  return true;
}

static f1ap_up_tnl_t cp_up_tnl(const f1ap_up_tnl_t *orig)
{
  f1ap_up_tnl_t cp = { .teid = orig->teid, };
  memcpy(&cp.tl_address, &orig->tl_address, sizeof(orig->tl_address));
  return cp;
}

static bool eq_up_tnl(const f1ap_up_tnl_t *a, const f1ap_up_tnl_t *b)
{
  if (memcmp(&a->tl_address, &b->tl_address, sizeof(a->tl_address)) != 0) {
    PRINT_ERROR("tl_address mismatch\n");
    return false;
  }
  _EQ_CHECK_INT(a->teid, b->teid);
  return true;
}

static void free_up_tnl(const f1ap_up_tnl_t *tnl)
{
  // nothing to free
  UNUSED(tnl);
}

/* Encode a DRBs_ToSetup list for UE Context Setup Request, from f1ap_drb_to_setup_t. */
static F1AP_DRBs_ToBeSetup_List_t encode_drbs_to_setup(int n, const f1ap_drb_to_setup_t *drbs)
{
  F1AP_DRBs_ToBeSetup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    const f1ap_drb_to_setup_t *drb = &drbs[i];
    asn1cSequenceAdd(list, F1AP_DRBs_ToBeSetup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_DRBs_ToBeSetup_ItemIEs__value_PR_DRBs_ToBeSetup_Item;

    F1AP_DRBs_ToBeSetup_Item_t *it = &itie->value.choice.DRBs_ToBeSetup_Item;
    it->dRBID = drb->id;
    AssertFatal(drb->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
    it->qoSInformation.present = F1AP_QoSInformation_PR_choice_extension;
    F1AP_QoSInformation_ExtIEs_t *qos_ext_ie = calloc_or_fail(1, sizeof(*qos_ext_ie));
    it->qoSInformation.choice.choice_extension = (struct F1AP_ProtocolIE_SingleContainer *)qos_ext_ie;
    qos_ext_ie->id = F1AP_ProtocolIE_ID_id_DRB_Information;
    qos_ext_ie->criticality = F1AP_Criticality_ignore;
    qos_ext_ie->value.present = F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information;
    qos_ext_ie->value.choice.DRB_Information = encode_drb_info_nr(&drb->nr);

    /* 12.1.3 uLUPTNLInformation_ToBeSetup_List */
    for (int j = 0; j < drb->up_ul_tnl_len; j++) {
      DevAssert(drb->up_ul_tnl[j].teid > 0);
      asn1cSequenceAdd(it->uLUPTNLInformation_ToBeSetup_List.list, F1AP_ULUPTNLInformation_ToBeSetup_Item_t, tnl_it);
      tnl_it->uLUPTNLInformation = encode_up_tnl(&drb->up_ul_tnl[j]);
    }

    it->rLCMode = rlc_mode_to_asn1(drb->rlc_mode);

    F1AP_ProtocolExtensionContainer_11023P82_t *ext = calloc_or_fail(1, sizeof(*ext));
    it->iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)ext;
    asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ext_ie);
    ext_ie->id = F1AP_ProtocolIE_ID_id_DLPDCPSNLength;
    ext_ie->criticality = F1AP_Criticality_ignore;
    ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength;
    AssertFatal(drb->dl_pdcp_sn_len != NULL, "dl_pdcp_sn_len required for setup\n");
    ext_ie->extensionValue.choice.PDCPSNLength = *drb->dl_pdcp_sn_len;

    if (drb->ul_pdcp_sn_len) {
      asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ext_ie);
      ext_ie->id = F1AP_ProtocolIE_ID_id_ULPDCPSNLength;
      ext_ie->criticality = F1AP_Criticality_ignore;
      ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength_1;
      ext_ie->extensionValue.choice.PDCPSNLength_1 = *drb->ul_pdcp_sn_len;
    }
  }
  return list;
}

/* Encode a DRBs_ToBeSetupMod list for UE Context Modification Request, from
 * f1ap_drb_to_setup_t. The ToBeSetup/ToBeSetupMod lists are almost the same
 * (with the exception that one field is optional in the latter), so that we
 * can reuse the internal types, but the asn1c types are different. */
static F1AP_DRBs_ToBeSetupMod_List_t encode_drbs_to_setupmod(int n, const f1ap_drb_to_setup_t *drbs)
{
  F1AP_DRBs_ToBeSetupMod_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    const f1ap_drb_to_setup_t *drb = &drbs[i];
    asn1cSequenceAdd(list, F1AP_DRBs_ToBeSetupMod_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetupMod_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_DRBs_ToBeSetupMod_ItemIEs__value_PR_DRBs_ToBeSetupMod_Item;

    F1AP_DRBs_ToBeSetupMod_Item_t *it = &itie->value.choice.DRBs_ToBeSetupMod_Item;
    it->dRBID = drb->id;
    AssertFatal(drb->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
    it->qoSInformation.present = F1AP_QoSInformation_PR_choice_extension;
    F1AP_QoSInformation_ExtIEs_t *qos_ext_ie = calloc_or_fail(1, sizeof(*qos_ext_ie));
    it->qoSInformation.choice.choice_extension = (struct F1AP_ProtocolIE_SingleContainer *)qos_ext_ie;
    qos_ext_ie->id = F1AP_ProtocolIE_ID_id_DRB_Information;
    qos_ext_ie->criticality = F1AP_Criticality_ignore;
    qos_ext_ie->value.present = F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information;
    qos_ext_ie->value.choice.DRB_Information = encode_drb_info_nr(&drb->nr);

    /* 12.1.3 uLUPTNLInformation_ToBeSetupMod_List */
    for (int j = 0; j < drb->up_ul_tnl_len; j++) {
      DevAssert(drb->up_ul_tnl[j].teid > 0);
      asn1cSequenceAdd(it->uLUPTNLInformation_ToBeSetup_List.list, F1AP_ULUPTNLInformation_ToBeSetup_Item_t, tnl_it);
      tnl_it->uLUPTNLInformation = encode_up_tnl(&drb->up_ul_tnl[j]);
    }

    it->rLCMode = rlc_mode_to_asn1(drb->rlc_mode);

    if (!drb->dl_pdcp_sn_len && !drb->ul_pdcp_sn_len)
      continue;

    F1AP_ProtocolExtensionContainer_11023P82_t *ext = calloc_or_fail(1, sizeof(*ext));
    it->iE_Extensions = (struct F1AP_ProtocolExtensionContainer *)ext;
    if (drb->dl_pdcp_sn_len) {
      asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetupMod_ItemExtIEs_t, ext_ie);
      ext_ie->id = F1AP_ProtocolIE_ID_id_DLPDCPSNLength;
      ext_ie->criticality = F1AP_Criticality_ignore;
      ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetupMod_ItemExtIEs__extensionValue_PR_PDCPSNLength;
      ext_ie->extensionValue.choice.PDCPSNLength = *drb->dl_pdcp_sn_len;
    }

    if (drb->ul_pdcp_sn_len) {
      asn1cSequenceAdd(ext->list, F1AP_DRBs_ToBeSetupMod_ItemExtIEs_t, ext_ie);
      ext_ie->id = F1AP_ProtocolIE_ID_id_ULPDCPSNLength;
      ext_ie->criticality = F1AP_Criticality_ignore;
      ext_ie->extensionValue.present = F1AP_DRBs_ToBeSetupMod_ItemExtIEs__extensionValue_PR_PDCPSNLength_1;
      ext_ie->extensionValue.choice.PDCPSNLength_1 = *drb->ul_pdcp_sn_len;
    }
  }
  return list;
}

/* \brief Decode DRBS-ToBeSetup list from UE Context Setup request, into
 * f1ap_drb_to_setup_t. */
static bool decode_drbs_to_setup(const F1AP_DRBs_ToBeSetup_List_t *f1ap, int *n, f1ap_drb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_to_setup_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_ToBeSetup_ItemIEs_t *itie = (const F1AP_DRBs_ToBeSetup_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_DRBs_ToBeSetup_ItemIEs__value_PR_DRBs_ToBeSetup_Item);
    const F1AP_DRBs_ToBeSetup_Item_t *it = &itie->value.choice.DRBs_ToBeSetup_Item;

    f1ap_drb_to_setup_t *drb = &drbs[i];
    drb->id = it->dRBID;

    _EQ_CHECK_INT(it->qoSInformation.present, F1AP_QoSInformation_PR_choice_extension);
    const F1AP_QoSInformation_ExtIEs_t *qos_ext = (F1AP_QoSInformation_ExtIEs_t *)it->qoSInformation.choice.choice_extension;
    _EQ_CHECK_LONG(qos_ext->id, F1AP_ProtocolIE_ID_id_DRB_Information);
    _EQ_CHECK_INT(qos_ext->value.present, F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information);
    drb->qos_choice = F1AP_QOS_CHOICE_NR;
    _F1_CHECK_EXP(decode_drb_info_nr(&qos_ext->value.choice.DRB_Information, &drb->nr));

    drb->up_ul_tnl_len = it->uLUPTNLInformation_ToBeSetup_List.list.count;
    EQ_CHECK_GENERIC(drb->up_ul_tnl_len > 0 && drb->up_ul_tnl_len <= 2, "%d", drb->up_ul_tnl_len, 1);
    for (int j = 0; j < drb->up_ul_tnl_len; ++j) {
      const F1AP_ULUPTNLInformation_ToBeSetup_Item_t *it_tnl = it->uLUPTNLInformation_ToBeSetup_List.list.array[j];
      _F1_CHECK_EXP(decode_up_tnl(&it_tnl->uLUPTNLInformation, &drb->up_ul_tnl[j]));
    }

    _F1_CHECK_EXP(rlc_mode_from_asn1(it->rLCMode, &drb->rlc_mode));

    _F1_CHECK_EXP(it->iE_Extensions); // PDCP SN length is under extension, is mandatory
    const F1AP_ProtocolExtensionContainer_11023P82_t *ext = (const F1AP_ProtocolExtensionContainer_11023P82_t *)it->iE_Extensions;
    const F1AP_DRBs_ToBeSetup_ItemExtIEs_t *ie;
    F1AP_LIB_FIND_IE(F1AP_DRBs_ToBeSetup_ItemExtIEs_t, ie, &ext->list, F1AP_ProtocolIE_ID_id_DLPDCPSNLength, true);

    for (int j = 0; j < ext->list.count; ++j) {
      ie = ext->list.array[j];
      AssertError(ie != NULL, return false, "ext->list.array[j] is NULL");
      switch (ie->id) {
        case F1AP_ProtocolIE_ID_id_DLPDCPSNLength:
          _EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength);
          _F1_MALLOC(drb->dl_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength);
          break;
        case F1AP_ProtocolIE_ID_id_ULPDCPSNLength:
          _EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetup_ItemExtIEs__extensionValue_PR_PDCPSNLength_1);
          _F1_MALLOC(drb->ul_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength_1);
          break;
        default:
          PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
          break;
      }
    }
  }

  return true;
}

/* \brief Decode DRBS-ToBeSetupMod list from UE Context Modification Request, into
 * f1ap_drb_to_setup_t. The ToBeSetup/ToBeSetupMod lists are almost the same
 * (except for some optional types in the latter), so we reuse the internal
 * representation, but  the asn1c types are different. */
static bool decode_drbs_to_setupmod(const F1AP_DRBs_ToBeSetupMod_List_t *f1ap, int *n, f1ap_drb_to_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_to_setup_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_ToBeSetupMod_ItemIEs_t *itie = (const F1AP_DRBs_ToBeSetupMod_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_ToBeSetupMod_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_DRBs_ToBeSetupMod_ItemIEs__value_PR_DRBs_ToBeSetupMod_Item);
    const F1AP_DRBs_ToBeSetupMod_Item_t *it = &itie->value.choice.DRBs_ToBeSetupMod_Item;

    f1ap_drb_to_setup_t *drb = &drbs[i];
    drb->id = it->dRBID;

    _EQ_CHECK_INT(it->qoSInformation.present, F1AP_QoSInformation_PR_choice_extension);
    const F1AP_QoSInformation_ExtIEs_t *qos_ext = (F1AP_QoSInformation_ExtIEs_t *)it->qoSInformation.choice.choice_extension;
    _EQ_CHECK_LONG(qos_ext->id, F1AP_ProtocolIE_ID_id_DRB_Information);
    _EQ_CHECK_INT(qos_ext->value.present, F1AP_QoSInformation_ExtIEs__value_PR_DRB_Information);
    drb->qos_choice = F1AP_QOS_CHOICE_NR;
    _F1_CHECK_EXP(decode_drb_info_nr(&qos_ext->value.choice.DRB_Information, &drb->nr));

    drb->up_ul_tnl_len = it->uLUPTNLInformation_ToBeSetup_List.list.count;
    EQ_CHECK_GENERIC(drb->up_ul_tnl_len > 0 && drb->up_ul_tnl_len <= 2, "%d", drb->up_ul_tnl_len, 1);
    for (int j = 0; j < drb->up_ul_tnl_len; ++j) {
      const F1AP_ULUPTNLInformation_ToBeSetup_Item_t *it_tnl = it->uLUPTNLInformation_ToBeSetup_List.list.array[j];
      _F1_CHECK_EXP(decode_up_tnl(&it_tnl->uLUPTNLInformation, &drb->up_ul_tnl[j]));
    }

    _F1_CHECK_EXP(rlc_mode_from_asn1(it->rLCMode, &drb->rlc_mode));

    if (!it->iE_Extensions)
      continue;
    const F1AP_ProtocolExtensionContainer_11023P83_t *ext = (const F1AP_ProtocolExtensionContainer_11023P83_t *)it->iE_Extensions;
    const F1AP_DRBs_ToBeSetupMod_ItemExtIEs_t *ie;

    for (int j = 0; j < ext->list.count; ++j) {
      ie = ext->list.array[j];
      AssertError(ie != NULL, return false, "ext->list.array[j] is NULL");
      switch (ie->id) {
        case F1AP_ProtocolIE_ID_id_DLPDCPSNLength:
          _EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetupMod_ItemExtIEs__extensionValue_PR_PDCPSNLength);
          _F1_MALLOC(drb->dl_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength);
          break;
        case F1AP_ProtocolIE_ID_id_ULPDCPSNLength:
          _EQ_CHECK_INT(ie->extensionValue.present, F1AP_DRBs_ToBeSetupMod_ItemExtIEs__extensionValue_PR_PDCPSNLength_1);
          _F1_MALLOC(drb->ul_pdcp_sn_len, ie->extensionValue.choice.PDCPSNLength_1);
          break;
        default:
          PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
          break;
      }
    }
  }

  return true;
}

static f1ap_drb_to_setup_t cp_drb_to_setup(const f1ap_drb_to_setup_t *orig)
{
  f1ap_drb_to_setup_t cp = {
    .id = orig->id,
    .qos_choice = orig->qos_choice,
    // nr below
    .up_ul_tnl_len = orig->up_ul_tnl_len,
    .up_ul_tnl[0] = cp_up_tnl(&orig->up_ul_tnl[0]),
    .up_ul_tnl[1] = cp_up_tnl(&orig->up_ul_tnl[1]),
    .rlc_mode = orig->rlc_mode,
    // dl_pdcp_sn_len below
    // ul_pdcp_sn_len below
  };
  AssertFatal(cp.qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
  if (orig->qos_choice == F1AP_QOS_CHOICE_NR)
    cp.nr = cp_drb_info_nr(&orig->nr);
  if (orig->dl_pdcp_sn_len) {
    _F1_MALLOC(cp.dl_pdcp_sn_len, *orig->dl_pdcp_sn_len);
  }
  if (orig->ul_pdcp_sn_len) {
    _F1_MALLOC(cp.ul_pdcp_sn_len, *orig->ul_pdcp_sn_len);
  }
  return cp;
}

static bool eq_drb_to_setup(const f1ap_drb_to_setup_t *a, const f1ap_drb_to_setup_t *b)
{
  _EQ_CHECK_INT(a->id, b->id);
  _EQ_CHECK_INT(a->qos_choice, b->qos_choice);
  AssertFatal(a->qos_choice == F1AP_QOS_CHOICE_NR, "only NR QoS choice implemented\n");
  if (a->qos_choice == F1AP_QOS_CHOICE_NR)
    eq_drb_info_nr(&a->nr, &b->nr);
  _EQ_CHECK_INT(a->up_ul_tnl_len, b->up_ul_tnl_len);
  for (int i = 0; i < a->up_ul_tnl_len; ++i) {
    _F1_CHECK_EXP(eq_up_tnl(&a->up_ul_tnl[i], &b->up_ul_tnl[i]));
  }
  _EQ_CHECK_INT(a->rlc_mode, b->rlc_mode);
  _EQ_CHECK_OPTIONAL_IE(a, b, dl_pdcp_sn_len, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, ul_pdcp_sn_len, _EQ_CHECK_INT);
  return true;
}

static void free_drb_to_setup(f1ap_drb_to_setup_t *drb)
{
  if (drb->qos_choice == F1AP_QOS_CHOICE_NR)
    free_drb_info_nr(&drb->nr);
  for (int i = 0; i < drb->up_ul_tnl_len; ++i)
    free_up_tnl(&drb->up_ul_tnl[i]);
  free(drb->dl_pdcp_sn_len);
  free(drb->ul_pdcp_sn_len);
}

static F1AP_DRBs_Setup_List_t encode_drbs_setup(int n, const f1ap_drb_setup_t *drbs)
{
  F1AP_DRBs_Setup_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    const f1ap_drb_setup_t *drb = &drbs[i];
    asn1cSequenceAdd(list, F1AP_DRBs_Setup_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_Setup_Item;
    itie->criticality = F1AP_Criticality_ignore;
    itie->value.present = F1AP_DRBs_Setup_ItemIEs__value_PR_DRBs_Setup_Item;

    F1AP_DRBs_Setup_Item_t *it = &itie->value.choice.DRBs_Setup_Item;
    it->dRBID = drb->id;
    if (drb->lcid)
      asn1cCallocOne(it->lCID, *drb->lcid);
    for (int j = 0; j < drb->up_dl_tnl_len; ++j) {
      DevAssert(drb->up_dl_tnl[j].teid > 0);
      asn1cSequenceAdd(it->dLUPTNLInformation_ToBeSetup_List.list, F1AP_DLUPTNLInformation_ToBeSetup_Item_t, tnl_it);
      tnl_it->dLUPTNLInformation = encode_up_tnl(&drb->up_dl_tnl[j]);
    }
  }
  return list;
}

static F1AP_DRBs_SetupMod_List_t encode_drbs_setupmod(int n, const f1ap_drb_setup_t *drbs)
{
  F1AP_DRBs_SetupMod_List_t list = {0};
  for (int i = 0; i < n; ++i) {
    const f1ap_drb_setup_t *drb = &drbs[i];
    asn1cSequenceAdd(list, F1AP_DRBs_SetupMod_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_SetupMod_Item;
    itie->criticality = F1AP_Criticality_ignore;
    itie->value.present = F1AP_DRBs_SetupMod_ItemIEs__value_PR_DRBs_SetupMod_Item;

    F1AP_DRBs_SetupMod_Item_t *it = &itie->value.choice.DRBs_SetupMod_Item;
    it->dRBID = drb->id;
    if (drb->lcid)
      asn1cCallocOne(it->lCID, *drb->lcid);
    for (int j = 0; j < drb->up_dl_tnl_len; ++j) {
      DevAssert(drb->up_dl_tnl[j].teid > 0);
      asn1cSequenceAdd(it->dLUPTNLInformation_ToBeSetup_List.list, F1AP_DLUPTNLInformation_ToBeSetup_Item_t, tnl_it);
      tnl_it->dLUPTNLInformation = encode_up_tnl(&drb->up_dl_tnl[j]);
    }
  }
  return list;
}

static bool decode_drbs_setup(const F1AP_DRBs_Setup_List_t *f1ap, int *n, f1ap_drb_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_setup_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_Setup_ItemIEs_t *itie = (const F1AP_DRBs_Setup_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_Setup_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_DRBs_Setup_ItemIEs__value_PR_DRBs_Setup_Item);
    const F1AP_DRBs_Setup_Item_t *it = &itie->value.choice.DRBs_Setup_Item;

    f1ap_drb_setup_t *drb = &drbs[i];
    drb->id = it->dRBID;
    if (it->lCID)
      _F1_MALLOC(drb->lcid, *it->lCID);

    drb->up_dl_tnl_len = it->dLUPTNLInformation_ToBeSetup_List.list.count;
    EQ_CHECK_GENERIC(drb->up_dl_tnl_len > 0 && drb->up_dl_tnl_len <= 2, "%d", drb->up_dl_tnl_len, 1);
    for (int j = 0; j < drb->up_dl_tnl_len; ++j) {
      F1AP_DLUPTNLInformation_ToBeSetup_Item_t *it_tnl = it->dLUPTNLInformation_ToBeSetup_List.list.array[j];
      _F1_CHECK_EXP(decode_up_tnl(&it_tnl->dLUPTNLInformation, &drb->up_dl_tnl[j]));
    }
  }

  return true;
}

static bool decode_drbs_setupmod(const F1AP_DRBs_SetupMod_List_t *f1ap, int *n, f1ap_drb_setup_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_setup_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_SetupMod_ItemIEs_t *itie = (const F1AP_DRBs_SetupMod_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_SetupMod_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_DRBs_SetupMod_ItemIEs__value_PR_DRBs_SetupMod_Item);
    const F1AP_DRBs_SetupMod_Item_t *it = &itie->value.choice.DRBs_SetupMod_Item;

    f1ap_drb_setup_t *drb = &drbs[i];
    drb->id = it->dRBID;
    if (it->lCID)
      _F1_MALLOC(drb->lcid, *it->lCID);

    drb->up_dl_tnl_len = it->dLUPTNLInformation_ToBeSetup_List.list.count;
    EQ_CHECK_GENERIC(drb->up_dl_tnl_len > 0 && drb->up_dl_tnl_len <= 2, "%d", drb->up_dl_tnl_len, 1);
    for (int j = 0; j < drb->up_dl_tnl_len; ++j) {
      F1AP_DLUPTNLInformation_ToBeSetup_Item_t *it_tnl = it->dLUPTNLInformation_ToBeSetup_List.list.array[j];
      _F1_CHECK_EXP(decode_up_tnl(&it_tnl->dLUPTNLInformation, &drb->up_dl_tnl[j]));
    }
  }

  return true;
}

static f1ap_drb_setup_t cp_drb_setup(const f1ap_drb_setup_t *orig)
{
  f1ap_drb_setup_t cp = {
      .id = orig->id,
      .up_dl_tnl_len = orig->up_dl_tnl_len,
      .up_dl_tnl[0] = cp_up_tnl(&orig->up_dl_tnl[0]),
      .up_dl_tnl[1] = cp_up_tnl(&orig->up_dl_tnl[1]),
  };
  if (orig->lcid)
    _F1_MALLOC(cp.lcid, *orig->lcid);
  return cp;
}

static bool eq_drb_setup(const f1ap_drb_setup_t *a, const f1ap_drb_setup_t *b)
{
  _EQ_CHECK_INT(a->id, b->id);
  _EQ_CHECK_OPTIONAL_IE(a, b, lcid, _EQ_CHECK_INT);
  _EQ_CHECK_INT(a->up_dl_tnl_len, b->up_dl_tnl_len);
  for (int i = 0; i < a->up_dl_tnl_len; ++i)
    _F1_CHECK_EXP(eq_up_tnl(&a->up_dl_tnl[i], &b->up_dl_tnl[i]));
  return true;
}

static void free_drb_setup(f1ap_drb_setup_t *drb)
{
  free(drb->lcid);
}

static F1AP_DRBs_ToBeReleased_List_t encode_drbs_to_release(int n, const f1ap_drb_to_release_t *drbs)
{
  F1AP_DRBs_ToBeReleased_List_t list = {0};
  for (int i = 0; i < n; i++) {
    asn1cSequenceAdd(list, F1AP_DRBs_ToBeReleased_ItemIEs_t, itie);
    itie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeReleased_Item;
    itie->criticality = F1AP_Criticality_reject;
    itie->value.present = F1AP_DRBs_ToBeReleased_ItemIEs__value_PR_DRBs_ToBeReleased_Item;
    itie->value.choice.DRBs_ToBeReleased_Item.dRBID = drbs[i].id;
  }
  return list;
}

static bool decode_drbs_to_release(const F1AP_DRBs_ToBeReleased_List_t *f1ap, int *n, f1ap_drb_to_release_t **out)
{
  DevAssert(out != NULL);
  *out = NULL;
  int count = *n = f1ap->list.count;
  if (count == 0)
    return true;

  f1ap_drb_to_release_t *drbs = *out = calloc_or_fail(count, sizeof(*drbs));
  for (int i = 0; i < count; ++i) {
    const F1AP_DRBs_ToBeReleased_ItemIEs_t *itie = (F1AP_DRBs_ToBeReleased_ItemIEs_t *)f1ap->list.array[i];
    _EQ_CHECK_LONG(itie->id, F1AP_ProtocolIE_ID_id_DRBs_ToBeReleased_Item);
    _EQ_CHECK_INT(itie->value.present, F1AP_DRBs_ToBeReleased_ItemIEs__value_PR_DRBs_ToBeReleased_Item);
    drbs[i].id = itie->value.choice.DRBs_ToBeReleased_Item.dRBID;
  }

  return true;
}

static f1ap_drb_to_release_t cp_drb_to_release(const f1ap_drb_to_release_t *orig)
{
  f1ap_drb_to_release_t cp = {
    .id = orig->id,
  };
  return cp;
}

static bool eq_drb_to_release(const f1ap_drb_to_release_t *a, const f1ap_drb_to_release_t *b)
{
  _EQ_CHECK_INT(a->id, b->id);
  return true;
}

static void free_drb_to_release(f1ap_drb_to_release_t *drb)
{
  // nothing to free
  UNUSED(drb);
}

/**
 * @brief Encode F1 UE context setup request to ASN.1
 */
F1AP_F1AP_PDU_t *encode_ue_context_setup_req(const f1ap_ue_context_setup_req_t *req)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextSetup;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextSetupRequest;
  F1AP_UEContextSetupRequest_t *out = &tmp->value.choice.UEContextSetupRequest;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextSetupRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = req->gNB_CU_ue_id;

  /* optional: GNB_DU_UE_F1AP_ID */
  if (req->gNB_DU_ue_id) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
    ie->value.choice.GNB_DU_UE_F1AP_ID = *req->gNB_DU_ue_id;
  }

  /* mandatory: SpCell ID/NRCGI */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_SpCell_ID;
  ie3->criticality = F1AP_Criticality_reject;
  ie3->value.present = F1AP_UEContextSetupRequestIEs__value_PR_NRCGI;
  F1AP_NRCGI_t *nrcgi = &ie3->value.choice.NRCGI;
  const plmn_id_t *plmn = &req->plmn;
  MCC_MNC_TO_PLMNID(plmn->mcc,plmn->mnc,plmn->mnc_digit_length, &nrcgi->pLMN_Identity);
  NR_CELL_ID_TO_BIT_STRING(req->nr_cellid, &nrcgi->nRCellIdentity);

  /* mandatory: ServCellIndox */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie4);
  ie4->id = F1AP_ProtocolIE_ID_id_ServCellIndex;
  ie4->criticality = F1AP_Criticality_reject;
  ie4->value.present = F1AP_UEContextSetupRequestIEs__value_PR_ServCellIndex;
  ie4->value.choice.ServCellIndex = req->servCellIndex;

  /* mandatory: CUtoDURRCInformation */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie6);
  ie6->id = F1AP_ProtocolIE_ID_id_CUtoDURRCInformation;
  ie6->criticality = F1AP_Criticality_reject;
  ie6->value.present = F1AP_UEContextSetupRequestIEs__value_PR_CUtoDURRCInformation;
  ie6->value.choice.CUtoDURRCInformation = encode_cu_to_du_rrc_info(&req->cu_to_du_rrc_info);

  /* optional: SRB to be setup list */
  if (req->srbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_List;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_SRBs_ToBeSetup_List;
    ie->value.choice.SRBs_ToBeSetup_List = encode_srbs_to_setup(req->srbs_len, req->srbs);
  }

  /* optional: DRB to be setup list */
  if (req->drbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_List;
    ie->criticality = F1AP_Criticality_reject;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_DRBs_ToBeSetup_List;
    ie->value.choice.DRBs_ToBeSetup_List = encode_drbs_to_setup(req->drbs_len, req->drbs);
  }

  /* optional: RRC container */
  if (req->rrc_container) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_RRCContainer;
    OCTET_STRING_fromBuf(&ie->value.choice.RRCContainer, (const char *)req->rrc_container->buf, req->rrc_container->len);
  }

  /* conditional if DRB setup list: gNB-DU UE Aggregate Maximum Bit Rate Uplink */
  if (req->gnb_du_ue_agg_mbr_ul) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupRequestIEs_t, ie);
    ie->id = F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL;
    ie->criticality = F1AP_Criticality_ignore;
    ie->value.present = F1AP_UEContextSetupRequestIEs__value_PR_BitRate;
    asn_long2INTEGER(&ie->value.choice.BitRate, *req->gnb_du_ue_agg_mbr_ul);
  }

  return pdu;
}

/**
 * @brief Decode F1 UE Context Setup Request
 */
bool decode_ue_context_setup_req(const F1AP_F1AP_PDU_t *pdu, f1ap_ue_context_setup_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextSetupRequest_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextSetupRequest;
  F1AP_UEContextSetupRequestIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_SpCell_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_ServCellIndex, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_CUtoDURRCInformation, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        _F1_MALLOC(out->gNB_DU_ue_id, ie->value.choice.GNB_DU_UE_F1AP_ID);
        break;
      case F1AP_ProtocolIE_ID_id_SpCell_ID: {
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_NRCGI);
        const F1AP_NRCGI_t *nrcgi = &ie->value.choice.NRCGI;
        PLMNID_TO_MCC_MNC(&nrcgi->pLMN_Identity, out->plmn.mcc, out->plmn.mnc, out->plmn.mnc_digit_length);
        BIT_STRING_TO_NR_CELL_IDENTITY(&nrcgi->nRCellIdentity, out->nr_cellid);
        } break;
      case F1AP_ProtocolIE_ID_id_ServCellIndex:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_ServCellIndex);
        out->servCellIndex = ie->value.choice.ServCellIndex;
        break;
      case F1AP_ProtocolIE_ID_id_CUtoDURRCInformation:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_CUtoDURRCInformation);
        _F1_CHECK_EXP(decode_cu_to_du_rrc_info(&out->cu_to_du_rrc_info, &ie->value.choice.CUtoDURRCInformation));
        break;
      case F1AP_ProtocolIE_ID_id_SRBs_ToBeSetup_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_SRBs_ToBeSetup_List);
        _F1_CHECK_EXP(decode_srbs_to_setup(&ie->value.choice.SRBs_ToBeSetup_List, &out->srbs_len, &out->srbs));
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_ToBeSetup_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_DRBs_ToBeSetup_List);
        {
          // gNB-DU UE Aggregate Maximum Bit Rate Uplink is C-ifDRBSetup
          // below, check that gNB-DU UE AMBR UL IE is present if we have DRB
          // setup, as required by the spec, or return false otherwise (like in
          // the beginning of the function)
          F1AP_UEContextSetupRequestIEs_t *check_ie;
          F1AP_LIB_FIND_IE(F1AP_UEContextSetupRequestIEs_t, check_ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL, true);
        }
        _F1_CHECK_EXP(decode_drbs_to_setup(&ie->value.choice.DRBs_ToBeSetup_List, &out->drbs_len, &out->drbs));
        break;
      case F1AP_ProtocolIE_ID_id_RRCContainer:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_RRCContainer);
        out->rrc_container = malloc_or_fail(sizeof(*out->rrc_container));
        {
          OCTET_STRING_t *os = &ie->value.choice.RRCContainer;
          *out->rrc_container = create_byte_array(os->size, os->buf);
        }
        break;
      case F1AP_ProtocolIE_ID_id_GNB_DU_UE_AMBR_UL:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupRequestIEs__value_PR_BitRate);
        out->gnb_du_ue_agg_mbr_ul = malloc_or_fail(sizeof(*out->gnb_du_ue_agg_mbr_ul));
        asn_INTEGER2uint64(&ie->value.choice.BitRate, out->gnb_du_ue_agg_mbr_ul);
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Setup Request deep copy
 */
f1ap_ue_context_setup_req_t cp_ue_context_setup_req(const f1ap_ue_context_setup_req_t *orig)
{
  /* copy all mandatory fields that are not dynamic memory */
  f1ap_ue_context_setup_req_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .plmn = orig->plmn,
    .nr_cellid = orig->nr_cellid,
    .servCellIndex = orig->servCellIndex,
    .cu_to_du_rrc_info = cp_cu_to_du_rrc_info(&orig->cu_to_du_rrc_info),
  };
  if (orig->gNB_DU_ue_id)
    _F1_MALLOC(cp.gNB_DU_ue_id, *orig->gNB_DU_ue_id);
  if (orig->srbs_len > 0 && orig->srbs) {
    cp.srbs = calloc_or_fail(orig->srbs_len, sizeof(*cp.srbs));
    cp.srbs_len = orig->srbs_len;
    for (int i = 0; i < cp.srbs_len; ++i)
      cp.srbs[i] = cp_srb_to_setup(&orig->srbs[i]);
  }
  if (orig->drbs_len > 0 && orig->drbs) {
    cp.drbs = calloc_or_fail(orig->drbs_len, sizeof(*cp.drbs));
    cp.drbs_len = orig->drbs_len;
    for (int i = 0; i < cp.drbs_len; ++i)
      cp.drbs[i] = cp_drb_to_setup(&orig->drbs[i]);
  }
  CP_OPT_BYTE_ARRAY(cp.rrc_container, orig->rrc_container);
  if (orig->gnb_du_ue_agg_mbr_ul)
    _F1_MALLOC(cp.gnb_du_ue_agg_mbr_ul, *orig->gnb_du_ue_agg_mbr_ul);
  return cp;
}

/**
 * @brief F1 UE Context Setup Request equality check
 */
bool eq_ue_context_setup_req(const f1ap_ue_context_setup_req_t *a, const f1ap_ue_context_setup_req_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_OPTIONAL_IE(a, b, gNB_DU_ue_id, _EQ_CHECK_INT);
  _F1_CHECK_EXP(eq_f1ap_plmn(&a->plmn, &b->plmn));
  _EQ_CHECK_LONG(a->nr_cellid, b->nr_cellid);
  _EQ_CHECK_INT(a->servCellIndex, b->servCellIndex);
  _F1_CHECK_EXP(eq_cu_to_du_rrc_info(&a->cu_to_du_rrc_info, &b->cu_to_du_rrc_info));

  _EQ_CHECK_INT(a->srbs_len, b->srbs_len);
  _F1_CHECK_EXP(a->srbs_len == 0 || (a->srbs && b->srbs));
  for (int i = 0; i < a->srbs_len; ++i)
    _F1_CHECK_EXP(eq_srb_to_setup(&a->srbs[i], &b->srbs[i]));

  _EQ_CHECK_INT(a->drbs_len, b->drbs_len);
  _F1_CHECK_EXP(a->drbs_len == 0 || (a->drbs && b->drbs));
  for (int i = 0; i < a->drbs_len; ++i)
    _F1_CHECK_EXP(eq_drb_to_setup(&a->drbs[i], &b->drbs[i]));

  _EQ_CHECK_OPTIONAL_IE(a, b, rrc_container, eq_ba);

  _EQ_CHECK_OPTIONAL_IE(a, b, gnb_du_ue_agg_mbr_ul, _EQ_CHECK_LONG);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Setup Request
 */
void free_ue_context_setup_req(f1ap_ue_context_setup_req_t *req)
{
  free(req->gNB_DU_ue_id);
  free_cu_to_du_rrc_info(&req->cu_to_du_rrc_info);
  for (int i = 0; i < req->srbs_len; ++i)
    free_srb_to_setup(&req->srbs[i]);
  free(req->srbs);
  for (int i = 0; i < req->drbs_len; ++i)
    free_drb_to_setup(&req->drbs[i]);
  free(req->drbs);
  FREE_OPT_BYTE_ARRAY(req->rrc_container);
  free(req->gnb_du_ue_agg_mbr_ul);
}

/**
 * @brief Encode F1 UE context setup response to ASN.1
 */
F1AP_F1AP_PDU_t *encode_ue_context_setup_resp(const f1ap_ue_context_setup_resp_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextSetup;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_SuccessfulOutcome__value_PR_UEContextSetupResponse;
  F1AP_UEContextSetupResponse_t *out = &tmp->value.choice.UEContextSetupResponse;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextSetupResponseIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextSetupResponseIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* mandatory: DUtoCURRCInformation */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_DUtoCURRCInformation;
  ie3->criticality = F1AP_Criticality_reject;
  ie3->value.present = F1AP_UEContextSetupResponseIEs__value_PR_DUtoCURRCInformation;
  ie3->value.choice.DUtoCURRCInformation = encode_du_to_cu_rrc_info(&msg->du_to_cu_rrc_info);

  /* optional: C-RNTI */
  if (msg->crnti) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie4);
    ie4->id = F1AP_ProtocolIE_ID_id_C_RNTI;
    ie4->criticality = F1AP_Criticality_ignore;
    ie4->value.present = F1AP_UEContextSetupResponseIEs__value_PR_C_RNTI;
    ie4->value.choice.C_RNTI = *msg->crnti;
  }

  /* optional: DRB Setup List */
  if (msg->drbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie7);
    ie7->id = F1AP_ProtocolIE_ID_id_DRBs_Setup_List;
    ie7->criticality = F1AP_Criticality_ignore;
    ie7->value.present = F1AP_UEContextSetupResponseIEs__value_PR_DRBs_Setup_List;
    ie7->value.choice.DRBs_Setup_List = encode_drbs_setup(msg->drbs_len, msg->drbs);
  }

  /* optional: SRB Setup List */
  if (msg->srbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextSetupResponseIEs_t, ie11);
    ie11->id = F1AP_ProtocolIE_ID_id_SRBs_Setup_List;
    ie11->criticality = F1AP_Criticality_ignore;
    ie11->value.present = F1AP_UEContextSetupResponseIEs__value_PR_SRBs_Setup_List;
    ie11->value.choice.SRBs_Setup_List = encode_srbs_setup(msg->srbs_len, msg->srbs);
  }

  return pdu;
}

/**
 * @brief Decode F1 UE Context Setup Response
 */
bool decode_ue_context_setup_resp(const struct F1AP_F1AP_PDU *pdu, f1ap_ue_context_setup_resp_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextSetupResponse_t *in = &pdu->choice.successfulOutcome->value.choice.UEContextSetupResponse;
  F1AP_UEContextSetupResponseIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_UEContextSetupResponseIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupResponseIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextSetupResponseIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_DUtoCURRCInformation, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_DUtoCURRCInformation:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_DUtoCURRCInformation);
        _F1_CHECK_EXP(decode_du_to_cu_rrc_info(&out->du_to_cu_rrc_info, &ie->value.choice.DUtoCURRCInformation));
        break;
      case F1AP_ProtocolIE_ID_id_C_RNTI:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_C_RNTI);
        _F1_MALLOC(out->crnti, ie->value.choice.C_RNTI);
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_Setup_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_DRBs_Setup_List);
        _F1_CHECK_EXP(decode_drbs_setup(&ie->value.choice.DRBs_Setup_List, &out->drbs_len, &out->drbs));
        break;
      case F1AP_ProtocolIE_ID_id_SRBs_Setup_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextSetupResponseIEs__value_PR_SRBs_Setup_List);
        _F1_CHECK_EXP(decode_srbs_setup(&ie->value.choice.SRBs_Setup_List, &out->srbs_len, &out->srbs));
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Setup Response deep copy
 */
f1ap_ue_context_setup_resp_t cp_ue_context_setup_resp(const f1ap_ue_context_setup_resp_t *orig)
{
  f1ap_ue_context_setup_resp_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
      .du_to_cu_rrc_info = cp_du_to_cu_rrc_info(&orig->du_to_cu_rrc_info),
      // crnti below
      .drbs_len = orig->drbs_len,
      // drbs below
      .srbs_len = orig->srbs_len,
      // srbs below
  };
  if (orig->crnti)
    _F1_MALLOC(cp.crnti, *orig->crnti);
  if (orig->drbs_len > 0 && orig->drbs) {
    cp.drbs = calloc_or_fail(orig->drbs_len, sizeof(*cp.drbs));
    cp.drbs_len = orig->drbs_len;
    for (int i = 0; i < cp.drbs_len; ++i)
      cp.drbs[i] = cp_drb_setup(&orig->drbs[i]);
  }
  if (orig->srbs_len > 0 && orig->srbs) {
    cp.srbs = calloc_or_fail(orig->srbs_len, sizeof(*cp.srbs));
    cp.srbs_len = orig->srbs_len;
    for (int i = 0; i < cp.srbs_len; ++i)
      cp.srbs[i] = cp_srb_setup(&orig->srbs[i]);
  }
  return cp;
}

/**
 * @brief F1 UE Context Setup Response equality check
 */
bool eq_ue_context_setup_resp(const f1ap_ue_context_setup_resp_t *a, const f1ap_ue_context_setup_resp_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _F1_CHECK_EXP(eq_du_to_cu_rrc_info(&a->du_to_cu_rrc_info, &b->du_to_cu_rrc_info));
  _EQ_CHECK_OPTIONAL_IE(a, b, crnti, _EQ_CHECK_INT);

  _EQ_CHECK_INT(a->drbs_len, b->drbs_len);
  _F1_CHECK_EXP(a->drbs_len == 0 || (a->drbs && b->drbs));
  for (int i = 0; i < a->drbs_len; ++i)
    _F1_CHECK_EXP(eq_drb_setup(&a->drbs[i], &b->drbs[i]));

  _EQ_CHECK_INT(a->srbs_len, b->srbs_len);
  _F1_CHECK_EXP(a->srbs_len == 0 || (a->srbs && b->srbs));
  for (int i = 0; i < a->srbs_len; ++i)
    _F1_CHECK_EXP(eq_srb_setup(&a->srbs[i], &b->srbs[i]));

  return true;
}

/**
 * @brief Free Allocated F1 UE Context Setup Response
 */
void free_ue_context_setup_resp(f1ap_ue_context_setup_resp_t *resp)
{
  free_du_to_cu_rrc_info(&resp->du_to_cu_rrc_info);
  free(resp->crnti);
  for (int i = 0; i < resp->drbs_len; ++i)
    free_drb_setup(&resp->drbs[i]);
  free(resp->drbs);
  for (int i = 0; i < resp->srbs_len; ++i)
    free_srb_setup(&resp->srbs[i]);
  free(resp->srbs);
}

/**
 * @brief Encode F1 UE context modification request to ASN.1
 */
F1AP_F1AP_PDU_t *encode_ue_context_mod_req(const f1ap_ue_context_mod_req_t *req)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextModification;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextModificationRequest;
  F1AP_UEContextModificationRequest_t *out = &tmp->value.choice.UEContextModificationRequest;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextModificationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = req->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextModificationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = req->gNB_DU_ue_id;

  /* optional: SpCellID/NRCGI */
  if (req->plmn) {
    DevAssert(req->nr_cellid);
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie3);
    ie3->id = F1AP_ProtocolIE_ID_id_SpCell_ID;
    ie3->criticality = F1AP_Criticality_ignore;
    ie3->value.present = F1AP_UEContextModificationRequestIEs__value_PR_NRCGI;
    F1AP_NRCGI_t *nrcgi = &ie3->value.choice.NRCGI;
    const plmn_id_t *plmn = req->plmn;
    MCC_MNC_TO_PLMNID(plmn->mcc, plmn->mnc, plmn->mnc_digit_length, &nrcgi->pLMN_Identity);
    NR_CELL_ID_TO_BIT_STRING(*req->nr_cellid, &nrcgi->nRCellIdentity);
  }

  /* optional: ServCellIndex */
  if (req->servCellIndex) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie4);
    ie4->id = F1AP_ProtocolIE_ID_id_ServCellIndex;
    ie4->criticality = F1AP_Criticality_reject;
    ie4->value.present = F1AP_UEContextModificationRequestIEs__value_PR_ServCellIndex;
    ie4->value.choice.ServCellIndex = *req->servCellIndex;
  }

  /* optional: CUtoDURRCInformation */
  if (req->cu_to_du_rrc_info) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie6);
    ie6->id = F1AP_ProtocolIE_ID_id_CUtoDURRCInformation;
    ie6->criticality = F1AP_Criticality_reject;
    ie6->value.present = F1AP_UEContextModificationRequestIEs__value_PR_CUtoDURRCInformation;
    ie6->value.choice.CUtoDURRCInformation = encode_cu_to_du_rrc_info(req->cu_to_du_rrc_info);
  }

  /* optional: TransmissionActionIndicator */
  if (req->transm_action_ind) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie7);
    ie7->id = F1AP_ProtocolIE_ID_id_TransmissionActionIndicator;
    ie7->criticality = F1AP_Criticality_ignore;
    ie7->value.present = F1AP_UEContextModificationRequestIEs__value_PR_TransmissionActionIndicator;
    ie7->value.choice.TransmissionActionIndicator = *req->transm_action_ind;
    // Stop is guaranteed to be 0, by restart might be different from 1
    static_assert((int)F1AP_TransmissionActionIndicator_restart == (int)TransmActionInd_RESTART,
                  "mismatch of ASN.1 and internal representation\n");
  }

  /* optional: RRCRconfigurationCompleteIndicator */
  if (req->reconfig_compl) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie71);
    ie71->id = F1AP_ProtocolIE_ID_id_RRCReconfigurationCompleteIndicator;
    ie71->criticality = F1AP_Criticality_ignore;
    ie71->value.present = F1AP_UEContextModificationRequestIEs__value_PR_RRCReconfigurationCompleteIndicator;
    ie71->value.choice.RRCReconfigurationCompleteIndicator = *req->reconfig_compl == RRCreconf_success
                                                                 ? F1AP_RRCReconfigurationCompleteIndicator_true
                                                                 : F1AP_RRCReconfigurationCompleteIndicator_failure;
  }

  /* optional: RRC container */
  if (req->rrc_container) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie81);
    ie81->id = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie81->criticality = F1AP_Criticality_ignore;
    ie81->value.present = F1AP_UEContextModificationRequestIEs__value_PR_RRCContainer;
    OCTET_STRING_fromBuf(&ie81->value.choice.RRCContainer, (const char *)req->rrc_container->buf, req->rrc_container->len);
  }

  /* optional: gNB-DU Configuration Query */
  if (req->gNB_DU_Configuration_Query && *req->gNB_DU_Configuration_Query) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie82);
    ie82->id = F1AP_ProtocolIE_ID_id_GNB_DUConfigurationQuery;
    ie82->criticality = F1AP_Criticality_ignore;
    ie82->value.present = F1AP_UEContextModificationRequestIEs__value_PR_GNB_DUConfigurationQuery;
    ie82->value.choice.GNB_DUConfigurationQuery = F1AP_GNB_DUConfigurationQuery_true;
  }

  /* optional: SRBs_ToBeSetupMod */
  if (req->srbs_len > 0) {
    DevAssert(req->srbs);
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie11);
    ie11->id = F1AP_ProtocolIE_ID_id_SRBs_ToBeSetupMod_List;
    ie11->criticality = F1AP_Criticality_reject;
    ie11->value.present = F1AP_UEContextModificationRequestIEs__value_PR_SRBs_ToBeSetupMod_List;
    ie11->value.choice.SRBs_ToBeSetupMod_List = encode_srbs_to_setupmod(req->srbs_len, req->srbs);
  }

  /* optional: DRBs_ToBeSetupMod */
  if (req->drbs_len > 0) {
    DevAssert(req->drbs);
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie12);
    ie12->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeSetupMod_List;
    ie12->criticality = F1AP_Criticality_reject;
    ie12->value.present = F1AP_UEContextModificationRequestIEs__value_PR_DRBs_ToBeSetupMod_List;
    ie12->value.choice.DRBs_ToBeSetupMod_List = encode_drbs_to_setupmod(req->drbs_len, req->drbs);
  }

  /* optional: DRBs_ToBeReleased */
  if (req->drbs_rel_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie15);
    ie15->id = F1AP_ProtocolIE_ID_id_DRBs_ToBeReleased_List;
    ie15->criticality = F1AP_Criticality_reject;
    ie15->value.present = F1AP_UEContextModificationRequestIEs__value_PR_DRBs_ToBeReleased_List;
    ie15->value.choice.DRBs_ToBeReleased_List = encode_drbs_to_release(req->drbs_rel_len, req->drbs_rel);
  }

  /* optional: Lower Layer Presence Status Change */
  if (req->status) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationRequestIEs_t, ie17);
    ie17->id = F1AP_ProtocolIE_ID_id_LowerLayerPresenceStatusChange;
    ie17->criticality = F1AP_Criticality_ignore;
    ie17->value.present = F1AP_UEContextModificationRequestIEs__value_PR_LowerLayerPresenceStatusChange;
    ie17->value.choice.LowerLayerPresenceStatusChange = *req->status == LOWER_LAYERS_SUSPEND
                                                            ? F1AP_LowerLayerPresenceStatusChange_suspend_lower_layers
                                                            : F1AP_LowerLayerPresenceStatusChange_resume_lower_layers;
  }
  return pdu;
}

/**
 * @brief Decode F1 UE Context Modification Request
 */
bool decode_ue_context_mod_req(const F1AP_F1AP_PDU_t *pdu, f1ap_ue_context_mod_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextModificationRequest_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextModificationRequest;
  F1AP_UEContextModificationRequestIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_UEContextModificationRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextModificationRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_SpCell_ID: {
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_NRCGI);
        const F1AP_NRCGI_t *nrcgi = &ie->value.choice.NRCGI;
        out->plmn = malloc_or_fail(sizeof(*out->plmn));
        PLMNID_TO_MCC_MNC(&nrcgi->pLMN_Identity, out->plmn->mcc, out->plmn->mnc, out->plmn->mnc_digit_length);
        out->nr_cellid = malloc_or_fail(sizeof(*out->nr_cellid));
        BIT_STRING_TO_NR_CELL_IDENTITY(&nrcgi->nRCellIdentity, *out->nr_cellid);
        } break;
      case F1AP_ProtocolIE_ID_id_ServCellIndex:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_ServCellIndex);
        _F1_MALLOC(out->servCellIndex, ie->value.choice.ServCellIndex);
        break;
      case F1AP_ProtocolIE_ID_id_CUtoDURRCInformation:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_CUtoDURRCInformation);
        out->cu_to_du_rrc_info = calloc(1, sizeof(*out->cu_to_du_rrc_info));
        _F1_CHECK_EXP(decode_cu_to_du_rrc_info(out->cu_to_du_rrc_info, &ie->value.choice.CUtoDURRCInformation));
        break;
      case F1AP_ProtocolIE_ID_id_TransmissionActionIndicator:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_TransmissionActionIndicator);
        {
          TransmActionInd_t v = ie->value.choice.TransmissionActionIndicator == F1AP_TransmissionActionIndicator_stop
                                    ? TransmActionInd_STOP
                                    : TransmActionInd_RESTART;
          _F1_MALLOC(out->transm_action_ind, v);
        }
        break;
      case F1AP_ProtocolIE_ID_id_RRCReconfigurationCompleteIndicator:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_RRCReconfigurationCompleteIndicator);
        {
          ReconfigurationCompl_t v =
              ie->value.choice.RRCReconfigurationCompleteIndicator == F1AP_RRCReconfigurationCompleteIndicator_true
                  ? RRCreconf_success
                  : RRCreconf_failure;
          _F1_MALLOC(out->reconfig_compl, v);
        }
        break;
      case F1AP_ProtocolIE_ID_id_RRCContainer:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_RRCContainer);
        out->rrc_container = malloc_or_fail(sizeof(*out->rrc_container));
        {
          OCTET_STRING_t *os = &ie->value.choice.RRCContainer;
          *out->rrc_container = create_byte_array(os->size, os->buf);
        }
        break;
      case F1AP_ProtocolIE_ID_id_GNB_DUConfigurationQuery:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_GNB_DUConfigurationQuery);
        {
          bool v = ie->value.choice.GNB_DUConfigurationQuery == F1AP_GNB_DUConfigurationQuery_true;
          _F1_MALLOC(out->gNB_DU_Configuration_Query, v);
        }
        break;
      case F1AP_ProtocolIE_ID_id_SRBs_ToBeSetupMod_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_SRBs_ToBeSetupMod_List);
        _F1_CHECK_EXP(decode_srbs_to_setupmod(&ie->value.choice.SRBs_ToBeSetupMod_List, &out->srbs_len, &out->srbs));
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_ToBeSetupMod_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_DRBs_ToBeSetupMod_List);
        _F1_CHECK_EXP(decode_drbs_to_setupmod(&ie->value.choice.DRBs_ToBeSetupMod_List, &out->drbs_len, &out->drbs));
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_ToBeReleased_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_DRBs_ToBeReleased_List);
        _F1_CHECK_EXP(decode_drbs_to_release(&ie->value.choice.DRBs_ToBeReleased_List, &out->drbs_rel_len, &out->drbs_rel));
        break;
      case F1AP_ProtocolIE_ID_id_LowerLayerPresenceStatusChange:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationRequestIEs__value_PR_LowerLayerPresenceStatusChange);
        {
          lower_layer_status_t v = ie->value.choice.LowerLayerPresenceStatusChange == F1AP_LowerLayerPresenceStatusChange_suspend_lower_layers
              ? LOWER_LAYERS_SUSPEND : LOWER_LAYERS_RESUME;
          _F1_MALLOC(out->status, v);
        }
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Modification Request deep copy
 */
f1ap_ue_context_mod_req_t cp_ue_context_mod_req(const f1ap_ue_context_mod_req_t *orig)
{
  f1ap_ue_context_mod_req_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .gNB_DU_ue_id = orig->gNB_DU_ue_id,
    // .plmn, .nr_cellid, .servCellIndex below
    // .cu_to_du_rrc_info below
    // .transm_action_ind below
    // .reconfig_compl below
    // .rrc_container below
    .srbs_len = orig->srbs_len,
    // .srbs below
    .drbs_len = orig->drbs_len,
    // .drbs below
    // .status below
  };

  if (orig->plmn)
    _F1_MALLOC(cp.plmn, *orig->plmn);
  if (orig->nr_cellid)
    _F1_MALLOC(cp.nr_cellid, *orig->nr_cellid);
  if (orig->servCellIndex)
    _F1_MALLOC(cp.servCellIndex, *orig->servCellIndex);
  if (orig->cu_to_du_rrc_info)
    _F1_MALLOC(cp.cu_to_du_rrc_info, cp_cu_to_du_rrc_info(orig->cu_to_du_rrc_info));
  if (orig->transm_action_ind)
    _F1_MALLOC(cp.transm_action_ind, *orig->transm_action_ind);
  if (orig->reconfig_compl)
    _F1_MALLOC(cp.reconfig_compl, *orig->reconfig_compl);
  CP_OPT_BYTE_ARRAY(cp.rrc_container, orig->rrc_container);
  if (orig->srbs_len > 0) {
    DevAssert(orig->srbs);
    cp.srbs = calloc_or_fail(orig->srbs_len, sizeof(*cp.srbs));
    cp.srbs_len = orig->srbs_len;
    for (int i = 0; i < cp.srbs_len; ++i)
      cp.srbs[i] = cp_srb_to_setup(&orig->srbs[i]);
  }
  if (orig->drbs_len > 0) {
    DevAssert(orig->drbs);
    cp.drbs = calloc_or_fail(orig->drbs_len, sizeof(*cp.drbs));
    cp.drbs_len = orig->drbs_len;
    for (int i = 0; i < cp.drbs_len; ++i)
      cp.drbs[i] = cp_drb_to_setup(&orig->drbs[i]);
  }
  if (orig->drbs_rel_len > 0) {
    DevAssert(orig->drbs_rel);
    cp.drbs_rel = calloc_or_fail(orig->drbs_rel_len, sizeof(*cp.drbs_rel));
    cp.drbs_rel_len = orig->drbs_rel_len;
    for (int i = 0; i < cp.drbs_rel_len; ++i)
      cp.drbs_rel[i] = cp_drb_to_release(&orig->drbs_rel[i]);
  }
  if (orig->status)
    _F1_MALLOC(cp.status, *orig->status);
  if (orig->gNB_DU_Configuration_Query)
    _F1_MALLOC(cp.gNB_DU_Configuration_Query, *orig->gNB_DU_Configuration_Query);
  return cp;
}

/**
 * @brief F1 UE Context Modification Request equality check
 */
bool eq_ue_context_mod_req(const f1ap_ue_context_mod_req_t *a, const f1ap_ue_context_mod_req_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _EQ_CHECK_OPTIONAL_PTR(a, b, plmn);
  _F1_CHECK_EXP(!a->plmn || eq_f1ap_plmn(a->plmn, b->plmn));
  _EQ_CHECK_OPTIONAL_IE(a, b, nr_cellid, _EQ_CHECK_LONG);
  _EQ_CHECK_OPTIONAL_IE(a, b, servCellIndex, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_PTR(a, b, cu_to_du_rrc_info);
  _F1_CHECK_EXP(!a->cu_to_du_rrc_info || eq_cu_to_du_rrc_info(a->cu_to_du_rrc_info, b->cu_to_du_rrc_info));
  _EQ_CHECK_OPTIONAL_IE(a, b, transm_action_ind, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, reconfig_compl, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, rrc_container, eq_ba);

  _EQ_CHECK_INT(a->srbs_len, b->srbs_len);
  _F1_CHECK_EXP(a->srbs_len == 0 || (a->srbs && b->srbs));
  for (int i = 0; i < a->srbs_len; ++i)
    _F1_CHECK_EXP(eq_srb_to_setup(&a->srbs[i], &b->srbs[i]));

  _EQ_CHECK_INT(a->drbs_len, b->drbs_len);
  _F1_CHECK_EXP(a->drbs_len == 0 || (a->drbs && b->drbs));
  for (int i = 0; i < a->drbs_len; ++i)
    _F1_CHECK_EXP(eq_drb_to_setup(&a->drbs[i], &b->drbs[i]));

  _EQ_CHECK_INT(a->drbs_rel_len, b->drbs_rel_len);
  _F1_CHECK_EXP(a->drbs_rel_len == 0 || (a->drbs_rel_len && b->drbs_rel_len));
  for (int i = 0; i < a->drbs_rel_len; ++i)
    _F1_CHECK_EXP(eq_drb_to_release(&a->drbs_rel[i], &b->drbs_rel[i]));

  _EQ_CHECK_OPTIONAL_IE(a, b, status, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, gNB_DU_Configuration_Query, _EQ_CHECK_INT);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Modification Request
 */
void free_ue_context_mod_req(f1ap_ue_context_mod_req_t *req)
{
  free(req->plmn);
  free(req->nr_cellid);
  free(req->servCellIndex);
  if (req->cu_to_du_rrc_info)
    free_cu_to_du_rrc_info(req->cu_to_du_rrc_info);
  free(req->cu_to_du_rrc_info);
  free(req->transm_action_ind);
  free(req->reconfig_compl);
  FREE_OPT_BYTE_ARRAY(req->rrc_container);
  for (int i = 0; i < req->srbs_len; ++i)
    free_srb_to_setup(&req->srbs[i]);
  free(req->srbs);
  for (int i = 0; i < req->drbs_len; ++i)
    free_drb_to_setup(&req->drbs[i]);
  free(req->drbs);
  for (int i = 0; i < req->drbs_rel_len; ++i)
    free_drb_to_release(&req->drbs_rel[i]);
  free(req->drbs_rel);
  free(req->status);
  free(req->gNB_DU_Configuration_Query);
}

/**
 * @brief Encode F1 UE context modification response to ASN.1
 */
F1AP_F1AP_PDU_t *encode_ue_context_mod_resp(const f1ap_ue_context_mod_resp_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextModification;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_SuccessfulOutcome__value_PR_UEContextModificationResponse;
  F1AP_UEContextModificationResponse_t *out = &tmp->value.choice.UEContextModificationResponse;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationResponseIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextModificationResponseIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationResponseIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextModificationResponseIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* optional: DUtoCURRCInformation */
  if (msg->du_to_cu_rrc_info != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationResponseIEs_t, ie4);
    ie4->id = F1AP_ProtocolIE_ID_id_DUtoCURRCInformation;
    ie4->criticality = F1AP_Criticality_reject;
    ie4->value.present = F1AP_UEContextModificationResponseIEs__value_PR_DUtoCURRCInformation;
    ie4->value.choice.DUtoCURRCInformation = encode_du_to_cu_rrc_info(msg->du_to_cu_rrc_info);
  }

  /* optional: DRBs_Setup_List */
  if (msg->drbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationResponseIEs_t, ie5);
    ie5->id = F1AP_ProtocolIE_ID_id_DRBs_SetupMod_List;
    ie5->criticality = F1AP_Criticality_ignore;
    ie5->value.present = F1AP_UEContextModificationResponseIEs__value_PR_DRBs_SetupMod_List;
    ie5->value.choice.DRBs_SetupMod_List = encode_drbs_setupmod(msg->drbs_len, msg->drbs);
  }

  if (msg->srbs_len > 0) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextModificationResponseIEs_t, ie12);
    ie12->id = F1AP_ProtocolIE_ID_id_SRBs_SetupMod_List;
    ie12->criticality = F1AP_Criticality_ignore;
    ie12->value.present = F1AP_UEContextModificationResponseIEs__value_PR_SRBs_SetupMod_List;
    ie12->value.choice.SRBs_SetupMod_List = encode_srbs_setupmod(msg->srbs_len, msg->srbs);
  }

  return pdu;
}

/**
 * @brief Decode F1 UE Context modification Response
 */
bool decode_ue_context_mod_resp(const struct F1AP_F1AP_PDU *pdu, f1ap_ue_context_mod_resp_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextModificationResponse_t *in = &pdu->choice.successfulOutcome->value.choice.UEContextModificationResponse;
  F1AP_UEContextModificationResponseIEs_t *ie;

  F1AP_LIB_FIND_IE(F1AP_UEContextModificationResponseIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextModificationResponseIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationResponseIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationResponseIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_DUtoCURRCInformation:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationResponseIEs__value_PR_DUtoCURRCInformation);
        out->du_to_cu_rrc_info = calloc(1, sizeof(*out->du_to_cu_rrc_info));
        _F1_CHECK_EXP(decode_du_to_cu_rrc_info(out->du_to_cu_rrc_info, &ie->value.choice.DUtoCURRCInformation));
        break;
      case F1AP_ProtocolIE_ID_id_DRBs_SetupMod_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationResponseIEs__value_PR_DRBs_SetupMod_List);
        _F1_CHECK_EXP(decode_drbs_setupmod(&ie->value.choice.DRBs_SetupMod_List, &out->drbs_len, &out->drbs));
        break;
      case F1AP_ProtocolIE_ID_id_SRBs_SetupMod_List:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextModificationResponseIEs__value_PR_SRBs_SetupMod_List);
        _F1_CHECK_EXP(decode_srbs_setupmod(&ie->value.choice.SRBs_SetupMod_List, &out->srbs_len, &out->srbs));
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context modification Response deep copy
 */
f1ap_ue_context_mod_resp_t cp_ue_context_mod_resp(const f1ap_ue_context_mod_resp_t *orig)
{
  f1ap_ue_context_mod_resp_t cp = {
      .gNB_CU_ue_id = orig->gNB_CU_ue_id,
      .gNB_DU_ue_id = orig->gNB_DU_ue_id,
      // du_to_cu_rrc_info
      .drbs_len = orig->drbs_len,
      // drbs below
      .srbs_len = orig->srbs_len,
      // srbs below
  };

  if (orig->du_to_cu_rrc_info) {
    f1ap_du_to_cu_rrc_info_t du2cu = cp_du_to_cu_rrc_info(orig->du_to_cu_rrc_info);
    _F1_MALLOC(cp.du_to_cu_rrc_info, du2cu);
  }

  if (orig->drbs_len > 0) {
    DevAssert(orig->drbs);
    cp.drbs = calloc_or_fail(orig->drbs_len, sizeof(*cp.drbs));
    cp.drbs_len = orig->drbs_len;
    for (int i = 0; i < cp.drbs_len; ++i)
      cp.drbs[i] = cp_drb_setup(&orig->drbs[i]);
  }

  if (orig->srbs_len > 0) {
    DevAssert(orig->srbs);
    cp.srbs = calloc_or_fail(orig->srbs_len, sizeof(*cp.srbs));
    cp.srbs_len = orig->srbs_len;
    for (int i = 0; i < cp.srbs_len; ++i)
      cp.srbs[i] = cp_srb_setup(&orig->srbs[i]);
  }

  return cp;
}

/**
 * @brief F1 UE Context modification Response equality check
 */
bool eq_ue_context_mod_resp(const f1ap_ue_context_mod_resp_t *a, const f1ap_ue_context_mod_resp_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  //_EQ_CHECK_OPTIONAL_IE(a, b, du_to_cu_rrc_info, eq_du_to_cu_rrc_info);
  _EQ_CHECK_OPTIONAL_PTR(a, b, du_to_cu_rrc_info);
  _F1_CHECK_EXP(!a->du_to_cu_rrc_info || eq_du_to_cu_rrc_info(a->du_to_cu_rrc_info, b->du_to_cu_rrc_info));

  _F1_CHECK_EXP(a->drbs_len == 0 || (a->drbs && b->drbs));
  for (int i = 0; i < a->drbs_len; ++i)
    _F1_CHECK_EXP(eq_drb_setup(&a->drbs[i], &b->drbs[i]));

  _EQ_CHECK_INT(a->srbs_len, b->srbs_len);
  _F1_CHECK_EXP(a->srbs_len == 0 || (a->srbs && b->srbs));
  for (int i = 0; i < a->srbs_len; ++i)
    _F1_CHECK_EXP(eq_srb_setup(&a->srbs[i], &b->srbs[i]));

  return true;
}

/**
 * @brief Free Allocated F1 UE Context modification Response
 */
void free_ue_context_mod_resp(f1ap_ue_context_mod_resp_t *resp)
{
  if (resp->du_to_cu_rrc_info)
    free_du_to_cu_rrc_info(resp->du_to_cu_rrc_info);
  free(resp->du_to_cu_rrc_info);
  for (int i = 0; i < resp->drbs_len; ++i)
    free_drb_setup(&resp->drbs[i]);
  free(resp->drbs);
  for (int i = 0; i < resp->srbs_len; ++i)
    free_srb_setup(&resp->srbs[i]);
  free(resp->srbs);
}

/*
 * @brief Encode F1 UE context release request to ASN.1
 */
struct F1AP_F1AP_PDU *encode_ue_context_rel_req(const f1ap_ue_context_rel_req_t *msg)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextReleaseRequest;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextReleaseRequest;
  F1AP_UEContextReleaseRequest_t *out = &tmp->value.choice.UEContextReleaseRequest;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseRequestIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextReleaseRequestIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = msg->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseRequestIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextReleaseRequestIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = msg->gNB_DU_ue_id;

  /* mandatory: Cause */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseRequestIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = F1AP_Criticality_ignore;
  ie3->value.present = F1AP_UEContextReleaseRequestIEs__value_PR_Cause;
  ie3->value.choice.Cause = encode_f1ap_cause(msg->cause, msg->cause_value);

  return pdu;
}

/**
 * @brief Decode F1 UE Context Release Request
 */
bool decode_ue_context_rel_req(const struct F1AP_F1AP_PDU *pdu, f1ap_ue_context_rel_req_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextReleaseRequest_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextReleaseRequest;
  F1AP_UEContextReleaseRequestIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseRequestIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseRequestIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseRequestIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_Cause:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseRequestIEs__value_PR_Cause);
        _F1_CHECK_EXP(decode_f1ap_cause(ie->value.choice.Cause, &out->cause, &out->cause_value));
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Release Request deep copy
 */
f1ap_ue_context_rel_req_t cp_ue_context_rel_req(const f1ap_ue_context_rel_req_t *orig)
{
  f1ap_ue_context_rel_req_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .gNB_DU_ue_id = orig->gNB_DU_ue_id,
    .cause = orig->cause,
    .cause_value = orig->cause_value,
  };
  return cp;
}

/**
 * @brief F1 UE Context Release Request equality check
 */
bool eq_ue_context_rel_req(const f1ap_ue_context_rel_req_t *a, const f1ap_ue_context_rel_req_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _EQ_CHECK_INT(a->cause, b->cause);
  _EQ_CHECK_LONG(a->cause_value, b->cause_value);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Release Request
 */
void free_ue_context_rel_req(f1ap_ue_context_rel_req_t *req)
{
  // nothing to free
  UNUSED(req);
}

/*
 * @brief Encode F1 UE context release command to ASN.1
 */
struct F1AP_F1AP_PDU *encode_ue_context_rel_cmd(const f1ap_ue_context_rel_cmd_t *cmd)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_initiatingMessage;
  asn1cCalloc(pdu->choice.initiatingMessage, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextRelease;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_InitiatingMessage__value_PR_UEContextReleaseCommand;
  F1AP_UEContextReleaseCommand_t *out = &tmp->value.choice.UEContextReleaseCommand;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = cmd->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = cmd->gNB_DU_ue_id;

  /* mandatory: Cause */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie3);
  ie3->id = F1AP_ProtocolIE_ID_id_Cause;
  ie3->criticality = F1AP_Criticality_ignore;
  ie3->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_Cause;
  ie3->value.choice.Cause = encode_f1ap_cause(cmd->cause, cmd->cause_value);

  /* optional: RRC Container */
  if (cmd->rrc_container != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie4);
    ie4->id = F1AP_ProtocolIE_ID_id_RRCContainer;
    ie4->criticality = F1AP_Criticality_ignore;
    ie4->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_RRCContainer;
    const byte_array_t *ba = cmd->rrc_container;
    OCTET_STRING_fromBuf(&ie4->value.choice.RRCContainer, (const char *)ba->buf, ba->len);

    // conditionally have SRBID if RRC Container
    AssertFatal(cmd->srb_id != NULL, "UEContextReleaseCommand SRB-ID C-ifRRCContainer violated\n");
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie5);
    ie5->id = F1AP_ProtocolIE_ID_id_SRBID;
    ie5->criticality = F1AP_Criticality_ignore;
    ie5->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_SRBID;
    ie5->value.choice.SRBID = *cmd->srb_id;
  }

  if (cmd->old_gNB_DU_ue_id != NULL) {
    asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCommandIEs_t, ie5);
    ie5->id = F1AP_ProtocolIE_ID_id_oldgNB_DU_UE_F1AP_ID;
    ie5->criticality = F1AP_Criticality_ignore;
    ie5->value.present = F1AP_UEContextReleaseCommandIEs__value_PR_GNB_DU_UE_F1AP_ID_1;
    ie5->value.choice.GNB_DU_UE_F1AP_ID_1 = *cmd->old_gNB_DU_ue_id;
  }

  return pdu;
}

/**
 * @brief Decode F1 UE Context Release Command
 */
bool decode_ue_context_rel_cmd(const struct F1AP_F1AP_PDU *pdu, f1ap_ue_context_rel_cmd_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextReleaseCommand_t *in = &pdu->choice.initiatingMessage->value.choice.UEContextReleaseCommand;
  F1AP_UEContextReleaseCommandIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCommandIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCommandIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCommandIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_Cause, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_Cause:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_Cause);
        _F1_CHECK_EXP(decode_f1ap_cause(ie->value.choice.Cause, &out->cause, &out->cause_value));
        break;
      case F1AP_ProtocolIE_ID_id_RRCContainer:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_RRCContainer);
        out->rrc_container = malloc_or_fail(sizeof(*out->rrc_container));
        {
          OCTET_STRING_t *os = &ie->value.choice.RRCContainer;
          *out->rrc_container = create_byte_array(os->size, os->buf);
        }
        break;
      case F1AP_ProtocolIE_ID_id_SRBID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_SRBID);
        {
          // SRB-ID
          F1AP_UEContextReleaseCommandIEs_t *check_ie;
          F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCommandIEs_t, check_ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_RRCContainer, true);
        }
        _F1_MALLOC(out->srb_id, ie->value.choice.SRBID);
        break;
      case F1AP_ProtocolIE_ID_id_oldgNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCommandIEs__value_PR_GNB_DU_UE_F1AP_ID_1);
        _F1_MALLOC(out->old_gNB_DU_ue_id, ie->value.choice.GNB_DU_UE_F1AP_ID_1);
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Release Command deep copy
 */
f1ap_ue_context_rel_cmd_t cp_ue_context_rel_cmd(const f1ap_ue_context_rel_cmd_t *orig)
{
  f1ap_ue_context_rel_cmd_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .gNB_DU_ue_id = orig->gNB_DU_ue_id,
    .cause = orig->cause,
    .cause_value = orig->cause_value,
  };
  CP_OPT_BYTE_ARRAY(cp.rrc_container, orig->rrc_container);
  if (orig->rrc_container) {
    AssertFatal(orig->srb_id != NULL, "UEContextReleaseCommand SRB-ID C-ifRRCContainer violated\n");
    _F1_MALLOC(cp.srb_id, *orig->srb_id);
  }
  if (orig->old_gNB_DU_ue_id)
    _F1_MALLOC(cp.old_gNB_DU_ue_id, *orig->old_gNB_DU_ue_id);
  return cp;
}

/**
 * @brief F1 UE Context Release Command equality check
 */
bool eq_ue_context_rel_cmd(const f1ap_ue_context_rel_cmd_t *a, const f1ap_ue_context_rel_cmd_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  _EQ_CHECK_INT(a->cause, b->cause);
  _EQ_CHECK_LONG(a->cause_value, b->cause_value);
  _EQ_CHECK_OPTIONAL_IE(a, b, rrc_container, eq_ba);
  _EQ_CHECK_OPTIONAL_IE(a, b, srb_id, _EQ_CHECK_INT);
  _EQ_CHECK_OPTIONAL_IE(a, b, old_gNB_DU_ue_id, _EQ_CHECK_INT);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Release Command
 */
void free_ue_context_rel_cmd(f1ap_ue_context_rel_cmd_t *cmd)
{
  FREE_OPT_BYTE_ARRAY(cmd->rrc_container);
  free(cmd->srb_id);
  free(cmd->old_gNB_DU_ue_id);
}

/*
 * @brief Encode F1 UE context release complete to ASN.1
 */
struct F1AP_F1AP_PDU *encode_ue_context_rel_cplt(const f1ap_ue_context_rel_cplt_t *cplt)
{
  F1AP_F1AP_PDU_t *pdu = calloc_or_fail(1, sizeof(*pdu));

  /* Message Type */
  pdu->present = F1AP_F1AP_PDU_PR_successfulOutcome;
  asn1cCalloc(pdu->choice.successfulOutcome, tmp);
  tmp->procedureCode = F1AP_ProcedureCode_id_UEContextRelease;
  tmp->criticality = F1AP_Criticality_reject;
  tmp->value.present = F1AP_SuccessfulOutcome__value_PR_UEContextReleaseComplete;
  F1AP_UEContextReleaseComplete_t *out = &tmp->value.choice.UEContextReleaseComplete;

  /* mandatory: GNB_CU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCompleteIEs_t, ie1);
  ie1->id = F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID;
  ie1->criticality = F1AP_Criticality_reject;
  ie1->value.present = F1AP_UEContextReleaseCompleteIEs__value_PR_GNB_CU_UE_F1AP_ID;
  ie1->value.choice.GNB_CU_UE_F1AP_ID = cplt->gNB_CU_ue_id;

  /* mandatory: GNB_DU_UE_F1AP_ID */
  asn1cSequenceAdd(out->protocolIEs.list, F1AP_UEContextReleaseCompleteIEs_t, ie2);
  ie2->id = F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID;
  ie2->criticality = F1AP_Criticality_reject;
  ie2->value.present = F1AP_UEContextReleaseCompleteIEs__value_PR_GNB_DU_UE_F1AP_ID;
  ie2->value.choice.GNB_DU_UE_F1AP_ID = cplt->gNB_DU_ue_id;

  return pdu;
}

/**
 * @brief Decode F1 UE Context Release Complete
 */
bool decode_ue_context_rel_cplt(const struct F1AP_F1AP_PDU *pdu, f1ap_ue_context_rel_cplt_t *out)
{
  DevAssert(out != NULL);
  memset(out, 0, sizeof(*out));

  F1AP_UEContextReleaseComplete_t *in = &pdu->choice.successfulOutcome->value.choice.UEContextReleaseComplete;
  F1AP_UEContextReleaseCompleteIEs_t *ie;
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCompleteIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID, true);
  F1AP_LIB_FIND_IE(F1AP_UEContextReleaseCompleteIEs_t, ie, &in->protocolIEs.list, F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID, true);

  for (int i = 0; i < in->protocolIEs.list.count; ++i) {
    ie = in->protocolIEs.list.array[i];
    AssertError(ie != NULL, return false, "in->protocolIEs.list.array[i] is NULL");
    switch (ie->id) {
      case F1AP_ProtocolIE_ID_id_gNB_CU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCompleteIEs__value_PR_GNB_CU_UE_F1AP_ID);
        out->gNB_CU_ue_id = ie->value.choice.GNB_CU_UE_F1AP_ID;
        break;
      case F1AP_ProtocolIE_ID_id_gNB_DU_UE_F1AP_ID:
        _EQ_CHECK_INT(ie->value.present, F1AP_UEContextReleaseCompleteIEs__value_PR_GNB_DU_UE_F1AP_ID);
        out->gNB_DU_ue_id = ie->value.choice.GNB_DU_UE_F1AP_ID;
        break;
      default:
        PRINT_ERROR("F1AP_ProtocolIE_ID_id %ld unknown, skipping\n", ie->id);
        break;
    }
  }

  return true;
}

/**
 * @brief F1 UE Context Release Complete deep copy
 */
f1ap_ue_context_rel_cplt_t cp_ue_context_rel_cplt(const f1ap_ue_context_rel_cplt_t *orig)
{
  f1ap_ue_context_rel_cplt_t cp = {
    .gNB_CU_ue_id = orig->gNB_CU_ue_id,
    .gNB_DU_ue_id = orig->gNB_DU_ue_id,
  };
  return cp;
}

/**
 * @brief F1 UE Context Release Complete equality check
 */
bool eq_ue_context_rel_cplt(const f1ap_ue_context_rel_cplt_t *a, const f1ap_ue_context_rel_cplt_t *b)
{
  _EQ_CHECK_INT(a->gNB_CU_ue_id, b->gNB_CU_ue_id);
  _EQ_CHECK_INT(a->gNB_DU_ue_id, b->gNB_DU_ue_id);
  return true;
}

/**
 * @brief Free Allocated F1 UE Context Release Complete
 */
void free_ue_context_rel_cplt(f1ap_ue_context_rel_cplt_t *cplt)
{
  // nothing to free
  UNUSED(cplt);
}
