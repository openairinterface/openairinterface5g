/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* XnAP UE ID table (model in xnap_ids.h): one entry per Xn handover, keyed by the local XnAP UE ID
 * from a counter, with an index by rrc_ue_id. Under a mutex, as both the XNAP and the RRC task use it. */

#include "xnap_ids.h"

#include <pthread.h>
#include <stdlib.h>
#include "ds/hashtable.h"
#include "common/utils/assertions.h"
#include "common/utils/utils.h"
#include "common/utils/threadPool/pthread_utils.h"
#include "common/utils/LOG/log.h"

/* Entries keyed on the local XnAP UE ID, plus an index rrc_ue_id -> local XnAP UE ID */
static struct {
  hash_table_t by_xnap_id;
  hash_table_t by_rrc_ue_id;
  pthread_mutex_t mutex;
  uint32_t next_id;
} xn_ues = {.mutex = PTHREAD_MUTEX_INITIALIZER, .next_id = 1};

/* All helpers below expect xn_ues.mutex to be held */
static uint32_t local_id(const xn_ue_data_t *e)
{
  return e->role == XN_ROLE_SOURCE ? e->s_ng_node_ue_xnap_id : e->t_ng_node_ue_xnap_id;
}

static uint32_t peer_id(const xn_ue_data_t *e)
{
  return e->role == XN_ROLE_SOURCE ? e->t_ng_node_ue_xnap_id : e->s_ng_node_ue_xnap_id;
}

static const char *role2str(xn_role_t role)
{
  return role == XN_ROLE_SOURCE ? "source" : "target";
}

/* The tables are created by the XNAP task, which does not run with Xn disabled, but RRC looks up
 * every released UE */
static bool table_created(void)
{
  return xn_ues.by_xnap_id.nodes != NULL;
}

static xn_ue_data_t *table_get(uint32_t xnap_id)
{
  void *data = NULL;
  if (!table_created())
    return NULL;
  return hashtable_get(&xn_ues.by_xnap_id, xnap_id, &data) == HASH_TABLE_OK ? data : NULL;
}

static xn_ue_data_t *table_get_by_rrc_ue_id(uint32_t rrc_ue_id)
{
  void *id = NULL;
  if (!table_created() || hashtable_get(&xn_ues.by_rrc_ue_id, rrc_ue_id, &id) != HASH_TABLE_OK)
    return NULL;
  return table_get(*(uint32_t *)id);
}

/* Entry in role of the handover peer_xnap_id with the peer assoc_id, or NULL */
static xn_ue_data_t *table_get_by_peer_id(xn_role_t role, sctp_assoc_t assoc_id, uint32_t peer_xnap_id)
{
  hash_table_iterator_s it = hashtable_get_iterator(&xn_ues.by_xnap_id);
  xn_ue_data_t *e = NULL;
  while (hashtable_iterator_getnext(&it, (void **)&e))
    if (e->role == role && e->assoc_id == assoc_id && peer_id(e) == peer_xnap_id)
      return e;
  return NULL;
}

static bool table_remove(uint32_t xnap_id)
{
  xn_ue_data_t *e = table_get(xnap_id);
  if (e == NULL)
    return false;
  LOG_D(XNAP,
        "UE %u: remove %s entry s_xnap_ue_id %u t_xnap_ue_id %u assoc_id %d\n",
        e->rrc_ue_id,
        role2str(e->role),
        e->s_ng_node_ue_xnap_id,
        e->t_ng_node_ue_xnap_id,
        e->assoc_id);
  if (e->rrc_ue_id != 0)
    hashtable_remove(&xn_ues.by_rrc_ue_id, e->rrc_ue_id, free);
  hashtable_remove(&xn_ues.by_xnap_id, xnap_id, free);
  return true;
}

/* index the entry xnap_id by rrc_ue_id, which must not be indexed yet */
static void table_index(uint32_t rrc_ue_id, uint32_t xnap_id)
{
  uint32_t *id = malloc_or_fail(sizeof(*id));
  *id = xnap_id;
  hashtable_rc_t rc = hashtable_insert(&xn_ues.by_rrc_ue_id, rrc_ue_id, id, free);
  AssertFatal(rc == HASH_TABLE_OK, "UE %u: cannot index Xn handover\n", rrc_ue_id);
}

/* copy e to out if it exists in role */
static bool table_copy(const xn_ue_data_t *e, xn_role_t role, xn_ue_data_t *out)
{
  if (e == NULL || e->role != role)
    return false;
  *out = *e;
  return true;
}

void xn_init_ue_data(void)
{
  mutexlock(xn_ues.mutex);
  DevAssert(xn_ues.by_xnap_id.nodes == NULL);
  xn_ues.by_xnap_id = hashtable_create(1319, NULL);
  xn_ues.by_rrc_ue_id = hashtable_create(1319, NULL);
  mutexunlock(xn_ues.mutex);
}

uint32_t xn_add_ue(xn_role_t role, uint32_t rrc_ue_id, uint32_t peer_xnap_id, sctp_assoc_t assoc_id)
{
  mutexlock(xn_ues.mutex);
  uint32_t xnap_id = 0;
  /* a repeated HandoverRequest of the same handover gets no second entry */
  if (role == XN_ROLE_SOURCE || table_get_by_peer_id(role, assoc_id, peer_xnap_id) == NULL) {
    xnap_id = xn_ues.next_id++;
    xn_ue_data_t *e = malloc_or_fail(sizeof(*e));
    *e = (xn_ue_data_t){.role = role, .rrc_ue_id = rrc_ue_id, .assoc_id = assoc_id};
    if (role == XN_ROLE_SOURCE) {
      e->s_ng_node_ue_xnap_id = xnap_id;
      e->t_ng_node_ue_xnap_id = peer_xnap_id;
    } else {
      e->s_ng_node_ue_xnap_id = peer_xnap_id;
      e->t_ng_node_ue_xnap_id = xnap_id;
    }
    hashtable_rc_t rc = hashtable_insert(&xn_ues.by_xnap_id, xnap_id, e, free);
    AssertFatal(rc == HASH_TABLE_OK, "XnAP UE ID %u already in use\n", xnap_id);
    if (rrc_ue_id != 0) {
      /* drop an entry left over from an earlier handover of this UE */
      xn_ue_data_t *stale = table_get_by_rrc_ue_id(rrc_ue_id);
      if (stale != NULL) {
        LOG_W(XNAP, "UE %u: dropping stale Xn handover (XnAP UE ID %u)\n", rrc_ue_id, local_id(stale));
        table_remove(local_id(stale));
      }
      table_index(rrc_ue_id, xnap_id);
    }
    LOG_D(XNAP,
          "UE %u: add %s entry s_xnap_ue_id %u t_xnap_ue_id %u assoc_id %d\n",
          rrc_ue_id,
          role2str(role),
          e->s_ng_node_ue_xnap_id,
          e->t_ng_node_ue_xnap_id,
          assoc_id);
  }
  mutexunlock(xn_ues.mutex);
  return xnap_id;
}

bool xn_set_ue_peer_id(uint32_t xnap_id, uint32_t peer_xnap_id)
{
  mutexlock(xn_ues.mutex);
  xn_ue_data_t *e = table_get(xnap_id);
  if (e != NULL) {
    if (e->role == XN_ROLE_SOURCE)
      e->t_ng_node_ue_xnap_id = peer_xnap_id;
    else
      e->s_ng_node_ue_xnap_id = peer_xnap_id;
    LOG_D(XNAP, "UE %u: %s XnAP UE ID %u: peer XnAP UE ID %u\n", e->rrc_ue_id, role2str(e->role), xnap_id, peer_xnap_id);
  }
  mutexunlock(xn_ues.mutex);
  return e != NULL;
}

bool xn_bind_ue(uint32_t xnap_id, uint32_t rrc_ue_id)
{
  mutexlock(xn_ues.mutex);
  xn_ue_data_t *e = table_get(xnap_id);
  if (e != NULL) {
    /* drop an entry left over from an earlier handover of this UE, unless already bound */
    xn_ue_data_t *stale = table_get_by_rrc_ue_id(rrc_ue_id);
    if (stale != e) {
      if (stale != NULL) {
        LOG_W(XNAP, "UE %u: dropping stale Xn handover (XnAP UE ID %u)\n", rrc_ue_id, local_id(stale));
        table_remove(local_id(stale));
      }
      table_index(rrc_ue_id, xnap_id);
    }
    e->rrc_ue_id = rrc_ue_id;
    LOG_D(XNAP, "UE %u: bound to %s XnAP UE ID %u\n", rrc_ue_id, role2str(e->role), xnap_id);
  }
  mutexunlock(xn_ues.mutex);
  return e != NULL;
}

bool xn_get_ue(xn_role_t role, uint32_t rrc_ue_id, xn_ue_data_t *out)
{
  mutexlock(xn_ues.mutex);
  bool found = table_copy(table_get_by_rrc_ue_id(rrc_ue_id), role, out);
  mutexunlock(xn_ues.mutex);
  return found;
}

bool xn_get_ue_by_xnap_id(xn_role_t role, uint32_t xnap_id, xn_ue_data_t *out)
{
  mutexlock(xn_ues.mutex);
  bool found = table_copy(table_get(xnap_id), role, out);
  mutexunlock(xn_ues.mutex);
  return found;
}

bool xn_remove_ue_by_xnap_id(uint32_t xnap_id)
{
  mutexlock(xn_ues.mutex);
  bool removed = table_remove(xnap_id);
  mutexunlock(xn_ues.mutex);
  return removed;
}

bool xn_remove_ue_by_peer_id(xn_role_t role, sctp_assoc_t assoc_id, uint32_t peer_xnap_id, xn_ue_data_t *out)
{
  mutexlock(xn_ues.mutex);
  bool found = table_copy(table_get_by_peer_id(role, assoc_id, peer_xnap_id), role, out);
  if (found)
    table_remove(local_id(out));
  mutexunlock(xn_ues.mutex);
  return found;
}

int xn_remove_ues_by_assoc(sctp_assoc_t assoc_id, xn_ue_data_t *out, int max)
{
  mutexlock(xn_ues.mutex);
  int n = 0;
  hash_table_iterator_s it = hashtable_get_iterator(&xn_ues.by_xnap_id);
  xn_ue_data_t *e = NULL;
  while (n < max && hashtable_iterator_getnext(&it, (void **)&e)) {
    if (e->assoc_id == assoc_id)
      out[n++] = *e;
  }
  for (int i = 0; i < n; i++)
    table_remove(local_id(&out[i]));
  mutexunlock(xn_ues.mutex);
  return n;
}

bool xn_remove_ue(uint32_t rrc_ue_id)
{
  mutexlock(xn_ues.mutex);
  xn_ue_data_t *e = table_get_by_rrc_ue_id(rrc_ue_id);
  bool removed = e != NULL && table_remove(local_id(e));
  mutexunlock(xn_ues.mutex);
  return removed;
}
