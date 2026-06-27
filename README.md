# textlt

`textlt` is a lightweight, blazing-fast, and highly customizable **Terminal User Interface (TUI)** text editor built from scratch in C++. Designed for modern developers who love the efficiency of the command line but prefer intuitive, modeless text editing over complex Vim/Emacs modal constraints.

Optimized to run perfectly in native Linux environments (**MX Linux**, **Ubuntu**, **Debian**) as well as **WSL (Windows Subsystem for Linux)**.

---

## Interface Preview

![Main Editor Interface](assets/main_editor.png)

---

## Key Features

- **Intuitive Modeless Editing:** No "command" or "insert" modes. Just open, click, and start typing, exactly like modern GUI text editors.
- **Advanced Polyglot Syntax Highlighting:** Powered by an ultra-fast state-machine engine that pre-scans syntax layout matrices up to the active viewport boundaries:
  - **Languages:** `C++`, `C`, `PHP` (with native WordPress multi-language support), `Python`, `Ruby` (inc. `Gemfile`), `Java`, `JavaScript`, `TypeScript`, `HTML`, `XML`, `CSS`, `SQL`, `GraphQL`.
  - **Configurations:** `YAML`, `JSON`, `Dockerfile`, `docker-compose.yml`, `.ini`, `.conf`, and `.env` profiles.
- **Embedded Syntax Engine (Heredoc/Nowdoc):** Seamlessly shifts lexical token states inside PHP code streams when encountering `<<<HTML`, `<<<CSS`, or `<<<JS` blocks, matching enterprise IDE behavior.
- **Full Interactive Mouse Integration:**
  - Fluid drag-selection tracking mapped directly onto character coordinate structures.
  - Smooth terminal scroll-wheel navigation.
  - Single-click cursor repositioning and side-panel file manager navigation.
- **Smart Code Commenting:** Context-aware line/block comment toggling via `Ctrl + /` that dynamically inserts language-specific prefixes (`//`, `#`, `--`) aligned cleanly before the first non-whitespace character.
- **Native Viewport Scroller & Scrollbar:** Custom right-aligned vertical TUI Scrollbar indicating precise location markers. Viewport bounds are dynamically calculated, reserving space for status indicators and input panel obstructions.
- **Split Find & Replace Panels:** Dedicated bottom overlay UI regions for real-time text matching (`Ctrl+F`) and variable replacement (`Ctrl+R`) with color-blended highlighting.
- **Modern Layout Foundations:** Soft Tabs framework supporting configurable space injections (`2` or `4`), dynamic line jumping (`Ctrl+G`), and transaction-safe atomic Undo/Redo historical snapshots.

---

## Tech Stack

- **Core Engine:** Modern C++17 (Object-Oriented, clean module subsystem split)
- **UI Architecture:** [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (Functional Terminal User Interface framework)
- **Build Core:** CMake & Bash Deployment Automations

---

## Getting Started

### Install from Release

#### Windows

Copy and paste this into PowerShell. Change `VERSION` to the release tag you want, for example `v0.5.0`.

```powershell
$VERSION = "v0.5.0"
$InstallRoot = "$env:LOCALAPPDATA\Programs\textlt"
$Archive = "$env:TEMP\textlt-windows-x64.zip"

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

Invoke-WebRequest `
  "https://github.com/ihor-liutak2/textlt/releases/download/$VERSION/textlt-windows-x64.zip" `
  -OutFile $Archive

Remove-Item "$InstallRoot\*" -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -Force $Archive -DestinationPath $InstallRoot

$Exe = Get-ChildItem $InstallRoot -Recurse -Filter textlt.exe | Select-Object -First 1

if (-not $Exe) {
    Write-Error "textlt.exe was not found in the extracted archive."
    exit 1
}

$ExeDir = Split-Path $Exe.FullName

@"
@echo off
cd /d "$ExeDir"
"$($Exe.FullName)" %*
"@ | Set-Content -Encoding ASCII "$InstallRoot\textlt.cmd"

$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")

if ($UserPath -notlike "*$InstallRoot*") {
    [Environment]::SetEnvironmentVariable("Path", "$UserPath;$InstallRoot", "User")
}

Write-Host "TextLT was installed to: $ExeDir"
Write-Host "Open a new PowerShell window and run: textlt"
```

Open a new PowerShell window after installing so Windows reloads `PATH`, then start the editor with:

```powershell
textlt
textlt path\to\file.cpp
```

To run it immediately without opening a new PowerShell window:

```powershell
& "$env:LOCALAPPDATA\Programs\textlt\textlt.cmd"
```

To uninstall TextLT from Windows:

```powershell
Remove-Item "$env:LOCALAPPDATA\Programs\textlt" -Recurse -Force
```

#### Linux

Copy and paste this into Bash. Change `VERSION` to the release tag you want, for example `v0.5.0`.

```bash
VERSION="v0.5.0"
INSTALL_ROOT="$HOME/.local/share/textlt"
BIN_DIR="$HOME/.local/bin"
ARCHIVE="/tmp/textlt-linux-x64.tar.gz"

mkdir -p "$INSTALL_ROOT" "$BIN_DIR"
wget -O "$ARCHIVE" "https://github.com/ihor-liutak2/textlt/releases/download/${VERSION}/textlt-linux-x64.tar.gz"
rm -rf "$INSTALL_ROOT"/*
tar -xzf "$ARCHIVE" -C "$INSTALL_ROOT"

EXE="$(find "$INSTALL_ROOT" -type f -name textlt -perm -111 | head -n 1)"

if [ -z "$EXE" ]; then
  echo "textlt executable was not found in the extracted archive." >&2
  exit 1
fi

EXE_DIR="$(dirname "$EXE")"

cat > "$BIN_DIR/textlt" <<EOF
#!/usr/bin/env bash
cd "$EXE_DIR" || exit 1
exec "$EXE" "\$@"
EOF

chmod +x "$BIN_DIR/textlt" "$EXE"

grep -qxF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
grep -qxF 'export COLORTERM=truecolor' "$HOME/.bashrc" || echo 'export COLORTERM=truecolor' >> "$HOME/.bashrc"
grep -qxF 'export TERM=xterm-256color' "$HOME/.bashrc" || echo 'export TERM=xterm-256color' >> "$HOME/.bashrc"

export PATH="$HOME/.local/bin:$PATH"
export COLORTERM=truecolor
export TERM=xterm-256color

textlt
```

After this, start the editor from any terminal with:

```bash
textlt path/to/file.cpp
```

To uninstall TextLT from Linux:

```bash
rm -rf "$HOME/.local/share/textlt" "$HOME/.local/bin/textlt"
```

### Build and Install from Source on Linux

Install build dependencies on Debian, Ubuntu, or MX Linux:

```bash
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libarchive-dev
```

Clone and build:

```bash
git clone https://github.com/ihor-liutak2/textlt.git
cd textlt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Run from the build directory:

```bash
./build/textlt path/to/file.cpp
```

Install the built binary for the current user:

```bash
mkdir -p "$HOME/.local/bin"
cp build/textlt "$HOME/.local/bin/textlt"
chmod +x "$HOME/.local/bin/textlt"
grep -qxF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
```

### Manual Compilation

If you prefer building manually, use CMake directly from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
./build/textlt path/to/your/file.cpp
```

TextLT is a terminal application. On Windows, run it from PowerShell, Windows Terminal, or another terminal emulator. The Windows installer creates a `textlt.cmd` wrapper so bundled resources such as `themes` and `resources` are loaded from the correct directory.

---

## Shortcuts Reference

| Shortcut | Action |
| --- | --- |
| `Ctrl + S` | Save current document buffer to disk |
| `Ctrl + /` | Toggle Smart Line/Block Comment (`//`, `#`, `--`) |
| `Ctrl + F` | Open Split Find Panel |
| `Ctrl + R` | Open Split Replace Panel |
| `Ctrl + G` | Open Go-To-Line prompt box |
| `Ctrl + Z` | Undo last atomic action |
| `Ctrl + Y` | Redo last reverted historical state |
| `Tab` | Insert Configurable Soft Tab (Spaces) |
| `F10` / `Mouse` | Activate global top bar dropdown menus |
| `Shift + Click` | Extend/Anchor block text selection via mouse |
| `Esc` | Close active dialogs / clear panel focus |

---

## Supported Ecosystem Matrix

Open the interactive **Help** window inside the editor at any time to view the live language support grid:

| Language / Context | File Extensions / Target Matchers | Comment Token |
| --- | --- | --- |
| **C++ / C** | `.cpp`, `.hpp`, `.h`, `.c`, `.cc`, `.cxx` | `//` |
| **Go** | `.go` | `//` |
| **Rust** | `.rs` | `//` |
| **PHP** | `.php` *(Supports Nested HTML/CSS/JS Heredocs)* | `//` |
| **Laravel Blade** | `.blade.php` *(HTML fallback plus Blade directives and PHP echo expressions)* | `{{-- --}}` / PHP |
| **JavaScript / TypeScript** | `.js`, `.mjs`, `.ts`, `.mts`, `.jsx`, `.tsx` | `//` |
| **Python** | `.py` | `#` |
| **Ruby** | `.rb`, `Gemfile` | `#` |
| **Java** | `.java` | `//` |
| **HTML / XML / CSS** | `.html`, `.htm`, `.xml`, `.xsd`, `.xsl`, `.xslt`, `.css` | `` / `<!-- -->` / `/* */` |
| **SQL** | `.sql` | `--` |
| **GraphQL** | `.graphql`, `.gql` | `--` |
| **YAML / Compose** | `.yaml`, `.yml`, `docker-compose.yml` | `#` |
| **Docker Engine** | `Dockerfile`, `Dockerfile.*` | `#` |
| **System Diagnostics** | `.ini`, `.conf`, `.json` | `;` / `#` / None |
| **Environment Configs** | `.env`, `.env.local`, `.env.production` | `#` |

---

## License

This project is licensed under the MIT License - see the [LICENSE](https://www.google.com/search?q=LICENSE) file for details.

