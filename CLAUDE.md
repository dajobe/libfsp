# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

libfsp is a minimal C library providing streaming buffer management for parsers built with Flex and Bison. It enables true incremental/streaming parsing by managing input buffers and state across chunk boundaries.

**What's Delivered (6 files):**

- Core library: [fsp.c](fsp.c), [fsp.h](fsp.h), [fsp_internal.h](fsp_internal.h)
- Build helpers: [scripts/postprocess-flex.py](scripts/postprocess-flex.py), [scripts/postprocess-bison.py](scripts/postprocess-bison.py), [scripts/README.md](scripts/README.md)

**Test Files (not delivered):**

- [test_lexer.l](test_lexer.l), [test_parser.y](test_parser.y), [fsp_test.c](fsp_test.c)

## Build Commands

### Standalone Build (GNUMakefile)

```shell
# Build library
make -f GNUMakefile

# Build and run tests
make -f GNUMakefile check

# Build fuzzer
make -f GNUMakefile fuzz

# Run fuzzer for 60 seconds (default)
make -f GNUMakefile fuzz-parse-run

# Run fuzzer for custom duration (5 minutes)
make -f GNUMakefile fuzz-parse-run FUZZ_TIME=300

# Clean
make -f GNUMakefile clean
```

### Autotools Build (Makefile.am)

```shell
# Only if explicitly requested by user
./autogen.sh
./configure --enable-maintainer-mode
make check
```

**Note:** DO NOT run configure/autogen.sh/make distclean unless explicitly requested by the user (per CLAUDE.md instructions).

## Code Quality Requirements

**Zero-warning compilation is fundamental:**

- All code (including generated lexer/parser) MUST compile without warnings
- In maintainer mode, 30+ warning flags are auto-detected and enabled
- Test lexer/parser are compiled with maximum warnings
- C++ compatibility is required (macros like `FSP_MALLOC` in [fsp_internal.h](fsp_internal.h))
- `make check` must complete with zero warnings and all tests passing

## Architecture

### Core Library Design

The library uses a **streaming buffer** pattern:

1. Host app feeds data chunks to `fsp_parse_chunk()`
2. Buffer accumulates chunks via `fsp_buffer_append()`
3. Flex lexer reads from buffer via `fsp_read_input()` (YY_INPUT)
4. Bison push parser maintains state across calls
5. Buffer compacts automatically to reclaim space

Key insight: Tokens can be split across chunk boundaries. The library handles this transparently.

### Integration Pattern

**Lexer (.l file):**

```c
#define YY_INPUT(buf,result,max_size) \
  result = fsp_read_input(yyextra, buf, max_size)
```

**Parser (.y file):**

```c
%define api.pure full
%define api.push-pull push
```

**Host code:**

```c
fsp_context *ctx = fsp_create();
while(has_data) {
  fsp_parse_chunk(ctx, chunk, len, is_end);
}
fsp_destroy(ctx);
```

### Test Language

The test lexer/parser implement a toy language for validation:

```text
program ::= statement*
statement ::= PRINT expression ';' | LET identifier '=' expression ';'
expression ::= string | identifier | integer
```

Triple-quoted strings (`"""..."""`) specifically test streaming across chunk boundaries (multi-line tokens).

### Postprocess Scripts

**Critical for production use:**

The Python postprocess scripts fix warnings and compatibility issues in generated Flex/Bison code:

- [scripts/postprocess-flex.py](scripts/postprocess-flex.py) - Fixes flex output
- [scripts/postprocess-bison.py](scripts/postprocess-bison.py) - Fixes bison output

These ensure generated code compiles warning-free with `-Wall -Wextra -Werror` and 30+ additional flags.

**Build integration:**

- Flex: Generate, then run postprocess-flex.py on both .c and .h files
- Bison: Generate, then run postprocess-bison.py on .c file

See [Makefile.am](Makefile.am) lines 74-89 and [GNUMakefile](GNUMakefile) lines 73-82 for examples.

## Fuzzing

Fuzzing requires LLVM Clang with libFuzzer (not Apple Clang):

```shell
# Build fuzzer
make -f GNUMakefile fuzz

# Quick run (60 seconds)
make -f GNUMakefile fuzz-parse-run

# Extended run (1 hour)
make -f GNUMakefile fuzz-parse-run FUZZ_TIME=3600
```

Built with AddressSanitizer and UndefinedBehaviorSanitizer. See [FUZZING.md](FUZZING.md) for details.

## File-Based Testing

Test suite validates parsing with AST validation:

- Input files: [tests/*.txt](tests/)
- Expected output: [tests/*.expected](tests/)
- Tests both valid input and error cases (missing semicolons, unterminated strings)

## Python Code Standards

All Python code in [scripts/](scripts/) should:

- Include type hints
- Be formatted with black
