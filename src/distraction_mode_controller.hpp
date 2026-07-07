#pragma once

#include <cstddef>
#include <string>

#include "editor/editor_viewport.hpp"

namespace textlt {

class DocumentSession;
class DocumentWorkspace;
class EditorConfig;

struct DistractionModeSettings {
    bool enabled = false;
    int column_count = 1;
    int column_width = 92;
    int column_gap = 6;
    int top_padding = 1;
    int bottom_padding = 1;
};

struct DistractionTopBarState {
    bool enabled = false;
    size_t current_page = 1;
    size_t total_pages = 1;
    std::string page_input = "1";
};

class DistractionModeController {
public:
    DistractionModeController() = default;
    explicit DistractionModeController(DistractionModeSettings settings);

    const DistractionModeSettings& Settings() const;
    void SetSettings(DistractionModeSettings settings);

    bool Enabled() const;
    void SetEnabled(bool enabled);
    void Enter();
    void Exit();
    bool Toggle();

    void SetColumnCount(int column_count);
    void SetPageInput(std::string input);
    const std::string& PageInput() const;

    EditorViewportOptions ApplyToViewportOptions(EditorViewportOptions options) const;
    DistractionTopBarState TopBarState(
        const EditorViewport* viewport,
        const DocumentSession* session,
        const EditorConfig* config) const;
    DistractionTopBarState TopBarState(
        const DocumentWorkspace* workspace,
        size_t visible_pane_count,
        const EditorConfig* config) const;

    bool NextPage(EditorViewport* viewport, DocumentSession* session, const EditorConfig* config);
    bool PreviousPage(EditorViewport* viewport, DocumentSession* session, const EditorConfig* config);
    bool GoToPage(EditorViewport* viewport, DocumentSession* session, const EditorConfig* config);
    bool NextPage(DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config);
    bool PreviousPage(DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config);
    bool GoToPage(DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config);
    bool SyncPageColumns(DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config);

    static DistractionModeSettings NormalizeSettings(DistractionModeSettings settings);

private:
    size_t VisiblePageHeight(const EditorViewport* viewport) const;
    size_t EffectiveColumnCount(size_t visible_pane_count) const;
    size_t TotalPages(const EditorViewport* viewport, const DocumentSession* session, const EditorConfig* config) const;
    size_t CurrentPage(const EditorViewport* viewport, const DocumentSession* session, const EditorConfig* config) const;
    bool SetPage(EditorViewport* viewport, DocumentSession* session, const EditorConfig* config, size_t page);
    size_t TotalPages(const DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config) const;
    size_t CurrentPage(const DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config) const;
    bool SetPage(DocumentWorkspace* workspace, size_t visible_pane_count, const EditorConfig* config, size_t page);

    DistractionModeSettings settings_;
    std::string page_input_ = "1";
};

} // namespace textlt
