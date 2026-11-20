/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "socket_pnf.h"
#include "nfapi.h"
static nfapi_pnf_config_t *config;
void socket_nfapi_nr_pnf_stop()
{
  AssertFatal(config->nr_stop_req, "STOP.request Handler should be set!");
  nfapi_nr_stop_request_scf_t req = {0};
  req.header.phy_id = 0;
  req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_STOP_REQUEST;
  config->nr_stop_req(config, 0, &req);
}

bool pnf_nr_send_p5_message(pnf_t *pnf, nfapi_nr_p4_p5_message_header_t *msg, uint32_t msg_len)
{
  int packed_len =
      pnf->_public.pack_func(msg, msg_len, pnf->tx_message_buffer, sizeof(pnf->tx_message_buffer), &pnf->_public.codec_config);

  if (packed_len < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "p5_message_pack failed (%d)\n", packed_len);
    return false;
  }

  return socket_send_p5_msg(pnf->sctp, pnf->p5_sock, NULL, pnf->tx_message_buffer, packed_len, 0) == packed_len;
}

static int send_p7_msg(pnf_p7_t *pnf_p7, uint8_t *msg, uint32_t len)
{
  // todo : consider how to do this only once
  struct sockaddr_in remote_addr = {.sin_family = AF_INET, .sin_port = htons(pnf_p7->_public.remote_p7_port)};

  if (inet_aton(pnf_p7->_public.remote_p7_addr, &remote_addr.sin_addr) == -1) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "inet_aton failed %d\n", errno);
    return -1;
  }

  socklen_t remote_addr_len = sizeof(struct sockaddr_in);

  int sendto_result = socket_send_p7_msg((int)pnf_p7->p7_sock, &remote_addr, msg, len);
  if (sendto_result < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR,
                "%s %s:%d sendto(%d, %p, %d) %d failed errno: %d\n",
                __FUNCTION__,
                pnf_p7->_public.remote_p7_addr,
                pnf_p7->_public.remote_p7_port,
                (int)pnf_p7->p7_sock,
                (const char *)msg,
                len,
                remote_addr_len,
                errno);
    return -1;
  }

  if (sendto_result != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s sendto failed to send the entire message %d %d\n", __FUNCTION__, sendto_result, len);
  }
  return 0;
}

bool pnf_nr_send_p7_message(pnf_p7_t *pnf_p7, nfapi_nr_p7_message_header_t *header, uint32_t msg_len)
{
  UNUSED(msg_len);
  if (pnf_p7->terminate) {
    //don't send any further P7 messages after terminating
    return false;
  }
  header->m_segment_sequence = NFAPI_NR_P7_SET_MSS(0, 0, pnf_p7->sequence_number);

  // Need to guard against different threads calling the encode function at the same time
  if (pthread_mutex_lock(&(pnf_p7->pack_mutex)) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
    return false;
  }

  uint8_t tx_buf[131072]; // four times NFAPI_MAX_PACKED_MESSAGE_SIZE as of this commit
  int len = pnf_p7->_public.pack_func(header, tx_buf, sizeof(tx_buf), &pnf_p7->_public.codec_config);

  if (len < 0) {
    if (pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
      return false;
    }

    NFAPI_TRACE(NFAPI_TRACE_ERROR, "nfapi_p7_message_pack failed with return %d\n", len);
    return false;
  }

  if (len > pnf_p7->_public.segment_size) {
    int msg_body_len = len - NFAPI_NR_P7_HEADER_LENGTH;
    int seg_body_len = pnf_p7->_public.segment_size - NFAPI_NR_P7_HEADER_LENGTH;
    int segment_count = (msg_body_len / (seg_body_len)) + ((msg_body_len % seg_body_len) ? 1 : 0);

    int segment = 0;
    int offset = NFAPI_NR_P7_HEADER_LENGTH;
    uint8_t buffer[pnf_p7->_public.segment_size];
    for (segment = 0; segment < segment_count; ++segment) {
      uint8_t last = 0;
      uint32_t size = pnf_p7->_public.segment_size - NFAPI_NR_P7_HEADER_LENGTH;
      if (segment + 1 == segment_count) {
        last = 1;
        size = (msg_body_len) - (seg_body_len * segment);
      }

      uint32_t segment_size = size + NFAPI_NR_P7_HEADER_LENGTH;

      // Update the header with the m and segement
      memcpy(&buffer[0], tx_buf, NFAPI_NR_P7_HEADER_LENGTH);

      // set the segment length , update and push m_segment_sequence
      uint8_t *buf_ptr = &buffer[4];
      uint8_t *buf_ptr_end = &buffer[10];
      push32(segment_size, (&buf_ptr), buf_ptr_end);
      header->m_segment_sequence = NFAPI_NR_P7_SET_MSS((!last), segment, pnf_p7->sequence_number);
      push16(header->m_segment_sequence, (&buf_ptr), buf_ptr_end);
      memcpy(&buffer[NFAPI_NR_P7_HEADER_LENGTH], tx_buf + offset, size);
      offset += size;

      if (pnf_p7->_public.checksum_enabled) {
        nfapi_nr_p7_update_checksum(buffer, segment_size);
      }

      send_p7_msg(pnf_p7, &buffer[0], segment_size);
    }
  } else {
    if (pnf_p7->_public.checksum_enabled) {
      nfapi_nr_p7_update_checksum(tx_buf, len);
    }

    // simple case that the message fits in a single segment
    send_p7_msg(pnf_p7, tx_buf, len);
  }

  pnf_p7->sequence_number++;

  if (pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
    return false;
  }

  return true;
}

void *pnf_start_p5_thread(void *ptr)
{
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[PNF] IN PNF NFAPI start thread %s\n", __FUNCTION__);
  config = (nfapi_pnf_config_t *)ptr;
  struct sched_param sp;
  sp.sched_priority = 20;
  pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

  // Verify that config is not null
  if (config == 0)
    return (void *)-1;

  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);

  pnf_t *_this = (pnf_t *)(config);

  while (_this->terminate == 0) {
    int connect_result = pnf_connect_socket(_this);

    if (connect_result > 0) {
      pnf_nr_message_pump(_this);
    } else if (connect_result < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s() Error connecting to P5 socket\n", __FUNCTION__);
      return 0;
    }

    sleep(1);
  }
  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() terminate=1 - EXITTING............\n", __FUNCTION__);

  return 0;
}

int pnf_connect_socket(pnf_t *pnf)
{
  uint8_t socketConnected = 0;

  if (pnf->_public.vnf_ip_addr == 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "vnfIpAddress is null\n");
    return -1;
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "Starting P5 PNF connection to VNF at %s:%u\n", pnf->_public.vnf_ip_addr, pnf->_public.vnf_p5_port);

  // todo split the vnf address list. currently only supporting 1

  struct addrinfo hints = {0}, *servinfo;

  hints.ai_socktype = SOCK_STREAM; // For SCTP we are only interested in SOCK_STREAM
  // todo : allow the client to restrict IPV4 or IPV6
  // todo : randomize which address to connect to?

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", pnf->_public.vnf_p5_port);
  if (getaddrinfo(pnf->_public.vnf_ip_addr, port_str, &hints, &servinfo) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to get host (%s) addr info h_errno:%d \n", pnf->_public.vnf_ip_addr, h_errno);
    return -1;
  }

  struct addrinfo *p = servinfo;
  int connected = 0;

  while (p != NULL && connected == 0) {
#ifdef NFAPI_TRACE_ENABLED
    char *family = "Unknown";
    char *address = "Unknown";
    char _addr[128];

    if (p->ai_family == AF_INET6) {
      family = "IPV6";
      struct sockaddr_in6 *addr = (struct sockaddr_in6 *)p->ai_addr;
      inet_ntop(AF_INET6, &addr->sin6_addr, _addr, sizeof(_addr));
      address = &_addr[0];
    } else if (p->ai_family == AF_INET) {
      family = "IPV4";
      struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
      address = inet_ntoa(addr->sin_addr);
    }

    NFAPI_TRACE(NFAPI_TRACE_NOTE, "Host address info  %d Family:%s Address:%s\n", i++, family, address);
#endif

    if (pnf->sctp) {
      // open the SCTP socket
      if ((pnf->p5_sock = socket(p->ai_family, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P5 socket errno: %d\n", errno);
        freeaddrinfo(servinfo);
        return -1;
      }
      int noDelay;
      struct sctp_initmsg initMsg = {0};

      // configure the socket options
      NFAPI_TRACE(NFAPI_TRACE_NOTE, "PNF Setting the SCTP_INITMSG\n");
      initMsg.sinit_num_ostreams = 5; // MAX_SCTP_STREAMS;  // number of output streams can be greater
      initMsg.sinit_max_instreams = 5; // MAX_SCTP_STREAMS;  // number of output streams can be greater
      if (setsockopt(pnf->p5_sock, IPPROTO_SCTP, SCTP_INITMSG, &initMsg, sizeof(initMsg)) < 0) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt errno: %d\n", errno);
        freeaddrinfo(servinfo);
        return -1;
      }
      noDelay = 1;
      if (setsockopt(pnf->p5_sock, IPPROTO_SCTP, SCTP_NODELAY, &noDelay, sizeof(noDelay)) < 0) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt errno: %d\n", errno);
        freeaddrinfo(servinfo);
        return -1;
      }

      struct sctp_event_subscribe events = {0};
      events.sctp_data_io_event = 1;

      if (setsockopt(pnf->p5_sock, SOL_SCTP, SCTP_EVENTS, (const void *)&events, sizeof(events)) < 0) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt errno: %d\n", errno);
        freeaddrinfo(servinfo);
        return -1;
      }
    } else {
      // Create an IP socket point
      if ((pnf->p5_sock = socket(p->ai_family, SOCK_STREAM, IPPROTO_IP)) < 0) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P5 socket errno: %d\n", errno);
        freeaddrinfo(servinfo);
        return -1;
      }
    }

    NFAPI_TRACE(NFAPI_TRACE_INFO, "P5 socket created...\n");

    if (connect(pnf->p5_sock, p->ai_addr, p->ai_addrlen) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR,
                  "After connect (address:%s port:%d) errno: %d\n",
                  pnf->_public.vnf_ip_addr,
                  pnf->_public.vnf_p5_port,
                  errno);

      if (errno == EINVAL) {
        freeaddrinfo(servinfo);
        return -1;
      }
      if (pnf->terminate != 0) {
        freeaddrinfo(servinfo);
        return 0;
      }
      close(pnf->p5_sock);
      sleep(1);
    } else {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "connect succeeded...\n");

      connected = 1;
    }

    p = p->ai_next;
  }

  freeaddrinfo(servinfo);

  // If we have failed to connect return 0 and it is retry
  if (connected == 0)
    return 0;

  NFAPI_TRACE(NFAPI_TRACE_NOTE, "After connect loop\n");
  if (pnf->sctp) {
    socklen_t optLen;
    struct sctp_status status = {0};

    // check the connection status
    optLen = (socklen_t)sizeof(struct sctp_status);
    if (getsockopt(pnf->p5_sock, IPPROTO_SCTP, SCTP_STATUS, &status, &optLen) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After getsockopt errno: %d\n", errno);
      return -1;
    } else {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "Association ID = %d\n", status.sstat_assoc_id);
      NFAPI_TRACE(NFAPI_TRACE_INFO, "Receiver window size = %d\n", status.sstat_rwnd);
      NFAPI_TRACE(NFAPI_TRACE_INFO, "In Streams = %d\n", status.sstat_instrms);
      NFAPI_TRACE(NFAPI_TRACE_INFO, "Out Streams = %d\n", status.sstat_outstrms);

      socketConnected = 1;
    }
  } else {
    int error;
    socklen_t len = sizeof(error);

    if (getsockopt(pnf->p5_sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After getsockopt errno: %d\n", errno);
      return -1;
    } else {
      // If error is zero, the socket is connected
      if (error == 0) {
        socketConnected = 1;
      }
    }
  }

  NFAPI_TRACE(NFAPI_TRACE_NOTE, "Socket %s\n", socketConnected ? "CONNECTED" : "NOT_CONNECTED");
  return socketConnected;
}

static int pnf_nr_read_dispatch_message(pnf_t *pnf)
{
  int socket_connected = 1;

  // 1. Peek the message header
  // 2. If the message is larger than the stack buffer, then create a dynamic buffer
  // 3. Read the buffer
  // 4. Handle the p5 message

  uint32_t header_buffer_size = NFAPI_NR_P5_HEADER_LENGTH;
  uint8_t header_buffer[header_buffer_size];

  uint32_t stack_buffer_size = 32; // should it be the size of then sctp_notificatoin structure
  uint8_t stack_buffer[stack_buffer_size];

  uint8_t *dynamic_buffer = 0;

  uint8_t *read_buffer = &stack_buffer[0];
  uint32_t message_size = 0;

  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);

  struct sctp_sndrcvinfo sndrcvinfo = {0};

  {
    int flags = MSG_PEEK;
    if (pnf->sctp) {
      message_size = sctp_recvmsg(pnf->p5_sock,
                                  header_buffer,
                                  header_buffer_size,
                                  /*(struct sockaddr*)&addr, &addr_len*/
                                  0,
                                  0,
                                  &sndrcvinfo,
                                  &flags);
    } else {
      message_size = recv(pnf->p5_sock, header_buffer, header_buffer_size, flags);
    }

    if (message_size < NFAPI_NR_P5_HEADER_LENGTH) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF Failed to peek sctp message size errno:%d\n", errno);
      return 0;
    }

    nfapi_nr_p4_p5_message_header_t header;
    const bool result = pnf->_public.hdr_unpack_func(header_buffer, header_buffer_size, &header, sizeof(header), 0);
    if (!result) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF Failed to unpack p5 message header\n");
      return 0;
    }
    message_size = header.message_length + header_buffer_size;

    // now have the size of the mesage
  }

  if (message_size > stack_buffer_size) {
    dynamic_buffer = (uint8_t *)malloc(message_size);

    if (dynamic_buffer == NULL) {
      // todo : add error mesage
      NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF Failed to allocate dynamic buffer for sctp_recvmsg size:%d\n", message_size);
      return -1;
    }

    read_buffer = dynamic_buffer;
  }

  {
    int flags = 0;

    ssize_t recvmsg_result = 0;
    if (pnf->sctp) {
      recvmsg_result =
          sctp_recvmsg(pnf->p5_sock, read_buffer, message_size, (struct sockaddr *)&addr, &addr_len, &sndrcvinfo, &flags);
    } else {
      if ((recvmsg_result = recv(pnf->p5_sock, read_buffer, message_size, flags)) <= 0) {
        recvmsg_result = -1;
      }
    }
    if (recvmsg_result == -1) {
      int tmp = errno;
      NFAPI_TRACE(NFAPI_TRACE_INFO, "Failed to read sctp message size error %s errno:%d\n", strerror(tmp), tmp);
    } else {
#if 0
			// print the received message
			printf("\n MESSAGE RECEIVED: \n");
			for(int i=0; i<message_size; i++){
				printf("read_buffer[%d] = 0x%02x\n",i, read_buffer[i]);
			}
			printf("\n");
#endif

      if (flags & MSG_NOTIFICATION) {
        NFAPI_TRACE(NFAPI_TRACE_INFO, "Notification received from %s:%u\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

        // todo - handle the events
      } else {
        /*
        NFAPI_TRACE(NFAPI_TRACE_INFO, "Received message fd:%d from %s:%u assoc:%d on stream %d, PPID %d, length %d, flags 0x%x\n",
            pnf->p5_sock,
            inet_ntoa(addr.sin_addr),
            ntohs(addr.sin_port),
            sndrcvinfo.sinfo_assoc_id,
            sndrcvinfo.sinfo_stream,
            ntohl(sndrcvinfo.sinfo_ppid),
            message_size,
            flags);
        */

        // handle now if complete message in one or more segments
        if ((flags & 0x80) == 0x80 || !pnf->sctp) {
          pnf_nr_handle_p5_message(pnf, read_buffer, message_size);
        } else {
          int tmp = errno;
          NFAPI_TRACE(NFAPI_TRACE_WARN,
                      "sctp_recvmsg: unhandled mode with flags 0x%x and error %s errno:%d\n",
                      flags,
                      strerror(tmp),
                      tmp);
          // assume socket disconnected
          NFAPI_TRACE(NFAPI_TRACE_WARN, "Disconnected socket\n");
          socket_connected = 0;
        }
      }
    }
  }

  if (dynamic_buffer) {
    free(dynamic_buffer);
  }

  return socket_connected;
}

int pnf_nr_message_pump(pnf_t *pnf)
{
  uint8_t socketConnected = 1;

  while (socketConnected && pnf->terminate == 0) {
    fd_set rfds;
    int selectRetval = 0;

    // select on a timeout and then get the message
    FD_ZERO(&rfds);
    FD_SET(pnf->p5_sock, &rfds);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    selectRetval = select(pnf->p5_sock + 1, &rfds, NULL, NULL, &timeout);

    if (selectRetval == 0) {
      // timeout
      continue;
    } else if (selectRetval == -1 && (errno == EINTR)) {
      // interrupted by signal
      NFAPI_TRACE(NFAPI_TRACE_WARN, "P5 Signal Interrupt %d\n", errno);
      continue;
    } else if (selectRetval == -1) {
      NFAPI_TRACE(NFAPI_TRACE_WARN, "P5 select() failed\n");
      sleep(1);
      continue;
    }

    if (FD_ISSET(pnf->p5_sock, &rfds)) {
      socketConnected = pnf_nr_read_dispatch_message(pnf);
    } else {
      NFAPI_TRACE(NFAPI_TRACE_WARN, "Why are we here\n");
    }
  }

  // Drop back to idle if we have lost connection
  pnf->_public.state = NFAPI_PNF_IDLE;

  // close the connection and socket
  if (close(pnf->p5_sock) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "close(sctpSock) failed errno: %d\n", errno);
  }

  return 0;
}

static void pnf_nr_reassemble_nfapi_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t *pnf_p7, uint32_t rx_hr_time)
{
  nfapi_nr_p7_message_header_t messageHeader;

  // validate the input params
  if (pRecvMsg == NULL || recvMsgLen < 4 || pnf_p7 == NULL) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "pnf_handle_p7_message: invalid input params (%p %d %p)\n", pRecvMsg, recvMsgLen, pnf_p7);
    return;
  }

  // unpack the message header
  const bool result =
      pnf_p7->_public.hdr_unpack_func(pRecvMsg, recvMsgLen, &messageHeader, sizeof(messageHeader), &pnf_p7->_public.codec_config);
  if (!result) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
    return;
  }

  uint8_t m = NFAPI_P7_GET_MORE(messageHeader.m_segment_sequence);
  uint8_t sequence_num = NFAPI_P7_GET_SEQUENCE(messageHeader.m_segment_sequence);
  uint8_t segment_num = NFAPI_P7_GET_SEGMENT(messageHeader.m_segment_sequence);

  if (pnf_p7->_public.checksum_enabled) {
    uint32_t checksum = nfapi_nr_p7_calculate_checksum(pRecvMsg, recvMsgLen);
    if (checksum != messageHeader.checksum) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "Checksum verification failed %d %d\n", checksum, messageHeader.checksum);
      return;
    }
  }

  if (m == 0 && segment_num == 0) {
    // we have a complete message
    // ensure the message is sensible
    if (recvMsgLen < 8 || pRecvMsg == NULL) {
      NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
      return;
    }

    pnf_nr_handle_p7_message(pRecvMsg, recvMsgLen, pnf_p7, rx_hr_time);
  } else {
    pnf_p7_rx_message_t *rx_msg = pnf_p7_rx_reassembly_queue_add_segment(pnf_p7,
                                                                         &(pnf_p7->reassembly_queue),
                                                                         rx_hr_time,
                                                                         sequence_num,
                                                                         segment_num,
                                                                         m,
                                                                         pRecvMsg,
                                                                         recvMsgLen);

    if (rx_msg->num_segments_received == rx_msg->num_segments_expected) {
      // send the buffer on
      uint16_t i = 0;
      uint32_t length = 0;
      for (i = 0; i < rx_msg->num_segments_expected; ++i) {
        length += rx_msg->segments[i].length - (i > 0 ? NFAPI_NR_P7_HEADER_LENGTH : 0);
      }

      if (pnf_p7->reassemby_buffer_size < length) {
        pnf_p7_free(pnf_p7, pnf_p7->reassemby_buffer);
        pnf_p7->reassemby_buffer = 0;
      }

      if (pnf_p7->reassemby_buffer == 0) {
        NFAPI_TRACE(NFAPI_TRACE_NOTE, "Resizing PNF_P7 Reassembly buffer %d->%d\n", pnf_p7->reassemby_buffer_size, length);
        pnf_p7->reassemby_buffer = (uint8_t *)pnf_p7_malloc(pnf_p7, length);

        if (pnf_p7->reassemby_buffer == 0) {
          NFAPI_TRACE(NFAPI_TRACE_NOTE, "Failed to allocate PNF_P7 reassemby buffer len:%d\n", length);
          return;
        }
        memset(pnf_p7->reassemby_buffer, 0, length);
        pnf_p7->reassemby_buffer_size = length;
      }

      uint32_t offset = 0;
      for (i = 0; i < rx_msg->num_segments_expected; ++i) {
        if (i == 0) {
          memcpy(pnf_p7->reassemby_buffer, rx_msg->segments[i].buffer, rx_msg->segments[i].length);
          offset += rx_msg->segments[i].length;
        } else {
          memcpy(pnf_p7->reassemby_buffer + offset,
                 rx_msg->segments[i].buffer + NFAPI_NR_P7_HEADER_LENGTH,
                 rx_msg->segments[i].length - NFAPI_NR_P7_HEADER_LENGTH);
          offset += rx_msg->segments[i].length - NFAPI_NR_P7_HEADER_LENGTH;
        }
      }

      pnf_nr_handle_p7_message(pnf_p7->reassemby_buffer, length, pnf_p7, rx_msg->rx_hr_time);

      // delete the structure
      pnf_p7_rx_reassembly_queue_remove_msg(pnf_p7, &(pnf_p7->reassembly_queue), rx_msg);
    }
  }

  // The timeout used to be 1000, i.e., 1ms, which is too short. The below 10ms
  // is selected to be able to encompass any "reasonable" slot ahead time for the VNF.
  // Ideally, we would remove old msg (segments) if we detect packet loss
  // (e.g., if the sequence numbers advances sufficiently); in the branch of
  // this commit, our goal is to make the PNF work, so we content ourselves to
  // just remove very old messages.
  pnf_p7_rx_reassembly_queue_remove_old_msgs(pnf_p7, &(pnf_p7->reassembly_queue), rx_hr_time, 10000);
}

static void pnf_nr_nfapi_p7_read_dispatch_message(pnf_p7_t *pnf_p7, uint32_t now_hr_time)
{
  int recvfrom_result = 0;
  struct sockaddr_in remote_addr;
  socklen_t remote_addr_size = sizeof(remote_addr);
  remote_addr.sin_family = 2; // hardcoded
  do {
    // peek the header
    uint8_t header_buffer[NFAPI_NR_P7_HEADER_LENGTH];
    recvfrom_result = recvfrom(pnf_p7->p7_sock,
                               header_buffer,
                               NFAPI_NR_P7_HEADER_LENGTH,
                               MSG_DONTWAIT | MSG_PEEK,
                               (struct sockaddr *)&remote_addr,
                               &remote_addr_size);
    if (recvfrom_result > 0) {
      // get the segment size
      nfapi_nr_p7_message_header_t header;
      pnf_p7->_public.hdr_unpack_func(header_buffer, NFAPI_NR_P7_HEADER_LENGTH, &header, 34, 0);

      // resize the buffer if we have a large segment
      if (header.message_length > pnf_p7->rx_message_buffer_size) {
        uint32_t newsize = pnf_p7->rx_message_buffer_size ? pnf_p7->rx_message_buffer_size * 2 : 65536;
        NFAPI_TRACE(NFAPI_TRACE_NOTE, "reallocing rx buffer %d (msg %d)\n", newsize, header.message_length);
        pnf_p7->rx_message_buffer = realloc(pnf_p7->rx_message_buffer, newsize);
        DevAssert(pnf_p7->rx_message_buffer);
        pnf_p7->rx_message_buffer_size = newsize;
      }

      // read the segment
      recvfrom_result = recvfrom(pnf_p7->p7_sock,
                                 pnf_p7->rx_message_buffer,
                                 header.message_length,
                                 MSG_DONTWAIT,
                                 (struct sockaddr *)&remote_addr,
                                 &remote_addr_size);

      now_hr_time = pnf_get_current_time_hr(); // moved to here - get closer timestamp???

      if (recvfrom_result > 0) {
        pnf_nr_reassemble_nfapi_p7_message(pnf_p7->rx_message_buffer, recvfrom_result, pnf_p7, now_hr_time);
        // printf("\npnf_handle_p7_message sfn=%d,slot=%d\n",pnf_p7->sfn,pnf_p7->slot);
      }
    } else if (recvfrom_result == 0) {
      // recv zero length message
      recvfrom_result =
          recvfrom(pnf_p7->p7_sock, header_buffer, 0, MSG_DONTWAIT, (struct sockaddr *)&remote_addr, &remote_addr_size);
    }

    if (recvfrom_result == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // return to the select
        // NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom would block :%d\n", __FUNCTION__, errno);
      } else {
        NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom failed errno:%d\n", __FUNCTION__, errno);
      }
    }

    // need to update the time as we would only use the value from the
    // select
  } while (recvfrom_result > 0);
}

int pnf_nr_p7_message_pump(pnf_p7_t *pnf_p7)
{
  // initialize the mutex lock
  if (pthread_mutex_init(&(pnf_p7->mutex), NULL) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
    return -1;
  }

  if (pthread_mutex_init(&(pnf_p7->pack_mutex), NULL) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
    return -1;
  }

  // create the pnf p7 socket
  if ((pnf_p7->p7_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 socket errno: %d\n", errno);
    return -1;
  }
  NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 socket created (%d)...\n", pnf_p7->p7_sock);

  // configure the UDP socket options
  int reuseaddr_enable = 1;
  if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_enable, sizeof(int)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEADDR) failed  errno: %d\n", errno);
    return -1;
  }

  /*
    int reuseport_enable = 1;
    if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEPORT, &reuseport_enable, sizeof(int)) < 0)
    {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEPORT) failed  errno: %d\n", errno);
      return -1;
    }
  */

  int iptos_value = 0;
  if (setsockopt(pnf_p7->p7_sock, IPPROTO_IP, IP_TOS, &iptos_value, sizeof(iptos_value)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (IPPROTO_IP, IP_TOS) failed errno: %d\n", errno);
    return -1;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(pnf_p7->_public.local_p7_port);

  if (pnf_p7->_public.local_p7_addr == 0) {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    // addr.sin_addr.s_addr = inet_addr(pnf_p7->_public.local_p7_addr);
    if (inet_aton(pnf_p7->_public.local_p7_addr, &addr.sin_addr) == -1) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "inet_aton failed\n");
    }
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 binding %d too %s:%d\n", pnf_p7->p7_sock, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
  if (bind(pnf_p7->p7_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 bind error fd:%d errno: %d\n", pnf_p7->p7_sock, errno);
    return -1;
  }
  NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 bind succeeded...\n");

  // Initializaing timing structures needed for slot ticking

  struct timespec slot_start;
  clock_gettime(CLOCK_MONOTONIC, &slot_start);

  struct timespec pselect_start;

  struct timespec slot_duration;
  slot_duration.tv_sec = 0;
  slot_duration.tv_nsec = 0.5e6;

  // Infinite loop

  while (pnf_p7->terminate == 0) {
    fd_set rfds;
    int selectRetval = 0;

    // select on a timeout and then get the message
    FD_ZERO(&rfds);
    FD_SET(pnf_p7->p7_sock, &rfds);

    struct timespec timeout;
    timeout.tv_sec = 100;
    timeout.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &pselect_start);

    // setting the timeout

    if ((pselect_start.tv_sec > slot_start.tv_sec)
        || ((pselect_start.tv_sec == slot_start.tv_sec) && (pselect_start.tv_nsec > slot_start.tv_nsec))) {
      // overran the end of the subframe we do not want to wait
      timeout.tv_sec = 0;
      timeout.tv_nsec = 0;

      // struct timespec overrun = pnf_timespec_sub(pselect_start, sf_start);
      // NFAPI_TRACE(NFAPI_TRACE_INFO, "Subframe overrun detected of %d.%d running to catchup\n", overrun.tv_sec, overrun.tv_nsec);
    } else {
      // still time before the end of the subframe wait
      timeout = pnf_timespec_sub(slot_start, pselect_start);
    }

    selectRetval = pselect(pnf_p7->p7_sock + 1, &rfds, NULL, NULL, &timeout, NULL);

    uint32_t now_hr_time = pnf_get_current_time_hr();

    if (selectRetval == 0) {
      // timeout

      // update slot start timing
      slot_start = pnf_timespec_add(slot_start, slot_duration);

      // increment sfn/slot
      if (++pnf_p7->slot == 20) {
        pnf_p7->slot = 0;
        pnf_p7->sfn = (pnf_p7->sfn + 1) % 1024;
      }

      continue;
    } else if (selectRetval == -1 && (errno == EINTR)) {
      // interrupted by signal
      NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 Signal Interrupt %d\n", errno);
      continue;
    } else if (selectRetval == -1) {
      NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 select() failed\n");
      sleep(1);
      continue;
    }

    if (FD_ISSET(pnf_p7->p7_sock, &rfds))

    {
      pnf_nr_nfapi_p7_read_dispatch_message(pnf_p7, now_hr_time);
    }
  }
  NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 Terminating..\n");

  // close the connection and socket
  if (close(pnf_p7->p7_sock) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "close failed errno: %d\n", errno);
  }

  if (pthread_mutex_destroy(&(pnf_p7->pack_mutex)) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
  }

  if (pthread_mutex_destroy(&(pnf_p7->mutex)) != 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
  }

  return 0;
}

static int nfapi_nr_pnf_p7_start(nfapi_pnf_p7_config_t *config)
{
  // Verify that config is not null
  if (config == 0)
    return -1;

  pnf_p7_t *_this = (pnf_p7_t *)(config);

  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s\n", __FUNCTION__);

  pnf_nr_p7_message_pump(_this);

  return 0;
}

void *pnf_nr_p7_thread_start(void *ptr)
{
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[NR_PNF] NR P7 THREAD %s\n", __FUNCTION__);
  nfapi_pnf_p7_config_t *config = (nfapi_pnf_p7_config_t *)ptr;
  nfapi_nr_pnf_p7_start(config);
  return 0;
}
