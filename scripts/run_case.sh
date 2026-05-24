#!/usr/bin/env bash
set -uo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_case.sh <source.sy> [input.txt]

Builds the compiler, emits Koopa/RISC-V files, links with libsysy,
then runs the result with qemu-riscv32-static.

Environment:
  SYSY_BUILD_DIR  Build directory, default: <repo>/build
  SYSY_RUN_DIR    Temporary output directory, default: /tmp/sysy-run
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage >&2
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${SYSY_BUILD_DIR:-$ROOT/build}"
RUN_DIR="${SYSY_RUN_DIR:-/tmp/sysy-run}"
SRC="$1"
INPUT="${2:-}"

if [[ ! -f "$SRC" ]]; then
  echo "source file not found: $SRC" >&2
  exit 1
fi

if [[ -z "$INPUT" && -f "${SRC%.*}.in" ]]; then
  INPUT="${SRC%.*}.in"
fi

if [[ -n "$INPUT" && ! -f "$INPUT" ]]; then
  echo "input file not found: $INPUT" >&2
  exit 1
fi

ABS_SRC="$(realpath "$SRC")"
ABS_INPUT=""
if [[ -n "$INPUT" ]]; then
  ABS_INPUT="$(realpath "$INPUT")"
fi

mkdir -p "$RUN_DIR"

BASE="$(basename "$ABS_SRC" .sy)"
SAFE_BASE="${BASE//[^A-Za-z0-9_.-]/_}"
KOOPA="$RUN_DIR/$SAFE_BASE.koopa"
ASM="$RUN_DIR/$SAFE_BASE.S"
OBJ="$RUN_DIR/$SAFE_BASE.o"
EXE="$RUN_DIR/$SAFE_BASE"

jobs_count="$(nproc 2>/dev/null || echo 2)"

echo "== build compiler =="
if ! cmake -S "$ROOT" -B "$BUILD"; then
  exit 1
fi
if ! cmake --build "$BUILD" -j"$jobs_count"; then
  exit 1
fi

COMPILER="$BUILD/compiler"
if [[ ! -x "$COMPILER" ]]; then
  echo "compiler not found after build: $COMPILER" >&2
  exit 1
fi

echo
echo "== compile SysY =="
echo "source: $ABS_SRC"
if ! "$COMPILER" -koopa "$ABS_SRC" -o "$KOOPA"; then
  echo "compile failed while generating Koopa IR" >&2
  exit 1
fi
if ! "$COMPILER" -riscv "$ABS_SRC" -o "$ASM"; then
  echo "compile failed while generating RISC-V assembly" >&2
  exit 1
fi
echo "koopa:  $KOOPA"
echo "riscv:  $ASM"

LIBSYSY_DIR="${CDE_LIBRARY_PATH:-/opt/lib}/riscv32"
if [[ ! -d "$LIBSYSY_DIR" ]]; then
  echo "libsysy directory not found: $LIBSYSY_DIR" >&2
  echo "Run this script inside maxxing/compiler-dev." >&2
  exit 1
fi

echo
echo "== link =="
if ! clang "$ASM" -c -o "$OBJ" -target riscv32-unknown-linux-elf -march=rv32imf -mabi=ilp32; then
  exit 1
fi
if ! ld.lld "$OBJ" -L"$LIBSYSY_DIR" -lsysy -o "$EXE"; then
  exit 1
fi
echo "binary: $EXE"

echo
echo "== program output =="
if [[ -n "$ABS_INPUT" ]]; then
  echo "input: $ABS_INPUT"
  qemu-riscv32-static "$EXE" < "$ABS_INPUT"
else
  qemu-riscv32-static "$EXE"
fi
PROGRAM_STATUS=$?

echo
echo "== SysY return code: $PROGRAM_STATUS =="
exit 0
