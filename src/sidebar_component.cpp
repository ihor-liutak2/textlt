#include "sidebar_component.hpp"

#include "path_utils.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/string.hpp"
#include "workspace_tabs.hpp"

namespace textlt {
namespace {

std::string DisplayName(const std::filesystem::path& path) {
    return Utf8FromPath(path.filename().empty() ? path : path.filename());
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

std::string VisibleLabel(const std::string& label, int offset, int width) {
    if (offset == 0 && ftxui::string_width(label) <= width) {
        return label;
    }
    const auto glyphs = ftxui::Utf8ToGlyphs(label);
    const int end = std::min(offset + width, static_cast<int>(glyphs.size()));
    std::string visible;
    for (int column = offset; column < end; ++column) {
        const std::string& glyph = glyphs[column];
        const int glyph_width = ftxui::string_width(glyph);
        // A full-width glyph must fit on both sides of the viewport.
        if (column == offset && glyph_width == 0) {
            visible += ' ';
        } else if (column != end - 1 || glyph_width != 2) {
            visible += glyph;
        }
    }
    return visible;
}

int BoxWidth(const ftxui::Box& box) {
    return box.IsEmpty() ? 0 : box.x_max - box.x_min + 1;
}

struct HorizontalScrollbar {
    int width;
    int max_offset;
    int thumb_width = 0;
    int thumb_start = 0;

    HorizontalScrollbar(int viewport_width, int label_width, int offset)
        : width(viewport_width), max_offset(std::max(0, label_width - width)) {
        if (width > 0 && max_offset > 0) {
            thumb_width = std::max(1, static_cast<int>(1LL * width * width / label_width));
            thumb_start = static_cast<int>(1LL * offset * Travel() / max_offset);
        }
    }

    int Travel() const { return width - thumb_width; }
};

} // namespace

class SidebarPanel::EntryList final : public ftxui::Node {
public:
    EntryList(SidebarPanel& sidebar, const Theme& theme)
        : sidebar_(sidebar), theme_(theme) {}

    void ComputeRequirement() override { requirement_.min_y = 1; }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        sidebar_.scrollbar_box_ = {box.x_min, box.x_max, box.y_max, box.y_max};
        sidebar_.menu_box_ = box;
        --sidebar_.menu_box_.y_max;
        children_.clear();

        if (box.IsEmpty()) {
            sidebar_.scrollbar_box_ = box;
            sidebar_.scrollbar_drag_.reset();
            return;
        }

        const int width = BoxWidth(box);
        sidebar_.ScrollLabels(0);
        sidebar_.ClampScrollOffset();

        size_t index = sidebar_.list_scroll_offset_;
        for (int y = box.y_min; y < box.y_max && index < sidebar_.entry_labels_.size(); ++y, ++index) {
            AddRow(sidebar_.RenderEntry(index, width, theme_), y);
        }

        const HorizontalScrollbar scrollbar(width, sidebar_.widest_label_, sidebar_.label_scroll_offset_);
        if (scrollbar.max_offset == 0) {
            sidebar_.scrollbar_drag_.reset();
            return;
        }

        using namespace ftxui;
        const auto track = [this](int length) {
            std::string line;
            for (int column = 0; column < length; ++column) {
                line += "─";
            }
            return text(std::move(line)) | color(SidebarBorderColor(theme_));
        };
        AddRow(hbox({
            track(scrollbar.thumb_start),
            text(std::string(scrollbar.thumb_width, ' ')) | bgcolor(theme_.modal_selected_item_bg),
            track(width - scrollbar.thumb_start - scrollbar.thumb_width),
        }), box.y_max);
    }

private:
    void AddRow(ftxui::Element row, int y) {
        row->ComputeRequirement();
        row->SetBox({box_.x_min, box_.x_max, y, y});
        children_.push_back(std::move(row));
    }

    SidebarPanel& sidebar_;
    Theme theme_;
};

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
    ShortcutLabelProvider shortcut_label_provider,
    OpenNotesCallback on_open_notes,
    OpenActionsCallback on_open_actions,
    OpenDocumentsCallback on_open_documents,
    ProjectIdCallback on_action_group_select,
    ActionCommandCallback on_action_command,
    ProjectsProvider projects_provider,
    ProjectIdCallback on_project_select,
    AddProjectCallback on_project_add,
    ProjectIdCallback on_project_rename,
    ProjectIdCallback on_project_remove,
    ProjectIdCallback on_project_remote,
    ProjectIdCallback on_project_assign_remote,
    ProjectIdCallback on_project_edit_remote,
    ProjectIdCallback on_project_unlink_remote)
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
      on_open_notes_(std::move(on_open_notes)),
      on_open_actions_(std::move(on_open_actions)),
      on_open_documents_(std::move(on_open_documents)),
      on_action_group_select_(std::move(on_action_group_select)),
      on_action_command_(std::move(on_action_command)),
      projects_provider_(std::move(projects_provider)),
      on_project_select_(std::move(on_project_select)),
      on_project_add_(std::move(on_project_add)),
      on_project_rename_(std::move(on_project_rename)),
      on_project_remove_(std::move(on_project_remove)),
      on_project_remote_(std::move(on_project_remote)),
      on_project_assign_remote_(std::move(on_project_assign_remote)),
      on_project_edit_remote_(std::move(on_project_edit_remote)),
      on_project_unlink_remote_(std::move(on_project_unlink_remote)),
      project_root_(std::filesystem::current_path()),
      current_path_(project_root_) {
    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    option.on_enter = [this] { OpenSelectedEntry(); };
    menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, option);
    Add(menu_);
    RebuildEntries();
}

ftxui::Element SidebarPanel::OnRender() {
    using namespace ftxui;

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const ftxui::Color sidebar_text = SidebarTextColor(theme);

    return vbox({
        RenderWorkspaceTabs(
            theme,
            mode_ == SidebarMode::Actions
                ? WorkspaceTab::Actions
                : WorkspaceTab::Documents,
            documents_tab_box_, notes_tab_box_, actions_tab_box_),
        separator() | color(SidebarBorderColor(theme)),
        mode_ == SidebarMode::Actions
            ? emptyElement()
            : hbox({
                  RenderModeButton("Folder", SidebarMode::Folder, project_tab_box_),
                  text(" "),
                  RenderModeButton("Projects", SidebarMode::Projects, projects_tab_box_),
              }),
        mode_ == SidebarMode::Actions
            ? emptyElement()
            : hbox({
                  RenderModeButton("Opened", SidebarMode::Opened, opened_tab_box_),
                  text(" "),
                  RenderModeButton("Favorites", SidebarMode::Favorites, favorites_tab_box_),
              }),
        mode_ == SidebarMode::Folder
            ? text(" " + DisplayName(project_root_) + "/") | bold | color(sidebar_text)
            : emptyElement(),
        mode_ == SidebarMode::Actions
            ? emptyElement()
            : separator() | color(SidebarBorderColor(theme)),
        RenderActionRow(),
        separator() | color(SidebarBorderColor(theme)),
        std::make_shared<EntryList>(*this, theme) | yflex,
    }) |
        borderStyled(ftxui::LIGHT, SidebarBorderColor(theme)) |
        reflect(panel_box_);
}

ftxui::Element SidebarPanel::RenderEntry(size_t index, int width, const Theme& theme) const {
    using namespace ftxui;
    Color label_color = SidebarTextColor(theme);
    if (mode_ == SidebarMode::Folder &&
        (entry_kinds_[index] == EntryKind::Directory || entry_kinds_[index] == EntryKind::File)) {
        const char status = GitStatusForPath(entry_paths_[index]);
        if (status == 'M') {
            label_color = theme.syntax_keyword;
        } else if (status == '?' || status == 'A') {
            label_color = theme.syntax_string;
        }
    }
    Element row = text(VisibleLabel(entry_labels_[index], label_scroll_offset_, width));
    if (static_cast<int>(index) == selected_entry_) {
        return row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
    }
    return row | color(label_color);
}

void SidebarPanel::ClearHitboxes() {
    const ftxui::Box empty{1, 0, 1, 0};
    panel_box_ = empty;
    menu_box_ = empty;
    scrollbar_box_ = empty;
    scrollbar_drag_.reset();
    opened_tab_box_ = empty;
    project_tab_box_ = empty;
    projects_tab_box_ = empty;
    favorites_tab_box_ = empty;
    notes_tab_box_ = empty;
    actions_tab_box_ = empty;
    documents_tab_box_ = empty;
    action_primary_box_ = empty;
    action_secondary_box_ = empty;
    action_tertiary_box_ = empty;
    action_quaternary_box_ = empty;
    action_quinary_box_ = empty;
    project_rename_box_ = empty;
    group_new_box_ = empty;
    group_rename_box_ = empty;
    group_delete_box_ = empty;
    group_merge_box_ = empty;
    page_new_box_ = empty;
    page_rename_box_ = empty;
    page_delete_box_ = empty;
    item_add_box_ = empty;
    item_edit_box_ = empty;
    item_delete_box_ = empty;
    item_move_up_box_ = empty;
    item_move_down_box_ = empty;
}

bool SidebarPanel::OnEvent(ftxui::Event event) {
    if (event.is_mouse() && HandleScrollbarMouse(event, event.mouse())) {
        return true;
    }

    // Action-view controls share the sidebar with the workspace tabs. Handle
    // their hitboxes first so a stale/overlapping tab box cannot turn a Delete
    // click into a Documents workspace switch.
    if (mode_ == SidebarMode::Actions &&
        event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        HandleActionButtonMouse(event.mouse())) {
        return true;
    }

    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        documents_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        if (on_open_documents_) on_open_documents_();
        return true;
    }
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        actions_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        if (on_open_actions_) on_open_actions_();
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        notes_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        if (on_open_notes_) {
            on_open_notes_();
        }
        return true;
    }
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

    if ((event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) &&
        BoxWidth(scrollbar_box_) > 0 && widest_label_ > BoxWidth(scrollbar_box_)) {
        ScrollLabels(event == ftxui::Event::ArrowLeft ? -1 : 1);
        return true;
    }

    if (event == ftxui::Event::Backspace && mode_ == SidebarMode::Folder) {
        const std::filesystem::path parent = current_path_.parent_path();
        if (current_path_ != project_root_ && !parent.empty() && parent != current_path_) {
            current_path_ = parent;
            RebuildEntries();
            list_scroll_offset_ = 0;
            label_scroll_offset_ = 0;
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
        SetMode(SidebarMode::Folder);
        return true;
    }

    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        projects_tab_box_.Contain(event.mouse().x, event.mouse().y)) {
        SetMode(SidebarMode::Projects);
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
            if (mode_ == SidebarMode::Opened || mode_ == SidebarMode::Favorites ||
                mode_ == SidebarMode::Actions) {
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
    SetMode(SidebarMode::Folder);
}

void SidebarPanel::ShowProjects() {
    SetMode(SidebarMode::Projects);
}

void SidebarPanel::ShowFavorites() {
    SetMode(SidebarMode::Favorites);
}

void SidebarPanel::ShowActions() { SetMode(SidebarMode::Actions); }

void SidebarPanel::SetActionGroups(std::vector<ActionGroupEntry> groups,
    const std::string& selected_id) {
    action_group_entries_ = std::move(groups);
    selected_action_group_id_ = selected_id;
    if (mode_ == SidebarMode::Actions) RebuildEntries();
}

void SidebarPanel::ToggleOpenedProject() {
    SetMode(mode_ == SidebarMode::Opened ? SidebarMode::Folder : SidebarMode::Opened);
}

void SidebarPanel::SetMode(SidebarMode mode) {
    if (mode_ == mode) {
        return;
    }

    mode_ = mode;
    selected_entry_ = 0;
    list_scroll_offset_ = 0;
    label_scroll_offset_ = 0;
    scrollbar_drag_.reset();
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
    if (selected_kind == EntryKind::ActionGroup) {
        if (on_action_group_select_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            selected_action_group_id_ = entry_ids_[selected_entry_];
            on_action_group_select_(selected_action_group_id_);
        }
        return;
    }
    std::error_code error;
    if (selected_kind == EntryKind::OpenedFile) {
        if (on_opened_file_select_) {
            on_opened_file_select_(static_cast<size_t>(selected_entry_));
        }
        return;
    }

    if (selected_kind == EntryKind::Project) {
        last_clicked_entry_ = -1;
        last_click_time_ = {};
        if (on_project_select_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            on_project_select_(entry_ids_[selected_entry_]);
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
    if (mode_ != SidebarMode::Folder ||
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
    label_scroll_offset_ = 0;
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

void SidebarPanel::ScrollLabels(int delta) {
    const int max_offset = std::max(0, widest_label_ - BoxWidth(scrollbar_box_));
    label_scroll_offset_ = std::clamp(label_scroll_offset_ + delta, 0, max_offset);
}

bool SidebarPanel::HandleScrollbarMouse(const ftxui::Event& event, const ftxui::Mouse& mouse) {
    using ftxui::Mouse;
    if (scrollbar_drag_ && mouse.motion == Mouse::Released) {
        scrollbar_drag_.reset();
        return true;
    }

    const HorizontalScrollbar scrollbar(BoxWidth(scrollbar_box_), widest_label_, label_scroll_offset_);
    if (scrollbar.width == 0 || scrollbar.max_offset == 0) {
        return false;
    }

    if (panel_box_.Contain(mouse.x, mouse.y)) {
        if (mouse.button == Mouse::WheelLeft || (mouse.shift && mouse.button == Mouse::WheelUp)) {
            ScrollLabels(-3);
            return true;
        }
        if (mouse.button == Mouse::WheelRight || (mouse.shift && mouse.button == Mouse::WheelDown)) {
            ScrollLabels(3);
            return true;
        }
    }

    if (scrollbar.Travel() == 0) {
        return false;
    }
    const int column = mouse.x - scrollbar_box_.x_min;
    if (mouse.motion == Mouse::Pressed && mouse.button == Mouse::Left &&
        scrollbar_box_.Contain(mouse.x, mouse.y)) {
        scrollbar_drag_ = CaptureMouse(event);
        if (!scrollbar_drag_) {
            return false;
        }
        const int relative_to_thumb = column - scrollbar.thumb_start;
        if (relative_to_thumb >= 0 && relative_to_thumb < scrollbar.thumb_width) {
            scrollbar_grab_column_ = relative_to_thumb;
            return true;
        }
        scrollbar_grab_column_ = scrollbar.thumb_width / 2;
    } else if (!scrollbar_drag_ || mouse.motion != Mouse::Moved) {
        return false;
    }

    const int travel = scrollbar.Travel();
    const int thumb_start = std::clamp(column - scrollbar_grab_column_, 0, travel);
    if (thumb_start != scrollbar.thumb_start) {
        label_scroll_offset_ = static_cast<int>((1LL * thumb_start * scrollbar.max_offset + travel - 1) / travel);
    }
    return true;
}

std::string SidebarPanel::ShortcutLabelForMode(SidebarMode mode) const {
    std::string command_id;
    std::string fallback;
    switch (mode) {
        case SidebarMode::Folder:
            command_id = "sidebar.show_project";
            fallback = "P";
            break;
        case SidebarMode::Projects:
            fallback = "";
            break;
        case SidebarMode::Favorites:
            command_id = "sidebar.show_favorites";
            fallback = "F";
            break;
        case SidebarMode::Opened:
            command_id = "sidebar.show_opened_files";
            fallback = "O";
            break;
        case SidebarMode::Actions:
            fallback = "";
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
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const Color label_color = SidebarTextColor(theme);

    action_primary_box_ = {};
    action_secondary_box_ = {};
    action_tertiary_box_ = {};
    action_quaternary_box_ = {};
    action_quinary_box_ = {};
    project_rename_box_ = {1, 0, 1, 0};
    group_new_box_ = {};
    group_rename_box_ = {};
    group_delete_box_ = {};
    group_merge_box_ = {};
    page_new_box_ = {};
    page_rename_box_ = {};
    page_delete_box_ = {};
    item_add_box_ = {};
    item_edit_box_ = {};
    item_delete_box_ = {};
    item_move_up_box_ = {};
    item_move_down_box_ = {};

    if (mode_ == SidebarMode::Projects) {
        const bool selected = selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(project_entries_.size());
        const bool has_remote = selected && project_entries_[selected_entry_].has_remote;
        const bool missing_remote = selected && project_entries_[selected_entry_].remote_missing;
        Element remote_actions = has_remote && !missing_remote
            ? hbox({
                  RenderFlatActionButton("Files", ButtonRole::Primary, action_primary_box_),
                  text(" "),
                  RenderFlatActionButton("Edit", ButtonRole::Primary, action_quaternary_box_),
                  text(" "),
                  RenderFlatActionButton("Unlink", ButtonRole::Warning, action_quinary_box_),
              })
            : hbox({
                  RenderFlatActionButton(
                      "Set",
                      ButtonRole::Primary,
                      action_primary_box_),
                  has_remote
                      ? hbox({
                            text(" "),
                            RenderFlatActionButton("Unlink", ButtonRole::Warning, action_quinary_box_),
                        })
                      : emptyElement(),
              });
        return vbox({
            text(" Projects:") | bold | color(label_color),
            hbox({
                RenderFlatActionButton("Add", ButtonRole::Primary, action_secondary_box_),
                text(" "),
                selected ? RenderFlatActionButton("Rename", ButtonRole::Utility, project_rename_box_)
                         : emptyElement(),
                selected ? text(" ") : emptyElement(),
                RenderFlatActionButton("Remove", ButtonRole::Danger, action_tertiary_box_),
            }),
            text(" Connection:") | bold | color(label_color),
            remote_actions,
        });
    }

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

    if (mode_ == SidebarMode::Actions) {
        return vbox({
            text(" Group:") | bold | color(label_color),
            hbox({
                RenderFlatActionButton("New", ButtonRole::Primary, group_new_box_),
                text(" "),
                RenderFlatActionButton("Rename", ButtonRole::Utility, group_rename_box_),
                text(" "),
                RenderFlatActionButton("Delete", ButtonRole::Danger, group_delete_box_),
            }),
            hbox({
                RenderFlatActionButton("Merge", ButtonRole::Warning, group_merge_box_),
            }),
            text(" Pages:") | bold | color(label_color),
            hbox({
                RenderFlatActionButton("New", ButtonRole::Primary, page_new_box_),
                text(" "),
                RenderFlatActionButton("Rename", ButtonRole::Utility, page_rename_box_),
                text(" "),
                RenderFlatActionButton("Delete", ButtonRole::Danger, page_delete_box_),
            }),
            text(" Actions:") | bold | color(label_color),
            hbox({
                RenderFlatActionButton("Add", ButtonRole::Primary, item_add_box_),
                text(" "),
                RenderFlatActionButton("Edit", ButtonRole::Utility, item_edit_box_),
                text(" "),
                RenderFlatActionButton("Delete", ButtonRole::Danger, item_delete_box_),
            }),
            hbox({
                RenderFlatActionButton("Move Up", ButtonRole::Navigation, item_move_up_box_),
                text(" "),
                RenderFlatActionButton("Move Down", ButtonRole::Navigation, item_move_down_box_),
            }),
        });
    }

    return hbox({
        RenderFlatActionButton("Files..", ButtonRole::Primary, action_primary_box_),
        text(" "),
        RenderFlatActionButton("Copy Path", ButtonRole::Primary, action_secondary_box_),
    });
}

bool SidebarPanel::HandleActionButtonMouse(const ftxui::Mouse& mouse) {
    if (mode_ == SidebarMode::Actions) {
        if (on_action_command_) {
            if (group_new_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::NewGroup);
                return true;
            }
            if (group_rename_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::RenameGroup);
                return true;
            }
            if (group_delete_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::DeleteGroup);
                return true;
            }
            if (group_merge_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::MergeGroup);
                return true;
            }
            if (page_new_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::NewPage);
                return true;
            }
            if (page_rename_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::RenamePage);
                return true;
            }
            if (page_delete_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::DeletePage);
                return true;
            }
            if (item_add_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::AddAction);
                return true;
            }
            if (item_edit_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::EditAction);
                return true;
            }
            if (item_delete_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::DeleteAction);
                return true;
            }
            if (item_move_up_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::MoveActionUp);
                return true;
            }
            if (item_move_down_box_.Contain(mouse.x, mouse.y)) {
                on_action_command_(ActionCommand::MoveActionDown);
                return true;
            }
        }
        return false;
    }
    if (action_primary_box_.Contain(mouse.x, mouse.y)) {
        switch (mode_) {
            case SidebarMode::Favorites:
                RemoveSelectedFavorite();
                return true;
            case SidebarMode::Opened:
                CloseSelectedOpenedFile();
                return true;
            case SidebarMode::Folder:
                OpenFilesModal();
                return true;
            case SidebarMode::Projects:
                if (selected_entry_ < static_cast<int>(entry_ids_.size())) {
                    const bool has_valid_remote = selected_entry_ < static_cast<int>(project_entries_.size()) &&
                        project_entries_[selected_entry_].has_remote &&
                        !project_entries_[selected_entry_].remote_missing;
                    if (has_valid_remote && on_project_remote_) {
                        on_project_remote_(entry_ids_[selected_entry_]);
                    } else if (on_project_assign_remote_) {
                        on_project_assign_remote_(entry_ids_[selected_entry_]);
                    }
                }
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
            case SidebarMode::Folder:
                CopySelectedProjectPath();
                return true;
            case SidebarMode::Projects:
                if (on_project_add_) {
                    on_project_add_(current_path_);
                }
                return true;
        }
    }

    if (mode_ == SidebarMode::Projects && project_rename_box_.Contain(mouse.x, mouse.y)) {
        if (on_project_rename_ && selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(entry_ids_.size())) {
            on_project_rename_(entry_ids_[selected_entry_]);
        }
        return true;
    }

    if (action_tertiary_box_.Contain(mouse.x, mouse.y) && mode_ == SidebarMode::Projects) {
        if (on_project_remove_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            on_project_remove_(entry_ids_[selected_entry_]);
        }
        return true;
    }

    if (action_quaternary_box_.Contain(mouse.x, mouse.y) && mode_ == SidebarMode::Projects) {
        if (on_project_edit_remote_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            on_project_edit_remote_(entry_ids_[selected_entry_]);
        }
        return true;
    }

    if (action_quinary_box_.Contain(mouse.x, mouse.y) && mode_ == SidebarMode::Projects) {
        if (on_project_unlink_remote_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            on_project_unlink_remote_(entry_ids_[selected_entry_]);
        }
        return true;
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
    if (mode_ != SidebarMode::Folder || selected_entry_ < 0 ||
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
    entry_ids_.clear();
    project_entries_.clear();
    widest_label_ = 0;

    switch (mode_) {
        case SidebarMode::Opened:
            RebuildOpenedEntries();
            break;
        case SidebarMode::Folder:
            RebuildProjectEntries();
            break;
        case SidebarMode::Projects:
            RebuildProjectsEntries();
            break;
        case SidebarMode::Favorites:
            RebuildFavoriteEntries();
            break;
        case SidebarMode::Actions:
            RebuildActionEntries();
            break;
    }

    if (entry_labels_.empty()) {
        const std::string placeholder =
            mode_ == SidebarMode::Opened ? " No opened files" :
            mode_ == SidebarMode::Favorites ? " No favorites" :
            mode_ == SidebarMode::Projects ? " No projects" :
            mode_ == SidebarMode::Actions ? " No action groups" :
            " <empty>";
        AddEntry({}, placeholder, EntryKind::Placeholder);
    }
    selected_entry_ = std::min(selected_entry_, static_cast<int>(entry_labels_.size()) - 1);
    EnsureSelectedEntryVisible();
}

void SidebarPanel::RebuildActionEntries() {
    for (const auto& group : action_group_entries_) {
        AddEntry({}, std::string(group.project ? "◆ " : "  ") + group.name,
            EntryKind::ActionGroup);
        entry_ids_.back() = group.id;
    }
    const auto selected = std::find(entry_ids_.begin(), entry_ids_.end(),
        selected_action_group_id_);
    if (selected != entry_ids_.end()) {
        selected_entry_ = static_cast<int>(std::distance(entry_ids_.begin(), selected));
    }
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
    if (current_path_ != project_root_ && !parent.empty() && parent != current_path_) {
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
        const std::string filename = Utf8FromPath(favorite_path.filename());
        AddEntry(
            favorite_path,
            " " + (filename.empty() ? Utf8FromPath(favorite_path) : filename),
            EntryKind::FavoriteFile);
    }
}

void SidebarPanel::RebuildProjectsEntries() {
    if (!projects_provider_) {
        return;
    }
    project_entries_ = projects_provider_();
    for (const ProjectEntry& project : project_entries_) {
        std::string label = project.active ? "> " : "  ";
        label += project.name.empty() ? DisplayName(project.root) : project.name;
        if (project.missing) {
            label += " [missing]";
        } else if (project.remote_missing) {
            label += " [remote missing]";
        } else if (project.has_remote) {
            label += " [remote]";
        }
        AddEntry(project.root, std::move(label), EntryKind::Project);
        entry_ids_.back() = project.id;
    }
}

void SidebarPanel::AddEntry(std::filesystem::path path, std::string label, EntryKind kind) {
    std::replace_if(label.begin(), label.end(), [](char ch) {
        return ch == '\n' || ch == '\r' || ch == '\t';
    }, ' ');
    widest_label_ = std::max(widest_label_, ftxui::string_width(label));
    entry_paths_.push_back(std::move(path));
    entry_labels_.push_back(std::move(label));
    entry_kinds_.push_back(kind);
    entry_ids_.emplace_back();
}

void SidebarPanel::SetProjectRoot(const std::filesystem::path& root) {
    if (root.empty()) {
        return;
    }
    std::error_code error;
    project_root_ = std::filesystem::weakly_canonical(root, error);
    if (error) {
        project_root_ = root.lexically_normal();
    }
    current_path_ = project_root_;
    if (mode_ != SidebarMode::Projects) {
        selected_entry_ = 0;
        list_scroll_offset_ = 0;
        label_scroll_offset_ = 0;
    }
    RebuildEntries();
}

} // namespace textlt
