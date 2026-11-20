/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "socket_vnf.h"

#include "common/utils/LOG/log.h"
#include "nfapi.h"
#include "vnf/nfapi_nr_vnf.h"
#include <common/platform_constants.h>
#include "nfapi/oai_integration/vendor_ext.h" //TODO: Remove this include when removing the Aerial transport stuff


void socket_stop_nfapi_p5_p7()
{
  get_p7_nr_vnf()->terminate = 1;
  get_nr_config()->pnf_disconnect_indication = NULL;
}

void socket_nfapi_send_stop_request(vnf_nr_t *vnf)
{
  nfapi_nr_stop_request_scf_t req = {.header.message_id = NFAPI_NR_PHY_MSG_TYPE_STOP_REQUEST, .header.phy_id = 0};
  nfapi_nr_vnf_stop_req(&vnf->_public, 0, &req);
  NFAPI_TRACE(NFAPI_TRACE_INFO, "Sent NFAPI STOP.request\n");
}

static bool send_p5_msg(vnf_nr_t *vnf, nfapi_vnf_pnf_info_t *pnf, const void *msg, int len, uint8_t stream)
{
  int result = socket_send_p5_msg(vnf->sctp, pnf->p5_sock, &pnf->p5_pnf_sockaddr, msg, len, stream);

  if (result != len) {
    if (result < 0) {
      // error
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "sctp sendto failed errno: %d\n", errno);
    } else {
      // did not send the entire message
    }
  }
  return result == len;
}

bool vnf_nr_send_p5_msg(vnf_nr_t *vnf, uint16_t p5_idx, nfapi_nr_p4_p5_message_header_t *msg, uint32_t msg_len)
{
  nfapi_vnf_pnf_info_t *pnf = nfapi_nr_vnf_pnf_list_find(&(vnf->_public), p5_idx);

  if (pnf) {
    // pack the message for transmission
    int packedMessageLength = 0;

    packedMessageLength =
        vnf->_public.pack_func(msg, msg_len, vnf->tx_message_buffer, sizeof(vnf->tx_message_buffer), &vnf->_public.codec_config);

    if (packedMessageLength < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "nfapi_nr_p5_message_pack failed with return %d\n", packedMessageLength);
      return false;
    }

    return send_p5_msg(vnf, pnf, vnf->tx_message_buffer, packedMessageLength, 0);
  } else {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() cannot find pnf info for p5_idx:%d\n", __FUNCTION__, p5_idx);
    return false;
  }
}

bool vnf_nr_send_p7_msg(vnf_p7_t *vnf_p7, nfapi_nr_p7_message_header_t *header)
{
  nfapi_vnf_p7_connection_info_t *p7_connection = vnf_p7_connection_info_list_find(vnf_p7, header->phy_id);
  if (p7_connection) {
    int send_result = 0;
    uint8_t buffer[1024 * 1024 * 3];

    header->m_segment_sequence = NFAPI_NR_P7_SET_MSS(0, 0, p7_connection->sequence_number);

    int len = vnf_p7->_public.pack_func(header, buffer, sizeof(buffer), &vnf_p7->_public.codec_config);

    if (len < 0) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() failed to pack p7 message phy_id:%d\n", __FUNCTION__, header->phy_id);
      return false;
    }

    if (len > vnf_p7->_public.segment_size) {
      // todo : consider replacing with the sendmmsg call
      // todo : worry about blocking writes?

      // segmenting the transmit
      int msg_body_len = len - NFAPI_NR_P7_HEADER_LENGTH;
      int seg_body_len = vnf_p7->_public.segment_size - NFAPI_NR_P7_HEADER_LENGTH;
      int segment_count = (msg_body_len / (seg_body_len)) + ((msg_body_len % seg_body_len) ? 1 : 0);

      int segment = 0;
      int offset = NFAPI_NR_P7_HEADER_LENGTH;
      uint8_t tx_buffer[vnf_p7->_public.segment_size];
      NFAPI_TRACE(NFAPI_TRACE_DEBUG,
                  "%s() MORE THAN ONE SEGMENT phy_id:%d nfapi_p7_message_pack()=len=%d vnf_p7->_public.segment_size:%u\n",
                  __FUNCTION__,
                  header->phy_id,
                  len,
                  vnf_p7->_public.segment_size);
      for (segment = 0; segment < segment_count; ++segment) {
        uint8_t last = 0;
        uint16_t size = vnf_p7->_public.segment_size - NFAPI_NR_P7_HEADER_LENGTH;
        if (segment + 1 == segment_count) {
          last = 1;
          size = (msg_body_len) - (seg_body_len * segment);
        }

        uint32_t segment_size = size + NFAPI_NR_P7_HEADER_LENGTH;

        // Update the header with the m and segement
        memcpy(&tx_buffer[0], buffer, NFAPI_NR_P7_HEADER_LENGTH);

        // set the segment length, update and push m_segment_sequence
        uint8_t *buf_ptr = &tx_buffer[4];
        uint8_t *buf_ptr_end = &tx_buffer[10];
        push32(segment_size, (&buf_ptr), buf_ptr_end);
        header->m_segment_sequence = NFAPI_NR_P7_SET_MSS((!last), segment, p7_connection->sequence_number);
        push16(header->m_segment_sequence, (&buf_ptr), buf_ptr_end);

        memcpy(&tx_buffer[NFAPI_NR_P7_HEADER_LENGTH], &buffer[0] + offset, size);
        offset += size;

        if (vnf_p7->_public.checksum_enabled) {
          nfapi_nr_p7_update_checksum(tx_buffer, segment_size);
        }
        const uint32_t time =
            calculate_transmit_timestamp(p7_connection->mu, p7_connection->sfn, p7_connection->slot, vnf_p7->slot_start_time_hr);
        nfapi_nr_p7_update_transmit_timestamp(tx_buffer, time);
        send_result = socket_send_p7_msg(vnf_p7->socket, &(p7_connection->remote_addr), &tx_buffer[0], segment_size);
      }
    } else {
      if (vnf_p7->_public.checksum_enabled) {
        nfapi_nr_p7_update_checksum(buffer, len);
      }
      const uint32_t time =
          calculate_transmit_timestamp(p7_connection->mu, p7_connection->sfn, p7_connection->slot, vnf_p7->slot_start_time_hr);
      nfapi_nr_p7_update_transmit_timestamp(buffer, time);
      send_result = socket_send_p7_msg(vnf_p7->socket, &(p7_connection->remote_addr), &buffer[0], len);
    }
    p7_connection->sequence_number++;
    return send_result == 0;
  } else {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() cannot find p7 connection info for phy_id:%d\n", __FUNCTION__, header->phy_id);
    return false;
  }
}

static int vnf_nr_read_dispatch_message(nfapi_nr_vnf_config_t *config, nfapi_vnf_pnf_info_t *pnf)
{
  if (1) {
    int socket_connected = 1;
    vnf_nr_t *vnf = (vnf_nr_t *)(config);
    // 1. Peek the message header
    // 2. If the message is larger than the stack buffer then create a dynamic buffer
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

    struct sctp_sndrcvinfo sndrcvinfo;
    (void)memset(&sndrcvinfo, 0, sizeof(struct sctp_sndrcvinfo));

    {
      int flags = MSG_PEEK;
      if (vnf->sctp) {
        message_size =
            sctp_recvmsg(pnf->p5_sock, header_buffer, header_buffer_size, (struct sockaddr *)&addr, &addr_len, &sndrcvinfo, &flags);
      } else {
        message_size = recv(pnf->p5_sock, header_buffer, header_buffer_size, flags);
      }

      if (message_size == -1) {
        NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Failed to peek sctp message size errno:%d\n", errno);
        return 0;
      }
      NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Peeked message with length :0x%02x\n", message_size);

      for (int i = 0; i < message_size; i++) {
        printf("%02x ", header_buffer[i]);
      }
      printf("\n");

      nfapi_nr_p4_p5_message_header_t header;
      const bool result = config->hdr_unpack_func(header_buffer, header_buffer_size, &header, sizeof(header), 0);
      if (!result) {
        NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Failed to decode message header\n");
        return 0;
      }
      message_size = header.message_length + header_buffer_size;

      // now have the size of the mesage
      NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF After header unpacking msg size is :0x%02x\n", message_size);
    }

    if (message_size > stack_buffer_size) {
      dynamic_buffer = (uint8_t *)malloc(message_size);

      if (dynamic_buffer == NULL) {
        // todo : add error mesage
        NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Failed to allocate dynamic buffer for sctp_recvmsg size:%d\n", message_size);
        return -1;
      }

      read_buffer = dynamic_buffer;
    }

    {
      int flags = 0;
      (void)memset(&sndrcvinfo, 0, sizeof(struct sctp_sndrcvinfo));

      ssize_t recvmsg_result = 0;
      if (vnf->sctp) {
        recvmsg_result =
            sctp_recvmsg(pnf->p5_sock, read_buffer, message_size, (struct sockaddr *)&addr, &addr_len, &sndrcvinfo, &flags);
      } else {
        recvmsg_result = recv(pnf->p5_sock, read_buffer, message_size, 0);
      }

      if (recvmsg_result == -1) {
        int tmp = errno;
        NFAPI_TRACE(NFAPI_TRACE_INFO, "Failed to read sctp message size error %s errno:%d\n", strerror(tmp), tmp);
      } else {
        NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF after recv flags are :0x%02x\n", flags);

        if (flags & MSG_NOTIFICATION) {
          NFAPI_TRACE(NFAPI_TRACE_INFO, "Notification received from %s:%u\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

          // todo - handle the events
        } else {
          NFAPI_TRACE(NFAPI_TRACE_INFO,
                      "Received message fd:%d from %s:%u assoc:%d on stream %d, PPID %d, length %d, flags 0x%x\n",
                      pnf->p5_sock,
                      inet_ntoa(addr.sin_addr),
                      ntohs(addr.sin_port),
                      sndrcvinfo.sinfo_assoc_id,
                      sndrcvinfo.sinfo_stream,
                      ntohl(sndrcvinfo.sinfo_ppid),
                      message_size,
                      flags);

          // handle now if complete message in one or more segments
          if ((flags & 0x80) == 0x80 || !vnf->sctp) {
            vnf_nr_handle_p4_p5_message(read_buffer, message_size, pnf->p5_idx, config);
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
}

static int nfapi_nr_vnf_p5_start(nfapi_nr_vnf_config_t *config)
{
  // Verify that config is not null
  if (config == 0)
    return -1;
  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s()\n", __FUNCTION__);

  int p5ListenSock, p5Sock;

  struct sockaddr_in addr = {0};
  socklen_t addrSize;

  struct sockaddr_in6 addr6 = {0};

  struct sctp_event_subscribe events = {0};
  struct sctp_initmsg initMsg = {0};
  int noDelay;

  vnf_nr_t *vnf = (vnf_nr_t *)(get_nr_config());

  NFAPI_TRACE(NFAPI_TRACE_INFO, "Starting P5 VNF connection on port %u\n", config->vnf_p5_port);

  {
    int protocol;
    int domain;

    if (vnf->sctp)
      protocol = IPPROTO_SCTP;
    else
      protocol = IPPROTO_IP;

    if (config->vnf_ipv6) {
      domain = PF_INET6;
    } else {
      domain = AF_INET;
    }

    // open the SCTP socket
    if ((p5ListenSock = socket(domain, SOCK_STREAM, protocol)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P5 socket errno: %d\n", errno);
      return 0;
    }
    NFAPI_TRACE(NFAPI_TRACE_INFO, "P5 socket created... %d\n", p5ListenSock);
  }

  if (vnf->sctp) {
    // configure for MSG_NOTIFICATION
    if (setsockopt(p5ListenSock, IPPROTO_SCTP, SCTP_EVENTS, &events, sizeof(struct sctp_event_subscribe)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt (SCTP_EVENTS) errno: %d\n", errno);
      close(p5ListenSock);
      return 0;
    }
    NFAPI_TRACE(NFAPI_TRACE_NOTE, "VNF Setting the SCTP_INITMSG\n");
    // configure the SCTP socket options
    initMsg.sinit_num_ostreams = 5; // MAX_SCTP_STREAMS;  // number of output streams can be greater
    initMsg.sinit_max_instreams = 5; // MAX_SCTP_STREAMS;  // number of output streams can be greater
    if (setsockopt(p5ListenSock, IPPROTO_SCTP, SCTP_INITMSG, &initMsg, sizeof(initMsg)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt (SCTP_INITMSG) errno: %d\n", errno);
      close(p5ListenSock);
      return 0;
    }
    noDelay = 1;
    if (setsockopt(p5ListenSock, IPPROTO_SCTP, SCTP_NODELAY, &noDelay, sizeof(noDelay)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt (STCP_NODELAY) errno: %d\n", errno);
      close(p5ListenSock);
      return 0;
    }
    struct sctp_event_subscribe events;
    memset((void *)&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;

    if (setsockopt(p5ListenSock, SOL_SCTP, SCTP_EVENTS, (const void *)&events, sizeof(events)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt errno: %d\n", errno);
      close(p5ListenSock);
      return -1;
    }
  } else {
    int error;
    socklen_t len = sizeof(error);

    if (getsockopt(p5ListenSock, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After getsockopt errno: %d\n", errno);
      close(p5ListenSock);
      return -1;
    }
  }

  if (config->vnf_ipv6) {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "IPV6 binding to port %d %d\n", config->vnf_p5_port, p5ListenSock);
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(config->vnf_p5_port);
    addr6.sin6_addr = in6addr_any;

    // bind to the configured address and port
    if (bind(p5ListenSock, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After bind errno: %d\n", errno);
      close(p5ListenSock);
      return 0;
    }
  } else if (config->vnf_ipv4) {
    NFAPI_TRACE(NFAPI_TRACE_INFO, "IPV4 binding to port %d\n", config->vnf_p5_port);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->vnf_p5_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // bind to the configured address and port
    if (bind(p5ListenSock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "After bind errno: %d\n", errno);
      close(p5ListenSock);
      AssertFatal(1 == 0, "Failed to bind socket with errno: %d\n", errno);
      return 0;
    }
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "bind succeeded..%d.\n", p5ListenSock);

  // put the socket into listen mode
  if (listen(p5ListenSock, 2) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After listen errno: %d\n", errno);
    close(p5ListenSock);
    return 0;
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "listen succeeded...\n");

  struct timeval tv;
  fd_set read_fd_set;

  int p5_idx = 0;
  while (vnf->terminate == 0) {
    FD_ZERO(&read_fd_set);

    FD_SET(p5ListenSock, &read_fd_set);
    int max_fd = p5ListenSock;

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    nfapi_vnf_pnf_info_t *pnf = config->pnf_list;
    while (pnf != 0) {
      if (pnf->connected) {
        FD_SET(pnf->p5_sock, &read_fd_set);
        if (pnf->p5_sock > max_fd) {
          max_fd = pnf->p5_sock;
        }
      }

      pnf = pnf->next;
    }

    int select_result = select(max_fd + 1, &read_fd_set, 0, 0, &tv);

    if (select_result == -1) {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "select result %d errno %d\n", select_result, errno);
      close(p5ListenSock);
      return 0;
    } else if (select_result) {
      if (FD_ISSET(p5ListenSock, &read_fd_set)) {
        addrSize = sizeof(struct sockaddr_in);
        NFAPI_TRACE(NFAPI_TRACE_INFO, "Accepting connection from PNF...\n");

        p5Sock = accept(p5ListenSock, (struct sockaddr *)&addr, &addrSize);

        if (p5Sock < 0) {
          NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to accept PNF connection reason:%d\n", errno);
        } else {
          NFAPI_TRACE(NFAPI_TRACE_INFO,
                      "PNF connection (fd:%d) accepted from %s:%d \n",
                      p5Sock,
                      inet_ntoa(addr.sin_addr),
                      ntohs(addr.sin_port));
          nfapi_vnf_pnf_info_t *pnf = (nfapi_vnf_pnf_info_t *)malloc(sizeof(nfapi_vnf_pnf_info_t));
          NFAPI_TRACE(NFAPI_TRACE_INFO, "MALLOC nfapi_vnf_pnf_info_t for pnf_list pnf:%p\n", pnf);
          memset(pnf, 0, sizeof(nfapi_vnf_pnf_info_t));
          pnf->p5_sock = p5Sock;
          pnf->p5_idx = p5_idx++;
          pnf->p5_pnf_sockaddr = addr;
          pnf->connected = 1;

          nfapi_nr_vnf_pnf_list_add(config, pnf);

          // Inform mac that a pnf connection has been established
          // todo : allow mac to 'accept' the connection. i.e. to
          // reject it.
          if (config->pnf_nr_connection_indication != 0) {
            (config->pnf_nr_connection_indication)(config, pnf->p5_idx);
          }

          // check the connection status
          if (vnf->sctp) {
            struct sctp_status status;
            (void)memset(&status, 0, sizeof(struct sctp_status));
            socklen_t optLen = (socklen_t)sizeof(struct sctp_status);
            if (getsockopt(p5Sock, IPPROTO_SCTP, SCTP_STATUS, &status, &optLen) < 0) {
              NFAPI_TRACE(NFAPI_TRACE_ERROR, "After getsockopt errno: %d\n", errno);
              return -1;
            } else {
              NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Association ID = %d\n", status.sstat_assoc_id);
              NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Receiver window size = %d\n", status.sstat_rwnd);
              NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF In Streams = %d\n", status.sstat_instrms);
              NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF Out Streams = %d\n", status.sstat_outstrms);
            }
          }
        }
      } else {
        uint8_t delete_pnfs = 0;

        nfapi_vnf_pnf_info_t *pnf = config->pnf_list;
        while (pnf != 0) {
          if (FD_ISSET(pnf->p5_sock, &read_fd_set)) {
            if (vnf_nr_read_dispatch_message(config, pnf) == 0) {
              if (config->pnf_disconnect_indication != 0) {
                (config->pnf_disconnect_indication)(config, pnf->p5_idx);
              }

              close(pnf->p5_sock);

              pnf->to_delete = 1;
              delete_pnfs = 1;
            }
          }

          pnf = pnf->next;
        }

        if (delete_pnfs) {
          nfapi_vnf_pnf_info_t *pnf = config->pnf_list;
          nfapi_vnf_pnf_info_t *prev = 0;
          while (pnf != 0) {
            nfapi_vnf_pnf_info_t *curr = pnf;

            if (pnf->to_delete == 1) {
              if (prev == 0) {
                config->pnf_list = pnf->next;
              } else {
                prev->next = pnf->next;
              }

              pnf = pnf->next;

              free(curr);
            } else {
              prev = pnf;
              pnf = pnf->next;
            }
          }
        }
      }

      continue;
    } else {
      // timeout

      // Should we test for socket closure here every second?

      continue;
    }
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "Closing p5Sock socket's\n");
  {
    nfapi_vnf_pnf_info_t *curr = config->pnf_list;
    while (curr != NULL) {
      if (config->pnf_disconnect_indication) {
        (config->pnf_disconnect_indication)(config, curr->p5_idx);
      }

      close(curr->p5_sock);
      curr = curr->next;
    }
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "Closing p5Listen socket\n");
  close(p5ListenSock);

  return 0;
}

void vnf_start_p5_thread(void *ptr)
{
  NFAPI_TRACE(NFAPI_TRACE_INFO, "[VNF] VNF NFAPI thread - nfapi_vnf_start()%s\n", __FUNCTION__);
  pthread_setname_np(pthread_self(), "VNF");
  nfapi_nr_vnf_p5_start((nfapi_nr_vnf_config_t *)ptr);
}

void vnf_nr_reassemble_p7_message(void *pRecvMsg, int recvMsgLen, vnf_p7_t *vnf_p7)
{
  nfapi_nr_p7_message_header_t messageHeader;

  // validate the input params
  if (pRecvMsg == NULL || recvMsgLen < 4 || vnf_p7 == NULL) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "vnf_handle_p7_message: invalid input params (%p %d %p)\n", pRecvMsg, recvMsgLen, vnf_p7);
    return;
  }

  // unpack the message header
  const bool result =
      vnf_p7->_public.hdr_unpack_func(pRecvMsg, recvMsgLen, &messageHeader, sizeof(messageHeader), &vnf_p7->_public.codec_config);
  if (!result) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
    return;
  }

  if (vnf_p7->_public.checksum_enabled) {
    uint32_t checksum = nfapi_nr_p7_calculate_checksum(pRecvMsg, recvMsgLen);
    if (checksum != messageHeader.checksum) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR,
                  "Checksum verification failed %d %d msg:%d len:%d\n",
                  checksum,
                  messageHeader.checksum,
                  messageHeader.message_id,
                  recvMsgLen);
      return;
    }
  }

  uint8_t m = NFAPI_P7_GET_MORE(messageHeader.m_segment_sequence);
  uint8_t segment_num = NFAPI_P7_GET_SEGMENT(messageHeader.m_segment_sequence);
  uint8_t sequence_num = NFAPI_P7_GET_SEQUENCE(messageHeader.m_segment_sequence);

  if (m == 0 && segment_num == 0) {
    // we have a complete message
    // ensure the message is sensible
    if (recvMsgLen < 8 || pRecvMsg == NULL) {
      NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
      return;
    }

    // vnf_dispatch_p7_message(&messageHeader, pRecvMsg, recvMsgLen, vnf_p7);
    vnf_nr_handle_p7_message(pRecvMsg, recvMsgLen, vnf_p7);
  } else {
    nfapi_vnf_p7_connection_info_t *phy = vnf_p7_connection_info_list_find(vnf_p7, messageHeader.phy_id);

    if (phy) {
      vnf_p7_rx_message_t *rx_msg = vnf_p7_rx_reassembly_queue_add_segment(vnf_p7,
                                                                           &(phy->reassembly_queue),
                                                                           sequence_num,
                                                                           segment_num,
                                                                           m,
                                                                           pRecvMsg,
                                                                           recvMsgLen);

      if (rx_msg->num_segments_received == rx_msg->num_segments_expected) {
        // send the buffer on
        uint16_t i = 0;
        uint16_t length = 0;
        for (i = 0; i < rx_msg->num_segments_expected; ++i) {
          length += rx_msg->segments[i].length - (i > 0 ? NFAPI_NR_P7_HEADER_LENGTH : 0);
        }

        if (phy->reassembly_buffer_size < length) {
          vnf_p7_free(vnf_p7, phy->reassembly_buffer);
          phy->reassembly_buffer = 0;
        }

        if (phy->reassembly_buffer == 0) {
          NFAPI_TRACE(NFAPI_TRACE_NOTE, "Resizing VNF_P7 Reassembly buffer %d->%d\n", phy->reassembly_buffer_size, length);
          phy->reassembly_buffer = (uint8_t *)vnf_p7_malloc(vnf_p7, length);

          if (phy->reassembly_buffer == 0) {
            NFAPI_TRACE(NFAPI_TRACE_NOTE, "Failed to allocate VNF_P7 reassemby buffer len:%d\n", length);
            return;
          }
          memset(phy->reassembly_buffer, 0, length);
          phy->reassembly_buffer_size = length;
        }

        uint16_t offset = 0;
        for (i = 0; i < rx_msg->num_segments_expected; ++i) {
          if (i == 0) {
            memcpy(phy->reassembly_buffer, rx_msg->segments[i].buffer, rx_msg->segments[i].length);
            offset += rx_msg->segments[i].length;
          } else {
            memcpy(phy->reassembly_buffer + offset,
                   rx_msg->segments[i].buffer + NFAPI_NR_P7_HEADER_LENGTH,
                   rx_msg->segments[i].length - NFAPI_NR_P7_HEADER_LENGTH);
            offset += rx_msg->segments[i].length - NFAPI_NR_P7_HEADER_LENGTH;
          }
        }

        vnf_nr_handle_p7_message(phy->reassembly_buffer, length, vnf_p7);

        // delete the structure
        vnf_p7_rx_reassembly_queue_remove_msg(vnf_p7, &(phy->reassembly_queue), rx_msg);
      }

      // see corresponding comment in pnf_nr_handle_p7_message() [same commit]
      vnf_p7_rx_reassembly_queue_remove_old_msgs(vnf_p7, &(phy->reassembly_queue), 10000);
    } else {
      NFAPI_TRACE(NFAPI_TRACE_INFO, "Unknown phy id %d\n", messageHeader.phy_id);
    }
  }
}

int vnf_nr_p7_read_dispatch_message(vnf_p7_t *vnf_p7)
{
  int recvfrom_result = 0;
  struct sockaddr_in remote_addr;
  socklen_t remote_addr_size = sizeof(remote_addr);

  do {
    // peek the header
    uint8_t header_buffer[NFAPI_NR_P7_HEADER_LENGTH];
    recvfrom_result = recvfrom(vnf_p7->socket,
                               header_buffer,
                               NFAPI_NR_P7_HEADER_LENGTH,
                               MSG_DONTWAIT | MSG_PEEK,
                               (struct sockaddr *)&remote_addr,
                               &remote_addr_size);

    if (recvfrom_result > 0) {
      // get the segment size
      nfapi_nr_p7_message_header_t header;
      const bool result = vnf_p7->_public.hdr_unpack_func(header_buffer, NFAPI_NR_P7_HEADER_LENGTH, &header, sizeof(header), 0);
      if (!result) {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
        return -1;
      }

      // resize the buffer if we have a large segment
      if (header.message_length > vnf_p7->rx_message_buffer_size) {
        uint32_t newsize = vnf_p7->rx_message_buffer_size ? vnf_p7->rx_message_buffer_size * 2 : 65536;
        NFAPI_TRACE(NFAPI_TRACE_NOTE, "reallocing rx buffer %d (msg %d)\n", newsize, header.message_length);
        vnf_p7->rx_message_buffer = realloc(vnf_p7->rx_message_buffer, newsize);
        DevAssert(vnf_p7->rx_message_buffer);
        vnf_p7->rx_message_buffer_size = newsize;
      }

      // read the segment
      recvfrom_result = recvfrom(vnf_p7->socket,
                                 vnf_p7->rx_message_buffer,
                                 header.message_length,
                                 MSG_WAITALL | MSG_TRUNC,
                                 (struct sockaddr *)&remote_addr,
                                 &remote_addr_size);
      NFAPI_TRACE(NFAPI_TRACE_DEBUG, "recvfrom_result = %d from %s():%d\n", recvfrom_result, __FUNCTION__, __LINE__);

      // todo : how to handle incomplete readfroms, need some sort of buffer/select

      if (recvfrom_result > 0) {
        if (recvfrom_result != header.message_length) {
          NFAPI_TRACE(NFAPI_TRACE_ERROR,
                      "(%d) Received unexpected number of bytes. %d != %d",
                      __LINE__,
                      recvfrom_result,
                      header.message_length);
          break;
        }
        NFAPI_TRACE(NFAPI_TRACE_DEBUG, "Calling vnf_nr_reassemble_p7_message from %d\n", __LINE__);
        vnf_nr_reassemble_p7_message(vnf_p7->rx_message_buffer, recvfrom_result, vnf_p7);
        return 0;
      } else {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "recvfrom failed %d %d\n", recvfrom_result, errno);
      }
    }

    if (recvfrom_result == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // return to the select
      } else {
        NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom failed errno:%d\n", __FUNCTION__, errno);
      }
    }
  } while (recvfrom_result > 0);

  return 0;
}

static int nfapi_nr_vnf_p7_start(nfapi_vnf_p7_config_t *config)
{
  if (config == 0)
    return -1;

  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s()\n", __FUNCTION__);
  vnf_p7_t *vnf_p7 = get_p7_nr_vnf();
  vnf_p7 = (vnf_p7_t *)config;

  // Create p7 receive udp port
  // todo : this needs updating for Ipv6

  NFAPI_TRACE(NFAPI_TRACE_INFO, "Initialising VNF P7 port:%u\n", config->port);

  // open the UDP socket
  if ((vnf_p7->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 socket errno: %d\n", errno);
    return -1;
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF P7 socket created...\n");

  // configure the UDP socket options
  int iptos_value = 0;
  if (setsockopt(vnf_p7->socket, IPPROTO_IP, IP_TOS, &iptos_value, sizeof(iptos_value)) < 0) {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After setsockopt (IP_TOS) errno: %d\n", errno);
    return -1;
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF P7 setsockopt succeeded...\n");

  // Create the address structure
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config->port);
  addr.sin_addr.s_addr = INADDR_ANY;

  // bind to the configured port
  NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF P7 binding too %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
  if (bind(vnf_p7->socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
  // if (sctp_bindx(config->socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in), 0) < 0)
  {
    NFAPI_TRACE(NFAPI_TRACE_ERROR, "After bind errno: %d\n", errno);
    return -1;
  }

  NFAPI_TRACE(NFAPI_TRACE_INFO, "VNF P7 bind succeeded...\n");

  // struct timespec original_pselect_timeout;
  struct timespec pselect_timeout;
  pselect_timeout.tv_sec = 100;
  pselect_timeout.tv_nsec = 0;

  struct timespec ref_time;
  clock_gettime(CLOCK_MONOTONIC, &ref_time);
  while (vnf_p7->terminate == 0) {
    fd_set rfds;
    int maxSock = 0;
    FD_ZERO(&rfds);
    int selectRetval = 0;

    // Add the p7 socket
    FD_SET(vnf_p7->socket, &rfds);
    maxSock = vnf_p7->socket;

    selectRetval = pselect(maxSock + 1, &rfds, NULL, NULL, &pselect_timeout, NULL);

    if (selectRetval == 0) {
      // pselect timed out, continue
    } else if (selectRetval > 0) {
      // have a p7 message
      if (FD_ISSET(vnf_p7->socket, &rfds)) {
        vnf_nr_p7_read_dispatch_message(vnf_p7);
      }
    } else {
      // pselect error
      if (selectRetval == -1 && errno == EINTR) {
        // a sigal was received.
      } else {
        // should we exit now?
        if (selectRetval == -1 && errno == 22) // invalid argument??? not sure about timeout duration
        {
          usleep(100000);
        }
      }
    }
  }
  NFAPI_TRACE(NFAPI_TRACE_INFO, "Closing p7 socket\n");
  close(vnf_p7->socket);

  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() returning\n", __FUNCTION__);

  return 0;
}

void *vnf_nr_start_p7_thread(void *ptr)
{
  NFAPI_TRACE(NFAPI_TRACE_INFO, "%s()\n", __FUNCTION__);
  nfapi_vnf_p7_config_t *config = (nfapi_vnf_p7_config_t *)ptr;
  nfapi_nr_vnf_p7_start(config);
  return config;
}
