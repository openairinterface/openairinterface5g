/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "dci_payload_utils.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"
#include "nr_fapi_common_util_test.h"

static void fill_dl_tti_request_beamforming(nfapi_nr_tx_precoding_and_beamforming_t *precodingAndBeamforming)
{
  precodingAndBeamforming->num_prgs = rand16_range(1, NFAPI_MAX_NUM_PRGS);
  precodingAndBeamforming->prg_size = rand16_range(1, 275);
  precodingAndBeamforming->dig_bf_interfaces = rand8_range(0,NFAPI_MAX_NUM_BG_IF);
  for (int prg = 0; prg < precodingAndBeamforming->num_prgs; ++prg) {
    precodingAndBeamforming->prgs_list[prg].pm_idx = rand16();
    for (int dbf_if = 0; dbf_if < precodingAndBeamforming->dig_bf_interfaces; ++dbf_if) {
      precodingAndBeamforming->prgs_list[prg].dig_bf_interface_list[dbf_if].beam_idx = rand16();
    }
  }
}

static void fill_dl_tti_request_pdcch_pdu(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdu)
{
  pdu->BWPSize = rand16();
  pdu->BWPStart = rand16();
  pdu->SubcarrierSpacing = rand8();
  pdu->CyclicPrefix = rand8();
  pdu->StartSymbolIndex = rand8();
  pdu->DurationSymbols = rand8();
  for (int fdr_idx = 0; fdr_idx < 6; ++fdr_idx) {
    pdu->FreqDomainResource[fdr_idx] = rand8();
  }
  pdu->CceRegMappingType = rand8();
  pdu->RegBundleSize = rand8();
  pdu->InterleaverSize = rand8();
  pdu->CoreSetType = rand8();
  pdu->ShiftIndex = rand8();
  pdu->precoderGranularity = rand8();
  pdu->numDlDci = rand8_range(1, MAX_DCI_CORESET);
  for (int dl_dci = 0; dl_dci < pdu->numDlDci; ++dl_dci) {
    pdu->dci_pdu[dl_dci].RNTI = rand16();
    pdu->dci_pdu[dl_dci].ScramblingId = rand16();
    pdu->dci_pdu[dl_dci].ScramblingRNTI = rand16();
    pdu->dci_pdu[dl_dci].CceIndex = rand8();
    pdu->dci_pdu[dl_dci].AggregationLevel = rand8();
    fill_dl_tti_request_beamforming(&pdu->dci_pdu[dl_dci].precodingAndBeamforming);
    pdu->dci_pdu[dl_dci].beta_PDCCH_1_0 = rand8();
    pdu->dci_pdu[dl_dci].powerControlOffsetSS = rand8();
    pdu->dci_pdu[dl_dci].PayloadSizeBits = rand16_range(0, DCI_PAYLOAD_BYTE_LEN * 8);
    generate_payload(pdu->dci_pdu[dl_dci].PayloadSizeBits, pdu->dci_pdu[dl_dci].Payload);
  }
  pdu->param_v4.numSpatialStreams = get_num_spatial_streams();
  for (int n = 0; n < pdu->param_v4.numSpatialStreams; n++) {
    pdu->param_v4.dci_spatialStreamIndices[n].dci_index = rand16();
    pdu->param_v4.dci_spatialStreamIndices[n].spatial_stream_index = rand16_range(0, pdu->param_v4.numSpatialStreams - 1);
  }
}

static void fill_dl_tti_request_pdsch_pdu(nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdu)
{
  pdu->pduBitmap = rand16_range(0, 2);
  pdu->rnti = rand16();
  pdu->pduIndex = rand16();
  pdu->BWPSize = rand16_range(1, 275);
  pdu->BWPStart = rand16_range(0, 274);
  pdu->SubcarrierSpacing = rand8_range(0, 4);
  pdu->CyclicPrefix = rand8_range(0, 1);
  pdu->NrOfCodewords = rand8_range(1, 2);
  for (int cw = 0; cw < pdu->NrOfCodewords; ++cw) {
    pdu->targetCodeRate[cw] = rand16();
    pdu->qamModOrder[cw] = rand8();
    pdu->mcsIndex[cw] = rand8();
    pdu->mcsTable[cw] = rand8_range(0, 2);
    pdu->rvIndex[cw] = rand8_range(0, 3);
    pdu->TBSize[cw] = rand16();
  }
  pdu->dataScramblingId = rand16();
  pdu->nrOfLayers = rand8_range(1, 8);
  pdu->transmissionScheme = rand8_range(0, 8);
  pdu->refPoint = rand8_range(0, 1);
  // dlDmrsSymbPos is a bitmap occupying the 14 LSB
  pdu->dlDmrsSymbPos = rand16_range(0, 0x3FFF);
  pdu->dmrsConfigType = rand8_range(0, 1);
  pdu->dlDmrsScramblingId = rand16();
  pdu->SCID = rand8_range(0, 1);
  pdu->numDmrsCdmGrpsNoData = rand8_range(1, 3);
  // dmrsPorts is a bitmap occupying the 11 LSB
  pdu->dmrsPorts = rand16_range(0, 0x7FF);
  pdu->resourceAlloc = rand8_range(0, 1);
  for (int i = 0; i < 36; ++i) {
    pdu->rbBitmap[i] = rand8();
  }
  pdu->rbStart = rand16_range(0, 274);
  pdu->rbSize = rand16_range(1, 275);
  pdu->VRBtoPRBMapping = rand8_range(0, 2);
  pdu->StartSymbolIndex = rand8_range(0, 13);
  pdu->NrOfSymbols = rand8_range(1, 14);

  if (pdu->pduBitmap & 0b1) {
    // PTRSPortIndex is a bitmap occupying the 6 LSB
    pdu->PTRSPortIndex = rand8_range(0, 0x3F);
    pdu->PTRSTimeDensity = rand8_range(0, 2);
    pdu->PTRSFreqDensity = rand8_range(0, 1);
    pdu->PTRSReOffset = rand8_range(0, 3);
    pdu->nEpreRatioOfPDSCHToPTRS = rand8_range(0, 3);
  }
  fill_dl_tti_request_beamforming(&pdu->precodingAndBeamforming);
  pdu->powerControlOffset = rand8_range(0, 23);
  pdu->powerControlOffsetSS = rand8_range(0, 3);
  // Check pduBitMap bit 1 to add or not CBG parameters
  if (pdu->pduBitmap & 0b10) {
    pdu->isLastCbPresent = rand8_range(0, 1);
    pdu->isInlineTbCrc = rand8_range(0, 1);
    pdu->dlTbCrc = rand32();
  }
  pdu->maintenance_parms_v3.ldpcBaseGraph = rand8();
  pdu->maintenance_parms_v3.tbSizeLbrmBytes = rand32();
  pdu->param_v4.numberCodewords = rand8_range(0, MAX_NUM_CODEWORDS);
  for (int c = 0; c < pdu->param_v4.numberCodewords; c++) {
    pdu->param_v4.spatialStreamsCw[c].numSpatialStreamIndices = get_num_spatial_streams();
    fill_spatial_streams(pdu->param_v4.spatialStreamsCw[c].numSpatialStreamIndices,
                         pdu->param_v4.spatialStreamsCw[c].spatialStreamIndices);
  }
}

static void fill_dl_tti_request_csi_rs_pdu(nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *pdu)
{
  pdu->bwp_size = rand16_range(1, 275);
  pdu->bwp_start = rand16_range(0, 274);
  pdu->subcarrier_spacing = rand8_range(0, 4);
  pdu->cyclic_prefix = rand8_range(0, 1);
  pdu->start_rb = rand16_range(0, 274);
  pdu->nr_of_rbs = rand16_range(0, pdu->bwp_size);
  pdu->csi_type = rand8_range(0, 2);
  pdu->row = rand8_range(1, 18);
  // freq_domain is a bitmap up to 12 LSB, depending on row value [TS38.211, sec 7.4.1.5.3]
  // row = 1 -> b0 to b3
  // row = 2 -> b0 to b11
  // row = 4 -> b0 to b2
  // other rows -> b0 to b5
  switch (pdu->row) {
    case 1: // 4 bits
      pdu->freq_domain = rand16_range(0, 0xF);
      break;
    case 2: // 12 bits
      pdu->freq_domain = rand16_range(0, 0xFFF);
      break;
    case 4: // 3 bits
      pdu->freq_domain = rand16_range(0, 7);
      break;
    default: // 6 bits
      pdu->freq_domain = rand16_range(0, 0x3F);
      break;
  }
  pdu->symb_l0 = rand8_range(0, 13);
  pdu->symb_l1 = rand8_range(2, 12);
  pdu->cdm_type = rand8_range(0, 3);
  pdu->scramb_id = rand16_range(0, 1023);
  pdu->power_control_offset = rand8_range(0, 23);
  pdu->power_control_offset_ss = rand8_range(0, 3);
  fill_dl_tti_request_beamforming(&pdu->precodingAndBeamforming);
  pdu->param_v4.numSpatialStreamIndices = get_num_spatial_streams();
  for (int n = 0; n < pdu->param_v4.numSpatialStreamIndices; n++)
    pdu->param_v4.spatialStreamIndices[n] = rand8_range(0, pdu->param_v4.numSpatialStreamIndices - 1);
}

static void fill_dl_tti_request_ssb_pdu(nfapi_nr_dl_tti_ssb_pdu_rel15_t *pdu)
{
  pdu->PhysCellId = rand16_range(0, 1007);
  pdu->BetaPss = rand8_range(0, 1);
  pdu->SsbBlockIndex = rand8_range(0, 63);
  pdu->SsbSubcarrierOffset = rand8_range(0, 31);
  pdu->ssbOffsetPointA = rand16_range(0, 2199);
  pdu->bchPayloadFlag = rand8_range(0, 2);
  pdu->bchPayload = rand24();
  fill_dl_tti_request_beamforming(&pdu->precoding_and_beamforming);
  pdu->param_v4.spatialStreamIndexPresent = rand8(0, 1);
  pdu->param_v4.spatialStreamIndex = rand16_range(0, MAX_NUM_SPATIAL_STREAMS);
}

static void fill_dl_tti_request_prs_pdu(nfapi_nr_dl_tti_prs_pdu_rel16_t *pdu)
{
  const uint8_t comb_sizes[] = {2, 4, 6, 12};
  const uint8_t num_symbols[] = {2, 4, 6, 12};
  pdu->subcarrierSpacing = rand8_range(0, 4);
  pdu->cyclicPrefix = rand8_range(0, 1);
  pdu->nIdPrs = rand16_range(0, 4095);
  pdu->pduIndex = rand16();
  pdu->combSize = comb_sizes[rand8_range(0, 3)];
  pdu->combOffset = rand8_range(0, pdu->combSize - 1);
  pdu->numSymbols = num_symbols[rand8_range(0, 3)];
  pdu->firstSymbol = rand8_range(0, 12);
  pdu->numRbs = 4 * rand16_range(6, 68); // 24 -> 272, multiples of 4
  pdu->startRb = rand16_range(0, 2176);
  pdu->symbolPuncturingBitmap = rand16_range(0, 0x3FFF); // bits 14 and 15 are reserved
  pdu->spatialStreamIndexPresent = rand8_range(0, 1);
  pdu->spatialStreamIndex = rand16_range(0, MAX_NUM_SPATIAL_STREAMS);
  fill_dl_tti_request_beamforming(&pdu->precodingAndBeamforming);
  pdu->prsPowerOffset = rands16_range(-32767, 32767);
}

static void fill_dl_tti_request(nfapi_nr_dl_tti_request_t *msg)
{
  msg->SFN = rand16_range(0, 1023);
  msg->Slot = rand16_range(0, 159);
  int available_PDUs = rand8_range(5, 16); // Minimum 5 PDUs in order to test at least one of each
  msg->dl_tti_request_body.nPDUs = available_PDUs;
  msg->dl_tti_request_body.nGroup = rand8();

  int pdu = 0;
  int num_PDCCH = rand8_range(1, available_PDUs - 4);

  available_PDUs -= num_PDCCH;
  int num_PDSCH = rand8_range(1, available_PDUs - 3);

  available_PDUs -= num_PDSCH;
  int num_CSI_RS = rand8_range(1, available_PDUs - 2);

  available_PDUs -= num_CSI_RS;
  int num_SSB = rand8_range(1, available_PDUs - 1);

  available_PDUs -= num_SSB;
  int num_PRS = available_PDUs;

  for (int i = 0; i < num_PDCCH; ++pdu, ++i) {
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUSize = rand16();
    fill_dl_tti_request_pdcch_pdu(&msg->dl_tti_request_body.dl_tti_pdu_list[pdu].pdcch_pdu.pdcch_pdu_rel15);
  }

  for (int i = 0; i < num_PDSCH; ++pdu, ++i) {
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUType = NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE;
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUSize = rand16();
    fill_dl_tti_request_pdsch_pdu(&msg->dl_tti_request_body.dl_tti_pdu_list[pdu].pdsch_pdu.pdsch_pdu_rel15);
  }

  for (int i = 0; i < num_CSI_RS; ++pdu, ++i) {
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUType = NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE;
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUSize = rand16();
    fill_dl_tti_request_csi_rs_pdu(&msg->dl_tti_request_body.dl_tti_pdu_list[pdu].csi_rs_pdu.csi_rs_pdu_rel15);
  }

  for (int i = 0; i < num_SSB; ++pdu, ++i) {
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUType = NFAPI_NR_DL_TTI_SSB_PDU_TYPE;
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUSize = rand16();
    fill_dl_tti_request_ssb_pdu(&msg->dl_tti_request_body.dl_tti_pdu_list[pdu].ssb_pdu.ssb_pdu_rel15);
  }

  for (int i = 0; i < num_PRS; ++pdu, ++i) {
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUType = NFAPI_NR_DL_TTI_PRS_PDU_TYPE;
    msg->dl_tti_request_body.dl_tti_pdu_list[pdu].PDUSize = rand16();
    fill_dl_tti_request_prs_pdu(&msg->dl_tti_request_body.dl_tti_pdu_list[pdu].prs_pdu.prs_pdu_rel16);
  }
}

static void test_pack_unpack(nfapi_nr_dl_tti_request_t *req)
{
  uint8_t msg_buf[1024 * 1024 * 6];
  // first test the packing procedure
  int pack_result = fapi_nr_p7_message_pack(req, msg_buf, sizeof(msg_buf), NULL);

  DevAssert(pack_result >= 0 + NFAPI_HEADER_LENGTH);
  // test the unpacking of the header
  // copy first NFAPI_HEADER_LENGTH bytes into a new buffer, to simulate SCTP PEEK
  fapi_message_header_t header;
  uint32_t header_buffer_size = NFAPI_HEADER_LENGTH;
  uint8_t header_buffer[header_buffer_size];
  for (int idx = 0; idx < header_buffer_size; idx++) {
    header_buffer[idx] = msg_buf[idx];
  }
  uint8_t *pReadPackedMessage = header_buffer;
  int unpack_header_result = fapi_nr_p7_message_header_unpack(pReadPackedMessage, NFAPI_HEADER_LENGTH, &header, sizeof(header), 0);
  DevAssert(unpack_header_result >= 0);
  DevAssert(header.message_id == req->header.message_id);
  printf("%d %d\n", header.message_length, req->header.message_length);
  DevAssert(header.message_length == req->header.message_length);
  // test the unpacking and compare with initial message
  nfapi_nr_dl_tti_request_t unpacked_req = {0};
  int unpack_result =
      fapi_nr_p7_message_unpack(msg_buf, header.message_length + NFAPI_HEADER_LENGTH, &unpacked_req, sizeof(unpacked_req), 0);
  DevAssert(unpack_result >= 0);
  DevAssert(eq_dl_tti_request(&unpacked_req, req));
  free_dl_tti_request(&unpacked_req);
}

static void test_copy(const nfapi_nr_dl_tti_request_t *msg)
{
  // Test copy function
  nfapi_nr_dl_tti_request_t copy = {0};
  copy_dl_tti_request(msg, &copy);
  DevAssert(eq_dl_tti_request(msg, &copy));
  free_dl_tti_request(&copy);
}

int main()
{
  fapi_test_init();

  nfapi_nr_dl_tti_request_t req = {.header.message_id = NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST};
  // Fill DL_TTI request
  fill_dl_tti_request(&req);
  // Perform tests
  test_pack_unpack(&req);
  test_copy(&req);
  // All tests successful!
  free_dl_tti_request(&req);
  return 0;
}
