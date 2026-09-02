/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "nr_fapi_p7_utils.h"

#include <malloc.h>

static bool eq_dl_tti_beamforming(const nfapi_nr_tx_precoding_and_beamforming_t *a,
                                  const nfapi_nr_tx_precoding_and_beamforming_t *b)
{
  EQ(a->num_prgs, b->num_prgs);
  EQ(a->prg_size, b->prg_size);
  EQ(a->dig_bf_interfaces, b->dig_bf_interfaces);
  for (int prg = 0; prg < a->num_prgs; ++prg) {
    EQ(a->prgs_list[prg].pm_idx, b->prgs_list[prg].pm_idx);
    for (int dbf_if = 0; dbf_if < a->dig_bf_interfaces; ++dbf_if) {
      EQ(a->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx, b->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx);
    }
  }

  return true;
}

static bool eq_dl_tti_request_pdcch_pdu(const nfapi_nr_dl_tti_pdcch_pdu_rel15_t *a, const nfapi_nr_dl_tti_pdcch_pdu_rel15_t *b)
{
  EQ(a->BWPSize, b->BWPSize);
  EQ(a->BWPStart, b->BWPStart);
  EQ(a->SubcarrierSpacing, b->SubcarrierSpacing);
  EQ(a->CyclicPrefix, b->CyclicPrefix);
  EQ(a->StartSymbolIndex, b->StartSymbolIndex);
  EQ(a->DurationSymbols, b->DurationSymbols);
  for (int fdr_idx = 0; fdr_idx < 6; ++fdr_idx) {
    EQ(a->FreqDomainResource[fdr_idx], b->FreqDomainResource[fdr_idx]);
  }
  EQ(a->CceRegMappingType, b->CceRegMappingType);
  EQ(a->RegBundleSize, b->RegBundleSize);
  EQ(a->InterleaverSize, b->InterleaverSize);
  EQ(a->CoreSetType, b->CoreSetType);
  EQ(a->ShiftIndex, b->ShiftIndex);
  EQ(a->precoderGranularity, b->precoderGranularity);
  EQ(a->numDlDci, b->numDlDci);
  for (int dl_dci = 0; dl_dci < a->numDlDci; ++dl_dci) {
    const nfapi_nr_dl_dci_pdu_t *a_dci_pdu = &a->dci_pdu[dl_dci];
    const nfapi_nr_dl_dci_pdu_t *b_dci_pdu = &b->dci_pdu[dl_dci];
    EQ(a_dci_pdu->RNTI, b_dci_pdu->RNTI);
    EQ(a_dci_pdu->ScramblingId, b_dci_pdu->ScramblingId);
    EQ(a_dci_pdu->ScramblingRNTI, b_dci_pdu->ScramblingRNTI);
    EQ(a_dci_pdu->CceIndex, b_dci_pdu->CceIndex);
    EQ(a_dci_pdu->AggregationLevel, b_dci_pdu->AggregationLevel);
    EQ(eq_dl_tti_beamforming(&a_dci_pdu->precodingAndBeamforming, &b_dci_pdu->precodingAndBeamforming), true);
    EQ(a_dci_pdu->beta_PDCCH_1_0, b_dci_pdu->beta_PDCCH_1_0);
    EQ(a_dci_pdu->powerControlOffsetSS, b_dci_pdu->powerControlOffsetSS);
    EQ(a_dci_pdu->PayloadSizeBits, b_dci_pdu->PayloadSizeBits);
    for (int i = 0; i < 8; ++i) {
      // The parameter itself always has 8 positions, no need to calculate how many bytes the payload actually occupies
      EQ(a_dci_pdu->Payload[i], b_dci_pdu->Payload[i]);
    }
  }
  return true;
}

static bool eq_dl_tti_request_pdsch_pdu(const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *a, const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *b)
{
  EQ(a->pduBitmap, b->pduBitmap);
  EQ(a->rnti, b->rnti);
  EQ(a->pduIndex, b->pduIndex);
  EQ(a->BWPSize, b->BWPSize);
  EQ(a->BWPStart, b->BWPStart);
  EQ(a->SubcarrierSpacing, b->SubcarrierSpacing);
  EQ(a->CyclicPrefix, b->CyclicPrefix);
  EQ(a->NrOfCodewords, b->NrOfCodewords);
  for (int cw = 0; cw < a->NrOfCodewords; ++cw) {
    EQ(a->targetCodeRate[cw], b->targetCodeRate[cw]);
    EQ(a->qamModOrder[cw], b->qamModOrder[cw]);
    EQ(a->mcsIndex[cw], b->mcsIndex[cw]);
    EQ(a->mcsTable[cw], b->mcsTable[cw]);
    EQ(a->rvIndex[cw], b->rvIndex[cw]);
    EQ(a->TBSize[cw], b->TBSize[cw]);
  }
  EQ(a->dataScramblingId, b->dataScramblingId);
  EQ(a->nrOfLayers, b->nrOfLayers);
  EQ(a->transmissionScheme, b->transmissionScheme);
  EQ(a->refPoint, b->refPoint);
  EQ(a->dlDmrsSymbPos, b->dlDmrsSymbPos);
  EQ(a->dmrsConfigType, b->dmrsConfigType);
  EQ(a->dlDmrsScramblingId, b->dlDmrsScramblingId);
  EQ(a->SCID, b->SCID);
  EQ(a->numDmrsCdmGrpsNoData, b->numDmrsCdmGrpsNoData);
  EQ(a->dmrsPorts, b->dmrsPorts);
  EQ(a->resourceAlloc, b->resourceAlloc);
  for (int i = 0; i < 36; ++i) {
    EQ(a->rbBitmap[i], b->rbBitmap[i]);
  }
  EQ(a->rbStart, b->rbStart);
  EQ(a->rbSize, b->rbSize);
  EQ(a->VRBtoPRBMapping, b->VRBtoPRBMapping);
  EQ(a->StartSymbolIndex, b->StartSymbolIndex);
  EQ(a->NrOfSymbols, b->NrOfSymbols);
  EQ(a->PTRSPortIndex, b->PTRSPortIndex);
  EQ(a->PTRSTimeDensity, b->PTRSTimeDensity);
  EQ(a->PTRSFreqDensity, b->PTRSFreqDensity);
  EQ(a->PTRSReOffset, b->PTRSReOffset);
  EQ(a->nEpreRatioOfPDSCHToPTRS, b->nEpreRatioOfPDSCHToPTRS);
  EQ(eq_dl_tti_beamforming(&a->precodingAndBeamforming, &b->precodingAndBeamforming), true);
  EQ(a->powerControlOffset, b->powerControlOffset);
  EQ(a->powerControlOffsetSS, b->powerControlOffsetSS);
  EQ(a->isLastCbPresent, b->isLastCbPresent);
  EQ(a->isInlineTbCrc, b->isInlineTbCrc);
  EQ(a->dlTbCrc, b->dlTbCrc);
  EQ(a->maintenance_parms_v3.ldpcBaseGraph, b->maintenance_parms_v3.ldpcBaseGraph);
  EQ(a->maintenance_parms_v3.tbSizeLbrmBytes, b->maintenance_parms_v3.tbSizeLbrmBytes);
  return true;
}

static bool eq_dl_tti_request_csi_rs_pdu(const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *a, const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *b)
{
  EQ(a->bwp_size, b->bwp_size);
  EQ(a->bwp_start, b->bwp_start);
  EQ(a->subcarrier_spacing, b->subcarrier_spacing);
  EQ(a->cyclic_prefix, b->cyclic_prefix);
  EQ(a->start_rb, b->start_rb);
  EQ(a->nr_of_rbs, b->nr_of_rbs);
  EQ(a->csi_type, b->csi_type);
  EQ(a->row, b->row);
  EQ(a->freq_domain, b->freq_domain);
  EQ(a->symb_l0, b->symb_l0);
  EQ(a->symb_l1, b->symb_l1);
  EQ(a->cdm_type, b->cdm_type);
  EQ(a->freq_density, b->freq_density);
  EQ(a->scramb_id, b->scramb_id);
  EQ(a->power_control_offset, b->power_control_offset);
  EQ(a->power_control_offset_ss, b->power_control_offset_ss);
  EQ(eq_dl_tti_beamforming(&a->precodingAndBeamforming, &b->precodingAndBeamforming), true);
  return true;
}

static bool eq_dl_tti_request_ssb_pdu(const nfapi_nr_dl_tti_ssb_pdu_rel15_t *a, const nfapi_nr_dl_tti_ssb_pdu_rel15_t *b)
{
  EQ(a->PhysCellId, b->PhysCellId);
  EQ(a->BetaPss, b->BetaPss);
  EQ(a->SsbBlockIndex, b->SsbBlockIndex);
  EQ(a->SsbSubcarrierOffset, b->SsbSubcarrierOffset);
  EQ(a->ssbOffsetPointA, b->ssbOffsetPointA);
  EQ(a->bchPayloadFlag, b->bchPayloadFlag);
  EQ(a->bchPayload, b->bchPayload);
  EQ(a->ssbRsrp, b->ssbRsrp);
  EQ(eq_dl_tti_beamforming(&a->precoding_and_beamforming, &b->precoding_and_beamforming), true);
  return true;
}

bool eq_dl_tti_request(const nfapi_nr_dl_tti_request_t *a, const nfapi_nr_dl_tti_request_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);

  EQ(a->SFN, b->SFN);
  EQ(a->Slot, b->Slot);
  EQ(a->dl_tti_request_body.nPDUs, b->dl_tti_request_body.nPDUs);
  EQ(a->dl_tti_request_body.nGroup, b->dl_tti_request_body.nGroup);

  for (int PDU = 0; PDU < a->dl_tti_request_body.nPDUs; ++PDU) {
    // take the PDU at the start of loops
    const nfapi_nr_dl_tti_request_pdu_t *a_dl_pdu = &a->dl_tti_request_body.dl_tti_pdu_list[PDU];
    const nfapi_nr_dl_tti_request_pdu_t *b_dl_pdu = &b->dl_tti_request_body.dl_tti_pdu_list[PDU];

    EQ(a_dl_pdu->PDUType, b_dl_pdu->PDUType);
    EQ(a_dl_pdu->PDUSize, b_dl_pdu->PDUSize);

    switch (a_dl_pdu->PDUType) {
      case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
        EQ(eq_dl_tti_request_pdcch_pdu(&a_dl_pdu->pdcch_pdu.pdcch_pdu_rel15, &b_dl_pdu->pdcch_pdu.pdcch_pdu_rel15), true);
        break;
      case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE:
        EQ(eq_dl_tti_request_pdsch_pdu(&a_dl_pdu->pdsch_pdu.pdsch_pdu_rel15, &b_dl_pdu->pdsch_pdu.pdsch_pdu_rel15), true);
        break;
      case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
        EQ(eq_dl_tti_request_csi_rs_pdu(&a_dl_pdu->csi_rs_pdu.csi_rs_pdu_rel15, &b_dl_pdu->csi_rs_pdu.csi_rs_pdu_rel15), true);
        break;
      case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
        EQ(eq_dl_tti_request_ssb_pdu(&a_dl_pdu->ssb_pdu.ssb_pdu_rel15, &b_dl_pdu->ssb_pdu.ssb_pdu_rel15), true);
        break;
      default:
        // PDU Type is not any known value
        return false;
    }
  }

  for (int nGroup = 0; nGroup < a->dl_tti_request_body.nGroup; ++nGroup) {
    EQ(a->dl_tti_request_body.nUe[nGroup], b->dl_tti_request_body.nUe[nGroup]);
    for (int UE = 0; UE < a->dl_tti_request_body.nUe[nGroup]; ++UE) {
      EQ(a->dl_tti_request_body.PduIdx[nGroup][UE], b->dl_tti_request_body.PduIdx[nGroup][UE]);
    }
  }

  return true;
}

static bool eq_ul_tti_beamforming(const nfapi_nr_ul_beamforming_t *a, const nfapi_nr_ul_beamforming_t *b)
{
  EQ(a->trp_scheme, b->trp_scheme);
  EQ(a->num_prgs, b->num_prgs);
  EQ(a->prg_size, b->prg_size);
  EQ(a->dig_bf_interface, b->dig_bf_interface);
  for (int prg = 0; prg < a->num_prgs; ++prg) {
    for (int dbf_if = 0; dbf_if < a->dig_bf_interface; ++dbf_if) {
      EQ(a->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx, b->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx);
    }
  }

  return true;
}

static bool eq_ul_tti_request_prach_pdu(const nfapi_nr_prach_pdu_t *a, const nfapi_nr_prach_pdu_t *b)
{
  EQ(a->phys_cell_id, b->phys_cell_id);
  EQ(a->num_prach_ocas, b->num_prach_ocas);
  EQ(a->prach_format, b->prach_format);
  EQ(a->num_ra, b->num_ra);
  EQ(a->prach_start_symbol, b->prach_start_symbol);
  EQ(a->num_cs, b->num_cs);
  EQ(eq_ul_tti_beamforming(&a->beamforming, &b->beamforming), true);
  return true;
}

static bool eq_ul_tti_request_pusch_pdu(const nfapi_nr_pusch_pdu_t *a, const nfapi_nr_pusch_pdu_t *b)
{
  EQ(a->pdu_bit_map, b->pdu_bit_map);
  EQ(a->rnti, b->rnti);
  EQ(a->handle, b->handle);
  EQ(a->bwp_size, b->bwp_size);
  EQ(a->bwp_start, b->bwp_start);
  EQ(a->subcarrier_spacing, b->subcarrier_spacing);
  EQ(a->cyclic_prefix, b->cyclic_prefix);
  EQ(a->target_code_rate, b->target_code_rate);
  EQ(a->qam_mod_order, b->qam_mod_order);
  EQ(a->mcs_index, b->mcs_index);
  EQ(a->mcs_table, b->mcs_table);
  EQ(a->transform_precoding, b->transform_precoding);
  EQ(a->data_scrambling_id, b->data_scrambling_id);
  EQ(a->nrOfLayers, b->nrOfLayers);
  EQ(a->ul_dmrs_symb_pos, b->ul_dmrs_symb_pos);
  EQ(a->dmrs_config_type, b->dmrs_config_type);
  EQ(a->ul_dmrs_scrambling_id, b->ul_dmrs_scrambling_id);
  EQ(a->pusch_identity, b->pusch_identity);
  EQ(a->scid, b->scid);
  EQ(a->num_dmrs_cdm_grps_no_data, b->num_dmrs_cdm_grps_no_data);
  EQ(a->dmrs_ports, b->dmrs_ports);
  EQ(a->resource_alloc, b->resource_alloc);
  for (int i = 0; i < 36; ++i) {
    EQ(a->rb_bitmap[i], b->rb_bitmap[i]);
  }
  EQ(a->rb_start, b->rb_start);
  EQ(a->rb_size, b->rb_size);
  EQ(a->vrb_to_prb_mapping, b->vrb_to_prb_mapping);
  EQ(a->frequency_hopping, b->frequency_hopping);
  EQ(a->tx_direct_current_location, b->tx_direct_current_location);
  EQ(a->uplink_frequency_shift_7p5khz, b->uplink_frequency_shift_7p5khz);
  EQ(a->start_symbol_index, b->start_symbol_index);
  EQ(a->nr_of_symbols, b->nr_of_symbols);

  if (a->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA) {
    const nfapi_nr_pusch_data_t *a_pusch_data = &a->pusch_data;
    const nfapi_nr_pusch_data_t *b_pusch_data = &b->pusch_data;
    EQ(a_pusch_data->rv_index, b_pusch_data->rv_index);
    EQ(a_pusch_data->harq_process_id, b_pusch_data->harq_process_id);
    EQ(a_pusch_data->new_data_indicator, b_pusch_data->new_data_indicator);
    EQ(a_pusch_data->tb_size, b_pusch_data->tb_size);
    EQ(a_pusch_data->num_cb, b_pusch_data->num_cb);
    for (int i = 0; i < (a_pusch_data->num_cb + 7) / 8; ++i) {
      EQ(a_pusch_data->cb_present_and_position[i], b_pusch_data->cb_present_and_position[i]);
    }
  }
  if (a->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI) {
    const nfapi_nr_pusch_uci_t *a_pusch_uci = &a->pusch_uci;
    const nfapi_nr_pusch_uci_t *b_pusch_uci = &b->pusch_uci;
    EQ(a_pusch_uci->harq_ack_bit_length, b_pusch_uci->harq_ack_bit_length);
    EQ(a_pusch_uci->csi_part1_bit_length, b_pusch_uci->csi_part1_bit_length);
    EQ(a_pusch_uci->csi_part2_bit_length, b_pusch_uci->csi_part2_bit_length);
    EQ(a_pusch_uci->alpha_scaling, b_pusch_uci->alpha_scaling);
    EQ(a_pusch_uci->beta_offset_harq_ack, b_pusch_uci->beta_offset_harq_ack);
    EQ(a_pusch_uci->beta_offset_csi1, b_pusch_uci->beta_offset_csi1);
    EQ(a_pusch_uci->beta_offset_csi2, b_pusch_uci->beta_offset_csi2);
  }
  if (a->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    const nfapi_nr_pusch_ptrs_t *a_pusch_ptrs = &a->pusch_ptrs;
    const nfapi_nr_pusch_ptrs_t *b_pusch_ptrs = &b->pusch_ptrs;

    EQ(a_pusch_ptrs->num_ptrs_ports, b_pusch_ptrs->num_ptrs_ports);
    for (int i = 0; i < a_pusch_ptrs->num_ptrs_ports; ++i) {
      const nfapi_nr_ptrs_ports_t *a_ptrs_port = &a_pusch_ptrs->ptrs_ports_list[i];
      const nfapi_nr_ptrs_ports_t *b_ptrs_port = &b_pusch_ptrs->ptrs_ports_list[i];

      EQ(a_ptrs_port->ptrs_port_index, b_ptrs_port->ptrs_port_index);
      EQ(a_ptrs_port->ptrs_dmrs_port, b_ptrs_port->ptrs_dmrs_port);
      EQ(a_ptrs_port->ptrs_re_offset, b_ptrs_port->ptrs_re_offset);
    }

    EQ(a_pusch_ptrs->ptrs_time_density, b_pusch_ptrs->ptrs_time_density);
    EQ(a_pusch_ptrs->ptrs_freq_density, b_pusch_ptrs->ptrs_freq_density);
    EQ(a_pusch_ptrs->ul_ptrs_power, b_pusch_ptrs->ul_ptrs_power);
  }
  if (a->pdu_bit_map & PUSCH_PDU_BITMAP_DFTS_OFDM) {
    const nfapi_nr_dfts_ofdm_t *a_dfts_ofdm = &a->dfts_ofdm;
    const nfapi_nr_dfts_ofdm_t *b_dfts_ofdm = &b->dfts_ofdm;

    EQ(a_dfts_ofdm->low_papr_group_number, b_dfts_ofdm->low_papr_group_number);
    EQ(a_dfts_ofdm->low_papr_sequence_number, b_dfts_ofdm->low_papr_sequence_number);
    EQ(a_dfts_ofdm->ul_ptrs_sample_density, b_dfts_ofdm->ul_ptrs_sample_density);
    EQ(a_dfts_ofdm->ul_ptrs_time_density_transform_precoding, b_dfts_ofdm->ul_ptrs_time_density_transform_precoding);
  }
  EQ(eq_ul_tti_beamforming(&a->beamforming, &b->beamforming), true);
  EQ(a->maintenance_parms_v3.ldpcBaseGraph, b->maintenance_parms_v3.ldpcBaseGraph);
  EQ(a->maintenance_parms_v3.tbSizeLbrmBytes, b->maintenance_parms_v3.tbSizeLbrmBytes);
  return true;
}

static bool eq_ul_tti_request_pucch_pdu(const nfapi_nr_pucch_pdu_t *a, const nfapi_nr_pucch_pdu_t *b)
{
  EQ(a->rnti, b->rnti);
  EQ(a->handle, b->handle);
  EQ(a->bwp_size, b->bwp_size);
  EQ(a->bwp_start, b->bwp_start);
  EQ(a->subcarrier_spacing, b->subcarrier_spacing);
  EQ(a->cyclic_prefix, b->cyclic_prefix);
  EQ(a->format_type, b->format_type);
  EQ(a->multi_slot_tx_indicator, b->multi_slot_tx_indicator);
  EQ(a->pi_2bpsk, b->pi_2bpsk);
  EQ(a->prb_start, b->prb_start);
  EQ(a->prb_size, b->prb_size);
  EQ(a->start_symbol_index, b->start_symbol_index);
  EQ(a->nr_of_symbols, b->nr_of_symbols);
  EQ(a->freq_hop_flag, b->freq_hop_flag);
  EQ(a->second_hop_prb, b->second_hop_prb);
  EQ(a->group_hop_flag, b->group_hop_flag);
  EQ(a->sequence_hop_flag, b->sequence_hop_flag);
  EQ(a->hopping_id, b->hopping_id);
  EQ(a->initial_cyclic_shift, b->initial_cyclic_shift);
  EQ(a->data_scrambling_id, b->data_scrambling_id);
  EQ(a->time_domain_occ_idx, b->time_domain_occ_idx);
  EQ(a->pre_dft_occ_idx, b->pre_dft_occ_idx);
  EQ(a->pre_dft_occ_len, b->pre_dft_occ_len);
  EQ(a->add_dmrs_flag, b->add_dmrs_flag);
  EQ(a->dmrs_scrambling_id, b->dmrs_scrambling_id);
  EQ(a->dmrs_cyclic_shift, b->dmrs_cyclic_shift);
  EQ(a->sr_flag, b->sr_flag);
  EQ(a->bit_len_harq, b->bit_len_harq);
  EQ(a->bit_len_csi_part1, b->bit_len_csi_part1);
  EQ(a->bit_len_csi_part2, b->bit_len_csi_part2);
  EQ(eq_ul_tti_beamforming(&a->beamforming, &b->beamforming), true);
  return true;
}

static bool eq_ul_tti_request_srs_parameters(const nfapi_v4_srs_parameters_t *a,
                                             const uint8_t num_symbols,
                                             const nfapi_v4_srs_parameters_t *b)
{
  EQ(a->srs_bandwidth_size, b->srs_bandwidth_size);
  for (int symbol_idx = 0; symbol_idx < num_symbols; ++symbol_idx) {
    const nfapi_v4_srs_parameters_symbols_t *a_symbol = &a->symbol_list[symbol_idx];
    const nfapi_v4_srs_parameters_symbols_t *b_symbol = &b->symbol_list[symbol_idx];
    EQ(a_symbol->srs_bandwidth_start, b_symbol->srs_bandwidth_start);
    EQ(a_symbol->sequence_group, b_symbol->sequence_group);
    EQ(a_symbol->sequence_number, b_symbol->sequence_number);
  }

#ifdef ENABLE_AERIAL
  // For Aerial, we always process the 4 reported symbols, not only the ones indicated by num_symbols
  for (int symbol_idx = num_symbols; symbol_idx < 4; ++symbol_idx) {
    const nfapi_v4_srs_parameters_symbols_t *a_symbol = &a->symbol_list[symbol_idx];
    const nfapi_v4_srs_parameters_symbols_t *b_symbol = &b->symbol_list[symbol_idx];
    EQ(a_symbol->srs_bandwidth_start, b_symbol->srs_bandwidth_start);
    EQ(a_symbol->sequence_group, b_symbol->sequence_group);
    EQ(a_symbol->sequence_number, b_symbol->sequence_number);
  }
#endif // ENABLE_AERIAL

  EQ(a->usage, b->usage);
  const uint8_t nUsage = __builtin_popcount(a->usage);
  for (int idx = 0; idx < nUsage; ++idx) {
    EQ(a->report_type[idx], b->report_type[idx]);
  }
  EQ(a->singular_Value_representation, b->singular_Value_representation);
  EQ(a->iq_representation, b->iq_representation);
  EQ(a->prg_size, b->prg_size);
  EQ(a->num_total_ue_antennas, b->num_total_ue_antennas);
  EQ(a->ue_antennas_in_this_srs_resource_set, b->ue_antennas_in_this_srs_resource_set);
  EQ(a->sampled_ue_antennas, b->sampled_ue_antennas);
  EQ(a->report_scope, b->report_scope);
  EQ(a->num_ul_spatial_streams_ports, b->num_ul_spatial_streams_ports);
  for (int idx = 0; idx < a->num_ul_spatial_streams_ports; ++idx) {
    EQ(a->Ul_spatial_stream_ports[idx], b->Ul_spatial_stream_ports[idx]);
  }
  return true;
}

static bool eq_ul_tti_request_srs_pdu(const nfapi_nr_srs_pdu_t *a, const nfapi_nr_srs_pdu_t *b)
{
  EQ(a->rnti, b->rnti);
  EQ(a->handle, b->handle);
  EQ(a->bwp_size, b->bwp_size);
  EQ(a->bwp_start, b->bwp_start);
  EQ(a->subcarrier_spacing, b->subcarrier_spacing);
  EQ(a->cyclic_prefix, b->cyclic_prefix);
  EQ(a->num_ant_ports, b->num_ant_ports);
  EQ(a->num_symbols, b->num_symbols);
  EQ(a->num_repetitions, b->num_repetitions);
  EQ(a->time_start_position, b->time_start_position);
  EQ(a->config_index, b->config_index);
  EQ(a->sequence_id, b->sequence_id);
  EQ(a->bandwidth_index, b->bandwidth_index);
  EQ(a->comb_size, b->comb_size);
  EQ(a->comb_offset, b->comb_offset);
  EQ(a->cyclic_shift, b->cyclic_shift);
  EQ(a->frequency_position, b->frequency_position);
  EQ(a->frequency_shift, b->frequency_shift);
  EQ(a->frequency_hopping, b->frequency_hopping);
  EQ(a->group_or_sequence_hopping, b->group_or_sequence_hopping);
  EQ(a->resource_type, b->resource_type);
  EQ(a->t_srs, b->t_srs);
  EQ(a->t_offset, b->t_offset);
  EQ(eq_ul_tti_beamforming(&a->beamforming, &b->beamforming), true);
  EQ(eq_ul_tti_request_srs_parameters(&a->srs_parameters_v4, 1 << a->num_symbols, &b->srs_parameters_v4), true);
  return true;
}

bool eq_ul_tti_request(const nfapi_nr_ul_tti_request_t *a, const nfapi_nr_ul_tti_request_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);

  EQ(a->SFN, b->SFN);
  EQ(a->Slot, b->Slot);
  EQ(a->n_pdus, b->n_pdus);
  EQ(a->rach_present, b->rach_present);
  EQ(a->n_ulsch, b->n_ulsch);
  EQ(a->n_ulcch, b->n_ulcch);
  EQ(a->n_group, b->n_group);

  for (int PDU = 0; PDU < a->n_pdus; ++PDU) {
    // take the PDU at the start of loops
    const nfapi_nr_ul_tti_request_number_of_pdus_t *a_pdu = &a->pdus_list[PDU];
    const nfapi_nr_ul_tti_request_number_of_pdus_t *b_pdu = &b->pdus_list[PDU];

    EQ(a_pdu->pdu_type, b_pdu->pdu_type);
    EQ(a_pdu->pdu_size, b_pdu->pdu_size);

    switch (a_pdu->pdu_type) {
      case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE:
        EQ(eq_ul_tti_request_prach_pdu(&a_pdu->prach_pdu, &b_pdu->prach_pdu), true);
        break;
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        EQ(eq_ul_tti_request_pusch_pdu(&a_pdu->pusch_pdu, &b_pdu->pusch_pdu), true);
        break;
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
        EQ(eq_ul_tti_request_pucch_pdu(&a_pdu->pucch_pdu, &b_pdu->pucch_pdu), true);
        break;
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
        EQ(eq_ul_tti_request_srs_pdu(&a_pdu->srs_pdu, &b_pdu->srs_pdu), true);
        break;
      default:
        // PDU Type is not any known value
        return false;
    }
  }

  for (int nGroup = 0; nGroup < a->n_group; ++nGroup) {
    EQ(a->groups_list[nGroup].n_ue, b->groups_list[nGroup].n_ue);
    for (int UE = 0; UE < a->groups_list[nGroup].n_ue; ++UE) {
      EQ(a->groups_list[nGroup].ue_list[UE].pdu_idx, b->groups_list[nGroup].ue_list[UE].pdu_idx);
    }
  }
  return true;
}

bool eq_slot_indication(const nfapi_nr_slot_indication_scf_t *a, const nfapi_nr_slot_indication_scf_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);

  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  return true;
}

bool eq_ul_dci_request_PDU(const nfapi_nr_ul_dci_request_pdus_t *a, const nfapi_nr_ul_dci_request_pdus_t *b)
{
  EQ(a->PDUType, b->PDUType);
  EQ(a->PDUSize, b->PDUSize);
  // Is the same PDU as the DL_TTI.request PDCCH PDU, reuse comparison function
  EQ(eq_dl_tti_request_pdcch_pdu(&a->pdcch_pdu.pdcch_pdu_rel15, &b->pdcch_pdu.pdcch_pdu_rel15), true);
  return true;
}

bool eq_ul_dci_request(const nfapi_nr_ul_dci_request_t *a, const nfapi_nr_ul_dci_request_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);

  EQ(a->SFN, b->SFN);
  EQ(a->Slot, b->Slot);
  EQ(a->numPdus, b->numPdus);
  for (int pdu_idx = 0; pdu_idx < a->numPdus; ++pdu_idx) {
    EQ(eq_ul_dci_request_PDU(&a->ul_dci_pdu_list[pdu_idx], &b->ul_dci_pdu_list[pdu_idx]), true);
  }
  return true;
}

bool eq_tx_data_request_PDU(const nfapi_nr_pdu_t *a, const nfapi_nr_pdu_t *b)
{
  EQ(a->PDU_length, b->PDU_length);
  EQ(a->PDU_index, b->PDU_index);
  EQ(a->num_TLV, b->num_TLV);
  for (int tlv_idx = 0; tlv_idx < a->num_TLV; ++tlv_idx) {
    const nfapi_nr_tx_data_request_tlv_t *a_tlv = &a->TLVs[tlv_idx];
    const nfapi_nr_tx_data_request_tlv_t *b_tlv = &b->TLVs[tlv_idx];
    EQ(a_tlv->length, b_tlv->length);
    // it does not matter which tag messages have, the payload must be the same
    EQ(a_tlv->tag == 0 || a_tlv->tag == 1, true);
    const uint32_t *abuf = a_tlv->tag == 0 ? a_tlv->value.direct : a_tlv->value.ptr;
    EQ(b_tlv->tag == 0 || b_tlv->tag == 1, true);
    const uint32_t *bbuf = b_tlv->tag == 0 ? b_tlv->value.direct : b_tlv->value.ptr;
    int ret = memcmp(abuf, bbuf, (a_tlv->length + 3) / 4);
    EQ(ret, 0);
  }
  return true;
}

bool eq_tx_data_request(const nfapi_nr_tx_data_request_t *a, const nfapi_nr_tx_data_request_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);

  EQ(a->SFN, b->SFN);
  EQ(a->Slot, b->Slot);
  EQ(a->Number_of_PDUs, b->Number_of_PDUs);
  for (int pdu_idx = 0; pdu_idx < a->Number_of_PDUs; ++pdu_idx) {
    EQ(eq_tx_data_request_PDU(&a->pdu_list[pdu_idx], &b->pdu_list[pdu_idx]), true);
  }
  return true;
}

bool eq_rx_data_indication_PDU(const nfapi_nr_rx_data_pdu_t *a, const nfapi_nr_rx_data_pdu_t *b)
{
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->harq_id, b->harq_id);
  EQ(a->pdu_length, b->pdu_length);
  EQ(a->ul_cqi, b->ul_cqi);
  EQ(a->timing_advance, b->timing_advance);
  EQ(a->rssi, b->rssi);
  for (int i = 0; i < a->pdu_length; ++i) {
    EQ(a->pdu[i], b->pdu[i]);
  }
  return true;
}

bool eq_rx_data_indication(const nfapi_nr_rx_data_indication_t *a, const nfapi_nr_rx_data_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->number_of_pdus, b->number_of_pdus);
  for (int pdu_idx = 0; pdu_idx < a->number_of_pdus; ++pdu_idx) {
    EQ(eq_rx_data_indication_PDU(&a->pdu_list[pdu_idx], &b->pdu_list[pdu_idx]), true);
  }
  return true;
}

bool eq_crc_indication_CRC(const nfapi_nr_crc_t *a, const nfapi_nr_crc_t *b)
{
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->harq_id, b->harq_id);
  EQ(a->tb_crc_status, b->tb_crc_status);
  EQ(a->num_cb, b->num_cb);
  if (a->num_cb > 0) {
    for (int cb = 0; cb < nr_bits_to_bytes(a->num_cb); ++cb) {
      EQ(a->cb_crc_status[cb], b->cb_crc_status[cb]);
    }
  }
  EQ(a->ul_cqi, b->ul_cqi);
  EQ(a->timing_advance, b->timing_advance);
  EQ(a->rssi, b->rssi);
  return true;
}

bool eq_crc_indication(const nfapi_nr_crc_indication_t *a, const nfapi_nr_crc_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->number_crcs, b->number_crcs);
  for (int crc_idx = 0; crc_idx < a->number_crcs; ++crc_idx) {
    EQ(eq_crc_indication_CRC(&a->crc_list[crc_idx], &b->crc_list[crc_idx]), true);
  }
  return true;
}

bool eq_uci_indication_sr_pdu_0_1(const nfapi_nr_sr_pdu_0_1_t *a, const nfapi_nr_sr_pdu_0_1_t *b)
{
  EQ(a->sr_indication, b->sr_indication);
  EQ(a->sr_confidence_level, b->sr_confidence_level);
  return true;
}

bool eq_uci_indication_sr_pdu_2_3_4(const nfapi_nr_sr_pdu_2_3_4_t *a, const nfapi_nr_sr_pdu_2_3_4_t *b)
{
  EQ(a->sr_bit_len, b->sr_bit_len);
  for (int i = 0; i < nr_bits_to_bytes(a->sr_bit_len); ++i) {
    EQ(a->sr_payload[i], b->sr_payload[i]);
  }
  return true;
}

bool eq_uci_indication_harq_pdu_0_1(const nfapi_nr_harq_pdu_0_1_t *a, const nfapi_nr_harq_pdu_0_1_t *b)
{
  EQ(a->num_harq, b->num_harq);
  EQ(a->harq_confidence_level, b->harq_confidence_level);
  for (int i = 0; i < a->num_harq; ++i) {
    EQ(a->harq_list[i].harq_value, b->harq_list[i].harq_value);
  }
  return true;
}

bool eq_uci_indication_harq_pdu_2_3_4(const nfapi_nr_harq_pdu_2_3_4_t *a, const nfapi_nr_harq_pdu_2_3_4_t *b)
{
  EQ(a->harq_crc, b->harq_crc);
  EQ(a->harq_bit_len, b->harq_bit_len);
  for (int i = 0; i < nr_bits_to_bytes(a->harq_bit_len); ++i) {
    EQ(a->harq_payload[i], b->harq_payload[i]);
  }
  return true;
}

bool eq_uci_indication_csi_part1(const nfapi_nr_csi_part1_pdu_t *a, const nfapi_nr_csi_part1_pdu_t *b)
{
  EQ(a->csi_part1_crc, b->csi_part1_crc);
  EQ(a->csi_part1_bit_len, b->csi_part1_bit_len);
  for (int i = 0; i < nr_bits_to_bytes(a->csi_part1_bit_len); ++i) {
    EQ(a->csi_part1_payload[i], b->csi_part1_payload[i]);
  }
  return true;
}

bool eq_uci_indication_csi_part2(const nfapi_nr_csi_part2_pdu_t *a, const nfapi_nr_csi_part2_pdu_t *b)
{
  EQ(a->csi_part2_crc, b->csi_part2_crc);
  EQ(a->csi_part2_bit_len, b->csi_part2_bit_len);
  for (int i = 0; i < nr_bits_to_bytes(a->csi_part2_bit_len); ++i) {
    EQ(a->csi_part2_payload[i], b->csi_part2_payload[i]);
  }
  return true;
}

bool eq_uci_indication_PUSCH(const nfapi_nr_uci_pusch_pdu_t *a, const nfapi_nr_uci_pusch_pdu_t *b)
{
  EQ(a->pduBitmap, b->pduBitmap);
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->ul_cqi, b->ul_cqi);
  EQ(a->timing_advance, b->timing_advance);
  EQ(a->rssi, b->rssi);

  // Bit 0 not used in PUSCH PDU
  // HARQ
  if ((a->pduBitmap >> 1) & 0x01) {
    EQ(eq_uci_indication_harq_pdu_2_3_4(&a->harq, &b->harq), true);
  }
  // CSI Part 1
  if ((a->pduBitmap >> 2) & 0x01) {
    EQ(eq_uci_indication_csi_part1(&a->csi_part1, &b->csi_part1), true);
  }
  // CSI Part 2
  if ((a->pduBitmap >> 3) & 0x01) {
    EQ(eq_uci_indication_csi_part2(&a->csi_part2, &b->csi_part2), true);
  }
  return true;
}

bool eq_uci_indication_PUCCH_0_1(const nfapi_nr_uci_pucch_pdu_format_0_1_t *a, const nfapi_nr_uci_pucch_pdu_format_0_1_t *b)
{
  EQ(a->pduBitmap, b->pduBitmap);
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->pucch_format, b->pucch_format);
  EQ(a->ul_cqi, b->ul_cqi);
  EQ(a->timing_advance, b->timing_advance);
  EQ(a->rssi, b->rssi);

  // SR
  if (a->pduBitmap & 0x01) {
    EQ(eq_uci_indication_sr_pdu_0_1(&a->sr, &b->sr), true);
  }
  // HARQ
  if ((a->pduBitmap >> 1) & 0x01) {
    EQ(eq_uci_indication_harq_pdu_0_1(&a->harq, &b->harq), true);
  }
  return true;
}

bool eq_uci_indication_PUCCH_2_3_4(const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *a, const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *b)
{
  EQ(a->pduBitmap, b->pduBitmap);
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->pucch_format, b->pucch_format);
  EQ(a->ul_cqi, b->ul_cqi);
  EQ(a->timing_advance, b->timing_advance);
  EQ(a->rssi, b->rssi);
  // SR
  if (a->pduBitmap & 0x01) {
    EQ(eq_uci_indication_sr_pdu_2_3_4(&a->sr, &b->sr), true);
  }
  // HARQ
  if ((a->pduBitmap >> 1) & 0x01) {
    EQ(eq_uci_indication_harq_pdu_2_3_4(&a->harq, &b->harq), true);
  }
  // CSI Part 1
  if ((a->pduBitmap >> 2) & 0x01) {
    EQ(eq_uci_indication_csi_part1(&a->csi_part1, &b->csi_part1), true);
  }
  // CSI Part 2
  if ((a->pduBitmap >> 3) & 0x01) {
    EQ(eq_uci_indication_csi_part2(&a->csi_part2, &b->csi_part2), true);
  }
  return true;
}

bool eq_uci_indication_UCI(const nfapi_nr_uci_t *a, const nfapi_nr_uci_t *b)
{
  EQ(a->pdu_type, b->pdu_type);
  EQ(a->pdu_size, b->pdu_size);
  switch (a->pdu_type) {
    case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
      EQ(eq_uci_indication_PUSCH(&a->pusch_pdu, &b->pusch_pdu), true);
      break;
    case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE:
      EQ(eq_uci_indication_PUCCH_0_1(&a->pucch_pdu_format_0_1, &b->pucch_pdu_format_0_1), true);
      break;
    case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE:
      EQ(eq_uci_indication_PUCCH_2_3_4(&a->pucch_pdu_format_2_3_4, &b->pucch_pdu_format_2_3_4), true);
      break;
    default:
      AssertFatal(1 == 0, "Unknown UCI.indication PDU Type %d\n", a->pdu_type);
      break;
  }
  return true;
}

bool eq_uci_indication(const nfapi_nr_uci_indication_t *a, const nfapi_nr_uci_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->num_ucis, b->num_ucis);
  for (int crc_idx = 0; crc_idx < a->num_ucis; ++crc_idx) {
    EQ(eq_uci_indication_UCI(&a->uci_list[crc_idx], &b->uci_list[crc_idx]), true);
  }
  return true;
}

static bool eq_srs_indication_report_tlv(const nfapi_srs_report_tlv_t *a, const nfapi_srs_report_tlv_t *b)
{
  EQ(a->tag, b->tag);
  EQ(a->length, b->length);
  for (int i = 0; i < (a->length + 3) / 4; ++i) {
    EQ(a->value[i], b->value[i]);
  }
  return true;
}

static bool eq_srs_indication_PDU(const nfapi_nr_srs_indication_pdu_t *a, const nfapi_nr_srs_indication_pdu_t *b)
{
  EQ(a->handle, b->handle);
  EQ(a->rnti, b->rnti);
  EQ(a->timing_advance_offset, b->timing_advance_offset);
  EQ(a->timing_advance_offset_nsec, b->timing_advance_offset_nsec);
  EQ(a->srs_usage, b->srs_usage);
  EQ(a->report_type, b->report_type);
  EQ(eq_srs_indication_report_tlv(&a->report_tlv, &b->report_tlv), true);
  return true;
}

bool eq_srs_indication(const nfapi_nr_srs_indication_t *a, const nfapi_nr_srs_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->control_length, b->control_length);
  EQ(a->number_of_pdus, b->number_of_pdus);
  for (int pdu_idx = 0; pdu_idx < a->number_of_pdus; ++pdu_idx) {
    EQ(eq_srs_indication_PDU(&a->pdu_list[pdu_idx], &b->pdu_list[pdu_idx]), true);
  }
  return true;
}

bool eq_srs_toa_vendor_ext_indication(const nfapi_nr_srs_toa_vendor_ext_indication_t *a,
                                      const nfapi_nr_srs_toa_vendor_ext_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->rnti, b->rnti);
  EQ(a->num_ta, b->num_ta);
  for (int ta_idx = 0; ta_idx < a->num_ta; ++ta_idx) {
    EQ(a->ta_offset_nsec[ta_idx], b->ta_offset_nsec[ta_idx]);
  }
  return true;
}

static bool eq_rach_indication_PDU(const nfapi_nr_prach_indication_pdu_t *a, const nfapi_nr_prach_indication_pdu_t *b)
{
  EQ(a->phy_cell_id, b->phy_cell_id);
  EQ(a->symbol_index, b->symbol_index);
  EQ(a->slot_index, b->slot_index);
  EQ(a->freq_index, b->freq_index);
  EQ(a->avg_rssi, b->avg_rssi);
  EQ(a->avg_snr, b->avg_snr);
  EQ(a->num_preamble, b->num_preamble);
  for (int preamble_idx = 0; preamble_idx < a->num_preamble; ++preamble_idx) {
    const nfapi_nr_prach_indication_preamble_t *a_pr = &a->preamble_list[preamble_idx];
    const nfapi_nr_prach_indication_preamble_t *b_pr = &b->preamble_list[preamble_idx];
    EQ(a_pr->preamble_index, b_pr->preamble_index);
    EQ(a_pr->timing_advance, b_pr->timing_advance);
    EQ(a_pr->preamble_pwr, b_pr->preamble_pwr);
  }
  return true;
}

bool eq_rach_indication(const nfapi_nr_rach_indication_t *a, const nfapi_nr_rach_indication_t *b)
{
  EQ(a->header.phy_id, b->header.phy_id);
  EQ(a->header.message_id, b->header.message_id);
  EQ(a->header.message_length, b->header.message_length);
  EQ(a->sfn, b->sfn);
  EQ(a->slot, b->slot);
  EQ(a->number_of_pdus, b->number_of_pdus);
  for (int pdu_idx = 0; pdu_idx < a->number_of_pdus; ++pdu_idx) {
    EQ(eq_rach_indication_PDU(&a->pdu_list[pdu_idx], &b->pdu_list[pdu_idx]), true);
  }
  return true;
}

void free_dl_tti_request(nfapi_nr_dl_tti_request_t *msg)
{
  free(msg->vendor_extension);
}

void free_ul_tti_request(nfapi_nr_ul_tti_request_t *msg)
{
  for (int idx_pdu = 0; idx_pdu < msg->n_pdus; ++idx_pdu) {
    nfapi_nr_ul_tti_request_number_of_pdus_t *pdu = &msg->pdus_list[idx_pdu];
    switch (pdu->pdu_type) {
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        free(pdu->pusch_pdu.pusch_ptrs.ptrs_ports_list);
        break;
      default:
        break;
    }
  }
}

void free_slot_indication(nfapi_nr_slot_indication_scf_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

void free_ul_dci_request(nfapi_nr_ul_dci_request_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

void free_tx_data_request(nfapi_nr_tx_data_request_t *msg)
{
  for (int pdu_idx = 0; pdu_idx < msg->Number_of_PDUs; ++pdu_idx) {
    nfapi_nr_pdu_t *pdu = &msg->pdu_list[pdu_idx];
    for (int tlv_idx = 0; tlv_idx < pdu->num_TLV; ++tlv_idx) {
      nfapi_nr_tx_data_request_tlv_t *tlv = &pdu->TLVs[tlv_idx];
      if (tlv->tag == 1) {
        // value.ptr
        free(tlv->value.ptr);
      }
    }
  }
}

void free_rx_data_indication(nfapi_nr_rx_data_indication_t *msg)
{
  if (msg->number_of_pdus > 0) {
    for (int pdu_idx = 0; pdu_idx < msg->number_of_pdus; ++pdu_idx) {
      nfapi_nr_rx_data_pdu_t *rx_pdu = &msg->pdu_list[pdu_idx];
      free(rx_pdu->pdu);
    }
    free(msg->pdu_list);
  }
}

void free_crc_indication(nfapi_nr_crc_indication_t *msg)
{
  for (int crc_idx = 0; crc_idx < msg->number_crcs; ++crc_idx) {
    nfapi_nr_crc_t *crc = &msg->crc_list[crc_idx];
    free(crc->cb_crc_status);
  }
  free(msg->crc_list);
}

void free_uci_indication(nfapi_nr_uci_indication_t *msg)
{
  for (int pdu_idx = 0; pdu_idx < msg->num_ucis; ++pdu_idx) {
    nfapi_nr_uci_t *uci_pdu = &msg->uci_list[pdu_idx];
    switch (uci_pdu->pdu_type) {
      case NFAPI_NR_UCI_PUSCH_PDU_TYPE: {
        nfapi_nr_uci_pusch_pdu_t *pdu = &uci_pdu->pusch_pdu;
        // Bit 0 not used in PUSCH PDU
        // HARQ
        if ((pdu->pduBitmap >> 1) & 0x01) {
          free(pdu->harq.harq_payload);
        }
        // CSI Part 1
        if ((pdu->pduBitmap >> 2) & 0x01) {
          free(pdu->csi_part1.csi_part1_payload);
        }
        // CSI Part 2
        if ((pdu->pduBitmap >> 3) & 0x01) {
          free(pdu->csi_part2.csi_part2_payload);
        }
        break;
      }
      case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE: {
        // Nothing to free
        break;
      }
      case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE: {
        nfapi_nr_uci_pucch_pdu_format_2_3_4_t *pdu = &uci_pdu->pucch_pdu_format_2_3_4;
        // SR
        if (pdu->pduBitmap & 0x01) {
          free(pdu->sr.sr_payload);
        }
        // HARQ
        if ((pdu->pduBitmap >> 1) & 0x01) {
          free(pdu->harq.harq_payload);
        }
        // CSI Part 1
        if ((pdu->pduBitmap >> 2) & 0x01) {
          free(pdu->csi_part1.csi_part1_payload);
        }
        // CSI Part 2
        if ((pdu->pduBitmap >> 3) & 0x01) {
          free(pdu->csi_part2.csi_part2_payload);
        }
        break;
      }
      default:
        AssertFatal(1 == 0, "Unknown UCI.indication PDU Type %d\n", uci_pdu->pdu_type);
        break;
    }
  }
  free(msg->uci_list);
}

void free_srs_indication(nfapi_nr_srs_indication_t *msg)
{
  free(msg->pdu_list);
}

void free_srs_toa_vendor_ext_indication(nfapi_nr_srs_toa_vendor_ext_indication_t *msg)
{
  // Nothing to free
  UNUSED(msg);
}

void free_rach_indication(nfapi_nr_rach_indication_t *msg)
{
  free(msg->pdu_list);
}

static void copy_dl_tti_beamforming(const nfapi_nr_tx_precoding_and_beamforming_t *src,
                                    nfapi_nr_tx_precoding_and_beamforming_t *dst)
{
  dst->num_prgs = src->num_prgs;
  dst->prg_size = src->prg_size;
  dst->dig_bf_interfaces = src->dig_bf_interfaces;
  for (int prg = 0; prg < dst->num_prgs; ++prg) {
    dst->prgs_list[prg].pm_idx = src->prgs_list[prg].pm_idx;
    for (int dbf_if = 0; dbf_if < dst->dig_bf_interfaces; ++dbf_if) {
      dst->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx = src->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx;
    }
  }
}

// Macro to get size from start to a member (exclusive)
#define SIZE_UPTO(type, member) offsetof(type, member)

// Macro to get size from start to a member (inclusive)
#define SIZE_INCLUDING(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))

// Macro to copy partial struct
#define PARTIAL_COPY(dst, src, type, last_member) memcpy((dst), (src), SIZE_INCLUDING(type, last_member))

// Macro to copy partial struct
#define PARTIAL_COPY_RANGE(dst, src, type, first_member, last_member) \
  memcpy(((uint8_t *)(dst)) + SIZE_UPTO(type, first_member),          \
         ((uint8_t *)(src)) + SIZE_UPTO(type, first_member),          \
         SIZE_INCLUDING(type, last_member) - SIZE_UPTO(type, first_member))

// Macro to copy array
#define COPY_STRUCT_ARRAY(dst, src, data_member, count_member)         \
  do {                                                                 \
    size_t _len = (src)->count_member * sizeof((src)->data_member[0]); \
    if (_len > sizeof((dst)->data_member))                             \
      _len = sizeof((dst)->data_member);                               \
    (dst)->count_member = (src)->count_member;                         \
    memcpy((dst)->data_member, (src)->data_member, _len);              \
  } while (0)

static void copy_dl_tti_request_pdcch_pdu(const nfapi_nr_dl_tti_pdcch_pdu_rel15_t *src, nfapi_nr_dl_tti_pdcch_pdu_rel15_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_dl_tti_pdcch_pdu_rel15_t, numDlDci);
  size_t partial_size = offsetof(nfapi_nr_dl_tti_pdcch_pdu_rel15_t, dci_pdu);
  memcpy(dst, src, partial_size);
  for (int dl_dci = 0; dl_dci < dst->numDlDci; ++dl_dci) {
    nfapi_nr_dl_dci_pdu_t *dst_dci_pdu = &dst->dci_pdu[dl_dci];
    const nfapi_nr_dl_dci_pdu_t *src_dci_pdu = &src->dci_pdu[dl_dci];
    PARTIAL_COPY(dst_dci_pdu, src_dci_pdu, nfapi_nr_dl_dci_pdu_t, AggregationLevel);
    copy_dl_tti_beamforming(&src_dci_pdu->precodingAndBeamforming, &dst_dci_pdu->precodingAndBeamforming);
    PARTIAL_COPY_RANGE(dst_dci_pdu, src_dci_pdu, nfapi_nr_dl_dci_pdu_t, beta_PDCCH_1_0, Payload);
  }
  dst->param_v4.numSpatialStreams = src->param_v4.numSpatialStreams;
  for (uint_fast16_t i = 0; i < dst->param_v4.numSpatialStreams; i++) {
    dst->param_v4.dci_spatialStreamIndices[i] = src->param_v4.dci_spatialStreamIndices[i];
  }
}

static void copy_dl_tti_request_pdsch_pdu(const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *src, nfapi_nr_dl_tti_pdsch_pdu_rel15_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_dl_tti_pdsch_pdu_rel15_t, nEpreRatioOfPDSCHToPTRS);
  copy_dl_tti_beamforming(&src->precodingAndBeamforming, &dst->precodingAndBeamforming);
  PARTIAL_COPY_RANGE(dst, src, nfapi_nr_dl_tti_pdsch_pdu_rel15_t, powerControlOffset, maintenance_parms_v3);
  dst->param_v4.numberCodewords = src->param_v4.numberCodewords;
  for (uint_fast8_t i = 0; i < dst->param_v4.numberCodewords; i++) {
    const nfapi_nr_spatial_stream_index_t *s = src->param_v4.spatialStreamsCw + i;
    nfapi_nr_spatial_stream_index_t *d = dst->param_v4.spatialStreamsCw + i;
    COPY_STRUCT_ARRAY(d, s, spatialStreamIndices, numSpatialStreamIndices);
  }
}

static void copy_dl_tti_request_csi_rs_pdu(const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *src, nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_dl_tti_csi_rs_pdu_rel15_t, power_control_offset_ss);
  copy_dl_tti_beamforming(&src->precodingAndBeamforming, &dst->precodingAndBeamforming);
  const struct nfapi_nr_csi_spatial_stream_index *s = &src->param_v4;
  struct nfapi_nr_csi_spatial_stream_index *d = &dst->param_v4;
  COPY_STRUCT_ARRAY(d, s, spatialStreamIndices, numSpatialStreamIndices);
}

static void copy_dl_tti_request_ssb_pdu(const nfapi_nr_dl_tti_ssb_pdu_rel15_t *src, nfapi_nr_dl_tti_ssb_pdu_rel15_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_dl_tti_ssb_pdu_rel15_t, ssbRsrp);
  copy_dl_tti_beamforming(&src->precoding_and_beamforming, &dst->precoding_and_beamforming);
  dst->param_v4 = src->param_v4;
}

static void copy_dl_tti_request_pdu(const nfapi_nr_dl_tti_request_pdu_t *src, nfapi_nr_dl_tti_request_pdu_t *dst)
{
  dst->PDUType = src->PDUType;
  dst->PDUSize = src->PDUSize;

  switch (dst->PDUType) {
    case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
      copy_dl_tti_request_pdcch_pdu(&src->pdcch_pdu.pdcch_pdu_rel15, &dst->pdcch_pdu.pdcch_pdu_rel15);
      break;
    case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE:
      copy_dl_tti_request_pdsch_pdu(&src->pdsch_pdu.pdsch_pdu_rel15, &dst->pdsch_pdu.pdsch_pdu_rel15);
      break;
    case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
      copy_dl_tti_request_csi_rs_pdu(&src->csi_rs_pdu.csi_rs_pdu_rel15, &dst->csi_rs_pdu.csi_rs_pdu_rel15);
      break;
    case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
      copy_dl_tti_request_ssb_pdu(&src->ssb_pdu.ssb_pdu_rel15, &dst->ssb_pdu.ssb_pdu_rel15);
      break;
    default:
      // PDU Type is not any known value
      AssertFatal(1 == 0, "PDU Type value unknown, allowed values range from 0 to 3\n");
  }
}

void copy_dl_tti_request(const nfapi_nr_dl_tti_request_t *src, nfapi_nr_dl_tti_request_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;
  if (src->vendor_extension) {
    dst->vendor_extension = calloc(1, sizeof(nfapi_vendor_extension_tlv_t));
    dst->vendor_extension->tag = src->vendor_extension->tag;
    dst->vendor_extension->length = src->vendor_extension->length;
    copy_vendor_extension_value(&dst->vendor_extension, &src->vendor_extension);
  }

  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->dl_tti_request_body.nPDUs = src->dl_tti_request_body.nPDUs;
  dst->dl_tti_request_body.nGroup = src->dl_tti_request_body.nGroup;
  for (int pdu = 0; pdu < dst->dl_tti_request_body.nPDUs; ++pdu) {
    copy_dl_tti_request_pdu(&src->dl_tti_request_body.dl_tti_pdu_list[pdu], &dst->dl_tti_request_body.dl_tti_pdu_list[pdu]);
  }
  if (dst->dl_tti_request_body.nGroup > 0) {
    for (int nGroup = 0; nGroup < dst->dl_tti_request_body.nGroup; ++nGroup) {
      dst->dl_tti_request_body.nUe[nGroup] = src->dl_tti_request_body.nUe[nGroup];
      for (int UE = 0; UE < dst->dl_tti_request_body.nUe[nGroup]; ++UE) {
        dst->dl_tti_request_body.PduIdx[nGroup][UE] = src->dl_tti_request_body.PduIdx[nGroup][UE];
      }
    }
  }
}

static void copy_ul_tti_beamforming(const nfapi_nr_ul_beamforming_t *src, nfapi_nr_ul_beamforming_t *dst)
{
  dst->trp_scheme = src->trp_scheme;
  dst->num_prgs = src->num_prgs;
  dst->prg_size = src->prg_size;
  dst->dig_bf_interface = src->dig_bf_interface;

  for (int prg = 0; prg < dst->num_prgs; ++prg) {
    if (dst->dig_bf_interface > 0) {
      for (int dbf_if = 0; dbf_if < dst->dig_bf_interface; ++dbf_if) {
        dst->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx = src->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx;
      }
    }
  }
}

static void copy_ul_tti_request_prach_pdu(const nfapi_nr_prach_pdu_t *src, nfapi_nr_prach_pdu_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_prach_pdu_t, num_cs);
  copy_ul_tti_beamforming(&src->beamforming, &dst->beamforming);
  const nfapi_nr_spatial_stream_index_t *s = &src->param_v4;
  nfapi_nr_spatial_stream_index_t *d = &dst->param_v4;
  COPY_STRUCT_ARRAY(d, s, spatialStreamIndices, numSpatialStreamIndices);
}

static void copy_ul_tti_request_pusch_pdu(const nfapi_nr_pusch_pdu_t *src, nfapi_nr_pusch_pdu_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_pusch_pdu_t, nr_of_symbols);
  if (dst->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA) {
    const nfapi_nr_pusch_data_t *src_pusch_data = &src->pusch_data;
    nfapi_nr_pusch_data_t *dst_pusch_data = &dst->pusch_data;
    *dst_pusch_data = *src_pusch_data;
  }
  if (src->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI) {
    const nfapi_nr_pusch_uci_t *src_pusch_uci = &src->pusch_uci;
    nfapi_nr_pusch_uci_t *dst_pusch_uci = &dst->pusch_uci;
    *dst_pusch_uci = *src_pusch_uci;
  }
  if (src->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    const nfapi_nr_pusch_ptrs_t *src_pusch_ptrs = &src->pusch_ptrs;
    nfapi_nr_pusch_ptrs_t *dst_pusch_ptrs = &dst->pusch_ptrs;

    dst_pusch_ptrs->num_ptrs_ports = src_pusch_ptrs->num_ptrs_ports;
    dst_pusch_ptrs->ptrs_ports_list = calloc(dst_pusch_ptrs->num_ptrs_ports, sizeof(nfapi_nr_ptrs_ports_t));
    for (int i = 0; i < src_pusch_ptrs->num_ptrs_ports; ++i) {
      const nfapi_nr_ptrs_ports_t *src_ptrs_port = &src_pusch_ptrs->ptrs_ports_list[i];
      nfapi_nr_ptrs_ports_t *dst_ptrs_port = &dst_pusch_ptrs->ptrs_ports_list[i];
      *dst_ptrs_port = *src_ptrs_port;
    }

    dst_pusch_ptrs->ptrs_time_density = src_pusch_ptrs->ptrs_time_density;
    dst_pusch_ptrs->ptrs_freq_density = src_pusch_ptrs->ptrs_freq_density;
    dst_pusch_ptrs->ul_ptrs_power = src_pusch_ptrs->ul_ptrs_power;
  }
  if (src->pdu_bit_map & PUSCH_PDU_BITMAP_DFTS_OFDM) {
    const nfapi_nr_dfts_ofdm_t *src_dfts_ofdm = &src->dfts_ofdm;
    nfapi_nr_dfts_ofdm_t *dst_dfts_ofdm = &dst->dfts_ofdm;
    *dst_dfts_ofdm = *src_dfts_ofdm;
  }
  copy_ul_tti_beamforming(&src->beamforming, &dst->beamforming);
  dst->maintenance_parms_v3.ldpcBaseGraph = src->maintenance_parms_v3.ldpcBaseGraph;
  dst->maintenance_parms_v3.tbSizeLbrmBytes = src->maintenance_parms_v3.tbSizeLbrmBytes;
  const nfapi_nr_spatial_stream_index_t *s = &src->param_v4;
  nfapi_nr_spatial_stream_index_t *d = &dst->param_v4;
  COPY_STRUCT_ARRAY(d, s, spatialStreamIndices, numSpatialStreamIndices);
}

static void copy_ul_tti_request_pucch_pdu(const nfapi_nr_pucch_pdu_t *src, nfapi_nr_pucch_pdu_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_pucch_pdu_t, bit_len_csi_part2);
  copy_ul_tti_beamforming(&src->beamforming, &dst->beamforming);
  const nfapi_nr_spatial_stream_index_t *s = &src->param_v4;
  nfapi_nr_spatial_stream_index_t *d = &dst->param_v4;
  COPY_STRUCT_ARRAY(d, s, spatialStreamIndices, numSpatialStreamIndices);
}

static void copy_ul_tti_request_srs_parameters(const nfapi_v4_srs_parameters_t *src,
                                               const uint8_t num_symbols,
                                               nfapi_v4_srs_parameters_t *dst)
{
  dst->srs_bandwidth_size = src->srs_bandwidth_size;
#ifdef ENABLE_AERIAL
  // For Aerial, we always process the 4 reported symbols, not only the ones indicated by num_symbols
  const uint8_t symbols_to_fill = 4;
  (void)num_symbols; // To supress unused warning
#else
  const uint8_t symbols_to_fill = num_symbols;
#endif // ENABLE_AERIAL
  for (uint_fast8_t symbol_idx = 0; symbol_idx < symbols_to_fill; ++symbol_idx) {
    nfapi_v4_srs_parameters_symbols_t *dst_symbol = &dst->symbol_list[symbol_idx];
    const nfapi_v4_srs_parameters_symbols_t *src_symbol = &src->symbol_list[symbol_idx];
    dst_symbol->srs_bandwidth_start = src_symbol->srs_bandwidth_start;
    dst_symbol->sequence_group = src_symbol->sequence_group;
    dst_symbol->sequence_number = src_symbol->sequence_number;
  }
  dst->usage = src->usage;
  const uint8_t nUsage = __builtin_popcount(dst->usage);
  for (int idx = 0; idx < nUsage; ++idx) {
    dst->report_type[idx] = src->report_type[idx];
  }
  dst->singular_Value_representation = src->singular_Value_representation;
  dst->iq_representation = src->iq_representation;
  dst->prg_size = src->prg_size;
  dst->num_total_ue_antennas = src->num_total_ue_antennas;
  dst->ue_antennas_in_this_srs_resource_set = src->ue_antennas_in_this_srs_resource_set;
  dst->sampled_ue_antennas = src->sampled_ue_antennas;
  dst->report_scope = src->report_scope;
  COPY_STRUCT_ARRAY(dst, src, Ul_spatial_stream_ports, num_ul_spatial_streams_ports);
}

static void copy_ul_tti_request_srs_pdu(const nfapi_nr_srs_pdu_t *src, nfapi_nr_srs_pdu_t *dst)
{
  PARTIAL_COPY(dst, src, nfapi_nr_srs_pdu_t, t_offset);
  copy_ul_tti_beamforming(&src->beamforming, &dst->beamforming);
  copy_ul_tti_request_srs_parameters(&src->srs_parameters_v4, 1 << src->num_symbols, &dst->srs_parameters_v4);
}

void copy_ul_tti_request(const nfapi_nr_ul_tti_request_t *src, nfapi_nr_ul_tti_request_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->n_pdus = src->n_pdus;
  dst->rach_present = src->rach_present;
  dst->n_ulsch = src->n_ulsch;
  dst->n_ulcch = src->n_ulcch;
  dst->n_group = src->n_group;

  for (int PDU = 0; PDU < src->n_pdus; ++PDU) {
    // take the PDU at the start of loops
    const nfapi_nr_ul_tti_request_number_of_pdus_t *src_pdu = &src->pdus_list[PDU];
    nfapi_nr_ul_tti_request_number_of_pdus_t *dst_pdu = &dst->pdus_list[PDU];

    dst_pdu->pdu_type = src_pdu->pdu_type;
    dst_pdu->pdu_size = src_pdu->pdu_size;

    switch (src_pdu->pdu_type) {
      case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE:
        copy_ul_tti_request_prach_pdu(&src_pdu->prach_pdu, &dst_pdu->prach_pdu);
        break;
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        copy_ul_tti_request_pusch_pdu(&src_pdu->pusch_pdu, &dst_pdu->pusch_pdu);
        break;
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
        copy_ul_tti_request_pucch_pdu(&src_pdu->pucch_pdu, &dst_pdu->pucch_pdu);
        break;
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
        copy_ul_tti_request_srs_pdu(&src_pdu->srs_pdu, &dst_pdu->srs_pdu);
        break;
      default:
        // PDU Type is not any known value
        AssertFatal(1 == 0, "PDU Type value unknown, allowed values range from 0 to 3\n");
    }
  }

  for (int nGroup = 0; nGroup < src->n_group; ++nGroup) {
    dst->groups_list[nGroup].n_ue = src->groups_list[nGroup].n_ue;
    for (int UE = 0; UE < src->groups_list[nGroup].n_ue; ++UE) {
      dst->groups_list[nGroup].ue_list[UE].pdu_idx = src->groups_list[nGroup].ue_list[UE].pdu_idx;
    }
  }
}

void copy_slot_indication(const nfapi_nr_slot_indication_scf_t *src, nfapi_nr_slot_indication_scf_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->sfn = src->sfn;
  dst->slot = src->slot;
}

void copy_ul_dci_request_pdu(const nfapi_nr_ul_dci_request_pdus_t *src, nfapi_nr_ul_dci_request_pdus_t *dst)
{
  dst->PDUType = src->PDUType;
  dst->PDUSize = src->PDUSize;

  // Is the same PDU as the DL_TTI.request PDCCH PDU, reuse copy function
  copy_dl_tti_request_pdcch_pdu(&src->pdcch_pdu.pdcch_pdu_rel15, &dst->pdcch_pdu.pdcch_pdu_rel15);
}

void copy_ul_dci_request(const nfapi_nr_ul_dci_request_t *src, nfapi_nr_ul_dci_request_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->numPdus = src->numPdus;

  for (int pdu_idx = 0; pdu_idx < src->numPdus; ++pdu_idx) {
    copy_ul_dci_request_pdu(&src->ul_dci_pdu_list[pdu_idx], &dst->ul_dci_pdu_list[pdu_idx]);
  }
}

void copy_tx_data_request_PDU(const nfapi_nr_pdu_t *src, nfapi_nr_pdu_t *dst)
{
  dst->PDU_length = src->PDU_length;
  dst->PDU_index = src->PDU_index;
  dst->num_TLV = src->num_TLV;
  for (int tlv_idx = 0; tlv_idx < src->num_TLV; ++tlv_idx) {
    const nfapi_nr_tx_data_request_tlv_t *src_tlv = &src->TLVs[tlv_idx];
    nfapi_nr_tx_data_request_tlv_t *dst_tlv = &dst->TLVs[tlv_idx];
    dst_tlv->tag = src_tlv->tag;
    dst_tlv->length = src_tlv->length;
    switch (src_tlv->tag) {
      case 0:
        // value.direct
        memcpy(dst_tlv->value.direct, src_tlv->value.direct, sizeof(src_tlv->value.direct));
        break;
      case 1:
        // value.ptr: instead of allocating a new pointer, we avoid overhead
        // and copy into direct
        memcpy(dst_tlv->value.direct, src_tlv->value.ptr, src_tlv->length);
        dst_tlv->tag = 0;
        break;
      default:
        AssertFatal(1 == 0, "TX_DATA request TLV tag value unsupported");
        break;
    }
  }
}

void copy_tx_data_request(const nfapi_nr_tx_data_request_t *src, nfapi_nr_tx_data_request_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->Number_of_PDUs = src->Number_of_PDUs;
  for (int pdu_idx = 0; pdu_idx < src->Number_of_PDUs; ++pdu_idx) {
    copy_tx_data_request_PDU(&src->pdu_list[pdu_idx], &dst->pdu_list[pdu_idx]);
  }
}

size_t get_tx_data_request_size(const nfapi_nr_tx_data_request_t *msg)
{
  // Get size of the whole message ( allocated pointer included )
  size_t total_size = sizeof(msg->header);
  total_size += sizeof(msg->SFN);
  total_size += sizeof(msg->Slot);
  total_size += sizeof(msg->Number_of_PDUs);
  for (int pdu_idx = 0; pdu_idx < msg->Number_of_PDUs; ++pdu_idx) {
    const nfapi_nr_pdu_t *pdu = &msg->pdu_list[pdu_idx];
    total_size += sizeof(pdu->PDU_length);
    total_size += sizeof(pdu->PDU_index);
    total_size += sizeof(pdu->num_TLV);
    for (int tlv_idx = 0; tlv_idx < pdu->num_TLV; ++tlv_idx) {
      const nfapi_nr_tx_data_request_tlv_t *tlv = &pdu->TLVs[tlv_idx];
      total_size += sizeof(tlv->tag);
      total_size += sizeof(tlv->length);
      if (tlv->tag == 0) {
        total_size += sizeof(tlv->value.direct);
      } else {
        total_size += (tlv->length + 3) / 4 * sizeof(uint32_t);
      }
    }
  }
  return total_size;
}

void copy_rx_data_indication_PDU(const nfapi_nr_rx_data_pdu_t *src, nfapi_nr_rx_data_pdu_t *dst)
{
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->harq_id = src->harq_id;
  dst->pdu_length = src->pdu_length;
  dst->ul_cqi = src->ul_cqi;
  dst->timing_advance = src->timing_advance;
  dst->rssi = src->rssi;
  dst->pdu = calloc(dst->pdu_length, sizeof(uint8_t));
  memcpy(dst->pdu, src->pdu, src->pdu_length);
}

void copy_rx_data_indication(const nfapi_nr_rx_data_indication_t *src, nfapi_nr_rx_data_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->number_of_pdus = src->number_of_pdus;
  dst->pdu_list = calloc(dst->number_of_pdus, sizeof(*dst->pdu_list));
  for (int pdu_idx = 0; pdu_idx < src->number_of_pdus; ++pdu_idx) {
    copy_rx_data_indication_PDU(&src->pdu_list[pdu_idx], &dst->pdu_list[pdu_idx]);
  }
}

size_t get_rx_data_indication_size(const nfapi_nr_rx_data_indication_t *msg)
{
  // Get size of the whole message ( allocated pointer included )
  size_t total_size = sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->number_of_pdus);
  for (int pdu_idx = 0; pdu_idx < msg->number_of_pdus; ++pdu_idx) {
    const nfapi_nr_rx_data_pdu_t *pdu = &msg->pdu_list[pdu_idx];
    total_size += sizeof(pdu->handle);
    total_size += sizeof(pdu->rnti);
    total_size += sizeof(pdu->harq_id);
    total_size += sizeof(pdu->pdu_length);
    total_size += sizeof(pdu->ul_cqi);
    total_size += sizeof(pdu->timing_advance);
    total_size += sizeof(pdu->rssi);
    total_size += pdu->pdu_length;
  }
  return total_size;
}

void copy_crc_indication_CRC(const nfapi_nr_crc_t *src, nfapi_nr_crc_t *dst)
{
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->harq_id = src->harq_id;
  dst->tb_crc_status = src->tb_crc_status;
  dst->num_cb = src->num_cb;
  if (dst->num_cb > 0) {
    const uint16_t cb_len = nr_bits_to_bytes(dst->num_cb);
    dst->cb_crc_status = calloc(cb_len, sizeof(uint8_t));
    for (int cb = 0; cb < cb_len; ++cb) {
      dst->cb_crc_status[cb] = src->cb_crc_status[cb];
    }
  }
  dst->ul_cqi = src->ul_cqi;
  dst->timing_advance = src->timing_advance;
  dst->rssi = src->rssi;
}

void copy_crc_indication(const nfapi_nr_crc_indication_t *src, nfapi_nr_crc_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;

  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->number_crcs = src->number_crcs;
  dst->crc_list = calloc(dst->number_crcs, sizeof(*dst->crc_list));
  for (int crc_idx = 0; crc_idx < src->number_crcs; ++crc_idx) {
    copy_crc_indication_CRC(&src->crc_list[crc_idx], &dst->crc_list[crc_idx]);
  }
}

size_t get_crc_indication_size(const nfapi_nr_crc_indication_t *msg)
{
  // Get size of the whole message ( allocated pointer included )
  size_t total_size = sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->number_crcs);
  for (int crc_idx = 0; crc_idx < msg->number_crcs; ++crc_idx) {
    const nfapi_nr_crc_t *crc = &msg->crc_list[crc_idx];
    total_size += sizeof(crc->handle);
    total_size += sizeof(crc->rnti);
    total_size += sizeof(crc->harq_id);
    total_size += sizeof(crc->tb_crc_status);
    total_size += sizeof(crc->num_cb);
    if (crc->num_cb > 0) {
      total_size += crc->num_cb / 8 + 1;
    }
    total_size += sizeof(crc->ul_cqi);
    total_size += sizeof(crc->timing_advance);
    total_size += sizeof(crc->rssi);
  }
  return total_size;
}

void copy_uci_indication_sr_pdu_0_1(const nfapi_nr_sr_pdu_0_1_t *src, nfapi_nr_sr_pdu_0_1_t *dst)
{
  dst->sr_indication = src->sr_indication;
  dst->sr_confidence_level = src->sr_confidence_level;
}

void copy_uci_indication_sr_pdu_2_3_4(const nfapi_nr_sr_pdu_2_3_4_t *src, nfapi_nr_sr_pdu_2_3_4_t *dst)
{
  dst->sr_bit_len = src->sr_bit_len;
  const uint16_t sr_len = nr_bits_to_bytes(dst->sr_bit_len);
  dst->sr_payload = calloc(sr_len, sizeof(*dst->sr_payload));
  for (int i = 0; i < sr_len; ++i) {
    dst->sr_payload[i] = src->sr_payload[i];
  }
}

void copy_uci_indication_harq_pdu_0_1(const nfapi_nr_harq_pdu_0_1_t *src, nfapi_nr_harq_pdu_0_1_t *dst)
{
  dst->num_harq = src->num_harq;
  dst->harq_confidence_level = src->harq_confidence_level;
  for (int i = 0; i < dst->num_harq; ++i) {
    dst->harq_list[i].harq_value = src->harq_list[i].harq_value;
  }
}

void copy_uci_indication_harq_pdu_2_3_4(const nfapi_nr_harq_pdu_2_3_4_t *src, nfapi_nr_harq_pdu_2_3_4_t *dst)
{
  dst->harq_crc = src->harq_crc;
  dst->harq_bit_len = src->harq_bit_len;
  const uint16_t harq_length = nr_bits_to_bytes(dst->harq_bit_len);
  dst->harq_payload = calloc(harq_length, sizeof(*dst->harq_payload));
  for (int i = 0; i < harq_length; ++i) {
    dst->harq_payload[i] = src->harq_payload[i];
  }
}

void copy_uci_indication_csi_part1(const nfapi_nr_csi_part1_pdu_t *src, nfapi_nr_csi_part1_pdu_t *dst)
{
  dst->csi_part1_crc = src->csi_part1_crc;
  dst->csi_part1_bit_len = src->csi_part1_bit_len;
  const uint16_t payload_length = nr_bits_to_bytes(dst->csi_part1_bit_len);
  dst->csi_part1_payload = calloc(payload_length, sizeof(*dst->csi_part1_payload));
  for (int i = 0; i < payload_length; ++i) {
    dst->csi_part1_payload[i] = src->csi_part1_payload[i];
  }
}

void copy_uci_indication_csi_part2(const nfapi_nr_csi_part2_pdu_t *src, nfapi_nr_csi_part2_pdu_t *dst)
{
  dst->csi_part2_crc = src->csi_part2_crc;
  dst->csi_part2_bit_len = src->csi_part2_bit_len;
  const uint16_t payload_length = nr_bits_to_bytes(dst->csi_part2_bit_len);
  dst->csi_part2_payload = calloc(payload_length, sizeof(*dst->csi_part2_payload));
  for (int i = 0; i < payload_length; ++i) {
    dst->csi_part2_payload[i] = src->csi_part2_payload[i];
  }
}

void copy_uci_indication_PUSCH(const nfapi_nr_uci_pusch_pdu_t *src, nfapi_nr_uci_pusch_pdu_t *dst)
{
  dst->pduBitmap = src->pduBitmap;
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->ul_cqi = src->ul_cqi;
  dst->timing_advance = src->timing_advance;
  dst->rssi = src->rssi;

  // Bit 0 not used in PUSCH PDU
  // HARQ
  if ((dst->pduBitmap >> 1) & 0x01) {
    copy_uci_indication_harq_pdu_2_3_4(&src->harq, &dst->harq);
  }
  // CSI Part 1
  if ((dst->pduBitmap >> 2) & 0x01) {
    copy_uci_indication_csi_part1(&src->csi_part1, &dst->csi_part1);
  }
  // CSI Part 2
  if ((dst->pduBitmap >> 3) & 0x01) {
    copy_uci_indication_csi_part2(&src->csi_part2, &dst->csi_part2);
  }
}

void copy_uci_indication_PUCCH_0_1(const nfapi_nr_uci_pucch_pdu_format_0_1_t *src, nfapi_nr_uci_pucch_pdu_format_0_1_t *dst)
{
  dst->pduBitmap = src->pduBitmap;
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->pucch_format = src->pucch_format;
  dst->ul_cqi = src->ul_cqi;
  dst->timing_advance = src->timing_advance;
  dst->rssi = src->rssi;

  // SR
  if (dst->pduBitmap & 0x01) {
    copy_uci_indication_sr_pdu_0_1(&src->sr, &dst->sr);
  }
  // HARQ
  if ((dst->pduBitmap >> 1) & 0x01) {
    copy_uci_indication_harq_pdu_0_1(&src->harq, &dst->harq);
  }
}

void copy_uci_indication_PUCCH_2_3_4(const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *src, nfapi_nr_uci_pucch_pdu_format_2_3_4_t *dst)
{
  dst->pduBitmap = src->pduBitmap;
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->pucch_format = src->pucch_format;
  dst->ul_cqi = src->ul_cqi;
  dst->timing_advance = src->timing_advance;
  dst->rssi = src->rssi;
  // SR
  if (dst->pduBitmap & 0x01) {
    copy_uci_indication_sr_pdu_2_3_4(&src->sr, &dst->sr);
  }
  // HARQ
  if ((dst->pduBitmap >> 1) & 0x01) {
    copy_uci_indication_harq_pdu_2_3_4(&src->harq, &dst->harq);
  }
  // CSI Part 1
  if ((dst->pduBitmap >> 2) & 0x01) {
    copy_uci_indication_csi_part1(&src->csi_part1, &dst->csi_part1);
  }
  // CSI Part 2
  if ((dst->pduBitmap >> 3) & 0x01) {
    copy_uci_indication_csi_part2(&src->csi_part2, &dst->csi_part2);
  }
}

void copy_uci_indication_UCI(const nfapi_nr_uci_t *src, nfapi_nr_uci_t *dst)
{
  dst->pdu_type = src->pdu_type;
  dst->pdu_size = src->pdu_size;
  switch (dst->pdu_type) {
    case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
      copy_uci_indication_PUSCH(&src->pusch_pdu, &dst->pusch_pdu);
      break;
    case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE:
      copy_uci_indication_PUCCH_0_1(&src->pucch_pdu_format_0_1, &dst->pucch_pdu_format_0_1);
      break;
    case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE:
      copy_uci_indication_PUCCH_2_3_4(&src->pucch_pdu_format_2_3_4, &dst->pucch_pdu_format_2_3_4);
      break;
    default:
      AssertFatal(1 == 0, "Unknown UCI.indication PDU Type %d\n", src->pdu_type);
      break;
  }
}

void copy_uci_indication(const nfapi_nr_uci_indication_t *src, nfapi_nr_uci_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;
  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->num_ucis = src->num_ucis;
  dst->uci_list = calloc(dst->num_ucis, sizeof(*dst->uci_list));
  for (int crc_idx = 0; crc_idx < src->num_ucis; ++crc_idx) {
    copy_uci_indication_UCI(&src->uci_list[crc_idx], &dst->uci_list[crc_idx]);
  }
}

size_t get_uci_indication_size(const nfapi_nr_uci_indication_t *msg)
{
  // Get size of the message (allocated pointer included)
  size_t total_size = 0;

  // Header and fixed-size fields
  total_size += sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->num_ucis);

  // Loop through each UCI in the uci_list
  for (int uci_idx = 0; uci_idx < msg->num_ucis; ++uci_idx) {
    const nfapi_nr_uci_t *uci_pdu = &msg->uci_list[uci_idx];

    total_size += sizeof(uci_pdu->pdu_type);
    total_size += sizeof(uci_pdu->pdu_size);

    switch (uci_pdu->pdu_type) {
      case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
        total_size += sizeof(uci_pdu->pusch_pdu);

        // HARQ payload, CSI Part 1 and 2 are conditionally allocated
        if ((uci_pdu->pusch_pdu.pduBitmap >> 1) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pusch_pdu.harq.harq_bit_len);
        }
        if ((uci_pdu->pusch_pdu.pduBitmap >> 2) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pusch_pdu.csi_part1.csi_part1_bit_len);
        }
        if ((uci_pdu->pusch_pdu.pduBitmap >> 3) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pusch_pdu.csi_part2.csi_part2_bit_len);
        }
        break;

      case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE:
        total_size += sizeof(uci_pdu->pucch_pdu_format_0_1);
        // SR and HARQ are conditionally filled, but neither require calloc inside
        break;

      case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE:
        total_size += sizeof(uci_pdu->pucch_pdu_format_2_3_4);

        // SR, HARQ, CSI Part 1, and CSI Part 2 are conditionally allocated
        if (uci_pdu->pucch_pdu_format_2_3_4.pduBitmap & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pucch_pdu_format_2_3_4.sr.sr_bit_len);
        }
        if ((uci_pdu->pucch_pdu_format_2_3_4.pduBitmap >> 1) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pucch_pdu_format_2_3_4.harq.harq_bit_len);
        }
        if ((uci_pdu->pucch_pdu_format_2_3_4.pduBitmap >> 2) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pucch_pdu_format_2_3_4.csi_part1.csi_part1_bit_len);
        }
        if ((uci_pdu->pucch_pdu_format_2_3_4.pduBitmap >> 3) & 0x01) {
          total_size += nr_bits_to_bytes(uci_pdu->pucch_pdu_format_2_3_4.csi_part2.csi_part2_bit_len);
        }
        break;

      default:
        AssertFatal(1 == 0, "Unknown UCI.indication PDU Type %d\n", uci_pdu->pdu_type);
        break;
    }
  }

  // Finally, add the size of the uci_list pointer itself
  total_size += msg->num_ucis * sizeof(*msg->uci_list);

  return total_size;
}

void copy_srs_indication_report_tlv(const nfapi_srs_report_tlv_t *src, nfapi_srs_report_tlv_t *dst)
{
  dst->tag = src->tag;
  dst->length = src->length;
  for (int i = 0; i < (dst->length + 3) / 4; ++i) {
    dst->value[i] = src->value[i];
  }
}

void copy_srs_indication_PDU(const nfapi_nr_srs_indication_pdu_t *src, nfapi_nr_srs_indication_pdu_t *dst)
{
  dst->handle = src->handle;
  dst->rnti = src->rnti;
  dst->timing_advance_offset = src->timing_advance_offset;
  dst->timing_advance_offset_nsec = src->timing_advance_offset_nsec;
  dst->srs_usage = src->srs_usage;
  dst->report_type = src->report_type;
  copy_srs_indication_report_tlv(&src->report_tlv, &dst->report_tlv);
}

void copy_srs_indication(const nfapi_nr_srs_indication_t *src, nfapi_nr_srs_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;
  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->control_length = src->control_length;
  dst->number_of_pdus = src->number_of_pdus;
  dst->pdu_list = calloc(dst->number_of_pdus, sizeof(*dst->pdu_list));
  for (int pdu_idx = 0; pdu_idx < src->number_of_pdus; ++pdu_idx) {
    copy_srs_indication_PDU(&src->pdu_list[pdu_idx], &dst->pdu_list[pdu_idx]);
  }
}

void copy_srs_toa_vendor_ext_indication(const nfapi_nr_srs_toa_vendor_ext_indication_t *src,
                                        nfapi_nr_srs_toa_vendor_ext_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;
  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->rnti = src->rnti;
  dst->num_ta = src->num_ta;
  for (int ta_idx = 0; ta_idx < src->num_ta; ++ta_idx) {
    memcpy(dst->ta_offset_nsec, src->ta_offset_nsec, sizeof(dst->ta_offset_nsec));
  }
}

size_t get_srs_indication_size(const nfapi_nr_srs_indication_t *msg)
{
  // Get size of the message (allocated pointer included)
  size_t total_size = 0;

  // Header and fixed-size fields
  total_size += sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->control_length);
  total_size += sizeof(msg->number_of_pdus);
  total_size += msg->number_of_pdus * sizeof(*msg->pdu_list);

  return total_size;
}

size_t get_srs_toa_vendor_ext_indication_size(const nfapi_nr_srs_toa_vendor_ext_indication_t *msg)
{
  // Get size of the message (allocated pointer included)
  size_t total_size = 0;

  // Header and fixed-size fields
  total_size += sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->rnti);
  total_size += sizeof(msg->num_ta);
  total_size += sizeof(msg->ta_offset_nsec);

  return total_size;
}

static void copy_rach_indication_PDU(const nfapi_nr_prach_indication_pdu_t *src, nfapi_nr_prach_indication_pdu_t *dst)
{
  dst->phy_cell_id = src->phy_cell_id;
  dst->symbol_index = src->symbol_index;
  dst->slot_index = src->slot_index;
  dst->freq_index = src->freq_index;
  dst->avg_rssi = src->avg_rssi;
  dst->avg_snr = src->avg_snr;
  dst->num_preamble = src->num_preamble;
  for (int preamble_idx = 0; preamble_idx < dst->num_preamble; ++preamble_idx) {
    nfapi_nr_prach_indication_preamble_t *dst_pr = &dst->preamble_list[preamble_idx];
    const nfapi_nr_prach_indication_preamble_t *src_pr = &src->preamble_list[preamble_idx];
    dst_pr->preamble_index = src_pr->preamble_index;
    dst_pr->timing_advance = src_pr->timing_advance;
    dst_pr->preamble_pwr = src_pr->preamble_pwr;
  }
}

void copy_rach_indication(const nfapi_nr_rach_indication_t *src, nfapi_nr_rach_indication_t *dst)
{
  dst->header.phy_id = src->header.phy_id;
  dst->header.message_id = src->header.message_id;
  dst->header.message_length = src->header.message_length;
  dst->sfn = src->sfn;
  dst->slot = src->slot;
  dst->number_of_pdus = src->number_of_pdus;
  dst->pdu_list = calloc(dst->number_of_pdus, sizeof(*dst->pdu_list));
  for (int pdu_idx = 0; pdu_idx < dst->number_of_pdus; ++pdu_idx) {
    copy_rach_indication_PDU(&src->pdu_list[pdu_idx], &dst->pdu_list[pdu_idx]);
  }
}

size_t get_rach_indication_size(nfapi_nr_rach_indication_t *msg)
{
  // Get size of the message (allocated pointer included)
  size_t total_size = 0;

  // Header and fixed-size fields
  total_size += sizeof(msg->header);
  total_size += sizeof(msg->sfn);
  total_size += sizeof(msg->slot);
  total_size += sizeof(msg->number_of_pdus);
  total_size += msg->number_of_pdus * sizeof(*msg->pdu_list);

  return total_size;
}

static void dump_p7_message_header(const nfapi_nr_p7_message_header_t *hdr, int depth)
{
  INDENTED_PRINTF("Message ID = 0x%02x\n", hdr->message_id);
  INDENTED_PRINTF("Message Length = 0x%02x\n", hdr->message_length);
}

static void dump_dl_beamforming_pdu(const nfapi_nr_tx_precoding_and_beamforming_t *pdu, int depth)
{
  INDENTED_PRINTF("numPRGs = %d\n", pdu->num_prgs);
  INDENTED_PRINTF("prgSize = %d\n", pdu->prg_size);
  INDENTED_PRINTF("digBFInterfaces = %d\n", pdu->dig_bf_interfaces);
  depth++;
  for (int i = 0; i < pdu->num_prgs; ++i) {
    INDENTED_PRINTF("PMIdx = %d\n", pdu->prgs_list[i].pm_idx);
    depth++;
    for (int j = 0; j < pdu->dig_bf_interfaces; ++j) {
      INDENTED_PRINTF("beamIDX = %d\n", pdu->prgs_list[i].dig_bf_interface_list[j].beam_idx);
    }
    depth--;
  }
}

static void dump_dl_dci_pdu(const nfapi_nr_dl_dci_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("RNTI = 0x%x\n", pdu->RNTI);
  INDENTED_PRINTF("ScramblingId = %d\n", pdu->ScramblingId);
  INDENTED_PRINTF("ScramblingRNTI = 0x%x\n", pdu->ScramblingRNTI);
  INDENTED_PRINTF("CCEIndex = %d\n", pdu->CceIndex);
  INDENTED_PRINTF("AggregationLevel = %d\n", pdu->AggregationLevel);
  dump_dl_beamforming_pdu(&pdu->precodingAndBeamforming, depth + 1);
  INDENTED_PRINTF("beta_PDCCH_1_0 = %d\n", pdu->beta_PDCCH_1_0);
  INDENTED_PRINTF("powerControlOffsetSS = %d\n", pdu->powerControlOffsetSS);
  INDENTED_PRINTF("PayloadSizeBits = %d\n", pdu->PayloadSizeBits);
  INDENTED_PRINTF("Payload = ");
  for (int i = 0; i < nr_bits_to_bytes(pdu->PayloadSizeBits); ++i) {
    printf("0x%02x ", pdu->Payload[i]);
  }
  printf("\n");
}

static void dump_dl_tti_request_PDCCH_PDU(const nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdu, int depth)
{
  INDENTED_PRINTF("BWPSize = %d\n", pdu->BWPSize);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->BWPStart);
  INDENTED_PRINTF("Subcarrier Spacing = %d\n", pdu->SubcarrierSpacing);
  INDENTED_PRINTF("Ciclic Prefix = %d\n", pdu->CyclicPrefix);
  INDENTED_PRINTF("StartSymbolIndex = %d\n", pdu->StartSymbolIndex);
  INDENTED_PRINTF("DurationSymbols = %d\n", pdu->DurationSymbols);
  INDENTED_PRINTF("FrequencyDomainResource : ");
  for (int i = 0; i < 6; ++i) {
    printf("%d ", pdu->FreqDomainResource[i]);
  }
  printf("\n");
  INDENTED_PRINTF("CCE Reg Mapping Type = %d\n", pdu->CceRegMappingType);
  INDENTED_PRINTF("Reg Bundle Size = %d\n", pdu->RegBundleSize);
  INDENTED_PRINTF("Interleaver Size = %d\n", pdu->InterleaverSize);
  INDENTED_PRINTF("CoreSetType = %d\n", pdu->CoreSetType);
  INDENTED_PRINTF("ShiftIndex = %d\n", pdu->ShiftIndex);
  INDENTED_PRINTF("precoderGranularity = %d\n", pdu->precoderGranularity);
  INDENTED_PRINTF("numDLDCI = %d\n", pdu->numDlDci);
  for (int i = 0; i < pdu->numDlDci; ++i) {
    dump_dl_dci_pdu(&pdu->dci_pdu[i], depth + 1);
  }
}

static void dump_dl_tti_request_PDSCH_PDU(const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdu, int depth)
{
  INDENTED_PRINTF("pduBitmap = %d\n", pdu->pduBitmap);
  INDENTED_PRINTF("RNTI = 0x%x\n", pdu->rnti);
  INDENTED_PRINTF("pduIndex = %d\n", pdu->pduIndex);
  INDENTED_PRINTF("BWPSize = %d\n", pdu->BWPSize);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->BWPStart);
  INDENTED_PRINTF("SubcarrierSpacing = %d\n", pdu->SubcarrierSpacing);
  INDENTED_PRINTF("CyclicPrefix = %d\n", pdu->CyclicPrefix);
  INDENTED_PRINTF("NrOfCodewords = %d\n", pdu->NrOfCodewords);
  for (int i = 0; i < pdu->NrOfCodewords; ++i) {
    depth++;
    INDENTED_PRINTF("Codeword #%d\n", i);
    depth++;
    INDENTED_PRINTF("targetCoderate = %d\n", pdu->targetCodeRate[i]);
    INDENTED_PRINTF("qamModOrder = %d\n", pdu->qamModOrder[i]);
    INDENTED_PRINTF("mcsIndex = %d\n", pdu->mcsIndex[i]);
    INDENTED_PRINTF("mcsTable = %d\n", pdu->mcsTable[i]);
    INDENTED_PRINTF("rvIndex = %d\n", pdu->rvIndex[i]);
    INDENTED_PRINTF("TBSize = %d\n", pdu->TBSize[i]);
    depth--;
    depth--;
  }

  INDENTED_PRINTF("dataScramblingId = %d\n", pdu->dataScramblingId);
  INDENTED_PRINTF("nrOfLayers = %d\n", pdu->nrOfLayers);
  INDENTED_PRINTF("transmissionScheme = %d\n", pdu->transmissionScheme);
  INDENTED_PRINTF("refPoint = %d\n", pdu->refPoint);
  INDENTED_PRINTF("dlDmrsSymbPos = %d\n", pdu->dlDmrsSymbPos);
  INDENTED_PRINTF("dmrsConfigType = %d\n", pdu->dmrsConfigType);
  INDENTED_PRINTF("dlDmrsScramblingId = %d\n", pdu->dlDmrsScramblingId);
  INDENTED_PRINTF("SCID = %d\n", pdu->SCID);
  INDENTED_PRINTF("numDmrsCdmGrpsNoData = %d\n", pdu->numDmrsCdmGrpsNoData);
  INDENTED_PRINTF("dmrsPorts = %d\n", pdu->dmrsPorts);
  INDENTED_PRINTF("resourceAlloc = %d\n", pdu->resourceAlloc);
  INDENTED_PRINTF("rbBitmap = ");
  for (int i = 0; i < 36; ++i) {
    printf("%d ", pdu->rbBitmap[i]);
  }
  printf("\n");
  INDENTED_PRINTF("rbStart = %d\n", pdu->rbStart);
  INDENTED_PRINTF("rbSize = %d\n", pdu->rbSize);
  INDENTED_PRINTF("VRBtoPRBMapping = %d\n", pdu->VRBtoPRBMapping);
  INDENTED_PRINTF("StartSymbolIndex = %d\n", pdu->StartSymbolIndex);
  INDENTED_PRINTF("NrOfSymbols = %d\n", pdu->NrOfSymbols);

  INDENTED_PRINTF("PTRSPortIndex = %d\n", pdu->PTRSPortIndex);
  INDENTED_PRINTF("PTRSTimeDensity = %d\n", pdu->PTRSTimeDensity);
  INDENTED_PRINTF("PTRSFreqDensity = %d\n", pdu->PTRSFreqDensity);
  INDENTED_PRINTF("PTRSReOffset = %d\n", pdu->PTRSReOffset);
  INDENTED_PRINTF("nEpreRatioOfPDSCHToPTRS = %d\n", pdu->nEpreRatioOfPDSCHToPTRS);

  dump_dl_beamforming_pdu(&pdu->precodingAndBeamforming, depth + 1);
  INDENTED_PRINTF("powerControlOffset = %d\n", pdu->powerControlOffset);
  INDENTED_PRINTF("powerControlOffsetSS = %d\n", pdu->powerControlOffsetSS);

  INDENTED_PRINTF("isLastCbPresent = %d\n", pdu->isLastCbPresent);
  INDENTED_PRINTF("isInlineTbCrc = %d\n", pdu->isInlineTbCrc);
  INDENTED_PRINTF("dlTbCrc = %d\n", pdu->dlTbCrc);
}

static void dump_dl_tti_request_CSI_RS_PDU(const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *pdu, int depth)
{
  INDENTED_PRINTF("BWPSize = %d\n", pdu->bwp_size);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->bwp_start);
  INDENTED_PRINTF("SubcarrierSpacing = %d\n", pdu->subcarrier_spacing);
  INDENTED_PRINTF("CyclicPrefix = %d\n", pdu->cyclic_prefix);
  INDENTED_PRINTF("StartRB = %d\n", pdu->start_rb);
  INDENTED_PRINTF("NrOfRBs = %d\n", pdu->nr_of_rbs);
  INDENTED_PRINTF("CSIType = %d\n", pdu->csi_type);
  INDENTED_PRINTF("Row = %d\n", pdu->row);
  INDENTED_PRINTF("FreqDomain = %d\n", pdu->freq_domain);
  INDENTED_PRINTF("SymbL0 = %d\n", pdu->symb_l0);
  INDENTED_PRINTF("SymbL1 = %d\n", pdu->symb_l1);
  INDENTED_PRINTF("CDMType = %d\n", pdu->cdm_type);
  INDENTED_PRINTF("FreqDensity = %d\n", pdu->freq_density);
  INDENTED_PRINTF("ScrambId = %d\n", pdu->scramb_id);
  INDENTED_PRINTF("powerControllOffset = %d\n", pdu->power_control_offset);
  INDENTED_PRINTF("powerControllOffsetSS = %d\n", pdu->power_control_offset_ss);
  dump_dl_beamforming_pdu(&pdu->precodingAndBeamforming, depth + 1);
}

static void dump_dl_tti_request_SSB_PDU(const nfapi_nr_dl_tti_ssb_pdu_rel15_t *pdu, int depth)
{
  INDENTED_PRINTF("physCellId = %d\n", pdu->PhysCellId);
  INDENTED_PRINTF("BetaPss = %d\n", pdu->BetaPss);
  INDENTED_PRINTF("SSBBlockIndex = %d\n", pdu->SsbBlockIndex);
  INDENTED_PRINTF("SSBSubcarrierOffset = %d\n", pdu->SsbSubcarrierOffset);
  INDENTED_PRINTF("SSBOffsetPointA = %d\n", pdu->ssbOffsetPointA);
  INDENTED_PRINTF("bchPayloadFlag = %d\n", pdu->bchPayloadFlag);
  INDENTED_PRINTF("bchPayload = %x\n", pdu->bchPayload);
  dump_dl_beamforming_pdu(&pdu->precoding_and_beamforming, depth + 1);
}

static char *dl_tti_pdu_type_to_str(uint16_t pdu_type)
{
  switch (pdu_type) {
    case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
      return "NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE";
    case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE:
      return "NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE";
    case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
      return "NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE";
    case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
      return "NFAPI_NR_DL_TTI_SSB_PDU_TYPE";
    default:
      return "Unknown";
      break;
  }
}

void dump_dl_tti_request(const nfapi_nr_dl_tti_request_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->SFN);
  INDENTED_PRINTF("Slot = %d\n", msg->Slot);
  INDENTED_PRINTF("nPDUs = %d\n", msg->dl_tti_request_body.nPDUs);
  INDENTED_PRINTF("nGroup = %d\n", msg->dl_tti_request_body.nGroup);
  for (int i = 0; i < msg->dl_tti_request_body.nPDUs; ++i) {
    const nfapi_nr_dl_tti_request_pdu_t *pdu = &msg->dl_tti_request_body.dl_tti_pdu_list[i];
    depth++;
    INDENTED_PRINTF("PDU #%d\n", i);
    INDENTED_PRINTF("PDUType = 0x%02x (%s)\n", pdu->PDUType, dl_tti_pdu_type_to_str(pdu->PDUType));
    INDENTED_PRINTF("PDUSize = 0x%02x\n", pdu->PDUSize);
    /* call to dump_dl_tti_request_<PDU_Type>_pdu */
    switch (pdu->PDUType) {
      case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
        dump_dl_tti_request_PDCCH_PDU(&pdu->pdcch_pdu.pdcch_pdu_rel15, depth + 1);
        break;
      case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE:
        dump_dl_tti_request_PDSCH_PDU(&pdu->pdsch_pdu.pdsch_pdu_rel15, depth + 1);
        break;
      case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
        dump_dl_tti_request_CSI_RS_PDU(&pdu->csi_rs_pdu.csi_rs_pdu_rel15, depth + 1);
        break;
      case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
        dump_dl_tti_request_SSB_PDU(&pdu->ssb_pdu.ssb_pdu_rel15, depth + 1);
        break;
      default:
        INDENTED_PRINTF("Unknown PDU type 0x%02x\n", pdu->PDUType);
        break;
    }
    depth--;
  }
  for (int group_idx = 0; group_idx < msg->dl_tti_request_body.nGroup; group_idx++) {
    depth++;
    INDENTED_PRINTF("Group #%d\n", group_idx);
    INDENTED_PRINTF("nUe[%d] = 0x%02x\n", group_idx, msg->dl_tti_request_body.nUe[group_idx]);
    for (int ue_idx = 0; ue_idx < msg->dl_tti_request_body.nUe[group_idx]; ue_idx++) {
      depth++;
      INDENTED_PRINTF("UE #%d\n", ue_idx);
      INDENTED_PRINTF("PduIdx[%d][%d] = 0x%02x\n", group_idx, ue_idx, msg->dl_tti_request_body.PduIdx[group_idx][ue_idx]);
      depth--;
    }
    depth--;
  }
}

static void dump_ul_tti_beamforming(const nfapi_nr_ul_beamforming_t *pdu, int depth)
{
  INDENTED_PRINTF("TRP Scheme = %d\n", pdu->trp_scheme);
  INDENTED_PRINTF("numPRGs = %d\n", pdu->num_prgs);
  INDENTED_PRINTF("prgSize = %d\n", pdu->prg_size);
  INDENTED_PRINTF("digBFInterface = %d\n", pdu->dig_bf_interface);
  for (int prg = 0; prg < pdu->num_prgs; ++prg) {
    INDENTED_PRINTF("PRG #%d \n", prg);
    depth++;
    for (int dbf_if = 0; dbf_if < pdu->dig_bf_interface; ++dbf_if) {
      INDENTED_PRINTF("digBFInterface #%d \n", dbf_if);
      depth++;
      INDENTED_PRINTF("beamIdx = %d\n", pdu->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx);
      depth--;
    }
    depth--;
  }
}

static char *ul_tti_prach_format_to_str(int prach_format)
{
  switch (prach_format) {
    case 0:
      return "0";
    case 1:
      return "1";
    case 2:
      return "2";
    case 3:
      return "3";
    case 4:
      return "A1";
    case 5:
      return "A2";
    case 6:
      return "A3";
    case 7:
      return "B1";
    case 8:
      return "B4";
    case 9:
      return "C0";
    case 10:
      return "C2";
    case 11:
      return "A1/B1";
    case 12:
      return "A2/B2";
    case 13:
      return "A3/B3";
    default:
      return "Unknown";
  }
}

static void dump_ul_tti_request_PRACH_PDU(const nfapi_nr_prach_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("physCellID = %d\n", pdu->phys_cell_id);
  INDENTED_PRINTF("NumPrachOcas = %d\n", pdu->num_prach_ocas);
  INDENTED_PRINTF("prachFormat = %d (%s)\n", pdu->prach_format, ul_tti_prach_format_to_str(pdu->prach_format));
  INDENTED_PRINTF("numRa = %d\n", pdu->num_ra);
  INDENTED_PRINTF("prachStartSymbol = %d\n", pdu->prach_start_symbol);
  INDENTED_PRINTF("numCs = %d\n", pdu->num_cs);
  dump_ul_tti_beamforming(&pdu->beamforming, depth + 1);
}

static void dump_ul_tti_request_PUSCH_PDU(const nfapi_nr_pusch_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("pduBitmap = %d\n", pdu->pdu_bit_map);
  INDENTED_PRINTF("RNTI = 0x%x\n", pdu->rnti);
  INDENTED_PRINTF("Handle = %d\n", pdu->handle);
  INDENTED_PRINTF("BWPSize = %d\n", pdu->bwp_size);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->bwp_start);
  INDENTED_PRINTF("SubcarrierSpacing = %d\n", pdu->subcarrier_spacing);
  INDENTED_PRINTF("CyclicPrefix = %d\n", pdu->cyclic_prefix);
  INDENTED_PRINTF("targetCodeRate = %d\n", pdu->target_code_rate);
  INDENTED_PRINTF("qamModOrder = %d\n", pdu->qam_mod_order);
  INDENTED_PRINTF("mcsIndex = %d\n", pdu->mcs_index);
  INDENTED_PRINTF("mcsTable = %d\n", pdu->mcs_table);
  INDENTED_PRINTF("TransformPrecoding = %d\n", pdu->transform_precoding);
  INDENTED_PRINTF("dataScramblingId = %d\n", pdu->data_scrambling_id);
  INDENTED_PRINTF("nrOfLayers = %d\n", pdu->nrOfLayers);
  INDENTED_PRINTF("ulDmrsSymbPos = %d\n", pdu->ul_dmrs_symb_pos);
  INDENTED_PRINTF("dmrsConfigType = %d\n", pdu->dmrs_config_type);
  INDENTED_PRINTF("ulDmrsScramblingId = %d\n", pdu->ul_dmrs_scrambling_id);
  INDENTED_PRINTF("puschIdentity = %d\n", pdu->pusch_identity);
  INDENTED_PRINTF("SCID = %d\n", pdu->scid);
  INDENTED_PRINTF("numDmrsCdmGrpsNoData = %d\n", pdu->num_dmrs_cdm_grps_no_data);
  INDENTED_PRINTF("dmrsPorts = %d\n", pdu->dmrs_ports);
  INDENTED_PRINTF("resourceAlloc = %d\n", pdu->resource_alloc);
  INDENTED_PRINTF("rbBitmap = ");
  for (int i = 0; i < 36; ++i) {
    printf("%d ", pdu->rb_bitmap[i]);
  }
  printf("\n");
  INDENTED_PRINTF("rbStart = %d\n", pdu->rb_start);
  INDENTED_PRINTF("rbSize = %d\n", pdu->rb_size);
  INDENTED_PRINTF("VRBtoPRBMapping = %d\n", pdu->vrb_to_prb_mapping);
  INDENTED_PRINTF("FrequencyHopping = %d\n", pdu->frequency_hopping);
  INDENTED_PRINTF("txDirectCurrentLocation = %d\n", pdu->tx_direct_current_location);
  INDENTED_PRINTF("uplinkFrequencyShift7p5khz = %d\n", pdu->uplink_frequency_shift_7p5khz);
  INDENTED_PRINTF("NrOfSymbols = %d\n", pdu->nr_of_symbols);

  INDENTED_PRINTF("puschData (%s)\n", pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA ? "included" : "not included");
  if (pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA) {
    depth++;
    INDENTED_PRINTF("rvIndex = %d\n", pdu->pusch_data.rv_index);
    INDENTED_PRINTF("harqProcessID = %d\n", pdu->pusch_data.harq_process_id);
    INDENTED_PRINTF("newDataIndicator = %d\n", pdu->pusch_data.new_data_indicator);
    INDENTED_PRINTF("TBSize = %d\n", pdu->pusch_data.tb_size);
    INDENTED_PRINTF("numCB = %d\n", pdu->pusch_data.num_cb);
    INDENTED_PRINTF("cbPresentAndPosition = ");
    for (int i = 0; i < (pdu->pusch_data.num_cb + 7) / 8; i++) {
      printf("%d ", pdu->pusch_data.cb_present_and_position[i]);
    }
    printf("\n");
    depth--;
  }

  INDENTED_PRINTF("puschUci (%s)\n", pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI ? "included" : "not included");
  if (pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI) {
    depth++;
    INDENTED_PRINTF("harqAckBitLength = %d\n", pdu->pusch_uci.harq_ack_bit_length);
    INDENTED_PRINTF("csiPart1BitLength = %d\n", pdu->pusch_uci.csi_part1_bit_length);
    INDENTED_PRINTF("csiPart2BitLength = %d\n", pdu->pusch_uci.csi_part2_bit_length);
    INDENTED_PRINTF("AlphaScaling = %d\n", pdu->pusch_uci.alpha_scaling);
    INDENTED_PRINTF("betaOffsetHarqAck = %d\n", pdu->pusch_uci.beta_offset_harq_ack);
    INDENTED_PRINTF("betaOffsetCsi1 = %d\n", pdu->pusch_uci.beta_offset_csi1);
    INDENTED_PRINTF("betaOffsetCsi2 = %d\n", pdu->pusch_uci.beta_offset_csi2);
    depth--;
  }

  INDENTED_PRINTF("puschPtrs (%s)\n", pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS ? "included" : "not included");
  if (pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    depth++;
    INDENTED_PRINTF("lowPaprGroupNumber = %d\n", pdu->pusch_ptrs.num_ptrs_ports);
    for (int i = 0; i < pdu->pusch_ptrs.num_ptrs_ports; ++i) {
      INDENTED_PRINTF("ptrsPort #%d\n", i);
      depth++;
      INDENTED_PRINTF("PTRSPortIndex = %d\n", pdu->pusch_ptrs.ptrs_ports_list[i].ptrs_port_index);
      INDENTED_PRINTF("PTRSDmrsPort = %d\n", pdu->pusch_ptrs.ptrs_ports_list[i].ptrs_dmrs_port);
      INDENTED_PRINTF("PTRSReOffset = %d\n", pdu->pusch_ptrs.ptrs_ports_list[i].ptrs_re_offset);
      depth--;
    }
    INDENTED_PRINTF("PTRSTimeDensity = %d\n", pdu->pusch_ptrs.ptrs_time_density);
    INDENTED_PRINTF("PTRSFreqDensity = %d\n", pdu->pusch_ptrs.ptrs_freq_density);
    INDENTED_PRINTF("ulPTRSPower = %d\n", pdu->pusch_ptrs.ul_ptrs_power);
    depth--;
  }

  INDENTED_PRINTF("dtfsOfdm (%s)\n", pdu->pdu_bit_map & PUSCH_PDU_BITMAP_DFTS_OFDM ? "included" : "not included");
  if (pdu->pdu_bit_map & PUSCH_PDU_BITMAP_DFTS_OFDM) {
    depth++;
    INDENTED_PRINTF("lowPaprGroupNumber = %d\n", pdu->dfts_ofdm.low_papr_group_number);
    INDENTED_PRINTF("lowPaprSequenceNumber = %d\n", pdu->dfts_ofdm.low_papr_sequence_number);
    INDENTED_PRINTF("ulPtrsSampleDensity = %d\n", pdu->dfts_ofdm.ul_ptrs_sample_density);
    INDENTED_PRINTF("ulPtrsTimeDensityTransformPrecoding = %d\n", pdu->dfts_ofdm.ul_ptrs_time_density_transform_precoding);
    depth--;
  }
  dump_ul_tti_beamforming(&pdu->beamforming, depth + 1);
}

static void dump_ul_tti_request_PUCCH_PDU(const nfapi_nr_pucch_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("RNTI = 0x%x\n", pdu->rnti);
  INDENTED_PRINTF("Handle = %d\n", pdu->handle);
  INDENTED_PRINTF("BWPSize = %d\n", pdu->bwp_size);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->bwp_start);
  INDENTED_PRINTF("SubcarrierSpacing = %d\n", pdu->subcarrier_spacing);
  INDENTED_PRINTF("CyclicPrefix = %d\n", pdu->cyclic_prefix);
  INDENTED_PRINTF("FormatType = %d\n", pdu->format_type);
  INDENTED_PRINTF("multiSlotTxIndicator = %d\n", pdu->multi_slot_tx_indicator);
  INDENTED_PRINTF("pi2Bpsk = %d\n", pdu->pi_2bpsk);
  INDENTED_PRINTF("prbStart = %d\n", pdu->prb_start);
  INDENTED_PRINTF("prbSize = %d\n", pdu->prb_size);
  INDENTED_PRINTF("StartSymbolIndex = %d\n", pdu->start_symbol_index);
  INDENTED_PRINTF("NrOfSymbols = %d\n", pdu->nr_of_symbols);
  INDENTED_PRINTF("freqHopFlag = %d\n", pdu->freq_hop_flag);
  INDENTED_PRINTF("secondHopPRB = %d\n", pdu->second_hop_prb);
  INDENTED_PRINTF("groupHopFlag = %d\n", pdu->group_hop_flag);
  INDENTED_PRINTF("sequenceHopFlag = %d\n", pdu->sequence_hop_flag);
  INDENTED_PRINTF("hoppingId = %d\n", pdu->hopping_id);
  INDENTED_PRINTF("InitialCyclicShift = %d\n", pdu->initial_cyclic_shift);
  INDENTED_PRINTF("dataScramblingId = %d\n", pdu->data_scrambling_id);
  INDENTED_PRINTF("TimeDomainOccIdx = %d\n", pdu->time_domain_occ_idx);
  INDENTED_PRINTF("PreDftOccIdx = %d\n", pdu->pre_dft_occ_idx);
  INDENTED_PRINTF("PreDftOccLen = %d\n", pdu->pre_dft_occ_len);
  INDENTED_PRINTF("AddDmrsFlag = %d\n", pdu->add_dmrs_flag);
  INDENTED_PRINTF("DmrsScramblingId = %d\n", pdu->dmrs_scrambling_id);
  INDENTED_PRINTF("DMRScyclicshift = %d\n", pdu->dmrs_cyclic_shift);
  INDENTED_PRINTF("SRFlag = %d\n", pdu->sr_flag);
  INDENTED_PRINTF("BitLenHarq = %d\n", pdu->bit_len_harq);
  INDENTED_PRINTF("BitLenCsiPart1 = %d\n", pdu->bit_len_csi_part1);
  INDENTED_PRINTF("BitLenCsiPart2 = %d\n", pdu->bit_len_csi_part2);
  dump_ul_tti_beamforming(&pdu->beamforming, depth + 1);
}

static void dump_ul_tti_request_srs_parameters(const nfapi_v4_srs_parameters_t *pdu, const uint8_t num_symbols, uint8_t depth)
{
  INDENTED_PRINTF("srsBandwidthSize = %d\n", pdu->srs_bandwidth_size);
  for (int symbol_idx = 0; symbol_idx < num_symbols; ++symbol_idx) {
    const nfapi_v4_srs_parameters_symbols_t *symbol = &pdu->symbol_list[symbol_idx];
    INDENTED_PRINTF("Symbol #%d\n", symbol_idx);
    depth++;
    INDENTED_PRINTF("srsBandwidthStart = %d\n", symbol->srs_bandwidth_start);
    INDENTED_PRINTF("sequenceGroup = %d\n", symbol->sequence_group);
    INDENTED_PRINTF("sequenceNumber = %d\n", symbol->sequence_number);
    depth--;
  }
  const uint8_t nUsage = __builtin_popcount(pdu->usage);
  INDENTED_PRINTF("Usage = %d (nUsage = %d)\n", pdu->usage, nUsage);
  depth++;
  for (int idx = 0; idx < nUsage; ++idx) {
    INDENTED_PRINTF("ReportType[%d] = %d\n", idx, pdu->report_type[idx]);
  }
  depth--;
  INDENTED_PRINTF("singular Value Representation = %d\n", pdu->singular_Value_representation);
  INDENTED_PRINTF("iq Representation = %d\n", pdu->iq_representation);
  INDENTED_PRINTF("prgSize = %d\n", pdu->prg_size);
  INDENTED_PRINTF("numTotalUeAntennas = %d\n", pdu->num_total_ue_antennas);
  INDENTED_PRINTF("ueAntennasInThisSrsResourceSet = %d\n", pdu->ue_antennas_in_this_srs_resource_set);
  INDENTED_PRINTF("sampledUeAntennas = %d\n", pdu->sampled_ue_antennas);
  INDENTED_PRINTF("reportScope = %d\n", pdu->report_scope);
  INDENTED_PRINTF("NumULSpatialStreamsPorts = %d\n", pdu->num_ul_spatial_streams_ports);
  depth++;
  for (int idx = 0; idx < pdu->num_ul_spatial_streams_ports; ++idx) {
    INDENTED_PRINTF("UlSpatialStreamPorts[%d] = %d\n", idx, pdu->Ul_spatial_stream_ports[idx]);
  }
}

static void dump_ul_tti_request_SRS_PDU(const nfapi_nr_srs_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("RNTI = 0x%x\n", pdu->rnti);
  INDENTED_PRINTF("Handle = %d\n", pdu->handle);
  INDENTED_PRINTF("BWPSize = %d\n", pdu->bwp_size);
  INDENTED_PRINTF("BWPStart = %d\n", pdu->bwp_start);
  INDENTED_PRINTF("SubcarrierSpacing = %d\n", pdu->subcarrier_spacing);
  INDENTED_PRINTF("CyclicPrefix = %d\n", pdu->cyclic_prefix);
  INDENTED_PRINTF("numAntPorts = %d\n", pdu->num_ant_ports);
  INDENTED_PRINTF("numSymbols = %d\n", pdu->num_symbols);
  INDENTED_PRINTF("numRepetitions = %d\n", pdu->num_repetitions);
  INDENTED_PRINTF("timeStartPosition = %d\n", pdu->time_start_position);
  INDENTED_PRINTF("configIndex = %d\n", pdu->config_index);
  INDENTED_PRINTF("sequenceId = %d\n", pdu->sequence_id);
  INDENTED_PRINTF("bandwidthIndex = %d\n", pdu->bandwidth_index);
  INDENTED_PRINTF("combSize = %d\n", pdu->comb_size);
  INDENTED_PRINTF("combOffset = %d\n", pdu->comb_offset);
  INDENTED_PRINTF("cyclicShift = %d\n", pdu->cyclic_shift);
  INDENTED_PRINTF("frequencyPosition = %d\n", pdu->frequency_position);
  INDENTED_PRINTF("frequencyShift = %d\n", pdu->frequency_shift);
  INDENTED_PRINTF("frequencyHopping = %d\n", pdu->frequency_hopping);
  INDENTED_PRINTF("groupOrSequenceHopping = %d\n", pdu->group_or_sequence_hopping);
  INDENTED_PRINTF("resourceType = %d\n", pdu->resource_type);
  INDENTED_PRINTF("Tsrs = %d\n", pdu->t_srs);
  INDENTED_PRINTF("Toffset = %d\n", pdu->t_offset);
  dump_ul_tti_beamforming(&pdu->beamforming, depth + 1);
  dump_ul_tti_request_srs_parameters(&pdu->srs_parameters_v4, 1 << pdu->num_symbols, depth + 1);
}

static char *ul_tti_pdu_type_to_str(uint16_t pdu_type)
{
  switch (pdu_type) {
    case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE:
      return "NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE";
    case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
      return "NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE";
    case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
      return "NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE";
    case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
      return "NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE";
    default:
      return "Unknown";
      break;
  }
}

void dump_ul_tti_request(const nfapi_nr_ul_tti_request_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->SFN);
  INDENTED_PRINTF("Slot = %d\n", msg->Slot);
  INDENTED_PRINTF("nPDUs = %d\n", msg->n_pdus);
  INDENTED_PRINTF("RachPresent = %d\n", msg->rach_present);
  INDENTED_PRINTF("nULSCH = %d\n", msg->n_ulsch);
  INDENTED_PRINTF("nULCCH = %d\n", msg->n_ulcch);
  INDENTED_PRINTF("nGroup = %d\n", msg->n_group);
  for (int i = 0; i < msg->n_pdus; ++i) {
    const nfapi_nr_ul_tti_request_number_of_pdus_t *pdu = &msg->pdus_list[i];
    depth++;
    INDENTED_PRINTF("PDU #%d\n", i);
    INDENTED_PRINTF("PDUType = 0x%02x (%s)\n", pdu->pdu_type, ul_tti_pdu_type_to_str(pdu->pdu_type));
    INDENTED_PRINTF("PDUSize = 0x%02x\n", pdu->pdu_size);
    /* call to dump_ul_tti_request_<PDU_Type>_pdu */
    switch (pdu->pdu_type) {
      case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE:
        dump_ul_tti_request_PRACH_PDU(&pdu->prach_pdu, depth + 1);
        break;
      case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE:
        dump_ul_tti_request_PUSCH_PDU(&pdu->pusch_pdu, depth + 1);
        break;
      case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE:
        dump_ul_tti_request_PUCCH_PDU(&pdu->pucch_pdu, depth + 1);
        break;
      case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE:
        dump_ul_tti_request_SRS_PDU(&pdu->srs_pdu, depth + 1);
        break;
      default:
        INDENTED_PRINTF("Unknown PDU type 0x%02x\n", pdu->pdu_type);
        break;
    }
    depth--;
  }
  for (int group_idx = 0; group_idx < msg->n_group; group_idx++) {
    const nfapi_nr_ul_tti_request_number_of_groups_t *grp = &msg->groups_list[group_idx];
    depth++;
    INDENTED_PRINTF("Group #%d\n", group_idx);
    INDENTED_PRINTF("nUe[%d] = 0x%02x\n", group_idx, grp->n_ue);
    for (int ue_idx = 0; ue_idx < grp->n_ue; ue_idx++) {
      depth++;
      INDENTED_PRINTF("UE #%d\n", ue_idx);
      INDENTED_PRINTF("PduIdx[%d][%d] = 0x%02x\n", group_idx, ue_idx, grp->ue_list[ue_idx].pdu_idx);
      depth--;
    }
    depth--;
  }
}

void dump_slot_indication(const nfapi_nr_slot_indication_scf_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);
}

void dump_ul_dci_request(const nfapi_nr_ul_dci_request_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->SFN);
  INDENTED_PRINTF("Slot = %d\n", msg->Slot);

  INDENTED_PRINTF("numPDUs = %d\n", msg->numPdus);
  for (int i = 0; i < msg->numPdus; i++) {
    depth++;
    INDENTED_PRINTF("PDU #%d\n", i);
    const nfapi_nr_ul_dci_request_pdus_t *pdu = &msg->ul_dci_pdu_list[i];
    INDENTED_PRINTF("PDUType = 0x%02x\n", pdu->PDUType);
    INDENTED_PRINTF("PDUSize = 0x%02x\n", pdu->PDUSize);
    /* call to dump_dl_tti_request_pdcch_pdu */
    dump_dl_tti_request_PDCCH_PDU(&pdu->pdcch_pdu.pdcch_pdu_rel15, depth + 1);
    depth--;
  }
}

static void dump_tx_data_request_tlv(const nfapi_nr_tx_data_request_tlv_t *tlv, int depth)
{
  INDENTED_PRINTF("Tag = %d\n", tlv->tag);
  INDENTED_PRINTF("Length = %d\n", tlv->length);
  INDENTED_PRINTF("Value = ");
  switch (tlv->tag) {
    case 0:
      for (int i = 0; i < tlv->length; i++) {
        printf("0x%02x ", ((uint8_t*)tlv->value.direct)[i]);
      }
      break;
    case 1:
      printf("%p", tlv->value.ptr);
      break;
    default:
      printf( " Unknown tag value %d",tlv->tag);
      break;
  }
  printf("\n");
}

void dump_tx_data_request(const nfapi_nr_tx_data_request_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->SFN);
  INDENTED_PRINTF("Slot = %d\n", msg->Slot);
  INDENTED_PRINTF("Number of PDUs = %d\n", msg->Number_of_PDUs);
  depth++;
  for (int i = 0; i < msg->Number_of_PDUs; i++) {
    INDENTED_PRINTF("PDU #%d\n", i);
    const nfapi_nr_pdu_t *pdu = &msg->pdu_list[i];
    INDENTED_PRINTF("PDU Length = 0x%02x\n", pdu->PDU_length);
    INDENTED_PRINTF("PDU Index = 0x%02x\n", pdu->PDU_index);
    INDENTED_PRINTF("numTLV = 0x%02x\n", pdu->num_TLV);
    for (int tlv_idx = 0; tlv_idx < pdu->num_TLV; tlv_idx++) {
      const nfapi_nr_tx_data_request_tlv_t *tlv = &pdu->TLVs[tlv_idx];
    /* call to dump_tx_data_request_tlv */
      dump_tx_data_request_tlv(tlv, depth + 1);
    }
  }
}

void dump_rx_data_indication(const nfapi_nr_rx_data_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);
  INDENTED_PRINTF("Number of PDUs = %d\n", msg->number_of_pdus);
  for (int pdu_idx = 0; pdu_idx < msg->number_of_pdus; pdu_idx++) {
    depth++;
    INDENTED_PRINTF("PDU #%d\n", pdu_idx);
    const nfapi_nr_rx_data_pdu_t *pdu = &msg->pdu_list[pdu_idx];
    INDENTED_PRINTF("Handle = 0x%02x\n", pdu->handle);
    INDENTED_PRINTF("RNTI = 0x%02x\n", pdu->rnti);
    INDENTED_PRINTF("HarqID = 0x%02x\n", pdu->harq_id);
    INDENTED_PRINTF("PDU Length = 0x%02x\n", pdu->pdu_length);
    INDENTED_PRINTF("UL_CQI = 0x%02x\n", pdu->ul_cqi);
    INDENTED_PRINTF("Timing advance = 0x%02x\n", pdu->timing_advance);
    INDENTED_PRINTF("RSSI = 0x%02x\n", pdu->rssi);
    INDENTED_PRINTF("PDU =");
    for (int i = 0; i < pdu->pdu_length; i++) {
      printf("%02x ", pdu->pdu[i]);
    }
    printf("\n");
    depth--;
  }
}

void dump_crc_indication(const nfapi_nr_crc_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);
  INDENTED_PRINTF("NumCRCs = %d\n", msg->number_crcs);
  for (int i = 0; i < msg->number_crcs; i++) {
    depth++;
    INDENTED_PRINTF("CRC #%d\n", i);
    const nfapi_nr_crc_t *crc = &msg->crc_list[i];
    INDENTED_PRINTF("Handle = 0x%02x\n", crc->handle);
    INDENTED_PRINTF("RNTI = 0x%02x\n", crc->rnti);
    INDENTED_PRINTF("HarqID = 0x%02x\n", crc->harq_id);
    INDENTED_PRINTF("TbCrcStatus = 0x%02x\n", crc->tb_crc_status);
    INDENTED_PRINTF("NumCb = 0x%02x\n", crc->num_cb);
    const uint16_t cb_len = nr_bits_to_bytes(crc->num_cb); // length is ceil(NumCb/8)
    INDENTED_PRINTF("CbCrcStatus =\n");
    for (int j = 0; j < cb_len; j++) {
      printf("%02x ", crc->cb_crc_status[j]);
    }
    printf("\n");
    INDENTED_PRINTF("UL_CQI = 0x%02x\n", crc->ul_cqi);
    INDENTED_PRINTF("Timing advance = 0x%02x\n", crc->timing_advance);
    INDENTED_PRINTF("RSSI = 0x%02x\n", crc->rssi);
    depth--;
  }
}

static void dump_harq_2_3_4(const nfapi_nr_harq_pdu_2_3_4_t *harq, int depth)
{
  INDENTED_PRINTF("HARQ CRC = %01x\n", harq->harq_crc);
  INDENTED_PRINTF("HARQ Bit Length = %d\n", harq->harq_bit_len);
  INDENTED_PRINTF("HARQ Payload = ");
  for (int i = 0; i < nr_bits_to_bytes(harq->harq_bit_len); i++) {
    printf("%x ", harq->harq_payload[i]);
  }
  printf("\n");
}

static void dump_csi_part_1(const nfapi_nr_csi_part1_pdu_t *csi, int depth)
{
  INDENTED_PRINTF("CSI Part 1 CRC = %01x\n", csi->csi_part1_crc);
  INDENTED_PRINTF("CSI Part 1 Bit Length = %d\n", csi->csi_part1_bit_len);
  INDENTED_PRINTF("CSI Part 1 Payload = ");
  for (int i = 0; i < nr_bits_to_bytes(csi->csi_part1_bit_len); i++) {
    printf("%x ", csi->csi_part1_payload[i]);
  }
  printf("\n");
}

static void dump_csi_part_2(const nfapi_nr_csi_part2_pdu_t *csi, int depth)
{
  INDENTED_PRINTF("CSI Part 1 CRC = %01x\n", csi->csi_part2_crc);
  INDENTED_PRINTF("CSI Part 1 Bit Length = %d\n", csi->csi_part2_bit_len);
  INDENTED_PRINTF("CSI Part 1 Payload = ");
  for (int i = 0; i < nr_bits_to_bytes(csi->csi_part2_bit_len); i++) {
    printf("%x ", csi->csi_part2_payload[i]);
  }
  printf("\n");
}

static void dump_uci_PUSCH_PDU(const nfapi_nr_uci_pusch_pdu_t *pdu, int depth)
{
  INDENTED_PRINTF("PDU Bitmap = 0x%01x\n", pdu->pduBitmap);
  INDENTED_PRINTF("Handle = 0x%04x\n", pdu->handle);
  INDENTED_PRINTF("RNTI = 0x%02x\n", pdu->rnti);
  INDENTED_PRINTF("UL_CQI = 0x%02x\n", pdu->ul_cqi);
  INDENTED_PRINTF("Timing Advance = 0x%02x\n", pdu->timing_advance);
  INDENTED_PRINTF("RSSI = 0x%02x\n", pdu->rssi);
  // HARQ payload, CSI Part 1 and 2 are conditionally allocated
  if ((pdu->pduBitmap >> 1) & 0x01) {
    INDENTED_PRINTF("HARQ Information\n");
    dump_harq_2_3_4(&pdu->harq, depth + 1);
  }
  if ((pdu->pduBitmap >> 2) & 0x01) {
    INDENTED_PRINTF("CSI Part 1\n");
    dump_csi_part_1(&pdu->csi_part1, depth + 1);
  }
  if ((pdu->pduBitmap >> 3) & 0x01) {
    INDENTED_PRINTF("CSI Part 1\n");
    dump_csi_part_2(&pdu->csi_part2, depth + 1);
  }
}

static void dump_sr_0_1(const nfapi_nr_sr_pdu_0_1_t *sr, int depth)
{
  INDENTED_PRINTF("SR Indication = 0x%02x\n", sr->sr_indication);
  INDENTED_PRINTF("SR Confidence Level = 0x%02x\n", sr->sr_confidence_level);
}

static void dump_harq_0_1(const nfapi_nr_harq_pdu_0_1_t *harq, int depth)
{
  INDENTED_PRINTF("Num HARQ = 0x%02x\n", harq->num_harq);
  INDENTED_PRINTF("HARQ Confidence Level = 0x%02x\n", harq->harq_confidence_level);
  depth++;
  for (int i = 0; i < harq->num_harq; i++) {
    INDENTED_PRINTF("HARQ Value[%d] : 0x%02x \n", i, harq->harq_list[i].harq_value);
  }
}

static void dump_uci_PUCCH_0_1(const nfapi_nr_uci_pucch_pdu_format_0_1_t *pdu, int depth)
{
  INDENTED_PRINTF("PDU Bitmap = 0x%01x\n", pdu->pduBitmap);
  INDENTED_PRINTF("Handle = 0x%04x\n", pdu->handle);
  INDENTED_PRINTF("RNTI = 0x%02x\n", pdu->rnti);
  INDENTED_PRINTF("PUCCH Format = 0x%02x\n", pdu->pucch_format);
  INDENTED_PRINTF("UL_CQI = 0x%02x\n", pdu->ul_cqi);
  INDENTED_PRINTF("Timing Advance = 0x%02x\n", pdu->timing_advance);
  INDENTED_PRINTF("RSSI = 0x%02x\n", pdu->rssi);
  if ((pdu->pduBitmap) & 0x01) {
    INDENTED_PRINTF("SR Information\n");
    dump_sr_0_1(&pdu->sr, depth + 1);
  }
  if ((pdu->pduBitmap >> 1) & 0x01) {
    INDENTED_PRINTF("HARQ Information\n");
    dump_harq_0_1(&pdu->harq, depth + 1);
  }
}

static void dump_sr_2_3_4(const nfapi_nr_sr_pdu_2_3_4_t *sr, int depth)
{
  INDENTED_PRINTF("SR Bit Length = %d\n", sr->sr_bit_len);
  INDENTED_PRINTF("SR Payload ");
  for (int i = 0; i < nr_bits_to_bytes(sr->sr_bit_len); i++) {
    printf("%x ", sr->sr_payload[i]);
  }
  printf("\n");
}

static void dump_uci_PUCCH_2_3_4(const nfapi_nr_uci_pucch_pdu_format_2_3_4_t *pdu, int depth)
{
  INDENTED_PRINTF("PDU Bitmap = 0x%01x\n", pdu->pduBitmap);
  INDENTED_PRINTF("Handle = 0x%04x\n", pdu->handle);
  INDENTED_PRINTF("RNTI = 0x%02x\n", pdu->rnti);
  INDENTED_PRINTF("PUCCH Format = 0x%02x\n", pdu->pucch_format);
  INDENTED_PRINTF("UL_CQI = 0x%02x\n", pdu->ul_cqi);
  INDENTED_PRINTF("Timing Advance = 0x%02x\n", pdu->timing_advance);
  INDENTED_PRINTF("RSSI = 0x%02x\n", pdu->rssi);
  // SR
  if (pdu->pduBitmap & 0x01) {
    dump_sr_2_3_4(&pdu->sr, depth + 1);
  }
  // HARQ
  if ((pdu->pduBitmap >> 1) & 0x01) {
    dump_harq_2_3_4(&pdu->harq, depth + 1);
  }
  // CSI Part 1
  if ((pdu->pduBitmap >> 2) & 0x01) {
    dump_csi_part_1(&pdu->csi_part1, depth + 1);
  }
  // CSI Part 2
  if ((pdu->pduBitmap >> 3) & 0x01) {
    dump_csi_part_2(&pdu->csi_part2, depth + 1);
  }
}

void dump_uci_indication(const nfapi_nr_uci_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);

  INDENTED_PRINTF("NumUCIS = %d\n", msg->num_ucis);
  for (int i = 0; i < msg->num_ucis; i++) {
    depth++;
    INDENTED_PRINTF("UCI #%d\n", i);
    const nfapi_nr_uci_t *uci = &msg->uci_list[i];
    INDENTED_PRINTF("PDUType = 0x%02x\n", uci->pdu_type);
    INDENTED_PRINTF("PDUSize = 0x%02x\n", uci->pdu_size);
    /* call to dump_uci_indication_<PDU_Type>_pdu */
    switch (uci->pdu_type) {
      case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
        dump_uci_PUSCH_PDU(&uci->pusch_pdu, depth + 1);
        break;
      case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE:
        dump_uci_PUCCH_0_1(&uci->pucch_pdu_format_0_1, depth + 1);
        break;
      case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE:
        dump_uci_PUCCH_2_3_4(&uci->pucch_pdu_format_2_3_4, depth + 1);
        break;
      default:
        INDENTED_PRINTF("Unknown PDU type 0x%02x\n", uci->pdu_type);
        break;
    }
    depth--;
  }
}

static void dump_srs_report_tlv(const nfapi_srs_report_tlv_t* tlv, int depth)
{
  depth++;
  INDENTED_PRINTF("TAG = 0x%02x\n", tlv->tag);
  INDENTED_PRINTF("LENGTH = 0x%02x\n", tlv->length);
  INDENTED_PRINTF("VALUE = ");
  for (int i = 0; i < ((tlv->length + 3) / 4); ++i) {
     printf("0x%04x ", tlv->value[i]);
  }
  printf("\n");
}

void dump_srs_indication(const nfapi_nr_srs_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);

  INDENTED_PRINTF("controlLength = %d\n", msg->control_length);
  INDENTED_PRINTF("Number of PDUs = %d\n", msg->number_of_pdus);
  for (int i = 0; i < msg->number_of_pdus; i++) {
    depth++;
    INDENTED_PRINTF("PDU #%d\n", i);
    const nfapi_nr_srs_indication_pdu_t *pdu = &msg->pdu_list[i];
    INDENTED_PRINTF("Handle = 0x%02x\n", pdu->handle);
    INDENTED_PRINTF("RNTI = 0x%02x\n", pdu->rnti);
    INDENTED_PRINTF("Timing advance offset = 0x%02x\n", pdu->timing_advance_offset);
    INDENTED_PRINTF("Timing advance offset in nanoseconds = 0x%02x\n", pdu->timing_advance_offset_nsec);
    INDENTED_PRINTF("SRS usage = 0x%02x\n", pdu->srs_usage);
    INDENTED_PRINTF("Report Type = 0x%02x\n", pdu->report_type);
    dump_srs_report_tlv(&pdu->report_tlv, depth);
    depth--;
  }
}

void dump_srs_toa_vendor_ext_indication(const nfapi_nr_srs_toa_vendor_ext_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);
  INDENTED_PRINTF("RNTI = 0x%02x\n", msg->rnti);
  INDENTED_PRINTF("Number of TA_NSEC = %d\n", msg->num_ta);
  depth++;
  for (int i = 0; i < msg->num_ta; i++) {
    INDENTED_PRINTF("Timing advance offset in nanoseconds [%d] = 0x%02x\n", i, msg->ta_offset_nsec[i]);
  }
}

void dump_rach_indication(const nfapi_nr_rach_indication_t *msg)
{
  int depth = 0;
  dump_p7_message_header(&msg->header, depth);
  depth++;
  INDENTED_PRINTF("SFN = %d\n", msg->sfn);
  INDENTED_PRINTF("Slot = %d\n", msg->slot);

  INDENTED_PRINTF("Number of PDUs = %d\n", msg->number_of_pdus);
  for (int i = 0; i < msg->number_of_pdus; i++) {
    depth++;
    INDENTED_PRINTF("PRACH PDU #%d\n", i);
    const nfapi_nr_prach_indication_pdu_t *pdu = &msg->pdu_list[i];
    INDENTED_PRINTF("physCellID = 0x%02x\n", pdu->phy_cell_id);
    INDENTED_PRINTF("SymbolIndex = 0x%02x\n", pdu->symbol_index);
    INDENTED_PRINTF("SlotIndex = 0x%02x\n", pdu->slot_index);
    INDENTED_PRINTF("FreqIndex = 0x%02x\n", pdu->freq_index);
    INDENTED_PRINTF("avgRssi = 0x%02x\n", pdu->avg_rssi);
    INDENTED_PRINTF("avgSnr = 0x%02x\n", pdu->avg_snr);
    INDENTED_PRINTF("numPreamble = 0x%02x\n", pdu->num_preamble);
    for (int j = 0; j < pdu->num_preamble; j++) {
      depth++;
      INDENTED_PRINTF("Preamble #%d\n", j);
      const nfapi_nr_prach_indication_preamble_t *preamble = &pdu->preamble_list[j];
      INDENTED_PRINTF("preambleIndex = 0x%02x\n", preamble->preamble_index);
      INDENTED_PRINTF("Timing advance = 0x%02x\n", preamble->timing_advance);
      INDENTED_PRINTF("PreamblePwr = 0x%02x\n", preamble->preamble_pwr);
      depth--;
    }
    depth--;
  }
}
