/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __LAYER2_NR_MAC_COMMON_H__
#define __LAYER2_NR_MAC_COMMON_H__

#include "NR_MIB.h"
#include "NR_CellGroupConfig.h"
#include "NR_UE-NR-Capability.h"
#include "NR_PCCH-Config.h"
#include "nr_mac.h"
#include "nr_prach_config.h"
#include "common/utils/nr/nr_common.h"

#define NB_SRS_PERIOD         (18)
static const uint16_t srs_period[NB_SRS_PERIOD] = { 0, 1, 2, 4, 5, 8, 10, 16, 20, 32, 40, 64, 80, 160, 320, 640, 1280, 2560};

// TS 38.212
static const uint16_t table_7_3_1_1_2_2_1layer[28] = {0,  1,  2,  3,  12, 13, 14, 15, 16, 17, 18, 19, 32, 33,
                                                      34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};
static const uint16_t table_7_3_1_1_2_2_2layers[22] = {4,  5,  6,  7,  8,  9,  20, 21, 22, 23, 24,
                                                       25, 26, 27, 48, 49, 50, 51, 52, 53, 54, 55};
static const uint16_t table_7_3_1_1_2_2_3layers[7] = {10, 28, 29, 56, 57, 58, 59};
static const uint16_t table_7_3_1_1_2_2_4layers[5] = {11, 30, 31, 60, 61};
static const uint16_t table_7_3_1_1_2_2B_1layer[16] = {0, 1, 2, 3, 15, 16, 17, 18, 19, 20, 21, 22, 23, 12, 24, 25};
static const uint16_t table_7_3_1_1_2_2B_2layers[14] = {4, 5, 6, 7, 8, 9, 13, 26, 27, 28, 29, 30, 31, 32};
static const uint16_t table_7_3_1_1_2_2B_3layers[3] = {10, 14, 33};
static const uint16_t table_7_3_1_1_2_2B_4layers[3] = {11, 34, 35};
static const uint16_t table_7_3_1_1_2_2A_1layer[16] = {0, 1, 2, 3, 12, 13, 14, 15, 16, 17, 18, 19, 20, 10, 21, 22};
static const uint16_t table_7_3_1_1_2_2A_2layers[14] = {4, 5, 6, 7, 8, 9, 11, 23, 24, 25, 26, 27, 28, 29};
static const uint16_t table_7_3_1_1_2_3A[16] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 14, 15};
static const uint16_t table_7_3_1_1_2_4_1layer_fullyAndPartialAndNonCoherent[6] = {0, 1, 3, 4, 5, 6};
static const uint16_t table_7_3_1_1_2_4_2layers_fullyAndPartialAndNonCoherent[3] = {2, 7, 8};
static const uint16_t table_7_3_1_1_2_4A_1layer[3] = {0, 1, 3};
static const uint16_t table_7_3_1_1_2_28[3][15] = {
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};
static const uint16_t table_7_3_1_1_2_29[3][15] = {
    {0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 0, 3, 4, 0, 5, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0},
};
static const uint16_t table_7_3_1_1_2_30[3][15] = {
    {0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 0, 3, 4, 0, 5, 0, 0, 6, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0},
};
static const uint16_t table_7_3_1_1_2_31[3][15] = {
    {0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 0, 3, 4, 0, 5, 0, 0, 6, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14},
};
static const uint16_t table_7_3_1_1_2_32[3][15] = {
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

#define MAX_FRONTLOAD_SYMB 2
#define MAX_CDM_GROUPS 3
#define MAX_TYPE1_DMRS_MASK 256
#define MAX_TYPE2_DMRS_MASK 4096

int get_dci_antenna_ports_val(uint8_t rank, uint16_t dmrs_ports, uint8_t cdm, int dmrs_type, uint8_t front_load, int tp);
int decode_dci_antenna_ports_val(uint8_t rank,
                                 const long *dmrs_type,
                                 long tp,
                                 uint8_t val,
                                 uint8_t *cdm,
                                 uint16_t *dmrs_ports,
                                 int *front_load);

typedef struct {
  uint8_t cdm_groups;
  uint16_t port_mask;
  uint8_t num_front_load_symb;
} dci_port_rev_t;

// Type 1 UE reverse tables
static const dci_port_rev_t lut_rev_t1_r1[] = {{1, 1, 1},
                                               {1, 2, 1},
                                               {2, 1, 1},
                                               {2, 2, 1},
                                               {2, 4, 1},
                                               {2, 8, 1},
                                               {2, 1, 2},
                                               {2, 2, 2},
                                               {2, 4, 2},
                                               {2, 8, 2},
                                               {2, 16, 2},
                                               {2, 32, 2},
                                               {2, 64, 2},
                                               {2, 128, 2}};
static const dci_port_rev_t lut_rev_t1_r2[] =
    {{1, 3, 1}, {2, 3, 1}, {2, 12, 1}, {2, 5, 1}, {2, 3, 2}, {2, 12, 2}, {2, 48, 2}, {2, 192, 2}, {2, 17, 2}, {2, 68, 2}};
static const dci_port_rev_t lut_rev_t1_r3[] = {{2, 7, 1}, {2, 19, 2}, {2, 76, 2}};
static const dci_port_rev_t lut_rev_t1_r4[] = {{2, 15, 1}, {2, 51, 2}, {2, 204, 2}, {2, 85, 2}};

// Type 2 UE reverse tables
static const dci_port_rev_t lut_rev_t2_r1[] = {
    {1, 1, 1},   {1, 2, 1},   {2, 1, 1},    {2, 2, 1},    {2, 4, 1}, {2, 8, 1}, {3, 1, 1},  {3, 2, 1},  {3, 4, 1},  {3, 8, 1},
    {3, 16, 1},  {3, 32, 1},  {3, 1, 2},    {3, 2, 2},    {3, 4, 2}, {3, 8, 2}, {3, 16, 2}, {3, 32, 2}, {3, 64, 2}, {3, 128, 2},
    {3, 256, 2}, {3, 512, 2}, {3, 1024, 2}, {3, 2048, 2}, {1, 1, 2}, {1, 2, 2}, {1, 64, 2}, {1, 128, 2}};
static const dci_port_rev_t lut_rev_t2_r2[] = {{1, 3, 1},
                                               {2, 3, 1},
                                               {2, 12, 1},
                                               {3, 3, 1},
                                               {3, 12, 1},
                                               {3, 48, 1},
                                               {2, 5, 1},
                                               {3, 3, 2},
                                               {3, 12, 2},
                                               {3, 48, 2},
                                               {3, 192, 2},
                                               {3, 768, 2},
                                               {3, 3072, 2},
                                               {1, 3, 2},
                                               {1, 192, 2},
                                               {2, 3, 2},
                                               {2, 12, 2},
                                               {2, 192, 2},
                                               {2, 768, 2}};
static const dci_port_rev_t lut_rev_t2_r3[] = {{2, 7, 1}, {3, 7, 1}, {3, 56, 1}, {3, 67, 2}, {3, 268, 2}, {3, 1072, 2}};
static const dci_port_rev_t lut_rev_t2_r4[] = {{2, 15, 1}, {3, 15, 1}, {3, 195, 2}, {3, 780, 2}, {3, 3120, 2}};

static const dci_port_rev_t lut_tp_rev[] = {{2, 1, 1},
                                            {2, 2, 1},
                                            {2, 4, 1},
                                            {2, 8, 1},
                                            {2, 1, 2},
                                            {2, 2, 2},
                                            {2, 4, 2},
                                            {2, 8, 2},
                                            {2, 16, 2},
                                            {2, 32, 2},
                                            {2, 64, 2},
                                            {2, 128, 2}};

typedef enum {
  pusch_dmrs_pos0 = 0,
  pusch_dmrs_pos1 = 1,
  pusch_dmrs_pos2 = 2,
  pusch_dmrs_pos3 = 3,
} pusch_dmrs_AdditionalPosition_t;

typedef enum {
  pdsch_dmrs_pos0 = 0,
  pdsch_dmrs_pos1 = 1,
  pdsch_dmrs_pos2 = 2,
  pdsch_dmrs_pos3 = 3,
} pdsch_dmrs_AdditionalPosition_t;

typedef enum {
  pusch_len1 = 1,
  pusch_len2 = 2
} pusch_maxLength_t;

typedef struct {
  uint16_t bwpStart;
  uint16_t bwpSize;
} bwp_info_t;

typedef struct {
  float ssb_per_ro;
  int preambles_per_ssb;
} ssb_ro_preambles_t;

uint32_t get_Y(const NR_SearchSpace_t *ss, int slot, rnti_t rnti);
pusch_dmrs_AdditionalPosition_t get_pusch_dmrs_pos(const long *dmrs_AdditionalPosition);
uint8_t get_BG(uint32_t A, uint16_t R);
uint32_t get_short_bsr_value(int idx);
uint32_t get_long_bsr_value(int idx);
int16_t fill_dmrs_mask(const NR_PDSCH_Config_t *pdsch_Config,
                       int dci_format,
                       int dmrs_TypeA_Position,
                       int NrOfSymbols,
                       int startSymbol,
                       mappingType_t mappingtype,
                       int length);
int get_slots_per_frame_from_scs(int scs);
uint16_t get_ul_bitmap(const frame_structure_t *fs, int slot);
bool is_ul_slot(const slot_t slot, const frame_structure_t *fs);
bool is_dl_slot(const slot_t slot, const frame_structure_t *fs);
bool is_mixed_slot(const slot_t slot, const frame_structure_t *fs);
bool is_ul_or_mixed_slot(const slot_t slot, const frame_structure_t *fs);
int get_tdd_period_idx(NR_TDD_UL_DL_ConfigCommon_t *tdd);
void config_frame_structure(int mu,
                            const NR_TDD_UL_DL_ConfigCommon_t *tdd_UL_DL_ConfigurationCommon,
                            uint8_t tdd_period,
                            uint8_t frame_type,
                            frame_structure_t *fs);

NR_PDSCH_TimeDomainResourceAllocationList_t *get_dl_tdalist(const NR_UE_DL_BWP_t *DL_BWP,
                                                            int controlResourceSetId,
                                                            int ss_type,
                                                            nr_rnti_type_t rnti_type);

NR_PUSCH_TimeDomainResourceAllocationList_t *get_ul_tdalist(const NR_UE_UL_BWP_t *UL_BWP,
                                                            int controlResourceSetId,
                                                            int ss_type,
                                                            nr_rnti_type_t rnti_type);

NR_tda_info_t get_ul_tda_info(const NR_UE_UL_BWP_t *ul_bwp,
                              int controlResourceSetId,
                              int ss_type,
                              nr_rnti_type_t rnti_type,
                              int tda_index);

NR_tda_info_t get_dl_tda_info(const NR_UE_DL_BWP_t *dl_BWP,
                              int ss_type,
                              int tda_index,
                              int dmrs_typeA_pos,
                              int mux_pattern,
                              nr_rnti_type_t rnti_type,
                              int coresetid,
                              bool sib1);

uint8_t getRBGSize(uint16_t bwp_size, long rbg_size_config);

uint16_t nr_dci_size(const NR_UE_DL_BWP_t *DL_BWP,
                     const NR_UE_UL_BWP_t *UL_BWP,
                     const NR_UE_ServingCell_Info_t *sc_info,
                     long pdsch_HARQ_ACK_Codebook,
                     dci_pdu_rel15_t *dci_pdu,
                     nr_dci_format_t format,
                     nr_rnti_type_t rnti_type,
                     NR_ControlResourceSet_t *coreset,
                     int ss_type,
                     uint16_t cset0_bwp_size,
                     uint16_t alt_size);

uint16_t get_rb_bwp_dci(nr_dci_format_t format,
                        int ss_type,
                        uint16_t cset0_bwp_size,
                        uint16_t ul_bwp_size,
                        uint16_t dl_bwp_size,
                        uint16_t initial_ul_bwp_size,
                        uint16_t initial_dl_bwp_size);

void find_aggregation_candidates(int *aggregation_level, int *nr_of_candidates, const NR_SearchSpace_t *ss, int L);

bool get_nr_prach_sched_from_info(nr_prach_info_t info,
                                  int config_index,
                                  int frame,
                                  int slot,
                                  int mu,
                                  frequency_range_t freq_range,
                                  uint16_t *RA_sfn_index,
                                  uint8_t unpaired);

uint8_t get_pusch_mcs_table(long *mcs_Table,
                            int is_tp,
                            int dci_format,
                            int rnti_type,
                            int target_ss,
                            bool config_grant);

uint8_t compute_nr_root_seq(NR_RACH_ConfigCommon_t *rach_config,
                            uint8_t nb_preambles,
                            uint8_t unpaired,
                            frequency_range_t);
ssb_ro_preambles_t get_ssb_ro_preambles_4step(struct NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB *config);

ssb_ro_preambles_t get_ssb_ro_preambles_2step(struct NR_RACH_ConfigCommonTwoStepRA_r16__msgA_SSB_PerRACH_OccasionAndCB_PreamblesPerSSB_r16 *config);

void get_config_index_and_ssb_ro(const NR_MsgA_ConfigCommon_r16_t *msgacc,
                                 const NR_RACH_ConfigCommon_t *rach_ConfigCommon,
                                 uint8_t *config_index,
                                 ssb_ro_preambles_t *ssb_ro);

int ul_ant_bits(NR_DMRS_UplinkConfig_t *NR_DMRS_UplinkConfig, long transformPrecoder);

uint8_t get_pdsch_mcs_table(long *mcs_Table, int dci_format, int rnti_type, int ss_type);

uint16_t get_NCS(uint8_t index, uint16_t format, uint8_t restricted_set_config);
int compute_pucch_crc_size(int O_uci);
uint8_t get_l0_ul(uint8_t mapping_type, uint8_t dmrs_typeA_position);
int32_t get_l_prime(uint8_t duration_in_symbols, uint8_t mapping_type, pusch_dmrs_AdditionalPosition_t additional_pos, pusch_maxLength_t pusch_maxLength, uint8_t start_symbolt, uint8_t dmrs_typeA_position);

uint8_t get_L_ptrs(uint8_t mcs1, uint8_t mcs2, uint8_t mcs3, uint8_t I_mcs, uint8_t mcs_table);
uint8_t get_K_ptrs(uint32_t nrb0, uint32_t nrb1, uint32_t N_RB);

uint32_t nr_compute_tbs(uint16_t Qm,
                        uint16_t R,
			uint16_t nb_rb,
			uint16_t nb_symb_sch,
			uint16_t nb_dmrs_prb,
                        uint16_t nb_rb_oh,
                        uint8_t tb_scaling,
			uint8_t Nl);

/** \brief Computes Q based on I_MCS PDSCH and table_idx for downlink. Implements MCS Tables from 38.214. */
uint8_t nr_get_Qm_dl(uint8_t Imcs, uint8_t table_idx);
uint32_t nr_get_code_rate_dl(uint8_t Imcs, uint8_t table_idx);

/** \brief Computes Q based on I_MCS PDSCH and table_idx for uplink. Implements MCS Tables from 38.214. */
uint8_t nr_get_Qm_ul(uint8_t Imcs, uint8_t table_idx);
uint32_t nr_get_code_rate_ul(uint8_t Imcs, uint8_t table_idx);
int srs_binomial_sum(int count, int Lmax);
int srs_codebook_nb_res(NR_SRS_Config_t *srs_config);
int srs_non_codebook_nb_res(NR_SRS_Config_t *srs_config);
int get_nr_srs_offset(NR_SRS_PeriodicityAndOffset_t periodicityAndOffset);
void get_monitoring_period_offset(const NR_SearchSpace_t *ss, int *period, int *offset);

uint32_t nr_compute_tbslbrm(uint16_t table,
			    uint16_t nb_rb,
		            uint8_t Nl);

void get_type0_PDCCH_CSS_config_parameters(NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config,
                                           frame_t frameP,
                                           const NR_MIB_t *mib,
                                           uint8_t num_slot_per_frame,
                                           uint8_t ssb_subcarrier_offset,
                                           uint16_t ssb_start_symbol,
                                           NR_SubcarrierSpacing_t scs_ssb,
                                           frequency_range_t frequency_range,
                                           int nr_band,
                                           int grid_size,
                                           uint32_t ssb_index,
                                           uint32_t ssb_period,
                                           uint32_t ssb_offset_point_a);

uint16_t get_ssb_start_symbol(const long band, NR_SubcarrierSpacing_t scs, int i_ssb);

NR_tda_info_t get_info_from_tda_tables(default_table_type_t table_type,
                                       int tda,
                                       int dmrs_TypeA_Position,
                                       int normal_CP);

NR_tda_info_t set_tda_info_from_list(NR_PDSCH_TimeDomainResourceAllocationList_t *tdalist, int tda_index);

default_table_type_t get_default_table_type(int mux_pattern);

void fill_coresetZero(NR_ControlResourceSet_t *coreset0, NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config);
void fill_searchSpaceZero(NR_SearchSpace_t *ss0,
                          int slots_per_frame,
                          NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config);

uint8_t get_pusch_nb_antenna_ports(NR_PUSCH_Config_t *pusch_Config,
                                   NR_SRS_Config_t *srs_config,
                                   dci_field_t srs_resource_indicator);

uint16_t compute_pucch_prb_size(uint8_t nr_prbs,
                                uint16_t O_csi,
                                uint16_t O_ack,
                                uint8_t O_sr,
                                NR_PUCCH_MaxCodeRate_t *maxCodeRate,
                                uint8_t Qm,
                                uint8_t n_symb,
                                uint8_t n_re_ctrl);

float get_max_code_rate(NR_PUCCH_MaxCodeRate_t *maxCodeRate);
int get_f3_dmrs_symbols(NR_PUCCH_Resource_t *pucchres, NR_PUCCH_Config_t *pucch_Config);

unsigned int get_delta_f_RA_long(const unsigned int format);
unsigned int get_N_RA_RB(const unsigned int delta_f_RA_PRACH, const unsigned int delta_f_PUSCH);

void find_period_offset_SR(const NR_SchedulingRequestResourceConfig_t *SchedulingReqRec, int *period, int *offset);

bool csi_period_offset(const NR_CSI_ReportConfig_t *csirep,
                       const struct NR_CSI_ResourcePeriodicityAndOffset *periodicityAndOffset,
                       int *period,
                       int *offset);

bool is_periodic_occasion(int frame, int slot, int slots_per_frame, int period, int offset);

bool set_dl_ptrs_values(NR_PTRS_DownlinkConfig_t *ptrs_config,
                        uint16_t rbSize, uint8_t mcsIndex, uint8_t mcsTable,
                        uint8_t *K_ptrs, uint8_t *L_ptrs,uint8_t *portIndex,
                        uint8_t *nERatio,uint8_t *reOffset,
                        uint8_t NrOfSymbols);

bool set_ul_ptrs_values(NR_PTRS_UplinkConfig_t *ul_ptrs_config,
                        uint16_t rbSize,uint8_t mcsIndex, uint8_t mcsTable,
                        uint8_t *K_ptrs, uint8_t *L_ptrs,
                        uint8_t *reOffset, uint8_t *maxNumPorts, uint8_t *ulPower,
                        uint8_t NrOfSymbols);

/* \brief Set the transform precoding according to 6.1.3 of 3GPP TS 38.214 version 16.3.0 Release 16
@param    *current_UL_BWP  pointer to uplink bwp
@param    dci_format       dci format
@param    configuredGrant  indicates whether a configured grant was received or not
@returns                   transformPrecoding value */
long get_transformPrecoding(const NR_UE_UL_BWP_t *current_UL_BWP, nr_dci_format_t dci_format, uint8_t configuredGrant);

void compute_csi_bitlen(const NR_CSI_MeasConfig_t *csi_MeasConfig, nr_csi_report_t *csi_report_template);

uint16_t nr_get_csi_bitlen(nr_csi_report_t *csi_report);

uint32_t compute_PDU_length(uint32_t num_TLV, uint32_t total_length);

rnti_t nr_get_ra_rnti(uint8_t s_id, uint8_t t_id, uint8_t f_id, uint8_t ul_carrier_id);

rnti_t nr_get_MsgB_rnti(uint8_t s_id, uint8_t t_id, uint8_t f_id, uint8_t ul_carrier_id);

int get_FeedbackDisabled(NR_DownlinkHARQ_FeedbackDisabled_r17_t *downlinkHARQ_FeedbackDisabled_r17, int harq_pid);

int get_nrofHARQ_ProcessesForPDSCH(const NR_UE_ServingCell_Info_t *sc_info);

int get_nrofHARQ_ProcessesForPUSCH(const NR_UE_ServingCell_Info_t *sc_info);

int nr_get_prach_or_ul_mu(const NR_MsgA_ConfigCommon_r16_t *msgacc, const NR_RACH_ConfigCommon_t *rach_ConfigCommon, const int ul_mu);

int get_delta_for_k2(int mu);

int get_j_for_k2(int mu);

uint16_t nr_pcch_default_paging_cycle_rf(const NR_PCCH_Config_t *pcch);

void nr_pcch_n_and_paging_frame_offset(const NR_PCCH_Config_t *pcch, uint16_t T, uint16_t *N, uint8_t *PF_offset);

uint8_t nr_pcch_ns_per_pf(const NR_PCCH_Config_t *pcch);

bool nr_pcch_first_pdcch_start_mo(const struct NR_PCCH_Config__firstPDCCH_MonitoringOccasionOfPO *po_list,
                                  uint8_t i_s,
                                  int *start_mo);

bool nr_pcch_sfn_is_pf(uint16_t frame, uint8_t PF_offset, uint16_t T, uint16_t N, uint16_t ue_id);

uint8_t nr_pcch_po_index(uint16_t ue_id, uint16_t N, uint8_t Ns);

bool nr_pcch_ss0_po_half_frame(uint8_t i_s, int slot, int slots_per_frame);

bool nr_pcch_type2_po_mo_in_range(int frame, int slot, int slots_per_frame, int period, int offset, int start_mo, int end_mo);

uint16_t nr_pdcch_monitoring_symbols_mask(const BIT_STRING_t *symbols_in_slot, uint8_t sps);

uint8_t getNRBG(uint16_t bwp_size, uint16_t bwp_start, long rbg_size_config);
#endif
