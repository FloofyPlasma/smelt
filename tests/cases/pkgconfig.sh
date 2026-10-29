#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/pc" "$TMP/p/src/app"
cat >"$TMP/pc/demo.pc" <<'EOF'
Name: demo
Description: test package
Version: 1.2.3
Cflags: -isystem /opt/demo/include -DDEMO=1
Libs: -lm
EOF
export PKG_CONFIG_PATH="$TMP/pc"

cat >"$TMP/p/project.smelt" <<'EOF'
project "p" {
    version = "0.1.0";
}

dependency "demo" {
    find = first(pkg_config("smelt-no-such-package"), pkg_config("demo"));
}

target "app" {
    type = exe;
    depends = ["demo"];
}
EOF
echo 'int main() { return 0; }' >"$TMP/p/src/app/main.cpp"

cd "$TMP/p"
run_smelt build
assert_eq "$RC" 0 "build with fallback"
assert_contains "$(cat "$TMP/out")" "demo: pkg-config demo (1.2.3)" "provider line"

run_smelt compdb
assert_eq "$RC" 0 "compdb"
grep -q -- '-isystem' compile_commands.json || fail "system include not passed as -isystem"
grep -q -- '-DDEMO=1' compile_commands.json || fail "pkg-config define missing"

cat >"$TMP/p/project.smelt" <<'EOF'
project "p" {
    version = "0.1.0";
}

dependency "demo" {
    find = pkg_config("smelt-no-such-package");
}

target "app" {
    type = exe;
    depends = ["demo"];
}
EOF
run_smelt build
[ "$RC" -ne 0 ] || fail "missing package accepted"
assert_contains "$(cat "$TMP/out")" "tried smelt-no-such-package" "missing package message"

echo "ok"
