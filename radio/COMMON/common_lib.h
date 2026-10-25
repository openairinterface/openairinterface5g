/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief common APIs for different RF frontend device
 */

#ifndef COMMON_LIB_H
#define COMMON_LIB_H
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdbool.h>
#include "record_player.h"

/* default name of shared library implementing the radio front end */
#define OAI_RF_LIBNAME        "oai_device"
/* name of shared library implementing the transport */
#define OAI_TP_LIBNAME        "oai_transpro"
/* name of shared library implementing a third-party transport */
#define OAI_THIRDPARTY_TP_LIBNAME        "thirdparty_transpro"
/* name of shared library implementing the rf simulator */
#define OAI_RFSIM_LIBNAME     "rfsimulator"
/* name of shared library implementing the iq player */
#define OAI_IQPLAYER_LIBNAME  "oai_iqplayer"

/* flags for BBU to determine whether the attached radio head is local or remote */
typedef enum { RAU_LOCAL_RADIO_HEAD, RAU_REMOTE_RADIO_HEAD, RAU_REMOTE_THIRDPARTY_RADIO_HEAD } rau_type_t;

#define MAX_WRITE_THREAD_PACKAGE     10
#define MAX_WRITE_THREAD_BUFFER_SIZE 8
#define MAX_CARDS 10
#define OPENAIR0_MAX_ANTENNAS 64

typedef int64_t openair0_timestamp_t;
typedef volatile int64_t openair0_vtimestamp_t;

/*!\brief structure holds the parameters to configure USRP devices*/
typedef struct openair0_device openair0_device_t;

//#define USRP_GAIN_OFFSET (56.0)  // 86 calibrated for USRP B210 @ 2.6 GHz to get equivalent RS EPRE in OAI to SMBV100 output

typedef enum {
  max_gain=0,med_gain,byp_gain
} rx_gain_t;

typedef enum {
  duplex_mode_TDD=1,duplex_mode_FDD=0
} duplex_mode_t;


/** @addtogroup _GENERIC_PHY_RF_INTERFACE_
 * @{
 */
/*!\brief RF device types
 */
typedef enum {
  MIN_RF_DEV_TYPE = 0,
  /*!\brief device is USRP B200/B210*/
  USRP_B200_DEV,
  /*!\brief device is USRP X300/X310*/
  USRP_X300_DEV,
  /*!\brief device is USRP N300/N310*/
  USRP_N300_DEV,
  /*!\brief device is USRP X400/X410*/
  USRP_X400_DEV,
  /*!\brief device is BLADE RF*/
  BLADERF_DEV,
  /*!\brief device is LMSSDR (SoDeRa)*/
  LMSSDR_DEV,
  /*!\brief device is Iris */
  IRIS_DEV,
  /*!\brief device is NONE*/
  NONE_DEV,
  /*!\brief device is UEDv2 */
  UEDv2_DEV,
  RFSIMULATOR,
  MAX_RF_DEV_TYPE
} dev_type_t;
/* list of names of devices, needs to match dev_type_t */

/*!\brief transport protocol types
 */
typedef enum {
  MIN_TRANSP_TYPE = 0,
  /*!\brief transport protocol ETHERNET */
  ETHERNET_TP,
  /*!\brief no transport protocol*/
  NONE_TP,
  MAX_TRANSP_TYPE
} transport_type_t;

/*!\brief  openair0 device host type */
typedef enum {
  MIN_HOST_TYPE = 0,
  /*!\brief device functions within a RAU */
  RAU_HOST,
  /*!\brief device functions within a RRU */
  RRU_HOST,
  MAX_HOST_TYPE
} host_type_t;


/*! \brief RF Gain clibration */
typedef struct {
  //! Frequency for which RX chain was calibrated
  double freq;
  //! Offset to be applied to RX gain
  double offset;
} rx_gain_calib_table_t;

/*! \brief Clock source types */
typedef enum {
  //! this means the paramter has not been set
  unset=-1,
  //! This tells the underlying hardware to use the internal reference
  internal=0,
  //! This tells the underlying hardware to use the external reference
  external=1,
  //! This tells the underlying hardware to use the gpsdo reference
  gpsdo=2
} clock_source_t;

/*! \brief Radio Tx burst flags */
typedef enum {
  TX_BURST_INVALID = 0,
  TX_BURST_MIDDLE = 1,
  TX_BURST_START = 2,
  TX_BURST_END = 3,
  TX_BURST_START_AND_END = 4,
  TX_BURST_END_NO_TIME_SPEC = 10,
} radio_tx_burst_flag_t;

/*! \brief Radio TX GPIO flags: MSB to enable sending GPIO command, 12 LSB carry GPIO values */
typedef enum {
  /* first 12 bits reserved for beams */
  TX_GPIO_CHANGE = 0x1000,
} radio_tx_gpio_flag_t;

typedef enum {
  RU_GPIO_CONTROL_NONE,
  RU_GPIO_CONTROL_GENERIC,
  RU_GPIO_CONTROL_INTERDIGITAL,
} gpio_control_t;

/*! \brief defines the direction of each symbol. Int values intentional and
 * analogous to FAPI/FHI 7.2 */
typedef enum { SYMBOL_DIR_DL = 0, SYMBOL_DIR_UL = 1, SYMBOL_DIR_GU = 2 } symbol_direction_t;

/*! \brief RF frontend parameters set by application */
typedef struct openair0_config {
  //! RU ID of this device
  int ru_id;
  //! duplexing mode
  duplex_mode_t duplex_mode;
  //! number of downlink resource blocks
  int num_rb_dl;
  double sample_rate;
  //! flag to indicate that the device is doing mmapped DMA transfers
  int mmapped_dma;
  //! offset in samples between TX and RX paths
  int tx_sample_advance;
  int command_line_sample_advance;
  //! samples per packet on the fronthaul interface
  int samples_per_packet;
  //! number of RX channels (=RX antennas)
  int rx_num_channels;
  //! number of TX channels (=TX antennas)
  int tx_num_channels;
  //! number of distributed radio-units
  int num_distributed_ru;
  //! rx daughter card
  char* rx_subdev;
  //! tx daughter card
  char* tx_subdev;
  //! \brief RX base addresses for mmapped_dma
  int32_t *rxbase[OPENAIR0_MAX_ANTENNAS];
  //! \brief RX buffer size for direct access
  int rxsize;
  //! \brief TX base addresses for mmapped_dma or direct access
  int32_t *txbase[OPENAIR0_MAX_ANTENNAS];
  //! \brief Center frequency in Hz for RX.
  //! index: [0..rx_num_channels[
  double rx_freq[OPENAIR0_MAX_ANTENNAS];
  //! \brief Center frequency in Hz for TX.
  //! index: [0..rx_num_channels[ !!! see lte-ue.c:427 FIXME iterates over rx_num_channels
  double tx_freq[OPENAIR0_MAX_ANTENNAS];
  double tune_offset;
  //! \brief memory
  //! \brief Pointer to Calibration table for RX gains
  rx_gain_calib_table_t *rx_gain_calib_table;
  //! \brief Gain for RX in dB.
  //! index: [0..rx_num_channels]
  double rx_gain[OPENAIR0_MAX_ANTENNAS];
  //! \brief Gain offset (for calibration) in dB
  //! index: [0..rx_num_channels]
  double rx_gain_offset[OPENAIR0_MAX_ANTENNAS];
  //! gain for TX in dB
  double tx_gain[OPENAIR0_MAX_ANTENNAS];
  //! RX bandwidth in Hz
  double rx_bw;
  //! TX bandwidth in Hz
  double tx_bw;
  //! clock source
  clock_source_t clock_source;
  //! timing_source
  clock_source_t time_source;
  //! Manual SDR IP address
  char *sdr_addrs;
  //! Auto calibration flag
  int autocal[OPENAIR0_MAX_ANTENNAS];
  //! Configuration file for LMS7002M
  char *configFilename;
  //! record player configuration, definition in record_player.h
  uint32_t       recplay_mode;
  recplay_conf_t *recplay_conf;
  //! Flag to indicate this configuration is for NR
  int nr_flag;
  //! Core IDs for RX FH
  int rxfh_cores[OPENAIR0_MAX_ANTENNAS];
  //! Core IDs for TX FH
  int txfh_cores[OPENAIR0_MAX_ANTENNAS];
  //! select the GPIO control method
  gpio_control_t gpio_controller;
} openair0_config_t;
extern openair0_config_t openair0_cfg_g[MAX_CARDS];

/*! \brief RF mapping */
typedef struct {
  //! card id
  int card;
  //! rf chain id
  int chain;
} openair0_rf_map_t;

typedef struct {
  char *remote_addr;
  //! remote port number for Ethernet interface (control)
  uint16_t remote_portc;
  //! remote port number for Ethernet interface (user)
  uint16_t remote_portd;
  //! local IP/MAC addr for Ethernet interface (eNB/RAU, UE)
  char *my_addr;
  //! local port number (control) for Ethernet interface (eNB/RAU, UE)
  uint16_t  my_portc;
  //! local port number (user) for Ethernet interface (eNB/RAU, UE)
  uint16_t  my_portd;
  //! local Ethernet interface (eNB/RAU, UE)
  char *local_if_name;
  //! transport type preference  (RAW/UDP)
  uint8_t transp_preference;
  //! compression enable (0: No comp/ 1: A-LAW)
  uint8_t if_compress;
} eth_params_t;

typedef struct {
  //! Tx buffer for if device, keep one per subframe now to allow multithreading
  void *tx[10];
  //! Tx buffer (PRACH) for if device
  void *tx_prach;
  //! Rx buffer for if device
  void *rx;
} if_buffer_t;

typedef struct {
  openair0_timestamp_t timestamp;
  void *buff[MAX_WRITE_THREAD_BUFFER_SIZE];// buffer to be write;
  int nsamps;
  int cc;
  signed char first_packet;
  signed char last_packet;
} openair0_write_package_t;

typedef struct {
  openair0_write_package_t write_package[MAX_WRITE_THREAD_PACKAGE];
  int start;
  int end;
  /// \internal This variable is protected by \ref mutex_write
  int count_write;
  /// pthread struct for trx write thread
  pthread_t pthread_write;
  /// pthread attributes for trx write thread
  pthread_attr_t attr_write;
  /// condition varible for trx write thread
  pthread_cond_t cond_write;
  /// mutex for trx write thread
  pthread_mutex_t mutex_write;
  /// to inform the thread to exit
  bool write_thread_exit;
} openair0_thread_t;

/* TX samples of the writers (TX threads, UEs sharing one RU) summed in a ring and sent in timestamp order.
   Each nb_writers entry holds the number of writers that wrote the sample (RO_COUNT bits) and the burst
   state requested by their flags (RO_ACTIVE/RO_START/RO_END). */
#define RO_COUNT 0xffff
#define RO_ACTIVE (1 << 16)
#define RO_START (1 << 17)
#define RO_END (1 << 18)
typedef struct {
  bool initDone;
  pthread_mutex_t mutex_store;
  pthread_cond_t written; // signaled after each write
  /* writers (one per UE sharing the RU): end of their last write, and absent when a flush waited in vain for them,
     until they write again (a UE resynchronizing after a radio link failure does not write) */
  int max_writers;
  openair0_timestamp_t *writer_end;
  bool *writer_absent;
  int nb_absent;
  openair0_timestamp_t nextTS; // first sample not sent yet
  openair0_timestamp_t maxTS; // end of the most recent write
  int sz;
  int grain;
  int nb_ant;
  int *nb_writers;
  void **ring;
} re_order_t;

/*!\brief structure holds the parameters to configure RF devices */
struct openair0_device {
  /*!tx write thread*/
  openair0_thread_t write_thread;

  /*!brief Type of this device */
  dev_type_t type;

  /*!brief Transport protocol type that the device supports (in case I/Q samples need to be transported) */
  transport_type_t transp_type;

  /*!brief Type of the device's host (RAU/RRU) */
  host_type_t host_type;

  /* !brief RF frontend parameters set by application */
  openair0_config_t *openair0_cfg;

  /* !brief timestamp of first read */
  openair0_timestamp_t firstTS;

  /* !brief flag indicating that firstTS was initialized */
  bool firstTS_initialized;

  /* !brief ETH params set by application */
  eth_params_t eth_params;
  //! record player data, definition in record_player.h
  recplay_state_t *recplay_state;
  /*!brief Can be used by driver to hold internal structure*/
  void *priv;

  /* Functions API, which are called by the application*/

  /*! \brief Called to start the transceiver. Return 0 if OK, < 0 if error
      @param device pointer to the device structure specific to the RF hardware target
  */
  int (*trx_start_func)(openair0_device_t *device);

  /*! \brief Called to configure the device
       @param device pointer to the device structure specific to the RF hardware target
   */

  int (*trx_config_func)(openair0_device_t *device, openair0_config_t *openair0_cfg);

  /*! \brief Called to send a request message between RAU-RRU on control port
      @param device pointer to the device structure specific to the RF hardware target
      @param msg pointer to the message structure passed between RAU-RRU
      @param msg_len length of the message
  */
  int (*trx_ctlsend_func)(openair0_device_t *device, void *msg, ssize_t msg_len);

  /*! \brief Called to receive a reply  message between RAU-RRU on control port
      @param device pointer to the device structure specific to the RF hardware target
      @param msg pointer to the message structure passed between RAU-RRU
      @param msg_len length of the message
  */
  int (*trx_ctlrecv_func)(openair0_device_t *device, void *msg, ssize_t msg_len);

  /*! \brief Called to send samples to the RF target
      @param device pointer to the device structure specific to the RF hardware target
      @param timestamp The timestamp at whicch the first sample MUST be sent
      @param buff Buffer which holds the samples (2 dimensional)
      @param nsamps number of samples to be sent
      @param nb_antennas_tx number of antennas
      @param flags flags must be set to true if timestamp parameter needs to be applied
  */
  int (*trx_write_func)(openair0_device_t *device,
                        openair0_timestamp_t timestamp,
                        void **buff,
                        int nsamps,
                        int nb_antennas_tx,
                        int flags);

  /*! \brief Called to send samples to the RF target
      @param device pointer to the device structure specific to the RF hardware target
      @param timestamp The timestamp at whicch the first sample MUST be sent
      @param buff Buffer which holds the samples (1 dimensional)
      @param nsamps number of samples to be sent
      @param flags flags must be set to true if timestamp parameter needs to be applied
  */
  int (*trx_write_func2)(openair0_device_t *device,
                         openair0_timestamp_t timestamp,
                         void **buff,
                         int fd_ind,
                         int nsamps,
                         int flags,
                         int nant);

  /*! \brief Receive samples from hardware.
   * Read nsamps samples from each channel to buffers. buff[0] is the array for
   * the first channel. *ptimestamp is the time at which the first sample
   * was received.
   * \param device the hardware to use
   * \param[out] ptimestamp the time at which the first sample was received.
   * \param[out] buff An array of pointers to buffers for received samples. The buffers must be large enough to hold the number of
   * samples nsamps. \param nsamps Number of samples. One sample is 2 byte I + 2 byte Q => 4 byte. \param num_antennas number of
   * antennas from which to receive samples \returns the number of sample read
   */

  int (*trx_read_func)(openair0_device_t *device, openair0_timestamp_t *ptimestamp, void **buff, int nsamps, int num_antennas);

  /*! \brief Receive samples from hardware, this version provides a single antenna at a time and returns.
   * Read nsamps samples from each channel to buffers. buff[0] is the array for
   * the first channel. *ptimestamp is the time at which the first sample
   * was received.
   * \param device the hardware to use
   * \param[out] ptimestamp the time at which the first sample was received.
   * \param[out] buff A pointer to a buffer[ant_id][] for received samples. The buffer[ant_id] must be large enough to hold the
   * number of samples nsamps * the number of packets. \param nsamps Number of samples. One sample is 2 byte I + 2 byte Q => 4 byte.
   * \param packet_idx offset into
   * \param antenna_id Index of antenna from which samples were received
   * \returns the number of sample read
   */
  int (*trx_read_func2)(openair0_device_t *device, openair0_timestamp_t *ptimestamp, uint32_t **buff, int nsamps);

  /*! \brief print the device statistics
   * \param device the hardware to use
   * \returns  0 on success
   */
  int (*trx_get_stats_func)(openair0_device_t *device);

  /*! \brief Reset device statistics
   * \param device the hardware to use
   * \returns 0 in success
   */
  int (*trx_reset_stats_func)(openair0_device_t *device);

  /*! \brief Terminate operation of the transceiver -- free all associated resources
   * \param device the hardware to use
   */
  void (*trx_end_func)(openair0_device_t *device);

  /*! \brief Stop operation of the transceiver
   */
  int (*trx_stop_func)(openair0_device_t *device);

  /*! \brief Get timestamp from timespec
  */
  openair0_timestamp_t (*get_timestamp)(openair0_device_t *device, struct timespec *ts);

  /* Functions API related to UE*/

  /*! \brief Set RX feaquencies
   * \param device the hardware to use
   * \param openair0_cfg RF frontend parameters set by application
   * \returns 0 in success
   */
  int (*trx_set_freq_func)(openair0_device_t *device, openair0_config_t *openair0_cfg);

  /*! \brief Set gains
   * \param device the hardware to use
   * \param openair0_cfg RF frontend parameters set by application
   * \returns 0 in success
   */
  int (*trx_set_gains_func)(openair0_device_t *device, openair0_config_t *openair0_cfg);

  /*! \brief Set tx/rx beams
   *
   * Set the tx/rx beams. This has to be done in advance of the reception in order to
   * allow the underlying device to change receiver configuration. The exact time depends
   * on the device.
   *
   * NOTICE: the samples returned from trx_read_func may belong to more than one beam. It is up
   * to the application to determine the beam of the received samples.
   *
   * \param device the hardware to use
   * \param beams pointer to array of beam ids
   * \param num_beams number of beams. Expected to be equal to number of antennas
   * \return 0 on success
   */
  int (*trx_set_beams)(openair0_device_t *device, uint16_t *beams, int num_beams, openair0_timestamp_t timestamp);

  /*! \brief RRU Configuration callback
   * \param idx RU index
   * \param arg pointer to capabilities or configuration
   */
  void (*configure_rru)(void *, void *arg);

  /*! \brief Pointer to generic RRU private information
   */

  void *thirdparty_priv;

  /*! \brief Callback for Third-party RRU Initialization routine
     \param device the hardware configuration to use
   */
  int (*thirdparty_init)(openair0_device_t *device);
  /*! \brief Callback for Third-party RRU Cleanup routine
     \param device the hardware configuration to use
   */
  int (*thirdparty_cleanup)(openair0_device_t *device);

  /*! \brief Callback for Third-party start streaming routine
     \param device the hardware configuration to use
   */
  int (*thirdparty_startstreaming)(openair0_device_t *device);

  /*! \brief RRU Configuration callback
   * \param idx RU index
   * \param arg pointer to capabilities or configuration
   */
  int (*trx_write_init)(openair0_device_t *device);
  /* \brief Get internal parameter
   * \param id parameter to get
   * \return a pointer to the parameter
   */
  void *(*get_internal_parameter)(char *id);
  /* \brief timing statistics for TX fronthaul (ethernet)
   */
  re_order_t reOrder;
};

typedef struct {
  uint32_t size;           // Number of samples per antenna to follow this header
  uint32_t nbAnt;          // Total number of antennas following this header
  // Samples per antenna follow this header,
  // i.e. nbAnt = 2 => this header+samples_antenna_0+samples_antenna_1
  // data following this header in bytes is nbAnt*size*sizeof(sample_t)
  uint64_t timestamp;      // Timestamp value of first sample
  uint32_t option_value;   // Option value
  uint32_t option_flag;    // Option flag
} samplesBlockHeader_t;

#ifdef __cplusplus
extern "C"
{
#endif

int load_lib(openair0_device_t *device, openair0_config_t *openair0_cfg, rau_type_t rau_type);
typedef struct PHY_VARS_NR_UE_s PHY_VARS_NR_UE;
typedef int (*nrue_ru_write_t)(PHY_VARS_NR_UE *UE, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags);

/// writer: index of the writer, in [0, nb_writers)
int openair0_write_reorder_common(nrue_ru_write_t nrue_ru_write,
                                  PHY_VARS_NR_UE *UE,
                                  openair0_device_t *device,
                                  openair0_timestamp_t timestamp,
                                  void **txp,
                                  int nsamps,
                                  int writer,
                                  int nb_writers,
                                  int nbAnt,
                                  int flags);

/*! \brief get device name from device type */
const char *get_devname(int devtype);
/*! \brief Initialize openair RF target. It returns 0 if OK */
int openair0_device_load(openair0_device_t *device, openair0_config_t *openair0_cfg);
/*! \brief Initialize transport protocol . It returns 0 if OK */
int openair0_transport_load(openair0_device_t *device, openair0_config_t *openair0_cfg);

/*! \brief Load the configured device library and return the address of symbol fname */
void *load_transport_shlib(char *fname);

/*! \brief Set RX frequencies
 * \param device the hardware to use
 * \param openair0_cfg RF frontend parameters set by application
 * \returns 0 in success
 */
int openair0_set_rx_frequencies(openair0_device_t *device, openair0_config_t *openair0_cfg);
/*! \brief read the iq record-player configuration */
extern int read_recplayconfig(recplay_conf_t **recplay_conf, recplay_state_t **recplay_state);

/*! \brief store recorded iqs from memory to file. */
extern void iqrecorder_end(openair0_device_t *device);

void openair0_write_reorder_init(re_order_t *r, const openair0_config_t *openair0_cfg);
int openair0_write_reorder(openair0_device_t *device, openair0_timestamp_t timestamp, void **txp, int nsamps, int nbAnt, int flags);
void openair0_write_reorder_clear_context(openair0_device_t *device);
void openair0_write_reorder_flush_common(nrue_ru_write_t nrue_ru_write,
                                         PHY_VARS_NR_UE *UE,
                                         openair0_device_t *device,
                                         int nb_writers,
                                         int nbAnt,
                                         openair0_timestamp_t force_until,
                                         int wait_ms);

/// number of elements of a ring of at least nb elements of elem_size bytes, a whole number of pages
int ring_elements(int nb, int elem_size);
void *create_ring(int sz_bytes);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif // COMMON_LIB_H

