#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

mkdir -p "$TMP/c"
cd "$TMP/c"
cat >project.smelt <<'EOF'
project "c" {
    version = "0.1.0";
}

dependency "gone" {
    git = "https://example.com/gone.git";
    tag = "v1";
}

dependency "sys" {
    find = pkg_config("smelt-no-such-package");
}

target "app" {
    type = exe;
    depends = ["gone.core"];
}
EOF

PATH=/nonexistent run_smelt check
assert_eq "$RC" 0 "check passes without tools or dependencies"
assert_contains "$(cat "$TMP/out")" "ok: project 'c'" "check output"
assert_no_path build "check wrote build/"
assert_no_path smelt.lock "check wrote the lock"
assert_no_path .smelt "check fetched dependencies"

cat >project.smelt <<'EOF'
project "c" {
    version = "0.1.0";
    color = "red";
}
EOF
run_smelt check
assert_eq "$RC" 1 "check rejects an unknown setting"
assert_contains "$(cat "$TMP/out")" "unknown setting 'color'" "check error"

mkdir -p "$TMP/empty" && cd "$TMP/empty"
run_smelt check
assert_eq "$RC" 1 "check without a project file"

echo "ok"
