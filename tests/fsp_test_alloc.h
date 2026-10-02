#ifndef FSP_TEST_ALLOC_H
#define FSP_TEST_ALLOC_H

#include <stddef.h>

void *fsp_test_malloc(size_t size);
void *fsp_test_calloc(size_t nmemb, size_t size);
void *fsp_test_realloc(void *ptr, size_t size);
void fsp_test_free(void *ptr);

void fsp_test_alloc_fail_after(long successful_allocations);
size_t fsp_test_alloc_outstanding(void);

#endif
