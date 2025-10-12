#!/usr/bin/env python3
"""
Calculate MIN_BUFFER_FOR_LEX from a Flex lexer file.

Analyzes a .l file to find the longest fixed-length token pattern,
which determines the minimum buffer size needed for proper streaming.

Usage:
    python3 calculate-min-buffer.py lexer.l
"""

import re
import sys
from typing import List, Tuple, Optional


def analyze_flex_pattern(pattern: str) -> Tuple[Optional[int], str]:
    """
    Analyze a flex pattern and return (min_length, description).
    Returns (None, reason) if pattern can match arbitrary-length tokens.
    Returns (length, desc) for fixed-length patterns.
    """
    # Remove outer quotes if present
    pattern = pattern.strip()
    if pattern.startswith('"') and pattern.endswith('"'):
        pattern = pattern[1:-1]

    # Patterns that can match arbitrary length
    if any(op in pattern for op in ['*', '+']):
        return (None, f"variable length (contains * or +)")

    # Check for character classes with quantifiers
    if re.search(r'\{[^}]+\}', pattern):
        # Extract the quantifier
        match = re.search(r'\{(\d+)(?:,(\d+))?\}', pattern)
        if match:
            min_q = int(match.group(1))
            max_q = match.group(2)
            if max_q is None:
                # Exact count {n}
                return (None, f"quantifier requires analysis")
            else:
                # Range {n,m}
                return (None, f"variable length quantifier")

    # Check for start conditions that accumulate
    if pattern.startswith('<'):
        return (None, "start condition pattern (accumulates)")

    # Simple literal string or fixed pattern
    # Count minimum characters needed
    # Remove escaped characters, regex operators, etc. for estimation
    cleaned = pattern
    # Remove escape sequences
    cleaned = re.sub(r'\\[nrt"\\]', 'X', cleaned)
    # Remove character classes (estimate as 1 char)
    cleaned = re.sub(r'\[[^\]]+\]', 'X', cleaned)
    # Remove optional parts
    cleaned = re.sub(r'\?', '', cleaned)
    # Count remaining characters
    min_len = len(cleaned)

    # If pattern is just a literal keyword
    if re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', pattern):
        return (len(pattern), f'keyword "{pattern}"')

    # If pattern is a literal string
    if re.match(r'^[^[\\(){}*+?|]+$', pattern):
        return (len(pattern), f'literal "{pattern[:20]}"')

    # Has regex operators but no * or +
    return (min_len, f'pattern requires ~{min_len} chars')


def extract_rules_from_flex(filename: str) -> List[Tuple[str, str]]:
    """
    Extract token rules from a flex file.
    Returns list of (pattern, action) tuples.
    """
    with open(filename, 'r') as f:
        content = f.read()

    # Find the rules section (between %% markers)
    parts = re.split(r'^%%\s*$', content, flags=re.MULTILINE)
    if len(parts) < 2:
        print(f"Warning: No rules section found in {filename}", file=sys.stderr)
        return []

    rules_section = parts[1]

    # Extract rules - only lines that start with a pattern at column 0 or after whitespace
    rules = []
    lines = rules_section.split('\n')
    i = 0
    in_action = False
    brace_depth = 0

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        i += 1

        # End of rules section
        if stripped == '%%':
            break

        # Skip comments
        if stripped.startswith('/*') or stripped.startswith('*') or stripped.startswith('//'):
            continue

        # Skip pattern definitions
        if stripped.startswith('%x') or stripped.startswith('%s'):
            continue

        # Skip empty lines
        if not stripped:
            in_action = False
            brace_depth = 0
            continue

        # Track brace depth to skip multi-line actions
        if in_action:
            brace_depth += line.count('{') - line.count('}')
            if brace_depth <= 0:
                in_action = False
                brace_depth = 0
            continue

        # A rule line must start with a pattern (not indented inside action)
        # Pattern can be: literal "...", <STATE>pattern, {NAME}, regex, or .
        if not line[0].isspace() or stripped.startswith('<') or stripped.startswith('"'):
            # This looks like a pattern line
            # Pattern is first token, followed by whitespace and action
            match = re.match(r'^(\S+)\s+(.*)$', stripped)
            if match:
                pattern = match.group(1)
                rest = match.group(2)

                # Skip pattern definitions (uppercase identifiers at start)
                if re.match(r'^[A-Z_]+$', pattern):
                    continue

                # Check if action starts with {
                if rest.strip().startswith('{'):
                    brace_depth = rest.count('{') - rest.count('}')
                    if brace_depth > 0:
                        in_action = True

                rules.append((pattern, rest))

    return rules


def calculate_min_buffer(filename: str, verbose: bool = False) -> int:
    """
    Calculate the minimum buffer size needed for a flex file.
    """
    rules = extract_rules_from_flex(filename)

    if not rules:
        print(f"No rules found in {filename}", file=sys.stderr)
        return 16  # Safe default

    fixed_lengths = []
    variable_patterns = []
    pattern_refs = []

    print(f"\nAnalyzing {filename}...")
    print("=" * 70)

    for pattern, action in rules:
        # Check if this is a pattern reference like {IDENT}
        if re.match(r'^\{[A-Z_]+\}$', pattern):
            pattern_refs.append(pattern)
            if verbose:
                print(f"  {pattern:30s} → REF     : pattern reference (check definition)")
            continue

        min_len, desc = analyze_flex_pattern(pattern)

        if verbose:
            if min_len is not None:
                print(f"  {pattern:30s} → {min_len:3d} bytes: {desc}")
            else:
                print(f"  {pattern:30s} → VAR     : {desc}")

        if min_len is not None:
            fixed_lengths.append((min_len, pattern, desc))
        else:
            variable_patterns.append((pattern, desc))

    print("=" * 70)

    if not fixed_lengths:
        print("\nNo fixed-length patterns found.")
        print("MIN_BUFFER_FOR_LEX = 16 (safe minimum)")
        return 16

    # Find the longest fixed-length pattern
    max_fixed = max(fixed_lengths, key=lambda x: x[0])
    max_len, max_pattern, max_desc = max_fixed

    print(f"\nFixed-length patterns found: {len(fixed_lengths)}")
    print(f"Variable-length patterns: {len(variable_patterns)}")
    print()
    print(f"Longest fixed-length token:")
    print(f"  Pattern: {max_pattern}")
    print(f"  Length:  {max_len} bytes")
    print(f"  Description: {max_desc}")
    print()

    # Recommend a value with some headroom
    recommended = max(16, ((max_len + 7) // 8) * 8)  # Round up to next multiple of 8
    if recommended < 32:
        recommended = 32

    print(f"Recommended MIN_BUFFER_FOR_LEX:")
    print(f"  Minimum safe: {max_len} bytes (exact)")
    print(f"  Recommended:  {recommended} bytes (with headroom)")
    print()
    print(f"Variable-length patterns (strings, comments, URIs, etc.):")
    for pattern, desc in variable_patterns[:5]:  # Show first 5
        print(f"  {pattern:30s} : {desc}")
    if len(variable_patterns) > 5:
        print(f"  ... and {len(variable_patterns) - 5} more")
    print()
    print("NOTE: Variable-length tokens (strings, URIs, comments) can be")
    print("arbitrarily long. MIN_BUFFER_FOR_LEX only needs to be large enough")
    print("for Flex to RECOGNIZE the token delimiter (e.g., opening quote).")
    print("Once recognized, Flex accumulates the content incrementally via")
    print("YY_INPUT calls, so megabyte-long strings work correctly.")
    print()

    return recommended


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Calculate MIN_BUFFER_FOR_LEX from a Flex lexer file"
    )
    parser.add_argument(
        "lexer_file",
        help="Path to .l (Flex lexer) file"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show all patterns"
    )
    parser.add_argument(
        "-q", "--quiet",
        action="store_true",
        help="Only output the numeric value"
    )

    args = parser.parse_args()

    try:
        recommended = calculate_min_buffer(args.lexer_file, verbose=args.verbose)

        if args.quiet:
            print(recommended)
        else:
            print("=" * 70)
            print(f"\n#define MIN_BUFFER_FOR_LEX {recommended}")
            print()
            print("Add this to your streaming parser code.")
            print("See README.md for usage details.")

    except FileNotFoundError:
        print(f"Error: File not found: {args.lexer_file}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
