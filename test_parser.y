/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_parser.y - libfsp test parser - demonstrates push parser with streaming
 *
 * Parser for a simple toy language:
 *   program ::= statement*
 *   statement ::= PRINT expression ';' | LET identifier '=' expression ';'
 *   expression ::= string | identifier | integer
 *
 * Example: print "hello"; let x = 42;
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
 */

%{
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fsp.h"

#include <test_parser.h>

#define YY_NO_UNISTD_H 1
#undef yylex
#include <test_lexer.h>

/* Prototypes */
static void test_parser_internal_error(const char *msg);
static void test_parser_print_result(const char *msg);

/* Prototype for yyerror (will be #defined to test_parser_error) */
void yyerror(fsp_context* fsp_ctx, void *scanner, const char *msg);

/* Internal error function */
static void
test_parser_internal_error(const char *msg)
{
  fprintf(stderr, "Parse error: %s\n", msg);
}

/* Result printer */
static void
test_parser_print_result(const char *msg)
{
  printf("Result: %s\n", msg);
}

/* Note: yyerror is #defined to test_parser_error by Bison,
 * so we use a different name for our internal helper */

%}

/* Bison directives */

%require "3.0"

/* File prefix (-b) */
%file-prefix "test_parser"

/* Bison 2.6+: Symbol prefix */
%define api.prefix {test_parser_}

/* Bison 3.4+: Generated header file */
%define api.header.include {<test_parser.h>}

/* Write parser header file with macros (bison -d) */
%defines

/* Make verbose error messages for syntax errors */
%define parse.error verbose

/* Write output file with verbose descriptions of parser states */
%verbose

/* Pure parser - want a reentrant parser */
%define api.pure full

/* CRITICAL: Use PUSH parser (not pull) for streaming */
%define api.push-pull push

/* Pure parser arguments */
%lex-param { yyscan_t yyscanner }
%parse-param { fsp_context* fsp_ctx } { void* yyscanner }

/* Union for semantic values */
%union {
  char *string;
  int integer;
}

/* Token declarations */
%token PRINT LET EQUALS SEMICOLON ERROR
%token <string> STRING IDENTIFIER
%token <integer> INTEGER

/* Non-terminal types */
%type <string> expr

/* Destructor for strings to avoid memory leaks */
%destructor { free($$); } <string>

%%

/* Grammar rules */

program:
    /* empty */
  | program statement
  ;

statement:
    PRINT expr SEMICOLON {
      test_parser_print_result($2);
      free($2);
    }
  | LET IDENTIFIER EQUALS expr SEMICOLON {
      char buf[1024];
      snprintf(buf, sizeof(buf), "LET %s = %s", $2, $4);
      test_parser_print_result(buf);
      free($2);
      free($4);
    }
  | error SEMICOLON {
      test_parser_internal_error("Invalid statement");
      yyerrok;
    }
  ;

expr:
    STRING {
      $$ = $1;
    }
  | IDENTIFIER {
      $$ = $1;
    }
  | INTEGER {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d", $1);
      $$ = strdup(buf);
    }
  ;

%%

/* Error function required by Bison */
void
yyerror(fsp_context* fsp_ctx, void *scanner, const char *msg)
{
  (void)fsp_ctx;
  (void)scanner;
  test_parser_internal_error(msg);
}

