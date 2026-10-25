#!/bin/sh

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CXX=${CXX:-clang++}
OUT="$ROOT/build/stage0"

mkdir -p "$OUT"

"$CXX" \
  -std=c++20 \
  -O2 \
  -Wall -Wextra \
  -ffile-prefix-map="$ROOT"=. \
  -I "$ROOT/src/smelt_core" \
  "$ROOT/src/smelt_core/token.cpp" \
  "$ROOT/src/smelt_core/lexer.cpp" \
  "$ROOT/src/smelt_core/parser.cpp" \
  "$ROOT/src/smelt_core/sema.cpp" \
  "$ROOT/src/smelt_core/glob.cpp" \
  "$ROOT/src/smelt_core/process.cpp" \
  "$ROOT/src/smelt_core/discover.cpp" \
  "$ROOT/src/smelt_core/workspace.cpp" \
  "$ROOT/src/smelt_core/build.cpp" \
  "$ROOT/src/smelt_core/lock.cpp" \
  "$ROOT/src/smelt_core/git.cpp" \
  "$ROOT/src/smelt_core/fetch.cpp" \
  "$ROOT/src/smelt/main.cpp" \
  -o "$OUT/smelt"

echo "built $OUT/smelt"
