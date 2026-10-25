/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief common APIs for different RF frontend device
 */
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "common_lib.h"
#include "assertions.h"
#include "common/utils/load_module_shlib.h"
#include "common/utils/LOG/log.h"
#include "executables/softmodem-common.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "common/cmake_defs.h"
#include "openair1/PHY/TOOLS/tools_defs.h"

const char *const devtype_names[MAX_RF_DEV_TYPE] =
    {"", "USRP B200", "USRP X300", "USRP N300", "USRP X400", "BLADERF", "LMSSDR", "IRIS", "No HW", "UEDv2", "RFSIMULATOR"};

const char *get_devname(int devtype) {
  if (devtype < MAX_RF_DEV_TYPE && devtype !=MIN_RF_DEV_TYPE )
    return devtype_names[devtype];
  return "none";
}

static int set_device(openair0_device_t *device)
{
  char *dev_type = device->host_type == RAU_HOST ? "RAU" : "RRU";
  const char *devname = get_devname(device->type);
  if (strcmp(devname, "none") != 0) {
    LOG_I(HW, "[%s] has loaded %s device.\n", dev_type, devname);
    return 0;
  }
  LOG_E(HW, "[%s] invalid HW device.\n", dev_type);
  return -1;
}

static int set_transport(openair0_device_t *device)
{
  char *dev_type = device->host_type == RAU_HOST ? "RAU" : "RRU";
  switch (device->transp_type) {
    case ETHERNET_TP:
      LOG_I(HW, "[%s] has loaded ETHERNET trasport protocol.\n", dev_type);
      return 0;
      break;

    case NONE_TP:
      LOG_I(HW, "[%s] has not loaded a transport protocol.\n", dev_type);
      return 0;
      break;

    default:
      LOG_E(HW, "[%s] invalid transport protocol.\n", dev_type);
      return -1;
      break;
  }
}

typedef int (*devfunc_t)(openair0_device_t *, openair0_config_t *);

#define  DEVICE_SECTION   "device"
#define CONFIG_HLP_DEVICE "Identifies the oai device (the interface to RF) to use, the shared lib \"lib_<name>.so\" will be loaded"
/* look for the interface library and load it */
int load_lib(openair0_device_t *device, openair0_config_t *openair0_cfg, rau_type_t rau_type)
{
  openair0_cfg->command_line_sample_advance = get_softmodem_params()->command_line_sample_advance;
  openair0_cfg->recplay_mode = read_recplayconfig(&openair0_cfg->recplay_conf, &device->recplay_state);
  // softmodem has to know we use the iqrecorder to workaround randomized algorithms
  if (openair0_cfg->recplay_mode == RECPLAY_RECORDMODE) {
    IS_SOFTMODEM_IQRECORDER = true; // softmodem has to know we use the iqrecorder to workaround randomized algorithms
  }
  char *deflibname = OAI_RF_LIBNAME;
  loader_shlibfunc_t shlib_fdesc = {.fname = "device_init"};
  if (openair0_cfg->recplay_mode == RECPLAY_REPLAYMODE) {
    deflibname = OAI_IQPLAYER_LIBNAME;
    IS_SOFTMODEM_IQPLAYER = true; // softmodem has to know we use the iqplayer to workaround randomized algorithms
  } else {
    switch (rau_type) {
      case RAU_LOCAL_RADIO_HEAD:
        if (IS_SOFTMODEM_RFSIM)
          deflibname = OAI_RFSIM_LIBNAME;
        break;
      case RAU_REMOTE_THIRDPARTY_RADIO_HEAD:
        deflibname = OAI_THIRDPARTY_TP_LIBNAME;
        shlib_fdesc.fname = "transport_init";
        break;
      case RAU_REMOTE_RADIO_HEAD:
        deflibname = OAI_TP_LIBNAME;
        shlib_fdesc.fname = "transport_init";
        break;
      default:
        AssertFatal(false, "impossible radio head\n");
    }
  }

  char *devname = NULL;
  paramdef_t device_params = {"name", CONFIG_HLP_DEVICE, 0, .strptr = &devname, .defstrval = deflibname, TYPE_STRING, 0};
  config_get(config_get_if(), &device_params, 1, DEVICE_SECTION);

  int ret = load_module_shlib(devname, &shlib_fdesc, 1, NULL);
  AssertFatal(ret >= 0, "Library %s couldn't be loaded\n", devname);
  return ((devfunc_t)shlib_fdesc.fptr)(device, openair0_cfg);
}

int ring_elements(int nb, int elem_size)
{
  // runtime page size: 64 KB on some aarch64 kernels, the second mapping of create_ring() must start on a page
  const int page = sysconf(_SC_PAGESIZE);
  AssertFatal(page > 0 && page % elem_size == 0, "page size %d, element size %d\n", page, elem_size);
  return ceil_mod(nb, page / elem_size);
}

void *create_ring(int sz_bytes)
{
  const int page = sysconf(_SC_PAGESIZE);
  AssertFatal(sz_bytes % page == 0, "must be a number of pages (%d bytes) %d\n", page, sz_bytes);
  // the same pages mapped twice in a row: an access of up to sz_bytes from any index never wraps
  FILE *f = tmpfile();
  AssertFatal(f, "tmpfile: %s\n", strerror(errno));
  const int fd = fileno(f);
  AssertFatal(ftruncate(fd, sz_bytes) == 0, "ftruncate: %s\n", strerror(errno));
  void *ret = mmap(NULL, 2 * sz_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  AssertFatal(ret != MAP_FAILED, "mmap: %s\n", strerror(errno));
  AssertFatal(mmap(ret, sz_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != MAP_FAILED,
              "mmap: %s\n",
              strerror(errno));
  AssertFatal(mmap(ret + sz_bytes, sz_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != MAP_FAILED,
              "mmap: %s\n",
              strerror(errno));
  fclose(f);
  return ret;
}

void openair0_write_reorder_init(re_order_t *r, const openair0_config_t *openair0_cfg)
{
  pthread_mutex_init(&r->mutex_store, NULL);
  pthread_cond_init(&r->written, NULL);

  r->sz = ring_elements(openair0_cfg->sample_rate / 100, sizeof(c16_t)); // 10 ms storage
  r->grain = 2048; // arbitrary write size, chosen to fit in a ethernet jumbo frame
  r->nb_ant = openair0_cfg->tx_num_channels;
  r->nb_writers = create_ring(r->sz * sizeof(*r->nb_writers));

  r->ring = calloc_or_fail(r->nb_ant, sizeof(*r->ring));
  for (int i = 0; i < r->nb_ant; i++)
    r->ring[i] = create_ring(r->sz * sizeof(c16_t));
}

int openair0_device_load(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  int rc=0;
  rc=load_lib(device, openair0_cfg, RAU_LOCAL_RADIO_HEAD);

  if ( rc >= 0) {
    if ( set_device(device) < 0) {
      LOG_E(HW, "%s %d:Unsupported radio head\n", __FILE__, __LINE__);
      return -1;
    }
  } else {
    AssertFatal(false, "can't open the radio device: %s\n", get_devname(device->type));
  }
  openair0_write_reorder_init(&device->reOrder, openair0_cfg);
  return rc;
}

int openair0_transport_load(openair0_device_t *device, openair0_config_t *openair0_cfg)
{
  int rc = load_lib(device, openair0_cfg, RAU_REMOTE_RADIO_HEAD);

  if ( rc >= 0) {
    if ( set_transport(device) < 0) {
      LOG_E(HW, "%s %d:Unsupported transport protocol\n", __FILE__, __LINE__);
      return -1;
    }
  }

  return rc;
}

void *load_transport_shlib(char *fname)
{
  char *devname = NULL;
  paramdef_t device_params = {"name", CONFIG_HLP_DEVICE, 0, .strptr = &devname, .defstrval = OAI_TP_LIBNAME, TYPE_STRING, 0};
  config_get(config_get_if(), &device_params, 1, DEVICE_SECTION);

  loader_shlibfunc_t shlib_fdesc = {.fname = fname};
  int ret = load_module_shlib(devname, &shlib_fdesc, 1, NULL);
  AssertFatal(ret >= 0, "Library %s couldn't be loaded\n", devname);
  return shlib_fdesc.fptr;
}

/* Send the samples from nextTS on, in chunks of at most one grain cut at burst boundaries, with the
   flags of the burst state stored with the samples. Samples before force_until are sent even if some
   writer has not written them: a writer that stops must not block the others. A chunk that does not
   reach a boundary is kept until it fills a grain. */
static void reorder_flush(nrue_ru_write_t nrue_ru_write,
                          PHY_VARS_NR_UE *UE,
                          openair0_device_t *device,
                          int nb_writers,
                          int nbAnt,
                          openair0_timestamp_t force_until)
{
  re_order_t *ctx = &device->reOrder;
  int *wr = ctx->nb_writers + ctx->nextTS % ctx->sz;
  int ready = 0;
  while (ready < ctx->sz && (wr[ready] & RO_COUNT) >= nb_writers)
    ready++;
  const int forced = force_until > ctx->nextTS ? min(force_until - ctx->nextTS, ctx->sz) : 0;
  ready = max(ready, forced);
  const bool gap = force_until - ctx->nextTS > ctx->sz; // nothing was written beyond the ring end

  for (int consumed = 0; ready > 0;) {
    wr = ctx->nb_writers + ctx->nextTS % ctx->sz;
    const int active = wr[0] & RO_ACTIVE;
    int len = 0;
    bool boundary = false;
    while (len < ready && len < ctx->grain) {
      if (len > 0 && ((wr[len] & RO_ACTIVE) != active || (wr[len] & RO_START))) {
        boundary = true;
        break;
      }
      if (wr[len++] & RO_END) {
        boundary = true;
        break;
      }
    }
    if (len < ctx->grain && !boundary && consumed >= forced)
      break; // wait for more samples
    int flags = TX_BURST_INVALID;
    if (active) {
      const bool start = wr[0] & RO_START, end = wr[len - 1] & RO_END;
      flags = start && end ? TX_BURST_START_AND_END : start ? TX_BURST_START : end ? TX_BURST_END : TX_BURST_MIDDLE;
    }
    if (active || IS_SOFTMODEM_RFSIM) {
      void *ptr[nbAnt];
      for (int a = 0; a < nbAnt; a++)
        ptr[a] = ((c16_t *)ctx->ring[a]) + ctx->nextTS % ctx->sz;
      int wroteSamples;
      if (nrue_ru_write)
        wroteSamples = nrue_ru_write(UE, ctx->nextTS, ptr, len, nbAnt, flags);
      else
        wroteSamples = device->trx_write_func(device, ctx->nextTS, ptr, len, nbAnt, flags);
      if (wroteSamples != len)
        LOG_W(HW, "Failed to write to RF: wrote %d out of %d samples\n", wroteSamples, len);
    }
    for (int a = 0; a < ctx->nb_ant; a++)
      memset(((c16_t *)ctx->ring[a]) + ctx->nextTS % ctx->sz, 0, len * sizeof(c16_t));
    memset(wr, 0, len * sizeof(*wr));
    ctx->nextTS += len;
    consumed += len;
    ready -= len;
  }
  if (gap)
    ctx->nextTS = force_until;
}

/* first allocation of the writer states, for a fixed number of writers */
static void reorder_init_writers(re_order_t *ctx, int nb_writers)
{
  if (ctx->max_writers) {
    AssertFatal(nb_writers == ctx->max_writers, "number of writers changed from %d to %d\n", ctx->max_writers, nb_writers);
    return;
  }
  ctx->max_writers = nb_writers;
  ctx->writer_end = calloc_or_fail(nb_writers, sizeof(*ctx->writer_end));
  ctx->writer_absent = calloc_or_fail(nb_writers, sizeof(*ctx->writer_absent));
}

// several writers (TX threads, UEs sharing one RU) may write out of order and concurrently
int openair0_write_reorder_common(nrue_ru_write_t nrue_ru_write,
                                  PHY_VARS_NR_UE *UE,
                                  openair0_device_t *device,
                                  openair0_timestamp_t timestamp,
                                  void **txp,
                                  int nsamps,
                                  int writer,
                                  int nb_writers,
                                  int nbAnt,
                                  int flags)
{
  re_order_t *ctx = &device->reOrder;
  LOG_D(HW, "received write order ts: %lu + %lu, nb samples %d, flags %d\n", timestamp, device->firstTS, nsamps, flags);
  AssertFatal(nb_writers > 0 && nb_writers <= RO_COUNT, "invalid number of writers %d\n", nb_writers);
  AssertFatal(nbAnt > 0 && nbAnt <= ctx->nb_ant, "invalid number of tx antennas %d (max %d)\n", nbAnt, ctx->nb_ant);
  AssertFatal(nsamps <= ctx->sz, "write of %d samples larger than the reorder ring (%d)\n", nsamps, ctx->sz);

  AssertFatal(writer >= 0 && writer < nb_writers, "invalid writer %d of %d\n", writer, nb_writers);
  pthread_mutex_lock(&ctx->mutex_store);
  reorder_init_writers(ctx, nb_writers);
  if (!ctx->initDone) {
    ctx->nextTS = timestamp;
    ctx->maxTS = timestamp;
    ctx->initDone = true;
  }
  ctx->writer_end[writer] = max(ctx->writer_end[writer], timestamp + nsamps);
  if (ctx->writer_absent[writer]) {
    ctx->writer_absent[writer] = false;
    ctx->nb_absent--;
  }
  nb_writers -= ctx->nb_absent;
  int skip = 0;
  if (timestamp < ctx->nextTS) {
    skip = min(ctx->nextTS - timestamp, nsamps);
    LOG_W(HW, "late write at %lu: %d of %d samples already sent, dropped\n", timestamp, skip, nsamps);
  }
  // make room: samples more than one ring before the end of this write must be sent first
  reorder_flush(nrue_ru_write, UE, device, nb_writers, nbAnt, timestamp + nsamps - ctx->sz);

  if (skip < nsamps) {
    const openair0_timestamp_t ts = timestamp + skip;
    const int n = nsamps - skip;
    for (int a = 0; a < nbAnt; a++) {
      c16_t *out = ((c16_t *)ctx->ring[a]) + ts % ctx->sz;
      c16adds(((c16_t *)txp[a]) + skip, out, out, n);
    }
    const int active = flags != TX_BURST_INVALID ? RO_ACTIVE : 0;
    int *wr = ctx->nb_writers + ts % ctx->sz;
    for (int i = 0; i < n; i++)
      wr[i] = (wr[i] + 1) | active;
    if (active && !skip && (flags == TX_BURST_START || flags == TX_BURST_START_AND_END))
      wr[0] |= RO_START;
    if (active && (flags == TX_BURST_END || flags == TX_BURST_START_AND_END || flags == TX_BURST_END_NO_TIME_SPEC))
      wr[n - 1] |= RO_END;
    ctx->maxTS = max(ctx->maxTS, timestamp + nsamps);
  }
  // don't wait more than half the ring for a writer that did not write
  reorder_flush(nrue_ru_write, UE, device, nb_writers, nbAnt, ctx->maxTS - ctx->sz / 2);
  pthread_cond_broadcast(&ctx->written);
  pthread_mutex_unlock(&ctx->mutex_store);
  return nsamps;
}

/* true when all the writers wrote the samples before until, *checked: first sample not checked yet
   (counts only grow until the samples are sent) */
static bool reorder_all_written(const re_order_t *ctx, int nb_writers, openair0_timestamp_t until, openair0_timestamp_t *checked)
{
  /* a writer may leave a few samples unwritten (timing advance change): a sample that all the present writers
     passed by a quarter of the ring will not be written any more */
  openair0_timestamp_t passed = INT64_MAX;
  for (int w = 0; w < ctx->max_writers; w++)
    if (!ctx->writer_absent[w])
      passed = min(passed, ctx->writer_end[w]);
  passed = passed > ctx->sz / 4 ? passed - ctx->sz / 4 : 0;
  openair0_timestamp_t ts = max(*checked, ctx->nextTS);
  const openair0_timestamp_t end = min(until, ctx->nextTS + ctx->sz);
  for (; ts < end; ts++)
    if ((ctx->nb_writers[ts % ctx->sz] & RO_COUNT) < nb_writers && ts >= passed)
      break;
  *checked = ts;
  return ts >= end;
}

/* send everything stored before force_until. Wait up to wait_ms for the writers that did not write these
   samples yet, then send them anyway: the samples of these writers are late */
void openair0_write_reorder_flush_common(nrue_ru_write_t nrue_ru_write,
                                         PHY_VARS_NR_UE *UE,
                                         openair0_device_t *device,
                                         int nb_writers,
                                         int nbAnt,
                                         openair0_timestamp_t force_until,
                                         int wait_ms)
{
  re_order_t *ctx = &device->reOrder;
  pthread_mutex_lock(&ctx->mutex_store);
  if (wait_ms > 0 && ctx->initDone && force_until > ctx->nextTS) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += wait_ms / 1000;
    deadline.tv_nsec += (wait_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
    }
    openair0_timestamp_t checked = ctx->nextTS;
    while (!reorder_all_written(ctx, nb_writers - ctx->nb_absent, force_until, &checked))
      if (pthread_cond_timedwait(&ctx->written, &ctx->mutex_store, &deadline) == ETIMEDOUT) {
        /* a writer that does not write a sample range at all (a UE resynchronizing after a radio link failure)
           would make every flush wait wait_ms: the writers that did not reach the missing sample are absent, not
           waited for, until they write again. The others are still waited for: the simulated time does not run
           ahead of their TX */
        for (int w = 0; w < ctx->max_writers; w++)
          if (!ctx->writer_absent[w] && ctx->writer_end[w] <= checked) {
            ctx->writer_absent[w] = true;
            ctx->nb_absent++;
            LOG_D(HW, "TX writer %d not written at %lu after %d ms, absent until it writes\n", w, checked, wait_ms);
          }
        break;
      }
  }
  if (ctx->initDone && force_until > ctx->nextTS)
    reorder_flush(nrue_ru_write, UE, device, nb_writers - ctx->nb_absent, nbAnt, force_until);
  pthread_mutex_unlock(&ctx->mutex_store);
}

int openair0_write_reorder(openair0_device_t *device, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags)
{
  return openair0_write_reorder_common(NULL, NULL, device, timestamp, txp, nsamps, 0, 1, nbAnt, flags);
}

void openair0_write_reorder_clear_context(openair0_device_t *device)
{
  LOG_W(HW, "received write reorder clear context\n");
  re_order_t *ctx = &device->reOrder;
  pthread_mutex_lock(&ctx->mutex_store);
  if (ctx->initDone) {
    for (int a = 0; a < ctx->nb_ant; a++)
      memset(ctx->ring[a], 0, ctx->sz * sizeof(c16_t));
    memset(ctx->nb_writers, 0, ctx->sz * sizeof(*ctx->nb_writers));
    for (int w = 0; w < ctx->max_writers; w++) {
      ctx->writer_end[w] = 0;
      ctx->writer_absent[w] = false;
    }
    ctx->nb_absent = 0;
    ctx->initDone = false;
  }
  pthread_mutex_unlock(&ctx->mutex_store);
}
