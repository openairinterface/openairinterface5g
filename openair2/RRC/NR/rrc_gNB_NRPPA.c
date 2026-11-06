/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */
#include "rrc_gNB_NRPPA.h"
#include "nr_rrc_defs.h"
#include "openair2/COMMON/nrppa_messages_types.h"
#include "openair2/COMMON/f1ap_messages_types.h"
#include "rrc_gNB_du.h"
#include "openair2/F1AP/lib/f1ap_positioning.h"
#include "openair3/NRPPA/nrppa_gNB_ue_context.h"
#include "openair2/RRC/NR/rrc_gNB_UE_context.h"
#include "openair2/F1AP/f1ap_ids.h"

static nrppa_trp_reference_point_type_t f1ap2nrppa_reference_point_type(const f1ap_trp_reference_point_type_t *in)
{
  nrppa_trp_reference_point_type_t out = {0};
  switch (in->present) {
    case F1AP_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_GEODETIC:
      out.present = NRPPA_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_GEODETIC;
      const f1ap_relative_geodetic_location_t *f1_tRPPositionRelativeGeodetic = &in->choice.trp_position_relative_geodetic;
      nrppa_relative_geodetic_location_t *tRPPositionRelativeGeodetic = &out.choice.trp_position_relative_geodetic;
      tRPPositionRelativeGeodetic->milli_arc_second_units = f1_tRPPositionRelativeGeodetic->milli_arc_second_units;
      tRPPositionRelativeGeodetic->height_units = f1_tRPPositionRelativeGeodetic->height_units;
      tRPPositionRelativeGeodetic->delta_latitude = f1_tRPPositionRelativeGeodetic->delta_latitude;
      tRPPositionRelativeGeodetic->delta_longitude = f1_tRPPositionRelativeGeodetic->delta_longitude;
      tRPPositionRelativeGeodetic->delta_height = f1_tRPPositionRelativeGeodetic->delta_height;

      const f1ap_location_uncertainty_t *f1_locationUncertainty_g = &f1_tRPPositionRelativeGeodetic->location_uncertainty;
      nrppa_location_uncertainty_t *locationUncertainty_g = &tRPPositionRelativeGeodetic->location_uncertainty;
      locationUncertainty_g->horizontal_uncertainty = f1_locationUncertainty_g->horizontal_uncertainty;
      locationUncertainty_g->horizontal_confidence = f1_locationUncertainty_g->horizontal_confidence;
      locationUncertainty_g->vertical_uncertainty = f1_locationUncertainty_g->vertical_uncertainty;
      locationUncertainty_g->vertical_confidence = f1_locationUncertainty_g->vertical_confidence;
      break;
    case F1AP_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_CARTESIAN:
      out.present = NRPPA_TRP_REFERENCE_POINT_TYPE_PR_TRPPOSITION_RELATIVE_CARTESIAN;
      const f1ap_relative_cartesian_location_t *f1_tRPPositionRelativeCartesian = &in->choice.trp_position_relative_cartesian;
      nrppa_relative_cartesian_location_t *tRPPositionRelativeCartesian = &out.choice.trp_position_relative_cartesian;
      tRPPositionRelativeCartesian->xyz_unit = f1_tRPPositionRelativeCartesian->xyz_unit;
      tRPPositionRelativeCartesian->xvalue = f1_tRPPositionRelativeCartesian->xvalue;
      tRPPositionRelativeCartesian->yvalue = f1_tRPPositionRelativeCartesian->yvalue;
      tRPPositionRelativeCartesian->zvalue = f1_tRPPositionRelativeCartesian->zvalue;

      const f1ap_location_uncertainty_t *f1_locationUncertainty_c = &f1_tRPPositionRelativeCartesian->location_uncertainty;
      nrppa_location_uncertainty_t *locationUncertainty_c = &tRPPositionRelativeCartesian->location_uncertainty;
      locationUncertainty_c->horizontal_uncertainty = f1_locationUncertainty_c->horizontal_uncertainty;
      locationUncertainty_c->horizontal_confidence = f1_locationUncertainty_c->horizontal_confidence;
      locationUncertainty_c->vertical_uncertainty = f1_locationUncertainty_c->vertical_uncertainty;
      locationUncertainty_c->vertical_confidence = f1_locationUncertainty_c->vertical_confidence;
      break;
    default:
      AssertFatal(false, "illegal trp reference point type entry %d\n", in->present);
      break;
  }
  return out;
}

static nrppa_ngran_high_accuracy_access_point_position_t f1ap2nrppa_trp_ha_pos(
    const f1ap_ngran_high_accuracy_access_point_position_t *in)
{
  nrppa_ngran_high_accuracy_access_point_position_t out = {0};
  out.latitude = in->latitude;
  out.longitude = in->longitude;
  out.altitude = in->altitude;
  out.uncertainty_semi_major = in->uncertainty_semi_major;
  out.uncertainty_semi_minor = in->uncertainty_semi_minor;
  out.orientation_of_major_axis = in->orientation_of_major_axis;
  out.horizontal_confidence = in->horizontal_confidence;
  out.uncertainty_altitude = in->uncertainty_altitude;
  out.vertical_confidence = in->vertical_confidence;
  return out;
}

static nrppa_geographical_coordinates_t f1ap2nrppa_geographical_coordinates(const f1ap_geographical_coordinates_t *in)
{
  nrppa_geographical_coordinates_t out = {0};
  const f1ap_trp_position_definition_type_t *f1_trp_pos_def_type = &in->trp_position_definition_type;
  nrppa_trp_position_definition_type_t *trp_pos_def_type = &out.trp_position_definition_type;
  switch (f1_trp_pos_def_type->present) {
    case F1AP_TRP_POSITION_DEFINITION_TYPE_PR_NOTHING:
      trp_pos_def_type->present = NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_NOTHING;
      break;
    case F1AP_TRP_POSITION_DEFINITION_TYPE_PR_DIRECT:
      trp_pos_def_type->present = NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_DIRECT;
      const f1ap_trp_position_direct_t *f1_direct = &f1_trp_pos_def_type->choice.direct;
      nrppa_trp_position_direct_t *direct = &trp_pos_def_type->choice.direct;

      if (f1_direct->accuracy.present == F1AP_TRP_POSITION_DIRECT_ACCURACY_PR_TRPPOSITION) {
        direct->accuracy.present = NRPPA_TRP_POSITION_DIRECT_ACCURACY_PR_TRPPOSITION;
        nrppa_access_point_position_t *trp_pos = &direct->accuracy.choice.trp_position;
        const f1ap_access_point_position_t *f1_trp_pos = &f1_direct->accuracy.choice.trp_position;
        trp_pos->latitude_sign = f1_trp_pos->latitude_sign;
        trp_pos->latitude = f1_trp_pos->latitude;
        trp_pos->longitude = f1_trp_pos->longitude;
        trp_pos->direction_of_altitude = f1_trp_pos->direction_of_altitude;
        trp_pos->altitude = f1_trp_pos->altitude;
        trp_pos->uncertainty_semi_major = f1_trp_pos->uncertainty_semi_major;
        trp_pos->uncertainty_semi_minor = f1_trp_pos->uncertainty_semi_minor;
        trp_pos->orientation_of_major_axis = f1_trp_pos->orientation_of_major_axis;
        trp_pos->uncertainty_altitude = f1_trp_pos->uncertainty_altitude;
        trp_pos->confidence = f1_trp_pos->confidence;
      } else if (f1_direct->accuracy.present == F1AP_TRP_POSITION_DIRECT_ACCURACY_PR_TRPHAPOSITION) {
        direct->accuracy.present = NRPPA_TRP_POSITION_DIRECT_ACCURACY_PR_TRPHAPOSITION;
        nrppa_ngran_high_accuracy_access_point_position_t *trp_ha_pos = &direct->accuracy.choice.trp_HAposition;
        const f1ap_ngran_high_accuracy_access_point_position_t *f1_trp_ha_pos = &f1_direct->accuracy.choice.trp_HAposition;
        *trp_ha_pos = f1ap2nrppa_trp_ha_pos(f1_trp_ha_pos);
      } else {
        AssertFatal(false, "illegal direct accuracy entry %d\n", direct->accuracy.present);
      }
      break;
    case F1AP_TRP_POSITION_DEFINITION_TYPE_PR_REFERENCED:
      trp_pos_def_type->present = NRPPA_TRP_POSITION_DEFINITION_TYPE_PR_REFERENCED;
      const f1ap_trp_position_referenced_t *f1_referenced = &f1_trp_pos_def_type->choice.referenced;
      nrppa_trp_position_referenced_t *referenced = &trp_pos_def_type->choice.referenced;
      const f1ap_reference_point_t *f1_referencePoint = &f1_referenced->reference_point;
      nrppa_reference_point_t *referencePoint = &referenced->reference_point;

      if (f1_referencePoint->present == F1AP_REFERENCE_POINT_PR_COORDINATEID) {
        referencePoint->present = NRPPA_REFERENCE_POINT_PR_COORDINATEID;
        referencePoint->choice.coordinate_id = f1_referencePoint->choice.coordinate_id;
      } else if (f1_referencePoint->present == F1AP_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATE) {
        referencePoint->present = NRPPA_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATE;
        nrppa_access_point_position_t *referencePointCoordinate = &referencePoint->choice.reference_point_coordinate;
        const f1ap_access_point_position_t *f1_referencePointCoordinate = &f1_referencePoint->choice.reference_point_coordinate;
        referencePointCoordinate->latitude_sign = f1_referencePointCoordinate->latitude_sign;
        referencePointCoordinate->latitude = f1_referencePointCoordinate->latitude;
        referencePointCoordinate->longitude = f1_referencePointCoordinate->longitude;
        referencePointCoordinate->direction_of_altitude = f1_referencePointCoordinate->direction_of_altitude;
        referencePointCoordinate->altitude = f1_referencePointCoordinate->altitude;
        referencePointCoordinate->uncertainty_semi_major = f1_referencePointCoordinate->uncertainty_semi_major;
        referencePointCoordinate->uncertainty_semi_minor = f1_referencePointCoordinate->uncertainty_semi_minor;
        referencePointCoordinate->orientation_of_major_axis = f1_referencePointCoordinate->orientation_of_major_axis;
        referencePointCoordinate->uncertainty_altitude = f1_referencePointCoordinate->uncertainty_altitude;
        referencePointCoordinate->confidence = f1_referencePointCoordinate->confidence;
      } else if (f1_referencePoint->present == F1AP_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATEHA) {
        referencePoint->present = NRPPA_REFERENCE_POINT_PR_REFERENCEPOINTCOORDINATEHA;
        nrppa_ngran_high_accuracy_access_point_position_t *referencePointCoordinateHA =
            &referencePoint->choice.reference_point_coordinateHA;
        const f1ap_ngran_high_accuracy_access_point_position_t *f1_referencePointCoordinateHA =
            &f1_referencePoint->choice.reference_point_coordinateHA;
        *referencePointCoordinateHA = f1ap2nrppa_trp_ha_pos(f1_referencePointCoordinateHA);
      } else {
        AssertFatal(false, "illegal reference point entry %d\n", referencePoint->present);
      }

      const f1ap_trp_reference_point_type_t *f1_referencePointType = &f1_referenced->reference_point_type;
      nrppa_trp_reference_point_type_t *referencePointType = &referenced->reference_point_type;
      *referencePointType = f1ap2nrppa_reference_point_type(f1_referencePointType);
      break;
    default:
      AssertFatal(false, "illegal Geographical Coordinates entry\n");
      break;
  }
  return out;
}

static nrppa_trp_information_type_response_item_t f1ap2nrppa_trp_info_type_response_item(
    const f1ap_trp_information_type_response_item_t *in)
{
  nrppa_trp_information_type_response_item_t out = {0};
  switch (in->present) {
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NOTHING:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NOTHING;
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PCI_NR:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PCI_NR;
      out.choice.pci_nr = in->choice.pci_nr;
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NG_RAN_CGI:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NG_RAN_CGI;
      out.choice.ng_ran_cgi.plmn.mcc = in->choice.ng_ran_cgi.plmn.mcc;
      out.choice.ng_ran_cgi.plmn.mnc = in->choice.ng_ran_cgi.plmn.mnc;
      out.choice.ng_ran_cgi.plmn.mnc_digit_length = in->choice.ng_ran_cgi.plmn.mnc_digit_length;
      out.choice.ng_ran_cgi.nr_cellid = in->choice.ng_ran_cgi.nr_cellid;
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NRARFCN:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_NRARFCN;
      out.choice.nr_arfcn = in->choice.nr_arfcn;
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PRSCONFIGURATION:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_PRSCONFIGURATION;
      AssertFatal(false, "TRP information type response item PRS configuration unsupported\n");
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SSBINFORMATION:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SSBINFORMATION;
      AssertFatal(false, "TRP information type response item SSB Information unsupported\n");
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SFNINITIALISATIONTIME:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SFNINITIALISATIONTIME;
      AssertFatal(false, "TRP information type response item SFN Initialization Time unsupported\n");
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SPATIALDIRECTIONINFORMATION:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_SPATIALDIRECTIONINFORMATION;
      AssertFatal(false, "TRP information type response item Spatial Direction Information unsupported\n");
      break;
    case F1AP_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_GEOGRAPHICALCOORDINATES:
      out.present = NRPPA_TRP_INFORMATION_TYPE_RESPONSE_ITEM_PR_GEOGRAPHICALCOORDINATES;
      out.choice.geographical_coordinates = f1ap2nrppa_geographical_coordinates(&in->choice.geographical_coordinates);
      break;
    default:
      AssertFatal(false, "received illegal trp information type response item %d\n", in->present);
      break;
  }
  return out;
}

static nrppa_subcarrier_spacing_pr f1ap2nrppa_subcarrier_spacing(const f1ap_subcarrier_spacing_pr scs)
{
  switch (scs) {
    case F1AP_SUBCARRIER_SPACING_15KHZ:
      return NRPPA_SUBCARRIER_SPACING_15KHZ;
    case F1AP_SUBCARRIER_SPACING_30KHZ:
      return NRPPA_SUBCARRIER_SPACING_30KHZ;
    case F1AP_SUBCARRIER_SPACING_60KHZ:
      return NRPPA_SUBCARRIER_SPACING_60KHZ;
    case F1AP_SUBCARRIER_SPACING_120KHZ:
      return NRPPA_SUBCARRIER_SPACING_120KHZ;
    default:
      AssertFatal(false, "Illegal Subcarrier Spacing\n");
      break;
  }
}

static nrppa_srs_configuration_t cp_f1ap_to_nrppa_srs_configuration(f1ap_srs_configuration_t *in)
{
  nrppa_srs_configuration_t srs_configuration = {0};
  nrppa_srs_carrier_list_t *srs_carrier_list = &srs_configuration.srs_carrier_list;

  f1ap_srs_carrier_list_t *f1_srs_carrier_list = &in->srs_carrier_list;

  uint32_t srs_carrier_list_length = f1_srs_carrier_list->srs_carrier_list_length;
  srs_carrier_list->srs_carrier_list_length = srs_carrier_list_length;

  if (srs_carrier_list_length > 0) {
    srs_carrier_list->srs_carrier_list_item =
        calloc_or_fail(srs_carrier_list_length, sizeof(*srs_carrier_list->srs_carrier_list_item));
  }

  for (int l = 0; l < srs_carrier_list_length; l++) {
    nrppa_srs_carrier_list_item_t *item = &srs_carrier_list->srs_carrier_list_item[l];
    f1ap_srs_carrier_list_item_t *f1_item = &f1_srs_carrier_list->srs_carrier_list_item[l];

    // pointA
    item->pointA = f1_item->pointA;

    // Uplink Channel BW-PerSCS-List
    nrppa_uplink_channel_bw_per_scs_list_t *ul_bw_list = &item->uplink_channel_bw_per_scs_list;
    f1ap_uplink_channel_bw_per_scs_list_t *f1_ul_bw_list = &f1_item->uplink_channel_bw_per_scs_list;

    uint32_t scs_specific_carrier_list_length = f1_ul_bw_list->scs_specific_carrier_list_length;
    ul_bw_list->scs_specific_carrier_list_length = scs_specific_carrier_list_length;

    if (scs_specific_carrier_list_length > 0) {
      ul_bw_list->scs_specific_carrier =
          calloc_or_fail(ul_bw_list->scs_specific_carrier_list_length, sizeof(*ul_bw_list->scs_specific_carrier));
    }

    for (int i = 0; i < scs_specific_carrier_list_length; i++) {
      nrppa_scs_specific_carrier_t *nrppa_scs = &ul_bw_list->scs_specific_carrier[i];
      f1ap_scs_specific_carrier_t *f1_scs = &f1_ul_bw_list->scs_specific_carrier[i];

      // offset to carrier
      nrppa_scs->offset_to_carrier = f1_scs->offset_to_carrier;

      // subcarrier spacing
      nrppa_scs->subcarrier_spacing = f1ap2nrppa_subcarrier_spacing(f1_scs->subcarrier_spacing);

      // carrier bandwidth
      nrppa_scs->carrier_bandwidth = f1_scs->carrier_bandwidth;
    }

    // Active UL BWP
    nrppa_active_ul_bwp_t *active_ul_bwp = &item->active_ul_bwp;
    f1ap_active_ul_bwp_t *f1_active_ul_bwp = &f1_item->active_ul_bwp;

    // location and bandwidth
    active_ul_bwp->location_and_bandwidth = f1_active_ul_bwp->location_and_bandwidth;

    // subcarrier spacing
    active_ul_bwp->subcarrier_spacing = f1ap2nrppa_subcarrier_spacing(f1_active_ul_bwp->subcarrier_spacing);

    if (f1_active_ul_bwp->cyclic_prefix) {
      active_ul_bwp->cyclic_prefix = NRPPA_CP_TYPE_EXTENDED;
    } else {
      active_ul_bwp->cyclic_prefix = NRPPA_CP_TYPE_NORMAL;
    }

    active_ul_bwp->tx_direct_current_location = f1_active_ul_bwp->tx_direct_current_location;

    nrppa_srs_config_t *srs_config = &active_ul_bwp->srs_config;
    f1ap_srs_config_t *f1_srs_config = &f1_active_ul_bwp->srs_config;

    // optional: srs_resource_list
    if (f1_srs_config->srs_resource_list) {
      f1ap_srs_resource_list_t *f1_srs_resource_list = f1_srs_config->srs_resource_list;

      srs_config->srs_resource_list = calloc_or_fail(1, sizeof(*srs_config->srs_resource_list));

      nrppa_srs_resource_list_t *srs_resource_list = srs_config->srs_resource_list;
      uint32_t srs_resource_list_length = f1_srs_resource_list->srs_resource_list_length;

      srs_resource_list->srs_resource_list_length = srs_resource_list_length;
      srs_resource_list->srs_resource = calloc_or_fail(srs_resource_list_length, sizeof(*srs_resource_list->srs_resource));

      for (int i = 0; i < srs_resource_list_length; i++) {
        nrppa_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
        f1ap_srs_resource_t *f1_srs_resource = &f1_srs_resource_list->srs_resource[i];

        srs_resource->srs_resource_id = f1_srs_resource->srs_resource_id;
        switch (f1_srs_resource->nr_of_srs_ports) {
          case F1AP_SRS_NUMBER_OF_PORTS_N1:
            srs_resource->nr_of_srs_ports = NRPPA_SRS_NUMBER_OF_PORTS_N1;
            break;
          case F1AP_SRS_NUMBER_OF_PORTS_N2:
            srs_resource->nr_of_srs_ports = NRPPA_SRS_NUMBER_OF_PORTS_N2;
            break;
          case F1AP_SRS_NUMBER_OF_PORTS_N4:
            srs_resource->nr_of_srs_ports = NRPPA_SRS_NUMBER_OF_PORTS_N4;
            break;
          default:
            AssertFatal(false, "Illegal number of ports %d\n", f1_srs_resource->nr_of_srs_ports);
            break;
        }

        f1ap_transmission_comb_t *f1_srs_tx_comb = &f1_srs_resource->transmission_comb;
        nrppa_transmission_comb_t *srs_tx_comb = &srs_resource->transmission_comb;

        switch (f1_srs_tx_comb->present) {
          case F1AP_TRANSMISSION_COMB_PR_NOTHING:
            srs_tx_comb->present = NRPPA_TRANSMISSION_COMB_PR_NOTHING;
            break;
          case F1AP_TRANSMISSION_COMB_PR_N2:
            srs_tx_comb->present = NRPPA_TRANSMISSION_COMB_PR_N2;
            srs_tx_comb->choice.n2.comb_offset_n2 = f1_srs_tx_comb->choice.n2.comb_offset_n2;
            srs_tx_comb->choice.n2.cyclic_shift_n2 = f1_srs_tx_comb->choice.n2.cyclic_shift_n2;
            break;
          case F1AP_TRANSMISSION_COMB_PR_N4:
            srs_tx_comb->present = NRPPA_TRANSMISSION_COMB_PR_N4;
            srs_tx_comb->choice.n4.comb_offset_n4 = f1_srs_tx_comb->choice.n4.comb_offset_n4;
            srs_tx_comb->choice.n4.cyclic_shift_n4 = f1_srs_tx_comb->choice.n4.cyclic_shift_n4;
            break;
          default:
            AssertFatal(false, "illegal transmissionComb %d\n", f1_srs_tx_comb->present);
            break;
        }

        srs_resource->start_position = f1_srs_resource->start_position;
        switch (f1_srs_resource->nr_of_symbols) {
          case F1AP_SRS_NUMBER_OF_SYMBOLS_N1:
            srs_resource->nr_of_symbols = NRPPA_SRS_NUMBER_OF_SYMBOLS_N1;
            break;
          case F1AP_SRS_NUMBER_OF_SYMBOLS_N2:
            srs_resource->nr_of_symbols = NRPPA_SRS_NUMBER_OF_SYMBOLS_N2;
            break;
          case F1AP_SRS_NUMBER_OF_SYMBOLS_N4:
            srs_resource->nr_of_symbols = NRPPA_SRS_NUMBER_OF_SYMBOLS_N4;
            break;
          default:
            AssertFatal(false, "illegal number of symbols %d\n", f1_srs_resource->nr_of_symbols);
            break;
        }
        switch (f1_srs_resource->repetition_factor) {
          case F1AP_SRS_REPETITION_FACTOR_RF1:
            srs_resource->repetition_factor = NRPPA_SRS_REPETITION_FACTOR_RF1;
            break;
          case F1AP_SRS_REPETITION_FACTOR_RF2:
            srs_resource->repetition_factor = NRPPA_SRS_REPETITION_FACTOR_RF2;
            break;
          case F1AP_SRS_REPETITION_FACTOR_RF4:
            srs_resource->repetition_factor = NRPPA_SRS_REPETITION_FACTOR_RF4;
            break;
          default:
            AssertFatal(false, "illegal repetition factor %d\n", f1_srs_resource->repetition_factor);
            break;
        }
        srs_resource->freq_domain_position = f1_srs_resource->freq_domain_position;
        srs_resource->freq_domain_shift = f1_srs_resource->freq_domain_shift;
        srs_resource->c_srs = f1_srs_resource->c_srs;
        srs_resource->b_srs = f1_srs_resource->b_srs;
        srs_resource->b_hop = f1_srs_resource->b_hop;
        switch (f1_srs_resource->group_or_sequence_hopping) {
          case F1AP_GROUPORSEQUENCEHOPPING_NOTHING:
            srs_resource->group_or_sequence_hopping = NRPPA_GROUPORSEQUENCEHOPPING_NOTHING;
            break;
          case F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING:
            srs_resource->group_or_sequence_hopping = NRPPA_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
            break;
          case F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING:
            srs_resource->group_or_sequence_hopping = NRPPA_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
            break;
          default:
            AssertFatal(false, "illegal group or sequence hopping %d\n", f1_srs_resource->group_or_sequence_hopping);
            break;
        }

        f1ap_resource_type_t *f1_res_type = &f1_srs_resource->resource_type;
        nrppa_resource_type_t *res_type = &srs_resource->resource_type;
        switch (f1_srs_resource->resource_type.present) {
          case F1AP_RESOURCE_TYPE_PR_NOTHING:
            res_type->present = NRPPA_RESOURCE_TYPE_PR_NOTHING;
            break;
          case F1AP_RESOURCE_TYPE_PR_PERIODIC:
            res_type->present = NRPPA_RESOURCE_TYPE_PR_PERIODIC;
            res_type->choice.periodic.periodicity =
                (nrppa_srs_resource_type_periodicity_pr)f1_res_type->choice.periodic.periodicity;
            res_type->choice.periodic.offset = f1_res_type->choice.periodic.offset;
            break;
          case F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT:
            res_type->present = NRPPA_RESOURCE_TYPE_PR_SEMI_PERSISTENT;
            res_type->choice.semi_persistent.periodicity =
                (nrppa_srs_resource_type_periodicity_pr)f1_res_type->choice.semi_persistent.periodicity;
            res_type->choice.semi_persistent.offset = f1_res_type->choice.semi_persistent.offset;
            break;
          case F1AP_RESOURCE_TYPE_PR_APERIODIC:
            res_type->present = NRPPA_RESOURCE_TYPE_PR_APERIODIC;
            res_type->choice.aperiodic = f1_res_type->choice.aperiodic;
            break;
          default:
            AssertFatal(false, "illegal resourceType %d\n", f1_res_type->present);
            break;
        }
        srs_resource->sequence_id = f1_srs_resource->sequence_id;
      }
    }

    // optional: srs_resource_set_list
    if (f1_srs_config->srs_resource_set_list) {
      f1ap_srs_resource_set_list_t *f1_srs_resource_set_list = f1_srs_config->srs_resource_set_list;

      srs_config->srs_resource_set_list = calloc_or_fail(1, sizeof(*srs_config->srs_resource_set_list));

      nrppa_srs_resource_set_list_t *srs_resource_set_list = srs_config->srs_resource_set_list;
      uint32_t srs_resource_set_list_length = f1_srs_resource_set_list->srs_resource_set_list_length;

      srs_resource_set_list->srs_resource_set_list_length = srs_resource_set_list_length;
      srs_resource_set_list->srs_resource_set =
          calloc_or_fail(srs_resource_set_list_length, sizeof(*srs_resource_set_list->srs_resource_set));

      for (int i = 0; i < srs_resource_set_list_length; i++) {
        nrppa_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
        f1ap_srs_resource_set_t *f1_srs_resource_set = &f1_srs_resource_set_list->srs_resource_set[i];

        srs_resource_set->srs_resource_set_id = f1_srs_resource_set->srs_resource_set_id;

        uint8_t srs_resource_id_list_length = f1_srs_resource_set->srs_resource_id_list.srs_resource_id_list_length;
        srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
        srs_resource_set->srs_resource_id_list.srs_resource_id =
            calloc_or_fail(srs_resource_id_list_length, sizeof(*srs_resource_set->srs_resource_id_list.srs_resource_id));

        for (int j = 0; j < srs_resource_id_list_length; j++) {
          srs_resource_set->srs_resource_id_list.srs_resource_id[j] = f1_srs_resource_set->srs_resource_id_list.srs_resource_id[j];
        }

        f1ap_resource_set_type_t *f1_res_set_type = &f1_srs_resource_set->resource_set_type;
        nrppa_resource_set_type_t *res_set_type = &srs_resource_set->resource_set_type;
        switch (f1_res_set_type->present) {
          case F1AP_RESOURCE_SET_TYPE_PR_NOTHING:
            res_set_type->present = NRPPA_RESOURCE_SET_TYPE_PR_NOTHING;
            break;
          case F1AP_RESOURCE_SET_TYPE_PR_PERIODIC:
            res_set_type->present = NRPPA_RESOURCE_SET_TYPE_PR_PERIODIC;
            res_set_type->choice.periodic = f1_res_set_type->choice.periodic;
            break;
          case F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
            res_set_type->present = NRPPA_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
            res_set_type->choice.semi_persistent = f1_res_set_type->choice.semi_persistent;
            break;
          case F1AP_RESOURCE_SET_TYPE_PR_APERIODIC:
            res_set_type->present = NRPPA_RESOURCE_SET_TYPE_PR_APERIODIC;
            res_set_type->choice.aperiodic.srs_resource_trigger = f1_res_set_type->choice.aperiodic.srs_resource_trigger;
            res_set_type->choice.aperiodic.slot_offset = f1_res_set_type->choice.aperiodic.slot_offset;
            break;
          default:
            AssertFatal(false, "illegal resource set type %d\n", f1_res_set_type->present);
            break;
        }
      }
    }
  }

  return srs_configuration;
}

static f1ap_subcarrier_spacing_pr nrppa2f1ap_subcarrier_spacing(const nrppa_subcarrier_spacing_pr scs)
{
  switch (scs) {
    case NRPPA_SUBCARRIER_SPACING_15KHZ:
      return F1AP_SUBCARRIER_SPACING_15KHZ;
    case NRPPA_SUBCARRIER_SPACING_30KHZ:
      return F1AP_SUBCARRIER_SPACING_30KHZ;
    case NRPPA_SUBCARRIER_SPACING_60KHZ:
      return F1AP_SUBCARRIER_SPACING_60KHZ;
    case NRPPA_SUBCARRIER_SPACING_120KHZ:
      return F1AP_SUBCARRIER_SPACING_120KHZ;
    default:
      AssertFatal(false, "Illegal Subcarrier Spacing\n");
      break;
  }
}

static f1ap_srs_configuration_t cp_nrppa_to_f1ap_srs_configuration(const nrppa_srs_configuration_t *in)
{
  f1ap_srs_configuration_t f1_srs_configuration = {0};
  f1ap_srs_carrier_list_t *f1_srs_carrier_list = &f1_srs_configuration.srs_carrier_list;

  const nrppa_srs_carrier_list_t *srs_carrier_list = &in->srs_carrier_list;

  uint32_t srs_carrier_list_length = srs_carrier_list->srs_carrier_list_length;
  f1_srs_carrier_list->srs_carrier_list_length = srs_carrier_list_length;

  if (srs_carrier_list_length > 0) {
    f1_srs_carrier_list->srs_carrier_list_item =
        calloc_or_fail(srs_carrier_list_length, sizeof(*f1_srs_carrier_list->srs_carrier_list_item));
  }

  for (int l = 0; l < srs_carrier_list_length; l++) {
    nrppa_srs_carrier_list_item_t *item = &srs_carrier_list->srs_carrier_list_item[l];
    f1ap_srs_carrier_list_item_t *f1_item = &f1_srs_carrier_list->srs_carrier_list_item[l];

    // pointA
    f1_item->pointA = item->pointA;

    // Uplink Channel BW-PerSCS-List
    nrppa_uplink_channel_bw_per_scs_list_t *ul_bw_list = &item->uplink_channel_bw_per_scs_list;
    f1ap_uplink_channel_bw_per_scs_list_t *f1_ul_bw_list = &f1_item->uplink_channel_bw_per_scs_list;

    uint32_t scs_specific_carrier_list_length = ul_bw_list->scs_specific_carrier_list_length;
    f1_ul_bw_list->scs_specific_carrier_list_length = scs_specific_carrier_list_length;

    if (scs_specific_carrier_list_length > 0) {
      f1_ul_bw_list->scs_specific_carrier =
          calloc_or_fail(scs_specific_carrier_list_length, sizeof(*f1_ul_bw_list->scs_specific_carrier));
    }

    for (int i = 0; i < scs_specific_carrier_list_length; i++) {
      nrppa_scs_specific_carrier_t *nrppa_scs = &ul_bw_list->scs_specific_carrier[i];
      f1ap_scs_specific_carrier_t *f1_scs = &f1_ul_bw_list->scs_specific_carrier[i];

      // offset to carrier
      f1_scs->offset_to_carrier = nrppa_scs->offset_to_carrier;

      // subcarrier spacing
      f1_scs->subcarrier_spacing = nrppa2f1ap_subcarrier_spacing(nrppa_scs->subcarrier_spacing);

      // carrier bandwidth
      f1_scs->carrier_bandwidth = nrppa_scs->carrier_bandwidth;
    }

    // Active UL BWP
    nrppa_active_ul_bwp_t *active_ul_bwp = &item->active_ul_bwp;
    f1ap_active_ul_bwp_t *f1_active_ul_bwp = &f1_item->active_ul_bwp;

    // location and bandwidth
    f1_active_ul_bwp->location_and_bandwidth = active_ul_bwp->location_and_bandwidth;

    // subcarrier spacing
    f1_active_ul_bwp->subcarrier_spacing = nrppa2f1ap_subcarrier_spacing(active_ul_bwp->subcarrier_spacing);

    if (active_ul_bwp->cyclic_prefix) {
      f1_active_ul_bwp->cyclic_prefix = F1AP_CP_TYPE_EXTENDED;
    } else {
      f1_active_ul_bwp->cyclic_prefix = F1AP_CP_TYPE_NORMAL;
    }

    f1_active_ul_bwp->tx_direct_current_location = active_ul_bwp->tx_direct_current_location;

    nrppa_srs_config_t *srs_config = &active_ul_bwp->srs_config;
    f1ap_srs_config_t *f1_srs_config = &f1_active_ul_bwp->srs_config;

    // optional: srs_resource_list
    if (srs_config->srs_resource_list) {
      nrppa_srs_resource_list_t *srs_resource_list = srs_config->srs_resource_list;

      f1_srs_config->srs_resource_list = calloc_or_fail(1, sizeof(*f1_srs_config->srs_resource_list));
      f1ap_srs_resource_list_t *f1_srs_resource_list = f1_srs_config->srs_resource_list;

      uint32_t srs_resource_list_length = srs_resource_list->srs_resource_list_length;

      f1_srs_resource_list->srs_resource_list_length = srs_resource_list_length;
      f1_srs_resource_list->srs_resource = calloc_or_fail(srs_resource_list_length, sizeof(*f1_srs_resource_list->srs_resource));

      for (int i = 0; i < srs_resource_list_length; i++) {
        nrppa_srs_resource_t *srs_resource = &srs_resource_list->srs_resource[i];
        f1ap_srs_resource_t *f1_srs_resource = &f1_srs_resource_list->srs_resource[i];

        f1_srs_resource->srs_resource_id = srs_resource->srs_resource_id;
        switch (srs_resource->nr_of_srs_ports) {
          case NRPPA_SRS_NUMBER_OF_PORTS_N1:
            f1_srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N1;
            break;
          case NRPPA_SRS_NUMBER_OF_PORTS_N2:
            f1_srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N2;
            break;
          case NRPPA_SRS_NUMBER_OF_PORTS_N4:
            f1_srs_resource->nr_of_srs_ports = F1AP_SRS_NUMBER_OF_PORTS_N4;
            break;
          default:
            AssertFatal(false, "Illegal number of ports %d\n", srs_resource->nr_of_srs_ports);
            break;
        }

        f1ap_transmission_comb_t *f1_srs_tx_comb = &f1_srs_resource->transmission_comb;
        nrppa_transmission_comb_t *srs_tx_comb = &srs_resource->transmission_comb;

        switch (srs_tx_comb->present) {
          case NRPPA_TRANSMISSION_COMB_PR_NOTHING:
            f1_srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_NOTHING;
            break;
          case NRPPA_TRANSMISSION_COMB_PR_N2:
            f1_srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_N2;
            f1_srs_tx_comb->choice.n2.comb_offset_n2 = srs_tx_comb->choice.n2.comb_offset_n2;
            f1_srs_tx_comb->choice.n2.cyclic_shift_n2 = srs_tx_comb->choice.n2.cyclic_shift_n2;
            break;
          case NRPPA_TRANSMISSION_COMB_PR_N4:
            f1_srs_tx_comb->present = F1AP_TRANSMISSION_COMB_PR_N4;
            f1_srs_tx_comb->choice.n4.comb_offset_n4 = srs_tx_comb->choice.n4.comb_offset_n4;
            f1_srs_tx_comb->choice.n4.cyclic_shift_n4 = srs_tx_comb->choice.n4.cyclic_shift_n4;
            break;
          default:
            AssertFatal(false, "illegal transmissionComb %d\n", srs_tx_comb->present);
            break;
        }

        f1_srs_resource->start_position = srs_resource->start_position;
        switch (srs_resource->nr_of_symbols) {
          case NRPPA_SRS_NUMBER_OF_SYMBOLS_N1:
            f1_srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N1;
            break;
          case NRPPA_SRS_NUMBER_OF_SYMBOLS_N2:
            f1_srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N2;
            break;
          case NRPPA_SRS_NUMBER_OF_SYMBOLS_N4:
            f1_srs_resource->nr_of_symbols = F1AP_SRS_NUMBER_OF_SYMBOLS_N4;
            break;
          default:
            AssertFatal(false, "illegal number of symbols %d\n", srs_resource->nr_of_symbols);
            break;
        }
        switch (srs_resource->repetition_factor) {
          case NRPPA_SRS_REPETITION_FACTOR_RF1:
            f1_srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF1;
            break;
          case NRPPA_SRS_REPETITION_FACTOR_RF2:
            f1_srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF2;
            break;
          case NRPPA_SRS_REPETITION_FACTOR_RF4:
            f1_srs_resource->repetition_factor = F1AP_SRS_REPETITION_FACTOR_RF4;
            break;
          default:
            AssertFatal(false, "illegal repetition factor %d\n", srs_resource->repetition_factor);
            break;
        }
        f1_srs_resource->freq_domain_position = srs_resource->freq_domain_position;
        f1_srs_resource->freq_domain_shift = srs_resource->freq_domain_shift;
        f1_srs_resource->c_srs = srs_resource->c_srs;
        f1_srs_resource->b_srs = srs_resource->b_srs;
        f1_srs_resource->b_hop = srs_resource->b_hop;
        switch (srs_resource->group_or_sequence_hopping) {
          case NRPPA_GROUPORSEQUENCEHOPPING_NOTHING:
            f1_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_NOTHING;
            break;
          case NRPPA_GROUPORSEQUENCEHOPPING_GROUPHOPPING:
            f1_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_GROUPHOPPING;
            break;
          case NRPPA_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING:
            f1_srs_resource->group_or_sequence_hopping = F1AP_GROUPORSEQUENCEHOPPING_SEQUENCEHOPPING;
            break;
          default:
            AssertFatal(false, "illegal group or sequence hopping %d\n", srs_resource->group_or_sequence_hopping);
            break;
        }

        f1ap_resource_type_t *f1_res_type = &f1_srs_resource->resource_type;
        nrppa_resource_type_t *res_type = &srs_resource->resource_type;

        switch (res_type->present) {
          case NRPPA_RESOURCE_TYPE_PR_NOTHING:
            f1_res_type->present = F1AP_RESOURCE_TYPE_PR_NOTHING;
            break;
          case NRPPA_RESOURCE_TYPE_PR_PERIODIC:
            f1_res_type->present = F1AP_RESOURCE_TYPE_PR_PERIODIC;
            f1_res_type->choice.periodic.periodicity = (f1ap_srs_resource_type_periodicity_pr)res_type->choice.periodic.periodicity;
            f1_res_type->choice.periodic.offset = res_type->choice.periodic.offset;
            break;
          case NRPPA_RESOURCE_TYPE_PR_SEMI_PERSISTENT:
            f1_res_type->present = F1AP_RESOURCE_TYPE_PR_SEMI_PERSISTENT;
            f1_res_type->choice.semi_persistent.periodicity =
                (f1ap_srs_resource_type_periodicity_pr)res_type->choice.semi_persistent.periodicity;
            f1_res_type->choice.semi_persistent.offset = res_type->choice.semi_persistent.offset;
            break;
          case NRPPA_RESOURCE_TYPE_PR_APERIODIC:
            f1_res_type->present = F1AP_RESOURCE_TYPE_PR_APERIODIC;
            f1_res_type->choice.aperiodic = res_type->choice.aperiodic;
            break;
          default:
            AssertFatal(false, "illegal resourceType %d\n", res_type->present);
            break;
        }

        f1_srs_resource->sequence_id = srs_resource->sequence_id;
      }
    }

    // optional: srs_resource_set_list
    if (srs_config->srs_resource_set_list) {
      nrppa_srs_resource_set_list_t *srs_resource_set_list = srs_config->srs_resource_set_list;

      f1_srs_config->srs_resource_set_list = calloc_or_fail(1, sizeof(*f1_srs_config->srs_resource_set_list));
      f1ap_srs_resource_set_list_t *f1_srs_resource_set_list = f1_srs_config->srs_resource_set_list;

      uint32_t srs_resource_set_list_length = srs_resource_set_list->srs_resource_set_list_length;

      f1_srs_resource_set_list->srs_resource_set_list_length = srs_resource_set_list_length;
      f1_srs_resource_set_list->srs_resource_set =
          calloc_or_fail(srs_resource_set_list_length, sizeof(*f1_srs_resource_set_list->srs_resource_set));

      for (int i = 0; i < srs_resource_set_list_length; i++) {
        nrppa_srs_resource_set_t *srs_resource_set = &srs_resource_set_list->srs_resource_set[i];
        f1ap_srs_resource_set_t *f1_srs_resource_set = &f1_srs_resource_set_list->srs_resource_set[i];

        f1_srs_resource_set->srs_resource_set_id = srs_resource_set->srs_resource_set_id;

        uint8_t srs_resource_id_list_length = srs_resource_set->srs_resource_id_list.srs_resource_id_list_length;
        f1_srs_resource_set->srs_resource_id_list.srs_resource_id_list_length = srs_resource_id_list_length;
        f1_srs_resource_set->srs_resource_id_list.srs_resource_id =
            calloc_or_fail(srs_resource_id_list_length, sizeof(*f1_srs_resource_set->srs_resource_id_list.srs_resource_id));

        for (int j = 0; j < srs_resource_id_list_length; j++) {
          f1_srs_resource_set->srs_resource_id_list.srs_resource_id[j] = srs_resource_set->srs_resource_id_list.srs_resource_id[j];
        }

        f1ap_resource_set_type_t *f1_res_set_type = &f1_srs_resource_set->resource_set_type;
        nrppa_resource_set_type_t *res_set_type = &srs_resource_set->resource_set_type;

        switch (res_set_type->present) {
          case NRPPA_RESOURCE_SET_TYPE_PR_NOTHING:
            f1_res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_NOTHING;
            break;
          case NRPPA_RESOURCE_SET_TYPE_PR_PERIODIC:
            f1_res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_PERIODIC;
            f1_res_set_type->choice.periodic = res_set_type->choice.periodic;
            break;
          case NRPPA_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT:
            f1_res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_SEMI_PERSISTENT;
            f1_res_set_type->choice.semi_persistent = res_set_type->choice.semi_persistent;
            break;
          case NRPPA_RESOURCE_SET_TYPE_PR_APERIODIC:
            f1_res_set_type->present = F1AP_RESOURCE_SET_TYPE_PR_APERIODIC;
            f1_res_set_type->choice.aperiodic.srs_resource_trigger = res_set_type->choice.aperiodic.srs_resource_trigger;
            f1_res_set_type->choice.aperiodic.slot_offset = res_set_type->choice.aperiodic.slot_offset;
            break;
          default:
            AssertFatal(false, "illegal resource set type %d\n", res_set_type->present);
            break;
        }
      }
    }
  }

  return f1_srs_configuration;
}

static nrppa_measurement_response_list_t cp_f1ap_to_nrppa_measurement_result_list(f1ap_pos_measurement_result_list_t *in)
{
  nrppa_measurement_response_list_t resp_list = {0};
  uint32_t res_list_len = in->pos_measurement_result_list_length;
  resp_list.measurement_response_list_length = res_list_len;
  resp_list.measurement_response_item = calloc_or_fail(res_list_len, sizeof(*resp_list.measurement_response_item));

  for (int i = 0; i < res_list_len; i++) {
    nrppa_measurement_response_item_t *resp_item = &resp_list.measurement_response_item[i];
    f1ap_pos_measurement_result_list_item_t *f1_res_item = &in->pos_measurement_result_list_item[i];

    resp_item->trp_id = f1_res_item->trp_id;

    nrppa_measurement_result_t *meas_res = &resp_item->measurement_result;
    f1ap_pos_measurement_result_t *f1_meas_res = &f1_res_item->pos_measurement_result;
    uint32_t meas_res_item_len = f1_meas_res->pos_measurement_result_item_length;
    meas_res->measurement_result_item_length = meas_res_item_len;
    meas_res->measurement_result_item = calloc_or_fail(meas_res_item_len, sizeof(*meas_res->measurement_result_item));

    for (int j = 0; j < meas_res_item_len; j++) {
      nrppa_measurement_result_item_t *res_item = &meas_res->measurement_result_item[j];
      f1ap_pos_measurement_result_item_t *f1_res_item = &f1_meas_res->pos_measurement_result_item[j];
      nrppa_measured_results_value_t *res_value = &res_item->measured_results_value;
      f1ap_measured_results_value_t *f1_res_value = &f1_res_item->measured_results_value;

      switch (f1_res_value->present) {
        case F1AP_MEASURED_RESULTS_VALUE_PR_NOTHING:
          res_value->present = NRPPA_MEASURED_RESULTS_VALUE_PR_NOTHING;
          break;
        case F1AP_MEASURED_RESULTS_VALUE_PR_UL_ANGLEOFARRIVAL:
          res_value->present = NRPPA_MEASURED_RESULTS_VALUE_PR_UL_ANGLEOFARRIVAL;
          nrppa_ul_aoa_t *ul_aoa = &res_value->choice.ul_angle_of_arrival;
          f1ap_ul_aoa_t *f1_ul_aoa = &f1_res_value->choice.ul_angle_of_arrival;
          ul_aoa->azimuth_aoa = f1_ul_aoa->azimuth_aoa;
          if (f1_ul_aoa->zenith_aoa) {
            ul_aoa->zenith_aoa = calloc_or_fail(1, sizeof(*ul_aoa->zenith_aoa));
            *ul_aoa->zenith_aoa = *f1_ul_aoa->zenith_aoa;
          }
          if (f1_ul_aoa->lcs_to_gcs_translation_aoa) {
            ul_aoa->lcs_to_gcs_translation_aoa = calloc_or_fail(1, sizeof(*ul_aoa->lcs_to_gcs_translation_aoa));
            ul_aoa->lcs_to_gcs_translation_aoa->alpha = f1_ul_aoa->lcs_to_gcs_translation_aoa->alpha;
            ul_aoa->lcs_to_gcs_translation_aoa->beta = f1_ul_aoa->lcs_to_gcs_translation_aoa->beta;
            ul_aoa->lcs_to_gcs_translation_aoa->gamma = f1_ul_aoa->lcs_to_gcs_translation_aoa->gamma;
          }
          break;
        case F1AP_MEASURED_RESULTS_VALUE_PR_UL_SRS_RSRP:
          res_value->present = NRPPA_MEASURED_RESULTS_VALUE_PR_UL_SRS_RSRP;
          res_value->choice.ul_srs_rsrp = f1_res_value->choice.ul_srs_rsrp;
          break;
        case F1AP_MEASURED_RESULTS_VALUE_PR_UL_RTOA:
          res_value->present = NRPPA_MEASURED_RESULTS_VALUE_PR_UL_RTOA;
          nrppa_ul_rtoa_measurement_t *ul_rtoa = &res_value->choice.ul_rtoa;
          f1ap_ul_rtoa_measurement_item_t *f1_ul_rtoa = &f1_res_value->choice.ul_rtoa.ul_rtoa_measurement_item;
          switch (f1_ul_rtoa->present) {
            case F1AP_ULRTOAMEAS_PR_NOTHING:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_NOTHING;
              break;
            case F1AP_ULRTOAMEAS_PR_K0:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K0;
              ul_rtoa->choice.k0 = f1_ul_rtoa->choice.k0;
              break;
            case F1AP_ULRTOAMEAS_PR_K1:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K1;
              ul_rtoa->choice.k1 = f1_ul_rtoa->choice.k1;
              break;
            case F1AP_ULRTOAMEAS_PR_K2:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K2;
              ul_rtoa->choice.k2 = f1_ul_rtoa->choice.k2;
              break;
            case F1AP_ULRTOAMEAS_PR_K3:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K3;
              ul_rtoa->choice.k3 = f1_ul_rtoa->choice.k3;
              break;
            case F1AP_ULRTOAMEAS_PR_K4:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K4;
              ul_rtoa->choice.k4 = f1_ul_rtoa->choice.k4;
              break;
            case F1AP_ULRTOAMEAS_PR_K5:
              ul_rtoa->present = NRPPA_ULRTOAMEAS_PR_K5;
              ul_rtoa->choice.k5 = f1_ul_rtoa->choice.k5;
              break;
            default:
              AssertFatal(false, "Illegal UL RTOA Measurement\n");
              break;
          }
          break;
        case F1AP_MEASURED_RESULTS_VALUE_PR_GNB_RXTXTIMEDIFF:
          res_value->present = NRPPA_MEASURED_RESULTS_VALUE_PR_GNB_RXTXTIMEDIFF;
          nrppa_gnb_rx_tx_time_diff_t *gnb_rx_tx_time_diff = &res_value->choice.gnb_rx_tx_time_diff;
          f1ap_gnb_rx_tx_time_diff_meas_t *f1_gnb_rx_tx_time_diff = &f1_res_value->choice.gnb_rx_tx_time_diff.rx_tx_time_diff;
          switch (f1_gnb_rx_tx_time_diff->present) {
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_NOTHING:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_NOTHING;
              gnb_rx_tx_time_diff->choice.k0 = f1_gnb_rx_tx_time_diff->choice.k0;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K0:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K0;
              gnb_rx_tx_time_diff->choice.k0 = f1_gnb_rx_tx_time_diff->choice.k0;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K1:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K1;
              gnb_rx_tx_time_diff->choice.k1 = f1_gnb_rx_tx_time_diff->choice.k1;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K2:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K2;
              gnb_rx_tx_time_diff->choice.k2 = f1_gnb_rx_tx_time_diff->choice.k2;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K3:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K3;
              gnb_rx_tx_time_diff->choice.k3 = f1_gnb_rx_tx_time_diff->choice.k3;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K4:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K4;
              gnb_rx_tx_time_diff->choice.k4 = f1_gnb_rx_tx_time_diff->choice.k4;
              break;
            case F1AP_GNBRXTXTIMEDIFFMEAS_PR_K5:
              gnb_rx_tx_time_diff->present = NRPPA_GNBRXTXTIMEDIFFMEAS_PR_K5;
              gnb_rx_tx_time_diff->choice.k5 = f1_gnb_rx_tx_time_diff->choice.k5;
              break;
            default:
              AssertFatal(false, "Illegal GNB RX TX Measurement\n");
              break;
          }
          break;
        default:
          AssertFatal(false, "Illegal Measurement Result Value\n");
          break;
      }
      nrppa_time_stamp_t *time_stamp = &res_item->time_stamp;
      f1ap_time_stamp_t *f1_time_stamp = &f1_res_item->time_stamp;
      time_stamp->system_frame_number = f1_time_stamp->system_frame_number;
      nrppa_time_stamp_slot_index_t *slot_index = &time_stamp->slot_index;
      f1ap_time_stamp_slot_index_t *f1_slot_index = &f1_time_stamp->slot_index;
      switch (f1_slot_index->present) {
        case F1AP_TIME_STAMP_SLOT_INDEX_PR_NOTHING:
          slot_index->present = NRPPA_TIME_STAMP_SLOT_INDEX_PR_NOTHING;
          break;
        case F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_15:
          slot_index->present = NRPPA_TIME_STAMP_SLOT_INDEX_PR_SCS_15;
          slot_index->choice.scs_15 = f1_slot_index->choice.scs_15;
          break;
        case F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_30:
          slot_index->present = NRPPA_TIME_STAMP_SLOT_INDEX_PR_SCS_30;
          slot_index->choice.scs_30 = f1_slot_index->choice.scs_30;
          break;
        case F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_60:
          slot_index->present = NRPPA_TIME_STAMP_SLOT_INDEX_PR_SCS_60;
          slot_index->choice.scs_60 = f1_slot_index->choice.scs_60;
          break;
        case F1AP_TIME_STAMP_SLOT_INDEX_PR_SCS_120:
          slot_index->present = NRPPA_TIME_STAMP_SLOT_INDEX_PR_SCS_120;
          slot_index->choice.scs_120 = f1_slot_index->choice.scs_120;
          break;
        default:
          AssertFatal(false, "Illegal Time Stamp Slot Index\n");
          break;
      }
    }
  }

  return resp_list;
}

static nrppa_measurement_resp_t f1ap2nrppa_cp_measurement_response(f1ap_positioning_measurement_resp_t *f1ap_msg)
{
  nrppa_measurement_resp_t nrppa_msg = {0};
  nrppa_msg.transaction_id = f1ap_msg->transaction_id;
  nrppa_msg.lmf_measurement_id = f1ap_msg->lmf_measurement_id;
  nrppa_msg.ran_measurement_id = f1ap_msg->ran_measurement_id;
  if (f1ap_msg->pos_measurement_result_list) {
    nrppa_msg.measurement_response_list = calloc_or_fail(1, sizeof(*nrppa_msg.measurement_response_list));
    *nrppa_msg.measurement_response_list = cp_f1ap_to_nrppa_measurement_result_list(f1ap_msg->pos_measurement_result_list);
  }
  return nrppa_msg;
}

void rrc_gNB_process_trp_information_request(gNB_RRC_INST *rrc, const nrppa_trp_information_req_t *msg)
{
  f1ap_trp_information_req_t f1ap_msg = {0};
  f1ap_msg.transaction_id = msg->transaction_id;

  if (msg->has_trp_list) {
    f1ap_msg.has_trp_list = true;
    uint32_t list_len = msg->trp_list.trp_list_length;
    f1ap_msg.trp_list.trp_list_length = list_len;
    f1ap_msg.trp_list.trp_list_item = calloc_or_fail(list_len, sizeof(*f1ap_msg.trp_list.trp_list_item));
    f1ap_trp_list_item_t *f1ap_item = f1ap_msg.trp_list.trp_list_item;
    const nrppa_trp_list_item_t *nrppa_item = msg->trp_list.trp_list_item;
    for (int i = 0; i < list_len; i++) {
      f1ap_item[i].trp_id = nrppa_item[i].trp_id;
    }
  }

  const nrppa_trp_information_type_list_t *nrppa_info_type_list = &msg->trp_information_type_list;
  f1ap_trp_information_type_list_t *f1ap_info_type_list = &f1ap_msg.trp_information_type_list;
  uint8_t info_type_list_len = nrppa_info_type_list->trp_information_type_list_length;
  DevAssert(info_type_list_len >= 0);
  if (info_type_list_len > 0) {
    f1ap_info_type_list->trp_information_type_list_length = info_type_list_len;
    f1ap_info_type_list->trp_information_type_item =
        calloc_or_fail(info_type_list_len, sizeof(*f1ap_info_type_list->trp_information_type_item));
    nrppa_trp_information_type_item_pr *nrppa_info_item = nrppa_info_type_list->trp_information_type_item;
    f1ap_trp_information_type_item_pr *f1ap_info_item = f1ap_info_type_list->trp_information_type_item;
    for (int i = 0; i < info_type_list_len; i++) {
      switch (nrppa_info_item[i]) {
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_PCI:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_NR_PCI;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_NG_RAN_CGI:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_NG_RAN_CGI;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_NR_ARFCN:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_NR_ARFCN;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_PRS_CONFIG:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_PRS_CONFIG;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_SSB_CONFIG:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_SSB_CONFIG;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_SFN_INIT_TIME:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_SFN_INIT_TIME;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_SPATIAL_DIRECTION_INFO:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_SPATIAL_DIRECTION_INFO;
          break;
        case NRPPA_TRP_INFORMATION_TYPE_ITEM_GEO_COORDINATES:
          f1ap_info_item[i] = F1AP_TRP_INFORMATION_TYPE_ITEM_GEO_COORDINATES;
          break;
        default:
          AssertFatal(false, "Illegal TRP Information Type\n");
          break;
      }
    }
  } else {
    // HACK: Made to work with OAI
    // We fill NG_RAN_CGI and GEO_COORDINATES as default
    info_type_list_len = 2;
    f1ap_info_type_list->trp_information_type_list_length = info_type_list_len;
    f1ap_info_type_list->trp_information_type_item =
        calloc_or_fail(info_type_list_len, sizeof(*f1ap_info_type_list->trp_information_type_item));
    f1ap_trp_information_type_item_pr *f1ap_info_item = f1ap_info_type_list->trp_information_type_item;
    f1ap_info_item[0] = F1AP_TRP_INFORMATION_TYPE_ITEM_NG_RAN_CGI;
    f1ap_info_item[1] = F1AP_TRP_INFORMATION_TYPE_ITEM_GEO_COORDINATES;
  }

  rrc_send_trp_information_request_to_dus(rrc, &f1ap_msg);
  free_trp_information_req(&f1ap_msg);
}

void rrc_CU_process_trp_information_response(f1ap_trp_information_resp_t *f1ap_msg)
{
  MessageDef *msg_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_TRP_INFORMATION_RESP);
  nrppa_trp_information_resp_t *nrppa_msg = &NRPPA_TRP_INFORMATION_RESP(msg_resp);
  nrppa_msg->transaction_id = f1ap_msg->transaction_id;

  uint32_t trp_info_item_length = f1ap_msg->trp_information_list.trp_information_item_length;
  AssertFatal(trp_info_item_length > 0, "at least 1 TRP Information Item must be present\n");
  nrppa_msg->trp_information_list.trp_information_item_length = trp_info_item_length;
  nrppa_msg->trp_information_list.trp_information_item =
      calloc_or_fail(trp_info_item_length, sizeof(*nrppa_msg->trp_information_list.trp_information_item));

  nrppa_trp_information_t *trp_information_item_nrppa = nrppa_msg->trp_information_list.trp_information_item;
  f1ap_trp_information_t *trp_information_item_f1ap = f1ap_msg->trp_information_list.trp_information_item;

  for (int i = 0; i < trp_info_item_length; i++) {
    uint8_t trp_info_type_resp_item_len =
        trp_information_item_f1ap[i].trp_information_type_response_list.trp_information_type_response_item_length;
    AssertFatal(trp_info_type_resp_item_len > 0, "at least 1 TRP Information type response Item must be present\n");

    trp_information_item_nrppa[i].trp_id = trp_information_item_f1ap[i].trp_id;

    nrppa_trp_information_type_response_list_t *trp_info_type_resp_list_nrppa =
        &trp_information_item_nrppa[i].trp_information_type_response_list;
    f1ap_trp_information_type_response_list_t *trp_info_type_resp_list_f1ap =
        &trp_information_item_f1ap[i].trp_information_type_response_list;
    trp_info_type_resp_list_nrppa->trp_information_type_response_item_length = trp_info_type_resp_item_len;
    trp_info_type_resp_list_nrppa->trp_information_type_response_item =
        calloc_or_fail(trp_info_type_resp_item_len, sizeof(*trp_info_type_resp_list_nrppa->trp_information_type_response_item));
    for (int j = 0; j < trp_info_type_resp_item_len; j++) {
      trp_info_type_resp_list_nrppa->trp_information_type_response_item[j] =
          f1ap2nrppa_trp_info_type_response_item(&trp_info_type_resp_list_f1ap->trp_information_type_response_item[j]);
    }
  }

  LOG_I(NR_RRC, "Sending NRPPA_TRP_INFORMATION_RESP to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_resp);
}

void rrc_gNB_process_positioning_information_request(gNB_RRC_INST *rrc, const nrppa_positioning_information_req_t *msg)
{
  f1ap_positioning_information_req_t f1ap_msg = {0};
  nrppa_gNB_ue_context_t *nrppa_ue_context = nrppa_get_ue_context(msg->transaction_id);
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, nrppa_ue_context->gNB_ue_ngap_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", nrppa_ue_context->gNB_ue_ngap_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  RETURN_IF_INVALID_ASSOC_ID(ue_data.du_assoc_id);
  f1ap_msg.gNB_CU_ue_id = UE->rrc_ue_id;
  f1ap_msg.gNB_DU_ue_id = ue_data.secondary_ue;
  rrc->mac_rrc.positioning_information_request(ue_data.du_assoc_id, &f1ap_msg);
}

void rrc_CU_process_positioning_information_response(f1ap_positioning_information_resp_t *f1ap_msg)
{
  MessageDef *msg_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_POSITIONING_INFORMATION_RESP);
  nrppa_positioning_information_resp_t *nrppa_msg = &NRPPA_POSITIONING_INFORMATION_RESP(msg_resp);
  nrppa_gNB_ue_context_t *nrppa_ue_context = nrppa_get_context_by_ue_id(f1ap_msg->gNB_CU_ue_id);
  nrppa_msg->transaction_id = nrppa_ue_context->transaction_id;
  if (f1ap_msg->srs_configuration) {
    nrppa_msg->srs_configuration = calloc_or_fail(1, sizeof(*nrppa_msg->srs_configuration));
    *nrppa_msg->srs_configuration = cp_f1ap_to_nrppa_srs_configuration(f1ap_msg->srs_configuration);
  }
  LOG_I(NR_RRC, "Sending NRPPA_POSITIONING_INFORMATION_RESP to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_resp);
}

void rrc_gNB_process_positioning_activation_request(gNB_RRC_INST *rrc, const nrppa_positioning_activation_req_t *msg)
{
  f1ap_positioning_activation_req_t f1ap_msg = {0};
  nrppa_gNB_ue_context_t *nrppa_ue_context = nrppa_get_ue_context(msg->transaction_id);
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, nrppa_ue_context->gNB_ue_ngap_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", nrppa_ue_context->gNB_ue_ngap_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  RETURN_IF_INVALID_ASSOC_ID(ue_data.du_assoc_id);
  f1ap_msg.gNB_CU_ue_id = UE->rrc_ue_id;
  f1ap_msg.gNB_DU_ue_id = ue_data.secondary_ue;
  const nrppa_srs_type_t *srs_type = &msg->srs_type;
  f1ap_srs_type_t *f1_srs_type = &f1ap_msg.srs_type;
  switch (srs_type->present) {
    case NRPPA_SRS_TYPE_PR_NOTHING:
      f1_srs_type->present = F1AP_SRS_TYPE_PR_NOTHING;
      break;
    case NRPPA_SRS_TYPE_PR_SEMIPERSISTENTSRS:
      f1_srs_type->present = F1AP_SRS_TYPE_PR_SEMIPERSISTENTSRS;
      f1_srs_type->choice.srs_resource_set_id = calloc_or_fail(1, sizeof(*f1_srs_type->choice.srs_resource_set_id));
      *f1_srs_type->choice.srs_resource_set_id = *srs_type->choice.srs_resource_set_id;
      break;
    case NRPPA_SRS_TYPE_PR_APERIODICSRS:
      f1_srs_type->present = F1AP_SRS_TYPE_PR_APERIODICSRS;
      f1_srs_type->choice.aperiodic = calloc_or_fail(1, sizeof(*f1_srs_type->choice.aperiodic));
      *f1_srs_type->choice.aperiodic = *srs_type->choice.aperiodic;
      break;
    default:
      AssertFatal(false, "Illegal SRS Type\n");
      break;
  }
  rrc->mac_rrc.positioning_activation_request(ue_data.du_assoc_id, &f1ap_msg);
  free_positioning_activation_req(&f1ap_msg);
}

void rrc_CU_process_positioning_activation_response(f1ap_positioning_activation_resp_t *f1ap_msg)
{
  MessageDef *msg_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_POSITIONING_ACTIVATION_RESP);
  nrppa_positioning_activation_resp_t *nrppa_msg = &NRPPA_POSITIONING_ACTIVATION_RESP(msg_resp);
  nrppa_gNB_ue_context_t *nrppa_ue_context = nrppa_get_context_by_ue_id(f1ap_msg->gNB_CU_ue_id);
  nrppa_msg->transaction_id = nrppa_ue_context->transaction_id;
  LOG_I(NR_RRC, "Sending NRPPA_POSITIONING_ACTIVATION_RESP to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_resp);
}

void rrc_gNB_process_positioning_measurement_request(gNB_RRC_INST *rrc, const nrppa_measurement_req_t *msg)
{
  f1ap_positioning_measurement_req_t f1ap_msg = {.transaction_id = msg->transaction_id,
                                                 .lmf_measurement_id = msg->lmf_measurement_id};

  // find ran_measurement_id
  f1ap_msg.ran_measurement_id = 1;

  // TRP Measurement Request List
  const nrppa_trp_measurement_request_list_t *trp_m_list = &msg->trp_measurement_request_list;
  f1ap_trp_measurement_request_list_t *f1_trp_m_list = &f1ap_msg.trp_measurement_request_list;
  uint32_t list_len = trp_m_list->trp_measurement_request_list_length;
  if (list_len > 0) {
    f1_trp_m_list->trp_measurement_request_list_length = trp_m_list->trp_measurement_request_list_length;
    f1_trp_m_list->trp_measurement_request_item = calloc_or_fail(list_len, sizeof(*f1_trp_m_list->trp_measurement_request_item));
  }
  for (int i = 0; i < list_len; i++) {
    f1_trp_m_list->trp_measurement_request_item[i].tRPID = trp_m_list->trp_measurement_request_item[i].trp_id;
  }

  // Report Characteristics
  const nrppa_report_characteristics_pr *pos_report = &msg->report_characteristics;
  f1ap_pos_report_characteristics_pr *f1_pos_report = &f1ap_msg.pos_report_characteristics;
  switch (*pos_report) {
    case NRPPA_POSREPORTCHARACTERISTICS_ONDEMAND:
      *f1_pos_report = F1AP_POSREPORTCHARACTERISTICS_ONDEMAND;
      break;
    case NRPPA_POSREPORTCHARACTERISTICS_PERIODIC:
      *f1_pos_report = F1AP_POSREPORTCHARACTERISTICS_PERIODIC;
      break;
    default:
      AssertFatal(false, "Illegal Positioning Report Charateristics\n");
      break;
  }

  // If Report characteristics : Periodic
  if (*pos_report == NRPPA_POSREPORTCHARACTERISTICS_PERIODIC) {
    const nrppa_measurement_periodicity_pr *measurement_periodicity = &msg->measurement_periodicity;
    f1ap_pos_measurement_periodicity_pr *f1_measurement_periodicity = &f1ap_msg.measurement_periodicity;
    switch (*measurement_periodicity) {
      case NRPPA_POSMEASUREMENTPERIODICITY_MS120:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS120;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS240:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS240;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS480:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS480;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS640:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS640;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS1024:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS1024;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS2048:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS2048;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS5120:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS5120;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MS10240:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MS10240;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MIN1:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MIN1;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MIN6:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MIN6;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MIN12:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MIN12;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MIN30:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MIN30;
        break;
      case NRPPA_POSMEASUREMENTPERIODICITY_MIN60:
        *f1_measurement_periodicity = F1AP_POSMEASUREMENTPERIODICITY_MIN60;
        break;
      default:
        AssertFatal(false, "Illegal Measurement Periodicity\n");
        break;
    }
  }

  // Measurement Quantities
  const nrppa_measurement_quantities_t *meas_quantities = &msg->measurement_quantities;
  f1ap_pos_measurement_quantities_t *f1_meas_quantities = &f1ap_msg.pos_measurement_quantities;
  uint32_t q_len = meas_quantities->measurement_quantities_length;
  if (q_len > 0) {
    f1_meas_quantities->pos_measurement_quantities_length = q_len;
    f1_meas_quantities->pos_measurement_quantities_item =
        calloc_or_fail(q_len, sizeof(*f1_meas_quantities->pos_measurement_quantities_item));
  }

  const nrppa_measurement_quantities_item_t *q_item = meas_quantities->measurement_quantities_item;
  f1ap_pos_measurement_quantities_item_t *f1_q_item = f1_meas_quantities->pos_measurement_quantities_item;

  for (int i = 0; i < q_len; i++) {
    const nrppa_measurement_type_pr *meas_type = &q_item[i].measurement_type;
    f1ap_PosMeasurementType_e *f1_meas_type = &f1_q_item[i].pos_measurement_type;
    switch (*meas_type) {
      case NRPPA_POSMEASUREMENTTYPE_GNB_RX_TX:
        *f1_meas_type = F1AP_POSMEASUREMENTTYPE_GNB_RX_TX;
        break;
      case NRPPA_POSMEASUREMENTTYPE_UL_SRS_RSRP:
        *f1_meas_type = F1AP_POSMEASUREMENTTYPE_UL_SRS_RSRP;
        break;
      case NRPPA_POSMEASUREMENTTYPE_UL_AOA:
        *f1_meas_type = F1AP_POSMEASUREMENTTYPE_UL_AOA;
        break;
      case NRPPA_POSMEASUREMENTTYPE_UL_RTOA:
        *f1_meas_type = F1AP_POSMEASUREMENTTYPE_UL_RTOA;
        break;
      default:
        AssertFatal(false, "Illegal Measurement Type\n");
        break;
    }
  }

  // HACK: Made to work with OAI-LMF
  // We fill UL_RTOA as default
  if (q_len == 0) {
    q_len = 1;
    f1_meas_quantities->pos_measurement_quantities_length = q_len;
    f1_meas_quantities->pos_measurement_quantities_item =
        calloc_or_fail(q_len, sizeof(*f1_meas_quantities->pos_measurement_quantities_item));
    f1ap_pos_measurement_quantities_item_t *f1_q_item = f1_meas_quantities->pos_measurement_quantities_item;
    f1_q_item[0].pos_measurement_type = F1AP_POSMEASUREMENTTYPE_UL_RTOA;
  }

  // SRS Configuration (optional)
  if (msg->srs_configuration) {
    f1ap_msg.srs_configuration = calloc_or_fail(1, sizeof(*f1ap_msg.srs_configuration));
    *f1ap_msg.srs_configuration = cp_nrppa_to_f1ap_srs_configuration(msg->srs_configuration);
  }

  // FIX THIS: send to all DUs that match the TRP ids
  rrc_send_positioning_measurement_request_to_dus(rrc, &f1ap_msg);
  free_positioning_measurement_req(&f1ap_msg);
}

void rrc_CU_process_positioning_measurement_response(f1ap_positioning_measurement_resp_t *f1ap_msg)
{
  MessageDef *msg_resp = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_MEASUREMENT_RESP);
  nrppa_measurement_resp_t *nrppa_msg = &NRPPA_MEASUREMENT_RESP(msg_resp);
  *nrppa_msg = f1ap2nrppa_cp_measurement_response(f1ap_msg);
  LOG_I(NR_RRC, "Sending NRPPA_MEASUREMENT_RESP to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_resp);
}

static nrppa_cause_t f1ap2nrppa_encode_cause(f1ap_Cause_t cause, long cause_value)
{
  nrppa_cause_t nrppa_cause = {0};
  switch (cause) {
    case F1AP_CAUSE_NOTHING:
      nrppa_cause.type = NRPPA_CAUSE_NOTHING;
      break;
    case F1AP_CAUSE_RADIO_NETWORK:
      nrppa_cause.type = NRPPA_CAUSE_RADIO_NETWORK;
      break;
    case F1AP_CAUSE_PROTOCOL:
      nrppa_cause.type = NRPPA_CAUSE_PROTOCOL;
      break;
    case F1AP_CAUSE_MISC:
      nrppa_cause.type = NRPPA_CAUSE_MISC;
      break;
    default:
      AssertFatal(false, "Illegal Cause\n");
      break;
  }
  nrppa_cause.value = cause_value;
  return nrppa_cause;
}

void rrc_CU_process_trp_information_failure(f1ap_trp_information_failure_t *f1ap_msg)
{
  MessageDef *msg_fail = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_TRP_INFORMATION_FAILURE);
  nrppa_trp_information_failure_t *nrppa_msg = &NRPPA_TRP_INFORMATION_FAILURE(msg_fail);
  nrppa_msg->transaction_id = f1ap_msg->transaction_id;
  nrppa_msg->cause = f1ap2nrppa_encode_cause(f1ap_msg->cause, f1ap_msg->cause_value);
  LOG_I(NR_RRC, "Sending NRPPA_TRP_INFORMATION_FAILURE to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_fail);
}

void rrc_CU_process_positioning_measurement_failure(f1ap_positioning_measurement_failure_t *f1ap_msg)
{
  MessageDef *msg_fail = itti_alloc_new_message(TASK_RRC_GNB, 0, NRPPA_MEASUREMENT_FAILURE);
  nrppa_measurement_failure_t *nrppa_msg = &NRPPA_MEASUREMENT_FAILURE(msg_fail);
  nrppa_msg->transaction_id = f1ap_msg->transaction_id;
  nrppa_msg->lmf_measurement_id = f1ap_msg->lmf_measurement_id;
  nrppa_msg->cause = f1ap2nrppa_encode_cause(f1ap_msg->cause, f1ap_msg->cause_value);
  LOG_I(NR_RRC, "Sending NRPPA_MEASUREMENT_FAILURE to TASK_NRPPA\n");
  itti_send_msg_to_task(TASK_NRPPA, 0, msg_fail);
}
