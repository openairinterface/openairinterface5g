/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Function prototypes for PHY physical/transport channel processing and generation V8.6 2009-03
 */
#ifndef __NR_TRANSPORT_PROTO_UE__H__
#define __NR_TRANSPORT_PROTO_UE__H__
#include "PHY/defs_nr_UE.h"
#include "SCHED_NR_UE/defs.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include <math.h>
#include "PHY/CODING/nrPolar_tools/nr_polar_psbch_defs.h"
#include "common/utils/bits.h"

// Specifies the data that should be copied to the scope during PDSCH RX
typedef struct pdsch_scope_req_s {
  bool copy_chanest_to_scope;
  bool copy_rxdataF_to_scope;
  size_t scope_rxdataF_offset;
  bool copy_rxdataF_comp_to_scope;
  size_t scope_rxdataF_comp_offset;
} pdsch_scope_req_t;

// Functions below implement 36-211 and 36-212

/** @addtogroup _PHY_TRANSPORT_
 * @{
 */


/** \brief This function initialises structures for DLSCH at UE
*/
void nr_ue_dlsch_init(NR_UE_DLSCH_t *dlsch_list, int num_dlsch, uint8_t max_ldpc_iterations);

void set_first_last_pdcch_symb(const NR_UE_PDCCH_CONFIG *phy_pdcch_config, int symb_slot, int *first_symb, int *last_symb);

int get_max_pdcch_monOcc(const NR_UE_PDCCH_CONFIG *phy_pdcch_config, int nb_symb_slot);

/** \brief This is the alternative top-level entry point for DLSCH decoding in UE.
    It handles all the HARQ processes in only one call. The routine first
    computes the segmentation information and then call LDPC decoder on the
    received LLRs computed by dlsch_demodulation.
    It stops after either unsuccesful decoding of at least
    one segment or correct decoding of all segments. Only the segment CRCs are checked for the moment, the
    overall CRC is ignored. Finally transport block reassembly is performed.
    @param[in] phy_vars_ue Pointer to ue variables
    @param[in] proc
    @param[in] dlsch_llr Pointers to LLR values computed by dlsch_demodulation
    @param[in] b
    @param[in] G array of Gs
    @returns 0 on success, 1 on unsuccessful decoding
*/
void nr_dlsch_decoding(PHY_VARS_NR_UE *phy_vars_ue,
                       const UE_nr_rxtx_proc_t *proc,
                       NR_UE_DLSCH_t *dlsch,
                       int cw_idx,
                       fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config,
                       int16_t *dlsch_llr,
                       uint8_t *b,
                       int number_rbs,
                       int G);

int nr_ulsch_pre_encoding(NR_UL_UE_HARQ_t *harq_process,
                          uint32_t tb_size_bytes,
                          uint8_t nrOfLayers,
                          uint8_t ldpcBaseGraph);

/** \brief This is the alternative top-level entry point for ULSCH encoding in UE.
    It handles all the HARQ processes in only one call. The routine first
    computes the segmentation information, followed by LDPC encoding algorithm of the
    Transport Block.
    @param[in] phy_vars_ue pointer to ue variables
    @param[in] ulsch Pointer to ULSCH descriptor
    @param[in] frame frame index
    @param[in] slot slot index
    @param[in] G array of Gs
    @param[in] nb_ulsch number of uplink shared channels
    @param[in] ULSCH_ids array of uplink shared channel ids
    @returns 0 on success, -1 on unsuccessful decoding
*/
int nr_ulsch_encoding(PHY_VARS_NR_UE *ue,
                      NR_UE_ULSCH_t *ulsch,
                      const uint32_t frame,
                      const uint8_t slot,
                      unsigned int *G,
                      int nb_ulsch,
                      uint8_t *ULSCH_ids);

/** \brief Alternative entry point to UE uplink shared channels procedures.
    It handles all the HARQ processes in only one call.
    Performs the following functionalities:
    - encoding
    - scrambling
    - modulation
    - transform precoding
    @param[in] UE pointer to ue variables
    @param[in] frame frame index
    @param[in] slot slot index
    @param[in] phy_data PHY layer informations
    @param[in] c16_t
*/
void nr_ue_ulsch_procedures(PHY_VARS_NR_UE *UE,
                            const uint32_t frame,
                            const uint8_t slot,
                            nr_phy_data_tx_t *phy_data,
                            c16_t **txdataF,
                            bool was_symbol_used[NR_SYMBOLS_PER_SLOT]);

/** \brief This function does IFFT for PUSCH
*/

void nr_tx_rotation_and_ofdm_mod(const uint8_t slot,
                                 const NR_DL_FRAME_PARMS *frame_parms,
                                 const uint8_t n_antenna_ports,
                                 c16_t **txdataF,
                                 c16_t **txdata,
                                 uint32_t linktype,
                                 bool was_symbol_used[NR_SYMBOLS_PER_SLOT],
                                 bool no_phase_pre_comp);

void ue_srs_procedures_nr(PHY_VARS_NR_UE *ue,
                          const UE_nr_rxtx_proc_t *proc,
                          c16_t **txdataF,
                          const fapi_nr_ul_config_srs_pdu *srs_config_pdu,
                          bool was_symbol_used[NR_SYMBOLS_PER_SLOT]);

void clean_UE_harq(PHY_VARS_NR_UE *UE);

void nr_dlsch_unscrambling(int16_t* llr,
			   uint32_t size,
			   uint8_t q,
			   uint32_t Nid,
			   uint32_t n_RNTI);

/*! \brief Performs detection of SSS to find cell ID and other framing parameters (FDD/TDD, normal/extended prefix)
  @param phy_vars_ue Pointer to UE variables
  @param tot_metric Pointer to variable containing maximum metric under framing hypothesis (to be compared to other hypotheses
  @param flip_max Pointer to variable indicating if start of frame is in second have of RX buffer (i.e. PSS/SSS is flipped)
  @param phase_max Pointer to variable (0 ... 6) containing rought phase offset between PSS and SSS (can be used for carrier
  frequency adjustment. 0 means -pi/3, 6 means pi/3.
  @returns 0 on success
*/
int rx_sss(PHY_VARS_NR_UE *phy_vars_ue,int32_t *tot_metric,uint8_t *flip_max,uint8_t *phase_max);

/*! \brief receiver for the PBCH
  \returns number of tx antennas or -1 if error
*/

double nr_ue_pbch_freq_offset(const NR_DL_FRAME_PARMS *frame_parms,
                              const c16_t dl_ch_est_symb1[NR_PBCH_NUM_RB * NR_NB_SC_PER_RB],
                              const c16_t dl_ch_est_symb3[NR_PBCH_NUM_RB * NR_NB_SC_PER_RB]);

#ifndef modOrder
#define modOrder(I_MCS,I_TBS) ((I_MCS-I_TBS)*2+2) // Find modulation order from I_TBS and I_MCS
#endif

/*!
  \brief This function performs the initial cell search procedure - PSS detection, SSS detection and PBCH detection.  At the
  end, the basic frame parameters are known (Frame configuration - TDD/FDD and cyclic prefix length,
  N_RB_DL, PHICH_CONFIG and Nid_cell) and the UE can begin decoding PDCCH and DLSCH SI to retrieve the rest.  Once these
  parameters are know, the routine calls some basic initialization routines (cell-specific reference signals, etc.)
@param proc
  @param phy_vars_ue Pointer to UE variables
@param n_frames
  @param sa current running mode
*/
nr_initial_sync_t nr_initial_sync(UE_nr_rxtx_proc_t *proc,
                                  PHY_VARS_NR_UE *phy_vars_ue,
                                  int input_sz,
                                  c16_t **input,
                                  nr_gscn_info_t gscnInfo[MAX_GSCN_BAND],
                                  int numGscn);

/*!
  \brief Common SSB search function shared by initial sync and neighbor cell search
  @param params Pointer to SSB search parameters structure
  @return true if SSB was successfully detected, false otherwise
*/
bool nr_search_ssb_common(nr_ssb_search_params_t *params);

/*!
  \brief This function gets the carrier frequencies either from FP or command-line-set global variables, depending on the
  availability of the latter
  @param ue
  @param dl_Carrier Pointer to DL carrier to be set
  @param ul_Carrier Pointer to UL carrier to be set
*/
void nr_get_carrier_frequencies(const PHY_VARS_NR_UE *ue, uint64_t *dl_Carrier, uint64_t *ul_Carrier);

/*!
  \brief This function sets the OAI RF card rx/tx params
  @param openair0_cfg   Pointer OAI config for a specific card
*/
void nr_rf_card_config_gain(openair0_config_t *openair0_cfg);

void nr_rf_card_config_freq(openair0_config_t *openair0_cfg,
                            uint64_t ul_Carrier,
                            uint64_t dl_Carrier,
                            int freq_offset);

void nr_rf_card_get_freq(const openair0_config_t *openair0_cfg, uint64_t *ul_freq, uint64_t *dl_freq, int channel_id);

void nr_sl_rf_card_config_freq(PHY_VARS_NR_UE *ue,
                               openair0_config_t *openair0_cfg,
                               int freq_offset);

/** \brief This function is the top-level entry point to PDSCH demodulation, after frequency-domain transformation and channel
   estimation.  It performs
    - RB extraction (signal and channel estimates)
    - channel compensation (matched filtering)
    - RE extraction (pilot, PBCH, synch. signals)
    - antenna combining (MRC, Alamouti, cycling)
    - LLR computation
    This function supports TM1, 2, 3, 5, and 6.
    @param ue Pointer to PHY variables
    @param proc
    @prama dlsch
    @param symbol Symbol on which to act (within sub-frame)
    @param first_symbol_flag set to 1 on first DLSCH symbol
    @param harq_pid
    @param pdsch_est_size
    @param dl_ch_estimates
    @param llr
    @param rxdataF
    @param llr_offset
    @param log2_maxhrx_size_symbol
    @param rx_size_symbol
    @param nbRx
    @param rxdataF_comp
    @param ptrs_phase_per_slot
    @param ptrs_re_per_slot
*/
uint32_t nr_rx_pdsch(PHY_VARS_NR_UE *ue,
                     const UE_nr_rxtx_proc_t *proc,
                     NR_UE_DLSCH_t *dlsch,
                     const freq_alloc_bitmap_t *freq_alloc,
                     fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config,
                     NR_DL_UE_HARQ_t *dlsch_harq,
                     unsigned char symbol,
                     bool first_symbol_flag,
                     unsigned char harq_pid,
                     uint32_t pdsch_est_size,
                     int32_t dl_ch_estimates[][pdsch_est_size],
                     int16_t *llr,
                     c16_t (*rxdataF)[ue->frame_parms.samples_per_slot_wCP],
                     int32_t *log2_maxh,
                     uint32_t pdsch_buf_size_max,
                     int nbRx,
                     int max_layers,
                     c16_t rxdataF_comp[][pdsch_buf_size_max],
                     c16_t dl_ch_mag[][pdsch_buf_size_max],
                     c16_t dl_ch_magb[][pdsch_buf_size_max],
                     c16_t dl_ch_magr[][pdsch_buf_size_max],
                     c16_t ptrs_phase,
                     uint ptrs_re_per_symbol,
                     uint32_t nvar,
                     pdsch_scope_req_t *scope_req,
                     c16_t rho_dl[][pdsch_buf_size_max],
                     uint16_t ptrs_symb_pos);

int32_t generate_nr_prach(PHY_VARS_NR_UE *ue, uint8_t gNB_id, int frame, uint8_t slot, int16_t tx_amp, c16_t **txData);
void apply_ntn_config(PHY_VARS_NR_UE *UE,
                      const NR_DL_FRAME_PARMS *fp,
                      int hfn_rx,
                      int frame_rx,
                      int slot_rx,
                      int *duration_rx_to_tx,
                      int *timing_advance,
                      int *ntn_koffset,
                      bool *ntn_targetcell);
void fix_ntn_epoch_hfn(PHY_VARS_NR_UE *UE, int hfn, int frame);
void apply_ntn_timing_advance_and_doppler(PHY_VARS_NR_UE *UE, const NR_DL_FRAME_PARMS *fp, int abs_subframe_tx);
void dump_nrdlsch(PHY_VARS_NR_UE *ue,uint8_t gNB_id,uint8_t nr_slot_rx,unsigned int *coded_bits_per_codeword,int round,  unsigned char harq_pid);
void nr_a_sum_b(c16_t *input_x, c16_t *input_y, unsigned short nb_rb);

void nr_generate_psbch_llr(const NR_DL_FRAME_PARMS *frame_parms,
                           const c16_t rxdataF[][frame_parms->ofdm_symbol_size],
                           const c16_t dl_ch_estimates[][frame_parms->ofdm_symbol_size],
                           int symbol,
                           int *psbch_e_rx_offset,
                           int16_t psbch_e_rx[SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2],
                           int16_t psbch_unClipped[SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2]);

int nr_psbch_decode(PHY_VARS_NR_UE *ue,
                    int16_t psbch_e_rx[SL_NR_POLAR_PSBCH_E_NORMAL_CP + 2],
                    const UE_nr_rxtx_proc_t *proc,
                    int psbch_e_rx_len,
                    int slss_id,
                    nr_phy_data_t *phy_data,
                    uint8_t decoded_pdu[4]);

void nr_tx_psbch(PHY_VARS_NR_UE *UE, uint32_t frame_tx, uint32_t slot_tx, sl_nr_tx_config_psbch_pdu_t *psbch_vars, c16_t **txdataF);

nr_initial_sync_t sl_nr_slss_search(PHY_VARS_NR_UE *UE, UE_nr_rxtx_proc_t *proc, int num_frames, int input_sz, c16_t **input);

// Reuse already existing PBCH functions
void nr_pbch_channel_compensation(const struct complex16 rxdataF_ext[][PBCH_MAX_RE_PER_SYMBOL],
                                  const struct complex16 dl_ch_estimates_ext[][PBCH_MAX_RE_PER_SYMBOL],
                                  int nb_re,
                                  struct complex16 rxdataF_comp[][PBCH_MAX_RE_PER_SYMBOL],
                                  const NR_DL_FRAME_PARMS *frame_parms,
                                  const uint8_t output_shift);
void nr_pbch_unscrambling(int16_t *demod_pbch_e,
                          const uint16_t Nid,
                          const uint8_t nushift,
                          const uint16_t M,
                          const uint16_t length,
                          const uint8_t bitwise,
                          const uint32_t unscrambling_mask,
                          const uint32_t pbch_a_prime,
                          uint32_t *pbch_a_interleaved);
void nr_pbch_quantize(int16_t *pbch_llr8, const int16_t *pbch_llr, const uint16_t len);
void nr_generate_pbch_llr(const PHY_VARS_NR_UE *ue,
                          const UE_nr_rxtx_proc_t *proc,
                          const NR_DL_FRAME_PARMS *frame_parms,
                          const int symbolSSB,
                          const int i_ssb,
                          const int nid,
                          const int ssb_start_subcarrier,
                          const c16_t rxdataF[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size],
                          const c16_t dl_ch_estimates[frame_parms->nb_antennas_rx][frame_parms->ofdm_symbol_size],
                          int16_t pbch_e_rx[NR_POLAR_PBCH_E],
                          uint8_t *log2_maxh);
int nr_pbch_decode(PHY_VARS_NR_UE *ue,
                   const NR_DL_FRAME_PARMS *frame_parms,
                   const UE_nr_rxtx_proc_t *proc,
                   const int i_ssb,
                   const int Nid_cell,
                   int16_t pbch_e_rx[NR_POLAR_PBCH_E],
                   int *half_frame_bit,
                   int *ssb_index,
                   int *ret_symbol_offset,
                   fapiPbch_t *result);
/**@}*/
#endif
