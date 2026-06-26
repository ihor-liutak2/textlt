#include "modal_text_processors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr int kProcessorListVisibleRows = 17;
std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

bool IsBackspaceEvent(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace ||
           event.input() == "\x7F" ||
           event.input() == "\x08";
}

bool IsIntegerText(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    size_t index = 0;
    if (value[0] == '-' || value[0] == '+') {
        index = 1;
    }
    if (index >= value.size()) {
        return false;
    }

    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(index), value.end(),
        [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

bool IsDecimalText(const std::string& value, char decimal_separator) {
    if (value.empty()) {
        return false;
    }

    bool has_digit = false;
    bool has_separator = false;
    for (size_t index = 0; index < value.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        if (index == 0 && (ch == '-' || ch == '+')) {
            continue;
        }
        if (std::isdigit(ch) != 0) {
            has_digit = true;
            continue;
        }
        if (static_cast<char>(ch) == decimal_separator && !has_separator) {
            has_separator = true;
            continue;
        }
        return false;
    }

    return has_digit;
}

std::string NormalizeDecimalText(std::string value, char decimal_separator) {
    if (decimal_separator != '.') {
        std::replace(value.begin(), value.end(), decimal_separator, '.');
    }
    return value;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

TextProcessorsModalContent::TextProcessorsModalContent(
    const Theme* theme,
    TargetTextProvider target_text_provider,
    ReplaceTargetCallback replace_target_text,
    CloseCallback on_close)
    : theme_(theme),
      target_text_provider_(std::move(target_text_provider)),
      replace_target_text_(std::move(replace_target_text)),
      on_close_(std::move(on_close)) {
    text_tab_button_ = MakeTextButton("Text", [this] { SetScope(TextParserScope::Text); });
    paragraph_tab_button_ = MakeTextButton("Paragraph", [this] { SetScope(TextParserScope::Paragraph); });
    code_tab_button_ = MakeTextButton("Code", [this] { SetScope(TextParserScope::Code); });

    processor_grid_component_ = ftxui::Renderer([this] {
        return RenderProcessorGrid();
    });

    for (size_t index = 0; index < 4; ++index) {
        ftxui::InputOption option;
        option.multiline = false;
        option.cursor_position = &param_cursors_[index];
        param_inputs_.push_back(ftxui::Input(&param_values_[index], "", option));
    }

    ftxui::InputOption repeat_option;
    repeat_option.multiline = false;
    repeat_option.cursor_position = &repeat_cursor_;
    repeat_option.on_enter = [this] { ApplySelected(); };
    repeat_input_ = ftxui::Input(&repeat_count_, "1", repeat_option);

    ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
    checkbox_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return item | ftxui::color(theme.modal_text_color);
    };
    whole_document_checkbox_ = ftxui::Checkbox(
        "Whole document", &whole_document_, checkbox_option);

    std::vector<ftxui::Component> controls = {
        text_tab_button_,
        paragraph_tab_button_,
        code_tab_button_,
        repeat_input_,
        whole_document_checkbox_,
        processor_grid_component_,
    };
    controls.insert(controls.end(), param_inputs_.begin(), param_inputs_.end());

    container_ = ftxui::Container::Vertical(controls);
}

ftxui::Component TextProcessorsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };

    return ftxui::Button(option);
}

void TextProcessorsModalContent::Open() {
    Reload();
    if (processor_grid_component_) {
        processor_grid_component_->TakeFocus();
    }
}

void TextProcessorsModalContent::Close() {
    status_ = "Select a text processor.";
    status_is_error_ = false;
}

void TextProcessorsModalContent::Reload() {
    std::string error;
    const std::filesystem::path default_processors_directory =
        std::filesystem::current_path() / "text_processors";

    if (!manager_.EnsureUserConfiguration(default_processors_directory, error)) {
        status_ = error;
        status_is_error_ = true;
        filtered_parsers_.clear();
        return;
    }

    if (!manager_.LoadFromUserConfiguration(error)) {
        status_ = error;
        status_is_error_ = true;
        filtered_parsers_.clear();
        return;
    }

    RebuildParserList();
    status_ = "Loaded " + std::to_string(manager_.GetParsers().size()) +
        " text processors from user configuration.";
    status_is_error_ = false;
}

void TextProcessorsModalContent::ApplySelected() {
    const TextParserDefinition* parser = SelectedParser();
    if (parser == nullptr) {
        status_ = "No text processor selected.";
        status_is_error_ = true;
        return;
    }

    std::unordered_map<std::string, std::string> params;
    int repeat_count = 1;
    std::string error;
    if (!ValidateCurrentInputs(params, repeat_count, error)) {
        status_ = error;
        status_is_error_ = true;
        return;
    }

    std::string input_text;
    if (!target_text_provider_ ||
        !target_text_provider_(whole_document_, input_text, error)) {
        status_ = error.empty() ? "Cannot read target text." : error;
        status_is_error_ = true;
        return;
    }

    const TextParserApplyResult result = manager_.ApplyParser(
        parser->id,
        input_text,
        params,
        repeat_count);
    if (!result.success) {
        status_ = result.error.empty() ? "Text processor failed." : result.error;
        status_is_error_ = true;
        return;
    }

    if (!replace_target_text_ ||
        !replace_target_text_(whole_document_, result.text, error)) {
        status_ = error.empty() ? "Cannot replace target text." : error;
        status_is_error_ = true;
        return;
    }

    status_ = "Applied " + parser->name + " to " +
        (whole_document_ ? "whole document." : "selected text.");
    status_is_error_ = false;

    if (on_close_) {
        on_close_();
    }
}

void TextProcessorsModalContent::TogglePinned() {
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        status_ = "No text processor selected.";
        status_is_error_ = true;
        return;
    }

    const std::string parser_id = parser->id;
    const std::string parser_name = parser->name;
    const bool next_pinned_state = !parser->pinned;

    std::string error;
    if (!manager_.SetParserPinnedInUserConfiguration(
            parser_id,
            next_pinned_state,
            error)) {
        status_ = error.empty() ? "Cannot update pinned state." : error;
        status_is_error_ = true;
        return;
    }

    if (!manager_.LoadFromUserConfiguration(error)) {
        status_ = error.empty() ? "Cannot reload text processors." : error;
        status_is_error_ = true;
        return;
    }

    RebuildParserListKeepingSelection(parser_id);
    status_ = std::string(next_pinned_state ? "Pinned " : "Unpinned ") +
        parser_name + ".";
    status_is_error_ = false;
}

bool TextProcessorsModalContent::HandleEvent(ftxui::Event event) {
    bool input_focused = repeat_input_ && repeat_input_->Focused();
    input_focused = input_focused || (whole_document_checkbox_ && whole_document_checkbox_->Focused());
    for (const auto& input : param_inputs_) {
        input_focused = input_focused || (input && input->Focused());
    }

    if (input_focused && !event.is_mouse()) {
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        MoveParserSelection(1);
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        MoveParserSelection(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowRight) {
        MoveParserSelection(0, 1);
        return true;
    }

    if (event == ftxui::Event::ArrowLeft) {
        MoveParserSelection(0, -1);
        return true;
    }

    if (event == ftxui::Event::PageDown) {
        MoveParserSelection(kProcessorListVisibleRows);
        return true;
    }

    if (event == ftxui::Event::PageUp) {
        MoveParserSelection(-kProcessorListVisibleRows);
        return true;
    }

    if (event == ftxui::Event::Return || event.input() == "\x0A") {
        ApplySelected();
        return true;
    }

    if (event.input() == "r" || event.input() == "R") {
        Reload();
        return true;
    }

    if (IsBackspaceEvent(event)) {
        processor_grid_component_->TakeFocus();
        return true;
    }

    if (event.is_mouse()) {
        const ftxui::Mouse& mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::WheelDown) {
            MoveParserSelection(3);
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelUp) {
            MoveParserSelection(-3);
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left &&
            mouse.motion == ftxui::Mouse::Pressed) {
            for (int index = 0; index < processor_cell_count_; ++index) {
                if (processor_cell_indices_[index] >= 0 &&
                    processor_cell_boxes_[index].Contain(mouse.x, mouse.y)) {
                    MoveParserSelectionToIndex(processor_cell_indices_[index]);
                    processor_grid_component_->TakeFocus();
                    return true;
                }
            }
        }
    }

    return false;
}

void TextProcessorsModalContent::SetScope(TextParserScope scope) {
    if (active_scope_ == scope) {
        return;
    }

    active_scope_ = scope;
    selected_parser_ = 0;
    parser_list_top_row_ = 0;
    RebuildParserList();
    status_ = "Showing " + ScopeLabel(scope) + " processors.";
    status_is_error_ = false;
}

TextParserScope TextProcessorsModalContent::CurrentScope() const {
    return active_scope_;
}

void TextProcessorsModalContent::RebuildParserList() {
    filtered_parsers_.clear();

    for (const TextParserDefinition& parser : manager_.GetParsers()) {
        if (parser.scope != active_scope_) {
            continue;
        }
        filtered_parsers_.push_back(&parser);
    }

    std::stable_sort(filtered_parsers_.begin(), filtered_parsers_.end(),
        [](const TextParserDefinition* left, const TextParserDefinition* right) {
            if (!left || !right) {
                return left != nullptr;
            }
            return left->pinned && !right->pinned;
        });

    selected_parser_ = std::clamp(
        selected_parser_,
        0,
        std::max(0, static_cast<int>(filtered_parsers_.size()) - 1));
    EnsureSelectionVisible();
    SyncParamsFromSelection();
}

void TextProcessorsModalContent::RebuildParserListKeepingSelection(
    const std::string& parser_id) {
    RebuildParserList();
    if (parser_id.empty()) {
        return;
    }

    for (size_t index = 0; index < filtered_parsers_.size(); ++index) {
        const TextParserDefinition* parser = filtered_parsers_[index];
        if (parser && parser->id == parser_id) {
            MoveParserSelectionToIndex(static_cast<int>(index));
            return;
        }
    }
}

void TextProcessorsModalContent::SyncParamsFromSelection() {
    const TextParserDefinition* parser = SelectedParser();
    for (size_t index = 0; index < param_values_.size(); ++index) {
        if (parser && index < parser->params.size()) {
            param_values_[index] = parser->params[index].default_value;
        } else {
            param_values_[index].clear();
        }
        param_cursors_[index] = static_cast<int>(param_values_[index].size());
    }

    if (parser) {
        repeat_count_ = std::to_string(std::max(1, parser->repeat_default));
    } else {
        repeat_count_ = "1";
    }
    repeat_cursor_ = static_cast<int>(repeat_count_.size());
}

const TextParserDefinition* TextProcessorsModalContent::SelectedParser() const {
    if (filtered_parsers_.empty()) {
        return nullptr;
    }
    if (selected_parser_ < 0 ||
        selected_parser_ >= static_cast<int>(filtered_parsers_.size())) {
        return nullptr;
    }
    return filtered_parsers_[static_cast<size_t>(selected_parser_)];
}

bool TextProcessorsModalContent::ValidateCurrentInputs(
    std::unordered_map<std::string, std::string>& params,
    int& repeat_count,
    std::string& error) const {
    params.clear();
    repeat_count = 1;

    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        error = "No text processor selected.";
        return false;
    }

    if (!IsIntegerText(repeat_count_)) {
        error = "Repeat must be an integer.";
        return false;
    }

    try {
        repeat_count = std::stoi(repeat_count_);
    } catch (...) {
        error = "Repeat must be an integer.";
        return false;
    }

    if (repeat_count < 1 || repeat_count > 50) {
        error = "Repeat must be between 1 and 50.";
        return false;
    }

    for (size_t index = 0; index < parser->params.size() && index < param_values_.size(); ++index) {
        const TextParserParam& param = parser->params[index];
        const std::string type = NormalizedParamType(param.type);
        std::string value = param_values_[index];

        if (type == "integer") {
            if (!IsIntegerText(value)) {
                error = param.label + " must be an integer.";
                return false;
            }
        } else if (type == "decimal") {
            const char separator = param.decimal_separator.empty()
                ? '.'
                : param.decimal_separator.front();
            if (!IsDecimalText(value, separator)) {
                error = param.label + " must be a decimal number.";
                return false;
            }
            value = NormalizeDecimalText(value, separator);
        } else if (type == "boolean") {
            const std::string lower = ToLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
                value = "true";
            } else if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
                value = "false";
            } else {
                error = param.label + " must be true or false.";
                return false;
            }
        }

        params[param.id] = value;
    }

    return true;
}

int TextProcessorsModalContent::RepeatCountOrDefault() const {
    try {
        size_t parsed = 0;
        const int value = std::stoi(repeat_count_, &parsed);
        if (parsed != repeat_count_.size()) {
            return 1;
        }
        return std::clamp(value, 1, 50);
    } catch (...) {
        return 1;
    }
}

void TextProcessorsModalContent::MoveParserSelection(int row_delta, int column_delta) {
    if (filtered_parsers_.empty()) {
        return;
    }

    const int row = selected_parser_ / 2;
    const int column = selected_parser_ % 2;
    int next_row = row + row_delta;
    int next_column = column + column_delta;

    if (next_column < 0) {
        next_column = 0;
    }
    if (next_column > 1) {
        next_column = 1;
    }

    next_row = std::max(0, next_row);
    int next_index = next_row * 2 + next_column;
    if (next_index >= static_cast<int>(filtered_parsers_.size())) {
        next_index = static_cast<int>(filtered_parsers_.size()) - 1;
    }

    MoveParserSelectionToIndex(next_index);
}

void TextProcessorsModalContent::MoveParserSelectionToIndex(int index) {
    if (filtered_parsers_.empty()) {
        selected_parser_ = 0;
        parser_list_top_row_ = 0;
        SyncParamsFromSelection();
        return;
    }

    selected_parser_ = std::clamp(
        index,
        0,
        static_cast<int>(filtered_parsers_.size()) - 1);
    EnsureSelectionVisible();
    SyncParamsFromSelection();
}

void TextProcessorsModalContent::EnsureSelectionVisible() {
    const int selected_row = selected_parser_ / 2;
    const int max_row = std::max(0, (static_cast<int>(filtered_parsers_.size()) + 1) / 2 - 1);
    parser_list_top_row_ = std::clamp(parser_list_top_row_, 0, max_row);

    if (selected_row < parser_list_top_row_) {
        parser_list_top_row_ = selected_row;
    } else if (selected_row >= parser_list_top_row_ + kProcessorListVisibleRows) {
        parser_list_top_row_ = selected_row - kProcessorListVisibleRows + 1;
    }
}

ftxui::Element TextProcessorsModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return text(" Text Processors ") | bold | color(theme.modal_accent);
}

ftxui::Element TextProcessorsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        RenderScopeTabs(),
        separator() | color(theme.modal_border),
        hbox({
            RenderSelectedParserInfo() |
                size(WIDTH, EQUAL, 49) |
                flex,
            separator() | color(theme.modal_border),
            RenderParameterFields() |
                size(WIDTH, EQUAL, 50) |
                flex,
        }),
        separator() | color(theme.modal_border),
        vbox({
            text("Processors") | bold | color(theme.modal_accent),
            RenderProcessorGrid() | flex,
        }) | flex,
        separator() | color(theme.modal_border),
        RenderStatusLine(),
    }) | bgcolor(theme.modal_background);
}

ftxui::Element TextProcessorsModalContent::RenderScopeTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Scope: ") | bold | color(theme.modal_accent),
        active_scope_ == TextParserScope::Text
            ? text("[Text]") | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : text_tab_button_->Render(),
        text(" "),
        active_scope_ == TextParserScope::Paragraph
            ? text("[Paragraph]") | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : paragraph_tab_button_->Render(),
        text(" "),
        active_scope_ == TextParserScope::Code
            ? text("[Code]") | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : code_tab_button_->Render(),
        filler(),
    });
}

ftxui::Element TextProcessorsModalContent::RenderSelectedParserInfo() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return vbox({
            text("Name") | bold | color(theme.modal_accent),
            text("No processor selected.") | color(theme.modal_text_color),
        });
    }

    Elements rows;
    rows.push_back(text("Name") | bold | color(theme.modal_accent));
    rows.push_back(text(parser->name) | color(theme.modal_text_color));
    rows.push_back(text(""));
    rows.push_back(text("Description") | bold | color(theme.modal_accent));

    const std::vector<std::string> description_lines = WrapText(parser->description, 45);
    if (description_lines.empty()) {
        rows.push_back(text("No description.") | dim | color(theme.modal_text_color));
    } else {
        for (const std::string& line : description_lines) {
            rows.push_back(text(line) | color(theme.modal_text_color));
        }
    }

    return vbox(std::move(rows));
}

ftxui::Element TextProcessorsModalContent::RenderParameterFields() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    Elements rows;

    rows.push_back(hbox({
        text("Parameters") | bold | color(theme.modal_accent),
        filler(),
        text("Repeat: ") | color(theme.modal_text_color),
        repeat_input_->Render() | size(WIDTH, EQUAL, 5),
    }));
    rows.push_back(whole_document_checkbox_->Render());

    if (!parser || parser->params.empty()) {
        rows.push_back(text("No parameters for this processor.") |
            dim |
            color(theme.modal_text_color));
        return vbox(rows);
    }

    for (size_t index = 0; index < parser->params.size() && index < param_inputs_.size(); ++index) {
        const TextParserParam& param = parser->params[index];
        const std::string type = NormalizedParamType(param.type);
        rows.push_back(hbox({
            text(TrimForDisplay(param.label, 17) + ": ") |
                color(theme.modal_text_color) |
                size(WIDTH, EQUAL, 19),
            param_inputs_[index]->Render() |
                size(WIDTH, GREATER_THAN, 18) |
                xflex,
            text(" ") | color(theme.modal_text_color),
            text(type) |
                dim |
                color(theme.modal_text_color) |
                size(WIDTH, EQUAL, 8),
        }));
    }

    const std::string hint = ParamHintText();
    if (!hint.empty()) {
        rows.push_back(text(""));
        rows.push_back(text("Hint") | bold | color(theme.modal_accent));
        for (const std::string& line : WrapText(hint, 46)) {
            rows.push_back(text(line) | dim | color(theme.modal_text_color));
        }
    }

    return vbox(rows);
}

ftxui::Element TextProcessorsModalContent::RenderProcessorGrid() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (filtered_parsers_.empty()) {
        processor_cell_count_ = 0;
        return text("No processors for " + ScopeLabel(active_scope_) + ".") |
            color(theme.modal_text_color) |
            frame;
    }

    const int visible_rows = kProcessorListVisibleRows;
    processor_cell_count_ = 0;

    return hbox({
        RenderProcessorColumn(false, visible_rows) |
            size(WIDTH, EQUAL, 49) |
            flex,
        separator() | color(theme.modal_border),
        RenderProcessorColumn(true, visible_rows) |
            size(WIDTH, EQUAL, 50) |
            flex,
    }) |
        vscroll_indicator |
        size(HEIGHT, EQUAL, kProcessorListVisibleRows);
}

ftxui::Element TextProcessorsModalContent::RenderProcessorColumn(
    bool right_column,
    int visible_rows) const {
    using namespace ftxui;

    Elements rows;
    for (int row = parser_list_top_row_; row < parser_list_top_row_ + visible_rows; ++row) {
        const int parser_index = row * 2 + (right_column ? 1 : 0);
        const int slot = processor_cell_count_ < kMaxVisibleProcessorCells
            ? processor_cell_count_++
            : kMaxVisibleProcessorCells - 1;
        rows.push_back(RenderProcessorCell(parser_index, right_column ? 50 : 49, slot));
    }

    return vbox(std::move(rows)) | flex;
}

ftxui::Element TextProcessorsModalContent::RenderProcessorCell(
    int parser_index,
    int width,
    int cell_slot) const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (cell_slot >= 0 && cell_slot < kMaxVisibleProcessorCells) {
        processor_cell_indices_[cell_slot] = parser_index;
    }

    if (parser_index < 0 || parser_index >= static_cast<int>(filtered_parsers_.size())) {
        if (cell_slot >= 0 && cell_slot < kMaxVisibleProcessorCells) {
            processor_cell_indices_[cell_slot] = -1;
        }
        return text(" ") |
            size(WIDTH, EQUAL, width) |
            reflect(processor_cell_boxes_[cell_slot]);
    }

    const TextParserDefinition* parser = filtered_parsers_[static_cast<size_t>(parser_index)];
    std::string label = parser ? parser->name : "";
    if (label.empty() && parser) {
        label = parser->script_path.filename().string();
    }
    if (parser && parser->pinned) {
        label = "* " + label;
    } else {
        label = "  " + label;
    }

    ftxui::Element row = text(" " + TrimForDisplay(label, static_cast<size_t>(width - 2)) + " ") |
        size(WIDTH, EQUAL, width) |
        reflect(processor_cell_boxes_[cell_slot]);

    if (parser_index == selected_parser_) {
        return row |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg) |
            bold;
    }

    if (parser && parser->pinned) {
        return row | color(theme.modal_accent);
    }

    return row | color(theme.modal_text_color);
}

ftxui::Element TextProcessorsModalContent::RenderStatusLine() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string prefix = status_is_error_ ? "Error: " : "Status: ";
    return hbox({
        text(" " + prefix + TrimForDisplay(status_, 100) + " ") |
            color(status_is_error_ ? theme.modal_accent : theme.modal_text_color),
        filler(),
    });
}

std::vector<std::string> TextProcessorsModalContent::WrapText(
    const std::string& text,
    size_t width) const {
    std::vector<std::string> lines;
    if (text.empty() || width == 0) {
        return lines;
    }

    std::istringstream input(text);
    std::string word;
    std::string line;
    while (input >> word) {
        if (line.empty()) {
            line = word;
            continue;
        }
        if (line.size() + 1 + word.size() <= width) {
            line += " " + word;
            continue;
        }
        lines.push_back(line);
        line = word;
    }

    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string TextProcessorsModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::string TextProcessorsModalContent::ScopeLabel(TextParserScope scope) const {
    switch (scope) {
    case TextParserScope::Paragraph:
        return "paragraph";
    case TextParserScope::Code:
        return "code";
    case TextParserScope::Text:
    default:
        return "text";
    }
}

std::string TextProcessorsModalContent::ScopeDisplay(TextParserScope scope) const {
    switch (scope) {
    case TextParserScope::Paragraph:
        return "Paragraph";
    case TextParserScope::Code:
        return "Code";
    case TextParserScope::Text:
    default:
        return "Text";
    }
}

std::string TextProcessorsModalContent::NormalizedParamType(const std::string& type) const {
    const std::string lower = ToLowerCopy(type);
    if (lower == "int" || lower == "integer") {
        return "integer";
    }
    if (lower == "number" || lower == "float" || lower == "double" || lower == "decimal") {
        return "decimal";
    }
    if (lower == "bool" || lower == "boolean") {
        return "boolean";
    }
    return "text";
}

std::string TextProcessorsModalContent::ParamHintText() const {
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return "";
    }

    for (size_t index = 0; index < parser->params.size() && index < param_inputs_.size(); ++index) {
        if (param_inputs_[index]->Focused() && !parser->params[index].description.empty()) {
            return parser->params[index].description;
        }
    }

    for (const TextParserParam& param : parser->params) {
        if (!param.description.empty()) {
            return param.description;
        }
    }

    return "";
}

TextProcessorsModal::TextProcessorsModal(
    const Theme* theme,
    TargetTextProvider target_text_provider,
    ReplaceTargetCallback replace_target_text)
    : theme_(theme) {
    content_ = std::make_shared<TextProcessorsModalContent>(
        theme_,
        std::move(target_text_provider),
        std::move(replace_target_text),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Apply", [this] {
            if (content_) {
                content_->ApplySelected();
            }
        }},
        {"Reload", [this] {
            if (content_) {
                content_->Reload();
            }
        }},
        {"Pin", [this] {
            if (content_) {
                content_->TogglePinned();
            }
        }},
        {"Close", [this] { Close(); }},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component TextProcessorsModal::View() const {
    return modal_;
}

void TextProcessorsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void TextProcessorsModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool TextProcessorsModal::IsOpen() const {
    return open_;
}

bool TextProcessorsModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        Close();
        return true;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
