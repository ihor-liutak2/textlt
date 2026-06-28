#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

struct ViewLayoutPaneInfo {
    std::string title;
    std::string path;
    std::string role;
    size_t document_index = 0;
    bool active = false;
};

struct ViewLayoutDocumentInfo {
    std::string title;
    std::string path;
    bool dirty = false;
    bool memory_only = false;
    bool active = false;
};

struct ViewLayoutSnapshot {
    int layout_index = 0;
    std::string layout_name;
    size_t active_pane_index = 0;
    int two_left_width = 72;
    int three_left_width = 48;
    int three_right_width = 48;
    std::vector<ViewLayoutPaneInfo> panes;
    std::vector<ViewLayoutDocumentInfo> documents;
};

class ViewLayoutContent : public IModalContent {
public:
    using SnapshotProvider = std::function<ViewLayoutSnapshot()>;
    using ApplyLayoutCallback = std::function<void(int layout_index)>;
    using PaneCallback = std::function<void(size_t pane_index)>;
    using PaneDocumentCallback = std::function<void(size_t pane_index, size_t document_index)>;
    using PaneRoleCallback = std::function<void(size_t pane_index, const std::string& role)>;
    using ActionCallback = std::function<void()>;

    ViewLayoutContent(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyLayoutCallback on_apply_layout,
        PaneCallback on_focus_pane,
        PaneDocumentCallback on_assign_document,
        PaneRoleCallback on_set_role,
        ActionCallback on_split_active,
        ActionCallback on_equal_widths);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "View Layout"; }
    ModalSizePreference GetModalSizePreference() const override { return {88, 26}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromApp();
    void ApplySelectedLayout();
    void TakeFocus();

private:
    ftxui::ButtonOption MakeButtonOption(const std::string& label, std::function<void()> on_click) const;
    ftxui::Element RenderLayoutSection(const Theme& theme);
    ftxui::Element RenderPaneManager(const Theme& theme);
    ftxui::Element RenderPanePreview(const Theme& theme);
    void RefreshEntriesFromSnapshot();
    void ClampSelections();
    void SyncControlsFromSelectedPane();
    void FocusSelectedPane();
    void AssignSelectedDocument();
    void ApplySelectedRole();
    std::string SelectedRoleLabel() const;

    const Theme* theme_ = nullptr;
    SnapshotProvider snapshot_provider_;
    ApplyLayoutCallback on_apply_layout_;
    PaneCallback on_focus_pane_;
    PaneDocumentCallback on_assign_document_;
    PaneRoleCallback on_set_role_;
    ActionCallback on_split_active_;
    ActionCallback on_equal_widths_;
    ViewLayoutSnapshot snapshot_;

    int selected_layout_index_ = 0;
    int selected_pane_index_ = 0;
    int selected_document_index_ = 0;
    int selected_role_index_ = 0;

    std::vector<std::string> pane_entries_;
    std::vector<std::string> document_entries_;
    std::vector<std::string> role_entries_ = {
        "General",
        "Source",
        "Target",
        "AI Draft",
        "Notes",
        "Glossary",
    };

    ftxui::Component single_button_;
    ftxui::Component two_button_;
    ftxui::Component three_button_;
    ftxui::Component pane_menu_;
    ftxui::Component document_menu_;
    ftxui::Component role_toggle_;
    ftxui::Component focus_pane_button_;
    ftxui::Component assign_document_button_;
    ftxui::Component set_role_button_;
    ftxui::Component split_active_button_;
    ftxui::Component equal_widths_button_;
    ftxui::Component container_;
};

class ViewLayoutModal {
public:
    using SnapshotProvider = ViewLayoutContent::SnapshotProvider;
    using ApplyLayoutCallback = ViewLayoutContent::ApplyLayoutCallback;
    using PaneCallback = ViewLayoutContent::PaneCallback;
    using PaneDocumentCallback = ViewLayoutContent::PaneDocumentCallback;
    using PaneRoleCallback = ViewLayoutContent::PaneRoleCallback;
    using ActionCallback = ViewLayoutContent::ActionCallback;
    using CloseCallback = std::function<void()>;

    ViewLayoutModal(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyLayoutCallback on_apply_layout,
        PaneCallback on_focus_pane,
        PaneDocumentCallback on_assign_document,
        PaneRoleCallback on_set_role,
        ActionCallback on_split_active,
        ActionCallback on_equal_widths,
        CloseCallback on_close);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    void TakeFocus();
    void SetTheme(const Theme* theme);

private:
    void RequestClose();

    bool open_ = false;
    const Theme* theme_ = nullptr;
    CloseCallback on_close_;
    std::shared_ptr<ViewLayoutContent> content_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
