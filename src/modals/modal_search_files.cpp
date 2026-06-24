#include "modals/modal_search_files.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include "json_utils.hpp"

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
}

std::string ToDisplayPath(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    return value.empty() ? "." : value;
}

std::string RootLabelForPath(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    if (!filename.empty()) {
        return filename;
    }

    const std::string value = path.generic_string();
    return value.empty() ? "." : value;
}

bool IsDigitsOnly(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isdigit(ch) != 0;
           });
}

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsIgnoredDirectoryName(const std::string& name) {
    static const std::vector<std::string> ignored = {
        ".git",
        ".hg",
        ".svn",
        ".cache",
        ".idea",
        ".vscode",
        "build",
        "cmake-build-debug",
        "cmake-build-release",
        "node_modules",
        "dist",
        "out",
        "target"
    };

    const std::string lower = LowerCopy(name);
    return std::find(ignored.begin(), ignored.end(), lower) != ignored.end();
}

} // namespace

SearchFilesModalContent::SearchFilesModalContent(
    const Theme* theme,
    RootProvider root_provider,
    OpenMatchCallback on_open,
    CloseCallback on_close)
    : theme_(theme),
        root_provider_(std::move(root_provider)),
        on_open_(std::move(on_open)),
        on_close_(std::move(on_close)),
        mask_sets_(FileSearchEngine::DefaultMaskSets()) {
          LoadFilters();

    if (mask_sets_.empty()) {
        mask_sets_.push_back(FileSearchEngine::DefaultCodeMaskSet());
    }

    RebuildFilterLabels();
    UseMaskSet(0);
    SyncFilterInputsFromSelection();

    search_tab_button_ = MakeTabButton("Search", 0);
    results_tab_button_ = MakeTabButton("Results", 1);
    filters_tab_button_ = MakeTabButton("Filters", 2);

    tab_buttons_ = ftxui::Container::Horizontal({
        search_tab_button_,
        results_tab_button_,
        filters_tab_button_,
    });

    ftxui::InputOption query_option;
    query_option.multiline = false;
    query_option.on_enter = [this] { ExecuteSearch(); };
    query_input_ = ftxui::Input(&query_, "text to search", query_option);

    ftxui::InputOption masks_option;
    masks_option.multiline = false;
    masks_option.on_enter = [this] { ExecuteSearch(); };
    masks_input_ = ftxui::Input(&masks_, "*.cpp *.hpp *.txt", masks_option);

    ftxui::MenuOption filter_option = ftxui::MenuOption::Vertical();
    filter_option.on_change = [this] {
        SyncFilterInputsFromSelection();
    };
    filter_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element row = ftxui::text(state.label);
        if (state.focused || state.active) {
            return row |
            ftxui::bgcolor(theme.modal_selected_item_bg) |
            ftxui::color(theme.modal_selected_item_fg) |
            ftxui::bold;
        }

        return row | ftxui::color(theme.modal_text_color);
    };

    filter_menu_ = ftxui::Menu(&filter_labels_, &selected_filter_, filter_option);

    ftxui::InputOption filter_name_option;
    filter_name_option.multiline = false;
    filter_name_option.on_enter = [this] {
        UpdateSelectedFilterFromInputs();
    };
    filter_name_input_component_ =
    ftxui::Input(&filter_name_input_, "filter name", filter_name_option);

    ftxui::InputOption filter_value_option;
    filter_value_option.multiline = false;
    filter_value_option.on_enter = [this] {
        UpdateSelectedFilterFromInputs();
    };
    filter_value_input_component_ =
    ftxui::Input(&filter_value_input_, "file masks", filter_value_option);

    apply_filter_button_ = MakeTextButton("Apply", [this] { ApplySelectedFilter(); });
    add_filter_button_ = MakeTextButton("Add", [this] { AddFilter(); });
    delete_filter_button_ = MakeTextButton("Delete", [this] { DeleteFilter(); });
    save_filters_button_ = MakeTextButton("Save", [this] { SaveFilters(); });

    ftxui::InputOption context_before_option;
    context_before_option.multiline = false;
    context_before_option.on_enter = [this] { ExecuteSearch(); };
    context_before_input_component_ =
        ftxui::Input(&context_before_input_, "before", context_before_option);

    ftxui::InputOption context_after_option;
    context_after_option.multiline = false;
    context_after_option.on_enter = [this] { ExecuteSearch(); };
    context_after_input_component_ =
        ftxui::Input(&context_after_input_, "after", context_after_option);

    start_mask_button_ = MakeTextButton("<< Start", [this] { UseFirstMaskSet(); });
    previous_mask_button_ = MakeTextButton("< Mask", [this] { UsePreviousMaskSet(); });
    next_mask_button_ = MakeTextButton("Mask >", [this] { UseNextMaskSet(); });

    ftxui::MenuOption directory_option = ftxui::MenuOption::Vertical();
    directory_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element row = ftxui::text(state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }

        return row | ftxui::color(theme.modal_text_color);
    };
    directory_menu_ =
        ftxui::Menu(&directory_labels_, &selected_directory_, directory_option);

        directory_list_component_ = ftxui::CatchEvent(
            directory_menu_,
            [this](ftxui::Event event) {
                if (event == ftxui::Event::Character(" ") &&
                    directory_menu_ &&
                    directory_menu_->Focused()) {
                    ToggleSelectedDirectory();
                return true;
                    }

                    return false;
            });

    toggle_directory_button_ = MakeTextButton("Toggle", [this] { ToggleSelectedDirectory(); });
    all_directories_button_ = MakeTextButton("All", [this] { SelectAllDirectories(); });
    none_directories_button_ = MakeTextButton("None", [this] { ClearDirectorySelection(); });

    open_button_ = MakeTextButton("Open", [this] { OpenSelectedMatch(); });

    ftxui::MenuOption result_option = ftxui::MenuOption::Vertical();
        result_option.entries_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        
            ftxui::Element row = ftxui::text(state.label);
            if (state.focused || state.active) {
                return row |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg) |
                    ftxui::bold;
            }
        
            return row | ftxui::color(theme.modal_foreground);
        };

    result_menu_ = ftxui::Menu(&result_labels_, &selected_result_, result_option);

    result_list_component_ = ftxui::CatchEvent(
        result_menu_,
        [this](ftxui::Event event) {
            if (event == ftxui::Event::Return) {
                OpenSelectedMatch();
                return true;
            }

            return false;
        });

    search_tab_container_ = ftxui::Container::Vertical({
        query_input_,
        masks_input_,
        ftxui::Container::Horizontal({
            start_mask_button_,
            previous_mask_button_,
            next_mask_button_,
            context_before_input_component_,
            context_after_input_component_,
        }),
        ftxui::Container::Horizontal({
            toggle_directory_button_,
            all_directories_button_,
            none_directories_button_,
        }),
            directory_list_component_,
    });

    results_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            open_button_,
        }),
        result_list_component_,
    });

    filters_tab_container_ = ftxui::Container::Vertical({
        filter_menu_,
        filter_name_input_component_,
        filter_value_input_component_,
        ftxui::Container::Horizontal({
            apply_filter_button_,
            add_filter_button_,
            delete_filter_button_,
            save_filters_button_,
        }),
    });

    tab_body_container_ = ftxui::Container::Tab({
        search_tab_container_,
        results_tab_container_,
        filters_tab_container_,
    }, &selected_tab_);

    container_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
}

ftxui::Component SearchFilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }

        return button | ftxui::color(theme.modal_accent);
    };

    return ftxui::Button(option);
}

ftxui::Component SearchFilesModalContent::MakeTabButton(
    std::string label,
    int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = [this, tab_index] {
        selected_tab_ = tab_index;
        if (selected_tab_ == 0 && query_input_) {
            query_input_->TakeFocus();
        }
    };
    option.transform = [this, tab_index](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element tab = ftxui::text(BracketLabel(state.label));
        if (selected_tab_ == tab_index || state.focused || state.active) {
            return tab |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }

        return tab | ftxui::color(theme.modal_text_color) | ftxui::dim;
    };

    return ftxui::Button(option);
}

void SearchFilesModalContent::Open() {
    LoadFilters();
    RebuildFilterLabels();
    UseMaskSet(std::min(selected_mask_set_, mask_sets_.size() - 1));
    SyncFilterInputsFromSelection();

    selected_tab_ = 0;
    selected_result_ = 0;
    status_.clear();

    last_clicked_directory_ = -1;
    last_directory_click_time_ = {};

    BuildDirectoryChoices();
    RefreshDirectoryLabels();

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::Close() {
}

void SearchFilesModalContent::ExecuteSearchFromFooter() {
    ExecuteSearch();
}

ftxui::Element SearchFilesModalContent::RenderTitle() {
    return ftxui::hbox({
        search_tab_button_->Render(),
        ftxui::text(" "),
        results_tab_button_->Render(),
        ftxui::text(" "),
        filters_tab_button_->Render(),
    });
}

ftxui::Element SearchFilesModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Element body;
    if (selected_tab_ == 0) {
        body = RenderSearchTab();
    } else if (selected_tab_ == 1) {
        body = RenderResultsTab();
    } else {
        body = RenderFiltersTab();
    }

    return body |
    ftxui::bgcolor(theme.modal_background) |
    ftxui::color(theme.modal_foreground);
}

std::string SearchFilesModalContent::GetFooterText() const {
    if (!status_.empty()) {
        return status_;
    }

    if (selected_tab_ == 0) {
        return DirectorySummaryText();
    }

    if (selected_tab_ == 1) {
        return StatusText();
    }

    return "Filters are saved to search_file_filter.json";
}

void SearchFilesModalContent::BuildDirectoryChoices() {
    directories_.clear();
    directory_labels_.clear();
    selected_directory_ = 0;

    std::vector<FileSearchRoot> roots;
    if (root_provider_) {
        roots = root_provider_();
    }

    std::vector<std::string> seen_paths;

    for (const FileSearchRoot& raw_root : roots) {
        std::error_code error;
        std::filesystem::path absolute_root =
            std::filesystem::absolute(raw_root.path, error).lexically_normal();
        if (error || absolute_root.empty()) {
            continue;
        }

        std::error_code status_error;
        if (!std::filesystem::is_directory(absolute_root, status_error)) {
            continue;
        }

        std::filesystem::directory_iterator iterator(
            absolute_root,
            std::filesystem::directory_options::skip_permission_denied,
            error);

        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;

            std::error_code entry_error;
            if (entry.is_directory(entry_error)) {
                const std::string directory_name = entry.path().filename().string();
                if (!IsIgnoredDirectoryName(directory_name)) {
                    AddDirectoryChoice(
                        FileSearchRoot{entry.path(), directory_name},
                        true,
                        &seen_paths);
                }
            }

            iterator.increment(error);
        }
    }

    if (directories_.empty()) {
        for (const FileSearchRoot& raw_root : roots) {
            std::error_code error;
            std::filesystem::path absolute_root =
                std::filesystem::absolute(raw_root.path, error).lexically_normal();
            if (!error && !absolute_root.empty()) {
                FileSearchRoot fallback = raw_root;
                fallback.path = absolute_root;
                if (fallback.label.empty()) {
                    fallback.label = RootLabelForPath(absolute_root);
                }
                AddDirectoryChoice(fallback, true, &seen_paths);
            }
        }
    }

    if (directories_.empty()) {
        directories_.push_back({
            FileSearchRoot{std::filesystem::current_path(), "."},
            true
        });
    }
}

void SearchFilesModalContent::AddDirectoryChoice(
    const FileSearchRoot& root,
    bool selected,
    std::vector<std::string>* seen_paths) {
    std::error_code error;
    const std::filesystem::path normalized =
        std::filesystem::absolute(root.path, error).lexically_normal();
    if (error) {
        return;
    }

    const std::string key = normalized.generic_string();
    if (seen_paths &&
        std::find(seen_paths->begin(), seen_paths->end(), key) != seen_paths->end()) {
        return;
    }

    if (seen_paths) {
        seen_paths->push_back(key);
    }

    FileSearchRoot stored = root;
    stored.path = normalized;
    if (stored.label.empty()) {
        stored.label = RootLabelForPath(normalized);
    }

    directories_.push_back({stored, selected});
}

void SearchFilesModalContent::RefreshDirectoryLabels() {
    directory_labels_.clear();
    directory_labels_.reserve(directories_.size());

    for (const DirectoryChoice& directory : directories_) {
        directory_labels_.push_back(
            std::string(directory.selected ? "[x] " : "[ ] ") +
            directory.root.label +
            " — " +
            ToDisplayPath(directory.root.path));
    }

    if (selected_directory_ < 0) {
        selected_directory_ = 0;
    }
    if (!directory_labels_.empty() &&
        selected_directory_ >= static_cast<int>(directory_labels_.size())) {
        selected_directory_ = static_cast<int>(directory_labels_.size() - 1);
    }
}

void SearchFilesModalContent::ToggleSelectedDirectory() {
    if (directories_.empty()) {
        return;
    }

    if (selected_directory_ < 0 ||
        selected_directory_ >= static_cast<int>(directories_.size())) {
        selected_directory_ = 0;
    }

    directories_[static_cast<size_t>(selected_directory_)].selected =
        !directories_[static_cast<size_t>(selected_directory_)].selected;

    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

void SearchFilesModalContent::SelectAllDirectories() {
    for (DirectoryChoice& directory : directories_) {
        directory.selected = true;
    }
    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

void SearchFilesModalContent::ClearDirectorySelection() {
    for (DirectoryChoice& directory : directories_) {
        directory.selected = false;
    }
    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

std::vector<FileSearchRoot> SearchFilesModalContent::SelectedRoots() const {
    std::vector<FileSearchRoot> roots;

    for (const DirectoryChoice& directory : directories_) {
        if (directory.selected) {
            roots.push_back(directory.root);
        }
    }

    return roots;
}

void SearchFilesModalContent::UseMaskSet(size_t index) {
    if (mask_sets_.empty()) {
        selected_mask_set_ = 0;
        selected_filter_ = 0;
        masks_.clear();
        return;
    }

    selected_mask_set_ = std::min(index, mask_sets_.size() - 1);
    selected_filter_ = static_cast<int>(selected_mask_set_);
    masks_ = mask_sets_[selected_mask_set_].value;
}

void SearchFilesModalContent::UseFirstMaskSet() {
    UseMaskSet(0);
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::UsePreviousMaskSet() {
    if (mask_sets_.empty()) {
        return;
    }

    if (selected_mask_set_ == 0) {
        selected_mask_set_ = mask_sets_.size() - 1;
    } else {
        --selected_mask_set_;
    }

    masks_ = mask_sets_[selected_mask_set_].value;
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::UseNextMaskSet() {
    if (mask_sets_.empty()) {
        return;
    }

    selected_mask_set_ = (selected_mask_set_ + 1) % mask_sets_.size();
    masks_ = mask_sets_[selected_mask_set_].value;
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::ExecuteSearch() {
    FileSearchOptions options;
    options.roots = SelectedRoots();
    options.query = query_;

    options.mask_set = mask_sets_.empty()
        ? FileSearchEngine::DefaultCodeMaskSet()
        : mask_sets_[selected_mask_set_];
    options.mask_set.value = masks_;

    options.context_before = ParseContextValue(context_before_input_);
    options.context_after = ParseContextValue(context_after_input_);

    summary_ = engine_.Search(options);
    RebuildResultLabels();
    
    selected_tab_ = 1;
    
    if (result_list_component_) {
        result_list_component_->TakeFocus();
    }

    if (summary_.HasErrors() && summary_.matches.empty()) {
        status_ = summary_.FirstError();
    } else {
        status_ = StatusText();
    }

    if (open_button_) {
        open_button_->TakeFocus();
    }
}

void SearchFilesModalContent::OpenSelectedMatch() {
    if (summary_.matches.empty()) {
        status_ = "No result selected.";
        return;
    }

    ClampResultSelection();

    if (!on_open_) {
        status_ = "Open callback is not configured.";
        return;
    }

    std::string error;
    const FileSearchMatch& match =
    summary_.matches[static_cast<size_t>(selected_result_)];

    if (!on_open_(match, error)) {
        status_ = error.empty() ? "Unable to open selected result." : error;
        return;
    }

    if (on_close_) {
        on_close_();
    }
}

void SearchFilesModalContent::MoveResultSelection(int delta) {
    if (summary_.matches.empty()) {
        selected_result_ = 0;
        return;
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    selected_result_ = std::max(0, std::min(max_index, selected_result_ + delta));
}

void SearchFilesModalContent::ClampResultSelection() {
    if (summary_.matches.empty()) {
        selected_result_ = 0;
        return;
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    selected_result_ = std::max(0, std::min(max_index, selected_result_));
}

void SearchFilesModalContent::RebuildResultLabels() {
    result_labels_.clear();
    result_labels_.reserve(summary_.matches.size());

    for (const FileSearchMatch& match : summary_.matches) {
        result_labels_.push_back(
            FormatLocation(match) +
            "  " +
            TrimForDisplay(match.line_text, 170));
    }

    selected_result_ = 0;
    ClampResultSelection();
}

size_t SearchFilesModalContent::ParseContextValue(const std::string& value) const {
    if (!IsDigitsOnly(value)) {
        return 0;
    }

    try {
        return std::min<size_t>(static_cast<size_t>(std::stoul(value)), 20);
    } catch (const std::exception&) {
        return 0;
    }
}

ftxui::Element SearchFilesModalContent::RenderSearchTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    const std::string mask_name = mask_sets_.empty()
        ? "Custom"
        : mask_sets_[selected_mask_set_].name;

    return ftxui::vbox({
    ftxui::text(""),

    ftxui::hbox({
        ftxui::text("Search: ") | ftxui::color(theme.modal_text_color),
        query_input_->Render() |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg) |
            ftxui::xflex,
    }),

    ftxui::text(""),

    ftxui::hbox({
        ftxui::text("Masks:  ") | ftxui::color(theme.modal_text_color),

            masks_input_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
        ftxui::hbox({
            ftxui::text("Set: " + mask_name + " ") |
                ftxui::color(theme.modal_text_color),
            start_mask_button_->Render(),
            ftxui::text(" "),
            previous_mask_button_->Render(),
            ftxui::text(" "),
            next_mask_button_->Render(),
            ftxui::text("   Context before: ") |
                ftxui::color(theme.modal_text_color),
            context_before_input_component_->Render() |
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 5) |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg),
            ftxui::text(" after: ") |
                ftxui::color(theme.modal_text_color),
            context_after_input_component_->Render() |
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 5) |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Directories: ") |
                ftxui::bold |
                ftxui::color(theme.modal_accent),
            ftxui::filler(),
            toggle_directory_button_->Render(),
            ftxui::text(" "),
            all_directories_button_->Render(),
            ftxui::text(" "),
            none_directories_button_->Render(),
        }),
        RenderDirectoryList() | ftxui::yflex,
    });
}

ftxui::Element SearchFilesModalContent::RenderResultsTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text(StatusText()) | ftxui::color(theme.modal_text_color),
            ftxui::filler(),
            open_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderResultList() | ftxui::yflex,
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderSelectedResultPreview(),
    });
}

ftxui::Element SearchFilesModalContent::RenderDirectoryList() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (directory_labels_.empty()) {
        return ftxui::text("No directories.") | ftxui::color(theme.modal_text_color);
    }

    return directory_list_component_->Render() |
        ftxui::vscroll_indicator |
        ftxui::frame;
}

ftxui::Element SearchFilesModalContent::RenderResultList() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (summary_.matches.empty()) {
        const std::string message = query_.empty()
            ? "Enter a query on the Search tab and press [Search]."
            : "No matches.";
        return ftxui::text(message) |
            ftxui::color(theme.modal_text_color) |
            ftxui::frame;
    }

    return result_list_component_->Render() |
        ftxui::vscroll_indicator |
        ftxui::frame;
}

ftxui::Element SearchFilesModalContent::RenderSelectedResultPreview() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (summary_.matches.empty()) {
        return ftxui::text("No selected result.") |
            ftxui::color(theme.modal_text_color);
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    const int safe_index = std::max(0, std::min(max_index, selected_result_));
    const FileSearchMatch& match = summary_.matches[static_cast<size_t>(safe_index)];

    ftxui::Elements rows;
    rows.push_back(
        ftxui::text(FormatLocation(match)) |
        ftxui::bold |
        ftxui::color(theme.modal_accent));

    for (const FileSearchContextLine& line : match.before) {
        rows.push_back(RenderContextLine(line));
    }

    rows.push_back(
        ftxui::text(
            "> " +
            FormatLineNumber(match.line_number) +
            " | " +
            TrimForDisplay(match.line_text, 170)) |
        ftxui::bgcolor(theme.modal_selected_item_bg) |
        ftxui::color(theme.modal_selected_item_fg) |
        ftxui::bold);

    for (const FileSearchContextLine& line : match.after) {
        rows.push_back(RenderContextLine(line));
    }

    return ftxui::vbox(std::move(rows)) |
        ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 9);
}

ftxui::Element SearchFilesModalContent::RenderMatch(
    const FileSearchMatch& match,
    size_t index,
    bool selected) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Elements rows;

    for (const FileSearchContextLine& line : match.before) {
        rows.push_back(RenderContextLine(line));
    }

    const std::string prefix = selected ? "> " : "  ";
    ftxui::Element main_line = ftxui::text(
        prefix +
        FormatLocation(match) +
        "  " +
        TrimForDisplay(match.line_text, 170));

    if (selected) {
        main_line = main_line |
            ftxui::bgcolor(theme.modal_selected_item_bg) |
            ftxui::color(theme.modal_selected_item_fg) |
            ftxui::bold;
    } else {
        main_line = main_line | ftxui::color(theme.modal_foreground);
    }

    rows.push_back(main_line);

    for (const FileSearchContextLine& line : match.after) {
        rows.push_back(RenderContextLine(line));
    }

    if (index + 1 < summary_.matches.size()) {
        rows.push_back(ftxui::text(""));
    }

    return ftxui::vbox(std::move(rows));
}

ftxui::Element SearchFilesModalContent::RenderContextLine(
    const FileSearchContextLine& line) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::text(
        "  " +
        FormatLineNumber(line.line_number) +
        " | " +
        TrimForDisplay(line.text, 170)) |
        ftxui::color(theme.modal_text_color) |
        ftxui::dim;
}

std::string SearchFilesModalContent::StatusText() const {
    std::ostringstream stream;
    stream << summary_.matches.size() << " match(es), "
           << summary_.files_with_matches << " file(s), "
           << summary_.files_scanned << " scanned";

    if (summary_.files_skipped > 0) {
        stream << ", " << summary_.files_skipped << " skipped";
    }

    if (summary_.truncated) {
        stream << ", truncated";
    }

    if (summary_.HasErrors()) {
        stream << ", warning: " << summary_.FirstError();
    }

    return stream.str();
}

std::string SearchFilesModalContent::DirectorySummaryText() const {
    size_t selected = 0;
    for (const DirectoryChoice& directory : directories_) {
        if (directory.selected) {
            ++selected;
        }
    }

    return std::to_string(selected) + " of " +
        std::to_string(directories_.size()) +
        " directorie(s) selected";
}

std::string SearchFilesModalContent::FormatLocation(
    const FileSearchMatch& match) const {
    return ToDisplayPath(match.relative_path) +
        ":" +
        std::to_string(match.line_number) +
        ":" +
        std::to_string(match.column);
}

std::string SearchFilesModalContent::FormatLineNumber(size_t line_number) const {
    std::string value = std::to_string(line_number);
    if (value.size() < 6) {
        value.insert(value.begin(), 6 - value.size(), ' ');
    }
    return value;
}

std::string SearchFilesModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }

    if (max_size <= 3) {
        return text.substr(0, max_size);
    }

    return text.substr(0, max_size - 3) + "...";
}

std::filesystem::path SearchFilesModalContent::FilterConfigPath() const {
    #ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && std::string(app_data).size() > 0) {
        return std::filesystem::path(app_data) / "textlt" / "search_file_filter.json";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).size() > 0) {
        return std::filesystem::path(user_profile) /
        "AppData" / "Roaming" / "textlt" / "search_file_filter.json";
    }

    return std::filesystem::path("search_file_filter.json");
    #else
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config_home && std::string(xdg_config_home).size() > 0) {
        return std::filesystem::path(xdg_config_home) /
        "textlt" / "search_file_filter.json";
    }

    const char* home = std::getenv("HOME");
    if (home && std::string(home).size() > 0) {
        return std::filesystem::path(home) /
        ".config" / "textlt" / "search_file_filter.json";
    }

    return std::filesystem::path("search_file_filter.json");
    #endif
}

void SearchFilesModalContent::LoadFilters() {
    std::vector<FileSearchMaskSet> loaded;

    const std::filesystem::path path = FilterConfigPath();
    std::error_code exists_error;
    if (std::filesystem::exists(path, exists_error)) {
        std::ifstream file(path, std::ios::binary);
        if (file) {
            const Json root = Json::parse(file, nullptr, false);
            if (!root.is_discarded() && root.is_object()) {
                const auto filters = root.find("filters");
                if (filters != root.end() && filters->is_array()) {
                    for (const Json& item : *filters) {
                        if (!item.is_object()) {
                            continue;
                        }

                        const std::string name = JsonString(item, "name");
                        const std::string value = JsonString(item, "value");

                        if (!name.empty() && !value.empty()) {
                            loaded.push_back({name, value});
                        }
                    }
                }
            }
        }
    }

    if (loaded.empty()) {
        loaded = FileSearchEngine::DefaultMaskSets();
    }

    if (loaded.empty()) {
        loaded.push_back(FileSearchEngine::DefaultCodeMaskSet());
    }

    mask_sets_ = std::move(loaded);

    if (selected_mask_set_ >= mask_sets_.size()) {
        selected_mask_set_ = 0;
    }
    selected_filter_ = static_cast<int>(selected_mask_set_);
}

void SearchFilesModalContent::SaveFilters() {
    UpdateSelectedFilterFromInputs();

    const std::filesystem::path path = FilterConfigPath();

    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);

    Json root = Json::object();
    root["filters"] = Json::array();

    for (const FileSearchMaskSet& filter : mask_sets_) {
        Json item = Json::object();
        item["name"] = filter.name;
        item["value"] = filter.value;
        root["filters"].push_back(item);
    }

    if (WriteJsonAtomically(path, root)) {
        status_ = "Filters saved: " + path.string();
    } else {
        status_ = "Unable to save filters: " + path.string();
    }

    RebuildFilterLabels();
}

void SearchFilesModalContent::SaveFiltersFromFooter() {
    SaveFilters();
}

void SearchFilesModalContent::RebuildFilterLabels() {
    filter_labels_.clear();
    filter_labels_.reserve(mask_sets_.size());

    for (const FileSearchMaskSet& filter : mask_sets_) {
        filter_labels_.push_back(
            filter.name + " — " + TrimForDisplay(filter.value, 90));
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (!filter_labels_.empty() &&
        selected_filter_ >= static_cast<int>(filter_labels_.size())) {
        selected_filter_ = static_cast<int>(filter_labels_.size() - 1);
        }
}

void SearchFilesModalContent::SyncFilterInputsFromSelection() {
    if (mask_sets_.empty()) {
        filter_name_input_.clear();
        filter_value_input_.clear();
        selected_mask_set_ = 0;
        selected_filter_ = 0;
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    filter_name_input_ = mask_sets_[selected_mask_set_].name;
    filter_value_input_ = mask_sets_[selected_mask_set_].value;
}

void SearchFilesModalContent::UpdateSelectedFilterFromInputs() {
    if (mask_sets_.empty()) {
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    if (filter_name_input_.empty()) {
        filter_name_input_ = "New Filter";
    }

    if (filter_value_input_.empty()) {
        filter_value_input_ = "*";
    }

    mask_sets_[selected_mask_set_].name = filter_name_input_;
    mask_sets_[selected_mask_set_].value = filter_value_input_;

    RebuildFilterLabels();
}

void SearchFilesModalContent::ApplySelectedFilter() {
    UpdateSelectedFilterFromInputs();

    if (mask_sets_.empty()) {
        status_ = "No filters available.";
        return;
    }

    UseMaskSet(static_cast<size_t>(selected_filter_));
    selected_tab_ = 0;
    status_ = "Applied filter: " + mask_sets_[selected_mask_set_].name;

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::AddFilter() {
    UpdateSelectedFilterFromInputs();

    FileSearchMaskSet filter;
    filter.name = filter_name_input_.empty() ? "New Filter" : filter_name_input_;
    filter.value = filter_value_input_.empty() ? masks_ : filter_value_input_;

    if (filter.value.empty()) {
        filter.value = "*";
    }

    mask_sets_.push_back(filter);

    selected_mask_set_ = mask_sets_.size() - 1;
    selected_filter_ = static_cast<int>(selected_mask_set_);

    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    status_ = "Filter added.";
}

void SearchFilesModalContent::DeleteFilter() {
    if (mask_sets_.size() <= 1) {
        status_ = "At least one filter is required.";
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    mask_sets_.erase(mask_sets_.begin() + selected_filter_);

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    status_ = "Filter deleted.";
}

ftxui::Element SearchFilesModalContent::RenderFiltersTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        ftxui::text(""),
                       ftxui::hbox({
                           ftxui::text("Filters:") |
                           ftxui::bold |
                           ftxui::color(theme.modal_accent),
                                   ftxui::filler(),
                                   apply_filter_button_->Render(),
                                   ftxui::text(" "),
                                   add_filter_button_->Render(),
                                   ftxui::text(" "),
                                   delete_filter_button_->Render(),
                                   ftxui::text(" "),
                                   save_filters_button_->Render(),
                       }),
                       ftxui::separator() | ftxui::color(theme.modal_border),
                       ftxui::hbox({
                           filter_menu_->Render() |
                           ftxui::vscroll_indicator |
                           ftxui::frame |
                           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 45) |
                           ftxui::yflex,

                           ftxui::separator() | ftxui::color(theme.modal_border),

                                   ftxui::vbox({
                                       ftxui::text("Name:") | ftxui::color(theme.modal_text_color),
                                               filter_name_input_component_->Render() |
                                               ftxui::bgcolor(theme.modal_input_bg) |
                                               ftxui::color(theme.modal_input_fg),

                                               ftxui::text(""),
                                               ftxui::text("Masks:") | ftxui::color(theme.modal_text_color),
                                               filter_value_input_component_->Render() |
                                               ftxui::bgcolor(theme.modal_input_bg) |
                                               ftxui::color(theme.modal_input_fg),

                                               ftxui::text(""),
                                               ftxui::paragraph(
                                                   "Apply copies the selected filter into the Search tab. "
                                                   "Save writes search_file_filter.json.") |
                                                   ftxui::color(theme.modal_text_color) |
                                                   ftxui::dim,
                                   }) | ftxui::xflex,
                       }) | ftxui::yflex,
    });
}

SearchFilesModal::SearchFilesModal(
    const Theme* theme,
    RootProvider root_provider,
    OpenMatchCallback on_open)
    : theme_(theme) {
        content_ = std::make_shared<SearchFilesModalContent>(
            theme_,
            std::move(root_provider),
            std::move(on_open),
            [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });

    modal_->SetFooterButtons({
        {"Search", [this] {
            if (content_) {
                content_->ExecuteSearchFromFooter();
            }
        }},
        {"Save", [this] {
            if (content_) {
                content_->SaveFiltersFromFooter();
            }
        }},
        {"Close", [this] { Close(); }},
    });

    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component SearchFilesModal::View() const {
    return modal_;
}

void SearchFilesModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void SearchFilesModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool SearchFilesModal::IsOpen() const {
    return open_;
}

bool SearchFilesModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (content_ && event.is_mouse()) {
        const bool modal_handled = modal_->OnEvent(event);

        if (content_->HandleEvent(std::move(event))) {
            return true;
        }

        return modal_handled;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}


bool SearchFilesModalContent::HandleEvent(ftxui::Event event) {
    if (selected_tab_ == 0) {
        return HandleDirectoryMouseEvent(event);
    }

    if (selected_tab_ != 1) {
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        MoveResultSelection(1);
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        MoveResultSelection(-1);
        return true;
    }

    if (event == ftxui::Event::PageDown) {
        MoveResultSelection(10);
        return true;
    }

    if (event == ftxui::Event::PageUp) {
        MoveResultSelection(-10);
        return true;
    }

    if (event == ftxui::Event::Return) {
        OpenSelectedMatch();
        return true;
    }

    return HandleResultsMouseEvent(event);
}

bool SearchFilesModalContent::HandleResultsMouseEvent(ftxui::Event event) {
    if (!event.is_mouse() || selected_tab_ != 1) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();

    if (mouse.button == ftxui::Mouse::WheelDown) {
        MoveResultSelection(3);
        return true;
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
        MoveResultSelection(-3);
        return true;
    }

    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
        }

        const int clicked_result = selected_result_;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_result_click_time_).count();

        const bool is_double_click =
        last_clicked_result_ == clicked_result &&
        elapsed_ms >= 0 &&
        elapsed_ms <= 500;

        last_result_click_time_ = now;
        last_clicked_result_ = clicked_result;

        if (is_double_click) {
            OpenSelectedMatch();
            return true;
        }

        return false;
}

bool SearchFilesModalContent::HandleDirectoryMouseEvent(ftxui::Event event) {
    if (!event.is_mouse() || selected_tab_ != 0) {
        return false;
    }

    if (!directory_menu_ || !directory_menu_->Focused()) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();

    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
        }

        if (directories_.empty()) {
            return false;
        }

        if (selected_directory_ < 0) {
            selected_directory_ = 0;
        }

        if (selected_directory_ >= static_cast<int>(directories_.size())) {
            selected_directory_ = static_cast<int>(directories_.size() - 1);
        }

        const int clicked_directory = selected_directory_;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_directory_click_time_).count();

            const bool is_double_click =
            last_clicked_directory_ == clicked_directory &&
            elapsed_ms >= 0 &&
            elapsed_ms <= 500;

            last_directory_click_time_ = now;
            last_clicked_directory_ = clicked_directory;

            if (is_double_click) {
                ToggleSelectedDirectory();
                last_clicked_directory_ = -1;
                return true;
            }

            return false;
}

} // namespace textlt
