#!/usr/bin/env bash
set -euo pipefail

VERSION="${TEXTLT_VERSION:-${1:-v0.9.1}}"
REPO="${TEXTLT_REPO:-ihor-liutak2/textlt}"
ARCH="$(uname -m)"
ASSET="${TEXTLT_ASSET:-textlt-macos-$ARCH.tar.gz}"
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

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required on macOS." >&2
  exit 1
fi
if ! command -v tar >/dev/null 2>&1; then
  echo "tar is required on macOS." >&2
  exit 1
fi

mkdir -p "$INSTALL_ROOT" "$APP_DIR" "$BIN_DIR"

echo "Downloading TextLT $VERSION for macOS ($ARCH)..."
curl -fL "$URL" -o "$ARCHIVE"

echo "Extracting archive..."
tar -xzf "$ARCHIVE" -C "$EXTRACT_DIR"

EXE="$(find "$EXTRACT_DIR" -type f -name textlt -perm -111 | head -n 1)"
if [[ -z "$EXE" ]]; then
  echo "textlt executable was not found in the extracted archive." >&2
  echo "If this release has no macOS archive yet, build TextLT from source." >&2
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

ZSHRC="$HOME/.zshrc"
touch "$ZSHRC"
grep -qxF 'export PATH="$HOME/.local/bin:$PATH"' "$ZSHRC" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$ZSHRC"

export PATH="$HOME/.local/bin:$PATH"

echo "TextLT installed to: $APP_DIR"
echo "Run: textlt"
textlt --help || true
