/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* "standalone" module to store a "secondary" UE ID for each UE in DU/CU.
 * Separate from the rest of F1, as it is also relevant for monolithic. */

#include "f1ap_ids.h"

#include <pthread.h>
#include "ds/hashtable.h"
#include "common/utils/assertions.h"


static f1_ue_data_t *get_hashtable_data(hash_table_t *ht, uint64_t ue_id)
{
  void *data = NULL;
  hashtable_rc_t ret = hashtable_get(ht, ue_id, &data);
  AssertFatal(ret == HASH_TABLE_OK && data != NULL, "element for ue_id %ld not found\n", ue_id);
  return data;
}

static bool add_hashtable_data(hash_table_t *ht, uint64_t ue_id, const f1_ue_data_t *data)
{
  f1_ue_data_t *idata = malloc(sizeof(*idata));
  AssertFatal(idata, "cannot allocate memory\n");
  *idata = *data;
  hashtable_rc_t ret = hashtable_insert(ht, ue_id, idata, free);
  return ret == HASH_TABLE_OK;
}

/* we have separate versions for CU and DU, as both CU&DU might coexist in the
 * same process */
static hash_table_t cu2du_ue_mapping;
static pthread_mutex_t cu2du_mutex = PTHREAD_MUTEX_INITIALIZER;

void cu_init_f1_ue_data(void)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes == NULL);
  cu2du_ue_mapping = hashtable_create(1319, NULL); // 1319 is prime, default hash func (unit)
  pthread_mutex_unlock(&cu2du_mutex);
}

bool cu_add_f1_ue_data(uint32_t ue_id, const f1_ue_data_t *data)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  if (hashtable_is_key_exists(&cu2du_ue_mapping, key) == HASH_TABLE_OK) {
    pthread_mutex_unlock(&cu2du_mutex);
    return false;
  }
  bool ret = add_hashtable_data(&cu2du_ue_mapping, key, data);
  pthread_mutex_unlock(&cu2du_mutex);
  return ret;
}

bool cu_exists_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  hashtable_rc_t ret = hashtable_is_key_exists(&cu2du_ue_mapping, key);
  pthread_mutex_unlock(&cu2du_mutex);
  return ret == HASH_TABLE_OK;
}

bool cu_update_f1_ue_data(uint32_t ue_id, const f1_ue_data_t *data)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  hashtable_rc_t retrm = hashtable_remove(&cu2du_ue_mapping, key, free);
  AssertFatal(retrm == HASH_TABLE_OK, "could not remove UE %d, ret %d\n", ue_id, retrm);
  bool ret = add_hashtable_data(&cu2du_ue_mapping, key, data);
  pthread_mutex_unlock(&cu2du_mutex);
  return ret;
}

f1_ue_data_t cu_get_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes != NULL);
  f1_ue_data_t ued = *get_hashtable_data(&cu2du_ue_mapping, ue_id);
  pthread_mutex_unlock(&cu2du_mutex);
  return ued;
}

bool cu_remove_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&cu2du_mutex);
  DevAssert(cu2du_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  hashtable_rc_t ret = hashtable_remove(&cu2du_ue_mapping, key, free);
  pthread_mutex_unlock(&cu2du_mutex);
  return ret == HASH_TABLE_OK;
}

/* DU version below */
static hash_table_t du2cu_ue_mapping;
static pthread_mutex_t du2cu_mutex = PTHREAD_MUTEX_INITIALIZER;

void du_init_f1_ue_data(void)
{
  pthread_mutex_lock(&du2cu_mutex);
  DevAssert(du2cu_ue_mapping.nodes == NULL);
  du2cu_ue_mapping = hashtable_create(1319, NULL); // 1319 is prime, default hash func (unit)
  pthread_mutex_unlock(&du2cu_mutex);
}

bool du_add_f1_ue_data(uint32_t ue_id, const f1_ue_data_t *data)
{
  pthread_mutex_lock(&du2cu_mutex);
  DevAssert(du2cu_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  if (hashtable_is_key_exists(&du2cu_ue_mapping, key) == HASH_TABLE_OK) {
    pthread_mutex_unlock(&du2cu_mutex);
    return false;
  }
  bool ret = add_hashtable_data(&du2cu_ue_mapping, key, data);
  pthread_mutex_unlock(&du2cu_mutex);
  return ret;
}

bool du_exists_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&du2cu_mutex);
  DevAssert(du2cu_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  hashtable_rc_t ret = hashtable_is_key_exists(&du2cu_ue_mapping, key);
  pthread_mutex_unlock(&du2cu_mutex);
  return ret == HASH_TABLE_OK;
}

f1_ue_data_t du_get_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&du2cu_mutex);
  DevAssert(du2cu_ue_mapping.nodes != NULL);
  f1_ue_data_t ued = *get_hashtable_data(&du2cu_ue_mapping, ue_id);
  pthread_mutex_unlock(&du2cu_mutex);
  return ued;
}

bool du_remove_f1_ue_data(uint32_t ue_id)
{
  pthread_mutex_lock(&du2cu_mutex);
  DevAssert(du2cu_ue_mapping.nodes != NULL);
  uint64_t key = ue_id;
  hashtable_rc_t ret = hashtable_remove(&du2cu_ue_mapping, key, free);
  pthread_mutex_unlock(&du2cu_mutex);
  return ret == HASH_TABLE_OK;
}
