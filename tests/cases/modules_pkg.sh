#!/bin/sh
. "$(dirname "$0")/../lib.sh"
make_tmp

# Works just fine on a fresh ubuntu podman container, and on multiple machines, yet doesnt work in github workflows...
if [ "${SMELT_MODULE_TESTS:-0}" != "1" ]; then
  echo "skipped (set SMELT_MODULE_TESTS=1)"
  exit 0
fi

if ! command -v clang-scan-deps >/dev/null 2>&1; then
  echo "skipped (clang-scan-deps not found)"
  exit 0
fi

write_spike() {
  d=$1
  mkdir -p "$d/src/app" "$d/lib/src/core" "$d/util/src/util"

  cat >"$d/project.smelt" <<'EOF'
project "modspike" {
    version = "0.1.0";
    cxx_std = 23;
    std_module = true;
}

dependency "lib" {
    path = "lib";
}

target "app" {
    type = exe;
    depends = ["lib.core"];
}
EOF
  cat >"$d/src/app/main.cpp" <<'EOF'
import core;

int main() {
    return twice_plus(3) == 7 ? 0 : 1;
}
EOF
  cat >"$d/lib/project.smelt" <<'EOF'
project "lib" {
    version = "1.0.0";
}

dependency "util" {
    path = "../util";
}

target "core" {
    type = static_lib;
    export = true;
    depends = ["util.util"];
}
EOF
  cat >"$d/lib/src/core/core.cppm" <<'EOF'
export module core;
import util;

export int twice_plus(int x) {
    return util_twice(x) + 1;
}
EOF
  cat >"$d/lib/src/core/core_impl.cpp" <<'EOF'
module core;
EOF
  cat >"$d/util/project.smelt" <<'EOF'
project "util" {
    version = "1.0.0";
}

target "util" {
    type = static_lib;
    export = true;
}
EOF
  cat >"$d/util/src/util/util.cppm" <<'EOF'
export module util;

export int util_twice(int x) {
    return 2 * x;
}
EOF
}

mtime() { stat -c %Y "$1"; }

write_spike "$TMP/ok"
cd "$TMP/ok"
run_smelt build
assert_eq "$RC" 0 "transitive build"

set +e
"$TMP/ok/build/debug/app/app"
RC=$?
set -e
assert_eq "$RC" 0 "transitive program result"

write_spike "$TMP/vis"
cat >"$TMP/vis/project.smelt" <<'EOF'
project "modspike" {
    version = "0.1.0";
    cxx_std = 23;
    std_module = true;
}

dependency "lib" {
    path = "lib";
}

dependency "other" {
    path = "other";
}

target "app" {
    type = exe;
    depends = ["lib.core"];
}

target "tool" {
    type = exe;
    depends = ["other.other"];
}
EOF
mkdir -p "$TMP/vis/src/tool" "$TMP/vis/other/src/other"
cat >"$TMP/vis/src/tool/main.cpp" <<'EOF'
int main() { return 0; }
EOF
cat >"$TMP/vis/other/project.smelt" <<'EOF'
project "other" {
    version = "1.0.0";
}

target "other" {
    type = static_lib;
    export = true;
}
EOF
cat >"$TMP/vis/other/src/other/other.cppm" <<'EOF'
export module other;

export int other_one() {
    return 1;
}
EOF
cat >"$TMP/vis/src/app/main.cpp" <<'EOF'
import other;

int main() {
    return other_one() == 1 ? 0 : 1;
}
EOF
cd "$TMP/vis"
run_smelt build
assert_eq "$RC" 1 "import from a non-dependency is rejected"
assert_contains "$(cat "$TMP/out")" "does not depend on" "visibility message"

write_spike "$TMP/exp"
cat >"$TMP/exp/util/project.smelt" <<'EOF'
project "util" {
    version = "1.0.0";
}

target "util" {
    type = static_lib;
    export = true;
}

target "internal" {
    type = static_lib;
}
EOF
mkdir -p "$TMP/exp/util/src/internal"
cat >"$TMP/exp/util/src/internal/secret.cppm" <<'EOF'
export module secret;

export int secret_value() {
    return 42;
}
EOF
cat >"$TMP/exp/project.smelt" <<'EOF'
project "modspike" {
    version = "0.1.0";
    cxx_std = 23;
    std_module = true;
}

dependency "lib" {
    path = "lib";
}

dependency "util" {
    path = "util";
}

target "app" {
    type = exe;
    depends = ["lib.core"];
}

target "reach" {
    type = exe;
    depends = ["util.internal"];
}
EOF
mkdir -p "$TMP/exp/src/reach" "$TMP/exp/src/app"
echo 'int main() { return 0; }' >"$TMP/exp/src/reach/main.cpp"
cat >"$TMP/exp/src/app/main.cpp" <<'EOF'
import secret;

int main() {
    return secret_value() == 42 ? 0 : 1;
}
EOF
cd "$TMP/exp"
run_smelt build
assert_eq "$RC" 1 "unexported module is rejected"
assert_contains "$(cat "$TMP/out")" "is not exported by project 'util'" "export message"

write_spike "$TMP/rb"
cd "$TMP/rb"
run_smelt build
assert_eq "$RC" 0 "rebuild baseline"

APP_OBJ=build/debug/app/obj/src/app/main.cpp.o
CORE_PCM=build/debug/deps/lib/core/modules/core.pcm
app1=$(mtime "$APP_OBJ")
core1=$(mtime "$CORE_PCM")

sleep 1
cat >"$TMP/rb/util/src/util/util_extra.cpp" <<'EOF'
module util;

int util_unused_helper() { return 0; }
EOF
run_smelt build
assert_eq "$RC" 0 "implementation change builds"
assert_eq "$(mtime "$APP_OBJ")" "$app1" "implementation change rebuilt app"
assert_eq "$(mtime "$CORE_PCM")" "$core1" "implementation change rebuilt core's precompile"

sleep 1
cat >"$TMP/rb/util/src/util/util.cppm" <<'EOF'
export module util;

export int util_twice(int x) {
    return 2 * x;
}

export int util_thrice(int x) {
    return 3 * x;
}
EOF
run_smelt build
assert_eq "$RC" 0 "interface change builds"
[ "$(mtime "$CORE_PCM")" -gt "$core1" ] || fail "interface change did not rebuild core's precompile"
[ "$(mtime "$APP_OBJ")" -gt "$app1" ] || fail "interface change did not rebuild app"

echo "ok"
