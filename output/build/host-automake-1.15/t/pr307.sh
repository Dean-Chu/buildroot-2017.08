#! /bin/sh
# Copyright (C) 2002-2014 Free Software Foundation, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Test for PR 307: depcomp with depmode=dashmstdout libtool race condition
# Report from Laurent Morichetti.
# (Also exercises check_LTLIBRARIES.)
#
# == Report ==
#  The dashmstdout depmode calls libtool in parallel to generate the
#  dependencies (with -M flag) and to build the objfile (both have
#  --mode=compile and -o).
#  The process with 'libtool --mode=compile .* -M' can corrupt the objfile
#  as none is generated by the compiler.  Since --mode=compile and -o are
#  set libtool assumes that a objfile should be generated and will execute
#  invalid $mv & $LN_S.
#
# == Fix ==
#  Now 'depcomp' never compute dependencies in the background, as this can
#  cause races with libtool.  Compute the dependencies after the actual
#  compilation.

required='libtoolize gcc'
. test-init.sh

cat >> configure.ac << 'END'
AC_PROG_CC
AM_PROG_AR
AC_PROG_LIBTOOL
AC_OUTPUT
END

cat > Makefile.am << 'END'
check_LTLIBRARIES = librace.la
librace_la_SOURCES = a.c b.c c.c d.c e.c f.c g.c h.c

# Make sure the dependencies are updated.
check-local:
	for i in $(librace_la_SOURCES:.c=.Plo); do \
	  echo "checking ./$(DEPDIR)/$$i"; \
	  grep 'foo\.h' ./$(DEPDIR)/$$i >tst || exit 1; \
	  test `wc -l <tst` -eq 2 || exit 1; \
	done
END

: >foo.h

for i in a b c d e f g h; do
  unindent >$i.c <<EOF
    #include "foo.h"
    int $i () { return 0; }
EOF
done

libtoolize --force
$ACLOCAL
$AUTOCONF
$AUTOMAKE -a

# Sanity check: make sure the variable we are attempting to force
# is indeed used by configure.
grep am_cv_CC_dependencies_compiler_type configure

./configure am_cv_CC_dependencies_compiler_type=dashmstdout

$MAKE
test -f librace.la && exit 1
$MAKE check

# The failure we check usually occurs during the above build,
# with an output such as:
#
#   mv -f .libs/f.lo f.lo
#   mv: cannot stat '.libs/f.lo': No such file or directory
#
# (This may happen on 'f' or on some other files.)

test -f librace.la
test -f tst # A proof that check-local was run.

: