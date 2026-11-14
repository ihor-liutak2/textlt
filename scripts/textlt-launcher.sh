#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALLER="$SELF_DIR/textlt-install-runtime-deps.sh"
BINARY="$SELF_DIR/textlt-bin"
LIB_DIR="$SELF_DIR/lib"

# Release tarballs carry the exact libarchive runtime used by the build. Keep
# any caller-provided library path after it so explicit overrides still work.
if [ -d "$LIB_DIR" ]; then
  LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LD_LIBRARY_PATH
fi

libarchive_is_missing() {
  [ -x "$BINARY" ] || return 1
  command -v ldd >/dev/null 2>&1 || return 1
  ldd "$BINARY" 2>/dev/null |
    grep -Eq 'libarchive[^[:space:]]*[[:space:]]+=>[[:space:]]+not found'
}

missing=""
for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing="$missing $tool"
  fi
done
if libarchive_is_missing; then
  missing="$missing libarchive"
fi

if [ "${TEXTLT_CHECK_RUNTIME_ONLY:-}" = "1" ]; then
  if [ -z "$missing" ]; then
    echo "TextLT runtime dependencies are available: curl ssh sftp libarchive"
    exit 0
  fi
  if [ -x "$INSTALLER" ]; then
    exec "$INSTALLER"
  fi
  echo "TextLT missing runtime dependencies:$missing" >&2
  exit 127
fi

if [ -n "$missing" ]; then
  if [ -x "$INSTALLER" ]; then
    "$INSTALLER"
  else
    echo "TextLT missing runtime dependencies:$missing" >&2
    echo "Installer script was not found: $INSTALLER" >&2
    echo "Install manually: curl, openssh-client, and libarchive" >&2
    exit 127
  fi
fi

if [ ! -x "$BINARY" ]; then
  echo "TextLT launcher cannot find executable: $BINARY" >&2
  exit 127
fi

exec "$BINARY" "$@"
