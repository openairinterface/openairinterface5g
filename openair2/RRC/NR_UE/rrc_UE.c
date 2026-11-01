/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief RRC procedures
 */

#define RRC_UE
#define RRC_UE_C

#include "LTE_MeasObjectToAddMod.h"
#include "NR_DL-DCCH-Message.h"        //asn_DEF_NR_DL_DCCH_Message
#include "NR_DL-CCCH-Message.h"        //asn_DEF_NR_DL_CCCH_Message
#include "NR_BCCH-BCH-Message.h"       //asn_DEF_NR_BCCH_BCH_Message
#include "NR_BCCH-DL-SCH-Message.h"    //asn_DEF_NR_BCCH_DL_SCH_Message
#include "NR_PCCH-Message.h" //asn_DEF_NR_PCCH_Message
#include "NR_CellGroupConfig.h"        //asn_DEF_NR_CellGroupConfig
#include "NR_BWP-Downlink.h"           //asn_DEF_NR_BWP_Downlink
#include "NR_RRCReconfiguration.h"
#include "NR_MeasConfig.h"
#include "NR_UL-DCCH-Message.h"
#include "uper_encoder.h"
#include "uper_decoder.h"

#include "rrc_defs.h"
#include "rrc_proto.h"
#include "verify_RRC.h"
#include "L2_interface_ue.h"
#include "LAYER2/NR_MAC_UE/mac_proto.h"

#include "intertask_interface.h"

#include "LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "nr-uesoftmodem.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "openair3/SECU/secu_defs.h"
#include "openair3/SECU/key_nas_deriver.h"

#include "common/utils/LOG/log.h"
#include "conversions.h"
#include "common/utils/ds/byte_array.h"

#ifndef CELLULAR
  #include "RRC/NR/MESSAGES/asn1_msg.h"
#endif

#include "SIMULATION/TOOLS/sim.h" // for taus

#include "nr_nas_msg.h"
#include "openair2/SDAP/nr_sdap/nr_sdap.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_entity.h"

static NR_UE_RRC_INST_t *NR_UE_rrc_inst[MAX_NUM_NR_UE_INST] = {0};
/* NAS Attach request with IMSI */
static const char nr_nas_attach_req_imsi_dummy_NSA_case[] = {
    0x07,
    0x41,
    /* EPS Mobile identity = IMSI */
    0x71,
    0x08,
    0x29,
    0x80,
    0x43,
    0x21,
    0x43,
    0x65,
    0x87,
    0xF9,
    /* End of EPS Mobile Identity */
    0x02,
    0xE0,
    0xE0,
    0x00,
    0x20,
    0x02,
    0x03,
    0xD0,
    0x11,
    0x27,
    0x1A,
    0x80,
    0x80,
    0x21,
    0x10,
    0x01,
    0x00,
    0x00,
    0x10,
    0x81,
    0x06,
    0x00,
    0x00,
    0x00,
    0x00,
    0x83,
    0x06,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x00,
    0x00,
    0x0A,
    0x00,
    0x52,
    0x12,
    0xF2,
    0x01,
    0x27,
    0x11,
};

/**
 * @brief Sends an RRC message to the connected UE MAC instance.
 *
 * @param rrc UE RRC instance structure
 * @param msg RRC message to be sent to MAC
 */
static void nr_rrc_send_msg_to_mac(NR_UE_RRC_INST_t *rrc, nr_mac_rrc_message_t *msg)
{
  AssertFatal(rrc->mac_input_nf != NULL, "MAC input NF is NULL for UE %ld\n", rrc->ue_id);
  notifiedFIFO_elt_t *nf_msg = newNotifiedFIFO_elt(sizeof(nr_mac_rrc_message_t), 0, NULL, NULL);
  nr_mac_rrc_message_t *rrc_msg = NotifiedFifoData(nf_msg);
  memcpy(rrc_msg, msg, sizeof(nr_mac_rrc_message_t));
  pushNotifiedFIFO(rrc->mac_input_nf, nf_msg);
}

/** @brief Ask MAC to start or restart random access
 * @param rrc   UE RRC instance
 * @param cause Why RA is started (setup, T300, post-SIB, re-establishment) */
static void nr_rrc_trigger_mac_ra(NR_UE_RRC_INST_t *rrc, nr_mac_ra_start_cause_t cause)
{
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_START_RA;
  rrc_msg.payload.start_ra.cause = cause;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
}

NR_UE_RRC_INST_t *get_NR_UE_rrc_inst(int instance)
{
  AssertFatal(instance >= 0 && instance < MAX_NUM_NR_UE_INST, "RRC instance %d out of bounds\n", instance);
  NR_UE_RRC_INST_t *rrc = NR_UE_rrc_inst[instance];
  if (rrc == NULL)
    return NULL;
  AssertFatal(rrc->ue_id == instance, "RRC ID %d doesn't match with input %d\n", (int)rrc->ue_id, instance);
  return rrc;
}

static NR_RB_status_t get_DRB_status(const NR_UE_RRC_INST_t *rrc, NR_DRB_Identity_t drb_id)
{
  AssertFatal(drb_id > 0 && drb_id < 33, "Invalid DRB ID %ld\n", drb_id);
  return rrc->status_DRBs[drb_id - 1];
}

static void set_DRB_status(NR_UE_RRC_INST_t *rrc, NR_DRB_Identity_t drb_id, NR_RB_status_t status)
{
  AssertFatal(drb_id > 0 && drb_id < 33, "Invalid DRB ID %ld\n", drb_id);
  rrc->status_DRBs[drb_id - 1] = status;
}

static int get_ulsyncvalidityduration_timer_value(NR_NTN_Config_r17_t *ntncfg)
{
  int retval = 0;
  AssertFatal(ntncfg, "NTN-Config IE not present\n");

  if (ntncfg->ntn_UlSyncValidityDuration_r17) {
    const int values[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 120, 180, 240, 900};
    retval = values[*ntncfg->ntn_UlSyncValidityDuration_r17];
  }

  return retval;
}

static void get_epochtime_from_sib19scheduling(NR_UE_RRC_SI_INFO *SI_info, int *frame, int *subframe)
{
  // TS 38.331 section 5.2.2.3.2
  // SI-window starts at the slot #a, where a = x mod N
  // x = (si-WindowPosition -1) × w, where w is the si-WindowLength;
  // N is the number of slots in a radio frame
  int wl_slots = 5 << SI_info->si_windowlength;
  int x = (SI_info->sib19_windowposition - 1) * wl_slots;
  int slots_per_subframe = 1 << SI_info->scs;
  int N = slots_per_subframe * 10;
  int slot_end_sib19_window = (x % N) + wl_slots;

  *frame += (slot_end_sib19_window / N);
  int slot = slot_end_sib19_window % N;
  *subframe = ceil(slot / slots_per_subframe);

  LOG_I(NR_RRC,
        "Get EPOCHTIME: x:%d, N:%d, slot_endw:%d, frame:%d, subframe:%d , slot:%d\n",
        x,
        N,
        slot_end_sib19_window,
        *frame,
        *subframe,
        slot);
}

static int eval_epoch_time(NR_UE_RRC_SI_INFO *SI_info, NR_NTN_Config_r17_t *ntncfg, int frame, bool is_targetcell)
{
  int epoch_frame = 0, epoch_subframe = 0;
  int diff_frames = 0;
  AssertFatal(frame >= 0, "Frame Incorrect, results in incorrect EPOCH time evaluation\n");
  if (ntncfg->epochTime_r17) {
    epoch_frame = ntncfg->epochTime_r17->sfn_r17;
    epoch_subframe = ntncfg->epochTime_r17->subFrameNR_r17;
  } else {
    // EPOCH time is optional in case of SIB19. This case happens only in case of SIB19 NTN config
    // If no EPOCH time is sent, epochtime points to SIB19 window end slot in the current scheduling window
    epoch_frame = frame;
    epoch_subframe = 0;
    get_epochtime_from_sib19scheduling(SI_info, &epoch_frame, &epoch_subframe);
    // Adding epochTime IE in SIB19, later MAC will use it.
    ntncfg->epochTime_r17 = CALLOC(1, sizeof(NR_EpochTime_r17_t));
    ntncfg->epochTime_r17->sfn_r17 = epoch_frame;
    ntncfg->epochTime_r17->subFrameNR_r17 = epoch_subframe;
  }
  if (is_targetcell) {
    // For Target cell,the SFN nearest to the frame needs to be considered
    // i.e. Epochframe can be in the past or future
    int diff1 = (frame - epoch_frame + 1024) % 1024;
    int diff2 = (epoch_frame - frame + 1024) % 1024;
    diff_frames = (diff1 < diff2) ? -diff1 : diff2;
  } else {
    // For serving cell, the field sfn indicates the current SFN or the next upcoming SFN
    // after the frame where the message indicating the epochTime is received
    // i.e. Epochframe can be present or future SFN
    diff_frames = (epoch_frame - frame + 1024) % 1024;    // According to 38.331 Epochtime is defined for serving cell like this
  }
  LOG_I(NR_RRC, "Epoch frame %d ahead by %d frames\n", epoch_frame, diff_frames);
  return diff_frames;
}

static int get_ntn_timervalues(NR_UE_RRC_SI_INFO *SI_info, NR_NTN_Config_r17_t *ntncfg, int diff_frames, int *val430_ms)
{
  int val430 = get_ulsyncvalidityduration_timer_value(ntncfg);
  int sib19_periodicity_ms = SI_info->sib19_periodicity * 10;
  *val430_ms = val430 * 1000 + diff_frames * 10; // in ms
  if (*val430_ms <= sib19_periodicity_ms)
    LOG_E(NR_RRC, "Too small T430 value. Might result in frequent ULSYNC failure\n");

  // by default SIB19 reception is started from the middle of the ulsyncvalidity duration.(i.e val430 in ms / 2)
  int sib19_timer_ms = val430 * 500 + diff_frames * 10;

  // if this is less than the SIB19 periodicity, use that instead.
  // set the timer to expire 1 frame (10 ms) before periodicity, to not miss the SIB19
  if (sib19_timer_ms <= sib19_periodicity_ms)
    sib19_timer_ms = sib19_periodicity_ms - 10;

  LOG_I(NR_RRC, "val430:%d s, T430:%d ms, sib19_timer:%d ms\n", val430, *val430_ms, sib19_timer_ms);
  return sib19_timer_ms;
}

static void nr_rrc_process_ntnconfig(NR_UE_RRC_INST_t *rrc, NR_UE_RRC_SI_INFO *SI_info, NR_NTN_Config_r17_t *ntncfg, int frame, bool is_targetcell)
{
  SI_info->SInfo_r17.sib19_validity = true;
  // Check if Epochtime is sent or not
  int diff_frames = eval_epoch_time(SI_info, ntncfg, frame, is_targetcell);

  if (ntncfg->ntn_UlSyncValidityDuration_r17) { // ulsyncvalidity duration configured
    int val430_ms = 0, sib19_timer_ms = 0;
    sib19_timer_ms = get_ntn_timervalues(SI_info, ntncfg, diff_frames, &val430_ms);
    // T430 should be started only in connected mode.
    // Inorder to avoid starting T430 when entering connected mode, T430 is started as soon as
    // SIB19 is received, and if UE enters connected mode T430 will be in running.
    // T430 expiry in RRC idle or inactive states does nothing.
    nr_timer_setup(&rrc->timers_and_constants.T430, val430_ms, 10);
    nr_timer_start(&rrc->timers_and_constants.T430);
    // SIB19 should be received before T430 expires
    // SIB19 validity timer should expire before T430 expiry such that new SIB19 is read
    if (sib19_timer_ms > 0) {
      nr_timer_setup(&SI_info->SInfo_r17.sib19_timer, sib19_timer_ms, 10);
      nr_timer_start(&SI_info->SInfo_r17.sib19_timer);
    } else
      // This makes sure that SIB19 is read again in the next window
      SI_info->SInfo_r17.sib19_validity = false;
  } else
    nr_timer_start(&SI_info->SInfo_r17.sib19_timer);
}

static void nr_decode_SI(NR_UE_RRC_SI_INFO *SI_info, NR_SystemInformation_t *si, NR_UE_RRC_INST_t *rrc, int hfn, int frame)
{
  if (si->criticalExtensions.present != NR_SystemInformation__criticalExtensions_PR_systemInformation) {
    LOG_W(NR_RRC, "Not implemented SystemInformation criticalExtension version\n");
    return;
  }

  NR_SIB19_r17_t *sib19 = NULL;
  for (int i = 0; i < si->criticalExtensions.choice.systemInformation->sib_TypeAndInfo.list.count; i++) {
    SystemInformation_IEs__sib_TypeAndInfo__Member *typeandinfo;
    typeandinfo = si->criticalExtensions.choice.systemInformation->sib_TypeAndInfo.list.array[i];
    LOG_A(NR_RRC, "Found SIB%d\n", typeandinfo->present + 1);
    switch(typeandinfo->present) {
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib2:
        SI_info->sib2_validity = true;
        nr_timer_start(&SI_info->sib2_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib3:
        SI_info->sib3_validity = true;
        nr_timer_start(&SI_info->sib3_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib4:
        SI_info->sib4_validity = true;
        nr_timer_start(&SI_info->sib4_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib5:
        SI_info->sib5_validity = true;
        nr_timer_start(&SI_info->sib5_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib6:
        SI_info->sib6_validity = true;
        nr_timer_start(&SI_info->sib6_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib7:
        SI_info->sib7_validity = true;
        nr_timer_start(&SI_info->sib7_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib8:
        SI_info->sib8_validity = true;
        nr_timer_start(&SI_info->sib8_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib9:
        SI_info->sib9_validity = true;
        nr_timer_start(&SI_info->sib9_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib10_v1610:
        SI_info->sib10_validity = true;
        nr_timer_start(&SI_info->sib10_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib11_v1610:
        SI_info->sib11_validity = true;
        nr_timer_start(&SI_info->sib11_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib12_v1610:
        SI_info->sib12_validity = true;
        nr_timer_start(&SI_info->sib12_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib13_v1610:
        SI_info->sib13_validity = true;
        nr_timer_start(&SI_info->sib13_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib14_v1610:
        SI_info->sib14_validity = true;
        nr_timer_start(&SI_info->sib14_timer);
        break;
      case NR_SystemInformation_IEs__sib_TypeAndInfo__Member_PR_sib19_v1700:
        sib19 = typeandinfo->choice.sib19_v1700;
        if (g_log->log_component[NR_RRC].level >= OAILOG_DEBUG)
          xer_fprint(stdout, &asn_DEF_NR_SIB19_r17, (const void *)sib19);
        nr_rrc_process_ntnconfig(rrc, SI_info, sib19->ntn_Config_r17, frame, false);
        break;
      default:
        break;
    }
  }

  if (sib19) {
    nr_mac_rrc_message_t rrc_msg = {0};
    rrc_msg.payload_type = NR_MAC_RRC_CONFIG_OTHER_SIB;
    nr_mac_rrc_config_other_sib_t *sib19_msg = &rrc_msg.payload.config_other_sib;
    asn_copy(&asn_DEF_NR_SIB19_r17, (void **)&sib19_msg->sib19, sib19);
    sib19_msg->hfn = hfn;
    sib19_msg->frame = frame;
    sib19_msg->can_start_ra = rrc->is_NTN_UE;
    nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
  }
}

static void nr_rrc_ue_prepare_RRCSetupRequest(NR_UE_RRC_INST_t *rrc)
{
  RRCLOG_D("Generation of RRCSetupRequest\n");
  uint8_t rv[6];
  // Get RRCConnectionRequest, fill random for now
  // Generate random byte stream for contention resolution
  for (int i = 0; i < 6; i++) {
#ifdef SMBV
    // if SMBV is configured the contention resolution needs to be fix for the connection procedure to succeed
    rv[i] = i;
#else
    rv[i] = taus() & 0xff;
#endif
  }

  uint8_t buf[1024];
  int len = do_RRCSetupRequest(buf, sizeof(buf), rv, rrc->fiveG_S_TMSI);

  nr_rlc_srb_recv_sdu(rrc->ue_id, 0, buf, len);
}

static void nr_rrc_configure_default_SI(NR_UE_RRC_SI_INFO *SI_info,
                                        NR_SI_SchedulingInfo_t *si_SchedulingInfo,
                                        NR_SI_SchedulingInfo_v1700_t *si_SchedulingInfo_v1700)
{
  for (int i = 0; i < MAX_SI_GROUPS; i++)
    SI_info->default_otherSI_map[i] = 0;
  int nb_groups = 0;
  if (si_SchedulingInfo) {
    nb_groups = si_SchedulingInfo->schedulingInfoList.list.count;
    AssertFatal(nb_groups <= MAX_SI_GROUPS, "Exceeding max number of SI groups configured\n");
    for (int i = 0; i < nb_groups; i++) {
      NR_SchedulingInfo_t *schedulingInfo = si_SchedulingInfo->schedulingInfoList.list.array[i];
      for (int j = 0; j < schedulingInfo->sib_MappingInfo.list.count; j++) {
        NR_SIB_TypeInfo_t *sib_Type = schedulingInfo->sib_MappingInfo.list.array[j];
        SI_info->default_otherSI_map[i] |= 1 << sib_Type->type;
      }
    }
  }

  if (si_SchedulingInfo_v1700) {
    int start_idx = nb_groups;
    nb_groups += si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.count;
    AssertFatal(nb_groups <= MAX_SI_GROUPS, "Exceeding max number of SI groups configured\n");
    for (int i = 0; i < si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.count; i++) {
      NR_SchedulingInfo2_r17_t *schedulingInfo2 = si_SchedulingInfo_v1700->schedulingInfoList2_r17.list.array[i];
      for (int j = 0; j < schedulingInfo2->sib_MappingInfo_r17.list.count; j++) {
        NR_SIB_TypeInfo_v1700_t *sib_TypeInfo_v1700 = schedulingInfo2->sib_MappingInfo_r17.list.array[j];
        if (sib_TypeInfo_v1700->sibType_r17.present == NR_SIB_TypeInfo_v1700__sibType_r17_PR_type1_r17) {
          SI_info->default_otherSI_map[start_idx + i] |= 1 << (sib_TypeInfo_v1700->sibType_r17.choice.type1_r17 + 13);
        }
      }
    }
  }
}

static bool verify_NTN_access(const NR_UE_RRC_SI_INFO *SI_info, const NR_SIB1_v1700_IEs_t *sib1_v1700)
{
  // SIB1 indicates if NTN access is present in the cell
  bool ntn_access = false;
  if (sib1_v1700 && sib1_v1700->cellBarredNTN_r17
      && *sib1_v1700->cellBarredNTN_r17 == NR_SIB1_v1700_IEs__cellBarredNTN_r17_notBarred)
    ntn_access = true;

  uint32_t sib19_mask = 1 << (NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType19 + 13);
  int sib19_present = false;
  for (int i = 0; i < MAX_SI_GROUPS; i++) {
    sib19_present = SI_info->default_otherSI_map[i] & sib19_mask;
    if (sib19_present)
      break;
  }
  AssertFatal(!ntn_access || sib19_present, "NTN cell, but SIB19 not configured.\n");
  return ntn_access && sib19_present;
}

static void get_sib19_schedinfo(NR_UE_RRC_SI_INFO *SI_info, NR_SI_SchedulingInfo_v1700_t *si_SchedInfo_v1700)
{
  // Find the SIB19 periodicity configured in the scheduling info
  if (si_SchedInfo_v1700) {
    int count_v17 = si_SchedInfo_v1700->schedulingInfoList2_r17.list.count;
    for (int i = 0; i < count_v17; i++) {
      struct NR_SchedulingInfo2_r17 *schedulingInfo2 = si_SchedInfo_v1700->schedulingInfoList2_r17.list.array[i];
      for (int j = 0; j < schedulingInfo2->sib_MappingInfo_r17.list.count; j++) {
        struct NR_SIB_TypeInfo_v1700 *sib_TypeInfo_v1700 = schedulingInfo2->sib_MappingInfo_r17.list.array[j];
        if (sib_TypeInfo_v1700->sibType_r17.present == NR_SIB_TypeInfo_v1700__sibType_r17_PR_type1_r17) {
          if (sib_TypeInfo_v1700->sibType_r17.choice.type1_r17 == NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType19) {
            SI_info->sib19_periodicity = 8 << schedulingInfo2->si_Periodicity_r17;
            SI_info->sib19_windowposition = schedulingInfo2->si_WindowPosition_r17;
            return;
          }
        }
      }
    }
  }
}

static void nr_rrc_send_sib1_to_mac(NR_UE_RRC_INST_t *rrc, NR_SIB1_t *sib1, bool can_start_ra, bool is_dedicatedSIB1)
{
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_SIB1;
  nr_mac_rrc_config_sib1_t *config_sib1 = &rrc_msg.payload.config_sib1;
  config_sib1->sib1 = sib1;
  config_sib1->is_dedicated_sib1 = is_dedicatedSIB1;
  config_sib1->can_start_ra = can_start_ra;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
}

static void nr_rrc_process_sib1(NR_UE_RRC_INST_t *rrc, NR_UE_RRC_SI_INFO *SI_info, NR_SIB1_t *sib1, bool is_dedicatedSIB1)
{
  if(g_log->log_component[NR_RRC].level >= OAILOG_DEBUG)
    xer_fprint(stdout, &asn_DEF_NR_SIB1, (const void *) sib1);
  RRCLOG_A("SIB1 decoded\n");

  plmn_id_t *plmn_id = malloc_or_fail(sizeof(plmn_id_t));

  /* selected_plmn_identity is one-indexed */
  AssertFatal(rrc->selected_plmn_identity > 0, "No PLMN selected");
  /* only one PLMN info block is currently supported*/
  NR_PLMN_Identity_t *plmn = sib1->cellAccessRelatedInfo.plmn_IdentityInfoList.list.array[0]
                                 ->plmn_IdentityList.list.array[rrc->selected_plmn_identity - 1];

  /* Convert MCC */
  plmn_id->mcc = (*plmn->mcc->list.array[0]) * 100 + (*plmn->mcc->list.array[1]) * 10 + (*plmn->mcc->list.array[2]);

  plmn_id->mnc_digit_length = plmn->mnc.list.count;

  /* Convert MNC (2 or 3 digits) */
  if (plmn->mnc.list.count == 3) {
    plmn_id->mnc = (*plmn->mnc.list.array[0]) * 100 + (*plmn->mnc.list.array[1]) * 10 + (*plmn->mnc.list.array[2]);
  } else {
    plmn_id->mnc = (*plmn->mnc.list.array[0]) * 10 + (*plmn->mnc.list.array[1]);
  }

  nr_ue_nas_t *nas = get_ue_nas_info(rrc->ue_id);
  nas->sn_id = plmn_id;

  /* Extract 36-bit NR Cell Identity */
  const NR_CellIdentity_t *ci = &sib1->cellAccessRelatedInfo.plmn_IdentityInfoList.list.array[0]->cellIdentity;
  rrc->cell_identity = BIT_STRING_to_uint64(ci);

  nr_timer_start(&SI_info->sib1_timer);
  SI_info->sib1_validity = true;
  if (rrc->nrRrcState == RRC_STATE_IDLE_NR) {
    rrc->ra_trigger = RRC_CONNECTION_SETUP;
  }

  NR_SIB1_v1700_IEs_t *sib1_v1700 = NULL;
  NR_SI_SchedulingInfo_v1700_t *si_SchedInfo_v1700 = NULL;
  if (sib1->nonCriticalExtension
      && sib1->nonCriticalExtension->nonCriticalExtension
      && sib1->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension) {
    sib1_v1700 = sib1->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension;
    si_SchedInfo_v1700 = sib1_v1700->si_SchedulingInfo_v1700;
  }

  AssertFatal(sib1->servingCellConfigCommon, "configuration issue in SIB1\n");
  SI_info->scs = sib1->servingCellConfigCommon->downlinkConfigCommon.initialDownlinkBWP.genericParameters.subcarrierSpacing;
  SI_info->si_windowlength = (sib1->si_SchedulingInfo) ? sib1->si_SchedulingInfo->si_WindowLength : 0;
  // configure default SI
  nr_rrc_configure_default_SI(SI_info, sib1->si_SchedulingInfo, si_SchedInfo_v1700);
  rrc->is_NTN_UE = verify_NTN_access(SI_info, sib1_v1700);
  if (rrc->is_NTN_UE)
    get_sib19_schedinfo(SI_info, si_SchedInfo_v1700);

  // configure timers and constant
  nr_rrc_set_sib1_timers_and_constants(&rrc->timers_and_constants, sib1);
  // RRC storage of SIB1 timers and constants (eg needed in re-establishment)
  UPDATE_IE(rrc->timers_and_constants.sib1_TimersAndConstants, sib1->ue_TimersAndConstants, NR_UE_TimersAndConstants_t);

  asn1cFreeStruc(asn_DEF_NR_SIB1, SI_info->sib1);
  const int copy_result = asn_copy(&asn_DEF_NR_SIB1, (void **)&SI_info->sib1, sib1);
  AssertFatal(copy_result == 0, "unable to copy NR_SIB1_t\n");

  nr_rrc_send_sib1_to_mac(rrc, sib1, !rrc->is_NTN_UE, is_dedicatedSIB1);
}

/** @brief Synchronize NH chain based on nextHopChainingCount
 *
 * This function synchronizes the NH parameter chain as specified in 3GPP TS 33.501 Annex A.10.
 * Use cases: Master Key Update, RRC Reestablishment: The UE receives an NCC value that is
 * different from the NCC associated with the currently active KgNB. The NH chain is synchronized
 * by computing the function defined in Annex A.10 iteratively (and increasing the NCC value until
 * it matches the NCC value received from the source ng-gNB).
 *
 * @param kamf K_AMF key from NAS
 * @param kgnb Current KgNB key (input/output)
 * @param nh Current NH parameter (input/output)
 * @param nhcc Pointer to current nextHopChainingCount (input/output)
 * @param target_ncc Target nextHopChainingCount value to synchronize to */
static void nr_sync_nh_chain(const uint8_t kamf[SECURITY_KEY_LEN],
                             uint8_t kgnb[SECURITY_KEY_LEN],
                             uint8_t nh[SECURITY_KEY_LEN],
                             uint64_t *nhcc,
                             const int8_t target_ncc)
{
  // If the UE received an NCC value that was different from the NCC associated with the
  // currently active KgNB, the UE shall first synchronize the locally kept NH
  // parameter by computing the function defined in Annex A.10 iteratively (and increasing
  // the NCC value until it matches the NCC value received from the source ng-gNB).
  if (target_ncc <= *nhcc) {
    LOG_W(NR_RRC, "Received NCC=%d is less than or equal to current nhcc=%ld (nothing to sync)\n", target_ncc, *nhcc);
    return;
  }
  if (target_ncc > *nhcc) {
    LOG_I(NR_RRC, "Synchronizing NH chain: current nhcc=%ld, target ncc=%d\n", *nhcc, target_ncc);
    if (*nhcc == 0) {
      // First derivation: derive KgNB from KAMF, then derive NH from KgNB (per TS 33.501 A.10)
      // Note: For the first NH derivation, we use UL NAS COUNT = 0 to match the AMF's derivation
      // during handover. The AMF derives the first NH using the UL NAS COUNT from the last
      // successful NAS SMC, which is 0 for the initial derivation.
      derive_kgnb(kamf, 0, kgnb);
      nr_derive_nh(kamf, kgnb, nh);
      *nhcc = 1;
    }
    // Following derivations: iterate from current nhcc to target_ncc
    for (uint8_t i = *nhcc; i < target_ncc; i++) {
      LOG_D(NR_RRC, "Derive keys for ChainingCount = %d\n", i);
      nr_derive_nh(kamf, nh, nh);
    }
    *nhcc = target_ncc;
  }
}

/** @brief Derive KNG-RAN* (KgNB*) using horizontal derivation
 *
 * This function derives KNG-RAN* from the currently active KgNB (horizontal derivation)
 * as specified in 3GPP TS 33.501 Annex A.11 and A.12.
 * Used when NCC values match (no NH synchronization needed).
 *
 * @param pci Physical Cell ID
 * @param nr_arfcn_dl NR ARFCN-DL
 * @param kgnb Current KgNB key (input/output)
 */
static void nr_derive_kgnb_horizontal(const uint16_t pci, const uint64_t nr_arfcn_dl, uint8_t kgnb[SECURITY_KEY_LEN])
{
  // When the NCC values match, the UE shall derive the KNG-RAN* from the currently active
  // KgNB and the target PCI and its frequency ARFCN-DL using the function defined in Annex A.11 and A.12.
  LOG_D(NR_RRC, "Deriving KNG-RAN* using horizontal derivation (from current KgNB)\n");
  nr_derive_key_ng_ran_star(pci, nr_arfcn_dl, kgnb, kgnb);
}

/** @brief Derive KNG-RAN* (KgNB*) using vertical derivation
 *
 * This function derives KNG-RAN* from the synchronized NH parameter (vertical derivation)
 * as specified in 3GPP TS 33.501 Annex A.11 and A.12.
 * Used after NH chain synchronization when NCC values differ.
 *
 * @param pci Physical Cell ID
 * @param nr_arfcn_dl NR ARFCN-DL
 * @param kgnb Current KgNB key (input/output)
 * @param nh Synchronized NH parameter (input)
 */
static void nr_derive_kgnb_vertical(const uint16_t pci,
                                    const uint64_t nr_arfcn_dl,
                                    uint8_t kgnb[SECURITY_KEY_LEN],
                                    const uint8_t nh[SECURITY_KEY_LEN])
{
  // When the NCC values match (after synchronization), the UE shall compute the K NG-RAN *
  // from the synchronized NH parameter and the target PCI and its frequency ARFCN-DL
  // using the function defined in Annex A.11 and A.12.
  LOG_D(NR_RRC, "Deriving KNG-RAN* using vertical derivation (from synchronized NH)\n");
  nr_derive_key_ng_ran_star(pci, nr_arfcn_dl, nh, kgnb);
}

/** @brief Update KgNB based on received nextHopChainingCount
 *
 * This function implements the common logic for updating KgNB based on received NCC value,
 * as specified in 3GPP TS 33.501 6.9.2.3.4.
 *
 * Per 33.501 6.9.2.3.4 and Fig 6.9.2.1.1-1:
 * - If NCC received == current NCC: use horizontal derivation (from currently active KgNB)
 *   This applies regardless of whether NCC is 0 or >0. For a fixed NCC level, multiple
 *   horizontal derivations can be done within that level.
 * - If NCC received > current NCC: synchronize NH chain (Annex A.10), then use vertical
 *   derivation (from NH) to enter the new NCC level.
 *
 * Horizontal derivation = derive KNG-RAN* from currently active KgNB + (PCI, ARFCN-DL)
 * Vertical derivation = derive KNG-RAN* from NH + (PCI, ARFCN-DL)
 *
 * @param rrc RRC instance pointer
 * @param kamf K_AMF key from NAS
 * @param received_ncc Received nextHopChainingCount value */
static void nr_update_kgnb_from_ncc(NR_UE_RRC_INST_t *rrc, const uint8_t kamf[SECURITY_KEY_LEN], int8_t received_ncc)
{
  const uint64_t original_nhcc = rrc->nhcc;

  if (received_ncc == original_nhcc) {
    // NCC values match: use horizontal derivation from currently active KgNB
    // Per TS 33.501 6.9.2.3.4: "derive the KNG-RAN* from the currently active KgNB"
    // This applies regardless of NCC being 0 or >0. For a fixed NCC level, we stay
    // within that level using horizontal derivations.
    RRCLOG_D("NCC values match (%d), using horizontal derivation\n", received_ncc);
    nr_derive_kgnb_horizontal(rrc->phyCellID, rrc->arfcn_ssb, rrc->kgnb);
  } else if (received_ncc < original_nhcc) {
    // Note: According to spec, NCC should only increase. If received_ncc < original_nhcc,
    // this is an error condition, but we handle it gracefully.
    RRCLOG_W("Received NCC=%d is less than current nhcc=%ld (unexpected per spec, NH chain should only increase)\n",
             received_ncc,
             original_nhcc);
  } else {
    // received_ncc > original_nhcc: synchronize NH chain first (per 33.501 A.10)
    nr_sync_nh_chain(kamf, rrc->kgnb, rrc->nh, &rrc->nhcc, received_ncc);
    // Store the received nextHopChainingCount value (per 38.331 5.3.7.5)
    RRCLOG_D("Synchronizing NH chain to target NCC %d\n", received_ncc);
    rrc->nhcc = received_ncc;
    // After synchronization, derive KNG-RAN* from synchronized NH (vertical derivation)
    // per 33.501 6.9.2.3.4: "When the NCC values match, the UE shall compute the K NG-RAN *
    // from the synchronized NH parameter"
    nr_derive_kgnb_vertical(rrc->phyCellID, rrc->arfcn_ssb, rrc->kgnb, rrc->nh);
  }
}

/** @brief AS security key update procedure (5.3.5.7 3GPP TS 38.331) */
void as_security_key_update(NR_UE_RRC_INST_t *rrc, NR_MasterKeyUpdate_t *mku)
{
  if (mku->nas_Container) {
    RRCLOG_E("forward the nas-Container to the upper layers: not implemented yet\n");
  }
  if (mku->keySetChangeIndicator) {
    RRCLOG_E("derive or update the K gNB key based on the K AMF key, as specified in TS 33.501: not implemented yet\n");
  } else {
    RRCLOG_I("Received masterKeyUpdate (nextHopChainingCount %ld): update security keys\n", mku->nextHopChainingCount);
    /** @todo: The KAMF should be obtained from NAS. This exchange over ITTI must be synchronized
     * with the rest of the RRCReconfiguration procedure, in particular, the RadioBearerConfig
     * processing that triggers bearer modifications. Security configueration of bearers must
     * complete using the newly derived keys. As a workaround NAS is directly accessed here. */
    nr_ue_nas_t *nas = get_ue_nas_info(rrc->ue_id);
    const uint8_t *kamf = nas->security.kamf;
    // KgNB update (TS 33.501 §6.9.2.3.3):
    // If received NCC != local NCC, iteratively derive NH (Annex A.10) until NCC matches.
    // Then derive the new KgNB from the synchronized NH.
    nr_update_kgnb_from_ncc(rrc, kamf, mku->nextHopChainingCount);
  }
}

static nr_pdcp_entity_security_keys_and_algos_t get_security_rrc_parameters(NR_UE_RRC_INST_t *ue, bool cp)
{
  nr_pdcp_entity_security_keys_and_algos_t out = {0};
  out.ciphering_algorithm = ue->cipheringAlgorithm;
  out.integrity_algorithm = ue->integrityProtAlgorithm;
  nr_derive_key(cp ? RRC_ENC_ALG : UP_ENC_ALG, ue->cipheringAlgorithm, ue->kgnb, out.ciphering_key);
  nr_derive_key(cp ? RRC_INT_ALG : UP_INT_ALG, ue->integrityProtAlgorithm, ue->kgnb, out.integrity_key);
  return out;
}

/** @brief Check if there is dedicated NAS information to forward to NAS */
static void nr_rrc_process_dedicatedNAS_MessageList(NR_UE_RRC_INST_t *rrc, NR_RRCReconfiguration_v1530_IEs_t *rec_1530)
{
  if (rec_1530->dedicatedNAS_MessageList) {
    struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList *tmp = rec_1530->dedicatedNAS_MessageList;
    for (int i = 0; i < tmp->list.count; i++) {
      MessageDef *ittiMsg = itti_alloc_new_message(TASK_RRC_NRUE, rrc->ue_id, NAS_CONN_ESTABLI_CNF);
      nas_establish_cnf_t *msg = &NAS_CONN_ESTABLI_CNF(ittiMsg);
      msg->errCode = AS_SUCCESS;
      msg->nasMsg.length = tmp->list.array[i]->size;
      msg->nasMsg.nas_data = malloc_or_fail(msg->nasMsg.length);
      memcpy(msg->nasMsg.nas_data, tmp->list.array[i]->buf, msg->nasMsg.length);
      itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, ittiMsg);
    }
  }
}

/** @brief RRC notifies SDAP to stop the reader for all active PSI
 * @note Fetch pdu_tun from NAS */
static void nr_rrc_ue_tun_stop_readers(ue_id_t ue_id)
{
  nr_ue_nas_t *nas = get_ue_nas_info(ue_id);
  for (int psi = 1; psi < MAX_NUM_PSI; psi++) {
    nas_ue_pdu_tun_t *t = &nas->pdu_tun[psi];
    if (nas->psi_status[psi] != PDU_SESSION_INACTIVE && t->sock >= 0)
      nr_sdap_tun_stop_reader(&t->reader_thread);
  }
}

/** @brief RRC notifies SDAP to start the idle listener for all active PSI
 * @note Fetch pdu_tun from NAS */
static void nr_rrc_ue_tun_start_idle_listeners(ue_id_t ue_id)
{
  nr_ue_nas_t *nas = get_ue_nas_info(ue_id);
  for (int psi = 1; psi < MAX_NUM_PSI; psi++) {
    nas_ue_pdu_tun_t *t = &nas->pdu_tun[psi];
    if (nas->psi_status[psi] != PDU_SESSION_INACTIVE && t->sock >= 0)
      nr_sdap_tun_start_idle_listener(ue_id, psi, t->sock, &t->reader_thread);
  }
}

/** @brief Start the user-plane TUN UL reader for one PDU session
 * Stop any prior reader on reader_thread, bind sock and qfi onto the SDAP entity, then start the reader
 * @note reader_thread is NAS pdu_tun state, start/stop only through RRC */
static void nr_rrc_ue_tun_start_user_plane(ue_id_t ue_id, int pdusession_id, int sock, int qfi, pthread_t *reader_thread)
{
  DevAssert(pdusession_id > 0 && pdusession_id < MAX_NUM_PSI);
  DevAssert(sock >= 0);
  DevAssert(reader_thread != NULL);
  nr_sdap_entity_t *entity = nr_sdap_get_entity(ue_id, pdusession_id);
  DevAssert(entity != NULL);
  nr_sdap_tun_stop_reader(reader_thread);
  nr_sdap_tun_bind(entity, sock, qfi);
  char name[64];
  snprintf(name, sizeof(name), "ue_tun_read_%ld_p%d", ue_id, pdusession_id);
  nr_sdap_tun_start_reader(entity, reader_thread, name);
}

/** @brief Add bearer in PDCP/SDAP for 5GC association (SA) */
static void rrc_ue_add_bearer(const int ue_id, const NR_DRB_ToAddMod_t *drb, const nr_pdcp_entity_security_keys_and_algos_t *sp)
{
  DevAssert(drb->cnAssociation);
  DevAssert(drb->cnAssociation->present != NR_DRB_ToAddMod__cnAssociation_PR_NOTHING);
  // get SDAP config
  sdap_config_t sdap = {0};
  if (drb->cnAssociation->present == NR_DRB_ToAddMod__cnAssociation_PR_eps_BearerIdentity) {
    DevAssert(drb->cnAssociation->present == NR_DRB_ToAddMod__cnAssociation_PR_eps_BearerIdentity);

    // EPC association
    sdap.pdusession_id = drb->cnAssociation->choice.eps_BearerIdentity;
    sdap.drb_id = drb->drb_Identity;
    sdap.defaultDRB = true;
  } else {
    DevAssert(drb->cnAssociation->present == NR_DRB_ToAddMod__cnAssociation_PR_sdap_Config);
    DevAssert(drb->cnAssociation->choice.sdap_Config);
    sdap = nr_sdap_get_config(GNB_FLAG_NO, drb->cnAssociation->choice.sdap_Config, drb->drb_Identity);
  }
  // add SDAP entity
  nr_sdap_addmod_entity(GNB_FLAG_NO, ue_id, &sdap);
  // add PDCP entity
  nr_pdcp_add_drb(GNB_FLAG_NO, ue_id, drb->pdcp_Config, &sdap, sp);
}

/**
 * @brief add, modify and release SRBs and/or DRBs
 * @ref   3GPP TS 38.331
 */
static void nr_rrc_ue_process_RadioBearerConfig(NR_UE_RRC_INST_t *rrc, NR_RadioBearerConfig_t *const radioBearerConfig)
{
  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_RadioBearerConfig, (const void *)radioBearerConfig);

  if (radioBearerConfig->srb3_ToRelease) {
    nr_pdcp_release_srb(rrc->ue_id, 3);
    rrc->Srb[3] = RB_NOT_PRESENT;
  }

  nr_pdcp_entity_security_keys_and_algos_t security_rrc_parameters = {0};
  nr_pdcp_entity_security_keys_and_algos_t security_up_parameters = {0};

  if (rrc->as_security_activated) {
    if (radioBearerConfig->securityConfig != NULL) {
      // When the field is not included, continue to use the currently configured keyToUse
      if (radioBearerConfig->securityConfig->keyToUse) {
        AssertFatal(*radioBearerConfig->securityConfig->keyToUse == NR_SecurityConfig__keyToUse_master,
                    "Secondary key usage seems not to be implemented\n");
        rrc->keyToUse = *radioBearerConfig->securityConfig->keyToUse;
      }
      // When the field is not included, continue to use the currently configured security algorithm
      const NR_SecurityAlgorithmConfig_t *algos = radioBearerConfig->securityConfig->securityAlgorithmConfig;
      if (algos) {
        rrc->cipheringAlgorithm = algos->cipheringAlgorithm;
        AssertFatal(algos->integrityProtAlgorithm, "Release of integrityProtAlgorithm (Need R) not supported\n");
        rrc->integrityProtAlgorithm = *algos->integrityProtAlgorithm;
      }
    }
    security_rrc_parameters = get_security_rrc_parameters(rrc, true);
    security_up_parameters = get_security_rrc_parameters(rrc, false);
  }

  if (radioBearerConfig->srb_ToAddModList != NULL) {
    for (int cnt = 0; cnt < radioBearerConfig->srb_ToAddModList->list.count; cnt++) {
      struct NR_SRB_ToAddMod *srb = radioBearerConfig->srb_ToAddModList->list.array[cnt];
      if (rrc->Srb[srb->srb_Identity] == RB_NOT_PRESENT) {
        rrc->Srb[srb->srb_Identity] = RB_ESTABLISHED;
        add_srb(false, rrc->ue_id, radioBearerConfig->srb_ToAddModList->list.array[cnt], &security_rrc_parameters);
      } else {
        AssertFatal(srb->discardOnPDCP == NULL, "discardOnPDCP not yet implemented\n");
        if (srb->reestablishPDCP) {
          rrc->Srb[srb->srb_Identity] = RB_ESTABLISHED;
          nr_pdcp_reestablishment(rrc->ue_id, srb->srb_Identity, true, &security_rrc_parameters);
        }
        if (srb->pdcp_Config && srb->pdcp_Config->t_Reordering)
          nr_pdcp_reconfigure_srb(rrc->ue_id, srb->srb_Identity, *srb->pdcp_Config->t_Reordering);
      }
    }
  }

  if (radioBearerConfig->drb_ToReleaseList) {
    for (int cnt = 0; cnt < radioBearerConfig->drb_ToReleaseList->list.count; cnt++) {
      NR_DRB_Identity_t *DRB_id = radioBearerConfig->drb_ToReleaseList->list.array[cnt];
      if (DRB_id) {
        nr_pdcp_release_drb(rrc->ue_id, *DRB_id);
        set_DRB_status(rrc, *DRB_id, RB_NOT_PRESENT);
      }
    }
  }

  /**
   * Establish/reconfig DRBs if DRB-ToAddMod is present
   * according to 3GPP TS 38.331 clause 5.3.5.6.5 DRB addition/modification
   */
  if (radioBearerConfig->drb_ToAddModList != NULL) {
    for (int cnt = 0; cnt < radioBearerConfig->drb_ToAddModList->list.count; cnt++) {
      struct NR_DRB_ToAddMod *drb = radioBearerConfig->drb_ToAddModList->list.array[cnt];
      int DRB_id = drb->drb_Identity;
      if (get_DRB_status(rrc, DRB_id) != RB_NOT_PRESENT) {
        if (drb->reestablishPDCP) {
          set_DRB_status(rrc, DRB_id, RB_ESTABLISHED);
          /* get integrity and cipehring settings from radioBearerConfig */
          bool has_integrity = drb->pdcp_Config != NULL
                               && drb->pdcp_Config->drb != NULL
                               && drb->pdcp_Config->drb->integrityProtection != NULL;
          bool has_ciphering = !(drb->pdcp_Config != NULL
                                 && drb->pdcp_Config->ext1 != NULL
                                 && drb->pdcp_Config->ext1->cipheringDisabled != NULL);
          security_up_parameters.ciphering_algorithm = has_ciphering ? rrc->cipheringAlgorithm : 0;
          security_up_parameters.integrity_algorithm = has_integrity ? rrc->integrityProtAlgorithm : 0;
          /* re-establish */
          nr_pdcp_reestablishment(rrc->ue_id, DRB_id, false, &security_up_parameters);
        }
        AssertFatal(drb->recoverPDCP == NULL, "recoverPDCP not yet implemented\n");
        /* PDCP reconfiguration */
        if (drb->pdcp_Config)
          nr_pdcp_reconfigure_drb(rrc->ue_id, DRB_id, drb->pdcp_Config);
        /* SDAP entity reconfiguration */
        /* sdap-Config is included (SA mode) */
        if (drb->cnAssociation && drb->cnAssociation->present == NR_DRB_ToAddMod__cnAssociation_PR_sdap_Config) {
          NR_SDAP_Config_t *sdap_Config = drb->cnAssociation->choice.sdap_Config;
          if (sdap_Config)
            nr_reconfigure_sdap_entity(sdap_Config, rrc->ue_id, sdap_Config->pdu_Session, DRB_id);
          else
            RRCLOG_E("SDAP-Config not present despite element of choice signaling its presence\n");
        }
      } else {
        set_DRB_status(rrc, DRB_id, RB_ESTABLISHED);
        rrc_ue_add_bearer(rrc->ue_id, radioBearerConfig->drb_ToAddModList->list.array[cnt], &security_up_parameters);
      }
    }
  } // drb_ToAddModList //

  rrc->nrRrcState = RRC_STATE_CONNECTED_NR;
  RRCLOG_I("State = NR_RRC_CONNECTED\n");
}

static void nr_rrc_signal_maxrtxindication(int ue_id)
{
  MessageDef *msg = itti_alloc_new_message(TASK_RLC_UE, ue_id, NR_RRC_RLC_MAXRTX);
  NR_RRC_RLC_MAXRTX(msg).ue_id = ue_id;
  itti_send_msg_to_task(TASK_RRC_NRUE, ue_id, msg);
}

/** @brief Release all active RLC entities for a UE and set to inactive.
 * This is typically used when tearing down all DRBs at UE side,
 * such as after a PDU session release or full reconfiguration.
 * @param rrc Pointer to NR_UE_RRC_INST_t structure
 * @param id Logical Channel ID (must be in range [0, NR_MAX_NUM_LCID-1]) */
static void nr_rrc_release_rlc_entity(NR_UE_RRC_INST_t *rrc, int id)
{
  DevAssert(rrc);
  DevAssert(id >= 0 && id < NR_MAX_NUM_LCID);
  if (rrc->active_RLC_entity[id]) {
    rrc->active_RLC_entity[id] = false;
    nr_rlc_release_entity(rrc->ue_id, id);
    RRCLOG_I("RRC Released RLC entity lc_id=%d\n", id);
  }
}

static void nr_rrc_manage_rlc_bearers(NR_UE_RRC_INST_t *rrc, const NR_CellGroupConfig_t *cellGroupConfig)
{
  if (cellGroupConfig->rlc_BearerToReleaseList != NULL) {
    for (int i = 0; i < cellGroupConfig->rlc_BearerToReleaseList->list.count; i++) {
      NR_LogicalChannelIdentity_t *lcid = cellGroupConfig->rlc_BearerToReleaseList->list.array[i];
      AssertFatal(lcid, "LogicalChannelIdentity shouldn't be null here\n");
      nr_rrc_release_rlc_entity(rrc, *lcid);
    }
  }

  if (cellGroupConfig->rlc_BearerToAddModList != NULL) {
    for (int i = 0; i < cellGroupConfig->rlc_BearerToAddModList->list.count; i++) {
      NR_RLC_BearerConfig_t *rlc_bearer = cellGroupConfig->rlc_BearerToAddModList->list.array[i];
      NR_LogicalChannelIdentity_t lcid = rlc_bearer->logicalChannelIdentity;
      if (rrc->active_RLC_entity[lcid]) {
        if (rlc_bearer->reestablishRLC)
          nr_rlc_reestablish_entity(rrc->ue_id, lcid);
        if (rlc_bearer->rlc_Config)
          nr_rlc_reconfigure_entity(rrc->ue_id, lcid, rlc_bearer->rlc_Config);
      } else {
        rrc->active_RLC_entity[lcid] = true;
        AssertFatal(rlc_bearer->servedRadioBearer, "servedRadioBearer mandatory in case of setup\n");
        AssertFatal(rlc_bearer->servedRadioBearer->present != NR_RLC_BearerConfig__servedRadioBearer_PR_NOTHING,
                    "Invalid RB for RLC configuration\n");
        if (rlc_bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity) {
          NR_SRB_Identity_t srb_id = rlc_bearer->servedRadioBearer->choice.srb_Identity;
          nr_rlc_add_srb(rrc->ue_id, srb_id, rlc_bearer);
          nr_rlc_set_rlf_handler(rrc->ue_id, nr_rrc_signal_maxrtxindication);
        } else { // DRB
          NR_DRB_Identity_t drb_id = rlc_bearer->servedRadioBearer->choice.drb_Identity;
          if (!rlc_bearer->rlc_Config) {
            RRCLOG_E("RRC RLC-Config not present but is mandatory for setup\n");
            rrc->active_RLC_entity[lcid] = false;
          } else {
            nr_rlc_add_drb(rrc->ue_id, drb_id, rlc_bearer);
            nr_rlc_set_rlf_handler(rrc->ue_id, nr_rrc_signal_maxrtxindication);
          }
        }
      }
    }
  }
}

static void nr_ue_meas_reset(meas_t *meas_cell, bool csi_meas)
{
  if (csi_meas)
    meas_cell->csi_rsrp_dBm.init = false;
  else
    meas_cell->ss_rsrp_dBm.init = false;
}

/** @brief Clear the source PCell snapshot, including its keys */
static void nr_rrc_clear_ho_source(NR_UE_RRC_INST_t *rrc)
{
  asn1cFreeStruc(asn_DEF_NR_SIB1, rrc->ho_source.sib1);
  memset(&rrc->ho_source, 0, sizeof(rrc->ho_source));
}

static void nr_rrc_process_reconfigurationWithSync(NR_UE_RRC_INST_t *rrc,
                                                   NR_ReconfigurationWithSync_t *reconfigurationWithSync,
                                                   int gNB_index)
{
  // perform Reconfiguration with sync according to 5.3.5.5.2
  if (!rrc->as_security_activated && !(get_softmodem_params()->phy_test || get_softmodem_params()->do_ra)) {
    // if the AS security is not activated, perform the actions upon going to RRC_IDLE as specified in 5.3.11
    // with the release cause 'other' upon which the procedure ends
    NR_Release_Cause_t release_cause = OTHER;
    nr_rrc_going_to_IDLE(rrc, release_cause, NULL);
    return;
  }

  // Clear neighbor cell lists from measurement objects during handover
  rrcPerNB_t *rrcNB = &rrc->perNB[gNB_index];
  for (int i = 0; i < NR_MAX_MEAS_OBJ; i++) {
    if (rrcNB->MeasObj[i] && rrcNB->MeasObj[i]->measObject.present == NR_MeasObjectToAddMod__measObject_PR_measObjectNR) {
      NR_MeasObjectNR_t *measObjNR = rrcNB->MeasObj[i]->measObject.choice.measObjectNR;
      if (measObjNR->cellsToAddModList) {
        ASN_STRUCT_FREE(asn_DEF_NR_CellsToAddModList, measObjNR->cellsToAddModList);
        measObjNR->cellsToAddModList = NULL;
      }
    }
  }

  // source PCell snapshot, before it is overwritten by the target's
  nr_rrc_clear_ho_source(rrc);
  if (rrc->as_security_activated) {
    rrc->ho_source.valid = true;
    rrc->ho_source.phyCellID = rrc->phyCellID;
    rrc->ho_source.rnti = rrc->rnti;
    memcpy(rrc->ho_source.kgnb, rrc->kgnb, sizeof(rrc->ho_source.kgnb));
    memcpy(rrc->ho_source.nh, rrc->nh, sizeof(rrc->ho_source.nh));
    rrc->ho_source.nhcc = rrc->nhcc;
    rrc->ho_source.cipheringAlgorithm = rrc->cipheringAlgorithm;
    rrc->ho_source.integrityProtAlgorithm = rrc->integrityProtAlgorithm;
    rrc->ho_source.keyToUse = rrc->keyToUse;
    memcpy(rrc->ho_source.Srb, rrc->Srb, sizeof(rrc->ho_source.Srb));
    memcpy(rrc->ho_source.status_DRBs, rrc->status_DRBs, sizeof(rrc->ho_source.status_DRBs));
    rrc->ho_source.sib1 = rrcNB->SInfo.sib1;
    rrcNB->SInfo.sib1 = NULL;
  }
  l3_measurements_t *l3m = &rrcNB->l3_measurements;
  for (int i = 0; i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
    nr_ue_meas_reset(&l3m->neighboring_cell[i], false);
    nr_ue_meas_reset(&l3m->neighboring_cell[i], true);
  }

  if (reconfigurationWithSync->spCellConfigCommon) {
    /* if the frequencyInfoDL is included, consider the target SpCell
       to be one on the SSB frequency indicated by the frequencyInfoDL */
    const NR_DownlinkConfigCommon_t *dcc = reconfigurationWithSync->spCellConfigCommon->downlinkConfigCommon;
    if (dcc && dcc->frequencyInfoDL && dcc->frequencyInfoDL->absoluteFrequencySSB) {
      rrc->arfcn_ssb = *dcc->frequencyInfoDL->absoluteFrequencySSB;
      RRCLOG_I("updated ARFCN_SSB=%ld\n", rrc->arfcn_ssb);
    }

    // consider the target SpCell to be one with a physical cell identity indicated by the physCellId
    if (!reconfigurationWithSync->spCellConfigCommon->physCellId)
      RRCLOG_E("physCellId absent but should be mandatory present upon cell change and cell addition\n");
    else
      rrc->phyCellID = *reconfigurationWithSync->spCellConfigCommon->physCellId;
  }

  NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
  nr_timer_stop(&tac->T310);
  if (!get_softmodem_params()->phy_test) {
    // T304 is stopped upon completion of RA procedure which is not done in phy-test mode
    int t304_value = nr_rrc_get_T304(reconfigurationWithSync->t304);
    nr_timer_setup(&tac->T304, t304_value, 10); // 10ms step
    nr_timer_start(&tac->T304);
  }
  rrc->rnti = reconfigurationWithSync->newUE_Identity;
  // reset the MAC entity of this cell group (done at MAC in handle_reconfiguration_with_sync)

  // 3GPP TS38.331 section 5.3.5.5.2
  nr_timer_stop(&tac->T430);

  if (rrc->target_ntncfg) {
    ASN_STRUCT_FREE(asn_DEF_NR_NTN_Config_r17, rrc->target_ntncfg);
    rrc->target_ntncfg = NULL;
    rrc->process_target_ntncfg = false;
  }
  if (reconfigurationWithSync->spCellConfigCommon &&
      reconfigurationWithSync->spCellConfigCommon->ext2 &&
      reconfigurationWithSync->spCellConfigCommon->ext2->ntn_Config_r17) {
    NR_NTN_Config_r17_t *ntncfg = reconfigurationWithSync->spCellConfigCommon->ext2->ntn_Config_r17;
    // EPOCH time is always sent if NTN config is sent through DCCH
    AssertFatal(ntncfg->epochTime_r17, "NTN-CONFIG sent in dedicated mode should have EPOCHTIME\n");
    const int copy_result = asn_copy(&asn_DEF_NR_NTN_Config_r17, (void **)&rrc->target_ntncfg, ntncfg);
    AssertFatal(copy_result == 0, "unable to copy NR_NTN_Config_r17_t\n");
  }
}

static bool nr_rrc_cellgroup_configuration(NR_UE_RRC_INST_t *rrc,
                                           NR_CellGroupConfig_t *cgConfig,
                                           int gNB_index,
                                           bool dedicatedsib1,
                                           bool *reconf_with_sync_scc)
{
  if (!check_cellgroup_config(cgConfig)) {
    // if we received a configuration not supported or with some wrong combination
    // we call the function for RLF (re-establishment if security is activated, going to IDLE otherwise)
    handle_rlf_detection(rrc);
    return false;
  }
  NR_SpCellConfig_t *spCellConfig = cgConfig->spCellConfig;
  if(spCellConfig) {
    NR_ServingCellConfig_t *spCellConfigDedicated = spCellConfig->spCellConfigDedicated;
    if (spCellConfigDedicated) {
      if (spCellConfigDedicated->firstActiveDownlinkBWP_Id)
        rrc->dl_bwp_id = *spCellConfigDedicated->firstActiveDownlinkBWP_Id;
      if (spCellConfigDedicated->uplinkConfig && spCellConfigDedicated->uplinkConfig->firstActiveUplinkBWP_Id)
        rrc->ul_bwp_id = *spCellConfigDedicated->uplinkConfig->firstActiveUplinkBWP_Id;
    }
    NR_ReconfigurationWithSync_t *reconfigurationWithSync = spCellConfig->reconfigurationWithSync;
    if (reconfigurationWithSync) {
      RRCLOG_I("Processing reconfigurationWithSync\n");
      if (reconf_with_sync_scc)
        *reconf_with_sync_scc = reconfigurationWithSync->spCellConfigCommon != NULL;
      nr_rrc_process_reconfigurationWithSync(rrc, reconfigurationWithSync, gNB_index);
      // if RRCReconfiguration does not include dedicatedSIB1-Delivery
      // if the active downlink BWP, which is indicated by the firstActiveDownlinkBWP-Id for the target SpCell of the MCG,
      // has a common search space configured by searchSpaceSIB1
      // acquire the SIB1, which is scheduled as specified in TS 38.213 [13], of the target SpCell of the MCG
      if (!dedicatedsib1 && IS_SA_MODE(get_softmodem_params())) {
        if (rrc->dl_bwp_id == 0) {
          // Check initial DL BWP for searchSpaceSIB1
          NR_ServingCellConfigCommon_t *spCellConfigCommon = reconfigurationWithSync->spCellConfigCommon;
          if (spCellConfigCommon) {
            NR_DownlinkConfigCommon_t *downlinkConfig = spCellConfigCommon->downlinkConfigCommon;
            if (downlinkConfig
                && downlinkConfig->initialDownlinkBWP
                && downlinkConfig->initialDownlinkBWP->pdcch_ConfigCommon
                && downlinkConfig->initialDownlinkBWP->pdcch_ConfigCommon->present == NR_SetupRelease_PDCCH_ConfigCommon_PR_setup
                && downlinkConfig->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceSIB1) {
              rrc->sched_reconfsync_sib1 = true;
            }
          }
        } else {
          // Check dedicated DL BWP for searchSpaceSIB1
          if (spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList) {
            for (int i = 0; i < spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList->list.count; i++) {
              NR_BWP_Downlink_t *bwp = spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList->list.array[i];
              if (bwp->bwp_Id == rrc->dl_bwp_id && bwp->bwp_Common && bwp->bwp_Common->pdcch_ConfigCommon
                  && bwp->bwp_Common->pdcch_ConfigCommon->present == NR_SetupRelease_PDCCH_ConfigCommon_PR_setup
                  && bwp->bwp_Common->pdcch_ConfigCommon->choice.setup->searchSpaceSIB1) {
                rrc->sched_reconfsync_sib1 = true;
                break;
              }
            }
          }
        }
      }
    }
    nr_rrc_handle_SetupRelease_RLF_TimersAndConstants(rrc, spCellConfig->rlf_TimersAndConstants);
  }

  nr_rrc_manage_rlc_bearers(rrc, cgConfig);
  return true;
}

static bool nr_rrc_ue_process_masterCellGroup(NR_UE_RRC_INST_t *rrc,
                                              OCTET_STRING_t *masterCellGroup,
                                              long *fullConfig,
                                              int gNB_index,
                                              bool dedicatedsib1,
                                              bool *reconf_with_sync_scc)
{
  AssertFatal(!fullConfig, "fullConfig not supported yet\n");
  NR_CellGroupConfig_t *cellGroupConfig = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL,
                                        &asn_DEF_NR_CellGroupConfig, //might be added prefix later
                                        (void **)&cellGroupConfig,
                                        (uint8_t *)masterCellGroup->buf,
                                        masterCellGroup->size, 0, 0);
  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    RRCLOG_E("CellGroupConfig decode error\n");
    // if the ASN1 decoding fails for the received CellGroup configuration
    // we call the function for RLF (re-establishment if security is activated, going to IDLE otherwise)
    handle_rlf_detection(rrc);
    return false;
  }
  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, (const void *) cellGroupConfig);
  }

  bool ret = nr_rrc_cellgroup_configuration(rrc, cellGroupConfig, gNB_index, dedicatedsib1, reconf_with_sync_scc);
  if (!ret)
    return false;

  RRCLOG_D("Sending CellGroupConfig to MAC the pointer will be managed by mac\n");
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_CG;
  nr_mac_rrc_config_cg_t *mac_msg = &rrc_msg.payload.config_cg;
  mac_msg->cellGroupConfig = cellGroupConfig;
  mac_msg->UE_NR_Capability = rrc->UECap.UE_NR_Capability;
  mac_msg->hfn = rrc->current_hfn;
  mac_msg->frame = rrc->current_frame;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
  return true;
}

static bool nr_rrc_process_reconfiguration_v1530(NR_UE_RRC_INST_t *rrc, NR_RRCReconfiguration_v1530_IEs_t *rec_1530, int gNB_index)
{
  if (rec_1530->fullConfig) {
    // TODO perform the full configuration procedure as specified in 5.3.5.11 of 331
    RRCLOG_E("RRCReconfiguration includes fullConfig but this is not implemented yet\n");
  }

  bool reconf_with_sync_scc = false;
  const bool dedicatedsib1 = rec_1530->dedicatedSIB1_Delivery != NULL;
  if (rec_1530->masterCellGroup) {
    bool ret = nr_rrc_ue_process_masterCellGroup(rrc,
                                                 rec_1530->masterCellGroup,
                                                 rec_1530->fullConfig,
                                                 gNB_index,
                                                 dedicatedsib1,
                                                 &reconf_with_sync_scc);
    if (!ret)
      return false;
  }
  if (rec_1530->masterKeyUpdate) {
    as_security_key_update(rrc, rec_1530->masterKeyUpdate);
    nr_pdcp_entity_security_keys_and_algos_t sp = get_security_rrc_parameters(rrc, true);
    nr_pdcp_config_set_security(rrc->ue_id, 1, true, &sp);
  }
  NR_UE_RRC_SI_INFO *SI_info = &rrc->perNB[gNB_index].SInfo;
  if (dedicatedsib1) {
    NR_SIB1_t *sib1 = NULL;
    asn_dec_rval_t dec_rval = uper_decode(NULL,
                                          &asn_DEF_NR_SIB1,
                                          (void **)&sib1,
                                          (uint8_t *)rec_1530->dedicatedSIB1_Delivery->buf,
                                          rec_1530->dedicatedSIB1_Delivery->size,
                                          0,
                                          0);
    if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
      RRCLOG_E("dedicatedSIB1-Delivery decode error\n");
      SEQUENCE_free(&asn_DEF_NR_SIB1, sib1, 1);
    } else {
      // mac layer will free sib1
      nr_rrc_process_sib1(rrc, SI_info, sib1, reconf_with_sync_scc);
    }
  }
  if (rec_1530->dedicatedSystemInformationDelivery) {
    NR_SystemInformation_t *si = NULL;
    asn_dec_rval_t dec_rval = uper_decode(NULL,
                                          &asn_DEF_NR_SystemInformation,
                                          (void **)&si,
                                          (uint8_t *)rec_1530->dedicatedSystemInformationDelivery->buf,
                                          rec_1530->dedicatedSystemInformationDelivery->size,
                                          0,
                                          0);
    if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
      RRCLOG_E("dedicatedSystemInformationDelivery decode error\n");
      SEQUENCE_free(&asn_DEF_NR_SystemInformation, si, 1);
    } else {
      RRCLOG_I("Decoding dedicatedSystemInformationDelivery\n");
      nr_decode_SI(SI_info, si, rrc, rrc->current_hfn, rrc->current_frame);
    }
  }
  if (rec_1530->otherConfig) {
    // TODO perform the other configuration procedure as specified in 5.3.5.9
    RRCLOG_E("RRCReconfiguration includes otherConfig but this is not handled yet\n");
  }
  NR_RRCReconfiguration_v1540_IEs_t *rec_1540 = rec_1530->nonCriticalExtension;
  if (rec_1540) {
    NR_RRCReconfiguration_v1560_IEs_t *rec_1560 = rec_1540->nonCriticalExtension;
    if (rec_1560) {
      if (rec_1560->sk_Counter) {
        // TODO perform AS security key update procedure as specified in 5.3.5.7
        RRCLOG_E("RRCReconfiguration includes sk-Counter but this is not implemented yet\n");
      }
      if (rec_1560->mrdc_SecondaryCellGroupConfig) {
        // TODO perform handling of mrdc-SecondaryCellGroupConfig as specified in 5.3.5.3
        RRCLOG_E("RRCReconfiguration includes mrdc-SecondaryCellGroupConfig but this is not handled yet\n");
      }
      if (rec_1560->radioBearerConfig2) {
        NR_RadioBearerConfig_t *RadioBearerConfig = NULL;
        asn_dec_rval_t dec_rval = uper_decode(NULL,
                                              &asn_DEF_NR_RadioBearerConfig,
                                              (void **)&RadioBearerConfig,
                                              (uint8_t *)rec_1560->radioBearerConfig2->buf,
                                              rec_1560->radioBearerConfig2->size,
                                              0,
                                              0);
        if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
          RRCLOG_E("radioBearerConfig2 decode error\n");
          SEQUENCE_free(&asn_DEF_NR_RadioBearerConfig, RadioBearerConfig, 1);
        } else
          nr_rrc_ue_process_RadioBearerConfig(rrc, RadioBearerConfig);
      }
    }
  }
  return dedicatedsib1;
}

static void handle_meas_reporting_remove(rrcPerNB_t *rrc, int id, NR_UE_Timers_Constants_t *timers)
{
  // remove the measurement reporting entry for this measId if included
  asn1cFreeStruc(asn_DEF_NR_VarMeasReport, rrc->MeasReport[id]);
  // TODO stop the periodical reporting timer or timer T321, whichever is running,
  // and reset the associated information (e.g. timeToTrigger) for this measId
  nr_timer_stop(&timers->T321);

  l3_measurements_t *l3_measurements = &rrc->l3_measurements;
  meas_report_params_t *params = &l3_measurements->meas_report[id];

  nr_timer_stop(&params->TA2);
  nr_timer_stop(&params->TA3);
  nr_timer_stop(&params->periodic_report_timer);

  memset(params, 0, sizeof(*params));
}

static void nr_rrc_release_meas_config(NR_UE_RRC_INST_t *rrc)
{
  for (int n = 0; n < NB_CNX_UE; n++) {
    rrcPerNB_t *nb = &rrc->perNB[n];
    for (int id = 0; id < NR_MAX_MEAS_ID; id++) {
      handle_meas_reporting_remove(nb, id, &rrc->timers_and_constants);
      asn1cFreeStruc(asn_DEF_NR_MeasIdToAddMod, nb->MeasId[id]);
    }
    for (int id = 0; id < NR_MAX_MEAS_OBJ; id++)
      asn1cFreeStruc(asn_DEF_NR_MeasObjectToAddMod, nb->MeasObj[id]);
    for (int id = 0; id < NR_MAX_MEAS_CONFIG; id++)
      asn1cFreeStruc(asn_DEF_NR_ReportConfigToAddMod, nb->ReportConfig[id]);
    for (int id = 0; id < MAX_QUANTITY_CONFIG; id++)
      asn1cFreeStruc(asn_DEF_NR_QuantityConfigNR, nb->QuantityConfig[id]);
    asn1cFreeStruc(asn_DEF_NR_MeasGapConfig, nb->measGapConfig);
    nb->s_measure = 0;
    nb->l3_measurements = (l3_measurements_t){0};
    nb->l3_measurements.ssb_filter_coeff_rsrp = 1.0f;
    nb->l3_measurements.csi_RS_filter_coeff_rsrp = 1.0f;
  }
}

static void handle_measobj_remove(rrcPerNB_t *rrc, struct NR_MeasObjectToRemoveList *remove_list, NR_UE_Timers_Constants_t *timers)
{
  // section 5.5.2.4 in 38.331
  for (int i = 0; i < remove_list->list.count; i++) {
    // for each measObjectId included in the received measObjectToRemoveList
    // that is part of measObjectList in the configuration
    NR_MeasObjectId_t id = *remove_list->list.array[i];
    if (rrc->MeasObj[id - 1]) {
      // remove the entry with the matching measObjectId from the measObjectList
      asn1cFreeStruc(asn_DEF_NR_MeasObjectToAddMod, rrc->MeasObj[id - 1]);
      // remove all measId associated with this measObjectId from the measIdList
      for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
        if (rrc->MeasId[j] && rrc->MeasId[j]->measObjectId == id) {
          asn1cFreeStruc(asn_DEF_NR_MeasIdToAddMod, rrc->MeasId[j]);
          handle_meas_reporting_remove(rrc, j, timers);
        }
      }
    }
  }
}

static void update_ssb_configmob(NR_SSB_ConfigMobility_t *source, NR_SSB_ConfigMobility_t *target)
{
  if (source->ssb_ToMeasure)
    HANDLE_SETUPRELEASE_IE(target->ssb_ToMeasure, source->ssb_ToMeasure, NR_SSB_ToMeasure_t, asn_DEF_NR_SSB_ToMeasure);
  target->deriveSSB_IndexFromCell = source->deriveSSB_IndexFromCell;
  if (source->ss_RSSI_Measurement)
    UPDATE_IE(target->ss_RSSI_Measurement, source->ss_RSSI_Measurement, NR_SS_RSSI_Measurement_t);
}

static void update_nr_measobj(NR_MeasObjectNR_t *source, NR_MeasObjectNR_t *target)
{
  UPDATE_IE(target->ssbFrequency, source->ssbFrequency, NR_ARFCN_ValueNR_t);
  UPDATE_IE(target->ssbSubcarrierSpacing, source->ssbSubcarrierSpacing, NR_SubcarrierSpacing_t);
  UPDATE_IE(target->smtc1, source->smtc1, NR_SSB_MTC_t);
  if (source->smtc2) {
    if (!target->smtc2)
      target->smtc2 = calloc_or_fail(1, sizeof(*target->smtc2));
    target->smtc2->periodicity = source->smtc2->periodicity;
    if (source->smtc2->pci_List)
      UPDATE_IE(target->smtc2->pci_List, source->smtc2->pci_List, struct NR_SSB_MTC2__pci_List);
  }
  else
    asn1cFreeStruc(asn_DEF_NR_SSB_MTC2, target->smtc2);
  UPDATE_IE(target->refFreqCSI_RS, source->refFreqCSI_RS, NR_ARFCN_ValueNR_t);
  if (source->referenceSignalConfig.ssb_ConfigMobility) {
    if (!target->referenceSignalConfig.ssb_ConfigMobility)
      target->referenceSignalConfig.ssb_ConfigMobility =
          calloc_or_fail(1, sizeof(*target->referenceSignalConfig.ssb_ConfigMobility));
    update_ssb_configmob(source->referenceSignalConfig.ssb_ConfigMobility, target->referenceSignalConfig.ssb_ConfigMobility);
  }
  UPDATE_IE(target->absThreshSS_BlocksConsolidation, source->absThreshSS_BlocksConsolidation, NR_ThresholdNR_t);
  UPDATE_IE(target->absThreshCSI_RS_Consolidation, source->absThreshCSI_RS_Consolidation, NR_ThresholdNR_t);
  UPDATE_IE(target->nrofSS_BlocksToAverage, source->nrofSS_BlocksToAverage, long);
  UPDATE_IE(target->nrofCSI_RS_ResourcesToAverage, source->nrofCSI_RS_ResourcesToAverage, long);
  target->quantityConfigIndex = source->quantityConfigIndex;
  UPDATE_NP_IE(target->offsetMO, source->offsetMO, NR_Q_OffsetRangeList_t);
  if (source->cellsToRemoveList) {
    RELEASE_IE_FROMLIST(source->cellsToRemoveList, target->cellsToAddModList, physCellId);
  }
  if (source->cellsToAddModList) {
    if (!target->cellsToAddModList)
      target->cellsToAddModList = calloc(1, sizeof(*target->cellsToAddModList));
    ADDMOD_IE_FROMLIST(source->cellsToAddModList, target->cellsToAddModList, physCellId, NR_CellsToAddMod_t);
  }
  if (source->excludedCellsToRemoveList) {
    RELEASE_IE_FROMLIST(source->excludedCellsToRemoveList, target->excludedCellsToAddModList, pci_RangeIndex);
  }
  if (source->excludedCellsToAddModList) {
    if (!target->excludedCellsToAddModList)
      target->excludedCellsToAddModList = calloc(1, sizeof(*target->excludedCellsToAddModList));
    ADDMOD_IE_FROMLIST(source->excludedCellsToAddModList, target->excludedCellsToAddModList, pci_RangeIndex, NR_PCI_RangeElement_t);
  }
  if (source->allowedCellsToRemoveList) {
    RELEASE_IE_FROMLIST(source->allowedCellsToRemoveList, target->allowedCellsToAddModList, pci_RangeIndex);
  }
  if (source->allowedCellsToAddModList) {
    if (!target->allowedCellsToAddModList)
      target->allowedCellsToAddModList = calloc(1, sizeof(*target->allowedCellsToAddModList));
    ADDMOD_IE_FROMLIST(source->allowedCellsToAddModList, target->allowedCellsToAddModList, pci_RangeIndex, NR_PCI_RangeElement_t);
  }
  if (source->ext1) {
    if (!target->ext1)
      target->ext1 = calloc_or_fail(1, sizeof(*target->ext1));
    UPDATE_IE(target->ext1->freqBandIndicatorNR, source->ext1->freqBandIndicatorNR, NR_FreqBandIndicatorNR_t);
    UPDATE_IE(target->ext1->measCycleSCell, source->ext1->measCycleSCell, long);
  }
}

static void handle_measobj_addmod(rrcPerNB_t *rrc, struct NR_MeasObjectToAddModList *addmod_list)
{
  // section 5.5.2.5 in 38.331
  for (int i = 0; i < addmod_list->list.count; i++) {
    NR_MeasObjectToAddMod_t *measObj = addmod_list->list.array[i];
    if (measObj->measObject.present != NR_MeasObjectToAddMod__measObject_PR_measObjectNR) {
      LOG_E(NR_RRC, "Cannot handle MeasObjt other than NR\n");
      continue;
    }
    NR_MeasObjectId_t id = measObj->measObjectId;
    if (rrc->MeasObj[id]) {
      update_nr_measobj(measObj->measObject.choice.measObjectNR, rrc->MeasObj[id]->measObject.choice.measObjectNR);
    }
    else {
      // add a new entry for the received measObject to the measObjectList
      UPDATE_IE(rrc->MeasObj[id], addmod_list->list.array[i], NR_MeasObjectToAddMod_t);
    }
  }
}

static void handle_reportconfig_remove(rrcPerNB_t *rrc,
                                       struct NR_ReportConfigToRemoveList *remove_list,
                                       NR_UE_Timers_Constants_t *timers)
{
  for (int i = 0; i < remove_list->list.count; i++) {
    NR_ReportConfigId_t id = *remove_list->list.array[i];
    // remove the entry with the matching reportConfigId from the reportConfigList
    asn1cFreeStruc(asn_DEF_NR_ReportConfigToAddMod, rrc->ReportConfig[id]);
    for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
      if (rrc->MeasId[j] && rrc->MeasId[j]->reportConfigId == id) {
        // remove all measId associated with the reportConfigId from the measIdList
        asn1cFreeStruc(asn_DEF_NR_MeasIdToAddMod, rrc->MeasId[j]);
        handle_meas_reporting_remove(rrc, j, timers);
      }
    }
  }
}

static void handle_reportconfig_addmod(rrcPerNB_t *rrc,
                                       struct NR_ReportConfigToAddModList *addmod_list,
                                       NR_UE_Timers_Constants_t *timers)
{
  for (int i = 0; i < addmod_list->list.count; i++) {
    NR_ReportConfigToAddMod_t *rep = addmod_list->list.array[i];
    if (rep->reportConfig.present != NR_ReportConfigToAddMod__reportConfig_PR_reportConfigNR) {
      LOG_E(NR_RRC, "Cannot handle reportConfig type other than NR\n");
      continue;
    }
    NR_ReportConfigId_t id = rep->reportConfigId;
    if (rrc->ReportConfig[id]) {
      for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
        // for each measId associated with this reportConfigId included in the measIdList
        if (rrc->MeasId[j] && rrc->MeasId[j]->reportConfigId == id)
          handle_meas_reporting_remove(rrc, j, timers);
      }
    }
    UPDATE_IE(rrc->ReportConfig[id], addmod_list->list.array[i], NR_ReportConfigToAddMod_t);
  }
}

static void handle_quantityconfig(rrcPerNB_t *rrc, NR_QuantityConfig_t *quantityConfig, NR_UE_Timers_Constants_t *timers)
{
  if (quantityConfig->quantityConfigNR_List) {
    for (int i = 0; i < quantityConfig->quantityConfigNR_List->list.count; i++) {
      NR_QuantityConfigNR_t *quantityNR = quantityConfig->quantityConfigNR_List->list.array[i];
      if (!rrc->QuantityConfig[i])
        rrc->QuantityConfig[i] = calloc(1, sizeof(*rrc->QuantityConfig[i]));
      UPDATE_NP_IE(rrc->QuantityConfig[i]->quantityConfigCell, quantityNR->quantityConfigCell, NR_QuantityConfigRS_t);
      // TODO: It remains to compute ssb_filter_coeff_rsrp and csi_RS_filter_coeff_rsrp for multiple quantityConfig
      // TS 38.331 - 5.5.3.2 Layer 3 filtering
      NR_QuantityConfigRS_t *qcc = &rrc->QuantityConfig[i]->quantityConfigCell;
      l3_measurements_t *l3_measurements = &rrc->l3_measurements;
      if (qcc->ssb_FilterConfig.filterCoefficientRSRP)
        l3_measurements->ssb_filter_coeff_rsrp = 1. / pow(2, (*qcc->ssb_FilterConfig.filterCoefficientRSRP) / 4);
      if (qcc->csi_RS_FilterConfig.filterCoefficientRSRP)
        l3_measurements->csi_RS_filter_coeff_rsrp = 1. / pow(2, (*qcc->csi_RS_FilterConfig.filterCoefficientRSRP) / 4);
      if (quantityNR->quantityConfigRS_Index)
        UPDATE_IE(rrc->QuantityConfig[i]->quantityConfigRS_Index, quantityNR->quantityConfigRS_Index, struct NR_QuantityConfigRS);
    }
  }
  for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
    // for each measId included in the measIdList
    if (rrc->MeasId[j])
      handle_meas_reporting_remove(rrc, j, timers);
  }
}

static void handle_measid_remove(rrcPerNB_t *rrc, struct NR_MeasIdToRemoveList *remove_list, NR_UE_Timers_Constants_t *timers)
{
  for (int i = 0; i < remove_list->list.count; i++) {
    NR_MeasId_t id = *remove_list->list.array[i];
    if (rrc->MeasId[id]) {
      asn1cFreeStruc(asn_DEF_NR_MeasIdToAddMod, rrc->MeasId[id]);
      handle_meas_reporting_remove(rrc, id, timers);
    }
  }
}

static long get_measurement_report_interval_ms(NR_ReportInterval_t interval)
{
  switch (interval) {
    case NR_ReportInterval_ms120:
      return 120;
    case NR_ReportInterval_ms240:
      return 240;
    case NR_ReportInterval_ms480:
      return 480;
    case NR_ReportInterval_ms640:
      return 640;
    case NR_ReportInterval_ms1024:
      return 1024;
    case NR_ReportInterval_ms2048:
      return 2048;
    case NR_ReportInterval_ms5120:
      return 5120;
    case NR_ReportInterval_ms10240:
      return 10240;
    case NR_ReportInterval_min1:
      return 60000;
    case NR_ReportInterval_min6:
      return 360000;
    case NR_ReportInterval_min12:
      return 720000;
    case NR_ReportInterval_min30:
      return 1800000;
    default:
      return 1024;
  }
}

static int get_meas_id(rrcPerNB_t *rrcNB, int report_config_id)
{
  for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
    NR_MeasIdToAddMod_t *meas_id_toAddMod = rrcNB->MeasId[j];
    if (meas_id_toAddMod && meas_id_toAddMod->reportConfigId == report_config_id)
      return meas_id_toAddMod->measId;
  }
  return -1;
}

static void setup_meas_params(meas_report_params_t *params,
                              long trigger_quantity,
                              long rs_type,
                              long report_amount,
                              bool is_infinity,
                              NR_ReportInterval_t report_interval,
                              bool neighbor_cell_valid)
{
  params->trigger_quantity = trigger_quantity;
  params->rs_type = rs_type;
  params->reports_sent = 0;
  params->max_reports = is_infinity ? INT_MAX : (1 << report_amount);
  params->report_interval_ms = get_measurement_report_interval_ms(report_interval);
  params->neighbor_cell_valid = neighbor_cell_valid;
  params->report_rsrp = false;
}

static void start_periodical_report(rrcPerNB_t *rrc, NR_PeriodicalReportConfig_t *periodical_config, int meas_id)
{
  l3_measurements_t *l3_measurements = &rrc->l3_measurements;
  meas_report_params_t *params = &l3_measurements->meas_report[meas_id];

  setup_meas_params(params,
                    0,
                    periodical_config->rsType,
                    periodical_config->reportAmount,
                    periodical_config->reportAmount == NR_PeriodicalReportConfig__reportAmount_infinity,
                    periodical_config->reportInterval,
                    true);

  params->reports_sent = 0;
  params->report_rsrp = periodical_config->reportQuantityCell.rsrp;
  params->max_report_cells = periodical_config->maxReportCells;

  // Start the periodic report timer
  if (!nr_timer_is_active(&params->periodic_report_timer)) {
    nr_timer_setup(&params->periodic_report_timer, params->report_interval_ms, 10);
    nr_timer_start(&params->periodic_report_timer);
  }
}

static void extract_neighbor_cells_from_measObject(NR_MeasObjectNR_t *obj_nr,
                                                   nr_neighbor_cell_info_t *neighbor_cells,
                                                   int *num_neighbors,
                                                   int max_neighbors,
                                                   uint32_t serving_cell_pci)
{
  uint32_t ssb_freq = 0;
  if (obj_nr->ssbFrequency) {
    ssb_freq = *obj_nr->ssbFrequency;
  }

  if (obj_nr->cellsToAddModList) {
    NR_CellsToAddModList_t *cellsToAddModList = obj_nr->cellsToAddModList;
    for (int j = 0; j < cellsToAddModList->list.count; j++) {
      NR_CellsToAddMod_t *cell = cellsToAddModList->list.array[j];
      // Skip serving cell
      if (cell->physCellId == serving_cell_pci) {
        LOG_D(NR_RRC, "Skipping serving cell PCI %d from neighbor list\n", serving_cell_pci);
        continue;
      }
      // Check if this cell is already in the list
      bool is_duplicate = false;
      for (int k = 0; k < *num_neighbors; k++) {
        if (neighbor_cells[k].Nid_cell == cell->physCellId && neighbor_cells[k].ssb_freq == ssb_freq) {
          is_duplicate = true;
          break;
        }
      }
      if (!is_duplicate) {
        if (*num_neighbors < max_neighbors) {
          neighbor_cells[*num_neighbors].Nid_cell = cell->physCellId;
          neighbor_cells[*num_neighbors].ssb_freq = ssb_freq;
          neighbor_cells[*num_neighbors].active = 1;
          (*num_neighbors)++;
        } else {
          LOG_W(NR_RRC, "More than %d neighboring cells configured, ignoring excess cells!\n", max_neighbors);
          break;
        }
      }
    }
  } else {
    // We do not know PCI of neighboring cells, so we do blind search
    bool is_duplicate = false;
    for (int k = 0; k < *num_neighbors; k++) {
      if (neighbor_cells[k].Nid_cell == -1 && neighbor_cells[k].ssb_freq == ssb_freq) {
        is_duplicate = true;
        break;
      }
    }
    if (!is_duplicate && *num_neighbors < max_neighbors) {
      neighbor_cells[*num_neighbors].Nid_cell = -1;
      neighbor_cells[*num_neighbors].ssb_freq = ssb_freq;
      neighbor_cells[*num_neighbors].active = 1;
      (*num_neighbors)++;
    }
  }

  // 3GPP TS 38.133
  // 9.2 NR intra-frequency measurements - 9.2.1 Introduction
  // The UE shall be able to identify new intra-frequency cells (...) even if no explicit neighbour list with physical layer cell
  // identities is provided
  if (*num_neighbors == 0) {
    neighbor_cells[*num_neighbors].Nid_cell = -1;
    neighbor_cells[*num_neighbors].ssb_freq = ssb_freq;
    neighbor_cells[*num_neighbors].active = 1;
    (*num_neighbors)++;
  }
}

static void handle_measid_addmod(rrcPerNB_t *rrc,
                                 struct NR_MeasIdToAddModList *addmod_list,
                                 NR_UE_Timers_Constants_t *timers,
                                 nr_neighbor_cell_info_t *neighbor_cells,
                                 int *num_neighbors,
                                 int max_neighbors,
                                 uint32_t serving_cell_pci)
{
  for (int i = 0; i < addmod_list->list.count; i++) {
    NR_MeasId_t id = addmod_list->list.array[i]->measId;
    NR_ReportConfigId_t reportId = addmod_list->list.array[i]->reportConfigId;
    NR_MeasObjectId_t measObjectId = addmod_list->list.array[i]->measObjectId;
    UPDATE_IE(rrc->MeasId[id], addmod_list->list.array[i], NR_MeasIdToAddMod_t);
    handle_meas_reporting_remove(rrc, id, timers);
    if (rrc->ReportConfig[reportId]) {
      NR_ReportConfigToAddMod_t *report = rrc->ReportConfig[reportId];
      AssertFatal(report->reportConfig.present == NR_ReportConfigToAddMod__reportConfig_PR_reportConfigNR,
                  "Only NR config report is supported\n");
      NR_ReportConfigNR_t *reportNR = report->reportConfig.choice.reportConfigNR;
      // if the reportType is set to reportCGI in the reportConfig associated with this measId
      if (reportNR->reportType.present == NR_ReportConfigNR__reportType_PR_reportCGI) {
        if (rrc->MeasObj[measObjectId]) {
          if (rrc->MeasObj[measObjectId]->measObject.present == NR_MeasObjectToAddMod__measObject_PR_measObjectNR) {
            NR_MeasObjectNR_t *obj_nr = rrc->MeasObj[measObjectId]->measObject.choice.measObjectNR;
            NR_ARFCN_ValueNR_t freq = 0;
            if (obj_nr->ssbFrequency)
              freq = *obj_nr->ssbFrequency;
            else if (obj_nr->refFreqCSI_RS)
              freq = *obj_nr->refFreqCSI_RS;
            AssertFatal(freq > 0, "Invalid ARFCN frequency for this measurement object\n");
            if (get_freq_range_from_arfcn(freq) == FR2)
              nr_timer_setup(&timers->T321, 16000, 10); // 16 seconds for FR2
            else
              nr_timer_setup(&timers->T321, 2000, 10); // 2 seconds for FR1
          }
          else // EUTRA
            nr_timer_setup(&timers->T321, 1000, 10); // 1 second for EUTRA
          nr_timer_start(&timers->T321);
        }
      }
      // Check for A3 event and extract neighbor cell info for MAC
      else if (reportNR->reportType.present == NR_ReportConfigNR__reportType_PR_eventTriggered) {
        NR_EventTriggerConfig_t *eventTriggerConfig = reportNR->reportType.choice.eventTriggered;
        if (eventTriggerConfig->eventId.present == NR_EventTriggerConfig__eventId_PR_eventA3) {
          if (rrc->MeasObj[measObjectId]
              && rrc->MeasObj[measObjectId]->measObject.present == NR_MeasObjectToAddMod__measObject_PR_measObjectNR) {
            NR_MeasObjectNR_t *obj_nr = rrc->MeasObj[measObjectId]->measObject.choice.measObjectNR;
            extract_neighbor_cells_from_measObject(obj_nr, neighbor_cells, num_neighbors, max_neighbors, serving_cell_pci);
          }
        }
      }
      // Check for periodical report and extract neighbor cell info for MAC
      else if (reportNR->reportType.present == NR_ReportConfigNR__reportType_PR_periodical) {
        NR_PeriodicalReportConfig_t *periodical_config = reportNR->reportType.choice.periodical;
        if (rrc->MeasObj[measObjectId]
            && rrc->MeasObj[measObjectId]->measObject.present == NR_MeasObjectToAddMod__measObject_PR_measObjectNR) {
          NR_MeasObjectNR_t *obj_nr = rrc->MeasObj[measObjectId]->measObject.choice.measObjectNR;
          extract_neighbor_cells_from_measObject(obj_nr, neighbor_cells, num_neighbors, max_neighbors, serving_cell_pci);
          // Initialize periodical report (first report sent when measurements available)
          start_periodical_report(rrc, periodical_config, id);
        }
      }
    }
  }
}

static void nr_rrc_ue_process_measConfig(rrcPerNB_t *rrc,
                                         NR_MeasConfig_t *const measConfig,
                                         NR_UE_Timers_Constants_t *timers,
                                         nr_neighbor_cell_info_t *neighbor_cells,
                                         int *num_neighbors,
                                         int max_neighbors,
                                         uint32_t serving_cell_pci)
{
  if (measConfig->measObjectToRemoveList)
    handle_measobj_remove(rrc, measConfig->measObjectToRemoveList, timers);

  if (measConfig->measObjectToAddModList)
    handle_measobj_addmod(rrc, measConfig->measObjectToAddModList);

  if (measConfig->reportConfigToRemoveList)
    handle_reportconfig_remove(rrc, measConfig->reportConfigToRemoveList, timers);

  if (measConfig->reportConfigToAddModList)
    handle_reportconfig_addmod(rrc, measConfig->reportConfigToAddModList, timers);

  if (measConfig->quantityConfig)
    handle_quantityconfig(rrc, measConfig->quantityConfig, timers);

  if (measConfig->measIdToRemoveList)
    handle_measid_remove(rrc, measConfig->measIdToRemoveList, timers);

  if (measConfig->measIdToAddModList)
    handle_measid_addmod(rrc, measConfig->measIdToAddModList, timers, neighbor_cells, num_neighbors, max_neighbors, serving_cell_pci);

  LOG_W(NR_RRC, "Measurement gaps not yet supported!\n");

  if (measConfig->s_MeasureConfig) {
    if (measConfig->s_MeasureConfig->present == NR_MeasConfig__s_MeasureConfig_PR_ssb_RSRP) {
      rrc->s_measure = measConfig->s_MeasureConfig->choice.ssb_RSRP;
    } else if (measConfig->s_MeasureConfig->present == NR_MeasConfig__s_MeasureConfig_PR_csi_RSRP) {
      rrc->s_measure = measConfig->s_MeasureConfig->choice.csi_RSRP;
    }
  }
}

static void nr_rrc_ue_process_rrcReconfiguration(NR_UE_RRC_INST_t *rrc, int gNB_index, NR_RRCReconfiguration_t *reconfiguration)
{
  rrcPerNB_t *rrcNB = rrc->perNB + gNB_index;

  switch (reconfiguration->criticalExtensions.present) {
    case NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration: {
      NR_RRCReconfiguration_IEs_t *ie = reconfiguration->criticalExtensions.choice.rrcReconfiguration;

      bool dedicatedsib1 = false;
      if (ie->nonCriticalExtension)
        dedicatedsib1 = nr_rrc_process_reconfiguration_v1530(rrc, ie->nonCriticalExtension, gNB_index);
      if (rrc->ho_source.valid)
        rrc->ho_source.meas_config = ie->measConfig != NULL;

      if (ie->radioBearerConfig) {
        RRCLOG_I("RRCReconfiguration includes radio Bearer Configuration\n");
        nr_rrc_ue_process_RadioBearerConfig(rrc, ie->radioBearerConfig);
        if (LOG_DEBUGFLAG(DEBUG_ASN1))
          xer_fprint(stdout, &asn_DEF_NR_RadioBearerConfig, (const void *)ie->radioBearerConfig);
      }

      /** @note This triggers PDU Session Establishment Accept which sets up the TUN interface.
       * SDAP entity is a pre-requisite, therefore the radioBearerConfig has to be processed
       * (in nr_rrc_ue_process_RadioBearerConfig, add_drb) early enough, or it may cause a race condition in SDAP. */
      if (ie->nonCriticalExtension)
        nr_rrc_process_dedicatedNAS_MessageList(rrc, ie->nonCriticalExtension);

      if (ie->secondaryCellGroup) {
        NR_CellGroupConfig_t *cellGroupConfig = NULL;
        asn_dec_rval_t dec_rval = uper_decode(NULL,
                                              &asn_DEF_NR_CellGroupConfig, // might be added prefix later
                                              (void **)&cellGroupConfig,
                                              (uint8_t *)ie->secondaryCellGroup->buf,
                                              ie->secondaryCellGroup->size,
                                              0,
                                              0);
        if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
          RRCLOG_E("NR_CellGroupConfig decode error, size: %ld\n", ie->secondaryCellGroup->size);
          // free the memory
          SEQUENCE_free(&asn_DEF_NR_CellGroupConfig, (void *)cellGroupConfig, 1);
          // if the ASN1 decoding fails for the received CellGroup configuration
          // we call the function for RLF (re-establishment if security is activated, going to IDLE otherwise)
          handle_rlf_detection(rrc);
        } else {
          if (LOG_DEBUGFLAG(DEBUG_ASN1))
            xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, (const void *) cellGroupConfig);

          bool ret = nr_rrc_cellgroup_configuration(rrc, cellGroupConfig, gNB_index, dedicatedsib1, NULL);
          AssertFatal(ret, "CellGroup has wrong configuration for the UE. Unexpected\n");
          AssertFatal(!IS_SA_MODE(get_softmodem_params()), "secondaryCellGroup only used in NSA for now\n");
          nr_mac_rrc_message_t rrc_msg = {0};
          rrc_msg.payload_type = NR_MAC_RRC_CONFIG_CG;
          nr_mac_rrc_config_cg_t *config_cg = &rrc_msg.payload.config_cg;
          config_cg->cellGroupConfig = cellGroupConfig;
          config_cg->UE_NR_Capability = rrc->UECap.UE_NR_Capability;
          config_cg->hfn = rrc->current_hfn;
          config_cg->frame = rrc->current_frame;
          nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
        }
      }
      if (ie->measConfig) {
        RRCLOG_I("RRCReconfiguration includes Measurement Configuration\n");
        nr_neighbor_cell_info_t neighbor_cells[NUMBER_OF_NEIGHBORING_CELLS_MAX];
        int num_neighbors = 0;
        nr_rrc_ue_process_measConfig(rrcNB, ie->measConfig, &rrc->timers_and_constants, neighbor_cells, &num_neighbors, NUMBER_OF_NEIGHBORING_CELLS_MAX, rrc->phyCellID);
        if (num_neighbors > 0) {
          nr_rrc_mac_config_req_meas(rrc->ue_id, neighbor_cells, num_neighbors);
        }
      }
      if (ie->lateNonCriticalExtension) {
        RRCLOG_E("RRCReconfiguration includes lateNonCriticalExtension. Not handled.\n");
      }
    } break;
    case NR_RRCReconfiguration__criticalExtensions_PR_NOTHING:
    case NR_RRCReconfiguration__criticalExtensions_PR_criticalExtensionsFuture:
    default:
      break;
  }
  return;
}

void process_nsa_message(NR_UE_RRC_INST_t *rrc, nsa_message_t nsa_message_type, void *message, int msg_len)
{
  switch (nsa_message_type) {
    case nr_SecondaryCellGroupConfig_r15: {
      NR_RRCReconfiguration_t *RRCReconfiguration=NULL;
      asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                     &asn_DEF_NR_RRCReconfiguration,
                                                     (void **)&RRCReconfiguration,
                                                     (uint8_t *)message,
                                                     msg_len);
      if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
        RRCLOG_E("NR_RRCReconfiguration decode error\n");
        // free the memory
        SEQUENCE_free(&asn_DEF_NR_RRCReconfiguration, RRCReconfiguration, 1);
        return;
      }
      nr_rrc_ue_process_rrcReconfiguration(rrc, 0, RRCReconfiguration);
      ASN_STRUCT_FREE(asn_DEF_NR_RRCReconfiguration, RRCReconfiguration);
    }
    break;

    case nr_RadioBearerConfigX_r15: {
      NR_RadioBearerConfig_t *RadioBearerConfig=NULL;
      asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                     &asn_DEF_NR_RadioBearerConfig,
                                                     (void **)&RadioBearerConfig,
                                                     (uint8_t *)message,
                                                     msg_len);
      if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
        RRCLOG_E("NR_RadioBearerConfig decode error\n");
        // free the memory
        SEQUENCE_free( &asn_DEF_NR_RadioBearerConfig, RadioBearerConfig, 1 );
        return;
      }
      RRCLOG_D("Calling nr_rrc_ue_process_RadioBearerConfig()with: e_rab_id = %ld, drbID = %ld, cipher_algo = %ld, key = %ld \n",
               RadioBearerConfig->drb_ToAddModList->list.array[0]->cnAssociation->choice.eps_BearerIdentity,
               RadioBearerConfig->drb_ToAddModList->list.array[0]->drb_Identity,
               RadioBearerConfig->securityConfig->securityAlgorithmConfig->cipheringAlgorithm,
               *RadioBearerConfig->securityConfig->keyToUse);
      nr_rrc_ue_process_RadioBearerConfig(rrc, RadioBearerConfig);
      if (LOG_DEBUGFLAG(DEBUG_ASN1))
        xer_fprint(stdout, &asn_DEF_NR_RadioBearerConfig, (const void *)RadioBearerConfig);
      ASN_STRUCT_FREE(asn_DEF_NR_RadioBearerConfig, RadioBearerConfig);
    }
    break;
    
    default:
      AssertFatal(1==0,"Unknown message %d\n",nsa_message_type);
      break;
  }
}

/**
 * @brief Verify UE capabilities parameters against CL-fed params
 *        (e.g. number of physical TX antennas)
 */
static bool verify_ue_cap(NR_UE_NR_Capability_t *UE_NR_Capability, int nb_antennas_tx)
{
  NR_FeatureSetUplink_t *ul_feature_setup = UE_NR_Capability->featureSets->featureSetsUplink->list.array[0];
  int srs_ant_ports = 1 << ul_feature_setup->supportedSRS_Resources->maxNumberSRS_Ports_PerResource;
  AssertFatal(srs_ant_ports <= nb_antennas_tx, "SRS antenna ports (%d) > nb_antennas_tx (%d)\n", srs_ant_ports, nb_antennas_tx);
  return true;
}

NR_UE_RRC_INST_t* nr_rrc_init_ue(char* uecap_file, int instance_id, int num_ant_tx)
{
  AssertFatal(instance_id < MAX_NUM_NR_UE_INST, "RRC instance %d out of bounds\n", instance_id);
  AssertFatal(NR_UE_rrc_inst[instance_id] == NULL, "RRC instance %d already initialized\n", instance_id);
  NR_UE_rrc_inst[instance_id] = calloc_or_fail(1, sizeof(NR_UE_RRC_INST_t));
  NR_UE_RRC_INST_t *rrc = NR_UE_rrc_inst[instance_id];
  rrc->ue_id = instance_id;
  // fill UE-NR-Capability @ UE-CapabilityRAT-Container here.
  rrc->selected_plmn_identity = 1;
  rrc->ra_trigger = RA_NOT_RUNNING;
  rrc->dl_bwp_id = 0;
  rrc->ul_bwp_id = 0;
  rrc->as_security_activated = false;
  rrc->sched_reconfsync_sib1 = false;
  rrc->detach_after_release = false;
  rrc->reconfig_after_reestab = false;
  /* 5G-S-TMSI starts unset (sentinel UINT64_MAX). NAS_5GMM_IND populates it on registration */
  rrc->fiveG_S_TMSI = UINT64_MAX;
  rrc->access_barred = false;

  FILE *f = NULL;
  if (uecap_file) {
    f = fopen(uecap_file, "r");
    AssertFatal(f, "could not open UE capabilities file \"%s\": %s\n", uecap_file, strerror(errno));
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    AssertFatal(file_size <= 1024 * 1024,
                "UE Capabilities XER file %s is too large (%ld bytes, max 1MB)\n", uecap_file, file_size);
    char *UE_NR_Capability_xer = malloc_or_fail(file_size);
    size_t size = fread(UE_NR_Capability_xer, 1, file_size, f);
    AssertFatal(size > 0, "UE Capabilities XER file %s: read error\n", uecap_file);
    asn_dec_rval_t dec_rval =
        xer_decode(0, &asn_DEF_NR_UE_NR_Capability, (void *)&rrc->UECap.UE_NR_Capability, UE_NR_Capability_xer, size);
    DevAssert(dec_rval.code == RC_OK);
    free(UE_NR_Capability_xer);
    fclose(f);
    /* Verify consistency of num PHY antennas vs UE Capabilities */
    verify_ue_cap(rrc->UECap.UE_NR_Capability, num_ant_tx);
  }

  memset(&rrc->timers_and_constants, 0, sizeof(rrc->timers_and_constants));
  set_default_timers_and_constants(&rrc->timers_and_constants);

  for (int j = 0; j < NR_NUM_SRB; j++)
    rrc->Srb[j] = RB_NOT_PRESENT;
  for (int j = 1; j <= MAX_DRBS_PER_UE; j++)
    set_DRB_status(rrc, j, RB_NOT_PRESENT);
  // SRB0 activated by default
  rrc->Srb[0] = RB_ESTABLISHED;
  for (int j = 0; j < NR_MAX_NUM_LCID; j++)
    rrc->active_RLC_entity[j] = false;

  for (int i = 0; i < NB_CNX_UE; i++) {
    rrcPerNB_t *ptr = &rrc->perNB[i];
    ptr->SInfo = (NR_UE_RRC_SI_INFO){0};
    ptr->l3_measurements = (l3_measurements_t){0};
    ptr->l3_measurements.ssb_filter_coeff_rsrp = 1.0f;
    ptr->l3_measurements.csi_RS_filter_coeff_rsrp = 1.0f;
    init_SI_timers(&ptr->SInfo);
  }

  init_sidelink(rrc);
  return rrc;
}

bool check_si_validity(NR_UE_RRC_SI_INFO *SI_info, int si_type)
{
  switch (si_type) {
    case NR_SIB_TypeInfo__type_sibType2:
      if (!SI_info->sib2_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType3:
      if (!SI_info->sib3_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType4:
      if (!SI_info->sib4_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType5:
      if (!SI_info->sib5_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType6:
      if (!SI_info->sib6_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType7:
      if (!SI_info->sib7_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType8:
      if (!SI_info->sib8_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType9:
      if (!SI_info->sib9_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType10_v1610:
      if (!SI_info->sib10_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType11_v1610:
      if (!SI_info->sib11_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType12_v1610:
      if (!SI_info->sib12_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType13_v1610:
      if (!SI_info->sib13_validity)
        return false;
      break;
    case NR_SIB_TypeInfo__type_sibType14_v1610:
      if (!SI_info->sib14_validity)
        return false;
      break;
    default :
      AssertFatal(false, "Invalid SIB type %d\n", si_type);
  }
  return true;
}

bool check_si_validity_r17(NR_UE_RRC_SI_INFO_r17 *SI_info, int si_type)
{
  switch (si_type) {
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType15:
      if (!SI_info->sib15_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType16:
      if (!SI_info->sib16_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType17:
      if (!SI_info->sib17_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType18:
      if (!SI_info->sib18_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType19:
      if (!SI_info->sib19_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType20:
      if (!SI_info->sib20_validity)
        return false;
      break;
    case NR_SIB_TypeInfo_v1700__sibType_r17__type1_r17_sibType21:
      if (!SI_info->sib21_validity)
        return false;
      break;
    default :
      AssertFatal(false, "Invalid SIB r17 type %d\n", si_type);
  }
  return true;
}

static int check_si_status(NR_UE_RRC_SI_INFO *SI_info)
{
  // schedule reception of SIB1 if RRC doesn't have it
  if (!SI_info->sib1_validity)
    return 1;
  else {
    for (int j = 0; j < MAX_SI_GROUPS; j++) {
      if (!SI_info->default_otherSI_map[j])
        continue;
      // Check if RRC has configured default SI
      // TODO can be used for on demand SI when (if) implemented
      for (int i = 2; i < 22; i++) {
        if (!((SI_info->default_otherSI_map[j] >> (i - 2)) & 0x01))
          continue;
        // if RRC has no valid version of one of the default configured SI
        // Then schedule reception of otherSI
        if (i < 15) {
          if (!check_si_validity(SI_info, i - 2))
            return 2 + j;
        } else {
          if (!check_si_validity_r17(&SI_info->SInfo_r17, i - 15))
            return 2 + j;
        }
      }
    }
  }
  return 0;
}

/*brief decode BCCH-BCH (MIB) message*/
static void nr_rrc_ue_decode_NR_BCCH_BCH_Message(NR_UE_RRC_INST_t *rrc,
                                                 const uint8_t gNB_index,
                                                 const uint32_t phycellid,
                                                 const long ssb_arfcn,
                                                 uint8_t *const bufferP,
                                                 const uint8_t buffer_len)
{
  // MIB received on the target cell. Now process target ntncfg once new timing is received.
  if (phycellid == rrc->phyCellID && rrc->target_ntncfg) {
    rrc->process_target_ntncfg = true;
  }

  NR_BCCH_BCH_Message_t *bcch_message = NULL;
  const bool cell_changed = rrc->phyCellID != phycellid || rrc->arfcn_ssb != ssb_arfcn;
  if (cell_changed) {
    RRCLOG_I("BCCH update: phyCellID %d->%u, arfcn_ssb %ld->%ld\n", rrc->phyCellID, phycellid, rrc->arfcn_ssb, ssb_arfcn);
  }
  rrc->phyCellID = phycellid;
  rrc->arfcn_ssb = ssb_arfcn;

  asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                 &asn_DEF_NR_BCCH_BCH_Message,
                                                 (void **)&bcch_message,
                                                 (const void *)bufferP,
                                                 buffer_len);

  if ((dec_rval.code != RC_OK) || (dec_rval.consumed == 0)) {
    RRCLOG_E("NR_BCCH_BCH decode error\n");
    return;
  }
  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_BCCH_BCH_Message, (void *)bcch_message);

  NR_UE_RRC_SI_INFO *SI_info = &rrc->perNB[gNB_index].SInfo;
  // Actions following cell selection while T311 is running
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;
  if (nr_timer_is_active(&timers->T311)) {
    nr_timer_stop(&timers->T311);
    rrc->ra_trigger = RRC_CONNECTION_REESTABLISHMENT;
    // stored SIB1 is not valid for another cell
    if (cell_changed) {
      SI_info->sib1_validity = false;
      SI_info->sib_pending = false;
    }

    // apply the default MAC Cell Group configuration
    // (done at MAC by calling nr_ue_mac_default_configs)

    // apply the timeAlignmentTimerCommon included in SIB1
    // not used
  }

  bool barred = rrc->access_barred || bcch_message->message.choice.mib->cellBarred == NR_MIB__cellBarred_barred;
  int get_sib = 0;
  if (IS_SA_MODE(get_softmodem_params())
      && !SI_info->sib_pending
      && bcch_message->message.present == NR_BCCH_BCH_MessageType_PR_mib
      && !barred
      && rrc->nrRrcState != RRC_STATE_DETACH_NR) {
    // to schedule MAC to get SI if required
    get_sib = check_si_status(SI_info);
  }
  if (bcch_message->message.present == NR_BCCH_BCH_MessageType_PR_mib) {
    nr_mac_rrc_message_t rrc_msg = {0};
    rrc_msg.payload_type = NR_MAC_RRC_CONFIG_MIB;
    nr_mac_rrc_config_mib_t *config_mib = &rrc_msg.payload.config_mib;
    config_mib->bcch = bcch_message;
    config_mib->access_barred = barred;
    nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
    if (get_sib) {
      SI_info->sib_pending = true;
      nr_mac_rrc_message_t sib_msg = {0};
      sib_msg.payload_type = NR_MAC_RRC_SCHED_SIB;
      sib_msg.payload.sched_sib.get_sib = get_sib;
      nr_rrc_send_msg_to_mac(rrc, &sib_msg);
    }
  } else {
    RRCLOG_E("RRC-received BCCH message is not a MIB\n");
    ASN_STRUCT_FREE(asn_DEF_NR_BCCH_BCH_Message, bcch_message);
  }
  return;
}

static void nr_rrc_ue_prepare_RRCReestablishmentRequest(NR_UE_RRC_INST_t *rrc)
{
  uint8_t buffer[1024];

  /* Compute shortMAC-I per TS 38.331 §5.3.7.4:
   * over the ASN.1 encoded as per clause 8 VarShortMAC-Input;
   * with the KRRCint key and integrity protection algorithm that was used in the source PCell
   * of the PCell in which the trigger for the re-establishment occurred (other cases);
   * with all input bits for COUNT, BEARER and DIRECTION set to binary ones */
  AssertFatal(rrc->as_security_activated, "RRCReestablishmentRequest requires AS security to be activated\n");
  uint8_t krrcint[16];
  nr_derive_key(RRC_INT_ALG, rrc->integrityProtAlgorithm, rrc->kgnb, krrcint);

  uint8_t var_input[8] = {0};
  /* Write 62 bits MSB-first into var_input */
  uint64_t hi = ((uint64_t)rrc->reestab_source_pci << 54) | (rrc->cell_identity << 18) | ((uint64_t)rrc->reestab_source_crnti << 2);
  var_input[0] = (hi >> 56) & 0xFF;
  var_input[1] = (hi >> 48) & 0xFF;
  var_input[2] = (hi >> 40) & 0xFF;
  var_input[3] = (hi >> 32) & 0xFF;
  var_input[4] = (hi >> 24) & 0xFF;
  var_input[5] = (hi >> 16) & 0xFF;
  var_input[6] = (hi >> 8) & 0xFF;
  var_input[7] = hi & 0xFF;

  stream_security_context_t *ctx = stream_integrity_init(rrc->integrityProtAlgorithm, krrcint);
  AssertFatal(ctx != NULL, "Failed to initialise integrity context for integrityProtAlgorithm %d\n", rrc->integrityProtAlgorithm);
  nas_stream_cipher_t cipher = {
      .context = ctx,
      .count = 0xffffffff,
      .bearer = 0x1f,
      .direction = SECU_DIRECTION_DOWNLINK,
      .message = var_input,
      .blength = 64, /* 8 bytes: 62 bits content + 2 zero padding bits */
  };
  uint8_t mac[4];
  stream_compute_integrity((eia_alg_id_e)rrc->integrityProtAlgorithm, &cipher, mac);
  stream_integrity_free(rrc->integrityProtAlgorithm, ctx);
  uint16_t short_mac_i = ((uint16_t)mac[2] << 8) | mac[3];

  int buf_size = do_RRCReestablishmentRequest(buffer, rrc->reestablishment_cause, rrc->reestab_source_pci, rrc->reestab_source_crnti, short_mac_i);
  nr_rlc_srb_recv_sdu(rrc->ue_id, 0, buffer, buf_size);
}

static void nr_rrc_prepare_msg3_payload(NR_UE_RRC_INST_t *rrc)
{
  if (!IS_SA_MODE(get_softmodem_params()))
    return;
  // Replace CCCH left queued by an abandoned RA before preparing the new request.
  // Msg3 HARQ retransmissions reuse MAC's cached PDU and do not reach this path.
  nr_rlc_reestablish_entity(rrc->ue_id, 0);
  switch (rrc->ra_trigger) {
    case RRC_CONNECTION_SETUP:
      // preparing RRC setup request payload in advance
      nr_rrc_ue_prepare_RRCSetupRequest(rrc);
      break;
    case RRC_CONNECTION_REESTABLISHMENT:
      // preparing MSG3 for re-establishment in advance
      nr_rrc_ue_prepare_RRCReestablishmentRequest(rrc);
      break;
    default:
      AssertFatal(false, "RA trigger not implemented\n");
  }
}

static void nr_rrc_handle_msg3_indication(NR_UE_RRC_INST_t *rrc, rnti_t rnti)
{
  NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
  switch (rrc->ra_trigger) {
    case RRC_CONNECTION_SETUP:
      // After SIB1 is received, prepare RRCConnectionRequest
      rrc->rnti = rnti;
      // start timer T300
      nr_timer_start(&tac->T300);
      break;
    case RRC_CONNECTION_REESTABLISHMENT:
      rrc->rnti = rnti;
      nr_timer_start(&tac->T301);
      int srb_id = 1;
      // re-establish PDCP for SRB1
      // (and suspend integrity protection and ciphering for SRB1)
      nr_pdcp_entity_security_keys_and_algos_t null_security_parameters = {0};
      nr_pdcp_reestablishment(rrc->ue_id, srb_id, true, &null_security_parameters);
      // re-establish RLC for SRB1
      int lc_id = nr_rlc_get_lcid_from_rb(rrc->ue_id, true, 1);
      nr_rlc_reestablish_entity(rrc->ue_id, lc_id);
      // apply the specified configuration defined in 9.2.1 for SRB1
      nr_rlc_reconfigure_entity(rrc->ue_id, lc_id, NULL);
      // resume SRB1
      rrc->Srb[srb_id] = RB_ESTABLISHED;
      nr_mac_rrc_message_t rrc_msg = {0};
      rrc_msg.payload_type = NR_MAC_RRC_RESUME_RB;
      rrc_msg.payload.resume_rb.is_srb = true;
      rrc_msg.payload.resume_rb.rb_id = 1;
      nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
      break;
    case DURING_HANDOVER:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    case NON_SYNCHRONISED:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    case TRANSITION_FROM_RRC_INACTIVE:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    case TO_ESTABLISH_TA:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    case REQUEST_FOR_OTHER_SI:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    case BEAM_FAILURE_RECOVERY:
      AssertFatal(1==0, "ra_trigger not implemented yet!\n");
      break;
    default:
      AssertFatal(1==0, "Invalid ra_trigger value!\n");
      break;
  }
}

static void nr_rrc_ue_decode_NR_BCCH_DL_SCH_Message(NR_UE_RRC_INST_t *rrc,
                                                    const uint8_t gNB_index,
                                                    uint8_t *const Sdu,
                                                    const uint8_t Sdu_len,
                                                    int hfn,
                                                    int frame,
                                                    int slot)
{
  NR_UE_RRC_SI_INFO *SI_info = &rrc->perNB[gNB_index].SInfo;
  SI_info->sib_pending = false;
  if (Sdu_len == 0) // decoding failed in L2
    return;

  NR_BCCH_DL_SCH_Message_t *bcch_message = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                 &asn_DEF_NR_BCCH_DL_SCH_Message,
                                                 (void **)&bcch_message,
                                                 (const void *)Sdu,
                                                 Sdu_len);

  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    RRCLOG_E("Failed to decode BCCH_DLSCH_MESSAGE (%zu bits)\n", dec_rval.consumed);
    log_dump(NR_RRC, Sdu, Sdu_len, LOG_DUMP_CHAR,"   Received bytes:\n");
    // free the memory
    SEQUENCE_free(&asn_DEF_NR_BCCH_DL_SCH_Message, (void *)bcch_message, 1);
    return;
  }

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_BCCH_DL_SCH_Message,(void *)bcch_message);
  }

  if (bcch_message->message.present == NR_BCCH_DL_SCH_MessageType_PR_c1) {
    switch (bcch_message->message.choice.c1->present) {
      case NR_BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1:
        nr_rrc_process_sib1(rrc, SI_info, bcch_message->message.choice.c1->choice.systemInformationBlockType1, false);
        // mac layer will free after usage the sib1
        bcch_message->message.choice.c1->choice.systemInformationBlockType1 = NULL;
        break;
      case NR_BCCH_DL_SCH_MessageType__c1_PR_systemInformation:
        RRCLOG_I("%d:%d Decoding SI\n", frame, slot);
        NR_SystemInformation_t *si = bcch_message->message.choice.c1->choice.systemInformation;
        nr_decode_SI(SI_info, si, rrc, hfn, frame);
        break;
      case NR_BCCH_DL_SCH_MessageType__c1_PR_NOTHING:
      default:
        break;
    }
  }
  SEQUENCE_free(&asn_DEF_NR_BCCH_DL_SCH_Message, bcch_message, ASFM_FREE_EVERYTHING);
}

static void rrc_ue_generate_RRCSetupComplete(NR_UE_RRC_INST_t *rrc, const uint8_t Transaction_id)
{
  uint8_t buffer[100];
  as_nas_info_t initialNasMsg = {0};

  if (IS_SA_MODE(get_softmodem_params())) {
    /* 3GPP TS 38.331:
     * - 5.3.3.1: RRC connection establishment is also used to transfer the initial NAS dedicated information
     *   message from the UE to the network.
     * - 5.3.3.4: set the dedicatedNAS-Message to include the information received from upper layers.
     * In paging-triggered service resumption (TS 24.501 service request procedure), the Service Request NAS PDU
     * is therefore carried here as the initial NAS message in RRCSetupComplete. */
    nr_ue_nas_t *nas = get_ue_nas_info(rrc->ue_id);
    if (rrc->pending_initial_nas.nas_data && rrc->pending_initial_nas.length > 0) {
      /* Deferred NAS PDU is placed in dedicatedNAS-Message (TS 38.331 §5.3.3.4). It is the initial NAS message that
       * starts CM-IDLE to CM-CONNECTED transition (TS 33.501 §6.8.1.2.1, TS 24.501). */
      initialNasMsg = rrc->pending_initial_nas;
      /* Clear RRC copy of pointer after shallow copy: NAS payload is owned by initialNasMsg */
      rrc->pending_initial_nas.nas_data = NULL;
      rrc->pending_initial_nas.length = 0;
      RRCLOG_I("Using pending initial NAS message for RRCSetupComplete\n");
      /* TS 33.501 §6.8.1.2.2: UE derives KgNB from KAMF using NAS UL COUNT of the NAS message that initiated
       * CM-IDLE to CM-CONNECTED transition. §6.2.3.2 / Annex A.9 tie initial KgNB to ngKSI + that COUNT. In this case,
       * NAS holds the derived KgNB value: copy into RRC so AS SMC and RRC keys (§6.2.3.1, §6.5) match after RRC release
       * cleared AS keys (§6.8.1.2.1). */
      if (nas->security_container && nas->security_container->integrity_context)
        memcpy(rrc->kgnb, nas->security.kgnb, sizeof(rrc->kgnb));
    } else {
      // Send Initial NAS message (Registration Request) before Security Mode control procedure
      generateRegistrationRequest(&initialNasMsg, nas, false);
    }
    if (!initialNasMsg.nas_data) {
      RRCLOG_E("Failed to complete RRCSetup. NAS InitialUEMessage message not found.\n");
      return;
    }
  } else {
    initialNasMsg.length = sizeof(nr_nas_attach_req_imsi_dummy_NSA_case);
    initialNasMsg.nas_data = malloc_or_fail(initialNasMsg.length);
    memcpy(initialNasMsg.nas_data, nr_nas_attach_req_imsi_dummy_NSA_case, initialNasMsg.length);
  }

  // Encode RRCSetupComplete
  int size = do_RRCSetupComplete(buffer,
                                 sizeof(buffer),
                                 Transaction_id,
                                 rrc->selected_plmn_identity,
                                 rrc->ra_trigger == RRC_CONNECTION_SETUP,
                                 rrc->fiveG_S_TMSI,
                                 (const uint32_t)initialNasMsg.length,
                                 (const char*)initialNasMsg.nas_data);

  // Free dynamically allocated data (heap allocated in both SA and NSA)
  free(initialNasMsg.nas_data);

  RRCLOG_I("[RAPROC] Logical Channel UL-DCCH (SRB1), Generating RRCSetupComplete (bytes%d)\n", size);
  int srb_id = 1; // RRC setup complete on SRB1
  RRCLOG_D("PDCP_DATA_REQ/%d Bytes RRCSetupComplete ---> %d\n", size, srb_id);
  nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, size, buffer, deliver_pdu_srb_rlc, NULL);
}

static void nr_rrc_rrcsetup_fallback(NR_UE_RRC_INST_t *rrc)
{
  RRCLOG_W("Received RRCSetup in response to %s request\n",
           rrc->ra_trigger == RRC_CONNECTION_REESTABLISHMENT ? "RRCReestablishment" : "RRCResume");

  // TS 38.331 clause 5.3.3.4: discard old dedicated measurements before setting up the new SRB1.
  nr_rrc_release_meas_config(rrc);

  // discard any stored UE Inactive AS context and suspendConfig
  // TODO

  // discard any current AS security context including
  // K_RRCenc key, the K_RRCint key, the K_UPint key and the K_UPenc key
  // TODO only kgnb is stored
  memset(rrc->kgnb, 0, sizeof(rrc->kgnb));
  rrc->as_security_activated = false;

  // release the RRC configuration except for the default L1 parameter values,
  // default MAC Cell Group configuration and CCCH configuration
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_RESET;
  rrc_msg.payload.config_reset.cause = RRC_SETUP_REESTAB_RESUME;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);

  // release radio resources for all established RBs except SRB0,
  // including release of the associated PDCP entities and of SDAP
  for (int i = 1; i <= MAX_DRBS_PER_UE; i++) {
    if (get_DRB_status(rrc, i) != RB_NOT_PRESENT) {
      set_DRB_status(rrc, i, RB_NOT_PRESENT);
      nr_pdcp_release_drb(rrc->ue_id, i);
    }
  }
  for (int i = 1; i < NR_NUM_SRB; i++) {
    if (rrc->Srb[i] != RB_NOT_PRESENT) {
      rrc->Srb[i] = RB_NOT_PRESENT;
      nr_pdcp_release_srb(rrc->ue_id, i);
    }
  }
  for (int i = 1; i < NR_MAX_NUM_LCID; i++) {
    nr_rrc_release_rlc_entity(rrc, i);
  }
  // stop UE TUN readers before SDAP delete (connected reader holds entity pointer)
  nr_rrc_ue_tun_stop_readers(rrc->ue_id);
  nr_sdap_delete_ue_entities(rrc->ue_id);

  // indicate to upper layers fallback of the RRC connection
  // TODO

  // stop timer T380, if running
  // TODO not implemented yet
}

static void nr_rrc_process_rrcsetup(NR_UE_RRC_INST_t *rrc, const NR_RRCSetup_t *rrcSetup)
{
  // if the RRCSetup is received in response to an RRCReestablishmentRequest
  // or RRCResumeRequest or RRCResumeRequest1
  if (rrc->ra_trigger == RRC_CONNECTION_REESTABLISHMENT || rrc->ra_trigger == RRC_RESUME_REQUEST)
    nr_rrc_rrcsetup_fallback(rrc);

  // perform the cell group configuration procedure in accordance with the received masterCellGroup
  bool ret = nr_rrc_ue_process_masterCellGroup(rrc, &rrcSetup->criticalExtensions.choice.rrcSetup->masterCellGroup, NULL, 0, false, NULL);
  if (!ret)
    return;
  // perform the radio bearer configuration procedure in accordance with the received radioBearerConfig
  nr_rrc_ue_process_RadioBearerConfig(rrc, &rrcSetup->criticalExtensions.choice.rrcSetup->radioBearerConfig);

  // TODO (not handled) if stored, discard the cell reselection priority information provided by
  // the cellReselectionPriorities or inherited from another RAT

  // stop timer T300, T301, T319, T320 if running;
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;
  nr_timer_stop(&timers->T300);
  nr_timer_stop(&timers->T301);
  nr_timer_stop(&timers->T319);
  nr_timer_stop(&timers->T320);

  // if T390 (not implemented) and T302 are running
  // stop timer
  // perform the actions as specified in 5.3.14.4
  nr_timer_stop(&timers->T302);
  handle_302_expired_stopped(rrc);

  // if the RRCSetup is received in response to an RRCResumeRequest, RRCResumeRequest1 or RRCSetupRequest
  // enter RRC_CONNECTED
  rrc->nrRrcState = RRC_STATE_CONNECTED_NR;

  // Indicate to NAS that the RRC connection has been established (5.3.1.3 of 3GPP TS 24.501)
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_NRUE, 0, NR_NAS_CONN_ESTABLISH_IND);
  itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, msg_p);

  // resetting the RA trigger state after receiving MSG4 with RRCSetup
  rrc->ra_trigger = RA_NOT_RUNNING;

  // set the content of RRCSetupComplete message
  // TODO procedues described in 5.3.3.4 seems more complex than what we actualy do
  rrc_ue_generate_RRCSetupComplete(rrc, rrcSetup->rrc_TransactionIdentifier);
}

static void nr_rrc_process_rrcreject(NR_UE_RRC_INST_t *rrc, const NR_RRCReject_t *rrcReject)
{
  // stop timer T300, T302, T319 if running;
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;
  nr_timer_stop(&timers->T300);
  nr_timer_stop(&timers->T302);
  nr_timer_stop(&timers->T319);

  // reset MAC and release the default MAC Cell Group configuration
  NR_UE_MAC_reset_cause_t cause = REJECT;
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_RESET;
  rrc_msg.payload.config_reset.cause = cause;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);

  // if waitTime is configured in the RRCReject: start timer T302, with the timer value set to the waitTime
  NR_RejectWaitTime_t *waitTime = NULL;
  if (rrcReject->criticalExtensions.present == NR_RRCReject__criticalExtensions_PR_rrcReject) {
    NR_RRCReject_IEs_t *ies = rrcReject->criticalExtensions.choice.rrcReject;
    waitTime = ies->waitTime; // Wait time value in seconds
  }
  if (waitTime) {
    nr_timer_setup(&timers->T302, *waitTime * 1000, 10);
    nr_timer_start(&timers->T302);
    rrc->access_barred = true;
  } else {
    RRCLOG_W("Error: waitTime should be always included in RRCReject message\n");
  }

  // TODO if RRCReject is received in response to a request from upper layers
  //      inform the upper layer that access barring is applicable for all access categories except categories '0' and '2'

  // TODO if RRCReject is received in response to an RRCSetupRequest
  //      inform upper layers about the failure to setup the RRC connection, upon which the procedure ends

  // TODO else if RRCReject is received in response to an RRCResumeRequest or an RRCResumeRequest1
  //      Resume not implemented yet
}

static int8_t nr_rrc_ue_decode_ccch(NR_UE_RRC_INST_t *rrc, const NRRrcMacCcchDataInd *ind)
{
  NR_DL_CCCH_Message_t *dl_ccch_msg = NULL;
  asn_dec_rval_t dec_rval;
  int rval=0;
  RRCLOG_D("Decoding DL-CCCH message (%d bytes), State %d\n", ind->sdu_size, rrc->nrRrcState);

  dec_rval = uper_decode(NULL, &asn_DEF_NR_DL_CCCH_Message, (void **)&dl_ccch_msg, ind->sdu, ind->sdu_size, 0, 0);

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_DL_CCCH_Message, (void *)dl_ccch_msg);

  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    RRCLOG_E("Failed to decode DL-CCCH-Message (%zu bytes)\n", dec_rval.consumed);
    return -1;
   }

   if (dl_ccch_msg->message.present == NR_DL_CCCH_MessageType_PR_c1) {
     switch (dl_ccch_msg->message.choice.c1->present) {
       case NR_DL_CCCH_MessageType__c1_PR_NOTHING:
         RRCLOG_I("Received PR_NOTHING on DL-CCCH-Message\n");
         rval = 0;
         break;

       case NR_DL_CCCH_MessageType__c1_PR_rrcReject:
         RRCLOG_W("Logical Channel DL-CCCH (SRB0), Received RRCReject \n");
         nr_rrc_process_rrcreject(rrc, dl_ccch_msg->message.choice.c1->choice.rrcReject);
         rval = 0;
         break;

       case NR_DL_CCCH_MessageType__c1_PR_rrcSetup:
         RRCLOG_I("[RAPROC] Logical Channel DL-CCCH (SRB0), Received NR_RRCSetup\n");
         nr_rrc_process_rrcsetup(rrc, dl_ccch_msg->message.choice.c1->choice.rrcSetup);
         rval = 0;
         break;

       default:
         RRCLOG_E("Unknown message\n");
         rval = -1;
         break;
     }
   }

   ASN_STRUCT_FREE(asn_DEF_NR_DL_CCCH_Message, dl_ccch_msg);
   return rval;
}

/** @brief Decode a PCCH paging message and scan its paging records (TS 38.331 §5.3.2.3).
 * @return -1 on decode error, 0 if no match is found, 1 if a record matches the UE */
static int8_t nr_rrc_ue_decode_pcch(NR_UE_RRC_INST_t *rrc, const byte_array_t pcch)
{
  // Paging is only relevant in RRC_IDLE and RRC_INACTIVE.
  if (rrc->nrRrcState != RRC_STATE_IDLE_NR && rrc->nrRrcState != RRC_STATE_INACTIVE_NR) {
    RRCLOG_D("Ignoring PCCH in RRC state %d\n", rrc->nrRrcState);
    return 0;
  }

  RRCLOG_D("Decoding PCCH message (%zu bytes), State %d\n", pcch.len, rrc->nrRrcState);

  nr_paging_params_t params[NR_PCCH_MAX_PAGING_RECORDS];
  int count = 0;
  if (nr_pcch_decode(pcch, params, &count) != 0) {
    RRCLOG_E("Failed to decode PCCH message (%zu bytes)\n", pcch.len);
    log_dump(NR_RRC, pcch.buf, pcch.len, LOG_DUMP_CHAR, "   Received bytes:\n");
    return -1;
  }

  RRCLOG_D("Received Paging message with %d record(s)\n", count);

  const uint64_t ue_fiveg_s_tmsi = rrc->fiveG_S_TMSI & ((1ULL << 48) - 1);
  for (int i = 0; i < count; i++) {
    if (params[i].ue_identity_type == NR_PagingUE_Identity_PR_ng_5G_S_TMSI
        && params[i].ue_identity.fiveg_s_tmsi == ue_fiveg_s_tmsi) {
      RRCLOG_I("Paging record %d matches 5G-S-TMSI=0x%012lu\n", i, ue_fiveg_s_tmsi);
      return 1;
    }
  }

  RRCLOG_D("No paging match found in PagingRecordList\n");
  return 0;
}

static void nr_rrc_ue_process_securityModeCommand(NR_UE_RRC_INST_t *rrc,
                                                  NR_SecurityModeCommand_t *const securityModeCommand,
                                                  int srb_id,
                                                  const uint8_t *msg,
                                                  int msg_size,
                                                  const nr_pdcp_integrity_data_t *msg_integrity)
{
  RRCLOG_I("Receiving from SRB1 (DL-DCCH), Processing securityModeCommand\n");

  AssertFatal(securityModeCommand->criticalExtensions.present == NR_SecurityModeCommand__criticalExtensions_PR_securityModeCommand,
        "securityModeCommand->criticalExtensions.present (%d) != "
        "NR_SecurityModeCommand__criticalExtensions_PR_securityModeCommand\n",
        securityModeCommand->criticalExtensions.present);

  NR_SecurityConfigSMC_t *securityConfigSMC =
      &securityModeCommand->criticalExtensions.choice.securityModeCommand->securityConfigSMC;

  switch (securityConfigSMC->securityAlgorithmConfig.cipheringAlgorithm) {
    case NR_CipheringAlgorithm_nea0:
    case NR_CipheringAlgorithm_nea1:
    case NR_CipheringAlgorithm_nea2:
      RRCLOG_I("Security algorithm is set to nea%ld\n", securityConfigSMC->securityAlgorithmConfig.cipheringAlgorithm);
      break;
    default:
      AssertFatal(0, "Security algorithm not known/supported\n");
  }
  rrc->cipheringAlgorithm = securityConfigSMC->securityAlgorithmConfig.cipheringAlgorithm;

  rrc->integrityProtAlgorithm = 0;
  if (securityConfigSMC->securityAlgorithmConfig.integrityProtAlgorithm != NULL) {
    switch (*securityConfigSMC->securityAlgorithmConfig.integrityProtAlgorithm) {
      case NR_IntegrityProtAlgorithm_nia0:
      case NR_IntegrityProtAlgorithm_nia1:
      case NR_IntegrityProtAlgorithm_nia2:
        RRCLOG_I("Integrity protection algorithm is set to nia%ld\n",
                 *securityConfigSMC->securityAlgorithmConfig.integrityProtAlgorithm);
        break;
      default:
        AssertFatal(0, "Integrity algorithm not known/supported\n");
    }
    rrc->integrityProtAlgorithm = *securityConfigSMC->securityAlgorithmConfig.integrityProtAlgorithm;
  }

  nr_pdcp_entity_security_keys_and_algos_t security_parameters;
  nr_derive_key(RRC_ENC_ALG, rrc->cipheringAlgorithm, rrc->kgnb, security_parameters.ciphering_key);
  nr_derive_key(RRC_INT_ALG, rrc->integrityProtAlgorithm, rrc->kgnb, security_parameters.integrity_key);

  log_dump(NR_RRC, rrc->kgnb, 32, LOG_DUMP_CHAR, "deriving kRRCenc, kRRCint from KgNB=");

  /* for SecurityModeComplete, ciphering is not activated yet, only integrity */
  security_parameters.ciphering_algorithm = 0;
  security_parameters.integrity_algorithm = rrc->integrityProtAlgorithm;
  // configure lower layers to apply SRB integrity protection and ciphering
  for (int i = 1; i < NR_NUM_SRB; i++) {
    if (rrc->Srb[i] == RB_ESTABLISHED)
      nr_pdcp_config_set_security(rrc->ue_id, i, true, &security_parameters);
  }

  NR_UL_DCCH_Message_t ul_dcch_msg = {0};

  ul_dcch_msg.message.present = NR_UL_DCCH_MessageType_PR_c1;
  asn1cCalloc(ul_dcch_msg.message.choice.c1, c1);

  // the SecurityModeCommand message needs to pass the integrity protection check
  // for the UE to declare AS security to be activated
  bool integrity_pass = nr_pdcp_check_integrity_srb(rrc->ue_id, srb_id, msg, msg_size, msg_integrity);
  if (!integrity_pass) {
    /* - continue using the configuration used prior to the reception of the SecurityModeCommand message, i.e.
     *   neither apply integrity protection nor ciphering.
     * - submit the SecurityModeFailure message to lower layers for transmission, upon which the procedure ends.
     */
    RRCLOG_E("integrity of SecurityModeCommand failed, reply with SecurityModeFailure\n");
    c1->present = NR_UL_DCCH_MessageType__c1_PR_securityModeFailure;
    asn1cCalloc(c1->choice.securityModeFailure, modeFailure);
    modeFailure->rrc_TransactionIdentifier = securityModeCommand->rrc_TransactionIdentifier;
    modeFailure->criticalExtensions.present = NR_SecurityModeFailure__criticalExtensions_PR_securityModeFailure;
    asn1cCalloc(modeFailure->criticalExtensions.choice.securityModeFailure, ext);
    ext->nonCriticalExtension = NULL;

    uint8_t buffer[200];
    asn_enc_rval_t enc_rval =
        uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message, NULL, (void *)&ul_dcch_msg, buffer, sizeof(buffer));
    AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %jd)!\n", enc_rval.failed_type->name, enc_rval.encoded);
    if (LOG_DEBUGFLAG(DEBUG_ASN1))
      xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);

    /* disable both ciphering and integrity */
    nr_pdcp_entity_security_keys_and_algos_t null_security_parameters = {0};
    for (int i = 1; i < NR_NUM_SRB; i++) {
      if (rrc->Srb[i] == RB_ESTABLISHED)
        nr_pdcp_config_set_security(rrc->ue_id, i, true, &null_security_parameters);
    }

    srb_id = 1; // SecurityModeFailure in SRB1
    nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, (enc_rval.encoded + 7) / 8, buffer, deliver_pdu_srb_rlc, NULL);

    return;
  }

  /* integrity passed, send SecurityModeComplete */
  c1->present = NR_UL_DCCH_MessageType__c1_PR_securityModeComplete;

  asn1cCalloc(c1->choice.securityModeComplete, modeComplete);
  modeComplete->rrc_TransactionIdentifier = securityModeCommand->rrc_TransactionIdentifier;
  modeComplete->criticalExtensions.present = NR_SecurityModeComplete__criticalExtensions_PR_securityModeComplete;
  asn1cCalloc(modeComplete->criticalExtensions.choice.securityModeComplete, ext);
  ext->nonCriticalExtension = NULL;
  RRCLOG_I("Receiving from SRB1 (DL-DCCH), encoding securityModeComplete, rrc_TransactionIdentifier: %ld\n",
           securityModeCommand->rrc_TransactionIdentifier);
  uint8_t buffer[200];
  asn_enc_rval_t enc_rval =
      uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message, NULL, (void *)&ul_dcch_msg, buffer, sizeof(buffer));
  AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %jd)!\n", enc_rval.failed_type->name, enc_rval.encoded);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
  }
  log_dump(NR_RRC, buffer, 16, LOG_DUMP_CHAR, "securityModeComplete payload: ");
  RRCLOG_D("securityModeComplete Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded + 7) / 8);
  ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);

  for (int i = 0; i < (enc_rval.encoded + 7) / 8; i++) {
    LOG_T(NR_RRC, "%02x.", buffer[i]);
  }
  LOG_T(NR_RRC, "\n");

  rrc->as_security_activated = true;
  srb_id = 1; // SecurityModeComplete in SRB1
  nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, (enc_rval.encoded + 7) / 8, buffer, deliver_pdu_srb_rlc, NULL);

  /* after encoding SecurityModeComplete we activate both ciphering and integrity */
  security_parameters.ciphering_algorithm = rrc->cipheringAlgorithm;
  // configure lower layers to apply SRB integrity protection and ciphering
  for (int i = 1; i < NR_NUM_SRB; i++) {
    if (rrc->Srb[i] == RB_ESTABLISHED)
      nr_pdcp_config_set_security(rrc->ue_id, i, true, &security_parameters);
  }
}

static void nr_rrc_ue_generate_RRCReconfigurationComplete(NR_UE_RRC_INST_t *rrc, const int srb_id, const uint8_t Transaction_id)
{
  uint8_t buffer[32];
  int size = do_NR_RRCReconfigurationComplete(buffer, sizeof(buffer), Transaction_id);
  RRCLOG_I(" Logical Channel UL-DCCH (SRB1), Generating RRCReconfigurationComplete (bytes %d)\n", size);
  AssertFatal(srb_id == 1 || srb_id == 3, "Invalid SRB ID %d\n", srb_id);
  RRCLOG_D(
      "RRC sent PDCP_DATA_REQ/%d Bytes (RRCReconfigurationComplete) "
      "--->][PDCP][RB %02d]\n",
      size,
      srb_id);
  nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, size, buffer, deliver_pdu_srb_rlc, NULL);
}

static void nr_rrc_ue_generate_rrcReestablishmentComplete(const NR_UE_RRC_INST_t *rrc,
                                                          const NR_RRCReestablishment_t *rrcReestablishment)
{
  uint8_t buffer[NR_RRC_BUF_SIZE] = {0};
  int size = do_RRCReestablishmentComplete(buffer, NR_RRC_BUF_SIZE, rrcReestablishment->rrc_TransactionIdentifier);
  RRCLOG_I("[RAPROC] Logical Channel UL-DCCH (SRB1), Generating RRCReestablishmentComplete (bytes %d)\n", size);
  int srb_id = 1; // RRC re-establishment complete on SRB1
  nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, size, buffer, deliver_pdu_srb_rlc, NULL);
}

/** @brief Process RRCReestablishment message
 * This function processes the RRCReestablishment message received from the gNB,
 * implementing procedures as described in 38.331 section 5.3.7.5 */
static void nr_rrc_ue_process_rrcReestablishment(NR_UE_RRC_INST_t *rrc,
                                                 const int gNB_index,
                                                 const NR_RRCReestablishment_t *rrcReestablishment,
                                                 int srb_id,
                                                 const uint8_t *msg,
                                                 int msg_size,
                                                 const nr_pdcp_integrity_data_t *msg_integrity)
{
  // stop timer T301
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;
  nr_timer_stop(&timers->T301);
  NR_RRCReestablishment_IEs_t *ies = rrcReestablishment->criticalExtensions.choice.rrcReestablishment;
  AssertFatal(ies, "Not expecting RRCReestablishment_IEs to be NULL\n");

  // Update KgNB based on the current K gNB key or the NH,
  // using the received nextHopChainingCount (per 33.501 6.9.2.3.4)
  // received from RRCReestablishment and update the stored value in rrc->nhcc
  int8_t received_ncc = ies->nextHopChainingCount;
  nr_ue_nas_t *nas = get_ue_nas_info(rrc->ue_id);
  const uint8_t *kamf = nas->security.kamf;
  nr_update_kgnb_from_ncc(rrc, kamf, received_ncc);

  // derive the K_RRCenc key associated with the previously configured cipheringAlgorithm
  // derive the K_RRCint key associated with the previously configured integrityProtAlgorithm
  nr_pdcp_entity_security_keys_and_algos_t security_parameters;
  security_parameters.ciphering_algorithm = rrc->cipheringAlgorithm;
  security_parameters.integrity_algorithm = rrc->integrityProtAlgorithm;
  nr_derive_key(RRC_ENC_ALG, rrc->cipheringAlgorithm, rrc->kgnb, security_parameters.ciphering_key);
  nr_derive_key(RRC_INT_ALG, rrc->integrityProtAlgorithm, rrc->kgnb, security_parameters.integrity_key);

  // configure lower layers to resume integrity protection for SRB1
  // configure lower layers to resume ciphering for SRB1
  AssertFatal(srb_id == 1, "rrcReestablishment SRB-ID %d, should be 1\n", srb_id);
  nr_pdcp_config_set_security(rrc->ue_id, srb_id, true, &security_parameters);

  // request lower layers to verify the integrity protection of the RRCReestablishment message
  // using the previously configured algorithm and the K_RRCint key
  bool integrity_pass = nr_pdcp_check_integrity_srb(rrc->ue_id, srb_id, msg, msg_size, msg_integrity);
  // if the integrity protection check of the RRCReestablishment message fails
  // perform the actions upon going to RRC_IDLE as specified in 5.3.11
  // with release cause 'RRC connection failure', upon which the procedure ends
  if (!integrity_pass) {
    RRCLOG_W("Integrity of RRCReestablishment failed, going to IDLE\n");
    NR_Release_Cause_t release_cause = RRC_CONNECTION_FAILURE;
    nr_rrc_going_to_IDLE(rrc, release_cause, NULL);
    return;
  }

  // release the measurement gap configuration indicated by the measGapConfig, if configured
  rrcPerNB_t *rrcNB = rrc->perNB + gNB_index;
  asn1cFreeStruc(asn_DEF_NR_MeasGapConfig, rrcNB->measGapConfig);

  // resetting the RA trigger state after receiving MSG4 with RRCReestablishment
  rrc->ra_trigger = RA_NOT_RUNNING;
  // to flag 1st reconfiguration after reestablishment
  rrc->reconfig_after_reestab = true;

  // submit the RRCReestablishmentComplete message to lower layers for transmission
  nr_rrc_ue_generate_rrcReestablishmentComplete(rrc, rrcReestablishment);
}

static void nr_rrc_ue_process_ueCapabilityEnquiry(NR_UE_RRC_INST_t *rrc, NR_UECapabilityEnquiry_t *UECapabilityEnquiry)
{
  NR_UL_DCCH_Message_t ul_dcch_msg = {0};
  RRCLOG_I("Receiving from SRB1 (DL-DCCH), Processing UECapabilityEnquiry\n");

  ul_dcch_msg.message.present = NR_UL_DCCH_MessageType_PR_c1;
  asn1cCalloc(ul_dcch_msg.message.choice.c1, c1);
  c1->present = NR_UL_DCCH_MessageType__c1_PR_ueCapabilityInformation;
  asn1cCalloc(c1->choice.ueCapabilityInformation, info);
  info->rrc_TransactionIdentifier = UECapabilityEnquiry->rrc_TransactionIdentifier;
  if (!rrc->UECap.UE_NR_Capability) {
    rrc->UECap.UE_NR_Capability = CALLOC(1, sizeof(NR_UE_NR_Capability_t));
    asn1cSequenceAdd(rrc->UECap.UE_NR_Capability->rf_Parameters.supportedBandListNR.list, NR_BandNR_t, nr_bandnr);
    nr_bandnr->bandNR = 1;
  }
  xer_fprint(stdout, &asn_DEF_NR_UE_NR_Capability, (void *)rrc->UECap.UE_NR_Capability);

  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_UE_NR_Capability,
                                                  NULL,
                                                  (void *)rrc->UECap.UE_NR_Capability,
                                                  &rrc->UECap.sdu[0],
                                                  MAX_UE_NR_CAPABILITY_SIZE);
  AssertFatal (enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
  rrc->UECap.sdu_size = (enc_rval.encoded + 7) / 8;
  RRCLOG_I("NR Capability encoded, %d bytes (%zd bits)\n", rrc->UECap.sdu_size, enc_rval.encoded + 7);
  NR_UECapabilityEnquiry_IEs_t *ueCapabilityEnquiry_ie = UECapabilityEnquiry->criticalExtensions.choice.ueCapabilityEnquiry;
  AssertFatal(UECapabilityEnquiry->criticalExtensions.present == NR_UECapabilityEnquiry__criticalExtensions_PR_ueCapabilityEnquiry,
              "UECapabilityEnquiry->criticalExtensions.present (%d) != UECapabilityEnquiry__criticalExtensions_PR_c1 (%d)\n",
              UECapabilityEnquiry->criticalExtensions.present,NR_UECapabilityEnquiry__criticalExtensions_PR_ueCapabilityEnquiry);

  NR_UECapabilityInformation_t *ueCapabilityInformation = ul_dcch_msg.message.choice.c1->choice.ueCapabilityInformation;
  ueCapabilityInformation->criticalExtensions.present = NR_UECapabilityInformation__criticalExtensions_PR_ueCapabilityInformation;
  asn1cCalloc(ueCapabilityInformation->criticalExtensions.choice.ueCapabilityInformation, infoIE);
  asn1cCalloc(infoIE->ue_CapabilityRAT_ContainerList, UEcapList);
  UEcapList->list.count = 0;

  for (int i = 0; i < ueCapabilityEnquiry_ie->ue_CapabilityRAT_RequestList.list.count; i++) {
    if (ueCapabilityEnquiry_ie->ue_CapabilityRAT_RequestList.list.array[i]->rat_Type == NR_RAT_Type_nr) {
      /* RAT Container */
      NR_UE_CapabilityRAT_Container_t *ue_CapabilityRAT_Container = CALLOC(1, sizeof(NR_UE_CapabilityRAT_Container_t));
      ue_CapabilityRAT_Container->rat_Type = NR_RAT_Type_nr;
      OCTET_STRING_fromBuf(&ue_CapabilityRAT_Container->ue_CapabilityRAT_Container, (const char *)rrc->UECap.sdu, rrc->UECap.sdu_size);
      asn1cSeqAdd(&UEcapList->list, ue_CapabilityRAT_Container);
      uint8_t buffer[MAX_UE_NR_CAPABILITY_SIZE + 16];
      asn_enc_rval_t enc_rval =
          uper_encode_to_buffer(&asn_DEF_NR_UL_DCCH_Message, NULL, (void *)&ul_dcch_msg, buffer, sizeof(buffer));
      AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %jd)!\n", enc_rval.failed_type->name, enc_rval.encoded);

      if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
        xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)&ul_dcch_msg);
      }
      RRCLOG_I("UECapabilityInformation Encoded %zd bits (%zd bytes)\n", enc_rval.encoded, (enc_rval.encoded + 7) / 8);
      int srb_id = 1; // UECapabilityInformation on SRB1
      nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, (enc_rval.encoded + 7) / 8, buffer, deliver_pdu_srb_rlc, NULL);
    }
  }
  /* Free struct members after it's done including locally allocated ue_CapabilityRAT_Container */
  ASN_STRUCT_RESET(asn_DEF_NR_UL_DCCH_Message, &ul_dcch_msg);
}


static int nr_rrc_ue_decode_dcch(NR_UE_RRC_INST_t *rrc,
                                 const srb_id_t Srb_id,
                                 const uint8_t *const Buffer,
                                 size_t Buffer_size,
                                 const uint8_t gNB_indexP,
                                 const nr_pdcp_integrity_data_t *msg_integrity)
{
  NR_DL_DCCH_Message_t *dl_dcch_msg = NULL;
  if (Srb_id != 1 && Srb_id != 2) {
    RRCLOG_E("Received message on DL-DCCH (SRB%ld), should not have ...\n", Srb_id);
  }

  RRCLOG_D("Decoding DL-DCCH Message\n");
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_DL_DCCH_Message, (void **)&dl_dcch_msg, Buffer, Buffer_size, 0, 0);

  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    RRCLOG_E("Failed to decode DL-DCCH (%zu bytes)\n", dec_rval.consumed);
    ASN_STRUCT_FREE(asn_DEF_NR_DL_DCCH_Message, dl_dcch_msg);
    return -1;
  }

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)dl_dcch_msg);
  }

  switch (dl_dcch_msg->message.present) {
    case NR_DL_DCCH_MessageType_PR_c1: {
      struct NR_DL_DCCH_MessageType__c1 *c1 = dl_dcch_msg->message.choice.c1;
      switch (c1->present) {
        case NR_DL_DCCH_MessageType__c1_PR_NOTHING:
          RRCLOG_I("Received PR_NOTHING on DL-DCCH-Message\n");
          break;

        case NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration: {
          if (rrc->reconfig_after_reestab) {
            // if this is the first RRCReconfiguration message after successful completion of the RRC re-establishment procedure
            // resume SRB2 and DRBs that are suspended
            if (rrc->Srb[2] == RB_SUSPENDED) {
              rrc->Srb[2] = RB_ESTABLISHED;
              nr_mac_rrc_message_t rrc_msg = {0};
              rrc_msg.payload_type = NR_MAC_RRC_RESUME_RB;
              rrc_msg.payload.resume_rb.is_srb = true;
              rrc_msg.payload.resume_rb.rb_id = 2;
              nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
            }
            for (int i = 1; i <= MAX_DRBS_PER_UE; i++) {
              if (get_DRB_status(rrc, i) == RB_SUSPENDED) {
                set_DRB_status(rrc, i, RB_ESTABLISHED);
                nr_mac_rrc_message_t rrc_msg = {0};
                rrc_msg.payload_type = NR_MAC_RRC_RESUME_RB;
                rrc_msg.payload.resume_rb.is_srb = false;
                rrc_msg.payload.resume_rb.rb_id = i;
                nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
              }
            }
            rrc->reconfig_after_reestab = false;
          }
          nr_rrc_ue_process_rrcReconfiguration(rrc, gNB_indexP, c1->choice.rrcReconfiguration);
          nr_rrc_ue_generate_RRCReconfigurationComplete(rrc, Srb_id, c1->choice.rrcReconfiguration->rrc_TransactionIdentifier);
        } break;

        case NR_DL_DCCH_MessageType__c1_PR_rrcResume:
          RRCLOG_E("Received rrcResume on DL-DCCH-Message -> Not handled\n");
          break;
        case NR_DL_DCCH_MessageType__c1_PR_rrcRelease:
          RRCLOG_I("Received RRCRelease (gNB %d)\n", gNB_indexP);
          // delay the actions 60 ms from the moment the RRCRelease message was received
          UPDATE_IE(rrc->RRCRelease, dl_dcch_msg->message.choice.c1->choice.rrcRelease, NR_RRCRelease_t);
          nr_timer_setup(&rrc->release_timer, 60, 10); // 10ms step
          nr_timer_start(&rrc->release_timer);
          break;

        case NR_DL_DCCH_MessageType__c1_PR_ueCapabilityEnquiry:
          RRCLOG_I("Received Capability Enquiry (gNB %d)\n", gNB_indexP);
          nr_rrc_ue_process_ueCapabilityEnquiry(rrc, c1->choice.ueCapabilityEnquiry);
          break;

        case NR_DL_DCCH_MessageType__c1_PR_rrcReestablishment:
          RRCLOG_I("Logical Channel DL-DCCH (SRB1), Received RRCReestablishment\n");
          nr_rrc_ue_process_rrcReestablishment(rrc,
                                               gNB_indexP,
                                               c1->choice.rrcReestablishment,
                                               Srb_id,
                                               Buffer,
                                               Buffer_size,
                                               msg_integrity);
          break;

        case NR_DL_DCCH_MessageType__c1_PR_dlInformationTransfer: {
          NR_DLInformationTransfer_t *dlInfo = c1->choice.dlInformationTransfer;

          if (dlInfo->criticalExtensions.present == NR_DLInformationTransfer__criticalExtensions_PR_dlInformationTransfer) {
            NR_DLInformationTransfer_IEs_t *dlInfo_IE = dlInfo->criticalExtensions.choice.dlInformationTransfer;
            /* This message hold a dedicated info NAS payload, forward it to NAS */
            NR_DedicatedNAS_Message_t *dedicatedNAS_Message = dlInfo_IE->dedicatedNAS_Message;
            if (dedicatedNAS_Message) {
              MessageDef *ittiMsg = itti_alloc_new_message(TASK_RRC_NRUE, rrc->ue_id, NAS_DOWNLINK_DATA_IND);
              dl_info_transfer_ind_t *msg = &NAS_DOWNLINK_DATA_IND(ittiMsg);
              msg->UEid = rrc->ue_id;
              msg->nasMsg.length = dedicatedNAS_Message->size;
              msg->nasMsg.nas_data = malloc(msg->nasMsg.length);
              memcpy(msg->nasMsg.nas_data, dedicatedNAS_Message->buf, msg->nasMsg.length);
              itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, ittiMsg);
            }
          }
        } break;
        case NR_DL_DCCH_MessageType__c1_PR_mobilityFromNRCommand:
        case NR_DL_DCCH_MessageType__c1_PR_dlDedicatedMessageSegment_r16:
        case NR_DL_DCCH_MessageType__c1_PR_ueInformationRequest_r16:
        case NR_DL_DCCH_MessageType__c1_PR_dlInformationTransferMRDC_r16:
        case NR_DL_DCCH_MessageType__c1_PR_loggedMeasurementConfiguration_r16:
        case NR_DL_DCCH_MessageType__c1_PR_spare3:
        case NR_DL_DCCH_MessageType__c1_PR_spare2:
        case NR_DL_DCCH_MessageType__c1_PR_spare1:
        case NR_DL_DCCH_MessageType__c1_PR_counterCheck:
          break;
        case NR_DL_DCCH_MessageType__c1_PR_securityModeCommand:
          RRCLOG_I("Received securityModeCommand (gNB %d)\n", gNB_indexP);
          nr_rrc_ue_process_securityModeCommand(rrc, c1->choice.securityModeCommand, Srb_id, Buffer, Buffer_size, msg_integrity);
          break;
      }
    } break;
    default:
      break;
  }
  //  release memory allocation
  SEQUENCE_free(&asn_DEF_NR_DL_DCCH_Message, dl_dcch_msg, ASFM_FREE_EVERYTHING);
  return 0;
}

/** @brief Encode NAS in ULInformationTransfer, submit to PDCP on SRB2 if established else SRB1. */
static void nr_rrc_ue_send_ul_information_transfer_nas(NR_UE_RRC_INST_t *rrc, uint32_t nas_length, uint8_t *nas_pdu)
{
  uint8_t *buffer = NULL;
  const int enc_bytes = do_NR_ULInformationTransfer(&buffer, nas_length, nas_pdu);
  const rb_id_t srb_id = rrc->Srb[2] == RB_ESTABLISHED ? 2 : 1;
  RRCLOG_D("PDCP_DATA_REQ ULInformationTransfer (NAS %u B) -> SRB%d encoded %d B\n", nas_length, (int)srb_id, enc_bytes);
  nr_pdcp_data_req_srb(rrc->ue_id, srb_id, 0, enc_bytes, buffer, deliver_pdu_srb_rlc, NULL);
  free(buffer);
}

static void apply_ema(val_init_t *vi_rsrp_dBm, float filter_coeff_rsrp, int rsrp_dBm)
{
  int *quant = &vi_rsrp_dBm->val;
  bool *meas_init = &vi_rsrp_dBm->init;
  float coef = *meas_init ? filter_coeff_rsrp : 1.0f; // if not init, first measurement gets full weight
  *quant = (1.0f - coef) * (*quant) + coef * rsrp_dBm;
  *meas_init = true;
}

void nr_ue_meas_filtering(rrcPerNB_t *rrc, meas_t *meas_cell, uint16_t Nid_cell, bool csi_meas, int rsrp_dBm)
{
  l3_measurements_t *l3_measurements = &rrc->l3_measurements;

  if (meas_cell->Nid_cell != Nid_cell) {
    meas_cell->ss_rsrp_dBm.init = false;
    meas_cell->csi_rsrp_dBm.init = false;
  }
  meas_cell->Nid_cell = Nid_cell;

  if (csi_meas)
    apply_ema(&meas_cell->csi_rsrp_dBm, l3_measurements->csi_RS_filter_coeff_rsrp, rsrp_dBm);
  else
    apply_ema(&meas_cell->ss_rsrp_dBm, l3_measurements->ssb_filter_coeff_rsrp, rsrp_dBm);
}

static int get_rsrp_value(const meas_t *cell)
{
  if (cell->ss_rsrp_dBm.init)
    return cell->ss_rsrp_dBm.val;
  if (cell->csi_rsrp_dBm.init)
    return cell->csi_rsrp_dBm.val;
  return INT_MAX;
}

static void setup_meas_trigger(l3_measurements_t *l3_measurements,
                               NR_EventTriggerConfig_t *event_config,
                               int meas_id,
                               long trigger_quantity,
                               bool neighbor_cell_valid)
{
  meas_report_params_t *params = &l3_measurements->meas_report[meas_id];

  setup_meas_params(params,
                    trigger_quantity,
                    event_config->rsType,
                    event_config->reportAmount,
                    event_config->reportAmount == NR_EventTriggerConfig__reportAmount_infinity,
                    event_config->reportInterval,
                    neighbor_cell_valid);

  params->report_rsrp = event_config->reportQuantityCell.rsrp;
  params->max_report_cells = event_config->maxReportCells;
}

static void start_meas_event(l3_measurements_t *l3_measurements,
                             rrcPerNB_t *rrcNB,
                             NR_timer_t *event_timer,
                             NR_EventTriggerConfig_t *event_config,
                             int report_config_id,
                             long trigger_quantity,
                             bool neighbor_cell_valid,
                             long time_to_trigger)
{
  nr_timer_setup(event_timer, get_event_time_to_trigger(time_to_trigger), 10);
  nr_timer_start(event_timer);

  int meas_id = get_meas_id(rrcNB, report_config_id);
  AssertFatal(meas_id > 0, "meas_id did not found for report_config_id %i\n", report_config_id);

  setup_meas_trigger(l3_measurements, event_config, meas_id, trigger_quantity, neighbor_cell_valid);
}

static void stop_meas_event(meas_report_params_t *params, NR_timer_t *event_timer)
{
  nr_timer_stop(event_timer);
  params->reports_sent = 0;
}

// TS 38.331 - 5.5.4.3 Event A2 (Serving becomes worse than threshold)
static void handle_event_a2(l3_measurements_t *l3_measurements,
                            rrcPerNB_t *rrcNB,
                            struct NR_EventTriggerConfig__eventId__eventA2 *event_A2,
                            NR_EventTriggerConfig_t *event_trigger_config,
                            long report_config_id)
{
  if (event_A2->a2_Threshold.present != NR_MeasTriggerQuantity_PR_rsrp)
    return;

  int meas_id = get_meas_id(rrcNB, report_config_id);
  if (meas_id < 0)
    return;
  meas_report_params_t *params = &l3_measurements->meas_report[meas_id];
  meas_t *serving_cell = &l3_measurements->serving_cell;
  int serving_cell_rsrp = get_rsrp_value(serving_cell);
  if (serving_cell_rsrp == INT_MAX) {
    LOG_E(NR_RRC, "There are no RSRP measurements taken for the active cell\n");
  }

  // TS 38.133 - Table 10.1.6.1-1: SS-RSRP and CSI-RSRP measurement report mapping
  int rsrp_threshold = event_A2->a2_Threshold.choice.rsrp - 157;
  int rsrp_hysteresis = event_A2->hysteresis >> 1;

  if (serving_cell_rsrp + rsrp_hysteresis < rsrp_threshold) {
    if (!nr_timer_is_active(&params->TA2) && (params->reports_sent == 0)) {
      start_meas_event(l3_measurements,
                       rrcNB,
                       &params->TA2,
                       event_trigger_config,
                       report_config_id,
                       event_A2->a2_Threshold.present,
                       false,
                       event_A2->timeToTrigger);

      LOG_W(NR_RRC,
            "(active_cell_rsrp) %i + (rsrp_hysteresis) %i < (rsrp_threshold) %i\n",
            serving_cell_rsrp,
            rsrp_hysteresis,
            rsrp_threshold);
    }
  } else if (nr_timer_is_active(&params->TA2) && (serving_cell_rsrp - rsrp_hysteresis > rsrp_threshold)) {
    stop_meas_event(params, &params->TA2);
  }
}

// TS 38.331 - 5.5.4.4 Event A3 (Neighbour becomes offset better than SpCell)
static void handle_event_a3(l3_measurements_t *l3_measurements,
                            rrcPerNB_t *rrcNB,
                            struct NR_EventTriggerConfig__eventId__eventA3 *event_A3,
                            NR_EventTriggerConfig_t *event_trigger_config,
                            long report_config_id)
{
  if (event_A3->a3_Offset.present != NR_MeasTriggerQuantityOffset_PR_rsrp)
    return;

  int meas_id = get_meas_id(rrcNB, report_config_id);
  if (meas_id < 0)
    return;
  meas_report_params_t *params = &l3_measurements->meas_report[meas_id];
  meas_t *serving_cell = &l3_measurements->serving_cell;

  int serving_cell_rsrp = get_rsrp_value(serving_cell);
  if (serving_cell_rsrp == INT_MAX) {
    LOG_D(NR_RRC, "There are no RSRP measurements taken for the serving cell\n");
    return;
  }

  int rsrp_offset = event_A3->a3_Offset.choice.rsrp >> 1;
  int rsrp_hysteresis = event_A3->hysteresis;

  // Check all neighboring cells for Event A3 condition
  bool entry_cond_met = false;
  bool above_leaving_threshold = false;
  int entry_neighbor_rsrp = INT_MIN;

  for (int i = 0; i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
    meas_t *neighboring_cell = &l3_measurements->neighboring_cell[i];
    int neighboring_cell_rsrp = get_rsrp_value(neighboring_cell);

    if (neighboring_cell_rsrp == INT_MAX) {
      LOG_D(NR_RRC, "There are no RSRP measurements taken for the neighboring cell %d\n", i);
      neighboring_cell_rsrp = INT_MIN;
    }

    // Check entry condition
    if (neighboring_cell_rsrp > serving_cell_rsrp + rsrp_offset + rsrp_hysteresis) {
      entry_cond_met = true;
      entry_neighbor_rsrp = neighboring_cell_rsrp;
    }

    // Check if any neighbor is still above leaving threshold
    if (neighboring_cell_rsrp >= serving_cell_rsrp + rsrp_offset - rsrp_hysteresis) {
      above_leaving_threshold = true;
    }
  }

  // Trigger event if any neighbor meets entry condition
  if (entry_cond_met && !nr_timer_is_active(&params->TA3) && (params->reports_sent == 0)) {
    start_meas_event(l3_measurements,
                     rrcNB,
                     &params->TA3,
                     event_trigger_config,
                     report_config_id,
                     NR_MeasTriggerQuantityOffset_PR_rsrp,
                     true,
                     event_A3->timeToTrigger);

    LOG_W(NR_RRC,
          "(neighboring_cell_rsrp) %i > (serving_cell_rsrp %i) + (rsrp_offset) %i + (rsrp_hysteresis) %i\n",
          entry_neighbor_rsrp,
          serving_cell_rsrp,
          rsrp_offset,
          rsrp_hysteresis);
  }
  // Stop event if all neighbors are below leaving threshold
  else if (nr_timer_is_active(&params->TA3) && !above_leaving_threshold) {
    stop_meas_event(params, &params->TA3);
  }
}

static void nr_ue_check_meas_report(NR_UE_RRC_INST_t *rrc, const uint8_t gnb_index)
{
  rrcPerNB_t *rrcNB = rrc->perNB + gnb_index;
  l3_measurements_t *l3_measurements = &rrcNB->l3_measurements;

  for (int i = 0; i < NR_MAX_MEAS_CONFIG; i++) {
    NR_ReportConfigToAddMod_t *report_config = rrcNB->ReportConfig[i];
    if (report_config == NULL)
      continue;

    if (report_config->reportConfig.present != NR_ReportConfigToAddMod__reportConfig_PR_reportConfigNR)
      continue;

    NR_ReportConfigNR_t *report_config_nr = report_config->reportConfig.choice.reportConfigNR;
    if (report_config_nr->reportType.present != NR_ReportConfigNR__reportType_PR_eventTriggered)
      continue;

    NR_EventTriggerConfig_t *event_trigger_config = report_config_nr->reportType.choice.eventTriggered;

    switch (event_trigger_config->eventId.present) {
      case NR_EventTriggerConfig__eventId_PR_eventA2:
        handle_event_a2(l3_measurements,
                        rrcNB,
                        event_trigger_config->eventId.choice.eventA2,
                        event_trigger_config,
                        report_config->reportConfigId);
        break;
      case NR_EventTriggerConfig__eventId_PR_eventA3:
        handle_event_a3(l3_measurements,
                        rrcNB,
                        event_trigger_config->eventId.choice.eventA3,
                        event_trigger_config,
                        report_config->reportConfigId);
        break;
      default:
        break;
    }
  }
}

static void nr_rrc_handle_ra_indication(NR_UE_RRC_INST_t *rrc, bool ra_succeeded, int gNB_index)
{
  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;
  if (ra_succeeded && nr_timer_is_active(&timers->T304)) {
    // successful Random Access procedure triggered by reconfigurationWithSync
    // procedures described in 5.3.5.3 of 38.331 when
    // reconfigurationWithSync is included in spCellConfig
    nr_timer_stop(&timers->T304);
    nr_rrc_clear_ho_source(rrc);

    // TODO apply the parts of the CQI reporting configuration,
    // the scheduling request configuration and the sounding RS
    // configuration that do not require the UE to know the SFN of the respective target SpCell
    // TODO apply the parts of the measurement and the radio resource configuration
    // that require the UE to know the SFN of the respective target SpCell
    // (not sure what to do for these two points, probably not relevant for our implementation)

    // if T390 is running stop timer T390 for all access categories
    if (nr_timer_is_active(&timers->T390)) {
      nr_timer_stop(&timers->T390);
      // TODO perform the actions as specified in 5.3.14.4.
    }

    if (rrc->sched_reconfsync_sib1) {
      rrc->sched_reconfsync_sib1 = false;
      NR_UE_RRC_SI_INFO *SI_info = &rrc->perNB[gNB_index].SInfo;
      SI_info->sib_pending = true;
      nr_mac_rrc_message_t sib_msg = {0};
      sib_msg.payload_type = NR_MAC_RRC_SCHED_SIB;
      sib_msg.payload.sched_sib.get_sib = 1;
      nr_rrc_send_msg_to_mac(rrc, &sib_msg);
    }
  } else if (!ra_succeeded) {
    // upon random access problem indication from MCG MAC
    // while neither T300, T301, T304, T311 nor T319 are running
    // consider radio link failure to be detected
    if (!nr_timer_is_active(&timers->T300)
        && !nr_timer_is_active(&timers->T301)
        && !nr_timer_is_active(&timers->T304)
        && !nr_timer_is_active(&timers->T311)
        && !nr_timer_is_active(&timers->T319))
      handle_rlf_detection(rrc);
  }
}

static void nr_rrc_handle_meas_indication(NR_UE_RRC_INST_t *rrc, NRRrcMacMeasDataInd *meas_ind)
{
  rrcPerNB_t *rrcNB = rrc->perNB + meas_ind->gnb_index;
  l3_measurements_t *l3_measurements = &rrcNB->l3_measurements;
  meas_t *meas_cell = NULL;

  if (meas_ind->is_neighboring_cell) {
    uint16_t target_nid_cell = meas_ind->Nid_cell;
    for (int i = 0; i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
      if (l3_measurements->neighboring_cell[i].Nid_cell == target_nid_cell) {
        meas_cell = &l3_measurements->neighboring_cell[i];
        break;
      }
    }
    if (!meas_cell) {
      for (int i = 0; i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
        if (!l3_measurements->neighboring_cell[i].ss_rsrp_dBm.init && !l3_measurements->neighboring_cell[i].csi_rsrp_dBm.init) {
          meas_cell = &l3_measurements->neighboring_cell[i];
          break;
        }
      }
    }
  } else {
    meas_cell = &l3_measurements->serving_cell;
  }

  if (!meas_cell) {
    RRCLOG_E("meas_cell not found!\n");
    return;
  }

  if (meas_ind->is_neighboring_cell && meas_ind->rsrp_dBm == INT_MAX) {
    RRCLOG_D("[Nid_cell %i] Neighboring cell not detected. L3 measurements will be reset.\n", meas_ind->Nid_cell);
    meas_cell->Nid_cell = meas_ind->Nid_cell;
    nr_ue_meas_reset(meas_cell, meas_ind->is_csi_meas);
  } else {
    RRCLOG_D("[%s][Nid_cell %i] Received %s measurements: RSRP = %i (dBm)\n",
             meas_ind->is_neighboring_cell ? "Neighboring cell" : "Active cell",
             meas_ind->Nid_cell,
             meas_ind->is_csi_meas ? "CSI meas" : "SSB meas",
             meas_ind->rsrp_dBm);

    nr_ue_meas_filtering(rrcNB, meas_cell, meas_ind->Nid_cell, meas_ind->is_csi_meas, meas_ind->rsrp_dBm);
    nr_ue_check_meas_report(rrc, meas_ind->gnb_index);
  }
}

void *rrc_nrue_task(void *args_p)
{
  UNUSED(args_p);
  itti_mark_task_ready(TASK_RRC_NRUE);
  while (1) {
    rrc_nrue(NULL);
  }
}

void *rrc_nrue(void *notUsed)
{
  UNUSED(notUsed);
  MessageDef *msg_p = NULL;
  itti_receive_msg(TASK_RRC_NRUE, &msg_p);
  instance_t instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);
  NR_UE_RRC_INST_t *rrc = get_NR_UE_rrc_inst(instance);
  AssertFatal(instance == rrc->ue_id, "Instance %ld received from ITTI doesn't matach with UE-ID %ld\n", instance, rrc->ue_id);
  RRCLOG_D("Received %s frame %d\n", ITTI_MSG_NAME(msg_p), rrc->current_frame);
  switch (ITTI_MSG_ID(msg_p)) {
  case TERMINATE_MESSAGE:
    RRCLOG_W(" *** Exiting RRC thread\n");
    itti_exit_task();
    break;

  case MESSAGE_TEST:
    break;

  case NR_RRC_MAC_SYNC_IND: {
    // TS 38.331 clause 5.3.10.1 applies in RRC_CONNECTED only
    if (rrc->nrRrcState != RRC_STATE_CONNECTED_NR)
      break;
    nr_sync_msg_t sync_msg = NR_RRC_MAC_SYNC_IND(msg_p).in_sync ? IN_SYNC : OUT_OF_SYNC;
    NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
    handle_rlf_sync(tac, sync_msg);
  } break;

  case NRRRC_FRAME_PROCESS:
    LOG_D(NR_RRC, "RRC frame advances %u -> %u\n", rrc->current_frame, NRRRC_FRAME_PROCESS(msg_p).frame);
    rrc->current_hfn = NRRRC_FRAME_PROCESS(msg_p).hfn;
    rrc->current_frame = NRRRC_FRAME_PROCESS(msg_p).frame;
    RRCLOG_D("Got new frame information: %d\n", rrc->current_frame);
    // increase the timers every 10ms (every new frame)
    nr_rrc_handle_timers(rrc);
    NR_UE_RRC_SI_INFO *SInfo = &rrc->perNB[NRRRC_FRAME_PROCESS(msg_p).gnb_id].SInfo;
    nr_rrc_SI_timers(SInfo);
    if (rrc->process_target_ntncfg) {
      // Process target NTNCFG as the target cells timing is acquired
      nr_rrc_process_ntnconfig(rrc, &rrc->perNB[NRRRC_FRAME_PROCESS(msg_p).gnb_id].SInfo, rrc->target_ntncfg, rrc->current_frame, true);
      ASN_STRUCT_FREE(asn_DEF_NR_NTN_Config_r17, rrc->target_ntncfg);
      rrc->target_ntncfg = NULL;
      rrc->process_target_ntncfg = false;
    }
    break;

  case NR_RRC_MAC_VERIFY:
    RRCLOG_W("L2 verification of RRC consistency failed\n");
    handle_rlf_detection(rrc);
    break;

  case NR_RRC_MAC_INAC_IND:
    RRCLOG_D("Received data inactivity indication from lower layers\n");
    NR_Release_Cause_t release_cause = RRC_CONNECTION_FAILURE;
    nr_rrc_going_to_IDLE(rrc, release_cause, NULL);
    break;

  case NR_RRC_RLC_MAXRTX:
    // detection of RLF upon indication from RLC that the maximum number of retransmissions has been reached
    RRCLOG_W("Received indication that RLC reached max retransmissions\n");
    handle_rlf_detection(rrc);
    break;

  case NR_RRC_MAC_MSG3_IND:
    if (NR_RRC_MAC_MSG3_IND(msg_p).prepare_payload)
      nr_rrc_prepare_msg3_payload(rrc);
    else
      nr_rrc_handle_msg3_indication(rrc, NR_RRC_MAC_MSG3_IND(msg_p).rnti);
    break;

  case NR_RRC_MAC_RA_IND:
    RRCLOG_D("Received %s: RA %s\n", ITTI_MSG_NAME(msg_p), NR_RRC_MAC_RA_IND(msg_p).RA_succeeded ? "successful" : "failed");
    nr_rrc_handle_ra_indication(rrc, NR_RRC_MAC_RA_IND(msg_p).RA_succeeded, NR_RRC_MAC_RA_IND(msg_p).gnb_index);
    break;

  case NR_RRC_MAC_BCCH_DATA_IND:
    RRCLOG_D("Received %s: gNB %d\n", ITTI_MSG_NAME(msg_p), NR_RRC_MAC_BCCH_DATA_IND(msg_p).gnb_index);
    NRRrcMacBcchDataInd *bcch = &NR_RRC_MAC_BCCH_DATA_IND(msg_p);
    if (bcch->is_bch)
      nr_rrc_ue_decode_NR_BCCH_BCH_Message(rrc, bcch->gnb_index, bcch->phycellid, bcch->ssb_arfcn, bcch->sdu, bcch->sdu_size);
    else
      nr_rrc_ue_decode_NR_BCCH_DL_SCH_Message(rrc, bcch->gnb_index, bcch->sdu, bcch->sdu_size, bcch->hfn, bcch->frame, bcch->slot);
    break;

  case NR_RRC_MAC_SBCCH_DATA_IND:
    RRCLOG_D("Received %s: gNB %d\n", ITTI_MSG_NAME(msg_p), NR_RRC_MAC_SBCCH_DATA_IND(msg_p).gnb_index);
    NRRrcMacSBcchDataInd *sbcch = &NR_RRC_MAC_SBCCH_DATA_IND(msg_p);
    nr_rrc_ue_decode_NR_SBCCH_SL_BCH_Message(rrc, sbcch->sdu, sbcch->sdu_size, sbcch->rx_slss_id);
    break;

  case NR_RRC_MAC_MEAS_DATA_IND:
    nr_rrc_handle_meas_indication(rrc, &NR_RRC_MAC_MEAS_DATA_IND(msg_p));
    break;

  case NR_RRC_MAC_CCCH_DATA_IND: {
    NRRrcMacCcchDataInd *ind = &NR_RRC_MAC_CCCH_DATA_IND(msg_p);
    nr_rrc_ue_decode_ccch(rrc, ind);
  } break;

  case NR_RRC_MAC_PCCH_DATA_IND: {
    NRRrcMacPcchDataInd *ind = &NR_RRC_MAC_PCCH_DATA_IND(msg_p);
    const byte_array_t pcch = {.len = ind->sdu_size, .buf = ind->sdu};
    if (nr_rrc_ue_decode_pcch(rrc, pcch) == 1) {
      RRCLOG_I("Paging match found in PagingRecordList\n");
      MessageDef *nas_msg = itti_alloc_new_message(TASK_RRC_NRUE, rrc->ue_id, NAS_PAGING_IND);
      if (nas_msg != NULL) {
        NAS_PAGING_IND(nas_msg).cause = AS_CONNECTION_ESTABLISH;
        RRCLOG_I("Triggering Service Request after paging (cause=AS_CONNECTION_ESTABLISH)\n");
        itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, nas_msg);
      } else {
        RRCLOG_E("Failed to allocate NAS_PAGING_IND message\n");
      }
    }
  } break;

  case NR_RRC_DCCH_DATA_IND:
    nr_rrc_ue_decode_dcch(rrc,
                          NR_RRC_DCCH_DATA_IND(msg_p).dcch_index,
                          NR_RRC_DCCH_DATA_IND(msg_p).sdu_p,
                          NR_RRC_DCCH_DATA_IND(msg_p).sdu_size,
                          NR_RRC_DCCH_DATA_IND(msg_p).gNB_index,
                          &NR_RRC_DCCH_DATA_IND(msg_p).msg_integrity);
    /* this is allocated by itti_malloc in PDCP task (deliver_sdu_srb)
       then passed to the RRC task and freed after use */
    free(NR_RRC_DCCH_DATA_IND(msg_p).sdu_p);
    break;

  case NAS_KENB_REFRESH_REQ:
    memcpy(rrc->kgnb, NAS_KENB_REFRESH_REQ(msg_p).kenb, sizeof(rrc->kgnb));
    break;

  case NAS_DETACH_REQ:
    if (NAS_DETACH_REQ(msg_p).wait_release)
      rrc->detach_after_release = true;
    else {
      rrc->nrRrcState = RRC_STATE_DETACH_NR;
      NR_Release_Cause_t release_cause = OTHER;
      nr_rrc_going_to_IDLE(rrc, release_cause, NULL);
    }
    break;

  case NAS_UPLINK_DATA_REQ: {
    ul_info_transfer_req_t *req = &NAS_UPLINK_DATA_REQ(msg_p);
    /* ULInformationTransfer (TS 38.331) requires an established UL-DCCH SRB: not used for CM-IDLE initial NAS
     * (that path uses RRCSetupComplete dedicatedNAS-Message (TS 38.331 §5.3.3.4, TS 33.501 §6.8.1.2.1)). */
    if (rrc->Srb[1] != RB_ESTABLISHED && rrc->Srb[2] != RB_ESTABLISHED) {
      RRCLOG_W("NAS UL requested but no SRB established: dropping UL request (%u B)\n", req->nasMsg.length);
      free(req->nasMsg.nas_data);
      break;
    }
    nr_rrc_ue_send_ul_information_transfer_nas(rrc, req->nasMsg.length, req->nasMsg.nas_data);
    free(req->nasMsg.nas_data);
    break;
  }

  case NAS_INITIAL_UL_TRANSFER_REQ: {
    ul_info_transfer_req_t *req = &NAS_INITIAL_UL_TRANSFER_REQ(msg_p);
    if (rrc->Srb[1] != RB_ESTABLISHED && rrc->Srb[2] != RB_ESTABLISHED) {
      /* No UL-DCCH SRB yet: cannot use ULInformationTransfer (TS 38.331). Buffer NAS for dedicatedNAS-Message in
       * RRCSetupComplete (§5.3.3.4). Typical source is Service Request from 5GMM-IDLE (TS 24.501 §5.6.1). */
      free(rrc->pending_initial_nas.nas_data);
      rrc->pending_initial_nas.nas_data = req->nasMsg.nas_data;
      rrc->pending_initial_nas.length = req->nasMsg.length;
      RRCLOG_I("Initial NAS UL: no SRB yet; buffered %u B for RRCSetupComplete dedicatedNAS (RRC state=%d)\n",
               req->nasMsg.length,
               rrc->nrRrcState);
      if (rrc->nrRrcState == RRC_STATE_IDLE_NR) {
        RA_trigger_t prev_trigger = rrc->ra_trigger;
        rrc->ra_trigger = RRC_CONNECTION_SETUP;
        nr_rrc_ue_prepare_RRCSetupRequest(rrc);
        nr_rrc_trigger_mac_ra(rrc, NR_MAC_RA_START_SETUP);
        RRCLOG_I("Triggering MAC RA for RRCSetupComplete pending NAS (prev_trigger=%d)\n", prev_trigger);
      }
      break;
    }
    RRCLOG_W("Initial NAS UL requested but SRB established: dropping request (length=%u)\n", req->nasMsg.length);
    free(rrc->pending_initial_nas.nas_data);
    rrc->pending_initial_nas.nas_data = NULL;
    rrc->pending_initial_nas.length = 0;
    free(req->nasMsg.nas_data);
    break;
  }

  case NAS_5GMM_IND: {
    nas_5gmm_ind_t *req = &NAS_5GMM_IND(msg_p);
    rrc->fiveG_S_TMSI = req->fiveG_STMSI;
    /* Push the 5G-S-TMSI-derived UE_ID to MAC for paging PF/PO derivation */
    nr_rrc_mac_config_req_paging_ue_id(rrc->ue_id, rrc->fiveG_S_TMSI);
    break;
  }

  case NAS_TUN_REQ: {
    const nas_tun_req_t *req = &NAS_TUN_REQ(msg_p);
    DevAssert(req->n_psi >= 0 && req->n_psi <= NAS_TUN_LIST_MAX);
    if (req->action == NAS_TUN_START_USER_PLANE) {
      for (int i = 0; i < req->n_psi; i++) {
        const nas_tun_psi_t *p = &req->psi[i];
        nr_rrc_ue_tun_start_user_plane(rrc->ue_id, p->pdusession_id, p->sock, p->qfi, p->reader_thread);
      }
    } else if (req->action == NAS_TUN_START_IDLE_LISTENER) {
      nr_rrc_ue_tun_stop_readers(rrc->ue_id);
      nr_rrc_ue_tun_start_idle_listeners(rrc->ue_id);
    } else {
      AssertFatal(false, "unknown NAS_TUN_REQ action %d\n", req->action);
    }
    break;
  }

  case NAS_MO_UL_DATA_IND: {
    nr_rrc_ue_tun_stop_readers(rrc->ue_id);
    MessageDef *nas_msg = itti_alloc_new_message(TASK_RRC_NRUE, rrc->ue_id, NAS_MO_UL_DATA_IND);
    itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, nas_msg);
    break;
  }

  default:
    RRCLOG_E("Received unexpected message %s\n", ITTI_MSG_NAME(msg_p));
    break;
  }
  RRCLOG_D("RRC Status %d\n", rrc->nrRrcState);
  int result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  return NULL;
}

void nr_rrc_ue_process_sidelink_radioResourceConfig(NR_SetupRelease_SL_ConfigDedicatedNR_r16_t *sl_ConfigDedicatedNR)
{
  //process sl_CommConfig, configure MAC/PHY for transmitting SL communication (RRC_CONNECTED)
  if (sl_ConfigDedicatedNR != NULL) {
    switch (sl_ConfigDedicatedNR->present){
      case NR_SetupRelease_SL_ConfigDedicatedNR_r16_PR_setup:
        //TODO
        break;
      case NR_SetupRelease_SL_ConfigDedicatedNR_r16_PR_release:
        break;
      case NR_SetupRelease_SL_ConfigDedicatedNR_r16_PR_NOTHING:
        break;
      default:
        break;
    }
  }
}

static void nr_rrc_initiate_rrcReestablishment(NR_UE_RRC_INST_t *rrc, NR_ReestablishmentCause_t cause)
{
  rrc->reestablishment_cause = cause;
  rrc->reestab_source_pci = rrc->phyCellID;
  rrc->reestab_source_crnti = rrc->rnti;

  NR_UE_Timers_Constants_t *timers = &rrc->timers_and_constants;

  // reset timers to SIB1 as part of release of spCellConfig
  // it needs to be done before handling timers
  set_rlf_sib1_timers_and_constants(timers, rrc->timers_and_constants.sib1_TimersAndConstants);

  // stop timer T310, if running
  nr_timer_stop(&timers->T310);
  // stop timer T304, if running
  nr_timer_stop(&timers->T304);
  // start timer T311
  nr_timer_start(&timers->T311);
  // suspend all RBs, except SRB0
  for (int i = 1; i < 4; i++) {
    if (rrc->Srb[i] == RB_ESTABLISHED) {
      rrc->Srb[i] = RB_SUSPENDED;
    }
  }
  for (int i = 1; i <= MAX_DRBS_PER_UE; i++) {
    if (get_DRB_status(rrc, i) == RB_ESTABLISHED) {
      set_DRB_status(rrc, i, RB_SUSPENDED);
    }
  }

  // Free Target NTNcfg is stored
  if (rrc->target_ntncfg) {
    ASN_STRUCT_FREE(asn_DEF_NR_NTN_Config_r17, rrc->target_ntncfg);
    rrc->target_ntncfg = NULL;
    rrc->process_target_ntncfg = false;
  }

  // release the MCG SCell(s), if configured
  // no SCell configured in our implementation

  // reset MAC
  // release spCellConfig, if configured
  // perform cell selection in accordance with the cell selection process
  RRCLOG_A("Initiating RRC re-establishment (cause=%s)\n",
           cause == 0   ? "reconfigurationFailure"
           : cause == 1 ? "handoverFailure"
                        : "otherFailure");
  nr_rrc_trigger_mac_ra(rrc, NR_MAC_RA_START_REESTABLISHMENT);
}

void handle_RRCRelease(NR_UE_RRC_INST_t *rrc)
{
  NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
  // stop timer T380, if running
  nr_timer_stop(&tac->T380);
  // stop timer T320, if running
  nr_timer_stop(&tac->T320);
  if (rrc->detach_after_release)
    rrc->nrRrcState = RRC_STATE_DETACH_NR;
  const struct NR_RRCRelease_IEs *rrcReleaseIEs = rrc->RRCRelease ? rrc->RRCRelease->criticalExtensions.choice.rrcRelease : NULL;
  if (!rrc->as_security_activated) {
    // ignore any field included in RRCRelease message except waitTime
    // perform the actions upon going to RRC_IDLE as specified in 5.3.11 with the release cause 'other'
    // upon which the procedure ends
    NR_Release_Cause_t cause = OTHER;
    nr_rrc_going_to_IDLE(rrc, cause, rrc->RRCRelease);
    asn1cFreeStruc(asn_DEF_NR_RRCRelease, rrc->RRCRelease);
    return;
  }
  bool suspend = false;
  if (rrcReleaseIEs) {
    if (rrcReleaseIEs->redirectedCarrierInfo)
      RRCLOG_E("redirectedCarrierInfo in RRCRelease not handled\n");
    if (rrcReleaseIEs->cellReselectionPriorities)
      RRCLOG_E("cellReselectionPriorities in RRCRelease not handled\n");
    if (rrcReleaseIEs->deprioritisationReq)
      RRCLOG_I("deprioritisationReq in RRCRelease not applied, UE doesn't support release with deprioritisation\n");
    if (rrcReleaseIEs->suspendConfig) {
      suspend = true;
      // procedures to go in INACTIVE state
      AssertFatal(false, "Inactive State not supported\n");
    }
  }
  if (!suspend) {
    NR_Release_Cause_t cause = OTHER;
    nr_rrc_going_to_IDLE(rrc, cause, rrc->RRCRelease);
  }
  asn1cFreeStruc(asn_DEF_NR_RRCRelease, rrc->RRCRelease);
}

void handle_rlf_detection(NR_UE_RRC_INST_t *rrc)
{
  // 5.3.10.3 in 38.331
  bool srb2 = rrc->Srb[2] != RB_NOT_PRESENT;
  bool any_drb = false;
  for (int i = 0; i < MAX_DRBS_PER_UE; i++) {
    if (rrc->status_DRBs[i] != RB_NOT_PRESENT) {
      any_drb = true;
      break;
    }
  }

  if (rrc->as_security_activated && srb2 && any_drb) // initiate the connection re-establishment procedure
    nr_rrc_initiate_rrcReestablishment(rrc, NR_ReestablishmentCause_otherFailure);
  else {
    NR_Release_Cause_t cause = rrc->as_security_activated ? RRC_CONNECTION_FAILURE : OTHER;
    nr_rrc_going_to_IDLE(rrc, cause, NULL);
  }
}

/** @brief T304 expiry (TS 38.331 5.3.5.8.3, non-DAPS): revert back to the source PCell and initiate re-establishment */
void handle_t304_expiry(NR_UE_RRC_INST_t *rrc)
{
  // MAC: release rach-ConfigDedicated, revert back to the source PCell
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_RESET;
  rrc_msg.payload.config_reset.cause = T304_EXPIRED;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);

  if (rrc->ho_source.valid) {
    rrc->phyCellID = rrc->ho_source.phyCellID;
    rrc->rnti = rrc->ho_source.rnti;
    memcpy(rrc->kgnb, rrc->ho_source.kgnb, sizeof(rrc->kgnb));
    memcpy(rrc->nh, rrc->ho_source.nh, sizeof(rrc->nh));
    rrc->nhcc = rrc->ho_source.nhcc;
    rrc->cipheringAlgorithm = rrc->ho_source.cipheringAlgorithm;
    rrc->integrityProtAlgorithm = rrc->ho_source.integrityProtAlgorithm;
    rrc->keyToUse = rrc->ho_source.keyToUse;
    // revert back to the source PCell common configuration
    NR_UE_RRC_SI_INFO *SI_info = &rrc->perNB[0].SInfo;
    asn1cFreeStruc(asn_DEF_NR_SIB1, SI_info->sib1);
    SI_info->sib1 = rrc->ho_source.sib1;
    rrc->ho_source.sib1 = NULL;
    if (SI_info->sib1 && SI_info->sib1_validity) {
      NR_SIB1_t *sib1 = NULL;
      const int copy_result = asn_copy(&asn_DEF_NR_SIB1, (void **)&sib1, SI_info->sib1);
      AssertFatal(copy_result == 0, "unable to copy NR_SIB1_t\n");
      nr_rrc_send_sib1_to_mac(rrc, sib1, false, false);
    }
    if (rrc->ho_source.meas_config)
      RRCLOG_E("Reverting back to the source PCell measConfig not handled (handover command included measConfig)\n");
    if (memcmp(rrc->ho_source.Srb, rrc->Srb, sizeof(rrc->Srb))
        || memcmp(rrc->ho_source.status_DRBs, rrc->status_DRBs, sizeof(rrc->status_DRBs)))
      RRCLOG_E("Reverting back to the source PCell radio bearers not handled (handover command added or released bearers)\n");
  }
  // TODO store the handover failure information in VarRLF-Report
  bool srb2 = rrc->Srb[2] != RB_NOT_PRESENT;
  bool any_drb = false;
  for (int i = 0; i < MAX_DRBS_PER_UE; i++) {
    if (rrc->status_DRBs[i] != RB_NOT_PRESENT) {
      any_drb = true;
      break;
    }
  }

  if (rrc->as_security_activated && srb2 && any_drb)
    nr_rrc_initiate_rrcReestablishment(rrc, NR_ReestablishmentCause_handoverFailure);
  else {
    NR_Release_Cause_t cause = rrc->as_security_activated ? RRC_CONNECTION_FAILURE : OTHER;
    nr_rrc_going_to_IDLE(rrc, cause, NULL);
  }
  nr_rrc_clear_ho_source(rrc);
}

void nr_rrc_going_to_IDLE(NR_UE_RRC_INST_t *rrc,
                          NR_Release_Cause_t release_cause,
                          NR_RRCRelease_t *RRCRelease)
{
  NR_UE_Timers_Constants_t *tac = &rrc->timers_and_constants;
  struct NR_RRCRelease_IEs *rrcReleaseIEs = RRCRelease ? RRCRelease->criticalExtensions.choice.rrcRelease : NULL;

  // if going to RRC_IDLE was triggered by reception
  // of the RRCRelease message including a waitTime
  NR_RejectWaitTime_t *waitTime = NULL;
  if (RRCRelease) {
    if(rrcReleaseIEs) {
      waitTime = rrcReleaseIEs->nonCriticalExtension ?
                 rrcReleaseIEs->nonCriticalExtension->waitTime : NULL;
      if (waitTime) {
        nr_timer_stop(&tac->T302); // stop 302
        // start timer T302 with the value set to the waitTime
        int target = *waitTime * 1000; // waitTime is in seconds
        nr_timer_setup(&tac->T302, target, 10);
        nr_timer_start(&tac->T302);
        // TODO inform upper layers that access barring is applicable
        //      for all access categories except categories '0' and '2'.
        // for now we just set the access barred in RRC
        rrc->access_barred = true;
      }
    }
  }
  if (!waitTime) {
    if (nr_timer_is_active(&tac->T302)) {
      nr_timer_stop(&tac->T302);
      handle_302_expired_stopped(rrc);
    }
  }
  if (nr_timer_is_active(&tac->T390)) {
    nr_timer_stop(&tac->T390);
    // TODO barring alleviation as in 5.3.14.4
    // not implemented
    RRCLOG_E("Go to IDLE. Barring alleviation not implemented\n");
  }
  if (!RRCRelease && rrc->nrRrcState == RRC_STATE_INACTIVE_NR) {
    // TODO discard the cell reselection priority information provided by the cellReselectionPriorities
    // cell reselection priorities not implemented yet
    nr_timer_stop(&tac->T320);
  }
  // Stop all the timers except T302, T320 and T325
  nr_timer_stop(&tac->T300);
  nr_timer_stop(&tac->T301);
  nr_timer_stop(&tac->T304);
  nr_timer_stop(&tac->T310);
  nr_timer_stop(&tac->T311);
  nr_timer_stop(&tac->T319);

  for (int i = 0; i < NB_CNX_UE; i++) {
    l3_measurements_t *l3m = &rrc->perNB[i].l3_measurements;
    for (int j = 0; j < NR_MAX_MEAS_ID; j++) {
      meas_report_params_t *p = &l3m->meas_report[j];
      nr_timer_stop(&p->TA2);
      nr_timer_stop(&p->TA3);
      nr_timer_stop(&p->periodic_report_timer);
    }
  }

  // discard the UE Inactive AS context
  // TODO there is no inactive AS context

  // release the suspendConfig
  // TODO suspendConfig not handled yet

  // discard the keys (only kgnb is stored)
  memset(rrc->kgnb, 0, sizeof(rrc->kgnb));
  rrc->integrityProtAlgorithm = 0;
  rrc->cipheringAlgorithm = 0;

  // release all radio resources, including release of the RLC entity,
  // the MAC configuration and the associated PDCP entity
  // and SDAP for all established RBs
  for (int i = 1; i <= MAX_DRBS_PER_UE; i++) {
    if (get_DRB_status(rrc, i) != RB_NOT_PRESENT) {
      set_DRB_status(rrc, i, RB_NOT_PRESENT);
      nr_pdcp_release_drb(rrc->ue_id, i);
    }
  }
  // Stop UE TUN readers before deleting SDAP entities,
  // then start idle listeners so MO UL can trigger Service Request while CM-IDLE
  nr_rrc_ue_tun_stop_readers(rrc->ue_id);
  nr_sdap_delete_ue_entities(rrc->ue_id);
  if (rrc->nrRrcState != RRC_STATE_DETACH_NR)
    nr_rrc_ue_tun_start_idle_listeners(rrc->ue_id);

  for (int i = 1; i < NR_NUM_SRB; i++) {
    if (rrc->Srb[i] != RB_NOT_PRESENT) {
      rrc->Srb[i] = RB_NOT_PRESENT;
      nr_pdcp_release_srb(rrc->ue_id, i);
    }
  }
  for (int i = 0; i < NR_MAX_NUM_LCID; i++) {
    nr_rrc_release_rlc_entity(rrc, i);
  }

  /* TS 38.331 §5.3.11 enters RRC_IDLE with cell selection per TS 38.304 §5.2.6.
   * With a normal no-redirection RRCRelease, preserve the already-acquired camped-cell context for paging. */
  const bool preserve_camped_context = rrc->nrRrcState != RRC_STATE_DETACH_NR && release_cause == OTHER && RRCRelease
                                       && (!rrcReleaseIEs || !rrcReleaseIEs->redirectedCarrierInfo);
  for (int i = 0; i < NB_CNX_UE; i++) {
    rrcPerNB_t *nb = &rrc->perNB[i];
    NR_UE_RRC_SI_INFO *SI_info = &nb->SInfo;
    init_SI_timers(SI_info);
    SI_info->sib_pending = false;
    if (!preserve_camped_context)
      SI_info->sib1_validity = false;
    SI_info->sib2_validity = false;
    SI_info->sib3_validity = false;
    SI_info->sib4_validity = false;
    SI_info->sib5_validity = false;
    SI_info->sib6_validity = false;
    SI_info->sib7_validity = false;
    SI_info->sib8_validity = false;
    SI_info->sib9_validity = false;
    SI_info->sib10_validity = false;
    SI_info->sib11_validity = false;
    SI_info->sib12_validity = false;
    SI_info->sib13_validity = false;
    SI_info->sib14_validity = false;
    SI_info->SInfo_r17.sib15_validity = false;
    SI_info->SInfo_r17.sib16_validity = false;
    SI_info->SInfo_r17.sib17_validity = false;
    SI_info->SInfo_r17.sib18_validity = false;
    SI_info->SInfo_r17.sib19_validity = false;
    SI_info->SInfo_r17.sib20_validity = false;
    SI_info->SInfo_r17.sib21_validity = false;
  }

  if (rrc->nrRrcState == RRC_STATE_DETACH_NR) {
    asn1cFreeStruc(asn_DEF_NR_UE_NR_Capability, rrc->UECap.UE_NR_Capability);
    asn1cFreeStruc(asn_DEF_NR_UE_TimersAndConstants, tac->sib1_TimersAndConstants);
  }

  // Free Target NTNcfg is stored
  if (rrc->target_ntncfg) {
    ASN_STRUCT_FREE(asn_DEF_NR_NTN_Config_r17, rrc->target_ntncfg);
    rrc->target_ntncfg = NULL;
    rrc->process_target_ntncfg = false;
  }

  // reset MAC
  NR_UE_MAC_reset_cause_t cause = GO_TO_IDLE;
  if (rrc->nrRrcState == RRC_STATE_DETACH_NR)
    cause = DETACH;
  else if (preserve_camped_context)
    cause = GO_TO_IDLE_KEEP_CAMPED;
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_RESET;
  rrc_msg.payload.config_reset.cause = cause;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);

  // enter RRC_IDLE
  RRCLOG_I("RRC moved into IDLE state\n");
  if (rrc->nrRrcState != RRC_STATE_DETACH_NR)
    rrc->nrRrcState = RRC_STATE_IDLE_NR;

  rrc->rnti = 0;

  // Indicate the release of the RRC connection to upper layers
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_NRUE, rrc->ue_id, NR_NAS_CONN_RELEASE_IND);
  NR_NAS_CONN_RELEASE_IND(msg_p).cause = release_cause;
  itti_send_msg_to_task(TASK_NAS_NRUE, rrc->ue_id, msg_p);
}

void handle_302_expired_stopped(NR_UE_RRC_INST_t *rrc)
{
  // for each Access Category for which T390 (TODO not implemented) is not running
  // consider the barring for this Access Category to be alleviated
  rrc->access_barred = false;
}

void handle_t300_expiry(NR_UE_RRC_INST_t *rrc)
{
  rrc->ra_trigger = RRC_CONNECTION_SETUP;
  nr_rrc_ue_prepare_RRCSetupRequest(rrc);

  nr_rrc_trigger_mac_ra(rrc, NR_MAC_RA_START_T300);

  // TODO handle connEstFailureControl
  // TODO inform upper layers about the failure to establish the RRC connection
}

void handle_t430_expiry(NR_UE_RRC_INST_t *rrc)
{
  // SPEC 38.331 section 5.2.2.6
  // Reacquire SIB19 after T430 expiry
  for (int i = 0; i < NB_CNX_UE; i++) {
    rrcPerNB_t *nb = &rrc->perNB[i];
    NR_UE_RRC_SI_INFO *SI_info = &nb->SInfo;
    SI_info->SInfo_r17.sib19_validity = false;
  }
  // Indicate MAC that UL SYNC is LOST
  NR_UE_MAC_reset_cause_t cause = UL_SYNC_LOST_T430_EXPIRED;
  nr_mac_rrc_message_t rrc_msg = {0};
  rrc_msg.payload_type = NR_MAC_RRC_CONFIG_RESET;
  rrc_msg.payload.config_reset.cause = cause;
  nr_rrc_send_msg_to_mac(rrc, &rrc_msg);
}

//This calls the sidelink preconf message after RRC, MAC instances are created.
void start_sidelink(int instance)
{

  NR_UE_RRC_INST_t *rrc = get_NR_UE_rrc_inst(instance);

  if (get_softmodem_params()->sl_mode == 2) {

    //Process the Sidelink Preconfiguration
    rrc_ue_process_sidelink_Preconfiguration(rrc, get_softmodem_params()->sync_ref);

  }
}

void nr_rrc_set_mac_queue(instance_t instance, notifiedFIFO_t *mac_input_nf)
{
  NR_UE_RRC_INST_t *rrc = get_NR_UE_rrc_inst(instance);
  rrc->mac_input_nf = mac_input_nf;
}

void rrc_ue_generate_measurementReport(rrcPerNB_t *rrc, instance_t ue_id, int meas_id)
{
  uint8_t buffer[NR_RRC_BUF_SIZE];
  l3_measurements_t *l3m = &rrc->l3_measurements;
  meas_report_params_t *params = &l3m->meas_report[meas_id];
  int rsrp_dBm = params->rs_type == NR_NR_RS_Type_ssb ? l3m->serving_cell.ss_rsrp_dBm.val : l3m->serving_cell.csi_rsrp_dBm.val;
  int rsrp_index = get_rsrp_index(rsrp_dBm);
  uint16_t neighbor_pcis[NUMBER_OF_NEIGHBORING_CELLS_MAX];
  int neighbor_rsrp_indexes[NUMBER_OF_NEIGHBORING_CELLS_MAX];
  int num_neighbors = 0;

  if (params->neighbor_cell_valid) {
    for (int i = 0; i < NUMBER_OF_NEIGHBORING_CELLS_MAX; i++) {
      bool is_valid = false;

      // Check if this neighboring cell has valid measurements
      if (params->rs_type == NR_NR_RS_Type_ssb) {
        is_valid = l3m->neighboring_cell[i].ss_rsrp_dBm.init;
      } else {
        is_valid = l3m->neighboring_cell[i].csi_rsrp_dBm.init;
      }

      if (is_valid) {
        neighbor_pcis[num_neighbors] = l3m->neighboring_cell[i].Nid_cell;
        int neighbor_rsrp_dBm = params->rs_type == NR_NR_RS_Type_ssb ? l3m->neighboring_cell[i].ss_rsrp_dBm.val
                                                                     : l3m->neighboring_cell[i].csi_rsrp_dBm.val;
        neighbor_rsrp_indexes[num_neighbors] = get_rsrp_index(neighbor_rsrp_dBm);
        num_neighbors++;
      }
    }

    // Sort neighbors in decreasing order of RSRP index per TS 38.331 5.5.5
    for (int i = 0; i < num_neighbors - 1; i++) {
      for (int j = i + 1; j < num_neighbors; j++) {
        if (neighbor_rsrp_indexes[j] > neighbor_rsrp_indexes[i]) {
          int tmp_rsrp = neighbor_rsrp_indexes[i];
          neighbor_rsrp_indexes[i] = neighbor_rsrp_indexes[j];
          neighbor_rsrp_indexes[j] = tmp_rsrp;
          uint16_t tmp_pci = neighbor_pcis[i];
          neighbor_pcis[i] = neighbor_pcis[j];
          neighbor_pcis[j] = tmp_pci;
        }
      }
    }

    if (params->max_report_cells > 0 && num_neighbors > params->max_report_cells)
      num_neighbors = params->max_report_cells;
  }

  LOG_D(NR_RRC, "Generating MeasurementReport with %d neighboring cells\n", num_neighbors);

  uint8_t size = do_nrMeasurementReport_SA(meas_id,
                                           params->trigger_quantity,
                                           params->report_rsrp,
                                           params->rs_type,
                                           l3m->serving_cell.Nid_cell,
                                           rsrp_index,
                                           num_neighbors,
                                           neighbor_pcis,
                                           neighbor_rsrp_indexes,
                                           buffer,
                                           sizeof(buffer));

  int srb_id = 1; // possibly TODO in SRB3 in some cases
  nr_pdcp_data_req_srb(ue_id, srb_id, 0, size, buffer, deliver_pdu_srb_rlc, NULL);
}
