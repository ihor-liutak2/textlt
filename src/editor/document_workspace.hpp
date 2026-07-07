#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace textlt {

class DocumentSession;

struct EditorPaneState {
    size_t document_index = 0;
    std::string role = "General";
};

class DocumentWorkspace {
public:
    std::vector<std::shared_ptr<DocumentSession>>& OpenDocuments();
    const std::vector<std::shared_ptr<DocumentSession>>& OpenDocuments() const;

    size_t ActiveDocumentIndex() const;
    size_t ActiveSessionIndex() const;
    void SetActiveDocumentIndex(size_t index);
    void SetActiveSessionIndex(size_t index);
    void ClampActiveDocumentIndex();
    void ClampActiveSessionIndex();

    std::shared_ptr<DocumentSession> ActiveDocumentSession() const;
    std::shared_ptr<DocumentSession> DocumentAt(size_t index) const;
    bool HasDocumentAt(size_t index) const;
    DocumentSession* ActiveSession();
    const DocumentSession* ActiveSession() const;
    DocumentSession* SessionAt(size_t index);
    const DocumentSession* SessionAt(size_t index) const;
    bool HasSessionAt(size_t index) const;
    int FindDocumentByPath(const std::filesystem::path& path) const;
    int FindSessionByPath(const std::filesystem::path& path) const;

    void SetEditorPaneCount(size_t count);
    size_t EditorPaneCount() const;
    bool HasEditorPaneAt(size_t index) const;
    size_t PaneDocumentIndex(size_t pane_index) const;
    size_t PaneSessionIndex(size_t pane_index) const;
    std::string PaneRole(size_t pane_index) const;
    bool SetPaneRole(size_t pane_index, const std::string& role);

    size_t ActiveEditorPaneIndex() const;
    void SetActiveEditorPaneIndex(size_t index);
    void ClampActiveEditorPaneIndex(size_t visible_pane_count);
    bool ActivateEditorPane(size_t pane_index, size_t visible_pane_count);
    bool AssignDocumentToPane(size_t pane_index, size_t document_index);
    bool AssignSessionToPane(size_t pane_index, size_t session_index);
    bool AssignDocumentToActivePane(size_t document_index);
    bool AssignSessionToActivePane(size_t session_index);
    void EnsureEditorPanesHaveDocuments(size_t visible_pane_count);
    void SyncEditorPaneDocuments(size_t visible_pane_count);
    bool SplitActiveDocumentToNextPane(
        size_t visible_pane_count,
        size_t& source_pane,
        size_t& target_pane,
        size_t& document_index);

    void ClearDocuments();
    bool Empty() const;
    size_t DocumentCount() const;
    size_t SessionCount() const;
    size_t AddDocumentSession(std::shared_ptr<DocumentSession> document);
    size_t AddSession(std::shared_ptr<DocumentSession> document);
    size_t AddUntitledDocumentSession();
    void RemoveDocumentSession(size_t index);

    static bool IsMemoryOnlyDocumentSession(const std::shared_ptr<DocumentSession>& document);
    static bool IsMemoryOnlySession(const DocumentSession* session);

private:
    std::vector<std::shared_ptr<DocumentSession>> open_documents_;
    size_t active_document_index_ = 0;
    std::vector<EditorPaneState> editor_panes_;
    size_t active_editor_pane_index_ = 0;
};

} // namespace textlt
