# textlt 🚀

`textlt` is a lightweight, blazing-fast, and highly customizable **Terminal User Interface (TUI)** text editor built from scratch in C++. Designed for modern developers who love the efficiency of the command line but prefer intuitive, modeless text editing over complex Vim/Emacs keybindings.

Optimized to run perfectly in native Linux distributions (Ubuntu, MX Linux, Debian) as well as **WSL (Windows Subsystem for Linux)** using Windows Terminal.

---

## ✨ Key Features

- **Intuitive Modeless Editing:** No "command" or "insert" modes. Just open and start typing, exactly like modern GUI text editors.
- **Robust Syntax Highlighting:** Powered by a high-performance single-pass State-Machine lexer supporting a vast polyglot ecosystem:
  - `C++`, `JSON`, `HTML`, `CSS`, `JavaScript`, `TypeScript`, `PHP`, `Java`, `Python`.
  - Full hybrid **React JSX & TSX** context switching with nested curly brace tracking.
- **Smart Word Wrap:** A hybrid word-wrapping engine that breaks lines by words or falls back to character wrapping using a strict safe 10-character boundary threshold.
- **Split Find & Replace:** Dedicated UI overlay panels for quick search (`Ctrl+F`) and text replacement (`Ctrl+H`) with color-blended matching overlays.
- **Fast Navigation:** Instant line jump dialog (`Ctrl+G`) with edge-case buffer clamping.
- **Modern Indentation Framework:** Full **Soft Tabs** support injecting configurable spaces (`2` or `4`) instead of literal tabs, complete with a **Convert Tabs to Spaces** (Untabify) utility.
- **Real-time Status Bar:** Dynamic layout tracking theme configuration, active file extension mapping (e.g., `React TSX Source`), and precise cursor telemetry (`Ln X, Col Y`).
- **Full Mouse Support:** Smooth scroll-wheel navigation, side-panel file manager selection, and click-to-focus window routing.
- **Transaction-safe Undo/Redo:** Every keystroke, multi-line replacement, or global text normalization maps into single atomic snapshot steps.


Installation & BuildClone the repository:Bashgit clone [https://github.com/yourusername/textlt.git](https://github.com/yourusername/textlt.git)
cd textlt
Configure and build the project:Bashcmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
UsageRun the editor directly from your terminal by passing a file path as an argument:Bash./build/textlt path/to/your/file.tsx
⌨️ Shortcuts ReferenceShortcutActionCtrl + SSave current documentCtrl + ZUndo last actionCtrl + YRedo actionCtrl + FOpen Find panelCtrl + HOpen Replace panelCtrl + GOpen Go-To-Line promptTabInsert Soft Tab (Spaces)EscClose active dialogs / prompts📄 LicenseThis project is licensed under the MIT License - see the LICENSE file for details.



---

## 🛠️ Tech Stack

- **Core Language:** C++17 (Modern object-oriented structure)
- **UI Architecture:** [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (Functional Terminal User Interface framework)
- **Build System:** CMake

---

## 🚀 Getting Started

### Prerequisites

Ensure you have a C++17 compliant compiler and CMake installed.

On **Ubuntu / Debian / MX Linux / WSL**:
```bash
sudo apt update
sudo apt install build-essential cmake