/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "common/config/config_userapi.h"
#include "common/utils/system.h"
#include "nr-oru.h"
#include "openair1/PHY/defs_nr_common.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "oru_packet_processor.h"
#include "common/utils/threadPool/thread-pool.h"
#include "common/utils/ds/work_q.h"
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "openair1/PHY/MODULATION/phy_ofdm_mod.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"

#define CONFIG_SECTION_ORU "ORUs.[0]"

#define CONFIG_STRING_ORU_TX_BW_LIST "tx_bw"
#define CONFIG_STRING_ORU_RX_BW_LIST "rx_bw"
#define CONFIG_STRING_ORU_CARRIER_TX_LIST "carrier_tx"
#define CONFIG_STRING_ORU_CARRIER_RX_LIST "carrier_rx"
#define CONFIG_STRING_ORU_FRAME_TYPE "frame_type"
#define CONFIG_STRING_ORU_PRACH_CONFIGID "prach_config_index"
#define CONFIG_STRING_ORU_PRACH_MSG1FREQ "prach_msg1_start"
#define CONFIG_STRING_ORU_NUMEROLOGY "mu"
#define CONFIG_STRING_ORU_TDD_PERIOD "tdd_period"
#define CONFIG_STRING_ORU_NUM_DL_SLOTS "num_dl_slots"
#define CONFIG_STRING_ORU_NUM_UL_SLOTS "num_ul_slots"
#define CONFIG_STRING_ORU_NUM_DL_SYMBOLS "num_dl_symbols"
#define CONFIG_STRING_ORU_NUM_UL_SYMBOLS "num_ul_symbols"
#define CONFIG_STRING_NB_FH_STREAMS "nb_fh_streams"
#define CONFIG_STRING_CODEBOOK_NB_BEAMS "codebook_nb_beams"
#define CONFIG_STRING_CODEBOOK_WEIGHTS "codebook_weights"
#define CONFIG_STRING_UL_CODEBOOK_WEIGHTS "ul_codebook_weights"
#define CONFIG_STRING_ORU_TX_CORE "tx_core"

#define HLP_ORU_TX_BW "set the TX bandwidth list per component carrier"
#define HLP_ORU_RX_BW "set the RX bandwidth list per component carrier"
#define HLP_ORU_CARRIER_TX "set the TX carrier frequencies per component carrier"
#define HLP_ORU_CARRIER_RX "set the RX carrier frequencies per component carrier"
#define HLP_ORU_FRAMETYPE "set the Frame type TDD/FDD of all component carriers"
#define HLP_ORU_PRACH_CONFIGID "set the PRACH configuration id of all component carriers"
#define HLP_ORU_PRACH_MSG1FREQ "set the PRACH MSG1 frequency of all component carriers"
#define HLP_ORU_NUMEROLOGY "set the numerology of the RU"
#define HLP_ORU_TDD_PERIOD "set the 3GPP TDD periodificty 0-9"
#define HLP_ORU_NUM_DL_SLOTS "set the number of DL Slots in TDD"
#define HLP_ORU_NUM_UL_SLOTS "set the number of UL Slots in TDD"
#define HLP_ORU_NUM_DL_SYMBOLS "set the number of DL symbols in the mixed slot"
#define HLP_ORU_NUM_UL_SYMBOLS "set the number of UL symbols in the mixed slot"
#define HLP_NB_FH_STREAMS "number of fronthaul streams from DU (0=passthrough, enables codebook beamforming when > 0)"
#define HLP_CODEBOOK_NB_BEAMS "number of beams in the O-RU codebook"
#define HLP_CODEBOOK_WEIGHTS                                                                         \
  "Q15 codebook weights: nb_beams * nb_tx * nb_fh_streams interleaved Re/Im pairs; per beam, the tx" \
  " antenna row, conjugated, is also the UL Rx combine weight for that RX antenna unless ul_codebook_weights is set"
#define HLP_UL_CODEBOOK_WEIGHTS                                                                                               \
  "optional UL Rx combine weights, same layout as codebook_weights (conjugated per RX antenna); unset: use codebook_weights." \
  " Lets DL fan one stream out to all antennas while UL keeps one stream per antenna"
#define HLP_ORU_THREEQUARTER_FS "set the 3/4 sampling frequency"

// clang-format off
#define CMDLINE_PARAMS_DESC_ORU \
{ \
  {CONFIG_STRING_ORU_TX_BW_LIST,                HLP_ORU_TX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_RX_BW_LIST,                HLP_ORU_RX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_TX_LIST,           HLP_ORU_CARRIER_TX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_RX_LIST,           HLP_ORU_CARRIER_RX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_FRAME_TYPE,                HLP_ORU_FRAMETYPE,                  0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_CONFIGID,            HLP_ORU_PRACH_CONFIGID,             0,    .uptr=NULL,       .defintval=152,               TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_MSG1FREQ,            HLP_ORU_PRACH_MSG1FREQ,             0,    .uptr=NULL,       .defintval=0,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUMEROLOGY,                HLP_ORU_NUMEROLOGY,                 0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_TDD_PERIOD,                HLP_ORU_TDD_PERIOD,                 0,    .uptr=NULL,       .defintval=5,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SLOTS,              HLP_ORU_NUM_DL_SLOTS,               0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SLOTS,              HLP_ORU_NUM_UL_SLOTS,               0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SYMBOLS,            HLP_ORU_NUM_DL_SYMBOLS,             0,    .uptr=NULL,       .defintval=7,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SYMBOLS,            HLP_ORU_NUM_UL_SYMBOLS,             0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_NB_FH_STREAMS,                 HLP_NB_FH_STREAMS,                  0,    .iptr=NULL,       .defintval=0,                 TYPE_INT,          0}, \
  {CONFIG_STRING_CODEBOOK_NB_BEAMS,             HLP_CODEBOOK_NB_BEAMS,              0,    .iptr=NULL,       .defintval=0,                 TYPE_INT,          0}, \
  {CONFIG_STRING_CODEBOOK_WEIGHTS,              HLP_CODEBOOK_WEIGHTS,               0,    .iptr=NULL,       .defintarrayval=NULL,          TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_UL_CODEBOOK_WEIGHTS,           HLP_UL_CODEBOOK_WEIGHTS,            0,    .iptr=NULL,       .defintarrayval=NULL,          TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_ORU_TX_CORE,                   "The CPU core to be used to deploy south write thread for O-RU.", 0, .iptr=NULL, .defintval=-1, TYPE_INT, 0}, \
}

#define CMDLINE_PARAMS_DESC_ORU_COMMON \
{ \
  {"E", "set the 3/4 sampling frequency",  PARAMFLAG_BOOL, .iptr=NULL, .defintval=0, TYPE_INT,    0}, \
}
// clang-format on

#define CONFIG_SECTION_ORU_FH "ORUs.[0].fronthaul"

#define CONFIG_STRING_ORU_DPDK_DEVICES "dpdk_devices"
#define CONFIG_STRING_RX_CORE "rx_core"
#define CONFIG_STRING_NORTH_CORES "north_cores"
#define CONFIG_STRING_SOUTH_CORE "south_core"
#define CONFIG_STRING_UL_WORKER_CORES "ul_worker_cores"
#define CONFIG_STRING_EXTRA_EAL_ARGS "extra_eal_args"
#define CONFIG_STRING_DU_MAC_ADDRESSES "du_mac_addr"
#define CONFIG_STRING_MTU "mtu"
#define CONFIG_STRING_T2A_UP "T2a_up"
#define CONFIG_STRING_T2A_CP "T2a_cp"
#define CONFIG_STRING_PRACH_EAXC_OFFSET "prach_eaxc_offset"
#define CONFIG_STRING_PRACH_KBAR "prach_kbar"
#define CONFIG_STRING_COMP_TYPE         "comp_type"
#define CONFIG_STRING_CLOCK_TIMEBASE    "clock_timebase"

#define HLP_DPDK_DEVICES "DPDK devices to use for the O-RU."
#define HLP_RX_CORE "The CPU core to be used to deploy dpdk RX worker for O-RU."
/* North/south/UL workers are RT-max; pin them or they roam into vrtsim cores. */
#define HLP_NORTH_CORES                                                                                                       \
  "CPU cores for O-RU north reader threads (one entry per thread, -1 = unpinned). List length sets the number of DL readers " \
  "(default [-1] = one unpinned reader)."
#define HLP_SOUTH_CORE "CPU core for the O-RU south (UL) reader thread; -1 for no pinning."
#define HLP_UL_WORKER_CORES "CPU cores for the UL Tpool workers; empty falls back to RUs.tp_cores."
#define HLP_EXTRA_EAL_ARGS "Extra arguments passed to RTE_EAL_INIT."
#define HLP_DU_MAC_ADDRESSES "DU MAC addreses, used to prepare Ethernet headers."
#define HLP_MTU "MTU for RX and TX."
#define HLP_PRACH_EAXC_OFFSET "PRACH eAxC offset."
#define HLP_PRACH_KBAR "PRACH sequence offset in the PRACH PRBs, in int16; default: 2 * k_bar of TS 38.211 Table 6.3.3.2-1"
#define HLP_COMP_TYPE         "DL U-plane compression method: none, bfp, blkscale, or ulaw."
#define HLP_CLOCK_TIMEBASE \
  "Timescale of CLOCK_REALTIME for FH GPS mapping: utc (default, GPS = unix - epoch + 18) or tai (GPS = unix - epoch - 19)."

#define COMP_TYPE_CHECK                                                               \
  &(checkedparam_t)                                                                   \
  {                                                                                   \
    .s3a = { config_checkstr_assign_integer, {"none", "bfp", "blkscale", "ulaw"}, {0, 1, 2, 3}, 4 } \
  }

#define CLOCK_TIMEBASE_CHECK                                                          \
  &(checkedparam_t)                                                                   \
  {                                                                                   \
    .s3a = { config_checkstr_assign_integer, {"utc", "tai"}, {0, 1}, 2 }               \
  }

// clang-format off
#define CMDLINE_PARAMS_DESC_ORU_FH \
{ \
  {CONFIG_STRING_ORU_DPDK_DEVICES,           HLP_DPDK_DEVICES,      PARAMFLAG_MANDATORY,    .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_RX_CORE,                    HLP_RX_CORE,           PARAMFLAG_MANDATORY,    .iptr=NULL,       .defintval=-1,                TYPE_INT,          0}, \
  {CONFIG_STRING_NORTH_CORES,                HLP_NORTH_CORES,       0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_SOUTH_CORE,                 HLP_SOUTH_CORE,        0,                      .iptr=NULL,       .defintval=-1,                TYPE_INT,          0}, \
  {CONFIG_STRING_UL_WORKER_CORES,            HLP_UL_WORKER_CORES,   0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_EXTRA_EAL_ARGS,             HLP_EXTRA_EAL_ARGS,    0,                      .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_DU_MAC_ADDRESSES,           HLP_DU_MAC_ADDRESSES,  PARAMFLAG_MANDATORY,    .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_MTU,                        HLP_MTU,               0,                      .iptr=NULL,       .defintval=9600,              TYPE_INT,          0}, \
  {CONFIG_STRING_T2A_UP,                     "",                    0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_T2A_CP,                     "",                    0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_PRACH_EAXC_OFFSET,          HLP_PRACH_EAXC_OFFSET, 0,                      .u8ptr=NULL,      .defuintval=0,                TYPE_UINT8,        0}, \
  {CONFIG_STRING_PRACH_KBAR,                 HLP_PRACH_KBAR,        0,                      .uptr=NULL,       .defuintval=0,                TYPE_UINT,         0}, \
  {CONFIG_STRING_COMP_TYPE,                  HLP_COMP_TYPE,         0,                      .strptr=NULL,     .defstrval="none",           TYPE_STRING,       0, COMP_TYPE_CHECK}, \
  {CONFIG_STRING_CLOCK_TIMEBASE,             HLP_CLOCK_TIMEBASE,    0,                      .strptr=NULL,     .defstrval="utc",            TYPE_STRING,       0, CLOCK_TIMEBASE_CHECK}  \
}
// clang-format on

#define UL_SYMBOL_MISS_LOG_RATELIMIT 10000
#define UL_CAL_ERR_LOG_RATELIMIT 10000
#define RATELIMIT(n, block)                                                               \
  do {                                                                                    \
    static _Atomic unsigned long counter = 0;                                             \
    unsigned long current = atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed); \
    if (current % (n) == 0) {                                                             \
      block                                                                               \
    }                                                                                     \
  } while (0)

static void dispatch_ul_fft(work_q_t *fft_q,
                            work_q_t *beam_q,
                            ORU_t *oru,
                            int frame,
                            int slot,
                            int symbol,
                            const ul_job_t *jobs,
                            int nb_jobs);

// ---------------------------------------------------------------------------
// UL job calendar: ring of UL_CAL_SLOTS slots x 14 symbols, UL_CAL_JOBS_PER_SYMBOL jobs per symbol
// entry. Replaces the flat pending_ul[64] array: job arrival parks at its due symbol's entry in
// O(1) instead of a linear scan, and the per-symbol tick drains exactly the jobs due now.
//
#define UL_CAL_SLOTS 4 // Number of slots ahead in the UL calendar ring
#define UL_CAL_JOBS_PER_SYMBOL 32 // one per C-Plane section and stream starting on a symbol

typedef struct {
  ul_job_t job;
  int symbols_sent; // symbols already dispatched (multi-symbol C-plane jobs)
  bool active;
} ul_cal_entry_t;

typedef struct {
  ul_cal_entry_t entries[UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT][UL_CAL_JOBS_PER_SYMBOL];
} ul_calendar_t;

static uint64_t ul_cal_abs_symbol(int frame, int slot, int symbol, int slots_per_frame)
{
  return ((uint64_t)frame * slots_per_frame + slot) * NR_SYMBOLS_PER_SLOT + symbol;
}

// Shortest-path difference between two absolute symbols (corrects for the 1024-frame wrap).
static int64_t ul_cal_symbol_diff(uint64_t a, uint64_t b, int slots_per_frame)
{
  const int64_t total = (int64_t)1024 * slots_per_frame * NR_SYMBOLS_PER_SLOT;
  int64_t diff = (int64_t)a - (int64_t)b;
  if (diff < -total / 2) {
    diff += total;
  } else if (diff > total / 2) {
    diff -= total;
  }
  return diff;
}

// Park a job on its due symbol's entry (used both for fresh arrivals and for multi-symbol
// continuations). Returns false, counting a drop, if that symbol's entry is full.
static bool ul_calendar_park(ul_calendar_t *cal, ORU_t *oru, const ul_job_t *job, int symbols_sent, uint64_t abs_symbol)
{
  const int idx = abs_symbol % (UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT);
  for (int i = 0; i < UL_CAL_JOBS_PER_SYMBOL; i++) {
    if (!cal->entries[idx][i].active) {
      cal->entries[idx][i] = (ul_cal_entry_t){.job = *job, .symbols_sent = symbols_sent, .active = true};
      return true;
    }
  }
  __atomic_fetch_add(&oru->ul_cal_overflow_dropped, 1, __ATOMIC_RELAXED);
  RATELIMIT(UL_CAL_ERR_LOG_RATELIMIT, {
    LOG_W(PHY, "[ORU south] UL calendar entry full (%d jobs) at symbol %lu, dropping job ant=%d\n",
          UL_CAL_JOBS_PER_SYMBOL, (unsigned long)abs_symbol, job->antenna_id);
  });
  return false;
}

// Dispatch the next `count` symbols of a job, one FFT job per symbol (it dispatches the beam items
// itself, so the per-antenna FFTs are computed once).
static void ul_calendar_dispatch(work_q_t *beam_q, work_q_t *fft_q, ORU_t *oru, const ul_job_t *job, int symbols_sent, int count)
{
  const int sym = job->symbol + symbols_sent;
  for (int k = 0; k < count; k++) {
    dispatch_ul_fft(fft_q, beam_q, oru, job->frame, job->slot_in_frame, sym + k, job, 1);
  }
}

// Route a freshly-polled UL job: dispatch right away if it is already due, park the rest at its
// due symbol. Jobs more than a slot late are dropped (their radio data is long gone).
static void ul_calendar_add(ul_calendar_t *cal, work_q_t *beam_q, work_q_t *fft_q, ORU_t *oru, uint64_t reader_abs, const ul_job_t *job)
{
  const int slots_per_frame = oru->ru->nr_frame_parms->slots_per_frame;
  const uint64_t job_abs = ul_cal_abs_symbol(job->frame, job->slot_in_frame, job->symbol, slots_per_frame);
  const int64_t diff = ul_cal_symbol_diff(job_abs, reader_abs, slots_per_frame);

  if (diff <= -NR_SYMBOLS_PER_SLOT) {
    __atomic_fetch_add(&oru->ul_symbols_missed, 1, __ATOMIC_RELAXED);
    RATELIMIT(UL_SYMBOL_MISS_LOG_RATELIMIT, {
      LOG_W(PHY, "[ORU south] missed UL symbol %d.%d.%d, dropping job ant=%d\n",
            job->frame, job->slot_in_frame, job->symbol, job->antenna_id);
    });
    return;
  }
  if (diff >= UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT) {
    __atomic_fetch_add(&oru->ul_cal_horizon_dropped, 1, __ATOMIC_RELAXED);
    RATELIMIT(UL_CAL_ERR_LOG_RATELIMIT, {
      LOG_W(PHY, "[ORU south] UL job %d.%d.%d declared %ld symbols ahead (calendar horizon %d), dropping\n",
            job->frame, job->slot_in_frame, job->symbol, (long)diff, UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT);
    });
    return;
  }

  // Dispatch every symbol already due (the last one's radio data is in the buffer), park the rest.
  int due = diff < 0 ? (int)(-diff) + 1 : 0;
  if (due > job->num_symbols) {
    due = job->num_symbols;
  }
  if (due > 0) {
    ul_calendar_dispatch(beam_q, fft_q, oru, job, 0, due);
  }
  if (due < job->num_symbols) {
    ul_calendar_park(cal, oru, job, due, job_abs + due);
  }
}

// Per-symbol tick: dispatch one symbol of every job parked on the current symbol's entry, re-park
// multi-symbol continuations on the next symbol's entry. The symbol's whole job set goes into ONE
// FFT job - the per-antenna FFTs are computed once and every job (section) of the symbol shares them.
static void ul_calendar_tick(ul_calendar_t *cal, work_q_t *beam_q, work_q_t *fft_q, ORU_t *oru, uint64_t reader_abs)
{
  const int idx = reader_abs % (UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT);
  ul_job_t batch[UL_CAL_JOBS_PER_SYMBOL];
  int nb_batch = 0;
  for (int i = 0; i < UL_CAL_JOBS_PER_SYMBOL; i++) {
    ul_cal_entry_t *entry = &cal->entries[idx][i];
    if (!entry->active) {
      continue;
    }
    batch[nb_batch++] = entry->job;
    if (entry->symbols_sent + 1 < entry->job.num_symbols) {
      ul_calendar_park(cal, oru, &entry->job, entry->symbols_sent + 1, reader_abs + 1);
    }
    entry->active = false;
  }
  if (nb_batch > 0) {
    // All jobs on this entry are due at the entry's own symbol - decode it for the FFT job.
    const int sym_per_slot = NR_SYMBOLS_PER_SLOT;
    const int slots_per_frame = oru->ru->nr_frame_parms->slots_per_frame;
    const uint64_t per_frame = (uint64_t)slots_per_frame * sym_per_slot;
    dispatch_ul_fft(fft_q, beam_q, oru, reader_abs / per_frame, (reader_abs % per_frame) / sym_per_slot,
                    reader_abs % sym_per_slot, batch, nb_batch);
  }
}


extern void set_scs_parameters(NR_DL_FRAME_PARMS *fp, int mu, int N_RB_DL, int ssb_case);
int tx_rf_symbols(RU_t *ru, int frame, int slot, uint64_t timestamp, int start_symbol, int num_symbols);

void prepare_prach_item(ORU_t *oru)
{
  AssertFatal(oru->ru != NULL, "ORU not configured\n");
  AssertFatal(oru->ru->nr_frame_parms != NULL, "ORU not configured\n");
  NR_DL_FRAME_PARMS *fp = oru->ru->nr_frame_parms;
  RU_t *ru = oru->ru;
  prach_item_t *prach_item = &oru->prach_item;
  prach_item->num_slots = oru->prach_info.format < 4 ? get_long_prach_dur(oru->prach_info.format, fp->numerology_index) : 1;
  prach_item->msg1_frequencystart = oru->prach_msg1_freq;
  prach_item->mu = fp->numerology_index;
  nfapi_nr_config_request_scf_t *cfg = &ru->config;
  prach_item->prach_sequence_length = cfg->prach_config.prach_sequence_length.value;
  prach_item->restricted_set = 0;
  prach_item->numerology_index = fp->numerology_index;
  prach_item->nb_rx = ru->nb_rx;
  prach_item->rx_prach = &oru->rx_prach;

  // Fill PRACH PDU
  nfapi_nr_prach_pdu_t *prach_pdu = &prach_item->pdu;
  prach_pdu->prach_start_symbol = oru->prach_info.start_symbol;
  prach_pdu->num_prach_ocas = 1; // TODO: Hardcoded.

  uint16_t format0 = oru->prach_info.format & 0xff;
  uint16_t format1 = (oru->prach_info.format >> 8) & 0xff;
  if (format1 != 0xff) {
    switch (format0) {
      case 0xa1:
        prach_pdu->prach_format = 11;
        break;
      case 0xa2:
        prach_pdu->prach_format = 12;
        break;
      case 0xa3:
        prach_pdu->prach_format = 13;
        break;
      default:
        AssertFatal(1 == 0, "Only formats A1/B1 A2/B2 A3/B3 are valid for dual format");
    }
  } else {
    switch (format0) {
      case 0:
        prach_pdu->prach_format = 0;
        break;
      case 1:
        prach_pdu->prach_format = 1;
        break;
      case 2:
        prach_pdu->prach_format = 2;
        break;
      case 3:
        prach_pdu->prach_format = 3;
        break;
      case 0xa1:
        prach_pdu->prach_format = 4;
        break;
      case 0xa2:
        prach_pdu->prach_format = 5;
        break;
      case 0xa3:
        prach_pdu->prach_format = 6;
        break;
      case 0xb1:
        prach_pdu->prach_format = 7;
        break;
      case 0xb4:
        prach_pdu->prach_format = 8;
        break;
      case 0xc0:
        prach_pdu->prach_format = 9;
        break;
      case 0xc2:
        prach_pdu->prach_format = 10;
        break;
      default:
        AssertFatal(1 == 0, "Invalid PRACH format");
    }
  }
}

// Fill cb->w from a codebook weight array (nb_beams x nb_tx x nb_fh_streams Re/Im pairs) of cb's dimensions
static void read_codebook_weights(const paramdef_t *wgt, int nb_tx, oru_codebook_t *cb)
{
  const int expected = cb->nb_beams * nb_tx * cb->nb_fh_streams * 2;
  AssertFatal(wgt->numelt == expected,
              "%s: expected %d elements (nb_beams=%d * nb_tx=%d * nb_fh_streams=%d * 2)\n",
              wgt->optname,
              expected,
              cb->nb_beams,
              nb_tx,
              cb->nb_fh_streams);
  int idx = 0;
  for (int b = 0; b < cb->nb_beams; b++)
    for (int t = 0; t < nb_tx; t++)
      for (int s = 0; s < cb->nb_fh_streams; s++) {
        AssertFatal(wgt->iptr[idx] >= INT16_MIN && wgt->iptr[idx] <= INT16_MAX,
                    "%s[%d] value %d out of Q15 range\n",
                    wgt->optname,
                    idx,
                    wgt->iptr[idx]);
        cb->w[b][t][s].r = (int16_t)wgt->iptr[idx++];
        AssertFatal(wgt->iptr[idx] >= INT16_MIN && wgt->iptr[idx] <= INT16_MAX,
                    "%s[%d] value %d out of Q15 range\n",
                    wgt->optname,
                    idx,
                    wgt->iptr[idx]);
        cb->w[b][t][s].i = (int16_t)wgt->iptr[idx++];
      }
}

int get_oru_options(ORU_t *oru)
{
  int DEFBW[] = {273};
  int DEFCARRIER[] = {3430560};
  paramdef_t param[] = CMDLINE_PARAMS_DESC_ORU;
  int nump = sizeofArray(param);

  int ret = config_get(config_get_if(), param, nump, CONFIG_SECTION_ORU);
  if (ret <= 0) {
    LOG_E(NR_PHY, "problem reading section \"%s\"\n", CONFIG_SECTION_ORU);
    return -1;
  }

  for (int i = 0; i < oru->ru->num_bands; i++) {
    oru->bw_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_TX_BW_LIST)->iptr[i];
    oru->bw_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_RX_BW_LIST)->iptr[i];
    oru->carrier_freq_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_TX_LIST)->iptr[i];
    oru->carrier_freq_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_RX_LIST)->iptr[i];
  }
  oru->frame_type = *gpd(param, nump, CONFIG_STRING_ORU_FRAME_TYPE)->iptr;
  oru->prach_config_index = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_CONFIGID)->iptr;
  oru->prach_msg1_freq = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_MSG1FREQ)->iptr;
  oru->numerology = *gpd(param, nump, CONFIG_STRING_ORU_NUMEROLOGY)->iptr;
  oru->tdd_period = *gpd(param, nump, CONFIG_STRING_ORU_TDD_PERIOD)->iptr;
  oru->num_DL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SLOTS)->iptr;
  oru->num_UL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SLOTS)->iptr;
  oru->num_DL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SYMBOLS)->iptr;
  oru->num_UL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SYMBOLS)->iptr;
  oru->codebook.nb_fh_streams = *gpd(param, nump, CONFIG_STRING_NB_FH_STREAMS)->iptr;
  oru->codebook.nb_beams = *gpd(param, nump, CONFIG_STRING_CODEBOOK_NB_BEAMS)->iptr;
  AssertFatal(oru->codebook.nb_fh_streams >= 0, "nb_fh_streams %d must be non-negative\n", oru->codebook.nb_fh_streams);
  if (oru->codebook.nb_fh_streams > 0) {
    AssertFatal(oru->codebook.nb_fh_streams <= ORU_CODEBOOK_MAX_STREAMS,
                "nb_fh_streams %d exceeds maximum %d\n",
                oru->codebook.nb_fh_streams,
                ORU_CODEBOOK_MAX_STREAMS);
    AssertFatal(oru->codebook.nb_beams > 0 && oru->codebook.nb_beams <= ORU_CODEBOOK_MAX_BEAMS,
                "codebook_nb_beams %d invalid (range 1..%d)\n",
                oru->codebook.nb_beams,
                ORU_CODEBOOK_MAX_BEAMS);
    AssertFatal(oru->ru->nb_tx <= ORU_CODEBOOK_MAX_NB_TX, "nb_tx %d exceeds maximum %d\n", oru->ru->nb_tx, ORU_CODEBOOK_MAX_NB_TX);
    AssertFatal(oru->codebook.nb_fh_streams <= oru->ru->nb_tx,
                "nb_fh_streams %d must not exceed nb_tx %d\n",
                oru->codebook.nb_fh_streams,
                oru->ru->nb_tx);
    // The codebook's antenna axis serves both DL (nb_tx) and UL Rx combining (nb_rx antennas).
    AssertFatal(oru->ru->nb_rx <= oru->ru->nb_tx,
                "codebook beamforming needs nb_rx %d <= nb_tx %d (one weight row per RX antenna)\n",
                oru->ru->nb_rx,
                oru->ru->nb_tx);
    read_codebook_weights(gpd(param, nump, CONFIG_STRING_CODEBOOK_WEIGHTS), oru->ru->nb_tx, &oru->codebook);
    oru->ul_codebook = oru->codebook;
    const paramdef_t *ul_wgt = gpd(param, nump, CONFIG_STRING_UL_CODEBOOK_WEIGHTS);
    if (ul_wgt->numelt > 0)
      read_codebook_weights(ul_wgt, oru->ru->nb_tx, &oru->ul_codebook);
    LOG_I(NR_PHY,
          "Codebook beamforming enabled: nb_fh_streams=%d nb_beams=%d nb_tx=%d, UL weights from %s\n",
          oru->codebook.nb_fh_streams,
          oru->codebook.nb_beams,
          oru->ru->nb_tx,
          ul_wgt->numelt > 0 ? CONFIG_STRING_UL_CODEBOOK_WEIGHTS : CONFIG_STRING_CODEBOOK_WEIGHTS);
  }

  oru->tx_write.core = *gpd(param, nump, CONFIG_STRING_ORU_TX_CORE)->iptr;

  paramdef_t fh_param[] = CMDLINE_PARAMS_DESC_ORU_FH;
  nump = sizeofArray(fh_param);
  oru_fh_config_t *fh_cfg = &oru->fh_config;
  ret = config_get(config_get_if(), fh_param, nump, CONFIG_SECTION_ORU_FH);
  if (ret <= 0) {
    printf("problem reading section \"%s\"\n", CONFIG_SECTION_ORU_FH);
    return -1;
  }

  oru_fh_dpdk_config_t *dpdk_conf = &fh_cfg->dpdk_conf;
  int num_dpdk_devices = gpd(fh_param, nump, CONFIG_STRING_ORU_DPDK_DEVICES)->numelt;
  dpdk_conf->num_dpdk_devices = num_dpdk_devices;
  AssertFatal(num_dpdk_devices > 0 && num_dpdk_devices <= 2,
              "Invalid number of DPDK devices (%d). Configure 1 or 2 devices\n",
              num_dpdk_devices);
  for (int i = 0; i < num_dpdk_devices; i++) {
    dpdk_conf->dpdk_devices[i] = gpd(fh_param, nump, CONFIG_STRING_ORU_DPDK_DEVICES)->strlistptr[i];
  }
  dpdk_conf->extra_eal_args = gpd(fh_param, nump, CONFIG_STRING_EXTRA_EAL_ARGS)->strlistptr;
  dpdk_conf->num_extra_eal_args = gpd(fh_param, nump, CONFIG_STRING_EXTRA_EAL_ARGS)->numelt;

  fh_cfg->num_du_mac_addrs = gpd(fh_param, nump, CONFIG_STRING_DU_MAC_ADDRESSES)->numelt;
  for (int i = 0; i < fh_cfg->num_du_mac_addrs; i++) {
    fh_cfg->du_mac_addrs[i] = gpd(fh_param, nump, CONFIG_STRING_DU_MAC_ADDRESSES)->strlistptr[i];
    AssertFatal(strlen(fh_cfg->du_mac_addrs[i]) == 17, "Invalid MAC address\n");
  }
  int comp_type_idx = config_paramidx_fromname(fh_param, nump, CONFIG_STRING_COMP_TYPE);
  AssertFatal(comp_type_idx >= 0, "Index for %s config option not found!\n", CONFIG_STRING_COMP_TYPE);
  fh_cfg->comp_type = (fh_comp_method_t)config_get_processedint(config_get_if(), &fh_param[comp_type_idx]);
  int clock_tb_idx = config_paramidx_fromname(fh_param, nump, CONFIG_STRING_CLOCK_TIMEBASE);
  AssertFatal(clock_tb_idx >= 0, "Index for %s config option not found!\n", CONFIG_STRING_CLOCK_TIMEBASE);
  fh_cfg->clock_timebase = (fh_clock_timebase_t)config_get_processedint(config_get_if(), &fh_param[clock_tb_idx]);
  fh_cfg->rx_core = *gpd(fh_param, nump, CONFIG_STRING_RX_CORE)->iptr;
  oru->num_dl_read_threads = gpd(fh_param, nump, CONFIG_STRING_NORTH_CORES)->numelt;
  if (oru->num_dl_read_threads == 0) {
    oru->num_dl_read_threads = 1;
    oru->north_cores[0] = -1;
  } else {
    AssertFatal(oru->num_dl_read_threads <= MAX_DL_READ_THREADS,
                "%s has %d entries; need [1, %d] (one core per DL reader, -1 to leave unpinned)\n",
                CONFIG_STRING_NORTH_CORES,
                oru->num_dl_read_threads,
                MAX_DL_READ_THREADS);
    for (int i = 0; i < oru->num_dl_read_threads; i++)
      oru->north_cores[i] = gpd(fh_param, nump, CONFIG_STRING_NORTH_CORES)->iptr[i];
  }
  oru->south_core = *gpd(fh_param, nump, CONFIG_STRING_SOUTH_CORE)->iptr;
  oru->num_ul_worker_cores = gpd(fh_param, nump, CONFIG_STRING_UL_WORKER_CORES)->numelt;
  AssertFatal(oru->num_ul_worker_cores <= MAX_ORU_UL_WORKERS,
              "%s has %d entries, at most %d supported\n",
              CONFIG_STRING_UL_WORKER_CORES,
              oru->num_ul_worker_cores,
              MAX_ORU_UL_WORKERS);
  for (int i = 0; i < oru->num_ul_worker_cores; i++)
    oru->ul_worker_cores[i] = gpd(fh_param, nump, CONFIG_STRING_UL_WORKER_CORES)->iptr[i];
  fh_cfg->mtu = *gpd(fh_param, nump, CONFIG_STRING_MTU)->iptr;
  fh_cfg->num_prbs = oru->bw_tx[0];
  fh_cfg->numerology = oru->numerology;
  fh_cfg->prach_eaxc_offset = *gpd(fh_param, nump, CONFIG_STRING_PRACH_EAXC_OFFSET)->u8ptr;
  const paramdef_t *kbar = gpd(fh_param, nump, CONFIG_STRING_PRACH_KBAR);
  fh_cfg->prach_kbar = kbar->paramflags & PARAMFLAG_PARAMSET ? *kbar->uptr : -1;

  AssertFatal(gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->numelt == 2, "Two parameters required for %s\n", CONFIG_STRING_T2A_UP);
  fh_cfg->T2a_up_min_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->iptr[0];
  fh_cfg->T2a_up_max_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->iptr[1];
  AssertFatal(fh_cfg->T2a_up_min_uS <= fh_cfg->T2a_up_max_uS,
              "T2a max (%d) has to be greater than T2a min (%d)\n",
              fh_cfg->T2a_up_max_uS,
              fh_cfg->T2a_up_min_uS);

  AssertFatal(gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->numelt == 2, "Two parameters required for %s\n", CONFIG_STRING_T2A_CP);
  fh_cfg->T2a_cp_min_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->iptr[0];
  fh_cfg->T2a_cp_max_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->iptr[1];
  AssertFatal(fh_cfg->T2a_cp_min_uS <= fh_cfg->T2a_cp_max_uS,
              "T2a max (%d) has to be greater than T2a min (%d)\n",
              fh_cfg->T2a_cp_max_uS,
              fh_cfg->T2a_cp_min_uS);

  oru_fh_tdd_pattern_t *tdd_pattern = &fh_cfg->tdd_pattern;
  tdd_pattern->num_dl_slots = oru->num_DL_slots;
  tdd_pattern->num_ul_slots = oru->num_UL_slots;
  tdd_pattern->num_dl_symbols = oru->num_DL_symbols;
  tdd_pattern->num_ul_symbols = oru->num_UL_symbols;
  int num_slots_frame = (1 << oru->numerology) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  int num_period_frame = get_nb_periods_per_frame(oru->tdd_period);
  int num_slots_period = num_slots_frame / num_period_frame;
  tdd_pattern->tdd_pattern_length_slots = num_slots_period;

  paramdef_t common_param[] = CMDLINE_PARAMS_DESC_ORU_COMMON;
  nump = sizeofArray(common_param);
  ret = config_get(config_get_if(), common_param, nump, NULL);
  oru->threequarter_fs = *gpd(common_param, nump, "E")->iptr;

  return 0;
}

void oru_init_frame_parms(ORU_t *oru)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  fp->frame_type = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.value = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.tl.tag = 0x100D;
  fp->N_RB_DL = oru->bw_tx[0];
  ru->config.ssb_config.scs_common.value = ru->numerology;
  ru->config.carrier_config.dl_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_tx[0];
  fp->N_RB_UL = oru->bw_rx[0];
  ru->config.carrier_config.ul_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_rx[0];
  fp->numerology_index = ru->numerology;
  fp->threequarter_fs = oru->threequarter_fs;
  LOG_I(NR_PHY,
        "Set RU frame type to %s, N_RB_DL %d, N_RB_UL %d, mu %d\n",
        oru->frame_type == TDD ? "TDD" : "FDD",
        oru->bw_tx[0],
        oru->bw_rx[0],
        ru->numerology);
  set_scs_parameters(fp, fp->numerology_index, oru->bw_tx[0], 0);
  fp->slots_per_frame = 10 * fp->slots_per_subframe;
  fp->nb_antennas_rx = ru->nb_rx;
  fp->nb_antennas_tx = ru->nb_tx;
  fp->symbols_per_slot = 14;
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot * fp->ofdm_symbol_size;
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_slot0 =
      fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples) + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = (oru->carrier_freq_tx[0] < 6e6) ? FR1 : FR2;

  fp->dl_CarrierFreq = (double)oru->carrier_freq_tx[0] * 1000;
  fp->ul_CarrierFreq = (double)oru->carrier_freq_rx[0] * 1000;
  fp->Ncp = NORMAL;
  fp->ofdm_offset_divisor = 8;

  // Split 7.2 parameters
  ru->config.prach_config.num_prach_fd_occasions.value = 1;
  ru->config.prach_config.prach_ConfigurationIndex.value = oru->prach_config_index;
  ru->config.prach_config.prach_ConfigurationIndex.tl.tag = 0x1029;
  ru->config.prach_config.num_prach_fd_occasions_list = malloc(sizeof(*ru->config.prach_config.num_prach_fd_occasions_list));
  ru->config.prach_config.num_prach_fd_occasions_list[0].k1.value = oru->prach_msg1_freq;
  if (ru->config.cell_config.frame_duplex_type.value == 1 /* TDD */) {
    ru->config.tdd_table.tdd_period.value = oru->tdd_period;
    ru->config.tdd_table.tdd_period.tl.tag = 0x1026;
    int numb_slots_frame = (1 << ru->numerology) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
    int numb_period_frame = get_nb_periods_per_frame(oru->tdd_period);
    int numb_slots_period = numb_slots_frame / numb_period_frame;
    ru->config.tdd_table.max_tdd_periodicity_list =
        malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list) * (numb_slots_frame));
    for (int n = 0; n < numb_slots_frame; n++) {
      int s = 0;
      int p = n % numb_slots_period;
      nfapi_nr_max_tdd_periodicity_t *periodicity = &ru->config.tdd_table.max_tdd_periodicity_list[n];
      periodicity->max_num_of_symbol_per_slot_list =
          malloc(sizeof(*periodicity->max_num_of_symbol_per_slot_list) * NR_SYMBOLS_PER_SLOT);
      nfapi_nr_max_num_of_symbol_per_slot_t *symbol_list = periodicity->max_num_of_symbol_per_slot_list;
      if (p < oru->num_DL_slots) {
        for (s = 0; s < 14; s++)
          symbol_list[s].slot_config.value = 0;
      } else if (p == oru->num_DL_slots) {
        for (s = 0; s < oru->num_DL_symbols; s++)
          symbol_list[s].slot_config.value = 0;
        for (; s < NR_SYMBOLS_PER_SLOT - oru->num_UL_symbols; s++)
          symbol_list[s].slot_config.value = 2;
        for (; s < NR_SYMBOLS_PER_SLOT; s++)
          symbol_list[s].slot_config.value = 1;
      } else {
        for (s = 0; s < NR_SYMBOLS_PER_SLOT; s++)
          symbol_list[s].slot_config.value = 1;
      }
    }
  }
}

void fft_and_cp_insertion(NR_DL_FRAME_PARMS *fp, c16_t *txdataF, c16_t *txdata, int slot, int symbol)
{
  int nb_prefix_samples = fp->nb_prefix_samples;
  if (fp->Ncp != 1) {
    if (fp->numerology_index != 0) {
      if (!(slot % (fp->slots_per_subframe / 2)) && (symbol == 0)) {
        nb_prefix_samples = fp->nb_prefix_samples0;
      }
    } else {
      if (symbol % 0x7 == 0) {
        nb_prefix_samples = fp->nb_prefix_samples0;
      }
    }
  }
  PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, nb_prefix_samples, CYCLIC_PREFIX);
}

static void dl_symbol_completed(ORU_t *oru, uint64_t abs_symbol)
{
  // symbol_reorder_advance() publishes the new contiguous high-water mark and wakes
  // oru_south_write_thread() (blocked in symbol_reorder_wait_at_least()) internally - no separate
  // publish step or mutex needed here.
  symbol_reorder_advance(oru->dl_reorder, abs_symbol, 1);
}

static void dl_symbol_process(ORU_t *oru, int frame, int slot, int symbol, c16_t **txDataF, int64_t timestamp, uint64_t abs_symbol)
{
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  uint32_t slot_offset = get_samples_slot_timestamp(fp, slot);
  uint32_t symbol_offset = get_samples_symbol_timestamp(fp, slot, symbol);

  __attribute__((aligned(64))) c16_t txdataF_shifted[fp->ofdm_symbol_size];
  memset(txdataF_shifted, 0, sizeof(txdataF_shifted));
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    // txDataF is already phase-rotated by combine_dl_streams() - don't rotate again.
    // Inverse FFT shift: contiguous PRB format received from the DU -> layout expected by the IDFT
    fftshift_inverse(txDataF[aatx], txdataF_shifted, fp->N_RB_DL * NR_NB_SC_PER_RB, fp->ofdm_symbol_size);
    fft_and_cp_insertion(ru->nr_frame_parms,
                         txdataF_shifted,
                         (c16_t *)&ru->common.txdata[aatx][slot_offset + symbol_offset],
                         slot,
                         symbol);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000UL + (end.tv_nsec - start.tv_nsec);
  uint64_t elapsed_sq4 = (elapsed_ns * 16ULL) / 1000;
  uint64_t add_val = ((uint64_t)1 << 32) | (elapsed_sq4 & 0xFFFFFFFFULL);
  __atomic_fetch_add(&oru->dl_packed_stats, add_val, __ATOMIC_RELAXED);

  uint64_t current_max = __atomic_load_n(&oru->dl_symbol_time_max_us, __ATOMIC_RELAXED);
  while (elapsed_sq4 > current_max) {
    if (__atomic_compare_exchange_n(&oru->dl_symbol_time_max_us, &current_max, elapsed_sq4, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      break;
    }
  }

  dl_symbol_completed(oru, abs_symbol);
}

// Computes this symbol's phase rotation and hands off to combine_dl_streams() (oru_beamforming.c).
static void assemble_dl_symbol(ORU_t *oru,
                               int slot,
                               int symbol,
                               c16_t **txDataF,
                               int n_rb_dl,
                               const dl_iq_stream_t *streams,
                               int num_streams)
{
  RU_t *ru = (RU_t *)oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  const int n_sc = n_rb_dl * NR_NB_SC_PER_RB;
  c16_t rotation = *(fp->symbol_rotation[0] + (slot % fp->slots_per_subframe) * fp->symbols_per_slot + symbol);

  combine_dl_streams(txDataF, ru->nb_tx, n_sc, streams, num_streams, &oru->codebook, rotation);
}

static pthread_mutex_t south_read_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t south_read_cond = PTHREAD_COND_INITIALIZER;
static bool south_read_ready = false;
static uint64_t notified_hyper_frame = 0;
static uint32_t notified_start_frame = 0;
static uint32_t notified_start_slot = 0;
static openair0_timestamp_t notified_timestamp = 0;

void notify_north_read(uint64_t hyper_frame, uint32_t start_frame, uint32_t start_slot, openair0_timestamp_t timestamp)
{
  pthread_mutex_lock(&south_read_mutex);
  notified_hyper_frame = hyper_frame;
  notified_start_frame = start_frame;
  notified_start_slot = start_slot;
  notified_timestamp = timestamp;
  south_read_ready = true;
  // Broadcast, not signal: multiple DL reader threads may be blocked here at startup, and every
  // one of them needs to wake up and read the notified anchor values.
  pthread_cond_broadcast(&south_read_cond);
  pthread_mutex_unlock(&south_read_mutex);
}

void wait_for_south_read(uint64_t *hyper_frame, uint32_t *start_frame, uint32_t *start_slot, int64_t *timestamp)
{
  pthread_mutex_lock(&south_read_mutex);
  while (!south_read_ready) {
    pthread_cond_wait(&south_read_cond, &south_read_mutex);
  }
  *hyper_frame = notified_hyper_frame;
  *start_frame = notified_start_frame;
  *start_slot = notified_start_slot;
  *timestamp = notified_timestamp;
  pthread_mutex_unlock(&south_read_mutex);
}

// Parallel DL readers share one TX timing anchor under tx_write.mutex.
void *oru_north_read_worker(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;

  RU_t *ru = (RU_t *)oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  __attribute__((aligned(64))) c16_t txDataF[ru->nb_tx][fp->N_RB_DL * NR_NB_SC_PER_RB];
  memset(txDataF, 0, sizeof(txDataF));
  c16_t *txDataF_ptr[ru->nb_tx];
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    txDataF_ptr[aatx] = txDataF[aatx];
  }
  // Scratch for read_dl_iq_streams(): reused every iteration, sized once for the worst case
  // (MAX_DL_FRAGMENTS_PER_SYMBOL fragment descriptors, IQ packed into a full band per stream).
  dl_iq_stream_t dl_streams[MAX_DL_FRAGMENTS_PER_SYMBOL];
  __attribute__((aligned(64))) uint32_t dl_iq_arena[DL_IQ_ARENA_PRBS(fp->N_RB_DL) * NR_NB_SC_PER_RB];
  uint32_t start_frame, start_slot;
  uint64_t start_hyper_frame;
  int64_t start_timestamp;

  pthread_mutex_lock(&oru->tx_write.mutex);
  if (!oru->tx_write.initialized) {
    if (ru->rfdevice.get_timestamp) {
      struct timespec utc_anchor_point;
      oru_fh_get_utc_anchor_point(oru->fronthaul, &start_hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
      start_timestamp = ru->rfdevice.get_timestamp(&ru->rfdevice, &utc_anchor_point);
    } else {
      pthread_mutex_unlock(&oru->tx_write.mutex);
      wait_for_south_read(&start_hyper_frame, &start_frame, &start_slot, &start_timestamp);
      pthread_mutex_lock(&oru->tx_write.mutex);
      if (oru->tx_write.initialized) {
        start_timestamp = oru->tx_write.start_timestamp;
        start_hyper_frame = oru->tx_write.start_hyper_frame;
        goto anchor_ready;
      }
    }
    start_timestamp -= (start_frame * fp->samples_per_frame + get_samples_slot_timestamp(fp, start_slot));
    oru->tx_write.start_timestamp = start_timestamp;
    oru->tx_write.start_hyper_frame = start_hyper_frame;
    oru->tx_write.start_symbol_index =
        start_frame * (fp->slots_per_frame * fp->symbols_per_slot) + start_slot * fp->symbols_per_slot;
    const uint8_t *dl_symbol_bitmask;
    uint16_t bitmask_bit_length;
    oru_fh_get_dl_symbol_bitmask(oru->fronthaul, &dl_symbol_bitmask, &bitmask_bit_length);
    oru->dl_reorder = symbol_reorder_create(oru->tx_write.start_symbol_index, dl_symbol_bitmask, bitmask_bit_length);
    oru->tx_write.initialized = true;
    pthread_cond_broadcast(&oru->tx_write.cond);
  } else {
    start_timestamp = oru->tx_write.start_timestamp;
    start_hyper_frame = oru->tx_write.start_hyper_frame;
  }
anchor_ready:
  pthread_mutex_unlock(&oru->tx_write.mutex);

  LOG_A(PHY,
        "DL thread started: start_timestamp %ld, start_hyper_frame %lu, start_symbol_index %lu\n",
        start_timestamp,
        (unsigned long)start_hyper_frame,
        (unsigned long)oru->tx_write.start_symbol_index);

  while (!oai_exit) {
    int frame = -1, slot = -1, symbol = -1;
    uint64_t hyper_frame;
    int num_streams = oru_fh_tx_read_symbol(oru->fronthaul,
                                            dl_streams,
                                            dl_iq_arena,
                                            MAX_DL_FRAGMENTS_PER_SYMBOL,
                                            &hyper_frame,
                                            &frame,
                                            &slot,
                                            &symbol);
    if (num_streams < 0) {
      LOG_E(PHY, "[RU_thread] read data error: frame %d, slot %d, symbol %d\n", frame, slot, symbol);
      continue;
    }
    if (start_hyper_frame > hyper_frame) {
      continue;
    }
    uint64_t num_frames = (hyper_frame - start_hyper_frame) * 1024 + frame;
    int64_t timestamp = start_timestamp + num_frames * fp->samples_per_frame + get_samples_slot_timestamp(fp, slot)
                        + get_samples_symbol_timestamp(fp, slot, symbol);
    if (timestamp < 0) {
      continue;
    }
    assemble_dl_symbol(oru, slot, symbol, txDataF_ptr, fp->N_RB_DL, dl_streams, num_streams);
    uint64_t abs_symbol = num_frames * (fp->slots_per_frame * fp->symbols_per_slot) + slot * fp->symbols_per_slot + symbol;
    dl_symbol_process(oru, frame, slot, symbol, txDataF_ptr, timestamp, abs_symbol);
    if (frame % 256 == 0 && slot == 0 && symbol == 0) {
      LOG_I(PHY, "[RU_thread] read data: frame %d, slot %d, symbol %d\n", frame, slot, symbol);
    }
  }
  return NULL;
}

// Returns PRACH symbol that was received in current frame, slot and symbol.
// If no PRACH symbol was received, returns -1
int get_prach_symbol(ORU_t *oru, int frame, int slot, int symbol, int numerology)
{
  uint16_t RA_sfn_index;
  AssertFatal(oru->ru->nr_frame_parms->frame_type == TDD, "Only supports TDD\n");
  if (get_nr_prach_sched_from_info(oru->prach_info, oru->prach_config_index, frame, slot, numerology, FR1, &RA_sfn_index, true)) {
    int format = oru->prach_item.pdu.prach_format;
    int start_symbol = oru->prach_item.pdu.prach_start_symbol;
    symbol -= start_symbol;
    // TODO: Support more PRACH formats
    AssertFatal(format == 8, "only support format B4\n");
    // TODO: This is not exactly the case but it is correct
    if (symbol >= 0 && symbol < 12) {
      return symbol;
    }
  }
  return -1;
}

void receive_prach(ORU_t *oru, int frame, int slot, int symbol, int prach_symbol)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  oru->prach_item.frame = frame;
  oru->prach_item.slot = slot;

  c16_t rxdataF[ru->nb_rx][NR_PRACH_SEQ_LEN_L];
  memset(rxdataF, 0, sizeof(rxdataF));

  rx_nr_prach_ru_rep(&oru->prach_item,
                     ru->common.rxdata,
                     fp,
                     ru->N_TA_offset,
                     prach_symbol,
                     0, // prachOccasion
                     rxdataF);

  c16_t *rxdataF_ptr[ru->nb_rx];
  for (int aarx = 0; aarx < ru->nb_rx; aarx++) {
    rxdataF_ptr[aarx] = rxdataF[aarx];
  }
  const oru_codebook_t *cb = &oru->ul_codebook;
  if (cb->nb_fh_streams <= 0) {
    oru_fh_rx_send_prach(oru->fronthaul, (uint32_t **)rxdataF_ptr, ru->nb_rx, frame, slot, symbol);
    return;
  }

  // Codebook mode: PRACH goes out in beam space like PUSCH - one stream per fronthaul stream, each
  // combined with the beam the DU declared for it in the section type 3 C-Plane.
  const int nb_streams = cb->nb_fh_streams;
  c16_t beamF[nb_streams][NR_PRACH_SEQ_LEN_L];
  c16_t *beamF_ptr[nb_streams];
  for (int s = 0; s < nb_streams; s++) {
    beamF_ptr[s] = beamF[s];
    const int beam_id = oru_fh_get_prach_beam_id(oru->fronthaul, slot, s, symbol);
    if (beam_id < 0) {
      memset(beamF[s], 0, sizeof(beamF[s])); // no C-Plane for this stream: write_prach_iq() skips it
      continue;
    }
    combine_ul_beam_fd((const c16_t *const *)rxdataF_ptr, ru->nb_rx, 0, NR_PRACH_SEQ_LEN_L, cb, beam_id, s, beamF[s]);
  }
  oru_fh_rx_send_prach(oru->fronthaul, (uint32_t **)beamF_ptr, nb_streams, frame, slot, symbol);
}

// Sends one UL job (C-Plane section) for a symbol from the symbol's shared per-antenna FFTs. Only the job's PRB range
// is combined, shifted and rotated: write_ul_iq() reads nothing else, so the per-section cost scales with its PRBs.
static void receive_pusch(ORU_t *oru, int frame, int slot, int symbol, ul_job_t *job, const c16_t *const *ant_fft)
{
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int aarx = job->antenna_id;
  const oru_codebook_t *cb = &oru->ul_codebook;
  const bool beamforming = cb->nb_fh_streams > 0;

  if (beamforming) {
    // Codebook mode: the section's eaxc is the beam output stream, not a physical antenna.
    if (aarx < 0 || aarx >= cb->nb_fh_streams) {
      LOG_W(PHY, "[ORU south] receive_pusch: UL beam stream %d exceeds nb_fh_streams %d, dropping\n", aarx, cb->nb_fh_streams);
      return;
    }
  } else if (aarx < 0 || aarx >= ru->nb_rx) {
    LOG_W(PHY, "[ORU south] receive_pusch: invalid antenna_id %d\n", aarx);
    return;
  }

  // The job's PRBs as at most two runs of the unshifted FFT window
  const int nbins = fp->N_RB_UL * NR_NB_SC_PER_RB;
  oru_fft_run_t runs[2];
  const int nruns = oru_prb_fft_runs(fp->ofdm_symbol_size, nbins, job->start_prb, job->num_prb, runs);

  const c16_t *fft;
  c16_t combined[fp->ofdm_symbol_size] __attribute__((aligned(32)));
  if (beamforming) {
    // UL Rx beamforming: combine the per-antenna FFTs via the codebook over this job's PRBs only
    for (int r = 0; r < nruns; r++)
      memset(&combined[runs[r].bin], 0, runs[r].len * sizeof(c16_t));
    combine_ul_beam_prbs(ant_fft,
                         ru->nb_rx,
                         fp->ofdm_symbol_size,
                         nbins,
                         job->start_prb,
                         job->num_prb,
                         cb,
                         job->beam_id,
                         aarx,
                         combined);
    fft = combined;
  } else {
    // No beamforming: the received antenna's FFT, shared by every section of that antenna in this symbol
    fft = ant_fft[aarx];
  }

  // FFT shift: FFT output layout -> contiguous PRB format sent to the DU
  c16_t rxdataF[nbins] __attribute__((aligned(32)));
  int nre = 0;
  for (int r = 0; r < nruns; r++) {
    memcpy(&rxdataF[runs[r].re], &fft[runs[r].bin], runs[r].len * sizeof(c16_t));
    nre += runs[r].len;
  }

  // Phase decompensation (conjugate rotation for UL); PRB offsets keep the 16-byte alignment it needs
  if (nre > 0) {
    const int re0 = runs[0].re;
    apply_nr_rotation_symbol_fftshifted_RX(fp->symbols_per_slot,
                                           fp->slots_per_subframe,
                                           fp->timeshift_symbol_rotation + re0,
                                           rxdataF + re0,
                                           fp->symbol_rotation[link_type_ul],
                                           nre / NR_NB_SC_PER_RB,
                                           slot,
                                           symbol);
  }

  oru_fh_rx_send_pusch(oru->fronthaul, (uint32_t *)rxdataF, symbol, job);

  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000UL + (end.tv_nsec - start.tv_nsec);
  uint64_t elapsed_sq4 = (elapsed_ns * 16ULL) / 1000;
  uint64_t add_val = ((uint64_t)1 << 32) | (elapsed_sq4 & 0xFFFFFFFFULL);
  __atomic_fetch_add(&oru->ul_packed_stats, add_val, __ATOMIC_RELAXED);

  uint64_t current_max = __atomic_load_n(&oru->ul_ant_time_max_us, __ATOMIC_RELAXED);
  while (elapsed_sq4 > current_max) {
    if (__atomic_compare_exchange_n(&oru->ul_ant_time_max_us, &current_max, elapsed_sq4, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      break;
    }
  }
}

// beam items: bound on in-flight UL jobs (backpressure), about 16 symbols of full calendar entries; a power of two
#define UL_WORK_QUEUE_DEPTH (UL_CAL_JOBS_PER_SYMBOL * 16)
#define UL_FFT_QUEUE_DEPTH 32 // FFT items: one per symbol; a few symbols of margin

typedef struct {
  ORU_t *oru;
  int frame;
  int slot;
  int symbol;
  ul_job_t job;
  work_q_t *queue;
  const c16_t *ant_fft[ORU_CODEBOOK_MAX_NB_TX]; // this symbol's per-antenna FFT windows
  void *fft_slot; // the FFT item whose refs this job releases on completion
} ul_work_item_t;

// One FFT job per symbol: computes each RX antenna's FFT once, then dispatches the symbol's UL
// jobs (one per C-Plane section) as beam items. The per-antenna windows live in this item's own queue slot
// (payload after the fixed fields, 32-byte aligned - work_q slots are 32-byte aligned). Beam jobs
// reference them via item pointers, and the slot is released by its last reader (refs), not by
// the FFT job itself - so an FFT job arriving late for an overdue symbol computes into its own
// slot and can never clobber a live one.
typedef struct {
  ORU_t *oru;
  int frame;
  int slot;
  int symbol;
  work_q_t *queue;      // the FFT queue this item lives in (for work_q_done)
  work_q_t *beam_queue; // where the beam jobs go
  int nb_jobs;
  _Atomic(int) refs; // beam jobs outstanding on this slot; the last one returns it
  ul_job_t jobs[UL_CAL_JOBS_PER_SYMBOL];
  c16_t ant_fft[] __attribute__((aligned(32))); // nb_rx * ofdm_symbol_size samples
} ul_fft_item_t;

// Total size of an FFT item including its per-antenna FFT payload (the queue's element size).
static size_t ul_fft_item_size(size_t payload_bytes)
{
  return sizeof(ul_fft_item_t) + payload_bytes;
}

static void ul_worker_pool_task(void *args)
{
  ul_work_item_t *item = args;
  receive_pusch(item->oru, item->frame, item->slot, item->symbol, &item->job, item->ant_fft);
  // Read fft_slot before releasing the beam slot: after work_q_done() a producer may reuse it.
  ul_fft_item_t *fft = item->fft_slot;
  work_q_done(item->queue, item);
  if (fft != NULL) {
    // Release the symbol's FFT slot: its payload stays alive until the last beam job that
    // references it has finished. Read the queue before the decrement - the item is alive while
    // refs >= 1, and the last reader frees it right after.
    work_q_t *q = fft->queue;
    if (atomic_fetch_sub(&fft->refs, 1) == 1) {
      work_q_done(q, fft);
    }
  }
}

// The FFT worker: one FFT per RX antenna for the symbol, then hand each of the symbol's UL jobs
// to the beam path. The payload lives in this item's own queue slot until the last beam job
// releases it (refs), so the slot is safe to reference from every pushed beam item.
static void ul_fft_pool_task(void *args)
{
  ul_fft_item_t *item = args;
  ORU_t *oru = item->oru;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  const int nb_rx = ru->nb_rx;
  const int n = fp->ofdm_symbol_size;

  // Codebook mode combines every RX antenna; passthrough only reads the antennas its jobs name.
  bool used[nb_rx];
  const bool beamforming = oru->codebook.nb_fh_streams > 0;
  for (int a = 0; a < nb_rx; a++)
    used[a] = beamforming;
  for (int i = 0; i < item->nb_jobs; i++) {
    const int a = item->jobs[i].antenna_id;
    if (a >= 0 && a < nb_rx)
      used[a] = true;
  }
  // Window offset, N_TA and frame-boundary wrap all inside nr_symbol_fep_ul().
  for (int a = 0; a < nb_rx; a++) {
    if (!used[a])
      continue;
    nr_symbol_fep_ul(fp, (c16_t *)ru->common.rxdata[a], &item->ant_fft[a * n], item->symbol, item->slot,
                     ru->N_TA_offset);
  }

  item->refs = item->nb_jobs; // plain store: readers only exist after the pushes below
  const c16_t *ant_fft[ORU_CODEBOOK_MAX_NB_TX];
  for (int a = 0; a < nb_rx; a++) {
    ant_fft[a] = &item->ant_fft[a * n];
  }

  work_q_t *q = item->beam_queue;
  for (int i = 0; i < item->nb_jobs; i++) {
    ul_work_item_t beam = {
        .oru = oru,
        .frame = item->frame,
        .slot = item->slot,
        .symbol = item->symbol,
        .job = item->jobs[i],
        .queue = q,
        .fft_slot = item,
    };
    memcpy(beam.ant_fft, ant_fft, nb_rx * sizeof(const c16_t *));
    void *slot = work_q_push(q, &beam);
    if (slot == NULL) {
      __atomic_fetch_add(&oru->ul_dropped_jobs, 1, __ATOMIC_RELAXED);
      // This push never reached a worker: release the slot ourselves.
      if (atomic_fetch_sub(&item->refs, 1) == 1) {
        work_q_done(item->queue, item);
      }
      continue;
    }
    pushTpool(oru->ru->threadPool, (task_t){.args = slot, .func = ul_worker_pool_task});
  }
  // NB: the FFT item's own slot is released by its last beam job (or the drop path above), not here.
}

// Push one FFT job for a symbol: the FFT worker computes the RX antennas' FFTs once and then
// dispatches the symbol's UL jobs as beam items (one FFT per antenna per symbol, shared by all
// beams and sections).
static void dispatch_ul_fft(work_q_t *fft_q,
                            work_q_t *beam_q,
                            ORU_t *oru,
                            int frame,
                            int slot,
                            int symbol,
                            const ul_job_t *jobs,
                            int nb_jobs)
{
  const size_t payload = (size_t)oru->ru->nb_rx * oru->ru->nr_frame_parms->ofdm_symbol_size * sizeof(c16_t);
  const size_t item_size = ul_fft_item_size(payload);
  uint8_t src[item_size]; // VLA: work_q_push copies a full element into its own slot
  memset(src, 0, item_size);
  ul_fft_item_t *item = (ul_fft_item_t *)src;
  item->oru = oru;
  item->frame = frame;
  item->slot = slot;
  item->symbol = symbol;
  item->queue = fft_q;
  item->beam_queue = beam_q;
  item->nb_jobs = nb_jobs;
  memcpy(item->jobs, jobs, nb_jobs * sizeof(ul_job_t));

  void *slot_ptr = work_q_push(fft_q, src);
  if (slot_ptr == NULL) {
    // FFT queue is a few symbols behind: dropping the whole symbol is equivalent to dropping
    // its jobs individually at this point.
    __atomic_fetch_add(&oru->ul_dropped_jobs, nb_jobs, __ATOMIC_RELAXED);
    RATELIMIT(UL_CAL_ERR_LOG_RATELIMIT, {
      LOG_W(PHY, "[ORU south] UL FFT queue full at symbol %d.%d.%d, dropping %d jobs\n",
            frame, slot, symbol, nb_jobs);
    });
    return;
  }
  pushTpool(oru->ru->threadPool, (task_t){.args = slot_ptr, .func = ul_fft_pool_task});
}

// O-RU-specific TX thread: TDD-aware (skips writes in UL slots) and uses a simpler
// synchronization mechanism than the generic USRP write thread, which has to stay agnostic of
// TDD/O-RU timing so it also serves FDD and non-O-RU (e.g. rfsimulator, split 8 non-O-RU) setups.
void *oru_south_write_thread(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  LOG_A(PHY, "South write thread started on core %d\n", oru->tx_write.core);

  uint64_t total_write_calls = 0;
  uint64_t total_symbols_sent = 0;
  uint64_t coalesce_histogram[NR_SYMBOLS_PER_SLOT + 1] = {0}; // max coalesced is 14

  pthread_mutex_lock(&oru->tx_write.mutex);
  while (!oru->tx_write.initialized && !oai_exit) {
    pthread_cond_wait(&oru->tx_write.cond, &oru->tx_write.mutex);
  }
  if (oai_exit) {
    pthread_mutex_unlock(&oru->tx_write.mutex);
    return NULL;
  }
  uint64_t next_symbol_to_send = oru->tx_write.start_symbol_index;
  int64_t start_ts = oru->tx_write.start_timestamp;
  pthread_mutex_unlock(&oru->tx_write.mutex);

  while (!oai_exit) {
    uint64_t latest = symbol_reorder_wait_at_least(oru->dl_reorder, next_symbol_to_send, &oai_exit);
    if (oai_exit) {
      break;
    }

    // Process all symbols from next_symbol_to_send to latest
    while (next_symbol_to_send <= latest) {
      int symbols_per_slot = fp->symbols_per_slot;
      int symbols_per_frame = fp->slots_per_frame * symbols_per_slot;

      uint64_t abs_frame = next_symbol_to_send / symbols_per_frame;
      int frame = abs_frame % 1024;
      int slot = (next_symbol_to_send % symbols_per_frame) / symbols_per_slot;
      int symbol = next_symbol_to_send % symbols_per_slot;

      // Calculate timestamp for the first symbol in this batch
      int64_t timestamp = start_ts + abs_frame * fp->samples_per_frame + get_samples_slot_timestamp(fp, slot)
                          + get_samples_symbol_timestamp(fp, slot, symbol);

      if (timestamp < 0) {
        next_symbol_to_send++;
        continue;
      }

      // How many symbols can we send in the same slot?
      // Since they are contiguous, the maximum number is up to the end of the slot
      uint64_t remaining_in_batch = latest - next_symbol_to_send + 1;
      int max_in_slot = symbols_per_slot - symbol;
      int num_coalesced = (remaining_in_batch < (uint64_t)max_in_slot) ? (int)remaining_in_batch : max_in_slot;

      // Send the coalesced symbols
      int sent = tx_rf_symbols(ru, frame, slot, timestamp, symbol, num_coalesced);

      if (sent > 0) {
        total_write_calls++;
        total_symbols_sent += sent;
        if (sent >= 1 && sent <= NR_SYMBOLS_PER_SLOT) {
          coalesce_histogram[sent]++;
        }
      }

      if (total_write_calls % (NR_SYMBOLS_PER_SLOT * 1000) == 0) {
        LOG_I(PHY, "O-RU South Write Coalescing Stats: calls=%lu, symbols=%lu, avg_coalesced=%.2f\n",
              total_write_calls, total_symbols_sent, (double)total_symbols_sent / total_write_calls);
        if (total_symbols_sent > total_write_calls) {
          uint64_t c3_4 = coalesce_histogram[3] + coalesce_histogram[4];
          uint64_t c5_6 = coalesce_histogram[5] + coalesce_histogram[6];
          uint64_t c7_9 = coalesce_histogram[7] + coalesce_histogram[8] + coalesce_histogram[9];
          uint64_t c10_plus = 0;
          for (int c = 10; c <= NR_SYMBOLS_PER_SLOT; c++) c10_plus += coalesce_histogram[c];
          LOG_I(PHY, "  Coalesced distribution: [1]:%lu, [2]:%lu, [3-4]:%lu, [5-6]:%lu, [7-9]:%lu, [10+]:%lu\n",
                coalesce_histogram[1], coalesce_histogram[2], c3_4, c5_6, c7_9, c10_plus);
        }
      }

      next_symbol_to_send += num_coalesced;
    }
  }

  return NULL;
}

void *oru_south_read_thread(void *arg)
{
  ORU_t *oru = arg;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int slot, frame;
  uint32_t start_frame = 0, start_slot = 0;
  uint64_t hyper_frame = 0;
  struct timespec utc_anchor_point;
  if (ru->rfdevice.get_timestamp) {
    AssertFatal(ru->rfdevice.get_timestamp != NULL, "rfdevice has no capability to translate UTC timestamp to sample index\n");
    oru_fh_get_utc_anchor_point(oru->fronthaul, &hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
    int64_t start_timestamp = ru->rfdevice.get_timestamp(&ru->rfdevice, &utc_anchor_point);

    const int num_samples = 3000;
    c16_t throwaway_samples[ru->nb_rx][num_samples];
    void *rxp[ru->nb_rx];
    for (int i = 0; i < ru->nb_rx; i++)
      rxp[i] = throwaway_samples[i];

    openair0_timestamp_t timestamp;
    int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
    AssertFatal(num_samples_read == num_samples, "Unexpected number of samples received\n");
    openair0_timestamp_t next_timestamp = timestamp + num_samples_read;
    while (next_timestamp > start_timestamp) {
      start_timestamp += get_samples_slot_duration(fp, start_slot, 1);
      start_slot++;
      if (start_slot == fp->slots_per_frame) {
        start_slot = 0;
        start_frame++;
        if (start_frame == 1024) {
          start_frame = 0;
        }
      }
    }
    while (next_timestamp < start_timestamp) {
      int num_samples_to_read = min(num_samples, (int)(start_timestamp - next_timestamp));
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples_to_read, ru->nb_rx);
      AssertFatal(num_samples_read == num_samples_to_read, "Unexpected number of samples received\n");
      next_timestamp += num_samples_read;
    }

    AssertFatal(next_timestamp == start_timestamp, "O-RU South thread could not sync to UTC anchor point\n");
  } else {
    int num_iter = 100;
    const int num_samples = 3000;
    openair0_timestamp_t timestamp;
    c16_t throwaway_samples[ru->nb_rx][num_samples];
    void *rxp[ru->nb_rx];
    for (int i = 0; i < ru->nb_rx; i++) {
      rxp[i] = throwaway_samples[i];
    }
    while (!oai_exit && num_iter-- > 0) {
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
      AssertFatal(num_samples_read == num_samples, "Unexpected number of samples received\n");
    }
    oru_fh_get_utc_anchor_point(oru->fronthaul, &hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
    timestamp += num_samples;
    notify_north_read(hyper_frame, start_frame, start_slot, timestamp);
  }
  slot = start_slot;
  frame = start_frame;

  work_q_t beam_queue;
  AssertFatal(work_q_alloc(&beam_queue, UL_WORK_QUEUE_DEPTH, sizeof(ul_work_item_t)),
              "[ORU south] failed to allocate UL work queue\n");

  // One FFT job per symbol computes each RX antenna's FFT once and dispatches the symbol's beam
  // jobs; the FFT payload lives in the job's own queue slot (refcounted).
  AssertFatal(ru->nb_rx <= ORU_CODEBOOK_MAX_NB_TX, "nb_rx %d exceeds maximum %d\n", ru->nb_rx, ORU_CODEBOOK_MAX_NB_TX);
  work_q_t fft_queue;
  const size_t fft_payload = (size_t)ru->nb_rx * fp->ofdm_symbol_size * sizeof(c16_t);
  AssertFatal(work_q_alloc(&fft_queue, UL_FFT_QUEUE_DEPTH, ul_fft_item_size(fft_payload)),
              "[ORU south] failed to allocate UL FFT queue\n");

  ul_calendar_t ul_cal = {0};

  while (!oai_exit) {
    int rx_slot_type = nr_slot_select(&ru->config, frame, slot);
    for (int symbol = 0; symbol < 14; symbol++) {
      int samples_to_read = get_samples_symbol_duration(fp, slot, symbol, 1);
      size_t offset = get_samples_slot_timestamp(fp, slot) + get_samples_symbol_timestamp(fp, slot, symbol);
      c16_t *rxp[fp->nb_antennas_rx];
      for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
        rxp[aarx] = (c16_t *)&ru->common.rxdata[aarx][offset];
      }

      openair0_timestamp_t timestamp;
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, (void **)rxp, samples_to_read, ru->nb_rx);
      AssertFatal(num_samples_read == samples_to_read, "Unexpected number of samples received\n");
      LOG_D(PHY,
            "[ORU south] read data: frame %d, slot %d, symbol %d, timestamp %ld num_symbols %d, samples %d\n",
            frame,
            slot,
            symbol,
            timestamp,
            1,
            num_samples_read);

      // Drain the UL job ring into the calendar (jobs already due dispatch immediately)
      const uint64_t reader_abs = ((uint64_t)frame * fp->slots_per_frame + slot) * NR_SYMBOLS_PER_SLOT + symbol;
      ul_job_t new_job;
      while (oru_fh_poll_ul_job(oru->fronthaul, &new_job) == 0) {
        ul_calendar_add(&ul_cal, &beam_queue, &fft_queue, oru, reader_abs, &new_job);
      }

      if (rx_slot_type == NR_UPLINK_SLOT || rx_slot_type == NR_MIXED_SLOT) {
        // Dispatch the jobs parked on this symbol's calendar entry
        ul_calendar_tick(&ul_cal, &beam_queue, &fft_queue, oru, reader_abs);
      }
      int prach_symbol = get_prach_symbol(oru, frame, slot, symbol, ru->numerology);
      if (prach_symbol != -1)
        receive_prach(oru, frame, slot, symbol, prach_symbol);
    }
    slot++;
    if (slot == fp->slots_per_frame) {
      slot = 0;
      frame++;
      if (frame == 1024) {
        frame = 0;
      }
    }
  }

  return NULL;
}

void oru_self_diagnosis(ORU_t *oru)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  uint64_t dl_packed = __atomic_exchange_n(&oru->dl_packed_stats, 0, __ATOMIC_RELAXED);
  uint64_t dl_count = dl_packed >> 32;
  uint64_t dl_time_total = dl_packed & 0xFFFFFFFFULL;
  uint64_t dl_time_max = __atomic_exchange_n(&oru->dl_symbol_time_max_us, 0, __ATOMIC_RELAXED);

  uint64_t ul_packed = __atomic_exchange_n(&oru->ul_packed_stats, 0, __ATOMIC_RELAXED);
  uint64_t ul_count = ul_packed >> 32;
  uint64_t ul_time_total = ul_packed & 0xFFFFFFFFULL;
  uint64_t ul_time_max = __atomic_exchange_n(&oru->ul_ant_time_max_us, 0, __ATOMIC_RELAXED);
  uint64_t ul_dropped = __atomic_exchange_n(&oru->ul_dropped_jobs, 0, __ATOMIC_RELAXED);
  uint64_t ul_symbols_missed = __atomic_exchange_n(&oru->ul_symbols_missed, 0, __ATOMIC_RELAXED);
  uint64_t ul_cal_overflow = __atomic_exchange_n(&oru->ul_cal_overflow_dropped, 0, __ATOMIC_RELAXED);
  uint64_t ul_cal_horizon = __atomic_exchange_n(&oru->ul_cal_horizon_dropped, 0, __ATOMIC_RELAXED);

  if (dl_count == 0 && ul_count == 0 && ul_dropped == 0 && ul_symbols_missed == 0 && ul_cal_overflow == 0
      && ul_cal_horizon == 0) {
    return;
  }

  double t_sym_avg_us = 1000.0 / (14.0 * (1 << fp->numerology_index));
  long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  int num_dl_workers = oru->num_dl_read_threads;
  int num_ul_workers = ru->nb_rx;

  LOG_I(PHY, "=================== O-RU Real-Time Self-Diagnosis Report ===================\n");
  LOG_I(PHY, "Configuration: numerology=%d, DL bandwidth=%d PRBs, UL bandwidth=%d PRBs\n",
        fp->numerology_index, fp->N_RB_DL, fp->N_RB_UL);
  LOG_I(PHY, "Antennas: TX=%d, RX=%d\n", fp->nb_antennas_tx, fp->nb_antennas_rx);
  LOG_I(PHY, "Physical Symbol Duration (average): %.2f us\n", t_sym_avg_us);
  LOG_I(PHY, "CPU Cores Online: %ld, DL Worker Threads: %d, UL Worker Threads: %d\n", num_cores, num_dl_workers, num_ul_workers);
  LOG_I(PHY, "----------------------------------------------------------------------------\n");

  bool pass = true;

  if (dl_count > 0) {
    double dl_avg_us = (double)dl_time_total / (dl_count * 16.0);
    double dl_max_us = (double)dl_time_max / 16.0;

    // DL symbols are now pipelined across num_dl_workers independent reader threads (each fully
    // processes one absolute symbol at a time, then hands off to the symbol reorder buffer), rather
    // than one thread processing every symbol serially - so the sustainable per-symbol-slot cost is
    // the per-thread time divided by however many of those threads a core can actually run at once.
    double effective_dl_workers = (num_dl_workers < num_cores) ? num_dl_workers : num_cores;
    if (effective_dl_workers < 1.0) effective_dl_workers = 1.0;
    double dl_eff_avg_us = dl_avg_us / effective_dl_workers;
    double dl_eff_max_us = dl_max_us / effective_dl_workers;
    double dl_margin = ((t_sym_avg_us - dl_eff_avg_us) / t_sym_avg_us) * 100.0;

    LOG_I(PHY, "DL (Pipelined OFDM Modulation + phase rotation across %d reader threads, %d antennas):\n",
          num_dl_workers, fp->nb_antennas_tx);
    LOG_I(PHY, "  Processed: %lu symbols\n", dl_count);
    LOG_I(PHY, "  Per-Thread Symbol Time: Avg = %.2f us, Max = %.2f us\n", dl_avg_us, dl_max_us);
    LOG_I(PHY, "  Effective Symbol Processing Time (with %d threads on %ld cores): Avg = %.2f us, Max = %.2f us\n",
          num_dl_workers, num_cores, dl_eff_avg_us, dl_eff_max_us);
    LOG_I(PHY, "  Safety Margin (based on Avg): %.2f%%\n", dl_margin);

    if (dl_eff_avg_us >= t_sym_avg_us) {
      LOG_E(PHY, "  [DIAGNOSIS] DL CRITICAL: Effective processing time (%.2f us) exceeds symbol budget (%.2f us)!\n",
            dl_eff_avg_us, t_sym_avg_us);
      pass = false;
    } else if (dl_margin < 20.0) {
      LOG_W(PHY, "  [DIAGNOSIS] DL WARNING: Safety margin is low (%.2f%% < 20%%). Risk of RT jitter.\n", dl_margin);
    } else {
      LOG_I(PHY, "  [DIAGNOSIS] DL PASS\n");
    }
  } else {
    LOG_I(PHY, "DL: No symbols processed in this window.\n");
  }

  LOG_I(PHY, "----------------------------------------------------------------------------\n");

  if (ul_count > 0 || ul_dropped > 0 || ul_symbols_missed > 0 || ul_cal_overflow > 0 || ul_cal_horizon > 0) {
    double ul_ant_avg_us = ul_count > 0 ? (double)ul_time_total / (ul_count * 16.0) : 0.0;
    double ul_ant_max_us = ul_count > 0 ? (double)ul_time_max / 16.0 : 0.0;

    double effective_workers = (num_ul_workers < num_cores) ? num_ul_workers : num_cores;
    if (effective_workers < 1.0) effective_workers = 1.0;

    int ceil_steps = (fp->nb_antennas_rx + (int)effective_workers - 1) / (int)effective_workers;
    double ul_eff_avg_us = ceil_steps * ul_ant_avg_us;
    double ul_eff_max_us = ceil_steps * ul_ant_max_us;
    double ul_margin = ((t_sym_avg_us - ul_eff_avg_us) / t_sym_avg_us) * 100.0;

    LOG_I(PHY, "UL (Parallel FFT + phase de-rotation for %d antennas):\n", fp->nb_antennas_rx);
    LOG_I(PHY, "  Processed: %lu antenna-symbols\n", ul_count);
    LOG_I(PHY, "  Dropped: %lu jobs due to threadpool queue full\n", ul_dropped);
    if (ul_count > 0) {
      LOG_I(PHY, "  Per-Antenna Time: Avg = %.2f us, Max = %.2f us\n", ul_ant_avg_us, ul_ant_max_us);
      LOG_I(PHY, "  Effective Symbol Processing Time (with %d threads on %ld cores): Avg = %.2f us, Max = %.2f us\n",
            num_ul_workers, num_cores, ul_eff_avg_us, ul_eff_max_us);
      LOG_I(PHY, "  Safety Margin (based on Avg): %.2f%%\n", ul_margin);

      if (ul_eff_avg_us >= t_sym_avg_us) {
        LOG_E(PHY, "  [DIAGNOSIS] UL CRITICAL: Effective processing time (%.2f us) exceeds symbol budget (%.2f us)!\n",
              ul_eff_avg_us, t_sym_avg_us);
        pass = false;
      } else if (ul_margin < 20.0) {
        LOG_W(PHY, "  [DIAGNOSIS] UL WARNING: Safety margin is low (%.2f%% < 20%%). Risk of RT jitter.\n", ul_margin);
      } else {
        LOG_I(PHY, "  [DIAGNOSIS] UL PASS\n");
      }
    } else {
      LOG_I(PHY, "  No symbols successfully processed in this window.\n");
    }

    if (ul_dropped > 0) {
      LOG_E(PHY, "  [DIAGNOSIS] UL CRITICAL: Dropped %lu jobs because the threadpool queue was full!\n", ul_dropped);
      pass = false;
    }
    if (ul_symbols_missed > 0) {
      LOG_E(PHY, "  [DIAGNOSIS] UL CRITICAL: Dropped %lu jobs that arrived %d or more symbols too late!\n", ul_symbols_missed, NR_SYMBOLS_PER_SLOT);
      pass = false;
    }
    if (ul_cal_overflow > 0) {
      LOG_E(PHY, "  [DIAGNOSIS] UL CRITICAL: Dropped %lu jobs because a calendar symbol entry was full (>%d concurrent jobs on one symbol)!\n",
            ul_cal_overflow, UL_CAL_JOBS_PER_SYMBOL);
      pass = false;
    }
    if (ul_cal_horizon > 0) {
      LOG_E(PHY, "  [DIAGNOSIS] UL CRITICAL: Dropped %lu jobs declared more than %d symbols ahead (beyond calendar horizon) - DU declaring UL C-plane too early?\n",
            ul_cal_horizon, UL_CAL_SLOTS * NR_SYMBOLS_PER_SLOT);
      pass = false;
    }
  } else {
    LOG_I(PHY, "UL: No symbols processed or dropped in this window.\n");
  }

  LOG_I(PHY, "----------------------------------------------------------------------------\n");
  if (pass) {
    LOG_I(PHY, "OVERALL STATUS: PASS\n");
  } else {
    LOG_E(PHY, "OVERALL STATUS: FAIL (Hardware configuration insufficient for real-time operation)\n");
  }
  LOG_I(PHY, "============================================================================\n");
}
