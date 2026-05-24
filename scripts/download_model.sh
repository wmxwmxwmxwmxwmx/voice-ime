#!/usr/bin/env bash
set -euo pipefail

MODEL="${1:-base}"
OUTPUT_DIR="${2:-models}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST_DIR="$ROOT/$OUTPUT_DIR"
mkdir -p "$DEST_DIR"

case "$MODEL" in
    base)    FILE="ggml-base.bin" ;;
    base.en) FILE="ggml-base.en.bin" ;;
    tiny)    FILE="ggml-tiny.bin" ;;
    small)   FILE="ggml-small.bin" ;;
    *) echo "未知模型：$MODEL（可选：base、base.en、tiny、small）" >&2; exit 1 ;;
esac

URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$FILE"
DEST="$DEST_DIR/$FILE"

if [[ -f "$DEST" ]]; then
    echo "模型已存在：$DEST"
    exit 0
fi

echo "正在下载 $FILE ..."
curl -L "$URL" -o "$DEST"
echo "已保存至 $DEST"
