#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-${TEXTLT_VERSION:-}}"
if [[ -z "$VERSION" ]]; then
  if command -v curl >/dev/null 2>&1; then
    VERSION="$(curl -fsSL https://api.github.com/repos/ihor-liutak2/textlt/releases/latest | grep '"tag_name"' | head -1 | sed -E 's/.*"tag_name":\s*"([^"]+)".*/\1/')"
  elif command -v wget >/dev/null 2>&1; then
    VERSION="$(wget -qO- https://api.github.com/repos/ihor-liutak2/textlt/releases/latest | grep '"tag_name"' | head -1 | sed -E 's/.*"tag_name":\s*"([^"]+)".*/\1/')"
  fi
  if [[ -z "$VERSION" ]]; then
    echo "Could not detect latest version. Pass it as argument: $0 vX.Y.Z" >&2
    exit 1
  fi
  echo "Auto-detected latest version: $VERSION"
fi
if [[ "$VERSION" != v* ]]; then
  VERSION="v$VERSION"
fi

REPO="${TEXTLT_REPO:-ihor-liutak2/textlt}"
ASSET="${TEXTLT_ASSET:-textlt-linux-x64.tar.gz}"
INSTALL_ROOT="${TEXTLT_INSTALL_ROOT:-$HOME/.local/share/textlt}"
APP_DIR="$INSTALL_ROOT/app"
BIN_DIR="${TEXTLT_BIN_DIR:-$HOME/.local/bin}"
ARCHIVE="${TMPDIR:-/tmp}/$ASSET"
EXTRACT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/textlt-install.XXXXXX")"
URL="${TEXTLT_DOWNLOAD_URL:-https://github.com/$REPO/releases/download/$VERSION/$ASSET}"

cleanup() {
  rm -rf "$EXTRACT_DIR"
}
trap cleanup EXIT

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Required tool is missing: $1" >&2
    exit 1
  fi
}

require_tool tar
mkdir -p "$INSTALL_ROOT" "$APP_DIR" "$BIN_DIR"

echo "Downloading TextLT $VERSION for Linux..."
if command -v curl >/dev/null 2>&1; then
  curl -fL "$URL" -o "$ARCHIVE"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$ARCHIVE" "$URL"
else
  echo "Install curl or wget first." >&2
  exit 1
fi

echo "Extracting archive..."
tar -xzf "$ARCHIVE" -C "$EXTRACT_DIR"

EXE="$(find "$EXTRACT_DIR" -type f -name textlt -perm -111 | head -n 1)"
if [[ -z "$EXE" ]]; then
  EXE="$(find "$EXTRACT_DIR" -type f -name textlt | head -n 1)"
fi
if [[ -z "$EXE" ]]; then
  echo "textlt executable was not found in the extracted archive." >&2
  exit 1
fi

EXE_DIR="$(dirname "$EXE")"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"
cp -a "$EXE_DIR"/. "$APP_DIR"/
chmod +x "$APP_DIR/textlt"

cat > "$BIN_DIR/textlt" <<TEXTLT_WRAPPER
#!/usr/bin/env bash
cd "$APP_DIR" || exit 1
exec "$APP_DIR/textlt" "\$@"
TEXTLT_WRAPPER
chmod +x "$BIN_DIR/textlt"

BASHRC="$HOME/.bashrc"
touch "$BASHRC"
PATH_LINE="export PATH=\"$BIN_DIR:\$PATH\""
grep -qxF "$PATH_LINE" "$BASHRC" || echo "$PATH_LINE" >> "$BASHRC"
grep -qxF 'export COLORTERM=truecolor' "$BASHRC" || echo 'export COLORTERM=truecolor' >> "$BASHRC"
grep -qxF 'export TERM=xterm-256color' "$BASHRC" || echo 'export TERM=xterm-256color' >> "$BASHRC"

export PATH="$BIN_DIR:$PATH"
export COLORTERM=truecolor
export TERM=xterm-256color

echo ""
echo "TextLT $VERSION installed to: $APP_DIR"
echo "Run: textlt"
textlt --help || true
