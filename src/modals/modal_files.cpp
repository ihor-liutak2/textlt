#include "modal_files.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr int kVisibleEntryRows = 14;
constexpr int kEntryNameWidth = 72;
constexpr int kEntrySizeWidth = 12;
constexpr int kMinDoubleClickMs = 80;
constexpr int kMaxDoubleClickMs = 500;

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

std::string FileTypeLabel(FileEntryType type) {
    switch (type) {
        case FileEntryType::Directory:
            return "DIR";
        case FileEntryType::File:
            return "FILE";
        case FileEntryType::Symlink:
            return "LINK";
        case FileEntryType::Other:
            return "OTHER";
    }
    return "OTHER";
}

std::string WithTrailingSeparator(std::filesystem::path path) {
    std::string value = path.lexically_normal().string();
    if (!value.empty() && value.back() != '/' && value.back() != '\\') {
        value += std::filesystem::path::preferred_separator;
    }
    return value;
}

} // namespace

FilesModalContent::FilesModalContent(
    const Theme* theme,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    FavoriteDirectoriesProvider favorite_directories_provider,
    AddFavoriteDirectoryCallback on_add_favorite_directory,
    CopyPathCallback on_copy_path,
    ConfirmPathCallback on_confirm_path,
    CloseCallback on_close)
    : theme_(theme),
      file_manager_(file_manager),
      start_directory_provider_(std::move(start_directory_provider)),
      favorite_directories_provider_(std::move(favorite_directories_provider)),
      on_add_favorite_directory_(std::move(on_add_favorite_directory)),
      on_copy_path_(std::move(on_copy_path)),
      on_confirm_path_(std::move(on_confirm_path)),
      on_close_(std::move(on_close)) {
    RebuildComponents();
}

ftxui::Component FilesModalContent::MakeTextButton(
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

void FilesModalContent::RebuildComponents() {
    home_button_ = MakeTextButton("Home", [this] {
        LoadBuiltInDirectory(FileManager::UserHomeDirectory());
    });
    documents_button_ = MakeTextButton("Documents", [this] {
        LoadBuiltInDirectory(FileManager::UserDocumentsDirectory());
    });
    downloads_button_ = MakeTextButton("Download", [this] {
        LoadBuiltInDirectory(FileManager::UserDownloadsDirectory());
    });
    current_dir_button_ = MakeTextButton("Current Dir", [this] {
        std::filesystem::path directory;
        if (start_directory_provider_) {
            directory = start_directory_provider_();
        }
        LoadBuiltInDirectory(directory);
    });
    add_dir_button_ = MakeTextButton("Add Dir", [this] {
        AddCurrentDirectoryToFavorites();
    });
    refresh_button_ = MakeTextButton("Refresh", [this] { Refresh(); });
    copy_path_button_ = MakeTextButton("Copy Path", [this] {
        CopySelectedPathText();
    });

    create_dir_button_ = MakeTextButton("Create Dir", [this] {
        SetStatus("Create Dir will be enabled in the file operation patch.");
    });
    create_file_button_ = MakeTextButton("Create File", [this] {
        SetStatus("Create File will be enabled in the file operation patch.");
    });
    delete_button_ = MakeTextButton("Delete", [this] {
        SetStatus("Delete will be enabled in the file operation patch.");
    });
    rename_button_ = MakeTextButton("Rename", [this] {
        SetStatus("Rename will be enabled in the file operation patch.");
    });
    copy_button_ = MakeTextButton("Copy", [this] {
        SetStatus("File copy will be enabled in the file operation patch.");
    });
    cut_button_ = MakeTextButton("Cut", [this] {
        SetStatus("File cut will be enabled in the file operation patch.");
    });
    paste_button_ = MakeTextButton("Paste", [this] {
        SetStatus("Paste will be enabled in the file operation patch.");
    });

    ftxui::InputOption path_option;
    path_option.multiline = false;
    path_option.cursor_position = &path_input_cursor_;
    path_option.on_enter = [this] { LoadPathFromInput(); };
    path_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };
    path_input_ = ftxui::Input(&path_input_value_, "directory path", path_option);

    ftxui::InputOption file_option = path_option;
    file_option.cursor_position = &file_name_input_cursor_;
    file_option.on_enter = [this] { ConfirmSelected(); };
    file_name_input_ = ftxui::Input(&file_name_input_value_, "file name", file_option);

    entry_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderEntryList(); }),
        [this](ftxui::Event event) { return HandleEvent(event); });

    container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            home_button_,
            documents_button_,
            downloads_button_,
            current_dir_button_,
            add_dir_button_,
            refresh_button_,
            copy_path_button_,
        }),
        path_input_,
        file_name_input_,
        ftxui::Container::Horizontal({
            create_dir_button_,
            create_file_button_,
            delete_button_,
            rename_button_,
            copy_button_,
            cut_button_,
            paste_button_,
        }),
        entry_list_component_,
    });
}

void FilesModalContent::Open(
    FilesModalMode mode,
    const std::filesystem::path& start_path,
    std::string suggested_file_name) {
    mode_ = mode;
    status_is_error_ = false;
    status_ = "Ready.";
    file_name_input_value_ = std::move(suggested_file_name);
    file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
    RefreshFavorites();

    std::filesystem::path directory = start_path;
    std::error_code error_code;
    if (directory.empty()) {
        if (start_directory_provider_) {
            directory = start_directory_provider_();
        }
    }
    if (directory.empty()) {
        directory = std::filesystem::current_path(error_code);
    }
    if (!std::filesystem::is_directory(directory, error_code)) {
        const std::string file_name = SuggestedFileNameFromPath(directory);
        if (file_name_input_value_.empty()) {
            file_name_input_value_ = file_name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
        directory = directory.parent_path();
    }
    if (directory.empty() || !std::filesystem::is_directory(directory, error_code)) {
        directory = std::filesystem::current_path(error_code);
    }

    LoadDirectory(directory);
    if (entry_list_component_) {
        entry_list_component_->TakeFocus();
    }
}

void FilesModalContent::Close() {
    mode_ = FilesModalMode::None;
    entries_.clear();
    favorite_boxes_.clear();
    entry_boxes_.clear();
    last_clicked_entry_ = -1;
}

std::string FilesModalContent::GetTitle() {
    return ModeTitle();
}

ftxui::Element FilesModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" " + ModeTitle() + " ") | bold | color(theme.modal_accent),
        text(" "),
        text(TrimForDisplay(FormatCurrentDirectory(), 72)) |
            dim |
            color(theme.modal_text_color),
    });
}

void FilesModalContent::Refresh() {
    LoadDirectory(current_directory_);
}

void FilesModalContent::RefreshFavorites() {
    favorite_directories_.clear();
    if (favorite_directories_provider_) {
        favorite_directories_ = favorite_directories_provider_();
    }
}

void FilesModalContent::LoadDirectory(const std::filesystem::path& directory) {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }

    std::error_code status_error;
    std::filesystem::path target = directory;
    if (target.empty()) {
        target = std::filesystem::current_path(status_error);
    }
    if (!std::filesystem::is_directory(target, status_error)) {
        SetStatus("Directory does not exist: " + target.string(), true);
        return;
    }

    std::vector<FileEntry> listed_entries;
    std::string error;
    if (!file_manager_->ListDirectory(target, FilterForMode(), listed_entries, error)) {
        SetStatus(error.empty() ? "Unable to read directory." : error, true);
        return;
    }

    current_directory_ = target.lexically_normal();
    entries_.clear();

    const std::filesystem::path parent = current_directory_.parent_path();
    if (!parent.empty() && parent != current_directory_) {
        FileEntry parent_entry;
        parent_entry.path = parent;
        parent_entry.name = "..";
        parent_entry.type = FileEntryType::Directory;
        entries_.push_back(std::move(parent_entry));
    }

    entries_.insert(entries_.end(), listed_entries.begin(), listed_entries.end());
    selected_entry_ = entries_.empty() ? 0 : std::min(selected_entry_, static_cast<int>(entries_.size()) - 1);
    scroll_offset_ = 0;
    path_input_value_ = current_directory_.string();
    path_input_cursor_ = static_cast<int>(path_input_value_.size());
    EnsureSelectionVisible();

    if (entries_.empty()) {
        SetStatus("Directory is empty.");
    } else {
        SetStatus("Enter opens folders or confirms selected file. Double click works too.");
    }
}

void FilesModalContent::LoadPathFromInput() {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }

    std::string error;
    const std::filesystem::path resolved = FileManager::ResolvePath(
        path_input_value_,
        current_directory_,
        error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }

    std::error_code status_error;
    if (std::filesystem::is_directory(resolved, status_error)) {
        LoadDirectory(resolved);
        return;
    }

    if (std::filesystem::is_regular_file(resolved, status_error)) {
        LoadDirectory(resolved.parent_path());
        const std::string name = resolved.filename().string();
        for (size_t index = 0; index < entries_.size(); ++index) {
            if (entries_[index].name == name) {
                SelectEntry(static_cast<int>(index));
                break;
            }
        }
        if (IsSaveLikeMode()) {
            file_name_input_value_ = name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
        return;
    }

    if (IsSaveLikeMode() && !resolved.parent_path().empty() &&
        std::filesystem::is_directory(resolved.parent_path(), status_error)) {
        LoadDirectory(resolved.parent_path());
        file_name_input_value_ = resolved.filename().string();
        file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        SetStatus("Target file name prepared: " + file_name_input_value_);
        return;
    }

    SetStatus("Path is not a directory or file: " + resolved.string(), true);
}

void FilesModalContent::LoadBuiltInDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        SetStatus("Directory is not available.", true);
        return;
    }
    LoadDirectory(directory);
}

void FilesModalContent::AddCurrentDirectoryToFavorites() {
    if (current_directory_.empty()) {
        SetStatus("No active directory to add.", true);
        return;
    }
    std::string error;
    if (on_add_favorite_directory_ &&
        !on_add_favorite_directory_(current_directory_, error)) {
        SetStatus(error.empty() ? "Directory was not added." : error, true);
        return;
    }
    RefreshFavorites();
    SetStatus("Added directory: " + current_directory_.string());
}

void FilesModalContent::CopySelectedPathText() {
    std::vector<std::filesystem::path> paths;
    if (!entries_.empty()) {
        ClampSelection();
        const FileEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
        if (!IsParentEntry(entry)) {
            paths.push_back(entry.path);
        }
    }
    if (paths.empty() && !current_directory_.empty()) {
        paths.push_back(current_directory_);
    }

    const std::string text = FileManager::BuildPathText(paths);
    if (text.empty()) {
        SetStatus("Nothing to copy.", true);
        return;
    }
    if (on_copy_path_) {
        on_copy_path_(text);
    }
    SetStatus("Copied path text.");
}

void FilesModalContent::ConfirmSelected() {
    if (!IsFileActionMode()) {
        SetStatus("Files mode is for navigation and file operations.");
        return;
    }

    std::filesystem::path target;
    std::string error;
    if (!TargetPathForMode(target, error)) {
        SetStatus(error, true);
        return;
    }

    if (on_confirm_path_ && !on_confirm_path_(mode_, target, error)) {
        SetStatus(error.empty() ? "Operation failed." : error, true);
        return;
    }

    SetStatus(FooterActionLabel() + " complete: " + target.string());
    if (on_close_) {
        on_close_();
    }
}

void FilesModalContent::NavigateUp() {
    if (current_directory_.empty()) {
        return;
    }
    const std::filesystem::path parent = current_directory_.parent_path();
    if (parent.empty() || parent == current_directory_) {
        SetStatus("Already at filesystem root.");
        return;
    }
    LoadDirectory(parent);
}

void FilesModalContent::ActivateSelected(bool double_click) {
    if (entries_.empty()) {
        SetStatus("No item selected.", true);
        return;
    }

    ClampSelection();
    const FileEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
    if (entry.type == FileEntryType::Directory) {
        LoadDirectory(entry.path);
        return;
    }

    if (IsSaveLikeMode()) {
        file_name_input_value_ = entry.name;
        file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        SetStatus("Selected target file name: " + entry.name);
        return;
    }

    if (double_click || mode_ == FilesModalMode::Open || mode_ == FilesModalMode::Import) {
        ConfirmSelected();
    }
}

void FilesModalContent::SelectEntry(int index) {
    if (entries_.empty()) {
        selected_entry_ = 0;
        scroll_offset_ = 0;
        return;
    }
    selected_entry_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    EnsureSelectionVisible();
    if (IsSaveLikeMode()) {
        const FileEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
        if (entry.type != FileEntryType::Directory) {
            file_name_input_value_ = entry.name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
    }
}

void FilesModalContent::MoveSelection(int delta) {
    if (entries_.empty()) {
        selected_entry_ = 0;
        scroll_offset_ = 0;
        return;
    }
    SelectEntry(selected_entry_ + delta);
}

void FilesModalContent::ClampSelection() {
    if (entries_.empty()) {
        selected_entry_ = 0;
        return;
    }
    selected_entry_ = std::clamp(selected_entry_, 0, static_cast<int>(entries_.size()) - 1);
}

void FilesModalContent::EnsureSelectionVisible() {
    if (entries_.empty()) {
        scroll_offset_ = 0;
        return;
    }
    ClampSelection();
    if (selected_entry_ < scroll_offset_) {
        scroll_offset_ = selected_entry_;
    }
    if (selected_entry_ >= scroll_offset_ + kVisibleEntryRows) {
        scroll_offset_ = selected_entry_ - kVisibleEntryRows + 1;
    }
    const int max_offset = std::max(0, static_cast<int>(entries_.size()) - kVisibleEntryRows);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_offset);
}

void FilesModalContent::SetStatus(std::string message, bool error) {
    status_ = std::move(message);
    status_is_error_ = error;
}

bool FilesModalContent::SelectedPath(std::filesystem::path& path, std::string& error) const {
    if (entries_.empty()) {
        error = "No file selected.";
        return false;
    }
    const int safe_index = std::clamp(
        selected_entry_,
        0,
        static_cast<int>(entries_.size()) - 1);
    const FileEntry& entry = entries_[static_cast<size_t>(safe_index)];
    if (IsParentEntry(entry)) {
        error = "Selected item is parent directory.";
        return false;
    }
    path = entry.path;
    return true;
}

bool FilesModalContent::TargetPathForMode(std::filesystem::path& path, std::string& error) const {
    if (IsSaveLikeMode()) {
        if (file_name_input_value_.empty()) {
            error = "Enter a file name.";
            return false;
        }
        if (!FileManager::IsPlainName(file_name_input_value_)) {
            error = "Enter only a file name.";
            return false;
        }
        path = current_directory_ / file_name_input_value_;
        return true;
    }

    if (!SelectedPath(path, error)) {
        return false;
    }

    std::error_code status_error;
    if (std::filesystem::is_directory(path, status_error)) {
        error = "Selected item is a directory.";
        return false;
    }
    return true;
}

FileFilter FilesModalContent::FilterForMode() const {
    FileFilter filter;
    filter.show_directories = true;
    filter.show_files = true;
    filter.show_hidden = true;
    switch (mode_) {
        case FilesModalMode::Import:
            filter.extensions = {".docx", ".fb2"};
            break;
        case FilesModalMode::Export:
            filter.extensions = {".txt", ".md"};
            break;
        case FilesModalMode::None:
        case FilesModalMode::Open:
        case FilesModalMode::SaveAs:
        case FilesModalMode::Manage:
            break;
    }
    return filter;
}

bool FilesModalContent::IsParentEntry(const FileEntry& entry) const {
    return entry.type == FileEntryType::Directory && entry.name == "..";
}

bool FilesModalContent::IsFileActionMode() const {
    return mode_ == FilesModalMode::Open ||
        mode_ == FilesModalMode::SaveAs ||
        mode_ == FilesModalMode::Import ||
        mode_ == FilesModalMode::Export;
}

bool FilesModalContent::IsSaveLikeMode() const {
    return mode_ == FilesModalMode::SaveAs || mode_ == FilesModalMode::Export;
}

bool FilesModalContent::HandleEvent(ftxui::Event event) {
    if (path_input_ && path_input_->Focused()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            LoadPathFromInput();
            return true;
        }
        return false;
    }

    if (file_name_input_ && file_name_input_->Focused()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ConfirmSelected();
            return true;
        }
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        MoveSelection(1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        MoveSelection(-1);
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        MoveSelection(kVisibleEntryRows);
        return true;
    }
    if (event == ftxui::Event::PageUp) {
        MoveSelection(-kVisibleEntryRows);
        return true;
    }
    if (event == ftxui::Event::Return || event.input() == "\x0A") {
        ActivateSelected(false);
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

    if (HandleFavoriteMouseEvent(event)) {
        return true;
    }
    return HandleEntryMouseEvent(event);
}

bool FilesModalContent::HandleEntryMouseEvent(ftxui::Event event) {
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

    if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }

    for (size_t visible_index = 0; visible_index < entry_boxes_.size(); ++visible_index) {
        if (!entry_boxes_[visible_index].Contain(mouse.x, mouse.y)) {
            continue;
        }

        const int entry_index = scroll_offset_ + static_cast<int>(visible_index);
        if (entry_index < 0 || entry_index >= static_cast<int>(entries_.size())) {
            return false;
        }

        SelectEntry(entry_index);
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_entry_click_time_).count();
        const bool is_double_click =
            last_clicked_entry_ == entry_index &&
            elapsed_ms >= kMinDoubleClickMs &&
            elapsed_ms <= kMaxDoubleClickMs;

        last_entry_click_time_ = now;
        last_clicked_entry_ = entry_index;

        if (is_double_click) {
            ActivateSelected(true);
            last_clicked_entry_ = -1;
        }
        return true;
    }
    return false;
}

bool FilesModalContent::HandleFavoriteMouseEvent(ftxui::Event event) {
    if (!event.is_mouse()) {
        return false;
    }
    const ftxui::Mouse& mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }

    for (size_t index = 0; index < favorite_boxes_.size(); ++index) {
        if (favorite_boxes_[index].Contain(mouse.x, mouse.y)) {
            if (index < favorite_directories_.size()) {
                last_clicked_entry_ = -1;
                LoadDirectory(favorite_directories_[index]);
                return true;
            }
        }
    }
    return false;
}

bool FilesModalContent::IsBackspaceEvent(const ftxui::Event& event) const {
    const std::string input = event.input();
    return event == ftxui::Event::Backspace || input == "\x7f" || input == "\b";
}

ftxui::Element FilesModalContent::RenderTopButtons() {
    using namespace ftxui;
    return hbox({
        home_button_->Render(),
        text(" "),
        documents_button_->Render(),
        text(" "),
        downloads_button_->Render(),
        text(" "),
        current_dir_button_->Render(),
        text(" "),
        add_dir_button_->Render(),
        text(" "),
        refresh_button_->Render(),
        text(" "),
        copy_path_button_->Render(),
    });
}

ftxui::Element FilesModalContent::RenderFavoriteDirectories() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    favorite_boxes_.assign(favorite_directories_.size(), {});
    if (favorite_directories_.empty()) {
        return text(" Added directories: none") |
            dim |
            color(theme.modal_text_color);
    }

    Elements buttons;
    buttons.push_back(text(" ") | color(theme.modal_text_color));
    for (size_t index = 0; index < favorite_directories_.size(); ++index) {
        if (index > 0) {
            buttons.push_back(text(" "));
        }
        std::string label = favorite_directories_[index].filename().string();
        if (label.empty()) {
            label = favorite_directories_[index].string();
        }
        buttons.push_back(
            text(BracketLabel(TrimForDisplay(label, 16))) |
            color(theme.modal_accent) |
            reflect(favorite_boxes_[index]));
    }
    return hbox(std::move(buttons));
}

ftxui::Element FilesModalContent::RenderPathInput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Path: ") | bold | color(theme.modal_accent),
        path_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
    });
}

ftxui::Element FilesModalContent::RenderFileNameInput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (!IsSaveLikeMode()) {
        return text("");
    }
    return hbox({
        text(" File: ") | bold | color(theme.modal_accent),
        file_name_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
    });
}

ftxui::Element FilesModalContent::RenderOperationButtons() {
    using namespace ftxui;
    return hbox({
        create_dir_button_->Render(),
        text(" "),
        create_file_button_->Render(),
        text(" "),
        delete_button_->Render(),
        text(" "),
        rename_button_->Render(),
        text(" "),
        copy_button_->Render(),
        text(" "),
        cut_button_->Render(),
        text(" "),
        paste_button_->Render(),
    });
}

ftxui::Element FilesModalContent::RenderConfirmRow() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return text(" ") | color(theme.modal_text_color);
}

ftxui::Element FilesModalContent::RenderEntryList() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    entry_boxes_.assign(0, {});

    Elements rows;
    rows.push_back(
        hbox({
            text("  Type ") | bold | size(WIDTH, EQUAL, 8),
            text("Name") | bold | size(WIDTH, EQUAL, kEntryNameWidth),
            text("Size") | bold | size(WIDTH, EQUAL, kEntrySizeWidth),
        }) | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));

    if (entries_.empty()) {
        rows.push_back(text("  No files or directories.") | color(theme.modal_text_color));
    } else {
        EnsureSelectionVisible();
        const int end = std::min(
            static_cast<int>(entries_.size()),
            scroll_offset_ + kVisibleEntryRows);
        entry_boxes_.assign(static_cast<size_t>(end - scroll_offset_), {});
        for (int index = scroll_offset_; index < end; ++index) {
            const FileEntry& entry = entries_[static_cast<size_t>(index)];
            const size_t visible_index = static_cast<size_t>(index - scroll_offset_);
            Element row = hbox({
                text(index == selected_entry_ ? "> " : "  "),
                text(FileTypeLabel(entry.type)) | size(WIDTH, EQUAL, 6),
                text(FormatEntryName(entry, kEntryNameWidth)) |
                    size(WIDTH, EQUAL, kEntryNameWidth),
                text(FormatSize(entry)) | size(WIDTH, EQUAL, kEntrySizeWidth),
            }) | reflect(entry_boxes_[visible_index]);

            if (index == selected_entry_) {
                row = row |
                    bgcolor(theme.modal_selected_item_bg) |
                    color(theme.modal_selected_item_fg) |
                    bold;
            } else {
                row = row | color(theme.modal_text_color);
            }
            rows.push_back(row);
        }
    }

    while (static_cast<int>(rows.size()) < kVisibleEntryRows + 2) {
        rows.push_back(text(""));
    }

    return vbox(std::move(rows)) |
        size(HEIGHT, EQUAL, kVisibleEntryRows + 2) |
        frame;
}

ftxui::Element FilesModalContent::RenderSelectionSummary() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    std::string text_value = "No selection";
    if (!entries_.empty()) {
        const int safe_index = std::clamp(
            selected_entry_,
            0,
            static_cast<int>(entries_.size()) - 1);
        const FileEntry& entry = entries_[static_cast<size_t>(safe_index)];
        text_value = "Selected: " + entry.path.string();
    }
    const std::string range = entries_.empty()
        ? "0/0"
        : std::to_string(selected_entry_ + 1) + "/" + std::to_string(entries_.size());
    return hbox({
        text(" " + TrimForDisplay(text_value, 82)) |
            dim |
            color(theme.modal_text_color),
        filler(),
        text(" " + range + " ") |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element FilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows = {
        RenderTopButtons(),
        RenderFavoriteDirectories(),
        RenderPathInput(),
    };
    if (IsSaveLikeMode()) {
        rows.push_back(RenderFileNameInput());
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderOperationButtons());
    rows.push_back(RenderConfirmRow());
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderEntryList());
    rows.push_back(RenderSelectionSummary());

    if (status_is_error_) {
        rows.push_back(text(" Error: " + status_) | color(Color::Red));
    } else {
        rows.push_back(text(" Status: " + status_) | color(theme.modal_text_color));
    }

    return vbox(std::move(rows)) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);
}

std::string FilesModalContent::ModeTitle() const {
    switch (mode_) {
        case FilesModalMode::Open:
            return "Open";
        case FilesModalMode::SaveAs:
            return "Save As";
        case FilesModalMode::Import:
            return "Import";
        case FilesModalMode::Export:
            return "Export";
        case FilesModalMode::Manage:
            return "Files";
        case FilesModalMode::None:
            break;
    }
    return "Files";
}

std::string FilesModalContent::FooterActionLabel() const {
    switch (mode_) {
        case FilesModalMode::Open:
            return "Open";
        case FilesModalMode::SaveAs:
            return "Save As";
        case FilesModalMode::Import:
            return "Import";
        case FilesModalMode::Export:
            return "Export";
        case FilesModalMode::Manage:
        case FilesModalMode::None:
            break;
    }
    return "Confirm";
}

std::string FilesModalContent::FormatCurrentDirectory() const {
    if (current_directory_.empty()) {
        return std::filesystem::current_path().string();
    }
    return current_directory_.string();
}

std::string FilesModalContent::FormatEntryName(const FileEntry& entry, size_t width) const {
    std::string name;
    if (IsParentEntry(entry)) {
        name = "../";
    } else if (entry.type == FileEntryType::Directory) {
        name = entry.name + "/";
    } else {
        name = entry.name;
    }
    return TrimForDisplay(name, width);
}

std::string FilesModalContent::FormatSize(const FileEntry& entry) const {
    if (entry.type == FileEntryType::Directory) {
        return "<DIR>";
    }
    if (entry.type == FileEntryType::Symlink) {
        return "<LINK>";
    }
    if (entry.type != FileEntryType::File) {
        return "";
    }
    return FileManager::FormatFileSize(entry.size);
}

std::string FilesModalContent::TrimForDisplay(const std::string& text, size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::string FilesModalContent::SuggestedFileNameFromPath(
    const std::filesystem::path& path) const {
    return path.filename().string();
}

FilesModal::FilesModal(
    const Theme* theme,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    FavoriteDirectoriesProvider favorite_directories_provider,
    AddFavoriteDirectoryCallback on_add_favorite_directory,
    CopyPathCallback on_copy_path,
    ConfirmPathCallback on_confirm_path)
    : theme_(theme) {
    content_ = std::make_shared<FilesModalContent>(
        theme_,
        file_manager,
        std::move(start_directory_provider),
        std::move(favorite_directories_provider),
        std::move(on_add_favorite_directory),
        std::move(on_copy_path),
        std::move(on_confirm_path),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
    RebuildFooterButtons();
}

ftxui::Component FilesModal::View() const {
    return modal_;
}

void FilesModal::Open(
    FilesModalMode mode,
    const std::filesystem::path& start_path,
    std::string suggested_file_name) {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open(mode, start_path, std::move(suggested_file_name));
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        RebuildFooterButtons();
        modal_->RefreshChildren();
    }
}

void FilesModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool FilesModal::IsOpen() const {
    return open_;
}

bool FilesModal::OnEvent(ftxui::Event event) {
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

void FilesModal::RebuildFooterButtons() {
    if (!modal_ || !content_) {
        return;
    }

    if (content_->Mode() == FilesModalMode::Manage || content_->Mode() == FilesModalMode::None) {
        modal_->SetFooterButtons({
            {"Close", [this] { Close(); }},
        });
        return;
    }

    modal_->SetFooterButtons({
        {content_->Mode() == FilesModalMode::SaveAs ? "Save As" :
            content_->Mode() == FilesModalMode::Import ? "Import" :
            content_->Mode() == FilesModalMode::Export ? "Export" : "Open",
            [this] {
                if (content_) {
                    content_->ConfirmSelected();
                }
            }},
        {"Close", [this] { Close(); }},
    });
}

} // namespace textlt
