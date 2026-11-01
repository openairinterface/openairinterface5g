/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pcap.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_byteorder.h>
#include "oru_packet_processor.h"
#include "xran_pkt_api.h"
#include "xran_pkt_bfw.h"
#include "log.h"
#include "common/config/config_userapi.h"

#define MAX_ANTENNAS 4

// OAI Linkage Satisfiers
void exit_function(const char *file, const char *function, const int line, const char *s, const int assertflag)
{
  fprintf(stderr, "Error at %s:%s:%d - %s\n", file, function, line, s ? s : "None");
  exit(1);
}
configmodule_interface_t *uniqCfg = NULL;

struct rte_mempool *mp = NULL;
uint64_t g_total_uplane_sent = 0;

void *test_alloc_mbuf(void *io_controller)
{
  return rte_pktmbuf_alloc(mp);
}

void test_send_mbuf(void *io_controller, struct rte_mbuf **mbufs, uint32_t num_mbufs)
{
  for (uint32_t i = 0; i < num_mbufs; i++) {
    g_total_uplane_sent++;
    rte_pktmbuf_free(mbufs[i]);
  }
}

/*
 * --xran-ext1-reference / --xran-ext11-reference: checks for the xran O-DU ext1 and ext11 reference captures
 * (https://github.com/ConnorBlasie/openairinterface5g/releases/tag/ext1-pcaps and .../tag/ext11-pcaps).
 * Both are made by the same kit and generator, only the section extension differs.
 */
#define REF_NUM_ANT 4
#define REF_NUM_BF_ELM 32
#define REF_NUM_SLOTS_FILE 20 // numSlots: input files repeat every 20 slots (one frame at 30 kHz)
#define REF_SLOTS_PER_FRAME 20
#define REF_SYMS_PER_SLOT 14
#define REF_NUM_PRB 273
#define REF_MAX_ELM 2
#define REF_EXT11_BUND_PRB 8
#define REF_MAX_EXT11_BUNDLES 18 // ceil(137 / 8)

typedef struct {
  int start_prb;
  int num_prb;
} ref_prb_elm_t;

static const ref_prb_elm_t ref_dl_elm[] = {{0, 136}, {136, 137}};
static const ref_prb_elm_t ref_ul_elm[] = {{0, 136}, {136, 137}};
#define REF_NUM_DL_ELM (int)(sizeof(ref_dl_elm) / sizeof(ref_dl_elm[0]))
#define REF_NUM_UL_ELM (int)(sizeof(ref_ul_elm) / sizeof(ref_ul_elm[0]))

typedef struct {
  bool announced;
  int start_sym;
  int num_sym;
  uint16_t sym_seen;
} ref_dl_section_t;

static int ref_ext_type; // 1 or 11, 0 if not checking a reference capture
static ref_dl_section_t ref_dl_sections[REF_NUM_ANT][256][REF_SLOTS_PER_FRAME][REF_MAX_ELM];
static int ref_section_id[2][REF_MAX_ELM]; // [dl][elm], -1 until first seen

static struct {
  uint64_t pkts, cplane_dl, cplane_ul, ext, uplane, iq_samples, errors;
  uint64_t dl_symbols_read, dl_symbols_silent, dl_iq_checked, dl_iq_errors, dl_iq_bad_symbols;
} ref_cnt;

#define REF_CHECK(cond, ...)               \
  do {                                     \
    if (!(cond)) {                         \
      if (ref_cnt.errors++ < 20) {         \
        printf("pkt %lu: ", ref_cnt.pkts); \
        printf(__VA_ARGS__);               \
        printf("\n");                      \
      }                                    \
      return;                              \
    }                                      \
  } while (0)

static uint16_t ref_be16(const uint8_t *p)
{
  return (uint16_t)(p[0] << 8 | p[1]);
}

// DDDSU: S slot = 6 DL, 4 guard, 4 UL symbols. Returns false if the slot has no symbols in that direction.
static bool ref_tdd_symbols(int slot, bool dl, int *start, int *num)
{
  switch (slot % 5) {
    case 0:
    case 1:
    case 2:
      *start = 0;
      *num = 14;
      return dl;
    case 3:
      *start = dl ? 0 : 10;
      *num = dl ? 6 : 4;
      return true;
    default:
      *start = 0;
      *num = 14;
      return !dl;
  }
}

static int ref_find_elm(const ref_prb_elm_t *tbl, int n, int start, int num)
{
  for (int k = 0; k < n; k++)
    if (tbl[k].start_prb == start && tbl[k].num_prb == num)
      return k;
  return -1;
}

static void ref_check_section_id(bool dl, int k, int id)
{
  if (ref_section_id[dl][k] < 0)
    ref_section_id[dl][k] = id;
  REF_CHECK(ref_section_id[dl][k] == id, "%s elm %d sectionId %d, earlier %d", dl ? "DL" : "UL", k, id, ref_section_id[dl][k]);
}

static void ref_check_ext1(const uint8_t *ext, const uint8_t *end, int ant, int slot, int k)
{
  // One per section, 3-byte header + 32 uncompressed 16-bit weights = 33 words
  int ext_words = ext[1];
  REF_CHECK(ext_words == 33 && ext + ext_words * 4 == end,
            "extLen %d words, %ld bytes left in packet",
            ext_words,
            (long)(end - ext));
  REF_CHECK(ext[2] == 0, "bfwCompHdr 0x%02x, expected uncompressed 16-bit", ext[2]);
  c16_t w[REF_NUM_BF_ELM];
  REF_CHECK(xran_decode_bfw_ext1(ext, end - ext, REF_NUM_BF_ELM, w) == XRAN_BFW_OK, "ext1 decode failed");
  // xran zero-pads ext1 to the 4-byte boundary (xran_decode_bfw_ext1() does not check this yet)
  for (const uint8_t *pad = ext + 3 + REF_NUM_BF_ELM * 4; pad < end; pad++)
    REF_CHECK(*pad == 0, "ext1 padding byte 0x%02x", *pad);
  for (int e = 0; e < REF_NUM_BF_ELM; e++)
    REF_CHECK(w[e].r == 1024 * ant + e && w[e].i == 300 * (slot % REF_NUM_SLOTS_FILE) + k,
              "ant %d slot %d elm %d weight %d: (%d,%d), expected (%d,%d)",
              ant,
              slot,
              k,
              e,
              w[e].r,
              w[e].i,
              1024 * ant + e,
              300 * (slot % REF_NUM_SLOTS_FILE) + k);
}

// One per section: RAD 1, 8 PRBs per bundle (the 137-PRB element's last bundle has 1 PRB), uncompressed 16-bit.
// sample-app takes bundle b's weights from byte nAntElmTRx * 2 * b of the slot's weight buffer, i.e. half a vector
// further per bundle (app_io_xran_ext_type11_populate()), so weight j of bundle b is buffer element n = 16 * b + j.
// Its beamIds are 0x7000 + b.
static void ref_check_ext11(const uint8_t *ext, const uint8_t *end, int ant, int slot, int num_prb)
{
  int n_bundles = (num_prb + REF_EXT11_BUND_PRB - 1) / REF_EXT11_BUND_PRB;
  size_t used = 6 + n_bundles * (2 + REF_NUM_BF_ELM * 4);
  size_t ext_len = (((size_t)ext[1] << 8) | ext[2]) * 4;
  REF_CHECK(ext_len == ((used + 3) & ~(size_t)3) && ext + ext_len == end,
            "extLen %zu bytes, %ld bytes left in packet, expected %zu",
            ext_len,
            (long)(end - ext),
            (used + 3) & ~(size_t)3);
  xran_bfw_ext11_hdr_t hdr;
  uint16_t ids[REF_MAX_EXT11_BUNDLES];
  c16_t w[REF_MAX_EXT11_BUNDLES * REF_NUM_BF_ELM];
  int dec_bundles = 0;
  xran_bfw_err_t err =
      xran_decode_bfw_ext11(ext, end - ext, num_prb, REF_NUM_BF_ELM, REF_MAX_EXT11_BUNDLES, &hdr, &dec_bundles, ids, w);
  REF_CHECK(err == XRAN_BFW_OK && dec_bundles == n_bundles,
            "ext11 decode failed: %s, %d bundles, expected %d",
            xran_bfw_strerror(err),
            dec_bundles,
            n_bundles);
  // The decoder also accepts compressed weights; this test only covers the uncompressed 16-bit shape
  REF_CHECK(!hdr.disableBFWs && hdr.RAD && hdr.bundleOffset == 0 && hdr.numBundPrb == REF_EXT11_BUND_PRB
                && hdr.bfwCompMeth == XRAN_BFWCOMPMETHOD_NONE && hdr.bfwIqWidth == 16,
            "ext11 header disableBFWs %d RAD %d bundleOffset %d numBundPrb %d bfwCompMeth %d bfwIqWidth %d, "
            "expected 0/1/0/%d, uncompressed 16-bit",
            hdr.disableBFWs,
            hdr.RAD,
            hdr.bundleOffset,
            hdr.numBundPrb,
            hdr.bfwCompMeth,
            hdr.bfwIqWidth,
            REF_EXT11_BUND_PRB);
  for (const uint8_t *pad = ext + used; pad < end; pad++)
    REF_CHECK(*pad == 0, "ext11 padding byte 0x%02x", *pad);
  for (int b = 0; b < n_bundles; b++) {
    REF_CHECK(ext[6 + b * (2 + REF_NUM_BF_ELM * 4)] >> 7 == 0, "bundle %d contInd set", b);
    REF_CHECK(ids[b] == 0x7000 + b, "bundle %d beamId 0x%04x, expected 0x%04x", b, ids[b], 0x7000 + b);
    for (int j = 0; j < REF_NUM_BF_ELM; j++) {
      int n = REF_NUM_BF_ELM / 2 * b + j;
      c16_t exp = {1024 * ant + n % REF_NUM_BF_ELM, 300 * (slot % REF_NUM_SLOTS_FILE) + n / REF_NUM_BF_ELM};
      c16_t got = w[b * REF_NUM_BF_ELM + j];
      REF_CHECK(got.r == exp.r && got.i == exp.i,
                "ant %d slot %d bundle %d weight %d: (%d,%d), expected (%d,%d)",
                ant,
                slot,
                b,
                j,
                got.r,
                got.i,
                exp.r,
                exp.i);
    }
  }
}

static void ref_check_cplane(const uint8_t *p, const uint8_t *end, int ant)
{
  REF_CHECK(end - p >= 16, "C-plane too short");
  bool dl = p[0] >> 7;
  int payload_ver = (p[0] >> 4) & 7, filter = p[0] & 0xF;
  int frame = p[1], subframe = p[2] >> 4, slot = subframe * 2 + (((p[2] & 0xF) << 2) | (p[3] >> 6)), start_sym = p[3] & 0x3F;
  REF_CHECK(payload_ver == 1 && filter == 0, "C-plane payloadVer %d filterIndex %d", payload_ver, filter);
  REF_CHECK(p[4] == 1 && p[5] == 1, "numberOfSections %d sectionType %d, expected 1/1", p[4], p[5]);
  REF_CHECK(p[6] == 0, "udCompHdr 0x%02x, expected uncompressed 16-bit", p[6]);
  REF_CHECK(slot < REF_SLOTS_PER_FRAME, "slot %d", slot);
  int tdd_start, tdd_num;
  REF_CHECK(ref_tdd_symbols(slot, dl, &tdd_start, &tdd_num),
            "%s C-plane in slot %d with no %s symbols",
            dl ? "DL" : "UL",
            slot,
            dl ? "DL" : "UL");
  REF_CHECK(start_sym == tdd_start, "%s slot %d startSymbolId %d, expected %d", dl ? "DL" : "UL", slot, start_sym, tdd_start);

  const uint8_t *s = p + 8; // section type 1 fields
  int sec_id = (s[0] << 4) | (s[1] >> 4), rb = (s[1] >> 3) & 1, sym_inc = (s[1] >> 2) & 1;
  int start_prb = ((s[1] & 3) << 8) | s[2], num_prb = s[3];
  int re_mask = (s[4] << 4) | (s[5] >> 4), num_sym = s[5] & 0xF, ef = s[6] >> 7, beam_id = ((s[6] & 0x7F) << 8) | s[7];
  int k = dl ? ref_find_elm(ref_dl_elm, REF_NUM_DL_ELM, start_prb, num_prb)
             : ref_find_elm(ref_ul_elm, REF_NUM_UL_ELM, start_prb, num_prb);
  REF_CHECK(k >= 0, "%s section startPrbc %d numPrbc %d not in the PRB element table", dl ? "DL" : "UL", start_prb, num_prb);
  REF_CHECK(rb == 0 && sym_inc == 0 && re_mask == 0xFFF, "rb %d symInc %d reMask 0x%03x", rb, sym_inc, re_mask);
  REF_CHECK(num_sym == tdd_num, "%s slot %d numSymbol %d, expected %d", dl ? "DL" : "UL", slot, num_sym, tdd_num);
  REF_CHECK(beam_id == k + 1, "beamId %d, expected %d", beam_id, k + 1);
  ref_check_section_id(dl, k, sec_id);

  const uint8_t *ext = s + 8;
  REF_CHECK(ef == 1 && end - ext >= 3, "section has no extension");
  int ext_type = ext[0] & 0x7F, ext_more = ext[0] >> 7;
  REF_CHECK(ext_type == ref_ext_type && ext_more == 0,
            "extType %d ef %d, expected a single ext%d",
            ext_type,
            ext_more,
            ref_ext_type);
  uint64_t errors = ref_cnt.errors;
  if (ref_ext_type == 1)
    ref_check_ext1(ext, end, ant, slot, k);
  else
    ref_check_ext11(ext, end, ant, slot, num_prb);
  if (ref_cnt.errors != errors)
    return;
  ref_cnt.ext++;

  if (dl) {
    ref_dl_section_t *d = &ref_dl_sections[ant][frame][slot][k];
    REF_CHECK(!d->announced, "duplicate DL C-plane ant %d frame %d slot %d elm %d", ant, frame, slot, k);
    *d = (ref_dl_section_t){.announced = true, .start_sym = start_sym, .num_sym = num_sym};
    ref_cnt.cplane_dl++;
  } else {
    ref_cnt.cplane_ul++;
  }
}

static void ref_check_uplane(const uint8_t *p, const uint8_t *end, int ant)
{
  REF_CHECK(end - p >= 8, "U-plane too short");
  bool dl = p[0] >> 7;
  int payload_ver = (p[0] >> 4) & 7, filter = p[0] & 0xF;
  int frame = p[1], subframe = p[2] >> 4, slot = subframe * 2 + (((p[2] & 0xF) << 2) | (p[3] >> 6)), sym = p[3] & 0x3F;
  REF_CHECK(dl && payload_ver == 1 && filter == 0, "U-plane dir %d payloadVer %d filterIndex %d", dl, payload_ver, filter);
  REF_CHECK(slot < REF_SLOTS_PER_FRAME && sym < REF_SYMS_PER_SLOT, "slot %d symbol %d", slot, sym);

  const uint8_t *s = p + 4; // one section per packet, no udCompHdr (static uncompressed)
  int sec_id = (s[0] << 4) | (s[1] >> 4), start_prb = ((s[1] & 3) << 8) | s[2], num_prb = s[3];
  int k = ref_find_elm(ref_dl_elm, REF_NUM_DL_ELM, start_prb, num_prb);
  REF_CHECK(k >= 0, "U-plane startPrbu %d numPrbu %d not in the PRB element table", start_prb, num_prb);
  REF_CHECK(sec_id == ref_section_id[1][k],
            "U-plane sectionId %d, C-plane sectionId %d for elm %d",
            sec_id,
            ref_section_id[1][k],
            k);
  ref_dl_section_t *d = &ref_dl_sections[ant][frame][slot][k];
  REF_CHECK(d->announced, "U-plane ant %d frame %d slot %d elm %d without a preceding C-plane", ant, frame, slot, k);
  REF_CHECK(sym >= d->start_sym && sym < d->start_sym + d->num_sym, "U-plane symbol %d outside C-plane symbols", sym);
  REF_CHECK(!(d->sym_seen & (1 << sym)), "duplicate U-plane ant %d frame %d slot %d symbol %d elm %d", ant, frame, slot, sym, k);
  d->sym_seen |= 1 << sym;

  const uint8_t *iq = s + 4;
  REF_CHECK(end - iq == num_prb * 12 * 4, "IQ payload %ld bytes, expected %d", (long)(end - iq), num_prb * 12 * 4);
  int16_t exp_i = 4096 * ant + 14 * (slot % REF_NUM_SLOTS_FILE) + sym;
  for (int n = 0; n < num_prb * 12; n++) {
    int16_t i = (int16_t)ref_be16(iq + 4 * n), q = (int16_t)ref_be16(iq + 4 * n + 2);
    int16_t exp_q = (start_prb * 12 + n) - 2048;
    REF_CHECK(i == exp_i && q == exp_q,
              "ant %d slot %d sym %d sc %d: IQ (%d,%d), expected (%d,%d)",
              ant,
              slot,
              sym,
              start_prb * 12 + n,
              i,
              q,
              exp_i,
              exp_q);
  }
  ref_cnt.iq_samples += num_prb * 12;
  ref_cnt.uplane++;
}

static void ref_check_packet(const struct pcap_pkthdr *h, const u_char *pkt)
{
  ref_cnt.pkts++;
  const uint8_t *end = pkt + h->caplen;
  REF_CHECK(h->caplen == h->len, "truncated frame (%u of %u bytes)", h->caplen, h->len);
  const uint8_t *p = pkt + 12;
  uint16_t eth_type = ref_be16(p);
  p += 2;
  if (eth_type == 0x8100) {
    eth_type = ref_be16(p + 2);
    p += 4;
  }
  REF_CHECK(eth_type == 0xAEFE, "EtherType 0x%04x", eth_type);
  REF_CHECK(end - p >= 8, "eCPRI header too short");
  int revision = p[0] >> 4, msg_type = p[1], payload_size = ref_be16(p + 2);
  REF_CHECK(revision == 1, "eCPRI revision %d", revision);
  REF_CHECK(payload_size == end - p - 4, "eCPRI payload size %d, frame has %ld", payload_size, (long)(end - p - 4));
  int ant = ref_be16(p + 4) & 0xFF; // sample-app eAxC: ruPortId in the low 8 bits
  REF_CHECK(ant < REF_NUM_ANT, "eAxC ruPortId %d", ant);
  if (msg_type == ECPRI_RT_CONTROL_DATA)
    ref_check_cplane(p + 8, end, ant);
  else if (msg_type == ECPRI_IQ_DATA)
    ref_check_uplane(p + 8, end, ant);
  else
    REF_CHECK(false, "unexpected eCPRI message type %d", msg_type);
}

// True if the capture's C-plane scheduled DL on this symbol
static bool ref_dl_symbol_announced(int frame, int slot, int symbol)
{
  for (int a = 0; a < REF_NUM_ANT; a++)
    for (int k = 0; k < REF_NUM_DL_ELM; k++) {
      const ref_dl_section_t *d = &ref_dl_sections[a][frame % 256][slot][k];
      if (d->announced && symbol >= d->start_sym && symbol < d->start_sym + d->num_sym)
        return true;
    }
  return false;
}

// The processor's DL output for one symbol, every antenna and subcarrier of the carrier: the IQ that was
// sent on a symbol the capture scheduled, zeros (nothing to transmit) on any other DL symbol, e.g. the
// symbols ticked after the capture ends
static void ref_check_dl_iq(uint32_t **txdataF, int frame, int slot, int symbol)
{
  bool announced = ref_dl_symbol_announced(frame, slot, symbol);
  if (announced)
    ref_cnt.dl_symbols_read++;
  else
    ref_cnt.dl_symbols_silent++;
  uint64_t wrong = 0;
  int first_a = 0, first_n = 0;
  for (int a = 0; a < REF_NUM_ANT; a++) {
    const c16_t *iq = (const c16_t *)txdataF[a];
    int16_t exp_i = announced ? 4096 * a + 14 * (slot % REF_NUM_SLOTS_FILE) + symbol : 0;
    for (int n = 0; n < REF_NUM_PRB * 12; n++) {
      int16_t exp_q = announced ? n - 2048 : 0;
      if (iq[n].r != exp_i || iq[n].i != exp_q) {
        if (wrong++ == 0) {
          first_a = a;
          first_n = n;
        }
      }
    }
  }
  ref_cnt.dl_iq_checked += REF_NUM_ANT * REF_NUM_PRB * 12;
  ref_cnt.dl_iq_errors += wrong;
  if (wrong && ref_cnt.dl_iq_bad_symbols++ < 20) {
    const c16_t *iq = (const c16_t *)txdataF[first_a];
    printf("read_dl_iq frame %d slot %d sym %d (%s): %lu wrong IQ samples, first ant %d sc %d (%d,%d), expected (%d,%d)\n",
           frame,
           slot,
           symbol,
           announced ? "scheduled" : "not scheduled",
           wrong,
           first_a,
           first_n,
           iq[first_n].r,
           iq[first_n].i,
           announced ? 4096 * first_a + 14 * (slot % REF_NUM_SLOTS_FILE) + symbol : 0,
           announced ? first_n - 2048 : 0);
  }
}

// Places each stream's IQ verbatim at its PRB offset on its antenna, the way read_dl_iq() used to
// fill txdataF - no rotation or weighting, so ref_check_dl_iq() can compare bit-exactly.
static void place_dl_streams(uint32_t **txdataF, const dl_iq_stream_t *streams, int num_streams)
{
  for (int a = 0; a < MAX_ANTENNAS; a++)
    memset(txdataF[a], 0, 273 * 12 * sizeof(uint32_t));
  for (int i = 0; i < num_streams; i++) {
    const dl_iq_stream_t *st = &streams[i];
    if (st->ant_id < MAX_ANTENNAS)
      memcpy(&txdataF[st->ant_id][st->start_prb * 12], st->iq, st->num_prb * 12 * sizeof(uint32_t));
  }
}

// Returns true if the reference capture passed: all packets as generated, all expected messages present,
// and the processor's DL IQ equal to what was sent
static bool ref_report(uint64_t exp_c, uint64_t exp_u)
{
  uint64_t incomplete = 0;
  for (int a = 0; a < REF_NUM_ANT; a++)
    for (int f = 0; f < 256; f++)
      for (int s = 0; s < REF_SLOTS_PER_FRAME; s++)
        for (int k = 0; k < REF_NUM_DL_ELM; k++) {
          const ref_dl_section_t *d = &ref_dl_sections[a][f][s][k];
          if (d->announced && d->sym_seen != ((1u << d->num_sym) - 1) << d->start_sym)
            incomplete++;
        }
  // Every DL U-plane packet carries one PRB element of one antenna for one symbol
  uint64_t exp_dl_symbols = exp_u / (REF_NUM_ANT * REF_NUM_DL_ELM);
  printf(
      "xran ext%d reference: packets %lu: C-plane DL %lu UL %lu (ext%d %lu), U-plane %lu (%lu IQ samples), incomplete "
      "DL sections %lu, errors %lu\n",
      ref_ext_type,
      ref_cnt.pkts,
      ref_cnt.cplane_dl,
      ref_cnt.cplane_ul,
      ref_ext_type,
      ref_cnt.ext,
      ref_cnt.uplane,
      ref_cnt.iq_samples,
      incomplete,
      ref_cnt.errors);
  printf(
      "xran ext%d reference: read_dl_iq %lu scheduled symbols (expected %lu) + %lu silent, %lu IQ samples checked, %lu wrong "
      "in %lu symbols\n",
      ref_ext_type,
      ref_cnt.dl_symbols_read,
      exp_dl_symbols,
      ref_cnt.dl_symbols_silent,
      ref_cnt.dl_iq_checked,
      ref_cnt.dl_iq_errors,
      ref_cnt.dl_iq_bad_symbols);
  return ref_cnt.errors == 0 && incomplete == 0 && ref_cnt.cplane_dl + ref_cnt.cplane_ul == exp_c && ref_cnt.ext == exp_c
         && ref_cnt.uplane == exp_u && ref_cnt.dl_symbols_read == exp_dl_symbols && ref_cnt.dl_iq_errors == 0;
}

int main(int argc, char *argv[])
{
  if (argc < 12) {
    printf(
        "Usage: %s <pcap_file> <numerology> <initial_symbol> <num_dl_slots> <num_ul_slots> <num_dl_symbols> <num_ul_symbols> "
        "<tdd_pattern_length_slots> <mtu> <prach_eaxc_offset> [<num_bf_weights> <min_ext1_received>] "
        "[--xran-ext1-reference|--xran-ext11-reference <expected_cplane> <expected_uplane>] -- <eal args>\n",
        argv[0]);
    return 1;
  }

  int eal_args_start = 1;
  while (eal_args_start < argc && strcmp("--", argv[eal_args_start]) != 0) {
    eal_args_start++;
  }

  if (eal_args_start < argc) {
    if (rte_eal_init(argc - eal_args_start, argv + eal_args_start) < 0) {
      fprintf(stderr, "Error: EAL initialization failed\n");
      return 1;
    }
  } else {
    // Default EAL args if none provided
    char *default_argv[] = {argv[0], "--no-huge", "--iova-mode=va", "--no-pci", "--file-prefix=test_oru_pcap"};
    if (rte_eal_init(5, default_argv) < 0) {
      fprintf(stderr, "Error: EAL initialization failed\n");
      return 1;
    }
  }
  logInit();

  mp = rte_pktmbuf_pool_create("test_pool", 4096, 0, 0, 10240, rte_socket_id());
  assert(mp != NULL);

  int numerology = atoi(argv[2]);
  uint64_t initial_symbol = atoll(argv[3]);
  int num_dl_slots = atoi(argv[4]);
  int num_ul_slots = atoi(argv[5]);
  int num_dl_symbols = atoi(argv[6]);
  int num_ul_symbols = atoi(argv[7]);
  int tdd_pattern_length_slots = atoi(argv[8]);
  size_t mtu = atoi(argv[9]);
  int prach_eaxc_offset = atoi(argv[10]);

  if (eal_args_start < 11) {
    printf("Error: Missing '--' separator for EAL arguments\n");
    return 1;
  }
  // Decode ext1 if present with N weights and require at least M min_ext1_received
  int num_bf_weights = 0;
  uint64_t min_ext1_received = 0;
  // Check the capture and the processor's DL IQ against the xran ext1/ext11 reference capture's generator
  uint64_t ref_exp_cplane = 0, ref_exp_uplane = 0;
  int num_positional = 0;
  for (int i = 11; i < eal_args_start; i++) {
    if ((strcmp(argv[i], "--xran-ext1-reference") == 0 || strcmp(argv[i], "--xran-ext11-reference") == 0)
        && i + 2 < eal_args_start) {
      ref_ext_type = strcmp(argv[i], "--xran-ext1-reference") == 0 ? 1 : 11;
      ref_exp_cplane = strtoull(argv[++i], NULL, 0);
      ref_exp_uplane = strtoull(argv[++i], NULL, 0);
    } else if (num_positional == 0) {
      num_bf_weights = atoi(argv[i]);
      num_positional++;
    } else if (num_positional == 1) {
      min_ext1_received = atoll(argv[i]);
      num_positional++;
    } else {
      printf("Error: unexpected argument '%s'\n", argv[i]);
      return 1;
    }
  }
  memset(ref_section_id, -1, sizeof(ref_section_id));

  printf("Starting test with parameters:\n");
  printf("  pcap: %s\n", argv[1]);
  printf("  numerology: %d\n", numerology);
  printf("  initial_symbol: %lu\n", initial_symbol);
  printf("  TDD: DL %d slots + %d sym, UL %d slots + %d sym, Pattern Length %d slots\n",
         num_dl_slots,
         num_dl_symbols,
         num_ul_slots,
         num_ul_symbols,
         tdd_pattern_length_slots);
  printf("  MTU: %zu\n", mtu);
  printf("  PRACH eAxC Offset: %d\n", prach_eaxc_offset);
  printf("  BF weights per ext1/ext11: %d, min ext1 expected: %lu (ext11 count: --xran-ext11-reference)\n",
         num_bf_weights,
         min_ext1_received);
  if (ref_ext_type)
    printf("  xran ext%d reference: expected C-plane %lu, U-plane %lu\n", ref_ext_type, ref_exp_cplane, ref_exp_uplane);

  void *ctx = init_packet_processor(numerology,
                                    273,
                                    0,
                                    1500,
                                    0,
                                    1200,
                                    num_dl_slots,
                                    num_ul_slots,
                                    num_dl_symbols,
                                    num_ul_symbols,
                                    tdd_pattern_length_slots,
                                    test_alloc_mbuf,
                                    test_send_mbuf,
                                    NULL,
                                    mtu,
                                    prach_eaxc_offset,
                                    FH_COMP_NONE,
                                    0);
  assert(ctx != NULL);
  set_num_bf_weights(ctx, num_bf_weights);

  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t *pcap = pcap_open_offline(argv[1], errbuf);
  if (!pcap) {
    fprintf(stderr, "Error: Could not open pcap file '%s': %s\n", argv[1], errbuf);
    return 1;
  }

  struct pcap_pkthdr *pkthdr;
  const u_char *packet;
  int pkt_count = 0;
  int ecpri_count = 0;

  double first_ts = 0;
  bool synced = false;

  uint64_t last_tick_sym = initial_symbol;

  static uint32_t *txdataF[MAX_ANTENNAS] = {NULL};
  for (int i = 0; i < MAX_ANTENNAS; i++)
    txdataF[i] = malloc(273 * 12 * sizeof(uint32_t));

  static dl_iq_stream_t dl_streams[MAX_DL_FRAGMENTS_PER_SYMBOL];
  static uint32_t *dl_iq_arena = NULL;
  dl_iq_arena = malloc(DL_IQ_ARENA_PRBS(273) * 12 * sizeof(uint32_t));

  while (pcap_next_ex(pcap, &pkthdr, &packet) >= 0) {
    pkt_count++;
    double ts = pkthdr->ts.tv_sec + pkthdr->ts.tv_usec / 1000000.0;
    if (ref_ext_type)
      ref_check_packet(pkthdr, packet);

    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mp);
    if (!mbuf) {
      fprintf(stderr, "Failed to allocate mbuf at packet %d\n", pkt_count);
      break;
    }

    char *dst = rte_pktmbuf_append(mbuf, pkthdr->caplen);
    memcpy(dst, packet, pkthdr->caplen);

    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    uint16_t eth_type = rte_be_to_cpu_16(eth->ether_type);
    size_t hdr_len = sizeof(struct rte_ether_hdr);

    if (eth_type == 0x8100) { // VLAN
      struct rte_vlan_hdr *vlan = (struct rte_vlan_hdr *)(eth + 1);
      eth_type = rte_be_to_cpu_16(vlan->eth_proto);
      hdr_len += sizeof(struct rte_vlan_hdr);
    }

    if (eth_type == 0xAEFE) {
      ecpri_count++;
      rte_pktmbuf_adj(mbuf, hdr_len);

      struct xran_ecpri_hdr *ecpri = rte_pktmbuf_mtod(mbuf, struct xran_ecpri_hdr *);

      if (!synced && ecpri->cmnhdr.bits.ecpri_mesg_type == ECPRI_RT_CONTROL_DATA) {
        struct xran_cp_radioapp_common_header *apphdr = (void *)((uint8_t *)ecpri + sizeof(struct xran_ecpri_hdr));
        uint32_t fields = rte_be_to_cpu_32(apphdr->field.all_bits);
        uint8_t frame = (fields >> 16) & 0xFF;
        uint8_t subframe = (fields >> 12) & 0x0F;
        uint8_t slot = (fields >> 6) & 0x3F;
        uint8_t start_sym = (fields)&0x3F;

        uint64_t target_sym = (uint64_t)frame * (140 * (1 << numerology)) + (subframe * (1 << numerology) + slot) * 14 + start_sym;
        handle_absolute_symbol_tick(ctx, initial_symbol);
        last_tick_sym = initial_symbol;
        first_ts = ts;
        synced = true;
        printf("Synced to pcap! First C-plane target sym: %lu, setting initial_symbol: %lu\n", target_sym, initial_symbol);
      }

      if (synced) {
        double sym_duration = 1.0 / (14000.0 * (1 << numerology));
        uint64_t elapsed_syms = (uint64_t)((ts - first_ts) / sym_duration);
        uint64_t current_sym = initial_symbol + elapsed_syms;

        if (current_sym > last_tick_sym) {
          for (uint64_t s = last_tick_sym + 1; s <= current_sym; s++) {
            handle_absolute_symbol_tick(ctx, s);

            // For UL verification, we call write_ul_iq for every symbol.
            // It will internally check TDD and UL C-plane presence.
            ul_job_t job;
            int poll_ret = poll_ul_job(ctx, &job);
            if (poll_ret == 0) {
              for (int i = 0; i < job.num_symbols; i++) {
                write_ul_iq(ctx, txdataF[0], job.symbol + i, &job);
              }
            }
            int frame = (s / (140 * (1 << numerology))) % 1024;
            int slot = (s / 14) % (10 * (1 << numerology));
            int sym = s % 14;
            write_prach_iq(ctx, txdataF, MAX_ANTENNAS, frame, slot, sym);

            // Drain ready jobs to prevent ring overflow
            int f, sl, sy;
            uint64_t hf;
            while (get_ready_job_count(ctx) > 0) {
              int n = read_dl_iq_streams(ctx, dl_streams, dl_iq_arena, MAX_DL_FRAGMENTS_PER_SYMBOL, &hf, &f, &sl, &sy);
              if (ref_ext_type) {
                place_dl_streams(txdataF, dl_streams, n);
                ref_check_dl_iq(txdataF, f, sl, sy);
              }
            }
          }
          last_tick_sym = current_sym;
        }

        if (ecpri->cmnhdr.bits.ecpri_mesg_type == ECPRI_IQ_DATA) {
          handle_uplane_packet(ctx, mbuf);
        } else if (ecpri->cmnhdr.bits.ecpri_mesg_type == ECPRI_RT_CONTROL_DATA) {
          handle_cplane_packet(ctx, mbuf);
        } else {
          rte_pktmbuf_free(mbuf);
        }
      } else {
        rte_pktmbuf_free(mbuf);
      }
    } else {
      rte_pktmbuf_free(mbuf);
    }
  }

  // Flush remaining symbols
  for (int i = 0; i < 100; i++) {
    handle_absolute_symbol_tick(ctx, ++last_tick_sym);
    // Drain ready jobs to prevent ring overflow during flush
    int f, sl, sy;
    uint64_t hf;
    while (get_ready_job_count(ctx) > 0) {
      int n = read_dl_iq_streams(ctx, dl_streams, dl_iq_arena, MAX_DL_FRAGMENTS_PER_SYMBOL, &hf, &f, &sl, &sy);
      if (ref_ext_type) {
        place_dl_streams(txdataF, dl_streams, n);
        ref_check_dl_iq(txdataF, f, sl, sy);
      }
    }
  }

  printf("\nProcessing complete.\n");
  printf("Total packets: %d\n", pkt_count);
  printf("eCPRI packets: %d\n", ecpri_count);

  oru_packet_processor_stats_t stats;
  get_packet_processor_stats(ctx, &stats);
  printf("ORU Packet Processor Stats:\n");
  printf("  Total C-Plane Packets: %lu\n", stats.total_cplane);
  printf("  Total U-Plane Received: %lu\n", stats.total_uplane_received);
  printf("  Total U-Plane Sent: %lu\n", stats.total_uplane_sent);
  printf("  C-Plane Header Errors: %lu\n", stats.cplane_err_hdr);
  printf("  C-Plane Protocol Version Errors: %lu\n", stats.cplane_err_ver);
  printf("  C-Plane Timing Early Errors: %lu\n", stats.cplane_err_early);
  printf("  C-Plane Timing Late Errors: %lu\n", stats.cplane_err_late);
  printf("  C-Plane Duplicate Packet Errors: %lu\n", stats.cplane_err_dup);
  printf("  U-Plane Timing Early Errors: %lu\n", stats.uplane_err_early);
  printf("  U-Plane Timing Late Errors: %lu\n", stats.uplane_err_late);
  printf("  U-Plane Duplicate Packet Errors: %lu\n", stats.uplane_err_dup);
  printf("  U-Plane Missing C-Plane Errors: %lu\n", stats.uplane_missing_cplane);
  printf("  UL C-Plane Missing: %lu\n", stats.ul_cplane_missing);
  printf("  DL TDD Mismatch: %lu\n", stats.dl_tdd_mismatch);
  printf("  UL TDD Mismatch: %lu\n", stats.ul_tdd_mismatch);
  printf("  Out of Mbufs: %lu\n", stats.out_of_mbufs);
  printf("  Application Too Slow Errors: %lu\n", stats.application_too_slow);
  printf("  C-Plane Section Extension 1 received: %lu\n", stats.cplane_ext1_received);
  printf("  C-Plane Section Extension 11 received: %lu\n", stats.cplane_ext11_received);
  printf("  C-Plane Malformed Section Extension 1 Errors: %lu\n", stats.cplane_err_sect_ext1);
  printf("  C-Plane Malformed Section Extension 11 Errors: %lu\n", stats.cplane_err_sect_ext11);
  printf("  C-Plane Malformed Section Extension Chain Errors: %lu\n", stats.cplane_err_sect_ext_chain);
  printf("  C-Plane Malformed Section Extension Other Errors: %lu\n", stats.cplane_err_sect_ext_other);

  pcap_close(pcap);
  cleanup_packet_processor(ctx);
  if (mp)
    rte_mempool_free(mp);
  for (int i = 0; i < MAX_ANTENNAS; i++)
    free(txdataF[i]);
  free(dl_iq_arena);

  if (stats.cplane_err_sect_ext1 != 0 || stats.cplane_err_sect_ext11 != 0 || stats.cplane_err_sect_ext_chain != 0
      || stats.cplane_err_sect_ext_other != 0) {
    printf("FAIL: %lu malformed ext1, %lu malformed ext11, %lu malformed extension chains, %lu malformed other extensions\n",
           stats.cplane_err_sect_ext1,
           stats.cplane_err_sect_ext11,
           stats.cplane_err_sect_ext_chain,
           stats.cplane_err_sect_ext_other);
    return 1;
  }
  if (stats.cplane_ext1_received < min_ext1_received) {
    printf("FAIL: %lu ext1 received, expected at least %lu\n", stats.cplane_ext1_received, min_ext1_received);
    return 1;
  }
  // Every extension in the reference window must also have reached the processor
  uint64_t ext_received = ref_ext_type == 11 ? stats.cplane_ext11_received : stats.cplane_ext1_received;
  if (ref_ext_type && ext_received < ref_exp_cplane) {
    printf("FAIL: %lu ext%d received by the processor, expected at least %lu\n", ext_received, ref_ext_type, ref_exp_cplane);
    return 1;
  }
  if (ref_ext_type && !ref_report(ref_exp_cplane, ref_exp_uplane)) {
    printf("FAIL: xran ext%d reference capture checks\n", ref_ext_type);
    return 1;
  }
  return 0;
}
