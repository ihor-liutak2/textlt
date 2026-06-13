#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "theme.hpp"

namespace textlt {

enum class FilePromptMode {
    None,
    Open,
    SaveAs,
};

class FileDialog {
public:
    using ConfirmCallback =
        std::function<bool(FilePromptMode mode, const std::string& path, std::string& error)>;

    FileDialog(const Theme* theme, ConfirmCallback on_confirm);

    ftxui::Component View() const;
    void Open(FilePromptMode mode, const std::string& current_path);
    void Close();
    bool IsOpen() const;
    void TakeFocus();

private:
    void Confirm();
    ftxui::Element Render();

    ConfirmCallback on_confirm_;
    const Theme* theme_ = nullptr;
    FilePromptMode mode_ = FilePromptMode::None;
    std::string title_;
    std::string path_;
    std::string error_;
    ftxui::Component input_;
    ftxui::Component container_;
    ftxui::Component renderer_;
};

} // namespace textlt
