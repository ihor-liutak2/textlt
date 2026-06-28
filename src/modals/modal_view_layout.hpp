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
    std::string role;
    bool active = false;
};

struct ViewLayoutSnapshot {
    int layout_index = 0;
    std::string layout_name;
    size_t active_pane_index = 0;
    std::vector<ViewLayoutPaneInfo> panes;
};

class ViewLayoutContent : public IModalContent {
public:
    using SnapshotProvider = std::function<ViewLayoutSnapshot()>;
    using ApplyCallback = std::function<void(int layout_index)>;

    ViewLayoutContent(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyCallback on_apply);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "View Layout"; }
    ModalSizePreference GetModalSizePreference() const override { return {72, 22}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromApp();
    void ApplySelectedLayout();
    void TakeFocus();

private:
    ftxui::ButtonOption MakeButtonOption(const std::string& label, std::function<void()> on_click) const;
    ftxui::Element RenderLayoutButtons(const Theme& theme);
    ftxui::Element RenderPanePreview(const Theme& theme);

    const Theme* theme_ = nullptr;
    SnapshotProvider snapshot_provider_;
    ApplyCallback on_apply_;
    ViewLayoutSnapshot snapshot_;
    int selected_layout_index_ = 0;
    ftxui::Component single_button_;
    ftxui::Component two_button_;
    ftxui::Component three_button_;
    ftxui::Component container_;
};

class ViewLayoutModal {
public:
    using SnapshotProvider = ViewLayoutContent::SnapshotProvider;
    using ApplyCallback = ViewLayoutContent::ApplyCallback;
    using CloseCallback = std::function<void()>;

    ViewLayoutModal(
        const Theme* theme,
        SnapshotProvider snapshot_provider,
        ApplyCallback on_apply,
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
