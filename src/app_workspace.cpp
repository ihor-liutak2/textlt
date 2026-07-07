#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

namespace {

std::string DocumentTitle(const std::shared_ptr<DocumentSession>& doc) {
    return doc ? doc->DisplayTitle() : "Untitled";
}

std::string ShortDocumentTitle(const std::shared_ptr<DocumentSession>& doc) {
    std::string title = DocumentTitle(doc);
    if (doc && doc->is_dirty) {
        title += " *";
    }
    return title;
}

} // namespace

std::shared_ptr<EditorComponent> TextltApp::ActiveEditor() const {
    if (!text_editor_) {
        return nullptr;
    }
    return std::static_pointer_cast<EditorComponent>(text_editor_);
}

size_t TextltApp::VisibleEditorPaneCount() const {
    return layout_controller_.VisiblePaneCount();
}

std::string TextltApp::EditorLayoutModeLabel(EditorLayoutMode mode) const {
    return LayoutController::ModeLabel(mode);
}

int TextltApp::EditorLayoutModeIndex() const {
    return layout_controller_.ModeIndex();
}

void TextltApp::SetEditorLayoutMode(EditorLayoutMode mode) {
    layout_controller_.SetMode(mode);
    BindEditorComponentsToWorkspace();
    SetActiveEditorPane(document_workspace_.ActiveEditorPaneIndex());
    active_action_ = "View layout: " + layout_controller_.ModeLabel();
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::MainViewCanActivateEditorPane() const {
    return ActiveLayer() == UiLayer::Main &&
        (!menu_bar_ || !menu_bar_->IsDropdownOpen()) &&
        current_search_mode_ == SearchMode::None &&
        !show_goto_line_bar_ &&
        !help_dialog_.IsOpen() &&
        !recent_files_modal_.IsOpen() &&
        !search_files_modal_.IsOpen() &&
        !files_modal_.IsOpen() &&
        !text_processors_modal_.IsOpen() &&
        !remote_connections_modal_.IsOpen() &&
        !remote_files_modal_.IsOpen() &&
        !git_modal_.IsOpen() &&
        !git_settings_modal_.IsOpen() &&
        !tts_modal_.IsOpen() &&
        !view_layout_modal_.IsOpen() &&
        !distraction_options_modal_.IsOpen() &&
        !ai_actions_modal_.IsOpen() &&
        !assistant_settings_modal_.IsOpen() &&
        !theme_dialog_.IsOpen() &&
        !unsaved_changes_dialog_.IsOpen();
}

void TextltApp::SetActiveEditorPane(size_t pane_index) {
    if (editor_pane_components_.empty() || !layout_controller_.HasVisiblePane(pane_index)) {
        return;
    }

    const bool editor_layer_is_active = ActiveLayer() == UiLayer::Main;
    if (!layout_controller_.ActivatePane(pane_index)) {
        return;
    }
    BindEditorComponentsToWorkspace();
    sidebar_has_focus_ = false;

    const size_t active_pane = layout_controller_.ActivePaneIndex();
    if (active_pane < editor_pane_components_.size()) {
        text_editor_ = editor_pane_components_[active_pane];
    }
    if (editor_layer_is_active && text_editor_) {
        text_editor_->TakeFocus();
    }
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
}

void TextltApp::FocusNextEditorPane() {
    if (!layout_controller_.FocusNextPane()) {
        active_action_ = "Only one editor pane is visible";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    SetActiveEditorPane(layout_controller_.ActivePaneIndex());
    active_action_ = "Active pane: " + std::to_string(layout_controller_.ActivePaneIndex() + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::FocusPreviousEditorPane() {
    if (!layout_controller_.FocusPreviousPane()) {
        active_action_ = "Only one editor pane is visible";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    SetActiveEditorPane(layout_controller_.ActivePaneIndex());
    active_action_ = "Active pane: " + std::to_string(layout_controller_.ActivePaneIndex() + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::EqualizeEditorPaneWidths() {
    layout_controller_.EqualizePaneWidths();
    active_action_ = "Editor pane widths reset";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SetEditorPaneRole(size_t pane_index, const std::string& role) {
    if (!document_workspace_.SetPaneRole(pane_index, role)) {
        return;
    }
    active_action_ = "Pane " + std::to_string(pane_index + 1) + " role: " + document_workspace_.PaneRole(pane_index);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::AssignSessionToEditorPane(size_t pane_index, size_t session_index) {
    if (pane_index >= editor_pane_components_.size() ||
        !document_workspace_.AssignSessionToPane(pane_index, session_index)) {
        return;
    }

    BindEditorComponentsToWorkspace();
    if (pane_index == document_workspace_.ActiveEditorPaneIndex()) {
        text_editor_ = editor_pane_components_[pane_index];
    }

    active_action_ = "Pane " + std::to_string(pane_index + 1) + " document: " +
        ShortDocumentTitle(document_workspace_.SessionPtrAt(session_index));
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SplitActiveSessionToNextPane() {
    if (document_workspace_.Empty()) {
        EnsureOneOpenSession();
    }
    if (document_workspace_.Empty()) {
        return;
    }

    if (layout_controller_.VisiblePaneCount() <= 1) {
        SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
    }

    size_t source_pane = 0;
    size_t target_pane = 0;
    size_t session_index = 0;
    if (!document_workspace_.SplitActiveSessionToNextPane(
            layout_controller_.VisiblePaneCount(),
            source_pane,
            target_pane,
            session_index)) {
        active_action_ = "Could not split the active document";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    // This is a view split, not a file copy: both panes point at the same
    // shared session. Each pane owns its own EditorViewport, so the same file
    // can be shown at different scroll positions.
    BindEditorComponentsToWorkspace();
    SetActiveEditorPane(source_pane);

    const std::string title = ShortDocumentTitle(document_workspace_.SessionPtrAt(session_index));
    active_action_ = "Split " + title + " into pane " + std::to_string(target_pane + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::BindEditorComponentsToWorkspace() {
    document_workspace_.SetEditorPaneCount(editor_pane_components_.size());
    layout_controller_.EnsureVisiblePanesHaveSessions();

    for (size_t pane_index = 0; pane_index < editor_pane_components_.size(); ++pane_index) {
        const size_t session_index = document_workspace_.PaneSessionIndex(pane_index);
        auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
        if (editor) {
            if (auto* viewport = document_workspace_.PaneViewport(pane_index)) {
                viewport->SetOptions(layout_controller_.ViewportOptionsForPane(pane_index));
                editor->SetViewport(viewport);
            } else {
                editor->SetViewport(nullptr);
            }
            editor->SetSession(document_workspace_.SessionPtrAt(session_index));
        }
    }

    document_workspace_.ClampActiveEditorPaneIndex(layout_controller_.VisiblePaneCount());
    const size_t active_pane = document_workspace_.ActiveEditorPaneIndex();
    layout_controller_.ActivatePane(active_pane);
    if (active_pane < editor_pane_components_.size()) {
        text_editor_ = editor_pane_components_[active_pane];
    }
}

ftxui::Element TextltApp::RenderEditorPane(size_t pane_index) {
    using namespace ftxui;

    if (!layout_controller_.HasVisiblePane(pane_index) ||
        pane_index >= editor_pane_components_.size()) {
        return emptyElement() | size(WIDTH, EQUAL, 0);
    }

    const Theme& theme = current_theme_;
    auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
    std::shared_ptr<DocumentSession> doc = editor ? editor->GetSession() : nullptr;

    const std::string title = DocumentTitle(doc);
    const bool dirty = doc && doc->is_dirty;
    const std::string type_label = doc ? doc->Label() : "Plain Text";

    const bool active = !sidebar_has_focus_ && pane_index == document_workspace_.ActiveEditorPaneIndex();
    const std::string marker = active ? "●" : "○";
    std::string header_text = " " + marker + " " + std::to_string(pane_index + 1) + ": " + title;
    if (dirty) {
        header_text += " *";
    }
    if (doc && doc->read_only) {
        header_text += " [RO]";
    }
    header_text += " | " + type_label;

    Element header = hbox({
        text(header_text) | bold | color(active ? theme.modal_selected_item_fg : theme.menu_foreground),
        filler(),
        text(document_workspace_.PaneRole(pane_index) + " ") |
            dim |
            color(active ? theme.modal_selected_item_fg : theme.menu_foreground),
    }) |
        bgcolor(active ? theme.modal_selected_item_bg : theme.menu_background) |
        size(WIDTH, GREATER_THAN, 12);

    Element body = editor_pane_components_[pane_index]->Render() | flex;
    Element pane = layout_controller_.IsDistractionModeActive()
        ? body | bgcolor(theme.background) | xflex
        : vbox({
              header,
              separator() | color(active ? theme.modal_accent : theme.gutter),
              body | flex,
          }) |
              borderStyled(active ? HEAVY : LIGHT, active ? theme.modal_accent : theme.gutter) |
              bgcolor(theme.background) |
              xflex;

    const size_t column_gap = layout_controller_.ColumnGapAfterPane(pane_index);
    if (column_gap > 0) {
        return hbox({
            pane,
            text(std::string(column_gap, ' ')) | bgcolor(theme.background),
        }) | xflex;
    }

    return pane;
}

ViewLayoutSnapshot TextltApp::CurrentViewLayoutSnapshot() const {
    return layout_controller_.Snapshot();
}

} // namespace textlt
