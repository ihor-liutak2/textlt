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
#include "ui_horizontal_scroll.hpp"
#include "ui_scrollbar.hpp"

namespace textlt {
namespace {

std::string DisplayName(const std::filesystem::path& path) {
    return Utf8FromPath(path.filename().empty() ? path : path.filename());
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

class SidebarActionContextLayer final : public ftxui::Node {
public:
    using Paint = std::function<void(ftxui::Screen&, const ftxui::Box&)>;

    SidebarActionContextLayer(ftxui::Element sidebar, Paint paint)
        : Node({std::move(sidebar)}), paint_(std::move(paint)) {}

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_.front()->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        Node::Render(screen);
        paint_(screen, box_);
    }

private:
    Paint paint_;
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
        if (sidebar_.mode_ != SidebarMode::Actions)
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
        const int row_limit = sidebar_.mode_ == SidebarMode::Actions
            ? box.y_max + 1 : box.y_max;
        for (int y = box.y_min; y < row_limit &&
             index < sidebar_.entry_labels_.size(); ++y, ++index) {
            AddRow(sidebar_.RenderEntry(index, width, theme_), y);
        }

        if (sidebar_.mode_ == SidebarMode::Actions) {
            sidebar_.scrollbar_drag_.reset();
            return;
        }

        const ScrollbarMetrics scrollbar(width, sidebar_.widest_label_, sidebar_.label_scroll_offset_);
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
            text(std::string(scrollbar.thumb_length, ' ')) | bgcolor(theme_.modal_selected_item_bg),
            track(width - scrollbar.thumb_start - scrollbar.thumb_length),
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
    const Theme* theme,
    GitManager* git_manager,
    FavoriteFilesProvider favorite_files_provider,
    RemoveFavoriteCallback on_remove_favorite,
    AddCurrentFavoriteCallback on_add_current_favorite,
    OpenFilesModalCallback on_open_files_modal,
    CopyPathCallback on_copy_path,
    ShortcutLabelProvider shortcut_label_provider,
    ActionNavigationCallback on_action_navigation,
    ActionCommandCallback on_action_command,
    ProjectsProvider projects_provider,
    ProjectIdCallback on_project_select,
    AddProjectCallback on_project_add,
    ProjectIdCallback on_project_rename,
    ProjectIdCallback on_project_remove,
    ProjectIdCallback on_project_remote,
    ProjectIdCallback on_project_assign_remote,
    ProjectIdCallback on_project_edit_remote,
    ProjectIdCallback on_project_unlink_remote,
    ContextCommandCallback on_context_command)
    : on_file_open_(std::move(on_file_open)),
      theme_(theme),
      git_manager_(git_manager),
      favorite_files_provider_(std::move(favorite_files_provider)),
      on_remove_favorite_(std::move(on_remove_favorite)),
      on_add_current_favorite_(std::move(on_add_current_favorite)),
      on_open_files_modal_(std::move(on_open_files_modal)),
      on_copy_path_(std::move(on_copy_path)),
      shortcut_label_provider_(std::move(shortcut_label_provider)),
      on_action_navigation_(std::move(on_action_navigation)),
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
      on_context_command_(std::move(on_context_command)),
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
    const ftxui::Color sidebar_text = FileListTextColor(theme);

    Element panel = vbox({
        mode_ == SidebarMode::Actions
            ? emptyElement()
            : hbox({
                  RenderModeButton("Folder", SidebarMode::Folder, project_tab_box_),
                  RenderModeButton("Favs", SidebarMode::Favorites, favorites_tab_box_),
                  RenderModeButton("Proj", SidebarMode::Projects, projects_tab_box_),
              }),
        mode_ == SidebarMode::Actions ? emptyElement() : text(""),
        mode_ == SidebarMode::Folder
            ? text(" " + DisplayName(project_root_) + "/") | bold | color(sidebar_text)
            : emptyElement(),
        RenderActionRow(),
        text(""),
        std::make_shared<EntryList>(*this, theme) | yflex,
    });
    Element rendered = panel | reflect(panel_box_);
    if ((mode_ == SidebarMode::Actions && action_context_open_) || item_context_open_) {
        rendered = std::make_shared<SidebarActionContextLayer>(
            std::move(rendered),
            [this](ftxui::Screen& screen, const ftxui::Box& box) {
                if (item_context_open_) DrawItemContextMenu(screen, box);
                else DrawActionContextMenu(screen, box);
            });
    }
    return rendered;
}

ftxui::Element SidebarPanel::RenderEntry(size_t index, int width, const Theme& theme) const {
    using namespace ftxui;
    if (mode_ == SidebarMode::Actions &&
        entry_kinds_[index] == EntryKind::ActionSpacer) {
        return text("");
    }
    Color label_color = FileListTextColor(theme);
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
    if (mode_ == SidebarMode::Actions) {
        const bool selected_group = entry_kinds_[index] == EntryKind::ActionGroup &&
            entry_ids_[index] == selected_action_group_id_ &&
            selected_action_page_id_.empty();
        const bool active_group = entry_kinds_[index] == EntryKind::ActionGroup &&
            entry_ids_[index] == selected_action_group_id_ &&
            !selected_action_page_id_.empty();
        const bool selected_page = entry_kinds_[index] == EntryKind::ActionPage &&
            entry_parent_ids_[index] == selected_action_group_id_ &&
            entry_ids_[index] == selected_action_page_id_;
        if (selected_group || selected_page)
            return row | bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        if (active_group)
            return row | bold | color(theme.modal_accent);
    }
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
    project_tab_box_ = empty;
    projects_tab_box_ = empty;
    favorites_tab_box_ = empty;
    action_primary_box_ = empty;
    action_secondary_box_ = empty;
    action_tertiary_box_ = empty;
    action_quaternary_box_ = empty;
    action_quinary_box_ = empty;
    project_rename_box_ = empty;
    action_context_box_ = empty;
    action_context_item_boxes_.fill(empty);
    action_context_open_ = false;
    item_context_open_ = false;
    item_context_item_boxes_.fill(empty);
    item_context_box_ = empty;
}

bool SidebarPanel::OnEvent(ftxui::Event event) {
    if (mode_ == SidebarMode::Actions && event.is_mouse()) {
        const auto& mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::Right &&
            mouse.motion == ftxui::Mouse::Pressed) {
            const int entry = EntryIndexAtMouse(mouse);
            if (entry >= 0 &&
                (entry_kinds_[entry] == EntryKind::ActionGroup ||
                 entry_kinds_[entry] == EntryKind::ActionPage)) {
                OpenActionContextMenu(entry, mouse.x, mouse.y);
                return true;
            }
        }
    }
    if (mode_ == SidebarMode::Actions && action_context_open_ &&
        HandleActionContextMenuEvent(event)) {
        return true;
    }
    if (item_context_open_ && HandleItemContextMenuEvent(event)) return true;
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Right &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        mode_ != SidebarMode::Actions) {
        const int entry = EntryIndexAtMouse(event.mouse());
        const bool project_entry = entry >= 0 && mode_ == SidebarMode::Projects &&
            entry_kinds_[entry] == EntryKind::Project;
        const bool file_entry = entry >= 0 &&
            (mode_ == SidebarMode::Folder || mode_ == SidebarMode::Favorites) &&
            (entry_kinds_[entry] == EntryKind::Directory ||
             entry_kinds_[entry] == EntryKind::File ||
             entry_kinds_[entry] == EntryKind::FavoriteFile) &&
            !(mode_ == SidebarMode::Folder &&
              entry_paths_[entry] == current_path_.parent_path());
        if (project_entry || file_entry) {
            OpenItemContextMenu(entry, event.mouse().x, event.mouse().y);
            return true;
        }
    }
    if (mode_ != SidebarMode::Actions && event.is_mouse() &&
        HandleScrollbarMouse(event, event.mouse())) {
        return true;
    }

    if (event == ftxui::Event::ArrowDown) {
        if (mode_ == SidebarMode::Actions) {
            int next = selected_entry_ + 1;
            while (next < static_cast<int>(entry_labels_.size()) &&
                   entry_kinds_[next] == EntryKind::ActionSpacer) ++next;
            if (next < static_cast<int>(entry_labels_.size())) {
                selected_entry_ = next;
                EnsureSelectedEntryVisible();
            }
        } else if (selected_entry_ + 1 < static_cast<int>(entry_labels_.size())) {
            ++selected_entry_;
            EnsureSelectedEntryVisible();
        }
        if (mode_ == SidebarMode::Actions) OpenSelectedEntry();
        FocusMenu();
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (mode_ == SidebarMode::Actions) {
            int next = selected_entry_ - 1;
            while (next >= 0 &&
                   entry_kinds_[next] == EntryKind::ActionSpacer) --next;
            if (next >= 0) {
                selected_entry_ = next;
                EnsureSelectedEntryVisible();
            }
        } else if (selected_entry_ > 0) {
            --selected_entry_;
            EnsureSelectedEntryVisible();
        }
        if (mode_ == SidebarMode::Actions) OpenSelectedEntry();
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

    if (mode_ != SidebarMode::Actions &&
        event.is_mouse() &&
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
            if (mode_ == SidebarMode::Actions &&
                entry_kinds_[clicked_entry] == EntryKind::ActionSpacer)
                return true;
            const auto now = std::chrono::steady_clock::now();
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_click_time_).count();

            selected_entry_ = clicked_entry;
            EnsureSelectedEntryVisible();
            FocusMenu();
            if (mode_ == SidebarMode::Favorites ||
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
    const std::string& selected_id, const std::string& selected_page_id) {
    action_group_entries_ = std::move(groups);
    selected_action_group_id_ = selected_id;
    selected_action_page_id_ = selected_page_id;
    if (mode_ == SidebarMode::Actions) RebuildEntries();
}

void SidebarPanel::SetMode(SidebarMode mode) {
    if (mode_ == mode) {
        return;
    }

    mode_ = mode;
    action_context_open_ = false;
    item_context_open_ = false;
    action_context_item_boxes_ = {};
    selected_entry_ = 0;
    list_scroll_offset_ = 0;
    label_scroll_offset_ = 0;
    scrollbar_drag_.reset();
    last_clicked_entry_ = -1;
    last_click_time_ = {};
    RebuildEntries();
}

void SidebarPanel::OpenItemContextMenu(int entry_index, int mouse_x, int mouse_y) {
    selected_entry_ = entry_index;
    item_context_selected_ = 0;
    item_context_anchor_x_ = mouse_x;
    item_context_anchor_y_ = mouse_y;
    item_context_commands_.clear();
    if (mode_ == SidebarMode::Projects) {
        item_context_commands_ = {
            ContextCommand::RenameProject, ContextCommand::RemoveProject};
    } else if (mode_ == SidebarMode::Favorites) {
        item_context_commands_ = {
            ContextCommand::RemoveFavorite, ContextCommand::Rename,
            ContextCommand::CopyPath};
    } else {
        item_context_commands_ = {
            ContextCommand::Copy, ContextCommand::Cut, ContextCommand::Paste,
            ContextCommand::Rename, ContextCommand::CopyPath,
            ContextCommand::Delete};
        if (entry_kinds_[static_cast<size_t>(entry_index)] == EntryKind::File)
            item_context_commands_.push_back(ContextCommand::SendTo);
        if (mode_ == SidebarMode::Folder &&
            entry_kinds_[static_cast<size_t>(entry_index)] == EntryKind::File) {
            item_context_commands_.push_back(ContextCommand::AddFavorite);
        }
    }
    item_context_open_ = true;
    FocusMenu();
}

void SidebarPanel::DrawItemContextMenu(
    ftxui::Screen& screen, const ftxui::Box& sidebar_box) {
    using namespace ftxui;
    if (!item_context_open_ || item_context_commands_.empty()) return;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const auto label_for = [](ContextCommand command) {
        switch (command) {
            case ContextCommand::Copy: return "Copy";
            case ContextCommand::Cut: return "Cut";
            case ContextCommand::Paste: return "Paste";
            case ContextCommand::Rename:
            case ContextCommand::RenameProject: return "Rename";
            case ContextCommand::CopyPath: return "Copy Path";
            case ContextCommand::Delete: return "Delete";
            case ContextCommand::RemoveProject:
            case ContextCommand::RemoveFavorite: return "Remove";
            case ContextCommand::SendTo: return "Send To";
            case ContextCommand::AddFavorite: return "Add Favorite";
        }
        return "";
    };
    const int menu_width = 16;
    const int menu_height = static_cast<int>(item_context_commands_.size()) + 2;
    const int sidebar_width = sidebar_box.x_max - sidebar_box.x_min + 1;
    const int sidebar_height = sidebar_box.y_max - sidebar_box.y_min + 1;
    if (sidebar_width < menu_width || sidebar_height < menu_height) {
        item_context_box_ = {0, -1, 0, -1};
        item_context_item_boxes_.fill({0, -1, 0, -1});
        return;
    }
    int x = item_context_anchor_x_ + 1;
    if (x + menu_width - 1 > sidebar_box.x_max) x = item_context_anchor_x_ - menu_width;
    x = std::clamp(x, sidebar_box.x_min, sidebar_box.x_max - menu_width + 1);
    const int y = std::clamp(item_context_anchor_y_, sidebar_box.y_min,
        sidebar_box.y_max - menu_height + 1);
    item_context_box_ = {x, x + menu_width - 1, y, y + menu_height - 1};
    item_context_item_boxes_.fill({0, -1, 0, -1});
    Elements rows;
    for (size_t index = 0; index < item_context_commands_.size(); ++index) {
        item_context_item_boxes_[index] = {
            x + 1, x + menu_width - 2,
            y + 1 + static_cast<int>(index), y + 1 + static_cast<int>(index)};
        Element row = hbox({
            text(" " + std::string(label_for(item_context_commands_[index])) + " "),
            filler()});
        row = index == item_context_selected_
            ? row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : row | color(theme.modal_text_color);
        rows.push_back(std::move(row));
    }
    Element menu = vbox(std::move(rows)) | borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.menu_background) | clear_under;
    menu->ComputeRequirement();
    menu->SetBox(item_context_box_);
    menu->Render(screen);
}

bool SidebarPanel::HandleItemContextMenuEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        item_context_open_ = false;
        return true;
    }
    const int last = static_cast<int>(item_context_commands_.size()) - 1;
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
        const int delta = event == ftxui::Event::ArrowDown ? 1 : -1;
        item_context_selected_ = static_cast<size_t>(std::clamp(
            static_cast<int>(item_context_selected_) + delta, 0, std::max(0, last)));
        return true;
    }
    if (event == ftxui::Event::Return) {
        ExecuteItemContextCommand(item_context_selected_);
        return true;
    }
    if (!event.is_mouse()) return true;
    const auto mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed)
        return true;
    for (size_t index = 0; index < item_context_commands_.size(); ++index) {
        if (item_context_item_boxes_[index].Contain(mouse.x, mouse.y)) {
            ExecuteItemContextCommand(index);
            return true;
        }
    }
    item_context_open_ = false;
    return false;
}

void SidebarPanel::ExecuteItemContextCommand(size_t command_index) {
    if (command_index >= item_context_commands_.size()) return;
    const ContextCommand command = item_context_commands_[command_index];
    const std::filesystem::path path =
        selected_entry_ >= 0 && selected_entry_ < static_cast<int>(entry_paths_.size())
            ? entry_paths_[static_cast<size_t>(selected_entry_)] : std::filesystem::path{};
    const std::string id =
        selected_entry_ >= 0 && selected_entry_ < static_cast<int>(entry_ids_.size())
            ? entry_ids_[static_cast<size_t>(selected_entry_)] : std::string{};
    item_context_open_ = false;
    item_context_item_boxes_.fill({0, -1, 0, -1});
    if (on_context_command_) on_context_command_(command, path, id);
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
        if (on_action_navigation_ && selected_entry_ < static_cast<int>(entry_ids_.size())) {
            selected_action_group_id_ = entry_ids_[selected_entry_];
            selected_action_page_id_.clear();
            on_action_navigation_(selected_action_group_id_, {});
        }
        return;
    }
    if (selected_kind == EntryKind::ActionPage) {
        if (on_action_navigation_ &&
            selected_entry_ < static_cast<int>(entry_ids_.size()) &&
            selected_entry_ < static_cast<int>(entry_parent_ids_.size())) {
            selected_action_group_id_ = entry_parent_ids_[selected_entry_];
            selected_action_page_id_ = entry_ids_[selected_entry_];
            on_action_navigation_(selected_action_group_id_,
                selected_action_page_id_);
        }
        return;
    }
    std::error_code error;
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

void SidebarPanel::OpenActionContextMenu(
    int entry_index, int mouse_x, int mouse_y) {
    if (entry_index < 0 ||
        entry_index >= static_cast<int>(entry_kinds_.size()) ||
        (entry_kinds_[entry_index] != EntryKind::ActionGroup &&
         entry_kinds_[entry_index] != EntryKind::ActionPage)) {
        return;
    }
    selected_entry_ = entry_index;
    action_context_page_ = entry_kinds_[entry_index] == EntryKind::ActionPage;
    action_context_selected_ = 0;
    action_context_anchor_x_ = mouse_x;
    action_context_anchor_y_ = mouse_y;
    OpenSelectedEntry();
    action_context_open_ = true;
    FocusMenu();
}

void SidebarPanel::DrawActionContextMenu(
    ftxui::Screen& screen, const ftxui::Box& sidebar_box) {
    using namespace ftxui;
    if (!action_context_open_) return;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::array<std::string, 3> labels = {"New", "Rename", "Delete"};
    constexpr int menu_width = 12;
    constexpr int menu_height = 5;
    const int sidebar_width = sidebar_box.x_max - sidebar_box.x_min + 1;
    const int sidebar_height = sidebar_box.y_max - sidebar_box.y_min + 1;
    if (sidebar_width < menu_width || sidebar_height < menu_height) {
        action_context_box_ = {0, -1, 0, -1};
        action_context_item_boxes_ = {};
        return;
    }
    int x = action_context_anchor_x_ + 1;
    if (x + menu_width - 1 > sidebar_box.x_max)
        x = action_context_anchor_x_ - menu_width;
    x = std::clamp(x, sidebar_box.x_min,
        sidebar_box.x_max - menu_width + 1);
    const int y = std::clamp(action_context_anchor_y_, sidebar_box.y_min,
        sidebar_box.y_max - menu_height + 1);
    action_context_box_ = {
        x, x + menu_width - 1, y, y + menu_height - 1};
    for (size_t index = 0; index < action_context_item_boxes_.size(); ++index) {
        action_context_item_boxes_[index] = {
            x + 1, x + menu_width - 2,
            y + 1 + static_cast<int>(index),
            y + 1 + static_cast<int>(index)};
    }
    Elements rows;
    for (size_t index = 0; index < labels.size(); ++index) {
        Element row = hbox({
            text(" " + labels[index] + " "),
            filler(),
        });
        if (index == action_context_selected_) {
            row = row | bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        } else {
            row = row | color(theme.modal_text_color);
        }
        rows.push_back(row);
    }
    Element menu = vbox(std::move(rows)) |
        borderStyled(ftxui::LIGHT, theme.modal_border) |
        bgcolor(theme.menu_background) | clear_under;
    menu->ComputeRequirement();
    menu->SetBox(action_context_box_);
    menu->Render(screen);
}

bool SidebarPanel::HandleActionContextMenuEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        action_context_open_ = false;
        return true;
    }
    if (event == ftxui::Event::ArrowUp ||
        event == ftxui::Event::ArrowDown) {
        const int delta = event == ftxui::Event::ArrowDown ? 1 : -1;
        action_context_selected_ = static_cast<size_t>(
            std::clamp(static_cast<int>(action_context_selected_) + delta, 0, 2));
        return true;
    }
    if (event == ftxui::Event::Return) {
        ExecuteActionContextCommand(action_context_selected_);
        return true;
    }
    if (!event.is_mouse()) return true;
    const ftxui::Mouse mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) return true;
    for (size_t index = 0; index < action_context_item_boxes_.size(); ++index) {
        if (action_context_item_boxes_[index].Contain(mouse.x, mouse.y)) {
            ExecuteActionContextCommand(index);
            return true;
        }
    }
    action_context_open_ = false;
    action_context_item_boxes_ = {};
    return false;
}

void SidebarPanel::ExecuteActionContextCommand(size_t command_index) {
    if (command_index >= 3) return;
    const ActionCommand command = action_context_page_
        ? (command_index == 0 ? ActionCommand::NewPage
           : command_index == 1 ? ActionCommand::RenamePage
                                : ActionCommand::DeletePage)
        : (command_index == 0 ? ActionCommand::NewGroup
           : command_index == 1 ? ActionCommand::RenameGroup
                                : ActionCommand::DeleteGroup);
    action_context_open_ = false;
    action_context_item_boxes_ = {};
    if (on_action_command_) on_action_command_(command);
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

    const ScrollbarMetrics scrollbar(BoxWidth(scrollbar_box_), widest_label_, label_scroll_offset_);
    if (scrollbar.length == 0 || scrollbar.max_offset == 0) {
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
        if (relative_to_thumb >= 0 && relative_to_thumb < scrollbar.thumb_length) {
            scrollbar_grab_column_ = relative_to_thumb;
            return true;
        }
        scrollbar_grab_column_ = scrollbar.thumb_length / 2;
    } else if (!scrollbar_drag_ || mouse.motion != Mouse::Moved) {
        return false;
    }

    const int travel = scrollbar.Travel();
    const int thumb_start = std::clamp(column - scrollbar_grab_column_, 0, travel);
    if (thumb_start != scrollbar.thumb_start) {
        label_scroll_offset_ = scrollbar.OffsetForThumb(thumb_start);
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
    const Color label_color = FileListTextColor(theme);

    project_rename_box_ = {1, 0, 1, 0};

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

    if (mode_ == SidebarMode::Actions) {
        return text("Groups / Pages") | bold | color(label_color);
    }

    return hbox({
        RenderFlatActionButton("Files..", ButtonRole::Primary, action_primary_box_),
        text(" "),
        RenderFlatActionButton("Copy Path", ButtonRole::Primary, action_secondary_box_),
    });
}

bool SidebarPanel::HandleActionButtonMouse(const ftxui::Mouse& mouse) {
    if (action_primary_box_.Contain(mouse.x, mouse.y)) {
        switch (mode_) {
            case SidebarMode::Favorites:
                RemoveSelectedFavorite();
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
        if (on_project_remove_ && selected_entry_ >= 0 &&
            selected_entry_ < static_cast<int>(entry_ids_.size())) {
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
    entry_parent_ids_.clear();
    project_entries_.clear();
    widest_label_ = 0;

    switch (mode_) {
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
        AddEntry({}, "", EntryKind::ActionSpacer);
        AddEntry({}, group.name, EntryKind::ActionGroup);
        entry_ids_.back() = group.id;
        for (const auto& [page_id, page_name] : group.pages) {
            AddEntry({}, "    " + page_name, EntryKind::ActionPage);
            entry_ids_.back() = page_id;
            entry_parent_ids_.back() = group.id;
        }
    }
    for (size_t index = 0; index < entry_ids_.size(); ++index) {
        if (entry_kinds_[index] == EntryKind::ActionPage &&
            entry_parent_ids_[index] == selected_action_group_id_ &&
            entry_ids_[index] == selected_action_page_id_) {
            selected_entry_ = static_cast<int>(index);
            return;
        }
    }
    for (size_t index = 0; index < entry_ids_.size(); ++index) {
        if (entry_kinds_[index] == EntryKind::ActionGroup &&
            entry_ids_[index] == selected_action_group_id_) {
            selected_entry_ = static_cast<int>(index);
            return;
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
    entry_parent_ids_.emplace_back();
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
