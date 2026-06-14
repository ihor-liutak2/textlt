#pragma once

#include <filesystem>
#include <functional>
#include <chrono>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "git_manager.hpp"
#include "theme.hpp"

namespace textlt {

class FileExplorer : public ftxui::ComponentBase {
public:
    using FileOpenCallback = std::function<void(const std::filesystem::path&)>;

    FileExplorer(FileOpenCallback on_file_open, const Theme* theme, GitManager* git_manager);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;
    void FocusMenu();
    void Refresh();

    // NEW ARCHITECTURAL GETTERS: Expose tree status properties safely to external consumers
    
    /**
     * @brief Retrieves the active parent directory root currently being browsed.
     */
    std::filesystem::path CurrentPath() const { return current_directory_; }

    /**
     * @brief Dynamically resolves the filesystem path matching the row highlighted by the user.
     * If the selection points to a directory, it returns that directory path; 
     * otherwise, it falls back to the current active directory.
     */
    std::filesystem::path GetSelectedDirectoryPath() const {
        if (selected_entry_ >= 0 && selected_entry_ < static_cast<int>(entry_paths_.size())) {
            const auto& target_path = entry_paths_[selected_entry_];
            if (std::filesystem::is_directory(target_path)) {
                return target_path;
            }
        }
        return current_directory_;
    }

private:
    void OpenSelectedEntry();
    bool OpenDirectoryEntry(int entry_index);
    int EntryIndexAtMouse(const ftxui::Mouse& mouse) const;
    char GitStatusForPath(const std::filesystem::path& path) const;
    void RebuildEntries();

    FileOpenCallback on_file_open_;
    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    std::filesystem::path current_directory_;
    std::vector<std::filesystem::path> entry_paths_;
    std::vector<std::string> entry_labels_;
    int selected_entry_ = 0;
    int last_clicked_entry_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};
    ftxui::Box menu_box_;
    ftxui::Component menu_;
};

} // namespace textlt
