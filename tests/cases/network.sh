#!/bin/sh
. "$(dirname "$0")/../lib.sh"

if [ "${SMELT_NETWORK_TESTS:-0}" != "1" ]; then
    echo "skipped (set SMELT_NETWORK_TESTS=1)"
    exit 0
fi
make_tmp

mkdir -p "$TMP/x/src/app"
cat >"$TMP/x/project.smelt" <<'EOF'
project "x" {
    version = "0.1.0";
}

dependency "xxhash" {
    registry = "xxhash";
    version = "0.8.4";
    target = "xxhash";
}

target "app" {
    type = exe;
    depends = ["xxhash"];
}
EOF
cat >"$TMP/x/src/app/main.cpp" <<'EOF'
#include <cstdio>
#include "xxhash.h"
int main(int argc, char **argv) {
  std::FILE *f = std::fopen(argv[1], "w");
  if (f == nullptr)
    return 1;
  std::fprintf(f, "%016llx\n", (unsigned long long)XXH64("", 0, 0));
  std::fclose(f);
  return 0;
}
EOF

cd "$TMP/x"
run_smelt fetch
assert_eq "$RC" 0 "fetch"
run_smelt build
assert_eq "$RC" 0 "build"
set +e
"$SMELT" run -- "$TMP/hash.txt" >/dev/null 2>&1
RC=$?
set -e
assert_eq "$RC" 0 "run"
assert_eq "$(cat "$TMP/hash.txt")" "ef46db3751d8e999" "xxhash hash"
