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
#include "ui_button.hpp"

namespace textlt {

struct ViewLayoutPaneInfo {
    std::string title;
    std::string path;
    size_t session_index = 0;
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
    using PaneSessionCallback = std::function<void(size_t pane_index, size_t session_index)>;
    using ActionCallback = std::function<void()>;

    ViewLayoutContent(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyLayoutCallback on_apply_layout,
        PaneSessionCallback on_assign_session,
        ActionCallback on_equal_widths,
        std::function<void()> on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "View Layout"; }
    ModalSizePreference GetModalSizePreference() const override { return {74, 24}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 1; }
    ftxui::Element RenderCustomFooter() override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromApp();
    void ApplySelectedLayout();
    void AssignSelectedDocumentToPane(size_t pane_index);
    void TakeFocus();

private:
    ftxui::ButtonOption MakeButtonOption(
        std::string label,
        std::function<void()> on_click,
        ButtonRole role = ButtonRole::Default,
        std::string icon = {}) const;
    ftxui::Element RenderLayoutSection(const Theme& theme);
    ftxui::Element RenderPaneManager(const Theme& theme);
    void RefreshEntriesFromSnapshot();
    void ClampSelections();
    void SyncControlsFromSelectedPane();

    const Theme* theme_ = nullptr;
    SnapshotProvider snapshot_provider_;
    ApplyLayoutCallback on_apply_layout_;
    PaneSessionCallback on_assign_session_;
    ActionCallback on_equal_widths_;
    std::function<void()> on_close_;
    ViewLayoutSnapshot snapshot_;

    int selected_layout_index_ = 0;
    int selected_session_index_ = 0;

    std::vector<std::string> document_entries_;
    ftxui::Component single_button_;
    ftxui::Component two_button_;
    ftxui::Component three_button_;
    ftxui::Component document_menu_;
    ftxui::Component set_pane_1_button_;
    ftxui::Component set_pane_2_button_;
    ftxui::Component set_pane_3_button_;
    ftxui::Component equal_widths_button_;
    ftxui::Component close_button_;
    ftxui::Component container_;
};

class ViewLayoutModal {
public:
    using SnapshotProvider = ViewLayoutContent::SnapshotProvider;
    using ApplyLayoutCallback = ViewLayoutContent::ApplyLayoutCallback;
    using PaneSessionCallback = ViewLayoutContent::PaneSessionCallback;
    using ActionCallback = ViewLayoutContent::ActionCallback;
    using CloseCallback = std::function<void()>;

    ViewLayoutModal(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyLayoutCallback on_apply_layout,
        PaneSessionCallback on_assign_session,
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
