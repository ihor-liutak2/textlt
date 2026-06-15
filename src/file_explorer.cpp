#include "file_explorer.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {
namespace {

std::string DisplayName(const std::filesystem::path& path) {
    return path.filename().empty() ? path.string() : path.filename().string();
}

} // namespace

FileExplorer::FileExplorer(
    FileOpenCallback on_file_open,
    const Theme* theme,
    GitManager* git_manager,
    EditorConfig* config)
    : on_file_open_(std::move(on_file_open)),
      theme_(theme),
      git_manager_(git_manager),
      config_(config),
      current_directory_(std::filesystem::current_path()) {
    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    option.on_enter = [this] { OpenSelectedEntry(); };
    menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, option);
    Add(menu_);
    RebuildEntries();
}

ftxui::Element FileExplorer::Render() {
    using namespace ftxui;

    if (git_manager_) {
        git_manager_->SetWorkingDirectory(current_directory_);
    }

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    for (size_t index = 0; index < entry_labels_.size(); ++index) {
        Element row = text(entry_labels_[index]);
        if (index < entry_paths_.size() && index < entry_kinds_.size() &&
            (entry_kinds_[index] == EntryKind::Directory ||
             entry_kinds_[index] == EntryKind::File)) {
            const char status = GitStatusForPath(entry_paths_[index]);
            if (status == 'M') {
                row = row | color(theme.syntax_keyword);
            } else if (status == '?' || status == 'A') {
                row = row | color(theme.syntax_string);
            }
        }
        if (index < entry_kinds_.size() && entry_kinds_[index] == EntryKind::FavoriteToggle) {
            row = row | bold | color(theme.modal_accent);
        }

        if (static_cast<int>(index) == selected_entry_) {
            row = row |
                bgcolor(theme.menu_foreground) |
                color(theme.menu_background);
        }
        rows.push_back(std::move(row));
    }

    return vbox({
        text("Files") | bold,
        separator(),
        vbox(std::move(rows)) | frame | reflect(menu_box_) | yflex,
    }) |
        border;
}

bool FileExplorer::OnEvent(ftxui::Event event) {
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const int clicked_entry = EntryIndexAtMouse(event.mouse());
        if (clicked_entry >= 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_click_time_).count();

            selected_entry_ = clicked_entry;
            FocusMenu();
            if (clicked_entry < static_cast<int>(entry_kinds_.size()) &&
                (entry_kinds_[clicked_entry] == EntryKind::FavoriteToggle ||
                 entry_kinds_[clicked_entry] == EntryKind::FavoriteFile)) {
                OpenSelectedEntry();
                last_clicked_entry_ = -1;
                last_click_time_ = {};
                return true;
            }

            if (duration < 300 && clicked_entry == last_clicked_entry_) {
                last_clicked_entry_ = -1;
                last_click_time_ = {};
                if (OpenDirectoryEntry(clicked_entry)) {
                    return true;
                }

                // Double-clicking a regular file delegates to the central file-open callback.
                OpenSelectedEntry();
                return true;
            }

            last_clicked_entry_ = clicked_entry;
            last_click_time_ = now;
            return true;
        }
    }

    return ComponentBase::OnEvent(event);
}

bool FileExplorer::Focusable() const {
    return true;
}

void FileExplorer::FocusMenu() {
    menu_->TakeFocus();
}

void FileExplorer::Refresh() {
    RebuildEntries();
}

void FileExplorer::OpenSelectedEntry() {
    if (selected_entry_ < 0 ||
        selected_entry_ >= static_cast<int>(entry_paths_.size())) {
        return;
    }

    if (selected_entry_ >= static_cast<int>(entry_kinds_.size())) {
        return;
    }

    const EntryKind selected_kind = entry_kinds_[selected_entry_];
    if (selected_kind == EntryKind::FavoriteToggle) {
        favorites_expanded_ = !favorites_expanded_;
        RebuildEntries();
        return;
    }

    const std::filesystem::path selected_path = entry_paths_[selected_entry_];
    if (selected_kind == EntryKind::FavoriteFile) {
        std::error_code error;
        if (std::filesystem::is_regular_file(selected_path, error) && on_file_open_) {
            on_file_open_(selected_path);
            return;
        }
        if (config_) {
            config_->RemoveFavorite(selected_path.string());
        }
        RebuildEntries();
        return;
    }

    std::error_code error;
    if (selected_kind == EntryKind::Directory &&
        std::filesystem::is_directory(selected_path, error)) {
        OpenDirectoryEntry(selected_entry_);
        return;
    }

    if (selected_kind == EntryKind::File &&
        std::filesystem::is_regular_file(selected_path, error) && on_file_open_) {
        on_file_open_(selected_path);
    }
}

bool FileExplorer::OpenDirectoryEntry(int entry_index) {
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_paths_.size())) {
        return false;
    }
    if (entry_index >= static_cast<int>(entry_kinds_.size()) ||
        entry_kinds_[entry_index] != EntryKind::Directory) {
        return false;
    }

    const std::filesystem::path selected_path = entry_paths_[entry_index];
    std::error_code error;
    if (!std::filesystem::is_directory(selected_path, error)) {
        return false;
    }

    current_directory_ = std::filesystem::canonical(selected_path, error);
    if (error) {
        current_directory_ = selected_path;
    }
    RebuildEntries();
    return true;
}

int FileExplorer::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    if (!menu_box_.Contain(mouse.x, mouse.y)) {
        return -1;
    }

    const int entry_index = mouse.y - menu_box_.y_min;
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_labels_.size())) {
        return -1;
    }
    return entry_index;
}

char FileExplorer::GitStatusForPath(const std::filesystem::path& path) const {
    if (!git_manager_) {
        return '\0';
    }

    const std::filesystem::path repository_root = git_manager_->RepositoryRoot();
    if (repository_root.empty()) {
        return '\0';
    }

    std::error_code error;
    const std::filesystem::path relative_path =
        std::filesystem::relative(path, repository_root, error);
    if (error || relative_path.empty()) {
        return '\0';
    }

    std::string key = relative_path.generic_string();
    const std::map<std::string, char> statuses = git_manager_->GetFileStatuses();
    const auto direct_match = statuses.find(key);
    if (direct_match != statuses.end()) {
        return direct_match->second;
    }

    if (!key.empty() && key.back() != '/') {
        key += "/";
    }
    for (const auto& [status_path, status] : statuses) {
        if (status_path.rfind(key, 0) == 0) {
            return status;
        }
    }

    return '\0';
}

void FileExplorer::RebuildEntries() {
    entry_paths_.clear();
    entry_labels_.clear();
    entry_kinds_.clear();

    if (config_ && !config_->favorites_.empty()) {
        AddEntry({}, favorites_expanded_ ? " [Favorites]" : " [Favorites]", EntryKind::FavoriteToggle);
        if (favorites_expanded_) {
            std::vector<std::string> unique_favorites;
            for (const std::string& favorite : config_->favorites_) {
                const std::string normalized_path = EditorConfig::NormalizeFavoritePath(favorite);
                if (!normalized_path.empty() &&
                    std::find(unique_favorites.begin(), unique_favorites.end(), normalized_path) ==
                        unique_favorites.end()) {
                    unique_favorites.push_back(normalized_path);
                    AddEntry(normalized_path, "  " + normalized_path, EntryKind::FavoriteFile);
                }
            }
        }
    }

    std::error_code error;
    const std::filesystem::path parent = current_directory_.parent_path();
    if (!parent.empty() && parent != current_directory_) {
        AddEntry(parent, " ../", EntryKind::Directory);
    }

    std::vector<std::filesystem::directory_entry> directories;
    std::vector<std::filesystem::directory_entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(current_directory_, error)) {
        if (entry.is_directory(error)) {
            directories.push_back(entry);
        } else if (entry.is_regular_file(error)) {
            files.push_back(entry);
        }
    }

    auto by_name = [](const auto& left, const auto& right) {
        return DisplayName(left.path()) < DisplayName(right.path());
    };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    for (const auto& directory : directories) {
        AddEntry(directory.path(), " [" + DisplayName(directory.path()) + "]", EntryKind::Directory);
    }
    for (const auto& file : files) {
        AddEntry(file.path(), " " + DisplayName(file.path()), EntryKind::File);
    }

    if (entry_labels_.empty()) {
        AddEntry({}, " <empty>", EntryKind::Placeholder);
    }
    selected_entry_ = std::min(selected_entry_, static_cast<int>(entry_labels_.size()) - 1);
}

void FileExplorer::AddEntry(std::filesystem::path path, std::string label, EntryKind kind) {
    entry_paths_.push_back(std::move(path));
    entry_labels_.push_back(std::move(label));
    entry_kinds_.push_back(kind);
}

} // namespace textlt
