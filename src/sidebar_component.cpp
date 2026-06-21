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

ftxui::Color SidebarTextColor(const Theme& theme) {
    if (theme.name == "Solarized Light") {
        return ftxui::Color::RGB(7, 54, 66);
    }
    if (theme.name == "GitHub Light") {
        return ftxui::Color::RGB(12, 18, 28);
    }
    if (theme.name == "Pantone Peach Fuzz") {
        return ftxui::Color::RGB(43, 27, 18);
    }
    return theme.foreground;
}

} // namespace

SidebarPanel::SidebarPanel(
    FileOpenCallback on_file_open,
    OpenedFileSelectCallback on_opened_file_select,
    const Theme* theme,
    GitManager* git_manager,
    EditorConfig* config)
    : on_file_open_(std::move(on_file_open)),
      on_opened_file_select_(std::move(on_opened_file_select)),
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
    ClampScrollOffset();

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const ftxui::Color sidebar_text = SidebarTextColor(theme);
    Element opened_tab =
        text("[ Opened ]") |
        color(sidebar_text) |
        dim |
        reflect(opened_tab_box_);
    Element project_tab =
        text("[ Project ]") |
        color(sidebar_text) |
        dim |
        reflect(project_tab_box_);
    Element favorites_tab =
        text("[ Favorites ]") |
        color(sidebar_text) |
        dim |
        reflect(favorites_tab_box_);

    // Active tabs mirror the modal menu selection palette.
    if (mode_ == SidebarMode::Opened) {
        opened_tab = opened_tab |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg) |
            bold;
    } else if (mode_ == SidebarMode::Project) {
        project_tab = project_tab |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg) |
            bold;
    } else {
        favorites_tab = favorites_tab |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg) |
            bold;
    }

    Elements rows;
    const size_t visible_entry_count = VisibleEntryCount();
    const size_t end_index =
        std::min(entry_labels_.size(), list_scroll_offset_ + visible_entry_count);
    for (size_t index = list_scroll_offset_; index < end_index; ++index) {
        Element row = text(entry_labels_[index]) | color(sidebar_text);
        if (mode_ == SidebarMode::Project &&
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
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        }
        rows.push_back(std::move(row));
    }
    while (rows.size() < visible_entry_count) {
        rows.push_back(text(""));
    }

    return vbox({
        hbox({
            project_tab,
            text(" "),
            favorites_tab,
        }),
        opened_tab,
        separator(),
        vbox(std::move(rows)) | reflect(menu_box_) | frame | yflex,
    }) |
        border |
        reflect(panel_box_);
}

bool SidebarPanel::OnEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        if (selected_entry_ + 1 < static_cast<int>(entry_labels_.size())) {
            ++selected_entry_;
            EnsureSelectedEntryVisible();
        }
        FocusMenu();
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (selected_entry_ > 0) {
            --selected_entry_;
            EnsureSelectedEntryVisible();
        }
        FocusMenu();
        return true;
    }

    if (event.is_mouse() &&
        (event.mouse().button == ftxui::Mouse::WheelDown ||
         event.mouse().button == ftxui::Mouse::WheelUp) &&
        panel_box_.Contain(event.mouse().x, event.mouse().y)) {
        ScrollEntries(event.mouse().button == ftxui::Mouse::WheelDown ? 3 : -3);
        FocusMenu();
        return true;
    }

    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        opened_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetMode(SidebarMode::Opened);
        return true;
    }

    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        project_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetMode(SidebarMode::Project);
        return true;
    }

    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        favorites_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetMode(SidebarMode::Favorites);
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
            EnsureSelectedEntryVisible();
            FocusMenu();
            if (mode_ == SidebarMode::Opened || mode_ == SidebarMode::Favorites) {
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

void SidebarPanel::SetOpenedFiles(std::vector<OpenedFileEntry> opened_files, size_t active_index) {
    opened_files_ = std::move(opened_files);
    active_opened_file_index_ = active_index;
    if (mode_ == SidebarMode::Opened) {
        RebuildEntries();
    }
}

void SidebarPanel::ShowOpenedFiles() {
    SetMode(SidebarMode::Opened);
}

void SidebarPanel::ShowProject() {
    SetMode(SidebarMode::Project);
}

void SidebarPanel::ToggleOpenedProject() {
    SetMode(mode_ == SidebarMode::Opened ? SidebarMode::Project : SidebarMode::Opened);
}

void SidebarPanel::SetMode(SidebarMode mode) {
    if (mode_ == mode) {
        return;
    }

    mode_ = mode;
    selected_entry_ = 0;
    list_scroll_offset_ = 0;
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
    if (selected_kind == EntryKind::OpenedFile) {
        if (on_opened_file_select_) {
            on_opened_file_select_(static_cast<size_t>(selected_entry_));
        }
        return;
    }

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
    if (mode_ != SidebarMode::Project ||
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
    list_scroll_offset_ = 0;
    return true;
}

int SidebarPanel::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    if (!menu_box_.Contain(mouse.x, mouse.y)) {
        return -1;
    }

    const int entry_index =
        static_cast<int>(list_scroll_offset_) + mouse.y - menu_box_.y_min;
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_labels_.size())) {
        return -1;
    }
    return entry_index;
}

size_t SidebarPanel::VisibleEntryCount() const {
    if (menu_box_.y_max >= menu_box_.y_min) {
        return std::max<size_t>(
            1,
            static_cast<size_t>(menu_box_.y_max - menu_box_.y_min + 1));
    }
    return std::max<size_t>(1, std::min<size_t>(entry_labels_.size(), 12));
}

size_t SidebarPanel::MaxScrollOffset() const {
    const size_t visible_count = VisibleEntryCount();
    return entry_labels_.size() > visible_count
        ? entry_labels_.size() - visible_count
        : 0;
}

void SidebarPanel::ClampScrollOffset() {
    list_scroll_offset_ = std::min(list_scroll_offset_, MaxScrollOffset());
}

void SidebarPanel::EnsureSelectedEntryVisible() {
    if (entry_labels_.empty()) {
        selected_entry_ = 0;
        list_scroll_offset_ = 0;
        return;
    }

    selected_entry_ = std::clamp(
        selected_entry_,
        0,
        static_cast<int>(entry_labels_.size()) - 1);

    const size_t visible_count = VisibleEntryCount();
    const size_t selected = static_cast<size_t>(selected_entry_);
    if (selected < list_scroll_offset_) {
        list_scroll_offset_ = selected;
    } else if (selected >= list_scroll_offset_ + visible_count) {
        list_scroll_offset_ = selected - visible_count + 1;
    }
    ClampScrollOffset();
}

void SidebarPanel::ClampSelectedEntryToVisible() {
    if (entry_labels_.empty()) {
        selected_entry_ = 0;
        return;
    }

    const size_t visible_count = VisibleEntryCount();
    const size_t max_visible_index =
        std::min(entry_labels_.size() - 1, list_scroll_offset_ + visible_count - 1);
    selected_entry_ = std::clamp(
        selected_entry_,
        static_cast<int>(list_scroll_offset_),
        static_cast<int>(max_visible_index));
}

void SidebarPanel::ScrollEntries(int delta) {
    const long long target_offset =
        static_cast<long long>(list_scroll_offset_) + static_cast<long long>(delta);
    list_scroll_offset_ = static_cast<size_t>(
        std::clamp<long long>(target_offset, 0, static_cast<long long>(MaxScrollOffset())));
    ClampSelectedEntryToVisible();
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

    switch (mode_) {
        case SidebarMode::Opened:
            RebuildOpenedEntries();
            break;
        case SidebarMode::Project:
            RebuildProjectEntries();
            break;
        case SidebarMode::Favorites:
            RebuildFavoriteEntries();
            break;
    }

    if (entry_labels_.empty()) {
        const std::string placeholder =
            mode_ == SidebarMode::Opened ? " No opened files" :
            mode_ == SidebarMode::Favorites ? " No favorites" :
            " <empty>";
        AddEntry({}, placeholder, EntryKind::Placeholder);
    }
    selected_entry_ = std::min(selected_entry_, static_cast<int>(entry_labels_.size()) - 1);
    EnsureSelectedEntryVisible();
}

void SidebarPanel::RebuildOpenedEntries() {
    for (size_t index = 0; index < opened_files_.size(); ++index) {
        const OpenedFileEntry& opened = opened_files_[index];
        std::string label = opened.active ? "> " : "  ";
        label += opened.label.empty() ? DisplayName(opened.path) : opened.label;
        if (opened.dirty) {
            label += " *";
        }
        AddEntry(opened.path, label, EntryKind::OpenedFile);
        if (opened.active) {
            selected_entry_ = static_cast<int>(index);
        }
    }
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
    for (const FavoriteEntry& favorite : config_->favorites_) {
        const std::string normalized_path = EditorConfig::NormalizeFavoritePath(favorite.path);
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
