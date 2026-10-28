/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "PHY/CODING/nrSmallBlock/nr_small_block_defs.h"
#include "PHY/NR_TRANSPORT/nr_ulsch.h"

typedef struct {
  uint64_t payload;
  int crc_result; // 0 = pass 1 = fail 2 = not present
} nr_uci_decoder_result_t;

/*
 * Unpacks the first n_bits bits of a UCI payload held in a 64-bit integer into a byte array, LSB first:
 *   out[0] = bits 0..7, out[1] = bits 8..15, ...
 * In the last byte only the valid bits are kept, the unused upper bits are left as 0.
 *
 * in:      payload word, bit 0 = first UCI bit of this field
 *          on return it is shifted right by exactly n_bits, so the bits left in it are the ones
 *          following this field (callers decoding several fields packed in one word rely on this)
 * out:     output buffer of n_bytes bytes
 */
void set_uci_payload(uint64_t *in, int n_bits, int n_bytes, uint8_t *out, int frame, int slot)
{
  AssertFatal(n_bits >= 1 && n_bits <= 64, "n_bits out of range: %d\n", n_bits);
  for (int i = 0; i < n_bytes; i++) {
    if (i == n_bytes - 1) {
      // last byte: may be partially filled, keep only the remaining valid bits
      int bit_left = n_bits - (i * 8);  // remaining bits, 1–8 on last iteration
      uint8_t mask = (bit_left == 8) ? 0xFF : (uint8_t)((1u << bit_left) - 1);
      out[i] = (uint8_t)(*in & mask);
      *in >>= bit_left;  // shift only the remaining bits on last iteration
    } else {
      // full byte: take the 8 least significant bits, then drop them
      out[i] = (uint8_t)(*in & 0xFF);
      *in >>= 8;
    }
    LOG_D(NR_PHY, "[UCI] %d.%d payload byte[%d] = 0x%02x\n", frame, slot, i, out[i]);
  }
}

static nr_uci_decoder_result_t uci_on_pusch_decoded_helper(const int16_t *llrs, int n_bits, int E, int Qm)
{
  nr_uci_decoder_result_t res = {0};
  if (n_bits > 11) {
    // Polar decoding
    AssertFatal(n_bits <= 64, "Polar decoded seems to be capped to 64 bits\n");
    int decoderState = polar_decoder_int16(llrs, &res.payload, 0, NR_POLAR_UCI_PUSCH_MESSAGE_TYPE, n_bits, E);
    res.payload = reverse_bits(res.payload, n_bits);
    res.crc_result = decoderState > 0;
  } else {
    int N = n_bits == 1 ? Qm : (n_bits == 2 ? 3 * Qm : 32); // 5.3.3 of 38.212
    int8_t decoder_input[N];
    nr_rate_matching_smallcodeblock_rx(llrs, E, N, decoder_input);
    res.crc_result = 2;
    if (n_bits > 2)
      res.payload = decodeSmallBlock(decoder_input, n_bits);
    else
      res.payload = decode_1_2_uci_bit(decoder_input, n_bits, Qm);
  }
  return res;
}

void nr_uci_decoding(PHY_VARS_gNB *phy_vars_gNB, nfapi_nr_uci_indication_t *uci_ind, int nb_pusch, const int *ULSCH_ids, int sfn, int slot)
{
  for (uint8_t pusch_id = 0; pusch_id < nb_pusch; pusch_id++) {
    uint8_t ULSCH_id = ULSCH_ids[pusch_id];
    NR_gNB_ULSCH_t *ulsch = &phy_vars_gNB->ulsch[ULSCH_id];
    nfapi_nr_pusch_pdu_t *pusch_pdu = &ulsch->harq_process->ulsch_pdu;
    if (!(pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI))
      continue;
    nfapi_nr_pusch_uci_t *pusch_uci = &pusch_pdu->pusch_uci;
    NR_gNB_PUSCH *pusch = &phy_vars_gNB->pusch_vars[ULSCH_id];
    uci_ind->sfn = sfn;
    uci_ind->slot = slot;
    nfapi_nr_uci_t *uci = uci_ind->uci_list + uci_ind->num_ucis;
    uci->pdu_type = NFAPI_NR_UCI_PUSCH_PDU_TYPE;
    uci->pdu_size = sizeof(nfapi_nr_uci_pusch_pdu_t);
    nfapi_nr_uci_pusch_pdu_t *pusch_ind = &uci->pusch_pdu;
    pusch_ind->handle = pusch_pdu->handle;
    pusch_ind->rnti = pusch_pdu->rnti;
    pusch_ind->ul_cqi = get_pusch_cqi(pusch, sfn, slot);
    pusch_ind->timing_advance = get_pusch_ta(phy_vars_gNB, get_phy_stats(phy_vars_gNB, ulsch->rnti), pusch, sfn, slot);
    pusch_ind->rssi = pusch->DTX ? 0 : get_pusch_rssi(pusch, pusch_pdu->param_v4.numSpatialStreamIndices);
    pusch_ind->pduBitmap = 0;
    int bytes = 0;
    if (pusch_uci->harq_ack_bit_length > 0) {
      pusch_ind->pduBitmap |= 2; // Bit 1: HARQ
      nr_uci_decoder_result_t ack_res = uci_on_pusch_decoded_helper(pusch->ack_llrs,
                                                                    pusch_uci->harq_ack_bit_length,
                                                                    pusch->uci_info.E_uci_ACK_actual,
                                                                    pusch_pdu->qam_mod_order);
      pusch_ind->harq.harq_crc = ack_res.crc_result;
      pusch_ind->harq.harq_bit_len = pusch_uci->harq_ack_bit_length;
      bytes = (pusch_uci->harq_ack_bit_length + 7) / 8;
      pusch_ind->harq.harq_payload = calloc_or_fail(bytes, sizeof(uint8_t));
      set_uci_payload(&ack_res.payload, pusch_uci->harq_ack_bit_length, bytes, pusch_ind->harq.harq_payload, sfn, slot);
    }
    if (pusch_uci->csi_part1_bit_length > 0) {
      pusch_ind->pduBitmap |= 4; // Bit 2: CSI Part 1
      nr_uci_decoder_result_t csi1_res = uci_on_pusch_decoded_helper(pusch->csi1_llrs,
                                                                     pusch_uci->csi_part1_bit_length,
                                                                     pusch->uci_info.E_uci_CSI1,
                                                                     pusch_pdu->qam_mod_order);
      pusch_ind->csi_part1.csi_part1_crc = csi1_res.crc_result;
      pusch_ind->csi_part1.csi_part1_bit_len = pusch_uci->csi_part1_bit_length;
      bytes = (pusch_uci->csi_part1_bit_length + 7) / 8;
      pusch_ind->csi_part1.csi_part1_payload = calloc_or_fail(bytes, sizeof(uint8_t));
      set_uci_payload(&csi1_res.payload, pusch_uci->csi_part1_bit_length, bytes, pusch_ind->csi_part1.csi_part1_payload, sfn, slot);
    }
    if (pusch_uci->csi_part2_bit_length > 0) {
      pusch_ind->pduBitmap |= 8; // Bit 3: CSI Part 2
      nr_uci_decoder_result_t csi2_res = uci_on_pusch_decoded_helper(pusch->csi2_llrs,
                                                                     pusch_uci->csi_part2_bit_length,
                                                                     pusch->uci_info.E_uci_CSI2,
                                                                     pusch_pdu->qam_mod_order);
      pusch_ind->csi_part2.csi_part2_crc = csi2_res.crc_result;
      pusch_ind->csi_part2.csi_part2_bit_len = pusch_uci->csi_part2_bit_length;
      bytes = (pusch_uci->csi_part2_bit_length + 7) / 8;
      pusch_ind->csi_part2.csi_part2_payload = calloc_or_fail(bytes, sizeof(uint8_t));
      set_uci_payload(&csi2_res.payload, pusch_uci->csi_part2_bit_length, bytes, pusch_ind->csi_part2.csi_part2_payload, sfn, slot);
    }
    uci_ind->num_ucis += 1;
  }
}
