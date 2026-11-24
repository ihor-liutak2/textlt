#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class SidebarShortcutModalContent : public IModalContent {
public:
    using RunCallback = std::function<void(const std::string& command_id)>;
    using CloseCallback = std::function<void()>;

    SidebarShortcutModalContent(
        const Theme* theme,
        RunCallback run_callback,
        CloseCallback close_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Quick Commands"; }
    ModalSizePreference GetModalSizePreference() const override { return {34, 15}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    bool HandleEvent(ftxui::Event event);

private:
    struct Entry {
        char key;
        std::string label;
        std::string command_id;
        bool switches_workspace = false;
    };

    void TriggerEntry(size_t index);
    bool HandleMouseEvent(ftxui::Event event);
    bool IsEntryKey(ftxui::Event event, char key) const;

    const Theme* theme_ = nullptr;
    RunCallback run_callback_;
    CloseCallback close_callback_;
    std::vector<Entry> entries_;
    std::vector<std::string> entry_labels_;
    std::vector<ftxui::Box> entry_boxes_;
    int selected_entry_ = 0;
    int last_clicked_entry_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};
    ftxui::Component menu_;
    ftxui::Component container_;
};

class SidebarShortcutModal {
public:
    SidebarShortcutModal(
        const Theme* theme,
        SidebarShortcutModalContent::RunCallback run_callback,
        SidebarShortcutModalContent::CloseCallback close_callback);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    SidebarShortcutModalContent::RunCallback run_callback_;
    SidebarShortcutModalContent::CloseCallback request_close_callback_;
    std::shared_ptr<SidebarShortcutModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
