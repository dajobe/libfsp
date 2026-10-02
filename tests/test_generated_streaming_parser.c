/* Compile the real fsp-helper.py output as part of the test program. */
#include <stdlib.h>

#define TEST_PARSER_DISCARD_LVAL(lval) do { free((lval)->string); } while(0)
#include "tests/test_generated_streaming_parser.inc"
