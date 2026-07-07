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

void DocumentWorkspace::SetEditorPaneCount(size_t count) {
    editor_panes_.resize(count);
    ClampActiveEditorPaneIndex(count);
}

size_t DocumentWorkspace::EditorPaneCount() const {
    return editor_panes_.size();
}

bool DocumentWorkspace::HasEditorPaneAt(size_t index) const {
    return index < editor_panes_.size();
}

size_t DocumentWorkspace::PaneDocumentIndex(size_t pane_index) const {
    if (!HasEditorPaneAt(pane_index)) {
        return 0;
    }
    return editor_panes_[pane_index].document_index;
}

std::string DocumentWorkspace::PaneRole(size_t pane_index) const {
    if (!HasEditorPaneAt(pane_index) || editor_panes_[pane_index].role.empty()) {
        return "General";
    }
    return editor_panes_[pane_index].role;
}

bool DocumentWorkspace::SetPaneRole(size_t pane_index, const std::string& role) {
    if (!HasEditorPaneAt(pane_index)) {
        return false;
    }
    editor_panes_[pane_index].role = role.empty() ? "General" : role;
    return true;
}

size_t DocumentWorkspace::ActiveEditorPaneIndex() const {
    return active_editor_pane_index_;
}

void DocumentWorkspace::SetActiveEditorPaneIndex(size_t index) {
    active_editor_pane_index_ = index;
}

void DocumentWorkspace::ClampActiveEditorPaneIndex(size_t visible_pane_count) {
    if (editor_panes_.empty() || visible_pane_count == 0) {
        active_editor_pane_index_ = 0;
        return;
    }

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    active_editor_pane_index_ = std::min(active_editor_pane_index_, visible_count - 1);
}

bool DocumentWorkspace::ActivateEditorPane(size_t pane_index, size_t visible_pane_count) {
    if (editor_panes_.empty() || visible_pane_count == 0) {
        active_editor_pane_index_ = 0;
        return false;
    }

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    pane_index = std::min(pane_index, visible_count - 1);
    active_editor_pane_index_ = pane_index;

    const size_t document_index = editor_panes_[pane_index].document_index;
    if (document_index < open_documents_.size() && open_documents_[document_index]) {
        active_document_index_ = document_index;
    }
    return true;
}

bool DocumentWorkspace::AssignDocumentToPane(size_t pane_index, size_t document_index) {
    if (!HasEditorPaneAt(pane_index) || !HasDocumentAt(document_index)) {
        return false;
    }

    editor_panes_[pane_index].document_index = document_index;
    if (pane_index == active_editor_pane_index_) {
        active_document_index_ = document_index;
    }
    return true;
}

bool DocumentWorkspace::AssignDocumentToActivePane(size_t document_index) {
    if (!HasDocumentAt(document_index)) {
        return false;
    }

    if (editor_panes_.empty()) {
        active_document_index_ = document_index;
        return true;
    }

    if (active_editor_pane_index_ >= editor_panes_.size()) {
        active_editor_pane_index_ = 0;
    }
    editor_panes_[active_editor_pane_index_].document_index = document_index;
    active_document_index_ = document_index;
    return true;
}

void DocumentWorkspace::EnsureEditorPanesHaveDocuments(size_t visible_pane_count) {
    if (open_documents_.empty() || editor_panes_.empty()) {
        return;
    }

    ClampActiveEditorPaneIndex(visible_pane_count);
    ClampActiveDocumentIndex();

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    for (size_t pane_index = 0; pane_index < editor_panes_.size(); ++pane_index) {
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
        editor_panes_[pane_index].document_index = std::min(document_index, open_documents_.size() - 1);
    }
}

void DocumentWorkspace::SyncEditorPaneDocuments(size_t visible_pane_count) {
    if (open_documents_.empty()) {
        return;
    }

    ClampActiveDocumentIndex();
    for (EditorPaneState& pane : editor_panes_) {
        if (pane.document_index >= open_documents_.size()) {
            pane.document_index = std::min(active_document_index_, open_documents_.size() - 1);
        }
    }
    EnsureEditorPanesHaveDocuments(visible_pane_count);
    AssignDocumentToActivePane(std::min(active_document_index_, open_documents_.size() - 1));
}

bool DocumentWorkspace::SplitActiveDocumentToNextPane(
    size_t visible_pane_count,
    size_t& source_pane,
    size_t& target_pane,
    size_t& document_index) {
    if (open_documents_.empty() || editor_panes_.empty() || visible_pane_count <= 1) {
        return false;
    }

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    if (visible_count <= 1) {
        return false;
    }

    ClampActiveEditorPaneIndex(visible_count);
    source_pane = active_editor_pane_index_;

    document_index = active_document_index_;
    if (source_pane < editor_panes_.size() && editor_panes_[source_pane].document_index < open_documents_.size()) {
        document_index = editor_panes_[source_pane].document_index;
    }
    if (!HasDocumentAt(document_index)) {
        document_index = std::min(active_document_index_, open_documents_.size() - 1);
    }
    if (!HasDocumentAt(document_index)) {
        return false;
    }

    source_pane = std::min(source_pane, visible_count - 1);
    target_pane = source_pane + 1;
    if (target_pane >= visible_count) {
        target_pane = source_pane == 0 ? 1 : source_pane - 1;
    }

    AssignDocumentToPane(source_pane, document_index);
    AssignDocumentToPane(target_pane, document_index);
    ActivateEditorPane(source_pane, visible_count);
    return true;
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
    return document && document->IsMemoryOnly();
}

} // namespace textlt
