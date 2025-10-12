/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * fsp_test.c - Test program for libfsp
 *
 * Copyright (C) 2025, Dave Beckett https://www.dajobe.org/
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

#ifdef FSP_CONFIG
#include <fsp_config.h>
#else
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fsp.h"
#include "fsp_internal.h"  /* For direct access to context internals in tests */
#include "test_parser.h"

/* Define YYSTYPE for lexer header */
#define YYSTYPE TEST_PARSER_STYPE

#include "test_lexer.h"

static int test_count = 0;
static int test_failed = 0;

/* Helper function to read file into memory */
static char*
read_file(const char *filename, size_t *length)
{
  FILE *fp;
  char *content;
  size_t file_size;
  size_t bytes_read;
  
  fp = fopen(filename, "rb");
  if(!fp)
    return NULL;
  
  /* Get file size */
  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  /* Allocate buffer */
  content = (char*)malloc(file_size + 1);
  if(!content) {
    fclose(fp);
    return NULL;
  }
  
  /* Read content */
  bytes_read = fread(content, 1, file_size, fp);
  fclose(fp);
  
  if(bytes_read != file_size) {
    free(content);
    return NULL;
  }
  
  content[file_size] = '\0';
  if(length)
    *length = file_size;
  
  return content;
}

/* Serialize AST to string for comparison */
static char*
serialize_ast(void)
{
  statement_node *stmt;
  char *result;
  size_t result_size;
  size_t result_len;
  
  stmt = test_parser_get_statements();
  
  /* Start with reasonable buffer */
  result_size = 1024;
  result = (char*)malloc(result_size);
  if(!result)
    return NULL;
  
  result[0] = '\0';
  result_len = 0;
  
  /* Serialize each statement */
  while(stmt) {
    char line[2048];
    size_t line_len;
    
    if(stmt->type == STMT_PRINT) {
      snprintf(line, sizeof(line), "PRINT: %s\n", stmt->value);
    } else if(stmt->type == STMT_LET) {
      snprintf(line, sizeof(line), "LET: %s = %s\n", stmt->identifier, stmt->value);
    } else {
      continue;
    }
    
    line_len = strlen(line);
    
    /* Grow buffer if needed */
    if(result_len + line_len + 1 > result_size) {
      char *new_result;
      result_size = result_size * 2 + line_len;
      new_result = (char*)realloc(result, result_size);
      if(!new_result) {
        free(result);
        return NULL;
      }
      result = new_result;
    }
    
    strcpy(result + result_len, line);
    result_len += line_len;
    
    stmt = stmt->next;
  }
  
  return result;
}

/* Validate parsed output against expected output */
static int
validate_parse_result(const char *expected_file)
{
  char *expected_content;
  char *actual_content;
  int result;
  
  expected_content = read_file(expected_file, NULL);
  if(!expected_content) {
    fprintf(stderr, "Failed to read expected file: %s\n", expected_file);
    return -1;
  }
  
  actual_content = serialize_ast();
  if(!actual_content) {
    fprintf(stderr, "Failed to serialize AST\n");
    free(expected_content);
    return -1;
  }
  
  /* Byte-for-byte comparison */
  if(strcmp(actual_content, expected_content) == 0) {
    result = 0;
  } else {
    fprintf(stderr, "  Output mismatch\n");
    fprintf(stderr, "  Expected:\n%s", expected_content);
    fprintf(stderr, "  Got:\n%s", actual_content);
    result = -1;
  }
  
  free(expected_content);
  free(actual_content);
  return result;
}

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

/* Minimum buffer size before calling lexer to prevent partial token issues
 * Set to 16 bytes - sufficient for any keyword in the test grammar.
 * This enables streaming with arbitrarily small chunks (even 1 byte).
 */
#define MIN_BUFFER_FOR_LEX 16

/* Test the parser with streaming chunks using proper buffer accumulation strategy */
static int
test_streaming_parser(const char *input, size_t chunk_size,
                      const char *expected_file)
{
  fsp_context *ctx;
  yyscan_t scanner;
  test_parser_pstate *pstate;
  int status;
  size_t pos = 0;
  size_t input_len = strlen(input);
  int result;
  int final_drain = 0;

  /* Reset parser state before test */
  test_parser_reset();

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

  /* PROPER STREAMING STRATEGY:
   * 1. Accumulate chunks until buffer has MIN_BUFFER_FOR_LEX bytes OR EOF
   * 2. Only then call lexer - prevents Flex from seeing partial tokens
   * 3. This allows streaming with arbitrarily small chunks (even 1 byte)
   * See README.md "Streaming with Small Chunks" section for details.
   */
  while(pos < input_len || final_drain) {
    int is_eof;

    /* Phase 1: Accumulate chunks until buffer is sufficiently full */
    while(pos < input_len && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX) {
      size_t chunk;

      chunk = input_len - pos;
      if(chunk > chunk_size)
        chunk = chunk_size;

      /* Append chunk to FSP buffer */
      if(fsp_buffer_append(ctx, input + pos, chunk) < 0) {
        test_parser_pstate_delete(pstate);
        test_lexer_lex_destroy(scanner);
        fsp_destroy(ctx);
        return -1;
      }

      pos += chunk;
    }

    /* Check if we've reached end of input */
    is_eof = (pos >= input_len);

    if(is_eof && !final_drain) {
      /* Signal EOF to FSP context - no more chunks coming */
      ctx->more_chunks_expected = 0;
      final_drain = 1;
    }

    /* Phase 2: Process tokens (only when buffer is full enough OR at EOF) */
    while(fsp_buffer_available(ctx) > 0 || (is_eof && final_drain)) {
      TEST_PARSER_STYPE lval;
      int token;

      /* Don't call lexer if buffer is low and more data is coming */
      if(!is_eof && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX)
        break;  /* Get more chunks first */

      /* Get next token from lexer */
      token = test_lexer_lex(&lval, scanner);

      if(token == 0) {
        /* No more tokens available */
        if(!is_eof) {
          /* Lexer needs more data but we have more chunks coming */
          break;
        }
        /* Real EOF - done draining */
        final_drain = 0;
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
    }

    /* Exit loop if we're done draining at EOF */
    if(!final_drain && is_eof)
      break;
  }

  /* Push final EOF to parser */
  status = test_parser_push_parse(pstate, 0, NULL, ctx, scanner);

done:
  test_parser_pstate_delete(pstate);
  test_lexer_lex_destroy(scanner);
  fsp_destroy(ctx);

  if(status != 0)
    return -1;

  /* Validate result if expected file is provided */
  if(expected_file) {
    result = validate_parse_result(expected_file);
    return result;
  }

  return 0;
}

/* Test parser with input from file */
static int
test_file_parser(const char *input_file, const char *expected_file,
                 size_t chunk_size)
{
  char *input;
  size_t length;
  int result;
  
  input = read_file(input_file, &length);
  if(!input) {
    fprintf(stderr, "Failed to read input file: %s\n", input_file);
    return -1;
  }
  
  result = test_streaming_parser(input, chunk_size, expected_file);
  free(input);
  
  return result;
}

int main(int argc, char **argv)
{
  fsp_context *ctx;
  const char *test_data = "Test data chunk";
  const char *chunk1;
  const char *chunk2;
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
    (void)bytes_read; /* We don't check bytes_read here; just exercising compact */
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

  /* Test 10: Simple parse from file */
  TEST("Simple parse from file (tests/simple.txt)");
  fsp_destroy(ctx);
  if(test_file_parser("tests/simple.txt", "tests/simple.expected", 1024) < 0) {
    FAIL("Simple parse failed");
  } else {
    PASS();
  }

  /* Test 11: Parse with small chunks (streaming stress test) */
  TEST("Streaming parse with small chunks (tests/simple.txt)");
  if(test_file_parser("tests/simple.txt", "tests/simple.expected", 5) < 0) {
    FAIL("Streaming parse with small chunks failed");
  } else {
    PASS();
  }

  /* Test 12: Parse triple-quoted string from file */
  TEST("Triple-quoted string parse (tests/triple-quoted.txt)");
  if(test_file_parser("tests/triple-quoted.txt", "tests/triple-quoted.expected", 10) < 0) {
    FAIL("Triple-quoted string parse failed");
  } else {
    PASS();
  }

  /* Test 13: Mixed statements from file */
  TEST("Mixed statements parse (tests/mixed.txt)");
  if(test_file_parser("tests/mixed.txt", "tests/mixed.expected", 20) < 0) {
    FAIL("Mixed statements parse failed");
  } else {
    PASS();
  }

  /* Test 14: Empty input */
  TEST("Empty input (tests/empty.txt)");
  if(test_file_parser("tests/empty.txt", "tests/empty.expected", 1024) < 0) {
    FAIL("Empty input failed");
  } else {
    PASS();
  }

  /* Test 15: Moderate long string (1KB) - realistic size */
  TEST("Moderate long string parse (tests/long_string.txt)");
  if(test_file_parser("tests/long_string.txt", "tests/long_string.expected", 512) < 0) {
    FAIL("Moderate long string parse failed");
  } else {
    PASS();
  }

  /* Test 16: Small chunk streaming (tests buffer accumulation strategy) */
  /* With the proper buffer accumulation strategy (see MIN_BUFFER_FOR_LEX),
   * streaming works correctly with ANY chunk size, including 1-byte chunks.
   * This test uses 5-byte chunks to verify streaming across token boundaries. */
  TEST("Small chunk streaming with 5-byte chunks (tests/mixed.txt)");
  if(test_file_parser("tests/mixed.txt", "tests/mixed.expected", 5) < 0) {
    FAIL("5-byte chunk streaming with mixed.txt failed");
  } else {
    PASS();
  }

  /* Test 17: Malformed input - missing semicolon (should not crash) */
  TEST("Malformed input - missing semicolon (tests/missing_semicolon.txt)");
  {
    char *input;
    size_t length;
    
    input = read_file("tests/missing_semicolon.txt", &length);
    if(input) {
      /* Should handle error gracefully, not crash */
      (void)test_streaming_parser(input, 1024, NULL);
      free(input);
      /* We expect this to fail parsing, but not crash */
      PASS();
    } else {
      FAIL("Could not read test file");
    }
  }

  /* Test 18: 1-byte chunks (demonstrates proper buffer accumulation works) */
  TEST("Streaming with 1-byte chunks (tests/triple-quoted.txt)");
  if(test_file_parser("tests/triple-quoted.txt", "tests/triple-quoted.expected", 1) < 0) {
    FAIL("1-byte chunk streaming with triple-quoted.txt failed");
  } else {
    PASS();
  }

  /* Test 19: Malformed input - unterminated string (should not crash) */
  TEST("Malformed input - unterminated string (tests/unterminated_string.txt)");
  {
    char *input;
    size_t length;
    
    input = read_file("tests/unterminated_string.txt", &length);
    if(input) {
      /* Should handle error gracefully, not crash */
      (void)test_streaming_parser(input, 1024, NULL);
      free(input);
      /* We expect this to fail parsing, but not crash */
      PASS();
    } else {
      FAIL("Could not read test file");
    }
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
