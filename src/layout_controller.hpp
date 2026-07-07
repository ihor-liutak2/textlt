#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "editor/document_workspace.hpp"
#include "editor/editor_viewport.hpp"
#include "modals/modal_view_layout.hpp"

namespace textlt {

class LayoutController {
public:
    enum class EditorLayoutMode {
        Single = 0,
        TwoColumns = 1,
        ThreeColumns = 2,
    };

    explicit LayoutController(DocumentWorkspace& workspace);

    EditorLayoutMode Mode() const;
    void SetMode(EditorLayoutMode mode);
    void SetModeByIndex(int layout_index);
    int ModeIndex() const;
    std::string ModeLabel() const;
    static std::string ModeLabel(EditorLayoutMode mode);

    size_t VisiblePaneCount() const;
    bool HasVisiblePane(size_t pane_index) const;

    size_t ActivePaneIndex() const;
    bool ActivatePane(size_t pane_index);
    bool FocusNextPane();
    bool FocusPreviousPane();

    void EqualizePaneWidths();
    int TwoLeftWidth() const;
    int ThreeLeftWidth() const;
    int ThreeRightWidth() const;

    EditorViewportOptions ViewportOptionsForPane(size_t pane_index) const;
    ViewLayoutSnapshot Snapshot() const;

private:
    static std::string ShortSessionTitle(const std::shared_ptr<DocumentSession>& session);

    DocumentWorkspace& workspace_;
    EditorLayoutMode mode_ = EditorLayoutMode::Single;
    int two_left_width_ = 72;
    int three_left_width_ = 48;
    int three_right_width_ = 48;
};

} // namespace textlt
