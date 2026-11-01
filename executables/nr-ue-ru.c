/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr-ue-ru.h"
#include "nr-uesoftmodem.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include "openair1/PHY/phy_extern_nr_ue.h"

/* NR UE RU configuration section name */
#define CONFIG_STRING_NRUE_RU_LIST "RUs"

#define NRUE_RU_NB_TX "nb_tx"
#define NRUE_RU_NB_RX "nb_rx"
#define NRUE_RU_ATT_TX "att_tx"
#define NRUE_RU_ATT_RX "att_rx"
#define NRUE_RU_MAX_RXGAIN "max_rxgain"
#define NRUE_RU_SDR_ADDRS "sdr_addrs"
#define NRUE_RU_TX_SUBDEV "tx_subdev"
#define NRUE_RU_RX_SUBDEV "rx_subdev"
#define NRUE_RU_CLOCK_SRC "clock_src"
#define NRUE_RU_TIME_SRC "time_src"
#define NRUE_RU_TUNE_OFFSET "tune_offset"
#define NRUE_RU_IF_FREQUENCY "if_freq"
#define NRUE_RU_IF_FREQ_OFFSET "if_offset"

#define NRUE_RU_SRC_CHECK                                                                                        \
  &(checkedparam_t)                                                                                              \
  {                                                                                                              \
    .s3a = { config_checkstr_assign_integer, {"internal", "external", "gpsdo"}, {internal, external, gpsdo}, 3 } \
  }

/*------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                                       NR UE RU configuration parameters                                                        */
/* optname                   helpstr                    paramflags  XXXptr        defXXXval              type         numelt  chkPptr             */
/*------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define NRUE_RU_PARAMS_DESC { \
  {NRUE_RU_NB_TX,            CONFIG_HLP_UENANTT,        0,         .uptr=NULL,   .defuintval=1,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_NB_RX,            CONFIG_HLP_UENANTR,        0,         .uptr=NULL,   .defuintval=1,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_ATT_TX,           NULL,                      0,         .uptr=NULL,   .defuintval=0,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_ATT_RX,           NULL,                      0,         .uptr=NULL,   .defuintval=0,          TYPE_UINT,   0,      NULL              }, \
  {NRUE_RU_MAX_RXGAIN,       NULL,                      0,         .iptr=NULL,   .defintval=120,         TYPE_INT,    0,      NULL              }, \
  {NRUE_RU_SDR_ADDRS,        CONFIG_HLP_USRP_ARGS,      0,         .strptr=NULL, .defstrval="type=b200", TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_TX_SUBDEV,        CONFIG_HLP_TX_SUBDEV,      0,         .strptr=NULL, .defstrval="",          TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_RX_SUBDEV,        CONFIG_HLP_RX_SUBDEV,      0,         .strptr=NULL, .defstrval="",          TYPE_STRING, 0,      NULL              }, \
  {NRUE_RU_CLOCK_SRC,        NULL,                      0,         .strptr=NULL, .defstrval="internal",  TYPE_STRING, 0,      NRUE_RU_SRC_CHECK }, \
  {NRUE_RU_TIME_SRC,         NULL,                      0,         .strptr=NULL, .defstrval="internal",  TYPE_STRING, 0,      NRUE_RU_SRC_CHECK }, \
  {NRUE_RU_TUNE_OFFSET,      CONFIG_HLP_TUNE_OFFSET,    0,         .dblptr=NULL, .defdblval=0.0,         TYPE_DOUBLE, 0,      NULL              }, \
  {NRUE_RU_IF_FREQUENCY,     CONFIG_HLP_IF_FREQ,        0,         .u64ptr=NULL, .defint64val=0,         TYPE_UINT64, 0,      NULL              }, \
  {NRUE_RU_IF_FREQ_OFFSET,   CONFIG_HLP_IF_FREQ_OFF,    0,         .iptr=NULL,   .defintval=0,           TYPE_INT,    0,      NULL              }, \
}
// clang-format on

/* NR UE cell configuration section name */
#define CONFIG_STRING_NRUE_CELL_LIST "cells"

#define NRUE_CELL_RU_ID "ru_id"
#define NRUE_CELL_BAND "band"
#define NRUE_CELL_RF_FREQUENCY "rf_freq"
#define NRUE_CELL_RF_FREQ_OFFSET "rf_offset"
#define NRUE_CELL_NUMEROLOGY "numerology"
#define NRUE_CELL_N_RB_DL "N_RB_DL"
#define NRUE_CELL_SSB_START "ssb_start"

/*------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                                      NR UE cell configuration parameters                                                       */
/* optname                   helpstr                    paramflags  XXXptr        defXXXval              type         numelt  chkPptr             */
/*------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define NRUE_CELL_PARAMS_DESC { \
  {NRUE_CELL_RU_ID,          NULL,                      0,         .iptr=NULL,   .defintval=0,           TYPE_INT,    0,      NULL              }, \
  {NRUE_CELL_BAND,           CONFIG_HLP_BAND,           0,         .iptr=NULL,   .defintval=78,          TYPE_INT,    0,      NULL              }, \
  {NRUE_CELL_RF_FREQUENCY,   CONFIG_HLP_DLF,            0,         .u64ptr=NULL, .defint64val=0,         TYPE_UINT64, 0,      NULL              }, \
  {NRUE_CELL_RF_FREQ_OFFSET, CONFIG_HLP_ULF,            0,         .i64ptr=NULL, .defint64val=0,         TYPE_INT64,  0,      NULL              }, \
  {NRUE_CELL_NUMEROLOGY,     CONFIG_HLP_NUMEROLOGY,     0,         .iptr=NULL,   .defintval=1,           TYPE_INT,    0,      NULL              }, \
  {NRUE_CELL_N_RB_DL,        CONFIG_HLP_PRB_SA,         0,         .iptr=NULL,   .defintval=106,         TYPE_INT,    0,      NULL              }, \
  {NRUE_CELL_SSB_START,      CONFIG_HLP_SSC,            0,         .iptr=NULL,   .defintval=516,         TYPE_INT,    0,      NULL              }, \
}
// clang-format on

static int nrue_cell_count;
static nrUE_cell_params_t *nrue_cells;
static NR_DL_FRAME_PARMS *nrue_cell_fp;

static int nrue_ru_count;
static nrUE_RU_params_t *nrue_rus;

openair0_config_t openair0_cfg_g[MAX_CARDS] = {};
static openair0_device_t openair0_dev[MAX_CARDS] = {};

int nrue_get_cell_count(void)
{
  return nrue_cell_count;
}

int nrue_get_band(const PHY_VARS_NR_UE *UE)
{
  int cell_id = nrue_rus[UE->rf_map.card].used_by_cell;
  return nrue_cells[cell_id].band;
}

const nrUE_cell_params_t *nrue_get_cell(int cell_id)
{
  AssertFatal(cell_id >= 0 && cell_id < nrue_cell_count, "Invalid cell ID %d! Cell count = %d\n", cell_id, nrue_cell_count);
  return &nrue_cells[cell_id];
}

NR_DL_FRAME_PARMS *nrue_get_cell_fp(int cell_id)
{
  AssertFatal(cell_id >= 0 && cell_id < nrue_cell_count, "Invalid cell ID %d! Cell count = %d\n", cell_id, nrue_cell_count);
  return &nrue_cell_fp[cell_id];
}

void nrue_set_cell(int cell_id, const nrUE_cell_params_t *cell)
{
  AssertFatal(cell_id >= 0 && cell_id < nrue_cell_count, "Invalid cell ID %d! Cell count = %d\n", cell_id, nrue_cell_count);
  nrue_cells[cell_id] = *cell;
}

void nrue_set_cell_params(configmodule_interface_t *cfg)
{
  paramdef_t cellParams[] = NRUE_CELL_PARAMS_DESC;
  paramlist_def_t cellParamList = {CONFIG_STRING_NRUE_CELL_LIST, NULL, 0};
  config_getlist(cfg, &cellParamList, cellParams, sizeofArray(cellParams), NULL);

  if (cellParamList.numelt <= 0) {
    nrue_cell_count = 1;
    nrue_cell_fp = calloc_or_fail(nrue_cell_count, sizeof(NR_DL_FRAME_PARMS));
    nrue_cells = calloc_or_fail(nrue_cell_count, sizeof(nrUE_cell_params_t));
    nrue_cells[0] = (nrUE_cell_params_t){.ru_id = 0,
                                         .band = get_softmodem_params()->band,
                                         .rf_frequency = downlink_frequency[0][0],
                                         .rf_freq_offset = uplink_frequency_offset[0][0],
                                         .numerology = get_softmodem_params()->numerology,
                                         .N_RB_DL = get_nrUE_params()->N_RB_DL,
                                         .ssb_start = get_nrUE_params()->ssb_start_subcarrier,
                                         .used_by_ue = -1};
    return;
  }

  nrue_cell_count = cellParamList.numelt;
  nrue_cell_fp = calloc_or_fail(nrue_cell_count, sizeof(NR_DL_FRAME_PARMS));
  nrue_cells = calloc_or_fail(nrue_cell_count, sizeof(nrUE_cell_params_t));

  LOG_W(NR_PHY, "Ignoring command line cell parameters, using the following instead:\n");

  for (int cell_id = 0; cell_id < nrue_cell_count; cell_id++) {
    nrue_cells[cell_id].ru_id          = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_RU_ID)->iptr);
    nrue_cells[cell_id].band           = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_BAND)->iptr);
    nrue_cells[cell_id].rf_frequency   = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_RF_FREQUENCY)->u64ptr);
    nrue_cells[cell_id].rf_freq_offset = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_RF_FREQ_OFFSET)->i64ptr);
    nrue_cells[cell_id].numerology     = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_NUMEROLOGY)->iptr);
    nrue_cells[cell_id].N_RB_DL        = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_N_RB_DL)->iptr);
    nrue_cells[cell_id].ssb_start      = *(gpd(cellParamList.paramarray[cell_id], sizeofArray(cellParams), NRUE_CELL_SSB_START)->iptr);
    nrue_cells[cell_id].used_by_ue     = -1;

    LOG_I(NR_PHY,
          "cell %d: ru_id %d, band %d, DL freq %lu, UL freq_offset %ld, numerology %d, N_RB_DL %d, ssb_start %d\n",
          cell_id,
          nrue_cells[cell_id].ru_id,
          nrue_cells[cell_id].band,
          nrue_cells[cell_id].rf_frequency,
          nrue_cells[cell_id].rf_freq_offset,
          nrue_cells[cell_id].numerology,
          nrue_cells[cell_id].N_RB_DL,
          nrue_cells[cell_id].ssb_start);
  }
}

int nrue_get_ru_count(void)
{
  return nrue_ru_count;
}

const nrUE_RU_params_t *nrue_get_ru(int ru_id)
{
  AssertFatal(ru_id >= 0 && ru_id < nrue_ru_count, "Invalid RU ID %d! RU count = %d\n", ru_id, nrue_ru_count);
  return &nrue_rus[ru_id];
}

void nrue_set_ru_cell_id(int ru_id, int cell_id)
{
  AssertFatal(ru_id >= 0 && ru_id < nrue_ru_count, "Invalid RU ID %d! RU count = %d\n", ru_id, nrue_ru_count);
  AssertFatal(cell_id >= 0 && cell_id < nrue_cell_count, "Invalid cell ID %d! Cell count = %d\n", cell_id, nrue_cell_count);
  nrue_rus[ru_id].used_by_cell = cell_id;
}

void nrue_set_ru_params(configmodule_interface_t *cfg)
{
  paramdef_t RUParams[] = NRUE_RU_PARAMS_DESC;
  paramlist_def_t RUParamList = {CONFIG_STRING_NRUE_RU_LIST, NULL, 0};
  config_getlist(cfg, &RUParamList, RUParams, sizeofArray(RUParams), NULL);

  if (RUParamList.numelt <= 0) {
    nrue_ru_count = 1;
    nrue_rus = calloc_or_fail(nrue_ru_count, sizeof(nrUE_RU_params_t));
    nrue_rus[0] = (nrUE_RU_params_t){.nb_tx = get_nrUE_params()->nb_antennas_tx,
                                     .nb_rx = get_nrUE_params()->nb_antennas_rx,
                                     .att_tx = get_nrUE_params()->tx_gain,
                                     .att_rx = 0,
                                     .max_rxgain = get_nrUE_params()->rx_gain,
                                     .sdr_addrs = get_nrUE_params()->usrp_args,
                                     .tx_subdev = get_nrUE_params()->tx_subdev,
                                     .rx_subdev = get_nrUE_params()->rx_subdev,
                                     .clock_source = get_softmodem_params()->clock_source,
                                     .time_source = get_softmodem_params()->timing_source,
                                     .tune_offset = get_softmodem_params()->tune_offset,
                                     .if_frequency = get_nrUE_params()->if_freq,
                                     .if_freq_offset = get_nrUE_params()->if_freq_off,
                                     .used_by_cell = -1};
    return;
  }

  nrue_ru_count = RUParamList.numelt;
  nrue_rus = calloc_or_fail(nrue_ru_count, sizeof(nrUE_RU_params_t));

  LOG_W(NR_PHY, "Ignoring command line RU parameters, using the following instead:\n");

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    nrue_rus[ru_id].nb_tx            = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_NB_TX)->uptr);
    nrue_rus[ru_id].nb_rx            = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_NB_RX)->uptr);
    nrue_rus[ru_id].att_tx           = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_ATT_TX)->uptr);
    nrue_rus[ru_id].att_rx           = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_ATT_RX)->uptr);
    nrue_rus[ru_id].max_rxgain       = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_MAX_RXGAIN)->iptr);
    nrue_rus[ru_id].tune_offset      = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_TUNE_OFFSET)->dblptr);
    nrue_rus[ru_id].if_frequency     = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_IF_FREQUENCY)->u64ptr);
    nrue_rus[ru_id].if_freq_offset   = *(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_IF_FREQ_OFFSET)->iptr);
    nrue_rus[ru_id].used_by_cell     = -1;

    nrue_rus[ru_id].sdr_addrs = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_SDR_ADDRS)->strptr));
    nrue_rus[ru_id].tx_subdev = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_TX_SUBDEV)->strptr));
    nrue_rus[ru_id].rx_subdev = strdup(*(gpd(RUParamList.paramarray[ru_id], sizeofArray(RUParams), NRUE_RU_RX_SUBDEV)->strptr));

    int clock_src_idx = config_paramidx_fromname(RUParams, sizeofArray(RUParams), NRUE_RU_CLOCK_SRC);
    AssertFatal(clock_src_idx >= 0, "Index for clock_src config option not found!\n");
    nrue_rus[ru_id].clock_source = config_get_processedint(cfg, &RUParamList.paramarray[ru_id][clock_src_idx]);

    int time_src_idx = config_paramidx_fromname(RUParams, sizeofArray(RUParams), NRUE_RU_TIME_SRC);
    AssertFatal(time_src_idx >= 0, "Index for time_src config option not found!\n");
    nrue_rus[ru_id].time_source = config_get_processedint(cfg, &RUParamList.paramarray[ru_id][time_src_idx]);

    LOG_I(NR_PHY,
          "RU %d: nb_tx %d, nb_rx %d, att_tx %d, att_rx %d, max_rxgain %d, tune_offset %f, if_frequency %lu, if_freq_offset %d, "
          "sdr_addrs \"%s\", tx_subdev \"%s\", rx_subdev \"%s\", clock_source %d, time_source %d\n",
          ru_id,
          nrue_rus[ru_id].nb_tx,
          nrue_rus[ru_id].nb_rx,
          nrue_rus[ru_id].att_tx,
          nrue_rus[ru_id].att_rx,
          nrue_rus[ru_id].max_rxgain,
          nrue_rus[ru_id].tune_offset,
          nrue_rus[ru_id].if_frequency,
          nrue_rus[ru_id].if_freq_offset,
          nrue_rus[ru_id].sdr_addrs,
          nrue_rus[ru_id].tx_subdev,
          nrue_rus[ru_id].rx_subdev,
          nrue_rus[ru_id].clock_source,
          nrue_rus[ru_id].time_source);
  }
}

void nrue_init_openair0(void)
{
  int freq_off = 0;
  bool is_sidelink = (get_softmodem_params()->sl_mode) ? true : false;
  AssertFatal(MAX_CARDS >= nrue_ru_count,
              "Too many RUs allocated (%d)! Maybe increase MAX_CARDS (%d).\n",
              nrue_ru_count,
              MAX_CARDS);

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    int cell_id = nrue_rus[ru_id].used_by_cell;
    if (cell_id < 0) {
      LOG_W(PHY, "Skipping initialization of RU %d because it is not used by any cell!\n", ru_id);
      continue;
    }
    NR_DL_FRAME_PARMS *frame_parms = &nrue_cell_fp[cell_id];
    if (is_sidelink) {
      int UE_id = nrue_cells[cell_id].used_by_ue;
      if (UE_id < 0) {
        LOG_W(PHY, "Skipping initialization of RU %d because it is not used by any UE!\n", ru_id);
        continue;
      }
      frame_parms = &nrPHY_vars_UE_g[UE_id][0]->SL_UE_PHY_PARAMS.sl_frame_params;
    }

    openair0_config_t *cfg = &openair0_cfg_g[ru_id];
    cfg->configFilename    = NULL;
    cfg->sample_rate       = frame_parms->samples_per_subframe * 1e3;

    if (frame_parms->frame_type == TDD)
      cfg->duplex_mode = duplex_mode_TDD;
    else
      cfg->duplex_mode = duplex_mode_FDD;

    cfg->ru_id = ru_id;
    cfg->num_rb_dl = frame_parms->N_RB_DL;
    cfg->tx_num_channels = min(4, frame_parms->nb_antennas_tx);
    cfg->rx_num_channels = min(4, frame_parms->nb_antennas_rx);

    LOG_I(PHY,
          "HW: Configuring ru_id %d, sample_rate %f, tx/rx num_channels %d/%d, duplex_mode %s\n",
          ru_id,
          cfg->sample_rate,
          cfg->tx_num_channels,
          cfg->rx_num_channels,
          duplex_mode_txt[cfg->duplex_mode]);

    uint64_t dl_carrier, ul_carrier;
    if (is_sidelink || nrue_rus[ru_id].if_frequency == 0) {
      dl_carrier = frame_parms->dl_CarrierFreq;
      ul_carrier = frame_parms->ul_CarrierFreq;
    } else {
      dl_carrier = nrue_rus[ru_id].if_frequency;
      ul_carrier = nrue_rus[ru_id].if_frequency + nrue_rus[ru_id].if_freq_offset;
    }

    nr_rf_card_config_freq(cfg, ul_carrier, dl_carrier, freq_off);
    nr_rf_card_config_gain(cfg);

    cfg->configFilename = get_softmodem_params()->rf_config_file;

    cfg->sdr_addrs = nrue_rus[ru_id].sdr_addrs;
    cfg->tx_subdev = nrue_rus[ru_id].tx_subdev;
    cfg->rx_subdev = nrue_rus[ru_id].rx_subdev;
    cfg->clock_source = nrue_rus[ru_id].clock_source;
    cfg->time_source = nrue_rus[ru_id].time_source;
    cfg->tune_offset = nrue_rus[ru_id].tune_offset;
  }
}

void nrue_ru_start(void)
{
  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    openair0_config_t *cfg = &openair0_cfg_g[ru_id];
    openair0_device_t *dev = &openair0_dev[ru_id];

    dev->host_type = RAU_HOST;
    int tmp = openair0_device_load(dev, cfg);
    AssertFatal(tmp == 0, "Could not load the device %d\n", ru_id);
    int tmp2 = dev->trx_start_func(dev);
    AssertFatal(tmp2 == 0, "Could not start the device %d\n", ru_id);
    if (usrp_tx_thread == 1)
      dev->trx_write_init(dev);
  }
}

void nrue_ru_stop(void)
{
  for (openair0_device_t *ru = openair0_dev; ru < openair0_dev + nrue_ru_count; ru++) {
    if (ru->trx_stop_func)
      ru->trx_stop_func(ru);
  }
}

void nrue_ru_end(void)
{
  for (openair0_device_t *ru = openair0_dev; ru < openair0_dev + nrue_ru_count; ru++) {
    if (ru->trx_get_stats_func)
      ru->trx_get_stats_func(ru);
    if (ru->trx_end_func)
      ru->trx_end_func(ru);
  }
}

void nrue_ru_set_freq(PHY_VARS_NR_UE *UE, uint64_t ul_carrier, uint64_t dl_carrier, int freq_offset)
{
  int current_cell_id = nrue_rus[UE->rf_map.card].used_by_cell;
  NR_DL_FRAME_PARMS *fp0 = &nrue_cell_fp[current_cell_id];
  uint64_t dl_carrier_distance =
      fp0->dl_CarrierFreq > dl_carrier ? fp0->dl_CarrierFreq - dl_carrier : dl_carrier - fp0->dl_CarrierFreq;
  uint64_t ul_carrier_distance =
      fp0->ul_CarrierFreq > ul_carrier ? fp0->ul_CarrierFreq - ul_carrier : ul_carrier - fp0->ul_CarrierFreq;
  uint64_t carrier_distance = dl_carrier_distance + ul_carrier_distance;

  for (int ru_id = 0; carrier_distance != 0 && ru_id < nrue_ru_count; ru_id++) {
    int cell_id = nrue_rus[ru_id].used_by_cell;
    if (cell_id < 0) // skip RUs that have no cell definition
      continue;
    if (nrue_cells[cell_id].used_by_ue >= 0) // skip cells that are already used by an UE
      continue;

    fp0 = &nrue_cell_fp[cell_id];
    uint64_t this_dl_carrier_distance =
        fp0->dl_CarrierFreq > dl_carrier ? fp0->dl_CarrierFreq - dl_carrier : dl_carrier - fp0->dl_CarrierFreq;
    uint64_t this_ul_carrier_distance =
        fp0->ul_CarrierFreq > ul_carrier ? fp0->ul_CarrierFreq - ul_carrier : ul_carrier - fp0->ul_CarrierFreq;
    uint64_t this_carrier_distance = this_dl_carrier_distance + this_ul_carrier_distance;
    if (this_carrier_distance < carrier_distance) {
      UE->rf_map.card = ru_id;
      carrier_distance = this_carrier_distance;
    }
  }

  nrue_cells[current_cell_id].used_by_ue = -1;
  current_cell_id = nrue_rus[UE->rf_map.card].used_by_cell;
  nrue_cells[current_cell_id].used_by_ue = UE->Mod_id;

  openair0_config_t *cfg = &openair0_cfg_g[UE->rf_map.card];
  openair0_device_t *dev = &openair0_dev[UE->rf_map.card];
  nr_rf_card_config_freq(cfg, ul_carrier, dl_carrier, freq_offset);
  dev->trx_set_freq_func(dev, cfg);
}

void nrue_ru_get_freq(const PHY_VARS_NR_UE *UE, uint64_t *ul_carrier, uint64_t *dl_carrier, int channel)
{
  const openair0_config_t *cfg = &openair0_cfg_g[UE->rf_map.card];
  nr_rf_card_get_freq(cfg, ul_carrier, dl_carrier, channel);
}

int nrue_ru_adjust_rx_gain(PHY_VARS_NR_UE *UE, int gain_change)
{
  openair0_config_t *cfg = &openair0_cfg_g[UE->rf_map.card];
  openair0_device_t *dev = &openair0_dev[UE->rf_map.card];

  // Increase the RX gain by the value determined by adjust_rxgain
  cfg->rx_gain[0] += gain_change;

  // Set new RX gain.
  int ret_gain = dev->trx_set_gains_func(dev, cfg);
  // APPLY RX gain again if crossed the MAX RX gain threshold
  if (ret_gain < 0) {
    gain_change += ret_gain;
    cfg->rx_gain[0] += ret_gain;
    ret_gain = dev->trx_set_gains_func(dev, cfg);
  }

  int applied_rxgain = cfg->rx_gain[0] - cfg->rx_gain_offset[0];
  LOG_I(HW, "Rxgain adjusted by %d dB, RX gain: %d dB \n", gain_change, applied_rxgain);

  return gain_change;
}

int nrue_ru_read(PHY_VARS_NR_UE *UE, openair0_timestamp_t *ptimestamp, void **buff, int nsamps, int num_antennas)
{
  openair0_device_t *dev = &openair0_dev[UE->rf_map.card];
  openair0_timestamp_t tmp_timestamp;
  int ret = dev->trx_read_func(dev, &tmp_timestamp, buff, nsamps, num_antennas);
  if (!dev->firstTS_initialized) {
    dev->firstTS = tmp_timestamp;
    dev->firstTS_initialized = true;
  }
  *ptimestamp = tmp_timestamp - dev->firstTS;

  if (UE->Mod_id != 0)
    return ret;

  // UE 0 needs to read from all RUs that are not used by any other UE

  void *tmp_buf[num_antennas];
  uint32_t tmp_samples[nsamps];
  for (int ant = 0; ant < num_antennas; ant++)
    tmp_buf[ant] = tmp_samples;

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    int cell_id = nrue_rus[ru_id].used_by_cell;
    if (cell_id < 0) // skip RUs that have no cell definition
      continue;
    int ue_id = nrue_cells[cell_id].used_by_ue;
    if (ue_id >= 0) // skip cells that are already used by an UE
      continue;

    dev = &openair0_dev[ru_id];
    dev->trx_read_func(dev, &tmp_timestamp, tmp_buf, nsamps, num_antennas);
    if (!dev->firstTS_initialized) {
      dev->firstTS = tmp_timestamp;
      dev->firstTS_initialized = true;
    }
  }

  return ret;
}

int nrue_ru_write(PHY_VARS_NR_UE *UE, openair0_timestamp_t timestamp, void **buff, int nsamps, int num_antennas, int flags)
{
  openair0_device_t *dev = &openair0_dev[UE->rf_map.card];
  int ret = dev->trx_write_func(dev, timestamp + dev->firstTS, buff, nsamps, num_antennas, flags);

  if (UE->Mod_id != 0)
    return ret;

  // UE 0 needs to write to all RUs that are not used by any other UE

  void *tmp_buf[num_antennas];
  uint32_t tmp_samples[nsamps];
  memset(tmp_samples, 0, sizeof(tmp_samples));
  for (int ant = 0; ant < num_antennas; ant++)
    tmp_buf[ant] = tmp_samples;

  for (int ru_id = 0; ru_id < nrue_ru_count; ru_id++) {
    int cell_id = nrue_rus[ru_id].used_by_cell;
    if (cell_id < 0) // skip RUs that have no cell definition
      continue;
    int ue_id = nrue_cells[cell_id].used_by_ue;
    if (ue_id >= 0) // skip cells that are already used by an UE
      continue;

    dev = &openair0_dev[ru_id];
    dev->trx_write_func(dev, timestamp + dev->firstTS, tmp_buf, nsamps, num_antennas, flags);
  }
  return ret;
}

int nrue_ru_write_reorder(PHY_VARS_NR_UE *UE, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags)
{
  openair0_device_t *device = &openair0_dev[UE->rf_map.card];
  return openair0_write_reorder_common(nrue_ru_write, UE, device, timestamp, txp, nsamps, nbAnt, flags);
}

void nrue_ru_write_reorder_clear_context(PHY_VARS_NR_UE *UE)
{
  openair0_device_t *device = &openair0_dev[UE->rf_map.card];
  openair0_write_reorder_clear_context(device);
}
