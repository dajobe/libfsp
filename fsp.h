/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * fsp.h - Header for libfsp (Flex/Bison Streaming Parser)
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

#ifndef FSP_H
#define FSP_H

#ifdef FSP_CONFIG
#include <fsp_config.h>
#endif

#include <stdio.h>
#include <stddef.h>

/**
 * fsp_status:
 * @FSP_STATUS_OK: Success, parsing complete
 * @FSP_STATUS_NEED_DATA: Need more input data
 * @FSP_STATUS_ERROR: Parse error
 * @FSP_STATUS_NO_MEMORY: Out of memory
 *
 * Status codes returned by fsp_parse_chunk()
 */
typedef enum {
  FSP_STATUS_OK = 0,
  FSP_STATUS_NEED_DATA,
  FSP_STATUS_ERROR,
  FSP_STATUS_NO_MEMORY
} fsp_status;

/**
 * fsp_context:
 *
 * Opaque streaming parser context
 */
typedef struct fsp_context_s fsp_context;

/**
 * fsp_read_callback:
 * @user_data: User data pointer
 * @buffer: Buffer to fill with data
 * @max_size: Maximum bytes to read
 *
 * Callback function for YY_INPUT to read more input data
 *
 * Return value: Number of bytes read, or 0 for EOF
 */
typedef int (*fsp_read_callback)(void *user_data, char *buffer, size_t max_size);

/* Core API */
fsp_context* fsp_create(void);
void fsp_destroy(fsp_context *ctx);
fsp_status fsp_parse_chunk(fsp_context *ctx, const char *chunk, size_t length, int is_end);
int fsp_read_input(void *user_data, char *buffer, size_t max_size);

/* Buffer management */
int fsp_buffer_append(fsp_context *ctx, const char *data, size_t length);
void fsp_buffer_compact(fsp_context *ctx);
size_t fsp_buffer_available(fsp_context *ctx);

/* Configuration */
void fsp_set_user_data(fsp_context *ctx, void *user_data);
void* fsp_get_user_data(fsp_context *ctx);

#endif /* FSP_H */

