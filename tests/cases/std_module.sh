#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/bad" && cd "$TMP/bad"
cat >project.smelt <<'EOF'
project "bad" {
    version = "0.1.0";
    cxx_std = 20;
    std_module = true;
}

target "app" {
    type = exe;
}
EOF
run_smelt check
assert_eq "$RC" 1 "std_module below C++23 is rejected"
assert_contains "$(cat "$TMP/out")" "needs cxx_std = 23" "std_module message"

mkdir -p "$TMP/good/src/app" && cd "$TMP/good"
cat >project.smelt <<'EOF'
project "good" {
    version = "0.1.0";
    cxx_std = 23;
    std_module = true;
}

target "app" {
    type = exe;
}
EOF
cat >src/app/main.cpp <<'EOF'
import std;

int main() {
  std::println("hello, world!");
}
EOF

run_smelt build
if [ "$RC" -ne 0 ] && grep -q 'libc++ std module not found' "$TMP/out"; then
  echo "skipped (no libc++ std module on this machine)"
  exit 0
fi
assert_eq "$RC" 0 "build with import std"

"$TMP/good/build/debug/app/app" >"$TMP/prog.txt"
assert_eq "$(cat "$TMP/prog.txt")" "hello, world!" "import std program output"

run_smelt build
assert_eq "$RC" 0 "cached build"
assert_not_contains "$(cat "$TMP/out")" "std: precompile" "precompile re-ran"

echo "ok"
