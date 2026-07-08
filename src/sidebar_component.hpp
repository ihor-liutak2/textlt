#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "git_manager.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class SidebarPanel : public ftxui::ComponentBase {
private:
    enum class SidebarMode {
        Opened,
        Project,
        Favorites,
    };

    enum class EntryKind {
        OpenedFile,
        Directory,
        File,
        FavoriteFile,
        Placeholder,
    };

public:
    struct OpenedFileEntry {
        std::filesystem::path path;
        std::string label;
        bool dirty = false;
        bool active = false;
    };

    using FileOpenCallback = std::function<void(const std::filesystem::path&)>;
    using OpenedFileSelectCallback = std::function<void(size_t index)>;
    using FavoriteFilesProvider = std::function<std::vector<std::filesystem::path>()>;
    using RemoveFavoriteCallback = std::function<void(const std::filesystem::path&)>;
    using AddCurrentFavoriteCallback = std::function<void()>;
    using CloseOpenedFileCallback = std::function<void(size_t index)>;
    using CloseAllOpenedFilesCallback = std::function<void()>;
    using OpenFilesModalCallback = std::function<void()>;
    using ShortcutLabelProvider = std::function<std::string(const std::string& command_id)>;

    SidebarPanel(
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
        ShortcutLabelProvider shortcut_label_provider);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;
    void FocusMenu();
    void Refresh();
    void SetOpenedFiles(std::vector<OpenedFileEntry> opened_files, size_t active_index);
    void ShowOpenedFiles();
    void ShowProject();
    void ShowFavorites();
    void ToggleOpenedProject();
    bool IsOpenedMode() const { return mode_ == SidebarMode::Opened; }
    bool IsProjectMode() const { return mode_ == SidebarMode::Project; }
    bool IsFavoritesMode() const { return mode_ == SidebarMode::Favorites; }

    std::filesystem::path CurrentPath() const { return current_path_; }

    bool ContainsPoint(int x, int y) const {
        return panel_box_.x_min >= 0 && panel_box_.Contain(x, y);
    }

    std::filesystem::path GetSelectedDirectoryPath() const {
        if (mode_ == SidebarMode::Project &&
            selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(entry_paths_.size()) &&
            selected_entry_ < static_cast<int>(entry_kinds_.size())) {
            const auto& target_path = entry_paths_[selected_entry_];
            if (entry_kinds_[selected_entry_] == EntryKind::Directory &&
                std::filesystem::is_directory(target_path)) {
                return target_path;
            }
        }
        return current_path_;
    }

    std::filesystem::path GetSelectedFileNameInCurrentDirectory() const {
        if (mode_ == SidebarMode::Project &&
            selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(entry_paths_.size()) &&
            selected_entry_ < static_cast<int>(entry_kinds_.size()) &&
            entry_kinds_[selected_entry_] == EntryKind::File) {
            const std::filesystem::path& target_path = entry_paths_[selected_entry_];
            if (target_path.parent_path() == current_path_) {
                return target_path.filename();
            }
        }
        return {};
    }

    std::filesystem::path GetSelectedPathInCurrentDirectory() const {
        if (mode_ == SidebarMode::Project &&
            selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(entry_paths_.size()) &&
            selected_entry_ < static_cast<int>(entry_kinds_.size()) &&
            (entry_kinds_[selected_entry_] == EntryKind::File ||
             entry_kinds_[selected_entry_] == EntryKind::Directory)) {
            const std::filesystem::path& target_path = entry_paths_[selected_entry_];
            if (target_path.parent_path() == current_path_) {
                return target_path.filename();
            }
        }
        return {};
    }

private:
    void SetMode(SidebarMode mode);
    void OpenSelectedEntry();
    bool OpenDirectoryEntry(int entry_index);
    int EntryIndexAtMouse(const ftxui::Mouse& mouse) const;
    char GitStatusForPath(const std::filesystem::path& path) const;
    size_t VisibleEntryCount() const;
    size_t MaxScrollOffset() const;
    void ClampScrollOffset();
    void EnsureSelectedEntryVisible();
    void ClampSelectedEntryToVisible();
    void ScrollEntries(int delta);
    void RebuildEntries();
    void RebuildOpenedEntries();
    void RebuildProjectEntries();
    void RebuildFavoriteEntries();
    void AddEntry(std::filesystem::path path, std::string label, EntryKind kind);
    std::string ShortcutLabelForMode(SidebarMode mode) const;
    std::string ActiveShortcutLabel() const;
    ftxui::Element RenderModeButton(
        const std::string& label,
        SidebarMode mode,
        ftxui::Box& box) const;
    ftxui::Element RenderFlatActionButton(
        const std::string& label,
        ButtonRole role,
        ftxui::Box& box) const;
    ftxui::Element RenderActionRow();
    bool HandleActionButtonMouse(const ftxui::Mouse& mouse);
    void AddCurrentFavorite();
    void RemoveSelectedFavorite();
    void CloseSelectedOpenedFile();
    void CloseAllOpenedFiles();
    void OpenFilesModal();

    FileOpenCallback on_file_open_;
    OpenedFileSelectCallback on_opened_file_select_;
    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    FavoriteFilesProvider favorite_files_provider_;
    RemoveFavoriteCallback on_remove_favorite_;
    AddCurrentFavoriteCallback on_add_current_favorite_;
    CloseOpenedFileCallback on_close_opened_file_;
    CloseAllOpenedFilesCallback on_close_all_opened_files_;
    OpenFilesModalCallback on_open_files_modal_;
    ShortcutLabelProvider shortcut_label_provider_;
    std::filesystem::path current_path_;
    std::vector<OpenedFileEntry> opened_files_;
    size_t active_opened_file_index_ = 0;
    std::vector<std::filesystem::path> entry_paths_;
    std::vector<std::string> entry_labels_;
    std::vector<EntryKind> entry_kinds_;
    int selected_entry_ = 0;
    size_t list_scroll_offset_ = 0;
    int last_clicked_entry_ = -1;
    SidebarMode mode_ = SidebarMode::Project;
    std::chrono::steady_clock::time_point last_click_time_{};
    ftxui::Box opened_tab_box_;
    ftxui::Box project_tab_box_;
    ftxui::Box favorites_tab_box_;
    ftxui::Box action_primary_box_;
    ftxui::Box action_secondary_box_;
    ftxui::Box panel_box_;
    ftxui::Box menu_box_;
    ftxui::Component menu_;
};

} // namespace textlt
