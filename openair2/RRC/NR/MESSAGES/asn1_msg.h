/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief primitives to build the asn1 messages
 */

#ifndef __RRC_NR_MESSAGES_ASN1_MSG__H__
#define __RRC_NR_MESSAGES_ASN1_MSG__H__

#include <common/utils/assertions.h>
#include "common/platform_constants.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "NR_ARFCN-ValueNR.h"
#include "NR_CellGroupConfig.h"
#include "NR_CipheringAlgorithm.h"
#include "NR_DRB-ToAddModList.h"
#include "NR_DRB-ToReleaseList.h"
#include "NR_IntegrityProtAlgorithm.h"
#include "NR_LogicalChannelConfig.h"
#include "NR_MeasConfig.h"
#include "NR_MeasTiming.h"
#include "NR_RLC-BearerConfig.h"
#include "NR_RLC-Config.h"
#include "NR_RRC-TransactionIdentifier.h"
#include "NR_RadioBearerConfig.h"
#include "NR_ReestablishmentCause.h"
#include "NR_SRB-ToAddModList.h"
#include "NR_SecurityConfig.h"
#include "NR_MeasurementReport.h"
#include "NR_MeasurementTimingConfiguration.h"
#include "NR_UE-NR-Capability.h"
#include "NR_UE-CapabilityRAT-ContainerList.h"
#include "NR_SIB2.h"
#include "NR_SIB3.h"
#include "NR_SIB4.h"
#include "NR_PagingUE-Identity.h"
#include "ds/seq_arr.h"
#include "ds/byte_array.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_configuration.h"
#include "common/utils/nr/nr_common.h"
struct asn_TYPE_descriptor_s;

typedef struct {
  uint8_t transaction_id;
  NR_SRB_ToAddModList_t *srb_config_list;
  NR_DRB_ToAddModList_t *drb_config_list;
  int *drb_rel;
  int n_drb_rel;
  NR_SecurityConfig_t *security_config;
  NR_MeasConfig_t *meas_config;
  byte_array_t dedicated_NAS_msg_list[MAX_DRBS_PER_UE];
  int num_nas_msg;
  byte_array_t *cgc;
  bool masterKeyUpdate;
  int nextHopChainingCount;
  byte_array_t ue_cap;
} nr_rrc_reconfig_param_t;

/*
 * The variant of the above function which dumps the BASIC-XER (XER_F_BASIC)
 * output into the chosen string buffer.
 * RETURN VALUES:
 *       0: The structure is printed.
 *      -1: Problem printing the structure.
 * WARNING: No sensible errno value is returned.
 */
int xer_sprint_NR(char *string, size_t string_size, struct asn_TYPE_descriptor_s *td, void *sptr);

byte_array_t do_SIB2_NR(const NR_SIB2_t *sib2);
byte_array_t do_SIB3_NR(const NR_SIB3_t *sib3);
byte_array_t do_SIB4_NR(NR_SIB4_t *sib4);

int do_RRCReject(uint8_t *const buffer);

int do_RRCSetup(uint8_t *const buffer,
                size_t buffer_size,
                const uint8_t transaction_id,
                const uint8_t *masterCellGroup,
                int masterCellGroup_len,
                NR_SRB_ToAddModList_t *SRBs);

int do_NR_SecurityModeCommand(uint8_t *const buffer,
                              const uint8_t Transaction_id,
                              const uint8_t cipheringAlgorithm,
                              NR_IntegrityProtAlgorithm_t integrityProtAlgorithm);

int do_NR_SA_UECapabilityEnquiry(uint8_t *const buffer, const uint8_t Transaction_id, const int band);

int do_NR_RRCRelease(uint8_t *buffer, size_t buffer_size, uint8_t Transaction_id);

byte_array_t do_RRCReconfiguration(const nr_rrc_reconfig_param_t *params);

int do_RRCSetupComplete(uint8_t *buffer,
                        size_t buffer_size,
                        const uint8_t Transaction_id,
                        uint8_t sel_plmn_id,
                        bool is_rrc_connection_setup,
                        uint64_t fiveG_S_TMSI,
                        const int dedicatedInfoNASLength,
                        const char *dedicatedInfoNAS);

int do_NR_MeasConfig(const NR_MeasConfig_t *measconfig, uint8_t *buf, int buf_size);

int do_NR_MeasurementTimingConfiguration(const NR_MeasurementTimingConfiguration_t *mtc, uint8_t *buf, int buf_size);

int do_RRCSetupRequest(uint8_t *buffer, size_t buffer_size, uint8_t *rv, uint64_t fiveG_S_TMSI_part1);

int do_nrMeasurementReport_SA(long trigger_to_measid,
                              long trigger_quantity,
                              bool report_rsrp,
                              long rs_type,
                              uint16_t Nid_cell,
                              int rsrp_index,
                              int num_neighbor_cells,
                              const uint16_t *neighbor_Nid_cells,
                              const int *neighbor_rsrp_indexes,
                              uint8_t *buffer,
                              size_t buffer_size);

int do_NR_RRCReconfigurationComplete_for_nsa(uint8_t *buffer, size_t buffer_size, NR_RRC_TransactionIdentifier_t Transaction_id);

int do_NR_RRCReconfigurationComplete(uint8_t *buffer, size_t buffer_size, const uint8_t Transaction_id);

byte_array_t do_NR_DLInformationTransfer(uint8_t Transaction_id, uint32_t pdu_length, uint8_t *pdu_buffer);

int do_NR_ULInformationTransfer(uint8_t **buffer,
                                uint32_t pdu_length,
                                uint8_t *pdu_buffer);

int do_RRCReestablishmentRequest(uint8_t *buffer,
                                 NR_ReestablishmentCause_t cause,
                                 uint32_t cell_id,
                                 uint16_t c_rnti,
                                 uint16_t short_mac_i);

int do_RRCReestablishment(int8_t nh_ncc, uint8_t *const buffer, size_t buffer_size, const uint8_t Transaction_id);

int do_RRCReestablishmentComplete(uint8_t *buffer, size_t buffer_size, int64_t rrc_TransactionIdentifier);

#include "openair2/RRC/NR/nr_rrc_defs.h"

NR_MeasConfig_t *get_MeasConfig(gNB_RRC_UE_t *ue,
                                const NR_MeasTiming_t *mt,
                                int band,
                                int nr_pci,
                                NR_ReportConfigToAddMod_t *rc_PER,
                                NR_ReportConfigToAddMod_t *rc_A2,
                                seq_arr_t *rc_A3_seq,
                                seq_arr_t *neigh_seq,
                                int *neigh_a3_id);
void free_MeasConfig(NR_MeasConfig_t *mc);

#define NR_PAGING_FULL_I_RNTI_SIZE 5 // 40 bits

/** Paging parameters for do_NR_Paging */
typedef struct {
  /// UE Identity type (ng-5G-S-TMSI or fullI-RNTI)
  NR_PagingUE_Identity_PR ue_identity_type;
  union {
    /// Full 48-bit 5G-S-TMSI (TS 23.003): AMF Set ID + AMF Pointer + 5G-TMSI
    uint64_t fiveg_s_tmsi;
    uint8_t full_i_rnti[NR_PAGING_FULL_I_RNTI_SIZE];
  } ue_identity;
  /// true = accessType non3GPP
  bool access_type;
  /// pagingCause voice
  int *paging_cause;
} nr_paging_params_t;

byte_array_t do_NR_Paging(int count, const nr_paging_params_t *params);
int nr_pcch_decode(const byte_array_t pcch, nr_paging_params_t *out_params, int *out_count);

byte_array_t get_HandoverPreparationInformation(nr_rrc_reconfig_param_t *params);
byte_array_t get_HandoverCommandMessage(nr_rrc_reconfig_param_t *params);
void fill_removal_lists_from_source_measConfig(NR_MeasConfig_t *measConfig, byte_array_t prep_info);
byte_array_t extract_ue_cap_from_HandoverPreparationInformation(byte_array_t prep_info);
byte_array_t doRRCReconfiguration_from_HandoverCommand(byte_array_t handoverCommand);

struct NR_UE_NR_Capability *get_ue_nr_capability(int rnti, uint8_t *buf, uint32_t len);
NR_UE_NR_Capability_t *decode_nr_ue_capability(int rnti, const NR_UE_CapabilityRAT_ContainerList_t *clist);
void dump_cgc(const uint8_t *buf, size_t len);

#endif  /* __RRC_NR_MESSAGES_ASN1_MSG__H__ */
