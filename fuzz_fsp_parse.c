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
 * Copyright (C) 2025, Dave Beckett https://www.dajobe.org/
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
#define YYLTYPE TEST_PARSER_LTYPE
#include "test_lexer.h"

typedef struct {
  int valid;
  int accepted;
  int final_line;
  uint64_t ast_hash;
} fuzz_snapshot;

static uint64_t
hash_ast(void)
{
  const uint64_t prime = UINT64_C(1099511628211);
  uint64_t hash = UINT64_C(1469598103934665603);
  statement_node *stmt = test_parser_get_statements();

  while(stmt) {
    const unsigned char *text;

    hash = (hash ^ (uint64_t)stmt->type) * prime;
    hash = (hash ^ (uint64_t)stmt->line) * prime;
    hash = (hash ^ (uint64_t)stmt->column) * prime;
    hash = (hash ^ (uint64_t)stmt->identifier_line) * prime;
    hash = (hash ^ (uint64_t)stmt->identifier_column) * prime;
    hash = (hash ^ (uint64_t)stmt->value_line) * prime;
    hash = (hash ^ (uint64_t)stmt->value_column) * prime;
    for(text = (const unsigned char*)stmt->identifier; text && *text; text++)
      hash = (hash ^ *text) * prime;
    hash = (hash ^ UINT64_C(0xff)) * prime;
    for(text = (const unsigned char*)stmt->value; text && *text; text++)
      hash = (hash ^ *text) * prime;
    hash = (hash ^ UINT64_C(0xfe)) * prime;
    stmt = stmt->next;
  }
  return hash;
}

static int
run_parse_case(const uint8_t *data, size_t size, size_t chunk_base,
               int whole_input, fuzz_snapshot *snapshot)
{
  fsp_context *ctx;
  yyscan_t scanner;
  test_parser_pstate *pstate;
  TEST_PARSER_LTYPE eof_loc;
  const uint8_t *p = data;
  size_t remain;
  int status;
  int parse_status = 1;

  snapshot->valid = 0;
  snapshot->accepted = 0;
  snapshot->final_line = 0;
  snapshot->ast_hash = 0;
  test_parser_set_quiet(1);
  test_parser_reset();
  ctx = fsp_create();
  if(!ctx)
    return -1;
  if(test_lexer_lex_init(&scanner)) {
    fsp_destroy(ctx);
    return -1;
  }
  test_lexer_set_extra(ctx, scanner);
  pstate = test_parser_pstate_new();
  if(!pstate) {
    test_lexer_lex_destroy(scanner);
    fsp_destroy(ctx);
    return -1;
  }

  test_lexer_fsp_commit(scanner);
  memset(&eof_loc, 0, sizeof(eof_loc));
  remain = size;
  while(remain > 0) {
    size_t chunk;
    size_t vary;
    int is_end;

    if(whole_input) {
      chunk = remain;
    } else {
      vary = (remain > 2 && p[0] > 0) ? (p[0] % 8) : 0;
      chunk = chunk_base + vary;
      if(chunk > remain)
        chunk = remain;
    }

    is_end = (chunk >= remain);
    if(fsp_parse_chunk(ctx, (const char*)p, chunk, is_end) == FSP_STATUS_NO_MEMORY)
      break;

    if(!fsp_input_ready(ctx)) {
      p += chunk;
      remain -= chunk;
      continue;
    }

    while(1) {
      TEST_PARSER_STYPE lval;
      TEST_PARSER_LTYPE lloc;
      int token;

      memset(&lval, 0, sizeof(lval));
      memset(&lloc, 0, sizeof(lloc));
      token = test_lexer_lex(&lval, &lloc, scanner);

      if(token == FSP_LEXER_NEED_MORE ||
         (!token && fsp_input_would_block(ctx))) {
        if(lval.string) {
          free(lval.string);
          lval.string = NULL;
        }
        test_lexer_fsp_rewind(scanner);
        break;
      }

      if(token == 0) {
        if(lval.string) {
          free(lval.string);
          lval.string = NULL;
        }
        if(is_end) {
          status = test_parser_push_parse(pstate, 0, NULL, &lloc, ctx, scanner);
          parse_status = status == 0 ? 0 : 1;
          goto done;
        }
        break;
      }

      test_lexer_fsp_commit(scanner);

      if(token == ERROR) {
        if(lval.string)
          free(lval.string);
        (void)test_parser_push_parse(pstate, 0, NULL, &lloc, ctx, scanner);
        parse_status = 1;
        goto done;
      }

      /* Push token to parser */
      status = test_parser_push_parse(pstate, token, &lval, &lloc,
                                      ctx, scanner);

      if(status != YYPUSH_MORE) {
        if(status != 0 && lval.string) {
          free(lval.string);
        }
        parse_status = status == 0 ? 0 : 1;
        goto done;
      }
    }

    p += chunk;
    remain -= chunk;
  }

  status = test_parser_push_parse(pstate, 0, NULL, &eof_loc, ctx, scanner);
  parse_status = status == 0 ? 0 : 1;

done:
  snapshot->valid = 1;
  snapshot->accepted = parse_status == 0;
  snapshot->final_line = test_lexer_get_lineno(scanner);
  snapshot->ast_hash = hash_ast();
  test_parser_pstate_delete(pstate);
  test_lexer_lex_destroy(scanner);
  fsp_destroy(ctx);
  test_parser_free_statements();
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  fuzz_snapshot whole;
  fuzz_snapshot chunked;
  size_t chunk_base;
  size_t input_size;
  const uint8_t *input;

  if(!data || size == 0)
    return 0;

  chunk_base = ((size_t)data[0] % 64) + 1;
  input = size > 1 ? data + 1 : data;
  input_size = size > 1 ? size - 1 : 0;

  if(run_parse_case(input, input_size, chunk_base, 1, &whole) < 0 ||
     run_parse_case(input, input_size, chunk_base, 0, &chunked) < 0)
    return 0;

  if(whole.valid != chunked.valid || whole.accepted != chunked.accepted ||
     whole.ast_hash != chunked.ast_hash ||
     whole.final_line != chunked.final_line)
    __builtin_trap();

  return 0;
}
