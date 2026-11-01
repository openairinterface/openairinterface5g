/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 *\brief Top-level constants and data structures definitions for NR UE
 */
#ifndef __PHY_DEFS_NR_UE__H__
#define __PHY_DEFS_NR_UE__H__

#ifdef __cplusplus
#include <atomic>
#ifndef _Atomic
#define _Atomic(X) std::atomic< X >
#endif
#endif

#include "defs_nr_common.h"
#include "PHY/defs_nr_sl_UE.h"
#include "openair1/PHY/nr_phy_common/inc/nr_ue_phy_meas.h"
#include "common/utils/threadPool/task_ans.h"
#include <malloc.h>
#include "fapi_nr_ue_interface.h"
#include "common/utils/barrier/barrier.h"
#include "common/utils/actor/actor.h"
//#include "openair1/SCHED_NR_UE/defs.h"

#define msg(aRGS...) LOG_D(PHY, ##aRGS)
// use msg in the real-time thread context
#define msg_nrt printf
// use msg_nrt in the non real-time context (for initialization, ...)
#ifndef malloc16
    #define malloc16(x) memalign(32,x)
#endif
#define free16(y,x) free(y)
#define openair_free(y,x) free((y))
#define PAGE_SIZE 4096

#define virt_to_phys(x) (x)
#define openair_sched_exit() exit(-1)

#define bzero(s,n) (memset((s),0,(n)))

// Set the number of barriers for processSlotTX to 512. This value has to be at least 483 for NTN where
// DL-to-UL offset is up to 483. The selected value is also half of the frame range so that
// (slot + frame * slots_per_frame) % NUM_PROCESS_SLOT_TX_BARRIERS is contiguous between the last slot of
// frame 1023 and first slot of frame 0
// e.g. in numerology 1:
//       (19 + 1023 * 20) % 512 = 511
//       (0  + 0 * 20) % 512 = 0
#define NUM_PROCESS_SLOT_TX_BARRIERS 512

// CSI for tracking can have up to 2 resources per slot
#define MAX_CSI_RES_SLOT 2
// Threshold to change radio frequency
#define TRS_CFO_THRESH 500

#include "impl_defs_nr.h"
#include "time_meas.h"
#include "PHY/CODING/coding_defs.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/TOOLS/tools_defs.h"
#include "common/platform_types.h"
#include "NR_UE_TRANSPORT/nr_transport_ue.h"
#include "openair1/PHY/defs_common.h"

#if defined(UPGRADE_RAT_NR)
  #include "PHY/NR_REFSIG/ss_pbch_nr.h"
#endif

#include <pthread.h>
#include "NR_UE_PHY_INTERFACE/NR_IF_Module.h"

/// Context data structure for gNB subframe processing
typedef struct {
  /// Component Carrier index
  uint8_t CC_id;
  /// Last RX timestamp
  openair0_timestamp_t timestamp_rx;
} UE_nr_proc_t;

#define debug_msg if (((mac_xface->frame%100) == 0) || (mac_xface->frame < 50)) msg

#define NEIGHBOR_CELL_MAX_CONSECUTIVE_FAILURES 10

typedef struct {
  int ssb_slot;
  int pss_search_start;
  int pss_search_length;
  uint32_t ssb_rsrp;
  int ssb_rsrp_dBm;
  int consec_fail; // Counter for consecutive detection failures
  bool valid_meas;
} neighboring_cell_info_t;

typedef struct {
  uint8_t decoded_output[3]; // PBCH paylod not larger than 3B
  uint8_t xtra_byte;
} fapiPbch_t;

typedef struct {

  // RRC measurements
  uint32_t rssi;
  int n_adj_cells;
  uint32_t rsrp[7];
  short rsrp_dBm[7];
  int ssb_rsrp_dBm[64];
  float ssb_sinr_dB[64];
  // common measurements
  //! total estimated noise power (linear)
  unsigned int   n0_power_tot;
  //! total estimated noise power (dB)
  short n0_power_tot_dB;
  //! average estimated noise power (linear)
  unsigned int   n0_power_avg;
  //! average estimated noise power (dB)
  short n0_power_avg_dB;
  //! total estimated noise power (dBm)
  short n0_power_tot_dBm;

  // UE measurements
  //! estimated received spatial signal power (linear)
  fourDimArray_t *rx_spatial_power;
  //! estimated received spatial signal power (dB)
  fourDimArray_t *rx_spatial_power_dB;

  /// estimated received signal power (sum over all TX/RX antennas)
  int rx_power_tot[NUMBER_OF_CONNECTED_gNB_MAX]; //NEW
  /// estimated received signal power (sum over all TX/RX antennas)
  short rx_power_tot_dB[NUMBER_OF_CONNECTED_gNB_MAX]; //NEW

  //! estimated received signal power (sum of all TX/RX antennas, time average)
  int rx_power_avg[NUMBER_OF_CONNECTED_gNB_MAX];
  //! estimated received signal power (sum of all TX/RX antennas, time average, in dB)
  short rx_power_avg_dB[NUMBER_OF_CONNECTED_gNB_MAX];

  /// SINR (sum of all TX/RX antennas, in dB)
  int wideband_cqi_tot[NUMBER_OF_CONNECTED_gNB_MAX];
  /// SINR (sum of all TX/RX antennas, time average, in dB)
  int wideband_cqi_avg[NUMBER_OF_CONNECTED_gNB_MAX];

  //! estimated rssi (dBm)
  short rx_rssi_dBm[NUMBER_OF_CONNECTED_gNB_MAX];

  /// Number of RX Antennas
  unsigned char  nb_antennas_rx;

  /// Info about neighboring cells to perform the measurements
  neighboring_cell_info_t neighboring_cell_info[NUMBER_OF_NEIGHBORING_CELLS_MAX];
  _Atomic(bool) meas_request_pending;
  _Atomic(bool) search_new_cells_pending;
  int last_blind_slot;
  int last_slot;
} PHY_NR_MEASUREMENTS;

typedef struct {
  bool active[2];
  fapi_nr_ul_config_pucch_pdu pucch_pdu[2];
} NR_UE_PUCCH;

typedef struct {
  /// \brief Holds the transmit data in time domain.
  /// For IFFT_FPGA this points to the same memory as PHY_vars->tx_vars[a].TX_DMA_BUFFER.
  /// - first index: tx antenna [0..nb_antennas_tx[
  /// - second index: sample [0..FRAME_LENGTH_COMPLEX_SAMPLES[
  c16_t **txData;

  /// \brief Holds the received data in time domain.
  /// Should point to the same memory as PHY_vars->rx_vars[a].RX_DMA_BUFFER.
  /// - first index: rx antenna [0..nb_antennas_rx[
  /// - second index: sample [0..2*FRAME_LENGTH_COMPLEX_SAMPLES+2048[
  c16_t **rxdata;

  /// estimated frequency offset (in radians) for all subcarriers
  int32_t freq_offset;
} NR_UE_COMMON;

#define NR_PRS_IDFT_OVERSAMP_FACTOR 1  // IDFT oversampling factor for NR PRS channel estimates in time domain, ALLOWED value 16x, and 1x is default(ie. IDFT size is frame_params->ofdm_symbol_size)
typedef struct {
  prs_config_t prs_cfg;
  int32_t reserved;
  prs_meas_t **prs_meas;
} NR_PRS_RESOURCE_t;

typedef struct {
  uint8_t NumPRSResources;
  NR_PRS_RESOURCE_t prs_resource[NR_MAX_PRS_RESOURCES_PER_SET];
} NR_UE_PRS;

#define NR_PDCCH_DEFS_NR_UE
#define NR_NBR_CORESET_ACT_BWP      3  // The number of CoreSets per BWP is limited to 3 (including initial CORESET: ControlResourceId 0)
#ifdef NR_PDCCH_DEFS_NR_UE

#define MAX_NR_DCI_DECODED_SLOT     10    // This value is not specified

#endif
typedef struct {
  int nb_search_space;
  fapi_nr_dl_config_dci_dl_pdu_rel15_t pdcch_config[FAPI_NR_MAX_SS];
} NR_UE_PDCCH_CONFIG;

#define NR_PSBCH_DMRS_LENGTH 297 // in mod symbols
#define NR_PSBCH_DMRS_LENGTH_DWORD 20 // ceil(2(QPSK)*NR_PBCH_DMRS_LENGTH/32)
#define PBCH_A 24

typedef struct {
  bool active;
  int num_prach_slots;
  fapi_nr_ul_config_prach_pdu prach_pdu;
} NR_UE_PRACH;

typedef struct {
  bool active;
  fapi_nr_dl_config_csiim_pdu_rel15_t csiim_config_pdu;
} NR_UE_CSI_IM;

typedef struct {
  bool active;
  fapi_nr_dl_config_csirs_pdu_rel15_t csirs_config_pdu;
} NR_UE_CSI_RS;

typedef struct {
  uint8_t csi_rs_generated_signal_bits;
  c16_t **csi_rs_generated_signal;
  bool csi_im_meas_computed;
  uint32_t interference_plus_noise_power;
} nr_csi_info_t;

typedef struct {
  bool active;
  fapi_nr_ul_config_srs_pdu srs_config_pdu;
} NR_UE_SRS;

typedef struct UE_NR_SCAN_INFO_s {
  /// 10 best amplitudes (linear) for each pss signals
  int32_t amp[3][10];
  /// 10 frequency offsets (kHz) corresponding to best amplitudes, with respect do minimum DL frequency in the band
  int32_t freq_offset_Hz[3][10];
} UE_NR_SCAN_INFO_t;

typedef struct {
  unsigned int nb_tx;
  unsigned int nb_rx;
  unsigned int att_tx;
  unsigned int att_rx;
  int max_rxgain;
  char *sdr_addrs;
  char *tx_subdev;
  char *rx_subdev;
  clock_source_t clock_source;
  clock_source_t time_source;
  double tune_offset;
  uint64_t if_frequency;
  int if_freq_offset;
  int used_by_cell;
} nrUE_RU_params_t;

typedef struct {
  int ru_id;
  int band;
  uint64_t rf_frequency;
  int64_t rf_freq_offset;
  int numerology;
  int N_RB_DL;
  int ssb_start;
  int used_by_ue;
} nrUE_cell_params_t;

/// Top-level PHY Data Structure for UE
typedef struct PHY_VARS_NR_UE_s {
  /// \brief Module ID indicator for this instance
  uint8_t Mod_id;
  /// \brief Component carrier ID for this PHY instance
  uint8_t CC_id;
  /// \brief Mapping of CC_id antennas to cards
  openair0_rf_map_t rf_map;
  /// \brief Indicator that UE should perform band scanning
  int UE_scan;
  /// \brief Indicator that UE should perform coarse scanning around carrier
  int UE_scan_carrier;
  /// \brief Indicator that UE should enable estimation and compensation of frequency offset
  int UE_fo_compensation;
  /// IF frequency for RF
  uint64_t if_freq;
  /// UL IF frequency offset for RF
  int if_freq_off;
  /// \brief Indicator that UE is synchronized to a gNB
  int is_synchronized;
  /// \brief Indicator that UE is synchronized to a SyncRef UE on Sidelink
  int is_synchronized_sl;
  /// \brief Target gNB Nid_cell when UE is resynchronizing
  int target_Nid_cell;
  /// \brief Indicator that UE is an SynchRef UE
  int sync_ref;
  /// Data structure for UE process scheduling
  UE_nr_proc_t proc;
  /// Flag to indicate the UE shouldn't do timing correction at all
  int no_timing_correction;
  /// \brief Total gain of the TX chain (16-bit baseband I/Q to antenna)
  uint32_t tx_total_gain_dB;
  /// \brief Total gain of the RX chain (antenna to baseband I/Q) This is a function of rx_gain_mode (and the corresponding gain) and the rx_gain of the card.
  uint32_t rx_total_gain_dB;
  /// \brief Total gains with maximum RF gain stage (ExpressMIMO2/Lime)
  uint32_t rx_gain_max[4];
  /// \brief Total gains with medium RF gain stage (ExpressMIMO2/Lime)
  uint32_t rx_gain_med[4];
  /// \brief Total gains with bypassed RF gain stage (ExpressMIMO2/Lime)
  uint32_t rx_gain_byp[4];
  /// \brief Current transmit power
  int16_t tx_power_dBm[NR_MAX_SLOTS_PER_FRAME];
  /// \brief Total number of REs in current transmission
  int tx_total_RE[NR_MAX_SLOTS_PER_FRAME];
  /// \brief Maximum transmit power
  int8_t tx_power_max_dBm;
  /// \brief Number of gNB seen by UE
  uint8_t n_connected_gNB;
  /// \brief indicator that Handover procedure has been initiated
  uint8_t ho_initiated;
  /// \brief indicator that Handover procedure has been triggered
  uint8_t ho_triggered;
  /// threshold for false dci detection
  int dci_thres;
  /// \brief Measurement variables.
  PHY_NR_MEASUREMENTS measurements;
  NR_DL_FRAME_PARMS  frame_parms;
  /// \brief Frame parame before ho used to recover if ho fails.
  NR_DL_FRAME_PARMS  frame_parms_before_ho;
  NR_UE_COMMON    common_vars;

  nr_ue_if_module_t *if_inst;
  bool received_config_request;
  fapi_nr_config_request_t nrUE_config;
  nr_synch_request_t synch_request;

  NR_UE_PRACH *prach_vars[NUMBER_OF_CONNECTED_gNB_MAX];
  NR_UE_PRS *prs_vars[NR_MAX_PRS_COMB_SIZE];
  uint8_t prs_active_gNBs;
  NR_DL_UE_HARQ_t dl_harq_processes[2][NR_MAX_HARQ_PROCESSES];
  NR_UL_UE_HARQ_t ul_harq_processes[NR_MAX_HARQ_PROCESSES];

  // Scrambling IDs used in PUSCH DMRS
  c16_t X_u[64][839];

  // flag to activate PRB based averaging of channel estimates
  // when off, defaults to frequency domain interpolation
  int chest_freq;
  int chest_time;

  UE_NR_SCAN_INFO_t scan_info[NB_BANDS_MAX];

  /// \brief ?.
  /// - first index: gNB [0..NUMBER_OF_CONNECTED_gNB_MAX[ (hard coded)
  uint32_t total_TBS[NUMBER_OF_CONNECTED_gNB_MAX];
  /// \brief ?.
  /// - first index: gNB [0..NUMBER_OF_CONNECTED_gNB_MAX[ (hard coded)
  uint32_t total_TBS_last[NUMBER_OF_CONNECTED_gNB_MAX];
  /// \brief ?.
  /// - first index: gNB [0..NUMBER_OF_CONNECTED_gNB_MAX[ (hard coded)
  uint32_t bitrate[NUMBER_OF_CONNECTED_gNB_MAX];
  /// \brief ?.
  /// - first index: gNB [0..NUMBER_OF_CONNECTED_gNB_MAX[ (hard coded)
  uint32_t total_received_bits[NUMBER_OF_CONNECTED_gNB_MAX];
  int dlsch_errors[NUMBER_OF_CONNECTED_gNB_MAX];
  int dlsch_errors_last[NUMBER_OF_CONNECTED_gNB_MAX];
  int dlsch_received[NUMBER_OF_CONNECTED_gNB_MAX];
  int dlsch_received_last[NUMBER_OF_CONNECTED_gNB_MAX];
  int dlsch_fer[NUMBER_OF_CONNECTED_gNB_MAX];
  /// temporary offset during cell search prior to MIB decoding
  int ssb_offset;
  uint16_t symbol_offset; /// offset in terms of symbols for detected ssb in sync
  int64_t max_pos_iir; /// Timing offset IIR filter
  int max_pos_acc; /// Timing offset accumuluated error for PI filter

  double initial_fo; /// initial frequency offset provided by the user
  int cont_fo_comp; /// flag enabling the continuous frequency offset estimation and compensation
  double freq_offset; /// currently compensated DL frequency offset (without dl_Doppler_shift)
  double freq_off_acc; /// accumulated DL frequency error (for PI controller)
  double dl_Doppler_shift; /// calculated DL Doppler shift
  double ul_Doppler_shift; /// calculated UL Doppler shift
  int disable_blind_search; /// flag disabling the blind search for UE searches by neighboring cells

  /// Timing Advance updates variables
  /// Timing advance update computed from the TA command signalled from gNB
  int timing_advance; /// corresponds to N_TA
  int timing_advance_ntn; /// corresponds to N_common_TA_adj + N_UE_TA_adj
  position_t position; /// configured UE position in ECEF coordinates, in metres
  int N_TA_offset; ///timing offset used in TDD
  int ta_frame;
  int ta_slot;
  int ta_command;
  bool ta_command_is_rar; /// Pending TA command originated from a Random Access Response

  /// Flag to initialize averaging of PHY measurements
  int init_averaging;

  /// sinr_effective used for CQI calulcation
  double sinr_eff;

  /// N0 (used for abstraction)
  double N0;

  /// NR LDPC coding related
  nrLDPC_coding_interface_t nrLDPC_coding_interface;
  uint8_t max_ldpc_iterations;

  /// CSI variables
  nr_csi_info_t *nr_csi_info;

  // TODO: move this out of phy
  time_stats_t ue_ul_indication_stats;
  nr_ue_phy_cpu_stat_t phy_cpu_stats;

  /// Phase precompensation flag
  bool no_phase_pre_comp;

  /// Enable ML-based LLR computation for 2-layer MIMO (QPSK/16QAM/64QAM).
  /// When false (default), MMSE equalization is used for all configurations.
  bool do_ml;

  void* scopeData;
  // Pointers to hold PDSCH data only for phy simulators
  void *phy_sim_rxdataF;
  void *phy_sim_pdsch_llr;
  void *phy_sim_pdsch_rxdataF_ext;
  void *phy_sim_pdsch_rxdataF_comp;
  void *phy_sim_pdsch_dl_ch_estimates;
  void *phy_sim_pdsch_dl_ch_estimates_ext;
  uint8_t *phy_sim_dlsch_b;
  uint8_t *phy_sim_test_buf;

  dynamic_barrier_t process_slot_tx_barriers[NUM_PROCESS_SLOT_TX_BARRIERS];

  // Gain change required for automation RX gain change
  int adjust_rxgain;

  // Sidelink parameters
  sl_nr_sidelink_mode_t sl_mode;
  sl_nr_ue_phy_params_t SL_UE_PHY_PARAMS;
  pthread_t main_thread;
  pthread_t stat_thread;
  // Per-DL-actor pre-allocated PDSCH scratch buffers (one set per actor to avoid races)
  struct pdsch_scratch_s {
    // 1-symbol-wide: channel compensation writes into these and LLR+demapping consumes
    // them within the same nr_rx_pdsch() call, so no per-symbol slot-wide storage is kept.
    c16_t   *rxdataF_comp;          // [max_layers][pdsch_buf_size_max]
    c16_t   *dl_ch_mag;             // [max_layers][pdsch_buf_size_max]
    c16_t   *dl_ch_magb;            // [max_layers][pdsch_buf_size_max]
    c16_t   *dl_ch_magr;            // [max_layers][pdsch_buf_size_max]
    c16_t   *rho_dl;                // [max_layers*max_layers][pdsch_buf_size_max], only allocated when do_ml is set
    int32_t *pdsch_dl_ch_estimates; // [nb_antennas_rx*NR_MAX_NB_LAYERS][pdsch_est_size]
    int16_t *llr[2];               // [2 codewords][llr_buf_max]
#ifdef LDPC_CUDA
    // gpu mapped version (gpuHostGetDevicePointer), typically the same for Jetson/GH/GB
    int16_t *llr_dev[2];
#endif
    uint32_t pdsch_buf_size_max;
    uint32_t pdsch_est_size;
    uint32_t llr_buf_max;
  } *pdsch_scratch;
  int pdsch_num_actors;
} PHY_VARS_NR_UE;
typedef struct pdsch_scratch_s pdsch_scratch_t;

typedef struct {
  openair0_timestamp_t timestamp_tx;
  int gNB_id;
  /// NR slot index within frame_tx [0 .. slots_per_frame - 1] to act upon for transmission
  int nr_slot_tx;
  int rx_slot_type;
  /// NR slot index within frame_rx [0 .. slots_per_frame - 1] to act upon for transmission
  int nr_slot_rx;
  int tx_slot_type;
  /// frame to act upon for transmission
  int frame_tx;
  /// frame to act upon for reception
  int frame_rx;
  /// hyper frame number to act upon for transmission
  int hfn_tx;
  /// hyper frame number to act upon for reception
  int hfn_rx;
} UE_nr_rxtx_proc_t;

typedef struct {
  bool cell_detected;
  int rx_offset;
} nr_initial_sync_t;

typedef struct {
  nr_gscn_info_t gscnInfo;
  int foFlag;
  int targetNidCell;
  c16_t **rxdata;
  int rxdata_sz;
  NR_DL_FRAME_PARMS *fp;
  UE_nr_rxtx_proc_t *proc;
  int halfFrameBit;
  int symbolOffset;
  int ssbIndex;
  int ssbOffset;
  int nidCell;
  int freqOffset;
  nr_initial_sync_t syncRes;
  fapiPbch_t pbchResult;
  int pssCorrPeakPower;
  int pssCorrAvgPower;
  int adjust_rxgain;
  task_ans_t *ans;
} nr_ue_ssb_scan_t;

typedef struct {
  bool success;
  int pos;
  int nid2;
  int freq_offset; // PSS frequency offset estimate
  int peak; // PSS correlation peak power
  int avg; // PSS correlation average power
} pss_detection_result_t;

typedef struct {
  bool success;
  int nid_cell; // detected PCI
  int freq_offset; // SSS frequency offset estimate
  int phase; // SSS phase
} sss_detection_result_t;

// Common SSB search parameters - used by both initial sync and neighbor cell search
typedef struct {
  uint64_t dl_CarrierFreq;
  uint sampling_rate;
  int slots_per_frame;
  int slots_per_subframe;
  int numerology_index;
  int ofdm_symbol_size;
  uint ofdm_offset_divisor;
  int nb_antennas_rx;
  int symbols_per_slot;
  int N_RB_DL;
  uint32_t rxdata_size;
  c16_t **rxdata;
  int nb_prefix_samples;
  int nb_prefix_samples0;
  int ssb_start_subcarrier;
  int subcarrier_spacing;
  int samples_per_slot_wCP;
  int target_nid_cell; // -1 for blind search, specific PCI for targeted search
  const uint16_t *exclude_nid_cells; // PCIs to exclude (serving cell + already discovered neighboring cells)
  int num_exclude_nid_cells; // Number of PCIs in exclude_nid_cells array
  bool apply_freq_offset; // whether to compensate frequency offset
  bool fo_flag; // frequency offset estimation flag for pss_synchro_nr()
  void *rxdataF; // Pre-allocated rxdataF buffer
  void *pssTime; // Pre-generated PSS time sequences
  // Output parameters
  pss_detection_result_t pss_res;
  sss_detection_result_t sss_res;
} nr_ssb_search_params_t;

typedef struct nr_phy_data_tx_s {
  NR_UE_ULSCH_t ulsch;
  NR_UE_PUCCH pucch_vars;
  NR_UE_SRS srs_vars;

  // Sidelink Rx action decided by MAC
  sl_nr_tx_config_type_enum_t sl_tx_action;
  sl_nr_tx_config_psbch_pdu_t psbch_vars;
} nr_phy_data_tx_t;

typedef struct nr_phy_data_s {
  NR_UE_PDCCH_CONFIG phy_pdcch_config;
  fapi_nr_dl_config_dlsch_pdu_rel15_t dlsch_config;
  NR_UE_DLSCH_t dlsch[2];
  int n_dlsch_codewords;
  // Sidelink Rx action decided by MAC
  sl_nr_rx_config_type_enum_t sl_rx_action;
  int num_csirs;
  NR_UE_CSI_RS csirs_vars[MAX_CSI_RES_SLOT];
  NR_UE_CSI_IM csiim_vars;
} nr_phy_data_t;

enum stream_status_e { STREAM_STATUS_UNSYNC, STREAM_STATUS_SYNCING, STREAM_STATUS_SYNCED};
/* this structure is used to pass both UE phy vars and
 * proc to the function UE_thread_rxn_txnp4
 */
typedef struct nr_rxtx_thread_data_s {
  UE_nr_rxtx_proc_t proc;
  PHY_VARS_NR_UE *UE;
  int writeBlockSize;
  nr_phy_data_t phy_data;
  dynamic_barrier_t* next_barrier;
  uint64_t absolute_deadline_us;
  Actor_t *ul_actors;
} nr_rxtx_thread_data_t;

typedef struct LDPCDecode_ue_s {
  PHY_VARS_NR_UE *phy_vars_ue;
  NR_DL_UE_HARQ_t *harq_process;
  t_nrLDPC_dec_params decoderParms;
  NR_UE_DLSCH_t *dlsch;
  short* dlsch_llr;
  int dlsch_id;
  int rv_index;
  int E;
  int Kc;
  int Qm;
  int Kr_bytes;
  int segment_r;
  int r_offset;
  int offset;
  int Tbslbrm;
  int decodeIterations;
  time_stats_t ts_ldpc_decode;
  task_ans_t *ans;
} ldpcDecode_ue_t;

static inline void start_meas_nr_ue_phy(PHY_VARS_NR_UE *ue, int meas_index) {
  start_meas(&ue->phy_cpu_stats.cpu_time_stats[meas_index]);
}

static inline void stop_meas_nr_ue_phy(PHY_VARS_NR_UE *ue, int meas_index) {
  stop_meas(&ue->phy_cpu_stats.cpu_time_stats[meas_index]);
}

#endif
