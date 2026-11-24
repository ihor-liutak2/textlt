#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BINARY="${TEXTLT_BINARY:-$SELF_DIR/textlt-bin}"

shared_library_is_missing() {
  library_pattern="$1"
  [ -x "$BINARY" ] || return 1
  command -v ldd >/dev/null 2>&1 || return 1
  ldd "$BINARY" 2>/dev/null |
    grep -Eq "${library_pattern}[^[:space:]]*[[:space:]]+=>[[:space:]]+not found"
}

missing=""
for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing="$missing $tool"
  fi
done
if shared_library_is_missing 'libarchive'; then
  missing="$missing libarchive"
fi
if shared_library_is_missing 'libxml2'; then
  missing="$missing libxml2"
fi
if shared_library_is_missing '(libssl|libcrypto)\.so\.3'; then
  missing="$missing openssl"
fi

if [ -z "$missing" ]; then
  echo "TextLT runtime dependencies are already installed: curl ssh sftp libarchive libxml2 OpenSSL"
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
  run_as_root apt-get update || true
  archive_package=""
  xml_package=""
  ssl_package=""
  distro_id="$(sed -n 's/^ID=//p' /etc/os-release 2>/dev/null | tr -d '"' | head -n 1)"
  distro_version="$(sed -n 's/^VERSION_ID=//p' /etc/os-release 2>/dev/null | tr -d '"' | head -n 1)"
  case "$distro_id:$distro_version" in
    ubuntu:22.04)
      archive_package="libarchive13"
      xml_package="libxml2"
      ;;
    ubuntu:24.04)
      archive_package="libarchive13t64"
      xml_package="libxml2"
      ;;
    ubuntu:26.04)
      archive_package="libarchive13t64"
      xml_package="libxml2-16"
      ;;
  esac
  if [ -z "$archive_package" ]; then
    if apt-cache show libarchive13t64 >/dev/null 2>&1; then
      archive_package="libarchive13t64"
    elif apt-cache show libarchive13 >/dev/null 2>&1; then
      archive_package="libarchive13"
    fi
  fi
  if [ -z "$xml_package" ]; then
    if apt-cache show libxml2-16 >/dev/null 2>&1; then
      xml_package="libxml2-16"
    elif apt-cache show libxml2 >/dev/null 2>&1; then
      xml_package="libxml2"
    fi
  fi
  if [ -z "$archive_package" ] || [ -z "$xml_package" ]; then
    echo "Could not find compatible libarchive and libxml2 runtime packages in the apt cache." >&2
    echo "Check WSL networking, run 'sudo apt-get update', and try again." >&2
    exit 1
  fi
  case "$missing " in
    *" openssl "*)
      if apt-cache show libssl3t64 >/dev/null 2>&1; then
        ssl_package=libssl3t64
      elif apt-cache show libssl3 >/dev/null 2>&1; then
        ssl_package=libssl3
      else
        echo "Could not find the OpenSSL 3 runtime package in the apt cache." >&2
        exit 1
      fi
      ;;
  esac
  if [ "$distro_id" = "ubuntu" ]; then
    echo "Installing TextLT runtime dependencies for Ubuntu $distro_version with apt-get..."
  else
    echo "Installing TextLT runtime dependencies with apt-get..."
  fi
  if ! run_as_root apt-get install -y --no-install-recommends \
      curl openssh-client "$archive_package" "$xml_package" ${ssl_package:+"$ssl_package"}; then
    echo "apt-get could not download the current runtime packages." >&2
    echo "Check WSL DNS, run 'sudo apt-get update --fix-missing', and try again." >&2
    exit 1
  fi
elif command -v dnf >/dev/null 2>&1; then
  install_with dnf dnf install -y curl openssh-clients libarchive libxml2 'libssl.so.3()(64bit)'
elif command -v zypper >/dev/null 2>&1; then
  install_with zypper zypper --non-interactive install curl openssh libarchive13 libxml2-2 libopenssl3
elif command -v pacman >/dev/null 2>&1; then
  ssl_package=openssl
  if pacman -Si openssl-3 >/dev/null 2>&1; then
    ssl_package=openssl-3
  fi
  install_with pacman pacman -Sy --needed --noconfirm curl openssh libarchive libxml2 "$ssl_package"
elif command -v apk >/dev/null 2>&1; then
  install_with apk apk add --no-cache curl openssh-client libarchive libxml2 libssl3 libcrypto3
elif command -v brew >/dev/null 2>&1; then
  install_with brew brew install curl openssh libarchive libxml2 openssl@3
else
  cat >&2 <<'MSG'
Cannot detect a supported package manager.
Install these runtime tools manually:
  curl
  ssh
  sftp
  libarchive
  libxml2
  OpenSSL 3

Debian/Ubuntu/MX:
  sudo apt install curl openssh-client libarchive13t64 libxml2 libssl3t64
  (use libarchive13 and libssl3 on older releases)
Ubuntu 26.04:
  sudo apt install curl openssh-client libarchive13t64 libxml2-16 libssl3t64
Fedora:
  sudo dnf install curl openssh-clients libarchive libxml2 'libssl.so.3()(64bit)'
Arch:
  sudo pacman -S curl openssh libarchive libxml2 openssl-3
Alpine:
  sudo apk add curl openssh-client libarchive libxml2 libssl3 libcrypto3
MSG
  exit 1
fi

for tool in curl ssh sftp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Runtime dependency still missing after install attempt: $tool" >&2
    exit 1
  fi
done
if shared_library_is_missing 'libarchive'; then
  echo "Runtime dependency still missing after install attempt: libarchive" >&2
  exit 1
fi
if shared_library_is_missing 'libxml2'; then
  echo "Runtime dependency still missing after install attempt: libxml2" >&2
  exit 1
fi
if shared_library_is_missing '(libssl|libcrypto)\.so\.3'; then
  echo "Runtime dependency still missing after install attempt: OpenSSL 3" >&2
  exit 1
fi

echo "TextLT runtime dependencies installed: curl ssh sftp libarchive libxml2 OpenSSL"
