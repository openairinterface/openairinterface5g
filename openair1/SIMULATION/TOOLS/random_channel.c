/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>

#include "PHY/TOOLS/tools_defs.h"
#include "sim.h"
#include "scm_corrmat.h"
#include "common/config/config_userapi.h"
#include "common/utils/telnetsrv/telnetsrv.h"
#include "common/utils/load_module_shlib.h"

// #define DEBUG_CH
// #define DOPPLER_DEBUG

#include "assertions.h"

extern void print_shorts(char *s,simde__m128i *x);
static mapping channelmod_names[] = {
  CHANNELMOD_MAP_INIT
};
static char *module_id_str[] = MODULEID_STR_INIT;
static int channelmod_show_cmd(char *buff, int debug, telnet_printfunc_t prnt);
static int channelmod_modify_cmd(char *buff, int debug, telnet_printfunc_t prnt);
static int channelmod_print_help(char *buff, int debug, telnet_printfunc_t prnt);
int get_modchannel_index(char *buf, int debug, void *vdata, telnet_printfunc_t prnt);
int get_channel_params(const char *buf, int debug, void *tdata, telnet_printfunc_t prnt);
int get_currentchannels_type(const char *buf, int debug, void *vdata, telnet_printfunc_t prnt);

#define HELP_WEBIF_MODIFCHAN_STRING "<channel index>"
static telnetshell_cmddef_t channelmod_cmdarray[] = {
    {"help", "", channelmod_print_help, {NULL}, 0, NULL},
    {"show", "<predef,current>", channelmod_show_cmd, {NULL}, TELNETSRV_CMDFLAG_TELNETONLY, NULL},
    {"show predef", "", channelmod_show_cmd, {NULL}, TELNETSRV_CMDFLAG_WEBSRVONLY, NULL},
    {"show current", "", channelmod_show_cmd, {NULL}, TELNETSRV_CMDFLAG_WEBSRVONLY, NULL},
    {"modify", "<channelid> <param> <value>", channelmod_modify_cmd, {NULL}, TELNETSRV_CMDFLAG_TELNETONLY, NULL},
    {"show params", "<channelid> <param> <value>", channelmod_modify_cmd, {.webfunc_getdata = get_currentchannels_type}, TELNETSRV_CMDFLAG_GETWEBTBLDATA | TELNETSRV_CMDFLAG_WEBSRV_SETRETURNTBL, NULL},
    {"show channelid",
     HELP_WEBIF_MODIFCHAN_STRING,
     channelmod_modify_cmd,
     {.webfunc_getdata = get_channel_params},
     TELNETSRV_CMDFLAG_NEEDPARAM | TELNETSRV_CMDFLAG_WEBSRVONLY | TELNETSRV_CMDFLAG_GETWEBTBLDATA,
     NULL},
    {"", "", NULL, {NULL}, 0, NULL},
};

static telnetshell_vardef_t channelmod_vardef[] = {{"", 0, 0, NULL}};

static unsigned int max_chan = 0;
static channel_desc_t **defined_channels = NULL;
static char *modellist_name = NULL;
static int noise_power_dBFS = INVALID_DBFS_VALUE;

static const double default_amps_lin[] = {0.3868472, 0.3094778, 0.1547389, 0.0773694, 0.0386847, 0.0193424, 0.0096712, 0.0038685};
static const double default_amp_lin[] = {1};

// Tap powers from linear values, normalized to a total power of 1
static void init_amps_lin(channel_desc_t *chan_desc, const double *amps_lin)
{
  chan_desc->amps = malloc(chan_desc->nb_taps * sizeof(*chan_desc->amps));
  double sum_amps = 0;
  for (int i = 0; i < chan_desc->nb_taps; i++) {
    chan_desc->amps[i] = amps_lin[i];
    sum_amps += chan_desc->amps[i];
  }
  for (int i = 0; i < chan_desc->nb_taps; i++) {
    chan_desc->amps[i] /= sum_amps;
  }
}

// Tap powers from values in dB, normalized to a total power of 1
static void init_amps(channel_desc_t *chan_desc, const double *amps_dB)
{
  double amps_lin[chan_desc->nb_taps];
  for (int i = 0; i < chan_desc->nb_taps; i++)
    amps_lin[i] = pow(10, 0.1 * amps_dB[i]);
  init_amps_lin(chan_desc, amps_lin);
}

static void init_delays(channel_desc_t *chan_desc, const double *delays)
{
  chan_desc->delays = malloc(chan_desc->nb_taps * sizeof(*chan_desc->delays));
  memcpy(chan_desc->delays, delays, chan_desc->nb_taps * sizeof(*chan_desc->delays));
}

static void fill_channel_desc(channel_desc_t *chan_desc, struct complexd *R_sqrt, uint8_t random_aoa)
{
  const int nb_rx = chan_desc->nb_rx;
  const int nb_tx = chan_desc->nb_tx;
  const int nb_taps = chan_desc->nb_taps;

  LOG_I(OCM,
        "[CHANNEL] Getting new channel descriptor, nb_tx %d, nb_rx %d, nb_taps %d, channel_length %d\n",
        nb_tx,
        nb_rx,
        nb_taps,
        chan_desc->channel_length);
  LOG_D(OCM, "[CHANNEL] Doing delays ...\n");

  if (chan_desc->amps == NULL) {
    if (nb_taps == 1) {
      init_amps_lin(chan_desc, default_amp_lin);
    } else {
      AssertFatal(nb_taps <= sizeofArray(default_amps_lin), "no default amplitudes for %d taps\n", nb_taps);
      init_amps_lin(chan_desc, default_amps_lin);
    }
  }

  if (chan_desc->delays == NULL) {
    chan_desc->delays = malloc(nb_taps * sizeof(*chan_desc->delays));
    const double delta_tau = chan_desc->Td / nb_taps;
    for (int i = 0; i < nb_taps; i++)
      chan_desc->delays[i] = i * delta_tau;
  }

  chan_desc->random_aoa = random_aoa;
  chan_desc->first_run = 1;
  chan_desc->ip = 0.0;
  chan_desc->Doppler_phase_cur = calloc(nb_rx, sizeof(double));

  LOG_D(OCM, "[CHANNEL] Doing R_sqrt ...\n");

  if (R_sqrt == NULL) {
    chan_desc->R_sqrt = calloc(nb_taps, sizeof(struct complexd *));
    chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_NTAPS;
    for (int i = 0; i < nb_taps; i++) {
      chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
      for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
        chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
    }
  } else {
    chan_desc->R_sqrt = calloc(nb_taps, sizeof(struct complexd *));
    for (int i = 0; i < nb_taps; i++) {
      /* all chan_desc share the same R_sqrt, coming from caller */
      chan_desc->R_sqrt[i] = R_sqrt;
    }
  }

  for (int i = 0; i < nb_taps; i++) {
    for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1) {
      LOG_D(OCM,"Rsqrt[%d][%d] %f %f\n",i,j,chan_desc->R_sqrt[i][j].r,chan_desc->R_sqrt[i][j].i);
    }
  }

  LOG_D(OCM,"[CHANNEL] RF %f\n",chan_desc->ricean_factor);

  for (int i = 0; i < chan_desc->nb_taps; i++)
    LOG_D(OCM,"[CHANNEL] tap %d: amp %f, delay %f\n",i,chan_desc->amps[i],chan_desc->delays[i]);

  chan_desc->nb_paths=10;
  reset_meas(&chan_desc->random_channel);
  reset_meas(&chan_desc->interp_time);
  reset_meas(&chan_desc->interp_freq);
  reset_meas(&chan_desc->convolution);
}

static const double mbsfn_delays[] =
    {0, .03, .15, .31, .37, 1.09, 12.490, 12.52, 12.64, 12.80, 12.86, 13.58, 27.49, 27.52, 27.64, 27.80, 27.86, 28.58};
static const double mbsfn_amps_dB[] =
    {0, -1.5, -1.4, -3.6, -0.6, -7.0, -10, -11.5, -11.4, -13.6, -10.6, -17.0, -20, -21.5, -21.4, -23.6, -20.6, -27};

static const double scm_c_delays[] = {0,
                                      0.0125,
                                      0.0250,
                                      0.3625,
                                      0.3750,
                                      0.3875,
                                      0.2500,
                                      0.2625,
                                      0.2750,
                                      1.0375,
                                      1.0500,
                                      1.0625,
                                      2.7250,
                                      2.7375,
                                      2.7500,
                                      4.6000,
                                      4.6125,
                                      4.6250};
static const double scm_c_amps_dB[] = {0.00,
                                       -2.22,
                                       -3.98,
                                       -1.86,
                                       -4.08,
                                       -5.84,
                                       -1.08,
                                       -3.30,
                                       -5.06,
                                       -9.08,
                                       -11.30,
                                       -13.06,
                                       -15.14,
                                       -17.36,
                                       -19.12,
                                       -20.64,
                                       -22.85,
                                       -24.62};

// TS 38.104 - Table G.2.1.1-2, delays normalized based on TR 38.901 - eq. 7.7-1
static const double tdl_a_delays[] = {0, 0.3333, 0.5000, 0.6667, 0.8333, 1.6667, 2.1667, 2.5000, 3.5000, 4.5000, 5.0000, 9.6667};
static const double tdl_a_amps_dB[] = {-15.5, 0.0, -5.1, -5.1, -9.6, -8.2, -13.1, -11.5, -11.0, -16.2, -16.6, -26.2};

// TS 38.104 - Table G.2.1.1-3, delays normalized based on TR 38.901 - eq. 7.7-1
static const double tdl_b_delays[] =
    {0.0000, 0.1000, 0.2000, 0.3000, 0.3500, 0.4500, 0.5500, 1.2000, 1.7000, 2.4500, 3.3000, 4.8000};
static const double tdl_b_amps_dB[] = {0.0, -2.2, -0.6, -0.6, -0.3, -1.2, -5.9, -2.2, -0.8, -6.3, -7.5, -7.1};

// TS 38.104 - Table G.2.1.1-4, delays normalized based on TR 38.901 - eq. 7.7-1
static const double tdl_c_delays[] =
    {0.0000, 0.2167, 0.2333, 0.6333, 0.6500, 0.6667, 0.8000, 1.0833, 1.7333, 3.4833, 5.0333, 8.6500};
static const double tdl_c_amps_dB[] = {-6.9, 0.0, -7.7, -2.5, -2.4, -9.9, -8.0, -6.6, -7.1, -13.0, -14.2, -16.0};

static const double tdl_d_delays[] = { // 0,
    0,
    0.035,
    0.612,
    1.363,
    1.405,
    1.804,
    2.596,
    1.775,
    4.042,
    7.937,
    9.424,
    9.708,
    12.525};

static const double tdl_d_amps_dB[] = { //-0.2,
    //-13.5,
    -.00147,
    -18.8,
    -21,
    -22.8,
    -17.9,
    -20.1,
    -21.9,
    -22.9,
    -27.8,
    -23.6,
    -24.8,
    -30.0,
    -27.7};

#define TDL_D_RICEAN_FACTOR .046774

static const double tdl_e_delays[] =
    {0, 0.5133, 0.5440, 0.5630, 0.5440, 0.7112, 1.9092, 1.9293, 1.9589, 2.6426, 3.7136, 5.4524, 12.0034, 20.6519};

static const double tdl_e_amps_dB[] = { //-0.03,
    //-22.03,
    -.00433,
    -15.8,
    -18.1,
    -19.8,
    -22.9,
    -22.4,
    -18.6,
    -20.8,
    -22.6,
    -22.3,
    -25.6,
    -20.2,
    -29.8,
    -29.2};

static const double ts_shift_delays[] = {0, 1 / 7.68};
static const double ts_shift_amps[] = {0, 1};

#define TDL_E_RICEAN_FACTOR 0.0063096

static const double epa_delays[] = {0, .03, .07, .09, .11, .19, .41};
static const double epa_amps_dB[] = {0.0, -1.0, -2.0, -3.0, -8.0, -17.2, -20.8};

static const double eva_delays[] = {0, .03, .15, .31, .37, .71, 1.09, 1.73, 2.51};
static const double eva_amps_dB[] = {0.0, -1.5, -1.4, -3.6, -0.6, -9.1, -7.0, -12.0, -16.9};

static const double etu_delays[] = {0, .05, .12, .2, .23, .5, 1.6, 2.3, 5.0};
static const double etu_amps_dB[] = {-1.0, -1.0, -1.0, 0.0, 0.0, 0.0, -3.0, -5.0, -7.0};

//correlation matrix for a 2x2 channel with full Tx correlation
static struct complexd R_sqrt_22_corr[16] = {{0.70711, 0},
                                             {0.0, 0.0},
                                             {0.70711, 0},
                                             {0.0, 0.0},
                                             {0.0, 0.0},
                                             {0.70711, 0},
                                             {0.0, 0.0},
                                             {0.70711, 0},
                                             {0.70711, 0},
                                             {0.0, 0.0},
                                             {0.70711, 0},
                                             {0.0, 0.0},
                                             {0.0, 0.0},
                                             {0.70711, 0},
                                             {0.0, 0.0},
                                             {0.70711, 0}};

//correlation matrix for a fully correlated 2x1 channel (h1==h2)
static struct complexd R_sqrt_21_corr[]  = {{0.70711,0}, {0.70711,0}, {0.70711,0}, {0.70711,0}};

//correlation matrix for a 2x2 channel with full Tx anti-correlation
static struct complexd R_sqrt_22_anticorr[16] = {{0.70711, 0},
                                                 {0.0, 0.0},
                                                 {-0.70711, 0},
                                                 {0.0, 0.0},
                                                 {0.0, 0.0},
                                                 {0.70711, 0},
                                                 {0.0, 0.0},
                                                 {-0.70711, 0},
                                                 {-0.70711, 0},
                                                 {0.0, 0.0},
                                                 {0.70711, 0},
                                                 {0.0, 0.0},
                                                 {0.0, 0.0},
                                                 {-0.70711, 0},
                                                 {0.0, 0.0},
                                                 {0.70711, 0}};

//correlation matrix for a fully anti-correlated 2x1 channel (h1==-h2)
static struct complexd R_sqrt_21_anticorr[4]  = {{0.70711,0}, {-0.70711,0}, {-0.70711,0}, {0.70711,0}};

// full correlation matrix in vectorized form for 2x2 channel, where h1 is  perfectly orthogonal to h2

static struct complexd R_sqrt_22_orthogonal[16] = {{0.70711, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.0, 0.0},
                                                   {0.70711, 0.0}};

// full correlation matrix for TM4 to make orthogonal effective channel
static struct complexd R_sqrt_22_orth_eff_ch_TM4_prec_real[16] = {{0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {-0.70711, 0.0},
                                                                  {0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.0},
                                                                  {-0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.70711, 0.0}};

static struct complexd R_sqrt_22_orth_eff_ch_TM4_prec_imag[16] = {{0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, -0.70711},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.70711},
                                                                  {0.0, -0.70711},
                                                                  {0.0, 0.0},
                                                                  {-0.70711, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.0},
                                                                  {0.0, 0.70711},
                                                                  {0.0, 0.0},
                                                                  {-0.70711, 0.0}};

//Correlation matrix for EPA channel
static struct complexd R_sqrt_22_EPA_low[16] = {{1.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {1.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {1.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {0.0, 0.0},
                                                {1.0, 0.0}};

static struct complexd R_sqrt_22_EPA_high[16] = {
  {0.7179,0.0}, {0.4500,0.0}, {0.4500,0.0}, {0.2821,0.0},
  {0.4500,0.0}, {0.7179,0.0}, {0.2821,0.0}, {0.4500,0.0},
  {0.4500,0.0}, {0.2821,0.0}, {0.7179,0.0}, {0.4500,0.0},
  {0.2821,0.0}, {0.4500,0.0}, {0.4500,0.0}, {0.7179,0.0}
};

static struct complexd R_sqrt_22_EPA_medium[16] = {{0.8375, 0.0},
                                                   {0.5249, 0.0},
                                                   {0.1286, 0.0},
                                                   {0.0806, 0.0},
                                                   {0.5249, 0.0},
                                                   {0.8375, 0.0},
                                                   {0.0806, 0.0},
                                                   {0.1286, 0.0},
                                                   {0.1286, 0.0},
                                                   {0.0806, 0.0},
                                                   {0.8375, 0.0},
                                                   {0.5249, 0.0},
                                                   {0.0806, 0.0},
                                                   {0.1286, 0.0},
                                                   {0.5249, 0.0},
                                                   {0.8375, 0.0}};

// Rayleigh1_orth_eff_ch_TM4

static void tdlModel(const int tdl_paths,
                     const double *tdl_delays,
                     const double *tdl_amps_dB,
                     double DS_TDL,
                     channel_desc_t *chan_desc)
{
  int nb_rx = chan_desc->nb_rx;
  int nb_tx = chan_desc->nb_tx;
  chan_desc->nb_taps = tdl_paths;
  chan_desc->Td = tdl_delays[tdl_paths - 1] * DS_TDL;
  printf("last path (%d) at %f * %e = %e\n", tdl_paths - 1, tdl_delays[tdl_paths - 1], DS_TDL, chan_desc->Td);
  chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                              + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
  printf("TDL : %f Ms/s, nb_taps %d, Td %e, channel_length %d\n",
         chan_desc->sampling_rate,
         tdl_paths,
         chan_desc->Td,
         chan_desc->channel_length);
  init_amps(chan_desc, tdl_amps_dB);
  chan_desc->delays = malloc(chan_desc->nb_taps * sizeof(*chan_desc->delays));
  for (int i = 0; i < chan_desc->nb_taps; i++)
    chan_desc->delays[i] = tdl_delays[i] * DS_TDL;

  chan_desc->aoa = 0;
  chan_desc->random_aoa = 0;

  const int matrix_size = nb_tx * nb_rx;
  // TS 38.104 Annex G spatial correlation matrix R (matrix_size x matrix_size, row major), NULL for low correlation
  const double *correlation_matrix = NULL;
  if (chan_desc->corr_level != CORR_LEVEL_LOW) {
    if (nb_rx == 1 && nb_tx == 2)
      correlation_matrix = &R12_medium_high[0][0];
    else if (nb_rx == 1 && nb_tx == 4)
      correlation_matrix = &R14_medium_high[0][0];
    else if (nb_rx == 1 && nb_tx == 8)
      correlation_matrix = &R18_medium_high[0][0];
    else if (nb_rx == 2 && nb_tx == 2 && chan_desc->corr_level == CORR_LEVEL_MEDIUM)
      correlation_matrix = &R22_medium[0][0];
    else if (nb_rx == 2 && nb_tx == 4 && chan_desc->corr_level == CORR_LEVEL_MEDIUM)
      correlation_matrix = &R24_medium[0][0];
    else if (nb_rx == 4 && nb_tx == 4 && chan_desc->corr_level == CORR_LEVEL_MEDIUM)
      correlation_matrix = &R44_medium[0][0];
    else if (nb_rx == 2 && nb_tx == 2 && chan_desc->corr_level == CORR_LEVEL_HIGH)
      correlation_matrix = &R22_high[0][0];
    else if (nb_rx == 2 && nb_tx == 4 && chan_desc->corr_level == CORR_LEVEL_HIGH)
      correlation_matrix = &R24_high[0][0];
    else if (nb_rx == 4 && nb_tx == 4 && chan_desc->corr_level == CORR_LEVEL_HIGH)
      correlation_matrix = &R44_high[0][0];
  }

  chan_desc->R_sqrt = calloc(matrix_size, sizeof(*chan_desc->R_sqrt));
  for (int row = 0; row < matrix_size; row++) {
    chan_desc->R_sqrt[row] = calloc(matrix_size, sizeof(**chan_desc->R_sqrt));
    if (correlation_matrix == NULL) {
      // TS 38.104 - Table G.2.3.1.2-4: MIMO correlation matrices for low correlation
      chan_desc->R_sqrt[row][row].r = 1.0;
    } else {
      for (int col = 0; col < matrix_size; col++) {
        chan_desc->R_sqrt[row][col].r = correlation_matrix[row * matrix_size + col];
      }
    }
  }
}

void get_cexp_doppler(struct complexd *cexp_doppler, channel_desc_t *chan_desc, const uint32_t length)
{
  // TS 38.104 - Table G.3-1
  uint16_t Dmin = 2;
  uint16_t Ds = 300;
  double v = chan_desc->max_Doppler * ((double)SPEED_OF_LIGHT / (double)chan_desc->center_freq);

#ifdef DOPPLER_DEBUG
  printf("v = %f\n", v);
#endif

  double phase0 = 2 * M_PI * uniformrandom();
  double cos_theta[length];
  double fs[length];

  for (uint32_t t_idx = 0; t_idx < length; t_idx++) {
    double t = t_idx / (chan_desc->sampling_rate * 1e6);
    if (t >= 0 && t <= Ds / v) {
      cos_theta[t_idx] = (Ds / 2 - v * t) / sqrt(Dmin * Dmin + (Ds / 2 - v * t) * (Ds / 2 - v * t));
    } else if (t > Ds / v && t <= 2 * Ds / v) {
      cos_theta[t_idx] = (-1.5 * Ds + v * t) / sqrt(Dmin * Dmin + (-1.5 * Ds + v * t) * (-1.5 * Ds + v * t));
    } else {
      cos_theta[t_idx] = cos(fmod(t, 2 * Ds / v));
    }
    fs[t_idx] = chan_desc->max_Doppler * cos_theta[t_idx];

    double complex tmp_cexp_doppler = cexp(I * (2 * M_PI * fs[t_idx] * t + phase0));
    cexp_doppler[t_idx].r = creal(tmp_cexp_doppler);
    cexp_doppler[t_idx].i = cimag(tmp_cexp_doppler);

#ifdef DOPPLER_DEBUG
    printf("(%2u) t_us = %f, cos_theta = %f, fs = %f, cexp_doppler = (%f, %f)\n", t_idx, t * 1e6, cos_theta[t_idx], fs[t_idx], cexp_doppler[t_idx].r, cexp_doppler[t_idx].i);
#endif
  }
}

double get_normalization_ch_factor(channel_desc_t *desc)
{
  if (!(desc->channel_length > 1 && desc->modelid >= TDL_A && desc->modelid <= TDL_E)) {
    return 1.0;
  }

  const uint N_average = 1000;
  double accumulated_ch_power = 0;

  for (uint n = 1; n <= N_average; n++) {
    struct complexd a[desc->nb_taps][desc->nb_tx][desc->nb_rx];
    for (uint l = 0; l < desc->nb_taps; l++) {
      const double coeff = sqrt(desc->ricean_factor * desc->amps[l] / 2);
      struct complexd anew[desc->nb_tx][desc->nb_rx];
      for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
        for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
          anew[aatx][aarx].r = coeff * gaussZiggurat(0.0, 1.0);
          anew[aatx][aarx].i = coeff * gaussZiggurat(0.0, 1.0);
          if ((l == 0) && (desc->ricean_factor != 1.0)) {
            anew[aatx][aarx].r += sqrt((1.0 - desc->ricean_factor) / 2);
            anew[aatx][aarx].i += sqrt((1.0 - desc->ricean_factor) / 2);
          }
        }
      }

      // Apply correlation matrix
      struct complexd acorr[desc->nb_tx][desc->nb_rx];
      bzero(acorr, sizeof(acorr));
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
          for (int aatx2 = 0; aatx2 < desc->nb_tx; aatx2++) {
            for (int aarx2 = 0; aarx2 < desc->nb_rx; aarx2++) {
              const cd_t tmp = cdMul(anew[aatx][aarx], desc->R_sqrt[aarx + aatx * desc->nb_rx][aarx2 + aatx2 * desc->nb_rx]);
              csum(acorr[aatx2][aarx2], tmp, acorr[aatx2][aarx2]);
            }
          }
        }
      }
      memcpy(a[l], acorr, sizeof(acorr));
    } // for (int l = 0; l < (int)desc->nb_taps; l++)

    for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int k = 0; k < desc->channel_length; k++) {
          cd_t ch = {};
          for (int l = 0; l < desc->nb_taps; l++) {
            const double tmp = M_PI * (k - (desc->delays[l] * desc->sampling_rate) - desc->channel_offset);
            double s = tmp != 0 ? sin(tmp) / tmp : 1.0;
            ch.r += s * a[l][aatx][aarx].r;
            ch.i += s * a[l][aatx][aarx].i;
          }
          accumulated_ch_power += squaredMod(ch);
        }
      }
    } // for (int aarx = 0; aarx < desc->nb_rx; aarx++)
  }

  return sqrt((N_average * desc->nb_tx * desc->nb_rx) / accumulated_ch_power);
}

channel_desc_t *new_channel_desc_scm(uint8_t nb_tx,
                                     uint8_t nb_rx,
                                     SCM_t channel_model,
                                     double sampling_rate,
                                     uint64_t center_freq,
                                     double channel_bandwidth,
                                     double DS_TDL,
                                     double maxDoppler,
                                     const corr_level_t corr_level,
                                     double forgetting_factor,
                                     uint64_t channel_offset,
                                     double path_loss_dB,
                                     float noise_power_dB)
{
  channel_desc_t *chan_desc = calloc(1, sizeof(channel_desc_t));

  if (defined_channels) { // case we use defined_channels
    int chan;
    for (chan = 0; chan < max_chan; chan++) {
      if (defined_channels[chan] == NULL) {
        defined_channels[chan] = chan_desc;
        chan_desc->chan_idx = chan;
        break;
      }
    }
    AssertFatal(chan != max_chan,
                "No more channel descriptors available, increase channelmod.max_chan parameter above %u\n",
                max_chan);
  }

  struct complexd *R_sqrt_ptr2;
  chan_desc->modelid                    = channel_model;
  chan_desc->nb_tx                      = nb_tx;
  chan_desc->nb_rx                      = nb_rx;
  chan_desc->sampling_rate              = sampling_rate;
  chan_desc->center_freq                = center_freq;
  chan_desc->channel_bandwidth          = channel_bandwidth;
  chan_desc->max_Doppler                = maxDoppler;
  chan_desc->corr_level                 = corr_level;
  chan_desc->forgetting_factor          = forgetting_factor;
  chan_desc->channel_offset             = channel_offset;
  chan_desc->path_loss_dB               = path_loss_dB;
  chan_desc->first_run                  = 1;
  chan_desc->ip                         = 0.0;
  chan_desc->noise_power_dB             = noise_power_dB;
  chan_desc->normalization_ch_factor    = 1.0;
  chan_desc->channelF_len = 275 * 12;
  LOG_I(OCM, "Channel Model (inside of new_channel_desc_scm)=%d\n\n", channel_model);

  /*  Spatial Channel Models (SCM)  channel model from TR 38.901 Section 7.7.2 */
  switch (channel_model) {
    case SCM_A:
      LOG_W(OCM, "channel model not yet supported\n");
      free(chan_desc);
      return (NULL);

    case SCM_B:
      LOG_W(OCM, "channel model not yet supported\n");
      free(chan_desc);
      return (NULL);

    case SCM_C:
      chan_desc->nb_taps = 18;
      chan_desc->Td = 4.625;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, scm_c_amps_dB);
      init_delays(chan_desc, scm_c_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;
      chan_desc->R_sqrt = calloc(6, sizeof(struct complexd **));

      if (nb_tx == 2 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R22_sqrt[i];
      } else if (nb_tx == 2 && nb_rx == 1) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R21_sqrt[i];
      } else if (nb_tx == 1 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R12_sqrt[i];
      } else {
        chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;
        for (int i = 0; i < 6; i++) {
          chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
          for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
            chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
          LOG_W(OCM, "correlation matrix not implemented for nb_tx==%d and nb_rx==%d, using identity\n", nb_tx, nb_rx);
        }
      }
      break;

    case SCM_D:
      LOG_W(OCM, "This is not the real SCM-D model! It is just SCM-C with an additional Rice factor!\n");
      chan_desc->nb_taps = 18;
      chan_desc->Td = 4.625;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, scm_c_amps_dB);
      init_delays(chan_desc, scm_c_delays);
      chan_desc->ricean_factor = 0.1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      chan_desc->R_sqrt = malloc(6 * sizeof(struct complexd **));

      if (nb_tx == 2 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R22_sqrt[i];
      } else if (nb_tx == 2 && nb_rx == 1) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R21_sqrt[i];
      } else if (nb_tx == 1 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R12_sqrt[i];
      } else {
        chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;

        for (int i = 0; i < 6; i++) {
          chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
          for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
            chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
          LOG_W(OCM, "correlation matrix not implemented for nb_tx==%d and nb_rx==%d, using identity\n", nb_tx, nb_rx);
        }
      }

      break;
      /*  tapped delay line (TDL)  channel model from TR 38.901 Section 7.7.2 */
#define tdl_m(MoDel)                                                                    \
  DevAssert(sizeof(tdl_##MoDel##_amps_dB) == sizeof(tdl_##MoDel##_delays));             \
  const int tdl_paths = sizeof(tdl_##MoDel##_amps_dB) / sizeof(*tdl_##MoDel##_amps_dB); \
  const double *tdl_delays = tdl_##MoDel##_delays;                                      \
  const double *tdl_amps_dB = tdl_##MoDel##_amps_dB

    case TDL_A: {
      chan_desc->ricean_factor = 1;
      tdl_m(a);
      tdlModel(tdl_paths, tdl_delays, tdl_amps_dB, DS_TDL, chan_desc);
    } break;

    case TDL_B: {
      chan_desc->ricean_factor = 1;
      tdl_m(b);
      tdlModel(tdl_paths, tdl_delays, tdl_amps_dB, DS_TDL, chan_desc);
    } break;

    case TDL_C: {
      chan_desc->ricean_factor = 1;
      tdl_m(c);
      tdlModel(tdl_paths, tdl_delays, tdl_amps_dB, DS_TDL, chan_desc);
    } break;

    case TDL_D: {
      chan_desc->ricean_factor = TDL_D_RICEAN_FACTOR;
      tdl_m(d);
      tdlModel(tdl_paths, tdl_delays, tdl_amps_dB, DS_TDL, chan_desc);
    } break;

    case TDL_E: {
      chan_desc->ricean_factor = TDL_E_RICEAN_FACTOR;
      tdl_m(e);
      tdlModel(tdl_paths, tdl_delays, tdl_amps_dB, DS_TDL, chan_desc);
    } break;

    case EPA:
      chan_desc->nb_taps = 7;
      chan_desc->Td = .410;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, epa_amps_dB);
      init_delays(chan_desc, epa_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      chan_desc->R_sqrt = malloc(6 * sizeof(struct complexd **));
      if (nb_tx == 2 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R22_sqrt[i];
      } else {
        chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;
        for (int i = 0; i < 6; i++) {
          chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
          for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
            chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
          LOG_W(OCM, "correlation matrix only implemented for nb_tx==2 and nb_rx==2, using identity\n");
        }
      }
      break;

    case EPA_low:
      chan_desc->nb_taps = 7;
      chan_desc->Td = .410;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, epa_amps_dB);
      init_delays(chan_desc, epa_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;
      if (nb_tx == 2 && nb_rx == 2) {
        chan_desc->R_sqrt = malloc(chan_desc->nb_taps * sizeof(struct complexd **));
        for (int i = 0; i < chan_desc->nb_taps; i++)
          chan_desc->R_sqrt[i] = R_sqrt_22_EPA_low;
      } else {
        printf("Correlation matrices are implemented for 2 x 2 only");
      }
      break;

    case EPA_high:
      chan_desc->nb_taps = 7;
      chan_desc->Td = .410;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, epa_amps_dB);
      init_delays(chan_desc, epa_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      if (nb_tx == 2 && nb_rx == 2) {
        chan_desc->R_sqrt = malloc(chan_desc->nb_taps * sizeof(struct complexd **));
        for (int i = 0; i < chan_desc->nb_taps; i++)
          chan_desc->R_sqrt[i] = R_sqrt_22_EPA_high;
      } else {
        printf("Correlation matrices are implemented for 2 x 2 only");
      }
      break;

    case EPA_medium:
      chan_desc->nb_taps = 7;
      chan_desc->Td = .410;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, epa_amps_dB);
      init_delays(chan_desc, epa_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      if (nb_tx == 2 && nb_rx == 2) {
        chan_desc->R_sqrt = malloc(chan_desc->nb_taps * sizeof(struct complexd **));
        for (int i = 0; i < chan_desc->nb_taps; i++)
          chan_desc->R_sqrt[i] = R_sqrt_22_EPA_medium;
      } else {
        printf("Correlation matrices are implemented for 2 x 2 only");
      }
      break;

    case EVA:
      chan_desc->nb_taps = 9;
      chan_desc->Td = 2.51;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, eva_amps_dB);
      init_delays(chan_desc, eva_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      chan_desc->R_sqrt = malloc(6 * sizeof(struct complexd **));
      if (nb_tx == 2 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R22_sqrt[i];
      } else {
        chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;
        for (int i = 0; i < 6; i++) {
          chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
          for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
            chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
          LOG_W(OCM, "correlation matrix only implemented for nb_tx==2 and nb_rx==2, using identity\n");
        }
      }
      break;

    case ETU:
      chan_desc->nb_taps = 9;
      chan_desc->Td = 5.0;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, etu_amps_dB);
      init_delays(chan_desc, etu_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      chan_desc->R_sqrt = malloc(6 * sizeof(struct complexd **));
      if (nb_tx == 2 && nb_rx == 2) {
        for (int i = 0; i < 6; i++)
          chan_desc->R_sqrt[i] = (struct complexd *)R22_sqrt[i];
      } else {
        chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;
        for (int i = 0; i < 6; i++) {
          chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
          for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
            chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
          LOG_W(OCM, "correlation matrix only implemented for nb_tx==2 and nb_rx==2, using identity\n");
        }
      }
      break;

    case MBSFN:
      chan_desc->nb_taps = 18;
      chan_desc->Td = 28.58;
      chan_desc->channel_length = 2 * chan_desc->sampling_rate * chan_desc->Td + 1
                                  + 2 / (M_PI * M_PI) * log(4 * M_PI * chan_desc->sampling_rate * chan_desc->Td);
      init_amps(chan_desc, mbsfn_amps_dB);
      init_delays(chan_desc, mbsfn_delays);
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0;
      chan_desc->random_aoa = 0;

      chan_desc->R_sqrt = malloc(6 * sizeof(struct complexd **));
      chan_desc->free_flags |= CHANMODEL_FREE_RSQRT_6;
      for (int i = 0; i < 6; i++) {
        chan_desc->R_sqrt[i] = calloc(nb_tx * nb_rx * nb_tx * nb_rx, sizeof(struct complexd));
        for (int j = 0; j < nb_tx * nb_rx * nb_tx * nb_rx; j += nb_tx * nb_rx + 1)
          chan_desc->R_sqrt[i][j] = (cd_t){1.0, 0.0};
        LOG_W(OCM, "correlation matrix only implemented for nb_tx==2 and nb_rx==2, using identity\n");
      }
      break;

    case Rayleigh8:
      chan_desc->Td = 0.8;
      chan_desc->channel_length = 11 + 2 * sampling_rate * chan_desc->Td;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 8;
      fill_channel_desc(chan_desc, NULL, 0);
      break;

    case Rice8:
      chan_desc->Td = 0.8;
      chan_desc->channel_length = 11 + 2 * sampling_rate * chan_desc->Td;
      chan_desc->ricean_factor = 0.1;
      chan_desc->aoa = 0.7854;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 8;
      fill_channel_desc(chan_desc, NULL, 1);
      break;

    case Rayleigh1: // MIMO Test uses Rayleigh1
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.0;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, NULL, 0);
      break;

    case Rayleigh1_800:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 800;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, NULL, 0);
      break;

    case Rayleigh1_corr:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;

      if ((nb_tx == 2) && (nb_rx == 1)) {
        R_sqrt_ptr2 = R_sqrt_21_corr;
      } else if ((nb_tx == 2) && (nb_rx == 2)) {
        R_sqrt_ptr2 = R_sqrt_22_corr;
      } else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case Rayleigh1_anticorr:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 1) { // check this
        R_sqrt_ptr2 = R_sqrt_21_anticorr;
      } else if (nb_tx == 2 && nb_rx == 2) {
        R_sqrt_ptr2 = R_sqrt_22_anticorr;
      } else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case Rice1:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.1;
      chan_desc->aoa = 0.7854;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, NULL, 0);
      break;

    case AWGN:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.0;
      chan_desc->aoa = 0.0;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, NULL, 0);
      printf("AWGN: ricean_factor %f\n", chan_desc->ricean_factor);
      break;

    case TS_SHIFT:
      chan_desc->nb_taps = 2;
      chan_desc->Td = chan_desc->delays[1];
      chan_desc->channel_length = 10;
      chan_desc->ricean_factor = 0.0;
      chan_desc->aoa = 0.0;
      chan_desc->max_Doppler = 0;
      init_amps_lin(chan_desc, ts_shift_amps);
      init_delays(chan_desc, ts_shift_delays);
      fill_channel_desc(chan_desc, NULL, 0);
      printf("TS_SHIFT: ricean_factor %f\n", chan_desc->ricean_factor);
      break;

    case Rice1_corr:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 1) {
        R_sqrt_ptr2 = R_sqrt_21_corr;
      } else if (nb_tx == 2 && nb_rx == 2) {
        R_sqrt_ptr2 = R_sqrt_22_corr;
      } else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 1);
      break;

    case Rice1_anticorr:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 1) {
        R_sqrt_ptr2 = R_sqrt_21_anticorr;
      } else if (nb_tx == 2 && nb_rx == 2) {
        R_sqrt_ptr2 = R_sqrt_22_anticorr;
      } else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 1);
      break;

    case Rayleigh1_orthogonal:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0.03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 2)
        R_sqrt_ptr2 = R_sqrt_22_orthogonal;
      else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case Rayleigh1_orth_eff_ch_TM4_prec_real:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0.03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 2)
        R_sqrt_ptr2 = R_sqrt_22_orth_eff_ch_TM4_prec_real;
      else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 1);
      break;

    case Rayleigh1_orth_eff_ch_TM4_prec_imag:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = 0.03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 2)
        R_sqrt_ptr2 = R_sqrt_22_orth_eff_ch_TM4_prec_imag;
      else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case Rayleigh8_orth_eff_ch_TM4_prec_real:
      if (nb_tx == 2 && nb_rx == 2)
        R_sqrt_ptr2 = R_sqrt_22_orth_eff_ch_TM4_prec_real;
      else
        R_sqrt_ptr2 = NULL;

      chan_desc->Td = 0.8;
      chan_desc->channel_length = 11 + 2 * sampling_rate * chan_desc->Td;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;
      chan_desc->nb_taps = 8;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case Rayleigh8_orth_eff_ch_TM4_prec_imag:
      chan_desc->Td = 0.8;
      chan_desc->channel_length = 11 + 2 * sampling_rate * chan_desc->Td;
      chan_desc->ricean_factor = 1;
      chan_desc->aoa = .03;
      chan_desc->max_Doppler = 0;

      if (nb_tx == 2 && nb_rx == 2)
        R_sqrt_ptr2 = R_sqrt_22_orth_eff_ch_TM4_prec_imag;
      else
        R_sqrt_ptr2 = NULL;
      chan_desc->nb_taps = 8;
      fill_channel_desc(chan_desc, R_sqrt_ptr2, 0);
      break;

    case SAT_LEO_TRANS:
    case SAT_LEO_REGEN:
      chan_desc->Td = 0;
      chan_desc->channel_length = 1;
      chan_desc->ricean_factor = 0.0;
      chan_desc->aoa = 0.0;
      chan_desc->max_Doppler = 0;
      chan_desc->sat_height = 600e3;
      chan_desc->enable_dynamic_delay = true;
      chan_desc->enable_dynamic_Doppler = true;
      chan_desc->nb_taps = 1;
      fill_channel_desc(chan_desc, NULL, 0);
      printf("%s: satellite orbit height %f km\n", map_int_to_str(channelmod_names, channel_model), chan_desc->sat_height / 1000);
      break;

    default:
      LOG_W(OCM, "channel model not yet supported\n");
      free(chan_desc);
      return (NULL);
  }

  chan_desc->normalization_ch_factor = get_normalization_ch_factor(chan_desc);

  LOG_D(OCM,"[CHANNEL] RF %f\n",chan_desc->ricean_factor);

  for (int i = 0; i < chan_desc->nb_taps; i++)
    LOG_D(OCM,"[CHANNEL] tap %d: amp %f, delay %f\n",i,chan_desc->amps[i],chan_desc->delays[i]);

  chan_desc->nb_paths = 10;
  return(chan_desc);
} /* channel_desc_t *new_channel_desc_scm  */

channel_desc_t *find_channel_desc_fromname(const char *modelname) {
  if (defined_channels)
    for (int i = 0; i < max_chan; i++)
      if (strcmp(defined_channels[i]->model_name,modelname) == 0)
        return defined_channels[i];

  LOG_E(OCM,"Model %s not found \n", modelname);
  return NULL;
}

void free_channel_desc_scm(channel_desc_t *ch) {
  // Must be made cleanly, a lot of leaks...
  if (defined_channels) {
    if (ch->chan_idx >= max_chan)
      LOG_E(OCM, "Channel idx impossible %d/%d\n", ch->chan_idx, max_chan);
    else
      defined_channels[ch->chan_idx] = NULL;
  }

  free(ch->amps);
  free(ch->delays);

  if(ch->free_flags&CHANMODEL_FREE_RSQRT_6)
    for (int i = 0; i<6; i++)
      free(ch->R_sqrt[i]);

  if(ch->free_flags&CHANMODEL_FREE_RSQRT_NTAPS)
    for (int i = 0; i<ch->nb_taps; i++)
      free(ch->R_sqrt[i]);

  free(ch->R_sqrt);
  free(ch->Doppler_phase_cur);
  free(ch->ch);
  free(ch->chF);
  free(ch->a);
  free(ch->model_name);
  free(ch);
}

void set_channeldesc_owner(channel_desc_t *cdesc, channelmod_moduleid_t module_id)
{
  cdesc->module_id = module_id;
}

void set_channeldesc_name(channel_desc_t *cdesc,char *modelname) {
  if(cdesc->model_name != NULL)
    free(cdesc->model_name);

  cdesc->model_name=strdup(modelname);
}

void set_channeldesc_direction(channel_desc_t *cdesc, bool is_uplink)
{
  cdesc->is_uplink = is_uplink;
}

int random_channel(channel_desc_t *desc, uint8_t abstraction_flag)
{
  start_meas(&desc->random_channel);

  // For AWGN and SAT_LEO_* channels, the received signal (Srx) is equal to transmitted signal (Stx) plus noise (N), i.e., Srx = Stx + N,
  //  therefore, the channel matrix is the identity matrix.

  allocCast3D(ch, struct complexd, desc->ch, desc->nb_tx, desc->nb_rx, desc->channel_length, false);
  allocCast3D(a, struct complexd, desc->a, desc->nb_taps, desc->nb_tx, desc->nb_rx, true);

  if (desc->modelid == AWGN || desc->modelid == SAT_LEO_TRANS || desc->modelid == SAT_LEO_REGEN) {
    for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        ch[aatx][aarx][0].r = aarx % desc->nb_tx == aatx ? 1.0 : 0.0;
        ch[aatx][aarx][0].i = 0.0;
        a[0][aatx][aarx] = ch[aatx][aarx][0];
      }
    }

    stop_meas(&desc->random_channel);
    desc->first_run = 0;
    return 0;
  }

  for (int i = 0; i < desc->nb_taps; i++) {
    struct complexd anew[desc->nb_tx][desc->nb_rx];
    const double tmp = sqrt(desc->ricean_factor * desc->amps[i] / 2) * desc->normalization_ch_factor;
    for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        anew[aatx][aarx] = (struct complexd){tmp * gaussZiggurat(0.0, 1.0), tmp * gaussZiggurat(0.0, 1.0)};
        if (i == 0 && desc->ricean_factor != 1.0) {
          if (desc->random_aoa==1) {
            desc->aoa = uniformrandom()*2*M_PI;
          }

          // this assumes that both RX and TX have linear antenna arrays with lambda/2 antenna spacing.
          // Furhter it is assumed that the arrays are parallel to each other and that they are far enough apart so
          // that we can safely assume plane wave propagation.
          struct complexd phase = {cos(M_PI * (aarx - aatx) * sin(desc->aoa)), sin(M_PI * (aarx - aatx) * sin(desc->aoa))};
          anew[aatx][aarx].r += phase.r * sqrt(1.0 - desc->ricean_factor) * desc->normalization_ch_factor;
          anew[aatx][aarx].i += phase.i * sqrt(1.0 - desc->ricean_factor) * desc->normalization_ch_factor;
        }

#ifdef DEBUG_CH
        printf("(%d,%d,%d) %f->(%f,%f) aoa %f ricean_factor %f\n",
               aarx,
               aatx,
               i,
               desc->amps[i],
               anew[aatx][aarx].r,
               anew[aatx][aarx].i,
               desc->aoa,
               desc->ricean_factor);
#endif
      } // aatx
    } // aarx

    // apply correlation matrix
    // compute acorr = R_sqrt[i] * anew
    struct complexd acorr[desc->nb_tx][desc->nb_rx];
    bzero(acorr, sizeof(acorr));
    if (desc->modelid >= TDL_A && desc->modelid <= TDL_E) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
          for (int aatx2 = 0; aatx2 < desc->nb_tx; aatx2++) {
            for (int aarx2 = 0; aarx2 < desc->nb_rx; aarx2++) {
              const cd_t tmp = cdMul(anew[aatx][aarx], desc->R_sqrt[aarx + aatx * desc->nb_rx][aarx2 + aatx2 * desc->nb_rx]);
              csum(acorr[aatx2][aarx2], tmp, acorr[aatx2][aarx2]);
            }
          }
        }
      }
    } else {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
          const cd_t tmp = cdMul(desc->R_sqrt[i / 3][0], anew[aatx][aarx]);
          csum(acorr[aatx][aarx], tmp, acorr[aatx][aarx]);
        }
      }
    }

    if (desc->first_run==1) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
          a[i][aatx][aarx] = acorr[aatx][aarx];
        }
      }
    } else {
      // a = alpha*acorr+beta*a
      // a = beta*a
      // a = a+alpha*acorr
      struct complexd alpha = {sqrt(1 - desc->forgetting_factor), 0};
      struct complexd beta = {sqrt(desc->forgetting_factor), 0};
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
          a[i][aatx][aarx] = cdMul(beta, a[i][aatx][aarx]);
          const cd_t tmp = cdMul(alpha, acorr[aatx][aarx]);
          csum(a[i][aatx][aarx], tmp, a[i][aatx][aarx]);
        }
      }
    }
  } // nb_taps

  stop_meas(&desc->random_channel);

  if (abstraction_flag==0) {
    start_meas(&desc->interp_time);

    for (int aarx = 0; aarx < desc->nb_rx; aarx++) {
      for (int aatx = 0; aatx < desc->nb_tx; aatx++) {
        if (desc->channel_length == 1) {
          ch[aatx][aarx][0] = a[0][aatx][aarx];
        } else {
          for (int k = 0; k < desc->channel_length; k++) {
            ch[aatx][aarx][k] = (struct complexd){};
            for (int l = 0; l < desc->nb_taps; l++) {
              double tmp = M_PI * (k - (desc->delays[l] * desc->sampling_rate) - desc->channel_offset);
              double s = tmp == 0 ? 1.0 : sin(tmp) / tmp;
              ch[aatx][aarx][k].r += s * a[l][aatx][aarx].r;
              ch[aatx][aarx][k].i += s * a[l][aatx][aarx].i;
            } // nb_taps

#ifdef DEBUG_CH
            printf("(%d,%d,%d)->(%e,%e)\n", k, aarx, aatx, ch[aatx][aarx][k].r, ch[aatx][aarx][k].i);
#endif

          } // channel_length
        }
      } // aatx
    } // aarx

    stop_meas(&desc->interp_time);
  }

  desc->first_run = 0;
  return (0);
}

double N_RB2sampling_rate(uint16_t N_RB) {
  double sampling_rate;

  switch (N_RB) {
    case 6:
      sampling_rate = 1.92;
      break;

    case 25:
      sampling_rate = 7.68;
      break;

    case 50:
      sampling_rate = 15.36;
      break;

    case 100:
      sampling_rate = 30.72;
      break;

    default:
      AssertFatal(1 == 0, "Unknown N_PRB %d", N_RB);
  }

  return(sampling_rate);
}

double N_RB2channel_bandwidth(uint16_t N_RB) {
  double channel_bandwidth;

  switch (N_RB) {
    case 6:
      channel_bandwidth = 1.25;
      break;

    case 25:
      channel_bandwidth = 5.00;
      break;

    case 50:
      channel_bandwidth = 10.00;
      break;

    case 100:
      channel_bandwidth = 20.00;
      break;

    default:
      LOG_E(OCM, "Unknown N_PRB\n");
      return (-1);
  }

  return(channel_bandwidth);
}

/*-----------------------------------------------------------------------------------------------------------*/
/* functions for telnet server and webserver                                                                 */
static int channelmod_print_help(char *buff, int debug, telnet_printfunc_t prnt ) {
  prnt("channelmod commands can be used to display or modify channel models parameters\n");
  prnt("channelmod show predef: display predefined model algorithms available in oai\n");
  prnt("channelmod show current: display the currently used models in the running executable\n");
  prnt("channelmod modify <model index> <param name> <param value>: set the specified parameters in a current model to the given value\n");
  prnt("                  <model index> specifies the model, the show current model command can be used to list the current models indexes\n");
  prnt("                  <param name> can be one of \"riceanf\", \"aoa\", \"randaoa\", \"ploss\", \"noise_power_dB\", \"offset\", \"forgetf\"\n");
  return CMDSTATUS_FOUND;
}

static char *pnames[] = {"riceanf", "aoa", "randaoa", "ploss", "noise_power_dB", "offset", "forgetf", NULL};
static char *pformat[] = {"%lf", "%lf", "%i", "%lf", "%lf", "%i", "%lf", NULL};
int get_channel_params(const char *buf, int debug, void *vdata, telnet_printfunc_t prnt)
{
  if (buf == NULL) {
    LOG_I(UTIL, "%s received NULL buffer\n", __FUNCTION__);
    return -1;
  }
  if (debug)
    LOG_I(UTIL, "%s received %s\n", __FUNCTION__, buf);
  int chanidx = 0;
  webdatadef_t *tdata = (webdatadef_t *)vdata;
  if (strstr(buf, "show") == buf) {
    if (tdata->lines[0].val[0] != NULL) {
      chanidx = strtol(tdata->lines[0].val[0], NULL, 0);
    } else {
      LOG_I(UTIL, "Channel index set to 0, not available in received data\n");
    }
    if (tdata != NULL && defined_channels[chanidx] != NULL) {
      tdata->numcols = 2;
      snprintf(tdata->columns[0].coltitle, sizeof(tdata->columns[0].coltitle), "parameter");
      tdata->columns[0].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_RDONLY;
      snprintf(tdata->columns[1].coltitle, sizeof(tdata->columns[1].coltitle), "value");
      tdata->columns[1].coltype = TELNET_VARTYPE_STRING | TELNET_VAR_NEEDFREE;
      tdata->numlines = 0;
      channel_desc_t *cd = defined_channels[chanidx];
      void *valptr[] = {&cd->ricean_factor,
                        &cd->aoa,
                        &cd->random_aoa,
                        &cd->path_loss_dB,
                        &cd->noise_power_dB,
                        &cd->channel_offset,
                        &cd->forgetting_factor};
      for (int i = 0; pnames[i] != NULL; i++) {
        tdata->lines[tdata->numlines].val[0] = malloc(strlen(pnames[i] + 1));
        tdata->lines[tdata->numlines].val[1] = malloc(64);
        strcpy(tdata->lines[tdata->numlines].val[0], pnames[i]);
        if (pformat[i][1] == 'i') {
          snprintf(tdata->lines[tdata->numlines].val[1], 64, pformat[i], *(int *)valptr[i]);
        } else {
          snprintf(tdata->lines[tdata->numlines].val[1], 64, pformat[i], *(double *)valptr[i]);
        }
        tdata->numlines++;
      }
      return tdata->numlines;
    }
  } /* show */ else if (strstr(buf, "set") == buf) {
    char cmdbuf[TELNET_MAX_MSGLENGTH];
    int sst = sscanf(tdata->tblname, "%*[^=]=%i", &chanidx);
    if (sst == 1) {
      int pidx = tdata->numlines;
      if (pformat[pidx][1] == 'i') {
        sprintf(cmdbuf, "channelmod modify %i %s %s", chanidx, pnames[pidx], tdata->lines[0].val[0]);
      } else {
        sprintf(cmdbuf, "channelmod modify %i %s %s", chanidx, pnames[pidx], tdata->lines[0].val[1]);
      }
      channelmod_modify_cmd(cmdbuf, debug, prnt);
      return CMDSTATUS_FOUND;
    } else {
      prnt("  channel index not found in cannelmod command\n");
    }
  } else {
    prnt("%s not implemented\n", buf);
  }

  return CMDSTATUS_NOTFOUND;

} /* get_currentchannel_type */

static void display_channelmodel(channel_desc_t *cd,int debug, telnet_printfunc_t prnt) {
  prnt("model owner: %s\n",(cd->module_id != 0)?module_id_str[cd->module_id]:"not set");
  prnt("nb_tx: %i    nb_rx: %i    taps: %i bandwidth: %lf    sampling: %lf\n",cd->nb_tx, cd->nb_rx, cd->nb_taps, cd->channel_bandwidth, cd->sampling_rate);
  prnt("channel length: %i    Max path delay: %lf   ricean fact.: %lf    angle of arrival: %lf (randomized:%s)\n",
       cd->channel_length, cd->Td, cd->ricean_factor, cd->aoa, (cd->random_aoa?"Yes":"No"));
  prnt("max Doppler: %lf    path loss: %lf  noise: %lf rchannel offset: %lu    forget factor; %lf\n",
       cd->max_Doppler,
       cd->path_loss_dB,
       cd->noise_power_dB,
       cd->channel_offset,
       cd->forgetting_factor);
  prnt("Initial phase: %lf   nb_path: %i \n",
       cd->ip, cd->nb_paths);
  if (cd->modelid == SAT_LEO_TRANS || cd->modelid == SAT_LEO_REGEN)
    prnt("satellite orbit height: %f\n", cd->sat_height);

  for (int i=0; i<cd->nb_taps ; i++) {
    prnt("taps: %i   lin. ampli. : %lf    delay: %lf \n",i,cd->amps[i], cd->delays[i]);
  }
}

int get_currentchannels_type(const char *buf, int debug, void *vdata, telnet_printfunc_t prnt)
{
  webdatadef_t *tdata;
  if (buf == NULL) {
    LOG_I(UTIL, "%s received NULL buffer\n", __FUNCTION__);
    return -1;
  }
  if (debug)
    LOG_I(UTIL, "%s received %s\n", __FUNCTION__, buf);

  if (vdata != NULL) {
    tdata = (webdatadef_t *)vdata;
  } else {
    LOG_I(UTIL, "%s vdata is NULL\n", __FUNCTION__);
    return -1;
  }

  if (strncmp(buf, "set", 3) == 0) {
    tdata->numcols = 1;
    return get_channel_params(buf, debug, vdata, prnt);
  }

  tdata->numcols = 4;
  snprintf(tdata->columns[0].coltitle, sizeof(tdata->columns[0].coltitle), "model index");
  tdata->columns[0].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_RDONLY | TELNET_VAR_NEEDFREE;
  snprintf(tdata->columns[1].coltitle, sizeof(tdata->columns[1].coltitle), "model name");
  tdata->columns[1].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_RDONLY;
  snprintf(tdata->columns[2].coltitle, sizeof(tdata->columns[2].coltitle), "module owner");
  tdata->columns[2].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_RDONLY;
  snprintf(tdata->columns[3].coltitle, sizeof(tdata->columns[3].coltitle), "algorithm");
  tdata->columns[3].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_SIMALGO;
  tdata->numlines = 0;
  for (int i = 0; i < max_chan && i < TELNET_MAXLINE_NUM; i++) {
    if (defined_channels[i] != NULL) {
      tdata->lines[tdata->numlines].val[0] = malloc(64);
      snprintf(tdata->lines[tdata->numlines].val[0], 64, "%02u", (unsigned int)i);
      tdata->lines[tdata->numlines].val[1] = (defined_channels[i]->model_name != NULL) ? defined_channels[i]->model_name : "(not set)";
      tdata->lines[tdata->numlines].val[2] = (defined_channels[i]->module_id != 0) ? module_id_str[defined_channels[i]->module_id] : "not set";
      tdata->lines[tdata->numlines].val[3] = map_int_to_str(channelmod_names, defined_channels[i]->modelid);
      tdata->numlines++;
    }
  }
  return tdata->numlines;
} /* get_currentchannel_type */

static int channelmod_show_cmd(char *buff, int debug, telnet_printfunc_t prnt) {
  char *subcmd=NULL;
  int s;
  if (buff == NULL) {
    subcmd = strdup(""); // enforce help display
    s = 2;
  } else {
    s = sscanf(buff, "%ms\n", &subcmd);
  }

  if (s>0) {
    if ( strcmp(subcmd,"predef") == 0) {
      for (int i=0; channelmod_names[i].name != NULL ; i++) {
        prnt("  %i %s\n", i, map_int_to_str(channelmod_names,i ));
      }
    } else if ( strcmp(subcmd,"current") == 0) {
      for (int i=0; i < max_chan ; i++) {
        if (defined_channels[i] != NULL) {
          prnt("model %i %s type %s:\n", i, (defined_channels[i]->model_name !=NULL)?defined_channels[i]->model_name:"(no name set)",
               map_int_to_str(channelmod_names,defined_channels[i]->modelid));
          display_channelmodel(defined_channels[i],debug,prnt);
          prnt("----------------\n");
        }
      }
    } else {
      channelmod_print_help(buff, debug, prnt);
    }

    free(subcmd);
  }

  return CMDSTATUS_FOUND;
}



static int channelmod_modify_cmd(char *buff, int debug, telnet_printfunc_t prnt) {
  char *param=NULL, *value=NULL;
  int cd_id= -1;
  int s = sscanf(buff, "%i %ms %ms \n", &cd_id, &param, &value);

  if (cd_id<0 || cd_id >= max_chan) {
    prnt("ERROR, %i: Channel model id outof range (0-%i)\n",cd_id,max_chan-1);
    return CMDSTATUS_FOUND;
  }

  if (defined_channels[cd_id]==NULL) {
    prnt("ERROR, %i: Channel model has not been set\n",cd_id);
    return CMDSTATUS_FOUND;
  }

  if (s==3) {
    if ( strcmp(param,"riceanf") == 0) {
      double dbl = atof(value);

      if (dbl <0 || dbl > 1)
        prnt("ERROR: ricean factor range: 0 to 1, %lf is outof range\n",dbl);
      else
        defined_channels[cd_id]->ricean_factor=dbl;
    } else if ( strcmp(param,"aoa") == 0) {
      double dbl = atof(value);

      if (dbl <0 || dbl >6.28)
        prnt("ERROR: angle of arrival range: 0 to 2*Pi,  %lf is outof range\n",dbl);
      else
        defined_channels[cd_id]->aoa=dbl;
    } else if ( strcmp(param,"randaoa") == 0) {
      int i = atoi(value);

      if (i!=0 && i!=1)
        prnt("ERROR: randaoa is a boolean, must be 0 or 1\n");
      else
        defined_channels[cd_id]->random_aoa=i;
    } else if ( strcmp(param,"ploss") == 0) {
      double dbl = atof(value);
      defined_channels[cd_id]->path_loss_dB=dbl;
    } else if ( strcmp(param,"noise_power_dB") == 0) {
      double dbl = atof(value);
      defined_channels[cd_id]->noise_power_dB=dbl;
    } else if ( strcmp(param,"offset") == 0) {
      int i = atoi(value);
      defined_channels[cd_id]->channel_offset=i;
    } else if ( strcmp(param,"forgetf") == 0) {
      double dbl = atof(value);

      if (dbl <0 || dbl > 1)
        prnt("ERROR: forgetting factor range: 0 to 1 (disable variation), %lf is outof range\n",dbl);
      else
        defined_channels[cd_id]->forgetting_factor=dbl;
    } else {
      prnt("ERROR: %s, unknown channel parameter\n",param);
      return CMDSTATUS_FOUND;
    }
    display_channelmodel(defined_channels[cd_id],debug,prnt);
    free(param);
    free(value);
    random_channel(defined_channels[cd_id],false);
  }

  return CMDSTATUS_FOUND;
}

int get_modchannel_index(char *buf, int debug, void *vdata, telnet_printfunc_t prnt)
{
  if (buf == NULL) {
    LOG_I(UTIL, "%s received NULL buffer\n", __FUNCTION__);
    return -1;
  }
  if (debug)
    LOG_I(UTIL, "%s received %s\n", __FUNCTION__, buf);
  webdatadef_t *tdata = (webdatadef_t *)vdata;
  if (strncmp(buf, "set", 3) == 0) {
    return get_channel_params(buf, debug, vdata, prnt);
  }
  int numlines = 0;
  if (tdata != NULL) {
    tdata->numlines = 0;
    for (int i = 0; i < max_chan; i++) {
      if (defined_channels[i] != NULL) {
        tdata->numlines++;
      }
    }
    tdata->numcols = 0;
    if (tdata->numlines > 0)
      snprintf(tdata->tblname, sizeof(tdata->tblname) - 1, "Running channel index (0-%i)", (tdata->numlines - 1));
    else {
      snprintf(tdata->tblname, sizeof(tdata->tblname) - 1, "No running model in the system");
    }
    numlines = tdata->numlines;
  }
  return numlines;
} /* get_currentchannel_type */
/*------------------------------------------------------------------------------------------------------------------*/

int modelid_fromstrtype(char *modeltype) {
  int modelid = map_str_to_int(channelmod_names, modeltype);
  if (modelid < 0)
    LOG_E(OCM, "random_channel.c: Error channel model %s unknown\n", modeltype);
  return modelid;
}

void init_channelmod(void) {
  paramdef_t channelmod_params[] = CHANNELMOD_PARAMS_DESC;
  int numparams = sizeofArray(channelmod_params);
  int ret = config_get(config_get_if(), channelmod_params, numparams, CHANNELMOD_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed");
  if (defined_channels == NULL) {
    defined_channels = calloc(max_chan, sizeof(channel_desc_t *));
    AssertFatal(defined_channels != NULL, "couldn't allocate %u channel descriptors\n", max_chan);
  }
  /* look for telnet server, if it is loaded, add the channel modeling commands to it */
  add_telnetcmd_func_t addcmd = (add_telnetcmd_func_t)get_shlibmodule_fptr("telnetsrv", TELNET_ADDCMD_FNAME);

  if (addcmd != NULL) {
    addcmd("channelmod",channelmod_vardef,channelmod_cmdarray);
  }
}

int load_channellist(uint8_t nb_tx, uint8_t nb_rx, double sampling_rate, uint64_t center_freq, double channel_bandwidth) {
  paramdef_t achannel_params[] = CHANNELMOD_MODEL_PARAMS_DESC;
  paramlist_def_t channel_list;
  memset(&channel_list,0,sizeof(paramlist_def_t));
  memcpy(channel_list.listname,modellist_name,sizeof(channel_list.listname)-1);
  int numparams = sizeofArray(achannel_params);
  config_getlist(config_get_if(), &channel_list, achannel_params, numparams, CHANNELMOD_SECTION);
  AssertFatal(channel_list.numelt>0, "List %s.%s not found in config file\n",CHANNELMOD_SECTION,channel_list.listname);
  int pindex_NAME = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_NAME_PNAME);
  int pindex_DT = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_DT_PNAME );
  int pindex_FF = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_FF_PNAME );
  int pindex_CO = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_CO_PNAME );
  int pindex_PL = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_PL_PNAME );
  int pindex_NP = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_NP_PNAME );
  int pindex_TYPE = config_paramidx_fromname(achannel_params,numparams, CHANNELMOD_MODEL_TYPE_PNAME);

  for (int i=0; i<channel_list.numelt; i++) {
    int modid = modelid_fromstrtype( *(channel_list.paramarray[i][pindex_TYPE].strptr) );

    if (modid <0) {
      LOG_E(OCM,"Valid channel model types:\n");

      for (int m=0; channelmod_names[i].name != NULL ; m++) {
        printf(" %s ", map_int_to_str(channelmod_names,m ));
      }

      AssertFatal(0, "\n  Choose a valid model type\n");
    }
    paramdef_t *param = channel_list.paramarray[i];
    channel_desc_t *channeldesc_p = new_channel_desc_scm(nb_tx,
                                                         nb_rx,
                                                         modid,
                                                         sampling_rate,
                                                         center_freq,
                                                         channel_bandwidth,
                                                         *param[pindex_DT].dblptr,
                                                         0.0,
                                                         CORR_LEVEL_LOW,
                                                         *param[pindex_FF].dblptr,
                                                         *param[pindex_CO].iptr,
                                                         *param[pindex_PL].dblptr,
                                                         *param[pindex_NP].dblptr);
    AssertFatal((channeldesc_p != NULL),
                "Could not allocate channel %s type %s \n",
                *param[pindex_NAME].strptr,
                *param[pindex_TYPE].strptr);
    channeldesc_p->model_name = strdup(*param[pindex_NAME].strptr);
    LOG_I(OCM,
          "Model %s type %s allocated from config file, list %s\n",
          *param[pindex_NAME].strptr,
          *param[pindex_TYPE].strptr,
          modellist_name);
  } /* for loop on channel_list */

  return channel_list.numelt;
} /* load_channelist */

int get_noise_power_dBFS(void) {
  return noise_power_dBFS;
}

#ifdef RANDOM_CHANNEL_MAIN
#define sampling_rate 5.0
#define Td 2.0
main(int argc,char **argv) {
  double amps[8] = {.8,.2,.1,.04,.02,.01,.005};
  struct complexd ch[1 + 2 * sampling_rate * Td], phase;
  int i;
  randominit();
  phase.x = 1.0;
  phase.y = 0;
  random_channel(amps, Td, 8, sampling_rate, ch, 1.0, &phase);
  /*
    for (i=0;i<(11+2*sampling_rate*Td);i++){
    printf("%f + sqrt(-1)*%f\n",ch[i].x,ch[i].y);
    }
  */
}

#endif
