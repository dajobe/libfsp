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

#ifdef HAVE_FSP_CONFIG_H
#include <fsp_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fsp.h"
#include "fsp_internal.h"  /* For direct access to context internals in tests */
#include "test_parser.h"

/* Define YYSTYPE for lexer header */
#define YYSTYPE TEST_PARSER_STYPE
#define YYLTYPE TEST_PARSER_LTYPE

#include "test_lexer.h"

int test_generated_parse(fsp_context *ctx, const char *input,
                         size_t chunk_size, void *user_data);

static int test_count = 0;
static int test_failed = 0;

/* Line number reached by the lexer in the last streaming parse */
static int last_lineno = 0;

/* Number of lexer batches in the last streaming parse, for retry bounds. */
static size_t last_lexer_batches = 0;

/* Helper function to read file into memory */
static char*
read_file(const char *filename, size_t *length)
{
  FILE *fp;
  char *content;
  size_t file_size;
  size_t bytes_read;
  const char *srcdir = getenv("srcdir");

  /* Automake supplies srcdir when tests run outside the source tree. */
  if(srcdir && *srcdir) {
    size_t path_size = strlen(srcdir) + strlen(filename) + 2;
    char *path = (char*)malloc(path_size);
    if(!path)
      return NULL;
    snprintf(path, path_size, "%s/%s", srcdir, filename);
    fp = fopen(path, "rb");
    free(path);
  } else {
    fp = fopen(filename, "rb");
  }
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

/* Parse input with the streaming parser, appending it in chunks of
 * chunk_size bytes.  Uses rewind support so that a token split across
 * chunks is rescanned once the rest of it has arrived.  The parsed
 * statements are left for the caller to inspect and free.
 *
 * Returns 0 on success, -1 on error
 */
static int
run_streaming_parser_plan(const char *input, size_t input_len,
                          size_t chunk_size, const size_t *chunk_plan,
                          size_t chunk_count, int separate_eof)
{
  fsp_context *ctx;
  yyscan_t scanner;
  test_parser_pstate *pstate;
  TEST_PARSER_LTYPE eof_loc;
  int status = 0;
  int eof_sent = 0;
  size_t pos = 0;
  size_t chunk_index = 0;

  /* Reset parser state before test */
  test_parser_reset();
  last_lexer_batches = 0;

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

  /* Enable rewind support before the first token */
  test_lexer_fsp_commit(scanner);
  memset(&eof_loc, 0, sizeof(eof_loc));

  while(1) {
    /* Append the next chunk, if any */
    if(pos < input_len || (chunk_plan && chunk_index < chunk_count)) {
      size_t chunk;
      int is_end;
      fsp_status chunk_status;

      if(chunk_plan) {
        if(chunk_index >= chunk_count) {
          status = 1;
          goto done;
        }
        chunk = chunk_plan[chunk_index++];
      } else {
        chunk = input_len - pos;
        if(chunk > chunk_size)
          chunk = chunk_size;
      }

      if(chunk > input_len - pos) {
        status = 1;
        goto done;
      }

      is_end = (pos + chunk >= input_len && !separate_eof);
      chunk_status = fsp_parse_chunk(ctx, chunk ? input + pos : NULL,
                                     chunk, is_end);
      if((is_end && chunk_status != FSP_STATUS_OK) ||
         (!is_end && chunk_status != FSP_STATUS_NEED_DATA)) {
        status = 1;
        goto done;
      }
      if(is_end)
        eof_sent = 1;
      pos += chunk;

    } else if(pos < input_len) {
      status = 1;
      goto done;
    } else if(!eof_sent) {
      if(fsp_parse_chunk(ctx, NULL, 0, 1) != FSP_STATUS_OK) {
        status = 1;
        goto done;
      }
      eof_sent = 1;
    }

    if(!fsp_input_ready(ctx))
      continue;

    last_lexer_batches++;

    /* Lex and parse until the lexer needs more input or the end */
    while(1) {
      TEST_PARSER_STYPE lval;
      TEST_PARSER_LTYPE lloc;
      int token;

      lval.string = NULL;
      memset(&lloc, 0, sizeof(lloc));
      token = test_lexer_lex(&lval, &lloc, scanner);

      if(token == FSP_LEXER_NEED_MORE ||
         (!token && fsp_input_would_block(ctx))) {
        /* The input ran out, possibly inside a token.  Discard any
         * partial long string and rescan from the end of the last
         * complete token after the next chunk is appended. */
        free(lval.string);
        test_lexer_fsp_rewind(scanner);
        break;
      }

      if(!token) {
        /* Real EOF; free an unterminated long string */
        free(lval.string);
        goto eof;
      }

      test_lexer_fsp_commit(scanner);

      if(token == ERROR) {
        /* Push EOF so the parser aborts and frees the values on its
         * stack, which deleting the parser state does not do */
        (void)test_parser_push_parse(pstate, 0, NULL, &lloc, ctx, scanner);
        status = 1;
        goto done;
      }

      /* Push token to parser */
      status = test_parser_push_parse(pstate, token, &lval, &lloc,
                                      ctx, scanner);

      if(status != YYPUSH_MORE) {
        /* Parse complete or error */
        goto done;
      }
    }
  }

eof:
  /* Push final EOF to parser */
  status = test_parser_push_parse(pstate, 0, NULL, &eof_loc, ctx, scanner);

done:
  last_lineno = test_lexer_get_lineno(scanner);
  test_parser_pstate_delete(pstate);
  test_lexer_lex_destroy(scanner);
  fsp_destroy(ctx);

  if(status != 0) {
    /* Free any partially parsed statements on error */
    test_parser_free_statements();
    return -1;
  }

  return 0;
}


static int
run_streaming_parser(const char *input, size_t input_len, size_t chunk_size)
{
  return run_streaming_parser_plan(input, input_len, chunk_size,
                                   NULL, 0, 0);
}


/* Test the parser with streaming chunks and validate the result */
static int
test_streaming_parser(const char *input, size_t chunk_size,
                      const char *expected_file)
{
  int result;

  if(run_streaming_parser(input, strlen(input), chunk_size) < 0)
    return -1;

  /* Validate result if expected file is provided */
  result = 0;
  if(expected_file)
    result = validate_parse_result(expected_file);

  test_parser_free_statements();
  return result;
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

  /* Test 16: Small chunk streaming across token boundaries */
  /* With rewind support, streaming works correctly with ANY chunk size,
   * including 1-byte chunks.  This test uses 5-byte chunks to verify
   * streaming across token boundaries. */
  TEST("Small chunk streaming with 5-byte chunks (tests/mixed.txt)");
  if(test_file_parser("tests/mixed.txt", "tests/mixed.expected", 5) < 0) {
    FAIL("5-byte chunk streaming with mixed.txt failed");
  } else {
    PASS();
  }

  /* Test 17: Malformed input - missing semicolon */
  TEST("Malformed input - missing semicolon (tests/missing_semicolon.txt)");
  {
    char *input;
    size_t length;
    size_t chunk_size;
    int failures = 0;

    input = read_file("tests/missing_semicolon.txt", &length);
    if(input) {
      test_parser_set_quiet(1);
      for(chunk_size = 1; chunk_size <= length + 1; chunk_size++) {
        if(test_streaming_parser(input, chunk_size, NULL) == 0) {
          fprintf(stderr, "\n  Accepted malformed input with %zu-byte chunks",
                  chunk_size);
          failures++;
        }
      }
      test_parser_set_quiet(0);
      free(input);
      if(failures)
        FAIL("Missing-semicolon input was accepted");
      else
        PASS();
    } else {
      FAIL("Could not read test file");
    }
  }

  /* Test 18: 1-byte chunks (every token is split across chunks) */
  TEST("Streaming with 1-byte chunks (tests/triple-quoted.txt)");
  if(test_file_parser("tests/triple-quoted.txt", "tests/triple-quoted.expected", 1) < 0) {
    FAIL("1-byte chunk streaming with triple-quoted.txt failed");
  } else {
    PASS();
  }

  /* Test 19: Malformed input - unterminated string */
  TEST("Malformed input - unterminated string (tests/unterminated_string.txt)");
  {
    char *input;
    size_t length;
    size_t chunk_size;
    int failures = 0;

    input = read_file("tests/unterminated_string.txt", &length);
    if(input) {
      test_parser_set_quiet(1);
      for(chunk_size = 1; chunk_size <= length + 1; chunk_size++) {
        if(test_streaming_parser(input, chunk_size, NULL) == 0) {
          fprintf(stderr, "\n  Accepted malformed input with %zu-byte chunks",
                  chunk_size);
          failures++;
        }
      }
      test_parser_set_quiet(0);
      free(input);
      if(failures)
        FAIL("Unterminated string input was accepted");
      else
        PASS();
    } else {
      FAIL("Could not read test file");
    }
  }

  /* Test 20: Error at EOF with cleanup validation */
  TEST("Error at EOF - validates cleanup (tests/error_at_eof.txt)");
  {
    char *input;
    size_t length;
    int result;
    statement_node *stmts_before;
    statement_node *stmts_after;

    input = read_file("tests/error_at_eof.txt", &length);
    if(input) {
      /* Reset parser state */
      test_parser_reset();

      /* Verify no statements exist before parsing */
      stmts_before = test_parser_get_statements();
      if(stmts_before != NULL) {
        FAIL("Statements exist before parsing");
        free(input);
      } else {
        /* Should detect error at EOF */
        result = test_streaming_parser(input, 1024, NULL);
        free(input);

        /* Verify error was detected */
        if(result >= 0) {
          FAIL("Expected parse error was not detected");
        } else {
          /* Verify statements were cleaned up after error */
          stmts_after = test_parser_get_statements();
          if(stmts_after != NULL) {
            FAIL("Statements not cleaned up after error");
          } else {
            /* Error detected and cleaned up properly */
            PASS();
          }
        }
      }
    } else {
      FAIL("Could not read test file");
    }
  }

  /* Test 21: Rewind API - commit, would block and rewind */
  TEST("fsp_buffer_commit/fsp_input_would_block/fsp_buffer_rewind");
  {
    int ok = 1;
    fsp_lexer_state state;
    fsp_lexer_state saved;

    ctx = fsp_create();
    if(!ctx) {
      FAIL("Failed to create FSP context");
    } else {
      /* Enable rewind support at the start of input */
      fsp_buffer_commit(ctx, 0, NULL);
      fsp_buffer_append(ctx, "abcdef", 6);

      /* Lexer reads everything, consumes 2 bytes as a token and still
       * has 4 bytes in its own buffer */
      bytes_read = fsp_read_input(ctx, buffer, sizeof(buffer));
      if(bytes_read != 6)
        ok = 0;
      state.start_condition = 7;
      state.at_bol = 0;
      state.lineno = 3;
      state.column = 5;
      fsp_buffer_commit(ctx, 4, &state);

      /* Input runs out with more chunks expected */
      if(fsp_input_would_block(ctx))
        ok = 0;
      if(fsp_read_input(ctx, buffer, sizeof(buffer)) != 0)
        ok = 0;
      if(!fsp_input_would_block(ctx))
        ok = 0;

      /* Rewind returns to the end of the token and the saved state */
      memset(&saved, 0, sizeof(saved));
      fsp_buffer_rewind(ctx, &saved);
      if(saved.start_condition != 7 || saved.at_bol != 0 ||
         saved.lineno != 3 || saved.column != 5)
        ok = 0;
      if(fsp_input_would_block(ctx))
        ok = 0;
      if(fsp_buffer_available(ctx) != 4)
        ok = 0;
      bytes_read = fsp_read_input(ctx, buffer, sizeof(buffer));
      if(bytes_read != 4 || memcmp(buffer, "cdef", 4) != 0)
        ok = 0;

      /* The mark never moves back before the previous commit */
      fsp_buffer_commit(ctx, 100, NULL);
      fsp_buffer_rewind(ctx, NULL);
      if(fsp_buffer_available(ctx) != 4)
        ok = 0;

      /* At the real end of input, running out does not block */
      ctx->more_chunks_expected = 0;
      while(fsp_read_input(ctx, buffer, sizeof(buffer)) > 0)
        ;
      if(fsp_read_input(ctx, buffer, sizeof(buffer)) != 0 ||
         fsp_input_would_block(ctx))
        ok = 0;

      if(ok)
        PASS();
      else
        FAIL("Rewind API returned unexpected values");
      fsp_destroy(ctx);
    }
  }

  /* Test 22: Compaction keeps uncommitted input for rewind */
  TEST("Buffer compaction keeps input after the commit mark");
  {
    size_t i;
    int ok = 1;

    ctx = fsp_create();
    large_size = 100 * 1024; /* more than the default buffer size */
    large_data = (char*)malloc(large_size);
    if(!ctx || !large_data) {
      FAIL("Failed to allocate test data");
    } else {
      for(i = 0; i < large_size; i++)
        large_data[i] = (char)('a' + (i % 26));

      fsp_buffer_commit(ctx, 0, NULL);
      fsp_buffer_append(ctx, large_data, 1000);

      /* Read all, commit after 10 bytes (990 unread by the lexer) then
       * append enough to compact */
      while(fsp_read_input(ctx, buffer, sizeof(buffer)) > 0)
        ;
      fsp_buffer_commit(ctx, 990, NULL);
      fsp_buffer_append(ctx, large_data + 1000, large_size - 1000);

      /* Rewind must return to byte 10, not to the compaction point */
      fsp_buffer_rewind(ctx, NULL);
      if(fsp_buffer_available(ctx) != large_size - 10)
        ok = 0;
      bytes_read = fsp_read_input(ctx, buffer, 26);
      if(bytes_read != 26 || memcmp(buffer, large_data + 10, 26) != 0)
        ok = 0;

      if(ok)
        PASS();
      else
        FAIL("Uncommitted input was lost by compaction");
    }
    free(large_data);
    fsp_destroy(ctx);
  }

  /* Test 23: Every chunk size gives the same result */
  TEST("Every chunk size from 1 byte to whole input (tests/*.txt)");
  {
    static const char * const files[][2] = {
      { "tests/simple.txt", "tests/simple.expected" },
      { "tests/triple-quoted.txt", "tests/triple-quoted.expected" },
      { "tests/mixed.txt", "tests/mixed.expected" },
      { "tests/empty.txt", "tests/empty.expected" },
      { "tests/long_string.txt", "tests/long_string.expected" },
      { "tests/long_tokens.txt", "tests/long_tokens.expected" },
      { "tests/comments.txt", "tests/comments.expected" }
    };
    size_t f;
    int failures = 0;

    for(f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
      char *input;
      size_t length;
      size_t chunk_size;
      int whole_lineno;

      input = read_file(files[f][0], &length);
      if(!input) {
        fprintf(stderr, "\n  Could not read %s", files[f][0]);
        failures++;
        continue;
      }

      /* Line number after parsing the whole input as one chunk */
      (void)test_streaming_parser(input, length + 1, NULL);
      whole_lineno = last_lineno;

      for(chunk_size = 1; chunk_size <= length + 1; chunk_size++) {
        if(test_streaming_parser(input, chunk_size, files[f][1]) < 0) {
          fprintf(stderr, "\n  %s failed with %zu-byte chunks",
                  files[f][0], chunk_size);
          failures++;
        } else if(last_lineno != whole_lineno) {
          fprintf(stderr, "\n  %s ended on line %d not %d with %zu-byte chunks",
                  files[f][0], last_lineno, whole_lineno, chunk_size);
          failures++;
        }
      }
      free(input);
    }

    if(failures) {
      fprintf(stderr, "\n");
      FAIL("Chunked parse differed from expected output");
    } else {
      PASS();
    }
  }

  /* Test 24: Long string bigger than the lexer and FSP buffers */
  TEST("Long string larger than the default FSP buffer");
  {
    static const size_t chunk_sizes[] = { 1000, 4096, 8192, 65536 };
    const size_t string_len = 150 * 1024;
    const char *prefix = "print \"\"\"";
    const char *suffix = "\"\"\";\nprint \"after\";\n";
    char *input;
    size_t input_len;
    size_t c;
    int failures = 0;

    input_len = strlen(prefix) + string_len + strlen(suffix);
    input = (char*)malloc(input_len + 1);
    if(!input) {
      FAIL("Failed to allocate test data");
    } else {
      strcpy(input, prefix);
      memset(input + strlen(prefix), 'x', string_len);
      strcpy(input + strlen(prefix) + string_len, suffix);

      for(c = 0; c < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); c++) {
        statement_node *stmt;

        if(run_streaming_parser(input, input_len, chunk_sizes[c]) < 0) {
          fprintf(stderr, "\n  Parse failed with %zu-byte chunks",
                  chunk_sizes[c]);
          failures++;
          continue;
        }

        stmt = test_parser_get_statements();
        if(!stmt || !stmt->value || strlen(stmt->value) != string_len ||
           strspn(stmt->value, "x") != string_len ||
           !stmt->next || !stmt->next->value ||
           strcmp(stmt->next->value, "after") || stmt->next->next) {
          fprintf(stderr, "\n  Wrong statements with %zu-byte chunks",
                  chunk_sizes[c]);
          failures++;
        }
        test_parser_free_statements();
      }
      free(input);

      if(failures) {
        fprintf(stderr, "\n");
        FAIL("Long string was not parsed correctly");
      } else {
        PASS();
      }
    }
  }

  /* Test 25: Beginning of line state is restored after a rewind */
  TEST("Mid-line # is an error at every chunk size");
  {
    /* The # follows a token on the same line, so it is not a comment.
     * If a rewind to the end of "1" set beginning of line, the ^#
     * comment rule would match and the input would wrongly parse. */
    const char *input = "let x = 1# not a comment\n;\n";
    size_t chunk_size;
    int failures = 0;

    test_parser_set_quiet(1);
    for(chunk_size = 1; chunk_size <= strlen(input) + 1; chunk_size++) {
      if(test_streaming_parser(input, chunk_size, NULL) == 0) {
        fprintf(stderr, "\n  Parsed with %zu-byte chunks", chunk_size);
        failures++;
      }
    }
    test_parser_set_quiet(0);

    if(failures) {
      fprintf(stderr, "\n");
      FAIL("Mid-line # was treated as a comment");
    } else {
      PASS();
    }
  }

  /* Test 26: Public chunk API status and EOF sequences */
  TEST("fsp_parse_chunk status and EOF sequences");
  {
    int ok = 1;

    ctx = fsp_create();
    if(!ctx) {
      FAIL("Failed to create FSP context");
    } else {
      if(fsp_parse_chunk(ctx, "last", 4, 1) != FSP_STATUS_OK ||
         ctx->more_chunks_expected || fsp_buffer_available(ctx) != 4)
        ok = 0;
      fsp_destroy(ctx);

      ctx = fsp_create();
      if(!ctx) {
        ok = 0;
      } else {
        if(fsp_parse_chunk(ctx, "part", 4, 0) != FSP_STATUS_NEED_DATA ||
           !ctx->more_chunks_expected ||
           fsp_parse_chunk(ctx, NULL, 0, 1) != FSP_STATUS_OK ||
           ctx->more_chunks_expected || fsp_buffer_available(ctx) != 4)
          ok = 0;
        fsp_destroy(ctx);
      }

      ctx = fsp_create();
      if(!ctx) {
        ok = 0;
      } else {
        fsp_buffer_commit(ctx, 0, NULL);
        if(fsp_parse_chunk(ctx, "abc", 3, 0) != FSP_STATUS_NEED_DATA ||
           fsp_read_input(ctx, buffer, sizeof(buffer)) != 3 ||
           fsp_read_input(ctx, buffer, sizeof(buffer)) != 0 ||
           !fsp_input_would_block(ctx))
          ok = 0;
        fsp_buffer_rewind(ctx, NULL);
        if(fsp_parse_chunk(ctx, "def", 3, 1) != FSP_STATUS_OK ||
           ctx->more_chunks_expected ||
           fsp_read_input(ctx, buffer, 6) != 6 ||
           memcmp(buffer, "abcdef", 6) != 0 ||
           fsp_input_would_block(ctx))
          ok = 0;
        fsp_destroy(ctx);
      }

      if(ok)
        PASS();
      else
        FAIL("fsp_parse_chunk returned unexpected status or EOF state");
    }
  }

  /* Test 27: Compile and execute fsp-helper.py generated parser code */
  TEST("Generated streaming parser parses one-byte chunks");
  {
    int result;
    statement_node *stmt;

    test_parser_reset();
    ctx = fsp_create();
    if(!ctx) {
      FAIL("Failed to create FSP context");
    } else {
      result = test_generated_parse(ctx, "print \"generated\";\n", 1, NULL);
      stmt = test_parser_get_statements();
      if(result || !stmt || stmt->type != STMT_PRINT || !stmt->value ||
         strcmp(stmt->value, "generated") || stmt->next) {
        FAIL("Generated streaming parser returned unexpected output");
      } else {
        PASS();
      }
      test_parser_free_statements();
      fsp_destroy(ctx);
    }
  }

  /* Test 28: Uneven chunks, empty chunks, and separate EOF notification */
  TEST("Irregular chunk partitions match whole-input parsing");
  {
    static const size_t patterns[][7] = {
      { 3, 0, 1, 5, 2, 0, 7 },
      { 2, 0, 9, 1, 0, 4, 3 }
    };
    static const char * const files[][2] = {
      { "tests/mixed.txt", "tests/mixed.expected" },
      { "tests/triple-quoted.txt", "tests/triple-quoted.expected" }
    };
    size_t f;
    int failures = 0;

    for(f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
      char *input;
      size_t input_len;
      size_t pattern_index;
      size_t whole_lineno;

      input = read_file(files[f][0], &input_len);
      if(!input) {
        failures++;
        continue;
      }
      if(run_streaming_parser(input, input_len, input_len + 1) < 0) {
        failures++;
        free(input);
        continue;
      }
      whole_lineno = last_lineno;
      test_parser_free_statements();

      for(pattern_index = 0; pattern_index < 2; pattern_index++) {
        const size_t *pattern;
        size_t pattern_len = 7;
        size_t *plan;
        size_t plan_capacity;
        size_t plan_count = 0;
        size_t plan_pos = 0;
        size_t input_pos = 0;

        pattern = patterns[pattern_index];
        plan_capacity = (input_len + 1) * 2;
        plan = (size_t*)malloc(plan_capacity * sizeof(size_t));
        if(!plan) {
          failures++;
          continue;
        }

        while(input_pos < input_len) {
          size_t chunk = pattern[plan_pos++ % pattern_len];
          if(chunk > input_len - input_pos)
            chunk = input_len - input_pos;
          plan[plan_count++] = chunk;
          input_pos += chunk;
        }

        if(run_streaming_parser_plan(input, input_len, 1, plan, plan_count,
                                     1) < 0 ||
           validate_parse_result(files[f][1]) < 0 ||
           last_lineno != (int)whole_lineno) {
          fprintf(stderr, "\n  %s failed for irregular partition %zu",
                  files[f][0], pattern_index + 1);
          failures++;
        }
        test_parser_free_statements();
        free(plan);
      }
      free(input);
    }

    if(failures)
      FAIL("Irregular streaming differed from whole-input parsing");
    else
      PASS();
  }

  /* Test 29: Flex yymore() and exclusive-state EOF actions */
  TEST("yymore and custom EOF rules survive chunking");
  {
    const char *input = "print joined;\n";
    size_t chunk_size;
    int failures = 0;

    test_parser_set_quiet(1);
    for(chunk_size = 1; chunk_size <= strlen(input) + 1; chunk_size++) {
      statement_node *stmt;
      if(run_streaming_parser(input, strlen(input), chunk_size) < 0) {
        failures++;
        continue;
      }
      stmt = test_parser_get_statements();
      if(!stmt || !stmt->value || strcmp(stmt->value, "joined") ||
         stmt->next)
        failures++;
      test_parser_free_statements();
    }
    test_parser_set_quiet(0);

    if(failures)
      FAIL("Flex rule actions changed under chunking");
    else
      PASS();
  }

  /* Test 30: The generated rewind hook restores the Flex column state */
  TEST("Rewind restores the committed Flex column");
  {
    int ok = 1;
    TEST_PARSER_STYPE lval;
    TEST_PARSER_LTYPE lloc;
    yyscan_t scanner;

    ctx = fsp_create();
    if(!ctx || test_lexer_lex_init(&scanner)) {
      FAIL("Failed to initialize lexer for column restoration");
      fsp_destroy(ctx);
    } else {
      test_lexer_set_extra(ctx, scanner);
      test_lexer_fsp_commit(scanner);
      if(fsp_parse_chunk(ctx, " ", 1, 0) != FSP_STATUS_NEED_DATA)
        ok = 0;
      memset(&lval, 0, sizeof(lval));
      memset(&lloc, 0, sizeof(lloc));
      if(test_lexer_lex(&lval, &lloc, scanner) != FSP_LEXER_NEED_MORE)
        ok = 0;
      free(lval.string);
      test_lexer_fsp_rewind(scanner);
      test_lexer_set_column(7, scanner);
      test_lexer_fsp_commit(scanner);
      if(fsp_parse_chunk(ctx, "\"\"\"partial", 10, 0) !=
         FSP_STATUS_NEED_DATA)
        ok = 0;
      memset(&lval, 0, sizeof(lval));
      memset(&lloc, 0, sizeof(lloc));
      if(test_lexer_lex(&lval, &lloc, scanner) != FSP_LEXER_NEED_MORE)
        ok = 0;
      free(lval.string);
      test_lexer_set_column(99, scanner);
      test_lexer_fsp_rewind(scanner);
      if(test_lexer_get_column(scanner) != 7)
        ok = 0;
      test_lexer_lex_destroy(scanner);
      fsp_destroy(ctx);
      if(ok)
        PASS();
      else
        FAIL("Lexer column changed across rewind");
    }
  }

  /* Test 31: Token locations agree for whole and chunked input */
  TEST("Statement, identifier, and value locations survive chunking");
  {
    const char *input = "print \"x\";\nlet y = 2;\n";
    size_t chunk_size;
    int failures = 0;

    for(chunk_size = 1; chunk_size <= strlen(input) + 1; chunk_size++) {
      statement_node *stmt;
      if(run_streaming_parser(input, strlen(input), chunk_size) < 0) {
        failures++;
        continue;
      }

      stmt = test_parser_get_statements();
      if(!stmt || stmt->line != 1 || stmt->column != 0 ||
         stmt->value_line != 1 || stmt->value_column != 6 ||
         !stmt->next || stmt->next->line != 2 || stmt->next->column != 0 ||
         stmt->next->identifier_line != 2 ||
         stmt->next->identifier_column != 4 ||
         stmt->next->value_line != 2 || stmt->next->value_column != 8 ||
         stmt->next->next) {
        fprintf(stderr, "\n  Wrong token locations with %zu-byte chunks",
                chunk_size);
        failures++;
      }
      test_parser_free_statements();
    }

    if(failures)
      FAIL("Token locations differed across chunk boundaries");
    else
      PASS();
  }

  /* Test 32: Binary bytes remain intact across compaction and growth */
  TEST("Binary buffers survive repeated compaction and growth independently");
  {
    const size_t total = 180 * 1024;
    const size_t first = 60 * 1024;
    const size_t consumed1 = 10 * 1024;
    const size_t consumed2 = 20 * 1024;
    char *source;
    char *alternate;
    char *readback1;
    char *readback2;
    size_t i;
    size_t offset = 0;
    int ok = 1;
    fsp_context *ctx1;
    fsp_context *ctx2;

    source = (char*)malloc(total);
    alternate = (char*)malloc(total);
    readback1 = (char*)malloc(total - consumed1 - consumed2);
    readback2 = (char*)malloc(total - consumed1 - consumed2);
    ctx1 = fsp_create();
    ctx2 = fsp_create();
    if(!source || !alternate || !readback1 || !readback2 || !ctx1 || !ctx2) {
      FAIL("Failed to allocate independent binary buffer test state");
    } else {
      for(i = 0; i < total; i++) {
        source[i] = (char)(i & 0xff);
        alternate[i] = (char)((i ^ 0xa5) & 0xff);
      }

      if(fsp_buffer_append(ctx1, source, first) < 0 ||
         fsp_buffer_append(ctx2, alternate, first) < 0)
        ok = 0;
      for(i = 0; i < consumed1; i += sizeof(buffer)) {
        size_t count = consumed1 - i;
        if(count > sizeof(buffer))
          count = sizeof(buffer);
        if(fsp_read_input(ctx1, buffer, count) != (int)count ||
           memcmp(buffer, source + i, count) != 0 ||
           fsp_read_input(ctx2, buffer, count) != (int)count ||
           memcmp(buffer, alternate + i, count) != 0)
          ok = 0;
      }
      fsp_buffer_compact(ctx1);
      fsp_buffer_compact(ctx2);
      if(fsp_buffer_append(ctx1, source + first, total - first) < 0 ||
         fsp_buffer_append(ctx2, alternate + first, total - first) < 0)
        ok = 0;
      for(i = 0; i < consumed2; i += sizeof(buffer)) {
        size_t count = consumed2 - i;
        if(count > sizeof(buffer))
          count = sizeof(buffer);
        if(fsp_read_input(ctx1, buffer, count) != (int)count ||
           memcmp(buffer, source + consumed1 + i, count) != 0 ||
           fsp_read_input(ctx2, buffer, count) != (int)count ||
           memcmp(buffer, alternate + consumed1 + i, count) != 0)
          ok = 0;
      }
      fsp_buffer_compact(ctx1);
      fsp_buffer_compact(ctx2);

      while(offset < total - consumed1 - consumed2) {
        size_t count = fsp_read_input(ctx1, readback1 + offset,
                                      total - consumed1 - consumed2 - offset);
        if(fsp_read_input(ctx2, readback2 + offset, count) != (int)count)
          ok = 0;
        if(!count)
          break;
        offset += count;
      }
      if(offset != total - consumed1 - consumed2 ||
         memcmp(readback1, source + consumed1 + consumed2, offset) != 0 ||
         memcmp(readback2, alternate + consumed1 + consumed2, offset) != 0 ||
         fsp_buffer_available(ctx2) != 0)
        ok = 0;

      if(ok)
        PASS();
      else
        FAIL("Compaction or growth changed binary buffer contents");
    }
    free(source);
    free(alternate);
    free(readback1);
    free(readback2);
    fsp_destroy(ctx1);
    fsp_destroy(ctx2);
  }

  TEST("Retry scheduling handles the cutoff, doubling, commit and EOF");
  {
    char data[1024];
    char readback[1024];
    int ok = 1;

    memset(data, 'x', sizeof(data));
    ctx = fsp_create();
    if(!ctx) {
      FAIL("Failed to allocate retry test context");
    } else {
      fsp_buffer_commit(ctx, 0, NULL);
      if(fsp_input_ready(ctx) || fsp_input_ready(NULL))
        ok = 0;

      /* Small unfinished input remains eager. */
      if(fsp_parse_chunk(ctx, data, 255, 0) != FSP_STATUS_NEED_DATA ||
         !fsp_input_ready(ctx) || fsp_read_input(ctx, readback, 255) != 255 ||
         fsp_read_input(ctx, readback, 1) != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      if(!fsp_input_ready(ctx) || ctx->lexer_retry_size != 0)
        ok = 0;

      /* Exactly 256 retained bytes need another 256 before retrying. */
      if(fsp_parse_chunk(ctx, data, 1, 0) != FSP_STATUS_NEED_DATA ||
         fsp_read_input(ctx, readback, 256) != 256 ||
         fsp_read_input(ctx, readback, 1) != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      if(fsp_input_ready(ctx) || ctx->lexer_retry_size != 512 ||
         fsp_input_would_block(ctx))
        ok = 0;
      if(fsp_parse_chunk(ctx, data, 255, 0) != FSP_STATUS_NEED_DATA ||
         fsp_input_ready(ctx) ||
         fsp_parse_chunk(ctx, data, 1, 0) != FSP_STATUS_NEED_DATA ||
         !fsp_input_ready(ctx))
        ok = 0;

      if(fsp_read_input(ctx, readback, 512) != 512 ||
         fsp_read_input(ctx, readback, 1) != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      if(fsp_input_ready(ctx) || ctx->lexer_retry_size != 1024)
        ok = 0;

      /* Commit clears the wait; an explicit, unblocked rewind adds none. */
      fsp_buffer_commit(ctx, 0, NULL);
      if(!fsp_input_ready(ctx) || ctx->lexer_retry_size != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      if(!fsp_input_ready(ctx) || ctx->lexer_retry_size != 0)
        ok = 0;

      if(fsp_read_input(ctx, readback, 512) != 512 ||
         fsp_read_input(ctx, readback, 1) != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      if(fsp_input_ready(ctx) ||
         fsp_parse_chunk(ctx, NULL, 0, 1) != FSP_STATUS_OK ||
         !fsp_input_ready(ctx))
        ok = 0;

      if(ok)
        PASS();
      else
        FAIL("Retry scheduling changed cutoff, growth, reset or EOF behavior");
      fsp_destroy(ctx);
    }
  }

  TEST("Retry thresholds survive compaction and saturate on overflow");
  {
    char data[1024];
    char readback[1024];
    int ok = 1;

    memset(data, 'x', sizeof(data));
    ctx = fsp_create();
    if(!ctx) {
      FAIL("Failed to allocate retry compaction test context");
    } else {
      fsp_buffer_commit(ctx, 0, NULL);
      if(fsp_parse_chunk(ctx, data, 1024, 0) != FSP_STATUS_NEED_DATA ||
         fsp_read_input(ctx, readback, 1024) != 1024)
        ok = 0;
      fsp_buffer_commit(ctx, 256, NULL);
      if(fsp_read_input(ctx, readback, 1) != 0)
        ok = 0;
      fsp_buffer_rewind(ctx, NULL);
      fsp_buffer_compact(ctx);
      if(fsp_input_ready(ctx) || ctx->lexer_retry_size != 512 ||
         fsp_parse_chunk(ctx, data, 256, 0) != FSP_STATUS_NEED_DATA ||
         !fsp_input_ready(ctx) || fsp_buffer_available(ctx) != 512)
        ok = 0;

      /* Synthetic sizes test overflow without an impossible allocation.
       * No input is read or appended while these sizes are installed. */
      ctx->data_length = (size_t)-1 / 2 + 1;
      ctx->read_position = ctx->data_length;
      ctx->mark_position = 0;
      ctx->input_would_block = 1;
      fsp_buffer_rewind(ctx, NULL);
      if(ctx->lexer_retry_size != (size_t)-1 || fsp_input_ready(ctx))
        ok = 0;
      ctx->data_length = 0;
      ctx->read_position = 0;
      if(fsp_parse_chunk(ctx, NULL, 0, 1) != FSP_STATUS_OK ||
         !fsp_input_ready(ctx))
        ok = 0;

      if(ok)
        PASS();
      else
        FAIL("Retry threshold changed after compaction or overflowed");
      fsp_destroy(ctx);
    }
  }

  TEST("Long tokens retain values and locations with bounded lexer retries");
  {
    const size_t lengths[] = { 64 * 1024, 1024 * 1024 };
    const size_t chunks[] = { 1, 25, 4096 };
    size_t l, c;
    int triple, separate_eof;
    int failures = 0;

    for(l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
      for(triple = 0; triple <= 1; triple++) {
        const char *prefix = triple ? "print \"\"\"" : "print \"";
        const char *suffix = triple ? "\"\"\";\nprint \"after\";\n" :
                                     "\";\nprint \"after\";\n";
        size_t prefix_len = strlen(prefix);
        size_t input_len = prefix_len + lengths[l] + strlen(suffix);
        char *input = (char*)malloc(input_len + 1);
        int expected_value_line;
        int expected_value_column;
        if(!input) {
          failures++;
          continue;
        }
        memcpy(input, prefix, prefix_len);
        memset(input + prefix_len, 'x', lengths[l]);
        memcpy(input + prefix_len + lengths[l], suffix, strlen(suffix) + 1);

        /* Compare locations to a whole-input parse. Triple-quoted values
         * use the closing rule's location in this test language. */
        if(run_streaming_parser_plan(input, input_len, input_len,
                                      NULL, 0, 0) < 0 ||
           !test_parser_get_statements()) {
          failures++;
          free(input);
          continue;
        }
        expected_value_line = test_parser_get_statements()->value_line;
        expected_value_column = test_parser_get_statements()->value_column;
        test_parser_free_statements();

        for(c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
          for(separate_eof = 0; separate_eof <= 1; separate_eof++) {
            statement_node *stmt;
            if(run_streaming_parser_plan(input, input_len, chunks[c],
                                          NULL, 0, separate_eof) < 0) {
              failures++;
              continue;
            }
            stmt = test_parser_get_statements();
            if(last_lexer_batches > 512 || !stmt || !stmt->value ||
               strlen(stmt->value) != lengths[l] ||
               memcmp(stmt->value, input + prefix_len, lengths[l]) != 0 ||
               stmt->value_line != expected_value_line ||
               stmt->value_column != expected_value_column ||
               !stmt->next || stmt->next->line != 2 ||
               strcmp(stmt->next->value, "after") || stmt->next->next) {
              fprintf(stderr, "\n  Failed %zu-byte %s token, chunks=%zu, "
                      "separate EOF=%d, batches=%zu", lengths[l],
                      triple ? "triple-quoted" : "quoted", chunks[c],
                      separate_eof, last_lexer_batches);
              failures++;
            }
            test_parser_free_statements();
          }
        }

        /* The generated integration must use the same scheduling API. */
        test_parser_reset();
        ctx = fsp_create();
        if(!ctx || test_generated_parse(ctx, input, 1, NULL) != 0) {
          failures++;
        } else {
          statement_node *stmt = test_parser_get_statements();
          if(!stmt || !stmt->value || strlen(stmt->value) != lengths[l] ||
             memcmp(stmt->value, input + prefix_len, lengths[l]) != 0 ||
             !stmt->next || strcmp(stmt->next->value, "after") ||
             stmt->next->next)
            failures++;
        }
        test_parser_free_statements();
        fsp_destroy(ctx);

        /* EOF must flush a throttled unterminated token and report failure,
         * whether EOF accompanies the data or arrives in an empty chunk. */
        input[prefix_len + lengths[l]] = '\0';
        test_parser_set_quiet(1);
        for(separate_eof = 0; separate_eof <= 1; separate_eof++) {
          if(run_streaming_parser_plan(input, prefix_len + lengths[l], 25,
                                        NULL, 0, separate_eof) == 0)
            failures++;
        }
        test_parser_reset();
        ctx = fsp_create();
        if(!ctx || test_generated_parse(ctx, input, 25, NULL) == 0)
          failures++;
        test_parser_free_statements();
        fsp_destroy(ctx);
        test_parser_set_quiet(0);
        free(input);
      }
    }

    if(failures)
      FAIL("Long token values, locations or retry bounds differed");
    else
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
