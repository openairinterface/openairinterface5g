/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* Xn handover test with N gNB instances and one UE stub: the UE is the real NR UE NAS
 * (as in nr-ue-nas-simulator), attached to the AMF through gNB0, without RRC/radio.
 * The stub gNB RRC of this file runs the real XnAP and NGAP procedures of the handovers:
 *   source: Handover Request -> (Handover Request Acknowledge) -> SN Status Transfer
 *   target: Handover Request Acknowledge, Path Switch Request (AMF), UE Context Release
 *   source: local release of the UE after UE Context Release
 * and hands the UE over round-robin gNB0 -> gNB1 -> ... -> gNB(N-1) -> gNB0 -> ...,
 * max(XN_HO_MIN, N) handovers. */

#include <arpa/inet.h>
#include <fcntl.h>
#include "common/utils/ocp_itti/intertask_interface.h"
#include "executables/nr-uesoftmodem.h"
#include "openair3/NAS/NR_UE/nr_nas_msg.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_qos_rule.h"
#include "openair3/NGAP/ngap_gNB_mobility_management.h"
#include "openair2/XNAP/lib/xnap_gNB_mobility_management.h"
#include "openair2/XNAP/xnap_ids.h"
#include "xn-ho-sim.h"

#define XN_HO_TIMEOUT_S 120
// minimum number of handovers, whatever the number of gNBs
#define XN_HO_MIN 5
#define DUMMY_CONTAINER_LEN 8

/* functions of the UE stack not used by the NAS of the UE stub */
int NB_UE_INST = 1;
nrUE_params_t *get_nrUE_params(void)
{
  static nrUE_params_t params = {0};
  return &params;
}
/* no TUN interface for the UE stub: no user plane, and the test runs without root.
 * The NAS requires a descriptor: /dev/null, never read as the UE stub RRC ignores NAS_TUN_REQ */
void nr_ue_tun_create_ip_if(nas_ue_pdu_tun_t *t, const char *ipv4, const char *ipv6, int ue_id, int pdu_session_id)
{
  if (t->sock < 0)
    t->sock = open("/dev/null", O_RDWR);
  UNUSED(ipv4);
  UNUSED(ipv6);
  UNUSED(ue_id);
  UNUSED(pdu_session_id);
}
void nr_ue_tun_create_eth_if(nas_ue_pdu_tun_t *t)
{
  if (t->sock < 0)
    t->sock = open("/dev/null", O_RDWR);
}
void nr_ue_tun_store_qfi(nas_ue_pdu_tun_t *t, uint8_t qfi)
{
  t->qfi = qfi;
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
  UNUSED(ue_id);
  UNUSED(pdusession_id);
  UNUSED(rule_id);
  UNUSED(qfi);
  UNUSED(precedence);
  UNUSED(is_default);
  UNUSED(pf_list);
  UNUSED(num_pf);
}
void nr_sdap_qos_rule_remove(ue_id_t ue_id, int pdusession_id, uint8_t rule_id)
{
  UNUSED(ue_id);
  UNUSED(pdusession_id);
  UNUSED(rule_id);
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
  UNUSED(ue_id);
  UNUSED(pdusession_id);
  UNUSED(rule_id);
  UNUSED(qfi);
  UNUSED(precedence);
  UNUSED(is_default);
  UNUSED(pf_list);
  UNUSED(num_pf);
  UNUSED(replace);
}
void nr_sdap_qos_rule_delete_pf(ue_id_t ue_id, int pdusession_id, uint8_t rule_id, const uint8_t *pf_ids, int num_ids)
{
  UNUSED(ue_id);
  UNUSED(pdusession_id);
  UNUSED(rule_id);
  UNUSED(pf_ids);
  UNUSED(num_ids);
}
int nr_rlc_get_available_tx_space(const rnti_t rntiP, const logical_chan_id_t channel_idP)
{
  UNUSED(rntiP);
  UNUSED(channel_idP);
  abort();
  return 0;
}

typedef struct {
  uint32_t gnb_id;
  uint32_t tac;
  plmn_id_t plmn;
  uint64_t cell_id;
  char ngu_ip[16];
  MessageDef *ng_register_req; // gNB0: at start, the others: once the UE is attached to gNB0
  sctp_assoc_t peer_assoc_id[XN_HO_MAX_GNB]; // Xn association to each other gNB, 0 before Xn Setup
  int nb_xn_peers;
} sim_gnb_t;

typedef struct {
  uint8_t id;
  pdu_session_type_t type;
  nssai_t nssai;
  gtpu_tunnel_t n3_incoming; // UPF side
  uint8_t nb_qos;
  pdusession_level_qos_parameter_t qos[MAX_QOS_FLOWS];
} sim_pdu_session_t;

/* the UE stub, as known by the gNBs */
typedef struct {
  int serving; // gNB instance
  uint32_t ue_id; // RRC UE ID = RAN UE NGAP ID at the serving gNB, unique in the process
  int target; // target gNB instance during a handover
  uint32_t target_ue_id; // at the target gNB during a handover
  uint64_t amf_ue_ngap_id;
  net_ip_address_t amf_ng_ip;
  nr_guami_t guami;
  ngap_security_capabilities_t sec_cap;
  ngap_ambr_t ambr;
  int nb_pdu;
  sim_pdu_session_t pdu[NR_MAX_NB_PDU_SESSIONS];
  uint32_t pdcp_count; // dummy PDCP COUNT for SN Status Transfer
} sim_ue_t;

static sim_gnb_t gnb[XN_HO_MAX_GNB];
static int nb_gnb = 0;
static sim_ue_t ue = {.ue_id = 1};
static uint32_t next_ue_id = 2;
static int nb_ho = 0; // handovers to do
static int nb_ho_done = 0;
static bool ho_started = false;
static bool gnbs_started = false;

void xn_ho_sim_add_gnb(int idx, MessageDef *ng_register_req, uint64_t cell_id, const char *ngu_ip)
{
  AssertFatal(idx >= 0 && idx < XN_HO_MAX_GNB, "at most %d gNBs in the Xn handover test\n", XN_HO_MAX_GNB);
  if (idx + 1 > nb_gnb)
    nb_gnb = idx + 1;
  const ngap_register_gnb_req_t *req = &NGAP_REGISTER_GNB_REQ(ng_register_req);
  sim_gnb_t *g = &gnb[idx];
  g->gnb_id = req->gNB_id;
  g->tac = req->tac;
  g->plmn = req->plmn[0].plmn;
  g->cell_id = cell_id;
  snprintf(g->ngu_ip, sizeof(g->ngu_ip), "%s", ngu_ip);
  g->ng_register_req = ng_register_req;
}

void xn_ho_sim_start(void)
{
  AssertFatal(nb_gnb >= 2, "the Xn handover test needs at least 2 gNBs\n");
  nb_ho = nb_gnb > XN_HO_MIN ? nb_gnb : XN_HO_MIN;
  LOG_I(NR_RRC, "Xn handover test with %d gNBs and %d handovers: starting gNB0\n", nb_gnb, nb_ho);
  itti_send_msg_to_task(TASK_NGAP, 0, gnb[0].ng_register_req);
}

static gtpu_tunnel_t ngu_tunnel(int inst, uint32_t teid)
{
  gtpu_tunnel_t t = {.teid = teid, .addr.length = 4};
  struct in_addr addr;
  inet_pton(AF_INET, gnb[inst].ngu_ip, &addr);
  memcpy(t.addr.buffer, &addr.s_addr, 4);
  return t;
}

static byte_array_t dummy_container(void)
{
  uint8_t buf[DUMMY_CONTAINER_LEN] = {0};
  return create_byte_array(sizeof(buf), buf);
}

/* ---------------------------------------------------------------- UE stub attach (NAS) */

static void send_initial_ue_message(void)
{
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, 0, NGAP_NAS_FIRST_REQ);
  ngap_nas_first_req_t *req = &NGAP_NAS_FIRST_REQ(msg);
  req->gNB_ue_ngap_id = ue.ue_id;
  req->plmn = gnb[0].plmn;
  req->nr_cell_id = gnb[0].cell_id;
  req->establishment_cause = NGAP_RRC_CAUSE_MO_SIGNALLING;

  nr_ue_nas_t *nas = get_ue_nas_info(0);
  // serving network name: no SIB1, take the PLMN of gNB0
  nas->sn_id = calloc_or_fail(1, sizeof(*nas->sn_id));
  *nas->sn_id = gnb[0].plmn;
  as_nas_info_t nas_msg = {0};
  generateRegistrationRequest(&nas_msg, nas, false);
  req->nas_pdu.buf = nas_msg.nas_data;
  req->nas_pdu.len = nas_msg.length;
  LOG_I(NR_RRC, "[gNB %d] UE stub %u: sending Registration Request\n", ue.serving, ue.ue_id);
  itti_send_msg_to_task(TASK_NGAP, ue.serving, msg);
}

static void send_uplink_nas(const as_nas_info_t *nas_msg)
{
  if (nas_msg->length == 0 || nas_msg->nas_data == NULL)
    return;
  const sim_gnb_t *g = &gnb[ue.serving];
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, ue.serving, NGAP_UPLINK_NAS);
  ngap_uplink_nas_t *ul = &NGAP_UPLINK_NAS(msg);
  ul->gNB_ue_ngap_id = ue.ue_id;
  ul->nas_pdu.buf = nas_msg->nas_data;
  ul->nas_pdu.len = nas_msg->length;
  ul->plmn = g->plmn;
  ul->nr_cell_id = g->cell_id;
  ul->tac = g->tac;
  itti_send_msg_to_task(TASK_NGAP, ue.serving, msg);
}

static void forward_downlink_nas(uint8_t *buf, uint32_t len)
{
  if (len == 0 || buf == NULL)
    return;
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, 0, NAS_DOWNLINK_DATA_IND);
  NAS_DOWNLINK_DATA_IND(msg).UEid = 0;
  NAS_DOWNLINK_DATA_IND(msg).nasMsg.length = len;
  NAS_DOWNLINK_DATA_IND(msg).nasMsg.nas_data = malloc_or_fail(len);
  memcpy(NAS_DOWNLINK_DATA_IND(msg).nasMsg.nas_data, buf, len);
  free(buf);
  itti_send_msg_to_task(TASK_NAS_NRUE, 0, msg);
}

/* store a PDU session to setup and fill the response item, with the N3 tunnel of the serving gNB */
static void setup_pdu_session(const pdusession_resource_item_t *item, pdusession_setup_t *resp)
{
  AssertFatal(ue.nb_pdu < NR_MAX_NB_PDU_SESSIONS, "too many PDU sessions\n");
  sim_pdu_session_t *s = &ue.pdu[ue.nb_pdu++];
  const pdusession_transfer_t *tr = &item->pdusessionTransfer;
  *s = (sim_pdu_session_t){.id = item->pdusession_id,
                           .type = tr->pdu_session_type,
                           .nssai = item->nssai,
                           .n3_incoming = tr->n3_incoming,
                           .nb_qos = tr->nb_qos};
  memcpy(s->qos, tr->qos, sizeof(s->qos));

  resp->pdusession_id = s->id;
  resp->pdu_session_type = s->type;
  resp->n3_outgoing = ngu_tunnel(ue.serving, ue.ue_id * 16 + s->id);
  resp->nb_of_qos_flow = s->nb_qos;
  for (int q = 0; q < s->nb_qos; q++) {
    resp->associated_qos_flows[q].qfi = s->qos[q].qfi;
    resp->associated_qos_flows[q].qos_flow_mapping_ind = QOSFLOW_MAPPING_INDICATION_DL;
  }
}

/* start the other gNBs once the UE is attached to gNB0 */
static void start_other_gnbs(void)
{
  if (gnbs_started)
    return;
  gnbs_started = true;
  LOG_I(NR_RRC, "UE stub %u attached to gNB0 with %d PDU session(s): starting gNB1..gNB%d\n", ue.ue_id, ue.nb_pdu, nb_gnb - 1);
  for (int i = 1; i < nb_gnb; i++)
    itti_send_msg_to_task(TASK_NGAP, i, gnb[i].ng_register_req);
}

static void process_initial_context_setup(instance_t inst, ngap_initial_context_setup_req_t *req)
{
  ue.amf_ue_ngap_id = req->amf_ue_ngap_id;
  ue.amf_ng_ip = req->amf_ng_ip;
  ue.guami = req->guami;
  ue.sec_cap = req->security_capabilities;
  ue.ambr = req->ue_ambr;
  if (req->nas_pdu.len > 0)
    forward_downlink_nas(req->nas_pdu.buf, req->nas_pdu.len);
  for (int i = 0; i < req->nb_of_pdusessions; i++)
    forward_downlink_nas(req->pdusession[i].nas_pdu.buf, req->pdusession[i].nas_pdu.len);

  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, NGAP_INITIAL_CONTEXT_SETUP_RESP);
  ngap_initial_context_setup_resp_t *resp = &NGAP_INITIAL_CONTEXT_SETUP_RESP(msg);
  resp->gNB_ue_ngap_id = req->gNB_ue_ngap_id;
  for (int i = 0; i < req->nb_of_pdusessions; i++)
    setup_pdu_session(&req->pdusession[i], &resp->pdusessions[resp->nb_of_pdusessions++]);
  itti_send_msg_to_task(TASK_NGAP, inst, msg);
  /* the AMF may set up the PDU session already in the Initial Context Setup */
  if (req->nb_of_pdusessions > 0)
    start_other_gnbs();
}

static void process_pdusession_setup(instance_t inst, ngap_pdusession_setup_req_t *req)
{
  for (int i = 0; i < req->nb_pdusessions_tosetup; i++)
    forward_downlink_nas(req->pdusession[i].nas_pdu.buf, req->pdusession[i].nas_pdu.len);

  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, NGAP_PDUSESSION_SETUP_RESP);
  ngap_pdusession_setup_resp_t *resp = &NGAP_PDUSESSION_SETUP_RESP(msg);
  resp->gNB_ue_ngap_id = req->gNB_ue_ngap_id;
  for (int i = 0; i < req->nb_pdusessions_tosetup; i++)
    setup_pdu_session(&req->pdusession[i], &resp->pdusessions[resp->nb_of_pdusessions++]);
  itti_send_msg_to_task(TASK_NGAP, inst, msg);
  start_other_gnbs();
}

/* ---------------------------------------------------------------- Xn handover */

/* source: send Handover Request for the UE stub to the next gNB */
static void trigger_handover(void)
{
  const int src = ue.serving, dst = (src + 1) % nb_gnb;
  ue.target = dst;
  LOG_I(NR_RRC,
        "=== Xn handover %d/%d: UE stub %u gNB%d (0x%x) -> gNB%d (0x%x)\n",
        nb_ho_done + 1,
        nb_ho,
        ue.ue_id,
        src,
        gnb[src].gnb_id,
        dst,
        gnb[dst].gnb_id);

  xnap_pdusession_resources_tobe_setup_item_t *pdu_list = calloc_or_fail(ue.nb_pdu, sizeof(*pdu_list));
  for (int i = 0; i < ue.nb_pdu; i++) {
    const sim_pdu_session_t *s = &ue.pdu[i];
    xnap_pdusession_resources_tobe_setup_item_t *item = &pdu_list[i];
    item->pdusession_id = s->id;
    item->pdu_session_type = s->type;
    item->n3_incoming = s->n3_incoming;
    item->nssai = malloc_or_fail(sizeof(*item->nssai));
    *item->nssai = s->nssai;
    item->num_qos = s->nb_qos;
    item->qos_list = calloc_or_fail(s->nb_qos, sizeof(*item->qos_list));
    for (int q = 0; q < s->nb_qos; q++) {
      const pdusession_level_qos_parameter_t *qp = &s->qos[q];
      xnap_qos_flow_tobe_setup_item_t *qi = &item->qos_list[q];
      qi->qfi = qp->qfi;
      qi->qos_params.qos_type = qp->fiveQI_type;
      if (qp->fiveQI_type == NON_DYNAMIC) {
        qi->qos_params.nondyn.fiveQI = qp->qos_characteristics.non_dynamic.fiveQI;
      } else {
        qi->qos_params.dyn.prio = qp->qos_characteristics.dynamic.qos_priority;
        qi->qos_params.dyn.pdb = qp->qos_characteristics.dynamic.packet_delay_budget;
        qi->qos_params.dyn.per.scalar = qp->qos_characteristics.dynamic.per.scalar;
        qi->qos_params.dyn.per.exponent = qp->qos_characteristics.dynamic.per.exponent;
      }
      qi->qos_params.arp = qp->arp;
    }
  }

  // UE history: the serving cell
  ue_history_info_t *history = calloc_or_fail(1, sizeof(*history));
  history->xnap_cell_type = XNAP_LAST_VISITED_CELL_NR;
  history->last_visited_cell_info = dummy_container();

  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, src, XNAP_HANDOVER_REQ);
  xnap_handover_req_t *req = &XNAP_HANDOVER_REQ(msg);
  *req = (xnap_handover_req_t){
      .cause = {.type = XNAP_CAUSE_RADIO_NETWORK, .value = XNAP_CAUSE_RADIO_NETWORK_LAYER_HANDOVER_DESIRABLE_FOR_RADIO_REASONS},
      .target_cgi = {.plmn_id = gnb[dst].plmn, .nrcell_id = gnb[dst].cell_id},
      .guami = ue.guami,
      .ue_context =
          {
              .ngc_ue_sig_ref = ue.amf_ue_ngap_id,
              .security_capabilities =
                  {
                      .nRencryption_algorithms = ue.sec_cap.nRencryption_algorithms,
                      .nRintegrity_algorithms = ue.sec_cap.nRintegrity_algorithms,
                      .eUTRAencryption_algorithms = ue.sec_cap.eUTRAencryption_algorithms,
                      .eUTRAintegrity_algorithms = ue.sec_cap.eUTRAintegrity_algorithms,
                  },
              .ue_ambr = {.br_ul = ue.ambr.br_ul, .br_dl = ue.ambr.br_dl},
              // HandoverPreparationInformation: no RRC in the UE stub
              .rrc_context = dummy_container(),
              .num_pdu = ue.nb_pdu,
              .pdusession_resources_tobe_setup_list = pdu_list,
          },
      .num_last_visited_cells = 1,
      .ue_history_info = history,
      .rrc_ue_id = ue.ue_id,
      .target_assoc_id = gnb[src].peer_assoc_id[dst],
  };
  // AMF NG-C address, as text in the Initial Context Setup Request
  struct in_addr amf_addr;
  if (inet_pton(AF_INET, ue.amf_ng_ip.ipv4_address, &amf_addr) == 1) {
    req->ue_context.cp_tnl_ip_source.length = 4;
    memcpy(req->ue_context.cp_tnl_ip_source.buffer, &amf_addr.s_addr, 4);
  }
  itti_send_msg_to_task(TASK_XNAP, src, msg);
}

/* target: admit the UE stub */
static void process_handover_request(instance_t inst, xnap_handover_req_t *req)
{
  ue.target_ue_id = next_ue_id++;
  LOG_I(NR_RRC,
        "[gNB %ld] Xn HO target: UE stub %u: Handover Request received (s_xnap_ue_id %u t_xnap_ue_id %u), %d PDU session(s)\n",
        inst,
        ue.target_ue_id,
        req->s_ng_node_ue_xnap_id,
        req->t_ng_node_ue_xnap_id,
        req->ue_context.num_pdu);
  AssertFatal(xn_bind_ue(req->t_ng_node_ue_xnap_id, ue.target_ue_id), "cannot bind t_xnap_ue_id %u\n", req->t_ng_node_ue_xnap_id);

  const int num_pdu = req->ue_context.num_pdu;
  xnap_pdusession_admitted_item_t *admitted = calloc_or_fail(num_pdu, sizeof(*admitted));
  for (int i = 0; i < num_pdu; i++) {
    const xnap_pdusession_resources_tobe_setup_item_t *p = &req->ue_context.pdusession_resources_tobe_setup_list[i];
    admitted[i].pdusession_id = p->pdusession_id;
    admitted[i].num_qos = p->num_qos;
    admitted[i].qos_list = calloc_or_fail(p->num_qos, sizeof(*admitted[i].qos_list));
    for (int q = 0; q < p->num_qos; q++)
      admitted[i].qos_list[q].qfi = p->qos_list[q].qfi;
  }

  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, XNAP_HANDOVER_REQ_ACK);
  XNAP_HANDOVER_REQ_ACK(msg) = (xnap_handover_req_ack_t){
      .num_pdu_admitted = num_pdu,
      .pdusession_admitted_list = admitted,
      // HandoverCommand: no RRC in the UE stub
      .target2source = dummy_container(),
      .rrc_ue_id = ue.target_ue_id,
  };
  itti_send_msg_to_task(TASK_XNAP, inst, msg);
}

/* source: "send the handover command to the UE", then the PDCP status of its DRBs */
static void process_handover_request_ack(instance_t inst, const xnap_handover_req_ack_t *ack)
{
  LOG_I(NR_RRC,
        "[gNB %ld] Xn HO source: UE stub %u: Handover Request Acknowledge (s_xnap_ue_id %u t_xnap_ue_id %u), "
        "handover command to the UE, sending SN Status Transfer\n",
        inst,
        ack->rrc_ue_id,
        ack->s_ng_node_ue_xnap_id,
        ack->t_ng_node_ue_xnap_id);
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, XNAP_SN_STATUS_TRANSFER);
  xnap_sn_status_transfer_t *sn = &XNAP_SN_STATUS_TRANSFER(msg);
  sn->rrc_ue_id = ue.ue_id;
  sn->ran_status.nb_drb = 1;
  ue.pdcp_count += 10;
  const xnap_drb_count_value_t count = {.pdcp_sn = ue.pdcp_count, .hfn = 0, .sn_len = XNAP_SN_LENGTH_18};
  sn->ran_status.drb_status_list[0] = (xnap_drb_status_t){.drb_id = 1, .ul_count = count, .dl_count = count};
  itti_send_msg_to_task(TASK_XNAP, inst, msg);
}

/* target: "the UE arrived": switch the N3 path of its PDU sessions to this gNB */
static void process_sn_status_transfer(instance_t inst, const xnap_sn_status_transfer_t *sn)
{
  LOG_I(NR_RRC,
        "[gNB %ld] Xn HO target: UE stub %u: SN Status Transfer received (s_xnap_ue_id %u t_xnap_ue_id %u), "
        "UE arrived, sending Path Switch Request\n",
        inst,
        sn->rrc_ue_id,
        sn->s_ng_node_ue_xnap_id,
        sn->t_ng_node_ue_xnap_id);
  const sim_gnb_t *g = &gnb[inst];
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, NGAP_PATH_SWITCH_REQ);
  ngap_path_switch_req_t *req = &NGAP_PATH_SWITCH_REQ(msg);
  memset(req, 0, sizeof(*req));
  req->gNB_ue_ngap_id = ue.target_ue_id;
  req->amf_ue_ngap_id = ue.amf_ue_ngap_id;
  req->user_info = (user_location_information_t){
      .nrCellIdentity = g->cell_id,
      .target_ng_ran = {.targetgNBId = g->gnb_id, .plmn_identity = g->plmn, .tac = g->tac},
  };
  req->security_capabilities = ue.sec_cap;
  req->nb_of_pdusessions = ue.nb_pdu;
  for (int i = 0; i < ue.nb_pdu; i++) {
    const sim_pdu_session_t *s = &ue.pdu[i];
    pdusession_setup_t *p = &req->pdusessions_tobeswitched[i];
    p->pdusession_id = s->id;
    p->n3_outgoing = ngu_tunnel(inst, ue.target_ue_id * 16 + s->id);
    LOG_I(NR_RRC,
          "[gNB %ld] Xn HO target: UE stub %u:   PDU session %d: N3 tunnel to switch to TEID 0x%x at %s\n",
          inst,
          ue.target_ue_id,
          s->id,
          p->n3_outgoing.teid,
          g->ngu_ip);
    p->nb_of_qos_flow = s->nb_qos;
    for (int q = 0; q < s->nb_qos; q++)
      p->associated_qos_flows[q].qfi = s->qos[q].qfi;
  }
  itti_send_msg_to_task(TASK_NGAP, inst, msg);
}

/* target: handover completed, release the UE at the source */
static void process_path_switch_ack(instance_t inst, const ngap_path_switch_req_ack_t *ack)
{
  LOG_I(NR_RRC,
        "[gNB %ld] Xn HO target: UE stub %u: Path Switch Request Acknowledge received (NCC %d), "
        "sending UE Context Release to the source\n",
        inst,
        ack->gNB_ue_ngap_id,
        ack->nh_ncc);
  for (int i = 0; i < ack->nb_of_pdusessions; i++) {
    const path_switch_request_ack_pdusession_t *sw = &ack->pdusessions_switched[i];
    for (int j = 0; j < ue.nb_pdu; j++)
      if (ue.pdu[j].id == sw->pdusession_id && sw->pathSwitchReqAckTransfer.n3_incoming)
        ue.pdu[j].n3_incoming = *sw->pathSwitchReqAckTransfer.n3_incoming;
  }
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, XNAP_UE_CONTEXT_RELEASE);
  XNAP_UE_CONTEXT_RELEASE(msg).rrc_ue_id = ue.target_ue_id;
  itti_send_msg_to_task(TASK_XNAP, inst, msg);
}

static void deregister_ue(void)
{
  LOG_I(NR_RRC, "%d Xn handovers done: deregistering UE stub %u from gNB%d\n", nb_ho_done, ue.ue_id, ue.serving);
  MessageDef *msg = itti_alloc_new_message(TASK_NAS_NRUE, 0, NAS_DEREGISTRATION_REQ);
  NAS_DEREGISTRATION_REQ(msg).cause = AS_DETACH;
  itti_send_msg_to_task(TASK_NAS_NRUE, 0, msg);
}

/* source: the UE is now served by the target, remove it */
static void process_ue_context_release(instance_t inst, const xnap_ue_context_release_t *rel)
{
  LOG_I(NR_RRC,
        "[gNB %ld] Xn HO source: UE stub %u: UE Context Release received (s_xnap_ue_id %u t_xnap_ue_id %u), "
        "releasing it locally\n",
        inst,
        rel->rrc_ue_id,
        rel->s_ng_node_ue_xnap_id,
        rel->t_ng_node_ue_xnap_id);
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_GNB, inst, NGAP_UE_CONTEXT_LOCAL_RELEASE);
  NGAP_UE_CONTEXT_LOCAL_RELEASE(msg).gNB_ue_ngap_id = rel->rrc_ue_id;
  itti_send_msg_to_task(TASK_NGAP, inst, msg);

  ue.serving = ue.target;
  ue.ue_id = ue.target_ue_id;
  nb_ho_done++;
  LOG_I(NR_RRC, "=== Xn handover %d/%d done: UE stub %u served by gNB%d\n", nb_ho_done, nb_ho, ue.ue_id, ue.serving);
  sleep(1);
  if (nb_ho_done < nb_ho)
    trigger_handover();
  else
    deregister_ue();
}

/* start the handovers once every gNB has completed Xn Setup with all the others */
static void process_xn_setup(instance_t inst, const xnap_setup_ind_t *ind)
{
  int peer = -1;
  for (int i = 0; i < nb_gnb; i++)
    if (gnb[i].gnb_id == ind->gnb_id)
      peer = i;
  AssertFatal(peer >= 0 && peer != inst, "[gNB %ld] Xn Setup with unknown gNB 0x%x\n", inst, ind->gnb_id);
  if (gnb[inst].peer_assoc_id[peer] == 0)
    gnb[inst].nb_xn_peers++;
  gnb[inst].peer_assoc_id[peer] = ind->assoc_id;
  LOG_I(NR_RRC,
        "[gNB %ld] Xn Setup with gNB%d (0x%x) completed, %d/%d peers\n",
        inst,
        peer,
        ind->gnb_id,
        gnb[inst].nb_xn_peers,
        nb_gnb - 1);

  for (int i = 0; i < nb_gnb; i++)
    if (gnb[i].nb_xn_peers < nb_gnb - 1)
      return;
  if (ho_started)
    return;
  ho_started = true;
  LOG_I(NR_RRC, "Xn Setup completed between all %d gNBs\n", nb_gnb);
  sleep(1);
  trigger_handover();
}

static void *watchdog(void *arg)
{
  (void)arg;
  sleep(XN_HO_TIMEOUT_S);
  LOG_E(NR_RRC, "Xn handover test: timeout after %d s, %d/%d handovers done\n", XN_HO_TIMEOUT_S, nb_ho_done, nb_ho);
  exit(EXIT_FAILURE);
}

void xn_ho_sim_ng_setup_done(instance_t inst)
{
  if (inst == 0) {
    pthread_t t;
    pthread_create(&t, NULL, watchdog, NULL);
    send_initial_ue_message();
  }
}

/* ---------------------------------------------------------------- tasks */

void *xn_ho_rrc_ue_task(void *arg)
{
  (void)arg;
  itti_mark_task_ready(TASK_RRC_NRUE);
  while (1) {
    MessageDef *msg;
    itti_receive_msg(TASK_RRC_NRUE, &msg);
    switch (ITTI_MSG_ID(msg)) {
      case TERMINATE_MESSAGE:
        itti_exit_task();
        break;
      case NAS_UPLINK_DATA_REQ:
        send_uplink_nas(&NAS_UPLINK_DATA_REQ(msg).nasMsg);
        break;
      case NAS_5GMM_IND:
      case NAS_KENB_REFRESH_REQ:
      case NAS_DETACH_REQ:
      case NAS_TUN_REQ:
        break;
      default:
        LOG_E(NR_RRC, "UE stub RRC: unexpected message %s\n", ITTI_MSG_NAME(msg));
        break;
    }
    AssertFatal(itti_free(ITTI_MSG_ORIGIN_ID(msg), msg) == EXIT_SUCCESS, "Failed to free memory\n");
  }
  return NULL;
}

void *xn_ho_rrc_gnb_task(void *arg)
{
  (void)arg;
  itti_mark_task_ready(TASK_RRC_GNB);
  while (1) {
    MessageDef *msg;
    itti_receive_msg(TASK_RRC_GNB, &msg);
    const instance_t inst = ITTI_MSG_DESTINATION_INSTANCE(msg);
    switch (ITTI_MSG_ID(msg)) {
      case TERMINATE_MESSAGE:
        itti_exit_task();
        break;

      /* UE stub attach */
      case NGAP_DOWNLINK_NAS:
        forward_downlink_nas(NGAP_DOWNLINK_NAS(msg).nas_pdu.buf, NGAP_DOWNLINK_NAS(msg).nas_pdu.len);
        break;
      case NGAP_INITIAL_CONTEXT_SETUP_REQ:
        process_initial_context_setup(inst, &NGAP_INITIAL_CONTEXT_SETUP_REQ(msg));
        break;
      case NGAP_PDUSESSION_SETUP_REQ:
        process_pdusession_setup(inst, &NGAP_PDUSESSION_SETUP_REQ(msg));
        break;

      /* Xn handover */
      case XNAP_SETUP_IND:
        process_xn_setup(inst, &XNAP_SETUP_IND(msg));
        break;
      case XNAP_HANDOVER_REQ:
        process_handover_request(inst, &XNAP_HANDOVER_REQ(msg));
        free_xnap_handover_request(&XNAP_HANDOVER_REQ(msg));
        break;
      case XNAP_HANDOVER_REQ_ACK:
        process_handover_request_ack(inst, &XNAP_HANDOVER_REQ_ACK(msg));
        free_xnap_handover_request_acknowledge(&XNAP_HANDOVER_REQ_ACK(msg));
        break;
      case XNAP_SN_STATUS_TRANSFER:
        process_sn_status_transfer(inst, &XNAP_SN_STATUS_TRANSFER(msg));
        break;
      case NGAP_PATH_SWITCH_REQ_ACK:
        process_path_switch_ack(inst, &NGAP_PATH_SWITCH_REQ_ACK(msg));
        free_ng_path_switch_req_ack(&NGAP_PATH_SWITCH_REQ_ACK(msg));
        break;
      case XNAP_UE_CONTEXT_RELEASE:
        process_ue_context_release(inst, &XNAP_UE_CONTEXT_RELEASE(msg));
        break;
      case XNAP_HANDOVER_PREP_FAILURE:
      case XNAP_HANDOVER_CANCEL:
      case XNAP_PEER_SHUTDOWN_IND:
        LOG_E(NR_RRC, "[gNB %ld] Xn handover %d failed: %s\n", inst, nb_ho_done + 1, ITTI_MSG_NAME(msg));
        exit(EXIT_FAILURE);
        break;

      /* end of the test: deregistration of the UE stub */
      case NGAP_UE_CONTEXT_RELEASE_COMMAND: {
        MessageDef *resp = itti_alloc_new_message(TASK_RRC_GNB, inst, NGAP_UE_CONTEXT_RELEASE_COMPLETE);
        NGAP_UE_CONTEXT_RELEASE_COMPLETE(resp).gNB_ue_ngap_id = NGAP_UE_CONTEXT_RELEASE_COMMAND(msg).gNB_ue_ngap_id;
        itti_send_msg_to_task(TASK_NGAP, inst, resp);
        sleep(1); // let NGAP send UE Context Release Complete
        if (nb_ho_done < nb_ho) {
          LOG_E(NR_RRC, "Xn handover test failed: UE released by the AMF after %d/%d handovers\n", nb_ho_done, nb_ho);
          exit(EXIT_FAILURE);
        }
        LOG_I(NR_RRC, "Xn handover test completed successfully: %d/%d handovers\n", nb_ho_done, nb_ho);
        exit(EXIT_SUCCESS);
      } break;

      default:
        LOG_E(NR_RRC, "[gNB %ld] Received unexpected message %s\n", inst, ITTI_MSG_NAME(msg));
        break;
    }
    AssertFatal(itti_free(ITTI_MSG_ORIGIN_ID(msg), msg) == EXIT_SUCCESS, "Failed to free memory\n");
  }
  return NULL;
}
