#include "sidebar_component.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
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

bool IsLightTheme(const Theme& theme) {
    return theme.name.find("Light") != std::string::npos;
}

ftxui::Color SidebarBorderColor(const Theme& theme) {
    if (IsLightTheme(theme)) {
        return ftxui::Color::RGB(70, 70, 70);
    }
    return theme.gutter;
}

} // namespace

SidebarPanel::SidebarPanel(
    FileOpenCallback on_file_open,
    OpenedFileSelectCallback on_opened_file_select,
    const Theme* theme,
    GitManager* git_manager,
    FavoriteFilesProvider favorite_files_provider,
    RemoveFavoriteCallback on_remove_favorite,
    AddCurrentFavoriteCallback on_add_current_favorite,
    CloseOpenedFileCallback on_close_opened_file,
    CloseAllOpenedFilesCallback on_close_all_opened_files,
    OpenFilesModalCallback on_open_files_modal,
    CopyPathCallback on_copy_path,
    ShortcutLabelProvider shortcut_label_provider)
    : on_file_open_(std::move(on_file_open)),
      on_opened_file_select_(std::move(on_opened_file_select)),
      theme_(theme),
      git_manager_(git_manager),
      favorite_files_provider_(std::move(favorite_files_provider)),
      on_remove_favorite_(std::move(on_remove_favorite)),
      on_add_current_favorite_(std::move(on_add_current_favorite)),
      on_close_opened_file_(std::move(on_close_opened_file)),
      on_close_all_opened_files_(std::move(on_close_all_opened_files)),
      on_open_files_modal_(std::move(on_open_files_modal)),
      on_copy_path_(std::move(on_copy_path)),
      shortcut_label_provider_(std::move(shortcut_label_provider)),
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
            RenderModeButton("Project", SidebarMode::Project, project_tab_box_),
            text(" "),
            RenderModeButton("Favorites", SidebarMode::Favorites, favorites_tab_box_),
        }),
        hbox({
            RenderModeButton("Opened", SidebarMode::Opened, opened_tab_box_),
            filler(),
            text(" " + ActiveShortcutLabel() + " ") | color(theme.gutter),
        }),
        RenderActionRow(),
        separator() | color(SidebarBorderColor(theme)),
        vbox(std::move(rows)) | reflect(menu_box_) | frame | yflex,
    }) |
        borderStyled(ftxui::LIGHT, SidebarBorderColor(theme)) |
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

    if (event == ftxui::Event::Backspace && mode_ == SidebarMode::Project) {
        const std::filesystem::path parent = current_path_.parent_path();
        if (!parent.empty() && parent != current_path_) {
            current_path_ = parent;
            RebuildEntries();
            list_scroll_offset_ = 0;
            FocusMenu();
            return true;
        }
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
        event.mouse().motion == ftxui::Mouse::Pressed &&
        HandleActionButtonMouse(event.mouse())) {
        // Action buttons may open a modal. Do not steal focus back to the
        // sidebar after the callback runs. The callback owns the next focus.
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

void SidebarPanel::ShowFavorites() {
    SetMode(SidebarMode::Favorites);
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
        if (on_remove_favorite_) {
            on_remove_favorite_(selected_path);
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

std::string SidebarPanel::ShortcutLabelForMode(SidebarMode mode) const {
    std::string command_id;
    std::string fallback;
    switch (mode) {
        case SidebarMode::Project:
            command_id = "sidebar.show_project";
            fallback = "P";
            break;
        case SidebarMode::Favorites:
            command_id = "sidebar.show_favorites";
            fallback = "F";
            break;
        case SidebarMode::Opened:
            command_id = "sidebar.show_opened_files";
            fallback = "O";
            break;
    }

    if (shortcut_label_provider_) {
        const std::string configured = shortcut_label_provider_(command_id);
        if (!configured.empty()) {
            return configured;
        }
    }
    return fallback;
}

std::string SidebarPanel::ActiveShortcutLabel() const {
    return ShortcutLabelForMode(mode_);
}

ftxui::Element SidebarPanel::RenderModeButton(
    const std::string& label,
    SidebarMode mode,
    ftxui::Box& box) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ButtonSpec spec = TabButtonSpec(label, mode_ == mode, "", ButtonSize::Compact);
    return RenderModalFlatButton(theme, spec, mode_ == mode) | reflect(box);
}

ftxui::Element SidebarPanel::RenderFlatActionButton(
    const std::string& label,
    ButtonRole role,
    ftxui::Box& box) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ButtonSpec spec = ButtonSpecFromLabel(label, role, ButtonVariant::Minimal, ButtonSize::Compact);
    return RenderModalFlatButton(theme, spec) | reflect(box);
}

ftxui::Element SidebarPanel::RenderActionRow() {
    using namespace ftxui;

    action_primary_box_ = {};
    action_secondary_box_ = {};

    if (mode_ == SidebarMode::Favorites) {
        return hbox({
            RenderFlatActionButton("Remove", ButtonRole::Danger, action_primary_box_),
            text(" "),
            RenderFlatActionButton("Add Current", ButtonRole::Primary, action_secondary_box_),
        });
    }

    if (mode_ == SidebarMode::Opened) {
        return hbox({
            RenderFlatActionButton("Close", ButtonRole::Warning, action_primary_box_),
            text(" "),
            RenderFlatActionButton("Close All", ButtonRole::Danger, action_secondary_box_),
        });
    }

    return hbox({
        RenderFlatActionButton("Files..", ButtonRole::Primary, action_primary_box_),
        text(" "),
        RenderFlatActionButton("Copy Path", ButtonRole::Utility, action_secondary_box_),
    });
}

bool SidebarPanel::HandleActionButtonMouse(const ftxui::Mouse& mouse) {
    if (action_primary_box_.Contain(mouse.x, mouse.y)) {
        switch (mode_) {
            case SidebarMode::Favorites:
                RemoveSelectedFavorite();
                return true;
            case SidebarMode::Opened:
                CloseSelectedOpenedFile();
                return true;
            case SidebarMode::Project:
                OpenFilesModal();
                return true;
        }
    }

    if (action_secondary_box_.Contain(mouse.x, mouse.y)) {
        switch (mode_) {
            case SidebarMode::Favorites:
                AddCurrentFavorite();
                return true;
            case SidebarMode::Opened:
                CloseAllOpenedFiles();
                return true;
            case SidebarMode::Project:
                CopySelectedProjectPath();
                return true;
        }
    }

    return false;
}

void SidebarPanel::AddCurrentFavorite() {
    if (on_add_current_favorite_) {
        on_add_current_favorite_();
    }
    Refresh();
}

void SidebarPanel::RemoveSelectedFavorite() {
    if (mode_ != SidebarMode::Favorites ||
        selected_entry_ < 0 ||
        selected_entry_ >= static_cast<int>(entry_paths_.size()) ||
        selected_entry_ >= static_cast<int>(entry_kinds_.size()) ||
        entry_kinds_[selected_entry_] != EntryKind::FavoriteFile) {
        return;
    }

    if (on_remove_favorite_) {
        on_remove_favorite_(entry_paths_[selected_entry_]);
    }
    Refresh();
}

void SidebarPanel::CloseSelectedOpenedFile() {
    if (mode_ != SidebarMode::Opened ||
        selected_entry_ < 0 ||
        selected_entry_ >= static_cast<int>(entry_kinds_.size()) ||
        entry_kinds_[selected_entry_] != EntryKind::OpenedFile) {
        return;
    }

    if (on_close_opened_file_) {
        on_close_opened_file_(static_cast<size_t>(selected_entry_));
    }
}

void SidebarPanel::CloseAllOpenedFiles() {
    if (on_close_all_opened_files_) {
        on_close_all_opened_files_();
    }
}

void SidebarPanel::OpenFilesModal() {
    if (on_open_files_modal_) {
        on_open_files_modal_();
    }
}

std::filesystem::path SidebarPanel::SelectedProjectPath() const {
    if (mode_ != SidebarMode::Project || selected_entry_ < 0 ||
        selected_entry_ >= static_cast<int>(entry_paths_.size()) ||
        selected_entry_ >= static_cast<int>(entry_kinds_.size())) {
        return {};
    }
    const EntryKind kind = entry_kinds_[static_cast<size_t>(selected_entry_)];
    if (kind != EntryKind::File && kind != EntryKind::Directory) {
        return {};
    }
    std::error_code error;
    std::filesystem::path selected = entry_paths_[static_cast<size_t>(selected_entry_)];
    std::filesystem::path absolute = std::filesystem::absolute(selected, error);
    if (error) {
        error.clear();
        absolute = selected;
    }
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : normalized;
}

bool SidebarPanel::CopySelectedProjectPath() {
    const std::filesystem::path selected = SelectedProjectPath();
    if (!on_copy_path_) {
        return false;
    }
    on_copy_path_(selected);
    return !selected.empty();
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
    if (!favorite_files_provider_) {
        return;
    }

    std::vector<std::filesystem::path> unique_favorites;
    for (const std::filesystem::path& favorite_path : favorite_files_provider_()) {
        if (favorite_path.empty()) {
            continue;
        }
        if (std::find(unique_favorites.begin(), unique_favorites.end(), favorite_path) !=
            unique_favorites.end()) {
            continue;
        }
        unique_favorites.push_back(favorite_path);
        const std::string filename = favorite_path.filename().string();
        AddEntry(
            favorite_path,
            " " + (filename.empty() ? favorite_path.string() : filename),
            EntryKind::FavoriteFile);
    }
}

void SidebarPanel::AddEntry(std::filesystem::path path, std::string label, EntryKind kind) {
    entry_paths_.push_back(std::move(path));
    entry_labels_.push_back(std::move(label));
    entry_kinds_.push_back(kind);
}

} // namespace textlt
