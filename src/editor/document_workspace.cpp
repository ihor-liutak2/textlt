#include "editor/document_workspace.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>

#include "editor/document_session.hpp"

namespace textlt {

std::vector<std::shared_ptr<DocumentSession>>& DocumentWorkspace::OpenSessions() {
    return sessions_;
}

const std::vector<std::shared_ptr<DocumentSession>>& DocumentWorkspace::OpenSessions() const {
    return sessions_;
}

size_t DocumentWorkspace::ActiveSessionIndex() const {
    return active_session_index_;
}

void DocumentWorkspace::SetActiveSessionIndex(size_t index) {
    active_session_index_ = index;
    ClampActiveSessionIndex();
}

void DocumentWorkspace::ClampActiveSessionIndex() {
    if (sessions_.empty()) {
        active_session_index_ = 0;
        return;
    }
    active_session_index_ = std::min(active_session_index_, sessions_.size() - 1);
}

std::shared_ptr<DocumentSession> DocumentWorkspace::ActiveSessionPtr() const {
    return SessionPtrAt(active_session_index_);
}

std::shared_ptr<DocumentSession> DocumentWorkspace::SessionPtrAt(size_t index) const {
    if (index >= sessions_.size()) {
        return nullptr;
    }
    return sessions_[index];
}

DocumentSession* DocumentWorkspace::ActiveSession() {
    return SessionAt(active_session_index_);
}

const DocumentSession* DocumentWorkspace::ActiveSession() const {
    return SessionAt(active_session_index_);
}

DocumentSession* DocumentWorkspace::SessionAt(size_t index) {
    const auto session = SessionPtrAt(index);
    return session.get();
}

const DocumentSession* DocumentWorkspace::SessionAt(size_t index) const {
    const auto session = SessionPtrAt(index);
    return session.get();
}

bool DocumentWorkspace::HasSessionAt(size_t index) const {
    return SessionPtrAt(index) != nullptr;
}

int DocumentWorkspace::FindSessionByPath(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) {
        normalized = path;
        error.clear();
    }

    for (size_t index = 0; index < sessions_.size(); ++index) {
        const auto& session = sessions_[index];
        if (!session) {
            continue;
        }

        std::filesystem::path session_path = std::filesystem::absolute(session->path, error);
        if (error) {
            session_path = session->path;
            error.clear();
        }
        if (session_path == normalized) {
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

size_t DocumentWorkspace::PaneSessionIndex(size_t pane_index) const {
    if (!HasEditorPaneAt(pane_index)) {
        return 0;
    }
    return editor_panes_[pane_index].session_index;
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


EditorViewport* DocumentWorkspace::PaneViewport(size_t pane_index) {
    if (!HasEditorPaneAt(pane_index)) {
        return nullptr;
    }
    return &editor_panes_[pane_index].viewport;
}

const EditorViewport* DocumentWorkspace::PaneViewport(size_t pane_index) const {
    if (!HasEditorPaneAt(pane_index)) {
        return nullptr;
    }
    return &editor_panes_[pane_index].viewport;
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

    const size_t session_index = editor_panes_[pane_index].session_index;
    if (session_index < sessions_.size() && sessions_[session_index]) {
        active_session_index_ = session_index;
        sessions_[session_index]->SetActiveCursorState(&editor_panes_[pane_index].viewport.CursorState());
    }
    return true;
}

bool DocumentWorkspace::AssignSessionToPane(size_t pane_index, size_t session_index) {
    if (!HasEditorPaneAt(pane_index) || !HasSessionAt(session_index)) {
        return false;
    }

    editor_panes_[pane_index].session_index = session_index;
    if (pane_index == active_editor_pane_index_) {
        active_session_index_ = session_index;
        if (sessions_[session_index]) {
            sessions_[session_index]->SetActiveCursorState(&editor_panes_[pane_index].viewport.CursorState());
        }
    }
    return true;
}

bool DocumentWorkspace::AssignSessionToActivePane(size_t session_index) {
    if (!HasSessionAt(session_index)) {
        return false;
    }

    if (editor_panes_.empty()) {
        active_session_index_ = session_index;
        return true;
    }

    if (active_editor_pane_index_ >= editor_panes_.size()) {
        active_editor_pane_index_ = 0;
    }
    editor_panes_[active_editor_pane_index_].session_index = session_index;
    active_session_index_ = session_index;
    if (sessions_[session_index]) {
        sessions_[session_index]->SetActiveCursorState(&editor_panes_[active_editor_pane_index_].viewport.CursorState());
    }
    return true;
}

void DocumentWorkspace::EnsureEditorPanesHaveSessions(size_t visible_pane_count) {
    if (sessions_.empty() || editor_panes_.empty()) {
        return;
    }

    ClampActiveEditorPaneIndex(visible_pane_count);
    ClampActiveSessionIndex();

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    for (size_t pane_index = 0; pane_index < editor_panes_.size(); ++pane_index) {
        size_t session_index = editor_panes_[pane_index].session_index;
        if (pane_index < visible_count) {
            if (pane_index == active_editor_pane_index_) {
                session_index = active_session_index_;
            } else if (session_index >= sessions_.size()) {
                session_index = std::min(pane_index, sessions_.size() - 1);
            }
        } else if (session_index >= sessions_.size()) {
            session_index = active_session_index_;
        }
        editor_panes_[pane_index].session_index = std::min(session_index, sessions_.size() - 1);
    }
}

void DocumentWorkspace::SyncEditorPaneSessions(size_t visible_pane_count) {
    if (sessions_.empty()) {
        return;
    }

    ClampActiveSessionIndex();
    for (EditorPaneState& pane : editor_panes_) {
        if (pane.session_index >= sessions_.size()) {
            pane.session_index = std::min(active_session_index_, sessions_.size() - 1);
        }
    }
    EnsureEditorPanesHaveSessions(visible_pane_count);
    AssignSessionToActivePane(std::min(active_session_index_, sessions_.size() - 1));
}

bool DocumentWorkspace::SplitActiveSessionToNextPane(
    size_t visible_pane_count,
    size_t& source_pane,
    size_t& target_pane,
    size_t& session_index) {
    if (sessions_.empty() || editor_panes_.empty() || visible_pane_count <= 1) {
        return false;
    }

    const size_t visible_count = std::min(visible_pane_count, editor_panes_.size());
    if (visible_count <= 1) {
        return false;
    }

    ClampActiveEditorPaneIndex(visible_count);
    source_pane = active_editor_pane_index_;

    session_index = active_session_index_;
    if (source_pane < editor_panes_.size() && editor_panes_[source_pane].session_index < sessions_.size()) {
        session_index = editor_panes_[source_pane].session_index;
    }
    if (!HasSessionAt(session_index)) {
        session_index = std::min(active_session_index_, sessions_.size() - 1);
    }
    if (!HasSessionAt(session_index)) {
        return false;
    }

    source_pane = std::min(source_pane, visible_count - 1);
    target_pane = source_pane + 1;
    if (target_pane >= visible_count) {
        target_pane = source_pane == 0 ? 1 : source_pane - 1;
    }

    AssignSessionToPane(source_pane, session_index);
    AssignSessionToPane(target_pane, session_index);
    editor_panes_[target_pane].viewport.CursorState() = editor_panes_[source_pane].viewport.CursorState();
    ActivateEditorPane(source_pane, visible_count);
    return true;
}

void DocumentWorkspace::ClearSessions() {
    sessions_.clear();
    active_session_index_ = 0;
    for (EditorPaneState& pane : editor_panes_) {
        pane.session_index = 0;
    }
}

bool DocumentWorkspace::Empty() const {
    return sessions_.empty();
}

size_t DocumentWorkspace::SessionCount() const {
    return sessions_.size();
}

size_t DocumentWorkspace::AddSession(std::shared_ptr<DocumentSession> session) {
    sessions_.push_back(std::move(session));
    active_session_index_ = sessions_.empty() ? 0 : sessions_.size() - 1;
    return active_session_index_;
}

size_t DocumentWorkspace::AddUntitledSession() {
    auto session = std::make_shared<DocumentSession>();
    session->Reset();
    return AddSession(std::move(session));
}

void DocumentWorkspace::RemoveSession(size_t index) {
    if (index >= sessions_.size()) {
        return;
    }
    sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(index));
    if (sessions_.empty()) {
        active_session_index_ = 0;
        for (EditorPaneState& pane : editor_panes_) {
            pane.session_index = 0;
        }
        return;
    }

    if (active_session_index_ >= sessions_.size()) {
        active_session_index_ = sessions_.size() - 1;
    } else if (index < active_session_index_) {
        --active_session_index_;
    }

    for (EditorPaneState& pane : editor_panes_) {
        if (pane.session_index > index) {
            --pane.session_index;
        } else if (pane.session_index == index) {
            pane.session_index = active_session_index_;
        }
    }
}

bool DocumentWorkspace::IsMemoryOnlySession(const std::shared_ptr<DocumentSession>& session) {
    return session && session->IsMemoryOnly();
}

bool DocumentWorkspace::IsMemoryOnlySession(const DocumentSession* session) {
    return session && session->IsMemoryOnly();
}

} // namespace textlt
