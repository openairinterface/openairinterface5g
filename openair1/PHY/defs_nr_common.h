/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level defines and structure definitions
 */

#ifndef __PHY_DEFS_NR_COMMON__H__
#define __PHY_DEFS_NR_COMMON__H__

#include "PHY/impl_defs_top.h"
#include "impl_defs_nr.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_defs.h"
#include "radio/COMMON/common_lib.h"
#include <pthread.h>

#define MAX_NUM_SUBCARRIER_SPACING 5
#define NR_MAX_OFDM_SYMBOL_SIZE 8192

#define ONE_OVER_SQRT2_Q15 23170

#define NR_MOD_TABLE_SIZE_SHORT 686

#define NR_PSS_LENGTH 127
#define NR_SSS_LENGTH 127

#define NR_MAX_PRS_LENGTH 3264 //272*6(max allocation per RB)*2(QPSK)
#define NR_MAX_PRS_INIT_LENGTH_DWORD 102 // ceil(NR_MAX_CSI_RS_LENGTH/32)
#define NR_MAX_PRS_COMB_SIZE 12
#define NR_MAX_PRS_RESOURCES_PER_SET 64

#define NR_PBCH_DMRS_LENGTH 144 // in mod symbols
#define NR_PBCH_DMRS_LENGTH_DWORD 10 // ceil(2(QPSK)*NR_PBCH_DMRS_LENGTH/32)
#define NR_PBCH_NUM_RB 20
#define NR_PBCH_DMRS_METRIC_FLOOR 1e9

/*used for the resource mapping*/
#define NR_MAX_PDCCH_DMRS_LENGTH 576 // 16(L)*2(QPSK)*3(3 DMRS symbs per REG)*6(REG per CCE)
#define NR_MAX_PDCCH_SIZE 8192 // It seems it is the max polar coded block size
#define NR_MAX_DCI_SIZE 1728 //16(L)*2(QPSK)*9(12 RE per REG - 3(DMRS))*6(REG per CCE)
#define NR_MAX_DCI_SIZE_DWORD 54 // ceil(NR_MAX_DCI_SIZE/32)

#define NR_MAX_PDCCH_AGG_LEVEL 16 // 3GPP TS 38.211 V15.8 Section 7.3.2 Table 7.3.2.1-1: Supported PDCCH aggregation levels

#define MAX_NUM_NR_RE (4*14*273*12)

#define MAX_NUM_NR_SRS_SYMBOLS 4
#define MAX_NUM_NR_SRS_AP 8
#define NUMBER_OF_NR_RU_PRACH_OCCASIONS_MAX 12

#define MAX_DELAY_COMP 20

#define PBCH_MAX_RE_PER_SYMBOL (20 * 12)

#define NR_PUCCH_DMRS_RB 4

typedef enum {
  NR_NORMAL = 0,
  NR_EXTENDED = 1
} nr_prefix_type_t;

typedef enum {
  NR_MU_0=0,
  NR_MU_1,
  NR_MU_2,
  NR_MU_3,
  NR_MU_4,
} nr_numerology_index_e;

typedef enum{
  nr_ssb_type_A = 0,
  nr_ssb_type_B,
  nr_ssb_type_C,
  nr_ssb_type_D,
  nr_ssb_type_E
} nr_ssb_type_e;

typedef struct {
  uint16_t Q_dash_ACK; // number of coded HARQ-ACK symbols
  uint16_t E_uci_ACK; // number of coded HARQ-ACK bits (including reserved ones)
  uint16_t E_uci_ACK_actual; // actual number of coded HARQ-ACK bits
  uint16_t Q_dash_CSI1; // number of coded CSI part 1 symbols
  uint16_t E_uci_CSI1; // number of coded CSI part 1 bits
  uint16_t Q_dash_CSI2; // number of coded CSI part 2 symbols
  uint16_t E_uci_CSI2; // number of coded CSI part 2 bits
  uint32_t G_ulsch; // bit capacity of ULSCH
  int O_ack;
} rate_match_info_uci_t;

typedef struct nr_srs_info_s {
  uint16_t k_0_p[MAX_NUM_NR_SRS_AP][MAX_NUM_NR_SRS_SYMBOLS];
  uint8_t srs_generated_signal_bits;
  int B_SRS;
  int C_SRS;
  int b_hop;
  int comb_size;
  int K_TC_overbar;
  int n_SRS_cs;
  int n_ID_SRS;
  int n_shift;
  int n_RRC;
  int groupOrSequenceHopping;
  int l_offset;
  int T_SRS;
  int T_offset;
  int R;
  int N_symb_SRS;
  int n_srs_ports;
  int resource_type;
} nr_srs_info_t;

typedef struct NR_DL_FRAME_PARMS_s {
  /// frequency range
  frequency_range_t freq_range;
  //  /// Placeholder to replace overlapping fields below
  //  nfapi_nr_rf_config_t rf_config;
  /// Placeholder to replace SSB overlapping fields below
  //  nfapi_nr_sch_config_t sch_config;
  /// Number of resource blocks (RB) in DL
  int N_RB_DL;
  /// Number of resource blocks (RB) in UL
  int N_RB_UL;
  /// Number of resource blocks (RB) in SL
  int N_RB_SL;
  ///  total Number of Resource Block Groups: this is ceil(N_PRB/P)
  uint8_t N_RBG;
  /// Total Number of Resource Block Groups SubSets: this is P
  uint8_t N_RBGS;
  /// DL carrier frequency
  uint64_t dl_CarrierFreq;
  /// UL carrier frequency
  uint64_t ul_CarrierFreq;
  /// SL carrier frequency
  uint64_t sl_CarrierFreq;
  /// TX attenuation
  uint32_t att_tx;
  /// RX attenuation
  uint32_t att_rx;
  ///  total Number of Resource Block Groups: this is ceil(N_PRB/P)
  /// Frame type (0 FDD, 1 TDD)
  frame_type_t frame_type;
  uint8_t tdd_config;
  /// Cell ID
  uint16_t Nid_cell;
  /// subcarrier spacing (15,30,60,120)
  uint32_t subcarrier_spacing;
  /// 3/4 sampling
  int threequarter_fs;
  /// Size of FFT
  uint16_t ofdm_symbol_size;
  /// Number of prefix samples in all but first symbol of slot
  uint16_t nb_prefix_samples;
  /// Number of prefix samples in first symbol of slot
  uint16_t nb_prefix_samples0;
  /// Carrier offset in FFT buffer for first RE in PRB0
  uint16_t first_carrier_offset;
  /// Number of OFDM/SC-FDMA symbols in one slot
  uint16_t symbols_per_slot;
  /// Number of slots per subframe
  uint16_t slots_per_subframe;
  /// Number of slots per frame
  uint16_t slots_per_frame;
  /// Number of samples in a subframe
  uint32_t samples_per_subframe;
  /// Number of samples in 0th and center slot of a subframe
  uint32_t samples_per_slot0;
  /// Number of samples in other slots of the subframe
  uint32_t samples_per_slotN0;
  /// Number of samples in a radio frame
  uint32_t samples_per_frame;
  /// Number of samples in a subframe without CP
  uint32_t samples_per_subframe_wCP;
  /// Number of samples in a slot without CP
  uint32_t samples_per_slot_wCP;
  /// Number of samples in a radio frame without CP
  uint32_t samples_per_frame_wCP;
  /// NR numerology index [0..5] as specified in 38.211 Section 4 (mu). 0=15khZ SCS, 1=30khZ, 2=60kHz, etc
  uint8_t numerology_index;
  /// Number of Physical transmit antennas in node (corresponds to nrOfAntennaPorts)
  uint8_t nb_antennas_tx;
  /// Number of Receive antennas in node
  uint8_t nb_antennas_rx;
  /// Number of common transmit antenna ports in eNodeB (1 or 2)
  uint8_t nb_antenna_ports_gNB;
  /// Cyclic Prefix for DL (0=Normal CP, 1=Extended CP)
  nr_prefix_type_t Ncp;
  /// sequence which is computed based on carrier frequency and numerology to rotate/derotate each OFDM symbol according to Section 5.3 in 38.211
  /// First dimension is for the direction of the link (0 DL, 1 UL, 2 SL)
  c16_t symbol_rotation[3][224];
  /// sequence used to compensate the phase rotation due to timeshifted OFDM symbols
  /// First dimenstion is for different CP lengths
  c16_t timeshift_symbol_rotation[4096*2] __attribute__ ((aligned (16)));
  /// Table used to apply the delay compensation in DL/UL
  c16_t delay_table[2 * MAX_DELAY_COMP + 1][NR_MAX_OFDM_SYMBOL_SIZE];
  /// Table used to apply the delay compensation in PUCCH2
  c16_t delay_table128[2 * MAX_DELAY_COMP + 1][128];
  /// Power used by SSB in order to estimate signal strength and path loss
  int ss_PBCH_BlockPower;

  /// TDD configuration
  uint16_t tdd_uplink_nr[2*NR_MAX_SLOTS_PER_FRAME]; /* this is a bitmap of symbol of each slot given for 2 frames */

  uint8_t half_frame_bit;

  //SSB related params
  /// Start in Subcarrier index of the SSB block
  uint16_t ssb_start_subcarrier;
  /// SSB type
  nr_ssb_type_e ssb_type;
  /// Max number of SSB in frame
  uint8_t Lmax;
  /// SS block pattern (max 64 ssb, each bit is on/off ssb)
  uint64_t L_ssb;
  /// Total number of SSB transmitted
  uint8_t N_ssb;
  /// SSB index
  uint8_t ssb_index;
  /// OFDM symbol offset divisor for UL
  uint32_t ofdm_offset_divisor;
  uint16_t tdd_slot_config;
  uint8_t tdd_period;
  bool print_ue_help_cmdline_log;
} NR_DL_FRAME_PARMS;

// PRS config structures
typedef struct {
    uint16_t PRSResourceSetPeriod[2];   // [slot period, slot offset] of a PRS resource set
    uint16_t PRSResourceOffset;         // Slot offset of each PRS resource defined relative to the slot offset of the PRS resource set (0...511)
    uint8_t  PRSResourceRepetition;     // Repetition factor for all PRS resources in resource set (1 /*default*/, 2, 4, 6, 8, 16, 32)
    uint8_t  PRSResourceTimeGap;        // Slot offset between two consecutive repetition indices of all PRS resources in a PRS resource set (1 /*default*/, 2, 4, 6, 8, 16, 32)
    uint16_t NumRB;                     // Number of PRBs allocated to all PRS resources in a PRS resource set (<= 272 and multiples of 4)
    uint8_t  NumPRSSymbols;             // Number of OFDM symbols in a slot allocated to each PRS resource in a PRS resource set
    uint8_t  SymbolStart;               // Starting OFDM symbol of each PRS resource in a PRS resource set
    uint16_t RBOffset;                  // Starting PRB index of all PRS resources in a PRS resource set
    uint8_t  CombSize;                  // RE density of all PRS resources in a PRS resource set (2, 4, 6, 12)
    uint8_t  REOffset;                  // Starting RE offset in the first OFDM symbol of each PRS resource in a PRS resource set
    uint32_t MutingPattern1[32];        // Muting bit pattern option-1, specified as [] or a binary-valued vector of length 2, 4, 6, 8, 16, or 32
    uint32_t MutingPattern2[32];        // Muting bit pattern option-2, specified as [] or a binary-valued vector of length 2, 4, 6, 8, 16, or 32
    uint8_t  MutingBitRepetition;       // Muting bit repetition factor, specified as 1, 2, 4, or 8
    uint16_t NPRSID;                    // Sequence identity of each PRS resource in a PRS resource set, specified in the range [0, 4095]
} prs_config_t;

typedef struct {
    int8_t  gNB_id;
    int32_t sfn;
    int8_t  slot;
    int8_t  rxAnt_idx;
    pthread_mutex_t dl_toa_mtx; // protect reading of max from write
    // circular buffer to be able to read maximum of last estimations
    float dl_toa[128]; // set through set_prs_dl_toa()
    float *next_dl_toa;
    int32_t dl_aoa;
    float snr;
    float rsrp;
    float rsrp_dBm;
    int32_t reserved;
} prs_meas_t;

// rel16 prs k_prime table as per ts138.211 sec.7.4.1.7.2
static const int16_t k_prime_table[4][12] = {{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
                                             {0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3},
                                             {0, 3, 1, 4, 2, 5, 0, 3, 1, 4, 2, 5},
                                             {0, 6, 3, 9, 1, 7, 4, 10, 2, 8, 5, 11}};

#define KHz (1000UL)
#define MHz (1000*KHz)

// Get symbol duration within slot in samples
uint32_t get_samples_symbol_duration(const NR_DL_FRAME_PARMS *fp, int slot, int start_symbol, int num_symbols);
// Get timestamp of symbol within slot in samples
uint32_t get_samples_symbol_timestamp(const NR_DL_FRAME_PARMS *fp, int slot, int symbol);
// Get slot duration between two slot
uint32_t get_samples_slot_duration(const NR_DL_FRAME_PARMS *fp, unsigned int start_slot, unsigned int num_slots);
// Get timestamp of slot from start of frame
uint32_t get_samples_slot_timestamp(const NR_DL_FRAME_PARMS *fp, unsigned int slot);
// Get slot from timestamp
uint32_t get_slot_from_timestamp(openair0_timestamp_t timestamp_rx, const NR_DL_FRAME_PARMS *fp);
// Get number of samples in the slot
uint32_t get_samples_per_slot(int slot, const NR_DL_FRAME_PARMS *fp);

#endif
