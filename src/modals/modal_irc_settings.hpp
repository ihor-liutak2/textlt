#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"

namespace textlt {

struct Theme;
class ModalWindow;
class IrcSettingsContent;
namespace connect { class IrcProvider; }

class IrcSettingsModal {
public:
    void Configure(const Theme& theme, connect::IrcProvider& provider, std::filesystem::path settings_path,
                   std::function<void()> close, std::function<void()> redraw,
                   std::function<bool(const std::string&)> write_clipboard);
    void Open();
    void Close();
    bool IsOpen() const { return open_; }
    ftxui::Component View() const;

private:
    bool open_ = false;
    std::shared_ptr<IrcSettingsContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
