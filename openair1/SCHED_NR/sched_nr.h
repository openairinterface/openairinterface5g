/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __openair_SCHED_NR_DEFS_H__
#define __openair_SCHED_NR_DEFS_H__

#include "PHY/defs_gNB.h"
#include "PHY/NR_TRANSPORT/nr_dci.h"
#include "phy_frame_config_nr.h"

void nr_set_ssb_first_subcarrier(nfapi_nr_config_request_scf_t *cfg, NR_DL_FRAME_PARMS *fp);
void phy_procedures_gNB_TX(PHY_VARS_gNB *gNB,
                           const nfapi_nr_dl_tti_request_t *DL_req,
                           const nfapi_nr_tx_data_request_t *TX_req,
                           const nfapi_nr_ul_dci_request_t *UL_dci_req,
                           int frame,
                           int slot);
void nr_save_ul_tti_req(PHY_VARS_gNB *gNB, nfapi_nr_ul_tti_request_t *UL_tti_req);
/* Fills the indications for UL slot frame_rx.slot_rx. The caller sets each indication's PDU list, with room for
 * MAX_NUM_NR_UCI_PDUS (UCI), MAX_UL_PDUS_PER_SLOT (CRC, RX data), and MAX_NUM_NR_SRS_PDUS (SRS, SRS ToA) entries. */
int phy_procedures_gNB_uespec_RX(PHY_VARS_gNB *gNB,
                                 int frame_rx,
                                 int slot_rx,
                                 nfapi_nr_uci_indication_t *uci_ind,
                                 nfapi_nr_crc_indication_t *crc_ind,
                                 nfapi_nr_rx_data_indication_t *rx_ind,
                                 nfapi_nr_srs_indication_t *srs_ind,
                                 nfapi_nr_srs_toa_vendor_ext_indication_t *srs_toa_v_ext);
void L1_nr_prach_procedures(PHY_VARS_gNB *gNB, prach_item_t *prach_id, nfapi_nr_rach_indication_t *rach_ind);
void nr_common_signal_procedures(PHY_VARS_gNB *gNB,
                                 int frame,
                                 int slot,
                                 const nfapi_nr_dl_tti_ssb_pdu *ssb_pdu,
                                 int prb_mask_words);
void nr_feptx_ofdm(RU_t *ru,int frame_tx,int tti_tx);
void nr_feptx0(RU_t *ru,int tti_tx,int first_symbol, int num_symbols, int aa);
void nr_feptx_prec_control(RU_t *ru,int frame,int tti_tx);
void nr_fep_tp(RU_t *ru, int slot);
void nr_feptx_tp(RU_t *ru, int frame_tx, int slot);
void feptx_prec(RU_t *ru,int frame_tx,int tti_tx);
void nr_phy_init_RU(RU_t *ru);
void nr_phy_free_RU(RU_t *ru);
void clear_slot_beamid(PHY_VARS_gNB *gNB, int slot);
void beam_index_allocation(uint16_t fapi_beam_index,
                           int ant,
                           int num_ports,
                           int symbols_per_slot,
                           int slot,
                           uint16_t bitmap_symbols,
                           int num_ant_max,
                           int num_beams_period,
                           uint16_t **ant_beam_id_list);
uint16_t get_first_ant_idx(bool das, uint16_t num_ports_beams, uint16_t beam_id, uint16_t fapi_start_port);
#endif
