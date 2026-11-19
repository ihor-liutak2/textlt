# FTXUI layout reference for notes columns

This note preserves the FTXUI examples supplied for the notes editor column
work and records how they map to TextLT. TextLT currently builds against
FTXUI v7.0.3 (`CMakeLists.txt`).

The examples are kept as reference material. The notes editor now implements
paged columns using the composition described at the end of this note.

## 1. Horizontal and vertical layout with flex

```cpp
#include <ftxui/dom/elements.hpp>  // for text, center, separator, operator|, flex, Element, vbox, Fit, hbox, border
#include <ftxui/screen/screen.hpp>  // for Full, Screen
#include <memory>                   // for allocator

#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/color.hpp"  // for ftxui

int main() {
  using namespace ftxui;
  auto document = hbox({
                      text("left-column"),
                      separator(),
                      vbox({
                          center(text("top")) | flex,
                          separator(),
                          center(text("bottom")),
                      }) | flex,
                      separator(),
                      text("right-column"),
                  }) |
                  border;
  auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
  Render(screen, document);
  screen.Print();

  return 0;
}
```

## 2. Nested grid layout

```cpp
#include <stdio.h>  // for getchar
#include <ftxui/dom/elements.hpp>  // for Elements, gridbox, Fit, operator|, text, border, Element
#include <ftxui/screen/screen.hpp>  // for Screen
#include <memory>                   // for allocator, shared_ptr

#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/color.hpp"  // for ftxui

int main() {
  using namespace ftxui;
  auto cell = [](const char* t) { return text(t) | border; };
  auto document =  //
      gridbox({
          {
              cell("north-west"),
              cell("north"),
              cell("north-east"),
          },
          {
              cell("center-west"),
              gridbox({
                  {
                      cell("center-north-west"),
                      cell("center-north-east"),
                  },
                  {
                      cell("center-south-west"),
                      cell("center-south-east"),
                  },
              }),
              cell("center-east"),
          },
          {
              cell("south-west"),
              cell("south"),
              cell("south-east"),
          },
      });
  auto screen = Screen::Create(Dimension::Fit(document));
  Render(screen, document);
  screen.Print();
  getchar();

  return 0;
}
```

## 3. Paragraph and hflow

```cpp
#include <chrono>                   // for operator""s, chrono_literals
#include <ftxui/screen/screen.hpp>  // for Full, Screen
#include <iostream>                 // for cout, ostream
#include <memory>                   // for allocator, shared_ptr
#include <string>                   // for string, operator<<
#include <thread>                   // for sleep_for

#include "ftxui/dom/elements.hpp"  // for hflow, paragraph, separator, hbox, vbox, filler, operator|, border, Element
#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/box.hpp"    // for ftxui

using namespace std::chrono_literals;
int main() {
  using namespace ftxui;
  std::string p =
      R"(In probability theory and statistics, Bayes' theorem (alternatively Bayes' law or Bayes' rule) describes the probability of an event, based on prior knowledge of conditions that might be related to the event. For example, if cancer is related to age, then, using Bayes' theorem, a person's age can be used to more accurately assess the probability that they have cancer, compared to the assessment of the probability of cancer made without knowledge of the person's age. One of the many applications of Bayes' theorem is Bayesian inference, a particular approach to statistical inference. When applied, the probabilities involved in Bayes' theorem may have different probability interpretations. With the Bayesian probability interpretation the theorem expresses how a subjective degree of belief should rationally change to account for availability of related evidence. Bayesian inference is fundamental to Bayesian statistics.)";

  std::string reset_position;
  while (true) {
    auto document = vbox({
                        hflow(paragraph(p)),
                        separator(),
                        hflow(paragraph(p)),
                        separator(),
                        hbox({
                            hflow(paragraph(p)),
                            separator(),
                            hflow(paragraph(p)),
                        }),
                    }) |
                    border;

    document = vbox(filler(), document);

    // auto screen = Screen::Create(Dimension::Fit(document));
    // Render(screen, document);
    // screen.Print();
    // getchar();

    auto screen = Screen::Create(Dimension::Full());
    Render(screen, document);
    std::cout << reset_position;
    screen.Print();
    reset_position = screen.ResetPosition();

    std::this_thread::sleep_for(0.01s);
  }

  return 0;
}
```

## 4. Separator styles

```cpp
#include <ftxui/screen/screen.hpp>  // for Screen
#include <iostream>                 // for endl, cout, ostream
#include <memory>                   // for allocator

#include "ftxui/dom/elements.hpp"  // for text, hbox, separatorDouble, separatorHeavy, separatorLight, vbox, operator|, Element, Fit, borderDouble, borderHeavy, borderLight
#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/box.hpp"    // for ftxui

int main() {
  using namespace ftxui;

  auto document = vbox({
      vbox({
          text("separatorLight"),
          separatorLight(),
          hbox(text("left"), separatorLight(), text("right")),
      }) | borderLight,

      vbox({
          text("separatorDashed"),
          separatorDashed(),
          hbox(text("left"), separatorDashed(), text("right")),
      }) | borderDashed,

      vbox({
          text("separatorHeavy"),
          separatorHeavy(),
          hbox(text("left"), separatorHeavy(), text("right")),
      }) | borderHeavy,

      vbox({
          text("separatorDouble"),
          separatorDouble(),
          hbox(text("left"), separatorDouble(), text("right")),
      }) | borderDouble,
  });

  auto screen =
      Screen::Create(Dimension::Fit(document), Dimension::Fit(document));
  Render(screen, document);
  screen.Print();
  std::cout << std::endl;
}
```

## 5. Table

```cpp
#include <ftxui/dom/elements.hpp>  // for color, Fit, LIGHT, align_right, bold, DOUBLE
#include <ftxui/dom/table.hpp>      // for Table, TableSelection
#include <ftxui/screen/screen.hpp>  // for Screen
#include <iostream>                 // for endl, cout, ostream
#include <string>                   // for basic_string, allocator, string
#include <vector>                   // for vector

#include "ftxui/dom/node.hpp"  // for Render
#include "ftxui/screen/color.hpp"  // for Color, Color::Blue, Color::Cyan, Color::White, ftxui

int main() {
  using namespace ftxui;

  auto table = Table({
      {"Version", "Marketing name", "Release date", "API level", "Runtime"},
      {"2.3", "Gingerbread", "February 9 2011", "10", "Dalvik 1.4.0"},
      {"4.0", "Ice Cream Sandwich", "October 19 2011", "15", "Dalvik"},
      {"4.1", "Jelly Bean", "July 9 2012", "16", "Dalvik"},
      {"4.2", "Jelly Bean", "November 13 2012", "17", "Dalvik"},
      {"4.3", "Jelly Bean", "July 24 2013", "18", "Dalvik"},
      {"4.4", "KitKat", "October 31 2013", "19", "Dalvik and ART"},
      {"5.0", "Lollipop", "November 3 2014", "21", "ART"},
      {"5.1", "Lollipop", "March 9 2015", "22", "ART"},
      {"6.0", "Marshmallow", "October 5 2015", "23", "ART"},
      {"7.0", "Nougat", "August 22 2016", "24", "ART"},
      {"7.1", "Nougat", "October 4 2016", "25", "ART"},
      {"8.0", "Oreo", "August 21 2017", "26", "ART"},
      {"8.1", "Oreo", "December 5 2017", "27", "ART"},
      {"9", "Pie", "August 6 2018", "28", "ART"},
      {"10", "10", "September 3 2019", "29", "ART"},
      {"11", "11", "September 8 2020", "30", "ART"},
  });

  table.SelectAll().Border(LIGHT);

  // Add border around the first column.
  table.SelectColumn(0).Border(LIGHT);

  // Make first row bold with a double border.
  table.SelectRow(0).Decorate(bold);
  table.SelectRow(0).SeparatorVertical(LIGHT);
  table.SelectRow(0).Border(DOUBLE);

  // Align right the "Release date" column.
  table.SelectColumn(2).DecorateCells(align_right);

  // Select row from the second to the last.
  auto content = table.SelectRows(1, -1);
  // Alternate in between 3 colors.
  content.DecorateCellsAlternateRow(color(Color::Blue), 3, 0);
  content.DecorateCellsAlternateRow(color(Color::Cyan), 3, 1);
  content.DecorateCellsAlternateRow(color(Color::White), 3, 2);

  // Decorate 2 random cells with a red border.
  table.SelectCell(3, 4).Border(LIGHT, color(Color::Red));
  table.SelectCell(2, 7).Border(LIGHT, color(Color::Red));

  auto document = table.Render();
  auto screen =
      Screen::Create(Dimension::Fit(document, /*extend_beyond_screen=*/true));
  Render(screen, document);
  screen.Print();
  std::cout << std::endl;

  return 0;
}
```

## 6. Filler-based alignment

```cpp
#include <stdio.h>                  // for getchar
#include <ftxui/dom/elements.hpp>   // for filler, text, hbox, vbox
#include <ftxui/screen/screen.hpp>  // for Full, Screen
#include <memory>                   // for allocator

#include "ftxui/dom/node.hpp"      // for Render
#include "ftxui/screen/color.hpp"  // for ftxui

int main() {
  using namespace ftxui;
  auto document =  //
      vbox({
          hbox({
              text("north-west"),
              filler(),
              text("north-east"),
          }),
          filler(),
          hbox({
              filler(),
              text("center"),
              filler(),
          }),
          filler(),
          hbox({
              text("south-west"),
              filler(),
              text("south-east"),
          }),
      });
  auto screen = Screen::Create(Dimension::Full());
  Render(screen, document);
  screen.Print();
  getchar();

  return 0;
}
```

## API observations relevant to notes columns

### `paragraph`

- `paragraph(text)` is implemented as a `vbox` of `flexbox` lines. Each line is
  split into words, and the flexbox inserts one cell of gap between adjacent
  words.
- A local probe against v7.0.3 confirmed that runs of spaces are preserved and
  long words are broken across lines.
- It accepts only a plain `std::string_view`. It cannot carry inline styles,
  a cursor, a selection, or a byte-to-screen-cell map.
- It therefore cannot replace the editable note body. The notes editor needs
  the wrapper's `EditorRow { block, start, end, prefix, last }` records to:
  - place the cursor at a byte offset;
  - render selection and per-run formatting;
  - map a mouse click back to a note position;
  - keep the cursor visible while scrolling.
- `paragraph` is still a good fit for read-only text, such as modal help and
  confirmation copy. The notes workspace already uses it for the confirmation
  popup message.

### Separators

- `separator()` and `separatorDashed()` draw box-drawing lines. The dashed
  style uses `╏` vertically and `╍` horizontally; it is not a dot pattern.
- `separatorCharacter("·")` fills its allocated box with the supplied string.
  A one-cell-wide vertical separator between columns therefore renders as a
  dotted line. This was confirmed with a local probe against v7.0.3.
  `separatorCharacter("┊")` is another dotted/dashed alternative.
- In an `hbox`, a separator keeps its one-cell minimum width while `xflex`
  columns take the remaining space.

### `gridbox` and `Table`

- `gridbox` lays out a rectangular matrix of elements and distributes extra
  space equally by default. It is useful when rows and columns must align.
- `Table` is a helper built on `gridbox`. It adds border/separator cells,
  supports `SelectRow`, `SelectColumn`, `Border`, `SeparatorVertical`, and
  cell decoration, and is intended for tabular data.
- For notes columns, the columns contain independent vertical runs of text,
  not aligned table cells. `Table` would add an outer grid, force border
  styling through `BorderStyle` values, and still does not expose per-cell
  geometry for cursor/click mapping. An `hbox` of `vbox` columns with
  `separatorCharacter("·")` is the simpler and more controllable composition.
- If equal column widths are required, each column can be wrapped in
  `size(WIDTH, EQUAL, width)`; `reflect` can capture each column box for exact
  mouse mapping.

## Implications for the notes column feature

1. Keep the existing visual-row model for the editable body. It is the part
   that makes cursor, selection, wrapping, and click mapping correct.
2. Add the column container with FTXUI layout primitives: `hbox` for columns,
   `vbox` for each column's rows, and `separatorCharacter("·")` between
   columns. This removes any custom column-boundary math; use `reflect` boxes
   to map clicks to a column.
3. The natural model is paged newspaper-style columns: visual rows are read
   top-to-bottom in the first column, then continue in the next column. The
   existing `editor_scroll_` can represent the first visible row, and
   `visible_rows * column_count` becomes the page height used by the
   scrollbar and Page Up/Page Down. The scrollbar track is only
   `visible_rows` tall, so its thumb fraction must use the larger page size,
   not just the viewport height.
4. Add `Cols:` plus `1`, `2`, `3` buttons to the existing editor toolbar.
   Reuse `ToolbarItem` for the active state and add one `ftxui::Box` per
   button for mouse hit testing.
5. Changing the column count changes the width available to each column, so
   the wrap model must be rebuilt with the narrower width. The cursor row and
   scroll position then need clamping as they do today.
6. `paragraph` can be introduced for read-only previews only. It cannot be
   used for the edit-mode body without removing cursor/selection precision.

## Implementation

The notes editor now supports one to three paged columns:

- the editor toolbar has a `Cols:` label and `1`, `2`, `3` buttons;
- the selected value is stored in `EditorConfig::notes_editor_columns` and
  persisted in `config.json`;
- `NotesWorkspaceComponent::SetEditorColumns()` clamps the value to `1..3`;
- `RenderEditor()` builds the visual rows with the per-column text width and
  renders the columns with `hbox`, `vbox`, `filler`, `size`, `reflect`, and
  `separatorCharacter("·")`;
- `NotePositionAt()` uses one reflected box per column, so mouse clicks and
  drag selection work in every column;
- Page Up/Page Down scroll by `column_rows * column_count`, and the scrollbar
  thumb uses the same page size while keeping its track at the column height.

