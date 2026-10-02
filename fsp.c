/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * fsp.c - Implementation of libfsp: Flex/Bison Streaming Parser Support Library
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

#include <stdlib.h>
#include <string.h>

#include "fsp.h"
#include "fsp_internal.h"

#ifndef FSP_DEFAULT_BUFFER_SIZE
#define FSP_DEFAULT_BUFFER_SIZE (64 * 1024)  /* 64KB */
#endif

/**
 * fsp_create - Create a new streaming parser context
 *
 * Returns:
 *   A new fsp_context, or NULL if memory allocation fails
 */
fsp_context*
fsp_create(void)
{
  fsp_context *ctx;

  ctx = (fsp_context*)calloc(1, sizeof(fsp_context));
  if(!ctx)
    return NULL;

  ctx->buffer_capacity = FSP_DEFAULT_BUFFER_SIZE;
  ctx->stream_buffer = (char*)malloc(ctx->buffer_capacity);
  if(!ctx->stream_buffer) {
    free(ctx);
    return NULL;
  }

  ctx->data_length = 0;
  ctx->read_position = 0;
  ctx->more_chunks_expected = 1;
  ctx->initialization_done = 0;

  return ctx;
}


/**
 * fsp_destroy - Destroy a streaming parser context
 *
 * @ctx: The context to destroy
 */
void
fsp_destroy(fsp_context *ctx)
{
  if(!ctx)
    return;

  if(ctx->stream_buffer) {
    free(ctx->stream_buffer);
    ctx->stream_buffer = NULL;
  }

  free(ctx);
}


/**
 * fsp_read_input - Read input data from the context
 *
 * @user_data: User data pointer
 * @buffer: Buffer to fill with data
 * @max_size: Maximum bytes to read
 *
 * Returns: Number of bytes read, or 0 for EOF
 */
int
fsp_read_input(void *user_data, char *buffer, size_t max_size)
{
  fsp_context *ctx = (fsp_context*)user_data;
  size_t available;
  size_t to_copy;

  if(!ctx || !buffer || max_size == 0)
    return 0;

  /* Calculate available unread data */
  available = ctx->data_length - ctx->read_position;

  if(available == 0) {
    /* No more data in buffer */
    if(ctx->more_chunks_expected) {
      /* More chunks will come - return 0 to signal "would block".
       * The lexer treats this like EOF so record it; any token being
       * matched may be incomplete. See fsp_input_would_block().
       */
      ctx->input_would_block = 1;
      return 0;
    } else {
      /* True EOF - no more data will ever come */
      return 0;
    }
  }

  /* Copy available data to caller's buffer */
  to_copy = (available < max_size) ? available : max_size;
  memcpy(buffer, ctx->stream_buffer + ctx->read_position, to_copy);
  ctx->read_position += to_copy;

  return (int)to_copy;
}


/**
 * fsp_buffer_append - Append data to the context's stream buffer
 *
 * @ctx: The context to append data to
 * @data: The data to append
 * @length: The length of the data to append
 *
 * Returns: 0 on success, -1 on failure
 */
int
fsp_buffer_append(fsp_context *ctx, const char *data, size_t length)
{
  size_t new_capacity;
  char *new_buffer;

  if(!ctx || !data || length == 0)
    return 0;

  /* Check if we need to grow or compact buffer */
  if(ctx->data_length + length > ctx->buffer_capacity) {
    /* Compact buffer (move retained data to beginning) */
    fsp_buffer_compact(ctx);

    /* If still not enough space, grow buffer */
    if(ctx->data_length + length > ctx->buffer_capacity) {
      new_capacity = ctx->buffer_capacity * 2;
      while(new_capacity < ctx->data_length + length) {
        new_capacity *= 2;
      }

      new_buffer = (char*)realloc(ctx->stream_buffer, new_capacity);
      if(!new_buffer)
        return -1; /* Out of memory */

      ctx->stream_buffer = new_buffer;
      ctx->buffer_capacity = new_capacity;
    }
  }

  /* Append data to buffer */
  memcpy(ctx->stream_buffer + ctx->data_length, data, length);
  ctx->data_length += length;

  return 0;
}


/**
 * fsp_buffer_compact - Compact the context's stream buffer
 *
 * @ctx: The context to compact
 *
 * Discards data that has been read.  If rewind support is enabled by
 * fsp_buffer_commit(), data after the commit mark is kept so that it
 * can be read again after fsp_buffer_rewind().
 */
void
fsp_buffer_compact(fsp_context *ctx)
{
  size_t keep;
  size_t retained;

  if(!ctx)
    return;

  keep = ctx->rewind_enabled ? ctx->mark_position : ctx->read_position;
  retained = ctx->data_length - keep;
  if(retained > 0 && keep > 0) {
    memmove(ctx->stream_buffer,
            ctx->stream_buffer + keep,
            retained);
  }

  ctx->data_length = retained;
  ctx->read_position -= keep;
  if(ctx->rewind_enabled)
    ctx->mark_position = 0;
}


/**
 * fsp_buffer_available - Get the number of available bytes in the context's stream buffer
 *
 * @ctx: The context to get the available bytes from
 *
 * Returns: The number of available bytes
 */
size_t
fsp_buffer_available(fsp_context *ctx)
{
  if(!ctx)
    return 0;

  return ctx->data_length - ctx->read_position;
}


/**
 * fsp_buffer_commit - Mark the input so far as complete tokens
 *
 * @ctx: The context
 * @unread: Number of bytes returned by fsp_read_input() that the lexer
 *   has not consumed yet, such as text still in the Flex buffer after
 *   the last token
 * @state: Lexer state to restore on rewind, or NULL
 *
 * Called after the lexer returns a complete token.  Moves the commit
 * mark to the end of the consumed input, which is the read position
 * less @unread, and saves @state for fsp_buffer_rewind().  Measuring
 * the unread input, rather than adding up token lengths, keeps the mark
 * correct when the lexer uses yyless(), yymore() or REJECT.
 *
 * The first call enables rewind support: from then on, input after the
 * commit mark is kept by buffer compaction until it is committed.  Call
 * it once before lexing starts.
 *
 * Also clears the would block flag.
 */
void
fsp_buffer_commit(fsp_context *ctx, size_t unread, const fsp_lexer_state *state)
{
  size_t start;

  if(!ctx)
    return;

  /* The mark never moves back before the previous commit */
  start = ctx->rewind_enabled ? ctx->mark_position : 0;
  if(unread > ctx->read_position - start)
    unread = ctx->read_position - start;

  ctx->rewind_enabled = 1;
  ctx->mark_position = ctx->read_position - unread;

  if(state)
    ctx->lexer_state = *state;
  else
    memset(&ctx->lexer_state, 0, sizeof(ctx->lexer_state));

  ctx->input_would_block = 0;
}


/**
 * fsp_buffer_rewind - Return the read position to the commit mark
 *
 * @ctx: The context
 * @state: Where to store the lexer state saved by the last commit, or
 *   NULL
 *
 * Makes the input after the commit mark available to read again with
 * fsp_read_input().  Used when the input ran out inside a token while
 * more chunks are expected: the host discards the partial token,
 * rewinds, and calls the lexer again after appending more input.  The
 * lexer's own buffer must also be discarded and its state restored;
 * postprocess-flex.py --fsp-rewind generates a function that does this.
 *
 * Also clears the would block flag.
 */
void
fsp_buffer_rewind(fsp_context *ctx, fsp_lexer_state *state)
{
  if(!ctx)
    return;

  if(ctx->rewind_enabled)
    ctx->read_position = ctx->mark_position;

  if(state)
    *state = ctx->lexer_state;

  ctx->input_would_block = 0;
}


/**
 * fsp_input_would_block - Check if input ran out before the real end
 *
 * @ctx: The context
 *
 * Flex treats YY_INPUT returning 0 as the end of input, so when the
 * data runs out in the middle of a token, Flex ends the token early.
 * This reports whether that may have happened.
 *
 * Returns: Non-zero if fsp_read_input() returned 0 because the
 * available data ran out while more chunks are expected, since the last
 * fsp_buffer_commit() or fsp_buffer_rewind().
 */
int
fsp_input_would_block(fsp_context *ctx)
{
  return ctx ? ctx->input_would_block : 0;
}


/**
 * fsp_set_user_data - Set the user data pointer for the context
 *
 * @ctx: The context to set the user data pointer for
 * @user_data: The user data pointer to set
 */
void
fsp_set_user_data(fsp_context *ctx, void *user_data)
{
  if(ctx)
    ctx->user_data = user_data;
}


/**
 * fsp_get_user_data - Get the user data pointer for the context
 *
 * @ctx: The context to get the user data pointer from
 *
 * Returns: The user data pointer
 */
void*
fsp_get_user_data(fsp_context *ctx)
{
  return ctx ? ctx->user_data : NULL;
}


/**
 * fsp_parse_chunk - Parse a chunk of input data
 *
 * @ctx: The context to parse the chunk in
 * @chunk: The chunk of input data to parse
 * @length: The length of the chunk of input data to parse
 * @is_end: Whether this is the last chunk of input data
 *
 * Returns: A status code
 */
fsp_status
fsp_parse_chunk(fsp_context *ctx, const char *chunk, size_t length, int is_end)
{
  if(!ctx)
    return FSP_STATUS_ERROR;

  /* Append chunk to buffer */
  if(fsp_buffer_append(ctx, chunk, length) != 0)
    return FSP_STATUS_NO_MEMORY;

  /* Update EOF flag */
  ctx->more_chunks_expected = !is_end;

  /* Note: Actual parsing happens in host-specific code
   * This is just the buffer management layer */
  if(is_end && ctx->data_length > 0)
    return FSP_STATUS_OK;
  else if(!is_end)
    return FSP_STATUS_NEED_DATA;
  else
    return FSP_STATUS_OK;
}

