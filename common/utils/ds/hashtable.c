/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "hashtable.h"
#include "common/utils/utils.h"

typedef struct hash_node_s {
  hash_key_t key;
  void *data;
} hash_node_t;

static void free_hash_node_list(hash_node_list_t *l)
{
  free(l->arr);
}

#define NODE_CAP_INIT 4
static hash_node_t *get_next_node(hash_node_list_t *l)
{
  if (l->num >= l->cap) {
    l->cap = l->cap == 0 ? NODE_CAP_INIT : l->cap * 2;
    l->arr = realloc(l->arr, sizeof(*l->arr) * l->cap);
    if (!l->arr)
      abort();
  }
  l->num++;
  return &l->arr[l->num - 1];
}


//-------------------------------------------------------------------------------------------------------------------------------
/*
 * Default hash function
 * def_hashfunc() is the default used by hashtable_create() when the user didn't specify one.
 * The bucket is hash % size, so the hash must depend on all the key bits: OAI keys are often structured
 * (e.g. c_init = A << 17 + 2 * N_ID + n_SCID), and with an identity hash and a power of two size, all the
 * keys of a cell land in the same few buckets. Use the splitmix64 finalizer to mix the 64 key bits.
 */

static hash_size_t def_hashfunc(const uint64_t keyP)
{
  uint64_t x = keyP;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return (hash_size_t)x;
}

//-------------------------------------------------------------------------------------------------------------------------------
/*
 * Initialisation
 * hashtable_create() sets up the initial structure of the hash table. The user specified size will be allocated and initialized to
 * NULL. The user can also specify a hash function. If the hashfunc argument is NULL, a default hash function is used. All other
 * values in the returned hash_table_t pointer should be released with hashtable_destroy().
 */
hash_table_t hashtable_create(const hash_size_t sizeP, hash_size_t (*hashfuncP)(const hash_key_t))
{
  hash_table_t hashtbl = {
    .nodes = calloc_or_fail(sizeP, sizeof(*hashtbl.nodes)),
    .hashfunc = hashfuncP ? hashfuncP : def_hashfunc,
    .size = sizeP,
  };
  return hashtbl;
}
//-------------------------------------------------------------------------------------------------------------------------------
/*
 * Cleanup
 * The hashtable_destroy() releases all elements. It also releases
 * the nodes array and the hash_table_t.
 */
hashtable_rc_t hashtable_destroy(hash_table_t *hashtblP, void (*freefunc)(void *))
{
  if (hashtblP == NULL) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash_node_list_t *start = &hashtblP->nodes[0];
  for (hash_node_list_t *l = start; l < start + hashtblP->size; ++l) {
    for (hash_node_t *x = l->arr; x < l->arr + l->num; ++x) {
      if (freefunc && x->data)
        freefunc(x->data);
    }
    free_hash_node_list(l);
  }

  free(hashtblP->nodes);
  return HASH_TABLE_OK;
}
//-------------------------------------------------------------------------------------------------------------------------------
hashtable_rc_t hashtable_is_key_exists(const hash_table_t *const hashtblP, const hash_key_t keyP)
//-------------------------------------------------------------------------------------------------------------------------------
{
  if (hashtblP == NULL) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash_size_t hash = hashtblP->hashfunc(keyP) % hashtblP->size;
  hash_node_list_t *l = &hashtblP->nodes[hash];

  for (hash_node_t *x = l->arr; x < l->arr + l->num; ++x) {
    if (x->key == keyP) {
      return HASH_TABLE_OK;
    }
  }

  return HASH_TABLE_KEY_NOT_EXISTS;
}

//-------------------------------------------------------------------------------------------------------------------------------
/*
 * Adding a new element
 * To make sure the hash value is not bigger than size, the result of the user provided hash function is used modulo size.
 */
hashtable_rc_t hashtable_insert(hash_table_t *const hashtblP, const hash_key_t keyP, void *dataP, void (*freefunc)(void *))
{

  if (hashtblP == NULL) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash_size_t hash = hashtblP->hashfunc(keyP) % hashtblP->size;
  hash_node_list_t *l = &hashtblP->nodes[hash];
  for (hash_node_t *x = l->arr; x < l->arr + l->num; ++x) {
    if (x->key == keyP) {
      if (freefunc && x->data) {
        freefunc(x->data);
      }

      x->data = dataP;
      return HASH_TABLE_INSERT_OVERWRITTEN_DATA;
    }
  }

  hash_node_t *new = get_next_node(l);
  new->key = keyP;
  new->data = dataP;

  hashtblP->entries++;
  return HASH_TABLE_OK;
}
//-------------------------------------------------------------------------------------------------------------------------------
/*
 * To remove an element from the hash table, we just search for it using hash value,
 * and remove it if it is found. If it was not found, it is an error and -1 is returned.
 */
hashtable_rc_t hashtable_remove(hash_table_t *const hashtblP, const hash_key_t keyP, void (*freefunc)(void *))
{

  if (hashtblP == NULL) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash_size_t hash = hashtblP->hashfunc(keyP) % hashtblP->size;
  hash_node_list_t *l = &hashtblP->nodes[hash];

  for (hash_node_t *x = l->arr; x < l->arr + l->num; ++x) {
    if (x->key == keyP) {
      if (freefunc && x->data) {
        freefunc(x->data);
      }

      for (hash_node_t *y = x + 1; y < l->arr + l->num; ++y)
        *x++ = *y;
      l->num--;
      hashtblP->entries--;
      return HASH_TABLE_OK;
    }
  }

  return HASH_TABLE_KEY_NOT_EXISTS;
}
//-------------------------------------------------------------------------------------------------------------------------------
/*
 * Searching for an element is easy. We search for the corresponding hash value.
 * NULL is returned if we didn't find it.
 */
hashtable_rc_t hashtable_get(const hash_table_t *const hashtblP, const hash_key_t keyP, void **dataP)
{
  if (hashtblP == NULL) {
    *dataP = NULL;
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash_size_t hash = hashtblP->hashfunc(keyP) % hashtblP->size;
  hash_node_list_t *l = &hashtblP->nodes[hash];

  for (hash_node_t *x = l->arr; x < l->arr + l->num; ++x) {
    if (x->key == keyP) {
      *dataP = x->data;
      return HASH_TABLE_OK;
    }
  }

  *dataP = NULL;
  return HASH_TABLE_KEY_NOT_EXISTS;
}

size_t hashtable_num_entries(const hash_table_t *const hashtbl)
{
  return hashtbl->entries;
}

hash_table_iterator_s hashtable_get_iterator(const hash_table_t *const hashtbl)
{
  hash_node_list_t *l = &hashtbl->nodes[0];
  return (hash_table_iterator_s){.list = l, .idx_in_list = 0, .hashtbl = hashtbl};
}

bool hashtable_iterator_getnext(hash_table_iterator_s *it, void **dataP)
{
  *dataP = NULL;
  // Iterate over table indexes
  const hash_node_list_t *end = it->hashtbl->nodes + it->hashtbl->size;
  while (it->list < end) {
    if (it->idx_in_list < it->list->num) {
      *dataP = it->list->arr[it->idx_in_list].data;
      it->idx_in_list++;
      return true;
    } // else: try in next list
    it->list++; // advance to next list
    it->idx_in_list = 0;
  }
  return false;
}
