#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALLER="$SELF_DIR/textlt-install-runtime-deps.sh"
BINARY="$SELF_DIR/textlt-bin"

missing=""
for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing="$missing $tool"
  fi
done

if [ "${TEXTLT_CHECK_RUNTIME_ONLY:-}" = "1" ]; then
  if [ -z "$missing" ]; then
    echo "TextLT runtime dependencies are available: curl ssh sftp"
    exit 0
  fi
  if [ -x "$INSTALLER" ]; then
    exec "$INSTALLER"
  fi
  echo "TextLT missing runtime dependencies:$missing" >&2
  exit 127
fi

if [ -n "$missing" ]; then
  echo "TextLT missing runtime dependencies:$missing" >&2
  if [ -x "$INSTALLER" ]; then
    "$INSTALLER"
  else
    echo "Installer script was not found: $INSTALLER" >&2
    echo "Install manually: curl and openssh-client" >&2
    exit 127
  fi
fi

if [ ! -x "$BINARY" ]; then
  echo "TextLT launcher cannot find executable: $BINARY" >&2
  exit 127
fi

exec "$BINARY" "$@"
