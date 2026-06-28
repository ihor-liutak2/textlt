#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

namespace {

std::string DocumentTitle(const std::shared_ptr<Document>& doc) {
    if (!doc) {
        return "Untitled";
    }
    std::string title = doc->path.filename().string();
    if (title.empty()) {
        title = doc->path.string();
    }
    if (title.empty()) {
        title = "Untitled";
    }
    return title;
}

std::string ShortDocumentTitle(const std::shared_ptr<Document>& doc) {
    std::string title = DocumentTitle(doc);
    if (doc && doc->is_dirty) {
        title += " *";
    }
    return title;
}

bool IsMemoryOnlyPath(const std::shared_ptr<Document>& doc) {
    if (!doc) {
        return false;
    }
    const std::string path = doc->path.string();
    return path.empty() || path == "Untitled" || path == "untitled.txt";
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
    if (active_editor_pane_index_ >= VisibleEditorPaneCount()) {
        active_editor_pane_index_ = 0;
    }
    EnsureEditorPanesHaveDocuments();
    SetActiveEditorPane(active_editor_pane_index_);
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
    pane_index = std::min(pane_index, visible_count - 1);

    const bool editor_layer_is_active = ActiveLayer() == UiLayer::Main;
    active_editor_pane_index_ = pane_index;
    sidebar_has_focus_ = false;

    if (pane_index < editor_panes_.size() &&
        editor_panes_[pane_index].document_index < open_documents_.size()) {
        active_document_index_ = editor_panes_[pane_index].document_index;
    }

    text_editor_ = editor_pane_components_[active_editor_pane_index_];
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
    SetActiveEditorPane((active_editor_pane_index_ + 1) % visible_count);
    active_action_ = "Active pane: " + std::to_string(active_editor_pane_index_ + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::FocusPreviousEditorPane() {
    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count <= 1) {
        active_action_ = "Only one editor pane is visible";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    const size_t next_index = active_editor_pane_index_ == 0
        ? visible_count - 1
        : active_editor_pane_index_ - 1;
    SetActiveEditorPane(next_index);
    active_action_ = "Active pane: " + std::to_string(active_editor_pane_index_ + 1);
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
    if (pane_index >= editor_panes_.size()) {
        return;
    }
    editor_panes_[pane_index].role = role.empty() ? "General" : role;
    active_action_ = "Pane " + std::to_string(pane_index + 1) + " role: " + editor_panes_[pane_index].role;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::AssignDocumentToEditorPane(size_t pane_index, size_t document_index) {
    if (pane_index >= editor_panes_.size() ||
        pane_index >= editor_pane_components_.size() ||
        document_index >= open_documents_.size() ||
        !open_documents_[document_index]) {
        return;
    }

    editor_panes_[pane_index].document_index = document_index;
    auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
    if (editor) {
        editor->SetDocument(open_documents_[document_index]);
    }

    if (pane_index == active_editor_pane_index_) {
        active_document_index_ = document_index;
        text_editor_ = editor_pane_components_[pane_index];
    }

    active_action_ = "Pane " + std::to_string(pane_index + 1) + " document: " +
        ShortDocumentTitle(open_documents_[document_index]);
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SplitActiveDocumentToNextPane() {
    if (open_documents_.empty()) {
        EnsureOneOpenDocument();
    }
    if (open_documents_.empty()) {
        return;
    }

    size_t source_pane = active_editor_pane_index_;
    if (source_pane >= editor_panes_.size()) {
        source_pane = 0;
        active_editor_pane_index_ = 0;
    }

    size_t source_document_index = active_document_index_;
    if (source_pane < editor_panes_.size() &&
        editor_panes_[source_pane].document_index < open_documents_.size()) {
        source_document_index = editor_panes_[source_pane].document_index;
    }
    if (source_document_index >= open_documents_.size() ||
        !open_documents_[source_document_index]) {
        source_document_index = std::min(active_document_index_, open_documents_.size() - 1);
    }

    if (VisibleEditorPaneCount() <= 1) {
        SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
    }

    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count <= 1) {
        active_action_ = "Could not split the active document";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    source_pane = std::min(source_pane, visible_count - 1);
    size_t target_pane = source_pane + 1;
    if (target_pane >= visible_count) {
        target_pane = source_pane == 0 ? 1 : source_pane - 1;
    }

    // This is a view split, not a file copy: both panes point at the same
    // shared Document object. EditorComponent keeps its own scroll state, so
    // each pane can view a different part of the same file.
    AssignDocumentToEditorPane(source_pane, source_document_index);
    AssignDocumentToEditorPane(target_pane, source_document_index);
    SetActiveEditorPane(source_pane);

    const std::string title = ShortDocumentTitle(open_documents_[source_document_index]);
    active_action_ = "Split " + title + " into pane " + std::to_string(target_pane + 1);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::EnsureEditorPanesHaveDocuments() {
    if (editor_panes_.size() < editor_pane_components_.size()) {
        editor_panes_.resize(editor_pane_components_.size());
    }
    if (open_documents_.empty()) {
        return;
    }

    const size_t visible_count = VisibleEditorPaneCount();
    for (size_t pane_index = 0; pane_index < editor_pane_components_.size(); ++pane_index) {
        size_t document_index = editor_panes_[pane_index].document_index;
        if (pane_index < visible_count) {
            if (pane_index == active_editor_pane_index_) {
                document_index = active_document_index_;
            } else if (document_index >= open_documents_.size()) {
                document_index = std::min(pane_index, open_documents_.size() - 1);
            }
        } else if (document_index >= open_documents_.size()) {
            document_index = active_document_index_;
        }
        document_index = std::min(document_index, open_documents_.size() - 1);
        editor_panes_[pane_index].document_index = document_index;
        auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
        if (editor && open_documents_[document_index]) {
            editor->SetDocument(open_documents_[document_index]);
        }
    }
}

void TextltApp::AssignDocumentToActivePane(size_t document_index) {
    if (open_documents_.empty() || document_index >= open_documents_.size()) {
        return;
    }
    if (active_editor_pane_index_ >= editor_panes_.size() ||
        active_editor_pane_index_ >= editor_pane_components_.size()) {
        active_editor_pane_index_ = 0;
    }
    editor_panes_[active_editor_pane_index_].document_index = document_index;
    active_document_index_ = document_index;
    text_editor_ = editor_pane_components_[active_editor_pane_index_];
    auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    if (editor) {
        editor->SetDocument(open_documents_[document_index]);
        if (ActiveLayer() == UiLayer::Main) {
            editor->TakeFocus();
        }
    }
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
}

void TextltApp::SyncEditorPaneDocuments() {
    if (open_documents_.empty()) {
        return;
    }
    for (EditorPaneState& pane : editor_panes_) {
        if (pane.document_index >= open_documents_.size()) {
            pane.document_index = std::min(active_document_index_, open_documents_.size() - 1);
        }
    }
    EnsureEditorPanesHaveDocuments();
    AssignDocumentToActivePane(std::min(active_document_index_, open_documents_.size() - 1));
}

ftxui::Element TextltApp::RenderEditorPane(size_t pane_index) {
    using namespace ftxui;

    if (pane_index >= VisibleEditorPaneCount() ||
        pane_index >= editor_pane_components_.size()) {
        return emptyElement() | size(WIDTH, EQUAL, 0);
    }

    const Theme& theme = current_theme_;
    auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
    std::shared_ptr<Document> doc = editor ? editor->GetDocument() : nullptr;

    const std::string title = DocumentTitle(doc);
    const bool dirty = doc && doc->is_dirty;
    const std::string type_label = doc ? doc->Label() : "Plain Text";

    const bool active = !sidebar_has_focus_ && pane_index == active_editor_pane_index_;
    const std::string marker = active ? "●" : "○";
    std::string header_text = " " + marker + " " + std::to_string(pane_index + 1) + ": " + title;
    if (dirty) {
        header_text += " *";
    }
    header_text += " | " + type_label;

    Element header = hbox({
        text(header_text) | bold | color(active ? theme.modal_selected_item_fg : theme.menu_foreground),
        filler(),
        text(editor_panes_[pane_index].role.empty() ? "General " : editor_panes_[pane_index].role + " ") |
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
    snapshot.active_pane_index = active_editor_pane_index_;
    snapshot.two_left_width = editor_two_left_width_;
    snapshot.three_left_width = editor_three_left_width_;
    snapshot.three_right_width = editor_three_right_width_;

    for (size_t doc_index = 0; doc_index < open_documents_.size(); ++doc_index) {
        const auto& doc = open_documents_[doc_index];
        if (!doc) {
            continue;
        }
        ViewLayoutDocumentInfo doc_info;
        doc_info.title = ShortDocumentTitle(doc);
        doc_info.path = doc->path.string();
        doc_info.dirty = doc->is_dirty;
        doc_info.memory_only = IsMemoryOnlyPath(doc);
        doc_info.active = doc_index == active_document_index_;
        snapshot.documents.push_back(std::move(doc_info));
    }

    const size_t visible_count = VisibleEditorPaneCount();
    for (size_t pane_index = 0; pane_index < visible_count; ++pane_index) {
        ViewLayoutPaneInfo info;
        info.role = pane_index < editor_panes_.size() ? editor_panes_[pane_index].role : "General";
        info.active = pane_index == active_editor_pane_index_;
        info.document_index = pane_index < editor_panes_.size() ? editor_panes_[pane_index].document_index : 0;
        if (info.document_index < open_documents_.size()) {
            const auto& doc = open_documents_[info.document_index];
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
