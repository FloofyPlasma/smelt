#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX="$HOME/.local"

while [ $# -gt 0 ]; do
  case "$1" in
  --prefix)
    PREFIX=$2
    shift 2
    ;;
  *)
    echo "usage: bootstrap/install.sh [--prefix DIR]" >&2
    exit 2
    ;;
  esac
done

cd "$ROOT"

"$ROOT/bootstrap/stage0.sh"

rm -rf build/debug
build/stage0/smelt build >/dev/null
cp build/debug/smelt/smelt build/stage1

rm -rf build/debug
build/stage1 build >/dev/null
cp build/debug/smelt/smelt build/stage2
if ! cmp -s build/stage1 build/stage2; then
  echo "stage1 and stage2 differ; not installing" >&2
  exit 1
fi

build/stage2 install --prefix "$PREFIX"
