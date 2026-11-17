/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_SDAP_QOS_RULE_H_
#define NR_SDAP_QOS_RULE_H_

#include <stdbool.h>
#include <stdint.h>

#include "common/5g_packet_filter.h"
#include "common/platform_types.h"

struct nr_sdap_entity_s;

void nr_sdap_qos_rules_init(struct nr_sdap_entity_s *entity);
void nr_sdap_qos_rules_free(struct nr_sdap_entity_s *entity);
uint8_t nr_sdap_match_ul_packet(struct nr_sdap_entity_s *entity, const uint8_t *ip_pkt, size_t pkt_len);

/** @brief Add an Authorized QoS rule */
void nr_sdap_qos_rule_add(ue_id_t ue_id,
                          int pdusession_id,
                          uint8_t rule_id,
                          uint8_t qfi,
                          uint8_t precedence,
                          bool is_default,
                          const packet_filter_decoded_t *pf_list,
                          int num_pf);

/** @brief Remove an Authorized QoS rule */
void nr_sdap_qos_rule_remove(ue_id_t ue_id, int pdusession_id, uint8_t rule_id);

/** @brief Update an Authorized QoS rule's packet filters
 * @param replace If true, replace all packet filters; if false, add pf_list to the existing ones */
void nr_sdap_qos_rule_update(ue_id_t ue_id,
                             int pdusession_id,
                             uint8_t rule_id,
                             uint8_t qfi,
                             uint8_t precedence,
                             bool is_default,
                             const packet_filter_decoded_t *pf_list,
                             int num_pf,
                             bool replace);

/** @brief Remove specific packet filters (by ID) from an Authorized QoS rule */
void nr_sdap_qos_rule_delete_pf(ue_id_t ue_id, int pdusession_id, uint8_t rule_id, const uint8_t *pf_ids, int num_ids);

#endif
