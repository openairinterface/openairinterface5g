/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022 Mikel Irazabal
 */

#include "seq_arr.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const size_t MIN_SIZE = 8;

static void maybe_expand(seq_arr_t* arr)
{
  if (arr->size + 1 == arr->cap) {
    arr->data = realloc(arr->data, 2 * arr->cap * arr->elt_size);
    assert(arr->data != NULL && "realloc failed to allocate memory");
    arr->cap *= 2;
  }
}

static void maybe_shrink(seq_arr_t* arr)
{
  const float occ = (float)arr->size / (float)arr->cap;
  if (arr->size > MIN_SIZE && occ < 0.25) {
    assert(arr->cap > MIN_SIZE);
    seq_arr_t tmp = {.data = NULL, .size = arr->size, .elt_size = arr->elt_size, .cap = arr->cap / 2};
    tmp.data = calloc(tmp.cap, tmp.elt_size);
    assert(tmp.data != NULL && "Memory exhausted");
    assert(arr->size <= tmp.cap);
    memcpy(tmp.data, arr->data, arr->size * arr->elt_size);
    free(arr->data);
    memcpy(arr, &tmp, sizeof(seq_arr_t));
  }
}

void seq_arr_init(seq_arr_t* arr, size_t elt_size) //__attribute__(malloc)
{
  assert(arr != NULL);
  seq_arr_t tmp = {.data = NULL, .size = 0, .elt_size = elt_size, .cap = MIN_SIZE};
  memcpy(arr, &tmp, sizeof(seq_arr_t));
  arr->data = calloc(arr->cap, elt_size);
  assert(arr->data != NULL);
}

void seq_arr_free(seq_arr_t* arr, void (*free_func)(void*))
{
  assert(arr != NULL);
  assert(arr->data != NULL);

  if (free_func != NULL) {
    void* start_it = seq_arr_front(arr);
    void* end_it = seq_arr_end(arr);
    while (start_it != end_it) {
      free_func(start_it);
      start_it = seq_arr_next(arr, start_it);
    }
  }

  free(arr->data);
}

void seq_arr_push_back(seq_arr_t* arr, void* data, size_t len)
{
  assert(arr != NULL);
  assert(arr->data != NULL);
  // assert(data != NULL);
  assert(len == arr->elt_size);

  maybe_expand(arr);

  const size_t offset = arr->size * arr->elt_size;
  memcpy(&arr->data[offset], data, arr->elt_size);
  arr->size += 1;
}

void seq_arr_erase(seq_arr_t* arr, void* start_it)
{
  // start_it must be in the range of arr->data
  assert(arr != NULL);
  assert(start_it != NULL);
  return seq_arr_erase_deep(arr, start_it, NULL);
}

void seq_arr_erase_deep(seq_arr_t* arr, void* start_it, void (*free_func)(void* it))
{
  // start_it must be in the range of arr->data
  assert(arr != NULL);
  assert(start_it != NULL);
  return seq_arr_erase_it(arr, start_it, seq_arr_next(arr, start_it), free_func);
}

void seq_arr_erase_it(seq_arr_t* arr, void* start_it, void* end_it, void (*free_func)(void* it))
{
  // start_it && end_it must be in the range of arr->data
  assert(arr != NULL);
  assert(start_it != NULL);
  assert(end_it != NULL);
  assert(end_it >= start_it);

  if (start_it == end_it)
    return;

  if (free_func != NULL) {
    void* it = start_it;
    while (it != end_it) {
      free_func(it);
      it = seq_arr_next(arr, it);
    }
  }

  const int num_bytes_move = seq_arr_end(arr) - end_it;
  assert(num_bytes_move > -1);
  memmove(start_it, end_it, num_bytes_move);

  const int num_bytes_erase = end_it - start_it;
  const int32_t num_elm_erase = num_bytes_erase / arr->elt_size;
  assert(num_elm_erase > 0);
  arr->size -= num_elm_erase;

  memset(seq_arr_end(arr), 0, num_bytes_erase);

  maybe_shrink(arr);
}

size_t seq_arr_size(seq_arr_t const* arr)
{
  assert(arr != NULL);
  return arr->size;
}

void* seq_arr_front(seq_arr_t const* arr)
{
  assert(arr != NULL);
  return arr->data;
}

void* seq_arr_next(seq_arr_t const* arr, void const* it)
{
  assert(arr != NULL);
  assert(it != NULL);
  return (uint8_t*)it + arr->elt_size;
}

void* seq_arr_end(seq_arr_t const* arr)
{
  assert(arr != NULL);
  assert(arr->data != NULL);
  return &arr->data[arr->size * arr->elt_size];
}

void* seq_arr_at(seq_arr_t const* arr, uint32_t pos)
{
  assert(arr != NULL);
  assert(arr->data != NULL);
  assert(pos < arr->size);
  return arr->data + pos * arr->elt_size;
}

ptrdiff_t seq_arr_dist(seq_arr_t const* arr, void const* first, void const* last)
{
  assert(arr != NULL);
  assert(first != NULL);
  assert(last != NULL);
  const ptrdiff_t pos = (last - first) / arr->elt_size;
  assert(pos > -1);
  return pos;
}
