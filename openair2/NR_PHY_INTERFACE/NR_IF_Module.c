/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief data structures for PHY/MAC interface modules
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "NR_IF_Module.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_fapi.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"

#include "common/utils/LOG/log.h"

#include "common/platform_constants.h"
#include "utils.h"

#include "nfapi_interface.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "nfapi/oai_integration/nfapi_pnf.h"

#define MAX_IF_MODULES 100

static NR_IF_Module_t *nr_if_inst[MAX_IF_MODULES];

static int pnf_send_slot_ind(const nfapi_nr_slot_indication_scf_t *ind, NR_Sched_Rsp_t *rsp)
{
  module_id_t module_id = 0;
  int phy_id = 0;
  reset_sched_response(rsp, ind->sfn, ind->slot, module_id, phy_id);
  return handle_nr_slot_ind(ind, rsp);
}

NR_IF_Module_t *NR_IF_Module_init(int Mod_id) {
  AssertFatal(Mod_id<MAX_MODULES,"Asking for Module %d > %d\n",Mod_id,MAX_IF_MODULES);
  LOG_D(PHY, "Installing callbacks for IF_Module - UL_indication\n");

  if (nr_if_inst[Mod_id] == NULL)
    nr_if_inst[Mod_id] = calloc_or_fail(1, sizeof(*nr_if_inst[Mod_id]));

  NR_IF_Module_t *ifm = nr_if_inst[Mod_id];

  if (NFAPI_MODE == NFAPI_MODE_PNF) {
    // if this is a PNF, call into nFAPI
    ifm->nr_slot_indication = pnf_send_slot_ind;
    ifm->nr_rx_data_indication = oai_nfapi_nr_rx_data_indication;
    ifm->nr_crc_indication = oai_nfapi_nr_crc_indication;
    ifm->nr_srs_indication = oai_nfapi_nr_srs_indication;
    ifm->nr_srs_toa_vendor_ext_indication = oai_nfapi_nr_srs_toa_vendor_ext_indication;
    ifm->nr_uci_indication = oai_nfapi_nr_uci_indication;
    ifm->nr_rach_indication = oai_nfapi_nr_rach_indication;
  } else {
    // all other modes call directly into the scheduler
    ifm->nr_slot_indication = nr_mac_handle_slot_indication;
    ifm->nr_rx_data_indication = nr_mac_handle_rx_data_indication;
    ifm->nr_crc_indication = nr_mac_handle_crc_indication;
    ifm->nr_srs_indication = nr_mac_handle_srs_indication;
    ifm->nr_srs_toa_vendor_ext_indication = nr_mac_handle_srs_toa_vendor_ext;
    ifm->nr_uci_indication = nr_mac_handle_uci_indication;
    ifm->nr_rach_indication = nr_mac_handle_rach_indication;
  }

  return nr_if_inst[Mod_id];
}

void reset_sched_response(NR_Sched_Rsp_t *sched_response, int frame, int slot, int module_id, int phy_id)
{
  sched_response->module_id = module_id;
  sched_response->phy_id = phy_id;
  sched_response->frame = frame;
  sched_response->slot = slot;


  nfapi_nr_dl_tti_request_t *DL_req = &sched_response->DL_req;
  DL_req->SFN = frame;
  DL_req->Slot = slot;
  DL_req->dl_tti_request_body.nPDUs = 0;
  DL_req->dl_tti_request_body.nGroup = 0;

  nfapi_nr_ul_dci_request_t *UL_dci_req = &sched_response->UL_dci_req;
  UL_dci_req->SFN = frame;
  UL_dci_req->Slot = slot;
  UL_dci_req->numPdus = 0;

  nfapi_nr_ul_tti_request_t *UL_tti_req = &sched_response->UL_tti_req;
  UL_tti_req->SFN = frame;
  UL_tti_req->Slot = slot;
  UL_tti_req->n_pdus = 0;
  UL_tti_req->n_group = 0;

  nfapi_nr_tx_data_request_t *TX_req = &sched_response->TX_req;
  TX_req->SFN = frame;
  TX_req->Slot = slot;
  TX_req->Number_of_PDUs = 0;
}
