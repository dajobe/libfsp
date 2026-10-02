#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsp.h"
#include "fsp_internal.h"
#include "fsp_test_alloc.h"

int
main(void)
{
  fsp_context *ctx;
  char *large;
  char buffer[8];
  size_t large_size = 70 * 1024;
  int failed = 0;

  fsp_test_alloc_fail_after(0);
  if(fsp_create() != NULL || fsp_test_alloc_outstanding() != 0) {
    fprintf(stderr, "fsp_create did not handle context allocation failure\n");
    failed = 1;
  }

  fsp_test_alloc_fail_after(1);
  if(fsp_create() != NULL || fsp_test_alloc_outstanding() != 0) {
    fprintf(stderr, "fsp_create leaked after buffer allocation failure\n");
    failed = 1;
  }

  ctx = fsp_create();
  large = (char*)malloc(large_size);
  if(!ctx || !large) {
    fprintf(stderr, "could not allocate growth-failure test state\n");
    fsp_destroy(ctx);
    free(large);
    return 1;
  }
  memset(large, 'x', large_size);

  if(fsp_buffer_append(ctx, "safe", 4) != 0) {
    fprintf(stderr, "could not seed growth-failure buffer\n");
    failed = 1;
  } else {
    fsp_test_alloc_fail_after(0);
    if(fsp_buffer_append(ctx, large, large_size) == 0 ||
       fsp_buffer_available(ctx) != 4 ||
       fsp_read_input(ctx, buffer, sizeof(buffer)) != 4 ||
       memcmp(buffer, "safe", 4) != 0) {
      fprintf(stderr, "failed growth changed retained buffer data\n");
      failed = 1;
    }
  }

  free(large);
  fsp_destroy(ctx);
  if(fsp_test_alloc_outstanding() != 0) {
    fprintf(stderr, "allocation accounting found a leak\n");
    failed = 1;
  }

  if(failed)
    return 1;
  puts("Allocation failure and cleanup checks passed");
  return 0;
}
