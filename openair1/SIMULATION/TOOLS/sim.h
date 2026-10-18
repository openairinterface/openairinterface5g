/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef __SIMULATION_TOOLS_DEFS_H__
#define __SIMULATION_TOOLS_DEFS_H__
#include "PHY/defs_common.h"
#include "common_lib.h"
#include <pthread.h>
/** @defgroup _numerical_ Useful Numerical Functions
 *@{
The present clause specifies several numerical functions for testing of digital communication systems.

-# Generation of Uniform Random Bits
-# Generation of Quantized Gaussian Random Variables
-# Generation of Floating-point Gaussian Random Variables
-# Generic Multipath Channel Generator

 * @defgroup _channel_ Multipath channel generator
 * @ingroup _numerical_
 * @{

*/

#define NB_SAMPLES_CHANNEL_OFFSET 4

typedef enum {
  UNSPECIFIED_MODID=0,
  RFSIMU_MODULEID=1
} channelmod_moduleid_t;
#define MODULEID_STR_INIT {"","rfsimulator"}

#define SHR3 (jz = jsr, jsr ^= (jsr << 13), jsr ^= (jsr >> 17), jsr ^= (jsr << 5), jz + jsr)

typedef enum {
  CORR_LEVEL_LOW,
  CORR_LEVEL_MEDIUM,
  CORR_LEVEL_HIGH
} corr_level_t;

typedef struct {
  ///Number of tx antennas
  uint nb_tx;
  ///Number of rx antennas
  uint nb_rx;
  ///number of taps
  uint nb_taps;
  ///linear amplitudes of taps
  double *amps;
  ///normalization channel factor
  double normalization_ch_factor;
  /// Delays of the taps in mus. length(delays)=nb_taps, ordered by increasing delays
  double *delays;
  /// length of impulse response in samples: 1 + ceil(sampling_rate * last tap delay) + CHANNEL_SINC_TAIL
  uint channel_length;
  ///channel state vector. size(state) = nb_taps * (n_tx * n_rx);
  fourDimArray_t *a;
  ///interpolated (sample-spaced) channel impulse response. size(ch) = (n_tx * n_rx) * channel_length. ATTENTION: the dimensions of ch are the transposed ones of a. This is to allow the use of BLAS when applying the correlation matrices to the state.
  fourDimArray_t *ch;
  ///Same as above but single precision
  struct complexf **ch_ps;
  ///Sampled frequency response (90 kHz resolution)
  int channelF_len;
  fourDimArray_t *chF;
  ///Carrier center frequency
  uint64_t center_freq;
  ///Channel bandwidth in MHz.
  double channel_bandwidth;
  ///System sampling rate in Msps.
  double sampling_rate;
  ///Ricean factor, sqrt(1/(K+1)), of first tap wrt other taps (0..1, where 0 means AWGN and 1 means Rayleigh channel).
  double ricean_factor;
  ///Correlation level of correlation channel matrix
  corr_level_t corr_level;
  ///Angle of arrival of wavefront (in radians). For Ricean channel only. This assumes that both RX and TX have linear antenna arrays with lambda/2 antenna spacing. Furhter it is assumed that the arrays are parallel to each other and that they are far enough apart so that we can safely assume plane wave propagation.
  double aoa;
  ///If set to 1, aoa is randomized according to a uniform random distribution
  int random_aoa;
  ///in Hz. if >0 generate a channel with a Clarke's Doppler profile with a maximum Doppler bandwidth max_Doppler. CURRENTLY NOT IMPLEMENTED!
  double max_Doppler;
  /// Square root of the spatial correlation matrix per tap: struct complexd [nb_taps][n_links][n_links],
  /// n_links = nb_tx * nb_rx, link index tx * nb_rx + rx
  fourDimArray_t *R_sqrt;
  ///path loss including shadow fading in dB (positive = attenuation)
  double path_loss_dB;
  ///additional delay of channel in samples.
  uint64_t channel_offset;
  float noise_power_dB;
  ///This parameter (0...1) allows for simple 1st order temporal variation. 0 means a new channel every call, 1 means keep channel constant all the time
  double forgetting_factor;
  ///needs to be set to 1 for the first call, 0 otherwise.
  uint8_t first_run;
  /// initial phase for frequency offset simulation
  double ip;
  /// number of paths taken by transmit signal
  uint nb_paths;
  /// timing measurements
  time_stats_t random_channel;
  time_stats_t interp_time;
  time_stats_t interp_freq;
  time_stats_t convolution;
  /// index in the channel descriptors array
  unsigned int chan_idx;
  /// id of the channel modeling algorithm
  int modelid;
  /// identifies channel descriptor owner (the module which created this descriptor
  channelmod_moduleid_t module_id;
  /// name of this descriptor,used for model created from config file at init time
  char *model_name;  
  /// flags to properly trigger memory free
  unsigned int free_flags;
  /// time stamp when the time varying channel emulation starts (when client connected)
  uint64_t start_TS;
  /// height of LEO satellite
  float sat_height;
  /// flag to enable dynamic delay simulation for LEO satellite
  bool enable_dynamic_delay;
  /// flag to enable dynamic Doppler simulation for LEO satellite
  bool enable_dynamic_Doppler;
  /// Doppler phase increment (might vary over time, e.g. for LEO satellite)
  float Doppler_phase_inc;
  /// current Doppler phase of each RX antenna (for continuous phase from one block to the next)
  float *Doppler_phase_cur;
  /// flag indicating if channel direction is UL or DL
  bool is_uplink;
} channel_desc_t;

typedef struct {
  /// Number of sectors (set to 1 in case of an omnidirectional antenna)
  uint8_t n_sectors;
  /// Antenna orientation for each sector (for non-omnidirectional antennas) in radians wrt north
  double alpha_rad[3];
  /// Antenna 3dB beam width (in radians)
  double phi_rad;
  /// Antenna gain (dBi)
  double ant_gain_dBi;
  /// Tx power (dBm)
  double tx_power_dBm;
  /// Rx noise level (dB)
  double rx_noise_level;
  ///x coordinate (cartesian, in m)
  double x;
  ///y coordinate (cartesian, in m)
  double y;
  ///z coordinate (antenna height, in m)
  double z;
  /// direction of travel in radians wrt north
  double direction_rad;
  /// speed of node (m/s)
  double speed;
} node_desc_t;

typedef enum {
  rural=0,
  urban,
  indoor
} scenario_t;

typedef struct {
  /// Scenario classifcation
  scenario_t scenario;
  /// Carrier frequency in Hz
  double carrier_frequency;
  /// Bandwidth (in Hz)
  double bandwidth;
  /// path loss at 0m distance in dB
  double path_loss_0;
  /// path loss exponent
  double path_loss_exponent;
  /// shadow fading standard deviation [dB] (assuming log-normal shadow fading with 0 mean)
  double shadow_fading_std;
  /// correlation distance of shadow fading
  double shadow_fading_correlation_distance;
  /// Shadowing correlation between cells
  double shadow_fading_correlation_cells;
  /// Shadowing correlation between sectors
  double shadow_fading_correlation_sectors;
  /// Rice factor???
  /// Walls (penetration loss)
  /// Nodes in the scenario
  node_desc_t *nodes;
} scenario_desc_t;

typedef enum {
  custom=0,
  SCM_A,
  SCM_B,
  SCM_C,
  SCM_D,
  EPA,
  EVA,
  ETU,
  MBSFN,
  TDL_A,
  TDL_B,
  TDL_C,
  TDL_D,
  TDL_E,
  Rayleigh8,
  Rayleigh1,
  Rayleigh1_800,
  Rayleigh1_corr,
  Rayleigh1_anticorr,
  Rice8,
  Rice1,
  Rice1_corr,
  Rice1_anticorr,
  AWGN,
  Rayleigh1_orthogonal,
  Rayleigh1_orth_eff_ch_TM4_prec_real,
  Rayleigh1_orth_eff_ch_TM4_prec_imag,
  Rayleigh8_orth_eff_ch_TM4_prec_real,
  Rayleigh8_orth_eff_ch_TM4_prec_imag,
  TS_SHIFT,
  EPA_low,
  EPA_medium,
  EPA_high,
  SAT_LEO_TRANS,
  SAT_LEO_REGEN,
} SCM_t;
#define CHANNELMOD_MAP_INIT \
  {"custom",custom},\
  {"SCM_A",SCM_A},\
  {"SCM_B",SCM_B},\
  {"SCM_C",SCM_C},\
  {"SCM_D",SCM_D},\
  {"EPA",EPA},\
  {"EVA",EVA},\
  {"ETU",ETU},\
  {"MBSFN",MBSFN},\
  {"TDL_A",TDL_A},\
  {"TDL_B",TDL_B},\
  {"TDL_C",TDL_C},\
  {"TDL_D",TDL_D},\
  {"TDL_E",TDL_E},\
  {"Rayleigh8",Rayleigh8},\
  {"Rayleigh1",Rayleigh1},\
  {"Rayleigh1_800",Rayleigh1_800},\
  {"Rayleigh1_corr",Rayleigh1_corr},\
  {"Rayleigh1_anticorr",Rayleigh1_anticorr},\
  {"Rice8",Rice8},\
  {"Rice1",Rice1},\
  {"Rice1_corr",Rice1_corr},\
  {"Rice1_anticorr",Rice1_anticorr},\
  {"AWGN",AWGN},\
  {"Rayleigh1_orthogonal",Rayleigh1_orthogonal},\
  {"Rayleigh1_orth_eff_ch_TM4_prec_real",Rayleigh1_orth_eff_ch_TM4_prec_real},\
  {"Rayleigh1_orth_eff_ch_TM4_prec_imag",Rayleigh1_orth_eff_ch_TM4_prec_imag},\
  {"Rayleigh8_orth_eff_ch_TM4_prec_real",Rayleigh8_orth_eff_ch_TM4_prec_real},\
  {"Rayleigh8_orth_eff_ch_TM4_prec_imag",Rayleigh8_orth_eff_ch_TM4_prec_imag},\
  {"TS_SHIFT",TS_SHIFT},\
  {"EPA_low",EPA_low},\
  {"EPA_medium",EPA_medium},\
  {"EPA_high",EPA_high},\
  {"SAT_LEO_TRANS",SAT_LEO_TRANS},\
  {"SAT_LEO_REGEN",SAT_LEO_REGEN},\
  {NULL, -1}

#define CONFIG_HLP_SNR     "Set average SNR in dB (for --siml1 option)\n"
#define CHANNELMOD_SECTION "channelmod"

/* global channel modelization parameters */
#define CHANNELMOD_MODELLIST_PARANAME "modellist"

#define CHANNELMOD_HELP_MODELLIST "<list name> channel list name in config file describing the model type and its parameters\n"
#define CHANNELMOD_HELP_NOISE_POWER \
  "Noise power in dBFS. If set, noise per channel is not applied. To achieve positive SNR use values below -36dBFS\n"

#define INVALID_DBFS_VALUE 100
// clang-format off
#define CHANNELMOD_PARAMS_DESC {  \
  {"max_chan",                    "Max number of runtime models",     0,                                  .uptr=&max_chan,              .defintval=10,                    TYPE_UINT,   0}, \
  {CHANNELMOD_MODELLIST_PARANAME, CHANNELMOD_HELP_MODELLIST,          0,                                  .strptr=&modellist_name,      .defstrval="DefaultChannelList",  TYPE_STRING, 0}, \
  {"noise_power_dBFS",            CHANNELMOD_HELP_NOISE_POWER,        0,                                  .iptr=&noise_power_dBFS,      .defintval=INVALID_DBFS_VALUE,    TYPE_INT,    0 },\
}
// clang-format on

/* parameters for one model */ 
#define CHANNELMOD_MODEL_NAME_PNAME "model_name"
#define CHANNELMOD_MODEL_TYPE_PNAME "type"
#define CHANNELMOD_MODEL_PL_PNAME "ploss_dB"
#define CHANNELMOD_MODEL_NP_PNAME "noise_power_dB"
#define CHANNELMOD_MODEL_FF_PNAME "forgetfact"
#define CHANNELMOD_MODEL_CO_PNAME "offset"
#define CHANNELMOD_MODEL_DT_PNAME "ds_tdl"

// clang-format off
#define CHANNELMOD_MODEL_PARAMS_DESC {  \
    {CHANNELMOD_MODEL_NAME_PNAME, "name of the model\n",               0,  .strptr=NULL ,            .defstrval="",                    TYPE_STRING,    0 }, \
    {CHANNELMOD_MODEL_TYPE_PNAME, "name of the model type\n",          0,  .strptr=NULL ,            .defstrval="AWGN",                TYPE_STRING,    0 }, \
    {CHANNELMOD_MODEL_PL_PNAME,   "channel path loss in dB (positive = attenuation)\n", 0,  .dblptr=NULL,             .defdblval=0,                     TYPE_DOUBLE,    0 }, \
    {CHANNELMOD_MODEL_NP_PNAME,   "channel noise in dB\n",             0,  .dblptr=NULL,             .defdblval=-50,                   TYPE_DOUBLE,    0 }, \
    {CHANNELMOD_MODEL_FF_PNAME,   "channel forget factor ((0 to 1)\n", 0,  .dblptr=NULL,             .defdblval=0,                     TYPE_DOUBLE,    0 }, \
    {CHANNELMOD_MODEL_CO_PNAME,   "channel offset in samps\n",         0,  .iptr=NULL,               .defintval=0,                     TYPE_INT,       0 }, \
    {CHANNELMOD_MODEL_DT_PNAME,   "delay spread for TDL models\n",     0,  .dblptr=NULL,             .defdblval=0,                     TYPE_DOUBLE,    0 }, \
}
// clang-format on

#include "common/platform_constants.h"

typedef struct {
  channel_desc_t *RU2UE[NUMBER_OF_RU_MAX][NUMBER_OF_UE_MAX][MAX_NUM_CCs];
  channel_desc_t *UE2RU[NUMBER_OF_UE_MAX][NUMBER_OF_RU_MAX][MAX_NUM_CCs];
  double r_re_DL[NUMBER_OF_UE_MAX][2][30720];
  double r_im_DL[NUMBER_OF_UE_MAX][2][30720];
  double r_re_UL[NUMBER_OF_eNB_MAX][2][30720];
  double r_im_UL[NUMBER_OF_eNB_MAX][2][30720];
  int RU_output_mask[NUMBER_OF_UE_MAX];
  pthread_mutex_t RU_output_mutex[NUMBER_OF_UE_MAX];
  pthread_mutex_t UE_output_mutex[NUMBER_OF_RU_MAX];
  openair0_timestamp_t current_ru_rx_timestamp[NUMBER_OF_RU_MAX][MAX_NUM_CCs];
  openair0_timestamp_t current_UE_rx_timestamp[MAX_MOBILES_PER_ENB][MAX_NUM_CCs];
  double ru_amp[NUMBER_OF_RU_MAX];
} sim_t;

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
                                     float noise_power_dB);

channel_desc_t *find_channel_desc_fromname(const char *modelname);


/**
\brief free memory allocated for a model descriptor
\param ch points to the model, which cannot be used after calling this fuction
*/
void free_channel_desc_scm(channel_desc_t *ch);

/**
\brief This set the ownerid of a model descriptor, can be later used to check what module created a channel model
\param cdesc points to the model descriptor
\param module_id identifies the channel model. should be define as a macro in simu.h
*/
void set_channeldesc_owner(channel_desc_t *cdesc, channelmod_moduleid_t module_id);

/**
\brief This function set a model name to a model descriptor, can be later used to identify a allocated channel model
\param cdesc points to the model descriptor
\param modelname is the C string to use as model name for the channel pointed by cdesc
*/
void set_channeldesc_name(channel_desc_t *cdesc,char *modelname);

/**
\brief This function set a channel model direction to either uplink or downlink
\param cdesc points to the model descriptor
\param is_uplink indicates if this channel is applied in uplink (not downlink) direction
*/
void set_channeldesc_direction(channel_desc_t *cdesc, bool is_uplink);

/** \fn void get_cexp_doppler(struct complexd *cexp_doppler, channel_desc_t *chan_desc, const uint32_t length)
\brief This routine generates the complex exponential to apply the Doppler shift
\param cexp_doppler Output with the complex exponential of Doppler shift
\param chan_desc Pointer to the channel descriptor
\param length Size of complex exponential of Doppler shift
*/
void get_cexp_doppler(struct complexd *cexp_doppler, channel_desc_t *chan_desc, const uint32_t length);

/** \fn void random_channel(channel_desc_t *desc)
\brief This routine generates a random channel response (time domain) according to a tapped delay line model.
\param desc Pointer to the channel descriptor
*/
int random_channel(channel_desc_t *desc, uint8_t abstraction_flag);

/**\fn void multipath_channel(channel_desc_t *desc,
           double tx_sig_re[NB_ANTENNAS_TX],
           double tx_sig_im[NB_ANTENANS_TX],
           double rx_sig_re[NB_ANTENNAS_RX],
           double rx_sig_im[NB_ANTENNAS_RX],
           uint32_t length,
           uint8_t keep_channel,
	   int log_channel)

\brief This function generates and applys a random frequency selective random channel model.
@param desc Pointer to channel descriptor
@param tx_sig_re input signal (real component)
@param tx_sig_im input signal (imaginary component)
@param rx_sig_re output signal (real component)
@param rx_sig_im output signal (imaginary component)
@param length Length of input signal
@param keep_channel Set to 1 to keep channel constant for null-B/F
@param log_channel=1 make channel coefficients come out for first sample of input
*/

void multipath_channel(channel_desc_t *desc,
                       double **tx_sig_re,
                       double **tx_sig_im,
                       double **rx_sig_re,
                       double **rx_sig_im,
                       uint32_t length,
                       uint8_t keep_channel,
		       int log_channel);

void multipath_channel_float(channel_desc_t *desc,
                             float **tx_sig_interleaved,
                             float **rx_sig_re,
                             float **rx_sig_im,
                             uint32_t length,
                             uint8_t keep_channel,
                             int log_channel);

void interleave_channel_output(float **rx_sig_re,
                               float **rx_sig_im,
                               float **output_interleaved,
                               int nb_rx,
                               int num_samples);

void add_noise_float(c16_t **rxdata,
                     const float **r_re,
                     const float **r_im,
                     const float sigma2,
                     const int length,
                     const int slot_offset,
                     const double ts,
                     const int delay,
                     bool apply_phase_noise,
                     const uint8_t nb_antennas_rx);

void add_rx_signals(float **y_re, float **y_im, float **x_re, float **x_im, int n_rx, int length);

/*
\fn double compute_pbch_sinr(channel_desc_t *desc,
                             channel_desc_t *desc_i1,
           channel_desc_t *desc_i2,
           double snr_dB,double snr_i1_dB,
           double snr_i2_dB,
           uint16_t nb_rb)

\brief This function computes the average SINR over all frequency resources of the PBCH.  It is used for PHY abstraction of the PBCH BLER
@param desc Pointer to channel descriptor of eNB
@param desc Pointer to channel descriptor of interfering eNB 1
@param desc Pointer to channel descriptor of interfering eNB 2
@param snr_dB SNR of eNB
@param snr_i1_dB SNR of interfering eNB 1
@param snr_i2_dB SNR of interfering eNB 2
@param nb_rb Number of RBs in system
*/
double compute_pbch_sinr(channel_desc_t *desc,
                         channel_desc_t *desc_i1,
                         channel_desc_t *desc_i2,
                         double snr_dB,double snr_i1_dB,
                         double snr_i2_dB,
                         uint16_t nb_rb);

double compute_sinr(channel_desc_t *desc,
                    channel_desc_t *desc_i1,
                    channel_desc_t *desc_i2,
                    double snr_dB,double snr_i1_dB,
                    double snr_i2_dB,
                    uint16_t nb_rb);

double pbch_bler(double sinr);

void load_pbch_desc(FILE *pbch_file_fd);

/**@}*/

/**
 * @defgroup _taus_ Tausworthe Uniform Random Variable Generator
 * @ingroup _numerical_
 * @{
\fn unsigned int taus()
\brief Tausworthe Uniform Random Generator.  This is based on the hardware implementation described in
  Lee et al, "A Hardware Gaussian Noise Generator Usign the Box-Muller Method and its Error Analysis," IEEE Trans. on Computers, 2006.
*/
unsigned int taus(void);

/**
\fn set_taus_seed
\brief Sets the seed for the Tausworthe generator.
@param seed_init 0 means generate based on CPU time, otherwise provide the seed
*/
void set_taus_seed(unsigned int seed_init);
/**@} */

/** @defgroup _gauss_ Generation of Quantized Gaussian Random Variables
 * @ingroup _numerical_
 * @{
This set of routines are used to generate quantized (i.e. fixed-point) Gaussian random noise efficiently.
The use of these routines allows for rapid computer simulation of digital communication systems. The method
is based on a lookup-table of the quantized normal probability distribution.  The routines assume that the
continuous-valued Gaussian random-variable,\f$x\f$ is quantized
to \f$N\f$ bits over the interval \f$[-L\sigma,L\sigma)\f$ where \f$N\f$ and \f$L\f$ control the precision
and range of the quantization.  The
random variable, \f$l\in\{-2^{N-1},-2^{N-1}+1,\cdots,0,1,\cdots,2^{N-1}-1\}\f$ corresponds to the event,
\f$E_l =
\begin{cases}
x\in\left[-\infty,-L\sigma\right) & l=-2^{N-1}, \\
x\in\left[\frac{lL\sigma}{2^{N-1}},\frac{(l+1)L\sigma}{2^{N-1}}\right) & <l>-2^{N-1}, \\
x\in\left[L\sigma,\infty\right) & l>-2^{N-1},
\end{cases}\f$
which occurs with probability
\f$\Pr(E_l) =
\begin{cases}
\mathrm{erfc}(L) & l=-2^{N-1}, \\
\mathrm{erfc}(L) & l>-2^{N-1}, \\
\mathrm{erf}\left(\frac{lL}{2^{N-1}}\right) \mathrm{erfc}\left(\frac{(l-1)L}{2^{N-1}}\right)& l>-2^{N-1}.
\end{cases}\f$
*/


/** \fn unsigned int *generate_gauss_LUT(unsigned char Nbits,unsigned char L)
\brief This routine generates a Gaussian pdf lookup table (LUT).  The table has \f$2^{\mathrm{Nbits}-1}\f$ entries which represent
the right half of the pdf.  The data stored in position \f$i\f$ is actually the scaled cumulative probability distribution,
\f$2^{31}\mathrm{erf}\left(\frac{iL}{2^{N-1}}\right)\f$.  This represents the average number of times that the random variable
falls in the interval \f$\left[0,\frac{i}{2^{N-1}}\right)\f$.  This format allows for rapid conversion of uniform 32-bit
random variables to \f$N\f$-bit Gaussian random variables using binary search.
@see gauss
@param Nbits Number of bits for the output variable
@param L Number of standard deviations in range
*/
unsigned int *generate_gauss_LUT(unsigned char Nbits,unsigned char L);

/** \fn int gauss(unsigned int *gauss_LUT,unsigned char Nbits);
\brief This routine returns a zero-mean unit-variance Gaussian random variable.
 Given a 32-bit uniform random variable,
\f$\mathrm{u}\f$ (from \ref _taus_, we first extract the sign and then search in the monotonically increasing Gaussian LUT for
the two entries \f$(i,i+1)\f$ for which
\f$ 2^{31}\mathrm{erf}\left(\frac{i}{2^{Nbits-1}}\right) < |u| \leq 2^{31}\mathrm{erf}\left(\frac{i+1}{2^{Nbits-1}}\right) \f$ and assign
the value \f$\mathrm{sgn}(u)i\f$.  The search requires at most \f$Nbits-1\f$ comparisons.
@see generate_gauss_LUT
@see taus
@param gauss_LUT pointer to lookup-table
@param Nbits number of bits for output variable ( between 1 and 16)
*/
int gauss(unsigned int *gauss_LUT,unsigned char Nbits);

void fill_random(void *buf, size_t sz);
double gaussdouble(double,double);
void randominit(); // fix seed number by env variable OAI_RNGSEED
double uniformrandom(void);
double gaussZiggurat(double mean, double variance);
int freq_channel(channel_desc_t *desc,uint16_t nb_rb, int16_t n_samples,int scs);
int init_freq_channel(channel_desc_t *desc,uint16_t nb_rb,int16_t n_samples,int scs);
void term_freq_channel(void);
uint8_t multipath_channel_nosigconv(channel_desc_t *desc);
void multipath_tv_channel(channel_desc_t *desc,
                          double **tx_sig_re,
                          double **tx_sig_im,
                          double **rx_sig_re,
                          double **rx_sig_im,
                          uint32_t length,
                          uint8_t keep_channel);

/**@} */
/**@} */

int modelid_fromstrtype(char *modeltype);
double channelmod_get_snr_dB(void);
double channelmod_get_sinr_dB(void);
void init_channelmod(void) ;
int load_channellist(uint8_t nb_tx, uint8_t nb_rx, double sampling_rate, uint64_t center_freq, double channel_bandwidth) ;
double N_RB2sampling_rate(uint16_t N_RB);
double N_RB2channel_bandwidth(uint16_t N_RB);

/* Linear phase noise model */
/**
  \brief This function produce phase noise and add to input signal
  \param ts Sampling time
  \param *Re *Im Real and Imag part of the signal
*/
void phase_noise(double ts, int16_t *InRe, int16_t *InIm);

/**
\brief Add AWGN noise and phase noise if enabled
\param rxdata output data with noise
\param r_re real part of input data without noise
\param r_im imaginary part of input data without noise
\param sigma noise power
\param length number of samples to apply the noise
\param slot_offset slot offset to start applying the noise
\param ts sampling time
\param delay introduce delay in terms of number of samples
\param pdu_bit_map bitmap indicating presence of optional PDUs
\param ptrs_bit_map
\param nb_antennas_rx number of receive antennas
*/
void add_noise(c16_t **rxdata,
               const double **r_re,
               const double **r_im,
               const double sigma,
               const int length,
               const int slot_offset,
               const double ts,
               const int delay,
               const uint16_t pdu_bit_map,
               const uint16_t ptrs_bit_map,
               const uint8_t nb_antennas_rx);

void do_DL_sig(sim_t *sim,
               uint16_t subframe,
               uint32_t offset,
               uint32_t length,
               uint8_t abstraction_flag,
               LTE_DL_FRAME_PARMS *ue_frame_parms,
               uint8_t UE_id,
               int CC_id);

int get_noise_power_dBFS(void);

#endif
