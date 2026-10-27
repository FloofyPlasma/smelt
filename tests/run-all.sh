#!/bin/sh
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

KEEP_GOING=0
BUILD=1
ONLY=""
for arg in "$@"; do
    case "$arg" in
    --keep-going) KEEP_GOING=1 ;;
    --no-build) BUILD=0 ;;
    -*)
        echo "usage: tests/run-all.sh [--keep-going] [--no-build] [NAME...]" >&2
        exit 2
        ;;
    *) ONLY="$ONLY $arg" ;;
    esac
done

LOGS="$ROOT/build/test-logs"
mkdir -p "$LOGS"

if [ "$BUILD" -eq 1 ]; then
    printf '%-20s ' "stage 0"
    if bootstrap/stage0.sh >"$LOGS/stage0.log" 2>&1; then
        echo "ok"
    else
        echo "FAILED"
        tail -n 40 "$LOGS/stage0.log" | sed 's/^/    /'
        exit 1
    fi
fi

selected() {
    [ -z "$ONLY" ] && return 0
    case " $ONLY " in
    *" $1 "*) return 0 ;;
    esac
    return 1
}

PASS=0
FAIL=0
FAILED=""

for t in tests/cases/*.sh; do
    name=$(basename "$t" .sh)
    case "$name" in
    lib | run-all) continue ;;
    esac
    selected "$name" || continue

    printf '%-20s ' "$name"
    timeout 600 sh "$t" >"$LOGS/$name.log" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "ok"
        PASS=$((PASS + 1))
    else
        if [ "$rc" -eq 124 ]; then
            echo "FAILED (timed out)"
        else
            echo "FAILED (exit $rc)"
        fi
        FAIL=$((FAIL + 1))
        FAILED="$FAILED $name"
        tail -n 40 "$LOGS/$name.log" | sed 's/^/    /'
        [ "$KEEP_GOING" -eq 1 ] || break
    fi
done

echo
echo "passed: $PASS  failed: $FAIL"
if [ "$FAIL" -ne 0 ]; then
    echo "failed:$FAILED"
    exit 1
fi
