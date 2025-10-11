# -*- Mode: Makefile -*-
#
# Makefile for libfsp convienience library
#

ANALYZE = clang
ANALYZE_FLAGS = "--analyze"
# Based on COMPILE target
ANALYZE_COMMAND = $(ANALYZE) \
	$(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	$(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) \
	$(ANALYZE_FLAGS)

noinst_LTLIBRARIES = libfsp.la
AM_CPPFLAGS = -DFSP_CONFIG -I$(top_srcdir)/src -I$(builddir)

noinst_HEADERS = fsp_internal.h

libfsp_la_SOURCES = \
fsp.c \
fsp.h

EXTRA_DIST = \
fsp_test.c \
test_lexer.l \
test_parser.y \
scripts/postprocess-flex.py \
scripts/postprocess-bison.py \
scripts/README.md \
tests/simple.txt \
tests/simple.expected \
tests/triple-quoted.txt \
tests/triple-quoted.expected \
tests/mixed.txt \
tests/mixed.expected

# Generated source files for test parser/lexer and config
BUILT_SOURCES = fsp_config.h test_lexer.c test_lexer.h test_parser.c test_parser.h

# Create fsp_config.h from config.h for standalone builds
fsp_config.h: $(top_builddir)/config.h
	@$(LN_S) $(top_builddir)/config.h fsp_config.h 2>/dev/null || cp -f $(top_builddir)/config.h fsp_config.h || true

MAINTAINERCLEANFILES = test_lexer.c test_lexer.h \
test_parser.c test_parser.h test_parser.output

TESTS=fsp_test$(EXEEXT)

check_PROGRAMS=fsp_test$(EXEEXT)

CLEANFILES=*.plist test_lexer.c.t test_lexer.h.t fsp_config.h

fsp_test_SOURCES = fsp_test.c test_lexer.c test_parser.c
fsp_test_LDADD = $(builddir)/libfsp.la

if MAINTAINER_MODE

# Test lexer generation with postprocess-flex.py
# Note: postprocess-flex.py outputs to stdout, so we redirect to .t and move
test_lexer.c: $(srcdir)/test_lexer.l test_parser.c $(srcdir)/scripts/postprocess-flex.py
	$(AM_V_GEN) \
	$(LEX) -o$@ $(srcdir)/test_lexer.l; \
	for file in test_lexer.c test_lexer.h; do \
	  $(PYTHON3) $(srcdir)/scripts/postprocess-flex.py $$file > $$file.t || exit 1; \
	  mv -f $$file.t $$file; \
	done

test_lexer.h: test_lexer.c ; @exit 0

# Test parser generation with postprocess-bison.py
test_parser.c: $(srcdir)/test_parser.y $(srcdir)/scripts/postprocess-bison.py
	$(AM_V_GEN) \
	$(BISON) -o $@ $(srcdir)/test_parser.y; \
	$(PYTHON3) $(srcdir)/scripts/postprocess-bison.py $@

test_parser.h: test_parser.c ; @exit 0

# Run Clang static analyzer over sources.
analyze: $(SOURCES)
	@list='$(SOURCES)'; \
	result=0; \
	for file in $$list; do \
	  if echo $$file | grep '\.c$$' >/dev/null 2>&1; then \
	    $(RECHO) "Analyzing $$file"; \
	    $(ANALYZE_COMMAND) $(srcdir)/$$file; \
	    status=$$?; \
	    if test $$status != 0; then \
	      result=1; \
	    fi; \
	  fi; \
	done; \
	set -e; exit $$result
endif

# Some people need a little help ;-)
test: check

help:
	@echo "libfsp - Flex/Bison Streaming Parser library"
	@echo ""
	@echo "Available targets:"
	@echo "  make              - Build the library"
	@echo "  make check        - Build and run tests"
	@echo "  make test         - Alias for 'make check'"
	@echo "  make clean        - Remove built files"
	@echo "  make install      - Install library (when integrated)"
	@echo ""
	@echo "Generated files (lexer/parser):"
	@echo "  test_lexer.c/h    - Generated from test_lexer.l"
	@echo "  test_parser.c/h   - Generated from test_parser.y"
	@echo "  fsp_config.h      - Generated from config.h"
	@echo ""
	@echo "Helper scripts:"
	@echo "  scripts/postprocess-flex.py  - Post-process flex output"
	@echo "  scripts/postprocess-bison.py - Post-process bison output"
