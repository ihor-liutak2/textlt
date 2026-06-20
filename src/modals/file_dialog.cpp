#include "file_dialog.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "file_utils.hpp"

namespace textlt {
namespace {

constexpr int kFileModalWidth = 72;
constexpr int kFileModalHeight = 8;

bool HasTrailingSeparator(const std::string& path) {
    return !path.empty() && (path.back() == '/' || path.back() == '\\');
}

std::string SaveAsDisplayPath(const std::string& current_path) {
    if (current_path.empty() ||
        current_path == "Untitled" ||
        current_path == "untitled.txt") {
        return "";
    }

    std::error_code error;
    const std::filesystem::path path(current_path);
    std::filesystem::path display_path = path.is_absolute()
        ? path
        : std::filesystem::absolute(path, error);
    if (error) {
        display_path = path;
    }

    std::string result = display_path.lexically_normal().string();
    if (HasTrailingSeparator(current_path) && !HasTrailingSeparator(result)) {
        result += std::filesystem::path::preferred_separator;
    }
    return result;
}

std::string OpenDisplayPath(const std::string& current_path) {
    std::filesystem::path path;
    if (current_path.empty() ||
        current_path == "Untitled" ||
        current_path == "untitled.txt") {
        path = std::filesystem::current_path();
    } else {
        std::error_code error;
        path = std::filesystem::path(current_path);
        path = path.is_absolute() ? path : std::filesystem::absolute(path, error);
        if (error) {
            path = std::filesystem::path(current_path);
        }

        if (!HasTrailingSeparator(current_path)) {
            std::error_code status_error;
            if (!std::filesystem::is_directory(path, status_error)) {
                path = path.parent_path();
            }
        }
    }

    std::string result = path.lexically_normal().string();
    if (result.empty()) {
        result = std::filesystem::current_path().string();
    }
    if (!HasTrailingSeparator(result)) {
        result += std::filesystem::path::preferred_separator;
    }
    return result;
}

} // namespace

FileDialogContent::FileDialogContent(const Theme* theme,
                                     std::string* path,
                                     std::string* error,
                                     ConfirmAction on_confirm)
    : theme_(theme),
      path_(path),
      error_(error),
      on_confirm_(std::move(on_confirm)) {
    RebuildInput(0);
}

void FileDialogContent::Configure(const std::string& title, size_t cursor_position) {
    title_ = title.empty() ? "File" : title;
    if (title_ == "Create Folder" || title_ == "Delete Folder" || title_ == "Delete File") {
        label_ = "Name";
        placeholder_ = title_ == "Delete File" ? "file name" : "folder name";
    } else {
        label_ = "Path";
        placeholder_ = "path/to/file";
    }
    RebuildInput(cursor_position);
}

void FileDialogContent::RebuildInput(size_t cursor_position) {
    ftxui::InputOption input_option;
    input_option.on_enter = [this] {
        if (on_confirm_) {
            on_confirm_();
        }
    };
    input_option.multiline = false;
    input_option.cursor_position = static_cast<int>(cursor_position);
    input_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };

    input_ = ftxui::Input(path_, placeholder_, input_option);
    container_ = ftxui::Container::Vertical({input_});
}

void FileDialogContent::TakeFocus() {
    if (input_) {
        input_->TakeFocus();
    }
}

ftxui::Element FileDialogContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element error_line = text("");
    if (error_ && !error_->empty()) {
        error_line = text(" Error: " + *error_) | color(Color::Red);
    }

    return vbox({
        hbox({
            text(" " + label_ + ": ") | color(theme.modal_text_color),
            input_->Render() | xflex | bgcolor(theme.modal_input_bg),
        }),
        error_line,
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color);
}

FileDialog::FileDialog(const Theme* theme, ConfirmCallback on_confirm)
    : on_confirm_(std::move(on_confirm)),
      theme_(theme) {
    content_impl_ = std::make_shared<FileDialogContent>(
        theme_,
        &path_,
        &error_,
        [this] { Confirm(); });
    modal_window_ = std::make_shared<ModalWindow>(
        content_impl_,
        theme_,
        [this] { Close(); });
    RebuildModal();
}

void FileDialog::RebuildModal() {
    if (!modal_window_) {
        return;
    }

    modal_window_->SetTheme(theme_);
    modal_window_->SetBodyFrameScrolling(false);
    modal_window_->SetModalSize(kFileModalWidth, kFileModalHeight);
    modal_window_->SetFooterText("Enter confirms, Escape cancels.");
    modal_window_->SetFooterButtons({
        {"Confirm", [this] { Confirm(); }},
        {"Cancel", [this] { Close(); }},
    });
}

ftxui::Component FileDialog::View() const {
    return modal_window_;
}

void FileDialog::Open(FilePromptMode mode, const std::string& current_path) {
    mode_ = mode;
    error_.clear();
    switch (mode) {
        case FilePromptMode::Open:
            title_ = "Open File";
            path_ = OpenDisplayPath(current_path);
            break;
        case FilePromptMode::SaveAs:
            title_ = "Save File";
            path_ = SaveAsDisplayPath(current_path);
            break;
        case FilePromptMode::CreateFolder:
            title_ = "Create Folder";
            path_.clear();
            break;
        case FilePromptMode::DeleteFolder:
            title_ = "Delete Folder";
            path_.clear();
            break;
        case FilePromptMode::DeleteFile:
            title_ = "Delete File";
            path_ = current_path;
            break;
        case FilePromptMode::None:
            title_ = "File";
            path_.clear();
            break;
    }

    content_impl_->SetTheme(theme_);
    content_impl_->Configure(title_, path_.size());
    RebuildModal();
    modal_window_->RefreshChildren();
    TakeFocus();
}

void FileDialog::Close() {
    mode_ = FilePromptMode::None;
    error_.clear();
}

bool FileDialog::IsOpen() const {
    return mode_ != FilePromptMode::None;
}

void FileDialog::TakeFocus() {
    if (content_impl_) {
        content_impl_->TakeFocus();
    }
}

void FileDialog::Confirm() {
    std::string entered_path = TrimTrailingNewlines(path_);
    path_ = entered_path;

    if (entered_path.empty()) {
        if (mode_ == FilePromptMode::CreateFolder || mode_ == FilePromptMode::DeleteFolder) {
            error_ = "Enter a folder name.";
        } else if (mode_ == FilePromptMode::DeleteFile) {
            error_ = "Enter a file name.";
        } else {
            error_ = "Enter a file path.";
        }
        return;
    }

    if (mode_ == FilePromptMode::SaveAs) {
        entered_path = EnsureTextExtension(entered_path);
        path_ = entered_path;
    }

    std::string callback_error;
    if (on_confirm_ && on_confirm_(mode_, entered_path, callback_error)) {
        Close();
        return;
    }
    error_ = callback_error.empty() ? "File action failed." : callback_error;
}

} // namespace textlt
