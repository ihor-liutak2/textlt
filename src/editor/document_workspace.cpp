#include "editor/document_workspace.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace textlt {

std::vector<std::shared_ptr<Document>>& DocumentWorkspace::OpenDocuments() {
    return open_documents_;
}

const std::vector<std::shared_ptr<Document>>& DocumentWorkspace::OpenDocuments() const {
    return open_documents_;
}

size_t& DocumentWorkspace::ActiveDocumentIndex() {
    return active_document_index_;
}

size_t DocumentWorkspace::ActiveDocumentIndex() const {
    return active_document_index_;
}

void DocumentWorkspace::SetActiveDocumentIndex(size_t index) {
    active_document_index_ = index;
}

std::vector<EditorPaneState>& DocumentWorkspace::EditorPanes() {
    return editor_panes_;
}

const std::vector<EditorPaneState>& DocumentWorkspace::EditorPanes() const {
    return editor_panes_;
}

size_t& DocumentWorkspace::ActiveEditorPaneIndex() {
    return active_editor_pane_index_;
}

size_t DocumentWorkspace::ActiveEditorPaneIndex() const {
    return active_editor_pane_index_;
}

void DocumentWorkspace::SetActiveEditorPaneIndex(size_t index) {
    active_editor_pane_index_ = index;
}

void DocumentWorkspace::ClearDocuments() {
    open_documents_.clear();
    active_document_index_ = 0;
    for (EditorPaneState& pane : editor_panes_) {
        pane.document_index = 0;
    }
}

bool DocumentWorkspace::Empty() const {
    return open_documents_.empty();
}

size_t DocumentWorkspace::DocumentCount() const {
    return open_documents_.size();
}

void DocumentWorkspace::AddDocument(std::shared_ptr<Document> document) {
    open_documents_.push_back(std::move(document));
    active_document_index_ = open_documents_.empty() ? 0 : open_documents_.size() - 1;
}

void DocumentWorkspace::RemoveDocument(size_t index) {
    if (index >= open_documents_.size()) {
        return;
    }
    open_documents_.erase(open_documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (open_documents_.empty()) {
        active_document_index_ = 0;
        for (EditorPaneState& pane : editor_panes_) {
            pane.document_index = 0;
        }
        return;
    }

    if (active_document_index_ >= open_documents_.size()) {
        active_document_index_ = open_documents_.size() - 1;
    } else if (index < active_document_index_) {
        --active_document_index_;
    }

    for (EditorPaneState& pane : editor_panes_) {
        if (pane.document_index > index) {
            --pane.document_index;
        } else if (pane.document_index == index) {
            pane.document_index = active_document_index_;
        }
    }
}

} // namespace textlt
