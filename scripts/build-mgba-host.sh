#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/mgba/src"
BUILD="$ROOT/build-mgba-host"
if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  "$ROOT/scripts/fetch-core.sh"
fi
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DLIBMGBA_ONLY=ON \
  -DDISABLE_FRONTENDS=ON \
  -DDISABLE_DEPS=ON \
  -DBUILD_STATIC=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel
