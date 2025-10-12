dnl Process this file with autoconf to produce a configure script.
AC_INIT([libfsp], [0.1.0], [https://github.com/dajobe/libfsp/issues])
AC_CONFIG_SRCDIR([fsp.c])
AC_CONFIG_AUX_DIR(build)
AC_CONFIG_MACRO_DIR(build)
AM_INIT_AUTOMAKE([1.11 foreign])
AC_CONFIG_HEADERS([config.h])

dnl Checks for programs
AC_CANONICAL_HOST
AM_SANITY_CHECK
AM_PROG_AR
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AM_PROG_CC_C_O

dnl Check for AWK (needed for version checking)
AC_PROG_AWK

dnl Check if compiler is clang
AC_MSG_CHECKING(whether $CC is clang)
CC_IS_CLANG=no
if $CC 2>&1 | grep clang >/dev/null 2>&1; then
  CC_IS_CLANG=yes
else
  :
fi
AC_MSG_RESULT($CC_IS_CLANG)

dnl Initialize libtool
LT_INIT

AM_MAINTAINER_MODE

dnl Macro to test if compiler supports a warning flag
AC_DEFUN([FSP_CC_TRY_FLAG], [
  AC_MSG_CHECKING([whether $CC supports $1])

  dnl backup CFLAGS and werror status
  fsp_save_CFLAGS="$CFLAGS"
  fsp_save_ac_c_werror_flag="${ac_c_werror_flag}"
  AC_LANG_WERROR

  CFLAGS="$CFLAGS $1"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([ ])], [fsp_cc_flag=yes], [fsp_cc_flag=no])

  dnl restore CFLAGS and werror status
  CFLAGS="$fsp_save_CFLAGS"
  ac_c_werror_flag="${fsp_save_ac_c_werror_flag}"

  if test "X$fsp_cc_flag" = "Xyes"; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi
  AC_MSG_RESULT($fsp_cc_flag)
])

dnl Compiler warning flags for strict compilation
dnl Generated code from flex/bison must compile warning-free
dnl Based on Raptor's warning flag detection

dnl GCC warning options
dnl https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
dnl
dnl Too noisy to enable always:
dnl  -Wmissing-field-initializers : too noisy
dnl  -Wsystem-headers   : not debugging system
dnl  -Wunused-parameter : variables can be marked  __attribute__('unused')
possible_warnings="\
-std=c11 \
-Wall \
-Wc++-compat \
-Wextra \
-Wpedantic \
-Wunused \
\
-Waggregate-return \
-Wbad-function-cast \
-Wcast-align \
-Wdisabled-optimization \
-Wendif-labels \
-Werror-implicit-function-declaration \
-Wfloat-equal \
-Wformat=2 \
-Wframe-larger-than=6000 \
-Winit-self \
-Winline \
-Wmissing-declarations \
-Wmissing-format-attribute \
-Wmissing-noreturn \
-Wmissing-prototypes \
-Wnested-externs \
-Wold-style-definition \
-Wpacked \
-Wpointer-arith \
-Wredundant-decls \
-Wshadow \
-Wsign-compare \
-Wstrict-overflow \
-Wstrict-prototypes \
-Wswitch-enum \
-Wunreachable-code \
-Wunsafe-loop-optimizations \
-Wwrite-strings \
\
-Wno-missing-field-initializers \
-Wno-system-headers \
-Wno-unused-parameter \
-Wswitch-bool \
-Wlogical-not-parentheses \
-Wsizeof-array-argument  \
-Wbool-compare \
-Wc99-c11-compat \
"

extra_compiler_cflags=""

dnl compiler specific warnings
if test $CC_IS_CLANG = yes; then
    dnl Always enable this for Clang
    dnl  -Wno-nullability-completeness : too noisy on OSX reporting
    dnl     warnings in stdio.h
    extra_compiler_cflags="$extra_compiler_cflags \
-Wno-nullability-completeness \
"
fi

dnl OS specific warnings
case "${host_os}" in
    darwin*)
	dnl Apple gcc/clang specific:
	dnl  -Wshorten-64-to-32
	possible_warnings="$possible_warnings \
-Wshorten-64-to-32 \
"
        ;;
    *)
	dnl  -Wundef : too noisy on OSX 10.12+ e.g. stdio.h fails
	possible_warnings="$possible_warnings \
-Wundef \
"
	;;
esac

warning_cflags=
if test "$USE_MAINTAINER_MODE" = yes; then
  AC_MSG_CHECKING(for supported $CC warning flags)
  AC_MSG_RESULT($warning_cflags)
  for warning in $possible_warnings; do
    FSP_CC_TRY_FLAG([$warning], [warning_cflags="$warning_cflags $warning"])
  done
  AC_MSG_CHECKING($CC supports warning flags)
  AC_MSG_RESULT($warning_cflags)
fi

if test "$USE_MAINTAINER_MODE" = yes; then
  AC_DEFINE([MAINTAINER_MODE], [1], [Define to 1 if maintainer mode is enabled.])
  CPPFLAGS="$warning_cflags $CPPFLAGS"
fi
dnl Extra compiler flags to always add
CPPFLAGS="$extra_compiler_cflags $CPPFLAGS"

dnl Flex detection (borrowed from Raptor)
FLEX_MIN_VERSION=2.5.19
FLEX_REC_VERSION=2.5.36

# Do not want AM_PROG_LEX which adds 'missing' to LEX if it's not around
# Just find flex binary, don't check for lex library (we use %option noyywrap)
AC_CHECK_PROGS(LEX, flex lex, :)
LEXLIB=
AC_SUBST(LEX_OUTPUT_ROOT, lex.yy)
AC_MSG_CHECKING(flex)
if test "$USE_MAINTAINER_MODE" = yes; then
  # maintainer mode - flex is required
  if test "X$LEX" = "X:" ; then
    AC_MSG_RESULT(not present)
    AC_MSG_WARN(Please get flex from https://github.com/westes/flex)
    AC_MSG_WARN(version $FLEX_MIN_VERSION ($FLEX_REC_VERSION recommended))
    AC_MSG_FAILURE(flex not present)
  fi

  # some kind of lexer is present
  if echo "$LEX" | grep flex >/dev/null 2>&1; then
    # flex is present
    FLEX_VERSION=`$LEX -V 2>&1 | $AWK '{print $2}'`
    FLEX_VERSION_DEC=`echo $FLEX_VERSION | $AWK -F. '{printf("%d\n", 10000*$1 + 100*$2 + $3)};'`

    FLEX_MIN_VERSION_DEC=`echo $FLEX_MIN_VERSION | $AWK -F. '{printf("%d\n", 10000*$1 + 100*$2 + $3)};'`

    if test $FLEX_VERSION_DEC -ge $FLEX_MIN_VERSION_DEC; then
      AC_MSG_RESULT($FLEX_VERSION - OK)
    else
      AC_MSG_RESULT(version $FLEX_VERSION - too old)
      AC_MSG_WARN(Please get flex from https://github.com/westes/flex)
      AC_MSG_WARN(version $FLEX_MIN_VERSION ($FLEX_REC_VERSION recommended))
      AC_MSG_FAILURE(flex is too old)
    fi
  else
    AC_MSG_RESULT(present - but is not flex)
    AC_MSG_WARN(Please get flex from https://github.com/westes/flex)
    AC_MSG_WARN(version $FLEX_MIN_VERSION ($FLEX_REC_VERSION recommended))
    AC_MSG_FAILURE($LEX is not not flex)
  fi
else
  # not maintainer mode; flex is not required
  AC_MSG_RESULT(not present - not required for non maintainer builds)
  LEX="$SHELL $missing_dir/missing flex"
  AC_SUBST(LEX_OUTPUT_ROOT, lex.yy)
  AC_SUBST(LEXLIB, '')
  FLEX_VERSION_DEC=00000
fi
AC_DEFINE_UNQUOTED(FLEX_VERSION_DECIMAL, $FLEX_VERSION_DEC, [Flex version as a decimal])

dnl Bison detection (borrowed from Raptor)
BISON_MIN_VERSION=3.4.0
BISON_REC_VERSION=3.7.2
BISON_MIN_VERSION_DEC=`echo $BISON_MIN_VERSION | $AWK -F. '{printf("%d\n", 10000*$1 + 100*$2 + $3)};'`

if test "$USE_MAINTAINER_MODE" = yes; then
  # Match these styles of versions
  # GNU Bison version 1.28
  # bison (GNU Bison) 1.875
  AC_CACHE_CHECK([for GNU bison newer than $BISON_MIN_VERSION], [ac_cv_path_BISON],
    [AC_PATH_PROGS_FEATURE_CHECK([BISON], [bison3 bison],
      [[bison_version=`$ac_path_BISON --version 2>&1 | sed -ne 's/^.*GNU Bison[^0-9]*//p'`
        bison_version_dec=`echo $bison_version | $AWK -F. '{printf("%d\n", 10000*$1 + 100*$2 + $3)};'`
       test "$bison_version_dec" -ge $BISON_MIN_VERSION_DEC \
	&& ac_cv_path_BISON=$ac_path_BISON ac_cv_version_BISON=$bison_version ac_cv_version_dec_BISON=$bison_version_dec]],
      [AC_MSG_ERROR([could not find new enough GNU Bison])])])
  AC_MSG_NOTICE([Using GNU Bison $ac_cv_version_BISON ($ac_cv_version_dec_BISON) at $ac_cv_path_BISON])
else
  # not maintainer mode, do not need bison
  ac_cv_path_BISON=:
fi
AC_SUBST([BISON], [$ac_cv_path_BISON])

dnl Find Python 3 for fix scripts
AC_CHECK_PROGS(PYTHON3, python3 python)

dnl Checks for header files
AC_CHECK_HEADERS([stdlib.h string.h])

dnl Checks for typedefs, structures, and compiler characteristics
AC_C_CONST
AC_C_INLINE

dnl Checks for library functions
AC_CHECK_FUNCS([memmove memcpy strdup])

AC_CONFIG_FILES([
  Makefile
])

AC_OUTPUT

AC_MSG_RESULT([
libfsp build summary:
  Compiler                : $CC
  Compiler is clang       : $CC_IS_CLANG
  Maintainer mode         : ${USE_MAINTAINER_MODE:-no}
  Warning flags           : ${warning_cflags:-none (not in maintainer mode)}
  Extra compiler flags    : ${extra_compiler_cflags:-none}
  Flex version            : ${FLEX_VERSION:-not required}
  Bison version           : ${ac_cv_version_BISON:-not required}
  Python3                 : ${PYTHON3:-not found}
])
