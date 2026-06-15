#!/usr/bin/env bash

set -Eeuo pipefail

readonly APP_NAME="textlt"
readonly BUILD_DIR="build"
resolve_target_user() {
  if [[ "$(id -u)" == "0" && -n "${SUDO_USER:-}" && "${SUDO_USER:-}" != "root" ]]; then
    printf '%s\n' "$SUDO_USER"
    return
  fi

  id -un
}

resolve_target_home() {
  local user="$1"
  local home_path=""

  if command -v getent >/dev/null 2>&1; then
    home_path="$(getent passwd "$user" | cut -d: -f6 || true)"
  fi
  if [[ -z "$home_path" && "$user" == "$(id -un)" && -n "${HOME:-}" ]]; then
    home_path="$HOME"
  fi
  if [[ -z "$home_path" ]]; then
    home_path="$(eval "printf '%s' ~$user")"
  fi

  if [[ -z "$home_path" || "$home_path" == "~$user" ]]; then
    printf '[ERROR] Could not resolve home directory for user %s.\n' "$user" >&2
    exit 1
  fi
  printf '%s\n' "$home_path"
}

readonly TARGET_USER="$(resolve_target_user)"
readonly TARGET_HOME="$(resolve_target_home "$TARGET_USER")"
readonly INSTALL_DIR="$TARGET_HOME/.local/bin"
readonly INSTALL_PATH="$INSTALL_DIR/$APP_NAME"
readonly CONFIG_DIR="$TARGET_HOME/.config/textlt"
readonly THEME_CONFIG_DIR="$CONFIG_DIR/themes"
readonly BASHRC="$TARGET_HOME/.bashrc"

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

restore_target_ownership() {
  local path="$1"

  if [[ "$(id -u)" != "0" || "$TARGET_USER" == "root" ]]; then
    return
  fi
  chown -R "$TARGET_USER:" "$path"
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
  if [[ -f "$INSTALL_PATH" ]]; then
    log_info "Found previous installation at $INSTALL_PATH. Removing it for a clean upgrade."
    rm -f "$INSTALL_PATH"
  fi
  cp "$BUILD_DIR/$APP_NAME" "$INSTALL_PATH"
  chmod +x "$INSTALL_PATH"
  restore_target_ownership "$INSTALL_DIR"
  log_success "Installed executable at $INSTALL_PATH."

  log_info "Deploying editor themes to local config directory."
  mkdir -p "$THEME_CONFIG_DIR"
  cp -r themes/* "$THEME_CONFIG_DIR/" 2>/dev/null || log_warn "No local themes folder found to copy."
  restore_target_ownership "$CONFIG_DIR"
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
  restore_target_ownership "$BASHRC"

  log_success "PATH update appended to $BASHRC."
  log_info "Run this now to use textlt immediately: source ~/.bashrc"
}

ensure_truecolor_in_shell_profile() {
  touch "$BASHRC"
  if grep -Fq 'export COLORTERM="truecolor"' "$BASHRC"; then
    log_success "COLORTERM variable is already configured in $BASHRC."
    return
  fi

  log_info "Adding global 24-bit TrueColor flag to $BASHRC."
  {
    printf '\n# Added by textlt installer for advanced theme rendering\n'
    printf 'export COLORTERM="truecolor"\n'
  } >> "$BASHRC"
  restore_target_ownership "$BASHRC"

  log_success "COLORTERM=TrueColor configuration appended to $BASHRC."
}

main() {
  log_info "Starting textlt installer."
  require_project_root
  validate_dependencies
  configure_and_build
  deploy_binary
  ensure_local_bin_on_path
  ensure_truecolor_in_shell_profile
  log_success "Installation complete. Run: textlt"
}

main "$@"
