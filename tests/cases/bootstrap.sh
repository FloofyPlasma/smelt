#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

cp -R "$ROOT" "$TMP/src"
rm -rf "$TMP/src/build"
cd "$TMP/src"

bootstrap/stage0.sh >/dev/null
build/stage0/smelt build --jobs 8 >/dev/null
cp build/debug/smelt/smelt "$TMP/stage1"
rm -rf build/debug
"$TMP/stage1" build --jobs 8 >/dev/null
cp build/debug/smelt/smelt "$TMP/stage2"

cmp "$TMP/stage1" "$TMP/stage2" || fail "stage 1 and stage 2 differ"
