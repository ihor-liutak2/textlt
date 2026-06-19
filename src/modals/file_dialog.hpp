#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "theme.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"

namespace textlt {

enum class FilePromptMode {
    None,
    Open,
    SaveAs,
};

class FileDialogContent : public IModalContent {
public:
    using ConfirmAction = std::function<void()>;

    FileDialogContent(const Theme* theme,
                      std::string* path,
                      std::string* error,
                      ConfirmAction on_confirm);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return title_; }

    void Configure(const std::string& title, size_t cursor_position);
    void SetTheme(const Theme* theme) { theme_ = theme; }
    void TakeFocus();

private:
    void RebuildInput(size_t cursor_position);

    const Theme* theme_ = nullptr;
    std::string* path_ = nullptr;
    std::string* error_ = nullptr;
    std::string title_ = "File";
    ConfirmAction on_confirm_;
    ftxui::Component input_;
    ftxui::Component container_;
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
    void RebuildModal();

    ConfirmCallback on_confirm_;
    const Theme* theme_ = nullptr;
    FilePromptMode mode_ = FilePromptMode::None;
    std::string title_;
    std::string path_;
    std::string error_;
    std::shared_ptr<FileDialogContent> content_impl_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
