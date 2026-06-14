#include "file_explorer.hpp"

#include <algorithm>
#include <chrono>
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

FileExplorer::FileExplorer(FileOpenCallback on_file_open)
    : on_file_open_(std::move(on_file_open)),
      current_directory_(std::filesystem::current_path()) {
    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    option.on_enter = [this] { OpenSelectedEntry(); };
    menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, option);
    Add(menu_);
    RebuildEntries();
}

ftxui::Element FileExplorer::Render() {
    using namespace ftxui;

    return vbox({
        text(" Files ") | bold,
        text(current_directory_.filename().string()) | dim,
        separator(),
        menu_->Render() | frame | reflect(menu_box_) | yflex,
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
            if (duration < 300 && clicked_entry == last_clicked_entry_) {
                last_clicked_entry_ = -1;
                last_click_time_ = {};
                OpenDirectoryEntry(clicked_entry);
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

    const std::filesystem::path selected_path = entry_paths_[selected_entry_];
    std::error_code error;
    if (std::filesystem::is_directory(selected_path, error)) {
        OpenDirectoryEntry(selected_entry_);
        return;
    }

    if (std::filesystem::is_regular_file(selected_path, error) && on_file_open_) {
        on_file_open_(selected_path);
    }
}

bool FileExplorer::OpenDirectoryEntry(int entry_index) {
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_paths_.size())) {
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
    if (entry_index < 0 || entry_index >= static_cast<int>(entry_paths_.size())) {
        return -1;
    }
    return entry_index;
}

void FileExplorer::RebuildEntries() {
    entry_paths_.clear();
    entry_labels_.clear();

    std::error_code error;
    const std::filesystem::path parent = current_directory_.parent_path();
    if (!parent.empty() && parent != current_directory_) {
        entry_paths_.push_back(parent);
        entry_labels_.push_back(" ../");
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
        entry_paths_.push_back(directory.path());
        entry_labels_.push_back(" [" + DisplayName(directory.path()) + "]");
    }
    for (const auto& file : files) {
        entry_paths_.push_back(file.path());
        entry_labels_.push_back(" " + DisplayName(file.path()));
    }

    if (entry_labels_.empty()) {
        entry_labels_.push_back(" <empty>");
    }
    selected_entry_ = std::min(selected_entry_, static_cast<int>(entry_labels_.size()) - 1);
}

} // namespace textlt
