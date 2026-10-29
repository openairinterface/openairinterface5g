/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap define procedures for gNB
 */
 
#include <stdint.h>

#include "queue.h"
#include "tree.h"
#include "openair2/COMMON/ngap_messages_types.h"

#ifndef NGAP_GNB_DEFS_H_
#define NGAP_GNB_DEFS_H_

#define NGAP_GNB_NAME_LENGTH_MAX    (150)

typedef enum {
  /* Disconnected state: initial state for any association. */
  NGAP_GNB_STATE_DISCONNECTED = 0x0,
  /* State waiting for S1 Setup response message if gNB is AMF accepted or
   * S1 Setup failure if AMF rejects the gNB.
   */
  NGAP_GNB_STATE_WAITING     = 0x1,
  /* The gNB is successfully connected to AMF, UE contexts can be created. */
  NGAP_GNB_STATE_CONNECTED   = 0x2,
  /* The AMF has sent an overload start message. Once the AMF disables the
   * OVERLOAD marker, the state of the association will be
   * NGAP_GNB_STATE_CONNECTED.
   */
  NGAP_GNB_OVERLOAD          = 0x3,
  /* Max number of states available */
  NGAP_GNB_STATE_MAX,
} ngap_gNB_state_t;

/* If the Overload Action IE in the OVERLOAD START message is set to
 * - “reject all RRC connection establishments for non-emergency mobile
 *    originated data transfer  E(i.e. reject traffic corresponding to RRC cause
 *    “mo-data  E(TS 36.331 [16])), or
 * - “reject all RRC connection establishments for signalling  E(i.e. reject
 *    traffic corresponding to RRC cause “modata Eand “mo-signalling E *    (TS 38.331 [16])),or
 * - “only permit RRC connection establishments for emergency sessions and
 *    mobile terminated services E(i.e. only permit traffic corresponding to RRC
 *    cause “emergency Eand “mt-Access E(TS 36.331 [16])).
 *
 * NOTE: When the Overload Action IE is set to “only permit RRC connection
 * establishments for emergency sessions and mobile terminated services E
 * emergency calls with RRC cause “highPriorityAcess Efrom high priority users
 * are rejected (TS 24.301 [24]).
 */
typedef enum {
  NGAP_OVERLOAD_REJECT_MO_DATA        = 0x0,
  NGAP_OVERLOAD_REJECT_ALL_SIGNALLING = 0x1,
  NGAP_OVERLOAD_ONLY_EMERGENCY_AND_MT = 0x2,
  NGAP_NO_OVERLOAD                    = 0x3,
  NGAP_OVERLOAD_MAX,
} ngap_overload_state_t;

/* ServedGUAMIItem: GUAMI per TS 38.413 §9.3.3.3 */
struct served_guami_s {
  nr_guami_t guami;
  STAILQ_ENTRY(served_guami_s) next;
};

struct ngap_gNB_instance_s;

/* This structure describes association of a gNB to a AMF */
typedef struct ngap_gNB_amf_data_s {
  long t_reconnect; // ITTI timer id (or equivalent)

  /* AMF descriptors tree, ordered by sctp assoc id */
  RB_ENTRY(ngap_gNB_amf_data_s) entry;

  /* This is the optional name provided by the AMF */
  char *amf_name;

  /* AMF NGAP IP address */
  net_ip_address_t amf_s1_ip;

  /* Served GUAMIs from NG Setup Response (items carry GUAMI §9.3.3.3) */
  STAILQ_HEAD(served_guamis_s, served_guami_s) served_guami;

  /* Relative processing capacity of an AMF with respect to the other AMFs
   * in the pool in order to load-balance AMFs within a pool as defined
   * in TS 23.401.
   */
  uint8_t relative_amf_capacity;

  /* Current AMF overload information (if any). */
  ngap_overload_state_t overload_state;
  /* Current gNB->AMF NGAP association state */
  ngap_gNB_state_t state;

  /* Next usable stream for UE signalling */
  int32_t nextstream;

  /* Number of input/ouput streams */
  uint16_t in_streams;
  uint16_t out_streams;

  /* Connexion id used between SCTP/NGAP */
  uint16_t cnx_id;

  /* SCTP association id */
  sctp_assoc_t assoc_id;

  /* This is served PLMN IDs communicated to the AMF via an index over the
   * MCC/MNC array in ngap_gNB_instance */
  uint8_t  broadcast_plmn_num;
  uint8_t broadcast_plmn_index[NR_MAX_NB_PLMNS];

  /* Only meaningfull in virtual mode */
  struct ngap_gNB_instance_s *ngap_gNB_instance;
} ngap_gNB_amf_data_t;

typedef struct ngap_gNB_instance_s {
  /* Next ngap gNB association.
   * Only used for virtual mode.
   */
  STAILQ_ENTRY(ngap_gNB_instance_s) ngap_gNB_entries;

  /* Number of AMF requested by gNB (tree size) */
  uint32_t ngap_amf_nb;
  /* Number of AMF for which association is pending */
  uint32_t ngap_amf_pending_nb;
  /* Number of AMF successfully associated to gNB */
  uint32_t ngap_amf_associated_nb;
  /* Tree of NGAP AMF associations ordered by association ID */
  RB_HEAD(ngap_amf_map, ngap_gNB_amf_data_s) ngap_amf_head;

  /* For virtual mode, mod_id as defined in the rest of the L1/L2 stack */
  instance_t instance;

  /* Displayable name of gNB */
  char *gNB_name;

  /* gNB identifier within a PLMN (22..32 bits) */
  uint32_t gNB_id;
  /* The type of the cell */
  enum cell_type_e cell_type;

  /* Tracking area code */
  uint32_t tac;

  /* gNB NGAP IP address */
  net_ip_address_t gNB_ng_ip;
  
  /* Mobile Country Code
   * Mobile Network Code
   */
  uint8_t num_plmn;
  ngap_plmn_t plmn[NR_MAX_NB_PLMNS];

  /* Default Paging DRX of the gNB as defined in TS 38.304 */
  ngap_paging_drx_t default_drx;
} ngap_gNB_instance_t;

typedef struct {
  /* List of served gNBs
   * Only used for virtual mode
   */
  STAILQ_HEAD(ngap_gNB_instances_head_s, ngap_gNB_instance_s) ngap_gNB_instances_head;
  /* Nb of registered gNBs */
  uint8_t nb_registered_gNBs;

  /* Generate a unique connexion id used between NGAP and SCTP */
  uint16_t global_cnx_id;
} ngap_gNB_internal_data_t;

int ngap_gNB_compare_assoc_id(
  struct ngap_gNB_amf_data_s *p1, struct ngap_gNB_amf_data_s *p2);

/* Generate the tree management functions */
RB_PROTOTYPE(ngap_amf_map, ngap_gNB_amf_data_s, entry,
             ngap_gNB_compare_assoc_id);

#endif /* NGAP_GNB_DEFS_H_ */
