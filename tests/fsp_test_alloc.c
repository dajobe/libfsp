#include <stdlib.h>

#include "fsp_test_alloc.h"

#define FSP_TEST_ALLOCATION_LIMIT 32

static void *allocations[FSP_TEST_ALLOCATION_LIMIT];
static long fail_after = -1;

static int
should_fail(void)
{
  if(fail_after < 0)
    return 0;
  if(fail_after == 0) {
    fail_after = -1;
    return 1;
  }
  fail_after--;
  return 0;
}

static void
track_allocation(void *ptr)
{
  size_t i;

  for(i = 0; i < FSP_TEST_ALLOCATION_LIMIT; i++) {
    if(!allocations[i]) {
      allocations[i] = ptr;
      return;
    }
  }
  abort();
}

void
fsp_test_alloc_fail_after(long successful_allocations)
{
  fail_after = successful_allocations;
}

size_t
fsp_test_alloc_outstanding(void)
{
  size_t i;
  size_t count = 0;

  for(i = 0; i < FSP_TEST_ALLOCATION_LIMIT; i++)
    if(allocations[i])
      count++;
  return count;
}

void*
fsp_test_malloc(size_t size)
{
  void *ptr;

  if(should_fail())
    return NULL;
  ptr = malloc(size);
  if(ptr)
    track_allocation(ptr);
  return ptr;
}

void*
fsp_test_calloc(size_t nmemb, size_t size)
{
  void *ptr;

  if(should_fail())
    return NULL;
  ptr = calloc(nmemb, size);
  if(ptr)
    track_allocation(ptr);
  return ptr;
}

void*
fsp_test_realloc(void *ptr, size_t size)
{
  size_t i;
  void *new_ptr;

  if(should_fail())
    return NULL;
  new_ptr = realloc(ptr, size);
  if(!new_ptr)
    return NULL;
  for(i = 0; i < FSP_TEST_ALLOCATION_LIMIT; i++) {
    if(allocations[i] == ptr) {
      allocations[i] = new_ptr;
      return new_ptr;
    }
  }
  track_allocation(new_ptr);
  return new_ptr;
}

void
fsp_test_free(void *ptr)
{
  size_t i;

  if(!ptr)
    return;
  for(i = 0; i < FSP_TEST_ALLOCATION_LIMIT; i++) {
    if(allocations[i] == ptr) {
      allocations[i] = NULL;
      free(ptr);
      return;
    }
  }
  abort();
}
