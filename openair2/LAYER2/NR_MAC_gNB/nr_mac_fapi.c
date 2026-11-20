/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_mac_fapi.h"
#include "mac_proto.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"

int nr_mac_handle_slot_indication(const nfapi_nr_slot_indication_scf_t *ind, NR_Sched_Rsp_t *rsp)
{
  module_id_t mod_id = 0;
  int phy_id = ind->header.phy_id;
  reset_sched_response(rsp, ind->sfn, ind->slot, mod_id, phy_id);
  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t* cell = nr_mac_get_cell_by_phy_id(nrmac, ind->header.phy_id);
  start_meas(&cell->gNB_scheduler);
  gNB_dlsch_ulsch_scheduler(nrmac, cell, ind->sfn, ind->slot, rsp);
  stop_meas(&cell->gNB_scheduler);
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}

int nr_mac_handle_rach_indication(const nfapi_nr_rach_indication_t *rach_ind)
{
  if (rach_ind->number_of_pdus == 0)
    return 0;

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t* cell = nr_mac_get_cell_by_phy_id(nrmac, rach_ind->header.phy_id);
  for (const nfapi_nr_prach_indication_pdu_t *rach = rach_ind->pdu_list; rach < rach_ind->pdu_list + rach_ind->number_of_pdus; ++rach) {
    if (rach->num_preamble > 1) {
      LOG_E(MAC, "Not more than 1 preamble per RACH PDU supported, ignoring the rest\n");
    }
    nr_initiate_ra_proc(nrmac,
                        cell,
                        rach_ind->sfn,
                        rach_ind->slot,
                        rach->preamble_list[0].preamble_index,
                        rach->freq_index,
                        rach->symbol_index,
                        rach->preamble_list[0].timing_advance,
                        rach->preamble_list[0].preamble_pwr);
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}

int nr_mac_handle_uci_indication(const nfapi_nr_uci_indication_t *uci_ind)
{
  if (uci_ind->num_ucis == 0)
    return 0;

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(nrmac, uci_ind->header.phy_id);
  for (const nfapi_nr_uci_t *uci = uci_ind->uci_list; uci < uci_ind->uci_list + uci_ind->num_ucis; ++uci) {
    switch (uci->pdu_type) {
      case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
        LOG_E(MAC, "%s(): unhandled NFAPI_NR_UCI_PUSCH_PDU_TYPE\n", __func__);
        break;

      case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE: {
        const nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_pdu = &uci->pucch_pdu_format_0_1;
        handle_nr_uci_pucch_0_1(nrmac, cell, uci_ind->sfn, uci_ind->slot, uci_pdu);
        break;
      }

      case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE: {
        const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *uci_pdu = &uci->pucch_pdu_format_2_3_4;
        handle_nr_uci_pucch_2_3_4(nrmac, cell, uci_ind->sfn, uci_ind->slot, uci_pdu);
        break;
      }
    }
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}

int nr_mac_handle_crc_indication(const nfapi_nr_crc_indication_t *crc_ind)
{
  if (crc_ind->number_crcs == 0)
    return 0;

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(nrmac, crc_ind->header.phy_id);
  for (const nfapi_nr_crc_t *crc = crc_ind->crc_list; crc < crc_ind->crc_list + crc_ind->number_crcs; ++crc) {
    nr_rx_sdu_crc(nrmac, cell, crc_ind->sfn, crc_ind->slot, crc);
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}

int nr_mac_handle_rx_data_indication(const nfapi_nr_rx_data_indication_t *rx_ind)
{
  if (rx_ind->number_of_pdus == 0)
    return 0;

  /* RLC PDUs of all UEs, delivered once sched_lock is released; entries point
   * into the PDUs of rx_ind. */
  struct rlc_ind {
    nr_rlc_data_ind_t data[MAX_NUM_DATA_IND];
    int num_data;
    rnti_t rnti;
  } rlc_ind[64];
  DevAssert(rx_ind->number_of_pdus < 64);
  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(nrmac, rx_ind->header.phy_id);
  start_meas(&cell->rx_ulsch_sdu);
  for (int i = 0; i < rx_ind->number_of_pdus; ++i) {
    const nfapi_nr_rx_data_pdu_t *rx = &rx_ind->pdu_list[i];
    struct rlc_ind *rlci = &rlc_ind[i];
    rlci->num_data = 0; // initialize to "no data"
    rlci->rnti = nr_rx_sdu_rx(nrmac, cell, rx_ind->sfn, rx_ind->slot, rx, rlci->data, &rlci->num_data);
  }
  stop_meas(&cell->rx_ulsch_sdu);
  NR_SCHED_UNLOCK(&nrmac->sched_lock);

  // RLC has its own lock: deliver outside sched_lock, one call per UE
  for (int i = 0; i < rx_ind->number_of_pdus; ++i) {
    struct rlc_ind *rlci = &rlc_ind[i];
    nr_mac_rlc_data_ind(nrmac->Mod_id, rlci->rnti, true, rlci->data, rlci->num_data);
  }
  return 0;
}

int nr_mac_handle_srs_indication(const nfapi_nr_srs_indication_t *srs_ind)
{
  if (srs_ind->number_of_pdus == 0)
    return 0;

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  nr_cell_sched_t *cell = nr_mac_get_cell_by_phy_id(nrmac, srs_ind->header.phy_id);
  for (const nfapi_nr_srs_indication_pdu_t *srs = srs_ind->pdu_list; srs < srs_ind->pdu_list + srs_ind->number_of_pdus; ++srs) {
    handle_nr_srs_measurements(nrmac, cell, srs_ind->sfn, srs_ind->slot, srs);
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}

int nr_mac_handle_srs_toa_vendor_ext(const nfapi_nr_srs_toa_vendor_ext_indication_t *srs_ind)
{
  if (srs_ind->number_of_pdus == 0)
    return 0;

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  for (const nfapi_nr_srs_toa_vendor_ext_pdu_t *srs = srs_ind->pdu_list; srs < srs_ind->pdu_list + srs_ind->number_of_pdus; ++srs) {
    handle_nr_srs_toa_vendor_ext_measurements(nrmac, srs_ind->sfn, srs_ind->slot, srs->num_ta, srs->ta_offset_nsec, srs->rnti);
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  return 0;
}
