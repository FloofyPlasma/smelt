#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

if ! command -v clang-scan-deps >/dev/null 2>&1; then
  echo "skipped (clang-scan-deps not found)"
  exit 0
fi

mkdir -p "$TMP/m/src/app" && cd "$TMP/m"
cat >project.smelt <<'EOF'
project "m" {
    version = "0.1.0";
    cxx_std = 23;
}

target "app" {
    type = exe;
}
EOF
cat >src/app/math.cppm <<'EOF'
export module math;

export int add(int a, int b);
export int twice(int x);
EOF
cat >src/app/math_impl.cpp <<'EOF'
module math;

int add(int a, int b) { return a + b; }
int twice(int x) { return 2 * x; }
EOF
cat >src/app/main.cpp <<'EOF'
import math;

int main() {
    return add(twice(1), 0) == 2 ? 0 : 1;
}
EOF

mtime() { stat -c %Y "$1"; }
MAIN_OBJ=build/debug/app/obj/src/app/main.cpp.o

run_smelt build
assert_eq "$RC" 0 "module build"

set +e
"$TMP/m/build/debug/app/app"
RC=$?
set -e
assert_eq "$RC" 0 "program result"

main1=$(mtime "$MAIN_OBJ")
sleep 1
run_smelt build
assert_eq "$RC" 0 "cached build"
assert_eq "$(mtime "$MAIN_OBJ")" "$main1" "cached build rebuilt the importer"

sleep 1
cat >src/app/math_impl.cpp <<'EOF'
module math;

int add(int a, int b) { return a + b; }
int twice(int x) { return 2 * x; }  // edited
EOF
run_smelt build
assert_eq "$RC" 0 "implementation change builds"
assert_eq "$(mtime "$MAIN_OBJ")" "$main1" "implementation change rebuilt the importer"

sleep 1
cat >src/app/math.cppm <<'EOF'
export module math;

export int add(int a, int b);
export int twice(int x);
export int thrice(int x);
EOF
run_smelt build
assert_eq "$RC" 0 "interface change builds"
[ "$(mtime "$MAIN_OBJ")" -gt "$main1" ] || fail "interface change did not rebuild the importer"

echo "ok"
