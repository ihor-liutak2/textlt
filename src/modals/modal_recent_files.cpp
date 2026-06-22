#include "modal_recent_files.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

RecentFilesModalContent::RecentFilesModalContent(
    const Theme* theme,
    RecentFilesHistory* history,
    OpenFileCallback on_open,
    CloseCallback on_close)
    : theme_(theme),
      history_(history),
      on_open_(std::move(on_open)),
      on_close_(std::move(on_close)) {
    renderer_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return Render(); }),
        [this](ftxui::Event event) { return OnEvent(std::move(event)); });
    Refresh();
}

void RecentFilesModalContent::Refresh() {
    entries_.clear();
    groups_.clear();
    entry_boxes_.clear();
    selected_entry_ = 0;
    selected_full_path_.clear();
    last_clicked_entry_ = -1;
    last_click_time_ = {};

    if (history_) {
        history_->Refresh();
        std::vector<std::vector<RecentFilesHistory::Entry>> grouped_entries;
        for (const RecentFilesHistory::Entry& entry : history_->Entries()) {
            auto group = std::find_if(
                groups_.begin(),
                groups_.end(),
                [&entry](const FolderGroup& existing) {
                    return existing.folder_path == entry.folder_path;
                });
            if (group == groups_.end()) {
                groups_.push_back({entry.folder_path, 0, 0});
                grouped_entries.emplace_back();
                group = std::prev(groups_.end());
            }
            grouped_entries[static_cast<size_t>(group - groups_.begin())].push_back(entry);
        }

        for (size_t index = 0; index < groups_.size(); ++index) {
            groups_[index].first_entry = entries_.size();
            groups_[index].entry_count = grouped_entries[index].size();
            entries_.insert(
                entries_.end(),
                grouped_entries[index].begin(),
                grouped_entries[index].end());
        }
    }

    if (entries_.empty()) {
        status_ = "No recent files";
        return;
    }

    selected_full_path_ = entries_.front().full_path;
    entry_boxes_.resize(entries_.size());
    status_ = "Enter or double-click opens selected file";
}

void RecentFilesModalContent::OpenSelected() {
    if (selected_entry_ < 0 || selected_entry_ >= static_cast<int>(entries_.size())) {
        return;
    }

    const std::filesystem::path path = entries_[selected_entry_].full_path;
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(path, error_code)) {
        if (history_) {
            history_->RemoveFile(path);
        }
        Refresh();
        status_ = "Removed missing file";
        return;
    }

    std::string error;
    if (on_open_ && on_open_(path, error)) {
        if (on_close_) {
            on_close_();
        }
        return;
    }

    if (!std::filesystem::is_regular_file(path, error_code)) {
        if (history_) {
            history_->RemoveFile(path);
        }
        Refresh();
        status_ = "Removed missing file";
        return;
    }
    status_ = error.empty() ? "Unable to open file" : error;
}

ftxui::Element RecentFilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (entries_.empty()) {
        return vbox({
            filler(),
            text("No recent files") | center | color(theme.modal_text_color),
            filler(),
        }) | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
    }

    Elements rows;
    rows.reserve(entries_.size() + groups_.size());
    for (const FolderGroup& group : groups_) {
        rows.push_back(
            text(" " + group.folder_path.string()) |
            dim |
            color(theme.modal_accent));
        for (size_t offset = 0; offset < group.entry_count; ++offset) {
            const size_t index = group.first_entry + offset;
            const RecentFilesHistory::Entry& entry = entries_[index];
            const bool selected = static_cast<int>(index) == selected_entry_;
            Element row =
                text("  " + entry.file_name) |
                bold |
                size(WIDTH, GREATER_THAN, 1) |
                reflect(entry_boxes_[index]);
            if (selected) {
                row = row |
                    bgcolor(theme.modal_selected_item_bg) |
                    color(theme.modal_selected_item_fg);
            } else {
                row = row | color(theme.modal_text_color);
            }
            rows.push_back(row);
        }
    }

    return vbox(std::move(rows)) |
        yframe |
        vscroll_indicator |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

bool RecentFilesModalContent::OnEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        if (selected_entry_ + 1 < static_cast<int>(entries_.size())) {
            ++selected_entry_;
            selected_full_path_ = entries_[selected_entry_].full_path;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (selected_entry_ > 0) {
            --selected_entry_;
            selected_full_path_ = entries_[selected_entry_].full_path;
        }
        return true;
    }

    if (event == ftxui::Event::Return) {
        OpenSelected();
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
            selected_full_path_ = entries_[selected_entry_].full_path;
            if (duration < 300 && clicked_entry == last_clicked_entry_) {
                last_clicked_entry_ = -1;
                last_click_time_ = {};
                OpenSelected();
                return true;
            }
            last_clicked_entry_ = clicked_entry;
            last_click_time_ = now;
            return true;
        }
    }

    return false;
}

int RecentFilesModalContent::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    for (size_t index = 0; index < entry_boxes_.size(); ++index) {
        if (entry_boxes_[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

RecentFilesModal::RecentFilesModal(
    const Theme* theme,
    RecentFilesHistory* history,
    OpenFileCallback on_open)
    : theme_(theme) {
    content_ = std::make_shared<RecentFilesModalContent>(
        theme_,
        history,
        std::move(on_open),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Enter or double-click opens selected file. Escape closes.");
    modal_->SetFooterButtons({
        {"Refresh", [this] { content_->Refresh(); }},
        {"Close", [this] { Close(); }},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component RecentFilesModal::View() const {
    return modal_;
}

void RecentFilesModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    content_->Refresh();
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void RecentFilesModal::Close() {
    open_ = false;
}

bool RecentFilesModal::IsOpen() const {
    return open_;
}

bool RecentFilesModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
