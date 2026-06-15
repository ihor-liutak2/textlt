#include "sidebar_component.hpp"

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

SidebarPanel::SidebarPanel(
    FileOpenCallback on_file_open,
    const Theme* theme,
    GitManager* git_manager,
    EditorConfig* config)
    : on_file_open_(std::move(on_file_open)),
      theme_(theme),
      git_manager_(git_manager),
      config_(config),
      current_path_(std::filesystem::current_path()) {
    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    option.on_enter = [this] { OpenSelectedEntry(); };
    menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, option);
    Add(menu_);
    RebuildEntries();
}

ftxui::Element SidebarPanel::Render() {
    using namespace ftxui;

    if (git_manager_) {
        git_manager_->SetWorkingDirectory(current_path_);
    }

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Element project_tab =
        text("[ Project ]") |
        color(theme.foreground) |
        reflect(project_tab_box_);
    Element favorites_tab =
        text("[ Favorites ]") |
        color(theme.foreground) |
        reflect(favorites_tab_box_);

    // Active tabs use the theme selection colors so the label stays readable
    // across light and dark themes without relying on inverted menu colors.
    if (!show_favorites_mode_) {
        project_tab = project_tab |
            bgcolor(theme.selection_bg) |
            color(theme.selection_fg) |
            bold;
    } else {
        favorites_tab = favorites_tab |
            bgcolor(theme.selection_bg) |
            color(theme.selection_fg) |
            bold;
    }

    Elements rows;
    for (size_t index = 0; index < entry_labels_.size(); ++index) {
        Element row = text(entry_labels_[index]);
        if (!show_favorites_mode_ &&
            index < entry_paths_.size() &&
            index < entry_kinds_.size() &&
            (entry_kinds_[index] == EntryKind::Directory ||
             entry_kinds_[index] == EntryKind::File)) {
            const char status = GitStatusForPath(entry_paths_[index]);
            if (status == 'M') {
                row = row | color(theme.syntax_keyword);
            } else if (status == '?' || status == 'A') {
                row = row | color(theme.syntax_string);
            }
        }

        if (static_cast<int>(index) == selected_entry_) {
            row = row |
                bgcolor(theme.menu_foreground) |
                color(theme.menu_background);
        }
        rows.push_back(std::move(row));
    }

    return vbox({
        hbox({
            project_tab,
            text(" "),
            favorites_tab,
        }),
        separator(),
        vbox(std::move(rows)) | frame | reflect(menu_box_) | yflex,
    }) |
        border;
}

bool SidebarPanel::OnEvent(ftxui::Event event) {
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        project_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetFavoritesMode(false);
        return true;
    }

    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        favorites_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetFavoritesMode(true);
        return true;
    }

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
            if (show_favorites_mode_) {
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

bool SidebarPanel::Focusable() const {
    return true;
}

void SidebarPanel::FocusMenu() {
    menu_->TakeFocus();
}

void SidebarPanel::Refresh() {
    RebuildEntries();
}

void SidebarPanel::SetFavoritesMode(bool enabled) {
    if (show_favorites_mode_ == enabled) {
        return;
    }

    show_favorites_mode_ = enabled;
    selected_entry_ = 0;
    last_clicked_entry_ = -1;
    last_click_time_ = {};
    RebuildEntries();
}

void SidebarPanel::OpenSelectedEntry() {
    if (selected_entry_ < 0 ||
        selected_entry_ >= static_cast<int>(entry_paths_.size()) ||
        selected_entry_ >= static_cast<int>(entry_kinds_.size())) {
        return;
    }

    const std::filesystem::path selected_path = entry_paths_[selected_entry_];
    const EntryKind selected_kind = entry_kinds_[selected_entry_];
    std::error_code error;
    if (selected_kind == EntryKind::FavoriteFile) {
        if (std::filesystem::is_regular_file(selected_path, error) && on_file_open_) {
            on_file_open_(selected_path);
            return;
        }
        if (config_) {
            config_->RemoveFavorite(selected_path.string());
        }
        Refresh();
        return;
    }

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

bool SidebarPanel::OpenDirectoryEntry(int entry_index) {
    if (show_favorites_mode_ ||
        entry_index < 0 ||
        entry_index >= static_cast<int>(entry_paths_.size()) ||
        entry_index >= static_cast<int>(entry_kinds_.size()) ||
        entry_kinds_[entry_index] != EntryKind::Directory) {
        return false;
    }

    const std::filesystem::path selected_path = entry_paths_[entry_index];
    std::error_code error;
    if (!std::filesystem::is_directory(selected_path, error)) {
        return false;
    }

    current_path_ = std::filesystem::canonical(selected_path, error);
    if (error) {
        current_path_ = selected_path;
    }
    RebuildEntries();
    return true;
}

int SidebarPanel::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    if (!menu_box_.Contain(mouse.x, mouse.y)) {
        return -1;
    }

    const int entry_index = mouse.y - menu_box_.y_min;
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_labels_.size())) {
        return -1;
    }
    return entry_index;
}

char SidebarPanel::GitStatusForPath(const std::filesystem::path& path) const {
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

void SidebarPanel::RebuildEntries() {
    entry_paths_.clear();
    entry_labels_.clear();
    entry_kinds_.clear();

    if (show_favorites_mode_) {
        RebuildFavoriteEntries();
    } else {
        RebuildProjectEntries();
    }

    if (entry_labels_.empty()) {
        AddEntry({}, show_favorites_mode_ ? " No favorites" : " <empty>", EntryKind::Placeholder);
    }
    selected_entry_ = std::min(selected_entry_, static_cast<int>(entry_labels_.size()) - 1);
}

void SidebarPanel::RebuildProjectEntries() {
    std::error_code error;
    const std::filesystem::path parent = current_path_.parent_path();
    if (!parent.empty() && parent != current_path_) {
        AddEntry(parent, " ../", EntryKind::Directory);
    }

    std::vector<std::filesystem::directory_entry> directories;
    std::vector<std::filesystem::directory_entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(current_path_, error)) {
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
}

void SidebarPanel::RebuildFavoriteEntries() {
    if (!config_) {
        return;
    }

    std::vector<std::string> unique_favorites;
    for (const std::string& favorite : config_->favorites_) {
        const std::string normalized_path = EditorConfig::NormalizeFavoritePath(favorite);
        if (!normalized_path.empty() &&
            std::find(unique_favorites.begin(), unique_favorites.end(), normalized_path) ==
                unique_favorites.end()) {
            unique_favorites.push_back(normalized_path);
            const std::string filename =
                std::filesystem::path(normalized_path).filename().string();
            AddEntry(
                normalized_path,
                " " + (filename.empty() ? normalized_path : filename),
                EntryKind::FavoriteFile);
        }
    }
}

void SidebarPanel::AddEntry(std::filesystem::path path, std::string label, EntryKind kind) {
    entry_paths_.push_back(std::move(path));
    entry_labels_.push_back(std::move(label));
    entry_kinds_.push_back(kind);
}

} // namespace textlt
