#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
S="$ROOT/build/stage0/smelt"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

export SMELT_TEST_LOCAL_GIT=1
export SMELT_REGISTRY="file://$T/registry"
export GIT_TERMINAL_PROMPT=0
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

mkdir -p "$T/up/include" "$T/up/src/core"
echo 'int add(int a, int b);' >"$T/up/include/up.h"
printf '#include "up.h"\nint add(int a, int b) { return a + b; }\n' \
  >"$T/up/src/core/add.cpp"
git -C "$T/up" init -q
git -C "$T/up" add -A
git -C "$T/up" commit -q -m init
git -C "$T/up" tag release-1.0.0
U1=$(git -C "$T/up" rev-parse "release-1.0.0^{commit}")

write_entry() {
  d="$T/registry/packages/up/$1"
  mkdir -p "$d"
  cat >"$d/source.smelt" <<EOF
// upstream tag: release-$1
source "up" {
    git = "file://$T/up";
    commit = "$2";
}
EOF
  cat >"$d/overlay.smelt" <<'EOF'
project "up" {
    version = "1.0.0";
}

target "core" {
    type = static_lib;
    export = true;
    public_includes = ["include"];
}
EOF
}

write_entry 1.0.0 "$U1"
git -C "$T/registry" init -q
git -C "$T/registry" add -A
git -C "$T/registry" commit -q -m "1.0.0"

mkdir -p "$T/app/src/app"
cat >"$T/app/project.smelt" <<'EOF'
project "app" {
    version = "0.1.0";
}

dependency "up" {
    registry = "up";
    version = "1.0.0";
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
grep -q "$U1" smelt.lock || {
  echo "FAIL: 1.0.0 not locked"
  exit 1
}

git -C "$T/up" commit -q --allow-empty -m "release 1.1.0"
git -C "$T/up" tag release-1.1.0
U2=$(git -C "$T/up" rev-parse "release-1.1.0^{commit}")
write_entry 1.1.0 "$U2"
git -C "$T/registry" add -A
git -C "$T/registry" commit -q -m "1.1.0"
R2=$(git -C "$T/registry" rev-parse HEAD)

sed -i 's/version = "1.0.0"/version = "1.1.0"/' project.smelt
if "$S" build >/dev/null 2>&1; then
  echo "FAIL: build accepted an unlocked version"
  exit 1
fi
if "$S" fetch >/dev/null 2>&1; then
  echo "FAIL: fetch changed a locked version"
  exit 1
fi

"$S" update up
grep -q "$U2" smelt.lock || {
  echo "FAIL: upstream not updated"
  exit 1
}
grep -q "^registry-commit $R2\$" smelt.lock || {
  echo "FAIL: registry not moved"
  exit 1
}
"$S" build >/dev/null
[ "$("$S" run | tail -n 1)" = "5" ] || {
  echo "FAIL: run after update"
  exit 1
}

NEW=$(cat smelt.lock)
"$S" fetch
[ "$(cat smelt.lock)" = "$NEW" ] || {
  echo "FAIL: fetch changed updated lock"
  exit 1
}

echo "update tests passed"
