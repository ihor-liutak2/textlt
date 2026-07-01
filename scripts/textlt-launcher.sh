#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALLER="$SELF_DIR/textlt-install-runtime-deps.sh"
BINARY="$SELF_DIR/textlt-bin"

if [ "${TEXTLT_CHECK_RUNTIME_ONLY:-}" = "1" ]; then
  if [ -x "$INSTALLER" ]; then
    exec "$INSTALLER"
  fi
  command -v curl >/dev/null 2>&1
  exit $?
fi

if ! command -v curl >/dev/null 2>&1; then
  if [ -x "$INSTALLER" ]; then
    "$INSTALLER"
  else
    echo "TextLT needs the external curl executable. Install it with: sudo apt install curl" >&2
    exit 127
  fi
fi

if [ ! -x "$BINARY" ]; then
  echo "TextLT launcher cannot find executable: $BINARY" >&2
  exit 127
fi

exec "$BINARY" "$@"
