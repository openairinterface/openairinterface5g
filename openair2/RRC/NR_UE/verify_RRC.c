/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
 
#include "rrc_proto.h"
#include "common/utils/bits.h"
 
static bool check_resourcesForInterference(const NR_CSI_MeasConfig_t *meas, NR_CSI_ResourceConfigId_t res_id, bool is_CSIIM)
{
  NR_CSI_ResourceConfig_t *res = NULL;
  if (meas->csi_ResourceConfigToAddModList) {
    for (int i = 0; i < meas->csi_ResourceConfigToAddModList->list.count; i++) {
      if (meas->csi_ResourceConfigToAddModList->list.array[i]->csi_ResourceConfigId == res_id)
        res = meas->csi_ResourceConfigToAddModList->list.array[i];
    }
  }
  if (!res) {
    LOG_E(NR_RRC, "No CSI-Resource matching with resourcesForInterference set in report config\n");
    return false;
  } else {
    if (is_CSIIM) {
      if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_csi_IM_ResourceSetList) {
        LOG_E(NR_RRC, "resourcesForInterference doesn't point to CSI-IM resource\n");
        return false;
      }
    } else {
      if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB
          || !res->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList) {
        LOG_E(NR_RRC, "NZP CSI-RS resourcesForInterference doesn't point to NZP CSI-RS resource\n");
        return false;
      }
    }
  }
  return true;
}

static bool check_resourcesForChannelMeasurement(const NR_CSI_MeasConfig_t *meas,
                                                 NR_CSI_ResourceConfigId_t res_id,
                                                 struct NR_CSI_ReportConfig__reportQuantity quantity)
{
  NR_CSI_ResourceConfig_t *res = NULL;
  if (meas->csi_ResourceConfigToAddModList) {
    for (int i = 0; i < meas->csi_ResourceConfigToAddModList->list.count; i++) {
      if (meas->csi_ResourceConfigToAddModList->list.array[i]->csi_ResourceConfigId == res_id)
        res = meas->csi_ResourceConfigToAddModList->list.array[i];
    }
  }
  if (!res) {
    LOG_E(NR_RRC, "No CSI-Resource matching with resourcesForChannelMeasurement set in report config\n");
    return false;
  } else {
    if (res->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB) {
      LOG_E(NR_RRC, "resourcesForChannelMeasurement doesn't point to CSI-RS-SSB resource\n");
      return false;
    } else {
      if (!res->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList
          && (quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP
          && quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_NOTHING
          && quantity.present != NR_CSI_ReportConfig__reportQuantity_PR_none)) {
        LOG_E(NR_RRC, "resourcesForChannelMeasurement is for CRI measurement but doen't point to an CRI-RS resource\n");
        return false;
      }
    }
  }
  return true;
}

static bool check_csi_resourceMapping_consistency(NR_CSI_RS_ResourceMapping_t resourceMapping)
{
  bool valid = true;
  struct NR_CSI_RS_ResourceMapping__density *density = &resourceMapping.density;
  switch(resourceMapping.frequencyDomainAllocation.present) {
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row1:
      if (resourceMapping.nrofPorts != NR_CSI_RS_ResourceMapping__nrofPorts_p1)
        valid = false;
      else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_noCDM)
        valid = false;
      else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_three
               && density->present != NR_CSI_RS_ResourceMapping__density_PR_NOTHING)
        valid = false;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row2:
      if (resourceMapping.nrofPorts != NR_CSI_RS_ResourceMapping__nrofPorts_p1)
        valid = false;
      else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_noCDM)
        valid = false;
      else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_dot5
               && density->present != NR_CSI_RS_ResourceMapping__density_PR_one)
        valid = false;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row4:
      if (resourceMapping.nrofPorts != NR_CSI_RS_ResourceMapping__nrofPorts_p4)
        valid = false;
      else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2)
        valid = false;
      else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_one
               && density->present != NR_CSI_RS_ResourceMapping__density_PR_NOTHING)
        valid = false;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_other:
      switch(resourceMapping.nrofPorts) {
        case NR_CSI_RS_ResourceMapping__nrofPorts_p1:
          valid = false;  // 1 port is row 1 or 2 not other
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p2:
          if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2)
            valid = false;
          else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_dot5
                   && density->present != NR_CSI_RS_ResourceMapping__density_PR_one)
            valid = false;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p4:
          // row 5 -> l0+1
          if (resourceMapping.firstOFDMSymbolInTimeDomain == 13)
            valid = false;
          else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2)
            valid = false;
          else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_one
                   && density->present != NR_CSI_RS_ResourceMapping__density_PR_NOTHING)
            valid = false;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p8: {
          uint32_t freq32 = 0;
          freq32 = (uint32_t)BIT_STRING_to_uint8(&resourceMapping.frequencyDomainAllocation.choice.other);
          if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2 || count_bits(&freq32, 1) != 4) {
            // row 7 and 8 -> l0+1
            if (resourceMapping.firstOFDMSymbolInTimeDomain == 13)
              valid = false;
          }
          if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2
              && resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            valid = false;
          else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_one
                   && density->present != NR_CSI_RS_ResourceMapping__density_PR_NOTHING)
            valid = false;
          break;
        }
        case NR_CSI_RS_ResourceMapping__nrofPorts_p12:
          if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2) {
            // row 10 -> l0+1
            if (resourceMapping.firstOFDMSymbolInTimeDomain == 13)
              valid = false;
          } else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2)
            valid = false;
          else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_one
                   && density->present != NR_CSI_RS_ResourceMapping__density_PR_NOTHING)
            valid = false;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p16:
          // l0 + 1 in all cases
          if (resourceMapping.firstOFDMSymbolInTimeDomain == 13)
            valid = false;
          else if (resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2
                   && resourceMapping.cdm_Type != NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            valid = false;
          else if (density->present != NR_CSI_RS_ResourceMapping__density_PR_dot5
                   && density->present != NR_CSI_RS_ResourceMapping__density_PR_one)
            valid = false;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p24:
        case NR_CSI_RS_ResourceMapping__nrofPorts_p32:
          if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_noCDM)
            valid = false;
          else if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2
              || resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_fd_CDM2) {
            // row 13, 14 and 16, 17 -> l0+1 and l1 (which should be larger than l0+1
            if (resourceMapping.firstOFDMSymbolInTimeDomain == 13)
              valid = false;
            if (!resourceMapping.firstOFDMSymbolInTimeDomain2
                || *resourceMapping.firstOFDMSymbolInTimeDomain2 <= resourceMapping.firstOFDMSymbolInTimeDomain + 1)
              valid = false;
          } else {
            // row 15 and 18 -> l0+3
            if (resourceMapping.firstOFDMSymbolInTimeDomain > 10)
              valid = false;
          }
          if (valid
              && (density->present != NR_CSI_RS_ResourceMapping__density_PR_dot5
              && density->present != NR_CSI_RS_ResourceMapping__density_PR_one))
            valid = false;
          break;
        default:
          valid = false;
      }
      break;
    default:
      valid = false;
  }
  if (!valid)
    return false;
  return true;
}

bool check_csi_report_consistency(const NR_CSI_MeasConfig_t *meas)
{
  if (!meas || !meas->csi_ReportConfigToAddModList)
    return true;
  for (int i = 0; i < meas->csi_ReportConfigToAddModList->list.count; i++) {
    NR_CSI_ReportConfig_t *csirep = meas->csi_ReportConfigToAddModList->list.array[i];
    if (!check_resourcesForChannelMeasurement(meas, csirep->resourcesForChannelMeasurement, csirep->reportQuantity))
      return false;
    if (csirep->csi_IM_ResourcesForInterference) {
      if (!check_resourcesForInterference(meas, *csirep->csi_IM_ResourcesForInterference, true))
        return false;
    }
    if (csirep->nzp_CSI_RS_ResourcesForInterference) {
      if (!check_resourcesForInterference(meas, *csirep->nzp_CSI_RS_ResourcesForInterference, false))
        return false;
    }
  }
  return true;
}

static bool check_csi_resource_consistency(const NR_CSI_MeasConfig_t *meas)
{
  if (!meas || !meas->nzp_CSI_RS_ResourceToAddModList)
    return true;
  for (int i = 0; i < meas->nzp_CSI_RS_ResourceToAddModList->list.count; i++) {
    NR_NZP_CSI_RS_Resource_t *res = meas->nzp_CSI_RS_ResourceToAddModList->list.array[i];
    if (!check_csi_resourceMapping_consistency(res->resourceMapping))
      return false;
  }
  return true;
}

static bool check_csi_MeasConfig(struct NR_SetupRelease_CSI_MeasConfig *csi_MeasConfig)
{
  const NR_CSI_MeasConfig_t *meas = csi_MeasConfig->choice.setup;
  if (!check_csi_resource_consistency(meas))
    return false;
  return true;
}

static bool check_srs_config(NR_SRS_Config_t *srs_Config)
{
  if (srs_Config->srs_ResourceToAddModList) {
    for (int i = 0; i < srs_Config->srs_ResourceToAddModList->list.count; i++) {
      NR_SRS_Resource_t *res = srs_Config->srs_ResourceToAddModList->list.array[i];
      int start = NR_SYMBOLS_PER_SLOT - res->resourceMapping.startPosition - 1;
      int num = 1 << res->resourceMapping.nrofSymbols;
      if (start + num > NR_SYMBOLS_PER_SLOT) {
        LOG_E(NR_RRC, "The configured SRS resource exceeds the slot boundary\n");
        return false;
      }
    }
  }
  return true;
}

static bool check_ul_bwp_config(NR_BWP_UplinkDedicated_t *bwp_Config)
{
  if (bwp_Config->srs_Config && bwp_Config->srs_Config->choice.setup) {
    if (!check_srs_config(bwp_Config->srs_Config->choice.setup))
      return false;
  }
  return true;
}

bool check_cellgroup_config(const NR_CellGroupConfig_t *cgConfig)
{
  if (cgConfig->spCellConfig && cgConfig->spCellConfig->spCellConfigDedicated) {
    NR_ServingCellConfig_t *spCellConfigDedicated = cgConfig->spCellConfig->spCellConfigDedicated;
    if (spCellConfigDedicated->csi_MeasConfig) {
      if (!check_csi_MeasConfig(spCellConfigDedicated->csi_MeasConfig))
        return false;
    }
    if (spCellConfigDedicated->uplinkConfig) {
      NR_UplinkConfig_t *ul_Config = spCellConfigDedicated->uplinkConfig;
      if (ul_Config->initialUplinkBWP) {
        if (!check_ul_bwp_config(ul_Config->initialUplinkBWP))
          return false;
      }
      if (ul_Config->uplinkBWP_ToAddModList) {
        for (int i = 0; i < ul_Config->uplinkBWP_ToAddModList->list.count; i++) {
          NR_BWP_Uplink_t *ul_bwp = ul_Config->uplinkBWP_ToAddModList->list.array[i];
          if (ul_bwp->bwp_Dedicated) {
            if (!check_ul_bwp_config(ul_bwp->bwp_Dedicated))
              return false;
          }
        }
      }
    }
  }

  if (cgConfig->ext1 && cgConfig->ext1->reportUplinkTxDirectCurrent) {
    LOG_E(NR_RRC, "Reporting of UplinkTxDirectCurrent not implemented\n");
    return false;
  }
  if (cgConfig->sCellToReleaseList) {
    LOG_E(NR_RRC, "Secondary serving cell release not implemented\n");
    return false;
  }
  if (cgConfig->sCellToAddModList) {
    LOG_E(NR_RRC, "Secondary serving cell addition not implemented\n");
    return false;
  }
  return true;
}
