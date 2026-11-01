/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level defines and structure definitions for gNB
 */

#ifndef __PHY_DEFS_GNB__H__
#define __PHY_DEFS_GNB__H__

#include "common/platform_constants.h"
#include "defs_nr_common.h"
#include "common/utils/bits.h"
#include "CODING/nrPolar_tools/nr_polar_pbch_defs.h"
#include "openair2/NR_PHY_INTERFACE/NR_IF_Module.h"
#include "PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface.h"
#include "PHY/CODING/nrLDPC_extern.h"
#include "PHY/CODING/nrLDPC_decoder/nrLDPC_types.h"
#include "nfapi_nr_interface_scf.h"
#include "common/utils/threadPool/task_ans.h"
#include "openair1/PHY/defs_RU.h"
#include "common/utils/ds/spsc_q.h"

#define MAX_NUM_RU_PER_gNB 8
#define MAX_PUCCH0_NID 8
#define NR_SRS_IDFT_OVERSAMP_FACTOR 2
#define NR_SRS_DETECTION_THRESHOLD 10

#define NUMBER_OF_NR_PRACH_MAX 8
typedef struct {
  int frame;
  int slot;
  int num_slots; // prach duration in slots
  nfapi_nr_prach_pdu_t pdu;
  int rootSequenceIndex;
  int numrootSequenceIndex;
  int msg1_frequencystart;
  int mu;
  int prach_sequence_length;
  int restricted_set;
  int numerology_index;
  int nb_rx;
  int ant_start;
  c16_t (*Xu)[839];
  time_stats_t *rx_prach;
  c16_t (*prach_buf)[NUMBER_OF_NR_RU_PRACH_OCCASIONS_MAX][NR_PRACH_SEQ_LEN_L];
} prach_item_t;

typedef struct {
  int nb_id;
  int Nid[MAX_PUCCH0_NID];
  int lut[MAX_PUCCH0_NID][160][14];
} NR_gNB_PUCCH0_LUT_t;

typedef struct {
  int dump_frame;
  int round_trials[8];
  uint64_t total_bytes_tx;
  int total_bytes_rx;
  int current_Qm;
  int current_RI;
  int power[MAX_ANT];
  int noise_power[MAX_ANT];
  int DTX;
  int sync_pos;
} NR_gNB_SCH_STATS_t;

typedef struct {
  int pucch0_sr_trials;
  int pucch0_sr_thres;
  int current_pucch0_sr_stat0;
  int current_pucch0_sr_stat1;
  int pucch0_positive_SR;
  int pucch01_trials;
  int pucch0_n00;
  int pucch0_n01;
  int pucch0_thres;
  int current_pucch0_stat0;
  int current_pucch0_stat1;
  int pucch01_DTX;
  int pucch02_trials;
  int pucch02_DTX;
  int pucch1_sr_trials;
  int pucch1_positive_SR;
  int pucch11_trials;
  int pucch2_trials;
  int pucch2_DTX;
} NR_gNB_UCI_STATS_t;

typedef struct {
  int frame;
  uint16_t rnti;
  bool active;
  /// statistics for DLSCH measurement collection
  NR_gNB_SCH_STATS_t dlsch_stats;
  /// statistics for ULSCH measurement collection
  NR_gNB_SCH_STATS_t ulsch_stats;
  NR_gNB_UCI_STATS_t uci_stats;
} NR_gNB_PHY_STATS_t;

typedef struct {
  /// Nfapi DLSCH PDU
  const nfapi_nr_dl_tti_pdsch_pdu *pdsch_pdu;
  /// freq allocation information
  freq_alloc_bitmap_t freq_alloc;
  /// pointer to pdu from MAC interface (this is "a" in 36.212)
  uint8_t *pdu;
  /// Pointer to the payload
  uint8_t *b;
  /// Pointers to transport block segments
  uint8_t **c;
#ifdef LDPC_CUDA
  /// Pointers to transport block segments (contains pointers in c above)
  uint8_t *c_devh;
  /// Pointers to transport block segments (GPU mapping)
  uint8_t *c_dev;
#endif
  /// Interleaver outputs
  uint8_t *f;
  /// REs unavailable for DLSCH (overlapping with PTRS, CSIRS etc.)
  uint32_t unav_res;
} NR_gNB_DLSCH_t;

typedef struct {
  uint8_t NumPRSResources;
  prs_config_t prs_cfg[NR_MAX_PRS_RESOURCES_PER_SET];
} NR_gNB_PRS;

typedef struct {
  /// Nfapi ULSCH PDU
  nfapi_nr_pusch_pdu_t ulsch_pdu; // !!
  /// Index of current HARQ round for this DLSCH
  uint8_t round;
  bool new_rx;
  /////////////////////// ulsch decoding ///////////////////////
  /// flag used to clear d properly
  /// set to true in nr_fill_ulsch() when new_data_indicator is received
  bool harq_to_be_cleared;
  /// Pointer to the payload (38.212 V15.4.0 section 5.1)
  uint8_t *b;
  /// Pointer to aggregated code blocks after code block segmentation and CRC attachment (38.212 V15.4.0 section 5.2.2)
  uint8_t *c;
  /// Number of bits in each code block (38.212 V15.4.0 section 5.2.2)
  uint32_t K;
  /// Number of "Filler" bits added in the code block segmentation (38.212 V15.4.0 section 5.2.2)
  uint32_t F;
  /// Number of code blocks after code block segmentation (38.212 V15.4.0 section 5.2.2)
  uint32_t C;
  /// Pointers to aggregated code blocks after LDPC coding (38.212 V15.4.0 section 5.3.2)
  int16_t *d;
  /// LDPC lifting size (38.212 V15.4.0 table 5.3.2-1)
  uint32_t Z;
  /// Number of bits in each code block after rate matching for LDPC code (38.212 V15.4.0 section 5.4.2.1)
  uint32_t E;
  /// Number of segments processed so far
  uint32_t processedSegments;
  decode_abort_t abort_decode;
  /// Last index of LLR buffer that contains information.
  /// Used for computing LDPC decoder R
  int llrLen;
  //////////////////////////////////////////////////////////////
} NR_UL_gNB_HARQ_t;

typedef struct {
  uint32_t frame;
  uint32_t slot;
  uint32_t unav_res;
  /// Pointers to 16 HARQ processes for the ULSCH
  NR_UL_gNB_HARQ_t *harq_process;
  /// HARQ process mask, indicates which processes are currently active
  int harq_pid;
  /// Allocated RNTI for this ULSCH
  uint16_t rnti;
  /// Maximum number of LDPC iterations
  uint8_t max_ldpc_iterations;
  /// number of iterations used in last LDPC decoding
  int8_t last_iteration_cnt;
  /// Status Flag indicating for this ULSCH
  bool active;
} NR_gNB_ULSCH_t;

typedef struct {
  /// Frame where current PUSCH pdu was sent
  uint32_t frame;
  /// Slot where current PUSCH pdu was sent
  uint32_t slot;
  /// ULSCH PDU
  nfapi_nr_pusch_pdu_t pusch_pdu;
  // Multi-User (MU) group index
  // -1 : unallocated
  int16_t mu_group_idx;
  // 1 for Single-User (SU) and > 1 is MU
  uint8_t mu_group_size;
} NR_gNB_PUSCH_job_t;

typedef struct {
  /// Frame where current PUCCH pdu was sent
  uint32_t frame;
  /// Slot where current PUCCH pdu was sent
  uint32_t slot;
  /// ULSCH PDU
  nfapi_nr_pucch_pdu_t pucch_pdu;
} NR_gNB_PUCCH_job_t;

typedef struct {
  /// Frame where current SRS pdu was received
  uint32_t frame;
  /// Slot where current SRS pdu was received
  uint32_t slot;
  /// ULSCH PDU
  nfapi_nr_srs_pdu_t srs_pdu;
} NR_gNB_SRS_job_t;

typedef struct {
  /// \brief Pointers (dynamic) to the received data in the frequency domain.
  /// - first index: tx antenna [0..16) where 16 is the total supported antenna ports.
  /// - second index: [0..4*ofdm_symbol_size*symbols_per_slot)
  c16_t **rxdataF;
  /// \brief holds the transmit data in the frequency domain.
  /// For IFFT_FPGA this points to the same memory as PHY_vars->rx_vars[a].RX_DMA_BUFFER. //?
  /// - first index: tx antenna [0..16) where 16 is the total supported antenna ports.
  /// - second index: sample [0..ofdm_symbol_size*symbols_per_frame)
  c16_t **txdataF;
  /// \brief Analogue beam ID for each [symbol, antenna] pair
  /// - first index: symbol [slot * fp->symbols_per_slot + sym_idx]
  /// - second index: logical antenna [0..nb_rx/nb_tx]
  uint16_t **beam_id;
  int num_beams_period;
  bool analog_bf;
} NR_gNB_COMMON;

typedef struct {
  /// \brief Hold the channel estimates in frequency domain based on DRS.
  /// - first index: rx antenna id [0..nb_antennas_rx[
  /// - second index: ? [0..12*N_RB_UL*frame_parms->symbols_per_tti[
  int32_t **ul_ch_estimates;
  /// \brief Holds the compensated signal.
  /// - first index: rx antenna id [0..nb_antennas_rx[
  /// - second index: ? [0..12*N_RB_UL*frame_parms->symbols_per_tti[
  c16_t **rxdataF_comp;
  /// \f$\log_2(\max|H_i|^2)\f$
  int16_t log2_maxh;
  /// measured RX power based on DRS
  uint32_t ulsch_power[8];
  /// total signal over antennas
  uint32_t ulsch_power_tot;
  /// measured RX noise power
  uint32_t ulsch_noise_power[8];
  /// total noise over antennas
  uint32_t ulsch_noise_power_tot;
  /// \brief llr values.
  /// - first index: ? [0..1179743] (hard coded)
  int16_t *ulsch_llrs;
#ifdef LDPC_CUDA
  /// \brief llr values link to device memory
  int16_t *llr_dev;
#endif
  int16_t *ack_llrs;
  int16_t *csi1_llrs;
  int16_t *csi2_llrs;
  /// \brief Total RE count after DMRS/PTRS RE's are extracted from respective symbol.
  /// - first index: ? [0...14] smybol per slot
  int16_t *ul_valid_re_per_slot;
  /// \brief offset for llr corresponding to each symbol
  int llr_offset[14];
  /// flag to indicate DTX on reception
  int DTX;
  /// delay estimation
  delay_t delay;
  /// information for UCI on PUSCH
  rate_match_info_uci_t uci_info;
} NR_gNB_PUSCH;

/// Context data structure for RX/TX portion of slot processing
typedef struct {
  /// Component Carrier index
  uint8_t CC_id;
  /// timestamp transmitted to HW
  openair0_timestamp_t timestamp_tx;
  /// slot to act upon for transmission
  int slot_tx;
  /// slot to act upon for reception
  int slot_rx;
  /// frame to act upon for transmission
  int frame_tx;
  /// frame to act upon for reception
  int frame_rx;
  /// \brief Instance count for RXn-TXnp4 processing thread.
  /// \internal This variable is protected by \ref mutex_rxtx.
  int instance_cnt;
  /// condition variable for tx processing thread
  pthread_cond_t cond;
  /// mutex for RXn-TXnp4 processing thread
  pthread_mutex_t mutex;
} gNB_L1_rxtx_proc_t;


/// Context data structure for eNB slot processing
typedef struct gNB_L1_proc_t_s {
  /// Component Carrier index
  uint8_t CC_id;
  /// timestamp received from HW
  openair0_timestamp_t timestamp_rx;
  /// timestamp to send to "slave rru"
  openair0_timestamp_t timestamp_tx;
  /// slot to act upon for reception
  int slot_rx;
  /// frame to act upon for reception
  int frame_rx;
  /// frame to act upon for transmission
  int frame_tx;
  /// pthread structure for dumping L1 stats
  pthread_t L1_stats_thread;
  /// set of scheduling variables RXn-TXnp4 threads
  gNB_L1_rxtx_proc_t L1_proc;
  gNB_L1_rxtx_proc_t L1_proc_tx;
} gNB_L1_proc_t;

typedef struct {
  // common measurements
  //! estimated noise power (linear)
  unsigned int   n0_power[MAX_NUM_RU_PER_gNB];
  //! estimated noise power (dB)
  int n0_power_dB[MAX_NUM_RU_PER_gNB];
  //! total estimated noise power (linear)
  unsigned int   n0_power_tot;
  //! estimated avg noise power (dB)
  int n0_power_tot_dB;
  //! estimated avg noise power per RB per RX ant (lin)
  fourDimArray_t *n0_subband_power;
  //! estimated avg subband noise power (dB)
  int n0_subband_power_avg_dB;
  //! estimated avg subband noise power per antenna (dB)
  int n0_subband_power_avg_perANT_dB[MAX_ANT];
  //! estimated avg noise power per RB (dB)
  int n0_subband_power_tot_dB[275];
  /// PRACH background noise level
  int prach_I0;
} PHY_MEASUREMENTS_gNB;

/// Top-level PHY Data Structure for gNB
typedef struct PHY_VARS_gNB_s {
  /// Module ID indicator for this instance
  module_id_t Mod_id;
  uint8_t CC_id;
  uint8_t configured;
  gNB_L1_proc_t proc;
  int num_RU;
  RU_t *RU_list[MAX_NUM_RU_PER_gNB];
  /// Ethernet parameters for fronthaul interface
  eth_params_t eth_params;
  int rx_total_gain_dB;
  nfapi_nr_config_request_scf_t gNB_config;
  NR_DL_FRAME_PARMS frame_parms;
  PHY_MEASUREMENTS_gNB measurements;
  NR_IF_Module_t *if_inst;

  nfapi_nr_ul_tti_request_t UL_tti_req;

  int max_nb_pdsch;
  int max_nb_pusch;

  NR_gNB_COMMON common_vars;
  spsc_q_t prach_ru_queue;
  spsc_q_t prach_l1rx_queue;
  // TODO: can we remove c from NR_gNB_DLSCH_t and put it on the stack?
  NR_gNB_DLSCH_t *dlsch;
  NR_gNB_PRS prs_vars;
  NR_gNB_PUSCH *pusch_vars;
  spsc_q_t pucch_queue;
  spsc_q_t pusch_queue;
  spsc_q_t srs_queue;
  NR_gNB_ULSCH_t *ulsch;
  NR_gNB_PHY_STATS_t phy_stats[MAX_MOBILES_PER_GNB];
  t_nrPolar_params **polarParams;

  // reference amplitude for TX
  int16_t TX_AMP;

  // flag to activate 3GPP phase symbolwise rotation
  bool phase_comp;

  // PUCCH0 Look-up table for cyclic-shifts
  NR_gNB_PUCCH0_LUT_t pucch0_lut;

  /// PBCH interleaver
  uint8_t nr_pbch_interleaver[NR_POLAR_PBCH_PAYLOAD_BITS];

  /// PRACH root sequence
  c16_t X_u[64][839];

  /// OFDM symbol offset divisor for UL
  uint32_t ofdm_offset_divisor;

  /// NR LDPC coding related
  nrLDPC_coding_interface_t nrLDPC_coding_interface;
  int max_ldpc_iterations;

  /// indicate the channel estimation technique in time domain
  int chest_time;
  /// indicate the channel estimation technique in freq domain
  int chest_freq;

  /// counter to average prach energh over first 100 prach opportunities
  int prach_energy_counter;

  int pucch0_thres;
  int pusch_thres;
  int prach_thres;
  int srs_thres;
  uint64_t bad_pucch;
  int num_ulprbbl;
  uint16_t ulprbbl [MAX_BWP_SIZE];

  bool enable_analog_das;

  time_stats_t l1_tx_proc;
  time_stats_t l1_rx_proc;

  time_stats_t phy_proc_tx;
  time_stats_t gnb_tx_procedures_stats;
  time_stats_t ru_tx_func_stats;
  time_stats_t phy_proc_rx;
  time_stats_t rx_prach;

  time_stats_t dlsch_encoding_stats;
  time_stats_t dlsch_ldpc_encode_stats;
  time_stats_t dlsch_modulation_stats;
  time_stats_t dlsch_scrambling_stats;
  time_stats_t dlsch_pdsch_generation_stats;
  time_stats_t dlsch_layer_mapping_stats;
  time_stats_t dlsch_resource_mapping_stats;
  time_stats_t dlsch_precoding_stats;
  time_stats_t dlsch_crc_stats;

  time_stats_t dci_generation_stats;
  time_stats_t rx_pusch_stats;
  time_stats_t rx_pusch_init_stats;
  time_stats_t rx_pusch_symbol_processing_stats;
  time_stats_t ul_indication_stats;
  time_stats_t slot_indication_stats;
  time_stats_t ulsch_decoding_stats;
  time_stats_t ts_ldpc_decode;
  time_stats_t ulsch_deinterleaving_stats;
  time_stats_t ulsch_channel_estimation_stats;
  time_stats_t pusch_channel_estimation_antenna_processing_stats;
  time_stats_t ulsch_llr_stats;
  time_stats_t ulsch_layer_demapping_stats;
  time_stats_t ulsch_unscrambling_stats;
  time_stats_t pusch_extraction_stats;
  time_stats_t pusch_channel_compensation_stats;
  time_stats_t rx_srs_stats;
  time_stats_t generate_srs_stats;
  time_stats_t get_srs_signal_stats;
  time_stats_t srs_channel_estimation_stats;
  time_stats_t srs_timing_advance_stats;
  time_stats_t srs_report_tlv_stats;
  time_stats_t srs_beam_report_stats;
  time_stats_t srs_iq_matrix_stats;
  time_stats_t pucch01_proc_rx;
  time_stats_t pucch23_proc_rx;

  notifiedFIFO_t resp_L1;
  notifiedFIFO_t L1_tx_out;
  notifiedFIFO_t L1_rx_out;
  tpool_t threadPool;
  int num_pusch_symbols_per_thread;
  int num_pdsch_symbols_per_thread;
  int dmrs_num_antennas_per_thread;
  pthread_t L1_rx_thread;
  int L1_rx_thread_core;
  pthread_t L1_tx_thread;
  int L1_tx_thread_core;
  void *scopeData;
} PHY_VARS_gNB;

struct puschSymbolReqId {
  uint16_t ulsch_id;
  uint16_t frame;
  uint8_t  slot;
  uint16_t spare;
} __attribute__((packed));

union puschSymbolReqUnion {
  struct puschSymbolReqId s;
  uint64_t p;
};

struct puschAntennaReqId {
  uint16_t ul_id;
  uint16_t spare;
} __attribute__((packed));

union puschAntennaReqUnion {
  struct puschAntennaReqId s;
  uint64_t p;
};

typedef struct LDPCDecode_s {
  PHY_VARS_gNB *gNB;
  NR_UL_gNB_HARQ_t *ulsch_harq;
  t_nrLDPC_dec_params decoderParms;
  NR_gNB_ULSCH_t *ulsch;
  int16_t *ulsch_llr;
  int ulsch_id;
  int harq_pid;
  int rv_index;
  int A;
  int E;
  int Kc;
  int Qm;
  int Kr_bytes;
  int nbSegments;
  int segment_r;
  int r_offset;
  int offset;
  int decodeIterations;
  uint32_t tbslbrm;
  task_ans_t *ans;
} ldpcDecode_t;

struct ldpcReqId {
  uint16_t rnti;
  uint16_t frame;
  uint8_t  slot;
  uint8_t  codeblock;
  uint16_t spare;
} __attribute__((packed));

union ldpcReqUnion {
  struct ldpcReqId s;
  uint64_t p;
};

typedef struct processingData_L1 {
  int frame_rx;
  int slot_rx;
  openair0_timestamp_t timestamp_tx;
  PHY_VARS_gNB *gNB;
  notifiedFIFO_elt_t *elt;
} processingData_L1_t;

typedef struct processingData_L1tx {
  int frame;
  int slot;
  int frame_rx;
  int slot_rx;
  openair0_timestamp_t timestamp_tx;
  PHY_VARS_gNB *gNB;
} processingData_L1tx_t;

typedef struct processingData_L1rx {
  int frame_rx;
  int slot_rx;
  PHY_VARS_gNB *gNB;
} processingData_L1rx_t;
#endif
