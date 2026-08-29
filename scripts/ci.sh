#!/usr/bin/env bash
# Configure, build, and test with one compiler. Runs INSIDE the nids-dev
# container. Usage: scripts/ci.sh [gcc|clang] [extra cmake args...]
set -euo pipefail

CC_ID="${1:-gcc}"
shift || true

case "$CC_ID" in
  gcc)   export CC=gcc-13   CXX=g++-13 ;;
  clang) export CC=clang-17 CXX=clang++-17 ;;
  *) echo "unknown compiler: $CC_ID" >&2; exit 2 ;;
esac

BUILD="build/${CC_ID}"
echo "== configure ($CC_ID) =="
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
echo "== build ($CC_ID) =="
cmake --build "$BUILD" -j"$(nproc)"
echo "== test ($CC_ID) =="
ctest --test-dir "$BUILD" --output-on-failure
