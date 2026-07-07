#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "editor/editor_viewport.hpp"

namespace textlt {

class DocumentSession;

struct EditorPaneState {
    size_t session_index = 0;
    std::string role = "General";
    EditorViewport viewport;
};

class DocumentWorkspace {
public:
    std::vector<std::shared_ptr<DocumentSession>>& OpenSessions();
    const std::vector<std::shared_ptr<DocumentSession>>& OpenSessions() const;

    size_t ActiveSessionIndex() const;
    void SetActiveSessionIndex(size_t index);
    void ClampActiveSessionIndex();

    std::shared_ptr<DocumentSession> ActiveSessionPtr() const;
    std::shared_ptr<DocumentSession> SessionPtrAt(size_t index) const;
    DocumentSession* ActiveSession();
    const DocumentSession* ActiveSession() const;
    DocumentSession* SessionAt(size_t index);
    const DocumentSession* SessionAt(size_t index) const;
    bool HasSessionAt(size_t index) const;
    int FindSessionByPath(const std::filesystem::path& path) const;

    void SetEditorPaneCount(size_t count);
    size_t EditorPaneCount() const;
    bool HasEditorPaneAt(size_t index) const;
    size_t PaneSessionIndex(size_t pane_index) const;
    std::string PaneRole(size_t pane_index) const;
    bool SetPaneRole(size_t pane_index, const std::string& role);
    EditorViewport* PaneViewport(size_t pane_index);
    const EditorViewport* PaneViewport(size_t pane_index) const;

    size_t ActiveEditorPaneIndex() const;
    void SetActiveEditorPaneIndex(size_t index);
    void ClampActiveEditorPaneIndex(size_t visible_pane_count);
    bool ActivateEditorPane(size_t pane_index, size_t visible_pane_count);
    bool AssignSessionToPane(size_t pane_index, size_t session_index);
    bool AssignSessionToActivePane(size_t session_index);
    void EnsureEditorPanesHaveSessions(size_t visible_pane_count);
    void SyncEditorPaneSessions(size_t visible_pane_count);
    bool SplitActiveSessionToNextPane(
        size_t visible_pane_count,
        size_t& source_pane,
        size_t& target_pane,
        size_t& session_index);

    void ClearSessions();
    bool Empty() const;
    size_t SessionCount() const;
    size_t AddSession(std::shared_ptr<DocumentSession> session);
    size_t AddUntitledSession();
    void RemoveSession(size_t index);

    static bool IsMemoryOnlySession(const std::shared_ptr<DocumentSession>& session);
    static bool IsMemoryOnlySession(const DocumentSession* session);

private:
    std::vector<std::shared_ptr<DocumentSession>> sessions_;
    size_t active_session_index_ = 0;
    std::vector<EditorPaneState> editor_panes_;
    size_t active_editor_pane_index_ = 0;
};

} // namespace textlt
