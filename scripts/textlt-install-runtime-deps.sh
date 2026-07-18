#!/usr/bin/env sh
set -eu

missing=""
for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing="$missing $tool"
  fi
done

if [ -z "$missing" ]; then
  echo "TextLT runtime dependencies are already installed: curl ssh sftp"
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
  install_with apt-get apt-get install -y curl openssh-client
elif command -v dnf >/dev/null 2>&1; then
  install_with dnf dnf install -y curl openssh-clients
elif command -v zypper >/dev/null 2>&1; then
  install_with zypper zypper --non-interactive install curl openssh
elif command -v pacman >/dev/null 2>&1; then
  install_with pacman pacman -Sy --needed --noconfirm curl openssh
elif command -v apk >/dev/null 2>&1; then
  install_with apk apk add --no-cache curl openssh-client
elif command -v brew >/dev/null 2>&1; then
  install_with brew brew install curl openssh
else
  cat >&2 <<'MSG'
Cannot detect a supported package manager.
Install these runtime tools manually:
  curl
  ssh
  sftp

Debian/Ubuntu/MX:
  sudo apt install curl openssh-client
Fedora:
  sudo dnf install curl openssh-clients
Arch:
  sudo pacman -S curl openssh
Alpine:
  sudo apk add curl openssh-client
MSG
  exit 1
fi

for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Runtime dependency still missing after install attempt: $tool" >&2
    exit 1
  fi
done

echo "TextLT runtime dependencies installed: curl ssh sftp"
