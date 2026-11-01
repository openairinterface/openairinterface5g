/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "gnb_config.h"
#include <ctype.h>
#include <complex.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "BIT_STRING.h"
#include "L1_nr_paramdef.h"
#include "MACRLC_nr_paramdef.h"
#ifdef ENABLE_AERIAL
#include "aerial_paramdef.h"
#endif
#include "PHY/INIT/nr_phy_init.h"
#include "RRC_nr_paramsvalues.h"
#include "T.h"
#include "asn_SEQUENCE_OF.h"
#include "asn_codecs.h"
#include "asn_internal.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "common/5g_platform_types.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/openairinterface5g_limits.h"
#include "common/ran_context.h"
#include "common/utils/T/T.h"
#include "constr_TYPE.h"
#include "enb_paramdef.h"
#include "executables/softmodem-common.h"
#include "f1ap_common.h"
#include "gnb_paramdef.h"
#include "lib/f1ap_interface_management.h"
#include "F1AP_CauseRadioNetwork.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "nfapi_pnf.h"
#include "vnf/nfapi_nr_vnf.h"
#include "openair3/NGAP/ngap_gNB.h"
#include "nr_common.h"
#include "oai_asn1.h"
#include "prs_nr_paramdef.h"
#include "radio/ETHERNET/if_defs.h"
#include "rrc_messages_types.h"
#include "s1ap_messages_types.h"
#include "openair3/SCTP/sctp_default_values.h"
#include "seq_arr.h"
#include "common/utils/alg/find.h"
#include "uper_encoder.h"
#include "utils.h"
#include "x2ap_messages_types.h"
#include "gnb_config_common.h"
#include "positioning_nr_paramdef.h"
#include "f1ap_cu_task.h"
#include "openair3/NRPPA/nrppa_gNB_config.h"

static int DEFBANDS[] = {7};
static int DEFENBS[] = {0};
static int DEFBFW[] = {0x00007fff};
static int DEFRUTPCORES[] = {-1,-1,-1,-1};

/**
 * @brief Helper define to allocate and initialize SetupRelease structures
 */
#define INIT_SETUP_RELEASE(type, element)                                    \
  do {                                                                       \
    element = calloc_or_fail(1, sizeof(*element));                           \
    (element)->present = NR_SetupRelease_##type##_PR_setup;                  \
    (element)->choice.setup = CALLOC(1, sizeof(*((element)->choice.setup))); \
  } while (0)

/**
 * Allocate memory and initialize ServingCellConfigCommon struct members
 */
void prepare_scc(NR_ServingCellConfigCommon_t *scc)
{
  // NR_ServingCellConfigCommon
  scc->physCellId = calloc_or_fail(1, sizeof(*scc->physCellId));
  scc->n_TimingAdvanceOffset = calloc_or_fail(1, sizeof(*scc->n_TimingAdvanceOffset));
  scc->ssb_PositionsInBurst = calloc_or_fail(1, sizeof(*scc->ssb_PositionsInBurst));
  scc->ssb_periodicityServingCell = calloc_or_fail(1, sizeof(*scc->ssb_periodicityServingCell));
  scc->ssbSubcarrierSpacing = calloc_or_fail(1, sizeof(*scc->ssbSubcarrierSpacing));
  scc->tdd_UL_DL_ConfigurationCommon = calloc_or_fail(1, sizeof(*scc->tdd_UL_DL_ConfigurationCommon));
  struct NR_TDD_UL_DL_ConfigCommon *tdd = scc->tdd_UL_DL_ConfigurationCommon;
  tdd->pattern1.ext1 = calloc_or_fail(1, sizeof(*tdd->pattern1.ext1));
  tdd->pattern1.ext1->dl_UL_TransmissionPeriodicity_v1530 =
      calloc_or_fail(1, sizeof(*tdd->pattern1.ext1->dl_UL_TransmissionPeriodicity_v1530));
  scc->downlinkConfigCommon = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon));
  scc->downlinkConfigCommon->frequencyInfoDL = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon->frequencyInfoDL));
  scc->downlinkConfigCommon->initialDownlinkBWP = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon->initialDownlinkBWP));

  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  frequencyInfoDL->absoluteFrequencySSB = calloc_or_fail(1, sizeof(*frequencyInfoDL->absoluteFrequencySSB));
  NR_FreqBandIndicatorNR_t *dl_frequencyBandList = calloc_or_fail(1, sizeof(*dl_frequencyBandList));
  asn1cSeqAdd(&frequencyInfoDL->frequencyBandList.list, dl_frequencyBandList);
  struct NR_SCS_SpecificCarrier *dl_scs_SpecificCarrierList = calloc_or_fail(1, sizeof(*dl_scs_SpecificCarrierList));
  asn1cSeqAdd(&frequencyInfoDL->scs_SpecificCarrierList.list, dl_scs_SpecificCarrierList);

  INIT_SETUP_RELEASE(PDCCH_ConfigCommon, scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon);
  NR_SetupRelease_PDCCH_ConfigCommon_t *pdcch_ConfigCommon = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon;
  struct NR_PDCCH_ConfigCommon *pdcchCCSetup = pdcch_ConfigCommon->choice.setup;
  pdcchCCSetup->controlResourceSetZero = calloc_or_fail(1, sizeof(*pdcchCCSetup->controlResourceSetZero));
  pdcchCCSetup->searchSpaceZero = calloc_or_fail(1, sizeof(*pdcchCCSetup->searchSpaceZero));
  pdcchCCSetup->commonControlResourceSet = NULL;

  INIT_SETUP_RELEASE(PDSCH_ConfigCommon, scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon);
  NR_SetupRelease_PDSCH_ConfigCommon_t *pdsch_ConfigCommon = scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon;
  pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList =
      calloc_or_fail(1, sizeof(*pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList));

  scc->uplinkConfigCommon = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon));
  scc->uplinkConfigCommon->frequencyInfoUL = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon->frequencyInfoUL));
  scc->uplinkConfigCommon->initialUplinkBWP = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon->initialUplinkBWP));

  NR_FrequencyInfoUL_t *frequencyInfoUL = scc->uplinkConfigCommon->frequencyInfoUL;
  NR_FreqBandIndicatorNR_t *ul_frequencyBandList = calloc_or_fail(1, sizeof(*ul_frequencyBandList));
  frequencyInfoUL->frequencyBandList = calloc_or_fail(1, sizeof(*frequencyInfoUL->frequencyBandList));
  asn1cSeqAdd(&frequencyInfoUL->frequencyBandList->list, ul_frequencyBandList);
  frequencyInfoUL->absoluteFrequencyPointA = calloc_or_fail(1, sizeof(*frequencyInfoUL->absoluteFrequencyPointA));
  frequencyInfoUL->p_Max = calloc_or_fail(1, sizeof(*frequencyInfoUL->p_Max));

  struct NR_SCS_SpecificCarrier *ul_scs_SpecificCarrierList = calloc_or_fail(1, sizeof(*ul_scs_SpecificCarrierList));
  asn1cSeqAdd(&frequencyInfoUL->scs_SpecificCarrierList.list, ul_scs_SpecificCarrierList);

  INIT_SETUP_RELEASE(RACH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon);
  NR_SetupRelease_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon;
  struct NR_RACH_ConfigCommon *rachCCSetup = rach_ConfigCommon->choice.setup;
  rachCCSetup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB =
      calloc_or_fail(1, sizeof(*rachCCSetup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB));
  rachCCSetup->rsrp_ThresholdSSB = calloc_or_fail(1, sizeof(*rachCCSetup->rsrp_ThresholdSSB));
  rachCCSetup->msg1_SubcarrierSpacing = calloc_or_fail(1, sizeof(*rachCCSetup->msg1_SubcarrierSpacing));
  rachCCSetup->msg3_transformPrecoder = calloc_or_fail(1, sizeof(*rachCCSetup->msg3_transformPrecoder));
  *rachCCSetup->msg3_transformPrecoder = NR_PUSCH_Config__transformPrecoder_disabled;

  INIT_SETUP_RELEASE(PUSCH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon);
  NR_SetupRelease_PUSCH_ConfigCommon_t *pusch_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon;
  struct NR_PUSCH_ConfigCommon *puschCCSetup = pusch_ConfigCommon->choice.setup;
  puschCCSetup->groupHoppingEnabledTransformPrecoding = NULL;
  puschCCSetup->pusch_TimeDomainAllocationList = calloc_or_fail(1, sizeof(*puschCCSetup->pusch_TimeDomainAllocationList));
  puschCCSetup->msg3_DeltaPreamble = calloc_or_fail(1, sizeof(*puschCCSetup->msg3_DeltaPreamble));
  puschCCSetup->p0_NominalWithGrant = calloc_or_fail(1, sizeof(*puschCCSetup->p0_NominalWithGrant));

  INIT_SETUP_RELEASE(PUCCH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon);
  NR_SetupRelease_PUCCH_ConfigCommon_t *pucch_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon;
  struct NR_PUCCH_ConfigCommon *pucchCCSetup = pucch_ConfigCommon->choice.setup;
  pucchCCSetup->p0_nominal = calloc_or_fail(1, sizeof(*pucchCCSetup->p0_nominal));
  pucchCCSetup->pucch_ResourceCommon = calloc_or_fail(1, sizeof(*pucchCCSetup->pucch_ResourceCommon));
  pucchCCSetup->hoppingId = calloc_or_fail(1, sizeof(*pucchCCSetup->hoppingId));

  scc->ext2 = calloc_or_fail(1, sizeof(*scc->ext2));
  scc->ext2->ntn_Config_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17));
  scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17 = calloc_or_fail(1, sizeof(long));
  scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17));

  scc->ext2->ntn_Config_r17->ephemerisInfo_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ephemerisInfo_r17));
  scc->ext2->ntn_Config_r17->ta_Info_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ta_Info_r17));
  scc->ext2->ntn_Config_r17->ta_Info_r17->ta_Common_r17 = -1;
  scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17));

  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->present = NR_EphemerisInfo_r17_PR_positionVelocity_r17;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17 =
      calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17));
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionX_r17 = LONG_MAX;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionY_r17 = LONG_MAX;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionZ_r17 = LONG_MAX;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVX_r17 = LONG_MAX;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVY_r17 = LONG_MAX;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVZ_r17 = LONG_MAX;
}
void prepare_msgA_scc(NR_ServingCellConfigCommon_t *scc) {
  NR_BWP_UplinkCommon_t *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;
  // Add the struct ext1
  initialUplinkBWP->ext1 = calloc(1, sizeof(*initialUplinkBWP->ext1));
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16 = calloc(1, sizeof(*initialUplinkBWP->ext1->msgA_ConfigCommon_r16));
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16->present = NR_SetupRelease_MsgA_ConfigCommon_r16_PR_setup;
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup =
      calloc(1, sizeof(*initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup));
  NR_MsgA_ConfigCommon_r16_t *NR_MsgA_ConfigCommon_r16 = initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  NR_RACH_ConfigCommonTwoStepRA_r16_t *NR_Rach_CC_TwoStepRA = &NR_MsgA_ConfigCommon_r16->rach_ConfigCommonTwoStepRA_r16;
  NR_Rach_CC_TwoStepRA->rach_ConfigGenericTwoStepRA_r16.msgB_ResponseWindow_r16 =
      calloc(1, sizeof(long));
  NR_Rach_CC_TwoStepRA->msgA_RSRP_Threshold_r16 = calloc(1, sizeof(NR_RSRP_Range_t));

  NR_Rach_CC_TwoStepRA->rach_ConfigGenericTwoStepRA_r16.msgA_PRACH_ConfigurationIndex_r16 = calloc(1, sizeof(long));
  NR_Rach_CC_TwoStepRA->msgA_SSB_PerRACH_OccasionAndCB_PreamblesPerSSB_r16 =
      calloc_or_fail(1, sizeof(*NR_Rach_CC_TwoStepRA->msgA_SSB_PerRACH_OccasionAndCB_PreamblesPerSSB_r16));

  NR_MsgA_ConfigCommon_r16->msgA_PUSCH_Config_r16 = calloc(1, sizeof(NR_MsgA_PUSCH_Config_r16_t));
  NR_MsgA_PUSCH_Config_r16_t *msgA_PUSCH_Config_r16 = NR_MsgA_ConfigCommon_r16->msgA_PUSCH_Config_r16;
  msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16 = calloc(1, sizeof(NR_MsgA_PUSCH_Resource_r16_t));
  NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource = msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
  msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16 = calloc(1, sizeof(long));
  msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16 = calloc(1, sizeof(long));
  msgA_PUSCH_Resource->mappingTypeMsgA_PUSCH_r16 = calloc(1, sizeof(long));
}

// Section 4.1 in 38.213
NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR get_ssb_len(NR_ServingCellConfigCommon_t *scc)
{
  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  int scs = *scc->ssbSubcarrierSpacing;
  int nr_band = *frequencyInfoDL->frequencyBandList.list.array[0];
  long freq = from_nrarfcn(nr_band, scs, frequencyInfoDL->absoluteFrequencyPointA);
  frame_type_t frame_type = get_frame_type(nr_band, scs);
  if (scs == 0) {
    if (freq < 3000000000)
      return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
    else
      return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
  }
  if (scs == 1) {
    if (nr_band == 5 || nr_band == 24 || nr_band == 66 || frame_type == FDD) { // case B or paired spectrum
      if (freq < 3000000000)
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
      else
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
    }
    else {  // case C and unpaired spectrum
      if (freq < 1880000000)
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
      else
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
    }
  }
  // FR2
  return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_longBitmap;
}

static struct NR_SCS_SpecificCarrier configure_scs_carrier(int mu, int N_RB)
{
  struct NR_SCS_SpecificCarrier scs_sc = {0};
  scs_sc.offsetToCarrier = 0;
  scs_sc.subcarrierSpacing = mu;
  scs_sc.carrierBandwidth = N_RB;
  return scs_sc;
}

/**
 * Fill ServingCellConfigCommon struct members for unitary simulators
 */
void fill_scc_sim(NR_ServingCellConfigCommon_t *scc, uint64_t *ssb_bitmap, int N_RB_DL, int N_RB_UL, int mu_dl, int mu_ul)
{
  *scc->physCellId = 0;
  *scc->ssb_periodicityServingCell = NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms20;
  scc->dmrs_TypeA_Position = NR_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
  *scc->ssbSubcarrierSpacing = mu_dl;

  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  NR_TDD_UL_DL_ConfigCommon_t *tdd_UL_DL_Config = scc->tdd_UL_DL_ConfigurationCommon;
  switch (mu_dl) {
    case 0 :
      *frequencyInfoDL->absoluteFrequencySSB = 520432;
      *frequencyInfoDL->frequencyBandList.list.array[0] = 38;
      frequencyInfoDL->absoluteFrequencyPointA = 520000;
      tdd_UL_DL_Config->pattern1.dl_UL_TransmissionPeriodicity = NR_TDD_UL_DL_Pattern__dl_UL_TransmissionPeriodicity_ms10;
      break;
    case 1 :
      *frequencyInfoDL->absoluteFrequencySSB = 641032;
      *frequencyInfoDL->frequencyBandList.list.array[0] = 78;
      frequencyInfoDL->absoluteFrequencyPointA = 640000;
      tdd_UL_DL_Config->pattern1.dl_UL_TransmissionPeriodicity = NR_TDD_UL_DL_Pattern__dl_UL_TransmissionPeriodicity_ms5;
      break;
    case 3 :
      *frequencyInfoDL->absoluteFrequencySSB = 2071387;
      *frequencyInfoDL->frequencyBandList.list.array[0] = 257;
      frequencyInfoDL->absoluteFrequencyPointA = 2071003;
      tdd_UL_DL_Config->pattern1.dl_UL_TransmissionPeriodicity = NR_TDD_UL_DL_Pattern__dl_UL_TransmissionPeriodicity_ms1p25;
      break;
     default :
       AssertFatal(false, "Numerolgy %d not supported\n", mu_dl);
  }

  *frequencyInfoDL->scs_SpecificCarrierList.list.array[0] = configure_scs_carrier(mu_dl, N_RB_DL);
  struct NR_BWP_DownlinkCommon *initialDownlinkBWP = scc->downlinkConfigCommon->initialDownlinkBWP;

  initialDownlinkBWP->genericParameters.locationAndBandwidth = PRBalloc_to_locationandbandwidth(N_RB_DL, 0);
  initialDownlinkBWP->genericParameters.subcarrierSpacing = mu_dl;
  *initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero = 12;
  *initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero = 0;
  struct NR_PDSCH_TimeDomainResourceAllocation *timedomainresourceallocation0 =
      calloc_or_fail(1, sizeof(*timedomainresourceallocation0));
  timedomainresourceallocation0->mappingType=NR_PDSCH_TimeDomainResourceAllocation__mappingType_typeA;
  timedomainresourceallocation0->startSymbolAndLength = 54;
  asn1cSeqAdd(&initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list,
              timedomainresourceallocation0);
  struct NR_PDSCH_TimeDomainResourceAllocation *timedomainresourceallocation1 =
      calloc_or_fail(1, sizeof(*timedomainresourceallocation1));
  timedomainresourceallocation1->mappingType = NR_PDSCH_TimeDomainResourceAllocation__mappingType_typeA;
  timedomainresourceallocation1->startSymbolAndLength = 57;
  asn1cSeqAdd(&initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list,
              timedomainresourceallocation1);

  struct NR_FrequencyInfoUL *frequencyInfoUL = scc->uplinkConfigCommon->frequencyInfoUL;
  switch (mu_ul) {
    case 0 :
      *frequencyInfoUL->frequencyBandList->list.array[0] = 38;
      break;
    case 1 :
      *frequencyInfoUL->frequencyBandList->list.array[0] = 78;
      break;
    case 3 :
      *frequencyInfoUL->frequencyBandList->list.array[0] = 257;
      break;
     default :
       AssertFatal(false, "Numerolgy %d not supported\n", mu_ul);
  }
  *frequencyInfoUL->absoluteFrequencyPointA = -1;
  *frequencyInfoUL->scs_SpecificCarrierList.list.array[0] = configure_scs_carrier(mu_ul, N_RB_UL);
  *frequencyInfoUL->p_Max = 20;

  struct NR_BWP_UplinkCommon *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;
  initialUplinkBWP->genericParameters.locationAndBandwidth = PRBalloc_to_locationandbandwidth(N_RB_UL, 0);
  initialUplinkBWP->genericParameters.subcarrierSpacing = mu_ul;

  struct NR_SetupRelease_RACH_ConfigCommon *rach_ConfigCommon = initialUplinkBWP->rach_ConfigCommon;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.prach_ConfigurationIndex = mu_ul == 3 ? 52 : 98;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FDM = NR_RACH_ConfigGeneric__msg1_FDM_one;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FrequencyStart = 0;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.zeroCorrelationZoneConfig = 13;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleReceivedTargetPower = -118;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleTransMax = NR_RACH_ConfigGeneric__preambleTransMax_n10;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.powerRampingStep = NR_RACH_ConfigGeneric__powerRampingStep_dB2;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.ra_ResponseWindow = NR_RACH_ConfigGeneric__ra_ResponseWindow_sl20;
  rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present =
      NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_one;
  rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one =
      NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB__one_n64;
  rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer = NR_RACH_ConfigCommon__ra_ContentionResolutionTimer_sf64;
  *rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB = 19;
  rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present = NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR_l139;
  rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139 = 0;
  rach_ConfigCommon->choice.setup->restrictedSetConfig = NR_RACH_ConfigCommon__restrictedSetConfig_unrestrictedSet;
  *rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing = mu_ul;
  struct NR_SetupRelease_PUSCH_ConfigCommon *pusch_ConfigCommon = initialUplinkBWP->pusch_ConfigCommon;

  *pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble = 1;
  *pusch_ConfigCommon->choice.setup->p0_NominalWithGrant = -90;
  struct NR_SetupRelease_PUCCH_ConfigCommon *pucch_ConfigCommon = initialUplinkBWP->pucch_ConfigCommon;
  pucch_ConfigCommon->choice.setup->pucch_GroupHopping = NR_PUCCH_ConfigCommon__pucch_GroupHopping_neither;
  *pucch_ConfigCommon->choice.setup->hoppingId = 40;
  *pucch_ConfigCommon->choice.setup->p0_nominal = -90;
  scc->ssb_PositionsInBurst->present = NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
  *ssb_bitmap = 0xff;

  tdd_UL_DL_Config->referenceSubcarrierSpacing = mu_dl;

  NR_TDD_UL_DL_Pattern_t *p1 = &tdd_UL_DL_Config->pattern1;
  p1->nrofDownlinkSlots = 7;
  p1->nrofDownlinkSymbols = 6;
  p1->nrofUplinkSlots = 2;
  p1->nrofUplinkSymbols = 4;

  struct NR_TDD_UL_DL_Pattern *p2 = tdd_UL_DL_Config->pattern2;
  if (p2) {
    p2->dl_UL_TransmissionPeriodicity = 321;
    p2->nrofDownlinkSlots = -1;
    p2->nrofDownlinkSymbols = -1;
    p2->nrofUplinkSlots = -1;
    p2->nrofUplinkSymbols = -1;
  }

  scc->ss_PBCH_BlockPower = 20;
}

static void fix_tdd_pattern(NR_ServingCellConfigCommon_t *scc)
{
  NR_TDD_UL_DL_Pattern_t *pattern1 = &scc->tdd_UL_DL_ConfigurationCommon->pattern1;
  int pattern_ext = pattern1->dl_UL_TransmissionPeriodicity - 8;
  // Check if the pattern1 extension is configured and set the value accordingly
  if (pattern_ext >= 0) {
    *pattern1->ext1->dl_UL_TransmissionPeriodicity_v1530 = pattern_ext;
    pattern1->dl_UL_TransmissionPeriodicity = 5;
  } else {
    free(pattern1->ext1->dl_UL_TransmissionPeriodicity_v1530);
    free(pattern1->ext1);
    pattern1->ext1 = NULL;
  }
  struct NR_TDD_UL_DL_Pattern *pattern2 = scc->tdd_UL_DL_ConfigurationCommon->pattern2;
  if (pattern2 != NULL) {
    /* The pattern2 is not configured free the memory these shall not be encoded with default values in SIB1 */
    if (pattern2->dl_UL_TransmissionPeriodicity == -1) {
      free(pattern2);
      pattern2 = NULL;
    } else {
      // Check if the pattern2 extension is configured and set the value accordingly
      pattern_ext = pattern2->dl_UL_TransmissionPeriodicity - 8;
      if (pattern_ext >= 0) {
        pattern2->ext1 = calloc_or_fail(1, sizeof(*pattern2->ext1));
        pattern2->ext1->dl_UL_TransmissionPeriodicity_v1530 =
            CALLOC(1, sizeof(*pattern2->ext1->dl_UL_TransmissionPeriodicity_v1530));
        *pattern2->ext1->dl_UL_TransmissionPeriodicity_v1530 = pattern_ext;
        pattern2->dl_UL_TransmissionPeriodicity = 5;
      } else {
        pattern2->ext1 = NULL;
      }
    }
  }
}

static int get_ulsyncvalidityduration_enum_value(int val)
{
  int retval = -1;

  switch (val) {
    case 5:
    case 10:
    case 15:
    case 20:
    case 25:
    case 30:
    case 35:
    case 40:
    case 45:
    case 50:
    case 55:
      retval = val / 5 - 1;
      break;
    case 60:
    case 120:
    case 180:
    case 240:
      retval = 10 + val / 60;
      break;
    case 900:
      retval = 15;
      break;
    default:
      AssertFatal(1 == 0, "ulsyncvalidityDuration in SIB19 set to invalid value in Conf file\n");
      break;
  }
  return retval;
}

void fix_scc(NR_ServingCellConfigCommon_t *scc, uint64_t ssbmap)
{
  scc->ssb_PositionsInBurst->present = get_ssb_len(scc);
  uint8_t curr_bit;

  // changing endianicity of ssbmap and filling the ssb_PositionsInBurst buffers
  if(scc->ssb_PositionsInBurst->present == NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap) {
    scc->ssb_PositionsInBurst->choice.shortBitmap.size = 1;
    scc->ssb_PositionsInBurst->choice.shortBitmap.bits_unused = 4;
    scc->ssb_PositionsInBurst->choice.shortBitmap.buf = CALLOC(1, 1);
    scc->ssb_PositionsInBurst->choice.shortBitmap.buf[0] = 0;
    for (int i = 0; i < 8; i++) {
      if (i<scc->ssb_PositionsInBurst->choice.shortBitmap.bits_unused)
        curr_bit = 0;
      else
        curr_bit = (ssbmap >> (7 - i)) & 0x01;
      scc->ssb_PositionsInBurst->choice.shortBitmap.buf[0] |= curr_bit << i;
    }
  } else if(scc->ssb_PositionsInBurst->present == NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap) {
    scc->ssb_PositionsInBurst->choice.mediumBitmap.size = 1;
    scc->ssb_PositionsInBurst->choice.mediumBitmap.bits_unused = 0;
    scc->ssb_PositionsInBurst->choice.mediumBitmap.buf = CALLOC(1, 1);
    scc->ssb_PositionsInBurst->choice.mediumBitmap.buf[0] = 0;
    for (int i = 0; i < 8; i++)
      scc->ssb_PositionsInBurst->choice.mediumBitmap.buf[0] |= (((ssbmap >> (7 - i)) & 0x01) << i);
  } else {
    scc->ssb_PositionsInBurst->choice.longBitmap.size = 8;
    scc->ssb_PositionsInBurst->choice.longBitmap.bits_unused = 0;
    scc->ssb_PositionsInBurst->choice.longBitmap.buf = CALLOC(1, 8);
    for (int j = 0; j < 8; j++) {
       scc->ssb_PositionsInBurst->choice.longBitmap.buf[j] = 0;
       curr_bit = (ssbmap >> (j << 3)) & 0xff;
       for (int i = 0; i < 8; i++)
         scc->ssb_PositionsInBurst->choice.longBitmap.buf[j] |= (((curr_bit >> (7 - i)) & 0x01) << i);
    }
  }

  // fix SS0 and Coreset0
  NR_DownlinkConfigCommon_t *dlcc = scc->downlinkConfigCommon;
  NR_PDCCH_ConfigCommon_t *pdcch_cc = dlcc->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup;
  if((int)*pdcch_cc->searchSpaceZero == -1) {
    free(pdcch_cc->searchSpaceZero);
    pdcch_cc->searchSpaceZero = NULL;
  }
  if((int)*pdcch_cc->controlResourceSetZero == -1) {
    free(pdcch_cc->controlResourceSetZero);
    pdcch_cc->controlResourceSetZero = NULL;
  }

  // fix UL absolute frequency
  if ((int)*scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA==-1) {
     free(scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA);
     scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA = NULL;
  }

  NR_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
  long config_index = rach_ConfigCommon->rach_ConfigGeneric.prach_ConfigurationIndex;
  frequency_range_t freq_range = get_freq_range_from_arfcn(dlcc->frequencyInfoDL->absoluteFrequencyPointA);
  frame_type_t frame_type = get_frame_type((int)*dlcc->frequencyInfoDL->frequencyBandList.list.array[0], *scc->ssbSubcarrierSpacing);
  nr_prach_info_t prach_info =  get_nr_prach_occasion_info_from_index(config_index, freq_range, frame_type);
  AssertFatal(prach_info.start_symbol + prach_info.N_t_slot * prach_info.N_dur < 14,
              "PRACH with configuration index %ld goes to the last symbol of the slot, for optimal performance pick another index. "
              "See Tables 6.3.3.2-2 to 6.3.3.2-4 in 38.211\n",
              config_index);
  // default value for msg3 precoder is NULL (0 means enabled)
  if (*rach_ConfigCommon->msg3_transformPrecoder != 0) {
    free(rach_ConfigCommon->msg3_transformPrecoder);
    rach_ConfigCommon->msg3_transformPrecoder = NULL;
  }

  // by default, select ra_ResponseWindow automatically
  if (rach_ConfigCommon->rach_ConfigGeneric.ra_ResponseWindow < 0) {
    int mu = *scc->ssbSubcarrierSpacing;
    // will select: mu=0 => 4 (10 slots), mu=1 => 5 (20 slots), mu>=3 => 7 (80 slots)
    rach_ConfigCommon->rach_ConfigGeneric.ra_ResponseWindow = min(NR_RACH_ConfigGeneric__ra_ResponseWindow_sl80, NR_RACH_ConfigGeneric__ra_ResponseWindow_sl10 + mu);
  }
  DevAssert(rach_ConfigCommon->rach_ConfigGeneric.ra_ResponseWindow >= 0);

  if (frame_type == FDD) {
    ASN_STRUCT_FREE(asn_DEF_NR_TDD_UL_DL_ConfigCommon, scc->tdd_UL_DL_ConfigurationCommon);
    scc->tdd_UL_DL_ConfigurationCommon = NULL;
  } else { // TDD
    fix_tdd_pattern(scc);
  }

  if ((int)*rach_ConfigCommon->msg1_SubcarrierSpacing == -1) {
    free(rach_ConfigCommon->msg1_SubcarrierSpacing);
    rach_ConfigCommon->msg1_SubcarrierSpacing = NULL;
  }

  if (scc->uplinkConfigCommon->initialUplinkBWP->ext1
      && (int)scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16
                 ->msgA_PUSCH_ResourceGroupA_r16->msgA_PUSCH_TimeDomainOffset_r16
             == 0) {
    NR_BWP_UplinkCommon_t *ulcc = scc->uplinkConfigCommon->initialUplinkBWP;
    ASN_STRUCT_FREE(asn_DEF_NR_SetupRelease_MsgA_ConfigCommon_r16, ulcc->ext1->msgA_ConfigCommon_r16);
    free(ulcc->ext1);
    ulcc->ext1 = NULL;
  }

  if ((int)*scc->n_TimingAdvanceOffset == -1) {
    free(scc->n_TimingAdvanceOffset);
    scc->n_TimingAdvanceOffset = NULL;
  }

  // check pucch_ResourceConfig
  AssertFatal(*scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon < 2,
	      "pucch_ResourceConfig should be 0 or 1 for now\n");

  if (*scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17 == 0) {
    free(scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17);
    scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17 = NULL;
  } else {
    int val = get_ulsyncvalidityduration_enum_value(*scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17);
    *scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17 = val;
  }
  if (*scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 == 0) {
    free(scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17);
    scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 = NULL;
  }
  if (*scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17 == 0) {
    free(scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17);
    scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17 = NULL;
  }
  if (scc->ext2->ntn_Config_r17->ta_Info_r17->ta_Common_r17 == -1) {
    free(scc->ext2->ntn_Config_r17->ta_Info_r17->ta_CommonDrift_r17);
    free(scc->ext2->ntn_Config_r17->ta_Info_r17);
    scc->ext2->ntn_Config_r17->ta_Info_r17 = NULL;
  }
  if (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionX_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionY_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionZ_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVX_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVY_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVZ_r17 == LONG_MAX) {
    free(scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17);
    free(scc->ext2->ntn_Config_r17->ephemerisInfo_r17);
    scc->ext2->ntn_Config_r17->ephemerisInfo_r17 = NULL;
  }

  if (!scc->ext2->ntn_Config_r17->ntn_UlSyncValidityDuration_r17 &&
      !scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 &&
      !scc->ext2->ntn_Config_r17->ta_Info_r17 &&
      !scc->ext2->ntn_Config_r17->ephemerisInfo_r17) {
    free(scc->ext2->ntn_Config_r17);
    free(scc->ext2);
    scc->ext2 = NULL;
  }
}

static void verify_gnb_param_notset(paramdef_t *params, int paramidx, const char *paramname)
{
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  AssertFatal(!config_isparamset(params, paramidx),
              "Option \"%s." GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON
              ".%s\" is not allowed in this config, please remove it\n",
              aprefix,
              paramname);
}
static void verify_section_notset(configmodule_interface_t *cfg, char *aprefix, const char *secname)
{
  paramlist_def_t pl = {0};
  strncpy(pl.listname, secname, sizeof(pl.listname) - 1);
  config_getlist(cfg, &pl, NULL, 0, aprefix);
  AssertFatal(pl.numelt == 0,
              "Section \"%s%s%s\" not allowed in this config, please remove it\n",
              aprefix ? aprefix : "",
              aprefix ? "." : "",
              secname);
}
void RCconfig_verify(configmodule_interface_t *cfg, ngran_node_t node_type)
{
  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);
  int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal(num_gnbs == 1, "need to have a " GNB_CONFIG_STRING_GNB_LIST " section, but %d found\n", num_gnbs);
  GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL, GNBPARAMS_CHECK);
  paramdef_t *gnbp = GNBParamList.paramarray[0];

  if (NODE_IS_CU(node_type)) {
    // verify that there is no SCC and radio config in the case of CU
    verify_section_notset(cfg, GNB_CONFIG_STRING_GNB_LIST ".[0]", GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON);

    verify_gnb_param_notset(gnbp, GNB_PDSCH_ANTENNAPORTS_N1_IDX, GNB_CONFIG_STRING_PDSCHANTENNAPORTS_N1);
    verify_gnb_param_notset(gnbp, GNB_PDSCH_ANTENNAPORTS_N2_IDX, GNB_CONFIG_STRING_PDSCHANTENNAPORTS_N2);
    verify_gnb_param_notset(gnbp, GNB_PDSCH_ANTENNAPORTS_XP_IDX, GNB_CONFIG_STRING_PDSCHANTENNAPORTS_XP);
    verify_gnb_param_notset(gnbp, GNB_PUSCH_ANTENNAPORTS_IDX, GNB_CONFIG_STRING_PUSCHANTENNAPORTS);
    verify_gnb_param_notset(gnbp, GNB_MINRXTXTIME_IDX, GNB_CONFIG_STRING_MINRXTXTIME);
    verify_gnb_param_notset(gnbp, GNB_DO_TCI_IDX, GNB_CONFIG_STRING_DOTCI);
    verify_gnb_param_notset(gnbp, GNB_DO_CSIRS_IDX, GNB_CONFIG_STRING_DOCSIRS);
    verify_gnb_param_notset(gnbp, GNB_DO_SRS_IDX, GNB_CONFIG_STRING_DOSRS);
    verify_gnb_param_notset(gnbp, GNB_FORCE256QAMOFF_IDX, GNB_CONFIG_STRING_FORCE256QAMOFF);
    verify_gnb_param_notset(gnbp, GNB_MAXMIMOLAYERS_IDX, GNB_CONFIG_STRING_MAXMIMOLAYERS);
    verify_gnb_param_notset(gnbp, GNB_DISABLE_HARQ_IDX, GNB_CONFIG_STRING_DISABLE_HARQ);
    verify_gnb_param_notset(gnbp, GNB_NUM_DL_HARQ_IDX, GNB_CONFIG_STRING_NUM_DL_HARQPROCESSES);
    verify_gnb_param_notset(gnbp, GNB_NUM_UL_HARQ_IDX, GNB_CONFIG_STRING_NUM_UL_HARQPROCESSES);
    verify_gnb_param_notset(gnbp, GNB_CONFIG_REP_IDX, GNB_CONFIG_STRING_CONFIG_REP);

    // check for some general sections
    verify_section_notset(cfg, NULL, CONFIG_STRING_L1_LIST);
    verify_section_notset(cfg, NULL, CONFIG_STRING_RU_LIST);
    verify_section_notset(cfg, NULL, MACRLC_LIST);
    verify_section_notset(cfg, NULL, CONFIG_STRING_NR_RLC_LIST);
  } else if (NODE_IS_DU(node_type)) {
    // verify that there is no bearer config
    verify_gnb_param_notset(gnbp, GNB_ENABLE_SDAP_IDX, GNB_CONFIG_STRING_ENABLE_SDAP);

    verify_section_notset(cfg, GNB_CONFIG_STRING_GNB_LIST ".[0]", GNB_CONFIG_STRING_AMF_IP_ADDRESS);
    verify_section_notset(cfg, NULL, CONFIG_STRING_SECURITY);
    verify_section_notset(cfg, NULL, CONFIG_STRING_NR_PDCP_LIST);
    verify_section_notset(cfg, NULL, GNB_CONFIG_STRING_NEIGHBOUR_LIST);
    verify_section_notset(cfg, NULL, GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION);
  } // else nothing to be checked

  /* other possible verifications: PNF, VNF, CU-CP, CU-UP, ...? */
}

void RCconfig_nr_prs(void)
{
  uint16_t  j = 0, k = 0;
  prs_config_t *prs_config = NULL;
  char str[7][100] = {0};

  AssertFatal(RC.gNB != NULL, "gNB context is null, cannot complete PRS configuration\n");

  GET_PARAMS_LIST(PRS_ParamList, PRS_Params, PRS_PARAMS_DESC, CONFIG_STRING_PRS_CONFIG, NULL);

  if (PRS_ParamList.numelt > 0) {
    for (j = 0; j < RC.nb_nr_L1_inst; j++) {
      AssertFatal(RC.gNB[j] != NULL, "gNB L1 instance is null at index %d, cannot complete L1 configuration\n", j);

      RC.gNB[j]->prs_vars.NumPRSResources = *(PRS_ParamList.paramarray[j][NUM_PRS_RESOURCES].uptr);
      for (k = 0; k < RC.gNB[j]->prs_vars.NumPRSResources; k++)
      {
        prs_config = &RC.gNB[j]->prs_vars.prs_cfg[k];
        prs_config->PRSResourceSetPeriod[0]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[0];
        prs_config->PRSResourceSetPeriod[1]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[1];
        // per PRS resources parameters
        prs_config->SymbolStart              = PRS_ParamList.paramarray[j][PRS_SYMBOL_START_LIST].uptr[k];
        prs_config->NumPRSSymbols            = PRS_ParamList.paramarray[j][PRS_NUM_SYMBOLS_LIST].uptr[k];
        prs_config->REOffset                 = PRS_ParamList.paramarray[j][PRS_RE_OFFSET_LIST].uptr[k];
        prs_config->PRSResourceOffset        = PRS_ParamList.paramarray[j][PRS_RESOURCE_OFFSET_LIST].uptr[k];
        prs_config->NPRSID                   = PRS_ParamList.paramarray[j][PRS_ID_LIST].uptr[k];
        // Common parameters to all PRS resources
        prs_config->NumRB                    = *(PRS_ParamList.paramarray[j][PRS_NUM_RB].uptr);
        prs_config->RBOffset                 = *(PRS_ParamList.paramarray[j][PRS_RB_OFFSET].uptr);
        prs_config->CombSize                 = *(PRS_ParamList.paramarray[j][PRS_COMB_SIZE].uptr);
        prs_config->PRSResourceRepetition    = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_REPETITION].uptr);
        prs_config->PRSResourceTimeGap       = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_TIME_GAP].uptr);
        prs_config->MutingBitRepetition      = *(PRS_ParamList.paramarray[j][PRS_MUTING_BIT_REPETITION].uptr);
        for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].numelt; l++)
        {
          prs_config->MutingPattern1[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].uptr[l];
          if (k == 0) // print only for 0th resource
            snprintf(str[5]+strlen(str[5]),sizeof(str[5])-strlen(str[5]),"%d, ",prs_config->MutingPattern1[l]);
        }
        for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].numelt; l++)
        {
          prs_config->MutingPattern2[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].uptr[l];
          if (k == 0) // print only for 0th resource
            snprintf(str[6]+strlen(str[6]),sizeof(str[6])-strlen(str[6]),"%d, ",prs_config->MutingPattern2[l]);
        }

        // print to buffer
        snprintf(str[0]+strlen(str[0]),sizeof(str[0])-strlen(str[0]),"%d, ",prs_config->SymbolStart);
        snprintf(str[1]+strlen(str[1]),sizeof(str[1])-strlen(str[1]),"%d, ",prs_config->NumPRSSymbols);
        snprintf(str[2]+strlen(str[2]),sizeof(str[2])-strlen(str[2]),"%d, ",prs_config->REOffset);
        snprintf(str[3]+strlen(str[3]),sizeof(str[3])-strlen(str[3]),"%d, ",prs_config->PRSResourceOffset);
        snprintf(str[4]+strlen(str[4]),sizeof(str[4])-strlen(str[4]),"%d, ",prs_config->NPRSID);
      } // for k

      prs_config = &RC.gNB[j]->prs_vars.prs_cfg[0];
      LOG_I(PHY, "-----------------------------------------\n");
      LOG_I(PHY, "PRS Config for gNB_id %d @ %p\n", j, prs_config);
      LOG_I(PHY, "-----------------------------------------\n");
      LOG_I(PHY, "NumPRSResources \t%d\n", RC.gNB[j]->prs_vars.NumPRSResources);
      LOG_I(PHY, "PRSResourceSetPeriod \t[%d, %d]\n", prs_config->PRSResourceSetPeriod[0], prs_config->PRSResourceSetPeriod[1]);
      LOG_I(PHY, "NumRB \t\t\t%d\n", prs_config->NumRB);
      LOG_I(PHY, "RBOffset \t\t%d\n", prs_config->RBOffset);
      LOG_I(PHY, "CombSize \t\t%d\n", prs_config->CombSize);
      LOG_I(PHY, "PRSResourceRepetition \t%d\n", prs_config->PRSResourceRepetition);
      LOG_I(PHY, "PRSResourceTimeGap \t%d\n", prs_config->PRSResourceTimeGap);
      LOG_I(PHY, "MutingBitRepetition \t%d\n", prs_config->MutingBitRepetition);
      LOG_I(PHY, "SymbolStart \t\t[%s\b\b]\n", str[0]);
      LOG_I(PHY, "NumPRSSymbols \t\t[%s\b\b]\n", str[1]);
      LOG_I(PHY, "REOffset \t\t[%s\b\b]\n", str[2]);
      LOG_I(PHY, "PRSResourceOffset \t[%s\b\b]\n", str[3]);
      LOG_I(PHY, "NPRS_ID \t\t[%s\b\b]\n", str[4]);
      LOG_I(PHY, "MutingPattern1 \t\t[%s\b\b]\n", str[5]);
      LOG_I(PHY, "MutingPattern2 \t\t[%s\b\b]\n", str[6]);
      LOG_I(PHY, "-----------------------------------------\n");
    } // for j
  }
  else
  {
    LOG_I(PHY,"No " CONFIG_STRING_PRS_CONFIG " configuration found..!!\n");
  }
}

#define ALL_SYMBOLS_TAKEN 0x3FFF

/**
 * @brief Get number or blacklisted UL PRBs and their mapping from gNB config
 */
static int get_prb_blacklist(int idx, uint16_t *prbbl)
{
  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
  config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal(num_gnbs > 0, "Failed to parse config file, no GNBs found in field %s \n", GNB_CONFIG_STRING_ACTIVE_GNBS);
  paramdef_t GNBParams[] = GNBPARAMS_DESC;
  config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);

  if (idx != 0) {
    LOG_W(GNB_APP, "Warning: requested to read PRB blacklist from index %d, but only 0 supported\n", idx);
  }
  idx = 0;

  int num_prbbl = 0;
  char *ulprbbl = *GNBParamList.paramarray[idx][GNB_ULPRBBLACKLIST_IDX].strptr;

  if (!ulprbbl)
    return -1;

  LOG_D(NR_PHY, "PRB blacklist found: %s\n", ulprbbl);
  char *save = NULL;
  char *pt = strtok_r(ulprbbl, ",", &save);

  while (pt) {
    const int rb = atoi(pt);
    AssertFatal(rb < MAX_BWP_SIZE, "RB %d out of bounds (max 275 PRBs)\n", rb);
    prbbl[rb] = ALL_SYMBOLS_TAKEN;
    LOG_D(NR_PHY, "Blacklisting prb %d\n", rb);
    pt = strtok_r(NULL, ",", &save);
    num_prbbl++;
  }
  return num_prbbl;
}

/* L1 needs bf_method for DAS. Only a PNF legitimately has no MACRLCs section to read it
 * from, and it cannot do DAS at all; anywhere else a missing section would silently cost
 * the operator the beamforming they asked for. */
static nr_bf_method_e get_bf_method(int idx)
{
  GET_PARAMS_LIST(MacRLC_ParamList, MacRLC_Params, MACRLCPARAMS_DESC, MACRLC_LIST, NULL, MACRLCPARAMS_CHECK);
  if (MacRLC_ParamList.numelt == 0) {
    AssertFatal(NFAPI_MODE == NFAPI_MODE_PNF || NFAPI_MODE == NFAPI_MODE_STANDALONE_PNF,
                "no " MACRLC_LIST " section to read " MACRLC_BF_METHOD " from\n");
    return BF_METHOD_STRAIGHT_WIRE;
  }
  if (idx >= MacRLC_ParamList.numelt)
    return BF_METHOD_STRAIGHT_WIRE;
  // config_get_processedint() takes only paramdef_t *, so cast const away
  paramdef_t *p = (paramdef_t *)gpd(MacRLC_ParamList.paramarray[idx], sizeofArray(MacRLC_Params), MACRLC_BF_METHOD);
  return config_get_processedint(config_get_if(), p);
}

void RCconfig_NR_L1(void)
{
  LOG_I(NR_PHY, "Initializing NR L1: RC.nb_nr_L1_inst = %d\n", RC.nb_nr_L1_inst);

  for (int j = 0; j < RC.nb_nr_L1_inst; j++) {
    PHY_VARS_gNB *gNB = RC.gNB[j];
    AssertFatal(RC.gNB[j] != NULL, "gNB L1 instance is null at index %d, cannot complete L1 configuration\n", j);
    // gNB params
    if (NFAPI_MODE != NFAPI_MODE_PNF) {
      GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);
      int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
      AssertFatal(num_gnbs > 0, "Failed to parse config file, no GNBs found in field %s \n", GNB_CONFIG_STRING_ACTIVE_GNBS);
      GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL);

      // PRB Blacklist
      uint16_t prbbl[MAX_BWP_SIZE] = {0};
      int idx = 0;
      int num_ulprbbl = get_prb_blacklist(idx, prbbl);
      if (num_ulprbbl != -1) {
        RC.gNB[j]->num_ulprbbl = num_ulprbbl;
        LOG_D(NR_PHY, "Copying %d blacklisted PRB to L1 context\n", RC.gNB[j]->num_ulprbbl);
        memcpy(RC.gNB[j]->ulprbbl, prbbl, MAX_BWP_SIZE * sizeof(prbbl[0]));
      }
    }

    // L1 params
    GET_PARAMS_LIST(L1_ParamList, L1_Params, L1PARAMS_DESC, CONFIG_STRING_L1_LIST, NULL);

    if (L1_ParamList.numelt > 0) {
      const paramdef_t *params = L1_ParamList.paramarray[j];
      const int np = sizeofArray(L1_Params);
      AssertFatal(*gpd(params, np, L1_THREAD_POOL_SIZE)->uptr == 2022,
                  "thread_pool_size removed, please use --thread-pool\n");
      gNB->ofdm_offset_divisor = *gpd(params, np, L1_OFDM_OFFSET_DIVISOR)->uptr;
      gNB->pucch0_thres = *gpd(params, np, L1_PUCCH0_DTX_THRESHOLD)->uptr;
      gNB->prach_thres = *gpd(params, np, L1_PRACH_DTX_THRESHOLD)->uptr;
      gNB->pusch_thres = *gpd(params, np, L1_PUSCH_DTX_THRESHOLD)->uptr;
      gNB->srs_thres = *gpd(params, np, L1_SRS_DTX_THRESHOLD)->uptr;
      gNB->max_ldpc_iterations = *gpd(params, np, L1_MAX_LDPC_ITERATIONS)->uptr;
      gNB->L1_rx_thread_core = *gpd(params, np, L1_RX_THREAD_CORE)->iptr;
      gNB->L1_tx_thread_core = *gpd(params, np, L1_TX_THREAD_CORE)->iptr;
      LOG_I(NR_PHY, "thread cores for L1_RX %d L1_TX %d\n", gNB->L1_rx_thread_core, gNB->L1_tx_thread_core);
      // PUSCH symbols per thread need to be calculated by how many threads we have
      gNB->num_pusch_symbols_per_thread = *gpd(params, np, L1_NUM_RX_SYM_PER_THREAD)->iptr;
      gNB->num_pdsch_symbols_per_thread = *gpd(params, np, L1_NUM_TX_SYM_PER_THREAD)->iptr;
      gNB->TX_AMP = min(32767.0 / pow(10.0, .05 * (double)(*gpd(params, np, L1_TX_AMP_BACKOFF_dB)->uptr)), INT16_MAX);
      LOG_I(NR_PHY, "TX_AMP = %d (-%d dBFS)\n", gNB->TX_AMP, *gpd(params, np, L1_TX_AMP_BACKOFF_dB)->uptr);
      AssertFatal(gNB->TX_AMP > 300, "TX_AMP is too small, must be larger than 300 (is %d)\n", gNB->TX_AMP);
      gNB->phase_comp = *gpd(params, np, L1_PHASE_COMP)->uptr;
      gNB->dmrs_num_antennas_per_thread = *gpd(params, np, L1_NUM_ANTENNAS_PER_THREAD)->uptr;
      AssertFatal(*gpd(params, np, L1_ANALOG_DAS_REMOVED)->iptr == -1,
                  "enable_das removed from the L1s section, set " MACRLC_BF_METHOD " = \"das\" in the " MACRLC_LIST
                  " section instead (not possible with a split L1, where DAS is unsupported)\n");
      gNB->enable_analog_das = get_bf_method(j) == BF_METHOD_DAS;
      /* A PNF keeps its own config file: the MACRLCs section it finds there is not the
       * one the VNF schedules with, so DAS has to be refused here as well. */
      AssertFatal(!gNB->enable_analog_das || NFAPI_MODE == NFAPI_MONOLITHIC,
                  MACRLC_BF_METHOD " \"das\" is not supported with a split L1, the beam indices come from the VNF\n");
      if (gNB->enable_analog_das)
        LOG_I(NR_PHY, "L1 configured for a distributed antenna system (DAS)\n");
      // Midhaul configuration
      if (strcmp(*gpd(params, np, L1_TRANSPORT_N_PREFERENCE)->strptr, "local_mac") == 0) {
        // do nothing
      } else if (strcmp(*gpd(params, np, L1_TRANSPORT_N_PREFERENCE)->strptr, "nfapi") == 0) {
        char *my_addr = strdup(*gpd(params, np, L1_LOCAL_N_ADDRESS)->strptr);
        char *remote_addr = strdup(*gpd(params, np, L1_REMOTE_N_ADDRESS)->strptr);
        int remote_portc = *gpd(params, np, L1_REMOTE_N_PORTC)->iptr;
        int my_portd = *gpd(params, np, L1_LOCAL_N_PORTD)->iptr;
        int remote_portd = *gpd(params, np, L1_REMOTE_N_PORTD)->iptr;

        configure_nr_nfapi_pnf(remote_addr, remote_portc, my_addr, my_portd, remote_portd);
      } else {
        // other midhaul, do nothing
      }
      LOG_D(NR_PHY, "Initializing northbound interface for L1\n");
      l1_north_init_gNB();
    } else {
      LOG_E(NR_PHY, "No " CONFIG_STRING_L1_LIST " configuration found");
    }
  }
}

/**
 * @brief Returns true when pattern2 is filled in
 */
bool is_pattern2_config(paramdef_t *param)
{
  if (param == NULL || param->i64ptr == NULL || *(param->i64ptr) == -1)
    return false;
  return true;
}

static NR_ServingCellConfigCommon_t *get_scc_config(int idx)
{
  NR_ServingCellConfigCommon_t *scc = calloc_or_fail(1, sizeof(*scc));
  uint64_t ssb_bitmap=0xff;
  prepare_scc(scc);
  prepare_msgA_scc(scc);

  char aprefix[MAX_OPTNAME_SIZE*2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, idx);
  GET_PARAMS_LIST(SCCsParamList, SCCsParams, SCCPARAMS_DESC(scc), GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, aprefix);
  GET_PARAMS_LIST(MsgASCCsParamList, MsgASCCsParams, MSGASCCPARAMS_DESC(scc), GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, aprefix);

  if (SCCsParamList.numelt > 0 || MsgASCCsParamList.numelt > 0) {
    snprintf(aprefix, sizeof(aprefix), "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, idx, GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, 0);
    GET_PARAMS(SCCsParams, SCCPARAMS_DESC(scc), aprefix);
    GET_PARAMS(MsgASCCsParams, MSGASCCPARAMS_DESC(scc), aprefix);
    // NR_TDD-UL-DL-ConfigCommon pattern2 (optional IE)
    // fetch params
    struct NR_TDD_UL_DL_Pattern p2;
    paramdef_t pattern2Params[] = SCC_PATTERN2_PARAMS_DESC(p2);
    char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
    snprintf(aprefix,
             sizeof(aprefix),
             "%s.[%i].%s.[%i].%s",
             GNB_CONFIG_STRING_GNB_LIST,
             idx,
             GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON,
             0,
             SCC_PATTERN2_STRING_CONFIG);
    config_get(config_get_if(), pattern2Params, sizeofArray(pattern2Params), aprefix);
    // check validity
    bool is_pattern2 = false;
    for (int i = 0; i < sizeofArray(pattern2Params); i++) {
      is_pattern2 |= is_pattern2_config(pattern2Params);
    }

    if (is_pattern2) {
      LOG_I(GNB_APP, "tdd->pattern2 present\n");
      struct NR_TDD_UL_DL_ConfigCommon *tdd = scc->tdd_UL_DL_ConfigurationCommon;
      tdd->pattern2 = calloc_or_fail(1, sizeof(*tdd->pattern2));
      *scc->tdd_UL_DL_ConfigurationCommon->pattern2 = p2;
    }
    struct NR_FrequencyInfoDL *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
    LOG_I(RRC,
          "Read in ServingCellConfigCommon (PhysCellId %d, ABSFREQSSB %d, DLBand %d, ABSFREQPOINTA %d, DLBW "
          "%d,RACH_TargetReceivedPower %d\n",
          (int)*scc->physCellId,
          (int)*frequencyInfoDL->absoluteFrequencySSB,
          (int)*frequencyInfoDL->frequencyBandList.list.array[0],
          (int)frequencyInfoDL->absoluteFrequencyPointA,
          (int)frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
          (int)scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric
              .preambleReceivedTargetPower);
    // SSB of the PCell is always on the sync raster
    uint64_t ssb_freq = from_nrarfcn(*frequencyInfoDL->frequencyBandList.list.array[0],
                                     *scc->ssbSubcarrierSpacing,
                                     *frequencyInfoDL->absoluteFrequencySSB);
    LOG_I(RRC, "absoluteFrequencySSB %ld corresponds to %lu Hz\n", *frequencyInfoDL->absoluteFrequencySSB, ssb_freq);
    if (IS_SA_MODE(get_softmodem_params()))
      check_ssb_raster(ssb_freq, *frequencyInfoDL->frequencyBandList.list.array[0], *scc->ssbSubcarrierSpacing);
    fix_scc(scc, ssb_bitmap);
  }

  // the gNB uses the servingCellConfigCommon everywhere, even when it should use the servingCellConfigCommonSIB.
  // previously (before this commit), the following fields were indirectly populated through get_SIB1_NR().
  // since this might lead to memory problems (e.g., double frees), it has been moved here.
  // note that the "right solution" would be to not populate the servingCellConfigCommon here, and use
  // an "abstraction struct" that contains the corresponding values, from which SCC/SIB1/... is generated.
  NR_PDCCH_ConfigCommon_t *pcc = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup;
  AssertFatal(pcc != NULL && pcc->commonSearchSpaceList == NULL, "memory leak\n");
  pcc->commonSearchSpaceList = calloc_or_fail(1, sizeof(*pcc->commonSearchSpaceList));

  // TODO: Make CSS aggregation levels configurable
  int css_num_agg_level_candidates[NUM_PDCCH_AGG_LEVELS];
  css_num_agg_level_candidates[PDCCH_AGG_LEVEL1] = NR_SearchSpace__nrofCandidates__aggregationLevel1_n0;
  css_num_agg_level_candidates[PDCCH_AGG_LEVEL2] = NR_SearchSpace__nrofCandidates__aggregationLevel2_n0;
  css_num_agg_level_candidates[PDCCH_AGG_LEVEL4] = NR_SearchSpace__nrofCandidates__aggregationLevel4_n2;
  css_num_agg_level_candidates[PDCCH_AGG_LEVEL8] = NR_SearchSpace__nrofCandidates__aggregationLevel8_n0;
  css_num_agg_level_candidates[PDCCH_AGG_LEVEL16] = NR_SearchSpace__nrofCandidates__aggregationLevel16_n0;

  NR_SearchSpace_t *ss1 = rrc_searchspace_config(true, 1, 0, css_num_agg_level_candidates);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss1);
  NR_SearchSpace_t *ss2 = rrc_searchspace_config(true, 2, 0, css_num_agg_level_candidates);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss2);
  NR_SearchSpace_t *ss3 = rrc_searchspace_config(true, 3, 0, css_num_agg_level_candidates);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss3);

  asn1cCallocOne(pcc->searchSpaceSIB1,  0);
  asn1cCallocOne(pcc->ra_SearchSpace, 1);
  asn1cCallocOne(pcc->pagingSearchSpace, 2);
  asn1cCallocOne(pcc->searchSpaceOtherSystemInformation, 3);

  return scc;
}

static int read_du_cell_info(bool separate_du,
                             uint32_t *gnb_id,
                             uint64_t *gnb_du_id,
                             char **name,
                             f1ap_served_cell_info_t *info,
                             int max_cell_info)
{
  AssertFatal(max_cell_info == 1, "only one cell supported\n");
  memset(info, 0, sizeof(*info));

  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);
  int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal(num_gnbs == 1, "cannot configure DU: required config section \"gNBs\" missing\n");

  GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL);

  // read the gNB-ID. The DU itself only needs the gNB-DU ID, but some (e.g.,
  // E2 agent) need the gNB-ID as well if it is running in a separate process
  AssertFatal(config_isparamset(GNBParamList.paramarray[0], GNB_GNB_ID_IDX), "%s is not defined in configuration file\n", GNB_CONFIG_STRING_GNB_ID);
  *gnb_id = *GNBParamList.paramarray[0][GNB_GNB_ID_IDX].uptr;

  AssertFatal(strcmp(GNBSParams[GNB_ACTIVE_GNBS_IDX].strlistptr[0], *GNBParamList.paramarray[0][GNB_GNB_NAME_IDX].strptr) == 0,
              "no active gNB found/mismatch of gNBs: %s vs %s\n",
              GNBSParams[GNB_ACTIVE_GNBS_IDX].strlistptr[0],
              *GNBParamList.paramarray[0][GNB_GNB_NAME_IDX].strptr);

  // if fronthaul is F1, require gNB_DU_ID, else use gNB_ID
  if (separate_du) {
    AssertFatal(config_isparamset(GNBParamList.paramarray[0], GNB_GNB_DU_ID_IDX), "%s is not defined in configuration file\n", GNB_CONFIG_STRING_GNB_DU_ID);
    *gnb_du_id = *GNBParamList.paramarray[0][GNB_GNB_DU_ID_IDX].u64ptr;
  } else {
    AssertFatal(!config_isparamset(GNBParamList.paramarray[0], GNB_GNB_DU_ID_IDX),
                "%s should not be defined in configuration file for monolithic gNB\n",
                GNB_CONFIG_STRING_GNB_DU_ID);
    *gnb_du_id = *gnb_id; // the gNB-DU ID is equal to the gNB ID, since the config has no gNB-DU ID
  }

  *name = strdup(*(GNBParamList.paramarray[0][GNB_GNB_NAME_IDX].strptr));
  info->tac = malloc(sizeof(*info->tac));
  AssertFatal(info->tac != NULL, "out of memory\n");
  *info->tac = *GNBParamList.paramarray[0][GNB_TRACKING_AREA_CODE_IDX].uptr;

  // PLMN
  plmn_id_t p[PLMN_LIST_MAX_SIZE] = {0};
  set_plmn_config(p, 0);
  info->plmn = p[0];
  info->nr_cellid = (uint64_t) * (GNBParamList.paramarray[0][GNB_NRCELLID_IDX].u64ptr);

  // SNSSAI
  info->num_ssi = set_snssai_config(info->nssai, MAX_NUM_SLICES, 0, 0);

  return 1;
}

f1ap_tdd_info_t read_tdd_config(const NR_ServingCellConfigCommon_t *scc)
{
  const NR_FrequencyInfoDL_t *dl = scc->downlinkConfigCommon->frequencyInfoDL;
  f1ap_tdd_info_t tdd = {
      .freqinfo.arfcn = dl->absoluteFrequencyPointA,
      .freqinfo.band = *dl->frequencyBandList.list.array[0],
      .tbw.scs = dl->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .tbw.nrb = dl->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
  };
  return tdd;
}

static f1ap_fdd_info_t read_fdd_config(const NR_ServingCellConfigCommon_t *scc)
{
  const NR_FrequencyInfoDL_t *dl = scc->downlinkConfigCommon->frequencyInfoDL;
  const NR_FrequencyInfoUL_t *ul = scc->uplinkConfigCommon->frequencyInfoUL;
  f1ap_fdd_info_t fdd = {
      .dl_freqinfo.arfcn = dl->absoluteFrequencyPointA,
      .ul_freqinfo.arfcn = *ul->absoluteFrequencyPointA,
      .dl_tbw.scs = dl->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .ul_tbw.scs = ul->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .dl_tbw.nrb = dl->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
      .ul_tbw.nrb = ul->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
      .dl_freqinfo.band = *dl->frequencyBandList.list.array[0],
      .ul_freqinfo.band = *ul->frequencyBandList->list.array[0],
  };
  return fdd;
}

f1ap_gnb_du_system_info_t *get_sys_info(NR_BCCH_BCH_Message_t *mib, const NR_BCCH_DL_SCH_Message_t *sib1, seq_arr_t *du_SIBs)
{
  int buf_len = 3;
  f1ap_gnb_du_system_info_t *sys_info = calloc_or_fail(1, sizeof(*sys_info));

  sys_info->mib = calloc_or_fail(buf_len, sizeof(*sys_info->mib));
  DevAssert(mib != NULL);
  sys_info->mib_length = encode_MIB_NR_setup(mib->message.choice.mib, 0, sys_info->mib, buf_len);
  DevAssert(sys_info->mib_length == buf_len);

  DevAssert(sib1 != NULL);
  NR_SIB1_t *bcch_SIB1 = sib1->message.choice.c1->choice.systemInformationBlockType1;
  sys_info->sib1 = calloc_or_fail(NR_MAX_SIB_LENGTH / 8, sizeof(*sys_info->sib1));
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_SIB1, NULL, (void *)bcch_SIB1, sys_info->sib1, NR_MAX_SIB_LENGTH / 8);
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  sys_info->sib1_length = (enc_rval.encoded + 7) / 8;

  if (du_SIBs) {
    for (int i = 0; i < du_SIBs->size; i++) {
      nr_SIBs_t *si = (nr_SIBs_t *)seq_arr_at(du_SIBs, i);
      // other SIB in gNB-DU System Information not implemented yet
      // only DU SIB not included in this message is SIB19
      AssertFatal(si->SIB_type == NR_SIB_19, "Cannot handle SIB%d in gNB-DU System Information\n", (int)si->SIB_type);
    }
  }
  return sys_info;
}

static f1ap_setup_req_t *RC_read_F1Setup(uint64_t id,
                                         const char *name,
                                         const f1ap_served_cell_info_t *info,
                                         const NR_ServingCellConfigCommon_t *scc,
                                         NR_BCCH_BCH_Message_t *mib,
                                         const NR_BCCH_DL_SCH_Message_t *sib1,
                                         seq_arr_t *du_SIBs)
{
  f1ap_setup_req_t *req = calloc(1, sizeof(*req));
  AssertFatal(req != NULL, "out of memory\n");
  req->gNB_DU_id = id;
  req->gNB_DU_name = strdup(name);
  req->num_cells_available = 1;
  req->cell = calloc_or_fail(req->num_cells_available, sizeof(*req->cell));
  req->cell[0].info = *info;
  LOG_I(GNB_APP,
        "F1AP: gNB idx %d gNB_DU_id %ld, gNB_DU_name %s, TAC %d MCC/MNC/length %d/%d/%d cellID %ld\n",
        0,
        req->gNB_DU_id,
        req->gNB_DU_name,
        *req->cell[0].info.tac,
        req->cell[0].info.plmn.mcc,
        req->cell[0].info.plmn.mnc,
        req->cell[0].info.plmn.mnc_digit_length,
        req->cell[0].info.nr_cellid);

  req->cell[0].info.nr_pci = *scc->physCellId;
  if (scc->tdd_UL_DL_ConfigurationCommon) {
    LOG_I(GNB_APP, "ngran_DU: Configuring Cell %d for TDD\n", 0);
    req->cell[0].info.mode = F1AP_MODE_TDD;
    req->cell[0].info.tdd = read_tdd_config(scc);
  } else {
    LOG_I(GNB_APP, "ngran_DU: Configuring Cell %d for FDD\n", 0);
    req->cell[0].info.mode = F1AP_MODE_FDD;
    req->cell[0].info.fdd = read_fdd_config(scc);
  }

  NR_MeasurementTimingConfiguration_t *mtc = get_new_MeasurementTimingConfiguration(scc);
  uint8_t buf[1024];
  int len = encode_MeasurementTimingConfiguration(mtc, buf, sizeof(buf));
  DevAssert(len <= sizeof(buf));
  free_MeasurementTimingConfiguration(mtc);
  uint8_t *mtc_buf = calloc(len, sizeof(*mtc_buf));
  AssertFatal(mtc_buf != NULL, "out of memory\n");
  memcpy(mtc_buf, buf, len);
  req->cell[0].info.measurement_timing_config = mtc_buf;
  req->cell[0].info.measurement_timing_config_len = len;

  if (IS_SA_MODE(get_softmodem_params())) {
    // in NSA we don't transmit SIB1, so cannot fill DU system information
    // so cannot send MIB either
    req->cell[0].sys_info = get_sys_info(mib, sib1, du_SIBs);
  }

  int num = read_version(TO_STRING(NR_RRC_VERSION), &req->rrc_ver[0], &req->rrc_ver[1], &req->rrc_ver[2]);
  AssertFatal(num == 3, "could not read RRC version string %s\n", TO_STRING(NR_RRC_VERSION));

  return req;
}

static nr_ptrs_config_t *get_ptrs_config(int idx)
{
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, idx);
  GET_PARAMS_LIST(ptrs_ParamsList, ptrs_Params, GNB_PTRS_PARAMS_DESC, GNB_CONFIG_STRING_PTRS, aprefix);
  if (ptrs_ParamsList.numelt == 0) {
    LOG_I(GNB_APP, "No PTRS configuration found\n");
    return NULL;
  }
  nr_ptrs_config_t *ptrs = calloc_or_fail(1, sizeof(*ptrs));
  ptrs->dl_FreqDensity0_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSFREQDENSITY0_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_FreqDensity0_0 %d\n", ptrs->dl_FreqDensity0_0);
  ptrs->dl_FreqDensity1_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSFREQDENSITY1_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_FreqDensity1_0 %d\n", ptrs->dl_FreqDensity1_0);
  ptrs->dl_TimeDensity0_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSTIMEDENSITY0_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_TimeDensity0_0 %d\n", ptrs->dl_TimeDensity0_0);
  ptrs->dl_TimeDensity1_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSTIMEDENSITY1_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_TimeDensity1_0 %d\n", ptrs->dl_TimeDensity1_0);
  ptrs->dl_TimeDensity2_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSTIMEDENSITY2_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_TimeDensity2_0 %d\n", ptrs->dl_TimeDensity2_0);
  ptrs->dl_EpreRatio_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSEPRERATIO_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_EpreRatio_0 %d\n", ptrs->dl_EpreRatio_0);
  ptrs->dl_ReOffset_0 = *ptrs_ParamsList.paramarray[0][GNB_DLPTRSREOFFSET_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: dl_ReOffset_0 %d\n", ptrs->dl_ReOffset_0);
  ptrs->ul_FreqDensity0_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSFREQDENSITY0_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_FreqDensity0_0 %d\n", ptrs->ul_FreqDensity0_0);
  ptrs->ul_FreqDensity1_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSFREQDENSITY1_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_FreqDensity1_0 %d\n", ptrs->ul_FreqDensity1_0);
  ptrs->ul_TimeDensity0_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSTIMEDENSITY0_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_TimeDensity0_0 %d\n", ptrs->ul_TimeDensity0_0);
  ptrs->ul_TimeDensity1_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSTIMEDENSITY1_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_TimeDensity1_0 %d\n", ptrs->ul_TimeDensity1_0);
  ptrs->ul_TimeDensity2_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSTIMEDENSITY2_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_TimeDensity2_0 %d\n", ptrs->ul_TimeDensity2_0);
  ptrs->ul_ReOffset_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSREOFFSET_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_ReOffset_0 %d\n", ptrs->ul_ReOffset_0);
  ptrs->ul_MaxPorts_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSMAXPORTS_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_MaxPorts_0 %d\n", ptrs->ul_MaxPorts_0);
  ptrs->ul_Power_0 = *ptrs_ParamsList.paramarray[0][GNB_ULPTRSPOWER_0_IDX].iptr;
  LOG_I(GNB_APP, "PTRS configuration: ul_Power_0 %d\n", ptrs->ul_Power_0);
  return ptrs;
}

static nr_redcap_config_t *get_redcap_config(int gnb_idx)
{
  paramdef_t RedCap_Params[] = GNB_REDCAP_PARAMS_DESC;
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%d].%s", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_REDCAP);
  int ret = config_get(config_get_if(), RedCap_Params, sizeofArray(RedCap_Params), aprefix);

  if (ret <= 0) {
    printf("problem reading section \"%s\"\n", aprefix);
    return NULL;
  }
  // Check for default/non-existing values in configuration file
  if (*RedCap_Params[GNB_REDCAP_CELL_BARRED_REDCAP1_RX_R17_IDX].i8ptr == -1
      || *RedCap_Params[GNB_REDCAP_CELL_BARRED_REDCAP2_RX_R17_IDX].i8ptr == -1) {
    LOG_I(GNB_APP, "No RedCap configuration found\n");
    return NULL;
  }

  nr_redcap_config_t *rc = calloc_or_fail(1, sizeof(*rc));
  rc->cellBarredRedCap1Rx_r17 = *RedCap_Params[GNB_REDCAP_CELL_BARRED_REDCAP1_RX_R17_IDX].i8ptr;
  rc->cellBarredRedCap2Rx_r17 = *RedCap_Params[GNB_REDCAP_CELL_BARRED_REDCAP2_RX_R17_IDX].i8ptr;
  rc->intraFreqReselectionRedCap_r17 = *RedCap_Params[GNB_REDCAP_INTRA_FREQ_RESELECTION_REDCAP_R17_IDX].u8ptr;

  LOG_I(GNB_APP,
        "cellBarredRedCap1Rx_r17 %d cellBarredRedCap2Rx_r17 %d intraFreqReselectionRedCap_r17 %d\n",
        rc->cellBarredRedCap1Rx_r17,
        rc->cellBarredRedCap2Rx_r17,
        rc->intraFreqReselectionRedCap_r17);
  return rc;
}

static seq_arr_t *fill_du_sibs(paramdef_t *GNBparamarray)
{
  if (NODE_IS_DU(get_node_type()))
    AssertFatal(GNBparamarray[GNB_CU_SIBS_IDX].numelt == 0, "This is DU, do not input CU SIBs\n");

  // TODO config_isparamset doesn't seem to work for array types, checking numelt instead
  if (GNBparamarray[GNB_DU_SIBS_IDX].numelt == 0)
    return NULL;

  if (!IS_SA_MODE(get_softmodem_params())) {
    LOG_E(GNB_APP, "DU SIBs only supported in SA mode, we'll skip them!\n");
    return NULL;
  }

  seq_arr_t *du_SIBs = malloc(sizeof(seq_arr_t));
  seq_arr_init(du_SIBs, sizeof(nr_SIBs_t));
  for (int i = 0; i < GNBparamarray[GNB_DU_SIBS_IDX].numelt; i++) {
    int sib_value = GNBparamarray[GNB_DU_SIBS_IDX].iptr[i];
    // SIB10, SIB12, SIB13, SIB14, SIB15, SIB17, SIB18, SIB19 and SIB20 are generated by the DU
    // all the rest in the DU
    AssertFatal((sib_value > NR_SIB_9 && sib_value < NR_SIB_21) && sib_value != NR_SIB_11 && sib_value != NR_SIB_16,
                "SIB%d is not a DU SIB, to be configured in DU section\n",
                sib_value);
    nr_SIBs_t *du_sib = calloc_or_fail(1, sizeof(nr_SIBs_t));
    du_sib->SIB_type = sib_value;
    LOG_I(GNB_APP, "activate SIB%d at DU\n", sib_value);
    seq_arr_push_back(du_SIBs, du_sib, sizeof(nr_SIBs_t));
  }
  return du_SIBs;
}

void config_rlc(configmodule_interface_t *cfg, nr_rlc_configuration_t *rlc_config)
{
  /* SRB configuration */
  paramdef_t rlc_srb_params[] = NR_RLC_SRB_GLOBALPARAMS_DESC;
  int ret = config_get(cfg, rlc_srb_params, sizeofArray(rlc_srb_params), CONFIG_STRING_NR_RLC_SRB);
  AssertFatal(ret >= 0, "problem reading NR RLC configuration from configuration file\n");

  rlc_config->srb.t_poll_retransmit = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_T_POLL_RETRANSMIT_IDX]);
  rlc_config->srb.t_reassembly = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_T_REASSEMBLY_IDX]);
  rlc_config->srb.t_status_prohibit = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_T_STATUS_PROHIBIT_IDX]);
  rlc_config->srb.poll_pdu = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_POLL_PDU_IDX]);
  rlc_config->srb.poll_byte = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_POLL_BYTE_IDX]);
  rlc_config->srb.max_retx_threshold = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_MAX_RETX_THRESHOLD_IDX]);
  rlc_config->srb.sn_field_length = config_get_processedint(cfg, &rlc_srb_params[CONFIG_NR_RLC_SRB_SN_FIELD_LENGTH_IDX]);

  /* DRB AM configuration */
  paramdef_t rlc_drb_am_params[] = NR_RLC_DRB_AM_GLOBALPARAMS_DESC;
  ret = config_get(cfg, rlc_drb_am_params, sizeofArray(rlc_drb_am_params), CONFIG_STRING_NR_RLC_DRB_AM);
  AssertFatal(ret >= 0, "problem reading NR RLC configuration from configuration file\n");

  rlc_config->drb_am.t_poll_retransmit = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_T_POLL_RETRANSMIT_IDX]);
  rlc_config->drb_am.t_reassembly = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_T_REASSEMBLY_IDX]);
  rlc_config->drb_am.t_status_prohibit = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_T_STATUS_PROHIBIT_IDX]);
  rlc_config->drb_am.poll_pdu = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_POLL_PDU_IDX]);
  rlc_config->drb_am.poll_byte = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_POLL_BYTE_IDX]);
  rlc_config->drb_am.max_retx_threshold = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_MAX_RETX_THRESHOLD_IDX]);
  rlc_config->drb_am.sn_field_length = config_get_processedint(cfg, &rlc_drb_am_params[CONFIG_NR_RLC_DRB_AM_SN_FIELD_LENGTH_IDX]);

  /* DRB UM configuration */
  paramdef_t rlc_drb_um_params[] = NR_RLC_DRB_UM_GLOBALPARAMS_DESC;
  ret = config_get(cfg, rlc_drb_um_params, sizeofArray(rlc_drb_um_params), CONFIG_STRING_NR_RLC_DRB_UM);
  AssertFatal(ret >= 0, "problem reading NR RLC configuration from configuration file\n");

  rlc_config->drb_um.t_reassembly = config_get_processedint(cfg, &rlc_drb_um_params[CONFIG_NR_RLC_DRB_UM_T_REASSEMBLY_IDX]);
  rlc_config->drb_um.sn_field_length = config_get_processedint(cfg, &rlc_drb_um_params[CONFIG_NR_RLC_DRB_UM_SN_FIELD_LENGTH_IDX]);
}

static void config_pdcp(configmodule_interface_t *cfg, nr_pdcp_configuration_t *pdcp_config)
{
  /* DRB configuration */
  paramdef_t pdcp_params[] = NR_PDCP_DRB_GLOBALPARAMS_DESC;
  int ret = config_get(cfg, pdcp_params, sizeofArray(pdcp_params), CONFIG_STRING_NR_PDCP_DRB);
  AssertFatal(ret >= 0, "problem reading NR PDCP configuration from configuration file\n");

  pdcp_config->drb.sn_size = config_get_processedint(cfg, &pdcp_params[CONFIG_NR_PDCP_DRB_SN_SIZE_IDX]);
  pdcp_config->drb.t_reordering = config_get_processedint(cfg, &pdcp_params[CONFIG_NR_PDCP_DRB_T_REORDERING_IDX]);
  pdcp_config->drb.discard_timer = config_get_processedint(cfg, &pdcp_params[CONFIG_NR_PDCP_DRB_DISCARD_TIMER_IDX]);
}

static void config_nrdc(configmodule_interface_t *cfg, nrdc_configuration_t *nrdc_config)
{
  GET_PARAMS_LIST(nrdc_combination_list, nrdc_combination, NRDC_PARAMS_DESC, CONFIG_NRDC_COMBINATION_LIST, CONFIG_STRING_NRDC);

  nrdc_config->combination_count = nrdc_combination_list.numelt;

  LOG_I(NR_RRC, "NR-DC: %d combinations configured\n", nrdc_config->combination_count);

  nrdc_config->combinations = calloc_or_fail(nrdc_config->combination_count, sizeof(nrdc_combination_t));
  for (int i = 0; i < nrdc_combination_list.numelt; i++) {
    const paramdef_t *combination = nrdc_combination_list.paramarray[i];
    uint64_t mcg_cell_id = *gpd(combination, sizeofArray(nrdc_combination), CONFIG_NRDC_MCG)->u64ptr;
    uint64_t scg_cell_id = *gpd(combination, sizeofArray(nrdc_combination), CONFIG_NRDC_SCG)->u64ptr;
    LOG_I(NR_RRC, "NR-DC: combination %d: mcg cell ID %"PRIu64" scg cell ID %"PRIu64"\n", i, mcg_cell_id, scg_cell_id);

    nrdc_config->combinations[i].mcg_cell_id = mcg_cell_id;
    nrdc_config->combinations[i].scg_cell_id = scg_cell_id;
  }
}

void nfapi_stop_l1()
{
  if (NFAPI_MODE && (NFAPI_MODE == NFAPI_MODE_AERIAL || NFAPI_MODE == NFAPI_MODE_VNF)) {
    stop_nr_nfapi_vnf();
  } else if (NFAPI_MODE && NFAPI_MODE == NFAPI_MODE_PNF) {
    // PNF should just wait for the STOP.request to come from the L2
    // If instead it was stopped first, in some cases it makes sense for it to terminate itself (WLS)
    // Sends a STOP.indication to the VNF
    stop_nr_nfapi_pnf();
  }
}

static void get_bwp_config(nr_mac_config_t *configuration, const int bw)
{
  char path[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(path, sizeof(path), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  GET_PARAMS_LIST(BWPParamList, BWPParams, GNBBWPPARAMS_DESC, GNB_CONFIG_STRING_BWP_LIST, path, BWPPARAMS_CHECK);
  configuration->num_additional_bwps = BWPParamList.numelt;
  AssertFatal(configuration->num_additional_bwps >= 0 && configuration->num_additional_bwps <= 4,
              "Invalid number of additional BWPs %d\n",
              configuration->num_additional_bwps);
  for (int i = 0; i < configuration->num_additional_bwps; i++) {
    configuration->bwp_config[i].id = i + 1;
    int bwp_start = *BWPParamList.paramarray[i][GNB_BWP_START_IDX].iptr;
    AssertFatal(bwp_start >= 0 && bwp_start < bw, "Invalid BWP start value %d\n", bwp_start);
    int bwp_size = *BWPParamList.paramarray[i][GNB_BWP_SIZE_IDX].iptr;
    AssertFatal(bwp_start + bwp_size <= bw, "BWP start %d and BWP size %d exceeds full BW %d\n", bwp_start, bwp_size, bw);
    configuration->bwp_config[i].location_and_bw = PRBalloc_to_locationandbandwidth(bwp_size, bwp_start);
    configuration->bwp_config[i].scs = *BWPParamList.paramarray[i][GNB_BWP_SCS_IDX].iptr;
    LOG_I(GNB_APP,
          "BWP %d, start PRB %d size %d locationandbandwidth %d, scs %d\n",
          configuration->bwp_config[i].id,
          bwp_start,
          bwp_size,
          configuration->bwp_config[i].location_and_bw,
          configuration->bwp_config[i].scs);
  }
}

/* sscanf() returns the number of assignments, not whether trailing literals matched, so
 * the imaginary unit has to be checked separately: "0.5" otherwise matches "%lfi" and a
 * purely real weight would be read as a purely imaginary one. */
static bool parse_complex_token(const char *tok, double complex *out)
{
  double re = 0.0;
  double im = 0.0;
  int n = 0;
  if (sscanf(tok, "%lf%lf%n", &re, &im, &n) == 2 && (tok[n] == 'i' || tok[n] == 'j') && tok[n + 1] == '\0') {
    *out = re + I * im;
    return true;
  }
  n = 0;
  if (sscanf(tok, "%lf%n", &im, &n) == 1 && (tok[n] == 'i' || tok[n] == 'j') && tok[n + 1] == '\0') {
    *out = I * im;
    return true;
  }
  n = 0;
  if (sscanf(tok, "%lf%n", &re, &n) == 1 && tok[n] == '\0') {
    *out = re;
    return true;
  }
  return false;
}

static double complex **read_dbt_from_csv(const char *filename,
                                          int *num_beams,
                                          int *num_weights_per_beam,
                                          uint16_t **beam_ids)
{
  FILE *fp = fopen(filename, "r");
  AssertFatal(fp != NULL, "Failed to open DBT CSV file '%s'\n", filename);

  char line[16384];
  AssertFatal(fgets(line, sizeof(line), fp) != NULL, "Failed to read DBT CSV header from '%s'\n", filename);

  int weights = 1;
  for (char *p = line; *p; ++p)
    if (*p == ',')
      weights++;
  weights -= 1; // first column is beam ID
  AssertFatal(weights > 0, "DBT CSV '%s' has no weight columns\n", filename);

  int beams = 0;
  while (fgets(line, sizeof(line), fp))
    beams++;
  AssertFatal(beams > 0, "No DBT beam rows found in CSV file '%s'\n", filename);

  rewind(fp);
  AssertFatal(fgets(line, sizeof(line), fp) != NULL, "Failed to reread DBT CSV header from '%s'\n", filename);

  double complex **table = calloc_or_fail(beams, sizeof(*table));
  uint16_t *ids = calloc_or_fail(beams, sizeof(*ids));

  int b = 0;
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = '\0';
    char *saveptr = NULL;
    char *tok = strtok_r(line, ",", &saveptr);
    AssertFatal(tok != NULL, "Malformed DBT row %d in '%s'\n", b + 1, filename);

    errno = 0;
    char *endptr = NULL;
    long beam_id = strtol(tok, &endptr, 10);
    AssertFatal(endptr != tok && errno != ERANGE && *endptr == '\0' && beam_id >= 0 && beam_id <= UINT16_MAX,
                "Invalid DBT beam id '%s' in file '%s', beam row %d\n",
                tok,
                filename,
                b + 1);
    ids[b] = (uint16_t)beam_id;

    table[b] = calloc_or_fail(weights, sizeof(*table[b]));
    for (int w = 0; w < weights; ++w) {
      tok = strtok_r(NULL, ",", &saveptr);
      AssertFatal(tok != NULL, "Missing DBT weight in file '%s', beam row %d column %d\n", filename, b + 1, w + 1);
      AssertFatal(parse_complex_token(tok, &table[b][w]),
                  "Invalid DBT complex weight '%s' in file '%s', beam row %d column %d\n",
                  tok,
                  filename,
                  b + 1,
                  w + 1);
    }
    AssertFatal(strtok_r(NULL, ",", &saveptr) == NULL,
                "Too many DBT columns in file '%s', beam row %d\n",
                filename,
                b + 1);
    b++;
  }
  AssertFatal(b == beams, "Parsed %d DBT rows but counted %d in file '%s'\n", b, beams, filename);

  fclose(fp);
  *num_beams = beams;
  *num_weights_per_beam = weights;
  *beam_ids = ids;
  return table;
}

#define DBT_PARAMS_DESC { \
  {"", "beam", 0, .strlistptr=NULL, .defstrlistval=NULL, TYPE_STRINGLIST, 0}, \
}

static double complex **read_dbt_from_config(const char *prefix,
                                             int *num_beams,
                                             int *num_weights_per_beam,
                                             uint16_t **beam_ids)
{
  paramdef_t dbt_params[] = DBT_PARAMS_DESC;
  paramlist_def_t dbt_list = {"dbt", NULL, 0};
  int ret = config_getlist(config_get_if(), &dbt_list, dbt_params, sizeofArray(dbt_params), prefix);
  if (ret <= 0) {
    *num_beams = 0;
    *num_weights_per_beam = 0;
    *beam_ids = NULL;
    return NULL;
  }

  const int beams = ret;
  int weights = 0;
  double complex **table = calloc_or_fail(beams, sizeof(*table));
  uint16_t *ids = calloc_or_fail(beams, sizeof(*ids));

  for (int b = 0; b < beams; ++b) {
    paramdef_t *row = &dbt_list.paramarray[b][0];
    AssertFatal(row->strlistptr != NULL, "Invalid dbt row %d in section '%s': missing string list\n", b, prefix);
    AssertFatal(row->numelt >= 2,
                "Invalid dbt row %d in section '%s': expected beam_id + >=1 weights, got %d elements\n",
                b,
                prefix,
                row->numelt);

    const int row_weights = row->numelt - 1;
    if (weights == 0)
      weights = row_weights;
    AssertFatal(weights == row_weights,
                "Inconsistent dbt row width in section '%s': row %d has %d weights, expected %d\n",
                prefix,
                b,
                row_weights,
                weights);

    errno = 0;
    char *endptr = NULL;
    long beam_id = strtol(row->strlistptr[0], &endptr, 10);
    AssertFatal(endptr != row->strlistptr[0] && errno != ERANGE && *endptr == '\0' && beam_id >= 0 && beam_id <= UINT16_MAX,
                "Invalid dbt beam id '%s' in section '%s', row %d\n",
                row->strlistptr[0],
                prefix,
                b);
    ids[b] = (uint16_t)beam_id;

    table[b] = calloc_or_fail(weights, sizeof(*table[b]));
    for (int w = 0; w < weights; ++w)
      AssertFatal(parse_complex_token(row->strlistptr[w + 1], &table[b][w]),
                  "Invalid dbt complex weight '%s' in section '%s', row %d col %d\n",
                  row->strlistptr[w + 1],
                  prefix,
                  b,
                  w + 1);
  }

  *num_beams = beams;
  *num_weights_per_beam = weights;
  *beam_ids = ids;
  return table;
}

/* a beam ID names at most one digital beam table entry, otherwise the entry a given
 * ssb_beams value resolves to would depend on the row order */
static void check_dbt_beam_ids_unique(const nr_beam_table_t *bt)
{
  if (!bt->beam_ids) // no explicit IDs: the row number is the ID, unique by construction
    return;
  for (int a = 0; a < bt->num_beams; a++)
    for (int b = a + 1; b < bt->num_beams; b++)
      AssertFatal(bt->beam_ids[a] != bt->beam_ids[b],
                  "digital beam table rows %d and %d both have beam ID %u\n",
                  a,
                  b,
                  bt->beam_ids[a]);
}

/* the beam ID names a digital beam table entry by beam_idx, not by row number */
static void check_ssb_beams_in_dbt(const int32_t *ssb_beams, int num_ssb_beams, const nr_beam_table_t *bt)
{
  for (int i = 0; i < num_ssb_beams; i++) {
    bool found = false;
    for (int b = 0; b < bt->num_beams && !found; b++)
      found = (bt->beam_ids ? bt->beam_ids[b] : b) == ssb_beams[i];
    AssertFatal(found,
                MACRLC_SSB_BEAMS_LIST "[%d] = %d is not a beam of the digital beam table (%d entries)\n",
                i,
                ssb_beams[i],
                bt->num_beams);
  }
}

static void config_spatial_stream_index(const paramdef_t *param, const size_t np, nr_mac_config_t *radio_config, int num_ru_ports)
{
  AssertFatal(num_ru_ports <= NR_MAC_MAX_RU_ANTENNA_PORTS,
              "Number of RU antenna ports (%d) exceeds NR_MAC_MAX_RU_ANTENNA_PORTS (%d)\n",
              num_ru_ports,
              NR_MAC_MAX_RU_ANTENNA_PORTS);
  const int n = gpd(param, np, MACRLC_SPATIAL_STREAM_IDX)->numelt;
  if (n == 0) {
    // No indices provided in config file. Set default indices starting from 0.
    for (int i = 0; i < num_ru_ports; i++)
      radio_config->spatial_stream_index[i] = i;
  } else {
    AssertFatal(n == num_ru_ports, "Number of spatial stream indices must match number of RU ports\n");
    for (int i = 0; i < n; i++)
      radio_config->spatial_stream_index[i] = gpd(param, np, MACRLC_SPATIAL_STREAM_IDX)->uptr[i];
  }
}

static nr_mac_config_t get_mac_config(configmodule_interface_t *cfg,
                                      int *num_tx,
                                      int idx,
                                      paramdef_t *gnb_p,
                                      paramdef_t *timer_p,
                                      const paramdef_t *macrlc_p,
                                      int macrlc_np,
                                      const int bw)
{
  nr_mac_config_t config = {0};
  config.pdsch_AntennaPorts.N1 = *gnb_p[GNB_PDSCH_ANTENNAPORTS_N1_IDX].iptr;
  config.pdsch_AntennaPorts.N2 = *gnb_p[GNB_PDSCH_ANTENNAPORTS_N2_IDX].iptr;
  config.pdsch_AntennaPorts.XP = *gnb_p[GNB_PDSCH_ANTENNAPORTS_XP_IDX].iptr;
  config.pusch_AntennaPorts = *gnb_p[GNB_PUSCH_ANTENNAPORTS_IDX].iptr;
  LOG_I(GNB_APP,
        "pdsch_AntennaPorts N1 %d N2 %d XP %d pusch_AntennaPorts %d\n",
        config.pdsch_AntennaPorts.N1,
        config.pdsch_AntennaPorts.N2,
        config.pdsch_AntennaPorts.XP,
        config.pusch_AntennaPorts);

  config.minRXTXTIME = *gnb_p[GNB_MINRXTXTIME_IDX].iptr;
  LOG_I(GNB_APP, "minTXRXTIME %d\n", config.minRXTXTIME);
  config.do_TCI = *gnb_p[GNB_DO_TCI_IDX].iptr;
  config.do_CSIRS = *gnb_p[GNB_DO_CSIRS_IDX].iptr;
  const char *srs_type_s = *gnb_p[GNB_DO_SRS_IDX].strptr;
  config.do_SRS = config_get_processedint(cfg, &gnb_p[GNB_DO_SRS_IDX]);
  config.max_num_rsrp = *gnb_p[GNB_LIMIT_RSRP_REPORT_IDX].iptr;
  const char *report_type_s = *gnb_p[GNB_CONFIG_REP_IDX].strptr;
  config.report_type = config_get_processedint(cfg, &gnb_p[GNB_CONFIG_REP_IDX]);
  config.force_256qam_off = *gnb_p[GNB_FORCE256QAMOFF_IDX].iptr;
  config.force_UL256qam_off = *gnb_p[GNB_FORCEUL256QAMOFF_IDX].iptr;
  config.use_deltaMCS = *gnb_p[GNB_USE_DELTA_MCS_IDX].iptr != 0;
  config.maxMIMO_layers = *gnb_p[GNB_MAXMIMOLAYERS_IDX].iptr;
  config.disable_harq = *gnb_p[GNB_DISABLE_HARQ_IDX].iptr;
  config.num_dlharq = *gnb_p[GNB_NUM_DL_HARQ_IDX].iptr;
  config.num_ulharq =  *gnb_p[GNB_NUM_UL_HARQ_IDX].iptr;
  if (config.disable_harq)
    LOG_W(GNB_APP, "\"disable_harq\" is a REL17 feature and is incompatible with REL15 and REL16 UEs!\n");
  LOG_I(GNB_APP,
        "CSI-RS %d, SRS %s, report type %d (%s), 256 QAM %s, delta_MCS %s, maxMIMO_Layers %d, HARQ feedback %s, num DLHARQ:%d, num ULHARQ:%d\n",
        config.do_CSIRS,
        srs_type_s,
        config.report_type,
        report_type_s,
        config.force_256qam_off ? "force off" : "may be on",
        config.use_deltaMCS ? "on" : "off",
        config.maxMIMO_layers,
        config.disable_harq ? "disabled" : "enabled",
        config.num_dlharq,
        config.num_ulharq);
  int tot_ant = config.pdsch_AntennaPorts.N1 * config.pdsch_AntennaPorts.N2 * config.pdsch_AntennaPorts.XP;
  AssertFatal(config.maxMIMO_layers != 0 && config.maxMIMO_layers <= tot_ant, "Invalid maxMIMO_layers %d\n", config.maxMIMO_layers);

  // Construct default aggragation level list or read from config
  int uess_num_agg_level_candidates[NUM_PDCCH_AGG_LEVELS];
  uess_num_agg_level_candidates[PDCCH_AGG_LEVEL1] = NR_SearchSpace__nrofCandidates__aggregationLevel1_n0;
  uess_num_agg_level_candidates[PDCCH_AGG_LEVEL2] = NR_SearchSpace__nrofCandidates__aggregationLevel2_n2;
  uess_num_agg_level_candidates[PDCCH_AGG_LEVEL4] = NR_SearchSpace__nrofCandidates__aggregationLevel4_n0;
  uess_num_agg_level_candidates[PDCCH_AGG_LEVEL8] = NR_SearchSpace__nrofCandidates__aggregationLevel8_n0;
  uess_num_agg_level_candidates[PDCCH_AGG_LEVEL16] = NR_SearchSpace__nrofCandidates__aggregationLevel16_n0;
  int* agg_level_list = uess_num_agg_level_candidates;
  int num_agg_levels = 5;
  if (gnb_p[GNB_UESS_AGG_LEVEL_LIST_IDX].numelt > 0) {
    agg_level_list = gnb_p[GNB_UESS_AGG_LEVEL_LIST_IDX].iptr;
    num_agg_levels = gnb_p[GNB_UESS_AGG_LEVEL_LIST_IDX].numelt;
  }
  memcpy(config.num_agg_level_candidates, agg_level_list, sizeof(int) * num_agg_levels);
  LOG_I(NR_MAC,
        "Candidates per PDCCH aggregation level on UESS: L1: %d, L2: %d, L4: %d, L8: %d, L16: %d\n",
        config.num_agg_level_candidates[PDCCH_AGG_LEVEL1],
        config.num_agg_level_candidates[PDCCH_AGG_LEVEL2],
        config.num_agg_level_candidates[PDCCH_AGG_LEVEL4],
        config.num_agg_level_candidates[PDCCH_AGG_LEVEL8],
        config.num_agg_level_candidates[PDCCH_AGG_LEVEL16]);

  config.redcap = get_redcap_config(idx);
  config.ptrs = get_ptrs_config(idx);

  config.timer_config.sr_ProhibitTimer = *timer_p[GNB_TIMERS_SR_PROHIBIT_TIMER_IDX].iptr;
  config.timer_config.sr_TransMax = *timer_p[GNB_TIMERS_SR_TRANS_MAX_IDX].iptr;
  config.timer_config.sr_ProhibitTimer_v1700 = *timer_p[GNB_TIMERS_SR_PROHIBIT_TIMER_V1700_IDX].iptr;
  config.timer_config.t300 = *timer_p[GNB_TIMERS_T300_IDX].iptr;
  config.timer_config.t301 = *timer_p[GNB_TIMERS_T301_IDX].iptr;
  config.timer_config.t310 = *timer_p[GNB_TIMERS_T310_IDX].iptr;
  config.timer_config.n310 = *timer_p[GNB_TIMERS_N310_IDX].iptr;
  config.timer_config.t311 = *timer_p[GNB_TIMERS_T311_IDX].iptr;
  config.timer_config.n311 = *timer_p[GNB_TIMERS_N311_IDX].iptr;
  config.timer_config.t319 = *timer_p[GNB_TIMERS_T319_IDX].iptr;
  LOG_I(GNB_APP,
        "sr_ProhibitTimer %d, sr_TransMax %d, sr_ProhibitTimer_v1700 %d, "
        "t300 %d, t301 %d, t310 %d, n310 %d, t311 %d, n311 %d, t319 %d\n",
        config.timer_config.sr_ProhibitTimer,
        config.timer_config.sr_TransMax,
        config.timer_config.sr_ProhibitTimer_v1700,
        config.timer_config.t300,
        config.timer_config.t301,
        config.timer_config.t310,
        config.timer_config.n310,
        config.timer_config.t311,
        config.timer_config.n311,
        config.timer_config.t319);

  config.pusch.target_snrx10 = *gpd(macrlc_p, macrlc_np, MACRLC_PUSCHTARGETSNRX10)->iptr;
  config.pusch.rssi_threshold = *gpd(macrlc_p, macrlc_np, MACRLC_PUSCH_RSSI_THRESHOLD)->iptr;
  config.pucch.rssi_threshold = *gpd(macrlc_p, macrlc_np, MACRLC_PUCCH_RSSI_THRESHOLD)->iptr;
  config.pucch.target_snrx10 = *gpd(macrlc_p, macrlc_np, MACRLC_PUCCHTARGETSNRX10)->iptr;
  config.ul_prbblack_SNR_threshold = *gpd(macrlc_p, macrlc_np, MACRLC_UL_PRBBLACK_SNR_THRESHOLD)->iptr;
  config.pucch.failure_thres = *gpd(macrlc_p, macrlc_np, MACRLC_PUCCHFAILURETHRES)->iptr;
  config.pusch.failure_thres = *gpd(macrlc_p, macrlc_np, MACRLC_PUSCHFAILURETHRES)->iptr;
  LOG_I(NR_MAC,
        "PUSCH Target %d RSSI thresh %d Failure %d, PUCCH Target %d RSSI thresh %d Failure %d\n",
        config.pusch.target_snrx10,
        config.pusch.rssi_threshold,
        config.pusch.failure_thres,
        config.pucch.target_snrx10,
        config.pucch.rssi_threshold,
        config.pucch.failure_thres);

  // BWP
  get_bwp_config(&config, bw);
  AssertFatal(config.num_additional_bwps <= 4, "Impossible to configure more than 4 additional BWPs\n");
  config.first_active_bwp = *gnb_p[GNB_1ST_ACTIVE_BWP_IDX].iptr;
  AssertFatal(config.first_active_bwp <= config.num_additional_bwps, "1st active BWP does not belog to the configured BWPs\n");

  // read before resolving bf_method: the table decides how L1 treats the beam IDs
  config.bt.num_beams = 0;
  config.bt.num_weights_per_beam = 0;
  config.bt.beam_ids = NULL;
  config.bt.beam_weights = NULL;
  char **fptr = gpd(macrlc_p, macrlc_np, MACRLC_DBT_FILE)->strptr;
  if (fptr && *fptr && **fptr != '\0') {
    LOG_I(GNB_APP, "loading DBT table from file %s\n", *fptr);
    config.bt.beam_weights =
        read_dbt_from_csv(*fptr, &config.bt.num_beams, &config.bt.num_weights_per_beam, &config.bt.beam_ids);
  } else {
    char prefix[MAX_OPTNAME_SIZE * 2 + 8];
    snprintf(prefix, sizeof(prefix), MACRLC_LIST ".[%d]", idx);
    config.bt.beam_weights =
        read_dbt_from_config(prefix, &config.bt.num_beams, &config.bt.num_weights_per_beam, &config.bt.beam_ids);
  }
  const bool have_dbt = config.bt.num_beams > 0;
  if (have_dbt)
    check_dbt_beam_ids_unique(&config.bt);

  // config_get_processedint() takes only paramdef_t *, so cast const away
  paramdef_t *p_bf = (paramdef_t *)gpd(macrlc_p, macrlc_np, MACRLC_BF_METHOD);
  config.beam_config.bf_method = config_get_processedint(cfg, p_bf);
  config.beam_config.beams_per_period = *gpd(macrlc_p, macrlc_np, MACRLC_BEAMS_PERIOD)->u8ptr;
  config.beam_config.beam_duration = *gpd(macrlc_p, macrlc_np, MACRLC_BEAM_DURATION)->u8ptr;
  // TODO config_isparamset doesn't seem to work for array types, checking numelt instead
  int num_ssb_beams = gpd(macrlc_p, macrlc_np, MACRLC_SSB_BEAMS_LIST)->numelt;
  switch (config.beam_config.bf_method) {
    case BF_METHOD_STRAIGHT_WIRE:
      AssertFatal(num_ssb_beams == 0,
                  MACRLC_SSB_BEAMS_LIST " is only meaningful if " MACRLC_BF_METHOD " is not \"straight-wire\"\n");
      AssertFatal(!have_dbt,
                  "a digital beam table is only meaningful if " MACRLC_BF_METHOD " is not \"straight-wire\"\n");
      break;
    case BF_METHOD_DAS:
      AssertFatal(num_ssb_beams == 0, "no need to set " MACRLC_SSB_BEAMS_LIST " in case of DAS\n");
      AssertFatal(!have_dbt, "no need to configure a digital beam table in case of DAS\n");
      // the beam index is the logical antenna port index, nothing to configure
      config.beam_config.beam_id_to_ru = true;
      break;
    case BF_METHOD_PREDEFINED:
      // always explicit: a digital beam table usually holds many more beams than SSBs
      AssertFatal(num_ssb_beams > 0, "predefined beamforming needs " MACRLC_SSB_BEAMS_LIST "\n");
      config.ssb_beams = calloc_or_fail(num_ssb_beams, sizeof(*config.ssb_beams));
      for (int b = 0; b < num_ssb_beams; b++)
        config.ssb_beams[b] = gpd(macrlc_p, macrlc_np, MACRLC_SSB_BEAMS_LIST)->iptr[b];
      if (have_dbt)
        check_ssb_beams_in_dbt(config.ssb_beams, num_ssb_beams, &config.bt);
      else
        config.beam_config.beam_id_to_ru = true;
      break;
    case BF_METHOD_DYNAMIC:
      /* TODO static beams for control, SRS-based precoding for DLSCH/ULSCH; also has
       * to disable CSI ports and DL precoding and take the DL layers from the SRS rank */
      AssertFatal(false, MACRLC_BF_METHOD " \"dynamic\" is not implemented yet\n");
      break;
    default:
      AssertFatal(false, "unhandled " MACRLC_BF_METHOD " %d\n", config.beam_config.bf_method);
  }
  /* Only Aerial applies a digital beam table. The table does reach a native L1, over
   * P5 or directly, but nr_feptx_prec() copies the samples through instead of
   * precoding them, so the weights would silently have no effect. */
  AssertFatal(!have_dbt || NFAPI_MODE == NFAPI_MODE_AERIAL,
              "a digital beam table is only supported with Aerial, the native L1 does not apply the weights\n");
  /* An nFAPI PNF has no MACRLCs section, so RCconfig_NR_L1() cannot derive DAS there
   * and the split L1 would silently run without it. Beam IDs meant for the RU or the
   * fronthaul do travel in every FAPI PDU, so predefined without a table is fine. */
  AssertFatal(config.beam_config.bf_method != BF_METHOD_DAS || NFAPI_MODE != NFAPI_MODE_VNF,
              MACRLC_BF_METHOD " \"das\" is not supported with a split L1: the PNF has no " MACRLC_LIST
              " section to read it from\n");
  config.num_ssb_beams = num_ssb_beams;
  LOG_I(GNB_APP,
        "Beamforming method %s: %d beams per period, beam duration %d slot(s), %d static beam(s), %d digital beam(s)\n",
        *gpd(macrlc_p, macrlc_np, MACRLC_BF_METHOD)->strptr,
        config.beam_config.beams_per_period,
        config.beam_config.beam_duration,
        config.num_ssb_beams,
        config.bt.num_beams);
  char **removed_ab = gpd(macrlc_p, macrlc_np, MACRLC_ANALOG_BEAMFORMING_REMOVED)->strptr;
  AssertFatal(removed_ab == NULL || *removed_ab == NULL,
              MACRLC_ANALOG_BEAMFORMING_REMOVED " removed, use " MACRLC_BF_METHOD
              " = \"straight-wire\"|\"das\"|\"predefined\"|\"dynamic\" instead\n");
  AssertFatal(gpd(macrlc_p, macrlc_np, MACRLC_BEAM_WEIGHTS_LIST_REMOVED)->numelt == 0,
              MACRLC_BEAM_WEIGHTS_LIST_REMOVED " removed, use " MACRLC_SSB_BEAMS_LIST
              " instead (one beam index per transmitted SSB)\n");

  const nr_pdsch_AntennaPorts_t *p = &config.pdsch_AntennaPorts;
  int beams_per_period = config.beam_config.beams_per_period;
  if (*num_tx > 0) {
    AssertFatal(*num_tx >= p->XP * p->N1 * p->N2 * beams_per_period,
                "Number of logical antenna ports (set in config file with pdsch_AntennaPorts and beams_per_period) cannot be "
                "larger than physical "
                "antennas (nb_tx)\n");
    AssertFatal(p->XP * p->N1 * p->N2 <= NR_MAX_CSI_PORTS,
                "Number of antenna ports set in config file exceeds the supported value of %d\n",
                NR_MAX_CSI_PORTS);
  } else {
    *num_tx = p->XP * p->N1 * p->N2 * beams_per_period;
    LOG_E(GNB_APP, "RU information not present in config file. Assuming physical antenna ports equal to logical antenna ports %d\n", *num_tx);
  }

#ifdef ENABLE_AERIAL
  // Aerial: TXRU counts advertised to the L1 in CONFIG.request, decoupled
  // from the logical antenna ports above
  // NOTE: this is not an array: for each cell, we fill identical information
  // from the (single) "Aerial" section
  GET_PARAMS(Aerial_Params, AERIALPARAMS_DESC, CONFIG_STRING_AERIAL);
  config.aerial.num_tx_ant = *gpd(Aerial_Params, sizeofArray(Aerial_Params), AERIAL_NUM_TX_ANT)->iptr;
  config.aerial.num_rx_ant = *gpd(Aerial_Params, sizeofArray(Aerial_Params), AERIAL_NUM_RX_ANT)->iptr;
  AssertFatal(config.aerial.num_tx_ant >= 0 && config.aerial.num_tx_ant <= 64
                  && config.aerial.num_rx_ant >= 0 && config.aerial.num_rx_ant <= 64,
              "Aerial num_tx_ant (%d) and num_rx_ant (%d) must be in 0..64\n",
              config.aerial.num_tx_ant,
              config.aerial.num_rx_ant);
  if (config.aerial.num_tx_ant > 0 || config.aerial.num_rx_ant > 0)
    LOG_I(GNB_APP, "Aerial TXRUs: num_tx_ant %d num_rx_ant %d\n", config.aerial.num_tx_ant, config.aerial.num_rx_ant);

  /* Aerial applies the beam weights, and the SRS-based precoder, per TXRU and
   * cannot derive that count from the logical antenna ports. */
  if (NFAPI_MODE == NFAPI_MODE_AERIAL
      && (config.beam_config.bf_method == BF_METHOD_PREDEFINED || config.beam_config.bf_method == BF_METHOD_DYNAMIC))
    AssertFatal(config.aerial.num_tx_ant > 0 && config.aerial.num_rx_ant > 0,
                MACRLC_BF_METHOD " \"%s\" needs the TXRU counts, set " CONFIG_STRING_AERIAL
                "." AERIAL_NUM_TX_ANT " and " CONFIG_STRING_AERIAL "." AERIAL_NUM_RX_ANT "\n",
                *gpd(macrlc_p, macrlc_np, MACRLC_BF_METHOD)->strptr);
#endif

  // Read spatial stream indices
  config_spatial_stream_index(macrlc_p, macrlc_np, &config, *num_tx);

  return config;
}

/*
 * \brief update some parameters in cell directly. All these parameters most
 * likely should be moved into nr_mac_config_t and be read in get_mac_config().
 */
static void config_nr_mac_update_cell(nr_cell_sched_t *cell,
                                      const nr_mac_config_t *config,
                                      const NR_BWP_UplinkCommon_t *bwp,
                                      int idx,
                                      const paramdef_t *macrlc_p,
                                      int macrlc_np)
{
  cell->ulsch_max_frame_inactivity = *gpd(macrlc_p, macrlc_np, MACRLC_ULSCH_MAX_FRAME_INACTIVITY)->uptr;
  NR_bler_options_t *dl_bler_options = &cell->dl_bler;
  dl_bler_options->upper = *gpd(macrlc_p, macrlc_np, MACRLC_DL_BLER_TARGET_UPPER)->dblptr;
  dl_bler_options->lower = *gpd(macrlc_p, macrlc_np, MACRLC_DL_BLER_TARGET_LOWER)->dblptr;
  dl_bler_options->min_mcs = *gpd(macrlc_p, macrlc_np, MACRLC_DL_MIN_MCS)->u8ptr;
  dl_bler_options->max_mcs = *gpd(macrlc_p, macrlc_np, MACRLC_DL_MAX_MCS)->u8ptr;
  if (config->disable_harq)
    dl_bler_options->harq_round_max = 1;
  else
    dl_bler_options->harq_round_max = *gpd(macrlc_p, macrlc_np, MACRLC_DL_HARQ_ROUND_MAX)->u8ptr;
  NR_bler_options_t *ul_bler_options = &cell->ul_bler;
  ul_bler_options->upper = *gpd(macrlc_p, macrlc_np, MACRLC_UL_BLER_TARGET_UPPER)->dblptr;
  ul_bler_options->lower = *gpd(macrlc_p, macrlc_np, MACRLC_UL_BLER_TARGET_LOWER)->dblptr;
  ul_bler_options->min_mcs = *gpd(macrlc_p, macrlc_np, MACRLC_UL_MIN_MCS)->u8ptr;
  ul_bler_options->max_mcs = *gpd(macrlc_p, macrlc_np, MACRLC_UL_MAX_MCS)->u8ptr;
  if (config->disable_harq)
    ul_bler_options->harq_round_max = 1;
  else
    ul_bler_options->harq_round_max = *gpd(macrlc_p, macrlc_np, MACRLC_UL_HARQ_ROUND_MAX)->u8ptr;
  cell->min_grant_prb = *gpd(macrlc_p, macrlc_np, MACRLC_MIN_GRANT_PRB)->u16ptr;
  long sc_fdma = NR_PUSCH_Config__transformPrecoder_enabled;
  if (bwp->rach_ConfigCommon->choice.setup->msg3_transformPrecoder != NULL)
    sc_fdma = *bwp->rach_ConfigCommon->choice.setup->msg3_transformPrecoder;
  int new_min = check_sc_fdma_rbsize(sc_fdma, cell->min_grant_prb);
  // NR_PUSCH_Config__transformPrecoder_enabled	= 0 |	NR_PUSCH_Config__transformPrecoder_disabled	= 1, so !uses_sc_fdma should
  // be used
  if (sc_fdma == NR_PUSCH_Config__transformPrecoder_enabled && cell->min_grant_prb != new_min) {
    LOG_W(NR_MAC,
          "min_rb value is set as %d. In SC-FDMA, it should be under format 2^x*3^y*5^z and has been automatically decreased "
          "to %d.\n",
          cell->min_grant_prb,
          new_min);
   cell->min_grant_prb = new_min;
  }
  cell->identity_pm = *gpd(macrlc_p, macrlc_np, MACRLC_IDENTITY_PM)->u8ptr;
  // PRB Blacklist
  uint16_t prbbl[MAX_BWP_SIZE] = {0};
  int num_ulprbbl = get_prb_blacklist(idx, prbbl);
  if (num_ulprbbl != -1) {
    LOG_I(NR_PHY, "Copying %d blacklisted PRB to L1 context\n", num_ulprbbl);
    memcpy(cell->ulprbbl, prbbl, MAX_BWP_SIZE * sizeof(prbbl[0]));
  }
}

void RCconfig_nr_macrlc(configmodule_interface_t *cfg)
{
  nr_cell_sched_t *cell = NULL;

  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);
  GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL, GNBPARAMS_CHECK);
  int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal(num_gnbs == 1,
              "Failed to parse config file: number of gnbs for gNB %s is %d != 1\n",
              GNB_CONFIG_STRING_ACTIVE_GNBS,
              num_gnbs);

  // MAC / RLC
  GET_PARAMS_LIST(MacRLC_ParamList, MacRLC_Params, MACRLCPARAMS_DESC, MACRLC_LIST, NULL, MACRLCPARAMS_CHECK);

  // RU
  GET_PARAMS_LIST(RUParamList, RUParams, RUPARAMS_DESC, CONFIG_STRING_RU_LIST, NULL);
  // TODO -1 means "don't know": temporary solution for 3rd party RU or nFAPI,
  // in which case we don't have RU section present in the config file
  int num_tx = -1;
  if (RUParamList.numelt > 0) {
    num_tx = 0;
    for (int i = 0; i < RUParamList.numelt; i++)
      num_tx += *(RUParamList.paramarray[i][RU_NB_TX_IDX].uptr);
  }

  nr_rlc_configuration_t default_rlc_config;
  config_rlc(cfg, &default_rlc_config);

  ngran_node_t node_type = get_node_type();
  gNB_MAC_INST *nrmac = mac_top_init_gNB(node_type, &default_rlc_config, &cell);

  int idx = 0;
  AssertFatal(MacRLC_ParamList.numelt == 1, "only one MACRLCs section supported!\n");
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%d].%s", GNB_CONFIG_STRING_GNB_LIST, idx, GNB_CONFIG_STRING_TIMERS_CONFIG);
  GET_PARAMS(Timers_Params, GNB_TIMERS_PARAMS_DESC, aprefix);
  NR_ServingCellConfigCommon_t *scc = get_scc_config(idx);
  const int bw = scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth;

  const paramdef_t *macrlc_p = MacRLC_ParamList.paramarray[idx];
  const int macrlc_np = sizeofArray(MacRLC_Params);
  nr_mac_config_t config = get_mac_config(cfg, &num_tx, idx, GNBParamList.paramarray[idx], Timers_Params, macrlc_p, macrlc_np, bw);

  nrmac->stats_max_ue = *gpd(macrlc_p, macrlc_np, MACRLC_STATS_MAX_UE)->iptr;
  nrmac->print_ue_stats = nrmac->stats_max_ue > 0;
  config_nr_mac_update_cell(cell, &config, scc->uplinkConfigCommon->initialUplinkBWP, idx, macrlc_p, macrlc_np);

  /* The weights are applied per TXRU: a native L1 takes that count from the RU
   * section, Aerial from num_tx_ant, which is required just above. */
  const bool have_dbt = config.bt.num_beams > 0;
  if (have_dbt) {
    if (RUParamList.numelt > 0) {
      AssertFatal(config.bt.num_weights_per_beam == num_tx,
                  "digital beam table has %d weight(s) per beam but %d TXRU(s) are configured "
                  "(sum of nb_tx over the " CONFIG_STRING_RU_LIST " section)\n",
                  config.bt.num_weights_per_beam,
                  num_tx);
    }
#ifdef ENABLE_AERIAL
    else {
      AssertFatal(config.bt.num_weights_per_beam == config.aerial.num_tx_ant,
                  "digital beam table has %d weight(s) per beam but " CONFIG_STRING_AERIAL "." AERIAL_NUM_TX_ANT
                  " is %d\n",
                  config.bt.num_weights_per_beam,
                  config.aerial.num_tx_ant);
    }
#endif
    /* The scheduler repeats the same beam ID on every beamforming port of a PDU
     * (the dig_bf_interface_list loops), so with more than one CSI port a single
     * beam's weights would be applied to all of them instead of one vector per
     * port. */
    const nr_pdsch_AntennaPorts_t *ap = &config.pdsch_AntennaPorts;
    const int csi_ports = ap->N1 * ap->N2 * ap->XP;
    AssertFatal(csi_ports == 1,
                "a digital beam table is only supported with one CSI port, but pdsch_AntennaPorts N1 %d N2 %d XP %d give "
                "%d: the same beam ID is signalled for every FAPI beamforming port\n",
                ap->N1,
                ap->N2,
                ap->XP,
                csi_ports);
  }

  nr_mac_cell_config_scc(cell, scc, &config);
  nrmac->positioning_config = RCconfig_nr_positioning();

  if (strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_N_PREFERENCE)->strptr, "local_RRC") == 0) {
    // check number of instances is same as RRC/PDCP

  } else if (strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_N_PREFERENCE)->strptr, "f1") == 0
             || strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_N_PREFERENCE)->strptr, "cudu") == 0) {
    char **f1caddr = gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_N_ADDRESS)->strptr;
    char **f1uaddr = gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_N_ADDRESS_F1U)->strptr;
    f1ap_net_config_t nc = {
      .CU_f1_ip_address = strdup(*gpd(macrlc_p, macrlc_np, MACRLC_REMOTE_N_ADDRESS)->strptr),
      .DU_f1c_ip_address = strdup(*f1caddr),
      .DU_f1u_ip_address = f1uaddr != NULL ? strdup(*f1uaddr) : strdup(*f1caddr),
      .DUport = *gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_N_PORTD)->iptr,
      .CUport = *gpd(macrlc_p, macrlc_np, MACRLC_REMOTE_N_PORTD)->iptr,
    };
    nrmac->net_config = nc;
    LOG_I(F1AP,
          "F1-C DU IPaddr %s, connect to F1-C CU %s, binding GTP to %s ports local %d remote %d\n",
          nc.DU_f1c_ip_address,
          nc.CU_f1_ip_address,
          nc.DU_f1u_ip_address,
          nc.DUport,
          nc.CUport);
  } else { // other midhaul
    AssertFatal(1 == 0, "MACRLC %d: %s unknown northbound midhaul\n", 0, *gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_N_PREFERENCE)->strptr);
  }

  if (strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_PREFERENCE)->strptr, "local_L1") == 0) {
    // triggers also PHY initialization in case we have L1 via FAPI
    nr_mac_send_config_req(nrmac, &cell->config);
  } else if (strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_PREFERENCE)->strptr, "nfapi") == 0) {
    const char *vnf_addr = *gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_S_ADDRESS)->strptr;
    uint16_t p5_port = *gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_S_PORTC)->iptr;
    uint16_t p7_port = *gpd(macrlc_p, macrlc_np, MACRLC_LOCAL_S_PORTD)->iptr;
    configure_nr_nfapi_vnf(vnf_addr, p5_port, p7_port);
  } else if(strcmp(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_PREFERENCE)->strptr, "aerial") == 0){
#ifdef ENABLE_AERIAL
    nvipc_params_t nvipc_p = {
      .nvipc_shm_prefix = strdup(*gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_SHM_PREFIX)->strptr),
      .nvipc_poll_core = *gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_POLL_CORE)->i8ptr,
      .num_phys = 1,
    };
    nrmac->nvipc_params_s = nvipc_p;
    LOG_I(GNB_APP, "Configuring VNF for Aerial connection with prefix %s\n", nvipc_p.nvipc_shm_prefix);
    // parameters are for socket-based communication, irrelevant for Aerial
    configure_nr_nfapi_vnf(NULL, 0xffff, 0xffff);

#endif
  } else { // other midhaul
    AssertFatal(1 == 0, "MACRLC %d: %s unknown southbound midhaul\n", 0, *gpd(macrlc_p, macrlc_np, MACRLC_TRANSPORT_S_PREFERENCE)->strptr);
  }

  uint64_t gnb_du_id = 0;
  uint32_t gnb_id = 0;
  char *name = NULL;
  f1ap_served_cell_info_t info;
  read_du_cell_info(NODE_IS_DU(node_type), &gnb_id, &gnb_du_id, &name, &info, 1);
  cell->nr_cellid = info.nr_cellid;
  cell->plmn = info.plmn;
  NR_COMMON_channels_t *cc = &cell->common_channels;
  cc->du_SIBs = fill_du_sibs(GNBParamList.paramarray[0]);

  if (IS_SA_MODE(get_softmodem_params()))
    nr_mac_configure_sib1(cell, &info.plmn, info.nr_cellid, *info.tac);

  // read F1 Setup information from config and generated MIB/SIB1
  // and store it at MAC for sending later
  NR_BCCH_BCH_Message_t *mib = cc->mib;
  const NR_BCCH_DL_SCH_Message_t *sib1 = cc->sib1;
  seq_arr_t *du_SIBs = cc->du_SIBs;
  f1ap_setup_req_t *req = RC_read_F1Setup(gnb_du_id, name, &info, scc, mib, sib1, du_SIBs);
  AssertFatal(req != NULL, "could not read F1 Setup information\n");
  LOG_I(GNB_APP, "Configured DU: cell ID %ld, PCI %d\n", req->cell[0].info.nr_cellid, req->cell[0].info.nr_pci);
  nrmac->f1_config.setup_req = req;
  nrmac->f1_config.gnb_id = gnb_id;

  free(name); /* read_du_cell_info() allocated memory */
}

void config_security(gNB_RRC_INST *rrc)
{
  paramdef_t sec_params[] = SECURITY_GLOBALPARAMS_DESC;
  int ret = config_get(config_get_if(), sec_params, sizeofArray(sec_params), CONFIG_STRING_SECURITY);
  int i;

  if (ret < 0) {
    LOG_W(RRC, "configuration file does not contain a \"security\" section, applying default parameters (nia2 nea0, integrity disabled for DRBs)\n");
    rrc->security.ciphering_algorithms[0]    = 0;  /* nea0 = no ciphering */
    rrc->security.ciphering_algorithms_count = 1;
    rrc->security.integrity_algorithms[0]    = 2;  /* nia2 */
    rrc->security.integrity_algorithms[1]    = 0;  /* nia0 = no integrity, as a fallback (but nia2 should be supported by all UEs) */
    rrc->security.integrity_algorithms_count = 2;
    rrc->security.do_drb_ciphering           = 1;  /* even if nea0 let's activate so that we don't generate cipheringDisabled in pdcp_Config */
    rrc->security.do_drb_integrity           = 0;
    return;
  }

  if (sec_params[SECURITY_CONFIG_CIPHERING_IDX].numelt > 4) {
    LOG_E(RRC, "too much ciphering algorithms in section \"security\" of the configuration file, maximum is 4\n");
    exit(1);
  }
  if (sec_params[SECURITY_CONFIG_INTEGRITY_IDX].numelt > 4) {
    LOG_E(RRC, "too much integrity algorithms in section \"security\" of the configuration file, maximum is 4\n");
    exit(1);
  }

  /* get ciphering algorithms */
  rrc->security.ciphering_algorithms_count = 0;
  for (i = 0; i < sec_params[SECURITY_CONFIG_CIPHERING_IDX].numelt; i++) {
    if (!strcmp(sec_params[SECURITY_CONFIG_CIPHERING_IDX].strlistptr[i], "nea0")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 0;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_CIPHERING_IDX].strlistptr[i], "nea1")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 1;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_CIPHERING_IDX].strlistptr[i], "nea2")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 2;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_CIPHERING_IDX].strlistptr[i], "nea3")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 3;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    LOG_E(RRC, "unknown ciphering algorithm \"%s\" in section \"security\" of the configuration file\n",
          sec_params[SECURITY_CONFIG_CIPHERING_IDX].strlistptr[i]);
    exit(1);
  }

  /* get integrity algorithms */
  rrc->security.integrity_algorithms_count = 0;
  for (i = 0; i < sec_params[SECURITY_CONFIG_INTEGRITY_IDX].numelt; i++) {
    if (!strcmp(sec_params[SECURITY_CONFIG_INTEGRITY_IDX].strlistptr[i], "nia0")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 0;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_INTEGRITY_IDX].strlistptr[i], "nia1")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 1;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_INTEGRITY_IDX].strlistptr[i], "nia2")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 2;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(sec_params[SECURITY_CONFIG_INTEGRITY_IDX].strlistptr[i], "nia3")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 3;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    LOG_E(RRC, "unknown integrity algorithm \"%s\" in section \"security\" of the configuration file\n",
          sec_params[SECURITY_CONFIG_INTEGRITY_IDX].strlistptr[i]);
    exit(1);
  }

  if (rrc->security.ciphering_algorithms_count == 0) {
    LOG_W(RRC, "no preferred ciphering algorithm set in configuration file, applying default parameters (no security)\n");
    rrc->security.ciphering_algorithms[0]    = 0;  /* nea0 = no ciphering */
    rrc->security.ciphering_algorithms_count = 1;
  }

  if (rrc->security.integrity_algorithms_count == 0) {
    LOG_W(RRC, "no preferred integrity algorithm set in configuration file, applying default parameters (nia2)\n");
    rrc->security.integrity_algorithms[0]    = 2;  /* nia2 */
    rrc->security.integrity_algorithms[1]    = 0;  /* nia0 = no integrity */
    rrc->security.integrity_algorithms_count = 2;
  }

  if (!strcmp(*sec_params[SECURITY_CONFIG_DO_DRB_CIPHERING_IDX].strptr, "yes")) {
    rrc->security.do_drb_ciphering = 1;
  } else if (!strcmp(*sec_params[SECURITY_CONFIG_DO_DRB_CIPHERING_IDX].strptr, "no")) {
    rrc->security.do_drb_ciphering = 0;
  } else {
    LOG_E(RRC, "in configuration file, bad drb_ciphering value '%s', only 'yes' and 'no' allowed\n",
          *sec_params[SECURITY_CONFIG_DO_DRB_CIPHERING_IDX].strptr);
    exit(1);
  }

  if (!strcmp(*sec_params[SECURITY_CONFIG_DO_DRB_INTEGRITY_IDX].strptr, "yes")) {
    rrc->security.do_drb_integrity = 1;
  } else if (!strcmp(*sec_params[SECURITY_CONFIG_DO_DRB_INTEGRITY_IDX].strptr, "no")) {
    rrc->security.do_drb_integrity = 0;
  } else {
    LOG_E(RRC, "in configuration file, bad drb_integrity value '%s', only 'yes' and 'no' allowed\n",
          *sec_params[SECURITY_CONFIG_DO_DRB_INTEGRITY_IDX].strptr);
    exit(1);
  }
}

static int sort_neighbour_cell_config_by_cell_id(const void *a, const void *b)
{
  const neighbour_cell_configuration_t *config_a = (const neighbour_cell_configuration_t *)a;
  const neighbour_cell_configuration_t *config_b = (const neighbour_cell_configuration_t *)b;

  if (config_a->nr_cell_id < config_b->nr_cell_id) {
    return -1;
  } else if (config_a->nr_cell_id > config_b->nr_cell_id) {
    return 1;
  }

  return 0;
}

static seq_arr_t *fill_cu_sibs(paramdef_t *GNBparamarray)
{
  if (NODE_IS_CU(get_node_type()))
    AssertFatal(GNBparamarray[GNB_DU_SIBS_IDX].numelt == 0, "This is CU, do not input DU SIBs\n");

  // TODO config_isparamset doesn't seem to work for array types, checking numelt instead
  if (GNBparamarray[GNB_CU_SIBS_IDX].numelt == 0)
    return NULL;

  if (!IS_SA_MODE(get_softmodem_params())) {
    LOG_E(GNB_APP, "CU SIBs only supported in SA mode, we'll skip them!\n");
    return NULL;
  }

  seq_arr_t *SIBs = malloc(sizeof(seq_arr_t));
  seq_arr_init(SIBs, sizeof(nr_SIBs_t));
  for (int i = 0; i < GNBparamarray[GNB_CU_SIBS_IDX].numelt; i++) {
    int sib_value = GNBparamarray[GNB_CU_SIBS_IDX].iptr[i];
    // SIB10, SIB12, SIB13, SIB14, SIB15, SIB17, SIB18, SIB19 and SIB20 are generated by the DU
    // all the rest in the CU
    AssertFatal((sib_value > 1 && sib_value < 10) || sib_value == 11 || sib_value == 16,
                "SIB%d is not a CU SIB, to be configured in DU section\n",
                sib_value);
    nr_SIBs_t *cu_sib = calloc_or_fail(1, sizeof(*cu_sib));
    cu_sib->SIB_type = sib_value;
    LOG_I(GNB_APP, "activate SIB%d at CU\n", sib_value);
    seq_arr_push_back(SIBs, cu_sib, sizeof(nr_SIBs_t));
  }
  return SIBs;
}

static bool eq_neighbour_pci(const void *vval, const void *vit)
{
  const int *pci = (const int *)vval;
  const nr_neighbour_cell_t *neighbour = (const nr_neighbour_cell_t *)vit;
  return neighbour->physicalCellId == *pci;
}

/** @brief Find neighbour by PCI within a cell's neighbour list (first match) */
static nr_neighbour_cell_t *get_neighbour_by_pci(seq_arr_t *neighbour_cells, int pci)
{
  elm_arr_t elm = find_if(neighbour_cells, &pci, eq_neighbour_pci);
  if (elm.found)
    return (nr_neighbour_cell_t *)elm.it;
  return NULL;
}

/** @brief Parse gNB-level inter-frequency list (frequency_list) into a shared array.
 * This parses gNBs.[gnb_idx].frequency_list once and validates per-frequency SIB4
 * parameters. The resulting list can then be reused for all serving cells.
 * @param[out] inter_freqs Sequence array of nr_inter_freq_cfg_t to populate
 * @param[in]  gnb_idx     gNB index */
static bool eq_inter_freq_arfcn_scs(const void *vval, const void *vit)
{
  const nr_inter_freq_cfg_t *key = (const nr_inter_freq_cfg_t *)vval;
  const nr_inter_freq_cfg_t *cur = (const nr_inter_freq_cfg_t *)vit;
  return key->arfcn == cur->arfcn && key->scs == cur->scs;
}

/** @brief Insert an inter-frequency config if (ARFCN,SCS) is unique in the list.
 * Fails fast if a duplicate is found.
 * @param[in,out] inter_freqs Existing inter-frequency list
 * @param[in]     f           Candidate entry to insert (by value)
 * @param[in]     gnb_idx     gNB index for logging
 */
static void add_inter_freq(seq_arr_t *inter_freqs, const nr_inter_freq_cfg_t *f, uint8_t gnb_idx)
{
  const nr_inter_freq_cfg_t key = {.arfcn = f->arfcn, .scs = f->scs};
  elm_arr_t dup = find_if(inter_freqs, (void *)&key, eq_inter_freq_arfcn_scs);
  if (dup.found) {
    AssertFatal(false, "gNB %d: duplicate inter-freq carrier ARFCN %d, SCS %d in frequency_list\n", gnb_idx, f->arfcn, f->scs);
  }

  seq_arr_push_back(inter_freqs, (void *)f, sizeof(*f));
}

static void parse_inter_freq_list(seq_arr_t *inter_freqs, const uint8_t gnb_idx)
{
  seq_arr_init(inter_freqs, sizeof(nr_inter_freq_cfg_t));

  char freq_path[MAX_OPTNAME_SIZE + 8];
  snprintf(freq_path, sizeof(freq_path), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, gnb_idx);
  GET_PARAMS_LIST(freq_list, freq_params, GNBFREQUENCYPARAMS_DESC, GNB_CONFIG_STRING_FREQUENCY_LIST, freq_path);

  for (int i = 0; i < freq_list.numelt; ++i) {
    const paramdef_t *freq_item = freq_list.paramarray[i];
    nr_inter_freq_cfg_t f = {0};
    f.arfcn = *gpd(freq_item, sizeofArray(freq_params), GNB_CONFIG_STRING_FREQUENCY_ABS_FREQ_SSB)->i64ptr;
    f.scs = *gpd(freq_item, sizeofArray(freq_params), GNB_CONFIG_STRING_FREQUENCY_SCS)->uptr;

    char cfg_path[MAX_OPTNAME_SIZE + 16];
    snprintf(cfg_path,
             sizeof(cfg_path),
             "%s.[%i].%s.[%i]",
             GNB_CONFIG_STRING_GNB_LIST,
             gnb_idx,
             GNB_CONFIG_STRING_FREQUENCY_LIST,
             i);
    GET_PARAMS(freq_cfg, GNBFREQUENCYCONFIGPARAMS_DESC, cfg_path);

    nr_neighbour_cell_sib4_freq_t *p = &f.freq_cfg;

    p->cellReselectionPriority = *freq_cfg[GNB_CONFIG_FREQUENCY_CELL_RESEL_PRIO_IDX].iptr;
    p->threshX_HighP = *freq_cfg[GNB_CONFIG_FREQUENCY_THRESH_X_HIGH_P_IDX].iptr;
    p->threshX_LowP = *freq_cfg[GNB_CONFIG_FREQUENCY_THRESH_X_LOW_P_IDX].iptr;
    p->threshX_HighQ = *freq_cfg[GNB_CONFIG_FREQUENCY_THRESH_X_HIGH_Q_IDX].iptr;
    p->threshX_LowQ = *freq_cfg[GNB_CONFIG_FREQUENCY_THRESH_X_LOW_Q_IDX].iptr;
    p->q_OffsetFreq = *freq_cfg[GNB_CONFIG_FREQUENCY_Q_OFFSET_FREQ_IDX].iptr;
    f.q_RxLevMin = *freq_cfg[GNB_CONFIG_FREQUENCY_Q_RXLEVMIN_IDX].iptr;
    f.t_ReselectionNR = *freq_cfg[GNB_CONFIG_FREQUENCY_T_RESEL_NR_IDX].uptr;

    /* Add inter-frequency configuration to the list. */
    add_inter_freq(inter_freqs, &f, gnb_idx);

    LOG_I(GNB_APP,
          "gNB %d: SIB4 inter-freq ARFCN %d, SCS %d, prio=%d, ThreshX_HighP=%d, ThreshX_LowP=%d, ThreshX_HighQ=%d, "
          "ThreshX_LowQ=%d, q_OffsetFreq=%d\n",
          gnb_idx,
          f.arfcn,
          f.scs,
          f.freq_cfg.cellReselectionPriority,
          f.freq_cfg.threshX_HighP,
          f.freq_cfg.threshX_LowP,
          f.freq_cfg.threshX_HighQ,
          f.freq_cfg.threshX_LowQ,
          f.freq_cfg.q_OffsetFreq);
  }
}

static void set_sib3_offsets(nr_neighbour_cell_t *n, const paramdef_t *cell_params, int n_cell_params)
{
  nr_neighbour_cell_neighbor_offset_t *sib3_off = &n->sib3.offset;

  /* SIB3 per-neighbour offsets */
  sib3_off->q_OffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_OFFSET_CELL)->iptr;
  sib3_off->q_RxLevMinOffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_RXLEVMIN_OFFSET_CELL)->iptr;
  sib3_off->q_QualMinOffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_QUALMIN_OFFSET_CELL)->iptr;
}

static void set_sib4_offsets(nr_neighbour_cell_t *n, const paramdef_t *cell_params, int n_cell_params)
{
  nr_neighbour_cell_neighbor_offset_t *sib4_off = &n->sib4.offset;

  /* SIB4 per-neighbour offsets */
  sib4_off->q_OffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_OFFSET_CELL)->iptr;
  sib4_off->q_RxLevMinOffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_RXLEVMIN_OFFSET_CELL)->iptr;
  sib4_off->q_QualMinOffsetCell = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_Q_QUALMIN_OFFSET_CELL)->iptr;
}

/** @brief Parse and add neighbour cells for a given cell configuration
 * @param[in,out] cell Neighbour cell configuration to populate
 * @param[in] list Parameter list describing neighbour cells
 * @param[in] n_path Base path for neighbour configuration in the config tree
 * @param[in] n_cell_params Number of neighbour-cell parameters */
static void parse_neighbour_cells_list(neighbour_cell_configuration_t *cell,
                                       const paramlist_def_t *list,
                                       const char *n_path,
                                       int n_cell_params)
{
  seq_arr_init(&cell->neighbour_cells, sizeof(nr_neighbour_cell_t));

  /* Parse each neighbour cell in the list */
  for (int l = 0; l < list->numelt; ++l) {
    const paramdef_t *cell_params = list->paramarray[l];
    nr_neighbour_cell_t n = {
        .gNB_ID = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_GNB_ID)->uptr,
        .nrcell_id = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NRCELLID)->u64ptr,
        .physicalCellId = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_PHYSICAL_ID)->uptr,
        .subcarrierSpacing = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_SCS)->uptr,
        .band = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_BAND)->uptr,
        .absoluteFrequencySSB = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_CELL_ABS_FREQ_SSB)->i64ptr,
        .tac = *gpd(cell_params, n_cell_params, GNB_CONFIG_STRING_NEIGHBOUR_TRACKING_ARE_CODE)->uptr};

    set_sib3_offsets(&n, cell_params, n_cell_params);
    set_sib4_offsets(&n, cell_params, n_cell_params);
    /* SIB4 per-frequency reselection parameters (q_OffsetFreq, thresholds, priority)
     * are populated in parse_inter_freq_list() per-frequency, not per neighbour here. */

    char p[CONFIG_MAXOPTLENGTH]; // plmn path
    snprintf(p, sizeof(p), "%s.%s.[%i].%s", n_path, GNB_CONFIG_STRING_NEIGHBOUR_CELL_LIST, l, GNB_CONFIG_STRING_NEIGHBOUR_PLMN);
    GET_PARAMS(NeighbourPlmn, GNBPLMNPARAMS_DESC, p);
    n.plmn = extract_plmn_from_params(NeighbourPlmn, sizeofArray(NeighbourPlmn));

    LOG_I(GNB_APP,
          "Neighbour[%d]: cellId %ld, PLMN %03u.%0*u, gNB %d, PCI %d, SCS %d, SSB ARFCN %u, TAC %u\n",
          l,
          n.nrcell_id,
          n.plmn.mcc,
          n.plmn.mnc_digit_length,
          n.plmn.mnc,
          n.gNB_ID,
          n.physicalCellId,
          n.subcarrierSpacing,
          n.absoluteFrequencySSB,
          n.tac);

    /* Check for duplicate PCI in the neighbour list */
    if (get_neighbour_by_pci(&cell->neighbour_cells, n.physicalCellId) != NULL) {
      LOG_E(GNB_APP, "Cell %ld: duplicate PCI %d in neighbour list (NCI=%ld)\n", cell->nr_cell_id, n.physicalCellId, n.nrcell_id);
      AssertFatal(false, "PCI must be unique within a cell's neighbour list\n");
    }

    seq_arr_push_back(&cell->neighbour_cells, &n, sizeof(n));
  }
}

/** @brief Sort neighbour cell configuration by cell ID
 * @param[in] rrc Pointer to RRC instance */
static void sort_neighbour_configuration(gNB_RRC_INST *rrc)
{
  void *base = seq_arr_front(rrc->neighbour_cell_configuration);
  size_t nmemb = seq_arr_size(rrc->neighbour_cell_configuration);
  size_t element_size = sizeof(neighbour_cell_configuration_t);
  qsort(base, nmemb, element_size, sort_neighbour_cell_config_by_cell_id);
}

/** @brief Fill neighbour cell configuration from config file
 * @param[in] gnb_idx gNB index
 * @param[in,out] rrc Pointer to RRC instance */
static void fill_neighbour_cell_configuration(const uint8_t gnb_idx, gNB_RRC_INST *rrc)
{
  char path[MAX_OPTNAME_SIZE + 8];
  snprintf(path, sizeof(path), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, gnb_idx);
  GET_PARAMS_LIST(nlist, nparams, GNB_NEIGHBOUR_LIST_PARAM_LIST, GNB_CONFIG_STRING_NEIGHBOUR_LIST, path);

  LOG_I(GNB_APP, "gNB %d: neighbour_list has %d serving cell(s)\n", gnb_idx, nlist.numelt);
  if (nlist.numelt < 1)
    return;

  /* Parse gNB-level inter-frequency configuration once and reuse it for all cells. */
  parse_inter_freq_list(&rrc->inter_freqs, gnb_idx);

  rrc->neighbour_cell_configuration = malloc_or_fail(sizeof(seq_arr_t));
  seq_arr_init(rrc->neighbour_cell_configuration, sizeof(neighbour_cell_configuration_t));

  for (int elm = 0; elm < nlist.numelt; ++elm) {
    neighbour_cell_configuration_t cell = {0};
    cell.nr_cell_id = *gpd(nlist.paramarray[elm], sizeofArray(nparams), GNB_CONFIG_STRING_NRCELLID)->u64ptr;
    snprintf(path, sizeof(path), "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_NEIGHBOUR_LIST, elm);
    GET_PARAMS_LIST(ncell_list, ncell_params, GNBNEIGHBOURCELLPARAMS_DESC, GNB_CONFIG_STRING_NEIGHBOUR_CELL_LIST, path);
    LOG_I(GNB_APP, "Cell %ld has %d neighbours configured\n", cell.nr_cell_id, ncell_list.numelt);
    if (ncell_list.numelt < 1)
      continue;
    parse_neighbour_cells_list(&cell, &ncell_list, path, sizeofArray(ncell_params));
    seq_arr_push_back(rrc->neighbour_cell_configuration, &cell, sizeof(cell));
  }

  sort_neighbour_configuration(rrc);
}

static void fill_measurement_configuration(uint8_t gnb_idx, gNB_RRC_INST *rrc)
{
  char measurement_path[MAX_OPTNAME_SIZE + 8];
  snprintf(measurement_path, sizeof(measurement_path), "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION);

  nr_measurement_configuration_t *measurementConfig = &rrc->measurementConfiguration;
  // Periodical Event Configuration
  char periodic_event_path[MAX_OPTNAME_SIZE + 8];
  snprintf(periodic_event_path,
           sizeof(periodic_event_path),
           "%s.[%i].%s.%s",
           GNB_CONFIG_STRING_GNB_LIST,
           gnb_idx,
           GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION,
           MEASUREMENT_EVENTS_PERIODICAL);
  GET_PARAMS(Periodical_EventParams, MEASUREMENT_PERIODICAL_GLOBALPARAMS_DESC, periodic_event_path);

  if (*Periodical_EventParams[MEASUREMENT_EVENTS_ENABLE_IDX].i64ptr) {
    nr_per_event_t *periodic_event = (nr_per_event_t *)calloc(1, sizeof(nr_per_event_t));
    periodic_event->includeBeamMeasurements = *Periodical_EventParams[MEASUREMENT_EVENTS_INCLUDE_BEAM_MEAS_IDX].i64ptr;
    periodic_event->maxReportCells = *Periodical_EventParams[MEASUREMENT_EVENTS_MAX_RS_INDEX_TO_REPORT].i64ptr;

    measurementConfig->per_event = periodic_event;
  }

  // A2 Event Configuration
  char a2_path[MAX_OPTNAME_SIZE + 8];
  snprintf(a2_path, sizeof(a2_path), "%s.[%i].%s.%s", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION, MEASUREMENT_EVENTS_A2);
  GET_PARAMS(A2_EventParams, MEASUREMENT_A2_GLOBALPARAMS_DESC, a2_path);

  if (*A2_EventParams[MEASUREMENT_EVENTS_ENABLE_IDX].i64ptr) {
    nr_a2_event_t *a2_event = (nr_a2_event_t *)calloc(1, sizeof(nr_a2_event_t));
    a2_event->threshold_RSRP = *A2_EventParams[MEASUREMENT_EVENTS_A2_THRESHOLD_IDX].i64ptr;
    a2_event->timeToTrigger = *A2_EventParams[MEASUREMENT_EVENTS_TIMETOTRIGGER_IDX].i64ptr;

    measurementConfig->a2_event = a2_event;
  }
  // A3 Event Configuration
  GET_PARAMS_LIST(A3_EventList, A3_EventParams, MEASUREMENT_A3_GLOBALPARAMS_DESC, MEASUREMENT_EVENTS_A3, measurement_path);

  LOG_D(GNB_APP, "HO LOG: A3 Configuration Exists: %d\n", A3_EventList.numelt);

  if (A3_EventList.numelt < 1)
    return;

  measurementConfig->a3_event_list = malloc(sizeof(seq_arr_t));
  seq_arr_init(measurementConfig->a3_event_list, sizeof(nr_a3_event_t));
  for (int i = 0; i < A3_EventList.numelt; i++) {
    nr_a3_event_t a3_event = {0};
    a3_event.pci = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_PCI_ID_IDX].i64ptr;
    AssertFatal(a3_event.pci >= -1 && a3_event.pci < 1024,
                "entry %s.%s must be -1<=PCI<1024, but is %d\n",
                measurement_path,
                MEASUREMENT_EVENTS_PCI_ID,
                a3_event.pci);
    a3_event.timeToTrigger = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_TIMETOTRIGGER_IDX].i64ptr;
    a3_event.a3_offset = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_OFFSET_IDX].i64ptr;
    a3_event.hysteresis = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_HYSTERESIS_IDX].i64ptr;

    if (a3_event.pci == -1)
      measurementConfig->is_default_a3_configuration_exists = true;

    seq_arr_push_back(measurementConfig->a3_event_list, &a3_event, sizeof(nr_a3_event_t));
  }
}

/** @brief Parse and populate SIB2 configuration from config file
 * @param[in] gnb_idx gNB index
 * @return populated SIB2 configuration */
static sib2_config_t fill_sib2_configuration(uint8_t gnb_idx)
{
  sib2_config_t sib2 = {0};

  char sib2_path[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(sib2_path, sizeof(sib2_path), "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_SIB2_CONFIG);

  GET_PARAMS(SIB2Params, GNBSIB2PARAMS_DESC, sib2_path);
  const int n_sib2_params = sizeofArray(SIB2Params);

  cell_reselection_info_common_t *common = &sib2.cell_reselection_info_common;
  common->q_Hyst = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_Q_HYST)->iptr;
  /* speedStateReselectionPars */
  common->speedStateReselectionPars = calloc_or_fail(1, sizeof(*common->speedStateReselectionPars));
  sib2_speed_state_reselection_pars_t *speed = common->speedStateReselectionPars;

  /* Cell reselection serving frequency information */
  cell_reselection_serving_freq_info_t *serv = &sib2.cell_reselection_serving_freq_info;
  serv->cellReselectionPriority = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_CELLRESEL_PRIORITY)->iptr;
  serv->threshServingLowP = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_THRESH_SERVING_LOW_P)->iptr;
  serv->threshServingLowQ = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_THRESH_SERVING_LOW_Q)->iptr;
  serv->s_NonIntraSearchP = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_S_NONINTRASEARCH_P)->iptr;
  serv->s_NonIntraSearchQ = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_S_NONINTRASEARCH_Q)->iptr;

  /* Intra-frequency cell reselection parameters */
  intra_freq_cell_reselection_info_t *intra = &sib2.intra_freq_cell_reselection_info;
  intra->q_RxLevMin = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_Q_RXLEVMIN)->iptr;
  intra->q_QualMin = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_Q_QUALMIN)->iptr;
  intra->s_IntraSearchP = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_S_INTRASEARCH_P)->iptr;
  intra->s_IntraSearchQ = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_S_INTRASEARCH_Q)->iptr;
  intra->t_ReselectionNR = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_T_RESEL_NR)->iptr;

  /* deriveSSB_IndexFromCell (0/1) */
  int derive_ssb = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_DERIVE_SSB_IDX)->iptr;
  sib2.deriveSSB_IndexFromCell = (derive_ssb != 0);

  /* Speed-state pars: optional parameters are omitted from the config file (default applied), not set to -1. */
  speed->t_Evaluation = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_T_EVAL)->iptr;
  speed->t_HystNormal = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_T_HYST_NORMAL)->iptr;
  speed->n_CellChangeMedium = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_N_CELL_CHG_MED)->iptr;
  speed->n_CellChangeHigh = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_N_CELL_CHG_HIGH)->iptr;
  speed->sf_Medium = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_SF_MEDIUM)->iptr;
  speed->sf_High = *gpd(SIB2Params, n_sib2_params, GNB_CONFIG_STRING_SIB2_SPEED_SF_HIGH)->iptr;

  LOG_I(GNB_APP,
        "gNB %d SIB2 config: q_Hyst=%d dB, cellReselPrio=%d, q_RxLevMin=%d dBm, t_ReselectionNR=%d s\n",
        gnb_idx,
        common->q_Hyst,
        serv->cellReselectionPriority,
        intra->q_RxLevMin,
        intra->t_ReselectionNR);

  return sib2;
}

/**
 * @brief Allocates and initializes RRC instances
 *        Currently assuming 1 instance
 */
gNB_RRC_INST *RCconfig_NRRRC()
{
  // Allocate memory for 1 RRC instance
  gNB_RRC_INST *rrc = calloc(1, sizeof(*rrc));

  int num_gnbs = 0;
  int32_t gnb_id = 0;
  int k = 0;
  int i = 0;

  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);
  num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal (i < num_gnbs,"Failed to parse config file no %dth element in %s \n", i, GNB_CONFIG_STRING_ACTIVE_GNBS);
  AssertFatal(num_gnbs == 1, "required section \"gNBs\" not in config!\n");

  if (num_gnbs > 0) {
    GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL);
    if (GNBParamList.paramarray[i][GNB_GNB_ID_IDX].uptr == NULL) {
    // Calculate a default gNB ID
      if (IS_SA_MODE(get_softmodem_params())) {
        uint32_t hash;
        hash = ngap_generate_gNB_id ();
        gnb_id = i + (hash & 0xFFFFFF8);
      } else {
        gnb_id = i;
      }
    } else {
      gnb_id = *(GNBParamList.paramarray[i][GNB_GNB_ID_IDX].uptr);
    }

    LOG_D(GNB_APP, "NRRRC %d: Southbound Transport %s\n", i, *(GNBParamList.paramarray[i][GNB_TRANSPORT_S_PREFERENCE_IDX].strptr));

    rrc->node_type = get_node_type();
    rrc->node_id        = gnb_id;

    if (NODE_IS_CU(rrc->node_type)) {
      char aprefix[MAX_OPTNAME_SIZE*2 + 8];
      snprintf(aprefix, sizeof(aprefix), "%s.[%d].%s", GNB_CONFIG_STRING_GNB_LIST, i, GNB_CONFIG_STRING_SCTP_CONFIG);
      GET_PARAMS(SCTPParams, GNBSCTPPARAMS_DESC, aprefix);
      LOG_I(GNB_APP,"F1AP: gNB_CU_id[%d] %d\n", k, rrc->node_id);
      rrc->node_name = strdup(*(GNBParamList.paramarray[0][GNB_GNB_NAME_IDX].strptr));
      LOG_I(GNB_APP,"F1AP: gNB_CU_name[%d] %s\n", k, rrc->node_name);
      f1ap_cu_conf_t *c = calloc_or_fail(1, sizeof(*c));
      c->type = rrc->node_type;
      c->bind_addr = strdup(*(GNBParamList.paramarray[i][GNB_LOCAL_S_ADDRESS_IDX].strptr));
      c->local_f1u_port = *GNBParamList.paramarray[i][GNB_LOCAL_S_PORTD_IDX].uptr;
      c->remote_f1u_port = *GNBParamList.paramarray[i][GNB_REMOTE_S_PORTD_IDX].uptr;
      // GNB_REMOTE_S_ADDRESS_IDX not used
      // GNB_LOCAL_S_PORTC_IDX not used
      // GNB_REMOTE_S_PORTC_IDX not used
      ittiTask_parms_t p = {.args_to_start_routine = c};
      int rc = itti_create_task(TASK_CU_F1, F1AP_CU_task, &p);
      AssertFatal(rc == 0, "Create task for F1AP CU failed\n");
    }

    // search if in active list

    nr_rrc_config_t nrrrc_config = {0};
    for (k=0; k <num_gnbs ; k++) {
      if (strcmp(GNBSParams[GNB_ACTIVE_GNBS_IDX].strlistptr[k], *(GNBParamList.paramarray[i][GNB_GNB_NAME_IDX].strptr) )== 0) {

        fill_neighbour_cell_configuration(k, rrc);

        fill_measurement_configuration(k, rrc);

        rrc->SIBs = fill_cu_sibs(GNBParamList.paramarray[i]);

        nrrrc_config.tac               = *GNBParamList.paramarray[i][GNB_TRACKING_AREA_CODE_IDX].uptr;
        AssertFatal(!GNBParamList.paramarray[i][GNB_MOBILE_COUNTRY_CODE_IDX_OLD].strptr
                    && !GNBParamList.paramarray[i][GNB_MOBILE_NETWORK_CODE_IDX_OLD].strptr,
                    "It seems that you use an old configuration file. Please change the existing\n"
                    "    tracking_area_code  =  \"1\";\n"
                    "    mobile_country_code =  \"208\";\n"
                    "    mobile_network_code =  \"93\";\n"
                    "to\n"
                    "    tracking_area_code  =  1; // no string!!\n"
                    "    plmn_list = ( { mcc = 208; mnc = 93; mnc_length = 2; } )\n");

        nrrrc_config.num_plmn = set_plmn_config(nrrrc_config.plmn, k);
        nrrrc_config.enable_sdap = *GNBParamList.paramarray[i][GNB_ENABLE_SDAP_IDX].iptr;
        LOG_I(GNB_APP, "SDAP UL/DL headers in RRC are %s\n", nrrrc_config.enable_sdap ? "present" : "absent");
        nrrrc_config.um_on_default_drb = *(GNBParamList.paramarray[i][GNB_UMONDEFAULTDRB_IDX].uptr);

      }//
    }//End for (k=0; k <num_gnbs ; k++)
    openair_rrc_gNB_configuration(rrc, &nrrrc_config);

    /* Load SIB2 configuration */
    rrc->sib2_config = fill_sib2_configuration(i);
  }//End if (num_gnbs>0)

  if (!NODE_IS_DU(rrc->node_type))
    config_security(rrc);

  config_rlc(config_get_if(), &rrc->rlc_config);
  config_pdcp(config_get_if(), &rrc->pdcp_config);

  config_nrdc(config_get_if(), &rrc->nrdc_config);

  return rrc;
}

static pthread_mutex_t rc_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool rc_done = false;

/**
 * @brief This function is initializing the RAN context
 *        the its various layer instances
 */
void NRRCConfig(void)
{
  // Call NRRCConfig only once
  pthread_mutex_lock(&rc_mutex);
  if (rc_done) {
    LOG_E(GNB_APP, "RAN Context has been already initialized\n");
    pthread_mutex_unlock(&rc_mutex);
    return;
  }

  memset((void *)&RC, 0, sizeof(RC));

  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);

  // Set num of gNBs instances
  RC.nb_nr_inst = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  AssertFatal(RC.nb_nr_inst <= NUMBER_OF_gNB_MAX,
              "Configuration error: RC.nb_nr_inst (%d) must equal NUMBER_OF_gNB_MAX (%d).\n"
              "Currently, only one instance of each layer (L1, L2, L3) is supported.\n"
              "Ensure that nb_nr_inst matches the maximum allowed gNB instances in this configuration.",
              RC.nb_nr_inst, NUMBER_OF_gNB_MAX);

  // Set num MACRLC instances
  paramlist_def_t MACRLCParamList = {MACRLC_LIST, NULL, 0};
  config_getlist(config_get_if(), &MACRLCParamList, NULL, 0, NULL);
  RC.nb_nr_macrlc_inst = MACRLCParamList.numelt;

  // Set num L1 instances
  paramlist_def_t L1ParamList = {CONFIG_STRING_L1_LIST, NULL, 0};
  config_getlist(config_get_if(), &L1ParamList, NULL, 0, NULL);
  RC.nb_nr_L1_inst = L1ParamList.numelt;

  // Set num RU instances
  paramlist_def_t RUParamList = {CONFIG_STRING_RU_LIST, NULL, 0};
  config_getlist(config_get_if(), &RUParamList, NULL, 0, NULL);
  RC.nb_RU = RUParamList.numelt;

  // Set num component carriers
  RC.nb_nr_CC = calloc_or_fail(1, sizeof(*RC.nb_nr_CC));
  *RC.nb_nr_CC = RC.nb_nr_L1_inst;
  AssertFatal(*RC.nb_nr_CC <= MAX_NUM_CCs, "Configured number of CCs (%d) not supported\n", *RC.nb_nr_CC);

  LOG_I(GNB_APP,
        "Initialized RAN Context: RC.nb_nr_inst = %d, RC.nb_nr_macrlc_inst = %d, RC.nb_nr_L1_inst = %d, RC.nb_RU = %d, "
        "RC.nb_nr_CC[0] = %d\n",
        RC.nb_nr_inst,
        RC.nb_nr_macrlc_inst,
        RC.nb_nr_L1_inst,
        RC.nb_RU,
        *RC.nb_nr_CC);

  rc_done = true;
  pthread_mutex_unlock(&rc_mutex);
}

int RCconfig_NR_X2(MessageDef *msg_p, uint32_t i) {
  int   J, l;
  char *address = NULL;
  char *cidr    = NULL;
  int                    j,k                                                           = 0;
  int32_t                gnb_id                                                        = 0;

  /* get global parameters, defined outside any section in the config file */
  GET_PARAMS(GNBSParams, GNBSPARAMS_DESC, NULL);

  NR_ServingCellConfigCommon_t *scc = calloc_or_fail(1, sizeof(*scc));
  uint64_t ssb_bitmap=0xff;
  memset((void*)scc,0,sizeof(NR_ServingCellConfigCommon_t));
  prepare_scc(scc);

  AssertFatal(i < GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt,
              "Failed to parse config file %s, %uth attribute %s \n",
              RC.config_file_name, i, GNB_CONFIG_STRING_ACTIVE_GNBS);

  if (GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt > 0) {
    // Output a list of all gNBs.
    GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL);

    if (GNBParamList.numelt > 0) {
      for (k = 0; k < GNBParamList.numelt; k++) {
        if (GNBParamList.paramarray[k][GNB_GNB_ID_IDX].uptr == NULL) {
          // Calculate a default eNB ID
          if (IS_SA_MODE(get_softmodem_params())) {
            uint32_t hash;
            hash = ngap_generate_gNB_id ();
            gnb_id = k + (hash & 0xFFFFFF8);
          } else {
            gnb_id = k;
          }
        } else {
          gnb_id = *(GNBParamList.paramarray[k][GNB_GNB_ID_IDX].uptr);
        }

        // search if in active list
        for (j = 0; j < GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt; j++) {
          if (strcmp(GNBSParams[GNB_ACTIVE_GNBS_IDX].strlistptr[j], *(GNBParamList.paramarray[k][GNB_GNB_NAME_IDX].strptr)) == 0) {
            /* map parameter checking array instances to parameter definition array instances */

            char* gnb_ipv4_address_for_NGU = NULL;
            uint32_t gnb_port_for_NGU = 0;
            char* gnb_ipv4_address_for_S1U = NULL;
            uint32_t gnb_port_for_S1U = 0;

            /* Some default/random parameters */
            X2AP_REGISTER_ENB_REQ (msg_p).eNB_id = gnb_id;

            if (strcmp(*(GNBParamList.paramarray[k][GNB_CELL_TYPE_IDX].strptr), "CELL_MACRO_GNB") == 0) {
              X2AP_REGISTER_ENB_REQ (msg_p).cell_type = CELL_MACRO_GNB;
            }else {
              AssertFatal (0,
                           "Failed to parse eNB configuration file %s, enb %u unknown value \"%s\" for cell_type choice: CELL_MACRO_ENB or CELL_HOME_ENB !\n",
                           RC.config_file_name, i, *(GNBParamList.paramarray[k][GNB_CELL_TYPE_IDX].strptr));
            }

            X2AP_REGISTER_ENB_REQ (msg_p).eNB_name         = strdup(*(GNBParamList.paramarray[k][GNB_GNB_NAME_IDX].strptr));
            X2AP_REGISTER_ENB_REQ (msg_p).tac              = *GNBParamList.paramarray[k][GNB_TRACKING_AREA_CODE_IDX].uptr;

            // PLMN
            plmn_id_t p[PLMN_LIST_MAX_SIZE];
            uint8_t num_plmn = set_plmn_config(p, k);
            if (num_plmn > 1)
              LOG_W(X2AP, "X2AP currently handles only one PLMN, ignoring the others!\n");
            X2AP_REGISTER_ENB_REQ(msg_p).mcc = p[0].mcc;
            X2AP_REGISTER_ENB_REQ(msg_p).mnc = p[0].mnc;
            X2AP_REGISTER_ENB_REQ(msg_p).mnc_digit_length = p[0].mnc_digit_length;
            AssertFatal(X2AP_REGISTER_ENB_REQ(msg_p).mnc_digit_length == 3
                        || X2AP_REGISTER_ENB_REQ(msg_p).mnc < 100,
                        "MNC %d cannot be encoded in two digits as requested (change mnc_digit_length to 3)\n",
                        X2AP_REGISTER_ENB_REQ(msg_p).mnc);

            char aprefix[MAX_OPTNAME_SIZE * 80 + 8];
            snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
            GET_PARAMS_LIST(SCCsParamList, SCCsParams, SCCPARAMS_DESC(scc), GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, aprefix);
            if (SCCsParamList.numelt > 0) {
              snprintf(aprefix, sizeof(aprefix), "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0, GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, 0);
              GET_PARAMS(SCCsParams, SCCPARAMS_DESC(scc), aprefix);
              fix_scc(scc,ssb_bitmap);
            }
            X2AP_REGISTER_ENB_REQ (msg_p).num_cc = SCCsParamList.numelt;
            for (J = 0; J < SCCsParamList.numelt ; J++) {
              struct NR_FrequencyInfoDL *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
              X2AP_REGISTER_ENB_REQ(msg_p).nr_band[J] = *frequencyInfoDL->frequencyBandList.list.array[0]; // nr_band; //78
              X2AP_REGISTER_ENB_REQ(msg_p).nrARFCN[J] = frequencyInfoDL->absoluteFrequencyPointA;
              X2AP_REGISTER_ENB_REQ (msg_p).uplink_frequency_offset[J] = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier; //0
              X2AP_REGISTER_ENB_REQ (msg_p).Nid_cell[J]= *scc->physCellId; //0
              X2AP_REGISTER_ENB_REQ(msg_p).N_RB_DL[J] =
                  frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth; // 106
              X2AP_REGISTER_ENB_REQ (msg_p).frame_type[J] = TDD;
              LOG_I(X2AP, "gNB configuration parameters: nr_band: %d, nr_ARFCN: %d, DL_RBs: %d, num_cc: %d \n",
                  X2AP_REGISTER_ENB_REQ (msg_p).nr_band[J],
                  X2AP_REGISTER_ENB_REQ (msg_p).nrARFCN[J],
                  X2AP_REGISTER_ENB_REQ (msg_p).N_RB_DL[J],
                  X2AP_REGISTER_ENB_REQ (msg_p).num_cc);
            }

            snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, k);
            GET_PARAMS_LIST(X2ParamList, X2Params, X2PARAMS_DESC, ENB_CONFIG_STRING_TARGET_ENB_X2_IP_ADDRESS, aprefix);

            AssertFatal(X2ParamList.numelt <= X2AP_MAX_NB_ENB_IP_ADDRESS,
                        "value of X2ParamList.numelt %d must be lower than X2AP_MAX_NB_ENB_IP_ADDRESS %d value: reconsider to increase X2AP_MAX_NB_ENB_IP_ADDRESS\n",
                        X2ParamList.numelt,X2AP_MAX_NB_ENB_IP_ADDRESS);
            X2AP_REGISTER_ENB_REQ (msg_p).nb_x2 = 0;

            for (l = 0; l < X2ParamList.numelt; l++) {
              X2AP_REGISTER_ENB_REQ (msg_p).nb_x2 += 1;
              strcpy(X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv4_address,*(X2ParamList.paramarray[l][ENB_X2_IPV4_ADDRESS_IDX].strptr));
              X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv4 = 1;
              X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv6 = 0;
            }

            // timers
            {
              int t_reloc_prep = 0;
              int tx2_reloc_overall = 0;
              int t_dc_prep = 0;
              int t_dc_overall = 0;
              paramdef_t p[] = {
                { "t_reloc_prep", "t_reloc_prep", 0, .iptr=&t_reloc_prep, .defintval=0, TYPE_INT, 0 },
                { "tx2_reloc_overall", "tx2_reloc_overall", 0, .iptr=&tx2_reloc_overall, .defintval=0, TYPE_INT, 0 },
                { "t_dc_prep", "t_dc_prep", 0, .iptr=&t_dc_prep, .defintval=0, TYPE_INT, 0 },
                { "t_dc_overall", "t_dc_overall", 0, .iptr=&t_dc_overall, .defintval=0, TYPE_INT, 0 }
              };
              config_get(config_get_if(), p, sizeofArray(p), aprefix);

              if (t_reloc_prep <= 0 || t_reloc_prep > 10000 ||
                  tx2_reloc_overall <= 0 || tx2_reloc_overall > 20000 ||
                  t_dc_prep <= 0 || t_dc_prep > 10000 ||
                  t_dc_overall <= 0 || t_dc_overall > 20000) {
                LOG_E(X2AP, "timers in configuration file have wrong values. We must have [0 < t_reloc_prep <= 10000] and [0 < tx2_reloc_overall <= 20000] and [0 < t_dc_prep <= 10000] and [0 < t_dc_overall <= 20000]\n");
                exit(1);
              }

              X2AP_REGISTER_ENB_REQ (msg_p).t_reloc_prep = t_reloc_prep;
              X2AP_REGISTER_ENB_REQ (msg_p).tx2_reloc_overall = tx2_reloc_overall;
              X2AP_REGISTER_ENB_REQ (msg_p).t_dc_prep = t_dc_prep;
              X2AP_REGISTER_ENB_REQ (msg_p).t_dc_overall = t_dc_overall;
            }
            // SCTP SETTING
            X2AP_REGISTER_ENB_REQ (msg_p).sctp_out_streams = SCTP_OUT_STREAMS;
            X2AP_REGISTER_ENB_REQ (msg_p).sctp_in_streams  = SCTP_IN_STREAMS;

            if (IS_SA_MODE(get_softmodem_params())) {
              snprintf(aprefix, sizeof(aprefix), "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, k, GNB_CONFIG_STRING_SCTP_CONFIG);
              GET_PARAMS(SCTPParams, GNBSCTPPARAMS_DESC, aprefix);
              X2AP_REGISTER_ENB_REQ (msg_p).sctp_in_streams = (uint16_t)*(SCTPParams[GNB_SCTP_INSTREAMS_IDX].uptr);
              X2AP_REGISTER_ENB_REQ (msg_p).sctp_out_streams = (uint16_t)*(SCTPParams[GNB_SCTP_OUTSTREAMS_IDX].uptr);
            }

            // NETWORK_INTERFACES
            snprintf(aprefix, sizeof(aprefix), "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, k, GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
            GET_PARAMS(NETParams, GNBNETPARAMS_DESC, aprefix);
            X2AP_REGISTER_ENB_REQ (msg_p).enb_port_for_X2C = (uint32_t)*(NETParams[GNB_PORT_FOR_X2C_IDX].uptr);

            //temp out
            if ((NETParams[GNB_IPV4_ADDR_FOR_X2C_IDX].strptr == NULL) || (X2AP_REGISTER_ENB_REQ (msg_p).enb_port_for_X2C == 0)) {
              LOG_E(RRC,"Add eNB IPv4 address and/or port for X2C in the CONF file!\n");
              exit(1);
            }

            cidr = *(NETParams[ENB_IPV4_ADDR_FOR_X2C_IDX].strptr);
            char *save = NULL;
            address = strtok_r(cidr, "/", &save);
            X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv6 = 0;
            X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv4 = 1;
            strcpy(X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv4_address, address);
          }
        }
      }
    }
  }

  return 0;
}

void wait_f1_setup_response(void)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  if (mac->f1_config.setup_resp != NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  LOG_W(GNB_APP, "waiting for F1 Setup Response before activating radio\n");

  /* for the moment, we keep it simple and just sleep to periodically check.
   * The actual check is protected by a mutex */
  while (mac->f1_config.setup_resp == NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    sleep(1);
    NR_SCHED_LOCK(&mac->sched_lock);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
}
static bool check_plmn_identity(const plmn_id_t *check_plmn, const plmn_id_t *plmn)
{
  return plmn->mcc == check_plmn->mcc && plmn->mnc_digit_length == check_plmn->mnc_digit_length && plmn->mnc == check_plmn->mnc;
}

int gNB_app_handle_f1ap_gnb_cu_configuration_update(f1ap_gnb_cu_configuration_update_t *gnb_cu_cfg_update) {
  int i, j, ret=0;
  LOG_I(GNB_APP, "cells_to_activate %d, RRC instances %d\n",
        gnb_cu_cfg_update->num_cells_to_activate, RC.nb_nr_inst);

  AssertFatal(gnb_cu_cfg_update->num_cells_to_activate == 1, "only one cell supported at the moment\n");
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  for (j = 0; j < gnb_cu_cfg_update->num_cells_to_activate; j++) {
    for (i = 0; i < RC.nb_nr_inst; i++) {
      f1ap_setup_req_t *setup_req = RC.nrmac[i]->f1_config.setup_req;
      // identify local index of cell j by nr_cellid, plmn identity and physical cell ID

      if (setup_req->cell[0].info.nr_cellid == gnb_cu_cfg_update->cells_to_activate[j].nr_cellid
          && check_plmn_identity(&setup_req->cell[0].info.plmn, &gnb_cu_cfg_update->cells_to_activate[j].plmn) > 0
          && setup_req->cell[0].info.nr_pci == gnb_cu_cfg_update->cells_to_activate[j].nrpci) {
        // copy system information and decode it
        AssertFatal(gnb_cu_cfg_update->cells_to_activate[j].num_SI == 0,
                    "gNB-CU Configuration Update: handling of additional SIs not implemend\n");
        ret++;
        mac->f1_config.setup_resp = malloc(sizeof(*mac->f1_config.setup_resp));
        AssertFatal(mac->f1_config.setup_resp != NULL, "out of memory\n");
        mac->f1_config.setup_resp->num_cells_to_activate = gnb_cu_cfg_update->num_cells_to_activate;
        mac->f1_config.setup_resp->cells_to_activate[0] = gnb_cu_cfg_update->cells_to_activate[0];
      } else {
        LOG_E(GNB_APP, "GNB_CU_CONFIGURATION_UPDATE not matching\n");
      }
    }
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
  /* Free F1AP struct after use */
  free_f1ap_cu_configuration_update(gnb_cu_cfg_update);
  MessageDef *msg_ack_p = NULL;
  if (ret > 0) {
    // generate gNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE
    msg_ack_p = itti_alloc_new_message (TASK_GNB_APP, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE);
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).num_cells_failed_to_be_activated = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).have_criticality = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofTNLAssociations_to_setup =0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofTNLAssociations_failed = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofDedicatedSIDeliveryNeededUEs = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).transaction_id = F1AP_get_next_transaction_identifier(0, 0);
    itti_send_msg_to_task (TASK_DU_F1, INSTANCE_DEFAULT, msg_ack_p);

  }
  else {
    // generate gNB_CU_CONFIGURATION_UPDATE_FAILURE
    msg_ack_p = itti_alloc_new_message (TASK_GNB_APP, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE_FAILURE);
    F1AP_GNB_CU_CONFIGURATION_UPDATE_FAILURE(msg_ack_p).cause = F1AP_CauseRadioNetwork_cell_not_available;

    itti_send_msg_to_task (TASK_DU_F1, INSTANCE_DEFAULT, msg_ack_p);

  }

  return(ret);
}

ngran_node_t get_node_type(void)
{
  GET_PARAMS_LIST(GNBParamList, GNBParams, GNBPARAMS_DESC, GNB_CONFIG_STRING_GNB_LIST, NULL);
  if (GNBParamList.numelt == 0) // We have no valid configuration, let's return a default
    return ngran_gNB;

  // MAC/RLC params
  GET_PARAMS_LIST(MacRLC_ParamList, MacRLC_Params, MACRLCPARAMS_DESC, MACRLC_LIST, NULL);
  for (int j = 0; j < RC.nb_nr_macrlc_inst; j++) {
    const paramdef_t *params = MacRLC_ParamList.paramarray[j];
    const int np = sizeofArray(MacRLC_Params);
    if (strcmp(*gpd(params, np, MACRLC_TRANSPORT_N_PREFERENCE)->strptr, "f1") == 0) {
      return ngran_gNB_DU; // MACRLCs present in config: it must be a DU
    }
  }

  // E1 params
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  GET_PARAMS_LIST(GNBE1ParamList, GNBE1Params, GNBE1PARAMS_DESC, GNB_CONFIG_STRING_E1_PARAMETERS, aprefix);
  if (strcmp(*(GNBParamList.paramarray[0][GNB_TRANSPORT_S_PREFERENCE_IDX].strptr), "f1") == 0) {
    if ( GNBE1ParamList.paramarray == NULL || GNBE1ParamList.numelt == 0 )
      return ngran_gNB_CU;
    else if (strcmp(*(GNBE1ParamList.paramarray[0][GNB_CONFIG_E1_CU_TYPE_IDX].strptr), "cp") == 0)
      return ngran_gNB_CUCP;
    else if (strcmp(*(GNBE1ParamList.paramarray[0][GNB_CONFIG_E1_CU_TYPE_IDX].strptr), "up") == 0)
      return ngran_gNB_CUUP;
    else
      return ngran_gNB_CU;
  } else {
    return ngran_gNB;
  }
}
