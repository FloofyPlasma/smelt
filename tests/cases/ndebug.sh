#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/ndebug_app/src/app"

cat >"$TMP/ndebug_app/project.smelt" <<'EOF'
project "ndebug_app" {
    version = "0.1.0";
}

profile debug {
    optimization = none;
    debug_info = true;
}

profile release {
    optimization = speed;
    debug_info = false;
}

target "app" {
    type = exe;
}
EOF

cat >"$TMP/ndebug_app/src/app/main.cpp" <<'EOF'
#include <cassert>
#include <cstdio>

int main() {
#ifdef NDEBUG
  std::puts("NDEBUG_ON");
#else
  std::puts("NDEBUG_OFF");
#endif
  assert(0);
  return 0;
}
EOF

cd "$TMP/ndebug_app"

# Release (debug_info = false): NDEBUG defined -> assert(0) is skipped, returns 0
run_smelt build --profile release
assert_eq "$RC" 0 "release build with NDEBUG"
OUT=$(./build/release/app/app)
assert_eq "$OUT" "NDEBUG_ON" "NDEBUG defined when debug_info is false"

# Debug (debug_info = true): NDEBUG not defined -> assert(0) aborts (non-zero)
run_smelt build --profile debug
assert_eq "$RC" 0 "debug build without NDEBUG"
set +e
./build/debug/app/app >/dev/null 2>&1
RC=$?
set -e
if [ "$RC" -eq 0 ]; then
  fail "expected assert(0) to abort when debug_info is true"
fi
