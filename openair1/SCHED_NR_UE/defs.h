/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief NR UE PHY functions prototypes
 */

#ifndef __openair_SCHED_H__
#define __openair_SCHED_H__

#include "PHY/defs_nr_UE.h"


/*enum THREAD_INDEX { OPENAIR_THREAD_INDEX = 0,
                    TOP_LEVEL_SCHEDULER_THREAD_INDEX,
                    DLC_SCHED_THREAD_INDEX,
                    openair_SCHED_NB_THREADS
                  };*/ // do not modify this line

#define OPENAIR_THREAD_STACK_SIZE     PTHREAD_STACK_MIN //4096 //RTL_PTHREAD_STACK_MIN*6
//#define DLC_THREAD_STACK_SIZE        4096 //DLC stack size
//#define UE_SLOT_PARALLELISATION
//#define UE_DLSCH_PARALLELISATION

/*enum openair_SCHED_STATUS {
  openair_SCHED_STOPPED=1,
  openair_SCHED_STARTING,
  openair_SCHED_STARTED,
  openair_SCHED_STOPPING
};*/

/*enum openair_ERROR {
  // HARDWARE CAUSES
  openair_ERROR_HARDWARE_CLOCK_STOPPED= 1,

  // SCHEDULER CAUSE
  openair_ERROR_OPENAIR_RUNNING_LATE,
  openair_ERROR_OPENAIR_SCHEDULING_FAILED,

  // OTHERS
  openair_ERROR_OPENAIR_TIMING_OFFSET_OUT_OF_BOUNDS,
};*/

/*enum openair_SYNCH_STATUS {
  openair_NOT_SYNCHED=1,
  openair_SYNCHED,
  openair_SCHED_EXIT
};*/

/*enum openair_HARQ_TYPE {
  openair_harq_DL = 0,
  openair_harq_UL,
  openair_harq_RA
};*/

/** @addtogroup _PHY_PROCEDURES_
 * @{
 */

/*! \brief Scheduling for UE TX procedures in normal subframes.
  @param ue Pointer to UE variables on which to act
  @param proc Pointer to RXn-TXnp4 proc information
@param phy_data
*/
/// @return false if nothing is transmitted in the slot: txp is then left unwritten
bool phy_procedures_nrUE_TX(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_tx_t *phy_data, c16_t **txp);

int pbch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);
void pdcch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data,int pscch_processing);

void pdsch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);

void processSlotTX(void *arg);

/*! \brief UL time alignment procedures for TA application
  @param ue
  @param slot_tx
  @param frame_tx
*/
void ue_ta_procedures(PHY_VARS_NR_UE *ue, int slot_tx, int frame_tx);

void set_tx_harq_id(NR_UE_ULSCH_t *ulsch, int harq_pid, int slot_tx);
int get_tx_harq_id(NR_UE_ULSCH_t *ulsch, int slot_tx);

int is_pbch_in_slot(fapi_nr_config_request_t *config, int frame, int slot, NR_DL_FRAME_PARMS *fp);
int is_ssb_in_slot(fapi_nr_config_request_t *config, int frame, int slot, NR_DL_FRAME_PARMS *fp);
bool is_csi_rs_in_symbol(fapi_nr_dl_config_csirs_pdu_rel15_t csirs_config_pdu, int symbol);

/*@}*/

/*! \brief This function prepares the dl rx indication
 */
void nr_fill_rx_indication(fapi_nr_rx_indication_t *rx_ind,
                           uint8_t pdu_type,
                           PHY_VARS_NR_UE *ue,
                           int cw_idx,
                           int harq_pid,
                           NR_UE_DLSCH_t *dlsch,
                           const UE_nr_rxtx_proc_t *proc,
                           void *typeSpecific);

void nr_pdcch_generate_llr(PHY_VARS_NR_UE *ue,
                           const UE_nr_rxtx_proc_t *proc,
                           int symbol,
                           nr_phy_data_t *phy_data,
                           int llr_size_symbol,
                           int num_monitoring_occ,
                           int max_symb,
                           c16_t rxdataF[ue->frame_parms.nb_antennas_rx][ue->frame_parms.ofdm_symbol_size],
                           c16_t pdcch_llr[phy_data->phy_pdcch_config.nb_search_space][num_monitoring_occ][max_symb * llr_size_symbol],
			   int pscch_processing);

void nr_pdcch_dci_indication(const UE_nr_rxtx_proc_t *proc,
                             int llr_size,
                             int max_monOcc,
                             PHY_VARS_NR_UE *ue,
                             nr_phy_data_t *phy_data,
                             c16_t llr[phy_data->phy_pdcch_config.nb_search_space][max_monOcc][llr_size],
			     int sci_indication);

void nr_ue_csi_im_procedures(PHY_VARS_NR_UE *ue,
                             const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP],
                             const fapi_nr_dl_config_csiim_pdu_rel15_t *csiim_config_pdu);

void nr_ue_csi_rs_procedures(PHY_VARS_NR_UE *ue,
                             const UE_nr_rxtx_proc_t *proc,
                             const c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP],
                             fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                             c16_t trs_estimates[][1][ue->frame_parms.ofdm_symbol_size],
                             const int res_idx,
                             const int trs_sym0);

void trs_freq_correction(PHY_VARS_NR_UE *ue, int cfo);

void nr_sl_print_phy_stats(PHY_VARS_NR_UE *ue, int frame_rx, int nr_slot_rx, bool *stats_printed);
int psbch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);
void pscch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);
void psfch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);
void pssch_processing(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_t *phy_data);
int nr_sl_psfch_feedback_abs_slot(const nr_phy_data_t *phy_data, const UE_nr_rxtx_proc_t *proc, int slots_per_frame);
void phy_procedures_nrUE_SL_TX(PHY_VARS_NR_UE *ue, const UE_nr_rxtx_proc_t *proc, nr_phy_data_tx_t *phy_data, c16_t **txp);
void nr_fill_sl_rx_indication(sl_nr_rx_indication_t *rx_ind,
                              uint8_t pdu_type,
                              PHY_VARS_NR_UE *ue,
                              uint16_t n_pdus,
                              void *typeSpecific,
                              uint16_t rx_slss_id);

typedef struct {
  //ldpcDecode_t *rdata;
  uint8_t *b;
  int TBS;
  int harq_pid;
  bool rxok;
} slsch_status_t;

#endif
/** @}*/
