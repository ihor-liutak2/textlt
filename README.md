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

#### Linux (Debian, Ubuntu, Fedora, Arch, and others)

One-liner install (downloads the latest release automatically):

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-linux.sh | bash
```

Or install a specific version:

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-linux.sh | bash -s -- v0.9.3
```

After installing, start the editor from any terminal with:

```bash
textlt
textlt path/to/file.cpp
```

To uninstall TextLT from Linux:

```bash
rm -rf "$HOME/.local/share/textlt" "$HOME/.local/bin/textlt"
```

#### Windows

Open PowerShell and run:

```powershell
irm https://ihor-liutak2.github.io/textlt/install-textlt-windows.ps1 | iex
```

Or install a specific version:

```powershell
$env:TEXTLT_VERSION = "v0.9.3"; irm https://ihor-liutak2.github.io/textlt/install-textlt-windows.ps1 | iex
```

Open a new PowerShell window after installing so Windows reloads `PATH`, then start the editor with:

```powershell
textlt
textlt path\to\file.cpp
```

To uninstall TextLT from Windows:

```powershell
Remove-Item "$env:LOCALAPPDATA\Programs\textlt" -Recurse -Force
```

#### macOS (Intel and Apple Silicon)

One-liner install (downloads the latest release automatically):

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-macos.sh | bash
```

Or install a specific version:

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-macos.sh | bash -s -- v0.9.3
```

After installing, start the editor from any terminal with:

```bash
textlt
textlt path/to/file.cpp
```

To uninstall TextLT from macOS:

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

Install the optional local Piper server used by Assistant → Server:

```bash
./scripts/textlt-install-piper-server.sh build
```

This copies `textlt-piper-server` to `$HOME/.local/share/textlt/piper/bin/`.

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

