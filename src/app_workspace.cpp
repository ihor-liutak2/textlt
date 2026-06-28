#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

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

void TextltApp::SetActiveEditorPane(size_t pane_index) {
    if (editor_pane_components_.empty()) {
        return;
    }
    const size_t visible_count = VisibleEditorPaneCount();
    if (visible_count == 0) {
        return;
    }
    pane_index = std::min(pane_index, visible_count - 1);
    // Changing the pane assignment can happen while the View Layout modal is
    // active. Do not switch the root Container::Tab back to Main or move FTXUI
    // focus into the editor in that case: the modal must keep receiving events
    // until it is explicitly closed.
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
        editor->TakeFocus();
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

    std::string title = "Untitled";
    bool dirty = false;
    std::string type_label = "Plain Text";
    if (doc) {
        title = doc->path.filename().string();
        if (title.empty()) {
            title = doc->path.string();
        }
        if (title.empty()) {
            title = "Untitled";
        }
        dirty = doc->is_dirty;
        type_label = doc->Label();
    }

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

    const size_t visible_count = VisibleEditorPaneCount();
    for (size_t pane_index = 0; pane_index < visible_count; ++pane_index) {
        ViewLayoutPaneInfo info;
        info.role = pane_index < editor_panes_.size() ? editor_panes_[pane_index].role : "General";
        info.active = pane_index == active_editor_pane_index_;
        if (pane_index < editor_panes_.size() &&
            editor_panes_[pane_index].document_index < open_documents_.size()) {
            const auto& doc = open_documents_[editor_panes_[pane_index].document_index];
            if (doc) {
                info.title = doc->path.filename().string();
                if (info.title.empty()) {
                    info.title = doc->path.string();
                }
                if (doc->is_dirty) {
                    info.title += " *";
                }
            }
        }
        if (info.title.empty()) {
            info.title = "Untitled";
        }
        snapshot.panes.push_back(std::move(info));
    }
    return snapshot;
}

} // namespace textlt
