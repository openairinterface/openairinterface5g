/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief rrc S1AP procedures for eNB
 */

#include "rrc_defs.h"
#include "rrc_proto.h"
#include "rrc_extern.h"
#include "RRC/L2_INTERFACE/openair_rrc_L2_interface.h"
#include "RRC/LTE/MESSAGES/asn1_msg.h"
#include "rrc_eNB_UE_context.h"
#include "rrc_eNB_S1AP.h"
#include "enb_config.h"
#include "common/ran_context.h"

#include "s1ap_eNB.h"
#include "s1ap_eNB_defs.h"
#include "s1ap_eNB_management_procedures.h"
#include "s1ap_eNB_ue_context.h"
#include "oai_asn1.h"
#include "intertask_interface.h"
#include "openair2/LAYER2/PDCP_v10.1.0/pdcp.h"
#include "openair2/LAYER2/PDCP_v10.1.0/pdcp_primitives.h"

#include "LTE_UERadioAccessCapabilityInformation.h"
#include "uper_encoder.h"

#include "openair3/ocp-gtpu/gtp_itf.h"
#include <openair3/ocp-gtpu/gtp_itf.h>

#include "openair3/SECU/secu_defs.h"
#include "openair3/SECU/key_nas_deriver.h"

#include "RRC/LTE/rrc_eNB_GTPV1U.h"

#include "TLVDecoder.h"
#include "S1AP_NAS-PDU.h"
#include "executables/softmodem-common.h"
extern RAN_CONTEXT_t RC;

/* Value to indicate an invalid UE initial id */
static const uint16_t UE_INITIAL_ID_INVALID = 0;

/* Masks for S1AP Encryption algorithms, EEA0 is always supported (not coded) */
static const uint16_t S1AP_ENCRYPTION_EEA1_MASK = 0x8000;
static const uint16_t S1AP_ENCRYPTION_EEA2_MASK = 0x4000;

/* Masks for S1AP Integrity algorithms, EIA0 is always supported (not coded) */
static const uint16_t S1AP_INTEGRITY_EIA1_MASK = 0x8000;
static const uint16_t S1AP_INTEGRITY_EIA2_MASK = 0x4000;

#define INTEGRITY_ALGORITHM_NONE LTE_SecurityAlgorithmConfig__integrityProtAlgorithm_eia0_v920


void extract_imsi(uint8_t *pdu_buf, uint32_t pdu_len, rrc_eNB_ue_context_t *ue_context_pP) {
  /* Process NAS message locally to get the IMSI */
  nas_message_t nas_msg;
  memset(&nas_msg, 0, sizeof(nas_message_t));
  int size = 0;
  nas_message_security_header_t *header = &nas_msg.header;
  /* Decode the first octet of the header (security header type or EPS
  * bearer identity, and protocol discriminator) */
  DECODE_U8((char *) pdu_buf, *(uint8_t *) (header), size);

  /* Decode NAS message only if decodable*/
  if (!(header->security_header_type <= SECURITY_HEADER_TYPE_INTEGRITY_PROTECTED
        && header->protocol_discriminator == EPS_MOBILITY_MANAGEMENT_MESSAGE
        && pdu_len > NAS_MESSAGE_SECURITY_HEADER_SIZE))
    return;

  /* Decode plain NAS message */
  EMM_msg *e_msg = &nas_msg.plain.emm;
  emm_msg_header_t *emm_header = &e_msg->header;

  if (header->security_header_type != SECURITY_HEADER_TYPE_NOT_PROTECTED) {
    /* Decode the message authentication code */
    DECODE_U32((char *) pdu_buf+size, header->message_authentication_code, size);
    /* Decode the sequence number */
    DECODE_U8((char *) pdu_buf+size, header->sequence_number, size);
    /* Decode the security header type and the protocol discriminator */
    DECODE_U8(pdu_buf + size, *(uint8_t *)(emm_header), size);

    /* Check that this is the right message */
    if (emm_header->protocol_discriminator != EPS_MOBILITY_MANAGEMENT_MESSAGE)
      return;
  }
  pdu_buf += size;
  pdu_len -= size;

  /* Check that buffer contains more than only the header */
  if (pdu_len <= sizeof(emm_msg_header_t))
    return;

  /* First decode the EMM message header */
  int e_head_size = 0;
  /* Decode the message type */
  DECODE_U8(pdu_buf + e_head_size, emm_header->message_type, e_head_size);

  pdu_buf += e_head_size;
  pdu_len -= e_head_size;

  if (emm_header->message_type == IDENTITY_RESPONSE) {
    decode_identity_response(&e_msg->identity_response, pdu_buf, pdu_len);

    if (e_msg->identity_response.mobileidentity.imsi.typeofidentity == MOBILE_IDENTITY_IMSI) {
      memcpy(&ue_context_pP->ue_context.imsi,
             &e_msg->identity_response.mobileidentity.imsi,
             sizeof(ImsiMobileIdentity_t));
    }
  } else if (emm_header->message_type == ATTACH_REQUEST) {
    decode_attach_request(&e_msg->attach_request, pdu_buf, pdu_len);

    if (e_msg->attach_request.oldgutiorimsi.imsi.typeofidentity == MOBILE_IDENTITY_IMSI) {
      /* the following is very dirty, we cast (implicitly) from
       * ImsiEpsMobileIdentity_t to ImsiMobileIdentity_t*/
      memcpy(&ue_context_pP->ue_context.imsi,
             &e_msg->attach_request.oldgutiorimsi.imsi,
             sizeof(ImsiMobileIdentity_t));
    }
  }
}


//------------------------------------------------------------------------------
/*
* Get the UE S1 struct containing hashtables S1_id/UE_id.
* Is also used to set the S1_id of the UE, depending on inputs.
*/
struct rrc_ue_s1ap_ids_s *
rrc_eNB_S1AP_get_ue_ids(
  eNB_RRC_INST *const rrc_instance_pP,
  const uint16_t ue_initial_id,
  const uint32_t eNB_ue_s1ap_id
)
//------------------------------------------------------------------------------
{
  rrc_ue_s1ap_ids_t *result = NULL;
  rrc_ue_s1ap_ids_t *result2 = NULL;
  /*****************************/
  instance_t instance = 0;
  s1ap_eNB_instance_t *s1ap_eNB_instance_p = NULL;
  s1ap_eNB_ue_context_t *ue_desc_p = NULL;
  /*****************************/
  hashtable_rc_t     h_rc;

  if (ue_initial_id != UE_INITIAL_ID_INVALID) {
    h_rc = hashtable_get(rrc_instance_pP->initial_id2_s1ap_ids, (hash_key_t)ue_initial_id, (void **)&result);

    if (h_rc == HASH_TABLE_OK) {
      if (eNB_ue_s1ap_id > 0) {
        h_rc = hashtable_get(rrc_instance_pP->s1ap_id2_s1ap_ids, (hash_key_t)eNB_ue_s1ap_id, (void **)&result2);

        if (h_rc != HASH_TABLE_OK) { // this case is equivalent to associate eNB_ue_s1ap_id and ue_initial_id
          result2 = malloc(sizeof(*result2));

          if (NULL != result2) {
            *result2 = *result;
            result2->eNB_ue_s1ap_id = eNB_ue_s1ap_id;
            result->eNB_ue_s1ap_id  = eNB_ue_s1ap_id;
            h_rc = hashtable_insert(rrc_instance_pP->s1ap_id2_s1ap_ids, (hash_key_t)eNB_ue_s1ap_id, result2, free);

            if (h_rc != HASH_TABLE_OK) {
              LOG_E(S1AP, "[eNB %ld] Error while hashtable_insert in s1ap_id2_s1ap_ids eNB_ue_s1ap_id %"PRIu32"\n",
                    rrc_instance_pP - RC.rrc[0],
                    eNB_ue_s1ap_id);
            }
          }
        } else { // here we should check that the association was done correctly
          if ((result->ue_initial_id != result2->ue_initial_id) || (result->eNB_ue_s1ap_id != result2->eNB_ue_s1ap_id)) {
            LOG_E(S1AP, "[eNB %ld] Error while hashtable_get, two rrc_ue_s1ap_ids_t that should be equal, are not:\n \
              ue_initial_id 1 = %"PRIu16",\n \
              ue_initial_id 2 = %"PRIu16",\n \
              eNB_ue_s1ap_id 1 = %"PRIu32",\n \
              eNB_ue_s1ap_id 2 = %"PRIu32"\n",
                  rrc_instance_pP - RC.rrc[0],
                  result->ue_initial_id,
                  result2->ue_initial_id,
                  result->eNB_ue_s1ap_id,
                  result2->eNB_ue_s1ap_id);
            // Still return *result
          }
        }
      } // end if if (eNB_ue_s1ap_id > 0)
    } else { // end if (h_rc == HASH_TABLE_OK)
      LOG_E(S1AP, "[eNB %ld] In hashtable_get, couldn't find in initial_id2_s1ap_ids ue_initial_id %"PRIu16"\n",
            rrc_instance_pP - RC.rrc[0],
            ue_initial_id);
      return NULL;
      /*
      * At the moment this is written, this case shouldn't (cannot) happen and is equivalent to an error.
      * One could try to find the struct instance based on s1ap_id2_s1ap_ids and eNB_ue_s1ap_id (if > 0),
      * but this behavior is not expected at the moment.
      */
    } // end else (h_rc != HASH_TABLE_OK)
  } else { // end if (ue_initial_id != UE_INITIAL_ID_INVALID)
    if (eNB_ue_s1ap_id > 0) {
      h_rc = hashtable_get(rrc_instance_pP->s1ap_id2_s1ap_ids, (hash_key_t)eNB_ue_s1ap_id, (void **)&result);

      if (h_rc != HASH_TABLE_OK) {
        /*
        * This case is uncommon, but can happen when:
        * -> if the first NAS message was a Detach Request (non exhaustiv), the UE RRC context exist
        * but is not associated with eNB_ue_s1ap_id
        * -> ... (?)
        */
        LOG_E(S1AP, "[eNB %ld] In hashtable_get, couldn't find in s1ap_id2_s1ap_ids eNB_ue_s1ap_id %"PRIu32", trying to find it through S1AP context\n",
              rrc_instance_pP - RC.rrc[0],
              eNB_ue_s1ap_id);
        instance = ENB_MODULE_ID_TO_INSTANCE(rrc_instance_pP - RC.rrc[0]); // get eNB instance
        s1ap_eNB_instance_p = s1ap_eNB_get_instance(instance); // get s1ap_eNB_instance

        if (s1ap_eNB_instance_p != NULL) {
          ue_desc_p = s1ap_eNB_get_ue_context(s1ap_eNB_instance_p, eNB_ue_s1ap_id); // get s1ap_eNB_ue_context
        } else {
          LOG_E(S1AP, "[eNB instance %ld] Couldn't find the eNB S1AP context\n",
                instance);
          return NULL;
        }

        if (ue_desc_p != NULL) {
          struct s1ap_eNB_ue_context_s *s1ap_ue_context_p = NULL;

          if ((s1ap_ue_context_p = RB_REMOVE(s1ap_ue_map, &s1ap_eNB_instance_p->s1ap_ue_head, ue_desc_p)) != NULL) {
            LOG_E(RRC, "Removed UE context eNB_ue_s1ap_id %u\n", s1ap_ue_context_p->eNB_ue_s1ap_id);
            s1ap_eNB_free_ue_context(s1ap_ue_context_p);
          } else {
            LOG_E(RRC, "Removing UE context eNB_ue_s1ap_id %u: did not find context\n",ue_desc_p->eNB_ue_s1ap_id);
          }

          return NULL; 
        } else {
          LOG_E(S1AP, "[eNB %ld] In hashtable_get, couldn't find in s1ap_id2_s1ap_ids eNB_ue_s1ap_id %"PRIu32", because ue_initial_id is invalid in S1AP context\n",
                rrc_instance_pP - RC.rrc[0],
                eNB_ue_s1ap_id);
          return NULL;
        }
      } // end if (h_rc != HASH_TABLE_OK)
    } // end if (eNB_ue_s1ap_id > 0)
  } // end else (ue_initial_id == UE_INITIAL_ID_INVALID)

  return result;
}

//------------------------------------------------------------------------------
/*
* Remove UE ids (ue_initial_id and S1_id) from hashtables.
*/
void
rrc_eNB_S1AP_remove_ue_ids(
  eNB_RRC_INST *const rrc_instance_pP,
  struct rrc_ue_s1ap_ids_s *const ue_ids_pP
)
//------------------------------------------------------------------------------
{
  hashtable_rc_t h_rc;

  if (rrc_instance_pP == NULL) {
    LOG_E(RRC, "Bad RRC instance\n");
    return;
  }

  if (ue_ids_pP == NULL) {
    LOG_E(RRC, "Trying to free a NULL S1AP UE IDs\n");
    return;
  }

  const uint16_t ue_initial_id  = ue_ids_pP->ue_initial_id;
  const uint32_t eNB_ue_s1ap_id = ue_ids_pP->eNB_ue_s1ap_id;

  if (eNB_ue_s1ap_id > 0) {
    h_rc = hashtable_remove(rrc_instance_pP->s1ap_id2_s1ap_ids, (hash_key_t)eNB_ue_s1ap_id, free);

    if (h_rc != HASH_TABLE_OK) {
      LOG_W(RRC, "S1AP Did not find entry in hashtable s1ap_id2_s1ap_ids for eNB_ue_s1ap_id %u\n", eNB_ue_s1ap_id);
    } else {
      LOG_W(RRC, "S1AP removed entry in hashtable s1ap_id2_s1ap_ids for eNB_ue_s1ap_id %u\n", eNB_ue_s1ap_id);
    }
  }

  if (ue_initial_id != UE_INITIAL_ID_INVALID) {
    h_rc = hashtable_remove(rrc_instance_pP->initial_id2_s1ap_ids, (hash_key_t)ue_initial_id, free);

    if (h_rc != HASH_TABLE_OK) {
      LOG_W(RRC, "S1AP Did not find entry in hashtable initial_id2_s1ap_ids for ue_initial_id %u\n", ue_initial_id);
    } else {
      LOG_W(RRC, "S1AP removed entry in hashtable initial_id2_s1ap_ids for ue_initial_id %u\n", ue_initial_id);
    }
  }
}

/*! \fn uint16_t get_next_ue_initial_id(uint8_t mod_id)
 *\brief provide an UE initial ID for S1AP initial communication.
 *\param mod_id Instance ID of eNB.
 *\return the UE initial ID.
 */
//------------------------------------------------------------------------------
static uint16_t
get_next_ue_initial_id(
  const module_id_t mod_id
)
//------------------------------------------------------------------------------
{
  static uint16_t ue_initial_id[NUMBER_OF_eNB_MAX];
  ue_initial_id[mod_id]++;

  /* Never use UE_INITIAL_ID_INVALID this is the invalid id! */
  if (ue_initial_id[mod_id] == UE_INITIAL_ID_INVALID) {
    ue_initial_id[mod_id]++;
  }

  return ue_initial_id[mod_id];
}

/*! \fn rrc_eNB_get_ue_context_from_s1ap_ids
 *\brief retrieve UE index in the eNB from the UE initial ID if not equal to UE_INDEX_INVALID or
 *\brief from the eNB_ue_s1ap_id previously transmitted by S1AP.
 *\param instanceP Instance ID of eNB.
 *\param ue_initial_id The UE initial ID sent to S1AP.
 *\param eNB_ue_s1ap_id The value sent by S1AP.
 *\return the UE index or UE_INDEX_INVALID if not found.
 */
static struct rrc_eNB_ue_context_s *
rrc_eNB_get_ue_context_from_s1ap_ids(
  const instance_t  instanceP,
  const uint16_t    ue_initial_idP,
  const uint32_t    eNB_ue_s1ap_idP
) {
  rrc_ue_s1ap_ids_t *temp = NULL;
  temp = rrc_eNB_S1AP_get_ue_ids(RC.rrc[ENB_INSTANCE_TO_MODULE_ID(instanceP)], ue_initial_idP, eNB_ue_s1ap_idP);

  if (temp != NULL) {
    return rrc_eNB_get_ue_context(RC.rrc[ENB_INSTANCE_TO_MODULE_ID(instanceP)], temp->ue_rnti);
  }

  return NULL;
}

/*! \fn e_SecurityAlgorithmConfig__cipheringAlgorithm rrc_eNB_select_ciphering(uint16_t algorithms)
 *\brief analyze available encryption algorithms bit mask and return the relevant one.
 *\param algorithms The bit mask of available algorithms received from S1AP.
 *\return the selected algorithm.
 */
static LTE_CipheringAlgorithm_r12_t rrc_eNB_select_ciphering(uint16_t algorithms) {
  //#warning "Forced   return SecurityAlgorithmConfig__cipheringAlgorithm_eea0, to be deleted in future"
  return LTE_CipheringAlgorithm_r12_eea0;

  if (algorithms & S1AP_ENCRYPTION_EEA2_MASK) {
    return LTE_CipheringAlgorithm_r12_eea2;
  }

  if (algorithms & S1AP_ENCRYPTION_EEA1_MASK) {
    return LTE_CipheringAlgorithm_r12_eea1;
  }

  return LTE_CipheringAlgorithm_r12_eea0;
}

/*! \fn e_SecurityAlgorithmConfig__integrityProtAlgorithm rrc_eNB_select_integrity(uint16_t algorithms)
 *\brief analyze available integrity algorithms bit mask and return the relevant one.
 *\param algorithms The bit mask of available algorithms received from S1AP.
 *\return the selected algorithm.
 */
static e_LTE_SecurityAlgorithmConfig__integrityProtAlgorithm rrc_eNB_select_integrity(uint16_t algorithms) {
  if (algorithms & S1AP_INTEGRITY_EIA2_MASK) {
    return LTE_SecurityAlgorithmConfig__integrityProtAlgorithm_eia2;
  }

  if (algorithms & S1AP_INTEGRITY_EIA1_MASK) {
    return LTE_SecurityAlgorithmConfig__integrityProtAlgorithm_eia1;
  }

  return INTEGRITY_ALGORITHM_NONE;
}

/*! \fn int rrc_eNB_process_security(const protocol_ctxt_t *const ctxt_pP,
                         rrc_eNB_ue_context_t *const ue_context_pP,
                         security_capabilities_t *security_capabilities_pP)
 *\brief save and analyze available security algorithms bit mask and select relevant ones.
 *\param mod_id Instance ID of eNB.
 *\param ue_index Instance ID of UE in the eNB.
 *\param security_capabilities The security capabilities received from S1AP.
 *\return true if at least one algorithm has been changed else false.
 */
int
rrc_eNB_process_security(const protocol_ctxt_t *const ctxt_pP,
                         rrc_eNB_ue_context_t *const ue_context_pP,
                         security_capabilities_t *security_capabilities_pP) {
  bool                                                  changed = false;
  LTE_CipheringAlgorithm_r12_t                          cipheringAlgorithm;
  e_LTE_SecurityAlgorithmConfig__integrityProtAlgorithm integrityProtAlgorithm;
  /* Save security parameters */
  ue_context_pP->ue_context.security_capabilities = *security_capabilities_pP;
  // translation
  LOG_D(RRC,
        "[eNB %d] NAS security_capabilities.encryption_algorithms %u AS ciphering_algorithm %lu NAS security_capabilities.integrity_algorithms %u AS integrity_algorithm %u\n",
        ctxt_pP->module_id,
        ue_context_pP->ue_context.security_capabilities.encryption_algorithms,
        (unsigned long)ue_context_pP->ue_context.ciphering_algorithm,
        ue_context_pP->ue_context.security_capabilities.integrity_algorithms,
        ue_context_pP->ue_context.integrity_algorithm);
  /* Select relevant algorithms */
  cipheringAlgorithm = rrc_eNB_select_ciphering (ue_context_pP->ue_context.security_capabilities.encryption_algorithms);

  if (ue_context_pP->ue_context.ciphering_algorithm != cipheringAlgorithm) {
    ue_context_pP->ue_context.ciphering_algorithm = cipheringAlgorithm;
    changed = true;
  }

  integrityProtAlgorithm = rrc_eNB_select_integrity (ue_context_pP->ue_context.security_capabilities.integrity_algorithms);

  if (ue_context_pP->ue_context.integrity_algorithm != integrityProtAlgorithm) {
    ue_context_pP->ue_context.integrity_algorithm = integrityProtAlgorithm;
    changed = true;
  }

  LOG_I (RRC, "[eNB %d][UE %x] Selected security algorithms (%p): %lx, %x, %s\n",
         ctxt_pP->module_id,
         ue_context_pP->ue_context.rnti,
         security_capabilities_pP,
         (unsigned long)cipheringAlgorithm,
         integrityProtAlgorithm,
         changed ? "changed" : "same");
  return changed;
}

/*! \fn void process_eNB_security_key (const protocol_ctxt_t* const ctxt_pP, eNB_RRC_UE_t * const ue_context_pP, uint8_t *security_key)
 *\brief save security key.
 *\param ctxt_pP         Running context.
 *\param ue_context_pP   UE context.
 *\param security_key_pP The security key received from S1AP.
 */
//------------------------------------------------------------------------------
void process_eNB_security_key (
  const protocol_ctxt_t *const ctxt_pP,
  rrc_eNB_ue_context_t          *const ue_context_pP,
  uint8_t               *security_key_pP
)
//------------------------------------------------------------------------------
{
  char ascii_buffer[65];
  uint8_t i;
  /* Saves the security key */
  memcpy (ue_context_pP->ue_context.kenb, security_key_pP, SECURITY_KEY_LENGTH);
  memset (ue_context_pP->ue_context.nh, 0, SECURITY_KEY_LENGTH);
  ue_context_pP->ue_context.nh_ncc = -1;

  for (i = 0; i < 32; i++) {
    sprintf(&ascii_buffer[2 * i], "%02X", ue_context_pP->ue_context.kenb[i]);
  }

  ascii_buffer[2 * i] = '\0';
  LOG_I (RRC, "[eNB %d][UE %x] Saved security key %s\n", ctxt_pP->module_id, ue_context_pP->ue_context.rnti, ascii_buffer);
}


//------------------------------------------------------------------------------
void
rrc_pdcp_config_security(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_eNB_ue_context_t          *const ue_context_pP,
  const uint8_t send_security_mode_command
)
//------------------------------------------------------------------------------
{
  LTE_SRB_ToAddModList_t             *SRB_configList = ue_context_pP->ue_context.SRB_configList;
  uint8_t kRRCenc[32] = {0};
  uint8_t kRRCint[32] = {0};
  uint8_t kUPenc[32] = {0};
  pdcp_t                             *pdcp_p   = NULL;
  static int                          print_keys= 1;
  hashtable_rc_t                      h_rc;
  hash_key_t                          key;

  /* Derive the keys from kenb */
  if (SRB_configList != NULL) {
    derive_key_nas(UP_ENC_ALG, ue_context_pP->ue_context.ciphering_algorithm, ue_context_pP->ue_context.kenb, kUPenc);
  }

  derive_key_nas(RRC_ENC_ALG, ue_context_pP->ue_context.ciphering_algorithm, ue_context_pP->ue_context.kenb, kRRCenc);
  derive_key_nas(RRC_INT_ALG, ue_context_pP->ue_context.integrity_algorithm, ue_context_pP->ue_context.kenb, kRRCint);
  if (!IS_SOFTMODEM_IQPLAYER) {
    SET_LOG_DUMP(DEBUG_SECURITY);
 }


  if ( LOG_DUMPFLAG( DEBUG_SECURITY ) ) {
    if (print_keys ==1 ) {
      print_keys =0;
      LOG_DUMPMSG(RRC, DEBUG_SECURITY, ue_context_pP->ue_context.kenb, 32,"\nKeNB:" );
      LOG_DUMPMSG(RRC, DEBUG_SECURITY, kRRCenc, 32,"\nKRRCenc:" );
      LOG_DUMPMSG(RRC, DEBUG_SECURITY, kRRCint, 32,"\nKRRCint:" );
    }
  }

  key = PDCP_COLL_KEY_VALUE(ctxt_pP->module_id, ctxt_pP->rntiMaybeUEid, ctxt_pP->enb_flag, DCCH, SRB_FLAG_YES);
  h_rc = hashtable_get(pdcp_coll_p, key, (void **)&pdcp_p);

  if (h_rc == HASH_TABLE_OK) {
    pdcp_config_set_security(
      ctxt_pP,
      pdcp_p,
      (send_security_mode_command == true)  ?
      0 | (ue_context_pP->ue_context.integrity_algorithm << 4) :
      (ue_context_pP->ue_context.ciphering_algorithm )         |
      (ue_context_pP->ue_context.integrity_algorithm << 4),
      kRRCenc,
      kRRCint,
      kUPenc);
  } else {
    LOG_E(RRC,
          PROTOCOL_RRC_CTXT_UE_FMT"Could not get PDCP instance for SRB DCCH %u\n",
          PROTOCOL_RRC_CTXT_UE_ARGS(ctxt_pP),
          DCCH);
  }
}

//------------------------------------------------------------------------------
void
rrc_eNB_send_S1AP_INITIAL_CONTEXT_SETUP_RESP(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_eNB_ue_context_t          *const ue_context_pP
)
//------------------------------------------------------------------------------
{
  MessageDef      *msg_p         = NULL;
  int e_rab;
  int e_rabs_done = 0;
  int e_rabs_failed = 0;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_INITIAL_CONTEXT_SETUP_RESP);
  S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;

  for (e_rab = 0; e_rab < ue_context_pP->ue_context.nb_of_e_rabs; e_rab++) {
   if (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_DONE || ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_TOMODIFY) {
      e_rabs_done++;
      S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).e_rabs[e_rab].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
      // TODO add other information from S1-U when it will be integrated
      S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).e_rabs[e_rab].gtp_teid = ue_context_pP->ue_context.enb_gtp_teid[e_rab];
      S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).e_rabs[e_rab].eNB_addr = ue_context_pP->ue_context.enb_gtp_addrs[e_rab];
      S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).e_rabs[e_rab].eNB_addr.length = 4;
      if (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_DONE) {
        ue_context_pP->ue_context.e_rab[e_rab].status = E_RAB_STATUS_ESTABLISHED;
        S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).nb_of_e_rabs = e_rabs_done;
        S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).nb_of_e_rabs_failed = e_rabs_failed;
        itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
      }

    } else {
      e_rabs_failed++;
      ue_context_pP->ue_context.e_rab[e_rab].status = E_RAB_STATUS_FAILED;
      S1AP_INITIAL_CONTEXT_SETUP_RESP (msg_p).e_rabs_failed[e_rab].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
      // TODO add cause when it will be integrated
    }
  }
}

//------------------------------------------------------------------------------
void
rrc_eNB_send_S1AP_UPLINK_NAS(
  const protocol_ctxt_t    *const ctxt_pP,
  rrc_eNB_ue_context_t             *const ue_context_pP,
  LTE_UL_DCCH_Message_t        *const ul_dcch_msg
)
//------------------------------------------------------------------------------
{
  {
    LTE_ULInformationTransfer_t *ulInformationTransfer = &ul_dcch_msg->message.choice.c1.choice.ulInformationTransfer;

    if ((ulInformationTransfer->criticalExtensions.present == LTE_ULInformationTransfer__criticalExtensions_PR_c1)
        && (ulInformationTransfer->criticalExtensions.choice.c1.present
            == LTE_ULInformationTransfer__criticalExtensions__c1_PR_ulInformationTransfer_r8)
        && (ulInformationTransfer->criticalExtensions.choice.c1.choice.ulInformationTransfer_r8.dedicatedInfoType.present
            == LTE_ULInformationTransfer_r8_IEs__dedicatedInfoType_PR_dedicatedInfoNAS)) {
      /* This message hold a dedicated info NAS payload, forward it to NAS */
      struct LTE_ULInformationTransfer_r8_IEs__dedicatedInfoType *dedicatedInfoType =
          &ulInformationTransfer->criticalExtensions.choice.c1.choice.ulInformationTransfer_r8.dedicatedInfoType;
      uint32_t pdu_length;
      uint8_t *pdu_buffer;
      MessageDef *msg_p;
      pdu_length = dedicatedInfoType->choice.dedicatedInfoNAS.size;
      pdu_buffer = dedicatedInfoType->choice.dedicatedInfoNAS.buf;
      msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_UPLINK_NAS);
      S1AP_UPLINK_NAS (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;
      S1AP_UPLINK_NAS (msg_p).nas_pdu.length = pdu_length;
      S1AP_UPLINK_NAS (msg_p).nas_pdu.buffer = pdu_buffer;
      extract_imsi(S1AP_UPLINK_NAS (msg_p).nas_pdu.buffer,
                   S1AP_UPLINK_NAS (msg_p).nas_pdu.length,
                   ue_context_pP);
      itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
    }
  }
}

//------------------------------------------------------------------------------
void rrc_eNB_send_S1AP_UE_CAPABILITIES_IND(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_eNB_ue_context_t          *const ue_context_pP,
  LTE_UL_DCCH_Message_t *ul_dcch_msg
)
//------------------------------------------------------------------------------
{
  LTE_UECapabilityInformation_t *ueCapabilityInformation = &ul_dcch_msg->message.choice.c1.choice.ueCapabilityInformation;
  /* 4096 is arbitrary, should be big enough */
  unsigned char buf[4096];
  unsigned char *buf2;
  LTE_UERadioAccessCapabilityInformation_t rac;

  if (ueCapabilityInformation->criticalExtensions.present != LTE_UECapabilityInformation__criticalExtensions_PR_c1
      || ueCapabilityInformation->criticalExtensions.choice.c1.present != LTE_UECapabilityInformation__criticalExtensions__c1_PR_ueCapabilityInformation_r8) {
    LOG_E(RRC, "[eNB %d][UE %x] bad UE capabilities\n", ctxt_pP->module_id, ue_context_pP->ue_context.rnti);
    return;
  }

  asn_enc_rval_t ret = uper_encode_to_buffer(&asn_DEF_LTE_UECapabilityInformation, NULL, ueCapabilityInformation, buf, 4096);

  if (ret.encoded == -1) abort();

  memset(&rac, 0, sizeof(LTE_UERadioAccessCapabilityInformation_t));
  rac.criticalExtensions.present = LTE_UERadioAccessCapabilityInformation__criticalExtensions_PR_c1;
  rac.criticalExtensions.choice.c1.present = LTE_UERadioAccessCapabilityInformation__criticalExtensions__c1_PR_ueRadioAccessCapabilityInformation_r8;
  rac.criticalExtensions.choice.c1.choice.ueRadioAccessCapabilityInformation_r8.ue_RadioAccessCapabilityInfo.buf = buf;
  rac.criticalExtensions.choice.c1.choice.ueRadioAccessCapabilityInformation_r8.ue_RadioAccessCapabilityInfo.size = (ret.encoded+7)/8;
  rac.criticalExtensions.choice.c1.choice.ueRadioAccessCapabilityInformation_r8.nonCriticalExtension = NULL;
  /* 8192 is arbitrary, should be big enough */
  buf2 = malloc16(8192);

  if (buf2 == NULL) abort();

  ret = uper_encode_to_buffer(&asn_DEF_LTE_UERadioAccessCapabilityInformation, NULL, &rac, buf2, 8192);

  if (ret.encoded == -1) abort();

  MessageDef *msg_p;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_UE_CAPABILITIES_IND);
  S1AP_UE_CAPABILITIES_IND (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;
  S1AP_UE_CAPABILITIES_IND (msg_p).ue_radio_cap.length = (ret.encoded+7)/8;
  S1AP_UE_CAPABILITIES_IND (msg_p).ue_radio_cap.buffer = buf2;
  itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
}

//------------------------------------------------------------------------------
/*
* Initial UE NAS message on S1AP.
*/
void
rrc_eNB_send_S1AP_NAS_FIRST_REQ(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_eNB_ue_context_t *const ue_context_pP,
  LTE_RRCConnectionSetupComplete_r8_IEs_t *rrcConnectionSetupComplete
)
//------------------------------------------------------------------------------
{
  eNB_RRC_INST *rrc = RC.rrc[ctxt_pP->module_id];
  {
    MessageDef         *message_p         = NULL;
    rrc_ue_s1ap_ids_t  *rrc_ue_s1ap_ids_p = NULL;
    hashtable_rc_t      h_rc;
    message_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_NAS_FIRST_REQ);
    memset(&message_p->ittiMsg.s1ap_nas_first_req, 0, sizeof(s1ap_nas_first_req_t));
    ue_context_pP->ue_context.ue_initial_id = get_next_ue_initial_id(ctxt_pP->module_id);
    S1AP_NAS_FIRST_REQ(message_p).ue_initial_id = ue_context_pP->ue_context.ue_initial_id;
    rrc_ue_s1ap_ids_p = malloc(sizeof(*rrc_ue_s1ap_ids_p));
    rrc_ue_s1ap_ids_p->ue_initial_id  = ue_context_pP->ue_context.ue_initial_id;
    rrc_ue_s1ap_ids_p->eNB_ue_s1ap_id = UE_INITIAL_ID_INVALID;
    rrc_ue_s1ap_ids_p->ue_rnti = ctxt_pP->rntiMaybeUEid;
    h_rc = hashtable_insert(RC.rrc[ctxt_pP->module_id]->initial_id2_s1ap_ids,
                            (hash_key_t)ue_context_pP->ue_context.ue_initial_id,
                            rrc_ue_s1ap_ids_p, free);

    if (h_rc != HASH_TABLE_OK) {
      LOG_E(S1AP, "[eNB %d] Error while hashtable_insert in initial_id2_s1ap_ids ue_initial_id %u\n",
            ctxt_pP->module_id,
            ue_context_pP->ue_context.ue_initial_id);
    }

    /* Assume that cause is coded in the same way in RRC and S1ap, just check that the value is in S1ap range */
    AssertFatal(ue_context_pP->ue_context.establishment_cause < RRC_CAUSE_LAST,
                "Establishment cause invalid (%jd/%d) for eNB %d!",
                ue_context_pP->ue_context.establishment_cause,
                RRC_CAUSE_LAST,
                ctxt_pP->module_id);
    S1AP_NAS_FIRST_REQ (message_p).establishment_cause = ue_context_pP->ue_context.establishment_cause;
    /* Forward NAS message */
    S1AP_NAS_FIRST_REQ (message_p).nas_pdu.buffer = rrcConnectionSetupComplete->dedicatedInfoNAS.buf;
    S1AP_NAS_FIRST_REQ (message_p).nas_pdu.length = rrcConnectionSetupComplete->dedicatedInfoNAS.size;
    extract_imsi(S1AP_NAS_FIRST_REQ (message_p).nas_pdu.buffer,
                 S1AP_NAS_FIRST_REQ (message_p).nas_pdu.length,
                 ue_context_pP);
    /* Fill UE identities with available information */
    {
      S1AP_NAS_FIRST_REQ (message_p).ue_identity.presenceMask = UE_IDENTITIES_NONE;

      if (ue_context_pP->ue_context.Initialue_identity_s_TMSI.presence) {
        /* Fill s-TMSI */
        UE_S_TMSI *s_TMSI = &ue_context_pP->ue_context.Initialue_identity_s_TMSI;
        S1AP_NAS_FIRST_REQ (message_p).ue_identity.presenceMask |= UE_IDENTITIES_s_tmsi;
        S1AP_NAS_FIRST_REQ (message_p).ue_identity.s_tmsi.mme_code = s_TMSI->mme_code;
        S1AP_NAS_FIRST_REQ (message_p).ue_identity.s_tmsi.m_tmsi = s_TMSI->m_tmsi;
        LOG_I(S1AP, "[eNB %d] Build S1AP_NAS_FIRST_REQ with s_TMSI: MME code %u M-TMSI %u ue %x\n",
              ctxt_pP->module_id,
              S1AP_NAS_FIRST_REQ (message_p).ue_identity.s_tmsi.mme_code,
              S1AP_NAS_FIRST_REQ (message_p).ue_identity.s_tmsi.m_tmsi,
              ue_context_pP->ue_context.rnti);
      } // end if S-TMSI presence

      /* selected_plmn_identity: IE is 1-based, convert to 0-based (C array) */
      int selected_plmn_identity = rrcConnectionSetupComplete->selectedPLMN_Identity - 1;
      S1AP_NAS_FIRST_REQ(message_p).selected_plmn_identity = selected_plmn_identity;

      if (rrcConnectionSetupComplete->registeredMME != NULL) {
        /* Fill GUMMEI */
        struct LTE_RegisteredMME *r_mme = rrcConnectionSetupComplete->registeredMME;
        S1AP_NAS_FIRST_REQ (message_p).ue_identity.presenceMask |= UE_IDENTITIES_gummei;

        if (r_mme->plmn_Identity != NULL) {
          if ((r_mme->plmn_Identity->mcc != NULL) && (r_mme->plmn_Identity->mcc->list.count == 3))
          {
            S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mcc = (*r_mme->plmn_Identity->mcc->list.array[0] & 0xf) * 100 +
                                                                    (*r_mme->plmn_Identity->mcc->list.array[1] & 0xf) * 10 +
                                                                    (*r_mme->plmn_Identity->mcc->list.array[2] & 0xf);
            LOG_I(S1AP, "[eNB %d] Build S1AP_NAS_FIRST_REQ adding in s_TMSI: GUMMEI MCC %u ue %x\n",
                  ctxt_pP->module_id,
                  S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mcc,
                  ue_context_pP->ue_context.rnti);
          }
          if(r_mme->plmn_Identity->mnc.list.count == 3)
          {
            S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc = (*r_mme->plmn_Identity->mnc.list.array[0] & 0xf) * 100 +
                                                                    (*r_mme->plmn_Identity->mnc.list.array[1] & 0xf) * 10 +
                                                                    (*r_mme->plmn_Identity->mnc.list.array[2] & 0xf);
            S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc_len = 3;
            LOG_I(S1AP, "[eNB %d] Build S1AP_NAS_FIRST_REQ adding in s_TMSI: GUMMEI MNC %u %udigit ue %x\n",
                  ctxt_pP->module_id,
                  S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc,
                  S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc_len,
                  ue_context_pP->ue_context.rnti);
          }
          else if(r_mme->plmn_Identity->mnc.list.count == 2)
          {
            S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc = (*r_mme->plmn_Identity->mnc.list.array[0] & 0xf) * 10 +
                                                                    (*r_mme->plmn_Identity->mnc.list.array[1] & 0xf);
            S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc_len = 2;
            LOG_I(S1AP, "[eNB %d] Build S1AP_NAS_FIRST_REQ adding in s_TMSI: GUMMEI MNC %u %udigit ue %x\n",
                  ctxt_pP->module_id,
                  S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc,
                  S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc_len,
                  ue_context_pP->ue_context.rnti);
          }
        } else { // end if plmn_Identity != NULL
          S1AP_NAS_FIRST_REQ(message_p).ue_identity.gummei.mcc = rrc->configuration.mcc[selected_plmn_identity];
          S1AP_NAS_FIRST_REQ(message_p).ue_identity.gummei.mnc = rrc->configuration.mnc[selected_plmn_identity];
          S1AP_NAS_FIRST_REQ(message_p).ue_identity.gummei.mnc_len = rrc->configuration.mnc_digit_length[selected_plmn_identity];
        } // end else (plmn_Identity == NULL)

        S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_code     = BIT_STRING_to_uint8 (&r_mme->mmec);
        S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_group_id = BIT_STRING_to_uint16 (&r_mme->mmegi);
        ue_context_pP->ue_context.ue_gummei.mcc = S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mcc;
        ue_context_pP->ue_context.ue_gummei.mnc = S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc;
        ue_context_pP->ue_context.ue_gummei.mnc_len = S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mnc_len;
        ue_context_pP->ue_context.ue_gummei.mme_code = S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_code;
        ue_context_pP->ue_context.ue_gummei.mme_group_id = S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_group_id;
        LOG_I(S1AP, "[eNB %d] Build S1AP_NAS_FIRST_REQ adding in s_TMSI: GUMMEI mme_code %u mme_group_id %u ue %x\n",
              ctxt_pP->module_id,
              S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_code,
              S1AP_NAS_FIRST_REQ (message_p).ue_identity.gummei.mme_group_id,
              ue_context_pP->ue_context.rnti);
      } // end if MME info present
    } // end "Fill UE identities with available information" sub-part
    itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, message_p);
  }
}


//------------------------------------------------------------------------------
int
rrc_eNB_process_S1AP_DOWNLINK_NAS(
  MessageDef *msg_p,
  const char *msg_name,
  instance_t instance,
  mui_t *rrc_eNB_mui
)
//------------------------------------------------------------------------------
{
  uint16_t ue_initial_id;
  uint32_t eNB_ue_s1ap_id;
  uint32_t length;
  uint8_t *buffer;
  uint8_t srb_id;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  memset(&ctxt, 0, sizeof(protocol_ctxt_t));
  ue_initial_id = S1AP_DOWNLINK_NAS (msg_p).ue_initial_id;
  eNB_ue_s1ap_id = S1AP_DOWNLINK_NAS (msg_p).eNB_ue_s1ap_id;
  ue_context_p = rrc_eNB_get_ue_context_from_s1ap_ids(instance, ue_initial_id, eNB_ue_s1ap_id);
  LOG_I(RRC, "[eNB %ld] Received %s: ue_initial_id %d, eNB_ue_s1ap_id %d\n",
        instance,
        msg_name,
        ue_initial_id,
        eNB_ue_s1ap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    MessageDef *msg_fail_p;
    LOG_W(RRC, "[eNB %ld] In S1AP_DOWNLINK_NAS: unknown UE from S1AP ids (%d, %d)\n", instance, ue_initial_id, eNB_ue_s1ap_id);
    msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_NAS_NON_DELIVERY_IND);
    S1AP_NAS_NON_DELIVERY_IND (msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    S1AP_NAS_NON_DELIVERY_IND (msg_fail_p).nas_pdu.length = S1AP_DOWNLINK_NAS (msg_p).nas_pdu.length;
    S1AP_NAS_NON_DELIVERY_IND (msg_fail_p).nas_pdu.buffer = S1AP_DOWNLINK_NAS (msg_p).nas_pdu.buffer;
    // TODO add failure cause when defined!
    itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    srb_id = ue_context_p->ue_context.Srb2.Srb_info.Srb_id;

    /* Is it the first income from S1AP ? */
    if (ue_context_p->ue_context.eNB_ue_s1ap_id == 0) {
      ue_context_p->ue_context.eNB_ue_s1ap_id = S1AP_DOWNLINK_NAS (msg_p).eNB_ue_s1ap_id;
    }

    /* Create message for PDCP (DLInformationTransfer_t) */
    length = do_DLInformationTransfer (
               instance,
               &buffer,
               rrc_eNB_get_next_transaction_identifier (instance),
               S1AP_DOWNLINK_NAS (msg_p).nas_pdu.length,
               S1AP_DOWNLINK_NAS (msg_p).nas_pdu.buffer);
    LOG_DUMPMSG(RRC,DEBUG_RRC,buffer,length,"[MSG] RRC DL Information Transfer\n");
    /*
     * switch UL or DL NAS message without RRC piggybacked to SRB2 if active.
     */
    /* Transfer data to PDCP */
    rrc_data_req (
      &ctxt,
      srb_id,
      (*rrc_eNB_mui)++,
      SDU_CONFIRM_NO,
      length,
      buffer,
      PDCP_TRANSMISSION_MODE_CONTROL);
    return (0);
  }
}

/*------------------------------------------------------------------------------*/
int rrc_eNB_process_S1AP_INITIAL_CONTEXT_SETUP_REQ(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  uint16_t                        ue_initial_id;
  uint32_t                        eNB_ue_s1ap_id;
  //MessageDef                     *message_gtpv1u_p = NULL;
  gtpv1u_enb_create_tunnel_req_t  create_tunnel_req;
  gtpv1u_enb_create_tunnel_resp_t create_tunnel_resp;
  uint8_t                         inde_list[NB_RB_MAX - 3]= {0};
  int                             ret;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  ue_initial_id  = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).ue_initial_id;
  eNB_ue_s1ap_id = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, ue_initial_id, eNB_ue_s1ap_id);
  LOG_I(RRC, "[eNB %ld] Received %s: ue_initial_id %d, eNB_ue_s1ap_id %d, nb_of_e_rabs %d\n",
        instance, msg_name, ue_initial_id, eNB_ue_s1ap_id, S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).nb_of_e_rabs);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    MessageDef *msg_fail_p = NULL;
    LOG_W(RRC, "[eNB %ld] In S1AP_INITIAL_CONTEXT_SETUP_REQ: unknown UE from S1AP ids (%d, %d)\n", instance, ue_initial_id, eNB_ue_s1ap_id);
    msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_INITIAL_CONTEXT_SETUP_FAIL);
    S1AP_INITIAL_CONTEXT_SETUP_FAIL (msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    // TODO add failure cause when defined!
    itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    ue_context_p->ue_context.eNB_ue_s1ap_id = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).eNB_ue_s1ap_id;
    ue_context_p->ue_context.mme_ue_s1ap_id = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).mme_ue_s1ap_id;
    /* Save e RAB information for later */
    {
      int i;
      memset(&create_tunnel_req, 0, sizeof(create_tunnel_req));
      ue_context_p->ue_context.nb_of_e_rabs = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).nb_of_e_rabs;

      for (i = 0; i < ue_context_p->ue_context.nb_of_e_rabs; i++) {
        ue_context_p->ue_context.e_rab[i].status = E_RAB_STATUS_NEW;
        ue_context_p->ue_context.e_rab[i].param = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).e_rab_param[i];
        create_tunnel_req.eps_bearer_id[i]       = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).e_rab_param[i].e_rab_id;
        create_tunnel_req.sgw_S1u_teid[i]        = S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).e_rab_param[i].gtp_teid;
        memcpy(&create_tunnel_req.sgw_addr[i],
               &S1AP_INITIAL_CONTEXT_SETUP_REQ (msg_p).e_rab_param[i].sgw_addr,
               sizeof(transport_layer_addr_t));
        inde_list[create_tunnel_req.num_tunnels]= i;
        create_tunnel_req.num_tunnels++;
      }

      create_tunnel_req.rnti       = ue_context_p->ue_context.rnti; // warning put zero above
      //      create_tunnel_req.num_tunnels    = i;
      ret = gtpv1u_create_s1u_tunnel(instance, &create_tunnel_req, &create_tunnel_resp, pdcp_data_req);

      if ( ret != 0 ) {
        LOG_E(RRC,"rrc_eNB_process_S1AP_INITIAL_CONTEXT_SETUP_REQ : gtpv1u_create_s1u_tunnel failed,start to release UE %x\n",ue_context_p->ue_context.rnti);
        ue_context_p->ue_context.ue_release_timer_s1 = 1;
        ue_context_p->ue_context.ue_release_timer_thres_s1 = 100;
        ue_context_p->ue_context.ue_release_timer = 0;
        ue_context_p->ue_context.ue_reestablishment_timer = 0;
        ue_context_p->ue_context.ul_failure_timer = 20000; // set ul_failure to 20000 for triggering rrc_eNB_send_S1AP_UE_CONTEXT_RELEASE_REQ
        rrc_eNB_free_UE(ctxt.module_id,ue_context_p);
        ue_context_p->ue_context.ul_failure_timer = 0;
        return (0);
      }

      rrc_eNB_process_GTPV1U_CREATE_TUNNEL_RESP(
        &ctxt,
        &create_tunnel_resp,
        &inde_list[0]);
      ue_context_p->ue_context.setup_e_rabs=ue_context_p->ue_context.nb_of_e_rabs;
    }
    /* TODO parameters yet to process ... */
    {
      //      S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).ue_ambr;
    }
    rrc_eNB_process_security (
      &ctxt,
      ue_context_p,
      &S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).security_capabilities);
    process_eNB_security_key (
      &ctxt,
      ue_context_p,
      S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).security_key);

    {
      uint8_t send_security_mode_command = true;
      rrc_pdcp_config_security(
        &ctxt,
        ue_context_p,
        send_security_mode_command);

      if (send_security_mode_command) {
        rrc_eNB_generate_SecurityModeCommand (
          &ctxt,
          ue_context_p);
        send_security_mode_command = false;
        // apply ciphering after RRC security command mode
        rrc_pdcp_config_security(
          &ctxt,
          ue_context_p,
          send_security_mode_command);
      } else {
        rrc_eNB_generate_UECapabilityEnquiry (&ctxt, ue_context_p);
      }
    }

    ue_context_p->ue_context.nr_security.ciphering_algorithms = S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).nr_security_capabilities.encryption_algorithms;
    ue_context_p->ue_context.nr_security.integrity_algorithms = S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).nr_security_capabilities.integrity_algorithms;
    /* let's initialize sk_counter to 0 */
    ue_context_p->ue_context.nr_security.sk_counter = 0;
    /* let's compute kgNB */
    derive_skgNB(ue_context_p->ue_context.kenb,
                 ue_context_p->ue_context.nr_security.sk_counter,
                 ue_context_p->ue_context.nr_security.kgNB);

    // in case, send the S1SP initial context response if it is not sent with the attach complete message
    if (ue_context_p->ue_context.StatusRrc == RRC_RECONFIGURED) {
      LOG_I(RRC, "Sending rrc_eNB_send_S1AP_INITIAL_CONTEXT_SETUP_RESP, cause %ld\n", ue_context_p->ue_context.reestablishment_cause);
      //if(ue_context_p->ue_context.reestablishment_cause == ReestablishmentCause_spare1){}
      rrc_eNB_send_S1AP_INITIAL_CONTEXT_SETUP_RESP(&ctxt,ue_context_p);
    }
  }
  
  return (0);
}

/*------------------------------------------------------------------------------*/
int rrc_eNB_process_S1AP_UE_CTXT_MODIFICATION_REQ(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  uint32_t eNB_ue_s1ap_id;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  eNB_ue_s1ap_id = S1AP_UE_CTXT_MODIFICATION_REQ (msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, UE_INITIAL_ID_INVALID, eNB_ue_s1ap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    MessageDef *msg_fail_p;
    LOG_W(RRC, "[eNB %ld] In S1AP_UE_CTXT_MODIFICATION_REQ: unknown UE from eNB_ue_s1ap_id (%d)\n", instance, eNB_ue_s1ap_id);
    msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_UE_CTXT_MODIFICATION_FAIL);
    S1AP_UE_CTXT_MODIFICATION_FAIL (msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    // TODO add failure cause when defined!
    itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    /* TODO parameters yet to process ... */
    {
      if (S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).present & S1AP_UE_CONTEXT_MODIFICATION_UE_AMBR) {
        //        S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).ue_ambr;
      }
    }

    if (S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).present & S1AP_UE_CONTEXT_MODIFICATION_UE_SECU_CAP) {
      if (rrc_eNB_process_security (
            &ctxt,
            ue_context_p,
            &S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).security_capabilities)) {
        /* transmit the new security parameters to UE */
        rrc_eNB_generate_SecurityModeCommand (
          &ctxt,
          ue_context_p);
      }
    }

    if (S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).present & S1AP_UE_CONTEXT_MODIFICATION_SECURITY_KEY) {
      process_eNB_security_key (
        &ctxt,
        ue_context_p,
        S1AP_UE_CTXT_MODIFICATION_REQ(msg_p).security_key);
      /* TODO reconfigure lower layers... */
    }

    /* Send the response */
    {
      MessageDef *msg_resp_p;
      msg_resp_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CTXT_MODIFICATION_RESP);
      S1AP_UE_CTXT_MODIFICATION_RESP(msg_resp_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
      itti_send_msg_to_task(TASK_S1AP, instance, msg_resp_p);
    }
    return (0);
  }
}

/*------------------------------------------------------------------------------*/
int rrc_eNB_process_S1AP_UE_CONTEXT_RELEASE_REQ (MessageDef *msg_p, const char *msg_name, instance_t instance) {
  uint32_t eNB_ue_s1ap_id;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  eNB_ue_s1ap_id = S1AP_UE_CONTEXT_RELEASE_REQ(msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, UE_INITIAL_ID_INVALID, eNB_ue_s1ap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    MessageDef *msg_fail_p;
    LOG_W(RRC, "[eNB %ld] In S1AP_UE_CONTEXT_RELEASE_REQ: unknown UE from eNB_ue_s1ap_id (%d)\n",
          instance,
          eNB_ue_s1ap_id);
    msg_fail_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CONTEXT_RELEASE_RESP); /* TODO change message ID. */
    S1AP_UE_CONTEXT_RELEASE_RESP(msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    // TODO add failure cause when defined!
    itti_send_msg_to_task(TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    /* TODO release context. */
    /* Send the response */
    {
      MessageDef *msg_resp_p;
      msg_resp_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CONTEXT_RELEASE_RESP);
      S1AP_UE_CONTEXT_RELEASE_RESP(msg_resp_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
      itti_send_msg_to_task(TASK_S1AP, instance, msg_resp_p);
    }
    return (0);
  }
}

//------------------------------------------------------------------------------
/*
* Send the S1 command UE_CONTEXT_RELEASE_REQUEST to the MME.
*/
void
rrc_eNB_send_S1AP_UE_CONTEXT_RELEASE_REQ(
  const module_id_t enb_mod_idP,
  const rrc_eNB_ue_context_t *const ue_context_pP,
  const s1ap_Cause_t causeP,
  const long cause_valueP)
//------------------------------------------------------------------------------
{
  if (ue_context_pP == NULL) {
    LOG_E(RRC, "[eNB] In S1AP_UE_CONTEXT_RELEASE_REQ: invalid UE\n");
  } else {
    MessageDef *msg_context_release_req_p = NULL;
    msg_context_release_req_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CONTEXT_RELEASE_REQ);
    S1AP_UE_CONTEXT_RELEASE_REQ(msg_context_release_req_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;
    S1AP_UE_CONTEXT_RELEASE_REQ(msg_context_release_req_p).cause          = causeP;
    S1AP_UE_CONTEXT_RELEASE_REQ(msg_context_release_req_p).cause_value    = cause_valueP;
    itti_send_msg_to_task(TASK_S1AP, ENB_MODULE_ID_TO_INSTANCE(enb_mod_idP), msg_context_release_req_p);
  }
}

void rrc_eNB_send_S1AP_UE_CONTEXT_RELEASE_CPLT(
  module_id_t enb_mod_idP,
  uint32_t eNB_ue_s1ap_id
) {
  MessageDef *msg = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CONTEXT_RELEASE_COMPLETE);
  S1AP_UE_CONTEXT_RELEASE_COMPLETE(msg).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
  itti_send_msg_to_task(TASK_S1AP, ENB_MODULE_ID_TO_INSTANCE(enb_mod_idP), msg);
}


//-----------------------------------------------------------------------------
/*
* Process the S1 command UE_CONTEXT_RELEASE_COMMAND, sent by MME.
* The eNB should remove all e-rab, S1 context, and other context of the UE.
*/
int
rrc_eNB_process_S1AP_UE_CONTEXT_RELEASE_COMMAND(
  MessageDef *msg_p,
  const char *msg_name,
  instance_t instance) {
  //-----------------------------------------------------------------------------
  uint32_t eNB_ue_s1ap_id = 0;
  protocol_ctxt_t ctxt;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  struct rrc_ue_s1ap_ids_s *rrc_ue_s1ap_ids = NULL;
  eNB_ue_s1ap_id = S1AP_UE_CONTEXT_RELEASE_COMMAND(msg_p).eNB_ue_s1ap_id;
  ue_context_p = rrc_eNB_get_ue_context_from_s1ap_ids(instance, UE_INITIAL_ID_INVALID, eNB_ue_s1ap_id);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index */
    MessageDef *msg_complete_p = NULL;
    LOG_W(RRC, "[eNB %ld] In S1AP_UE_CONTEXT_RELEASE_COMMAND: unknown UE from eNB_ue_s1ap_id (%d)\n",
          instance,
          eNB_ue_s1ap_id);
    msg_complete_p = itti_alloc_new_message(TASK_RRC_ENB, 0, S1AP_UE_CONTEXT_RELEASE_COMPLETE);
    S1AP_UE_CONTEXT_RELEASE_COMPLETE(msg_complete_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    itti_send_msg_to_task(TASK_S1AP, instance, msg_complete_p);
    rrc_ue_s1ap_ids = rrc_eNB_S1AP_get_ue_ids(RC.rrc[instance], UE_INITIAL_ID_INVALID, eNB_ue_s1ap_id);

    if (rrc_ue_s1ap_ids != NULL) {
      rrc_eNB_S1AP_remove_ue_ids(RC.rrc[instance], rrc_ue_s1ap_ids);
    }

    return -1;
  } else {
    ue_context_p->ue_context.ue_release_timer_s1 = 0;
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    rrc_eNB_generate_RRCConnectionRelease(&ctxt, ue_context_p);
    return 0;
  }
}

int rrc_eNB_process_S1AP_E_RAB_SETUP_REQ(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  uint16_t                        ue_initial_id;
  uint32_t                        eNB_ue_s1ap_id;
  gtpv1u_enb_create_tunnel_req_t  create_tunnel_req;
  gtpv1u_enb_create_tunnel_resp_t create_tunnel_resp;
  uint8_t                         inde_list[NB_RB_MAX - 3]= {0};
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  uint8_t                      e_rab_done;
  int                          ret = 0;
  ue_initial_id  = S1AP_E_RAB_SETUP_REQ (msg_p).ue_initial_id;
  eNB_ue_s1ap_id = S1AP_E_RAB_SETUP_REQ (msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, ue_initial_id, eNB_ue_s1ap_id);
  LOG_I(RRC, "[eNB %ld] Received %s: ue_initial_id %d, eNB_ue_s1ap_id %d, nb_of_e_rabs %d\n",
        instance, msg_name, ue_initial_id, eNB_ue_s1ap_id, S1AP_E_RAB_SETUP_REQ (msg_p).nb_e_rabs_tosetup);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    MessageDef *msg_fail_p = NULL;
    LOG_W(RRC, "[eNB %ld] In S1AP_E_RAB_SETUP_REQ: unknown UE from S1AP ids (%d, %d)\n", instance, ue_initial_id, eNB_ue_s1ap_id);
    msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_SETUP_REQUEST_FAIL);
    S1AP_E_RAB_SETUP_REQ  (msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    // TODO add failure cause when defined!
    itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    ue_context_p->ue_context.eNB_ue_s1ap_id = S1AP_E_RAB_SETUP_REQ  (msg_p).eNB_ue_s1ap_id;
    /* Save e RAB information for later */
    {
      int i;
      memset(&create_tunnel_req, 0, sizeof(create_tunnel_req));
      uint8_t nb_e_rabs_tosetup = S1AP_E_RAB_SETUP_REQ  (msg_p).nb_e_rabs_tosetup;
      e_rab_done = 0;

      // keep the previous bearer
      // the index for the rec
      for (i = 0;
           // i < nb_e_rabs_tosetup;
           i < NB_RB_MAX - 3;  // loop all e-rabs in e_rab[]
           i++) {
        //if (ue_context_p->ue_context.e_rab[i+ue_context_p->ue_context.setup_e_rabs].status == E_RAB_STATUS_DONE)
        //  LOG_W(RRC,"E-RAB already configured, reconfiguring\n");
        // check e-rab status, if e rab status is greater than E_RAB_STATUS_DONE, don't not config this one
        if(ue_context_p->ue_context.e_rab[i].status >= E_RAB_STATUS_DONE)
          continue;

        //ue_context_p->ue_context.e_rab[i+ue_context_p->ue_context.setup_e_rabs].status = E_RAB_STATUS_NEW;
        //ue_context_p->ue_context.e_rab[i+ue_context_p->ue_context.setup_e_rabs].param = S1AP_E_RAB_SETUP_REQ  (msg_p).e_rab_setup_params[i];
        ue_context_p->ue_context.e_rab[i].status = E_RAB_STATUS_NEW;
        ue_context_p->ue_context.e_rab[i].param = S1AP_E_RAB_SETUP_REQ  (msg_p).e_rab_setup_params[e_rab_done];
        create_tunnel_req.eps_bearer_id[e_rab_done]       = S1AP_E_RAB_SETUP_REQ  (msg_p).e_rab_setup_params[e_rab_done].e_rab_id;
        create_tunnel_req.sgw_S1u_teid[e_rab_done]        = S1AP_E_RAB_SETUP_REQ  (msg_p).e_rab_setup_params[e_rab_done].gtp_teid;
        memcpy(&create_tunnel_req.sgw_addr[e_rab_done],
               & S1AP_E_RAB_SETUP_REQ (msg_p).e_rab_setup_params[e_rab_done].sgw_addr,
               sizeof(transport_layer_addr_t));
        LOG_I(RRC,"E_RAB setup REQ: local index %d teid %u, eps id %d \n",
              i,
              create_tunnel_req.sgw_S1u_teid[e_rab_done],
              create_tunnel_req.eps_bearer_id[i] );
        inde_list[e_rab_done] = i;
        e_rab_done++;

        if(e_rab_done >= nb_e_rabs_tosetup) {
          break;
        }
      }

      ue_context_p->ue_context.nb_of_e_rabs=nb_e_rabs_tosetup;
      create_tunnel_req.rnti       = ue_context_p->ue_context.rnti; // warning put zero above
      create_tunnel_req.num_tunnels    = e_rab_done;
      // NN: not sure if we should create a new tunnel: need to check teid, etc.
      ret = gtpv1u_create_s1u_tunnel(instance, &create_tunnel_req, &create_tunnel_resp, pdcp_data_req);

      if ( ret != 0 ) {
        LOG_E(RRC,"rrc_eNB_process_S1AP_E_RAB_SETUP_REQ : gtpv1u_create_s1u_tunnel failed,start to release UE %x\n",ue_context_p->ue_context.rnti);
        ue_context_p->ue_context.ue_release_timer_s1 = 1;
        ue_context_p->ue_context.ue_release_timer_thres_s1 = 100;
        ue_context_p->ue_context.ue_release_timer = 0;
        ue_context_p->ue_context.ue_reestablishment_timer = 0;
        ue_context_p->ue_context.ul_failure_timer = 20000; // set ul_failure to 20000 for triggering rrc_eNB_send_S1AP_UE_CONTEXT_RELEASE_REQ
        rrc_eNB_free_UE(ctxt.module_id,ue_context_p);
        ue_context_p->ue_context.ul_failure_timer = 0;
        return (0);
      }

      rrc_eNB_process_GTPV1U_CREATE_TUNNEL_RESP(
        &ctxt,
        &create_tunnel_resp,
        &inde_list[0]);
      ue_context_p->ue_context.setup_e_rabs+=nb_e_rabs_tosetup;
    }
    /* TODO parameters yet to process ... */
    {
      //      S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).ue_ambr;
    }
    rrc_eNB_generate_dedicatedRRCConnectionReconfiguration(&ctxt, ue_context_p, 0);
    return (0);
  }
}

/*NN: careful about the typcast of xid (long -> uint8_t*/
int rrc_eNB_send_S1AP_E_RAB_SETUP_RESP(const protocol_ctxt_t *const ctxt_pP,
                                       rrc_eNB_ue_context_t          *const ue_context_pP,
                                       uint8_t xid ) {
  MessageDef      *msg_p         = NULL;
  int e_rab;
  int e_rabs_done = 0;
  int e_rabs_failed = 0;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_SETUP_RESP);
  S1AP_E_RAB_SETUP_RESP (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;

  for (e_rab = 0; e_rab <  ue_context_pP->ue_context.setup_e_rabs ; e_rab++) {
    /* only respond to the corresponding transaction */
    //if (((xid+1)%4) == ue_context_pP->ue_context.e_rab[e_rab].xid) {
    if (xid == ue_context_pP->ue_context.e_rab[e_rab].xid) {
      if (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_DONE) {
        S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
        // TODO add other information from S1-U when it will be integrated
        S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].gtp_teid = ue_context_pP->ue_context.enb_gtp_teid[e_rab];
        S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].eNB_addr = ue_context_pP->ue_context.enb_gtp_addrs[e_rab];
        //S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rab].eNB_addr.length += 4;
        ue_context_pP->ue_context.e_rab[e_rab].status = E_RAB_STATUS_ESTABLISHED;
        LOG_I (RRC,"enb_gtp_addr (msg index %d, e_rab index %d, status %d, xid %d): nb_of_e_rabs %d,  e_rab_id %d, teid: %u, addr: %d.%d.%d.%d \n ",
               e_rabs_done,  e_rab, ue_context_pP->ue_context.e_rab[e_rab].status, xid,
               ue_context_pP->ue_context.nb_of_e_rabs,
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].e_rab_id,
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].gtp_teid,
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].eNB_addr.buffer[0],
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].eNB_addr.buffer[1],
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].eNB_addr.buffer[2],
               S1AP_E_RAB_SETUP_RESP (msg_p).e_rabs[e_rabs_done].eNB_addr.buffer[3]);
        e_rabs_done++;
      } else if ((ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_NEW)  ||
                 (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_ESTABLISHED)) {
        LOG_D (RRC,"E-RAB is NEW or already ESTABLISHED\n");
      } else { /* to be improved */
        ue_context_pP->ue_context.e_rab[e_rab].status = E_RAB_STATUS_FAILED;
        S1AP_E_RAB_SETUP_RESP  (msg_p).e_rabs_failed[e_rabs_failed].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
        e_rabs_failed++;
        // TODO add cause when it will be integrated
      }

      S1AP_E_RAB_SETUP_RESP (msg_p).nb_of_e_rabs = e_rabs_done;
      S1AP_E_RAB_SETUP_RESP (msg_p).nb_of_e_rabs_failed = e_rabs_failed;
      // NN: add conditions for e_rabs_failed
    } else {
      /*debug info for the xid */
      LOG_D(RRC,"xid does not corresponds  (context e_rab index %d, status %d, xid %d/%d) \n ",
            e_rab, ue_context_pP->ue_context.e_rab[e_rab].status, xid, ue_context_pP->ue_context.e_rab[e_rab].xid);
    }
  }

  if ((e_rabs_done > 0) ) {
    LOG_I(RRC,"S1AP_E_RAB_SETUP_RESP: sending the message: nb_of_erabs %d, total e_rabs %d, index %d\n",
          ue_context_pP->ue_context.nb_of_e_rabs, ue_context_pP->ue_context.setup_e_rabs, e_rab);
    itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
  }

  for(int i = 0; i < NB_RB_MAX; i++) {
    ue_context_pP->ue_context.e_rab[i].xid = -1;
  }

  return 0;
}

int rrc_eNB_process_S1AP_E_RAB_MODIFY_REQ(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  int                             i;
  uint16_t                        ue_initial_id;
  uint32_t                        eNB_ue_s1ap_id;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  ue_initial_id  = S1AP_E_RAB_MODIFY_REQ (msg_p).ue_initial_id;
  eNB_ue_s1ap_id = S1AP_E_RAB_MODIFY_REQ (msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, ue_initial_id, eNB_ue_s1ap_id);
  LOG_D(RRC, "[eNB %ld] Received %s: ue_initial_id %d, eNB_ue_s1ap_id %d, nb_of_e_rabs %d\n",
        instance, msg_name, ue_initial_id, eNB_ue_s1ap_id, S1AP_E_RAB_MODIFY_REQ (msg_p).nb_e_rabs_tomodify);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    LOG_W(RRC, "[eNB %ld] In S1AP_E_RAB_MODIFY_REQ: unknown UE from S1AP ids (%d, %d)\n", instance, ue_initial_id, eNB_ue_s1ap_id);
    int nb_of_e_rabs_failed = 0;
    MessageDef *msg_fail_p = NULL;
    msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_MODIFY_RESP);
    S1AP_E_RAB_MODIFY_RESP (msg_fail_p).eNB_ue_s1ap_id = S1AP_E_RAB_MODIFY_REQ (msg_p).eNB_ue_s1ap_id;
    S1AP_E_RAB_MODIFY_RESP (msg_fail_p).nb_of_e_rabs = 0;

    for (nb_of_e_rabs_failed = 0; nb_of_e_rabs_failed < S1AP_E_RAB_MODIFY_REQ (msg_p).nb_e_rabs_tomodify; nb_of_e_rabs_failed++) {
      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs_failed[nb_of_e_rabs_failed].e_rab_id =
        S1AP_E_RAB_MODIFY_REQ (msg_p).e_rab_modify_params[nb_of_e_rabs_failed].e_rab_id;
      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs_failed[nb_of_e_rabs_failed].cause = S1AP_CAUSE_RADIO_NETWORK;
      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs_failed[nb_of_e_rabs_failed].cause_value = 31;//S1ap_CauseRadioNetwork_multiple_E_RAB_ID_instances;
    }

    S1AP_E_RAB_MODIFY_RESP (msg_fail_p).nb_of_e_rabs_failed = nb_of_e_rabs_failed;
    itti_send_msg_to_task(TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    ue_context_p->ue_context.eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    /* Save e RAB information for later */
    {
      int j;
      bool is_treated[S1AP_MAX_E_RAB] = {false};
      uint8_t nb_of_failed_e_rabs = 0;

      // keep the previous bearer
      // the index for the rec
      for (i = 0; i < S1AP_E_RAB_MODIFY_REQ (msg_p).nb_e_rabs_tomodify; i++) {
        if (is_treated[i] == true) {
          // already treated
          continue;
        }

        for (j = i+1; j < S1AP_E_RAB_MODIFY_REQ (msg_p).nb_e_rabs_tomodify; j++) {
          if (is_treated[j] == false &&
              S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[j].e_rab_id == S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id) {
            // handle multiple E-RAB ID
            ue_context_p->ue_context.modify_e_rab[j].status = E_RAB_STATUS_NEW;
            ue_context_p->ue_context.modify_e_rab[j].param.e_rab_id = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[j].e_rab_id;
            ue_context_p->ue_context.modify_e_rab[j].cause = S1AP_CAUSE_RADIO_NETWORK;
            ue_context_p->ue_context.modify_e_rab[j].cause_value = 31;//S1ap_CauseRadioNetwork_multiple_E_RAB_ID_instances;
            nb_of_failed_e_rabs++;
            is_treated[i] = true;
            is_treated[j] = true;
          }
        }

        if (is_treated[i] == true) {
          // handle multiple E-RAB ID
          ue_context_p->ue_context.modify_e_rab[i].status = E_RAB_STATUS_NEW;
          ue_context_p->ue_context.modify_e_rab[i].param.e_rab_id = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id;
          ue_context_p->ue_context.modify_e_rab[i].cause = S1AP_CAUSE_RADIO_NETWORK;
          ue_context_p->ue_context.modify_e_rab[i].cause_value = 31;//S1ap_CauseRadioNetwork_multiple_E_RAB_ID_instances;
          nb_of_failed_e_rabs++;
          continue;
        }

        if (S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].nas_pdu.length == 0) {
          // nas_pdu.length == 0
          ue_context_p->ue_context.modify_e_rab[i].status = E_RAB_STATUS_NEW;
          ue_context_p->ue_context.modify_e_rab[i].param.e_rab_id = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id;
          ue_context_p->ue_context.modify_e_rab[i].cause = S1AP_CAUSE_NAS;
          ue_context_p->ue_context.modify_e_rab[i].cause_value = 3;//S1ap_CauseNas_unspecified;
          nb_of_failed_e_rabs++;
          is_treated[i] = true;
          continue;
        }

        for (j = 0; j < NB_RB_MAX-3; j++) {
          if (ue_context_p->ue_context.e_rab[j].param.e_rab_id == S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id) {
            if(ue_context_p->ue_context.e_rab[j].status == E_RAB_STATUS_TORELEASE || ue_context_p->ue_context.e_rab[j].status == E_RAB_STATUS_DONE) {
              break;
            }

            ue_context_p->ue_context.modify_e_rab[i].status = E_RAB_STATUS_NEW;
            ue_context_p->ue_context.modify_e_rab[i].cause = S1AP_CAUSE_NOTHING;
            ue_context_p->ue_context.modify_e_rab[i].param.e_rab_id = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id;
            ue_context_p->ue_context.modify_e_rab[i].param.qos =  S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].qos;
            ue_context_p->ue_context.modify_e_rab[i].param.nas_pdu.length = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].nas_pdu.length;
            ue_context_p->ue_context.modify_e_rab[i].param.nas_pdu.buffer = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].nas_pdu.buffer;
            ue_context_p->ue_context.modify_e_rab[i].param.sgw_addr = ue_context_p->ue_context.e_rab[j].param.sgw_addr;
            ue_context_p->ue_context.modify_e_rab[i].param.gtp_teid = ue_context_p->ue_context.e_rab[j].param.gtp_teid;
            is_treated[i] = true;
            break;
          }
        }

        if (is_treated[i] == false) {
          // handle Unknown E-RAB ID
          ue_context_p->ue_context.modify_e_rab[i].status = E_RAB_STATUS_NEW;
          ue_context_p->ue_context.modify_e_rab[i].param.e_rab_id = S1AP_E_RAB_MODIFY_REQ(msg_p).e_rab_modify_params[i].e_rab_id;
          ue_context_p->ue_context.modify_e_rab[i].cause = S1AP_CAUSE_RADIO_NETWORK;
          ue_context_p->ue_context.modify_e_rab[i].cause_value = 30;//S1ap_CauseRadioNetwork_unknown_E_RAB_ID;
          nb_of_failed_e_rabs++;
          is_treated[i] = true;
        }
      }

      ue_context_p->ue_context.nb_of_modify_e_rabs = S1AP_E_RAB_MODIFY_REQ  (msg_p).nb_e_rabs_tomodify;
      ue_context_p->ue_context.nb_of_failed_e_rabs = nb_of_failed_e_rabs;
    }
    /* TODO parameters yet to process ... */
    {
      //      S1AP_INITIAL_CONTEXT_SETUP_REQ(msg_p).ue_ambr;
    }

    if (ue_context_p->ue_context.nb_of_failed_e_rabs < ue_context_p->ue_context.nb_of_modify_e_rabs) {
      if (0 == rrc_eNB_modify_dedicatedRRCConnectionReconfiguration(&ctxt, ue_context_p, 0)) {
        return (0);
      }
    }

    {
      int nb_of_e_rabs_failed = 0;
      MessageDef *msg_fail_p = NULL;
      msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_MODIFY_RESP);
      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).eNB_ue_s1ap_id = S1AP_E_RAB_MODIFY_REQ (msg_p).eNB_ue_s1ap_id;
      //      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs[S1AP_MAX_E_RAB];
      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).nb_of_e_rabs = 0;

      for(nb_of_e_rabs_failed = 0; nb_of_e_rabs_failed < ue_context_p->ue_context.nb_of_failed_e_rabs; nb_of_e_rabs_failed++) {
        S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs_failed[nb_of_e_rabs_failed].e_rab_id =
          ue_context_p->ue_context.modify_e_rab[nb_of_e_rabs_failed].param.e_rab_id;
        S1AP_E_RAB_MODIFY_RESP (msg_fail_p).e_rabs_failed[nb_of_e_rabs_failed].cause = ue_context_p->ue_context.modify_e_rab[nb_of_e_rabs_failed].cause;
      }

      S1AP_E_RAB_MODIFY_RESP (msg_fail_p).nb_of_e_rabs_failed = nb_of_e_rabs_failed;
      itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
      ue_context_p->ue_context.nb_of_modify_e_rabs = 0;
      ue_context_p->ue_context.nb_of_failed_e_rabs = 0;
      memset(ue_context_p->ue_context.modify_e_rab, 0, sizeof(ue_context_p->ue_context.modify_e_rab));
      return (0);
    }
  }  // end of ue_context_p != NULL
}

/*NN: careful about the typcast of xid (long -> uint8_t*/
int rrc_eNB_send_S1AP_E_RAB_MODIFY_RESP(const protocol_ctxt_t *const ctxt_pP,
                                        rrc_eNB_ue_context_t          *const ue_context_pP,
                                        uint8_t xid ) {
  MessageDef      *msg_p         = NULL;
  int i;
  int e_rab;
  int e_rabs_done = 0;
  int e_rabs_failed = 0;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_MODIFY_RESP);
  S1AP_E_RAB_MODIFY_RESP (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;

  for (e_rab = 0; e_rab < ue_context_pP->ue_context.nb_of_modify_e_rabs; e_rab++) {
    /* only respond to the corresponding transaction */
    if (xid == ue_context_pP->ue_context.modify_e_rab[e_rab].xid) {
      if (ue_context_pP->ue_context.modify_e_rab[e_rab].status == E_RAB_STATUS_DONE) {
        for (i = 0; i < ue_context_pP->ue_context.setup_e_rabs; i++) {
          if (ue_context_pP->ue_context.modify_e_rab[e_rab].param.e_rab_id == ue_context_pP->ue_context.e_rab[i].param.e_rab_id) {
            // Update ue_context_pP->ue_context.e_rab
            ue_context_pP->ue_context.e_rab[i].status = E_RAB_STATUS_ESTABLISHED;
            ue_context_pP->ue_context.e_rab[i].param.qos = ue_context_pP->ue_context.modify_e_rab[e_rab].param.qos;
            ue_context_pP->ue_context.e_rab[i].cause = S1AP_CAUSE_NOTHING;
            break;
          }
        }

        if (i < ue_context_pP->ue_context.setup_e_rabs) {
          S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs[e_rabs_done].e_rab_id = ue_context_pP->ue_context.modify_e_rab[e_rab].param.e_rab_id;
          // TODO add other information from S1-U when it will be integrated
          LOG_D (RRC,"enb_gtp_addr (msg index %d, e_rab index %d, status %d, xid %d): nb_of_modify_e_rabs %d,  e_rab_id %d \n ",
                 e_rabs_done,  e_rab, ue_context_pP->ue_context.modify_e_rab[e_rab].status, xid,
                 ue_context_pP->ue_context.nb_of_modify_e_rabs,
                 S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs[e_rabs_done].e_rab_id);
          e_rabs_done++;
        } else {
          // unexpected
          S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs_failed[e_rabs_failed].e_rab_id = ue_context_pP->ue_context.modify_e_rab[e_rab].param.e_rab_id;
          S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs_failed[e_rabs_failed].cause = S1AP_CAUSE_RADIO_NETWORK;
          S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs_failed[e_rabs_failed].cause_value = 30;//S1ap_CauseRadioNetwork_unknown_E_RAB_ID;
          e_rabs_failed++;
        }
      } else if ((ue_context_pP->ue_context.modify_e_rab[e_rab].status == E_RAB_STATUS_NEW) ||
                 (ue_context_pP->ue_context.modify_e_rab[e_rab].status == E_RAB_STATUS_ESTABLISHED)) {
        LOG_D (RRC,"E-RAB is NEW or already ESTABLISHED\n");
      } else {  /* status == E_RAB_STATUS_FAILED; */
        S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs_failed[e_rabs_failed].e_rab_id = ue_context_pP->ue_context.modify_e_rab[e_rab].param.e_rab_id;
        // add failure cause when defined
        S1AP_E_RAB_MODIFY_RESP (msg_p).e_rabs_failed[e_rabs_failed].cause = ue_context_pP->ue_context.modify_e_rab[e_rab].cause;
        e_rabs_failed++;
      }
    } else {
      /*debug info for the xid */
      LOG_D(RRC,"xid does not corresponds  (context e_rab index %d, status %d, xid %d/%d) \n ",
            e_rab, ue_context_pP->ue_context.modify_e_rab[e_rab].status, xid, ue_context_pP->ue_context.modify_e_rab[e_rab].xid);
    }
  }

  S1AP_E_RAB_MODIFY_RESP (msg_p).nb_of_e_rabs = e_rabs_done;
  S1AP_E_RAB_MODIFY_RESP (msg_p).nb_of_e_rabs_failed = e_rabs_failed;

  // NN: add conditions for e_rabs_failed
  if (e_rabs_done > 0 || e_rabs_failed > 0) {
    LOG_D(RRC,"S1AP_E_RAB_MODIFY_RESP: sending the message: nb_of_modify_e_rabs %d, total e_rabs %d, index %d\n",
          ue_context_pP->ue_context.nb_of_modify_e_rabs, ue_context_pP->ue_context.setup_e_rabs, e_rab);
    itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
  } else {
    itti_free (ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  }

  return 0;
}
int rrc_eNB_process_S1AP_E_RAB_RELEASE_COMMAND(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  uint32_t                        eNB_ue_s1ap_id;
  struct rrc_eNB_ue_context_s    *ue_context_p = NULL;
  protocol_ctxt_t                 ctxt;
  e_rab_release_t e_rab_release_params[S1AP_MAX_E_RAB];
  uint8_t nb_e_rabs_torelease;
  int erab;
  int i;
  uint8_t b_existed,is_existed;
  uint8_t xid;
  uint8_t e_rab_release_drb;
  e_rab_release_drb = 0;
  memcpy(&e_rab_release_params[0], &(S1AP_E_RAB_RELEASE_COMMAND (msg_p).e_rab_release_params[0]), sizeof(e_rab_release_t)*S1AP_MAX_E_RAB);
  eNB_ue_s1ap_id = S1AP_E_RAB_RELEASE_COMMAND (msg_p).eNB_ue_s1ap_id;
  nb_e_rabs_torelease = S1AP_E_RAB_RELEASE_COMMAND (msg_p).nb_e_rabs_torelease;
  if (nb_e_rabs_torelease > S1AP_MAX_E_RAB) {
    return -1;
  }
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, UE_INITIAL_ID_INVALID, eNB_ue_s1ap_id);

  if(ue_context_p != NULL) {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    xid = rrc_eNB_get_next_transaction_identifier(ctxt.module_id);
    LOG_D(RRC,"S1AP-E-RAB Release Command: MME_UE_S1AP_ID %d  ENB_UE_S1AP_ID %d release_e_rabs %d \n",
          S1AP_E_RAB_RELEASE_COMMAND (msg_p).mme_ue_s1ap_id, eNB_ue_s1ap_id,nb_e_rabs_torelease);

    for(erab = 0; erab < nb_e_rabs_torelease; erab++) {
      b_existed = 0;
      is_existed = 0;

      for ( i = erab-1;  i>= 0; i--) {
        if (e_rab_release_params[erab].e_rab_id == e_rab_release_params[i].e_rab_id) {
          is_existed = 1;
          break;
        }
      }

      if(is_existed == 1) {
        //e_rab_id is existed
        continue;
      }

      for ( i = 0;  i < NB_RB_MAX; i++) {
        if (e_rab_release_params[erab].e_rab_id == ue_context_p->ue_context.e_rab[i].param.e_rab_id) {
          b_existed = 1;
          break;
        }
      }

      if(b_existed == 0) {
        //no e_rab_id
        ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].e_rab_id = e_rab_release_params[erab].e_rab_id;
        ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].cause = S1AP_CAUSE_RADIO_NETWORK;
        ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].cause_value = 30;
        ue_context_p->ue_context.nb_release_of_e_rabs++;
      } else {
        if(ue_context_p->ue_context.e_rab[i].status == E_RAB_STATUS_FAILED) {
          ue_context_p->ue_context.e_rab[i].xid = xid;
          continue;
        } else if(ue_context_p->ue_context.e_rab[i].status == E_RAB_STATUS_ESTABLISHED) {
          ue_context_p->ue_context.e_rab[i].status = E_RAB_STATUS_TORELEASE;
          ue_context_p->ue_context.e_rab[i].xid = xid;
          e_rab_release_drb++;
        } else {
          //e_rab_id status NG
          ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].e_rab_id = e_rab_release_params[erab].e_rab_id;
          ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].cause = S1AP_CAUSE_RADIO_NETWORK;
          ue_context_p->ue_context.e_rabs_release_failed[ue_context_p->ue_context.nb_release_of_e_rabs].cause_value = 0;
          ue_context_p->ue_context.nb_release_of_e_rabs++;
        }
      }
    }

    if(e_rab_release_drb > 0) {
      //RRCConnectionReconfiguration To UE
      rrc_eNB_generate_dedicatedRRCConnectionReconfiguration_release(&ctxt, ue_context_p, xid, S1AP_E_RAB_RELEASE_COMMAND (msg_p).nas_pdu.length, S1AP_E_RAB_RELEASE_COMMAND (msg_p).nas_pdu.buffer);
    } else {
      //gtp tunnel delete
      gtpv1u_enb_delete_tunnel_req_t  delete_tunnels={0};
      delete_tunnels.rnti = ue_context_p->ue_context.rnti;
      delete_tunnels.from_gnb = 0;

      for(i = 0; i < NB_RB_MAX; i++) {
        if(xid == ue_context_p->ue_context.e_rab[i].xid) {
          delete_tunnels.eps_bearer_id[delete_tunnels.num_erab++] = ue_context_p->ue_context.enb_gtp_ebi[i];
          ue_context_p->ue_context.enb_gtp_teid[i] = 0;
          memset(&ue_context_p->ue_context.enb_gtp_addrs[i], 0, sizeof(ue_context_p->ue_context.enb_gtp_addrs[i]));
          ue_context_p->ue_context.enb_gtp_ebi[i]  = 0;
        }
      }
      gtpv1u_delete_s1u_tunnel(instance,&delete_tunnels);
      //S1AP_E_RAB_RELEASE_RESPONSE
      rrc_eNB_send_S1AP_E_RAB_RELEASE_RESPONSE(&ctxt, ue_context_p, xid);
    }
  } else {
    LOG_E(RRC,"S1AP-E-RAB Release Command: MME_UE_S1AP_ID %d  ENB_UE_S1AP_ID %d  Error ue_context_p NULL \n",
          S1AP_E_RAB_RELEASE_COMMAND (msg_p).mme_ue_s1ap_id, S1AP_E_RAB_RELEASE_COMMAND (msg_p).eNB_ue_s1ap_id);
    return -1;
  }

  return 0;
}


int rrc_eNB_send_S1AP_E_RAB_RELEASE_RESPONSE(const protocol_ctxt_t *const ctxt_pP, rrc_eNB_ue_context_t *const ue_context_pP, uint8_t xid) {
  int e_rabs_released = 0;
  MessageDef   *msg_p;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_RELEASE_RESPONSE);
  S1AP_E_RAB_RELEASE_RESPONSE (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;

  for (int i = 0;  i < NB_RB_MAX; i++) {
    if (xid == ue_context_pP->ue_context.e_rab[i].xid) {
      S1AP_E_RAB_RELEASE_RESPONSE (msg_p).e_rab_release[e_rabs_released].e_rab_id = ue_context_pP->ue_context.e_rab[i].param.e_rab_id;
      e_rabs_released++;
      //clear
      memset(&ue_context_pP->ue_context.e_rab[i],0,sizeof(e_rab_param_t));
    }
  }

  S1AP_E_RAB_RELEASE_RESPONSE (msg_p).nb_of_e_rabs_released = e_rabs_released;
  S1AP_E_RAB_RELEASE_RESPONSE (msg_p).nb_of_e_rabs_failed = ue_context_pP->ue_context.nb_release_of_e_rabs;
  memcpy(&(S1AP_E_RAB_RELEASE_RESPONSE (msg_p).e_rabs_failed[0]),&ue_context_pP->ue_context.e_rabs_release_failed[0],sizeof(e_rab_failed_t)*ue_context_pP->ue_context.nb_release_of_e_rabs);
  ue_context_pP->ue_context.setup_e_rabs -= e_rabs_released;
  LOG_I(RRC,"S1AP-E-RAB RELEASE RESPONSE: ENB_UE_S1AP_ID %d release_e_rabs %d setup_e_rabs %d \n",
        S1AP_E_RAB_RELEASE_RESPONSE (msg_p).eNB_ue_s1ap_id,
        e_rabs_released, ue_context_pP->ue_context.setup_e_rabs);
  itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);

  //clear xid
  for(int i = 0; i < NB_RB_MAX; i++) {
    ue_context_pP->ue_context.e_rab[i].xid = -1;
  }

  //clear release e_rabs
  ue_context_pP->ue_context.nb_release_of_e_rabs = 0;
  memset(&ue_context_pP->ue_context.e_rabs_release_failed[0],0,sizeof(e_rab_failed_t)*S1AP_MAX_E_RAB);
  return 0;
}

/*------------------------------------------------------------------------------*/
int rrc_eNB_process_PAGING_IND(MessageDef *msg_p, const char *msg_name, instance_t instance) {
  const unsigned int Ttab[4] = {32,64,128,256};
  uint8_t Tc,Tue;  /* DRX cycle of UE */
  uint32_t pcch_nB;  /* 4T, 2T, T, T/2, T/4, T/8, T/16, T/32 */
  uint32_t N;  /* N: min(T,nB). total count of PF in one DRX cycle */
  uint32_t Ns = 0;  /* Ns: max(1,nB/T) */
  uint8_t i_s;  /* i_s = floor(UE_ID/N) mod Ns */
  uint32_t T;  /* DRX cycle */

  for (uint16_t tai_size = 0; tai_size < S1AP_PAGING_IND(msg_p).tai_size; tai_size++) {
    LOG_D(RRC,"[eNB %ld] In S1AP_PAGING_IND: MCC %d, MNC %d, TAC %d\n", instance, S1AP_PAGING_IND(msg_p).plmn_identity[tai_size].mcc,
          S1AP_PAGING_IND(msg_p).plmn_identity[tai_size].mnc, S1AP_PAGING_IND(msg_p).tac[tai_size]);

    for (uint8_t j = 0; j < RC.rrc[instance]->configuration.num_plmn; j++) {
      if (RC.rrc[instance]->configuration.mcc[j] == S1AP_PAGING_IND(msg_p).plmn_identity[tai_size].mcc
          && RC.rrc[instance]->configuration.mnc[j] == S1AP_PAGING_IND(msg_p).plmn_identity[tai_size].mnc
          && RC.rrc[instance]->configuration.tac == S1AP_PAGING_IND(msg_p).tac[tai_size]) {
        for (uint8_t CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
          frame_type_t frame_type = RC.eNB[instance][CC_id]->frame_parms.frame_type;
          /* get nB from configuration */
          /* get default DRX cycle from configuration */
          Tc = (uint8_t)RC.rrc[instance]->configuration.radioresourceconfig[CC_id].pcch_defaultPagingCycle;

          if (Tc < LTE_PCCH_Config__defaultPagingCycle_rf32 || Tc > LTE_PCCH_Config__defaultPagingCycle_rf256) {
            continue;
          }

          Tue = (uint8_t)S1AP_PAGING_IND(msg_p).paging_drx;
          /* set T = min(Tc,Tue) */
          T = Tc < Tue ? Ttab[Tc] : Ttab[Tue];
          /* set pcch_nB = PCCH-Config->nB */
          pcch_nB = (uint32_t)RC.rrc[instance]->configuration.radioresourceconfig[CC_id].pcch_nB;

          switch (pcch_nB) {
            case LTE_PCCH_Config__nB_fourT:
              Ns = 4;
              break;

            case LTE_PCCH_Config__nB_twoT:
              Ns = 2;
              break;

            default:
              Ns = 1;
              break;
          }

          /* set N = min(T,nB) */
          if (pcch_nB > LTE_PCCH_Config__nB_oneT) {
            switch (pcch_nB) {
              case LTE_PCCH_Config__nB_halfT:
                N = T/2;
                break;

              case LTE_PCCH_Config__nB_quarterT:
                N = T/4;
                break;

              case LTE_PCCH_Config__nB_oneEighthT:
                N = T/8;
                break;

              case LTE_PCCH_Config__nB_oneSixteenthT:
                N = T/16;
                break;

              case LTE_PCCH_Config__nB_oneThirtySecondT:
                N = T/32;
                break;

              default:
                /* pcch_nB error */
                LOG_E(RRC, "[eNB %ld] In S1AP_PAGING_IND:  pcch_nB error (pcch_nB %d) \n",
                      instance, pcch_nB);
                return (-1);
            }
          } else {
            N = T;
          }

          /* insert data to UE_PF_PO or update data in UE_PF_PO */
          pthread_mutex_lock(&ue_pf_po_mutex);
          uint8_t i = 0;

          for (i = 0; i < MAX_MOBILES_PER_ENB; i++) {
            if ((UE_PF_PO[CC_id][i].enable_flag == true && UE_PF_PO[CC_id][i].ue_index_value == (uint16_t)(S1AP_PAGING_IND(msg_p).ue_index_value))
                || (UE_PF_PO[CC_id][i].enable_flag != true)) {
              /* set T = min(Tc,Tue) */
              UE_PF_PO[CC_id][i].T = T;
              /* set UE_ID */
              UE_PF_PO[CC_id][i].ue_index_value = (uint16_t)S1AP_PAGING_IND(msg_p).ue_index_value;
              /* calculate PF and PO */
              /* set PF_min : SFN mod T = (T div N)*(UE_ID mod N) */
              UE_PF_PO[CC_id][i].PF_min = (T / N) * (UE_PF_PO[CC_id][i].ue_index_value % N);
              /* set PO */
              /* i_s = floor(UE_ID/N) mod Ns */
              i_s = (uint8_t)((UE_PF_PO[CC_id][i].ue_index_value / N) % Ns);

              if (Ns == 1) {
                UE_PF_PO[CC_id][i].PO = (frame_type==FDD) ? 9 : 0;
              } else if (Ns==2) {
                UE_PF_PO[CC_id][i].PO = (frame_type==FDD) ? (4+(5*i_s)) : (5*i_s);
              } else if (Ns==4) {
                UE_PF_PO[CC_id][i].PO = (frame_type==FDD) ? (4*(i_s&1)+(5*(i_s>>1))) : ((i_s&1)+(5*(i_s>>1)));
              }

              if (UE_PF_PO[CC_id][i].enable_flag == true) {
                //paging exist UE log
                LOG_D(RRC,"[eNB %ld] CC_id %d In S1AP_PAGING_IND: Update exist UE %d, T %d, PF %d, PO %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value, T, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].PO);
              } else {
                /* set enable_flag */
                UE_PF_PO[CC_id][i].enable_flag = true;
                //paging new UE log
                LOG_D(RRC,"[eNB %ld] CC_id %d In S1AP_PAGING_IND: Insert a new UE %d, T %d, PF %d, PO %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value, T, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].PO);
              }

              break;
            }
          }

          pthread_mutex_unlock(&ue_pf_po_mutex);
          uint32_t length;
          uint8_t buffer[RRC_BUF_SIZE];
          uint8_t *message_buffer;
          /* Transfer data to PDCP */
          MessageDef *message_p;
          message_p = itti_alloc_new_message (TASK_RRC_ENB, 0, RRC_PCCH_DATA_REQ);
          /* Create message for PDCP (DLInformationTransfer_t) */
          length = do_Paging (instance,
                              buffer, sizeof(buffer),
                              S1AP_PAGING_IND(msg_p).ue_paging_identity,
                              S1AP_PAGING_IND(msg_p).cn_domain);

          if(length == -1) {
            LOG_I(RRC, "do_Paging error");
            return -1;
          }

          message_buffer = itti_malloc (TASK_RRC_ENB, TASK_PDCP_ENB, length);
          /* Uses a new buffer to avoid issue with PDCP buffer content that could be changed by PDCP (asynchronous message handling). */
          memcpy (message_buffer, buffer, length);
          RRC_PCCH_DATA_REQ (message_p).sdu_size  = length;
          RRC_PCCH_DATA_REQ (message_p).sdu_p     = message_buffer;
          RRC_PCCH_DATA_REQ (message_p).mode      = PDCP_TRANSMISSION_MODE_TRANSPARENT;  /* not used */
          RRC_PCCH_DATA_REQ (message_p).rnti      = P_RNTI;
          RRC_PCCH_DATA_REQ (message_p).ue_index  = i;
          RRC_PCCH_DATA_REQ (message_p).CC_id  = CC_id;
          LOG_D(RRC, "[eNB %ld] CC_id %d In S1AP_PAGING_IND: send encdoed buffer to PDCP buffer_size %d\n", instance, CC_id, length);
          itti_send_msg_to_task (TASK_PDCP_ENB, instance, message_p);
        }
      }
    }
  }

  return (0);
}

/*NN: careful about the typcast of xid (long -> uint8_t*/
int rrc_eNB_send_PATH_SWITCH_REQ(const protocol_ctxt_t *const ctxt_pP,
                                 rrc_eNB_ue_context_t          *const ue_context_pP) {
  MessageDef      *msg_p         = NULL;
  int e_rab = 0;
  int e_rabs_done = 0;
  rrc_ue_s1ap_ids_t  *rrc_ue_s1ap_ids_p = NULL;
  hashtable_rc_t      h_rc;
  gtpv1u_enb_create_tunnel_req_t  create_tunnel_req;
  gtpv1u_enb_create_tunnel_resp_t create_tunnel_resp;
  uint8_t inde_list[ue_context_pP->ue_context.nb_of_e_rabs];
  memset(inde_list, 0, ue_context_pP->ue_context.nb_of_e_rabs*sizeof(uint8_t));
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_PATH_SWITCH_REQ);
  ue_context_pP->ue_context.ue_initial_id = get_next_ue_initial_id (ctxt_pP->module_id);
  S1AP_PATH_SWITCH_REQ (msg_p).ue_initial_id = ue_context_pP->ue_context.ue_initial_id;
  rrc_ue_s1ap_ids_p = malloc(sizeof(*rrc_ue_s1ap_ids_p));
  rrc_ue_s1ap_ids_p->ue_initial_id  = ue_context_pP->ue_context.ue_initial_id;
  rrc_ue_s1ap_ids_p->eNB_ue_s1ap_id = UE_INITIAL_ID_INVALID;
  rrc_ue_s1ap_ids_p->ue_rnti = ctxt_pP->rntiMaybeUEid;
  h_rc = hashtable_insert(RC.rrc[ctxt_pP->module_id]->initial_id2_s1ap_ids,
                          (hash_key_t)ue_context_pP->ue_context.ue_initial_id,
                          rrc_ue_s1ap_ids_p, free);

  if (h_rc != HASH_TABLE_OK) {
    LOG_E(S1AP, "[eNB %d] Error while hashtable_insert in initial_id2_s1ap_ids ue_initial_id %u\n",
          ctxt_pP->module_id, ue_context_pP->ue_context.ue_initial_id);
  }

  S1AP_PATH_SWITCH_REQ (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;
  S1AP_PATH_SWITCH_REQ (msg_p).mme_ue_s1ap_id = ue_context_pP->ue_context.mme_ue_s1ap_id;
  S1AP_PATH_SWITCH_REQ (msg_p).ue_gummei.mcc = ue_context_pP->ue_context.ue_gummei.mcc;
  S1AP_PATH_SWITCH_REQ (msg_p).ue_gummei.mnc = ue_context_pP->ue_context.ue_gummei.mnc;
  S1AP_PATH_SWITCH_REQ (msg_p).ue_gummei.mnc_len = ue_context_pP->ue_context.ue_gummei.mnc_len;
  S1AP_PATH_SWITCH_REQ (msg_p).ue_gummei.mme_code = ue_context_pP->ue_context.ue_gummei.mme_code;
  S1AP_PATH_SWITCH_REQ (msg_p).ue_gummei.mme_group_id = ue_context_pP->ue_context.ue_gummei.mme_group_id;
  S1AP_PATH_SWITCH_REQ (msg_p).security_capabilities.encryption_algorithms=ue_context_pP->ue_context.security_capabilities.encryption_algorithms;
  S1AP_PATH_SWITCH_REQ (msg_p).security_capabilities.integrity_algorithms=ue_context_pP->ue_context.security_capabilities.integrity_algorithms;
  LOG_I (RRC,"Path switch request: nb nb_of_e_rabs %u status %u\n",
         ue_context_pP->ue_context.nb_of_e_rabs,
         ue_context_pP->ue_context.e_rab[e_rab].status);
  memset(&create_tunnel_req, 0, sizeof(create_tunnel_req));

  // the context for UE to be handovered is obtained through ho_req message
  for (e_rab = 0; e_rab <  ue_context_pP->ue_context.nb_of_e_rabs ; e_rab++) {
    if (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_ESTABLISHED) {
      create_tunnel_req.eps_bearer_id[e_rabs_done]       = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
      create_tunnel_req.sgw_S1u_teid[e_rabs_done]        = ue_context_pP->ue_context.e_rab[e_rab].param.gtp_teid;
      memcpy(&create_tunnel_req.sgw_addr[e_rabs_done],
             &ue_context_pP->ue_context.e_rab[e_rab].param.sgw_addr,
             sizeof(transport_layer_addr_t));
      inde_list[e_rabs_done] = e_rab;
      e_rabs_done++;
    }
  }

  S1AP_PATH_SWITCH_REQ (msg_p).nb_of_e_rabs = e_rabs_done;
  create_tunnel_req.rnti           = ue_context_pP->ue_context.rnti;
  create_tunnel_req.num_tunnels    = e_rabs_done;
  gtpv1u_create_s1u_tunnel(ctxt_pP->instance, &create_tunnel_req, &create_tunnel_resp, pdcp_data_req);
  rrc_eNB_process_GTPV1U_CREATE_TUNNEL_RESP(ctxt_pP, &create_tunnel_resp, &inde_list[0]);

  for (e_rab = 0; e_rab < e_rabs_done; e_rab++) {
    S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].e_rab_id = create_tunnel_resp.eps_bearer_id[e_rab];
    S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].gtp_teid = create_tunnel_resp.enb_S1u_teid[e_rab];
    S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].eNB_addr = create_tunnel_resp.enb_addr;
    LOG_I (RRC,"enb_gtp_addr (msg index %d, e_rab index %d, status %d): nb_of_e_rabs %d,  e_rab_id %d, teid: %u, addr: %d.%d.%d.%d \n ",
           e_rabs_done,  e_rab, ue_context_pP->ue_context.e_rab[inde_list[e_rab]].status,
           S1AP_PATH_SWITCH_REQ (msg_p).nb_of_e_rabs,
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].e_rab_id,
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].gtp_teid,
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].eNB_addr.buffer[0],
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].eNB_addr.buffer[1],
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].eNB_addr.buffer[2],
           S1AP_PATH_SWITCH_REQ (msg_p).e_rabs_tobeswitched[e_rab].eNB_addr.buffer[3]);
  }

  // NN: add conditions for e_rabs_failed
  if (e_rabs_done > 0) {
    LOG_I(RRC,"S1AP_PATH_SWITCH_REQ: sending the message: nb_of_erabstobeswitched %d, total e_rabs %d, index %d\n",
          S1AP_PATH_SWITCH_REQ (msg_p).nb_of_e_rabs, ue_context_pP->ue_context.setup_e_rabs, e_rab);
    itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
  } else {
    itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  }

  return 0;
}

int rrc_eNB_process_X2AP_TUNNEL_SETUP_REQ(instance_t instance, rrc_eNB_ue_context_t *const ue_context_target_p) {
  gtpv1u_enb_create_x2u_tunnel_req_t  create_tunnel_req;
  gtpv1u_enb_create_x2u_tunnel_resp_t create_tunnel_resp;
  uint8_t                           e_rab_done;
  uint8_t                             inde_list[NB_RB_MAX - 3]= {0};

  if (ue_context_target_p == NULL) {
    return (-1);
  } else {
    /* Save e RAB information for later */
    {
      LOG_I(RRC, "[eNB %ld] rrc_eNB_process_X2AP_TUNNEL_SETUP_REQ: rnti %u nb_of_e_rabs %d\n",
            instance, ue_context_target_p->ue_context.rnti, ue_context_target_p->ue_context.nb_of_e_rabs);
      int i;
      memset(&create_tunnel_req, 0, sizeof(create_tunnel_req));
      uint8_t nb_e_rabs_tosetup = ue_context_target_p->ue_context.nb_of_e_rabs;
      e_rab_done = 0;

      for (i = 0; i < nb_e_rabs_tosetup; i++) {
        if(ue_context_target_p->ue_context.e_rab[i].status >= E_RAB_STATUS_DONE)
          continue;

        create_tunnel_req.eps_bearer_id[e_rab_done]       = ue_context_target_p->ue_context.e_rab[i].param.e_rab_id;
        LOG_I(RRC,"x2 tunnel setup: local index %d UE id %x, eps id %d \n",
              i,
              ue_context_target_p->ue_context.rnti,
              create_tunnel_req.eps_bearer_id[i] );
        inde_list[i] = e_rab_done;
        e_rab_done++;
      }

      create_tunnel_req.rnti           = ue_context_target_p->ue_context.rnti; // warning put zero above
      create_tunnel_req.num_tunnels    = e_rab_done;
      // NN: not sure if we should create a new tunnel: need to check teid, etc.
      gtpv1u_create_x2u_tunnel(
        instance,
        &create_tunnel_req,
        &create_tunnel_resp);
      ue_context_target_p->ue_context.nb_x2u_e_rabs = create_tunnel_resp.num_tunnels;

      for (i = 0; i < create_tunnel_resp.num_tunnels; i++) {
        ue_context_target_p->ue_context.enb_gtp_x2u_teid[inde_list[i]]  = create_tunnel_resp.enb_X2u_teid[i];
        ue_context_target_p->ue_context.enb_gtp_x2u_addrs[inde_list[i]] = create_tunnel_resp.enb_addr;
        ue_context_target_p->ue_context.enb_gtp_x2u_ebi[inde_list[i]]   = create_tunnel_resp.eps_bearer_id[i];
        LOG_I(RRC, "rrc_eNB_process_X2AP_TUNNEL_SETUP_REQ tunnel (%u, %u) bearer UE context index %u, msg index %u, eps bearer id %u, gtp addr len %d \n",
              create_tunnel_resp.enb_X2u_teid[i],
              ue_context_target_p->ue_context.enb_gtp_x2u_teid[inde_list[i]],
              inde_list[i],
              i,
              create_tunnel_resp.eps_bearer_id[i],
              create_tunnel_resp.enb_addr.length);
      }
    }
    return (0);
  }
}

int rrc_eNB_process_S1AP_PATH_SWITCH_REQ_ACK (MessageDef *msg_p,
    const char *msg_name,
    instance_t instance) {
  uint16_t                        ue_initial_id;
  uint32_t                        eNB_ue_s1ap_id;
  //gtpv1u_enb_create_tunnel_req_t  create_tunnel_req;
  //gtpv1u_enb_create_tunnel_resp_t create_tunnel_resp;
  gtpv1u_enb_delete_tunnel_req_t  delete_tunnel_req;
  struct rrc_eNB_ue_context_s *ue_context_p = NULL;
  protocol_ctxt_t              ctxt;
  int i;
  ue_initial_id  = S1AP_PATH_SWITCH_REQ_ACK (msg_p).ue_initial_id;
  eNB_ue_s1ap_id = S1AP_PATH_SWITCH_REQ_ACK (msg_p).eNB_ue_s1ap_id;
  ue_context_p   = rrc_eNB_get_ue_context_from_s1ap_ids(instance, ue_initial_id, eNB_ue_s1ap_id);
  LOG_I(RRC, "[eNB %ld] Received %s: ue_initial_id %d, eNB_ue_s1ap_id %d, nb_of_e_rabs %d\n",
        instance, msg_name, ue_initial_id, eNB_ue_s1ap_id, S1AP_E_RAB_SETUP_REQ (msg_p).nb_e_rabs_tosetup);

  if (ue_context_p == NULL) {
    /* Can not associate this message to an UE index, send a failure to S1AP and discard it! */
    //MessageDef *msg_fail_p = NULL;
    LOG_W(RRC, "[eNB %ld] In S1AP_PATH_SWITCH_REQ_ACK: unknown UE from S1AP ids (%d, %d)\n", instance, ue_initial_id, eNB_ue_s1ap_id);
    //msg_fail_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_PATH_SWITCH_REQ_ACK_FAIL);
    //S1AP_PATH_SWITCH_REQ_ACK  (msg_fail_p).eNB_ue_s1ap_id = eNB_ue_s1ap_id;
    // TODO add failure cause when defined!
    //itti_send_msg_to_task (TASK_S1AP, instance, msg_fail_p);
    return (-1);
  } else {
    PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, ENB_FLAG_YES, ue_context_p->ue_context.rnti, 0, 0);
    ue_context_p->ue_context.eNB_ue_s1ap_id = S1AP_PATH_SWITCH_REQ_ACK (msg_p).eNB_ue_s1ap_id;
    ue_context_p->ue_context.mme_ue_s1ap_id = S1AP_PATH_SWITCH_REQ_ACK (msg_p).mme_ue_s1ap_id;
    /* Save e RAB information for later */
    {
      ue_context_p->ue_context.nb_release_of_e_rabs = S1AP_PATH_SWITCH_REQ_ACK (msg_p).nb_e_rabs_tobereleased;

      for (i = 0;
           i < ue_context_p->ue_context.setup_e_rabs; // go over total number of e_rabs received through x2_ho_req msg
           i++) {
        // assume that we are releasing all the DRBs
        ue_context_p->ue_context.e_rab[i].status = E_RAB_STATUS_REESTABLISHED;

        if (ue_context_p->ue_context.nb_release_of_e_rabs==0) {
          LOG_I(RRC,"Bearer re-established with ID: %d\n", ue_context_p->ue_context.e_rab[i].param.e_rab_id);
        }
      }

      //memset(&create_tunnel_req, 0 , sizeof(create_tunnel_req));
      uint8_t nb_e_rabs_tobeswitched = S1AP_PATH_SWITCH_REQ_ACK (msg_p).nb_e_rabs_tobeswitched;

      // keep the previous bearer
      // the index for the rec
      if (nb_e_rabs_tobeswitched>0) {
        int e_rab_switch_index=0;

        for (i = 0;
             i < ue_context_p->ue_context.setup_e_rabs; // go over total number of e_rabs received through x2_ho_req msg
             i++) {
          /* Harmonize with enb_gtp_teid, enb_gtp_addrs, and enb_gtp_rbi vars in the top level structure */
          if (ue_context_p->ue_context.e_rab[i].param.e_rab_id == S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobeswitched[e_rab_switch_index].e_rab_id) {
            ue_context_p->ue_context.e_rab[i].param.e_rab_id = S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobeswitched[e_rab_switch_index].e_rab_id;
            ue_context_p->ue_context.e_rab[i].param.sgw_addr= S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobeswitched[e_rab_switch_index].sgw_addr;
            ue_context_p->ue_context.e_rab[i].param.gtp_teid = S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobeswitched[e_rab_switch_index].gtp_teid;
            e_rab_switch_index++;
          }
        }
      }
    }
    ue_context_p->ue_context.ue_ambr=S1AP_PATH_SWITCH_REQ_ACK (msg_p).ue_ambr;
    ue_context_p->ue_context.setup_e_rabs = ue_context_p->ue_context.setup_e_rabs - ue_context_p->ue_context.nb_release_of_e_rabs;
    ue_context_p->ue_context.nb_of_e_rabs = ue_context_p->ue_context.nb_of_e_rabs - ue_context_p->ue_context.nb_release_of_e_rabs;
    memset(&delete_tunnel_req, 0, sizeof(delete_tunnel_req));

    if (ue_context_p->ue_context.nb_release_of_e_rabs>0) {
      int e_rab_release_index=0;

      for (i = 0;
           i < ue_context_p->ue_context.setup_e_rabs;
           i++) {
        if (ue_context_p->ue_context.e_rab[i].param.e_rab_id == S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobereleased[e_rab_release_index].e_rab_id) {
          LOG_I(RRC,"Bearer released with ID: %d\n", ue_context_p->ue_context.e_rab[i].param.e_rab_id);
          ue_context_p->ue_context.e_rab[i].status =  E_RAB_STATUS_TORELEASE;
          ue_context_p->ue_context.e_rabs_tobereleased[e_rab_release_index]=S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobereleased[e_rab_release_index].e_rab_id;
          delete_tunnel_req.eps_bearer_id[e_rab_release_index] = S1AP_PATH_SWITCH_REQ_ACK (msg_p).e_rabs_tobereleased[e_rab_release_index].e_rab_id;
          e_rab_release_index++;
        } else {
          LOG_I(RRC,"Bearer re-established with ID: %d\n", ue_context_p->ue_context.e_rab[i].param.e_rab_id);
        }
      }
    }

    if (ue_context_p->ue_context.nb_release_of_e_rabs>0) {
      delete_tunnel_req.rnti= ue_context_p->ue_context.rnti;
      delete_tunnel_req.num_erab= ue_context_p->ue_context.nb_release_of_e_rabs;
      /* this could also be done through ITTI message */
      gtpv1u_delete_s1u_tunnel(instance,
                               &delete_tunnel_req);
      /* TBD: release the DRB not admitted */
      //rrc_eNB_generate_dedicatedRRCConnectionReconfiguration(&ctxt, ue_context_p, 0);
      if ( ue_context_p->ue_context.ue_release_timer_rrc > 0 &&
	   (ue_context_p->ue_context.handover_info == NULL ||
	    (ue_context_p->ue_context.handover_info->state != HO_RELEASE &&
	     ue_context_p->ue_context.handover_info->state != HO_CANCEL
	     )
	    )
	   )
      ue_context_p->ue_context.ue_release_timer_rrc = ue_context_p->ue_context.ue_release_timer_thres_rrc;
  }
  
    /* Security key */
    ue_context_p->ue_context.next_hop_chain_count=S1AP_PATH_SWITCH_REQ_ACK (msg_p).next_hop_chain_count;
    memcpy ( ue_context_p->ue_context.next_security_key,
             S1AP_PATH_SWITCH_REQ_ACK (msg_p).next_security_key,
             SECURITY_KEY_LENGTH);
    rrc_eNB_send_X2AP_UE_CONTEXT_RELEASE(&ctxt, ue_context_p);
    return (0);
  }
}

int rrc_eNB_send_X2AP_UE_CONTEXT_RELEASE(const protocol_ctxt_t *const ctxt_pP,
    rrc_eNB_ue_context_t *const ue_context_pP) {
  MessageDef      *msg_p         = NULL;
  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, X2AP_UE_CONTEXT_RELEASE);
  X2AP_UE_CONTEXT_RELEASE (msg_p).rnti = ue_context_pP->ue_context.rnti;
  X2AP_UE_CONTEXT_RELEASE (msg_p).source_assoc_id = ue_context_pP->ue_context.handover_info->assoc_id;
  itti_send_msg_to_task (TASK_X2AP, ctxt_pP->instance, msg_p);
  return (0);
}

int s1ap_ue_context_release(instance_t instance, const uint32_t eNB_ue_s1ap_id) {
  s1ap_eNB_instance_t *s1ap_eNB_instance_p = NULL;
  struct s1ap_eNB_ue_context_s *ue_context_p = NULL;
  s1ap_eNB_instance_p = s1ap_eNB_get_instance(instance);
  DevAssert(s1ap_eNB_instance_p != NULL);

  if ((ue_context_p = s1ap_eNB_get_ue_context(s1ap_eNB_instance_p,
                      eNB_ue_s1ap_id)) == NULL) {
    /* The context for this eNB ue s1ap id doesn't exist in the map of eNB UEs */
    LOG_W(RRC,"Failed to find ue context associated with eNB ue s1ap id: %u\n",
          eNB_ue_s1ap_id);
    return -1;
  }

  // release UE context
  struct s1ap_eNB_ue_context_s *ue_context2_p = NULL;

  if ((ue_context2_p = RB_REMOVE(s1ap_ue_map, &s1ap_eNB_instance_p->s1ap_ue_head, ue_context_p))
      != NULL) {
    LOG_W(RRC,"Removed UE context eNB_ue_s1ap_id %u\n",
          ue_context2_p->eNB_ue_s1ap_id);
    s1ap_eNB_free_ue_context(ue_context2_p);
  } else {
    LOG_W(RRC,"Removing UE context eNB_ue_s1ap_id %u: did not find context\n",
          ue_context_p->eNB_ue_s1ap_id);
  }

  /*RB_FOREACH(ue_context_p, s1ap_ue_map, &s1ap_eNB_instance_p->s1ap_ue_head) {
    S1AP_WARN("in s1ap_ue_map: UE context eNB_ue_s1ap_id %u mme_ue_s1ap_id %u state %u\n",
        ue_context_p->eNB_ue_s1ap_id, ue_context_p->mme_ue_s1ap_id,
        ue_context_p->ue_state);
  }*/
  return 0;
}


int rrc_eNB_send_E_RAB_Modification_Indication(const protocol_ctxt_t *const ctxt_pP,
                                 rrc_eNB_ue_context_t          *const ue_context_pP) {
  MessageDef      *msg_p         = NULL;
  int e_rab = 0;
  int e_rab_modify_index = 0;
  int e_rab_notmodify_index = 0;

  uint8_t inde_list[ue_context_pP->ue_context.nb_of_e_rabs];
  memset(inde_list, 0, ue_context_pP->ue_context.nb_of_e_rabs*sizeof(uint8_t));

  msg_p = itti_alloc_new_message (TASK_RRC_ENB, 0, S1AP_E_RAB_MODIFICATION_IND);


  S1AP_E_RAB_MODIFICATION_IND (msg_p).eNB_ue_s1ap_id = ue_context_pP->ue_context.eNB_ue_s1ap_id;
  S1AP_E_RAB_MODIFICATION_IND (msg_p).mme_ue_s1ap_id = ue_context_pP->ue_context.mme_ue_s1ap_id;

  LOG_I (RRC,"E-RAB modification indication: nb nb_of_e_rabs %u status %u\n",
         ue_context_pP->ue_context.nb_of_e_rabs,
         ue_context_pP->ue_context.e_rab[e_rab].status);

  if (ue_context_pP->ue_context.nb_of_modify_endc_e_rabs > 0){
	  S1AP_E_RAB_MODIFICATION_IND (msg_p).nb_of_e_rabs_tobemodified = ue_context_pP->ue_context.nb_of_modify_endc_e_rabs;
	  for (e_rab = 0; e_rab <  ue_context_pP->ue_context.setup_e_rabs ; e_rab++) {
		  //Add E-RAB in the list of E-RABs to be modified
		  if (ue_context_pP->ue_context.e_rab[e_rab].status == E_RAB_STATUS_TOMODIFY) {
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_tobemodified[e_rab_modify_index].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
			  memcpy(S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_tobemodified[e_rab_modify_index].eNB_addr.buffer,
					  ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].buffer,
					  ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].length);
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_tobemodified[e_rab_modify_index].eNB_addr.length = ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].length;
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_tobemodified[e_rab_modify_index].gtp_teid = ue_context_pP->ue_context.gnb_gtp_endc_teid[e_rab];
			  e_rab_modify_index++;
		  }
		  //Add E-RAB in the list of E-RABs NOT to be modified
		  else{
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_nottobemodified[e_rab_notmodify_index].e_rab_id = ue_context_pP->ue_context.e_rab[e_rab].param.e_rab_id;
			  memcpy(S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_nottobemodified[e_rab_notmodify_index].eNB_addr.buffer,
					  ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].buffer,
					  ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].length);
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_nottobemodified[e_rab_notmodify_index].eNB_addr.length = ue_context_pP->ue_context.gnb_gtp_endc_addrs[e_rab].length;
			  S1AP_E_RAB_MODIFICATION_IND (msg_p).e_rabs_nottobemodified[e_rab_notmodify_index].gtp_teid = ue_context_pP->ue_context.gnb_gtp_endc_teid[e_rab];
			  e_rab_notmodify_index++;
		  }
	  }
	  S1AP_E_RAB_MODIFICATION_IND (msg_p).nb_of_e_rabs_nottobemodified = e_rab_notmodify_index;
  }

  if (e_rab_modify_index > 0) {
    LOG_I(RRC,"S1AP_E_RAB_MODIFICATION_IND: sending the message: nb_of_erabstobemodified %d, total e_rabs %d, index %d\n",
    		S1AP_E_RAB_MODIFICATION_IND (msg_p).nb_of_e_rabs_tobemodified, ue_context_pP->ue_context.setup_e_rabs, e_rab);
    itti_send_msg_to_task (TASK_S1AP, ctxt_pP->instance, msg_p);
  } else {
    itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
  }

  return 0;
}



