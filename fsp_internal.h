/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * fsp_internal.h - Internal header for libfsp
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

#ifndef FSP_INTERNAL_H
#define FSP_INTERNAL_H

#ifdef FSP_CONFIG
#include <fsp_config.h>
#endif

#include "fsp.h"

struct fsp_context_s {
  /* Bison push parser state (to be set by host) */
  void *parser_state;              /* yypstate* */
  int parser_status;               /* YYPUSH_MORE, YYACCEPT, YYABORT */

  /* Flex lexer state (to be set by host) */
  void *lexer_scanner;             /* yyscan_t from reentrant scanner */

  /* Streaming byte buffer */
  char *stream_buffer;             /* Accumulated input bytes */
  size_t buffer_capacity;          /* Total allocated size */
  size_t data_length;              /* Bytes currently in buffer */
  size_t read_position;            /* Current read position for YY_INPUT */

  /* State flags */
  int more_chunks_expected;        /* 0 = EOF, 1 = more coming */
  int initialization_done;         /* Track first-time setup */

  /* User data */
  void *user_data;                 /* Opaque pointer for callbacks */
};

/* Internal buffer management */
int fsp_buffer_grow(fsp_context *ctx, size_t needed);

#endif /* FSP_INTERNAL_H */

