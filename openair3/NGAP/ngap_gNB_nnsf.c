/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap NAS node selection functions
 * @ingroup _ngap
 */

#include "ngap_gNB_nnsf.h"
#include <stdio.h>
#include "ngap_gNB_defs.h"
#include "ngap_common.h"
#include "queue.h"
#include "tree.h"

ngap_gNB_amf_data_t *ngap_gNB_nnsf_select_amf(ngap_gNB_instance_t *instance_p, const ngap_rrc_establishment_cause_t cause)
{
  struct ngap_gNB_amf_data_s *amf_data_p = NULL;
  struct ngap_gNB_amf_data_s *amf_highest_capacity_p = NULL;
  uint8_t current_capacity = 0;

  RB_FOREACH(amf_data_p, ngap_amf_map, &instance_p->ngap_amf_head) {
    if (amf_data_p->state != NGAP_GNB_STATE_CONNECTED) {
      /* The association between AMF and gNB is not ready for the moment,
       * go to the next known AMF.
       */
      if (amf_data_p->state == NGAP_GNB_OVERLOAD) {
        /* AMF is overloaded. We have to check the RRC establishment
         * cause and take decision to the select this AMF depending on
         * the overload state.
         */
        if ((cause == NGAP_RRC_CAUSE_MO_DATA)
            && (amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_MO_DATA)) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_ALL_SIGNALLING)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA))) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_ONLY_EMERGENCY_AND_MT)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA)
                || (cause == NGAP_RRC_CAUSE_HIGH_PRIO_ACCESS))) {
          continue;
        }

        /* At this point, the RRC establishment can be handled by the AMF
         * even if it is in overload state.
         */
      } else {
        /* The AMF is not overloaded, association is simply not ready. */
        continue;
      }
    }

    if (current_capacity < amf_data_p->relative_amf_capacity) {
      /* We find a better AMF, keep a reference to it */
      current_capacity = amf_data_p->relative_amf_capacity;
      amf_highest_capacity_p = amf_data_p;
    }
  }

  return amf_highest_capacity_p;
}

ngap_gNB_amf_data_t *ngap_gNB_nnsf_select_amf_by_plmn_id(ngap_gNB_instance_t *instance_p,
                                                         const ngap_rrc_establishment_cause_t cause,
                                                         const plmn_id_t selected_plmn_identity)
{
  struct ngap_gNB_amf_data_s *amf_data_p = NULL;
  struct ngap_gNB_amf_data_s *amf_highest_capacity_p = NULL;
  uint8_t current_capacity = 0;

  RB_FOREACH(amf_data_p, ngap_amf_map, &instance_p->ngap_amf_head) {
    struct served_guami_s *guami_p = NULL;
    if (amf_data_p->state != NGAP_GNB_STATE_CONNECTED) {
      /* The association between AMF and gNB is not ready for the moment,
       * go to the next known AMF.
       */
      if (amf_data_p->state == NGAP_GNB_OVERLOAD) {
        /* AMF is overloaded. We have to check the RRC establishment
         * cause and take decision to the select this AMF depending on
         * the overload state.
         */
        if ((cause == NGAP_RRC_CAUSE_MO_DATA)
            && (amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_MO_DATA)) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_ALL_SIGNALLING)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA))) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_ONLY_EMERGENCY_AND_MT)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA)
                || (cause == NGAP_RRC_CAUSE_HIGH_PRIO_ACCESS))) {
          continue;
        }

        /* At this point, the RRC establishment can be handled by the AMF
         * even if it is in overload state.
         */
      } else {
        /* The AMF is not overloaded, association is simply not ready. */
        continue;
      }
    }

    /* Looking for served GUAMI PLMN Identity selected matching the one provided by the UE */
    STAILQ_FOREACH(guami_p, &amf_data_p->served_guami, next) {
      if (eq_ngap_plmn(&guami_p->guami.plmn, &selected_plmn_identity)) {
        break;
      }
    }
    /* if we didn't find such a served PLMN, go on with the next AMF */
    if (!guami_p)
      continue;

    if (current_capacity < amf_data_p->relative_amf_capacity) {
      /* We find a better AMF, keep a reference to it */
      current_capacity = amf_data_p->relative_amf_capacity;
      amf_highest_capacity_p = amf_data_p;
    }
  }

  return amf_highest_capacity_p;
}

ngap_gNB_amf_data_t *ngap_gNB_nnsf_select_amf_by_amf_setid(ngap_gNB_instance_t *instance_p,
                                                           const ngap_rrc_establishment_cause_t cause,
                                                           const plmn_id_t selected_plmn_identity,
                                                           uint16_t amf_setid)
{
  struct ngap_gNB_amf_data_s *amf_data_p = NULL;

  RB_FOREACH(amf_data_p, ngap_amf_map, &instance_p->ngap_amf_head) {
    struct served_guami_s *guami_p = NULL;

    if (amf_data_p->state != NGAP_GNB_STATE_CONNECTED) {
      /* The association between AMF and gNB is not ready for the moment,
       * go to the next known AMF.
       */
      if (amf_data_p->state == NGAP_GNB_OVERLOAD) {
        /* AMF is overloaded. We have to check the RRC establishment
         * cause and take decision to the select this AMF depending on
         * the overload state.
         */
        if ((cause == NGAP_RRC_CAUSE_MO_DATA)
            && (amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_MO_DATA)) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_ALL_SIGNALLING)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA))) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_ONLY_EMERGENCY_AND_MT)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA)
                || (cause == NGAP_RRC_CAUSE_HIGH_PRIO_ACCESS))) {
          continue;
        }

        /* At this point, the RRC establishment can be handled by the AMF
         * even if it is in overload state.
         */
      } else {
        /* The AMF is not overloaded, association is simply not ready. */
        continue;
      }
    }

    /* Looking for AMF code matching the one provided by NAS */
    STAILQ_FOREACH(guami_p, &amf_data_p->served_guami, next) {
      if (eq_ngap_plmn(&guami_p->guami.plmn, &selected_plmn_identity)
          && (guami_p->guami.amf_set_id == amf_setid)) {
        return amf_data_p;
      }
    }
  }

  NGAP_WARN("No matching AMF found for PLMN (MCC=%03d MNC=%0*d) and AMF SetID=%u\n",
            selected_plmn_identity.mcc,
            selected_plmn_identity.mnc_digit_length,
            selected_plmn_identity.mnc,
            amf_setid);

  /* At this point no AMF matches the selected PLMN and AMF code. In this case,
   * return NULL. That way the RRC layer should know about it and reject RRC
   * connectivity. */
  return NULL;
}

ngap_gNB_amf_data_t *ngap_gNB_nnsf_select_amf_by_guami(ngap_gNB_instance_t *instance_p,
                                                       const ngap_rrc_establishment_cause_t cause,
                                                       const nr_guami_t guami)
{
  struct ngap_gNB_amf_data_s *amf_data_p             = NULL;

  RB_FOREACH(amf_data_p, ngap_amf_map, &instance_p->ngap_amf_head) {
    struct served_guami_s *guami_p = NULL;

    if (amf_data_p->state != NGAP_GNB_STATE_CONNECTED) {
      /* The association between AMF and gNB is not ready for the moment,
       * go to the next known AMF.
       */
      if (amf_data_p->state == NGAP_GNB_OVERLOAD) {
        /* AMF is overloaded. We have to check the RRC establishment
         * cause and take decision to the select this AMF depending on
         * the overload state.
         */
        if ((cause == NGAP_RRC_CAUSE_MO_DATA)
            && (amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_MO_DATA)) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_REJECT_ALL_SIGNALLING)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA))) {
          continue;
        }

        if ((amf_data_p->overload_state == NGAP_OVERLOAD_ONLY_EMERGENCY_AND_MT)
            && ((cause == NGAP_RRC_CAUSE_MO_SIGNALLING) || (cause == NGAP_RRC_CAUSE_MO_DATA)
                || (cause == NGAP_RRC_CAUSE_HIGH_PRIO_ACCESS))) {
          continue;
        }

        /* At this point, the RRC establishment can be handled by the AMF
         * even if it is in overload state.
         */
      } else {
        /* The AMF is not overloaded, association is simply not ready. */
        continue;
      }
    }

    /* Looking for AMF guami matching the one provided by NAS */
    STAILQ_FOREACH(guami_p, &amf_data_p->served_guami, next) {
      if (eq_ngap_guami(&guami_p->guami, &guami)) {
        return amf_data_p;
      }
    }
  }

  NGAP_WARN("No matching AMF found for GUAMI (MCC=%03d MNC=%0*d RegionID=%d SetID=%d Pointer=%d)\n",
            guami.plmn.mcc,
            guami.plmn.mnc_digit_length,
            guami.plmn.mnc,
            guami.amf_region_id,
            guami.amf_set_id,
            guami.amf_pointer);

  /* At this point no AMF matches the provided GUAMI. In this case, return
   * NULL. That way the RRC layer should know about it and reject RRC
   * connectivity. */
  return NULL;
}

ngap_gNB_amf_data_t *ngap_gNB_nnsf_select_amf_by_guami_no_cause(ngap_gNB_instance_t *instance_p, const nr_guami_t guami)
{
  struct ngap_gNB_amf_data_s *amf_data_p             = NULL;
  struct ngap_gNB_amf_data_s *amf_highest_capacity_p = NULL;
  uint8_t                     current_capacity       = 0;


  RB_FOREACH(amf_data_p, ngap_amf_map, &instance_p->ngap_amf_head) {
    struct served_guami_s *guami_p = NULL;

    if (amf_data_p->state != NGAP_GNB_STATE_CONNECTED) {
      /* The association between AMF and gNB is not ready for the moment,
       * go to the next known AMF.
       */
      if (amf_data_p->state == NGAP_GNB_OVERLOAD) {
        /* AMF is overloaded. We have to check the RRC establishment
         * cause and take decision to the select this AMF depending on
         * the overload state.
         */
    } else {
        /* The AMF is not overloaded, association is simply not ready. */
        continue;
      }
    }

    if (current_capacity < amf_data_p->relative_amf_capacity) {
      /* We find a better AMF, keep a reference to it */
      current_capacity = amf_data_p->relative_amf_capacity;
      amf_highest_capacity_p = amf_data_p;
    }

    /* Looking for AMF guami matching the one provided by NAS */
    STAILQ_FOREACH(guami_p, &amf_data_p->served_guami, next) {
      if (eq_ngap_guami(&guami_p->guami, &guami)) {
        return amf_data_p;
      }
    }
  }

  /* At this point no AMF matches the provided GUAMI. Select the one with the
   * highest relative capacity.
   * In case the list of known AMF is empty, simply return NULL, that way the RRC
   * layer should know about it and reject RRC connectivity.
   */

  return amf_highest_capacity_p;
}
