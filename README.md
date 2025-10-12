# libfsp - Flex/Bison Streaming Parser Support Library

License: LGPL 2.1+ or GPL 2+ or Apache 2.0+

Home: <https://github.com/dajobe/libfsp>
Source: `git clone https://github.com/dajobe/libfsp.git`

## Overview

libfsp is a minimal C library that provides streaming buffer management for
parsers built with Flex and Bison. It enables true incremental/streaming parsing
by managing input buffers and state across chunk boundaries.

## What's Delivered

The delivered library consists of:

### Core Library (3 files)

- **`fsp.c`** - Core buffer management implementation
- **`fsp.h`** - Public API header
- **`fsp_internal.h`** - Internal structures and C++ compatibility macros

### Build Helper Scripts (3 files)

- **`scripts/postprocess-flex.py`** - Configurable post-processor for Flex output
- **`scripts/postprocess-bison.py`** - Configurable post-processor for Bison output
- **`scripts/README.md`** - Documentation for the postprocess scripts

The core library provides pure C buffer management primitives. The postprocess
scripts are **essential utilities** that host projects use with their own
Flex/Bison parsers to ensure the generated code compiles **warning-free** at
high warning levels (including `-Wall -Wextra -Werror`). They fix warnings,
compatibility issues, and code quality problems in generated code.

## What's for Testing Only

The test suite includes components to validate the library works:

- **`test_lexer.l`** - Example lexer demonstrating Flex integration
- **`test_parser.y`** - Example parser demonstrating Bison push parser
- **`fsp_test.c`** - Test cases exercising streaming with various chunk sizes

These test components prove the streaming approach works but are **NOT
delivered**. Host projects like Raptor and Rasqal have their own lexer/parser
implementations.

### Test Language

The test lexer and parser implement a simple toy language designed to exercise
streaming parser capabilities, particularly for multi-line tokens:

**Grammar:**
```
program ::= statement*
statement ::= PRINT expression ';'
           | LET identifier '=' expression ';'
expression ::= string | identifier | integer
```

**Example inputs:**
```c
print "hello";
let x = 42;
print """This is a
multi-line
string""";
```

The language includes triple-quoted strings (`"""..."""`) which are specifically
designed to test streaming across chunk boundaries, since they can span multiple
lines and may be split mid-token when fed to the parser in small chunks.

**Note:** The test files use the delivered `postprocess-flex.py` and
`postprocess-bison.py` scripts to build the test lexer and parser. Host projects
can use these same scripts with their own Flex/Bison files.

## Features

- True streaming/incremental parsing for Flex/Bison parsers
- Handles tokens split across input chunk boundaries
- Bison push parser integration via buffer management
- Flex YY_INPUT buffer management
- Reentrant and thread-safe
- Zero-copy where possible
- Bounded memory usage for arbitrarily large files
- Minimal dependencies: Only standard C library (malloc, free, memcpy, memmove)
- C++ compatible: Compiles cleanly with C and C++ compilers

## How It Works

libfsp enables Flex/Bison parsers to handle streaming input by:

1. **Accumulating input chunks** in a managed byte buffer (`fsp_buffer_append()`)
2. **Providing YY_INPUT function** for Flex to read from the stream buffer (`fsp_read_input()`)
3. **Managing buffer lifecycle** with compaction and growth as needed
4. **Supporting Bison push parser** by maintaining state across calls
5. **Handling partial tokens** automatically via Flex's buffer mechanism

The host project's parser uses Bison's push parser API, and the lexer calls
libfsp's `fsp_read_input()` from its YY_INPUT macro.

## Requirements

### Core Library

- C compiler (C99 or later) or C++ compiler
- Standard C library (stdlib.h, string.h)
- No external dependencies beyond libc

### Testing (optional, only needed for `make check`)

- Flex 2.5.31 or later
- Bison 3.0 or later (with push parser support)
- Python 3 (for postprocess scripts)

### Quality Standards

**The project maintains zero-warning compilation as a fundamental requirement:**

- Generated lexer/parser code **MUST** compile without warnings at high warning levels
- Tested and verified with: gcc, clang, and g++ (C++ mode)
- In maintainer mode, the build system automatically detects and enables **all** warning
  flags supported by your compiler (40+ flags tested, typically 30+ enabled)
- Flags include: `-std=c11 -Wall -Wc++-compat -Wextra -Wpedantic -Wunused
  -Waggregate-return -Wbad-function-cast -Wcast-align -Wdeclaration-after-statement`
  and many more
- The `postprocess-flex.py` and `postprocess-bison.py` scripts exist specifically to
  ensure generated code meets this standard
- C++ compatibility macros (`FSP_MALLOC`, etc.) in `fsp_internal.h` ensure clean
  compilation with C++ compilers
- A "working state" for the project means `make check` completes with **zero warnings**
  from the test lexer and parser compilation

**Maintainer Mode vs. Normal Mode:**

- **Maintainer mode** (`--enable-maintainer-mode`): Enables maximum warnings, regenerates
  lexer/parser from `.l`/`.y` files, runs postprocess scripts
- **Normal mode**: Uses moderate warnings, expects pre-generated lexer/parser files

## Standalone Build

```shell
make -f GNUmakefile
make -f GNUmakefile check    # Run tests
```

For a list of available build targets:

```shell
make help
```

## Embedded Build in an Automake Package

``` shell
git submodule add --name libfsp https://github.com/dajobe/libfsp.git libfsp
git submodule init libfsp
git submodule update libfsp
```

Then add these lines to your library `Makefile.am`:

``` makefile
AM_CFLAGS += -DFSP_CONFIG -I$(top_srcdir)/libfsp
libexample_la_LIBADD += $(top_builddir)/libfsp/libfsp.la
libexample_la_DEPENDENCIES += $(top_builddir)/libfsp/libfsp.la

$(top_builddir)/libfsp/libfsp.la:
 cd $(top_builddir)/libfsp && $(MAKE) libfsp.la
```

And add a configuration header `fsp_config.h` in the include path which
defines `HAVE_STDLIB_H` etc. as needed by `fsp.h` and `fsp.c`.

Optionally you might want in this file to redefine the exposed API
symbols with lines like:

``` c
#define fsp_create example_fsp_create
#define fsp_destroy example_fsp_destroy
#define fsp_parse_chunk example_fsp_parse_chunk
#define fsp_read_input example_fsp_read_input
```

You can see this pattern demonstrated in:

- [Rasqal](https://github.com/dajobe/rasqal) with libsv integration
- [Raptor](https://github.com/dajobe/raptor) (after libfsp integration)

## Example Usage

See integration examples in Raptor and Rasqal.

## API Overview

### Core Functions

```c
/* Create streaming parser context */
fsp_context* fsp_create(void);

/* Destroy context and free resources */
void fsp_destroy(fsp_context *ctx);

/* Parse a chunk of input data */
fsp_status fsp_parse_chunk(fsp_context *ctx, const char *chunk, 
                          size_t length, int is_end);

/* Read input function for YY_INPUT macro */
int fsp_read_input(void *user_data, char *buffer, size_t max_size);
```

### Buffer Management

```c
/* Append data to stream buffer */
int fsp_buffer_append(fsp_context *ctx, const char *data, size_t length);

/* Compact buffer to reclaim space */
void fsp_buffer_compact(fsp_context *ctx);

/* Get available unread bytes */
size_t fsp_buffer_available(fsp_context *ctx);
```

### Configuration

```c
/* Set user data pointer */
void fsp_set_user_data(fsp_context *ctx, void *user_data);

/* Get user data pointer */
void* fsp_get_user_data(fsp_context *ctx);
```

## Integration Pattern

### In your Flex lexer (.l file)

```c
/* Enable YY_INPUT for streaming */
#define YY_INPUT(buf,result,max_size) \
  result = fsp_read_input(yyextra, buf, max_size)
```

### In your Bison parser (.y file)

```c
/* Enable push parser */
%define api.pure full
%define api.push-pull push
```

### In your host code (proper streaming integration)

**Important:** To support arbitrarily small chunks (including 1-byte chunks), use the **buffer accumulation strategy**:

```c
#include <fsp.h>

#define MIN_BUFFER_FOR_LEX 64  /* Minimum buffer before calling lexer */

fsp_context *ctx = fsp_create();
yyscan_t scanner;
parser_pstate *pstate;
int final_drain = 0;

/* Initialize lexer and parser... */

/* Feed chunks with proper accumulation */
while(has_more_data || final_drain) {
    /* Phase 1: Accumulate chunks until buffer is full enough */
    while(has_more_data && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX) {
        fsp_buffer_append(ctx, chunk, chunk_size);
        has_more_data = read_next_chunk(&chunk, &chunk_size);
    }

    int is_eof = !has_more_data;
    if(is_eof && !final_drain) {
        ctx->more_chunks_expected = 0;
        final_drain = 1;
    }

    /* Phase 2: Process tokens when buffer is ready */
    while(fsp_buffer_available(ctx) > 0 || (is_eof && final_drain)) {
        if(!is_eof && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX)
            break;  /* Need more chunks */

        token = lexer_lex(&lval, scanner);
        if(token == 0) {
            if(!is_eof) break;
            final_drain = 0;
            break;
        }

        /* Push token to parser... */
    }

    if(!final_drain && is_eof) break;
}

/* Push final EOF to parser... */

fsp_destroy(ctx);
```

**Why buffer accumulation?** Flex interprets `YY_INPUT` returning 0 as EOF and makes tokenization decisions immediately. By accumulating chunks before calling the lexer, we ensure Flex always has enough lookahead to correctly identify tokens. This works with **any chunk size** (1 byte to 64KB).

**See also:** `fsp_test.c` (function `test_streaming_parser`) for a complete working example.

## Streaming with Small Chunks

libfsp supports streaming with arbitrarily small chunks when using the proper integration pattern:

- ✅ **1-byte chunks**: Works correctly with buffer accumulation
- ✅ **Any chunk size**: No minimum requirement
- ✅ **Multi-line tokens**: Triple-quoted strings work across chunk boundaries
- ✅ **Unlimited-length tokens**: Strings, URIs, comments of any size work correctly
- ✅ **Performance**: Minimal overhead, O(1) amortized append

### The Key Insight: Recognizing Delimiters vs. Holding Content

**MIN_BUFFER_FOR_LEX only needs to be large enough for Flex to RECOGNIZE token delimiters, not to hold entire token content.**

For example, with `MIN_BUFFER_FOR_LEX = 64`:

- **Keywords**: Must fit in buffer (e.g., `"print"` = 5 bytes) ✓
- **Operators**: Must fit in buffer (e.g., `"="` = 1 byte) ✓
- **String delimiters**: Must fit in buffer (e.g., `"` or `"""` = 1-3 bytes) ✓
- **String content**: Can be **megabytes** - Flex accumulates incrementally via `YY_INPUT` calls ✓

Once Flex recognizes a string pattern has started (sees the opening `"`), it continues calling `YY_INPUT` and accumulating characters until it sees the closing delimiter. The string content itself doesn't need to fit in the buffer.

The same applies to:
- **URIs**: `<http://...>` - Only need `<` in buffer to start matching
- **Comments**: `/* ... */` - Only need `/*` in buffer to start matching
- **Multi-line strings**: `"""..."""` - Only need `"""` in buffer to enter start condition

### Calculating MIN_BUFFER_FOR_LEX

Set `MIN_BUFFER_FOR_LEX` to the length of your **longest fixed-length token** (typically keywords):

```bash
# Automatically calculate from your lexer file
python3 scripts/calculate-min-buffer.py your_lexer.l
```

For most grammars:
- **Minimum: 16 bytes** (safe for most keywords)
- **Recommended: 64-256 bytes** (provides comfortable headroom)
- **Never needs to be huge**: Even 256 bytes handles any realistic grammar

## Testing

```shell
make -f GNUMakefile check
```

Or with Autotools:

```shell
./autogen.sh
./configure --enable-maintainer-mode  # Enables test lexer/parser generation
make check
```

The test suite builds an example lexer and parser (for testing purposes only)
to validate that the streaming approach works correctly. These are compiled
using Flex/Bison and demonstrate the complete integration, but they are not
part of the delivered library.

**Note:** `--enable-maintainer-mode` is required if you want to regenerate
the test lexer/parser from `.l` and `.y` files. Otherwise, pre-generated
files can be used (if distributed).

**Success Criteria:** `make check` must complete with:

- Zero compilation warnings from test_lexer.c and test_parser.c
- All tests passing

This validates both the core library functionality and the postprocess scripts'
ability to produce warning-free generated code.

## Project Structure

```text
libfsp/
├── fsp.c                       # Core implementation [DELIVERED]
├── fsp.h                       # Public API [DELIVERED]
├── fsp_internal.h              # Internal structures and C++ macros [DELIVERED]
│
├── scripts/
│   ├── postprocess-flex.py    # Flex postprocessor [DELIVERED]
│   ├── postprocess-bison.py   # Bison postprocessor [DELIVERED]
│   └── README.md              # Script documentation [DELIVERED]
│
├── fsp_test.c                 # Test suite [testing only]
├── test_lexer.l               # Example lexer [testing only]
├── test_parser.y              # Example parser [testing only]
│
├── Makefile.am                # Automake build
├── GNUMakefile                # Standalone build
├── configure.ac               # Autoconf configuration
└── README.md                  # This file
```

**Summary:** The core library (3 files) and build helper scripts (3 files) are
delivered to host projects. Test files (fsp_test.c, test_lexer.l, test_parser.y)
exist solely to validate that the library works correctly.

## Author

Dave Beckett <https://www.dajobe.org/>
