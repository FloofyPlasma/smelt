#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/lib/include" "$TMP/lib/src/core" "$TMP/app/src/app"

cat >"$TMP/lib/project.smelt" <<'EOF'
project "lib" {
    version = "1.0.0";
}

target "core" {
    type = static_lib;
    export = true;
    public_includes = ["include"];
    files = ["add.c"];
}
EOF
cat >"$TMP/lib/include/lib.h" <<'EOF'
#ifdef __cplusplus
extern "C" {
#endif
int add(int a, int b);
#ifdef __cplusplus
}
#endif
EOF
echo '#include "lib.h"
int add(int a, int b) { return a + b; }' >"$TMP/lib/src/core/add.c"
echo 'int drop(void) { return 0; }' >"$TMP/lib/src/core/drop.c"

cat >"$TMP/app/project.smelt" <<'EOF'
project "app" {
    version = "0.1.0";
}

dependency "lib" {
    path = "../lib";
}

target "app" {
    type = exe;
    depends = ["lib.core"];
}
EOF
cat >"$TMP/app/src/app/main.cpp" <<'EOF'
#include <cstdio>
#include "lib.h"
int main(int argc, char **argv) {
  std::FILE *f = std::fopen(argv[1], "w");
  if (f == nullptr)
    return 1;
  std::fprintf(f, "%d %d\n", add(2, 3), argc - 2);
  std::fclose(f);
  return 7;
}
EOF

cd "$TMP/app"

run_smelt build
assert_eq "$RC" 0 "first build"

run_smelt compdb
assert_eq "$RC" 0 "compdb"
grep -q 'drop.c' compile_commands.json && fail "files allowlist ignored"

set +e
"$SMELT" run -- "$TMP/result.txt" one two >/dev/null 2>&1
RC=$?
set -e
assert_eq "$RC" 7 "run exit code"
assert_eq "$(cat "$TMP/result.txt")" "5 2" "run output"

OBJ=build/debug/app/obj/src/app/main.cpp.o
BEFORE=$(stat -c %Y "$OBJ")
run_smelt build
assert_eq "$RC" 0 "cached build"
assert_eq "$(stat -c %Y "$OBJ")" "$BEFORE" "cached build recompiled main.cpp"
