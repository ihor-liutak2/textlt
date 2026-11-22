#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ai/ai_backend.hpp"
#include "modals/modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class QuickAiModalContent;

class QuickAiModal {
public:
    using ApplyResult = std::function<bool(const std::string&, std::string&)>;

    QuickAiModal(const Theme* theme, std::function<void()> redraw,
        std::function<void()> request_close, std::function<std::string()> read_clipboard,
        std::function<bool(const std::string&)> write_clipboard);

    ftxui::Component View() const;
    void Open(AiBackendSettings settings, std::string source, ApplyResult apply = {});
    void Close();
    bool IsOpen() const { return open_; }

private:
    std::shared_ptr<QuickAiModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
    bool open_ = false;
};

} // namespace textlt
