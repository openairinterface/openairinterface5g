/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef GNB_CONFIG_H_
#define GNB_CONFIG_H_
#include <stdint.h>
#include "RRC/NR/nr_rrc_defs.h"
#include "assertions.h"
#include "common/config/config_load_configmodule.h"
#include "common/ngran_types.h"
#include "f1ap_messages_types.h"
#include "intertask_interface.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

void RCconfig_verify(configmodule_interface_t *cfg, ngran_node_t node_type);
void RCconfig_nr_prs(void);
void RCconfig_NR_L1(void);
void RCconfig_nr_macrlc(configmodule_interface_t *cfg);
void NRRCConfig(void);

gNB_RRC_INST *RCconfig_NRRRC();
int RCconfig_NR_X2(MessageDef *msg_p, uint32_t i);
void wait_f1_setup_response(void);
f1ap_tdd_info_t read_tdd_config(const NR_ServingCellConfigCommon_t *scc);
f1ap_gnb_du_system_info_t *get_sys_info(NR_BCCH_BCH_Message_t *mib, const NR_BCCH_DL_SCH_Message_t *sib1, seq_arr_t *du_SIBs);
int gNB_app_handle_f1ap_gnb_cu_configuration_update(f1ap_gnb_cu_configuration_update_t *gnb_cu_cfg_update);
MessageDef *RCconfig_NR_CU_E1(const E1_t *entity);
ngran_node_t get_node_type(void);
void nfapi_stop_l1();
#ifdef E2_AGENT
#include "openair2/E2AP/e2_agent_arg.h"
e2_agent_args_t RCconfig_NR_E2agent(void);
#endif // E2_AGENT

#endif /* GNB_CONFIG_H_ */
/** @} */
