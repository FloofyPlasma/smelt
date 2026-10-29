#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/pc" "$TMP/p/src/app"
cat > "$TMP/pc/demo.pc" <<'EOF'
Name: demo
Description: test package
Version: 1.2.3
Cflags: -DDEMO=1
Libs: -lm
EOF
export PKG_CONFIG_PATH="$TMP/pc"

cat > "$TMP/p/project.smelt" <<'EOF'
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

cd "$TMP/p"
run_smelt tree
assert_eq "$RC" 0 "tree with provider"
assert_contains "$(cat "$TMP/out")" "demo  pkg-config demo (1.2.3)" "provider shown"

sed -i 's/, pkg_config("demo")//' project.smelt
run_smelt tree
assert_eq "$RC" 0 "tree with missing package"
assert_contains "$(cat "$TMP/out")" "tried smelt-no-such-package" "missing candidates shown"

PATH=/nonexistent run_smelt tree
assert_eq "$RC" 0 "tree without pkg-config"
assert_contains "$(cat "$TMP/out")" "pkg-config is not installed" "missing tool shown"

echo "ok"