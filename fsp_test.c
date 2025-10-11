/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * fsp_test.c - Test program for libfsp
 *
 * Copyright (C) 2025, Dave Beckett http://www.dajobe.org/
 * 
 * This package is Free Software
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fsp.h"
#include "test_parser.h"

/* Define YYSTYPE for lexer header */
#define YYSTYPE TEST_PARSER_STYPE

#include "test_lexer.h"

static int test_count = 0;
static int test_failed = 0;

#define TEST(name) do { \
  test_count++; \
  fprintf(stderr, "Test %d: %s ... ", test_count, name); \
} while(0)

#define PASS() do { \
  fprintf(stderr, "PASS\n"); \
} while(0)

#define FAIL(msg) do { \
  fprintf(stderr, "FAIL: %s\n", msg); \
  test_failed++; \
} while(0)

/* Test the parser with streaming chunks */
static int
test_streaming_parser(const char *input, size_t chunk_size)
{
  fsp_context *ctx;
  yyscan_t scanner;
  test_parser_pstate *pstate;
  int status;
  size_t pos = 0;
  size_t input_len = strlen(input);

  /* Create FSP context */
  ctx = fsp_create();
  if(!ctx)
    return -1;

  /* Initialize lexer */
  if(test_lexer_lex_init(&scanner)) {
    fsp_destroy(ctx);
    return -1;
  }

  /* Set FSP context as extra data for lexer */
  test_lexer_set_extra(ctx, scanner);

  /* Create push parser state */
  pstate = test_parser_pstate_new();
  if(!pstate) {
    test_lexer_lex_destroy(scanner);
    fsp_destroy(ctx);
    return -1;
  }

  /* Feed input in chunks */
  while(pos < input_len) {
    size_t chunk;
    int is_end;
    
    chunk = input_len - pos;
    if(chunk > chunk_size)
      chunk = chunk_size;

    is_end = (pos + chunk >= input_len);

    /* Append chunk to FSP buffer */
    if(fsp_buffer_append(ctx, input + pos, chunk) < 0) {
      test_parser_pstate_delete(pstate);
      test_lexer_lex_destroy(scanner);
      fsp_destroy(ctx);
      return -1;
    }

    pos += chunk;

    /* Feed tokens to parser until buffer is exhausted or parse completes */
    do {
      TEST_PARSER_STYPE lval;
      int token;

      /* Check if we have data to read */
      if(fsp_buffer_available(ctx) == 0 && !is_end)
        break;

      /* Get next token from lexer */
      token = test_lexer_lex(&lval, scanner);

      if(token == 0) {
        /* No more tokens available */
        if(is_end) {
          /* EOF - push EOF token to parser */
          status = test_parser_push_parse(pstate, 0, NULL, ctx, scanner);
          goto done;
        }
        /* Need more data */
        break;
      }

      if(token == ERROR) {
        status = 1;
        goto done;
      }

      /* Push token to parser */
      status = test_parser_push_parse(pstate, token, &lval, ctx, scanner);

      if(status != YYPUSH_MORE) {
        /* Parse complete or error */
        goto done;
      }
    } while(1);
  }

  /* Push final EOF */
  status = test_parser_push_parse(pstate, 0, NULL, ctx, scanner);

done:
  test_parser_pstate_delete(pstate);
  test_lexer_lex_destroy(scanner);
  fsp_destroy(ctx);

  return (status == 0) ? 0 : -1;
}

int main(int argc, char **argv)
{
  fsp_context *ctx;
  const char *test_data = "Test data chunk";
  const char *chunk1;
  const char *chunk2;
  const char *simple_input;
  const char *long_string_input;
  size_t test_data_len;
  size_t expected;
  size_t large_size;
  char buffer[1024];
  char *large_data;
  int bytes_read;
  size_t available;
  
  (void)argc;
  (void)argv;
  
  test_data_len = strlen(test_data);

  fprintf(stderr, "libfsp test suite\n");
  fprintf(stderr, "==================\n\n");

  /* Test 1: Create context */
  TEST("fsp_create");
  ctx = fsp_create();
  if(!ctx) {
    FAIL("Failed to create FSP context");
    return 1;
  }
  PASS();

  /* Test 2: Buffer append */
  TEST("fsp_buffer_append");
  if(fsp_buffer_append(ctx, test_data, test_data_len) < 0) {
    FAIL("Failed to append data");
    fsp_destroy(ctx);
    return 1;
  }
  PASS();

  /* Test 3: Buffer available */
  TEST("fsp_buffer_available");
  available = fsp_buffer_available(ctx);
  if(available != test_data_len) {
    FAIL("Available bytes mismatch");
    fprintf(stderr, "  Expected %zu, got %zu\n", test_data_len, available);
  } else {
    PASS();
  }

  /* Test 4: Read input */
  TEST("fsp_read_input");
  bytes_read = fsp_read_input(ctx, buffer, sizeof(buffer));
  if(bytes_read != (int)test_data_len) {
    FAIL("Read byte count mismatch");
    fprintf(stderr, "  Expected %zu, got %d\n", test_data_len, bytes_read);
  } else if(memcmp(buffer, test_data, test_data_len) != 0) {
    FAIL("Read data mismatch");
  } else {
    PASS();
  }

  /* Test 5: Buffer available after read */
  TEST("fsp_buffer_available after read");
  available = fsp_buffer_available(ctx);
  if(available != 0) {
    FAIL("Should have no available bytes after read");
    fprintf(stderr, "  Expected 0, got %zu\n", available);
  } else {
    PASS();
  }

  /* Test 6: User data */
  {
    int test_value = 42;
    int *retrieved;
    TEST("fsp_set_user_data/fsp_get_user_data");
    fsp_set_user_data(ctx, &test_value);
    retrieved = (int*)fsp_get_user_data(ctx);
    if(!retrieved || *retrieved != test_value) {
      FAIL("User data mismatch");
    } else {
      PASS();
    }
  }

  /* Test 7: Buffer compact */
  TEST("fsp_buffer_compact");
  /* Append more data */
  if(fsp_buffer_append(ctx, test_data, test_data_len) < 0) {
    FAIL("Failed to append data for compact test");
  } else {
    /* Read half */
    bytes_read = fsp_read_input(ctx, buffer, test_data_len / 2);
    /* Compact */
    fsp_buffer_compact(ctx);
    available = fsp_buffer_available(ctx);
    if(available != test_data_len - (test_data_len / 2)) {
      FAIL("Buffer compact failed");
      fprintf(stderr, "  Expected %zu, got %zu\n",
              test_data_len - (test_data_len / 2), available);
    } else {
      PASS();
    }
  }

  /* Test 8: Multiple chunks */
  TEST("Multiple chunk append");
  fsp_destroy(ctx);
  ctx = fsp_create();

  chunk1 = "First chunk ";
  chunk2 = "Second chunk";

  if(fsp_buffer_append(ctx, chunk1, strlen(chunk1)) < 0 ||
     fsp_buffer_append(ctx, chunk2, strlen(chunk2)) < 0) {
    FAIL("Failed to append multiple chunks");
  } else {
    available = fsp_buffer_available(ctx);
    expected = strlen(chunk1) + strlen(chunk2);
    if(available != expected) {
      FAIL("Multiple chunk size mismatch");
      fprintf(stderr, "  Expected %zu, got %zu\n", expected, available);
    } else {
      PASS();
    }
  }

  /* Test 9: Large buffer growth */
  TEST("Large buffer growth");
  fsp_destroy(ctx);
  ctx = fsp_create();

  /* Append data larger than default buffer */
  large_size = 128 * 1024; /* 128KB */
  large_data = (char*)malloc(large_size);
  if(!large_data) {
    FAIL("Failed to allocate test data");
  } else {
    memset(large_data, 'X', large_size);
    if(fsp_buffer_append(ctx, large_data, large_size) < 0) {
      FAIL("Failed to append large data");
    } else {
      available = fsp_buffer_available(ctx);
      if(available != large_size) {
        FAIL("Large buffer size mismatch");
        fprintf(stderr, "  Expected %zu, got %zu\n", large_size, available);
      } else {
        PASS();
      }
    }
    free(large_data);
  }

  /* Test 10: Simple parse with large chunks */
  TEST("Simple parse (large chunks)");
  fsp_destroy(ctx);
  simple_input = "print \"hello\"; let x = 42;";
  if(test_streaming_parser(simple_input, 1024) < 0) {
    FAIL("Simple parse failed");
  } else {
    PASS();
  }

  /* Test 11: Parse with small chunks (streaming stress test) */
  TEST("Streaming parse (small chunks)");
  if(test_streaming_parser(simple_input, 5) < 0) {
    FAIL("Streaming parse with small chunks failed");
  } else {
    PASS();
  }

  /* Test 12: Parse with triple-quoted string */
  TEST("Triple-quoted string parse");
  long_string_input = "print \"\"\"This is a\nmulti-line\nstring\"\"\";";
  if(test_streaming_parser(long_string_input, 10) < 0) {
    FAIL("Triple-quoted string parse failed");
  } else {
    PASS();
  }

  /* Summary */
  fprintf(stderr, "\n==================\n");
  fprintf(stderr, "Tests run: %d\n", test_count);
  fprintf(stderr, "Tests passed: %d\n", test_count - test_failed);
  fprintf(stderr, "Tests failed: %d\n", test_failed);

  if(test_failed > 0) {
    fprintf(stderr, "\nFAILED\n");
    return 1;
  }

  fprintf(stderr, "\nAll tests passed\n");
  return 0;
}
