/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "common/platform_types.h"
#include "common/utils/LOG/log.h"
#include "common/config/config_userapi.h"

#include "gtp_itf.h"

extern "C" {
#include "gtpu_extensions.h"

int nr_rlc_get_available_tx_space(const rnti_t rntiP, const logical_chan_id_t channel_idP)
{
  UNUSED(rntiP);
  UNUSED(channel_idP);
  return 4096;
}
void *get_softmodem_params(void)
{
  abort();
  return NULL;
};
}

/* @brief initialize GTP interface for IP:port */
static instance_t init_gtp(const char *ip, uint16_t port)
{
  openAddr_t iface = {0};
  DevAssert(strlen(ip) <= sizeof(iface.originHost));
  strcpy(iface.originHost, ip);
  snprintf(iface.originService, sizeof(iface.originService), "%d", port);
  snprintf(iface.destinationService, sizeof(iface.destinationService), "%d", port);
  return gtpv1Init(iface);
}

static in_addr_t get_addr(int ai_family, const char *ip)
{
  AssertFatal(ai_family == AF_INET, "only IPv4 supported\n");
  in_addr_t addr = {0};
  inet_pton(ai_family, ip, &addr);
  return addr;
}

static transport_layer_addr_t get_tl_addr(int ai_family, const char *ip)
{
  in_addr_t addr = get_addr(ai_family, ip);
  transport_layer_addr_t tl_addr = {.length = 32};
  memcpy(tl_addr.buffer, &addr, 4);
  return tl_addr;
}

typedef union {
  struct sockaddr sa;
  struct sockaddr_in sin;
} test_sockaddr_t;

static test_sockaddr_t get_sock_addr(const char *ip, uint16_t port)
{
  test_sockaddr_t a = {};
  a.sin.sin_family = AF_INET;
  a.sin.sin_port = htons(port);
  a.sin.sin_addr.s_addr = get_addr(AF_INET, ip);
  return a;
}

static void run_basic_test(uint32_t ue_id,
                           long pdu_id,
                           long qfi,
                           int num_send,
                           int *rcv_count,
                           gtpCallback callBack,
                           gtpCallbackSDAP callBackSDAP)
{
  /* set up two instances on different IPs */
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  instance_t ep1 = init_gtp(ip1, port);
  EXPECT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  EXPECT_GE(ep2, 1);
  EXPECT_NE(ep1, ep2); // different IPs => different interfaces

  /* Create receiving end on ep1. Since we don't know the ep2's TEID, we also
   * don't provide an address yet, hence "null_addr". Install the callback
   * specific to this test. */
  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, callBack, callBackSDAP, NULL);

  /* Create the sending end on ep2. We have ep1's address/TEID, so create the
   * remote endpoint. Don't provide a callback, as this is supposed to be
   * unidirectional. */
  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL, NULL);

  EXPECT_NE(t1, t2); // cannot be the same TEIDs

  /* With the TEID at ep2, update ep1 endpoint. */
  in_addr_t addr2 = get_addr(AF_INET, ip2);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, addr2, t2);

  /* Send num_send messages, with each message three bytes with a specfic
   * payload structure (each byte increasing within the buffer and across
   * message) checked by the callback recv_basic_conn_qfi(). */
  uint8_t buf[3];
  uint8_t payload_counter = 0;
  for (int i = 0; i < num_send; ++i) {
    for (size_t p = 0; p < sizeof(buf); p++)
      buf[p] = payload_counter++;
    if (qfi >= 0)
      gtpv1uSendDirectWithQFI(ep2, ue_id, pdu_id, qfi, buf, sizeof(buf));
    else
      gtpv1uSendDirect(ep2, ue_id, pdu_id, buf, sizeof(buf), false, false);
  }

  usleep(100 * 1000); // wait 100ms to give time to receive packets

  /* check that three messages have been received. The callback checked that
   * the messages had the expected format. */
  EXPECT_EQ(*rcv_count, num_send);

  int ret;
  ret = newGtpuDeleteAllTunnels(ep1, ue_id);
  EXPECT_EQ(ret, 0);
  ret = newGtpuDeleteAllTunnels(ep2, ue_id);
  EXPECT_EQ(ret, 0);

  ret = gtpv1Term(ep1);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep2);
  EXPECT_EQ(ret, 0);
}

static int recv_count_qfi = 0;
static bool recv_basic_conn_qfi(protocol_ctxt_t *ctxt,
                                const ue_id_t ue_id,
                                const srb_flag_t flag,
                                const mui_t mui,
                                const confirm_t confirm,
                                const sdu_size_t size,
                                unsigned char *const buf,
                                const pdcp_transmission_mode_t modeP,
                                const uint32_t *sourceL2Id,
                                const uint32_t *destinationL2Id,
                                const uint8_t qfi,
                                const bool rqi,
                                const int pdusession_id)
{
  UNUSED(ctxt);
  UNUSED(flag);
  UNUSED(mui);
  UNUSED(confirm);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  UNUSED(rqi);
  UNUSED(pdusession_id);
  EXPECT_EQ(ue_id, 1); // as defined in basic_conn_qfi
  EXPECT_EQ(qfi, 1);
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count_qfi * size + i);
  recv_count_qfi++;
  LOG_I(GTPU, "received message %d with size 3, content %d.%d.%d\n", recv_count_qfi, buf[0], buf[1], buf[2]);
  return true;
}

static int recv_count_multi_qfi = 0;
static const uint8_t expected_qfis_multi_qfi[] = {1, 5, 9, 5, 1};
static const int expected_qfis_multi_qfi_count = sizeof(expected_qfis_multi_qfi) / sizeof(expected_qfis_multi_qfi[0]);
static bool recv_multi_qfi_same_pdu(protocol_ctxt_t *ctxt,
                                    const ue_id_t ue_id,
                                    const srb_flag_t flag,
                                    const mui_t mui,
                                    const confirm_t confirm,
                                    const sdu_size_t size,
                                    unsigned char *const buf,
                                    const pdcp_transmission_mode_t modeP,
                                    const uint32_t *sourceL2Id,
                                    const uint32_t *destinationL2Id,
                                    const uint8_t qfi,
                                    const bool rqi,
                                    const int pdusession_id)
{
  UNUSED(ctxt);
  UNUSED(flag);
  UNUSED(mui);
  UNUSED(confirm);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  UNUSED(rqi);
  EXPECT_EQ(ue_id, 7U);
  EXPECT_EQ(pdusession_id, 9);
  EXPECT_LT(recv_count_multi_qfi, expected_qfis_multi_qfi_count);
  EXPECT_EQ(qfi, expected_qfis_multi_qfi[recv_count_multi_qfi]);
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count_multi_qfi * size + i);
  recv_count_multi_qfi++;
  return true;
}

static void run_multi_qos_flows_test(uint32_t ue_id, long pdu_id, const uint8_t *qfis, int num_send)
{
  /* set up two instances on different IPs */
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  instance_t ep1 = init_gtp(ip1, port);
  EXPECT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  EXPECT_GE(ep2, 1);
  EXPECT_NE(ep1, ep2);

  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, NULL, recv_multi_qfi_same_pdu, NULL);

  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL, NULL);
  EXPECT_NE(t1, t2);

  in_addr_t addr2 = get_addr(AF_INET, ip2);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, addr2, t2);

  uint8_t buf[3];
  uint8_t payload_counter = 0;
  for (int i = 0; i < num_send; ++i) {
    for (size_t p = 0; p < sizeof(buf); p++)
      buf[p] = payload_counter++;
    gtpv1uSendDirectWithQFI(ep2, ue_id, pdu_id, qfis[i], buf, sizeof(buf));
  }

  usleep(100 * 1000);
  EXPECT_EQ(recv_count_multi_qfi, num_send);

  int ret = newGtpuDeleteAllTunnels(ep1, ue_id);
  EXPECT_EQ(ret, 0);
  ret = newGtpuDeleteAllTunnels(ep2, ue_id);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep1);
  EXPECT_EQ(ret, 0);
  ret = gtpv1Term(ep2);
  EXPECT_EQ(ret, 0);
}

/* Test unidirectional GTP message forwarding for a single UE with QFI. */
TEST(gtp, basic_conn_qfi)
{
  /* we consider only a single UE on a specfic bearer (ID=3) */
  uint32_t ue_id = 1;
  long pdu_id = 3;
  long qfi = 1;
  int num_send = 3;
  run_basic_test(ue_id, pdu_id, qfi, num_send, &recv_count_qfi, NULL, recv_basic_conn_qfi);
}

/* Test one PDU session carrying multiple QoS flows (different QFIs). */
TEST(gtp, multi_qos_flows)
{
  uint32_t ue_id = 7;
  long pdu_id = 9;
  static const uint8_t qfis[] = {1, 5, 9, 5, 1};
  recv_count_multi_qfi = 0;
  run_multi_qos_flows_test(ue_id, pdu_id, qfis, sizeof(qfis) / sizeof(qfis[0]));
}

static int recv_count = 0;
static bool recv_basic_conn(protocol_ctxt_t *ctxt,
                            const srb_flag_t srb_flagP,
                            const rb_id_t rb_idP,
                            const mui_t muiP,
                            const confirm_t confirmP,
                            const sdu_size_t size,
                            unsigned char *const buf,
                            const pdcp_transmission_mode_t modeP,
                            const uint32_t *sourceL2Id,
                            const uint32_t *destinationL2Id)
{
  UNUSED(srb_flagP);
  UNUSED(rb_idP);
  UNUSED(muiP);
  UNUSED(confirmP);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  EXPECT_EQ(ctxt->rntiMaybeUEid, 12); // as defined in basic_conn
  EXPECT_EQ(size, 3);
  for (int i = 0; i < size; ++i)
    EXPECT_EQ(buf[i], recv_count * size + i);
  recv_count++;
  LOG_I(GTPU, "received message %d with size 3, content %d.%d.%d\n", recv_count, buf[0], buf[1], buf[2]);
  return true;
}

/* Test unidirectional GTP message forwarding for a single UE (without QFI). */
TEST(gtp, basic_conn)
{
  /* we consider only a single UE on a specfic bearer (ID=3) */
  uint32_t ue_id = 12;
  long pdu_id = 5;
  long noqfi = -1;
  int num_send = 12;
  run_basic_test(ue_id, pdu_id, noqfi, num_send, &recv_count, recv_basic_conn, NULL);
}

enum class GtpuPacketError { none, empty, truncated_header, length_too_small, length_too_large };

static constexpr ue_id_t batch_ue_id = 42;
static constexpr int batch_bearer_id = 1;
static constexpr int batch_size = 8;
static constexpr uint8_t last_batch_packet = batch_size + 1;

static struct {
  std::mutex mutex;
  std::condition_variable changed;
  bool receiver_blocked = false;
  bool release_receiver = false;
  std::vector<uint8_t> received;
} recv_batch_state;

static bool recv_batch(protocol_ctxt_t *ctxt,
                       const srb_flag_t srb_flagP,
                       const rb_id_t rb_idP,
                       const mui_t muiP,
                       const confirm_t confirmP,
                       const sdu_size_t size,
                       unsigned char *const buf,
                       const pdcp_transmission_mode_t modeP,
                       const uint32_t *sourceL2Id,
                       const uint32_t *destinationL2Id)
{
  UNUSED(srb_flagP);
  UNUSED(muiP);
  UNUSED(confirmP);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  EXPECT_EQ(ctxt->rntiMaybeUEid, batch_ue_id);
  EXPECT_EQ(rb_idP, batch_bearer_id);
  EXPECT_EQ(size, 1);
  if (size != 1)
    return false;

  std::unique_lock<std::mutex> lock(recv_batch_state.mutex);
  if (buf[0] == 0) {
    recv_batch_state.receiver_blocked = true;
    recv_batch_state.changed.notify_one();
    recv_batch_state.changed.wait(lock, [] { return recv_batch_state.release_receiver; });
  } else {
    recv_batch_state.received.push_back(buf[0]);
    recv_batch_state.changed.notify_one();
  }
  return true;
}

static void send_batch_packet(int sender,
                              const test_sockaddr_t *destination,
                              teid_t teid,
                              uint8_t payload,
                              GtpuPacketError error = GtpuPacketError::none)
{
  /* TS 29.281 clause 5.1: mandatory G-PDU header followed by one payload octet. */
  uint8_t packet[] = {0x30, 255, 0, 1, uint8_t(teid >> 24), uint8_t(teid >> 16), uint8_t(teid >> 8), uint8_t(teid), payload};
  size_t len = sizeof(packet);
  switch (error) {
    case GtpuPacketError::none:
      break;
    case GtpuPacketError::empty:
      len = 0;
      break;
    case GtpuPacketError::truncated_header:
      len = 7;
      break;
    case GtpuPacketError::length_too_small:
      packet[3] = 0;
      break;
    case GtpuPacketError::length_too_large:
      packet[3] = 2;
      break;
  }
  ASSERT_EQ(sendto(sender, packet, len, 0, &destination->sa, sizeof(destination->sin)), ssize_t(len));
}

static void check_receive_batch(instance_t instance, int sender, GtpuPacketError malformed, int bad_index)
{
  test_sockaddr_t destination = {};
  socklen_t addr_len = sizeof(destination.sin);
  ASSERT_EQ(getsockname(instance, &destination.sa, &addr_len), 0);
  transport_layer_addr_t remote_addr = get_tl_addr(AF_INET, "127.0.0.1");
  teid_t teid =
      newGtpuCreateTunnel(instance, batch_ue_id, batch_bearer_id, batch_bearer_id, 1, remote_addr, recv_batch, NULL, NULL);
  ASSERT_NE(teid, 0U);

  ASSERT_NO_FATAL_FAILURE(send_batch_packet(sender, &destination, teid, 0));
  {
    std::unique_lock<std::mutex> lock(recv_batch_state.mutex);
    ASSERT_TRUE(recv_batch_state.changed.wait_for(lock, std::chrono::seconds(5), [] { return recv_batch_state.receiver_blocked; }));
  }

  /* Hold the receiver in its callback until an entire recvmmsg batch is queued. */
  std::vector<uint8_t> expected;
  for (int i = 0; i < batch_size; ++i) {
    ASSERT_NO_FATAL_FAILURE(
        send_batch_packet(sender, &destination, teid, i + 1, i == bad_index ? malformed : GtpuPacketError::none));
    if (i != bad_index)
      expected.push_back(i + 1);
  }
  /* This packet belongs to the next batch and proves the receiver made progress. */
  ASSERT_NO_FATAL_FAILURE(send_batch_packet(sender, &destination, teid, last_batch_packet));
  expected.push_back(last_batch_packet);
  {
    std::unique_lock<std::mutex> lock(recv_batch_state.mutex);
    recv_batch_state.release_receiver = true;
    recv_batch_state.changed.notify_one();
    ASSERT_TRUE(recv_batch_state.changed.wait_for(lock, std::chrono::seconds(5), [] {
      return !recv_batch_state.received.empty() && recv_batch_state.received.back() == last_batch_packet;
    }));
    EXPECT_EQ(recv_batch_state.received, expected);
  }
}

static void run_receive_batch_test(GtpuPacketError malformed, int bad_index)
{
  SCOPED_TRACE(testing::Message() << "malformed datagram at batch position " << bad_index);
  {
    std::lock_guard<std::mutex> lock(recv_batch_state.mutex);
    recv_batch_state.receiver_blocked = false;
    recv_batch_state.release_receiver = false;
    recv_batch_state.received.clear();
  }

  instance_t instance = init_gtp("127.0.0.1", 0);
  ASSERT_GE(instance, 0);
  int sender = socket(AF_INET, SOCK_DGRAM, 0);
  EXPECT_GE(sender, 0);
  if (sender >= 0)
    check_receive_batch(instance, sender, malformed, bad_index);

  /* Release the callback even if a setup, send, or receive assertion failed. */
  {
    std::lock_guard<std::mutex> lock(recv_batch_state.mutex);
    recv_batch_state.release_receiver = true;
  }
  recv_batch_state.changed.notify_one();
  EXPECT_EQ(newGtpuDeleteAllTunnels(instance, batch_ue_id), 0);
  EXPECT_EQ(gtpv1Term(instance), 0);
  if (sender >= 0) {
    EXPECT_EQ(close(sender), 0);
  }
}

TEST(gtp, receive_batch_empty_datagram)
{
  for (int bad_index : {0, 3, 7})
    run_receive_batch_test(GtpuPacketError::empty, bad_index);
}

TEST(gtp, receive_batch_truncated_header)
{
  for (int bad_index : {0, 3, 7})
    run_receive_batch_test(GtpuPacketError::truncated_header, bad_index);
}

TEST(gtp, receive_batch_length_too_small)
{
  for (int bad_index : {0, 3, 7})
    run_receive_batch_test(GtpuPacketError::length_too_small, bad_index);
}

TEST(gtp, receive_batch_length_too_large)
{
  for (int bad_index : {0, 3, 7})
    run_receive_batch_test(GtpuPacketError::length_too_large, bad_index);
}

static int recv_count_end_marker = 0;
static bool recv_end_marker(protocol_ctxt_t *ctxt,
                            const srb_flag_t srb_flagP,
                            const rb_id_t rb_idP,
                            const mui_t muiP,
                            const confirm_t confirmP,
                            const sdu_size_t size,
                            unsigned char *const buf,
                            const pdcp_transmission_mode_t modeP,
                            const uint32_t *sourceL2Id,
                            const uint32_t *destinationL2Id)
{
  UNUSED(ctxt);
  UNUSED(srb_flagP);
  UNUSED(rb_idP);
  UNUSED(muiP);
  UNUSED(confirmP);
  UNUSED(modeP);
  UNUSED(sourceL2Id);
  UNUSED(destinationL2Id);
  EXPECT_EQ(size, 3);
  EXPECT_NE(buf, nullptr);
  recv_count_end_marker++;
  return true;
}
/* Only G-PDUs carry a T-PDU (TS 29.281 §7.1): an End Marker (§7.3.2) must not reach PDCP, a following G-PDU must */
TEST(gtp, end_marker_not_delivered)
{
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  uint32_t ue_id = 30;
  long pdu_id = 2;
  instance_t ep1 = init_gtp(ip1, port);
  ASSERT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  ASSERT_GE(ep2, 1);
  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, recv_end_marker, NULL, NULL);
  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL, NULL);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, get_addr(AF_INET, ip2), t2);
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(fd, 0);
  test_sockaddr_t src = get_sock_addr(ip2, 0);
  ASSERT_EQ(bind(fd, &src.sa, sizeof(src.sin)), 0);
  test_sockaddr_t dst = get_sock_addr(ip1, port);
  /* TS 29.281 §5.1: 8-octet header, no optional fields, no payload */
  const uint8_t end_marker[] = {0x30, 254, 0x00, 0x00, (uint8_t)(t1 >> 24), (uint8_t)(t1 >> 16), (uint8_t)(t1 >> 8), (uint8_t)t1};
  ASSERT_EQ(sendto(fd, end_marker, sizeof(end_marker), 0, &dst.sa, sizeof(dst.sin)), (ssize_t)sizeof(end_marker));
  usleep(100 * 1000);
  EXPECT_EQ(recv_count_end_marker, 0);
  uint8_t data[3] = {1, 2, 3};
  gtpv1uSendDirect(ep2, ue_id, pdu_id, data, sizeof(data), false, false);
  usleep(100 * 1000);
  EXPECT_EQ(recv_count_end_marker, 1);
  close(fd);
  EXPECT_EQ(newGtpuDeleteAllTunnels(ep1, ue_id), 0);
  EXPECT_EQ(newGtpuDeleteAllTunnels(ep2, ue_id), 0);
  EXPECT_EQ(gtpv1Term(ep1), 0);
  EXPECT_EQ(gtpv1Term(ep2), 0);
}

static int build_gtpu_nrup(uint8_t *out, int out_len, uint32_t teid, gtpu_extension_header_t *ext)
{
  /* TS 29.281: 8-octet GTP-U header + 4 octets (seq / N-PDU / next ext) because E=1 */
  const int gtpu_hdr_len = 12;
  if (out_len < gtpu_hdr_len)
    return -1;
  memset(out, 0, gtpu_hdr_len);
  out[0] = 0x34; /* Version=1, Protocol Type=GTP, Extension Header=1 */
  out[1] = 255; /* G-PDU */
  out[4] = teid >> 24; /* TEID, octets 5-8 */
  out[5] = teid >> 16;
  out[6] = teid >> 8;
  out[7] = teid;
  out[11] = serialize_gtpu_extension_type(ext->type);
  const int ext_len = serialize_extension(ext, GTPU_EXT_NONE, &out[gtpu_hdr_len], out_len - gtpu_hdr_len);
  if (ext_len < 0)
    return -1;
  const int pkt_len = gtpu_hdr_len + ext_len;
  const uint16_t gtp_len = pkt_len - 8; /* Length: octets after the mandatory 8-octet header */
  out[2] = gtp_len >> 8;
  out[3] = gtp_len;
  return pkt_len;
}

/* CU TX (gtpv1uSendDirectWithNRUSeqNum) does not set Report Delivered yet, so this test
 * injects GTP-U + NR-UP DL USER DATA with that IE. The receiver then sends DL DATA DELIVERY STATUS. */
TEST(gtp, nrup_ddds)
{
  const char *ip1 = "127.0.0.1";
  const char *ip2 = "127.0.0.2";
  uint16_t port = 4567;
  uint32_t ue_id = 20;
  long pdu_id = 1;

  instance_t ep1 = init_gtp(ip1, port);
  ASSERT_GE(ep1, 1);
  instance_t ep2 = init_gtp(ip2, port);
  ASSERT_GE(ep2, 1);

  transport_layer_addr_t null_addr = {.length = 32};
  teid_t t1 = newGtpuCreateTunnel(ep1, ue_id, pdu_id, pdu_id, -1, null_addr, NULL, NULL, NULL);
  transport_layer_addr_t tl_addr1 = get_tl_addr(AF_INET, ip1);
  teid_t t2 = newGtpuCreateTunnel(ep2, ue_id, pdu_id, pdu_id, t1, tl_addr1, NULL, NULL, NULL);
  in_addr_t addr2 = get_addr(AF_INET, ip2);
  GtpuUpdateTunnelOutgoingAddressAndTeid(ep1, ue_id, pdu_id, addr2, t2);

  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(fd, 0);
  test_sockaddr_t src = get_sock_addr(ip2, 0);
  ASSERT_EQ(bind(fd, &src.sa, sizeof(src.sin)), 0);
  test_sockaddr_t dst = get_sock_addr(ip1, port);

  gtpu_extension_header_t ext = {
      .type = GTPU_EXT_DL_USER_DATA,
      .dl_user_data = {.nru_sequence_number = 1, .report_delivered = true, .nr_pdcp_pdu_sn = 5},
  };
  uint8_t pkt[256];
  const int pkt_len = build_gtpu_nrup(pkt, sizeof(pkt), t1, &ext);
  ASSERT_GT(pkt_len, 0);
  ASSERT_EQ(sendto(fd, pkt, pkt_len, 0, &dst.sa, sizeof(dst.sin)), pkt_len);

  usleep(100 * 1000);
  uint8_t ddds[256];
  const ssize_t n = recv(fd, ddds, sizeof(ddds), MSG_DONTWAIT);
  EXPECT_GT(n, 0) << "expected DDDS from receiver";

  close(fd);
  EXPECT_EQ(newGtpuDeleteAllTunnels(ep1, ue_id), 0);
  EXPECT_EQ(newGtpuDeleteAllTunnels(ep2, ue_id), 0);
  EXPECT_EQ(gtpv1Term(ep1), 0);
  EXPECT_EQ(gtpv1Term(ep2), 0);
}

/** @brief Build a GTPv1-U Error Indication (omit an IE by leaving it zero) */
static size_t build_error_indication(uint8_t *buf, size_t buf_cap, const gtpv1u_error_indication_t *in)
{
  const uint8_t peer_octets = in->gtpu_peer_address.length / 8;
  /** TEID-I (TV): 1 type + 4 value = 5 bytes
   * Peer Address (TLV): 1 type + 2 length + address octets */
  const size_t teid_ie_len = (in->teid_i != 0) ? (1U + GTPU_TEID_I_VALUE_OCTETS) : 0U;
  const size_t peer_ie_len = (peer_octets != 0) ? (1U + 2U + peer_octets) : 0U;
  const size_t ie_len = teid_ie_len + peer_ie_len;
  const size_t body_len = 4U + ie_len; /* optional header (S=1) + IEs */
  const size_t total_len = 8U + body_len; /* mandatory header (8) + payload */

  if (buf_cap < total_len || ie_len == 0)
    return 0;

  uint8_t *p = buf;

  /* Mandatory GTP-U header (8 octets, TS 29.281 clause 5.1 Figure 5.1-1) */
  *p++ = 0x32; /* TS 29.281 clause 5.1: S=1 for Error Indication */
  *p++ = 26; /* Message Type: Error Indication (TS 29.281 Table 6.1-1) */
  *p++ = (body_len >> 8) & 0xff; /* Length (network byte order) */
  *p++ = body_len & 0xff;
  memset(p, 0, 4); /* TEID = 0 (TS 29.281 clause 5.1) */
  p += 4;

  /* Optional GTP-U header fields (4 octets) */
  memset(p, 0, 4); /* Sequence Number, N-PDU Number, Next Extension Header Type */
  p += 4;

  /* Message body: Error Indication IEs (TS 29.281 Table 7.3.1-1) */
  /* TEID-I (TV IE: Type + 4-octet value) */
  if (in->teid_i != 0) {
    *p++ = GTPU_TEID_I;
    uint32_t teid_be = htonl(in->teid_i);
    memcpy(p, &teid_be, sizeof teid_be);
    p += sizeof teid_be;
  }

  /* GTP-U Peer Address (TLV: Type + Length + address octets) */
  if (peer_octets != 0) {
    *p++ = GTPU_PEER_ADDRESS; /* Type */
    *p++ = 0; /* Length (network byte order) */
    *p++ = peer_octets; /* number of address octets */
    memcpy(p, in->gtpu_peer_address.buffer, peer_octets);
    p += peer_octets;
  }

  return total_len;
}

TEST(gtp, error_indication_decode)
{
  uint8_t buf[48] = {0};
  gtpv1u_error_indication_t indication = {0};
  gtpv1u_error_indication_t in = {0};
  size_t len = 0;

  in.gtpu_peer_address = get_tl_addr(AF_INET, "192.168.1.1");

  /* valid mandatory IEs (TEID-I + GTP-U Peer Address) */
  in.teid_i = 0x12345678;
  len = build_error_indication(buf, sizeof buf, &in);
  ASSERT_GT(len, 0U);
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len, &indication), 0);
  EXPECT_EQ(indication.teid_i, 0x12345678U);
  EXPECT_EQ(indication.gtpu_peer_address.length, 32U);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[0], 192);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[3], 1);

  /* truncation */
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len - 2, &indication), GTPNOK);

  /* missing mandatory IE (GTP-U Peer Address) */
  in.teid_i = 0x12345678;
  in.gtpu_peer_address.length = 0;
  len = build_error_indication(buf, sizeof buf, &in);
  ASSERT_GT(len, 0U);
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len, &indication), GTPNOK);

  /* missing mandatory IE (TEID-I) */
  in.teid_i = 0;
  in.gtpu_peer_address = get_tl_addr(AF_INET, "192.168.1.1");
  len = build_error_indication(buf, sizeof buf, &in);
  ASSERT_GT(len, 0U);
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len, &indication), GTPNOK);

  /* non-zero header TEID */
  in.teid_i = 0x1;
  len = build_error_indication(buf, sizeof buf, &in);
  ASSERT_GT(len, 0U);
  buf[7] = 1;
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len, &indication), 0);
  EXPECT_EQ(indication.teid_i, 0x1U);
  EXPECT_EQ(indication.gtpu_peer_address.length, 32U);

  /* S=0, 8-byte header (e.g. OAI CN5G UPF): warn and accept */
  static const uint8_t ei_s0[] = {
      0x30, 0x1a, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, /* flags S=0, type 26, len 12, TEID 0 */
      0x10, 0x00, 0x00, 0x00, 0x1e, /* TEID-I 0x1e */
      0x85, 0x00, 0x04, 192,  168,  70,   129, /* Peer Address 192.168.70.129 */
  };
  EXPECT_EQ(gtpv1u_decode_error_indication(ei_s0, sizeof ei_s0, &indication), 0);
  EXPECT_EQ(indication.teid_i, 0x1eU);
  EXPECT_EQ(indication.gtpu_peer_address.length, 32U);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[0], 192);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[3], 129);

  /* optional IE present (Recovery Time Stamp) */
  in.teid_i = 0xdeadbeef;
  len = build_error_indication(buf, sizeof buf, &in);
  ASSERT_GT(len, 0U);
  uint8_t *p = buf + len;
  *p++ = GTPU_RECOVERY_TIME_STAMP;
  *p++ = 0;
  *p++ = 4;
  *p++ = 0x12;
  *p++ = 0x34;
  *p++ = 0x56;
  *p++ = 0x78;
  const size_t total_len = p - buf;
  buf[2] = ((total_len - 8) >> 8) & 0xff;
  buf[3] = (total_len - 8) & 0xff;
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, total_len, &indication), 0);
  EXPECT_EQ(indication.teid_i, 0xdeadbeefU);
  EXPECT_EQ(indication.gtpu_peer_address.length, 32U);

  /* E=1 + non-zero header TEID (Open5GS lab): extensions skipped, TEID-I used */
  static const uint8_t ei_with_extensions[] = {
      0x36, 0x1a, 0x00, 0x18, 0x00, 0x00, 0x2c, 0xdf, /* GTP-U: E=1, S=1, type 26, TEID 0x2cdf */
      0x00, 0x00, 0x00, 0x85, /* optional header: Next Ext = PDU Session Container (0x85) */
      0x01, 0x00, 0x01, 0x40, /* ext 0x85, Next Ext = UDP Port (0x40) */
      0x01, 0x00, 0x00, 0x00, /* ext 0x40, Next Ext = none */
      0x10, 0x00, 0x00, 0x2c, 0xdf, /* TEID-I (type 16) */
      0x85, 0x00, 0x04, 192,  168,  71,   1, /* Peer Address (type 133), 192.168.71.1 */
  };
  EXPECT_EQ(gtpv1u_decode_error_indication(ei_with_extensions, sizeof ei_with_extensions, &indication), 0);
  EXPECT_EQ(indication.teid_i, 0x2cdfU);
  EXPECT_EQ(indication.gtpu_peer_address.length, 32U);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[0], 192);
  EXPECT_EQ(indication.gtpu_peer_address.buffer[3], 1);
}

TEST(gtp, error_indication_encode)
{
  uint8_t ie[32] = {0};
  const gtpv1u_error_indication_t in = {.teid_i = 0x12345678, .gtpu_peer_address = get_tl_addr(AF_INET, "10.0.0.1")};

  const int encoded = gtpv1u_encode_error_indication(&in, ie, sizeof ie);
  ASSERT_EQ(encoded, 1 + GTPU_TEID_I_VALUE_OCTETS + 1 + 2 + GTPU_PEER_ADDRESS_IPV4_OCTETS);
  EXPECT_EQ(gtpv1u_encode_error_indication(&in, ie, 4), GTPNOK);

  uint8_t buf[48] = {0};
  const size_t ie_len = 12; /* TEID-I (TV IE) + GTP-U Peer Address (TLV) */
  const size_t body_len = 4U + ie_len; /* optional 4-octet header block (S=1) + IEs */
  const size_t len = 8U + body_len; /* mandatory 8-octet header + body */
  uint8_t *p = buf;
  *p++ = 0x32; /* Flags: version=1, PT=GTP, S=1 (TS 29.281 clause 5.1) */
  *p++ = 26; /* Message Type: Error Indication */
  *p++ = (body_len >> 8) & 0xff; /* Length (network byte order) */
  *p++ = body_len & 0xff;
  memset(p, 0, 4); /* TEID = 0 (TS 29.281 clause 5.1) */
  p += 4;
  memset(p, 0, 4); /* Optional block: Sequence Number, N-PDU Number, Next Extension Header Type */
  p += 4;
  memcpy(p, ie, ie_len); /* TEID-I (TV IE) + GTP-U Peer Address (TLV) */

  gtpv1u_error_indication_t indication = {0};
  EXPECT_EQ(gtpv1u_decode_error_indication(buf, len, &indication), 0);
  EXPECT_EQ(indication.teid_i, in.teid_i);
  EXPECT_EQ(indication.gtpu_peer_address.length, in.gtpu_peer_address.length);
  EXPECT_EQ(memcmp(indication.gtpu_peer_address.buffer, in.gtpu_peer_address.buffer, in.gtpu_peer_address.length / 8), 0);

  uint8_t ie_rt[32] = {0};
  const int encoded_rt = gtpv1u_encode_error_indication(&indication, ie_rt, sizeof ie_rt);
  ASSERT_EQ(encoded_rt, encoded);
  EXPECT_EQ(memcmp(ie, ie_rt, encoded), 0);
}

/* ideas for tests:
 * - share IP addresses among two instances (e.g., F1-U/NG-U on same IP/port)
 * - support of IPv6
 * - test extension headers
 *
 * ideas for cleanup:
 * - simplify callback, harmonize "normal" and "SDAP" callbacks
 * - consistently use either transport_layer_addr_t or in_addr
 */

int main(int argc, char **argv)
{
  //if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL) {
  //  fprintf(stderr, "configuration module init failed\n");
  //  return -1;
  //}
  // TODO logInit() without config?
  logInit();
  g_log->log_component[GTPU].level = OAILOG_DEBUG;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
