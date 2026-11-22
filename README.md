# textlt

[Website & installation guide](https://ihor-liutak2.github.io/textlt/)

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
  - `Ctrl+Click` extends text selection inside the editor without triggering the terminal's native `Shift+Click` screen selection.
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

### Check an installation

Run `textlt --startup-check` from the account that will use the editor. On Windows,
open a regular PowerShell window after an administrator installs TextLT. The
report tests local tools, bundled processors, and the account’s access to its
configuration, data, cache, and temporary directories. Each failed check names
the tool or path. Exit code zero means the checks and editor initialization passed.
Piper is an optional download. The checks run offline.

### Connect AI Settings

Open **Tools → AI Settings** and choose a provider tab. Select **Get Models**,
highlight a model, and select **Use Model**. **Save** keeps the provider's
settings, and **Set Active** marks that model as the active one. The
**Providers** tab lists saved models and can switch between them. Effort is saved
for each model. Leave it blank to use the provider default.

| Provider | Connection |
| --- | --- |
| Ollama | Start your local Ollama server and use `http://localhost:11434/v1` or enter its URL. **Get Efforts** checks which thinking levels the selected model reports. |
| OpenCode | Enter a Console API key. Check **Use Go subscription** for Go or clear it for pay-as-you-go. |
| ChatGPT | Install the Codex CLI, run `codex login` with your ChatGPT account, then use `codex` or enter the path to its executable. |
| Claude | Enter an Anthropic API key. |
| MiMo | Enter a Xiaomi MiMo API key. |

TextLT masks the API key fields and stores saved keys in its local configuration
file. Double-click any input to select its contents. Paste with **Ctrl+V** or
your terminal's paste shortcut, usually **Ctrl+Shift+V** in Windows Terminal.
**Show** reveals a key until you hide it or leave the tab. **Info** explains
the fields on the current tab. **Copy Message** copies the full status or
error. **Test Model** sends a short sample through the configured provider.

Use **Tools → Quick AI** or **Alt+M** to rephrase or translate selected
text or the current paragraph in Docs or an open note. Pick a mode, optionally
enable **Translate** and choose the languages, then select **Do**. Use
**Change** to replace the original selection or paragraph, or **Copy Result**
to use the result elsewhere. Double-click either text pane to select its contents.

### Install from Release

Installing or upgrading resets TextLT configuration, custom themes, text-parser
configuration, and downloaded registries so the release's current defaults and
themes are used. Installed Piper/TTS voices are preserved. Legacy managed local
AI runtimes and models are removed. Manage Ollama and its models separately
before connecting them through AI Settings.

#### Linux (Debian, Ubuntu, Fedora, Arch, and others)

One-liner install (downloads the latest release automatically):

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-linux.sh | bash
```

Or install a specific version:

```bash
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-linux.sh | bash -s -- v1.0.0
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
$env:TEXTLT_VERSION = "v1.0.0"; irm https://ihor-liutak2.github.io/textlt/install-textlt-windows.ps1 | iex
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
curl -fsSL https://ihor-liutak2.github.io/textlt/install-textlt-macos.sh | bash -s -- v1.0.0
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

#### Rebuild and Test Local Changes

From the repository root, configure the project, rebuild it, and run the local
binary:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/textlt
```

Run `./build/textlt` while testing changes. Running `textlt` without the
`./build/` prefix may start an older installed version instead. For mouse
selection testing, drag with the left mouse button to select text, or use
`Ctrl+left-click`/`Ctrl+drag` to extend the selection inside the editor without
triggering the terminal's native `Shift+mouse` screen selection.

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
