/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <chrono>
#include <gtest/gtest.h>
#include <vector>
extern "C" {
#include "common_lib.h"
#include "executables/softmodem-common.h"
#include "common/utils/LOG/log.h"
static softmodem_params_t params;
softmodem_params_t *get_softmodem_params(void)
{
  return &params;
}
}

struct Write {
  openair0_timestamp_t ts;
  int n;
  int flags;
  std::vector<c16_t> s;
};
static std::vector<Write> writes;

static int capture(openair0_device_t *dev, openair0_timestamp_t ts, void **buff, int n, int nb_ant, int flags)
{
  const c16_t *b = (const c16_t *)buff[0];
  writes.push_back({ts, n, flags, std::vector<c16_t>(b, b + n)});
  return n;
}

class WriteReorder : public ::testing::Test {
 protected:
  openair0_device_t dev = {};
  void SetUp() override
  {
    writes.clear();
    params.optmask.bit.SOFTMODEM_RFSIM_BIT = 0;
    openair0_config_t cfg = {};
    cfg.sample_rate = 3.84e6; // ring of 38912 samples, 10 ms
    cfg.tx_num_channels = 1;
    openair0_write_reorder_init(&dev.reOrder, &cfg);
    dev.trx_write_func = capture;
  }
  void TearDown() override
  {
    openair0_write_reorder_free(&dev.reOrder);
  }
  int write(openair0_timestamp_t ts, int n, int flags, int16_t v = 1, int nb_writers = 1, int writer = 0)
  {
    std::vector<c16_t> buf(n, c16_t{v, v});
    void *p[1] = {buf.data()};
    return openair0_write_reorder_common(NULL, NULL, &dev, ts, p, n, writer, nb_writers, 1, flags);
  }
  // flush up to until, waiting up to wait_ms for the writers, returns the time waited in ms
  double flush(int nb_writers, openair0_timestamp_t until, int wait_ms)
  {
    auto start = std::chrono::steady_clock::now();
    openair0_write_reorder_flush_common(NULL, NULL, &dev, nb_writers, 1, until, wait_ms);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  int sent(int64_t from, int64_t to) // number of written samples in [from, to)
  {
    int n = 0;
    for (auto &w : writes)
      n += std::max<int64_t>(0, std::min<int64_t>(to, w.ts + w.n) - std::max<int64_t>(from, w.ts));
    return n;
  }
};

// TDD on real hardware: the UE writes every slot, with TX_BURST_INVALID in DL slots. The UL burst must be
// sent completely, START on its first sample and END on its last, even when its tail is flushed during the
// next DL slot write; DL samples are not sent.
TEST_F(WriteReorder, tdd_burst_flags)
{
  const int slot = 3000;
  write(0, slot, TX_BURST_INVALID);
  write(slot, slot, TX_BURST_START);
  write(2 * slot, slot, TX_BURST_END);
  write(3 * slot, slot, TX_BURST_INVALID);
  write(4 * slot, slot, TX_BURST_INVALID);
  ASSERT_FALSE(writes.empty());
  EXPECT_EQ(sent(0, slot), 0);
  EXPECT_EQ(sent(slot, 3 * slot), 2 * slot);
  EXPECT_EQ(sent(3 * slot, 5 * slot), 0);
  EXPECT_EQ(writes.front().ts, slot);
  EXPECT_EQ(writes.front().flags, TX_BURST_START);
  EXPECT_EQ(writes.back().ts + writes.back().n, 3 * slot);
  EXPECT_EQ(writes.back().flags, TX_BURST_END);
  for (size_t i = 1; i + 1 < writes.size(); i++)
    EXPECT_EQ(writes[i].flags, TX_BURST_MIDDLE);
  for (auto &w : writes)
    EXPECT_LE(w.n, dev.reOrder.grain);
}

// FDD: every write is TX_BURST_MIDDLE, all samples are sent, in timestamp order, in full grains
TEST_F(WriteReorder, fdd_in_order)
{
  for (int i = 0; i < 10; i++)
    write(i * 1000, 1000, TX_BURST_MIDDLE);
  EXPECT_EQ(sent(0, 8192), 8192);
  for (size_t i = 0; i < writes.size(); i++) {
    EXPECT_EQ(writes[i].flags, TX_BURST_MIDDLE);
    EXPECT_EQ(writes[i].n, dev.reOrder.grain);
    if (i) {
      EXPECT_EQ(writes[i].ts, writes[i - 1].ts + writes[i - 1].n);
    }
  }
}

// two writers: a sample is sent once both wrote it, as the saturated sum
TEST_F(WriteReorder, two_writers_sum)
{
  write(0, 4096, TX_BURST_MIDDLE, 30000, 2, 0);
  EXPECT_TRUE(writes.empty());
  write(0, 4096, TX_BURST_MIDDLE, 30000, 2, 1);
  ASSERT_EQ(sent(0, 4096), 4096);
  EXPECT_EQ(writes[0].s[0].r, INT16_MAX);
}

// out of order writes (several TX threads) are sent in order; the first write sets the start of the stream
TEST_F(WriteReorder, out_of_order)
{
  write(0, 2048, TX_BURST_MIDDLE, 1);
  write(4096, 2048, TX_BURST_MIDDLE, 3);
  write(2048, 2048, TX_BURST_MIDDLE, 2);
  write(6144, 2048, TX_BURST_MIDDLE, 4);
  ASSERT_GE(writes.size(), 3u);
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(writes[i].ts, i * 2048);
    EXPECT_EQ(writes[i].s[0].r, i + 1);
  }
}

// a writer that stops does not block the other one beyond half the ring
TEST_F(WriteReorder, missing_writer)
{
  const int sz = dev.reOrder.sz;
  for (int ts = 0; ts < sz; ts += 2048)
    write(ts, 2048, TX_BURST_MIDDLE, 1, 2);
  EXPECT_GE(sent(0, sz), sz / 2 - 2048);
}

// a flush that waited in vain marks absent only the writer that did not write: it is not waited for until it
// writes again, then it is waited for again
TEST_F(WriteReorder, absent_writer)
{
  write(0, 4096, TX_BURST_MIDDLE, 1, 2, 0);
  EXPECT_GE(flush(2, 4096, 20), 15);
  EXPECT_EQ(sent(0, 4096), 4096);
  EXPECT_EQ(dev.reOrder.nb_absent, 1);
  EXPECT_TRUE(dev.reOrder.writer_absent[1]);
  // writer 1 absent: the samples of writer 0 alone are complete
  write(4096, 2048, TX_BURST_MIDDLE, 1, 2, 0);
  EXPECT_LT(flush(2, 6144, 1000), 500);
  EXPECT_EQ(sent(4096, 6144), 2048);
  // writer 1 writes again: it is waited for again
  write(6144, 2048, TX_BURST_MIDDLE, 1, 2, 1);
  EXPECT_EQ(dev.reOrder.nb_absent, 0);
  write(6144, 2048, TX_BURST_MIDDLE, 1, 2, 0);
  write(8192, 2048, TX_BURST_MIDDLE, 1, 2, 0);
  EXPECT_GE(flush(2, 10240, 20), 15);
  EXPECT_EQ(sent(6144, 8192), 2048);
  EXPECT_EQ(dev.reOrder.nb_absent, 1);
  EXPECT_TRUE(dev.reOrder.writer_absent[1]);
}

// a few samples a writer left unwritten (timing advance change) are not waited for once all the writers passed them
TEST_F(WriteReorder, writer_gap)
{
  const int sz = dev.reOrder.sz;
  write(0, sz / 2, TX_BURST_MIDDLE, 1, 2, 0);
  write(0, 1000, TX_BURST_MIDDLE, 1, 2, 1);
  write(1010, sz / 2 - 1010, TX_BURST_MIDDLE, 1, 2, 1);
  EXPECT_LT(flush(2, 2048, 1000), 500);
  EXPECT_EQ(sent(0, 2048), 2048);
  EXPECT_EQ(dev.reOrder.nb_absent, 0);
}

// a write older than what was already sent is dropped, the rest of it is kept
TEST_F(WriteReorder, late_write)
{
  write(0, 4096, TX_BURST_MIDDLE);
  const int64_t next = dev.reOrder.nextTS;
  ASSERT_EQ(next, 4096);
  write(2048, 4096, TX_BURST_MIDDLE, 5); // [2048, 4096) already sent
  write(6144, 2048, TX_BURST_MIDDLE);
  EXPECT_EQ(sent(0, 8192), 8192);
  EXPECT_EQ(writes.back().ts + writes.back().n, 8192);
}

// clear_context discards pending samples: nothing stale is sent after restarting at another timestamp
TEST_F(WriteReorder, clear_context)
{
  write(0, 1000, TX_BURST_MIDDLE, 7);
  openair0_write_reorder_clear_context(&dev);
  writes.clear();
  write(100000, 2048, TX_BURST_MIDDLE, 9);
  ASSERT_EQ(writes.size(), 1u);
  EXPECT_EQ(writes[0].ts, 100000);
  for (auto &c : writes[0].s)
    EXPECT_EQ(c.r, 9);
}

int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
