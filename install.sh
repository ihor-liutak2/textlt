#!/usr/bin/env bash

set -Eeuo pipefail

readonly APP_NAME="textlt"
readonly BUILD_DIR="build"
readonly INSTALL_DIR="$HOME/.local/bin"
readonly INSTALL_PATH="$INSTALL_DIR/$APP_NAME"
readonly BASHRC="$HOME/.bashrc"

if [[ -t 1 ]]; then
  readonly C_RESET=$'\033[0m'
  readonly C_INFO=$'\033[1;34m'
  readonly C_SUCCESS=$'\033[1;32m'
  readonly C_WARN=$'\033[1;33m'
  readonly C_ERROR=$'\033[1;31m'
else
  readonly C_RESET=""
  readonly C_INFO=""
  readonly C_SUCCESS=""
  readonly C_WARN=""
  readonly C_ERROR=""
fi

log_info() {
  printf '%s[INFO]%s %s\n' "$C_INFO" "$C_RESET" "$*"
}

log_success() {
  printf '%s[SUCCESS]%s %s\n' "$C_SUCCESS" "$C_RESET" "$*"
}

log_warn() {
  printf '%s[WARN]%s %s\n' "$C_WARN" "$C_RESET" "$*"
}

log_error() {
  printf '%s[ERROR]%s %s\n' "$C_ERROR" "$C_RESET" "$*" >&2
}

die() {
  log_error "$*"
  exit 1
}

confirm() {
  local prompt="$1"
  local answer
  read -r -p "$prompt [y/N] " answer
  [[ "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]]
}

require_project_root() {
  [[ -f "CMakeLists.txt" && -d "src" ]] ||
    die "Run this script from the textlt project root."
}

install_dependencies_if_requested() {
  local missing=("$@")

  log_warn "Missing required build tools: ${missing[*]}"
  if ! command -v apt >/dev/null 2>&1; then
    die "apt was not found. Please install build-essential and cmake manually."
  fi
  if ! command -v sudo >/dev/null 2>&1; then
    die "sudo was not found. Please install dependencies manually: build-essential cmake"
  fi

  if confirm "Install dependencies with sudo apt update && sudo apt install -y build-essential cmake?"; then
    sudo apt update
    sudo apt install -y build-essential cmake
  else
    die "Cannot continue without required build tools."
  fi
}

validate_dependencies() {
  local missing=()
  local tool

  for tool in cmake g++ make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing+=("$tool")
    fi
  done

  if ((${#missing[@]} > 0)); then
    install_dependencies_if_requested "${missing[@]}"
  fi

  for tool in cmake g++ make; do
    command -v "$tool" >/dev/null 2>&1 ||
      die "$tool is still missing after dependency validation."
  done

  log_success "Build dependencies are available."
}

configure_and_build() {
  local jobs
  jobs="$(nproc 2>/dev/null || printf '1')"

  if [[ -d "$BUILD_DIR" ]]; then
    log_info "Removing existing build directory for a clean Release configuration."
    rm -rf "$BUILD_DIR"
  fi

  log_info "Configuring Release build."
  cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

  log_info "Building with $jobs parallel job(s)."
  cmake --build "$BUILD_DIR" --parallel "$jobs"

  [[ -x "$BUILD_DIR/$APP_NAME" ]] ||
    die "Build completed, but $BUILD_DIR/$APP_NAME was not found."

  log_success "Build completed."
}

deploy_binary() {
  log_info "Installing $APP_NAME to $INSTALL_PATH."
  mkdir -p "$INSTALL_DIR"
  cp "$BUILD_DIR/$APP_NAME" "$INSTALL_PATH"
  chmod +x "$INSTALL_PATH"
  log_success "Installed executable at $INSTALL_PATH."
}

ensure_local_bin_on_path() {
  if [[ ":$PATH:" == *":$INSTALL_DIR:"* ]]; then
    log_success "$INSTALL_DIR is already available in PATH."
    return
  fi

  touch "$BASHRC"
  if grep -Fq 'export PATH="$HOME/.local/bin:$PATH"' "$BASHRC"; then
    log_warn "$INSTALL_DIR is already configured in $BASHRC, but not active in this shell."
    log_info "Run: source ~/.bashrc"
    return
  fi

  log_info "Adding $INSTALL_DIR to PATH in $BASHRC."
  {
    printf '\n# Added by textlt installer\n'
    printf 'export PATH="$HOME/.local/bin:$PATH"\n'
  } >> "$BASHRC"

  log_success "PATH update appended to $BASHRC."
  log_info "Run this now to use textlt immediately: source ~/.bashrc"
}

main() {
  log_info "Starting textlt installer."
  require_project_root
  validate_dependencies
  configure_and_build
  deploy_binary
  ensure_local_bin_on_path
  log_success "Installation complete. Run: textlt"
}

main "$@"
