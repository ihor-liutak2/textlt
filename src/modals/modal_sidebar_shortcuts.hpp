#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

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
    std::string GetTitle() override { return "Sidebar"; }
    ModalSizePreference GetModalSizePreference() const override { return {44, 13}; }
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
    };

    void TriggerEntry(size_t index);
    bool IsEntryKey(ftxui::Event event, char key) const;

    const Theme* theme_ = nullptr;
    RunCallback run_callback_;
    CloseCallback close_callback_;
    std::vector<Entry> entries_;
    std::vector<std::string> entry_labels_;
    int selected_entry_ = 0;
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
