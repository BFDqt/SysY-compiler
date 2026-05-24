#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD"
cmake --build "$BUILD" -j

for src in "$ROOT"/tests/positive/*.sy; do
  name="$(basename "$src" .sy)"
  "$BUILD/compiler" -koopa "$src" -o "/tmp/$name.koopa"
  "$BUILD/compiler" -riscv "$src" -o "/tmp/$name.S"
  echo "generated $name"
done
