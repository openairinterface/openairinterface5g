/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief procedures related to UE
 */


#include <stdio.h>
#include <math.h>

/* exe */
#include "executables/nr-softmodem.h"

/* RRC*/
#include "RRC/NR_UE/L2_interface_ue.h"

/* MAC */
#include "LAYER2/NR_MAC_COMMON/nr_mac.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/NR_UE_PHY_INTERFACE/NR_Packet_Drop.h"

/* PHY */
#include "executables/softmodem-common.h"

/* utils */
#include "assertions.h"
#include "bits.h"
#include "oai_asn1.h"
#include "common/utils/LOG/log.h"
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"

// #define DEBUG_MIB
// #define ENABLE_MAC_PAYLOAD_DEBUG 1
// #define DEBUG_RAR

// table 7.2-1 TS 38.321
const uint16_t table_7_2_1[16] = {
    5,    // row index 0
    10,   // row index 1
    20,   // row index 2
    30,   // row index 3
    40,   // row index 4
    60,   // row index 5
    80,   // row index 6
    120,  // row index 7
    160,  // row index 8
    240,  // row index 9
    320,  // row index 10
    480,  // row index 11
    960,  // row index 12
    1920, // row index 13
};

typedef struct {
  uint32_t ssb_index;
  short ssb_rsrp_dBm;
  float_t ssb_sinr_dB;
} NR_RSRP_meas_t;

/* TS 36.213 Table 9.2.3-3: Mapping of values for one HARQ-ACK bit to sequences */
static const int sequence_cyclic_shift_1_harq_ack_bit[2]
/*        HARQ-ACK Value        0    1 */
/* Sequence cyclic shift */ = { 0,   6 };

/* TS 36.213 Table 9.2.5-1: Mapping of values for one HARQ-ACK bit and positive SR to sequences */
static const int sequence_cyclic_shift_1_harq_ack_bit_positive_sr[2]
/*        HARQ-ACK Value        0    1 */
/* Sequence cyclic shift */ = { 3,   9 };

/* TS 36.213 Table 9.2.5-2: Mapping of values for two HARQ-ACK bits and positive SR to sequences */
static const int sequence_cyclic_shift_2_harq_ack_bits_positive_sr[4]
/*        HARQ-ACK Value      (0,0)  (0,1)   (1,0)  (1,1) */
/* Sequence cyclic shift */ = {  1,     4,     10,     7 };

/* TS 38.213 Table 9.2.3-4: Mapping of values for two HARQ-ACK bits to sequences */
static const int sequence_cyclic_shift_2_harq_ack_bits[4]
/*        HARQ-ACK Value       (0,0)  (0,1)  (1,0)  (1,1) */
/* Sequence cyclic shift */ = {   0,     3,     9,     6 };

static int get_pucch0_mcs(const int O_ACK, const int O_SR, const int ack_payload, const int sr_payload)
{
  int mcs = 0;
  if (O_SR == 0 || sr_payload == 0) { /* only ack is transmitted TS 36.213 9.2.3 UE procedure for reporting HARQ-ACK */
    if (O_ACK == 1)
      mcs = sequence_cyclic_shift_1_harq_ack_bit[ack_payload & 0x1]; /* only harq of 1 bit */
    else
      mcs = sequence_cyclic_shift_2_harq_ack_bits[ack_payload & 0x3]; /* only harq with 2 bits */
  } else { /* SR + eventually ack are transmitted TS 36.213 9.2.5.1 UE procedure for multiplexing HARQ-ACK or CSI and SR */
    if (sr_payload == 1) { /* positive scheduling request */
      if (O_ACK == 1)
        mcs = sequence_cyclic_shift_1_harq_ack_bit_positive_sr[ack_payload & 0x1]; /* positive SR and harq of 1 bit */
      else if (O_ACK == 2)
        mcs = sequence_cyclic_shift_2_harq_ack_bits_positive_sr[ack_payload & 0x3]; /* positive SR and harq with 2 bits */
      else
        mcs = 0; /* only positive SR */
    }
  }
  return mcs;
}

/* TS 36.213 Table 9.2.1-1: PUCCH resource sets before dedicated PUCCH resource configuration */
const initial_pucch_resource_t initial_pucch_resource[16] = {
/*              format           first symbol     Number of symbols        PRB offset    nb index for       set of initial CS */
/*  0  */ {  0,      12,                  2,                   0,            2,       {    0,   3,    0,    0  }   },
/*  1  */ {  0,      12,                  2,                   0,            3,       {    0,   4,    8,    0  }   },
/*  2  */ {  0,      12,                  2,                   3,            3,       {    0,   4,    8,    0  }   },
/*  3  */ {  1,      10,                  4,                   0,            2,       {    0,   6,    0,    0  }   },
/*  4  */ {  1,      10,                  4,                   0,            4,       {    0,   3,    6,    9  }   },
/*  5  */ {  1,      10,                  4,                   2,            4,       {    0,   3,    6,    9  }   },
/*  6  */ {  1,      10,                  4,                   4,            4,       {    0,   3,    6,    9  }   },
/*  7  */ {  1,       4,                 10,                   0,            2,       {    0,   6,    0,    0  }   },
/*  8  */ {  1,       4,                 10,                   0,            4,       {    0,   3,    6,    9  }   },
/*  9  */ {  1,       4,                 10,                   2,            4,       {    0,   3,    6,    9  }   },
/* 10  */ {  1,       4,                 10,                   4,            4,       {    0,   3,    6,    9  }   },
/* 11  */ {  1,       0,                 14,                   0,            2,       {    0,   6,    0,    0  }   },
/* 12  */ {  1,       0,                 14,                   0,            4,       {    0,   3,    6,    9  }   },
/* 13  */ {  1,       0,                 14,                   2,            4,       {    0,   3,    6,    9  }   },
/* 14  */ {  1,       0,                 14,                   4,            4,       {    0,   3,    6,    9  }   },
/* 15  */ {  1,       0,                 14,                   0,            4,       {    0,   3,    6,    9  }   },
};

int get_rnti_type(const NR_UE_MAC_INST_t *mac, const uint16_t rnti)
{
  const RA_config_t *ra = &mac->ra;
  nr_rnti_type_t rnti_type;

  if (rnti == ra->ra_rnti) {
    rnti_type = TYPE_RA_RNTI_;
  } else if (rnti == ra->MsgB_rnti && (ra->ra_state == nrRA_WAIT_MSGB || ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION)) {
    rnti_type = TYPE_MSGB_RNTI_;
  } else if (rnti == ra->t_crnti && (ra->ra_state == nrRA_WAIT_RAR || ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION)) {
    rnti_type = TYPE_TC_RNTI_;
  } else if (rnti == mac->crnti) {
    rnti_type = TYPE_C_RNTI_;
  } else if (rnti == 0xFFFE) {
    rnti_type = TYPE_P_RNTI_;
  } else if (rnti == 0xFFFF) {
    rnti_type = TYPE_SI_RNTI_;
  } else {
    AssertFatal(1 == 0, "Not identified/handled rnti %d \n", rnti);
  }
  LOG_D(MAC, "Returning rnti_type %s \n", rnti_types(rnti_type));
  return rnti_type;
}

void nr_ue_decode_mib(NR_UE_MAC_INST_t *mac, int cc_id)
{
  LOG_D(MAC,"[L2][MAC] decode mib\n");

  if (mac->mib->cellBarred == NR_MIB__cellBarred_barred) {
    LOG_W(MAC, "Cell is barred. Going back to sync mode.\n");
    mac->synch_request.Mod_id = mac->ue_id;
    mac->synch_request.CC_id = cc_id;
    mac->synch_request.synch_req.target_Nid_cell = -1;
    mac->if_module->synch_request(&mac->synch_request);
    return;
  }

  uint16_t frame = (mac->mib->systemFrameNumber.buf[0] >> mac->mib->systemFrameNumber.bits_unused);
  uint16_t frame_number_4lsb = 0;

  int extra_bits = mac->mib_additional_bits;
  for (int i = 0; i < 4; i++)
    frame_number_4lsb |= ((extra_bits >> i) & 1) << (3 - i);

  uint8_t ssb_subcarrier_offset_msb = (extra_bits >> 5) & 0x1;    //	extra bits[5]
  uint8_t ssb_subcarrier_offset = (uint8_t)mac->mib->ssb_SubcarrierOffset;

  frame = frame << 4;
  mac->mib_frame = frame | frame_number_4lsb;
  if (mac->frequency_range == FR1) {
    if(ssb_subcarrier_offset_msb)
      ssb_subcarrier_offset = ssb_subcarrier_offset | 0x10;
  }

#ifdef DEBUG_MIB
  uint8_t half_frame_bit = (extra_bits >> 4) & 0x1; //	extra bits[4]
  LOG_I(MAC,"system frame number(6 MSB bits): %d\n",  mac->mib->systemFrameNumber.buf[0]);
  LOG_I(MAC,"system frame number(with LSB): %d\n", (int) mac->mib_frame);
  LOG_I(MAC,"subcarrier spacing (0=15or60, 1=30or120): %d\n", (int)mac->mib->subCarrierSpacingCommon);
  LOG_I(MAC,"ssb carrier offset(with MSB):  %d\n", (int)ssb_subcarrier_offset);
  LOG_I(MAC,"dmrs type A position (0=pos2,1=pos3): %d\n", (int)mac->mib->dmrs_TypeA_Position);
  LOG_I(MAC,"controlResourceSetZero: %d\n", (int)mac->mib->pdcch_ConfigSIB1.controlResourceSetZero);
  LOG_I(MAC,"searchSpaceZero: %d\n", (int)mac->mib->pdcch_ConfigSIB1.searchSpaceZero);
  LOG_I(MAC,"cell barred (0=barred,1=notBarred): %d\n", (int)mac->mib->cellBarred);
  LOG_I(MAC,"intra frequency reselection (0=allowed,1=notAllowed): %d\n", (int)mac->mib->intraFreqReselection);
  LOG_I(MAC,"half frame bit(extra bits):    %d\n", (int)half_frame_bit);
  LOG_I(MAC,"ssb index(extra bits):         %d\n", (int)mac->mib_ssb);
#endif

  mac->ssb_subcarrier_offset = ssb_subcarrier_offset;
  mac->dmrs_TypeA_Position = mac->mib->dmrs_TypeA_Position;

}

static void configure_ratematching_csi(fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu,
                                       const fapi_nr_dl_config_request_t *dl_config,
                                       int rnti_type,
                                       int frame,
                                       int slot,
                                       int mu,
                                       int slots_per_frame,
                                       const NR_PDSCH_Config_t *pdsch_config,
                                       NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  // only for C-RNTI, MCS-C-RNTI, CS-RNTI (and only C-RNTI is supported for now)
  if (rnti_type != TYPE_C_RNTI_)
    return;

  if (pdsch_config && pdsch_config->zp_CSI_RS_ResourceToAddModList) {
    bool found = false;
    const NR_SetupRelease_ZP_CSI_RS_ResourceSet_t *zp_set = pdsch_config->p_ZP_CSI_RS_ResourceSet;
    AssertFatal(zp_set && zp_set->choice.setup, "Only periodic ZP resource set is implemented\n");
    const NR_ZP_CSI_RS_Resource_t *zp_res = NULL;
    for (int j = 0; j < zp_set->choice.setup->zp_CSI_RS_ResourceIdList.list.count; j++) {
      for (int i = 0; i < pdsch_config->zp_CSI_RS_ResourceToAddModList->list.count; i++) {
        zp_res = pdsch_config->zp_CSI_RS_ResourceToAddModList->list.array[i];
        NR_ZP_CSI_RS_ResourceId_t id = zp_res->zp_CSI_RS_ResourceId;
        if (*zp_set->choice.setup->zp_CSI_RS_ResourceIdList.list.array[j] == id) {
          found = true;
          break;
        }
      }
      AssertFatal(found, "Couldn't find periodic ZP resouce in set\n");
      AssertFatal(zp_res->periodicityAndOffset, "periodicityAndOffset cannot be null for periodic ZP resource\n");
      int period, offset;
      csi_period_offset(NULL, zp_res->periodicityAndOffset, &period, &offset);
      if (!is_periodic_occasion(frame, slot, slots_per_frame, period, offset))
        continue;
      AssertFatal(dlsch_pdu->numCsiRsForRateMatching < NFAPI_MAX_NUM_CSI_RATEMATCH, "csiRsForRateMatching out of bounds\n");
      fapi_nr_dl_config_csirs_pdu_rel15_t *csi_pdu = &dlsch_pdu->csiRsForRateMatching[dlsch_pdu->numCsiRsForRateMatching];
      csi_pdu->csi_type = 2; // ZP-CSI
      csi_pdu->subcarrier_spacing = mu;
      configure_csi_resource_mapping(csi_pdu, &zp_res->resourceMapping, dlsch_pdu->BWPSize, dlsch_pdu->BWPStart);
      dlsch_pdu->numCsiRsForRateMatching++;
    }
  }

  // Handle NZP CSI-RS for rate matching
  if (csi_MeasConfig && csi_MeasConfig->nzp_CSI_RS_ResourceToAddModList) {
    for (int i = 0; i < csi_MeasConfig->nzp_CSI_RS_ResourceToAddModList->list.count; i++) {
      NR_NZP_CSI_RS_Resource_t *nzp_res = csi_MeasConfig->nzp_CSI_RS_ResourceToAddModList->list.array[i];
      if (nzp_res->periodicityAndOffset) {
        int period, offset;
        csi_period_offset(NULL, nzp_res->periodicityAndOffset, &period, &offset);
        if (!is_periodic_occasion(frame, slot, slots_per_frame, period, offset))
          continue;
        AssertFatal(dlsch_pdu->numCsiRsForRateMatching < NFAPI_MAX_NUM_CSI_RATEMATCH, "csiRsForRateMatching out of bounds\n");
        fapi_nr_dl_config_csirs_pdu_rel15_t *csi_pdu = &dlsch_pdu->csiRsForRateMatching[dlsch_pdu->numCsiRsForRateMatching];
        csi_pdu->csi_type = 1; // NZP CSI-RS
        csi_pdu->subcarrier_spacing = mu;
        configure_csi_resource_mapping(csi_pdu, &nzp_res->resourceMapping, dlsch_pdu->BWPSize, dlsch_pdu->BWPStart);
        dlsch_pdu->numCsiRsForRateMatching++;
      }
    }
  }

  for (int i = 0; i < dl_config->number_pdus; i++) {
    // This assumes that CSI-RS are scheduled before this moment which is true in current implementation
    const fapi_nr_dl_config_request_pdu_t *csi_req = &dl_config->dl_config_list[i];
    if (csi_req->pdu_type == FAPI_NR_DL_CONFIG_TYPE_CSI_RS) {
      AssertFatal(dlsch_pdu->numCsiRsForRateMatching < NFAPI_MAX_NUM_CSI_RATEMATCH, "csiRsForRateMatching out of bounds\n");
      dlsch_pdu->csiRsForRateMatching[dlsch_pdu->numCsiRsForRateMatching] = csi_req->csirs_config_pdu.csirs_config_rel15;
      dlsch_pdu->numCsiRsForRateMatching++;
    }
  }
}

void nr_ue_decode_BCCH_DL_SCH(NR_UE_MAC_INST_t *mac,
                              unsigned int gNB_index,
                              uint8_t ack_nack,
                              uint8_t *pduP,
                              uint32_t pdu_len,
                              int hfn,
                              int frame,
                              int slot)
{
  if(ack_nack) {
    LOG_D(NR_MAC, "Decoding NR-BCCH-DL-SCH-Message (SIB1 or SI)\n");
    nr_mac_rrc_data_ind_ue(mac->ue_id, gNB_index, hfn, frame, slot, mac->physCellId, 0, NR_BCCH_DL_SCH, (uint8_t *) pduP, pdu_len);
    if (mac->get_sib1)
      mac->get_sib1 = false;
    for (int i = 0; i < MAX_SI_GROUPS; i++) {
      if (mac->get_otherSI[i])
        mac->get_otherSI[i] = false;
    }
    mac->si_SchedInfo.si_window_start = -1;
    T(T_NRUE_MAC_DL_PDU_WITH_DATA,
      T_INT(SI_RNTI),
      T_INT(-1 /* frame, unavailable here */),
      T_INT(-1 /* slot, unavailable here */),
      T_INT(0 /* harq_pid */),
      T_BUFFER(pduP, pdu_len));
  }
  else {
    LOG_E(NR_MAC, "Got NACK on NR-BCCH-DL-SCH-Message (%s)\n", mac->get_sib1 ? "SIB1" : "other SI");
    nr_mac_rrc_data_ind_ue(mac->ue_id, gNB_index, hfn, frame, slot, mac->physCellId, 0, NR_BCCH_DL_SCH, NULL, 0);
  }
}

/*
 * This code contains all the functions needed to process all dci fields.
 * These tables and functions are going to be called by function nr_ue_process_dci
 */

static inline int writeBit(uint8_t *bitmap, int offset, int value, int size)
{
  for (int i = offset; i < offset + size; i++)
    bitmap[i / 8] |= value << (i % 8);
  return size;
}

// 38.214 Section 5.1.3.1
static uint8_t get_dlsch_mcs_table(nr_dci_format_t format,
                                   nr_rnti_type_t rnti_type,
                                   int ss_type,
                                   long *mcs_Table,
                                   long *sps_mcs_Table,
                                   bool sps_transmission,
                                   long *mcs_C_RNTI)
{
  // TODO procedures for SPS-Config (semi-persistent scheduling) not implemented
  if (mcs_Table && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam256 && format == NR_DL_DCI_FORMAT_1_1 && rnti_type == TYPE_C_RNTI_)
    return 1; // Table 5.1.3.1-2: MCS index table 2 for PDSCH
  else if (!mcs_C_RNTI
           && mcs_Table
           && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam64LowSE
           && ss_type == NR_SearchSpace__searchSpaceType_PR_ue_Specific
           && rnti_type == TYPE_C_RNTI_)
    return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  else if (mcs_C_RNTI && rnti_type == TYPE_MCS_C_RNTI_)
    return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  else if (!sps_mcs_Table && mcs_Table && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam256) {
    if ((format == NR_DL_DCI_FORMAT_1_1 && rnti_type == TYPE_CS_RNTI_) || sps_transmission)
      return 1; // Table 5.1.3.1-2: MCS index table 2 for PDSCH
  }
  else if (sps_mcs_Table) {
    if (rnti_type == TYPE_CS_RNTI_ || sps_transmission)
      return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  }
  // otherwise
  return 0; // Table 5.1.3.1-1: MCS index table 1 for PDSCH
}

int8_t nr_ue_process_dci_freq_dom_resource_assignment(nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                                                      fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu,
                                                      NR_PDSCH_Config_t *pdsch_Config,
                                                      uint16_t n_RB_ULBWP,
                                                      uint16_t n_RB_DLBWP,
                                                      int start_DLBWP,
                                                      dci_field_t frequency_domain_assignment)
{

  /*
   * TS 38.214 subclause 5.1.2.2 Resource allocation in frequency domain (downlink)
   * when the scheduling grant is received with DCI format 1_0, then downlink resource allocation type 1 is used
   */
  if(dlsch_config_pdu != NULL) {
    if (pdsch_Config &&
        pdsch_Config->resourceAllocation == NR_PDSCH_Config__resourceAllocation_resourceAllocationType0) {
      // TS 38.214 subclause 5.1.2.2.1 Downlink resource allocation type 0
      dlsch_config_pdu->resource_alloc = 0;
      uint8_t *rb_bitmap = dlsch_config_pdu->rb_bitmap;
      memset(rb_bitmap, 0, sizeof(dlsch_config_pdu->rb_bitmap));
      int P = getRBGSize(n_RB_DLBWP, pdsch_Config->rbg_Size);
      int currentBit = 0;

      // write first bit sepcial case
      const int n_RBG = frequency_domain_assignment.nbits;
      int first_bit_rbg = (frequency_domain_assignment.val >> (n_RBG - 1)) & 0x01;
      currentBit += writeBit(rb_bitmap, currentBit, first_bit_rbg, P - (start_DLBWP % P));

      // write all bits until last bit special case
      for (int i = 1; i < n_RBG - 1; i++) {
        // The order of RBG bitmap is such that RBG 0 to RBG n_RBG − 1 are mapped from MSB to LSB
        int bit_rbg = (frequency_domain_assignment.val >> (n_RBG - 1 - i)) & 0x01;
        currentBit += writeBit(rb_bitmap, currentBit, bit_rbg, P);
      }

      // write last bit (only if more than 1 RBG)
      if (n_RBG > 1) {
        int last_bit_rbg = frequency_domain_assignment.val & 0x01;
        const int tmp = (start_DLBWP + n_RB_DLBWP) % P;
        int last_RBG = tmp ? tmp : P;
        writeBit(rb_bitmap, currentBit, last_bit_rbg, last_RBG);
      }
    } else if (pdsch_Config && pdsch_Config->resourceAllocation == NR_PDSCH_Config__resourceAllocation_dynamicSwitch)
      AssertFatal(false, "DLSCH dynamic switch allocation not yet supported\n");
    else {
      // TS 38.214 subclause 5.1.2.2.2 Downlink resource allocation type 1
      dlsch_config_pdu->resource_alloc = 1;
      int riv = frequency_domain_assignment.val;
      dlsch_config_pdu->number_rbs = NRRIV2BW(riv,n_RB_DLBWP);
      dlsch_config_pdu->start_rb   = NRRIV2PRBOFFSET(riv,n_RB_DLBWP);

      // Sanity check in case a false or erroneous DCI is received
      if ((dlsch_config_pdu->number_rbs < 1) || (dlsch_config_pdu->number_rbs > n_RB_DLBWP - dlsch_config_pdu->start_rb)) {
        // DCI is invalid!
        LOG_W(MAC, "Frequency domain assignment values are invalid! #RBs: %d, Start RB: %d, n_RB_DLBWP: %d \n", dlsch_config_pdu->number_rbs, dlsch_config_pdu->start_rb, n_RB_DLBWP);
        return -1;
      }
      LOG_D(MAC,"DLSCH riv = %i\n", riv);
      LOG_D(MAC,"DLSCH n_RB_DLBWP = %i\n", n_RB_DLBWP);
      LOG_D(MAC,"DLSCH number_rbs = %i\n", dlsch_config_pdu->number_rbs);
      LOG_D(MAC,"DLSCH start_rb = %i\n", dlsch_config_pdu->start_rb);
    }
  }
  if(pusch_config_pdu != NULL) {
    /*
     * TS 38.214 subclause 6.1.2.2 Resource allocation in frequency domain (uplink)
     */
    /*
     * TS 38.214 subclause 6.1.2.2.1 Uplink resource allocation type 0
     */
    /*
     * TS 38.214 subclause 6.1.2.2.2 Uplink resource allocation type 1
     */
    int riv = frequency_domain_assignment.val;
    pusch_config_pdu->rb_size  = NRRIV2BW(riv,n_RB_ULBWP);
    pusch_config_pdu->rb_start = NRRIV2PRBOFFSET(riv,n_RB_ULBWP);

    // Sanity check in case a false or erroneous DCI is received
    if ((pusch_config_pdu->rb_size < 1) || (pusch_config_pdu->rb_size > n_RB_ULBWP - pusch_config_pdu->rb_start)) {
      // DCI is invalid!
      LOG_W(MAC, "Frequency domain assignment values are invalid! #RBs: %d, Start RB: %d, n_RB_ULBWP: %d \n",pusch_config_pdu->rb_size, pusch_config_pdu->rb_start, n_RB_ULBWP);
      return -1;
    }
  }
  return 0;
}

/* TS 38.213 9.2.3: UE has PUCCH-ResourceSet configured in PUCCH-Config */
static inline bool nr_ue_has_dedicated_pucch_resource_set(const NR_PUCCH_Config_t *cfg)
{
  return cfg && cfg->resourceSetToAddModList && cfg->resourceSetToAddModList->list.array[0];
}

static void set_harq_status(NR_UE_MAC_INST_t *mac,
                            uint8_t pucch_id,
                            uint8_t harq_id,
                            int cw_id,
                            int8_t delta_pucch,
                            uint16_t data_toul_fb,
                            uint8_t dai,
                            int n_CCE,
                            int N_CCE,
                            frame_t frame,
                            int slot)
{
  NR_UE_DL_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[harq_id][cw_id];
  const NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP ? mac->current_UL_BWP->pucch_Config : NULL;
  const NR_PUCCH_ConfigCommon_t *pucch_ConfigCommon = mac->current_UL_BWP ? mac->current_UL_BWP->pucch_ConfigCommon : NULL;
  current_harq->active = true;
  current_harq->ack_received = false;
  current_harq->pucch_resource_indicator = pucch_id;
  // TS 38.213: PRI applies to the resource set at DCI detection
  // No dedicated PUCCH-ResourceSet yet: freeze pucch-ResourceCommon (9.2.1), else -1 (9.2.3)
  if (!nr_ue_has_dedicated_pucch_resource_set(pucch_Config)) {
    AssertFatal(pucch_ConfigCommon && pucch_ConfigCommon->pucch_ResourceCommon,
                "pucch_ResourceCommon required for Table 9.2.1-1 HARQ-ACK\n");
    current_harq->pucch_ResourceCommon = *pucch_ConfigCommon->pucch_ResourceCommon;
  } else {
    current_harq->pucch_ResourceCommon = -1;
  }
  current_harq->n_CCE = n_CCE;
  current_harq->N_CCE = N_CCE;
  current_harq->dai_cumul = 0;
  current_harq->delta_pucch = delta_pucch;
  // FIXME k0 != 0 currently not taken into consideration
  int slots_per_frame = mac->frame_structure.numb_slots_frame;
  current_harq->ul_frame = frame;
  current_harq->ul_slot = slot + data_toul_fb;
  if (current_harq->ul_slot >= slots_per_frame) {
    current_harq->ul_frame = (frame + current_harq->ul_slot / slots_per_frame) % MAX_FRAME_NUMBER;
    current_harq->ul_slot %= slots_per_frame;
  }
  // counter DAI in DCI ranges from 0 to 3
  // we might have more than 4 HARQ processes to report per PUCCH
  // we need to keep track of how many DAI we received in a slot (dai_cumul) despite the modulo operation
  int highest_dai = -1;
  int temp_dai = dai;
  const int num_dl_harq = get_nrofHARQ_ProcessesForPDSCH(&mac->sc_info);
  for (int i = 0; i < num_dl_harq; i++) {
    // looking for other active HARQ processes with feedback in the same frame/slot
    if (i == harq_id)
      continue;
    for (int c = 0; c < 2; c++) {
      NR_UE_DL_HARQ_STATUS_t *harq = &mac->dl_harq_info[i][c];
      if (harq->active &&
          harq->ul_frame == current_harq->ul_frame &&
          harq->ul_slot == current_harq->ul_slot) {
        // highest_dai is the largest cumulative dai in the set of HARQ allocations for a given slot
        if (harq->dai_cumul > highest_dai)
          highest_dai = harq->dai_cumul - 1;
      }
    }
  }

  current_harq->dai_cumul = temp_dai + 1;  // DAI = 0 (temp_dai) corresponds to 1st assignment and so on
  // if temp_dai is less or equal than cumulative highest dai for given slot
  // it's an indication dai was reset due to modulo 4 operation
  if (temp_dai <= highest_dai) {
    int mod4_count = (highest_dai + 1) / 4; // to take into account how many times dai wrapped up (modulo 4)
    current_harq->dai_cumul += (mod4_count * 4);
  }
  LOG_D(NR_MAC,
        "Setting harq_status for harq_id %d, dl %d.%d, sched ul %d.%d fb time %d total dai %d\n",
        harq_id,
        frame,
        slot,
        current_harq->ul_frame,
        current_harq->ul_slot,
        data_toul_fb,
        current_harq->dai_cumul);
}

static int nr_ue_process_dci_ul_00(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    10 FREQ_DOM_RESOURCE_ASSIGNMENT_UL: PUSCH hopping with resource allocation type 1 not considered
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 6.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    17 FREQ_HOPPING_FLAG: 0 bit if only resource allocation type 0
   *    24 MCS:
   *    25 NDI:
   *    26 RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    32 TPC_PUSCH:
   *    49 PADDING_NR_DCI: (Note 2) If DCI format 0_0 is monitored in common search space
   *    50 SUL_IND_0_0:
   */
  // Calculate the slot in which ULSCH should be scheduled. This is current slot + K2,
  // where K2 is the offset between the slot in which UL DCI is received and the slot
  // in which ULSCH should be scheduled. K2 is configured in RRC configuration.
  // todo:
  // - SUL_IND_0_0

  // Schedule PUSCH
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_ul_tda_info(mac->current_UL_BWP,
                                           coreset_type,
                                           dci_ind->ss_type,
                                           get_rnti_type(mac, dci_ind->rnti),
                                           dci->time_domain_assignment.val);

  if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0)
    return -1;

  frame_t frame_tx;
  int slot_tx;
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, mac->current_UL_BWP->scs);
  if (-1 == nr_ue_pusch_scheduler(mac, 0, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset)) {
    LOG_E(MAC, "Cannot schedule PUSCH\n");
    return -1;
  }

  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
  if (!pdu)
    return -1;

  int ret = nr_config_pusch_pdu(mac,
                                &tda_info,
                                &pdu->pusch_config_pdu,
                                dci,
                                NULL,
                                NULL,
                                dci_ind->rnti,
                                dci_ind->ss_type,
                                NR_UL_DCI_FORMAT_0_0);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
  return ret;
}

static int nr_ue_process_dci_ul_01(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or SP-CSI-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    1  CARRIER_IND
   *    2  SUL_IND_0_1
   *    7  BANDWIDTH_PART_IND
   *    10 FREQ_DOM_RESOURCE_ASSIGNMENT_UL: PUSCH hopping with resource allocation type 1 not considered
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 6.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    17 FREQ_HOPPING_FLAG: 0 bit if only resource allocation type 0
   *    24 MCS:
   *    25 NDI:
   *    26 RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    29 FIRST_DAI
   *    30 SECOND_DAI
   *    32 TPC_PUSCH:
   *    36 SRS_RESOURCE_IND:
   *    37 PRECOD_NBR_LAYERS:
   *    38 ANTENNA_PORTS:
   *    40 SRS_REQUEST:
   *    42 CSI_REQUEST:
   *    43 CBGTI
   *    45 PTRS_DMRS
   *    46 BETA_OFFSET_IND
   *    47 DMRS_SEQ_INI
   *    48 UL_SCH_IND
   *    49 PADDING_NR_DCI: (Note 2) If DCI format 0_0 is monitored in common search space
   */
  // TODO:
  // - FIRST_DAI
  // - SECOND_DAI
  // - SRS_RESOURCE_IND

  /* CSI_REQUEST */
  long csi_K2 = -1;
  nfapi_nr_ue_csi_payload_t csi_report = {0};
  if (dci->csi_request.nbits > 0 && dci->csi_request.val > 0)
    csi_report = nr_ue_aperiodic_csi_reporting(mac, dci->csi_request, dci->time_domain_assignment.val, &csi_K2);

  /* SRS_REQUEST */
  AssertFatal(dci->srs_request.nbits == 2, "If SUL is supported in the cell, there is an additional bit in SRS request field\n");
  if (dci->srs_request.val > 0)
    nr_ue_aperiodic_srs_scheduling(mac, dci->srs_request.val, frame, slot);

  // Schedule PUSCH
  frame_t frame_tx;
  int slot_tx;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_ul_tda_info(mac->current_UL_BWP,
                                           coreset_type,
                                           dci_ind->ss_type,
                                           get_rnti_type(mac, dci_ind->rnti),
                                           dci->time_domain_assignment.val);

  if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0)
    return -1;

  if (dci->ulsch_indicator == 0) {
    // in case of CSI on PUSCH and no ULSCH we need to use reportSlotOffset in trigger state
    if (csi_K2 <= 0) {
      LOG_E(MAC, "Invalid CSI K2 value %ld\n", csi_K2);
      return -1;
    }
    tda_info.k2 = csi_K2;
  }

  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, mac->current_UL_BWP->scs);
  if (-1 == nr_ue_pusch_scheduler(mac, 0, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset)) {
    LOG_E(MAC, "Cannot schedule PUSCH\n");
    return -1;
  }

  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
  if (!pdu)
    return -1;
  int ret = nr_config_pusch_pdu(mac,
                                &tda_info,
                                &pdu->pusch_config_pdu,
                                dci,
                                &csi_report,
                                NULL,
                                dci_ind->rnti,
                                dci_ind->ss_type,
                                NR_UL_DCI_FORMAT_0_1);
  LOG_D(NR_MAC_DCI,
      "add ul dci harq %d for %d.%d %d.%d round %d\n",
        pdu->pusch_config_pdu.pusch_data.harq_process_id,
        frame,
        slot,
        frame_tx,
        slot_tx,
        0);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
  return ret;
}

// Table 7.3.1.3-1 of 38.211
static int get_nl_for_cw(int Nl, int cw_idx)
{
  AssertFatal(Nl >= 0 && Nl <= 8, "Invalid number of layers %d\n", Nl);
  if (Nl < 5)
    return Nl;
  else {
    if (cw_idx == 0)
      return Nl / 2;
    else
      return Nl / 2 + Nl % 2;
  }
}

static bool get_cw_info(NR_UE_DL_HARQ_STATUS_t *current_harq,
                        fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu,
                        uint8_t pdu_type,
                        fapi_nr_dl_cw_info_t *cw_info,
                        int number_rbs,
                        int nb_re_dmrs,
                        int nb_rb_oh,
                        int rv,
                        int mcs,
                        int ndi,
                        int cw_idx)
{
  uint8_t Nl = 0;
  for (int i = 0; i < 12; i++) { // max 12 ports
    if ((dlsch_pdu->dmrs_ports >> i) & 0x01)
      Nl += 1;
  }

  cw_info->mcs = mcs;
  /* RV for transport block */
  cw_info->rv = rv;
  /* NDI for transport block*/
  if (pdu_type == FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH || pdu_type == FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH
      || pdu_type == FAPI_NR_DL_CONFIG_TYPE_P_DLSCH || ndi != current_harq->last_ndi) {
    // new data
    cw_info->new_data_indicator = true;
    current_harq->R = 0;
    current_harq->TBS = 0;
  } else
    cw_info->new_data_indicator = false;
  if (pdu_type != FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH && pdu_type != FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH
      && pdu_type != FAPI_NR_DL_CONFIG_TYPE_P_DLSCH) {
    current_harq->last_ndi = ndi;
    if (cw_info->new_data_indicator)
      current_harq->round = 0;
    else
      current_harq->round++;
  }
  cw_info->qamModOrder = nr_get_Qm_dl(cw_info->mcs, dlsch_pdu->mcs_table);
  if (cw_info->qamModOrder == 0) {
    LOG_W(NR_MAC, "Invalid code rate or Mod order, likely due to unexpected DL DCI\n");
    return false;
  }

  cw_info->Nl = get_nl_for_cw(Nl, cw_idx);
  int R = nr_get_code_rate_dl(cw_info->mcs, dlsch_pdu->mcs_table);
  if (R > 0) {
    cw_info->targetCodeRate = R;
    cw_info->TBS = nr_compute_tbs(cw_info->qamModOrder,
                                  R,
                                  number_rbs,
                                  dlsch_pdu->number_symbols,
                                  nb_re_dmrs * get_num_dmrs(dlsch_pdu->dlDmrsSymbPos),
                                  nb_rb_oh,
                                  0,
                                  cw_info->Nl);
    // storing for possible retransmissions
    if (!cw_info->new_data_indicator && current_harq->TBS != cw_info->TBS) {
      LOG_W(NR_MAC,
            "NDI indicates re-transmission but computed TBS %d doesn't match with what previously stored %d\n",
            cw_info->TBS,
            current_harq->TBS);
      cw_info->new_data_indicator = true; // treated as new data
    }
    current_harq->R = cw_info->targetCodeRate;
    current_harq->TBS = cw_info->TBS;
  }
  else {
    cw_info->targetCodeRate = current_harq->R;
    cw_info->TBS = current_harq->TBS;
  }

  if (cw_info->TBS == 0) {
    LOG_E(MAC, "Invalid TBS = 0. Probably caused by missed detection of DCI\n");
    return false;
  }
  cw_info->ldpcBaseGraph = get_BG(cw_info->TBS, cw_info->targetCodeRate);

  return true;
}

/* Counterpart of the UL accumulation in nr_ue_dl_scheduler(), so that the DL line of
 * print_ue_mac_stats() can report the same averages. Weighted by TBS like the UL side, and
 * accumulated on every grant including retransmissions so the per-TB averages divide by the
 * same round total the print uses. */
static void accumulate_dl_stats(NR_UE_MAC_INST_t *mac,
                                const fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu,
                                const fapi_nr_dl_cw_info_t *cw_info,
                                int number_rbs)
{
  int bits = cw_info->TBS;
  mac->stats.dl.total_bits += bits;
  mac->stats.dl.target_code_rate += (uint64_t)cw_info->targetCodeRate * bits;
  if (cw_info->qamModOrder)
    mac->stats.dl.total_symbols += bits / cw_info->qamModOrder;
  mac->stats.dl.rb_size += number_rbs;
  mac->stats.dl.nr_of_symbols += dlsch_pdu->number_symbols;
}

static int nr_ue_process_dci_dl_10_p_rnti(NR_UE_MAC_INST_t *mac,
                                          frame_t frame,
                                          int slot,
                                          const dci_pdu_rel15_t *dci,
                                          fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu,
                                          const NR_UE_DL_BWP_t *current_DL_BWP)
{
  static NR_UE_DL_HARQ_STATUS_t prnti_harq;

  const int nb_rb_oh = mac->sc_info.xOverhead_PDSCH ? 6 * (1 + *mac->sc_info.xOverhead_PDSCH) : 0;
  const int nb_re_dmrs = ((dlsch_pdu->dmrsConfigType == NFAPI_NR_DMRS_TYPE1) ? 6 : 4) * dlsch_pdu->n_dmrs_cdm_groups;
  if (!get_cw_info(&prnti_harq,
                   dlsch_pdu,
                   FAPI_NR_DL_CONFIG_TYPE_P_DLSCH,
                   &dlsch_pdu->cw_info[0],
                   dlsch_pdu->number_rbs,
                   nb_re_dmrs,
                   nb_rb_oh,
                   0,
                   dci->mcs,
                   1,
                   0)) {
    LOG_W(NR_MAC_DCI,
          "[%04d.%02d] Reject P-RNTI DCI: invalid CW (mcs=%d table=%d rb_len=%d symbols=%d)\n",
          frame,
          slot,
          dci->mcs,
          dlsch_pdu->mcs_table,
          dlsch_pdu->number_rbs,
          dlsch_pdu->number_symbols);
    return -1;
  }

  const int bw_tbslbrm = current_DL_BWP ? mac->sc_info.dl_bw_tbslbrm : dlsch_pdu->BWPSize;
  dlsch_pdu->tbslbrm = nr_compute_tbslbrm(dlsch_pdu->mcs_table, bw_tbslbrm, 1);
  dlsch_pdu->n_codewords = 1;
  dlsch_pdu->harq_process_nbr = 0;

  /* TB scaling field for DCI 1_0 with CRC scrambled by P-RNTI/RA-RNTI/MsgB-RNTI
   * (TS 38.214 §5.1.3.2, Table 5.1.3.2-2). */
  if (dci->tb_scaling > 3) {
    LOG_W(NR_MAC_DCI, "[%04d.%02d] Reject P-RNTI DCI: invalid tb_scaling=%d\n", frame, slot, dci->tb_scaling);
    return -1;
  }
  const float factor[] = {1, 0.5, 0.25, 0};
  dlsch_pdu->scaling_factor_S = factor[dci->tb_scaling];
  dlsch_pdu->k1_feedback = 0;
  return 0;
}

static int nr_ue_process_dci_dl_10(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    25 NDI:
     *    26 RV:
     *    27 HARQ_PROCESS_NUMBER:
     *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
     *    33 TPC_PUCCH:
     *    34 PUCCH_RESOURCE_IND:
     *    35 PDSCH_TO_HARQ_FEEDBACK_TIME_IND:
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by P-RNTI
     *    8  SHORT_MESSAGE_IND
     *    9  SHORT_MESSAGES
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    31 TB_SCALING
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by SI-RNTI
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    26 RV:
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by RA-RNTI
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    31 TB_SCALING
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by TC-RNTI
     *    0  IDENTIFIER_DCI_FORMATS:
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    25 NDI:
     *    26 RV:
     *    27 HARQ_PROCESS_NUMBER:
     *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
     *    33 TPC_PUCCH:
   */

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  fapi_nr_dl_config_request_pdu_t *dl_conf_req = &dl_config->dl_config_list[dl_config->number_pdus];
  dl_conf_req->dlsch_config_pdu.rnti = dci_ind->rnti;

  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu = &dl_conf_req->dlsch_config_pdu.dlsch_config_rel15;

  dlsch_pdu->pduBitmap = 0;
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  nr_rnti_type_t rnti_type = get_rnti_type(mac, dci_ind->rnti);
  /* Dedicated PDSCH-Config does not apply to P-RNTI PCCH or SIB1 acquisition: use common defaults. */
  NR_PDSCH_Config_t *pdsch_config =
      (rnti_type == TYPE_P_RNTI_ || !current_DL_BWP || mac->get_sib1) ? NULL : current_DL_BWP->pdsch_Config;
  if (dci_ind->ss_type == NR_SearchSpace__searchSpaceType_PR_common) {
    dlsch_pdu->BWPSize =
        mac->type0_PDCCH_CSS_config.num_rbs ? mac->type0_PDCCH_CSS_config.num_rbs : mac->sc_info.initial_dl_BWPSize;
    dlsch_pdu->BWPStart = dci_ind->cset_start;
  } else {
    dlsch_pdu->BWPSize = current_DL_BWP->BWPSize;
    dlsch_pdu->BWPStart = current_DL_BWP->BWPStart;
  }

  if (rnti_type == TYPE_P_RNTI_) {
    /* DCI 1_0 P-RNTI: SMI per TS 38.212 clause 7.3.1.2.1: 00 reserved, 01 paging grant, 10 short
     * message only, 11 paging + short message */
    if (dci->short_messages_indicator == NR_DCI_PRNTI_SMI_RESERVED) {
      LOG_W(NR_MAC_DCI, "[%04d.%02d] P-RNTI DCI ignored: reserved SMI (short_messages_indicator=0)\n", frame, slot);
      return -1;
    }
    if (dci->short_messages_indicator == NR_DCI_PRNTI_SMI_SHORT_MSG_ONLY) {
      LOG_D(NR_MAC_DCI, "[%04d.%02d] P-RNTI DCI: short-message only (no paging PDSCH)\n", frame, slot);
      return -1;
    }
    LOG_I(NR_MAC_DCI,
          "[%d.%d] Detected P-RNTI DCI: SMI=%d short_msg=0x%02x, attempting PDSCH scheduling\n",
          frame,
          slot,
          dci->short_messages_indicator,
          dci->short_messages);
  }

  int mux_pattern = 1;
  if (rnti_type == TYPE_SI_RNTI_) {
    NR_Type0_PDCCH_CSS_config_t type0_PDCCH_CSS_config = mac->type0_PDCCH_CSS_config;
    mux_pattern = type0_PDCCH_CSS_config.type0_pdcch_ss_mux_pattern;
    dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH;
    // in MIB SCS is signaled as 15or60 and 30or120
    dlsch_pdu->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon;
    if (mac->frequency_range == FR2)
      dlsch_pdu->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon + 2;
  } else {
    dlsch_pdu->SubcarrierSpacing = current_DL_BWP->scs;
    if (rnti_type == TYPE_RA_RNTI_) {
      if (nr_timer_is_active(&mac->ra.response_window_timer)) {
        dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH;
      } else {
        // Discard the DCI
        return -1;
      }
    } else if (rnti_type == TYPE_P_RNTI_) {
      dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_P_DLSCH;
    } else {
      dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_DLSCH;
    }
  }

  dlsch_pdu->numCsiRsForRateMatching = 0;
  int slots_frame = mac->frame_structure.numb_slots_frame;
  configure_ratematching_csi(dlsch_pdu,
                             dl_config,
                             rnti_type,
                             frame,
                             slot,
                             dlsch_pdu->SubcarrierSpacing,
                             slots_frame,
                             pdsch_config,
                             mac->sc_info.csi_MeasConfig);

  /* IDENTIFIER_DCI_FORMATS */
  /* FREQ_DOM_RESOURCE_ASSIGNMENT_DL */
  if (nr_ue_process_dci_freq_dom_resource_assignment(NULL,
                                                     dlsch_pdu,
                                                     NULL,
                                                     0,
                                                     dlsch_pdu->BWPSize,
                                                     0,
                                                     dci->frequency_domain_assignment)
      < 0) {
    LOG_W(NR_MAC, "[%d.%d] Invalid frequency_domain_assignment. Possibly due to false DCI. Ignoring DCI!\n", frame, slot);
    return -1;
  }

  dlsch_pdu->refPoint = mac->get_sib1 ? 1 : 0;

  /* TIME_DOM_RESOURCE_ASSIGNMENT */
  int dmrs_typeA_pos = mac->dmrs_TypeA_Position;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;


  NR_tda_info_t tda_info = get_dl_tda_info(current_DL_BWP,
                                           dci_ind->ss_type,
                                           dci->time_domain_assignment.val,
                                           dmrs_typeA_pos,
                                           mux_pattern,
                                           rnti_type,
                                           coreset_type,
                                           mac->get_sib1);
  if (!tda_info.valid_tda)
    return -1;

  dlsch_pdu->number_symbols = tda_info.nrOfSymbols;
  dlsch_pdu->start_symbol = tda_info.startSymbolIndex;

  mappingType_t type = tda_info.mapping_type;
  NR_DMRS_DownlinkConfig_t *dl_dmrs_config = NULL;
  if (pdsch_config) {
    if (type == typeA) {
      if (!pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeA) {
        LOG_E(MAC, "Invalid PDSCH DMRS configuration, expected typeA but not configured\n");
        return -1;
      } else
        dl_dmrs_config = pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup;
    } else { // typeB
      if (!pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeB) {
        LOG_E(MAC, "Invalid PDSCH DMRS configuration, expected typeB but not configured\n");
        return -1;
      } else
        dl_dmrs_config = pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup;
    }
  }

  dlsch_pdu->nscid = 0;
  if (dl_dmrs_config && dl_dmrs_config->scramblingID0)
    dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID0;
  else
    dlsch_pdu->dlDmrsScramblingId = mac->physCellId;

  if (rnti_type == TYPE_C_RNTI_
      && dci_ind->ss_type != NR_SearchSpace__searchSpaceType_PR_common
      && pdsch_config->dataScramblingIdentityPDSCH)
    dlsch_pdu->dlDataScramblingId = *pdsch_config->dataScramblingIdentityPDSCH;
  else
    dlsch_pdu->dlDataScramblingId = mac->physCellId;

  /* dmrs symbol positions*/
  dlsch_pdu->dlDmrsSymbPos = fill_dmrs_mask(pdsch_config,
                                            NR_DL_DCI_FORMAT_1_0,
                                            mac->dmrs_TypeA_Position,
                                            dlsch_pdu->number_symbols,
                                            dlsch_pdu->start_symbol,
                                            tda_info.mapping_type,
                                            1);

  dlsch_pdu->dmrsConfigType = (dl_dmrs_config != NULL) ? (dl_dmrs_config->dmrs_Type == NULL ? 0 : 1) : 0;

  /* number of DM-RS CDM groups without data according to subclause 5.1.6.2 of 3GPP TS 38.214 version 15.9.0 Release 15 */
  if (dlsch_pdu->number_symbols == 2)
    dlsch_pdu->n_dmrs_cdm_groups = 1;
  else
    dlsch_pdu->n_dmrs_cdm_groups = 2;
  dlsch_pdu->dmrs_ports = 1; // only port 0 in case of DCI 1_0
  /* VRB_TO_PRB_MAPPING */
  dlsch_pdu->vrb_to_prb_mapping =
      (dci->vrb_to_prb_mapping.val == 0) ? vrb_to_prb_mapping_non_interleaved : vrb_to_prb_mapping_interleaved;
  /* MCS TABLE INDEX */
  dlsch_pdu->mcs_table = get_dlsch_mcs_table(NR_DL_DCI_FORMAT_1_0,
                                             rnti_type,
                                             dci_ind->ss_type,
                                             pdsch_config ? pdsch_config->mcs_Table : NULL,
                                             NULL, // SPS not implemented,
                                             false, // as above
                                             NULL); // MCS-C-RNTI not implemented

  if (rnti_type == TYPE_P_RNTI_) {
    const int ret = nr_ue_process_dci_dl_10_p_rnti(mac, frame, slot, dci, dlsch_pdu, current_DL_BWP);
    if (ret >= 0) {
      LOG_D(NR_MAC,
            "[%04d.%02d][UE %d] P-RNTI DCI accepted: rb=%d+%d sym=%d+%d mcs=%d tbs=%d\n",
            frame,
            slot,
            mac->ue_id,
            dlsch_pdu->start_rb,
            dlsch_pdu->number_rbs,
            dlsch_pdu->start_symbol,
            dlsch_pdu->number_symbols,
            dlsch_pdu->cw_info[0].mcs,
            dlsch_pdu->cw_info[0].TBS);
      dl_config->number_pdus++;
    }
    return ret;
  }

  int nb_re_dmrs = ((dlsch_pdu->dmrsConfigType == NFAPI_NR_DMRS_TYPE1) ? 6 : 4) * dlsch_pdu->n_dmrs_cdm_groups;
  int nb_rb_oh = mac->sc_info.xOverhead_PDSCH ? nb_rb_oh = 6 * (1 + *mac->sc_info.xOverhead_PDSCH) : 0;
  NR_UE_DL_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[dci->harq_pid.val][0];
  if (!get_cw_info(current_harq,
                   dlsch_pdu,
                   dl_conf_req->pdu_type,
                   &dlsch_pdu->cw_info[0],
                   dlsch_pdu->number_rbs,
                   nb_re_dmrs,
                   nb_rb_oh,
                   dci->rv,
                   dci->mcs,
                   dci->ndi,
                   0))
    return -1;

  dlsch_pdu->n_codewords = 1;

  int bw_tbslbrm = current_DL_BWP ? mac->sc_info.dl_bw_tbslbrm : dlsch_pdu->BWPSize;
  dlsch_pdu->tbslbrm = nr_compute_tbslbrm(dlsch_pdu->mcs_table, bw_tbslbrm, 1);

  /* HARQ_PROCESS_NUMBER (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  dlsch_pdu->harq_process_nbr = dci->harq_pid.val;
  /* TB_SCALING (only if CRC scrambled by P-RNTI or RA-RNTI) */
  // according to TS 38.214 Table 5.1.3.2-3
  if (dci->tb_scaling > 3) {
    LOG_E(MAC, "invalid tb_scaling %d\n", dci->tb_scaling);
    return -1;
  }
  const float factor[] = {1, 0.5, 0.25, 0};
  dlsch_pdu->scaling_factor_S = factor[dci->tb_scaling];
  /* TPC_PUCCH (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  // according to TS 38.213 Table 7.2.1-1
  if (dci->tpc > 3) {
    LOG_E(MAC, "invalid tpc %d\n", dci->tpc);
    return -1;
  }

  // SI-RNTI and RA-RNTI DCI 1_0 do not carry a PUCCH resource indicator.
  bool valid = rnti_type == TYPE_SI_RNTI_ || rnti_type == TYPE_RA_RNTI_;
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP ? mac->current_UL_BWP->pucch_Config : NULL;

  if (!valid && pucch_Config && pucch_Config->resourceSetToAddModList) {
    int pucch_res_set_cnt = pucch_Config->resourceSetToAddModList->list.count;
    for (int id = 0; id < pucch_res_set_cnt; id++) {
      if (dci->pucch_resource_indicator < pucch_Config->resourceSetToAddModList->list.array[id]->resourceList.list.count) {
        valid = true;
        break;
      }
    }
  } else
    valid = true;
  if (!valid) {
    LOG_W(MAC,
          "[%d.%d] pucch_resource_indicator value %d is out of bounds. Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->pucch_resource_indicator);
    return -1;
  }

  /* PDSCH_TO_HARQ_FEEDBACK_TIME_IND */
  // according to TS 38.213 9.2.3
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, dlsch_pdu->SubcarrierSpacing);
  uint16_t feedback_ti = 0;

  if (rnti_type == TYPE_RA_RNTI_) {
    // RA-RNTI indicates RAR. Ensure we can decode RAR before the earliest UL scheduler call
    // that can process the MSG3. Also assume that RAR is sent in the same slot as DCI (k0 == 0)
    // This is not perfect as the MSG3 might end up being scheduled later, so we could be
    // halting the UL scheduler for a longer time than necessary.
    feedback_ti = max(1 + GET_DURATION_RX_TO_TX(mac, dlsch_pdu->SubcarrierSpacing),
                      get_delta_for_k2(mac->current_UL_BWP->scs) + get_j_for_k2(mac->current_UL_BWP->scs));
  }

  if (rnti_type != TYPE_RA_RNTI_ && rnti_type != TYPE_SI_RNTI_) {
    if (!get_FeedbackDisabled(mac->sc_info.downlinkHARQ_FeedbackDisabled_r17, dci->harq_pid.val)) {
      feedback_ti = 1 + dci->pdsch_to_harq_feedback_timing_indicator.val + ntn_ue_koffset;
      AssertFatal(feedback_ti >= GET_DURATION_RX_TO_TX(mac, dlsch_pdu->SubcarrierSpacing),
                  "PDSCH to HARQ feedback time (%d) needs to be higher than DURATION_RX_TO_TX (%ld).\n",
                  feedback_ti,
                  GET_DURATION_RX_TO_TX(mac, dlsch_pdu->SubcarrierSpacing));
      // set the harq status at MAC for feedback
      const int tpc[] = {-1, 0, 1, 3};
      set_harq_status(mac,
                      dci->pucch_resource_indicator,
                      dci->harq_pid.val,
                      0,
                      tpc[dci->tpc],
                      feedback_ti,
                      dci->dai[0].val,
                      dci_ind->n_CCE,
                      dci_ind->N_CCE,
                      frame,
                      slot);
    }
    if (current_harq->round < sizeofArray(mac->stats.dl.rounds))
      mac->stats.dl.rounds[current_harq->round]++;
  }

  LOG_D(MAC,
        "(nr_ue_procedures.c) rnti = %x dl_config->number_pdus = %d\n",
        dl_conf_req->dlsch_config_pdu.rnti,
        dl_config->number_pdus);
  LOG_D(MAC,
        "(nr_ue_procedures.c) frequency_domain_resource_assignment=%d \t number_rbs=%d \t start_rb=%d\n",
        dci->frequency_domain_assignment.val,
        dlsch_pdu->number_rbs,
        dlsch_pdu->start_rb);
  LOG_D(MAC,
        "(nr_ue_procedures.c) time_domain_resource_assignment=%d \t number_symbols=%d \t start_symbol=%d\n",
        dci->time_domain_assignment.val,
        dlsch_pdu->number_symbols,
        dlsch_pdu->start_symbol);
  LOG_D(MAC,
        "(nr_ue_procedures.c) vrb_to_prb_mapping=%d \n>>> mcs=%d\n>>> ndi=%d\n>>> rv=%d\n>>> harq_process_nbr=%d\n>>> dai=%d\n>>> "
        "scaling_factor_S=%f\n>>> tpc_pucch=%d\n>>> pucch_res_ind=%d\n>>> pdsch_to_harq_feedback_time_ind=%d\n",
        dlsch_pdu->vrb_to_prb_mapping,
        dlsch_pdu->cw_info[0].mcs,
        dlsch_pdu->cw_info[0].new_data_indicator,
        dlsch_pdu->cw_info[0].rv,
        dlsch_pdu->harq_process_nbr,
        dci->dai[0].val,
        dlsch_pdu->scaling_factor_S,
        dci->tpc,
        dci->pucch_resource_indicator,
        feedback_ti);

  dlsch_pdu->k1_feedback = feedback_ti;

  LOG_D(MAC, "(nr_ue_procedures.c) pdu_type=%d\n", dl_conf_req->pdu_type);

  // the prepared dci is valid, we add it in the list
  dl_config->number_pdus++;
  return 0;
}

static int nr_ue_process_dci_dl_11(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    1  CARRIER_IND:
   *    7  BANDWIDTH_PART_IND:
   *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
   *    14 PRB_BUNDLING_SIZE_IND:
   *    15 RATE_MATCHING_IND:
   *    16 ZP_CSI_RS_TRIGGER:
   *    18 TB1_MCS:
   *    19 TB1_NDI:
   *    20 TB1_RV:
   *    21 TB2_MCS:
   *    22 TB2_NDI:
   *    23 TB2_RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
   *    33 TPC_PUCCH:
   *    34 PUCCH_RESOURCE_IND:
   *    35 PDSCH_TO_HARQ_FEEDBACK_TIME_IND:
   *    38 ANTENNA_PORTS:
   *    39 TCI:
   *    40 SRS_REQUEST:
   *    43 CBGTI:
   *    44 CBGFI:
   *    47 DMRS_SEQ_INI:
   */

  if (dci->bwp_indicator.val > NR_MAX_NUM_BWP) {
    LOG_W(NR_MAC,
          "[%d.%d] bwp_indicator %d > NR_MAX_NUM_BWP Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->bwp_indicator.val);
    return -1;
  }
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_PDSCH_Config_t *pdsch_Config = current_DL_BWP->pdsch_Config;
  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  fapi_nr_dl_config_request_pdu_t *dl_conf_req = &dl_config->dl_config_list[dl_config->number_pdus];

  dl_conf_req->dlsch_config_pdu.rnti = dci_ind->rnti;

  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu = &dl_conf_req->dlsch_config_pdu.dlsch_config_rel15;

  dlsch_pdu->BWPSize = current_DL_BWP->BWPSize;
  dlsch_pdu->BWPStart = current_DL_BWP->BWPStart;
  dlsch_pdu->SubcarrierSpacing = current_DL_BWP->scs;

  nr_rnti_type_t rnti_type = get_rnti_type(mac, dci_ind->rnti);
  dlsch_pdu->numCsiRsForRateMatching = 0;
  int slots_frame = mac->frame_structure.numb_slots_frame;
  configure_ratematching_csi(dlsch_pdu,
                             dl_config,
                             rnti_type,
                             frame,
                             slot,
                             current_DL_BWP->scs,
                             slots_frame,
                             pdsch_Config,
                             mac->sc_info.csi_MeasConfig);

  /* IDENTIFIER_DCI_FORMATS */
  /* CARRIER_IND */
  /* BANDWIDTH_PART_IND */
  //    dlsch_pdu->bandwidth_part_ind = dci->bandwidth_part_ind;
  /* FREQ_DOM_RESOURCE_ASSIGNMENT_DL */
  if (nr_ue_process_dci_freq_dom_resource_assignment(NULL,
                                                     dlsch_pdu,
                                                     pdsch_Config,
                                                     0,
                                                     current_DL_BWP->BWPSize,
                                                     current_DL_BWP->BWPStart,
                                                     dci->frequency_domain_assignment)
      < 0) {
    LOG_W(MAC, "[%d.%d] Invalid frequency_domain_assignment. Possibly due to false DCI. Ignoring DCI!\n", frame, slot);
    return -1;
  }
  dlsch_pdu->refPoint = 0;
  /* TIME_DOM_RESOURCE_ASSIGNMENT */
  int dmrs_typeA_pos = mac->dmrs_TypeA_Position;
  int mux_pattern = 1;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_dl_tda_info(current_DL_BWP,
                                           dci_ind->ss_type,
                                           dci->time_domain_assignment.val,
                                           dmrs_typeA_pos,
                                           mux_pattern,
                                           rnti_type,
                                           coreset_type,
                                           false);
  if (!tda_info.valid_tda)
    return -1;

  dlsch_pdu->number_symbols = tda_info.nrOfSymbols;
  dlsch_pdu->start_symbol = tda_info.startSymbolIndex;

  mappingType_t type = tda_info.mapping_type;
  NR_DMRS_DownlinkConfig_t *dl_dmrs_config = NULL;
  if (pdsch_Config) {
    if (type == typeA) {
      if (!pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA) {
        LOG_E(MAC, "Invalid PDSCH DMRS configuration, expected typeA but not configured\n");
        return -1;
      } else
        dl_dmrs_config = pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup;
    } else { // typeB
      if (!pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeB) {
        LOG_E(MAC, "Invalid PDSCH DMRS configuration, expected typeB but not configured\n");
        return -1;
      } else
        dl_dmrs_config = pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup;
    }
  }

  switch (dci->dmrs_sequence_initialization.val) {
    case 0:
      dlsch_pdu->nscid = 0;
      if (dl_dmrs_config->scramblingID0)
        dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID0;
      else
        dlsch_pdu->dlDmrsScramblingId = mac->physCellId;
      break;
    case 1:
      dlsch_pdu->nscid = 1;
      if (dl_dmrs_config->scramblingID1)
        dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID1;
      else
        dlsch_pdu->dlDmrsScramblingId = mac->physCellId;
      break;
    default:
      LOG_E(MAC, "Invalid dmrs sequence initialization value %d\n", dci->dmrs_sequence_initialization.val);
      return -1;
  }

  if (pdsch_Config->dataScramblingIdentityPDSCH)
    dlsch_pdu->dlDataScramblingId = *pdsch_Config->dataScramblingIdentityPDSCH;
  else
    dlsch_pdu->dlDataScramblingId = mac->physCellId;

  dlsch_pdu->dmrsConfigType = dl_dmrs_config->dmrs_Type == NULL ? NFAPI_NR_DMRS_TYPE1 : NFAPI_NR_DMRS_TYPE2;

  /* VRB_TO_PRB_MAPPING */
  if ((pdsch_Config->resourceAllocation == 1) && (pdsch_Config->vrb_ToPRB_Interleaver != NULL))
    dlsch_pdu->vrb_to_prb_mapping =
        (dci->vrb_to_prb_mapping.val == 0) ? vrb_to_prb_mapping_non_interleaved : vrb_to_prb_mapping_interleaved;
  /* PRB_BUNDLING_SIZE_IND */
  dlsch_pdu->prb_bundling_size_ind = dci->prb_bundling_size_indicator.val;
  /* RATE_MATCHING_IND */
  dlsch_pdu->rate_matching_ind = dci->rate_matching_indicator.val;
  /* ZP_CSI_RS_TRIGGER */
  dlsch_pdu->zp_csi_rs_trigger = dci->zp_csi_rs_trigger.val;

  /* HARQ_PROCESS_NUMBER */
  dlsch_pdu->harq_process_nbr = dci->harq_pid.val;
  /* TPC_PUCCH */
  // according to TS 38.213 Table 7.2.1-1
  if (dci->tpc > 3) {
    LOG_E(MAC, "invalid tpc %d\n", dci->tpc);
    return -1;
  }

  // Sanity check for pucch_resource_indicator value received to check for false DCI.
  bool valid = false;
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP->pucch_Config;
  int pucch_res_set_cnt = pucch_Config->resourceSetToAddModList->list.count;
  for (int id = 0; id < pucch_res_set_cnt; id++) {
    if (dci->pucch_resource_indicator < pucch_Config->resourceSetToAddModList->list.array[id]->resourceList.list.count) {
      valid = true;
      break;
    }
  }
  if (!valid) {
    LOG_W(MAC,
          "[%d.%d] pucch_resource_indicator value %d is out of bounds. Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->pucch_resource_indicator);
    return -1;
  }

  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  int nb_rb_oh;
  if (sc_info->xOverhead_PDSCH)
    nb_rb_oh = 6 * (1 + *sc_info->xOverhead_PDSCH);
  else
    nb_rb_oh = 0;

  /* ANTENNA_PORTS */
  long *max_length = dl_dmrs_config->maxLength;
  long *dmrs_type = dl_dmrs_config->dmrs_Type;

  // In case the higher layer parameter maxNrofCodeWordsScheduledByDCI indicates that two codeword transmission is enabled,
  // then one of the two transport blocks is disabled by DCI format 1_1
  // if IMCS = 26 and if rvid = 1 for the corresponding transport block
  bool cw0 = true;
  bool cw1 = true;
  if (pdsch_Config->maxNrofCodeWordsScheduledByDCI
      && *pdsch_Config->maxNrofCodeWordsScheduledByDCI == NR_PDSCH_Config__maxNrofCodeWordsScheduledByDCI_n2) {
    cw0 = dci->rv != 1 || dci->mcs != 26;
    cw1 = dci->rv2.val != 1 || dci->mcs2.val != 26;
  } else
    cw1 = false;

  dlsch_pdu->n_front_load_symb = 1; // default value
  set_antenna_port_parameters(dlsch_pdu, cw0 + cw1, max_length, dmrs_type, dci->antenna_ports.val);
  int nb_re_dmrs = ((dmrs_type == NULL) ? 6 : 4) * dlsch_pdu->n_dmrs_cdm_groups;

  /* dmrs symbol positions*/
  dlsch_pdu->dlDmrsSymbPos = fill_dmrs_mask(pdsch_Config,
                                            NR_DL_DCI_FORMAT_1_1,
                                            mac->dmrs_TypeA_Position,
                                            dlsch_pdu->number_symbols,
                                            dlsch_pdu->start_symbol,
                                            tda_info.mapping_type,
                                            dlsch_pdu->n_front_load_symb);

  dlsch_pdu->mcs_table = get_dlsch_mcs_table(NR_DL_DCI_FORMAT_1_1,
                                             rnti_type,
                                             dci_ind->ss_type,
                                             pdsch_Config ? pdsch_Config->mcs_Table : NULL,
                                             NULL, // SPS not implemented,
                                             false, // as above
                                             NULL); // MCS-C-RNTI not implemented

  /* PDSCH_TO_HARQ_FEEDBACK_TIME_IND */
  // according to TS 38.213 Table 9.2.3-1
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, dlsch_pdu->SubcarrierSpacing);
  uint16_t feedback_ti = 0;
  const int tpc[] = {-1, 0, 1, 3};
  if (!get_FeedbackDisabled(mac->sc_info.downlinkHARQ_FeedbackDisabled_r17, dci->harq_pid.val)) {
    feedback_ti = pucch_Config->dl_DataToUL_ACK->list.array[dci->pdsch_to_harq_feedback_timing_indicator.val][0] + ntn_ue_koffset;
    AssertFatal(feedback_ti >= GET_DURATION_RX_TO_TX(mac, dlsch_pdu->SubcarrierSpacing),
                "PDSCH to HARQ feedback time (%d) needs to be higher than DURATION_RX_TO_TX (%ld). Min feedback time set in config "
                "file (min_rxtxtime).\n",
                feedback_ti,
                GET_DURATION_RX_TO_TX(mac, dlsch_pdu->SubcarrierSpacing));
  }


  int number_rbs;
  if (dlsch_pdu->resource_alloc == 1) {
    number_rbs = dlsch_pdu->number_rbs;
  } else {
    uint32_t temp_bitmap[9];
    memcpy(temp_bitmap, dlsch_pdu->rb_bitmap, 36);
    number_rbs = count_bits(temp_bitmap, 9);
  }

  int cw_idx = 0;
  NR_UE_DL_HARQ_STATUS_t *current_harq = NULL;
  if (cw0) {
    current_harq = &mac->dl_harq_info[dci->harq_pid.val][0];
    if (get_cw_info(current_harq,
                    dlsch_pdu,
                    dl_conf_req->pdu_type,
                    &dlsch_pdu->cw_info[0],
                    number_rbs,
                    nb_re_dmrs,
                    nb_rb_oh,
                    dci->rv,
                    dci->mcs,
                    dci->ndi,
                    cw_idx)) {
      if (current_harq->round < sizeofArray(mac->stats.dl.rounds))
        mac->stats.dl.rounds[current_harq->round]++;
      accumulate_dl_stats(mac, dlsch_pdu, &dlsch_pdu->cw_info[0], number_rbs);
      // set the harq status at MAC for feedback
      set_harq_status(mac,
                      dci->pucch_resource_indicator,
                      dci->harq_pid.val,
                      0,
                      tpc[dci->tpc],
                      feedback_ti,
                      dci->dai[0].val,
                      dci_ind->n_CCE,
                      dci_ind->N_CCE,
                      frame,
                      slot);
      cw_idx++;
    } else
      return -1;
  }

  if (cw1) {
    current_harq = &mac->dl_harq_info[dci->harq_pid.val][cw_idx];
    if (get_cw_info(current_harq,
                    dlsch_pdu,
                    dl_conf_req->pdu_type,
                    &dlsch_pdu->cw_info[1],
                    number_rbs,
                    nb_re_dmrs,
                    nb_rb_oh,
                    dci->rv2.val,
                    dci->mcs2.val,
                    dci->ndi2.val,
                    cw_idx)) {
      if (current_harq->round < sizeofArray(mac->stats.dl.rounds))
        mac->stats.dl.rounds[current_harq->round]++;
      accumulate_dl_stats(mac, dlsch_pdu, &dlsch_pdu->cw_info[1], number_rbs);
      // set the harq status at MAC for feedback
      set_harq_status(mac,
                      dci->pucch_resource_indicator,
                      dci->harq_pid.val,
                      1,
                      tpc[dci->tpc],
                      feedback_ti,
                      dci->dai[0].val,
                      dci_ind->n_CCE,
                      dci_ind->N_CCE,
                      frame,
                      slot);
      cw_idx++;
    } else
      return -1;
  }

  dlsch_pdu->n_codewords = cw_idx;
  /* TCI */
  if (dl_conf_req->dci_config_pdu.dci_config_rel15.coreset.tci_present_in_dci == 1) {
    // 0 bit if higher layer parameter tci-PresentInDCI is not enabled
    // otherwise 3 bits as defined in Subclause 5.1.5 of [6, TS38.214]
    dlsch_pdu->tci_state = dci->transmission_configuration_indication.val;
  }

  /* SRS_REQUEST */
  AssertFatal(dci->srs_request.nbits == 2, "If SUL is supported in the cell, there is an additional bit in SRS request field\n");
  if (dci->srs_request.val > 0)
    nr_ue_aperiodic_srs_scheduling(mac, dci->srs_request.val, frame, slot);
  /* CBGTI */
  dlsch_pdu->cbgti = dci->cbgti.val;
  /* CBGFI */
  dlsch_pdu->codeBlockGroupFlushIndicator = dci->cbgfi.val;
  /* DMRS_SEQ_INI */
  // FIXME!!!

  // send the ack/nack slot number to phy to indicate tx thread to wait for DLSCH decoding
  dlsch_pdu->k1_feedback = feedback_ti;

  // TBS_LBRM according to section 5.4.2.1 of 38.212
  int max_mimo_layers = 0;
  if (sc_info->maxMIMO_Layers_PDSCH)
    max_mimo_layers = *sc_info->maxMIMO_Layers_PDSCH;
  else
    max_mimo_layers = mac->uecap_maxMIMO_PDSCH_layers;
  AssertFatal(max_mimo_layers > 0, "Invalid number of max MIMO layers for PDSCH\n");
  int nl_tbslbrm = max_mimo_layers < 4 ? max_mimo_layers : 4;
  dlsch_pdu->tbslbrm = nr_compute_tbslbrm(dlsch_pdu->mcs_table, sc_info->dl_bw_tbslbrm, nl_tbslbrm);
  /*PTRS configuration */
  dlsch_pdu->pduBitmap = 0;
  if (dl_dmrs_config->phaseTrackingRS != NULL) {
    AssertFatal(cw_idx == 1, "Cannot handle PTRS with 2 codewords\n");
    bool valid_ptrs_setup =
        set_dl_ptrs_values(dl_dmrs_config->phaseTrackingRS->choice.setup,
                           number_rbs,
                           dlsch_pdu->cw_info[0].mcs,
                           dlsch_pdu->mcs_table,
                           &dlsch_pdu->PTRSFreqDensity,
                           &dlsch_pdu->PTRSTimeDensity,
                           &dlsch_pdu->PTRSPortIndex,
                           &dlsch_pdu->nEpreRatioOfPDSCHToPTRS,
                           &dlsch_pdu->PTRSReOffset,
                           dlsch_pdu->number_symbols);
    if (valid_ptrs_setup == true) {
      dlsch_pdu->pduBitmap |= 0x1;
      LOG_D(MAC, "DL PTRS values: PTRS time den: %d, PTRS freq den: %d\n", dlsch_pdu->PTRSTimeDensity, dlsch_pdu->PTRSFreqDensity);
    }
  }
  // the prepared dci is valid, we add it in the list
  dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_DLSCH;
  dl_config->number_pdus++; // The DCI configuration is valid, we add it in the list
  return 0;
}

static int8_t nr_ue_process_dci(NR_UE_MAC_INST_t *mac,
                                frame_t frame,
                                int slot,
                                dci_pdu_rel15_t *dci,
                                fapi_nr_dci_indication_pdu_t *dci_ind,
                                const nr_dci_format_t format)
{
  const char *dci_formats[] = {"1_0", "1_1", "2_0", "2_1", "2_2", "2_3", "0_0", "0_1"};
  LOG_D(MAC, "Processing received DCI format %s\n", dci_formats[format]);

  switch (format) {
    case NR_UL_DCI_FORMAT_0_0:
      return nr_ue_process_dci_ul_00(mac, frame, slot, dci, dci_ind);
      break;

    case NR_UL_DCI_FORMAT_0_1:
      return nr_ue_process_dci_ul_01(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_1_0:
      return nr_ue_process_dci_dl_10(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_1_1:
      return nr_ue_process_dci_dl_11(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_2_0:
      AssertFatal(false, "DCI Format 2-0 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_1:
      AssertFatal(false, "DCI Format 2-1 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_2:
      AssertFatal(false, "DCI Format 2-2 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_3:
      AssertFatal(false, "DCI Format 2-3 handling not implemented\n");
      break;

    default:
      break;
  }
  return -1;
}

void nr_ue_process_l1_measurements(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_l1_measurements_t *l1_measurements)
{
  LOG_D(NR_MAC, "(%d.%d) Received measurements from L1\n", frame, slot);
  bool csi_meas = l1_measurements->meas_type == NFAPI_NR_CSI_MEAS;
  if (!csi_meas && !l1_measurements->is_neighboring_cell) {
    int ssb_index = l1_measurements->ssb_index;
    mac->ssb_measurements[ssb_index].ssb_rsrp_dBm = l1_measurements->rsrp_dBm;
    mac->ssb_measurements[ssb_index].ssb_sinr_dB = l1_measurements->sinr_dB;
  } else if (csi_meas) {
    mac->csirs_measurements.rsrp_dBm = l1_measurements->rsrp_dBm;
    mac->csirs_measurements.i_1_1 = l1_measurements->i_1_1;
    mac->csirs_measurements.i_1_2 = l1_measurements->i_1_2;
    mac->csirs_measurements.i_1_3 = l1_measurements->i_1_3;
    mac->csirs_measurements.i_2 = l1_measurements->i_2;
    mac->csirs_measurements.cqi = l1_measurements->cqi;
    mac->csirs_measurements.ri = l1_measurements->rank_indicator;
  }
  nr_mac_rrc_meas_ind_ue(mac->ue_id,
                         l1_measurements->gNB_index,
                         l1_measurements->Nid_cell,
                         csi_meas,
                         l1_measurements->is_neighboring_cell,
                         l1_measurements->rsrp_dBm);
}

initial_pucch_resource_t get_initial_pucch_resource(const int idx)
{
  return initial_pucch_resource[idx];
}

int nr_ue_configure_pucch(NR_UE_MAC_INST_t *mac,
                           int slot,
                           frame_t frame,
                           uint16_t rnti,
                           PUCCH_sched_t *pucch,
                           fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  NR_PUCCH_FormatConfig_t *pucchfmt;
  long *pusch_id = NULL;
  long *id0 = NULL;
  const int scs = current_UL_BWP->scs;
  int subframe_number = slot / (mac->frame_structure.numb_slots_frame / 10);
  pucch_pdu->rnti = rnti;

  LOG_D(NR_MAC, "initial_pucch_id %d, pucch_resource %p\n", pucch->initial_pucch_id, pucch->pucch_resource);
  // configure pucch from Table 9.2.1-1
  // only for ack/nack
  if (pucch->initial_pucch_id > -1 && pucch->pucch_resource == NULL) {
    const initial_pucch_resource_t pucch_resourcecommon = get_initial_pucch_resource(pucch->pucch_ResourceCommon);
    pucch_pdu->format_type = pucch_resourcecommon.format;
    pucch_pdu->start_symbol_index = pucch_resourcecommon.startingSymbolIndex;
    pucch_pdu->nr_of_symbols = pucch_resourcecommon.nrofSymbols;

    pucch_pdu->bwp_size = current_UL_BWP->BWPSize;
    pucch_pdu->bwp_start = current_UL_BWP->BWPStart;

    pucch_pdu->prb_size = 1; // format 0 or 1
    int RB_BWP_offset;
    if (pucch->pucch_ResourceCommon == 15)
      RB_BWP_offset = pucch_pdu->bwp_size >> 2;
    else
      RB_BWP_offset = pucch_resourcecommon.PRB_offset;

    int N_CS = pucch_resourcecommon.nb_CS_indexes;

    if (pucch->initial_pucch_id >> 3 == 0) {
      const int tmp = pucch->initial_pucch_id / N_CS;
      pucch_pdu->prb_start = RB_BWP_offset + tmp;
      pucch_pdu->second_hop_prb = pucch_pdu->bwp_size - 1 - RB_BWP_offset - tmp;
      pucch_pdu->initial_cyclic_shift = pucch_resourcecommon.initial_CS_indexes[pucch->initial_pucch_id % N_CS];
    } else {
      const int tmp = (pucch->initial_pucch_id - 8) / N_CS;
      pucch_pdu->prb_start = pucch_pdu->bwp_size - 1 - RB_BWP_offset - tmp;
      pucch_pdu->second_hop_prb = RB_BWP_offset + tmp;
      pucch_pdu->initial_cyclic_shift = pucch_resourcecommon.initial_CS_indexes[(pucch->initial_pucch_id - 8) % N_CS];
    }
    pucch_pdu->freq_hop_flag = 1;
    pucch_pdu->time_domain_occ_idx = 0;
    // Only HARQ transmitted in default PUCCH
    pucch_pdu->mcs = get_pucch0_mcs(pucch->n_harq, 0, pucch->ack_payload, 0);
    pucch_pdu->payload = pucch->ack_payload;
    pucch_pdu->n_bit = 1;
  } else if (pucch->pucch_resource != NULL) {

    NR_PUCCH_Resource_t *pucchres = pucch->pucch_resource;

    if (mac->harq_ACK_SpatialBundlingPUCCH ||
        mac->pdsch_HARQ_ACK_Codebook != NR_PhysicalCellGroupConfig__pdsch_HARQ_ACK_Codebook_dynamic) {
      LOG_E(NR_MAC, "PUCCH Unsupported cell group configuration\n");
      return -1;
    } else if (sc_info->pdsch_CGB_Transmission) {
      LOG_E(NR_MAC, "PUCCH Unsupported code block group for serving cell config\n");
      return -1;
    }

    NR_PUSCH_Config_t *pusch_Config = current_UL_BWP ? current_UL_BWP->pusch_Config : NULL;
    if (pusch_Config) {
      pusch_id = pusch_Config->dataScramblingIdentityPUSCH;
      struct NR_SetupRelease_DMRS_UplinkConfig *tmp = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA;
      if (tmp && tmp->choice.setup->transformPrecodingDisabled != NULL)
        id0 = tmp->choice.setup->transformPrecodingDisabled->scramblingID0;
      else {
        struct NR_SetupRelease_DMRS_UplinkConfig *tmp = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB;
        if (tmp && tmp->choice.setup->transformPrecodingDisabled != NULL)
          id0 = tmp->choice.setup->transformPrecodingDisabled->scramblingID0;
      }
    }

    NR_PUCCH_Config_t *pucch_Config = current_UL_BWP->pucch_Config;
    AssertFatal(pucch_Config, "no pucch_Config\n");

    pucch_pdu->bwp_size = current_UL_BWP->BWPSize;
    pucch_pdu->bwp_start = current_UL_BWP->BWPStart;
    pucch_pdu->prb_start = pucchres->startingPRB;
    pucch_pdu->freq_hop_flag = pucchres->intraSlotFrequencyHopping!= NULL ?  1 : 0;
    pucch_pdu->second_hop_prb = pucchres->secondHopPRB!= NULL ?  *pucchres->secondHopPRB : 0;
    pucch_pdu->prb_size = 1; // format 0 or 1

    int n_uci = pucch->n_sr + pucch->n_harq + pucch->csi_payload.p1_bits;
    if (n_uci > (sizeof(uint64_t) * 8)) {
      LOG_E(NR_MAC, "PUCCH number of UCI bits exceeds payload size\n");
      return -1;
    }

    switch(pucchres->format.present) {
      case NR_PUCCH_Resource__format_PR_format0 :
        pucch_pdu->format_type = 0;
        pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format0->initialCyclicShift;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format0->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format0->startingSymbolIndex;
        pucch_pdu->mcs = get_pucch0_mcs(pucch->n_harq, pucch->n_sr, pucch->ack_payload, pucch->sr_payload);
        break;
      case NR_PUCCH_Resource__format_PR_format1 :
        pucch_pdu->format_type = 1;
        pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format1->initialCyclicShift;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format1->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format1->startingSymbolIndex;
        pucch_pdu->time_domain_occ_idx = pucchres->format.choice.format1->timeDomainOCC;
        if (pucch->n_harq > 0) {
          // only HARQ bits are transmitted, resource selection depends on SR
          // resource selection handled in function multiplex_pucch_resource
          pucch_pdu->n_bit = pucch->n_harq;
          pucch_pdu->payload = pucch->ack_payload;
        }
        else {
          // For a positive SR transmission using PUCCH format 1,
          // the UE transmits the PUCCH as described in 38.211 by setting b(0) = 0
          pucch_pdu->n_bit = pucch->n_sr;
          pucch_pdu->payload = 0;
        }
        break;
      case NR_PUCCH_Resource__format_PR_format2 :
        pucch_pdu->format_type = 2;
        pucch_pdu->n_bit = n_uci;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format2->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format2->startingSymbolIndex;
        pucch_pdu->data_scrambling_id = pusch_id != NULL ? *pusch_id : mac->physCellId;
        pucch_pdu->dmrs_scrambling_id = id0 != NULL ? *id0 : mac->physCellId;
        pucch_pdu->prb_size = compute_pucch_prb_size(pucchres->format.choice.format2->nrofPRBs,
                                                     pucch->csi_payload.p1_bits,
                                                     pucch->n_harq,
                                                     pucch->n_sr,
                                                     pucch_Config->format2->choice.setup->maxCodeRate,
                                                     2,
                                                     pucchres->format.choice.format2->nrofSymbols,
                                                     8);
        pucch_pdu->payload = (pucch->csi_payload.part1_payload << (pucch->n_harq + pucch->n_sr))
                             | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      case NR_PUCCH_Resource__format_PR_format3 :
        pucch_pdu->format_type = 3;
        pucch_pdu->n_bit = n_uci;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format3->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format3->startingSymbolIndex;
        pucch_pdu->data_scrambling_id = pusch_id != NULL ? *pusch_id : mac->physCellId;
        if (pucch_Config->format3 == NULL) {
          pucch_pdu->pi_2bpsk = 0;
          pucch_pdu->add_dmrs_flag = 0;
        }
        else {
          pucchfmt = pucch_Config->format3->choice.setup;
          pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK!= NULL ?  1 : 0;
          pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS!= NULL ?  1 : 0;
        }
        int f3_dmrs_symbols;
        if (pucchres->format.choice.format3->nrofSymbols==4)
          f3_dmrs_symbols = 1<<pucch_pdu->freq_hop_flag;
        else {
          if(pucchres->format.choice.format3->nrofSymbols<10)
            f3_dmrs_symbols = 2;
          else
            f3_dmrs_symbols = 2<<pucch_pdu->add_dmrs_flag;
        }
        pucch_pdu->prb_size = compute_pucch_prb_size(pucchres->format.choice.format3->nrofPRBs,
                                                     pucch->csi_payload.p1_bits,
                                                     pucch->n_harq,
                                                     pucch->n_sr,
                                                     pucch_Config->format3->choice.setup->maxCodeRate,
                                                     2 - pucch_pdu->pi_2bpsk,
                                                     pucchres->format.choice.format3->nrofSymbols - f3_dmrs_symbols,
                                                     12);
        pucch_pdu->payload = (pucch->csi_payload.part1_payload << (pucch->n_harq + pucch->n_sr))
                             | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      case NR_PUCCH_Resource__format_PR_format4 :
        pucch_pdu->format_type = 4;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format4->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format4->startingSymbolIndex;
        pucch_pdu->pre_dft_occ_len = pucchres->format.choice.format4->occ_Length;
        pucch_pdu->pre_dft_occ_idx = pucchres->format.choice.format4->occ_Index;
        pucch_pdu->data_scrambling_id = pusch_id!= NULL ? *pusch_id : mac->physCellId;
        if (pucch_Config->format3 == NULL) {
          pucch_pdu->pi_2bpsk = 0;
          pucch_pdu->add_dmrs_flag = 0;
        }
        else {
          pucchfmt = pucch_Config->format3->choice.setup;
          pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK!= NULL ?  1 : 0;
          pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS!= NULL ?  1 : 0;
        }
        pucch_pdu->payload = (pucch->csi_payload.part1_payload << (pucch->n_harq + pucch->n_sr))
                             | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      default :
        LOG_E(NR_MAC, "Undefined PUCCH format \n");
        return -1;
    }

    int sum_delta_pucch = get_sum_delta_pucch(mac, slot, frame);

    pucch_pdu->pucch_tx_power = get_pucch_tx_power_ue(mac,
                                                      scs,
                                                      pucch_Config,
                                                      sum_delta_pucch,
                                                      pucch_pdu->format_type,
                                                      pucch_pdu->prb_size,
                                                      pucch_pdu->freq_hop_flag,
                                                      pucch_pdu->add_dmrs_flag,
                                                      pucch_pdu->nr_of_symbols,
                                                      subframe_number,
                                                      n_uci,
                                                      pucch_pdu->prb_start);
  } else {
    LOG_E(NR_MAC, "problem with pucch configuration\n");
    return -1;
  }

  NR_PUCCH_ConfigCommon_t *pucch_ConfigCommon = current_UL_BWP->pucch_ConfigCommon;

  if (pucch_ConfigCommon->hoppingId != NULL)
    pucch_pdu->hopping_id = *pucch_ConfigCommon->hoppingId;
  else
    pucch_pdu->hopping_id = mac->physCellId;

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
      LOG_E(NR_MAC, "Group hopping flag undefined (0,1,2) \n");
      return -1;
  }
  return 0;
}

static int find_pucch_resource_set(NR_PUCCH_Config_t *pucch_Config, int size)
{
  // Procedure described in 38.213 Section 9.2.1

  AssertFatal(pucch_Config && pucch_Config->resourceSetToAddModList, "pucch-Config NULL, this function shouldn't have been called\n");
  AssertFatal(size <= 1706, "O_UCI cannot be larger that 1706 bits\n");

  // a first set of PUCCH resources with pucch-ResourceSetId = 0 if O UCI ≤ 2 including 1 or 2 HARQ-ACK information bits
  if (size <= 2)
    return 0;

  const int n_set = pucch_Config->resourceSetToAddModList->list.count;

  int N2 = 1706;
  int N3 = 1706;
  for (int i = 0; i < n_set; i++) {
    NR_PUCCH_ResourceSet_t *pucchresset = pucch_Config->resourceSetToAddModList->list.array[i];
    NR_PUCCH_ResourceId_t id = pucchresset->pucch_ResourceSetId;
    if (id == 1)
      N2 = pucchresset->maxPayloadSize ? *pucchresset->maxPayloadSize : 1706;
    if (id == 2)
      N3 = pucchresset->maxPayloadSize ? *pucchresset->maxPayloadSize : 1706;
  }

  // a second set of PUCCH resources with pucch-ResourceSetId = 1, if provided by higher layers, if 2 < O UCI ≤ N 2
  if (size <= N2)
    return 1;
  // a third set of PUCCH resources with pucch-ResourceSetId = 2, if provided by higher layers, if N 2 < O UCI ≤ N 3
  if (size <= N3)
    return 2;
  // a fourth set of PUCCH resources with pucch-ResourceSetId = 3, if provided by higher layers, if N 3 < O UCI ≤ 1706
  return 3;
}

NR_PUCCH_Resource_t *find_pucch_resource_from_list(struct NR_PUCCH_Config__resourceToAddModList *resourceToAddModList,
                                                   long resource_id)
{
  NR_PUCCH_Resource_t *pucch_resource = NULL;
  int n_list = resourceToAddModList->list.count;
  for (int i = 0; i < n_list; i++) {
    if (resourceToAddModList->list.array[i]->pucch_ResourceId == resource_id)
      pucch_resource = resourceToAddModList->list.array[i];
  }
  return pucch_resource;
}

static bool check_mux_acknack_csi(NR_PUCCH_Resource_t *csi_res, NR_PUCCH_Config_t *pucch_Config)
{
  bool ret;
  switch (csi_res->format.present) {
    case NR_PUCCH_Resource__format_PR_format2:
      ret = pucch_Config->format2->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    case NR_PUCCH_Resource__format_PR_format3:
      ret = pucch_Config->format3->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    case NR_PUCCH_Resource__format_PR_format4:
      ret = pucch_Config->format4->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    default:
      AssertFatal(false, "Invalid PUCCH format for CSI\n");
  }
  return ret;
}

void get_pucch_start_symbol_length(NR_PUCCH_Resource_t *pucch_resource, int *start, int *length)
{
  switch (pucch_resource->format.present) {
    case NR_PUCCH_Resource__format_PR_format0:
      *length = pucch_resource->format.choice.format0->nrofSymbols;
      *start = pucch_resource->format.choice.format0->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format1:
      *length = pucch_resource->format.choice.format1->nrofSymbols;
      *start = pucch_resource->format.choice.format1->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format2:
      *length = pucch_resource->format.choice.format2->nrofSymbols;
      *start = pucch_resource->format.choice.format2->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format3:
      *length = pucch_resource->format.choice.format3->nrofSymbols;
      *start = pucch_resource->format.choice.format3->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format4:
      *length = pucch_resource->format.choice.format4->nrofSymbols;
      *start = pucch_resource->format.choice.format4->startingSymbolIndex;
      break;
    default:
      AssertFatal(false, "Invalid PUCCH format\n");
  }
}

// Ref. 38.213 section 9.2.5 order(Q)
void order_resources(PUCCH_sched_t *pucch, int num_res)
{
  int k = 0;
  while (k < num_res - 1) {
    int l = 0;
    while (l < num_res - 1 - k) {
      NR_PUCCH_Resource_t *pucch_resource = pucch[l].pucch_resource;
      int curr_start, curr_length;
      get_pucch_start_symbol_length(pucch_resource, &curr_start, &curr_length);
      pucch_resource = pucch[l + 1].pucch_resource;
      int next_start, next_length;
      get_pucch_start_symbol_length(pucch_resource, &next_start, &next_length);
      if (curr_start > next_start || (curr_start == next_start && curr_length < next_length)) {
        // swap resources
        PUCCH_sched_t temp_res = pucch[l];
        pucch[l] = pucch[l + 1];
        pucch[l + 1] = temp_res;
      }
      l++;
    }
    k++;
  }
}

bool check_overlapping_resources(PUCCH_sched_t *pucch, int j, int o)
{
  // assuming overlapping means if two resources overlaps in time,
  // ie share a symbol in the slot regardless of PRB
  NR_PUCCH_Resource_t *pucch_resource = pucch[j - o].pucch_resource;
  int curr_start, curr_length;
  get_pucch_start_symbol_length(pucch_resource, &curr_start, &curr_length);
  pucch_resource = pucch[j + 1].pucch_resource;
  int next_start, next_length;
  get_pucch_start_symbol_length(pucch_resource, &next_start, &next_length);

  if (curr_start == next_start)
    return true;
  if (curr_start + curr_length - 1 < next_start)
    return false;
  else
    return true;
}

/* Helpers for PUCCH resource multiplexing. */

static bool is_f0_or_f1(const NR_PUCCH_Resource_t *r)
{
  return r->format.present == NR_PUCCH_Resource__format_PR_format0 || r->format.present == NR_PUCCH_Resource__format_PR_format1;
}

static bool is_f1(const NR_PUCCH_Resource_t *r)
{
  return r->format.present == NR_PUCCH_Resource__format_PR_format1;
}

static bool is_f2_or_above(const NR_PUCCH_Resource_t *r)
{
  return r->format.present == NR_PUCCH_Resource__format_PR_format2 || r->format.present == NR_PUCCH_Resource__format_PR_format3
         || r->format.present == NR_PUCCH_Resource__format_PR_format4;
}

/*
 * Move the payload fields of src into dst, then zero src.
 * Does NOT touch pucch_resource pointers.
 */
static void absorb_payload(PUCCH_sched_t *dst, PUCCH_sched_t *src)
{
  if (src->n_sr > 0 && dst->n_sr == 0) {
    dst->n_sr = src->n_sr;
    dst->sr_payload = src->sr_payload;
    src->n_sr = 0;
    src->sr_payload = 0;
  }
  if (src->n_harq > 0 && dst->n_harq == 0) {
    dst->n_harq = src->n_harq;
    dst->ack_payload = src->ack_payload;
    src->n_harq = 0;
    src->ack_payload = 0;
  }
  if (src->csi_payload.p1_bits > 0 && dst->csi_payload.p1_bits == 0) {
    dst->csi_payload = src->csi_payload;
    src->csi_payload = (typeof(src->csi_payload)){0};
  }
}

/*
 * Keep 'winner', place it in index i+1, clear index i.
 * winner must point to either res[i] or res[i+1].
 */
static void keep(PUCCH_sched_t *res, int i, PUCCH_sched_t *winner)
{
  static const PUCCH_sched_t empty = {0};
  if (winner == &res[i]) {
    res[i + 1] = res[i];
  }
  /* if winner is already res[i+1] there's nothing to copy */
  res[i] = empty;
}

/* SR + HARQ merging for F0
 *
 * For F0 (and when F0 holds HARQ and the other index holds SR):
 * the HARQ resource wins; copy SR into it.
 */
static void merge_sr_harq_into_harq_resource(PUCCH_sched_t *res, int i, PUCCH_sched_t *harq_sch, PUCCH_sched_t *sr_sch)
{
  AssertFatal(harq_sch->n_harq > 0 && sr_sch->n_sr > 0, "Expected one HARQ-only and one SR-only resource\n");
  harq_sch->n_sr = sr_sch->n_sr;
  harq_sch->sr_payload = sr_sch->sr_payload;
  keep(res, i, harq_sch);
}

/*
 * Positive-SR rule for F1+F1: HARQ goes into the SR resource.
 */
static void merge_f1_f1_positive_sr(PUCCH_sched_t *res, int i, PUCCH_sched_t *harq_sch, PUCCH_sched_t *sr_sch)
{
  sr_sch->n_harq = harq_sch->n_harq;
  sr_sch->ack_payload = harq_sch->ack_payload;
  sr_sch->n_sr = 0;
  sr_sch->sr_payload = 0;
  keep(res, i, sr_sch);
}

/* Merge HARQ (any format) with CSI (F2+)
 *
 * harq_res: resource carrying HARQ (any format), lives in *harq_sch
 * csi_slot: resource carrying CSI (F2+),         lives in *csi_sch
 *
 * If mux is allowed -> merge everything into the HARQ resource after reselecting the resource set.
 * If mux is denied  -> drop CSI; HARQ (+ any SR) survives.
 */
static void merge_harq_with_csi(PUCCH_sched_t *res,
                                int i,
                                PUCCH_sched_t *harq_sch,
                                PUCCH_sched_t *csi_sch,
                                NR_PUCCH_Resource_t *csi_res,
                                NR_PUCCH_Config_t *pucch_Config)
{
  AssertFatal(harq_sch->n_harq > 0, "Expected HARQ in harq_sch\n");
  AssertFatal(csi_sch->n_harq == 0, "Standard disallows >1 PUCCH with HARQ per slot\n");

  if (check_mux_acknack_csi(csi_res, pucch_Config)) {
    /* merge CSI (and any SR from csi_sch) into the HARQ resource */
    absorb_payload(harq_sch, csi_sch);

    if (!is_f2_or_above(harq_sch->pucch_resource)) {
      /* A PUCCH resource with CSI report means format >= 2. Hence the resource for
      multiplexed HARQ ACK and CSI report is chosen by HARQ ACK resource indicator
      from resource set id >= 1. As per 38.213 9.5.2. */
      int o_uci = harq_sch->n_harq + harq_sch->csi_payload.p1_bits + harq_sch->csi_payload.p2_bits;
      int res_set_id = find_pucch_resource_set(pucch_Config, o_uci);
      AssertFatal(res_set_id > 0, "Resource set id can't be 0 when multiplexing CSI and HARQ ACK and/or SR\n");
      const NR_PUCCH_ResourceSet_t *res_set = pucch_Config->resourceSetToAddModList->list.array[res_set_id];
      DevAssert(harq_sch->harq_ack_pucch_res_ind >= 0);
      long res_id = *res_set->resourceList.list.array[harq_sch->harq_ack_pucch_res_ind];
      harq_sch->pucch_resource = find_pucch_resource_from_list(pucch_Config->resourceToAddModList, res_id);
    }

    keep(res, i, harq_sch);
  } else {
    /* drop CSI; HARQ resource survives */
    if (csi_sch->n_sr > 0) {
      /* pull SR that was co-located with CSI over to the HARQ resource */
      AssertFatal(harq_sch->n_sr == 0, "We don't support more than 1 SR in a slot\n");
      if (is_f1(harq_sch->pucch_resource)) {
        LOG_E(MAC, "Not sure what to do here\n");
      }
      harq_sch->n_sr = csi_sch->n_sr;
      harq_sch->sr_payload = csi_sch->sr_payload;
    }
    keep(res, i, harq_sch);
  }
}

/* Merge two F2+ resources
 *
 * One carries CSI, the other carries HARQ (enforced by AssertFatal).
 */
static void merge_f2p_f2p(PUCCH_sched_t *res, int i, NR_PUCCH_Config_t *pucch_Config)
{
  PUCCH_sched_t *harq_sch, *csi_sch;

  if (res[i + 1].csi_payload.p1_bits > 0) {
    AssertFatal(res[i].csi_payload.p1_bits == 0, "Multiplexing multiple CSI reports in a single PUCCH not supported yet\n");
    AssertFatal(res[i].n_harq > 0 && res[i + 1].n_harq == 0, "Expected HARQ in res[i] when CSI is in res[i+1]\n");
    harq_sch = &res[i];
    csi_sch = &res[i + 1];
  } else {
    AssertFatal(res[i].csi_payload.p1_bits > 0, "We expect at least one of the 2 PUCCH F2+ resources to carry CSI\n");
    AssertFatal(res[i + 1].csi_payload.p1_bits == 0, "Multiplexing multiple CSI reports in a single PUCCH not supported yet\n");
    AssertFatal(res[i + 1].n_harq > 0 && res[i].n_harq == 0, "Expected HARQ in res[i+1] when CSI is in res[i]\n");
    harq_sch = &res[i + 1];
    csi_sch = &res[i];
  }

  /* re-use the HARQ resource pointer for the mux check (standard says
   * simultaneousHARQ-ACK-CSI must be the same for F2/3/4, so either works) */
  merge_harq_with_csi(res, i, harq_sch, csi_sch, harq_sch->pucch_resource, pucch_Config);
}

/* Top-level pair merger */

static void merge_pair(PUCCH_sched_t *res, int i, NR_PUCCH_Config_t *pucch_Config)
{
  static const PUCCH_sched_t empty = {0};

  NR_PUCCH_Resource_t *cr = res[i].pucch_resource;
  NR_PUCCH_Resource_t *nr = res[i + 1].pucch_resource;

  bool curr_low = is_f0_or_f1(cr);
  bool next_low = is_f0_or_f1(nr);
  bool curr_high = is_f2_or_above(cr);
  bool next_high = is_f2_or_above(nr);

  /* both low-format */
  if (curr_low && next_low) {
    bool curr_sr = res[i].n_sr > 0 && res[i].n_harq == 0;
    bool curr_harq = res[i].n_sr == 0 && res[i].n_harq > 0;
    bool next_sr = res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0;
    bool next_harq = res[i + 1].n_sr == 0 && res[i + 1].n_harq > 0;

    AssertFatal(curr_sr != curr_harq && next_sr != next_harq,
                "Expected one SR-only and one HARQ-only resource in the low-format pair\n");
    AssertFatal(curr_sr != next_sr, "We cannot have two SR resources or two HARQ resources here\n");

    bool f1_sr = is_f1(curr_sr ? cr : nr);
    bool f1_harq = is_f1(curr_harq ? cr : nr);

    PUCCH_sched_t *sr_sch = curr_sr ? &res[i] : &res[i + 1];
    PUCCH_sched_t *harq_sch = curr_harq ? &res[i] : &res[i + 1];

    if (!f1_sr && !f1_harq) {
      /* F0+F0 or F0+F1(SR) + F0(HARQ): HARQ resource wins, absorb SR */
      merge_sr_harq_into_harq_resource(res, i, harq_sch, sr_sch);
    } else if (f1_harq && !f1_sr) {
      /* F1(HARQ) + F0(SR): HARQ resource wins, SR just disappears (only HARQ sent) */
      keep(res, i, harq_sch);
    } else if (!f1_harq && f1_sr) {
      /* F0(HARQ) + F1(SR) or F1(SR) + F0(HARQ): HARQ resource wins */
      merge_sr_harq_into_harq_resource(res, i, harq_sch, sr_sch);
    } else {
      /* F1+F1 */
      if (sr_sch->sr_payload == 0) {
        /* negative SR -> HARQ only in HARQ resource */
        keep(res, i, harq_sch);
      } else {
        /* positive SR -> HARQ only in SR resource */
        merge_f1_f1_positive_sr(res, i, harq_sch, sr_sch);
      }
    }
    return;
  }

  /* low + high */
  if (curr_low && next_high) {
    if (res[i].n_sr > 0 && res[i].n_harq == 0) {
      /* SR in low format -> into high-format resource */
      AssertFatal(res[i + 1].n_sr == 0, "We don't support multiple SR in a slot\n");
      res[i + 1].n_sr = res[i].n_sr;
      res[i + 1].sr_payload = res[i].sr_payload;
      res[i] = empty;
    } else if (res[i].n_harq > 0) {
      merge_harq_with_csi(res, i, &res[i], &res[i + 1], nr, pucch_Config);
    }
    return;
  }

  /* high + low */
  if (curr_high && next_low) {
    if (res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
      /* SR in low format -> into high-format resource */
      AssertFatal(res[i].n_sr == 0, "We don't support multiple SR in a slot\n");
      res[i].n_sr = res[i + 1].n_sr;
      res[i].sr_payload = res[i + 1].sr_payload;
      keep(res, i, &res[i]);
    } else if (res[i + 1].n_harq > 0) {
      merge_harq_with_csi(res, i, &res[i + 1], &res[i], cr, pucch_Config);
    }
    return;
  }

  /* both high-format */
  if (curr_high && next_high) {
    merge_f2p_f2p(res, i, pucch_Config);
    return;
  }

  AssertFatal(false, "Invalid PUCCH format combination: curr=%d next=%d\n", cr->format.present, nr->format.present);
}

// 38.213 section 9.2.5
static void merge_resources(PUCCH_sched_t *res, int num_res, NR_PUCCH_Config_t *pucch_Config)
{
  for (int i = 0; i < num_res - 1; i++)
    merge_pair(res, i, pucch_Config);
}

/* @brief Order PUCCH occasions in the slot and merge overlapping ones.
 * The UE multiplexes HARQ-ACK information, SR, and CSI reports (TS 38.213 9.2.5.1, 9.2.5.2) */
void multiplex_pucch_resource(NR_UE_MAC_INST_t *mac, PUCCH_sched_t *pucch, int num_res)
{
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP->pucch_Config;
  order_resources(pucch, num_res);
  // following pseudocode in Ref. 38.213 section 9.2.5 to multiplex resources
  int j = 0;
  int o = 0;
  while (j <= num_res - 1) {
    if ((j < num_res - 1) && check_overlapping_resources(pucch, j, o)) {
      o++;
      j++;
    } else {
      if (o > 0) { // if there are overlapping resources, merge them
        merge_resources(&pucch[j - o], o + 1, pucch_Config);
        // move the resources to occupy the places left empty
        int num_empty = o;
        for (int i = j; i < num_res; i++)
          pucch[i - num_empty] = pucch[i];
        for (int e = num_res - num_empty; e < num_res; e++)
          memset(&pucch[e], 0, sizeof(pucch[e]));
        j = 0;
        o = 0;
        num_res = num_res - num_empty;
        order_resources(pucch, num_res);
      } else
        j++;
    }
  }
}

void configure_initial_pucch(PUCCH_sched_t *pucch, int res_ind, int pucch_ResourceCommon)
{
  /* see TS 38.213 9.2.1  PUCCH Resource Sets */
  int delta_PRI = res_ind;
  int n_CCE_0 = pucch->n_CCE;
  int N_CCE_0 = pucch->N_CCE;
  if (N_CCE_0 == 0)
    AssertFatal(1 == 0, "PUCCH No compatible pucch format found\n");
  int r_PUCCH = ((2 * n_CCE_0) / N_CCE_0) + (2 * delta_PRI);
  pucch->initial_pucch_id = r_PUCCH;
  pucch->pucch_resource = NULL; // not a dedicated PUCCH-Resource
  AssertFatal(pucch_ResourceCommon >= 0 && pucch_ResourceCommon < sizeofArray(initial_pucch_resource),
              "invalid pucch_ResourceCommon %d\n",
              pucch_ResourceCommon);
  pucch->pucch_ResourceCommon = pucch_ResourceCommon; // Table 9.2.1-1 row index
}

/*******************************************************************
*
* NAME :         get_downlink_ack
*
* PARAMETERS :   ue context
*                processing slots of reception/transmission
*                gNB_id identifier
*
* RETURN :       o_ACK acknowledgment data
*                o_ACK_number_bits number of bits for acknowledgment
*
* DESCRIPTION :  return acknowledgment value
*                TS 38.213 9.1.3 Type-2 HARQ-ACK codebook determination
*
*          --+--------+-------+--------+-------+---  ---+-------+--
*            | PDCCH1 |       | PDCCH2 |PDCCH3 |        | PUCCH |
*          --+--------+-------+--------+-------+---  ---+-------+--
*    DAI_DL      1                 2       3              ACK for
*                V                 V       V        PDCCH1, PDDCH2 and PCCH3
*                |                 |       |               ^
*                +-----------------+-------+---------------+
*
*                PDCCH1, PDCCH2 and PDCCH3 are PDCCH monitoring occasions
*                M is the total of monitoring occasions
*
*********************************************************************/

bool get_downlink_ack(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, PUCCH_sched_t *pucch)
{
  uint32_t ack_data[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}};
  uint32_t dai[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}};       /* for serving cell */
  uint32_t dai_total[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}}; /* for multiple cells */
  int number_harq_feedback = 0;
  uint32_t dai_max = 0;
  /* Aggregated Table 9.2.1-1 row for this PUCCH occasion, -1 if all HARQs are dedicated */
  int occasion_pucch_ResourceCommon = -1;

  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  bool two_transport_blocks = false;
  int number_of_code_word = 1;
  if (current_DL_BWP
      && current_DL_BWP->pdsch_Config
      && current_DL_BWP->pdsch_Config->maxNrofCodeWordsScheduledByDCI
      && current_DL_BWP->pdsch_Config->maxNrofCodeWordsScheduledByDCI[0] == 2) {
    two_transport_blocks = true;
    number_of_code_word = 2;
  }

  const int num_dl_harq = get_nrofHARQ_ProcessesForPDSCH(&mac->sc_info);
  int res_ind = -1;

  /* look for dl acknowledgment which should be done on current uplink slot */
  for (int code_word = 0; code_word < number_of_code_word; code_word++) {
    for (int dl_harq_pid = 0; dl_harq_pid < num_dl_harq; dl_harq_pid++) {
      NR_UE_DL_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[dl_harq_pid][code_word];
      if (current_harq->active) {
        LOG_D(PHY, "HARQ pid %d is active for %d.%d\n",
              dl_harq_pid, current_harq->ul_frame, current_harq->ul_slot);
        /* check if current tx slot should transmit downlink acknowlegment */
        if (current_harq->ul_frame == frame && current_harq->ul_slot == slot) {
          if (res_ind != -1 && res_ind != current_harq->pucch_resource_indicator)
            LOG_E(NR_MAC,
                  "Value of pucch_resource_indicator %d not matching with what set before %d (Possibly due to a false DCI) \n",
                  current_harq->pucch_resource_indicator,
                  res_ind);
          else {
            if (!current_harq->ack_received)
              LOG_E(NR_MAC, "DLSCH ACK/NACK reporting initiated for harq pid %d before DLSCH decoding completed\n", dl_harq_pid);

            if (get_FeedbackDisabled(mac->sc_info.downlinkHARQ_FeedbackDisabled_r17, dl_harq_pid)) {
              LOG_W(NR_MAC, "skipping DLSCH ACK/NACK reporting for harq pid %d\n", dl_harq_pid);
              current_harq->active = false;
              current_harq->ack_received = false;
              continue;
            }

            if (current_harq->dai_cumul == 0) {
              LOG_E(NR_MAC,"PUCCH Downlink DAI is invalid\n");
              return false;
            } else if (current_harq->dai_cumul > dai_max) {
              dai_max = current_harq->dai_cumul;
            }

            number_harq_feedback++;
            int dai_index = current_harq->dai_cumul - 1;
            if (current_harq->ack_received) {
              ack_data[code_word][dai_index] = current_harq->ack;
              current_harq->active = false;
              current_harq->ack_received = false;
            } else {
              ack_data[code_word][dai_index] = 0;
            }
            dai[code_word][dai_index] = (dai_index % 4) + 1; // value between 1 and 4
            int temp_ind = current_harq->pucch_resource_indicator;
            AssertFatal(res_ind == -1 || res_ind == temp_ind,
                        "Current resource index %d does not match with previous resource index %d\n",
                        temp_ind,
                        res_ind);
            res_ind = temp_ind;
            pucch->harq_ack_pucch_res_ind = temp_ind;
            pucch->n_CCE = current_harq->n_CCE;
            pucch->N_CCE = current_harq->N_CCE;
            // Collect 9.2.1 set index for this occasion, all common HARQs must agree
            if (current_harq->pucch_ResourceCommon >= 0) {
              AssertFatal(occasion_pucch_ResourceCommon < 0 || occasion_pucch_ResourceCommon == current_harq->pucch_ResourceCommon,
                          "mixed occasion pucch_ResourceCommon %d vs %d\n",
                          occasion_pucch_ResourceCommon,
                          current_harq->pucch_ResourceCommon);
              occasion_pucch_ResourceCommon = current_harq->pucch_ResourceCommon;
            }
            LOG_D(NR_MAC,"%4d.%2d Sent %d ack on harq pid %d\n", frame, slot, current_harq->ack, dl_harq_pid);
          }
        }
      }
    }
  }

  /* no any ack to transmit */
  if (number_harq_feedback == 0) {
    return false;
  }
  else  if (number_harq_feedback > (sizeof(uint32_t)*8)) {
    LOG_E(MAC,"PUCCH number of ack bits exceeds payload size\n");
    return false;
  }

  for (int code_word = 0; code_word < number_of_code_word; code_word++) {
    for (uint32_t i = 0; i < dai_max ; i++ ) {
      if (dai[code_word][i] == 0) {
        dai[code_word][i] = (i % 4) + 1; // it covers case for which PDCCH DCI has not been successfully decoded and so it has been missed
        ack_data[code_word][i] = 0;      // nack data transport block which has been missed
        number_harq_feedback++;
      }
      if (two_transport_blocks == true) {
        dai_total[code_word][i] = dai[code_word][i]; /* for a single cell, dai_total is the same as dai of first cell */
      }
    }
  }

  int M = dai_max;
  int j = 0;
  uint32_t V_temp = 0;
  uint32_t V_temp2 = 0;
  int O_ACK = 0;
  uint8_t o_ACK = 0;
  int O_bit_number_cw0 = 0;
  int O_bit_number_cw1 = 0;

  for (int m = 0; m < M ; m++) {
    if (dai[0][m] <= V_temp) {
      j = j + 1;
    }

    V_temp = dai[0][m]; /* value of the counter DAI for format 1_0 and format 1_1 on serving cell c */

    if (dai_total[0][m] == 0) {
      V_temp2 = dai[0][m];
    } else {
      V_temp2 = dai[1][m];         /* second code word has been received */
      O_bit_number_cw1 = (8 * j) + 2*(V_temp - 1) + 1;
      o_ACK = o_ACK | (ack_data[1][m] << O_bit_number_cw1);
    }

    if (two_transport_blocks == true) {
      O_bit_number_cw0 = (8 * j) + 2*(V_temp - 1);
    }
    else {
      O_bit_number_cw0 = (4 * j) + (V_temp - 1);
    }

    o_ACK = o_ACK | (ack_data[0][m] << O_bit_number_cw0);
    LOG_D(MAC,"m %d bit number %d o_ACK %d\n",m,O_bit_number_cw0,o_ACK);
  }

  if (V_temp2 < V_temp) {
    j = j + 1;
  }

  if (two_transport_blocks == true) {
    O_ACK = 2 * ( 4 * j + V_temp2);  /* for two transport blocks */
  }
  else {
    O_ACK = 4 * j + V_temp2;         /* only one transport block */
  }

  if (number_harq_feedback != O_ACK) {
    LOG_E(MAC,"PUCCH Error for number of bits for acknowledgment\n");
    return false;
  }

  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;
  // Any HARQ on 9.2.1 keeps the whole occasion on Table 9.2.1-1 (PRI/set at DCI)
  // Else if dedicated ResourceSet still absent at TX, use current BWP pucch-ResourceCommon
  if (occasion_pucch_ResourceCommon >= 0 || !nr_ue_has_dedicated_pucch_resource_set(pucch_Config)) {
    int pucch_ResourceCommon = occasion_pucch_ResourceCommon;
    if (pucch_ResourceCommon < 0) { // Fall back to active BWP pucch-ResourceCommon
      AssertFatal(current_UL_BWP && current_UL_BWP->pucch_ConfigCommon && current_UL_BWP->pucch_ConfigCommon->pucch_ResourceCommon,
                  "pucch_ResourceCommon NULL\n");
      pucch_ResourceCommon = *current_UL_BWP->pucch_ConfigCommon->pucch_ResourceCommon;
    }
    configure_initial_pucch(pucch, res_ind, pucch_ResourceCommon);
  } else {
    int resource_set_id = find_pucch_resource_set(pucch_Config, O_ACK);
    int n_list = pucch_Config->resourceSetToAddModList->list.count;
    AssertFatal(resource_set_id < n_list, "Invalid PUCCH resource set id %d\n", resource_set_id);
    n_list = pucch_Config->resourceSetToAddModList->list.array[resource_set_id]->resourceList.list.count;
    AssertFatal(res_ind < n_list, "Invalid PUCCH resource id %d\n", res_ind);
    long *acknack_resource_id =
        pucch_Config->resourceSetToAddModList->list.array[resource_set_id]->resourceList.list.array[res_ind];
    AssertFatal(acknack_resource_id != NULL, "Couldn't find PUCCH Resource ID in ResourceSet\n");
    NR_PUCCH_Resource_t *acknack_resource = find_pucch_resource_from_list(pucch_Config->resourceToAddModList, *acknack_resource_id);
    AssertFatal(acknack_resource != NULL, "Couldn't find PUCCH Resource ID for ACK/NACK in PUCCH resource list\n");
    pucch->pucch_resource = acknack_resource;
    LOG_D(MAC, "frame %d slot %d pucch acknack payload %d\n", frame, slot, o_ACK);
  }
  pucch->ack_payload = reverse_bits(o_ACK, number_harq_feedback);
  pucch->n_harq = number_harq_feedback;

  return (number_harq_feedback > 0);
}

/*! \fn int nr_ue_get_SR(NR_UE_MAC_INST_t *mac, frame_t frame, slot_t slot, NR_SchedulingRequestId_t sr_id);
   \brief This function schedules a positive or negative SR for schedulingRequestID sr_id
          depending on the presence of any active SR and the prohibit timer.
          If the max number of retransmissions is reached, it triggers a new RA  */
static int nr_ue_get_SR(NR_UE_MAC_INST_t *mac, frame_t frame, slot_t slot, NR_SchedulingRequestId_t sr_id)
{
  // no UL-SCH resources available for this tti && UE has a valid PUCCH resources for SR configuration for this tti
  NR_UE_SCHEDULING_INFO *si = &mac->scheduling_info;
  nr_sr_info_t *sr_info = &si->sr_info[sr_id];
  if (!sr_info->active_SR_ID) {
    LOG_E(NR_MAC, "SR triggered for an inactive SR with ID %ld\n", sr_id);
    return 0;
  }

  LOG_D(NR_MAC,
        "[UE %d] Frame %d slot %d SR %s for ID %ld, timer %d\n",
        mac->ue_id,
        frame,
        slot,
        sr_info->pending ? "pending" : "not pending",
        sr_id, // todo
        nr_timer_is_active(&sr_info->prohibitTimer));

  // TODO check if the PUCCH resource for the SR transmission occasion does not overlap with a UL-SCH resource
  if (!sr_info->pending || nr_timer_is_active(&sr_info->prohibitTimer))
    return 0;

  if (sr_info->counter < sr_info->maxTransmissions) {
    sr_info->counter++;
    // start the sr-prohibittimer
    nr_timer_start(&sr_info->prohibitTimer);
    LOG_D(NR_MAC, "[UE %d] Frame %d slot %d instruct the physical layer to signal the SR (counter/sr_TransMax %d/%d)\n",
          mac->ue_id,
          frame,
          slot,
          sr_info->counter,
          sr_info->maxTransmissions);
    return 1;
  }

  LOG_W(NR_MAC,
        "[UE %d] SR not served! SR counter %d reached sr_MaxTransmissions %d\n",
        mac->ue_id,
        sr_info->counter,
        sr_info->maxTransmissions);

  // initiate a Random Access procedure (see clause 5.1) on the SpCell and cancel all pending SRs.
  sr_info->pending = false;
  sr_info->counter = 0;
  nr_timer_stop(&sr_info->prohibitTimer);
  schedule_RA_after_SR_failure(mac);
  return -1;
}

bool trigger_periodic_scheduling_request(NR_UE_MAC_INST_t *mac, PUCCH_sched_t *pucch, frame_t frame, int slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;

  if(!pucch_Config ||
     !pucch_Config->schedulingRequestResourceToAddModList ||
     pucch_Config->schedulingRequestResourceToAddModList->list.count == 0)
    return false; // SR not configured

  int sr_count = 0;
  for (int id = 0; id < pucch_Config->schedulingRequestResourceToAddModList->list.count; id++) {
    NR_SchedulingRequestResourceConfig_t *sr_Config = pucch_Config->schedulingRequestResourceToAddModList->list.array[id];
    int SR_period; int SR_offset;

    find_period_offset_SR(sr_Config, &SR_period, &SR_offset);
    const int n_slots_frame = mac->frame_structure.numb_slots_frame;
    int sfn_sf = frame * n_slots_frame + slot;

    if ((sfn_sf - SR_offset) % SR_period == 0) {
      LOG_D(MAC, "Scheduling Request active in frame %d slot %d \n", frame, slot);
      if (!sr_Config->resource) {
        LOG_E(MAC, "No resource associated with SR. SR not scheduled\n");
        break;
      }
      NR_PUCCH_Resource_t *sr_pucch =
          find_pucch_resource_from_list(pucch_Config->resourceToAddModList, *sr_Config->resource);
      AssertFatal(sr_pucch != NULL, "Couldn't find PUCCH Resource ID for SR in PUCCH resource list\n");
      pucch->pucch_resource = sr_pucch;
      pucch->n_sr = 1;
      /* sr_payload = 1 means that this is a positive SR, sr_payload = 0 means that it is a negative SR */
      int ret = nr_ue_get_SR(mac, frame, slot, sr_Config->schedulingRequestID);
      if (ret < 0) {
        memset(pucch, 0, sizeof(*pucch));
        return false;
      }
      pucch->sr_payload = ret;
      sr_count++;
      AssertFatal(sr_count < 2, "Cannot handle more than 1 SR per slot yet\n");
    }
  }
  return sr_count > 0;
}

// section 5.2.5 of 38.214
int compute_csi_priority(NR_UE_MAC_INST_t *mac, NR_CSI_ReportConfig_t *csirep)
{
  int y = 4 - csirep->reportConfigType.present;
  int k = csirep->reportQuantity.present == NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP
                  || csirep->reportQuantity.present == NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP
              ? 0
              : 1;
  int Ncells = 32; // maxNrofServingCells
  int c = mac->servCellIndex;
  int s = csirep->reportConfigId;
  int Ms = 48; // maxNrofCSI-ReportConfigurations

  return 2 * Ncells * Ms * y + Ncells * Ms * k + Ms * c + s;
}

int nr_get_csi_measurements(NR_UE_MAC_INST_t *mac,
                            frame_t frame,
                            int slot,
                            nfapi_nr_ue_csi_payload_t *csi_payload,
                            NR_PUCCH_Resource_t **csi_pucch)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;

  if (!mac->sc_info.csi_MeasConfig || !pucch_Config)
    return 0;

  int num_csi = 0;
  NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

  int csi_priority = INT_MAX;
  for (int csi_report_id = 0; csi_report_id < csi_measconfig->csi_ReportConfigToAddModList->list.count; csi_report_id++) {
    NR_CSI_ReportConfig_t *csirep = csi_measconfig->csi_ReportConfigToAddModList->list.array[csi_report_id];

    AssertFatal(csirep->reportConfigType.present == NR_CSI_ReportConfig__reportConfigType_PR_aperiodic
                    || csirep->reportConfigType.present == NR_CSI_ReportConfig__reportConfigType_PR_periodic,
                "Not supported CSI report type\n");

    // Aperiodic CSI measurements handled in nr_ue_aperiodic_csi_reporting function
    if (csirep->reportConfigType.present != NR_CSI_ReportConfig__reportConfigType_PR_periodic)
      continue;

    int period, offset;
    csi_period_offset(csirep, NULL, &period, &offset);
    const int n_slots_frame = mac->frame_structure.numb_slots_frame;
    // Check if current slot falls in the report period
    if (!is_periodic_occasion(frame, slot, n_slots_frame, period, offset))
      continue;

    int csi_res_id = -1;
    for (int i = 0; i < csirep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.count; i++) {
      const NR_PUCCH_CSI_Resource_t *pucchcsires = csirep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.array[i];
      if (pucchcsires->uplinkBandwidthPartId == current_UL_BWP->bwp_id) {
        csi_res_id = pucchcsires->pucch_Resource;
        break;
      }
    }
    if (csi_res_id < 0) {
      // This CSI Report ID is not associated with current active BWP
      continue;
    }
    *csi_pucch = find_pucch_resource_from_list(pucch_Config->resourceToAddModList, csi_res_id);
    AssertFatal(*csi_pucch != NULL, "Couldn't find PUCCH Resource ID for CSI in PUCCH resource list\n");
    LOG_D(NR_MAC, "Preparing CSI report in frame %d slot %d CSI report ID %d\n", frame, slot, csi_report_id);
    int temp_priority = compute_csi_priority(mac, csirep);
    if (num_csi > 0) {
      // need to verify if we can multiplex multiple CSI report
      if (pucch_Config->multi_CSI_PUCCH_ResourceList) {
        AssertFatal(false, "Multiplexing multiple CSI report in a single PUCCH not supported yet\n");
      } else if (temp_priority < csi_priority) {
        // we discard previous report
        csi_priority = temp_priority;
        num_csi = 1;
        // 38.214 section 5.2.3: "For both Type I and Type II reports configured for PUCCH but transmitted
        // on PUSCH, the determination of the payload for CSI part 1 and CSI part 2 follows that of PUCCH
        // as described in Clause 5.2.4." Hence WIDEBAND_ON_PUCCH is used here regardless of whether
        // the CSI report is sent on PUCCH or PUSCH.
        *csi_payload = nr_get_csi_payload(mac, csi_report_id, WIDEBAND_ON_PUCCH, csi_measconfig);
      } else
        continue;
    } else {
      num_csi = 1;
      csi_priority = temp_priority;
      *csi_payload = nr_get_csi_payload(mac, csi_report_id, WIDEBAND_ON_PUCCH, csi_measconfig);
    }
  }
  return num_csi;
}

// Referred - Table 10.1.16.1-1 in 38.133 V16.7.0.
static uint8_t get_sinr_index(float sinr)
{
  int index = sinr * 2 + 47;
  if (sinr >= 40)
    index = 127;
  if (sinr < -23)
    index = 0;

  return index;
}

// Reffered Table 10.1.16.1-2 in 38.133 V16.7.0. Differential value is reported.
static uint8_t get_sinr_diff_index(float best_sinr, float current_sinr)
{
  int diff = best_sinr - current_sinr;
  if (diff >= 15)
    return 15;
  else if (diff <= 0)
    return 0;
  else
    return diff;
}

// Comparison function for sorting SSB SINR measurements in descending order
static int compare_ssb_sinr(const void *a, const void *b)
{
  const NR_RSRP_meas_t *ma = (const NR_RSRP_meas_t *)a;
  const NR_RSRP_meas_t *mb = (const NR_RSRP_meas_t *)b;
  return mb->ssb_sinr_dB - ma->ssb_sinr_dB;
}

static nfapi_nr_ue_csi_payload_t get_ssb_sinr_payload(NR_UE_MAC_INST_t *mac,
                                                      const struct NR_CSI_ReportConfig *csi_reportconfig,
                                                      const NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                                      const NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  int nb_ssb = 0; // nb of ssb in the resource
  int nb_meas = 0; // nb of ssb to report measurements on
  int bits = 0;
  uint64_t temp_payload = 0;

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {
    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {
      if (csi_reportconfig->groupBasedBeamReporting.present == NR_CSI_ReportConfig__groupBasedBeamReporting_PR_disabled) {
        if (csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS != NULL)
          nb_meas = *(csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS) + 1;
        else
          nb_meas = 1;
      } else
        nb_meas = 2;

      const struct NR_CSI_SSB_ResourceSet__csi_SSB_ResourceList *SSB_resource = NULL;
      for (int csi_ssb_idx = 0; csi_ssb_idx < csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.count; csi_ssb_idx++) {
        if (csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceSetId
            == *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->csi_SSB_ResourceSetList->list.array[0])) {
          SSB_resource = &csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceList;
          /// only one SSB resource set from spec 38.331 IE CSI-ResourceConfig
          nb_ssb = SSB_resource->list.count;
          break;
        }
      }

      AssertFatal(nb_ssb > 0, "No SSB found in the resource set\n");
      AssertFatal(nb_meas <= 4,"Can't report more than 4 RSRPs\n");
      int ssbri_bits = ceil_log2_u32(nb_ssb);

      // map SSB index to SSB resource table index, copy measurements, sort in descending order
      NR_RSRP_meas_t sorted_sinr_measurements[nb_ssb];
      int sorted_idx = 0;
      for (int measured_ssb_idx = 0; measured_ssb_idx < MAX_NB_SSB; measured_ssb_idx++) {
        // searching for the SSB index in the SSB resource table
        for (int ssb_resource = 0; ssb_resource < nb_ssb; ssb_resource++) {
          if (*SSB_resource->list.array[ssb_resource] == measured_ssb_idx) {
            sorted_sinr_measurements[sorted_idx].ssb_index = ssb_resource;
            sorted_sinr_measurements[sorted_idx].ssb_rsrp_dBm = mac->ssb_measurements[measured_ssb_idx].ssb_rsrp_dBm;
            sorted_sinr_measurements[sorted_idx].ssb_sinr_dB = mac->ssb_measurements[measured_ssb_idx].ssb_sinr_dB;
            sorted_idx++;
            break;
          }
        }
      }
      qsort(sorted_sinr_measurements, nb_ssb, sizeof(NR_RSRP_meas_t), compare_ssb_sinr);

      // TS38.212 v16.5.0: Table 6.3.1.1.2-8A
      for (int i = 0; i < nb_meas; i++) {
        if (ssbri_bits > 0) {
          uint8_t ssbi = sorted_sinr_measurements[i].ssb_index;
          temp_payload |= (reverse_bits(ssbi, ssbri_bits) << bits);
          bits += ssbri_bits;
        }
      }

      uint8_t sinr_idx = get_sinr_index(sorted_sinr_measurements[0].ssb_sinr_dB);
      temp_payload |= (reverse_bits(sinr_idx, 7) << bits);
      bits += 7; // 7 bits for highest SINR

      for (int i = 1; i < nb_meas; i++) {
        sinr_idx = get_sinr_diff_index(sorted_sinr_measurements[0].ssb_sinr_dB, sorted_sinr_measurements[i].ssb_sinr_dB);
        temp_payload |= (reverse_bits(sinr_idx, 4) << bits);
        bits += 4; // 4 bits for differential SINR
      }
      break; // resource found
    }
  }
  int max_bits = sizeof(((nfapi_nr_ue_csi_payload_t *)0)->part1_payload) * 8;
  AssertFatal(bits <= max_bits, "Not supporting CSI report with more than %d bits (payload: %d bits)\n", max_bits, bits);
  nfapi_nr_ue_csi_payload_t csi = {.part1_payload = temp_payload, .part2_payload = 0, .p1_bits = bits, csi.p2_bits = 0};
  return csi;
}

// Comparison function for sorting SSB RSRP measurements in descending order
static int compare_ssb_rsrp(const void *a, const void *b)
{
  const NR_RSRP_meas_t *ma = (const NR_RSRP_meas_t *)a;
  const NR_RSRP_meas_t *mb = (const NR_RSRP_meas_t *)b;
  return mb->ssb_rsrp_dBm - ma->ssb_rsrp_dBm;
}

// returns index from differential RSRP
// according to Table 10.1.6.1-2 in 38.133
static uint8_t get_rsrp_diff_index(int best_rsrp, int current_rsrp)
{
  int diff = best_rsrp-current_rsrp;
  if (diff>30)
    return 15;
  else
    return (diff>>1);
}

static nfapi_nr_ue_csi_payload_t get_ssb_rsrp_payload(NR_UE_MAC_INST_t *mac,
                                          const struct NR_CSI_ReportConfig *csi_reportconfig,
                                          const NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                          const NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  int nb_ssb = 0;  // nb of ssb in the resource
  int nb_meas = 0; // nb of ssb to report measurements on
  int bits = 0;
  uint64_t temp_payload = 0;

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {
    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {

      if (csi_reportconfig->groupBasedBeamReporting.present == NR_CSI_ReportConfig__groupBasedBeamReporting_PR_disabled) {
        if (csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS != NULL)
          nb_meas = *(csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS) + 1;
        else
          nb_meas = 1;
      } else
        nb_meas = 2;

      const struct NR_CSI_SSB_ResourceSet__csi_SSB_ResourceList *SSB_resource = NULL;
      for (int csi_ssb_idx = 0; csi_ssb_idx < csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.count; csi_ssb_idx++) {
        if (csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceSetId ==
            *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->csi_SSB_ResourceSetList->list.array[0])){
          SSB_resource = &csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceList;
          ///only one SSB resource set from spec 38.331 IE CSI-ResourceConfig
          nb_ssb = SSB_resource->list.count;
          break;
        }
      }

      AssertFatal(nb_ssb > 0,"No SSB found in the resource set\n");
      AssertFatal(nb_meas <= 4,"Can't report more than 4 RSRPs\n");
      int ssbri_bits = ceil_log2_u32(nb_ssb);

      // map SSB index to SSB resource table index, copy measurements, sort in descending order
      NR_RSRP_meas_t sorted_rsrp_measurements[nb_ssb];
      int sorted_idx = 0;
      for (int measured_ssb_idx = 0; measured_ssb_idx < MAX_NB_SSB; measured_ssb_idx++) {
        // searching for the SSB index in the SSB resource table
        for (int ssb_resource = 0; ssb_resource < nb_ssb; ssb_resource++) {
          if (*SSB_resource->list.array[ssb_resource] == measured_ssb_idx) {
            sorted_rsrp_measurements[sorted_idx].ssb_index = ssb_resource;
            sorted_rsrp_measurements[sorted_idx].ssb_rsrp_dBm = mac->ssb_measurements[measured_ssb_idx].ssb_rsrp_dBm;
            sorted_rsrp_measurements[sorted_idx].ssb_sinr_dB = mac->ssb_measurements[measured_ssb_idx].ssb_sinr_dB;
            sorted_idx++;
            break;
          }
        }
      }
      qsort(sorted_rsrp_measurements, nb_ssb, sizeof(NR_RSRP_meas_t), compare_ssb_rsrp);

      for (int i = 0; i < nb_meas; i++) {
        if (ssbri_bits > 0) {
          uint32_t ssbi = sorted_rsrp_measurements[i].ssb_index;
          temp_payload |= (reverse_bits(ssbi, ssbri_bits) << bits);
          bits += ssbri_bits;
        }
      }

      uint8_t rsrp_idx = get_rsrp_index(sorted_rsrp_measurements[0].ssb_rsrp_dBm);
      temp_payload |= (reverse_bits(rsrp_idx, 7) << bits);
      bits += 7; // 7 bits for highest RSRP

      // from the second SSB, differential report
      for (int i = 1; i < nb_meas; i++) {
        rsrp_idx = get_rsrp_diff_index(sorted_rsrp_measurements[0].ssb_rsrp_dBm,sorted_rsrp_measurements[i].ssb_rsrp_dBm);
        temp_payload |= (reverse_bits(rsrp_idx, 4) << bits);
        bits += 4; // 4 bits for subsequent RSRP
      }
      break; // resource found
    }
  }
  int max_bits = sizeof(((nfapi_nr_ue_csi_payload_t *)0)->part1_payload) * 8;
  AssertFatal(bits <= max_bits, "Not supporting CSI report with more than %d bits (payload: %d bits)\n", max_bits, bits);

  nfapi_nr_ue_csi_payload_t csi = {.part1_payload = temp_payload, .part2_payload = 0, .p1_bits = bits, .p2_bits = 0};
  return csi;
}

// Pack i_1,1 || i_1,2 || i_1,3 into the X1 PMI field, with i_1,1 in the most significant bits
// (ordering per TS 38.212 Sections 6.3.1.1.2 and 6.3.2.1.2).
// Sub-field widths are inferred from the total pmi_x1_bitlen, which is unique across the Type1 Single Panel configurations
// covered by the PMI estimator:
//   ports | rank | pmi_x1_bitlen | i_1,1 | i_1,2 | i_1,3
//     2   | 1,2  |       0       |   -   |   -   |   -
//     4   |   1  |       3       |   3   |   0   |   0
//     4   |   2  |       4       |   3   |   0   |   1
//     8   |   1  |       6       |   3   |   3   |   0
//     8   |   2  |       8       |   3   |   3   |   2
static uint16_t pack_pmi_x1(int pmi_x1_bitlen, uint8_t i_1_1, uint8_t i_1_2, uint8_t i_1_3)
{
  int bits_12 = 0, bits_13 = 0;
  switch (pmi_x1_bitlen) {
    case 0:
      return 0; // 2-port: no X1
    case 3:
      break; // 4-port rank 1
    case 4:
      bits_13 = 1;
      break; // 4-port rank 2
    case 6:
      bits_12 = 3;
      break; // 8-port rank 1
    case 8:
      bits_12 = 3;
      bits_13 = 2;
      break; // 8-port rank 2
    default:
      LOG_W(NR_MAC, "pack_pmi_x1: unsupported pmi_x1_bitlen %d (Type1 SinglePanel only)\n", pmi_x1_bitlen);
      return 0;
  }
  return ((uint16_t)i_1_1 << (bits_12 + bits_13)) | ((uint16_t)i_1_2 << bits_13) | (uint16_t)i_1_3;
}

static nfapi_nr_ue_csi_payload_t get_csirs_RI_PMI_CQI_payload(NR_UE_MAC_INST_t *mac,
                                                              const struct NR_CSI_ReportConfig *csi_reportconfig,
                                                              const NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                                              const NR_CSI_MeasConfig_t *csi_MeasConfig,
                                                              const CSI_mapping_t mapping_type)
{
  int p1_bits = 0;
  int p2_bits = 0;
  uint64_t temp_payload_1 = 0;
  uint64_t temp_payload_2 = 0;
  AssertFatal(mapping_type != SUBBAND_ON_PUCCH, "CSI mapping for subband PMI and CQI not implemented\n");

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {
    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId != csi_ResourceConfigId)
      continue;

    for (int csi_idx = 0; csi_idx < csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.count; csi_idx++) {
      if (csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[csi_idx]->nzp_CSI_ResourceSetId
          != *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[0]))
        continue;

      nr_csi_report_t *csi_report = NULL;
      for (int i = 0; i < MAX_CSI_REPORTCONFIG; i++) {
        if (mac->csi_report_template[i].reportConfigId == csi_reportconfig->reportConfigId) {
          csi_report = &mac->csi_report_template[i];
          break;
        }
      }
      AssertFatal(csi_report, "Couldn't find CSI report with ID %ld\n", csi_reportconfig->reportConfigId);

      const uint8_t ri = mac->csirs_measurements.ri;
      const int cri_bitlen = csi_report->csi_meas_bitlen.cri_bitlen;
      const int ri_bitlen = csi_report->csi_meas_bitlen.ri_bitlen;
      const int pmi_x1_bitlen = csi_report->csi_meas_bitlen.pmi_x1_bitlen[ri];
      const int pmi_x2_bitlen = csi_report->csi_meas_bitlen.pmi_x2_bitlen[ri];
      const int cqi_bitlen = csi_report->csi_meas_bitlen.cqi_bitlen[ri];
      int padding_bitlen = 0;

      // Reconstruct X1 from the separate i_1,1 / i_1,2 / i_1,3 fields; X2 is just i_2.
      const uint16_t pmi_x1 =
          pack_pmi_x1(pmi_x1_bitlen, mac->csirs_measurements.i_1_1, mac->csirs_measurements.i_1_2, mac->csirs_measurements.i_1_3);
      const uint8_t pmi_x2 = mac->csirs_measurements.i_2;

      // TODO: Improvements will be needed to cri_bitlen>0 and pmi_x1_bitlen>0
      if (mapping_type == ON_PUSCH) {
        p1_bits = cri_bitlen + ri_bitlen + cqi_bitlen;
        p2_bits = pmi_x1_bitlen + pmi_x2_bitlen;
        temp_payload_1 = ((uint64_t)0 /* cri */ << (cqi_bitlen + ri_bitlen)) | ((uint64_t)ri << cqi_bitlen)
                         | (uint64_t)mac->csirs_measurements.cqi;
        temp_payload_2 = ((uint64_t)pmi_x1 << pmi_x2_bitlen) | (uint64_t)pmi_x2;
      } else {
        p1_bits = nr_get_csi_bitlen(csi_report);
        padding_bitlen = p1_bits - (cri_bitlen + ri_bitlen + pmi_x1_bitlen + pmi_x2_bitlen + cqi_bitlen);
        temp_payload_1 = ((uint64_t)0 /* cri */ << (cqi_bitlen + pmi_x2_bitlen + pmi_x1_bitlen + padding_bitlen + ri_bitlen))
                         | ((uint64_t)ri << (cqi_bitlen + pmi_x2_bitlen + pmi_x1_bitlen + padding_bitlen))
                         | ((uint64_t)pmi_x1 << (cqi_bitlen + pmi_x2_bitlen)) | ((uint64_t)pmi_x2 << cqi_bitlen)
                         | (uint64_t)mac->csirs_measurements.cqi;
      }

      temp_payload_1 = reverse_bits(temp_payload_1, p1_bits);
      temp_payload_2 = reverse_bits(temp_payload_2, p2_bits);

      LOG_D(NR_MAC, "cri_bitlen = %d\n", cri_bitlen);
      LOG_D(NR_MAC, "ri_bitlen = %d\n", ri_bitlen);
      LOG_D(NR_MAC,
            "pmi_x1_bitlen = %d (i_1,1=%u i_1,2=%u i_1,3=%u -> X1=0x%x)\n",
            pmi_x1_bitlen,
            mac->csirs_measurements.i_1_1,
            mac->csirs_measurements.i_1_2,
            mac->csirs_measurements.i_1_3,
            pmi_x1);
      LOG_D(NR_MAC, "pmi_x2_bitlen = %d (i_2=%u)\n", pmi_x2_bitlen, pmi_x2);
      LOG_D(NR_MAC, "cqi_bitlen = %d\n", cqi_bitlen);
      LOG_D(NR_MAC, "csi_part1_payload = 0x%lx\n", temp_payload_1);
      LOG_D(NR_MAC, "csi_part2_payload = 0x%lx\n", temp_payload_2);
      LOG_D(NR_MAC, "part1_bits = %d\n", p1_bits);
      LOG_D(NR_MAC, "part2_bits = %d\n", p2_bits);
      break;
    }
  }
  AssertFatal(p1_bits <= 32 && p2_bits <= 32, "Not supporting CSI report with more than 32 bits\n");
  nfapi_nr_ue_csi_payload_t csi = {
      .part1_payload = temp_payload_1,
      .part2_payload = temp_payload_2,
      .p1_bits = p1_bits,
      .p2_bits = p2_bits,
  };
  return csi;
}

static nfapi_nr_ue_csi_payload_t get_csirs_RSRP_payload(NR_UE_MAC_INST_t *mac,
                                            const struct NR_CSI_ReportConfig *csi_reportconfig,
                                            const NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                            const NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  int n_bits = 0;
  uint64_t temp_payload = 0;

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {

    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {

      for (int csi_idx = 0; csi_idx < csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.count; csi_idx++) {
        if (csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[csi_idx]->nzp_CSI_ResourceSetId ==
            *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[0])) {

          nr_csi_report_t *csi_report = NULL;
          for (int i = 0; i < MAX_CSI_REPORTCONFIG; i++) {
            if (mac->csi_report_template[i].reportConfigId == csi_reportconfig->reportConfigId) {
              csi_report = &mac->csi_report_template[i];
              break;
            }
          }
          AssertFatal(csi_report, "Couldn't find CSI report with ID %ld\n", csi_reportconfig->reportConfigId);
          n_bits = nr_get_csi_bitlen(csi_report);
          int cri_ssbri_bitlen = csi_report->CSI_report_bitlen.cri_ssbri_bitlen;
          int rsrp_bitlen = csi_report->CSI_report_bitlen.rsrp_bitlen;
          int diff_rsrp_bitlen = csi_report->CSI_report_bitlen.diff_rsrp_bitlen;

          if (cri_ssbri_bitlen > 0) {
            LOG_E(NR_MAC, "Implementation for cri_ssbri_bitlen>0 is not supported yet!\n");;
          }

          // TODO: Improvements will be needed to cri_ssbri_bitlen>0
          temp_payload = get_rsrp_index(mac->csirs_measurements.rsrp_dBm);
          temp_payload = reverse_bits(temp_payload, n_bits);

          LOG_D(NR_MAC, "cri_ssbri_bitlen = %d\n", cri_ssbri_bitlen);
          LOG_D(NR_MAC, "rsrp_bitlen = %d\n", rsrp_bitlen);
          LOG_D(NR_MAC, "diff_rsrp_bitlen = %d\n", diff_rsrp_bitlen);
          LOG_D(NR_MAC, "n_bits = %d\n", n_bits);
          LOG_D(NR_MAC, "csi_part1_payload = 0x%lx\n", temp_payload);
          break;
        }
      }
    }
  }
  AssertFatal(n_bits <= 32, "Not supporting CSI report with more than 32 bits\n");
  nfapi_nr_ue_csi_payload_t csi = {.part1_payload = temp_payload, .p1_bits = n_bits, csi.p2_bits = 0};
  return csi;
}

nfapi_nr_ue_csi_payload_t nr_get_csi_payload(NR_UE_MAC_INST_t *mac,
                                             int csi_report_id,
                                             CSI_mapping_t mapping_type,
                                             const NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  AssertFatal(csi_MeasConfig->csi_ReportConfigToAddModList->list.count > 0,"No CSI Report configuration available\n");
  nfapi_nr_ue_csi_payload_t csi = {0};
  struct NR_CSI_ReportConfig *csi_reportconfig = csi_MeasConfig->csi_ReportConfigToAddModList->list.array[csi_report_id];
  NR_CSI_ResourceConfigId_t csi_ResourceConfigId = csi_reportconfig->resourcesForChannelMeasurement;
  if (csi_reportconfig->ext2 && csi_reportconfig->ext2->reportQuantity_r16) {
    switch (csi_reportconfig->ext2->reportQuantity_r16->present) {
      case NR_CSI_ReportConfig__ext2__reportQuantity_r16_PR_ssb_Index_SINR_r16:
        csi = get_ssb_sinr_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig);
        break;
      case NR_CSI_ReportConfig__ext2__reportQuantity_r16_PR_cri_SINR_r16:
        LOG_E(NR_MAC, "CSI Reporting of CSI-RS based SINR not yet available\n");
        break;
      default:
        AssertFatal(1 == 0, "Invalid CSI report quantity r16 type %d\n", csi_reportconfig->ext2->reportQuantity_r16->present);
    }
  } else {
    switch (csi_reportconfig->reportQuantity.present) {
      case NR_CSI_ReportConfig__reportQuantity_PR_none:
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP:
        csi = get_ssb_rsrp_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_PMI_CQI:
        csi = get_csirs_RI_PMI_CQI_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig, mapping_type);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP:
        csi = get_csirs_RSRP_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1:
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1_CQI:
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_CQI:
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_LI_PMI_CQI:
        LOG_E(NR_MAC, "Measurement report %d based on CSI-RS is not available\n", csi_reportconfig->reportQuantity.present);
        break;
      default:
        AssertFatal(1 == 0, "Invalid CSI report quantity type %d\n", csi_reportconfig->reportQuantity.present);
    }
  }
  return csi;
}

static void set_time_alignment(NR_UE_MAC_INST_t *mac, int ta, ta_type_t type, int frame, int slot)
{
  NR_UL_TIME_ALIGNMENT_t *ul_time_alignment = &mac->ul_time_alignment;
  ul_time_alignment->ta_command = ta;
  ul_time_alignment->ta_apply = type;
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, mac->current_UL_BWP->scs);
  const int n_slots_frame = mac->frame_structure.numb_slots_frame;
  ul_time_alignment->frame = (frame + (slot + ntn_ue_koffset) / n_slots_frame) % MAX_FRAME_NUMBER;
  ul_time_alignment->slot = (slot + ntn_ue_koffset) % n_slots_frame;
  // start or restart the timeAlignmentTimer associated with the indicated TAG
  nr_timer_start(&mac->time_alignment_timer);
}

static void handle_rar_reception(NR_UE_MAC_INST_t *mac, NR_MAC_RAR *rar, frame_t frame, int slot)
{
  RAR_grant_t rar_grant;
  RA_config_t *ra = &mac->ra;
#ifdef DEBUG_RAR
  // CSI
  unsigned char csi_req = (unsigned char)(rar->UL_GRANT_4 & 0x01);
#endif

  // TPC
  unsigned char tpc_command = (unsigned char)((rar->UL_GRANT_4 >> 1) & 0x07);
  ra->Msg3_TPC = (tpc_command << 1) - 6;

  // MCS
  rar_grant.mcs = (unsigned char)(rar->UL_GRANT_4 >> 4);
  // time alloc
  rar_grant.Msg3_t_alloc = (unsigned char)(rar->UL_GRANT_3 & 0x0f);
  // frequency alloc
  rar_grant.Msg3_f_alloc = (uint16_t)((rar->UL_GRANT_3 >> 4) | (rar->UL_GRANT_2 << 4) | ((rar->UL_GRANT_1 & 0x03) << 12));
  // frequency hopping
  rar_grant.freq_hopping = (unsigned char)(rar->UL_GRANT_1 >> 2);

  // Schedule Msg3
  const NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  const NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  const NR_BWP_PDCCH_t *pdcch_config = &mac->config_BWP_PDCCH[current_DL_BWP->bwp_id];
  const NR_SearchSpace_t *ra_SS = get_common_search_space(mac, pdcch_config->ra_SS_id);
  NR_tda_info_t tda_info = get_ul_tda_info(current_UL_BWP,
                                           *ra_SS->controlResourceSetId,
                                           ra_SS->searchSpaceType->present,
                                           TYPE_RA_RNTI_,
                                           rar_grant.Msg3_t_alloc);
  if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0) {
    LOG_E(MAC, "Cannot schedule Msg3. Something wrong in TDA information\n");
    // resume RAR response window timer if MSG2 decoding failed
    nr_timer_suspension(&mac->ra.response_window_timer);
    return;
  }
  frame_t frame_tx = 0;
  int slot_tx = 0;
  const int ntn_ue_koffset = GET_NTN_UE_K_OFFSET(&mac->phy_config.config_req.ntn_config, mac->current_UL_BWP->scs);
  int ret = nr_ue_pusch_scheduler(mac, 1, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset);

  // TA command
  const int ta = rar->TA2 + (rar->TA1 << 5);
  const bool ta_timer_active = nr_timer_is_active(&mac->time_alignment_timer);
  // if the timeAlignmentTimer associated with this TAG is not running
  if (!ta_timer_active)
    set_time_alignment(mac, ta, rar_ta, frame_tx, slot_tx);
  // else ignore the received Timing Advance Command
  LOG_D(NR_MAC,
        "[UE %d] RAR %d.%d RAPID %u absolute TA %d Msg3 %d.%d k2 %ld NTN K-offset %d "
        "Msg3-scheduled %d timeAlignmentTimer %s TA-action %s\n",
        mac->ue_id,
        frame,
        slot,
        ra->ra_PreambleIndex,
        ta,
        frame_tx,
        slot_tx,
        tda_info.k2,
        ntn_ue_koffset,
        ret != -1,
        ta_timer_active ? "active" : "inactive",
        ta_timer_active ? "ignored" : "applied");

#ifdef DEBUG_RAR
  LOG_I(NR_MAC, "rarh->E = 0x%x\n", rarh->E);
  LOG_I(NR_MAC, "rarh->T = 0x%x\n", rarh->T);
  LOG_I(NR_MAC, "rarh->RAPID = 0x%x (%i)\n", rarh->RAPID, rarh->RAPID);

  LOG_I(NR_MAC, "rar->R = 0x%x\n", rar->R);
  LOG_I(NR_MAC, "rar->TA1 = 0x%x\n", rar->TA1);

  LOG_I(NR_MAC, "rar->TA2 = 0x%x\n", rar->TA2);
  LOG_I(NR_MAC, "rar->UL_GRANT_1 = 0x%x\n", rar->UL_GRANT_1);

  LOG_I(NR_MAC, "rar->UL_GRANT_2 = 0x%x\n", rar->UL_GRANT_2);
  LOG_I(NR_MAC, "rar->UL_GRANT_3 = 0x%x\n", rar->UL_GRANT_3);
  LOG_I(NR_MAC, "rar->UL_GRANT_4 = 0x%x\n", rar->UL_GRANT_4);

  LOG_I(NR_MAC, "rar->TCRNTI_1 = 0x%x\n", rar->TCRNTI_1);
  LOG_I(NR_MAC, "rar->TCRNTI_2 = 0x%x\n", rar->TCRNTI_2);

  LOG_I(NR_MAC,
        "[%d.%d]: [UE %d] Received RAR with t_alloc %d f_alloc %d ta_command %d mcs %d freq_hopping %d tpc_command %d\n",
        frame,
        slot,
        mac->ue_id,
        rar_grant.Msg3_t_alloc,
        rar_grant.Msg3_f_alloc,
        ta_command,
        rar_grant.mcs,
        rar_grant.freq_hopping,
        tpc_command);
#endif

  if (ret != -1) {
    uint16_t rnti = mac->crnti;
    // Upon successful reception, set the T-CRNTI to the RAR value
    // if the RA preamble is selected among the contention-based RA Preambles
    if (!ra->cfra) {
      ra->t_crnti = rar->TCRNTI_2 + (rar->TCRNTI_1 << 8);
      rnti = ra->t_crnti;
      if (!mac->msg3_C_RNTI)
        nr_mac_rrc_msg3_ind(mac->ue_id, rnti, false);
    }
    fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
    if (!pdu)
      return;
    // Config Msg3 PDU
    int ret = nr_config_pusch_pdu(mac,
                                  &tda_info,
                                  &pdu->pusch_config_pdu,
                                  NULL,
                                  NULL,
                                  &rar_grant,
                                  rnti,
                                  NR_SearchSpace__searchSpaceType_PR_common,
                                  NR_DCI_NONE);
    if (ret != 0)
      remove_ul_config_last_item(pdu);
    release_ul_config(pdu, false);
  }
}

/////////////////////////////////////
//    Random Access Response PDU   //
//         TS 38.213 ch 8.2        //
//        TS 38.321 ch 6.2.3       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//| E | T |       R A P I D       |//
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |//
//| R |           T A             |//
//|       T A         |  UL grant |//
//|            UL grant           |//
//|            UL grant           |//
//|            UL grant           |//
//|         T C - R N T I         |//
//|         T C - R N T I         |//
/////////////////////////////////////
//       UL grant  (27 bits)       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//|-------------------|FHF|F_alloc|//
//|        Freq allocation        |//
//|    F_alloc    |Time allocation|//
//|      MCS      |     TPC   |CSI|//
/////////////////////////////////////
// TbD WIP Msg3 development ongoing
// - apply UL grant freq alloc & time alloc as per 8.2 TS 38.213
// - apply tpc command
// WIP fix:
// - time domain indication hardcoded to 0 for k2 offset
// - extend TS 38.213 ch 8.3 Msg3 PUSCH
// - b buffer
// - ulsch power offset
// - optimize: mu_pusch, j and table_6_1_2_1_1_2_time_dom_res_alloc_A are already defined in nr_ue_procedures
static void nr_ue_process_rar(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  frame_t frame = dl_info->frame;
  int slot = dl_info->slot;

  if(dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.ack_nack == 0) {
    LOG_W(NR_MAC,"[UE %d][RAPROC][%d.%d] CRC check failed on RAR (NAK)\n", mac->ue_id, frame, slot);
    return;
  }

  RA_config_t *ra = &mac->ra;
  ra->t_crnti = 0;
  uint8_t n_subPDUs  = 0;  // number of RAR payloads
  uint8_t n_subheaders = 0;  // number of MAC RAR subheaders
  uint8_t *dlsch_buffer = dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.pdu;
  NR_RA_HEADER_RAPID *rarh = (NR_RA_HEADER_RAPID *) dlsch_buffer; // RAR subheader pointer
  NR_MAC_RAR *rar = (NR_MAC_RAR *) (dlsch_buffer + 1);   // RAR subPDU pointer
  uint8_t preamble_index = ra->ra_PreambleIndex;
  uint16_t rnti = mac->ra.ra_rnti;

  T(T_NRUE_MAC_DL_RAR_PDU_WITH_DATA, T_INT(rnti), T_INT(frame), T_INT(slot),
    T_BUFFER(dlsch_buffer, dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.pdu_length));

  ra->RA_backoff_limit = 0;
  LOG_D(NR_MAC, "[%d.%d]: [UE %d][RAPROC] MAC received RAR (current preamble %d)\n", frame, slot, mac->ue_id, preamble_index);

  while (1) {
    n_subheaders++;
    if (rarh->T == 1) {
      n_subPDUs++;
      LOG_I(NR_MAC, "[UE %d][RAPROC][RA-RNTI %04x] Got RAPID RAR subPDU\n", mac->ue_id, rnti);
    } else {
      int bi_ms = table_7_2_1[((NR_RA_HEADER_BI *)rarh)->BI] * ra->scaling_factor_bi;
      int slots_per_ms = mac->frame_structure.numb_slots_frame / 10;
      ra->RA_backoff_limit = bi_ms * slots_per_ms;
      LOG_I(NR_MAC, "[UE %d][RAPROC][RA-RNTI %04x] Got BI RAR subPDU %d ms\n", mac->ue_id, rnti, bi_ms);
      if (((NR_RA_HEADER_BI *)rarh)->E == 1) {
        rarh += sizeof(NR_RA_HEADER_BI);
        continue;
      } else {
        break;
      }
    }
    if (rarh->RAPID == preamble_index) {
      // The MAC entity may stop ra-ResponseWindow (and hence monitoring for Random Access Response(s)) after
      // successful reception of a Random Access Response containing Random Access Preamble identifiers
      // that matches the transmitted PREAMBLE_INDEX.
      nr_timer_stop(&ra->response_window_timer);
      LOG_A(NR_MAC, "[UE %d][RAPROC][%d.%d] Found RAR with the intended RAPID %d\n", mac->ue_id, frame, slot, rarh->RAPID);
      rar = (NR_MAC_RAR *) (dlsch_buffer + n_subheaders + (n_subPDUs - 1) * sizeof(NR_MAC_RAR));
      handle_rar_reception(mac, rar, frame, slot);
      if (ra->cfra)
        nr_ra_succeeded(mac, frame, slot);
      break;
    }
    if (rarh->E == 0) {
      LOG_W(NR_MAC,"[UE %d][RAPROC][%d.%d] Received RAR preamble (%d) doesn't match the intended RAPID (%d)\n",
            mac->ue_id,
            frame,
            slot,
            rarh->RAPID,
            preamble_index);
      // resume RAR response window timer if MSG2 decoding failed
      nr_timer_suspension(&mac->ra.response_window_timer);
      break;
    } else {
      rarh += sizeof(NR_MAC_RAR) + 1;
    }
  }

#ifdef DEBUG_RAR
  LOG_D(NR_MAC,
        "[DEBUG_RAR] (%d,%d) number of RAR subheader %d; number of RAR pyloads %d\n",
        frame,
        slot,
        n_subheaders,
        n_subPDUs);
  LOG_D(NR_MAC,
        "[DEBUG_RAR] Received RAR (%02x|%02x.%02x.%02x.%02x.%02x.%02x) for preamble %d/%d\n",
        *(uint8_t *) rarh,
        rar[0],
        rar[1],
        rar[2],
        rar[3],
        rar[4],
        rar[5],
        rarh->RAPID,
        preamble_index);
#endif
  return;
}

// #define EXTRACT_DCI_ITEM(val,size) val= readBits(dci_pdu, &pos, size);
#define EXTRACT_DCI_ITEM(val, size)    \
  val = readBits(dci_pdu, &pos, size); \
  LOG_D(NR_MAC_DCI, "     " #val ": %d\n", val);

// Fixme: Intel Endianess only procedure
static inline int readBits(const uint8_t *dci, int *start, int length)
{
  uint32_t mask = (1U << length) - 1;
  uint64_t *tmp = (uint64_t *)dci;
  *start -= length;
  return *tmp >> *start & mask;
}

static void extract_10_ra_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 RA rnti\n");

  // Freq domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, ceil_log2_u32((N_RB * (N_RB + 1)) >> 1));
  // Time domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // TB scaling
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tb_scaling, 2);
}

static uint8_t extract_10_si_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 SI rnti\n");

  // Freq domain assignment 0-16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, ceil_log2_u32((N_RB * (N_RB + 1)) >> 1));
  // Time domain assignment 4 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS 5bit  //bit over 32
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // Redundancy version  2 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // System information indicator 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->system_info_indicator, 1);
  return dci_pdu_rel15->system_info_indicator;
}

static void extract_10_p_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 P-RNTI\n");
  // Short Messages Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->short_messages_indicator, 2);
  // Short Messages
  EXTRACT_DCI_ITEM(dci_pdu_rel15->short_messages, 8);
  // Freq domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, ceil_log2_u32((N_RB * (N_RB + 1)) >> 1));
  // Time domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // TB scaling
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tb_scaling, 2);
}

static bool extract_10_c_rnti(NR_UE_MAC_INST_t *mac,
                              dci_pdu_rel15_t *dci_pdu_rel15,
                              const uint8_t *dci_pdu,
                              int pos,
                              const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 C rnti\n");

  // Freq domain assignment (275rb >> fsize = 16)
  int fsize = ceil_log2_u32((N_RB * (N_RB + 1)) >> 1);
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, fsize);
  bool pdcch_order = true;
  for (int i = 0; i < fsize; i++) {
    if (!((dci_pdu_rel15->frequency_domain_assignment.val >> i) & 1)) {
      pdcch_order = false;
      break;
    }
  }
  if (pdcch_order) { // Frequency domain resource assignment field are all 1  38.212 section 7.3.1.2.1
    // ra_preamble_index 6 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ra_preamble_index, 6);
    // UL/SUL indicator  1 bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ul_sul_indicator.val, 1);
    // SS/PBCH index  6 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ss_pbch_index, 6);
    //  prach_mask_index  4 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->prach_mask_index, 4);
    trigger_MAC_UE_RA(mac, dci_pdu_rel15);
    return true;
  } // end if

  // Time domain assignment 4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping  1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS 5bit  //bit over 32, so dci_pdu ++
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4/5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, dci_pdu_rel15->harq_pid.nbits);
  // Downlink assignment index  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, 2);
  // TPC command for scheduled PUCCH  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // PUCCH resource indicator  3bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
  // PDSCH-to-HARQ_feedback timing indicator 3bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val, 3);
  return false;
}

static void extract_00_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 0_0 C rnti\n");

  // Frequency domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain assignment 4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // Frequency hopping flag  E1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, 4);
  // TPC command for scheduled PUSCH  E2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // UL/SUL indicator  E1 bit
  /* commented for now (RK): need to get this from BWP descriptor
     if (cfg->pucch_config.pucch_GroupHopping.value)
       dci_pdu->= ((uint64_t)readBits(dci_pdu,>>(dci_size-pos)ul_sul_indicator&1)<<(dci_size-pos++);
  */
}

static void extract_10_tc_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 TC rnti\n");

  // Freq domain assignment 0-16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, ceil_log2_u32((N_RB * (N_RB + 1)) >> 1));
  // Time domain assignment - 4 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping - 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator - 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number - 4 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, 4);
  // Downlink assignment index - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, 2);
  // TPC command for scheduled PUCCH - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // PUCCH resource indicator - 3 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
  // PDSCH-to-HARQ_feedback timing indicator - 3 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val, 3);
}

static void extract_00_tc_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 0_0 TC rnti\n");

  // Frequency domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain assignment 4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // Frequency hopping flag  E1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, 4);
  // TPC command for scheduled PUSCH  E2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
}

static void extract_11_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_1 C rnti\n");

  // Carrier indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->carrier_indicator.val, dci_pdu_rel15->carrier_indicator.nbits);
  // BWP Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->bwp_indicator.val, dci_pdu_rel15->bwp_indicator.nbits);
  // Frequency domain resource assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain resource assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, dci_pdu_rel15->time_domain_assignment.nbits);
  // VRB-to-PRB mapping
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, dci_pdu_rel15->vrb_to_prb_mapping.nbits);
  // PRB bundling size indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->prb_bundling_size_indicator.val, dci_pdu_rel15->prb_bundling_size_indicator.nbits);
  // Rate matching indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rate_matching_indicator.val, dci_pdu_rel15->rate_matching_indicator.nbits);
  // ZP CSI-RS trigger
  EXTRACT_DCI_ITEM(dci_pdu_rel15->zp_csi_rs_trigger.val, dci_pdu_rel15->zp_csi_rs_trigger.nbits);
  // TB1
  //  MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  //TB2
  // MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs2.val, dci_pdu_rel15->mcs2.nbits);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi2.val, dci_pdu_rel15->ndi2.nbits);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv2.val, dci_pdu_rel15->rv2.nbits);
  // HARQ process number  4/5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, dci_pdu_rel15->harq_pid.nbits);
  // Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, dci_pdu_rel15->dai[0].nbits);
  // TPC command for scheduled PUCCH  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // PUCCH resource indicator  3bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
  // PDSCH-to-HARQ_feedback timing indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val,
                   dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.nbits);
  // Antenna ports
  EXTRACT_DCI_ITEM(dci_pdu_rel15->antenna_ports.val, dci_pdu_rel15->antenna_ports.nbits);
  // TCI
  EXTRACT_DCI_ITEM(dci_pdu_rel15->transmission_configuration_indication.val,
                   dci_pdu_rel15->transmission_configuration_indication.nbits);
  // SRS request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_request.val, dci_pdu_rel15->srs_request.nbits);
  // CBG transmission information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgti.val, dci_pdu_rel15->cbgti.nbits);
  // CBG flushing out information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgfi.val, dci_pdu_rel15->cbgfi.nbits);
  // DMRS sequence init
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dmrs_sequence_initialization.val, 1);
}

static void extract_01_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 0_1 C rnti\n");

  // Carrier indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->carrier_indicator.val, dci_pdu_rel15->carrier_indicator.nbits);
  // UL/SUL Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ul_sul_indicator.val, dci_pdu_rel15->ul_sul_indicator.nbits);
  // BWP Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->bwp_indicator.val, dci_pdu_rel15->bwp_indicator.nbits);
  // Freq domain assignment  max 16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, ceil_log2_u32((N_RB * (N_RB + 1)) >> 1));
  // Time domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, dci_pdu_rel15->time_domain_assignment.nbits);
  // Not supported yet - skip for now
  // Frequency hopping flag – 1 bit
  // EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4/5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid.val, dci_pdu_rel15->harq_pid.nbits);
  // 1st Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, dci_pdu_rel15->dai[0].nbits);
  // 2nd Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[1].val, dci_pdu_rel15->dai[1].nbits);
  // TPC command for scheduled PUSCH – 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // SRS resource indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_resource_indicator.val, dci_pdu_rel15->srs_resource_indicator.nbits);
  // Precoding info and n. of layers
  EXTRACT_DCI_ITEM(dci_pdu_rel15->precoding_information.val, dci_pdu_rel15->precoding_information.nbits);
  // Antenna ports
  EXTRACT_DCI_ITEM(dci_pdu_rel15->antenna_ports.val, dci_pdu_rel15->antenna_ports.nbits);
  // SRS request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_request.val, dci_pdu_rel15->srs_request.nbits);
  // CSI request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->csi_request.val, dci_pdu_rel15->csi_request.nbits);
  // CBG transmission information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgti.val, dci_pdu_rel15->cbgti.nbits);
  // PTRS DMRS association
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ptrs_dmrs_association.val, dci_pdu_rel15->ptrs_dmrs_association.nbits);
  // Beta offset indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->beta_offset_indicator.val, dci_pdu_rel15->beta_offset_indicator.nbits);
  // DMRS sequence initialization
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dmrs_sequence_initialization.val, dci_pdu_rel15->dmrs_sequence_initialization.nbits);
  // UL-SCH indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ulsch_indicator, 1);
  // UL/SUL indicator – 1 bit
  /* commented for now (RK): need to get this from BWP descriptor
     if (cfg->pucch_config.pucch_GroupHopping.value)
       dci_pdu->= ((uint64_t)*dci_pdu>>(dci_size-pos)ul_sul_indicator&1)<<(dci_size-pos++);
  */
}

static int get_nrb_for_dci(NR_UE_MAC_INST_t *mac, nr_dci_format_t dci_format, int ss_type)
{
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  int N_RB;
  if(current_DL_BWP)
    N_RB = get_rb_bwp_dci(dci_format,
                          ss_type,
                          mac->type0_PDCCH_CSS_config.num_rbs,
                          current_UL_BWP->BWPSize,
                          current_DL_BWP->BWPSize,
                          mac->sc_info.initial_dl_BWPSize,
                          mac->sc_info.initial_dl_BWPSize);
  else
    N_RB = mac->type0_PDCCH_CSS_config.num_rbs;

  if (N_RB == 0)
    LOG_E(NR_MAC_DCI, "DCI configuration error! N_RB = 0\n");

  return N_RB;
}

static nr_dci_format_t nr_extract_dci_00_10(NR_UE_MAC_INST_t *mac,
                                            int pos,
                                            const int rnti_type,
                                            const uint8_t *dci_pdu,
                                            const int slot,
                                            const int ss_type)
{
  nr_dci_format_t format = NR_DCI_NONE;
  dci_pdu_rel15_t *dci_pdu_rel15 = NULL;
  int format_indicator = -1;
  int n_RB = 0;

  switch (rnti_type) {
    case TYPE_RA_RNTI_ :
      format = NR_DL_DCI_FORMAT_1_0;
      dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
      n_RB = get_nrb_for_dci(mac, format, ss_type);
      if (n_RB == 0)
        return NR_DCI_NONE;
      extract_10_ra_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      break;
    case TYPE_P_RNTI_ :
      format = NR_DL_DCI_FORMAT_1_0;
      dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
      n_RB = get_nrb_for_dci(mac, format, ss_type);
      if (n_RB == 0) {
        LOG_W(NR_MAC_DCI, "[%d] P-RNTI DCI extraction aborted: n_RB=0 (ss_type=%d)\n", slot, ss_type);
        return NR_DCI_NONE;
      }
      extract_10_p_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      break;
    case TYPE_SI_RNTI_ :
      format = NR_DL_DCI_FORMAT_1_0;
      dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
      n_RB = get_nrb_for_dci(mac, format, ss_type);
      if (n_RB == 0)
        return NR_DCI_NONE;
      uint8_t sys_info = extract_10_si_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      // sys info = 0 for SIB1 and 1 for other SIB
      if (mac->get_sib1 == 0 && sys_info == 0)
        return NR_DCI_NONE;
      // received DCI for other SI while still waiting to receive SIB1
      if (mac->get_sib1 != 0 && sys_info == 1)
        return NR_DCI_NONE;
      break;
    case TYPE_C_RNTI_ :
      // Identifier for DCI formats
      EXTRACT_DCI_ITEM(format_indicator, 1);
      if (format_indicator == 1) {
        format = NR_DL_DCI_FORMAT_1_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        int n_RB = get_nrb_for_dci(mac, format, ss_type);
        if (n_RB == 0)
          return NR_DCI_NONE;
        bool pdcch_order = extract_10_c_rnti(mac, dci_pdu_rel15, dci_pdu, pos, n_RB);
        if (pdcch_order)
          return NR_DCI_NONE;
      }
      else {
        format = NR_UL_DCI_FORMAT_0_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        extract_00_c_rnti(dci_pdu_rel15, dci_pdu, pos);
      }
      dci_pdu_rel15->format_indicator = format_indicator;
      break;
    case TYPE_TC_RNTI_ :
    case TYPE_MSGB_RNTI_:
      // Identifier for DCI formats
      EXTRACT_DCI_ITEM(format_indicator, 1);
      if (format_indicator == 1) {
        format = NR_DL_DCI_FORMAT_1_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        n_RB = get_nrb_for_dci(mac, format, ss_type);
        if (n_RB == 0)
          return NR_DCI_NONE;
        extract_10_tc_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      }
      else {
        format = NR_UL_DCI_FORMAT_0_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        extract_00_tc_rnti(dci_pdu_rel15, dci_pdu, pos);
      }
      dci_pdu_rel15->format_indicator = format_indicator;
      break;
    default :
      AssertFatal(false, "Invalid RNTI type\n");
  }
  return format;
}

static nr_dci_format_t nr_extract_dci_info(NR_UE_MAC_INST_t *mac,
                                           const nfapi_nr_dci_formats_e dci_format,
                                           const uint8_t dci_size,
                                           const uint16_t rnti,
                                           const int ss_type,
                                           const uint8_t *dci_pdu,
                                           const int slot)
{
  LOG_D(NR_MAC_DCI,"nr_extract_dci_info : dci_pdu %lx, size %d, format %d\n", *(uint64_t *)dci_pdu, dci_size, dci_format);
  int rnti_type = get_rnti_type(mac, rnti);
  int pos = dci_size;

  nr_dci_format_t format = NR_DCI_NONE;
  switch(dci_format) {
    case  NFAPI_NR_FORMAT_0_0_AND_1_0 :
      format = nr_extract_dci_00_10(mac, pos, rnti_type, dci_pdu, slot, ss_type);
      break;
    case  NFAPI_NR_FORMAT_0_1_AND_1_1 :
      if (rnti_type == TYPE_C_RNTI_) {
        // Identifier for DCI formats
        int format_indicator = 0;
        EXTRACT_DCI_ITEM(format_indicator, 1);
        if (format_indicator == 1) {
          format = NR_DL_DCI_FORMAT_1_1;
          dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
          def_dci_pdu_rel15->format_indicator = format_indicator;
          extract_11_c_rnti(def_dci_pdu_rel15, dci_pdu, pos);
        }
        else {
          format = NR_UL_DCI_FORMAT_0_1;
          dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
          def_dci_pdu_rel15->format_indicator = format_indicator;
          int n_RB = get_nrb_for_dci(mac, format, ss_type);
          if (n_RB == 0)
            return NR_DCI_NONE;
          extract_01_c_rnti(def_dci_pdu_rel15, dci_pdu, pos, n_RB);
        }
      }
      else {
        LOG_E(NR_MAC_DCI, "RNTI type not supported for formats 01 or 11\n");
        return NR_DCI_NONE;
      }
      break;
    default :
      LOG_E(NR_MAC_DCI, "DCI format not supported\n");
  }
  return format;
}

nr_dci_format_t nr_ue_process_dci_indication_pdu(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_dci_indication_pdu_t *dci)
{
  const nr_rnti_type_t rnti_type = get_rnti_type(mac, dci->rnti);
  LOG_D(NR_MAC,
        "Received dci indication (rnti %x, rnti_type %d, dci format %d, n_CCE %d, payloadSize %d, payload %llx)\n",
        dci->rnti,
        rnti_type,
        dci->dci_format,
        dci->n_CCE,
        dci->payloadSize,
        *(unsigned long long *)dci->payloadBits);
  const nr_dci_format_t format =
      nr_extract_dci_info(mac, dci->dci_format, dci->payloadSize, dci->rnti, dci->ss_type, dci->payloadBits, slot);
  if (format == NR_DCI_NONE)
    return NR_DCI_NONE;
  int ret = nr_ue_process_dci(mac, frame, slot, mac->def_dci_pdu_rel15[slot] + format, dci, format);
  if (ret < 0) {
    mac->stats.bad_dci++;
    return NR_DCI_NONE;
  }
  return format;
}

static bool check_ra_contention_resolution(const uint8_t *pdu, const uint8_t *cont_res)
{
  if (IS_SOFTMODEM_IQPLAYER) // Control is bypassed when replaying IQs (BMC)
    return true;
  for (int i = 0; i < 6; i++) {
    if (pdu[i] != cont_res[i]) {
      return false;
    }
  }
  return true;
}

static int nr_ue_validate_successrar(uint8_t *pduP, int32_t pdu_len, NR_UE_MAC_INST_t *mac, frame_t frameP, int slot)
{
  // TS 38.321 - Figure 6.1.5a-1: BI MAC subheader
  // TS 38.321 - Figure 6.1.5a-3: SuccessRAR MAC subheader
  int n = 0;
  uint8_t E = 1;
  uint8_t cont_res_id[6];
  RA_config_t *ra = &mac->ra;
  ra->RA_backoff_limit = 0;

  while (n < pdu_len && E) {
    E = (pduP[n] >> 7) & 0x1;
    uint8_t SUCESS_RAR_header_T1 = (pduP[n] >> 6) & 0x1;
    if (SUCESS_RAR_header_T1 == 0) { // T2 exist
      int SUCESS_RAR_header_T2 = (pduP[n] >> 5) & 0x1;
      if (SUCESS_RAR_header_T2 == 0) { // BI
        int bi_ms = table_7_2_1[(pduP[n] & 0x0F)] * ra->scaling_factor_bi;
        int slots_per_ms = mac->frame_structure.numb_slots_frame / 10;
        ra->RA_backoff_limit = bi_ms * slots_per_ms;
        n++;
      } else { // S
        n++;
        // TS 38.321 - Figure 6.2.3a-2: successRAR
        for (int i = 0; i < 6; ++i)
          cont_res_id[i] = pduP[n + i];
        n += 6;
        // Oct 7
        ra->MsgB_R = 0;
        ra->MsgB_CH_ACESS_CPEXT = (pduP[n] >> 5) & 0x3;
        ra->MsgB_TPC = (pduP[n] >> 3) & 3;
        ra->MsgB_HARQ_FTI = (int8_t)pduP[n] & 0x7;
        // Oct 8
        n++;
        ra->PUCCH_RI = ((int8_t)pduP[n] >> 4) & 0x0F;
        // Oct 8 and Oct 9
        ra->timing_advance_command = ((uint16_t)(pduP[n] & 0xf) << 8) | pduP[n + 1];
        n += 2;
        // Oct 10 and Oct 11
        ra->t_crnti = ((uint16_t)pduP[n] << 8) | pduP[n + 1];
        n += 2;

        LOG_D(NR_MAC,
              "successRAR: Contention Resolution ID 0x%02x%02x%02x%02x%02x%02x R 0x%01x CH_ACESS_CPEXT 0x%02x TPC 0x%02x "
              "HARQ_FTI 0x%03x PUCCH_RI 0x%04x TA 0x%012x CRNTI 0x%04x\n",
              cont_res_id[0],
              cont_res_id[1],
              cont_res_id[2],
              cont_res_id[3],
              cont_res_id[4],
              cont_res_id[5],
              ra->MsgB_R,
              ra->MsgB_CH_ACESS_CPEXT,
              ra->MsgB_TPC,
              ra->MsgB_HARQ_FTI,
              ra->PUCCH_RI,
              ra->timing_advance_command,
              ra->t_crnti);

        bool ra_success = check_ra_contention_resolution(cont_res_id, ra->cont_res_id);

        if (ra->RA_active && ra_success) {
          nr_timer_stop(&ra->response_window_timer);
          nr_ra_succeeded(mac, frameP, slot);
        } else if (!ra_success) {
          nr_ra_backoff_setting(ra);
        }
      }
    } else { // RAPID
      int RAPID = pduP[n] & 0x3F;
      n++;
      LOG_D(MAC, "RAPID %d\n", RAPID);
      AssertFatal(false, "FallbackRAR not implemented yet!\n");
    }
  }
  const int ta = ((NR_MAC_CE_TA *)pduP)[1].TA_COMMAND;

  set_time_alignment(mac, ta, adjustment_ta, frameP, slot);
  return n;
}

#define MAX_NUM_DATA_IND 1024

///////////////////////////////////
// brief:     nr_ue_process_mac_pdu
// function:  parsing DL PDU header
///////////////////////////////////
//  Header for DLSCH:
//  Except:
//   - DL-SCH: fixed-size MAC CE(known by LCID)
//   - DL-SCH: padding
//
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |       L       |
////////////////////////////////
//  Header for DLSCH:
//   - DLSCH: fixed-size MAC CE(known by LCID)
//   - DLSCH: padding, for single/multiple 1-oct padding CE(s)
//
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|R|   LCID    |
//  LCID: The Logical Channel ID field identifies the logical channel instance of the corresponding MAC SDU or the type of the
//  corresponding MAC CE or padding as described
//         in Tables 6.2.1-1 and 6.2.1-2 for the DL-SCH and UL-SCH respectively. There is one LCID field per MAC subheader. The LCID
//         field size is 6 bits;
//  L:    The Length field indicates the length of the corresponding MAC SDU or variable-sized MAC CE in bytes. There is one L field
//  per MAC subheader except for subheaders
//         corresponding to fixed-sized MAC CEs and padding. The size of the L field is indicated by the F field;
//  F:    lenght of L is 0:8 or 1:16 bits wide
//  R:    Reserved bit, set to zero.
////////////////////////////////
static void nr_ue_process_mac_pdu(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  frame_t frameP = dl_info->frame;
  int slot = dl_info->slot;
  fapi_nr_pdsch_pdu_t *pdsch_pdu = &dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu;
  uint8_t *pduP = pdsch_pdu->pdu;
  int32_t pdu_len = (int32_t)pdsch_pdu->pdu_length;
  uint8_t CC_id = dl_info->cc_id;
  uint8_t done = 0;
  RA_config_t *ra = &mac->ra;

  if (!pduP) {
    return;
  }

  T(T_NRUE_MAC_DL_PDU_WITH_DATA, T_INT(mac->crnti), T_INT(frameP), T_INT(slot), T_INT(pdsch_pdu->harq_pid), T_BUFFER(pduP, pdu_len));

  LOG_D(MAC,
        "[%d.%d]: processing PDU %d (with length %d) of %d total number of PDUs...\n",
        frameP,
        slot,
        pdu_id,
        pdu_len,
        dl_info->rx_ind->number_pdus);

  if (ra->ra_type == RA_2_STEP && ra->ra_state == nrRA_WAIT_MSGB) {
    int n = nr_ue_validate_successrar(pduP, pdu_len, mac, frameP, slot);
    pduP += n;
    pdu_len -= n;
  }

  nr_rlc_data_ind_t data_ind[MAX_NUM_DATA_IND] = {0};
  int num_data_ind = 0;

  while (!done && pdu_len > 0){
    uint16_t mac_len = 0x0000;
    uint16_t mac_subheader_len = 0x0001; //  default to fixed-length subheader = 1-oct
    uint8_t rx_lcid = ((NR_MAC_SUBHEADER_FIXED *)pduP)->LCID;

    LOG_D(MAC, "[UE] LCID %d, PDU length %d\n", rx_lcid, pdu_len);
    bool ret;
    switch (rx_lcid) {
      //  MAC CE
      case DL_SCH_LCID_CCCH:
        //  MSG4 RRC Setup 38.331
        //  variable length
        ret=get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len);
        if (!ret) {
          // A subheader with LCID 0 whose length exceeds the PDU: drop the PDU,
          // as for the other LCIDs, instead of aborting.
          LOG_E(MAC, "CCCH: invalid mac_len %d for PDU len %d, dropping the PDU\n", mac_len, pdu_len);
          done = 1;
          break;
        }

        // Check if it is a valid CCCH message, we get all 00's messages very often
        int i = 0;
        for(i=0; i<(mac_subheader_len+mac_len); i++) {
          if(pduP[i] != 0) {
            break;
          }
        }
        if (i == (mac_subheader_len+mac_len)) {
          LOG_E(NR_MAC, "Invalid CCCH message!, pdu_len: %d\n", pdu_len);
          done = 1;
          break;
        }

        if (mac_len > 0) {
          LOG_DDUMP(NR_MAC, (void *)pduP, mac_subheader_len + mac_len, LOG_DUMP_CHAR, "DL_SCH_LCID_CCCH (e.g. RRCSetup) payload: ");
          nr_rlc_data_ind_t ind = {.ch = rx_lcid, .buf = pduP + mac_subheader_len, .len = mac_len};
          data_ind[num_data_ind++] = ind;
          DevAssert(num_data_ind < MAX_NUM_DATA_IND);
        }
        break;
      case DL_SCH_LCID_TCI_STATE_ACT_UE_SPEC_PDSCH:
      case DL_SCH_LCID_APERIODIC_CSI_TRI_STATE_SUBSEL:
      case DL_SCH_LCID_SP_CSI_RS_CSI_IM_RES_SET_ACT:
      case DL_SCH_LCID_SP_SRS_ACTIVATION:

        //  38.321 Ch6.1.3.14
        //  varialbe length
        get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len);
        break;

      case DL_SCH_LCID_RECOMMENDED_BITRATE:
        //  38.321 Ch6.1.3.20
        mac_len = 2;
        break;
      case DL_SCH_LCID_SP_ZP_CSI_RS_RES_SET_ACT:
        //  38.321 Ch6.1.3.19
        mac_len = 2;
        break;
      case DL_SCH_LCID_PUCCH_SPATIAL_RELATION_ACT:
        //  38.321 Ch6.1.3.18
        mac_len = 3;
        break;
      case DL_SCH_LCID_SP_CSI_REP_PUCCH_ACT:
        //  38.321 Ch6.1.3.16
        mac_len = 2;
        break;
      case DL_SCH_LCID_TCI_STATE_IND_UE_SPEC_PDCCH:
        //  38.321 Ch6.1.3.15
        mac_len = 2;
        break;
      case DL_SCH_LCID_DUPLICATION_ACT:
        //  38.321 Ch6.1.3.11
        mac_len = 1;
        break;
      case DL_SCH_LCID_SCell_ACT_4_OCT:
        //  38.321 Ch6.1.3.10
        mac_len = 4;
        break;
      case DL_SCH_LCID_SCell_ACT_1_OCT:
        //  38.321 Ch6.1.3.10
        mac_len = 1;
        break;
      case DL_SCH_LCID_L_DRX:
        //  38.321 Ch6.1.3.6
        //  fixed length but not yet specify.
        mac_len = 0;
        break;
      case DL_SCH_LCID_DRX:
        //  38.321 Ch6.1.3.5
        //  fixed length but not yet specify.
        mac_len = 0;
        break;
      case DL_SCH_LCID_TA_COMMAND:
        //  38.321 Ch6.1.3.4
        mac_len = 1;

        const int ta = ((NR_MAC_CE_TA *)pduP)[1].TA_COMMAND;
        const int tag = ((NR_MAC_CE_TA *)pduP)[1].TAGID;

        if (tag != mac->tag_Id) {
          LOG_E(NR_MAC, "MAC CE TAG %d does not correspond to the one configured at MAC %ld\n", tag, mac->tag_Id);
          done = 1;
          break;
        }

        set_time_alignment(mac, ta, adjustment_ta, frameP, slot);

        if (ta == 31)
          LOG_D(NR_MAC, "[%d.%d] Received TA_COMMAND %u TAGID %u CC_id %d \n", frameP, slot, ta, tag, CC_id);
        else
          LOG_I(NR_MAC, "[%d.%d] Received TA_COMMAND %u TAGID %u CC_id %d \n", frameP, slot, ta, tag, CC_id);
        break;
      case DL_SCH_LCID_CON_RES_ID:
        //  Clause 5.1.5 and 6.1.3.3 of 3GPP TS 38.321 version 16.2.1 Release 16
        // MAC Header: 1 byte (R/R/LCID)
        // MAC SDU: 6 bytes (UE Contention Resolution Identity)
        mac_len = 6;

        if (ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION) {
          LOG_D(NR_MAC,
                "[UE %d]Frame %d Contention resolution identity: 0x%02x%02x%02x%02x%02x%02x Terminating RA procedure\n",
                mac->ue_id,
                frameP,
                pduP[1],
                pduP[2],
                pduP[3],
                pduP[4],
                pduP[5],
                pduP[6]);

          nr_timer_stop(&ra->contention_resolution_timer);
          bool ra_success = check_ra_contention_resolution(&pduP[1], ra->cont_res_id);

          if (ra->RA_active && ra_success) {
            nr_ra_succeeded(mac, frameP, slot);
          } else if (!ra_success) {
            // consider this Contention Resolution not successful and discard the successfully decoded MAC PDU
            nr_ra_contention_resolution_failed(mac);
            return;
          }
        }
        break;
      case DL_SCH_LCID_PADDING:
        done = 1;
        //  end of MAC PDU, can ignore the rest.
        break;
        //  MAC SDU
      // From values 1 to 32 it equals to the identity of the logical channel
      case 1 ... 32:
        if (!get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len)) {
          LOG_E(MAC, "get_mac_len(): invalid pdu_len %d (mac_len %d mac_subheader_len %d)\n", pdu_len, mac_len, mac_subheader_len);
          done = 1;
          break;
        }
        // discard the received subPDU if RB is suspended
        if (is_lcid_suspended(mac, rx_lcid)) {
          LOG_W(NR_MAC, "Received PDU for a suspended RB, corresponding to LCID %d. Dropping it.\n", rx_lcid);
          break;
        }
        LOG_D(NR_MAC, "%4d.%2d : DLSCH -> LCID %d %d bytes\n", frameP, slot, rx_lcid, mac_len);
        nr_rlc_data_ind_t ind = {.ch = rx_lcid, .buf = pduP + mac_subheader_len, .len = mac_len};
        data_ind[num_data_ind++] = ind;
        DevAssert(num_data_ind < MAX_NUM_DATA_IND);
        break;
      default:
        LOG_W(MAC, "unknown lcid %02x\n", rx_lcid);
        break;
    }
    pduP += (mac_subheader_len + mac_len);
    pdu_len -= (mac_subheader_len + mac_len);
    if (pdu_len < 0) {
      LOG_E(MAC, "[UE %d][%d.%d] nr_ue_process_mac_pdu, residual mac pdu length %d < 0!\n", mac->ue_id, frameP, slot, pdu_len);
      done = 1;
    }
  }

  nr_mac_rlc_data_ind(mac->ue_id, mac->ue_id, false, data_ind, num_data_ind);
}

/**
 * Function:      generating MAC CEs (MAC CE and subheader) for the ULSCH PDU
 * Parameters:
 * @mac_ce        pointer to the MAC sub-PDUs including the MAC CEs
 * Return:        number of written bytes
 */
int nr_write_ce_ulsch_pdu(uint8_t *mac_ce, NR_SINGLE_ENTRY_PHR_MAC_CE *power_headroom, const type_bsr_t *bsr, uint8_t *mac_ce_end)
{
  uint8_t *pdu = mac_ce;
  if (power_headroom) {
    // MAC CE fixed subheader
    *(NR_MAC_SUBHEADER_FIXED *)mac_ce = (NR_MAC_SUBHEADER_FIXED){.R = 0, .LCID = UL_SCH_LCID_SINGLE_ENTRY_PHR};
    mac_ce += sizeof(NR_MAC_SUBHEADER_FIXED);
    *(NR_SINGLE_ENTRY_PHR_MAC_CE *)mac_ce = *power_headroom;
    mac_ce += sizeof(NR_SINGLE_ENTRY_PHR_MAC_CE);
    LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : power_headroom pdu %p mac_ce %p b\n",
          pdu, mac_ce);
  }

  switch (bsr->type_bsr) {
    case b_short:
    case b_short_trunc: {
      *(NR_MAC_SUBHEADER_FIXED *)mac_ce =
          (NR_MAC_SUBHEADER_FIXED){.R = 0, .LCID = bsr->type_bsr == b_short ? UL_SCH_LCID_S_BSR : UL_SCH_LCID_S_TRUNCATED_BSR};
      mac_ce += sizeof(NR_MAC_SUBHEADER_FIXED);
      *(NR_BSR_SHORT *)mac_ce = bsr->bsr.s;
      mac_ce += sizeof(NR_BSR_SHORT);
      LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : bsr Buffer_size %d LcgID %d \n", bsr->bsr.s.Buffer_size, bsr->bsr.s.LcgID);
    } break;
    case b_long:
    case b_long_trunc: {
      // ch 6.1.3.1. TS 38.321
      NR_MAC_SUBHEADER_SHORT *mac_pdu_subheader_ptr = (NR_MAC_SUBHEADER_SHORT *)mac_ce;
      mac_ce += sizeof(NR_MAC_SUBHEADER_SHORT);
      uint8_t *mark = mac_ce;
      // Could move to nr_get_sdu()
      NR_BSR_LONG *ceLong = (NR_BSR_LONG *)mac_ce;
      *ceLong = (NR_BSR_LONG){0};
      mac_ce += sizeof(NR_BSR_LONG);
      // int NR_BSR_LONG_SIZE = 1;
      if (bsr->bsr.lcg_bsr[0] && mac_ce < mac_ce_end) {
        ceLong->LcgID0 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[0];
      }
      if (bsr->bsr.lcg_bsr[1] && mac_ce < mac_ce_end) {
        ceLong->LcgID1 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[1];
      }
      if (bsr->bsr.lcg_bsr[2] && mac_ce < mac_ce_end) {
        ceLong->LcgID2 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[2];
      }
      if (bsr->bsr.lcg_bsr[3] && mac_ce < mac_ce_end) {
        ceLong->LcgID3 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[3];
      }
      if (bsr->bsr.lcg_bsr[4] && mac_ce < mac_ce_end) {
        ceLong->LcgID4 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[4];
      }
      if (bsr->bsr.lcg_bsr[5] && mac_ce < mac_ce_end) {
        ceLong->LcgID5 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[5];
      }
      if (bsr->bsr.lcg_bsr[6] && mac_ce < mac_ce_end) {
        ceLong->LcgID6 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[6];
      }
      if (bsr->bsr.lcg_bsr[7] && mac_ce < mac_ce_end) {
        ceLong->LcgID7 = 1;
        *mac_ce++ = bsr->bsr.lcg_bsr[7];
      }
      *mac_pdu_subheader_ptr =
          (NR_MAC_SUBHEADER_SHORT){.LCID = bsr->type_bsr == b_long ? UL_SCH_LCID_L_BSR : UL_SCH_LCID_L_TRUNCATED_BSR,
                                   .L = mac_ce - mark};
      LOG_D(NR_MAC,
            "[UE] Generating ULSCH PDU : long_bsr size %d Lcgbit 0x%02x Buffer_size %d %d %d %d %d %d %d %d\n",
            ((NR_MAC_SUBHEADER_SHORT *)mac_pdu_subheader_ptr)->L,
            *mac_ce,
            bsr->bsr.lcg_bsr[0],
            bsr->bsr.lcg_bsr[1],
            bsr->bsr.lcg_bsr[2],
            bsr->bsr.lcg_bsr[3],
            bsr->bsr.lcg_bsr[4],
            bsr->bsr.lcg_bsr[5],
            bsr->bsr.lcg_bsr[6],
            bsr->bsr.lcg_bsr[7]);
    } break;
    case b_none:
      break;
    default:
      DevAssert(false);
  }

  return mac_ce - pdu;
}

void nr_ue_send_sdu(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  LOG_D(NR_MAC,
        "In [%d.%d] Handling DLSCH PDU type %d\n",
        dl_info->frame,
        dl_info->slot,
        dl_info->rx_ind->rx_indication_body[pdu_id].pdu_type);

  // Processing MAC PDU
  // it parses MAC CEs subheaders, MAC CEs, SDU subheaderds and SDUs
  switch (dl_info->rx_ind->rx_indication_body[pdu_id].pdu_type) {
    case FAPI_NR_RX_PDU_TYPE_DLSCH :
      // start or restart dataInactivityTimer if any MAC entity receives a MAC SDU for DTCH logical channel,
      // DCCH logical channel, or CCCH logical channel
      if (mac->data_inactivity_timer)
        nr_timer_start(mac->data_inactivity_timer);
      // DL data arrival during RRC_CONNECTED when UL synchronisation status is "non-synchronised"
      if (!nr_timer_is_active(&mac->time_alignment_timer) && mac->state == UE_CONNECTED && !get_softmodem_params()->phy_test) {
        trigger_MAC_UE_RA(mac, NULL);
        break;
      }
      nr_ue_process_mac_pdu(mac, dl_info, pdu_id);
      break;
    case FAPI_NR_RX_PDU_TYPE_RAR :
      nr_ue_process_rar(mac, dl_info, pdu_id);
      break;
    default :
      AssertFatal(false, "Invalid DLSCH PDU type\n");
  }
}
