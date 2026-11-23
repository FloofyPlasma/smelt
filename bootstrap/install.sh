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
build/stage0/smelt build
cp build/debug/smelt/smelt build/stage1

rm -rf build/debug
build/stage1 build
cp build/debug/smelt/smelt build/stage2

if [ "$(uname -s)" = "Darwin" ]; then
  if [ "$(wc -c <build/stage1)" -ne "$(wc -c <build/stage2)" ]; then
    echo "stage1 and stage2 differ in size; not installing" >&2
    exit 1
  fi
else
  if ! cmp -s build/stage1 build/stage2; then
    echo "stage1 and stage2 differ; not installing" >&2
    exit 1
  fi
fi

build/stage2 install --prefix "$PREFIX"
