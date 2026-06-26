#include "modal_import_text.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

std::string FormatKindPrefix(const TextImportEntry& entry) {
    switch (entry.kind) {
        case TextImportEntryKind::ParentDirectory:
            return "[..]  ";
        case TextImportEntryKind::Directory:
            return "[DIR] ";
        case TextImportEntryKind::File:
            break;
    }

    switch (entry.format) {
        case TextImportFormat::Docx:
            return "[DOCX]";
        case TextImportFormat::Fb2:
            return "[FB2] ";
        case TextImportFormat::Unsupported:
            return "[FILE]";
    }

    return "[FILE]";
}

bool IsBackspaceEvent(const ftxui::Event& event) {
    const std::string input = event.input();
    return event == ftxui::Event::Backspace ||
        input == "\x7f" ||
        input == "\b";
}

} // namespace

ImportTextModalContent::ImportTextModalContent(
    const Theme* theme,
    StartDirectoryProvider start_directory_provider,
    ImportTextCallback on_import_text,
    CloseCallback on_close)
    : theme_(theme),
      start_directory_provider_(std::move(start_directory_provider)),
      on_import_text_(std::move(on_import_text)),
      on_close_(std::move(on_close)) {
    ftxui::MenuOption entry_option = ftxui::MenuOption::Vertical();
    entry_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(TrimForDisplay(state.label, 84));
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };

    entry_menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, entry_option);
    entry_list_component_ = ftxui::CatchEvent(entry_menu_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ActivateSelected();
            return true;
        }
        if (IsBackspaceEvent(event)) {
            NavigateUp();
            return true;
        }
        return false;
    });

    import_button_ = MakeTextButton("Import", [this] { ImportSelected(); });
    up_button_ = MakeTextButton("Up", [this] { NavigateUp(); });
    refresh_button_ = MakeTextButton("Refresh", [this] { Refresh(); });

    container_ = ftxui::Container::Vertical({
        entry_list_component_,
        ftxui::Container::Horizontal({
            import_button_,
            up_button_,
            refresh_button_,
        }),
    });
}

ftxui::Component ImportTextModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };

    return ftxui::Button(option);
}

void ImportTextModalContent::Open() {
    std::filesystem::path directory;
    if (start_directory_provider_) {
        directory = start_directory_provider_();
    }
    if (directory.empty()) {
        directory = std::filesystem::current_path();
    }
    LoadDirectory(directory);
    if (entry_list_component_) {
        entry_list_component_->TakeFocus();
    }
}

void ImportTextModalContent::Close() {
    status_ = "Select a .docx or .fb2 file to import.";
    last_clicked_entry_ = -1;
}

void ImportTextModalContent::Refresh() {
    LoadDirectory(current_directory_);
}

void ImportTextModalContent::LoadDirectory(const std::filesystem::path& directory) {
    std::error_code error_code;
    std::filesystem::path target = directory;
    if (!std::filesystem::is_directory(target, error_code)) {
        target = target.parent_path();
    }
    if (target.empty() || !std::filesystem::is_directory(target, error_code)) {
        target = std::filesystem::current_path(error_code);
    }

    std::string error;
    std::vector<TextImportEntry> next_entries = importer_.ListDirectory(target, error);
    if (!error.empty()) {
        status_ = error;
        return;
    }

    current_directory_ = target.lexically_normal();
    entries_ = std::move(next_entries);
    selected_entry_ = 0;
    last_clicked_entry_ = -1;
    RebuildEntryLabels();

    status_ = entries_.empty()
        ? "No folders, .docx files, or .fb2 files in this folder."
        : "Enter/double click opens or imports. Backspace goes up.";
}

void ImportTextModalContent::NavigateUp() {
    if (current_directory_.empty()) {
        return;
    }

    const std::filesystem::path parent = current_directory_.parent_path();
    if (parent.empty() || parent == current_directory_) {
        status_ = "Already at filesystem root.";
        return;
    }

    LoadDirectory(parent);
}

void ImportTextModalContent::ActivateSelected() {
    if (entries_.empty()) {
        status_ = "No item selected.";
        return;
    }

    ClampSelection();
    const TextImportEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
    if (entry.kind == TextImportEntryKind::ParentDirectory ||
        entry.kind == TextImportEntryKind::Directory) {
        LoadDirectory(entry.path);
        return;
    }

    ImportSelected();
}

void ImportTextModalContent::ImportSelected() {
    if (entries_.empty()) {
        status_ = "No file selected.";
        return;
    }

    ClampSelection();
    const TextImportEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
    if (entry.kind != TextImportEntryKind::File) {
        status_ = "Selected item is a folder. Press Enter to open it.";
        return;
    }

    TextImportResult result = importer_.ImportFile(entry.path);
    if (!result.success) {
        status_ = result.error.empty() ? "Import failed." : result.error;
        return;
    }

    std::string error;
    if (on_import_text_ && !on_import_text_(entry.path, result.text, error)) {
        status_ = error.empty() ? "Imported text could not be inserted." : error;
        return;
    }

    status_ = "Imported " + entry.path.filename().string();
    if (on_close_) {
        on_close_();
    }
}

void ImportTextModalContent::MoveSelection(int delta) {
    if (entries_.empty()) {
        selected_entry_ = 0;
        return;
    }

    selected_entry_ += delta;
    ClampSelection();
}

void ImportTextModalContent::ClampSelection() {
    if (entries_.empty()) {
        selected_entry_ = 0;
        return;
    }

    selected_entry_ = std::clamp(
        selected_entry_,
        0,
        static_cast<int>(entries_.size()) - 1);
}

void ImportTextModalContent::RebuildEntryLabels() {
    entry_labels_.clear();
    entry_labels_.reserve(entries_.size());
    for (const TextImportEntry& entry : entries_) {
        entry_labels_.push_back(FormatEntryLabel(entry));
    }
    ClampSelection();
}

ftxui::Element ImportTextModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Import Text ") | bold | color(theme.modal_accent),
        text(" "),
        text(TrimForDisplay(FormatCurrentDirectory(), 58)) |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element ImportTextModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        hbox({
            text(" Folder: ") | bold | color(theme.modal_accent),
            text(TrimForDisplay(FormatCurrentDirectory(), 74)) |
                color(theme.modal_text_color),
        }),
        separator() | color(theme.modal_border),
        RenderEntryList() | flex,
        separator() | color(theme.modal_border),
        hbox({
            import_button_->Render(),
            text(" "),
            up_button_->Render(),
            text(" "),
            refresh_button_->Render(),
            filler(),
            text(TrimForDisplay(FormatSelectedFile(), 36)) |
                dim |
                color(theme.modal_text_color),
        }),
        RenderHelpLine(),
    }) |
        bgcolor(theme.modal_background);
}

ftxui::Element ImportTextModalContent::RenderEntryList() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (entry_labels_.empty()) {
        return text("No folders, .docx files, or .fb2 files.") |
            color(theme.modal_text_color) |
            frame;
    }

    return entry_list_component_->Render() |
        vscroll_indicator |
        frame;
}

ftxui::Element ImportTextModalContent::RenderHelpLine() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return hbox({
        text(" Enter/double click: open/import ") |
            dim |
            color(theme.modal_text_color),
        text(" "),
        text(" Backspace: up ") |
            dim |
            color(theme.modal_text_color),
        text(" "),
        text(" Esc: close ") |
            dim |
            color(theme.modal_text_color),
    });
}

bool ImportTextModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        MoveSelection(1);
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        MoveSelection(-1);
        return true;
    }

    if (event == ftxui::Event::PageDown) {
        MoveSelection(10);
        return true;
    }

    if (event == ftxui::Event::PageUp) {
        MoveSelection(-10);
        return true;
    }

    if (event == ftxui::Event::Return || event.input() == "\x0A") {
        ActivateSelected();
        return true;
    }

    if (IsBackspaceEvent(event)) {
        NavigateUp();
        return true;
    }

    if (event.input() == "r" || event.input() == "R") {
        Refresh();
        return true;
    }

    return HandleEntryMouseEvent(event);
}

bool ImportTextModalContent::HandleEntryMouseEvent(ftxui::Event event) {
    if (!event.is_mouse()) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();
    if (mouse.button == ftxui::Mouse::WheelDown) {
        MoveSelection(3);
        return true;
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
        MoveSelection(-3);
        return true;
    }

    if (!entry_menu_ || !entry_menu_->Focused()) {
        return false;
    }

    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }

    if (entries_.empty()) {
        return false;
    }

    ClampSelection();
    const int clicked_entry = selected_entry_;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_entry_click_time_).count();

    const bool is_double_click =
        last_clicked_entry_ == clicked_entry &&
        elapsed_ms >= 0 &&
        elapsed_ms <= 500;

    last_entry_click_time_ = now;
    last_clicked_entry_ = clicked_entry;

    if (is_double_click) {
        ActivateSelected();
        last_clicked_entry_ = -1;
        return true;
    }

    return false;
}

std::string ImportTextModalContent::FormatEntryLabel(const TextImportEntry& entry) const {
    std::string label = FormatKindPrefix(entry);
    if (entry.kind == TextImportEntryKind::Directory) {
        label += " ";
        label += entry.name;
        label += "/";
        return label;
    }

    label += " ";
    label += entry.name;
    return label;
}

std::string ImportTextModalContent::FormatCurrentDirectory() const {
    if (current_directory_.empty()) {
        return std::filesystem::current_path().string();
    }
    return current_directory_.string();
}

std::string ImportTextModalContent::FormatSelectedFile() const {
    if (entries_.empty()) {
        return "No selection";
    }

    const int safe_index = std::clamp(
        selected_entry_,
        0,
        static_cast<int>(entries_.size()) - 1);
    const TextImportEntry& entry = entries_[static_cast<size_t>(safe_index)];
    if (entry.kind == TextImportEntryKind::File) {
        return "Selected: " + entry.name;
    }
    return "Selected folder: " + entry.name;
}

std::string ImportTextModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }

    if (max_size <= 3) {
        return text.substr(0, max_size);
    }

    return text.substr(0, max_size - 3) + "...";
}

ImportTextModal::ImportTextModal(
    const Theme* theme,
    StartDirectoryProvider start_directory_provider,
    ImportTextCallback on_import_text)
    : theme_(theme) {
    content_ = std::make_shared<ImportTextModalContent>(
        theme_,
        std::move(start_directory_provider),
        std::move(on_import_text),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Import", [this] {
            if (content_) {
                content_->ImportSelected();
            }
        }},
        {"Refresh", [this] {
            if (content_) {
                content_->Refresh();
            }
        }},
        {"Cancel", [this] { Close(); }},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component ImportTextModal::View() const {
    return modal_;
}

void ImportTextModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void ImportTextModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool ImportTextModal::IsOpen() const {
    return open_;
}

bool ImportTextModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (content_ && event.is_mouse()) {
        const bool modal_handled = modal_->OnEvent(event);
        if (content_->HandleEvent(std::move(event))) {
            return true;
        }
        return modal_handled;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}

} // namespace textlt