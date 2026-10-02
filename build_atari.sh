#!/bin/bash
# Full rebuild of outrun.tos with the Cygwin m68k-atari-mint toolchain
# (no make available). Run through: C:\cygwin64\bin\bash.exe -lc "/cygdrive/c/claude/cannonball/build_atari.sh"
export PATH=/opt/cross-mint/bin:$PATH
cd "$(dirname "$0")"
CPU=${CPU:-68000}
OUT=${OUT:-OUTRUN.TOS}   # 8.3-safe default (6+3 chars) - override with OUT=... for other names, keep them 8.3 too
EXTRA=${EXTRA:-}
OPT=${OPT:--O2}
CXXFLAGS="-Wall $OPT -mcpu=$CPU $EXTRA -fomit-frame-pointer -std=gnu++0x -Isrc/main -DPLATFORM_ATARI"
OBJS=""
FAIL=0
for f in $(grep -E '^[[:space:]]+\$\(SRC\)/.*\.cpp' Makefile.atari | grep -oE '[A-Za-z0-9_/]+\.cpp' | sed 's|^|src/main/|' | sort -u) ; do
  o="${f%.cpp}.o"
  if ! m68k-atari-mint-g++ $CXXFLAGS -c "$f" -o "$o" 2> /tmp/err.txt; then
    echo "FAIL $f"; head -5 /tmp/err.txt; FAIL=1
  fi
  OBJS="$OBJS $o"
done
for s in src/main/atari/*.S; do
  m68k-atari-mint-gcc -mcpu=$CPU $EXTRA -c "$s" -o "${s%.S}.o" || FAIL=1
  OBJS="$OBJS ${s%.S}.o"
done
[ $FAIL = 0 ] && m68k-atari-mint-g++ -mcpu=${LINKCPU:-$CPU} -o $OUT $OBJS -lgem && echo BUILD_OK
