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

#ifdef FSP_CONFIG
#include <fsp_config.h>
#else
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
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
      /* More chunks will come - return 0 to signal "would block" */
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
  size_t unread;
  size_t new_capacity;
  char *new_buffer;

  if(!ctx || !data || length == 0)
    return 0;

  /* Check if we need to grow or compact buffer */
  if(ctx->data_length + length > ctx->buffer_capacity) {
    /* Compact buffer (move unread data to beginning) */
    unread = ctx->data_length - ctx->read_position;
    if(unread > 0) {
      memmove(ctx->stream_buffer,
              ctx->stream_buffer + ctx->read_position,
              unread);
    }
    ctx->data_length = unread;
    ctx->read_position = 0;

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
 */
void
fsp_buffer_compact(fsp_context *ctx)
{
  size_t unread;

  if(!ctx)
    return;

  unread = ctx->data_length - ctx->read_position;
  if(unread > 0 && ctx->read_position > 0) {
    memmove(ctx->stream_buffer,
            ctx->stream_buffer + ctx->read_position,
            unread);
  }

  ctx->data_length = unread;
  ctx->read_position = 0;
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

