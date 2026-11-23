#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/mgba/src"
TAG="${MGBA_TAG:-0.10.5}"
if [[ -d "$DEST/.git" ]]; then
  git -C "$DEST" fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG"
  git -C "$DEST" checkout --detach "$TAG"
else
  mkdir -p "$ROOT/third_party/mgba"
  git clone --depth 1 --branch "$TAG" https://github.com/mgba-emu/mgba.git "$DEST"
fi
printf 'mGBA source checked out: %s\n' "$TAG"
