#include "distraction_mode_controller.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "editor/document_session.hpp"
#include "editor/document_workspace.hpp"
#include "editor_config.hpp"

namespace textlt {
namespace {

constexpr int kMinColumnWidth = 40;
constexpr int kMaxColumnWidth = 180;
constexpr int kMinColumnGap = 0;
constexpr int kMaxColumnGap = 24;
constexpr int kMinPadding = 0;
constexpr int kMaxPadding = 12;

size_t LineCount(const DocumentSession* session) {
    if (!session || session->LineCount() == 0) {
        return 1;
    }
    return session->LineCount();
}

std::string PageInputFor(size_t page) {
    return std::to_string(std::max<size_t>(page, 1));
}

size_t ParsePageInput(const std::string& value) {
    size_t page = 0;
    bool has_digit = false;
    for (unsigned char character : value) {
        if (!std::isdigit(character)) {
            continue;
        }
        has_digit = true;
        const size_t digit = static_cast<size_t>(character - '0');
        constexpr size_t kMaxSafePage = 1000000;
        if (page > (kMaxSafePage - digit) / 10) {
            page = kMaxSafePage;
        } else {
            page = page * 10 + digit;
        }
    }
    return has_digit ? std::max<size_t>(1, page) : 1;
}

} // namespace

DistractionModeController::DistractionModeController(DistractionModeSettings settings) {
    SetSettings(settings);
}

const DistractionModeSettings& DistractionModeController::Settings() const {
    return settings_;
}

void DistractionModeController::SetSettings(DistractionModeSettings settings) {
    settings_ = NormalizeSettings(settings);
}

bool DistractionModeController::Enabled() const {
    return settings_.enabled;
}

void DistractionModeController::SetEnabled(bool enabled) {
    settings_.enabled = enabled;
}

void DistractionModeController::Enter() {
    SetEnabled(true);
}

void DistractionModeController::Exit() {
    SetEnabled(false);
}

bool DistractionModeController::Toggle() {
    SetEnabled(!Enabled());
    return Enabled();
}

void DistractionModeController::SetColumnCount(int column_count) {
    settings_.column_count = column_count == 2 ? 2 : 1;
    settings_.column_width = settings_.column_count == 2 ? 72 : 92;
    settings_ = NormalizeSettings(settings_);
}

void DistractionModeController::SetPageInput(std::string input) {
    page_input_ = PageInputFor(ParsePageInput(input));
}

const std::string& DistractionModeController::PageInput() const {
    return page_input_;
}

EditorViewportOptions DistractionModeController::ApplyToViewportOptions(EditorViewportOptions options) const {
    if (!Enabled()) {
        return options;
    }

    options.distraction_mode = true;
    options.center_text = true;
    options.show_line_numbers = false;
    options.show_scrollbar = false;
    options.max_text_width = static_cast<size_t>(settings_.column_width);
    options.column_gap = static_cast<size_t>(settings_.column_gap);
    options.top_padding = static_cast<size_t>(settings_.top_padding);
    options.bottom_padding = static_cast<size_t>(settings_.bottom_padding);
    return options;
}

DistractionTopBarState DistractionModeController::TopBarState(
    const EditorViewport* viewport,
    const DocumentSession* session,
    const EditorConfig* config) const {
    DistractionTopBarState state;
    state.enabled = Enabled();
    state.total_pages = TotalPages(viewport, session, config);
    state.current_page = std::min(CurrentPage(viewport, session, config), state.total_pages);
    state.page_input = PageInputFor(state.current_page);
    return state;
}

DistractionTopBarState DistractionModeController::TopBarState(
    const DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) const {
    DistractionTopBarState state;
    state.enabled = Enabled();
    state.total_pages = TotalPages(workspace, visible_pane_count, config);
    state.current_page = std::min(CurrentPage(workspace, visible_pane_count, config), state.total_pages);
    state.page_input = PageInputFor(state.current_page);
    return state;
}

bool DistractionModeController::NextPage(
    EditorViewport* viewport,
    DocumentSession* session,
    const EditorConfig* config) {
    return SetPage(viewport, session, config, CurrentPage(viewport, session, config) + 1);
}

bool DistractionModeController::PreviousPage(
    EditorViewport* viewport,
    DocumentSession* session,
    const EditorConfig* config) {
    const size_t current_page = CurrentPage(viewport, session, config);
    return SetPage(viewport, session, config, current_page > 1 ? current_page - 1 : 1);
}

bool DistractionModeController::GoToPage(
    EditorViewport* viewport,
    DocumentSession* session,
    const EditorConfig* config) {
    return SetPage(viewport, session, config, ParsePageInput(page_input_));
}

bool DistractionModeController::NextPage(
    DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) {
    return SetPage(workspace, visible_pane_count, config,
        CurrentPage(workspace, visible_pane_count, config) + 1);
}

bool DistractionModeController::PreviousPage(
    DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) {
    const size_t current_page = CurrentPage(workspace, visible_pane_count, config);
    return SetPage(workspace, visible_pane_count, config, current_page > 1 ? current_page - 1 : 1);
}

bool DistractionModeController::GoToPage(
    DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) {
    return SetPage(workspace, visible_pane_count, config, ParsePageInput(page_input_));
}

bool DistractionModeController::SyncPageColumns(
    DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) {
    if (!Enabled() || !workspace) {
        return false;
    }
    return SetPage(workspace, visible_pane_count, config,
        CurrentPage(workspace, visible_pane_count, config));
}

DistractionModeSettings DistractionModeController::NormalizeSettings(DistractionModeSettings settings) {
    settings.column_count = settings.column_count == 2 ? 2 : 1;
    settings.column_width = std::clamp(settings.column_width, kMinColumnWidth, kMaxColumnWidth);
    settings.column_gap = std::clamp(settings.column_gap, kMinColumnGap, kMaxColumnGap);
    settings.top_padding = std::clamp(settings.top_padding, kMinPadding, kMaxPadding);
    settings.bottom_padding = std::clamp(settings.bottom_padding, kMinPadding, kMaxPadding);
    return settings;
}

size_t DistractionModeController::VisiblePageHeight(const EditorViewport* viewport) const {
    if (!viewport) {
        return 1;
    }
    return std::max<size_t>(viewport->VisibleHeight(), 1);
}

size_t DistractionModeController::EffectiveColumnCount(size_t visible_pane_count) const {
    if (!Enabled()) {
        return 1;
    }
    const size_t requested_columns = settings_.column_count == 2 ? 2 : 1;
    const size_t visible_columns = std::max<size_t>(visible_pane_count, 1);
    return std::max<size_t>(1, std::min(requested_columns, visible_columns));
}

size_t DistractionModeController::TotalPages(
    const EditorViewport* viewport,
    const DocumentSession* session,
    const EditorConfig* /*config*/) const {
    const size_t visible_height = VisiblePageHeight(viewport);
    return std::max<size_t>(1, (LineCount(session) + visible_height - 1) / visible_height);
}

size_t DistractionModeController::CurrentPage(
    const EditorViewport* viewport,
    const DocumentSession* session,
    const EditorConfig* config) const {
    if (!viewport) {
        return 1;
    }
    const size_t visible_height = VisiblePageHeight(viewport);
    const size_t page = (viewport->scroll_y / visible_height) + 1;
    return std::min(page, TotalPages(viewport, session, config));
}

bool DistractionModeController::SetPage(
    EditorViewport* viewport,
    DocumentSession* session,
    const EditorConfig* config,
    size_t page) {
    if (!viewport || !session) {
        page_input_ = "1";
        return false;
    }

    const size_t visible_height = VisiblePageHeight(viewport);
    const size_t total_pages = TotalPages(viewport, session, config);
    page = std::clamp(page, size_t{1}, total_pages);

    const size_t line_count = LineCount(session);
    const size_t max_scroll_y = line_count > visible_height ? line_count - visible_height : 0;
    viewport->scroll_y = std::min((page - 1) * visible_height, max_scroll_y);
    viewport->scroll_x = 0;
    session->SetCursorPosition(viewport->scroll_y, 0);
    page_input_ = PageInputFor(page);
    return true;
}

size_t DistractionModeController::TotalPages(
    const DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* /*config*/) const {
    if (!workspace) {
        return 1;
    }

    const EditorViewport* lead_viewport = workspace->PaneViewport(0);
    const std::shared_ptr<DocumentSession> session = workspace->ActiveSessionPtr();
    const size_t visible_height = VisiblePageHeight(lead_viewport);
    const size_t text_pages = std::max<size_t>(1, (LineCount(session.get()) + visible_height - 1) / visible_height);
    const size_t columns = EffectiveColumnCount(visible_pane_count);
    return std::max<size_t>(1, (text_pages + columns - 1) / columns);
}

size_t DistractionModeController::CurrentPage(
    const DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config) const {
    if (!workspace) {
        return 1;
    }

    const EditorViewport* lead_viewport = workspace->PaneViewport(0);
    const std::shared_ptr<DocumentSession> session = workspace->ActiveSessionPtr();
    if (!lead_viewport || !session) {
        return 1;
    }

    const size_t visible_height = VisiblePageHeight(lead_viewport);
    const size_t text_page = (lead_viewport->scroll_y / visible_height) + 1;
    const size_t columns = EffectiveColumnCount(visible_pane_count);
    const size_t display_page = ((text_page - 1) / columns) + 1;
    return std::min(display_page, TotalPages(workspace, visible_pane_count, config));
}

bool DistractionModeController::SetPage(
    DocumentWorkspace* workspace,
    size_t visible_pane_count,
    const EditorConfig* config,
    size_t page) {
    if (!workspace) {
        page_input_ = "1";
        return false;
    }

    const std::shared_ptr<DocumentSession> session = workspace->ActiveSessionPtr();
    if (!session) {
        page_input_ = "1";
        return false;
    }

    const size_t columns = EffectiveColumnCount(visible_pane_count);
    EditorViewport* lead_viewport = workspace->PaneViewport(0);
    const size_t visible_height = VisiblePageHeight(lead_viewport);
    const size_t total_pages = TotalPages(workspace, visible_pane_count, config);
    page = std::clamp(page, size_t{1}, total_pages);

    const size_t line_count = LineCount(session.get());
    const size_t max_scroll_y = line_count > visible_height ? line_count - visible_height : 0;
    const size_t lead_text_page = (page - 1) * columns + 1;
    const size_t base_scroll_y = (lead_text_page - 1) * visible_height;

    for (size_t pane_index = 0; pane_index < columns; ++pane_index) {
        EditorViewport* viewport = workspace->PaneViewport(pane_index);
        if (!viewport) {
            continue;
        }
        const size_t pane_scroll_y = std::min(base_scroll_y + pane_index * visible_height, max_scroll_y);
        viewport->scroll_y = pane_scroll_y;
        viewport->scroll_x = 0;
        viewport->CursorState().cursor_row = pane_scroll_y;
        viewport->CursorState().cursor_col = 0;
        viewport->CursorState().selection.active = false;
    }

    const size_t active_pane = std::min(workspace->ActiveEditorPaneIndex(), columns - 1);
    const EditorViewport* active_viewport = workspace->PaneViewport(active_pane);
    session->SetCursorPosition(active_viewport ? active_viewport->scroll_y : base_scroll_y, 0);
    page_input_ = PageInputFor(page);
    return true;
}

} // namespace textlt
