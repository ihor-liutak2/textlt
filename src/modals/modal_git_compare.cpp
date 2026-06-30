#include "modals/modal_git.hpp"

#include <algorithm>
#include <sstream>

#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string TrimCopy(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() &&
           (value[start] == '\n' || value[start] == '\r' || value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::string ShortRefLabel(std::string value) {
    value = TrimCopy(std::move(value));
    if (value.size() <= 40) {
        return value;
    }
    return value.substr(0, 37) + "...";
}

std::string CompareEntryLabel(const GitManager::CompareEntry& entry) {
    std::string label = entry.status.empty() ? "?" : entry.status;
    label += "  ";
    if (!entry.old_path.empty()) {
        label += entry.old_path + " -> ";
    }
    label += entry.path;
    return label;
}

} // namespace

void GitModalContent::RefreshCompareRefs() {
    compare_refs_.clear();
    compare_ref_labels_.clear();

    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }

    compare_refs_ = git_manager_->GetCompareRefs();
    RebuildCompareLabels();

    if (compare_refs_.empty()) {
        selected_compare_left_ref_ = 0;
        selected_compare_right_ref_ = 0;
        status_ = "No Git refs found.";
        return;
    }

    if (selected_compare_left_ref_ < 0 ||
        selected_compare_left_ref_ >= static_cast<int>(compare_refs_.size())) {
        selected_compare_left_ref_ = compare_refs_.size() > 1 ? 1 : 0;
    }
    if (selected_compare_right_ref_ < 0 ||
        selected_compare_right_ref_ >= static_cast<int>(compare_refs_.size())) {
        selected_compare_right_ref_ = 0;
    }
}

void GitModalContent::RefreshCompareFiles() {
    compare_entries_.clear();
    compare_file_labels_.clear();
    compare_diff_text_.clear();
    compare_diff_lines_.clear();
    compare_diff_scroll_offset_ = 0;

    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    if (compare_refs_.empty()) {
        RefreshCompareRefs();
    }
    if (compare_refs_.empty()) {
        return;
    }

    const std::string left_ref = SelectedCompareLeftRef();
    const std::string right_ref = SelectedCompareRightRef();
    if (left_ref == right_ref) {
        status_ = "Choose two different refs to compare.";
        return;
    }

    compare_entries_ = git_manager_->GetCompareEntries(left_ref, right_ref);
    for (const GitManager::CompareEntry& entry : compare_entries_) {
        compare_file_labels_.push_back(CompareEntryLabel(entry));
    }

    if (selected_compare_file_ >= static_cast<int>(compare_file_labels_.size())) {
        selected_compare_file_ = static_cast<int>(compare_file_labels_.size()) - 1;
    }
    if (selected_compare_file_ < 0) {
        selected_compare_file_ = 0;
    }

    status_ = compare_entries_.empty()
        ? "No changed files between selected refs."
        : std::to_string(compare_entries_.size()) + " changed file(s) between selected refs.";
}

void GitModalContent::RebuildCompareLabels() {
    compare_ref_labels_.clear();
    compare_ref_labels_.reserve(compare_refs_.size());
    for (const GitManager::CompareRef& ref : compare_refs_) {
        compare_ref_labels_.push_back(ShortRefLabel(ref.label));
    }
}

std::string GitModalContent::SelectedCompareFilePath() const {
    if (selected_compare_file_ < 0 ||
        selected_compare_file_ >= static_cast<int>(compare_entries_.size())) {
        return "";
    }
    return compare_entries_[static_cast<size_t>(selected_compare_file_)].path;
}

std::string GitModalContent::SelectedCompareLeftRef() const {
    if (selected_compare_left_ref_ < 0 ||
        selected_compare_left_ref_ >= static_cast<int>(compare_refs_.size())) {
        return "";
    }
    return compare_refs_[static_cast<size_t>(selected_compare_left_ref_)].value;
}

std::string GitModalContent::SelectedCompareRightRef() const {
    if (selected_compare_right_ref_ < 0 ||
        selected_compare_right_ref_ >= static_cast<int>(compare_refs_.size())) {
        return "";
    }
    return compare_refs_[static_cast<size_t>(selected_compare_right_ref_)].value;
}

void GitModalContent::OpenCompareSideBySide() {
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    if (!on_open_compare_) {
        status_ = "Compare open callback is not configured.";
        return;
    }

    const std::string path = SelectedCompareFilePath();
    if (path.empty()) {
        status_ = "No changed file selected.";
        return;
    }

    const std::string left_ref = SelectedCompareLeftRef();
    const std::string right_ref = SelectedCompareRightRef();
    GitManager::CommandResult left_result = git_manager_->ReadFileAtRef(left_ref, path);
    GitManager::CommandResult right_result = git_manager_->ReadFileAtRef(right_ref, path);

    if (!left_result.success()) {
        left_result.output.clear();
    }
    if (!right_result.success()) {
        right_result.output.clear();
    }

    const std::string left_title = path + " @ " + (left_ref.empty() ? "WORKTREE" : left_ref);
    const std::string right_title = path + " @ " + (right_ref.empty() ? "WORKTREE" : right_ref);

    std::string error;
    if (!on_open_compare_(left_title, left_result.output, right_title, right_result.output, error)) {
        status_ = error.empty() ? "Unable to open side-by-side compare." : error;
        return;
    }

    if (on_close_) {
        on_close_();
    }
}

void GitModalContent::OpenCompareUnifiedDiff() {
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    if (!on_open_compare_) {
        status_ = "Compare open callback is not configured.";
        return;
    }

    const std::string path = SelectedCompareFilePath();
    if (path.empty()) {
        status_ = "No changed file selected.";
        return;
    }

    const std::string left_ref = SelectedCompareLeftRef();
    const std::string right_ref = SelectedCompareRightRef();
    GitManager::CommandResult diff_result = git_manager_->CompareDiff(left_ref, right_ref, path);
    compare_diff_text_ = diff_result.output;
    SplitOutputLines(compare_diff_text_, &compare_diff_lines_);

    if (!diff_result.success()) {
        status_ = "Diff failed: " + TrimForDisplay(diff_result.output, 120);
        return;
    }

    const std::string title = "diff " + (left_ref.empty() ? "WORKTREE" : left_ref) +
        ".." + (right_ref.empty() ? "WORKTREE" : right_ref) + " -- " + path;
    std::string error;
    if (!on_open_compare_(title, compare_diff_text_, "", "", error)) {
        status_ = error.empty() ? "Unable to open unified diff." : error;
        return;
    }

    if (on_close_) {
        on_close_();
    }
}

void GitModalContent::CopyCompareDiff() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }

    const std::string path = SelectedCompareFilePath();
    if (path.empty()) {
        status_ = "No changed file selected.";
        return;
    }

    GitManager::CommandResult diff_result = git_manager_->CompareDiff(
        SelectedCompareLeftRef(),
        SelectedCompareRightRef(),
        path);
    compare_diff_text_ = diff_result.output;
    SplitOutputLines(compare_diff_text_, &compare_diff_lines_);
    compare_diff_scroll_offset_ = 0;

    if (!diff_result.success()) {
        status_ = "Diff failed: " + TrimForDisplay(diff_result.output, 120);
        return;
    }

    write_clipboard_(compare_diff_text_);
    status_ = "Compare diff copied.";
}

ftxui::Element GitModalContent::RenderCompareTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string left_label = selected_compare_left_ref_ >= 0 &&
            selected_compare_left_ref_ < static_cast<int>(compare_refs_.size())
        ? compare_refs_[static_cast<size_t>(selected_compare_left_ref_)].label
        : "-";
    const std::string right_label = selected_compare_right_ref_ >= 0 &&
            selected_compare_right_ref_ < static_cast<int>(compare_refs_.size())
        ? compare_refs_[static_cast<size_t>(selected_compare_right_ref_)].label
        : "-";

    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("Compare refs and files") | ftxui::bold | ftxui::color(theme.modal_accent),
            ftxui::filler(),
            refresh_compare_refs_button_->Render(),
            ftxui::text(" "),
            refresh_compare_files_button_->Render(),
            ftxui::text(" "),
            open_compare_side_button_->Render(),
            ftxui::text(" "),
            open_compare_diff_button_->Render(),
            ftxui::text(" "),
            copy_compare_diff_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::vbox({
                ftxui::text("Left: " + ShortRefLabel(left_label)) |
                    ftxui::bold | ftxui::color(theme.modal_text_color),
                compare_left_ref_menu_->Render() |
                    ftxui::vscroll_indicator |
                    ftxui::frame |
                    ftxui::yflex,
            }) | ftxui::xflex,
            ftxui::separator() | ftxui::color(theme.modal_border),
            ftxui::vbox({
                ftxui::text("Right: " + ShortRefLabel(right_label)) |
                    ftxui::bold | ftxui::color(theme.modal_text_color),
                compare_right_ref_menu_->Render() |
                    ftxui::vscroll_indicator |
                    ftxui::frame |
                    ftxui::yflex,
            }) | ftxui::xflex,
        }) | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 10),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::text("Changed files") | ftxui::bold | ftxui::color(theme.modal_accent),
        compare_file_labels_.empty()
            ? (ftxui::text("No changed files. Press Refresh files after choosing refs.") |
                ftxui::color(theme.modal_text_color) |
                ftxui::frame |
                ftxui::yflex)
            : (compare_file_menu_->Render() |
                ftxui::vscroll_indicator |
                ftxui::frame |
                ftxui::yflex),
    });
}

} // namespace textlt
