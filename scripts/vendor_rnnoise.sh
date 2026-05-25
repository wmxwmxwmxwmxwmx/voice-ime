#!/usr/bin/env bash
# 将 simple-rnnoise-wasm 预编译产物复制到 frontend/vendor/rnnoise
set -euo pipefail
VERSION="${1:-1.1.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/frontend/vendor/rnnoise"
mkdir -p "$DEST"
BASE="https://unpkg.com/simple-rnnoise-wasm@${VERSION}/dist"

fetch() {
  local name="$1" out="$2"
  echo "Downloading $out ..."
  curl -fsSL "$BASE/$name" -o "$DEST/$out"
}

fetch "rnnoise.worklet.js" "rnnoise.worklet.js"
fetch "rnnoise.wasm" "simple-rnnoise.wasm"
echo "Done. Files in $DEST"
