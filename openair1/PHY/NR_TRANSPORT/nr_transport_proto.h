/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Function prototypes for PHY physical/transport channel processing and generation
 */

#ifndef __NR_TRANSPORT__H__
#define __NR_TRANSPORT__H__

#include "PHY/defs_nr_common.h"
#include "PHY/defs_gNB.h"
#include "common/utils/fsn.h"

#define NR_PBCH_PDU_BITS 24

NR_gNB_PHY_STATS_t *get_phy_stats(PHY_VARS_gNB *gNB, uint16_t rnti);

int nr_generate_prs(int slot, c16_t *txdataF, int16_t amp, prs_config_t *prs_cfg, const NR_DL_FRAME_PARMS *frame_parms);

/*!
\fn int nr_generate_pss
\brief Generation of the NR PSS
@param
@returns 0 on success
 */
int nr_generate_pss(c16_t *txdataF,
                    int16_t amp,
                    uint8_t ssb_start_symbol,
                    nfapi_nr_config_request_scf_t *config,
                    NR_DL_FRAME_PARMS *frame_parms);

/*!
\fn int nr_generate_sss
\brief Generation of the NR SSS
@param
@returns 0 on success
 */
int nr_generate_sss(c16_t *txdataF, int16_t amp, uint8_t ssb_start_symbol, int nid, NR_DL_FRAME_PARMS *frame_parms);

/*!
\fn void nr_generate_pbch_dmrs
\brief Generation of the DMRS for the PBCH
@param
 */
void nr_generate_pbch_dmrs(uint32_t *gold_pbch_dmrs,
                           c16_t *txdataF,
                           int16_t amp,
                           uint8_t ssb_start_symbol,
                           nfapi_nr_config_request_scf_t *config,
                           NR_DL_FRAME_PARMS *frame_parms);

/*!
\fn void nr_generate_pbch
\brief Generation of the PBCH
@param
 */
void nr_generate_pbch(PHY_VARS_gNB *gNB,
                      const nfapi_nr_dl_tti_ssb_pdu *ssb_pdu,
                      c16_t *txdataF,
                      uint8_t ssb_start_symbol,
                      uint8_t n_hf,
                      int sfn,
                      nfapi_nr_config_request_scf_t *config,
                      NR_DL_FRAME_PARMS *frame_parms);

/*!
\fn int nr_generate_pbch
\brief PBCH interleaving function
@param bit index i of the input payload
@returns the bit index of the output
 */
void nr_init_pbch_interleaver(uint8_t *interleaver);
uint32_t nr_pbch_extra_byte_generation(int sfn, int n_hf, int ssb_index, int ssb_sc_offset, int Lmax);

NR_gNB_DLSCH_t new_gNB_dlsch(NR_DL_FRAME_PARMS *frame_parms, uint16_t N_RB);

void free_gNB_dlsch(NR_gNB_DLSCH_t *dlsch, uint16_t N_RB, const NR_DL_FRAME_PARMS *frame_parms);

/** \brief This function is the top-level entry point to PUSCH demodulation, after frequency-domain transformation and channel estimation.  It performs
    - RB extraction (signal and channel estimates)
    - channel compensation (matched filtering)
    - RE extraction (dmrs)
    - antenna combining (MRC, Alamouti, cycling)
    - LLR computation
    This function supports TM1, 2, 3, 5, and 6.
    @param ue Pointer to PHY variables
    @param pusch_vars ULSCH PUSCH data
    @param rel15_ul FAPI message to process
    @param unav_res unav_res result
    @param frame Frame number
    @param slot Slot number
*/
int nr_rx_pusch_group_tp(PHY_VARS_gNB *gNB,
                         NR_gNB_PUSCH **pusch_vars,
                         const nfapi_nr_pusch_pdu_t **rel15_ul,
                         uint32_t **ret_unav_res,
                         uint8_t group_size,
                         uint32_t frame,
                         uint8_t slot);

/*!
\brief This function implements the idft transform precoding in PUSCH
\param z Pointer to input in frequnecy domain, and it is also the output in time domain
\param Msc_PUSCH number of allocated data subcarriers
*/
void nr_idft(int32_t *z, uint32_t Msc_PUSCH);

void reset_active_stats(PHY_VARS_gNB *gNB, int frame);
void reset_active_ulsch(PHY_VARS_gNB *gNB, int frame);

void nr_fill_ulsch(PHY_VARS_gNB *gNB,
                   int frame,
                   int slot,
                   nfapi_nr_pusch_pdu_t *ulsch_pdu,
                   int16_t mu_group_idx,
                   uint8_t mu_group_size);

void nr_schedule_rx_prach(PHY_VARS_gNB *gNB, int SFN, int Slot, nfapi_nr_prach_pdu_t *prach_pdu);

typedef struct rx_prach_out {
  uint16_t max_preamble;
  uint16_t max_preamble_energy;
  uint16_t max_preamble_delay;
  uint16_t max_preamble_delay_raw; // raw PRACH correlation-bin delay before TA normalization
} rx_prach_out_t;
rx_prach_out_t rx_nr_prach(const prach_item_t *, int occasion);

void rx_nr_prach_ru(prach_item_t *, int32_t **, NR_DL_FRAME_PARMS *frame_parms, int N_TA_offset, bool das);
void rx_nr_prach_ru_rep(prach_item_t *p,
                        int32_t **rxdata,
                        NR_DL_FRAME_PARMS *fp,
                        int N_TA_offset,
                        int rep,
                        int prachOccasion,
                        c16_t (*rxsigF)[NR_PRACH_SEQ_LEN_L]);

void nr_fill_pucch(PHY_VARS_gNB *gNB,
                   int frame,
                   int slot,
                   nfapi_nr_pucch_pdu_t *pucch_pdu);

void nr_fill_srs(PHY_VARS_gNB *gNB,
                 frame_t frame,
                 slot_t slot,
                 nfapi_nr_srs_pdu_t *srs_pdu);

int nr_get_srs_signal(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      slot_t slot,
                      const nfapi_nr_srs_pdu_t *srs_pdu,
                      nr_srs_info_t *nr_srs_info,
                      c16_t srs_received_signal[][gNB->frame_parms.ofdm_symbol_size * (1 << srs_pdu->num_symbols)]);

nr_srs_info_t nr_srs_rx_procedures(PHY_VARS_gNB *gNB,
                          int frame_rx,
                          int slot_rx,
                          uint8_t nb_antennas_rx,
                          uint8_t N_ap,
                          uint8_t N_symb_SRS,
                          uint16_t ofdm_symbol_size,
                          const NR_gNB_SRS_job_t *srs,
                          int *srs_est,
                          int8_t *snr,
                          c16_t srs_estimated_channel_freq[][N_ap][ofdm_symbol_size * N_symb_SRS],
                          int16_t *snr_per_rb,
                          uint16_t *timing_advance_offset,
                          int16_t *timing_advance_offset_nsec);

int get_nr_prach_duration(uint8_t prach_format);

void init_nr_prach(PHY_VARS_gNB *gNB);
void reset_nr_prach(PHY_VARS_gNB *gNB);
void free_nr_prach_entry(prach_item_t *);
bool get_next_nr_prach(spsc_q_t *q, const fsn_t *now, prach_item_t *p);

void nr_decode_pucch1(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t *uci_pdu,
                      nfapi_nr_pucch_pdu_t *pucch_pdu);

void set_uci_payload(uint64_t *in, int n_bits, int n_bytes, uint8_t *out, int frame, int slot);
void nr_decode_pucch2(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_2_3_4_t* uci_pdu,
                      const nfapi_nr_pucch_pdu_t* pucch_pdu);

void nr_decode_pucch0(PHY_VARS_gNB *gNB,
                      c16_t **rxdataF,
                      int frame,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t* uci_pdu,
                      const nfapi_nr_pucch_pdu_t* pucch_pdu);

void nr_uci_decoding(PHY_VARS_gNB *phy_vars_gNB, NR_UL_IND_t *UL_INFO, int nb_pusch, const int *ULSCH_ids, int frame, int slot);
#endif /*__NR_TRANSPORT__H__*/
