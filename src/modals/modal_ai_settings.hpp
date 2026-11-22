#pragma once

#include <functional>
#include <memory>

#include "editor_config.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "modals/modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class AiSettingsModalContent;

class AiSettingsModal {
public:
    AiSettingsModal(
        const Theme* theme,
        EditorConfig* config,
        std::function<void()> request_redraw = {},
        std::function<std::string()> read_clipboard = {},
        std::function<bool(const std::string&)> write_clipboard = {});

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    std::shared_ptr<AiSettingsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
