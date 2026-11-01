/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief primitives used by gNB for BCH, RACH, ULSCH, DLSCH scheduling
 */

#include "nfapi_nr_interface_scf.h"
#include <softmodem-common.h>
#include "assertions.h"

#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "common/utils/bits.h"
#include "common/utils/LOG/log.h"
#include "UTIL/OPT/opt.h"

#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "intertask_interface.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "F1AP_CauseRadioNetwork.h"

#include "T.h"
#include "uper_encoder.h"

#include "SIMULATION/TOOLS/sim.h" // for taus

#define ENABLE_MAC_PAYLOAD_DEBUG

#include "common/ran_context.h"
#include "nfapi/oai_integration/vendor_ext.h"

#include "common/utils/alg/find.h"

// 3GPP TS 38.331 Section 12 Table 12.1-1: UE performance requirements for RRC procedures for UEs
#define NR_RRC_RECONFIGURATION_DELAY_MS 10
#define NR_RRC_BWP_SWITCHING_DELAY_MS   6

// #define DEBUG_DCI
//  CQI TABLES (10 times the value in 214 to adequately compare with R)
//  Table 1 (38.214 5.2.2.1-2)
static const uint16_t cqi_table1[16][2] = {{0, 0},
                                           {2, 780},
                                           {2, 1200},
                                           {2, 1930},
                                           {2, 3080},
                                           {2, 4490},
                                           {2, 6020},
                                           {4, 3780},
                                           {4, 4900},
                                           {4, 6160},
                                           {6, 4660},
                                           {6, 5670},
                                           {6, 6660},
                                           {6, 7720},
                                           {6, 8730},
                                           {6, 9480}};

// Table 2 (38.214 5.2.2.1-3)
static const uint16_t cqi_table2[16][2] = {{0, 0},
                                           {2, 780},
                                           {2, 1930},
                                           {2, 4490},
                                           {4, 3780},
                                           {4, 4900},
                                           {4, 6160},
                                           {6, 4660},
                                           {6, 5670},
                                           {6, 6660},
                                           {6, 7720},
                                           {6, 8730},
                                           {8, 7110},
                                           {8, 7970},
                                           {8, 8850},
                                           {8, 9480}};

// Table 2 (38.214 5.2.2.1-4)
static const uint16_t cqi_table3[16][2] = {{0, 0},
                                           {2, 300},
                                           {2, 500},
                                           {2, 780},
                                           {2, 1200},
                                           {2, 1930},
                                           {2, 3080},
                                           {2, 4490},
                                           {2, 6020},
                                           {4, 3780},
                                           {4, 4900},
                                           {4, 6160},
                                           {6, 4660},
                                           {6, 5670},
                                           {6, 6660},
                                           {6, 7720}};

fsn_t get_fb_frame_slot(int frame, int slot, int K, int n_slots_frame, int NTN_gNB_Koffset)
{
  // K is the feedback time in slot
  fsn_t fb = {.f = (frame + ((slot + K + NTN_gNB_Koffset) / n_slots_frame)) % MAX_FRAME_NUMBER,
              .s = (slot + K + NTN_gNB_Koffset) % n_slots_frame};
  return fb;
}

int get_ssbidx_from_beam(const nr_cell_sched_t *cell, int beam_idx)
{
  for (int i = 0; i < MAX_NUM_OF_SSB; i++)
    if (beam_idx == cell->beam_index_list[i])
      return i;
  AssertFatal(false, "beam_idx %d not found\n", beam_idx);
  return 0;
}

uint8_t get_dl_nrOfLayers(const NR_UE_sched_ctrl_t *sched_ctrl, const nr_dci_format_t dci_format)
{
  // TODO check this but it should be enough for now
  // if there is not csi report activated RI is 0 from initialization
  if(dci_format == NR_DL_DCI_FORMAT_1_0)
    return 1;
  else
    return sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.ri + 1;
}

// Table 5.2.2.2.1-3 and Table 5.2.2.2.1-4 in 38.214
void get_k1_k2_indices(const int layers, const int N1, const int N2, const int i13, int *k1, int *k2)
{
  AssertFatal(layers > 0 && layers < 5, "Number of layers %d not supported\n", layers);
  *k1 = 0;
  *k2 = 0;
  if (layers == 2) {
    if (N2 == 1)
      *k1 = i13;
    else if (N1 == N2) {
      *k1 = i13 & 1;
      *k2 = i13 >> 1;
    }
    else {
      *k1 = (i13 & 1) + (i13 == 3);
      *k2 = (i13 == 2);
    }
  }
  if (layers == 3 || layers == 4) {
    if (N2 == 1)
      *k1 = i13 + 1;
    else if (N1 == 2 && N2 == 2) {
      *k1 = !(i13 & 1);
      *k2 = (i13 > 0);
    }
    else {
      if (i13 == 0)
        *k1 = 1;
      if (i13 == 1)
        *k2 = 1;
      if (i13 == 2) {
        *k1 = 1;
        *k2 = 1;
      }
      if (i13 == 3)
        *k1 = 2;
    }
  }
}

uint16_t get_pm_index(const nr_cell_sched_t *cell,
                      const NR_UE_info_t *UE,
                      nr_dci_format_t dci_format,
                      int layers,
                      int xp_pdsch_antenna_ports)
{
  if (dci_format == NR_DL_DCI_FORMAT_1_0 || cell->identity_pm || xp_pdsch_antenna_ports == 1)
    return 0; //identity matrix (basic 5G configuration handled by PMI report is with XP antennas)
  const NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  const int report_id = sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.csi_report_id;
  const nr_csi_report_t *csi_report = &UE->csi_report_template[report_id];
  const int N1 = csi_report->N1;
  const int N2 = csi_report->N2;
  const int antenna_ports = (N1 * N2) << 1;
  if (antenna_ports < 2)
    return 0; // single antenna port

  int x1 = sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.pmi_x1;
  const int x2 = sched_ctrl->CSI_report.cri_ri_li_pmi_cqi_report.pmi_x2;
  LOG_D(NR_MAC,"PMI report: x1 %d x2 %d layers: %d\n", x1, x2, layers);

  int prev_layers_size = 0;
  for (int i = 1; i < layers; i++)
    prev_layers_size += cell->precoding_matrix_size[i - 1];

  // need to return PM index to matrix initialized in init_DL_MIMO_codebook
  // index 0 is for identity matrix
  // order of matrices depends on layers to be transmitted
  // elements from 1 to n for 1 layer
  // elements from n+1 to m for 2 layers etc.
  if (antenna_ports == 2)
    return 1 + prev_layers_size + x2;  // 0 for identity matrix
  else {
    int idx = layers - 1;
    // the order of i1x in X1 report needs to be verified
    // the standard is not very clear (Table 6.3.1.1.2-7 in 38.212)
    // it says: PMI wideband information fields X1 , from left to right
    int bitlen = csi_report->csi_meas_bitlen.pmi_i13_bitlen[idx];
    if (layers == 1 && bitlen != 0) {
      LOG_E(NR_MAC, "Invalid i13 bit length %d for single layer! It should be 0\n", bitlen);
      return 0;
    }
    const int i13 = x1 & ((1 << bitlen) - 1);
    x1 >>= bitlen;
    bitlen = csi_report->csi_meas_bitlen.pmi_i12_bitlen[idx];
    const int i12 = x1 & ((1 << bitlen) - 1);
    x1 >>= bitlen;
    bitlen = csi_report->csi_meas_bitlen.pmi_i11_bitlen[idx];
    const int i11 = x1 & ((1 << bitlen) - 1);
    const int i2 = x2;
    int k1, k2;
    get_k1_k2_indices(layers, N1, N2, i13, &k1, &k2); // get indices k1 and k2 for PHY matrix (not actual k1 and k2 values)
    const int O2 = N2 == 1 ? 1 : 4;
    int O1 = 4; //Horizontal beam oversampling = 4 for more than 2 antenna ports
    int max_i2 = 0;
    int lay_index = 0;
    max_i2 = layers == 1 ? 4 : 2;
    int K1, K2;
    get_K1_K2(N1, N2, &K1, &K2, layers);
    // computing precoding matrix index according to rule set in allocation function init_codebook_gNB
    lay_index = i2 + (i11 * max_i2) + (i12 * max_i2 * N1 * O1) + (k1 * max_i2 * N1 * O1 * N2 * O2) + (k2 * max_i2 * N1 * O1 * N2 * O2 * K1);
    return 1 + prev_layers_size + lay_index;
  }
}

// look-up table for AMC. Based on BLER vs SNR curves from the nr_dlsim simulation
// command line: nr_dlsim -n 10000 -m 0 -R 25 -b 25 -e MCS -s START_SNR -t 99.99
// SNR Thresholds for MCS=[0,...,28]; START_SNR=chosen values with a resolution of 0.2dB to maintain a BLER of 10^-3
static const int SINRx10_MCS_mapping[29] = {
  -10,  -4,   6,  16,  24,  34,  42,  50,  56,  62, //  0..9
   86,  92,  98, 104, 112, 118, 124, 140, 146, 154, // 10..19
  162, 170, 178, 186, 194, 202, 212, 220, 245       // 20..28
};

int get_mcs_from_SINRx10(int mcs_table, int SINRx10, int Nl)
{
  if (mcs_table != 0) {
    LOG_E(MAC, "mcs_table = %d, but get_mcs_from_SINRx10() only supports MCS table 0 (TS 38.214 - Table 5.1.3.1-1)\n", mcs_table);
    return 28;
  }

  int MIMO_SNRx10 = 0;
  if (Nl == 2)
    MIMO_SNRx10 = 40;
  else if (Nl == 4)
    MIMO_SNRx10 = 70;

  for (int i = 28; i >= 0; i--) {
    if (SINRx10 >= SINRx10_MCS_mapping[i] + MIMO_SNRx10)
      return i;
  }

  LOG_W(MAC, "SINR (%d.%d dB) too low, no MCS possible to achieve BLER of 10^-3\n", SINRx10 / 10, SINRx10 % 10);

  return 0;
}

uint8_t get_mcs_from_cqi(int mcs_table, int cqi_table, int cqi_idx)
{
  if (cqi_idx <= 0) {
    LOG_E(NR_MAC, "invalid cqi_idx %d, default to MCS 9\n", cqi_idx);
    return 9;
  }

  if (mcs_table != cqi_table) {
    LOG_E(NR_MAC, "indices of CQI (%d) and MCS (%d) tables don't correspond yet\n", cqi_table, mcs_table);
    return 9;
  }

  uint16_t target_coderate, target_qm;
  switch (cqi_table) {
    case 0:
      target_qm = cqi_table1[cqi_idx][0];
      target_coderate = cqi_table1[cqi_idx][1];
      break;
    case 1:
      target_qm = cqi_table2[cqi_idx][0];
      target_coderate = cqi_table2[cqi_idx][1];
      break;
    case 2:
      target_qm = cqi_table3[cqi_idx][0];
      target_coderate = cqi_table3[cqi_idx][1];
      break;
    default:
      AssertFatal(1==0,"Invalid cqi table index %d\n",cqi_table);
  }
  const int max_mcs = mcs_table == 1 ? 27 : 28;
  for (int i = 0; i <= max_mcs; i++) {
    const int R = nr_get_code_rate_dl(i, mcs_table);
    const int Qm = nr_get_Qm_dl(i, mcs_table);
    if (Qm == target_qm && target_coderate <= R)
      return i;
  }

  LOG_E(NR_MAC, "could not find maximum MCS from cqi_idx %d, default to 9\n", cqi_idx);
  return 9;
}

NR_pdsch_dmrs_t get_dl_dmrs_params(const NR_ServingCellConfigCommon_t *scc,
                                   const NR_UE_DL_BWP_t *dl_bwp,
                                   const NR_tda_info_t *tda_info,
                                   const int Layers)
{
  NR_pdsch_dmrs_t dmrs = {0};
  int frontloaded_symb = 1; // default value
  nr_dci_format_t dci_format = dl_bwp ? dl_bwp->dci_format : NR_DL_DCI_FORMAT_1_0;
  if (dci_format == NR_DL_DCI_FORMAT_1_0) {
    dmrs.numDmrsCdmGrpsNoData = tda_info->nrOfSymbols <= 2 ? 1 : 2;
    dmrs.dmrs_ports_id = 0;
  } else {
    //TODO first basic implementation of dmrs port selection
    //     only vaild for a single codeword
    //     for now it assumes a selection of Nl consecutive dmrs ports
    //     and a single front loaded symbol
    //     dmrs_ports_id is the index of Tables 7.3.1.2.2-1/2/3/4
    //     number of front loaded symbols need to be consistent with maxLength
    //     when a more complete implementation is done

    switch (Layers) {
      case 1:
        dmrs.dmrs_ports_id = 0;
        dmrs.numDmrsCdmGrpsNoData = 1;
        frontloaded_symb = 1;
        break;
      case 2:
        dmrs.dmrs_ports_id = 2;
        dmrs.numDmrsCdmGrpsNoData = 1;
        frontloaded_symb = 1;
        break;
      case 3:
        dmrs.dmrs_ports_id = 9;
        dmrs.numDmrsCdmGrpsNoData = 2;
        frontloaded_symb = 1;
        break;
      case 4:
        dmrs.dmrs_ports_id = 10;
        dmrs.numDmrsCdmGrpsNoData = 2;
        frontloaded_symb = 1;
        break;
      default:
        AssertFatal(1==0,"Number of layers %d\n not supported or not valid\n",Layers);
    }
  }

  dmrs.n_scid = 0;
  long *scramblingID = NULL;
  NR_PDSCH_Config_t *pdsch_Config = dl_bwp ? dl_bwp->pdsch_Config : NULL;
  if (pdsch_Config) {
    NR_DMRS_DownlinkConfig_t *dmrs_Config = tda_info->mapping_type == typeB ?
                                            pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup :
                                            pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup;
    dmrs.phaseTrackingRS = dmrs_Config->phaseTrackingRS ? dmrs_Config->phaseTrackingRS->choice.setup : NULL;
    dmrs.dmrsConfigType = dmrs_Config->dmrs_Type != NULL;
    scramblingID = dmrs.n_scid ? dmrs_Config->scramblingID1 : dmrs_Config->scramblingID0;
  } else {
    dmrs.dmrsConfigType = NFAPI_NR_DMRS_TYPE1;
    dmrs.phaseTrackingRS = NULL;
  }
  dmrs.scrambling_id = scramblingID ? *scramblingID : *scc->physCellId;
  dmrs.N_PRB_DMRS = dmrs.numDmrsCdmGrpsNoData * (dmrs.dmrsConfigType == NFAPI_NR_DMRS_TYPE1 ? 6 : 4);
  dmrs.dl_dmrs_symb_pos = fill_dmrs_mask(pdsch_Config, dci_format, scc->dmrs_TypeA_Position, tda_info->nrOfSymbols, tda_info->startSymbolIndex, tda_info->mapping_type, frontloaded_symb);
  dmrs.N_DMRS_SLOT = get_num_dmrs(dmrs.dl_dmrs_symb_pos);
  LOG_D(NR_MAC,"Filling dmrs info, ps->N_PRB_DMRS %d, ps->dl_dmrs_symb_pos %x, ps->N_DMRS_SLOT %d\n",dmrs.N_PRB_DMRS,dmrs.dl_dmrs_symb_pos,dmrs.N_DMRS_SLOT);
  return dmrs;
}

NR_ControlResourceSet_t *get_coreset(nr_cell_sched_t *cell,
                                     NR_ServingCellConfigCommon_t *scc,
                                     NR_BWP_DownlinkDedicated_t *bwp_dedicated,
                                     NR_ControlResourceSetId_t coreset_id)
{
  if (coreset_id == 0) {
    return &cell->sched_ctrlSIB1->coreset; // this is coreset 0
  }
  if (bwp_dedicated) {
    const int n = bwp_dedicated->pdcch_Config->choice.setup->controlResourceSetToAddModList->list.count;
    for (int i = 0; i < n; i++) {
      NR_ControlResourceSet_t *coreset = bwp_dedicated->pdcch_Config->choice.setup->controlResourceSetToAddModList->list.array[i];
      if (coreset->controlResourceSetId == coreset_id) {
        return coreset;
      }
    }
    AssertFatal(false, "Couldn't find CORESET with id %ld in BWP_DownlinkDedicated\n", coreset_id);
  }
  if (scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->commonControlResourceSet) {
    NR_ControlResourceSet_t *coreset =
        scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->commonControlResourceSet;
    if (coreset->controlResourceSetId == coreset_id) {
      return coreset;
    }
    AssertFatal(false, "Couldn't find CORESET with id %ld in commonControlResourceSet\n", coreset_id);
  }
  AssertFatal(false, "Couldn't find coreset with id %ld\n", coreset_id);
}

static NR_SearchSpace_t *get_searchspace(NR_ServingCellConfigCommon_t *scc,
                                         NR_BWP_DownlinkDedicated_t *bwp_dedicated,
                                         NR_SearchSpace__searchSpaceType_PR target_ss)
{
  if (bwp_dedicated) {
    const int n = bwp_dedicated->pdcch_Config->choice.setup->searchSpacesToAddModList->list.count;
    for (int i = 0; i < n; i++) {
      NR_SearchSpace_t *ss = bwp_dedicated->pdcch_Config->choice.setup->searchSpacesToAddModList->list.array[i];
      if (ss->searchSpaceType->present == target_ss) {
        AssertFatal(ss->controlResourceSetId, "searchSpaceId %ld has a NULL controlResourceSetId\n", ss->searchSpaceId);
        return ss;
      }
    }
    AssertFatal(false, "Couldn't find an adequate SearchSpace for target SearchSpace %d in BWP_DownlinkDedicated\n", target_ss);
  }

  const int n = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->commonSearchSpaceList->list.count;
  for (int i = 0; i < n; i++) {
    NR_SearchSpace_t *ss =
        scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->commonSearchSpaceList->list.array[i];
    if (ss->searchSpaceType->present == target_ss) {
      AssertFatal(ss->controlResourceSetId, "searchSpaceId %ld has a NULL controlResourceSetId\n", ss->searchSpaceId);
      return ss;
    }
  }
  AssertFatal(false, "Couldn't find an adequate SearchSpace for target SearchSpace %d in ServingCellConfigCommon\n", target_ss);
}

/// @brief Get CORESET resource block parameters from frequency domain resources bitmap.
/// @param coreset Pointer to the CORESET configuratio
/// @param n_rb Output parameter for total # of RBs in the CORESET
/// @param rb_start Output parameter for the starting resource block index of the CORESET
static void get_coreset_rb_params(const NR_ControlResourceSet_t *coreset, uint16_t *n_rb, uint16_t *rb_start)
{
  AssertFatal(!coreset->ext1 || !coreset->ext1->rb_Offset_r16, "rb-Offset in coreset configuration not handled\n");
  *n_rb = 0;
  *rb_start = 0;

  for (int i = 0; i < 6; i++) {
    for (int t = 0; t < 8; t++) {
      if ((coreset->frequencyDomainResources.buf[i] >> (7 - t)) & 1) {
        if (*n_rb == 0) {
            *rb_start = 48 * i + t * 6;
        }
        *n_rb += 6;  // Each bit represents 6 RBs
      }
    }
  }
}

NR_sched_pdcch_t set_pdcch_structure(nr_cell_sched_t *cell,
                                     NR_SearchSpace_t *ss,
                                     NR_ControlResourceSet_t *coreset,
                                     NR_ServingCellConfigCommon_t *scc,
                                     NR_BWP_t *bwp,
                                     NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config)
{
  AssertFatal(*ss->controlResourceSetId == coreset->controlResourceSetId,
              "coreset id in SS %ld does not correspond to the one in coreset %ld",
              *ss->controlResourceSetId, coreset->controlResourceSetId);

  int sps;
  NR_sched_pdcch_t pdcch;
  if (bwp) { // This is not for SIB1
    if(coreset->controlResourceSetId == 0) {
      pdcch.BWPSize = cell->cset0_bwp_size;
      pdcch.BWPStart = cell->cset0_bwp_start;
    } else {
      pdcch.BWPSize = NRRIV2BW(bwp->locationAndBandwidth, MAX_BWP_SIZE);
      pdcch.BWPStart = NRRIV2PRBOFFSET(bwp->locationAndBandwidth, MAX_BWP_SIZE);
    }
    pdcch.SubcarrierSpacing = bwp->subcarrierSpacing;
    pdcch.CyclicPrefix = (bwp->cyclicPrefix == NULL) ? 0 : *bwp->cyclicPrefix;
    //AssertFatal(pdcch_scs==kHz15, "PDCCH SCS above 15kHz not allowed if a symbol above 2 is monitored");
    sps = bwp->cyclicPrefix == NULL ? 14 : 12;
  } else {
    AssertFatal(type0_PDCCH_CSS_config != NULL, "type0_PDCCH_CSS_config is null,bwp %p\n", bwp);
    pdcch.BWPSize = type0_PDCCH_CSS_config->num_rbs;
    pdcch.BWPStart = type0_PDCCH_CSS_config->cset_start_rb;
    pdcch.SubcarrierSpacing = type0_PDCCH_CSS_config->scs_pdcch;
    pdcch.CyclicPrefix = 0;
    sps = 14;
  }

  AssertFatal(ss->monitoringSymbolsWithinSlot != NULL, "ss->monitoringSymbolsWithinSlot is null\n");
  BIT_STRING_t *symbolsInSlot = ss->monitoringSymbolsWithinSlot;
  AssertFatal(symbolsInSlot->buf != NULL, "ss->monitoringSymbolsWithinSlot->buf is null\n");

  int monitoringSymbolsWithinSlot = nr_pdcch_monitoring_symbols_mask(symbolsInSlot, sps);

  for (int i = 0; i < sps; i++) {
    if ((monitoringSymbolsWithinSlot >> (sps - 1 - i)) & 1) {
      pdcch.StartSymbolIndex = i;
      break;
    }
  }

  pdcch.DurationSymbols = coreset->duration;

  //cce-REG-MappingType
  pdcch.CceRegMappingType = coreset->cce_REG_MappingType.present == NR_ControlResourceSet__cce_REG_MappingType_PR_interleaved?
    NFAPI_NR_CCE_REG_MAPPING_INTERLEAVED : NFAPI_NR_CCE_REG_MAPPING_NON_INTERLEAVED;

  if (pdcch.CceRegMappingType == NFAPI_NR_CCE_REG_MAPPING_INTERLEAVED) {
    pdcch.RegBundleSize = (coreset->cce_REG_MappingType.choice.interleaved->reg_BundleSize ==
                                NR_ControlResourceSet__cce_REG_MappingType__interleaved__reg_BundleSize_n6) ? 6 : (2+coreset->cce_REG_MappingType.choice.interleaved->reg_BundleSize);
    pdcch.InterleaverSize = (coreset->cce_REG_MappingType.choice.interleaved->interleaverSize ==
                                  NR_ControlResourceSet__cce_REG_MappingType__interleaved__interleaverSize_n6) ? 6 : (2+coreset->cce_REG_MappingType.choice.interleaved->interleaverSize);
    AssertFatal(scc->physCellId != NULL,"scc->physCellId is null\n");
    pdcch.ShiftIndex = coreset->cce_REG_MappingType.choice.interleaved->shiftIndex != NULL ? *coreset->cce_REG_MappingType.choice.interleaved->shiftIndex : *scc->physCellId;
  }
  else {
    pdcch.RegBundleSize = 6;
    pdcch.InterleaverSize = 0;
    pdcch.ShiftIndex = 0;
  }

  get_coreset_rb_params(coreset, &pdcch.n_rb, &pdcch.rb_start);

  return pdcch;
}

int find_pdcch_candidate(const nr_cell_sched_t *cell,
                         int aggregation,
                         int nr_of_candidates,
                         int beam_idx,
                         const NR_sched_pdcch_t *pdcch,
                         const NR_ControlResourceSet_t *coreset,
                         uint32_t Y)
{
  const uint16_t *vrb_map = cell->common_channels.vrb_map[beam_idx];
  const int N_ci = 0;

  const int N_rb = pdcch->n_rb;  // nb of rbs of coreset per symbol
  const int N_symb = coreset->duration; // nb of coreset symbols
  const int N_regs = N_rb * N_symb; // nb of REGs per coreset
  const int N_cces = N_regs / NR_NB_REG_PER_CCE; // nb of cces in coreset
  const int R = pdcch->InterleaverSize;
  const int L = pdcch->RegBundleSize;
  const int C = R > 0 ? N_regs / (L * R) : 0;
  const int B_rb = L / N_symb; // nb of RBs occupied by each REG bundle

  // loop over all the available candidates
  // this implements TS 38.211 Sec. 7.3.2.2
  for(int m = 0; m < nr_of_candidates; m++) { // loop over candidates
    bool taken = false; // flag if the resource for a given candidate are taken
    int first_cce = aggregation * ((Y + ((m * N_cces) / (aggregation * nr_of_candidates)) + N_ci) % (N_cces / aggregation));
    LOG_D(NR_MAC,"Candidate %d of %d first_cce %d (L %d N_cces %d Y %d)\n", m, nr_of_candidates, first_cce, aggregation, N_cces, Y);
    for (int j = first_cce; (j < first_cce + aggregation) && !taken; j++) { // loop over CCEs
      for (int k = 6 * j / L; (k < (6 * j / L + 6 / L)) && !taken; k++) { // loop over REG bundles
        int f = cce_to_reg_interleaving(R, k, pdcch->ShiftIndex, C, L, N_regs);
        for(int rb = 0; rb < B_rb; rb++) { // loop over the RBs of the bundle
          if(vrb_map[pdcch->BWPStart + f * B_rb + rb] & SL_to_bitmap(pdcch->StartSymbolIndex,N_symb)) {
            taken = true;
            break;
          }
        }
      }
    }
    if(!taken)
      return first_cce;
  }
  return -1;
}

/// @brief Orders PDCCH aggregation levels so that we first check desired aggregation level according to
///        pdcch_cl_adjust
/// @param agg_level_search_order in/out 5-element array of aggregation levels from 0 to 4
/// @param pdcch_cl_adjust value from 0 to 1 indication channel impariments (0 - good channel, 1 - bad channel)
static void determine_aggregation_level_search_order(int agg_level_search_order[NUM_PDCCH_AGG_LEVELS], float pdcch_cl_adjust)
{
  int desired_agg_level_index = round(4 * pdcch_cl_adjust);
  int agg_level_search_index = 0;
  for (int i = desired_agg_level_index; i < NUM_PDCCH_AGG_LEVELS; i++) {
    agg_level_search_order[agg_level_search_index++] = i;
  }
  for (int i = desired_agg_level_index - 1; i >= 0; i--) {
    agg_level_search_order[agg_level_search_index++] = i;
  }
}

int get_cce_index(const nr_cell_sched_t *cell,
                  const int slot,
                  const rnti_t rnti,
                  int *aggregation_level,
                  int beam_idx,
                  const NR_SearchSpace_t *ss,
                  const NR_ControlResourceSet_t *coreset,
                  NR_sched_pdcch_t *sched_pdcch,
                  float pdcch_cl_adjust)
{
  const uint32_t Y = get_Y(ss, slot, rnti);
  int agg_level_search_order[NUM_PDCCH_AGG_LEVELS];
  determine_aggregation_level_search_order(agg_level_search_order, pdcch_cl_adjust);
  int nr_of_candidates;
  for (int i = 0; i < NUM_PDCCH_AGG_LEVELS; i++) {
    find_aggregation_candidates(aggregation_level, &nr_of_candidates, ss, 1 << agg_level_search_order[i]);
    if (nr_of_candidates > 0)
      break;
  }
  int CCEIndex = find_pdcch_candidate(cell, *aggregation_level, nr_of_candidates, beam_idx, sched_pdcch, coreset, Y);
  return CCEIndex;
}

void fill_pdcch_vrb_map(nr_cell_sched_t *cell,
                        NR_sched_pdcch_t *pdcch,
                        int first_cce,
                        int aggregation,
                        int beam)
{
  uint16_t *vrb_map = cell->common_channels.vrb_map[beam];

  int N_rb = pdcch->n_rb; // nb of rbs of coreset per symbol
  int L = pdcch->RegBundleSize;
  int R = pdcch->InterleaverSize;
  int n_shift = pdcch->ShiftIndex;
  int N_symb = pdcch->DurationSymbols;
  int N_regs = N_rb*N_symb; // nb of REGs per coreset
  int B_rb = L/N_symb; // nb of RBs occupied by each REG bundle
  int C = R>0 ? N_regs/(L*R) : 0;

  for (int j=first_cce; j<first_cce+aggregation; j++) { // loop over CCEs
    for (int k=6*j/L; k<(6*j/L+6/L); k++) { // loop over REG bundles
      int f = cce_to_reg_interleaving(R, k, n_shift, C, L, N_regs);
      for(int rb=0; rb<B_rb; rb++) // loop over the RBs of the bundle
        vrb_map[pdcch->BWPStart + f*B_rb + rb] |= SL_to_bitmap(pdcch->StartSymbolIndex, N_symb);
    }
  }
}

bool update_rb_mcs_tbs(NR_sched_pdsch_t *pdsch, uint32_t num_total_bytes, uint16_t *vrb_map)
{
  const NR_tda_info_t *tda_info = &pdsch->tda_info;

  // Calculate number of PRB_DMRS
  uint8_t N_PRB_DMRS = pdsch->dmrs_parms.N_PRB_DMRS;
  LOG_D(MAC, "dlDmrsSymbPos %x\n", pdsch->dmrs_parms.dl_dmrs_symb_pos);
  int mcsTableIdx = 0;
  const uint16_t slbitmap = SL_to_bitmap(tda_info->startSymbolIndex, tda_info->nrOfSymbols);
  int bwpSize = pdsch->bwp_info.bwpSize;
  int bwpStart = pdsch->bwp_info.bwpStart;

  for (pdsch->mcs = 0; pdsch->mcs < 10; pdsch->mcs++) {
    pdsch->Qm = nr_get_Qm_dl(pdsch->mcs, mcsTableIdx);
    pdsch->R = nr_get_code_rate_dl(pdsch->mcs, mcsTableIdx);
    if (!nr_find_nb_rb(pdsch->Qm,
                       pdsch->R,
                       1, // no transform precoding for DL
                       1, // single layer
                       tda_info->nrOfSymbols,
                       pdsch->dmrs_parms.N_PRB_DMRS * pdsch->dmrs_parms.N_DMRS_SLOT,
                       num_total_bytes,
                       1, // min_rbSize
                       bwpSize, // max_rbSize,
                       &pdsch->tb_size,
                       &pdsch->rbSize))
      continue;
    int rbStart, rbSize;
    if (get_rb_alloc(pdsch->rbSize, pdsch->rbSize, bwpStart, bwpSize, vrb_map, slbitmap, &rbStart, &rbSize)) {
      pdsch->rbStart = rbStart;
      pdsch->rbSize = rbSize;
      pdsch->alloc_type = PDSCH_TYPE1;
      break;
    }
  }

  if (pdsch->mcs >= 10 || pdsch->tb_size < num_total_bytes) {
    LOG_D(NR_MAC,
          "Couldn't allocate enough resources for %d bytes in SIB PDSCH (rbStart %d, rbSize %d, bwpSize %d)\n",
          num_total_bytes,
          pdsch->rbStart,
          pdsch->rbSize,
          bwpSize);
    return false;
  }

  LOG_D(NR_MAC,
        "mcs=%i, startSymbolIndex = %i, nrOfSymbols = %i, rbSize = %i, TBS = %i, dmrs_length %d, N_PRB_DMRS = %d, mappingtype = %d\n",
        pdsch->mcs,
        tda_info->startSymbolIndex,
        tda_info->nrOfSymbols,
        pdsch->rbSize,
        pdsch->tb_size,
        pdsch->dmrs_parms.N_DMRS_SLOT,
        N_PRB_DMRS,
        tda_info->mapping_type);
  return true;
}

bool nr_find_nb_rb(uint16_t Qm,
                   uint16_t R,
                   long transform_precoding,
                   uint8_t nrOfLayers,
                   uint16_t nb_symb_sch,
                   uint16_t nb_dmrs_prb,
                   uint32_t bytes,
                   uint16_t nb_rb_min,
                   uint16_t nb_rb_max,
                   uint32_t *tbs,
                   uint16_t *nb_rb)
{
  /* is the maximum (not even) enough? */
  *nb_rb = nb_rb_max;
  *tbs = nr_compute_tbs(Qm, R, *nb_rb, nb_symb_sch, nb_dmrs_prb, 0, 0, nrOfLayers) >> 3;
  /* check whether it does not fit, or whether it exactly fits. Some algorithms
   * might depend on the return value! */
  if (bytes > *tbs)
    return false;
  if (bytes == *tbs)
    return true;

  /* is the minimum enough? */
  *nb_rb = nb_rb_min;
  *tbs = nr_compute_tbs(Qm, R, *nb_rb, nb_symb_sch, nb_dmrs_prb, 0, 0, nrOfLayers) >> 3;
  if (bytes <= *tbs)
    return true;

  /* perform binary search to allocate all bytes within a TBS up to nb_rb_max
   * RBs */
  int hi = nb_rb_max;
  int lo = nb_rb_min;
  for (int p = (hi + lo) / 2; lo + 1 < hi; p = (hi + lo) / 2) {
    // If by increasing p for transform precoding we already hit the high, break to avoid infinite loop
    if (p == hi)
      break;

    const uint32_t TBS = nr_compute_tbs(Qm, R, p, nb_symb_sch, nb_dmrs_prb, 0, 0, nrOfLayers) >> 3;
    if (bytes == TBS) {
      hi = p;
      break;
    } else if (bytes < TBS) {
      hi = p;
    } else {
      lo = p;
    }
  }
  *nb_rb = hi;
  *tbs = nr_compute_tbs(Qm, R, *nb_rb, nb_symb_sch, nb_dmrs_prb, 0, 0, nrOfLayers) >> 3;
  /* return whether we could allocate all bytes and stay below nb_rb_max */
  return *tbs >= bytes && *nb_rb <= nb_rb_max;
}

// Find the largest contiguous block of free RBs in the VRB map.
// Returns the block size, or 0 if no free RB is found. out_start is set to the
// first RB of the largest block when the returned size is nonzero.
int find_largest_free_block(const uint16_t *vrb_map, uint16_t slbitmap, int bwp_start, int bwp_size, int *out_start)
{
  int best_start = 0, best_len = 0;
  int cur_start = 0, cur_len = 0;
  for (int rb = 0; rb < bwp_size; rb++) {
    if (!(vrb_map[rb + bwp_start] & slbitmap)) {
      if (cur_len == 0)
        cur_start = rb;
      cur_len++;
    } else {
      if (cur_len > best_len) {
        best_start = cur_start;
        best_len = cur_len;
      }
      cur_len = 0;
    }
  }
  if (cur_len > best_len) {
    best_start = cur_start;
    best_len = cur_len;
  }
  *out_start = best_start;
  return best_len;
}

bool get_rb_alloc(int rbSize_min,
                  int rbSize_max,
                  int bwpStart,
                  int bwpSize,
                  const uint16_t *vrb_map,
                  uint16_t sym_mask,
                  int *rbStart_ptr,
                  int *rbSize_ptr)
{
  const uint16_t *bwp_map = &vrb_map[bwpStart];
  int rbStart = 0;
  while (rbStart + rbSize_min <= bwpSize) {
    // 1. Find the first free RB
    if ((bwp_map[rbStart] & sym_mask) != 0) {
      rbStart++;
      continue;
    }

    // 2. Measure contiguous block
    int current_limit = min(rbStart + rbSize_max, bwpSize);
    int rbEnd = rbStart + 1;
    while (rbEnd < current_limit && (bwp_map[rbEnd] & sym_mask) == 0) {
      rbEnd++;
    }
    int rbSize = rbEnd - rbStart;

    // 3. Validate
    if (rbSize >= rbSize_min) {
      *rbStart_ptr = rbStart;
      *rbSize_ptr = rbSize;
      return true;
    }

    // Skip the block we just checked + the occupied one that stopped us
    rbStart = rbEnd + 1;
  }
  return false;
}

const NR_DMRS_UplinkConfig_t *get_DMRS_UplinkConfig(const NR_PUSCH_Config_t *pusch_Config, const NR_tda_info_t *tda_info)
{
  if (pusch_Config == NULL)
    return NULL;

  return tda_info->mapping_type == typeA ? pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup
                                         : pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup;
}

NR_pusch_dmrs_t get_ul_dmrs_params(const NR_ServingCellConfigCommon_t *scc,
                                   const NR_UE_UL_BWP_t *ul_bwp,
                                   const NR_tda_info_t *tda_info,
                                   const int Layers,
                                   const uint16_t dmrs_ports,
                                   const uint8_t cdm_groups)
{
  NR_pusch_dmrs_t dmrs = {0};
  dmrs.dmrs_ports = (dmrs_ports != 0) ? dmrs_ports : (uint16_t)((1 << Layers) - 1);
  dmrs.num_dmrs_cdm_grps_no_data = (cdm_groups > 0) ? cdm_groups : 2;

  const NR_DMRS_UplinkConfig_t *NR_DMRS_UplinkConfig = get_DMRS_UplinkConfig(ul_bwp->pusch_Config, tda_info);
  dmrs.ptrsConfig = NR_DMRS_UplinkConfig
                    && NR_DMRS_UplinkConfig->phaseTrackingRS ?
                    NR_DMRS_UplinkConfig->phaseTrackingRS->choice.setup : NULL;

  dmrs.pusch_identity = *scc->physCellId;
  dmrs.dmrs_scrambling_id = *scc->physCellId;
  dmrs.scid = 0;
  if (ul_bwp->transform_precoding) { // transform precoding disabled
    long *scramblingid = NULL;
    if (NR_DMRS_UplinkConfig && dmrs.scid == 0)
      scramblingid = NR_DMRS_UplinkConfig->transformPrecodingDisabled->scramblingID0;
    else if (NR_DMRS_UplinkConfig)
      scramblingid = NR_DMRS_UplinkConfig->transformPrecodingDisabled->scramblingID1;
    if (scramblingid)
      dmrs.dmrs_scrambling_id = *scramblingid;
  } else {
    if (NR_DMRS_UplinkConfig && NR_DMRS_UplinkConfig->transformPrecodingEnabled) {
      if (NR_DMRS_UplinkConfig->transformPrecodingEnabled->nPUSCH_Identity)
        dmrs.pusch_identity = *NR_DMRS_UplinkConfig->transformPrecodingEnabled->nPUSCH_Identity;
      if ((!NR_DMRS_UplinkConfig->transformPrecodingEnabled->sequenceGroupHopping
          && !NR_DMRS_UplinkConfig->transformPrecodingEnabled->sequenceHopping)
          && !scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->groupHoppingEnabledTransformPrecoding)
        dmrs.low_papr_sequence_number = 0;
      else
        AssertFatal(false, "Hopping mode is not supported in transform precoding\n");
    }
  }

  dmrs.dmrs_config_type = NR_DMRS_UplinkConfig && NR_DMRS_UplinkConfig->dmrs_Type ? 1 : 0;
  pusch_dmrs_AdditionalPosition_t additional_pos = pusch_dmrs_pos2;
  if (NR_DMRS_UplinkConfig)
    additional_pos = get_pusch_dmrs_pos(NR_DMRS_UplinkConfig->dmrs_AdditionalPosition);

  const pusch_maxLength_t pusch_maxLength = NR_DMRS_UplinkConfig ? (NR_DMRS_UplinkConfig->maxLength == NULL ? 1 : 2) : 1;
  dmrs.ul_dmrs_symb_pos = get_l_prime(tda_info->nrOfSymbols,
                                       tda_info->mapping_type,
                                       additional_pos,
                                       pusch_maxLength,
                                       tda_info->startSymbolIndex,
                                       scc->dmrs_TypeA_Position);

  dmrs.num_dmrs_symb = count_bits64_with_mask(dmrs.ul_dmrs_symb_pos, tda_info->startSymbolIndex, tda_info->nrOfSymbols);
  dmrs.N_PRB_DMRS = dmrs.num_dmrs_cdm_grps_no_data * (dmrs.dmrs_config_type == 0 ? 6 : 4);
  return dmrs;
}

#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f
int nr_adapt_mcs_from_bler(int current_mcs, int min_mcs, int max_mcs, float bler, float bler_lower, float bler_upper, int num_sched)
{
  int mcs = current_mcs;
  if (bler < bler_lower && mcs < max_mcs && num_sched > 3)
    mcs++;
  else if (bler > bler_upper || num_sched <= 3) // above threshold or no activity
    mcs--;
  return max(min_mcs, min(mcs, max_mcs));
}

bool update_bler_stats(const NR_bler_options_t *bler_options,
                       const NR_mac_dir_stats_t *stats,
                       NR_bler_stats_t *bler_stats,
                       frame_t frame)
{
  int diff = frame - bler_stats->last_frame;
  if (diff < 0) // wrap around
    diff += 1024;

  if (diff < BLER_UPDATE_FRAME)
    return false;

  const int num_dl_sched = (int)(stats->rounds[0] - bler_stats->rounds[0]);
  const int num_dl_retx = (int)(stats->rounds[1] - bler_stats->rounds[1]);
  const float bler_window = num_dl_sched > 0 ? (float)num_dl_retx / num_dl_sched : bler_stats->bler;
  bler_stats->bler = BLER_FILTER * bler_stats->bler + (1 - BLER_FILTER) * bler_window;

  bler_stats->last_frame = frame;
  bler_stats->last_num_sched = num_dl_sched;
  memcpy(bler_stats->rounds, stats->rounds, sizeof(stats->rounds));
  LOG_D(MAC,
        "frame %4d BLER update (num_sched %d, num_retx %d, BLER wnd %.3f avg %.6f)\n",
        frame,
        num_dl_sched,
        num_dl_retx,
        bler_window,
        bler_stats->bler);
  return true;
}

nfapi_nr_dl_dci_pdu_t *prepare_dci_pdu(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu,
                                       const NR_ServingCellConfigCommon_t *scc,
                                       const NR_SearchSpace_t *ss,
                                       const NR_ControlResourceSet_t *coreset,
                                       const uint16_t *spatial_stream_idx,
                                       int aggregation_level,
                                       int cce_index,
                                       int beam_index,
                                       int rnti)
{
  DevAssert(pdcch_pdu->numDlDci < MAX_DCI_CORESET);
  nfapi_nr_dl_dci_pdu_t *dci_pdu = &pdcch_pdu->dci_pdu[pdcch_pdu->numDlDci];
  dci_pdu->RNTI = rnti;
  dci_pdu->AggregationLevel = aggregation_level;
  dci_pdu->CceIndex = cce_index;
  if (coreset->pdcch_DMRS_ScramblingID &&
      ss->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_ue_Specific) {
    dci_pdu->ScramblingId = *coreset->pdcch_DMRS_ScramblingID;
    dci_pdu->ScramblingRNTI = rnti;
  } else {
    dci_pdu->ScramblingId = *scc->physCellId;
    dci_pdu->ScramblingRNTI = 0;
  }

  uint16_t N_rb = 0;
  uint16_t rb_start = 0;
  get_coreset_rb_params(coreset, &N_rb, &rb_start);

  dci_pdu->beta_PDCCH_1_0 = 0;
  dci_pdu->powerControlOffsetSS = 1;
  dci_pdu->precodingAndBeamforming.num_prgs = 1;
  dci_pdu->precodingAndBeamforming.prg_size = N_rb;
  dci_pdu->precodingAndBeamforming.prgs_list[0].pm_idx = 0;

  // Spatial stream indexing for MU-MIMO
  const int num_ant_ports_per_dci = 1; // Only one stream per DCI for now
  pdcch_pdu->param_v4.numSpatialStreams = (pdcch_pdu->numDlDci + 1 /*count this dci too*/) * num_ant_ports_per_dci;
  for (uint_fast16_t i = 0; i < num_ant_ports_per_dci; i++) {
    pdcch_pdu->param_v4.dci_spatialStreamIndices[pdcch_pdu->numDlDci * num_ant_ports_per_dci + i].dci_index = pdcch_pdu->numDlDci;
    // Map the spatial stream index from the corresponding PDSCH signal
    pdcch_pdu->param_v4.dci_spatialStreamIndices[pdcch_pdu->numDlDci * num_ant_ports_per_dci + i].spatial_stream_index =
        spatial_stream_idx[i];
  }
  dci_pdu->precodingAndBeamforming.dig_bf_interfaces = num_ant_ports_per_dci;
  dci_pdu->precodingAndBeamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx = beam_index;
  return dci_pdu;
}

// See Section 5.1.2.2.1 of 38.214
static uint32_t bitmap_to_rbg_allocation(const uint8_t *rbBitmap, const NR_UE_DL_BWP_t *dl_BWP)
{
  AssertFatal(dl_BWP && dl_BWP->pdsch_Config, "DL BWP and PDSCH_config must be configured for Type0 PDSCH allocation\n");
  int N_RBG = getNRBG(dl_BWP->BWPSize, dl_BWP->BWPStart, dl_BWP->pdsch_Config->rbg_Size);
  int P = getRBGSize(dl_BWP->BWPSize, dl_BWP->pdsch_Config->rbg_Size);
  uint32_t rbg_bitmap = 0;
  for (int i = 0; i < N_RBG; i++) {
    // compute start and size of this RBG
    // LSB of byte 0 of rbBitmap represents VRB 0 per SCF document (assuming it means CRB0)
    int rbg_start, rbg_sz;
    if (i == 0) {
      rbg_start = dl_BWP->BWPStart;
      rbg_sz = P - (dl_BWP->BWPStart % P);
    } else if (i == N_RBG - 1) {
      rbg_start = dl_BWP->BWPStart + P - (dl_BWP->BWPStart % P) + (i - 1) * P;
      int tmp = (dl_BWP->BWPStart + dl_BWP->BWPSize) % P;
      rbg_sz = tmp ? tmp : P;
    } else {
      rbg_start = dl_BWP->BWPStart + P - (dl_BWP->BWPStart % P) + (i - 1) * P;
      rbg_sz = P;
    }
    // check all RBs in this RBG are either all set or all clear
    int first_rb_set = (rbBitmap[rbg_start / 8] >> (rbg_start % 8)) & 1;
    for (int rb = rbg_start + 1; rb < rbg_start + rbg_sz; rb++) {
      int rb_set = (rbBitmap[rb / 8] >> (rb % 8)) & 1;
      AssertFatal(rb_set == first_rb_set,
                 "RB bitmap is not compatible with RBG size %d: RBG %d is partially allocated (PRB %d differs from RB start %d)\n",
                  P,
                  i,
                  rb,
                  rbg_start);
    }
    int allocated = first_rb_set;
    // The order of RBG bitmap is such that RBG 0 is mapped to MSB
    if (allocated)
      rbg_bitmap |= (1 << (N_RBG - 1 - i));
  }
  return rbg_bitmap;
}


dci_pdu_rel15_t prepare_dci_dl_payload(const nr_cell_sched_t *cell,
                                       const NR_UE_info_t *UE,
                                       nr_rnti_type_t rnti_type,
                                       NR_SearchSpace__searchSpaceType_PR ss_type,
                                       const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdsch_pdu,
                                       const NR_sched_pdsch_t *sched_pdsch,
                                       const NR_sched_pucch_t *pucch,
                                       int tpc,
                                       int harq_pid,
                                       int tb_scaling,
                                       bool is_sib1)
{
  const NR_UE_DL_BWP_t *dl_BWP = UE ? &UE->current_DL_BWP : NULL;
  dci_pdu_rel15_t dci_payload = {0};
  dci_payload.format_indicator = 1;
  dci_payload.time_domain_assignment.val = sched_pdsch->time_domain_allocation;
  dci_payload.mcs = sched_pdsch->mcs;
  dci_payload.rv = pdsch_pdu->rvIndex[0];
  dci_payload.vrb_to_prb_mapping.val = 0;
  int riv_bwp = pdsch_pdu->BWPSize;
  if (!UE)
    riv_bwp = cell->cset0_bwp_size;
  else if (dl_BWP->dci_format == NR_DL_DCI_FORMAT_1_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common) {
    if (cell->cset0_bwp_size > 0)
      riv_bwp = cell->cset0_bwp_size;
    else
      riv_bwp = UE->sc_info.initial_dl_BWPSize;
  }
  if (sched_pdsch->alloc_type == PDSCH_TYPE1)
    dci_payload.frequency_domain_assignment.val = PRBalloc_to_locationandbandwidth0(pdsch_pdu->rbSize, pdsch_pdu->rbStart, riv_bwp);
  else
    dci_payload.frequency_domain_assignment.val = bitmap_to_rbg_allocation(sched_pdsch->rbBitmap, dl_BWP);
  if (rnti_type == TYPE_SI_RNTI_) {
    dci_payload.system_info_indicator = !is_sib1;
    return dci_payload;
  }

  if (rnti_type == TYPE_RA_RNTI_) {
    dci_payload.tb_scaling = tb_scaling;
    return dci_payload;
  }

  if (rnti_type == TYPE_P_RNTI_) {
    /* Paging DCI 1_0: P-RNTI has no UE-specific HARQ fields. */
    dci_payload.tb_scaling = tb_scaling;
    return dci_payload;
  }

  dci_payload.dmrs_sequence_initialization.val = pdsch_pdu->SCID;
  dci_payload.antenna_ports.val = sched_pdsch->dmrs_parms.dmrs_ports_id;
  dci_payload.tpc = tpc;

  const NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  const NR_UE_harq_t *harq = &sched_ctrl->harq_processes[harq_pid];
  AssertFatal(harq, "HARQ process should be available for DCI with RNTI %s\n", rnti_types(rnti_type));
  dci_payload.harq_pid.val = harq_pid;
  dci_payload.ndi = harq->ndi;
  dci_payload.pucch_resource_indicator = pucch ? pucch->resource_indicator : 0;
  dci_payload.pdsch_to_harq_feedback_timing_indicator.val = pucch ? pucch->timing_indicator : 0; // PDSCH to HARQ TI
  dci_payload.dai[0].val = pucch ? (pucch->dai_c - 1) & 3 : 0;
  // bwp indicator
  // as per table 7.3.1.1.2-1 in 38.212
  if (dl_BWP) {
    int ext_bwp_id = dl_BWP->bwp_id ? 1 : 0; // BWP-ID sent to the UE is always 1 except for BWP 0
    dci_payload.bwp_indicator.val = UE->sc_info.n_dl_bwp < 4 ? ext_bwp_id : ext_bwp_id - 1;
  }
  return dci_payload;
}

static uint32_t compute_srs_resource_indicator(long *maxMIMO_Layers,
                                               NR_PUSCH_Config_t *pusch_Config,
                                               NR_SRS_Config_t *srs_config,
                                               nr_srs_feedback_t *srs_feedback)
{
  uint32_t val = 0;
  if (srs_config && pusch_Config && pusch_Config->txConfig != NULL) {
    if (*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook) {

      // TS 38.212 - Section 7.3.1.1.2: SRS resource indicator has ceil(log2(N_SRS)) bits according to
      // Tables 7.3.1.1.2-32, 7.3.1.1.2-32A and 7.3.1.1.2-32B if the higher layer parameter txConfig = codebook,
      // where N_SRS is the number of configured SRS resources in the SRS resource set configured by higher layer
      // parameter srs-ResourceSetToAddModList, and associated with the higher layer parameter usage of value codeBook.
      int count = srs_codebook_nb_res(srs_config);
      if (count > 1)
        val = table_7_3_1_1_2_32[count - 2][srs_feedback->sri];
    } else {
      // TS 38.212 - Section 7.3.1.1.2: SRS resource indicator has ceil(log2(sum(k = 1 until min(Lmax,N_SRS) of binomial(N_SRS,k))))
      // bits according to Tables 7.3.1.1.2-28/29/30/31 if the higher layer parameter txConfig = nonCodebook, where
      // N_SRS is the number of configured SRS resources in the SRS resource set configured by higher layer parameter
      // srs-ResourceSetToAddModList, and associated with the higher layer parameter usage of value nonCodeBook and:
      //
      // - if UE supports operation with maxMIMO-Layers and the higher layer parameter maxMIMO-Layers of
      // PUSCH-ServingCellConfig of the serving cell is configured, Lmax is given by that parameter;
      //
      // - otherwise, Lmax is given by the maximum number of layers for PUSCH supported by the UE for the serving cell
      // for non-codebook based operation.
      int Lmax = 0;
      if (maxMIMO_Layers != NULL)
        Lmax = *maxMIMO_Layers;
      else
        AssertFatal(false, "MIMO on PUSCH not supported, maxMIMO_Layers needs to be set to 1\n");
      int count = srs_non_codebook_nb_res(srs_config);
      int lsum = srs_binomial_sum(count, Lmax);
      if (lsum > 0 && ceil_log2_u32(lsum) > 0) {
        switch(Lmax) {
          case 1:
            val = table_7_3_1_1_2_28[count-2][srs_feedback->sri];
            break;
          case 2:
            val = table_7_3_1_1_2_29[count-2][srs_feedback->sri];
            break;
          case 3:
            val = table_7_3_1_1_2_30[count-2][srs_feedback->sri];
            break;
          case 4:
            val = table_7_3_1_1_2_31[count-2][srs_feedback->sri];
            break;
          default:
            LOG_E(NR_MAC, "%s (%d) - Invalid Lmax %d\n", __FUNCTION__, __LINE__, Lmax);
        }
      }
    }
  }
  return val;
}

static uint32_t compute_precoding_information(NR_PUSCH_Config_t *pusch_Config,
                                              NR_SRS_Config_t *srs_config,
                                              dci_field_t srs_resource_indicator,
                                              nr_srs_feedback_t *srs_feedback,
                                              const uint8_t *nrOfLayers,
                                              int *tpmi)
{
  uint32_t val = 0;

  uint8_t pusch_antenna_ports = get_pusch_nb_antenna_ports(pusch_Config, srs_config, srs_resource_indicator);
  if (!pusch_Config
      || (pusch_Config->txConfig != NULL && *pusch_Config->txConfig == NR_PUSCH_Config__txConfig_nonCodebook)
      || pusch_antenna_ports == 1) {
    return val;
  }

  long max_rank = *pusch_Config->maxRank;
  long *ul_FullPowerTransmission = pusch_Config->ext1 ? pusch_Config->ext1->ul_FullPowerTransmission_r16 : NULL;
  long *codebookSubset = pusch_Config->codebookSubset;
  int ul_tpmi = tpmi ? *tpmi : srs_feedback ? srs_feedback->tpmi : -1;

  if (pusch_antenna_ports == 2) {

    if (max_rank == 1) {
      // - 1 or 3 bits according to Table 7.3.1.1.2-5 for 2 antenna ports, if txConfig = codebook, ul-FullPowerTransmission
      //   is not configured or configured to fullpowerMode2 or configured to fullpower, and according to whether transform
      //   precoder is enabled or disabled, and the values of higher layer parameters maxRank and codebookSubset;
      // - 2 bits according to Table 7.3.1.1.2-5A for 2 antenna ports, if txConfig = codebook, ul-FullPowerTransmission =
      //   fullpowerMode1, maxRank=1, and according to whether transform precoder is enabled or disabled, and the values
      //   of higher layer parameter codebookSubset;
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        AssertFatal(ul_tpmi <= 2,"TPMI %d is invalid!\n", ul_tpmi);
        val = ul_tpmi;
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          AssertFatal(ul_tpmi <= 1,"TPMI %d is invalid!\n", ul_tpmi);
          val = ul_tpmi;
        } else {
          AssertFatal(ul_tpmi <= 5,"TPMI %d is invalid!\n", ul_tpmi);
          val = ul_tpmi;
        }
      }
    } else {
      // - 2 or 4 bits according to Table 7.3.1.1.2-4 for 2 antenna ports, if txConfig = codebook, ul-FullPowerTransmission
      //   is not configured or configured to fullpowerMode2 or configured to fullpower, and according to whether transform
      //   precoder is enabled or disabled, and the values of higher layer parameters maxRank and codebookSubset;
      // - 2 bits according to Table 7.3.1.1.2-4A for 2 antenna ports, if txConfig = codebook, ul-FullPowerTransmission =
      //   fullpowerMode1, transform precoder is disabled, maxRank=2, and codebookSubset=nonCoherent;
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        AssertFatal((*nrOfLayers==1 && ul_tpmi <= 2)
                    || (*nrOfLayers==2 && ul_tpmi == 0),
                    "TPMI %d is invalid!\n",
                    ul_tpmi);
        val = *nrOfLayers == 1 ? table_7_3_1_1_2_4A_1layer[ul_tpmi] : 2;
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          AssertFatal((*nrOfLayers==1 && ul_tpmi <= 1)
                      || (*nrOfLayers==2 && ul_tpmi == 0),
                      "TPMI %d is invalid!\n",
                      ul_tpmi);
          val = *nrOfLayers == 1 ? ul_tpmi : 2;
        } else {
          AssertFatal((*nrOfLayers==1 && ul_tpmi <= 5)
                      || (*nrOfLayers==2 && ul_tpmi <= 2),
                      "TPMI %d is invalid!\n",
                      ul_tpmi);
          val = *nrOfLayers == 1 ? table_7_3_1_1_2_4_1layer_fullyAndPartialAndNonCoherent[ul_tpmi] :
                                   table_7_3_1_1_2_4_2layers_fullyAndPartialAndNonCoherent[ul_tpmi];
        }
      }
    }

  } else if (pusch_antenna_ports == 4) {

    if (max_rank == 1) {
      // - 2, 4, or 5 bits according to Table 7.3.1.1.2-3 for 4 antenna ports, if txConfig = codebook, ul-FullPowerTransmission
      //   is not configured or configured to fullpowerMode2 or configured to fullpower, and according to whether transform
      //   precoder is enabled or disabled, and the values of higher layer parameters maxRank, and codebookSubset;
      // - 3 or 4 bits according to Table 7.3.1.1.2-3A for 4 antenna ports, if txConfig = codebook, ul-FullPowerTransmission =
      //   fullpowerMode1, maxRank=1, and according to whether transform precoder is enabled or disabled, and the values
      //   of higher layer parameter codebookSubset;
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
           AssertFatal(ul_tpmi <= 3 || ul_tpmi == 13, "TPMI %d is invalid!\n", ul_tpmi);
        } else {
          AssertFatal(ul_tpmi <= 15, "TPMI %d is invalid!\n", ul_tpmi);
        }
        val = table_7_3_1_1_2_3A[ul_tpmi];
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          AssertFatal(ul_tpmi <= 3, "TPMI %d is invalid!\n", ul_tpmi);
        } else if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent) {
          AssertFatal(ul_tpmi <= 11, "TPMI %d is invalid!\n", ul_tpmi);
        } else {
          AssertFatal(ul_tpmi <= 27, "TPMI %d is invalid!\n", ul_tpmi);
        }
        val = ul_tpmi;
      }
    } else {
      // - 4, 5, or 6 bits according to Table 7.3.1.1.2-2 for 4 antenna ports, if txConfig = codebook, ul-FullPowerTransmission
      //   is not configured or configured to fullpowerMode2 or configured to fullpower, and according to whether transform
      //   precoder is enabled or disabled, and the values of higher layer parameters maxRank, and codebookSubset;
      // - 4 or 5 bits according to Table 7.3.1.1.2-2A for 4 antenna ports, if txConfig = codebook, ul-FullPowerTransmission =
      //   fullpowerMode1, maxRank=2, transform precoder is disabled, and according to the values of higher layer parameter
      //   codebookSubset;
      // - 4 or 6 bits according to Table 7.3.1.1.2-2B for 4 antenna ports, if txConfig = codebook, ul-FullPowerTransmission =
      //   fullpowerMode1, maxRank=3 or 4, transform precoder is disabled, and according to the values of higher layer
      //   parameter codebookSubset;
      if (ul_FullPowerTransmission && *ul_FullPowerTransmission == NR_PUSCH_Config__ext1__ul_FullPowerTransmission_r16_fullpowerMode1) {
        if (max_rank == 2) {
          if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
            AssertFatal((*nrOfLayers==1
                        && (ul_tpmi <= 3 || ul_tpmi == 13))
                        || (*nrOfLayers == 2 && ul_tpmi <= 6),
                        "TPMI %d is invalid!\n",
                        ul_tpmi);
          } else {
            AssertFatal((*nrOfLayers == 1 && ul_tpmi <= 15)
                        || (*nrOfLayers == 2 && ul_tpmi <= 13),
                        "TPMI %d is invalid!\n",
                        ul_tpmi);
          }
          val = *nrOfLayers == 1 ? table_7_3_1_1_2_2A_1layer[ul_tpmi] : table_7_3_1_1_2_2A_2layers[ul_tpmi];
        } else {
          if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
            AssertFatal((*nrOfLayers == 1
                        && (ul_tpmi <= 3 || ul_tpmi == 13))
                        || (*nrOfLayers == 2 && ul_tpmi <= 6)
                        || (*nrOfLayers == 3 && ul_tpmi <= 1)
                        || (*nrOfLayers == 4 && ul_tpmi == 0),
                        "TPMI %d is invalid!\n",
                        ul_tpmi);
          } else {
            AssertFatal((*nrOfLayers == 1 && ul_tpmi <= 15)
                        || (*nrOfLayers == 2 && ul_tpmi <= 13)
                        || (*nrOfLayers == 3 && ul_tpmi <= 2)
                        || (*nrOfLayers == 4 && ul_tpmi <= 2),
                        "TPMI %d is invalid!\n",
                        ul_tpmi);
          }
          switch (*nrOfLayers) {
            case 1:
              val = table_7_3_1_1_2_2B_1layer[ul_tpmi];
              break;
            case 2:
              val = table_7_3_1_1_2_2B_2layers[ul_tpmi];
              break;
            case 3:
              val = table_7_3_1_1_2_2B_3layers[ul_tpmi];
              break;
            case 4:
              val = table_7_3_1_1_2_2B_4layers[ul_tpmi];
              break;
            default:
              LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
          }
        }
      } else {
        if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent) {
          AssertFatal((*nrOfLayers == 1 && ul_tpmi <= 3)
                      || (*nrOfLayers == 2 && ul_tpmi <= 5)
                      || (*nrOfLayers == 3 && ul_tpmi == 0)
                      || (*nrOfLayers == 4 && ul_tpmi == 0),
                      "TPMI %d is invalid!\n",
                      ul_tpmi);
        } else if (codebookSubset && *codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent) {
          AssertFatal((*nrOfLayers == 1 && ul_tpmi <= 11)
                      || (*nrOfLayers == 2 && ul_tpmi <= 13)
                      || (*nrOfLayers == 3 && ul_tpmi <= 2)
                      || (*nrOfLayers == 4 && ul_tpmi <= 2),
                      "TPMI %d is invalid!\n",
                      ul_tpmi);
        } else {
          AssertFatal((*nrOfLayers == 1 && ul_tpmi <= 28)
                      || (*nrOfLayers == 2 && ul_tpmi <= 22)
                      || (*nrOfLayers == 3 && ul_tpmi <= 7)
                      || (*nrOfLayers == 4 && ul_tpmi <= 5),
                      "TPMI %d is invalid!\n",
                      ul_tpmi);
        }
        switch (*nrOfLayers) {
          case 1:
            val = table_7_3_1_1_2_2_1layer[ul_tpmi];
            break;
          case 2:
            val = table_7_3_1_1_2_2_2layers[ul_tpmi];
            break;
          case 3:
            val = table_7_3_1_1_2_2_3layers[ul_tpmi];
            break;
          case 4:
            val = table_7_3_1_1_2_2_4layers[ul_tpmi];
            break;
          default:
            LOG_E(NR_MAC,"Number of layers %d is invalid!\n", *nrOfLayers);
        }
      }
    }
  }
  return val;
}

static uint8_t get_pusch_front_load_symb(const nfapi_nr_pusch_pdu_t *pusch_pdu)
{
  // Detect if the scheduled DMRS uses 1 or 2 front-loaded symbols
  // Consecutive bits signify a double-symbol DMRS
  for (int i = 0; i < NR_SYMBOLS_PER_SLOT - 1; i++) {
    if (((pusch_pdu->ul_dmrs_symb_pos >> i) & 0x3) == 0x3)
      return 2;
  }
  return 1;
}

void config_uldci(const NR_UE_ServingCell_Info_t *sc_info,
                  const nfapi_nr_pusch_pdu_t *pusch_pdu,
                  dci_pdu_rel15_t *dci_pdu_rel15,
                  nr_srs_feedback_t *srs_feedback,
                  int *tpmi,
                  int time_domain_assignment,
                  uint8_t tpc,
                  uint8_t ndi,
                  NR_UE_UL_BWP_t *ul_bwp,
                  NR_SearchSpace__searchSpaceType_PR ss_type)
{
  nr_dci_format_t dci_format = ul_bwp->dci_format;

  // 3GPP TS 38.214 Section 6.1.2.2.2 Uplink resource allocation type 1
  uint16_t riv_bwp_size = ul_bwp->BWPSize;
  if (dci_format == NR_UL_DCI_FORMAT_0_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common && ul_bwp->pusch_Config
      && ul_bwp->pusch_Config->resourceAllocation == NR_PUSCH_Config__resourceAllocation_resourceAllocationType1) {
    riv_bwp_size = sc_info->initial_ul_BWPSize;
  }
  dci_pdu_rel15->frequency_domain_assignment.val =
      PRBalloc_to_locationandbandwidth0(pusch_pdu->rb_size, pusch_pdu->rb_start, riv_bwp_size);
  dci_pdu_rel15->time_domain_assignment.val = time_domain_assignment;
  dci_pdu_rel15->frequency_hopping_flag.val = pusch_pdu->frequency_hopping;
  dci_pdu_rel15->mcs = pusch_pdu->mcs_index;
  dci_pdu_rel15->ndi = ndi;
  dci_pdu_rel15->rv = pusch_pdu->pusch_data.rv_index;
  dci_pdu_rel15->harq_pid.val = pusch_pdu->pusch_data.harq_process_id;
  dci_pdu_rel15->tpc = tpc;

  NR_PUSCH_Config_t *pusch_Config = ul_bwp->pusch_Config;
  if (pusch_Config)
    AssertFatal(pusch_Config->resourceAllocation == NR_PUSCH_Config__resourceAllocation_resourceAllocationType1,
                "Only frequency resource allocation type 1 is currently supported\n");

  switch (dci_format) {
    case NR_UL_DCI_FORMAT_0_0:
      dci_pdu_rel15->format_indicator = 0;
      break;
    case NR_UL_DCI_FORMAT_0_1:
      LOG_D(NR_MAC,"Configuring DCI Format 0_1\n");
      dci_pdu_rel15->dai[0].val = 0; //TODO
      // bwp indicator as per table 7.3.1.1.2-1 in 38.212
      int ext_bwp_id = ul_bwp->bwp_id ? 1 : 0; // BWP-ID sent to the UE is always 1 except for BWP 0
      dci_pdu_rel15->bwp_indicator.val = sc_info->n_ul_bwp < 4 ? ext_bwp_id : ext_bwp_id - 1;
      // SRS resource indicator
      if (pusch_Config && pusch_Config->txConfig != NULL) {
        AssertFatal(*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook,
                    "Non Codebook configuration non supported\n");
        dci_pdu_rel15->srs_resource_indicator.val = compute_srs_resource_indicator(sc_info->maxMIMO_Layers_PUSCH,
                                                                                   pusch_Config,
                                                                                   ul_bwp->srs_Config,
                                                                                   srs_feedback);
      }
      dci_pdu_rel15->precoding_information.val = compute_precoding_information(pusch_Config,
                                                                               ul_bwp->srs_Config,
                                                                               dci_pdu_rel15->srs_resource_indicator,
                                                                               srs_feedback,
                                                                               &pusch_pdu->nrOfLayers,
                                                                               tpmi);

      // Antenna Ports
      uint8_t front_load_symb = get_pusch_front_load_symb(pusch_pdu);
      int antenna_ports_val = get_dci_antenna_ports_val(pusch_pdu->nrOfLayers,
                                                        pusch_pdu->dmrs_ports,
                                                        pusch_pdu->num_dmrs_cdm_grps_no_data,
                                                        pusch_pdu->dmrs_config_type,
                                                        front_load_symb,
                                                        ul_bwp->transform_precoding);
      if (antenna_ports_val < 0) {
        LOG_E(NR_MAC,
              "No DCI antenna_ports entry for rank=%d ports=0x%04x cdm=%d type=%d front load symbols %d\n",
              pusch_pdu->nrOfLayers,
              pusch_pdu->dmrs_ports,
              pusch_pdu->num_dmrs_cdm_grps_no_data,
              pusch_pdu->dmrs_config_type,
              front_load_symb);
        antenna_ports_val = 0;
      }
      dci_pdu_rel15->antenna_ports.val = (uint32_t)antenna_ports_val;

      // DMRS sequence initialization
      dci_pdu_rel15->dmrs_sequence_initialization.val = pusch_pdu->scid;
      break;
    default :
      AssertFatal(0, "Valid UL formats are 0_0 and 0_1\n");
  }

  LOG_D(NR_MAC,
        "%s() ULDCI type 0 payload: dci_format %d, freq_alloc %d, time_alloc %d, freq_hop_flag %d, precoding_information.val %d antenna_ports.val %d mcs %d tpc %d ndi %d rv %d\n",
        __func__,
        dci_format,
        dci_pdu_rel15->frequency_domain_assignment.val,
        dci_pdu_rel15->time_domain_assignment.val,
        dci_pdu_rel15->frequency_hopping_flag.val,
        dci_pdu_rel15->precoding_information.val,
        dci_pdu_rel15->antenna_ports.val,
        dci_pdu_rel15->mcs,
        dci_pdu_rel15->tpc,
        dci_pdu_rel15->ndi,
        dci_pdu_rel15->rv);
}

const int default_pucch_fmt[]       = {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1};
const int default_pucch_firstsymb[] = {12,12,12,10,10,10,10,4,4,4,4,0,0,0,0,0};
const int default_pucch_numbsymb[]  = {2,2,2,2,4,4,4,4,10,10,10,10,14,14,14,14,14};
const int default_pucch_prboffset[] = {0,0,3,0,0,2,4,0,0,2,4,0,0,2,4,-1};
const int default_pucch_csset[]     = {2,3,3,2,4,4,4,2,4,4,4,2,4,4,4,4};

void nr_configure_pdcch(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu, NR_ControlResourceSet_t *coreset, NR_sched_pdcch_t *pdcch)
{
  pdcch_pdu->BWPSize = pdcch->BWPSize;
  pdcch_pdu->BWPStart = pdcch->BWPStart;
  pdcch_pdu->SubcarrierSpacing = pdcch->SubcarrierSpacing;
  pdcch_pdu->CyclicPrefix = pdcch->CyclicPrefix;
  pdcch_pdu->StartSymbolIndex = pdcch->StartSymbolIndex;

  pdcch_pdu->DurationSymbols  = coreset->duration;

  for (int i = 0; i < 6; i++)
    pdcch_pdu->FreqDomainResource[i] = coreset->frequencyDomainResources.buf[i];

  LOG_D(NR_MAC,
        "Coreset : BWPstart %d, BWPsize %d, SCS %d, freq %x, , duration %ld\n",
        pdcch_pdu->BWPStart,
        pdcch_pdu->BWPSize,
        pdcch_pdu->SubcarrierSpacing,
        coreset->frequencyDomainResources.buf[0],
        coreset->duration);

  pdcch_pdu->CceRegMappingType = pdcch->CceRegMappingType;
  pdcch_pdu->RegBundleSize = pdcch->RegBundleSize;
  pdcch_pdu->InterleaverSize = pdcch->InterleaverSize;
  pdcch_pdu->ShiftIndex = pdcch->ShiftIndex;

  pdcch_pdu->CoreSetType = coreset->controlResourceSetId != 0 ? NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG : NFAPI_NR_CSET_CONFIG_MIB_SIB1;

  //precoderGranularity
  pdcch_pdu->precoderGranularity = coreset->precoderGranularity;
}

int nr_get_pucch_resource(NR_ControlResourceSet_t *coreset,
                          NR_PUCCH_Config_t *pucch_Config,
                          int CCEIndex) {
  int r_pucch = -1;
  if(pucch_Config == NULL) {
    int n_rb,rb_offset;
    get_coreset_rballoc(coreset->frequencyDomainResources.buf,&n_rb,&rb_offset);
    const uint16_t N_cce = n_rb * coreset->duration / NR_NB_REG_PER_CCE;
    const int delta_PRI=0;
    r_pucch = ((CCEIndex<<1)/N_cce)+(delta_PRI<<1);
  }
  return r_pucch;
}

// This function configures pucch pdu fapi structure
void nr_configure_pucch(nfapi_nr_pucch_pdu_t *pucch_pdu,
                        NR_ServingCellConfigCommon_t *scc,
                        NR_UE_info_t *UE,
                        uint8_t pucch_resource,
                        uint16_t O_csi,
                        uint16_t O_ack,
                        uint8_t O_sr,
                        int r_pucch,
                        const NR_beam_info_t *beam_info,
                        uint16_t ant_port_start,
                        uint16_t *ssi,
                        uint16_t num_ant)
{
  NR_PUCCH_Resource_t *pucchres;
  NR_PUCCH_FormatConfig_t *pucchfmt;
  NR_UE_UL_BWP_t *current_BWP = &UE->current_UL_BWP;

  int res_found = 0;

  pucch_pdu->bit_len_harq = O_ack;
  pucch_pdu->bit_len_csi_part1 = O_csi;

  uint16_t O_uci = O_csi + O_ack;

  NR_PUSCH_Config_t *pusch_Config = current_BWP->pusch_Config;

  long *pusch_id = pusch_Config ? pusch_Config->dataScramblingIdentityPUSCH : NULL;

  long *id0 = NULL;
  if (pusch_Config &&
      pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA != NULL &&
      pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup->transformPrecodingDisabled != NULL)
    id0 = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup->transformPrecodingDisabled->scramblingID0;
  else if (pusch_Config && pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB != NULL &&
           pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->transformPrecodingDisabled != NULL)
    id0 = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->transformPrecodingDisabled->scramblingID0;
  else id0 = scc->physCellId;

  NR_PUCCH_ConfigCommon_t *pucch_ConfigCommon = current_BWP->pucch_ConfigCommon;

  // hop flags and hopping id are valid for any BWP
  switch (pucch_ConfigCommon->pucch_GroupHopping){
  case 0 :
    // if neither, both disabled
    pucch_pdu->group_hop_flag = 0;
    pucch_pdu->sequence_hop_flag = 0;
    break;
  case 1 :
    // if enable, group enabled
    pucch_pdu->group_hop_flag = 1;
    pucch_pdu->sequence_hop_flag = 0;
    break;
  case 2 :
    // if disable, sequence disabled
    pucch_pdu->group_hop_flag = 0;
    pucch_pdu->sequence_hop_flag = 1;
    break;
  default:
    AssertFatal(1==0,"Group hopping flag %ld undefined (0,1,2) \n", pucch_ConfigCommon->pucch_GroupHopping);
  }

  if (pucch_ConfigCommon->hoppingId != NULL)
    pucch_pdu->hopping_id = *pucch_ConfigCommon->hoppingId;
  else
    pucch_pdu->hopping_id = *scc->physCellId;

  pucch_pdu->bwp_size  = current_BWP->BWPSize;
  pucch_pdu->bwp_start = current_BWP->BWPStart;
  pucch_pdu->subcarrier_spacing = current_BWP->scs;
  pucch_pdu->cyclic_prefix = (current_BWP->cyclicprefix==NULL) ? 0 : *current_BWP->cyclicprefix;

  NR_PUCCH_Config_t *pucch_Config = current_BWP->pucch_Config;
  if (r_pucch < 0 || pucch_Config) {
    LOG_D(NR_MAC, "pucch_acknak: Filling dedicated configuration for PUCCH\n");

    int resource_id = get_pucch_resourceid(pucch_Config, O_uci, pucch_resource);

    AssertFatal(pucch_Config->resourceToAddModList!=NULL,
                "PUCCH resourceToAddModList is null\n");

    int n_list = pucch_Config->resourceToAddModList->list.count;
    AssertFatal(n_list > 0, "PUCCH resourceToAddModList is empty\n");

    // going through the list of PUCCH resources to find the one indexed by resource_id
    for (int i = 0; i < n_list; i++) {
      pucchres = pucch_Config->resourceToAddModList->list.array[i];
      if (pucchres->pucch_ResourceId == resource_id) {
        res_found = 1;
        pucch_pdu->prb_start = pucchres->startingPRB;
        pucch_pdu->rnti = UE->rnti;
        // FIXME why there is only one frequency hopping flag
        // what about inter slot frequency hopping?
        pucch_pdu->freq_hop_flag = pucchres->intraSlotFrequencyHopping ? 1 : 0;
        pucch_pdu->second_hop_prb = pucchres->secondHopPRB!= NULL ?  *pucchres->secondHopPRB : 0;
        switch(pucchres->format.present) {
          case NR_PUCCH_Resource__format_PR_format0 :
            pucch_pdu->format_type = 0;
            pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format0->initialCyclicShift;
            pucch_pdu->nr_of_symbols = pucchres->format.choice.format0->nrofSymbols;
            pucch_pdu->start_symbol_index = pucchres->format.choice.format0->startingSymbolIndex;
            pucch_pdu->sr_flag = O_sr;
            pucch_pdu->prb_size = 1;
            break;
          case NR_PUCCH_Resource__format_PR_format1 :
            pucch_pdu->format_type = 1;
            pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format1->initialCyclicShift;
            pucch_pdu->nr_of_symbols = pucchres->format.choice.format1->nrofSymbols;
            pucch_pdu->start_symbol_index = pucchres->format.choice.format1->startingSymbolIndex;
            pucch_pdu->time_domain_occ_idx = pucchres->format.choice.format1->timeDomainOCC;
            pucch_pdu->sr_flag = O_sr;
            pucch_pdu->prb_size = 1;
            break;
          case NR_PUCCH_Resource__format_PR_format2 :
            pucch_pdu->format_type = 2;
            pucch_pdu->sr_flag = O_sr;
            pucch_pdu->nr_of_symbols = pucchres->format.choice.format2->nrofSymbols;
            pucch_pdu->start_symbol_index = pucchres->format.choice.format2->startingSymbolIndex;
            pucch_pdu->data_scrambling_id = pusch_id ? *pusch_id : *scc->physCellId;
            pucch_pdu->dmrs_scrambling_id = id0 ? *id0 : *scc->physCellId;
            pucch_pdu->prb_size = compute_pucch_prb_size(pucchres->format.choice.format2->nrofPRBs,
                                                         O_csi,
                                                         O_ack,
                                                         O_sr,
                                                         pucch_Config->format2->choice.setup->maxCodeRate,
                                                         2,
                                                         pucchres->format.choice.format2->nrofSymbols,
                                                         8);
            pucch_pdu->bit_len_csi_part1 = O_csi;
            break;
          case NR_PUCCH_Resource__format_PR_format3 :
            pucch_pdu->format_type = 3;
            pucch_pdu->nr_of_symbols = pucchres->format.choice.format3->nrofSymbols;
            pucch_pdu->start_symbol_index = pucchres->format.choice.format3->startingSymbolIndex;
            pucch_pdu->data_scrambling_id = pusch_id ? *pusch_id : *scc->physCellId;
            if (pucch_Config->format3 == NULL) {
              pucch_pdu->pi_2bpsk = 0;
              pucch_pdu->add_dmrs_flag = 0;
            }
            else {
              pucchfmt = pucch_Config->format3->choice.setup;
              pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK ? 1 : 0;
              pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS ? 1 : 0;
            }
            int f3_dmrs_symbols = get_f3_dmrs_symbols(pucchres, pucch_Config);
            pucch_pdu->prb_size = compute_pucch_prb_size(pucchres->format.choice.format3->nrofPRBs,
                                                         O_csi,
                                                         O_ack,
                                                         O_sr,
                                                         pucch_Config->format3->choice.setup->maxCodeRate,
                                                         2 - pucch_pdu->pi_2bpsk,
                                                         pucchres->format.choice.format3->nrofSymbols - f3_dmrs_symbols,
                                                         12);
            pucch_pdu->bit_len_csi_part1 = O_csi;
            break;
          case NR_PUCCH_Resource__format_PR_format4 :
            pucch_pdu->format_type = 4;
            pucch_pdu->nr_of_symbols = pucchres->format.choice.format4->nrofSymbols;
            pucch_pdu->start_symbol_index = pucchres->format.choice.format4->startingSymbolIndex;
            pucch_pdu->pre_dft_occ_len = pucchres->format.choice.format4->occ_Length;
            pucch_pdu->pre_dft_occ_idx = pucchres->format.choice.format4->occ_Index;
            pucch_pdu->data_scrambling_id = pusch_id!= NULL ? *pusch_id : *scc->physCellId;
            if (pucch_Config->format3 == NULL) {
              pucch_pdu->pi_2bpsk = 0;
              pucch_pdu->add_dmrs_flag = 0;
            }
            else {
              pucchfmt = pucch_Config->format3->choice.setup;
              pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK != NULL;
              pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS != NULL;
            }
            pucch_pdu->bit_len_csi_part1 = O_csi;
            break;
          default :
            AssertFatal(1==0,"Undefined PUCCH format \n");
        }
      }
    }
    AssertFatal(res_found == 1, "No PUCCH resource found corresponding to id %d\n", resource_id);
    LOG_D(NR_MAC,
          "Configure pucch: pucch_pdu->format_type %d pucch_pdu->bit_len_harq %d, pucch->pdu->bit_len_csi %d\n",
          pucch_pdu->format_type,
          pucch_pdu->bit_len_harq,
          pucch_pdu->bit_len_csi_part1);
  } else { // this is the default PUCCH configuration, PUCCH format 0 or 1
    LOG_D(NR_MAC,
          "pucch_acknak: Filling default PUCCH configuration from Tables (r_pucch %d, pucch_Config %p)\n",
          r_pucch,
          pucch_Config);
    int rsetindex = *pucch_ConfigCommon->pucch_ResourceCommon;
    int prb_start, second_hop_prb, nr_of_symb, start_symb;
    set_r_pucch_parms(rsetindex,
                      r_pucch,
                      pucch_pdu->bwp_size,
                      &prb_start,
                      &second_hop_prb,
                      &nr_of_symb,
                      &start_symb);

    pucch_pdu->prb_start = prb_start;
    pucch_pdu->rnti = UE->rnti;
    pucch_pdu->freq_hop_flag = 1;
    pucch_pdu->second_hop_prb = second_hop_prb;
    pucch_pdu->format_type = default_pucch_fmt[rsetindex];
    int initial_cyclic_shift_idx = (r_pucch % 8) % default_pucch_csset[rsetindex];
    if (rsetindex == 3 || rsetindex == 7 || rsetindex == 11)
      pucch_pdu->initial_cyclic_shift = initial_cyclic_shift_idx * 6;
    else if (rsetindex == 1 || rsetindex == 2)
      pucch_pdu->initial_cyclic_shift = initial_cyclic_shift_idx * 4;
    else
      pucch_pdu->initial_cyclic_shift = initial_cyclic_shift_idx * 3;
    pucch_pdu->nr_of_symbols = nr_of_symb;
    pucch_pdu->start_symbol_index = start_symb;
    if (pucch_pdu->format_type == 1)
      pucch_pdu->time_domain_occ_idx = 0; // check this!!
    pucch_pdu->sr_flag = O_sr;
    pucch_pdu->prb_size=1;
  }
  // Beamforming
  pucch_pdu->beamforming.num_prgs = 1;
  pucch_pdu->beamforming.prg_size = pucch_pdu->prb_size;
  pucch_pdu->beamforming.dig_bf_interface = 1;
  const uint16_t fapi_beam = convert_to_fapi_beam(UE->UE_beam_index, beam_info);
  pucch_pdu->beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx = fapi_beam;
  pucch_pdu->param_v4.numSpatialStreamIndices = num_ant;
  for (int i = 0; i < num_ant; i++)
    pucch_pdu->param_v4.spatialStreamIndices[i] = ssi[ant_port_start + i];
}

void set_r_pucch_parms(int rsetindex,
                       int r_pucch,
                       int bwp_size,
                       int *prb_start,
                       int *second_hop_prb,
                       int *nr_of_symbols,
                       int *start_symbol_index)
{
  // procedure described in 38.213 section 9.2.1
  int prboffset = (r_pucch % 8) / default_pucch_csset[rsetindex];
  int offset1 = default_pucch_prboffset[rsetindex] + prboffset;
  int offset2 = bwp_size - 1 - default_pucch_prboffset[rsetindex] - prboffset;
  *prb_start = (r_pucch >> 3) == 0 ? offset1 : offset2;
  *second_hop_prb = (r_pucch >> 3) == 0 ? offset2 : offset1;
  *nr_of_symbols = default_pucch_numbsymb[rsetindex];
  *start_symbol_index = default_pucch_firstsymb[rsetindex];
}

static void prepare_dci_X1(const NR_UE_ServingCell_Info_t *servingCellInfo,
                           const NR_UE_DL_BWP_t *current_BWP,
                           const NR_ControlResourceSet_t *coreset,
                           dci_pdu_rel15_t *dci_pdu_rel15,
                           int srs_request,
                           nr_dci_format_t format)
{
  const NR_PDSCH_Config_t *pdsch_Config = current_BWP ? current_BWP->pdsch_Config : NULL;

  switch(format) {
    case NR_UL_DCI_FORMAT_0_1:
      // format indicator
      dci_pdu_rel15->format_indicator = 0;
      // carrier indicator
      if (servingCellInfo->crossCarrierSchedulingConfig != NULL)
        AssertFatal(1==0,"Cross Carrier Scheduling Config currently not supported\n");
      // supplementary uplink
      if (servingCellInfo->supplementaryUplink != NULL)
        AssertFatal(1==0,"Supplementary Uplink currently not supported\n");
      // SRS request
      dci_pdu_rel15->srs_request.val = srs_request;
      dci_pdu_rel15->ulsch_indicator = 1;
      break;
    case NR_DL_DCI_FORMAT_1_1:
      // format indicator
      dci_pdu_rel15->format_indicator = 1;
      // carrier indicator
      if (servingCellInfo->crossCarrierSchedulingConfig != NULL)
        AssertFatal(1==0,"Cross Carrier Scheduling Config currently not supported\n");
      //vrb to prb mapping
      if (pdsch_Config->vrb_ToPRB_Interleaver==NULL)
        dci_pdu_rel15->vrb_to_prb_mapping.val = 0;
      else
        dci_pdu_rel15->vrb_to_prb_mapping.val = 1;
      //bundling size indicator
      if (pdsch_Config->prb_BundlingType.present == NR_PDSCH_Config__prb_BundlingType_PR_dynamicBundling)
        AssertFatal(1==0,"Dynamic PRB bundling type currently not supported\n");
      //rate matching indicator
      uint16_t msb = (pdsch_Config->rateMatchPatternGroup1==NULL)?0:1;
      uint16_t lsb = (pdsch_Config->rateMatchPatternGroup2==NULL)?0:1;
      dci_pdu_rel15->rate_matching_indicator.val = lsb | (msb<<1);
      // aperiodic ZP CSI-RS trigger
      if (pdsch_Config->aperiodic_ZP_CSI_RS_ResourceSetsToAddModList != NULL)
        AssertFatal(1==0,"Aperiodic ZP CSI-RS currently not supported\n");
      // transmission configuration indication
      if (coreset->tci_PresentInDCI != NULL)
        AssertFatal(1==0,"TCI in DCI currently not supported\n");
      //srs resource set
      if (servingCellInfo->carrierSwitching) {
        if (servingCellInfo->carrierSwitching->srs_TPC_PDCCH_Group) {
          switch (servingCellInfo->carrierSwitching->srs_TPC_PDCCH_Group->present) {
            case NR_SRS_CarrierSwitching__srs_TPC_PDCCH_Group_PR_NOTHING:
              dci_pdu_rel15->srs_request.val = 0;
              break;
            case NR_SRS_CarrierSwitching__srs_TPC_PDCCH_Group_PR_typeA:
              AssertFatal(1==0,"SRS TPC PRCCH group type A currently not supported\n");
              break;
            case NR_SRS_CarrierSwitching__srs_TPC_PDCCH_Group_PR_typeB:
              AssertFatal(1==0,"SRS TPC PRCCH group type B currently not supported\n");
              break;
          }
        } else
          dci_pdu_rel15->srs_request.val = 0;
      } else
        dci_pdu_rel15->srs_request.val = 0;
    // CBGTI and CBGFI
      if (servingCellInfo->pdsch_CGB_Transmission)
        AssertFatal(1 == 0, "CBG transmission currently not supported\n");
      break;
  default :
    AssertFatal(1==0,"Prepare dci currently only implemented for 1_1 and 0_1 \n");
  }
}

void fill_dci_pdu_rel15(const NR_UE_ServingCell_Info_t *servingCellInfo,
                        const NR_UE_DL_BWP_t *current_DL_BWP,
                        const NR_UE_UL_BWP_t *current_UL_BWP,
                        nfapi_nr_dl_dci_pdu_t *pdcch_dci_pdu,
                        dci_pdu_rel15_t *dci_pdu_rel15,
                        int dci_format,
                        int rnti_type,
                        int srs_request,
                        NR_SearchSpace_t *ss,
                        NR_ControlResourceSet_t *coreset,
                        long pdsch_HARQ_ACK_Codebook,
                        uint16_t cset0_bwp_size)
{
  uint8_t fsize = 0, pos = 0;
  uint64_t *dci_pdu = (uint64_t *)pdcch_dci_pdu->Payload;
  *dci_pdu = 0;
  uint16_t alt_size = 0;
  uint16_t N_RB;
  if(current_DL_BWP) {
    N_RB = get_rb_bwp_dci(dci_format,
                          ss->searchSpaceType->present,
                          cset0_bwp_size,
                          current_UL_BWP->BWPSize,
                          current_DL_BWP->BWPSize,
                          servingCellInfo->initial_ul_BWPSize,
                          servingCellInfo->initial_dl_BWPSize);

    // computing alternative size for padding
    dci_pdu_rel15_t temp_pdu;
    if(dci_format == NR_DL_DCI_FORMAT_1_0)
      alt_size = nr_dci_size(current_DL_BWP,
                             current_UL_BWP,
                             servingCellInfo,
                             pdsch_HARQ_ACK_Codebook,
                             &temp_pdu,
                             NR_UL_DCI_FORMAT_0_0,
                             rnti_type,
                             coreset,
                             ss->searchSpaceType->present,
                             cset0_bwp_size,
                             0);

    if(dci_format == NR_UL_DCI_FORMAT_0_0)
      alt_size = nr_dci_size(current_DL_BWP,
                             current_UL_BWP,
                             servingCellInfo,
                             pdsch_HARQ_ACK_Codebook,
                             &temp_pdu,
                             NR_DL_DCI_FORMAT_1_0,
                             rnti_type,
                             coreset,
                             ss->searchSpaceType->present,
                             cset0_bwp_size,
                             0);

  }
  else
    N_RB = cset0_bwp_size;

  int dci_size = nr_dci_size(current_DL_BWP,
                             current_UL_BWP,
                             servingCellInfo,
                             pdsch_HARQ_ACK_Codebook,
                             dci_pdu_rel15,
                             dci_format,
                             rnti_type,
                             coreset,
                             ss->searchSpaceType->present,
                             cset0_bwp_size,
                             alt_size);

  if (dci_size == 0)
    return;
  pdcch_dci_pdu->PayloadSizeBits = dci_size;
  AssertFatal(dci_size <= 64, "DCI sizes above 64 bits not yet supported");
  if (dci_format == NR_DL_DCI_FORMAT_1_1 || dci_format == NR_UL_DCI_FORMAT_0_1)
    prepare_dci_X1(servingCellInfo, current_DL_BWP, coreset, dci_pdu_rel15, srs_request, dci_format);

  /// Payload generation
  switch (dci_format) {
  case NR_DL_DCI_FORMAT_1_0:
    switch (rnti_type) {
      case TYPE_RA_RNTI_:
        // Freq domain assignment
        fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
        pos = fsize;
        *dci_pdu |= (((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val & ((1 << fsize) - 1)) << (dci_size - pos));
        LOG_D(NR_MAC,
              "RA_RNTI, size %d frequency-domain assignment %d (%d bits) N_RB_BWP %d=> %d (0x%lx)\n",
              dci_size,
              dci_pdu_rel15->frequency_domain_assignment.val,
              fsize,
              N_RB,
              dci_size - pos,
              *dci_pdu);
        // Time domain assignment
        pos += 4;
        *dci_pdu |= (((uint64_t)dci_pdu_rel15->time_domain_assignment.val & 0xf) << (dci_size - pos));
        LOG_D(NR_MAC,
              "time-domain assignment %d  (4 bits)=> %d (0x%lx)\n",
              dci_pdu_rel15->time_domain_assignment.val,
              dci_size - pos,
              *dci_pdu);
        // VRB to PRB mapping
        pos++;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->vrb_to_prb_mapping.val & 0x1) << (dci_size - pos);
        LOG_D(NR_MAC,
              "vrb to prb mapping %d  (1 bits)=> %d (0x%lx)\n",
              dci_pdu_rel15->vrb_to_prb_mapping.val,
              dci_size - pos,
              *dci_pdu);
        // MCS
        pos += 5;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
        LOG_D(NR_MAC, "mcs %d  (5 bits)=> %d (0x%lx)\n", dci_pdu_rel15->mcs, dci_size - pos, *dci_pdu);
        // TB scaling
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->tb_scaling & 0x3) << (dci_size - pos);
        LOG_D(NR_MAC, "tb_scaling %d  (2 bits)=> %d (0x%lx)\n", dci_pdu_rel15->tb_scaling, dci_size - pos, *dci_pdu);
        break;

      case TYPE_C_RNTI_:
        // indicating a DL DCI format 1bit
        pos++;
        *dci_pdu |= ((uint64_t)1) << (dci_size - pos);
        LOG_D(NR_MAC,
              "DCI1_0 (size %d): Format indicator %d (%d bits) N_RB_BWP %d => %d (0x%lx)\n",
              dci_size,
              dci_pdu_rel15->format_indicator,
              1,
              N_RB,
              dci_size - pos,
              *dci_pdu);
        // Freq domain assignment (275rb >> fsize = 16)
        fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
        pos += fsize;
        *dci_pdu |= (((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val & ((1 << fsize) - 1)) << (dci_size - pos));
        LOG_D(NR_MAC,
              "Freq domain assignment %d (%d bits)=> %d (0x%lx)\n",
              dci_pdu_rel15->frequency_domain_assignment.val,
              fsize,
              dci_size - pos,
              *dci_pdu);
        uint16_t is_ra = 1;
        for (int i = 0; i < fsize; i++) {
          if (!((dci_pdu_rel15->frequency_domain_assignment.val >> i) & 1)) {
            is_ra = 0;
            break;
          }
        }
        if (is_ra) { // fsize are all 1  38.212 p86
          // ra_preamble_index 6 bits
          pos += 6;
          *dci_pdu |= ((dci_pdu_rel15->ra_preamble_index & 0x3f) << (dci_size - pos));
          // UL/SUL indicator  1 bit
          pos++;
          *dci_pdu |= (dci_pdu_rel15->ul_sul_indicator.val & 1) << (dci_size - pos);
          // SS/PBCH index  6 bits
          pos += 6;
          *dci_pdu |= ((dci_pdu_rel15->ss_pbch_index & 0x3f) << (dci_size - pos));
          //  prach_mask_index  4 bits
          pos += 4;
          *dci_pdu |= ((dci_pdu_rel15->prach_mask_index & 0xf) << (dci_size - pos));
        } else {
          // Time domain assignment 4bit
          pos += 4;
          *dci_pdu |= ((dci_pdu_rel15->time_domain_assignment.val & 0xf) << (dci_size - pos));
          LOG_D(NR_MAC,
                "Time domain assignment %d (%d bits)=> %d (0x%lx)\n",
                dci_pdu_rel15->time_domain_assignment.val,
                4,
                dci_size - pos,
                *dci_pdu);
          // VRB to PRB mapping  1bit
          pos++;
          *dci_pdu |= (dci_pdu_rel15->vrb_to_prb_mapping.val & 1) << (dci_size - pos);
          LOG_D(NR_MAC,
                "VRB to PRB %d (%d bits)=> %d (0x%lx)\n",
                dci_pdu_rel15->vrb_to_prb_mapping.val,
                1,
                dci_size - pos,
                *dci_pdu);
          // MCS 5bit  //bit over 32, so dci_pdu ++
          pos += 5;
          *dci_pdu |= (dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
          LOG_D(NR_MAC, "MCS %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->mcs, 5, dci_size - pos, *dci_pdu);
          // New data indicator 1bit
          pos++;
          *dci_pdu |= (dci_pdu_rel15->ndi & 1) << (dci_size - pos);
          LOG_D(NR_MAC, "NDI %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->ndi, 1, dci_size - pos, *dci_pdu);
          // Redundancy version  2bit
          pos += 2;
          *dci_pdu |= (dci_pdu_rel15->rv & 0x3) << (dci_size - pos);
          LOG_D(NR_MAC, "RV %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->rv, 2, dci_size - pos, *dci_pdu);
          // HARQ process number  4bit/5bit
          fsize = dci_pdu_rel15->harq_pid.nbits;
          pos += fsize;
          *dci_pdu |= ((dci_pdu_rel15->harq_pid.val & ((1 << fsize) - 1)) << (dci_size - pos));
          LOG_D(NR_MAC, "HARQ_PID %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->harq_pid.val, fsize, dci_size - pos, *dci_pdu);
          // Downlink assignment index  2bit
          pos += 2;
          *dci_pdu |= ((dci_pdu_rel15->dai[0].val & 3) << (dci_size - pos));
          LOG_D(NR_MAC, "DAI %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->dai[0].val, 2, dci_size - pos, *dci_pdu);
          // TPC command for scheduled PUCCH  2bit
          pos += 2;
          *dci_pdu |= ((dci_pdu_rel15->tpc & 3) << (dci_size - pos));
          LOG_D(NR_MAC, "TPC %d (%d bits)=> %d (0x%lx)\n", dci_pdu_rel15->tpc, 2, dci_size - pos, *dci_pdu);
          // PUCCH resource indicator  3bit
          pos += 3;
          *dci_pdu |= ((dci_pdu_rel15->pucch_resource_indicator & 0x7) << (dci_size - pos));
          LOG_D(NR_MAC,
                "PUCCH RI %d (%d bits)=> %d (0x%lx)\n",
                dci_pdu_rel15->pucch_resource_indicator,
                3,
                dci_size - pos,
                *dci_pdu);
          // PDSCH-to-HARQ_feedback timing indicator 3bit
          pos += 3;
          *dci_pdu |= ((dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val & 0x7) << (dci_size - pos));
          LOG_D(NR_MAC,
                "PDSCH to HARQ TI %d (%d bits)=> %d (0x%lx)\n",
                dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val,
                3,
                dci_size - pos,
                *dci_pdu);
        } // end else
        break;

      case TYPE_P_RNTI_:
        /* TS 38.212 §7.3.1.2.1: pack MSB-first field groups. Per-bit placement shifts every
         * field after SMI/short messages by one bit. */
        *dci_pdu |= (dci_pdu_rel15->short_messages_indicator & 0x3) * (1ULL << (dci_size - pos - 2));
        pos += 2;
        *dci_pdu |= (dci_pdu_rel15->short_messages & 0xff) * (1ULL << (dci_size - pos - 8));
        pos += 8;
        fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
        *dci_pdu |= (dci_pdu_rel15->frequency_domain_assignment.val & ((1U << fsize) - 1)) * (1ULL << (dci_size - pos - fsize));
        pos += fsize;
        *dci_pdu |= (dci_pdu_rel15->time_domain_assignment.val & 0xf) * (1ULL << (dci_size - pos - 4));
        pos += 4;
        *dci_pdu |= (dci_pdu_rel15->vrb_to_prb_mapping.val & 1) * (1ULL << (dci_size - pos - 1));
        pos += 1;
        *dci_pdu |= (dci_pdu_rel15->mcs & 0x1f) * (1ULL << (dci_size - pos - 5));
        pos += 5;
        *dci_pdu |= (dci_pdu_rel15->tb_scaling & 0x3) * (1ULL << (dci_size - pos - 2));
        pos += 2;
        LOG_I(NR_MAC,
              "P-RNTI DCI TX packed: dci_size=%d payload=0x%lx SMI=%u short_msg=0x%02x FDA=%u TDA=%u mcs=%u tb_scaling=%u\n",
              dci_size,
              *dci_pdu,
              dci_pdu_rel15->short_messages_indicator,
              dci_pdu_rel15->short_messages,
              dci_pdu_rel15->frequency_domain_assignment.val,
              dci_pdu_rel15->time_domain_assignment.val,
              dci_pdu_rel15->mcs,
              dci_pdu_rel15->tb_scaling);
        break;

      case TYPE_SI_RNTI_:
        pos = 1;
        // Freq domain assignment 0-16 bit
        fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
        LOG_D(NR_MAC, "fsize = %i\n", fsize);
        for (int i = 0; i < fsize; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val >> (fsize - i - 1)) & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_domain_assignment.val = %i\n", dci_pdu_rel15->frequency_domain_assignment.val);
        // Time domain assignment 4 bit
        for (int i = 0; i < 4; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->time_domain_assignment.val >> (3 - i)) & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->time_domain_assignment.val = %i\n", dci_pdu_rel15->time_domain_assignment.val);
        // VRB to PRB mapping 1 bit
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->vrb_to_prb_mapping.val & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->vrb_to_prb_mapping.val = %i\n", dci_pdu_rel15->vrb_to_prb_mapping.val);
        // MCS 5bit  //bit over 32, so dci_pdu ++
        for (int i = 0; i < 5; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->mcs >> (4 - i)) & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->mcs = %i\n", dci_pdu_rel15->mcs);
        // Redundancy version  2bit
        for (int i = 0; i < 2; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->rv >> (1 - i)) & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->rv = %i\n", dci_pdu_rel15->rv);
        // System information indicator 1bit
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->system_info_indicator & 1) << (dci_size - pos++);
        LOG_D(NR_MAC, "dci_pdu_rel15->system_info_indicator = %i\n", dci_pdu_rel15->system_info_indicator);
        break;

      case TYPE_TC_RNTI_:
        pos = 1;
        // indicating a DL DCI format 1bit
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->format_indicator & 1) << (dci_size - pos++);
        // Freq domain assignment 0-16 bit
        fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
        for (int i = 0; i < fsize; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val >> (fsize - i - 1)) & 1) << (dci_size - pos++);
        // Time domain assignment 4 bit
        for (int i = 0; i < 4; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->time_domain_assignment.val >> (3 - i)) & 1) << (dci_size - pos++);
        // VRB to PRB mapping 1 bit
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->vrb_to_prb_mapping.val & 1) << (dci_size - pos++);
        // MCS 5bit  //bit over 32, so dci_pdu ++
        for (int i = 0; i < 5; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->mcs >> (4 - i)) & 1) << (dci_size - pos++);
        // New data indicator 1bit
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi & 1) << (dci_size - pos++);
        // Redundancy version  2bit
        for (int i = 0; i < 2; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->rv >> (1 - i)) & 1) << (dci_size - pos++);
        // HARQ process number  4bit/5bit
        fsize = dci_pdu_rel15->harq_pid.nbits;
        for (int i = 0; i < fsize; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->harq_pid.val >> (fsize - i - 1)) & 1) << (dci_size - pos++);
        // Downlink assignment index – 2 bits
        for (int i = 0; i < 2; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->dai[0].val >> (1 - i)) & 1) << (dci_size - pos++);
        // TPC command for scheduled PUCCH – 2 bits
        for (int i = 0; i < 2; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->tpc >> (1 - i)) & 1) << (dci_size - pos++);
        // PUCCH resource indicator – 3 bits
        for (int i = 0; i < 3; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->pucch_resource_indicator >> (2 - i)) & 1) << (dci_size - pos++);
        // PDSCH-to-HARQ_feedback timing indicator – 3 bits
        for (int i = 0; i < 3; i++)
          *dci_pdu |= (((uint64_t)dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val >> (2 - i)) & 1) << (dci_size - pos++);

        LOG_D(NR_MAC, "N_RB = %i\n", N_RB);
        LOG_D(NR_MAC, "dci_size = %i\n", dci_size);
        LOG_D(NR_MAC, "fsize = %i\n", fsize);
        LOG_D(NR_MAC, "dci_pdu_rel15->format_indicator = %i\n", dci_pdu_rel15->format_indicator);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_domain_assignment.val = %i\n", dci_pdu_rel15->frequency_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->time_domain_assignment.val = %i\n", dci_pdu_rel15->time_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->vrb_to_prb_mapping.val = %i\n", dci_pdu_rel15->vrb_to_prb_mapping.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->mcs = %i\n", dci_pdu_rel15->mcs);
        LOG_D(NR_MAC, "dci_pdu_rel15->rv = %i\n", dci_pdu_rel15->rv);
        LOG_D(NR_MAC, "dci_pdu_rel15->harq_pid = %i\n", dci_pdu_rel15->harq_pid.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->dai[0].val = %i\n", dci_pdu_rel15->dai[0].val);
        LOG_D(NR_MAC, "dci_pdu_rel15->tpc = %i\n", dci_pdu_rel15->tpc);
        LOG_D(NR_MAC,
              "dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val = %i\n",
              dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val);

        break;
    }
    break;

  case NR_UL_DCI_FORMAT_0_0:
    switch (rnti_type) {
      case TYPE_C_RNTI_:
        LOG_D(NR_MAC,
              "Filling format 0_0 DCI for CRNTI (size %d bits, format ind %d)\n",
              dci_size,
              dci_pdu_rel15->format_indicator);
        // indicating a UL DCI format 1bit
        pos = 1;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->format_indicator & 1) << (dci_size - pos);
        // Freq domain assignment  max 16 bit
        fsize = dci_pdu_rel15->frequency_domain_assignment.nbits;
        pos += fsize;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val & ((1 << fsize) - 1)) << (dci_size - pos);
        // Time domain assignment 4bit
        pos += 4;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->time_domain_assignment.val & ((1 << 4) - 1)) << (dci_size - pos);
        // Frequency hopping flag – 1 bit
        pos++;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_hopping_flag.val & 1) << (dci_size - pos);
        // MCS  5 bit
        pos += 5;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
        // New data indicator 1bit
        pos++;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi & 1) << (dci_size - pos);
        // Redundancy version  2bit
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->rv & 0x3) << (dci_size - pos);
        // HARQ process number  4bit/5bit
        fsize = dci_pdu_rel15->harq_pid.nbits;
        pos += fsize;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->harq_pid.val & ((1 << fsize) - 1)) << (dci_size - pos);
        // TPC command for scheduled PUSCH – 2 bits
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->tpc & 0x3) << (dci_size - pos);
        // Padding bits
        for (int a = pos; a < dci_size; a++)
          *dci_pdu |= ((uint64_t)dci_pdu_rel15->padding & 1) << (dci_size - pos++);
        // UL/SUL indicator – 1 bit
        /* commented for now (RK): need to get this from BWP descriptor
        if (cfg->pucch_config.pucch_GroupHopping.value)
          *dci_pdu |=
        ((uint64_t)dci_pdu_rel15->ul_sul_indicator.val&1)<<(dci_size-pos++);
          */

        LOG_D(NR_MAC, "N_RB = %i\n", N_RB);
        LOG_D(NR_MAC, "dci_size = %i\n", dci_size);
        LOG_D(NR_MAC, "fsize = %i\n", fsize);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_domain_assignment.val = %i\n", dci_pdu_rel15->frequency_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->time_domain_assignment.val = %i\n", dci_pdu_rel15->time_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_hopping_flag.val = %i\n", dci_pdu_rel15->frequency_hopping_flag.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->mcs = %i\n", dci_pdu_rel15->mcs);
        LOG_D(NR_MAC, "dci_pdu_rel15->ndi = %i\n", dci_pdu_rel15->ndi);
        LOG_D(NR_MAC, "dci_pdu_rel15->rv = %i\n", dci_pdu_rel15->rv);
        LOG_D(NR_MAC, "dci_pdu_rel15->harq_pid = %i\n", dci_pdu_rel15->harq_pid.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->tpc = %i\n", dci_pdu_rel15->tpc);
        LOG_D(NR_MAC, "dci_pdu_rel15->padding = %i\n", dci_pdu_rel15->padding);
        break;

      case TYPE_TC_RNTI_:
        // indicating a UL DCI format 1bit
        pos = 1;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->format_indicator & 1) << (dci_size - pos);
        // Freq domain assignment  max 16 bit
        fsize = dci_pdu_rel15->frequency_domain_assignment.nbits;
        pos += fsize;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val & ((1 << fsize) - 1)) << (dci_size - pos);
        // Time domain assignment 4bit
        pos += 4;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->time_domain_assignment.val & ((1 << 4) - 1)) << (dci_size - pos);
        // Frequency hopping flag – 1 bit
        pos++;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_hopping_flag.val & 1) << (dci_size - pos);
        // MCS  5 bit
        pos += 5;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
        // New data indicator 1bit
        pos++;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi & 1) << (dci_size - pos);
        // Redundancy version  2bit
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->rv & 0x3) << (dci_size - pos);
        // HARQ process number  4bit/5bit
        fsize = dci_pdu_rel15->harq_pid.nbits;
        pos += fsize;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->harq_pid.val & ((1 << fsize) - 1)) << (dci_size - pos);
        // Padding bits
        for (int a = pos; a < dci_size; a++)
          *dci_pdu |= ((uint64_t)dci_pdu_rel15->padding & 1) << (dci_size - pos++);
        // UL/SUL indicator – 1 bit
        /* commented for now (RK): need to get this from BWP descriptor
        if (cfg->pucch_config.pucch_GroupHopping.value)
          *dci_pdu |=
        ((uint64_t)dci_pdu_rel15->ul_sul_indicator.val&1)<<(dci_size-pos++);
          */
        LOG_D(NR_MAC, "N_RB = %i\n", N_RB);
        LOG_D(NR_MAC, "dci_size = %i\n", dci_size);
        LOG_D(NR_MAC, "fsize = %i\n", fsize);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_domain_assignment.val = %i\n", dci_pdu_rel15->frequency_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->time_domain_assignment.val = %i\n", dci_pdu_rel15->time_domain_assignment.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->frequency_hopping_flag.val = %i\n", dci_pdu_rel15->frequency_hopping_flag.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->mcs = %i\n", dci_pdu_rel15->mcs);
        LOG_D(NR_MAC, "dci_pdu_rel15->ndi = %i\n", dci_pdu_rel15->ndi);
        LOG_D(NR_MAC, "dci_pdu_rel15->rv = %i\n", dci_pdu_rel15->rv);
        LOG_D(NR_MAC, "dci_pdu_rel15->harq_pid = %i\n", dci_pdu_rel15->harq_pid.val);
        LOG_D(NR_MAC, "dci_pdu_rel15->tpc = %i\n", dci_pdu_rel15->tpc);
        LOG_D(NR_MAC, "dci_pdu_rel15->padding = %i\n", dci_pdu_rel15->padding);

        break;
    }
    break;

  case NR_UL_DCI_FORMAT_0_1:
    switch (rnti_type) {
      case TYPE_C_RNTI_:
        LOG_D(NR_MAC, "Filling NR_UL_DCI_FORMAT_0_1 size %d format indicator %d\n", dci_size, dci_pdu_rel15->format_indicator);
        // Indicating a DL DCI format 1bit
        pos = 1;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->format_indicator & 0x1) << (dci_size - pos);
        // Carrier indicator
        pos += dci_pdu_rel15->carrier_indicator.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->carrier_indicator.val & ((1 << dci_pdu_rel15->carrier_indicator.nbits) - 1))
                    << (dci_size - pos);
        // UL/SUL Indicator
        pos += dci_pdu_rel15->ul_sul_indicator.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ul_sul_indicator.val & ((1 << dci_pdu_rel15->ul_sul_indicator.nbits) - 1))
                    << (dci_size - pos);
        // BWP indicator
        pos += dci_pdu_rel15->bwp_indicator.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->bwp_indicator.val & ((1 << dci_pdu_rel15->bwp_indicator.nbits) - 1))
                    << (dci_size - pos);
        // Frequency domain resource assignment
        pos += dci_pdu_rel15->frequency_domain_assignment.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val
                     & ((1 << dci_pdu_rel15->frequency_domain_assignment.nbits) - 1))
                    << (dci_size - pos);
        // Time domain resource assignment
        pos += dci_pdu_rel15->time_domain_assignment.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->time_domain_assignment.val & ((1 << dci_pdu_rel15->time_domain_assignment.nbits) - 1))
                    << (dci_size - pos);
        // Frequency hopping
        pos += dci_pdu_rel15->frequency_hopping_flag.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_hopping_flag.val & ((1 << dci_pdu_rel15->frequency_hopping_flag.nbits) - 1))
                    << (dci_size - pos);
        // MCS 5bit
        pos += 5;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
        // New data indicator 1bit
        pos += 1;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi & 0x1) << (dci_size - pos);
        // Redundancy version  2bit
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->rv & 0x3) << (dci_size - pos);
        // HARQ process number  4bit/5bit
        fsize = dci_pdu_rel15->harq_pid.nbits;
        pos += fsize;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->harq_pid.val & ((1 << fsize) - 1)) << (dci_size - pos);
        // 1st Downlink assignment index
        pos += dci_pdu_rel15->dai[0].nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->dai[0].val & ((1 << dci_pdu_rel15->dai[0].nbits) - 1)) << (dci_size - pos);
        // 2nd Downlink assignment index
        pos += dci_pdu_rel15->dai[1].nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->dai[1].val & ((1 << dci_pdu_rel15->dai[1].nbits) - 1)) << (dci_size - pos);
        // TPC command for scheduled PUSCH  2bit
        pos += 2;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->tpc & 0x3) << (dci_size - pos);
        // SRS resource indicator
        pos += dci_pdu_rel15->srs_resource_indicator.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->srs_resource_indicator.val & ((1 << dci_pdu_rel15->srs_resource_indicator.nbits) - 1))
                    << (dci_size - pos);
        // Precoding info and n. of layers
        pos += dci_pdu_rel15->precoding_information.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->precoding_information.val & ((1 << dci_pdu_rel15->precoding_information.nbits) - 1))
                    << (dci_size - pos);
        // Antenna ports
        pos += dci_pdu_rel15->antenna_ports.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->antenna_ports.val & ((1 << dci_pdu_rel15->antenna_ports.nbits) - 1))
                    << (dci_size - pos);
        // SRS request
        pos += dci_pdu_rel15->srs_request.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->srs_request.val & ((1 << dci_pdu_rel15->srs_request.nbits) - 1)) << (dci_size - pos);
        // CSI request
        pos += dci_pdu_rel15->csi_request.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->csi_request.val & ((1 << dci_pdu_rel15->csi_request.nbits) - 1)) << (dci_size - pos);
        // CBG transmission information
        pos += dci_pdu_rel15->cbgti.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->cbgti.val & ((1 << dci_pdu_rel15->cbgti.nbits) - 1)) << (dci_size - pos);
        // PTRS DMRS association
        pos += dci_pdu_rel15->ptrs_dmrs_association.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ptrs_dmrs_association.val & ((1 << dci_pdu_rel15->ptrs_dmrs_association.nbits) - 1))
                    << (dci_size - pos);
        // Beta offset indicator
        pos += dci_pdu_rel15->beta_offset_indicator.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->beta_offset_indicator.val & ((1 << dci_pdu_rel15->beta_offset_indicator.nbits) - 1))
                    << (dci_size - pos);
        // DMRS sequence initialization
        pos += dci_pdu_rel15->dmrs_sequence_initialization.nbits;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->dmrs_sequence_initialization.val
                     & ((1 << dci_pdu_rel15->dmrs_sequence_initialization.nbits) - 1))
                    << (dci_size - pos);
        // UL-SCH indicator
        pos += 1;
        *dci_pdu |= ((uint64_t)dci_pdu_rel15->ulsch_indicator & 0x1) << (dci_size - pos);

#ifdef DEBUG_DCI
        LOG_I(NR_MAC,"============= NR_UL_DCI_FORMAT_0_1 =============\n");
        LOG_I(NR_MAC,"dci_size = %i\n", dci_size);
        LOG_I(NR_MAC,"dci_pdu_rel15->format_indicator = %i\n", dci_pdu_rel15->format_indicator);
        LOG_I(NR_MAC,"dci_pdu_rel15->carrier_indicator.val = %i\n", dci_pdu_rel15->carrier_indicator.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->ul_sul_indicator.val = %i\n", dci_pdu_rel15->ul_sul_indicator.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->bwp_indicator.val = %i\n", dci_pdu_rel15->bwp_indicator.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->frequency_domain_assignment.val = %i\n", dci_pdu_rel15->frequency_domain_assignment.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->time_domain_assignment.val = %i\n", dci_pdu_rel15->time_domain_assignment.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->frequency_hopping_flag.val = %i\n", dci_pdu_rel15->frequency_hopping_flag.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->mcs = %i\n", dci_pdu_rel15->mcs);
        LOG_I(NR_MAC,"dci_pdu_rel15->ndi = %i\n", dci_pdu_rel15->ndi);
        LOG_I(NR_MAC,"dci_pdu_rel15->rv= %i\n", dci_pdu_rel15->rv);
        LOG_I(NR_MAC,"dci_pdu_rel15->harq_pid = %i\n", dci_pdu_rel15->harq_pid);
        LOG_I(NR_MAC,"dci_pdu_rel15->dai[0].val = %i\n", dci_pdu_rel15->dai[0].val);
        LOG_I(NR_MAC,"dci_pdu_rel15->dai[1].val = %i\n", dci_pdu_rel15->dai[1].val);
        LOG_I(NR_MAC,"dci_pdu_rel15->tpc = %i\n", dci_pdu_rel15->tpc);
        LOG_I(NR_MAC,"dci_pdu_rel15->srs_resource_indicator.val = %i\n", dci_pdu_rel15->srs_resource_indicator.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->precoding_information.val = %i\n", dci_pdu_rel15->precoding_information.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->antenna_ports.val = %i\n", dci_pdu_rel15->antenna_ports.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->srs_request.val = %i\n", dci_pdu_rel15->srs_request.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->csi_request.val = %i\n", dci_pdu_rel15->csi_request.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->cbgti.val = %i\n", dci_pdu_rel15->cbgti.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->ptrs_dmrs_association.val = %i\n", dci_pdu_rel15->ptrs_dmrs_association.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->beta_offset_indicator.val = %i\n", dci_pdu_rel15->beta_offset_indicator.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->dmrs_sequence_initialization.val = %i\n", dci_pdu_rel15->dmrs_sequence_initialization.val);
        LOG_I(NR_MAC,"dci_pdu_rel15->ulsch_indicator = %i\n", dci_pdu_rel15->ulsch_indicator);
#endif

        break;
    }
    break;

  case NR_DL_DCI_FORMAT_1_1:
    // Indicating a DL DCI format 1bit
    LOG_D(NR_MAC,"Filling Format 1_1 DCI of size %d\n",dci_size);
    pos = 1;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->format_indicator & 0x1) << (dci_size - pos);
    // Carrier indicator
    pos += dci_pdu_rel15->carrier_indicator.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->carrier_indicator.val & ((1 << dci_pdu_rel15->carrier_indicator.nbits) - 1)) << (dci_size - pos);
    // BWP indicator
    pos += dci_pdu_rel15->bwp_indicator.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->bwp_indicator.val & ((1 << dci_pdu_rel15->bwp_indicator.nbits) - 1)) << (dci_size - pos);
    // Frequency domain resource assignment
    pos += dci_pdu_rel15->frequency_domain_assignment.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->frequency_domain_assignment.val & ((1 << dci_pdu_rel15->frequency_domain_assignment.nbits) - 1)) << (dci_size - pos);
    // Time domain resource assignment
    pos += dci_pdu_rel15->time_domain_assignment.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->time_domain_assignment.val & ((1 << dci_pdu_rel15->time_domain_assignment.nbits) - 1)) << (dci_size - pos);
    // VRB-to-PRB mapping
    pos += dci_pdu_rel15->vrb_to_prb_mapping.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->vrb_to_prb_mapping.val & ((1 << dci_pdu_rel15->vrb_to_prb_mapping.nbits) - 1)) << (dci_size - pos);
    // PRB bundling size indicator
    pos += dci_pdu_rel15->prb_bundling_size_indicator.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->prb_bundling_size_indicator.val & ((1 << dci_pdu_rel15->prb_bundling_size_indicator.nbits) - 1)) << (dci_size - pos);
    // Rate matching indicator
    pos += dci_pdu_rel15->rate_matching_indicator.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->rate_matching_indicator.val & ((1 << dci_pdu_rel15->rate_matching_indicator.nbits) - 1)) << (dci_size - pos);
    // ZP CSI-RS trigger
    pos += dci_pdu_rel15->zp_csi_rs_trigger.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->zp_csi_rs_trigger.val & ((1 << dci_pdu_rel15->zp_csi_rs_trigger.nbits) - 1)) << (dci_size - pos);
    // TB1
    // MCS 5bit
    pos += 5;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs & 0x1f) << (dci_size - pos);
    // New data indicator 1bit
    pos += 1;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi & 0x1) << (dci_size - pos);
    // Redundancy version  2bit
    pos += 2;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->rv & 0x3) << (dci_size - pos);
    // TB2
    // MCS 5bit
    pos += dci_pdu_rel15->mcs2.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->mcs2.val & ((1 << dci_pdu_rel15->mcs2.nbits) - 1)) << (dci_size - pos);
    // New data indicator 1bit
    pos += dci_pdu_rel15->ndi2.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->ndi2.val & ((1 << dci_pdu_rel15->ndi2.nbits) - 1)) << (dci_size - pos);
    // Redundancy version  2bit
    pos += dci_pdu_rel15->rv2.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->rv2.val & ((1 << dci_pdu_rel15->rv2.nbits) - 1)) << (dci_size - pos);
    // HARQ process number  4bit/5bit
    fsize = dci_pdu_rel15->harq_pid.nbits;
    pos += fsize;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->harq_pid.val & ((1 << fsize) - 1)) << (dci_size - pos);
    // Downlink assignment index
    pos += dci_pdu_rel15->dai[0].nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->dai[0].val & ((1 << dci_pdu_rel15->dai[0].nbits) - 1)) << (dci_size - pos);
    // TPC command for scheduled PUCCH  2bit
    pos += 2;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->tpc & 0x3) << (dci_size - pos);
    // PUCCH resource indicator  3bit
    pos += 3;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->pucch_resource_indicator & 0x7) << (dci_size - pos);
    // PDSCH-to-HARQ_feedback timing indicator
    pos += dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val & ((1 << dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.nbits) - 1)) << (dci_size - pos);
    // Antenna ports
    pos += dci_pdu_rel15->antenna_ports.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->antenna_ports.val & ((1 << dci_pdu_rel15->antenna_ports.nbits) - 1)) << (dci_size - pos);
    // TCI
    pos += dci_pdu_rel15->transmission_configuration_indication.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->transmission_configuration_indication.val & ((1 << dci_pdu_rel15->transmission_configuration_indication.nbits) - 1)) << (dci_size - pos);
    // SRS request
    pos += dci_pdu_rel15->srs_request.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->srs_request.val & ((1 << dci_pdu_rel15->srs_request.nbits) - 1)) << (dci_size - pos);
    // CBG transmission information
    pos += dci_pdu_rel15->cbgti.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->cbgti.val & ((1 << dci_pdu_rel15->cbgti.nbits) - 1)) << (dci_size - pos);
    // CBG flushing out information
    pos += dci_pdu_rel15->cbgfi.nbits;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->cbgfi.val & ((1 << dci_pdu_rel15->cbgfi.nbits) - 1)) << (dci_size - pos);
    // DMRS sequence init
    pos += 1;
    *dci_pdu |= ((uint64_t)dci_pdu_rel15->dmrs_sequence_initialization.val & 0x1) << (dci_size - pos);
  }
  LOG_D(NR_MAC, "DCI has %d bits and the payload is %lx\n", dci_size, *dci_pdu);
}

int get_spf(nfapi_nr_config_request_scf_t *cfg) {

  int mu = cfg->ssb_config.scs_common.value;
  AssertFatal(mu>=0&&mu<4,"Illegal scs %d\n",mu);

  return(10 * (1<<mu));
}

int to_absslot(nfapi_nr_config_request_scf_t *cfg,int frame,int slot) {

  return(get_spf(cfg)*frame) + slot;

}

int extract_startSymbol(int startSymbolAndLength) {
  int tmp = startSymbolAndLength/14;
  int tmp2 = startSymbolAndLength%14;

  if (tmp > 0 && tmp < (14-tmp2)) return(tmp2);
  else                            return(13-tmp2);
}

int extract_length(int startSymbolAndLength) {
  int tmp = startSymbolAndLength/14;
  int tmp2 = startSymbolAndLength%14;

  if (tmp > 0 && tmp < (14-tmp2)) return(tmp);
  else                            return(15-tmp2);
}

/*
 * Create a new NR_list
 */
void create_nr_list(NR_list_t *list, int len)
{
  list->head = -1;
  list->next = malloc(len * sizeof(*list->next));
  AssertFatal(list->next, "cannot malloc() memory for NR_list_t->next\n");
  for (int i = 0; i < len; ++i)
    list->next[i] = -1;
  list->tail = -1;
  list->len = len;
}

/*
 * Resize an NR_list
 */
void resize_nr_list(NR_list_t *list, int new_len)
{
  if (new_len == list->len)
    return;
  if (new_len > list->len) {
    /* list->head remains */
    const int old_len = list->len;
    int* n = realloc(list->next, new_len * sizeof(*list->next));
    AssertFatal(n, "cannot realloc() memory for NR_list_t->next\n");
    list->next = n;
    for (int i = old_len; i < new_len; ++i)
      list->next[i] = -1;
    /* list->tail remains */
    list->len = new_len;
  } else { /* new_len < len */
    AssertFatal(list->head < new_len, "shortened list head out of index %d (new len %d)\n", list->head, new_len);
    AssertFatal(list->tail < new_len, "shortened list tail out of index %d (new len %d)\n", list->head, new_len);
    for (int i = 0; i < list->len; ++i)
      AssertFatal(list->next[i] < new_len, "shortened list entry out of index %d (new len %d)\n", list->next[i], new_len);
    /* list->head remains */
    int *n = realloc(list->next, new_len * sizeof(*list->next));
    AssertFatal(n, "cannot realloc() memory for NR_list_t->next\n");
    list->next = n;
    /* list->tail remains */
    list->len = new_len;
  }
}

/*
 * Destroy an NR_list
 */
void destroy_nr_list(NR_list_t *list)
{
  free(list->next);
}

/*
 * Add an ID to an NR_list at the end, traversing the whole list. Note:
 * add_tail_nr_list() is a faster alternative, but this implementation ensures
 * we do not add an existing ID.
 */
void add_nr_list(NR_list_t *listP, int id)
{
  int *cur = &listP->head;
  while (*cur >= 0) {
    AssertFatal(*cur != id, "id %d already in NR_UE_list!\n", id);
    cur = &listP->next[*cur];
  }
  *cur = id;
  if (listP->next[id] < 0)
    listP->tail = id;
}

/*
 * Remove an ID from an NR_list
 */
void remove_nr_list(NR_list_t *listP, int id)
{
  int *cur = &listP->head;
  int *prev = &listP->head;
  while (*cur != -1 && *cur != id) {
    prev = cur;
    cur = &listP->next[*cur];
  }
  AssertFatal(*cur != -1, "ID %d not found in UE_list\n", id);
  int *next = &listP->next[*cur];
  *cur = listP->next[*cur];
  *next = -1;
  listP->tail = *prev >= 0 && listP->next[*prev] >= 0 ? listP->tail : *prev;
}

/*
 * Add an ID to the tail of the NR_list in O(1). Note that there is
 * corresponding remove_tail_nr_list(), as we cannot set the tail backwards and
 * therefore need to go through the whole list (use remove_nr_list())
 */
void add_tail_nr_list(NR_list_t *listP, int id)
{
  int *last = listP->tail < 0 ? &listP->head : &listP->next[listP->tail];
  *last = id;
  listP->next[id] = -1;
  listP->tail = id;
}

/*
 * Add an ID to the front of the NR_list in O(1)
 */
void add_front_nr_list(NR_list_t *listP, int id)
{
  const int ohead = listP->head;
  listP->head = id;
  listP->next[id] = ohead;
  if (listP->tail < 0)
    listP->tail = id;
}

/*
 * Remove an ID from the front of the NR_list in O(1)
 */
void remove_front_nr_list(NR_list_t *listP)
{
  AssertFatal(listP->head >= 0, "Nothing to remove\n");
  const int ohead = listP->head;
  listP->head = listP->next[ohead];
  listP->next[ohead] = -1;
  if (listP->head < 0)
    listP->tail = -1;
}

NR_UE_info_t *find_connected_nr_ue(NR_UEs_t *UEs, rnti_t rntiP)
{
  void *data;
  hashtable_rc_t rc = hashtable_get(&UEs->connected_ues, rntiP, &data);
  if (rc != HASH_TABLE_OK)
    return NULL;
  return data;
}

size_t get_num_connected_ue(NR_UEs_t *UEs)
{
  int num = hashtable_num_entries(&UEs->connected_ues);
  DevAssert(num == seq_arr_size(&UEs->connected_ues_list));
  return num;
}

NR_UE_info_t **get_periodic_ue(periodic_ue_sched_t *p, int ul_idx, int index)
{
  return &p->list[ul_idx * p->max_ue_per_slot + index];
}

static void reset_periodic_info(const NR_UE_info_t *UE, periodic_ue_sched_t *periodic_config, int ue_periodic_offset)
{
  if (periodic_config->list && ue_periodic_offset >= 0) {
    for (int i = 0; i < periodic_config->max_ue_per_slot; i++) {
      if (*get_periodic_ue(periodic_config, ue_periodic_offset, i) != UE)
        continue;
      *get_periodic_ue(periodic_config, ue_periodic_offset, i) = NULL;
    }
  }
}

static int set_periodic_info(NR_UE_info_t *UE, int ue_offset, periodic_ue_sched_t *periodic_config, int idx)
{
  bool reset = false;
  if (ue_offset != -1) {
    if (ue_offset == idx) {
      for (int i = 0; i < periodic_config->max_ue_per_slot; i++) {
        if (*get_periodic_ue(periodic_config, idx, i) == UE)
          return ue_offset; // info already set for this UE
      }
      reset = true;
    } else
      reset = true;
  }
  if (reset)
    reset_periodic_info(UE, periodic_config, ue_offset); // need to re-configure
  for (int i = 0; i < periodic_config->max_ue_per_slot; i++) {
    if (*get_periodic_ue(periodic_config, idx, i) != NULL)
      continue;
    *get_periodic_ue(periodic_config, idx, i) = UE;
    return idx;
  }
  return -1;
}

void delete_nr_ue_data(void *p)
{
  NR_UE_info_t *UE = p;
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->CellGroup);
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->reconfigCellGroup);
  ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->capability);
  reset_periodic_info(UE, &UE->pcell->periodic_srs_config, UE->sched_srs.periodic_sched.periodic_offset);
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  seq_arr_free(&sched_ctrl->lc_config, NULL);
  destroy_nr_list(&sched_ctrl->available_dl_harq);
  destroy_nr_list(&sched_ctrl->feedback_dl_harq);
  destroy_nr_list(&sched_ctrl->retrans_dl_harq);
  destroy_nr_list(&sched_ctrl->available_ul_harq);
  destroy_nr_list(&sched_ctrl->feedback_ul_harq);
  destroy_nr_list(&sched_ctrl->retrans_ul_harq);
  for (int i = 0; i < NR_MAX_HARQ_PROCESSES; ++i)
    free_transportBlock_buffer(&sched_ctrl->harq_processes[i].transportBlock);
  free_sched_pucch_list(sched_ctrl);
  free_and_zero(UE->ra);
  free(UE);
}

#define TB_SINGLE_LAYER (32 * 1024)
uint8_t *allocate_transportBlock_buffer(byte_array_t *tb, uint32_t needed)
{
  DevAssert(needed > 0);
  if (tb->buf != NULL && needed <= tb->len)
    return tb->buf; // nothing to do, current is enough

  uint32_t size = TB_SINGLE_LAYER;
  while (needed > size)
    size *= 2;
  LOG_D(NR_MAC, "allocating new TB block of size %d\n", size);
  free(tb->buf);
  tb->buf = aligned_alloc(4, size); // important: FAPI use u32*
  tb->len = size;
  return tb->buf;
}

void free_transportBlock_buffer(byte_array_t *tb)
{
  free_byte_array(*tb);
}

static void set_max_fb_time(NR_UE_UL_BWP_t *UL_BWP)
{
  UL_BWP->max_fb_time = 8; // default value
  // take the maximum in dl_DataToUL_ACK list
  if (UL_BWP->pucch_Config) {
    const struct NR_PUCCH_Config__dl_DataToUL_ACK *fb_times = UL_BWP->pucch_Config->dl_DataToUL_ACK;
    for (int i = 0; i < fb_times->list.count; i++) {
      if(*fb_times->list.array[i] > UL_BWP->max_fb_time)
        UL_BWP->max_fb_time = *fb_times->list.array[i];
    }
  }
}

void reset_sched_ctrl(NR_UE_sched_ctrl_t *sched_ctrl)
{
  sched_ctrl->srs_feedback.ul_ri = 0;
  sched_ctrl->srs_feedback.tpmi = 0;
  sched_ctrl->srs_feedback.sri = 0;
}

int get_dlbw_tbslbrm(int scc_bwpsize, const NR_ServingCellConfig_t *servingCellConfig)
{
  int bw = scc_bwpsize;
  if (servingCellConfig) {
    if (servingCellConfig->downlinkBWP_ToAddModList) {
      const struct NR_ServingCellConfig__downlinkBWP_ToAddModList *BWP_list = servingCellConfig->downlinkBWP_ToAddModList;
      for (int i = 0; i < BWP_list->list.count; i++) {
        NR_BWP_t genericParameters = BWP_list->list.array[i]->bwp_Common->genericParameters;
        int curr_bw = NRRIV2BW(genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
        if (curr_bw > bw)
          bw = curr_bw;
      }
    }
  }
  return bw;
}

int get_ulbw_tbslbrm(int scc_bwpsize, const NR_ServingCellConfig_t *servingCellConfig)
{
  int bw = scc_bwpsize;
  if (servingCellConfig) {
    if (servingCellConfig->uplinkConfig && servingCellConfig->uplinkConfig->uplinkBWP_ToAddModList) {
      const struct NR_UplinkConfig__uplinkBWP_ToAddModList *BWP_list = servingCellConfig->uplinkConfig->uplinkBWP_ToAddModList;
      for (int i = 0; i < BWP_list->list.count; i++) {
        NR_BWP_t genericParameters = BWP_list->list.array[i]->bwp_Common->genericParameters;
        int curr_bw = NRRIV2BW(genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
        if (curr_bw > bw)
          bw = curr_bw;
      }
    }
  }
  return bw;
}

static void set_sched_pucch_list(NR_UE_sched_ctrl_t *sched_ctrl,
                                 const NR_UE_UL_BWP_t *ul_bwp,
                                 const NR_ServingCellConfigCommon_t *scc,
                                 const frame_structure_t *fs)
{
  const int NTN_gNB_Koffset = get_NTN_Koffset(scc);
  const int n_ul_slots_period = get_ul_slots_per_period(fs);

  // PUCCH list size is given by the number of UL slots in the PUCCH period
  // the length PUCCH period is determined by max_fb_time since we may need to prepare PUCCH for ACK/NACK max_fb_time slots ahead
  const int list_size = n_ul_slots_period << ceil_log2_u32((ul_bwp->max_fb_time + NTN_gNB_Koffset) / fs->numb_slots_period + 1);

  if (!sched_ctrl->sched_pucch) {
    sched_ctrl->sched_pucch = calloc_or_fail(list_size, sizeof(*sched_ctrl->sched_pucch));
    sched_ctrl->sched_pucch_size = list_size;
  } else if (list_size > sched_ctrl->sched_pucch_size) {
    sched_ctrl->sched_pucch = realloc(sched_ctrl->sched_pucch, list_size * sizeof(*sched_ctrl->sched_pucch));
    for (int i = sched_ctrl->sched_pucch_size; i < list_size; i++) {
      NR_sched_pucch_t *curr_pucch = &sched_ctrl->sched_pucch[i];
      memset(curr_pucch, 0, sizeof(*curr_pucch));
    }
    sched_ctrl->sched_pucch_size = list_size;
  }
}

static void create_dl_harq_list(NR_UE_sched_ctrl_t *sched_ctrl, const NR_UE_ServingCell_Info_t *sc_info, bool dci_00_10)
{
  int nrofHARQ = get_nrofHARQ_ProcessesForPDSCH(sc_info);
  // limit the number of HARQ PIDs to 16 in case of common search space
  if (dci_00_10 && nrofHARQ > 16)
    nrofHARQ = 16;
  // add all available DL HARQ processes for this UE
  AssertFatal(sched_ctrl->available_dl_harq.len == sched_ctrl->feedback_dl_harq.len
              && sched_ctrl->available_dl_harq.len == sched_ctrl->retrans_dl_harq.len,
              "HARQ lists have different lengths (%d/%d/%d)\n",
              sched_ctrl->available_dl_harq.len,
              sched_ctrl->feedback_dl_harq.len,
              sched_ctrl->retrans_dl_harq.len);
  if (sched_ctrl->available_dl_harq.len == 0) {
    create_nr_list(&sched_ctrl->available_dl_harq, nrofHARQ);
    for (int harq = 0; harq < nrofHARQ; harq++)
      add_tail_nr_list(&sched_ctrl->available_dl_harq, harq);
    create_nr_list(&sched_ctrl->feedback_dl_harq, nrofHARQ);
    create_nr_list(&sched_ctrl->retrans_dl_harq, nrofHARQ);
  } else if (sched_ctrl->available_dl_harq.len == nrofHARQ) {
    LOG_D(NR_MAC, "nrofHARQ %d already configured\n", nrofHARQ);
  } else {
    const int old_nrofHARQ = sched_ctrl->available_dl_harq.len;
    AssertFatal(nrofHARQ > old_nrofHARQ,
                "cannot resize HARQ list to be smaller (nrofHARQ %d, old_nrofHARQ %d)\n",
                nrofHARQ, old_nrofHARQ);
    resize_nr_list(&sched_ctrl->available_dl_harq, nrofHARQ);
    for (int harq = old_nrofHARQ; harq < nrofHARQ; harq++)
      add_tail_nr_list(&sched_ctrl->available_dl_harq, harq);
    resize_nr_list(&sched_ctrl->feedback_dl_harq, nrofHARQ);
    resize_nr_list(&sched_ctrl->retrans_dl_harq, nrofHARQ);
  }
}

static void create_ul_harq_list(NR_UE_sched_ctrl_t *sched_ctrl, const NR_UE_ServingCell_Info_t *sc_info, bool dci_00_10)
{
  int nrofHARQ = get_nrofHARQ_ProcessesForPUSCH(sc_info);
  // limit the number of HARQ PIDs to 16 in case of common search space
  if (dci_00_10 && nrofHARQ > 16)
    nrofHARQ = 16;
  if (sched_ctrl->available_ul_harq.len == 0) {
    create_nr_list(&sched_ctrl->available_ul_harq, nrofHARQ);
    for (int harq = 0; harq < nrofHARQ; harq++)
      add_tail_nr_list(&sched_ctrl->available_ul_harq, harq);
    create_nr_list(&sched_ctrl->feedback_ul_harq, nrofHARQ);
    create_nr_list(&sched_ctrl->retrans_ul_harq, nrofHARQ);
  } else if (sched_ctrl->available_ul_harq.len == nrofHARQ) {
    LOG_D(NR_MAC, "nrofHARQ %d already configured\n", nrofHARQ);
  } else {
    const int old_nrofHARQ = sched_ctrl->available_ul_harq.len;
    AssertFatal(nrofHARQ > old_nrofHARQ,
                "cannot resize HARQ list to be smaller (nrofHARQ %d, old_nrofHARQ %d)\n",
                nrofHARQ, old_nrofHARQ);
    resize_nr_list(&sched_ctrl->available_ul_harq, nrofHARQ);
    for (int harq = old_nrofHARQ; harq < nrofHARQ; harq++)
      add_tail_nr_list(&sched_ctrl->available_ul_harq, harq);
    resize_nr_list(&sched_ctrl->feedback_ul_harq, nrofHARQ);
    resize_nr_list(&sched_ctrl->retrans_ul_harq, nrofHARQ);
  }
}

void reset_sc_info(NR_UE_ServingCell_Info_t *sc_info)
{
  sc_info->csi_MeasConfig = NULL;
  sc_info->nrofHARQ_ProcessesForPDSCH = NULL;
  sc_info->maxMIMO_Layers_PDSCH = NULL;
  sc_info->downlinkHARQ_FeedbackDisabled_r17 = NULL;
  sc_info->pdsch_CGB_Transmission = NULL;
  sc_info->nrofHARQ_ProcessesForPDSCH_v1700 = NULL;
  sc_info->crossCarrierSchedulingConfig = NULL;
  sc_info->supplementaryUplink = NULL;
  sc_info->carrierSwitching = NULL;
  sc_info->maxMIMO_Layers_PUSCH = NULL;
  sc_info->rateMatching_PUSCH = NULL;
  sc_info->pusch_CGB_Transmission = NULL;
  sc_info->nrofHARQ_ProcessesForPUSCH_r17 = NULL;
}

static void configure_sched_srs(nr_cell_sched_t *cell, const NR_SRS_Config_t *srs_config, NR_UE_info_t *UE)
{
  NR_sched_srs_t *sched_srs = &UE->sched_srs;
  nr_srs_type_t srs_type = cell->radio_config.do_SRS;
  if (!srs_config || srs_type == NO_SRS) {
    sched_srs->srs_resource = NULL;
    reset_periodic_info(UE, &cell->periodic_srs_config, sched_srs->periodic_sched.periodic_offset);
    sched_srs->periodic_sched.periodic_offset = -1;
    return;
  }

  NR_SRS_ResourceSet__resourceType_PR set_type = srs_type == PERIODIC_SRS ?
                                                 NR_SRS_ResourceSet__resourceType_PR_periodic :
                                                 NR_SRS_ResourceSet__resourceType_PR_aperiodic;

  NR_SRS_ResourceSet_t *srs_resource_set = NULL;
  for(int rs = 0; rs < srs_config->srs_ResourceSetToAddModList->list.count; rs++) {
    // Find resource set
    if (srs_config->srs_ResourceSetToAddModList->list.array[rs]->resourceType.present == set_type) {
      srs_resource_set = srs_config->srs_ResourceSetToAddModList->list.array[rs];
      break;
    }
  }
  AssertFatal(srs_resource_set, "Couldn't find %s SRS resource set\n", srs_type == PERIODIC_SRS ? "periodic" : "aperiodic");
  sched_srs->usage = srs_resource_set->usage;
  if (srs_type == APERIODIC_SRS) {
    struct NR_SRS_ResourceSet__resourceType__aperiodic *aperiodic = srs_resource_set->resourceType.choice.aperiodic;
    sched_srs->aperiodic_sched.aperiodic_slotOffset = aperiodic->slotOffset;
    sched_srs->aperiodic_sched.aperiodic_ResourceTrigger = aperiodic->aperiodicSRS_ResourceTrigger;
  }

  NR_SRS_Resource__resourceType_PR res_type = srs_type == PERIODIC_SRS ?
                                              NR_SRS_Resource__resourceType_PR_periodic :
                                              NR_SRS_Resource__resourceType_PR_aperiodic;

  NR_SRS_Resource_t *srs_resource = NULL;
  for (int r1 = 0; r1 < srs_resource_set->srs_ResourceIdList->list.count; r1++) {
    for (int r2 = 0; r2 < srs_config->srs_ResourceToAddModList->list.count; r2++) {
      if ((*srs_resource_set->srs_ResourceIdList->list.array[r1] ==
          srs_config->srs_ResourceToAddModList->list.array[r2]->srs_ResourceId) &&
          (srs_config->srs_ResourceToAddModList->list.array[r2]->resourceType.present == res_type))
        srs_resource = srs_config->srs_ResourceToAddModList->list.array[r2];
    }
  }
  AssertFatal(srs_resource, "Couldn't find %s SRS resource\n", srs_type == PERIODIC_SRS ? "periodic" : "aperiodic");
  sched_srs->srs_resource = srs_resource;
  if (srs_type == PERIODIC_SRS) {
    int slot_offset = get_nr_srs_offset(srs_resource->resourceType.choice.periodic->periodicityAndOffset_p);
    AssertFatal(slot_offset >= 0 && slot_offset < cell->periodic_srs_config.max_period,
                "Offset not compatible with periodicity for SRS\n");
    int idx = get_ul_period_idx_from_abs_slot(&cell->frame_structure, slot_offset, false, cell->periodic_srs_config.max_period);
    AssertFatal(idx >= 0, "SRS offset %d is not a valid UL slot\n", slot_offset);
    sched_srs->periodic_sched.periodic_offset = set_periodic_info(UE,
                                                                  sched_srs->periodic_sched.periodic_offset,
                                                                  &cell->periodic_srs_config,
                                                                  idx);
    AssertFatal(sched_srs->periodic_sched.periodic_offset >= 0, "Couldn't configure periodic information for SRS\n");
  }
}

// main function to configure parameters of current BWP
void configure_UE_BWP(nr_cell_sched_t *cell,
                      NR_ServingCellConfigCommon_t *scc,
                      NR_UE_info_t *UE,
                      bool is_RA,
                      int target_ss,
                      int dl_bwp_switch,
                      int ul_bwp_switch)
{
  NR_CellGroupConfig_t *CellGroup = UE->CellGroup;
  NR_UE_ServingCell_Info_t *sc_info = &UE->sc_info;
  NR_UE_DL_BWP_t *DL_BWP = &UE->current_DL_BWP;
  NR_UE_UL_BWP_t *UL_BWP = &UE->current_UL_BWP;

  NR_BWP_Downlink_t *dl_bwp = NULL;
  NR_BWP_Uplink_t *ul_bwp = NULL;
  NR_BWP_DownlinkDedicated_t *bwpd = NULL;
  NR_BWP_UplinkDedicated_t *ubwpd = NULL;
  // number of additional BWPs (excluding initial BWP)
  sc_info->n_dl_bwp = 0;
  sc_info->n_ul_bwp = 0;
  int old_dl_bwp_id = DL_BWP->bwp_id;
  int old_ul_bwp_id = UL_BWP->bwp_id;

  NR_ServingCellConfig_t *servingCellConfig = NULL;
  if (CellGroup && CellGroup->spCellConfig && CellGroup->spCellConfig->spCellConfigDedicated) {

    servingCellConfig  = CellGroup->spCellConfig->spCellConfigDedicated;

    if(dl_bwp_switch >= 0 && ul_bwp_switch >= 0) {
      AssertFatal(dl_bwp_switch == ul_bwp_switch, "Different UL and DL BWP not supported\n");
      DL_BWP->bwp_id = dl_bwp_switch;
      UL_BWP->bwp_id = ul_bwp_switch;
      UE->local_bwp_id = dl_bwp_switch; // assuming they are the same
    } else {
      DL_BWP->bwp_id = UE->local_bwp_id;
      UL_BWP->bwp_id = UE->local_bwp_id;
    }

    const struct NR_ServingCellConfig__downlinkBWP_ToAddModList *bwpList = servingCellConfig->downlinkBWP_ToAddModList;
    if(bwpList)
      sc_info->n_dl_bwp = 1;
    if (DL_BWP->bwp_id > 0) {
      dl_bwp = bwpList->list.array[0];
      AssertFatal(dl_bwp, "Couldn't find DLBWP corresponding to BWP ID %ld\n", DL_BWP->bwp_id);
    }

    const struct NR_UplinkConfig__uplinkBWP_ToAddModList *ubwpList = servingCellConfig->uplinkConfig->uplinkBWP_ToAddModList;
    if(ubwpList)
      sc_info->n_ul_bwp = 1;
    if (UL_BWP->bwp_id > 0) {
      ul_bwp = ubwpList->list.array[0];
      AssertFatal(ul_bwp, "Couldn't find ULBWP corresponding to BWP ID %ld\n", UL_BWP->bwp_id);
    }

    // selection of dedicated BWPs
    if(dl_bwp)
      bwpd = dl_bwp->bwp_Dedicated;
    else
      bwpd = servingCellConfig->initialDownlinkBWP;
    if(ul_bwp)
      ubwpd = ul_bwp->bwp_Dedicated;
    else
      ubwpd = servingCellConfig->uplinkConfig->initialUplinkBWP;

    DL_BWP->pdsch_Config = bwpd->pdsch_Config->choice.setup;
    UL_BWP->configuredGrantConfig = ubwpd->configuredGrantConfig ? ubwpd->configuredGrantConfig->choice.setup : NULL;
    UL_BWP->pusch_Config = ubwpd->pusch_Config->choice.setup;
    UL_BWP->pucch_Config = ubwpd->pucch_Config->choice.setup;
    UL_BWP->srs_Config = ubwpd->srs_Config ? ubwpd->srs_Config->choice.setup : NULL;
  } else {
    DL_BWP->bwp_id = 0;
    UL_BWP->bwp_id = 0;
    DL_BWP->pdsch_Config = NULL;
    UL_BWP->pusch_Config = NULL;
    UL_BWP->pucch_Config = NULL;
    UL_BWP->srs_Config = NULL;
    UL_BWP->configuredGrantConfig = NULL;
  }

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  configure_sched_srs(cell, UL_BWP->srs_Config, UE);

  // TDA lists
  if (DL_BWP->bwp_id > 0)
    DL_BWP->tdaList_Common = dl_bwp->bwp_Common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList;
  else
    DL_BWP->tdaList_Common = scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList;

  if(UL_BWP->bwp_id > 0)
    UL_BWP->tdaList_Common = ul_bwp->bwp_Common->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList;
  else
    UL_BWP->tdaList_Common = scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList;

  // setting generic parameters
  NR_BWP_t dl_genericParameters = (DL_BWP->bwp_id > 0 && dl_bwp) ?
    dl_bwp->bwp_Common->genericParameters:
    scc->downlinkConfigCommon->initialDownlinkBWP->genericParameters;

  DL_BWP->scs = dl_genericParameters.subcarrierSpacing;
  DL_BWP->cyclicprefix = dl_genericParameters.cyclicPrefix;
  DL_BWP->BWPSize = NRRIV2BW(dl_genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  DL_BWP->BWPStart = NRRIV2PRBOFFSET(dl_genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  sc_info->initial_dl_BWPSize =
      NRRIV2BW(scc->downlinkConfigCommon->initialDownlinkBWP->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  sc_info->initial_dl_BWPStart =
      NRRIV2PRBOFFSET(scc->downlinkConfigCommon->initialDownlinkBWP->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);

  NR_BWP_t ul_genericParameters = (UL_BWP->bwp_id > 0 && ul_bwp) ?
    ul_bwp->bwp_Common->genericParameters:
    scc->uplinkConfigCommon->initialUplinkBWP->genericParameters;

  UL_BWP->scs = ul_genericParameters.subcarrierSpacing;
  UL_BWP->cyclicprefix = ul_genericParameters.cyclicPrefix;
  UL_BWP->BWPSize = NRRIV2BW(ul_genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  UL_BWP->BWPStart = NRRIV2PRBOFFSET(ul_genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  sc_info->initial_ul_BWPSize =
      NRRIV2BW(scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);
  sc_info->initial_ul_BWPStart =
      NRRIV2PRBOFFSET(scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.locationAndBandwidth, MAX_BWP_SIZE);

  sc_info->dl_bw_tbslbrm = get_dlbw_tbslbrm(sc_info->initial_dl_BWPSize, servingCellConfig);
  sc_info->ul_bw_tbslbrm = get_ulbw_tbslbrm(sc_info->initial_ul_BWPSize, servingCellConfig);

  if (UL_BWP->bwp_id > 0) {
    UL_BWP->pucch_ConfigCommon = ul_bwp->bwp_Common->pucch_ConfigCommon ? ul_bwp->bwp_Common->pucch_ConfigCommon->choice.setup : NULL;
    UL_BWP->rach_ConfigCommon = ul_bwp->bwp_Common->rach_ConfigCommon ? ul_bwp->bwp_Common->rach_ConfigCommon->choice.setup : NULL;
  } else {
    UL_BWP->pucch_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup;
    UL_BWP->rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup;
  }

  // if PRACH occasions are not configured for the active UL BWP
  // switch the active UL BWP to BWP indicated by initialUplinkBWP
  // as described in 5.15 of 38.321
  if (is_RA && !UL_BWP->rach_ConfigCommon) {
    LOG_I(NR_MAC, "Cannot perform RA in current BWP, switching to initial BWP\n");
    UE->local_bwp_id = 0;
    /* Move the UE back to its dedicated BWP once RA/reconfiguration completes */
    UE->pending_bwp_restore = true;
    configure_UE_BWP(cell, scc, UE, is_RA, target_ss, 0, 0);
    return;
  }
  if (old_dl_bwp_id != DL_BWP->bwp_id)
    LOG_I(NR_MAC, "Switching to DL-BWP %li\n", DL_BWP->bwp_id);
  if (old_ul_bwp_id != UL_BWP->bwp_id)
    LOG_I(NR_MAC, "Switching to UL-BWP %li\n", UL_BWP->bwp_id);

  // Reset required fields in sched_ctrl (e.g. ul_ri and tpmi)
  reset_sched_ctrl(sched_ctrl);
  if(!is_RA) {
    if (servingCellConfig) {
      if (servingCellConfig->csi_MeasConfig) {
        sc_info->csi_MeasConfig = servingCellConfig->csi_MeasConfig->choice.setup;
        compute_csi_bitlen(sc_info->csi_MeasConfig, UE->csi_report_template);
      }
      if (servingCellConfig->pdsch_ServingCellConfig && servingCellConfig->pdsch_ServingCellConfig->choice.setup) {
        NR_PDSCH_ServingCellConfig_t *pdsch_servingcellconfig = servingCellConfig->pdsch_ServingCellConfig->choice.setup;
        sc_info->nrofHARQ_ProcessesForPDSCH = pdsch_servingcellconfig->nrofHARQ_ProcessesForPDSCH;
        if (pdsch_servingcellconfig->ext1)
          sc_info->maxMIMO_Layers_PDSCH = pdsch_servingcellconfig->ext1->maxMIMO_Layers;
        if (pdsch_servingcellconfig->ext3 &&
            pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17)
          sc_info->downlinkHARQ_FeedbackDisabled_r17 = &pdsch_servingcellconfig->ext3->downlinkHARQ_FeedbackDisabled_r17->choice.setup;
        if (pdsch_servingcellconfig->codeBlockGroupTransmission
            && pdsch_servingcellconfig->codeBlockGroupTransmission->choice.setup)
          sc_info->pdsch_CGB_Transmission = pdsch_servingcellconfig->codeBlockGroupTransmission->choice.setup;
        if (pdsch_servingcellconfig->ext3)
          sc_info->nrofHARQ_ProcessesForPDSCH_v1700 = pdsch_servingcellconfig->ext3->nrofHARQ_ProcessesForPDSCH_v1700;
      }
      sc_info->crossCarrierSchedulingConfig = servingCellConfig->crossCarrierSchedulingConfig;
      sc_info->supplementaryUplink = servingCellConfig->supplementaryUplink;
      if (servingCellConfig->uplinkConfig) {
        if (servingCellConfig->uplinkConfig->carrierSwitching)
          sc_info->carrierSwitching = servingCellConfig->uplinkConfig->carrierSwitching->choice.setup;
        if (servingCellConfig->uplinkConfig->pusch_ServingCellConfig
            && servingCellConfig->uplinkConfig->pusch_ServingCellConfig->choice.setup) {
          NR_PUSCH_ServingCellConfig_t *pusch_servingcellconfig =
              servingCellConfig->uplinkConfig->pusch_ServingCellConfig->choice.setup;
          if (pusch_servingcellconfig->ext1)
            sc_info->maxMIMO_Layers_PUSCH = pusch_servingcellconfig->ext1->maxMIMO_Layers;
          sc_info->rateMatching_PUSCH = pusch_servingcellconfig->rateMatching;
          if (pusch_servingcellconfig->codeBlockGroupTransmission
              && pusch_servingcellconfig->codeBlockGroupTransmission->choice.setup)
            sc_info->pusch_CGB_Transmission = pusch_servingcellconfig->codeBlockGroupTransmission->choice.setup;
          if (pusch_servingcellconfig->ext3)
            sc_info->nrofHARQ_ProcessesForPUSCH_r17 = pusch_servingcellconfig->ext3->nrofHARQ_ProcessesForPUSCH_r17;
        }
      }
    } else
      reset_sc_info(sc_info);

    if (CellGroup && CellGroup->physicalCellGroupConfig)
      UE->pdsch_HARQ_ACK_Codebook = CellGroup->physicalCellGroupConfig->pdsch_HARQ_ACK_Codebook;

    // setting PDCCH related structures for sched_ctrl
    sched_ctrl->search_space = get_searchspace(scc, bwpd, target_ss);
    sched_ctrl->coreset = get_coreset(cell, scc, bwpd, *sched_ctrl->search_space->controlResourceSetId);

    sched_ctrl->sched_pdcch = set_pdcch_structure(cell,
                                                  sched_ctrl->search_space,
                                                  sched_ctrl->coreset,
                                                  scc,
                                                  &dl_genericParameters,
                                                  cell->type0_PDCCH_CSS_config);

    // set DL DCI format
    DL_BWP->dci_format = (sched_ctrl->search_space->searchSpaceType &&
                         sched_ctrl->search_space->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_ue_Specific) ?
                         (sched_ctrl->search_space->searchSpaceType->choice.ue_Specific->dci_Formats == NR_SearchSpace__searchSpaceType__ue_Specific__dci_Formats_formats0_1_And_1_1 ?
                         NR_DL_DCI_FORMAT_1_1 : NR_DL_DCI_FORMAT_1_0) :
                         NR_DL_DCI_FORMAT_1_0;
    // set UL DCI format
    UL_BWP->dci_format = (sched_ctrl->search_space->searchSpaceType &&
                         sched_ctrl->search_space->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_ue_Specific) ?
                         (sched_ctrl->search_space->searchSpaceType->choice.ue_Specific->dci_Formats == NR_SearchSpace__searchSpaceType__ue_Specific__dci_Formats_formats0_1_And_1_1 ?
                         NR_UL_DCI_FORMAT_0_1 : NR_UL_DCI_FORMAT_0_0) :
                         NR_UL_DCI_FORMAT_0_0;

  } else {
    // setting PDCCH related structures for RA
    struct NR_PDCCH_ConfigCommon__commonSearchSpaceList *commonSearchSpaceList = NULL;
    NR_SearchSpaceId_t ra_SearchSpace = 0;
    if(dl_bwp) {
      commonSearchSpaceList = dl_bwp->bwp_Common->pdcch_ConfigCommon->choice.setup->commonSearchSpaceList;
      ra_SearchSpace = *dl_bwp->bwp_Common->pdcch_ConfigCommon->choice.setup->ra_SearchSpace;
    } else {
      commonSearchSpaceList = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->commonSearchSpaceList;
      ra_SearchSpace = *scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->ra_SearchSpace;
    }
    AssertFatal(commonSearchSpaceList->list.count > 0, "common SearchSpace list has 0 elements\n");
    for (int i = 0; i < commonSearchSpaceList->list.count; i++) {
      NR_SearchSpace_t *ss = commonSearchSpaceList->list.array[i];
      if (ss->searchSpaceId == ra_SearchSpace)
        sched_ctrl->search_space = ss;
    }
    AssertFatal(sched_ctrl->search_space != NULL, "SearchSpace cannot be null for RA\n");

    sched_ctrl->coreset = get_coreset(cell, scc, bwpd, *sched_ctrl->search_space->controlResourceSetId);
    int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
    sched_ctrl->sched_pdcch = set_pdcch_structure(cell,
                                                  sched_ctrl->search_space,
                                                  sched_ctrl->coreset,
                                                  scc,
                                                  &dl_genericParameters,
                                                  &cell->type0_PDCCH_CSS_config[ssb_index]);

    UL_BWP->dci_format = NR_UL_DCI_FORMAT_0_0;
    DL_BWP->dci_format = NR_DL_DCI_FORMAT_1_0;
  }

  bool format_00_10 = DL_BWP->dci_format == NR_DL_DCI_FORMAT_1_0; // if DL is 10 UL is surely 00
  create_dl_harq_list(sched_ctrl, sc_info, format_00_10);
  create_ul_harq_list(sched_ctrl, sc_info, format_00_10);

  set_max_fb_time(UL_BWP);
  set_sched_pucch_list(sched_ctrl, UL_BWP, scc, &cell->frame_structure);

  // Set MCS tables
  long *dl_mcs_Table = DL_BWP->pdsch_Config ? DL_BWP->pdsch_Config->mcs_Table : NULL;
  DL_BWP->mcsTableIdx = get_pdsch_mcs_table(dl_mcs_Table, DL_BWP->dci_format, TYPE_C_RNTI_, target_ss);

  // 0 precoding enabled 1 precoding disabled
  UL_BWP->transform_precoding = get_transformPrecoding(UL_BWP, UL_BWP->dci_format, 0);
  // Set uplink MCS table
  long *mcs_Table = NULL;
  if (UL_BWP->pusch_Config)
    mcs_Table = UL_BWP->transform_precoding ? UL_BWP->pusch_Config->mcs_Table : UL_BWP->pusch_Config->mcs_TableTransformPrecoder;

  UL_BWP->mcs_table = get_pusch_mcs_table(mcs_Table, !UL_BWP->transform_precoding, UL_BWP->dci_format, TYPE_C_RNTI_, target_ss, false);
}

void reset_srs_stats(NR_UE_info_t *UE) {
  if (UE) {
    UE->mac_stats.srs_stats[0] = '\0';
  }
}

static void init_bler_stats(const NR_bler_options_t *bler_options, NR_bler_stats_t *bler_stats, frame_t frame)
{
  bler_stats->last_frame = frame;
  bler_stats->mcs = bler_options->min_mcs;
  bler_stats->bler = (float)(bler_options->lower + bler_options->upper) / 2.0f;
}

/* @brief returns a new UE allocated instance.
 *
 * It will be typically added to the access_ue_list, but not always (e.g.,
 * phytest mode), so this is not done in this function (and also, to allow
 * error handling). Remove with release_nr_ue_inst().  */
NR_UE_info_t *get_new_nr_ue_inst(uid_allocator_t *uia, rnti_t rnti, NR_CellGroupConfig_t *CellGroup, nr_cell_sched_t *cell)
{
  NR_UE_info_t *UE = calloc_or_fail(1, sizeof(NR_UE_info_t));
  for (int i = 0; i < MAX_NUM_OF_SSB; i++)
    UE->beam_rsrp[i] = UE->beam_sinr[i] = INT16_MIN;
  UE->uid = uid_linear_allocator_new(uia);
  UE->rnti = rnti;
  UE->CellGroup = CellGroup;
  UE->pcell = cell;
  UE->ra = calloc(1, sizeof(*UE->ra));
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  sched_ctrl->ta_update = 31;
  UE->sched_srs.periodic_sched.periodic_offset = -1;

  const nr_mac_config_t *config = &cell->radio_config;
  nr_mac_set_target_snrx10(&sched_ctrl->pucch_pc, config->pucch.target_snrx10);
  sched_ctrl->pucch_pc.avg_snr = config->pucch.target_snrx10 / 10.0f; // set initial SNR to what we would expect on average
  nr_mac_set_rssi_threshold(&sched_ctrl->pucch_pc, config->pucch.rssi_threshold);
  nr_mac_set_target_snrx10(&sched_ctrl->pusch_pc, config->pusch.target_snrx10);
  sched_ctrl->pusch_pc.avg_snr = config->pusch.target_snrx10 / 10.0f; // set initial SNR to what we would expect on average
  nr_mac_set_rssi_threshold(&sched_ctrl->pusch_pc, config->pusch.rssi_threshold);

  /* Set default BWPs */
  AssertFatal(UE->sc_info.n_ul_bwp <= NR_MAX_NUM_BWP, "uplinkBWP_ToAddModList has %d BWP!\n", UE->sc_info.n_ul_bwp);

  // initialize LCID structure
  seq_arr_init(&sched_ctrl->lc_config, sizeof(nr_lc_config_t));
  return UE;
}

void release_nr_ue_inst(uid_allocator_t *uia, NR_UE_info_t *UE)
{
  LOG_I(NR_MAC, "Remove NR rnti 0x%04x\n", UE->rnti);
  uid_linear_allocator_free(uia, UE->uid);
  delete_nr_ue_data(UE);
}

static bool find_rnti(const void *search, const void *current)
{
  rnti_t s = *(const rnti_t *) search;
  const NR_UE_info_t *c = *(const NR_UE_info_t **) current;
  return s == c->rnti;
}

NR_UE_info_t *remove_connected_nr_ue(NR_UEs_t *UEs, rnti_t rnti)
{
  void *data;
  hashtable_rc_t get = hashtable_get(&UEs->connected_ues, rnti, &data);
  if (get != HASH_TABLE_OK)
    return NULL;
  hashtable_rc_t rc = hashtable_remove(&UEs->connected_ues, rnti, NULL); // caller has to free
  elm_arr_t el = find_if(&UEs->connected_ues_list, &rnti, find_rnti);
  DevAssert(el.found); // if in hashtable, must be here
  seq_arr_erase(&UEs->connected_ues_list, el.it);
  DevAssert(rc == HASH_TABLE_OK);
  DevAssert(data != NULL);
  return data;
}

/** @brief Transitions a UE from access list to connected list (i.e., the RA
 * list to the "normal" UE context list. */
bool transition_ra_connected_nr_ue(gNB_MAC_INST *nr_mac, NR_UE_info_t *UE)
{
  NR_UEs_t *UE_info = &nr_mac->UE_info;

  // remove UE from initial access list (moved to connected mode)
  NR_UE_info_t *r = remove_ra_ue(UE_info, UE->rnti);
  DevAssert(r == UE); /* sanity check: we should have removed the current UE ptr from list */

  // The RA completed, so the UE's UL works. Clear any UL-failure state
  nr_mac_reset_ul_failure(&UE->UE_sched_ctrl);

  free_and_zero(UE->ra);

  return add_connected_nr_ue(nr_mac, UE);
}

/** @brief Add a UE to the list of UEs in * connected mode.
 *
 * To remove the UE, use mac_remove_nr_ue(). */
bool add_connected_nr_ue(gNB_MAC_INST *nr_mac, NR_UE_info_t *UE)
{
  LOG_I(NR_MAC, "Adding new UE context with RNTI 0x%04x\n", UE->rnti);
  NR_UEs_t *UE_info = &nr_mac->UE_info;
  AssertFatal(!UE->ra, "UE in connected cannot have RA process\n");

  bool exists = hashtable_is_key_exists(&UE_info->connected_ues, UE->rnti) != HASH_TABLE_KEY_NOT_EXISTS;
  int num_ues = get_num_connected_ue(UE_info);
  if (exists || num_ues >= MAX_MOBILES_PER_GNB) {
    LOG_E(NR_MAC, "Try to add UE %04x but UE existing or the list is full\n", UE->rnti);
    return false;
  }
  bool success = hashtable_insert(&UE_info->connected_ues, UE->rnti, UE, NULL) == HASH_TABLE_OK;
  DevAssert(success); // logic bug: must be free after hashtable_is_key_exists() succeeded
  seq_arr_push_back(&UE_info->connected_ues_list, &UE, sizeof(UE));

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  sched_ctrl->dl_max_mcs = 28; /* do not limit MCS for individual UEs */
  sched_ctrl->pdcch_cl_adjust = 0;
  if (UE->pcell->radio_config.do_SRS == APERIODIC_SRS) {
    // for now aperiodic hardcoded every 160 slots
    nr_timer_setup(&UE->sched_srs.aperiodic_sched.aperiodic_srs_timer, 160, 1);
    nr_timer_start(&UE->sched_srs.aperiodic_sched.aperiodic_srs_timer);
  }
  reset_srs_stats(UE);

  // Initialize bler_stats
  init_bler_stats(&UE->pcell->dl_bler, &sched_ctrl->dl_bler_stats, nr_mac->frame);
  init_bler_stats(&UE->pcell->ul_bler, &sched_ctrl->ul_bler_stats, nr_mac->frame);

  return true;
}

void free_sched_pucch_list(NR_UE_sched_ctrl_t *sched_ctrl)
{
  free(sched_ctrl->sched_pucch);
}

void reset_dl_harq_list(NR_UE_sched_ctrl_t *sched_ctrl) {
  int harq;
  while ((harq = sched_ctrl->feedback_dl_harq.head) >= 0) {
    remove_front_nr_list(&sched_ctrl->feedback_dl_harq);
    add_tail_nr_list(&sched_ctrl->available_dl_harq, harq);
  }

  while ((harq = sched_ctrl->retrans_dl_harq.head) >= 0) {
    remove_front_nr_list(&sched_ctrl->retrans_dl_harq);
    add_tail_nr_list(&sched_ctrl->available_dl_harq, harq);
  }

  for (int i = 0; i < NR_MAX_HARQ_PROCESSES; i++) {
    sched_ctrl->harq_processes[i].feedback_slot = -1;
    sched_ctrl->harq_processes[i].round = 0;
    sched_ctrl->harq_processes[i].is_waiting = false;
  }
}

void reset_ul_harq_list(NR_UE_sched_ctrl_t *sched_ctrl) {
  int harq;
  while ((harq = sched_ctrl->feedback_ul_harq.head) >= 0) {
    remove_front_nr_list(&sched_ctrl->feedback_ul_harq);
    add_tail_nr_list(&sched_ctrl->available_ul_harq, harq);
  }

  while ((harq = sched_ctrl->retrans_ul_harq.head) >= 0) {
    remove_front_nr_list(&sched_ctrl->retrans_ul_harq);
    add_tail_nr_list(&sched_ctrl->available_ul_harq, harq);
  }

  for (int i = 0; i < NR_MAX_HARQ_PROCESSES; i++) {
    sched_ctrl->ul_harq_processes[i].feedback_slot = -1;
    sched_ctrl->ul_harq_processes[i].round = 0;
    sched_ctrl->ul_harq_processes[i].is_waiting = false;
  }
}

void mac_remove_nr_ue(gNB_MAC_INST *nr_mac, rnti_t rnti)
{
  NR_UEs_t *UE_info = &nr_mac->UE_info;
  NR_UE_info_t *UE = remove_connected_nr_ue(UE_info, rnti);
  if (!UE)
    UE = remove_ra_ue(UE_info, rnti);
  if (UE)
    release_nr_ue_inst(&nr_mac->UE_info.uid_allocator, UE);
}

/**
 * @brief Returns the number of dBs for given transmit power control (TPC)
 * command. Asserts on illegal TPC.
 * @param tpc TPC command in range 0..3
 * @return The dB value expressed by the TPC command
 */
static int tpc_to_db(int tpc)
{
  DevAssert(tpc >= 0 && tpc <= 3);
  const int db[] = {-1, 0, 1, 3};
  return db[tpc];
}

/**
 * @brief Limits the power control commands (TPC) in NR by checking the RSSI threshold.
 *
 * This function evaluates the received signal strength indicator (RSSI) and adjusts the
 * transmit power control (TPC) commands accordingly to ensure they remain within acceptable
 * limits. It helps in maintaining the signal quality and preventing excessive power usage.
 *
 * @param rssi The received signal strength indicator value, as defined in FAPI specifications.
 * @param tpc The transmit power control command to be limited.
 * @param rssi_threshold RSSI threshold in 0.1 dBm/dBFS, range -1280 to 0
 * @return The adjusted TPC command after applying the RSSI threshold check.
 */
static uint8_t nr_limit_tpc(int tpc, int rssi, int rssi_threshold)
{
  if (rssi == 0xFFFF) {
    // RSSI not available, keep tpc
    return tpc;
  }
  // Convert RSSI threshold to FAPI scale
  const int fapi_rssi_0dBm_or_0dBFS = 1280;
  int rssi_fapi_threshold = fapi_rssi_0dBm_or_0dBFS + rssi_threshold;
  // Further limit TPC if above or near RSSI threshold
  if (rssi > rssi_fapi_threshold) {
    // RSSI above theshold, reduce power
    return 0;
  } else if (rssi + tpc_to_db(tpc) * 10 > rssi_fapi_threshold) {
    // Cannot apply required TPC, check 1 dB increment
    if (rssi + 10 > rssi_fapi_threshold) {
      // Still cannot apply required TPC, keep power
      return 1;
    } else {
      // Can apply 1dB increment, but 3 was requested
      return 2;
    }
  }
  return tpc;
}

int get_pdsch_to_harq_feedback(NR_PUCCH_Config_t *pucch_Config,
                                nr_dci_format_t dci_format,
                                uint8_t *pdsch_to_harq_feedback) {
  /* already mutex protected: held in nr_acknack_scheduling() */

  if (dci_format == NR_DL_DCI_FORMAT_1_0) {
    for (int i = 0; i < 8; i++)
      pdsch_to_harq_feedback[i] = i + 1;
    return 8;
  }
  else {
    AssertFatal(pucch_Config != NULL && pucch_Config->dl_DataToUL_ACK != NULL,"dl_DataToUL_ACK shouldn't be null here\n");
    for (int i = 0; i < pucch_Config->dl_DataToUL_ACK->list.count; i++) {
      pdsch_to_harq_feedback[i] = *pucch_Config->dl_DataToUL_ACK->list.array[i];
    }
    return pucch_Config->dl_DataToUL_ACK->list.count;
  }
}

void nr_csirs_scheduling(gNB_MAC_INST *gNB_mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, nfapi_nr_dl_tti_request_t *DL_req)
{
  NR_UEs_t *UE_info = &gNB_mac->UE_info;
  int n_slots_frame = cell->frame_structure.numb_slots_frame;

  UE_info->sched_csirs = 0;

  FOR_EACH_CONNECTED_UE(UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    NR_UE_DL_BWP_t *dl_bwp = &UE->current_DL_BWP;

    // CSI-RS is common to all UEs in a given BWP
    // therefore we need to schedule only once per BWP
    // the following condition verifies if CSI-RS
    // has been already scheduled in this BWP
    if (UE_info->sched_csirs & (1 << dl_bwp->bwp_id))
      continue;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    if (nr_timer_is_active(&sched_ctrl->transm_interrupt))
      continue;

    if (!UE->sc_info.csi_MeasConfig)
      continue;

    NR_CSI_MeasConfig_t *csi_measconfig = UE->sc_info.csi_MeasConfig;

    // Need all three lists in order to resolve CSI-ResourceConfig -> ResourceSet -> Resource.
    if (csi_measconfig->nzp_CSI_RS_ResourceToAddModList == NULL || csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList == NULL
        || csi_measconfig->csi_ResourceConfigToAddModList == NULL)
      continue;

    nfapi_nr_dl_tti_request_body_t *dl_req = &DL_req->dl_tti_request_body;

    for (int csi_list = 0; csi_list < csi_measconfig->csi_ResourceConfigToAddModList->list.count; csi_list++) {
      NR_CSI_ResourceConfig_t *csires = csi_measconfig->csi_ResourceConfigToAddModList->list.array[csi_list];

      // Transmitting CSI-RS only for current BWP
      if (csires->bwp_Id > 1) {
        LOG_E(NR_MAC, "Invalid CSI resource BWP ID %ld, we only configure BWP up to 1\n", csires->bwp_Id);
        continue;
      }
      if (csires->csi_RS_ResourceSetList.present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB
          || !csires->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList)
        continue;

      // Iterate over every NZP-CSI-RS-ResourceSet ID referenced by this CSI-ResourceConfig
      for (int s = 0; s < csires->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.count; s++) {
        NR_NZP_CSI_RS_ResourceSetId_t target_set_id =
            *csires->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[s];

        // Resolve the ResourceSet by its set ID
        NR_NZP_CSI_RS_ResourceSet_t *nzp_set = NULL;
        for (int k = 0; k < csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList->list.count; k++) {
          if (csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[k]->nzp_CSI_ResourceSetId == target_set_id) {
            nzp_set = csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[k];
            break;
          }
        }
        if (!nzp_set)
          continue;

        // For each NZP-CSI-RS-Resource ID listed inside this ResourceSet, resolve the matching NZP-CSI-RS-Resource and
        // try to schedule its transmission in this slot.
        for (int r = 0; r < nzp_set->nzp_CSI_RS_Resources.list.count; r++) {
          NR_NZP_CSI_RS_ResourceId_t target_res_id = *nzp_set->nzp_CSI_RS_Resources.list.array[r];

          NR_NZP_CSI_RS_Resource_t *nzpcsi = NULL;
          for (int q = 0; q < csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.count; q++) {
            if (csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.array[q]->nzp_CSI_RS_ResourceId == target_res_id) {
              nzpcsi = csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.array[q];
              break;
            }
          }
          if (!nzpcsi)
            continue;

          NR_CSI_RS_ResourceMapping_t resourceMapping = nzpcsi->resourceMapping;
          int period, offset;
          csi_period_offset(NULL, nzpcsi->periodicityAndOffset, &period, &offset);

          if (!is_periodic_occasion(frame, slot, n_slots_frame, period, offset))
            continue;

          LOG_D(NR_MAC,"Scheduling CSI-RS in frame %d slot %d Resource ID %ld\n", frame, slot, nzpcsi->nzp_CSI_RS_ResourceId);
          NR_beam_alloc_t beam_csi = beam_allocation_procedure(&cell->beam_info, frame, slot, UE->UE_beam_index, n_slots_frame);
          AssertFatal(beam_csi.idx >= 0, "Cannot allocate CSI-RS in any available beam\n");
          uint16_t *vrb_map = cell->common_channels.vrb_map[beam_csi.idx];
          UE_info->sched_csirs |= (1 << dl_bwp->bwp_id);

          nfapi_nr_dl_tti_request_pdu_t *dl_tti_csirs_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
          memset((void*)dl_tti_csirs_pdu,0,sizeof(nfapi_nr_dl_tti_request_pdu_t));
          dl_tti_csirs_pdu->PDUType = NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE;
          dl_tti_csirs_pdu->PDUSize = (uint8_t)(2+sizeof(nfapi_nr_dl_tti_csi_rs_pdu));

          nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *csirs_pdu_rel15 = &dl_tti_csirs_pdu->csi_rs_pdu.csi_rs_pdu_rel15;
          csirs_pdu_rel15->precodingAndBeamforming.num_prgs = 1;
          csirs_pdu_rel15->precodingAndBeamforming.prg_size = resourceMapping.freqBand.nrofRBs; //1 PRG of max size
          csirs_pdu_rel15->precodingAndBeamforming.dig_bf_interfaces = 1;
          csirs_pdu_rel15->precodingAndBeamforming.prgs_list[0].pm_idx = 0;
          const uint16_t fapi_beam = convert_to_fapi_beam(UE->UE_beam_index, &cell->beam_info);
          // TODO: set correctly dig_bf_interface_list when ports of same CDM group is used and PMI if used.
          csirs_pdu_rel15->precodingAndBeamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx = fapi_beam;
          const nr_pdsch_AntennaPorts_t *p = &cell->radio_config.pdsch_AntennaPorts;
          const uint16_t num_max_csi_ports = p->N1 * p->N2 * p->XP;
          /* The L1 does not take number of spatial streams parameter into
          consideration because the CSI-RS generation function uses information
          in mapping params to determine number of ports and maps to contiguous
          logical ports. Hence we set only the first port for CSI-RS here. */
          csirs_pdu_rel15->param_v4.numSpatialStreamIndices = 1;
          csirs_pdu_rel15->param_v4.spatialStreamIndices[0] = beam_csi.idx * num_max_csi_ports;
          csirs_pdu_rel15->bwp_size = dl_bwp->BWPSize;
          csirs_pdu_rel15->bwp_start = dl_bwp->BWPStart;
          csirs_pdu_rel15->subcarrier_spacing = dl_bwp->scs;
          if (dl_bwp->cyclicprefix)
            csirs_pdu_rel15->cyclic_prefix = *dl_bwp->cyclicprefix;
          else
            csirs_pdu_rel15->cyclic_prefix = 0;

          // According to last paragraph of TS 38.214 5.2.2.3.1
          if (resourceMapping.freqBand.startingRB < dl_bwp->BWPStart) {
            csirs_pdu_rel15->start_rb = dl_bwp->BWPStart;
          } else {
            csirs_pdu_rel15->start_rb = resourceMapping.freqBand.startingRB;
          }
          if (resourceMapping.freqBand.nrofRBs > (dl_bwp->BWPStart + dl_bwp->BWPSize - csirs_pdu_rel15->start_rb)) {
            csirs_pdu_rel15->nr_of_rbs = dl_bwp->BWPStart + dl_bwp->BWPSize - csirs_pdu_rel15->start_rb;
          } else {
            csirs_pdu_rel15->nr_of_rbs = resourceMapping.freqBand.nrofRBs;
          }
          AssertFatal(csirs_pdu_rel15->nr_of_rbs >= 24, "CSI-RS has %d RBs, but the minimum is 24\n", csirs_pdu_rel15->nr_of_rbs);

          csirs_pdu_rel15->csi_type = 1; // NZP-CSI-RS
          csirs_pdu_rel15->symb_l0 = resourceMapping.firstOFDMSymbolInTimeDomain;
          if (resourceMapping.firstOFDMSymbolInTimeDomain2)
            csirs_pdu_rel15->symb_l1 = *resourceMapping.firstOFDMSymbolInTimeDomain2;
          csirs_pdu_rel15->cdm_type = resourceMapping.cdm_Type;
          csirs_pdu_rel15->freq_density = resourceMapping.density.present;
          if ((resourceMapping.density.present == NR_CSI_RS_ResourceMapping__density_PR_dot5)
              && (resourceMapping.density.choice.dot5 == NR_CSI_RS_ResourceMapping__density__dot5_evenPRBs))
            csirs_pdu_rel15->freq_density--;
          csirs_pdu_rel15->scramb_id = nzpcsi->scramblingID;
          csirs_pdu_rel15->power_control_offset = nzpcsi->powerControlOffset + 8;
          if (nzpcsi->powerControlOffsetSS)
            csirs_pdu_rel15->power_control_offset_ss = *nzpcsi->powerControlOffsetSS;
          else
            csirs_pdu_rel15->power_control_offset_ss = 1; // 0 dB
          switch(resourceMapping.frequencyDomainAllocation.present){
            case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row1:
              csirs_pdu_rel15->row = 1;
              csirs_pdu_rel15->freq_domain = ((resourceMapping.frequencyDomainAllocation.choice.row1.buf[0])>>4)&0x0f;
              for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
              break;
            case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row2:
              csirs_pdu_rel15->row = 2;
              csirs_pdu_rel15->freq_domain = (((resourceMapping.frequencyDomainAllocation.choice.row2.buf[1]>>4)&0x0f) |
                                             ((resourceMapping.frequencyDomainAllocation.choice.row2.buf[0]<<4)&0xff0));
              for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
              break;
            case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row4:
              csirs_pdu_rel15->row = 4;
              csirs_pdu_rel15->freq_domain = ((resourceMapping.frequencyDomainAllocation.choice.row4.buf[0])>>5)&0x07;
              for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
              break;
            case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_other:
              csirs_pdu_rel15->freq_domain = ((resourceMapping.frequencyDomainAllocation.choice.other.buf[0])>>2)&0x3f;
              // determining the row of table 7.4.1.5.3-1 in 38.211
              switch(resourceMapping.nrofPorts){
                case NR_CSI_RS_ResourceMapping__nrofPorts_p1:
                  AssertFatal(1==0,"Resource with 1 CSI port shouldn't be within other rows\n");
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p2:
                  csirs_pdu_rel15->row = 3;
                  for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                    vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p4:
                  csirs_pdu_rel15->row = 5;
                  for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                    vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2);
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p8:
                  if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2) {
                    csirs_pdu_rel15->row = 8;
                    for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                      vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2);
                  }
                  else{
                    int num_k = 0;
                    for (int k=0; k<6; k++)
                      num_k+=(((csirs_pdu_rel15->freq_domain)>>k)&0x01);
                    if(num_k==4) {
                      csirs_pdu_rel15->row = 6;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
                    }
                    else {
                      csirs_pdu_rel15->row = 7;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2);
                    }
                  }
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p12:
                  if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2) {
                    csirs_pdu_rel15->row = 10;
                    for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                      vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2);
                  }
                  else {
                    csirs_pdu_rel15->row = 9;
                    for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                      vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 1);
                  }
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p16:
                  if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
                    csirs_pdu_rel15->row = 12;
                  else
                    csirs_pdu_rel15->row = 11;
                  for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                    vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2);
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p24:
                  if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2) {
                    csirs_pdu_rel15->row = 14;
                    for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                      vrb_map[rb] |= (SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2) | SL_to_bitmap(csirs_pdu_rel15->symb_l1, 2));
                  }
                  else{
                    if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4) {
                      csirs_pdu_rel15->row = 15;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 3);
                    }
                    else {
                      csirs_pdu_rel15->row = 13;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= (SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2) | SL_to_bitmap(csirs_pdu_rel15->symb_l1, 2));
                    }
                  }
                  break;
                case NR_CSI_RS_ResourceMapping__nrofPorts_p32:
                  if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2) {
                    csirs_pdu_rel15->row = 17;
                    for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                      vrb_map[rb] |= (SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2) | SL_to_bitmap(csirs_pdu_rel15->symb_l1, 2));
                  }
                  else{
                    if (resourceMapping.cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4) {
                      csirs_pdu_rel15->row = 18;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= SL_to_bitmap(csirs_pdu_rel15->symb_l0, 3);
                    }
                    else {
                      csirs_pdu_rel15->row = 16;
                      for (int rb = csirs_pdu_rel15->start_rb; rb < (csirs_pdu_rel15->start_rb + csirs_pdu_rel15->nr_of_rbs); rb++)
                        vrb_map[rb] |= (SL_to_bitmap(csirs_pdu_rel15->symb_l0, 2) | SL_to_bitmap(csirs_pdu_rel15->symb_l1, 2));
                    }
                  }
                  break;
              default:
                AssertFatal(1==0,"Invalid number of ports in CSI-RS resource\n");
              }
              break;
          default:
            AssertFatal(1==0,"Invalid freqency domain allocation in CSI-RS resource\n");
          }
          dl_req->nPDUs++;
        }
      }
    }
  }
}

static void nr_mac_interrupt_ue_transmission(gNB_MAC_INST *mac, NR_UE_info_t *UE, int slots, int slots_per_frame)
{
  DevAssert(mac != NULL);
  DevAssert(UE != NULL);

  nr_timer_setup(&UE->UE_sched_ctrl.transm_interrupt, slots, 1);
  nr_timer_start(&UE->UE_sched_ctrl.transm_interrupt);

  // it might happen that timing advance command should be sent during the UE inactivity time.
  // To prevent this, delay next TA command just after the UE inactivity time.
  const int inactive_frames = slots / slots_per_frame + 1;
  if ((UE->UE_sched_ctrl.ta_frame - mac->frame + MAX_FRAME_NUMBER) % MAX_FRAME_NUMBER < inactive_frames)
    UE->UE_sched_ctrl.ta_frame = (mac->frame + inactive_frames) % MAX_FRAME_NUMBER;

  LOG_D(NR_MAC, "UE %04x: Interrupt UE transmission (%d slots)\n", UE->rnti, slots);
}


void nr_measgap_scheduling(gNB_MAC_INST *nr_mac, const nr_cell_sched_t *cell, frame_t frame, sub_frame_t slot)
{
  NR_UEs_t *UE_info = &nr_mac->UE_info;
  FOR_EACH_CONNECTED_UE(UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    measgap_config_t *mgc = &UE->measgap_config;
    if (!mgc->enable)
      continue;

    const int slots_frame = cell->frame_structure.numb_slots_frame;
    const frame_t f = (frame + (slot + mgc->n_slots_advance) / slots_frame) % MAX_FRAME_NUMBER;
    const slot_t s = (slot + mgc->n_slots_advance) % slots_frame;

    // TS 38 331 - Section 5.5.2.9 Measurement gap configuration
    if (!(((f % (mgc->mgrp_ms / 10)) == (mgc->gapOffset / 10)) && (s == mgc->gapOffset % 10)))
      continue;

    NR_timer_t *t = &UE->UE_sched_ctrl.transm_interrupt;
    // start a timer to stop scheduling UE during MeasGap, or extend timer for
    // duration of measGap with existing follow-up action
    if (!nr_timer_is_active(t) || nr_timer_remaining_time(t) < mgc->mgl_slots) {
      nr_mac_interrupt_ue_transmission(nr_mac, UE, mgc->mgl_slots, slots_frame);
    }
  }
}

void clean_bwp_structures(NR_SpCellConfig_t *spCellConfig)
{
  NR_ServingCellConfig_t *spCellConfigDedicated = spCellConfig->spCellConfigDedicated;
  if (spCellConfigDedicated->downlinkBWP_ToAddModList && spCellConfigDedicated->downlinkBWP_ToAddModList->list.count > 1)
    asn_sequence_del(&spCellConfigDedicated->downlinkBWP_ToAddModList->list, 0, 1);
  if (spCellConfigDedicated->uplinkConfig->uplinkBWP_ToAddModList
      && spCellConfigDedicated->uplinkConfig->uplinkBWP_ToAddModList->list.count > 1)
    asn_sequence_del(&spCellConfigDedicated->uplinkConfig->uplinkBWP_ToAddModList->list, 0, 1);
}

void nr_mac_clean_cellgroup(NR_CellGroupConfig_t *cell_group)
{
  DevAssert(cell_group != NULL);
  NR_SpCellConfig_t *spCellConfig = cell_group->spCellConfig;
  /* remove a reconfigurationWithSync, we don't need it anymore */
  if (spCellConfig && spCellConfig->reconfigurationWithSync != NULL) {
    ASN_STRUCT_FREE(asn_DEF_NR_ReconfigurationWithSync, spCellConfig->reconfigurationWithSync);
    spCellConfig->reconfigurationWithSync = NULL;
  }
  /* remove the rlc_BearerToReleaseList, we don't need it anymore */
  if (cell_group->rlc_BearerToReleaseList != NULL) {
    struct NR_CellGroupConfig__rlc_BearerToReleaseList *l = cell_group->rlc_BearerToReleaseList;
    asn_sequence_del(&l->list, l->list.count, /* free */ 1);
    free(cell_group->rlc_BearerToReleaseList);
    cell_group->rlc_BearerToReleaseList = NULL;
  }
  /* remove reestablishRLC, we don't need it anymore */
  for (int i = 0; i < cell_group->rlc_BearerToAddModList->list.count; ++i)
    free_and_zero(cell_group->rlc_BearerToAddModList->list.array[i]->reestablishRLC);
  /* remove the BWP release lists */
  NR_ServingCellConfig_t *scd = spCellConfig ? spCellConfig->spCellConfigDedicated : NULL;
  if (scd && scd->downlinkBWP_ToReleaseList) {
    struct NR_ServingCellConfig__downlinkBWP_ToReleaseList *l = scd->downlinkBWP_ToReleaseList;
    while (l->list.count > 0)
      ASN_STRUCT_FREE(asn_DEF_NR_BWP_Id, l->list.array[--l->list.count]);
    free(l->list.array);
    free_and_zero(scd->downlinkBWP_ToReleaseList);
  }
  if (scd && scd->uplinkConfig && scd->uplinkConfig->uplinkBWP_ToReleaseList) {
    struct NR_UplinkConfig__uplinkBWP_ToReleaseList *l = scd->uplinkConfig->uplinkBWP_ToReleaseList;
    while (l->list.count > 0)
      ASN_STRUCT_FREE(asn_DEF_NR_BWP_Id, l->list.array[--l->list.count]);
    free(l->list.array);
    free_and_zero(scd->uplinkConfig->uplinkBWP_ToReleaseList);
  }
}

int nr_mac_get_reconfig_delay_slots(NR_SubcarrierSpacing_t scs)
{
  /* we previously assumed a specific "slot ahead" value for the PHY processing
   * time. However, we cannot always know it (e.g., third-party PHY), so simply
   * assume a tentative worst-case slot processing time */
  const int sl_ahead = 10;
  /* 16ms seems to be the most common: See 38.331 Tab 12.1-1 */
  int delay_ms = NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS;
  return (delay_ms << scs) + sl_ahead;
}

static void nr_mac_ue_transmission_timeout(gNB_MAC_INST *mac, NR_UE_info_t *UE, int slots)
{
  DevAssert(mac != NULL);
  DevAssert(UE != NULL);

  nr_timer_setup(&UE->UE_sched_ctrl.transm_timeout, slots, 1);
  nr_timer_start(&UE->UE_sched_ctrl.transm_timeout);

  LOG_I(NR_MAC, "UE %04x: Timeout for UE transmission (%d slots)\n", UE->rnti, slots);
  return;
}

int nr_transmission_action_indicator_stop(gNB_MAC_INST *mac, NR_UE_info_t *UE_info)
{
  int delay = nr_mac_get_reconfig_delay_slots(UE_info->current_DL_BWP.scs);
  nr_mac_ue_transmission_timeout(mac, UE_info, delay);
  LOG_I(NR_MAC, "gNB-DU received the TransmissionActionIndicator with Stop value for UE %04x\n", UE_info->rnti);
  return 0;
}

void nr_mac_trigger_release_complete(gNB_MAC_INST *mac, int rnti)
{
  // the CU might not know such UE, e.g., because we never sent a message to
  // it, so there might not be a corresponding entry for such UE in the look up
  // table. This can happen, e.g., on Msg.3 with C-RNTI, where we create a UE
  // MAC context, decode the PDU, find the C-RNTI MAC CE, and then throw the
  // newly created context away. See also in _nr_rx_sdu() and commit 93f59a3c6e56f
  if (!du_exists_f1_ue_data(rnti))
    return;

  // unlock the scheduler temporarily to prevent possible deadlocks with
  // du_remove_f1_ue_data() (and also while sending the message to RRC)
  NR_SCHED_UNLOCK(&mac->sched_lock);
  f1_ue_data_t ue_data = du_get_f1_ue_data(rnti);
  f1ap_ue_context_rel_cplt_t complete = {
    .gNB_CU_ue_id = ue_data.secondary_ue,
    .gNB_DU_ue_id = rnti,
  };
  mac->mac_rrc.ue_context_release_complete(&complete);

  du_remove_f1_ue_data(rnti);
  NR_SCHED_LOCK(&mac->sched_lock);
}

void nr_mac_release_ue(gNB_MAC_INST *mac, int rnti)
{
  nr_rlc_remove_ue(rnti);
  mac_remove_nr_ue(mac, rnti);
}

void beam_switching_procedure(gNB_MAC_INST *mac, nr_cell_sched_t *cell, NR_UE_info_t *UE, int new_beam_index)
{
  // currently we only need to reconfigure if CSI-RS is enabled
  // if we reconfigure we need to wait to switch the confirmation of context modification
  if (cell->radio_config.do_CSIRS)
    nr_mac_trigger_reconfiguration(mac, cell, UE, -1, new_beam_index);
  else {
    LOG_I(NR_MAC, "[UE %x] Switching to beam with ID %d (from %d)\n", UE->rnti, new_beam_index, UE->UE_beam_index);
    UE->UE_beam_index = new_beam_index;
  }
}

void nr_mac_update_timers(gNB_MAC_INST *mac, nr_cell_sched_t *cell)
{
  // Releasing a UE compacts either UE list, and nr_mac_trigger_release_complete() drops sched_lock:
  // collect the UEs to release and release them by RNTI after both loops
  typedef struct {
    rnti_t rnti;
    bool send_complete;
  } ue_release_t;
  ue_release_t to_release[MAX_MOBILES_PER_GNB + NR_NB_RA_PROC_MAX];
  int num_release = 0;
  FOR_EACH_CONNECTED_UE(&mac->UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;

    if (nr_mac_check_release(sched_ctrl)) {
      to_release[num_release++] = (ue_release_t){.rnti = UE->rnti, .send_complete = true};
      continue;
    }

    /* check if UL failure and trigger release request if necessary */
    if (nr_mac_check_ul_failure(mac, UE->rnti, sched_ctrl)) {
      to_release[num_release++] = (ue_release_t){.rnti = UE->rnti, .send_complete = false};
      continue;
    }

    if (nr_timer_tick(&sched_ctrl->transm_interrupt)) {
      /* expired */
      nr_timer_stop(&sched_ctrl->transm_interrupt);
    }
    if (nr_timer_tick(&sched_ctrl->transm_timeout)) {
      nr_timer_stop(&sched_ctrl->transm_timeout);
      LOG_W(NR_MAC, "UE %04x UL failure after transmission timeout\n", UE->rnti);
      nr_mac_trigger_ul_failure(sched_ctrl, UE->current_DL_BWP.scs);
    }
    if (nr_timer_tick(&sched_ctrl->tci_beam_switch)) {
      nr_timer_stop(&sched_ctrl->tci_beam_switch);
      beam_switching_procedure(mac, cell, UE, sched_ctrl->UE_mac_ce_ctrl.tci_state_ind.tciStateId);
    }
    nr_timer_tick(&UE->sched_srs.aperiodic_sched.aperiodic_srs_timer);
  }

  // Pending access contexts can also receive a release command or exhaust Msg4 retries.
  FOR_EACH_RA_UE(&mac->UE_info, UE) {
    if (UE->pcell != cell)
      continue;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    if (nr_mac_check_release(sched_ctrl)) {
      to_release[num_release++] = (ue_release_t){.rnti = UE->rnti, .send_complete = true};
    } else if (nr_mac_check_ul_failure(mac, UE->rnti, sched_ctrl)) {
      to_release[num_release++] = (ue_release_t){.rnti = UE->rnti, .send_complete = false};
    }
  }

  for (int i = 0; i < num_release; i++) {
    // confirm the release to the CU, then remove the RLC and MAC contexts
    if (to_release[i].send_complete)
      nr_mac_trigger_release_complete(mac, to_release[i].rnti);
    nr_mac_release_ue(mac, to_release[i].rnti);
  }
}

int ul_buffer_index(int frame, int slot, int slots_per_frame, int size)
{
  const int abs_slot = frame * slots_per_frame + slot;
  return abs_slot % size;
}

void UL_tti_req_ahead_initialization(nr_cell_sched_t *cell, int n, frame_t frameP, int slotP)
{

  if(cell->UL_tti_req_ahead[1].Slot == 1)
    return;

  /* fill in slot/frame numbers: slot is fixed, frame will be updated by scheduler
   * consider that scheduler runs sl_ahead: the first sl_ahead slots are
   * already "in the past" and thus we put frame 1 instead of 0! */
  for (int i = 0; i < cell->UL_tti_req_ahead_size; ++i) {
    int abs_slot = frameP * n + slotP + i;
    nfapi_nr_ul_tti_request_t *req = &cell->UL_tti_req_ahead[abs_slot % cell->UL_tti_req_ahead_size];
    req->SFN = (abs_slot / n) % MAX_FRAME_NUMBER;
    req->Slot = abs_slot % n;
  }
}

int get_beam_from_ssbidx(nr_cell_sched_t *cell, int ssb_idx)
{
  int beam_idx = cell->beam_index_list[ssb_idx];
  AssertFatal(beam_idx >= 0, "Invalid beamforming index %d\n", beam_idx);
  return beam_idx;
}

/** @brief Maximum number of SS/PBCH block positions (L_max)
 * @param scc ServingCellConfigCommon for which to determine L_max from ssb-PositionsInBurst (TS 38.331).
 * @return L_max: shortBitmap: 4; mediumBitmap: 8; longBitmap: 64. */
int get_max_ssbs(const NR_ServingCellConfigCommon_t *scc)
{
  switch (scc->ssb_PositionsInBurst->present) {
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap:
      return 4;
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap:
      return 8;
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_longBitmap:
      return 64;
    default:
      AssertFatal(false, "Invalid SSB configuration\n");
  }
}

/** @brief Returns true if @param frame contains a Type0-PDCCH CSS monitoring occasion (TS 38.213 Clause 13). */
static bool check_frame_type0(const long *ssb_periodicityServingCell, const NR_Type0_PDCCH_CSS_config_t *type0, int frame)
{
  if (type0->type0_pdcch_ss_mux_pattern == 1)
    return (frame % 2) == type0->sfn_c;
  DevAssert(ssb_periodicityServingCell);
  long ssb_period = *ssb_periodicityServingCell; // every how many frames SSB are generated
  int ssb_frame_periodicity = (ssb_period > 1) ? (1 << (ssb_period - 1)) : 1; // 0 is every half frame
  return (frame % ssb_frame_periodicity) == 0;
}

/** @brief True if @param frame, @param slot is the Type0-PDCCH CSS monitoring occasion
 * for @param type0 (TS 38.213 Clause 13). Used by SIB1 and paging (SearchSpaceId = 0) scheduling. */
bool is_type0_occasion(NR_ServingCellConfigCommon_t *scc, const NR_Type0_PDCCH_CSS_config_t *type0, int frame, uint32_t slot)
{
  DevAssert(scc);
  DevAssert(type0);
  return type0->active && (slot == type0->slot) && check_frame_type0(scc->ssb_periodicityServingCell, type0, frame);
}

uint64_t get_ssb_bitmap_and_len(const NR_ServingCellConfigCommon_t *scc, uint8_t *len)
{
  switch (scc->ssb_PositionsInBurst->present) {
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap:
      *len = 4;
      break;
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap:
      *len = 8;
      break;
    case NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_longBitmap:
      *len = 64;
      break;
    default :
      AssertFatal(false, "Invalid configuration\n");
  }
  return get_ssb_bitmap(scc);
}

// TODO this is a placeholder for a possibly more complex function
// for now the fapi beam index is the number of SSBs transmitted before ssb_index i
void fill_beam_index_list(NR_ServingCellConfigCommon_t *scc, const nr_mac_config_t *config, nr_cell_sched_t *cell)
{
  if (cell->beam_info.config.bf_method == BF_METHOD_STRAIGHT_WIRE)
    return;

  uint8_t len = 0;
  const uint64_t ssbBitmap = get_ssb_bitmap_and_len(scc, &len);
  int index = 0;
  for (int i = 0; i < len; ++i) {
    if (IS_BIT_SET(ssbBitmap, (63 - i))) {
      int fapi_index = index; // no explicit list (DAS): the beam index is the SSB ordinal
      if (config->num_ssb_beams > 0) {
        AssertFatal(index < config->num_ssb_beams,
                    "not enough beam indices for the transmitted SSBs: only %d configured, need at least %d\n",
                    config->num_ssb_beams,
                    index + 1);
        fapi_index = config->ssb_beams[index];
      }
      cell->beam_index_list[i] = fapi_index;
      LOG_I(NR_MAC, "SSB %d transmitted on beam %d\n", i, fapi_index);
      index++;
    } else
      cell->beam_index_list[i] = -1;
  }
  AssertFatal(config->num_ssb_beams == 0 || index == config->num_ssb_beams,
              "%d beam indices configured but %d SSB(s) transmitted\n",
              config->num_ssb_beams,
              index);
}

static inline int get_beam_index(const NR_beam_info_t *beam_info, int frame, int slot, int slots_per_frame)
{
  return ((frame * slots_per_frame + slot) / beam_info->config.beam_duration) % beam_info->beam_allocation_size;
}

NR_beam_alloc_t beam_allocation_procedure(NR_beam_info_t *beam_info, int frame, int slot, int16_t beam_index, int slots_per_frame)
{
  // if no beam allocation for analog beamforming we always return beam index 0 (no multiple beams)
  if (beam_info->config.bf_method == BF_METHOD_STRAIGHT_WIRE)
    return (NR_beam_alloc_t) {.new_beam = false, .idx = 0};

  const int index = get_beam_index(beam_info, frame, slot, slots_per_frame);
  for (int i = 0; i < beam_info->config.beams_per_period; i++) {
    NR_beam_alloc_t beam_struct = {.new_beam = false, .idx = i};
    int16_t *beam = &beam_info->beam_allocation[i][index];
    if (*beam == -1) {
      beam_struct.new_beam = true;
      *beam = beam_index;
    }
    if (*beam == beam_index) {
      LOG_D(NR_MAC, "%d.%d Using beam structure with index %d for beam %d (%s)\n", frame, slot, beam_struct.idx, beam_index, beam_struct.new_beam ? "new beam" : "old beam");
      return beam_struct;
    }
  }

  return (NR_beam_alloc_t) {.new_beam = false, .idx = -1};
}

uint16_t convert_to_fapi_beam(const uint16_t beam_idx, const NR_beam_info_t *beam_info)
{
  AssertFatal(beam_idx < 32768, "Beam index out of range. Valid range is [0, 32767]\n");
  // MSB set: use the ID as is; MSB clear: it points into a pre-stored set of weights
  return beam_info->config.beam_id_to_ru ? SET_BIT(beam_idx, 15) : beam_idx;
}

int16_t get_allocated_beam(const NR_beam_info_t *beam_info, int frame, int slot, int slots_per_frame, int beam_number_in_period)
{
  int16_t beam_idx = 0;
  if (beam_info->config.bf_method != BF_METHOD_STRAIGHT_WIRE) {
    const int index = get_beam_index(beam_info, frame, slot, slots_per_frame);
    beam_idx = beam_info->beam_allocation[beam_number_in_period][index];
  }
  return beam_idx;
}

void reset_beam_status(NR_beam_info_t *beam_info, int frame, int slot, int16_t beam_index, int slots_per_frame, bool new_beam)
{
  if(!new_beam) // need to reset only if the beam was allocated specifically for this instance
    return;
  const int index = get_beam_index(beam_info, frame, slot, slots_per_frame);
  for (int i = 0; i < beam_info->config.beams_per_period; i++) {
    if (beam_info->beam_allocation[i][index] == beam_index)
      beam_info->beam_allocation[i][index] = -1;
  }
}

// TODO Values based on Table 10.1.6.1-2 of 38.133 to be refined
static int required_hysteresis_count(int rsrp_gap_db)
{
  if (rsrp_gap_db >= 10)
    return 1;  // clearly better -> switch immediately
  if (rsrp_gap_db >= 6)
    return 2;
  if (rsrp_gap_db >= 2)
    return 4;
  return 8;  // marginal -> need sustained evidence
}

int beam_selection_procedures(nr_cell_sched_t *cell, NR_UE_info_t *UE)
{
  if (cell->beam_info.config.bf_method == BF_METHOD_STRAIGHT_WIRE)
    return -1;

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  RSRP_report_list_t *rsrp_report = &sched_ctrl->CSI_report.ssb_rsrp_report;
  beam_hysteresis_t *hyst = &sched_ctrl->beam_hysteresis;

  int best_beam = get_beam_from_ssbidx(cell, rsrp_report->r[0].resource_id);
  int best_rsrp = rsrp_report->r[0].RSRP;
  tciStateInd_t *tci = cell->radio_config.do_TCI ? &sched_ctrl->UE_mac_ce_ctrl.tci_state_ind : NULL;

  if (best_beam == UE->UE_beam_index) {
    // current beam confirmed best again -> reset any accumulating challenger
    hyst->candidate_beam_index = -1;
    hyst->consecutive_count = 0;
    if (tci && tci->is_scheduled) {
      LOG_I(NR_MAC, "[UE %x] Stopping procedure to switch beam, old beam reported as best again\n", UE->rnti);
      tci->is_scheduled = false;
    }
    return -1;
  }

  // a TCI switch is already outstanding -> don't evaluate a new candidate until it completes
  if (tci && tci->is_scheduled)
    return -1;

  // looking for current beam in the list of differential reports
  int current_rsrp = INT_MAX;
  for (int k = 1; k < rsrp_report->nb; k++) {
    if (get_beam_from_ssbidx(cell, rsrp_report->r[k].resource_id) == UE->UE_beam_index)
      current_rsrp = rsrp_report->r[k].RSRP;
  }
  int gap = (current_rsrp == INT_MAX) ? INT_MAX : best_rsrp - current_rsrp; // not in report -> force fastest switch

  if (best_beam == hyst->candidate_beam_index)
    hyst->consecutive_count++;
  else {
    hyst->candidate_beam_index = best_beam;
    hyst->consecutive_count = 1;
  }

  LOG_D(NR_MAC,
        "[UE %x] Beam switching: gap %d hyst count %d required to switch %d\n",
        UE->rnti,
        gap,
        hyst->consecutive_count,
        required_hysteresis_count(gap));
  if (hyst->consecutive_count < required_hysteresis_count(gap))
    return -1; // too soon to switch, best beam too close to best beam

  // threshold met -> commit the switch
  hyst->candidate_beam_index = -1;
  hyst->consecutive_count = 0;

  if (!tci)
    return best_beam;

  LOG_I(NR_MAC, "[UE %x] Starting procedure to switch beam\n", UE->rnti);
  tci->is_scheduled = true;
  tci->coresetId = sched_ctrl->coreset->controlResourceSetId;
  tci->tciStateId = best_beam;
  return -1;
}

void send_initial_ul_rrc_message(int rnti, const uint8_t *sdu, sdu_size_t sdu_len, void *data)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_UE_info_t *UE = (NR_UE_info_t *)data;

  uint8_t du2cu[1024];
  int encoded_len = 0;
  if (UE->CellGroup)
    encoded_len = encode_cellGroupConfig(UE->CellGroup, du2cu, sizeof(du2cu));

  DevAssert(mac->f1_config.setup_req != NULL);
  AssertFatal(mac->f1_config.setup_req->num_cells_available == 1, "can handle only one cell\n");
  const f1ap_initial_ul_rrc_message_t ul_rrc_msg = {
    .plmn = mac->f1_config.setup_req->cell[0].info.plmn,
    .nr_cellid = mac->f1_config.setup_req->cell[0].info.nr_cellid,
    .gNB_DU_ue_id = rnti,
    .crnti = rnti,
    .rrc_container = (uint8_t *) sdu,
    .rrc_container_length = sdu_len,
    .du2cu_rrc_container = encoded_len ? (uint8_t *) du2cu : NULL,
    .du2cu_rrc_container_length = encoded_len
  };
  mac->mac_rrc.initial_ul_rrc_message_transfer(0, &ul_rrc_msg);
}

bool prepare_initial_ul_rrc_message(gNB_MAC_INST *mac, nr_cell_sched_t *cell, NR_UE_info_t *UE)
{
  /* activate SRB0 */
  if (!nr_rlc_activate_srb0(UE->rnti, UE, send_initial_ul_rrc_message))
    return false;

  if (UE->uid >= MAX_MOBILES_PER_GNB) {
    // verifying if any UE left in the meantime and it is possible to get a valid UID
    uid_linear_allocator_free(&mac->UE_info.uid_allocator, UE->uid);
    UE->uid = uid_linear_allocator_new(&mac->UE_info.uid_allocator);
    if (UE->uid >= MAX_MOBILES_PER_GNB) {
      // if the UE list is full, we should reject the UE with an RRC reject message
      // to signal the upper layers to send a reject we send an empty CellGroup information element
      UE->CellGroup = NULL;
      LOG_W(NR_MAC, "Maximum number of UE alllowed at DU reached. Cannot serve UE %04x", UE->rnti);
      return true;
    }
  }

  /* create this UE's initial CellGroup */
  int srb_id = 1;
  const NR_ServingCellConfigCommon_t *scc = cell->common_channels.ServingCellConfigCommon;
  int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
  NR_CellGroupConfig_t *cellGroupConfig = get_initial_cellGroupConfig(UE->uid,
                                                                      UE->is_redcap,
                                                                      scc,
                                                                      cell,
                                                                      &mac->rlc_config,
                                                                      ssb_index);
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->CellGroup);
  UE->CellGroup = cellGroupConfig;
  UE->local_bwp_id = UE->is_redcap ? 0 : cell->radio_config.first_active_bwp;

  if (!cellGroupConfig)
    return true;

  /* the cellGroup sent to CU specifies there is SRB1, so create it */
  DevAssert(cellGroupConfig->rlc_BearerToAddModList->list.count == 1);
  const NR_RLC_BearerConfig_t *bearer = cellGroupConfig->rlc_BearerToAddModList->list.array[0];
  DevAssert(bearer->servedRadioBearer->choice.srb_Identity == srb_id);
  nr_rlc_add_srb(UE->rnti, bearer->servedRadioBearer->choice.srb_Identity, bearer);
  int priority = bearer->mac_LogicalChannelConfig->ul_SpecificParameters->priority;
  nr_lc_config_t c = {.lcid = bearer->logicalChannelIdentity, .priority = priority};
  nr_mac_add_lcid(&UE->UE_sched_ctrl, &c);
  return true;
}

void nr_mac_trigger_release_timer(NR_UE_sched_ctrl_t *sched_ctrl, NR_SubcarrierSpacing_t subcarrier_spacing)
{
  // trigger 60ms
  sched_ctrl->release_timer = 100 << subcarrier_spacing;
}

bool nr_mac_check_release(NR_UE_sched_ctrl_t *sched_ctrl)
{
  if (sched_ctrl->release_timer == 0)
    return false;
  sched_ctrl->release_timer--;
  return sched_ctrl->release_timer == 0;
}

bool nr_mac_ue_is_active(const NR_UE_info_t *ue)
{
  /* pass NR_UE_info_t, so that later we could adapt, e.g., for DRX */
  const NR_UE_sched_ctrl_t *sched_ctrl = &ue->UE_sched_ctrl;
  if (sched_ctrl->ul_failure)
    return false;
  if (nr_timer_is_active(&sched_ctrl->transm_interrupt))
    return false;
  return true;
}

#define UL_FAILURE_REQ_GRACE 10000
#define UL_FAILURE_TIMEOUT 30000
void nr_mac_trigger_ul_failure(NR_UE_sched_ctrl_t *sched_ctrl, NR_SubcarrierSpacing_t subcarrier_spacing)
{
  if (sched_ctrl->ul_failure) {
    /* already running */
    return;
  }
  sched_ctrl->ul_failure = true;
  // UL_FAILURE_TIMEOUT ms till triggering release request
  // UL_FAILURE_REQ_GRACE ms till automatically released after request
  sched_ctrl->ul_failure_timer = (UL_FAILURE_REQ_GRACE + UL_FAILURE_TIMEOUT) << subcarrier_spacing;
}

void nr_mac_reset_ul_failure(NR_UE_sched_ctrl_t *sched_ctrl)
{
  sched_ctrl->ul_failure = false;
  sched_ctrl->ul_failure_timer = 0;
  sched_ctrl->pusch_consecutive_dtx_cnt = 0;
}

/* \brief trigger a release request towards the CU.
 * \returns true if release was requested, else false (in which case the CU
 * does not know the UE, and we can safely remove it). */
bool nr_mac_request_release_ue(const gNB_MAC_INST *nrmac, int rnti)
{
  if (!du_exists_f1_ue_data(rnti)) {
    LOG_W(NR_MAC, "UE %04x: no CU-UE ID stored, cannot request release\n", rnti);
    return false;
  }
  f1_ue_data_t ue_data = du_get_f1_ue_data(rnti);
  f1ap_ue_context_rel_req_t request = {
    .gNB_CU_ue_id = ue_data.secondary_ue,
    .gNB_DU_ue_id = rnti,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = F1AP_CauseRadioNetwork_rl_failure_rlc,
  };
  nrmac->mac_rrc.ue_context_release_request(&request);
  return true;
}

// returns true if the UE must be dropped (nr_mac_release_ue()) by the caller
bool nr_mac_check_ul_failure(gNB_MAC_INST *nrmac, int rnti, NR_UE_sched_ctrl_t *sched_ctrl)
{
  if (!sched_ctrl->ul_failure)
    return false;
  if (sched_ctrl->ul_failure_timer > 0)
    sched_ctrl->ul_failure_timer--;
  /* trigger once: trigger when ul_failure_timer == UL_FAILURE_REQ_GRACE( we
   * wait for a UE release command from upper layers), and UE is automatically
   * released when no answer after UL_FAILURE_REQ_GRACE */
  if (sched_ctrl->ul_failure_timer == UL_FAILURE_REQ_GRACE) {
    LOG_W(MAC, "UE %04x: request release after UL failure timer expiry\n", rnti);
    bool requested = nr_mac_request_release_ue(nrmac, rnti);
    if (!requested)
      sched_ctrl->ul_failure_timer = 0; /* force expiry */
  }
  if (sched_ctrl->ul_failure_timer == 0) {
    LOG_W(NR_MAC, "UE %04x: no answer for release request, dropping UE\n", rnti);
    return true;
  }
  return false;
}

static bool verify_bwp_switch(const NR_UE_info_t *UE, const nr_mac_config_t *configuration, int new_bwp_id)
{
  if (new_bwp_id == UE->current_DL_BWP.bwp_id) {
    LOG_E(NR_MAC, "Source BWP ID and target BWP ID are the same, can't perform switch\n");
    return false;
  }

  if (new_bwp_id == 0)
    return true;
  for (int i = 0; i < configuration->num_additional_bwps; i++) {
    const nr_bwp_config_t *bwp_config = &configuration->bwp_config[i];
    if (bwp_config->id == new_bwp_id)
      return true;
  }
  LOG_E(NR_MAC, "New BWP %d does not belong to the list of configured BWPs, can't perform switch\n", new_bwp_id);
  return false;
}

void nr_mac_trigger_reconfiguration(const gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, NR_UE_info_t *UE, int new_bwp_id, int new_beam)
{
  DevAssert(UE->CellGroup != NULL);
  NR_CellGroupConfig_t *cellGroup_for_UE = NULL;
  int ssb_index = get_ssbidx_from_beam(cell, UE->UE_beam_index);
  if (new_beam >= 0) {
    UE->cm_info.trigger_info = BEAM_SWITCH;
    UE->cm_info.new_state = new_beam;
    cellGroup_for_UE = update_cellGroupConfig_for_reconfig(UE->CellGroup,
                                                           cell,
                                                           UE->capability,
                                                           &UE->uecap_fs_ids,
                                                           cell->common_channels.ServingCellConfigCommon,
                                                           UE->uid,
                                                           UE->current_DL_BWP.bwp_id,
                                                           -1,
                                                           ssb_index);
  } else if (new_bwp_id >= 0) {
    AssertFatal(UE->current_DL_BWP.bwp_id == UE->current_UL_BWP.bwp_id, "We only support same BWP for UL and DL\n");
    if (!verify_bwp_switch(UE, &cell->radio_config, new_bwp_id))
      return;
    else {
      UE->cm_info.trigger_info = BWP_SWITCH;
      UE->cm_info.new_state = new_bwp_id;
      cellGroup_for_UE = update_cellGroupConfig_for_reconfig(UE->CellGroup,
                                                             cell,
                                                             UE->capability,
                                                             &UE->uecap_fs_ids,
                                                             cell->common_channels.ServingCellConfigCommon,
                                                             UE->uid,
                                                             UE->current_DL_BWP.bwp_id,
                                                             new_bwp_id,
                                                             ssb_index);
    }
  } else
    UE->cm_info.trigger_info = MSG3_CRNTI;

  uint8_t buf[2048];
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                  NULL,
                                                  cellGroup_for_UE ? cellGroup_for_UE : UE->CellGroup,
                                                  buf,
                                                  sizeof(buf));
  AssertFatal(enc_rval.encoded > 0, "ASN1 encoding of CellGroupConfig failed, failed type %s\n", enc_rval.failed_type->name);
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->reconfigCellGroup);
  UE->reconfigCellGroup = cellGroup_for_UE;
  du_to_cu_rrc_information_t du2cu = {
    .cellGroupConfig = buf,
    .cellGroupConfig_length = (enc_rval.encoded + 7) >> 3,
  };
  f1_ue_data_t ue_data = du_get_f1_ue_data(UE->rnti);
  f1ap_ue_context_modif_required_t required = {
    .gNB_CU_ue_id = ue_data.secondary_ue,
    .gNB_DU_ue_id = UE->rnti,
    .du_to_cu_rrc_information = &du2cu,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = F1AP_CauseRadioNetwork_action_desirable_for_radio_reasons,
  };
  nrmac->mac_rrc.ue_context_modification_required(&required);
}

long get_lcid_from_drbid(int drb_id)
{
  return drb_id + 3; /* LCID is DRB + 3 */
}

long get_lcid_from_srbid(int srb_id)
{
  return srb_id;
}

static bool eq_lcid_config(const void *vval, const void *vit)
{
  const nr_lc_config_t *val = (const nr_lc_config_t *)vval;
  const nr_lc_config_t *it = (const nr_lc_config_t *)vit;
  return it->lcid == val->lcid;
}

static int cmp_lc_config(const void *va, const void *vb)
{
  const nr_lc_config_t *a = (const nr_lc_config_t *)va;
  const nr_lc_config_t *b = (const nr_lc_config_t *)vb;

  if (a->priority < b->priority)
    return -1;
  if (a->priority == b->priority)
    return 0;
  return 1;
}

nr_lc_config_t *nr_mac_get_lc_config(NR_UE_sched_ctrl_t* sched_ctrl, int lcid)
{
  nr_lc_config_t c = {.lcid = lcid};
  elm_arr_t elm = find_if(&sched_ctrl->lc_config, &c, eq_lcid_config);
  if (elm.found)
    return elm.it;
  else
    return NULL;
}

bool nr_mac_add_lcid(NR_UE_sched_ctrl_t* sched_ctrl, const nr_lc_config_t *c)
{
  elm_arr_t elm = find_if(&sched_ctrl->lc_config, (void *) c, eq_lcid_config);
  if (elm.found) {
    LOG_I(NR_MAC, "cannot add LCID %d: already present, updating configuration\n", c->lcid);
    nr_lc_config_t *exist = (nr_lc_config_t *)elm.it;
    *exist = *c;
  } else {
    LOG_D(NR_MAC, "Add LCID %d\n", c->lcid);
    seq_arr_push_back(&sched_ctrl->lc_config, (void*) c, sizeof(*c));
  }
  void *base = seq_arr_front(&sched_ctrl->lc_config);
  size_t nmemb = seq_arr_size(&sched_ctrl->lc_config);
  size_t size = sizeof(*c);
  qsort(base, nmemb, size, cmp_lc_config);
  return true;
}

bool nr_mac_remove_lcid(NR_UE_sched_ctrl_t *sched_ctrl, long lcid)
{
  nr_lc_config_t c = {.lcid = lcid};
  elm_arr_t elm = find_if(&sched_ctrl->lc_config, &c, eq_lcid_config);
  if (!elm.found) {
    LOG_E(NR_MAC, "can not remove LC: no such LC with ID %ld\n", lcid);
    return false;
  }

  seq_arr_erase(&sched_ctrl->lc_config, elm.it);
  return true;
}

bool nr_mac_get_new_rnti(NR_UEs_t *UEs, rnti_t *rnti)
{
  int loop = 0;
  bool exist_connected_ue, exist_in_pending_ra_ue;
  do {
    *rnti = (taus() % 0xffef) + 1;
    exist_connected_ue = find_connected_nr_ue(UEs, *rnti) != NULL;
    exist_in_pending_ra_ue = find_ra_ue(UEs, *rnti) != NULL;
    loop++;
  } while (loop < 100 && (exist_connected_ue || exist_in_pending_ra_ue));
  return loop < 100; // nothing found: loop count 100
}

/// @brief Update PDCCH closed loop adjust for UE depending on detection of feedback.
/// @param sched_ctrl UE scheduling control info
/// @param feedback_not_detected Whether feedback (PUSCH or HARQ) was detected
void nr_mac_update_pdcch_closed_loop_adjust(NR_UE_sched_ctrl_t *sched_ctrl, bool feedback_not_detected)
{
  if (feedback_not_detected) {
    sched_ctrl->pdcch_cl_adjust = min(1, sched_ctrl->pdcch_cl_adjust + 0.05);
  } else {
    sched_ctrl->pdcch_cl_adjust = max(0, sched_ctrl->pdcch_cl_adjust - 0.01);
  }
}

/**
 * @brief Calculate the current average SNR for the given power control loop.
 * @param pc the power control loop
 * @return the current average SNR
 */
float nr_mac_get_snr(const nr_power_control_t *pc)
{
  return pc->avg_snr + pc->tpc_in_flight;
}

float nr_mac_get_rssi(const nr_power_control_t *pc)
{
  // in FAPI scale: convert to dBm/dBFs
  return pc->avg_rssi / 10.f - 128.f;
}

/**
 * @brief Calculates the difference of current average SNR to the target SNR
 * set in the power control loop.
 * @param pc the power control loop
 * @return the SNR difference
 */
static float get_snr_diff(const nr_power_control_t *pc)
{
  float snr = nr_mac_get_snr(pc);
  float delta = (snr * 10.0f - pc->target_snrx10) / 10.0f;
  LOG_D(NR_MAC, "target %.2f snr %.2f delta %.2f\n", pc->target_snrx10 / 10.0f, snr, delta);
  return delta;
}

/// averaging constant for power control (used for SNR, RSSI).
#define PC_AVG_CNST 0.975f
/**
 * @brief Enter new SNR and RSSI value for the latest UL transmission, and
 * update the average SNR and average RSSI of the corresponding UE.
 *
 * Since the SNR is averaged, any changes through transmit power control (TPC)
 * commands will take time to reflect in the averaged SNR. To allow a
 * continuous tracking of necessary TPC, as well as a continuous adjustment of
 * the target SNR, the algorithm keeps track of "TPC in flight" which is a
 * moving average of the last TPC commands sent to the UE. The sum of the
 * averaged SNR and "TPC in flight" is the actual, current SNR. The RSSI in
 * turn is used to limit TPC commands (see also nr_limit_tpc()).
 *
 * @param pc the power control loop
 * @param snrx10 the current SNR measurement multiplied by 10
 * @param rssi the current RSSI measurement
 */
void nr_mac_pc_snr(nr_power_control_t *pc, int snrx10, int rssi)
{
  pc->avg_snr = PC_AVG_CNST * pc->avg_snr + (1.0f - PC_AVG_CNST) * 0.1 * snrx10;
  pc->avg_rssi = PC_AVG_CNST * pc->avg_rssi + (1.0f - PC_AVG_CNST) * rssi;
  // use an EMA to average out tpc_in_flight as fast as EMA for avg_snr.
  // this will ensure that on TPC change, avg_snr approximates real SNR as
  // fast as tpc_in_flight returns to 0.
  pc->tpc_in_flight = PC_AVG_CNST * pc->tpc_in_flight; // + (1 - PC_AVG_CNST) * 0.0f
}

/**
 * @brief Enter new SNR and RSSI value for the latest UL transmission as new
 * fixed averages. Reset "TPC in flight" to zero.
 *
 * @param pc the power control loop
 * @param snrx10 the current SNR measurement multiplied by 10
 * @param rssi the current RSSI measurement
 */
void nr_mac_pc_reset_snr(nr_power_control_t *pc, int snrx10, int rssi)
{
  pc->avg_snr = 0.1f * snrx10;
  pc->avg_rssi = rssi;
  pc->tpc_in_flight = 0.0f;
}

/**
 * @brief Set a new target SNR for this power control loop, which can be
 * updated on a continuous basis, and will be reflected immediately upon
 * calls to nr_mac_get_tpc() (no "settle time" necessary).
 *
 * @param pc the power control loop
 * @param target_snrx10 the target SNR to be set times 10.
 */
void nr_mac_set_target_snrx10(nr_power_control_t *pc, int target_snrx10)
{
  pc->target_snrx10 = target_snrx10;
}

/**
 * @brif Set a new RSSI threshold for this power control loop. Will be used to
 * limit TPCs, see also nr_limit_tpc().
 *
 * @param pc the power control loop
 * @param rssi_threshold the RSSI threshold
 */
void nr_mac_set_rssi_threshold(nr_power_control_t *pc, int rssi_threshold)
{
  pc->rssi_threshold = rssi_threshold;
}

/**
 * @brief Signal that this power control loop experienced a discontinuous
 * transmission (DTC), aka "no reception".. This will decrease the "TPC in
 * flight" to artificially reduce the current SNR, which will trigger a TPC
 * command to increase the power. Repeated DTX will lead to more TPC.
 *
 * @param pc the power control loop
 */
void nr_mac_signal_dtx(nr_power_control_t *pc)
{
  // repeated DTX will make nr_mac_get_tpc() return positive TPCs, see
  // nr_mac_pc_snr(). The more DTX, the more TPC increase, which will
  // eventually zero out and return back to target SNR (change is temporary).
  // Also, this will only send positive TPC changes.
  pc->tpc_in_flight = max(pc->tpc_in_flight - 1.0f, -5.0f); // TODO threshold good?
}

/**
 * @brief Get a transmit power control (TPC) command for the given power
 * control loop according to current and target SNR. This function can be
 * called continuously on every UL transmission occasion, and keeps track of
 * past TPC commands. It also limits TPC commands according to RSSI, see
 * nr_limit_tpc() for more information.
 *
 * @param pc the power control loop
 * @return the next TPC command.
 */
int nr_mac_get_tpc(nr_power_control_t *pc)
{
  float diff = get_snr_diff(pc);
  int tpc = 1;
  if (diff <= -3.0f) {
    tpc = 3; // increase 3dB
  } else if (diff <= -1.0f) {
    tpc = 2; // increase 1dB
  } else if (diff >= 2.0f) {
    tpc = 0; // decrease 1dB
  } else {
    tpc = 1; // within -1<=target<=2dB => is ok.
  }
  tpc = nr_limit_tpc(tpc, pc->avg_rssi, pc->rssi_threshold);
  int change = tpc_to_db(tpc);
  pc->tpc_in_flight += change;
  LOG_D(NR_MAC, "tpc %d => %d\n", tpc, change);
  return tpc;
}
