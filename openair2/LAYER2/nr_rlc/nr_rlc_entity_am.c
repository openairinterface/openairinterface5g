/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_rlc_entity_am.h"

#include <stdlib.h>
#include <string.h>

#include "nr_rlc_pdu.h"

#include "LOG/log.h"
#include "common/utils/time_stat.h"
#include "common/utils/assertions.h"

/* for a given SDU/SDU segment, computes the corresponding PDU header size */
static int compute_pdu_header_size(nr_rlc_entity_am_t *entity,
                                   nr_rlc_sdu_segment_t *sdu)
{
  int header_size = 2;
  /* one more byte if SN field length is 18 */
  if (entity->sn_field_length == 18)
    header_size++;
  /* two more bytes for SO if SDU segment is not the first */
  if (!sdu->is_first) header_size += 2;
  return header_size;
}

/*************************************************************************/
/* PDU RX functions                                                      */
/*************************************************************************/

static int modulus_rx(nr_rlc_entity_am_t *entity, int a)
{
  /* as per 38.322 7.1, modulus base is rx_next */
  int r = a - entity->rx_next;
  if (r < 0) r += entity->sn_modulus;
  return r;
}

static inline int modulus_tx(nr_rlc_entity_am_t *entity, int a)
{
  int r = a - entity->tx_next_ack;
  if (r < 0) r += entity->sn_modulus;
  return r;
}

static int sn_in_recv_window(void *_entity, int sn)
{
  nr_rlc_entity_am_t *entity = _entity;
  int mod_sn = modulus_rx(entity, sn);
  /* we simplify rx_next <= sn < rx_next + am_window_size */
  return mod_sn < entity->window_size;
}

static int sn_compare_rx(void *_entity, int a, int b)
{
  nr_rlc_entity_am_t *entity = _entity;
  return modulus_rx(entity, a) - modulus_rx(entity, b);
}

static inline int sn_compare_tx(void *_entity, int a, int b)
{
  nr_rlc_entity_am_t *entity = _entity;
  return modulus_tx(entity, a) - modulus_tx(entity, b);
}

nr_rlc_sdu_segment_t *nr_rlc_tx_sdu_segment_list_add(nr_rlc_entity_am_t *entity,
    nr_rlc_sdu_segment_t *list, nr_rlc_sdu_segment_t *sdu_segment)
{
  nr_rlc_sdu_segment_t head;
  nr_rlc_sdu_segment_t *cur;
  nr_rlc_sdu_segment_t *prev;

  head.next = list;
  cur = list;
  prev = &head;

  /* order is by 'sn', if 'sn' is the same then order is by 'so' */
  while (cur != NULL) {
    /* check if 'sdu_segment' is before 'cur' in the list */
    if (sn_compare_tx(entity, cur->sdu->sn, sdu_segment->sdu->sn) > 0 ||
        (cur->sdu->sn == sdu_segment->sdu->sn && cur->so > sdu_segment->so)) {
      break;
    }
    prev = cur;
    cur = cur->next;
  }
  prev->next = sdu_segment;
  sdu_segment->next = cur;
  return head.next;
}

static int segment_already_received(nr_rlc_entity_am_t *entity,
    int sn, int so, int size)
{
  nr_rlc_pdu_t *l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
  int covered;

  while (l != NULL && size > 0) {
    if (l->so <= so && so < l->so + l->size) {
      covered = l->size - (so - l->so);
      size -= covered;
      so += covered;
    } else if (l->so <= so+size-1 && so+size-1 < l->so + l->size) {
      covered = size - (l->so - so);
      size -= covered;
    }
    l = l->next;
  }

  return size <= 0;
}

/* checks that all the bytes of the SDU sn have been received (but SDU
 * has not been already processed)
 */
static int sdu_full(nr_rlc_entity_am_t *entity, int sn)
{
  nr_rlc_pdu_t *l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);

  int last_byte = -1;

  /* check if the data has already been processed */
  if (l != NULL && l->data == NULL)
    return 0;

  while (l != NULL) {
    if (l->so > last_byte + 1)
      return 0;
    if (l->is_last)
      return 1;
    int new_last_byte = l->so + l->size - 1;
    if (new_last_byte > last_byte)
      last_byte = new_last_byte;
    l = l->next;
  }

  return 0;
}

/* checks that an SDU has already been delivered */
static int sdu_delivered(nr_rlc_entity_am_t *entity, int sn)
{
  nr_rlc_pdu_t *l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
  return l != NULL && l->data == NULL;
}

/* check if there is some missing bytes before the last received of SDU sn */
/* todo: be sure that when no byte was received or the SDU has already been
 *       processed then the SDU has no missing byte
 */
static int sdu_has_missing_bytes(nr_rlc_entity_am_t *entity, int sn)
{
  nr_rlc_pdu_t *l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
  int last_byte;
  int new_last_byte;

  last_byte = -1;

  /* check if the data has already been processed */
  if (l != NULL && l->data == NULL)
    return 0;                    /* data already processed: no missing byte */

  while (l != NULL) {
    if (l->so > last_byte + 1)
      return 1;
    new_last_byte = l->so + l->size - 1;
    if (new_last_byte > last_byte)
      last_byte = new_last_byte;
    l = l->next;
  }

  return 0;
}

static void reassemble_and_deliver(nr_rlc_entity_am_t *entity, int sn)
{
  nr_rlc_pdu_t *pdu;
  char sdu[NR_SDU_MAX];
  int so = 0;
  int bad_sdu = 0;

  pdu = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);

  /* reassemble - free 'data' of each segment after processing */
  while (pdu != NULL) {
    if (pdu->so > so && !bad_sdu) {
      /* pdu->so > so is possible when the other end sends bogus data */
      LOG_E(RLC, "%s:%d:%s: inconsistent SDU, discarding\n",
            __FILE__, __LINE__, __FUNCTION__);
      bad_sdu = 1;
    }
    int len = pdu->size - (so - pdu->so);
    if (so + len > NR_SDU_MAX && !bad_sdu) {
      LOG_E(RLC, "%s:%d:%s: bad SDU, too big, discarding\n",
            __FILE__, __LINE__, __FUNCTION__);
      bad_sdu = 1;
    }
    if (!bad_sdu && len > 0) {
      memcpy(sdu + so, pdu->data + so - pdu->so, len);
      so += len;
    }
    free(pdu->data);
    pdu->data = NULL;
    entity->rx_size -= pdu->size;
    pdu = pdu->next;
  }

  if (bad_sdu)
    return;

  /* deliver */
  entity->common.deliver_sdu(entity->common.deliver_sdu_data,
                             (nr_rlc_entity_t *)entity,
                             sdu, so);

  entity->common.stats.txsdu_pkts++;
  /* AM 'txsdu_bytes' now only count successfully transmitted bytes */
  // entity->common.stats.txsdu_bytes += so;
}

static void reception_actions(nr_rlc_entity_am_t *entity, nr_rlc_pdu_t *pdu)
{
  int x = pdu->sn;

  if (sn_compare_rx(entity, x, entity->rx_next_highest) >= 0)
    entity->rx_next_highest = (x + 1) % entity->sn_modulus;

  if (sdu_full(entity, x)) {
    reassemble_and_deliver(entity, x);

    if (x == entity->rx_highest_status) {
      int rx_highest_status = entity->rx_highest_status;
      while (sdu_delivered(entity, rx_highest_status))
        rx_highest_status = (rx_highest_status + 1) % entity->sn_modulus;
      entity->rx_highest_status = rx_highest_status;
    }

    if (x == entity->rx_next) {
      /* update rx_next and free all delivered SDUs at the head of the
       * RX list
       */
      int rx_next = entity->rx_next;
      int count = 0;
      nr_rlc_pdu_t *l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, rx_next);
      while (l != NULL && l->data == NULL) {
        /* free all segments of this SDU */
        do {
          nr_rlc_pdu_t *p = l;
          l = l->next;
          free(p);
        } while (l != NULL);
        nr_rlc_rx_manager_clear_pdu(entity->rx, rx_next);
        count++;
        rx_next = (rx_next + 1) % entity->sn_modulus;
        l = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, rx_next);
      }
      entity->rx_next = rx_next;
      nr_rlc_rx_manager_advance(entity->rx, count);
      nr_rlc_rx_manager_set_start(entity->rx, rx_next);
    }
  }

  if (entity->t_reassembly_start) {
    if (entity->rx_next_status_trigger == entity->rx_next ||
        (entity->rx_next_status_trigger == (entity->rx_next + 1)
                                             % entity->sn_modulus &&
         !sdu_has_missing_bytes(entity, entity->rx_next)) ||
        (!sn_in_recv_window(entity, entity->rx_next_status_trigger) &&
         entity->rx_next_status_trigger !=
           (entity->rx_next + entity->window_size) % entity->sn_modulus)) {
      entity->t_reassembly_start = 0;
    }
  }

  if (entity->t_reassembly_start == 0) {
    if (sn_compare_rx(entity, entity->rx_next_highest,
                      (entity->rx_next + 1) % entity->sn_modulus) > 0 ||
        (entity->rx_next_highest == (entity->rx_next + 1)
                                      % entity->sn_modulus &&
         sdu_has_missing_bytes(entity, entity->rx_next))) {
      entity->t_reassembly_start = entity->t_current;
      entity->rx_next_status_trigger = entity->rx_next_highest;
    }
  }
}

static int so_overlap(int s1, int e1, int s2, int e2)
{
  if (s1 < s2) {
    if (e1 == -1 || e1 >= s2)
      return 1;
    return 0;
  }
  if (e2 == -1 || s1 <= e2)
    return 1;
  return 0;
}

static void process_control_pdu(nr_rlc_entity_am_t *entity,
                                char *buffer, int size)
{
#define R(d) do { if (nr_rlc_pdu_decoder_in_error(&d)) goto err; } while (0)
  nr_rlc_pdu_decoder_t decoder;
  int i;
  int cpt;
  int ack_sn;
  int nack_sn;
  int e1;
  int e2;
  int e3;
  int so_start;
  int so_end;
  int range;
  int prev_nack_sn = -1;
  int prev_so_start = 0;
  int prev_so_end = 0;
  int tx_next_ack;
  nr_rlc_sdu_segment_t *cur_wait_list;
  nr_rlc_sdu_segment_t *prev_wait_list;
  nr_rlc_sdu_segment_t *end_wait_list = NULL;
  nr_rlc_sdu_segment_t head_wait_list;
  nr_rlc_sdu_segment_t *cur_retransmit_list;
  nr_rlc_sdu_segment_t *new_retransmit_list;
  nr_rlc_sdu_segment_t head_retransmit_list;
  int cmp;

  /* validate the control PDU: read it, check the values of ACK and NACKs */

  nr_rlc_pdu_decoder_init(&decoder, buffer, size);
  nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder); /* dc */

  cpt = nr_rlc_pdu_decoder_get_bits(&decoder, 3); R(decoder);
  if (cpt != 0) {
    LOG_E(RLC, "discard PDU, CPT not 0 (%d)\n", cpt);
    goto err;
  }
  ack_sn = nr_rlc_pdu_decoder_get_bits(&decoder, entity->sn_field_length); R(decoder);
  e1 = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
  /* r bits */
  if (entity->sn_field_length == 18) {
    nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
  } else {
    nr_rlc_pdu_decoder_get_bits(&decoder, 7); R(decoder);
  }

  /* discard the whole control PDU if ack_sn is invalid, that is
   * if it does not satisfy tx_next_ack <= ack_sn <= tx_next
   * (no need to test tx_next_ack <= ack_sn, this is always true since
   * tx_next_ack is the modulus base)
   */
  if (sn_compare_tx(entity, ack_sn, entity->tx_next) > 0) {
    LOG_W(RLC, "ack_sn (%d) not valid (tx_next_ack %d tx_next %d), discard control PDU\n",
          ack_sn, entity->tx_next_ack, entity->tx_next);
    return;
  }

  /* discard the whole control PDU if NACKs are bad (not <= ack_sn, not in
   * increasing order)
   */
  while (e1) {
    nack_sn = nr_rlc_pdu_decoder_get_bits(&decoder, entity->sn_field_length); R(decoder);
    e1 = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
    e2 = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
    e3 = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
    /* r bits */
    if (entity->sn_field_length == 18) {
      nr_rlc_pdu_decoder_get_bits(&decoder, 3); R(decoder);
    } else {
      nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
    }
    if (e2) {
      so_start = nr_rlc_pdu_decoder_get_bits(&decoder, 16); R(decoder);
      so_end = nr_rlc_pdu_decoder_get_bits(&decoder, 16); R(decoder);
    } else {
      so_start = 0;
      so_end = 0xffff;
    }
    if (e3) {
      range = nr_rlc_pdu_decoder_get_bits(&decoder, 8); R(decoder);
    } else {
      range = 1;
    }
    /* special value 0xffff indicates 'all bytes to the end' */
    if (so_end == 0xffff)
      so_end = -1;

    for (i = 0; i < range; i++) {
      int cur_nack_sn  = (nack_sn + i) % entity->sn_modulus;
      int cur_so_start = i == 0 ?         so_start : 0;
      int cur_so_end   = i == range - 1 ? so_end : -1;

      /* check that current nack is > previous nack and <= ack
       * if not then reject the control PDU
       */
      if (prev_nack_sn != -1) {
        cmp = sn_compare_tx(entity, cur_nack_sn, prev_nack_sn);
        if (cmp < 0
            || (cmp == 0
                && (prev_so_end == -1
                    || cur_so_start <= prev_so_end))) {
          LOG_E(RLC, "bad NACK, not bigger than previous NACK (nack sn %d so start/end %d/%d, previous nack sn %d so start/end %d/%d)\n",
                cur_nack_sn, cur_so_start, cur_so_end,
                prev_nack_sn, prev_so_start, prev_so_end);
          goto err;
        }
      }
      if (sn_compare_tx(entity, cur_nack_sn, ack_sn) > 0) {
        LOG_E(RLC, "bad NACK, bigger than ACK (nack %d ack %d) (tx_next_ack %d tx_next %d)\n",
              cur_nack_sn, ack_sn, entity->tx_next_ack, entity->tx_next);
        goto err;
      }
    }
  }

  /* process the control PDU for real (checks not needed now, previous steps
   * validated everything)
   */

  head_wait_list.next = entity->wait_list;
  cur_wait_list       = entity->wait_list;
  prev_wait_list      = &head_wait_list;

  head_retransmit_list.next = NULL;
  cur_retransmit_list = entity->retransmit_list;
  new_retransmit_list = &head_retransmit_list;

  nr_rlc_pdu_decoder_init(&decoder, buffer, size);
  nr_rlc_pdu_decoder_get_bits(&decoder, 1);             /* dc */

  cpt = nr_rlc_pdu_decoder_get_bits(&decoder, 3);
  ack_sn = nr_rlc_pdu_decoder_get_bits(&decoder, entity->sn_field_length);
  e1 = nr_rlc_pdu_decoder_get_bits(&decoder, 1);
  /* r bits */
  if (entity->sn_field_length == 18) {
    nr_rlc_pdu_decoder_get_bits(&decoder, 1);
  } else {
    nr_rlc_pdu_decoder_get_bits(&decoder, 7);
  }

  /* 38.322 5.3.3.3 says to stop t_poll_retransmit if a ACK or NACK is
   * received for the SN 'poll_sn' - check ACK case (NACK done below)
   */
  if (sn_compare_tx(entity, entity->poll_sn, ack_sn) < 0)
    entity->t_poll_retransmit_start = 0;

  int last_counted_sn = -1;
  while (e1) {
    nack_sn = nr_rlc_pdu_decoder_get_bits(&decoder, entity->sn_field_length);
    e1 = nr_rlc_pdu_decoder_get_bits(&decoder, 1);
    e2 = nr_rlc_pdu_decoder_get_bits(&decoder, 1);
    e3 = nr_rlc_pdu_decoder_get_bits(&decoder, 1);
    /* r bits */
    if (entity->sn_field_length == 18) {
      nr_rlc_pdu_decoder_get_bits(&decoder, 3);
    } else {
      nr_rlc_pdu_decoder_get_bits(&decoder, 1);
    }
    if (e2) {
      so_start = nr_rlc_pdu_decoder_get_bits(&decoder, 16);
      so_end = nr_rlc_pdu_decoder_get_bits(&decoder, 16);
    } else {
      so_start = 0;
      so_end = 0xffff;
    }
    if (e3) {
      range = nr_rlc_pdu_decoder_get_bits(&decoder, 8);
    } else {
      range = 1;
    }
    /* special value 0xffff indicates 'all bytes to the end' */
    if (so_end == 0xffff)
      so_end = -1;

    /* process nacks */
    for (i = 0; i < range; i++) {
      int cur_nack_sn  = (nack_sn + i) % entity->sn_modulus;
      int cur_so_start = i == 0 ?         so_start : 0;
      int cur_so_end   = i == range - 1 ? so_end : -1;


process_next_pdu:
      /* process smallest SN either from wait_list or retransmit list */
      if (cur_wait_list == NULL && cur_retransmit_list == NULL)
        goto lists_over;
      if (cur_wait_list == NULL)
        goto process_retransmit_list_head;
      if (cur_retransmit_list == NULL)
        goto process_wait_list_head;
      cmp = sn_compare_tx(entity, cur_wait_list->sdu->sn, cur_retransmit_list->sdu->sn);
      if (cmp < 0
          || (cmp == 0
              && cur_wait_list->so < cur_retransmit_list->so))
        goto process_wait_list_head;
      goto process_retransmit_list_head;

process_wait_list_head:
      /* if nack overlaps with current segment, put it in retransmit list */
      if (cur_wait_list->sdu->sn == cur_nack_sn
          && so_overlap(cur_so_start, cur_so_end,
                        cur_wait_list->so,
                        cur_wait_list->so + cur_wait_list->size - 1)) {
        prev_wait_list->next = cur_wait_list->next;
        if (cur_wait_list == entity->wait_end)
          end_wait_list = prev_wait_list;
        cur_wait_list->next = NULL;
        new_retransmit_list->next = cur_wait_list;
        new_retransmit_list = cur_wait_list;
        /* The wait list groups segments by SN. Count the SDU once for this STATUS. */
        if (cur_nack_sn != last_counted_sn) {
          last_counted_sn = cur_nack_sn;
          cur_wait_list->sdu->retx_count++;
          /* report max RETX reached for all retx_count >= max_retx_threshold
           * (specs say to report if retx_count == max_retx_threshold).
           * Upper layers should react (radio link failure), so no big deal.
           */
          if (cur_wait_list->sdu->retx_count >= entity->max_retx_threshold)
            entity->common.max_retx_reached(entity->common.max_retx_reached_data, (nr_rlc_entity_t *)entity);
        }
        /* update buffer status */
        entity->common.bstatus.retx_size += compute_pdu_header_size(entity, cur_wait_list)
                                            + cur_wait_list->size;
        /* and go process the next pdu, still for the current nack */
        cur_wait_list = prev_wait_list->next;
        goto process_next_pdu;
      }

      /* if current segment SN > current NACK, we can't classify it yet */
      cmp = sn_compare_tx(entity, cur_wait_list->sdu->sn, cur_nack_sn);
      if (cmp > 0
          || (cmp == 0
              && cur_wait_list->so > cur_so_start))
        goto done_nack;

      /* if current segment is acked, free it, indicate successful delivery
       * if fully acked
       */
      if (sn_compare_tx(entity, cur_wait_list->sdu->sn, ack_sn) < 0) {
        int upper_layer_id = cur_wait_list->sdu->upper_layer_id;
        int sdu_size = cur_wait_list->sdu->size;
        prev_wait_list->next = cur_wait_list->next;
        if (cur_wait_list == entity->wait_end)
          end_wait_list = prev_wait_list;
        if (nr_rlc_free_sdu_segment(cur_wait_list)) {
          entity->tx_size -= sdu_size;
          // Wait-ACK: count as successfully transmitted bytes
          entity->common.stats.txsdu_bytes += sdu_size;
          entity->common.sdu_successful_delivery(
              entity->common.sdu_successful_delivery_data,
              (nr_rlc_entity_t *)entity, upper_layer_id);
        }
        cur_wait_list = prev_wait_list->next;
        goto process_next_pdu;
      }

      /* if current segment SN > ack_sn, we're done with this list */
      if (sn_compare_tx(entity, cur_wait_list->sdu->sn, ack_sn) > 0) {
        cur_wait_list = NULL;
        goto process_next_pdu;
      }

      /* current segment SN == ack_sn, skip this segment and move to the next.
       * Not sure if correct, can we get a nack_sn == ack_sn? 38.322 5.3.4 says
       * "set the ACK_SN to the SN of the next not received RLC SDU which is
       * not indicated as missing in the resulting STATUS PDU." Is it okay to
       * receive some NACK for a given SN and an ACK == this SN? I can see a
       * case where it's possible (only parts of an SN were received and the
       * TB is too small to report the several NACKs for this SN, then we
       * can't set ACK = SN+1 but in this case, shouldn't we skip sending
       * NACKs for this SN and only send ACK = SN? our current implementation
       * does send both NACKs and ACK, see function generate_status()).
       */
      prev_wait_list = cur_wait_list;
      cur_wait_list = cur_wait_list->next;
      goto process_next_pdu;

process_retransmit_list_head:
      /* if nack overlaps with current segment, leave it in retransmit list */
      if (cur_retransmit_list->sdu->sn == cur_nack_sn
          && so_overlap(cur_so_start, cur_so_end,
                        cur_retransmit_list->so,
                        cur_retransmit_list->so + cur_retransmit_list->size - 1)) {
        new_retransmit_list->next = cur_retransmit_list;
        cur_retransmit_list = cur_retransmit_list->next;
        new_retransmit_list = new_retransmit_list->next;
        new_retransmit_list->next = NULL;
        /* go process the next pdu, still for the current nack */
        goto process_next_pdu;
      }

      /* if current segment SN > current NACK, we can't classify it yet */
      cmp = sn_compare_tx(entity, cur_retransmit_list->sdu->sn, cur_nack_sn);
      if (cmp > 0
          || (cmp == 0
              && cur_retransmit_list->so > cur_so_start))
        goto done_nack;

      /* if current segment is acked, free it, indicate successful delivery
       * if fully acked
       */
      if (sn_compare_tx(entity, cur_retransmit_list->sdu->sn, ack_sn) < 0) {
        nr_rlc_sdu_segment_t *cur = cur_retransmit_list;
        int upper_layer_id = cur->sdu->upper_layer_id;
        int sdu_size = cur->sdu->size;
        cur_retransmit_list = cur_retransmit_list->next;
        /* update buffer status */
        entity->common.bstatus.retx_size -= compute_pdu_header_size(entity, cur)
                                            + cur->size;
        if (nr_rlc_free_sdu_segment(cur)) {
          entity->tx_size -= sdu_size;
          // Retransmit-ACK: count as successfully transmitted bytes
          entity->common.stats.txsdu_bytes += sdu_size;
          entity->common.sdu_successful_delivery(
              entity->common.sdu_successful_delivery_data,
              (nr_rlc_entity_t *)entity, upper_layer_id);
        }
        goto process_next_pdu;
      }

      /* current segment SN >= ack_sn
       * if the wait list is empty, then put the remaining retransmit list at
       * the end of the new retransmit list (just a speedup)
       * if not, put only this segment
       */
      if (cur_wait_list == NULL) {
        new_retransmit_list->next = cur_retransmit_list;
        cur_retransmit_list = NULL;
        goto lists_over;
      }
      new_retransmit_list->next = cur_retransmit_list;
      cur_retransmit_list = cur_retransmit_list->next;
      new_retransmit_list = new_retransmit_list->next;
      new_retransmit_list->next = NULL;
      goto process_next_pdu;

done_nack:
      prev_nack_sn  = cur_nack_sn;
      prev_so_start = cur_so_start;
      prev_so_end   = cur_so_end;
    } /* for (i = 0; i < range; i++) */

lists_over:
    /* 38.322 5.3.3.3 says to stop t_poll_retransmit if a ACK or NACK is
     * received for the SN 'poll_sn' - check NACK case (ACK done above)
     */
    if (sn_compare_tx(entity, nack_sn, entity->poll_sn) <= 0 &&
        sn_compare_tx(entity, entity->poll_sn, (nack_sn + range) % entity->sn_modulus) < 0)
      entity->t_poll_retransmit_start = 0;
  } /* while (e1) */

  /* nacks done, finish with ack */
  /* we may report successful delivery out of order, if it's a problem
   * then we can have a single loop and deal with the smallest sn of
   * the current head of wait list and the current head of retransmit list.
   * (It's simpler to process those two lists the one after the other.)
   */
  /* deal with wait list */
  while (cur_wait_list != NULL
         && sn_compare_tx(entity, cur_wait_list->sdu->sn, ack_sn) < 0) {
    /* current segment is acked, free it, indicate successful delivery
     * if fully acked
     */
    int upper_layer_id = cur_wait_list->sdu->upper_layer_id;
    int sdu_size = cur_wait_list->sdu->size;
    prev_wait_list->next = cur_wait_list->next;
    if (cur_wait_list == entity->wait_end)
      end_wait_list = prev_wait_list;
    if (nr_rlc_free_sdu_segment(cur_wait_list)) {
      entity->tx_size -= sdu_size;
      // Wait-NACK done: count as successfully transmitted bytes
      entity->common.stats.txsdu_bytes += sdu_size;
      entity->common.sdu_successful_delivery(
          entity->common.sdu_successful_delivery_data,
          (nr_rlc_entity_t *)entity, upper_layer_id);
    }
    cur_wait_list = prev_wait_list->next;
  }
  /* deal with retransmit list */
  while (cur_retransmit_list != NULL
         && sn_compare_tx(entity, cur_retransmit_list->sdu->sn, ack_sn) < 0) {
    /* current segment is acked, free it, indicate successful delivery
     * if fully acked
     */
    nr_rlc_sdu_segment_t *cur = cur_retransmit_list;
    int upper_layer_id = cur->sdu->upper_layer_id;
    int sdu_size = cur->sdu->size;
    cur_retransmit_list = cur_retransmit_list->next;
    /* update buffer status */
    entity->common.bstatus.retx_size -= compute_pdu_header_size(entity, cur)
                                        + cur->size;
    if (nr_rlc_free_sdu_segment(cur)) {
      entity->tx_size -= sdu_size;
      // Retransmit-NACK done: count as successfully transmitted bytes
      entity->common.stats.txsdu_bytes += sdu_size;
      entity->common.sdu_successful_delivery(
          entity->common.sdu_successful_delivery_data,
          (nr_rlc_entity_t *)entity, upper_layer_id);
    }
  }

  new_retransmit_list->next = cur_retransmit_list;

  entity->wait_list       = head_wait_list.next;
  entity->retransmit_list = head_retransmit_list.next;

  if (end_wait_list != NULL) {
    if (end_wait_list == &head_wait_list)
      entity->wait_end = NULL;
    else
      entity->wait_end = end_wait_list;
  }

  /* update tx_next_ack */
  /* 38.322 5.2.3.1.1 says "set TX_Next_Ack equal to the SN of the RLC SDU
   * with the smallest SN, whose SN falls within the range
   * TX_Next_Ack <= SN <= TX_Next and for which a positive acknowledgment
   * has not been received yet.
   */
  /* let's start from highest possible value and go down as needed */
  tx_next_ack = entity->tx_next;
  if (entity->wait_list != NULL
      && sn_compare_tx(entity, entity->wait_list->sdu->sn, tx_next_ack) < 0)
    tx_next_ack = entity->wait_list->sdu->sn;
  if (entity->retransmit_list != NULL
      && sn_compare_tx(entity, entity->retransmit_list->sdu->sn, tx_next_ack) < 0)
    tx_next_ack = entity->retransmit_list->sdu->sn;
  if (sn_compare_tx(entity, ack_sn, tx_next_ack) < 0)
    tx_next_ack = ack_sn;
  entity->tx_next_ack = tx_next_ack;

  return;

err:
  LOG_E(RLC, "error decoding control PDU, discarding\n");

#undef R
}

void nr_rlc_entity_am_recv_pdu(nr_rlc_entity_t *_entity,
                               char *buffer, int size)
{
#define R(d) do { if (nr_rlc_pdu_decoder_in_error(&d)) goto err; } while (0)
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  nr_rlc_pdu_decoder_t decoder;
  nr_rlc_pdu_t *pdu;
  int dc;
  int p = 0;
  int si;
  int sn;
  int so = 0;
  int data_size;
  int is_first;
  int is_last;

  entity->common.stats.rxpdu_pkts++;
  entity->common.stats.rxpdu_bytes += size;

  nr_rlc_pdu_decoder_init(&decoder, buffer, size);
  dc = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);

  if (dc == 0) {
    LOG_D(RLC, "RLC received control PDU\n");
    return process_control_pdu(entity, buffer, size);
  }

  /* data PDU */
  p  = nr_rlc_pdu_decoder_get_bits(&decoder, 1); R(decoder);
  si = nr_rlc_pdu_decoder_get_bits(&decoder, 2); R(decoder);

  is_first = (si & 0x2) == 0;
  is_last = (si & 0x1) == 0;

  if (entity->sn_field_length == 18) {
    nr_rlc_pdu_decoder_get_bits(&decoder, 2); R(decoder);
  }

  sn = nr_rlc_pdu_decoder_get_bits(&decoder, entity->sn_field_length);
  R(decoder);

  if (!is_first) {
    so = nr_rlc_pdu_decoder_get_bits(&decoder, 16); R(decoder);
    if (so == 0) {
      LOG_E(RLC, "%s:%d:%s: warning: discard PDU, bad so\n",
            __FILE__, __LINE__, __FUNCTION__);
      goto discard;
    }
  }

  data_size = size - decoder.byte;

  /* dicard PDU if no data */
  if (data_size <= 0) {
    LOG_D(RLC, "%s:%d:%s: warning: discard PDU, no data\n",
          __FILE__, __LINE__, __FUNCTION__);
    goto discard;
  }

  /* dicard PDU if rx buffer is full */
  if (entity->rx_size + data_size > entity->rx_maxsize) {
    LOG_W(RLC, "%s:%d:%s: warning: discard PDU, RX buffer full\n",
          __FILE__, __LINE__, __FUNCTION__);
    goto discard;
  }

  if (!sn_in_recv_window(entity, sn)) {
    LOG_D(RLC, "%s:%d:%s: warning: discard PDU, sn out of window (sn %d rx_next %d)\n",
          __FILE__, __LINE__, __FUNCTION__,
           sn, entity->rx_next);

    entity->common.stats.rxpdu_ow_pkts++;
    entity->common.stats.rxpdu_ow_bytes += size;

    goto discard;
  }

  /* discard segment if all the bytes of the segment are already there */
  if (segment_already_received(entity, sn, so, data_size)) {
    LOG_D(RLC, "%s:%d:%s: warning: discard PDU, already received\n",
          __FILE__, __LINE__, __FUNCTION__);

    entity->common.stats.rxpdu_dup_pkts++;
    entity->common.stats.rxpdu_dup_bytes += size;

    goto discard;
  }

  /* put in pdu reception list */
  entity->rx_size += data_size;
  pdu = nr_rlc_new_pdu(sn, so, is_first, is_last,
                       buffer + size - data_size, data_size);
  nr_rlc_rx_manager_add_pdu(entity->rx, pdu);

  /* do reception actions (38.322 5.2.3.2.3) */
  LOG_D(RLC, "RLC received PDU sn %d so %d is_first %d is_last %d data_size = %d \n", sn, so, is_first, is_last, data_size);
  reception_actions(entity, pdu);

  if (p) {
    /* 38.322 5.3.4 says status triggering should be delayed
     * until x < rx_highest_status or x >= rx_next + am_window_size.
     * This is not clear (what is x then? we keep the same?). So let's
     * trigger no matter what.
     * todo: delay status triggering properly
     */
    int v = (entity->rx_next + entity->window_size) % entity->sn_modulus;
    entity->status_triggered = 1;
    if (!(sn_compare_rx(entity, sn, entity->rx_highest_status) < 0 ||
          sn_compare_rx(entity, sn, v) >= 0)) {
      LOG_D(RLC, "warning: STATUS triggerered but should be delayed according to specs\n");
    }
  }

  return;

err:
  LOG_W(RLC, "RX error decoding PDU, discarding\n");
  goto discard;

discard:
  if (p)
    entity->status_triggered = 1;

  entity->common.stats.rxpdu_dd_pkts++;
  entity->common.stats.rxpdu_dd_bytes += size;

#undef R
}

/*************************************************************************/
/* TX functions                                                          */
/*************************************************************************/

static int is_window_stalling(nr_rlc_entity_am_t *entity)
{
  /* we are stalling if tx_next is not:
   *   tx_next_ack <= tx_next < tx_next_ack + window_size
   */
  return !(sn_compare_tx(entity, entity->tx_next_ack, entity->tx_next) <= 0 &&
           sn_compare_tx(entity, entity->tx_next,
                         (entity->tx_next_ack + entity->window_size) %
                           entity->sn_modulus) < 0);
}

static void include_poll(nr_rlc_entity_am_t *entity, char *buffer)
{
  /* set the P bit to 1 */
  buffer[0] |= 0x40;

  entity->pdu_without_poll = 0;
  entity->byte_without_poll = 0;

  /* set POLL_SN to highest SN submitted to lower layer
   * (this is: entity->tx_next - 1) (todo: be sure of this)
   */
  entity->poll_sn = (entity->tx_next - 1 + entity->sn_modulus)
                      % entity->sn_modulus;

  /* start/restart t_poll_retransmit */
  entity->t_poll_retransmit_start = entity->t_current;
}

static int check_poll_after_pdu_assembly(nr_rlc_entity_am_t *entity)
{
  int retransmission_buffer_empty;
  int transmission_buffer_empty;

  /* is transmission buffer empty? */
  if (entity->tx_list == NULL)
    transmission_buffer_empty = 1;
  else
    transmission_buffer_empty = 0;

  /* is retransmission buffer empty? */
  if (entity->retransmit_list == NULL)
    retransmission_buffer_empty = 1;
  else
    retransmission_buffer_empty = 0;

  return (transmission_buffer_empty && retransmission_buffer_empty) ||
         is_window_stalling(entity);
}

static int serialize_sdu(nr_rlc_entity_am_t *entity,
                         nr_rlc_sdu_segment_t *sdu, char *buffer, int bufsize,
                         int p)
{
  nr_rlc_pdu_encoder_t encoder;

  /* generate header */
  nr_rlc_pdu_encoder_init(&encoder, buffer, bufsize);

  nr_rlc_pdu_encoder_put_bits(&encoder, 1, 1);             /* D/C: 1 = data */
  nr_rlc_pdu_encoder_put_bits(&encoder, 0, 1);     /* P: reserve, set later */

  nr_rlc_pdu_encoder_put_bits(&encoder, 1-sdu->is_first,1);/* 1st bit of SI */
  nr_rlc_pdu_encoder_put_bits(&encoder, 1-sdu->is_last,1); /* 2nd bit of SI */

  if (entity->sn_field_length == 18)
    nr_rlc_pdu_encoder_put_bits(&encoder, 0, 2);                       /* R */

  nr_rlc_pdu_encoder_put_bits(&encoder, sdu->sdu->sn,
                                        entity->sn_field_length);     /* SN */

  if (!sdu->is_first)
    nr_rlc_pdu_encoder_put_bits(&encoder, sdu->so, 16);               /* SO */

  /* data */
  memcpy(buffer + encoder.byte, sdu->sdu->data + sdu->so, sdu->size);

  if (p)
    include_poll(entity, buffer);

  return encoder.byte + sdu->size;
}

/* resize SDU/SDU segment for the corresponding PDU to fit into 'pdu_size'
 * bytes
 * - modifies SDU/SDU segment to become an SDU segment
 * - returns a new SDU segment covering the remaining data bytes
 */
static nr_rlc_sdu_segment_t *resegment(nr_rlc_sdu_segment_t *sdu,
                                       nr_rlc_entity_am_t *entity,
                                       int pdu_size)
{
  nr_rlc_sdu_segment_t *next;
  int pdu_header_size;
  int over_size;

  sdu->sdu->ref_count++;

  pdu_header_size = compute_pdu_header_size(entity, sdu);

  next = calloc(1, sizeof(nr_rlc_sdu_segment_t));
  AssertFatal(next != NULL, "out of memory\n");
  *next = *sdu;

  over_size = pdu_header_size + sdu->size - pdu_size;

  /* update SDU */
  sdu->size -= over_size;
  sdu->is_last = 0;

  /* create new segment */
  next->size = over_size;
  next->so = sdu->so + sdu->size;
  next->is_first = 0;
  return next;
}

/*************************************************************************/
/* TX functions - status reporting [begin]                               */
/*************************************************************************/

typedef struct {
  /* data for missing bytes */
  int sn_start;    /* set to -1 when no more missing part to report */
  int so_start;
  int sn_end;
  int so_end;
  /* pdu to use for next call to 'next_missing' */
  nr_rlc_pdu_t *next;
} missing_data_t;

/* returns the first RX PDU with rx_next <= sn <= rx_highest_status
 * returns NULL if none is found
 */
static nr_rlc_pdu_t *get_first_rx_pdu(nr_rlc_entity_am_t *entity)
{
  int first_sn = entity->rx_next;
  int last_sn = entity->rx_highest_status;
  int sn = first_sn;

  while (sn != last_sn) {
    nr_rlc_pdu_t *ret = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
    if (ret)
      return ret;
    sn = (sn + 1) % entity->sn_modulus;
  }

  return nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
}

/* find the next RX PDU coming after 'cur'
 * two cases:
 * - there is a PDU with same SN, return it
 * - find PDU with SN <= rx_highest_status if any
 * limit search to rx_highest_status
 */
static nr_rlc_pdu_t *get_next_rx_pdu(nr_rlc_entity_am_t *entity,
                                     nr_rlc_pdu_t *cur)
{
  if (cur->next)
    return cur->next;

  /* don't process past rx_highest_status */
  if (cur->sn == entity->rx_highest_status)
    return NULL;

  int first_sn = (cur->sn + 1) % entity->sn_modulus;
  int last_sn = entity->rx_highest_status;
  int sn = first_sn;

  while (sn != last_sn) {
    nr_rlc_pdu_t *ret = nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
    if (ret)
      return ret;
    sn = (sn + 1) % entity->sn_modulus;
  }

  return nr_rlc_rx_manager_get_pdu_from_sn(entity->rx, sn);
}

/* todo: rewrite this function, too messy */
static missing_data_t next_missing(nr_rlc_entity_am_t *entity,
                                        nr_rlc_pdu_t *cur, int check_head)
{
  missing_data_t ret;
  int cur_max_so;
  int sn;
  int max_so       = 0;
  int last_reached = 0;

  /* special case: missing part before the head of RX list */
  if (check_head) {
    if (cur->sn != entity->rx_next || !cur->is_first) {
      /* don't report if out of reporting window */
      if (sn_compare_rx(entity, entity->rx_highest_status,
                        entity->rx_next) <= 0) {
        ret.sn_start = -1;
        return ret;
      }
      /* the missing part is starting from rx_next(0)
       * going to min of:
       *     - cur->sn(cur->so-1) [if cur->sn is not first]
       *       or (cur->sn-1)(0xffff) [if cur->sn is first]
       *     - (entity->rx_highest_status-1)(0xffff)
       */
      ret.sn_start = entity->rx_next;
      ret.so_start = 0;
      ret.next = cur;
      goto set_end_different_sdu;
    }
  }

next_pdu:
  sn = cur->sn;
  cur_max_so = cur->so + cur->size - 1;
  if (cur_max_so > max_so)
    max_so = cur_max_so;
  last_reached = last_reached | cur->is_last;

  nr_rlc_pdu_t *cur_next = get_next_rx_pdu(entity, cur);

  /* no next? */
  if (cur_next == NULL) {
    /* inform the caller that work is over */
    ret.next = NULL;

    /* already processed => next SDU to rx_highest_status - 1 to be nacked */
    if (cur->data == NULL) {
      ret.sn_start = (cur->sn + 1) % entity->sn_modulus;
      /* don't report if out of reporting window */
      if (sn_compare_rx(entity, entity->rx_highest_status,
                        ret.sn_start) <= 0) {
        ret.sn_start = -1;
        return ret;
      }
      ret.so_start = 0;
      ret.sn_end   = (entity->rx_highest_status - 1 + entity->sn_modulus) %
                        entity->sn_modulus;
      ret.so_end   = 0xffff;
      return ret;
    }
    /* not already processed => all bytes after max_so (if any) then all SDU
     * to rx_highest_status-1 to be nacked
     */
    if (last_reached) {
      ret.sn_start = (cur->sn + 1) % entity->sn_modulus;
      ret.so_start = 0;
    } else {
      ret.sn_start = cur->sn;
      ret.so_start = max_so + 1;
    }
    /* don't report if out of reporting window */
    if (sn_compare_rx(entity, entity->rx_highest_status,
                      ret.sn_start) <= 0) {
      ret.sn_start = -1;
      return ret;
    }
    ret.sn_end   = (entity->rx_highest_status - 1 + entity->sn_modulus) %
                      entity->sn_modulus;
    ret.so_end   = 0xffff;
    return ret;
  }

  cur = cur_next;

  /* no discontinuity in data => process to next PDU */
  if (cur->sn == sn && max_so >= cur->so - 1)
    goto next_pdu;
  if (cur->sn == (sn + 1) % entity->sn_modulus && last_reached &&
      cur->is_first) {
    last_reached = 0;
    max_so       = 0;
    goto next_pdu;
  }

  /* discontinuity in data */

  /* remember where to start from for the next call */
  ret.next = cur;

  /* discontinuity in same SDU */
  if (cur->sn == sn) {
    ret.sn_start = sn;
    /* don't report if out of reporting window */
    if (sn_compare_rx(entity, entity->rx_highest_status,
                      ret.sn_start) <= 0) {
      ret.sn_start = -1;
      return ret;
    }
    ret.so_start = max_so + 1;
    ret.sn_end = sn;
    ret.so_end = cur->so - 1;
    return ret;
  }

  /* discontinuity between different SDUs */
  if (last_reached) {
    ret.sn_start = (sn + 1) % entity->sn_modulus;
    ret.so_start = 0;
  } else {
    ret.sn_start = sn;
    ret.so_start = max_so + 1;
  }
  /* don't report if out of reporting window */
  if (sn_compare_rx(entity, entity->rx_highest_status, ret.sn_start) <= 0) {
    ret.sn_start = -1;
    return ret;
  }

set_end_different_sdu:
  /* if cur is the head of a SDU, then use cur-1 */
  if (cur->is_first) {
    ret.sn_end = (cur->sn - 1 + entity->sn_modulus) % entity->sn_modulus;
    ret.so_end = 0xffff;
  } else {
    ret.sn_end = cur->sn;
    ret.so_end = cur->so - 1;
  }
  /* don't go more than rx_highest_status - 1 */
  if (sn_compare_rx(entity, entity->rx_highest_status, ret.sn_end) <= 0) {
    ret.sn_end = (entity->rx_highest_status - 1 + entity->sn_modulus) %
                      entity->sn_modulus;
    ret.so_end   = 0xffff;
    ret.next = NULL;
  }

  return ret;
}

static int nack_size(nr_rlc_entity_am_t *entity, missing_data_t *m)
{
  int nack_length = 2 + (entity->sn_field_length == 18);

  if (m->sn_start == m->sn_end) {
    /* only nack_sn, no so_start/end, no nack range */
    if (m->so_start == 0 && m->so_end == 0xffff)
      return nack_length;
    /* nack_sn + so_start/end */
    return nack_length + 4;
  }

  /* nack_sn + nack range, no so_start/end */
  if (m->so_start == 0 && m->so_end == 0xffff)
    return nack_length + 1;

  /* nack_sn + so_start/end + nack range */
  return nack_length + 5;
}

/* returns the e1 byte/bit position supposing the encoder points at
 * the beginning of a nack_sn block
 */
static void get_e1_position(nr_rlc_entity_am_t *entity,
                            nr_rlc_pdu_encoder_t *encoder,
                            int *e1_byte, int *e1_bit)
{
  if (entity->sn_field_length == 18) {
    *e1_byte = encoder->byte + 2;
    *e1_bit = 5;
  } else {
    *e1_byte = encoder->byte + 1;
    *e1_bit = 3;
  }
}

/* returns the last nack SN generated, -1 if nothing generated.
 * If there is no more room in the status buffer,
 * will set m->next = NULL (and may serialize
 * less nacks than required by 'm'), also
 * sets *generation_truncated to 1.
 */
static int generate_missing(nr_rlc_entity_am_t *entity,
                            nr_rlc_pdu_encoder_t *encoder,
                            missing_data_t *m, int *e1_byte, int *e1_bit,
                            int *generation_truncated,
                            unsigned char **so_end_address)
{
  int r_bits = entity->sn_field_length == 18 ? 3 : 1;
  int last_nack_generated = -1;
  int sn_start;
  int so_start;
  int sn_end;
  int so_end;
  int sn_count;
  missing_data_t m_nack;
  int e2;
  int e3;

  /* be careful to limit a range to 255 SNs, that is: cut if needed */
  sn_count = (m->sn_end - m->sn_start + entity->sn_modulus)
              % entity->sn_modulus + 1;

  sn_start = m->sn_start;

  while (sn_count) {
    int cur_sn_count = sn_count;
    if (cur_sn_count > 255)
      cur_sn_count = 255;

    /* for first range, so_start is the one of the initial range
     * for the following ones, it is 0
     */
    if (sn_start == m->sn_start) {
      /* first range */
      so_start = m->so_start;
    } else {
      /* following ranges */
      so_start = 0;
    }

    /* for the last range, sn_end/so_end are the ones of the initial range
     * for the previous ones, it is sn_start+254/0xffff
     */
    if (cur_sn_count == sn_count) {
      /* last range */
      sn_end = m->sn_end;
      so_end = m->so_end;
    } else {
      /* previous ranges */
      sn_end = (sn_start + 254) % entity->sn_modulus;
      so_end = 0xffff;
    }

    /* check that there is room for a nack */
    m_nack.sn_start = sn_start;
    m_nack.so_start = so_start;
    m_nack.sn_end = sn_end;
    m_nack.so_end = so_end;
    if (encoder->byte + nack_size(entity, &m_nack) > encoder->size) {
      m->next = NULL;
      *generation_truncated = 1;
      break;
    }

    /* set the previous e1 bit to 1 */
    encoder->buffer[*e1_byte] |= 1 << *e1_bit;

    get_e1_position(entity, encoder, e1_byte, e1_bit);

    if (sn_start == sn_end) {
      if (so_start == 0 && so_end == 0xffff) {
        /* only nack_sn, no so_start/end, no nack range */
        e2 = 0;
        e3 = 0;
      } else {
        /* nack_sn + so_start/end, no nack range */
        e2 = 1;
        e3 = 0;
      }
    } else {
      if (so_start == 0 && so_end == 0xffff) {
        /* nack_sn + nack range, no so_start/end */
        e2 = 0;
        e3 = 1;
      } else {
        /* nack_sn + so_start/end + nack range */
        e2 = 1;
        e3 = 1;
      }
    }

    /* nack_sn */
    nr_rlc_pdu_encoder_put_bits(encoder, sn_start,
                                entity->sn_field_length);
    last_nack_generated = sn_start;
    /* e1 = 0 (set later if needed) */
    nr_rlc_pdu_encoder_put_bits(encoder, 0, 1);
    /* e2 */
    nr_rlc_pdu_encoder_put_bits(encoder, e2, 1);
    /* e3 */
    nr_rlc_pdu_encoder_put_bits(encoder, e3, 1);
    /* r */
    nr_rlc_pdu_encoder_put_bits(encoder, 0, r_bits);
    /* so_start/so_end */
    if (e2) {
      nr_rlc_pdu_encoder_put_bits(encoder, so_start, 16);
      *so_end_address = (unsigned char *)encoder->buffer + encoder->byte;
      nr_rlc_pdu_encoder_put_bits(encoder, so_end, 16);
    } else
      *so_end_address = NULL;
    /* nack range */
    if (e3) {
      nr_rlc_pdu_encoder_put_bits(encoder, cur_sn_count, 8);
      last_nack_generated += cur_sn_count - 1;
    }

    sn_count -= cur_sn_count;
    sn_start = (sn_start + cur_sn_count) % entity->sn_modulus;
  }

  return last_nack_generated;
}

static int generate_status(nr_rlc_entity_am_t *entity, char *buffer, int size)
{
  int                  last_nack;
  int                  ack_sn;
  missing_data_t       m;
  nr_rlc_pdu_t         *cur;
  int                  check_head = 1;
  nr_rlc_pdu_encoder_t encoder;
  int                  e1_byte;
  int                  e1_bit;
  int                  generation_truncated;
  int                  ln;
  unsigned char        *so_end_address = NULL;

  /* if not enough room, do nothing */
  if (size < 3)
    return 0;

  /* initial last_nack is rx_next - 1 */
  last_nack = (entity->rx_next - 1 + entity->sn_modulus) % entity->sn_modulus;

  nr_rlc_pdu_encoder_init(&encoder, buffer, size);

  /* first 3 bytes, ack_sn and e1 will be set later */
  nr_rlc_pdu_encoder_put_bits(&encoder, 0, 8*3);

  cur = get_first_rx_pdu(entity);

  /* store the position of the e1 bit to be set if
   * there is a nack following
   */
  e1_byte = 2;
  e1_bit = entity->sn_field_length == 18 ? 1 : 7;

  while (cur != NULL) {
    m = next_missing(entity, cur, check_head);
    check_head = 0;

    /* stop here if no more nack to report */
    if (m.sn_start == -1)
      break;

    generation_truncated = 0;
    ln = generate_missing(entity, &encoder, &m, &e1_byte, &e1_bit,
                          &generation_truncated, &so_end_address);
    /* remember the last nack put, if any */
    if (ln != -1)
      last_nack = ln;
    /* if generation was truncated and so_end was put, we force its value to
     * 0xffff (end of SDU) because we don't know what missing nack information
     * was supposed to be put, so we nack until the end of the PDU to be sure
     */
    if (generation_truncated && so_end_address != NULL) {
      so_end_address[0] = 0xff;
      so_end_address[1] = 0xff;
    }

    cur = m.next;
  }

  /* put ack_sn, which is last_nack + 1 */
  ack_sn = (last_nack + 1) % entity->sn_modulus;
  if (entity->sn_field_length == 12) {
    buffer[0] = ack_sn >> 8;
    buffer[1] = ack_sn & 255;
  } else {
    buffer[0] = ack_sn >> 14;
    buffer[1] = (ack_sn >> 6) & 255;
    buffer[2] |= (ack_sn & 0x3f) << 2;
  }

  /* reset the trigger */
  entity->status_triggered = 0;

  /* start t_status_prohibit */
  entity->t_status_prohibit_start = entity->t_current;

  entity->common.stats.txpdu_pkts++;
  entity->common.stats.txpdu_bytes += encoder.byte;
  entity->common.stats.txpdu_status_pkts++;
  entity->common.stats.txpdu_status_bytes += encoder.byte;

  return encoder.byte;
}

static int status_to_report(nr_rlc_entity_am_t *entity)
{
  return entity->status_triggered &&
         (entity->t_status_prohibit_start == 0 ||
          entity->t_current - entity->t_status_prohibit_start >
              entity->t_status_prohibit);
}

static int missing_size(nr_rlc_entity_am_t *entity, missing_data_t *m,
                        int *size, int maxsize)
{
  int r_bits = entity->sn_field_length == 18 ? 3 : 1;
  int range_count = 0;
  int sn_start;
  int so_start;
  int sn_end;
  int so_end;
  int sn_count;
  missing_data_t m_nack;

  /* be careful to limit a range to 255 SNs, that is: cut if needed */
  sn_count = (m->sn_end - m->sn_start + entity->sn_modulus)
              % entity->sn_modulus + 1;

  sn_start = m->sn_start;

  while (sn_count) {
    int cur_sn_count = sn_count;
    if (cur_sn_count > 255)
      cur_sn_count = 255;

    /* for first range, so_start is the one of the initial range
     * for the following ones, it is 0
     */
    if (sn_start == m->sn_start) {
      /* first range */
      so_start = m->so_start;
    } else {
      /* following ranges */
      so_start = 0;
    }

    /* for the last range, sn_end/so_end are the ones of the initial range
     * for the previous ones, it is sn_start+254/0xffff
     */
    if (cur_sn_count == sn_count) {
      /* last range */
      sn_end = m->sn_end;
      so_end = m->so_end;
    } else {
      /* previous ranges */
      sn_end = (sn_start + 254) % entity->sn_modulus;
      so_end = 0xffff;
    }

    /* check that there is room for a nack */
    m_nack.sn_start = sn_start;
    m_nack.so_start = so_start;
    m_nack.sn_end = sn_end;
    m_nack.so_end = so_end;
    if (*size + nack_size(entity, &m_nack) > maxsize) {
      m->next = NULL;
      break;
    }

    if (sn_start == sn_end) {
      if (so_start == 0 && so_end == 0xffff) {
        /* only nack_sn, no so_start/end, no nack range */
        *size += (entity->sn_field_length + 3 + r_bits) / 8;
      } else {
        /* nack_sn + so_start/end, no nack range */
        *size += (entity->sn_field_length + 3 + r_bits + 16*2) / 8;
      }
    } else {
      if (so_start == 0 && so_end == 0xffff) {
        /* nack_sn + nack range, no so_start/end */
        *size += (entity->sn_field_length + 3 + r_bits + 8) / 8;
      } else {
        /* nack_sn + so_start/end + nack range */
        *size += (entity->sn_field_length + 3 + r_bits + 16*2 + 8) / 8;
      }
    }

    sn_count -= cur_sn_count;
    sn_start = (sn_start + cur_sn_count) % entity->sn_modulus;
    range_count++;
  }

  return range_count;
}

static int status_size(nr_rlc_entity_am_t *entity, int maxsize)
{
  missing_data_t  m;
  nr_rlc_pdu_t    *cur;
  int             nack_count = 0;
  int             size;

  /* if not enough room, do nothing */
  if (maxsize < 3)
    return 0;

  /* minimum 3 bytes */
  size = 3;

  cur = get_first_rx_pdu(entity);

  while (cur != NULL) {
    m = next_missing(entity, cur, nack_count == 0);

    /* stop here if no more nack to report */
    if (m.sn_start == -1)
      break;

    nack_count += missing_size(entity, &m, &size, maxsize);

    cur = m.next;
  }

  return size;
}

/*************************************************************************/
/* TX functions - status reporting [end]                                 */
/*************************************************************************/

static int generate_retx_pdu(nr_rlc_entity_am_t *entity, char *buffer,
                             int size)
{
  nr_rlc_sdu_segment_t *sdu;
  int pdu_header_size;
  int pdu_size;
  int p;

  sdu = entity->retransmit_list;

  pdu_header_size = compute_pdu_header_size(entity, sdu);

  /* not enough room for at least one byte of data? do nothing */
  if (pdu_header_size + 1 > size)
    return 0;

  entity->retransmit_list = entity->retransmit_list->next;

  sdu->next = NULL;

  pdu_size = pdu_header_size + sdu->size;

  /* update buffer status */
  entity->common.bstatus.retx_size -= pdu_size;

  /* segment if necessary */
  if (pdu_size > size) {
    nr_rlc_sdu_segment_t *next_sdu;
    next_sdu = resegment(sdu, entity, size);
    /* put the second SDU back at the head of the retransmit list */
    next_sdu->next = entity->retransmit_list;
    entity->retransmit_list = next_sdu;
    /* update buffer status */
    entity->common.bstatus.retx_size += compute_pdu_header_size(entity, next_sdu)
                                        + next_sdu->size;

    entity->common.stats.txpdu_segmented++;
  }

  /* put SDU/SDU segment in the wait list */
  /* speedup: check end of wait list, maybe the new sdu comes after */
  if (entity->wait_end == NULL
      || sn_compare_tx(entity, sdu->sdu->sn, entity->wait_end->sdu->sn) > 0
      || (sn_compare_tx(entity, sdu->sdu->sn, entity->wait_end->sdu->sn) == 0
          && sdu->so > entity->wait_end->so))
    nr_rlc_sdu_segment_list_append(&entity->wait_list, &entity->wait_end, sdu);
  else {
    entity->wait_list = nr_rlc_tx_sdu_segment_list_add(entity,
                            entity->wait_list, sdu);
    if (entity->wait_list->next == NULL)
      entity->wait_end = entity->wait_list;
  }

  p = check_poll_after_pdu_assembly(entity);

  if (entity->force_poll) {
    p = 1;
    entity->force_poll = 0;
  }

  LOG_D(RLC, "RLC TX: sending sdu sn = %d is_first = %d, is_last = %d size = %d\n", sdu->sdu->sn,  sdu->is_first, sdu->is_last, size);

  int ret_size = serialize_sdu(entity, sdu, buffer, size, p);
  entity->common.stats.txpdu_pkts++;
  entity->common.stats.txpdu_bytes += ret_size;
  entity->common.stats.txpdu_retx_pkts++;
  entity->common.stats.txpdu_retx_bytes += ret_size;

  return ret_size;
//  return serialize_sdu(entity, sdu, buffer, size, p);
}

static int generate_tx_pdu(nr_rlc_entity_am_t *entity, char *buffer, int size)
{
  nr_rlc_sdu_segment_t *sdu;
  int pdu_header_size;
  int pdu_size;
  int p;

  /* sn out of window (that is: we have window stalling)? do nothing */
  if (is_window_stalling(entity)) {
    LOG_D(RLC, "Abort transmit due to window stall\n");
    return 0;
  }

  if (entity->tx_list == NULL) {
    return 0;
  }

  sdu = entity->tx_list;

  pdu_header_size = compute_pdu_header_size(entity, sdu);

  /* not enough room for at least one byte of data? do nothing */
  if (pdu_header_size + 1 > size)
    return 0;

  entity->tx_list = entity->tx_list->next;
  if (entity->tx_list == NULL)
    entity->tx_end = NULL;

  sdu->next = NULL;

  pdu_size = pdu_header_size + sdu->size;

  /* update buffer status */
  entity->common.bstatus.tx_size -= pdu_size;

  /* assign SN to SDU */
  if (sdu->sdu->sn == -1) {
    sdu->sdu->sn = entity->tx_next;
    entity->tx_next = (entity->tx_next + 1) % entity->sn_modulus;
  }

  /* segment if necessary */
  if (pdu_size > size) {
    LOG_D(RLC, "Segmentation (header %d + data %d) / (%d)\n", pdu_header_size, size - pdu_header_size, pdu_size - pdu_header_size);
    nr_rlc_sdu_segment_t *next_sdu;
    next_sdu = resegment(sdu, entity, size);
    /* put the second SDU back at the head of the TX list */
    next_sdu->next = entity->tx_list;
    entity->tx_list = next_sdu;
    if (entity->tx_end == NULL)
      entity->tx_end = entity->tx_list;

    entity->common.stats.txpdu_segmented++;
    /* update buffer status */
    entity->common.bstatus.tx_size += compute_pdu_header_size(entity, next_sdu)
                                      + next_sdu->size;
  }

  /* put SDU/SDU segment in the wait list */
  /* speedup: check end of wait list, probably the new sdu comes after */
  if (entity->wait_end == NULL
      || sn_compare_tx(entity, sdu->sdu->sn, entity->wait_end->sdu->sn) > 0
      || (sn_compare_tx(entity, sdu->sdu->sn, entity->wait_end->sdu->sn) == 0
          && sdu->so > entity->wait_end->so))
    nr_rlc_sdu_segment_list_append(&entity->wait_list, &entity->wait_end, sdu);
  else {
    entity->wait_list = nr_rlc_tx_sdu_segment_list_add(entity,
                            entity->wait_list, sdu);
    if (entity->wait_list->next == NULL)
      entity->wait_end = entity->wait_list;
  }

  /* polling actions for a new PDU */
  entity->pdu_without_poll++;
  entity->byte_without_poll += sdu->size;
  if ((entity->poll_pdu != -1 &&
       entity->pdu_without_poll >= entity->poll_pdu) ||
      (entity->poll_byte != -1 &&
       entity->byte_without_poll >= entity->poll_byte))
    p = 1;
  else
    p = check_poll_after_pdu_assembly(entity);

  if (entity->force_poll) {
    p = 1;
    entity->force_poll = 0;
  }
  int ret_size = serialize_sdu(entity, sdu, buffer, size, p);

  entity->common.stats.txpdu_pkts++;
  entity->common.stats.txpdu_bytes += ret_size;

  /* No need to 'zero' time-of-arrival;
  Segmented packets do need to be duplicated in time-sensitive use cases */
  if (entity->common.avg_time_is_on) {
    uint64_t time_now = time_average_now();
    uint64_t waited_time = time_now - sdu->sdu->time_of_arrival;
    time_average_add(entity->common.txsdu_avg_time_to_tx, time_now, waited_time);
  }

  LOG_D(RLC,
        "RLC TX: sending sdu sn = %d is_first = %d, is_last = %d header size = %d data size = %d\n",
        sdu->sdu->sn,
        sdu->is_first,
        sdu->is_last,
        pdu_header_size,
        size - pdu_header_size);

  return ret_size;
//  return serialize_sdu(entity, sdu, buffer, size, p);
}

nr_rlc_entity_buffer_status_t nr_rlc_entity_am_buffer_status(
    nr_rlc_entity_t *_entity, int maxsize)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  nr_rlc_entity_buffer_status_t ret;

  if (status_to_report(entity))
    ret.status_size = status_size(entity, maxsize);
  else
    ret.status_size = 0;

  ret.tx_size = entity->common.bstatus.tx_size;
  ret.retx_size = entity->common.bstatus.retx_size;

  return ret;
}

int nr_rlc_entity_am_generate_pdu(nr_rlc_entity_t *_entity,
                                  char *buffer, int size)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  int ret;

  if (status_to_report(entity)) {
    ret = generate_status(entity, buffer, size);
    if (ret != 0) {
      LOG_D(RLC, "RLC transmit status pdu PDU\n");
      return ret;
    }
  }

  if (entity->retransmit_list != NULL) {
    ret = generate_retx_pdu(entity, buffer, size);
    if (ret != 0) {
      LOG_D(RLC, "RLC retransmit PDU\n");
      return ret;
    }
  }

  return generate_tx_pdu(entity, buffer, size);
}

/*************************************************************************/
/* SDU RX functions                                                      */
/*************************************************************************/

void nr_rlc_entity_am_recv_sdu(nr_rlc_entity_t *_entity,
                               char *buffer, int size,
                               int sdu_id)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  nr_rlc_sdu_segment_t *sdu;

  entity->common.stats.rxsdu_pkts++;

  AssertFatal(size <= NR_SDU_MAX, "Fatal: SDU size too big (%d bytes)\n", size);

  /* log SDUs rejected at most once per 1280 ms (128 frames @ 10 ms, to match MAC cadence) */
  if (entity->sdu_rejected != 0
      && entity->t_current > entity->t_log_buffer_full + 1280) {
    LOG_E(RLC, "%d SDU rejected, SDU buffer full\n", entity->sdu_rejected);
    entity->sdu_rejected = 0;
    entity->t_log_buffer_full = entity->t_current;
  }

  if (entity->tx_size + size > entity->tx_maxsize) {
    entity->sdu_rejected++;
    entity->common.stats.txpdu_dd_pkts++;
    entity->common.stats.txpdu_dd_bytes += size;
    return;
  }

  entity->tx_size += size;
  // SDU received: Count as arrival bytes
  entity->common.stats.rxsdu_bytes += size;

  sdu = nr_rlc_new_sdu(buffer, size, sdu_id);

  LOG_D(RLC, "Created new RLC SDU and append it to the RLC list \n");

  nr_rlc_sdu_segment_list_append(&entity->tx_list, &entity->tx_end, sdu);

  /* update buffer status */
  entity->common.bstatus.tx_size += compute_pdu_header_size(entity, sdu)
                                    + sdu->size;

  if (entity->common.avg_time_is_on)
    sdu->sdu->time_of_arrival = time_average_now();
}

/*************************************************************************/
/* time/timers                                                           */
/*************************************************************************/

static void check_t_poll_retransmit(nr_rlc_entity_am_t *entity)
{
  nr_rlc_sdu_segment_t *cur;

  /* 38.322 5.3.3.4 */
  /* did t_poll_retransmit expire? */
  if (entity->t_poll_retransmit_start == 0 ||
      entity->t_current <= entity->t_poll_retransmit_start +
                               entity->t_poll_retransmit)
    return;

  /* stop timer */
  entity->t_poll_retransmit_start = 0;

  /* 38.322 5.3.3.4 says:
   *
   *     - include a poll in a RLC data PDU as described in section 5.3.3.2
   *
   * That does not seem to be conditional. So we forcefully will send
   * a poll as soon as we generate a PDU.
   * Hopefully this interpretation is correct. In the worst case we generate
   * more polling than necessary, but it's not a big deal. When
   * 't_poll_retransmit' expires it means we didn't receive a status report,
   * meaning a bad radio link, so things are quite bad at this point and
   * asking again for a poll won't hurt much more.
   */
  entity->force_poll = 1;

  LOG_D(RLC, "%s:%d:%s: warning: t_poll_retransmit expired\n",
        __FILE__, __LINE__, __FUNCTION__);

  /* do we meet conditions of 38.322 5.3.3.4? */
  if (!check_poll_after_pdu_assembly(entity))
    return;

  /* retransmit the SDU at the head of wait list, this is the case
   * "consider any RLC SDU which has not been positively acknowledged for
   * retransmission" of 36.322 5.3.3.4.
   * We don't search for the highest SN, it's simpler to just take the head
   * of wait list. This can be changed if needed.
   */
  cur = entity->wait_list;

  /* todo: do we need to for check cur == NULL?
   * It seems that no, the wait list should not be empty here, but not sure.
   */

  /* increase retx count. Don't care about segmentation, so maybe we
   * increase too much.
   */
  cur->sdu->retx_count++;

  int sdu_retx_count = cur->sdu->retx_count;
  int retransmit_sn = cur->sdu->sn;

  /* 38.322 says "SDU", not "SDU segment", so let's retransmit all
   * SDU segments with the retransmit SN in the wait list.
   */
  do {
    entity->wait_list = cur->next;
    if (entity->wait_list == NULL)
       entity->wait_end = NULL;

    /* update buffer status */
    entity->common.bstatus.retx_size += compute_pdu_header_size(entity, cur)
                                        + cur->size;

    LOG_D(RLC, "put sn %d so %d size %d in retx list (retx_count %d)\n",
          cur->sdu->sn, cur->so, cur->size, cur->sdu->retx_count);

    /* put in retransmit list */
    entity->retransmit_list = nr_rlc_tx_sdu_segment_list_add(entity,
                                  entity->retransmit_list, cur);

    cur = entity->wait_list;
  } while (cur != NULL && cur->sdu->sn == retransmit_sn);

  /* report max RETX reached for all retx_count >= max_retx_threshold
   * (specs say to report if retx_count == max_retx_threshold).
   * Upper layers should react (radio link failure), so no big deal.
   * Because of segmentation, we may report too early/too late, not
   * very clear. To refine if needed.
   */
  if (sdu_retx_count >= entity->max_retx_threshold)
    entity->common.max_retx_reached(entity->common.max_retx_reached_data,
                                    (nr_rlc_entity_t *)entity);
}

static void check_t_reassembly(nr_rlc_entity_am_t *entity)
{
  int sn;

  /* is t_reassembly running and if yes has it expired? */
  if (entity->t_reassembly_start == 0 ||
      entity->t_current <= entity->t_reassembly_start + entity->t_reassembly)
    return;

  /* stop timer */
  entity->t_reassembly_start = 0;

  LOG_D(RLC, "%s:%d:%s: t_reassembly expired\n",
        __FILE__, __LINE__, __FUNCTION__);

  /* update RX_Highest_Status */
  sn = entity->rx_next_status_trigger;
  while (sdu_delivered(entity, sn))
    sn = (sn + 1) % entity->sn_modulus;
  entity->rx_highest_status = sn;

  /* trigger status report */
  entity->status_triggered = 1;

  if (sn_compare_rx(entity, entity->rx_next_highest,
                    (entity->rx_highest_status+1) % entity->sn_modulus) > 0 ||
      (entity->rx_next_highest ==
         (entity->rx_highest_status+1) % entity->sn_modulus &&
       sdu_has_missing_bytes(entity, entity->rx_highest_status))) {
    entity->t_reassembly_start = entity->t_current;
    entity->rx_next_status_trigger = entity->rx_next_highest;
  }
}

void nr_rlc_entity_am_set_time(nr_rlc_entity_t *_entity, uint64_t now)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;

  entity->t_current = now;

  check_t_poll_retransmit(entity);

  check_t_reassembly(entity);
}

/*************************************************************************/
/* discard/re-establishment/delete                                       */
/*************************************************************************/

void nr_rlc_entity_am_discard_sdu(nr_rlc_entity_t *_entity, int sdu_id)
{
  /* implements 38.322 5.4 */
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  nr_rlc_sdu_segment_t head;
  nr_rlc_sdu_segment_t *cur;
  nr_rlc_sdu_segment_t *prev;

  head.next = entity->tx_list;
  cur = entity->tx_list;
  prev = &head;

  while (cur != NULL && cur->sdu->upper_layer_id != sdu_id) {
    prev = cur;
    cur = cur->next;
  }

  /* if sdu_id not found or some bytes have already been 'PDU-ized'
   * then do nothing
   */
  if (cur == NULL || !cur->is_first || !cur->is_last)
    return;

  /* remove SDU from tx_list */
  prev->next = cur->next;
  entity->tx_list = head.next;
  if (entity->tx_end == cur) {
    if (prev != &head)
      entity->tx_end = prev;
    else
      entity->tx_end = NULL;
  }

  /* update buffer status */
  entity->common.bstatus.tx_size -= compute_pdu_header_size(entity, cur)
                                    + cur->size;

  entity->tx_size -= cur->sdu->size;

  /* Uncomment to assert if SDU are ever discarded */
  // assert(0 != 0 && "[RLC-TRAP] SDU discard should never be reached!");

  nr_rlc_free_sdu_segment(cur);
}

static void clear_entity(nr_rlc_entity_am_t *entity)
{
  entity->rx_next                = 0;
  entity->rx_next_status_trigger = 0;
  entity->rx_highest_status      = 0;
  entity->rx_next_highest        = 0;

  entity->status_triggered = 0;

  entity->tx_next           = 0;
  entity->tx_next_ack       = 0;
  entity->poll_sn           = 0;
  entity->pdu_without_poll  = 0;
  entity->byte_without_poll = 0;
  entity->force_poll        = 0;

  entity->t_current = 0;

  entity->t_log_buffer_full = 0;
  entity->sdu_rejected      = 0;

  entity->t_poll_retransmit_start = 0;
  entity->t_reassembly_start      = 0;
  entity->t_status_prohibit_start = 0;

  nr_rlc_clear_rx_manager(entity->rx);
  entity->rx_size = 0;

  nr_rlc_free_sdu_segment_list(entity->tx_list);
  nr_rlc_free_sdu_segment_list(entity->wait_list);
  nr_rlc_free_sdu_segment_list(entity->retransmit_list);

  entity->tx_list         = NULL;
  entity->tx_end          = NULL;
  entity->tx_size         = 0;

  entity->wait_list       = NULL;
  entity->wait_end        = NULL;

  entity->retransmit_list = NULL;

  entity->common.bstatus.tx_size   = 0;
  entity->common.bstatus.retx_size = 0;
}

void nr_rlc_entity_am_reestablishment(nr_rlc_entity_t *_entity)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  clear_entity(entity);
}

void nr_rlc_entity_am_delete(nr_rlc_entity_t *_entity)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  clear_entity(entity);
  nr_rlc_free_rx_manager(entity->rx);
  time_average_free(entity->common.txsdu_avg_time_to_tx);
  free(entity);
}

int nr_rlc_entity_am_available_tx_space(nr_rlc_entity_t *_entity)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  return entity->tx_maxsize - entity->tx_size;
}

int nr_rlc_entity_am_tx_list_occupancy(nr_rlc_entity_t *_entity)
{
  nr_rlc_entity_am_t *entity = (nr_rlc_entity_am_t *)_entity;
  return entity->tx_size;
}
