# TextLT UI Button System

This document defines the first foundation layer for TextLT buttons.

The first implementation stage only adds semantic design tokens and C++
button model types. It does not migrate existing modal buttons yet.

## Roles

- `Default` — neutral action.
- `Primary` — main action such as Save, Open, Apply, Generate.
- `Secondary` — supporting action such as Refresh, Test, Reload.
- `Success` — positive/completed action.
- `Warning` — overwrite, replace, or risky reversible action.
- `Danger` — destructive action such as Delete, Remove, Clear Cache.
- `Cancel` — Cancel, Close, Back, No.
- `Utility` — Copy, Browse, Open Folder, diagnostics.
- `Navigation` — Prev, Next, Back, folder movement.
- `Tab` — tab headers.
- `Toggle` — mode or provider choices.
- `Media` — Play, Stop, Pause, Next.

## Variants

- `Bracket` — classic `[Caption]` terminal button.
- `AccentBar` — colored left marker and caption.
- `Pill` — padded label with background when active.
- `ColoredBrackets` — only brackets use the role accent.
- `Minimal` — plain command-style caption.
- `Shadow` — reserved for themes that want a subtle 3D style.

## Migration plan

1. Keep existing UI unchanged.
2. Use the semantic theme colors as fallback-safe design tokens.
3. Add a button factory/helper in a later patch.
4. Migrate one modal at a time, starting with TTS and Remote.
