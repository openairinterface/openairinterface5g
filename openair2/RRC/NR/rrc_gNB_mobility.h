/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef RRC_GNB_MOBILITY_H_
#define RRC_GNB_MOBILITY_H_

#include <stdint.h>
#include "common/utils/ds/byte_array.h"
#include "nr_rrc_defs.h"

RB_PROTOTYPE(rrc_xn_cand_tree, rrc_xn_candidate_s, entry, rrc_xn_candidate_cmp);

/* forward declarations */
typedef struct gNB_RRC_INST_s gNB_RRC_INST;
typedef struct gNB_RRC_UE_s gNB_RRC_UE_t;

typedef struct NR_CellGroupConfig NR_CellGroupConfig_t;

typedef void (*ho_cancel_t)(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue);
typedef int (*ho_status_transfer_t)(gNB_RRC_INST *rrc,
                                    gNB_RRC_UE_t *UE,
                                    const int n_to_mod,
                                    const int *drb_ids,
                                    const e1_pdcp_status_info_t *pdcp_status);

typedef struct nr_ho_source_cu {
  /// pointer to the (source) cell container
  const nr_rrc_cell_container_t *cell;
  /// (source) DU UE ID; in F1, the DU UE ID will change to the new (target) DU
  /// UE ID during handover, and the CU needs to keep track of this in case of
  /// reestablishment
  uint32_t du_ue_id;
  /// old (source) RNTI (to recognize a UE during reestablishment)
  rnti_t old_rnti;
  /// old (source) CellGroupConfig (to send the cellGroupConfig in case of
  /// reestablishment)
  byte_array_t old_cgc;
  /// NR Cell Identity of the target cell
  uint64_t target_nci;
  /// function pointer to announce the handover cancellation, e.g.,
  /// reestablishment
  ho_cancel_t ho_cancel;
  /// status transfer
  ho_status_transfer_t ho_status_transfer;
  /// old (source) downlink tunnel
  gtpu_tunnel_t old_du_tunnel_config;
} nr_ho_source_cu_t;

/* acknowledgement of handover request. buf+len is the RRC Reconfiguration */
typedef void (*ho_req_ack_t)(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue);
typedef void (*ho_success_t)(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue);
typedef void (*ho_failure_t)(gNB_RRC_INST *rrc, uint32_t gnb_ue_id, ngap_handover_failure_t *msg);
typedef void (*ho_trigger_t)(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue);

typedef struct nr_ho_target_cu {
  /// pointer to the (target) cell container
  const nr_rrc_cell_container_t *cell;
  /// (target) DU UE ID; the (source) DU UE ID will change to the new (target)
  /// DU UE ID after sending a reconfiguration, which is later than when
  /// receiving this ID
  uint32_t du_ue_id;
  /// new (target) RNTI (as for du_ue_id)
  rnti_t new_rnti;
  /// Handover Preparation Buffer
  byte_array_t ue_ho_prep_info;
  /// function pointer to trigger handover on target gNB
  ho_trigger_t ho_trigger;
  /// function pointer to announce handover request acknowledgment
  ho_req_ack_t ho_req_ack;
  /// function pointer to announce handover success
  ho_success_t ho_success;
  /// function pointer to announce the handover failure
  ho_failure_t ho_failure;
} nr_ho_target_cu_t;

typedef struct nr_handover_context_s {
  nr_ho_source_cu_t *source;
  nr_ho_target_cu_t *target;
} nr_handover_context_t;

typedef enum { HO_CTX_BOTH, HO_CTX_SOURCE, HO_CTX_TARGET } ho_ctx_type_t;
nr_handover_context_t *alloc_ho_ctx(ho_ctx_type_t type);

void nr_rrc_trigger_f1_ho(gNB_RRC_INST *rrc,
                          gNB_RRC_UE_t *ue,
                          const nr_rrc_cell_container_t *source_cell,
                          const nr_rrc_cell_container_t *target_cell);
void nr_rrc_finalize_ho(gNB_RRC_UE_t *ue);
void nr_rrc_n2_ho_failure(gNB_RRC_INST *rrc, uint32_t gnb_ue_id, ngap_handover_failure_t *msg);

void nr_rrc_trigger_n2_ho(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, const nr_neighbour_cell_t *neighbour_config);

void rrc_gNB_trigger_reconfiguration_for_handover(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, uint8_t *rrc_reconf, int rrc_reconf_len);

void nr_rrc_trigger_n2_ho_target(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue);

byte_array_t *get_meas_timing_config(const NR_MeasurementTimingConfiguration_t *mtc, const NR_MeasConfig_t *measConfig);

void nr_rrc_apply_target_context(gNB_RRC_UE_t *UE);

bool nr_rrc_update_cell_assoc_after_ho(gNB_RRC_UE_t *UE);

void rrc_add_xn_candidate(gNB_RRC_INST *rrc, uint32_t gnb_id, sctp_assoc_t assoc_id);
void rrc_remove_xn_candidate(gNB_RRC_INST *rrc, uint32_t gnb_id);

const nr_neighbour_cell_t *get_neighbour_cell_by_pci(const neighbour_cell_configuration_t *cell, int pci);
const nr_neighbour_cell_t *get_neighbour_cell_by_cell_id(const neighbour_cell_configuration_t *cell, uint64_t nrcell_id);
void nr_HO_F1_trigger_telnet(gNB_RRC_INST *rrc, uint32_t rrc_ue_id);
void nr_HO_F1_trigger_telnet_pci(gNB_RRC_INST *rrc, uint32_t target_pci, uint32_t rrc_ue_id);
void nr_HO_N2_trigger_telnet(gNB_RRC_INST *rrc, uint32_t neighbour_pci, uint32_t rrc_ue_id);

#endif /* RRC_GNB_MOBILITY_H_ */
