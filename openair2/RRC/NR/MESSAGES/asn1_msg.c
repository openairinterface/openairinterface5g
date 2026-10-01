/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief primitives to build the asn1 messages
 */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h> /* for atoi(3) */
#include <unistd.h> /* for getopt(3) */
#include <string.h> /* for strerror(3) */
#include <sysexits.h> /* for EX_* exit codes */
#include <errno.h>  /* for errno */
#include "common/utils/LOG/log.h"
#include "oai_asn1.h"
#include <asn_application.h>
#include <per_encoder.h>
#include <nr/nr_common.h>
#include <softmodem-common.h>

#include "executables/softmodem-common.h"
#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "asn1_msg.h"
#include "../nr_rrc_proto.h"
#include "LAYER2/nr_pdcp/nr_pdcp_asn1_utils.h"
#include "openair2/RRC/NR/rrc_gNB_measurements.h"

#include "openair3/SECU/key_nas_deriver.h"

#include "NR_DL-CCCH-Message.h"
#include "NR_UL-CCCH-Message.h"
#include "NR_DL-DCCH-Message.h"
#include "NR_RRCReject.h"
#include "NR_RejectWaitTime.h"
#include "NR_RRCSetup.h"
#include "NR_RRCSetup-IEs.h"
#include "NR_SRB-ToAddModList.h"
#include "NR_CellGroupConfig.h"
#include "NR_RLC-BearerConfig.h"
#include "NR_RLC-Config.h"
#include "NR_LogicalChannelConfig.h"
#include "NR_PDCP-Config.h"
#include "NR_MAC-CellGroupConfig.h"
#include "NR_HandoverPreparationInformation.h"
#include "NR_HandoverCommand.h"
#include "NR_HandoverCommand-IEs.h"
#include "NR_AS-Config.h"
#include "NR_SecurityModeCommand.h"
#include "NR_CipheringAlgorithm.h"
#include "NR_RRCReconfiguration-IEs.h"
#include "NR_DRB-ToAddMod.h"
#include "NR_DRB-ToAddModList.h"
#include "NR_SecurityConfig.h"
#include "NR_RRCReconfiguration-v1530-IEs.h"
#include "NR_UL-DCCH-Message.h"
#include "NR_SDAP-Config.h"
#include "NR_RRCReconfigurationComplete.h"
#include "NR_RRCReconfigurationComplete-IEs.h"
#include "NR_DLInformationTransfer.h"
#include "NR_RRCReestablishmentRequest.h"
#include "NR_PCCH-Message.h"
#include "NR_PagingRecord.h"
#include "NR_Paging-v1700-IEs.h"
#include "NR_UE-CapabilityRequestFilterNR.h"
#include "NR_HandoverPreparationInformation.h"
#include "NR_HandoverPreparationInformation-IEs.h"
#include "NR_HandoverCommand.h"
#include "NR_UE-CapabilityRAT-ContainerList.h"
#include "common/utils/nr/nr_common.h"
#if defined(NR_Rel16)
  #include "NR_SCS-SpecificCarrier.h"
  #include "NR_TDD-UL-DL-ConfigCommon.h"
  #include "NR_FrequencyInfoUL.h"
  #include "NR_FrequencyInfoDL.h"
  #include "NR_RACH-ConfigGeneric.h"
  #include "NR_RACH-ConfigCommon.h"
  #include "NR_PUSCH-TimeDomainResourceAllocation.h"
  #include "NR_PUSCH-ConfigCommon.h"
  #include "NR_PUCCH-ConfigCommon.h"
  #include "NR_PDSCH-TimeDomainResourceAllocation.h"
  #include "NR_PDSCH-ConfigCommon.h"
  #include "NR_RateMatchPattern.h"
  #include "NR_RateMatchPatternLTE-CRS.h"
  #include "NR_SearchSpace.h"
  #include "NR_ControlResourceSet.h"
  #include "NR_EUTRA-MBSFN-SubframeConfig.h"
  #include "NR_BWP-DownlinkCommon.h"
  #include "NR_BWP-DownlinkDedicated.h"
  #include "NR_UplinkConfigCommon.h"
  #include "NR_SetupRelease.h"
  #include "NR_PDCCH-ConfigCommon.h"
  #include "NR_BWP-UplinkCommon.h"

  #include "assertions.h"
  //#include "RRCConnectionRequest.h"
  //#include "UL-CCCH-Message.h"
  #include "NR_UL-DCCH-Message.h"
  //#include "DL-CCCH-Message.h"
  #include "NR_DL-DCCH-Message.h"
  //#include "EstablishmentCause.h"
  //#include "RRCConnectionSetup.h"
  #include "NR_SRB-ToAddModList.h"
  #include "NR_DRB-ToAddModList.h"
  //#include "MCCH-Message.h"
  //#define MRB1 1

  //#include "RRCConnectionSetupComplete.h"
  //#include "RRCConnectionReconfigurationComplete.h"
  //#include "RRCConnectionReconfiguration.h"
  #include "NR_MIB.h"
  //#include "SystemInformation.h"

  #include "NR_SIB1.h"
  #include "NR_ServingCellConfigCommon.h"
  //#include "SIB-Type.h"

  //#include "BCCH-DL-SCH-Message.h"

  //#include "PHY/defs.h"

  #include "NR_MeasObjectToAddModList.h"
  #include "NR_ReportConfigToAddModList.h"
  #include "NR_MeasIdToAddModList.h"
  #include "gnb_config.h"
#endif

#include "intertask_interface.h"

#include "common/ran_context.h"
#include "conversions.h"
#include "ds/byte_array.h"

//#define XER_PRINT

typedef struct xer_sprint_string_s {
  char *string;
  size_t string_size;
  size_t string_index;
} xer_sprint_string_t;

/*
 * This is a helper function for xer_sprint, which directs all incoming data
 * into the provided string.
 */
static int xer__nr_print2s(const void *buffer, size_t size, void *app_key)
{
  xer_sprint_string_t *string_buffer = (xer_sprint_string_t *) app_key;
  size_t string_remaining = string_buffer->string_size - string_buffer->string_index;

  if (string_remaining > 0) {
    if (size > string_remaining) {
      size = string_remaining;
    }

    memcpy(&string_buffer->string[string_buffer->string_index], buffer, size);
    string_buffer->string_index += size;
  }

  return 0;
}

int xer_nr_sprint(char *string, size_t string_size, asn_TYPE_descriptor_t *td, void *sptr)
{
  asn_enc_rval_t er;
  xer_sprint_string_t string_buffer;
  string_buffer.string = string;
  string_buffer.string_size = string_size;
  string_buffer.string_index = 0;
  er = xer_encode(td, sptr, XER_F_BASIC, xer__nr_print2s, &string_buffer);

  if (er.encoded < 0) {
    LOG_E(RRC, "xer_sprint encoding error (%zd)!", er.encoded);
    er.encoded = string_buffer.string_size;
  } else {
    if (er.encoded > string_buffer.string_size) {
      LOG_E(RRC, "xer_sprint string buffer too small, got %zd need %zd!", string_buffer.string_size, er.encoded);
      er.encoded = string_buffer.string_size;
    }
  }

  return er.encoded;
}

struct NR_UE_NR_Capability *get_ue_nr_capability(int rnti, uint8_t *buf, uint32_t len)
{
  if (!buf || len == 0) return NULL;

  NR_UE_CapabilityRAT_ContainerList_t *clist = NULL;
  asn_dec_rval_t rval = uper_decode(NULL, &asn_DEF_NR_UE_CapabilityRAT_ContainerList,
                                    (void **)&clist, buf, len, 0, 0);

  if (rval.code != RC_OK) {
    LOG_W(NR_MAC, "UE RNTI %04x: Failed to decode container list\n", rnti);
    return NULL;
  }

  NR_UE_NR_Capability_t *cap = decode_nr_ue_capability(rnti, clist);
  ASN_STRUCT_FREE(asn_DEF_NR_UE_CapabilityRAT_ContainerList, clist);
  return cap;
}

NR_UE_NR_Capability_t *decode_nr_ue_capability(int rnti, const NR_UE_CapabilityRAT_ContainerList_t *clist)
{
  if (!clist) return NULL;

  for (int i = 0; i < clist->list.count; i++) {
    const NR_UE_CapabilityRAT_Container_t *c = clist->list.array[i];
    if (c->rat_Type != NR_RAT_Type_nr)
      continue;

    NR_UE_NR_Capability_t *cap = NULL;
    asn_dec_rval_t rval = uper_decode(NULL, &asn_DEF_NR_UE_NR_Capability,
                                      (void **)&cap,
                                      c->ue_CapabilityRAT_Container.buf,
                                      c->ue_CapabilityRAT_Container.size,
                                      0, 0);
    if (rval.code == RC_OK)
      return cap;

    LOG_W(NR_MAC, "UE RNTI %04x: Failed to decode NR capability (%zu bytes)\n", rnti, rval.consumed);
    ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, cap);
  }
  return NULL;
}

byte_array_t do_SIB2_NR(const NR_SIB2_t *sib2)
{
  byte_array_t msg = {.buf = NULL, .len = 0};
  char errbuf[256] = {0};
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_NR_SIB2, sib2, errbuf, &errlen);
  if (ret != 0) {
    LOG_E(NR_RRC, "SIB2 constraint check failed: %s\n", errbuf);
    return msg;
  }

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_SIB2, NULL, (void *)sib2, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode SIB2\n");
    return msg;
  }

  msg.len = val;
  return msg;
}

byte_array_t do_SIB3_NR(const NR_SIB3_t *sib3)
{
  byte_array_t msg = {.buf = NULL, .len = 0};
  char errbuf[256] = {0};
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_NR_SIB3, sib3, errbuf, &errlen);
  if (ret != 0) {
    LOG_E(NR_RRC, "SIB3 constraint check failed: %s\n", errbuf);
    return msg;
  }

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_SIB3, NULL, (void *)sib3, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode SIB3\n");
    return msg;
  }

  msg.len = val;
  return msg;
}

byte_array_t do_SIB4_NR(NR_SIB4_t *sib4)
{
  byte_array_t msg = {.buf = NULL, .len = 0};
  char errbuf[256] = {0};
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_NR_SIB4, sib4, errbuf, &errlen);
  if (ret != 0) {
    LOG_E(NR_RRC, "SIB4 constraint check failed: %s\n", errbuf);
    return msg;
  }

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_SIB4, NULL, (void *)sib4, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode SIB4\n");
    return msg;
  }

  msg.len = val;
  return msg;
}

int do_RRCReject(uint8_t *const buffer)
{
    asn_enc_rval_t                                   enc_rval;
    NR_DL_CCCH_Message_t                             dl_ccch_msg;
    NR_RRCReject_t                                   *rrcReject;

    memset((void *)&dl_ccch_msg, 0, sizeof(NR_DL_CCCH_Message_t));
    dl_ccch_msg.message.present = NR_DL_CCCH_MessageType_PR_c1;
    dl_ccch_msg.message.choice.c1          = CALLOC(1, sizeof(struct NR_DL_CCCH_MessageType__c1));
    dl_ccch_msg.message.choice.c1->present = NR_DL_CCCH_MessageType__c1_PR_rrcReject;

    dl_ccch_msg.message.choice.c1->choice.rrcReject = CALLOC(1,sizeof(NR_RRCReject_t));
    rrcReject = dl_ccch_msg.message.choice.c1->choice.rrcReject;

    rrcReject->criticalExtensions.choice.rrcReject           = CALLOC(1, sizeof(struct NR_RRCReject_IEs));
    rrcReject->criticalExtensions.choice.rrcReject->waitTime = CALLOC(1, sizeof(NR_RejectWaitTime_t));
    *rrcReject->criticalExtensions.choice.rrcReject->waitTime = 10;

    rrcReject->criticalExtensions.present = NR_RRCReject__criticalExtensions_PR_rrcReject;

    if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
        xer_fprint(stdout, &asn_DEF_NR_DL_CCCH_Message, (void *)&dl_ccch_msg);
    }

    enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_CCCH_Message,
                                    NULL,
                                    (void *)&dl_ccch_msg,
                                    buffer,
                                    100);

    AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n",
                enc_rval.failed_type->name, enc_rval.encoded);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_CCCH_Message, &dl_ccch_msg);

    LOG_D(NR_RRC,"RRCReject Encoded %zd bits (%zd bytes)\n",
            enc_rval.encoded,(enc_rval.encoded+7)/8);
    return (enc_rval.encoded + 7) / 8;
}

//------------------------------------------------------------------------------
int do_RRCSetup(uint8_t *const buffer,
                size_t buffer_size,
                const uint8_t transaction_id,
                const uint8_t *masterCellGroup,
                int masterCellGroup_len,
                NR_SRB_ToAddModList_t *SRBs)
//------------------------------------------------------------------------------
{
  NR_DL_CCCH_Message_t dl_ccch_msg = {0};
  dl_ccch_msg.message.present = NR_DL_CCCH_MessageType_PR_c1;
  asn1cCalloc(dl_ccch_msg.message.choice.c1, dl_msg);
  dl_msg->present = NR_DL_CCCH_MessageType__c1_PR_rrcSetup;
  asn1cCalloc(dl_msg->choice.rrcSetup, rrcSetup);
  rrcSetup->criticalExtensions.present = NR_RRCSetup__criticalExtensions_PR_rrcSetup;
  rrcSetup->rrc_TransactionIdentifier = transaction_id;
  rrcSetup->criticalExtensions.choice.rrcSetup = calloc(1, sizeof(NR_RRCSetup_IEs_t));
  NR_RRCSetup_IEs_t *ie = rrcSetup->criticalExtensions.choice.rrcSetup;

  /****************************** radioBearerConfig ******************************/
  ie->radioBearerConfig.srb_ToAddModList = SRBs;
  ie->radioBearerConfig.srb3_ToRelease = NULL;
  ie->radioBearerConfig.drb_ToAddModList = NULL;
  ie->radioBearerConfig.drb_ToReleaseList = NULL;
  ie->radioBearerConfig.securityConfig = NULL;

  /****************************** masterCellGroup ******************************/
  DevAssert(masterCellGroup && masterCellGroup_len > 0);
  ie->masterCellGroup.buf = malloc(masterCellGroup_len);
  AssertFatal(ie->masterCellGroup.buf != NULL, "could not allocate memory for masterCellGroup\n");
  memcpy(ie->masterCellGroup.buf, masterCellGroup, masterCellGroup_len);
  ie->masterCellGroup.size = masterCellGroup_len;

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_DL_CCCH_Message, (void *)&dl_ccch_msg);

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_CCCH_Message, NULL, (void *)&dl_ccch_msg, buffer, buffer_size);

  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  // free what we did not allocate ourselves
  ie->radioBearerConfig.srb_ToAddModList = NULL;
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_CCCH_Message, &dl_ccch_msg);

  LOG_D(NR_RRC, "RRCSetup Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded + 7) / 8);
  return ((enc_rval.encoded + 7) / 8);
}

int do_NR_SecurityModeCommand(uint8_t *const buffer,
                              const uint8_t Transaction_id,
                              const uint8_t cipheringAlgorithm,
                              NR_IntegrityProtAlgorithm_t integrityProtAlgorithm)
//------------------------------------------------------------------------------
{
  NR_DL_DCCH_Message_t dl_dcch_msg={0};
  asn_enc_rval_t enc_rval;
  dl_dcch_msg.message.present           = NR_DL_DCCH_MessageType_PR_c1;
  asn1cCalloc(dl_dcch_msg.message.choice.c1, c1);
  c1->present = NR_DL_DCCH_MessageType__c1_PR_securityModeCommand;
  asn1cCalloc(c1->choice.securityModeCommand,scm);
  scm->rrc_TransactionIdentifier = Transaction_id;
  scm->criticalExtensions.present = NR_SecurityModeCommand__criticalExtensions_PR_securityModeCommand;

  asn1cCalloc(scm->criticalExtensions.choice.securityModeCommand,scmIE);
  // the two following information could be based on the mod_id
  scmIE->securityConfigSMC.securityAlgorithmConfig.cipheringAlgorithm
    = (NR_CipheringAlgorithm_t)cipheringAlgorithm;
  asn1cCallocOne(scmIE->securityConfigSMC.securityAlgorithmConfig.integrityProtAlgorithm, integrityProtAlgorithm);

  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)&dl_dcch_msg);
  }

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_DCCH_Message,
                                   NULL,
                                   (void *)&dl_dcch_msg,
                                   buffer,
                                   100);

  AssertFatal(enc_rval.encoded >0 , "ASN1 message encoding failed (%s, %lu)!\n",
              enc_rval.failed_type->name, enc_rval.encoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message,&dl_dcch_msg);

  //  rrc_ue_process_ueCapabilityEnquiry(0,1000,&dl_dcch_msg.message.choice.c1.choice.ueCapabilityEnquiry,0);
  //  exit(-1);
  return((enc_rval.encoded+7)/8);
}

int do_NR_SA_UECapabilityEnquiry(uint8_t *const buffer, const uint8_t Transaction_id, const int band)
{
  NR_UE_CapabilityRequestFilterNR_t *sa_band_filter;
  NR_FreqBandList_t *sa_band_list;
  NR_FreqBandInformation_t *sa_band_info;
  NR_FreqBandInformationNR_t *sa_band_infoNR;

  NR_DL_DCCH_Message_t dl_dcch_msg;
  NR_UE_CapabilityRAT_Request_t *ue_capabilityrat_request;

  memset(&dl_dcch_msg,0,sizeof(NR_DL_DCCH_Message_t));
  dl_dcch_msg.message.present           = NR_DL_DCCH_MessageType_PR_c1;
  dl_dcch_msg.message.choice.c1 = CALLOC(1,sizeof(struct NR_DL_DCCH_MessageType__c1));
  dl_dcch_msg.message.choice.c1->present = NR_DL_DCCH_MessageType__c1_PR_ueCapabilityEnquiry;
  dl_dcch_msg.message.choice.c1->choice.ueCapabilityEnquiry = CALLOC(1,sizeof(struct NR_UECapabilityEnquiry));
  dl_dcch_msg.message.choice.c1->choice.ueCapabilityEnquiry->rrc_TransactionIdentifier = Transaction_id;
  dl_dcch_msg.message.choice.c1->choice.ueCapabilityEnquiry->criticalExtensions.present = NR_UECapabilityEnquiry__criticalExtensions_PR_ueCapabilityEnquiry;
  dl_dcch_msg.message.choice.c1->choice.ueCapabilityEnquiry->criticalExtensions.choice.ueCapabilityEnquiry = CALLOC(1,sizeof(struct NR_UECapabilityEnquiry_IEs));
  ue_capabilityrat_request =  CALLOC(1,sizeof(NR_UE_CapabilityRAT_Request_t));
  memset(ue_capabilityrat_request,0,sizeof(NR_UE_CapabilityRAT_Request_t));
  ue_capabilityrat_request->rat_Type = NR_RAT_Type_nr;

  sa_band_infoNR = (NR_FreqBandInformationNR_t*)calloc(1,sizeof(NR_FreqBandInformationNR_t));
  sa_band_infoNR->bandNR = band;
  sa_band_info = (NR_FreqBandInformation_t*)calloc(1,sizeof(NR_FreqBandInformation_t));
  sa_band_info->present = NR_FreqBandInformation_PR_bandInformationNR;
  sa_band_info->choice.bandInformationNR = sa_band_infoNR;
  
  sa_band_list = (NR_FreqBandList_t *)calloc(1, sizeof(NR_FreqBandList_t));
  asn1cSeqAdd(&sa_band_list->list, sa_band_info);

  sa_band_filter = (NR_UE_CapabilityRequestFilterNR_t*)calloc(1,sizeof(NR_UE_CapabilityRequestFilterNR_t));
  sa_band_filter->frequencyBandListFilter = sa_band_list;

  OCTET_STRING_t *req_freq = calloc(1, sizeof(*req_freq));
  AssertFatal(req_freq != NULL, "out of memory\n");
  ssize_t encoded = uper_encode_to_new_buffer(&asn_DEF_NR_UE_CapabilityRequestFilterNR, NULL, sa_band_filter, (void **)&req_freq->buf);
  AssertFatal(encoded > 0, "ASN1 message encoding failed (encoded %ld)!\n", encoded);
  req_freq->size = encoded;

  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_UE_CapabilityRequestFilterNR, (void *)sa_band_filter);
  }
  ASN_STRUCT_FREE(asn_DEF_NR_UE_CapabilityRequestFilterNR, sa_band_filter);

  ue_capabilityrat_request->capabilityRequestFilter = req_freq;

  asn1cSeqAdd(&dl_dcch_msg.message.choice.c1->choice.ueCapabilityEnquiry->criticalExtensions.choice.ueCapabilityEnquiry->ue_CapabilityRAT_RequestList.list,
                   ue_capabilityrat_request);


  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)&dl_dcch_msg);
  }

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_DCCH_Message, NULL, (void *)&dl_dcch_msg, buffer, 100);

  AssertFatal(enc_rval.encoded >0, "ASN1 message encoding failed (%s, %lu)!\n",
              enc_rval.failed_type->name, enc_rval.encoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);

  LOG_D(NR_RRC, "NR UECapabilityRequestEncoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded + 7) / 8);

  return((enc_rval.encoded+7)/8);
}

int do_NR_RRCRelease(uint8_t *buffer, size_t buffer_size, uint8_t Transaction_id)
{
  asn_enc_rval_t enc_rval;
  NR_DL_DCCH_Message_t dl_dcch_msg;
  NR_RRCRelease_t *rrcConnectionRelease;
  memset(&dl_dcch_msg,0,sizeof(NR_DL_DCCH_Message_t));
  dl_dcch_msg.message.present           = NR_DL_DCCH_MessageType_PR_c1;
  dl_dcch_msg.message.choice.c1=CALLOC(1,sizeof(struct NR_DL_DCCH_MessageType__c1));
  dl_dcch_msg.message.choice.c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcRelease;
  dl_dcch_msg.message.choice.c1->choice.rrcRelease = CALLOC(1, sizeof(NR_RRCRelease_t));
  rrcConnectionRelease = dl_dcch_msg.message.choice.c1->choice.rrcRelease;
  // RRCConnectionRelease
  rrcConnectionRelease->rrc_TransactionIdentifier = Transaction_id;
  rrcConnectionRelease->criticalExtensions.present = NR_RRCRelease__criticalExtensions_PR_rrcRelease;
  rrcConnectionRelease->criticalExtensions.choice.rrcRelease = CALLOC(1, sizeof(NR_RRCRelease_IEs_t));
  rrcConnectionRelease->criticalExtensions.choice.rrcRelease->deprioritisationReq =
      CALLOC(1, sizeof(struct NR_RRCRelease_IEs__deprioritisationReq));
  rrcConnectionRelease->criticalExtensions.choice.rrcRelease->deprioritisationReq->deprioritisationType =
      NR_RRCRelease_IEs__deprioritisationReq__deprioritisationType_nr;
  rrcConnectionRelease->criticalExtensions.choice.rrcRelease->deprioritisationReq->deprioritisationTimer =
      NR_RRCRelease_IEs__deprioritisationReq__deprioritisationTimer_min10;

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_DCCH_Message,
                                   NULL,
                                   (void *)&dl_dcch_msg,
                                   buffer,
                                   buffer_size);
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n",
              enc_rval.failed_type->name, enc_rval.encoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
  return((enc_rval.encoded+7)/8);
}

/** @brief Build RRCReconfiguration message (3GPP TS 38.331) */
static NR_RRCReconfiguration_IEs_t *build_RRCReconfiguration_IEs(const nr_rrc_reconfig_param_t *params)
{
  NR_RRCReconfiguration_IEs_t *ie = calloc_or_fail(1, sizeof(*ie));

  // radioBearerConfig
  if ((params->srb_config_list && params->srb_config_list->list.size)
      || (params->drb_config_list && params->drb_config_list->list.size)) {
    ie->radioBearerConfig = calloc_or_fail(1, sizeof(*ie->radioBearerConfig));
    struct NR_RadioBearerConfig *cfg = ie->radioBearerConfig;
    cfg->srb_ToAddModList = params->srb_config_list;
    cfg->drb_ToAddModList = params->drb_config_list;
    cfg->securityConfig = params->security_config;
    cfg->srb3_ToRelease = NULL;
    if (params->n_drb_rel) {
      asn1cCalloc(cfg->drb_ToReleaseList, to_release);
      for (int i = 0; i < params->n_drb_rel; i++) {
        asn1cSequenceAdd(to_release->list, NR_DRB_Identity_t, DRB_release);
        *DRB_release = params->drb_rel[i];
      }
    }
  }

  /* measConfig */
  ie->measConfig = params->meas_config;

  /* nonCriticalExtension, RRCReconfiguration-v1530-IEs */
  if (params->cgc || params->num_nas_msg) {
    // Allocate memory for extension IE
    ie->nonCriticalExtension = calloc_or_fail(1, sizeof(*ie->nonCriticalExtension));
  }

  // Configure Cell Group Config
  if (ie->nonCriticalExtension) {
    if (params->num_nas_msg) {
      asn1cCalloc(ie->nonCriticalExtension->dedicatedNAS_MessageList, list);
      /* dedicatedNAS-MessageList: The field is absent in case of reconfiguration with sync
        otherwise it is optionally present */
      for (int i = 0; i < params->num_nas_msg; i++) {
        asn1cSequenceAdd(list->list, NR_DedicatedNAS_Message_t, msg);
        OCTET_STRING_fromBuf(msg, (char *)params->dedicated_NAS_msg_list[i].buf, params->dedicated_NAS_msg_list[i].len);
      }
    }

    /* masterCellGroup - Transparent forwarding per TS 38.473 */
    if (params->cgc) {
      // Copy pre-encoded CellGroupConfig bytes directly without decode/re-encode (TS 38.473 transparency)
      // CU receives encoded bytes from DU and forwards to UE without modification
      LOG_D(NR_RRC, "Transparent forwarding CellGroupConfig (len=%ld bytes)\n", params->cgc->len);
      ie->nonCriticalExtension->masterCellGroup = calloc_or_fail(1, sizeof(*ie->nonCriticalExtension->masterCellGroup));
      OCTET_STRING_fromBuf(ie->nonCriticalExtension->masterCellGroup, (const char *)params->cgc->buf, params->cgc->len);
    }

    /* masterKeyUpdate */
    if (params->masterKeyUpdate) {
      ie->nonCriticalExtension->masterKeyUpdate = calloc_or_fail(1, sizeof(*ie->nonCriticalExtension->masterKeyUpdate));
      ie->nonCriticalExtension->masterKeyUpdate->keySetChangeIndicator = false;
      ie->nonCriticalExtension->masterKeyUpdate->nextHopChainingCount = params->nextHopChainingCount;
    }
  }

  return ie;
}

static byte_array_t do_HO_RRCReconfiguration(nr_rrc_reconfig_param_t *params)
{
  NR_RRCReconfiguration_IEs_t *ie = build_RRCReconfiguration_IEs(params);
  byte_array_t msg = {.buf = NULL, .len = 0};

  // Create the standalone RRCReconfiguration message
  NR_RRCReconfiguration_t rrcReconf = {0};
  rrcReconf.rrc_TransactionIdentifier = params->transaction_id;
  rrcReconf.criticalExtensions.present = NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
  rrcReconf.criticalExtensions.choice.rrcReconfiguration = ie;

  // Encode the message
  int val = uper_encode_to_new_buffer(&asn_DEF_NR_RRCReconfiguration, NULL, &rrcReconf, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "ASN1 RRCReconfiguration message encoding failed\n");
  } else {
    msg.len = val;
    LOG_D(NR_RRC, "RRCReconfiguration: Encoded (%ld bytes)\n", msg.len);
  }

  // don't free what we did not allocate, so set fields with pointers to NULL
  // if memory comes from outside
  ie->measConfig = NULL;
  if (ie->radioBearerConfig) {
    ie->radioBearerConfig->srb3_ToRelease = NULL;
    ie->radioBearerConfig->drb_ToAddModList = NULL;
    ie->radioBearerConfig->srb_ToAddModList = NULL;
    ie->radioBearerConfig->securityConfig = NULL;
  }

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_RRCReconfiguration, &rrcReconf);

  return msg;
}

void dump_cgc(const uint8_t *buf, size_t len)
{
  // Decode the encoded CellGroupConfig for debugging
  NR_CellGroupConfig_t *temp_cellGroupConfig = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_CellGroupConfig, (void **)&temp_cellGroupConfig, buf, len);
  if (dec_rval.code == RC_OK && dec_rval.consumed > 0) {
    xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, temp_cellGroupConfig);
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, temp_cellGroupConfig);
  } else {
    LOG_W(NR_RRC, "Failed to decode CellGroupConfig (code=%d consumed=%zu)\n", dec_rval.code, dec_rval.consumed);
  }
}

static void dump_mcg(const NR_DL_DCCH_Message_t *dl_dcch_msg)
{
  if (!dl_dcch_msg) {
    LOG_W(NR_RRC, "DL_DCCH_Message is NULL\n");
    return;
  }

  const NR_RRCReconfiguration_IEs_t *reconf_ies = NULL;
  const struct NR_DL_DCCH_MessageType__c1 *c1 = NULL;

  if (dl_dcch_msg->message.present == NR_DL_DCCH_MessageType_PR_c1
      && (c1 = dl_dcch_msg->message.choice.c1)
      && c1->present == NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration
      && c1->choice.rrcReconfiguration
      && c1->choice.rrcReconfiguration->criticalExtensions.present
             == NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration
      && c1->choice.rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration) {
    reconf_ies = c1->choice.rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration;
  }

  if (reconf_ies && reconf_ies->nonCriticalExtension && reconf_ies->nonCriticalExtension->masterCellGroup) {
    const OCTET_STRING_t *mcg = reconf_ies->nonCriticalExtension->masterCellGroup;

    /* decode and XER print CellGroupConfig */
    dump_cgc(mcg->buf, mcg->size);
  } else {
    LOG_W(NR_RRC, "No masterCellGroup found in RRCReconfiguration nonCriticalExtension\n");
  }
}

byte_array_t do_RRCReconfiguration(const nr_rrc_reconfig_param_t *params)
{
  byte_array_t msg = {.buf = NULL, .len = 0};
  NR_RRCReconfiguration_IEs_t *ie = build_RRCReconfiguration_IEs(params);
  if (!ie) {
    LOG_E(NR_RRC, "%s: failed to encode RRCReconfiguration\n", __func__);
    return msg;
  }

  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;

  asn1cCalloc(dl_dcch_msg.message.choice.c1, c1);
  c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;
  asn1cCalloc(c1->choice.rrcReconfiguration, rrcReconf);
  rrcReconf->rrc_TransactionIdentifier = params->transaction_id;
  rrcReconf->criticalExtensions.present = NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
  rrcReconf->criticalExtensions.choice.rrcReconfiguration = ie;

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)&dl_dcch_msg);
    dump_mcg(&dl_dcch_msg);
  }

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_DL_DCCH_Message, NULL, &dl_dcch_msg, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode DL-DCCH message\n");
  } else {
    msg.len = val;
    LOG_D(NR_RRC, "RRCReconfiguration: Encoded (%ld bytes)\n", msg.len);
  }

  // Do not free what is not allocated in this context
  ie->measConfig = NULL;
  if (ie->radioBearerConfig) {
    ie->radioBearerConfig->securityConfig = NULL;
    ie->radioBearerConfig->srb_ToAddModList = NULL;
    ie->radioBearerConfig->drb_ToAddModList = NULL;
  }

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);

  return msg;
}

int do_RRCSetupRequest(uint8_t *buffer, size_t buffer_size, uint8_t *rv, uint64_t fiveG_S_TMSI)
{
  NR_UL_CCCH_Message_t ul_ccch_msg = {0};
  ul_ccch_msg.message.present           = NR_UL_CCCH_MessageType_PR_c1;
  asn1cCalloc(ul_ccch_msg.message.choice.c1, c1);
  c1->present = NR_UL_CCCH_MessageType__c1_PR_rrcSetupRequest;
  asn1cCalloc(c1->choice.rrcSetupRequest, rrcSetupRequest);

  if (fiveG_S_TMSI == UINT64_MAX) {
    /* set the ue-Identity to a random value */
    rrcSetupRequest->rrcSetupRequest.ue_Identity.present = NR_InitialUE_Identity_PR_randomValue;
    BIT_STRING_t *str = &rrcSetupRequest->rrcSetupRequest.ue_Identity.choice.randomValue;
    str->size = 5;
    str->bits_unused = 1;
    str->buf = calloc_or_fail(str->size, sizeof(str->buf[0]));
    str->buf[0] = rv[0];
    str->buf[1] = rv[1];
    str->buf[2] = rv[2];
    str->buf[3] = rv[3];
    str->buf[4] = rv[4] & 0xfe;
  } else {
    uint64_t fiveG_S_TMSI_part1 = nr_extract_5g_s_tmsi_part1(fiveG_S_TMSI);
    /** set the ue-Identity to ng-5G-S-TMSI-Part1
     * ng-5G-S-TMSI-Part1: the rightmost 39 bits of 5G-S-TMSI
     * BIT STRING (SIZE (39)) - 3GPP TS 38.331 */
    LOG_D(NR_RRC, "5G-S-TMSI: %lu, set the ue-Identity to ng-5G-S-TMSI-Part1 %lu\n", fiveG_S_TMSI, fiveG_S_TMSI_part1);
    rrcSetupRequest->rrcSetupRequest.ue_Identity.present = NR_InitialUE_Identity_PR_ng_5G_S_TMSI_Part1;
    BIT_STRING_t *str = &rrcSetupRequest->rrcSetupRequest.ue_Identity.choice.ng_5G_S_TMSI_Part1;
    str->size = 5;
    str->bits_unused = 1;
    str->buf = calloc_or_fail(str->size, sizeof(str->buf[0]));
    str->buf[0] = (fiveG_S_TMSI_part1 >> 31) & 0xff;
    str->buf[1] = (fiveG_S_TMSI_part1 >> 23) & 0xff;
    str->buf[2] = (fiveG_S_TMSI_part1 >> 15) & 0xff;
    str->buf[3] = (fiveG_S_TMSI_part1 >> 7) & 0xff;
    str->buf[4] = (fiveG_S_TMSI_part1 << 1) & 0xff;
  }

  rrcSetupRequest->rrcSetupRequest.establishmentCause = NR_EstablishmentCause_mo_Signalling; //EstablishmentCause_mo_Data;
  rrcSetupRequest->rrcSetupRequest.spare.buf = CALLOC(1, 1);
  rrcSetupRequest->rrcSetupRequest.spare.buf[0] = 0; // spare not used
  rrcSetupRequest->rrcSetupRequest.spare.size=1;
  rrcSetupRequest->rrcSetupRequest.spare.bits_unused = 7;

  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_UL_CCCH_Message, (void *)&ul_ccch_msg);
  }

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_CCCH_Message, NULL, (void *)&ul_ccch_msg, buffer, buffer_size);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  LOG_D(NR_RRC,"[UE] RRCSetupRequest Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded+7)/8);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_CCCH_Message, &ul_ccch_msg);
  return((enc_rval.encoded+7)/8);
}

//------------------------------------------------------------------------------
int do_NR_RRCReconfigurationComplete_for_nsa(
  uint8_t *buffer,
  size_t buffer_size,
  NR_RRC_TransactionIdentifier_t Transaction_id)
//------------------------------------------------------------------------------
{
  NR_RRCReconfigurationComplete_t rrc_complete_msg;
  memset(&rrc_complete_msg, 0, sizeof(rrc_complete_msg));
  rrc_complete_msg.rrc_TransactionIdentifier = Transaction_id;
  rrc_complete_msg.criticalExtensions.choice.rrcReconfigurationComplete =
        CALLOC(1, sizeof(*rrc_complete_msg.criticalExtensions.choice.rrcReconfigurationComplete));
  rrc_complete_msg.criticalExtensions.present =
	NR_RRCReconfigurationComplete__criticalExtensions_PR_rrcReconfigurationComplete;
  rrc_complete_msg.criticalExtensions.choice.rrcReconfigurationComplete->nonCriticalExtension = NULL;
  rrc_complete_msg.criticalExtensions.choice.rrcReconfigurationComplete->lateNonCriticalExtension = NULL;
  if (0) {
    xer_fprint(stdout, &asn_DEF_NR_RRCReconfigurationComplete, (void *)&rrc_complete_msg);
  }

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_RRCReconfigurationComplete,
                                                  NULL,
                                                  (void *)&rrc_complete_msg,
                                                  buffer,
                                                  buffer_size);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n",
               enc_rval.failed_type->name, enc_rval.encoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_RRCReconfigurationComplete, &rrc_complete_msg);
  LOG_A(NR_RRC, "rrcReconfigurationComplete Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded+7)/8);
  return((enc_rval.encoded+7)/8);
}

//------------------------------------------------------------------------------
int do_NR_RRCReconfigurationComplete(uint8_t *buffer, size_t buffer_size, const uint8_t Transaction_id)
//------------------------------------------------------------------------------
{
  NR_UL_DCCH_Message_t ul_dcch_msg = {0};
  ul_dcch_msg.message.present                     = NR_UL_DCCH_MessageType_PR_c1;
  asn1cCalloc(ul_dcch_msg.message.choice.c1, c1);
  c1->present = NR_UL_DCCH_MessageType__c1_PR_rrcReconfigurationComplete;
  asn1cCalloc(c1->choice.rrcReconfigurationComplete, reconfComplete);
  reconfComplete->rrc_TransactionIdentifier = Transaction_id;
  reconfComplete->criticalExtensions.present = NR_RRCReconfigurationComplete__criticalExtensions_PR_rrcReconfigurationComplete;
  asn1cCalloc(reconfComplete->criticalExtensions.choice.rrcReconfigurationComplete, extension);
  extension->nonCriticalExtension = NULL;
  extension->lateNonCriticalExtension = NULL;
  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
  }

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message, NULL, (void *)&ul_dcch_msg, buffer, buffer_size);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n",
               enc_rval.failed_type->name, enc_rval.encoded);
  LOG_I(NR_RRC,"rrcReconfigurationComplete Encoded %zd bits (%zd bytes)\n",enc_rval.encoded,(enc_rval.encoded+7)/8);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
  return((enc_rval.encoded+7)/8);
}

int do_RRCSetupComplete(uint8_t *buffer,
                        size_t buffer_size,
                        const uint8_t Transaction_id,
                        uint8_t sel_plmn_id,
                        bool is_rrc_connection_setup,
                        uint64_t fiveG_s_tmsi,
                        const int dedicatedInfoNASLength,
                        const char *dedicatedInfoNAS)
{
  NR_UL_DCCH_Message_t ul_dcch_msg = {0};
  ul_dcch_msg.message.present = NR_UL_DCCH_MessageType_PR_c1;
  ul_dcch_msg.message.choice.c1 = CALLOC(1,sizeof(struct NR_UL_DCCH_MessageType__c1));
  ul_dcch_msg.message.choice.c1->present = NR_UL_DCCH_MessageType__c1_PR_rrcSetupComplete;
  ul_dcch_msg.message.choice.c1->choice.rrcSetupComplete = CALLOC(1, sizeof(NR_RRCSetupComplete_t));
  NR_RRCSetupComplete_t *RrcSetupComplete = ul_dcch_msg.message.choice.c1->choice.rrcSetupComplete;
  RrcSetupComplete->rrc_TransactionIdentifier = Transaction_id;
  RrcSetupComplete->criticalExtensions.present = NR_RRCSetupComplete__criticalExtensions_PR_rrcSetupComplete;
  RrcSetupComplete->criticalExtensions.choice.rrcSetupComplete = CALLOC(1, sizeof(NR_RRCSetupComplete_IEs_t));
  NR_RRCSetupComplete_IEs_t *ies = RrcSetupComplete->criticalExtensions.choice.rrcSetupComplete;
  ies->selectedPLMN_Identity = sel_plmn_id;
  ies->registeredAMF = NULL;
  /* RRCSetup is received in response to an RRCSetupRequest
   * set the ng-5G-S-TMSI-Value to ng-5G-S-TMSI-Part2
   * i.e. the leftmost 9 bits of 5G-S-TMSI (5.3.3.4 of 3GPP TS 38.331) */
  if (fiveG_s_tmsi != UINT64_MAX) {
    if (is_rrc_connection_setup) {
      ies->ng_5G_S_TMSI_Value = calloc_or_fail(1, sizeof(*ies->ng_5G_S_TMSI_Value));
      ies->ng_5G_S_TMSI_Value->present = NR_RRCSetupComplete_IEs__ng_5G_S_TMSI_Value_PR_ng_5G_S_TMSI_Part2;
      BIT_STRING_t *str = &ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI_Part2;
      str->size = 2;
      str->bits_unused = 7;
      str->buf = calloc_or_fail(str->size, sizeof(str->buf[0]));
      uint16_t fiveG_s_tmsi_part2 = nr_extract_5g_s_tmsi_part2(fiveG_s_tmsi);
      str->buf[0] = (fiveG_s_tmsi_part2 >> (8 - str->bits_unused)) & 0xFF;
      str->buf[1] = (fiveG_s_tmsi_part2 << str->bits_unused) & 0xFF;
      LOG_D(NR_RRC, "5G-S-TMSI part 2 %d in RRCSetupComplete (5G-S-TMSI %ld)\n", fiveG_s_tmsi_part2, fiveG_s_tmsi);
    } else {
      ies->ng_5G_S_TMSI_Value = CALLOC(1, sizeof(struct NR_RRCSetupComplete_IEs__ng_5G_S_TMSI_Value));
      ies->ng_5G_S_TMSI_Value->present = NR_RRCSetupComplete_IEs__ng_5G_S_TMSI_Value_PR_ng_5G_S_TMSI;
      FIVEG_S_TMSI_TO_BIT_STRING(fiveG_s_tmsi, &ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI);
      LOG_D(NR_RRC, "5G-S-TMSI %lu in RRCSetupComplete\n", fiveG_s_tmsi);
    }
  } else {
    LOG_D(NR_RRC, "5G-S-TMSI is not available!\n");
    ies->ng_5G_S_TMSI_Value = NULL;
  }

  memset(&ies->dedicatedNAS_Message,0,sizeof(OCTET_STRING_t));
  OCTET_STRING_fromBuf(&ies->dedicatedNAS_Message, dedicatedInfoNAS, dedicatedInfoNASLength);
  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
  }

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message,
                                                  NULL,
                                                  (void *)&ul_dcch_msg,
                                                  buffer,
                                                  buffer_size);
  AssertFatal(enc_rval.encoded > 0,"ASN1 message encoding failed (%s, %lu)!\n",
              enc_rval.failed_type->name,enc_rval.encoded);
  LOG_D(NR_RRC,"RRCSetupComplete Encoded %zd bits (%zd bytes)\n",enc_rval.encoded,(enc_rval.encoded+7)/8);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
  return((enc_rval.encoded+7)/8);
}

// This function is implemented for event A2/A3 and periodical report
int do_nrMeasurementReport_SA(long trigger_to_measid,
                              long trigger_quantity,
                              bool report_rsrp,
                              long rs_type,
                              uint16_t Nid_cell,
                              int rsrp_index,
                              int num_neighbor_cells,
                              const uint16_t *neighbor_Nid_cells,
                              const int *neighbor_rsrp_indexes,
                              uint8_t *buffer,
                              size_t buffer_size)
{
  asn_enc_rval_t enc_rval;
  NR_UL_DCCH_Message_t ul_dcch_msg = {0};

  ul_dcch_msg.message.present = NR_UL_DCCH_MessageType_PR_c1;
  asn1cCalloc(ul_dcch_msg.message.choice.c1, c1);
  c1->present = NR_UL_DCCH_MessageType__c1_PR_measurementReport;

  asn1cCalloc(c1->choice.measurementReport, measurementReport);
  measurementReport->criticalExtensions.present = NR_MeasurementReport__criticalExtensions_PR_measurementReport;

  asn1cCalloc(measurementReport->criticalExtensions.choice.measurementReport, mrIE);
  mrIE->measResults.measId = trigger_to_measid;

  // Serving cell
  NR_MeasResultServMO_t *measResultServMo = calloc_or_fail(1, sizeof(*measResultServMo));

  NR_MeasResultNR_t *measResultServingCell = &measResultServMo->measResultServingCell;
  asn1cCalloc(measResultServingCell->physCellId, pci);
  *pci = Nid_cell;

  struct NR_MeasQuantityResults *active_mq_res = calloc_or_fail(1, sizeof(*active_mq_res));
  if (trigger_quantity == NR_MeasTriggerQuantityOffset_PR_rsrp || report_rsrp) {
    asn1cCalloc(active_mq_res->rsrp, rsrp);
    // Assign precomputed RSRP index
    *rsrp = rsrp_index;
    if (rs_type == NR_NR_RS_Type_ssb)
      measResultServingCell->measResult.cellResults.resultsSSB_Cell = active_mq_res;
    else
      measResultServingCell->measResult.cellResults.resultsCSI_RS_Cell = active_mq_res;
  }

  ASN_SEQUENCE_ADD(&mrIE->measResults.measResultServingMOList.list, measResultServMo);

  // Neighbor cells
  if (num_neighbor_cells > 0) {
    struct NR_MeasResults__measResultNeighCells *measResultNeighCells = calloc_or_fail(1, sizeof(*measResultNeighCells));
    mrIE->measResults.measResultNeighCells = measResultNeighCells;
    measResultNeighCells->present = NR_MeasResults__measResultNeighCells_PR_measResultListNR;
    NR_MeasResultListNR_t *measResultListNR = calloc_or_fail(1, sizeof(*measResultListNR));
    measResultNeighCells->choice.measResultListNR = measResultListNR;

    for (int i = 0; i < num_neighbor_cells; i++) {
      struct NR_MeasResultNR *meas_result_neigh_cell = calloc_or_fail(1, sizeof(*meas_result_neigh_cell));
      asn1cCalloc(meas_result_neigh_cell->physCellId, neighbor_pci);
      *neighbor_pci = neighbor_Nid_cells[i];
      struct NR_MeasResultNR__measResult__cellResults *cellResults = &meas_result_neigh_cell->measResult.cellResults;
      struct NR_MeasQuantityResults *neigh_mq_res = calloc_or_fail(1, sizeof(*neigh_mq_res));
      if (trigger_quantity == NR_MeasTriggerQuantityOffset_PR_rsrp || report_rsrp) {
        asn1cCalloc(neigh_mq_res->rsrp, rsrp);
        *rsrp = neighbor_rsrp_indexes[i];
        if (rs_type == NR_NR_RS_Type_ssb)
          cellResults->resultsSSB_Cell = neigh_mq_res;
        else
          cellResults->resultsCSI_RS_Cell = neigh_mq_res;
      }
      ASN_SEQUENCE_ADD(&measResultListNR->list, meas_result_neigh_cell);
    }
  }

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message, NULL, (void *)&ul_dcch_msg, buffer, buffer_size);
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
  }

  LOG_I(NR_RRC, "MeasurementReport Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded + 7) / 8);

  int ret = (enc_rval.encoded + 7) / 8;
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
  return ret;
}

byte_array_t do_NR_DLInformationTransfer(uint8_t transaction_id, uint32_t pdu_length, uint8_t *pdu_buffer)
{
  byte_array_t msg = {0};
  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;
  asn1cCalloc(dl_dcch_msg.message.choice.c1, c1);
  c1->present = NR_DL_DCCH_MessageType__c1_PR_dlInformationTransfer;

  asn1cCalloc(c1->choice.dlInformationTransfer, infoTransfer);
  infoTransfer->rrc_TransactionIdentifier = transaction_id;
  infoTransfer->criticalExtensions.present = NR_DLInformationTransfer__criticalExtensions_PR_dlInformationTransfer;

  asn1cCalloc(infoTransfer->criticalExtensions.choice.dlInformationTransfer, dlInfoTransfer);
  asn1cCalloc(dlInfoTransfer->dedicatedNAS_Message, nas);
  /* Takes ownership of pdu_buffer, freed below via ASN_STRUCT_FREE_CONTENTS_ONLY */
  nas->buf = pdu_buffer;
  nas->size = pdu_length;

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_DL_DCCH_Message, NULL, &dl_dcch_msg, (void **)&msg.buf);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
  if (val <= 0) {
    LOG_E(NR_RRC, "ASN1 message encoding failed (DLInformationTransfer, %d)!\n", val);
    return msg;
  }
  msg.len = val;
  LOG_D(NR_RRC, "DLInformationTransfer Encoded %ld bytes\n", msg.len);
  return msg;
}

int do_NR_ULInformationTransfer(uint8_t **buffer, uint32_t pdu_length, uint8_t *pdu_buffer)
{
    ssize_t encoded;
    NR_UL_DCCH_Message_t ul_dcch_msg;
    memset(&ul_dcch_msg, 0, sizeof(NR_UL_DCCH_Message_t));
    ul_dcch_msg.message.present           = NR_UL_DCCH_MessageType_PR_c1;
    ul_dcch_msg.message.choice.c1          = CALLOC(1,sizeof(struct NR_UL_DCCH_MessageType__c1));
    ul_dcch_msg.message.choice.c1->present = NR_UL_DCCH_MessageType__c1_PR_ulInformationTransfer;
    ul_dcch_msg.message.choice.c1->choice.ulInformationTransfer = CALLOC(1,sizeof(struct NR_ULInformationTransfer));
    ul_dcch_msg.message.choice.c1->choice.ulInformationTransfer->criticalExtensions.present = NR_ULInformationTransfer__criticalExtensions_PR_ulInformationTransfer;
    ul_dcch_msg.message.choice.c1->choice.ulInformationTransfer->criticalExtensions.choice.ulInformationTransfer = CALLOC(1,sizeof(struct NR_ULInformationTransfer_IEs));
    struct NR_ULInformationTransfer_IEs *ulInformationTransfer = ul_dcch_msg.message.choice.c1->choice.ulInformationTransfer->criticalExtensions.choice.ulInformationTransfer;
    ulInformationTransfer->dedicatedNAS_Message = CALLOC(1,sizeof(NR_DedicatedNAS_Message_t));
    ulInformationTransfer->dedicatedNAS_Message->buf = pdu_buffer;
    ulInformationTransfer->dedicatedNAS_Message->size = pdu_length;
    ulInformationTransfer->lateNonCriticalExtension = NULL;
    encoded = uper_encode_to_new_buffer (&asn_DEF_NR_UL_DCCH_Message, NULL, (void *) &ul_dcch_msg, (void **) buffer);
    AssertFatal(encoded > 0,"ASN1 message encoding failed (%s, %ld)!\n",
                "ULInformationTransfer",encoded);
    ulInformationTransfer->dedicatedNAS_Message->buf = NULL; // Let caller decide when to free it
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
    LOG_D(NR_RRC,"ULInformationTransfer Encoded %zd bytes\n",encoded);

    return encoded;
}

int do_RRCReestablishmentRequest(uint8_t *buffer,
                                 NR_ReestablishmentCause_t cause,
                                 uint32_t cell_id,
                                 uint16_t c_rnti,
                                 uint16_t short_mac_i)
{
  asn_enc_rval_t enc_rval;
  NR_UL_CCCH_Message_t ul_ccch_msg;
  NR_RRCReestablishmentRequest_t *rrcReestablishmentRequest;
  uint8_t buf[2];

  memset((void *)&ul_ccch_msg,0,sizeof(NR_UL_CCCH_Message_t));
  ul_ccch_msg.message.present            = NR_UL_CCCH_MessageType_PR_c1;
  ul_ccch_msg.message.choice.c1          = CALLOC(1, sizeof(struct NR_UL_CCCH_MessageType__c1));
  ul_ccch_msg.message.choice.c1->present = NR_UL_CCCH_MessageType__c1_PR_rrcReestablishmentRequest;
  ul_ccch_msg.message.choice.c1->choice.rrcReestablishmentRequest = CALLOC(1, sizeof(NR_RRCReestablishmentRequest_t));

  rrcReestablishmentRequest = ul_ccch_msg.message.choice.c1->choice.rrcReestablishmentRequest;
  // test
  rrcReestablishmentRequest->rrcReestablishmentRequest.reestablishmentCause = cause;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.c_RNTI = c_rnti;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.physCellId = cell_id;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf = buf;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf[0] = (short_mac_i >> 8) & 0xFF;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf[1] = short_mac_i & 0xFF;
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.shortMAC_I.size = 2;

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_UL_CCCH_Message, (void *)&ul_ccch_msg);
  }

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_CCCH_Message,
                                   NULL,
                                   (void *)&ul_ccch_msg,
                                   buffer,
                                   100);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  // shortMAC_I.buf is on the stack, cannot free useing ASN_STRUCT_FREE macro
  rrcReestablishmentRequest->rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf = NULL;
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_CCCH_Message, &ul_ccch_msg);
  LOG_D(NR_RRC,"[UE] RRCReestablishmentRequest Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded+7)/8);
  return((enc_rval.encoded+7)/8);
}

//------------------------------------------------------------------------------
int do_RRCReestablishment(int8_t nh_ncc, uint8_t *const buffer, size_t buffer_size, const uint8_t Transaction_id)
{
  asn_enc_rval_t enc_rval;
  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  NR_RRCReestablishment_t *rrcReestablishment = NULL;

  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;
  dl_dcch_msg.message.choice.c1 = calloc(1, sizeof(struct NR_DL_DCCH_MessageType__c1));
  dl_dcch_msg.message.choice.c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcReestablishment;
  dl_dcch_msg.message.choice.c1->choice.rrcReestablishment = CALLOC(1, sizeof(NR_RRCReestablishment_t));
  rrcReestablishment = dl_dcch_msg.message.choice.c1->choice.rrcReestablishment;

  /****************************** masterCellGroup ******************************/
  rrcReestablishment->rrc_TransactionIdentifier = Transaction_id;
  rrcReestablishment->criticalExtensions.present = NR_RRCReestablishment__criticalExtensions_PR_rrcReestablishment;
  rrcReestablishment->criticalExtensions.choice.rrcReestablishment = CALLOC(1, sizeof(NR_RRCReestablishment_IEs_t));

  // 3GPP TS 33.501 Section 6.11 Security handling for RRC connection re-establishment procedure
  rrcReestablishment->criticalExtensions.choice.rrcReestablishment->nextHopChainingCount = nh_ncc;
  rrcReestablishment->criticalExtensions.choice.rrcReestablishment->lateNonCriticalExtension = NULL;
  rrcReestablishment->criticalExtensions.choice.rrcReestablishment->nonCriticalExtension = NULL;

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)&dl_dcch_msg);
  }

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_DCCH_Message, NULL, (void *)&dl_dcch_msg, buffer, buffer_size);

  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n",
              enc_rval.failed_type->name, enc_rval.encoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);

  LOG_D(NR_RRC, "RRCReestablishment Encoded %u bits (%u bytes)\n", (uint32_t)enc_rval.encoded, (uint32_t)(enc_rval.encoded + 7) / 8);
  return ((enc_rval.encoded + 7) / 8);
}

int do_RRCReestablishmentComplete(uint8_t *buffer, size_t buffer_size, int64_t rrc_TransactionIdentifier)
{
  asn_enc_rval_t enc_rval;
  NR_UL_DCCH_Message_t ul_dcch_msg;
  NR_RRCReestablishmentComplete_t *rrcReestablishmentComplete;

  memset((void *)&ul_dcch_msg,0,sizeof(NR_UL_DCCH_Message_t));
  ul_dcch_msg.message.present            = NR_UL_DCCH_MessageType_PR_c1;
  ul_dcch_msg.message.choice.c1          = CALLOC(1, sizeof(struct NR_UL_DCCH_MessageType__c1));
  ul_dcch_msg.message.choice.c1->present = NR_UL_DCCH_MessageType__c1_PR_rrcReestablishmentComplete;
  ul_dcch_msg.message.choice.c1->choice.rrcReestablishmentComplete = CALLOC(1, sizeof(NR_RRCReestablishmentComplete_t));

  rrcReestablishmentComplete = ul_dcch_msg.message.choice.c1->choice.rrcReestablishmentComplete;
  rrcReestablishmentComplete->rrc_TransactionIdentifier = rrc_TransactionIdentifier;
  rrcReestablishmentComplete->criticalExtensions.present = NR_RRCReestablishmentComplete__criticalExtensions_PR_rrcReestablishmentComplete;
  rrcReestablishmentComplete->criticalExtensions.choice.rrcReestablishmentComplete = CALLOC(1, sizeof(NR_RRCReestablishmentComplete_IEs_t));
  rrcReestablishmentComplete->criticalExtensions.choice.rrcReestablishmentComplete->lateNonCriticalExtension = NULL;
  rrcReestablishmentComplete->criticalExtensions.choice.rrcReestablishmentComplete->nonCriticalExtension = NULL;

  if ( LOG_DEBUGFLAG(DEBUG_ASN1) ) {
    xer_fprint(stdout, &asn_DEF_NR_UL_CCCH_Message, (void *)&ul_dcch_msg);
  }

  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message,
                                   NULL,
                                   (void *)&ul_dcch_msg,
                                   buffer,
                                   buffer_size);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  LOG_D(NR_RRC,"[UE] RRCReestablishmentComplete Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded+7)/8);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
  return((enc_rval.encoded+7)/8);
}

static NR_MeasObjectToAddMod_t *get_MeasObject(const struct NR_MeasTiming__frequencyAndTiming *ft,
                                               int band,
                                               NR_ARFCN_ValueNR_t ssbFrequency,
                                               NR_MeasObjectId_t measObjectId)
{
  const NR_SSB_MTC_t *ssb_mtc = &ft->ssb_MeasurementTimingConfiguration;
  NR_MeasObjectToAddMod_t *mo = calloc_or_fail(1, sizeof(*mo));
  mo->measObjectId = measObjectId;
  mo->measObject.present = NR_MeasObjectToAddMod__measObject_PR_measObjectNR;
  NR_MeasObjectNR_t *monr = calloc_or_fail(1, sizeof(*monr));
  asn1cCallocOne(monr->ssbFrequency, ssbFrequency);
  asn1cCallocOne(monr->ssbSubcarrierSpacing, ft->ssbSubcarrierSpacing);
  monr->referenceSignalConfig.ssb_ConfigMobility = calloc_or_fail(1, sizeof(*monr->referenceSignalConfig.ssb_ConfigMobility));
  monr->referenceSignalConfig.ssb_ConfigMobility->deriveSSB_IndexFromCell = true;
  monr->absThreshSS_BlocksConsolidation = calloc_or_fail(1, sizeof(*monr->absThreshSS_BlocksConsolidation));
  asn1cCallocOne(monr->absThreshSS_BlocksConsolidation->thresholdRSRP, 36);
  asn1cCallocOne(monr->nrofSS_BlocksToAverage, 8);
  monr->smtc1 = calloc_or_fail(1, sizeof(*monr->smtc1));
  monr->smtc1->periodicityAndOffset = ssb_mtc->periodicityAndOffset;
  monr->smtc1->duration = ssb_mtc->duration;
  monr->quantityConfigIndex = 1;
  monr->ext1 = calloc_or_fail(1, sizeof(*monr->ext1));
  asn1cCallocOne(monr->ext1->freqBandIndicatorNR, band);
  mo->measObject.choice.measObjectNR = monr;
  return mo;
}

static NR_MeasIdToAddMod_t *get_MeasId(NR_MeasId_t measId, NR_ReportConfigId_t reportConfigId, NR_MeasObjectId_t measObjectId)
{
  NR_MeasIdToAddMod_t *measid = calloc_or_fail(1, sizeof(NR_MeasIdToAddMod_t));
  measid->measId = measId;
  measid->reportConfigId = reportConfigId;
  measid->measObjectId = measObjectId;
  return measid;
}

NR_MeasConfig_t *get_MeasConfig(gNB_RRC_UE_t *ue,
                                const NR_MeasTiming_t *mt,
                                int band,
                                int nr_pci,
                                NR_ReportConfigToAddMod_t *rc_PER,
                                NR_ReportConfigToAddMod_t *rc_A2,
                                seq_arr_t *rc_A3_seq,
                                seq_arr_t *neigh_seq,
                                int *neigh_a3_id)
{
  DevAssert(mt != NULL && mt->frequencyAndTiming != NULL);
  const struct NR_MeasTiming__frequencyAndTiming *ft = mt->frequencyAndTiming;

  NR_MeasConfig_t *mc = calloc_or_fail(1, sizeof(*mc));
  mc->measObjectToAddModList = calloc_or_fail(1, sizeof(*mc->measObjectToAddModList));
  mc->reportConfigToAddModList = calloc_or_fail(1, sizeof(*mc->reportConfigToAddModList));
  mc->measIdToAddModList = calloc_or_fail(1, sizeof(*mc->measIdToAddModList));

  // Report Configuration: A reporting configuration defines the reporting criteria. The reporting criteria are classified as event
  // triggered reporting, periodic reporting, CGI reporting or SFTD reporting.

  // Periodic report
  if (rc_PER)
    asn1cSeqAdd(&mc->reportConfigToAddModList->list, rc_PER);

  // Event A2
  if (rc_A2)
    asn1cSeqAdd(&mc->reportConfigToAddModList->list, rc_A2);

  // Event A3
  if (rc_A3_seq) {
    for (int i = 0; i < rc_A3_seq->size; i++) {
      NR_ReportConfigToAddMod_t *rc_A3 = (NR_ReportConfigToAddMod_t *)seq_arr_at(rc_A3_seq, i);
      // Create a deep copy of the report config
      NR_ReportConfigToAddMod_t *rc_A3_copy = NULL;
      int result = asn_copy(&asn_DEF_NR_ReportConfigToAddMod, (void **)&rc_A3_copy, rc_A3);
      AssertFatal(result >= 0, "error during asn_copy() of ReportConfigToAddMod\n");
      asn1cSeqAdd(&mc->reportConfigToAddModList->list, rc_A3_copy);
    }
  }

  // Measurement Objects: Specifies what is to be measured. For NR and inter-RAT E-UTRA measurements, this may include
  // cell-specific offsets, blacklisted cells to be ignored and whitelisted cells to consider for measurements.

  // Serving cell
  int serving_cell_measobj_id = allocate_measurement_object_id(ue);
  NR_MeasObjectToAddMod_t *mo1 = get_MeasObject(ft, band, ft->carrierFreq, serving_cell_measobj_id);
  NR_MeasObjectNR_t *monr1 = mo1->measObject.choice.measObjectNR;
  monr1->cellsToAddModList = calloc_or_fail(1, sizeof(*monr1->cellsToAddModList));
  NR_CellsToAddMod_t *cell = calloc_or_fail(1, sizeof(*cell));
  cell->physCellId = nr_pci;
  ASN_SEQUENCE_ADD(&monr1->cellsToAddModList->list, cell);
  asn1cSeqAdd(&mc->measObjectToAddModList->list, mo1);

  int *neighbour_measurement_object_id = NULL;
  if (neigh_seq && neigh_seq->size)
    neighbour_measurement_object_id = calloc_or_fail(neigh_seq->size, sizeof(int));

  // Neighbour cells
  if (neigh_seq) {
    int i = 0;
    FOR_EACH_SEQ_ARR(nr_neighbour_cell_t *, neigh_cell, neigh_seq) {
      int mo_id = allocate_measurement_object_id(ue);
      neighbour_measurement_object_id[i] = mo_id;
      NR_MeasObjectToAddMod_t *mo_neighbour = get_MeasObject(ft,  neigh_cell->band, neigh_cell->absoluteFrequencySSB, mo_id);
      NR_MeasObjectNR_t *monr = mo_neighbour->measObject.choice.measObjectNR;
      monr->cellsToAddModList = calloc_or_fail(1, sizeof(*monr->cellsToAddModList));
      NR_CellsToAddMod_t *cell = calloc_or_fail(1, sizeof(*cell));
      cell->physCellId = neigh_cell->physicalCellId;
      ASN_SEQUENCE_ADD(&monr->cellsToAddModList->list, cell);
      asn1cSeqAdd(&mc->measObjectToAddModList->list, mo_neighbour);
      i++;
    }
  }

  // Measurement identities: A list of measurement identities where each measurement identity links one measurement object with one
  // reporting configuration. By configuring multiple measurement identities, it is possible to link more than one measurement
  // object to the same reporting configuration, as well as to link more than one reporting configuration to the same measurement
  // object.

  // MeasId for Periodic report
  if (rc_PER) {
    for (int i = 0; i < mc->measObjectToAddModList->list.count; i++) {
      const NR_MeasObjectId_t measObjectId = mc->measObjectToAddModList->list.array[i]->measObjectId;
      int meas_idx = allocate_measurement_id(ue);
      NR_MeasIdToAddMod_t *measid = get_MeasId(meas_idx, rc_PER->reportConfigId, measObjectId);
      asn1cSeqAdd(&mc->measIdToAddModList->list, measid);
    }
  }

  // MeasId for Event A2
  if (rc_A2) {
    int meas_idx = allocate_measurement_id(ue);
    int meas_obj_id = serving_cell_measobj_id;
    NR_MeasIdToAddMod_t *measid_A2 = get_MeasId(meas_idx, rc_A2->reportConfigId, meas_obj_id);
    asn1cSeqAdd(&mc->measIdToAddModList->list, measid_A2);
  }

  // MeasId for Event A3
  if (neigh_seq) {
    int i = 0;
    FOR_EACH_SEQ_ARR(nr_neighbour_cell_t *, neigh_cell, neigh_seq) {
      NR_ReportConfigId_t reportConfigId = neigh_a3_id[i];
      /* check that there is a A3 configured for this neighbour */
      if (reportConfigId == -1) {
        i++;
        continue;
      }
      int meas_idx = allocate_measurement_id(ue);
      int meas_obj_id = neighbour_measurement_object_id[i];
      NR_MeasIdToAddMod_t *measid_A3 = get_MeasId(meas_idx, reportConfigId, meas_obj_id);
      asn1cSeqAdd(&mc->measIdToAddModList->list, measid_A3);
      i++;
    }
  }

  // Quantity configurations: The quantity configuration defines the measurement filtering configuration used for measurement event
  // evaluation and related reporting, and for periodical reporting of that measurement.
  mc->quantityConfig = calloc_or_fail(1, sizeof(*mc->quantityConfig));
  mc->quantityConfig->quantityConfigNR_List = calloc_or_fail(1, sizeof(*mc->quantityConfig->quantityConfigNR_List));
  NR_QuantityConfigNR_t *qcnr = calloc_or_fail(1, sizeof(*qcnr));
  asn1cCallocOne(qcnr->quantityConfigCell.ssb_FilterConfig.filterCoefficientRSRP, NR_FilterCoefficient_fc6);
  asn1cCallocOne(qcnr->quantityConfigCell.csi_RS_FilterConfig.filterCoefficientRSRP, NR_FilterCoefficient_fc6);
  asn1cSeqAdd(&mc->quantityConfig->quantityConfigNR_List->list, qcnr);

  free(neighbour_measurement_object_id);

  return mc;
}

/** @brief Prepares removal of measurement configurations related to the source gNB.
 * This function decodes the HandoverPreparationInformation received from the source gNB,
 * extracts the source MeasConfig, and marks those entries (measurement objects, report
 * configurations, and measurement IDs) for removal by populating the corresponding
 * ToRemoveList fields in the UE's current measConfig.
 * This ensures that obsolete or source-specific measurement configurations are removed
 * from the UE after handover and allows the target gNB to install its own valid configuration.
 * @param measConfig Target gNB's current measurement configuration to be updated.
 * @param prep_info Encoded HandoverPreparationInformation received from the source gNB.*/
void fill_removal_lists_from_source_measConfig(NR_MeasConfig_t *currentMC, byte_array_t prep_info)
{
  if (currentMC == NULL) {
    LOG_E(NR_RRC, "HO LOG: UE's Measurement Configuration is NULL!\n");
    return;
  }

  // Decodes the HandoverPreparationInformation message
  NR_HandoverPreparationInformation_t *hpi = NULL;
  asn_dec_rval_t hoPrep_dec_rval = uper_decode_complete(NULL,
                                                        &asn_DEF_NR_HandoverPreparationInformation,
                                                        (void **)&hpi,
                                                        (uint8_t *)prep_info.buf,
                                                        prep_info.len);

  if (hoPrep_dec_rval.code != RC_OK || hoPrep_dec_rval.consumed < 0) {
    LOG_E(NR_RRC, "Handover Prep Info decode error while removing source gnb measurement configuration!\n");
    return;
  }

  /* Decodes the RRCReconfiguration message within the HandoverPreparationInformation
     and extract the measConfig provided by the source gNB */
  if (!hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation->sourceConfig) {
    LOG_W(NR_RRC, "Missing sourceConfig: in source gNB rrcReconfiguration\n");
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
    return;
  }
  NR_RRCReconfiguration_t *rrcReconf = NULL;
  NR_AS_Config_t *sourceConfig = hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation->sourceConfig;
  asn_dec_rval_t rrcReconf_dec_rval = uper_decode_complete(NULL,
                                                           &asn_DEF_NR_RRCReconfiguration,
                                                           (void **)&rrcReconf,
                                                           (uint8_t *)sourceConfig->rrcReconfiguration.buf,
                                                           sourceConfig->rrcReconfiguration.size);
  if (rrcReconf_dec_rval.code != RC_OK || rrcReconf_dec_rval.consumed < 0) {
    LOG_E(NR_RRC, "Failed to decode source gNB rrcReconfiguration!\n");
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
    return;
  }

  if (rrcReconf->criticalExtensions.choice.rrcReconfiguration->measConfig == NULL) {
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
    ASN_STRUCT_FREE(asn_DEF_NR_RRCReconfiguration, rrcReconf);
    return;
  }

  NR_MeasConfig_t *sourceMC = rrcReconf->criticalExtensions.choice.rrcReconfiguration->measConfig;

  /* Add measurement objects, report configurations, and measurement IDs
     to the UE's measConfig removal lists, and update UE->measConfig */
  if (sourceMC->measObjectToAddModList->list.count > 0) {
    currentMC->measObjectToRemoveList = calloc_or_fail(1, sizeof(*currentMC->measObjectToRemoveList));
    for (int i = 0; i < sourceMC->measObjectToAddModList->list.count; i++) {
      NR_MeasObjectId_t *measObjId = calloc_or_fail(1, sizeof(NR_MeasObjectId_t));
      *measObjId = sourceMC->measObjectToAddModList->list.array[i]->measObjectId;
      asn1cSeqAdd(&currentMC->measObjectToRemoveList->list, measObjId);
    }
  }

  if (sourceMC->reportConfigToAddModList->list.count > 0) {
    currentMC->reportConfigToRemoveList = calloc_or_fail(1, sizeof(*currentMC->reportConfigToRemoveList));
    for (int i = 0; i < sourceMC->reportConfigToAddModList->list.count; i++) {
      NR_ReportConfigId_t *reportConfigId = calloc_or_fail(1, sizeof(NR_ReportConfigId_t));
      *reportConfigId = sourceMC->reportConfigToAddModList->list.array[i]->reportConfigId;
      asn1cSeqAdd(&currentMC->reportConfigToRemoveList->list, reportConfigId);
    }
  }

  if (sourceMC->measIdToAddModList->list.count > 0) {
    currentMC->measIdToRemoveList = calloc_or_fail(1, sizeof(*currentMC->measIdToRemoveList));
    for (int i = 0; i < sourceMC->measIdToAddModList->list.count; i++) {
      NR_MeasId_t *measId = calloc_or_fail(1, sizeof(NR_MeasId_t));
      *measId = sourceMC->measIdToAddModList->list.array[i]->measId;
      asn1cSeqAdd(&currentMC->measIdToRemoveList->list, measId);
    }
  }

  // Clean up allocated memory
  ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
  ASN_STRUCT_FREE(asn_DEF_NR_RRCReconfiguration, rrcReconf);
}

/** @brief Extract and re-encode the UE Capability RAT Container List embedded in a received
 *  HandoverPreparationInformation, in the standalone encoded form get_HandoverPreparationInformation()
 *  expects back out of ue_cap_buffer for a subsequent handover -- carries UE capabilities across an
 *  inter-CU handover so they survive to be re-sent if this UE is handed over again later. */
byte_array_t extract_ue_cap_from_HandoverPreparationInformation(byte_array_t prep_info)
{
  byte_array_t out = {.buf = NULL, .len = 0};

  NR_HandoverPreparationInformation_t *hpi = NULL;
  asn_dec_rval_t dec_rval =
      uper_decode_complete(NULL, &asn_DEF_NR_HandoverPreparationInformation, (void **)&hpi, (uint8_t *)prep_info.buf, prep_info.len);
  if (dec_rval.code != RC_OK || !hpi) {
    LOG_E(NR_RRC, "Failed to decode HandoverPreparationInformation to extract UE capabilities\n");
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
    return out;
  }

  /* the HandoverPreparationInformation comes from the source gNB: check its structure */
  if (hpi->criticalExtensions.present != NR_HandoverPreparationInformation__criticalExtensions_PR_c1
      || !hpi->criticalExtensions.choice.c1
      || hpi->criticalExtensions.choice.c1->present != NR_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation
      || !hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation) {
    LOG_E(NR_RRC, "Invalid HandoverPreparationInformation in criticalExtensions.choice.c1, no UE capabilities\n");
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
    return out;
  }

  NR_UE_CapabilityRAT_ContainerList_t *clist = &hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation->ue_CapabilityRAT_List;
  out.len = uper_encode_to_new_buffer(&asn_DEF_NR_UE_CapabilityRAT_ContainerList, NULL, (void *)clist, (void **)&out.buf);
  if (out.len <= 0) {
    LOG_E(NR_RRC, "Failed to re-encode UE Capability RAT Container List from HandoverPreparationInformation\n");
    out = (byte_array_t){.buf = NULL, .len = 0};
  }

  ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
  return out;
}

byte_array_t doRRCReconfiguration_from_HandoverCommand(const byte_array_t handoverCommand)
{
  DevAssert(handoverCommand.buf);
  DevAssert(handoverCommand.len > 0);
  byte_array_t msg = {.buf = NULL, .len = 0};

  // Decode Handover Command
  NR_HandoverCommand_t *hoCommand = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_HandoverCommand, (void **)&hoCommand, handoverCommand.buf, handoverCommand.len);

  if (dec_rval.code != RC_OK || !hoCommand) {
    LOG_E(NR_RRC, "Failed to decode Handover Command (dec_rval.code=%d)!\n", dec_rval.code);
    return msg;
  }

  // Validate HandoverCommand structure
  if (hoCommand->criticalExtensions.present != NR_HandoverCommand__criticalExtensions_PR_c1
      || !hoCommand->criticalExtensions.choice.c1
      || hoCommand->criticalExtensions.choice.c1->present != NR_HandoverCommand__criticalExtensions__c1_PR_handoverCommand
      || !hoCommand->criticalExtensions.choice.c1->choice.handoverCommand) {
    LOG_E(NR_RRC, "Invalid HandoverCommand in criticalExtensions.choice.c1->choice.handoverCommand\n");
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverCommand, hoCommand);
    return msg;
  }

  // Prepare DL DCCH message for RRCReconfiguration
  NR_DL_DCCH_Message_t dl_dcch_msg = {0};
  dl_dcch_msg.message.present = NR_DL_DCCH_MessageType_PR_c1;
  asn1cCalloc(dl_dcch_msg.message.choice.c1, c1);
  c1->present = NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;
  asn1cCalloc(c1->choice.rrcReconfiguration, rrcReconf);

  // Decode RRCReconfiguration from handoverCommandMessage
  OCTET_STRING_t *ho = &hoCommand->criticalExtensions.choice.c1->choice.handoverCommand->handoverCommandMessage;
  if (!ho->buf || ho->size == 0) {
    LOG_E(NR_RRC, "Invalid handoverCommandMessage OCTET_STRING (buf=%p, size=%zu)!\n", ho->buf, ho->size);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverCommand, hoCommand);
    return msg;
  }
  asn_dec_rval_t rrc_dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_RRCReconfiguration, (void **)&rrcReconf, ho->buf, ho->size);
  if (rrc_dec_rval.code != RC_OK || !rrcReconf) {
    LOG_E(NR_RRC, "Failed to decode RRCReconfiguration from Handover Command!\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverCommand, hoCommand);
    return msg;
  }
  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)&dl_dcch_msg);

  // Encode DL DCCH message to new buffer
  int val = uper_encode_to_new_buffer(&asn_DEF_NR_DL_DCCH_Message, NULL, &dl_dcch_msg, (void **)&msg.buf);
  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode RRCReconfiguration from Handover Command!\n");
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
    ASN_STRUCT_FREE(asn_DEF_NR_HandoverCommand, hoCommand);
    return msg;
  }
  msg.len = val;
  LOG_D(NR_RRC, "RRCReconfiguration from HandoverCommand: Encoded (%ld bytes)\n", msg.len);

  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_DL_DCCH_Message, &dl_dcch_msg);
  ASN_STRUCT_FREE(asn_DEF_NR_HandoverCommand, hoCommand);

  return msg;
}

byte_array_t get_HandoverCommandMessage(nr_rrc_reconfig_param_t *params)
{
  // Encode RRCReconfiguration to buffer
  byte_array_t msg = do_HO_RRCReconfiguration(params);

  // Add RRCReconfiguration to handoverCommand
  NR_HandoverCommand_t HoCommand = {0};
  HoCommand.criticalExtensions.present = NR_HandoverCommand__criticalExtensions_PR_c1;
  asn1cCalloc(HoCommand.criticalExtensions.choice.c1, c1);
  c1->present = NR_HandoverCommand__criticalExtensions__c1_PR_handoverCommand;
  asn1cCalloc(HoCommand.criticalExtensions.choice.c1->choice.handoverCommand, hc);
  OCTET_STRING_fromBuf(&hc->handoverCommandMessage, (const char *)msg.buf, msg.len);
  free_byte_array(msg);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_HandoverCommand, (void *)&HoCommand);

  // Encode handoverCommand to buffer
  byte_array_t buffer = {.len = -1, .buf = NULL};
  ssize_t encoded = uper_encode_to_new_buffer(&asn_DEF_NR_HandoverCommand, NULL, (void *)&HoCommand, (void **)&buffer.buf);
  AssertFatal(encoded > 0,"ASN1 message encoding failed HandoverCommand\n");
  buffer.len = encoded;
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_HandoverCommand, &HoCommand);

  return buffer;
}

/** @brief HandoverPreparationInformation message (11.2.2 of TS 38.331)
 *         1) generate RRCReconfiguration to be transferred
 *         2) encodes UE Capabilities from UE Context
 *         3) encodes AS context
 *         4) generates HO Preparation Info */
byte_array_t get_HandoverPreparationInformation(nr_rrc_reconfig_param_t *params)
{
  // Buffer to return
  byte_array_t buffer = {.buf = NULL, .len = 0};

  /* Prepare handoverPreparationInformation IEs */
  NR_HandoverPreparationInformation_t hoPrepInfo = {0};
  hoPrepInfo.criticalExtensions.present = NR_HandoverPreparationInformation__criticalExtensions_PR_c1;
  asn1cCalloc(hoPrepInfo.criticalExtensions.choice.c1, c1);
  c1->present = NR_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation;
  asn1cCalloc(c1->choice.handoverPreparationInformation, hpi);

  /* Decode stored NR_UE_CapabilityRAT_ContainerList and add to IEs */
  NR_UE_CapabilityRAT_ContainerList_t *clist = NULL;
  asn_dec_rval_t dec_rval =
      uper_decode(NULL, &asn_DEF_NR_UE_CapabilityRAT_ContainerList, (void **)&clist, params->ue_cap.buf, params->ue_cap.len, 0, 0);
  if (dec_rval.code != RC_OK) {
    LOG_W(NR_RRC, "Failed to decode UE capability container list for HandoverPreparationInformation, ignoring capabilities\n");
    return buffer;
  }
  hpi->ue_CapabilityRAT_List = *clist;
  free(clist);

  /* sourceConfig: Encode RRCReconfiguration as used in the source cell */
  byte_array_t msg = do_HO_RRCReconfiguration(params);
  asn1cCalloc(hpi->sourceConfig, sourceConfig);
  OCTET_STRING_fromBuf(&sourceConfig->rrcReconfiguration, (const char *)msg.buf, msg.len);
  free_byte_array(msg);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_HandoverPreparationInformation, (void **)&hoPrepInfo);

  /* encode */
  ssize_t encoded = uper_encode_to_new_buffer(&asn_DEF_NR_HandoverPreparationInformation, NULL, (void *)&hoPrepInfo, (void *)&buffer.buf);
  AssertFatal(encoded > 0, "ASN1 message encoding failed (%ld)!\n", encoded);
  buffer.len = encoded;
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_HandoverPreparationInformation, &hoPrepInfo);
  return buffer;
}

void free_MeasConfig(NR_MeasConfig_t *mc)
{
  ASN_STRUCT_FREE(asn_DEF_NR_MeasConfig, mc);
}

/** @brief Generate NR RRC Paging message (TS 38.331 §5.3.2)
 * Creates a PCCH message with a single PagingRecord containing ng-5G-S-TMSI
 * for CN-initiated paging of RRC_IDLE UEs. */
byte_array_t do_NR_Paging(int count, const nr_paging_params_t *params)
{
  DevAssert(count > 0 && params != NULL);
  DevAssert(count <= NR_PCCH_MAX_PAGING_RECORDS);
  LOG_D(NR_RRC, "Generate NR RRC Paging message (count=%d, ue_identity_type=%d)\n", count, params->ue_identity_type);
  byte_array_t msg = {.buf = NULL, .len = 0};

  NR_PCCH_Message_t pcch_msg = {0};
  pcch_msg.message.present = NR_PCCH_MessageType_PR_c1;
  asn1cCalloc(pcch_msg.message.choice.c1, c1);
  c1->present = NR_PCCH_MessageType__c1_PR_paging;
  asn1cCalloc(c1->choice.paging, paging);

  /* pagingRecordList (Optional)
   * Network may address multiple UEs by including one PagingRecord per UE.
   * If pagingRecordList-v1700 is included, it has same entries in same order. */
  asn1cCalloc(paging->pagingRecordList, pagingRecordList);
  for (int i = 0; i < count; i++) {
    asn1cSequenceAdd(pagingRecordList->list, NR_PagingRecord_t, paging_record_p);
    const nr_paging_params_t *p = &params[i];

    /* PagingUE-Identity (choice) { ng-5G-S-TMSI | fullI-RNTI } */
    paging_record_p->ue_Identity.present = p->ue_identity_type;
    if (p->ue_identity_type == NR_PagingUE_Identity_PR_ng_5G_S_TMSI) {
      /* ng-5G-S-TMSI: BIT STRING (SIZE(48)) - AMF Set ID, AMF Pointer, 5G-TMSI (TS 38.331 / 23.003) */
      BIT_STRING_t *tmsi_bs = &paging_record_p->ue_Identity.choice.ng_5G_S_TMSI;
      FIVEG_S_TMSI_TO_BIT_STRING(p->ue_identity.fiveg_s_tmsi, tmsi_bs);
    } else {
      /* fullI-RNTI: I-RNTI-Value BIT STRING (SIZE(40)) for RAN-initiated paging (RRC_INACTIVE). */
      BIT_STRING_t *i_rnti_bs = &paging_record_p->ue_Identity.choice.fullI_RNTI;
      i_rnti_bs->size = NR_PAGING_FULL_I_RNTI_SIZE;
      i_rnti_bs->buf = calloc_or_fail(NR_PAGING_FULL_I_RNTI_SIZE, sizeof(uint8_t));
      i_rnti_bs->bits_unused = 0;
      memcpy(i_rnti_bs->buf, p->ue_identity.full_i_rnti, NR_PAGING_FULL_I_RNTI_SIZE);
    }

    /* accessType (optional): indicates whether paging is due to PDU sessions from non-3GPP access */
    if (p->access_type) {
      paging_record_p->accessType = calloc_or_fail(1, sizeof(long));
      *paging_record_p->accessType = NR_PagingRecord__accessType_non3GPP;
    }
  }

  /** If pagingRecordList-v1700 is included: same count/order as pagingRecordList (38.331).
   * pagingCause-r17 present means IMS voice, absent in v1700 entry means non-voice (UE-dependent). */
  int i = 0;
  while (i < count && params[i].paging_cause == NULL)
    i++;
  if (i < count) {
    asn1cCalloc(paging->nonCriticalExtension, ext_v1700);
    ext_v1700->pagingGroupList_r17 = NULL;
    ext_v1700->nonCriticalExtension = NULL;
    asn1cCalloc(ext_v1700->pagingRecordList_v1700, list_v1700);
    for (i = 0; i < count; i++) {
      asn1cSequenceAdd(list_v1700->list, NR_PagingRecord_v1700_t, rec_v1700);
      if (params[i].paging_cause != NULL) {
        rec_v1700->pagingCause_r17 = calloc_or_fail(1, sizeof(long));
        *rec_v1700->pagingCause_r17 = NR_PagingRecord_v1700__pagingCause_r17_voice;
      }
    }
  }

  LOG_D(NR_RRC, "Paging: PagingRecordList.count=%d\n", pagingRecordList->list.count);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_PCCH_Message, (void *)&pcch_msg);
  }

  int val = uper_encode_to_new_buffer(&asn_DEF_NR_PCCH_Message, NULL, &pcch_msg, (void **)&msg.buf);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_PCCH_Message, &pcch_msg);

  if (val <= 0) {
    LOG_E(NR_RRC, "Failed to encode NR RRC Paging message\n");
    free_byte_array(msg);
    return msg;
  }

  msg.len = val;
  LOG_I(NR_RRC, "Encoded NR RRC Paging message (%zu bytes)\n", msg.len);

  return msg;
}

/** @brief Decode an NR PCCH message into nr_paging_params_t records.
 * @param pcch PCCH SDU as byte_array_t (buffer + length)
 * @param out_params output array of decoded paging records
 * @param out_count on success, number of records written to out_params
 * @return -1 on decode/validation error, 0 on success */
int nr_pcch_decode(const byte_array_t pcch, nr_paging_params_t *out_params, int *out_count)
{
  *out_count = 0;
  NR_PCCH_Message_t *pcch_msg = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_PCCH_Message, (void **)&pcch_msg, pcch.buf, pcch.len);

  if ((dec_rval.code != RC_OK) || (dec_rval.consumed == 0)) {
    LOG_E(NR_RRC,
          "Failed to decode PCCH message (%zu bytes, dec_rval.code=%d, dec_rval.consumed=%zu)\n",
          pcch.len,
          dec_rval.code,
          dec_rval.consumed);
    ASN_STRUCT_FREE(asn_DEF_NR_PCCH_Message, pcch_msg);
    return -1;
  }

  if (pcch_msg->message.present != NR_PCCH_MessageType_PR_c1) {
    LOG_E(NR_RRC, "PCCH message (%zu bytes) is not a paging message\n", pcch.len);
    ASN_STRUCT_FREE(asn_DEF_NR_PCCH_Message, pcch_msg);
    return -1;
  }

  if (pcch_msg->message.choice.c1->present != NR_PCCH_MessageType__c1_PR_paging) {
    LOG_E(NR_RRC, "PCCH message (%zu bytes) is not a paging message\n", pcch.len);
    ASN_STRUCT_FREE(asn_DEF_NR_PCCH_Message, pcch_msg);
    return -1;
  }

  NR_Paging_t *paging = pcch_msg->message.choice.c1->choice.paging;
  if (paging->pagingRecordList == NULL || paging->pagingRecordList->list.count == 0) {
    LOG_E(NR_RRC, "PCCH message (%zu bytes) has no paging records\n", pcch.len);
    ASN_STRUCT_FREE(asn_DEF_NR_PCCH_Message, pcch_msg);
    return -1;
  }

  int n = 0;
  for (int i = 0; i < paging->pagingRecordList->list.count && n < NR_PCCH_MAX_PAGING_RECORDS; i++) {
    NR_PagingRecord_t *record = paging->pagingRecordList->list.array[i];
    nr_paging_params_t *p = &out_params[n];
    *p = (nr_paging_params_t){0};
    p->ue_identity_type = record->ue_Identity.present;
    p->access_type = (record->accessType != NULL && *record->accessType == NR_PagingRecord__accessType_non3GPP);
    if (record->ue_Identity.present == NR_PagingUE_Identity_PR_ng_5G_S_TMSI) {
      const BIT_STRING_t *ng_5g_s_tmsi = &record->ue_Identity.choice.ng_5G_S_TMSI;
      DevAssert(ng_5g_s_tmsi->size >= 6);
      p->ue_identity.fiveg_s_tmsi = BIT_STRING_to_uint64(ng_5g_s_tmsi) & ((1ULL << 48) - 1);
    } else if (record->ue_Identity.present == NR_PagingUE_Identity_PR_fullI_RNTI) {
      BIT_STRING_t *i_rnti = &record->ue_Identity.choice.fullI_RNTI;
      DevAssert(i_rnti->size == NR_PAGING_FULL_I_RNTI_SIZE);
      memcpy(p->ue_identity.full_i_rnti, i_rnti->buf, NR_PAGING_FULL_I_RNTI_SIZE);
    }
    n++;
  }
  *out_count = n;
  ASN_STRUCT_FREE(asn_DEF_NR_PCCH_Message, pcch_msg);
  return 0;
}

int do_NR_MeasConfig(const NR_MeasConfig_t *measconfig, uint8_t *buf, int buf_size)
{
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_MeasConfig, NULL, measconfig, buf, buf_size);
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  return (enc_rval.encoded + 7) / 8;
}

int do_NR_MeasurementTimingConfiguration(const NR_MeasurementTimingConfiguration_t *mtc, uint8_t *buf, int buf_size)
{
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_MeasurementTimingConfiguration, NULL, mtc, buf, buf_size);
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  return (enc_rval.encoded + 7) / 8;
}
