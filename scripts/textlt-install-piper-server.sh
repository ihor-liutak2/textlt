#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
SERVER_BIN="$BUILD_DIR/textlt-piper-server"
TARGET_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/textlt/piper/bin"
TARGET_BIN="$TARGET_DIR/textlt-piper-server"

if [[ ! -x "$SERVER_BIN" ]]; then
  echo "textlt-piper-server was not found at: $SERVER_BIN" >&2
  echo "Build it first: cmake --build $BUILD_DIR --target textlt-piper-server" >&2
  exit 1
fi

mkdir -p "$TARGET_DIR"
TMP_BIN="${TARGET_BIN}.$$.new"
cleanup() {
  rm -f "$TMP_BIN"
}
trap cleanup EXIT
cp "$SERVER_BIN" "$TMP_BIN"
chmod +x "$TMP_BIN"
mv -f "$TMP_BIN" "$TARGET_BIN"
trap - EXIT

echo "Installed: $TARGET_BIN"
