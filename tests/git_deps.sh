#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/build/stage0/smelt"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

export SMELT_TEST_LOCAL_GIT=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

mkdir -p "$T/up/include" "$T/up/src/core"
cat >"$T/up/project.smelt" <<'EOF'
project "up" {
    version = "1.0.0";
}

target "core" {
    type = static_lib;
    export = true;
    public_includes = ["include"];
}
EOF
echo 'int add(int a, int b);' >"$T/up/include/up.h"
printf '#include "up.h"\nint add(int a, int b) { return a + b; }\n' \
  >"$T/up/src/core/add.cpp"
git -C "$T/up" init -q
git -C "$T/up" add -A
git -C "$T/up" commit -m init
git -C "$T/up" tag v1.0.0

mkdir -p "$T/app/src/app"
cat >"$T/app/project.smelt" <<EOF
project "app" {
    version = "0.1.0";
}

dependency "up" {
    git = "file://$T/up";
    tag = "v1.0.0";
}

target "app" {
    type = exe;
    depends = ["up.core"];
}
EOF
cat >"$T/app/src/app/main.cpp" <<'EOF'
#include <cstdio>
#include "up.h"
int main() { std::printf("%d\n", add(2, 3)); return 0; }
EOF

cd "$T/app"

"$S" fetch
test -f smelt.lock
LOCKED=$(cat smelt.lock)
"$S" build >/dev/null
[ "$("$S" run | tail -n 1)" = "5" ] || {
  echo "FAIL: first run"
  exit 1
}

git -C "$T/up" commit -q --allow-empty -m moved
git -C "$T/up" tag -f v1.0.0
"$S" fetch
[ "$(cat smelt.lock)" = "$LOCKED" ] || {
  echo "FAIL: lock changed"
  exit 1
}
[ "$("$S" run | tail -n 1)" = "5" ] || {
  echo "FAIL: run after tag move"
  exit 1
}

rm -rf .smelt
if "$S" build >/dev/null 2>&1; then
  echo "FAIL: build succeeded without checkout"
  exit 1
fi
"$S" fetch
[ "$(cat smelt.lock)" = "$LOCKED" ] || {
  echo "FAIL: lock changed on restore"
  exit 1
}
[ "$("$S" run | tail -n 1)" = "5" ] || {
  echo "FAIL: run after restore"
  exit 1
}

echo "git dependency tests passed"
