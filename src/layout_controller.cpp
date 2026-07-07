#include "layout_controller.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "editor/document_session.hpp"
#include "distraction_mode_controller.hpp"

namespace textlt {

LayoutController::LayoutController(
    DocumentWorkspace& workspace,
    const DistractionModeController* distraction_controller)
    : workspace_(workspace), distraction_controller_(distraction_controller) {}

void LayoutController::SetDistractionModeController(
    const DistractionModeController* distraction_controller) {
    distraction_controller_ = distraction_controller;
}

LayoutController::EditorLayoutMode LayoutController::Mode() const {
    return mode_;
}

LayoutController::EditorLayoutMode LayoutController::EffectiveMode() const {
    if (distraction_controller_ && distraction_controller_->Enabled()) {
        return distraction_controller_->Settings().column_count == 2
            ? EditorLayoutMode::TwoColumns
            : EditorLayoutMode::Single;
    }
    return mode_;
}

void LayoutController::SetMode(EditorLayoutMode mode) {
    mode_ = mode;
    EnsureVisiblePanesHaveSessions();
}

void LayoutController::SetModeByIndex(int layout_index) {
    switch (layout_index) {
        case 1:
            SetMode(EditorLayoutMode::TwoColumns);
            break;
        case 2:
            SetMode(EditorLayoutMode::ThreeColumns);
            break;
        case 0:
        default:
            SetMode(EditorLayoutMode::Single);
            break;
    }
}

int LayoutController::ModeIndex() const {
    switch (mode_) {
        case EditorLayoutMode::TwoColumns: return 1;
        case EditorLayoutMode::ThreeColumns: return 2;
        case EditorLayoutMode::Single:
        default: return 0;
    }
}

std::string LayoutController::ModeLabel() const {
    return ModeLabel(mode_);
}

std::string LayoutController::ModeLabel(EditorLayoutMode mode) {
    switch (mode) {
        case EditorLayoutMode::TwoColumns: return "Two columns";
        case EditorLayoutMode::ThreeColumns: return "Three columns";
        case EditorLayoutMode::Single:
        default: return "Single column";
    }
}

size_t LayoutController::VisiblePaneCount() const {
    switch (EffectiveMode()) {
        case EditorLayoutMode::TwoColumns: return 2;
        case EditorLayoutMode::ThreeColumns: return 3;
        case EditorLayoutMode::Single:
        default: return 1;
    }
}

bool LayoutController::HasVisiblePane(size_t pane_index) const {
    return pane_index < VisiblePaneCount();
}

size_t LayoutController::ActivePaneIndex() const {
    return workspace_.ActiveEditorPaneIndex();
}

bool LayoutController::ActivatePane(size_t pane_index) {
    return workspace_.ActivateEditorPane(pane_index, VisiblePaneCount());
}

bool LayoutController::FocusNextPane() {
    const size_t visible_count = VisiblePaneCount();
    if (visible_count <= 1) {
        return false;
    }
    const size_t next_index = (workspace_.ActiveEditorPaneIndex() + 1) % visible_count;
    return ActivatePane(next_index);
}

bool LayoutController::FocusPreviousPane() {
    const size_t visible_count = VisiblePaneCount();
    if (visible_count <= 1) {
        return false;
    }
    const size_t current_index = workspace_.ActiveEditorPaneIndex();
    const size_t next_index = current_index == 0 ? visible_count - 1 : current_index - 1;
    return ActivatePane(next_index);
}

void LayoutController::EqualizePaneWidths() {
    two_left_width_ = 72;
    three_left_width_ = 48;
    three_right_width_ = 48;
}

int LayoutController::TwoLeftWidth() const {
    return two_left_width_;
}

int LayoutController::ThreeLeftWidth() const {
    return three_left_width_;
}

int LayoutController::ThreeRightWidth() const {
    return three_right_width_;
}

bool LayoutController::IsDistractionModeActive() const {
    return distraction_controller_ && distraction_controller_->Enabled();
}

void LayoutController::EnsureVisiblePanesHaveSessions() {
    const size_t visible_count = VisiblePaneCount();
    workspace_.ClampActiveEditorPaneIndex(visible_count);
    workspace_.EnsureEditorPanesHaveSessions(visible_count);

    if (!IsDistractionModeActive() || visible_count <= 1 || workspace_.SessionCount() == 0) {
        return;
    }

    const size_t session_index = workspace_.ActiveSessionIndex();
    for (size_t pane_index = 0; pane_index < visible_count; ++pane_index) {
        workspace_.AssignSessionToPane(pane_index, session_index);
    }
    workspace_.ActivateEditorPane(workspace_.ActiveEditorPaneIndex(), visible_count);
}

size_t LayoutController::ColumnGapAfterPane(size_t pane_index) const {
    if (!IsDistractionModeActive()) {
        return 0;
    }
    const size_t visible_count = VisiblePaneCount();
    if (visible_count <= 1 || pane_index + 1 >= visible_count) {
        return 0;
    }
    return static_cast<size_t>(std::max(0, distraction_controller_->Settings().column_gap));
}

EditorViewportOptions LayoutController::ViewportOptionsForPane(size_t pane_index) const {
    (void)pane_index;
    EditorViewportOptions options;
    if (distraction_controller_) {
        options = distraction_controller_->ApplyToViewportOptions(options);
    }
    return options;
}

ViewLayoutSnapshot LayoutController::Snapshot() const {
    ViewLayoutSnapshot snapshot;
    snapshot.layout_index = ModeIndex();
    snapshot.layout_name = IsDistractionModeActive()
        ? std::string("Distraction ") + (VisiblePaneCount() == 2 ? "2 columns" : "1 column")
        : ModeLabel();
    snapshot.active_pane_index = workspace_.ActiveEditorPaneIndex();
    snapshot.two_left_width = two_left_width_;
    snapshot.three_left_width = three_left_width_;
    snapshot.three_right_width = three_right_width_;

    const auto& sessions = workspace_.OpenSessions();
    for (size_t session_index = 0; session_index < sessions.size(); ++session_index) {
        const auto& session = sessions[session_index];
        if (!session) {
            continue;
        }

        ViewLayoutDocumentInfo info;
        info.title = ShortSessionTitle(session);
        info.path = session->path.string();
        info.dirty = session->is_dirty;
        info.memory_only = DocumentWorkspace::IsMemoryOnlySession(session.get());
        info.active = session_index == workspace_.ActiveSessionIndex();
        snapshot.documents.push_back(std::move(info));
    }

    const size_t visible_count = VisiblePaneCount();
    for (size_t pane_index = 0; pane_index < visible_count; ++pane_index) {
        ViewLayoutPaneInfo info;
        info.role = workspace_.PaneRole(pane_index);
        info.active = pane_index == workspace_.ActiveEditorPaneIndex();
        info.session_index = workspace_.PaneSessionIndex(pane_index);
        if (info.session_index < sessions.size()) {
            const auto& session = sessions[info.session_index];
            info.title = ShortSessionTitle(session);
            info.path = session ? session->path.string() : "";
        }
        if (info.title.empty()) {
            info.title = "Untitled";
        }
        snapshot.panes.push_back(std::move(info));
    }

    return snapshot;
}

std::string LayoutController::ShortSessionTitle(const std::shared_ptr<DocumentSession>& session) {
    std::string title = session ? session->DisplayTitle() : "Untitled";
    if (session && session->is_dirty) {
        title += " *";
    }
    return title;
}

} // namespace textlt
