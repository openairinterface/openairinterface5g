/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "rrc_gNB_du.h"
#include "rrc_cell_management.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "F1AP_CauseMisc.h"
#include "F1AP_CauseProtocol.h"
#include "F1AP_CauseRadioNetwork.h"
#include "T.h"
#include "asn_codecs.h"
#include "assertions.h"
#include "common/ran_context.h"
#include "common/utils/nr/nr_common.h"
#include "common/utils/T/T.h"
#include "common/utils/alg/foreach.h"
#include "common/utils/ds/seq_arr.h"
#include "constr_TYPE.h"
#include "executables/softmodem-common.h"
#include "f1ap_messages_types.h"
#include "ngap_messages_types.h"
#include "nr_rrc_defs.h"
#include "NR_SIB2.h"
#include "NR_SIB3.h"
#include "NR_SIB4.h"
#include "NR_Q-OffsetRange.h"
#include "NR_asn_constant.h"
#include "openair2/RRC/NR/MESSAGES/asn1_msg.h"
#include "rrc_gNB_mobility.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "lib/f1ap_interface_management.h"
#include "rrc_gNB_NGAP.h"
#include "rrc_gNB_UE_context.h"
#include "rrc_messages_types.h"
#include "s1ap_messages_types.h"
#include "tree.h"
#include "uper_decoder.h"
#include "common/utils/oai_asn1.h"
#include "utils.h"
#include "xer_encoder.h"

static int get_dl_band(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.freqinfo.band : cell_info->fdd.dl_freqinfo.band;
}

static int get_ssb_scs(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.tbw.scs : cell_info->fdd.dl_tbw.scs;
}

NR_SSB_MTC_t *get_ssb_mtc(const NR_MeasurementTimingConfiguration_t *mtc)
{
  // TODO verify which element of the list to pick
  NR_MeasTimingList_t *mtlist = mtc->criticalExtensions.choice.c1->choice.measTimingConf->measTiming;
  NR_SSB_MTC_t *ssb_mtc = calloc(1, sizeof(*ssb_mtc));
  *ssb_mtc = mtlist->list.array[0]->frequencyAndTiming->ssb_MeasurementTimingConfiguration;
  return ssb_mtc;
}

/** @brief Map config q_Hyst (dB: 0,1,2,3,4,5,6,8,10..24) to ASN.1 q-Hyst enum index (0..15).
 *  ASN.1 NR_SIB2 cellReselectionInfoCommon.q-Hyst is encoded with 16 values. */
static long get_q_hyst_asn1(int q_hyst_db)
{
  switch (q_hyst_db) {
    case 0:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB0;
    case 1:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB1;
    case 2:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB2;
    case 3:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB3;
    case 4:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB4;
    case 5:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB5;
    case 6:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB6;
    case 8:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB8;
    case 10:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB10;
    case 12:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB12;
    case 14:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB14;
    case 16:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB16;
    case 18:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB18;
    case 20:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB20;
    case 22:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB22;
    case 24:
      return NR_SIB2__cellReselectionInfoCommon__q_Hyst_dB24;
    default:
      AssertFatal(false, "unexpected q-Hyst value %d (configuration should have been validated)\n", q_hyst_db);
  }
}

/** @brief Build NR_SIB2 from validated SIB2 configuration.
 *  @param cfg    Validated SIB2 configuration from config file
 *  @param ssbmtc SSB-MTC instance to attach to intraFreqCellReselectionInfo.smtc. */
static NR_SIB2_t *get_sib2_from_cfg(const sib2_config_t *cfg, NR_SSB_MTC_t *ssbmtc)
{
  NR_SIB2_t *sib2 = calloc_or_fail(1, sizeof(*sib2));
  typeof(sib2->cellReselectionInfoCommon) *common = &sib2->cellReselectionInfoCommon;
  const cell_reselection_info_common_t *cfg_common = &cfg->cell_reselection_info_common;

  /* q-Hyst: ASN.1 enum (0..15) converted from config dB (0,1,2,3,4,5,6,8,10..24). */
  common->q_Hyst = get_q_hyst_asn1(cfg_common->q_Hyst);

  /* speedStateReselectionPars */
  if (cfg_common->speedStateReselectionPars != NULL) {
    struct NR_SIB2__cellReselectionInfoCommon__speedStateReselectionPars *speed = calloc_or_fail(1, sizeof(*speed));
    NR_MobilityStateParameters_t mobilityStateParameters = {0};
    const sib2_speed_state_reselection_pars_t *pars = cfg_common->speedStateReselectionPars;
    /* t-Evaluation / t-HystNormal: (0..7) */
    mobilityStateParameters.t_Evaluation = pars->t_Evaluation;
    mobilityStateParameters.t_HystNormal = pars->t_HystNormal;
    /* n-CellChangeMedium / n-CellChangeHigh: (1..16) */
    mobilityStateParameters.n_CellChangeMedium = pars->n_CellChangeMedium;
    mobilityStateParameters.n_CellChangeHigh = pars->n_CellChangeHigh;
    speed->mobilityStateParameters = mobilityStateParameters;

    /* q-HystSF: sf-Medium/sf-High (0..3) */
    struct NR_SIB2__cellReselectionInfoCommon__speedStateReselectionPars__q_HystSF qhyst = {0};
    qhyst.sf_Medium = pars->sf_Medium;
    qhyst.sf_High = pars->sf_High;
    speed->q_HystSF = qhyst;

    common->speedStateReselectionPars = speed;
  }

  /* cellReselectionServingFreqInfo */
  typeof(sib2->cellReselectionServingFreqInfo) *serv = &sib2->cellReselectionServingFreqInfo;
  const cell_reselection_serving_freq_info_t *cfg_serv = &cfg->cell_reselection_serving_freq_info;
  /* cellReselectionPriority: CellReselectionPriority (0..7) */
  serv->cellReselectionPriority = cfg_serv->cellReselectionPriority;
  /* threshServingLowP: ReselectionThreshold (0..31) */
  serv->threshServingLowP = cfg_serv->threshServingLowP;
  /* threshServingLowQ: ReselectionThresholdQ (0..31) */
  if (cfg_serv->threshServingLowQ != -1)
    asn1cCallocOne(serv->threshServingLowQ, cfg_serv->threshServingLowQ);
  /* s-NonIntraSearchP/Q: ReselectionThreshold/ReselectionThresholdQ (0..31) */
  if (cfg_serv->s_NonIntraSearchP != -1)
    asn1cCallocOne(serv->s_NonIntraSearchP, cfg_serv->s_NonIntraSearchP);
  if (cfg_serv->s_NonIntraSearchQ != -1)
    asn1cCallocOne(serv->s_NonIntraSearchQ, cfg_serv->s_NonIntraSearchQ);

  /* intraFreqCellReselectionInfo */
  typeof(sib2->intraFreqCellReselectionInfo) *intra = &sib2->intraFreqCellReselectionInfo;
  const intra_freq_cell_reselection_info_t *cfg_intra = &cfg->intra_freq_cell_reselection_info;
  /* q-RxLevMin: Q-RxLevMin (-70..-22) */
  intra->q_RxLevMin = cfg_intra->q_RxLevMin;
  /* q-QualMin: Q-QualMin (-43..-12) */
  if (cfg_intra->q_QualMin != -1)
    asn1cCallocOne(intra->q_QualMin, cfg_intra->q_QualMin);
  /* s-IntraSearchP: ReselectionThreshold (0..31) */
  intra->s_IntraSearchP = cfg_intra->s_IntraSearchP;
  /* s-IntraSearchQ: ReselectionThresholdQ (0..31) */
  if (cfg_intra->s_IntraSearchQ != -1)
    asn1cCallocOne(intra->s_IntraSearchQ, cfg_intra->s_IntraSearchQ);
  /* t-ReselectionNR: (0..7) */
  intra->t_ReselectionNR = cfg_intra->t_ReselectionNR;
  /* deriveSSB-IndexFromCell (bool) */
  intra->deriveSSB_IndexFromCell = cfg->deriveSSB_IndexFromCell;
  /* smtc */
  intra->smtc = ssbmtc;

  return sib2;
}

/** @brief Map internal int enum to ASN.1 Q-OffsetRange value. */
static long get_q_offset_asn1(int q_offset_db)
{
  switch (q_offset_db) {
    case -24:
      return NR_Q_OffsetRange_dB_24;
    case -22:
      return NR_Q_OffsetRange_dB_22;
    case -20:
      return NR_Q_OffsetRange_dB_20;
    case -18:
      return NR_Q_OffsetRange_dB_18;
    case -16:
      return NR_Q_OffsetRange_dB_16;
    case -14:
      return NR_Q_OffsetRange_dB_14;
    case -12:
      return NR_Q_OffsetRange_dB_12;
    case -10:
      return NR_Q_OffsetRange_dB_10;
    case -8:
      return NR_Q_OffsetRange_dB_8;
    case -6:
      return NR_Q_OffsetRange_dB_6;
    case -5:
      return NR_Q_OffsetRange_dB_5;
    case -4:
      return NR_Q_OffsetRange_dB_4;
    case -3:
      return NR_Q_OffsetRange_dB_3;
    case -2:
      return NR_Q_OffsetRange_dB_2;
    case -1:
      return NR_Q_OffsetRange_dB_1;
    case 0:
      return NR_Q_OffsetRange_dB0;
    case 1:
      return NR_Q_OffsetRange_dB1;
    case 2:
      return NR_Q_OffsetRange_dB2;
    case 3:
      return NR_Q_OffsetRange_dB3;
    case 4:
      return NR_Q_OffsetRange_dB4;
    case 5:
      return NR_Q_OffsetRange_dB5;
    case 6:
      return NR_Q_OffsetRange_dB6;
    case 8:
      return NR_Q_OffsetRange_dB8;
    case 10:
      return NR_Q_OffsetRange_dB10;
    case 12:
      return NR_Q_OffsetRange_dB12;
    case 14:
      return NR_Q_OffsetRange_dB14;
    case 16:
      return NR_Q_OffsetRange_dB16;
    case 18:
      return NR_Q_OffsetRange_dB18;
    case 20:
      return NR_Q_OffsetRange_dB20;
    case 22:
      return NR_Q_OffsetRange_dB22;
    case 24:
      return NR_Q_OffsetRange_dB24;
    default:
      AssertFatal(false, "Invalid q_offset_db value %d\n", q_offset_db);
  }
}

/** @brief Build a SIB3 (intra-frequency neighbour cell list) from the configured neighbour cells.
 * Only neighbours whose SSB ARFCN matches @param serving_ssb_arfcn are included.
 * @param neighbours list of configured neighbour cells
 * @param serving_ssb_arfcn SSB ARFCN of the serving cell, used to filter intra-frequency neighbours
 * @returns allocated NR_SIB3_t on success, NULL if no matching neighbours were found */
static NR_SIB3_t *get_sib3_intra_freq_neighbors(const seq_arr_t *neighbours, uint32_t serving_ssb_arfcn)
{
  DevAssert(neighbours);

  NR_SIB3_t *sib3 = calloc_or_fail(1, sizeof(*sib3));
  sib3->intraFreqNeighCellList = calloc_or_fail(1, sizeof(*sib3->intraFreqNeighCellList));

  int intra_count = 0;
  FOR_EACH_SEQ_ARR (const nr_neighbour_cell_t *, neigh, neighbours) {
    if (neigh->absoluteFrequencySSB != serving_ssb_arfcn)
      continue;

    LOG_D(NR_RRC,
          "SIB3: intra-frequency neighbour candidate: cell ID %lu, PCI %d (SSB ARFCN %d)\n",
          neigh->nrcell_id,
          neigh->physicalCellId,
          neigh->absoluteFrequencySSB);

    NR_IntraFreqNeighCellInfo_t *cell = calloc_or_fail(1, sizeof(*cell));
    cell->physCellId = neigh->physicalCellId;

    const nr_neighbour_cell_neighbor_offset_t *off = &neigh->sib3.offset;
    /* q-OffsetCell: NR_Q-OffsetRange (-24..24 dB), 0 dB default */
    cell->q_OffsetCell = get_q_offset_asn1(off->q_OffsetCell);

    /* q-RxLevMinOffsetCell / q-QualMinOffsetCell: (1..8) */
    if (off->q_RxLevMinOffsetCell != -1)
      asn1cCallocOne(cell->q_RxLevMinOffsetCell, off->q_RxLevMinOffsetCell);
    if (off->q_QualMinOffsetCell != -1)
      asn1cCallocOne(cell->q_QualMinOffsetCell, off->q_QualMinOffsetCell);

    /* Add to SIB3 intra-frequency neighbour list */
    if (sib3->intraFreqNeighCellList->list.count >= NR_maxCellIntra) {
      LOG_W(NR_RRC, "SIB3: too many intra-frequency neighbors (max %d), skipping cell %ld\n", NR_maxCellIntra, neigh->nrcell_id);
      asn1cFreeStruc(asn_DEF_NR_IntraFreqNeighCellInfo, cell);
      break;
    }
    asn1cSeqAdd(&sib3->intraFreqNeighCellList->list, cell);
    ++intra_count;
  }

  if (intra_count == 0) {
    LOG_W(NR_RRC,
          "SIB3: no intra-frequency neighbours found for serving SSB ARFCN %u "
          "(configured neighbours %u), not building SIB3\n",
          serving_ssb_arfcn,
          (unsigned)seq_arr_size(neighbours));
    asn1cFreeStruc(asn_DEF_NR_SIB3, sib3);
    return NULL;
  }

  return sib3;
}

/** @brief Build an NR_InterFreqCarrierFreqInfo structure from a nr_inter_freq_cfg_t configuration.
 * @param cfg Configuration to build the carrier from
 * @return Allocated NR_InterFreqCarrierFreqInfo_t on success, NULL if the configuration is invalid */
static NR_InterFreqCarrierFreqInfo_t *build_inter_freq_carrier_from_cfg(const nr_inter_freq_cfg_t *cfg)
{
  DevAssert(cfg);

  const nr_neighbour_cell_sib4_freq_t *f = &cfg->freq_cfg;

  NR_InterFreqCarrierFreqInfo_t *carrier = calloc_or_fail(1, sizeof(*carrier));
  /* dl_CarrierFreq (ARFCN) */
  carrier->dl_CarrierFreq = cfg->arfcn;
  /* ssbSubcarrierSpacing: (15/30/60/120 kHz) */
  carrier->ssbSubcarrierSpacing = cfg->scs;
  carrier->deriveSSB_IndexFromCell = true;
  /* Q-RxLevMin (-70..-22 dBm) */
  carrier->q_RxLevMin = cfg->q_RxLevMin;
  /* T-Reselection (0..7) */
  carrier->t_ReselectionNR = cfg->t_ReselectionNR;
  /* threshX_HighP/LowP: Reselection threshold (0..31) */
  carrier->threshX_HighP = f->threshX_HighP;
  carrier->threshX_LowP = f->threshX_LowP;
  /* threshX-Q: Optional RSRQ thresholds; emit only when both are provided. */
  if (f->threshX_HighQ != -1 && f->threshX_LowQ != -1) {
    carrier->threshX_Q = calloc_or_fail(1, sizeof(*carrier->threshX_Q));
    carrier->threshX_Q->threshX_HighQ = f->threshX_HighQ;
    carrier->threshX_Q->threshX_LowQ = f->threshX_LowQ;
  } else if (f->threshX_HighQ != -1 || f->threshX_LowQ != -1) {
    LOG_W(NR_RRC,
          "SIB4: partial threshX-Q for ARFCN %d (HighQ=%d LowQ=%d), omitting threshX-Q\n",
          cfg->arfcn,
          f->threshX_HighQ,
          f->threshX_LowQ);
  }
  /* CellReselectionPriority (0..7) */
  if (f->cellReselectionPriority != -1)
    asn1cCallocOne(carrier->cellReselectionPriority, f->cellReselectionPriority);
  /* q-OffsetFreq (-24..24 dB), 0 dB default */
  asn1cCallocOne(carrier->q_OffsetFreq, get_q_offset_asn1(f->q_OffsetFreq));
  /* InterFreqNeighCellList */
  carrier->interFreqNeighCellList = calloc_or_fail(1, sizeof(*carrier->interFreqNeighCellList));
  return carrier;
}

/**
 * @brief Build an NR_SIB4 structure populated with inter-frequency neighbour cell entries.
 *
 * Iterates over all configured inter-frequency carriers (excluding the serving cell ARFCN)
 * and, for each carrier, adds the matching neighbour cells from @p neighbour_config.
 * Carriers with no matching neighbours are discarded. If no valid carrier is found,
 * the function returns NULL.
 *
 * @param neighbour_config  Neighbour cell configuration (cells only).
 * @param inter_freqs       gNB-level inter-frequency configuration.
 * @param serving_ssb_arfcn SSB ARFCN of the serving cell, used to skip the serving carrier.
 * @return Allocated NR_SIB4_t on success, NULL if no inter-frequency neighbours are available.
 */
static NR_SIB4_t *get_sib4_inter_freq_neighbors(const neighbour_cell_configuration_t *neighbour_config,
                                                const seq_arr_t *inter_freqs,
                                                uint32_t serving_ssb_arfcn)
{
  DevAssert(neighbour_config);

  NR_SIB4_t *sib4 = calloc_or_fail(1, sizeof(*sib4));

  const seq_arr_t *neigh = &neighbour_config->neighbour_cells;

  FOR_EACH_SEQ_ARR (nr_inter_freq_cfg_t *, cfg, inter_freqs) {
    if (cfg->arfcn == serving_ssb_arfcn)
      continue;

    NR_InterFreqCarrierFreqInfo_t *carrier = build_inter_freq_carrier_from_cfg(cfg);
    if (!carrier)
      continue;

    NR_InterFreqNeighCellList_t *neigh_list = carrier->interFreqNeighCellList;

    FOR_EACH_SEQ_ARR (nr_neighbour_cell_t *, nc, neigh) {
      if (nc->absoluteFrequencySSB != cfg->arfcn || nc->subcarrierSpacing != cfg->scs)
        continue;

      if (neigh_list->list.count >= NR_maxCellInter) {
        LOG_W(NR_RRC,
              "SIB4: too many inter-frequency neighbors for ARFCN %ld (max %d), skipping neighbour\n",
              carrier->dl_CarrierFreq,
              NR_maxCellInter);
        continue;
      }

      const nr_neighbour_cell_neighbor_offset_t *offset = &nc->sib4.offset;
      LOG_I(NR_RRC,
            "SIB4: added inter-frequency neighbour cell: cellId %ld (PCI %d), ARFCN %d, SCS %d\n",
            nc->nrcell_id,
            nc->physicalCellId,
            cfg->arfcn,
            cfg->scs);
      asn1cSequenceAdd(neigh_list->list, NR_InterFreqNeighCellInfo_t, cell);
      cell->physCellId = nc->physicalCellId;

      /* q_OffsetCell: 0 dB is default */
      cell->q_OffsetCell = get_q_offset_asn1(offset->q_OffsetCell);

      /* q_RxLevMinOffsetCell / q_QualMinOffsetCell: range 1..8 */
      if (offset->q_RxLevMinOffsetCell != -1)
        asn1cCallocOne(cell->q_RxLevMinOffsetCell, offset->q_RxLevMinOffsetCell);
      if (offset->q_QualMinOffsetCell != -1)
        asn1cCallocOne(cell->q_QualMinOffsetCell, offset->q_QualMinOffsetCell);
    }

    if (neigh_list->list.count > 0) {
      asn1cSeqAdd(&sib4->interFreqCarrierFreqList.list, carrier);
    } else {
      ASN_STRUCT_FREE(asn_DEF_NR_InterFreqCarrierFreqInfo, carrier);
    }
  }

  if (sib4->interFreqCarrierFreqList.list.count == 0) {
    ASN_STRUCT_FREE(asn_DEF_NR_SIB4, sib4);
    return NULL;
  }

  return sib4;
}

/** @brief Return the frequency of the SS block of the cell for which this message is included,
 * or of other SS blocks within the same carrier. MeasurementTimingConfiguration message is
 * used to convey assistance information for measurement timing between nodes (e.g F1 setup) */
static int ssb_arfcn_mtc(const NR_MeasurementTimingConfiguration_t *mtc)
{
  /* format has been verified when accepting MeasurementTimingConfiguration */
  NR_MeasTimingList_t *mtlist = mtc->criticalExtensions.choice.c1->choice.measTimingConf->measTiming;
  return mtlist->list.array[0]->frequencyAndTiming->carrierFreq;
}

/** @brief Find first cell owned by this DU (assoc_id) and return its SSB ARFCN
 * @note Current assumption is that each DU serves only one cell*/
int get_ssb_arfcn(const struct nr_rrc_cell_container_t *cell)
{
  DevAssert(cell != NULL);
  if (cell->mtc != NULL)
    return ssb_arfcn_mtc(cell->mtc);
  AssertFatal(false, "no mtc found for cell %ld\n", cell->info.cell_id);
  return 0;
}

static bool rrc_gNB_plmn_matches(const gNB_RRC_INST *rrc, const f1ap_served_cell_info_t *info)
{
  const nr_rrc_config_t *conf = &rrc->configuration;
  return conf->num_plmn == 1 // F1 supports only one
         && conf->plmn[0].mcc == info->plmn.mcc && conf->plmn[0].mnc == info->plmn.mnc;
}

static bool extract_sys_info(const f1ap_gnb_du_system_info_t *sys_info, NR_MIB_t **mib, NR_SIB1_t **sib1)
{
  DevAssert(sys_info != NULL);
  DevAssert(mib != NULL);
  DevAssert(sib1 != NULL);

  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_MIB, (void **)mib, sys_info->mib, sys_info->mib_length);
  if (dec_rval.code != RC_OK) {
    LOG_E(NR_RRC, "Failed to decode NR_MIB (%zu bits) of DU\n", dec_rval.consumed);
    ASN_STRUCT_FREE(asn_DEF_NR_MIB, *mib);
    return false;
  }

  if (sys_info->sib1) {
    dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_SIB1, (void **)sib1, sys_info->sib1, sys_info->sib1_length);
    if (dec_rval.code != RC_OK) {
      ASN_STRUCT_FREE(asn_DEF_NR_MIB, *mib);
      ASN_STRUCT_FREE(asn_DEF_NR_SIB1, *sib1);
      LOG_E(NR_RRC, "Failed to decode NR_SIB1 (%zu bits), rejecting DU\n", dec_rval.consumed);
      return false;
    }
  }

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_MIB, *mib);
    xer_fprint(stdout, &asn_DEF_NR_SIB1, *sib1);
  }

  return true;
}

static NR_MeasurementTimingConfiguration_t *extract_mtc(uint8_t *buf, int buf_len)
{
  NR_MeasurementTimingConfiguration_t *mtc = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_MeasurementTimingConfiguration, (void **)&mtc, buf, buf_len);
  if (dec_rval.code != RC_OK) {
    ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, mtc);
    return NULL;
  }
  /* verify that it has the format we need */
  if (mtc->criticalExtensions.present != NR_MeasurementTimingConfiguration__criticalExtensions_PR_c1
      || mtc->criticalExtensions.choice.c1 == NULL
      || mtc->criticalExtensions.choice.c1->present != NR_MeasurementTimingConfiguration__criticalExtensions__c1_PR_measTimingConf
      || mtc->criticalExtensions.choice.c1->choice.measTimingConf == NULL
      || mtc->criticalExtensions.choice.c1->choice.measTimingConf->measTiming == NULL
      || mtc->criticalExtensions.choice.c1->choice.measTimingConf->measTiming->list.count == 0) {
    LOG_E(NR_RRC, "error: measurementTimingConfiguration does not have expected format (at least one measTiming entry\n");
    if (LOG_DEBUGFLAG(DEBUG_ASN1))
      xer_fprint(stdout, &asn_DEF_NR_MeasurementTimingConfiguration, mtc);
    ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, mtc);
    return NULL;
  }
  return mtc;
}

static int nr_cell_id_match(const void *key, const void *element)
{
  const int *key_id = (const int *)key;
  const neighbour_cell_configuration_t *config_element = (const neighbour_cell_configuration_t *)element;

  if (*key_id < config_element->nr_cell_id) {
    return -1;
  } else if (*key_id == config_element->nr_cell_id) {
    return 0;
  }

  return 1;
}

static const neighbour_cell_configuration_t *get_cell_neighbour_list(const gNB_RRC_INST *rrc, const nr_rrc_cell_container_t *cell)
{
  void *base = seq_arr_front(rrc->neighbour_cell_configuration);
  size_t nmemb = seq_arr_size(rrc->neighbour_cell_configuration);
  size_t size = sizeof(neighbour_cell_configuration_t);

  void *it = bsearch((void *)&cell->info.cell_id, base, nmemb, size, nr_cell_id_match);

  return (const neighbour_cell_configuration_t *)it;
}

static bool valid_du_in_neighbour_configs(const seq_arr_t *neighbour_cell_configuration, const f1ap_served_cell_info_t *cell)
{
  // MTC is mandatory, but some DUs don't send it in the F1 Setup Request, so
  // "tolerate" this behavior, despite it being mandatory
  NR_MeasurementTimingConfiguration_t *mtc = extract_mtc(cell->measurement_timing_config, cell->measurement_timing_config_len);
  int ssb_arfcn = ssb_arfcn_mtc(mtc);
  ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, mtc);

  FOR_EACH_SEQ_ARR(const neighbour_cell_configuration_t *, neighbour_config, neighbour_cell_configuration) {
    FOR_EACH_SEQ_ARR(const nr_neighbour_cell_t *, nc, &neighbour_config->neighbour_cells) {
      if (nc->nrcell_id != cell->nr_cellid)
        continue;
      // current cell is in the nc config, check that config matches
      if (nc->physicalCellId != cell->nr_pci) {
        LOG_W(NR_RRC, "Cell %ld in neighbour config: PCI mismatch (%d vs %d)\n", cell->nr_cellid, cell->nr_pci, nc->physicalCellId);
        return false;
      }
      if (cell->tac && nc->tac != *cell->tac) {
        LOG_W(NR_RRC, "Cell %ld in neighbour config: TAC mismatch (%d vs %d)\n", cell->nr_cellid, *cell->tac, nc->tac);
        return false;
      }
      if (ssb_arfcn != nc->absoluteFrequencySSB) {
        LOG_W(NR_RRC,
              "Cell %ld in neighbour config: SSB ARFCN mismatch (%d vs %d)\n",
              cell->nr_cellid,
              ssb_arfcn,
              nc->absoluteFrequencySSB);
        return false;
      }
      // Check neighbour band
      if (get_dl_band(cell) != nc->band) {
        LOG_W(NR_RRC, "Cell %ld in neighbour config: Band mismatch (%d vs %d)\n", cell->nr_cellid, get_dl_band(cell), nc->band);
        return false;
      }
      if (get_ssb_scs(cell) != nc->subcarrierSpacing) {
        LOG_W(NR_RRC,
              "Cell %ld in neighbour config: SCS mismatch (%d vs %d)\n",
              cell->nr_cellid,
              get_ssb_scs(cell),
              nc->subcarrierSpacing);
        return false;
      }
      if (cell->plmn.mcc != nc->plmn.mcc || cell->plmn.mnc != nc->plmn.mnc
          || cell->plmn.mnc_digit_length != nc->plmn.mnc_digit_length) {
        LOG_W(NR_RRC,
              "Cell %ld in neighbour config: PLMN mismatch (%03d.%0*d vs %03d.%0*d)\n",
              cell->nr_cellid,
              cell->plmn.mcc,
              cell->plmn.mnc_digit_length,
              cell->plmn.mnc,
              nc->plmn.mcc,
              nc->plmn.mnc_digit_length,
              nc->plmn.mnc);
        return false;
      }
      LOG_D(NR_RRC, "Cell %ld is neighbor of cell %ld\n", cell->nr_cellid, neighbour_config->nr_cell_id);
    }
  }
  return true;
}

static void cp_f1_served_cell_info_to_cell(nr_rrc_cell_container_t *dst, const f1ap_served_cell_info_t *src)
{
  DevAssert(dst != NULL);
  DevAssert(src != NULL);

  // Copy basic cell information
  dst->info.cell_id = src->nr_cellid;
  dst->info.pci = src->nr_pci;
  dst->info.plmn = src->plmn;
  dst->info.tac = src->tac ? *src->tac : 0;

  // Copy frequency information based on mode
  if (src->mode == F1AP_MODE_TDD) {
    dst->info.mode = NR_MODE_TDD;
    dst->info.tdd.dlul.band = src->tdd.freqinfo.band;
    dst->info.tdd.dlul.arfcn = src->tdd.freqinfo.arfcn;
    dst->info.tdd.dlul.scs = src->tdd.tbw.scs;
    dst->info.tdd.dlul.nrb = src->tdd.tbw.nrb;
  } else {
    dst->info.mode = NR_MODE_FDD;
    /* DL */
    dst->info.fdd.dl.band = src->fdd.dl_freqinfo.band;
    dst->info.fdd.dl.arfcn = src->fdd.dl_freqinfo.arfcn;
    dst->info.fdd.dl.scs = src->fdd.dl_tbw.scs;
    dst->info.fdd.dl.nrb = src->fdd.dl_tbw.nrb;
    /* UL */
    dst->info.fdd.ul.band = src->fdd.ul_freqinfo.band;
    dst->info.fdd.ul.arfcn = src->fdd.ul_freqinfo.arfcn;
    dst->info.fdd.ul.scs = src->fdd.ul_tbw.scs;
    dst->info.fdd.ul.nrb = src->fdd.ul_tbw.nrb;
  }

  // Extract and store MTC
  if (src->measurement_timing_config_len > 0) {
    dst->mtc = extract_mtc(src->measurement_timing_config, src->measurement_timing_config_len);
  }
}

static void add_si_msg(served_cells_to_activate_t *cell, int sib_type, const byte_array_t *enc)
{
  AssertFatal(cell->num_SI < F1AP_MAX_NO_SIB_TYPES,
              "Too many SI messages (%u), max supported is %u\n",
              cell->num_SI,
              F1AP_MAX_NO_SIB_TYPES);
  cell->SI_msg[cell->num_SI].SI_container = copy_byte_array(*enc);
  cell->SI_msg[cell->num_SI].SI_type = sib_type;
  cell->num_SI++;
}

void rrc_gNB_process_f1_setup_req(f1ap_setup_req_t *req, sctp_assoc_t assoc_id)
{
  AssertFatal(assoc_id != 0, "illegal assoc_id == 0: should be -1 (monolithic) or >0 (split)\n");
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  DevAssert(rrc);

  LOG_I(NR_RRC, "Received F1 Setup Request from gNB_DU %lu (%s) on assoc_id %d\n", req->gNB_DU_id, req->gNB_DU_name, assoc_id);
  // pre-fill F1 Setup Failure message
  f1ap_setup_failure_t fail = {.transaction_id = F1AP_get_next_transaction_identifier(0, 0)};

  // check:
  // - PLMN and Cell ID matches
  // - no previous DU with the same ID
  // else reject
  nr_rrc_du_container_t *it = NULL;
  RB_FOREACH(it, rrc_du_tree, &rrc->dus) {
    if (it->gNB_DU_id == req->gNB_DU_id) {
      LOG_E(NR_RRC,
            "gNB-DU ID: existing DU %s on assoc_id %d already has ID %ld, rejecting requesting gNB-DU\n",
            it->gNB_DU_name,
            it->assoc_id,
            it->gNB_DU_id);
      fail.cause = F1AP_CauseMisc_unspecified;
      rrc->mac_rrc.f1_setup_failure(assoc_id, &fail);
      return;
    }
  }
  LOG_I(NR_RRC, "Accepting DU %ld (%s) (RRC version %u.%u.%u)\n", req->gNB_DU_id, req->gNB_DU_name, req->rrc_ver[0], req->rrc_ver[1], req->rrc_ver[2]);

  // DU is accepted, add it to tree and add cell to DU's cell array
  nr_rrc_du_container_t *du = calloc_or_fail(1, sizeof(*du));
  du->assoc_id = assoc_id;
  du->gNB_DU_id = req->gNB_DU_id;
  du->gNB_DU_name = strdup(req->gNB_DU_name ? req->gNB_DU_name : "N/A");
  memcpy(du->rrc_ver, req->rrc_ver, sizeof(du->rrc_ver));

  // Initialize cell array for this DU
  seq_arr_init(&du->cells, sizeof(nr_rrc_cell_container_t *));
  nr_rrc_du_container_t *du_collision = rrc_add_du(rrc, du);
  AssertFatal(du_collision == NULL, "rrc_add_du should succeed for new DU (assoc_id %d)", assoc_id);

  // Build F1 Setup Response
  f1ap_setup_resp_t resp = {.transaction_id = req->transaction_id,
                            .num_cells_to_activate = req->num_cells_available,
                            .cells_to_activate = calloc_or_fail(req->num_cells_available, sizeof(*resp.cells_to_activate))};
  int num = read_version(TO_STRING(NR_RRC_VERSION), &resp.rrc_ver[0], &resp.rrc_ver[1], &resp.rrc_ver[2]);
  AssertFatal(num == 3, "could not read RRC version string %s\n", TO_STRING(NR_RRC_VERSION));
  if (rrc->node_name != NULL)
    resp.gNB_CU_name = strdup(rrc->node_name);

  for (int i = 0; i < req->num_cells_available; i++) {
    f1ap_served_cell_info_t *cell_info = &req->cell[i].info;
    if (!rrc_gNB_plmn_matches(rrc, cell_info)) {
      LOG_E(NR_RRC,
            "PLMN mismatch: CU %03d.%0*d, DU %03d%0*d\n",
            rrc->configuration.plmn[0].mcc,
            rrc->configuration.plmn[0].mnc_digit_length,
            rrc->configuration.plmn[0].mnc,
            cell_info->plmn.mcc,
            cell_info->plmn.mnc_digit_length,
            cell_info->plmn.mnc);
      fail.cause = F1AP_CauseRadioNetwork_plmn_not_served_by_the_gNB_CU;
      rrc->mac_rrc.f1_setup_failure(assoc_id, &fail);
      free_f1ap_setup_response(&resp);
      return;
    }

    if (rrc->neighbour_cell_configuration && !valid_du_in_neighbour_configs(rrc->neighbour_cell_configuration, cell_info)) {
      LOG_E(NR_RRC, "problem with DU %ld in neighbor configuration, rejecting DU\n", req->gNB_DU_id);
      f1ap_setup_failure_t fail = {.cause = F1AP_CauseMisc_unspecified};
      rrc->mac_rrc.f1_setup_failure(assoc_id, &fail);
      free_f1ap_setup_response(&resp);
      return;
    }

    const f1ap_gnb_du_system_info_t *sys_info = req->cell[i].sys_info;
    NR_MIB_t *mib = NULL;
    NR_SIB1_t *sib1 = NULL;
    if (sys_info != NULL && sys_info->mib != NULL && !(sys_info->sib1 == NULL && IS_SA_MODE(get_softmodem_params()))) {
      if (!extract_sys_info(sys_info, &mib, &sib1)) {
        LOG_W(NR_RRC, "rejecting DU ID %ld\n", req->gNB_DU_id);
        fail.cause = F1AP_CauseProtocol_semantic_error;
        rrc->mac_rrc.f1_setup_failure(assoc_id, &fail);
        free_f1ap_setup_response(&resp);
        return;
      }
    }

    /* Create cell and add to global tree */
    nr_rrc_cell_container_t *new = calloc_or_fail(1, sizeof(*new));
    new->assoc_id = assoc_id;
    cp_f1_served_cell_info_to_cell(new, cell_info);
    new->mib = mib;
    new->sib1 = sib1;
    // Add cell to DU's cell array
    nr_rrc_cell_container_t *collision = rrc_add_cell(rrc, new);
    if (collision != NULL) {
      nr_rrc_du_container_t *existing_du = get_du_by_assoc_id(rrc, collision->assoc_id);
      const char *du_name = existing_du ? existing_du->gNB_DU_name : "unknown";
      LOG_E(NR_RRC,
            "Cell ID %lu already exists in DU %s (assoc_id %d), rejecting requesting gNB-DU\n",
            new->info.cell_id,
            du_name,
            collision->assoc_id);
      rrc_free_cell_container(new);
      fail.cause = F1AP_CauseRadioNetwork_cell_not_available;
      rrc->mac_rrc.f1_setup_failure(assoc_id, &fail);
      free_f1ap_setup_response(&resp);
      return;
    }

    nr_rrc_cell_container_t *added = rrc_add_cell_to_du(&du->cells, new);
    AssertFatal(added != NULL, "Failed to add cell %ld to DU %ld\n", new->info.cell_id, du->gNB_DU_id);
    LOG_I(NR_RRC, "DU %ld: Added cell %ld\n", du->gNB_DU_id, new->info.cell_id);

    served_cells_to_activate_t cell = {
        .plmn = cell_info->plmn,
        .nr_cellid = cell_info->nr_cellid,
        .nrpci = cell_info->nr_pci,
        .num_SI = 0,
    };

    // Encode CU SIBs and configure setup response with sysinfo
    seq_arr_t *sibs = rrc->SIBs;
    if (sibs) {
      FOR_EACH_SEQ_ARR (nr_SIBs_t *, sib, sibs) {
        switch (sib->SIB_type) {
          case NR_SIB_2: {
            NR_SSB_MTC_t *ssbmtc = get_ssb_mtc(new->mtc);
            NR_SIB2_t *sib2 = get_sib2_from_cfg(&rrc->sib2_config, ssbmtc);
            byte_array_t enc = do_SIB2_NR(sib2);
            ASN_STRUCT_FREE(asn_DEF_NR_SIB2, sib2);
            if (!enc.buf || enc.len == 0) {
              free_byte_array(enc);
              LOG_E(NR_RRC, "SIB2 encoding failed\n");
              break;
            }
            add_si_msg(&cell, sib->SIB_type, &enc);
            free_byte_array(enc);
            LOG_I(NR_RRC, "DU %ld: added SIB2 to F1 Setup Response (cell %ld)\n", du->gNB_DU_id, new->info.cell_id);
          } break;
          case NR_SIB_3: {
            if (!rrc->neighbour_cell_configuration)
              break;
            const neighbour_cell_configuration_t *neighbour_config = get_cell_neighbour_list(rrc, new);
            if (!neighbour_config)
              break;
            const seq_arr_t *neigh_cells = &neighbour_config->neighbour_cells;

            NR_SIB3_t *sib3 = get_sib3_intra_freq_neighbors(neigh_cells, get_ssb_arfcn(new));
            if (!sib3)
              break;

            byte_array_t enc = do_SIB3_NR(sib3);
            ASN_STRUCT_FREE(asn_DEF_NR_SIB3, sib3);
            if (!enc.buf || enc.len == 0) {
              free_byte_array(enc);
              LOG_E(NR_RRC, "SIB3 encoding failed\n");
              break;
            }
            add_si_msg(&cell, sib->SIB_type, &enc);
            free_byte_array(enc);
            LOG_I(NR_RRC, "DU %ld: added SIB3 to F1 Setup Response (cell %ld)\n", du->gNB_DU_id, new->info.cell_id);
          } break;
          case NR_SIB_4: {
            if (!rrc->neighbour_cell_configuration)
              break;
            const neighbour_cell_configuration_t *neighbour_cfg = get_cell_neighbour_list(rrc, new);
            if (!neighbour_cfg)
              break;

            NR_SIB4_t *sib4 = get_sib4_inter_freq_neighbors(neighbour_cfg, &rrc->inter_freqs, get_ssb_arfcn(new));
            if (!sib4) {
              LOG_W(NR_RRC, "SIB4: could not build from neighbours, skipping\n");
              break;
            }

            byte_array_t enc = do_SIB4_NR(sib4);
            ASN_STRUCT_FREE(asn_DEF_NR_SIB4, sib4);
            if (!enc.buf || enc.len == 0) {
              free_byte_array(enc);
              LOG_E(NR_RRC, "SIB4 encoding failed\n");
              break;
            }
            add_si_msg(&cell, sib->SIB_type, &enc);
            free_byte_array(enc);
            LOG_I(NR_RRC, "DU %ld: added SIB4 to F1 Setup Response (cell %ld)\n", du->gNB_DU_id, new->info.cell_id);
          } break;
          default:
            AssertFatal(false, "SIB%d not handled yet\n", sib->SIB_type);
        }
      }
    }

    resp.cells_to_activate[i] = cell;
  }

  LOG_I(NR_RRC, "DU %ld (%s): sending F1 Setup Response\n", req->gNB_DU_id, req->gNB_DU_name);
  rrc->mac_rrc.f1_setup_response(assoc_id, &resp);
  free_f1ap_setup_response(&resp);

  /* we need to setup one default UE for phy-test and do-ra modes in the MAC */
  if (get_softmodem_params()->phy_test > 0 || get_softmodem_params()->do_ra > 0)
    rrc_add_nsa_user(rrc, NULL, assoc_id);
}

static int invalidate_du_connections(gNB_RRC_INST *rrc, sctp_assoc_t assoc_id)
{
  int count = 0;
  seq_arr_t ue_context_to_remove;
  seq_arr_init(&ue_context_to_remove, sizeof(rrc_gNB_ue_context_t *));
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &rrc->rrc_ue_head) {
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    uint32_t ue_id = UE->rrc_ue_id;
    if (UE->ho_context != NULL)
      LOG_W(NR_RRC, "DU disconnected while handover for UE %d active\n", ue_id);

    // Remove SCells from this DU for this UE
    rrc_remove_ue_scells_from_du(UE, assoc_id);

    f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_id);
    if (ue_data.du_assoc_id != assoc_id)
      continue; /* this UE is on another DU */
    if (IS_SA_MODE(get_softmodem_params())) {
      /* this UE belongs to the DU that disconnected, set du_assoc_id to 0,
       * meaning DU is offline, then trigger release request */
      ue_data.du_assoc_id = 0;
      bool success = cu_update_f1_ue_data(ue_id, &ue_data);
      DevAssert(success);
      ngap_cause_t cause = {.type = NGAP_CAUSE_RADIO_NETWORK, .value = NGAP_CAUSE_RADIO_NETWORK_RADIO_CONNECTION_WITH_UE_LOST};
      rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(0, ue_context_p, cause);
      count++;
    } else {
      seq_arr_push_back(&ue_context_to_remove, &ue_context_p, sizeof(ue_context_p));
    }
  }
  for (int i = 0; i < seq_arr_size(&ue_context_to_remove); ++i) {
    /* we retrieve a pointer (=iterator) to the UE context pointer
     * (ue_context_p), so dereference once */
    rrc_gNB_ue_context_t *p = *(rrc_gNB_ue_context_t **)seq_arr_at(&ue_context_to_remove, i);
    rrc_remove_nsa_user_context(rrc, p);
  }
  seq_arr_free(&ue_context_to_remove, NULL);
  return count;
}

/** @brief Update cell information from F1AP DU configuration update
 * @param[in] rrc RRC instance (needed to get DU and check PCI duplicates)
 * @param[in] old_nci The cell_id used to find the cell in the tree
 * @param[in] new_ci Pointer to the new cell information from F1AP message
 * @return Pointer to the updated cell container, or NULL on failure
 * @note RB tree manipulation: The cell tree is keyed by cell_id. If the cell_id changes
 *       during update, the node must be removed from the tree before updating (using the
 *       old cell_id) and re-inserted after updating (using the new cell_id). This is
 *       necessary because RB trees maintain ordering based on the key value. If cell_id
 *       doesn't change, no tree manipulation is needed. */
static nr_rrc_cell_container_t *update_cell_info(gNB_RRC_INST *rrc, const uint64_t old_nci, const f1ap_served_cell_info_t *new_ci)
{
  DevAssert(rrc != NULL);
  DevAssert(new_ci != NULL);

  struct rrc_cell_tree *cells = &rrc->cells;
  // Find the cell by old cell_id from the update message
  nr_rrc_cell_container_t *cell = get_cell_by_cell_id(cells, old_nci);
  if (cell == NULL) {
    LOG_W(NR_RRC, "no cell with ID %ld found, ignoring gNB-DU configuration update\n", old_nci);
    return NULL;
  }

  // Check if PCI will change - if so, check for duplicate PCI within the same DU
  bool pci_changed = (cell->info.pci != new_ci->nr_pci);
  if (pci_changed) {
    nr_rrc_du_container_t *du = get_du_by_assoc_id(rrc, cell->assoc_id);
    if (du != NULL) {
      // Check if new PCI would conflict with another cell in this DU (excluding current cell)
      nr_rrc_cell_container_t *existing_pci = rrc_get_cell_by_pci_for_du(&du->cells, new_ci->nr_pci);
      if (existing_pci != NULL && existing_pci != cell) {
        LOG_E(NR_RRC,
              "Cannot change PCI from %d to %d: PCI %d already exists in this DU (cell ID %lu)\n",
              cell->info.pci,
              new_ci->nr_pci,
              new_ci->nr_pci,
              existing_pci->info.cell_id);
        return NULL;
      }
    }
  }

  // Check if cell_id will change - if so, need to update tree structure
  bool cell_id_changed = (old_nci != new_ci->nr_cellid);
  if (cell_id_changed) {
    // Check if new cell_id would create a duplicate (excluding current cell)
    nr_rrc_cell_container_t *existing = get_cell_by_cell_id(cells, new_ci->nr_cellid);
    if (existing != NULL && existing != cell) {
      LOG_E(NR_RRC, "Cannot change cell_id from %ld to %ld: new cell_id already exists\n", old_nci, new_ci->nr_cellid);
      return NULL;
    }
    // Remove from tree before updating cell_id
    nr_rrc_cell_container_t *removed = RB_REMOVE(rrc_cell_tree, cells, cell);
    AssertFatal(removed == cell, "Failed to remove cell %ld from tree\n", old_nci);
  }

  // Free old MTC before updating
  if (new_ci->measurement_timing_config_len > 0)
    ASN_STRUCT_FREE(asn_DEF_NR_MeasurementTimingConfiguration, cell->mtc);

  // Update cell information from the new F1AP served cell info
  cp_f1_served_cell_info_to_cell(cell, new_ci);

  // If cell_id changed, re-insert into tree with new cell_id
  if (cell_id_changed) {
    nr_rrc_cell_container_t *existing = RB_INSERT(rrc_cell_tree, cells, cell);
    AssertFatal(existing == NULL, "Failed to insert cell %ld into tree\n", cell->info.cell_id);
    LOG_I(NR_RRC, "Updated cell_id from %ld to %ld\n", old_nci, cell->info.cell_id);
  }

  return cell;
}

void rrc_gNB_process_f1_du_configuration_update(f1ap_gnb_du_configuration_update_t *conf_up, sctp_assoc_t assoc_id)
{
  AssertFatal(assoc_id != 0, "illegal assoc_id == 0: should be -1 (monolithic) or >0 (split)\n");
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  DevAssert(rrc);

  // check:
  // - it is one cell
  // - PLMN and Cell ID matches
  // - no previous DU with the same ID
  // else reject

  nr_rrc_du_container_t *du = get_du_by_assoc_id(rrc, assoc_id);
  AssertError(du != NULL, return, "no DU found for assoc_id %d\n", assoc_id);
  // Validate each cell to add for uniqueness (cell_id globally; PCI within this DU)
  for (uint16_t i = 0; i < conf_up->num_cells_to_add; i++) {
    f1ap_served_cell_info_t *cell_info = &conf_up->cell_to_add[i].info;
    // Check for duplicate cell_id globally
    nr_rrc_cell_container_t *existing = get_cell_by_cell_id(&rrc->cells, cell_info->nr_cellid);
    if (existing != NULL) {
      nr_rrc_du_container_t *existing_du = get_du_by_assoc_id(rrc, existing->assoc_id);
      const char *du_name = existing_du ? existing_du->gNB_DU_name : "unknown";
      LOG_E(NR_RRC,
            "Cell ID %lu already exists in DU %s (assoc_id %d), rejecting cell addition\n",
            cell_info->nr_cellid,
            du_name,
            existing->assoc_id);
      /** @todo send failure message */
      return;
    }
    // Check for duplicate PCI within this DU
    nr_rrc_cell_container_t *existing_pci = rrc_get_cell_by_pci_for_du(&du->cells, cell_info->nr_pci);
    if (existing_pci != NULL) {
      LOG_E(NR_RRC,
            "PCI %d already exists in this DU (cell ID %lu), rejecting cell addition (cell ID %lu)\n",
            cell_info->nr_pci,
            existing_pci->info.cell_id,
            cell_info->nr_cellid);
      /** @todo send failure message */
      return;
    }
  }

  if (conf_up->num_cells_to_modify > 0) {
    // here the old nrcgi is used to find the cell information, if it exist then we modify consequently otherwise we fail
    AssertFatal(conf_up->num_cells_to_modify == 1, "cannot handle more than one cell!\n");
    const uint64_t old_nci = conf_up->cell_to_modify[0].old_nr_cellid;
    const f1ap_served_cell_info_t *new_ci = &conf_up->cell_to_modify[0].info;

    // verify the new plmn of the cell
    if (!rrc_gNB_plmn_matches(rrc, new_ci)) {
      LOG_W(NR_RRC, "PLMN does not match, ignoring gNB-DU configuration update\n");
      return;
    }


    nr_rrc_cell_container_t *cell = get_cell_by_cell_id(&rrc->cells, old_nci);
    if (cell->assoc_id != du->assoc_id) {
      LOG_W(NR_RRC,
            "cell %ld belongs to different DU (assoc_id %d vs %d), ignoring gNB-DU configuration update\n",
            old_nci,
            cell->assoc_id,
            du->assoc_id);
      return;
    }
    cell = update_cell_info(rrc, old_nci, new_ci);
    if (cell == NULL) {
      LOG_W(NR_RRC, "Failed to update cell %ld, ignoring gNB-DU configuration update\n", old_nci);
      return;
    }

    const f1ap_gnb_du_system_info_t *sys_info = conf_up->cell_to_modify[0].sys_info;

    if (sys_info != NULL && sys_info->mib != NULL && !(sys_info->sib1 == NULL && IS_SA_MODE(get_softmodem_params()))) {
      // MIB is mandatory, so will be overwritten. SIB1 is optional, so will
      // only be overwritten if present in sys_info
      ASN_STRUCT_FREE(asn_DEF_NR_MIB, cell->mib);
      if (sys_info->sib1 != NULL) {
        ASN_STRUCT_FREE(asn_DEF_NR_SIB1, cell->sib1);
        cell->sib1 = NULL;
      }

      NR_MIB_t *mib = NULL;
      if (!extract_sys_info(sys_info, &mib, &cell->sib1)) {
        LOG_W(NR_RRC, "cannot update sys_info for DU %ld\n", du->gNB_DU_id);
      } else {
        DevAssert(mib != NULL);
        cell->mib = mib;
        LOG_I(NR_RRC, "update system information of DU %ld\n", du->gNB_DU_id);
      }
    }
  }

  if (conf_up->num_cells_to_delete > 0) {
    // delete the cell and send cell to desactive IE in the response.
    LOG_W(NR_RRC, "du_configuration_update->cells_to_delete_list is not supported yet");
  }

  for (int i = 0; i < conf_up->num_status; ++i) {
    const f1ap_cell_status_t *cs = &conf_up->status[i];
    const char *status = cs->service_state == F1AP_STATE_IN_SERVICE ? "in service" : "out of service";
    const plmn_id_t *p = &cs->plmn;
    LOG_I(NR_RRC, "cell PLMN %03d.%0*d Cell ID %ld is %s\n", p->mcc, p->mnc_digit_length, p->mnc, cs->nr_cellid, status);
  }

  /* Send DU Configuration Acknowledgement */
  f1ap_gnb_du_configuration_update_acknowledge_t ack = {.transaction_id = conf_up->transaction_id};
  rrc->mac_rrc.gnb_du_configuration_update_acknowledge(assoc_id, &ack);
}

void rrc_CU_process_f1_lost_connection(gNB_RRC_INST *rrc, f1ap_lost_connection_t *lc, sctp_assoc_t assoc_id)
{
  AssertFatal(assoc_id != 0, "illegal assoc_id == 0: should be -1 (monolithic) or >0 (split)\n");
  (void) lc; // unused for the moment

  nr_rrc_du_container_t e = {.assoc_id = assoc_id};
  nr_rrc_du_container_t *du = RB_FIND(rrc_du_tree, &rrc->dus, &e);
  if (du == NULL) {
    LOG_W(NR_RRC, "no DU connected or not found for assoc_id %d: F1 Setup Failed?\n", assoc_id);
    return;
  }

  // Clean up DU and associated cell resources
  rrc_cleanup_du(rrc, du);

  int num = invalidate_du_connections(rrc, assoc_id);
  if (num > 0) {
    LOG_I(NR_RRC, "%d UEs lost through DU disconnect\n", num);
  }
}

nr_rrc_du_container_t *get_du_for_ue(gNB_RRC_INST *rrc, uint32_t ue_id)
{
  if (!cu_exists_f1_ue_data(ue_id))
    return NULL;
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_id);
  return get_du_by_assoc_id(rrc, ue_data.du_assoc_id);
}

/** @brief Dump DU and cell information to file
 * @param[in] rrc RRC instance
 * @param[in] f File handle to write to
 * @note Complexity: O(d + N_CELL) = O(N_CELL) where d = number of DUs, N_CELL = total number of cells across all DUs
 * @note Called periodically (every 1 second) for stats logging */
void dump_du_info(const gNB_RRC_INST *rrc, FILE *f)
{
  fprintf(f, "%ld connected DUs \n", rrc->num_dus);
  int du_idx = 1;
  nr_rrc_du_container_t *du = NULL;
  /* cast is necessary to eliminate warning "discards 'const' qualifier" */
  RB_FOREACH(du, rrc_du_tree, &((gNB_RRC_INST *)rrc)->dus) {
    fprintf(f, "[%d] DU ID %ld (%s) ", du_idx++, du->gNB_DU_id, du->gNB_DU_name);
    if (du->assoc_id == -1) {
      fprintf(f, "integrated DU-CU");
    } else {
      fprintf(f, "assoc_id %d", du->assoc_id);
    }

    /* Print all cells for this DU */
    int cell_count = seq_arr_size(&du->cells);

    if (cell_count == 0) {
      fprintf(f, ": no cells\n");
    } else {
      fprintf(f, ": %d cell%s\n", cell_count, cell_count > 1 ? "s" : "");
      int cell_idx = 1;
      FOR_EACH_SEQ_ARR (nr_rrc_cell_container_t **, cell_ptr, &du->cells) {
        nr_rrc_cell_container_t *cell = *cell_ptr;
        const char *mode = (cell->info.mode == NR_MODE_TDD) ? "TDD" : "FDD";
        fprintf(f,
                "  [%d] nrCellID %lu, PCI %d, Mode %s, SSB ARFCN %d\n",
                cell_idx++,
                cell->info.cell_id,
                cell->info.pci,
                mode,
                ssb_arfcn_mtc(cell->mtc));
        if (cell->info.mode == NR_MODE_TDD) {
          const nr_rrc_freq_info_t *tdd = &cell->info.tdd.dlul;
          fprintf(f, "      TDD: band %d ARFCN %d SCS %d (kHz) PRB %d\n", tdd->band, tdd->arfcn, 15 * (1 << tdd->scs), tdd->nrb);
        } else {
          const nr_rrc_freq_info_t *dl = &cell->info.fdd.dl;
          const nr_rrc_freq_info_t *ul = &cell->info.fdd.ul;
          fprintf(f, "      DL:  band %d ARFCN %d SCS %d (kHz) PRB %d\n", dl->band, dl->arfcn, 15 * (1 << dl->scs), dl->nrb);
          fprintf(f, "      UL:  band %d ARFCN %d SCS %d (kHz) PRB %d\n", ul->band, ul->arfcn, 15 * (1 << ul->scs), ul->nrb);
        }
      }
    }
  }
}

nr_rrc_du_container_t *find_target_du(gNB_RRC_INST *rrc, sctp_assoc_t source_assoc_id)
{
  nr_rrc_du_container_t *target_du = NULL;
  nr_rrc_du_container_t *it = NULL;
  bool next_du = false;
  RB_FOREACH (it, rrc_du_tree, &rrc->dus) {
    if (next_du == false && source_assoc_id != it->assoc_id) {
      continue;
    } else if (source_assoc_id == it->assoc_id) {
      next_du = true;
    } else {
      target_du = it;
      break;
    }
  }
  if (target_du == NULL) {
    RB_FOREACH (it, rrc_du_tree, &rrc->dus) {
      if (source_assoc_id == it->assoc_id) {
        continue;
      } else {
        target_du = it;
        break;
      }
    }
  }
  return target_du;
}

void trigger_f1_reset(gNB_RRC_INST *rrc, sctp_assoc_t du_assoc_id)
{
  f1ap_reset_t reset = {
    .transaction_id = F1AP_get_next_transaction_identifier(0, 0),
    .cause = F1AP_CAUSE_TRANSPORT,
    .cause_value = 1, // F1AP_CauseTransport_transport_resource_unavailable
    .reset_type = F1AP_RESET_ALL, // DU does not support partial reset yet
  };
  rrc->mac_rrc.f1_reset(du_assoc_id, &reset);
}

/** @brief Distribute paging to DUs serving cells matching any of the requested TAIs.
 *  Loops DUs once, collects matching cells per DU, sends one message per DU.
 *  Each cell is added at most once per DU (cells are unique by cell_id within a DU).
 *  Per 3GPP TS 23.003, TAI is uniquely identified by (PLMN, TAC) combination.
 *  @param rrc RRC instance
 *  @param tais Array of TAIs (PLMN + TAC) to match against
 *  @param n_tai Number of TAIs in the array (must be > 0)
 *  @param msg Pre-filled F1AP paging message */
void rrc_send_paging_to_dus(gNB_RRC_INST *rrc, const nr_tai_t tais[], uint8_t n_tai, f1ap_paging_t *msg)
{
  DevAssert(msg && !msg->cells);
  DevAssert(n_tai > 0);

  nr_rrc_du_container_t *du = NULL;
  // Loop over DUs once, for each DU, collects matching cells
  RB_FOREACH (du, rrc_du_tree, &rrc->dus) {
    RETURN_IF_INVALID_ASSOC_ID(du->assoc_id);

    // Stack-allocated cell list for this DU iteration
    f1ap_paging_cell_item_t cells[F1AP_MAX_NB_CELLS] = {0};
    msg->cells = cells;
    msg->n_cells = 0;

    // For each cell in this DU, check if it matches any of the matching TAIs
    FOR_EACH_SEQ_ARR (nr_rrc_cell_container_t **, cell, &du->cells) {
      if (msg->n_cells >= F1AP_MAX_NB_CELLS) {
        LOG_W(F1AP,
              "[gNB] Paging: DU assoc_id %d has more than %d matching cells, skipping next cells\n",
              du->assoc_id,
              F1AP_MAX_NB_CELLS);
        break;
      }

      const nr_rrc_cell_info_t *info = &(*cell)->info;
      const plmn_id_t *cell_plmn = &info->plmn;
      const uint16_t cell_tac = info->tac;
      // Check if this cell's TAI (PLMN + TAC) matches any of the matching TAIs
      for (uint8_t j = 0; j < n_tai; j++) {
        const nr_tai_t *req_tai = &tais[j];
        const plmn_id_t *req_plmn = &req_tai->plmn;
        const uint16_t req_tac = req_tai->tac;
        if (cell_plmn->mcc == req_plmn->mcc && cell_plmn->mnc == req_plmn->mnc
            && cell_plmn->mnc_digit_length == req_plmn->mnc_digit_length && cell_tac == req_tac) {
          f1ap_paging_cell_item_t *paging_cell = &msg->cells[msg->n_cells++];
          paging_cell->plmn = *cell_plmn;
          paging_cell->nr_cellid = info->cell_id;
          break; // Found match, no need to check other TAIs
        }
      }
    }

    // Skip DUs with no matching cells
    if (msg->n_cells == 0)
      continue;
    DevAssert(msg->n_cells <= F1AP_MAX_NB_CELLS);
    LOG_I(F1AP,
          "[gNB] Paging transfer to DU assoc_id %d: ue_identity_index_value=%u, paging_identity=%lu, n_cells=%u\n",
          du->assoc_id,
          msg->ue_identity_index_value,
          msg->identity.cn_ue_paging_identity,
          msg->n_cells);
    // Send one message to the DU with all matching cells
    rrc->mac_rrc.paging_transfer(du->assoc_id, msg);
  }

  msg->cells = NULL; // Stack allocation, no need to free
}

void rrc_send_trp_information_request_to_dus(gNB_RRC_INST *rrc, f1ap_trp_information_req_t *msg)
{
  nr_rrc_du_container_t *du = NULL;
  RB_FOREACH (du, rrc_du_tree, &rrc->dus) {
    rrc->mac_rrc.trp_information_request(du->assoc_id, msg);
  }
}

void rrc_send_positioning_measurement_request_to_dus(gNB_RRC_INST *rrc, f1ap_positioning_measurement_req_t *msg)
{
  nr_rrc_du_container_t *du = NULL;
  RB_FOREACH (du, rrc_du_tree, &rrc->dus) {
    rrc->mac_rrc.positioning_measurement_request(du->assoc_id, msg);
  }
}
