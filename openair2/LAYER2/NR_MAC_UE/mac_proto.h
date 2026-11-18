/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief MAC functions prototypes for gNB and UE
 */

#ifndef __LAYER2_MAC_UE_PROTO_H__
#define __LAYER2_MAC_UE_PROTO_H__

#include "mac_defs.h"
#include "oai_asn1.h"
#include "RRC/NR_UE/rrc_defs.h"
#include "nfapi/open-nFAPI/nfapi/public_inc/nfapi_nr_interface.h"

#define NR_DL_MAX_DAI                            (4)                      /* TS 38.213 table 9.1.3-1 Value of counter DAI for DCI format 1_0 and 1_1 */
#define NR_DL_MAX_NB_CW                          (2)                      /* number of downlink code word */

// 38.213 Table 16.3-1 set of cyclic shift pairs
static const int16_t table_16_3_1[4][6] = {
                                          {0},
                                          {0, 3},
                                          {0, 2, 4},
                                          {0, 1, 2, 3, 4, 5}
                                       };

typedef struct psfch_params {
  uint16_t m0;
  uint16_t prb_ordinal_base;
  uint16_t selected_subchannel;
  uint16_t num_rbs;
  uint16_t num_psfch_slots;
  uint16_t prbs_per_subchannel_slot;
  uint16_t *rb_set;
} psfch_params_t;

/**\brief initialize the field in nr_mac instance
   \param mac      MAC pointer */
void nr_ue_init_mac(NR_UE_MAC_INST_t *mac);

void nr_ue_init_mac_sl(NR_UE_MAC_INST_t *mac);

NR_UE_DL_BWP_t *get_dl_bwp_structure(NR_UE_MAC_INST_t *mac, int bwp_id, bool setup);
NR_UE_UL_BWP_t *get_ul_bwp_structure(NR_UE_MAC_INST_t *mac, int bwp_id, bool setup);

void send_srb0_rrc(int ue_id, const uint8_t *sdu, sdu_size_t sdu_len, void *data);
void send_pcch_rrc(int ue_id, const uint8_t *sdu, sdu_size_t sdu_len, void *data);
void update_mac_ul_timers(NR_UE_MAC_INST_t *mac);
void update_mac_dl_timers(NR_UE_MAC_INST_t *mac);
NR_LC_SCHEDULING_INFO *get_scheduling_info_from_lcid(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid);

/**\brief apply default configuration values in nr_mac instance
   \param mac           mac instance */
void nr_ue_mac_default_configs(NR_UE_MAC_INST_t *mac);

void nr_ue_decode_mib(NR_UE_MAC_INST_t *mac, int cc_id);

/**\brief decode SIB1 and other SIs pdus in NR_UE, from if_module dl_ind
   \param mac            pointer to MAC instance
   \param gNB_index      gNB index
   \param sibs_mask      sibs mask
   \param pduP           pointer to pdu
   \param pdu_length     length of pdu
   \param frame,slot     Received TTI*/
void nr_ue_decode_BCCH_DL_SCH(NR_UE_MAC_INST_t *mac,
                              unsigned int gNB_index,
                              uint8_t ack_nack,
                              uint8_t *pduP,
                              uint32_t pdu_len,
                              int hfn,
                              int frame,
                              int slot);

void release_dl_BWP(NR_UE_MAC_INST_t *mac, int index);
void release_ul_BWP(NR_UE_MAC_INST_t *mac, int index);
void nr_release_mac_config_logicalChannelBearer(NR_UE_MAC_INST_t *mac, long channel_identity);

void nr_rrc_mac_config_req_cg(module_id_t module_id,
                              int cc_idP,
                              int hfn,
                              int frame,
                              NR_CellGroupConfig_t *cell_group_config,
                              NR_UE_NR_Capability_t *ue_Capability);

void nr_rrc_mac_config_req_mib(module_id_t module_id, int cc_idP, NR_MIB_t *mibP, bool barred);
void nr_rrc_mac_sched_sib(module_id_t module_id, int sched_sib);
void nr_rrc_mac_config_req_sib1(module_id_t module_id, int cc_idP, NR_SIB1_t *sib1, bool can_start_ra, bool dedicated_sib1);
void nr_rrc_mac_config_req_paging_ue_id(module_id_t module_id, uint64_t fiveG_S_TMSI);
void nr_rrc_mac_start_ra(module_id_t module_id, nr_mac_ra_start_cause_t cause);

struct position; /* forward declaration */
void nr_rrc_mac_config_other_sib(module_id_t module_id, NR_SIB19_r17_t *sib19_r17, int hfn, int frame, bool can_start_ra);
void nr_rrc_mac_resume_rb(module_id_t module_id, bool is_srb, int rb_id);
void nr_rrc_mac_config_req_reset(module_id_t module_id, NR_UE_MAC_reset_cause_t cause);

typedef struct nr_neighbor_cell_info {
  int16_t Nid_cell; // Physical Cell ID
  uint32_t ssb_freq; // SSB frequency (ARFCN)
  uint8_t active; // 1 = active, 0 = inactive
} nr_neighbor_cell_info_t;

void nr_rrc_mac_config_req_meas(module_id_t module_id, const nr_neighbor_cell_info_t *neighbor_cells, int num_neighbors);

/**\brief initialization NR UE MAC instance(s)*/
NR_UE_MAC_INST_t * nr_l2_init_ue(int nb_inst, int numerology, int rx2tx_capability);

/**\brief fetch MAC instance by module_id
   \param module_id index of MAC instance(s)*/
NR_UE_MAC_INST_t *get_mac_inst(module_id_t module_id);

void reset_mac_inst(NR_UE_MAC_INST_t *nr_mac);
void reset_ra(NR_UE_MAC_INST_t *nr_mac, bool free_prach);
void release_mac_configuration(NR_UE_MAC_INST_t *mac,
                               NR_UE_MAC_reset_cause_t cause);

/**\brief called at each slot, slot length based on numerology. now use u=0, scs=15kHz, slot=1ms
          performs BSR/SR/PHR procedures, random access procedure handler and DLSCH/ULSCH procedures.
   \param dl_info     DL indication
   \param ul_info     UL indication*/
void nr_ue_ul_scheduler(NR_UE_MAC_INST_t *mac, nr_uplink_indication_t *ul_info);
void nr_ue_dl_scheduler(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info);

nfapi_nr_ue_csi_payload_t nr_ue_aperiodic_csi_reporting(NR_UE_MAC_INST_t *mac, dci_field_t csi_request, int tda, long *K2);

nr_dci_format_t nr_ue_process_dci_indication_pdu(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_dci_indication_pdu_t *dci);

void nr_ue_process_l1_measurements(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_l1_measurements_t *l1_measurements);

void nr_ue_aperiodic_srs_scheduling(NR_UE_MAC_INST_t *mac, long resource_trigger, int frame, int slot);

bool trigger_periodic_scheduling_request(NR_UE_MAC_INST_t *mac,
                                         PUCCH_sched_t *pucch,
                                         frame_t frame,
                                         int slot);

int nr_get_csi_measurements(NR_UE_MAC_INST_t *mac,
                            frame_t frame,
                            int slot,
                            nfapi_nr_ue_csi_payload_t *csi_payload,
                            NR_PUCCH_Resource_t **csi_pucch);

nfapi_nr_ue_csi_payload_t nr_get_csi_payload(NR_UE_MAC_INST_t *mac,
                                 int csi_report_id,
                                 CSI_mapping_t mapping_type,
                                 const NR_CSI_MeasConfig_t *csi_MeasConfig);

/* \brief Get payload (MAC PDU) from UE PHY
@param dl_info            pointer to dl indication
@param ul_time_alignment  pointer to timing advance parameters
@param pdu_id             index of DL PDU
@returns void
*/
void nr_ue_send_sdu(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id);

typedef struct {
  union {
    NR_BSR_SHORT s;
    NR_BSR_LONG l;
    uint8_t lcg_bsr[8];
  } bsr;
  enum { b_none, b_long, b_short, b_short_trunc, b_long_trunc } type_bsr;
} type_bsr_t;

int nr_write_ce_ulsch_pdu(uint8_t *mac_ce, NR_SINGLE_ENTRY_PHR_MAC_CE *power_headroom, const type_bsr_t *bsr, uint8_t *mac_ce_end);

typedef struct {
  uint8_t phr_len;
  uint sdu_length_total;
  NR_SINGLE_ENTRY_PHR_MAC_CE phr;
  type_bsr_t bsr;
  uint8_t *pdu_end;
  uint8_t *end_for_tailer;
  uint8_t *cur_ptr;
  uint num_sdus;
  // int highest_priority = 16;
  //  variable used to store the lcid data status during lcp
  bool lcids_data_status[NR_MAX_NUM_LCID];
  uint32_t lcp_allocation_counter;
} NR_UE_MAC_CE_INFO;

void ue_dci_configuration(NR_UE_MAC_INST_t *mac, fapi_nr_dl_config_request_t *dl_config, const frame_t frame, const int slot);
void set_precoding_information_parameters(nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                                          int n_antenna_port,
                                          long transformPrecoder,
                                          int precoding_information,
                                          NR_PUSCH_Config_t *pusch_Config);

bool get_downlink_ack(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, PUCCH_sched_t *pucch);
initial_pucch_resource_t get_initial_pucch_resource(const int idx);
void multiplex_pucch_resource(NR_UE_MAC_INST_t *mac, PUCCH_sched_t *pucch, int num_res);
int16_t get_pucch_tx_power_ue(NR_UE_MAC_INST_t *mac,
                              int scs,
                              NR_PUCCH_Config_t *pucch_Config,
                              int delta_pucch,
                              uint8_t format_type,
                              uint16_t nb_of_prbs,
                              uint8_t freq_hop_flag,
                              uint8_t add_dmrs_flag,
                              uint8_t N_symb_PUCCH,
                              int subframe_number,
                              int O_uci,
                              uint16_t start_prb);
int get_pusch_tx_power_ue(
  NR_UE_MAC_INST_t *mac,
  int num_rb,
  int start_prb,
  uint16_t nb_symb_sch,
  uint16_t nb_dmrs_prb,
  uint16_t nb_ptrs_prb,
  uint16_t qm,
  uint16_t R,
  uint16_t beta_offset_csi1,
  uint32_t sum_bits_in_codeblocks,
  int delta_pusch,
  bool is_rar_tx_retx,
  bool transform_precoding);

int get_srs_tx_power_ue(NR_UE_MAC_INST_t *mac,
                        NR_SRS_Resource_t *srs_resource,
                        NR_SRS_ResourceSet_t *srs_resource_set,
                        int delta_srs,
                        bool is_configured_for_pusch_on_current_bwp);

int nr_ue_configure_pucch(NR_UE_MAC_INST_t *mac,
                           int slot,
                           frame_t frame,
                           uint16_t rnti,
                           PUCCH_sched_t *pucch,
                           fapi_nr_ul_config_pucch_pdu *pucch_pdu);

float nr_get_Pcmax(int p_Max,
                   uint16_t nr_band,
                   frame_type_t frame_type,
                   frequency_range_t frequency_range,
                   int channel_bandwidth_index,
                   int Qm,
                   bool powerBoostPi2BPSK,
                   int scs,
                   int N_RB_UL,
                   bool is_transform_precoding,
                   int n_prbs,
                   int start_prb);

float nr_get_Pcmin(int bandwidth_index);
bool set_antenna_port_parameters(fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu, int n_cw, long *max_length, long *dmrs, int ant);
int get_sum_delta_pucch(NR_UE_MAC_INST_t *mac, int slot, frame_t frame);
void ul_ports_config(NR_UE_MAC_INST_t *mac,
                     int *n_front_load_symb,
                     nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                     dci_pdu_rel15_t *dci);

bool init_RA(NR_UE_MAC_INST_t *mac);

/* Random Access */
/* \brief This function schedules the PRACH according to prach_ConfigurationIndex and TS 38.211 tables 6.3.3.2.x
and fills the PRACH PDU per each FD occasion.
@param mac pointer to MAC instance
@param frameP Frame index
@param slotP Slot index
@returns void
*/

void configure_csi_resource_mapping(fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                    const NR_CSI_RS_ResourceMapping_t *resourceMapping,
                                    uint32_t bwp_size,
                                    uint32_t bwp_start);

bool is_lcid_suspended(NR_UE_MAC_INST_t *mac, int lcid);
void nr_ra_contention_resolution_failed(NR_UE_MAC_INST_t *mac);
void nr_ra_succeeded(NR_UE_MAC_INST_t *mac, const frame_t frame, const int slot);
void nr_ra_backoff_setting(RA_config_t *ra);
void nr_get_RA_window(NR_UE_MAC_INST_t *mac);
void prepare_msg4_msgb_feedback(NR_UE_MAC_INST_t *mac, int pid, int ack_nack);
void configure_initial_pucch(PUCCH_sched_t *pucch, int res_ind, int pucch_ResourceCommon);
void release_PUCCH_SRS(NR_UE_MAC_INST_t *mac);
void nr_ue_reset_sync_state(NR_UE_MAC_INST_t *mac, bool reconf);
void nr_ue_send_synch_request(NR_UE_MAC_INST_t *mac, module_id_t module_id, int cc_id, const fapi_nr_synch_request_t *sync_req);
bool is_ss_monitor_occasion(const int frame, const int slot, const int slots_per_frame, const NR_SearchSpace_t *ss);

/**
 * @brief   Get UE sync state
 * @param   mod_id      UE ID
 * @return      UE sync state
 */
NR_UE_L2_STATE_t nr_ue_get_sync_state(module_id_t mod_id);

int16_t get_prach_tx_power(NR_UE_MAC_INST_t *mac);
void schedule_RA_after_SR_failure(NR_UE_MAC_INST_t *mac);
void nr_rar_not_successful(NR_UE_MAC_INST_t *mac);
void ra_resource_selection(NR_UE_MAC_INST_t *mac);
void nr_Msg3_transmitted(NR_UE_MAC_INST_t *mac);
void trigger_MAC_UE_RA(NR_UE_MAC_INST_t *mac, dci_pdu_rel15_t *pdcch_order);
void nr_get_Msg3_MsgA_PUSCH_payload(NR_UE_MAC_INST_t *mac, uint8_t *buf, int TBS_max);
void handle_time_alignment_timer_expired(NR_UE_MAC_INST_t *mac);
void handle_ulsync_loss(NR_UE_MAC_INST_t *mac);
int8_t nr_ue_process_dci_freq_dom_resource_assignment(nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                                                      fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu,
                                                      NR_PDSCH_Config_t *pdsch_Config,
                                                      uint16_t n_RB_ULBWP,
                                                      uint16_t n_RB_DLBWP,
                                                      int start_DLBWP,
                                                      dci_field_t frequency_domain_assignment);

void ue_init_config_request(NR_UE_MAC_INST_t *mac, int scs);

fapi_nr_dl_config_request_t *get_dl_config_request(NR_UE_MAC_INST_t *mac, int slot);

fapi_nr_ul_config_request_pdu_t *lockGet_ul_config(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx, uint8_t pdu_type);
void remove_ul_config_last_item(fapi_nr_ul_config_request_pdu_t *pdu);
fapi_nr_ul_config_request_pdu_t *fapiLockIterator(fapi_nr_ul_config_request_t *ul_config, frame_t frame_tx, int slot_tx);

void release_ul_config(fapi_nr_ul_config_request_pdu_t *pdu, bool clearIt);
int16_t compute_nr_SSB_PL(NR_UE_MAC_INST_t *mac);

// PUSCH scheduler:
// - Calculate the slot in which ULSCH should be scheduled. This is current slot + K2,
// - where K2 is the offset between the slot in which UL DCI is received and the slot
// - in which ULSCH should be scheduled. K2 is configured in RRC configuration.
// PUSCH Msg3 scheduler:
// - scheduled by RAR UL grant according to 8.3 of TS 38.213
int nr_ue_pusch_scheduler(const NR_UE_MAC_INST_t *mac,
                          const uint8_t is_Msg3,
                          const frame_t current_frame,
                          const int current_slot,
                          frame_t *frame_tx,
                          int *slot_tx,
                          const long k2);

int get_rnti_type(const NR_UE_MAC_INST_t *mac, const uint16_t rnti);

// Configuration of Msg3 PDU according to clauses:
// - 8.3 of 3GPP TS 38.213 version 16.3.0 Release 16
// - 6.1.2.2 of TS 38.214
// - 6.1.3 of TS 38.214
// - 6.2.2 of TS 38.214
// - 6.1.4.2 of TS 38.214
// - 6.4.1.1.1 of TS 38.211
// - 6.3.1.7 of 38.211
int nr_config_pusch_pdu(NR_UE_MAC_INST_t *mac,
                        NR_tda_info_t *tda_info,
                        nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                        dci_pdu_rel15_t *dci,
                        nfapi_nr_ue_csi_payload_t *csi_report,
                        RAR_grant_t *rar_grant,
                        rnti_t rnti,
                        int ss_type,
                        const nr_dci_format_t dci_format);

int nr_rrc_mac_config_req_sl_preconfig(module_id_t module_id,
                                       NR_SL_PreconfigurationNR_r16_t *sl_preconfiguration,
                                       uint8_t sync_source);

uint16_t count_on_bits(const uint8_t *buf, size_t size);

void nr_rrc_mac_transmit_slss_req(module_id_t module_id,
                                  uint8_t *sl_mib_payload,
                                  uint16_t tx_slss_id,
                                  NR_SL_SSB_TimeAllocation_r16_t *ssb_ta);
void nr_rrc_mac_config_req_sl_mib(module_id_t module_id,
                                  NR_SL_SSB_TimeAllocation_r16_t *ssb_ta,
                                  uint16_t rx_slss_id,
                                  uint8_t *sl_mib);

void nr_sl_params_read_conf(module_id_t module_id);

void sl_prepare_psbch_payload(NR_TDD_UL_DL_ConfigCommon_t *TDD_UL_DL_Config,
                              uint8_t *bits_0_to_7, uint8_t *bits_8_to_11,
                              uint8_t mu, uint8_t L, uint8_t Y);

uint8_t sl_decode_sl_TDD_Config(NR_TDD_UL_DL_ConfigCommon_t *TDD_UL_DL_Config,
                                uint8_t bits_0_to_7, uint8_t bits_8_to_11,
                                uint8_t mu, uint8_t L, uint8_t Y);

uint8_t sl_determine_sci_1a_len(uint16_t *num_subchannels,
                                NR_SL_ResourcePool_r16_t *rpool,
                                sidelink_sci_format_1a_fields_t *sci_1a);
/** \brief This function checks nr UE slot for Sidelink direction : Sidelink
 *  @param cfg      : Sidelink config request
 *  @param nr_frame : frame number
 *  @param nr_slot  : slot number
 *  @param frame duplex type  : Frame type
    @returns int : 0 or Sidelink slot type */
int sl_nr_ue_slot_select(const sl_nr_phy_config_request_t *cfg, int nr_slot, uint8_t frame_duplex_type);

void nr_ue_process_mac_sl_pdu(int module_idP,
                              sl_nr_rx_indication_t *rx_ind,
                              int pdu_id);

NR_SL_UE_info_t* find_UE(NR_UE_MAC_INST_t *mac,
                         uint16_t ue_id);


uint16_t sl_get_subchannel_size(NR_SL_ResourcePool_r16_t *rpool);

int nr_ue_process_sci1_indication_pdu(NR_UE_MAC_INST_t *mac,module_id_t mod_id,int cc_id,frame_t frame, int slot, sl_nr_sci_indication_pdu_t *sci,void *phy_data);

void nr_ue_sidelink_scheduler(nr_sidelink_indication_t *sl_ind, NR_UE_MAC_INST_t *mac);

NR_SearchSpace_t *get_common_search_space(const NR_UE_MAC_INST_t *mac, const NR_SearchSpaceId_t ss_id);
void update_pdcch_config(NR_UE_MAC_INST_t *mac);

void nr_schedule_slsch(NR_UE_MAC_INST_t *mac, int frameP, int slotP, nr_sci_pdu_t *sci_pdu,
                       nr_sci_pdu_t *sci2_pdu,
                       nr_sci_format_t format2,
                       NR_SL_UE_info_t *UE,
                       uint16_t *slsch_pdu_length,
                       NR_UE_sl_harq_t *cur_harq,
                       mac_rlc_status_resp_t *rlc_status,
                       sl_resource_info_t *resource);

void nr_ue_sl_free_resource_pools(NR_UE_MAC_INST_t *mac);
void nr_ue_sl_mac_free(NR_UE_MAC_INST_t *mac);
void sl_ue_mac_free(NR_UE_MAC_INST_t *mac);

bool nr_ue_sl_pssch_scheduler(NR_UE_MAC_INST_t *mac,
                              nr_sidelink_indication_t *sl_ind,
                              const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                              const NR_SL_ResourcePool_r16_t *sl_res_pool,
                              sl_nr_tx_config_request_t *tx_config,
                              sl_resource_info_t *resource,
                              uint8_t *config_type);

void nr_ue_sl_pscch_rx_scheduler(nr_sidelink_indication_t *sl_ind,
                                 const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                                 const NR_SL_ResourcePool_r16_t *sl_res_pool,
                                 sl_nr_rx_config_request_t *rx_config,
                                 uint8_t *config_type,
                                 bool sl_has_psfch);

void nr_ue_sl_psfch_scheduler(NR_UE_MAC_INST_t *mac,
                              frame_t frame,
                              uint16_t slot,
                              long psfch_period,
                              nr_sidelink_indication_t *sl_ind,
                              const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                              sl_nr_tx_config_request_t *tx_config,
                              uint8_t *config_type);

void config_pscch_pdu_rx(sl_nr_rx_config_pscch_pdu_t *nr_sl_pscch_pdu,
                         const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                         const NR_SL_ResourcePool_r16_t *sl_res_pool,
                         bool sl_has_psfch);

int config_pssch_sci_pdu_rx(sl_nr_rx_config_pssch_sci_pdu_t *nr_sl_pssch_sci_pdu,
                             nr_sci_format_t sci2_format,
                             nr_sci_pdu_t *sci_pdu,
                             uint32_t pscch_Nid,
                             const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                             const NR_SL_ResourcePool_r16_t *sl_res_pool);

sl_resource_info_t* get_resource_element(seq_arr_t* resource_list, frameslot_t sfn);

int nr_ue_process_sci2_indication_pdu(NR_UE_MAC_INST_t *mac,
                                      module_id_t mod_id,
                                      int cc_id,
                                      frame_t frame,
                                      int slot,
                                      sl_nr_sci_indication_pdu_t *sci,
                                      void *phy_data);

void extract_pssch_sci_pdu(uint64_t *sci2_payload,
                           int len,
                           const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                           const NR_SL_ResourcePool_r16_t *sl_res_pool,
                           nr_sci_pdu_t *sci_pdu,
                           nr_sci_format_t sci2_format);

void fill_pssch_pscch_pdu(sl_nr_ue_mac_params_t *sl_mac_params,
                          sl_nr_tx_config_pscch_pssch_pdu_t *nr_sl_pssch_pscch_pdu,
                          const NR_SL_BWP_ConfigCommon_r16_t *sl_bwp,
                          const NR_SL_ResourcePool_r16_t *sl_res_pool,
                          nr_sci_pdu_t *sci_pdu,
                          nr_sci_pdu_t *sci2_pdu,
                          uint16_t slsch_pdu_length,
                          const nr_sci_format_t format1,
                          const nr_sci_format_t format2,
                          uint16_t slot,
                          sl_resource_info_t *selected_resource);

void fill_psfch_pdu(SL_sched_feedback_t *mac_psfch_pdu,
                    sl_nr_tx_rx_config_psfch_pdu_t *tx_psfch_pdu,
                    int num_psfch_symbols);

void update_harq_lists(NR_UE_MAC_INST_t *mac, frame_t frame, sub_frame_t slot, NR_SL_UE_info_t* UE);

int find_current_slot_harqs(frame_t frame, sub_frame_t slot, NR_SL_UE_sched_ctrl_t * sched_ctrl, NR_UE_sl_harq_t **matched_harqs);

int sl_num_slsch_feedbacks(NR_UE_MAC_INST_t *mac);

bool is_feedback_scheduled(NR_UE_MAC_INST_t *mac, int frameP,int slotP);

uint16_t sl_get_num_subch(NR_SL_ResourcePool_r16_t *rpool);

void fill_psfch_params_tx(NR_UE_MAC_INST_t *mac,
                          sl_nr_rx_indication_t *rx_ind,
                          int pdu_id,
                          long psfch_period,
                          uint16_t sched_frame,
                          uint16_t sched_slot,
                          uint8_t ack_nack,
                          psfch_params_t *psfch_params,
                          const int nr_slots_frame,
                          int psfch_index);

void fill_psfch_params_rx(sl_nr_rx_config_request_t *rx_config, sl_nr_tx_rx_config_psfch_pdu_t *psfch_pdu, psfch_params_t *psfch_params, NR_UE_sl_harq_t *cur_harq, NR_UE_MAC_INST_t *mac, long psfch_period, const uint16_t slot, uint16_t peer_id);

void configure_psfch_params_rx(int module_idP, NR_UE_MAC_INST_t *mac, sl_nr_rx_config_request_t *rx_config);

void reset_sched_psfch(NR_UE_MAC_INST_t *mac, int frameP,int slotP);

void handle_nr_ue_sl_harq(module_id_t mod_id, frame_t frame, sub_frame_t slot, sl_nr_slsch_pdu_t *rx_slsch_pdu, uint16_t src_id);

void handle_nr_ue_sl_psfch(module_id_t mod_id, frame_t frame, sub_frame_t slot, const sl_nr_psfch_pdu_t *rx_psfch_pdu);

void abort_nr_ue_sl_harq(NR_UE_MAC_INST_t *mac, int8_t harq_pid, NR_SL_UE_info_t *UE_info);

int nr_ue_sl_acknack_scheduling(NR_UE_MAC_INST_t *mac, sl_nr_rx_indication_t *rx_ind,
                                long psfch_period, uint16_t frame, uint16_t slot, const int nr_slots_frame);





void update_sensing_data(seq_arr_t* sensing_data, frameslot_t *frame_slot, sl_nr_ue_mac_params_t *sl_mac, uint16_t pool_id);

void update_transmit_history(seq_arr_t* transmit_history, frameslot_t *frame_slot, sl_nr_ue_mac_params_t *sl_mac, uint16_t pool_id);

int64_t normalize(frameslot_t *frame_slot, uint8_t mu);

void de_normalize(int64_t abs_slot_idx, uint8_t mu, frameslot_t *frame_slot);

frameslot_t add_to_sfn(frameslot_t* sfn, uint16_t slot_n, uint8_t mu);

uint16_t get_T2_min(uint16_t pool_id, sl_nr_ue_mac_params_t *sl_mac, uint8_t mu);

uint16_t get_t2(uint16_t pool_id,
                uint8_t mu,
                nr_sl_transmission_params_t* sl_tx_params,
                sl_nr_ue_mac_params_t *sl_mac);

uint16_t time_to_slots(uint8_t mu, uint16_t time);

/* TS 38.331 ASN.1 enum -> physical-unit conversions.
 * These are the canonical functions for reading sidelink config fields;
 * use them at every MAC/RRC boundary so the rest of the stack sees ms. */
uint16_t nr_sl_sensing_window_to_ms(long idx);  /* {ms100=0, ms1100=1} */
uint16_t nr_sl_sw_to_ms(long idx);              /* {n1=0, n5=1, n10=2, n20=3} */
uint16_t nr_sl_rrp_to_ms(const NR_SL_ResourceReservePeriod_r16_t *p);

uint8_t get_tproc0(sl_nr_ue_mac_params_t *sl_mac, uint16_t pool_id);

void remove_old_sensing_data(frameslot_t *frame_slot,
                             uint16_t sensing_window,
                             seq_arr_t* sensing_data,
                             sl_nr_ue_mac_params_t *sl_mac);

void remove_old_transmit_history(frameslot_t *frame_slot,
                                 uint16_t sensing_window,
                                 seq_arr_t* transmit_history,
                                 sl_nr_ue_mac_params_t *sl_mac);

seq_arr_t* get_candidate_resources(frameslot_t *frame_slot,
                                NR_UE_MAC_INST_t *mac,
                                seq_arr_t *sensing_data,
                                seq_arr_t *transmission_history);

seq_arr_t get_nr_sl_comm_opportunities(NR_UE_MAC_INST_t *mac,
                                    uint64_t abs_idx_cur_slot,
                                    uint8_t bwp_id,
                                    uint16_t mu,
                                    uint16_t pool_id,
                                    uint8_t t1,
                                    uint16_t t2,
                                    uint8_t psfch_period);

bool is_sl_slot(NR_UE_MAC_INST_t *mac, BIT_STRING_t *phy_sl_bitmap, size_t phy_map_sz, uint64_t abs_slot);

void validate_selected_sl_slot(NR_UE_MAC_INST_t *mac, bool tx, bool rx, NR_TDD_UL_DL_ConfigCommon_t *conf, frameslot_t frame_slot);

bool check_t1_within_tproc1(uint8_t mu, uint16_t t1_slots);

NR_SL_ResourcePool_r16_t* get_resource_pool(NR_UE_MAC_INST_t *mac, uint16_t pool_id);

bool slot_has_psfch(NR_UE_MAC_INST_t *mac, BIT_STRING_t *phy_sl_bitmap, uint64_t abs_index_cur_slot, uint8_t psfch_period, size_t phy_sl_map_size, NR_TDD_UL_DL_ConfigCommon_t *conf);
size_t sl_abs_slot_to_bit_pos(uint64_t abs_slot, size_t phy_map_sz);
bool sl_slot_carries_psfch(const BIT_STRING_t *phy_sl_bitmap, size_t phy_map_sz, uint64_t abs_slot, uint8_t psfch_period);
int64_t get_feedback_abs_slot(const BIT_STRING_t *phy_sl_bitmap, size_t phy_map_sz, uint64_t tx_abs_slot, uint8_t min_time_gap, uint8_t psfch_period);
int sl_psfch_pssch_slot_index(const BIT_STRING_t *phy_sl_bitmap, size_t phy_map_sz, uint64_t abs_slot,
                              uint8_t min_time_gap, uint8_t psfch_period);
int32_t nr_sl_psfch_feedback_offset(const NR_UE_MAC_INST_t *mac, uint64_t pssch_abs_slot);



int get_physical_sl_pool(NR_UE_MAC_INST_t *mac, BIT_STRING_t *sl_time_rsrc, BIT_STRING_t *phy_sl_bitmap);

seq_arr_t* get_candidate_resources_from_slots(frameslot_t *sfn,
                                          uint8_t psfch_period,
                                          uint8_t min_time_gap_psfch,
                                          uint16_t l_subch,
                                          uint16_t total_subch,
                                          seq_arr_t* slot_info,
                                          uint8_t mu);

seq_arr_t exclude_reserved_resources(sensing_data_t *sensed_data,
                                  float slot_period_ms,
                                  uint16_t resv_period_slots,
                                  uint16_t t1,
                                  uint16_t t2,
                                  uint8_t mu);

void exclude_resources_based_on_history(frameslot_t frame_slot,
                                        seq_arr_t* transmit_history,
                                        seq_arr_t* candidate_resources,
                                        seq_arr_t* sl_rsrc_rsrv_period_list,
                                        uint8_t mu);

bool overlapped_resource(uint8_t first_start,
                         uint8_t first_length,
                         uint8_t second_start,
                         uint8_t second_length);

uint8_t get_random_reselection_counter(uint16_t rri);

uint32_t compute_TRIV(uint8_t N, uint8_t t1, uint8_t t2);
uint32_t compute_FRIV(uint8_t sl_max_num_per_reserve,
                      uint8_t L_sub_chan,
                      uint8_t n_start_subch1,
                      uint8_t n_start_subch2,
                      uint8_t N_sl_subch);

void nr_ue_get_sdu_mac_ce_pre(NR_UE_MAC_INST_t *mac,
                                     frame_t frame,
                                     slot_t slot,
                                     uint8_t *ulsch_buffer,
                                     uint32_t buflen,
                                     uint32_t *LCG_bytes,
                                     NR_UE_MAC_CE_INFO *mac_ce_p,
                                     int tx_power,
                                     int P_CMAX);

#endif
/** @}*/
