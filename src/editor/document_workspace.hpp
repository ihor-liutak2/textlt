#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace textlt {

struct Document;

struct EditorPaneState {
    size_t document_index = 0;
    std::string role = "General";
};

class DocumentWorkspace {
public:
    std::vector<std::shared_ptr<Document>>& OpenDocuments();
    const std::vector<std::shared_ptr<Document>>& OpenDocuments() const;

    size_t& ActiveDocumentIndex();
    size_t ActiveDocumentIndex() const;
    void SetActiveDocumentIndex(size_t index);

    std::vector<EditorPaneState>& EditorPanes();
    const std::vector<EditorPaneState>& EditorPanes() const;

    size_t& ActiveEditorPaneIndex();
    size_t ActiveEditorPaneIndex() const;
    void SetActiveEditorPaneIndex(size_t index);

    void ClearDocuments();
    bool Empty() const;
    size_t DocumentCount() const;
    size_t AddDocument(std::shared_ptr<Document> document);
    void RemoveDocument(size_t index);

private:
    std::vector<std::shared_ptr<Document>> open_documents_;
    size_t active_document_index_ = 0;
    std::vector<EditorPaneState> editor_panes_;
    size_t active_editor_pane_index_ = 0;
};

} // namespace textlt
