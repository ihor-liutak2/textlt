#include "editor/document_workspace.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>

#include "document.hpp"

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
    ClampActiveDocumentIndex();
}

void DocumentWorkspace::ClampActiveDocumentIndex() {
    if (open_documents_.empty()) {
        active_document_index_ = 0;
        return;
    }
    active_document_index_ = std::min(active_document_index_, open_documents_.size() - 1);
}

std::shared_ptr<Document> DocumentWorkspace::ActiveDocument() const {
    return DocumentAt(active_document_index_);
}

std::shared_ptr<Document> DocumentWorkspace::DocumentAt(size_t index) const {
    if (index >= open_documents_.size()) {
        return nullptr;
    }
    return open_documents_[index];
}

bool DocumentWorkspace::HasDocumentAt(size_t index) const {
    return DocumentAt(index) != nullptr;
}

int DocumentWorkspace::FindDocumentByPath(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) {
        normalized = path;
        error.clear();
    }

    for (size_t index = 0; index < open_documents_.size(); ++index) {
        const auto& document = open_documents_[index];
        if (!document) {
            continue;
        }

        std::filesystem::path document_path = std::filesystem::absolute(document->path, error);
        if (error) {
            document_path = document->path;
            error.clear();
        }
        if (document_path == normalized) {
            return static_cast<int>(index);
        }
    }
    return -1;
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

size_t DocumentWorkspace::AddDocument(std::shared_ptr<Document> document) {
    open_documents_.push_back(std::move(document));
    active_document_index_ = open_documents_.empty() ? 0 : open_documents_.size() - 1;
    return active_document_index_;
}

size_t DocumentWorkspace::AddUntitledDocument() {
    auto document = std::make_shared<Document>();
    document->Reset();
    return AddDocument(std::move(document));
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

bool DocumentWorkspace::IsMemoryOnlyDocument(const std::shared_ptr<Document>& document) {
    if (!document) {
        return false;
    }
    const std::string path = document->path.string();
    return path.empty() || path == "Untitled" || path == "untitled.txt";
}

} // namespace textlt
