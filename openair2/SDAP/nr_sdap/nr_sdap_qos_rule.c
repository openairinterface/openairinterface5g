/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_sdap_qos_rule.h"

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <stdlib.h>
#include <string.h>

#include "common/utils/LOG/log.h"
#include "common/utils/alg/find.h"
#include "nr_sdap_entity.h"

#define MAX_NUM_SDAP_QOS_RULES 64

typedef struct {
  uint8_t qfi;
  uint8_t rule_id;
  uint8_t precedence;
  bool is_default;
  seq_arr_t packet_filters;
} nr_sdap_qos_rule_t;

static void free_qos_rule(void *ptr)
{
  nr_sdap_qos_rule_t *rule = ptr;
  seq_arr_free(&rule->packet_filters, NULL);
}

void nr_sdap_qos_rules_init(nr_sdap_entity_t *entity)
{
  seq_arr_init(&entity->qos_rules, sizeof(nr_sdap_qos_rule_t));
  pthread_mutex_init(&entity->qos_rules_lock, NULL);
  entity->use_packet_filters = false;
}

void nr_sdap_qos_rules_free(nr_sdap_entity_t *entity)
{
  pthread_mutex_destroy(&entity->qos_rules_lock);
  seq_arr_free(&entity->qos_rules, free_qos_rule);
}

static bool eq_rule_id(const void *value, const void *it)
{
  return *(const uint8_t *)value == ((const nr_sdap_qos_rule_t *)it)->rule_id;
}

static nr_sdap_qos_rule_t *find_qos_rule(seq_arr_t *rules, uint8_t rule_id)
{
  elm_arr_t found = find_if(rules, &rule_id, eq_rule_id);
  return found.found ? found.it : NULL;
}

static bool eq_pf_id(const void *value, const void *it)
{
  return *(const uint8_t *)value == ((const packet_filter_decoded_t *)it)->pf_id;
}

static packet_filter_decoded_t *find_packet_filter(seq_arr_t *filters, uint8_t pf_id)
{
  elm_arr_t found = find_if(filters, &pf_id, eq_pf_id);
  return found.found ? found.it : NULL;
}

static int compare_precedence(const void *a, const void *b)
{
  const nr_sdap_qos_rule_t *rule_a = a;
  const nr_sdap_qos_rule_t *rule_b = b;
  return rule_a->precedence - rule_b->precedence;
}

static void add_packet_filters(seq_arr_t *filters, const packet_filter_decoded_t *pf_list, int num_pf)
{
  DevAssert(num_pf == 0 || pf_list != NULL);
  for (int i = 0; i < num_pf; ++i)
    seq_arr_push_back(filters, (void *)&pf_list[i], sizeof(packet_filter_decoded_t));
}

static bool ipv4_match(struct in_addr pkt_addr, struct in_addr filter_addr, struct in_addr mask)
{
  return (pkt_addr.s_addr & mask.s_addr) == (filter_addr.s_addr & mask.s_addr);
}

static bool ipv6_match(const struct in6_addr *pkt_addr, const struct in6_addr *filter_addr, uint8_t prefix_len)
{
  uint8_t bytes = prefix_len / 8;
  uint8_t bits = prefix_len % 8;

  if (memcmp(pkt_addr, filter_addr, bytes) != 0)
    return false;

  if (bits > 0) {
    uint8_t mask = 0xFF << (8 - bits);
    if ((pkt_addr->s6_addr[bytes] & mask) != (filter_addr->s6_addr[bytes] & mask))
      return false;
  }

  return true;
}

static bool packet_filter_match(const packet_filter_decoded_t *pf, const uint8_t *ip_pkt, size_t pkt_len)
{
  if (pf->num_components == 0 || pkt_len < 20)
    return false;

  uint8_t ip_version = (ip_pkt[0] >> 4) & 0x0F;
  struct iphdr ip4_hdr;
  struct ip6_hdr ip6_hdr;
  struct iphdr *ip4 = NULL;
  struct ip6_hdr *ip6 = NULL;
  uint8_t protocol = 0;
  uint16_t src_port = 0;
  uint16_t dst_port = 0;
  const uint8_t *transport_hdr = NULL;

  if (ip_version == 4) {
    uint8_t ihl = ip_pkt[0] & 0x0F;
    size_t header_len = (size_t)ihl * 4;
    if (ihl < 5 || header_len > pkt_len)
      return false;

    memcpy(&ip4_hdr, ip_pkt, sizeof(ip4_hdr));
    ip4 = &ip4_hdr;
    protocol = ip4->protocol;
    transport_hdr = ip_pkt + header_len;
  } else if (ip_version == 6) {
    if (pkt_len < sizeof(ip6_hdr))
      return false;

    memcpy(&ip6_hdr, ip_pkt, sizeof(ip6_hdr));
    ip6 = &ip6_hdr;
    protocol = ip6->ip6_nxt;
    transport_hdr = ip_pkt + sizeof(ip6_hdr);
  } else {
    return false;
  }

  if ((protocol == IPPROTO_TCP || protocol == IPPROTO_UDP) && transport_hdr + 4 <= ip_pkt + pkt_len) {
    uint16_t src_port_be;
    uint16_t dst_port_be;
    memcpy(&src_port_be, transport_hdr, sizeof(src_port_be));
    memcpy(&dst_port_be, transport_hdr + 2, sizeof(dst_port_be));
    src_port = ntohs(src_port_be);
    dst_port = ntohs(dst_port_be);
  }

  const bool ul = pf->direction == PF_DIR_UPLINK || pf->direction == PF_DIR_BIDIRECTIONAL;

  for (int i = 0; i < pf->num_components; ++i) {
    const packet_filter_component_t *comp = &pf->components[i];
    bool match = false;

    switch (comp->type) {
      case PF_COMP_MATCH_ALL:
        match = true;
        break;
      case PF_COMP_IPV4_REMOTE_ADDR:
        if (ip4 && ul)
          match = ipv4_match(*(struct in_addr *)&ip4->daddr, comp->value.ipv4.addr, comp->value.ipv4.mask);
        else if (ip4)
          match = ipv4_match(*(struct in_addr *)&ip4->saddr, comp->value.ipv4.addr, comp->value.ipv4.mask);
        break;
      case PF_COMP_IPV4_LOCAL_ADDR:
        if (ip4 && ul)
          match = ipv4_match(*(struct in_addr *)&ip4->saddr, comp->value.ipv4.addr, comp->value.ipv4.mask);
        else if (ip4)
          match = ipv4_match(*(struct in_addr *)&ip4->daddr, comp->value.ipv4.addr, comp->value.ipv4.mask);
        break;
      case PF_COMP_IPV6_REMOTE_ADDR_PREFIX:
        if (ip6 && ul)
          match = ipv6_match(&ip6->ip6_dst, &comp->value.ipv6.addr, comp->value.ipv6.prefix_len);
        else if (ip6)
          match = ipv6_match(&ip6->ip6_src, &comp->value.ipv6.addr, comp->value.ipv6.prefix_len);
        break;
      case PF_COMP_IPV6_LOCAL_ADDR_PREFIX:
        if (ip6 && ul)
          match = ipv6_match(&ip6->ip6_src, &comp->value.ipv6.addr, comp->value.ipv6.prefix_len);
        else if (ip6)
          match = ipv6_match(&ip6->ip6_dst, &comp->value.ipv6.addr, comp->value.ipv6.prefix_len);
        break;
      case PF_COMP_PROTOCOL_ID_NEXT_HDR:
        match = protocol == comp->value.protocol;
        break;
      case PF_COMP_SINGLE_REMOTE_PORT:
        match = ul ? dst_port == comp->value.single_port : src_port == comp->value.single_port;
        break;
      case PF_COMP_SINGLE_LOCAL_PORT:
        match = ul ? src_port == comp->value.single_port : dst_port == comp->value.single_port;
        break;
      case PF_COMP_REMOTE_PORT_RANGE:
        match = ul ? dst_port >= comp->value.port_range.port_low && dst_port <= comp->value.port_range.port_high
                   : src_port >= comp->value.port_range.port_low && src_port <= comp->value.port_range.port_high;
        break;
      case PF_COMP_LOCAL_PORT_RANGE:
        match = ul ? src_port >= comp->value.port_range.port_low && src_port <= comp->value.port_range.port_high
                   : dst_port >= comp->value.port_range.port_low && dst_port <= comp->value.port_range.port_high;
        break;
      default:
        LOG_W(SDAP, "Packet filter %d: matching failed for component type 0x%02x\n", pf->pf_id, comp->type);
        break;
    }

    if (!match)
      return false;
  }

  return true;
}

uint8_t nr_sdap_match_ul_packet(nr_sdap_entity_t *entity, const uint8_t *ip_pkt, size_t pkt_len)
{
  pthread_mutex_lock(&entity->qos_rules_lock);

  FOR_EACH_SEQ_ARR (nr_sdap_qos_rule_t *, rule, &entity->qos_rules) {
    if (seq_arr_size(&rule->packet_filters) == 0 || rule->is_default)
      continue;

    FOR_EACH_SEQ_ARR (packet_filter_decoded_t *, filter, &rule->packet_filters) {
      if (filter->direction != PF_DIR_UPLINK && filter->direction != PF_DIR_BIDIRECTIONAL)
        continue;
      if (packet_filter_match(filter, ip_pkt, pkt_len)) {
        uint8_t qfi = rule->qfi;
        LOG_D(SDAP,
              "UE %lu PDU session %d: UL packet matched QFI %d (rule %d, filter %d)\n",
              entity->tun.ue_id,
              entity->tun.pdusession_id,
              qfi,
              rule->rule_id,
              filter->pf_id);
        pthread_mutex_unlock(&entity->qos_rules_lock);
        return qfi;
      }
    }
  }

  uint8_t default_qfi = entity->qfi;
  LOG_D(SDAP,
        "UE %lu PDU session %d: UL packet did not match any filter, using default QFI %d\n",
        entity->tun.ue_id,
        entity->tun.pdusession_id,
        default_qfi);
  pthread_mutex_unlock(&entity->qos_rules_lock);
  return default_qfi;
}

void nr_sdap_qos_rule_add(ue_id_t ue_id,
                          int pdusession_id,
                          uint8_t rule_id,
                          uint8_t qfi,
                          uint8_t precedence,
                          bool is_default,
                          const packet_filter_decoded_t *pf_list,
                          int num_pf)
{
  nr_sdap_entity_t *entity = nr_sdap_get_entity(ue_id, pdusession_id);
  if (entity == NULL || entity->tun.is_gnb) {
    LOG_E(SDAP, "UE %ld PDU session %d: no UE entity for QoS rule add\n", ue_id, pdusession_id);
    return;
  }

  nr_sdap_qos_rule_t rule = {.qfi = qfi, .rule_id = rule_id, .precedence = precedence, .is_default = is_default};
  seq_arr_init(&rule.packet_filters, sizeof(packet_filter_decoded_t));
  add_packet_filters(&rule.packet_filters, pf_list, num_pf);

  pthread_mutex_lock(&entity->qos_rules_lock);
  if (seq_arr_size(&entity->qos_rules) >= MAX_NUM_SDAP_QOS_RULES) {
    pthread_mutex_unlock(&entity->qos_rules_lock);
    free_qos_rule(&rule);
    LOG_E(SDAP,
          "UE %ld PDU session %d: Cannot add QoS rule %d: maximum of %d rules reached\n",
          ue_id,
          pdusession_id,
          rule_id,
          MAX_NUM_SDAP_QOS_RULES);
    return;
  }
  seq_arr_push_back(&entity->qos_rules, &rule, sizeof(rule));
  qsort(entity->qos_rules.data, seq_arr_size(&entity->qos_rules), sizeof(nr_sdap_qos_rule_t), compare_precedence);
  if (is_default)
    entity->qfi = qfi;

  entity->use_packet_filters = true;
  pthread_mutex_unlock(&entity->qos_rules_lock);
  LOG_I(SDAP,
        "UE %ld PDU session %d: Added QoS rule %d (QFI %d, precedence %d, %d filters%s)\n",
        ue_id,
        pdusession_id,
        rule_id,
        qfi,
        precedence,
        num_pf,
        is_default ? " (default)" : "");
}

void nr_sdap_qos_rule_remove(ue_id_t ue_id, int pdusession_id, uint8_t rule_id)
{
  nr_sdap_entity_t *entity = nr_sdap_get_entity(ue_id, pdusession_id);
  if (entity == NULL || entity->tun.is_gnb) {
    LOG_E(SDAP, "UE %ld PDU session %d: no UE entity for QoS rule remove\n", ue_id, pdusession_id);
    return;
  }

  pthread_mutex_lock(&entity->qos_rules_lock);
  nr_sdap_qos_rule_t *rule = find_qos_rule(&entity->qos_rules, rule_id);
  if (rule != NULL) {
    LOG_I(SDAP, "UE %ld PDU session %d: Removing QoS rule %d (QFI %d)\n", ue_id, pdusession_id, rule_id, rule->qfi);
    seq_arr_erase_deep(&entity->qos_rules, rule, free_qos_rule);
  } else {
    LOG_W(SDAP, "UE %ld PDU session %d: QoS rule %d not found for removal\n", ue_id, pdusession_id, rule_id);
  }
  if (seq_arr_size(&entity->qos_rules) == 0) {
    entity->use_packet_filters = false;
    LOG_I(SDAP, "UE %ld PDU session %d: Disabled UL packet filter matching (no rules)\n", ue_id, pdusession_id);
  }
  pthread_mutex_unlock(&entity->qos_rules_lock);
}

void nr_sdap_qos_rule_update(ue_id_t ue_id,
                             int pdusession_id,
                             uint8_t rule_id,
                             uint8_t qfi,
                             uint8_t precedence,
                             bool is_default,
                             const packet_filter_decoded_t *pf_list,
                             int num_pf,
                             bool replace)
{
  nr_sdap_entity_t *entity = nr_sdap_get_entity(ue_id, pdusession_id);
  if (entity == NULL || entity->tun.is_gnb) {
    LOG_E(SDAP, "UE %ld PDU session %d: no UE entity for QoS rule update\n", ue_id, pdusession_id);
    return;
  }

  pthread_mutex_lock(&entity->qos_rules_lock);
  nr_sdap_qos_rule_t *rule = find_qos_rule(&entity->qos_rules, rule_id);
  if (rule == NULL) {
    LOG_W(SDAP, "UE %ld PDU session %d: QoS rule %d not found for update\n", ue_id, pdusession_id, rule_id);
    pthread_mutex_unlock(&entity->qos_rules_lock);
    return;
  }

  rule->qfi = qfi;
  rule->precedence = precedence;
  rule->is_default = is_default;
  if (replace) {
    seq_arr_free(&rule->packet_filters, NULL);
    seq_arr_init(&rule->packet_filters, sizeof(packet_filter_decoded_t));
    add_packet_filters(&rule->packet_filters, pf_list, num_pf);
    LOG_I(SDAP,
          "UE %ld PDU session %d: Replaced packet filters for QoS rule %d (QFI %d) - now %zu filters\n",
          ue_id,
          pdusession_id,
          rule_id,
          rule->qfi,
          seq_arr_size(&rule->packet_filters));
  } else {
    size_t old_size = seq_arr_size(&rule->packet_filters);
    add_packet_filters(&rule->packet_filters, pf_list, num_pf);
    LOG_I(SDAP,
          "UE %ld PDU session %d: Added %zu packet filters to QoS rule %d (QFI %d) - now %zu filters\n",
          ue_id,
          pdusession_id,
          seq_arr_size(&rule->packet_filters) - old_size,
          rule_id,
          rule->qfi,
          seq_arr_size(&rule->packet_filters));
  }
  if (is_default)
    entity->qfi = qfi;
  qsort(entity->qos_rules.data, seq_arr_size(&entity->qos_rules), sizeof(nr_sdap_qos_rule_t), compare_precedence);
  pthread_mutex_unlock(&entity->qos_rules_lock);
}

void nr_sdap_qos_rule_delete_pf(ue_id_t ue_id, int pdusession_id, uint8_t rule_id, const uint8_t *pf_ids, int num_ids)
{
  nr_sdap_entity_t *entity = nr_sdap_get_entity(ue_id, pdusession_id);
  if (entity == NULL || entity->tun.is_gnb) {
    LOG_E(SDAP, "UE %ld PDU session %d: no UE entity for QoS rule packet filter deletion\n", ue_id, pdusession_id);
    return;
  }

  pthread_mutex_lock(&entity->qos_rules_lock);
  nr_sdap_qos_rule_t *rule = find_qos_rule(&entity->qos_rules, rule_id);
  if (rule == NULL) {
    LOG_W(SDAP, "UE %ld PDU session %d: QoS rule %d not found for packet filter deletion\n", ue_id, pdusession_id, rule_id);
    pthread_mutex_unlock(&entity->qos_rules_lock);
    return;
  }

  int removed = 0;
  for (int i = 0; i < num_ids; ++i) {
    packet_filter_decoded_t *filter = find_packet_filter(&rule->packet_filters, pf_ids[i]);
    if (filter != NULL) {
      seq_arr_erase(&rule->packet_filters, filter);
      removed++;
    }
  }
  LOG_I(SDAP,
        "UE %ld PDU session %d: Deleted %d/%d packet filters from QoS rule %d (QFI %d) - now %zu filters\n",
        ue_id,
        pdusession_id,
        removed,
        num_ids,
        rule_id,
        rule->qfi,
        seq_arr_size(&rule->packet_filters));
  pthread_mutex_unlock(&entity->qos_rules_lock);
}
