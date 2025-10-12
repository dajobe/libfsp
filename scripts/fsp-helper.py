#!/usr/bin/env python3
"""
fsp-helper - Utility for libfsp streaming parser integration.

This tool provides three main functions:
  1. Calculate optimal MIN_BUFFER_FOR_LEX value from lexer files
  2. Generate customized streaming parser implementation
  3. Validate lexer/parser are correctly configured for streaming

Usage:
  # Calculate MIN_BUFFER_FOR_LEX
  fsp-helper.py calculate turtle_lexer.l

  # Generate streaming parser implementation
  fsp-helper.py generate --lexer-prefix turtle_lexer --parser-prefix turtle_parser

  # Validate configuration
  fsp-helper.py validate --lexer turtle_lexer.l --parser turtle_parser.y

  # All-in-one check
  fsp-helper.py check --lexer turtle_lexer.l --parser turtle_parser.y

(C) Copyright 2025 Dave Beckett https://www.dajobe.org/
"""

import argparse
import re
import sys
from dataclasses import dataclass
from enum import Enum
from typing import List, Optional, TextIO, Tuple


# ========================================================================
# CALCULATE: MIN_BUFFER_FOR_LEX calculation
# ========================================================================


def extract_rules_from_flex(filename: str) -> List[Tuple[str, str]]:
    """
    Extract token rules from Flex lexer file.

    Args:
        filename: Path to .l file

    Returns:
        List of (pattern, action) tuples
    """
    rules = []
    in_rules_section = False
    in_multiline_action = False
    current_pattern = ""
    brace_count = 0

    with open(filename, "r") as f:
        for line in f:
            stripped = line.strip()

            # Skip comments
            if stripped.startswith("/*") or stripped.startswith("//"):
                continue

            # Start of rules section (after second %%)
            if stripped == "%%":
                if in_rules_section:
                    break  # End of rules section
                in_rules_section = True
                continue

            if not in_rules_section:
                continue

            # Handle multi-line actions
            if in_multiline_action:
                brace_count += line.count("{") - line.count("}")
                if brace_count <= 0:
                    in_multiline_action = False
                continue

            # Skip blank lines
            if not stripped:
                continue

            # Match rule: pattern followed by action
            # Pattern can be: keyword, regex, or start condition
            if "{" in line:
                # Extract pattern before the action
                pattern_part = line.split("{")[0].strip()
                if pattern_part and not pattern_part.startswith("<"):
                    # Not a start condition line
                    rules.append((pattern_part, "action"))

                # Check if this starts a multi-line action
                brace_count = line.count("{") - line.count("}")
                if brace_count > 0:
                    in_multiline_action = True

    return rules


def analyze_flex_pattern(pattern: str) -> Tuple[Optional[int], str]:
    """
    Analyze a Flex pattern and determine if it's fixed or variable length.

    Args:
        pattern: Flex pattern (regex)

    Returns:
        (length, description) if fixed-length, (None, reason) if variable
    """
    # Remove leading/trailing whitespace and quotes
    pattern = pattern.strip().strip('"')

    # Variable-length indicators
    if any(op in pattern for op in ["*", "+", "?", "{", "["]):
        if pattern.count("[") == pattern.count("]"):
            # Character class - could be fixed
            if "+" in pattern or "*" in pattern:
                return None, "variable repetition"
        else:
            return None, "regex with repetition"

    # Quoted literal strings
    if pattern.startswith('"') and pattern.endswith('"'):
        content = pattern[1:-1]
        return len(content), f'literal "{content}"'

    # Case-insensitive patterns like [Pp][Rr][Ii][Nn][Tt]
    if pattern.count("[") > 0 and pattern.count("[") == pattern.count("]"):
        # Count character classes
        char_classes = re.findall(r"\[[^\]]+\]", pattern)
        if all(len(cc) <= 10 for cc in char_classes):  # Reasonable char class
            length = len(char_classes)
            return length, f"pattern requires ~{length} chars"

    # Simple literal (letters/digits only)
    if re.match(r"^[a-zA-Z0-9_]+$", pattern):
        return len(pattern), f'keyword "{pattern}"'

    # Operators and punctuation
    if len(pattern) <= 3 and all(c in "=;:,(){}[]<>!@#$%^&*+-/|\\." for c in pattern):
        return len(pattern), f'operator "{pattern}"'

    # Assume variable if we can't determine
    return None, "complex pattern"


def calculate_min_buffer(filename: str, verbose: bool = False, quiet: bool = False) -> int:
    """
    Calculate optimal MIN_BUFFER_FOR_LEX value.

    Args:
        filename: Path to lexer .l file
        verbose: Show detailed analysis
        quiet: Only output the number

    Returns:
        Recommended MIN_BUFFER_FOR_LEX value
    """
    rules = extract_rules_from_flex(filename)

    fixed_patterns = []
    variable_patterns = []

    for pattern, _ in rules:
        length, description = analyze_flex_pattern(pattern)
        if length is not None:
            fixed_patterns.append((pattern, length, description))
        else:
            variable_patterns.append((pattern, description))

    if not fixed_patterns:
        if not quiet:
            print(f"Warning: No fixed-length patterns found in {filename}")
            print("Using default MIN_BUFFER_FOR_LEX = 64")
        return 64

    # Find longest fixed-length pattern
    longest = max(fixed_patterns, key=lambda x: x[1])
    max_length = longest[1]

    # Recommend with 2x headroom
    recommended = max_length * 2
    # Round up to power of 2 for clean values
    import math

    recommended = 2 ** math.ceil(math.log2(recommended))

    if quiet:
        print(recommended)
        return recommended

    print(f"Analyzing {filename}...")
    print("=" * 70)
    print(f"Fixed-length patterns found: {len(fixed_patterns)}")
    print(f"Variable-length patterns: {len(variable_patterns)}")
    print()
    print("Longest fixed-length token:")
    print(f"  Pattern: {longest[0]}")
    print(f"  Length:  {longest[1]} bytes")
    print(f"  Description: {longest[2]}")
    print()
    print("Recommended MIN_BUFFER_FOR_LEX:")
    print(f"  Minimum safe: {max_length} bytes (exact)")
    print(f"  Recommended:  {recommended} bytes (with headroom)")
    print()
    print(f"#define MIN_BUFFER_FOR_LEX {recommended}")

    if verbose:
        print()
        print("All fixed-length patterns:")
        for pat, length, desc in sorted(fixed_patterns, key=lambda x: x[1], reverse=True)[:10]:
            print(f"  {length:3d} bytes: {pat:30s} ({desc})")

    return recommended


# ========================================================================
# GENERATE: Streaming parser code generator
# ========================================================================


def generate_streaming_parser(
    lexer_prefix: str,
    parser_prefix: str,
    min_buffer: int,
    function_name: str,
    output: TextIO,
) -> None:
    """
    Generate a streaming parser implementation.

    Args:
        lexer_prefix: Prefix for lexer functions (e.g., 'turtle_lexer')
        parser_prefix: Prefix for parser functions (e.g., 'turtle_parser')
        min_buffer: MIN_BUFFER_FOR_LEX value
        function_name: Name of the generated function
        output: Output file handle
    """
    parser_upper = parser_prefix.upper()

    output.write(
        f"""/* Generated by fsp-helper.py from libfsp
 * https://github.com/dajobe/libfsp
 *
 * Lexer prefix: {lexer_prefix}
 * Parser prefix: {parser_prefix}
 * MIN_BUFFER_FOR_LEX: {min_buffer}
 */

#include <stddef.h>
#include <string.h>
#include "fsp.h"
#include "{lexer_prefix}.h"
#include "{parser_prefix}.h"

/* Minimum bytes to accumulate before calling lexer */
#define MIN_BUFFER_FOR_LEX {min_buffer}

/**
 * {function_name}:
 * @ctx: FSP context (must be already created via fsp_create())
 * @input: Input string to parse
 * @chunk_size: Size of chunks to process (can be 1 byte to any size)
 * @user_data: Optional user data pointer (pass NULL if not needed)
 *
 * Parse input using streaming with buffer accumulation strategy.
 * Supports arbitrarily small chunks (including 1-byte chunks).
 *
 * Return value: 0 on success, -1 on error
 */
int
{function_name}(fsp_context *ctx, const char *input, size_t chunk_size, void *user_data)
{{
  yyscan_t scanner;
  {parser_prefix}_pstate *pstate;
  int status;
  size_t pos = 0;
  size_t input_len;
  int final_drain = 0;
  int result = 0;

  if(!ctx || !input)
    return -1;

  input_len = strlen(input);

  /* Initialize lexer */
  if({lexer_prefix}_lex_init(&scanner)) {{
    return -1;
  }}

  /* Set FSP context as extra data for lexer */
  {lexer_prefix}_set_extra(ctx, scanner);

  /* Store user data in FSP context if provided */
  if(user_data)
    fsp_set_user_data(ctx, user_data);

  /* Create push parser state */
  pstate = {parser_prefix}_pstate_new();
  if(!pstate) {{
    {lexer_prefix}_lex_destroy(scanner);
    return -1;
  }}

  /* STREAMING STRATEGY:
   * Phase 1: Accumulate chunks until buffer has MIN_BUFFER_FOR_LEX bytes OR EOF
   * Phase 2: Process tokens when buffer is ready
   *
   * This pattern allows streaming with arbitrarily small chunks (even 1 byte)
   * while ensuring Flex always has enough lookahead to correctly identify tokens.
   *
   * See libfsp README.md "Streaming with Small Chunks" section for details.
   */
  while(pos < input_len || final_drain) {{
    int is_eof;

    /* Phase 1: Accumulate chunks until buffer is sufficiently full */
    while(pos < input_len && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX) {{
      size_t chunk;

      chunk = input_len - pos;
      if(chunk > chunk_size)
        chunk = chunk_size;

      /* Append chunk to FSP buffer */
      if(fsp_buffer_append(ctx, input + pos, chunk) < 0) {{
        result = -1;
        goto cleanup;
      }}

      pos += chunk;
    }}

    /* Check if we've reached end of input */
    is_eof = (pos >= input_len);

    if(is_eof && !final_drain) {{
      /* Signal EOF to FSP context - no more chunks coming */
      ctx->more_chunks_expected = 0;
      final_drain = 1;
    }}

    /* Phase 2: Process tokens (only when buffer is full enough OR at EOF) */
    while(fsp_buffer_available(ctx) > 0 || (is_eof && final_drain)) {{
      {parser_upper}_STYPE lval;
      int token;

      /* Don't call lexer if buffer is low and more data is coming */
      if(!is_eof && fsp_buffer_available(ctx) < MIN_BUFFER_FOR_LEX)
        break;  /* Get more chunks first */

      /* Get next token from lexer */
      token = {lexer_prefix}_lex(&lval, scanner);

      if(token == 0) {{
        /* No more tokens available */
        if(!is_eof) {{
          /* Lexer needs more data but we have more chunks coming */
          break;
        }}
        /* Real EOF - done draining */
        final_drain = 0;
        break;
      }}

      /* Push token to parser */
      status = {parser_prefix}_push_parse(pstate, token, &lval, ctx, scanner);

      if(status != YYPUSH_MORE) {{
        /* Parse complete or error */
        if(status != 0)
          result = -1;
        goto cleanup;
      }}
    }}

    /* Exit loop if we're done draining at EOF */
    if(!final_drain && is_eof)
      break;
  }}

  /* Push final EOF to parser */
  status = {parser_prefix}_push_parse(pstate, 0, NULL, ctx, scanner);
  if(status != 0)
    result = -1;

cleanup:
  {parser_prefix}_pstate_delete(pstate);
  {lexer_prefix}_lex_destroy(scanner);

  return result;
}}
"""
    )


# ========================================================================
# VALIDATE: Configuration validation
# ========================================================================


class Severity(Enum):
    """Validation issue severity."""

    ERROR = "ERROR"
    WARNING = "WARNING"
    INFO = "INFO"


@dataclass
class ValidationIssue:
    """A validation issue found in lexer or parser."""

    severity: Severity
    message: str
    file: str
    line_number: Optional[int] = None


class LexerValidator:
    """Validates Flex lexer file for streaming compatibility."""

    def __init__(self, filename: str):
        self.filename = filename
        self.issues: List[ValidationIssue] = []
        self.options: dict[str, bool] = {}
        self.has_yy_input = False
        self.yy_input_calls_fsp = False

    def validate(self) -> List[ValidationIssue]:
        """Validate lexer file."""
        try:
            with open(self.filename, "r") as f:
                lines = f.readlines()
        except FileNotFoundError:
            self.issues.append(
                ValidationIssue(
                    severity=Severity.ERROR,
                    message=f"File not found: {self.filename}",
                    file=self.filename,
                )
            )
            return self.issues

        self._check_options(lines)
        self._check_yy_input(lines)

        return self.issues

    def _check_options(self, lines: List[str]) -> None:
        """Check for required Flex options."""
        for line in lines:
            line = line.strip()
            if line.startswith("%option"):
                options_str = line[7:].strip()
                for opt in options_str.split():
                    if opt.startswith("no"):
                        self.options[opt[2:]] = False
                    else:
                        self.options[opt] = True

        required_options = {
            "reentrant": "Required for push parser integration",
            "bison-bridge": "Required for passing yylval to parser",
        }

        for opt, reason in required_options.items():
            if opt not in self.options or not self.options[opt]:
                self.issues.append(
                    ValidationIssue(
                        severity=Severity.ERROR,
                        message=f"Missing required option '%option {opt}': {reason}",
                        file=self.filename,
                    )
                )

    def _check_yy_input(self, lines: List[str]) -> None:
        """Check for YY_INPUT macro definition."""
        in_definition = False
        definition_lines = []
        definition_start_line = 0

        for line_num, line in enumerate(lines, start=1):
            if re.search(r"#\s*define\s+YY_INPUT", line):
                self.has_yy_input = True
                in_definition = True
                definition_start_line = line_num
                definition_lines = [line]
                continue

            if in_definition:
                definition_lines.append(line)
                if not line.rstrip().endswith("\\"):
                    in_definition = False
                    full_definition = "".join(definition_lines)
                    if "fsp_read_input" in full_definition:
                        self.yy_input_calls_fsp = True
                    else:
                        self.issues.append(
                            ValidationIssue(
                                severity=Severity.ERROR,
                                message="YY_INPUT is defined but does not call fsp_read_input()",
                                file=self.filename,
                                line_number=definition_start_line,
                            )
                        )

        if not self.has_yy_input:
            self.issues.append(
                ValidationIssue(
                    severity=Severity.ERROR,
                    message="Missing YY_INPUT macro definition (required for streaming)",
                    file=self.filename,
                )
            )


class ParserValidator:
    """Validates Bison parser file for streaming compatibility."""

    def __init__(self, filename: str):
        self.filename = filename
        self.issues: List[ValidationIssue] = []
        self.api_settings: dict[str, str] = {}

    def validate(self) -> List[ValidationIssue]:
        """Validate parser file."""
        try:
            with open(self.filename, "r") as f:
                lines = f.readlines()
        except FileNotFoundError:
            self.issues.append(
                ValidationIssue(
                    severity=Severity.ERROR,
                    message=f"File not found: {self.filename}",
                    file=self.filename,
                )
            )
            return self.issues

        self._check_api_defines(lines)

        return self.issues

    def _check_api_defines(self, lines: List[str]) -> None:
        """Check for %define api.* directives."""
        for line in lines:
            line = line.strip()
            match = re.match(r"%define\s+api\.([a-z\-\.]+)\s+(.+)", line)
            if match:
                api_key = match.group(1)
                api_value = match.group(2).strip()
                self.api_settings[api_key] = api_value

        if "push-pull" in self.api_settings:
            if self.api_settings["push-pull"] != "push":
                self.issues.append(
                    ValidationIssue(
                        severity=Severity.ERROR,
                        message=f"api.push-pull is '{self.api_settings['push-pull']}' "
                        "but must be 'push' for streaming",
                        file=self.filename,
                    )
                )
        else:
            self.issues.append(
                ValidationIssue(
                    severity=Severity.ERROR,
                    message="Missing '%define api.push-pull push' (required for streaming)",
                    file=self.filename,
                )
            )

        if "pure" in self.api_settings:
            if self.api_settings["pure"] not in ["true", "full"]:
                self.issues.append(
                    ValidationIssue(
                        severity=Severity.ERROR,
                        message=f"api.pure is '{self.api_settings['pure']}' but should be 'full' or 'true'",
                        file=self.filename,
                    )
                )
        else:
            self.issues.append(
                ValidationIssue(
                    severity=Severity.ERROR,
                    message="Missing '%define api.pure full' (required for reentrant parser)",
                    file=self.filename,
                )
            )


def print_issues(issues: List[ValidationIssue], strict: bool = False) -> int:
    """Print validation issues and return exit code."""
    if not issues:
        print("✓ All checks passed")
        return 0

    errors = [i for i in issues if i.severity == Severity.ERROR]
    warnings = [i for i in issues if i.severity == Severity.WARNING]
    infos = [i for i in issues if i.severity == Severity.INFO]

    for issue in errors:
        location = issue.file
        if issue.line_number:
            location += f":{issue.line_number}"
        print(f"{location}: ERROR: {issue.message}", file=sys.stderr)

    for issue in warnings:
        location = issue.file
        if issue.line_number:
            location += f":{issue.line_number}"
        severity_str = "ERROR" if strict else "WARNING"
        stream = sys.stderr if strict else sys.stdout
        print(f"{location}: {severity_str}: {issue.message}", file=stream)

    for issue in infos:
        location = issue.file
        if issue.line_number:
            location += f":{issue.line_number}"
        print(f"{location}: INFO: {issue.message}")

    print()
    if errors:
        print(f"✗ {len(errors)} error(s) found - streaming will NOT work", file=sys.stderr)
        return 1
    elif warnings and strict:
        print(f"✗ {len(warnings)} warning(s) found (treated as errors in strict mode)", file=sys.stderr)
        return 1
    elif warnings:
        print(f"⚠ {len(warnings)} warning(s) found - streaming may work but configuration is incomplete")
        return 2
    else:
        print(f"✓ {len(infos)} informational note(s)")
        return 0


# ========================================================================
# MAIN
# ========================================================================


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="libfsp helper - Calculate, generate, and validate streaming parsers",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # Calculate subcommand
    calc = subparsers.add_parser("calculate", aliases=["calc"], help="Calculate MIN_BUFFER_FOR_LEX value")
    calc.add_argument("lexer_file", help="Flex lexer file (.l)")
    calc.add_argument("-v", "--verbose", action="store_true", help="Show detailed analysis")
    calc.add_argument("-q", "--quiet", action="store_true", help="Only output the number")

    # Generate subcommand
    gen = subparsers.add_parser("generate", aliases=["gen"], help="Generate streaming parser implementation")
    gen.add_argument("--lexer-prefix", required=True, help="Lexer function prefix (e.g., turtle_lexer)")
    gen.add_argument("--parser-prefix", required=True, help="Parser function prefix (e.g., turtle_parser)")
    gen.add_argument("--min-buffer", type=int, default=64, help="MIN_BUFFER_FOR_LEX value (default: 64)")
    gen.add_argument(
        "--function-name", help="Generated function name (default: {lexer_prefix}_streaming_parse)"
    )
    gen.add_argument("-o", "--output", type=argparse.FileType("w"), default=sys.stdout, help="Output file")

    # Validate subcommand
    val = subparsers.add_parser("validate", aliases=["val"], help="Validate lexer/parser configuration")
    val.add_argument("--lexer", "-l", help="Flex lexer file (.l)")
    val.add_argument("--parser", "-p", help="Bison parser file (.y)")
    val.add_argument("--strict", "-s", action="store_true", help="Treat warnings as errors")

    # Check subcommand (calculate + validate)
    check = subparsers.add_parser("check", help="Calculate MIN_BUFFER and validate configuration")
    check.add_argument("--lexer", "-l", required=True, help="Flex lexer file (.l)")
    check.add_argument("--parser", "-p", help="Bison parser file (.y)")
    check.add_argument("--strict", "-s", action="store_true", help="Treat warnings as errors")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Calculate
    if args.command in ["calculate", "calc"]:
        calculate_min_buffer(args.lexer_file, verbose=args.verbose, quiet=args.quiet)
        return 0

    # Generate
    elif args.command in ["generate", "gen"]:
        function_name = args.function_name or f"{args.lexer_prefix}_streaming_parse"
        generate_streaming_parser(
            lexer_prefix=args.lexer_prefix,
            parser_prefix=args.parser_prefix,
            min_buffer=args.min_buffer,
            function_name=function_name,
            output=args.output,
        )
        if args.output != sys.stdout:
            args.output.close()
        return 0

    # Validate
    elif args.command in ["validate", "val"]:
        if not args.lexer and not args.parser:
            print("Error: Must specify --lexer, --parser, or both", file=sys.stderr)
            return 1

        all_issues = []
        if args.lexer:
            lexer_validator = LexerValidator(args.lexer)
            all_issues.extend(lexer_validator.validate())
        if args.parser:
            parser_validator = ParserValidator(args.parser)
            all_issues.extend(parser_validator.validate())

        return print_issues(all_issues, strict=args.strict)

    # Check (calculate + validate)
    elif args.command == "check":
        print("=" * 70)
        print("STEP 1: Calculate MIN_BUFFER_FOR_LEX")
        print("=" * 70)
        min_buffer = calculate_min_buffer(args.lexer, verbose=False, quiet=False)

        print()
        print("=" * 70)
        print("STEP 2: Validate Configuration")
        print("=" * 70)

        all_issues = []
        lexer_validator = LexerValidator(args.lexer)
        all_issues.extend(lexer_validator.validate())

        if args.parser:
            parser_validator = ParserValidator(args.parser)
            all_issues.extend(parser_validator.validate())

        result = print_issues(all_issues, strict=args.strict)

        if result == 0:
            print()
            print("=" * 70)
            print("SUMMARY")
            print("=" * 70)
            print(f"✓ Configuration is correct for streaming")
            print(f"✓ Recommended MIN_BUFFER_FOR_LEX: {min_buffer}")

        return result

    return 1


if __name__ == "__main__":
    sys.exit(main())
