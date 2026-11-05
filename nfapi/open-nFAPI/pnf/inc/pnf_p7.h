/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright 2017 Cisco Systems, Inc.
 */



#ifndef _PNF_P7_H_
#define _PNF_P7_H_

#define TIMEHR_SEC(_time_hr) ((uint32_t)(_time_hr) >> 20)
#define TIMEHR_USEC(_time_hr) ((uint32_t)(_time_hr) & 0xFFFFF)
#define TIME2TIMEHR(_time) (((uint32_t)(_time.tv_sec) & 0xFFF) << 20 | ((uint32_t)(_time.tv_usec) & 0xFFFFF))

#include "nfapi_pnf_interface.h"


typedef struct {
	uint16_t dl_conf_ontime;
	uint16_t dl_conf_late;
	uint16_t ul_conf_ontime;
	uint16_t ul_conf_late;
	uint16_t hi_dci0_ontime;
	uint16_t hi_dci0_late;
	uint16_t tx_ontime;
	uint16_t tx_late;
} pnf_p7_stats_t;


typedef struct pnf_p7_arr_time {
  uint16_t ontime;
  uint16_t late;
} pnf_p7_arr_time_t;
typedef struct pnf_p7_dir {
  uint32_t bytes;
} pnf_p7_dir_t;
typedef struct {
  pnf_p7_arr_time_t dl_tti;
  pnf_p7_arr_time_t ul_tti;
  pnf_p7_arr_time_t ul_dci;
  pnf_p7_arr_time_t tx_data;
  pnf_p7_dir_t dl;
  pnf_p7_dir_t ul;
} pnf_p7_nr_stats_t;

typedef struct {
	uint8_t* buffer;
	uint32_t length;
} pnf_p7_rx_message_segment_t;

typedef struct pnf_p7_rx_message pnf_p7_rx_message_t;

typedef struct pnf_p7_rx_message {
	uint8_t sequence_number;
	uint8_t num_segments_received;
	uint8_t num_segments_expected;

	// the spec allows of upto 128 segments, this does seem excessive
	pnf_p7_rx_message_segment_t segments[128];

	uint32_t rx_hr_time;

	pnf_p7_rx_message_t* next;
} pnf_p7_rx_message_t;

typedef struct {

	pnf_p7_rx_message_t* msg_queue;

} pnf_p7_rx_reassembly_queue_t;

typedef struct pnf_p7_s {
  nfapi_pnf_p7_config_t _public;

  //private data
	int p7_sock;

	uint8_t terminate;

	uint8_t tx_message_buffer[NFAPI_MAX_PACKED_MESSAGE_SIZE];
	uint8_t* rx_message_buffer;
	uint32_t rx_message_buffer_size;

	pthread_mutex_t mutex; // should we allow the client to specifiy
	pthread_mutex_t pack_mutex; // should we allow the client to specifiy

	nfapi_pnf_p7_subframe_buffer_t subframe_buffer[30/*NFAPI_MAX_TIMING_WINDOW_SIZE*/];
    nfapi_pnf_p7_slot_buffer_t slot_buffer[30/*NFAPI_MAX_TIMING_WINDOW_SIZE*/];
	uint32_t sequence_number;
	uint16_t max_num_segments;

	pnf_p7_rx_reassembly_queue_t reassembly_queue;

	uint8_t* reassemby_buffer;
	uint32_t reassemby_buffer_size;

	uint16_t sfn_sf;
	uint32_t sf_start_time_hr;
	int32_t sfn_sf_shift;
	
	uint16_t sfn;
	uint16_t slot;
  int mu;
	uint16_t sfn_slot;
	uint32_t slot_start_time_hr;
	int32_t slot_shift;

	uint8_t timing_info_period_counter;
	uint8_t timing_info_aperiodic_send; // 0:false 1:true

	uint32_t timing_info_ms_counter; // number of ms since last timing info

	uint32_t dl_config_jitter;
	uint32_t ul_config_jitter;
	uint32_t hi_dci0_jitter;
	uint32_t tx_jitter;
    
	//P7 NR
	uint32_t dl_tti_jitter;
	uint32_t ul_tti_jitter;
	uint32_t ul_dci_jitter;
	uint32_t tx_data_jitter;

	uint32_t tick;
	pnf_p7_stats_t stats;
	pnf_p7_nr_stats_t nr_stats;

} pnf_p7_t;

int pnf_p7_message_pump(pnf_p7_t* pnf_p7);
int pnf_nr_p7_message_pump(pnf_p7_t* pnf_p7);
int pnf_p7_pack_and_send_p7_message(pnf_p7_t* pnf_p7, nfapi_p7_message_header_t* msg, uint32_t msg_len);
int pnf_p7_send_message(pnf_p7_t* pnf_p7, uint8_t* msg, uint32_t msg_len);

int nr_pnf_p7_get_msgs(pnf_p7_t* pnf_p7,
                       uint16_t phy_id,
                       uint16_t sfn,
                       uint16_t slot,
                       nfapi_nr_dl_tti_request_t* ret_dl_tti,
                       nfapi_nr_ul_tti_request_t* ret_ul_tti,
                       nfapi_nr_ul_dci_request_t* ret_ul_dci,
                       nfapi_nr_tx_data_request_t* ret_tx_data);
int pnf_p7_subframe_ind(pnf_p7_t* config, uint16_t phy_id, uint16_t sfn_sf);
int nfapi_pnf_p7_nr_slot_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_slot_indication_scf_t* ind);
int nfapi_pnf_p7_nr_rx_data_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_rx_data_indication_t* ind);
int nfapi_pnf_p7_nr_crc_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_crc_indication_t* ind);
int nfapi_pnf_p7_nr_srs_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_srs_indication_t* ind);
int nfapi_pnf_p7_nr_srs_toa_vendor_ext_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_srs_toa_vendor_ext_indication_t* ind);
int nfapi_pnf_p7_nr_uci_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_uci_indication_t* ind);
int nfapi_pnf_p7_nr_rach_ind(nfapi_pnf_p7_config_t* config, nfapi_nr_rach_indication_t* ind);
pnf_p7_rx_message_t* pnf_p7_rx_reassembly_queue_add_segment(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, uint32_t rx_hr_time, uint16_t sequence_number, uint16_t segment_number, uint8_t m, uint8_t* data, uint16_t data_len);
void pnf_p7_rx_reassembly_queue_remove_msg(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, pnf_p7_rx_message_t* msg);
void pnf_p7_rx_reassembly_queue_remove_old_msgs(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, uint32_t rx_hr_time, uint32_t delta);
void pnf_nr_handle_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t *pnf_p7, uint32_t rx_hr_time);
struct timespec pnf_timespec_sub(struct timespec lhs, struct timespec rhs);
uint32_t pnf_get_current_time_hr(void);
struct timespec pnf_timespec_add(struct timespec lhs, struct timespec rhs);
void pnf_p7_free(pnf_p7_t* pnf_p7, void* ptr);
void* pnf_p7_malloc(pnf_p7_t* pnf_p7, size_t size);
#endif /* _PNF_P7_H_ */

