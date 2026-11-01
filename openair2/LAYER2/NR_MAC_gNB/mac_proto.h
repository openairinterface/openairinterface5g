/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief MAC functions prototypes for gNB
 */

#ifndef __LAYER2_NR_MAC_PROTO_H__
#define __LAYER2_NR_MAC_PROTO_H__

#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch_default_policies.h"
#include "LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch_default_policies.h"
#include "NR_TAG-Id.h"
#include "common/ngran_types.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_configuration.h"

void set_cset_offset(uint16_t);
void get_K1_K2(int N1, int N2, int *K1, int *K2, int layers);
int get_NTN_Koffset(const NR_ServingCellConfigCommon_t *scc);
bool is_ssb_configured(const NR_ServingCellConfigCommon_t *scc, int ssb_index);
int get_max_ssbs(const NR_ServingCellConfigCommon_t *scc);
int get_first_ul_slot(const frame_structure_t *fs, bool mixed);
int get_ul_slots_per_period(const frame_structure_t *fs);
int get_ul_slots_per_frame(const frame_structure_t *fs);
int get_dl_slots_per_period(const frame_structure_t *fs);
int get_full_ul_slots_per_period(const frame_structure_t *fs);
int get_full_dl_slots_per_period(const frame_structure_t *fs);
int get_ul_slot_offset(const frame_structure_t *fs, int idx, bool count_mixed);
int get_ul_period_idx_from_abs_slot(const frame_structure_t *fs, int slot, bool count_mixed, int max_period);
NR_UE_info_t **get_periodic_ue(periodic_ue_sched_t *p, int ul_idx, int index);
gNB_MAC_INST *mac_top_init_gNB(ngran_node_t node_type, const nr_rlc_configuration_t *default_rlc_config, nr_cell_sched_t **cell);
void mac_top_destroy_gNB(gNB_MAC_INST *mac);
void nr_mac_send_f1_setup_req(void);
int get_ssbidx_from_beam(const nr_cell_sched_t *cell, int beam_idx);
void nr_mac_cell_config_scc(nr_cell_sched_t *cell, NR_ServingCellConfigCommon_t *scc, const nr_mac_config_t *mac_config);
void nr_mac_send_config_req(gNB_MAC_INST *nrmac, nfapi_nr_config_request_scf_t *cfg);
void nr_mac_configure_sib1(nr_cell_sched_t *cell, const plmn_id_t *plmn, uint64_t cellID, int tac);
bool nr_mac_configure_other_sib(nr_cell_sched_t *cell, int num_cu_sib, const f1ap_sib_msg_t cu_sib[num_cu_sib]);
bool nr_mac_add_test_ue(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, uint32_t rnti, NR_CellGroupConfig_t *CellGroup);
void nr_mac_prepare_ra_ue(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, NR_UE_info_t *UE);
nr_cell_sched_t *nr_mac_get_cell_by_phy_id(gNB_MAC_INST *mac, uint16_t phy_id);
nr_cell_sched_t *nr_mac_get_cell_by_cgi(gNB_MAC_INST *mac, plmn_id_t plmn, uint64_t nr_cellid);
int nr_mac_get_reconfig_delay_slots(NR_SubcarrierSpacing_t scs);

int nr_transmission_action_indicator_stop(gNB_MAC_INST *mac, NR_UE_info_t *UE_info);

void clear_nr_nfapi_information(nr_cell_sched_t *cell, frame_t frameP, slot_t slotP);

void nr_mac_update_timers(gNB_MAC_INST *gNB, nr_cell_sched_t *cell);

void gNB_dlsch_ulsch_scheduler(module_id_t module_idP, int cell_id, frame_t frame_rxP, slot_t slot_rxP, NR_Sched_Rsp_t *sched_info);

/* \brief main DL scheduler function. Calls a preprocessor to decide on
 * resource allocation, then "post-processes" resource allocation (nFAPI
 * messages, statistics, HARQ handling, CEs, ... */
void nr_schedule_ue_spec(gNB_MAC_INST *mac,
                         nr_cell_sched_t *cell,
                         frame_t frame,
                         slot_t slot,
                         nfapi_nr_dl_tti_request_t *DL_req,
                         nfapi_nr_tx_data_request_t *TX_req);

/* \brief default DL preprocessor */
void nr_dlsch_preprocessor(gNB_MAC_INST *mac, nr_cell_sched_t *cell, post_process_pdsch_t *pp_pdsch);

void schedule_nr_sib1(nr_cell_sched_t *cell,
                      frame_t frameP,
                      slot_t slotP,
                      nfapi_nr_dl_tti_request_t *DL_req,
                      nfapi_nr_tx_data_request_t *TX_req);

void schedule_nr_other_sib(nr_cell_sched_t *cell,
                           frame_t frame,
                           slot_t slot,
                           nfapi_nr_dl_tti_request_t *DL_req,
                           nfapi_nr_tx_data_request_t *TX_req);

struct NR_SchedulingInfo2_r17* find_sib19_sched_info(const struct NR_SI_SchedulingInfo_v1700*);

void schedule_nr_mib(nr_cell_sched_t *cell, frame_t frameP, slot_t slotP, nfapi_nr_dl_tti_request_t *DL_req);

/* \brief main UL scheduler function. Calls a preprocessor to decide on
 * resource allocation, then "post-processes" resource allocation (nFAPI
 * messages, statistics, HARQ handling, ... */
void nr_schedule_ulsch(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, nfapi_nr_ul_dci_request_t *ul_dci_req);

/* \brief default UL preprocessor */
void nr_ulsch_preprocessor(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pusch_t *pp_pusch);

int check_sc_fdma_rbsize(long transform_precoding, uint16_t rb);

void nr_mac_pcch_queue_init(NR_COMMON_channels_t *cc);
void nr_mac_pcch_queue_free(NR_COMMON_channels_t *cc);
void nr_mac_pcch_enqueue(gNB_MAC_INST *mac, nr_cell_sched_t *cell, uint64_t fiveg_s_tmsi, uint16_t ue_id);
void schedule_nr_pcch(gNB_MAC_INST *gNB_mac,
                      nr_cell_sched_t *cell,
                      frame_t frameP,
                      slot_t slotP,
                      nfapi_nr_dl_tti_request_t *DL_req,
                      nfapi_nr_tx_data_request_t *TX_req);

////// Random Access MAC-PHY interface functions and primitives ///////

void nr_schedule_RA(gNB_MAC_INST *mac,
                    nr_cell_sched_t *cell,
                    frame_t frameP,
                    slot_t slotP,
                    nfapi_nr_ul_dci_request_t *ul_dci_req,
                    nfapi_nr_dl_tti_request_t *DL_req,
                    nfapi_nr_tx_data_request_t *TX_req);

void nr_initiate_ra_proc(module_id_t module_idP,
                         int phy_id,
                         frame_t frameP,
                         int slotP,
                         uint16_t preamble_index,
                         uint8_t freq_index,
                         uint8_t symbol,
                         int16_t timing_offset,
                         uint32_t preamble_power);

int nr_allocate_CCEs(int module_idP, int CC_idP, frame_t frameP, slot_t slotP, int test_only);

void schedule_nr_prach(gNB_MAC_INST *gNB, nr_cell_sched_t *cell, frame_t frameP, slot_t slotP);

uint16_t nr_mac_compute_RIV(uint16_t N_RB_DL, uint16_t RBstart, uint16_t Lcrbs);

/////// Phy test scheduler ///////

/* \brief preprocessor for phytest: schedules UE_id 0 with fixed MCS on all
 * freq resources */
void nr_preprocessor_phytest(gNB_MAC_INST *mac, nr_cell_sched_t *cell, post_process_pdsch_t *pp_pdsch);
/* \brief UL preprocessor for phytest: schedules UE_id 0 with fixed MCS on a
 * fixed set of resources */
void nr_ul_preprocessor_phytest(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, post_process_pusch_t *pp_pusch);

void handle_nr_uci_pucch_0_1(module_id_t mod_id,
                             int cell_id,
                             frame_t frame,
                             slot_t slot,
                             const nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_01);
void handle_nr_uci_pucch_2_3_4(module_id_t mod_id,
                               int cell_id,
                               frame_t frame,
                               slot_t slot,
                               const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *uci_234);

void config_uldci(const NR_UE_ServingCell_Info_t *sc_info,
                  const nfapi_nr_pusch_pdu_t *pusch_pdu,
                  dci_pdu_rel15_t *dci_pdu_rel15,
                  nr_srs_feedback_t *srs_feedback,
                  int *tpmi,
                  int time_domain_assignment,
                  uint8_t tpc,
                  uint8_t ndi,
                  NR_UE_UL_BWP_t *ul_bwp,
                  NR_SearchSpace__searchSpaceType_PR ss_type);
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
                                       bool is_sib1);
nfapi_nr_dl_dci_pdu_t *prepare_dci_pdu(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu,
                                       const NR_ServingCellConfigCommon_t *scc,
                                       const NR_SearchSpace_t *ss,
                                       const NR_ControlResourceSet_t *coreset,
                                       const uint16_t *spatial_stream_idx,
                                       int aggregation_level,
                                       int cce_index,
                                       int beam_index,
                                       int rnti);

void nr_schedule_pucch(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frameP, slot_t slotP);

void nr_srs_ri_computation(const nfapi_nr_srs_normalized_channel_iq_matrix_t *nr_srs_normalized_channel_iq_matrix,
                           const NR_UE_UL_BWP_t *current_BWP,
                           uint8_t *ul_ri);

int get_pucch_resourceid(NR_PUCCH_Config_t *pucch_Config, int O_uci, int pucch_resource);

void nr_schedule_periodic_srs(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frame, int slot);
bool nr_schedule_aperiodic_srs(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, NR_UE_info_t *UE, int sched_frame, int sched_slot, int k2, int sched_srs);
void nr_csirs_scheduling(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, nfapi_nr_dl_tti_request_t *DL_req);

void nr_csi_meas_reporting(gNB_MAC_INST *mac, nr_cell_sched_t *cell, frame_t frameP, slot_t slotP);

void nr_measgap_scheduling(gNB_MAC_INST *nr_mac, const nr_cell_sched_t *cell, frame_t frame, sub_frame_t slot);

int nr_acknack_scheduling(nr_cell_sched_t *cell,
                          NR_UE_info_t *UE,
                          frame_t frameP,
                          slot_t slotP,
                          int beam_index,
                          int r_pucch,
                          int is_common);

int get_pdsch_to_harq_feedback(NR_PUCCH_Config_t *pucch_Config,
                               nr_dci_format_t dci_format,
                               uint8_t *pdsch_to_harq_feedback);

int nr_get_pucch_resource(NR_ControlResourceSet_t *coreset,
                          NR_PUCCH_Config_t *pucch_Config,
                          int CCEIndex);

void nr_configure_pucch(nfapi_nr_pucch_pdu_t *pucch_pdu,
                        NR_ServingCellConfigCommon_t *scc,
                        NR_UE_info_t *UE,
                        uint8_t pucch_resource,
                        uint16_t O_csi,
                        uint16_t O_ack,
                        uint8_t O_sr,
                        int r_pucch,
                        const NR_beam_info_t *beam_info,
                        uint16_t ant_port_idx,
                        uint16_t *ssi,
                        uint16_t num_ant);

void find_search_space(int ss_type,
                       NR_BWP_Downlink_t *bwp,
                       NR_SearchSpace_t *ss);

void nr_configure_pdcch(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu, NR_ControlResourceSet_t *coreset, NR_sched_pdcch_t *pdcch);

NR_sched_pdcch_t set_pdcch_structure(nr_cell_sched_t *cell,
                                     NR_SearchSpace_t *ss,
                                     NR_ControlResourceSet_t *coreset,
                                     NR_ServingCellConfigCommon_t *scc,
                                     NR_BWP_t *bwp,
                                     NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config);

bool is_type0_occasion(NR_ServingCellConfigCommon_t *scc, const NR_Type0_PDCCH_CSS_config_t *type0, int frame, uint32_t slot);

int find_pdcch_candidate(const nr_cell_sched_t *cell,
                         int aggregation,
                         int nr_of_candidates,
                         int beam_idx,
                         const NR_sched_pdcch_t *pdcch,
                         const NR_ControlResourceSet_t *coreset,
                         uint32_t Y);

void fill_pdcch_vrb_map(nr_cell_sched_t *cell,
                        NR_sched_pdcch_t *pdcch,
                        int first_cce,
                        int aggregation,
                        int beam);
bool update_rb_mcs_tbs(NR_sched_pdsch_t *pdsch, uint32_t num_total_bytes, uint16_t *vrb_map);

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
                        uint16_t cset0_bwp_size);

void set_r_pucch_parms(int rsetindex,
                       int r_pucch,
                       int bwp_size,
                       int *prb_start,
                       int *second_hop_prb,
                       int *nr_of_symbols,
                       int *start_symbol_index);

/* find coreset within the search space */
NR_ControlResourceSet_t *get_coreset(nr_cell_sched_t *cell,
                                     NR_ServingCellConfigCommon_t *scc,
                                     NR_BWP_DownlinkDedicated_t *bwp_dedicated,
                                     NR_ControlResourceSetId_t coreset_id);

const NR_DMRS_UplinkConfig_t *get_DMRS_UplinkConfig(const NR_PUSCH_Config_t *pusch_Config, const NR_tda_info_t *tda_info);

NR_pusch_dmrs_t get_ul_dmrs_params(const NR_ServingCellConfigCommon_t *scc,
                                   const NR_UE_UL_BWP_t *ul_bwp,
                                   const NR_tda_info_t *tda_info,
                                   const int Layers,
                                   const uint16_t dmrs_ports,
                                   const uint8_t cdm_groups);

int get_spf(nfapi_nr_config_request_scf_t *cfg);

int to_absslot(nfapi_nr_config_request_scf_t *cfg,int frame,int slot);

int NRRIV2BW(int locationAndBandwidth,int N_RB);

int NRRIV2PRBOFFSET(int locationAndBandwidth,int N_RB);

/* Functions to manage an NR_list_t */
void create_nr_list(NR_list_t *listP, int len);
void resize_nr_list(NR_list_t *list, int new_len);
void destroy_nr_list(NR_list_t *list);
void add_nr_list(NR_list_t *listP, int id);
void remove_nr_list(NR_list_t *listP, int id);
void add_tail_nr_list(NR_list_t *listP, int id);
void add_front_nr_list(NR_list_t *listP, int id);
void remove_front_nr_list(NR_list_t *listP);
void configure_UE_BWP(nr_cell_sched_t *cell,
                      NR_ServingCellConfigCommon_t *scc,
                      NR_UE_info_t *UE,
                      bool is_RA,
                      int target_ss,
                      int dl_bwp_switch,
                      int ul_bwp_switch);

/* functions for creating/deleting UE data */
NR_UE_info_t *get_new_nr_ue_inst(uid_allocator_t *uia, rnti_t rnti, NR_CellGroupConfig_t *CellGroup, nr_cell_sched_t *cell);
void release_nr_ue_inst(uid_allocator_t *uia, NR_UE_info_t *UE);
void delete_nr_ue_data(void *UE); /* void * so can pass into collections */

/* remove a UE, whether in "access mode" or "connected mode" */
void mac_remove_nr_ue(gNB_MAC_INST *nr_mac, rnti_t rnti);

/* functions for UEs in "access mode" */
NR_UE_info_t *find_ra_ue(NR_UEs_t *UEs, rnti_t rntiP);
bool add_new_ra_ue(NR_UEs_t *UEs, NR_UE_info_t *UE);
NR_UE_info_t *remove_ra_ue(NR_UEs_t *UEs, rnti_t rnti);

/* functions for UEs in "connected mode" */
size_t get_num_connected_ue(NR_UEs_t *UEs);
NR_UE_info_t *find_connected_nr_ue(NR_UEs_t* UEs, rnti_t rntiP);
bool transition_ra_connected_nr_ue(gNB_MAC_INST *nr_mac, NR_UE_info_t *UE);
bool add_connected_nr_ue(gNB_MAC_INST *nr_mac, NR_UE_info_t *UE);
NR_UE_info_t *remove_connected_nr_ue(NR_UEs_t *UEs, rnti_t rnti);

bool nr_check_Msg4_MsgB_Ack(gNB_MAC_INST *nr_mac, nr_cell_sched_t *cell, frame_t frame, slot_t slot, NR_UE_info_t *UE, bool success);
nfapi_nr_pusch_pdu_t *prepare_pusch_pdu(nfapi_nr_ul_tti_request_t *future_ul_tti_req,
                                        const NR_UE_info_t *UE,
                                        const NR_ServingCellConfigCommon_t *scc,
                                        const NR_sched_pusch_t *sched_pusch,
                                        int transform_precoding,
                                        int harq_id,
                                        int harq_round,
                                        int fh,
                                        int rnti,
                                        const NR_beam_info_t *beam_info);
nfapi_nr_dl_tti_pdsch_pdu_rel15_t *prepare_pdsch_pdu(nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdsch_pdu,
                                                     const nr_cell_sched_t *cell,
                                                     const NR_UE_info_t *UE,
                                                     const NR_sched_pdsch_t *sched_pdsch,
                                                     const NR_PDSCH_Config_t *pdsch_Config,
                                                     bool is_sib1,
                                                     int harq_round,
                                                     int rnti,
                                                     int beam_index,
                                                     int nl_tbslbrm,
                                                     int pdu_index);
int nr_write_ce_dlsch_pdu(gNB_MAC_INST *gNB,
                          const NR_UE_sched_ctrl_t *ue_sched_ctl,
                          unsigned char *mac_pdu,
                          unsigned char drx_cmd,
                          unsigned char *ue_cont_res_id);

/* \brief Function to indicate a received SDU on ULSCH.
@param Mod_id Instance ID of gNB
@param CC_id Component carrier index
@param rnti RNTI of UE transmitting the SDU
@param sdu Pointer to received SDU
@param sdu_len Length of SDU
@param timing_advance timing advance adjustment after this pdu
@param ul_cqi Uplink CQI estimate after this pdu (SNR quantized to 8 bits, -64 ... 63.5 dB in .5dB steps)
*/
void nr_rx_sdu(const module_id_t gnb_mod_idP,
               const int CC_idP,
               const frame_t frameP,
               const slot_t subframeP,
               const rnti_t rntiP,
               uint8_t * sduP,
               const uint32_t sdu_lenP,
               const int8_t harq_pid,
               const uint16_t timing_advance,
               const uint8_t ul_cqi,
               const uint16_t rssi);

void reset_dl_harq_list(NR_UE_sched_ctrl_t *sched_ctrl);

void reset_ul_harq_list(NR_UE_sched_ctrl_t *sched_ctrl);

uint8_t *allocate_transportBlock_buffer(byte_array_t *tb, uint32_t needed);
void free_transportBlock_buffer(byte_array_t *tb);

void handle_nr_srs_measurements(const module_id_t module_id,
                                const int cell_id,
                                const frame_t frame,
                                const slot_t slot,
                                nfapi_nr_srs_indication_pdu_t *srs_ind);

void handle_nr_srs_toa_vendor_ext_measurements(const module_id_t module_id,
                                               const frame_t frame,
                                               const slot_t slot,
                                               const uint8_t num_ta,
                                               const int16_t *ta_offset_nsec,
                                               const rnti_t rnti);

void find_SSB_and_RO_available(nr_cell_sched_t *cell);

NR_pdsch_dmrs_t get_dl_dmrs_params(const NR_ServingCellConfigCommon_t *scc,
                                   const NR_UE_DL_BWP_t *BWP,
                                   const NR_tda_info_t *tda_info,
                                   const int Layers);

uint16_t get_pm_index(const nr_cell_sched_t *cell,
                      const NR_UE_info_t *UE,
                      nr_dci_format_t dci_format,
                      int layers,
                      int xp_pdsch_antenna_ports);

int get_mcs_from_SINRx10(int mcs_table, int SINRx10, int Nl);
uint8_t get_mcs_from_cqi(int mcs_table, int cqi_table, int cqi_idx);

uint8_t get_dl_nrOfLayers(const NR_UE_sched_ctrl_t *sched_ctrl, const nr_dci_format_t dci_format);

void free_sched_pucch_list(NR_UE_sched_ctrl_t *sched_ctrl);
int get_dl_tda(const gNB_MAC_INST *nrmac, const nr_cell_sched_t *cell, int slot);
int get_num_ul_tda(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, int slot, int k2, const NR_tda_info_t **first_idx);
const NR_tda_info_t *get_best_ul_tda(const nr_cell_sched_t *cell,
                                     int beam,
                                     const NR_tda_info_t *tdas,
                                     int n_tda,
                                     int frame,
                                     int slot,
                                     int *rb_start,
                                     int *rb_len);

int get_cce_index(const nr_cell_sched_t *cell,
                  const int slot,
                  const rnti_t rnti,
                  int *aggregation_level,
                  int beam_idx,
                  const NR_SearchSpace_t *ss,
                  const NR_ControlResourceSet_t *coreset,
                  NR_sched_pdcch_t *sched_pdcch,
                  float pdcch_cl_adjust);

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
                   uint16_t *nb_rb);

/**
 * \brief Function to return a free block of RBs of a required size within an BWP
 * \param rbSize_min Minimum size of the free block of RBs
 * \param rbSize_max Maximum size of the free block of RBs
 * \param bwpStart Start RB of the BWP
 * \param bwpSize Size of the BWP in RBs
 * \param vrb_map Pointer to the RB allocation map
 * \param sym_mask Mask indicating the symbols to be used in the block of RBs
 * \param rbStart_ptr Pointer returning the start RB of the found free block
 * \param rbSize_ptr Pointer returning the size of the found free block of RBs
 * \return Indicates if a free block of RBs of the required size could be found and *rbStart_ptr and *rbSize_ptr are set accordingly
 */
int find_largest_free_block(const uint16_t *vrb_map, uint16_t slbitmap, int bwp_start, int bwp_size, int *out_start);

bool get_rb_alloc(int rbSize_min,
                  int rbSize_max,
                  int bwpStart,
                  int bwpSize,
                  const uint16_t *vrb_map,
                  uint16_t sym_mask,
                  int *rbStart_ptr,
                  int *rbSize_ptr);

/* Scalar core of the BLER -> MCS adaptation rule. Single source of truth
 * for the activity-guard threshold and the lower/upper hysteresis. */
int nr_adapt_mcs_from_bler(int current_mcs,
                           int min_mcs,
                           int max_mcs,
                           float bler,
                           float bler_lower,
                           float bler_upper,
                           int num_sched);

bool update_bler_stats(const NR_bler_options_t *bler_options,
                       const NR_mac_dir_stats_t *stats,
                       NR_bler_stats_t *bler_stats,
                       frame_t frame);

float dl_pf_weight(int mcs, int mcs_table, int nrOfLayers, float avg_throughput);
uint16_t check_dl_retx_feasibility(const nr_dl_candidate_t *cand,
                                   int tda,
                                   const NR_tda_info_t *tda_info,
                                   const NR_ServingCellConfigCommon_t *scc,
                                   uint16_t max_rbSize);
bool nr_dl_validate_cce_pucch(const nr_dl_sched_params_t *params, nr_dl_candidate_t *cand);
bool commit_alloc(const nr_dl_sched_params_t *params, nr_dl_candidate_t *cand);

// Use inside the policy loops: sets RB/MCS on candidate, validates CCE/PUCCH,
// marks scheduled; continues on failure, returns on max_num_ue.
#define COMMIT_ALLOC(params, cand, rb_start_, rb_size_, mcs_, n_sched) \
  do {                                                                 \
    (cand)->sched_pdsch.alloc_type = PDSCH_TYPE1;                      \
    (cand)->sched_pdsch.rbStart = (rb_start_);                         \
    (cand)->sched_pdsch.rbSize = (rb_size_);                           \
    (cand)->sched_pdsch.mcs = (mcs_);                                  \
    if (!commit_alloc(params, cand))                                   \
      continue;                                                        \
    (cand)->scheduled = true;                                          \
    (n_sched)++;                                                       \
    if ((n_sched) >= (params)->max_num_ue)                             \
      return (n_sched);                                                \
  } while (0)

int ul_buffer_index(int frame, int slot, int slots_per_frame, int size);
void UL_tti_req_ahead_initialization(nr_cell_sched_t *cell, int n, frame_t frameP, int slotP);
uint64_t get_ssb_bitmap(const NR_ServingCellConfigCommon_t *scc);
uint64_t get_ssb_bitmap_and_len(const NR_ServingCellConfigCommon_t *scc, uint8_t *len);
void fill_beam_index_list(NR_ServingCellConfigCommon_t *scc, const nr_mac_config_t *config, nr_cell_sched_t *cell);
int get_beam_from_ssbidx(nr_cell_sched_t *cell, int ssb_idx);
int16_t get_allocated_beam(const NR_beam_info_t *beam_info, int frame, int slot, int slots_per_frame, int beam_number_in_period);
uint16_t convert_to_fapi_beam(const uint16_t beam_idx, const NR_beam_info_t *beam_info);
NR_beam_alloc_t beam_allocation_procedure(NR_beam_info_t *beam_info, int frame, int slot, int16_t beam_index, int slots_per_frame);
void reset_beam_status(NR_beam_info_t *beam_info, int frame, int slot, int16_t beam_index, int slots_per_frame, bool new_beam);
int beam_selection_procedures(nr_cell_sched_t *cell, NR_UE_info_t *UE);
void beam_switching_procedure(gNB_MAC_INST *mac, nr_cell_sched_t *cell, NR_UE_info_t *UE, int new_beam_index);
void nr_sr_reporting(gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, frame_t frameP, slot_t slotP);
bwp_info_t get_pdsch_bwp_start_size(nr_cell_sched_t *cell, NR_UE_info_t *UE);
bwp_info_t get_pusch_bwp_start_size(NR_UE_info_t *UE);
size_t dump_mac_stats(gNB_MAC_INST *gNB, const nr_cell_sched_t *cell, char *output, size_t strlen, bool reset_rsrp);
periodic_ue_sched_t create_period_structure(nr_cell_sched_t *cell, nr_periodic_channel_t channel);
long get_lcid_from_drbid(int drb_id);
long get_lcid_from_srbid(int srb_id);

bool prepare_initial_ul_rrc_message(gNB_MAC_INST *mac, nr_cell_sched_t *cell, NR_UE_info_t *UE);
void send_initial_ul_rrc_message(int rnti, const uint8_t *sdu, sdu_size_t sdu_len, void *data);

void finish_nr_dl_harq(NR_UE_sched_ctrl_t *sched_ctrl, int harq_pid);
void abort_nr_dl_harq(NR_UE_info_t* UE, int8_t harq_pid);

void nr_mac_trigger_release_timer(NR_UE_sched_ctrl_t *sched_ctrl, NR_SubcarrierSpacing_t subcarrier_spacing);
bool nr_mac_check_release(NR_UE_sched_ctrl_t *sched_ctrl);
void nr_mac_trigger_release_complete(gNB_MAC_INST *mac, int rnti);
void nr_mac_release_ue(gNB_MAC_INST *mac, int rnti);
bool nr_mac_request_release_ue(const gNB_MAC_INST *nrmac, int rnti);
void clean_bwp_structures(NR_SpCellConfig_t *spCellConfig);
bool nr_mac_ue_is_active(const NR_UE_info_t *ue);

void nr_mac_trigger_ul_failure(NR_UE_sched_ctrl_t *sched_ctrl, NR_SubcarrierSpacing_t subcarrier_spacing);
void nr_mac_reset_ul_failure(NR_UE_sched_ctrl_t *sched_ctrl);
bool nr_mac_check_ul_failure(gNB_MAC_INST *nrmac, int rnti, NR_UE_sched_ctrl_t *sched_ctrl);

void nr_mac_trigger_reconfiguration(const gNB_MAC_INST *nrmac, nr_cell_sched_t *cell, NR_UE_info_t *UE, int new_bwp_id, int new_beam);
void reset_sc_info(NR_UE_ServingCell_Info_t *sc_info);
bool nr_mac_add_lcid(NR_UE_sched_ctrl_t *sched_ctrl, const nr_lc_config_t *c);
nr_lc_config_t *nr_mac_get_lc_config(NR_UE_sched_ctrl_t* sched_ctrl, int lcid);
bool nr_mac_remove_lcid(NR_UE_sched_ctrl_t *sched_ctrl, long lcid);

bool nr_mac_get_new_rnti(NR_UEs_t *UEs, rnti_t *rnti);
void nr_mac_update_pdcch_closed_loop_adjust(NR_UE_sched_ctrl_t *sched_ctrl, bool feedback_not_detected);

void prepare_du_configuration_update(gNB_MAC_INST *mac,
                                     f1ap_served_cell_info_t *info,
                                     NR_BCCH_BCH_Message_t *mib,
                                     const NR_BCCH_DL_SCH_Message_t *sib1);

void nr_mac_clean_cellgroup(NR_CellGroupConfig_t *cell_group);

void post_process_dlsch(gNB_MAC_INST *nr_mac,
                        nr_cell_sched_t *cell,
                        post_process_pdsch_t *pdsch,
                        NR_UE_info_t *UE,
                        NR_sched_pdsch_t *sched_pdsch,
                        const nr_dl_candidate_t *candidate);
void post_process_ulsch(gNB_MAC_INST *nr_mac,
                        nr_cell_sched_t *cell,
                        post_process_pusch_t *pusch,
                        NR_UE_info_t *UE,
                        NR_sched_pusch_t *sched_pusch,
                        int sched_srs);

float nr_mac_get_snr(const nr_power_control_t *pc);
float nr_mac_get_rssi(const nr_power_control_t *pc);
void nr_mac_pc_snr(nr_power_control_t *pc, int snrx10, int rssi);
void nr_mac_pc_reset_snr(nr_power_control_t *pc, int snrx10, int rssi);
void nr_mac_set_target_snrx10(nr_power_control_t *pc, int target_snrx10);
void nr_mac_set_rssi_threshold(nr_power_control_t *pc, int rssi_threshold);
void nr_mac_signal_dtx(nr_power_control_t *pc);
int nr_mac_get_tpc(nr_power_control_t *pc);

/* UL scheduler helpers (shared between default policies and custom plugins) */
float ul_pf_weight(int mcs, int mcs_table, int nrOfLayers, float avg_throughput);
void update_ul_ue_R_Qm(int mcs, int mcs_table, const NR_PUSCH_Config_t *pusch_Config, uint16_t *R, uint8_t *Qm);
uint16_t check_ul_retx_feasibility(const nr_ul_candidate_t *cand,
                                   int tda,
                                   const NR_tda_info_t *tda_info,
                                   const NR_ServingCellConfigCommon_t *scc,
                                   uint16_t max_rbSize);
bool nr_ul_validate_cce(const nr_ul_sched_params_t *params, nr_ul_candidate_t *cand);
bool commit_ul_alloc(const nr_ul_sched_params_t *params, nr_ul_candidate_t *cand);

/* Use inside policy loops: writes alloc fields, validates CCE,
   marks scheduled; continues on failure, returns n_sched on max_num_ue. */
#define COMMIT_UL_ALLOC(params, cand, rb_start_, rb_size_, mcs_, n_sched) \
  do {                                                                    \
    (cand)->sched_pusch.rbStart = (rb_start_);                            \
    (cand)->sched_pusch.rbSize = (rb_size_);                              \
    (cand)->sched_pusch.mcs = (mcs_);                                     \
    if (!commit_ul_alloc(params, cand))                                   \
      continue;                                                           \
    (cand)->scheduled = true;                                             \
    (n_sched)++;                                                          \
    if ((n_sched) >= (params)->max_num_ue)                                \
      return (n_sched);                                                   \
  } while (0)

bool nr_ul_check_phr(const nr_ul_sched_params_t *params,
                     const nr_ul_candidate_t *cand,
                     uint16_t rbSize,
                     uint8_t mcs,
                     nr_ul_phr_advice_t *advice);
fsn_t get_fb_frame_slot(int frame, int slot, int K, int n_slots_frame, int NTN_gNB_Koffset);
#endif /*__LAYER2_NR_MAC_PROTO_H__*/
