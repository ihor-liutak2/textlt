# TextLT UI Button System

This document defines the TextLT button design system.

The foundation layer adds semantic theme tokens, C++ button roles, variants,
states, sizes, and a small rendering factory. Existing modal code can migrate
one modal at a time.

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

- `Bracket` — classic `[ Caption ]` terminal button.
- `AccentBar` — colored left marker and caption.
- `Pill` — padded label with background.
- `ColoredBrackets` — brackets use the semantic role accent and the caption
  uses the normal button text color.
- `Minimal` — plain command-style caption.
- `Shadow` — optional two-line 3D-like style for themes that want it.

## Factory helpers

`src/ui_button.hpp` provides these helpers:

```cpp
ButtonCaptionText(spec);
PadButtonCaption(text, size);
RenderButton(theme, spec, focused);
MakeButtonOption(theme, spec, on_click);
MakeButton(theme, spec, on_click);
```

Use `RenderButton()` when an existing FTXUI component needs dynamic state, such
as selected tabs. Use `MakeButton()` when a simple action button is enough.

## First migration

The TTS modal is the first pilot migration:

- tabs use `ButtonRole::Tab` with `ButtonVariant::AccentBar`;
- Play/Pause/Next use `ButtonRole::Media`;
- Stop uses `ButtonRole::Warning`;
- Delete/Clear audio cache use `ButtonRole::Danger`;
- Save/Set current use `ButtonRole::Primary`;
- Close uses `ButtonRole::Cancel`.

## Migration plan

1. Keep old helper functions in other modals until each modal is migrated.
2. Migrate one modal per patch to keep visual and behavior changes reviewable.
3. Prefer semantic roles over hard-coded colors.
4. Keep `Bracket` or `ColoredBrackets` when compact layout matters.
5. Use `AccentBar` for tabs, media controls, and primary toolbar actions.
