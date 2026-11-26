#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ftxui/component/component.hpp"
#include "telegram/telegram_provider.hpp"
#include "theme.hpp"

namespace textlt {

class ModalWindow;
class TelegramSettingsContent;

class TelegramSettingsModal {
public:
    using ClipboardPaste = std::function<void(std::optional<std::string>)>;
    using ClipboardRead = std::function<void(ClipboardPaste)>;

    void Configure(
        const Theme& theme,
        telegram::TelegramProvider& provider,
        std::function<void()> close,
        std::function<void()> request_redraw = {},
        ClipboardRead read_clipboard = {});
    void Open();
    void Close();
    bool IsOpen() const { return open_; }
    ftxui::Component View() const;

private:
    bool open_ = false;
    std::shared_ptr<TelegramSettingsContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
