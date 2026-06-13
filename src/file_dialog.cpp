#include "file_dialog.hpp"

#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "file_utils.hpp"

namespace textlt {

FileDialog::FileDialog(const Theme* theme, ConfirmCallback on_confirm)
    : on_confirm_(std::move(on_confirm)),
      theme_(theme) {
    
    ftxui::InputOption input_option;
    input_option.on_enter = [this] { Confirm(); };
    
    // Correctly catch the InputState structure from FTXUI
    input_option.transform = [this](ftxui::InputState state) {
        const Theme& current_theme = theme_ ? *theme_ : FallbackTheme();
        
        // Directly style the internal element provided by the state object
        return state.element | 
               ftxui::bgcolor(current_theme.modal_input_bg) | 
               ftxui::color(current_theme.modal_input_fg);
    };

    input_ = ftxui::Input(&path_, "path/to/file", input_option);
    container_ = ftxui::Container::Vertical({input_});
    renderer_ = ftxui::Renderer(container_, [this] { return Render(); });
}

ftxui::Component FileDialog::View() const {
    return renderer_;
}

void FileDialog::Open(FilePromptMode mode, const std::string& current_path) {
    mode_ = mode;
    error_.clear();
    if (mode == FilePromptMode::Open) {
        title_ = "Open File";
        path_.clear();
    } else {
        title_ = "Save File";
        path_ = current_path == "untitled.txt" ? std::string() : current_path;
    }
    
    // Configure standard layout options for the input field component
    ftxui::InputOption input_option;
    input_option.on_enter = [this] { Confirm(); };
    
    // CRITICAL CURSOR FIX: Securely position the terminal cursor at the end of the text string
    input_option.cursor_position = static_cast<int>(path_.size());
    
    input_option.transform = [this](ftxui::InputState state) {
        const Theme& current_theme = theme_ ? *theme_ : FallbackTheme();
        return state.element | 
               ftxui::bgcolor(current_theme.modal_input_bg) | 
               ftxui::color(current_theme.modal_input_fg);
    };
    
    // Reassemble component references to snap focus properties correctly
    input_ = ftxui::Input(&path_, "path/to/file", input_option);
    container_ = ftxui::Container::Vertical({input_});
    renderer_ = ftxui::Renderer(container_, [this] { return Render(); });

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
    input_->TakeFocus();
}

void FileDialog::Confirm() {
    std::string entered_path = TrimTrailingNewlines(path_);
    path_ = entered_path;

    if (entered_path.empty()) {
        error_ = "Enter a file path.";
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

ftxui::Element FileDialog::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements prompt_lines = {
        // Modal window header with padding space
        text(" " + title_ + " ") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        
        // Input row block with forced background padding matching input field
        hbox({
            text(" Path: ") | color(theme.modal_text_color),
            input_->Render() | xflex | bgcolor(theme.modal_input_bg),
        }),
        
        separator() | color(theme.modal_border),
        
        // Bottom helper hint text
        text(" Enter confirms, Escape cancels. ") |
            dim |
            color(theme.modal_text_color),
    };

    // If an action generated an error, append it at the bottom
    if (!error_.empty()) {
        prompt_lines.push_back(text(" Error: " + error_) | color(Color::Red));
    }

    // Assemble outer dialog frame step-by-step to prevent style bleeding
    // Use clear_under and double bgcolor to force the terminal to paint the border area
    return vbox({
        vbox(std::move(prompt_lines)) 
            | bgcolor(theme.modal_background) 
            | color(theme.modal_text_color)
    }) | border 
       | bgcolor(theme.modal_background) // Forces the background color onto the border lines too
       | color(theme.modal_border) 
       | clear_under
       | size(WIDTH, GREATER_THAN, 48);
}

} // namespace textltz