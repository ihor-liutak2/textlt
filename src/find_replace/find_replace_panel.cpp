#include "find_replace/find_replace_panel.hpp"

#include "editor/clipboard_controller.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

#include "editor_utils.hpp"
#include "ftxui/component/mouse.hpp"
#include "keyboard_shortcuts.hpp"

namespace textlt {
namespace {

constexpr int kPanelWidth = 28;
constexpr int kPanelInnerWidth = kPanelWidth - 2;
constexpr int kFieldColumns = kPanelInnerWidth - 2;
constexpr int kResultPreviewColumns = kPanelInnerWidth - 2;
constexpr int kResultRowHeight = 2;
constexpr int kDoubleClickMs = 500;

bool IsPrimaryPress(ftxui::Event event) {
    return event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed;
}

bool IsWheel(ftxui::Event event) {
    return event.is_mouse() &&
        (event.mouse().button == ftxui::Mouse::WheelUp ||
         event.mouse().button == ftxui::Mouse::WheelDown);
}

bool IsBackspace(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace || event.input() == "\x7f" ||
        event.input() == "\x08";
}

bool IsDelete(const ftxui::Event& event) {
    return event == ftxui::Event::Delete || event.input() == "\x1B[3~";
}

std::string WindowedText(const std::string& value, std::size_t cursor,
    std::size_t columns, std::size_t& shown_cursor) {
    cursor = std::min(cursor, value.size());
    const std::size_t cursor_column =
        utils::Utf8DisplayWidth(value, 0, cursor);
    const std::size_t start_column =
        cursor_column > columns ? cursor_column - columns : 0;
    const std::size_t start = utils::Utf8ByteIndexAtDisplayColumn(
        value, 0, start_column);
    const std::size_t end = utils::Utf8ByteIndexAtDisplayColumn(
        value, start, columns + 1);
    shown_cursor = cursor >= start ? std::min(cursor - start, end - start) : 0;
    return value.substr(start, end - start);
}

// Shortens a match preview around the match, keeping highlight offsets aligned.
std::string TrimPreview(const std::string& preview, std::size_t match_start,
    std::size_t match_length, std::size_t columns, std::size_t& shown_start,
    std::size_t& shown_length) {
    match_start = std::min(match_start, preview.size());
    match_length = std::min(match_length, preview.size() - match_start);
    const std::size_t match_column =
        utils::Utf8DisplayWidth(preview, 0, match_start);
    const std::size_t match_width =
        utils::Utf8DisplayWidth(preview, match_start, match_start + match_length);
    const std::size_t left_budget = columns > match_width
        ? (columns - match_width) / 3 : 0;
    const std::size_t start_column =
        match_column > left_budget ? match_column - left_budget : 0;
    const std::size_t start = utils::Utf8ByteIndexAtDisplayColumn(
        preview, 0, start_column);
    const std::size_t end = utils::Utf8ByteIndexAtDisplayColumn(
        preview, start, columns);
    const std::string shown = preview.substr(start, end - start);
    shown_start = match_start >= start ? match_start - start : 0;
    if (shown_start > shown.size()) shown_start = shown.size();
    shown_length = std::min(shown.size() - shown_start, match_length);
    return shown;
}

ftxui::Element HighlightedPreview(const Theme& theme, const std::string& preview,
    std::size_t match_start, std::size_t match_length, bool active,
    const ftxui::Color& base_color) {
    using namespace ftxui;
    std::size_t shown_start = 0;
    std::size_t shown_length = 0;
    const std::string shown = TrimPreview(preview, match_start, match_length,
        kResultPreviewColumns - 1, shown_start, shown_length);
    if (shown_length == 0 || shown_start > shown.size()) {
        return text(shown) | color(base_color);
    }
    Elements parts;
    if (shown_start > 0) {
        parts.push_back(text(shown.substr(0, shown_start)) | color(base_color));
    }
    parts.push_back(text(shown.substr(shown_start, shown_length)) |
        bgcolor(active ? theme.active_match_bg : theme.match_bg) |
        color(theme.selection_fg));
    const std::size_t after = shown_start + shown_length;
    if (after < shown.size()) {
        parts.push_back(text(shown.substr(after)) | color(base_color));
    }
    return hbox(std::move(parts));
}

ftxui::Element FieldElement(const Theme& theme, const std::string& value,
    std::size_t cursor, bool focused, bool select_all, const char* placeholder) {
    using namespace ftxui;
    const ftxui::Color border = focused ? theme.modal_accent : theme.gutter;
    if (value.empty() && !focused) {
        return text(std::string(" ") + placeholder) | color(theme.gutter) |
            borderStyled(LIGHT, border);
    }
    if (select_all) {
        return text(" " + value) | bgcolor(theme.selection_bg) |
            color(theme.selection_fg) | borderStyled(LIGHT, border);
    }
    std::size_t shown_cursor = 0;
    const std::string shown = WindowedText(value, cursor, kFieldColumns, shown_cursor);
    Elements parts;
    if (shown_cursor > 0) {
        parts.push_back(text(" " + shown.substr(0, shown_cursor)));
    } else {
        parts.push_back(text(" "));
    }
    if (focused) {
        const std::size_t glyph_end =
            utils::NextUtf8CodepointStart(shown, shown_cursor);
        parts.push_back(text(shown_cursor < shown.size()
                ? shown.substr(shown_cursor, glyph_end - shown_cursor) : " ")
            | inverted);
        if (shown_cursor < shown.size()) {
            parts.push_back(text(shown.substr(glyph_end)));
        }
    } else {
        parts.push_back(text(shown.substr(shown_cursor)));
    }
    return hbox(std::move(parts)) | borderStyled(LIGHT, border);
}

} // namespace

FindReplacePanel::FindReplacePanel(const Theme* theme, StatusCallback status,
    RedrawCallback redraw, CloseCallback close)
    : theme_(theme), status_(std::move(status)), redraw_(std::move(redraw)),
      close_(std::move(close)) {}

void FindReplacePanel::SetTarget(FindTarget* target) {
    if (target_ == target) return;
    target_ = target;
    have_snapshot_ = false;
    activated_ = false;
    if (open_) Refresh();
}

void FindReplacePanel::Open(FindReplaceTab tab, std::optional<std::string> query) {
    if (query) {
        query_ = std::move(*query);
        query_cursor_ = query_.size();
        query_select_all_ = false;
        selected_ = 0;
        result_scroll_ = 0;
    }
    open_ = true;
    focused_ = true;
    tab_ = tab;
    focus_ = Field::Query;
    have_snapshot_ = false;
    activated_ = false;
    Refresh();
}

void FindReplacePanel::Close() {
    if (!open_) return;
    open_ = false;
    focused_ = false;
    if (target_) target_->ClearHighlights();
    results_.clear();
    truncated_ = false;
    selected_ = 0;
    result_scroll_ = 0;
    activated_ = false;
}

void FindReplacePanel::SetFocused(bool focused) {
    focused_ = focused;
    if (focused) {
        if (focus_ == Field::Results && results_.empty()) focus_ = Field::Query;
    }
}

bool FindReplacePanel::Contains(int x, int y) const {
    return panel_box_.x_min <= panel_box_.x_max &&
        panel_box_.y_min <= panel_box_.y_max &&
        panel_box_.Contain(x, y);
}

void FindReplacePanel::Refresh() {
    const std::size_t previous_selection = selected_;
    results_.clear();
    truncated_ = false;
    selected_ = 0;
    result_scroll_ = 0;
    activated_ = false;
    if (!target_) return;
    known_available_ = target_->IsAvailable();
    known_version_ = target_->Version();
    have_snapshot_ = true;
    if (!known_available_) {
        target_->ClearHighlights();
        return;
    }
    if (query_.empty()) {
        target_->ClearHighlights();
        return;
    }
    results_ = target_->Find(query_, options_, kFindResultLimit, truncated_);
    if (results_.empty()) {
        selected_ = 0;
        return;
    }
    selected_ = std::min(previous_selection, results_.size() - 1);
    EnsureSelectedVisible();
}

void FindReplacePanel::Tick() {
    if (!open_ || !target_) return;
    const bool available = target_->IsAvailable();
    const std::uint64_t version = target_->Version();
    if (!have_snapshot_ || available != known_available_ || version != known_version_) {
        Refresh();
    }
}

void FindReplacePanel::Report(const std::string& message) {
    if (status_) status_(message);
}

void FindReplacePanel::RequestRedraw() {
    if (redraw_) redraw_();
}

std::vector<FindReplacePanel::Field> FindReplacePanel::FocusOrder() const {
    std::vector<Field> order = {
        Field::FindTab,
        Field::ReplaceTab,
        Field::MatchCase,
        Field::WholeWord,
        Field::Query,
        Field::Clear,
        Field::Previous,
        Field::Next,
    };
    if (tab_ == FindReplaceTab::Replace) {
        order.push_back(Field::ReplaceInput);
        order.push_back(Field::Replace);
        order.push_back(Field::ReplaceAll);
    }
    order.push_back(Field::Results);
    return order;
}

void FindReplacePanel::CycleFocus(int direction) {
    const std::vector<Field> order = FocusOrder();
    if (order.empty()) return;
    const auto found = std::find(order.begin(), order.end(), focus_);
    int index = found == order.end() ? 0 : static_cast<int>(found - order.begin());
    index = (index + direction + static_cast<int>(order.size())) %
        static_cast<int>(order.size());
    FocusField(order[static_cast<std::size_t>(index)]);
}

void FindReplacePanel::FocusField(Field field) {
    focus_ = field;
    if (field == Field::ReplaceInput && tab_ != FindReplaceTab::Replace) {
        tab_ = FindReplaceTab::Replace;
    }
}

void FindReplacePanel::OnQueryChanged() {
    query_cursor_ = std::min(query_cursor_, query_.size());
    Refresh();
    if (results_.empty()) {
        RequestRedraw();
    } else {
        ActivateSelected();
    }
}

void FindReplacePanel::OnReplacementChanged() {
    replacement_cursor_ = std::min(replacement_cursor_, replacement_.size());
    RequestRedraw();
}

void FindReplacePanel::EnsureSelectedVisible() {
    if (results_.empty()) {
        selected_ = 0;
        result_scroll_ = 0;
        return;
    }
    selected_ = std::min(selected_, results_.size() - 1);
    const std::size_t page = std::max<std::size_t>(1, result_visible_rows_);
    if (selected_ < result_scroll_) {
        result_scroll_ = selected_;
    } else if (selected_ >= result_scroll_ + page) {
        result_scroll_ = selected_ + 1 - page;
    }
    const std::size_t max_scroll =
        results_.size() > page ? results_.size() - page : 0;
    result_scroll_ = std::min(result_scroll_, max_scroll);
}

void FindReplacePanel::ActivateSelected() {
    if (!target_ || !target_->IsAvailable() || results_.empty()) return;
    selected_ = std::min(selected_, results_.size() - 1);
    target_->Activate(selected_);
    activated_ = true;
    EnsureSelectedVisible();
    RequestRedraw();
}

void FindReplacePanel::MoveSelection(int delta, bool activate) {
    if (results_.empty()) return;
    int next = static_cast<int>(selected_) + delta;
    if (!activated_ && delta > 0) next = static_cast<int>(selected_);
    const int count = static_cast<int>(results_.size());
    next = (next % count + count) % count;
    selected_ = static_cast<std::size_t>(next);
    EnsureSelectedVisible();
    if (activate) ActivateSelected();
    RequestRedraw();
}

void FindReplacePanel::ClearQuery() {
    query_.clear();
    query_cursor_ = 0;
    query_select_all_ = false;
    Refresh();
    focus_ = Field::Query;
    RequestRedraw();
}

void FindReplacePanel::ToggleMatchCase() {
    options_.match_case = !options_.match_case;
    Refresh();
    RequestRedraw();
}

void FindReplacePanel::ToggleWholeWord() {
    options_.whole_word = !options_.whole_word;
    Refresh();
    RequestRedraw();
}

void FindReplacePanel::ReplaceSelected() {
    if (!target_) return;
    if (!target_->IsAvailable()) {
        Report(target_->UnavailableMessage());
        return;
    }
    if (!target_->SupportsReplace()) {
        Report("Replace is unavailable for this target");
        return;
    }
    if (results_.empty()) {
        Report("No match to replace");
        return;
    }
    const std::size_t index = std::min(selected_, results_.size() - 1);
    std::string error;
    if (!target_->ReplaceAt(index, replacement_, error)) {
        Report(error.empty() ? "Could not replace match" : error);
        return;
    }
    Refresh();
    activated_ = false;
    Report("Replaced 1 match");
    RequestRedraw();
}

void FindReplacePanel::ReplaceAll() {
    if (!target_) return;
    if (!target_->IsAvailable()) {
        Report(target_->UnavailableMessage());
        return;
    }
    if (!target_->SupportsReplace()) {
        Report("Replace is unavailable for this target");
        return;
    }
    if (query_.empty()) {
        Report("Enter text to replace");
        return;
    }
    std::size_t count = 0;
    std::string error;
    if (!target_->ReplaceAll(query_, replacement_, options_, count, error)) {
        Report(error.empty() ? "Could not replace matches" : error);
        return;
    }
    Refresh();
    activated_ = false;
    Report(count == 0 ? "No matches to replace"
                      : "Replaced " + std::to_string(count) + " matches");
    RequestRedraw();
}

bool FindReplacePanel::HandleFieldKey(ftxui::Event event, std::string& value,
    std::size_t& cursor, bool& select_all, bool query_field) {
    cursor = std::min(cursor, value.size());
    const auto changed = [&] {
        if (query_field) OnQueryChanged();
        else OnReplacementChanged();
    };
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'a')) {
        select_all = true;
        cursor = value.size();
        return true;
    }
    if (IsBackspace(event)) {
        if (select_all) {
            value.clear();
            cursor = 0;
            select_all = false;
        } else if (cursor > 0) {
            const std::size_t previous =
                utils::PreviousUtf8CodepointStart(value, cursor);
            value.erase(previous, cursor - previous);
            cursor = previous;
        }
        changed();
        return true;
    }
    if (IsDelete(event)) {
        if (select_all) {
            value.clear();
            cursor = 0;
            select_all = false;
        } else if (cursor < value.size()) {
            const std::size_t next =
                utils::NextUtf8CodepointStart(value, cursor);
            value.erase(cursor, next - cursor);
        }
        changed();
        return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
        select_all = false;
        if (cursor > 0) cursor = utils::PreviousUtf8CodepointStart(value, cursor);
        return true;
    }
    if (event == ftxui::Event::ArrowRight) {
        select_all = false;
        if (cursor < value.size()) cursor = utils::NextUtf8CodepointStart(value, cursor);
        return true;
    }
    if (event == ftxui::Event::Home) {
        select_all = false;
        cursor = 0;
        return true;
    }
    if (event == ftxui::Event::End) {
        select_all = false;
        cursor = value.size();
        return true;
    }
    const auto pasted = ClipboardController::PastedText(event, false);
    if (pasted || event.is_character()) {
        const std::string input = pasted ? *pasted : event.input();
        if (input.empty() || static_cast<unsigned char>(input[0]) < 0x20) {
            return false;
        }
        if (select_all) {
            value.clear();
            cursor = 0;
            select_all = false;
        }
        value.insert(cursor, input);
        cursor += input.size();
        changed();
        return true;
    }
    return false;
}

bool FindReplacePanel::HandleKey(ftxui::Event event) {
    if (event == ftxui::Event::F1) return false;
    if (event == ftxui::Event::Escape) {
        if (close_) close_();
        return true;
    }
    if (event == ftxui::Event::Tab) {
        CycleFocus(1);
        RequestRedraw();
        return true;
    }
    if (event == ftxui::Event::TabReverse || event.input() == "Shift+Tab" ||
        event.input() == "\x1B[Z") {
        CycleFocus(-1);
        RequestRedraw();
        return true;
    }
    if (event == ftxui::Event::F3) {
        MoveSelection(1);
        return true;
    }

    const bool query_field = focus_ == Field::Query;
    const bool replacement_field = focus_ == Field::ReplaceInput;
    if (query_field) {
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Return) {
            if (!results_.empty()) {
                focus_ = Field::Results;
                ActivateSelected();
            }
            return true;
        }
        if (HandleFieldKey(std::move(event), query_, query_cursor_,
                query_select_all_, true)) {
            return true;
        }
        return true;
    }
    if (replacement_field) {
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Return) {
            focus_ = Field::Replace;
            return true;
        }
        if (HandleFieldKey(std::move(event), replacement_, replacement_cursor_,
                replacement_select_all_, false)) {
            return true;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
        MoveSelection(event == ftxui::Event::ArrowUp ? -1 : 1);
        return true;
    }
    if (event == ftxui::Event::Return || event.is_character()) {
        const bool space = event.input() == " ";
        switch (focus_) {
            case Field::FindTab:
            case Field::ReplaceTab:
                CycleTab();
                return true;
            case Field::MatchCase:
                ToggleMatchCase();
                return true;
            case Field::WholeWord:
                ToggleWholeWord();
                return true;
            case Field::Clear:
                ClearQuery();
                return true;
            case Field::Previous:
                MoveSelection(-1);
                return true;
            case Field::Next:
                MoveSelection(1);
                return true;
            case Field::Replace:
                ReplaceSelected();
                return true;
            case Field::ReplaceAll:
                ReplaceAll();
                return true;
            case Field::Results:
                ActivateSelected();
                return true;
            default:
                if (space) return true;
                break;
        }
    }
    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
        if (focus_ == Field::FindTab || focus_ == Field::ReplaceTab) {
            CycleTab();
            return true;
        }
    }
    return true;
}

void FindReplacePanel::CycleTab() {
    tab_ = tab_ == FindReplaceTab::Find
        ? FindReplaceTab::Replace : FindReplaceTab::Find;
    if (tab_ == FindReplaceTab::Find && focus_ == Field::ReplaceInput) {
        focus_ = Field::Query;
    }
    RequestRedraw();
}

void FindReplacePanel::HandleMouse(ftxui::Event event) {
    const auto& mouse = event.mouse();
    if (IsWheel(event)) {
        if (results_view_box_.Contain(mouse.x, mouse.y)) {
            const int delta = mouse.button == ftxui::Mouse::WheelUp ? -3 : 3;
            int next = static_cast<int>(result_scroll_) + delta;
            const std::size_t page = std::max<std::size_t>(1, result_visible_rows_);
            const std::size_t max_scroll =
                results_.size() > page ? results_.size() - page : 0;
            next = std::clamp(next, 0, static_cast<int>(max_scroll));
            result_scroll_ = static_cast<std::size_t>(next);
            RequestRedraw();
        }
        return;
    }
    if (!IsPrimaryPress(event)) return;

    if (find_tab_box_.Contain(mouse.x, mouse.y)) {
        tab_ = FindReplaceTab::Find;
        focus_ = Field::FindTab;
        RequestRedraw();
        return;
    }
    if (replace_tab_box_.Contain(mouse.x, mouse.y)) {
        tab_ = FindReplaceTab::Replace;
        focus_ = Field::ReplaceTab;
        RequestRedraw();
        return;
    }
    if (match_case_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::MatchCase;
        ToggleMatchCase();
        return;
    }
    if (whole_word_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::WholeWord;
        ToggleWholeWord();
        return;
    }
    if (query_box_.Contain(mouse.x, mouse.y)) {
        const auto now = std::chrono::steady_clock::now();
        const bool double_click = last_clicked_field_ == Field::Query &&
            now - last_field_click_ <= std::chrono::milliseconds(kDoubleClickMs);
        last_clicked_field_ = Field::Query;
        last_field_click_ = now;
        focus_ = Field::Query;
        if (double_click) {
            query_select_all_ = true;
            query_cursor_ = query_.size();
        } else {
            query_select_all_ = false;
            query_cursor_ = query_.size();
        }
        RequestRedraw();
        return;
    }
    if (clear_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::Clear;
        ClearQuery();
        return;
    }
    if (previous_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::Previous;
        MoveSelection(-1);
        return;
    }
    if (next_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::Next;
        MoveSelection(1);
        return;
    }
    if (replacement_box_.Contain(mouse.x, mouse.y)) {
        const auto now = std::chrono::steady_clock::now();
        const bool double_click = last_clicked_field_ == Field::ReplaceInput &&
            now - last_field_click_ <= std::chrono::milliseconds(kDoubleClickMs);
        last_clicked_field_ = Field::ReplaceInput;
        last_field_click_ = now;
        focus_ = Field::ReplaceInput;
        if (double_click) {
            replacement_select_all_ = true;
            replacement_cursor_ = replacement_.size();
        } else {
            replacement_select_all_ = false;
            replacement_cursor_ = replacement_.size();
        }
        RequestRedraw();
        return;
    }
    if (replace_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::Replace;
        ReplaceSelected();
        return;
    }
    if (replace_all_box_.Contain(mouse.x, mouse.y)) {
        focus_ = Field::ReplaceAll;
        ReplaceAll();
        return;
    }
    for (std::size_t i = 0; i < result_row_boxes_.size(); ++i) {
        if (result_row_boxes_[i].Contain(mouse.x, mouse.y)) {
            selected_ = result_slice_start_ + i;
            EnsureSelectedVisible();
            focus_ = Field::Results;
            ActivateSelected();
            return;
        }
    }
}

bool FindReplacePanel::OnEvent(ftxui::Event event) {
    if (!open_) return false;
    if (event.is_mouse()) {
        if (!Contains(event.mouse().x, event.mouse().y)) return false;
        focused_ = true;
        HandleMouse(std::move(event));
        return true;
    }
    return HandleKey(std::move(event));
}

ftxui::Element FindReplacePanel::RenderTabs() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto tab = [&](const char* label, FindReplaceTab value, ftxui::Box& box) {
        const bool selected = tab_ == value;
        Element element = text(std::string(" ") + label + " ") |
            color(selected ? theme.button_selected_fg : theme.modal_text_color);
        if (selected) {
            element = element | bgcolor(theme.button_selected_bg) | bold;
        }
        if (focused_ && focus_ == (value == FindReplaceTab::Find
                ? Field::FindTab : Field::ReplaceTab)) {
            element = element | bold;
        }
        return element | reflect(box);
    };
    return hbox({
        tab("Find", FindReplaceTab::Find, find_tab_box_),
        text(" "),
        tab("Replace", FindReplaceTab::Replace, replace_tab_box_),
    });
}

ftxui::Element FindReplacePanel::RenderOption(const std::string& label,
    bool selected, Field field) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Element element = text(std::string(" ") + (selected ? "[x] " : "[ ] ") + label);
    if (selected) {
        element = element | bgcolor(theme.button_selected_bg) |
            color(theme.button_selected_fg);
    } else {
        element = element | color(theme.modal_text_color);
    }
    if (focused_ && focus_ == field) {
        element = element | bold;
    }
    return element;
}

ftxui::Element FindReplacePanel::RenderOptions() {
    using namespace ftxui;
    return vbox({
        hbox({RenderOption("Match Case", options_.match_case, Field::MatchCase) |
                  reflect(match_case_box_),
              filler()}),
        hbox({RenderOption("Whole Word", options_.whole_word, Field::WholeWord) |
                  reflect(whole_word_box_),
              filler()}),
    });
}

ftxui::Element FindReplacePanel::RenderQueryField() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return FieldElement(theme, query_, query_cursor_,
        focused_ && focus_ == Field::Query, query_select_all_, "Find text") |
        xflex | reflect(query_box_);
}

ftxui::Element FindReplacePanel::RenderReplacementField(const char* placeholder) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return FieldElement(theme, replacement_, replacement_cursor_,
        focused_ && focus_ == Field::ReplaceInput, replacement_select_all_,
        placeholder) | xflex | reflect(replacement_box_);
}

ftxui::Element FindReplacePanel::RenderButton(const std::string& label,
    Field field, bool enabled) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Element element = text(" " + label + " ") |
        color(enabled ? theme.modal_text_color : theme.button_disabled_fg);
    if (focused_ && focus_ == field) {
        element = element | bgcolor(theme.button_focused_bg) |
            color(theme.button_focused_fg) | bold;
    }
    return element;
}

ftxui::Element FindReplacePanel::RenderResults() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    result_row_boxes_.clear();
    if (!target_) {
        rows.push_back(text(" No search target") | dim | color(theme.gutter));
    } else if (!target_->IsAvailable()) {
        rows.push_back(text(" " + target_->UnavailableMessage()) |
            color(theme.modal_text_color) | xflex);
    } else if (query_.empty()) {
        rows.push_back(text(" Type to search") | dim | color(theme.gutter));
    } else if (results_.empty()) {
        rows.push_back(text(" No matches") | dim | color(theme.gutter));
    } else {
        EnsureSelectedVisible();
        const std::size_t page = std::max<std::size_t>(1, result_visible_rows_);
        const std::size_t start = std::min(result_scroll_, results_.size() - 1);
        const std::size_t end = std::min(results_.size(), start + page + 2);
        result_slice_start_ = start;
        result_row_boxes_.assign(end - start, {});
        for (std::size_t index = start; index < end; ++index) {
            const FindResult& result = results_[index];
            const bool selected = index == selected_;
            const bool active = selected && activated_;
            const ftxui::Color text_color = selected
                ? theme.modal_selected_item_fg : theme.modal_text_color;
            Element label = text(" " + result.label) |
                color(selected ? theme.modal_selected_item_fg : theme.gutter) |
                dim;
            Element preview = hbox({
                text(" "),
                HighlightedPreview(theme, result.preview,
                    result.preview_match_start, result.preview_match_length,
                    active, text_color),
                filler(),
            });
            Element row = vbox({std::move(label), std::move(preview)}) |
                reflect(result_row_boxes_[index - start]);
            if (selected) {
                row = row | bgcolor(theme.modal_selected_item_bg) |
                    color(theme.modal_selected_item_fg);
            }
            rows.push_back(std::move(row));
        }
    }
    rows.push_back(filler());
    Element body = vbox(std::move(rows)) | yframe | yflex |
        reflect(results_view_box_);
    return body;
}

ftxui::Element FindReplacePanel::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    const bool replace_tab = tab_ == FindReplaceTab::Replace;
    const bool target_available = target_ && target_->IsAvailable();
    const bool replace_enabled = target_available && target_->SupportsReplace();

    std::string count_label;
    if (!query_.empty() && target_available) {
        count_label = std::to_string(results_.size());
        if (truncated_) count_label += "+";
        count_label += results_.size() == 1 ? " match" : " matches";
    }

    Elements layout;
    layout.push_back(RenderTabs());
    layout.push_back(separator() | color(theme.gutter));
    layout.push_back(RenderOptions());
    layout.push_back(separator() | color(theme.gutter));
    layout.push_back(text(" Find") | bold | color(theme.modal_accent));
    layout.push_back(RenderQueryField());
    layout.push_back(hbox({
        RenderButton("Clear", Field::Clear) | reflect(clear_box_),
        text(" "),
        RenderButton("Prev", Field::Previous) | reflect(previous_box_),
        text(" "),
        RenderButton("Next", Field::Next) | reflect(next_box_),
        filler(),
    }));

    if (replace_tab) {
        layout.push_back(separator() | color(theme.gutter));
        layout.push_back(text(" Replace") | bold | color(theme.modal_accent));
        layout.push_back(RenderReplacementField("Replacement"));
        layout.push_back(hbox({
            RenderButton("Replace", Field::Replace, replace_enabled) |
                reflect(replace_box_),
            text(" "),
            RenderButton("All", Field::ReplaceAll, replace_enabled) |
                reflect(replace_all_box_),
            filler(),
        }));
    }

    layout.push_back(separator() | color(theme.gutter));
    layout.push_back(hbox({
        text(" " + count_label) | dim | color(theme.gutter) | xflex,
        truncated_ ? text("100+ ") | color(theme.syntax_keyword) : text(""),
    }));
    layout.push_back(RenderResults() | yflex);

    return vbox(std::move(layout)) | bgcolor(theme.background) |
        borderStyled(LIGHT, theme.gutter) | reflect(panel_box_) | yflex;
}

} // namespace textlt
