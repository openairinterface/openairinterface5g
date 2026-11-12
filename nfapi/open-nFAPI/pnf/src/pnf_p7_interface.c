/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright 2017 Cisco Systems, Inc.
 */


#include "pnf_p7.h"
#include <stdlib.h>
#include <string.h>

nfapi_pnf_p7_config_t* nfapi_pnf_p7_config_create()
{
  pnf_p7_t* _this = NULL;
  int rc = posix_memalign((void**)&_this, 32, sizeof(pnf_p7_t));

  if (_this == NULL || rc != 0)
    return 0;


	// set the default parameters
	_this->_public.segment_size = 65000; // UDP max packet size is 65535
	_this->max_num_segments = 8;
	
	_this->_public.subframe_buffer_size = 8;// TODO: Initialize the slot_buffer size
	_this->_public.timing_info_mode_periodic = 1;
	_this->_public.timing_info_period = 32;
	_this->_public.timing_info_mode_aperiodic = 1;
	
	_this->_public.checksum_enabled = 1;
	
	_this->_public.malloc = &malloc;
	_this->_public.free = &free;	

	_this->_public.codec_config.allocate = &malloc;
	_this->_public.codec_config.deallocate = &free;

	// cppcheck-suppress memleak
	return &(_this->_public);
}

void nfapi_pnf_p7_config_destory(nfapi_pnf_p7_config_t* config)
{
	if(config == 0)
		return ;

	free(config);
}



int nfapi_pnf_p7_start(nfapi_pnf_p7_config_t* config)
{
	// Verify that config is not null
	if(config == 0)
		return -1;

	pnf_p7_t* _this = (pnf_p7_t*)(config);

	NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);

	pnf_p7_message_pump(_this);

	return 0;
}




int nfapi_pnf_p7_stop(nfapi_pnf_p7_config_t* config)
{
	// Verify that config is not null
	if(config == 0)
		return -1;

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	_this->terminate = 1;

	return 0;
}

int nfapi_pnf_p7_get_msgs(nfapi_pnf_p7_config_t* config,
                          uint16_t phy_id,
                          uint16_t sfn,
                          uint16_t slot,
                          nfapi_nr_dl_tti_request_t* ret_dl_tti,
                          nfapi_nr_ul_tti_request_t* ret_ul_tti,
                          nfapi_nr_ul_dci_request_t* ret_ul_dci,
                          nfapi_nr_tx_data_request_t* ret_tx_data)
{
  // Verify that config is not null
  if (config == 0)
    return -1;

  pnf_p7_t* _this = (pnf_p7_t*)(config);

  return nr_pnf_p7_get_msgs(_this, phy_id, sfn, slot, ret_dl_tti, ret_ul_tti, ret_ul_dci, ret_tx_data);
}

int nfapi_pnf_p7_subframe_ind(nfapi_pnf_p7_config_t* config, uint16_t phy_id, uint16_t sfn_sf)
{
	// Verify that config is not null
	if(config == 0)
		return -1;
	
	pnf_p7_t* _this = (pnf_p7_t*)(config);

	return pnf_p7_subframe_ind(_this, phy_id, sfn_sf);
}

int nfapi_pnf_p7_harq_ind(nfapi_pnf_p7_config_t* config, nfapi_harq_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);

	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_harq_indication_t));
}
int nfapi_pnf_p7_crc_ind(nfapi_pnf_p7_config_t* config, nfapi_crc_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_crc_indication_t));
}
int nfapi_pnf_p7_rx_ind(nfapi_pnf_p7_config_t* config, nfapi_rx_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_rx_indication_t));
}
int nfapi_pnf_p7_rach_ind(nfapi_pnf_p7_config_t* config, nfapi_rach_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_rach_indication_t));
}

int nfapi_pnf_p7_sr_ind(nfapi_pnf_p7_config_t* config, nfapi_sr_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_sr_indication_t));
}
int nfapi_pnf_p7_cqi_ind(nfapi_pnf_p7_config_t* config, nfapi_cqi_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_cqi_indication_t));
}
int nfapi_pnf_p7_lbt_dl_ind(nfapi_pnf_p7_config_t* config, nfapi_lbt_dl_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_lbt_dl_indication_t));
}
int nfapi_pnf_p7_nb_harq_ind(nfapi_pnf_p7_config_t* config, nfapi_nb_harq_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_nb_harq_indication_t));
}
int nfapi_pnf_p7_nrach_ind(nfapi_pnf_p7_config_t* config, nfapi_nrach_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, (nfapi_p7_message_header_t*)ind, sizeof(nfapi_nrach_indication_t));
}
int nfapi_pnf_p7_vendor_extension(nfapi_pnf_p7_config_t* config, nfapi_p7_message_header_t* msg)
{
	if(config == NULL || msg == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
	return pnf_p7_pack_and_send_p7_message(_this, msg, 0);
}

int nfapi_pnf_ue_release_resp(nfapi_pnf_p7_config_t* config, nfapi_ue_release_response_t* resp)
{
	if (config == NULL || resp == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: NULL parameters\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);

	return pnf_p7_pack_and_send_p7_message(_this, &(resp->header), sizeof(nfapi_ue_release_response_t));
}

//NR UPLINK INDICATION

int nfapi_pnf_p7_nr_slot_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_slot_indication_scf_t* ind)
{
  if (config == NULL || ind == NULL) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
    return -1;
  }

  pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
  return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_slot_indication_scf_t));
}

int nfapi_pnf_p7_nr_rx_data_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_rx_data_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
	bool ret = _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_rx_data_indication_t));
  if (ret) {
    for (int i = 0; i < ind->number_of_pdus; ++i)
      _this->nr_stats.ul.bytes += ind->pdu_list[i].pdu_length;
  }
  return ret;
}

int nfapi_pnf_p7_nr_crc_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_crc_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
	return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_crc_indication_t));
}

int nfapi_pnf_p7_nr_srs_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_srs_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
	return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_srs_indication_t));
}

int nfapi_pnf_p7_nr_srs_toa_vendor_ext_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_srs_toa_vendor_ext_indication_t* ind)
{
  if (config == NULL || ind == NULL) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
    return -1;
  }

  pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
  return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_srs_toa_vendor_ext_indication_t));
}

int nfapi_pnf_p7_nr_uci_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_uci_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
	return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_uci_indication_t));
}

int nfapi_pnf_p7_nr_rach_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_rach_indication_t* ind)
{
	if(config == NULL || ind == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return -1;
	}

	pnf_p7_t* _this = (pnf_p7_t*)(config);
  AssertFatal(_this->_public.send_p7_msg, "Function pointer must be configured|");
	return _this->_public.send_p7_msg(_this, (nfapi_nr_p7_message_header_t*)ind, sizeof(nfapi_nr_rach_indication_t));
}
