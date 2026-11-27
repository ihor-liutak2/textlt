#include "editor/clipboard_controller.hpp"

#include "modals/modal_action_editor.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"
#include "ftxui/screen/terminal.hpp"
#include "ui_button.hpp"
#include "ui_scrollbar.hpp"

namespace textlt {
namespace {

constexpr int kActionContentInputHeight = 16;
constexpr int kActionContentViewportColumns = 96;
constexpr int kActionEditorContentRowsWithoutScrollbar =
    1 + 1 + // Heading and divider.
    4 + 1 + // Title input and spacer.
    1 +     // Tab buttons.
    4 + 1 + // Caption input and spacer.
    17 +    // Content label and input.
    1 + 1;  // Spacer and checkbox.

int WidestLineWidth(const std::string& content) {
    int widest = 0;
    size_t start = 0;
    while (start <= content.size()) {
        const size_t end = content.find('\n', start);
        widest = std::max(widest, ftxui::string_width(
            content.substr(start, end == std::string::npos
                ? std::string::npos : end - start)));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return widest;
}

struct CursorLine {
    size_t start = 0;
    int column = 0;
};

CursorLine FindCursorLine(const std::string& content, size_t cursor) {
    cursor = std::min(cursor, content.size());
    size_t start = cursor == 0
        ? std::string::npos : content.rfind('\n', cursor - 1);
    start = start == std::string::npos ? 0 : start + 1;
    return {start, ftxui::string_width(
        content.substr(start, cursor - start))};
}

size_t ByteOffsetAtColumn(const std::string& line, int column) {
    const auto glyphs = ftxui::Utf8ToGlyphs(line);
    int current_column = 0;
    size_t byte_offset = 0;
    for (const std::string& glyph : glyphs) {
        if (current_column >= column) break;
        current_column += ftxui::string_width(glyph);
        byte_offset += glyph.size();
    }
    return std::min(byte_offset, line.size());
}

ftxui::Element InputField(const std::string& label,
    const ftxui::Component& input, const Theme& theme, ftxui::Box& clear_box,
    int height = 3, ftxui::Box* input_box = nullptr) {
    using namespace ftxui;
    ButtonSpec clear_spec = ButtonSpecFromLabel(
        "Clear", ButtonRole::Warning, ButtonVariant::Minimal, ButtonSize::Compact);
    Element input_element = input->Render() |
        color(theme.modal_input_fg) |
        bgcolor(theme.modal_input_bg) |
        borderStyled(LIGHT, theme.modal_border) |
        size(HEIGHT, EQUAL, height);
    if (input_box) input_element = input_element | reflect(*input_box);
    return vbox({
        hbox({
            text(label) | bold | color(theme.modal_accent) | xflex,
            RenderModalFlatButton(theme, clear_spec) | reflect(clear_box),
        }),
        std::move(input_element),
    });
}

} // namespace

ActionEditorModalContent::ActionEditorModalContent(
    const Theme* theme, SubmitCallback on_submit)
    : theme_(theme), on_submit_(std::move(on_submit)) {
    auto make_options = [this](std::string* value, int* cursor,
                               bool multiline, size_t metadata_limit) {
        ftxui::InputOption option;
        option.cursor_position = cursor;
        option.multiline = multiline;
        if (metadata_limit > 0) {
            option.on_change = [value, cursor, metadata_limit] {
                std::string limited = actions::LimitUtf8Characters(
                    *value, metadata_limit);
                if (limited.size() != value->size()) {
                    *value = std::move(limited);
                    *cursor = std::min(*cursor, static_cast<int>(value->size()));
                }
            };
        }
        option.transform = [this](ftxui::InputState state) {
            const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
            return resolved.InputTransform(std::move(state));
        };
        return option;
    };

    title_input_ = ClipboardController::MakeInput(&title_, "Short descriptive title",
        make_options(&title_, &title_cursor_, false,
            actions::kActionMetadataCharacterLimit));
    for (int index = 0; index < static_cast<int>(actions::kMaxActionTabs); ++index) {
        const size_t tab = static_cast<size_t>(index);
        caption_inputs_[tab] = ClipboardController::MakeInput(&captions_[tab], "Optional caption",
            make_options(&captions_[tab], &caption_cursors_[tab], false,
                actions::kActionCaptionCharacterLimit));
        content_inputs_[tab] = ClipboardController::MakeInput(&contents_[tab],
            "Exact text copied to the clipboard",
            make_options(&contents_[tab], &content_cursors_[tab], true, 0));

        ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
        checkbox_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element element = ftxui::text(
                std::string(state.state ? "[X] " : "[ ] ") + state.label);
            if (state.focused || state.active) {
                return element |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg);
            }
            return element | ftxui::color(theme.modal_text_color);
        };
        hide_text_checkboxes_[tab] = ftxui::Checkbox(
            "Hide text", &hide_text_[tab], checkbox_option);
        tab_forms_[tab] = ftxui::Container::Vertical({
            caption_inputs_[tab], content_inputs_[tab], hide_text_checkboxes_[tab],
        });
    }

    auto make_tab_button = [this](int index) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        const std::string label = "Tab " + std::to_string(index + 1);
        option.label = label;
        option.on_click = [this, index] { SelectTab(index); };
        option.transform = [this, index, label](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return RenderModalTabButton(theme, label,
                active_tab_ == index, state.focused || state.active);
        };
        return ftxui::Button(option);
    };
    std::vector<ftxui::Component> tab_button_components;
    std::vector<ftxui::Component> tab_form_components;
    for (int index = 0; index < static_cast<int>(actions::kMaxActionTabs); ++index) {
        tab_buttons_[static_cast<size_t>(index)] = make_tab_button(index);
        tab_button_components.push_back(tab_buttons_[static_cast<size_t>(index)]);
        tab_form_components.push_back(tab_forms_[static_cast<size_t>(index)]);
    }
    tab_button_row_ = ftxui::Container::Horizontal(std::move(tab_button_components));
    tab_container_ = ftxui::Container::Tab(
        std::move(tab_form_components), &active_tab_);
    container_ = ftxui::Container::Vertical({
        title_input_, tab_button_row_, tab_container_,
    });
}

void ActionEditorModalContent::SelectTab(int index) {
    active_tab_ = std::clamp(index, 0,
        static_cast<int>(actions::kMaxActionTabs) - 1);
}

void ActionEditorModalContent::Begin(const actions::ActionItem* item) {
    id_ = item ? item->id : std::string{};
    title_ = item ? actions::LimitActionMetadata(item->title) : std::string{};
    captions_ = {};
    contents_ = {};
    hide_text_ = {};
    content_scroll_offsets_ = {};
    content_input_boxes_ = {};
    content_scrollbar_boxes_ = {};
    content_scrollbar_dragging_ = false;
    if (item && !item->tabs.empty()) {
        for (size_t index = 0;
             index < std::min(actions::kMaxActionTabs, item->tabs.size()); ++index) {
            captions_[index] = actions::LimitActionCaption(item->tabs[index].caption);
            contents_[index] = item->tabs[index].content;
            hide_text_[index] = item->tabs[index].hide_text;
        }
    }
    active_tab_ = 0;
    status_.clear();
    title_cursor_ = static_cast<int>(title_.size());
    for (size_t index = 0; index < actions::kMaxActionTabs; ++index) {
        caption_cursors_[index] = static_cast<int>(captions_[index].size());
        content_cursors_[index] = static_cast<int>(contents_[index].size());
    }
    title_input_->TakeFocus();
}

std::string ActionEditorModalContent::GetTitle() {
    return EditingExisting() ? "Edit Action" : "Add Action";
}

ftxui::Element ActionEditorModalContent::RenderTitle() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::text(" " + GetTitle() + " ") |
        ftxui::bold | ftxui::color(theme.modal_accent);
}

ModalSizePreference ActionEditorModalContent::GetModalSizePreference() const {
    const bool has_scrollbar = WidestLineWidth(
        contents_[static_cast<size_t>(active_tab_)]) >
        kActionContentViewportColumns;
    const int footer_rows = status_.empty() ? 4 : 5;
    const int content_height =
        kActionEditorContentRowsWithoutScrollbar +
        static_cast<int>(has_scrollbar) + 1 + footer_rows;
    const int terminal_height = ftxui::Terminal::Size().dimy;
    const int available_height = terminal_height > 0
        ? std::max(1, terminal_height - 2) : content_height;
    return {
        102,
        std::min(content_height, available_height),
    };
}

ftxui::Element ActionEditorModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    title_clear_box_ = {1, 0, 1, 0};
    caption_clear_boxes_ = {};
    content_clear_boxes_ = {};
    content_input_boxes_ = {};
    content_scrollbar_boxes_ = {};
    tab_boxes_ = {};
    Elements tab_buttons;
    for (size_t index = 0; index < tab_boxes_.size(); ++index) {
        if (index > 0) tab_buttons.push_back(text(" "));
        tab_buttons.push_back(RenderModalTabButton(theme,
            "Tab " + std::to_string(index + 1),
            active_tab_ == static_cast<int>(index)) | reflect(tab_boxes_[index]));
    }
    tab_buttons.push_back(filler());
    tab_buttons.push_back(text("Tabs 2–8 are optional") |
        dim | color(theme.modal_text_color));
    return vbox({
        text("Action details") | bold | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        InputField("Title", title_input_, theme, title_clear_box_),
        text(""),
        hbox(std::move(tab_buttons)),
        InputField("Caption", caption_inputs_[static_cast<size_t>(active_tab_)],
            theme, caption_clear_boxes_[static_cast<size_t>(active_tab_)]),
        text(""),
        InputField("Content", content_inputs_[static_cast<size_t>(active_tab_)],
            theme, content_clear_boxes_[static_cast<size_t>(active_tab_)],
            kActionContentInputHeight,
            &content_input_boxes_[static_cast<size_t>(active_tab_)]),
        RenderContentHorizontalScrollbar(
            static_cast<size_t>(active_tab_), theme),
        text(""),
        hide_text_checkboxes_[static_cast<size_t>(active_tab_)]->Render(),
    }) | size(WIDTH, EQUAL, 98) | color(theme.modal_text_color);
}

ftxui::Element ActionEditorModalContent::RenderContentHorizontalScrollbar(
    size_t tab, const Theme& theme) {
    using namespace ftxui;
    const int content_width = WidestLineWidth(contents_[tab]);
    if (content_width <= kActionContentViewportColumns) {
        content_scrollbar_boxes_[tab] = {1, 0, 1, 0};
        content_scroll_offsets_[tab] = 0;
        return emptyElement();
    }

    const int max_offset = content_width - kActionContentViewportColumns;
    const CursorLine cursor_line = FindCursorLine(
        contents_[tab], static_cast<size_t>(
            std::max(0, content_cursors_[tab])));
    content_scroll_offsets_[tab] = std::clamp(
        cursor_line.column - kActionContentViewportColumns + 1, 0, max_offset);
    const ScrollbarMetrics metrics(kActionContentViewportColumns,
        content_width, static_cast<int>(content_scroll_offsets_[tab]));
    Elements cells;
    cells.reserve(static_cast<size_t>(kActionContentViewportColumns));
    for (int column = 0; column < kActionContentViewportColumns; ++column) {
        if (column >= metrics.thumb_start &&
            column < metrics.thumb_start + metrics.thumb_length) {
            cells.push_back(text("━") | color(theme.button_primary));
        } else {
            cells.push_back(text("─") | color(theme.gutter) | dim);
        }
    }
    return hbox(std::move(cells)) |
        reflect(content_scrollbar_boxes_[tab]);
}

void ActionEditorModalContent::ScrollContentToOffset(
    size_t tab, size_t offset) {
    const std::string& content = contents_[tab];
    size_t line_start = 0;
    size_t longest_start = 0;
    std::string longest_line;
    int longest_width = -1;
    while (line_start <= content.size()) {
        const size_t line_end = content.find('\n', line_start);
        const std::string line = content.substr(line_start,
            line_end == std::string::npos
                ? std::string::npos : line_end - line_start);
        const int width = ftxui::string_width(line);
        if (width > longest_width) {
            longest_width = width;
            longest_start = line_start;
            longest_line = line;
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    const int max_offset = std::max(
        0, longest_width - kActionContentViewportColumns);
    const int clamped_offset = std::clamp(
        static_cast<int>(std::min<size_t>(offset,
            static_cast<size_t>(std::numeric_limits<int>::max()))),
        0, max_offset);
    const int target_column = clamped_offset <
            static_cast<int>(content_scroll_offsets_[tab])
        ? clamped_offset
        : std::min(longest_width,
              clamped_offset + kActionContentViewportColumns - 1);
    content_cursors_[tab] = static_cast<int>(
        longest_start + ByteOffsetAtColumn(longest_line, target_column));
    content_scroll_offsets_[tab] = static_cast<size_t>(clamped_offset);
    content_inputs_[tab]->TakeFocus();
}

bool ActionEditorModalContent::HandleEvent(ftxui::Event event) {
    if (!event.is_mouse()) return false;
    const ftxui::Mouse mouse = event.mouse();
    if (content_scrollbar_dragging_ &&
        mouse.motion == ftxui::Mouse::Released) {
        content_scrollbar_dragging_ = false;
        return true;
    }
    if (content_scrollbar_dragging_ &&
        mouse.motion == ftxui::Mouse::Moved) {
        const size_t tab = content_scrollbar_drag_tab_;
        const int content_width = WidestLineWidth(contents_[tab]);
        const int max_offset = std::max(
            0, content_width - kActionContentViewportColumns);
        const ScrollbarMetrics metrics(kActionContentViewportColumns,
            content_width, static_cast<int>(content_scroll_offsets_[tab]));
        if (metrics.Travel() > 0) {
            const int thumb_start = std::clamp(
                mouse.x - content_scrollbar_boxes_[tab].x_min -
                    content_scrollbar_drag_thumb_start_,
                0, metrics.Travel());
            const size_t offset = static_cast<size_t>(std::clamp(
                metrics.OffsetForThumb(thumb_start), 0, max_offset));
            ScrollContentToOffset(tab, offset);
        }
        return true;
    }
    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) return false;
    for (size_t tab = 0; tab < content_scrollbar_boxes_.size(); ++tab) {
        const ftxui::Box& box = content_scrollbar_boxes_[tab];
        if (!box.Contain(mouse.x, mouse.y)) continue;
        const int content_width = WidestLineWidth(contents_[tab]);
        const int max_offset = std::max(
            0, content_width - kActionContentViewportColumns);
        const ScrollbarMetrics metrics(kActionContentViewportColumns,
            content_width, static_cast<int>(content_scroll_offsets_[tab]));
        const int relative_x = mouse.x - box.x_min;
        int thumb_start = metrics.thumb_start;
        if (relative_x >= metrics.thumb_start &&
            relative_x < metrics.thumb_start + metrics.thumb_length) {
            content_scrollbar_dragging_ = true;
            content_scrollbar_drag_tab_ = tab;
            content_scrollbar_drag_thumb_start_ =
                relative_x - metrics.thumb_start;
        } else {
            thumb_start = std::clamp(relative_x - metrics.thumb_length / 2,
                0, metrics.Travel());
        }
        const size_t offset = static_cast<size_t>(std::clamp(
            metrics.OffsetForThumb(thumb_start), 0, max_offset));
        ScrollContentToOffset(tab, offset);
        return true;
    }
    for (size_t index = 0; index < tab_boxes_.size(); ++index) {
        if (tab_boxes_[index].Contain(mouse.x, mouse.y)) {
            SelectTab(static_cast<int>(index));
            return true;
        }
    }
    if (title_clear_box_.Contain(mouse.x, mouse.y)) {
        title_.clear();
        title_cursor_ = 0;
        title_input_->TakeFocus();
        return true;
    }
    const size_t tab = static_cast<size_t>(active_tab_);
    if (caption_clear_boxes_[tab].Contain(mouse.x, mouse.y)) {
        captions_[tab].clear();
        caption_cursors_[tab] = 0;
        caption_inputs_[tab]->TakeFocus();
        return true;
    }
    if (content_clear_boxes_[tab].Contain(mouse.x, mouse.y)) {
        contents_[tab].clear();
        content_cursors_[tab] = 0;
        content_inputs_[tab]->TakeFocus();
        return true;
    }
    return false;
}

bool ActionEditorModalContent::Submit() {
    if (title_.empty()) {
        status_ = "Title is required.";
        title_input_->TakeFocus();
        return false;
    }
    size_t last_populated_tab = 0;
    for (size_t index = 1; index < actions::kMaxActionTabs; ++index) {
        if (!captions_[index].empty() || !contents_[index].empty() ||
            hide_text_[index]) last_populated_tab = index;
    }
    for (size_t index = 1; index <= last_populated_tab; ++index) {
        if (contents_[index].empty() &&
            (!captions_[index].empty() || hide_text_[index])) {
            status_ = "Tab " + std::to_string(index + 1) +
                " needs content, or clear its caption and hide option.";
            SelectTab(static_cast<int>(index));
            caption_inputs_[index]->TakeFocus();
            return false;
        }
    }

    std::vector<actions::ActionTab> tabs = {
        {captions_[0], contents_[0], hide_text_[0]},
    };
    for (size_t index = 1; index <= last_populated_tab; ++index) {
        tabs.push_back({captions_[index], contents_[index], hide_text_[index]});
    }
    actions::ActionItem item = actions::ActionItem::WithTabs(
        id_, title_, std::move(tabs));
    std::string error;
    if (!on_submit_ || !on_submit_(item, error)) {
        status_ = error.empty() ? "Could not save the action." : error;
        return false;
    }
    status_.clear();
    return true;
}

ActionEditorModal::ActionEditorModal(
    const Theme* theme, SubmitCallback on_submit) {
    content_ = std::make_shared<ActionEditorModalContent>(
        theme, std::move(on_submit));
    modal_ = std::make_shared<ModalWindow>(content_, theme, [this] { Close(); });
    modal_->SetHeaderCloseVisible(true);
    modal_->SetBodyFrameScrolling(true);
    ConfigureFooter();
}

void ActionEditorModal::ConfigureFooter() {
    modal_->SetFooterButtons({
        {"Cancel", [this] { Close(); }, ButtonRole::Cancel},
        {content_->EditingExisting() ? "Save" : "Add",
         [this] { if (content_->Submit()) Close(); }, ButtonRole::Primary},
    });
}

void ActionEditorModal::Open(const actions::ActionItem* item) {
    content_->Begin(item);
    ConfigureFooter();
    open_ = true;
    modal_->RefreshChildren();
    modal_->TakeFocus();
    content_->FocusFirstField();
}

void ActionEditorModal::Close() {
    open_ = false;
    content_->ClearStatus();
}

bool ActionEditorModal::OnEvent(ftxui::Event event) {
    if (!open_) return false;
    if (event.is_mouse() && content_->HandleEvent(event)) return true;
    if (!event.is_mouse() && event != ftxui::Event::Escape) {
        if (ftxui::Component form = content_->GetMainComponent();
            form && form->OnEvent(event)) return true;
    }
    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
