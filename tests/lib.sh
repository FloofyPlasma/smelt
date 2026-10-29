#!/bin/sh
set -eu

ROOT=${SMELT_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
SMELT="$ROOT/build/stage0/smelt"

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=commit.gpgsign
export GIT_CONFIG_VALUE_0=false
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test
export GIT_TERMINAL_PROMPT=0
export SMELT_TEST_LOCAL_GIT=1
unset SMELT_REGISTRY

TMP=""
RC=0

make_tmp() {
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
}

fail() {
    echo "FAIL: $*" >&2
    if [ -n "$TMP" ] && [ -f "$TMP/out" ]; then
        echo "--- output of the last command ---" >&2
        sed 's/^/    /' "$TMP/out" >&2
    fi
    exit 1
}

assert_eq() { # assert_eq GOT WANT MESSAGE
    [ "$1" = "$2" ] || fail "$3: got '$1', want '$2'"
}

assert_contains() { # assert_contains TEXT NEEDLE MESSAGE
    case "$1" in
    *"$2"*) ;;
    *) fail "$3: missing '$2'" ;;
    esac
}

assert_not_contains() { # assert_not_contains TEXT NEEDLE MESSAGE
    case "$1" in
    *"$2"*) fail "$3: unexpected '$2'" ;;
    esac
}

assert_file() { # assert_file PATH MESSAGE
    [ -f "$1" ] || fail "$2: missing $1"
}

assert_no_path() { # assert_no_path PATH MESSAGE
    [ ! -e "$1" ] || fail "$2: unexpected $1"
}

run_smelt() {
    set +e
    "$SMELT" "$@" >"$TMP/out" 2>&1
    RC=$?
    set -e
}
