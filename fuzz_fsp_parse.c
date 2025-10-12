/*
 * fuzz_fsp_parse.c - libFuzzer harness for libfsp streaming parser
 *
 * Purpose:
 *  - Exercise the streaming parser with mutated inputs to discover crashes,
 *    memory leaks, buffer overflows, and undefined behavior.
 *  - Derives chunk size from first bytes, then feeds the rest as data in
 *    varying chunks to stress-test token boundary handling.
 *
 * Notes:
 *  - This harness validates that parsing completes without crashing or
 *    triggering sanitizers (ASan, UBSan).
 *  - Build with Clang + libFuzzer and sanitizers (see GNUMakefile targets).
 *
 * Copyright (C) 2025, Dave Beckett http://www.dajobe.org/
 * 
 * This package is Free Software
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fsp.h"
#include "test_parser.h"

/* Define YYSTYPE for lexer header */
#define YYSTYPE TEST_PARSER_STYPE
#include "test_lexer.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fsp_context *ctx;
  yyscan_t scanner;
  test_parser_pstate *pstate;
  const uint8_t *p;
  size_t remain;
  size_t chunk_base;
  int status;

  if (!data || size == 0)
    return 0;

  /* Reset parser state */
  test_parser_reset();

  /* Create FSP context */
  ctx = fsp_create();
  if(!ctx)
    return 0;

  /* Initialize lexer */
  if(test_lexer_lex_init(&scanner)) {
    fsp_destroy(ctx);
    return 0;
  }

  /* Set FSP context as extra data for lexer */
  test_lexer_set_extra(ctx, scanner);

  /* Create push parser state */
  pstate = test_parser_pstate_new();
  if(!pstate) {
    test_lexer_lex_destroy(scanner);
    fsp_destroy(ctx);
    return 0;
  }

  /* Derive chunk size from first byte (1-64 bytes)
   * This tests various token boundary conditions */
  chunk_base = (size > 0) ? ((size_t)data[0] % 64) + 1 : 16;
  
  /* Start feeding from second byte */
  p = (size > 1) ? data + 1 : data;
  remain = (size > 1) ? size - 1 : 0;

  /* Feed input in varying chunks to stress streaming */
  while(remain > 0) {
    size_t chunk;
    size_t vary;
    int is_end;
    
    /* Vary chunk size slightly to hit different boundary conditions */
    vary = (remain > 2 && p[0] > 0) ? (p[0] % 8) : 0;
    chunk = chunk_base + vary;
    if(chunk > remain)
      chunk = remain;

    is_end = (chunk >= remain);

    /* Append chunk to FSP buffer */
    if(fsp_buffer_append(ctx, (const char*)p, chunk) < 0)
      break;

    /* Feed tokens to parser */
    while(fsp_buffer_available(ctx) > 0 || is_end) {
      TEST_PARSER_STYPE lval;
      int token;

      /* Initialize lval to avoid using uninitialized memory */
      memset(&lval, 0, sizeof(lval));

      /* Get next token from lexer */
      token = test_lexer_lex(&lval, scanner);

      if(token == 0) {
        /* No more tokens available */
        /* Free any partially constructed string */
        if(lval.string) {
          free(lval.string);
          lval.string = NULL;
        }
        if(is_end) {
          /* EOF - push EOF token to parser */
          (void)test_parser_push_parse(pstate, 0, NULL, ctx, scanner);
          goto done;
        }
        /* Need more data */
        break;
      }

      if(token == ERROR) {
        /* Free any allocated string from lval before exiting */
        if(lval.string)
          free(lval.string);
        goto done;
      }

      /* Push token to parser */
      status = test_parser_push_parse(pstate, token, &lval, ctx, scanner);

      if(status != YYPUSH_MORE) {
        /* Parser failed or completed. If it failed, it may not have consumed
         * the token, so we need to free any string in lval. Bison push parser
         * returns YYABORT (1) or YYACCEPT (0) but not YYPUSH_MORE (4) on error. */
        if(status != 0 && lval.string) {
          /* Parse error - free the unconsumed token */
          free(lval.string);
        }
        goto done;
      }
    }

    p += chunk;
    remain -= chunk;
  }

  /* Push final EOF */
  (void)test_parser_push_parse(pstate, 0, NULL, ctx, scanner);

done:
  /* Clean up parser state. The %destructor should handle cleanup of any
   * semantic values still on the stack, but this doesn't always work
   * correctly with Bison push parsers during error recovery.
   * See: https://www.gnu.org/software/bison/manual/html_node/Destructor-Decl.html */
  test_parser_pstate_delete(pstate);
  test_lexer_lex_destroy(scanner);
  fsp_destroy(ctx);
  test_parser_free_statements();

  return 0;
}

