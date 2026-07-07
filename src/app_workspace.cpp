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
    switch (editor_layout_mode_) {
        case EditorLayoutMode::TwoColumns: return 2;
        case EditorLayoutMode::ThreeColumns: return 3;
        case EditorLayoutMode::Single:
        default: return 1;
    }
}

std::string TextltApp::EditorLayoutModeLabel(EditorLayoutMode mode) const {
    switch (mode) {
        case EditorLayoutMode::TwoColumns: return "Two columns";
        case EditorLayoutMode::ThreeColumns: return "Three columns";
        case EditorLayoutMode::Single:
        default: return "Single column";
    }
}

int TextltApp::EditorLayoutModeIndex() const {
    switch (editor_layout_mode_) {
        case EditorLayoutMode::TwoColumns: return 1;
        case EditorLayoutMode::ThreeColumns: return 2;
        case EditorLayoutMode::Single:
        default: return 0;
    }
}

void TextltApp::SetEditorLayoutMode(EditorLayoutMode mode) {
    editor_layout_mode_ = mode;
    document_workspace_.ClampActiveEditorPaneIndex(VisibleEditorPaneCount());
    document_workspace_.EnsureEditorPanesHaveSessions(VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
    SetActiveEditorPane(document_workspace_.ActiveEditorPaneIndex());
    active_action_ = "View layout: " + EditorLayoutModeLabel(mode);
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
        !ai_actions_modal_.IsOpen() &&
        !assistant_settings_modal_.IsOpen() &&
        !theme_dialog_.IsOpen() &&
        !unsaved_changes_dialog_.IsOpen();
}

void TextltApp::SetActiveEditorPane(size_t pane_index) {
    if (editor_pane_components_.empty()) {
        return;
    }

    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count == 0) {
        return;
    }

    const bool editor_layer_is_active = ActiveLayer() == UiLayer::Main;
    document_workspace_.ActivateEditorPane(pane_index, visible_count);
    BindEditorComponentsToWorkspace();
    sidebar_has_focus_ = false;

    const size_t active_pane = document_workspace_.ActiveEditorPaneIndex();
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
    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count <= 1) {
        active_action_ = "Only one editor pane is visible";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    SetActiveEditorPane((document_workspace_.ActiveEditorPaneIndex() + 1) % visible_count);
    active_action_ = "Active pane: " + std::to_string(document_workspace_.ActiveEditorPaneIndex() + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::FocusPreviousEditorPane() {
    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count <= 1) {
        active_action_ = "Only one editor pane is visible";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    const size_t next_index = document_workspace_.ActiveEditorPaneIndex() == 0
        ? visible_count - 1
        : document_workspace_.ActiveEditorPaneIndex() - 1;
    SetActiveEditorPane(next_index);
    active_action_ = "Active pane: " + std::to_string(document_workspace_.ActiveEditorPaneIndex() + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::EqualizeEditorPaneWidths() {
    editor_two_left_width_ = 72;
    editor_three_left_width_ = 48;
    editor_three_right_width_ = 48;
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

    if (VisibleEditorPaneCount() <= 1) {
        SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
    }

    size_t source_pane = 0;
    size_t target_pane = 0;
    size_t session_index = 0;
    if (!document_workspace_.SplitActiveSessionToNextPane(
            VisibleEditorPaneCount(),
            source_pane,
            target_pane,
            session_index)) {
        active_action_ = "Could not split the active document";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    // This is a view split, not a file copy: both panes point at the same
    // shared Document object. EditorComponent keeps its own scroll state, so
    // each pane can view a different part of the same file.
    BindEditorComponentsToWorkspace();
    SetActiveEditorPane(source_pane);

    const std::string title = ShortDocumentTitle(document_workspace_.SessionPtrAt(session_index));
    active_action_ = "Split " + title + " into pane " + std::to_string(target_pane + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::BindEditorComponentsToWorkspace() {
    document_workspace_.SetEditorPaneCount(editor_pane_components_.size());
    document_workspace_.EnsureEditorPanesHaveSessions(VisibleEditorPaneCount());

    for (size_t pane_index = 0; pane_index < editor_pane_components_.size(); ++pane_index) {
        const size_t session_index = document_workspace_.PaneSessionIndex(pane_index);
        auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
        if (editor) {
            editor->SetSession(document_workspace_.SessionPtrAt(session_index));
        }
    }

    document_workspace_.ClampActiveEditorPaneIndex(VisibleEditorPaneCount());
    const size_t active_pane = document_workspace_.ActiveEditorPaneIndex();
    if (active_pane < editor_pane_components_.size()) {
        text_editor_ = editor_pane_components_[active_pane];
    }
}

ftxui::Element TextltApp::RenderEditorPane(size_t pane_index) {
    using namespace ftxui;

    if (pane_index >= VisibleEditorPaneCount() ||
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
    return vbox({
        header,
        separator() | color(active ? theme.modal_accent : theme.gutter),
        body | flex,
    }) |
        borderStyled(active ? HEAVY : LIGHT, active ? theme.modal_accent : theme.gutter) |
        bgcolor(theme.background) |
        xflex;
}

ViewLayoutSnapshot TextltApp::CurrentViewLayoutSnapshot() const {
    ViewLayoutSnapshot snapshot;
    snapshot.layout_index = EditorLayoutModeIndex();
    snapshot.layout_name = EditorLayoutModeLabel(editor_layout_mode_);
    snapshot.active_pane_index = document_workspace_.ActiveEditorPaneIndex();
    snapshot.two_left_width = editor_two_left_width_;
    snapshot.three_left_width = editor_three_left_width_;
    snapshot.three_right_width = editor_three_right_width_;

    for (size_t doc_index = 0; doc_index < document_workspace_.OpenSessions().size(); ++doc_index) {
        const auto& doc = document_workspace_.OpenSessions()[doc_index];
        if (!doc) {
            continue;
        }
        ViewLayoutDocumentInfo doc_info;
        doc_info.title = ShortDocumentTitle(doc);
        const DocumentSession& session = *doc;
        doc_info.path = session.path.string();
        doc_info.dirty = doc->is_dirty;
        doc_info.memory_only = DocumentWorkspace::IsMemoryOnlySession(&session);
        doc_info.active = doc_index == document_workspace_.ActiveSessionIndex();
        snapshot.documents.push_back(std::move(doc_info));
    }

    const size_t visible_count = VisibleEditorPaneCount();
    for (size_t pane_index = 0; pane_index < visible_count; ++pane_index) {
        ViewLayoutPaneInfo info;
        info.role = document_workspace_.PaneRole(pane_index);
        info.active = pane_index == document_workspace_.ActiveEditorPaneIndex();
        info.session_index = document_workspace_.PaneSessionIndex(pane_index);
        if (info.session_index < document_workspace_.OpenSessions().size()) {
            const auto& doc = document_workspace_.OpenSessions()[info.session_index];
            info.title = ShortDocumentTitle(doc);
            info.path = doc ? doc->path.string() : "";
        }
        if (info.title.empty()) {
            info.title = "Untitled";
        }
        snapshot.panes.push_back(std::move(info));
    }
    return snapshot;
}

} // namespace textlt
