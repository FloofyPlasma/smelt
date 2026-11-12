#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

# smelt.lock is present (copied from a clone where it was fetched) but the
# .smelt directory is not. The fix is 'smelt fetch', not 'smelt update'.
mkdir -p "$TMP/x"
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
cat >"$TMP/x/smelt.lock" <<'EOF'
# smelt.lock; Generated file. Please do not edit by hand.
registry-commit 1111111111111111111111111111111111111111
registry xxhash https://example.invalid/registry version 0.8.4 2222222222222222222222222222222222222222
EOF

cd "$TMP/x"
run_smelt build
assert_eq "$RC" 1 "build without .smelt"
assert_contains "$(cat "$TMP/out")" "run 'smelt fetch'" "suggested command"
assert_not_contains "$(cat "$TMP/out")" "smelt update" "suggested command"
