/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef _UTILS_COLLECTION_HASH_TABLE_H_
#define _UTILS_COLLECTION_HASH_TABLE_H_
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef size_t hash_size_t;
typedef uint64_t hash_key_t;

#define HASHTABLE_NOT_A_KEY_VALUE ((uint64_t)-1)

typedef enum hashtable_return_code_e {
  HASH_TABLE_OK = 0,
  HASH_TABLE_INSERT_OVERWRITTEN_DATA = 1,
  HASH_TABLE_KEY_NOT_EXISTS = 2,
  HASH_TABLE_KEY_ALREADY_EXISTS = 3,
  HASH_TABLE_BAD_PARAMETER_HASHTABLE = 4,
  HASH_TABLE_SYSTEM_ERROR = 5,
  HASH_TABLE_CODE_MAX
} hashtable_rc_t;

struct hash_node_s;
typedef struct hash_node_list_s {
  struct hash_node_s *arr;
  size_t cap;
  size_t num;
} hash_node_list_t;

typedef struct hash_table_s {
  hash_size_t size;
  size_t entries;
  struct hash_node_list_s *nodes; // pointer to array
  hash_size_t (*hashfunc)(const hash_key_t);
} hash_table_t;

typedef struct hash_table_iterator_s {
  struct hash_node_list_s *list;
  size_t idx_in_list;
  const hash_table_t *const hashtbl;
} hash_table_iterator_s;

hash_table_t hashtable_create(const hash_size_t size, hash_size_t (*hashfunc)(const hash_key_t));
hashtable_rc_t hashtable_destroy(hash_table_t *hashtbl, void (*freefunc)(void *));
hashtable_rc_t hashtable_is_key_exists(const hash_table_t *const hashtbl, const uint64_t key);
hashtable_rc_t hashtable_insert(hash_table_t *const hashtbl, const hash_key_t key, void *data, void (*freefunc)(void *));
hashtable_rc_t hashtable_remove(hash_table_t *const hashtbl, const hash_key_t key, void (*freefunc)(void *));
hashtable_rc_t hashtable_get(const hash_table_t *const hashtbl, const hash_key_t key, void **dataP);
size_t hashtable_num_entries(const hash_table_t *const hashtbl);

/* iterate through the hashtable. assumes the hashtable is stable (no
 * addition/removal during iteration). */
hash_table_iterator_s hashtable_get_iterator(const hash_table_t *const hashtbl);
bool hashtable_iterator_getnext(hash_table_iterator_s *iterator, void **dataP);

#endif
