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
- `AccentBar` — legacy colored left marker and caption.
- `AccentEdges` — default TextLT textual button with colored left and right
  semantic edges, for example `▌ Save ▐`.
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
5. Use `AccentEdges` for regular textual buttons. Keep `AccentBar` only when
   a compact one-sided marker is intentionally needed.

## Remote and Files migration

The Remote Connections, Remote Files, and Files modals are the first broad
workflow migration after the TTS pilot:

- navigation actions use `ButtonRole::Navigation`;
- create/open/save/confirm actions use `ButtonRole::Primary`;
- rename/test/sync actions use `ButtonRole::Secondary`;
- copy/refresh/path/error helpers use `ButtonRole::Utility`;
- clear/cut/overwrite-like actions use `ButtonRole::Warning`;
- delete actions use `ButtonRole::Danger`;
- close/cancel actions use `ButtonRole::Cancel`;
- remote provider choices use `ButtonRole::Toggle`.

This keeps destructive, confirmation, navigation, and utility controls visually
consistent across file-management workflows without changing their behavior.

## Migration patch B

The second migration pass moves Search, Git, Git Settings, Text Processors,
and Custom Processor Builder controls to the shared button renderer. The modal
logic is unchanged; only button rendering now goes through semantic roles and
variants.

Role mapping used in this pass:

- Search: tabs use `Tab`, mask navigation uses `Navigation`, directory switches
  use `Toggle`, open/apply/save actions use `Primary`, copy/paste actions use
  `Utility`, and delete uses `Danger`.
- Git: commit/open/checkout/push/fetch actions use `Primary`, copy/refresh/check
  actions use `Utility`, branch rewrite operations use `Warning`, delete and
  force push use `Danger`, and confirmation rows use `Primary` plus `Cancel`.
- Processor modals: scope/group selectors use `Tab`, builder utility actions use
  `Utility`, save uses `Primary`, clear uses `Warning`, delete uses `Danger`,
  and close uses `Cancel`.

## Patch C migration notes

Small modal cleanup uses the shared button system in generic and lightweight modals:

- `ModalWindow` footer buttons and header close button now resolve semantic roles from labels.
- `KeyboardShortcutsModal` uses tab, primary, warning and cancel button roles.
- `ViewLayoutModal` uses toggle buttons for layout presets and semantic action buttons for pane/document operations.
- `AssistantSettingsModal` uses semantic buttons for registry fetch, installs, downloads, tests, delete confirmations and tab buttons.

`ButtonRoleFromLabel()` is intentionally conservative. Explicit roles in larger modals are still preferred when the context is important, while generic footer buttons can rely on the label-based fallback.

## Patch D presets and polish

Patch D adds preset helpers so new modal code can use semantic buttons without
building `ButtonSpec` objects manually in every call site.

Preferred spec presets:

```cpp
PrimaryButtonSpec("Save"); // renders with AccentEdges by default
SecondaryButtonSpec("Rename");
SuccessButtonSpec("Connected");
WarningButtonSpec("Clear cache");
DangerButtonSpec("Delete");
CancelButtonSpec("Close");
UtilityButtonSpec("Copy path");
NavigationButtonSpec("Parent");
TabButtonSpec("Player", selected);
ToggleButtonSpec("SFTP", selected);
MediaButtonSpec("Play", "▶");
```

Use modifier helpers when a preset needs a small local adjustment:

```cpp
WithButtonSize(DangerButtonSpec("Delete"), ButtonSize::Compact);
WithButtonVariant(SecondaryButtonSpec("Rename"), ButtonVariant::Minimal);
WithButtonEnabled(UtilityButtonSpec("Copy path"), can_copy);
WithButtonSelected(TabButtonSpec("Run"), active_tab == 0);
WithButtonIcon(PrimaryButtonSpec("Generate"), "▶");
```

For simple clickable components, use role wrappers:

```cpp
MakePrimaryButton(&theme, "Save", on_save);
MakeSuccessButton(&theme, "Connected", on_connected, "✓");
MakeWarningButton(&theme, "Clear cache", on_clear);
MakeDangerButton(&theme, "Delete", on_delete);
MakeCancelButton(&theme, on_close);
MakeUtilityButton(&theme, "Copy path", on_copy, "⧉");
MakeNavigationButton(&theme, "Parent", on_parent, "↑");
MakeMediaButton(&theme, "Play", on_play, "▶");
```

`RenderRoleButton()` is available for existing custom components that render
buttons manually and need a semantic one-liner without constructing a full spec.

The label-based fallback has also been widened for generic footer buttons:

- `OK`, `Yes`, `Done`, `Generate`, `Connect`, `Commit`, `Push`, `Fetch`,
  `Import`, and `Export` resolve to `Primary`.
- `Drop`, `Erase`, and `Trash` resolve to `Danger`.
- `Disconnect`, `Rebase`, `Unstage`, and `Abort` resolve to `Warning`.
- `Parent`, `Up`, `Local`, and `Remote` resolve to `Navigation`.
- `Help`, `Preview`, `Path`, `Token`, `Settings`, and `Log` resolve to `Utility`.

For larger modals, explicit presets are still preferred over label inference
because context is clearer in code review.


## Patch E accent edge default

Patch E introduces `ButtonVariant::AccentEdges` as the default visual style for
semantic text buttons. The button caption stays in the normal button text color,
while the left and right edge markers use the semantic role accent color.

Example shape:

```text
▌ Save ▐
▌ Delete ▐
▌ Cancel ▐
```

This keeps TextLT buttons terminal-native, but gives every action a clearer start
and end than the old one-sided `AccentBar` style. `AccentBar`, `Bracket`,
`ColoredBrackets`, `Pill`, `Minimal`, and `Shadow` remain available for special
layouts and theme experiments.

`ButtonVariantForRole()` now resolves all semantic roles to `AccentEdges` by
default. Existing migrated modal buttons that previously used explicit
`AccentBar` or `ColoredBrackets` have also been moved to `AccentEdges`, while
explicit `Minimal` toggle/provider buttons remain minimal.
