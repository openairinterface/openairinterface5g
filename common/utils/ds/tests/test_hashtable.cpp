/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <gtest/gtest.h>
extern "C" {
#include "hashtable.h"
}

TEST(hashtable, test_insert_remove) {
  hash_table_t table = hashtable_create(250, NULL);
  int* a = (int*)calloc(1, sizeof(*a));
  EXPECT_EQ(hashtable_insert(&table, 1, a, free), HASH_TABLE_OK);
  EXPECT_EQ(hashtable_remove(&table, 1, free), HASH_TABLE_OK);
  EXPECT_EQ(hashtable_destroy(&table, free), HASH_TABLE_OK);
}

TEST(hashtable, test_insert_iterate_remove) {
  hash_table_t table = hashtable_create(250, NULL);
  int* a = (int*)calloc(1, sizeof(*a));
  EXPECT_EQ(hashtable_insert(&table, 1, a, free), HASH_TABLE_OK);
  auto iterator = hashtable_get_iterator(&table);
  void* ptr;
  int num_items = 0;
  while(hashtable_iterator_getnext(&iterator, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, 1);
  EXPECT_EQ(hashtable_remove(&table, 1, free), HASH_TABLE_OK);
  num_items = 0;

  auto iterator2 = hashtable_get_iterator(&table);
  while(hashtable_iterator_getnext(&iterator2, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, 0);
  EXPECT_EQ(num_items, hashtable_num_entries(&table));
  EXPECT_EQ(hashtable_destroy(&table, free), HASH_TABLE_OK);
}

TEST(hashtable, test_insert_three_iterate_remove) {
  hash_table_t table = hashtable_create(250, NULL);
  int* a1 = (int*)calloc(1, sizeof(*a1));
  int* a2 = (int*)calloc(1, sizeof(*a2));
  int* a3 = (int*)calloc(1, sizeof(*a3));
  EXPECT_EQ(hashtable_insert(&table, 1, a1, free), HASH_TABLE_OK);
  EXPECT_EQ(hashtable_insert(&table, 2, a2, free), HASH_TABLE_OK);
  EXPECT_EQ(hashtable_insert(&table, 3, a3, free), HASH_TABLE_OK);
  auto iterator = hashtable_get_iterator(&table);
  void* ptr;
  int num_items = 0;
  while(hashtable_iterator_getnext(&iterator, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, 3);
  EXPECT_EQ(num_items, hashtable_num_entries(&table));
  EXPECT_EQ(hashtable_remove(&table, 1, free), HASH_TABLE_OK);
  num_items = 0;

  auto iterator2 = hashtable_get_iterator(&table);
  while(hashtable_iterator_getnext(&iterator2, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, 2);
  EXPECT_EQ(num_items, hashtable_num_entries(&table));
  EXPECT_EQ(hashtable_destroy(&table, free), HASH_TABLE_OK);
}

TEST(hashtable, test_insert_empty_insert_interate) {
  hash_table_t table = hashtable_create(250, NULL);
  int* a = (int*)calloc(1, sizeof(*a));
  EXPECT_EQ(hashtable_insert(&table, 1, a, free), HASH_TABLE_OK);
  EXPECT_EQ(hashtable_remove(&table, 1, free), HASH_TABLE_OK);

  int* a2 = (int*)calloc(1, sizeof(*a2));
  EXPECT_EQ(hashtable_insert(&table, 1, a2, free), HASH_TABLE_OK);
  auto iterator3 = hashtable_get_iterator(&table);
  int num_items = 0;
  void* ptr;
  while(hashtable_iterator_getnext(&iterator3, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, 1);
  EXPECT_EQ(num_items, hashtable_num_entries(&table));

  EXPECT_EQ(hashtable_destroy(&table, free), HASH_TABLE_OK);
}

TEST(hashtable, test_overfill) {
  hash_table_t table = hashtable_create(1, NULL);
  int num_add = 100;
  for (int i = 0; i < num_add; ++i) {
    int* a = (int*)calloc(1, sizeof(*a));
    *a = i;
    EXPECT_EQ(hashtable_insert(&table, i, a, free), HASH_TABLE_OK);
  }
  void* ptr;
  int num_items = 0;
  auto iterator = hashtable_get_iterator(&table);
  while(hashtable_iterator_getnext(&iterator, &ptr)) {
    num_items++;
  }
  EXPECT_EQ(num_items, num_add);
  EXPECT_EQ(hashtable_num_entries(&table), num_add);
  for (int i = 0; i < num_add; ++i) {
    EXPECT_EQ(hashtable_is_key_exists(&table, i), HASH_TABLE_OK);
    void *e;
    EXPECT_EQ(hashtable_get(&table, i, &e), HASH_TABLE_OK);
    EXPECT_EQ(i, *(int *)e);
  }
  for (int i = 0; i < num_add; ++i) {
    int idx = (50 + i) % num_add; // to remove in middle
    EXPECT_EQ(hashtable_remove(&table, idx, free), HASH_TABLE_OK);
  }

  EXPECT_EQ(hashtable_num_entries(&table), 0);
  EXPECT_EQ(hashtable_destroy(&table, free), HASH_TABLE_OK);
}

/* keys with constant low bits (like the c_init of the DMRS gold sequences, see refsig.c) must spread over a
 * power of two table with the default hash function */
TEST(hashtable, test_default_hash_spreads_structured_keys)
{
  const hash_size_t size = 1024;
  hash_table_t table = hashtable_create(size, NULL);
  const int num_keys = 20 * 14;
  for (int k = 0; k < num_keys; k++) {
    const hash_key_t key = ((((hash_key_t)k + 1) * 3) << 17) | 2;
    EXPECT_EQ(hashtable_insert(&table, key, (void*)1, NULL), HASH_TABLE_OK);
  }
  int longest = 0;
  for (hash_size_t n = 0; n < size; n++) {
    int len = table.nodes[n].num;
    longest = std::max(longest, len);
  }
  EXPECT_LE(longest, 8);
  EXPECT_EQ(hashtable_destroy(&table, NULL), HASH_TABLE_OK);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
