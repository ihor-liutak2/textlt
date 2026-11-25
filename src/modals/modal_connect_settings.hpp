#pragma once

#include <functional>
#include <memory>

#include "ftxui/component/component.hpp"

namespace textlt {

namespace connect_ui {
class ConnectWorkspace;
}
class ModalWindow;
class ConnectSettingsContent;
struct Theme;

enum class ConnectSettingsTab { LocalSend, Diagnostics };

class ConnectSettingsModal {
public:
    void Configure(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                   std::function<bool(const std::string&)> copy, std::function<void()> close);
    void Open(ConnectSettingsTab tab);
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    ftxui::Component View() const;

private:
    bool open_ = false;
    std::shared_ptr<ConnectSettingsContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
