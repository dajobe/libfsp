# libfsp - Flex/Bison Streaming Parser Support Library

License: LGPL 2.1+ or GPL 2+ or Apache 2.0+

Home: <https://github.com/dajobe/libfsp> Source: `git clone
https://github.com/dajobe/libfsp.git`

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

### Build Helper Scripts (4 files)

- **`scripts/postprocess-flex.py`** - Configurable post-processor for Flex
  output
- **`scripts/postprocess-bison.py`** - Configurable post-processor for Bison
  output
- **`scripts/fsp-helper.py`** - Integration utility (calculate, generate,
  validate, check)
- **`scripts/README.md`** - Documentation for the scripts

The core library provides pure C buffer management primitives. The helper
scripts provide:

- **postprocess-flex.py / postprocess-bison.py**: Fix warnings in generated code
  to ensure **warning-free** compilation at high warning levels (including
  `-Wall -Wextra -Werror`). `postprocess-flex.py --fsp-rewind` also adds the
  lexer functions for [rewind support](#rewind-support) and lets refills grow
  with the scanner buffer when matching long tokens
- **fsp-helper.py**: Integration assistant with four commands:
  - `generate` - Create customized streaming parser implementation
  - `validate` - Verify lexer/parser are correctly configured for streaming
  - `check` - Validate with the lexer file required
  - `calculate` - Deprecated: compute a MIN_BUFFER_FOR_LEX value, which is not
    needed with rewind support

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

```text
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
- Handles tokens split across input chunk boundaries by rescanning them (rewind
  support)
- Bison push parser integration via buffer management
- Flex YY_INPUT buffer management
- Reentrant and thread-safe
- Zero-copy where possible
- Bounded memory usage for arbitrarily large files
- Minimal dependencies: Only standard C library (malloc, free, memcpy, memmove)
- C++ compatible: Compiles cleanly with C and C++ compilers

## How It Works

libfsp enables Flex/Bison parsers to handle streaming input by:

1. **Accumulating input chunks** in a managed byte buffer
   (`fsp_buffer_append()`)
2. **Providing YY_INPUT function** for Flex to read from the stream buffer
   (`fsp_read_input()`)
3. **Managing buffer lifecycle** with compaction and growth as needed
4. **Supporting Bison push parser** by maintaining state across calls
5. **Rescanning partial tokens** with rewind support when a token is split
   across chunks (see [Rewind support](#rewind-support))
6. **Finalizing parsing** by pushing EOF token (0) to detect incomplete input

The host project's parser uses Bison's push parser API, and the lexer calls
libfsp's `fsp_read_input()` from its YY_INPUT macro. After all input is
processed, the host must push a final EOF token (0) to the Bison push parser to
properly finalize parsing and detect syntax errors in incomplete input.

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

- Generated lexer/parser code **MUST** compile without warnings at high warning
  levels
- Tested and verified with: gcc, clang, and g++ (C++ mode)
- In maintainer mode, the build system automatically detects and enables **all**
  warning flags supported by your compiler (40+ flags tested, typically 30+
  enabled)
- Flags include: `-std=c11 -Wall -Wc++-compat -Wextra -Wpedantic -Wunused
  -Waggregate-return -Wbad-function-cast -Wcast-align
  -Wdeclaration-after-statement` and many more
- The `postprocess-flex.py` and `postprocess-bison.py` scripts exist
  specifically to ensure generated code meets this standard
- C++ compatibility macros (`FSP_MALLOC`, etc.) in `fsp_internal.h` ensure clean
  compilation with C++ compilers
- A "working state" for the project means `make check` completes with **zero
  warnings** from the test lexer and parser compilation

**Maintainer Mode vs. Normal Mode:**

- **Maintainer mode** (`--enable-maintainer-mode`): Enables maximum warnings,
  regenerates lexer/parser from `.l`/`.y` files, runs postprocess scripts
- **Normal mode**: Uses moderate warnings, expects pre-generated lexer/parser
  files

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

And add a configuration header `fsp_config.h` in the include path which defines
`HAVE_STDLIB_H` etc. as needed by `fsp.h` and `fsp.c`.

Optionally you might want in this file to redefine the exposed API symbols with
lines like:

``` c
#define fsp_create example_fsp_create
#define fsp_destroy example_fsp_destroy
#define fsp_parse_chunk example_fsp_parse_chunk
#define fsp_read_input example_fsp_read_input
```

You can see this pattern demonstrated in:

- [Rasqal](https://github.com/dajobe/rasqal) with libsv integration
- [Raptor](https://github.com/dajobe/raptor) (after libfsp integration)

### Real-World Example: Raptor Integration

Raptor's integration (commit 292ec8bd) demonstrates the complete pattern:

**1. Create `fsp_config.h` wrapper:**

```c
/* src/fsp_config.h */
#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#ifdef WIN32
#include <win32_raptor_config.h>
#else
#include <raptor_config.h>
#endif

/* Rename libfsp functions to avoid conflicts */
#define fsp_create raptor_fsp_create
#define fsp_destroy raptor_fsp_destroy
#define fsp_buffer_append raptor_fsp_buffer_append
#define fsp_buffer_available raptor_fsp_buffer_available
#define fsp_buffer_commit raptor_fsp_buffer_commit
#define fsp_buffer_compact raptor_fsp_buffer_compact
#define fsp_buffer_rewind raptor_fsp_buffer_rewind
#define fsp_input_would_block raptor_fsp_input_would_block
#define fsp_input_ready raptor_fsp_input_ready
#define fsp_read_input raptor_fsp_read_input
#define fsp_set_user_data raptor_fsp_set_user_data
#define fsp_get_user_data raptor_fsp_get_user_data

#endif
```

**2. Include libfsp source directly:**

```makefile
# src/Makefile.am
libraptor2_la_SOURCES += \
  fsp_config.h \
  $(top_srcdir)/libfsp/fsp.c \
  $(top_srcdir)/libfsp/fsp.h

AM_CPPFLAGS += -DHAVE_FSP_CONFIG_H -I$(top_srcdir)/libfsp
```

**3. Use postprocess scripts with project config:**

<!-- markdownlint-disable MD010 -->

```makefile
turtle_lexer.c: turtle_lexer.l turtle_parser.c \
                $(top_srcdir)/libfsp/scripts/postprocess-flex.py
	$(LEX) -o$@ turtle_lexer.l
	$(PYTHON3) $(top_srcdir)/libfsp/scripts/postprocess-flex.py \
	  --fsp-rewind -c raptor_config.h -g HAVE_CONFIG_H \
	  turtle_lexer.c > turtle_lexer.t
	mv -f turtle_lexer.t turtle_lexer.c
```

<!-- markdownlint-enable MD010 -->

**4. Streaming parser implementation pattern:**

```c
/* Store fsp_context and push parser state in parser struct */
struct raptor_turtle_parser_s {
  fsp_context *fsp_ctx;
  turtle_parser_pstate *pstate;
  /* ... other fields ... */
};

/* Initialize on parse_start */
turtle_parser->fsp_ctx = fsp_create();
fsp_set_user_data(turtle_parser->fsp_ctx, rdf_parser);

/* In lexer: enable streaming via YY_INPUT and rewind support */
#define YY_INPUT(buf,result,max_size) \
  result = fsp_read_input(yyextra, buf, max_size)
#define YY_USER_ACTION FSP_LEXER_USER_ACTION(yyextra)

/* In parser: retrieve user data */
#define PARSER_FROM_FSP_CONTEXT(fsp_ctx) \
  ((raptor_parser*)fsp_get_user_data(fsp_ctx))

/* Enable rewind support before the first token */
turtle_lexer_fsp_commit(scanner);

/* After feeding a chunk, wait if a pending retry needs more input. */
if(!fsp_input_ready(fsp_ctx))
  return 0;

/* Lex until the lexer needs more input */
while(1) {
  token = turtle_lexer_lex(&lval, scanner);
  if(token == FSP_LEXER_NEED_MORE ||
     (!token && !is_end && fsp_input_would_block(fsp_ctx))) {
    turtle_lexer_fsp_rewind(scanner);  /* rescan after more input */
    return 0;
  }
  if(!token)
    break;                            /* EOF: push token 0 */
  turtle_lexer_fsp_commit(scanner);
  rc = turtle_parser_push_parse(pstate, token, &lval, fsp_ctx, scanner);
  if(rc != YYPUSH_MORE) break;
}
```

This pattern eliminates Raptor's old manual buffer management
(consumed/processed/consumable tracking) and enables proper streaming with
arbitrary chunk sizes.

## Example Usage

**Quick Start:**

```bash
# Validate your lexer/parser configuration
python3 scripts/fsp-helper.py check --lexer your_lexer.l --parser your_parser.y

# Generate streaming parser implementation
python3 scripts/fsp-helper.py generate \
  --lexer-prefix your_lexer \
  --parser-prefix your_parser \
  -o your_streaming.c
```

**Detailed Examples:**

- [RAPTOR_INTEGRATION.md](RAPTOR_INTEGRATION.md) - Complete analysis of
  integrating libfsp into Raptor's Turtle parser
- [fsp_test.c](fsp_test.c) - Working test implementation with 24 test cases
- [Rasqal](https://github.com/dajobe/rasqal) - Production use with libsv
  integration
- [Raptor](https://github.com/dajobe/raptor) - (integration in progress)

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

### Rewind Functions

```c
/* Lexer state saved at a commit: start condition, beginning of line,
 * line and column */
typedef struct {
  int start_condition;
  int at_bol;
  int lineno;
  int column;
} fsp_lexer_state;

/* Mark the input so far as complete tokens.  unread is the number of
 * bytes read but still unconsumed in the lexer's buffer.
 * The first call enables rewind support. */
void fsp_buffer_commit(fsp_context *ctx, size_t unread, const fsp_lexer_state *state);

/* Return to the commit mark and get the saved lexer state */
void fsp_buffer_rewind(fsp_context *ctx, fsp_lexer_state *state);

/* Check after feeding a chunk, before starting a lexer batch.
 * Rewinds throttle retries; commits clear the wait; EOF always permits
 * processing. Do not check between tokens buffered inside the lexer. */
int fsp_input_ready(fsp_context *ctx);

/* Non-zero if the input ran out while more chunks are expected */
int fsp_input_would_block(fsp_context *ctx);

/* Lexer YY_USER_ACTION body and the value it returns for a cut short token */
#define YY_USER_ACTION FSP_LEXER_USER_ACTION(yyextra)
FSP_LEXER_NEED_MORE
```

Most hosts use the lexer functions `PREFIXfsp_commit()` and `PREFIXfsp_rewind()`
added by `postprocess-flex.py --fsp-rewind`, which call `fsp_buffer_commit()`
and `fsp_buffer_rewind()`, measure the input left in the Flex buffer, save and
restore the Flex start condition, beginning of line flag, line and column, and
discard the Flex buffer.

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

/* Enable rewind support for tokens split across chunks */
#define YY_USER_ACTION FSP_LEXER_USER_ACTION(yyextra)
```

Post-process the generated lexer with `postprocess-flex.py --fsp-rewind` to add
the `PREFIXfsp_commit()` and `PREFIXfsp_rewind()` functions.

### In your Bison parser (.y file)

```c
/* Enable push parser */
%define api.pure full
%define api.push-pull push
```

### In your host code (proper streaming integration)

Append each chunk, check `fsp_input_ready()`, then lex and parse until the lexer
needs more input. Commit after every complete token and rewind when the input
runs out:

```c
#include <fsp.h>

fsp_context *ctx = fsp_create();
yyscan_t scanner;
parser_pstate *pstate;

/* Initialize lexer and parser, set ctx as the lexer extra data... */

/* Enable rewind support before the first token */
lexer_fsp_commit(scanner);

while(1) {
    /* Read and append the next chunk; is_end marks the last one */
    fsp_parse_chunk(ctx, chunk, chunk_size, is_end);

    if(!fsp_input_ready(ctx))
        continue;  /* Feed the next chunk before retrying the lexer */

    /* Lex and parse until the lexer needs more input or the end */
    while(1) {
        token = lexer_lex(&lval, scanner);

        if(token == FSP_LEXER_NEED_MORE ||
           (!token && fsp_input_would_block(ctx))) {
            /* The input ran out, possibly inside a token.  Discard it
             * and rescan from the last complete token after the next
             * chunk is appended. */
            lexer_fsp_rewind(scanner);
            break;
        }

        if(!token)
            goto eof;  /* Real end of input */

        lexer_fsp_commit(scanner);

        /* Push token to parser... */
    }

    if(!has_more_data)
        break;
}

eof:
/* CRITICAL: Push final EOF token (0) to parser to finalize parsing.
 * This allows the parser to detect incomplete statements and syntax errors.
 * Without this, the parser may incorrectly accept incomplete input. */
status = parser_push_parse(pstate, 0, NULL, ctx, scanner);

fsp_destroy(ctx);
```

**Why push EOF token (0)?** After draining all tokens from the lexer, you
**MUST** push a final EOF token (0) to the Bison push parser. This signals
end-of-input and allows the parser to:

- Detect incomplete statements (e.g., missing semicolons)
- Report syntax errors for truncated input
- Properly finalize parsing and return success/failure status

Without the EOF token, the parser may incorrectly accept incomplete input.

**See also:**

- Complete implementation: [fsp_test.c](fsp_test.c) function
  `run_streaming_parser()`
- Helper tool: [scripts/fsp-helper.py](scripts/fsp-helper.py) - `generate`
  writes this loop for your lexer and parser
- Integration guide: [RAPTOR_INTEGRATION.md](RAPTOR_INTEGRATION.md) - Real-world
  example with Raptor Turtle parser

## Rewind Support

Flex treats `YY_INPUT` returning 0 as the end of the input. When the available
data runs out in the middle of a token and more chunks are still expected,
`fsp_read_input()` has to return 0, and Flex then ends the token early. It
matches the longest token it can from the partial text, so
`<http://example.org/>` cut after `<http` can become `<` followed by `http`, and
an unterminated long string reaches its end-of-file rule. Keeping a minimum
number of bytes in the buffer before calling the lexer does not prevent this,
because a token can be longer than any minimum and Flex can read past the buffer
while matching it.

Rewind support handles this by rescanning:

1. `fsp_read_input()` records when it returns 0 while more chunks are expected;
   `fsp_input_would_block()` reports this.
2. `FSP_LEXER_USER_ACTION` in `YY_USER_ACTION` runs before every rule action. If
   the input ran out during the match, the lexer returns `FSP_LEXER_NEED_MORE`
   without running the rule action, so a partial token has no side effects such
   as allocations, errors or line counting.
3. After each complete token the host calls `PREFIXfsp_commit()`, which moves
   the commit mark to the end of the input Flex has consumed, measured from the
   text still in the Flex buffer so that `yyless()`, `yymore()` and `REJECT`
   work, and saves the Flex start condition, beginning of line flag, line and
   column. Buffer compaction keeps the input after the commit mark.
4. When the lexer returns `FSP_LEXER_NEED_MORE`, or returns 0 while
   `fsp_input_would_block()` is true, the host calls `PREFIXfsp_rewind()`, which
   returns the read position to the commit mark, discards the Flex buffer and
   restores the saved Flex state. The host then appends more input and calls
   `fsp_input_ready()` before starting another lexer batch, which rescans the
   token from its start.

This works with any chunk size, including 1-byte chunks, and with tokens of any
length.

Host responsibilities:

- **Rules in exclusive start conditions with `<<EOF>>` actions** do not run
  `YY_USER_ACTION`. Make them return `FSP_LEXER_NEED_MORE` when
  `fsp_input_would_block(yyextra)` is true, before reporting an end-of-file
  error.
- **Lexer state outside Flex**, such as a line counter kept by the host or a
  buffer used to build a long string over several rule matches, must be saved by
  the host when it commits and restored or freed when it rewinds. The same
  applies to a semantic value the lexer allocates before returning
  `FSP_LEXER_NEED_MORE`; code from `fsp-helper.py generate` calls
  `PARSER_DISCARD_LVAL()` for this.
- **Retry scheduling** requires the host to check `fsp_input_ready()` after
  feeding each chunk and before entering its lexer loop. Do not check between
  individual tokens: Flex may still hold unread input in its own buffer.

### Retry scheduling and long tokens

After a rewind caused by exhausted input, libfsp retries eagerly while retained,
uncommitted input is below `FSP_LEXER_RETRY_MIN_BYTES` (256 bytes). At or above
that cutoff, `fsp_input_ready()` waits for the retained input to double before
allowing another lexer batch. Repeated attempts therefore scan geometrically
growing prefixes instead of rescanning after every small chunk. The cutoff is a
tuning choice, not a token length limit. A commit clears the retry threshold,
and buffer compaction preserves it.

Waiting can delay callbacks or error reporting even when a later chunk completes
the pending token. A final chunk always allows processing, including an empty
final chunk sent separately from the last data. Explicit rewinds without an
input exhaustion do not introduce a retry wait.

`postprocess-flex.py --fsp-rewind` also sets the guarded `YY_READ_BUF_SIZE`
default to `INT_MAX`. Flex limits each actual read to the available space in its
scanner buffer; this does not request an `INT_MAX` allocation. As that buffer
grows with a long token, refills grow too, avoiding repeated token-state
rebuilds after fixed-size reads. `YY_INPUT` reads only data already buffered in
libfsp. An explicit host definition of `YY_READ_BUF_SIZE` overrides this
default.

## Streaming with Small Chunks

With rewind support, libfsp supports streaming with arbitrarily small chunks:

- **1-byte chunks**: Split tokens are rescanned when the retry scheduler permits
- **Any chunk size**: No minimum requirement
- **Multi-line tokens**: Triple-quoted strings work across chunk boundaries
- **Long tokens**: Strings, URIs and comments longer than the Flex buffer work
- **Performance**: O(1) amortized append; large partial tokens wait for retained
  input to double between rescans

Earlier versions of this document recommended calling the lexer only when at
least `MIN_BUFFER_FOR_LEX` bytes were buffered. That reduces how often a token
is cut short but does not prevent it, so it is deprecated in favour of rewind
support. `fsp-helper.py calculate` is kept only for existing users.

### Integration Validation and Code Generation

Validate configuration and optionally generate streaming parser code:

```bash
# Validate the lexer and parser
python3 scripts/fsp-helper.py check --lexer your_lexer.l --parser your_parser.y

# Validate either file
python3 scripts/fsp-helper.py validate --lexer your_lexer.l --parser your_parser.y

# Generate streaming parser implementation
python3 scripts/fsp-helper.py generate \\
  --lexer-prefix your_lexer \\
  --parser-prefix your_parser \\
  -o your_streaming.c
```

The validator checks:

- ✅ Lexer defines custom `YY_INPUT` calling `fsp_read_input()`
- ✅ Lexer defines `YY_USER_ACTION` as `FSP_LEXER_USER_ACTION(yyextra)`
- ✅ Lexer uses `%option reentrant` (required for push parser)
- ✅ Lexer has `%option bison-bridge` (for yylval)
- ✅ Parser uses `%define api.push-pull push`
- ✅ Parser uses `%define api.pure full`
- ✅ No conflicting options that break streaming

The generator creates a complete streaming parser function customized for your
lexer/parser, using rewind support, ready to use or adapt.

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

The test suite builds an example lexer and parser (for testing purposes only) to
validate that the streaming approach works correctly. These are compiled using
Flex/Bison and demonstrate the complete integration, but they are not part of
the delivered library.

**Note:** `--enable-maintainer-mode` is required if you want to regenerate the
test lexer/parser from `.l` and `.y` files. Otherwise, pre-generated files can
be used (if distributed).

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
│   ├── fsp-helper.py          # Integration utility [DELIVERED]
│   └── README.md              # Script documentation [DELIVERED]
│
├── fsp_test.c                 # Test suite [testing only]
├── test_lexer.l               # Example lexer [testing only]
├── test_parser.y              # Example parser [testing only]
│
├── RAPTOR_INTEGRATION.md      # Integration analysis example
├── AGENTS.md                  # Agent instructions
├── CLAUDE.md                  # Pointer to AGENTS.md
├── FUZZING.md                 # Fuzzing guide
│
├── Makefile.am                # Automake build
├── GNUMakefile                # Standalone build
├── configure.ac               # Autoconf configuration
└── README.md                  # This file
```

**Summary:** The core library (3 files) and build helper scripts (4 files) are
delivered to host projects. Test files (fsp_test.c, test_lexer.l, test_parser.y)
exist solely to validate that the library works correctly.

## Author

Dave Beckett <https://www.dajobe.org/>
