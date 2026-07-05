# TextLT Button Colors Usage Report

This note documents the current button color tokens after the Button System migration and the recent `AccentEdges` polish. Nothing here is a removal plan. These colors should stay in the theme structure so old themes remain compatible and future UI states can use them.

## Current button color tokens

```text
button_default
button_primary
button_secondary
button_success
button_warning
button_danger
button_cancel
button_utility
button_navigation
button_tab
button_toggle
button_media
button_text
button_muted_text
button_focused_bg
button_focused_fg
button_selected_bg
button_selected_fg
button_disabled_fg
button_shadow
button_bracket
```

## Colors actively used by the current UI

These tokens are already important for the visible button system:

```text
button_primary
button_secondary
button_warning
button_danger
button_cancel
button_utility
button_navigation
button_tab
button_toggle
button_media
button_text
button_focused_bg
button_focused_fg
button_selected_bg
button_selected_fg
```

They drive the semantic roles used across TTS, Files, Remote, Search, Git, Processor modals, small modal footers, and top-bar TTS controls.

## Colors that are unused or only lightly used

### button_success

`button_success` exists through `ButtonRole::Success` and `MakeSuccessButton()`, but the current modal migration almost never marks real buttons as `Success`.

Keep it. It is useful for future actions and statuses such as:

```text
Connected
Installed
Done
Verified
Downloaded
Ready
```

Recommended future use: successful connection/install/download states, not ordinary primary actions.

### button_muted_text

`button_muted_text` is currently not heavily used by button rendering. It should stay because it is useful for lower-priority visual states.

Good future uses:

```text
inactive tabs
inactive toggle choices
minimal secondary buttons
helper/disabled-looking utility captions
```

Recommended future use: inactive `Tab`, inactive `Toggle`, and `Minimal` variant buttons.

### button_bracket

`button_bracket` was useful for the old bracket style:

```text
[ Save ]
```

After moving the default visual language to `AccentEdges`, buttons now look like:

```text
▌ Save ▐
```

The left and right edges are normally colored by the button role accent, not by `button_bracket`. Because of that, `button_bracket` is now mostly a compatibility and fallback token.

Keep it. It still makes sense for:

```text
ButtonVariant::Bracket
ButtonVariant::ColoredBrackets
classic themes
retro terminal themes
```

Recommended future use: only bracket-based variants, not the default `AccentEdges` style.

### button_shadow

`button_shadow` is only relevant for `ButtonVariant::Shadow`. That variant exists, but it is not part of the current default TextLT style.

Keep it as an experimental/theme-ready token.

Good future uses:

```text
3D button theme
pressed/floating button style
special demo theme
```

Recommended future use: only in optional visual variants, not normal modals.

### button_default

`button_default` is mostly a fallback color for `ButtonRole::Default`. Most migrated buttons now have explicit roles or derive roles from labels.

Keep it as a safe fallback. It is useful when a new button has not yet been classified.

Recommended future use: unclassified actions during development.

### button_disabled_fg

`button_disabled_fg` is supported by the button style resolver, but most current UI does not render disabled buttons. Buttons are usually either shown as active components or hidden/not available.

Keep it. It is necessary if we later add real disabled buttons.

Good future uses:

```text
disabled unavailable actions
buttons waiting for selected item
buttons blocked by missing configuration
buttons blocked by no audio player / no remote connection
```

Recommended future use: actual disabled state instead of hiding buttons when the action exists but cannot currently run.

## Recommendation

Do not delete any button color tokens now.

The best next improvement is to start using two underused tokens more deliberately:

```text
button_muted_text → inactive tabs/toggles/minimal buttons
button_success    → connected/installed/done/verified states
```

The least important token for the current default style is:

```text
button_bracket
```

But it should remain because classic bracket variants still exist and old themes may rely on it.

## Practical design direction

The current TextLT default direction should remain:

```text
AccentEdges for normal text buttons:
▌ Caption ▐
```

Role color should be used mainly on the two edge symbols. The caption should normally use `button_text`. This keeps the UI readable and prevents too many competing colors in dense modals.

For quieter areas, prefer:

```text
Minimal variant + button_muted_text
```

For successful states, prefer:

```text
Success role + button_success
```

For danger/warning actions, keep the current clear separation:

```text
Danger  → destructive actions: Delete, Remove, Trash
Warning → risky but recoverable actions: Clear, Cut, Reset, Merge/Rebase/Force Push
```

## Summary

The current button theme palette is intentionally slightly larger than the currently visible usage. That is good. It gives TextLT room for disabled states, success states, classic bracket themes, and optional 3D/shadow variants without changing the theme schema again.

Keep all colors, but document their intended purpose and gradually use `button_muted_text` and `button_success` where they improve readability.
