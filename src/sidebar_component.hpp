#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_config.hpp"
#include "git_manager.hpp"
#include "theme.hpp"

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

    SidebarPanel(
        FileOpenCallback on_file_open,
        OpenedFileSelectCallback on_opened_file_select,
        const Theme* theme,
        GitManager* git_manager,
        EditorConfig* config);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;
    void FocusMenu();
    void Refresh();
    void SetOpenedFiles(std::vector<OpenedFileEntry> opened_files, size_t active_index);
    void ShowOpenedFiles();
    void ShowProject();
    void ToggleOpenedProject();

    std::filesystem::path CurrentPath() const { return current_path_; }

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

    FileOpenCallback on_file_open_;
    OpenedFileSelectCallback on_opened_file_select_;
    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    EditorConfig* config_ = nullptr;
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
    ftxui::Box panel_box_;
    ftxui::Box menu_box_;
    ftxui::Component menu_;
};

} // namespace textlt
