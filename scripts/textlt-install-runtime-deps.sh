#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BINARY="${TEXTLT_BINARY:-$SELF_DIR/textlt-bin}"

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

if [ -z "$missing" ]; then
  echo "TextLT runtime dependencies are already installed: curl ssh sftp libarchive"
  exit 0
fi

echo "TextLT missing runtime dependencies:$missing"

run_as_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "sudo is not available. Install these tools manually:$missing" >&2
    exit 1
  fi
}

install_with() {
  manager="$1"
  shift
  echo "Installing TextLT runtime dependencies with $manager..."
  run_as_root "$@"
}

if command -v apt-get >/dev/null 2>&1; then
  run_as_root apt-get update
  install_with apt-get apt-get install -y curl openssh-client libarchive-dev
elif command -v dnf >/dev/null 2>&1; then
  install_with dnf dnf install -y curl openssh-clients libarchive
elif command -v zypper >/dev/null 2>&1; then
  install_with zypper zypper --non-interactive install curl openssh libarchive13
elif command -v pacman >/dev/null 2>&1; then
  install_with pacman pacman -Sy --needed --noconfirm curl openssh libarchive
elif command -v apk >/dev/null 2>&1; then
  install_with apk apk add --no-cache curl openssh-client libarchive
elif command -v brew >/dev/null 2>&1; then
  install_with brew brew install curl openssh libarchive
else
  cat >&2 <<'MSG'
Cannot detect a supported package manager.
Install these runtime tools manually:
  curl
  ssh
  sftp
  libarchive

Debian/Ubuntu/MX:
  sudo apt install curl openssh-client libarchive-dev
Fedora:
  sudo dnf install curl openssh-clients libarchive
Arch:
  sudo pacman -S curl openssh libarchive
Alpine:
  sudo apk add curl openssh-client libarchive
MSG
  exit 1
fi

for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Runtime dependency still missing after install attempt: $tool" >&2
    exit 1
  fi
done
if libarchive_is_missing; then
  echo "Runtime dependency still missing after install attempt: libarchive" >&2
  exit 1
fi

echo "TextLT runtime dependencies installed: curl ssh sftp libarchive"
