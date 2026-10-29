#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

expect_error() {
    mkdir -p "$TMP/$1"
    printf '%s\n' "$2" >"$TMP/$1/project.smelt"
    cd "$TMP/$1"
    set +e
    timeout 5 "$SMELT" build >"$TMP/out" 2>&1
    RC=$?
    set -e
    [ "$RC" -ne 124 ] || fail "$1: hung"
    [ "$RC" -ne 0 ] || fail "$1: accepted bad input"
    assert_contains "$(cat "$TMP/out")" "$3" "$1"
}

expect_error stray_brace \
    'project "p" { version = "0.1.0"; } }' "expected a declaration"

expect_error unknown_key \
    'project "p" { version = "0.1.0"; colour = "red"; }' "unknown setting 'colour'"

expect_error sources_key \
    'project "p" { version = "0.1.0"; }
target "a" { type = exe; sources = ["x.c"]; }' "'sources' is not a valid setting"

expect_error missing_semicolon \
    'project "p" { version = "0.1.0" }' "expected ';'"

expect_error unterminated_block \
    'project "p" { version = "0.1.0";' "expected '}'"

expect_error call_unclosed \
    'project "p" { version = f("a"; }' "expected ',' or ')' in call"

expect_error call_nested_wrong_type \
    'project "p" { version = f(g("a")); }' "'version' must be a quoted string"

echo "ok"
