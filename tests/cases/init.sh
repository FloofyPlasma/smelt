#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

init_with() {
  printf "$1" >"$TMP/in"
  set +e
  "$SMELT" init <"$TMP/in" >"$TMP/out" 2>&1
  RC=$?
  set -e
}

mkdir -p "$TMP/my-app" && cd "$TMP/my-app"
init_with '\n\n\n\n\n'
assert_eq "$RC" 0 "init with defaults"
assert_file src/my-app/main.cpp "exe source"

run_smelt check
assert_eq "$RC" 0 "init project checks"
run_smelt build
assert_eq "$RC" 0 "init project builds"
run_smelt run
assert_eq "$RC" 0 "init program runs"
assert_contains "$(cat "$TMP/out")" "hello from my-app" "init program output"

init_with '\n\n\n\n\n'
assert_eq "$RC" 1 "init refuses to overwrite"
assert_contains "$(cat "$TMP/out")" "already exists" "overwrite message"

mkdir -p "$TMP/eof-app" && cd "$TMP/eof-app"
set +e
"$SMELT" init </dev/null >"$TMP/out" 2>&1
RC=$?
set -e
assert_eq "$RC" 0 "end of input takes defaults"
assert_file src/eof-app/main.cpp "defaults at end of input"

mkdir -p "$TMP/lib-dir" && cd "$TMP/lib-dir"
init_with 'mylib\n0.2.0\nstatic_lib\n17\n\n'
assert_eq "$RC" 0 "init library"
assert_file include/mylib.hpp "library header"
assert_file src/mylib/mylib.cpp "library source"
run_smelt build
assert_eq "$RC" 0 "library builds"
assert_file build/debug/mylib/libmylib.a "library archive"

mkdir -p "$TMP/retry" && cd "$TMP/retry"
init_with 'bad name\nmy-app\nnot-a-version\n0.3\n\n\n\n'
assert_eq "$RC" 0 "init after invalid answers"
assert_contains "$(cat project.smelt)" 'project "my-app"' "name after retry"
assert_contains "$(cat project.smelt)" 'version = "0.3"' "version after retry"

mkdir -p "$TMP/no" && cd "$TMP/no"
init_with 'x\n\n\n\nn\n'
assert_eq "$RC" 1 "declined init exit status"
assert_contains "$(cat "$TMP/out")" "nothing written" "declined message"
assert_no_path project.smelt "declined init wrote project.smelt"

echo "ok"
