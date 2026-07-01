#!/usr/bin/env sh
set -eu

need_curl=0
if ! command -v curl >/dev/null 2>&1; then
  need_curl=1
fi

if [ "$need_curl" -eq 0 ]; then
  echo "TextLT runtime dependency check: curl is installed."
  exit 0
fi

run_install() {
  if command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    "$@"
  fi
}

if command -v apt-get >/dev/null 2>&1; then
  echo "TextLT needs the external curl executable. Installing with apt-get..."
  run_install apt-get update
  run_install apt-get install -y curl
elif command -v dnf >/dev/null 2>&1; then
  echo "TextLT needs the external curl executable. Installing with dnf..."
  run_install dnf install -y curl
elif command -v zypper >/dev/null 2>&1; then
  echo "TextLT needs the external curl executable. Installing with zypper..."
  run_install zypper --non-interactive install curl
elif command -v pacman >/dev/null 2>&1; then
  echo "TextLT needs the external curl executable. Installing with pacman..."
  run_install pacman -Sy --needed --noconfirm curl
elif command -v apk >/dev/null 2>&1; then
  echo "TextLT needs the external curl executable. Installing with apk..."
  run_install apk add curl
else
  cat >&2 <<'EOF'
TextLT needs the external curl executable for cloud/HTTP features.
Install it with your system package manager, for example:
  sudo apt install curl
EOF
  exit 127
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "TextLT dependency install finished, but curl is still not available in PATH." >&2
  exit 127
fi

echo "TextLT runtime dependency check: curl is installed."
