#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/build/stage0/smelt"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp -R "$ROOT/tests/fixtures/withdep" "$T/withdep"
cp -R "$ROOT/tests/fixtures/mathlib" "$T/mathlib"

cd "$T/withdep"
"$S" compdb >/dev/null

DB=compile_commands.json
test -f "$DB" || {
  echo "FAIL: no compile_commands.json"
  exit 1
}

grep -q '"file": ".*main\.cpp"' "$DB" || {
  echo "FAIL: main.cpp missing"
  exit 1
}
grep -q '"file": ".*add\.cpp"' "$DB" || {
  echo "FAIL: add.cpp missing"
  exit 1
}
if grep -q 'unused\.cpp' "$DB"; then
  echo "FAIL: unreached target appears in database"
  exit 1
fi

grep -q '"-I[^"]*mathlib/include"' "$DB" || {
  echo "FAIL: include path"
  exit 1
}
grep -q '"-std=c++20"' "$DB" || {
  echo "FAIL: standard"
  exit 1
}

if [ -d build ]; then
  echo "FAIL: compdb created build/"
  exit 1
fi

echo "compdb tests passed"
