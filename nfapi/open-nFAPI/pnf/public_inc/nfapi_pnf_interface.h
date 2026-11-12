/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright 2017 Cisco Systems, Inc.
 */


#ifndef _NFAPI_PNF_INTERFACE_H_
#define _NFAPI_PNF_INTERFACE_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "nfapi_interface.h"
#include "debug.h"
#include <openair2/PHY_INTERFACE/IF_Module.h>
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"

#include <sys/types.h>
#include "openair1/PHY/defs_gNB.h"

typedef struct pnf_s pnf_t;
typedef struct pnf_p7_s pnf_p7_t;

/*! This enum is used to describe the states of the pnf 
 */
typedef enum
{
	NFAPI_PNF_IDLE = 0,
	NFAPI_PNF_CONFIGURED,
	NFAPI_PNF_RUNNING
} nfapi_pnf_state_e;

/*! This enum is used to describe the states of a phy instance of a pnf
 */
typedef enum
{
	NFAPI_PNF_PHY_IDLE = 0,
	NFAPI_PNF_PHY_CONFIGURED = 1,
	NFAPI_PNF_PHY_RUNNING = 2
} nfapi_pnf_phy_state_e;

typedef struct nfapi_pnf_phy_config nfapi_pnf_phy_config_t;

/*! Configuration information for a pnf phy instance
 */
typedef struct nfapi_pnf_phy_config
{
	/*! The PHY id*/
	uint16_t phy_id;

	/*! The state of the PNF PHY instance*/
	nfapi_pnf_phy_state_e state;

	/*! Optional user defined data that will be passed back in the callbacks*/
	void* user_data;

	/*! Pointer for use in a linked list */
	struct nfapi_pnf_phy_config* next;
} nfapi_pnf_phy_config_t;

typedef struct nfapi_pnf_config nfapi_pnf_config_t;

/*! Configuration information for the pnf created by calling nfapi_pnf_create
 */
typedef struct nfapi_pnf_config
{
	/*! A user define callback to override the default memory allocation 
	 * \param size The size of the data buffer to allocate
	 * \return A pointer to a data buffer
	 */
	void* (*malloc)(size_t size);
	
	/*! A user define callback to override the default memory deallocation 
	 * \param ptr Pointer to a data buffer to be deallocated
	 */
	void (*free)(void* ptr);

	/*! A user define callback to handle trace from the pnf 
	 * \param level The trace level 
	 * \param message The trace string
	 * 
	 * This is a vardic function.
	 */
	void (*trace)(nfapi_trace_level_t  level, const char* message, ...);

	/*! The ip address of the VNF 
	 *
	 */
	char* vnf_ip_addr;

	/*! The ip port of the VNF 
	 */
	int vnf_p5_port;

	/*! The state of the PNF */
	nfapi_pnf_state_e state;

	/*! List of PHY instances configured for this PNF */
	nfapi_pnf_phy_config_t* phys;
	
	/*! Configuation option of the p4 p5 encode decode functions */
	nfapi_p4_p5_codec_config_t codec_config;
	
	/*! Optional user defined data that will be passed back in the callbacks*/
	void* user_data;


	/*! A callback for the PNF_PARAM.request 
	 *  \param config A pointer to the pnf configuration
	 *  \param req A data structure for the decoded PNF_PARAM.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the PNF_PARAM.response after receiving the
	 *  PNF_PARAM.request. This can be done in the call back. 
	 */
	int (*pnf_nr_param_req)(nfapi_pnf_config_t* config, nfapi_nr_pnf_param_request_t* req);
	int (*pnf_param_req)(nfapi_pnf_config_t* config, nfapi_pnf_param_request_t* req);
	
	/*! A callback for the PNF_CONFIG.request
	 *  \param config A pointer to the pnf configuration
	 *  \param req A data structure for the decoded PNF_CONFIG.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the PNF_CONFIG.response after receiving the
	 *  PNF_CONFIG.request. This can be done in the call back. 
	 */
	int (*pnf_config_req)(nfapi_pnf_config_t* config, nfapi_pnf_config_request_t* req);
	int (*pnf_nr_config_req)(nfapi_pnf_config_t* config, nfapi_nr_pnf_config_request_t* req);

	/*! A callback for the PNF_START.request
	 *  \param config A pointer to the pnf configuration
	 *  \param req A data structure for the decoded PNF_CONFIG.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the PNF_START.response after receiving the
	 *  PNF_START.request. This can be done in the call back. 
	 */
	int (*pnf_start_req)(nfapi_pnf_config_t* config, nfapi_pnf_start_request_t* req);
	int (*pnf_nr_start_req)(nfapi_pnf_config_t* config, nfapi_nr_pnf_start_request_t* req);

	/*! A callback for the PNF_STOP.request
	 *  \param config A pointer to the pnf configuration
	 *  \param req A data structure for the decoded PNF_STOP.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the PNF_STOP.response after receiving the
	 *  PNF_STOP.request. This can be done in the call back. 
	 */
	int (*pnf_stop_req)(nfapi_pnf_config_t* config, nfapi_pnf_stop_request_t* req);

	/*! A callback for the PARAM.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded PARAM.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the PARAM.response after receiving the
	 *  PARAM.request. This can be done in the call back. 
	 */
	int (*param_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_param_request_t* req);
	int (*nr_param_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_nr_param_request_scf_t* req);

	/*! A callback for the CONFIG.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded CONFIG.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the CONFIG.response after receiving the
	 *  CONFIG.request. This can be done in the call back. 
	 */
	int (*config_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_config_request_t* req);
	int (*nr_config_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_nr_config_request_scf_t* req);
	
	/*! A callback for the START.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded START.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the START.response after the client has received the
	 *  first subframe indication from FAPI.
	 */
	int (*start_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy,  nfapi_start_request_t* req);
	int (*nr_start_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy,  nfapi_nr_start_request_scf_t* req);
	/*! A callback for the STOP.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded STOP.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the STOP.response after receiving the
	 *  STOP.request. This can be done in the call back. 
	 */
	int (*stop_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_stop_request_t* req);
  int (*nr_stop_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_nr_stop_request_scf_t* req);

  /*! A callback for the MEASUREMENT.request
   *  \param config A pointer to the pnf configuration
   *  \param phy A pointer to the pnf phy configuration
   *  \param req A data structure for the decoded MEASUREMENT.request. This will have
   *             been allocated on the stack
   *  \return not currently used
   *
   * 	The client is expected to send the MEASUREMENT.response after receiving the
   *  MEASUREMENT.request. This can be done in the call back.
   */
  int (*measurement_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_measurement_request_t* req);

  /*! A callback for the RSSI.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded RSSI.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the RSSI.response after receiving the
	 *  RSSI.request. This can be done in the call back. 
	 */
	int (*rssi_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_rssi_request_t* req);
	
	/*! A callback for the CELL_SEARCH.request
	 *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded CELL_SEARCH.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the CELL_SEARCH.response after receiving the
	 *  CELL_SEARCH.request. This can be done in the call back. 	
	 */
	int (*cell_search_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_cell_search_request_t* req);
	
	/*! A callback for the BROADCAST_DETECT.request
     *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded BROADCAST_DETECT.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the BROADCAST_DETECT.response after receiving the
	 *  BROADCAST_DETECT.request. This can be done in the call back. 	
	 */
	int (*broadcast_detect_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_broadcast_detect_request_t* req);
	
	/*! A callback for the SYSTEM_INFORMATION_SCHEDULE.request
     *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded SYSTEM_INFORMATION_SCHEDULE.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the SYSTEM_INFORMATION_SCHEDULE.response after receiving the
	 *  SYSTEM_INFORMATION_SCHEDULE.request. This can be done in the call back. 	
	 */
	int (*system_information_schedule_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_system_information_schedule_request_t* req);
	
	/*! A callback for the SYSTEM_INFORMATION.request
     *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded SYSTEM_INFORMATION.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the SYSTEM_INFORMATION.response after receiving the
	 *  SYSTEM_INFORMATION.request. This can be done in the call back. 	
	 */
	int (*system_information_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_system_information_request_t* req);
	
	/*! A callback for the NMM_STOP.request
     *  \param config A pointer to the pnf configuration
	 *  \param phy A pointer to the pnf phy configuration
	 *  \param req A data structure for the decoded NMM_STOP.request. This will have
	 *             been allocated on the stack
	 *  \return not currently used
	 * 
	 * 	The client is expected to send the NMM_STOP.response after receiving the
	 *  NMM_STOP.request. This can be done in the call back.
	 */
	int (*nmm_stop_req)(nfapi_pnf_config_t* config, nfapi_pnf_phy_config_t* phy, nfapi_nmm_stop_request_t* req);
	
	/*! A callback for any vendor extension messages recevied
	 *  \param config A pointer to the pnf configuration
	 *  \param msg A pointer to the decode P4/P5 message
	 *  \return not current used
	 */
	int (*vendor_ext)(nfapi_pnf_config_t* config, void* msg);
	
	/*! A callback to allocate vendor extension message
	 * \param message_id The message id from the decode P4/P5 message header
	 * \param msg_size A pointer a the size of the allocated message structure. The callee should set this
	 * \return A pointer to a allocated P4/P5 message structure
	 */
	void* (*allocate_p4_p5_vendor_ext)(uint16_t message_id, uint16_t* msg_size);
	
	/*! A callback to deallocate vendor extension message 
	 * \param header A pointer to an P4/P5 message structure
	 */
	void (*deallocate_p4_p5_vendor_ext)(void* header);

  /*! \brief Encodes an (n)FAPI P5 message to a buffer
   *
   *  This function is called internally in send_p5_msg()
   *  \param pMessageBuf A pointer to a nfapi p5 message structure
   *  \param messageBufLen The size of the p5 message structure
   *  \param pPackedBuf A pointer to the buffer that the p5 message will be packed into
   *  \param packedBufLen The size of the buffer
   *  \param config A pointer to the nfapi configuration structure
   *  \return != 0 means success, -1 means failure.
   */
  int (*pack_func)(void* pMessageBuf,
                   uint32_t messageBufLen,
                   void* pPackedBuf,
                   uint32_t packedBufLen,
                   nfapi_p4_p5_codec_config_t* config);

  /*! \brief Decodes a (n)FAPI P5 message
   *  \param pMessageBuf A pointer to an encoded P5 message
   *  \param messageBufLen The size of the encoded P5 message
   *  \param pUnpackedBuf A pointer to the nfapi_message_header
   *  \param unpackedBufLen The size of nfapi_message_header structure.
   *  \param config A pointer to the nfapi configuration structure
   *  \return true on success, false on failure.
   */
  bool (*unpack_func)(void* pMessageBuf,
                      uint32_t messageBufLen,
                      void* pUnpackedBuf,
                      uint32_t unpackedBufLen,
                      nfapi_p4_p5_codec_config_t* config);

  /*! \brief Decodes an (n)FAPI P5 message header
   *  \param pMessageBuf A pointer to an encoded P5 message header
   *  \param messageBufLen The size of the encoded P5 message header
   *  \param pUnpackedBuf A pointer to the nfapi_message_header
   *  \param unpackedBufLen The size of nfapi_message_header structure.
   *  \param config A pointer to the nfapi configuration structure
   *  \return true on success, false on failure.
   */
  bool (*hdr_unpack_func)(void* pMessageBuf,
                          uint32_t messageBufLen,
                          void* pUnpackedBuf,
                          uint32_t unpackedBufLen,
                          nfapi_p4_p5_codec_config_t* config);

  /*! \brief Sends a (N)FAPI P5 message
   *
   *  This function internally packs the given message into a buffer by calling the previously defined pack_func() function pointer
   *  Then, having the message buffer populated, sends it to the VNF
   *  \param pnf A pointer to a pnf_t*
   *  \param msg A pointer to the P5 message
   *  \param msg_len The size of the encoded P5 message
   *  \return true on success, false on failure.
   */
  bool (*send_p5_msg)(pnf_t* pnf, nfapi_nr_p4_p5_message_header_t* msg, uint32_t msg_len);
} nfapi_pnf_config_t;

/*! Create a pnf configuration 
 *  \return A pointer to a pnf configuration struture
 * 
 *  This function will create and initialize a pnf instance. It is expected that 
 *  the client will set the callback and parameters need before calling nfapi_pnf_start
 *
 *  0 will be returned if it fails.
 * 
 * \code
 * nfapi_pnf_config_t* config = nfapi_pnf_config_create(void);
 * \endcode
 */
nfapi_pnf_config_t* nfapi_pnf_config_create(void);

/*! Delete a pnf configuration 
 * \param config A pointer to a pnf configuraiton
 * \return 0 is success, -1 for failure
 */
int nfapi_pnf_config_destroy(nfapi_pnf_config_t* config);

/*! Start the PNF library. 
 * \param config A pointer to the pnf configuration
 * \return 0 is success, -1 for failure
 * 
 * This function will not return until nfapi_pnf_stop is called
 *
 * \code
 * // Create the pnf config
 * nfapi_pnf_config_t* config = nfapi_pnf_config_create();
 *
 * // Assumed that the vnf_address and vnf_port are provided over the P9 interface
 * config->vnf_ip_addr = vnf_address;
 * config->vnf_p5_port = vnf_port;
 *
 * // Set the required callbacks
 * config->pnf_param_req = &pnf_param_request;
 * ...
 * 
 * // Call start for the PNF to initiate a connection to the VNF
 * nfai_pnf_start(config);
 * 
 * \endcode
 */
int nfapi_pnf_start(nfapi_pnf_config_t* config);

/*! Stop the PNF library. 
 * \param config A pointer to the pnf configuration
 * \return 0 is success, -1 for failure
 * 
 * This function will cause the nfapi_pnf_start function to return
 */
int nfapi_pnf_stop(nfapi_pnf_config_t* config);

/*! Send the PNF_PARAM.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_pnf_param_resp(nfapi_pnf_config_t* config, nfapi_pnf_param_response_t* resp);
int nfapi_nr_pnf_pnf_param_resp(nfapi_pnf_config_t* config, nfapi_nr_pnf_param_response_t* resp);


/*! Send the PNF_CONFIG.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_pnf_config_resp(nfapi_pnf_config_t* config, nfapi_pnf_config_response_t* resp);
int nfapi_nr_pnf_pnf_config_resp(nfapi_pnf_config_t* config, nfapi_nr_pnf_config_response_t* resp);

/*! Send the PNF_START.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_pnf_start_resp(nfapi_pnf_config_t* config, nfapi_pnf_start_response_t* resp);
int nfapi_nr_pnf_pnf_start_resp(nfapi_pnf_config_t* config, nfapi_nr_pnf_start_response_t* resp);
/*! Send the PNF_STOP.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_pnf_stop_resp(nfapi_pnf_config_t* config, nfapi_pnf_stop_response_t* resp);

/*! Send the PARAM.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_param_resp(nfapi_pnf_config_t* config, nfapi_param_response_t* resp);
int nfapi_nr_pnf_param_resp(nfapi_pnf_config_t* config, nfapi_nr_param_response_scf_t* resp);

/*! Send the CONFIG.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_config_resp(nfapi_pnf_config_t* config, nfapi_config_response_t* resp);
int nfapi_nr_pnf_config_resp(nfapi_pnf_config_t* config, nfapi_nr_config_response_scf_t* resp);
/*! Send the START.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_start_resp(nfapi_pnf_config_t* config, nfapi_start_response_t* resp);
int nfapi_nr_pnf_start_resp(nfapi_pnf_config_t* config, nfapi_nr_start_response_scf_t* resp);

/*! Send the STOP.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_stop_resp(nfapi_pnf_config_t* config, nfapi_stop_response_t* resp);

/*! Send the STOP.indication
 * \param config A pointer to a pnf configuration
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 *
 */
int nfapi_nr_stop_indication(nfapi_pnf_config_t* config, nfapi_nr_stop_indication_scf_t* resp);

/*! Send the MEASUREMENT.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_measurement_resp(nfapi_pnf_config_t* config, nfapi_measurement_response_t* resp);

/*! Send the RSSI.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_rssi_resp(nfapi_pnf_config_t* config, nfapi_rssi_response_t* resp);

/*! Send the RSSI.indication
 * \param config A pointer to a pnf configuraiton
 * \param ind A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_rssi_ind(nfapi_pnf_config_t* config, nfapi_rssi_indication_t* ind);

/*! Send the CELL_SEARCH.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_cell_search_resp(nfapi_pnf_config_t* config, nfapi_cell_search_response_t* resp);

/*! Send the CELL_SEARCH.indication
 * \param config A pointer to a pnf configuraiton
 * \param ind A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_cell_search_ind(nfapi_pnf_config_t* config, nfapi_cell_search_indication_t* ind);

/*! Send the BROADCAST_DETECT.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_broadcast_detect_resp(nfapi_pnf_config_t* config, nfapi_broadcast_detect_response_t* resp);

/*! Send the BROADCAST_DETECT.indication
 * \param config A pointer to a pnf configuraiton
 * \param ind A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_broadcast_detect_ind(nfapi_pnf_config_t* config, nfapi_broadcast_detect_indication_t* ind);

/*! Send the SYSTEM_INFORMATION_SCHEDULE.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_system_information_schedule_resp(nfapi_pnf_config_t* config, nfapi_system_information_schedule_response_t* resp);

/*! Send the SYSTEM_INFORMATION_SCHEDULE.indication
 * \param config A pointer to a pnf configuraiton
 * \param ind A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_system_information_schedule_ind(nfapi_pnf_config_t* config, nfapi_system_information_schedule_indication_t* ind);

/*! Send the SYSTEM_INFORMATION.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_system_information_resp(nfapi_pnf_config_t* config, nfapi_system_information_response_t* resp);

/*! Send the SYSTEM_INFORMATION.indication
 * \param config A pointer to a pnf configuraiton
 * \param ind A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_system_information_ind(nfapi_pnf_config_t* config, nfapi_system_information_indication_t* ind);

/*! Send the NMM_STOP.response
 * \param config A pointer to a pnf configuraiton
 * \param resp A pointer to the message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_nmm_stop_resp(nfapi_pnf_config_t* config, nfapi_nmm_stop_response_t* resp);

/*! Send a vendor extension message
 * \param config A pointer to a pnf configuraiton
 * \param msg A pointer to the vendor extention message structure
 * \param msg_len The size of the vendor extention message structure
 * \return 0 for success, -1 for failure
 * 
 */
int nfapi_pnf_vendor_extension(nfapi_pnf_config_t* config, nfapi_p4_p5_message_header_t* msg, uint32_t msg_len);

//--------------------------

/*! A subframe buffer structure which can be used by the client to 
 *  to configure the dummy information
 */
typedef struct 
{
	uint16_t sfn_sf;
	
	nfapi_dl_config_request_t* dl_config_req;
	nfapi_ul_config_request_t* ul_config_req;
	nfapi_hi_dci0_request_t* hi_dci0_req;
	nfapi_tx_request_t* tx_req;
	nfapi_lbt_dl_config_request_t* lbt_dl_config_req;
	nfapi_ue_release_request_t* ue_release_req;
} nfapi_pnf_p7_subframe_buffer_t;

typedef struct 
{
	//uint16_t sfn_slot
	uint16_t sfn;
	uint16_t slot;
	nfapi_nr_dl_tti_request_t  dl_tti_req;
	nfapi_nr_ul_tti_request_t  ul_tti_req;
	nfapi_nr_ul_dci_request_t  ul_dci_req;
	nfapi_nr_tx_data_request_t tx_data_req;

	//TODO: check these two later
	//nfapi_lbt_dl_config_request_t* lbt_dl_config_req;
	//nfapi_ue_release_request_t* ue_release_req;
} nfapi_pnf_p7_slot_buffer_t;

typedef struct nfapi_pnf_p7_config nfapi_pnf_p7_config_t;

/*! The nfapi PNF PHY P7 configuration information created using the nfapi_pnf_p7_create function
 */
typedef struct nfapi_pnf_p7_config
{
	/*! A user define callback to override the default memory allocation 
	 * \param size The size of the buffer to allocate
	 * \return An allocated buffer. 0 in the case of failure
	 * 
	 * If not set malloc will be used
	 */
	void* (*malloc)(size_t size);
	
	/*! A user define callback to override the default memory deallocation 
	 *  \param ptr Pointer to a buffer to dellocate
	 *
	 * If not set free will be used
	 */
	void (*free)(void* ptr);

	/*! A user define callback to handle trace from the pnf
	 * \param level The trace level
	 * \param message The message string
	 */
	void (*trace)(nfapi_trace_level_t  level, const char* message, ...);

	/*! The PHY id*/
	uint16_t phy_id;

	// remote
	/*! The VNF P7 UDP port */
	int remote_p7_port;
	/*! The VNF P7 UDP address */
	char* remote_p7_addr;

	// local
	/*! The PNF P7 UDP port */
	int local_p7_port;
	/*! The PNF P7 UDP address */
	char* local_p7_addr;

	/*! Flag to indicate of the pnf should use the P7 checksum */
	uint8_t checksum_enabled;

	/*! The maxium size of a P7 segement. If a message is large that this it
	 * will be segmented. Note: u32 to cover 4G and 5G */
	uint32_t segment_size;

	/*! The dummy subframe buffer structure that should be used in case there
	 * are no 'valid' subframe messages */
	nfapi_pnf_p7_subframe_buffer_t dummy_subframe;

	nfapi_pnf_p7_slot_buffer_t dummy_slot; // defining a slot equivalent for now
	
	/*! Configuration options for the p7 pack unpack functions*/
	nfapi_p7_codec_config_t codec_config;
	
	/*! Optional userdata that will be passed back in the callbacks*/
	void* user_data;

	// tdb : if these should be public
	uint16_t subframe_buffer_size;
	uint16_t slot_buffer_size;
	uint8_t timing_info_mode_periodic; // 0:false 1:true
	uint8_t timing_info_mode_aperiodic; // 0:false 1:true
	uint8_t timing_info_period; // 1..225 in subframes

	/*! A callback for the DL_CONFIG.request
	 * \param config A poiner to the PNF P7 config
	 * \param req A pointer to the dl config request message structure
	 * \return not currently used
	 */
	int (*dl_tti_req_fn)(gNB_L1_rxtx_proc_t *proc,nfapi_pnf_p7_config_t* config, nfapi_nr_dl_tti_request_t* req);
	int (*dl_config_req)(nfapi_pnf_p7_config_t* config, nfapi_dl_config_request_t* req);
	
	/*! A callback for the UL_CONFIG.request
	 * \param config A poiner to the PNF P7 config
	 * \param req A pointer to the ul config request message structure
	 * \return not currently used	
	 */
	int (*ul_tti_req_fn)(gNB_L1_rxtx_proc_t *proc,nfapi_pnf_p7_config_t* config, nfapi_nr_ul_tti_request_t* req);
	int (*ul_config_req)(nfapi_pnf_p7_config_t* config, nfapi_ul_config_request_t* req);
	
	/*! A callback for the HI_DCI0.request
	 * \param config A poiner to the PNF P7 config
	 * \param req A pointer to the hi dci0 request message structure
	 * \return not currently used
	 */
	int (*ul_dci_req_fn)(gNB_L1_rxtx_proc_t *proc,nfapi_pnf_p7_config_t* config, nfapi_nr_ul_dci_request_t* req);
	int (*hi_dci0_req)(nfapi_pnf_p7_config_t* config, nfapi_hi_dci0_request_t* req);

	/*! A callback for the TX_REQ.request
	 * \param config A poiner to the PNF P7 config
	 * \param req A pointer to the tx request message structure
	 * \return not currently used
	 * 
	 * The TX request contains pointers to the downlink PDUs to be sent. In the case that the FAPI interface
	 * will 'keep' the pointers until they are transmitted the callee should set the pointers in the req to 0
	 * and then use the p7 codec config free function to release the pdu's when appropriate. 
	 */
	int (*tx_data_req_fn)(nfapi_pnf_p7_config_t* config, nfapi_nr_tx_data_request_t* req);
	int (*tx_req)(nfapi_pnf_p7_config_t* config, nfapi_tx_request_t* req);
	
	/*! A callback for the LBT_DL_CONFIG.request
	 * \param config A poiner to the PNF P7 config
	 * \param req A pointer to the lbt dl request message structure
	 * \return not currently used
	 */
	int (*lbt_dl_config_req)(nfapi_pnf_p7_config_t* config, nfapi_lbt_dl_config_request_t* req);

    /*! A callback for the UE_RELEASE_REQ.request
     * \param config A poiner to the PNF P7 config
     * \param req A pointer to the release rnti request message structure
     * \return not currently used
     *
     * The release request contains pointers to the release rnti to be sent. In the case that the FAPI interface
     * will 'keep' the pointers until they are transmitted the callee should set the pointers in the req to 0
     * and then use the p7 codec config free function to release the rnti when appropriate.
     */
    int (*ue_release_req)(nfapi_pnf_p7_config_t* config, nfapi_ue_release_request_t* req);
	
	/*! A callback for vendor extension messages
	 * \param config A poiner to the PNF P7 config
	 * \param msg A pointer to a decode vendor extention message
	 * \return not currently used
	 */
	int (*vendor_ext)(nfapi_pnf_p7_config_t* config, void* msg);

	/*! A callback to allocate vendor extension message
	 * \param message_id The vendor extention message id from the decode message header
	 * \param msg_size A pointer to size of the allocate vendor extention message. Set by the callee
	 * \return A pointer to an allocated vendor extention message structure. 0 if failed
	 * 
	 * 
	 */
	void* (*allocate_p7_vendor_ext)(uint16_t message_id, uint16_t* msg_size);
	
	/*! A callback to deallocate vendor extension message
	 * \param header A pointer to a p7 vendor extention message
	 */
	void (*deallocate_p7_vendor_ext)(void* header);

  /*! A callback to pack a P7 message
   *
   *  This function is called internally in send_p7_msg()
   *  \param pMessageBuf A pointer to a nfapi p7 message structure
   *  \param pPackedBuf A pointer to the buffer that the p7 message will be packed into
   *  \param packedBufLen The size of the buffer
   *  \param config A pointer to the nfapi configuration structure
   *  \return != -1 means success, -1 means failure.
   */
  int (*pack_func)(void* pMessageBuf, void* pPackedBuf, uint32_t packedBufLen, nfapi_p7_codec_config_t* config);

  /*! A callback to unpack a P7 message
   *  \param pMessageBuf A pointer to an encoded P7 message header
   *  \param messageBufLen The size of the encoded P7 message header
   *  \param pUnpackedBuf A pointer to the nfapi_message_header
   *  \param unpackedBufLen The size of nfapi_message_header structure.
   *  \param config A pointer to the nfapi configuration structure
   *  \return true success, false on failure.
   */
  bool (*unpack_func)(void* pMessageBuf,
                      uint32_t messageBufLen,
                      void* pUnpackedBuf,
                      uint32_t unpackedBufLen,
                      nfapi_p7_codec_config_t* config);

  /*! \brief Decodes a NFAPI P7 message header
   *  \param pMessageBuf A pointer to an encoded P5 message header
   *  \param messageBufLen The size of the encoded P5 message header
   *  \param pUnpackedBuf A pointer to the nfapi_message_header
   *  \param unpackedBufLen The size of nfapi_message_header structure.
   *  \param config A pointer to the nfapi configuration structure
   *  \return true on success, false on failure.
   */
  bool (*hdr_unpack_func)(void* pMessageBuf,
                          uint32_t messageBufLen,
                          void* pUnpackedBuf,
                          uint32_t unpackedBufLen,
                          nfapi_p7_codec_config_t* config);

  /*! \brief Sends a (N)FAPI P7 message
   *
   *  This function internally packs the given message into a buffer by calling the previously defined pack_func() function pointer
   *  Then, having the message buffer populated, sends it to the VNF
   *  \param pnf_p7 A pointer to a pnf_p7_t struct
   *  \param header A pointer to the P7 message to pack & send
   *  \param msg_len The P7 message length
   *  \return true on success, false on failure.
   */
  bool (*send_p7_msg)(pnf_p7_t* pnf_p7, nfapi_nr_p7_message_header_t* header, uint32_t msg_len);
} nfapi_pnf_p7_config_t;

/*! Create and initialise a nfapi_pnf_p7_config structure
 * \return A pointer to a PNF P7 config structure
 */
nfapi_pnf_p7_config_t* nfapi_pnf_p7_config_create(void);

/*! Delete an nfapi_pnf_p7_config structure
 * \param config 
 */
int nfapi_pnf_p7_config_destroy(nfapi_pnf_p7_config_t* config);


/*! Start the PNF P7 library. 
 * \param config A pointer to a PNF P7 config
 * \return 0 means success, -1 means failure
 * 
 * This function will not return until nfapi_pnf_p7_stop is called.
 */
int nfapi_pnf_p7_start(nfapi_pnf_p7_config_t* config);


/*! Stop the PNF P7 library. 
 * \param config A pointer to a PNF P7 config
 * \return  0 means success, -1 means failure
 * 
 * This function will cause the nfapi_pnf_p7_start to return
 */
int nfapi_pnf_p7_stop(nfapi_pnf_p7_config_t* config);

/*! NR get stuff
 * message copied from nfapi_pnf_p7_subframe_ind
 * \param config A pointer to a PNF P7 config
 * \param phy_id The phy_id for the phy instance
 * \param sfn_sf The SFN and SF in the format of FAPI
 * \return 0 means success, -1 means failure
 * 
 * The client should call the subframe indication every 1ms. The PNF will
 * respond by invoking the pnf p7 subframe callbacks with the messages from the subframe buffer
 *
 * If messages are not in the subframe buffer, they dummy subframe messages will be sent
 */
int nfapi_pnf_p7_get_msgs(nfapi_pnf_p7_config_t* config,
                          uint16_t phy_id,
                          uint16_t sfn,
                          uint16_t slot,
                          nfapi_nr_dl_tti_request_t* ret_dl_tti,
                          nfapi_nr_ul_tti_request_t* ret_ul_tti,
                          nfapi_nr_ul_dci_request_t* ret_ul_dci,
                          nfapi_nr_tx_data_request_t* ret_tx_data);

/*! Subframe indication
 * \param config A pointer to a PNF P7 config
 * \param phy_id The phy_id for the phy instance
 * \param sfn_sf The SFN and SF in the format of FAPI
 * \return 0 means success, -1 means failure
 * 
 * The client should call the subframe indication every 1ms. The PNF will
 * respond by invoking the pnf p7 subframe callbacks with the messages from the subframe buffer
 *
 * If messages are not in the subframe buffer, they dummy subframe messages will be sent
 */
int nfapi_pnf_p7_subframe_ind(nfapi_pnf_p7_config_t* config, uint16_t phy_id, uint16_t sfn_sf);

/*! Send the HARQ.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the harq indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_harq_ind(nfapi_pnf_p7_config_t* config, nfapi_harq_indication_t* ind);

/*! Send the CRC.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the crc indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_crc_ind(nfapi_pnf_p7_config_t* config, nfapi_crc_indication_t* ind);
int nfapi_pnf_p7_nr_crc_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_crc_indication_t* ind);

/*! Send the RX.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the rx indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_rx_ind(nfapi_pnf_p7_config_t* config, nfapi_rx_indication_t* ind);
int nfapi_pnf_p7_nr_rx_data_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_rx_data_indication_t* ind);

/*! Send the RACH.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the rach indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_rach_ind(nfapi_pnf_p7_config_t* config, nfapi_rach_indication_t* ind);

/*! Send the SRS.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the srs indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_srs_ind(nfapi_pnf_p7_config_t* config, nfapi_srs_indication_t* ind);
int nfapi_pnf_p7_nr_srs_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_srs_indication_t* ind);

/*! Send the SRS ToA Vendor Extension: Introduced for positioning
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the srs toa vendor ext indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_nr_srs_toa_vendor_ext_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_srs_toa_vendor_ext_indication_t* ind);

/*! Send the SR.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the sr indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_sr_ind(nfapi_pnf_p7_config_t* config, nfapi_sr_indication_t* ind);

/*! Send the CQI.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the cqi indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_cqi_ind(nfapi_pnf_p7_config_t* config, nfapi_cqi_indication_t* ind);

/*! Send the LBT_DL.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the lbt dl indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_lbt_dl_ind(nfapi_pnf_p7_config_t* config, nfapi_lbt_dl_indication_t* ind);
int nfapi_pnf_p7_nr_uci_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_uci_indication_t* ind);

/*! Send the NB_HARQ.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the lbt dl indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_nb_harq_ind(nfapi_pnf_p7_config_t* config, nfapi_nb_harq_indication_t* ind);

/*! Send the NRACH.indication
 * \param config A pointer to a PNF P7 config
 * \param ind A pointer to the lbt dl indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_nrach_ind(nfapi_pnf_p7_config_t* config, nfapi_nrach_indication_t* ind);
int nfapi_pnf_p7_nr_rach_ind(nfapi_pnf_p7_config_t* config, const nfapi_nr_rach_indication_t* ind);


/*! Send a vendor exntesion message
 * \param config A pointer to a PNF P7 config
 * \param msg A pointer to the lbt dl indication message structure
 * \return 0 means success, -1 means failure
 */
int nfapi_pnf_p7_vendor_extension(nfapi_pnf_p7_config_t* config, nfapi_p7_message_header_t* msg);

int nfapi_pnf_ue_release_resp(nfapi_pnf_p7_config_t* config, nfapi_ue_release_response_t* resp);
#if defined(__cplusplus)
}
#endif

#endif // _NFAPI_PNF_INTERFACE_H_
