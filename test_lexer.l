/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_lexer.l - libfsp test lexer - demonstrates streaming with Flex
 *
 * Lexer for a simple toy language with:
 * - Keywords: print, let
 * - String literals (single and triple-quoted for multi-line)
 * - Identifiers and integers
 * - Triple-quoted strings test streaming across chunk boundaries
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
 */

/* recognise 8-bits */
%option 8bit
%option warn nodefault

/* all symbols prefixed by this */
%option prefix="test_lexer_"

/* Emit a C header file for prototypes */
%option header-file="test_lexer.h"

/* Do not emit #include <unistd.h> */
%option nounistd

/* Never interactive */
%option never-interactive

/* Batch scanner */
%option batch

/* Never use yyunput */
%option nounput

/* Supply our own alloc/realloc/free functions */
%option noyyalloc noyyrealloc noyyfree

/* Re-entrant scanner */
%option reentrant

%option extra-type="fsp_context*"

/* Makes yyget_lval() yyset_lval() and yylval appear */
%option bison-bridge
%option bison-locations

/* Track line numbers, to test that rewind restores them */
%option yylineno

  /* definitions */

%{
#ifdef HAVE_FSP_CONFIG_H
#include <fsp_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* strdup is POSIX but not in C11 standard - provide prototype */
char *strdup(const char *s);

#include "fsp.h"
#include "fsp_internal.h"
#include "test_parser.h"

#define YYSTYPE TEST_PARSER_STYPE
#define YYLTYPE TEST_PARSER_LTYPE

/* Prototypes */
static char *test_copy_string(const char *text, size_t len);
static void test_lexer_error_handler(void *yyscanner, const char *msg);

/* Forward declaration of cleanup function for postprocess-flex.py */
static void test_lexer_cleanup(yyscan_t yyscanner);
#undef yycleanup
#define yycleanup test_lexer_cleanup

/* Custom fatal error handler - must be defined before YY_FATAL_ERROR is used */
#define YY_FATAL_ERROR(msg) do { \
    test_lexer_error_handler(yyscanner, msg); \
    abort(); \
} while(0)

/* Enable YY_INPUT for streaming - THIS IS THE KEY CHANGE */
#define YY_INPUT(buf,result,max_size) \
  result = fsp_read_input(yyextra, buf, max_size)

/* Return FSP_LEXER_NEED_MORE instead of a token cut short by the end of
 * the available input, and count matched bytes for rewind support */
#define YY_USER_ACTION do { \
  size_t fsp_location_i; \
  size_t fsp_location_start; \
  FSP_LEXER_USER_ACTION(yyextra) \
  yylloc->first_line = yylineno; \
  yylloc->last_line = yylineno; \
  yylloc->first_column = yycolumn; \
  yylloc->last_column = yycolumn + (int)yyleng; \
  fsp_location_start = (size_t)YY_MORE_ADJ; \
  for(fsp_location_i = fsp_location_start; \
      fsp_location_i < (size_t)yyleng; fsp_location_i++) { \
    if(yytext[fsp_location_i] == '\n') \
      yycolumn = 0; \
    else \
      yycolumn++; \
  } \
} while(0);

/* Do not need input() */
#define YY_NO_INPUT 1

%}

/* Token patterns */
DIGIT    [0-9]
LETTER   [A-Za-z]
IDENT    {LETTER}({LETTER}|{DIGIT}|_)*
INTEGER  {DIGIT}+
WS       [ \t\r\n]+

/* Start conditions for long strings */
%x LONG_STRING

%%
  /* rules */

{WS}     { /* skip whitespace */ }

  /* Comment only at the beginning of a line, to test that rewind
   * restores the beginning of line state.  A # elsewhere is an error. */
^"#"[^\n]*\n { /* skip comment */ }

  /* Keywords */
"print"  { return PRINT; }
"let"    { return LET; }
  /* Give back the whitespace with yyless(), to test that rewind does not
   * count it as consumed */
"let"{WS} {
    int matched_len = (int)yyleng;
    yyless(3);
    yycolumn -= matched_len - 3;
    yylloc->last_column = yylloc->first_column + (int)yyleng;
    return LET;
}
"="      { return EQUALS; }
";"      { return SEMICOLON; }

  /* Regular string literals */
\"([^\"\\\n]|\\.)*\"  {
    yylval->string = test_copy_string(yytext+1, yyleng-2);
    if(!yylval->string)
      return ERROR;
    return STRING;
}

  /* Triple-quoted strings - THE STREAMING CHALLENGE */
\"\"\"  {
    BEGIN(LONG_STRING);
    /* Start accumulating the long string */
    yylval->string = strdup("");
    if(!yylval->string)
      return ERROR;
}

<LONG_STRING>\"\"\" {
    /* End of long string */
    BEGIN(INITIAL);
    return STRING;
}

<LONG_STRING><<EOF>> {
    if(fsp_input_would_block(yyextra))
      return FSP_LEXER_NEED_MORE;
    /* ERROR has no semantic destructor in the example grammar. */
    free(yylval->string);
    yylval->string = NULL;
    /* Let a recovering push parser see real EOF on the next lexer call. */
    BEGIN(INITIAL);
    return ERROR;
}

<LONG_STRING>[^\"]+|\" {
    /* Accumulate text in long string.  With rewind support the whole
     * string is matched within one lexer call, so yylval->string is
     * always the value started by the opening quotes. */
    size_t old_len;
    char *new_str;

    old_len = strlen(yylval->string);
    new_str = FSP_MALLOC(char*, old_len + (size_t)yyleng + 1);
    if(!new_str)
      return ERROR;
    memcpy(new_str, yylval->string, old_len);
    memcpy(new_str + old_len, yytext, (size_t)yyleng);
    new_str[old_len + (size_t)yyleng] = '\0';
    free(yylval->string);
    yylval->string = new_str;
}

  /* Identifiers */
"join" { yymore(); }
"ed"  {
    yylval->string = test_copy_string(yytext, (size_t)yyleng);
    if(!yylval->string)
      return ERROR;
    return IDENTIFIER;
}
{IDENT}  {
    yylval->string = test_copy_string(yytext, yyleng);
    if(!yylval->string)
      return ERROR;
    return IDENTIFIER;
}

  /* Integer literals */
{INTEGER} {
    yylval->integer = atoi(yytext);
    return INTEGER;
}

  /* Catch-all for errors */
.        { return ERROR; }

%%

/* Copy a string token */
static char *
test_copy_string(const char *text, size_t len)
{
  char *result = FSP_MALLOC(char*, len + 1);
  if(!result)
    return NULL;
  memcpy(result, text, len);
  result[len] = '\0';
  return result;
}

/* Fatal error handler */
static void
test_lexer_error_handler(void *yyscanner, const char *msg)
{
  (void)yyscanner;
  fprintf(stderr, "Lexer fatal error: %s\n", msg);
}

/* Required memory management functions */
void *
test_lexer_alloc(size_t bytes, void *yyscanner)
{
  (void)yyscanner;
  return malloc(bytes);
}

void *
test_lexer_realloc(void *ptr, size_t bytes, void *yyscanner)
{
  (void)yyscanner;
  return realloc(ptr, bytes);
}

void
test_lexer_free(void *ptr, void *yyscanner)
{
  (void)yyscanner;
  free(ptr);
}

/* Cleanup function called by postprocess-flex.py injected code */
static void
test_lexer_cleanup(yyscan_t yyscanner)
{
  /* Nothing to clean up currently */
  (void)yyscanner;
}

/* Wrap function - called at EOF */
int
test_lexer_wrap(yyscan_t yyscanner)
{
  (void)yyscanner;
  return 1; /* Always return 1 to indicate EOF */
}
