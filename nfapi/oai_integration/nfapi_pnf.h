/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NFAPI_PNF_H_
#define NFAPI_PNF_H_

extern nfapi_ue_release_request_body_t release_rntis;
int oai_nfapi_rach_ind(nfapi_rach_indication_t *rach_ind);
void configure_nfapi_pnf(char *vnf_ip_addr, int vnf_p5_port, char *pnf_ip_addr, int pnf_p7_port, int vnf_p7_port);
void configure_nr_nfapi_pnf(char *vnf_ip_addr, int vnf_p5_port, char *pnf_ip_addr, int pnf_p7_port, int vnf_p7_port);

void oai_subframe_ind(uint16_t sfn, uint16_t sf);
struct NR_Sched_Rsp;
void handle_nr_slot_ind(const nfapi_nr_slot_indication_scf_t *ind, struct NR_Sched_Rsp *resp);
void sfnslot_add_slot(int mu, uint16_t *sfn, uint16_t *slot, int offset);

int oai_nfapi_nr_slot_indication(const nfapi_nr_slot_indication_scf_t *ind);
int oai_nfapi_nr_rx_data_indication(const nfapi_nr_rx_data_indication_t *ind);
int oai_nfapi_nr_crc_indication(const nfapi_nr_crc_indication_t *ind);
int oai_nfapi_nr_srs_indication(const nfapi_nr_srs_indication_t *ind);
int oai_nfapi_nr_srs_toa_vendor_ext_indication(const nfapi_nr_srs_toa_vendor_ext_indication_t *ind);
int oai_nfapi_nr_uci_indication(const nfapi_nr_uci_indication_t *ind);
int oai_nfapi_nr_rach_indication(const nfapi_nr_rach_indication_t *ind);
void stop_nr_nfapi_pnf();
#endif /* NFAPI_PNF_H_ */
