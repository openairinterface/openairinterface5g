/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_mac_gNB.h"
#include "mac_proto.h"
#include "nr_mac_cell.h"
#ifdef E3_AGENT
#include "openair2/E3AP/ran_func_spectrum_extern.h"
#endif /* E3_AGENT */

int nr_mac_get_cell_idx(gNB_MAC_INST *mac, nr_cell_sched_t *cell)
{
  int cell_idx = cell - nr_mac_get_cell_by_idx(mac, 0);
  AssertFatal(cell_idx >= 0 && cell_idx < seq_arr_size(&mac->cells), "Invalid cell_idx %d (only %zu cells configured)\n", cell_idx, seq_arr_size(&mac->cells));
  return cell_idx;
}

int nr_mac_get_cell_phy_id(gNB_MAC_INST *mac, nr_cell_sched_t *cell)
{
  return nr_mac_get_cell_idx(mac, cell);
}

nr_cell_sched_t *nr_mac_get_cell_by_idx(gNB_MAC_INST *mac, uint16_t idx)
{
  AssertFatal(idx < seq_arr_size(&mac->cells), "Invalid idx %d (only %zu cells configured)\n", idx, seq_arr_size(&mac->cells));
  return (nr_cell_sched_t *)seq_arr_at(&mac->cells, idx);
}

nr_cell_sched_t *nr_mac_get_cell_by_phy_id(gNB_MAC_INST *mac, uint16_t phy_id)
{
  return nr_mac_get_cell_by_idx(mac, phy_id);
}

//TS 38.473 §9.3.1.12 NR CGI defined by PLMN Identity and NR Cell Identity
nr_cell_sched_t *nr_mac_get_cell_by_cgi(gNB_MAC_INST *mac, plmn_id_t plmn, uint64_t nr_cellid)
{
  FOR_EACH_SEQ_ARR(nr_cell_sched_t *, cell, &mac->cells) {
    if (cell->nr_cellid == nr_cellid && cell->plmn.mcc == plmn.mcc && cell->plmn.mnc == plmn.mnc
        && cell->plmn.mnc_digit_length == plmn.mnc_digit_length)
      return cell;
  }
  return NULL;
}

static void destroy_periodic_sched(periodic_ue_sched_t p)
{
  free(p.list);
}

void seq_arr_cell_free(void *cell)
{
  nr_mac_cell_free(cell);
}

void nr_mac_cell_free(nr_cell_sched_t *cell)
{
  free(cell->radio_config.ssb_beams);
  NR_COMMON_channels_t *cc = &cell->common_channels;
  nr_mac_pcch_queue_free(cc);
  ASN_STRUCT_FREE(asn_DEF_NR_BCCH_BCH_Message, cc->mib);
  ASN_STRUCT_FREE(asn_DEF_NR_BCCH_DL_SCH_Message, cc->sib1);
  ASN_STRUCT_FREE(asn_DEF_NR_ServingCellConfigCommon, cc->ServingCellConfigCommon);
#ifdef E3_AGENT
  e3_spectrum_mac_detach_cell(cell);
#endif /* E3_AGENT */
  destroy_periodic_sched(cell->periodic_srs_config);
}
