/*
 * SPDX-License-Identifier: MIT
 */

#include "../seq_arr.h"
#include "../../alg/find.h"
#include "../../alg/foreach.h"
#include <assert.h>

/*
  Example to show seq_arr_t capabilities and usage
  To compile: gcc test_seq_array.c ../seq_arr.c  ../../alg/find.c ../../alg/foreach.c
*/

static bool eq_int(const void* value, const void* it)
{
  const int* v = (const int*)value;
  const int* i = (const int*)it;
  return *v == *i;
}

static void sum_elm(void* value, void* it)
{
  int* sum = (int*)value;
  int* elm = (int*)it;
  *sum += *elm;
}

static int compar(const void* m0, const void* m1)
{
  int* a = (int*)m0;
  int* b = (int*)m1;

  if (*a < *b)
    return -1;

  if (*a == *b)
    return 0;

  return 1;
}

static int free_func_call_count = 0;

static void dummy_free_func(void* it)
{
  ++free_func_call_count;
}

// shrink with elements larger than the new capacity
static void test_shrink_big_elements(void)
{
  typedef struct {
    int id;
    char pad[96];
  } big_elm_t;
  seq_arr_t big = {0};
  seq_arr_init(&big, sizeof(big_elm_t));
  for (int i = 0; i < 40; ++i) {
    big_elm_t e = {.id = i};
    seq_arr_push_back(&big, &e, sizeof(e));
  }
  // capacity is now 64: erasing down to 10 shrinks it to 32
  while (seq_arr_size(&big) > 10)
    seq_arr_erase(&big, seq_arr_front(&big));
  assert(big.cap == 32);
  for (int i = 0; i < 10; ++i)
    assert(((big_elm_t*)seq_arr_at(&big, i))->id == 30 + i);
  // fill the shrunk buffer up to its capacity
  for (int i = 0; i < 20; ++i) {
    big_elm_t e = {.id = 100 + i};
    seq_arr_push_back(&big, &e, sizeof(e));
  }
  assert(seq_arr_size(&big) == 30);
  assert(((big_elm_t*)seq_arr_at(&big, 29))->id == 119);
  seq_arr_free(&big, NULL);
}

int main()
{
  seq_arr_t arr = {0};
  seq_arr_init(&arr, sizeof(int));

  // Insert data and expand
  for (int i = 0; i < 100; ++i)
    seq_arr_push_back(&arr, &i, sizeof(int));

  // Check inserted data
  assert(seq_arr_size(&arr) == 100);
  assert(*(int*)seq_arr_front(&arr) == 0);
  assert(*(int*)seq_arr_at(&arr, 25) == 25);

  // Find element in the array
  int value = 50;
  elm_arr_t elm = find_if(&arr, &value, eq_int);
  assert(elm.found == true);
  // Check
  assert(*(int*)elm.it == 50);
  assert(seq_arr_dist(&arr, seq_arr_front(&arr), elm.it) == 50);

  // Apply predicate to all the elements
  int sum = 0;
  for_each(&arr, &sum, sum_elm);
  assert(sum == 4950); // N*(N+1)/2 -> 99*100/2

  // Erase found element in the array
  seq_arr_erase(&arr, elm.it);
  // Check
  assert(seq_arr_size(&arr) == 99);
  assert(*(int*)seq_arr_at(&arr, 50) == 51);

  // Erase range and force shrink
  seq_arr_erase_it(&arr, seq_arr_front(&arr), seq_arr_at(&arr, 90), NULL);
  assert(seq_arr_size(&arr) == 9);
  assert(*(int*)seq_arr_front(&arr) == 91);

  // Recall that C functions qsort and bsearch, also work
  int key = 95;
  void* base = seq_arr_front(&arr);
  size_t nmemb = seq_arr_size(&arr);
  size_t size = sizeof(int);

  void* it = bsearch(&key, base, nmemb, size, compar);
  assert(seq_arr_dist(&arr, seq_arr_front(&arr), it) == 4);

  // Regression test: erase a partial range only
  // Erase 92, 93, 94 (i.e., indices 1 to 3)
  int* start = seq_arr_at(&arr, 1);
  int* end = seq_arr_at(&arr, 4);
  seq_arr_erase_it(&arr, start, end, dummy_free_func);

  // Now values should be: [91, 95, 96, 97, 98, 99]
  assert(free_func_call_count == 3);
  assert(seq_arr_size(&arr) == 6);
  assert(*(int*)seq_arr_at(&arr, 0) == 91);
  assert(*(int*)seq_arr_at(&arr, 1) == 95);
  assert(*(int*)seq_arr_at(&arr, 5) == 99);

  // Free data structure
  seq_arr_free(&arr, NULL);

  test_shrink_big_elements();

  return EXIT_SUCCESS;
}
