# FTXUI UI refactoring reference

This note preserves the FTXUI examples supplied for the event/focus/modal
refactoring. The original examples cover these component patterns:

## 1. Scrollable menu

- `Container::Vertical` with `MenuEntry` children and a shared selected index.
- Custom entry rendering through `MenuEntryOption::transform`.
- Viewport clipping with `frame | size(HEIGHT, LESS_THAN, 10)`.
- Focus, active state, keyboard and mouse handling remain in the component tree.

```cpp
int selected = 0;
auto menu = Container::Vertical(
    {MenuEntry("1. improve"), MenuEntry("2. tolerant")}, &selected);
auto view = Renderer(menu, [&] {
  return menu->Render() | frame | size(HEIGHT, LESS_THAN, 10) | border;
});
```

## 2. Multiple menus in one container

- Two `Menu` components owned by `Container::Horizontal`.
- Each menu owns only its selection state.
- The parent renderer composes their DOM output without routing events manually.

```cpp
auto left = Menu(&left_entries, &left_selected, option);
auto right = Menu(&right_entries, &right_selected, option);
auto container = Container::Horizontal({left, right});
```

## 3. Boolean-controlled modal

- The primary component is decorated with `Modal(modal_component, &shown)`.
- While shown, FTXUI sends input to the modal component.
- Opening and closing only changes the boolean state.

```cpp
bool shown = false;
auto main = MainComponent([&] { shown = true; }, exit);
auto dialog = ModalComponent(do_nothing, [&] { shown = false; });
main |= Modal(dialog, &shown);
```

## 4. Layer selected through `Container::Tab`

- An integer depth selects exactly one interactive component layer.
- Rendering uses `dbox`, `clear_under`, and `center` for the overlay.
- Useful where a modal cannot be represented by the `Modal` decorator, but a
  typed application state should replace the raw integer depth.

```cpp
auto layers = Container::Tab({base, dialog}, &depth);
auto view = Renderer(layers, [&] {
  auto document = base->Render();
  if (depth == 1)
    document = dbox({document, dialog->Render() | clear_under | center});
  return document;
});
```

## 5. Nested event loops

- A nested screen can be opened synchronously with `App::FitComponent()`.
- Its Back button exits only that nested loop.
- This is a reference pattern, not the preferred modal architecture for the
  application because nested loops complicate shared state and lifecycle.

```cpp
void Nested(std::string path) {
  auto screen = App::FitComponent();
  auto back = Button("Back", screen.ExitLoopClosure());
  screen.Loop(Container::Vertical({back}));
}
```

## 6. Scrollable window content

- Content is framed with `focusPositionRelative(scroll_x, scroll_y) | frame`.
- Horizontal and vertical `Slider` components act as scrollbars.
- All controls are children of nested vertical/horizontal containers.

```cpp
auto scrollable = Renderer(content, [&, content] {
  return content->Render() | focusPositionRelative(scroll_x, scroll_y) |
         frame | flex;
});
Add(Container::Vertical({
    Container::Horizontal({scrollable, scrollbar_y}) | flex,
    Container::Horizontal({scrollbar_x}),
}));
```

## 7. Stacked windows

- `Window` wraps an interactive inner component.
- `Container::Stacked` controls focus and z-order for overlapping windows.
- Position and dimensions can be fixed values or pointers to mutable state.

```cpp
auto window = Window({
    .inner = content,
    .title = "Window",
    .left = &left,
    .top = &top,
    .width = &width,
    .height = &height,
});
auto windows = Container::Stacked({window_1, window_2});
```

## 8. DOM-only windows and colors

- `ftxui::window` is a DOM element, unlike the interactive `Window` component.
- It is appropriate for visual grouping but does not participate in component
  focus or event dispatch.
- RGB background colors are produced with `bgcolor(Color::RGB(...))`.

## 9. Focus cursor rendering

- A renderer can receive its focused state.
- Cursor decorators (`focus`, `focusCursorBlock`, `focusCursorBar`,
  `focusCursorUnderline` and blinking variants) expose terminal focus without
  custom global event routing.

```cpp
Component Instance(std::string label, Decorator cursor) {
  return Renderer([=](bool focused) {
    return focused ? hbox({text("> " + label), cursor(text(" "))})
                   : text("  " + label);
  });
}
```

## Refactoring implications

1. Interactive elements must be reachable through one FTXUI component tree.
2. Renderers compose DOM; containers own focus and event routing.
3. Standard `Modal` is preferred for one active dialog over the main UI.
4. `Container::Tab` is suitable for mutually exclusive layers when backed by a
   typed state rather than scattered numeric indices.
5. `Container::Stacked`/`Window` is suitable only when multiple movable or
   overlapping interactive windows are actually required.
6. Mouse, scrolling and focus should be implemented by components, not by a
   global event dispatcher or custom DOM hit-testing.
